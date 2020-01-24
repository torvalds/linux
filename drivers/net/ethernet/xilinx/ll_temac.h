/* SPDX-License-Identifier: GPL-2.0 */

#ifndef XILINX_LL_TEMAC_H
#define XILINX_LL_TEMAC_H

#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/spinlock.h>

#ifdef CONFIG_PPC_DCR
#include <asm/dcr.h>
#include <asm/dcr-regs.h>
#endif

/* packet size info */
#define XTE_HDR_SIZE			14      /* size of Ethernet header */
#define XTE_TRL_SIZE			4       /* size of Ethernet trailer (FCS) */
#define XTE_JUMBO_MTU			9000
#define XTE_MAX_JUMBO_FRAME_SIZE	(XTE_JUMBO_MTU + XTE_HDR_SIZE + XTE_TRL_SIZE)

/*  Configuration options */

/*  Accept all incoming packets.
 *  This option defaults to disabled (cleared) */
#define XTE_OPTION_PROMISC                      (1 << 0)
/*  Jumbo frame support for Tx & Rx.
 *  This option defaults to disabled (cleared) */
#define XTE_OPTION_JUMBO                        (1 << 1)
/*  VLAN Rx & Tx frame support.
 *  This option defaults to disabled (cleared) */
#define XTE_OPTION_VLAN                         (1 << 2)
/*  Enable recognition of flow control frames on Rx
 *  This option defaults to enabled (set) */
#define XTE_OPTION_FLOW_CONTROL                 (1 << 4)
/*  Strip FCS and PAD from incoming frames.
 *  Note: PAD from VLAN frames is not stripped.
 *  This option defaults to disabled (set) */
#define XTE_OPTION_FCS_STRIP                    (1 << 5)
/*  Generate FCS field and add PAD automatically for outgoing frames.
 *  This option defaults to enabled (set) */
#define XTE_OPTION_FCS_INSERT                   (1 << 6)
/*  Enable Length/Type error checking for incoming frames. When this option is
set, the MAC will filter frames that have a mismatched type/length field
and if XTE_OPTION_REPORT_RXERR is set, the user is notified when these
types of frames are encountered. When this option is cleared, the MAC will
allow these types of frames to be received.
This option defaults to enabled (set) */
#define XTE_OPTION_LENTYPE_ERR                  (1 << 7)
/*  Enable the transmitter.
 *  This option defaults to enabled (set) */
#define XTE_OPTION_TXEN                         (1 << 11)
/*  Enable the receiver
*   This option defaults to enabled (set) */
#define XTE_OPTION_RXEN                         (1 << 12)

/*  Default options set when device is initialized or reset */
#define XTE_OPTION_DEFAULTS                     \
	(XTE_OPTION_TXEN |                          \
	 XTE_OPTION_FLOW_CONTROL |                  \
	 XTE_OPTION_RXEN)

/* XPS_LL_TEMAC SDMA registers definition */

#define TX_NXTDESC_PTR      0x00            /* r */
#define TX_CURBUF_ADDR      0x01            /* r */
#define TX_CURBUF_LENGTH    0x02            /* r */
#define TX_CURDESC_PTR      0x03            /* rw */
#define TX_TAILDESC_PTR     0x04            /* rw */
#define TX_CHNL_CTRL        0x05            /* rw */
/*
 0:7      24:31       IRQTimeout
 8:15     16:23       IRQCount
 16:20    11:15       Reserved
 21       10          0
 22       9           UseIntOnEnd
 23       8           LdIRQCnt
 24       7           IRQEn
 25:28    3:6         Reserved
 29       2           IrqErrEn
 30       1           IrqDlyEn
 31       0           IrqCoalEn
*/
#define CHNL_CTRL_IRQ_IOE       (1 << 9)
#define CHNL_CTRL_IRQ_EN        (1 << 7)
#define CHNL_CTRL_IRQ_ERR_EN    (1 << 2)
#define CHNL_CTRL_IRQ_DLY_EN    (1 << 1)
#define CHNL_CTRL_IRQ_COAL_EN   (1 << 0)
#define TX_IRQ_REG          0x06            /* rw */
/*
  0:7      24:31       DltTmrValue
 8:15     16:23       ClscCntrValue
 16:17    14:15       Reserved
 18:21    10:13       ClscCnt
 22:23    8:9         DlyCnt
 24:28    3::7        Reserved
 29       2           ErrIrq
 30       1           DlyIrq
 31       0           CoalIrq
 */
#define TX_CHNL_STS         0x07            /* r */
/*
   0:9      22:31   Reserved
 10       21      TailPErr
 11       20      CmpErr
 12       19      AddrErr
 13       18      NxtPErr
 14       17      CurPErr
 15       16      BsyWr
 16:23    8:15    Reserved
 24       7       Error
 25       6       IOE
 26       5       SOE
 27       4       Cmplt
 28       3       SOP
 29       2       EOP
 30       1       EngBusy
 31       0       Reserved
*/

#define RX_NXTDESC_PTR      0x08            /* r */
#define RX_CURBUF_ADDR      0x09            /* r */
#define RX_CURBUF_LENGTH    0x0a            /* r */
#define RX_CURDESC_PTR      0x0b            /* rw */
#define RX_TAILDESC_PTR     0x0c            /* rw */
#define RX_CHNL_CTRL        0x0d            /* rw */
/*
 0:7      24:31       IRQTimeout
 8:15     16:23       IRQCount
 16:20    11:15       Reserved
 21       10          0
 22       9           UseIntOnEnd
 23       8           LdIRQCnt
 24       7           IRQEn
 25:28    3:6         Reserved
 29       2           IrqErrEn
 30       1           IrqDlyEn
 31       0           IrqCoalEn
 */
#define RX_IRQ_REG          0x0e            /* rw */
#define IRQ_COAL        (1 << 0)
#define IRQ_DLY         (1 << 1)
#define IRQ_ERR         (1 << 2)
#define IRQ_DMAERR      (1 << 7)            /* this is not documented ??? */
/*
 0:7      24:31       DltTmrValue
 8:15     16:23       ClscCntrValue
 16:17    14:15       Reserved
 18:21    10:13       ClscCnt
 22:23    8:9         DlyCnt
 24:28    3::7        Reserved
*/
#define RX_CHNL_STS         0x0f        /* r */
#define CHNL_STS_ENGBUSY    (1 << 1)
#define CHNL_STS_EOP        (1 << 2)
#define CHNL_STS_SOP        (1 << 3)
#define CHNL_STS_CMPLT      (1 << 4)
#define CHNL_STS_SOE        (1 << 5)
#define CHNL_STS_IOE        (1 << 6)
#define CHNL_STS_ERR        (1 << 7)

#define CHNL_STS_BSYWR      (1 << 16)
#define CHNL_STS_CURPERR    (1 << 17)
#define CHNL_STS_NXTPERR    (1 << 18)
#define CHNL_STS_ADDRERR    (1 << 19)
#define CHNL_STS_CMPERR     (1 << 20)
#define CHNL_STS_TAILERR    (1 << 21)
/*
 0:9      22:31   Reserved
 10       21      TailPErr
 11       20      CmpErr
 12       19      AddrErr
 13       18      NxtPErr
 14       17      CurPErr
 15       16      BsyWr
 16:23    8:15    Reserved
 24       7       Error
 25       6       IOE
 26       5       SOE
 27       4       Cmplt
 28       3       SOP
 29       2       EOP
 30       1       EngBusy
 31       0       Reserved
*/

#define DMA_CONTROL_REG             0x10            /* rw */
#define DMA_CONTROL_RST                 (1 << 0)
#define DMA_TAIL_ENABLE                 (1 << 2)

/* XPS_LL_TEMAC direct registers definition */

#define XTE_RAF0_OFFSET              0x00
#define RAF0_RST                        (1 << 0)
#define RAF0_MCSTREJ                    (1 << 1)
#define RAF0_BCSTREJ                    (1 << 2)
#define XTE_TPF0_OFFSET              0x04
#define XTE_IFGP0_OFFSET             0x08
#define XTE_ISR0_OFFSET              0x0c
#define ISR0_HARDACSCMPLT               (1 << 0)
#define ISR0_AUTONEG                    (1 << 1)
#define ISR0_RXCMPLT                    (1 << 2)
#define ISR0_RXREJ                      (1 << 3)
#define ISR0_RXFIFOOVR                  (1 << 4)
#define ISR0_TXCMPLT                    (1 << 5)
#define ISR0_RXDCMLCK                   (1 << 6)

#define XTE_IPR0_OFFSET              0x10
#define XTE_IER0_OFFSET              0x14

#define XTE_MSW0_OFFSET              0x20
#define XTE_LSW0_OFFSET              0x24
#define XTE_CTL0_OFFSET              0x28
#define XTE_RDY0_OFFSET              0x2c

#define XTE_RSE_MIIM_RR_MASK      0x0002
#define XTE_RSE_MIIM_WR_MASK      0x0004
#define XTE_RSE_CFG_RR_MASK       0x0020
#define XTE_RSE_CFG_WR_MASK       0x0040
#define XTE_RDY0_HARD_ACS_RDY_MASK  (0x10000)

/* XPS_LL_TEMAC indirect registers offset definition */

#define	XTE_RXC0_OFFSET			0x00000200 /* Rx configuration word 0 */
#define	XTE_RXC1_OFFSET			0x00000240 /* Rx configuration word 1 */
#define XTE_RXC1_RXRST_MASK		(1 << 31)  /* Receiver reset */
#define XTE_RXC1_RXJMBO_MASK		(1 << 30)  /* Jumbo frame enable */
#define XTE_RXC1_RXFCS_MASK		(1 << 29)  /* FCS not stripped */
#define XTE_RXC1_RXEN_MASK		(1 << 28)  /* Receiver enable */
#define XTE_RXC1_RXVLAN_MASK		(1 << 27)  /* VLAN enable */
#define XTE_RXC1_RXHD_MASK		(1 << 26)  /* Half duplex */
#define XTE_RXC1_RXLT_MASK		(1 << 25)  /* Length/type check disable */

#define XTE_TXC_OFFSET			0x00000280 /*  Tx configuration */
#define XTE_TXC_TXRST_MASK		(1 << 31)  /* Transmitter reset */
#define XTE_TXC_TXJMBO_MASK		(1 << 30)  /* Jumbo frame enable */
#define XTE_TXC_TXFCS_MASK		(1 << 29)  /* Generate FCS */
#define XTE_TXC_TXEN_MASK		(1 << 28)  /* Transmitter enable */
#define XTE_TXC_TXVLAN_MASK		(1 << 27)  /* VLAN enable */
#define XTE_TXC_TXHD_MASK		(1 << 26)  /* Half duplex */

#define XTE_FCC_OFFSET			0x000002C0 /* Flow control config */
#define XTE_FCC_RXFLO_MASK		(1 << 29)  /* Rx flow control enable */
#define XTE_FCC_TXFLO_MASK		(1 << 30)  /* Tx flow control enable */

#define XTE_EMCFG_OFFSET		0x00000300 /* EMAC configuration */
#define XTE_EMCFG_LINKSPD_MASK		0xC0000000 /* Link speed */
#define XTE_EMCFG_HOSTEN_MASK		(1 << 26)  /* Host interface enable */
#define XTE_EMCFG_LINKSPD_10		0x00000000 /* 10 Mbit LINKSPD_MASK */
#define XTE_EMCFG_LINKSPD_100		(1 << 30)  /* 100 Mbit LINKSPD_MASK */
#define XTE_EMCFG_LINKSPD_1000		(1 << 31)  /* 1000 Mbit LINKSPD_MASK */

#define XTE_GMIC_OFFSET			0x00000320 /* RGMII/SGMII config */
#define XTE_MC_OFFSET			0x00000340 /* MDIO configuration */
#define XTE_UAW0_OFFSET			0x00000380 /* Unicast address word 0 */
#define XTE_UAW1_OFFSET			0x00000384 /* Unicast address word 1 */

#define XTE_MAW0_OFFSET			0x00000388 /* Multicast addr word 0 */
#define XTE_MAW1_OFFSET			0x0000038C /* Multicast addr word 1 */
#define XTE_AFM_OFFSET			0x00000390 /* Promiscuous mode */
#define XTE_AFM_EPPRM_MASK		(1 << 31)  /* Promiscuous mode enable */

/* Interrupt Request status */
#define XTE_TIS_OFFSET			0x000003A0
#define TIS_FRIS			(1 << 0)
#define TIS_MRIS			(1 << 1)
#define TIS_MWIS			(1 << 2)
#define TIS_ARIS			(1 << 3)
#define TIS_AWIS			(1 << 4)
#define TIS_CRIS			(1 << 5)
#define TIS_CWIS			(1 << 6)

#define XTE_TIE_OFFSET			0x000003A4 /* Interrupt enable */

/**  MII Mamagement Control register (MGTCR) */
#define XTE_MGTDR_OFFSET		0x000003B0 /* MII data */
#define XTE_MIIMAI_OFFSET		0x000003B4 /* MII control */

#define CNTLREG_WRITE_ENABLE_MASK   0x8000
#define CNTLREG_EMAC1SEL_MASK       0x0400
#define CNTLREG_ADDRESSCODE_MASK    0x03ff

/* CDMAC descriptor status bit definitions */

#define STS_CTRL_APP0_ERR         (1 << 31)
#define STS_CTRL_APP0_IRQONEND    (1 << 30)
/* undoccumented */
#define STS_CTRL_APP0_STOPONEND   (1 << 29)
#define STS_CTRL_APP0_CMPLT       (1 << 28)
#define STS_CTRL_APP0_SOP         (1 << 27)
#define STS_CTRL_APP0_EOP         (1 << 26)
#define STS_CTRL_APP0_ENGBUSY     (1 << 25)
/* undocumented */
#define STS_CTRL_APP0_ENGRST      (1 << 24)

#define TX_CONTROL_CALC_CSUM_MASK   1

#define MULTICAST_CAM_TABLE_NUM 4

/* TEMAC Synthesis features */
#define TEMAC_FEATURE_RX_CSUM  (1 << 0)
#define TEMAC_FEATURE_TX_CSUM  (1 << 1)

/* TX/RX CURDESC_PTR points to first descriptor */
/* TX/RX TAILDESC_PTR points to last descriptor in linked list */

/**
 * struct cdmac_bd - LocalLink buffer descriptor format
 *
 * app0 bits:
 *	0    Error
 *	1    IrqOnEnd    generate an interrupt at completion of DMA  op
 *	2    reserved
 *	3    completed   Current descriptor completed
 *	4    SOP         TX - marks first desc/ RX marks first desct
 *	5    EOP         TX marks last desc/RX marks last desc
 *	6    EngBusy     DMA is processing
 *	7    reserved
 *	8:31 application specific
 */
struct cdmac_bd {
	u32 next;	/* Physical address of next buffer descriptor */
	u32 phys;
	u32 len;
	u32 app0;
	u32 app1;	/* TX start << 16 | insert */
	u32 app2;	/* TX csum */
	u32 app3;
	u32 app4;	/* skb for TX length for RX */
};

struct temac_local {
	struct net_device *ndev;
	struct device *dev;

	/* Connection to PHY device */
	struct device_node *phy_node;
	/* For non-device-tree devices */
	char phy_name[MII_BUS_ID_SIZE + 3];
	phy_interface_t phy_interface;

	/* MDIO bus data */
	struct mii_bus *mii_bus;	/* MII bus reference */

	/* IO registers, dma functions and IRQs */
	void __iomem *regs;
	void __iomem *sdma_regs;
#ifdef CONFIG_PPC_DCR
	dcr_host_t sdma_dcrs;
#endif
	u32 (*temac_ior)(struct temac_local *lp, int offset);
	void (*temac_iow)(struct temac_local *lp, int offset, u32 value);
	u32 (*dma_in)(struct temac_local *lp, int reg);
	void (*dma_out)(struct temac_local *lp, int reg, u32 value);

	int tx_irq;
	int rx_irq;
	int emac_num;

	struct sk_buff **rx_skb;
	spinlock_t rx_lock;
	/* For synchronization of indirect register access.  Must be
	 * shared mutex between interfaces in same TEMAC block.
	 */
	spinlock_t *indirect_lock;
	u32 options;			/* Current options word */
	int last_link;
	unsigned int temac_features;

	/* Buffer descriptors */
	struct cdmac_bd *tx_bd_v;
	dma_addr_t tx_bd_p;
	struct cdmac_bd *rx_bd_v;
	dma_addr_t rx_bd_p;
	int tx_bd_ci;
	int tx_bd_next;
	int tx_bd_tail;
	int rx_bd_ci;

	/* DMA channel control setup */
	u32 tx_chnl_ctrl;
	u32 rx_chnl_ctrl;
};

/* Wrappers for temac_ior()/temac_iow() function pointers above */
#define temac_ior(lp, o) ((lp)->temac_ior(lp, o))
#define temac_iow(lp, o, v) ((lp)->temac_iow(lp, o, v))

/* xilinx_temac.c */
int temac_indirect_busywait(struct temac_local *lp);
u32 temac_indirect_in32(struct temac_local *lp, int reg);
u32 temac_indirect_in32_locked(struct temac_local *lp, int reg);
void temac_indirect_out32(struct temac_local *lp, int reg, u32 value);
void temac_indirect_out32_locked(struct temac_local *lp, int reg, u32 value);

/* xilinx_temac_mdio.c */
int temac_mdio_setup(struct temac_local *lp, struct platform_device *pdev);
void temac_mdio_teardown(struct temac_local *lp);

#endif /* XILINX_LL_TEMAC_H */
