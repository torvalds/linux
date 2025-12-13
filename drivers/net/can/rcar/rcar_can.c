// SPDX-License-Identifier: GPL-2.0+
/* Renesas R-Car CAN device driver
 *
 * Copyright (C) 2013 Cogent Embedded, Inc. <source@cogentembedded.com>
 * Copyright (C) 2013 Renesas Solutions Corp.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/can/dev.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>

#define RCAR_CAN_DRV_NAME	"rcar_can"

/* Clock Select Register settings */
enum CLKR {
	CLKR_CLKP1 = 0, /* Peripheral clock (clkp1) */
	CLKR_CLKP2 = 1, /* Peripheral clock (clkp2) */
	CLKR_CLKEXT = 3, /* Externally input clock */
};

#define RCAR_SUPPORTED_CLOCKS	(BIT(CLKR_CLKP1) | BIT(CLKR_CLKP2) | \
				 BIT(CLKR_CLKEXT))

/* Mailbox configuration:
 * mailbox 60 - 63 - Rx FIFO mailboxes
 * mailbox 56 - 59 - Tx FIFO mailboxes
 * non-FIFO mailboxes are not used
 */
#define RCAR_CAN_N_MBX		64 /* Number of mailboxes in non-FIFO mode */
#define RCAR_CAN_RX_FIFO_MBX	60 /* Mailbox - window to Rx FIFO */
#define RCAR_CAN_TX_FIFO_MBX	56 /* Mailbox - window to Tx FIFO */
#define RCAR_CAN_FIFO_DEPTH	4

/* Mailbox registers structure */
struct rcar_can_mbox_regs {
	u32 id;		/* IDE and RTR bits, SID and EID */
	u8 stub;	/* Not used */
	u8 dlc;		/* Data Length Code - bits [0..3] */
	u8 data[8];	/* Data Bytes */
	u8 tsh;		/* Time Stamp Higher Byte */
	u8 tsl;		/* Time Stamp Lower Byte */
};

struct rcar_can_regs {
	struct rcar_can_mbox_regs mb[RCAR_CAN_N_MBX]; /* Mailbox registers */
	u32 mkr_2_9[8];	/* Mask Registers 2-9 */
	u32 fidcr[2];	/* FIFO Received ID Compare Register */
	u32 mkivlr1;	/* Mask Invalid Register 1 */
	u32 mier1;	/* Mailbox Interrupt Enable Register 1 */
	u32 mkr_0_1[2];	/* Mask Registers 0-1 */
	u32 mkivlr0;    /* Mask Invalid Register 0*/
	u32 mier0;      /* Mailbox Interrupt Enable Register 0 */
	u8 pad_440[0x3c0];
	u8 mctl[64];	/* Message Control Registers */
	u16 ctlr;	/* Control Register */
	u16 str;	/* Status register */
	u8 bcr[3];	/* Bit Configuration Register */
	u8 clkr;	/* Clock Select Register */
	u8 rfcr;	/* Receive FIFO Control Register */
	u8 rfpcr;	/* Receive FIFO Pointer Control Register */
	u8 tfcr;	/* Transmit FIFO Control Register */
	u8 tfpcr;       /* Transmit FIFO Pointer Control Register */
	u8 eier;	/* Error Interrupt Enable Register */
	u8 eifr;	/* Error Interrupt Factor Judge Register */
	u8 recr;	/* Receive Error Count Register */
	u8 tecr;        /* Transmit Error Count Register */
	u8 ecsr;	/* Error Code Store Register */
	u8 cssr;	/* Channel Search Support Register */
	u8 mssr;	/* Mailbox Search Status Register */
	u8 msmr;	/* Mailbox Search Mode Register */
	u16 tsr;	/* Time Stamp Register */
	u8 afsr;	/* Acceptance Filter Support Register */
	u8 pad_857;
	u8 tcr;		/* Test Control Register */
	u8 pad_859[7];
	u8 ier;		/* Interrupt Enable Register */
	u8 isr;		/* Interrupt Status Register */
	u8 pad_862;
	u8 mbsmr;	/* Mailbox Search Mask Register */
};

struct rcar_can_priv {
	struct can_priv can;	/* Must be the first member! */
	struct net_device *ndev;
	struct napi_struct napi;
	struct rcar_can_regs __iomem *regs;
	struct clk *can_clk;
	u32 tx_head;
	u32 tx_tail;
	u8 clock_select;
	u8 ier;
};

static const struct can_bittiming_const rcar_can_bittiming_const = {
	.name = RCAR_CAN_DRV_NAME,
	.tseg1_min = 4,
	.tseg1_max = 16,
	.tseg2_min = 2,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 1024,
	.brp_inc = 1,
};

/* Control Register bits */
#define RCAR_CAN_CTLR_BOM	GENMASK(12, 11)	/* Bus-Off Recovery Mode Bits */
#define RCAR_CAN_CTLR_BOM_ENT		1	/* Entry to halt mode */
						/* at bus-off entry */
#define RCAR_CAN_CTLR_SLPM	BIT(10)		/* Sleep Mode */
#define RCAR_CAN_CTLR_CANM	GENMASK(9, 8)	/* Operating Mode Select Bit */
#define RCAR_CAN_CTLR_CANM_OPER		0	/* Operation Mode */
#define RCAR_CAN_CTLR_CANM_RESET	1	/* Reset Mode */
#define RCAR_CAN_CTLR_CANM_HALT		2	/* Halt Mode */
#define RCAR_CAN_CTLR_CANM_FORCE_RESET	3	/* Reset Mode (forcible) */
#define RCAR_CAN_CTLR_MLM	BIT(3)		/* Message Lost Mode Select */
#define RCAR_CAN_CTLR_IDFM	GENMASK(2, 1)	/* ID Format Mode Select Bits */
#define RCAR_CAN_CTLR_IDFM_STD		0	/* Standard ID mode */
#define RCAR_CAN_CTLR_IDFM_EXT		1	/* Extended ID mode */
#define RCAR_CAN_CTLR_IDFM_MIXED	2	/* Mixed ID mode */
#define RCAR_CAN_CTLR_MBM	BIT(0)		/* Mailbox Mode select */

/* Status Register bits */
#define RCAR_CAN_STR_RSTST	BIT(8)		/* Reset Status Bit */

/* FIFO Received ID Compare Registers 0 and 1 bits */
#define RCAR_CAN_FIDCR_IDE	BIT(31)		/* ID Extension Bit */
#define RCAR_CAN_FIDCR_RTR	BIT(30)		/* Remote Transmission Request Bit */

/* Receive FIFO Control Register bits */
#define RCAR_CAN_RFCR_RFEST	BIT(7)		/* Receive FIFO Empty Status Flag */
#define RCAR_CAN_RFCR_RFE	BIT(0)		/* Receive FIFO Enable */

/* Transmit FIFO Control Register bits */
#define RCAR_CAN_TFCR_TFUST	GENMASK(3, 1)	/* Transmit FIFO Unsent Message */
						/* Number Status Bits */
#define RCAR_CAN_TFCR_TFE	BIT(0)		/* Transmit FIFO Enable */

#define RCAR_CAN_N_RX_MKREGS1	2		/* Number of mask registers */
						/* for Rx mailboxes 0-31 */
#define RCAR_CAN_N_RX_MKREGS2	8

/* Bit Configuration Register settings */
#define RCAR_CAN_BCR_TSEG1	GENMASK(23, 20)
#define RCAR_CAN_BCR_BRP	GENMASK(17, 8)
#define RCAR_CAN_BCR_SJW	GENMASK(5, 4)
#define RCAR_CAN_BCR_TSEG2	GENMASK(2, 0)

/* Mailbox and Mask Registers bits */
#define RCAR_CAN_IDE		BIT(31)		/* ID Extension */
#define RCAR_CAN_RTR		BIT(30)		/* Remote Transmission Request */
#define RCAR_CAN_SID		GENMASK(28, 18)	/* Standard ID */
#define RCAR_CAN_EID		GENMASK(28, 0)	/* Extended ID */

/* Mailbox Interrupt Enable Register 1 bits */
#define RCAR_CAN_MIER1_RXFIE	BIT(28)		/* Receive  FIFO Interrupt Enable */
#define RCAR_CAN_MIER1_TXFIE	BIT(24)		/* Transmit FIFO Interrupt Enable */

/* Interrupt Enable Register bits */
#define RCAR_CAN_IER_ERSIE	BIT(5)		/* Error (ERS) Interrupt Enable Bit */
#define RCAR_CAN_IER_RXFIE	BIT(4)		/* Reception FIFO Interrupt */
						/* Enable Bit */
#define RCAR_CAN_IER_TXFIE	BIT(3)		/* Transmission FIFO Interrupt */
						/* Enable Bit */
/* Interrupt Status Register bits */
#define RCAR_CAN_ISR_ERSF	BIT(5)		/* Error (ERS) Interrupt Status Bit */
#define RCAR_CAN_ISR_RXFF	BIT(4)		/* Reception FIFO Interrupt */
						/* Status Bit */
#define RCAR_CAN_ISR_TXFF	BIT(3)		/* Transmission FIFO Interrupt */
						/* Status Bit */

/* Error Interrupt Enable Register bits */
#define RCAR_CAN_EIER_BLIE	BIT(7)		/* Bus Lock Interrupt Enable */
#define RCAR_CAN_EIER_OLIE	BIT(6)		/* Overload Frame Transmit */
						/* Interrupt Enable */
#define RCAR_CAN_EIER_ORIE	BIT(5)		/* Receive Overrun  Interrupt Enable */
#define RCAR_CAN_EIER_BORIE	BIT(4)		/* Bus-Off Recovery Interrupt Enable */
#define RCAR_CAN_EIER_BOEIE	BIT(3)		/* Bus-Off Entry Interrupt Enable */
#define RCAR_CAN_EIER_EPIE	BIT(2)		/* Error Passive Interrupt Enable */
#define RCAR_CAN_EIER_EWIE	BIT(1)		/* Error Warning Interrupt Enable */
#define RCAR_CAN_EIER_BEIE	BIT(0)		/* Bus Error Interrupt Enable */

/* Error Interrupt Factor Judge Register bits */
#define RCAR_CAN_EIFR_BLIF	BIT(7)		/* Bus Lock Detect Flag */
#define RCAR_CAN_EIFR_OLIF	BIT(6)		/* Overload Frame Transmission */
						/* Detect Flag */
#define RCAR_CAN_EIFR_ORIF	BIT(5)		/* Receive Overrun Detect Flag */
#define RCAR_CAN_EIFR_BORIF	BIT(4)		/* Bus-Off Recovery Detect Flag */
#define RCAR_CAN_EIFR_BOEIF	BIT(3)		/* Bus-Off Entry Detect Flag */
#define RCAR_CAN_EIFR_EPIF	BIT(2)		/* Error Passive Detect Flag */
#define RCAR_CAN_EIFR_EWIF	BIT(1)		/* Error Warning Detect Flag */
#define RCAR_CAN_EIFR_BEIF	BIT(0)		/* Bus Error Detect Flag */

/* Error Code Store Register bits */
#define RCAR_CAN_ECSR_EDPM	BIT(7)		/* Error Display Mode Select Bit */
#define RCAR_CAN_ECSR_ADEF	BIT(6)		/* ACK Delimiter Error Flag */
#define RCAR_CAN_ECSR_BE0F	BIT(5)		/* Bit Error (dominant) Flag */
#define RCAR_CAN_ECSR_BE1F	BIT(4)		/* Bit Error (recessive) Flag */
#define RCAR_CAN_ECSR_CEF	BIT(3)		/* CRC Error Flag */
#define RCAR_CAN_ECSR_AEF	BIT(2)		/* ACK Error Flag */
#define RCAR_CAN_ECSR_FEF	BIT(1)		/* Form Error Flag */
#define RCAR_CAN_ECSR_SEF	BIT(0)		/* Stuff Error Flag */

#define RCAR_CAN_NAPI_WEIGHT	4
#define MAX_STR_READS		0x100

static void tx_failure_cleanup(struct net_device *ndev)
{
	int i;

	for (i = 0; i < RCAR_CAN_FIFO_DEPTH; i++)
		can_free_echo_skb(ndev, i, NULL);
}

static void rcar_can_error(struct net_device *ndev)
{
	struct rcar_can_priv *priv = netdev_priv(ndev);
	struct can_frame *cf;
	struct sk_buff *skb;
	u8 eifr, txerr = 0, rxerr = 0;

	/* Propagate the error condition to the CAN stack */
	skb = alloc_can_err_skb(ndev, &cf);

	eifr = readb(&priv->regs->eifr);
	if (eifr & (RCAR_CAN_EIFR_EWIF | RCAR_CAN_EIFR_EPIF)) {
		txerr = readb(&priv->regs->tecr);
		rxerr = readb(&priv->regs->recr);
		if (skb)
			cf->can_id |= CAN_ERR_CRTL;
	}
	if (eifr & RCAR_CAN_EIFR_BEIF) {
		int rx_errors = 0, tx_errors = 0;
		u8 ecsr;

		netdev_dbg(priv->ndev, "Bus error interrupt:\n");
		if (skb)
			cf->can_id |= CAN_ERR_BUSERROR | CAN_ERR_PROT;

		ecsr = readb(&priv->regs->ecsr);
		if (ecsr & RCAR_CAN_ECSR_ADEF) {
			netdev_dbg(priv->ndev, "ACK Delimiter Error\n");
			tx_errors++;
			writeb((u8)~RCAR_CAN_ECSR_ADEF, &priv->regs->ecsr);
			if (skb)
				cf->data[3] = CAN_ERR_PROT_LOC_ACK_DEL;
		}
		if (ecsr & RCAR_CAN_ECSR_BE0F) {
			netdev_dbg(priv->ndev, "Bit Error (dominant)\n");
			tx_errors++;
			writeb((u8)~RCAR_CAN_ECSR_BE0F, &priv->regs->ecsr);
			if (skb)
				cf->data[2] |= CAN_ERR_PROT_BIT0;
		}
		if (ecsr & RCAR_CAN_ECSR_BE1F) {
			netdev_dbg(priv->ndev, "Bit Error (recessive)\n");
			tx_errors++;
			writeb((u8)~RCAR_CAN_ECSR_BE1F, &priv->regs->ecsr);
			if (skb)
				cf->data[2] |= CAN_ERR_PROT_BIT1;
		}
		if (ecsr & RCAR_CAN_ECSR_CEF) {
			netdev_dbg(priv->ndev, "CRC Error\n");
			rx_errors++;
			writeb((u8)~RCAR_CAN_ECSR_CEF, &priv->regs->ecsr);
			if (skb)
				cf->data[3] = CAN_ERR_PROT_LOC_CRC_SEQ;
		}
		if (ecsr & RCAR_CAN_ECSR_AEF) {
			netdev_dbg(priv->ndev, "ACK Error\n");
			tx_errors++;
			writeb((u8)~RCAR_CAN_ECSR_AEF, &priv->regs->ecsr);
			if (skb) {
				cf->can_id |= CAN_ERR_ACK;
				cf->data[3] = CAN_ERR_PROT_LOC_ACK;
			}
		}
		if (ecsr & RCAR_CAN_ECSR_FEF) {
			netdev_dbg(priv->ndev, "Form Error\n");
			rx_errors++;
			writeb((u8)~RCAR_CAN_ECSR_FEF, &priv->regs->ecsr);
			if (skb)
				cf->data[2] |= CAN_ERR_PROT_FORM;
		}
		if (ecsr & RCAR_CAN_ECSR_SEF) {
			netdev_dbg(priv->ndev, "Stuff Error\n");
			rx_errors++;
			writeb((u8)~RCAR_CAN_ECSR_SEF, &priv->regs->ecsr);
			if (skb)
				cf->data[2] |= CAN_ERR_PROT_STUFF;
		}

		priv->can.can_stats.bus_error++;
		ndev->stats.rx_errors += rx_errors;
		ndev->stats.tx_errors += tx_errors;
		writeb((u8)~RCAR_CAN_EIFR_BEIF, &priv->regs->eifr);
	}
	if (eifr & RCAR_CAN_EIFR_EWIF) {
		netdev_dbg(priv->ndev, "Error warning interrupt\n");
		priv->can.state = CAN_STATE_ERROR_WARNING;
		priv->can.can_stats.error_warning++;
		/* Clear interrupt condition */
		writeb((u8)~RCAR_CAN_EIFR_EWIF, &priv->regs->eifr);
		if (skb)
			cf->data[1] = txerr > rxerr ? CAN_ERR_CRTL_TX_WARNING :
					      CAN_ERR_CRTL_RX_WARNING;
	}
	if (eifr & RCAR_CAN_EIFR_EPIF) {
		netdev_dbg(priv->ndev, "Error passive interrupt\n");
		priv->can.state = CAN_STATE_ERROR_PASSIVE;
		priv->can.can_stats.error_passive++;
		/* Clear interrupt condition */
		writeb((u8)~RCAR_CAN_EIFR_EPIF, &priv->regs->eifr);
		if (skb)
			cf->data[1] = txerr > rxerr ? CAN_ERR_CRTL_TX_PASSIVE :
					      CAN_ERR_CRTL_RX_PASSIVE;
	}
	if (eifr & RCAR_CAN_EIFR_BOEIF) {
		netdev_dbg(priv->ndev, "Bus-off entry interrupt\n");
		tx_failure_cleanup(ndev);
		priv->ier = RCAR_CAN_IER_ERSIE;
		writeb(priv->ier, &priv->regs->ier);
		priv->can.state = CAN_STATE_BUS_OFF;
		/* Clear interrupt condition */
		writeb((u8)~RCAR_CAN_EIFR_BOEIF, &priv->regs->eifr);
		priv->can.can_stats.bus_off++;
		can_bus_off(ndev);
		if (skb)
			cf->can_id |= CAN_ERR_BUSOFF;
	} else if (skb) {
		cf->can_id |= CAN_ERR_CNT;
		cf->data[6] = txerr;
		cf->data[7] = rxerr;
	}
	if (eifr & RCAR_CAN_EIFR_ORIF) {
		netdev_dbg(priv->ndev, "Receive overrun error interrupt\n");
		ndev->stats.rx_over_errors++;
		ndev->stats.rx_errors++;
		writeb((u8)~RCAR_CAN_EIFR_ORIF, &priv->regs->eifr);
		if (skb) {
			cf->can_id |= CAN_ERR_CRTL;
			cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
		}
	}
	if (eifr & RCAR_CAN_EIFR_OLIF) {
		netdev_dbg(priv->ndev,
			   "Overload Frame Transmission error interrupt\n");
		ndev->stats.rx_over_errors++;
		ndev->stats.rx_errors++;
		writeb((u8)~RCAR_CAN_EIFR_OLIF, &priv->regs->eifr);
		if (skb) {
			cf->can_id |= CAN_ERR_PROT;
			cf->data[2] |= CAN_ERR_PROT_OVERLOAD;
		}
	}

	if (skb)
		netif_rx(skb);
}

static void rcar_can_tx_done(struct net_device *ndev)
{
	struct rcar_can_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	u8 isr;

	while (1) {
		u8 unsent = FIELD_GET(RCAR_CAN_TFCR_TFUST,
			    readb(&priv->regs->tfcr));

		if (priv->tx_head - priv->tx_tail <= unsent)
			break;
		stats->tx_packets++;
		stats->tx_bytes +=
			can_get_echo_skb(ndev,
					 priv->tx_tail % RCAR_CAN_FIFO_DEPTH,
					 NULL);

		priv->tx_tail++;
		netif_wake_queue(ndev);
	}
	/* Clear interrupt */
	isr = readb(&priv->regs->isr);
	writeb(isr & ~RCAR_CAN_ISR_TXFF, &priv->regs->isr);
}

static irqreturn_t rcar_can_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct rcar_can_priv *priv = netdev_priv(ndev);
	u8 isr;

	isr = readb(&priv->regs->isr);
	if (!(isr & priv->ier))
		return IRQ_NONE;

	if (isr & RCAR_CAN_ISR_ERSF)
		rcar_can_error(ndev);

	if (isr & RCAR_CAN_ISR_TXFF)
		rcar_can_tx_done(ndev);

	if (isr & RCAR_CAN_ISR_RXFF) {
		if (napi_schedule_prep(&priv->napi)) {
			/* Disable Rx FIFO interrupts */
			priv->ier &= ~RCAR_CAN_IER_RXFIE;
			writeb(priv->ier, &priv->regs->ier);
			__napi_schedule(&priv->napi);
		}
	}

	return IRQ_HANDLED;
}

static void rcar_can_set_bittiming(struct net_device *ndev)
{
	struct rcar_can_priv *priv = netdev_priv(ndev);
	struct can_bittiming *bt = &priv->can.bittiming;
	u32 bcr;

	bcr = FIELD_PREP(RCAR_CAN_BCR_TSEG1, bt->phase_seg1 + bt->prop_seg - 1) |
	      FIELD_PREP(RCAR_CAN_BCR_BRP, bt->brp - 1) |
	      FIELD_PREP(RCAR_CAN_BCR_SJW, bt->sjw - 1) |
	      FIELD_PREP(RCAR_CAN_BCR_TSEG2, bt->phase_seg2 - 1);
	/* Don't overwrite CLKR with 32-bit BCR access; CLKR has 8-bit access.
	 * All the registers are big-endian but they get byte-swapped on 32-bit
	 * read/write (but not on 8-bit, contrary to the manuals)...
	 */
	writel((bcr << 8) | priv->clock_select, &priv->regs->bcr);
}

static void rcar_can_start(struct net_device *ndev)
{
	struct rcar_can_priv *priv = netdev_priv(ndev);
	u16 ctlr;
	int i;

	/* Set controller to known mode:
	 * - FIFO mailbox mode
	 * - accept all messages
	 * - overrun mode
	 * CAN is in sleep mode after MCU hardware or software reset.
	 */
	ctlr = readw(&priv->regs->ctlr);
	ctlr &= ~RCAR_CAN_CTLR_SLPM;
	writew(ctlr, &priv->regs->ctlr);
	/* Go to reset mode */
	ctlr |= FIELD_PREP(RCAR_CAN_CTLR_CANM, RCAR_CAN_CTLR_CANM_FORCE_RESET);
	writew(ctlr, &priv->regs->ctlr);
	for (i = 0; i < MAX_STR_READS; i++) {
		if (readw(&priv->regs->str) & RCAR_CAN_STR_RSTST)
			break;
	}
	rcar_can_set_bittiming(ndev);
	/* Select mixed ID mode */
	ctlr |= FIELD_PREP(RCAR_CAN_CTLR_IDFM, RCAR_CAN_CTLR_IDFM_MIXED);
	/* Entry to halt mode automatically at bus-off */
	ctlr |= FIELD_PREP(RCAR_CAN_CTLR_BOM, RCAR_CAN_CTLR_BOM_ENT);
	ctlr |= RCAR_CAN_CTLR_MBM;	/* Select FIFO mailbox mode */
	ctlr |= RCAR_CAN_CTLR_MLM;	/* Overrun mode */
	writew(ctlr, &priv->regs->ctlr);

	/* Accept all SID and EID */
	writel(0, &priv->regs->mkr_2_9[6]);
	writel(0, &priv->regs->mkr_2_9[7]);
	/* In FIFO mailbox mode, write "0" to bits 24 to 31 */
	writel(0, &priv->regs->mkivlr1);
	/* Accept all frames */
	writel(0, &priv->regs->fidcr[0]);
	writel(RCAR_CAN_FIDCR_IDE | RCAR_CAN_FIDCR_RTR, &priv->regs->fidcr[1]);
	/* Enable and configure FIFO mailbox interrupts */
	writel(RCAR_CAN_MIER1_RXFIE | RCAR_CAN_MIER1_TXFIE, &priv->regs->mier1);

	priv->ier = RCAR_CAN_IER_ERSIE | RCAR_CAN_IER_RXFIE |
		    RCAR_CAN_IER_TXFIE;
	writeb(priv->ier, &priv->regs->ier);

	/* Accumulate error codes */
	writeb(RCAR_CAN_ECSR_EDPM, &priv->regs->ecsr);
	/* Enable error interrupts */
	writeb(RCAR_CAN_EIER_EWIE | RCAR_CAN_EIER_EPIE | RCAR_CAN_EIER_BOEIE |
	       (priv->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING ?
	       RCAR_CAN_EIER_BEIE : 0) | RCAR_CAN_EIER_ORIE |
	       RCAR_CAN_EIER_OLIE, &priv->regs->eier);
	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	/* Go to operation mode */
	ctlr &= ~RCAR_CAN_CTLR_CANM;
	ctlr |= FIELD_PREP(RCAR_CAN_CTLR_CANM, RCAR_CAN_CTLR_CANM_OPER);
	writew(ctlr, &priv->regs->ctlr);
	for (i = 0; i < MAX_STR_READS; i++) {
		if (!(readw(&priv->regs->str) & RCAR_CAN_STR_RSTST))
			break;
	}
	/* Enable Rx and Tx FIFO */
	writeb(RCAR_CAN_RFCR_RFE, &priv->regs->rfcr);
	writeb(RCAR_CAN_TFCR_TFE, &priv->regs->tfcr);
}

static int rcar_can_open(struct net_device *ndev)
{
	struct rcar_can_priv *priv = netdev_priv(ndev);
	int err;

	err = pm_runtime_resume_and_get(ndev->dev.parent);
	if (err) {
		netdev_err(ndev, "pm_runtime_resume_and_get() failed %pe\n",
			   ERR_PTR(err));
		goto out;
	}
	err = clk_prepare_enable(priv->can_clk);
	if (err) {
		netdev_err(ndev, "failed to enable CAN clock: %pe\n",
			   ERR_PTR(err));
		goto out_rpm;
	}
	err = open_candev(ndev);
	if (err) {
		netdev_err(ndev, "open_candev() failed %pe\n", ERR_PTR(err));
		goto out_can_clock;
	}
	napi_enable(&priv->napi);
	err = request_irq(ndev->irq, rcar_can_interrupt, 0, ndev->name, ndev);
	if (err) {
		netdev_err(ndev, "request_irq(%d) failed %pe\n", ndev->irq,
			   ERR_PTR(err));
		goto out_close;
	}
	rcar_can_start(ndev);
	netif_start_queue(ndev);
	return 0;
out_close:
	napi_disable(&priv->napi);
	close_candev(ndev);
out_can_clock:
	clk_disable_unprepare(priv->can_clk);
out_rpm:
	pm_runtime_put(ndev->dev.parent);
out:
	return err;
}

static void rcar_can_stop(struct net_device *ndev)
{
	struct rcar_can_priv *priv = netdev_priv(ndev);
	u16 ctlr;
	int i;

	/* Go to (force) reset mode */
	ctlr = readw(&priv->regs->ctlr);
	ctlr |= FIELD_PREP(RCAR_CAN_CTLR_CANM, RCAR_CAN_CTLR_CANM_FORCE_RESET);
	writew(ctlr, &priv->regs->ctlr);
	for (i = 0; i < MAX_STR_READS; i++) {
		if (readw(&priv->regs->str) & RCAR_CAN_STR_RSTST)
			break;
	}
	writel(0, &priv->regs->mier0);
	writel(0, &priv->regs->mier1);
	writeb(0, &priv->regs->ier);
	writeb(0, &priv->regs->eier);
	/* Go to sleep mode */
	ctlr |= RCAR_CAN_CTLR_SLPM;
	writew(ctlr, &priv->regs->ctlr);
	priv->can.state = CAN_STATE_STOPPED;
}

static int rcar_can_close(struct net_device *ndev)
{
	struct rcar_can_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);
	rcar_can_stop(ndev);
	free_irq(ndev->irq, ndev);
	napi_disable(&priv->napi);
	clk_disable_unprepare(priv->can_clk);
	pm_runtime_put(ndev->dev.parent);
	close_candev(ndev);
	return 0;
}

static netdev_tx_t rcar_can_start_xmit(struct sk_buff *skb,
				       struct net_device *ndev)
{
	struct rcar_can_priv *priv = netdev_priv(ndev);
	struct can_frame *cf = (struct can_frame *)skb->data;
	u32 data, i;

	if (can_dev_dropped_skb(ndev, skb))
		return NETDEV_TX_OK;

	if (cf->can_id & CAN_EFF_FLAG)	/* Extended frame format */
		data = FIELD_PREP(RCAR_CAN_EID, cf->can_id & CAN_EFF_MASK) |
		       RCAR_CAN_IDE;
	else				/* Standard frame format */
		data = FIELD_PREP(RCAR_CAN_SID, cf->can_id & CAN_SFF_MASK);

	if (cf->can_id & CAN_RTR_FLAG) { /* Remote transmission request */
		data |= RCAR_CAN_RTR;
	} else {
		for (i = 0; i < cf->len; i++)
			writeb(cf->data[i],
			       &priv->regs->mb[RCAR_CAN_TX_FIFO_MBX].data[i]);
	}

	writel(data, &priv->regs->mb[RCAR_CAN_TX_FIFO_MBX].id);

	writeb(cf->len, &priv->regs->mb[RCAR_CAN_TX_FIFO_MBX].dlc);

	can_put_echo_skb(skb, ndev, priv->tx_head % RCAR_CAN_FIFO_DEPTH, 0);
	priv->tx_head++;
	/* Start Tx: write 0xff to the TFPCR register to increment
	 * the CPU-side pointer for the transmit FIFO to the next
	 * mailbox location
	 */
	writeb(0xff, &priv->regs->tfpcr);
	/* Stop the queue if we've filled all FIFO entries */
	if (priv->tx_head - priv->tx_tail >= RCAR_CAN_FIFO_DEPTH)
		netif_stop_queue(ndev);

	return NETDEV_TX_OK;
}

static const struct net_device_ops rcar_can_netdev_ops = {
	.ndo_open = rcar_can_open,
	.ndo_stop = rcar_can_close,
	.ndo_start_xmit = rcar_can_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static const struct ethtool_ops rcar_can_ethtool_ops = {
	.get_ts_info = ethtool_op_get_ts_info,
};

static void rcar_can_rx_pkt(struct rcar_can_priv *priv)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	u32 data;
	u8 dlc;

	skb = alloc_can_skb(priv->ndev, &cf);
	if (!skb) {
		stats->rx_dropped++;
		return;
	}

	data = readl(&priv->regs->mb[RCAR_CAN_RX_FIFO_MBX].id);
	if (data & RCAR_CAN_IDE)
		cf->can_id = FIELD_GET(RCAR_CAN_EID, data) | CAN_EFF_FLAG;
	else
		cf->can_id = FIELD_GET(RCAR_CAN_SID, data);

	dlc = readb(&priv->regs->mb[RCAR_CAN_RX_FIFO_MBX].dlc);
	cf->len = can_cc_dlc2len(dlc);
	if (data & RCAR_CAN_RTR) {
		cf->can_id |= CAN_RTR_FLAG;
	} else {
		for (dlc = 0; dlc < cf->len; dlc++)
			cf->data[dlc] =
			readb(&priv->regs->mb[RCAR_CAN_RX_FIFO_MBX].data[dlc]);

		stats->rx_bytes += cf->len;
	}
	stats->rx_packets++;

	netif_receive_skb(skb);
}

static int rcar_can_rx_poll(struct napi_struct *napi, int quota)
{
	struct rcar_can_priv *priv = container_of(napi,
						  struct rcar_can_priv, napi);
	int num_pkts;

	for (num_pkts = 0; num_pkts < quota; num_pkts++) {
		u8 rfcr, isr;

		isr = readb(&priv->regs->isr);
		/* Clear interrupt bit */
		if (isr & RCAR_CAN_ISR_RXFF)
			writeb(isr & ~RCAR_CAN_ISR_RXFF, &priv->regs->isr);
		rfcr = readb(&priv->regs->rfcr);
		if (rfcr & RCAR_CAN_RFCR_RFEST)
			break;
		rcar_can_rx_pkt(priv);
		/* Write 0xff to the RFPCR register to increment
		 * the CPU-side pointer for the receive FIFO
		 * to the next mailbox location
		 */
		writeb(0xff, &priv->regs->rfpcr);
	}
	/* All packets processed */
	if (num_pkts < quota) {
		napi_complete_done(napi, num_pkts);
		priv->ier |= RCAR_CAN_IER_RXFIE;
		writeb(priv->ier, &priv->regs->ier);
	}
	return num_pkts;
}

static int rcar_can_do_set_mode(struct net_device *ndev, enum can_mode mode)
{
	switch (mode) {
	case CAN_MODE_START:
		rcar_can_start(ndev);
		netif_wake_queue(ndev);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int rcar_can_get_berr_counter(const struct net_device *ndev,
				     struct can_berr_counter *bec)
{
	struct rcar_can_priv *priv = netdev_priv(ndev);
	int err;

	err = pm_runtime_resume_and_get(ndev->dev.parent);
	if (err)
		return err;

	bec->txerr = readb(&priv->regs->tecr);
	bec->rxerr = readb(&priv->regs->recr);

	pm_runtime_put(ndev->dev.parent);

	return 0;
}

static const char * const clock_names[] = {
	[CLKR_CLKP1]	= "clkp1",
	[CLKR_CLKP2]	= "clkp2",
	[CLKR_CLKEXT]	= "can_clk",
};

static int rcar_can_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rcar_can_priv *priv;
	struct net_device *ndev;
	void __iomem *addr;
	u32 clock_select = CLKR_CLKP1;
	int err = -ENODEV;
	int irq;

	of_property_read_u32(dev->of_node, "renesas,can-clock-select",
			     &clock_select);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		err = irq;
		goto fail;
	}

	addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(addr)) {
		err = PTR_ERR(addr);
		goto fail;
	}

	ndev = alloc_candev(sizeof(struct rcar_can_priv), RCAR_CAN_FIFO_DEPTH);
	if (!ndev) {
		err = -ENOMEM;
		goto fail;
	}

	priv = netdev_priv(ndev);

	if (!(BIT(clock_select) & RCAR_SUPPORTED_CLOCKS)) {
		err = -EINVAL;
		dev_err(dev, "invalid CAN clock selected\n");
		goto fail_clk;
	}
	priv->can_clk = devm_clk_get(dev, clock_names[clock_select]);
	if (IS_ERR(priv->can_clk)) {
		dev_err(dev, "cannot get CAN clock: %pe\n", priv->can_clk);
		err = PTR_ERR(priv->can_clk);
		goto fail_clk;
	}

	ndev->netdev_ops = &rcar_can_netdev_ops;
	ndev->ethtool_ops = &rcar_can_ethtool_ops;
	ndev->irq = irq;
	ndev->flags |= IFF_ECHO;
	priv->ndev = ndev;
	priv->regs = addr;
	priv->clock_select = clock_select;
	priv->can.clock.freq = clk_get_rate(priv->can_clk);
	priv->can.bittiming_const = &rcar_can_bittiming_const;
	priv->can.do_set_mode = rcar_can_do_set_mode;
	priv->can.do_get_berr_counter = rcar_can_get_berr_counter;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_BERR_REPORTING;
	platform_set_drvdata(pdev, ndev);
	SET_NETDEV_DEV(ndev, dev);

	netif_napi_add_weight(ndev, &priv->napi, rcar_can_rx_poll,
			      RCAR_CAN_NAPI_WEIGHT);

	pm_runtime_enable(dev);

	err = register_candev(ndev);
	if (err) {
		dev_err(dev, "register_candev() failed %pe\n", ERR_PTR(err));
		goto fail_rpm;
	}

	dev_info(dev, "device registered (IRQ%d)\n", ndev->irq);

	return 0;
fail_rpm:
	pm_runtime_disable(dev);
	netif_napi_del(&priv->napi);
fail_clk:
	free_candev(ndev);
fail:
	return err;
}

static void rcar_can_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct rcar_can_priv *priv = netdev_priv(ndev);

	unregister_candev(ndev);
	pm_runtime_disable(&pdev->dev);
	netif_napi_del(&priv->napi);
	free_candev(ndev);
}

static int rcar_can_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct rcar_can_priv *priv = netdev_priv(ndev);
	u16 ctlr;

	if (!netif_running(ndev))
		return 0;

	netif_stop_queue(ndev);
	netif_device_detach(ndev);

	ctlr = readw(&priv->regs->ctlr);
	ctlr |= FIELD_PREP(RCAR_CAN_CTLR_CANM, RCAR_CAN_CTLR_CANM_HALT);
	writew(ctlr, &priv->regs->ctlr);
	ctlr |= RCAR_CAN_CTLR_SLPM;
	writew(ctlr, &priv->regs->ctlr);
	priv->can.state = CAN_STATE_SLEEPING;

	pm_runtime_put(dev);
	return 0;
}

static int rcar_can_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	int err;

	if (!netif_running(ndev))
		return 0;

	err = pm_runtime_resume_and_get(dev);
	if (err) {
		netdev_err(ndev, "pm_runtime_resume_and_get() failed %pe\n",
			   ERR_PTR(err));
		return err;
	}

	rcar_can_start(ndev);

	netif_device_attach(ndev);
	netif_start_queue(ndev);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(rcar_can_pm_ops, rcar_can_suspend,
				rcar_can_resume);

static const struct of_device_id rcar_can_of_table[] __maybe_unused = {
	{ .compatible = "renesas,can-r8a7778" },
	{ .compatible = "renesas,can-r8a7779" },
	{ .compatible = "renesas,can-r8a7790" },
	{ .compatible = "renesas,can-r8a7791" },
	{ .compatible = "renesas,rcar-gen1-can" },
	{ .compatible = "renesas,rcar-gen2-can" },
	{ .compatible = "renesas,rcar-gen3-can" },
	{ }
};
MODULE_DEVICE_TABLE(of, rcar_can_of_table);

static struct platform_driver rcar_can_driver = {
	.driver = {
		.name = RCAR_CAN_DRV_NAME,
		.of_match_table = of_match_ptr(rcar_can_of_table),
		.pm = pm_sleep_ptr(&rcar_can_pm_ops),
	},
	.probe = rcar_can_probe,
	.remove = rcar_can_remove,
};

module_platform_driver(rcar_can_driver);

MODULE_AUTHOR("Cogent Embedded, Inc.");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CAN driver for Renesas R-Car SoC");
MODULE_ALIAS("platform:" RCAR_CAN_DRV_NAME);
