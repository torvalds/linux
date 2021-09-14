/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PCIe driver for Renesas R-Car SoCs
 *  Copyright (C) 2014-2020 Renesas Electronics Europe Ltd
 *
 * Author: Phil Edworthy <phil.edworthy@renesas.com>
 */

#ifndef _PCIE_RCAR_H
#define _PCIE_RCAR_H

#define PCIECAR			0x000010
#define PCIECCTLR		0x000018
#define  CONFIG_SEND_ENABLE	BIT(31)
#define  TYPE0			(0 << 8)
#define  TYPE1			BIT(8)
#define PCIECDR			0x000020
#define PCIEMSR			0x000028
#define PCIEINTXR		0x000400
#define  ASTINTX		BIT(16)
#define PCIEPHYSR		0x0007f0
#define  PHYRDY			BIT(0)
#define PCIEMSITXR		0x000840

/* Transfer control */
#define PCIETCTLR		0x02000
#define  DL_DOWN		BIT(3)
#define  CFINIT			BIT(0)
#define PCIETSTR		0x02004
#define  DATA_LINK_ACTIVE	BIT(0)
#define PCIEERRFR		0x02020
#define  UNSUPPORTED_REQUEST	BIT(4)
#define PCIEMSIFR		0x02044
#define PCIEMSIALR		0x02048
#define  MSIFE			BIT(0)
#define PCIEMSIAUR		0x0204c
#define PCIEMSIIER		0x02050

/* root port address */
#define PCIEPRAR(x)		(0x02080 + ((x) * 0x4))

/* local address reg & mask */
#define PCIELAR(x)		(0x02200 + ((x) * 0x20))
#define PCIELAMR(x)		(0x02208 + ((x) * 0x20))
#define  LAM_PREFETCH		BIT(3)
#define  LAM_64BIT		BIT(2)
#define  LAR_ENABLE		BIT(1)

/* PCIe address reg & mask */
#define PCIEPALR(x)		(0x03400 + ((x) * 0x20))
#define PCIEPAUR(x)		(0x03404 + ((x) * 0x20))
#define PCIEPAMR(x)		(0x03408 + ((x) * 0x20))
#define PCIEPTCTLR(x)		(0x0340c + ((x) * 0x20))
#define  PAR_ENABLE		BIT(31)
#define  IO_SPACE		BIT(8)

/* Configuration */
#define PCICONF(x)		(0x010000 + ((x) * 0x4))
#define  INTDIS			BIT(10)
#define PMCAP(x)		(0x010040 + ((x) * 0x4))
#define MSICAP(x)		(0x010050 + ((x) * 0x4))
#define  MSICAP0_MSIE		BIT(16)
#define  MSICAP0_MMESCAP_OFFSET	17
#define  MSICAP0_MMESE_OFFSET	20
#define  MSICAP0_MMESE_MASK	GENMASK(22, 20)
#define EXPCAP(x)		(0x010070 + ((x) * 0x4))
#define VCCAP(x)		(0x010100 + ((x) * 0x4))

/* link layer */
#define IDSETR0			0x011000
#define IDSETR1			0x011004
#define SUBIDSETR		0x011024
#define TLCTLR			0x011048
#define MACSR			0x011054
#define  SPCHGFIN		BIT(4)
#define  SPCHGFAIL		BIT(6)
#define  SPCHGSUC		BIT(7)
#define  LINK_SPEED		(0xf << 16)
#define  LINK_SPEED_2_5GTS	(1 << 16)
#define  LINK_SPEED_5_0GTS	(2 << 16)
#define MACCTLR			0x011058
#define  MACCTLR_NFTS_MASK	GENMASK(23, 16)	/* The name is from SH7786 */
#define  SPEED_CHANGE		BIT(24)
#define  SCRAMBLE_DISABLE	BIT(27)
#define  LTSMDIS		BIT(31)
#define  MACCTLR_INIT_VAL	(LTSMDIS | MACCTLR_NFTS_MASK)
#define PMSR			0x01105c
#define  L1FAEG			BIT(31)
#define  PMEL1RX		BIT(23)
#define  PMSTATE		GENMASK(18, 16)
#define  PMSTATE_L1		(3 << 16)
#define PMCTLR			0x011060
#define  L1IATN			BIT(31)

#define MACS2R			0x011078
#define MACCGSPSETR		0x011084
#define  SPCNGRSN		BIT(31)

/* R-Car H1 PHY */
#define H1_PCIEPHYADRR		0x04000c
#define  WRITE_CMD		BIT(16)
#define  PHY_ACK		BIT(24)
#define  RATE_POS		12
#define  LANE_POS		8
#define  ADR_POS		0
#define H1_PCIEPHYDOUTR		0x040014

/* R-Car Gen2 PHY */
#define GEN2_PCIEPHYADDR	0x780
#define GEN2_PCIEPHYDATA	0x784
#define GEN2_PCIEPHYCTRL	0x78c

#define INT_PCI_MSI_NR		32

#define RCONF(x)		(PCICONF(0) + (x))
#define RPMCAP(x)		(PMCAP(0) + (x))
#define REXPCAP(x)		(EXPCAP(0) + (x))
#define RVCCAP(x)		(VCCAP(0) + (x))

#define PCIE_CONF_BUS(b)	(((b) & 0xff) << 24)
#define PCIE_CONF_DEV(d)	(((d) & 0x1f) << 19)
#define PCIE_CONF_FUNC(f)	(((f) & 0x7) << 16)

#define RCAR_PCI_MAX_RESOURCES	4
#define MAX_NR_INBOUND_MAPS	6

struct rcar_pcie {
	struct device		*dev;
	void __iomem		*base;
};

enum {
	RCAR_PCI_ACCESS_READ,
	RCAR_PCI_ACCESS_WRITE,
};

void rcar_pci_write_reg(struct rcar_pcie *pcie, u32 val, unsigned int reg);
u32 rcar_pci_read_reg(struct rcar_pcie *pcie, unsigned int reg);
void rcar_rmw32(struct rcar_pcie *pcie, int where, u32 mask, u32 data);
int rcar_pcie_wait_for_phyrdy(struct rcar_pcie *pcie);
int rcar_pcie_wait_for_dl(struct rcar_pcie *pcie);
void rcar_pcie_set_outbound(struct rcar_pcie *pcie, int win,
			    struct resource_entry *window);
void rcar_pcie_set_inbound(struct rcar_pcie *pcie, u64 cpu_addr,
			   u64 pci_addr, u64 flags, int idx, bool host);

#endif
