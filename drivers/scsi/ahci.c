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
#include <linux/sched.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <linux/libata.h>
#include <asm/io.h>

#define DRV_NAME	"ahci"
#define DRV_VERSION	"1.3"


enum {
	AHCI_PCI_BAR		= 5,
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
	RX_FIS_UNK		= 0x60, /* offset of Unknown FIS data */

	board_ahci		= 0,
	board_ahci_vt8251	= 1,

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
	HOST_CAP_CLO		= (1 << 24), /* Command List Override support */
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

	PORT_CMD_ICC_ACTIVE	= (0x1 << 28), /* Put i/f in active state */
	PORT_CMD_ICC_PARTIAL	= (0x2 << 28), /* Put i/f in partial state */
	PORT_CMD_ICC_SLUMBER	= (0x6 << 28), /* Put i/f in slumber state */

	/* hpriv->flags bits */
	AHCI_FLAG_MSI		= (1 << 0),

	/* ap->flags bits */
	AHCI_FLAG_RESET_NEEDS_CLO	= (1 << 24),
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
	unsigned long		flags;
	u32			cap;	/* cache of HOST_CAP register */
	u32			port_map; /* cache of HOST_PORTS_IMPL reg */
};

struct ahci_port_priv {
	struct ahci_cmd_hdr	*cmd_slot;
	dma_addr_t		cmd_slot_dma;
	void			*cmd_tbl;
	dma_addr_t		cmd_tbl_dma;
	void			*rx_fis;
	dma_addr_t		rx_fis_dma;
};

static u32 ahci_scr_read (struct ata_port *ap, unsigned int sc_reg);
static void ahci_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val);
static int ahci_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static unsigned int ahci_qc_issue(struct ata_queued_cmd *qc);
static irqreturn_t ahci_interrupt (int irq, void *dev_instance, struct pt_regs *regs);
static int ahci_probe_reset(struct ata_port *ap, unsigned int *classes);
static void ahci_irq_clear(struct ata_port *ap);
static int ahci_port_start(struct ata_port *ap);
static void ahci_port_stop(struct ata_port *ap);
static void ahci_tf_read(struct ata_port *ap, struct ata_taskfile *tf);
static void ahci_qc_prep(struct ata_queued_cmd *qc);
static u8 ahci_check_status(struct ata_port *ap);
static void ahci_freeze(struct ata_port *ap);
static void ahci_thaw(struct ata_port *ap);
static void ahci_error_handler(struct ata_port *ap);
static void ahci_post_internal_cmd(struct ata_queued_cmd *qc);
static void ahci_remove_one (struct pci_dev *pdev);

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

	.probe_reset		= ahci_probe_reset,

	.qc_prep		= ahci_qc_prep,
	.qc_issue		= ahci_qc_issue,

	.irq_handler		= ahci_interrupt,
	.irq_clear		= ahci_irq_clear,

	.scr_read		= ahci_scr_read,
	.scr_write		= ahci_scr_write,

	.freeze			= ahci_freeze,
	.thaw			= ahci_thaw,

	.error_handler		= ahci_error_handler,
	.post_internal_cmd	= ahci_post_internal_cmd,

	.port_start		= ahci_port_start,
	.port_stop		= ahci_port_stop,
};

static const struct ata_port_info ahci_port_info[] = {
	/* board_ahci */
	{
		.sht		= &ahci_sht,
		.host_flags	= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_MMIO | ATA_FLAG_PIO_DMA,
		.pio_mask	= 0x1f, /* pio0-4 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &ahci_ops,
	},
	/* board_ahci_vt8251 */
	{
		.sht		= &ahci_sht,
		.host_flags	= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_MMIO | ATA_FLAG_PIO_DMA |
				  AHCI_FLAG_RESET_NEEDS_CLO,
		.pio_mask	= 0x1f, /* pio0-4 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &ahci_ops,
	},
};

static const struct pci_device_id ahci_pci_tbl[] = {
	{ PCI_VENDOR_ID_INTEL, 0x2652, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ICH6 */
	{ PCI_VENDOR_ID_INTEL, 0x2653, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ICH6M */
	{ PCI_VENDOR_ID_INTEL, 0x27c1, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ICH7 */
	{ PCI_VENDOR_ID_INTEL, 0x27c5, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ICH7M */
	{ PCI_VENDOR_ID_INTEL, 0x27c3, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ICH7R */
	{ PCI_VENDOR_ID_AL, 0x5288, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ULi M5288 */
	{ PCI_VENDOR_ID_INTEL, 0x2681, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ESB2 */
	{ PCI_VENDOR_ID_INTEL, 0x2682, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ESB2 */
	{ PCI_VENDOR_ID_INTEL, 0x2683, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ESB2 */
	{ PCI_VENDOR_ID_INTEL, 0x27c6, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ICH7-M DH */
	{ PCI_VENDOR_ID_INTEL, 0x2821, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ICH8 */
	{ PCI_VENDOR_ID_INTEL, 0x2822, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ICH8 */
	{ PCI_VENDOR_ID_INTEL, 0x2824, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ICH8 */
	{ PCI_VENDOR_ID_INTEL, 0x2829, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ICH8M */
	{ PCI_VENDOR_ID_INTEL, 0x282a, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ICH8M */
	{ 0x197b, 0x2360, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* JMicron JMB360 */
	{ 0x197b, 0x2363, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* JMicron JMB363 */
	{ PCI_VENDOR_ID_ATI, 0x4380, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ATI SB600 non-raid */
	{ PCI_VENDOR_ID_ATI, 0x4381, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci }, /* ATI SB600 raid */
	{ PCI_VENDOR_ID_VIA, 0x3349, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_ahci_vt8251 }, /* VIA VT8251 */
	{ }	/* terminate list */
};


static struct pci_driver ahci_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= ahci_pci_tbl,
	.probe			= ahci_init_one,
	.remove			= ahci_remove_one,
};


static inline unsigned long ahci_port_base_ul (unsigned long base, unsigned int port)
{
	return base + 0x100 + (port * 0x80);
}

static inline void __iomem *ahci_port_base (void __iomem *base, unsigned int port)
{
	return (void __iomem *) ahci_port_base_ul((unsigned long)base, port);
}

static int ahci_port_start(struct ata_port *ap)
{
	struct device *dev = ap->host_set->dev;
	struct ahci_host_priv *hpriv = ap->host_set->private_data;
	struct ahci_port_priv *pp;
	void __iomem *mmio = ap->host_set->mmio_base;
	void __iomem *port_mmio = ahci_port_base(mmio, ap->port_no);
	void *mem;
	dma_addr_t mem_dma;
	int rc;

	pp = kmalloc(sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;
	memset(pp, 0, sizeof(*pp));

	rc = ata_pad_alloc(ap, dev);
	if (rc) {
		kfree(pp);
		return rc;
	}

	mem = dma_alloc_coherent(dev, AHCI_PORT_PRIV_DMA_SZ, &mem_dma, GFP_KERNEL);
	if (!mem) {
		ata_pad_free(ap, dev);
		kfree(pp);
		return -ENOMEM;
	}
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

	if (hpriv->cap & HOST_CAP_64)
		writel((pp->cmd_slot_dma >> 16) >> 16, port_mmio + PORT_LST_ADDR_HI);
	writel(pp->cmd_slot_dma & 0xffffffff, port_mmio + PORT_LST_ADDR);
	readl(port_mmio + PORT_LST_ADDR); /* flush */

	if (hpriv->cap & HOST_CAP_64)
		writel((pp->rx_fis_dma >> 16) >> 16, port_mmio + PORT_FIS_ADDR_HI);
	writel(pp->rx_fis_dma & 0xffffffff, port_mmio + PORT_FIS_ADDR);
	readl(port_mmio + PORT_FIS_ADDR); /* flush */

	writel(PORT_CMD_ICC_ACTIVE | PORT_CMD_FIS_RX |
	       PORT_CMD_POWER_ON | PORT_CMD_SPIN_UP |
	       PORT_CMD_START, port_mmio + PORT_CMD);
	readl(port_mmio + PORT_CMD); /* flush */

	return 0;
}


static void ahci_port_stop(struct ata_port *ap)
{
	struct device *dev = ap->host_set->dev;
	struct ahci_port_priv *pp = ap->private_data;
	void __iomem *mmio = ap->host_set->mmio_base;
	void __iomem *port_mmio = ahci_port_base(mmio, ap->port_no);
	u32 tmp;

	tmp = readl(port_mmio + PORT_CMD);
	tmp &= ~(PORT_CMD_START | PORT_CMD_FIS_RX);
	writel(tmp, port_mmio + PORT_CMD);
	readl(port_mmio + PORT_CMD); /* flush */

	/* spec says 500 msecs for each PORT_CMD_{START,FIS_RX} bit, so
	 * this is slightly incorrect.
	 */
	msleep(500);

	ap->private_data = NULL;
	dma_free_coherent(dev, AHCI_PORT_PRIV_DMA_SZ,
			  pp->cmd_slot, pp->cmd_slot_dma);
	ata_pad_free(ap, dev);
	kfree(pp);
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

	return readl((void __iomem *) ap->ioaddr.scr_addr + (sc_reg * 4));
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

	writel(val, (void __iomem *) ap->ioaddr.scr_addr + (sc_reg * 4));
}

static int ahci_stop_engine(struct ata_port *ap)
{
	void __iomem *mmio = ap->host_set->mmio_base;
	void __iomem *port_mmio = ahci_port_base(mmio, ap->port_no);
	int work;
	u32 tmp;

	tmp = readl(port_mmio + PORT_CMD);
	tmp &= ~PORT_CMD_START;
	writel(tmp, port_mmio + PORT_CMD);

	/* wait for engine to stop.  TODO: this could be
	 * as long as 500 msec
	 */
	work = 1000;
	while (work-- > 0) {
		tmp = readl(port_mmio + PORT_CMD);
		if ((tmp & PORT_CMD_LIST_ON) == 0)
			return 0;
		udelay(10);
	}

	return -EIO;
}

static void ahci_start_engine(struct ata_port *ap)
{
	void __iomem *mmio = ap->host_set->mmio_base;
	void __iomem *port_mmio = ahci_port_base(mmio, ap->port_no);
	u32 tmp;

	tmp = readl(port_mmio + PORT_CMD);
	tmp |= PORT_CMD_START;
	writel(tmp, port_mmio + PORT_CMD);
	readl(port_mmio + PORT_CMD); /* flush */
}

static unsigned int ahci_dev_classify(struct ata_port *ap)
{
	void __iomem *port_mmio = (void __iomem *) ap->ioaddr.cmd_addr;
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
	void __iomem *port_mmio = (void __iomem *) ap->ioaddr.cmd_addr;
	struct ahci_host_priv *hpriv = ap->host_set->private_data;
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

static int ahci_softreset(struct ata_port *ap, unsigned int *class)
{
	struct ahci_port_priv *pp = ap->private_data;
	void __iomem *mmio = ap->host_set->mmio_base;
	void __iomem *port_mmio = ahci_port_base(mmio, ap->port_no);
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
	ahci_tf_read(ap, &tf);
	if (tf.command & (ATA_BUSY | ATA_DRQ)) {
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

	*class = ATA_DEV_NONE;
	if (ata_port_online(ap)) {
		if (ata_busy_sleep(ap, ATA_TMOUT_BOOT_QUICK, ATA_TMOUT_BOOT)) {
			rc = -EIO;
			reason = "device not ready";
			goto fail;
		}
		*class = ahci_dev_classify(ap);
	}

	DPRINTK("EXIT, class=%u\n", *class);
	return 0;

 fail_restart:
	ahci_start_engine(ap);
 fail:
	ata_port_printk(ap, KERN_ERR, "softreset failed (%s)\n", reason);
	return rc;
}

static int ahci_hardreset(struct ata_port *ap, unsigned int *class)
{
	int rc;

	DPRINTK("ENTER\n");

	ahci_stop_engine(ap);
	rc = sata_std_hardreset(ap, class);
	ahci_start_engine(ap);

	if (rc == 0 && ata_port_online(ap))
		*class = ahci_dev_classify(ap);
	if (*class == ATA_DEV_UNKNOWN)
		*class = ATA_DEV_NONE;

	DPRINTK("EXIT, rc=%d, class=%u\n", rc, *class);
	return rc;
}

static void ahci_postreset(struct ata_port *ap, unsigned int *class)
{
	void __iomem *port_mmio = (void __iomem *) ap->ioaddr.cmd_addr;
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

static int ahci_probe_reset(struct ata_port *ap, unsigned int *classes)
{
	if ((ap->flags & AHCI_FLAG_RESET_NEEDS_CLO) &&
	    (ata_busy_wait(ap, ATA_BUSY, 1000) & ATA_BUSY)) {
		/* ATA_BUSY hasn't cleared, so send a CLO */
		ahci_clo(ap);
	}

	return ata_drive_probe_reset(ap, ata_std_probeinit,
				     ahci_softreset, ahci_hardreset,
				     ahci_postreset, classes);
}

static u8 ahci_check_status(struct ata_port *ap)
{
	void __iomem *mmio = (void __iomem *) ap->ioaddr.cmd_addr;

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

	if (irq_stat & PORT_IRQ_TF_ERR)
		err_mask |= AC_ERR_DEV;

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
		err_mask |= AC_ERR_ATA_BUS;
		action |= ATA_EH_SOFTRESET;
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
	void __iomem *mmio = ap->host_set->mmio_base;
	void __iomem *port_mmio = ahci_port_base(mmio, ap->port_no);
	struct ata_eh_info *ehi = &ap->eh_info;
	u32 status, qc_active;
	int rc;

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

	/* some devices send D2H reg with I bit set during NCQ command phase */
	if (ap->sactive && status & PORT_IRQ_D2H_REG_FIS)
		return;

	/* ignore interim PIO setup fis interrupts */
	if (ata_tag_valid(ap->active_tag)) {
		struct ata_queued_cmd *qc =
			ata_qc_from_tag(ap, ap->active_tag);

		if (qc && qc->tf.protocol == ATA_PROT_PIO &&
		    (status & PORT_IRQ_PIOS_FIS))
			return;
	}

	if (ata_ratelimit())
		ata_port_printk(ap, KERN_INFO, "spurious interrupt "
				"(irq_stat 0x%x active_tag %d sactive 0x%x)\n",
				status, ap->active_tag, ap->sactive);
}

static void ahci_irq_clear(struct ata_port *ap)
{
	/* TODO */
}

static irqreturn_t ahci_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ata_host_set *host_set = dev_instance;
	struct ahci_host_priv *hpriv;
	unsigned int i, handled = 0;
	void __iomem *mmio;
	u32 irq_stat, irq_ack = 0;

	VPRINTK("ENTER\n");

	hpriv = host_set->private_data;
	mmio = host_set->mmio_base;

	/* sigh.  0xffffffff is a valid return from h/w */
	irq_stat = readl(mmio + HOST_IRQ_STAT);
	irq_stat &= hpriv->port_map;
	if (!irq_stat)
		return IRQ_NONE;

        spin_lock(&host_set->lock);

        for (i = 0; i < host_set->n_ports; i++) {
		struct ata_port *ap;

		if (!(irq_stat & (1 << i)))
			continue;

		ap = host_set->ports[i];
		if (ap) {
			ahci_host_intr(ap);
			VPRINTK("port %u\n", i);
		} else {
			VPRINTK("port %u (no irq)\n", i);
			if (ata_ratelimit())
				dev_printk(KERN_WARNING, host_set->dev,
					"interrupt on disabled port %u\n", i);
		}

		irq_ack |= (1 << i);
	}

	if (irq_ack) {
		writel(irq_ack, mmio + HOST_IRQ_STAT);
		handled = 1;
	}

	spin_unlock(&host_set->lock);

	VPRINTK("EXIT\n");

	return IRQ_RETVAL(handled);
}

static unsigned int ahci_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	void __iomem *port_mmio = (void __iomem *) ap->ioaddr.cmd_addr;

	if (qc->tf.protocol == ATA_PROT_NCQ)
		writel(1 << qc->tag, port_mmio + PORT_SCR_ACT);
	writel(1 << qc->tag, port_mmio + PORT_CMD_ISSUE);
	readl(port_mmio + PORT_CMD_ISSUE);	/* flush */

	return 0;
}

static void ahci_freeze(struct ata_port *ap)
{
	void __iomem *mmio = ap->host_set->mmio_base;
	void __iomem *port_mmio = ahci_port_base(mmio, ap->port_no);

	/* turn IRQ off */
	writel(0, port_mmio + PORT_IRQ_MASK);
}

static void ahci_thaw(struct ata_port *ap)
{
	void __iomem *mmio = ap->host_set->mmio_base;
	void __iomem *port_mmio = ahci_port_base(mmio, ap->port_no);
	u32 tmp;

	/* clear IRQ */
	tmp = readl(port_mmio + PORT_IRQ_STAT);
	writel(tmp, port_mmio + PORT_IRQ_STAT);
	writel(1 << ap->id, mmio + HOST_IRQ_STAT);

	/* turn IRQ back on */
	writel(DEF_PORT_IRQ, port_mmio + PORT_IRQ_MASK);
}

static void ahci_error_handler(struct ata_port *ap)
{
	if (!(ap->flags & ATA_FLAG_FROZEN)) {
		/* restart engine */
		ahci_stop_engine(ap);
		ahci_start_engine(ap);
	}

	/* perform recovery */
	ata_do_eh(ap, ata_std_prereset, ahci_softreset, ahci_hardreset,
		  ahci_postreset);
}

static void ahci_post_internal_cmd(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	if (qc->flags & ATA_QCFLAG_FAILED)
		qc->err_mask |= AC_ERR_OTHER;

	if (qc->err_mask) {
		/* make DMA engine forget about the failed command */
		ahci_stop_engine(ap);
		ahci_start_engine(ap);
	}
}

static void ahci_setup_port(struct ata_ioports *port, unsigned long base,
			    unsigned int port_idx)
{
	VPRINTK("ENTER, base==0x%lx, port_idx %u\n", base, port_idx);
	base = ahci_port_base_ul(base, port_idx);
	VPRINTK("base now==0x%lx\n", base);

	port->cmd_addr		= base;
	port->scr_addr		= base + PORT_SCR;

	VPRINTK("EXIT\n");
}

static int ahci_host_init(struct ata_probe_ent *probe_ent)
{
	struct ahci_host_priv *hpriv = probe_ent->private_data;
	struct pci_dev *pdev = to_pci_dev(probe_ent->dev);
	void __iomem *mmio = probe_ent->mmio_base;
	u32 tmp, cap_save;
	unsigned int i, j, using_dac;
	int rc;
	void __iomem *port_mmio;

	cap_save = readl(mmio + HOST_CAP);
	cap_save &= ( (1<<28) | (1<<17) );
	cap_save |= (1 << 27);

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
		dev_printk(KERN_ERR, &pdev->dev,
			   "controller reset failed (0x%x)\n", tmp);
		return -EIO;
	}

	writel(HOST_AHCI_EN, mmio + HOST_CTL);
	(void) readl(mmio + HOST_CTL);	/* flush */
	writel(cap_save, mmio + HOST_CAP);
	writel(0xf, mmio + HOST_PORTS_IMPL);
	(void) readl(mmio + HOST_PORTS_IMPL);	/* flush */

	if (pdev->vendor == PCI_VENDOR_ID_INTEL) {
		u16 tmp16;

		pci_read_config_word(pdev, 0x92, &tmp16);
		tmp16 |= 0xf;
		pci_write_config_word(pdev, 0x92, tmp16);
	}

	hpriv->cap = readl(mmio + HOST_CAP);
	hpriv->port_map = readl(mmio + HOST_PORTS_IMPL);
	probe_ent->n_ports = (hpriv->cap & 0x1f) + 1;

	VPRINTK("cap 0x%x  port_map 0x%x  n_ports %d\n",
		hpriv->cap, hpriv->port_map, probe_ent->n_ports);

	using_dac = hpriv->cap & HOST_CAP_64;
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

	for (i = 0; i < probe_ent->n_ports; i++) {
#if 0 /* BIOSen initialize this incorrectly */
		if (!(hpriv->port_map & (1 << i)))
			continue;
#endif

		port_mmio = ahci_port_base(mmio, i);
		VPRINTK("mmio %p  port_mmio %p\n", mmio, port_mmio);

		ahci_setup_port(&probe_ent->port[i],
				(unsigned long) mmio, i);

		/* make sure port is not active */
		tmp = readl(port_mmio + PORT_CMD);
		VPRINTK("PORT_CMD 0x%x\n", tmp);
		if (tmp & (PORT_CMD_LIST_ON | PORT_CMD_FIS_ON |
			   PORT_CMD_FIS_RX | PORT_CMD_START)) {
			tmp &= ~(PORT_CMD_LIST_ON | PORT_CMD_FIS_ON |
				 PORT_CMD_FIS_RX | PORT_CMD_START);
			writel(tmp, port_mmio + PORT_CMD);
			readl(port_mmio + PORT_CMD); /* flush */

			/* spec says 500 msecs for each bit, so
			 * this is slightly incorrect.
			 */
			msleep(500);
		}

		writel(PORT_CMD_SPIN_UP, port_mmio + PORT_CMD);

		j = 0;
		while (j < 100) {
			msleep(10);
			tmp = readl(port_mmio + PORT_SCR_STAT);
			if ((tmp & 0xf) == 0x3)
				break;
			j++;
		}

		tmp = readl(port_mmio + PORT_SCR_ERR);
		VPRINTK("PORT_SCR_ERR 0x%x\n", tmp);
		writel(tmp, port_mmio + PORT_SCR_ERR);

		/* ack any pending irq events for this port */
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

	pci_set_master(pdev);

	return 0;
}

static void ahci_print_info(struct ata_probe_ent *probe_ent)
{
	struct ahci_host_priv *hpriv = probe_ent->private_data;
	struct pci_dev *pdev = to_pci_dev(probe_ent->dev);
	void __iomem *mmio = probe_ent->mmio_base;
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
	if (cc == 0x0101)
		scc_s = "IDE";
	else if (cc == 0x0106)
		scc_s = "SATA";
	else if (cc == 0x0104)
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

static int ahci_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	struct ata_probe_ent *probe_ent = NULL;
	struct ahci_host_priv *hpriv;
	unsigned long base;
	void __iomem *mmio_base;
	unsigned int board_idx = (unsigned int) ent->driver_data;
	int have_msi, pci_dev_busy = 0;
	int rc;

	VPRINTK("ENTER\n");

	WARN_ON(ATA_MAX_QUEUE > AHCI_MAX_CMDS);

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc) {
		pci_dev_busy = 1;
		goto err_out;
	}

	if (pci_enable_msi(pdev) == 0)
		have_msi = 1;
	else {
		pci_intx(pdev, 1);
		have_msi = 0;
	}

	probe_ent = kmalloc(sizeof(*probe_ent), GFP_KERNEL);
	if (probe_ent == NULL) {
		rc = -ENOMEM;
		goto err_out_msi;
	}

	memset(probe_ent, 0, sizeof(*probe_ent));
	probe_ent->dev = pci_dev_to_dev(pdev);
	INIT_LIST_HEAD(&probe_ent->node);

	mmio_base = pci_iomap(pdev, AHCI_PCI_BAR, 0);
	if (mmio_base == NULL) {
		rc = -ENOMEM;
		goto err_out_free_ent;
	}
	base = (unsigned long) mmio_base;

	hpriv = kmalloc(sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv) {
		rc = -ENOMEM;
		goto err_out_iounmap;
	}
	memset(hpriv, 0, sizeof(*hpriv));

	probe_ent->sht		= ahci_port_info[board_idx].sht;
	probe_ent->host_flags	= ahci_port_info[board_idx].host_flags;
	probe_ent->pio_mask	= ahci_port_info[board_idx].pio_mask;
	probe_ent->udma_mask	= ahci_port_info[board_idx].udma_mask;
	probe_ent->port_ops	= ahci_port_info[board_idx].port_ops;

       	probe_ent->irq = pdev->irq;
       	probe_ent->irq_flags = SA_SHIRQ;
	probe_ent->mmio_base = mmio_base;
	probe_ent->private_data = hpriv;

	if (have_msi)
		hpriv->flags |= AHCI_FLAG_MSI;

	/* JMicron-specific fixup: make sure we're in AHCI mode */
	if (pdev->vendor == 0x197b)
		pci_write_config_byte(pdev, 0x41, 0xa1);

	/* initialize adapter */
	rc = ahci_host_init(probe_ent);
	if (rc)
		goto err_out_hpriv;

	if (hpriv->cap & HOST_CAP_NCQ)
		probe_ent->host_flags |= ATA_FLAG_NCQ;

	ahci_print_info(probe_ent);

	/* FIXME: check ata_device_add return value */
	ata_device_add(probe_ent);
	kfree(probe_ent);

	return 0;

err_out_hpriv:
	kfree(hpriv);
err_out_iounmap:
	pci_iounmap(pdev, mmio_base);
err_out_free_ent:
	kfree(probe_ent);
err_out_msi:
	if (have_msi)
		pci_disable_msi(pdev);
	else
		pci_intx(pdev, 0);
	pci_release_regions(pdev);
err_out:
	if (!pci_dev_busy)
		pci_disable_device(pdev);
	return rc;
}

static void ahci_remove_one (struct pci_dev *pdev)
{
	struct device *dev = pci_dev_to_dev(pdev);
	struct ata_host_set *host_set = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host_set->private_data;
	struct ata_port *ap;
	unsigned int i;
	int have_msi;

	for (i = 0; i < host_set->n_ports; i++) {
		ap = host_set->ports[i];

		scsi_remove_host(ap->host);
	}

	have_msi = hpriv->flags & AHCI_FLAG_MSI;
	free_irq(host_set->irq, host_set);

	for (i = 0; i < host_set->n_ports; i++) {
		ap = host_set->ports[i];

		ata_scsi_release(ap->host);
		scsi_host_put(ap->host);
	}

	kfree(hpriv);
	pci_iounmap(pdev, host_set->mmio_base);
	kfree(host_set);

	if (have_msi)
		pci_disable_msi(pdev);
	else
		pci_intx(pdev, 0);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	dev_set_drvdata(dev, NULL);
}

static int __init ahci_init(void)
{
	return pci_module_init(&ahci_pci_driver);
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
