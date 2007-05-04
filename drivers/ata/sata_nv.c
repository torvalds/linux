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
#define DRV_VERSION			"3.3"

#define NV_ADMA_DMA_BOUNDARY		0xffffffffUL

enum {
	NV_MMIO_BAR			= 5,

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
	NV_ADMA_ATAPI_SETUP_COMPLETE	= (1 << 1),

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
	void __iomem *		ctl_block;
	void __iomem *		gen_block;
	void __iomem *		notifier_clear_block;
	u8			flags;
	int			last_issue_ncq;
};

struct nv_host_priv {
	unsigned long		type;
};

#define NV_ADMA_CHECK_INTR(GCTL, PORT) ((GCTL) & ( 1 << (19 + (12 * (PORT)))))

static int nv_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static void nv_remove_one (struct pci_dev *pdev);
#ifdef CONFIG_PM
static int nv_pci_device_resume(struct pci_dev *pdev);
#endif
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
static int nv_adma_check_atapi_dma(struct ata_queued_cmd *qc);
static void nv_adma_qc_prep(struct ata_queued_cmd *qc);
static unsigned int nv_adma_qc_issue(struct ata_queued_cmd *qc);
static irqreturn_t nv_adma_interrupt(int irq, void *dev_instance);
static void nv_adma_irq_clear(struct ata_port *ap);
static int nv_adma_port_start(struct ata_port *ap);
static void nv_adma_port_stop(struct ata_port *ap);
#ifdef CONFIG_PM
static int nv_adma_port_suspend(struct ata_port *ap, pm_message_t mesg);
static int nv_adma_port_resume(struct ata_port *ap);
#endif
static void nv_adma_freeze(struct ata_port *ap);
static void nv_adma_thaw(struct ata_port *ap);
static void nv_adma_error_handler(struct ata_port *ap);
static void nv_adma_host_stop(struct ata_host *host);
static void nv_adma_post_internal_cmd(struct ata_queued_cmd *qc);
static void nv_adma_tf_read(struct ata_port *ap, struct ata_taskfile *tf);

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
#ifdef CONFIG_PM
	.suspend		= ata_pci_device_suspend,
	.resume			= nv_pci_device_resume,
#endif
	.remove			= nv_remove_one,
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
	.data_xfer		= ata_data_xfer,
	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,
	.irq_ack		= ata_irq_ack,
	.scr_read		= nv_scr_read,
	.scr_write		= nv_scr_write,
	.port_start		= ata_port_start,
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
	.data_xfer		= ata_data_xfer,
	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,
	.irq_ack		= ata_irq_ack,
	.scr_read		= nv_scr_read,
	.scr_write		= nv_scr_write,
	.port_start		= ata_port_start,
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
	.data_xfer		= ata_data_xfer,
	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,
	.irq_ack		= ata_irq_ack,
	.scr_read		= nv_scr_read,
	.scr_write		= nv_scr_write,
	.port_start		= ata_port_start,
	.host_stop		= nv_ck804_host_stop,
};

static const struct ata_port_operations nv_adma_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= ata_tf_load,
	.tf_read		= nv_adma_tf_read,
	.check_atapi_dma	= nv_adma_check_atapi_dma,
	.exec_command		= ata_exec_command,
	.check_status		= ata_check_status,
	.dev_select		= ata_std_dev_select,
	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= nv_adma_qc_prep,
	.qc_issue		= nv_adma_qc_issue,
	.freeze			= nv_adma_freeze,
	.thaw			= nv_adma_thaw,
	.error_handler		= nv_adma_error_handler,
	.post_internal_cmd	= nv_adma_post_internal_cmd,
	.data_xfer		= ata_data_xfer,
	.irq_clear		= nv_adma_irq_clear,
	.irq_on			= ata_irq_on,
	.irq_ack		= ata_irq_ack,
	.scr_read		= nv_scr_read,
	.scr_write		= nv_scr_write,
	.port_start		= nv_adma_port_start,
	.port_stop		= nv_adma_port_stop,
#ifdef CONFIG_PM
	.port_suspend		= nv_adma_port_suspend,
	.port_resume		= nv_adma_port_resume,
#endif
	.host_stop		= nv_adma_host_stop,
};

static const struct ata_port_info nv_port_info[] = {
	/* generic */
	{
		.sht		= &nv_sht,
		.flags		= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_HRST_TO_RESUME,
		.pio_mask	= NV_PIO_MASK,
		.mwdma_mask	= NV_MWDMA_MASK,
		.udma_mask	= NV_UDMA_MASK,
		.port_ops	= &nv_generic_ops,
		.irq_handler	= nv_generic_interrupt,
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
		.irq_handler	= nv_nf2_interrupt,
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
		.irq_handler	= nv_ck804_interrupt,
	},
	/* ADMA */
	{
		.sht		= &nv_adma_sht,
		.flags		= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_HRST_TO_RESUME |
				  ATA_FLAG_MMIO | ATA_FLAG_NCQ,
		.pio_mask	= NV_PIO_MASK,
		.mwdma_mask	= NV_MWDMA_MASK,
		.udma_mask	= NV_UDMA_MASK,
		.port_ops	= &nv_adma_ops,
		.irq_handler	= nv_adma_interrupt,
	},
};

MODULE_AUTHOR("NVIDIA");
MODULE_DESCRIPTION("low-level driver for NVIDIA nForce SATA controller");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, nv_pci_tbl);
MODULE_VERSION(DRV_VERSION);

static int adma_enabled = 1;

static void nv_adma_register_mode(struct ata_port *ap)
{
	struct nv_adma_port_priv *pp = ap->private_data;
	void __iomem *mmio = pp->ctl_block;
	u16 tmp, status;
	int count = 0;

	if (pp->flags & NV_ADMA_PORT_REGISTER_MODE)
		return;

	status = readw(mmio + NV_ADMA_STAT);
	while(!(status & NV_ADMA_STAT_IDLE) && count < 20) {
		ndelay(50);
		status = readw(mmio + NV_ADMA_STAT);
		count++;
	}
	if(count == 20)
		ata_port_printk(ap, KERN_WARNING,
			"timeout waiting for ADMA IDLE, stat=0x%hx\n",
			status);

	tmp = readw(mmio + NV_ADMA_CTL);
	writew(tmp & ~NV_ADMA_CTL_GO, mmio + NV_ADMA_CTL);

	count = 0;
	status = readw(mmio + NV_ADMA_STAT);
	while(!(status & NV_ADMA_STAT_LEGACY) && count < 20) {
		ndelay(50);
		status = readw(mmio + NV_ADMA_STAT);
		count++;
	}
	if(count == 20)
		ata_port_printk(ap, KERN_WARNING,
			 "timeout waiting for ADMA LEGACY, stat=0x%hx\n",
			 status);

	pp->flags |= NV_ADMA_PORT_REGISTER_MODE;
}

static void nv_adma_mode(struct ata_port *ap)
{
	struct nv_adma_port_priv *pp = ap->private_data;
	void __iomem *mmio = pp->ctl_block;
	u16 tmp, status;
	int count = 0;

	if (!(pp->flags & NV_ADMA_PORT_REGISTER_MODE))
		return;

	WARN_ON(pp->flags & NV_ADMA_ATAPI_SETUP_COMPLETE);

	tmp = readw(mmio + NV_ADMA_CTL);
	writew(tmp | NV_ADMA_CTL_GO, mmio + NV_ADMA_CTL);

	status = readw(mmio + NV_ADMA_STAT);
	while(((status & NV_ADMA_STAT_LEGACY) ||
	      !(status & NV_ADMA_STAT_IDLE)) && count < 20) {
		ndelay(50);
		status = readw(mmio + NV_ADMA_STAT);
		count++;
	}
	if(count == 20)
		ata_port_printk(ap, KERN_WARNING,
			"timeout waiting for ADMA LEGACY clear and IDLE, stat=0x%hx\n",
			status);

	pp->flags &= ~NV_ADMA_PORT_REGISTER_MODE;
}

static int nv_adma_slave_config(struct scsi_device *sdev)
{
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct nv_adma_port_priv *pp = ap->private_data;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u64 bounce_limit;
	unsigned long segment_boundary;
	unsigned short sg_tablesize;
	int rc;
	int adma_enable;
	u32 current_reg, new_reg, config_mask;

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

		/* Since the legacy DMA engine is in use, we need to disable ADMA
		   on the port. */
		adma_enable = 0;
		nv_adma_register_mode(ap);
	}
	else {
		bounce_limit = *ap->dev->dma_mask;
		segment_boundary = NV_ADMA_DMA_BOUNDARY;
		sg_tablesize = NV_ADMA_SGTBL_TOTAL_LEN;
		adma_enable = 1;
	}

	pci_read_config_dword(pdev, NV_MCP_SATA_CFG_20, &current_reg);

	if(ap->port_no == 1)
		config_mask = NV_MCP_SATA_CFG_20_PORT1_EN |
			      NV_MCP_SATA_CFG_20_PORT1_PWB_EN;
	else
		config_mask = NV_MCP_SATA_CFG_20_PORT0_EN |
			      NV_MCP_SATA_CFG_20_PORT0_PWB_EN;

	if(adma_enable) {
		new_reg = current_reg | config_mask;
		pp->flags &= ~NV_ADMA_ATAPI_SETUP_COMPLETE;
	}
	else {
		new_reg = current_reg & ~config_mask;
		pp->flags |= NV_ADMA_ATAPI_SETUP_COMPLETE;
	}

	if(current_reg != new_reg)
		pci_write_config_dword(pdev, NV_MCP_SATA_CFG_20, new_reg);

	blk_queue_bounce_limit(sdev->request_queue, bounce_limit);
	blk_queue_segment_boundary(sdev->request_queue, segment_boundary);
	blk_queue_max_hw_segments(sdev->request_queue, sg_tablesize);
	ata_port_printk(ap, KERN_INFO,
		"bounce limit 0x%llX, segment boundary 0x%lX, hw segs %hu\n",
		(unsigned long long)bounce_limit, segment_boundary, sg_tablesize);
	return rc;
}

static int nv_adma_check_atapi_dma(struct ata_queued_cmd *qc)
{
	struct nv_adma_port_priv *pp = qc->ap->private_data;
	return !(pp->flags & NV_ADMA_ATAPI_SETUP_COMPLETE);
}

static void nv_adma_tf_read(struct ata_port *ap, struct ata_taskfile *tf)
{
	/* Since commands where a result TF is requested are not
	   executed in ADMA mode, the only time this function will be called
	   in ADMA mode will be if a command fails. In this case we
	   don't care about going into register mode with ADMA commands
	   pending, as the commands will all shortly be aborted anyway. */
	nv_adma_register_mode(ap);

	ata_tf_read(ap, tf);
}

static unsigned int nv_adma_tf_to_cpb(struct ata_taskfile *tf, __le16 *cpb)
{
	unsigned int idx = 0;

	if(tf->flags & ATA_TFLAG_ISADDR) {
		if (tf->flags & ATA_TFLAG_LBA48) {
			cpb[idx++] = cpu_to_le16((ATA_REG_ERR   << 8) | tf->hob_feature | WNB);
			cpb[idx++] = cpu_to_le16((ATA_REG_NSECT << 8) | tf->hob_nsect);
			cpb[idx++] = cpu_to_le16((ATA_REG_LBAL  << 8) | tf->hob_lbal);
			cpb[idx++] = cpu_to_le16((ATA_REG_LBAM  << 8) | tf->hob_lbam);
			cpb[idx++] = cpu_to_le16((ATA_REG_LBAH  << 8) | tf->hob_lbah);
			cpb[idx++] = cpu_to_le16((ATA_REG_ERR    << 8) | tf->feature);
		} else
			cpb[idx++] = cpu_to_le16((ATA_REG_ERR    << 8) | tf->feature | WNB);

		cpb[idx++] = cpu_to_le16((ATA_REG_NSECT  << 8) | tf->nsect);
		cpb[idx++] = cpu_to_le16((ATA_REG_LBAL   << 8) | tf->lbal);
		cpb[idx++] = cpu_to_le16((ATA_REG_LBAM   << 8) | tf->lbam);
		cpb[idx++] = cpu_to_le16((ATA_REG_LBAH   << 8) | tf->lbah);
	}

	if(tf->flags & ATA_TFLAG_DEVICE)
		cpb[idx++] = cpu_to_le16((ATA_REG_DEVICE << 8) | tf->device);

	cpb[idx++] = cpu_to_le16((ATA_REG_CMD    << 8) | tf->command | CMDEND);

	while(idx < 12)
		cpb[idx++] = cpu_to_le16(IGN);

	return idx;
}

static int nv_adma_check_cpb(struct ata_port *ap, int cpb_num, int force_err)
{
	struct nv_adma_port_priv *pp = ap->private_data;
	u8 flags = pp->cpb[cpb_num].resp_flags;

	VPRINTK("CPB %d, flags=0x%x\n", cpb_num, flags);

	if (unlikely((force_err ||
		     flags & (NV_CPB_RESP_ATA_ERR |
			      NV_CPB_RESP_CMD_ERR |
			      NV_CPB_RESP_CPB_ERR)))) {
		struct ata_eh_info *ehi = &ap->eh_info;
		int freeze = 0;

		ata_ehi_clear_desc(ehi);
		ata_ehi_push_desc(ehi, "CPB resp_flags 0x%x", flags );
		if (flags & NV_CPB_RESP_ATA_ERR) {
			ata_ehi_push_desc(ehi, ": ATA error");
			ehi->err_mask |= AC_ERR_DEV;
		} else if (flags & NV_CPB_RESP_CMD_ERR) {
			ata_ehi_push_desc(ehi, ": CMD error");
			ehi->err_mask |= AC_ERR_DEV;
		} else if (flags & NV_CPB_RESP_CPB_ERR) {
			ata_ehi_push_desc(ehi, ": CPB error");
			ehi->err_mask |= AC_ERR_SYSTEM;
			freeze = 1;
		} else {
			/* notifier error, but no error in CPB flags? */
			ehi->err_mask |= AC_ERR_OTHER;
			freeze = 1;
		}
		/* Kill all commands. EH will determine what actually failed. */
		if (freeze)
			ata_port_freeze(ap);
		else
			ata_port_abort(ap);
		return 1;
	}

	if (likely(flags & NV_CPB_RESP_DONE)) {
		struct ata_queued_cmd *qc = ata_qc_from_tag(ap, cpb_num);
		VPRINTK("CPB flags done, flags=0x%x\n", flags);
		if (likely(qc)) {
			DPRINTK("Completing qc from tag %d\n",cpb_num);
			ata_qc_complete(qc);
		} else {
			struct ata_eh_info *ehi = &ap->eh_info;
			/* Notifier bits set without a command may indicate the drive
			   is misbehaving. Raise host state machine violation on this
			   condition. */
			ata_port_printk(ap, KERN_ERR, "notifier for tag %d with no command?\n",
				cpb_num);
			ehi->err_mask |= AC_ERR_HSM;
			ehi->action |= ATA_EH_SOFTRESET;
			ata_port_freeze(ap);
			return 1;
		}
	}
	return 0;
}

static int nv_host_intr(struct ata_port *ap, u8 irq_stat)
{
	struct ata_queued_cmd *qc = ata_qc_from_tag(ap, ap->active_tag);

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
	return ata_host_intr(ap, qc);
}

static irqreturn_t nv_adma_interrupt(int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	int i, handled = 0;
	u32 notifier_clears[2];

	spin_lock(&host->lock);

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];
		notifier_clears[i] = 0;

		if (ap && !(ap->flags & ATA_FLAG_DISABLED)) {
			struct nv_adma_port_priv *pp = ap->private_data;
			void __iomem *mmio = pp->ctl_block;
			u16 status;
			u32 gen_ctl;
			u32 notifier, notifier_error;
			
			/* if ADMA is disabled, use standard ata interrupt handler */
			if (pp->flags & NV_ADMA_ATAPI_SETUP_COMPLETE) {
				u8 irq_stat = readb(host->iomap[NV_MMIO_BAR] + NV_INT_STATUS_CK804)
					>> (NV_INT_PORT_SHIFT * i);
				handled += nv_host_intr(ap, irq_stat);
				continue;
			}

			/* if in ATA register mode, check for standard interrupts */
			if (pp->flags & NV_ADMA_PORT_REGISTER_MODE) {
				u8 irq_stat = readb(host->iomap[NV_MMIO_BAR] + NV_INT_STATUS_CK804)
					>> (NV_INT_PORT_SHIFT * i);
				if(ata_tag_valid(ap->active_tag))
					/** NV_INT_DEV indication seems unreliable at times
					    at least in ADMA mode. Force it on always when a
					    command is active, to prevent losing interrupts. */
					irq_stat |= NV_INT_DEV;
				handled += nv_host_intr(ap, irq_stat);
			}

			notifier = readl(mmio + NV_ADMA_NOTIFIER);
			notifier_error = readl(mmio + NV_ADMA_NOTIFIER_ERROR);
			notifier_clears[i] = notifier | notifier_error;

			gen_ctl = readl(pp->gen_block + NV_ADMA_GEN_CTL);

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

			handled++; /* irq handled if we got here */

			/* freeze if hotplugged or controller error */
			if (unlikely(status & (NV_ADMA_STAT_HOTPLUG |
					       NV_ADMA_STAT_HOTUNPLUG |
					       NV_ADMA_STAT_TIMEOUT |
					       NV_ADMA_STAT_SERROR))) {
				struct ata_eh_info *ehi = &ap->eh_info;

				ata_ehi_clear_desc(ehi);
				ata_ehi_push_desc(ehi, "ADMA status 0x%08x", status );
				if (status & NV_ADMA_STAT_TIMEOUT) {
					ehi->err_mask |= AC_ERR_SYSTEM;
					ata_ehi_push_desc(ehi, ": timeout");
				} else if (status & NV_ADMA_STAT_HOTPLUG) {
					ata_ehi_hotplugged(ehi);
					ata_ehi_push_desc(ehi, ": hotplug");
				} else if (status & NV_ADMA_STAT_HOTUNPLUG) {
					ata_ehi_hotplugged(ehi);
					ata_ehi_push_desc(ehi, ": hot unplug");
				} else if (status & NV_ADMA_STAT_SERROR) {
					/* let libata analyze SError and figure out the cause */
					ata_ehi_push_desc(ehi, ": SError");
				}
				ata_port_freeze(ap);
				continue;
			}

			if (status & (NV_ADMA_STAT_DONE |
				      NV_ADMA_STAT_CPBERR)) {
				u32 check_commands;
				int pos, error = 0;

				if(ata_tag_valid(ap->active_tag))
					check_commands = 1 << ap->active_tag;
				else
					check_commands = ap->sactive;

				/** Check CPBs for completed commands */
				while ((pos = ffs(check_commands)) && !error) {
					pos--;
					error = nv_adma_check_cpb(ap, pos,
						notifier_error & (1 << pos) );
					check_commands &= ~(1 << pos );
				}
			}
		}
	}

	if(notifier_clears[0] || notifier_clears[1]) {
		/* Note: Both notifier clear registers must be written
		   if either is set, even if one is zero, according to NVIDIA. */
		struct nv_adma_port_priv *pp = host->ports[0]->private_data;
		writel(notifier_clears[0], pp->notifier_clear_block);
		pp = host->ports[1]->private_data;
		writel(notifier_clears[1], pp->notifier_clear_block);
	}

	spin_unlock(&host->lock);

	return IRQ_RETVAL(handled);
}

static void nv_adma_freeze(struct ata_port *ap)
{
	struct nv_adma_port_priv *pp = ap->private_data;
	void __iomem *mmio = pp->ctl_block;
	u16 tmp;

	nv_ck804_freeze(ap);

	if (pp->flags & NV_ADMA_ATAPI_SETUP_COMPLETE)
		return;

	/* clear any outstanding CK804 notifications */
	writeb( NV_INT_ALL << (ap->port_no * NV_INT_PORT_SHIFT),
		ap->host->iomap[NV_MMIO_BAR] + NV_INT_STATUS_CK804);

	/* Disable interrupt */
	tmp = readw(mmio + NV_ADMA_CTL);
	writew( tmp & ~(NV_ADMA_CTL_AIEN | NV_ADMA_CTL_HOTPLUG_IEN),
		mmio + NV_ADMA_CTL);
	readw( mmio + NV_ADMA_CTL );	/* flush posted write */
}

static void nv_adma_thaw(struct ata_port *ap)
{
	struct nv_adma_port_priv *pp = ap->private_data;
	void __iomem *mmio = pp->ctl_block;
	u16 tmp;

	nv_ck804_thaw(ap);

	if (pp->flags & NV_ADMA_ATAPI_SETUP_COMPLETE)
		return;

	/* Enable interrupt */
	tmp = readw(mmio + NV_ADMA_CTL);
	writew( tmp | (NV_ADMA_CTL_AIEN | NV_ADMA_CTL_HOTPLUG_IEN),
		mmio + NV_ADMA_CTL);
	readw( mmio + NV_ADMA_CTL );	/* flush posted write */
}

static void nv_adma_irq_clear(struct ata_port *ap)
{
	struct nv_adma_port_priv *pp = ap->private_data;
	void __iomem *mmio = pp->ctl_block;
	u32 notifier_clears[2];

	if (pp->flags & NV_ADMA_ATAPI_SETUP_COMPLETE) {
		ata_bmdma_irq_clear(ap);
		return;
	}

	/* clear any outstanding CK804 notifications */
	writeb( NV_INT_ALL << (ap->port_no * NV_INT_PORT_SHIFT),
		ap->host->iomap[NV_MMIO_BAR] + NV_INT_STATUS_CK804);

	/* clear ADMA status */
	writew(0xffff, mmio + NV_ADMA_STAT);
	
	/* clear notifiers - note both ports need to be written with
	   something even though we are only clearing on one */
	if (ap->port_no == 0) {
		notifier_clears[0] = 0xFFFFFFFF;
		notifier_clears[1] = 0;
	} else {
		notifier_clears[0] = 0;
		notifier_clears[1] = 0xFFFFFFFF;
	}
	pp = ap->host->ports[0]->private_data;
	writel(notifier_clears[0], pp->notifier_clear_block);
	pp = ap->host->ports[1]->private_data;
	writel(notifier_clears[1], pp->notifier_clear_block);
}

static void nv_adma_post_internal_cmd(struct ata_queued_cmd *qc)
{
	struct nv_adma_port_priv *pp = qc->ap->private_data;

	if(pp->flags & NV_ADMA_PORT_REGISTER_MODE)
		ata_bmdma_post_internal_cmd(qc);
}

static int nv_adma_port_start(struct ata_port *ap)
{
	struct device *dev = ap->host->dev;
	struct nv_adma_port_priv *pp;
	int rc;
	void *mem;
	dma_addr_t mem_dma;
	void __iomem *mmio;
	u16 tmp;

	VPRINTK("ENTER\n");

	rc = ata_port_start(ap);
	if (rc)
		return rc;

	pp = devm_kzalloc(dev, sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;

	mmio = ap->host->iomap[NV_MMIO_BAR] + NV_ADMA_PORT +
	       ap->port_no * NV_ADMA_PORT_SIZE;
	pp->ctl_block = mmio;
	pp->gen_block = ap->host->iomap[NV_MMIO_BAR] + NV_ADMA_GEN;
	pp->notifier_clear_block = pp->gen_block +
	       NV_ADMA_NOTIFIER_CLEAR + (4 * ap->port_no);

	mem = dmam_alloc_coherent(dev, NV_ADMA_PORT_PRIV_DMA_SZ,
				  &mem_dma, GFP_KERNEL);
	if (!mem)
		return -ENOMEM;
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

	/* clear GO for register mode, enable interrupt */
	tmp = readw(mmio + NV_ADMA_CTL);
	writew( (tmp & ~NV_ADMA_CTL_GO) | NV_ADMA_CTL_AIEN |
		 NV_ADMA_CTL_HOTPLUG_IEN, mmio + NV_ADMA_CTL);

	tmp = readw(mmio + NV_ADMA_CTL);
	writew(tmp | NV_ADMA_CTL_CHANNEL_RESET, mmio + NV_ADMA_CTL);
	readw( mmio + NV_ADMA_CTL );	/* flush posted write */
	udelay(1);
	writew(tmp & ~NV_ADMA_CTL_CHANNEL_RESET, mmio + NV_ADMA_CTL);
	readw( mmio + NV_ADMA_CTL );	/* flush posted write */

	return 0;
}

static void nv_adma_port_stop(struct ata_port *ap)
{
	struct nv_adma_port_priv *pp = ap->private_data;
	void __iomem *mmio = pp->ctl_block;

	VPRINTK("ENTER\n");
	writew(0, mmio + NV_ADMA_CTL);
}

#ifdef CONFIG_PM
static int nv_adma_port_suspend(struct ata_port *ap, pm_message_t mesg)
{
	struct nv_adma_port_priv *pp = ap->private_data;
	void __iomem *mmio = pp->ctl_block;

	/* Go to register mode - clears GO */
	nv_adma_register_mode(ap);

	/* clear CPB fetch count */
	writew(0, mmio + NV_ADMA_CPB_COUNT);

	/* disable interrupt, shut down port */
	writew(0, mmio + NV_ADMA_CTL);

	return 0;
}

static int nv_adma_port_resume(struct ata_port *ap)
{
	struct nv_adma_port_priv *pp = ap->private_data;
	void __iomem *mmio = pp->ctl_block;
	u16 tmp;

	/* set CPB block location */
	writel(pp->cpb_dma & 0xFFFFFFFF, 	mmio + NV_ADMA_CPB_BASE_LOW);
	writel((pp->cpb_dma >> 16 ) >> 16,	mmio + NV_ADMA_CPB_BASE_HIGH);

	/* clear any outstanding interrupt conditions */
	writew(0xffff, mmio + NV_ADMA_STAT);

	/* initialize port variables */
	pp->flags |= NV_ADMA_PORT_REGISTER_MODE;

	/* clear CPB fetch count */
	writew(0, mmio + NV_ADMA_CPB_COUNT);

	/* clear GO for register mode, enable interrupt */
	tmp = readw(mmio + NV_ADMA_CTL);
	writew( (tmp & ~NV_ADMA_CTL_GO) | NV_ADMA_CTL_AIEN |
		 NV_ADMA_CTL_HOTPLUG_IEN, mmio + NV_ADMA_CTL);

	tmp = readw(mmio + NV_ADMA_CTL);
	writew(tmp | NV_ADMA_CTL_CHANNEL_RESET, mmio + NV_ADMA_CTL);
	readw( mmio + NV_ADMA_CTL );	/* flush posted write */
	udelay(1);
	writew(tmp & ~NV_ADMA_CTL_CHANNEL_RESET, mmio + NV_ADMA_CTL);
	readw( mmio + NV_ADMA_CTL );	/* flush posted write */

	return 0;
}
#endif

static void nv_adma_setup_port(struct ata_port *ap)
{
	void __iomem *mmio = ap->host->iomap[NV_MMIO_BAR];
	struct ata_ioports *ioport = &ap->ioaddr;

	VPRINTK("ENTER\n");

	mmio += NV_ADMA_PORT + ap->port_no * NV_ADMA_PORT_SIZE;

	ioport->cmd_addr	= mmio;
	ioport->data_addr	= mmio + (ATA_REG_DATA * 4);
	ioport->error_addr	=
	ioport->feature_addr	= mmio + (ATA_REG_ERR * 4);
	ioport->nsect_addr	= mmio + (ATA_REG_NSECT * 4);
	ioport->lbal_addr	= mmio + (ATA_REG_LBAL * 4);
	ioport->lbam_addr	= mmio + (ATA_REG_LBAM * 4);
	ioport->lbah_addr	= mmio + (ATA_REG_LBAH * 4);
	ioport->device_addr	= mmio + (ATA_REG_DEVICE * 4);
	ioport->status_addr	=
	ioport->command_addr	= mmio + (ATA_REG_STATUS * 4);
	ioport->altstatus_addr	=
	ioport->ctl_addr	= mmio + 0x20;
}

static int nv_adma_host_init(struct ata_host *host)
{
	struct pci_dev *pdev = to_pci_dev(host->dev);
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

	for (i = 0; i < host->n_ports; i++)
		nv_adma_setup_port(host->ports[i]);

	return 0;
}

static void nv_adma_fill_aprd(struct ata_queued_cmd *qc,
			      struct scatterlist *sg,
			      int idx,
			      struct nv_adma_prd *aprd)
{
	u8 flags = 0;
	if (qc->tf.flags & ATA_TFLAG_WRITE)
		flags |= NV_APRD_WRITE;
	if (idx == qc->n_elem - 1)
		flags |= NV_APRD_END;
	else if (idx != 4)
		flags |= NV_APRD_CONT;

	aprd->addr  = cpu_to_le64(((u64)sg_dma_address(sg)));
	aprd->len   = cpu_to_le32(((u32)sg_dma_len(sg))); /* len in bytes */
	aprd->flags = flags;
	aprd->packet_len = 0;
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
	else
		cpb->next_aprd = cpu_to_le64(0);
}

static int nv_adma_use_reg_mode(struct ata_queued_cmd *qc)
{
	struct nv_adma_port_priv *pp = qc->ap->private_data;

	/* ADMA engine can only be used for non-ATAPI DMA commands,
	   or interrupt-driven no-data commands, where a result taskfile
	   is not required. */
	if((pp->flags & NV_ADMA_ATAPI_SETUP_COMPLETE) ||
	   (qc->tf.flags & ATA_TFLAG_POLLING) ||
	   (qc->flags & ATA_QCFLAG_RESULT_TF))
		return 1;

	if((qc->flags & ATA_QCFLAG_DMAMAP) ||
	   (qc->tf.protocol == ATA_PROT_NODATA))
		return 0;

	return 1;
}

static void nv_adma_qc_prep(struct ata_queued_cmd *qc)
{
	struct nv_adma_port_priv *pp = qc->ap->private_data;
	struct nv_adma_cpb *cpb = &pp->cpb[qc->tag];
	u8 ctl_flags = NV_CPB_CTL_CPB_VALID |
		       NV_CPB_CTL_IEN;

	if (nv_adma_use_reg_mode(qc)) {
		nv_adma_register_mode(qc->ap);
		ata_qc_prep(qc);
		return;
	}

	cpb->resp_flags = NV_CPB_RESP_DONE;
	wmb();
	cpb->ctl_flags = 0;
	wmb();

	cpb->len		= 3;
	cpb->tag		= qc->tag;
	cpb->next_cpb_idx	= 0;

	/* turn on NCQ flags for NCQ commands */
	if (qc->tf.protocol == ATA_PROT_NCQ)
		ctl_flags |= NV_CPB_CTL_QUEUE | NV_CPB_CTL_FPDMA;

	VPRINTK("qc->flags = 0x%lx\n", qc->flags);

	nv_adma_tf_to_cpb(&qc->tf, cpb->tf);

	if(qc->flags & ATA_QCFLAG_DMAMAP) {
		nv_adma_fill_sg(qc, cpb);
		ctl_flags |= NV_CPB_CTL_APRD_VALID;
	} else
		memset(&cpb->aprd[0], 0, sizeof(struct nv_adma_prd) * 5);

	/* Be paranoid and don't let the device see NV_CPB_CTL_CPB_VALID until we are
	   finished filling in all of the contents */
	wmb();
	cpb->ctl_flags = ctl_flags;
	wmb();
	cpb->resp_flags = 0;
}

static unsigned int nv_adma_qc_issue(struct ata_queued_cmd *qc)
{
	struct nv_adma_port_priv *pp = qc->ap->private_data;
	void __iomem *mmio = pp->ctl_block;
	int curr_ncq = (qc->tf.protocol == ATA_PROT_NCQ);

	VPRINTK("ENTER\n");

	if (nv_adma_use_reg_mode(qc)) {
		/* use ATA register mode */
		VPRINTK("using ATA register mode: 0x%lx\n", qc->flags);
		nv_adma_register_mode(qc->ap);
		return ata_qc_issue_prot(qc);
	} else
		nv_adma_mode(qc->ap);

	/* write append register, command tag in lower 8 bits
	   and (number of cpbs to append -1) in top 8 bits */
	wmb();

	if(curr_ncq != pp->last_issue_ncq) {
	   	/* Seems to need some delay before switching between NCQ and non-NCQ
		   commands, else we get command timeouts and such. */
		udelay(20);
		pp->last_issue_ncq = curr_ncq;
	}

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
	irq_stat = ioread8(host->ports[0]->ioaddr.scr_addr + NV_INT_STATUS);
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
	irq_stat = readb(host->iomap[NV_MMIO_BAR] + NV_INT_STATUS_CK804);
	ret = nv_do_interrupt(host, irq_stat);
	spin_unlock(&host->lock);

	return ret;
}

static u32 nv_scr_read (struct ata_port *ap, unsigned int sc_reg)
{
	if (sc_reg > SCR_CONTROL)
		return 0xffffffffU;

	return ioread32(ap->ioaddr.scr_addr + (sc_reg * 4));
}

static void nv_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val)
{
	if (sc_reg > SCR_CONTROL)
		return;

	iowrite32(val, ap->ioaddr.scr_addr + (sc_reg * 4));
}

static void nv_nf2_freeze(struct ata_port *ap)
{
	void __iomem *scr_addr = ap->host->ports[0]->ioaddr.scr_addr;
	int shift = ap->port_no * NV_INT_PORT_SHIFT;
	u8 mask;

	mask = ioread8(scr_addr + NV_INT_ENABLE);
	mask &= ~(NV_INT_ALL << shift);
	iowrite8(mask, scr_addr + NV_INT_ENABLE);
}

static void nv_nf2_thaw(struct ata_port *ap)
{
	void __iomem *scr_addr = ap->host->ports[0]->ioaddr.scr_addr;
	int shift = ap->port_no * NV_INT_PORT_SHIFT;
	u8 mask;

	iowrite8(NV_INT_ALL << shift, scr_addr + NV_INT_STATUS);

	mask = ioread8(scr_addr + NV_INT_ENABLE);
	mask |= (NV_INT_MASK << shift);
	iowrite8(mask, scr_addr + NV_INT_ENABLE);
}

static void nv_ck804_freeze(struct ata_port *ap)
{
	void __iomem *mmio_base = ap->host->iomap[NV_MMIO_BAR];
	int shift = ap->port_no * NV_INT_PORT_SHIFT;
	u8 mask;

	mask = readb(mmio_base + NV_INT_ENABLE_CK804);
	mask &= ~(NV_INT_ALL << shift);
	writeb(mask, mmio_base + NV_INT_ENABLE_CK804);
}

static void nv_ck804_thaw(struct ata_port *ap)
{
	void __iomem *mmio_base = ap->host->iomap[NV_MMIO_BAR];
	int shift = ap->port_no * NV_INT_PORT_SHIFT;
	u8 mask;

	writeb(NV_INT_ALL << shift, mmio_base + NV_INT_STATUS_CK804);

	mask = readb(mmio_base + NV_INT_ENABLE_CK804);
	mask |= (NV_INT_MASK << shift);
	writeb(mask, mmio_base + NV_INT_ENABLE_CK804);
}

static int nv_hardreset(struct ata_port *ap, unsigned int *class,
			unsigned long deadline)
{
	unsigned int dummy;

	/* SATA hardreset fails to retrieve proper device signature on
	 * some controllers.  Don't classify on hardreset.  For more
	 * info, see http://bugme.osdl.org/show_bug.cgi?id=3352
	 */
	return sata_std_hardreset(ap, &dummy, deadline);
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
		void __iomem *mmio = pp->ctl_block;
		int i;
		u16 tmp;

		if(ata_tag_valid(ap->active_tag) || ap->sactive) {
			u32 notifier = readl(mmio + NV_ADMA_NOTIFIER);
			u32 notifier_error = readl(mmio + NV_ADMA_NOTIFIER_ERROR);
			u32 gen_ctl = readl(pp->gen_block + NV_ADMA_GEN_CTL);
			u32 status = readw(mmio + NV_ADMA_STAT);
			u8 cpb_count = readb(mmio + NV_ADMA_CPB_COUNT);
			u8 next_cpb_idx = readb(mmio + NV_ADMA_NEXT_CPB_IDX);

			ata_port_printk(ap, KERN_ERR, "EH in ADMA mode, notifier 0x%X "
				"notifier_error 0x%X gen_ctl 0x%X status 0x%X "
				"next cpb count 0x%X next cpb idx 0x%x\n",
				notifier, notifier_error, gen_ctl, status,
				cpb_count, next_cpb_idx);

			for( i=0;i<NV_ADMA_MAX_CPBS;i++) {
				struct nv_adma_cpb *cpb = &pp->cpb[i];
				if( (ata_tag_valid(ap->active_tag) && i == ap->active_tag) ||
				    ap->sactive & (1 << i) )
					ata_port_printk(ap, KERN_ERR,
						"CPB %d: ctl_flags 0x%x, resp_flags 0x%x\n",
						i, cpb->ctl_flags, cpb->resp_flags);
			}
		}

		/* Push us back into port register mode for error handling. */
		nv_adma_register_mode(ap);

		/* Mark all of the CPBs as invalid to prevent them from being executed */
		for( i=0;i<NV_ADMA_MAX_CPBS;i++)
			pp->cpb[i].ctl_flags &= ~NV_CPB_CTL_CPB_VALID;

		/* clear CPB fetch count */
		writew(0, mmio + NV_ADMA_CPB_COUNT);

		/* Reset channel */
		tmp = readw(mmio + NV_ADMA_CTL);
		writew(tmp | NV_ADMA_CTL_CHANNEL_RESET, mmio + NV_ADMA_CTL);
		readw( mmio + NV_ADMA_CTL );	/* flush posted write */
		udelay(1);
		writew(tmp & ~NV_ADMA_CTL_CHANNEL_RESET, mmio + NV_ADMA_CTL);
		readw( mmio + NV_ADMA_CTL );	/* flush posted write */
	}

	ata_bmdma_drive_eh(ap, ata_std_prereset, ata_std_softreset,
			   nv_hardreset, ata_std_postreset);
}

static int nv_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version = 0;
	const struct ata_port_info *ppi[] = { NULL, NULL };
	struct ata_host *host;
	struct nv_host_priv *hpriv;
	int rc;
	u32 bar;
	void __iomem *base;
	unsigned long type = ent->driver_data;

        // Make sure this is a SATA controller by counting the number of bars
        // (NVIDIA SATA controllers will always have six bars).  Otherwise,
        // it's an IDE controller and we ignore it.
	for (bar=0; bar<6; bar++)
		if (pci_resource_start(pdev, bar) == 0)
			return -ENODEV;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	/* determine type and allocate host */
	if (type >= CK804 && adma_enabled) {
		dev_printk(KERN_NOTICE, &pdev->dev, "Using ADMA mode\n");
		type = ADMA;
	}

	ppi[0] = &nv_port_info[type];
	rc = ata_pci_prepare_native_host(pdev, ppi, &host);
	if (rc)
		return rc;

	hpriv = devm_kzalloc(&pdev->dev, sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv)
		return -ENOMEM;
	hpriv->type = type;
	host->private_data = hpriv;

	/* set 64bit dma masks, may fail */
	if (type == ADMA) {
		if (pci_set_dma_mask(pdev, DMA_64BIT_MASK) == 0)
			pci_set_consistent_dma_mask(pdev, DMA_64BIT_MASK);
	}

	/* request and iomap NV_MMIO_BAR */
	rc = pcim_iomap_regions(pdev, 1 << NV_MMIO_BAR, DRV_NAME);
	if (rc)
		return rc;

	/* configure SCR access */
	base = host->iomap[NV_MMIO_BAR];
	host->ports[0]->ioaddr.scr_addr = base + NV_PORT0_SCR_REG_OFFSET;
	host->ports[1]->ioaddr.scr_addr = base + NV_PORT1_SCR_REG_OFFSET;

	/* enable SATA space for CK804 */
	if (type >= CK804) {
		u8 regval;

		pci_read_config_byte(pdev, NV_MCP_SATA_CFG_20, &regval);
		regval |= NV_MCP_SATA_CFG_20_SATA_SPACE_EN;
		pci_write_config_byte(pdev, NV_MCP_SATA_CFG_20, regval);
	}

	/* init ADMA */
	if (type == ADMA) {
		rc = nv_adma_host_init(host);
		if (rc)
			return rc;
	}

	pci_set_master(pdev);
	return ata_host_activate(host, pdev->irq, ppi[0]->irq_handler,
				 IRQF_SHARED, ppi[0]->sht);
}

static void nv_remove_one (struct pci_dev *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	struct nv_host_priv *hpriv = host->private_data;

	ata_pci_remove_one(pdev);
	kfree(hpriv);
}

#ifdef CONFIG_PM
static int nv_pci_device_resume(struct pci_dev *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	struct nv_host_priv *hpriv = host->private_data;
	int rc;

	rc = ata_pci_device_do_resume(pdev);
	if(rc)
		return rc;

	if (pdev->dev.power.power_state.event == PM_EVENT_SUSPEND) {
		if(hpriv->type >= CK804) {
			u8 regval;

			pci_read_config_byte(pdev, NV_MCP_SATA_CFG_20, &regval);
			regval |= NV_MCP_SATA_CFG_20_SATA_SPACE_EN;
			pci_write_config_byte(pdev, NV_MCP_SATA_CFG_20, regval);
		}
		if(hpriv->type == ADMA) {
			u32 tmp32;
			struct nv_adma_port_priv *pp;
			/* enable/disable ADMA on the ports appropriately */
			pci_read_config_dword(pdev, NV_MCP_SATA_CFG_20, &tmp32);

			pp = host->ports[0]->private_data;
			if(pp->flags & NV_ADMA_ATAPI_SETUP_COMPLETE)
				tmp32 &= ~(NV_MCP_SATA_CFG_20_PORT0_EN |
				 	   NV_MCP_SATA_CFG_20_PORT0_PWB_EN);
			else
				tmp32 |=  (NV_MCP_SATA_CFG_20_PORT0_EN |
				 	   NV_MCP_SATA_CFG_20_PORT0_PWB_EN);
			pp = host->ports[1]->private_data;
			if(pp->flags & NV_ADMA_ATAPI_SETUP_COMPLETE)
				tmp32 &= ~(NV_MCP_SATA_CFG_20_PORT1_EN |
				 	   NV_MCP_SATA_CFG_20_PORT1_PWB_EN);
			else
				tmp32 |=  (NV_MCP_SATA_CFG_20_PORT1_EN |
				 	   NV_MCP_SATA_CFG_20_PORT1_PWB_EN);

			pci_write_config_dword(pdev, NV_MCP_SATA_CFG_20, tmp32);
		}
	}

	ata_host_resume(host);

	return 0;
}
#endif

static void nv_ck804_host_stop(struct ata_host *host)
{
	struct pci_dev *pdev = to_pci_dev(host->dev);
	u8 regval;

	/* disable SATA space for CK804 */
	pci_read_config_byte(pdev, NV_MCP_SATA_CFG_20, &regval);
	regval &= ~NV_MCP_SATA_CFG_20_SATA_SPACE_EN;
	pci_write_config_byte(pdev, NV_MCP_SATA_CFG_20, regval);
}

static void nv_adma_host_stop(struct ata_host *host)
{
	struct pci_dev *pdev = to_pci_dev(host->dev);
	u32 tmp32;

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
