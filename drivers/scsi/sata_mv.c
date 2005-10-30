/*
 * sata_mv.c - Marvell SATA support
 *
 * Copyright 2005: EMC Corporation, all rights reserved. 
 *
 * Please ALWAYS copy linux-ide@vger.kernel.org on emails.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
#include "scsi.h"
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <asm/io.h>

#define DRV_NAME	"sata_mv"
#define DRV_VERSION	"0.25"

enum {
	/* BAR's are enumerated in terms of pci_resource_start() terms */
	MV_PRIMARY_BAR		= 0,	/* offset 0x10: memory space */
	MV_IO_BAR		= 2,	/* offset 0x18: IO space */
	MV_MISC_BAR		= 3,	/* offset 0x1c: FLASH, NVRAM, SRAM */

	MV_MAJOR_REG_AREA_SZ	= 0x10000,	/* 64KB */
	MV_MINOR_REG_AREA_SZ	= 0x2000,	/* 8KB */

	MV_PCI_REG_BASE		= 0,
	MV_IRQ_COAL_REG_BASE	= 0x18000,	/* 6xxx part only */
	MV_SATAHC0_REG_BASE	= 0x20000,

	MV_PCI_REG_SZ		= MV_MAJOR_REG_AREA_SZ,
	MV_SATAHC_REG_SZ	= MV_MAJOR_REG_AREA_SZ,
	MV_SATAHC_ARBTR_REG_SZ	= MV_MINOR_REG_AREA_SZ,		/* arbiter */
	MV_PORT_REG_SZ		= MV_MINOR_REG_AREA_SZ,

	MV_USE_Q_DEPTH		= ATA_DEF_QUEUE,

	MV_MAX_Q_DEPTH		= 32,
	MV_MAX_Q_DEPTH_MASK	= MV_MAX_Q_DEPTH - 1,

	/* CRQB needs alignment on a 1KB boundary. Size == 1KB
	 * CRPB needs alignment on a 256B boundary. Size == 256B
	 * SG count of 176 leads to MV_PORT_PRIV_DMA_SZ == 4KB
	 * ePRD (SG) entries need alignment on a 16B boundary. Size == 16B
	 */
	MV_CRQB_Q_SZ		= (32 * MV_MAX_Q_DEPTH),
	MV_CRPB_Q_SZ		= (8 * MV_MAX_Q_DEPTH),
	MV_MAX_SG_CT		= 176,
	MV_SG_TBL_SZ		= (16 * MV_MAX_SG_CT),
	MV_PORT_PRIV_DMA_SZ	= (MV_CRQB_Q_SZ + MV_CRPB_Q_SZ + MV_SG_TBL_SZ),

	/* Our DMA boundary is determined by an ePRD being unable to handle
	 * anything larger than 64KB
	 */
	MV_DMA_BOUNDARY		= 0xffffU,

	MV_PORTS_PER_HC		= 4,
	/* == (port / MV_PORTS_PER_HC) to determine HC from 0-7 port */
	MV_PORT_HC_SHIFT	= 2,
	/* == (port % MV_PORTS_PER_HC) to determine hard port from 0-7 port */
	MV_PORT_MASK		= 3,

	/* Host Flags */
	MV_FLAG_DUAL_HC		= (1 << 30),  /* two SATA Host Controllers */
	MV_FLAG_IRQ_COALESCE	= (1 << 29),  /* IRQ coalescing capability */
	MV_FLAG_GLBL_SFT_RST	= (1 << 28),  /* Global Soft Reset support */
	MV_COMMON_FLAGS		= (ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				   ATA_FLAG_SATA_RESET | ATA_FLAG_MMIO |
				   ATA_FLAG_PIO_POLLING),
	MV_6XXX_FLAGS		= (MV_FLAG_IRQ_COALESCE | 
				   MV_FLAG_GLBL_SFT_RST),

	chip_504x		= 0,
	chip_508x		= 1,
	chip_604x		= 2,
	chip_608x		= 3,

	CRQB_FLAG_READ		= (1 << 0),
	CRQB_TAG_SHIFT		= 1,
	CRQB_CMD_ADDR_SHIFT	= 8,
	CRQB_CMD_CS		= (0x2 << 11),
	CRQB_CMD_LAST		= (1 << 15),

	CRPB_FLAG_STATUS_SHIFT	= 8,

	EPRD_FLAG_END_OF_TBL	= (1 << 31),

	/* PCI interface registers */

	PCI_COMMAND_OFS		= 0xc00,

	PCI_MAIN_CMD_STS_OFS	= 0xd30,
	STOP_PCI_MASTER		= (1 << 2),
	PCI_MASTER_EMPTY	= (1 << 3),
	GLOB_SFT_RST		= (1 << 4),

	PCI_IRQ_CAUSE_OFS	= 0x1d58,
	PCI_IRQ_MASK_OFS	= 0x1d5c,
	PCI_UNMASK_ALL_IRQS	= 0x7fffff,	/* bits 22-0 */

	HC_MAIN_IRQ_CAUSE_OFS	= 0x1d60,
	HC_MAIN_IRQ_MASK_OFS	= 0x1d64,
	PORT0_ERR		= (1 << 0),	/* shift by port # */
	PORT0_DONE		= (1 << 1),	/* shift by port # */
	HC0_IRQ_PEND		= 0x1ff,	/* bits 0-8 = HC0's ports */
	HC_SHIFT		= 9,		/* bits 9-17 = HC1's ports */
	PCI_ERR			= (1 << 18),
	TRAN_LO_DONE		= (1 << 19),	/* 6xxx: IRQ coalescing */
	TRAN_HI_DONE		= (1 << 20),	/* 6xxx: IRQ coalescing */
	PORTS_0_7_COAL_DONE	= (1 << 21),	/* 6xxx: IRQ coalescing */
	GPIO_INT		= (1 << 22),
	SELF_INT		= (1 << 23),
	TWSI_INT		= (1 << 24),
	HC_MAIN_RSVD		= (0x7f << 25),	/* bits 31-25 */
	HC_MAIN_MASKED_IRQS	= (TRAN_LO_DONE | TRAN_HI_DONE | 
				   PORTS_0_7_COAL_DONE | GPIO_INT | TWSI_INT |
				   HC_MAIN_RSVD),

	/* SATAHC registers */
	HC_CFG_OFS		= 0,

	HC_IRQ_CAUSE_OFS	= 0x14,
	CRPB_DMA_DONE		= (1 << 0),	/* shift by port # */
	HC_IRQ_COAL		= (1 << 4),	/* IRQ coalescing */
	DEV_IRQ			= (1 << 8),	/* shift by port # */

	/* Shadow block registers */
	SHD_BLK_OFS		= 0x100,
	SHD_CTL_AST_OFS		= 0x20,		/* ofs from SHD_BLK_OFS */

	/* SATA registers */
	SATA_STATUS_OFS		= 0x300,  /* ctrl, err regs follow status */
	SATA_ACTIVE_OFS		= 0x350,

	/* Port registers */
	EDMA_CFG_OFS		= 0,
	EDMA_CFG_Q_DEPTH	= 0,			/* queueing disabled */
	EDMA_CFG_NCQ		= (1 << 5),
	EDMA_CFG_NCQ_GO_ON_ERR	= (1 << 14),		/* continue on error */
	EDMA_CFG_RD_BRST_EXT	= (1 << 11),		/* read burst 512B */
	EDMA_CFG_WR_BUFF_LEN	= (1 << 13),		/* write buffer 512B */

	EDMA_ERR_IRQ_CAUSE_OFS	= 0x8,
	EDMA_ERR_IRQ_MASK_OFS	= 0xc,
	EDMA_ERR_D_PAR		= (1 << 0),
	EDMA_ERR_PRD_PAR	= (1 << 1),
	EDMA_ERR_DEV		= (1 << 2),
	EDMA_ERR_DEV_DCON	= (1 << 3),
	EDMA_ERR_DEV_CON	= (1 << 4),
	EDMA_ERR_SERR		= (1 << 5),
	EDMA_ERR_SELF_DIS	= (1 << 7),
	EDMA_ERR_BIST_ASYNC	= (1 << 8),
	EDMA_ERR_CRBQ_PAR	= (1 << 9),
	EDMA_ERR_CRPB_PAR	= (1 << 10),
	EDMA_ERR_INTRL_PAR	= (1 << 11),
	EDMA_ERR_IORDY		= (1 << 12),
	EDMA_ERR_LNK_CTRL_RX	= (0xf << 13),
	EDMA_ERR_LNK_CTRL_RX_2	= (1 << 15),
	EDMA_ERR_LNK_DATA_RX	= (0xf << 17),
	EDMA_ERR_LNK_CTRL_TX	= (0x1f << 21),
	EDMA_ERR_LNK_DATA_TX	= (0x1f << 26),
	EDMA_ERR_TRANS_PROTO	= (1 << 31),
	EDMA_ERR_FATAL		= (EDMA_ERR_D_PAR | EDMA_ERR_PRD_PAR | 
				   EDMA_ERR_DEV_DCON | EDMA_ERR_CRBQ_PAR |
				   EDMA_ERR_CRPB_PAR | EDMA_ERR_INTRL_PAR |
				   EDMA_ERR_IORDY | EDMA_ERR_LNK_CTRL_RX_2 | 
				   EDMA_ERR_LNK_DATA_RX |
				   EDMA_ERR_LNK_DATA_TX | 
				   EDMA_ERR_TRANS_PROTO),

	EDMA_REQ_Q_BASE_HI_OFS	= 0x10,
	EDMA_REQ_Q_IN_PTR_OFS	= 0x14,		/* also contains BASE_LO */
	EDMA_REQ_Q_BASE_LO_MASK	= 0xfffffc00U,

	EDMA_REQ_Q_OUT_PTR_OFS	= 0x18,
	EDMA_REQ_Q_PTR_SHIFT	= 5,

	EDMA_RSP_Q_BASE_HI_OFS	= 0x1c,
	EDMA_RSP_Q_IN_PTR_OFS	= 0x20,
	EDMA_RSP_Q_OUT_PTR_OFS	= 0x24,		/* also contains BASE_LO */
	EDMA_RSP_Q_BASE_LO_MASK	= 0xffffff00U,
	EDMA_RSP_Q_PTR_SHIFT	= 3,

	EDMA_CMD_OFS		= 0x28,
	EDMA_EN			= (1 << 0),
	EDMA_DS			= (1 << 1),
	ATA_RST			= (1 << 2),

	/* Host private flags (hp_flags) */
	MV_HP_FLAG_MSI		= (1 << 0),

	/* Port private flags (pp_flags) */
	MV_PP_FLAG_EDMA_EN	= (1 << 0),
	MV_PP_FLAG_EDMA_DS_ACT	= (1 << 1),
};

/* Command ReQuest Block: 32B */
struct mv_crqb {
	u32			sg_addr;
	u32			sg_addr_hi;
	u16			ctrl_flags;
	u16			ata_cmd[11];
};

/* Command ResPonse Block: 8B */
struct mv_crpb {
	u16			id;
	u16			flags;
	u32			tmstmp;
};

/* EDMA Physical Region Descriptor (ePRD); A.K.A. SG */
struct mv_sg {
	u32			addr;
	u32			flags_size;
	u32			addr_hi;
	u32			reserved;
};

struct mv_port_priv {
	struct mv_crqb		*crqb;
	dma_addr_t		crqb_dma;
	struct mv_crpb		*crpb;
	dma_addr_t		crpb_dma;
	struct mv_sg		*sg_tbl;
	dma_addr_t		sg_tbl_dma;

	unsigned		req_producer;		/* cp of req_in_ptr */
	unsigned		rsp_consumer;		/* cp of rsp_out_ptr */
	u32			pp_flags;
};

struct mv_host_priv {
	u32			hp_flags;
};

static void mv_irq_clear(struct ata_port *ap);
static u32 mv_scr_read(struct ata_port *ap, unsigned int sc_reg_in);
static void mv_scr_write(struct ata_port *ap, unsigned int sc_reg_in, u32 val);
static void mv_phy_reset(struct ata_port *ap);
static void mv_host_stop(struct ata_host_set *host_set);
static int mv_port_start(struct ata_port *ap);
static void mv_port_stop(struct ata_port *ap);
static void mv_qc_prep(struct ata_queued_cmd *qc);
static int mv_qc_issue(struct ata_queued_cmd *qc);
static irqreturn_t mv_interrupt(int irq, void *dev_instance,
				struct pt_regs *regs);
static void mv_eng_timeout(struct ata_port *ap);
static int mv_init_one(struct pci_dev *pdev, const struct pci_device_id *ent);

static Scsi_Host_Template mv_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.eh_strategy_handler	= ata_scsi_error,
	.can_queue		= MV_USE_Q_DEPTH,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= MV_MAX_SG_CT,
	.max_sectors		= ATA_MAX_SECTORS,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= MV_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.bios_param		= ata_std_bios_param,
	.ordered_flush		= 1,
};

static const struct ata_port_operations mv_ops = {
	.port_disable		= ata_port_disable,

	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.phy_reset		= mv_phy_reset,

	.qc_prep		= mv_qc_prep,
	.qc_issue		= mv_qc_issue,

	.eng_timeout		= mv_eng_timeout,

	.irq_handler		= mv_interrupt,
	.irq_clear		= mv_irq_clear,

	.scr_read		= mv_scr_read,
	.scr_write		= mv_scr_write,

	.port_start		= mv_port_start,
	.port_stop		= mv_port_stop,
	.host_stop		= mv_host_stop,
};

static struct ata_port_info mv_port_info[] = {
	{  /* chip_504x */
		.sht		= &mv_sht,
		.host_flags	= MV_COMMON_FLAGS,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.udma_mask	= 0,	/* 0x7f (udma0-6 disabled for now) */
		.port_ops	= &mv_ops,
	},
	{  /* chip_508x */
		.sht		= &mv_sht,
		.host_flags	= (MV_COMMON_FLAGS | MV_FLAG_DUAL_HC),
		.pio_mask	= 0x1f,	/* pio0-4 */
		.udma_mask	= 0,	/* 0x7f (udma0-6 disabled for now) */
		.port_ops	= &mv_ops,
	},
	{  /* chip_604x */
		.sht		= &mv_sht,
		.host_flags	= (MV_COMMON_FLAGS | MV_6XXX_FLAGS),
		.pio_mask	= 0x1f,	/* pio0-4 */
		.udma_mask	= 0x7f,	/* udma0-6 */
		.port_ops	= &mv_ops,
	},
	{  /* chip_608x */
		.sht		= &mv_sht,
		.host_flags	= (MV_COMMON_FLAGS | MV_6XXX_FLAGS | 
				   MV_FLAG_DUAL_HC),
		.pio_mask	= 0x1f,	/* pio0-4 */
		.udma_mask	= 0x7f,	/* udma0-6 */
		.port_ops	= &mv_ops,
	},
};

static struct pci_device_id mv_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x5040), 0, 0, chip_504x},
	{PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x5041), 0, 0, chip_504x},
	{PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x5080), 0, 0, chip_508x},
	{PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x5081), 0, 0, chip_508x},

	{PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x6040), 0, 0, chip_604x},
	{PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x6041), 0, 0, chip_604x},
	{PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x6080), 0, 0, chip_608x},
	{PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x6081), 0, 0, chip_608x},
	{}			/* terminate list */
};

static struct pci_driver mv_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= mv_pci_tbl,
	.probe			= mv_init_one,
	.remove			= ata_pci_remove_one,
};

/*
 * Functions
 */

static inline void writelfl(unsigned long data, void __iomem *addr)
{
	writel(data, addr);
	(void) readl(addr);	/* flush to avoid PCI posted write */
}

static inline void __iomem *mv_hc_base(void __iomem *base, unsigned int hc)
{
	return (base + MV_SATAHC0_REG_BASE + (hc * MV_SATAHC_REG_SZ));
}

static inline void __iomem *mv_port_base(void __iomem *base, unsigned int port)
{
	return (mv_hc_base(base, port >> MV_PORT_HC_SHIFT) +
		MV_SATAHC_ARBTR_REG_SZ + 
		((port & MV_PORT_MASK) * MV_PORT_REG_SZ));
}

static inline void __iomem *mv_ap_base(struct ata_port *ap)
{
	return mv_port_base(ap->host_set->mmio_base, ap->port_no);
}

static inline int mv_get_hc_count(unsigned long hp_flags)
{
	return ((hp_flags & MV_FLAG_DUAL_HC) ? 2 : 1);
}

static void mv_irq_clear(struct ata_port *ap)
{
}

/**
 *      mv_start_dma - Enable eDMA engine
 *      @base: port base address
 *      @pp: port private data
 *
 *      Verify the local cache of the eDMA state is accurate with an
 *      assert.
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static void mv_start_dma(void __iomem *base, struct mv_port_priv *pp)
{
	if (!(MV_PP_FLAG_EDMA_EN & pp->pp_flags)) {
		writelfl(EDMA_EN, base + EDMA_CMD_OFS);
		pp->pp_flags |= MV_PP_FLAG_EDMA_EN;
	}
	assert(EDMA_EN & readl(base + EDMA_CMD_OFS));
}

/**
 *      mv_stop_dma - Disable eDMA engine
 *      @ap: ATA channel to manipulate
 *
 *      Verify the local cache of the eDMA state is accurate with an
 *      assert.
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static void mv_stop_dma(struct ata_port *ap)
{
	void __iomem *port_mmio = mv_ap_base(ap);
	struct mv_port_priv *pp	= ap->private_data;
	u32 reg;
	int i;

	if (MV_PP_FLAG_EDMA_EN & pp->pp_flags) {
		/* Disable EDMA if active.   The disable bit auto clears.
		 */
		writelfl(EDMA_DS, port_mmio + EDMA_CMD_OFS);
		pp->pp_flags &= ~MV_PP_FLAG_EDMA_EN;
	} else {
		assert(!(EDMA_EN & readl(port_mmio + EDMA_CMD_OFS)));
  	}
	
	/* now properly wait for the eDMA to stop */
	for (i = 1000; i > 0; i--) {
		reg = readl(port_mmio + EDMA_CMD_OFS);
		if (!(EDMA_EN & reg)) {
			break;
		}
		udelay(100);
	}

	if (EDMA_EN & reg) {
		printk(KERN_ERR "ata%u: Unable to stop eDMA\n", ap->id);
		/* FIXME: Consider doing a reset here to recover */
	}
}

#ifdef ATA_DEBUG
static void mv_dump_mem(void __iomem *start, unsigned bytes)
{
	int b, w;
	for (b = 0; b < bytes; ) {
		DPRINTK("%p: ", start + b);
		for (w = 0; b < bytes && w < 4; w++) {
			printk("%08x ",readl(start + b));
			b += sizeof(u32);
		}
		printk("\n");
	}
}
#endif

static void mv_dump_pci_cfg(struct pci_dev *pdev, unsigned bytes)
{
#ifdef ATA_DEBUG
	int b, w;
	u32 dw;
	for (b = 0; b < bytes; ) {
		DPRINTK("%02x: ", b);
		for (w = 0; b < bytes && w < 4; w++) {
			(void) pci_read_config_dword(pdev,b,&dw);
			printk("%08x ",dw);
			b += sizeof(u32);
		}
		printk("\n");
	}
#endif
}
static void mv_dump_all_regs(void __iomem *mmio_base, int port,
			     struct pci_dev *pdev)
{
#ifdef ATA_DEBUG
	void __iomem *hc_base = mv_hc_base(mmio_base, 
					   port >> MV_PORT_HC_SHIFT);
	void __iomem *port_base;
	int start_port, num_ports, p, start_hc, num_hcs, hc;

	if (0 > port) {
		start_hc = start_port = 0;
		num_ports = 8;		/* shld be benign for 4 port devs */
		num_hcs = 2;
	} else {
		start_hc = port >> MV_PORT_HC_SHIFT;
		start_port = port;
		num_ports = num_hcs = 1;
	}
	DPRINTK("All registers for port(s) %u-%u:\n", start_port, 
		num_ports > 1 ? num_ports - 1 : start_port);

	if (NULL != pdev) {
		DPRINTK("PCI config space regs:\n");
		mv_dump_pci_cfg(pdev, 0x68);
	}
	DPRINTK("PCI regs:\n");
	mv_dump_mem(mmio_base+0xc00, 0x3c);
	mv_dump_mem(mmio_base+0xd00, 0x34);
	mv_dump_mem(mmio_base+0xf00, 0x4);
	mv_dump_mem(mmio_base+0x1d00, 0x6c);
	for (hc = start_hc; hc < start_hc + num_hcs; hc++) {
		hc_base = mv_hc_base(mmio_base, port >> MV_PORT_HC_SHIFT);
		DPRINTK("HC regs (HC %i):\n", hc);
		mv_dump_mem(hc_base, 0x1c);
	}
	for (p = start_port; p < start_port + num_ports; p++) {
		port_base = mv_port_base(mmio_base, p);
		DPRINTK("EDMA regs (port %i):\n",p);
		mv_dump_mem(port_base, 0x54);
		DPRINTK("SATA regs (port %i):\n",p);
		mv_dump_mem(port_base+0x300, 0x60);
	}
#endif
}

static unsigned int mv_scr_offset(unsigned int sc_reg_in)
{
	unsigned int ofs;

	switch (sc_reg_in) {
	case SCR_STATUS:
	case SCR_CONTROL:
	case SCR_ERROR:
		ofs = SATA_STATUS_OFS + (sc_reg_in * sizeof(u32));
		break;
	case SCR_ACTIVE:
		ofs = SATA_ACTIVE_OFS;   /* active is not with the others */
		break;
	default:
		ofs = 0xffffffffU;
		break;
	}
	return ofs;
}

static u32 mv_scr_read(struct ata_port *ap, unsigned int sc_reg_in)
{
	unsigned int ofs = mv_scr_offset(sc_reg_in);

	if (0xffffffffU != ofs) {
		return readl(mv_ap_base(ap) + ofs);
	} else {
		return (u32) ofs;
	}
}

static void mv_scr_write(struct ata_port *ap, unsigned int sc_reg_in, u32 val)
{
	unsigned int ofs = mv_scr_offset(sc_reg_in);

	if (0xffffffffU != ofs) {
		writelfl(val, mv_ap_base(ap) + ofs);
	}
}

/**
 *      mv_global_soft_reset - Perform the 6xxx global soft reset
 *      @mmio_base: base address of the HBA
 *
 *      This routine only applies to 6xxx parts.
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static int mv_global_soft_reset(void __iomem *mmio_base)
{
	void __iomem *reg = mmio_base + PCI_MAIN_CMD_STS_OFS;
	int i, rc = 0;
	u32 t;

	/* Following procedure defined in PCI "main command and status
	 * register" table.
	 */
	t = readl(reg);
	writel(t | STOP_PCI_MASTER, reg);

	for (i = 0; i < 1000; i++) {
		udelay(1);
		t = readl(reg);
		if (PCI_MASTER_EMPTY & t) {
			break;
		}
	}
	if (!(PCI_MASTER_EMPTY & t)) {
		printk(KERN_ERR DRV_NAME ": PCI master won't flush\n");
		rc = 1;
		goto done;
	}

	/* set reset */
	i = 5;
	do {
		writel(t | GLOB_SFT_RST, reg);
		t = readl(reg);
		udelay(1);
	} while (!(GLOB_SFT_RST & t) && (i-- > 0));

	if (!(GLOB_SFT_RST & t)) {
		printk(KERN_ERR DRV_NAME ": can't set global reset\n");
		rc = 1;
		goto done;
	}

	/* clear reset and *reenable the PCI master* (not mentioned in spec) */
	i = 5;
	do {
		writel(t & ~(GLOB_SFT_RST | STOP_PCI_MASTER), reg);
		t = readl(reg);
		udelay(1);
	} while ((GLOB_SFT_RST & t) && (i-- > 0));

	if (GLOB_SFT_RST & t) {
		printk(KERN_ERR DRV_NAME ": can't clear global reset\n");
		rc = 1;
	}
done:
	return rc;
}

/**
 *      mv_host_stop - Host specific cleanup/stop routine.
 *      @host_set: host data structure
 *
 *      Disable ints, cleanup host memory, call general purpose
 *      host_stop.
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static void mv_host_stop(struct ata_host_set *host_set)
{
	struct mv_host_priv *hpriv = host_set->private_data;
	struct pci_dev *pdev = to_pci_dev(host_set->dev);

	if (hpriv->hp_flags & MV_HP_FLAG_MSI) {
		pci_disable_msi(pdev);
	} else {
		pci_intx(pdev, 0);
	}
	kfree(hpriv);
	ata_host_stop(host_set);
}

/**
 *      mv_port_start - Port specific init/start routine.
 *      @ap: ATA channel to manipulate
 *
 *      Allocate and point to DMA memory, init port private memory,
 *      zero indices.
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static int mv_port_start(struct ata_port *ap)
{
	struct device *dev = ap->host_set->dev;
	struct mv_port_priv *pp;
	void __iomem *port_mmio = mv_ap_base(ap);
	void *mem;
	dma_addr_t mem_dma;

	pp = kmalloc(sizeof(*pp), GFP_KERNEL);
	if (!pp) {
		return -ENOMEM;
	}
	memset(pp, 0, sizeof(*pp));

	mem = dma_alloc_coherent(dev, MV_PORT_PRIV_DMA_SZ, &mem_dma, 
				 GFP_KERNEL);
	if (!mem) {
		kfree(pp);
		return -ENOMEM;
	}
	memset(mem, 0, MV_PORT_PRIV_DMA_SZ);

	/* First item in chunk of DMA memory: 
	 * 32-slot command request table (CRQB), 32 bytes each in size
	 */
	pp->crqb = mem;
	pp->crqb_dma = mem_dma;
	mem += MV_CRQB_Q_SZ;
	mem_dma += MV_CRQB_Q_SZ;

	/* Second item: 
	 * 32-slot command response table (CRPB), 8 bytes each in size
	 */
	pp->crpb = mem;
	pp->crpb_dma = mem_dma;
	mem += MV_CRPB_Q_SZ;
	mem_dma += MV_CRPB_Q_SZ;

	/* Third item:
	 * Table of scatter-gather descriptors (ePRD), 16 bytes each
	 */
	pp->sg_tbl = mem;
	pp->sg_tbl_dma = mem_dma;

	writelfl(EDMA_CFG_Q_DEPTH | EDMA_CFG_RD_BRST_EXT | 
		 EDMA_CFG_WR_BUFF_LEN, port_mmio + EDMA_CFG_OFS);

	writel((pp->crqb_dma >> 16) >> 16, port_mmio + EDMA_REQ_Q_BASE_HI_OFS);
	writelfl(pp->crqb_dma & EDMA_REQ_Q_BASE_LO_MASK, 
		 port_mmio + EDMA_REQ_Q_IN_PTR_OFS);

	writelfl(0, port_mmio + EDMA_REQ_Q_OUT_PTR_OFS);
	writelfl(0, port_mmio + EDMA_RSP_Q_IN_PTR_OFS);

	writel((pp->crpb_dma >> 16) >> 16, port_mmio + EDMA_RSP_Q_BASE_HI_OFS);
	writelfl(pp->crpb_dma & EDMA_RSP_Q_BASE_LO_MASK, 
		 port_mmio + EDMA_RSP_Q_OUT_PTR_OFS);

	pp->req_producer = pp->rsp_consumer = 0;

	/* Don't turn on EDMA here...do it before DMA commands only.  Else
	 * we'll be unable to send non-data, PIO, etc due to restricted access
	 * to shadow regs.
	 */
	ap->private_data = pp;
	return 0;
}

/**
 *      mv_port_stop - Port specific cleanup/stop routine.
 *      @ap: ATA channel to manipulate
 *
 *      Stop DMA, cleanup port memory.
 *
 *      LOCKING:
 *      This routine uses the host_set lock to protect the DMA stop.
 */
static void mv_port_stop(struct ata_port *ap)
{
	struct device *dev = ap->host_set->dev;
	struct mv_port_priv *pp = ap->private_data;
	unsigned long flags;

	spin_lock_irqsave(&ap->host_set->lock, flags);
	mv_stop_dma(ap);
	spin_unlock_irqrestore(&ap->host_set->lock, flags);

	ap->private_data = NULL;
	dma_free_coherent(dev, MV_PORT_PRIV_DMA_SZ, pp->crpb, pp->crpb_dma);
	kfree(pp);
}

/**
 *      mv_fill_sg - Fill out the Marvell ePRD (scatter gather) entries
 *      @qc: queued command whose SG list to source from
 *
 *      Populate the SG list and mark the last entry.
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static void mv_fill_sg(struct ata_queued_cmd *qc)
{
	struct mv_port_priv *pp = qc->ap->private_data;
	unsigned int i;

	for (i = 0; i < qc->n_elem; i++) {
		u32 sg_len;
		dma_addr_t addr;

		addr = sg_dma_address(&qc->sg[i]);
		sg_len = sg_dma_len(&qc->sg[i]);

		pp->sg_tbl[i].addr = cpu_to_le32(addr & 0xffffffff);
		pp->sg_tbl[i].addr_hi = cpu_to_le32((addr >> 16) >> 16);
		assert(0 == (sg_len & ~MV_DMA_BOUNDARY));
		pp->sg_tbl[i].flags_size = cpu_to_le32(sg_len);
	}
	if (0 < qc->n_elem) {
		pp->sg_tbl[qc->n_elem - 1].flags_size |= 
			cpu_to_le32(EPRD_FLAG_END_OF_TBL);
	}
}

static inline unsigned mv_inc_q_index(unsigned *index)
{
	*index = (*index + 1) & MV_MAX_Q_DEPTH_MASK;
	return *index;
}

static inline void mv_crqb_pack_cmd(u16 *cmdw, u8 data, u8 addr, unsigned last)
{
	*cmdw = data | (addr << CRQB_CMD_ADDR_SHIFT) | CRQB_CMD_CS |
		(last ? CRQB_CMD_LAST : 0);
}

/**
 *      mv_qc_prep - Host specific command preparation.
 *      @qc: queued command to prepare
 *
 *      This routine simply redirects to the general purpose routine
 *      if command is not DMA.  Else, it handles prep of the CRQB
 *      (command request block), does some sanity checking, and calls
 *      the SG load routine.
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static void mv_qc_prep(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct mv_port_priv *pp = ap->private_data;
	u16 *cw;
	struct ata_taskfile *tf;
	u16 flags = 0;

 	if (ATA_PROT_DMA != qc->tf.protocol) {
		return;
	}

	/* the req producer index should be the same as we remember it */
	assert(((readl(mv_ap_base(qc->ap) + EDMA_REQ_Q_IN_PTR_OFS) >> 
		 EDMA_REQ_Q_PTR_SHIFT) & MV_MAX_Q_DEPTH_MASK) ==
	       pp->req_producer);

	/* Fill in command request block
	 */
	if (!(qc->tf.flags & ATA_TFLAG_WRITE)) {
		flags |= CRQB_FLAG_READ;
	}
	assert(MV_MAX_Q_DEPTH > qc->tag);
	flags |= qc->tag << CRQB_TAG_SHIFT;

	pp->crqb[pp->req_producer].sg_addr = 
		cpu_to_le32(pp->sg_tbl_dma & 0xffffffff);
	pp->crqb[pp->req_producer].sg_addr_hi = 
		cpu_to_le32((pp->sg_tbl_dma >> 16) >> 16);
	pp->crqb[pp->req_producer].ctrl_flags = cpu_to_le16(flags);

	cw = &pp->crqb[pp->req_producer].ata_cmd[0];
	tf = &qc->tf;

	/* Sadly, the CRQB cannot accomodate all registers--there are
	 * only 11 bytes...so we must pick and choose required
	 * registers based on the command.  So, we drop feature and
	 * hob_feature for [RW] DMA commands, but they are needed for
	 * NCQ.  NCQ will drop hob_nsect.
	 */
	switch (tf->command) {
	case ATA_CMD_READ:
	case ATA_CMD_READ_EXT:
	case ATA_CMD_WRITE:
	case ATA_CMD_WRITE_EXT:
		mv_crqb_pack_cmd(cw++, tf->hob_nsect, ATA_REG_NSECT, 0);
		break;
#ifdef LIBATA_NCQ		/* FIXME: remove this line when NCQ added */
	case ATA_CMD_FPDMA_READ:
	case ATA_CMD_FPDMA_WRITE:
		mv_crqb_pack_cmd(cw++, tf->hob_feature, ATA_REG_FEATURE, 0); 
		mv_crqb_pack_cmd(cw++, tf->feature, ATA_REG_FEATURE, 0);
		break;
#endif				/* FIXME: remove this line when NCQ added */
	default:
		/* The only other commands EDMA supports in non-queued and
		 * non-NCQ mode are: [RW] STREAM DMA and W DMA FUA EXT, none
		 * of which are defined/used by Linux.  If we get here, this
		 * driver needs work.
		 *
		 * FIXME: modify libata to give qc_prep a return value and
		 * return error here.
		 */
		BUG_ON(tf->command);
		break;
	}
	mv_crqb_pack_cmd(cw++, tf->nsect, ATA_REG_NSECT, 0);
	mv_crqb_pack_cmd(cw++, tf->hob_lbal, ATA_REG_LBAL, 0);
	mv_crqb_pack_cmd(cw++, tf->lbal, ATA_REG_LBAL, 0);
	mv_crqb_pack_cmd(cw++, tf->hob_lbam, ATA_REG_LBAM, 0);
	mv_crqb_pack_cmd(cw++, tf->lbam, ATA_REG_LBAM, 0);
	mv_crqb_pack_cmd(cw++, tf->hob_lbah, ATA_REG_LBAH, 0);
	mv_crqb_pack_cmd(cw++, tf->lbah, ATA_REG_LBAH, 0);
	mv_crqb_pack_cmd(cw++, tf->device, ATA_REG_DEVICE, 0);
	mv_crqb_pack_cmd(cw++, tf->command, ATA_REG_CMD, 1);	/* last */

	if (!(qc->flags & ATA_QCFLAG_DMAMAP)) {
		return;
	}
	mv_fill_sg(qc);
}

/**
 *      mv_qc_issue - Initiate a command to the host
 *      @qc: queued command to start
 *
 *      This routine simply redirects to the general purpose routine
 *      if command is not DMA.  Else, it sanity checks our local
 *      caches of the request producer/consumer indices then enables
 *      DMA and bumps the request producer index.
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static int mv_qc_issue(struct ata_queued_cmd *qc)
{
	void __iomem *port_mmio = mv_ap_base(qc->ap);
	struct mv_port_priv *pp = qc->ap->private_data;
	u32 in_ptr;

	if (ATA_PROT_DMA != qc->tf.protocol) {
		/* We're about to send a non-EDMA capable command to the
		 * port.  Turn off EDMA so there won't be problems accessing
		 * shadow block, etc registers.
		 */
		mv_stop_dma(qc->ap);
		return ata_qc_issue_prot(qc);
	}

	in_ptr = readl(port_mmio + EDMA_REQ_Q_IN_PTR_OFS);

	/* the req producer index should be the same as we remember it */
	assert(((in_ptr >> EDMA_REQ_Q_PTR_SHIFT) & MV_MAX_Q_DEPTH_MASK) ==
	       pp->req_producer);
	/* until we do queuing, the queue should be empty at this point */
	assert(((in_ptr >> EDMA_REQ_Q_PTR_SHIFT) & MV_MAX_Q_DEPTH_MASK) ==
	       ((readl(port_mmio + EDMA_REQ_Q_OUT_PTR_OFS) >> 
		 EDMA_REQ_Q_PTR_SHIFT) & MV_MAX_Q_DEPTH_MASK));

	mv_inc_q_index(&pp->req_producer);	/* now incr producer index */

	mv_start_dma(port_mmio, pp);

	/* and write the request in pointer to kick the EDMA to life */
	in_ptr &= EDMA_REQ_Q_BASE_LO_MASK;
	in_ptr |= pp->req_producer << EDMA_REQ_Q_PTR_SHIFT;
	writelfl(in_ptr, port_mmio + EDMA_REQ_Q_IN_PTR_OFS);

	return 0;
}

/**
 *      mv_get_crpb_status - get status from most recently completed cmd
 *      @ap: ATA channel to manipulate
 *
 *      This routine is for use when the port is in DMA mode, when it
 *      will be using the CRPB (command response block) method of
 *      returning command completion information.  We assert indices
 *      are good, grab status, and bump the response consumer index to
 *      prove that we're up to date.
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static u8 mv_get_crpb_status(struct ata_port *ap)
{
	void __iomem *port_mmio = mv_ap_base(ap);
	struct mv_port_priv *pp = ap->private_data;
	u32 out_ptr;

	out_ptr = readl(port_mmio + EDMA_RSP_Q_OUT_PTR_OFS);

	/* the response consumer index should be the same as we remember it */
	assert(((out_ptr >> EDMA_RSP_Q_PTR_SHIFT) & MV_MAX_Q_DEPTH_MASK) == 
	       pp->rsp_consumer);

	/* increment our consumer index... */
	pp->rsp_consumer = mv_inc_q_index(&pp->rsp_consumer);
	
	/* and, until we do NCQ, there should only be 1 CRPB waiting */
	assert(((readl(port_mmio + EDMA_RSP_Q_IN_PTR_OFS) >> 
		 EDMA_RSP_Q_PTR_SHIFT) & MV_MAX_Q_DEPTH_MASK) == 
	       pp->rsp_consumer);

	/* write out our inc'd consumer index so EDMA knows we're caught up */
	out_ptr &= EDMA_RSP_Q_BASE_LO_MASK;
	out_ptr |= pp->rsp_consumer << EDMA_RSP_Q_PTR_SHIFT;
	writelfl(out_ptr, port_mmio + EDMA_RSP_Q_OUT_PTR_OFS);

	/* Return ATA status register for completed CRPB */
	return (pp->crpb[pp->rsp_consumer].flags >> CRPB_FLAG_STATUS_SHIFT);
}

/**
 *      mv_err_intr - Handle error interrupts on the port
 *      @ap: ATA channel to manipulate
 *
 *      In most cases, just clear the interrupt and move on.  However,
 *      some cases require an eDMA reset, which is done right before
 *      the COMRESET in mv_phy_reset().  The SERR case requires a
 *      clear of pending errors in the SATA SERROR register.  Finally,
 *      if the port disabled DMA, update our cached copy to match.
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static void mv_err_intr(struct ata_port *ap)
{
	void __iomem *port_mmio = mv_ap_base(ap);
	u32 edma_err_cause, serr = 0;

	edma_err_cause = readl(port_mmio + EDMA_ERR_IRQ_CAUSE_OFS);

	if (EDMA_ERR_SERR & edma_err_cause) {
		serr = scr_read(ap, SCR_ERROR);
		scr_write_flush(ap, SCR_ERROR, serr);
	}
	if (EDMA_ERR_SELF_DIS & edma_err_cause) {
		struct mv_port_priv *pp	= ap->private_data;
		pp->pp_flags &= ~MV_PP_FLAG_EDMA_EN;
	}
	DPRINTK(KERN_ERR "ata%u: port error; EDMA err cause: 0x%08x "
		"SERR: 0x%08x\n", ap->id, edma_err_cause, serr);

	/* Clear EDMA now that SERR cleanup done */
	writelfl(0, port_mmio + EDMA_ERR_IRQ_CAUSE_OFS);

	/* check for fatal here and recover if needed */
	if (EDMA_ERR_FATAL & edma_err_cause) {
		mv_phy_reset(ap);
	}
}

/**
 *      mv_host_intr - Handle all interrupts on the given host controller
 *      @host_set: host specific structure
 *      @relevant: port error bits relevant to this host controller
 *      @hc: which host controller we're to look at
 *
 *      Read then write clear the HC interrupt status then walk each
 *      port connected to the HC and see if it needs servicing.  Port
 *      success ints are reported in the HC interrupt status reg, the
 *      port error ints are reported in the higher level main
 *      interrupt status register and thus are passed in via the
 *      'relevant' argument.
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static void mv_host_intr(struct ata_host_set *host_set, u32 relevant,
			 unsigned int hc)
{
	void __iomem *mmio = host_set->mmio_base;
	void __iomem *hc_mmio = mv_hc_base(mmio, hc);
	struct ata_port *ap;
	struct ata_queued_cmd *qc;
	u32 hc_irq_cause;
	int shift, port, port0, hard_port, handled;
	u8 ata_status = 0;

	if (hc == 0) {
		port0 = 0;
	} else {
		port0 = MV_PORTS_PER_HC;
	}

	/* we'll need the HC success int register in most cases */
	hc_irq_cause = readl(hc_mmio + HC_IRQ_CAUSE_OFS);
	if (hc_irq_cause) {
		writelfl(~hc_irq_cause, hc_mmio + HC_IRQ_CAUSE_OFS);
	}

	VPRINTK("ENTER, hc%u relevant=0x%08x HC IRQ cause=0x%08x\n",
		hc,relevant,hc_irq_cause);

	for (port = port0; port < port0 + MV_PORTS_PER_HC; port++) {
		ap = host_set->ports[port];
		hard_port = port & MV_PORT_MASK;	/* range 0-3 */
		handled = 0;	/* ensure ata_status is set if handled++ */

		if ((CRPB_DMA_DONE << hard_port) & hc_irq_cause) {
			/* new CRPB on the queue; just one at a time until NCQ
			 */
			ata_status = mv_get_crpb_status(ap);
			handled++;
		} else if ((DEV_IRQ << hard_port) & hc_irq_cause) {
			/* received ATA IRQ; read the status reg to clear INTRQ
			 */
			ata_status = readb((void __iomem *)
					   ap->ioaddr.status_addr);
			handled++;
		}

		shift = port << 1;		/* (port * 2) */
		if (port >= MV_PORTS_PER_HC) {
			shift++;	/* skip bit 8 in the HC Main IRQ reg */
		}
		if ((PORT0_ERR << shift) & relevant) {
			mv_err_intr(ap);
			/* OR in ATA_ERR to ensure libata knows we took one */
			ata_status = readb((void __iomem *)
					   ap->ioaddr.status_addr) | ATA_ERR;
			handled++;
		}
		
		if (handled && ap) {
			qc = ata_qc_from_tag(ap, ap->active_tag);
			if (NULL != qc) {
				VPRINTK("port %u IRQ found for qc, "
					"ata_status 0x%x\n", port,ata_status);
				/* mark qc status appropriately */
				ata_qc_complete(qc, ata_status);
			}
		}
	}
	VPRINTK("EXIT\n");
}

/**
 *      mv_interrupt - 
 *      @irq: unused
 *      @dev_instance: private data; in this case the host structure
 *      @regs: unused
 *
 *      Read the read only register to determine if any host
 *      controllers have pending interrupts.  If so, call lower level
 *      routine to handle.  Also check for PCI errors which are only
 *      reported here.
 *
 *      LOCKING: 
 *      This routine holds the host_set lock while processing pending
 *      interrupts.
 */
static irqreturn_t mv_interrupt(int irq, void *dev_instance,
				struct pt_regs *regs)
{
	struct ata_host_set *host_set = dev_instance;
	unsigned int hc, handled = 0, n_hcs;
	void __iomem *mmio = host_set->mmio_base;
	u32 irq_stat;

	irq_stat = readl(mmio + HC_MAIN_IRQ_CAUSE_OFS);

	/* check the cases where we either have nothing pending or have read
	 * a bogus register value which can indicate HW removal or PCI fault
	 */
	if (!irq_stat || (0xffffffffU == irq_stat)) {
		return IRQ_NONE;
	}

	n_hcs = mv_get_hc_count(host_set->ports[0]->flags);
	spin_lock(&host_set->lock);

	for (hc = 0; hc < n_hcs; hc++) {
		u32 relevant = irq_stat & (HC0_IRQ_PEND << (hc * HC_SHIFT));
		if (relevant) {
			mv_host_intr(host_set, relevant, hc);
			handled++;
		}
	}
	if (PCI_ERR & irq_stat) {
		printk(KERN_ERR DRV_NAME ": PCI ERROR; PCI IRQ cause=0x%08x\n",
		       readl(mmio + PCI_IRQ_CAUSE_OFS));

		DPRINTK("All regs @ PCI error\n");
		mv_dump_all_regs(mmio, -1, to_pci_dev(host_set->dev));

		writelfl(0, mmio + PCI_IRQ_CAUSE_OFS);
		handled++;
	}
	spin_unlock(&host_set->lock);

	return IRQ_RETVAL(handled);
}

/**
 *      mv_phy_reset - Perform eDMA reset followed by COMRESET
 *      @ap: ATA channel to manipulate
 *
 *      Part of this is taken from __sata_phy_reset and modified to
 *      not sleep since this routine gets called from interrupt level.
 *
 *      LOCKING:
 *      Inherited from caller.  This is coded to safe to call at
 *      interrupt level, i.e. it does not sleep.
 */
static void mv_phy_reset(struct ata_port *ap)
{
	void __iomem *port_mmio = mv_ap_base(ap);
	struct ata_taskfile tf;
	struct ata_device *dev = &ap->device[0];
	unsigned long timeout;

	VPRINTK("ENTER, port %u, mmio 0x%p\n", ap->port_no, port_mmio);

	mv_stop_dma(ap);

	writelfl(ATA_RST, port_mmio + EDMA_CMD_OFS);
	udelay(25);		/* allow reset propagation */

	/* Spec never mentions clearing the bit.  Marvell's driver does
	 * clear the bit, however.
	 */
	writelfl(0, port_mmio + EDMA_CMD_OFS);

	VPRINTK("S-regs after ATA_RST: SStat 0x%08x SErr 0x%08x "
		"SCtrl 0x%08x\n", mv_scr_read(ap, SCR_STATUS),
		mv_scr_read(ap, SCR_ERROR), mv_scr_read(ap, SCR_CONTROL));

	/* proceed to init communications via the scr_control reg */
	scr_write_flush(ap, SCR_CONTROL, 0x301);
	mdelay(1);
	scr_write_flush(ap, SCR_CONTROL, 0x300);
	timeout = jiffies + (HZ * 1);
	do {
		mdelay(10);
		if ((scr_read(ap, SCR_STATUS) & 0xf) != 1)
			break;
	} while (time_before(jiffies, timeout));

	VPRINTK("S-regs after PHY wake: SStat 0x%08x SErr 0x%08x "
		"SCtrl 0x%08x\n", mv_scr_read(ap, SCR_STATUS),
		mv_scr_read(ap, SCR_ERROR), mv_scr_read(ap, SCR_CONTROL));

	if (sata_dev_present(ap)) {
		ata_port_probe(ap);
	} else {
		printk(KERN_INFO "ata%u: no device found (phy stat %08x)\n",
		       ap->id, scr_read(ap, SCR_STATUS));
		ata_port_disable(ap);
		return;
	}
	ap->cbl = ATA_CBL_SATA;

	tf.lbah = readb((void __iomem *) ap->ioaddr.lbah_addr);
	tf.lbam = readb((void __iomem *) ap->ioaddr.lbam_addr);
	tf.lbal = readb((void __iomem *) ap->ioaddr.lbal_addr);
	tf.nsect = readb((void __iomem *) ap->ioaddr.nsect_addr);

	dev->class = ata_dev_classify(&tf);
	if (!ata_dev_present(dev)) {
		VPRINTK("Port disabled post-sig: No device present.\n");
		ata_port_disable(ap);
	}
	VPRINTK("EXIT\n");
}

/**
 *      mv_eng_timeout - Routine called by libata when SCSI times out I/O
 *      @ap: ATA channel to manipulate
 *
 *      Intent is to clear all pending error conditions, reset the
 *      chip/bus, fail the command, and move on.
 *
 *      LOCKING:
 *      This routine holds the host_set lock while failing the command.
 */
static void mv_eng_timeout(struct ata_port *ap)
{
	struct ata_queued_cmd *qc;
	unsigned long flags;

	printk(KERN_ERR "ata%u: Entering mv_eng_timeout\n",ap->id);
	DPRINTK("All regs @ start of eng_timeout\n");
	mv_dump_all_regs(ap->host_set->mmio_base, ap->port_no, 
			 to_pci_dev(ap->host_set->dev));

	qc = ata_qc_from_tag(ap, ap->active_tag);
        printk(KERN_ERR "mmio_base %p ap %p qc %p scsi_cmnd %p &cmnd %p\n",
	       ap->host_set->mmio_base, ap, qc, qc->scsicmd, 
	       &qc->scsicmd->cmnd);

	mv_err_intr(ap);
	mv_phy_reset(ap);

	if (!qc) {
		printk(KERN_ERR "ata%u: BUG: timeout without command\n",
		       ap->id);
	} else {
		/* hack alert!  We cannot use the supplied completion
	 	 * function from inside the ->eh_strategy_handler() thread.
	 	 * libata is the only user of ->eh_strategy_handler() in
	 	 * any kernel, so the default scsi_done() assumes it is
	 	 * not being called from the SCSI EH.
	 	 */
		spin_lock_irqsave(&ap->host_set->lock, flags);
		qc->scsidone = scsi_finish_command;
		ata_qc_complete(qc, ATA_ERR);
		spin_unlock_irqrestore(&ap->host_set->lock, flags);
	}
}

/**
 *      mv_port_init - Perform some early initialization on a single port.
 *      @port: libata data structure storing shadow register addresses
 *      @port_mmio: base address of the port
 *
 *      Initialize shadow register mmio addresses, clear outstanding
 *      interrupts on the port, and unmask interrupts for the future
 *      start of the port.
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static void mv_port_init(struct ata_ioports *port,  void __iomem *port_mmio)
{
	unsigned long shd_base = (unsigned long) port_mmio + SHD_BLK_OFS;
	unsigned serr_ofs;

	/* PIO related setup 
	 */
	port->data_addr = shd_base + (sizeof(u32) * ATA_REG_DATA);
	port->error_addr = 
		port->feature_addr = shd_base + (sizeof(u32) * ATA_REG_ERR);
	port->nsect_addr = shd_base + (sizeof(u32) * ATA_REG_NSECT);
	port->lbal_addr = shd_base + (sizeof(u32) * ATA_REG_LBAL);
	port->lbam_addr = shd_base + (sizeof(u32) * ATA_REG_LBAM);
	port->lbah_addr = shd_base + (sizeof(u32) * ATA_REG_LBAH);
	port->device_addr = shd_base + (sizeof(u32) * ATA_REG_DEVICE);
	port->status_addr = 
		port->command_addr = shd_base + (sizeof(u32) * ATA_REG_STATUS);
	/* special case: control/altstatus doesn't have ATA_REG_ address */
	port->altstatus_addr = port->ctl_addr = shd_base + SHD_CTL_AST_OFS;

	/* unused: */
	port->cmd_addr = port->bmdma_addr = port->scr_addr = 0;

	/* Clear any currently outstanding port interrupt conditions */
	serr_ofs = mv_scr_offset(SCR_ERROR);
	writelfl(readl(port_mmio + serr_ofs), port_mmio + serr_ofs);
	writelfl(0, port_mmio + EDMA_ERR_IRQ_CAUSE_OFS);

	/* unmask all EDMA error interrupts */
	writelfl(~0, port_mmio + EDMA_ERR_IRQ_MASK_OFS);

	VPRINTK("EDMA cfg=0x%08x EDMA IRQ err cause/mask=0x%08x/0x%08x\n", 
		readl(port_mmio + EDMA_CFG_OFS),
		readl(port_mmio + EDMA_ERR_IRQ_CAUSE_OFS),
		readl(port_mmio + EDMA_ERR_IRQ_MASK_OFS));
}

/**
 *      mv_host_init - Perform some early initialization of the host.
 *      @probe_ent: early data struct representing the host
 *
 *      If possible, do an early global reset of the host.  Then do
 *      our port init and clear/unmask all/relevant host interrupts.
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static int mv_host_init(struct ata_probe_ent *probe_ent)
{
	int rc = 0, n_hc, port, hc;
	void __iomem *mmio = probe_ent->mmio_base;
	void __iomem *port_mmio;

	if ((MV_FLAG_GLBL_SFT_RST & probe_ent->host_flags) && 
	    mv_global_soft_reset(probe_ent->mmio_base)) {
		rc = 1;
		goto done;
	}

	n_hc = mv_get_hc_count(probe_ent->host_flags);
	probe_ent->n_ports = MV_PORTS_PER_HC * n_hc;

	for (port = 0; port < probe_ent->n_ports; port++) {
		port_mmio = mv_port_base(mmio, port);
		mv_port_init(&probe_ent->port[port], port_mmio);
	}

	for (hc = 0; hc < n_hc; hc++) {
		void __iomem *hc_mmio = mv_hc_base(mmio, hc);

		VPRINTK("HC%i: HC config=0x%08x HC IRQ cause "
			"(before clear)=0x%08x\n", hc,
			readl(hc_mmio + HC_CFG_OFS),
			readl(hc_mmio + HC_IRQ_CAUSE_OFS));

		/* Clear any currently outstanding hc interrupt conditions */
		writelfl(0, hc_mmio + HC_IRQ_CAUSE_OFS);
	}

	/* Clear any currently outstanding host interrupt conditions */
	writelfl(0, mmio + PCI_IRQ_CAUSE_OFS);

	/* and unmask interrupt generation for host regs */
	writelfl(PCI_UNMASK_ALL_IRQS, mmio + PCI_IRQ_MASK_OFS);
	writelfl(~HC_MAIN_MASKED_IRQS, mmio + HC_MAIN_IRQ_MASK_OFS);

	VPRINTK("HC MAIN IRQ cause/mask=0x%08x/0x%08x "
		"PCI int cause/mask=0x%08x/0x%08x\n", 
		readl(mmio + HC_MAIN_IRQ_CAUSE_OFS),
		readl(mmio + HC_MAIN_IRQ_MASK_OFS),
		readl(mmio + PCI_IRQ_CAUSE_OFS),
		readl(mmio + PCI_IRQ_MASK_OFS));
done:
	return rc;
}

/**
 *      mv_print_info - Dump key info to kernel log for perusal.
 *      @probe_ent: early data struct representing the host
 *
 *      FIXME: complete this.
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static void mv_print_info(struct ata_probe_ent *probe_ent)
{
	struct pci_dev *pdev = to_pci_dev(probe_ent->dev);
	struct mv_host_priv *hpriv = probe_ent->private_data;
	u8 rev_id, scc;
	const char *scc_s;

	/* Use this to determine the HW stepping of the chip so we know
	 * what errata to workaround
	 */
	pci_read_config_byte(pdev, PCI_REVISION_ID, &rev_id);

	pci_read_config_byte(pdev, PCI_CLASS_DEVICE, &scc);
	if (scc == 0)
		scc_s = "SCSI";
	else if (scc == 0x01)
		scc_s = "RAID";
	else
		scc_s = "unknown";

	printk(KERN_INFO DRV_NAME 
	       "(%s) %u slots %u ports %s mode IRQ via %s\n",
	       pci_name(pdev), (unsigned)MV_MAX_Q_DEPTH, probe_ent->n_ports, 
	       scc_s, (MV_HP_FLAG_MSI & hpriv->hp_flags) ? "MSI" : "INTx");
}

/**
 *      mv_init_one - handle a positive probe of a Marvell host
 *      @pdev: PCI device found
 *      @ent: PCI device ID entry for the matched host
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static int mv_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version = 0;
	struct ata_probe_ent *probe_ent = NULL;
	struct mv_host_priv *hpriv;
	unsigned int board_idx = (unsigned int)ent->driver_data;
	void __iomem *mmio_base;
	int pci_dev_busy = 0, rc;

	if (!printed_version++) {
		printk(KERN_INFO DRV_NAME " version " DRV_VERSION "\n");
	}

	rc = pci_enable_device(pdev);
	if (rc) {
		return rc;
	}

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc) {
		pci_dev_busy = 1;
		goto err_out;
	}

	probe_ent = kmalloc(sizeof(*probe_ent), GFP_KERNEL);
	if (probe_ent == NULL) {
		rc = -ENOMEM;
		goto err_out_regions;
	}

	memset(probe_ent, 0, sizeof(*probe_ent));
	probe_ent->dev = pci_dev_to_dev(pdev);
	INIT_LIST_HEAD(&probe_ent->node);

	mmio_base = pci_iomap(pdev, MV_PRIMARY_BAR, 0);
	if (mmio_base == NULL) {
		rc = -ENOMEM;
		goto err_out_free_ent;
	}

	hpriv = kmalloc(sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv) {
		rc = -ENOMEM;
		goto err_out_iounmap;
	}
	memset(hpriv, 0, sizeof(*hpriv));

	probe_ent->sht = mv_port_info[board_idx].sht;
	probe_ent->host_flags = mv_port_info[board_idx].host_flags;
	probe_ent->pio_mask = mv_port_info[board_idx].pio_mask;
	probe_ent->udma_mask = mv_port_info[board_idx].udma_mask;
	probe_ent->port_ops = mv_port_info[board_idx].port_ops;

	probe_ent->irq = pdev->irq;
	probe_ent->irq_flags = SA_SHIRQ;
	probe_ent->mmio_base = mmio_base;
	probe_ent->private_data = hpriv;

	/* initialize adapter */
	rc = mv_host_init(probe_ent);
	if (rc) {
		goto err_out_hpriv;
	}

	/* Enable interrupts */
	if (pci_enable_msi(pdev) == 0) {
		hpriv->hp_flags |= MV_HP_FLAG_MSI;
	} else {
		pci_intx(pdev, 1);
	}

	mv_dump_pci_cfg(pdev, 0x68);
	mv_print_info(probe_ent);

	if (ata_device_add(probe_ent) == 0) {
		rc = -ENODEV;		/* No devices discovered */
		goto err_out_dev_add;
	}

	kfree(probe_ent);
	return 0;

err_out_dev_add:
	if (MV_HP_FLAG_MSI & hpriv->hp_flags) {
		pci_disable_msi(pdev);
	} else {
		pci_intx(pdev, 0);
	}
err_out_hpriv:
	kfree(hpriv);
err_out_iounmap:
	pci_iounmap(pdev, mmio_base);
err_out_free_ent:
	kfree(probe_ent);
err_out_regions:
	pci_release_regions(pdev);
err_out:
	if (!pci_dev_busy) {
		pci_disable_device(pdev);
	}

	return rc;
}

static int __init mv_init(void)
{
	return pci_module_init(&mv_pci_driver);
}

static void __exit mv_exit(void)
{
	pci_unregister_driver(&mv_pci_driver);
}

MODULE_AUTHOR("Brett Russ");
MODULE_DESCRIPTION("SCSI low-level driver for Marvell SATA controllers");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, mv_pci_tbl);
MODULE_VERSION(DRV_VERSION);

module_init(mv_init);
module_exit(mv_exit);
