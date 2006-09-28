/*
 *  sata_nv.c - NVIDIA nForce SATA
 *
 *  Copyright 2004 NVIDIA Corp.  All rights reserved.
 *  Copyright 2004 Andrew Chew
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *  libata documentation is available via 'make {ps|pdf}docs',
 *  as Documentation/DocBook/libata.*
 *
 *  No hardware documentation available outside of NVIDIA.
 *  This driver programs the NVIDIA SATA controller in a similar
 *  fashion as with other PCI IDE BMDMA controllers, with a few
 *  NV-specific details such as register offsets, SATA phy location,
 *  hotplug info, etc.
 *
 *  CK804/MCP04 controllers support an alternate programming interface
 *  similar to the ADMA specification (with some modifications).
 *  This allows the use of NCQ. Non-DMA-mapped ATA commands are still
 *  sent through the legacy interface.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <linux/libata.h>

#define DRV_NAME			"sata_nv"
#define DRV_VERSION			"3.1"

#define NV_ADMA_DMA_BOUNDARY		0xffffffffUL

enum {
	NV_PORTS			= 2,
	NV_PIO_MASK			= 0x1f,
	NV_MWDMA_MASK			= 0x07,
	NV_UDMA_MASK			= 0x7f,
	NV_PORT0_SCR_REG_OFFSET		= 0x00,
	NV_PORT1_SCR_REG_OFFSET		= 0x40,

	/* INT_STATUS/ENABLE */
	NV_INT_STATUS			= 0x10,
	NV_INT_ENABLE			= 0x11,
	NV_INT_STATUS_CK804		= 0x440,
	NV_INT_ENABLE_CK804		= 0x441,

	/* INT_STATUS/ENABLE bits */
	NV_INT_DEV			= 0x01,
	NV_INT_PM			= 0x02,
	NV_INT_ADDED			= 0x04,
	NV_INT_REMOVED			= 0x08,

	NV_INT_PORT_SHIFT		= 4,	/* each port occupies 4 bits */

	NV_INT_ALL			= 0x0f,
	NV_INT_MASK			= NV_INT_DEV |
					  NV_INT_ADDED | NV_INT_REMOVED,

	/* INT_CONFIG */
	NV_INT_CONFIG			= 0x12,
	NV_INT_CONFIG_METHD		= 0x01, // 0 = INT, 1 = SMI

	// For PCI config register 20
	NV_MCP_SATA_CFG_20		= 0x50,
	NV_MCP_SATA_CFG_20_SATA_SPACE_EN = 0x04,
	NV_MCP_SATA_CFG_20_PORT0_EN	= (1 << 17),
	NV_MCP_SATA_CFG_20_PORT1_EN	= (1 << 16),
	NV_MCP_SATA_CFG_20_PORT0_PWB_EN	= (1 << 14),
	NV_MCP_SATA_CFG_20_PORT1_PWB_EN	= (1 << 12),

	NV_ADMA_MAX_CPBS		= 32,
	NV_ADMA_CPB_SZ			= 128,
	NV_ADMA_APRD_SZ			= 16,
	NV_ADMA_SGTBL_LEN		= (1024 - NV_ADMA_CPB_SZ) /
					   NV_ADMA_APRD_SZ,
	NV_ADMA_SGTBL_TOTAL_LEN		= NV_ADMA_SGTBL_LEN + 5,
	NV_ADMA_SGTBL_SZ                = NV_ADMA_SGTBL_LEN * NV_ADMA_APRD_SZ,
	NV_ADMA_PORT_PRIV_DMA_SZ        = NV_ADMA_MAX_CPBS *
					   (NV_ADMA_CPB_SZ + NV_ADMA_SGTBL_SZ),

	/* BAR5 offset to ADMA general registers */
	NV_ADMA_GEN			= 0x400,
	NV_ADMA_GEN_CTL			= 0x00,
	NV_ADMA_NOTIFIER_CLEAR		= 0x30,

	/* BAR5 offset to ADMA ports */
	NV_ADMA_PORT			= 0x480,

	/* size of ADMA port register space  */
	NV_ADMA_PORT_SIZE		= 0x100,

	/* ADMA port registers */
	NV_ADMA_CTL			= 0x40,
	NV_ADMA_CPB_COUNT		= 0x42,
	NV_ADMA_NEXT_CPB_IDX		= 0x43,
	NV_ADMA_STAT			= 0x44,
	NV_ADMA_CPB_BASE_LOW		= 0x48,
	NV_ADMA_CPB_BASE_HIGH		= 0x4C,
	NV_ADMA_APPEND			= 0x50,
	NV_ADMA_NOTIFIER		= 0x68,
	NV_ADMA_NOTIFIER_ERROR		= 0x6C,

	/* NV_ADMA_CTL register bits */
	NV_ADMA_CTL_HOTPLUG_IEN		= (1 << 0),
	NV_ADMA_CTL_CHANNEL_RESET	= (1 << 5),
	NV_ADMA_CTL_GO			= (1 << 7),
	NV_ADMA_CTL_AIEN		= (1 << 8),
	NV_ADMA_CTL_READ_NON_COHERENT	= (1 << 11),
	NV_ADMA_CTL_WRITE_NON_COHERENT	= (1 << 12),

	/* CPB response flag bits */
	NV_CPB_RESP_DONE		= (1 << 0),
	NV_CPB_RESP_ATA_ERR		= (1 << 3),
	NV_CPB_RESP_CMD_ERR		= (1 << 4),
	NV_CPB_RESP_CPB_ERR		= (1 << 7),

	/* CPB control flag bits */
	NV_CPB_CTL_CPB_VALID		= (1 << 0),
	NV_CPB_CTL_QUEUE		= (1 << 1),
	NV_CPB_CTL_APRD_VALID		= (1 << 2),
	NV_CPB_CTL_IEN			= (1 << 3),
	NV_CPB_CTL_FPDMA		= (1 << 4),

	/* APRD flags */
	NV_APRD_WRITE			= (1 << 1),
	NV_APRD_END			= (1 << 2),
	NV_APRD_CONT			= (1 << 3),

	/* NV_ADMA_STAT flags */
	NV_ADMA_STAT_TIMEOUT		= (1 << 0),
	NV_ADMA_STAT_HOTUNPLUG		= (1 << 1),
	NV_ADMA_STAT_HOTPLUG		= (1 << 2),
	NV_ADMA_STAT_CPBERR		= (1 << 4),
	NV_ADMA_STAT_SERROR		= (1 << 5),
	NV_ADMA_STAT_CMD_COMPLETE	= (1 << 6),
	NV_ADMA_STAT_IDLE		= (1 << 8),
	NV_ADMA_STAT_LEGACY		= (1 << 9),
	NV_ADMA_STAT_STOPPED		= (1 << 10),
	NV_ADMA_STAT_DONE		= (1 << 12),
	NV_ADMA_STAT_ERR		= NV_ADMA_STAT_CPBERR |
	 				  NV_ADMA_STAT_TIMEOUT,

	/* port flags */
	NV_ADMA_PORT_REGISTER_MODE	= (1 << 0),

};

/* ADMA Physical Region Descriptor - one SG segment */
struct nv_adma_prd {
	__le64			addr;
	__le32			len;
	u8			flags;
	u8			packet_len;
	__le16			reserved;
};

enum nv_adma_regbits {
	CMDEND	= (1 << 15),		/* end of command list */
	WNB	= (1 << 14),		/* wait-not-BSY */
	IGN	= (1 << 13),		/* ignore this entry */
	CS1n	= (1 << (4 + 8)),	/* std. PATA signals follow... */
	DA2	= (1 << (2 + 8)),
	DA1	= (1 << (1 + 8)),
	DA0	= (1 << (0 + 8)),
};

/* ADMA Command Parameter Block
   The first 5 SG segments are stored inside the Command Parameter Block itself.
   If there are more than 5 segments the remainder are stored in a separate
   memory area indicated by next_aprd. */
struct nv_adma_cpb {
	u8			resp_flags;    /* 0 */
	u8			reserved1;     /* 1 */
	u8			ctl_flags;     /* 2 */
	/* len is length of taskfile in 64 bit words */
 	u8			len;           /* 3  */
	u8			tag;           /* 4 */
	u8			next_cpb_idx;  /* 5 */
	__le16			reserved2;     /* 6-7 */
	__le16			tf[12];        /* 8-31 */
	struct nv_adma_prd	aprd[5];       /* 32-111 */
	__le64			next_aprd;     /* 112-119 */
	__le64			reserved3;     /* 120-127 */
};


struct nv_adma_port_priv {
	struct nv_adma_cpb	*cpb;
	dma_addr_t		cpb_dma;
	struct nv_adma_prd	*aprd;
	dma_addr_t		aprd_dma;
	u8			flags;
};

#define NV_ADMA_CHECK_INTR(GCTL, PORT) ((GCTL) & ( 1 << (19 + (12 * (PORT)))))

static int nv_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static void nv_ck804_host_stop(struct ata_host *host);
static irqreturn_t nv_generic_interrupt(int irq, void *dev_instance);
static irqreturn_t nv_nf2_interrupt(int irq, void *dev_instance);
static irqreturn_t nv_ck804_interrupt(int irq, void *dev_instance);
static u32 nv_scr_read (struct ata_port *ap, unsigned int sc_reg);
static void nv_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val);

static void nv_nf2_freeze(struct ata_port *ap);
static void nv_nf2_thaw(struct ata_port *ap);
static void nv_ck804_freeze(struct ata_port *ap);
static void nv_ck804_thaw(struct ata_port *ap);
static void nv_error_handler(struct ata_port *ap);
static int nv_adma_slave_config(struct scsi_device *sdev);
static void nv_adma_qc_prep(struct ata_queued_cmd *qc);
static unsigned int nv_adma_qc_issue(struct ata_queued_cmd *qc);
static irqreturn_t nv_adma_interrupt(int irq, void *dev_instance);
static void nv_adma_irq_clear(struct ata_port *ap);
static int nv_adma_port_start(struct ata_port *ap);
static void nv_adma_port_stop(struct ata_port *ap);
static void nv_adma_error_handler(struct ata_port *ap);
static void nv_adma_host_stop(struct ata_host *host);
static void nv_adma_bmdma_setup(struct ata_queued_cmd *qc);
static void nv_adma_bmdma_start(struct ata_queued_cmd *qc);
static void nv_adma_bmdma_stop(struct ata_queued_cmd *qc);
static u8 nv_adma_bmdma_status(struct ata_port *ap);

enum nv_host_type
{
	GENERIC,
	NFORCE2,
	NFORCE3 = NFORCE2,	/* NF2 == NF3 as far as sata_nv is concerned */
	CK804,
	ADMA
};

static const struct pci_device_id nv_pci_tbl[] = {
	{ PCI_VDEVICE(NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE2S_SATA), NFORCE2 },
	{ PCI_VDEVICE(NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE3S_SATA), NFORCE3 },
	{ PCI_VDEVICE(NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE3S_SATA2), NFORCE3 },
	{ PCI_VDEVICE(NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_CK804_SATA), CK804 },
	{ PCI_VDEVICE(NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_CK804_SATA2), CK804 },
	{ PCI_VDEVICE(NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP04_SATA), CK804 },
	{ PCI_VDEVICE(NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP04_SATA2), CK804 },
	{ PCI_VDEVICE(NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP51_SATA), GENERIC },
	{ PCI_VDEVICE(NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP51_SATA2), GENERIC },
	{ PCI_VDEVICE(NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP55_SATA), GENERIC },
	{ PCI_VDEVICE(NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP55_SATA2), GENERIC },
	{ PCI_VDEVICE(NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP61_SATA), GENERIC },
	{ PCI_VDEVICE(NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP61_SATA2), GENERIC },
	{ PCI_VDEVICE(NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP61_SATA3), GENERIC },
	{ PCI_VDEVICE(NVIDIA, 0x045c), GENERIC }, /* MCP65 */
	{ PCI_VDEVICE(NVIDIA, 0x045d), GENERIC }, /* MCP65 */
	{ PCI_VDEVICE(NVIDIA, 0x045e), GENERIC }, /* MCP65 */
	{ PCI_VDEVICE(NVIDIA, 0x045f), GENERIC }, /* MCP65 */
	{ PCI_VDEVICE(NVIDIA, 0x0550), GENERIC }, /* MCP67 */
	{ PCI_VDEVICE(NVIDIA, 0x0551), GENERIC }, /* MCP67 */
	{ PCI_VDEVICE(NVIDIA, 0x0552), GENERIC }, /* MCP67 */
	{ PCI_VDEVICE(NVIDIA, 0x0553), GENERIC }, /* MCP67 */
	{ PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID,
		PCI_ANY_ID, PCI_ANY_ID,
		PCI_CLASS_STORAGE_IDE<<8, 0xffff00, GENERIC },
	{ PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID,
		PCI_ANY_ID, PCI_ANY_ID,
		PCI_CLASS_STORAGE_RAID<<8, 0xffff00, GENERIC },

	{ } /* terminate list */
};

static struct pci_driver nv_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= nv_pci_tbl,
	.probe			= nv_init_one,
	.remove			= ata_pci_remove_one,
};

static struct scsi_host_template nv_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_MAX_PRD,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.slave_destroy		= ata_scsi_slave_destroy,
	.bios_param		= ata_std_bios_param,
};

static struct scsi_host_template nv_adma_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.can_queue		= NV_ADMA_MAX_CPBS,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= NV_ADMA_SGTBL_TOTAL_LEN,
	.max_sectors		= ATA_MAX_SECTORS,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= NV_ADMA_DMA_BOUNDARY,
	.slave_configure	= nv_adma_slave_config,
	.slave_destroy		= ata_scsi_slave_destroy,
	.bios_param		= ata_std_bios_param,
};

static const struct ata_port_operations nv_generic_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.exec_command		= ata_exec_command,
	.check_status		= ata_check_status,
	.dev_select		= ata_std_dev_select,
	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler		= nv_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,
	.data_xfer		= ata_pio_data_xfer,
	.irq_handler		= nv_generic_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,
	.scr_read		= nv_scr_read,
	.scr_write		= nv_scr_write,
	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
	.host_stop		= ata_pci_host_stop,
};

static const struct ata_port_operations nv_nf2_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.exec_command		= ata_exec_command,
	.check_status		= ata_check_status,
	.dev_select		= ata_std_dev_select,
	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.freeze			= nv_nf2_freeze,
	.thaw			= nv_nf2_thaw,
	.error_handler		= nv_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,
	.data_xfer		= ata_pio_data_xfer,
	.irq_handler		= nv_nf2_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,
	.scr_read		= nv_scr_read,
	.scr_write		= nv_scr_write,
	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
	.host_stop		= ata_pci_host_stop,
};

static const struct ata_port_operations nv_ck804_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.exec_command		= ata_exec_command,
	.check_status		= ata_check_status,
	.dev_select		= ata_std_dev_select,
	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.freeze			= nv_ck804_freeze,
	.thaw			= nv_ck804_thaw,
	.error_handler		= nv_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,
	.data_xfer		= ata_pio_data_xfer,
	.irq_handler		= nv_ck804_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,
	.scr_read		= nv_scr_read,
	.scr_write		= nv_scr_write,
	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
	.host_stop		= nv_ck804_host_stop,
};

static const struct ata_port_operations nv_adma_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.exec_command		= ata_exec_command,
	.check_status		= ata_check_status,
	.dev_select		= ata_std_dev_select,
	.bmdma_setup		= nv_adma_bmdma_setup,
	.bmdma_start		= nv_adma_bmdma_start,
	.bmdma_stop		= nv_adma_bmdma_stop,
	.bmdma_status		= nv_adma_bmdma_status,
	.qc_prep		= nv_adma_qc_prep,
	.qc_issue		= nv_adma_qc_issue,
	.freeze			= nv_ck804_freeze,
	.thaw			= nv_ck804_thaw,
	.error_handler		= nv_adma_error_handler,
	.post_internal_cmd	= nv_adma_bmdma_stop,
	.data_xfer		= ata_mmio_data_xfer,
	.irq_handler		= nv_adma_interrupt,
	.irq_clear		= nv_adma_irq_clear,
	.scr_read		= nv_scr_read,
	.scr_write		= nv_scr_write,
	.port_start		= nv_adma_port_start,
	.port_stop		= nv_adma_port_stop,
	.host_stop		= nv_adma_host_stop,
};

static struct ata_port_info nv_port_info[] = {
	/* generic */
	{
		.sht		= &nv_sht,
		.flags		= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_HRST_TO_RESUME,
		.pio_mask	= NV_PIO_MASK,
		.mwdma_mask	= NV_MWDMA_MASK,
		.udma_mask	= NV_UDMA_MASK,
		.port_ops	= &nv_generic_ops,
	},
	/* nforce2/3 */
	{
		.sht		= &nv_sht,
		.flags		= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_HRST_TO_RESUME,
		.pio_mask	= NV_PIO_MASK,
		.mwdma_mask	= NV_MWDMA_MASK,
		.udma_mask	= NV_UDMA_MASK,
		.port_ops	= &nv_nf2_ops,
	},
	/* ck804 */
	{
		.sht		= &nv_sht,
		.flags		= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_HRST_TO_RESUME,
		.pio_mask	= NV_PIO_MASK,
		.mwdma_mask	= NV_MWDMA_MASK,
		.udma_mask	= NV_UDMA_MASK,
		.port_ops	= &nv_ck804_ops,
	},
	/* ADMA */
	{
		.sht		= &nv_adma_sht,
		.flags		= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_MMIO | ATA_FLAG_NCQ,
		.pio_mask	= NV_PIO_MASK,
		.mwdma_mask	= NV_MWDMA_MASK,
		.udma_mask	= NV_UDMA_MASK,
		.port_ops	= &nv_adma_ops,
	},
};

MODULE_AUTHOR("NVIDIA");
MODULE_DESCRIPTION("low-level driver for NVIDIA nForce SATA controller");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, nv_pci_tbl);
MODULE_VERSION(DRV_VERSION);

static int adma_enabled = 1;

static int nv_adma_slave_config(struct scsi_device *sdev)
{
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	u64 bounce_limit;
	unsigned long segment_boundary;
	unsigned short sg_tablesize;
	int rc;

	rc = ata_scsi_slave_config(sdev);

	if (sdev->id >= ATA_MAX_DEVICES || sdev->channel || sdev->lun)
		/* Not a proper libata device, ignore */
		return rc;

	if (ap->device[sdev->id].class == ATA_DEV_ATAPI) {
		/*
		 * NVIDIA reports that ADMA mode does not support ATAPI commands.
		 * Therefore ATAPI commands are sent through the legacy interface.
		 * However, the legacy interface only supports 32-bit DMA.
		 * Restrict DMA parameters as required by the legacy interface
		 * when an ATAPI device is connected.
		 */
		bounce_limit = ATA_DMA_MASK;
		segment_boundary = ATA_DMA_BOUNDARY;
		/* Subtract 1 since an extra entry may be needed for padding, see
		   libata-scsi.c */
		sg_tablesize = LIBATA_MAX_PRD - 1;
	}
	else {
		bounce_limit = *ap->dev->dma_mask;
		segment_boundary = NV_ADMA_DMA_BOUNDARY;
		sg_tablesize = NV_ADMA_SGTBL_TOTAL_LEN;
	}

	blk_queue_bounce_limit(sdev->request_queue, bounce_limit);
	blk_queue_segment_boundary(sdev->request_queue, segment_boundary);
	blk_queue_max_hw_segments(sdev->request_queue, sg_tablesize);
	ata_port_printk(ap, KERN_INFO,
		"bounce limit 0x%llX, segment boundary 0x%lX, hw segs %hu\n",
		(unsigned long long)bounce_limit, segment_boundary, sg_tablesize);
	return rc;
}

static unsigned int nv_adma_tf_to_cpb(struct ata_taskfile *tf, u16 *cpb)
{
	unsigned int idx = 0;

	cpb[idx++] = cpu_to_le16((ATA_REG_DEVICE << 8) | tf->device | WNB);

	if ((tf->flags & ATA_TFLAG_LBA48) == 0) {
		cpb[idx++] = cpu_to_le16(IGN);
		cpb[idx++] = cpu_to_le16(IGN);
		cpb[idx++] = cpu_to_le16(IGN);
		cpb[idx++] = cpu_to_le16(IGN);
		cpb[idx++] = cpu_to_le16(IGN);
	}
	else {
		cpb[idx++] = cpu_to_le16((ATA_REG_ERR   << 8) | tf->hob_feature);
		cpb[idx++] = cpu_to_le16((ATA_REG_NSECT << 8) | tf->hob_nsect);
		cpb[idx++] = cpu_to_le16((ATA_REG_LBAL  << 8) | tf->hob_lbal);
		cpb[idx++] = cpu_to_le16((ATA_REG_LBAM  << 8) | tf->hob_lbam);
		cpb[idx++] = cpu_to_le16((ATA_REG_LBAH  << 8) | tf->hob_lbah);
	}
	cpb[idx++] = cpu_to_le16((ATA_REG_ERR    << 8) | tf->feature);
	cpb[idx++] = cpu_to_le16((ATA_REG_NSECT  << 8) | tf->nsect);
	cpb[idx++] = cpu_to_le16((ATA_REG_LBAL   << 8) | tf->lbal);
	cpb[idx++] = cpu_to_le16((ATA_REG_LBAM   << 8) | tf->lbam);
	cpb[idx++] = cpu_to_le16((ATA_REG_LBAH   << 8) | tf->lbah);

	cpb[idx++] = cpu_to_le16((ATA_REG_CMD    << 8) | tf->command | CMDEND);

	return idx;
}

static inline void __iomem *__nv_adma_ctl_block(void __iomem *mmio,
					        unsigned int port_no)
{
	mmio += NV_ADMA_PORT + port_no * NV_ADMA_PORT_SIZE;
	return mmio;
}

static inline void __iomem *nv_adma_ctl_block(struct ata_port *ap)
{
	return __nv_adma_ctl_block(ap->host->mmio_base, ap->port_no);
}

static inline void __iomem *nv_adma_gen_block(struct ata_port *ap)
{
	return (ap->host->mmio_base + NV_ADMA_GEN);
}

static inline void __iomem *nv_adma_notifier_clear_block(struct ata_port *ap)
{
	return (nv_adma_gen_block(ap) + NV_ADMA_NOTIFIER_CLEAR + (4 * ap->port_no));
}

static void nv_adma_check_cpb(struct ata_port *ap, int cpb_num, int force_err)
{
	struct nv_adma_port_priv *pp = ap->private_data;
	int complete = 0, have_err = 0;
	u16 flags = pp->cpb[cpb_num].resp_flags;

	VPRINTK("CPB %d, flags=0x%x\n", cpb_num, flags);

	if (flags & NV_CPB_RESP_DONE) {
		VPRINTK("CPB flags done, flags=0x%x\n", flags);
		complete = 1;
	}
	if (flags & NV_CPB_RESP_ATA_ERR) {
		ata_port_printk(ap, KERN_ERR, "CPB flags ATA err, flags=0x%x\n", flags);
		have_err = 1;
		complete = 1;
	}
	if (flags & NV_CPB_RESP_CMD_ERR) {
		ata_port_printk(ap, KERN_ERR, "CPB flags CMD err, flags=0x%x\n", flags);
		have_err = 1;
		complete = 1;
	}
	if (flags & NV_CPB_RESP_CPB_ERR) {
		ata_port_printk(ap, KERN_ERR, "CPB flags CPB err, flags=0x%x\n", flags);
		have_err = 1;
		complete = 1;
	}
	if(complete || force_err)
	{
		struct ata_queued_cmd *qc = ata_qc_from_tag(ap, cpb_num);
		if(likely(qc)) {
			u8 ata_status = 0;
			/* Only use the ATA port status for non-NCQ commands.
			   For NCQ commands the current status may have nothing to do with
			   the command just completed. */
			if(qc->tf.protocol != ATA_PROT_NCQ)
				ata_status = readb(nv_adma_ctl_block(ap) + (ATA_REG_STATUS * 4));

			if(have_err || force_err)
				ata_status |= ATA_ERR;

			qc->err_mask |= ac_err_mask(ata_status);
			DPRINTK("Completing qc from tag %d with err_mask %u\n",cpb_num,
				qc->err_mask);
			ata_qc_complete(qc);
		}
	}
}

static irqreturn_t nv_adma_interrupt(int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	int i, handled = 0;

	spin_lock(&host->lock);

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		if (ap && !(ap->flags & ATA_FLAG_DISABLED)) {
			struct nv_adma_port_priv *pp = ap->private_data;
			void __iomem *mmio = nv_adma_ctl_block(ap);
			u16 status;
			u32 gen_ctl;
			int have_global_err = 0;
			u32 notifier, notifier_error;

			/* if in ATA register mode, use standard ata interrupt handler */
			if (pp->flags & NV_ADMA_PORT_REGISTER_MODE) {
				struct ata_queued_cmd *qc;
				VPRINTK("in ATA register mode\n");
				qc = ata_qc_from_tag(ap, ap->active_tag);
				if (qc && (!(qc->tf.flags & ATA_TFLAG_POLLING)))
					handled += ata_host_intr(ap, qc);
				else {
					/* No request pending?  Clear interrupt status
					   anyway, in case there's one pending. */
					ap->ops->check_status(ap);
					handled++;
				}
				continue;
			}

			notifier = readl(mmio + NV_ADMA_NOTIFIER);
			notifier_error = readl(mmio + NV_ADMA_NOTIFIER_ERROR);

			gen_ctl = readl(nv_adma_gen_block(ap) + NV_ADMA_GEN_CTL);

			/* Seems necessary to clear notifiers even when they were 0.
			   Otherwise we seem to stop receiving further interrupts.
			   Unsure why. */
			writel(notifier | notifier_error, nv_adma_notifier_clear_block(ap));

			if( !NV_ADMA_CHECK_INTR(gen_ctl, ap->port_no) && !notifier &&
			    !notifier_error)
				/* Nothing to do */
				continue;

			status = readw(mmio + NV_ADMA_STAT);

			/* Clear status. Ensure the controller sees the clearing before we start
			   looking at any of the CPB statuses, so that any CPB completions after
			   this point in the handler will raise another interrupt. */
			writew(status, mmio + NV_ADMA_STAT);
			readw(mmio + NV_ADMA_STAT); /* flush posted write */
			rmb();

			/* freeze if hotplugged */
			if (unlikely(status & (NV_ADMA_STAT_HOTPLUG | NV_ADMA_STAT_HOTUNPLUG))) {
				ata_port_printk(ap, KERN_NOTICE, "Hotplug event, freezing\n");
				ata_port_freeze(ap);
				handled++;
				continue;
			}

			if (status & NV_ADMA_STAT_TIMEOUT) {
				ata_port_printk(ap, KERN_ERR, "timeout, stat=0x%x\n", status);
				have_global_err = 1;
			}
			if (status & NV_ADMA_STAT_CPBERR) {
				ata_port_printk(ap, KERN_ERR, "CPB error, stat=0x%x\n", status);
				have_global_err = 1;
			}
			if ((status & NV_ADMA_STAT_DONE) || have_global_err) {
				/** Check CPBs for completed commands */

				if(ata_tag_valid(ap->active_tag))
					/* Non-NCQ command */
					nv_adma_check_cpb(ap, ap->active_tag, have_global_err ||
						(notifier_error & (1 << ap->active_tag)));
				else {
					int pos;
					u32 active = ap->sactive;
					while( (pos = ffs(active)) ) {
						pos--;
						nv_adma_check_cpb(ap, pos, have_global_err ||
							(notifier_error & (1 << pos)) );
						active &= ~(1 << pos );
					}
				}
			}

			handled++; /* irq handled if we got here */
		}
	}

	spin_unlock(&host->lock);

	return IRQ_RETVAL(handled);
}

static void nv_adma_irq_clear(struct ata_port *ap)
{
	void __iomem *mmio = nv_adma_ctl_block(ap);
	u16 status = readw(mmio + NV_ADMA_STAT);
	u32 notifier = readl(mmio + NV_ADMA_NOTIFIER);
	u32 notifier_error = readl(mmio + NV_ADMA_NOTIFIER_ERROR);

	/* clear ADMA status */
	writew(status, mmio + NV_ADMA_STAT);
	writel(notifier | notifier_error,
	       nv_adma_notifier_clear_block(ap));

	/** clear legacy status */
	ap->flags &= ~ATA_FLAG_MMIO;
	ata_bmdma_irq_clear(ap);
	ap->flags |= ATA_FLAG_MMIO;
}

static void nv_adma_bmdma_setup(struct ata_queued_cmd *qc)
{
	struct nv_adma_port_priv *pp = qc->ap->private_data;

	if(pp->flags & NV_ADMA_PORT_REGISTER_MODE) {
		WARN_ON(1);
		return;
	}

	qc->ap->flags &= ~ATA_FLAG_MMIO;
	ata_bmdma_setup(qc);
	qc->ap->flags |= ATA_FLAG_MMIO;
}

static void nv_adma_bmdma_start(struct ata_queued_cmd *qc)
{
	struct nv_adma_port_priv *pp = qc->ap->private_data;

	if(pp->flags & NV_ADMA_PORT_REGISTER_MODE) {
		WARN_ON(1);
		return;
	}

	qc->ap->flags &= ~ATA_FLAG_MMIO;
	ata_bmdma_start(qc);
	qc->ap->flags |= ATA_FLAG_MMIO;
}

static void nv_adma_bmdma_stop(struct ata_queued_cmd *qc)
{
	struct nv_adma_port_priv *pp = qc->ap->private_data;

	if(pp->flags & NV_ADMA_PORT_REGISTER_MODE)
		return;

	qc->ap->flags &= ~ATA_FLAG_MMIO;
	ata_bmdma_stop(qc);
	qc->ap->flags |= ATA_FLAG_MMIO;
}

static u8 nv_adma_bmdma_status(struct ata_port *ap)
{
	u8 status;
	struct nv_adma_port_priv *pp = ap->private_data;

	WARN_ON(pp->flags & NV_ADMA_PORT_REGISTER_MODE);

	ap->flags &= ~ATA_FLAG_MMIO;
	status = ata_bmdma_status(ap);
	ap->flags |= ATA_FLAG_MMIO;
	return status;
}

static void nv_adma_register_mode(struct ata_port *ap)
{
	void __iomem *mmio = nv_adma_ctl_block(ap);
	struct nv_adma_port_priv *pp = ap->private_data;
	u16 tmp;

	if (pp->flags & NV_ADMA_PORT_REGISTER_MODE)
		return;

	tmp = readw(mmio + NV_ADMA_CTL);
	writew(tmp & ~NV_ADMA_CTL_GO, mmio + NV_ADMA_CTL);

	pp->flags |= NV_ADMA_PORT_REGISTER_MODE;
}

static void nv_adma_mode(struct ata_port *ap)
{
	void __iomem *mmio = nv_adma_ctl_block(ap);
	struct nv_adma_port_priv *pp = ap->private_data;
	u16 tmp;

	if (!(pp->flags & NV_ADMA_PORT_REGISTER_MODE))
		return;

	tmp = readw(mmio + NV_ADMA_CTL);
	writew(tmp | NV_ADMA_CTL_GO, mmio + NV_ADMA_CTL);

	pp->flags &= ~NV_ADMA_PORT_REGISTER_MODE;
}

static int nv_adma_port_start(struct ata_port *ap)
{
	struct device *dev = ap->host->dev;
	struct nv_adma_port_priv *pp;
	int rc;
	void *mem;
	dma_addr_t mem_dma;
	void __iomem *mmio = nv_adma_ctl_block(ap);
	u16 tmp;

	VPRINTK("ENTER\n");

	rc = ata_port_start(ap);
	if (rc)
		return rc;

	pp = kzalloc(sizeof(*pp), GFP_KERNEL);
	if (!pp) {
		rc = -ENOMEM;
		goto err_out;
	}

	mem = dma_alloc_coherent(dev, NV_ADMA_PORT_PRIV_DMA_SZ,
				 &mem_dma, GFP_KERNEL);

	if (!mem) {
		rc = -ENOMEM;
		goto err_out_kfree;
	}
	memset(mem, 0, NV_ADMA_PORT_PRIV_DMA_SZ);

	/*
	 * First item in chunk of DMA memory:
	 * 128-byte command parameter block (CPB)
	 * one for each command tag
	 */
	pp->cpb     = mem;
	pp->cpb_dma = mem_dma;

	writel(mem_dma & 0xFFFFFFFF, 	mmio + NV_ADMA_CPB_BASE_LOW);
	writel((mem_dma >> 16 ) >> 16,	mmio + NV_ADMA_CPB_BASE_HIGH);

	mem     += NV_ADMA_MAX_CPBS * NV_ADMA_CPB_SZ;
	mem_dma += NV_ADMA_MAX_CPBS * NV_ADMA_CPB_SZ;

	/*
	 * Second item: block of ADMA_SGTBL_LEN s/g entries
	 */
	pp->aprd = mem;
	pp->aprd_dma = mem_dma;

	ap->private_data = pp;

	/* clear any outstanding interrupt conditions */
	writew(0xffff, mmio + NV_ADMA_STAT);

	/* initialize port variables */
	pp->flags = NV_ADMA_PORT_REGISTER_MODE;

	/* clear CPB fetch count */
	writew(0, mmio + NV_ADMA_CPB_COUNT);

	/* clear GO for register mode */
	tmp = readw(mmio + NV_ADMA_CTL);
	writew(tmp & ~NV_ADMA_CTL_GO, mmio + NV_ADMA_CTL);

	tmp = readw(mmio + NV_ADMA_CTL);
	writew(tmp | NV_ADMA_CTL_CHANNEL_RESET, mmio + NV_ADMA_CTL);
	readl( mmio + NV_ADMA_CTL );	/* flush posted write */
	udelay(1);
	writew(tmp & ~NV_ADMA_CTL_CHANNEL_RESET, mmio + NV_ADMA_CTL);
	readl( mmio + NV_ADMA_CTL );	/* flush posted write */

	return 0;

err_out_kfree:
	kfree(pp);
err_out:
	ata_port_stop(ap);
	return rc;
}

static void nv_adma_port_stop(struct ata_port *ap)
{
	struct device *dev = ap->host->dev;
	struct nv_adma_port_priv *pp = ap->private_data;
	void __iomem *mmio = nv_adma_ctl_block(ap);

	VPRINTK("ENTER\n");

	writew(0, mmio + NV_ADMA_CTL);

	ap->private_data = NULL;
	dma_free_coherent(dev, NV_ADMA_PORT_PRIV_DMA_SZ, pp->cpb, pp->cpb_dma);
	kfree(pp);
	ata_port_stop(ap);
}


static void nv_adma_setup_port(struct ata_probe_ent *probe_ent, unsigned int port)
{
	void __iomem *mmio = probe_ent->mmio_base;
	struct ata_ioports *ioport = &probe_ent->port[port];

	VPRINTK("ENTER\n");

	mmio += NV_ADMA_PORT + port * NV_ADMA_PORT_SIZE;

	ioport->cmd_addr	= (unsigned long) mmio;
	ioport->data_addr	= (unsigned long) mmio + (ATA_REG_DATA * 4);
	ioport->error_addr	=
	ioport->feature_addr	= (unsigned long) mmio + (ATA_REG_ERR * 4);
	ioport->nsect_addr	= (unsigned long) mmio + (ATA_REG_NSECT * 4);
	ioport->lbal_addr	= (unsigned long) mmio + (ATA_REG_LBAL * 4);
	ioport->lbam_addr	= (unsigned long) mmio + (ATA_REG_LBAM * 4);
	ioport->lbah_addr	= (unsigned long) mmio + (ATA_REG_LBAH * 4);
	ioport->device_addr	= (unsigned long) mmio + (ATA_REG_DEVICE * 4);
	ioport->status_addr	=
	ioport->command_addr	= (unsigned long) mmio + (ATA_REG_STATUS * 4);
	ioport->altstatus_addr	=
	ioport->ctl_addr	= (unsigned long) mmio + 0x20;
}

static int nv_adma_host_init(struct ata_probe_ent *probe_ent)
{
	struct pci_dev *pdev = to_pci_dev(probe_ent->dev);
	unsigned int i;
	u32 tmp32;

	VPRINTK("ENTER\n");

	/* enable ADMA on the ports */
	pci_read_config_dword(pdev, NV_MCP_SATA_CFG_20, &tmp32);
	tmp32 |= NV_MCP_SATA_CFG_20_PORT0_EN |
		 NV_MCP_SATA_CFG_20_PORT0_PWB_EN |
		 NV_MCP_SATA_CFG_20_PORT1_EN |
		 NV_MCP_SATA_CFG_20_PORT1_PWB_EN;

	pci_write_config_dword(pdev, NV_MCP_SATA_CFG_20, tmp32);

	for (i = 0; i < probe_ent->n_ports; i++)
		nv_adma_setup_port(probe_ent, i);

	for (i = 0; i < probe_ent->n_ports; i++) {
		void __iomem *mmio = __nv_adma_ctl_block(probe_ent->mmio_base, i);
		u16 tmp;

		/* enable interrupt, clear reset if not already clear */
		tmp = readw(mmio + NV_ADMA_CTL);
		writew(tmp | NV_ADMA_CTL_AIEN, mmio + NV_ADMA_CTL);
	}

	return 0;
}

static void nv_adma_fill_aprd(struct ata_queued_cmd *qc,
			      struct scatterlist *sg,
			      int idx,
			      struct nv_adma_prd *aprd)
{
	u32 flags;

	memset(aprd, 0, sizeof(struct nv_adma_prd));

	flags = 0;
	if (qc->tf.flags & ATA_TFLAG_WRITE)
		flags |= NV_APRD_WRITE;
	if (idx == qc->n_elem - 1)
		flags |= NV_APRD_END;
	else if (idx != 4)
		flags |= NV_APRD_CONT;

	aprd->addr  = cpu_to_le64(((u64)sg_dma_address(sg)));
	aprd->len   = cpu_to_le32(((u32)sg_dma_len(sg))); /* len in bytes */
	aprd->flags = cpu_to_le32(flags);
}

static void nv_adma_fill_sg(struct ata_queued_cmd *qc, struct nv_adma_cpb *cpb)
{
	struct nv_adma_port_priv *pp = qc->ap->private_data;
	unsigned int idx;
	struct nv_adma_prd *aprd;
	struct scatterlist *sg;

	VPRINTK("ENTER\n");

	idx = 0;

	ata_for_each_sg(sg, qc) {
		aprd = (idx < 5) ? &cpb->aprd[idx] : &pp->aprd[NV_ADMA_SGTBL_LEN * qc->tag + (idx-5)];
		nv_adma_fill_aprd(qc, sg, idx, aprd);
		idx++;
	}
	if (idx > 5)
		cpb->next_aprd = cpu_to_le64(((u64)(pp->aprd_dma + NV_ADMA_SGTBL_SZ * qc->tag)));
}

static void nv_adma_qc_prep(struct ata_queued_cmd *qc)
{
	struct nv_adma_port_priv *pp = qc->ap->private_data;
	struct nv_adma_cpb *cpb = &pp->cpb[qc->tag];
	u8 ctl_flags = NV_CPB_CTL_CPB_VALID |
		       NV_CPB_CTL_APRD_VALID |
		       NV_CPB_CTL_IEN;

	VPRINTK("qc->flags = 0x%lx\n", qc->flags);

	if (!(qc->flags & ATA_QCFLAG_DMAMAP) ||
	     qc->tf.protocol == ATA_PROT_ATAPI_DMA) {
		ata_qc_prep(qc);
		return;
	}

	memset(cpb, 0, sizeof(struct nv_adma_cpb));

	cpb->len		= 3;
	cpb->tag		= qc->tag;
	cpb->next_cpb_idx	= 0;

	/* turn on NCQ flags for NCQ commands */
	if (qc->tf.protocol == ATA_PROT_NCQ)
		ctl_flags |= NV_CPB_CTL_QUEUE | NV_CPB_CTL_FPDMA;

	nv_adma_tf_to_cpb(&qc->tf, cpb->tf);

	nv_adma_fill_sg(qc, cpb);

	/* Be paranoid and don't let the device see NV_CPB_CTL_CPB_VALID until we are
	   finished filling in all of the contents */
	wmb();
	cpb->ctl_flags = ctl_flags;
}

static unsigned int nv_adma_qc_issue(struct ata_queued_cmd *qc)
{
	void __iomem *mmio = nv_adma_ctl_block(qc->ap);

	VPRINTK("ENTER\n");

	if (!(qc->flags & ATA_QCFLAG_DMAMAP) ||
	     qc->tf.protocol == ATA_PROT_ATAPI_DMA) {
		/* use ATA register mode */
		VPRINTK("no dmamap or ATAPI, using ATA register mode: 0x%lx\n", qc->flags);
		nv_adma_register_mode(qc->ap);
		return ata_qc_issue_prot(qc);
	} else
		nv_adma_mode(qc->ap);

	/* write append register, command tag in lower 8 bits
	   and (number of cpbs to append -1) in top 8 bits */
	wmb();
	writew(qc->tag, mmio + NV_ADMA_APPEND);

	DPRINTK("Issued tag %u\n",qc->tag);

	return 0;
}

static irqreturn_t nv_generic_interrupt(int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	unsigned int i;
	unsigned int handled = 0;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap;

		ap = host->ports[i];
		if (ap &&
		    !(ap->flags & ATA_FLAG_DISABLED)) {
			struct ata_queued_cmd *qc;

			qc = ata_qc_from_tag(ap, ap->active_tag);
			if (qc && (!(qc->tf.flags & ATA_TFLAG_POLLING)))
				handled += ata_host_intr(ap, qc);
			else
				// No request pending?  Clear interrupt status
				// anyway, in case there's one pending.
				ap->ops->check_status(ap);
		}

	}

	spin_unlock_irqrestore(&host->lock, flags);

	return IRQ_RETVAL(handled);
}

static int nv_host_intr(struct ata_port *ap, u8 irq_stat)
{
	struct ata_queued_cmd *qc = ata_qc_from_tag(ap, ap->active_tag);
	int handled;

	/* freeze if hotplugged */
	if (unlikely(irq_stat & (NV_INT_ADDED | NV_INT_REMOVED))) {
		ata_port_freeze(ap);
		return 1;
	}

	/* bail out if not our interrupt */
	if (!(irq_stat & NV_INT_DEV))
		return 0;

	/* DEV interrupt w/ no active qc? */
	if (unlikely(!qc || (qc->tf.flags & ATA_TFLAG_POLLING))) {
		ata_check_status(ap);
		return 1;
	}

	/* handle interrupt */
	handled = ata_host_intr(ap, qc);
	if (unlikely(!handled)) {
		/* spurious, clear it */
		ata_check_status(ap);
	}

	return 1;
}

static irqreturn_t nv_do_interrupt(struct ata_host *host, u8 irq_stat)
{
	int i, handled = 0;

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		if (ap && !(ap->flags & ATA_FLAG_DISABLED))
			handled += nv_host_intr(ap, irq_stat);

		irq_stat >>= NV_INT_PORT_SHIFT;
	}

	return IRQ_RETVAL(handled);
}

static irqreturn_t nv_nf2_interrupt(int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	u8 irq_stat;
	irqreturn_t ret;

	spin_lock(&host->lock);
	irq_stat = inb(host->ports[0]->ioaddr.scr_addr + NV_INT_STATUS);
	ret = nv_do_interrupt(host, irq_stat);
	spin_unlock(&host->lock);

	return ret;
}

static irqreturn_t nv_ck804_interrupt(int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	u8 irq_stat;
	irqreturn_t ret;

	spin_lock(&host->lock);
	irq_stat = readb(host->mmio_base + NV_INT_STATUS_CK804);
	ret = nv_do_interrupt(host, irq_stat);
	spin_unlock(&host->lock);

	return ret;
}

static u32 nv_scr_read (struct ata_port *ap, unsigned int sc_reg)
{
	if (sc_reg > SCR_CONTROL)
		return 0xffffffffU;

	return ioread32((void __iomem *)ap->ioaddr.scr_addr + (sc_reg * 4));
}

static void nv_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val)
{
	if (sc_reg > SCR_CONTROL)
		return;

	iowrite32(val, (void __iomem *)ap->ioaddr.scr_addr + (sc_reg * 4));
}

static void nv_nf2_freeze(struct ata_port *ap)
{
	unsigned long scr_addr = ap->host->ports[0]->ioaddr.scr_addr;
	int shift = ap->port_no * NV_INT_PORT_SHIFT;
	u8 mask;

	mask = inb(scr_addr + NV_INT_ENABLE);
	mask &= ~(NV_INT_ALL << shift);
	outb(mask, scr_addr + NV_INT_ENABLE);
}

static void nv_nf2_thaw(struct ata_port *ap)
{
	unsigned long scr_addr = ap->host->ports[0]->ioaddr.scr_addr;
	int shift = ap->port_no * NV_INT_PORT_SHIFT;
	u8 mask;

	outb(NV_INT_ALL << shift, scr_addr + NV_INT_STATUS);

	mask = inb(scr_addr + NV_INT_ENABLE);
	mask |= (NV_INT_MASK << shift);
	outb(mask, scr_addr + NV_INT_ENABLE);
}

static void nv_ck804_freeze(struct ata_port *ap)
{
	void __iomem *mmio_base = ap->host->mmio_base;
	int shift = ap->port_no * NV_INT_PORT_SHIFT;
	u8 mask;

	mask = readb(mmio_base + NV_INT_ENABLE_CK804);
	mask &= ~(NV_INT_ALL << shift);
	writeb(mask, mmio_base + NV_INT_ENABLE_CK804);
}

static void nv_ck804_thaw(struct ata_port *ap)
{
	void __iomem *mmio_base = ap->host->mmio_base;
	int shift = ap->port_no * NV_INT_PORT_SHIFT;
	u8 mask;

	writeb(NV_INT_ALL << shift, mmio_base + NV_INT_STATUS_CK804);

	mask = readb(mmio_base + NV_INT_ENABLE_CK804);
	mask |= (NV_INT_MASK << shift);
	writeb(mask, mmio_base + NV_INT_ENABLE_CK804);
}

static int nv_hardreset(struct ata_port *ap, unsigned int *class)
{
	unsigned int dummy;

	/* SATA hardreset fails to retrieve proper device signature on
	 * some controllers.  Don't classify on hardreset.  For more
	 * info, see http://bugme.osdl.org/show_bug.cgi?id=3352
	 */
	return sata_std_hardreset(ap, &dummy);
}

static void nv_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, ata_std_prereset, ata_std_softreset,
			   nv_hardreset, ata_std_postreset);
}

static void nv_adma_error_handler(struct ata_port *ap)
{
	struct nv_adma_port_priv *pp = ap->private_data;
	if(!(pp->flags & NV_ADMA_PORT_REGISTER_MODE)) {
		void __iomem *mmio = nv_adma_ctl_block(ap);
		int i;
		u16 tmp;

		u32 notifier = readl(mmio + NV_ADMA_NOTIFIER);
		u32 notifier_error = readl(mmio + NV_ADMA_NOTIFIER_ERROR);
		u32 gen_ctl = readl(nv_adma_gen_block(ap) + NV_ADMA_GEN_CTL);
		u32 status = readw(mmio + NV_ADMA_STAT);

		ata_port_printk(ap, KERN_ERR, "EH in ADMA mode, notifier 0x%X "
			"notifier_error 0x%X gen_ctl 0x%X status 0x%X\n",
			notifier, notifier_error, gen_ctl, status);

		for( i=0;i<NV_ADMA_MAX_CPBS;i++) {
			struct nv_adma_cpb *cpb = &pp->cpb[i];
			if( cpb->ctl_flags || cpb->resp_flags )
				ata_port_printk(ap, KERN_ERR,
					"CPB %d: ctl_flags 0x%x, resp_flags 0x%x\n",
					i, cpb->ctl_flags, cpb->resp_flags);
		}

		/* Push us back into port register mode for error handling. */
		nv_adma_register_mode(ap);

		ata_port_printk(ap, KERN_ERR, "Resetting port\n");

		/* Mark all of the CPBs as invalid to prevent them from being executed */
		for( i=0;i<NV_ADMA_MAX_CPBS;i++)
			pp->cpb[i].ctl_flags &= ~NV_CPB_CTL_CPB_VALID;

		/* clear CPB fetch count */
		writew(0, mmio + NV_ADMA_CPB_COUNT);

		/* Reset channel */
		tmp = readw(mmio + NV_ADMA_CTL);
		writew(tmp | NV_ADMA_CTL_CHANNEL_RESET, mmio + NV_ADMA_CTL);
		readl( mmio + NV_ADMA_CTL );	/* flush posted write */
		udelay(1);
		writew(tmp & ~NV_ADMA_CTL_CHANNEL_RESET, mmio + NV_ADMA_CTL);
		readl( mmio + NV_ADMA_CTL );	/* flush posted write */
	}

	ata_bmdma_drive_eh(ap, ata_std_prereset, ata_std_softreset,
			   nv_hardreset, ata_std_postreset);
}

static int nv_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version = 0;
	struct ata_port_info *ppi[2];
	struct ata_probe_ent *probe_ent;
	int pci_dev_busy = 0;
	int rc;
	u32 bar;
	unsigned long base;
	unsigned long type = ent->driver_data;
	int mask_set = 0;

        // Make sure this is a SATA controller by counting the number of bars
        // (NVIDIA SATA controllers will always have six bars).  Otherwise,
        // it's an IDE controller and we ignore it.
	for (bar=0; bar<6; bar++)
		if (pci_resource_start(pdev, bar) == 0)
			return -ENODEV;

	if (	!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	rc = pci_enable_device(pdev);
	if (rc)
		goto err_out;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc) {
		pci_dev_busy = 1;
		goto err_out_disable;
	}

	if(type >= CK804 && adma_enabled) {
		dev_printk(KERN_NOTICE, &pdev->dev, "Using ADMA mode\n");
		type = ADMA;
		if(!pci_set_dma_mask(pdev, DMA_64BIT_MASK) &&
		   !pci_set_consistent_dma_mask(pdev, DMA_64BIT_MASK))
			mask_set = 1;
	}

	if(!mask_set) {
		rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
		if (rc)
			goto err_out_regions;
		rc = pci_set_consistent_dma_mask(pdev, ATA_DMA_MASK);
		if (rc)
			goto err_out_regions;
	}

	rc = -ENOMEM;

	ppi[0] = ppi[1] = &nv_port_info[type];
	probe_ent = ata_pci_init_native_mode(pdev, ppi, ATA_PORT_PRIMARY | ATA_PORT_SECONDARY);
	if (!probe_ent)
		goto err_out_regions;

	probe_ent->mmio_base = pci_iomap(pdev, 5, 0);
	if (!probe_ent->mmio_base) {
		rc = -EIO;
		goto err_out_free_ent;
	}

	base = (unsigned long)probe_ent->mmio_base;

	probe_ent->port[0].scr_addr = base + NV_PORT0_SCR_REG_OFFSET;
	probe_ent->port[1].scr_addr = base + NV_PORT1_SCR_REG_OFFSET;

	/* enable SATA space for CK804 */
	if (type >= CK804) {
		u8 regval;

		pci_read_config_byte(pdev, NV_MCP_SATA_CFG_20, &regval);
		regval |= NV_MCP_SATA_CFG_20_SATA_SPACE_EN;
		pci_write_config_byte(pdev, NV_MCP_SATA_CFG_20, regval);
	}

	pci_set_master(pdev);

	if (type == ADMA) {
		rc = nv_adma_host_init(probe_ent);
		if (rc)
			goto err_out_iounmap;
	}

	rc = ata_device_add(probe_ent);
	if (rc != NV_PORTS)
		goto err_out_iounmap;

	kfree(probe_ent);

	return 0;

err_out_iounmap:
	pci_iounmap(pdev, probe_ent->mmio_base);
err_out_free_ent:
	kfree(probe_ent);
err_out_regions:
	pci_release_regions(pdev);
err_out_disable:
	if (!pci_dev_busy)
		pci_disable_device(pdev);
err_out:
	return rc;
}

static void nv_ck804_host_stop(struct ata_host *host)
{
	struct pci_dev *pdev = to_pci_dev(host->dev);
	u8 regval;

	/* disable SATA space for CK804 */
	pci_read_config_byte(pdev, NV_MCP_SATA_CFG_20, &regval);
	regval &= ~NV_MCP_SATA_CFG_20_SATA_SPACE_EN;
	pci_write_config_byte(pdev, NV_MCP_SATA_CFG_20, regval);

	ata_pci_host_stop(host);
}

static void nv_adma_host_stop(struct ata_host *host)
{
	struct pci_dev *pdev = to_pci_dev(host->dev);
	int i;
	u32 tmp32;

	for (i = 0; i < host->n_ports; i++) {
		void __iomem *mmio = __nv_adma_ctl_block(host->mmio_base, i);
		u16 tmp;

		/* disable interrupt */
		tmp = readw(mmio + NV_ADMA_CTL);
		writew(tmp & ~NV_ADMA_CTL_AIEN, mmio + NV_ADMA_CTL);
	}

	/* disable ADMA on the ports */
	pci_read_config_dword(pdev, NV_MCP_SATA_CFG_20, &tmp32);
	tmp32 &= ~(NV_MCP_SATA_CFG_20_PORT0_EN |
		   NV_MCP_SATA_CFG_20_PORT0_PWB_EN |
		   NV_MCP_SATA_CFG_20_PORT1_EN |
		   NV_MCP_SATA_CFG_20_PORT1_PWB_EN);

	pci_write_config_dword(pdev, NV_MCP_SATA_CFG_20, tmp32);

	nv_ck804_host_stop(host);
}

static int __init nv_init(void)
{
	return pci_register_driver(&nv_pci_driver);
}

static void __exit nv_exit(void)
{
	pci_unregister_driver(&nv_pci_driver);
}

module_init(nv_init);
module_exit(nv_exit);
module_param_named(adma, adma_enabled, bool, 0444);
MODULE_PARM_DESC(adma, "Enable use of ADMA (Default: true)");
