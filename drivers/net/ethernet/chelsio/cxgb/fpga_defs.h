/* $Date: 2005/03/07 23:59:05 $ $RCSfile: fpga_defs.h,v $ $Revision: 1.4 $ */

/*
 * FPGA specific definitions
 */

#ifndef __CHELSIO_FPGA_DEFS_H__
#define __CHELSIO_FPGA_DEFS_H__

#define FPGA_PCIX_ADDR_VERSION               0xA08
#define FPGA_PCIX_ADDR_STAT                  0xA0C

/* FPGA master interrupt Cause/Enable bits */
#define FPGA_PCIX_INTERRUPT_SGE_ERROR        0x1
#define FPGA_PCIX_INTERRUPT_SGE_DATA         0x2
#define FPGA_PCIX_INTERRUPT_TP               0x4
#define FPGA_PCIX_INTERRUPT_MC3              0x8
#define FPGA_PCIX_INTERRUPT_GMAC             0x10
#define FPGA_PCIX_INTERRUPT_PCIX             0x20

/* TP interrupt register addresses */
#define FPGA_TP_ADDR_INTERRUPT_ENABLE        0xA10
#define FPGA_TP_ADDR_INTERRUPT_CAUSE         0xA14
#define FPGA_TP_ADDR_VERSION                 0xA18

/* TP interrupt Cause/Enable bits */
#define FPGA_TP_INTERRUPT_MC4                0x1
#define FPGA_TP_INTERRUPT_MC5                0x2

/*
 * PM interrupt register addresses
 */
#define FPGA_MC3_REG_INTRENABLE              0xA20
#define FPGA_MC3_REG_INTRCAUSE               0xA24
#define FPGA_MC3_REG_VERSION                 0xA28

/*
 * GMAC interrupt register addresses
 */
#define FPGA_GMAC_ADDR_INTERRUPT_ENABLE      0xA30
#define FPGA_GMAC_ADDR_INTERRUPT_CAUSE       0xA34
#define FPGA_GMAC_ADDR_VERSION               0xA38

/* GMAC Cause/Enable bits */
#define FPGA_GMAC_INTERRUPT_PORT0            0x1
#define FPGA_GMAC_INTERRUPT_PORT1            0x2
#define FPGA_GMAC_INTERRUPT_PORT2            0x4
#define FPGA_GMAC_INTERRUPT_PORT3            0x8

/* MI0 registers */
#define A_MI0_CLK 0xb00

#define S_MI0_CLK_DIV    0
#define M_MI0_CLK_DIV    0xff
#define V_MI0_CLK_DIV(x) ((x) << S_MI0_CLK_DIV)
#define G_MI0_CLK_DIV(x) (((x) >> S_MI0_CLK_DIV) & M_MI0_CLK_DIV)

#define S_MI0_CLK_CNT    8
#define M_MI0_CLK_CNT    0xff
#define V_MI0_CLK_CNT(x) ((x) << S_MI0_CLK_CNT)
#define G_MI0_CLK_CNT(x) (((x) >> S_MI0_CLK_CNT) & M_MI0_CLK_CNT)

#define A_MI0_CSR 0xb04

#define S_MI0_CSR_POLL    0
#define V_MI0_CSR_POLL(x) ((x) << S_MI0_CSR_POLL)
#define F_MI0_CSR_POLL    V_MI0_CSR_POLL(1U)

#define S_MI0_PREAMBLE    1
#define V_MI0_PREAMBLE(x) ((x) << S_MI0_PREAMBLE)
#define F_MI0_PREAMBLE    V_MI0_PREAMBLE(1U)

#define S_MI0_INTR_ENABLE    2
#define V_MI0_INTR_ENABLE(x) ((x) << S_MI0_INTR_ENABLE)
#define F_MI0_INTR_ENABLE    V_MI0_INTR_ENABLE(1U)

#define S_MI0_BUSY    3
#define V_MI0_BUSY(x) ((x) << S_MI0_BUSY)
#define F_MI0_BUSY    V_MI0_BUSY(1U)

#define S_MI0_MDIO    4
#define V_MI0_MDIO(x) ((x) << S_MI0_MDIO)
#define F_MI0_MDIO    V_MI0_MDIO(1U)

#define A_MI0_ADDR 0xb08

#define S_MI0_PHY_REG_ADDR    0
#define M_MI0_PHY_REG_ADDR    0x1f
#define V_MI0_PHY_REG_ADDR(x) ((x) << S_MI0_PHY_REG_ADDR)
#define G_MI0_PHY_REG_ADDR(x) (((x) >> S_MI0_PHY_REG_ADDR) & M_MI0_PHY_REG_ADDR)

#define S_MI0_PHY_ADDR    5
#define M_MI0_PHY_ADDR    0x1f
#define V_MI0_PHY_ADDR(x) ((x) << S_MI0_PHY_ADDR)
#define G_MI0_PHY_ADDR(x) (((x) >> S_MI0_PHY_ADDR) & M_MI0_PHY_ADDR)

#define A_MI0_DATA_EXT 0xb0c
#define A_MI0_DATA_INT 0xb10

/* GMAC registers */
#define A_GMAC_MACID_LO	0x28
#define A_GMAC_MACID_HI	0x2c
#define A_GMAC_CSR	0x30

#define S_INTERFACE    0
#define M_INTERFACE    0x3
#define V_INTERFACE(x) ((x) << S_INTERFACE)
#define G_INTERFACE(x) (((x) >> S_INTERFACE) & M_INTERFACE)

#define S_MAC_TX_ENABLE    2
#define V_MAC_TX_ENABLE(x) ((x) << S_MAC_TX_ENABLE)
#define F_MAC_TX_ENABLE    V_MAC_TX_ENABLE(1U)

#define S_MAC_RX_ENABLE    3
#define V_MAC_RX_ENABLE(x) ((x) << S_MAC_RX_ENABLE)
#define F_MAC_RX_ENABLE    V_MAC_RX_ENABLE(1U)

#define S_MAC_LB_ENABLE    4
#define V_MAC_LB_ENABLE(x) ((x) << S_MAC_LB_ENABLE)
#define F_MAC_LB_ENABLE    V_MAC_LB_ENABLE(1U)

#define S_MAC_SPEED    5
#define M_MAC_SPEED    0x3
#define V_MAC_SPEED(x) ((x) << S_MAC_SPEED)
#define G_MAC_SPEED(x) (((x) >> S_MAC_SPEED) & M_MAC_SPEED)

#define S_MAC_HD_FC_ENABLE    7
#define V_MAC_HD_FC_ENABLE(x) ((x) << S_MAC_HD_FC_ENABLE)
#define F_MAC_HD_FC_ENABLE    V_MAC_HD_FC_ENABLE(1U)

#define S_MAC_HALF_DUPLEX    8
#define V_MAC_HALF_DUPLEX(x) ((x) << S_MAC_HALF_DUPLEX)
#define F_MAC_HALF_DUPLEX    V_MAC_HALF_DUPLEX(1U)

#define S_MAC_PROMISC    9
#define V_MAC_PROMISC(x) ((x) << S_MAC_PROMISC)
#define F_MAC_PROMISC    V_MAC_PROMISC(1U)

#define S_MAC_MC_ENABLE    10
#define V_MAC_MC_ENABLE(x) ((x) << S_MAC_MC_ENABLE)
#define F_MAC_MC_ENABLE    V_MAC_MC_ENABLE(1U)

#define S_MAC_RESET    11
#define V_MAC_RESET(x) ((x) << S_MAC_RESET)
#define F_MAC_RESET    V_MAC_RESET(1U)

#define S_MAC_RX_PAUSE_ENABLE    12
#define V_MAC_RX_PAUSE_ENABLE(x) ((x) << S_MAC_RX_PAUSE_ENABLE)
#define F_MAC_RX_PAUSE_ENABLE    V_MAC_RX_PAUSE_ENABLE(1U)

#define S_MAC_TX_PAUSE_ENABLE    13
#define V_MAC_TX_PAUSE_ENABLE(x) ((x) << S_MAC_TX_PAUSE_ENABLE)
#define F_MAC_TX_PAUSE_ENABLE    V_MAC_TX_PAUSE_ENABLE(1U)

#define S_MAC_LWM_ENABLE    14
#define V_MAC_LWM_ENABLE(x) ((x) << S_MAC_LWM_ENABLE)
#define F_MAC_LWM_ENABLE    V_MAC_LWM_ENABLE(1U)

#define S_MAC_MAGIC_PKT_ENABLE    15
#define V_MAC_MAGIC_PKT_ENABLE(x) ((x) << S_MAC_MAGIC_PKT_ENABLE)
#define F_MAC_MAGIC_PKT_ENABLE    V_MAC_MAGIC_PKT_ENABLE(1U)

#define S_MAC_ISL_ENABLE    16
#define V_MAC_ISL_ENABLE(x) ((x) << S_MAC_ISL_ENABLE)
#define F_MAC_ISL_ENABLE    V_MAC_ISL_ENABLE(1U)

#define S_MAC_JUMBO_ENABLE    17
#define V_MAC_JUMBO_ENABLE(x) ((x) << S_MAC_JUMBO_ENABLE)
#define F_MAC_JUMBO_ENABLE    V_MAC_JUMBO_ENABLE(1U)

#define S_MAC_RX_PAD_ENABLE    18
#define V_MAC_RX_PAD_ENABLE(x) ((x) << S_MAC_RX_PAD_ENABLE)
#define F_MAC_RX_PAD_ENABLE    V_MAC_RX_PAD_ENABLE(1U)

#define S_MAC_RX_CRC_ENABLE    19
#define V_MAC_RX_CRC_ENABLE(x) ((x) << S_MAC_RX_CRC_ENABLE)
#define F_MAC_RX_CRC_ENABLE    V_MAC_RX_CRC_ENABLE(1U)

#define A_GMAC_IFS 0x34

#define S_MAC_IFS2    0
#define M_MAC_IFS2    0x3f
#define V_MAC_IFS2(x) ((x) << S_MAC_IFS2)
#define G_MAC_IFS2(x) (((x) >> S_MAC_IFS2) & M_MAC_IFS2)

#define S_MAC_IFS1    8
#define M_MAC_IFS1    0x7f
#define V_MAC_IFS1(x) ((x) << S_MAC_IFS1)
#define G_MAC_IFS1(x) (((x) >> S_MAC_IFS1) & M_MAC_IFS1)

#define A_GMAC_JUMBO_FRAME_LEN 0x38
#define A_GMAC_LNK_DLY 0x3c
#define A_GMAC_PAUSETIME 0x40
#define A_GMAC_MCAST_LO 0x44
#define A_GMAC_MCAST_HI 0x48
#define A_GMAC_MCAST_MASK_LO 0x4c
#define A_GMAC_MCAST_MASK_HI 0x50
#define A_GMAC_RMT_CNT 0x54
#define A_GMAC_RMT_DATA 0x58
#define A_GMAC_BACKOFF_SEED 0x5c
#define A_GMAC_TXF_THRES 0x60

#define S_TXF_READ_THRESHOLD    0
#define M_TXF_READ_THRESHOLD    0xff
#define V_TXF_READ_THRESHOLD(x) ((x) << S_TXF_READ_THRESHOLD)
#define G_TXF_READ_THRESHOLD(x) (((x) >> S_TXF_READ_THRESHOLD) & M_TXF_READ_THRESHOLD)

#define S_TXF_WRITE_THRESHOLD    16
#define M_TXF_WRITE_THRESHOLD    0xff
#define V_TXF_WRITE_THRESHOLD(x) ((x) << S_TXF_WRITE_THRESHOLD)
#define G_TXF_WRITE_THRESHOLD(x) (((x) >> S_TXF_WRITE_THRESHOLD) & M_TXF_WRITE_THRESHOLD)

#define MAC_REG_BASE 0x600
#define MAC_REG_ADDR(idx, reg) (MAC_REG_BASE + (idx) * 128 + (reg))

#define MAC_REG_IDLO(idx)              MAC_REG_ADDR(idx, A_GMAC_MACID_LO)
#define MAC_REG_IDHI(idx)              MAC_REG_ADDR(idx, A_GMAC_MACID_HI)
#define MAC_REG_CSR(idx)               MAC_REG_ADDR(idx, A_GMAC_CSR)
#define MAC_REG_IFS(idx)               MAC_REG_ADDR(idx, A_GMAC_IFS)
#define MAC_REG_LARGEFRAMELENGTH(idx) MAC_REG_ADDR(idx, A_GMAC_JUMBO_FRAME_LEN)
#define MAC_REG_LINKDLY(idx)           MAC_REG_ADDR(idx, A_GMAC_LNK_DLY)
#define MAC_REG_PAUSETIME(idx)         MAC_REG_ADDR(idx, A_GMAC_PAUSETIME)
#define MAC_REG_CASTLO(idx)            MAC_REG_ADDR(idx, A_GMAC_MCAST_LO)
#define MAC_REG_MCASTHI(idx)           MAC_REG_ADDR(idx, A_GMAC_MCAST_HI)
#define MAC_REG_CASTMASKLO(idx)        MAC_REG_ADDR(idx, A_GMAC_MCAST_MASK_LO)
#define MAC_REG_MCASTMASKHI(idx)       MAC_REG_ADDR(idx, A_GMAC_MCAST_MASK_HI)
#define MAC_REG_RMCNT(idx)             MAC_REG_ADDR(idx, A_GMAC_RMT_CNT)
#define MAC_REG_RMDATA(idx)            MAC_REG_ADDR(idx, A_GMAC_RMT_DATA)
#define MAC_REG_GMRANDBACKOFFSEED(idx) MAC_REG_ADDR(idx, A_GMAC_BACKOFF_SEED)
#define MAC_REG_TXFTHRESHOLDS(idx)     MAC_REG_ADDR(idx, A_GMAC_TXF_THRES)

#endif
