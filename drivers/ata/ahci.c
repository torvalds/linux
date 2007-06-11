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
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <linux/libata.h>

#define DRV_NAME	"ahci"
#define DRV_VERSION	"2.2"


enum {
	AHCI_PCI_BAR		= 5,
	AHCI_MAX_PORTS		= 32,
	AHCI_MAX_SG		= 168, /* hardware max is 64K */
	AHCI_DMA_BOUNDARY	= 0xffffffff,
	AHCI_USE_CLUSTERING	= 0,
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
	board_ahci_pi		= 1,
	board_ahci_vt8251	= 2,
	board_ahci_ign_iferr	= 3,
	board_ahci_sb600	= 4,

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
	HOST_CAP_CLO		= (1 << 24), /* Command List Override support */
	HOST_CAP_SSS		= (1 << 27), /* Staggered Spin-up */
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
	PORT_SCR		= 0x28, /* SATA phy register block */
	PORT_SCR_STAT		= 0x28, /* SATA phy register: SStatus */
	PORT_SCR_CTL		= 0x2c, /* SATA phy register: SControl */
	PORT_SCR_ERR		= 0x30, /* SATA phy register: SError */
	PORT_SCR_ACT		= 0x34, /* SATA phy register: SActive */

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
				  PORT_IRQ_UNK_FIS,
	PORT_IRQ_ERROR		= PORT_IRQ_FREEZE |
				  PORT_IRQ_TF_ERR |
				  PORT_IRQ_HBUS_DATA_ERR,
	DEF_PORT_IRQ		= PORT_IRQ_ERROR | PORT_IRQ_SG_DONE |
				  PORT_IRQ_SDB_FIS | PORT_IRQ_DMAS_FIS |
				  PORT_IRQ_PIOS_FIS | PORT_IRQ_D2H_REG_FIS,

	/* PORT_CMD bits */
	PORT_CMD_ATAPI		= (1 << 24), /* Device is ATAPI */
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

	/* ap->flags bits */
	AHCI_FLAG_NO_NCQ		= (1 << 24),
	AHCI_FLAG_IGN_IRQ_IF_ERR	= (1 << 25), /* ignore IRQ_IF_ERR */
	AHCI_FLAG_HONOR_PI		= (1 << 26), /* honor PORTS_IMPL */
	AHCI_FLAG_IGN_SERR_INTERNAL	= (1 << 27), /* ignore SERR_INTERNAL */
	AHCI_FLAG_32BIT_ONLY		= (1 << 28), /* force 32bit */

	AHCI_FLAG_COMMON		= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
					  ATA_FLAG_MMIO | ATA_FLAG_PIO_DMA |
					  ATA_FLAG_SKIP_D2H_BSY |
					  ATA_FLAG_ACPI_SATA,
};

struct ahci_cmd_hdr {
	u32			opts;
	u32			status;
	u32			tbl_addr;
	u32			tbl_addr_hi;
	u32			reserved[4];
};

struct ahci_sg {
	u32			addr;
	u32			addr_hi;
	u32			reserved;
	u32			flags_size;
};

struct ahci_host_priv {
	u32			cap;		/* cap to use */
	u32			port_map;	/* port map to use */
	u32			saved_cap;	/* saved initial cap */
	u32			saved_port_map;	/* saved initial port_map */
};

struct ahci_port_priv {
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
};

static u32 ahci_scr_read (struct ata_port *ap, unsigned int sc_reg);
static void ahci_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val);
static int ahci_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static unsigned int ahci_qc_issue(struct ata_queued_cmd *qc);
static void ahci_irq_clear(struct ata_port *ap);
static int ahci_port_start(struct ata_port *ap);
static void ahci_port_stop(struct ata_port *ap);
static void ahci_tf_read(struct ata_port *ap, struct ata_taskfile *tf);
static void ahci_qc_prep(struct ata_queued_cmd *qc);
static u8 ahci_check_status(struct ata_port *ap);
static void ahci_freeze(struct ata_port *ap);
static void ahci_thaw(struct ata_port *ap);
static void ahci_error_handler(struct ata_port *ap);
static void ahci_vt8251_error_handler(struct ata_port *ap);
static void ahci_post_internal_cmd(struct ata_queued_cmd *qc);
#ifdef CONFIG_PM
static int ahci_port_suspend(struct ata_port *ap, pm_message_t mesg);
static int ahci_port_resume(struct ata_port *ap);
static int ahci_pci_device_suspend(struct pci_dev *pdev, pm_message_t mesg);
static int ahci_pci_device_resume(struct pci_dev *pdev);
#endif

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
};

static const struct ata_port_operations ahci_ops = {
	.port_disable		= ata_port_disable,

	.check_status		= ahci_check_status,
	.check_altstatus	= ahci_check_status,
	.dev_select		= ata_noop_dev_select,

	.tf_read		= ahci_tf_read,

	.qc_prep		= ahci_qc_prep,
	.qc_issue		= ahci_qc_issue,

	.irq_clear		= ahci_irq_clear,
	.irq_on			= ata_dummy_irq_on,
	.irq_ack		= ata_dummy_irq_ack,

	.scr_read		= ahci_scr_read,
	.scr_write		= ahci_scr_write,

	.freeze			= ahci_freeze,
	.thaw			= ahci_thaw,

	.error_handler		= ahci_error_handler,
	.post_internal_cmd	= ahci_post_internal_cmd,

#ifdef CONFIG_PM
	.port_suspend		= ahci_port_suspend,
	.port_resume		= ahci_port_resume,
#endif

	.port_start		= ahci_port_start,
	.port_stop		= ahci_port_stop,
};

static const struct ata_port_operations ahci_vt8251_ops = {
	.port_disable		= ata_port_disable,

	.check_status		= ahci_check_status,
	.check_altstatus	= ahci_check_status,
	.dev_select		= ata_noop_dev_select,

	.tf_read		= ahci_tf_read,

	.qc_prep		= ahci_qc_prep,
	.qc_issue		= ahci_qc_issue,

	.irq_clear		= ahci_irq_clear,
	.irq_on			= ata_dummy_irq_on,
	.irq_ack		= ata_dummy_irq_ack,

	.scr_read		= ahci_scr_read,
	.scr_write		= ahci_scr_write,

	.freeze			= ahci_freeze,
	.thaw			= ahci_thaw,

	.error_handler		= ahci_vt8251_error_handler,
	.post_internal_cmd	= ahci_post_internal_cmd,

#ifdef CONFIG_PM
	.port_suspend		= ahci_port_suspend,
	.port_resume		= ahci_port_resume,
#endif

	.port_start		= ahci_port_start,
	.port_stop		= ahci_port_stop,
};

static const struct ata_port_info ahci_port_info[] = {
	/* board_ahci */
	{
		.flags		= AHCI_FLAG_COMMON,
		.pio_mask	= 0x1f, /* pio0-4 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &ahci_ops,
	},
	/* board_ahci_pi */
	{
		.flags		= AHCI_FLAG_COMMON | AHCI_FLAG_HONOR_PI,
		.pio_mask	= 0x1f, /* pio0-4 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &ahci_ops,
	},
	/* board_ahci_vt8251 */
	{
		.flags		= AHCI_FLAG_COMMON | ATA_FLAG_HRST_TO_RESUME |
				  AHCI_FLAG_NO_NCQ,
		.pio_mask	= 0x1f, /* pio0-4 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &ahci_vt8251_ops,
	},
	/* board_ahci_ign_iferr */
	{
		.flags		= AHCI_FLAG_COMMON | AHCI_FLAG_IGN_IRQ_IF_ERR,
		.pio_mask	= 0x1f, /* pio0-4 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &ahci_ops,
	},
	/* board_ahci_sb600 */
	{
		.flags		= AHCI_FLAG_COMMON |
				  AHCI_FLAG_IGN_SERR_INTERNAL |
				  AHCI_FLAG_32BIT_ONLY,
		.pio_mask	= 0x1f, /* pio0-4 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
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
	{ PCI_VDEVICE(INTEL, 0x2821), board_ahci_pi }, /* ICH8 */
	{ PCI_VDEVICE(INTEL, 0x2822), board_ahci_pi }, /* ICH8 */
	{ PCI_VDEVICE(INTEL, 0x2824), board_ahci_pi }, /* ICH8 */
	{ PCI_VDEVICE(INTEL, 0x2829), board_ahci_pi }, /* ICH8M */
	{ PCI_VDEVICE(INTEL, 0x282a), board_ahci_pi }, /* ICH8M */
	{ PCI_VDEVICE(INTEL, 0x2922), board_ahci_pi }, /* ICH9 */
	{ PCI_VDEVICE(INTEL, 0x2923), board_ahci_pi }, /* ICH9 */
	{ PCI_VDEVICE(INTEL, 0x2924), board_ahci_pi }, /* ICH9 */
	{ PCI_VDEVICE(INTEL, 0x2925), board_ahci_pi }, /* ICH9 */
	{ PCI_VDEVICE(INTEL, 0x2927), board_ahci_pi }, /* ICH9 */
	{ PCI_VDEVICE(INTEL, 0x2929), board_ahci_pi }, /* ICH9M */
	{ PCI_VDEVICE(INTEL, 0x292a), board_ahci_pi }, /* ICH9M */
	{ PCI_VDEVICE(INTEL, 0x292b), board_ahci_pi }, /* ICH9M */
	{ PCI_VDEVICE(INTEL, 0x292c), board_ahci_pi }, /* ICH9M */
	{ PCI_VDEVICE(INTEL, 0x292f), board_ahci_pi }, /* ICH9M */
	{ PCI_VDEVICE(INTEL, 0x294d), board_ahci_pi }, /* ICH9 */
	{ PCI_VDEVICE(INTEL, 0x294e), board_ahci_pi }, /* ICH9M */

	/* JMicron 360/1/3/5/6, match class to avoid IDE function */
	{ PCI_VENDOR_ID_JMICRON, PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID,
	  PCI_CLASS_STORAGE_SATA_AHCI, 0xffffff, board_ahci_ign_iferr },

	/* ATI */
	{ PCI_VDEVICE(ATI, 0x4380), board_ahci_sb600 }, /* ATI SB600 */
	{ PCI_VDEVICE(ATI, 0x4390), board_ahci_sb600 }, /* ATI SB700 */

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

	/* SiS */
	{ PCI_VDEVICE(SI, 0x1184), board_ahci }, /* SiS 966 */
	{ PCI_VDEVICE(SI, 0x1185), board_ahci }, /* SiS 966 */
	{ PCI_VDEVICE(SI, 0x0186), board_ahci }, /* SiS 968 */

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

static inline void __iomem *ahci_port_base(struct ata_port *ap)
{
	void __iomem *mmio = ap->host->iomap[AHCI_PCI_BAR];

	return mmio + 0x100 + (ap->port_no * 0x80);
}

/**
 *	ahci_save_initial_config - Save and fixup initial config values
 *	@pdev: target PCI device
 *	@pi: associated ATA port info
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
				     const struct ata_port_info *pi,
				     struct ahci_host_priv *hpriv)
{
	void __iomem *mmio = pcim_iomap_table(pdev)[AHCI_PCI_BAR];
	u32 cap, port_map;
	int i;

	/* Values prefixed with saved_ are written back to host after
	 * reset.  Values without are used for driver operation.
	 */
	hpriv->saved_cap = cap = readl(mmio + HOST_CAP);
	hpriv->saved_port_map = port_map = readl(mmio + HOST_PORTS_IMPL);

	/* some chips lie about 64bit support */
	if ((cap & HOST_CAP_64) && (pi->flags & AHCI_FLAG_32BIT_ONLY)) {
		dev_printk(KERN_INFO, &pdev->dev,
			   "controller can't do 64bit DMA, forcing 32bit\n");
		cap &= ~HOST_CAP_64;
	}

	/* fixup zero port_map */
	if (!port_map) {
		port_map = (1 << ahci_nr_ports(hpriv->cap)) - 1;
		dev_printk(KERN_WARNING, &pdev->dev,
			   "PORTS_IMPL is zero, forcing 0x%x\n", port_map);

		/* write the fixed up value to the PI register */
		hpriv->saved_port_map = port_map;
	}

	/* cross check port_map and cap.n_ports */
	if (pi->flags & AHCI_FLAG_HONOR_PI) {
		u32 tmp_port_map = port_map;
		int n_ports = ahci_nr_ports(cap);

		for (i = 0; i < AHCI_MAX_PORTS && n_ports; i++) {
			if (tmp_port_map & (1 << i)) {
				n_ports--;
				tmp_port_map &= ~(1 << i);
			}
		}

		/* Whine if inconsistent.  No need to update cap.
		 * port_map is used to determine number of ports.
		 */
		if (n_ports || tmp_port_map)
			dev_printk(KERN_WARNING, &pdev->dev,
				   "nr_ports (%u) and implemented port map "
				   "(0x%x) don't match\n",
				   ahci_nr_ports(cap), port_map);
	} else {
		/* fabricate port_map from cap.nr_ports */
		port_map = (1 << ahci_nr_ports(cap)) - 1;
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

static u32 ahci_scr_read (struct ata_port *ap, unsigned int sc_reg_in)
{
	unsigned int sc_reg;

	switch (sc_reg_in) {
	case SCR_STATUS:	sc_reg = 0; break;
	case SCR_CONTROL:	sc_reg = 1; break;
	case SCR_ERROR:		sc_reg = 2; break;
	case SCR_ACTIVE:	sc_reg = 3; break;
	default:
		return 0xffffffffU;
	}

	return readl(ap->ioaddr.scr_addr + (sc_reg * 4));
}


static void ahci_scr_write (struct ata_port *ap, unsigned int sc_reg_in,
			       u32 val)
{
	unsigned int sc_reg;

	switch (sc_reg_in) {
	case SCR_STATUS:	sc_reg = 0; break;
	case SCR_CONTROL:	sc_reg = 1; break;
	case SCR_ERROR:		sc_reg = 2; break;
	case SCR_ACTIVE:	sc_reg = 3; break;
	default:
		return;
	}

	writel(val, ap->ioaddr.scr_addr + (sc_reg * 4));
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

static void ahci_init_port(struct ata_port *ap)
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
	void __iomem *mmio = host->iomap[AHCI_PCI_BAR];
	u32 tmp;

	/* global controller reset */
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
	writel(HOST_AHCI_EN, mmio + HOST_CTL);
	(void) readl(mmio + HOST_CTL);	/* flush */

	/* some registers might be cleared on reset.  restore initial values */
	ahci_restore_initial_config(host);

	if (pdev->vendor == PCI_VENDOR_ID_INTEL) {
		u16 tmp16;

		/* configure PCS */
		pci_read_config_word(pdev, 0x92, &tmp16);
		tmp16 |= 0xf;
		pci_write_config_word(pdev, 0x92, tmp16);
	}

	return 0;
}

static void ahci_init_controller(struct ata_host *host)
{
	struct pci_dev *pdev = to_pci_dev(host->dev);
	void __iomem *mmio = host->iomap[AHCI_PCI_BAR];
	int i, rc;
	u32 tmp;

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];
		void __iomem *port_mmio = ahci_port_base(ap);
		const char *emsg = NULL;

		if (ata_port_is_dummy(ap))
			continue;

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

		writel(1 << i, mmio + HOST_IRQ_STAT);
	}

	tmp = readl(mmio + HOST_CTL);
	VPRINTK("HOST_CTL 0x%x\n", tmp);
	writel(tmp | HOST_IRQ_EN, mmio + HOST_CTL);
	tmp = readl(mmio + HOST_CTL);
	VPRINTK("HOST_CTL 0x%x\n", tmp);
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

static int ahci_clo(struct ata_port *ap)
{
	void __iomem *port_mmio = ap->ioaddr.cmd_addr;
	struct ahci_host_priv *hpriv = ap->host->private_data;
	u32 tmp;

	if (!(hpriv->cap & HOST_CAP_CLO))
		return -EOPNOTSUPP;

	tmp = readl(port_mmio + PORT_CMD);
	tmp |= PORT_CMD_CLO;
	writel(tmp, port_mmio + PORT_CMD);

	tmp = ata_wait_register(port_mmio + PORT_CMD,
				PORT_CMD_CLO, PORT_CMD_CLO, 1, 500);
	if (tmp & PORT_CMD_CLO)
		return -EIO;

	return 0;
}

static int ahci_softreset(struct ata_port *ap, unsigned int *class,
			  unsigned long deadline)
{
	struct ahci_port_priv *pp = ap->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	const u32 cmd_fis_len = 5; /* five dwords */
	const char *reason = NULL;
	struct ata_taskfile tf;
	u32 tmp;
	u8 *fis;
	int rc;

	DPRINTK("ENTER\n");

	if (ata_port_offline(ap)) {
		DPRINTK("PHY reports no device\n");
		*class = ATA_DEV_NONE;
		return 0;
	}

	/* prepare for SRST (AHCI-1.1 10.4.1) */
	rc = ahci_stop_engine(ap);
	if (rc) {
		reason = "failed to stop engine";
		goto fail_restart;
	}

	/* check BUSY/DRQ, perform Command List Override if necessary */
	if (ahci_check_status(ap) & (ATA_BUSY | ATA_DRQ)) {
		rc = ahci_clo(ap);

		if (rc == -EOPNOTSUPP) {
			reason = "port busy but CLO unavailable";
			goto fail_restart;
		} else if (rc) {
			reason = "port busy but CLO failed";
			goto fail_restart;
		}
	}

	/* restart engine */
	ahci_start_engine(ap);

	ata_tf_init(ap->device, &tf);
	fis = pp->cmd_tbl;

	/* issue the first D2H Register FIS */
	ahci_fill_cmd_slot(pp, 0,
			   cmd_fis_len | AHCI_CMD_RESET | AHCI_CMD_CLR_BUSY);

	tf.ctl |= ATA_SRST;
	ata_tf_to_fis(&tf, fis, 0);
	fis[1] &= ~(1 << 7);	/* turn off Command FIS bit */

	writel(1, port_mmio + PORT_CMD_ISSUE);

	tmp = ata_wait_register(port_mmio + PORT_CMD_ISSUE, 0x1, 0x1, 1, 500);
	if (tmp & 0x1) {
		rc = -EIO;
		reason = "1st FIS failed";
		goto fail;
	}

	/* spec says at least 5us, but be generous and sleep for 1ms */
	msleep(1);

	/* issue the second D2H Register FIS */
	ahci_fill_cmd_slot(pp, 0, cmd_fis_len);

	tf.ctl &= ~ATA_SRST;
	ata_tf_to_fis(&tf, fis, 0);
	fis[1] &= ~(1 << 7);	/* turn off Command FIS bit */

	writel(1, port_mmio + PORT_CMD_ISSUE);
	readl(port_mmio + PORT_CMD_ISSUE);	/* flush */

	/* spec mandates ">= 2ms" before checking status.
	 * We wait 150ms, because that was the magic delay used for
	 * ATAPI devices in Hale Landis's ATADRVR, for the period of time
	 * between when the ATA command register is written, and then
	 * status is checked.  Because waiting for "a while" before
	 * checking status is fine, post SRST, we perform this magic
	 * delay here as well.
	 */
	msleep(150);

	rc = ata_wait_ready(ap, deadline);
	/* link occupied, -ENODEV too is an error */
	if (rc) {
		reason = "device not ready";
		goto fail;
	}
	*class = ahci_dev_classify(ap);

	DPRINTK("EXIT, class=%u\n", *class);
	return 0;

 fail_restart:
	ahci_start_engine(ap);
 fail:
	ata_port_printk(ap, KERN_ERR, "softreset failed (%s)\n", reason);
	return rc;
}

static int ahci_hardreset(struct ata_port *ap, unsigned int *class,
			  unsigned long deadline)
{
	struct ahci_port_priv *pp = ap->private_data;
	u8 *d2h_fis = pp->rx_fis + RX_FIS_D2H_REG;
	struct ata_taskfile tf;
	int rc;

	DPRINTK("ENTER\n");

	ahci_stop_engine(ap);

	/* clear D2H reception area to properly wait for D2H FIS */
	ata_tf_init(ap->device, &tf);
	tf.command = 0x80;
	ata_tf_to_fis(&tf, d2h_fis, 0);

	rc = sata_std_hardreset(ap, class, deadline);

	ahci_start_engine(ap);

	if (rc == 0 && ata_port_online(ap))
		*class = ahci_dev_classify(ap);
	if (*class == ATA_DEV_UNKNOWN)
		*class = ATA_DEV_NONE;

	DPRINTK("EXIT, rc=%d, class=%u\n", rc, *class);
	return rc;
}

static int ahci_vt8251_hardreset(struct ata_port *ap, unsigned int *class,
				 unsigned long deadline)
{
	int rc;

	DPRINTK("ENTER\n");

	ahci_stop_engine(ap);

	rc = sata_port_hardreset(ap, sata_ehc_deb_timing(&ap->eh_context),
				 deadline);

	/* vt8251 needs SError cleared for the port to operate */
	ahci_scr_write(ap, SCR_ERROR, ahci_scr_read(ap, SCR_ERROR));

	ahci_start_engine(ap);

	DPRINTK("EXIT, rc=%d, class=%u\n", rc, *class);

	/* vt8251 doesn't clear BSY on signature FIS reception,
	 * request follow-up softreset.
	 */
	return rc ?: -EAGAIN;
}

static void ahci_postreset(struct ata_port *ap, unsigned int *class)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 new_tmp, tmp;

	ata_std_postreset(ap, class);

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
	struct ahci_sg *ahci_sg;
	unsigned int n_sg = 0;

	VPRINTK("ENTER\n");

	/*
	 * Next, the S/G list.
	 */
	ahci_sg = cmd_tbl + AHCI_CMD_TBL_HDR_SZ;
	ata_for_each_sg(sg, qc) {
		dma_addr_t addr = sg_dma_address(sg);
		u32 sg_len = sg_dma_len(sg);

		ahci_sg->addr = cpu_to_le32(addr & 0xffffffff);
		ahci_sg->addr_hi = cpu_to_le32((addr >> 16) >> 16);
		ahci_sg->flags_size = cpu_to_le32(sg_len - 1);

		ahci_sg++;
		n_sg++;
	}

	return n_sg;
}

static void ahci_qc_prep(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ahci_port_priv *pp = ap->private_data;
	int is_atapi = is_atapi_taskfile(&qc->tf);
	void *cmd_tbl;
	u32 opts;
	const u32 cmd_fis_len = 5; /* five dwords */
	unsigned int n_elem;

	/*
	 * Fill in command table information.  First, the header,
	 * a SATA Register - Host to Device command FIS.
	 */
	cmd_tbl = pp->cmd_tbl + qc->tag * AHCI_CMD_TBL_SZ;

	ata_tf_to_fis(&qc->tf, cmd_tbl, 0);
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
	opts = cmd_fis_len | n_elem << 16;
	if (qc->tf.flags & ATA_TFLAG_WRITE)
		opts |= AHCI_CMD_WRITE;
	if (is_atapi)
		opts |= AHCI_CMD_ATAPI | AHCI_CMD_PREFETCH;

	ahci_fill_cmd_slot(pp, qc->tag, opts);
}

static void ahci_error_intr(struct ata_port *ap, u32 irq_stat)
{
	struct ahci_port_priv *pp = ap->private_data;
	struct ata_eh_info *ehi = &ap->eh_info;
	unsigned int err_mask = 0, action = 0;
	struct ata_queued_cmd *qc;
	u32 serror;

	ata_ehi_clear_desc(ehi);

	/* AHCI needs SError cleared; otherwise, it might lock up */
	serror = ahci_scr_read(ap, SCR_ERROR);
	ahci_scr_write(ap, SCR_ERROR, serror);

	/* analyze @irq_stat */
	ata_ehi_push_desc(ehi, "irq_stat 0x%08x", irq_stat);

	/* some controllers set IRQ_IF_ERR on device errors, ignore it */
	if (ap->flags & AHCI_FLAG_IGN_IRQ_IF_ERR)
		irq_stat &= ~PORT_IRQ_IF_ERR;

	if (irq_stat & PORT_IRQ_TF_ERR) {
		err_mask |= AC_ERR_DEV;
		if (ap->flags & AHCI_FLAG_IGN_SERR_INTERNAL)
			serror &= ~SERR_INTERNAL;
	}

	if (irq_stat & (PORT_IRQ_HBUS_ERR | PORT_IRQ_HBUS_DATA_ERR)) {
		err_mask |= AC_ERR_HOST_BUS;
		action |= ATA_EH_SOFTRESET;
	}

	if (irq_stat & PORT_IRQ_IF_ERR) {
		err_mask |= AC_ERR_ATA_BUS;
		action |= ATA_EH_SOFTRESET;
		ata_ehi_push_desc(ehi, ", interface fatal error");
	}

	if (irq_stat & (PORT_IRQ_CONNECT | PORT_IRQ_PHYRDY)) {
		ata_ehi_hotplugged(ehi);
		ata_ehi_push_desc(ehi, ", %s", irq_stat & PORT_IRQ_CONNECT ?
			"connection status changed" : "PHY RDY changed");
	}

	if (irq_stat & PORT_IRQ_UNK_FIS) {
		u32 *unk = (u32 *)(pp->rx_fis + RX_FIS_UNK);

		err_mask |= AC_ERR_HSM;
		action |= ATA_EH_SOFTRESET;
		ata_ehi_push_desc(ehi, ", unknown FIS %08x %08x %08x %08x",
				  unk[0], unk[1], unk[2], unk[3]);
	}

	/* okay, let's hand over to EH */
	ehi->serror |= serror;
	ehi->action |= action;

	qc = ata_qc_from_tag(ap, ap->active_tag);
	if (qc)
		qc->err_mask |= err_mask;
	else
		ehi->err_mask |= err_mask;

	if (irq_stat & PORT_IRQ_FREEZE)
		ata_port_freeze(ap);
	else
		ata_port_abort(ap);
}

static void ahci_host_intr(struct ata_port *ap)
{
	void __iomem *port_mmio = ap->ioaddr.cmd_addr;
	struct ata_eh_info *ehi = &ap->eh_info;
	struct ahci_port_priv *pp = ap->private_data;
	u32 status, qc_active;
	int rc, known_irq = 0;

	status = readl(port_mmio + PORT_IRQ_STAT);
	writel(status, port_mmio + PORT_IRQ_STAT);

	if (unlikely(status & PORT_IRQ_ERROR)) {
		ahci_error_intr(ap, status);
		return;
	}

	if (ap->sactive)
		qc_active = readl(port_mmio + PORT_SCR_ACT);
	else
		qc_active = readl(port_mmio + PORT_CMD_ISSUE);

	rc = ata_qc_complete_multiple(ap, qc_active, NULL);
	if (rc > 0)
		return;
	if (rc < 0) {
		ehi->err_mask |= AC_ERR_HSM;
		ehi->action |= ATA_EH_SOFTRESET;
		ata_port_freeze(ap);
		return;
	}

	/* hmmm... a spurious interupt */

	/* if !NCQ, ignore.  No modern ATA device has broken HSM
	 * implementation for non-NCQ commands.
	 */
	if (!ap->sactive)
		return;

	if (status & PORT_IRQ_D2H_REG_FIS) {
		if (!pp->ncq_saw_d2h)
			ata_port_printk(ap, KERN_INFO,
				"D2H reg with I during NCQ, "
				"this message won't be printed again\n");
		pp->ncq_saw_d2h = 1;
		known_irq = 1;
	}

	if (status & PORT_IRQ_DMAS_FIS) {
		if (!pp->ncq_saw_dmas)
			ata_port_printk(ap, KERN_INFO,
				"DMAS FIS during NCQ, "
				"this message won't be printed again\n");
		pp->ncq_saw_dmas = 1;
		known_irq = 1;
	}

	if (status & PORT_IRQ_SDB_FIS) {
		const __le32 *f = pp->rx_fis + RX_FIS_SDB;

		if (le32_to_cpu(f[1])) {
			/* SDB FIS containing spurious completions
			 * might be dangerous, whine and fail commands
			 * with HSM violation.  EH will turn off NCQ
			 * after several such failures.
			 */
			ata_ehi_push_desc(ehi,
				"spurious completions during NCQ "
				"issue=0x%x SAct=0x%x FIS=%08x:%08x",
				readl(port_mmio + PORT_CMD_ISSUE),
				readl(port_mmio + PORT_SCR_ACT),
				le32_to_cpu(f[0]), le32_to_cpu(f[1]));
			ehi->err_mask |= AC_ERR_HSM;
			ehi->action |= ATA_EH_SOFTRESET;
			ata_port_freeze(ap);
		} else {
			if (!pp->ncq_saw_sdb)
				ata_port_printk(ap, KERN_INFO,
					"spurious SDB FIS %08x:%08x during NCQ, "
					"this message won't be printed again\n",
					le32_to_cpu(f[0]), le32_to_cpu(f[1]));
			pp->ncq_saw_sdb = 1;
		}
		known_irq = 1;
	}

	if (!known_irq)
		ata_port_printk(ap, KERN_INFO, "spurious interrupt "
				"(irq_stat 0x%x active_tag 0x%x sactive 0x%x)\n",
				status, ap->active_tag, ap->sactive);
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
			ahci_host_intr(ap);
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

	/* clear IRQ */
	tmp = readl(port_mmio + PORT_IRQ_STAT);
	writel(tmp, port_mmio + PORT_IRQ_STAT);
	writel(1 << ap->port_no, mmio + HOST_IRQ_STAT);

	/* turn IRQ back on */
	writel(DEF_PORT_IRQ, port_mmio + PORT_IRQ_MASK);
}

static void ahci_error_handler(struct ata_port *ap)
{
	if (!(ap->pflags & ATA_PFLAG_FROZEN)) {
		/* restart engine */
		ahci_stop_engine(ap);
		ahci_start_engine(ap);
	}

	/* perform recovery */
	ata_do_eh(ap, ata_std_prereset, ahci_softreset, ahci_hardreset,
		  ahci_postreset);
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

static void ahci_post_internal_cmd(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	if (qc->flags & ATA_QCFLAG_FAILED) {
		/* make DMA engine forget about the failed command */
		ahci_stop_engine(ap);
		ahci_start_engine(ap);
	}
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
		ahci_init_port(ap);
	}

	return rc;
}

static int ahci_port_resume(struct ata_port *ap)
{
	ahci_power_up(ap);
	ahci_init_port(ap);

	return 0;
}

static int ahci_pci_device_suspend(struct pci_dev *pdev, pm_message_t mesg)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	void __iomem *mmio = host->iomap[AHCI_PCI_BAR];
	u32 ctl;

	if (mesg.event == PM_EVENT_SUSPEND) {
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
	int rc;

	pp = devm_kzalloc(dev, sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;

	rc = ata_pad_alloc(ap, dev);
	if (rc)
		return rc;

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

	ap->private_data = pp;

	/* power up port */
	ahci_power_up(ap);

	/* initialize port */
	ahci_init_port(ap);

	return 0;
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
	       	"%s%s%s%s%s%s"
	       	"%s%s%s%s%s%s%s\n"
	       	,

		cap & (1 << 31) ? "64bit " : "",
		cap & (1 << 30) ? "ncq " : "",
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

static int ahci_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	struct ata_port_info pi = ahci_port_info[ent->driver_data];
	const struct ata_port_info *ppi[] = { &pi, NULL };
	struct device *dev = &pdev->dev;
	struct ahci_host_priv *hpriv;
	struct ata_host *host;
	int i, rc;

	VPRINTK("ENTER\n");

	WARN_ON(ATA_MAX_QUEUE > AHCI_MAX_CMDS);

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	/* acquire resources */
	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	rc = pcim_iomap_regions(pdev, 1 << AHCI_PCI_BAR, DRV_NAME);
	if (rc == -EBUSY)
		pcim_pin_device(pdev);
	if (rc)
		return rc;

	if (pci_enable_msi(pdev))
		pci_intx(pdev, 1);

	hpriv = devm_kzalloc(dev, sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv)
		return -ENOMEM;

	/* save initial config */
	ahci_save_initial_config(pdev, &pi, hpriv);

	/* prepare host */
	if (!(pi.flags & AHCI_FLAG_NO_NCQ) && (hpriv->cap & HOST_CAP_NCQ))
		pi.flags |= ATA_FLAG_NCQ;

	host = ata_host_alloc_pinfo(&pdev->dev, ppi, fls(hpriv->port_map));
	if (!host)
		return -ENOMEM;
	host->iomap = pcim_iomap_table(pdev);
	host->private_data = hpriv;

	for (i = 0; i < host->n_ports; i++) {
		if (hpriv->port_map & (1 << i)) {
			struct ata_port *ap = host->ports[i];
			void __iomem *port_mmio = ahci_port_base(ap);

			ap->ioaddr.cmd_addr = port_mmio;
			ap->ioaddr.scr_addr = port_mmio + PORT_SCR;
		} else
			host->ports[i]->ops = &ata_dummy_port_ops;
	}

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
