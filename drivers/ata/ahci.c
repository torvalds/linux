/*
 *  ahci.c - AHCI SATA support
 *
 *  Maintained by:  Jeff Garzik <jgarzik@pobox.com>
 *    		    Please ALWAYS copy linux-ide@vger.kernel.org
 *		    on emails.
 *
 *  Copyright 2004-2005 Red Hat, Inc.
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
 * libata documentation is available via 'make {ps|pdf}docs',
 * as Documentation/DocBook/libata.*
 *
 * AHCI hardware documentation:
 * http://www.intel.com/technology/serialata/pdf/rev1_0.pdf
 * http://www.intel.com/technology/serialata/pdf/rev1_1.pdf
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <linux/libata.h>

#define DRV_NAME	"ahci"
#define DRV_VERSION	"3.0"

static int ahci_skip_host_reset;
module_param_named(skip_host_reset, ahci_skip_host_reset, int, 0444);
MODULE_PARM_DESC(skip_host_reset, "skip global host reset (0=don't skip, 1=skip)");

static int ahci_enable_alpm(struct ata_port *ap,
		enum link_pm policy);
static void ahci_disable_alpm(struct ata_port *ap);

enum {
	AHCI_PCI_BAR		= 5,
	AHCI_MAX_PORTS		= 32,
	AHCI_MAX_SG		= 168, /* hardware max is 64K */
	AHCI_DMA_BOUNDARY	= 0xffffffff,
	AHCI_USE_CLUSTERING	= 1,
	AHCI_MAX_CMDS		= 32,
	AHCI_CMD_SZ		= 32,
	AHCI_CMD_SLOT_SZ	= AHCI_MAX_CMDS * AHCI_CMD_SZ,
	AHCI_RX_FIS_SZ		= 256,
	AHCI_CMD_TBL_CDB	= 0x40,
	AHCI_CMD_TBL_HDR_SZ	= 0x80,
	AHCI_CMD_TBL_SZ		= AHCI_CMD_TBL_HDR_SZ + (AHCI_MAX_SG * 16),
	AHCI_CMD_TBL_AR_SZ	= AHCI_CMD_TBL_SZ * AHCI_MAX_CMDS,
	AHCI_PORT_PRIV_DMA_SZ	= AHCI_CMD_SLOT_SZ + AHCI_CMD_TBL_AR_SZ +
				  AHCI_RX_FIS_SZ,
	AHCI_IRQ_ON_SG		= (1 << 31),
	AHCI_CMD_ATAPI		= (1 << 5),
	AHCI_CMD_WRITE		= (1 << 6),
	AHCI_CMD_PREFETCH	= (1 << 7),
	AHCI_CMD_RESET		= (1 << 8),
	AHCI_CMD_CLR_BUSY	= (1 << 10),

	RX_FIS_D2H_REG		= 0x40,	/* offset of D2H Register FIS data */
	RX_FIS_SDB		= 0x58, /* offset of SDB FIS data */
	RX_FIS_UNK		= 0x60, /* offset of Unknown FIS data */

	board_ahci		= 0,
	board_ahci_vt8251	= 1,
	board_ahci_ign_iferr	= 2,
	board_ahci_sb600	= 3,
	board_ahci_mv		= 4,
	board_ahci_sb700	= 5,

	/* global controller registers */
	HOST_CAP		= 0x00, /* host capabilities */
	HOST_CTL		= 0x04, /* global host control */
	HOST_IRQ_STAT		= 0x08, /* interrupt status */
	HOST_PORTS_IMPL		= 0x0c, /* bitmap of implemented ports */
	HOST_VERSION		= 0x10, /* AHCI spec. version compliancy */

	/* HOST_CTL bits */
	HOST_RESET		= (1 << 0),  /* reset controller; self-clear */
	HOST_IRQ_EN		= (1 << 1),  /* global IRQ enable */
	HOST_AHCI_EN		= (1 << 31), /* AHCI enabled */

	/* HOST_CAP bits */
	HOST_CAP_SSC		= (1 << 14), /* Slumber capable */
	HOST_CAP_PMP		= (1 << 17), /* Port Multiplier support */
	HOST_CAP_CLO		= (1 << 24), /* Command List Override support */
	HOST_CAP_ALPM		= (1 << 26), /* Aggressive Link PM support */
	HOST_CAP_SSS		= (1 << 27), /* Staggered Spin-up */
	HOST_CAP_SNTF		= (1 << 29), /* SNotification register */
	HOST_CAP_NCQ		= (1 << 30), /* Native Command Queueing */
	HOST_CAP_64		= (1 << 31), /* PCI DAC (64-bit DMA) support */

	/* registers for each SATA port */
	PORT_LST_ADDR		= 0x00, /* command list DMA addr */
	PORT_LST_ADDR_HI	= 0x04, /* command list DMA addr hi */
	PORT_FIS_ADDR		= 0x08, /* FIS rx buf addr */
	PORT_FIS_ADDR_HI	= 0x0c, /* FIS rx buf addr hi */
	PORT_IRQ_STAT		= 0x10, /* interrupt status */
	PORT_IRQ_MASK		= 0x14, /* interrupt enable/disable mask */
	PORT_CMD		= 0x18, /* port command */
	PORT_TFDATA		= 0x20,	/* taskfile data */
	PORT_SIG		= 0x24,	/* device TF signature */
	PORT_CMD_ISSUE		= 0x38, /* command issue */
	PORT_SCR_STAT		= 0x28, /* SATA phy register: SStatus */
	PORT_SCR_CTL		= 0x2c, /* SATA phy register: SControl */
	PORT_SCR_ERR		= 0x30, /* SATA phy register: SError */
	PORT_SCR_ACT		= 0x34, /* SATA phy register: SActive */
	PORT_SCR_NTF		= 0x3c, /* SATA phy register: SNotification */

	/* PORT_IRQ_{STAT,MASK} bits */
	PORT_IRQ_COLD_PRES	= (1 << 31), /* cold presence detect */
	PORT_IRQ_TF_ERR		= (1 << 30), /* task file error */
	PORT_IRQ_HBUS_ERR	= (1 << 29), /* host bus fatal error */
	PORT_IRQ_HBUS_DATA_ERR	= (1 << 28), /* host bus data error */
	PORT_IRQ_IF_ERR		= (1 << 27), /* interface fatal error */
	PORT_IRQ_IF_NONFATAL	= (1 << 26), /* interface non-fatal error */
	PORT_IRQ_OVERFLOW	= (1 << 24), /* xfer exhausted available S/G */
	PORT_IRQ_BAD_PMP	= (1 << 23), /* incorrect port multiplier */

	PORT_IRQ_PHYRDY		= (1 << 22), /* PhyRdy changed */
	PORT_IRQ_DEV_ILCK	= (1 << 7), /* device interlock */
	PORT_IRQ_CONNECT	= (1 << 6), /* port connect change status */
	PORT_IRQ_SG_DONE	= (1 << 5), /* descriptor processed */
	PORT_IRQ_UNK_FIS	= (1 << 4), /* unknown FIS rx'd */
	PORT_IRQ_SDB_FIS	= (1 << 3), /* Set Device Bits FIS rx'd */
	PORT_IRQ_DMAS_FIS	= (1 << 2), /* DMA Setup FIS rx'd */
	PORT_IRQ_PIOS_FIS	= (1 << 1), /* PIO Setup FIS rx'd */
	PORT_IRQ_D2H_REG_FIS	= (1 << 0), /* D2H Register FIS rx'd */

	PORT_IRQ_FREEZE		= PORT_IRQ_HBUS_ERR |
				  PORT_IRQ_IF_ERR |
				  PORT_IRQ_CONNECT |
				  PORT_IRQ_PHYRDY |
				  PORT_IRQ_UNK_FIS |
				  PORT_IRQ_BAD_PMP,
	PORT_IRQ_ERROR		= PORT_IRQ_FREEZE |
				  PORT_IRQ_TF_ERR |
				  PORT_IRQ_HBUS_DATA_ERR,
	DEF_PORT_IRQ		= PORT_IRQ_ERROR | PORT_IRQ_SG_DONE |
				  PORT_IRQ_SDB_FIS | PORT_IRQ_DMAS_FIS |
				  PORT_IRQ_PIOS_FIS | PORT_IRQ_D2H_REG_FIS,

	/* PORT_CMD bits */
	PORT_CMD_ASP		= (1 << 27), /* Aggressive Slumber/Partial */
	PORT_CMD_ALPE		= (1 << 26), /* Aggressive Link PM enable */
	PORT_CMD_ATAPI		= (1 << 24), /* Device is ATAPI */
	PORT_CMD_PMP		= (1 << 17), /* PMP attached */
	PORT_CMD_LIST_ON	= (1 << 15), /* cmd list DMA engine running */
	PORT_CMD_FIS_ON		= (1 << 14), /* FIS DMA engine running */
	PORT_CMD_FIS_RX		= (1 << 4), /* Enable FIS receive DMA engine */
	PORT_CMD_CLO		= (1 << 3), /* Command list override */
	PORT_CMD_POWER_ON	= (1 << 2), /* Power up device */
	PORT_CMD_SPIN_UP	= (1 << 1), /* Spin up device */
	PORT_CMD_START		= (1 << 0), /* Enable port DMA engine */

	PORT_CMD_ICC_MASK	= (0xf << 28), /* i/f ICC state mask */
	PORT_CMD_ICC_ACTIVE	= (0x1 << 28), /* Put i/f in active state */
	PORT_CMD_ICC_PARTIAL	= (0x2 << 28), /* Put i/f in partial state */
	PORT_CMD_ICC_SLUMBER	= (0x6 << 28), /* Put i/f in slumber state */

	/* hpriv->flags bits */
	AHCI_HFLAG_NO_NCQ		= (1 << 0),
	AHCI_HFLAG_IGN_IRQ_IF_ERR	= (1 << 1), /* ignore IRQ_IF_ERR */
	AHCI_HFLAG_IGN_SERR_INTERNAL	= (1 << 2), /* ignore SERR_INTERNAL */
	AHCI_HFLAG_32BIT_ONLY		= (1 << 3), /* force 32bit */
	AHCI_HFLAG_MV_PATA		= (1 << 4), /* PATA port */
	AHCI_HFLAG_NO_MSI		= (1 << 5), /* no PCI MSI */
	AHCI_HFLAG_NO_PMP		= (1 << 6), /* no PMP */
	AHCI_HFLAG_NO_HOTPLUG		= (1 << 7), /* ignore PxSERR.DIAG.N */
	AHCI_HFLAG_SECT255		= (1 << 8), /* max 255 sectors */

	/* ap->flags bits */

	AHCI_FLAG_COMMON		= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
					  ATA_FLAG_MMIO | ATA_FLAG_PIO_DMA |
					  ATA_FLAG_ACPI_SATA | ATA_FLAG_AN |
					  ATA_FLAG_IPM,
	AHCI_LFLAG_COMMON		= ATA_LFLAG_SKIP_D2H_BSY,

	ICH_MAP				= 0x90, /* ICH MAP register */
};

struct ahci_cmd_hdr {
	__le32			opts;
	__le32			status;
	__le32			tbl_addr;
	__le32			tbl_addr_hi;
	__le32			reserved[4];
};

struct ahci_sg {
	__le32			addr;
	__le32			addr_hi;
	__le32			reserved;
	__le32			flags_size;
};

struct ahci_host_priv {
	unsigned int		flags;		/* AHCI_HFLAG_* */
	u32			cap;		/* cap to use */
	u32			port_map;	/* port map to use */
	u32			saved_cap;	/* saved initial cap */
	u32			saved_port_map;	/* saved initial port_map */
};

struct ahci_port_priv {
	struct ata_link		*active_link;
	struct ahci_cmd_hdr	*cmd_slot;
	dma_addr_t		cmd_slot_dma;
	void			*cmd_tbl;
	dma_addr_t		cmd_tbl_dma;
	void			*rx_fis;
	dma_addr_t		rx_fis_dma;
	/* for NCQ spurious interrupt analysis */
	unsigned int		ncq_saw_d2h:1;
	unsigned int		ncq_saw_dmas:1;
	unsigned int		ncq_saw_sdb:1;
	u32 			intr_mask;	/* interrupts to enable */
};

static int ahci_scr_read(struct ata_port *ap, unsigned int sc_reg, u32 *val);
static int ahci_scr_write(struct ata_port *ap, unsigned int sc_reg, u32 val);
static int ahci_init_one(struct pci_dev *pdev, const struct pci_device_id *ent);
static unsigned int ahci_qc_issue(struct ata_queued_cmd *qc);
static void ahci_irq_clear(struct ata_port *ap);
static int ahci_port_start(struct ata_port *ap);
static void ahci_port_stop(struct ata_port *ap);
static void ahci_tf_read(struct ata_port *ap, struct ata_taskfile *tf);
static void ahci_qc_prep(struct ata_queued_cmd *qc);
static u8 ahci_check_status(struct ata_port *ap);
static void ahci_freeze(struct ata_port *ap);
static void ahci_thaw(struct ata_port *ap);
static void ahci_pmp_attach(struct ata_port *ap);
static void ahci_pmp_detach(struct ata_port *ap);
static void ahci_error_handler(struct ata_port *ap);
static void ahci_vt8251_error_handler(struct ata_port *ap);
static void ahci_p5wdh_error_handler(struct ata_port *ap);
static void ahci_post_internal_cmd(struct ata_queued_cmd *qc);
static int ahci_port_resume(struct ata_port *ap);
static void ahci_dev_config(struct ata_device *dev);
static unsigned int ahci_fill_sg(struct ata_queued_cmd *qc, void *cmd_tbl);
static void ahci_fill_cmd_slot(struct ahci_port_priv *pp, unsigned int tag,
			       u32 opts);
#ifdef CONFIG_PM
static int ahci_port_suspend(struct ata_port *ap, pm_message_t mesg);
static int ahci_pci_device_suspend(struct pci_dev *pdev, pm_message_t mesg);
static int ahci_pci_device_resume(struct pci_dev *pdev);
#endif

static struct class_device_attribute *ahci_shost_attrs[] = {
	&class_device_attr_link_power_management_policy,
	NULL
};

static struct scsi_host_template ahci_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.change_queue_depth	= ata_scsi_change_queue_depth,
	.can_queue		= AHCI_MAX_CMDS - 1,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= AHCI_MAX_SG,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= AHCI_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= AHCI_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.slave_destroy		= ata_scsi_slave_destroy,
	.bios_param		= ata_std_bios_param,
	.shost_attrs		= ahci_shost_attrs,
};

static const struct ata_port_operations ahci_ops = {
	.check_status		= ahci_check_status,
	.check_altstatus	= ahci_check_status,
	.dev_select		= ata_noop_dev_select,

	.dev_config		= ahci_dev_config,

	.tf_read		= ahci_tf_read,

	.qc_defer		= sata_pmp_qc_defer_cmd_switch,
	.qc_prep		= ahci_qc_prep,
	.qc_issue		= ahci_qc_issue,

	.irq_clear		= ahci_irq_clear,

	.scr_read		= ahci_scr_read,
	.scr_write		= ahci_scr_write,

	.freeze			= ahci_freeze,
	.thaw			= ahci_thaw,

	.error_handler		= ahci_error_handler,
	.post_internal_cmd	= ahci_post_internal_cmd,

	.pmp_attach		= ahci_pmp_attach,
	.pmp_detach		= ahci_pmp_detach,

#ifdef CONFIG_PM
	.port_suspend		= ahci_port_suspend,
	.port_resume		= ahci_port_resume,
#endif
	.enable_pm		= ahci_enable_alpm,
	.disable_pm		= ahci_disable_alpm,

	.port_start		= ahci_port_start,
	.port_stop		= ahci_port_stop,
};

static const struct ata_port_operations ahci_vt8251_ops = {
	.check_status		= ahci_check_status,
	.check_altstatus	= ahci_check_status,
	.dev_select		= ata_noop_dev_select,

	.tf_read		= ahci_tf_read,

	.qc_defer		= sata_pmp_qc_defer_cmd_switch,
	.qc_prep		= ahci_qc_prep,
	.qc_issue		= ahci_qc_issue,

	.irq_clear		= ahci_irq_clear,

	.scr_read		= ahci_scr_read,
	.scr_write		= ahci_scr_write,

	.freeze			= ahci_freeze,
	.thaw			= ahci_thaw,

	.error_handler		= ahci_vt8251_error_handler,
	.post_internal_cmd	= ahci_post_internal_cmd,

	.pmp_attach		= ahci_pmp_attach,
	.pmp_detach		= ahci_pmp_detach,

#ifdef CONFIG_PM
	.port_suspend		= ahci_port_suspend,
	.port_resume		= ahci_port_resume,
#endif

	.port_start		= ahci_port_start,
	.port_stop		= ahci_port_stop,
};

static const struct ata_port_operations ahci_p5wdh_ops = {
	.check_status		= ahci_check_status,
	.check_altstatus	= ahci_check_status,
	.dev_select		= ata_noop_dev_select,

	.tf_read		= ahci_tf_read,

	.qc_defer		= sata_pmp_qc_defer_cmd_switch,
	.qc_prep		= ahci_qc_prep,
	.qc_issue		= ahci_qc_issue,

	.irq_clear		= ahci_irq_clear,

	.scr_read		= ahci_scr_read,
	.scr_write		= ahci_scr_write,

	.freeze			= ahci_freeze,
	.thaw			= ahci_thaw,

	.error_handler		= ahci_p5wdh_error_handler,
	.post_internal_cmd	= ahci_post_internal_cmd,

	.pmp_attach		= ahci_pmp_attach,
	.pmp_detach		= ahci_pmp_detach,

#ifdef CONFIG_PM
	.port_suspend		= ahci_port_suspend,
	.port_resume		= ahci_port_resume,
#endif

	.port_start		= ahci_port_start,
	.port_stop		= ahci_port_stop,
};

#define AHCI_HFLAGS(flags)	.private_data	= (void *)(flags)

static const struct ata_port_info ahci_port_info[] = {
	/* board_ahci */
	{
		.flags		= AHCI_FLAG_COMMON,
		.link_flags	= AHCI_LFLAG_COMMON,
		.pio_mask	= 0x1f, /* pio0-4 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &ahci_ops,
	},
	/* board_ahci_vt8251 */
	{
		AHCI_HFLAGS	(AHCI_HFLAG_NO_NCQ | AHCI_HFLAG_NO_PMP),
		.flags		= AHCI_FLAG_COMMON,
		.link_flags	= AHCI_LFLAG_COMMON | ATA_LFLAG_HRST_TO_RESUME,
		.pio_mask	= 0x1f, /* pio0-4 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &ahci_vt8251_ops,
	},
	/* board_ahci_ign_iferr */
	{
		AHCI_HFLAGS	(AHCI_HFLAG_IGN_IRQ_IF_ERR),
		.flags		= AHCI_FLAG_COMMON,
		.link_flags	= AHCI_LFLAG_COMMON,
		.pio_mask	= 0x1f, /* pio0-4 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &ahci_ops,
	},
	/* board_ahci_sb600 */
	{
		AHCI_HFLAGS	(AHCI_HFLAG_IGN_SERR_INTERNAL |
				 AHCI_HFLAG_32BIT_ONLY |
				 AHCI_HFLAG_SECT255 | AHCI_HFLAG_NO_PMP),
		.flags		= AHCI_FLAG_COMMON,
		.link_flags	= AHCI_LFLAG_COMMON,
		.pio_mask	= 0x1f, /* pio0-4 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &ahci_ops,
	},
	/* board_ahci_mv */
	{
		AHCI_HFLAGS	(AHCI_HFLAG_NO_NCQ | AHCI_HFLAG_NO_MSI |
				 AHCI_HFLAG_MV_PATA),
		.flags		= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_MMIO | ATA_FLAG_PIO_DMA,
		.link_flags	= AHCI_LFLAG_COMMON,
		.pio_mask	= 0x1f, /* pio0-4 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &ahci_ops,
	},
	/* board_ahci_sb700 */
	{
		AHCI_HFLAGS	(AHCI_HFLAG_IGN_SERR_INTERNAL |
				 AHCI_HFLAG_NO_PMP),
		.flags		= AHCI_FLAG_COMMON,
		.link_flags	= AHCI_LFLAG_COMMON,
		.pio_mask	= 0x1f, /* pio0-4 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &ahci_ops,
	},
};

static const struct pci_device_id ahci_pci_tbl[] = {
	/* Intel */
	{ PCI_VDEVICE(INTEL, 0x2652), board_ahci }, /* ICH6 */
	{ PCI_VDEVICE(INTEL, 0x2653), board_ahci }, /* ICH6M */
	{ PCI_VDEVICE(INTEL, 0x27c1), board_ahci }, /* ICH7 */
	{ PCI_VDEVICE(INTEL, 0x27c5), board_ahci }, /* ICH7M */
	{ PCI_VDEVICE(INTEL, 0x27c3), board_ahci }, /* ICH7R */
	{ PCI_VDEVICE(AL, 0x5288), board_ahci_ign_iferr }, /* ULi M5288 */
	{ PCI_VDEVICE(INTEL, 0x2681), board_ahci }, /* ESB2 */
	{ PCI_VDEVICE(INTEL, 0x2682), board_ahci }, /* ESB2 */
	{ PCI_VDEVICE(INTEL, 0x2683), board_ahci }, /* ESB2 */
	{ PCI_VDEVICE(INTEL, 0x27c6), board_ahci }, /* ICH7-M DH */
	{ PCI_VDEVICE(INTEL, 0x2821), board_ahci }, /* ICH8 */
	{ PCI_VDEVICE(INTEL, 0x2822), board_ahci }, /* ICH8 */
	{ PCI_VDEVICE(INTEL, 0x2824), board_ahci }, /* ICH8 */
	{ PCI_VDEVICE(INTEL, 0x2829), board_ahci }, /* ICH8M */
	{ PCI_VDEVICE(INTEL, 0x282a), board_ahci }, /* ICH8M */
	{ PCI_VDEVICE(INTEL, 0x2922), board_ahci }, /* ICH9 */
	{ PCI_VDEVICE(INTEL, 0x2923), board_ahci }, /* ICH9 */
	{ PCI_VDEVICE(INTEL, 0x2924), board_ahci }, /* ICH9 */
	{ PCI_VDEVICE(INTEL, 0x2925), board_ahci }, /* ICH9 */
	{ PCI_VDEVICE(INTEL, 0x2927), board_ahci }, /* ICH9 */
	{ PCI_VDEVICE(INTEL, 0x2929), board_ahci }, /* ICH9M */
	{ PCI_VDEVICE(INTEL, 0x292a), board_ahci }, /* ICH9M */
	{ PCI_VDEVICE(INTEL, 0x292b), board_ahci }, /* ICH9M */
	{ PCI_VDEVICE(INTEL, 0x292c), board_ahci }, /* ICH9M */
	{ PCI_VDEVICE(INTEL, 0x292f), board_ahci }, /* ICH9M */
	{ PCI_VDEVICE(INTEL, 0x294d), board_ahci }, /* ICH9 */
	{ PCI_VDEVICE(INTEL, 0x294e), board_ahci }, /* ICH9M */
	{ PCI_VDEVICE(INTEL, 0x502a), board_ahci }, /* Tolapai */
	{ PCI_VDEVICE(INTEL, 0x502b), board_ahci }, /* Tolapai */
	{ PCI_VDEVICE(INTEL, 0x3a05), board_ahci }, /* ICH10 */
	{ PCI_VDEVICE(INTEL, 0x3a25), board_ahci }, /* ICH10 */

	/* JMicron 360/1/3/5/6, match class to avoid IDE function */
	{ PCI_VENDOR_ID_JMICRON, PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID,
	  PCI_CLASS_STORAGE_SATA_AHCI, 0xffffff, board_ahci_ign_iferr },

	/* ATI */
	{ PCI_VDEVICE(ATI, 0x4380), board_ahci_sb600 }, /* ATI SB600 */
	{ PCI_VDEVICE(ATI, 0x4390), board_ahci_sb700 }, /* ATI SB700/800 */
	{ PCI_VDEVICE(ATI, 0x4391), board_ahci_sb700 }, /* ATI SB700/800 */
	{ PCI_VDEVICE(ATI, 0x4392), board_ahci_sb700 }, /* ATI SB700/800 */
	{ PCI_VDEVICE(ATI, 0x4393), board_ahci_sb700 }, /* ATI SB700/800 */
	{ PCI_VDEVICE(ATI, 0x4394), board_ahci_sb700 }, /* ATI SB700/800 */
	{ PCI_VDEVICE(ATI, 0x4395), board_ahci_sb700 }, /* ATI SB700/800 */

	/* VIA */
	{ PCI_VDEVICE(VIA, 0x3349), board_ahci_vt8251 }, /* VIA VT8251 */
	{ PCI_VDEVICE(VIA, 0x6287), board_ahci_vt8251 }, /* VIA VT8251 */

	/* NVIDIA */
	{ PCI_VDEVICE(NVIDIA, 0x044c), board_ahci },		/* MCP65 */
	{ PCI_VDEVICE(NVIDIA, 0x044d), board_ahci },		/* MCP65 */
	{ PCI_VDEVICE(NVIDIA, 0x044e), board_ahci },		/* MCP65 */
	{ PCI_VDEVICE(NVIDIA, 0x044f), board_ahci },		/* MCP65 */
	{ PCI_VDEVICE(NVIDIA, 0x045c), board_ahci },		/* MCP65 */
	{ PCI_VDEVICE(NVIDIA, 0x045d), board_ahci },		/* MCP65 */
	{ PCI_VDEVICE(NVIDIA, 0x045e), board_ahci },		/* MCP65 */
	{ PCI_VDEVICE(NVIDIA, 0x045f), board_ahci },		/* MCP65 */
	{ PCI_VDEVICE(NVIDIA, 0x0550), board_ahci },		/* MCP67 */
	{ PCI_VDEVICE(NVIDIA, 0x0551), board_ahci },		/* MCP67 */
	{ PCI_VDEVICE(NVIDIA, 0x0552), board_ahci },		/* MCP67 */
	{ PCI_VDEVICE(NVIDIA, 0x0553), board_ahci },		/* MCP67 */
	{ PCI_VDEVICE(NVIDIA, 0x0554), board_ahci },		/* MCP67 */
	{ PCI_VDEVICE(NVIDIA, 0x0555), board_ahci },		/* MCP67 */
	{ PCI_VDEVICE(NVIDIA, 0x0556), board_ahci },		/* MCP67 */
	{ PCI_VDEVICE(NVIDIA, 0x0557), board_ahci },		/* MCP67 */
	{ PCI_VDEVICE(NVIDIA, 0x0558), board_ahci },		/* MCP67 */
	{ PCI_VDEVICE(NVIDIA, 0x0559), board_ahci },		/* MCP67 */
	{ PCI_VDEVICE(NVIDIA, 0x055a), board_ahci },		/* MCP67 */
	{ PCI_VDEVICE(NVIDIA, 0x055b), board_ahci },		/* MCP67 */
	{ PCI_VDEVICE(NVIDIA, 0x07f0), board_ahci },		/* MCP73 */
	{ PCI_VDEVICE(NVIDIA, 0x07f1), board_ahci },		/* MCP73 */
	{ PCI_VDEVICE(NVIDIA, 0x07f2), board_ahci },		/* MCP73 */
	{ PCI_VDEVICE(NVIDIA, 0x07f3), board_ahci },		/* MCP73 */
	{ PCI_VDEVICE(NVIDIA, 0x07f4), board_ahci },		/* MCP73 */
	{ PCI_VDEVICE(NVIDIA, 0x07f5), board_ahci },		/* MCP73 */
	{ PCI_VDEVICE(NVIDIA, 0x07f6), board_ahci },		/* MCP73 */
	{ PCI_VDEVICE(NVIDIA, 0x07f7), board_ahci },		/* MCP73 */
	{ PCI_VDEVICE(NVIDIA, 0x07f8), board_ahci },		/* MCP73 */
	{ PCI_VDEVICE(NVIDIA, 0x07f9), board_ahci },		/* MCP73 */
	{ PCI_VDEVICE(NVIDIA, 0x07fa), board_ahci },		/* MCP73 */
	{ PCI_VDEVICE(NVIDIA, 0x07fb), board_ahci },		/* MCP73 */
	{ PCI_VDEVICE(NVIDIA, 0x0ad0), board_ahci },		/* MCP77 */
	{ PCI_VDEVICE(NVIDIA, 0x0ad1), board_ahci },		/* MCP77 */
	{ PCI_VDEVICE(NVIDIA, 0x0ad2), board_ahci },		/* MCP77 */
	{ PCI_VDEVICE(NVIDIA, 0x0ad3), board_ahci },		/* MCP77 */
	{ PCI_VDEVICE(NVIDIA, 0x0ad4), board_ahci },		/* MCP77 */
	{ PCI_VDEVICE(NVIDIA, 0x0ad5), board_ahci },		/* MCP77 */
	{ PCI_VDEVICE(NVIDIA, 0x0ad6), board_ahci },		/* MCP77 */
	{ PCI_VDEVICE(NVIDIA, 0x0ad7), board_ahci },		/* MCP77 */
	{ PCI_VDEVICE(NVIDIA, 0x0ad8), board_ahci },		/* MCP77 */
	{ PCI_VDEVICE(NVIDIA, 0x0ad9), board_ahci },		/* MCP77 */
	{ PCI_VDEVICE(NVIDIA, 0x0ada), board_ahci },		/* MCP77 */
	{ PCI_VDEVICE(NVIDIA, 0x0adb), board_ahci },		/* MCP77 */
	{ PCI_VDEVICE(NVIDIA, 0x0ab4), board_ahci },		/* MCP79 */
	{ PCI_VDEVICE(NVIDIA, 0x0ab5), board_ahci },		/* MCP79 */
	{ PCI_VDEVICE(NVIDIA, 0x0ab6), board_ahci },		/* MCP79 */
	{ PCI_VDEVICE(NVIDIA, 0x0ab7), board_ahci },		/* MCP79 */
	{ PCI_VDEVICE(NVIDIA, 0x0ab8), board_ahci },		/* MCP79 */
	{ PCI_VDEVICE(NVIDIA, 0x0ab9), board_ahci },		/* MCP79 */
	{ PCI_VDEVICE(NVIDIA, 0x0aba), board_ahci },		/* MCP79 */
	{ PCI_VDEVICE(NVIDIA, 0x0abb), board_ahci },		/* MCP79 */
	{ PCI_VDEVICE(NVIDIA, 0x0abc), board_ahci },		/* MCP79 */
	{ PCI_VDEVICE(NVIDIA, 0x0abd), board_ahci },		/* MCP79 */
	{ PCI_VDEVICE(NVIDIA, 0x0abe), board_ahci },		/* MCP79 */
	{ PCI_VDEVICE(NVIDIA, 0x0abf), board_ahci },		/* MCP79 */
	{ PCI_VDEVICE(NVIDIA, 0x0bc8), board_ahci },		/* MCP7B */
	{ PCI_VDEVICE(NVIDIA, 0x0bc9), board_ahci },		/* MCP7B */
	{ PCI_VDEVICE(NVIDIA, 0x0bca), board_ahci },		/* MCP7B */
	{ PCI_VDEVICE(NVIDIA, 0x0bcb), board_ahci },		/* MCP7B */
	{ PCI_VDEVICE(NVIDIA, 0x0bcc), board_ahci },		/* MCP7B */
	{ PCI_VDEVICE(NVIDIA, 0x0bcd), board_ahci },		/* MCP7B */
	{ PCI_VDEVICE(NVIDIA, 0x0bce), board_ahci },		/* MCP7B */
	{ PCI_VDEVICE(NVIDIA, 0x0bcf), board_ahci },		/* MCP7B */
	{ PCI_VDEVICE(NVIDIA, 0x0bd0), board_ahci },		/* MCP7B */
	{ PCI_VDEVICE(NVIDIA, 0x0bd1), board_ahci },		/* MCP7B */
	{ PCI_VDEVICE(NVIDIA, 0x0bd2), board_ahci },		/* MCP7B */
	{ PCI_VDEVICE(NVIDIA, 0x0bd3), board_ahci },		/* MCP7B */

	/* SiS */
	{ PCI_VDEVICE(SI, 0x1184), board_ahci }, /* SiS 966 */
	{ PCI_VDEVICE(SI, 0x1185), board_ahci }, /* SiS 966 */
	{ PCI_VDEVICE(SI, 0x0186), board_ahci }, /* SiS 968 */

	/* Marvell */
	{ PCI_VDEVICE(MARVELL, 0x6145), board_ahci_mv },	/* 6145 */
	{ PCI_VDEVICE(MARVELL, 0x6121), board_ahci_mv },	/* 6121 */

	/* Generic, PCI class code for AHCI */
	{ PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID,
	  PCI_CLASS_STORAGE_SATA_AHCI, 0xffffff, board_ahci },

	{ }	/* terminate list */
};


static struct pci_driver ahci_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= ahci_pci_tbl,
	.probe			= ahci_init_one,
	.remove			= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend		= ahci_pci_device_suspend,
	.resume			= ahci_pci_device_resume,
#endif
};


static inline int ahci_nr_ports(u32 cap)
{
	return (cap & 0x1f) + 1;
}

static inline void __iomem *__ahci_port_base(struct ata_host *host,
					     unsigned int port_no)
{
	void __iomem *mmio = host->iomap[AHCI_PCI_BAR];

	return mmio + 0x100 + (port_no * 0x80);
}

static inline void __iomem *ahci_port_base(struct ata_port *ap)
{
	return __ahci_port_base(ap->host, ap->port_no);
}

static void ahci_enable_ahci(void __iomem *mmio)
{
	u32 tmp;

	/* turn on AHCI_EN */
	tmp = readl(mmio + HOST_CTL);
	if (!(tmp & HOST_AHCI_EN)) {
		tmp |= HOST_AHCI_EN;
		writel(tmp, mmio + HOST_CTL);
		tmp = readl(mmio + HOST_CTL);	/* flush && sanity check */
		WARN_ON(!(tmp & HOST_AHCI_EN));
	}
}

/**
 *	ahci_save_initial_config - Save and fixup initial config values
 *	@pdev: target PCI device
 *	@hpriv: host private area to store config values
 *
 *	Some registers containing configuration info might be setup by
 *	BIOS and might be cleared on reset.  This function saves the
 *	initial values of those registers into @hpriv such that they
 *	can be restored after controller reset.
 *
 *	If inconsistent, config values are fixed up by this function.
 *
 *	LOCKING:
 *	None.
 */
static void ahci_save_initial_config(struct pci_dev *pdev,
				     struct ahci_host_priv *hpriv)
{
	void __iomem *mmio = pcim_iomap_table(pdev)[AHCI_PCI_BAR];
	u32 cap, port_map;
	int i;
	int mv;

	/* make sure AHCI mode is enabled before accessing CAP */
	ahci_enable_ahci(mmio);

	/* Values prefixed with saved_ are written back to host after
	 * reset.  Values without are used for driver operation.
	 */
	hpriv->saved_cap = cap = readl(mmio + HOST_CAP);
	hpriv->saved_port_map = port_map = readl(mmio + HOST_PORTS_IMPL);

	/* some chips have errata preventing 64bit use */
	if ((cap & HOST_CAP_64) && (hpriv->flags & AHCI_HFLAG_32BIT_ONLY)) {
		dev_printk(KERN_INFO, &pdev->dev,
			   "controller can't do 64bit DMA, forcing 32bit\n");
		cap &= ~HOST_CAP_64;
	}

	if ((cap & HOST_CAP_NCQ) && (hpriv->flags & AHCI_HFLAG_NO_NCQ)) {
		dev_printk(KERN_INFO, &pdev->dev,
			   "controller can't do NCQ, turning off CAP_NCQ\n");
		cap &= ~HOST_CAP_NCQ;
	}

	if ((cap & HOST_CAP_PMP) && (hpriv->flags & AHCI_HFLAG_NO_PMP)) {
		dev_printk(KERN_INFO, &pdev->dev,
			   "controller can't do PMP, turning off CAP_PMP\n");
		cap &= ~HOST_CAP_PMP;
	}

	/*
	 * Temporary Marvell 6145 hack: PATA port presence
	 * is asserted through the standard AHCI port
	 * presence register, as bit 4 (counting from 0)
	 */
	if (hpriv->flags & AHCI_HFLAG_MV_PATA) {
		if (pdev->device == 0x6121)
			mv = 0x3;
		else
			mv = 0xf;
		dev_printk(KERN_ERR, &pdev->dev,
			   "MV_AHCI HACK: port_map %x -> %x\n",
			   port_map,
			   port_map & mv);

		port_map &= mv;
	}

	/* cross check port_map and cap.n_ports */
	if (port_map) {
		int map_ports = 0;

		for (i = 0; i < AHCI_MAX_PORTS; i++)
			if (port_map & (1 << i))
				map_ports++;

		/* If PI has more ports than n_ports, whine, clear
		 * port_map and let it be generated from n_ports.
		 */
		if (map_ports > ahci_nr_ports(cap)) {
			dev_printk(KERN_WARNING, &pdev->dev,
				   "implemented port map (0x%x) contains more "
				   "ports than nr_ports (%u), using nr_ports\n",
				   port_map, ahci_nr_ports(cap));
			port_map = 0;
		}
	}

	/* fabricate port_map from cap.nr_ports */
	if (!port_map) {
		port_map = (1 << ahci_nr_ports(cap)) - 1;
		dev_printk(KERN_WARNING, &pdev->dev,
			   "forcing PORTS_IMPL to 0x%x\n", port_map);

		/* write the fixed up value to the PI register */
		hpriv->saved_port_map = port_map;
	}

	/* record values to use during operation */
	hpriv->cap = cap;
	hpriv->port_map = port_map;
}

/**
 *	ahci_restore_initial_config - Restore initial config
 *	@host: target ATA host
 *
 *	Restore initial config stored by ahci_save_initial_config().
 *
 *	LOCKING:
 *	None.
 */
static void ahci_restore_initial_config(struct ata_host *host)
{
	struct ahci_host_priv *hpriv = host->private_data;
	void __iomem *mmio = host->iomap[AHCI_PCI_BAR];

	writel(hpriv->saved_cap, mmio + HOST_CAP);
	writel(hpriv->saved_port_map, mmio + HOST_PORTS_IMPL);
	(void) readl(mmio + HOST_PORTS_IMPL);	/* flush */
}

static unsigned ahci_scr_offset(struct ata_port *ap, unsigned int sc_reg)
{
	static const int offset[] = {
		[SCR_STATUS]		= PORT_SCR_STAT,
		[SCR_CONTROL]		= PORT_SCR_CTL,
		[SCR_ERROR]		= PORT_SCR_ERR,
		[SCR_ACTIVE]		= PORT_SCR_ACT,
		[SCR_NOTIFICATION]	= PORT_SCR_NTF,
	};
	struct ahci_host_priv *hpriv = ap->host->private_data;

	if (sc_reg < ARRAY_SIZE(offset) &&
	    (sc_reg != SCR_NOTIFICATION || (hpriv->cap & HOST_CAP_SNTF)))
		return offset[sc_reg];
	return 0;
}

static int ahci_scr_read(struct ata_port *ap, unsigned int sc_reg, u32 *val)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	int offset = ahci_scr_offset(ap, sc_reg);

	if (offset) {
		*val = readl(port_mmio + offset);
		return 0;
	}
	return -EINVAL;
}

static int ahci_scr_write(struct ata_port *ap, unsigned int sc_reg, u32 val)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	int offset = ahci_scr_offset(ap, sc_reg);

	if (offset) {
		writel(val, port_mmio + offset);
		return 0;
	}
	return -EINVAL;
}

static void ahci_start_engine(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 tmp;

	/* start DMA */
	tmp = readl(port_mmio + PORT_CMD);
	tmp |= PORT_CMD_START;
	writel(tmp, port_mmio + PORT_CMD);
	readl(port_mmio + PORT_CMD); /* flush */
}

static int ahci_stop_engine(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 tmp;

	tmp = readl(port_mmio + PORT_CMD);

	/* check if the HBA is idle */
	if ((tmp & (PORT_CMD_START | PORT_CMD_LIST_ON)) == 0)
		return 0;

	/* setting HBA to idle */
	tmp &= ~PORT_CMD_START;
	writel(tmp, port_mmio + PORT_CMD);

	/* wait for engine to stop. This could be as long as 500 msec */
	tmp = ata_wait_register(port_mmio + PORT_CMD,
				PORT_CMD_LIST_ON, PORT_CMD_LIST_ON, 1, 500);
	if (tmp & PORT_CMD_LIST_ON)
		return -EIO;

	return 0;
}

static void ahci_start_fis_rx(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_host_priv *hpriv = ap->host->private_data;
	struct ahci_port_priv *pp = ap->private_data;
	u32 tmp;

	/* set FIS registers */
	if (hpriv->cap & HOST_CAP_64)
		writel((pp->cmd_slot_dma >> 16) >> 16,
		       port_mmio + PORT_LST_ADDR_HI);
	writel(pp->cmd_slot_dma & 0xffffffff, port_mmio + PORT_LST_ADDR);

	if (hpriv->cap & HOST_CAP_64)
		writel((pp->rx_fis_dma >> 16) >> 16,
		       port_mmio + PORT_FIS_ADDR_HI);
	writel(pp->rx_fis_dma & 0xffffffff, port_mmio + PORT_FIS_ADDR);

	/* enable FIS reception */
	tmp = readl(port_mmio + PORT_CMD);
	tmp |= PORT_CMD_FIS_RX;
	writel(tmp, port_mmio + PORT_CMD);

	/* flush */
	readl(port_mmio + PORT_CMD);
}

static int ahci_stop_fis_rx(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 tmp;

	/* disable FIS reception */
	tmp = readl(port_mmio + PORT_CMD);
	tmp &= ~PORT_CMD_FIS_RX;
	writel(tmp, port_mmio + PORT_CMD);

	/* wait for completion, spec says 500ms, give it 1000 */
	tmp = ata_wait_register(port_mmio + PORT_CMD, PORT_CMD_FIS_ON,
				PORT_CMD_FIS_ON, 10, 1000);
	if (tmp & PORT_CMD_FIS_ON)
		return -EBUSY;

	return 0;
}

static void ahci_power_up(struct ata_port *ap)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 cmd;

	cmd = readl(port_mmio + PORT_CMD) & ~PORT_CMD_ICC_MASK;

	/* spin up device */
	if (hpriv->cap & HOST_CAP_SSS) {
		cmd |= PORT_CMD_SPIN_UP;
		writel(cmd, port_mmio + PORT_CMD);
	}

	/* wake up link */
	writel(cmd | PORT_CMD_ICC_ACTIVE, port_mmio + PORT_CMD);
}

static void ahci_disable_alpm(struct ata_port *ap)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 cmd;
	struct ahci_port_priv *pp = ap->private_data;

	/* IPM bits should be disabled by libata-core */
	/* get the existing command bits */
	cmd = readl(port_mmio + PORT_CMD);

	/* disable ALPM and ASP */
	cmd &= ~PORT_CMD_ASP;
	cmd &= ~PORT_CMD_ALPE;

	/* force the interface back to active */
	cmd |= PORT_CMD_ICC_ACTIVE;

	/* write out new cmd value */
	writel(cmd, port_mmio + PORT_CMD);
	cmd = readl(port_mmio + PORT_CMD);

	/* wait 10ms to be sure we've come out of any low power state */
	msleep(10);

	/* clear out any PhyRdy stuff from interrupt status */
	writel(PORT_IRQ_PHYRDY, port_mmio + PORT_IRQ_STAT);

	/* go ahead and clean out PhyRdy Change from Serror too */
	ahci_scr_write(ap, SCR_ERROR, ((1 << 16) | (1 << 18)));

	/*
 	 * Clear flag to indicate that we should ignore all PhyRdy
 	 * state changes
 	 */
	hpriv->flags &= ~AHCI_HFLAG_NO_HOTPLUG;

	/*
 	 * Enable interrupts on Phy Ready.
 	 */
	pp->intr_mask |= PORT_IRQ_PHYRDY;
	writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK);

	/*
 	 * don't change the link pm policy - we can be called
 	 * just to turn of link pm temporarily
 	 */
}

static int ahci_enable_alpm(struct ata_port *ap,
	enum link_pm policy)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 cmd;
	struct ahci_port_priv *pp = ap->private_data;
	u32 asp;

	/* Make sure the host is capable of link power management */
	if (!(hpriv->cap & HOST_CAP_ALPM))
		return -EINVAL;

	switch (policy) {
	case MAX_PERFORMANCE:
	case NOT_AVAILABLE:
		/*
 		 * if we came here with NOT_AVAILABLE,
 		 * it just means this is the first time we
 		 * have tried to enable - default to max performance,
 		 * and let the user go to lower power modes on request.
 		 */
		ahci_disable_alpm(ap);
		return 0;
	case MIN_POWER:
		/* configure HBA to enter SLUMBER */
		asp = PORT_CMD_ASP;
		break;
	case MEDIUM_POWER:
		/* configure HBA to enter PARTIAL */
		asp = 0;
		break;
	default:
		return -EINVAL;
	}

	/*
 	 * Disable interrupts on Phy Ready. This keeps us from
 	 * getting woken up due to spurious phy ready interrupts
	 * TBD - Hot plug should be done via polling now, is
	 * that even supported?
 	 */
	pp->intr_mask &= ~PORT_IRQ_PHYRDY;
	writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK);

	/*
 	 * Set a flag to indicate that we should ignore all PhyRdy
 	 * state changes since these can happen now whenever we
 	 * change link state
 	 */
	hpriv->flags |= AHCI_HFLAG_NO_HOTPLUG;

	/* get the existing command bits */
	cmd = readl(port_mmio + PORT_CMD);

	/*
 	 * Set ASP based on Policy
 	 */
	cmd |= asp;

	/*
 	 * Setting this bit will instruct the HBA to aggressively
 	 * enter a lower power link state when it's appropriate and
 	 * based on the value set above for ASP
 	 */
	cmd |= PORT_CMD_ALPE;

	/* write out new cmd value */
	writel(cmd, port_mmio + PORT_CMD);
	cmd = readl(port_mmio + PORT_CMD);

	/* IPM bits should be set by libata-core */
	return 0;
}

#ifdef CONFIG_PM
static void ahci_power_down(struct ata_port *ap)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 cmd, scontrol;

	if (!(hpriv->cap & HOST_CAP_SSS))
		return;

	/* put device into listen mode, first set PxSCTL.DET to 0 */
	scontrol = readl(port_mmio + PORT_SCR_CTL);
	scontrol &= ~0xf;
	writel(scontrol, port_mmio + PORT_SCR_CTL);

	/* then set PxCMD.SUD to 0 */
	cmd = readl(port_mmio + PORT_CMD) & ~PORT_CMD_ICC_MASK;
	cmd &= ~PORT_CMD_SPIN_UP;
	writel(cmd, port_mmio + PORT_CMD);
}
#endif

static void ahci_start_port(struct ata_port *ap)
{
	/* enable FIS reception */
	ahci_start_fis_rx(ap);

	/* enable DMA */
	ahci_start_engine(ap);
}

static int ahci_deinit_port(struct ata_port *ap, const char **emsg)
{
	int rc;

	/* disable DMA */
	rc = ahci_stop_engine(ap);
	if (rc) {
		*emsg = "failed to stop engine";
		return rc;
	}

	/* disable FIS reception */
	rc = ahci_stop_fis_rx(ap);
	if (rc) {
		*emsg = "failed stop FIS RX";
		return rc;
	}

	return 0;
}

static int ahci_reset_controller(struct ata_host *host)
{
	struct pci_dev *pdev = to_pci_dev(host->dev);
	struct ahci_host_priv *hpriv = host->private_data;
	void __iomem *mmio = host->iomap[AHCI_PCI_BAR];
	u32 tmp;

	/* we must be in AHCI mode, before using anything
	 * AHCI-specific, such as HOST_RESET.
	 */
	ahci_enable_ahci(mmio);

	/* global controller reset */
	if (!ahci_skip_host_reset) {
		tmp = readl(mmio + HOST_CTL);
		if ((tmp & HOST_RESET) == 0) {
			writel(tmp | HOST_RESET, mmio + HOST_CTL);
			readl(mmio + HOST_CTL); /* flush */
		}

		/* reset must complete within 1 second, or
		 * the hardware should be considered fried.
		 */
		ssleep(1);

		tmp = readl(mmio + HOST_CTL);
		if (tmp & HOST_RESET) {
			dev_printk(KERN_ERR, host->dev,
				   "controller reset failed (0x%x)\n", tmp);
			return -EIO;
		}

		/* turn on AHCI mode */
		ahci_enable_ahci(mmio);

		/* Some registers might be cleared on reset.  Restore
		 * initial values.
		 */
		ahci_restore_initial_config(host);
	} else
		dev_printk(KERN_INFO, host->dev,
			   "skipping global host reset\n");

	if (pdev->vendor == PCI_VENDOR_ID_INTEL) {
		u16 tmp16;

		/* configure PCS */
		pci_read_config_word(pdev, 0x92, &tmp16);
		if ((tmp16 & hpriv->port_map) != hpriv->port_map) {
			tmp16 |= hpriv->port_map;
			pci_write_config_word(pdev, 0x92, tmp16);
		}
	}

	return 0;
}

static void ahci_port_init(struct pci_dev *pdev, struct ata_port *ap,
			   int port_no, void __iomem *mmio,
			   void __iomem *port_mmio)
{
	const char *emsg = NULL;
	int rc;
	u32 tmp;

	/* make sure port is not active */
	rc = ahci_deinit_port(ap, &emsg);
	if (rc)
		dev_printk(KERN_WARNING, &pdev->dev,
			   "%s (%d)\n", emsg, rc);

	/* clear SError */
	tmp = readl(port_mmio + PORT_SCR_ERR);
	VPRINTK("PORT_SCR_ERR 0x%x\n", tmp);
	writel(tmp, port_mmio + PORT_SCR_ERR);

	/* clear port IRQ */
	tmp = readl(port_mmio + PORT_IRQ_STAT);
	VPRINTK("PORT_IRQ_STAT 0x%x\n", tmp);
	if (tmp)
		writel(tmp, port_mmio + PORT_IRQ_STAT);

	writel(1 << port_no, mmio + HOST_IRQ_STAT);
}

static void ahci_init_controller(struct ata_host *host)
{
	struct ahci_host_priv *hpriv = host->private_data;
	struct pci_dev *pdev = to_pci_dev(host->dev);
	void __iomem *mmio = host->iomap[AHCI_PCI_BAR];
	int i;
	void __iomem *port_mmio;
	u32 tmp;
	int mv;

	if (hpriv->flags & AHCI_HFLAG_MV_PATA) {
		if (pdev->device == 0x6121)
			mv = 2;
		else
			mv = 4;
		port_mmio = __ahci_port_base(host, mv);

		writel(0, port_mmio + PORT_IRQ_MASK);

		/* clear port IRQ */
		tmp = readl(port_mmio + PORT_IRQ_STAT);
		VPRINTK("PORT_IRQ_STAT 0x%x\n", tmp);
		if (tmp)
			writel(tmp, port_mmio + PORT_IRQ_STAT);
	}

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		port_mmio = ahci_port_base(ap);
		if (ata_port_is_dummy(ap))
			continue;

		ahci_port_init(pdev, ap, i, mmio, port_mmio);
	}

	tmp = readl(mmio + HOST_CTL);
	VPRINTK("HOST_CTL 0x%x\n", tmp);
	writel(tmp | HOST_IRQ_EN, mmio + HOST_CTL);
	tmp = readl(mmio + HOST_CTL);
	VPRINTK("HOST_CTL 0x%x\n", tmp);
}

static void ahci_dev_config(struct ata_device *dev)
{
	struct ahci_host_priv *hpriv = dev->link->ap->host->private_data;

	if (hpriv->flags & AHCI_HFLAG_SECT255) {
		dev->max_sectors = 255;
		ata_dev_printk(dev, KERN_INFO,
			       "SB600 AHCI: limiting to 255 sectors per cmd\n");
	}
}

static unsigned int ahci_dev_classify(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ata_taskfile tf;
	u32 tmp;

	tmp = readl(port_mmio + PORT_SIG);
	tf.lbah		= (tmp >> 24)	& 0xff;
	tf.lbam		= (tmp >> 16)	& 0xff;
	tf.lbal		= (tmp >> 8)	& 0xff;
	tf.nsect	= (tmp)		& 0xff;

	return ata_dev_classify(&tf);
}

static void ahci_fill_cmd_slot(struct ahci_port_priv *pp, unsigned int tag,
			       u32 opts)
{
	dma_addr_t cmd_tbl_dma;

	cmd_tbl_dma = pp->cmd_tbl_dma + tag * AHCI_CMD_TBL_SZ;

	pp->cmd_slot[tag].opts = cpu_to_le32(opts);
	pp->cmd_slot[tag].status = 0;
	pp->cmd_slot[tag].tbl_addr = cpu_to_le32(cmd_tbl_dma & 0xffffffff);
	pp->cmd_slot[tag].tbl_addr_hi = cpu_to_le32((cmd_tbl_dma >> 16) >> 16);
}

static int ahci_kick_engine(struct ata_port *ap, int force_restart)
{
	void __iomem *port_mmio = ap->ioaddr.cmd_addr;
	struct ahci_host_priv *hpriv = ap->host->private_data;
	u32 tmp;
	int busy, rc;

	/* do we need to kick the port? */
	busy = ahci_check_status(ap) & (ATA_BUSY | ATA_DRQ);
	if (!busy && !force_restart)
		return 0;

	/* stop engine */
	rc = ahci_stop_engine(ap);
	if (rc)
		goto out_restart;

	/* need to do CLO? */
	if (!busy) {
		rc = 0;
		goto out_restart;
	}

	if (!(hpriv->cap & HOST_CAP_CLO)) {
		rc = -EOPNOTSUPP;
		goto out_restart;
	}

	/* perform CLO */
	tmp = readl(port_mmio + PORT_CMD);
	tmp |= PORT_CMD_CLO;
	writel(tmp, port_mmio + PORT_CMD);

	rc = 0;
	tmp = ata_wait_register(port_mmio + PORT_CMD,
				PORT_CMD_CLO, PORT_CMD_CLO, 1, 500);
	if (tmp & PORT_CMD_CLO)
		rc = -EIO;

	/* restart engine */
 out_restart:
	ahci_start_engine(ap);
	return rc;
}

static int ahci_exec_polled_cmd(struct ata_port *ap, int pmp,
				struct ata_taskfile *tf, int is_cmd, u16 flags,
				unsigned long timeout_msec)
{
	const u32 cmd_fis_len = 5; /* five dwords */
	struct ahci_port_priv *pp = ap->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	u8 *fis = pp->cmd_tbl;
	u32 tmp;

	/* prep the command */
	ata_tf_to_fis(tf, pmp, is_cmd, fis);
	ahci_fill_cmd_slot(pp, 0, cmd_fis_len | flags | (pmp << 12));

	/* issue & wait */
	writel(1, port_mmio + PORT_CMD_ISSUE);

	if (timeout_msec) {
		tmp = ata_wait_register(port_mmio + PORT_CMD_ISSUE, 0x1, 0x1,
					1, timeout_msec);
		if (tmp & 0x1) {
			ahci_kick_engine(ap, 1);
			return -EBUSY;
		}
	} else
		readl(port_mmio + PORT_CMD_ISSUE);	/* flush */

	return 0;
}

static int ahci_do_softreset(struct ata_link *link, unsigned int *class,
			     int pmp, unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	const char *reason = NULL;
	unsigned long now, msecs;
	struct ata_taskfile tf;
	int rc;

	DPRINTK("ENTER\n");

	if (ata_link_offline(link)) {
		DPRINTK("PHY reports no device\n");
		*class = ATA_DEV_NONE;
		return 0;
	}

	/* prepare for SRST (AHCI-1.1 10.4.1) */
	rc = ahci_kick_engine(ap, 1);
	if (rc && rc != -EOPNOTSUPP)
		ata_link_printk(link, KERN_WARNING,
				"failed to reset engine (errno=%d)\n", rc);

	ata_tf_init(link->device, &tf);

	/* issue the first D2H Register FIS */
	msecs = 0;
	now = jiffies;
	if (time_after(now, deadline))
		msecs = jiffies_to_msecs(deadline - now);

	tf.ctl |= ATA_SRST;
	if (ahci_exec_polled_cmd(ap, pmp, &tf, 0,
				 AHCI_CMD_RESET | AHCI_CMD_CLR_BUSY, msecs)) {
		rc = -EIO;
		reason = "1st FIS failed";
		goto fail;
	}

	/* spec says at least 5us, but be generous and sleep for 1ms */
	msleep(1);

	/* issue the second D2H Register FIS */
	tf.ctl &= ~ATA_SRST;
	ahci_exec_polled_cmd(ap, pmp, &tf, 0, 0, 0);

	/* wait a while before checking status */
	ata_wait_after_reset(ap, deadline);

	rc = ata_wait_ready(ap, deadline);
	/* link occupied, -ENODEV too is an error */
	if (rc) {
		reason = "device not ready";
		goto fail;
	}
	*class = ahci_dev_classify(ap);

	DPRINTK("EXIT, class=%u\n", *class);
	return 0;

 fail:
	ata_link_printk(link, KERN_ERR, "softreset failed (%s)\n", reason);
	return rc;
}

static int ahci_softreset(struct ata_link *link, unsigned int *class,
			  unsigned long deadline)
{
	int pmp = 0;

	if (link->ap->flags & ATA_FLAG_PMP)
		pmp = SATA_PMP_CTRL_PORT;

	return ahci_do_softreset(link, class, pmp, deadline);
}

static int ahci_hardreset(struct ata_link *link, unsigned int *class,
			  unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct ahci_port_priv *pp = ap->private_data;
	u8 *d2h_fis = pp->rx_fis + RX_FIS_D2H_REG;
	struct ata_taskfile tf;
	int rc;

	DPRINTK("ENTER\n");

	ahci_stop_engine(ap);

	/* clear D2H reception area to properly wait for D2H FIS */
	ata_tf_init(link->device, &tf);
	tf.command = 0x80;
	ata_tf_to_fis(&tf, 0, 0, d2h_fis);

	rc = sata_std_hardreset(link, class, deadline);

	ahci_start_engine(ap);

	if (rc == 0 && ata_link_online(link))
		*class = ahci_dev_classify(ap);
	if (rc != -EAGAIN && *class == ATA_DEV_UNKNOWN)
		*class = ATA_DEV_NONE;

	DPRINTK("EXIT, rc=%d, class=%u\n", rc, *class);
	return rc;
}

static int ahci_vt8251_hardreset(struct ata_link *link, unsigned int *class,
				 unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	u32 serror;
	int rc;

	DPRINTK("ENTER\n");

	ahci_stop_engine(ap);

	rc = sata_link_hardreset(link, sata_ehc_deb_timing(&link->eh_context),
				 deadline);

	/* vt8251 needs SError cleared for the port to operate */
	ahci_scr_read(ap, SCR_ERROR, &serror);
	ahci_scr_write(ap, SCR_ERROR, serror);

	ahci_start_engine(ap);

	DPRINTK("EXIT, rc=%d, class=%u\n", rc, *class);

	/* vt8251 doesn't clear BSY on signature FIS reception,
	 * request follow-up softreset.
	 */
	return rc ?: -EAGAIN;
}

static int ahci_p5wdh_hardreset(struct ata_link *link, unsigned int *class,
				unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct ahci_port_priv *pp = ap->private_data;
	u8 *d2h_fis = pp->rx_fis + RX_FIS_D2H_REG;
	struct ata_taskfile tf;
	int rc;

	ahci_stop_engine(ap);

	/* clear D2H reception area to properly wait for D2H FIS */
	ata_tf_init(link->device, &tf);
	tf.command = 0x80;
	ata_tf_to_fis(&tf, 0, 0, d2h_fis);

	rc = sata_link_hardreset(link, sata_ehc_deb_timing(&link->eh_context),
				 deadline);

	ahci_start_engine(ap);

	if (rc || ata_link_offline(link))
		return rc;

	/* spec mandates ">= 2ms" before checking status */
	msleep(150);

	/* The pseudo configuration device on SIMG4726 attached to
	 * ASUS P5W-DH Deluxe doesn't send signature FIS after
	 * hardreset if no device is attached to the first downstream
	 * port && the pseudo device locks up on SRST w/ PMP==0.  To
	 * work around this, wait for !BSY only briefly.  If BSY isn't
	 * cleared, perform CLO and proceed to IDENTIFY (achieved by
	 * ATA_LFLAG_NO_SRST and ATA_LFLAG_ASSUME_ATA).
	 *
	 * Wait for two seconds.  Devices attached to downstream port
	 * which can't process the following IDENTIFY after this will
	 * have to be reset again.  For most cases, this should
	 * suffice while making probing snappish enough.
	 */
	rc = ata_wait_ready(ap, jiffies + 2 * HZ);
	if (rc)
		ahci_kick_engine(ap, 0);

	return 0;
}

static void ahci_postreset(struct ata_link *link, unsigned int *class)
{
	struct ata_port *ap = link->ap;
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 new_tmp, tmp;

	ata_std_postreset(link, class);

	/* Make sure port's ATAPI bit is set appropriately */
	new_tmp = tmp = readl(port_mmio + PORT_CMD);
	if (*class == ATA_DEV_ATAPI)
		new_tmp |= PORT_CMD_ATAPI;
	else
		new_tmp &= ~PORT_CMD_ATAPI;
	if (new_tmp != tmp) {
		writel(new_tmp, port_mmio + PORT_CMD);
		readl(port_mmio + PORT_CMD); /* flush */
	}
}

static int ahci_pmp_softreset(struct ata_link *link, unsigned int *class,
			      unsigned long deadline)
{
	return ahci_do_softreset(link, class, link->pmp, deadline);
}

static u8 ahci_check_status(struct ata_port *ap)
{
	void __iomem *mmio = ap->ioaddr.cmd_addr;

	return readl(mmio + PORT_TFDATA) & 0xFF;
}

static void ahci_tf_read(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ahci_port_priv *pp = ap->private_data;
	u8 *d2h_fis = pp->rx_fis + RX_FIS_D2H_REG;

	ata_tf_from_fis(d2h_fis, tf);
}

static unsigned int ahci_fill_sg(struct ata_queued_cmd *qc, void *cmd_tbl)
{
	struct scatterlist *sg;
	struct ahci_sg *ahci_sg = cmd_tbl + AHCI_CMD_TBL_HDR_SZ;
	unsigned int si;

	VPRINTK("ENTER\n");

	/*
	 * Next, the S/G list.
	 */
	for_each_sg(qc->sg, sg, qc->n_elem, si) {
		dma_addr_t addr = sg_dma_address(sg);
		u32 sg_len = sg_dma_len(sg);

		ahci_sg[si].addr = cpu_to_le32(addr & 0xffffffff);
		ahci_sg[si].addr_hi = cpu_to_le32((addr >> 16) >> 16);
		ahci_sg[si].flags_size = cpu_to_le32(sg_len - 1);
	}

	return si;
}

static void ahci_qc_prep(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ahci_port_priv *pp = ap->private_data;
	int is_atapi = ata_is_atapi(qc->tf.protocol);
	void *cmd_tbl;
	u32 opts;
	const u32 cmd_fis_len = 5; /* five dwords */
	unsigned int n_elem;

	/*
	 * Fill in command table information.  First, the header,
	 * a SATA Register - Host to Device command FIS.
	 */
	cmd_tbl = pp->cmd_tbl + qc->tag * AHCI_CMD_TBL_SZ;

	ata_tf_to_fis(&qc->tf, qc->dev->link->pmp, 1, cmd_tbl);
	if (is_atapi) {
		memset(cmd_tbl + AHCI_CMD_TBL_CDB, 0, 32);
		memcpy(cmd_tbl + AHCI_CMD_TBL_CDB, qc->cdb, qc->dev->cdb_len);
	}

	n_elem = 0;
	if (qc->flags & ATA_QCFLAG_DMAMAP)
		n_elem = ahci_fill_sg(qc, cmd_tbl);

	/*
	 * Fill in command slot information.
	 */
	opts = cmd_fis_len | n_elem << 16 | (qc->dev->link->pmp << 12);
	if (qc->tf.flags & ATA_TFLAG_WRITE)
		opts |= AHCI_CMD_WRITE;
	if (is_atapi)
		opts |= AHCI_CMD_ATAPI | AHCI_CMD_PREFETCH;

	ahci_fill_cmd_slot(pp, qc->tag, opts);
}

static void ahci_error_intr(struct ata_port *ap, u32 irq_stat)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	struct ahci_port_priv *pp = ap->private_data;
	struct ata_eh_info *host_ehi = &ap->link.eh_info;
	struct ata_link *link = NULL;
	struct ata_queued_cmd *active_qc;
	struct ata_eh_info *active_ehi;
	u32 serror;

	/* determine active link */
	ata_port_for_each_link(link, ap)
		if (ata_link_active(link))
			break;
	if (!link)
		link = &ap->link;

	active_qc = ata_qc_from_tag(ap, link->active_tag);
	active_ehi = &link->eh_info;

	/* record irq stat */
	ata_ehi_clear_desc(host_ehi);
	ata_ehi_push_desc(host_ehi, "irq_stat 0x%08x", irq_stat);

	/* AHCI needs SError cleared; otherwise, it might lock up */
	ahci_scr_read(ap, SCR_ERROR, &serror);
	ahci_scr_write(ap, SCR_ERROR, serror);
	host_ehi->serror |= serror;

	/* some controllers set IRQ_IF_ERR on device errors, ignore it */
	if (hpriv->flags & AHCI_HFLAG_IGN_IRQ_IF_ERR)
		irq_stat &= ~PORT_IRQ_IF_ERR;

	if (irq_stat & PORT_IRQ_TF_ERR) {
		/* If qc is active, charge it; otherwise, the active
		 * link.  There's no active qc on NCQ errors.  It will
		 * be determined by EH by reading log page 10h.
		 */
		if (active_qc)
			active_qc->err_mask |= AC_ERR_DEV;
		else
			active_ehi->err_mask |= AC_ERR_DEV;

		if (hpriv->flags & AHCI_HFLAG_IGN_SERR_INTERNAL)
			host_ehi->serror &= ~SERR_INTERNAL;
	}

	if (irq_stat & PORT_IRQ_UNK_FIS) {
		u32 *unk = (u32 *)(pp->rx_fis + RX_FIS_UNK);

		active_ehi->err_mask |= AC_ERR_HSM;
		active_ehi->action |= ATA_EH_SOFTRESET;
		ata_ehi_push_desc(active_ehi,
				  "unknown FIS %08x %08x %08x %08x" ,
				  unk[0], unk[1], unk[2], unk[3]);
	}

	if (ap->nr_pmp_links && (irq_stat & PORT_IRQ_BAD_PMP)) {
		active_ehi->err_mask |= AC_ERR_HSM;
		active_ehi->action |= ATA_EH_SOFTRESET;
		ata_ehi_push_desc(active_ehi, "incorrect PMP");
	}

	if (irq_stat & (PORT_IRQ_HBUS_ERR | PORT_IRQ_HBUS_DATA_ERR)) {
		host_ehi->err_mask |= AC_ERR_HOST_BUS;
		host_ehi->action |= ATA_EH_SOFTRESET;
		ata_ehi_push_desc(host_ehi, "host bus error");
	}

	if (irq_stat & PORT_IRQ_IF_ERR) {
		host_ehi->err_mask |= AC_ERR_ATA_BUS;
		host_ehi->action |= ATA_EH_SOFTRESET;
		ata_ehi_push_desc(host_ehi, "interface fatal error");
	}

	if (irq_stat & (PORT_IRQ_CONNECT | PORT_IRQ_PHYRDY)) {
		ata_ehi_hotplugged(host_ehi);
		ata_ehi_push_desc(host_ehi, "%s",
			irq_stat & PORT_IRQ_CONNECT ?
			"connection status changed" : "PHY RDY changed");
	}

	/* okay, let's hand over to EH */

	if (irq_stat & PORT_IRQ_FREEZE)
		ata_port_freeze(ap);
	else
		ata_port_abort(ap);
}

static void ahci_port_intr(struct ata_port *ap)
{
	void __iomem *port_mmio = ap->ioaddr.cmd_addr;
	struct ata_eh_info *ehi = &ap->link.eh_info;
	struct ahci_port_priv *pp = ap->private_data;
	struct ahci_host_priv *hpriv = ap->host->private_data;
	int resetting = !!(ap->pflags & ATA_PFLAG_RESETTING);
	u32 status, qc_active;
	int rc;

	status = readl(port_mmio + PORT_IRQ_STAT);
	writel(status, port_mmio + PORT_IRQ_STAT);

	/* ignore BAD_PMP while resetting */
	if (unlikely(resetting))
		status &= ~PORT_IRQ_BAD_PMP;

	/* If we are getting PhyRdy, this is
 	 * just a power state change, we should
 	 * clear out this, plus the PhyRdy/Comm
 	 * Wake bits from Serror
 	 */
	if ((hpriv->flags & AHCI_HFLAG_NO_HOTPLUG) &&
		(status & PORT_IRQ_PHYRDY)) {
		status &= ~PORT_IRQ_PHYRDY;
		ahci_scr_write(ap, SCR_ERROR, ((1 << 16) | (1 << 18)));
	}

	if (unlikely(status & PORT_IRQ_ERROR)) {
		ahci_error_intr(ap, status);
		return;
	}

	if (status & PORT_IRQ_SDB_FIS) {
		/* If SNotification is available, leave notification
		 * handling to sata_async_notification().  If not,
		 * emulate it by snooping SDB FIS RX area.
		 *
		 * Snooping FIS RX area is probably cheaper than
		 * poking SNotification but some constrollers which
		 * implement SNotification, ICH9 for example, don't
		 * store AN SDB FIS into receive area.
		 */
		if (hpriv->cap & HOST_CAP_SNTF)
			sata_async_notification(ap);
		else {
			/* If the 'N' bit in word 0 of the FIS is set,
			 * we just received asynchronous notification.
			 * Tell libata about it.
			 */
			const __le32 *f = pp->rx_fis + RX_FIS_SDB;
			u32 f0 = le32_to_cpu(f[0]);

			if (f0 & (1 << 15))
				sata_async_notification(ap);
		}
	}

	/* pp->active_link is valid iff any command is in flight */
	if (ap->qc_active && pp->active_link->sactive)
		qc_active = readl(port_mmio + PORT_SCR_ACT);
	else
		qc_active = readl(port_mmio + PORT_CMD_ISSUE);

	rc = ata_qc_complete_multiple(ap, qc_active, NULL);

	/* while resetting, invalid completions are expected */
	if (unlikely(rc < 0 && !resetting)) {
		ehi->err_mask |= AC_ERR_HSM;
		ehi->action |= ATA_EH_SOFTRESET;
		ata_port_freeze(ap);
	}
}

static void ahci_irq_clear(struct ata_port *ap)
{
	/* TODO */
}

static irqreturn_t ahci_interrupt(int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	struct ahci_host_priv *hpriv;
	unsigned int i, handled = 0;
	void __iomem *mmio;
	u32 irq_stat, irq_ack = 0;

	VPRINTK("ENTER\n");

	hpriv = host->private_data;
	mmio = host->iomap[AHCI_PCI_BAR];

	/* sigh.  0xffffffff is a valid return from h/w */
	irq_stat = readl(mmio + HOST_IRQ_STAT);
	irq_stat &= hpriv->port_map;
	if (!irq_stat)
		return IRQ_NONE;

	spin_lock(&host->lock);

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap;

		if (!(irq_stat & (1 << i)))
			continue;

		ap = host->ports[i];
		if (ap) {
			ahci_port_intr(ap);
			VPRINTK("port %u\n", i);
		} else {
			VPRINTK("port %u (no irq)\n", i);
			if (ata_ratelimit())
				dev_printk(KERN_WARNING, host->dev,
					"interrupt on disabled port %u\n", i);
		}

		irq_ack |= (1 << i);
	}

	if (irq_ack) {
		writel(irq_ack, mmio + HOST_IRQ_STAT);
		handled = 1;
	}

	spin_unlock(&host->lock);

	VPRINTK("EXIT\n");

	return IRQ_RETVAL(handled);
}

static unsigned int ahci_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_port_priv *pp = ap->private_data;

	/* Keep track of the currently active link.  It will be used
	 * in completion path to determine whether NCQ phase is in
	 * progress.
	 */
	pp->active_link = qc->dev->link;

	if (qc->tf.protocol == ATA_PROT_NCQ)
		writel(1 << qc->tag, port_mmio + PORT_SCR_ACT);
	writel(1 << qc->tag, port_mmio + PORT_CMD_ISSUE);
	readl(port_mmio + PORT_CMD_ISSUE);	/* flush */

	return 0;
}

static void ahci_freeze(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);

	/* turn IRQ off */
	writel(0, port_mmio + PORT_IRQ_MASK);
}

static void ahci_thaw(struct ata_port *ap)
{
	void __iomem *mmio = ap->host->iomap[AHCI_PCI_BAR];
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 tmp;
	struct ahci_port_priv *pp = ap->private_data;

	/* clear IRQ */
	tmp = readl(port_mmio + PORT_IRQ_STAT);
	writel(tmp, port_mmio + PORT_IRQ_STAT);
	writel(1 << ap->port_no, mmio + HOST_IRQ_STAT);

	/* turn IRQ back on */
	writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK);
}

static void ahci_error_handler(struct ata_port *ap)
{
	if (!(ap->pflags & ATA_PFLAG_FROZEN)) {
		/* restart engine */
		ahci_stop_engine(ap);
		ahci_start_engine(ap);
	}

	/* perform recovery */
	sata_pmp_do_eh(ap, ata_std_prereset, ahci_softreset,
		       ahci_hardreset, ahci_postreset,
		       sata_pmp_std_prereset, ahci_pmp_softreset,
		       sata_pmp_std_hardreset, sata_pmp_std_postreset);
}

static void ahci_vt8251_error_handler(struct ata_port *ap)
{
	if (!(ap->pflags & ATA_PFLAG_FROZEN)) {
		/* restart engine */
		ahci_stop_engine(ap);
		ahci_start_engine(ap);
	}

	/* perform recovery */
	ata_do_eh(ap, ata_std_prereset, ahci_softreset, ahci_vt8251_hardreset,
		  ahci_postreset);
}

static void ahci_p5wdh_error_handler(struct ata_port *ap)
{
	if (!(ap->pflags & ATA_PFLAG_FROZEN)) {
		/* restart engine */
		ahci_stop_engine(ap);
		ahci_start_engine(ap);
	}

	/* perform recovery */
	ata_do_eh(ap, ata_std_prereset, ahci_softreset, ahci_p5wdh_hardreset,
		  ahci_postreset);
}

static void ahci_post_internal_cmd(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	/* make DMA engine forget about the failed command */
	if (qc->flags & ATA_QCFLAG_FAILED)
		ahci_kick_engine(ap, 1);
}

static void ahci_pmp_attach(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_port_priv *pp = ap->private_data;
	u32 cmd;

	cmd = readl(port_mmio + PORT_CMD);
	cmd |= PORT_CMD_PMP;
	writel(cmd, port_mmio + PORT_CMD);

	pp->intr_mask |= PORT_IRQ_BAD_PMP;
	writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK);
}

static void ahci_pmp_detach(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_port_priv *pp = ap->private_data;
	u32 cmd;

	cmd = readl(port_mmio + PORT_CMD);
	cmd &= ~PORT_CMD_PMP;
	writel(cmd, port_mmio + PORT_CMD);

	pp->intr_mask &= ~PORT_IRQ_BAD_PMP;
	writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK);
}

static int ahci_port_resume(struct ata_port *ap)
{
	ahci_power_up(ap);
	ahci_start_port(ap);

	if (ap->nr_pmp_links)
		ahci_pmp_attach(ap);
	else
		ahci_pmp_detach(ap);

	return 0;
}

#ifdef CONFIG_PM
static int ahci_port_suspend(struct ata_port *ap, pm_message_t mesg)
{
	const char *emsg = NULL;
	int rc;

	rc = ahci_deinit_port(ap, &emsg);
	if (rc == 0)
		ahci_power_down(ap);
	else {
		ata_port_printk(ap, KERN_ERR, "%s (%d)\n", emsg, rc);
		ahci_start_port(ap);
	}

	return rc;
}

static int ahci_pci_device_suspend(struct pci_dev *pdev, pm_message_t mesg)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	void __iomem *mmio = host->iomap[AHCI_PCI_BAR];
	u32 ctl;

	if (mesg.event & PM_EVENT_SLEEP) {
		/* AHCI spec rev1.1 section 8.3.3:
		 * Software must disable interrupts prior to requesting a
		 * transition of the HBA to D3 state.
		 */
		ctl = readl(mmio + HOST_CTL);
		ctl &= ~HOST_IRQ_EN;
		writel(ctl, mmio + HOST_CTL);
		readl(mmio + HOST_CTL); /* flush */
	}

	return ata_pci_device_suspend(pdev, mesg);
}

static int ahci_pci_device_resume(struct pci_dev *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	int rc;

	rc = ata_pci_device_do_resume(pdev);
	if (rc)
		return rc;

	if (pdev->dev.power.power_state.event == PM_EVENT_SUSPEND) {
		rc = ahci_reset_controller(host);
		if (rc)
			return rc;

		ahci_init_controller(host);
	}

	ata_host_resume(host);

	return 0;
}
#endif

static int ahci_port_start(struct ata_port *ap)
{
	struct device *dev = ap->host->dev;
	struct ahci_port_priv *pp;
	void *mem;
	dma_addr_t mem_dma;

	pp = devm_kzalloc(dev, sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;

	mem = dmam_alloc_coherent(dev, AHCI_PORT_PRIV_DMA_SZ, &mem_dma,
				  GFP_KERNEL);
	if (!mem)
		return -ENOMEM;
	memset(mem, 0, AHCI_PORT_PRIV_DMA_SZ);

	/*
	 * First item in chunk of DMA memory: 32-slot command table,
	 * 32 bytes each in size
	 */
	pp->cmd_slot = mem;
	pp->cmd_slot_dma = mem_dma;

	mem += AHCI_CMD_SLOT_SZ;
	mem_dma += AHCI_CMD_SLOT_SZ;

	/*
	 * Second item: Received-FIS area
	 */
	pp->rx_fis = mem;
	pp->rx_fis_dma = mem_dma;

	mem += AHCI_RX_FIS_SZ;
	mem_dma += AHCI_RX_FIS_SZ;

	/*
	 * Third item: data area for storing a single command
	 * and its scatter-gather table
	 */
	pp->cmd_tbl = mem;
	pp->cmd_tbl_dma = mem_dma;

	/*
	 * Save off initial list of interrupts to be enabled.
	 * This could be changed later
	 */
	pp->intr_mask = DEF_PORT_IRQ;

	ap->private_data = pp;

	/* engage engines, captain */
	return ahci_port_resume(ap);
}

static void ahci_port_stop(struct ata_port *ap)
{
	const char *emsg = NULL;
	int rc;

	/* de-initialize port */
	rc = ahci_deinit_port(ap, &emsg);
	if (rc)
		ata_port_printk(ap, KERN_WARNING, "%s (%d)\n", emsg, rc);
}

static int ahci_configure_dma_masks(struct pci_dev *pdev, int using_dac)
{
	int rc;

	if (using_dac &&
	    !pci_set_dma_mask(pdev, DMA_64BIT_MASK)) {
		rc = pci_set_consistent_dma_mask(pdev, DMA_64BIT_MASK);
		if (rc) {
			rc = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
			if (rc) {
				dev_printk(KERN_ERR, &pdev->dev,
					   "64-bit DMA enable failed\n");
				return rc;
			}
		}
	} else {
		rc = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
		if (rc) {
			dev_printk(KERN_ERR, &pdev->dev,
				   "32-bit DMA enable failed\n");
			return rc;
		}
		rc = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
		if (rc) {
			dev_printk(KERN_ERR, &pdev->dev,
				   "32-bit consistent DMA enable failed\n");
			return rc;
		}
	}
	return 0;
}

static void ahci_print_info(struct ata_host *host)
{
	struct ahci_host_priv *hpriv = host->private_data;
	struct pci_dev *pdev = to_pci_dev(host->dev);
	void __iomem *mmio = host->iomap[AHCI_PCI_BAR];
	u32 vers, cap, impl, speed;
	const char *speed_s;
	u16 cc;
	const char *scc_s;

	vers = readl(mmio + HOST_VERSION);
	cap = hpriv->cap;
	impl = hpriv->port_map;

	speed = (cap >> 20) & 0xf;
	if (speed == 1)
		speed_s = "1.5";
	else if (speed == 2)
		speed_s = "3";
	else
		speed_s = "?";

	pci_read_config_word(pdev, 0x0a, &cc);
	if (cc == PCI_CLASS_STORAGE_IDE)
		scc_s = "IDE";
	else if (cc == PCI_CLASS_STORAGE_SATA)
		scc_s = "SATA";
	else if (cc == PCI_CLASS_STORAGE_RAID)
		scc_s = "RAID";
	else
		scc_s = "unknown";

	dev_printk(KERN_INFO, &pdev->dev,
		"AHCI %02x%02x.%02x%02x "
		"%u slots %u ports %s Gbps 0x%x impl %s mode\n"
		,

		(vers >> 24) & 0xff,
		(vers >> 16) & 0xff,
		(vers >> 8) & 0xff,
		vers & 0xff,

		((cap >> 8) & 0x1f) + 1,
		(cap & 0x1f) + 1,
		speed_s,
		impl,
		scc_s);

	dev_printk(KERN_INFO, &pdev->dev,
		"flags: "
		"%s%s%s%s%s%s%s"
		"%s%s%s%s%s%s%s\n"
		,

		cap & (1 << 31) ? "64bit " : "",
		cap & (1 << 30) ? "ncq " : "",
		cap & (1 << 29) ? "sntf " : "",
		cap & (1 << 28) ? "ilck " : "",
		cap & (1 << 27) ? "stag " : "",
		cap & (1 << 26) ? "pm " : "",
		cap & (1 << 25) ? "led " : "",

		cap & (1 << 24) ? "clo " : "",
		cap & (1 << 19) ? "nz " : "",
		cap & (1 << 18) ? "only " : "",
		cap & (1 << 17) ? "pmp " : "",
		cap & (1 << 15) ? "pio " : "",
		cap & (1 << 14) ? "slum " : "",
		cap & (1 << 13) ? "part " : ""
		);
}

/* On ASUS P5W DH Deluxe, the second port of PCI device 00:1f.2 is
 * hardwired to on-board SIMG 4726.  The chipset is ICH8 and doesn't
 * support PMP and the 4726 either directly exports the device
 * attached to the first downstream port or acts as a hardware storage
 * controller and emulate a single ATA device (can be RAID 0/1 or some
 * other configuration).
 *
 * When there's no device attached to the first downstream port of the
 * 4726, "Config Disk" appears, which is a pseudo ATA device to
 * configure the 4726.  However, ATA emulation of the device is very
 * lame.  It doesn't send signature D2H Reg FIS after the initial
 * hardreset, pukes on SRST w/ PMP==0 and has bunch of other issues.
 *
 * The following function works around the problem by always using
 * hardreset on the port and not depending on receiving signature FIS
 * afterward.  If signature FIS isn't received soon, ATA class is
 * assumed without follow-up softreset.
 */
static void ahci_p5wdh_workaround(struct ata_host *host)
{
	static struct dmi_system_id sysids[] = {
		{
			.ident = "P5W DH Deluxe",
			.matches = {
				DMI_MATCH(DMI_SYS_VENDOR,
					  "ASUSTEK COMPUTER INC"),
				DMI_MATCH(DMI_PRODUCT_NAME, "P5W DH Deluxe"),
			},
		},
		{ }
	};
	struct pci_dev *pdev = to_pci_dev(host->dev);

	if (pdev->bus->number == 0 && pdev->devfn == PCI_DEVFN(0x1f, 2) &&
	    dmi_check_system(sysids)) {
		struct ata_port *ap = host->ports[1];

		dev_printk(KERN_INFO, &pdev->dev, "enabling ASUS P5W DH "
			   "Deluxe on-board SIMG4726 workaround\n");

		ap->ops = &ahci_p5wdh_ops;
		ap->link.flags |= ATA_LFLAG_NO_SRST | ATA_LFLAG_ASSUME_ATA;
	}
}

static int ahci_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	struct ata_port_info pi = ahci_port_info[ent->driver_data];
	const struct ata_port_info *ppi[] = { &pi, NULL };
	struct device *dev = &pdev->dev;
	struct ahci_host_priv *hpriv;
	struct ata_host *host;
	int n_ports, i, rc;

	VPRINTK("ENTER\n");

	WARN_ON(ATA_MAX_QUEUE > AHCI_MAX_CMDS);

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	/* acquire resources */
	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	/* AHCI controllers often implement SFF compatible interface.
	 * Grab all PCI BARs just in case.
	 */
	rc = pcim_iomap_regions_request_all(pdev, 1 << AHCI_PCI_BAR, DRV_NAME);
	if (rc == -EBUSY)
		pcim_pin_device(pdev);
	if (rc)
		return rc;

	if (pdev->vendor == PCI_VENDOR_ID_INTEL &&
	    (pdev->device == 0x2652 || pdev->device == 0x2653)) {
		u8 map;

		/* ICH6s share the same PCI ID for both piix and ahci
		 * modes.  Enabling ahci mode while MAP indicates
		 * combined mode is a bad idea.  Yield to ata_piix.
		 */
		pci_read_config_byte(pdev, ICH_MAP, &map);
		if (map & 0x3) {
			dev_printk(KERN_INFO, &pdev->dev, "controller is in "
				   "combined mode, can't enable AHCI mode\n");
			return -ENODEV;
		}
	}

	hpriv = devm_kzalloc(dev, sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv)
		return -ENOMEM;
	hpriv->flags |= (unsigned long)pi.private_data;

	if ((hpriv->flags & AHCI_HFLAG_NO_MSI) || pci_enable_msi(pdev))
		pci_intx(pdev, 1);

	/* save initial config */
	ahci_save_initial_config(pdev, hpriv);

	/* prepare host */
	if (hpriv->cap & HOST_CAP_NCQ)
		pi.flags |= ATA_FLAG_NCQ;

	if (hpriv->cap & HOST_CAP_PMP)
		pi.flags |= ATA_FLAG_PMP;

	/* CAP.NP sometimes indicate the index of the last enabled
	 * port, at other times, that of the last possible port, so
	 * determining the maximum port number requires looking at
	 * both CAP.NP and port_map.
	 */
	n_ports = max(ahci_nr_ports(hpriv->cap), fls(hpriv->port_map));

	host = ata_host_alloc_pinfo(&pdev->dev, ppi, n_ports);
	if (!host)
		return -ENOMEM;
	host->iomap = pcim_iomap_table(pdev);
	host->private_data = hpriv;

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];
		void __iomem *port_mmio = ahci_port_base(ap);

		ata_port_pbar_desc(ap, AHCI_PCI_BAR, -1, "abar");
		ata_port_pbar_desc(ap, AHCI_PCI_BAR,
				   0x100 + ap->port_no * 0x80, "port");

		/* set initial link pm policy */
		ap->pm_policy = NOT_AVAILABLE;

		/* standard SATA port setup */
		if (hpriv->port_map & (1 << i))
			ap->ioaddr.cmd_addr = port_mmio;

		/* disabled/not-implemented port */
		else
			ap->ops = &ata_dummy_port_ops;
	}

	/* apply workaround for ASUS P5W DH Deluxe mainboard */
	ahci_p5wdh_workaround(host);

	/* initialize adapter */
	rc = ahci_configure_dma_masks(pdev, hpriv->cap & HOST_CAP_64);
	if (rc)
		return rc;

	rc = ahci_reset_controller(host);
	if (rc)
		return rc;

	ahci_init_controller(host);
	ahci_print_info(host);

	pci_set_master(pdev);
	return ata_host_activate(host, pdev->irq, ahci_interrupt, IRQF_SHARED,
				 &ahci_sht);
}

static int __init ahci_init(void)
{
	return pci_register_driver(&ahci_pci_driver);
}

static void __exit ahci_exit(void)
{
	pci_unregister_driver(&ahci_pci_driver);
}


MODULE_AUTHOR("Jeff Garzik");
MODULE_DESCRIPTION("AHCI SATA low-level driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, ahci_pci_tbl);
MODULE_VERSION(DRV_VERSION);

module_init(ahci_init);
module_exit(ahci_exit);
