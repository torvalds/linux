/*
 * sata_mv.c - Marvell SATA support
 *
 * Copyright 2008: Marvell Corporation, all rights reserved.
 * Copyright 2005: EMC Corporation, all rights reserved.
 * Copyright 2005 Red Hat, Inc.  All rights reserved.
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

/*
 * sata_mv TODO list:
 *
 * --> Errata workaround for NCQ device errors.
 *
 * --> More errata workarounds for PCI-X.
 *
 * --> Complete a full errata audit for all chipsets to identify others.
 *
 * --> ATAPI support (Marvell claims the 60xx/70xx chips can do it).
 *
 * --> Investigate problems with PCI Message Signalled Interrupts (MSI).
 *
 * --> Cache frequently-accessed registers in mv_port_priv to reduce overhead.
 *
 * --> Develop a low-power-consumption strategy, and implement it.
 *
 * --> [Experiment, low priority] Investigate interrupt coalescing.
 *       Quite often, especially with PCI Message Signalled Interrupts (MSI),
 *       the overhead reduced by interrupt mitigation is quite often not
 *       worth the latency cost.
 *
 * --> [Experiment, Marvell value added] Is it possible to use target
 *       mode to cross-connect two Linux boxes with Marvell cards?  If so,
 *       creating LibATA target mode support would be very interesting.
 *
 *       Target mode, for those without docs, is the ability to directly
 *       connect two SATA ports.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dmapool.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>
#include <linux/mbus.h>
#include <linux/bitops.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <linux/libata.h>

#define DRV_NAME	"sata_mv"
#define DRV_VERSION	"1.24"

enum {
	/* BAR's are enumerated in terms of pci_resource_start() terms */
	MV_PRIMARY_BAR		= 0,	/* offset 0x10: memory space */
	MV_IO_BAR		= 2,	/* offset 0x18: IO space */
	MV_MISC_BAR		= 3,	/* offset 0x1c: FLASH, NVRAM, SRAM */

	MV_MAJOR_REG_AREA_SZ	= 0x10000,	/* 64KB */
	MV_MINOR_REG_AREA_SZ	= 0x2000,	/* 8KB */

	MV_PCI_REG_BASE		= 0,
	MV_IRQ_COAL_REG_BASE	= 0x18000,	/* 6xxx part only */
	MV_IRQ_COAL_CAUSE		= (MV_IRQ_COAL_REG_BASE + 0x08),
	MV_IRQ_COAL_CAUSE_LO		= (MV_IRQ_COAL_REG_BASE + 0x88),
	MV_IRQ_COAL_CAUSE_HI		= (MV_IRQ_COAL_REG_BASE + 0x8c),
	MV_IRQ_COAL_THRESHOLD		= (MV_IRQ_COAL_REG_BASE + 0xcc),
	MV_IRQ_COAL_TIME_THRESHOLD	= (MV_IRQ_COAL_REG_BASE + 0xd0),

	MV_SATAHC0_REG_BASE	= 0x20000,
	MV_FLASH_CTL_OFS	= 0x1046c,
	MV_GPIO_PORT_CTL_OFS	= 0x104f0,
	MV_RESET_CFG_OFS	= 0x180d8,

	MV_PCI_REG_SZ		= MV_MAJOR_REG_AREA_SZ,
	MV_SATAHC_REG_SZ	= MV_MAJOR_REG_AREA_SZ,
	MV_SATAHC_ARBTR_REG_SZ	= MV_MINOR_REG_AREA_SZ,		/* arbiter */
	MV_PORT_REG_SZ		= MV_MINOR_REG_AREA_SZ,

	MV_MAX_Q_DEPTH		= 32,
	MV_MAX_Q_DEPTH_MASK	= MV_MAX_Q_DEPTH - 1,

	/* CRQB needs alignment on a 1KB boundary. Size == 1KB
	 * CRPB needs alignment on a 256B boundary. Size == 256B
	 * ePRD (SG) entries need alignment on a 16B boundary. Size == 16B
	 */
	MV_CRQB_Q_SZ		= (32 * MV_MAX_Q_DEPTH),
	MV_CRPB_Q_SZ		= (8 * MV_MAX_Q_DEPTH),
	MV_MAX_SG_CT		= 256,
	MV_SG_TBL_SZ		= (16 * MV_MAX_SG_CT),

	/* Determine hc from 0-7 port: hc = port >> MV_PORT_HC_SHIFT */
	MV_PORT_HC_SHIFT	= 2,
	MV_PORTS_PER_HC		= (1 << MV_PORT_HC_SHIFT), /* 4 */
	/* Determine hc port from 0-7 port: hardport = port & MV_PORT_MASK */
	MV_PORT_MASK		= (MV_PORTS_PER_HC - 1),   /* 3 */

	/* Host Flags */
	MV_FLAG_DUAL_HC		= (1 << 30),  /* two SATA Host Controllers */
	MV_FLAG_IRQ_COALESCE	= (1 << 29),  /* IRQ coalescing capability */

	MV_COMMON_FLAGS		= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_MMIO | ATA_FLAG_NO_ATAPI |
				  ATA_FLAG_PIO_POLLING,

	MV_6XXX_FLAGS		= MV_FLAG_IRQ_COALESCE,

	MV_GENIIE_FLAGS		= MV_COMMON_FLAGS | MV_6XXX_FLAGS |
				  ATA_FLAG_PMP | ATA_FLAG_ACPI_SATA |
				  ATA_FLAG_NCQ | ATA_FLAG_AN,

	CRQB_FLAG_READ		= (1 << 0),
	CRQB_TAG_SHIFT		= 1,
	CRQB_IOID_SHIFT		= 6,	/* CRQB Gen-II/IIE IO Id shift */
	CRQB_PMP_SHIFT		= 12,	/* CRQB Gen-II/IIE PMP shift */
	CRQB_HOSTQ_SHIFT	= 17,	/* CRQB Gen-II/IIE HostQueTag shift */
	CRQB_CMD_ADDR_SHIFT	= 8,
	CRQB_CMD_CS		= (0x2 << 11),
	CRQB_CMD_LAST		= (1 << 15),

	CRPB_FLAG_STATUS_SHIFT	= 8,
	CRPB_IOID_SHIFT_6	= 5,	/* CRPB Gen-II IO Id shift */
	CRPB_IOID_SHIFT_7	= 7,	/* CRPB Gen-IIE IO Id shift */

	EPRD_FLAG_END_OF_TBL	= (1 << 31),

	/* PCI interface registers */

	PCI_COMMAND_OFS		= 0xc00,
	PCI_COMMAND_MRDTRIG	= (1 << 7),	/* PCI Master Read Trigger */

	PCI_MAIN_CMD_STS_OFS	= 0xd30,
	STOP_PCI_MASTER		= (1 << 2),
	PCI_MASTER_EMPTY	= (1 << 3),
	GLOB_SFT_RST		= (1 << 4),

	MV_PCI_MODE_OFS		= 0xd00,
	MV_PCI_MODE_MASK	= 0x30,

	MV_PCI_EXP_ROM_BAR_CTL	= 0xd2c,
	MV_PCI_DISC_TIMER	= 0xd04,
	MV_PCI_MSI_TRIGGER	= 0xc38,
	MV_PCI_SERR_MASK	= 0xc28,
	MV_PCI_XBAR_TMOUT_OFS	= 0x1d04,
	MV_PCI_ERR_LOW_ADDRESS	= 0x1d40,
	MV_PCI_ERR_HIGH_ADDRESS	= 0x1d44,
	MV_PCI_ERR_ATTRIBUTE	= 0x1d48,
	MV_PCI_ERR_COMMAND	= 0x1d50,

	PCI_IRQ_CAUSE_OFS	= 0x1d58,
	PCI_IRQ_MASK_OFS	= 0x1d5c,
	PCI_UNMASK_ALL_IRQS	= 0x7fffff,	/* bits 22-0 */

	PCIE_IRQ_CAUSE_OFS	= 0x1900,
	PCIE_IRQ_MASK_OFS	= 0x1910,
	PCIE_UNMASK_ALL_IRQS	= 0x40a,	/* assorted bits */

	/* Host Controller Main Interrupt Cause/Mask registers (1 per-chip) */
	PCI_HC_MAIN_IRQ_CAUSE_OFS = 0x1d60,
	PCI_HC_MAIN_IRQ_MASK_OFS  = 0x1d64,
	SOC_HC_MAIN_IRQ_CAUSE_OFS = 0x20020,
	SOC_HC_MAIN_IRQ_MASK_OFS  = 0x20024,
	ERR_IRQ			= (1 << 0),	/* shift by port # */
	DONE_IRQ		= (1 << 1),	/* shift by port # */
	HC0_IRQ_PEND		= 0x1ff,	/* bits 0-8 = HC0's ports */
	HC_SHIFT		= 9,		/* bits 9-17 = HC1's ports */
	PCI_ERR			= (1 << 18),
	TRAN_LO_DONE		= (1 << 19),	/* 6xxx: IRQ coalescing */
	TRAN_HI_DONE		= (1 << 20),	/* 6xxx: IRQ coalescing */
	PORTS_0_3_COAL_DONE	= (1 << 8),
	PORTS_4_7_COAL_DONE	= (1 << 17),
	PORTS_0_7_COAL_DONE	= (1 << 21),	/* 6xxx: IRQ coalescing */
	GPIO_INT		= (1 << 22),
	SELF_INT		= (1 << 23),
	TWSI_INT		= (1 << 24),
	HC_MAIN_RSVD		= (0x7f << 25),	/* bits 31-25 */
	HC_MAIN_RSVD_5		= (0x1fff << 19), /* bits 31-19 */
	HC_MAIN_RSVD_SOC	= (0x3fffffb << 6),     /* bits 31-9, 7-6 */

	/* SATAHC registers */
	HC_CFG_OFS		= 0,

	HC_IRQ_CAUSE_OFS	= 0x14,
	DMA_IRQ			= (1 << 0),	/* shift by port # */
	HC_COAL_IRQ		= (1 << 4),	/* IRQ coalescing */
	DEV_IRQ			= (1 << 8),	/* shift by port # */

	/* Shadow block registers */
	SHD_BLK_OFS		= 0x100,
	SHD_CTL_AST_OFS		= 0x20,		/* ofs from SHD_BLK_OFS */

	/* SATA registers */
	SATA_STATUS_OFS		= 0x300,  /* ctrl, err regs follow status */
	SATA_ACTIVE_OFS		= 0x350,
	SATA_FIS_IRQ_CAUSE_OFS	= 0x364,
	SATA_FIS_IRQ_AN		= (1 << 9),	/* async notification */

	LTMODE_OFS		= 0x30c,
	LTMODE_BIT8		= (1 << 8),	/* unknown, but necessary */

	PHY_MODE3		= 0x310,
	PHY_MODE4		= 0x314,
	PHY_MODE4_CFG_MASK	= 0x00000003,	/* phy internal config field */
	PHY_MODE4_CFG_VALUE	= 0x00000001,	/* phy internal config field */
	PHY_MODE4_RSVD_ZEROS	= 0x5de3fffa,	/* Gen2e always write zeros */
	PHY_MODE4_RSVD_ONES	= 0x00000005,	/* Gen2e always write ones */

	PHY_MODE2		= 0x330,
	SATA_IFCTL_OFS		= 0x344,
	SATA_TESTCTL_OFS	= 0x348,
	SATA_IFSTAT_OFS		= 0x34c,
	VENDOR_UNIQUE_FIS_OFS	= 0x35c,

	FISCFG_OFS		= 0x360,
	FISCFG_WAIT_DEV_ERR	= (1 << 8),	/* wait for host on DevErr */
	FISCFG_SINGLE_SYNC	= (1 << 16),	/* SYNC on DMA activation */

	MV5_PHY_MODE		= 0x74,
	MV5_LTMODE_OFS		= 0x30,
	MV5_PHY_CTL_OFS		= 0x0C,
	SATA_INTERFACE_CFG_OFS	= 0x050,

	MV_M2_PREAMP_MASK	= 0x7e0,

	/* Port registers */
	EDMA_CFG_OFS		= 0,
	EDMA_CFG_Q_DEPTH	= 0x1f,		/* max device queue depth */
	EDMA_CFG_NCQ		= (1 << 5),	/* for R/W FPDMA queued */
	EDMA_CFG_NCQ_GO_ON_ERR	= (1 << 14),	/* continue on error */
	EDMA_CFG_RD_BRST_EXT	= (1 << 11),	/* read burst 512B */
	EDMA_CFG_WR_BUFF_LEN	= (1 << 13),	/* write buffer 512B */
	EDMA_CFG_EDMA_FBS	= (1 << 16),	/* EDMA FIS-Based Switching */
	EDMA_CFG_FBS		= (1 << 26),	/* FIS-Based Switching */

	EDMA_ERR_IRQ_CAUSE_OFS	= 0x8,
	EDMA_ERR_IRQ_MASK_OFS	= 0xc,
	EDMA_ERR_D_PAR		= (1 << 0),	/* UDMA data parity err */
	EDMA_ERR_PRD_PAR	= (1 << 1),	/* UDMA PRD parity err */
	EDMA_ERR_DEV		= (1 << 2),	/* device error */
	EDMA_ERR_DEV_DCON	= (1 << 3),	/* device disconnect */
	EDMA_ERR_DEV_CON	= (1 << 4),	/* device connected */
	EDMA_ERR_SERR		= (1 << 5),	/* SError bits [WBDST] raised */
	EDMA_ERR_SELF_DIS	= (1 << 7),	/* Gen II/IIE self-disable */
	EDMA_ERR_SELF_DIS_5	= (1 << 8),	/* Gen I self-disable */
	EDMA_ERR_BIST_ASYNC	= (1 << 8),	/* BIST FIS or Async Notify */
	EDMA_ERR_TRANS_IRQ_7	= (1 << 8),	/* Gen IIE transprt layer irq */
	EDMA_ERR_CRQB_PAR	= (1 << 9),	/* CRQB parity error */
	EDMA_ERR_CRPB_PAR	= (1 << 10),	/* CRPB parity error */
	EDMA_ERR_INTRL_PAR	= (1 << 11),	/* internal parity error */
	EDMA_ERR_IORDY		= (1 << 12),	/* IORdy timeout */

	EDMA_ERR_LNK_CTRL_RX	= (0xf << 13),	/* link ctrl rx error */
	EDMA_ERR_LNK_CTRL_RX_0	= (1 << 13),	/* transient: CRC err */
	EDMA_ERR_LNK_CTRL_RX_1	= (1 << 14),	/* transient: FIFO err */
	EDMA_ERR_LNK_CTRL_RX_2	= (1 << 15),	/* fatal: caught SYNC */
	EDMA_ERR_LNK_CTRL_RX_3	= (1 << 16),	/* transient: FIS rx err */

	EDMA_ERR_LNK_DATA_RX	= (0xf << 17),	/* link data rx error */

	EDMA_ERR_LNK_CTRL_TX	= (0x1f << 21),	/* link ctrl tx error */
	EDMA_ERR_LNK_CTRL_TX_0	= (1 << 21),	/* transient: CRC err */
	EDMA_ERR_LNK_CTRL_TX_1	= (1 << 22),	/* transient: FIFO err */
	EDMA_ERR_LNK_CTRL_TX_2	= (1 << 23),	/* transient: caught SYNC */
	EDMA_ERR_LNK_CTRL_TX_3	= (1 << 24),	/* transient: caught DMAT */
	EDMA_ERR_LNK_CTRL_TX_4	= (1 << 25),	/* transient: FIS collision */

	EDMA_ERR_LNK_DATA_TX	= (0x1f << 26),	/* link data tx error */

	EDMA_ERR_TRANS_PROTO	= (1 << 31),	/* transport protocol error */
	EDMA_ERR_OVERRUN_5	= (1 << 5),
	EDMA_ERR_UNDERRUN_5	= (1 << 6),

	EDMA_ERR_IRQ_TRANSIENT  = EDMA_ERR_LNK_CTRL_RX_0 |
				  EDMA_ERR_LNK_CTRL_RX_1 |
				  EDMA_ERR_LNK_CTRL_RX_3 |
				  EDMA_ERR_LNK_CTRL_TX,

	EDMA_EH_FREEZE		= EDMA_ERR_D_PAR |
				  EDMA_ERR_PRD_PAR |
				  EDMA_ERR_DEV_DCON |
				  EDMA_ERR_DEV_CON |
				  EDMA_ERR_SERR |
				  EDMA_ERR_SELF_DIS |
				  EDMA_ERR_CRQB_PAR |
				  EDMA_ERR_CRPB_PAR |
				  EDMA_ERR_INTRL_PAR |
				  EDMA_ERR_IORDY |
				  EDMA_ERR_LNK_CTRL_RX_2 |
				  EDMA_ERR_LNK_DATA_RX |
				  EDMA_ERR_LNK_DATA_TX |
				  EDMA_ERR_TRANS_PROTO,

	EDMA_EH_FREEZE_5	= EDMA_ERR_D_PAR |
				  EDMA_ERR_PRD_PAR |
				  EDMA_ERR_DEV_DCON |
				  EDMA_ERR_DEV_CON |
				  EDMA_ERR_OVERRUN_5 |
				  EDMA_ERR_UNDERRUN_5 |
				  EDMA_ERR_SELF_DIS_5 |
				  EDMA_ERR_CRQB_PAR |
				  EDMA_ERR_CRPB_PAR |
				  EDMA_ERR_INTRL_PAR |
				  EDMA_ERR_IORDY,

	EDMA_REQ_Q_BASE_HI_OFS	= 0x10,
	EDMA_REQ_Q_IN_PTR_OFS	= 0x14,		/* also contains BASE_LO */

	EDMA_REQ_Q_OUT_PTR_OFS	= 0x18,
	EDMA_REQ_Q_PTR_SHIFT	= 5,

	EDMA_RSP_Q_BASE_HI_OFS	= 0x1c,
	EDMA_RSP_Q_IN_PTR_OFS	= 0x20,
	EDMA_RSP_Q_OUT_PTR_OFS	= 0x24,		/* also contains BASE_LO */
	EDMA_RSP_Q_PTR_SHIFT	= 3,

	EDMA_CMD_OFS		= 0x28,		/* EDMA command register */
	EDMA_EN			= (1 << 0),	/* enable EDMA */
	EDMA_DS			= (1 << 1),	/* disable EDMA; self-negated */
	EDMA_RESET		= (1 << 2),	/* reset eng/trans/link/phy */

	EDMA_STATUS_OFS		= 0x30,		/* EDMA engine status */
	EDMA_STATUS_CACHE_EMPTY	= (1 << 6),	/* GenIIe command cache empty */
	EDMA_STATUS_IDLE	= (1 << 7),	/* GenIIe EDMA enabled/idle */

	EDMA_IORDY_TMOUT_OFS	= 0x34,
	EDMA_ARB_CFG_OFS	= 0x38,

	EDMA_HALTCOND_OFS	= 0x60,		/* GenIIe halt conditions */

	GEN_II_NCQ_MAX_SECTORS	= 256,		/* max sects/io on Gen2 w/NCQ */

	/* Host private flags (hp_flags) */
	MV_HP_FLAG_MSI		= (1 << 0),
	MV_HP_ERRATA_50XXB0	= (1 << 1),
	MV_HP_ERRATA_50XXB2	= (1 << 2),
	MV_HP_ERRATA_60X1B2	= (1 << 3),
	MV_HP_ERRATA_60X1C0	= (1 << 4),
	MV_HP_GEN_I		= (1 << 6),	/* Generation I: 50xx */
	MV_HP_GEN_II		= (1 << 7),	/* Generation II: 60xx */
	MV_HP_GEN_IIE		= (1 << 8),	/* Generation IIE: 6042/7042 */
	MV_HP_PCIE		= (1 << 9),	/* PCIe bus/regs: 7042 */
	MV_HP_CUT_THROUGH	= (1 << 10),	/* can use EDMA cut-through */
	MV_HP_FLAG_SOC		= (1 << 11),	/* SystemOnChip, no PCI */

	/* Port private flags (pp_flags) */
	MV_PP_FLAG_EDMA_EN	= (1 << 0),	/* is EDMA engine enabled? */
	MV_PP_FLAG_NCQ_EN	= (1 << 1),	/* is EDMA set up for NCQ? */
	MV_PP_FLAG_FBS_EN	= (1 << 2),	/* is EDMA set up for FBS? */
	MV_PP_FLAG_DELAYED_EH	= (1 << 3),	/* delayed dev err handling */
};

#define IS_GEN_I(hpriv) ((hpriv)->hp_flags & MV_HP_GEN_I)
#define IS_GEN_II(hpriv) ((hpriv)->hp_flags & MV_HP_GEN_II)
#define IS_GEN_IIE(hpriv) ((hpriv)->hp_flags & MV_HP_GEN_IIE)
#define IS_PCIE(hpriv) ((hpriv)->hp_flags & MV_HP_PCIE)
#define IS_SOC(hpriv) ((hpriv)->hp_flags & MV_HP_FLAG_SOC)

#define WINDOW_CTRL(i)		(0x20030 + ((i) << 4))
#define WINDOW_BASE(i)		(0x20034 + ((i) << 4))

enum {
	/* DMA boundary 0xffff is required by the s/g splitting
	 * we need on /length/ in mv_fill-sg().
	 */
	MV_DMA_BOUNDARY		= 0xffffU,

	/* mask of register bits containing lower 32 bits
	 * of EDMA request queue DMA address
	 */
	EDMA_REQ_Q_BASE_LO_MASK	= 0xfffffc00U,

	/* ditto, for response queue */
	EDMA_RSP_Q_BASE_LO_MASK	= 0xffffff00U,
};

enum chip_type {
	chip_504x,
	chip_508x,
	chip_5080,
	chip_604x,
	chip_608x,
	chip_6042,
	chip_7042,
	chip_soc,
};

/* Command ReQuest Block: 32B */
struct mv_crqb {
	__le32			sg_addr;
	__le32			sg_addr_hi;
	__le16			ctrl_flags;
	__le16			ata_cmd[11];
};

struct mv_crqb_iie {
	__le32			addr;
	__le32			addr_hi;
	__le32			flags;
	__le32			len;
	__le32			ata_cmd[4];
};

/* Command ResPonse Block: 8B */
struct mv_crpb {
	__le16			id;
	__le16			flags;
	__le32			tmstmp;
};

/* EDMA Physical Region Descriptor (ePRD); A.K.A. SG */
struct mv_sg {
	__le32			addr;
	__le32			flags_size;
	__le32			addr_hi;
	__le32			reserved;
};

struct mv_port_priv {
	struct mv_crqb		*crqb;
	dma_addr_t		crqb_dma;
	struct mv_crpb		*crpb;
	dma_addr_t		crpb_dma;
	struct mv_sg		*sg_tbl[MV_MAX_Q_DEPTH];
	dma_addr_t		sg_tbl_dma[MV_MAX_Q_DEPTH];

	unsigned int		req_idx;
	unsigned int		resp_idx;

	u32			pp_flags;
	unsigned int		delayed_eh_pmp_map;
};

struct mv_port_signal {
	u32			amps;
	u32			pre;
};

struct mv_host_priv {
	u32			hp_flags;
	u32			main_irq_mask;
	struct mv_port_signal	signal[8];
	const struct mv_hw_ops	*ops;
	int			n_ports;
	void __iomem		*base;
	void __iomem		*main_irq_cause_addr;
	void __iomem		*main_irq_mask_addr;
	u32			irq_cause_ofs;
	u32			irq_mask_ofs;
	u32			unmask_all_irqs;
	/*
	 * These consistent DMA memory pools give us guaranteed
	 * alignment for hardware-accessed data structures,
	 * and less memory waste in accomplishing the alignment.
	 */
	struct dma_pool		*crqb_pool;
	struct dma_pool		*crpb_pool;
	struct dma_pool		*sg_tbl_pool;
};

struct mv_hw_ops {
	void (*phy_errata)(struct mv_host_priv *hpriv, void __iomem *mmio,
			   unsigned int port);
	void (*enable_leds)(struct mv_host_priv *hpriv, void __iomem *mmio);
	void (*read_preamp)(struct mv_host_priv *hpriv, int idx,
			   void __iomem *mmio);
	int (*reset_hc)(struct mv_host_priv *hpriv, void __iomem *mmio,
			unsigned int n_hc);
	void (*reset_flash)(struct mv_host_priv *hpriv, void __iomem *mmio);
	void (*reset_bus)(struct ata_host *host, void __iomem *mmio);
};

static int mv_scr_read(struct ata_port *ap, unsigned int sc_reg_in, u32 *val);
static int mv_scr_write(struct ata_port *ap, unsigned int sc_reg_in, u32 val);
static int mv5_scr_read(struct ata_port *ap, unsigned int sc_reg_in, u32 *val);
static int mv5_scr_write(struct ata_port *ap, unsigned int sc_reg_in, u32 val);
static int mv_port_start(struct ata_port *ap);
static void mv_port_stop(struct ata_port *ap);
static int mv_qc_defer(struct ata_queued_cmd *qc);
static void mv_qc_prep(struct ata_queued_cmd *qc);
static void mv_qc_prep_iie(struct ata_queued_cmd *qc);
static unsigned int mv_qc_issue(struct ata_queued_cmd *qc);
static int mv_hardreset(struct ata_link *link, unsigned int *class,
			unsigned long deadline);
static void mv_eh_freeze(struct ata_port *ap);
static void mv_eh_thaw(struct ata_port *ap);
static void mv6_dev_config(struct ata_device *dev);

static void mv5_phy_errata(struct mv_host_priv *hpriv, void __iomem *mmio,
			   unsigned int port);
static void mv5_enable_leds(struct mv_host_priv *hpriv, void __iomem *mmio);
static void mv5_read_preamp(struct mv_host_priv *hpriv, int idx,
			   void __iomem *mmio);
static int mv5_reset_hc(struct mv_host_priv *hpriv, void __iomem *mmio,
			unsigned int n_hc);
static void mv5_reset_flash(struct mv_host_priv *hpriv, void __iomem *mmio);
static void mv5_reset_bus(struct ata_host *host, void __iomem *mmio);

static void mv6_phy_errata(struct mv_host_priv *hpriv, void __iomem *mmio,
			   unsigned int port);
static void mv6_enable_leds(struct mv_host_priv *hpriv, void __iomem *mmio);
static void mv6_read_preamp(struct mv_host_priv *hpriv, int idx,
			   void __iomem *mmio);
static int mv6_reset_hc(struct mv_host_priv *hpriv, void __iomem *mmio,
			unsigned int n_hc);
static void mv6_reset_flash(struct mv_host_priv *hpriv, void __iomem *mmio);
static void mv_soc_enable_leds(struct mv_host_priv *hpriv,
				      void __iomem *mmio);
static void mv_soc_read_preamp(struct mv_host_priv *hpriv, int idx,
				      void __iomem *mmio);
static int mv_soc_reset_hc(struct mv_host_priv *hpriv,
				  void __iomem *mmio, unsigned int n_hc);
static void mv_soc_reset_flash(struct mv_host_priv *hpriv,
				      void __iomem *mmio);
static void mv_soc_reset_bus(struct ata_host *host, void __iomem *mmio);
static void mv_reset_pci_bus(struct ata_host *host, void __iomem *mmio);
static void mv_reset_channel(struct mv_host_priv *hpriv, void __iomem *mmio,
			     unsigned int port_no);
static int mv_stop_edma(struct ata_port *ap);
static int mv_stop_edma_engine(void __iomem *port_mmio);
static void mv_edma_cfg(struct ata_port *ap, int want_ncq);

static void mv_pmp_select(struct ata_port *ap, int pmp);
static int mv_pmp_hardreset(struct ata_link *link, unsigned int *class,
				unsigned long deadline);
static int  mv_softreset(struct ata_link *link, unsigned int *class,
				unsigned long deadline);
static void mv_pmp_error_handler(struct ata_port *ap);
static void mv_process_crpb_entries(struct ata_port *ap,
					struct mv_port_priv *pp);

/* .sg_tablesize is (MV_MAX_SG_CT / 2) in the structures below
 * because we have to allow room for worst case splitting of
 * PRDs for 64K boundaries in mv_fill_sg().
 */
static struct scsi_host_template mv5_sht = {
	ATA_BASE_SHT(DRV_NAME),
	.sg_tablesize		= MV_MAX_SG_CT / 2,
	.dma_boundary		= MV_DMA_BOUNDARY,
};

static struct scsi_host_template mv6_sht = {
	ATA_NCQ_SHT(DRV_NAME),
	.can_queue		= MV_MAX_Q_DEPTH - 1,
	.sg_tablesize		= MV_MAX_SG_CT / 2,
	.dma_boundary		= MV_DMA_BOUNDARY,
};

static struct ata_port_operations mv5_ops = {
	.inherits		= &ata_sff_port_ops,

	.qc_defer		= mv_qc_defer,
	.qc_prep		= mv_qc_prep,
	.qc_issue		= mv_qc_issue,

	.freeze			= mv_eh_freeze,
	.thaw			= mv_eh_thaw,
	.hardreset		= mv_hardreset,
	.error_handler		= ata_std_error_handler, /* avoid SFF EH */
	.post_internal_cmd	= ATA_OP_NULL,

	.scr_read		= mv5_scr_read,
	.scr_write		= mv5_scr_write,

	.port_start		= mv_port_start,
	.port_stop		= mv_port_stop,
};

static struct ata_port_operations mv6_ops = {
	.inherits		= &mv5_ops,
	.dev_config             = mv6_dev_config,
	.scr_read		= mv_scr_read,
	.scr_write		= mv_scr_write,

	.pmp_hardreset		= mv_pmp_hardreset,
	.pmp_softreset		= mv_softreset,
	.softreset		= mv_softreset,
	.error_handler		= mv_pmp_error_handler,
};

static struct ata_port_operations mv_iie_ops = {
	.inherits		= &mv6_ops,
	.dev_config		= ATA_OP_NULL,
	.qc_prep		= mv_qc_prep_iie,
};

static const struct ata_port_info mv_port_info[] = {
	{  /* chip_504x */
		.flags		= MV_COMMON_FLAGS,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &mv5_ops,
	},
	{  /* chip_508x */
		.flags		= MV_COMMON_FLAGS | MV_FLAG_DUAL_HC,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &mv5_ops,
	},
	{  /* chip_5080 */
		.flags		= MV_COMMON_FLAGS | MV_FLAG_DUAL_HC,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &mv5_ops,
	},
	{  /* chip_604x */
		.flags		= MV_COMMON_FLAGS | MV_6XXX_FLAGS |
				  ATA_FLAG_PMP | ATA_FLAG_ACPI_SATA |
				  ATA_FLAG_NCQ,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &mv6_ops,
	},
	{  /* chip_608x */
		.flags		= MV_COMMON_FLAGS | MV_6XXX_FLAGS |
				  ATA_FLAG_PMP | ATA_FLAG_ACPI_SATA |
				  ATA_FLAG_NCQ | MV_FLAG_DUAL_HC,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &mv6_ops,
	},
	{  /* chip_6042 */
		.flags		= MV_GENIIE_FLAGS,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &mv_iie_ops,
	},
	{  /* chip_7042 */
		.flags		= MV_GENIIE_FLAGS,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &mv_iie_ops,
	},
	{  /* chip_soc */
		.flags		= MV_GENIIE_FLAGS,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &mv_iie_ops,
	},
};

static const struct pci_device_id mv_pci_tbl[] = {
	{ PCI_VDEVICE(MARVELL, 0x5040), chip_504x },
	{ PCI_VDEVICE(MARVELL, 0x5041), chip_504x },
	{ PCI_VDEVICE(MARVELL, 0x5080), chip_5080 },
	{ PCI_VDEVICE(MARVELL, 0x5081), chip_508x },
	/* RocketRAID 1740/174x have different identifiers */
	{ PCI_VDEVICE(TTI, 0x1740), chip_508x },
	{ PCI_VDEVICE(TTI, 0x1742), chip_508x },

	{ PCI_VDEVICE(MARVELL, 0x6040), chip_604x },
	{ PCI_VDEVICE(MARVELL, 0x6041), chip_604x },
	{ PCI_VDEVICE(MARVELL, 0x6042), chip_6042 },
	{ PCI_VDEVICE(MARVELL, 0x6080), chip_608x },
	{ PCI_VDEVICE(MARVELL, 0x6081), chip_608x },

	{ PCI_VDEVICE(ADAPTEC2, 0x0241), chip_604x },

	/* Adaptec 1430SA */
	{ PCI_VDEVICE(ADAPTEC2, 0x0243), chip_7042 },

	/* Marvell 7042 support */
	{ PCI_VDEVICE(MARVELL, 0x7042), chip_7042 },

	/* Highpoint RocketRAID PCIe series */
	{ PCI_VDEVICE(TTI, 0x2300), chip_7042 },
	{ PCI_VDEVICE(TTI, 0x2310), chip_7042 },

	{ }			/* terminate list */
};

static const struct mv_hw_ops mv5xxx_ops = {
	.phy_errata		= mv5_phy_errata,
	.enable_leds		= mv5_enable_leds,
	.read_preamp		= mv5_read_preamp,
	.reset_hc		= mv5_reset_hc,
	.reset_flash		= mv5_reset_flash,
	.reset_bus		= mv5_reset_bus,
};

static const struct mv_hw_ops mv6xxx_ops = {
	.phy_errata		= mv6_phy_errata,
	.enable_leds		= mv6_enable_leds,
	.read_preamp		= mv6_read_preamp,
	.reset_hc		= mv6_reset_hc,
	.reset_flash		= mv6_reset_flash,
	.reset_bus		= mv_reset_pci_bus,
};

static const struct mv_hw_ops mv_soc_ops = {
	.phy_errata		= mv6_phy_errata,
	.enable_leds		= mv_soc_enable_leds,
	.read_preamp		= mv_soc_read_preamp,
	.reset_hc		= mv_soc_reset_hc,
	.reset_flash		= mv_soc_reset_flash,
	.reset_bus		= mv_soc_reset_bus,
};

/*
 * Functions
 */

static inline void writelfl(unsigned long data, void __iomem *addr)
{
	writel(data, addr);
	(void) readl(addr);	/* flush to avoid PCI posted write */
}

static inline unsigned int mv_hc_from_port(unsigned int port)
{
	return port >> MV_PORT_HC_SHIFT;
}

static inline unsigned int mv_hardport_from_port(unsigned int port)
{
	return port & MV_PORT_MASK;
}

/*
 * Consolidate some rather tricky bit shift calculations.
 * This is hot-path stuff, so not a function.
 * Simple code, with two return values, so macro rather than inline.
 *
 * port is the sole input, in range 0..7.
 * shift is one output, for use with main_irq_cause / main_irq_mask registers.
 * hardport is the other output, in range 0..3.
 *
 * Note that port and hardport may be the same variable in some cases.
 */
#define MV_PORT_TO_SHIFT_AND_HARDPORT(port, shift, hardport)	\
{								\
	shift    = mv_hc_from_port(port) * HC_SHIFT;		\
	hardport = mv_hardport_from_port(port);			\
	shift   += hardport * 2;				\
}

static inline void __iomem *mv_hc_base(void __iomem *base, unsigned int hc)
{
	return (base + MV_SATAHC0_REG_BASE + (hc * MV_SATAHC_REG_SZ));
}

static inline void __iomem *mv_hc_base_from_port(void __iomem *base,
						 unsigned int port)
{
	return mv_hc_base(base, mv_hc_from_port(port));
}

static inline void __iomem *mv_port_base(void __iomem *base, unsigned int port)
{
	return  mv_hc_base_from_port(base, port) +
		MV_SATAHC_ARBTR_REG_SZ +
		(mv_hardport_from_port(port) * MV_PORT_REG_SZ);
}

static void __iomem *mv5_phy_base(void __iomem *mmio, unsigned int port)
{
	void __iomem *hc_mmio = mv_hc_base_from_port(mmio, port);
	unsigned long ofs = (mv_hardport_from_port(port) + 1) * 0x100UL;

	return hc_mmio + ofs;
}

static inline void __iomem *mv_host_base(struct ata_host *host)
{
	struct mv_host_priv *hpriv = host->private_data;
	return hpriv->base;
}

static inline void __iomem *mv_ap_base(struct ata_port *ap)
{
	return mv_port_base(mv_host_base(ap->host), ap->port_no);
}

static inline int mv_get_hc_count(unsigned long port_flags)
{
	return ((port_flags & MV_FLAG_DUAL_HC) ? 2 : 1);
}

static void mv_set_edma_ptrs(void __iomem *port_mmio,
			     struct mv_host_priv *hpriv,
			     struct mv_port_priv *pp)
{
	u32 index;

	/*
	 * initialize request queue
	 */
	pp->req_idx &= MV_MAX_Q_DEPTH_MASK;	/* paranoia */
	index = pp->req_idx << EDMA_REQ_Q_PTR_SHIFT;

	WARN_ON(pp->crqb_dma & 0x3ff);
	writel((pp->crqb_dma >> 16) >> 16, port_mmio + EDMA_REQ_Q_BASE_HI_OFS);
	writelfl((pp->crqb_dma & EDMA_REQ_Q_BASE_LO_MASK) | index,
		 port_mmio + EDMA_REQ_Q_IN_PTR_OFS);
	writelfl(index, port_mmio + EDMA_REQ_Q_OUT_PTR_OFS);

	/*
	 * initialize response queue
	 */
	pp->resp_idx &= MV_MAX_Q_DEPTH_MASK;	/* paranoia */
	index = pp->resp_idx << EDMA_RSP_Q_PTR_SHIFT;

	WARN_ON(pp->crpb_dma & 0xff);
	writel((pp->crpb_dma >> 16) >> 16, port_mmio + EDMA_RSP_Q_BASE_HI_OFS);
	writelfl(index, port_mmio + EDMA_RSP_Q_IN_PTR_OFS);
	writelfl((pp->crpb_dma & EDMA_RSP_Q_BASE_LO_MASK) | index,
		 port_mmio + EDMA_RSP_Q_OUT_PTR_OFS);
}

static void mv_set_main_irq_mask(struct ata_host *host,
				 u32 disable_bits, u32 enable_bits)
{
	struct mv_host_priv *hpriv = host->private_data;
	u32 old_mask, new_mask;

	old_mask = hpriv->main_irq_mask;
	new_mask = (old_mask & ~disable_bits) | enable_bits;
	if (new_mask != old_mask) {
		hpriv->main_irq_mask = new_mask;
		writelfl(new_mask, hpriv->main_irq_mask_addr);
	}
}

static void mv_enable_port_irqs(struct ata_port *ap,
				     unsigned int port_bits)
{
	unsigned int shift, hardport, port = ap->port_no;
	u32 disable_bits, enable_bits;

	MV_PORT_TO_SHIFT_AND_HARDPORT(port, shift, hardport);

	disable_bits = (DONE_IRQ | ERR_IRQ) << shift;
	enable_bits  = port_bits << shift;
	mv_set_main_irq_mask(ap->host, disable_bits, enable_bits);
}

/**
 *      mv_start_dma - Enable eDMA engine
 *      @base: port base address
 *      @pp: port private data
 *
 *      Verify the local cache of the eDMA state is accurate with a
 *      WARN_ON.
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static void mv_start_dma(struct ata_port *ap, void __iomem *port_mmio,
			 struct mv_port_priv *pp, u8 protocol)
{
	int want_ncq = (protocol == ATA_PROT_NCQ);

	if (pp->pp_flags & MV_PP_FLAG_EDMA_EN) {
		int using_ncq = ((pp->pp_flags & MV_PP_FLAG_NCQ_EN) != 0);
		if (want_ncq != using_ncq)
			mv_stop_edma(ap);
	}
	if (!(pp->pp_flags & MV_PP_FLAG_EDMA_EN)) {
		struct mv_host_priv *hpriv = ap->host->private_data;
		int hardport = mv_hardport_from_port(ap->port_no);
		void __iomem *hc_mmio = mv_hc_base_from_port(
					mv_host_base(ap->host), hardport);
		u32 hc_irq_cause, ipending;

		/* clear EDMA event indicators, if any */
		writelfl(0, port_mmio + EDMA_ERR_IRQ_CAUSE_OFS);

		/* clear EDMA interrupt indicator, if any */
		hc_irq_cause = readl(hc_mmio + HC_IRQ_CAUSE_OFS);
		ipending = (DEV_IRQ | DMA_IRQ) << hardport;
		if (hc_irq_cause & ipending) {
			writelfl(hc_irq_cause & ~ipending,
				 hc_mmio + HC_IRQ_CAUSE_OFS);
		}

		mv_edma_cfg(ap, want_ncq);

		/* clear FIS IRQ Cause */
		if (IS_GEN_IIE(hpriv))
			writelfl(0, port_mmio + SATA_FIS_IRQ_CAUSE_OFS);

		mv_set_edma_ptrs(port_mmio, hpriv, pp);
		mv_enable_port_irqs(ap, DONE_IRQ|ERR_IRQ);

		writelfl(EDMA_EN, port_mmio + EDMA_CMD_OFS);
		pp->pp_flags |= MV_PP_FLAG_EDMA_EN;
	}
}

static void mv_wait_for_edma_empty_idle(struct ata_port *ap)
{
	void __iomem *port_mmio = mv_ap_base(ap);
	const u32 empty_idle = (EDMA_STATUS_CACHE_EMPTY | EDMA_STATUS_IDLE);
	const int per_loop = 5, timeout = (15 * 1000 / per_loop);
	int i;

	/*
	 * Wait for the EDMA engine to finish transactions in progress.
	 * No idea what a good "timeout" value might be, but measurements
	 * indicate that it often requires hundreds of microseconds
	 * with two drives in-use.  So we use the 15msec value above
	 * as a rough guess at what even more drives might require.
	 */
	for (i = 0; i < timeout; ++i) {
		u32 edma_stat = readl(port_mmio + EDMA_STATUS_OFS);
		if ((edma_stat & empty_idle) == empty_idle)
			break;
		udelay(per_loop);
	}
	/* ata_port_printk(ap, KERN_INFO, "%s: %u+ usecs\n", __func__, i); */
}

/**
 *      mv_stop_edma_engine - Disable eDMA engine
 *      @port_mmio: io base address
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static int mv_stop_edma_engine(void __iomem *port_mmio)
{
	int i;

	/* Disable eDMA.  The disable bit auto clears. */
	writelfl(EDMA_DS, port_mmio + EDMA_CMD_OFS);

	/* Wait for the chip to confirm eDMA is off. */
	for (i = 10000; i > 0; i--) {
		u32 reg = readl(port_mmio + EDMA_CMD_OFS);
		if (!(reg & EDMA_EN))
			return 0;
		udelay(10);
	}
	return -EIO;
}

static int mv_stop_edma(struct ata_port *ap)
{
	void __iomem *port_mmio = mv_ap_base(ap);
	struct mv_port_priv *pp = ap->private_data;

	if (!(pp->pp_flags & MV_PP_FLAG_EDMA_EN))
		return 0;
	pp->pp_flags &= ~MV_PP_FLAG_EDMA_EN;
	mv_wait_for_edma_empty_idle(ap);
	if (mv_stop_edma_engine(port_mmio)) {
		ata_port_printk(ap, KERN_ERR, "Unable to stop eDMA\n");
		return -EIO;
	}
	return 0;
}

#ifdef ATA_DEBUG
static void mv_dump_mem(void __iomem *start, unsigned bytes)
{
	int b, w;
	for (b = 0; b < bytes; ) {
		DPRINTK("%p: ", start + b);
		for (w = 0; b < bytes && w < 4; w++) {
			printk("%08x ", readl(start + b));
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
			(void) pci_read_config_dword(pdev, b, &dw);
			printk("%08x ", dw);
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
		hc_base = mv_hc_base(mmio_base, hc);
		DPRINTK("HC regs (HC %i):\n", hc);
		mv_dump_mem(hc_base, 0x1c);
	}
	for (p = start_port; p < start_port + num_ports; p++) {
		port_base = mv_port_base(mmio_base, p);
		DPRINTK("EDMA regs (port %i):\n", p);
		mv_dump_mem(port_base, 0x54);
		DPRINTK("SATA regs (port %i):\n", p);
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

static int mv_scr_read(struct ata_port *ap, unsigned int sc_reg_in, u32 *val)
{
	unsigned int ofs = mv_scr_offset(sc_reg_in);

	if (ofs != 0xffffffffU) {
		*val = readl(mv_ap_base(ap) + ofs);
		return 0;
	} else
		return -EINVAL;
}

static int mv_scr_write(struct ata_port *ap, unsigned int sc_reg_in, u32 val)
{
	unsigned int ofs = mv_scr_offset(sc_reg_in);

	if (ofs != 0xffffffffU) {
		writelfl(val, mv_ap_base(ap) + ofs);
		return 0;
	} else
		return -EINVAL;
}

static void mv6_dev_config(struct ata_device *adev)
{
	/*
	 * Deal with Gen-II ("mv6") hardware quirks/restrictions:
	 *
	 * Gen-II does not support NCQ over a port multiplier
	 *  (no FIS-based switching).
	 *
	 * We don't have hob_nsect when doing NCQ commands on Gen-II.
	 * See mv_qc_prep() for more info.
	 */
	if (adev->flags & ATA_DFLAG_NCQ) {
		if (sata_pmp_attached(adev->link->ap)) {
			adev->flags &= ~ATA_DFLAG_NCQ;
			ata_dev_printk(adev, KERN_INFO,
				"NCQ disabled for command-based switching\n");
		} else if (adev->max_sectors > GEN_II_NCQ_MAX_SECTORS) {
			adev->max_sectors = GEN_II_NCQ_MAX_SECTORS;
			ata_dev_printk(adev, KERN_INFO,
				"max_sectors limited to %u for NCQ\n",
				adev->max_sectors);
		}
	}
}

static int mv_qc_defer(struct ata_queued_cmd *qc)
{
	struct ata_link *link = qc->dev->link;
	struct ata_port *ap = link->ap;
	struct mv_port_priv *pp = ap->private_data;

	/*
	 * Don't allow new commands if we're in a delayed EH state
	 * for NCQ and/or FIS-based switching.
	 */
	if (pp->pp_flags & MV_PP_FLAG_DELAYED_EH)
		return ATA_DEFER_PORT;
	/*
	 * If the port is completely idle, then allow the new qc.
	 */
	if (ap->nr_active_links == 0)
		return 0;

	/*
	 * The port is operating in host queuing mode (EDMA) with NCQ
	 * enabled, allow multiple NCQ commands.  EDMA also allows
	 * queueing multiple DMA commands but libata core currently
	 * doesn't allow it.
	 */
	if ((pp->pp_flags & MV_PP_FLAG_EDMA_EN) &&
	    (pp->pp_flags & MV_PP_FLAG_NCQ_EN) && ata_is_ncq(qc->tf.protocol))
		return 0;

	return ATA_DEFER_PORT;
}

static void mv_config_fbs(void __iomem *port_mmio, int want_ncq, int want_fbs)
{
	u32 new_fiscfg, old_fiscfg;
	u32 new_ltmode, old_ltmode;
	u32 new_haltcond, old_haltcond;

	old_fiscfg   = readl(port_mmio + FISCFG_OFS);
	old_ltmode   = readl(port_mmio + LTMODE_OFS);
	old_haltcond = readl(port_mmio + EDMA_HALTCOND_OFS);

	new_fiscfg   = old_fiscfg & ~(FISCFG_SINGLE_SYNC | FISCFG_WAIT_DEV_ERR);
	new_ltmode   = old_ltmode & ~LTMODE_BIT8;
	new_haltcond = old_haltcond | EDMA_ERR_DEV;

	if (want_fbs) {
		new_fiscfg = old_fiscfg | FISCFG_SINGLE_SYNC;
		new_ltmode = old_ltmode | LTMODE_BIT8;
		if (want_ncq)
			new_haltcond &= ~EDMA_ERR_DEV;
		else
			new_fiscfg |=  FISCFG_WAIT_DEV_ERR;
	}

	if (new_fiscfg != old_fiscfg)
		writelfl(new_fiscfg, port_mmio + FISCFG_OFS);
	if (new_ltmode != old_ltmode)
		writelfl(new_ltmode, port_mmio + LTMODE_OFS);
	if (new_haltcond != old_haltcond)
		writelfl(new_haltcond, port_mmio + EDMA_HALTCOND_OFS);
}

static void mv_60x1_errata_sata25(struct ata_port *ap, int want_ncq)
{
	struct mv_host_priv *hpriv = ap->host->private_data;
	u32 old, new;

	/* workaround for 88SX60x1 FEr SATA#25 (part 1) */
	old = readl(hpriv->base + MV_GPIO_PORT_CTL_OFS);
	if (want_ncq)
		new = old | (1 << 22);
	else
		new = old & ~(1 << 22);
	if (new != old)
		writel(new, hpriv->base + MV_GPIO_PORT_CTL_OFS);
}

static void mv_edma_cfg(struct ata_port *ap, int want_ncq)
{
	u32 cfg;
	struct mv_port_priv *pp    = ap->private_data;
	struct mv_host_priv *hpriv = ap->host->private_data;
	void __iomem *port_mmio    = mv_ap_base(ap);

	/* set up non-NCQ EDMA configuration */
	cfg = EDMA_CFG_Q_DEPTH;		/* always 0x1f for *all* chips */
	pp->pp_flags &= ~MV_PP_FLAG_FBS_EN;

	if (IS_GEN_I(hpriv))
		cfg |= (1 << 8);	/* enab config burst size mask */

	else if (IS_GEN_II(hpriv)) {
		cfg |= EDMA_CFG_RD_BRST_EXT | EDMA_CFG_WR_BUFF_LEN;
		mv_60x1_errata_sata25(ap, want_ncq);

	} else if (IS_GEN_IIE(hpriv)) {
		int want_fbs = sata_pmp_attached(ap);
		/*
		 * Possible future enhancement:
		 *
		 * The chip can use FBS with non-NCQ, if we allow it,
		 * But first we need to have the error handling in place
		 * for this mode (datasheet section 7.3.15.4.2.3).
		 * So disallow non-NCQ FBS for now.
		 */
		want_fbs &= want_ncq;

		mv_config_fbs(port_mmio, want_ncq, want_fbs);

		if (want_fbs) {
			pp->pp_flags |= MV_PP_FLAG_FBS_EN;
			cfg |= EDMA_CFG_EDMA_FBS; /* FIS-based switching */
		}

		cfg |= (1 << 23);	/* do not mask PM field in rx'd FIS */
		cfg |= (1 << 22);	/* enab 4-entry host queue cache */
		if (!IS_SOC(hpriv))
			cfg |= (1 << 18);	/* enab early completion */
		if (hpriv->hp_flags & MV_HP_CUT_THROUGH)
			cfg |= (1 << 17); /* enab cut-thru (dis stor&forwrd) */
	}

	if (want_ncq) {
		cfg |= EDMA_CFG_NCQ;
		pp->pp_flags |=  MV_PP_FLAG_NCQ_EN;
	} else
		pp->pp_flags &= ~MV_PP_FLAG_NCQ_EN;

	writelfl(cfg, port_mmio + EDMA_CFG_OFS);
}

static void mv_port_free_dma_mem(struct ata_port *ap)
{
	struct mv_host_priv *hpriv = ap->host->private_data;
	struct mv_port_priv *pp = ap->private_data;
	int tag;

	if (pp->crqb) {
		dma_pool_free(hpriv->crqb_pool, pp->crqb, pp->crqb_dma);
		pp->crqb = NULL;
	}
	if (pp->crpb) {
		dma_pool_free(hpriv->crpb_pool, pp->crpb, pp->crpb_dma);
		pp->crpb = NULL;
	}
	/*
	 * For GEN_I, there's no NCQ, so we have only a single sg_tbl.
	 * For later hardware, we have one unique sg_tbl per NCQ tag.
	 */
	for (tag = 0; tag < MV_MAX_Q_DEPTH; ++tag) {
		if (pp->sg_tbl[tag]) {
			if (tag == 0 || !IS_GEN_I(hpriv))
				dma_pool_free(hpriv->sg_tbl_pool,
					      pp->sg_tbl[tag],
					      pp->sg_tbl_dma[tag]);
			pp->sg_tbl[tag] = NULL;
		}
	}
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
	struct device *dev = ap->host->dev;
	struct mv_host_priv *hpriv = ap->host->private_data;
	struct mv_port_priv *pp;
	int tag;

	pp = devm_kzalloc(dev, sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;
	ap->private_data = pp;

	pp->crqb = dma_pool_alloc(hpriv->crqb_pool, GFP_KERNEL, &pp->crqb_dma);
	if (!pp->crqb)
		return -ENOMEM;
	memset(pp->crqb, 0, MV_CRQB_Q_SZ);

	pp->crpb = dma_pool_alloc(hpriv->crpb_pool, GFP_KERNEL, &pp->crpb_dma);
	if (!pp->crpb)
		goto out_port_free_dma_mem;
	memset(pp->crpb, 0, MV_CRPB_Q_SZ);

	/* 6041/6081 Rev. "C0" (and newer) are okay with async notify */
	if (hpriv->hp_flags & MV_HP_ERRATA_60X1C0)
		ap->flags |= ATA_FLAG_AN;
	/*
	 * For GEN_I, there's no NCQ, so we only allocate a single sg_tbl.
	 * For later hardware, we need one unique sg_tbl per NCQ tag.
	 */
	for (tag = 0; tag < MV_MAX_Q_DEPTH; ++tag) {
		if (tag == 0 || !IS_GEN_I(hpriv)) {
			pp->sg_tbl[tag] = dma_pool_alloc(hpriv->sg_tbl_pool,
					      GFP_KERNEL, &pp->sg_tbl_dma[tag]);
			if (!pp->sg_tbl[tag])
				goto out_port_free_dma_mem;
		} else {
			pp->sg_tbl[tag]     = pp->sg_tbl[0];
			pp->sg_tbl_dma[tag] = pp->sg_tbl_dma[0];
		}
	}
	return 0;

out_port_free_dma_mem:
	mv_port_free_dma_mem(ap);
	return -ENOMEM;
}

/**
 *      mv_port_stop - Port specific cleanup/stop routine.
 *      @ap: ATA channel to manipulate
 *
 *      Stop DMA, cleanup port memory.
 *
 *      LOCKING:
 *      This routine uses the host lock to protect the DMA stop.
 */
static void mv_port_stop(struct ata_port *ap)
{
	mv_stop_edma(ap);
	mv_enable_port_irqs(ap, 0);
	mv_port_free_dma_mem(ap);
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
	struct scatterlist *sg;
	struct mv_sg *mv_sg, *last_sg = NULL;
	unsigned int si;

	mv_sg = pp->sg_tbl[qc->tag];
	for_each_sg(qc->sg, sg, qc->n_elem, si) {
		dma_addr_t addr = sg_dma_address(sg);
		u32 sg_len = sg_dma_len(sg);

		while (sg_len) {
			u32 offset = addr & 0xffff;
			u32 len = sg_len;

			if ((offset + sg_len > 0x10000))
				len = 0x10000 - offset;

			mv_sg->addr = cpu_to_le32(addr & 0xffffffff);
			mv_sg->addr_hi = cpu_to_le32((addr >> 16) >> 16);
			mv_sg->flags_size = cpu_to_le32(len & 0xffff);

			sg_len -= len;
			addr += len;

			last_sg = mv_sg;
			mv_sg++;
		}
	}

	if (likely(last_sg))
		last_sg->flags_size |= cpu_to_le32(EPRD_FLAG_END_OF_TBL);
}

static void mv_crqb_pack_cmd(__le16 *cmdw, u8 data, u8 addr, unsigned last)
{
	u16 tmp = data | (addr << CRQB_CMD_ADDR_SHIFT) | CRQB_CMD_CS |
		(last ? CRQB_CMD_LAST : 0);
	*cmdw = cpu_to_le16(tmp);
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
	__le16 *cw;
	struct ata_taskfile *tf;
	u16 flags = 0;
	unsigned in_index;

	if ((qc->tf.protocol != ATA_PROT_DMA) &&
	    (qc->tf.protocol != ATA_PROT_NCQ))
		return;

	/* Fill in command request block
	 */
	if (!(qc->tf.flags & ATA_TFLAG_WRITE))
		flags |= CRQB_FLAG_READ;
	WARN_ON(MV_MAX_Q_DEPTH <= qc->tag);
	flags |= qc->tag << CRQB_TAG_SHIFT;
	flags |= (qc->dev->link->pmp & 0xf) << CRQB_PMP_SHIFT;

	/* get current queue index from software */
	in_index = pp->req_idx;

	pp->crqb[in_index].sg_addr =
		cpu_to_le32(pp->sg_tbl_dma[qc->tag] & 0xffffffff);
	pp->crqb[in_index].sg_addr_hi =
		cpu_to_le32((pp->sg_tbl_dma[qc->tag] >> 16) >> 16);
	pp->crqb[in_index].ctrl_flags = cpu_to_le16(flags);

	cw = &pp->crqb[in_index].ata_cmd[0];
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
	case ATA_CMD_WRITE_FUA_EXT:
		mv_crqb_pack_cmd(cw++, tf->hob_nsect, ATA_REG_NSECT, 0);
		break;
	case ATA_CMD_FPDMA_READ:
	case ATA_CMD_FPDMA_WRITE:
		mv_crqb_pack_cmd(cw++, tf->hob_feature, ATA_REG_FEATURE, 0);
		mv_crqb_pack_cmd(cw++, tf->feature, ATA_REG_FEATURE, 0);
		break;
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

	if (!(qc->flags & ATA_QCFLAG_DMAMAP))
		return;
	mv_fill_sg(qc);
}

/**
 *      mv_qc_prep_iie - Host specific command preparation.
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
static void mv_qc_prep_iie(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct mv_port_priv *pp = ap->private_data;
	struct mv_crqb_iie *crqb;
	struct ata_taskfile *tf;
	unsigned in_index;
	u32 flags = 0;

	if ((qc->tf.protocol != ATA_PROT_DMA) &&
	    (qc->tf.protocol != ATA_PROT_NCQ))
		return;

	/* Fill in Gen IIE command request block */
	if (!(qc->tf.flags & ATA_TFLAG_WRITE))
		flags |= CRQB_FLAG_READ;

	WARN_ON(MV_MAX_Q_DEPTH <= qc->tag);
	flags |= qc->tag << CRQB_TAG_SHIFT;
	flags |= qc->tag << CRQB_HOSTQ_SHIFT;
	flags |= (qc->dev->link->pmp & 0xf) << CRQB_PMP_SHIFT;

	/* get current queue index from software */
	in_index = pp->req_idx;

	crqb = (struct mv_crqb_iie *) &pp->crqb[in_index];
	crqb->addr = cpu_to_le32(pp->sg_tbl_dma[qc->tag] & 0xffffffff);
	crqb->addr_hi = cpu_to_le32((pp->sg_tbl_dma[qc->tag] >> 16) >> 16);
	crqb->flags = cpu_to_le32(flags);

	tf = &qc->tf;
	crqb->ata_cmd[0] = cpu_to_le32(
			(tf->command << 16) |
			(tf->feature << 24)
		);
	crqb->ata_cmd[1] = cpu_to_le32(
			(tf->lbal << 0) |
			(tf->lbam << 8) |
			(tf->lbah << 16) |
			(tf->device << 24)
		);
	crqb->ata_cmd[2] = cpu_to_le32(
			(tf->hob_lbal << 0) |
			(tf->hob_lbam << 8) |
			(tf->hob_lbah << 16) |
			(tf->hob_feature << 24)
		);
	crqb->ata_cmd[3] = cpu_to_le32(
			(tf->nsect << 0) |
			(tf->hob_nsect << 8)
		);

	if (!(qc->flags & ATA_QCFLAG_DMAMAP))
		return;
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
static unsigned int mv_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	void __iomem *port_mmio = mv_ap_base(ap);
	struct mv_port_priv *pp = ap->private_data;
	u32 in_index;

	if ((qc->tf.protocol != ATA_PROT_DMA) &&
	    (qc->tf.protocol != ATA_PROT_NCQ)) {
		static int limit_warnings = 10;
		/*
		 * Errata SATA#16, SATA#24: warn if multiple DRQs expected.
		 *
		 * Someday, we might implement special polling workarounds
		 * for these, but it all seems rather unnecessary since we
		 * normally use only DMA for commands which transfer more
		 * than a single block of data.
		 *
		 * Much of the time, this could just work regardless.
		 * So for now, just log the incident, and allow the attempt.
		 */
		if (limit_warnings > 0 && (qc->nbytes / qc->sect_size) > 1) {
			--limit_warnings;
			ata_link_printk(qc->dev->link, KERN_WARNING, DRV_NAME
					": attempting PIO w/multiple DRQ: "
					"this may fail due to h/w errata\n");
		}
		/*
		 * We're about to send a non-EDMA capable command to the
		 * port.  Turn off EDMA so there won't be problems accessing
		 * shadow block, etc registers.
		 */
		mv_stop_edma(ap);
		mv_enable_port_irqs(ap, ERR_IRQ);
		mv_pmp_select(ap, qc->dev->link->pmp);
		return ata_sff_qc_issue(qc);
	}

	mv_start_dma(ap, port_mmio, pp, qc->tf.protocol);

	pp->req_idx = (pp->req_idx + 1) & MV_MAX_Q_DEPTH_MASK;
	in_index = pp->req_idx << EDMA_REQ_Q_PTR_SHIFT;

	/* and write the request in pointer to kick the EDMA to life */
	writelfl((pp->crqb_dma & EDMA_REQ_Q_BASE_LO_MASK) | in_index,
		 port_mmio + EDMA_REQ_Q_IN_PTR_OFS);

	return 0;
}

static struct ata_queued_cmd *mv_get_active_qc(struct ata_port *ap)
{
	struct mv_port_priv *pp = ap->private_data;
	struct ata_queued_cmd *qc;

	if (pp->pp_flags & MV_PP_FLAG_NCQ_EN)
		return NULL;
	qc = ata_qc_from_tag(ap, ap->link.active_tag);
	if (qc && (qc->tf.flags & ATA_TFLAG_POLLING))
		qc = NULL;
	return qc;
}

static void mv_pmp_error_handler(struct ata_port *ap)
{
	unsigned int pmp, pmp_map;
	struct mv_port_priv *pp = ap->private_data;

	if (pp->pp_flags & MV_PP_FLAG_DELAYED_EH) {
		/*
		 * Perform NCQ error analysis on failed PMPs
		 * before we freeze the port entirely.
		 *
		 * The failed PMPs are marked earlier by mv_pmp_eh_prep().
		 */
		pmp_map = pp->delayed_eh_pmp_map;
		pp->pp_flags &= ~MV_PP_FLAG_DELAYED_EH;
		for (pmp = 0; pmp_map != 0; pmp++) {
			unsigned int this_pmp = (1 << pmp);
			if (pmp_map & this_pmp) {
				struct ata_link *link = &ap->pmp_link[pmp];
				pmp_map &= ~this_pmp;
				ata_eh_analyze_ncq_error(link);
			}
		}
		ata_port_freeze(ap);
	}
	sata_pmp_error_handler(ap);
}

static unsigned int mv_get_err_pmp_map(struct ata_port *ap)
{
	void __iomem *port_mmio = mv_ap_base(ap);

	return readl(port_mmio + SATA_TESTCTL_OFS) >> 16;
}

static void mv_pmp_eh_prep(struct ata_port *ap, unsigned int pmp_map)
{
	struct ata_eh_info *ehi;
	unsigned int pmp;

	/*
	 * Initialize EH info for PMPs which saw device errors
	 */
	ehi = &ap->link.eh_info;
	for (pmp = 0; pmp_map != 0; pmp++) {
		unsigned int this_pmp = (1 << pmp);
		if (pmp_map & this_pmp) {
			struct ata_link *link = &ap->pmp_link[pmp];

			pmp_map &= ~this_pmp;
			ehi = &link->eh_info;
			ata_ehi_clear_desc(ehi);
			ata_ehi_push_desc(ehi, "dev err");
			ehi->err_mask |= AC_ERR_DEV;
			ehi->action |= ATA_EH_RESET;
			ata_link_abort(link);
		}
	}
}

static int mv_req_q_empty(struct ata_port *ap)
{
	void __iomem *port_mmio = mv_ap_base(ap);
	u32 in_ptr, out_ptr;

	in_ptr  = (readl(port_mmio + EDMA_REQ_Q_IN_PTR_OFS)
			>> EDMA_REQ_Q_PTR_SHIFT) & MV_MAX_Q_DEPTH_MASK;
	out_ptr = (readl(port_mmio + EDMA_REQ_Q_OUT_PTR_OFS)
			>> EDMA_REQ_Q_PTR_SHIFT) & MV_MAX_Q_DEPTH_MASK;
	return (in_ptr == out_ptr);	/* 1 == queue_is_empty */
}

static int mv_handle_fbs_ncq_dev_err(struct ata_port *ap)
{
	struct mv_port_priv *pp = ap->private_data;
	int failed_links;
	unsigned int old_map, new_map;

	/*
	 * Device error during FBS+NCQ operation:
	 *
	 * Set a port flag to prevent further I/O being enqueued.
	 * Leave the EDMA running to drain outstanding commands from this port.
	 * Perform the post-mortem/EH only when all responses are complete.
	 * Follow recovery sequence from 6042/7042 datasheet (7.3.15.4.2.2).
	 */
	if (!(pp->pp_flags & MV_PP_FLAG_DELAYED_EH)) {
		pp->pp_flags |= MV_PP_FLAG_DELAYED_EH;
		pp->delayed_eh_pmp_map = 0;
	}
	old_map = pp->delayed_eh_pmp_map;
	new_map = old_map | mv_get_err_pmp_map(ap);

	if (old_map != new_map) {
		pp->delayed_eh_pmp_map = new_map;
		mv_pmp_eh_prep(ap, new_map & ~old_map);
	}
	failed_links = hweight16(new_map);

	ata_port_printk(ap, KERN_INFO, "%s: pmp_map=%04x qc_map=%04x "
			"failed_links=%d nr_active_links=%d\n",
			__func__, pp->delayed_eh_pmp_map,
			ap->qc_active, failed_links,
			ap->nr_active_links);

	if (ap->nr_active_links <= failed_links && mv_req_q_empty(ap)) {
		mv_process_crpb_entries(ap, pp);
		mv_stop_edma(ap);
		mv_eh_freeze(ap);
		ata_port_printk(ap, KERN_INFO, "%s: done\n", __func__);
		return 1;	/* handled */
	}
	ata_port_printk(ap, KERN_INFO, "%s: waiting\n", __func__);
	return 1;	/* handled */
}

static int mv_handle_fbs_non_ncq_dev_err(struct ata_port *ap)
{
	/*
	 * Possible future enhancement:
	 *
	 * FBS+non-NCQ operation is not yet implemented.
	 * See related notes in mv_edma_cfg().
	 *
	 * Device error during FBS+non-NCQ operation:
	 *
	 * We need to snapshot the shadow registers for each failed command.
	 * Follow recovery sequence from 6042/7042 datasheet (7.3.15.4.2.3).
	 */
	return 0;	/* not handled */
}

static int mv_handle_dev_err(struct ata_port *ap, u32 edma_err_cause)
{
	struct mv_port_priv *pp = ap->private_data;

	if (!(pp->pp_flags & MV_PP_FLAG_EDMA_EN))
		return 0;	/* EDMA was not active: not handled */
	if (!(pp->pp_flags & MV_PP_FLAG_FBS_EN))
		return 0;	/* FBS was not active: not handled */

	if (!(edma_err_cause & EDMA_ERR_DEV))
		return 0;	/* non DEV error: not handled */
	edma_err_cause &= ~EDMA_ERR_IRQ_TRANSIENT;
	if (edma_err_cause & ~(EDMA_ERR_DEV | EDMA_ERR_SELF_DIS))
		return 0;	/* other problems: not handled */

	if (pp->pp_flags & MV_PP_FLAG_NCQ_EN) {
		/*
		 * EDMA should NOT have self-disabled for this case.
		 * If it did, then something is wrong elsewhere,
		 * and we cannot handle it here.
		 */
		if (edma_err_cause & EDMA_ERR_SELF_DIS) {
			ata_port_printk(ap, KERN_WARNING,
				"%s: err_cause=0x%x pp_flags=0x%x\n",
				__func__, edma_err_cause, pp->pp_flags);
			return 0; /* not handled */
		}
		return mv_handle_fbs_ncq_dev_err(ap);
	} else {
		/*
		 * EDMA should have self-disabled for this case.
		 * If it did not, then something is wrong elsewhere,
		 * and we cannot handle it here.
		 */
		if (!(edma_err_cause & EDMA_ERR_SELF_DIS)) {
			ata_port_printk(ap, KERN_WARNING,
				"%s: err_cause=0x%x pp_flags=0x%x\n",
				__func__, edma_err_cause, pp->pp_flags);
			return 0; /* not handled */
		}
		return mv_handle_fbs_non_ncq_dev_err(ap);
	}
	return 0;	/* not handled */
}

static void mv_unexpected_intr(struct ata_port *ap, int edma_was_enabled)
{
	struct ata_eh_info *ehi = &ap->link.eh_info;
	char *when = "idle";

	ata_ehi_clear_desc(ehi);
	if (!ap || (ap->flags & ATA_FLAG_DISABLED)) {
		when = "disabled";
	} else if (edma_was_enabled) {
		when = "EDMA enabled";
	} else {
		struct ata_queued_cmd *qc = ata_qc_from_tag(ap, ap->link.active_tag);
		if (qc && (qc->tf.flags & ATA_TFLAG_POLLING))
			when = "polling";
	}
	ata_ehi_push_desc(ehi, "unexpected device interrupt while %s", when);
	ehi->err_mask |= AC_ERR_OTHER;
	ehi->action   |= ATA_EH_RESET;
	ata_port_freeze(ap);
}

/**
 *      mv_err_intr - Handle error interrupts on the port
 *      @ap: ATA channel to manipulate
 *      @qc: affected command (non-NCQ), or NULL
 *
 *      Most cases require a full reset of the chip's state machine,
 *      which also performs a COMRESET.
 *      Also, if the port disabled DMA, update our cached copy to match.
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static void mv_err_intr(struct ata_port *ap)
{
	void __iomem *port_mmio = mv_ap_base(ap);
	u32 edma_err_cause, eh_freeze_mask, serr = 0;
	u32 fis_cause = 0;
	struct mv_port_priv *pp = ap->private_data;
	struct mv_host_priv *hpriv = ap->host->private_data;
	unsigned int action = 0, err_mask = 0;
	struct ata_eh_info *ehi = &ap->link.eh_info;
	struct ata_queued_cmd *qc;
	int abort = 0;

	/*
	 * Read and clear the SError and err_cause bits.
	 * For GenIIe, if EDMA_ERR_TRANS_IRQ_7 is set, we also must read/clear
	 * the FIS_IRQ_CAUSE register before clearing edma_err_cause.
	 */
	sata_scr_read(&ap->link, SCR_ERROR, &serr);
	sata_scr_write_flush(&ap->link, SCR_ERROR, serr);

	edma_err_cause = readl(port_mmio + EDMA_ERR_IRQ_CAUSE_OFS);
	if (IS_GEN_IIE(hpriv) && (edma_err_cause & EDMA_ERR_TRANS_IRQ_7)) {
		fis_cause = readl(port_mmio + SATA_FIS_IRQ_CAUSE_OFS);
		writelfl(~fis_cause, port_mmio + SATA_FIS_IRQ_CAUSE_OFS);
	}
	writelfl(~edma_err_cause, port_mmio + EDMA_ERR_IRQ_CAUSE_OFS);

	if (edma_err_cause & EDMA_ERR_DEV) {
		/*
		 * Device errors during FIS-based switching operation
		 * require special handling.
		 */
		if (mv_handle_dev_err(ap, edma_err_cause))
			return;
	}

	qc = mv_get_active_qc(ap);
	ata_ehi_clear_desc(ehi);
	ata_ehi_push_desc(ehi, "edma_err_cause=%08x pp_flags=%08x",
			  edma_err_cause, pp->pp_flags);

	if (IS_GEN_IIE(hpriv) && (edma_err_cause & EDMA_ERR_TRANS_IRQ_7)) {
		ata_ehi_push_desc(ehi, "fis_cause=%08x", fis_cause);
		if (fis_cause & SATA_FIS_IRQ_AN) {
			u32 ec = edma_err_cause &
			       ~(EDMA_ERR_TRANS_IRQ_7 | EDMA_ERR_IRQ_TRANSIENT);
			sata_async_notification(ap);
			if (!ec)
				return; /* Just an AN; no need for the nukes */
			ata_ehi_push_desc(ehi, "SDB notify");
		}
	}
	/*
	 * All generations share these EDMA error cause bits:
	 */
	if (edma_err_cause & EDMA_ERR_DEV) {
		err_mask |= AC_ERR_DEV;
		action |= ATA_EH_RESET;
		ata_ehi_push_desc(ehi, "dev error");
	}
	if (edma_err_cause & (EDMA_ERR_D_PAR | EDMA_ERR_PRD_PAR |
			EDMA_ERR_CRQB_PAR | EDMA_ERR_CRPB_PAR |
			EDMA_ERR_INTRL_PAR)) {
		err_mask |= AC_ERR_ATA_BUS;
		action |= ATA_EH_RESET;
		ata_ehi_push_desc(ehi, "parity error");
	}
	if (edma_err_cause & (EDMA_ERR_DEV_DCON | EDMA_ERR_DEV_CON)) {
		ata_ehi_hotplugged(ehi);
		ata_ehi_push_desc(ehi, edma_err_cause & EDMA_ERR_DEV_DCON ?
			"dev disconnect" : "dev connect");
		action |= ATA_EH_RESET;
	}

	/*
	 * Gen-I has a different SELF_DIS bit,
	 * different FREEZE bits, and no SERR bit:
	 */
	if (IS_GEN_I(hpriv)) {
		eh_freeze_mask = EDMA_EH_FREEZE_5;
		if (edma_err_cause & EDMA_ERR_SELF_DIS_5) {
			pp->pp_flags &= ~MV_PP_FLAG_EDMA_EN;
			ata_ehi_push_desc(ehi, "EDMA self-disable");
		}
	} else {
		eh_freeze_mask = EDMA_EH_FREEZE;
		if (edma_err_cause & EDMA_ERR_SELF_DIS) {
			pp->pp_flags &= ~MV_PP_FLAG_EDMA_EN;
			ata_ehi_push_desc(ehi, "EDMA self-disable");
		}
		if (edma_err_cause & EDMA_ERR_SERR) {
			ata_ehi_push_desc(ehi, "SError=%08x", serr);
			err_mask |= AC_ERR_ATA_BUS;
			action |= ATA_EH_RESET;
		}
	}

	if (!err_mask) {
		err_mask = AC_ERR_OTHER;
		action |= ATA_EH_RESET;
	}

	ehi->serror |= serr;
	ehi->action |= action;

	if (qc)
		qc->err_mask |= err_mask;
	else
		ehi->err_mask |= err_mask;

	if (err_mask == AC_ERR_DEV) {
		/*
		 * Cannot do ata_port_freeze() here,
		 * because it would kill PIO access,
		 * which is needed for further diagnosis.
		 */
		mv_eh_freeze(ap);
		abort = 1;
	} else if (edma_err_cause & eh_freeze_mask) {
		/*
		 * Note to self: ata_port_freeze() calls ata_port_abort()
		 */
		ata_port_freeze(ap);
	} else {
		abort = 1;
	}

	if (abort) {
		if (qc)
			ata_link_abort(qc->dev->link);
		else
			ata_port_abort(ap);
	}
}

static void mv_process_crpb_response(struct ata_port *ap,
		struct mv_crpb *response, unsigned int tag, int ncq_enabled)
{
	struct ata_queued_cmd *qc = ata_qc_from_tag(ap, tag);

	if (qc) {
		u8 ata_status;
		u16 edma_status = le16_to_cpu(response->flags);
		/*
		 * edma_status from a response queue entry:
		 *   LSB is from EDMA_ERR_IRQ_CAUSE_OFS (non-NCQ only).
		 *   MSB is saved ATA status from command completion.
		 */
		if (!ncq_enabled) {
			u8 err_cause = edma_status & 0xff & ~EDMA_ERR_DEV;
			if (err_cause) {
				/*
				 * Error will be seen/handled by mv_err_intr().
				 * So do nothing at all here.
				 */
				return;
			}
		}
		ata_status = edma_status >> CRPB_FLAG_STATUS_SHIFT;
		if (!ac_err_mask(ata_status))
			ata_qc_complete(qc);
		/* else: leave it for mv_err_intr() */
	} else {
		ata_port_printk(ap, KERN_ERR, "%s: no qc for tag=%d\n",
				__func__, tag);
	}
}

static void mv_process_crpb_entries(struct ata_port *ap, struct mv_port_priv *pp)
{
	void __iomem *port_mmio = mv_ap_base(ap);
	struct mv_host_priv *hpriv = ap->host->private_data;
	u32 in_index;
	bool work_done = false;
	int ncq_enabled = (pp->pp_flags & MV_PP_FLAG_NCQ_EN);

	/* Get the hardware queue position index */
	in_index = (readl(port_mmio + EDMA_RSP_Q_IN_PTR_OFS)
			>> EDMA_RSP_Q_PTR_SHIFT) & MV_MAX_Q_DEPTH_MASK;

	/* Process new responses from since the last time we looked */
	while (in_index != pp->resp_idx) {
		unsigned int tag;
		struct mv_crpb *response = &pp->crpb[pp->resp_idx];

		pp->resp_idx = (pp->resp_idx + 1) & MV_MAX_Q_DEPTH_MASK;

		if (IS_GEN_I(hpriv)) {
			/* 50xx: no NCQ, only one command active at a time */
			tag = ap->link.active_tag;
		} else {
			/* Gen II/IIE: get command tag from CRPB entry */
			tag = le16_to_cpu(response->id) & 0x1f;
		}
		mv_process_crpb_response(ap, response, tag, ncq_enabled);
		work_done = true;
	}

	/* Update the software queue position index in hardware */
	if (work_done)
		writelfl((pp->crpb_dma & EDMA_RSP_Q_BASE_LO_MASK) |
			 (pp->resp_idx << EDMA_RSP_Q_PTR_SHIFT),
			 port_mmio + EDMA_RSP_Q_OUT_PTR_OFS);
}

static void mv_port_intr(struct ata_port *ap, u32 port_cause)
{
	struct mv_port_priv *pp;
	int edma_was_enabled;

	if (!ap || (ap->flags & ATA_FLAG_DISABLED)) {
		mv_unexpected_intr(ap, 0);
		return;
	}
	/*
	 * Grab a snapshot of the EDMA_EN flag setting,
	 * so that we have a consistent view for this port,
	 * even if something we call of our routines changes it.
	 */
	pp = ap->private_data;
	edma_was_enabled = (pp->pp_flags & MV_PP_FLAG_EDMA_EN);
	/*
	 * Process completed CRPB response(s) before other events.
	 */
	if (edma_was_enabled && (port_cause & DONE_IRQ)) {
		mv_process_crpb_entries(ap, pp);
		if (pp->pp_flags & MV_PP_FLAG_DELAYED_EH)
			mv_handle_fbs_ncq_dev_err(ap);
	}
	/*
	 * Handle chip-reported errors, or continue on to handle PIO.
	 */
	if (unlikely(port_cause & ERR_IRQ)) {
		mv_err_intr(ap);
	} else if (!edma_was_enabled) {
		struct ata_queued_cmd *qc = mv_get_active_qc(ap);
		if (qc)
			ata_sff_host_intr(ap, qc);
		else
			mv_unexpected_intr(ap, edma_was_enabled);
	}
}

/**
 *      mv_host_intr - Handle all interrupts on the given host controller
 *      @host: host specific structure
 *      @main_irq_cause: Main interrupt cause register for the chip.
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static int mv_host_intr(struct ata_host *host, u32 main_irq_cause)
{
	struct mv_host_priv *hpriv = host->private_data;
	void __iomem *mmio = hpriv->base, *hc_mmio;
	unsigned int handled = 0, port;

	for (port = 0; port < hpriv->n_ports; port++) {
		struct ata_port *ap = host->ports[port];
		unsigned int p, shift, hardport, port_cause;

		MV_PORT_TO_SHIFT_AND_HARDPORT(port, shift, hardport);
		/*
		 * Each hc within the host has its own hc_irq_cause register,
		 * where the interrupting ports bits get ack'd.
		 */
		if (hardport == 0) {	/* first port on this hc ? */
			u32 hc_cause = (main_irq_cause >> shift) & HC0_IRQ_PEND;
			u32 port_mask, ack_irqs;
			/*
			 * Skip this entire hc if nothing pending for any ports
			 */
			if (!hc_cause) {
				port += MV_PORTS_PER_HC - 1;
				continue;
			}
			/*
			 * We don't need/want to read the hc_irq_cause register,
			 * because doing so hurts performance, and
			 * main_irq_cause already gives us everything we need.
			 *
			 * But we do have to *write* to the hc_irq_cause to ack
			 * the ports that we are handling this time through.
			 *
			 * This requires that we create a bitmap for those
			 * ports which interrupted us, and use that bitmap
			 * to ack (only) those ports via hc_irq_cause.
			 */
			ack_irqs = 0;
			for (p = 0; p < MV_PORTS_PER_HC; ++p) {
				if ((port + p) >= hpriv->n_ports)
					break;
				port_mask = (DONE_IRQ | ERR_IRQ) << (p * 2);
				if (hc_cause & port_mask)
					ack_irqs |= (DMA_IRQ | DEV_IRQ) << p;
			}
			hc_mmio = mv_hc_base_from_port(mmio, port);
			writelfl(~ack_irqs, hc_mmio + HC_IRQ_CAUSE_OFS);
			handled = 1;
		}
		/*
		 * Handle interrupts signalled for this port:
		 */
		port_cause = (main_irq_cause >> shift) & (DONE_IRQ | ERR_IRQ);
		if (port_cause)
			mv_port_intr(ap, port_cause);
	}
	return handled;
}

static int mv_pci_error(struct ata_host *host, void __iomem *mmio)
{
	struct mv_host_priv *hpriv = host->private_data;
	struct ata_port *ap;
	struct ata_queued_cmd *qc;
	struct ata_eh_info *ehi;
	unsigned int i, err_mask, printed = 0;
	u32 err_cause;

	err_cause = readl(mmio + hpriv->irq_cause_ofs);

	dev_printk(KERN_ERR, host->dev, "PCI ERROR; PCI IRQ cause=0x%08x\n",
		   err_cause);

	DPRINTK("All regs @ PCI error\n");
	mv_dump_all_regs(mmio, -1, to_pci_dev(host->dev));

	writelfl(0, mmio + hpriv->irq_cause_ofs);

	for (i = 0; i < host->n_ports; i++) {
		ap = host->ports[i];
		if (!ata_link_offline(&ap->link)) {
			ehi = &ap->link.eh_info;
			ata_ehi_clear_desc(ehi);
			if (!printed++)
				ata_ehi_push_desc(ehi,
					"PCI err cause 0x%08x", err_cause);
			err_mask = AC_ERR_HOST_BUS;
			ehi->action = ATA_EH_RESET;
			qc = ata_qc_from_tag(ap, ap->link.active_tag);
			if (qc)
				qc->err_mask |= err_mask;
			else
				ehi->err_mask |= err_mask;

			ata_port_freeze(ap);
		}
	}
	return 1;	/* handled */
}

/**
 *      mv_interrupt - Main interrupt event handler
 *      @irq: unused
 *      @dev_instance: private data; in this case the host structure
 *
 *      Read the read only register to determine if any host
 *      controllers have pending interrupts.  If so, call lower level
 *      routine to handle.  Also check for PCI errors which are only
 *      reported here.
 *
 *      LOCKING:
 *      This routine holds the host lock while processing pending
 *      interrupts.
 */
static irqreturn_t mv_interrupt(int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	struct mv_host_priv *hpriv = host->private_data;
	unsigned int handled = 0;
	u32 main_irq_cause, pending_irqs;

	spin_lock(&host->lock);
	main_irq_cause = readl(hpriv->main_irq_cause_addr);
	pending_irqs   = main_irq_cause & hpriv->main_irq_mask;
	/*
	 * Deal with cases where we either have nothing pending, or have read
	 * a bogus register value which can indicate HW removal or PCI fault.
	 */
	if (pending_irqs && main_irq_cause != 0xffffffffU) {
		if (unlikely((pending_irqs & PCI_ERR) && !IS_SOC(hpriv)))
			handled = mv_pci_error(host, hpriv->base);
		else
			handled = mv_host_intr(host, pending_irqs);
	}
	spin_unlock(&host->lock);
	return IRQ_RETVAL(handled);
}

static unsigned int mv5_scr_offset(unsigned int sc_reg_in)
{
	unsigned int ofs;

	switch (sc_reg_in) {
	case SCR_STATUS:
	case SCR_ERROR:
	case SCR_CONTROL:
		ofs = sc_reg_in * sizeof(u32);
		break;
	default:
		ofs = 0xffffffffU;
		break;
	}
	return ofs;
}

static int mv5_scr_read(struct ata_port *ap, unsigned int sc_reg_in, u32 *val)
{
	struct mv_host_priv *hpriv = ap->host->private_data;
	void __iomem *mmio = hpriv->base;
	void __iomem *addr = mv5_phy_base(mmio, ap->port_no);
	unsigned int ofs = mv5_scr_offset(sc_reg_in);

	if (ofs != 0xffffffffU) {
		*val = readl(addr + ofs);
		return 0;
	} else
		return -EINVAL;
}

static int mv5_scr_write(struct ata_port *ap, unsigned int sc_reg_in, u32 val)
{
	struct mv_host_priv *hpriv = ap->host->private_data;
	void __iomem *mmio = hpriv->base;
	void __iomem *addr = mv5_phy_base(mmio, ap->port_no);
	unsigned int ofs = mv5_scr_offset(sc_reg_in);

	if (ofs != 0xffffffffU) {
		writelfl(val, addr + ofs);
		return 0;
	} else
		return -EINVAL;
}

static void mv5_reset_bus(struct ata_host *host, void __iomem *mmio)
{
	struct pci_dev *pdev = to_pci_dev(host->dev);
	int early_5080;

	early_5080 = (pdev->device == 0x5080) && (pdev->revision == 0);

	if (!early_5080) {
		u32 tmp = readl(mmio + MV_PCI_EXP_ROM_BAR_CTL);
		tmp |= (1 << 0);
		writel(tmp, mmio + MV_PCI_EXP_ROM_BAR_CTL);
	}

	mv_reset_pci_bus(host, mmio);
}

static void mv5_reset_flash(struct mv_host_priv *hpriv, void __iomem *mmio)
{
	writel(0x0fcfffff, mmio + MV_FLASH_CTL_OFS);
}

static void mv5_read_preamp(struct mv_host_priv *hpriv, int idx,
			   void __iomem *mmio)
{
	void __iomem *phy_mmio = mv5_phy_base(mmio, idx);
	u32 tmp;

	tmp = readl(phy_mmio + MV5_PHY_MODE);

	hpriv->signal[idx].pre = tmp & 0x1800;	/* bits 12:11 */
	hpriv->signal[idx].amps = tmp & 0xe0;	/* bits 7:5 */
}

static void mv5_enable_leds(struct mv_host_priv *hpriv, void __iomem *mmio)
{
	u32 tmp;

	writel(0, mmio + MV_GPIO_PORT_CTL_OFS);

	/* FIXME: handle MV_HP_ERRATA_50XXB2 errata */

	tmp = readl(mmio + MV_PCI_EXP_ROM_BAR_CTL);
	tmp |= ~(1 << 0);
	writel(tmp, mmio + MV_PCI_EXP_ROM_BAR_CTL);
}

static void mv5_phy_errata(struct mv_host_priv *hpriv, void __iomem *mmio,
			   unsigned int port)
{
	void __iomem *phy_mmio = mv5_phy_base(mmio, port);
	const u32 mask = (1<<12) | (1<<11) | (1<<7) | (1<<6) | (1<<5);
	u32 tmp;
	int fix_apm_sq = (hpriv->hp_flags & MV_HP_ERRATA_50XXB0);

	if (fix_apm_sq) {
		tmp = readl(phy_mmio + MV5_LTMODE_OFS);
		tmp |= (1 << 19);
		writel(tmp, phy_mmio + MV5_LTMODE_OFS);

		tmp = readl(phy_mmio + MV5_PHY_CTL_OFS);
		tmp &= ~0x3;
		tmp |= 0x1;
		writel(tmp, phy_mmio + MV5_PHY_CTL_OFS);
	}

	tmp = readl(phy_mmio + MV5_PHY_MODE);
	tmp &= ~mask;
	tmp |= hpriv->signal[port].pre;
	tmp |= hpriv->signal[port].amps;
	writel(tmp, phy_mmio + MV5_PHY_MODE);
}


#undef ZERO
#define ZERO(reg) writel(0, port_mmio + (reg))
static void mv5_reset_hc_port(struct mv_host_priv *hpriv, void __iomem *mmio,
			     unsigned int port)
{
	void __iomem *port_mmio = mv_port_base(mmio, port);

	mv_reset_channel(hpriv, mmio, port);

	ZERO(0x028);	/* command */
	writel(0x11f, port_mmio + EDMA_CFG_OFS);
	ZERO(0x004);	/* timer */
	ZERO(0x008);	/* irq err cause */
	ZERO(0x00c);	/* irq err mask */
	ZERO(0x010);	/* rq bah */
	ZERO(0x014);	/* rq inp */
	ZERO(0x018);	/* rq outp */
	ZERO(0x01c);	/* respq bah */
	ZERO(0x024);	/* respq outp */
	ZERO(0x020);	/* respq inp */
	ZERO(0x02c);	/* test control */
	writel(0xbc, port_mmio + EDMA_IORDY_TMOUT_OFS);
}
#undef ZERO

#define ZERO(reg) writel(0, hc_mmio + (reg))
static void mv5_reset_one_hc(struct mv_host_priv *hpriv, void __iomem *mmio,
			unsigned int hc)
{
	void __iomem *hc_mmio = mv_hc_base(mmio, hc);
	u32 tmp;

	ZERO(0x00c);
	ZERO(0x010);
	ZERO(0x014);
	ZERO(0x018);

	tmp = readl(hc_mmio + 0x20);
	tmp &= 0x1c1c1c1c;
	tmp |= 0x03030303;
	writel(tmp, hc_mmio + 0x20);
}
#undef ZERO

static int mv5_reset_hc(struct mv_host_priv *hpriv, void __iomem *mmio,
			unsigned int n_hc)
{
	unsigned int hc, port;

	for (hc = 0; hc < n_hc; hc++) {
		for (port = 0; port < MV_PORTS_PER_HC; port++)
			mv5_reset_hc_port(hpriv, mmio,
					  (hc * MV_PORTS_PER_HC) + port);

		mv5_reset_one_hc(hpriv, mmio, hc);
	}

	return 0;
}

#undef ZERO
#define ZERO(reg) writel(0, mmio + (reg))
static void mv_reset_pci_bus(struct ata_host *host, void __iomem *mmio)
{
	struct mv_host_priv *hpriv = host->private_data;
	u32 tmp;

	tmp = readl(mmio + MV_PCI_MODE_OFS);
	tmp &= 0xff00ffff;
	writel(tmp, mmio + MV_PCI_MODE_OFS);

	ZERO(MV_PCI_DISC_TIMER);
	ZERO(MV_PCI_MSI_TRIGGER);
	writel(0x000100ff, mmio + MV_PCI_XBAR_TMOUT_OFS);
	ZERO(MV_PCI_SERR_MASK);
	ZERO(hpriv->irq_cause_ofs);
	ZERO(hpriv->irq_mask_ofs);
	ZERO(MV_PCI_ERR_LOW_ADDRESS);
	ZERO(MV_PCI_ERR_HIGH_ADDRESS);
	ZERO(MV_PCI_ERR_ATTRIBUTE);
	ZERO(MV_PCI_ERR_COMMAND);
}
#undef ZERO

static void mv6_reset_flash(struct mv_host_priv *hpriv, void __iomem *mmio)
{
	u32 tmp;

	mv5_reset_flash(hpriv, mmio);

	tmp = readl(mmio + MV_GPIO_PORT_CTL_OFS);
	tmp &= 0x3;
	tmp |= (1 << 5) | (1 << 6);
	writel(tmp, mmio + MV_GPIO_PORT_CTL_OFS);
}

/**
 *      mv6_reset_hc - Perform the 6xxx global soft reset
 *      @mmio: base address of the HBA
 *
 *      This routine only applies to 6xxx parts.
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static int mv6_reset_hc(struct mv_host_priv *hpriv, void __iomem *mmio,
			unsigned int n_hc)
{
	void __iomem *reg = mmio + PCI_MAIN_CMD_STS_OFS;
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
		if (PCI_MASTER_EMPTY & t)
			break;
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

static void mv6_read_preamp(struct mv_host_priv *hpriv, int idx,
			   void __iomem *mmio)
{
	void __iomem *port_mmio;
	u32 tmp;

	tmp = readl(mmio + MV_RESET_CFG_OFS);
	if ((tmp & (1 << 0)) == 0) {
		hpriv->signal[idx].amps = 0x7 << 8;
		hpriv->signal[idx].pre = 0x1 << 5;
		return;
	}

	port_mmio = mv_port_base(mmio, idx);
	tmp = readl(port_mmio + PHY_MODE2);

	hpriv->signal[idx].amps = tmp & 0x700;	/* bits 10:8 */
	hpriv->signal[idx].pre = tmp & 0xe0;	/* bits 7:5 */
}

static void mv6_enable_leds(struct mv_host_priv *hpriv, void __iomem *mmio)
{
	writel(0x00000060, mmio + MV_GPIO_PORT_CTL_OFS);
}

static void mv6_phy_errata(struct mv_host_priv *hpriv, void __iomem *mmio,
			   unsigned int port)
{
	void __iomem *port_mmio = mv_port_base(mmio, port);

	u32 hp_flags = hpriv->hp_flags;
	int fix_phy_mode2 =
		hp_flags & (MV_HP_ERRATA_60X1B2 | MV_HP_ERRATA_60X1C0);
	int fix_phy_mode4 =
		hp_flags & (MV_HP_ERRATA_60X1B2 | MV_HP_ERRATA_60X1C0);
	u32 m2, m3;

	if (fix_phy_mode2) {
		m2 = readl(port_mmio + PHY_MODE2);
		m2 &= ~(1 << 16);
		m2 |= (1 << 31);
		writel(m2, port_mmio + PHY_MODE2);

		udelay(200);

		m2 = readl(port_mmio + PHY_MODE2);
		m2 &= ~((1 << 16) | (1 << 31));
		writel(m2, port_mmio + PHY_MODE2);

		udelay(200);
	}

	/*
	 * Gen-II/IIe PHY_MODE3 errata RM#2:
	 * Achieves better receiver noise performance than the h/w default:
	 */
	m3 = readl(port_mmio + PHY_MODE3);
	m3 = (m3 & 0x1f) | (0x5555601 << 5);

	/* Guideline 88F5182 (GL# SATA-S11) */
	if (IS_SOC(hpriv))
		m3 &= ~0x1c;

	if (fix_phy_mode4) {
		u32 m4 = readl(port_mmio + PHY_MODE4);
		/*
		 * Enforce reserved-bit restrictions on GenIIe devices only.
		 * For earlier chipsets, force only the internal config field
		 *  (workaround for errata FEr SATA#10 part 1).
		 */
		if (IS_GEN_IIE(hpriv))
			m4 = (m4 & ~PHY_MODE4_RSVD_ZEROS) | PHY_MODE4_RSVD_ONES;
		else
			m4 = (m4 & ~PHY_MODE4_CFG_MASK) | PHY_MODE4_CFG_VALUE;
		writel(m4, port_mmio + PHY_MODE4);
	}
	/*
	 * Workaround for 60x1-B2 errata SATA#13:
	 * Any write to PHY_MODE4 (above) may corrupt PHY_MODE3,
	 * so we must always rewrite PHY_MODE3 after PHY_MODE4.
	 */
	writel(m3, port_mmio + PHY_MODE3);

	/* Revert values of pre-emphasis and signal amps to the saved ones */
	m2 = readl(port_mmio + PHY_MODE2);

	m2 &= ~MV_M2_PREAMP_MASK;
	m2 |= hpriv->signal[port].amps;
	m2 |= hpriv->signal[port].pre;
	m2 &= ~(1 << 16);

	/* according to mvSata 3.6.1, some IIE values are fixed */
	if (IS_GEN_IIE(hpriv)) {
		m2 &= ~0xC30FF01F;
		m2 |= 0x0000900F;
	}

	writel(m2, port_mmio + PHY_MODE2);
}

/* TODO: use the generic LED interface to configure the SATA Presence */
/* & Acitivy LEDs on the board */
static void mv_soc_enable_leds(struct mv_host_priv *hpriv,
				      void __iomem *mmio)
{
	return;
}

static void mv_soc_read_preamp(struct mv_host_priv *hpriv, int idx,
			   void __iomem *mmio)
{
	void __iomem *port_mmio;
	u32 tmp;

	port_mmio = mv_port_base(mmio, idx);
	tmp = readl(port_mmio + PHY_MODE2);

	hpriv->signal[idx].amps = tmp & 0x700;	/* bits 10:8 */
	hpriv->signal[idx].pre = tmp & 0xe0;	/* bits 7:5 */
}

#undef ZERO
#define ZERO(reg) writel(0, port_mmio + (reg))
static void mv_soc_reset_hc_port(struct mv_host_priv *hpriv,
					void __iomem *mmio, unsigned int port)
{
	void __iomem *port_mmio = mv_port_base(mmio, port);

	mv_reset_channel(hpriv, mmio, port);

	ZERO(0x028);		/* command */
	writel(0x101f, port_mmio + EDMA_CFG_OFS);
	ZERO(0x004);		/* timer */
	ZERO(0x008);		/* irq err cause */
	ZERO(0x00c);		/* irq err mask */
	ZERO(0x010);		/* rq bah */
	ZERO(0x014);		/* rq inp */
	ZERO(0x018);		/* rq outp */
	ZERO(0x01c);		/* respq bah */
	ZERO(0x024);		/* respq outp */
	ZERO(0x020);		/* respq inp */
	ZERO(0x02c);		/* test control */
	writel(0xbc, port_mmio + EDMA_IORDY_TMOUT_OFS);
}

#undef ZERO

#define ZERO(reg) writel(0, hc_mmio + (reg))
static void mv_soc_reset_one_hc(struct mv_host_priv *hpriv,
				       void __iomem *mmio)
{
	void __iomem *hc_mmio = mv_hc_base(mmio, 0);

	ZERO(0x00c);
	ZERO(0x010);
	ZERO(0x014);

}

#undef ZERO

static int mv_soc_reset_hc(struct mv_host_priv *hpriv,
				  void __iomem *mmio, unsigned int n_hc)
{
	unsigned int port;

	for (port = 0; port < hpriv->n_ports; port++)
		mv_soc_reset_hc_port(hpriv, mmio, port);

	mv_soc_reset_one_hc(hpriv, mmio);

	return 0;
}

static void mv_soc_reset_flash(struct mv_host_priv *hpriv,
				      void __iomem *mmio)
{
	return;
}

static void mv_soc_reset_bus(struct ata_host *host, void __iomem *mmio)
{
	return;
}

static void mv_setup_ifcfg(void __iomem *port_mmio, int want_gen2i)
{
	u32 ifcfg = readl(port_mmio + SATA_INTERFACE_CFG_OFS);

	ifcfg = (ifcfg & 0xf7f) | 0x9b1000;	/* from chip spec */
	if (want_gen2i)
		ifcfg |= (1 << 7);		/* enable gen2i speed */
	writelfl(ifcfg, port_mmio + SATA_INTERFACE_CFG_OFS);
}

static void mv_reset_channel(struct mv_host_priv *hpriv, void __iomem *mmio,
			     unsigned int port_no)
{
	void __iomem *port_mmio = mv_port_base(mmio, port_no);

	/*
	 * The datasheet warns against setting EDMA_RESET when EDMA is active
	 * (but doesn't say what the problem might be).  So we first try
	 * to disable the EDMA engine before doing the EDMA_RESET operation.
	 */
	mv_stop_edma_engine(port_mmio);
	writelfl(EDMA_RESET, port_mmio + EDMA_CMD_OFS);

	if (!IS_GEN_I(hpriv)) {
		/* Enable 3.0gb/s link speed: this survives EDMA_RESET */
		mv_setup_ifcfg(port_mmio, 1);
	}
	/*
	 * Strobing EDMA_RESET here causes a hard reset of the SATA transport,
	 * link, and physical layers.  It resets all SATA interface registers
	 * (except for SATA_INTERFACE_CFG), and issues a COMRESET to the dev.
	 */
	writelfl(EDMA_RESET, port_mmio + EDMA_CMD_OFS);
	udelay(25);	/* allow reset propagation */
	writelfl(0, port_mmio + EDMA_CMD_OFS);

	hpriv->ops->phy_errata(hpriv, mmio, port_no);

	if (IS_GEN_I(hpriv))
		mdelay(1);
}

static void mv_pmp_select(struct ata_port *ap, int pmp)
{
	if (sata_pmp_supported(ap)) {
		void __iomem *port_mmio = mv_ap_base(ap);
		u32 reg = readl(port_mmio + SATA_IFCTL_OFS);
		int old = reg & 0xf;

		if (old != pmp) {
			reg = (reg & ~0xf) | pmp;
			writelfl(reg, port_mmio + SATA_IFCTL_OFS);
		}
	}
}

static int mv_pmp_hardreset(struct ata_link *link, unsigned int *class,
				unsigned long deadline)
{
	mv_pmp_select(link->ap, sata_srst_pmp(link));
	return sata_std_hardreset(link, class, deadline);
}

static int mv_softreset(struct ata_link *link, unsigned int *class,
				unsigned long deadline)
{
	mv_pmp_select(link->ap, sata_srst_pmp(link));
	return ata_sff_softreset(link, class, deadline);
}

static int mv_hardreset(struct ata_link *link, unsigned int *class,
			unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct mv_host_priv *hpriv = ap->host->private_data;
	struct mv_port_priv *pp = ap->private_data;
	void __iomem *mmio = hpriv->base;
	int rc, attempts = 0, extra = 0;
	u32 sstatus;
	bool online;

	mv_reset_channel(hpriv, mmio, ap->port_no);
	pp->pp_flags &= ~MV_PP_FLAG_EDMA_EN;

	/* Workaround for errata FEr SATA#10 (part 2) */
	do {
		const unsigned long *timing =
				sata_ehc_deb_timing(&link->eh_context);

		rc = sata_link_hardreset(link, timing, deadline + extra,
					 &online, NULL);
		rc = online ? -EAGAIN : rc;
		if (rc)
			return rc;
		sata_scr_read(link, SCR_STATUS, &sstatus);
		if (!IS_GEN_I(hpriv) && ++attempts >= 5 && sstatus == 0x121) {
			/* Force 1.5gb/s link speed and try again */
			mv_setup_ifcfg(mv_ap_base(ap), 0);
			if (time_after(jiffies + HZ, deadline))
				extra = HZ; /* only extend it once, max */
		}
	} while (sstatus != 0x0 && sstatus != 0x113 && sstatus != 0x123);

	return rc;
}

static void mv_eh_freeze(struct ata_port *ap)
{
	mv_stop_edma(ap);
	mv_enable_port_irqs(ap, 0);
}

static void mv_eh_thaw(struct ata_port *ap)
{
	struct mv_host_priv *hpriv = ap->host->private_data;
	unsigned int port = ap->port_no;
	unsigned int hardport = mv_hardport_from_port(port);
	void __iomem *hc_mmio = mv_hc_base_from_port(hpriv->base, port);
	void __iomem *port_mmio = mv_ap_base(ap);
	u32 hc_irq_cause;

	/* clear EDMA errors on this port */
	writel(0, port_mmio + EDMA_ERR_IRQ_CAUSE_OFS);

	/* clear pending irq events */
	hc_irq_cause = readl(hc_mmio + HC_IRQ_CAUSE_OFS);
	hc_irq_cause &= ~((DEV_IRQ | DMA_IRQ) << hardport);
	writelfl(hc_irq_cause, hc_mmio + HC_IRQ_CAUSE_OFS);

	mv_enable_port_irqs(ap, ERR_IRQ);
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
	void __iomem *shd_base = port_mmio + SHD_BLK_OFS;
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
	port->cmd_addr = port->bmdma_addr = port->scr_addr = NULL;

	/* Clear any currently outstanding port interrupt conditions */
	serr_ofs = mv_scr_offset(SCR_ERROR);
	writelfl(readl(port_mmio + serr_ofs), port_mmio + serr_ofs);
	writelfl(0, port_mmio + EDMA_ERR_IRQ_CAUSE_OFS);

	/* unmask all non-transient EDMA error interrupts */
	writelfl(~EDMA_ERR_IRQ_TRANSIENT, port_mmio + EDMA_ERR_IRQ_MASK_OFS);

	VPRINTK("EDMA cfg=0x%08x EDMA IRQ err cause/mask=0x%08x/0x%08x\n",
		readl(port_mmio + EDMA_CFG_OFS),
		readl(port_mmio + EDMA_ERR_IRQ_CAUSE_OFS),
		readl(port_mmio + EDMA_ERR_IRQ_MASK_OFS));
}

static unsigned int mv_in_pcix_mode(struct ata_host *host)
{
	struct mv_host_priv *hpriv = host->private_data;
	void __iomem *mmio = hpriv->base;
	u32 reg;

	if (IS_SOC(hpriv) || !IS_PCIE(hpriv))
		return 0;	/* not PCI-X capable */
	reg = readl(mmio + MV_PCI_MODE_OFS);
	if ((reg & MV_PCI_MODE_MASK) == 0)
		return 0;	/* conventional PCI mode */
	return 1;	/* chip is in PCI-X mode */
}

static int mv_pci_cut_through_okay(struct ata_host *host)
{
	struct mv_host_priv *hpriv = host->private_data;
	void __iomem *mmio = hpriv->base;
	u32 reg;

	if (!mv_in_pcix_mode(host)) {
		reg = readl(mmio + PCI_COMMAND_OFS);
		if (reg & PCI_COMMAND_MRDTRIG)
			return 0; /* not okay */
	}
	return 1; /* okay */
}

static int mv_chip_id(struct ata_host *host, unsigned int board_idx)
{
	struct pci_dev *pdev = to_pci_dev(host->dev);
	struct mv_host_priv *hpriv = host->private_data;
	u32 hp_flags = hpriv->hp_flags;

	switch (board_idx) {
	case chip_5080:
		hpriv->ops = &mv5xxx_ops;
		hp_flags |= MV_HP_GEN_I;

		switch (pdev->revision) {
		case 0x1:
			hp_flags |= MV_HP_ERRATA_50XXB0;
			break;
		case 0x3:
			hp_flags |= MV_HP_ERRATA_50XXB2;
			break;
		default:
			dev_printk(KERN_WARNING, &pdev->dev,
			   "Applying 50XXB2 workarounds to unknown rev\n");
			hp_flags |= MV_HP_ERRATA_50XXB2;
			break;
		}
		break;

	case chip_504x:
	case chip_508x:
		hpriv->ops = &mv5xxx_ops;
		hp_flags |= MV_HP_GEN_I;

		switch (pdev->revision) {
		case 0x0:
			hp_flags |= MV_HP_ERRATA_50XXB0;
			break;
		case 0x3:
			hp_flags |= MV_HP_ERRATA_50XXB2;
			break;
		default:
			dev_printk(KERN_WARNING, &pdev->dev,
			   "Applying B2 workarounds to unknown rev\n");
			hp_flags |= MV_HP_ERRATA_50XXB2;
			break;
		}
		break;

	case chip_604x:
	case chip_608x:
		hpriv->ops = &mv6xxx_ops;
		hp_flags |= MV_HP_GEN_II;

		switch (pdev->revision) {
		case 0x7:
			hp_flags |= MV_HP_ERRATA_60X1B2;
			break;
		case 0x9:
			hp_flags |= MV_HP_ERRATA_60X1C0;
			break;
		default:
			dev_printk(KERN_WARNING, &pdev->dev,
				   "Applying B2 workarounds to unknown rev\n");
			hp_flags |= MV_HP_ERRATA_60X1B2;
			break;
		}
		break;

	case chip_7042:
		hp_flags |= MV_HP_PCIE | MV_HP_CUT_THROUGH;
		if (pdev->vendor == PCI_VENDOR_ID_TTI &&
		    (pdev->device == 0x2300 || pdev->device == 0x2310))
		{
			/*
			 * Highpoint RocketRAID PCIe 23xx series cards:
			 *
			 * Unconfigured drives are treated as "Legacy"
			 * by the BIOS, and it overwrites sector 8 with
			 * a "Lgcy" metadata block prior to Linux boot.
			 *
			 * Configured drives (RAID or JBOD) leave sector 8
			 * alone, but instead overwrite a high numbered
			 * sector for the RAID metadata.  This sector can
			 * be determined exactly, by truncating the physical
			 * drive capacity to a nice even GB value.
			 *
			 * RAID metadata is at: (dev->n_sectors & ~0xfffff)
			 *
			 * Warn the user, lest they think we're just buggy.
			 */
			printk(KERN_WARNING DRV_NAME ": Highpoint RocketRAID"
				" BIOS CORRUPTS DATA on all attached drives,"
				" regardless of if/how they are configured."
				" BEWARE!\n");
			printk(KERN_WARNING DRV_NAME ": For data safety, do not"
				" use sectors 8-9 on \"Legacy\" drives,"
				" and avoid the final two gigabytes on"
				" all RocketRAID BIOS initialized drives.\n");
		}
		/* drop through */
	case chip_6042:
		hpriv->ops = &mv6xxx_ops;
		hp_flags |= MV_HP_GEN_IIE;
		if (board_idx == chip_6042 && mv_pci_cut_through_okay(host))
			hp_flags |= MV_HP_CUT_THROUGH;

		switch (pdev->revision) {
		case 0x2: /* Rev.B0: the first/only public release */
			hp_flags |= MV_HP_ERRATA_60X1C0;
			break;
		default:
			dev_printk(KERN_WARNING, &pdev->dev,
			   "Applying 60X1C0 workarounds to unknown rev\n");
			hp_flags |= MV_HP_ERRATA_60X1C0;
			break;
		}
		break;
	case chip_soc:
		hpriv->ops = &mv_soc_ops;
		hp_flags |= MV_HP_FLAG_SOC | MV_HP_GEN_IIE |
			MV_HP_ERRATA_60X1C0;
		break;

	default:
		dev_printk(KERN_ERR, host->dev,
			   "BUG: invalid board index %u\n", board_idx);
		return 1;
	}

	hpriv->hp_flags = hp_flags;
	if (hp_flags & MV_HP_PCIE) {
		hpriv->irq_cause_ofs	= PCIE_IRQ_CAUSE_OFS;
		hpriv->irq_mask_ofs	= PCIE_IRQ_MASK_OFS;
		hpriv->unmask_all_irqs	= PCIE_UNMASK_ALL_IRQS;
	} else {
		hpriv->irq_cause_ofs	= PCI_IRQ_CAUSE_OFS;
		hpriv->irq_mask_ofs	= PCI_IRQ_MASK_OFS;
		hpriv->unmask_all_irqs	= PCI_UNMASK_ALL_IRQS;
	}

	return 0;
}

/**
 *      mv_init_host - Perform some early initialization of the host.
 *	@host: ATA host to initialize
 *      @board_idx: controller index
 *
 *      If possible, do an early global reset of the host.  Then do
 *      our port init and clear/unmask all/relevant host interrupts.
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static int mv_init_host(struct ata_host *host, unsigned int board_idx)
{
	int rc = 0, n_hc, port, hc;
	struct mv_host_priv *hpriv = host->private_data;
	void __iomem *mmio = hpriv->base;

	rc = mv_chip_id(host, board_idx);
	if (rc)
		goto done;

	if (IS_SOC(hpriv)) {
		hpriv->main_irq_cause_addr = mmio + SOC_HC_MAIN_IRQ_CAUSE_OFS;
		hpriv->main_irq_mask_addr  = mmio + SOC_HC_MAIN_IRQ_MASK_OFS;
	} else {
		hpriv->main_irq_cause_addr = mmio + PCI_HC_MAIN_IRQ_CAUSE_OFS;
		hpriv->main_irq_mask_addr  = mmio + PCI_HC_MAIN_IRQ_MASK_OFS;
	}

	/* global interrupt mask: 0 == mask everything */
	mv_set_main_irq_mask(host, ~0, 0);

	n_hc = mv_get_hc_count(host->ports[0]->flags);

	for (port = 0; port < host->n_ports; port++)
		hpriv->ops->read_preamp(hpriv, port, mmio);

	rc = hpriv->ops->reset_hc(hpriv, mmio, n_hc);
	if (rc)
		goto done;

	hpriv->ops->reset_flash(hpriv, mmio);
	hpriv->ops->reset_bus(host, mmio);
	hpriv->ops->enable_leds(hpriv, mmio);

	for (port = 0; port < host->n_ports; port++) {
		struct ata_port *ap = host->ports[port];
		void __iomem *port_mmio = mv_port_base(mmio, port);

		mv_port_init(&ap->ioaddr, port_mmio);

#ifdef CONFIG_PCI
		if (!IS_SOC(hpriv)) {
			unsigned int offset = port_mmio - mmio;
			ata_port_pbar_desc(ap, MV_PRIMARY_BAR, -1, "mmio");
			ata_port_pbar_desc(ap, MV_PRIMARY_BAR, offset, "port");
		}
#endif
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

	if (!IS_SOC(hpriv)) {
		/* Clear any currently outstanding host interrupt conditions */
		writelfl(0, mmio + hpriv->irq_cause_ofs);

		/* and unmask interrupt generation for host regs */
		writelfl(hpriv->unmask_all_irqs, mmio + hpriv->irq_mask_ofs);

		/*
		 * enable only global host interrupts for now.
		 * The per-port interrupts get done later as ports are set up.
		 */
		mv_set_main_irq_mask(host, 0, PCI_ERR);
	}
done:
	return rc;
}

static int mv_create_dma_pools(struct mv_host_priv *hpriv, struct device *dev)
{
	hpriv->crqb_pool   = dmam_pool_create("crqb_q", dev, MV_CRQB_Q_SZ,
							     MV_CRQB_Q_SZ, 0);
	if (!hpriv->crqb_pool)
		return -ENOMEM;

	hpriv->crpb_pool   = dmam_pool_create("crpb_q", dev, MV_CRPB_Q_SZ,
							     MV_CRPB_Q_SZ, 0);
	if (!hpriv->crpb_pool)
		return -ENOMEM;

	hpriv->sg_tbl_pool = dmam_pool_create("sg_tbl", dev, MV_SG_TBL_SZ,
							     MV_SG_TBL_SZ, 0);
	if (!hpriv->sg_tbl_pool)
		return -ENOMEM;

	return 0;
}

static void mv_conf_mbus_windows(struct mv_host_priv *hpriv,
				 struct mbus_dram_target_info *dram)
{
	int i;

	for (i = 0; i < 4; i++) {
		writel(0, hpriv->base + WINDOW_CTRL(i));
		writel(0, hpriv->base + WINDOW_BASE(i));
	}

	for (i = 0; i < dram->num_cs; i++) {
		struct mbus_dram_window *cs = dram->cs + i;

		writel(((cs->size - 1) & 0xffff0000) |
			(cs->mbus_attr << 8) |
			(dram->mbus_dram_target_id << 4) | 1,
			hpriv->base + WINDOW_CTRL(i));
		writel(cs->base, hpriv->base + WINDOW_BASE(i));
	}
}

/**
 *      mv_platform_probe - handle a positive probe of an soc Marvell
 *      host
 *      @pdev: platform device found
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static int mv_platform_probe(struct platform_device *pdev)
{
	static int printed_version;
	const struct mv_sata_platform_data *mv_platform_data;
	const struct ata_port_info *ppi[] =
	    { &mv_port_info[chip_soc], NULL };
	struct ata_host *host;
	struct mv_host_priv *hpriv;
	struct resource *res;
	int n_ports, rc;

	if (!printed_version++)
		dev_printk(KERN_INFO, &pdev->dev, "version " DRV_VERSION "\n");

	/*
	 * Simple resource validation ..
	 */
	if (unlikely(pdev->num_resources != 2)) {
		dev_err(&pdev->dev, "invalid number of resources\n");
		return -EINVAL;
	}

	/*
	 * Get the register base first
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -EINVAL;

	/* allocate host */
	mv_platform_data = pdev->dev.platform_data;
	n_ports = mv_platform_data->n_ports;

	host = ata_host_alloc_pinfo(&pdev->dev, ppi, n_ports);
	hpriv = devm_kzalloc(&pdev->dev, sizeof(*hpriv), GFP_KERNEL);

	if (!host || !hpriv)
		return -ENOMEM;
	host->private_data = hpriv;
	hpriv->n_ports = n_ports;

	host->iomap = NULL;
	hpriv->base = devm_ioremap(&pdev->dev, res->start,
				   res->end - res->start + 1);
	hpriv->base -= MV_SATAHC0_REG_BASE;

	/*
	 * (Re-)program MBUS remapping windows if we are asked to.
	 */
	if (mv_platform_data->dram != NULL)
		mv_conf_mbus_windows(hpriv, mv_platform_data->dram);

	rc = mv_create_dma_pools(hpriv, &pdev->dev);
	if (rc)
		return rc;

	/* initialize adapter */
	rc = mv_init_host(host, chip_soc);
	if (rc)
		return rc;

	dev_printk(KERN_INFO, &pdev->dev,
		   "slots %u ports %d\n", (unsigned)MV_MAX_Q_DEPTH,
		   host->n_ports);

	return ata_host_activate(host, platform_get_irq(pdev, 0), mv_interrupt,
				 IRQF_SHARED, &mv6_sht);
}

/*
 *
 *      mv_platform_remove    -       unplug a platform interface
 *      @pdev: platform device
 *
 *      A platform bus SATA device has been unplugged. Perform the needed
 *      cleanup. Also called on module unload for any active devices.
 */
static int __devexit mv_platform_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ata_host *host = dev_get_drvdata(dev);

	ata_host_detach(host);
	return 0;
}

static struct platform_driver mv_platform_driver = {
	.probe			= mv_platform_probe,
	.remove			= __devexit_p(mv_platform_remove),
	.driver			= {
				   .name = DRV_NAME,
				   .owner = THIS_MODULE,
				  },
};


#ifdef CONFIG_PCI
static int mv_pci_init_one(struct pci_dev *pdev,
			   const struct pci_device_id *ent);


static struct pci_driver mv_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= mv_pci_tbl,
	.probe			= mv_pci_init_one,
	.remove			= ata_pci_remove_one,
};

/*
 * module options
 */
static int msi;	      /* Use PCI msi; either zero (off, default) or non-zero */


/* move to PCI layer or libata core? */
static int pci_go_64(struct pci_dev *pdev)
{
	int rc;

	if (!pci_set_dma_mask(pdev, DMA_64BIT_MASK)) {
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

	return rc;
}

/**
 *      mv_print_info - Dump key info to kernel log for perusal.
 *      @host: ATA host to print info about
 *
 *      FIXME: complete this.
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static void mv_print_info(struct ata_host *host)
{
	struct pci_dev *pdev = to_pci_dev(host->dev);
	struct mv_host_priv *hpriv = host->private_data;
	u8 scc;
	const char *scc_s, *gen;

	/* Use this to determine the HW stepping of the chip so we know
	 * what errata to workaround
	 */
	pci_read_config_byte(pdev, PCI_CLASS_DEVICE, &scc);
	if (scc == 0)
		scc_s = "SCSI";
	else if (scc == 0x01)
		scc_s = "RAID";
	else
		scc_s = "?";

	if (IS_GEN_I(hpriv))
		gen = "I";
	else if (IS_GEN_II(hpriv))
		gen = "II";
	else if (IS_GEN_IIE(hpriv))
		gen = "IIE";
	else
		gen = "?";

	dev_printk(KERN_INFO, &pdev->dev,
	       "Gen-%s %u slots %u ports %s mode IRQ via %s\n",
	       gen, (unsigned)MV_MAX_Q_DEPTH, host->n_ports,
	       scc_s, (MV_HP_FLAG_MSI & hpriv->hp_flags) ? "MSI" : "INTx");
}

/**
 *      mv_pci_init_one - handle a positive probe of a PCI Marvell host
 *      @pdev: PCI device found
 *      @ent: PCI device ID entry for the matched host
 *
 *      LOCKING:
 *      Inherited from caller.
 */
static int mv_pci_init_one(struct pci_dev *pdev,
			   const struct pci_device_id *ent)
{
	static int printed_version;
	unsigned int board_idx = (unsigned int)ent->driver_data;
	const struct ata_port_info *ppi[] = { &mv_port_info[board_idx], NULL };
	struct ata_host *host;
	struct mv_host_priv *hpriv;
	int n_ports, rc;

	if (!printed_version++)
		dev_printk(KERN_INFO, &pdev->dev, "version " DRV_VERSION "\n");

	/* allocate host */
	n_ports = mv_get_hc_count(ppi[0]->flags) * MV_PORTS_PER_HC;

	host = ata_host_alloc_pinfo(&pdev->dev, ppi, n_ports);
	hpriv = devm_kzalloc(&pdev->dev, sizeof(*hpriv), GFP_KERNEL);
	if (!host || !hpriv)
		return -ENOMEM;
	host->private_data = hpriv;
	hpriv->n_ports = n_ports;

	/* acquire resources */
	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	rc = pcim_iomap_regions(pdev, 1 << MV_PRIMARY_BAR, DRV_NAME);
	if (rc == -EBUSY)
		pcim_pin_device(pdev);
	if (rc)
		return rc;
	host->iomap = pcim_iomap_table(pdev);
	hpriv->base = host->iomap[MV_PRIMARY_BAR];

	rc = pci_go_64(pdev);
	if (rc)
		return rc;

	rc = mv_create_dma_pools(hpriv, &pdev->dev);
	if (rc)
		return rc;

	/* initialize adapter */
	rc = mv_init_host(host, board_idx);
	if (rc)
		return rc;

	/* Enable interrupts */
	if (msi && pci_enable_msi(pdev))
		pci_intx(pdev, 1);

	mv_dump_pci_cfg(pdev, 0x68);
	mv_print_info(host);

	pci_set_master(pdev);
	pci_try_set_mwi(pdev);
	return ata_host_activate(host, pdev->irq, mv_interrupt, IRQF_SHARED,
				 IS_GEN_I(hpriv) ? &mv5_sht : &mv6_sht);
}
#endif

static int mv_platform_probe(struct platform_device *pdev);
static int __devexit mv_platform_remove(struct platform_device *pdev);

static int __init mv_init(void)
{
	int rc = -ENODEV;
#ifdef CONFIG_PCI
	rc = pci_register_driver(&mv_pci_driver);
	if (rc < 0)
		return rc;
#endif
	rc = platform_driver_register(&mv_platform_driver);

#ifdef CONFIG_PCI
	if (rc < 0)
		pci_unregister_driver(&mv_pci_driver);
#endif
	return rc;
}

static void __exit mv_exit(void)
{
#ifdef CONFIG_PCI
	pci_unregister_driver(&mv_pci_driver);
#endif
	platform_driver_unregister(&mv_platform_driver);
}

MODULE_AUTHOR("Brett Russ");
MODULE_DESCRIPTION("SCSI low-level driver for Marvell SATA controllers");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, mv_pci_tbl);
MODULE_VERSION(DRV_VERSION);
MODULE_ALIAS("platform:" DRV_NAME);

#ifdef CONFIG_PCI
module_param(msi, int, 0444);
MODULE_PARM_DESC(msi, "Enable use of PCI MSI (0=off, 1=on)");
#endif

module_init(mv_init);
module_exit(mv_exit);
