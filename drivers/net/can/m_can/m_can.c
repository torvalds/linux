/*
 * CAN bus driver for Bosch M_CAN controller
 *
 * Copyright (C) 2014 Freescale Semiconductor, Inc.
 *	Dong Aisheng <b29396@freescale.com>
 *
 * Bosch M_CAN user manual can be obtained from:
 * http://www.bosch-semiconductors.de/media/pdf_1/ipmodules_1/m_can/
 * mcan_users_manual_v302.pdf
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <linux/can/dev.h>

/* napi related */
#define M_CAN_NAPI_WEIGHT	64

/* message ram configuration data length */
#define MRAM_CFG_LEN	8

/* registers definition */
enum m_can_reg {
	M_CAN_CREL	= 0x0,
	M_CAN_ENDN	= 0x4,
	M_CAN_CUST	= 0x8,
	M_CAN_FBTP	= 0xc,
	M_CAN_TEST	= 0x10,
	M_CAN_RWD	= 0x14,
	M_CAN_CCCR	= 0x18,
	M_CAN_BTP	= 0x1c,
	M_CAN_TSCC	= 0x20,
	M_CAN_TSCV	= 0x24,
	M_CAN_TOCC	= 0x28,
	M_CAN_TOCV	= 0x2c,
	M_CAN_ECR	= 0x40,
	M_CAN_PSR	= 0x44,
	M_CAN_IR	= 0x50,
	M_CAN_IE	= 0x54,
	M_CAN_ILS	= 0x58,
	M_CAN_ILE	= 0x5c,
	M_CAN_GFC	= 0x80,
	M_CAN_SIDFC	= 0x84,
	M_CAN_XIDFC	= 0x88,
	M_CAN_XIDAM	= 0x90,
	M_CAN_HPMS	= 0x94,
	M_CAN_NDAT1	= 0x98,
	M_CAN_NDAT2	= 0x9c,
	M_CAN_RXF0C	= 0xa0,
	M_CAN_RXF0S	= 0xa4,
	M_CAN_RXF0A	= 0xa8,
	M_CAN_RXBC	= 0xac,
	M_CAN_RXF1C	= 0xb0,
	M_CAN_RXF1S	= 0xb4,
	M_CAN_RXF1A	= 0xb8,
	M_CAN_RXESC	= 0xbc,
	M_CAN_TXBC	= 0xc0,
	M_CAN_TXFQS	= 0xc4,
	M_CAN_TXESC	= 0xc8,
	M_CAN_TXBRP	= 0xcc,
	M_CAN_TXBAR	= 0xd0,
	M_CAN_TXBCR	= 0xd4,
	M_CAN_TXBTO	= 0xd8,
	M_CAN_TXBCF	= 0xdc,
	M_CAN_TXBTIE	= 0xe0,
	M_CAN_TXBCIE	= 0xe4,
	M_CAN_TXEFC	= 0xf0,
	M_CAN_TXEFS	= 0xf4,
	M_CAN_TXEFA	= 0xf8,
};

/* m_can lec values */
enum m_can_lec_type {
	LEC_NO_ERROR = 0,
	LEC_STUFF_ERROR,
	LEC_FORM_ERROR,
	LEC_ACK_ERROR,
	LEC_BIT1_ERROR,
	LEC_BIT0_ERROR,
	LEC_CRC_ERROR,
	LEC_UNUSED,
};

enum m_can_mram_cfg {
	MRAM_SIDF = 0,
	MRAM_XIDF,
	MRAM_RXF0,
	MRAM_RXF1,
	MRAM_RXB,
	MRAM_TXE,
	MRAM_TXB,
	MRAM_CFG_NUM,
};

/* Fast Bit Timing & Prescaler Register (FBTP) */
#define FBTR_FBRP_MASK		0x1f
#define FBTR_FBRP_SHIFT		16
#define FBTR_FTSEG1_SHIFT	8
#define FBTR_FTSEG1_MASK	(0xf << FBTR_FTSEG1_SHIFT)
#define FBTR_FTSEG2_SHIFT	4
#define FBTR_FTSEG2_MASK	(0x7 << FBTR_FTSEG2_SHIFT)
#define FBTR_FSJW_SHIFT		0
#define FBTR_FSJW_MASK		0x3

/* Test Register (TEST) */
#define TEST_LBCK	BIT(4)

/* CC Control Register(CCCR) */
#define CCCR_TEST		BIT(7)
#define CCCR_CMR_MASK		0x3
#define CCCR_CMR_SHIFT		10
#define CCCR_CMR_CANFD		0x1
#define CCCR_CMR_CANFD_BRS	0x2
#define CCCR_CMR_CAN		0x3
#define CCCR_CME_MASK		0x3
#define CCCR_CME_SHIFT		8
#define CCCR_CME_CAN		0
#define CCCR_CME_CANFD		0x1
#define CCCR_CME_CANFD_BRS	0x2
#define CCCR_TEST		BIT(7)
#define CCCR_MON		BIT(5)
#define CCCR_CCE		BIT(1)
#define CCCR_INIT		BIT(0)
#define CCCR_CANFD		0x10

/* Bit Timing & Prescaler Register (BTP) */
#define BTR_BRP_MASK		0x3ff
#define BTR_BRP_SHIFT		16
#define BTR_TSEG1_SHIFT		8
#define BTR_TSEG1_MASK		(0x3f << BTR_TSEG1_SHIFT)
#define BTR_TSEG2_SHIFT		4
#define BTR_TSEG2_MASK		(0xf << BTR_TSEG2_SHIFT)
#define BTR_SJW_SHIFT		0
#define BTR_SJW_MASK		0xf

/* Error Counter Register(ECR) */
#define ECR_RP			BIT(15)
#define ECR_REC_SHIFT		8
#define ECR_REC_MASK		(0x7f << ECR_REC_SHIFT)
#define ECR_TEC_SHIFT		0
#define ECR_TEC_MASK		0xff

/* Protocol Status Register(PSR) */
#define PSR_BO		BIT(7)
#define PSR_EW		BIT(6)
#define PSR_EP		BIT(5)
#define PSR_LEC_MASK	0x7

/* Interrupt Register(IR) */
#define IR_ALL_INT	0xffffffff
#define IR_STE		BIT(31)
#define IR_FOE		BIT(30)
#define IR_ACKE		BIT(29)
#define IR_BE		BIT(28)
#define IR_CRCE		BIT(27)
#define IR_WDI		BIT(26)
#define IR_BO		BIT(25)
#define IR_EW		BIT(24)
#define IR_EP		BIT(23)
#define IR_ELO		BIT(22)
#define IR_BEU		BIT(21)
#define IR_BEC		BIT(20)
#define IR_DRX		BIT(19)
#define IR_TOO		BIT(18)
#define IR_MRAF		BIT(17)
#define IR_TSW		BIT(16)
#define IR_TEFL		BIT(15)
#define IR_TEFF		BIT(14)
#define IR_TEFW		BIT(13)
#define IR_TEFN		BIT(12)
#define IR_TFE		BIT(11)
#define IR_TCF		BIT(10)
#define IR_TC		BIT(9)
#define IR_HPM		BIT(8)
#define IR_RF1L		BIT(7)
#define IR_RF1F		BIT(6)
#define IR_RF1W		BIT(5)
#define IR_RF1N		BIT(4)
#define IR_RF0L		BIT(3)
#define IR_RF0F		BIT(2)
#define IR_RF0W		BIT(1)
#define IR_RF0N		BIT(0)
#define IR_ERR_STATE	(IR_BO | IR_EW | IR_EP)
#define IR_ERR_LEC	(IR_STE	| IR_FOE | IR_ACKE | IR_BE | IR_CRCE)
#define IR_ERR_BUS	(IR_ERR_LEC | IR_WDI | IR_ELO | IR_BEU | \
			 IR_BEC | IR_TOO | IR_MRAF | IR_TSW | IR_TEFL | \
			 IR_RF1L | IR_RF0L)
#define IR_ERR_ALL	(IR_ERR_STATE | IR_ERR_BUS)

/* Interrupt Line Select (ILS) */
#define ILS_ALL_INT0	0x0
#define ILS_ALL_INT1	0xFFFFFFFF

/* Interrupt Line Enable (ILE) */
#define ILE_EINT0	BIT(0)
#define ILE_EINT1	BIT(1)

/* Rx FIFO 0/1 Configuration (RXF0C/RXF1C) */
#define RXFC_FWM_OFF	24
#define RXFC_FWM_MASK	0x7f
#define RXFC_FWM_1	(1 << RXFC_FWM_OFF)
#define RXFC_FS_OFF	16
#define RXFC_FS_MASK	0x7f

/* Rx FIFO 0/1 Status (RXF0S/RXF1S) */
#define RXFS_RFL	BIT(25)
#define RXFS_FF		BIT(24)
#define RXFS_FPI_OFF	16
#define RXFS_FPI_MASK	0x3f0000
#define RXFS_FGI_OFF	8
#define RXFS_FGI_MASK	0x3f00
#define RXFS_FFL_MASK	0x7f

/* Rx Buffer / FIFO Element Size Configuration (RXESC) */
#define M_CAN_RXESC_8BYTES	0x0
#define M_CAN_RXESC_64BYTES	0x777

/* Tx Buffer Configuration(TXBC) */
#define TXBC_NDTB_OFF		16
#define TXBC_NDTB_MASK		0x3f

/* Tx Buffer Element Size Configuration(TXESC) */
#define TXESC_TBDS_8BYTES	0x0
#define TXESC_TBDS_64BYTES	0x7

/* Tx Event FIFO Con.guration (TXEFC) */
#define TXEFC_EFS_OFF		16
#define TXEFC_EFS_MASK		0x3f

/* Message RAM Configuration (in bytes) */
#define SIDF_ELEMENT_SIZE	4
#define XIDF_ELEMENT_SIZE	8
#define RXF0_ELEMENT_SIZE	72
#define RXF1_ELEMENT_SIZE	72
#define RXB_ELEMENT_SIZE	16
#define TXE_ELEMENT_SIZE	8
#define TXB_ELEMENT_SIZE	72

/* Message RAM Elements */
#define M_CAN_FIFO_ID		0x0
#define M_CAN_FIFO_DLC		0x4
#define M_CAN_FIFO_DATA(n)	(0x8 + ((n) << 2))

/* Rx Buffer Element */
/* R0 */
#define RX_BUF_ESI		BIT(31)
#define RX_BUF_XTD		BIT(30)
#define RX_BUF_RTR		BIT(29)
/* R1 */
#define RX_BUF_ANMF		BIT(31)
#define RX_BUF_EDL		BIT(21)
#define RX_BUF_BRS		BIT(20)

/* Tx Buffer Element */
/* R0 */
#define TX_BUF_XTD		BIT(30)
#define TX_BUF_RTR		BIT(29)

/* address offset and element number for each FIFO/Buffer in the Message RAM */
struct mram_cfg {
	u16 off;
	u8  num;
};

/* m_can private data structure */
struct m_can_priv {
	struct can_priv can;	/* must be the first member */
	struct napi_struct napi;
	struct net_device *dev;
	struct device *device;
	struct clk *hclk;
	struct clk *cclk;
	void __iomem *base;
	u32 irqstatus;

	/* message ram configuration */
	void __iomem *mram_base;
	struct mram_cfg mcfg[MRAM_CFG_NUM];
};

static inline u32 m_can_read(const struct m_can_priv *priv, enum m_can_reg reg)
{
	return readl(priv->base + reg);
}

static inline void m_can_write(const struct m_can_priv *priv,
			       enum m_can_reg reg, u32 val)
{
	writel(val, priv->base + reg);
}

static inline u32 m_can_fifo_read(const struct m_can_priv *priv,
				  u32 fgi, unsigned int offset)
{
	return readl(priv->mram_base + priv->mcfg[MRAM_RXF0].off +
		     fgi * RXF0_ELEMENT_SIZE + offset);
}

static inline void m_can_fifo_write(const struct m_can_priv *priv,
				    u32 fpi, unsigned int offset, u32 val)
{
	return writel(val, priv->mram_base + priv->mcfg[MRAM_TXB].off +
		      fpi * TXB_ELEMENT_SIZE + offset);
}

static inline void m_can_config_endisable(const struct m_can_priv *priv,
					  bool enable)
{
	u32 cccr = m_can_read(priv, M_CAN_CCCR);
	u32 timeout = 10;
	u32 val = 0;

	if (enable) {
		/* enable m_can configuration */
		m_can_write(priv, M_CAN_CCCR, cccr | CCCR_INIT);
		udelay(5);
		/* CCCR.CCE can only be set/reset while CCCR.INIT = '1' */
		m_can_write(priv, M_CAN_CCCR, cccr | CCCR_INIT | CCCR_CCE);
	} else {
		m_can_write(priv, M_CAN_CCCR, cccr & ~(CCCR_INIT | CCCR_CCE));
	}

	/* there's a delay for module initialization */
	if (enable)
		val = CCCR_INIT | CCCR_CCE;

	while ((m_can_read(priv, M_CAN_CCCR) & (CCCR_INIT | CCCR_CCE)) != val) {
		if (timeout == 0) {
			netdev_warn(priv->dev, "Failed to init module\n");
			return;
		}
		timeout--;
		udelay(1);
	}
}

static inline void m_can_enable_all_interrupts(const struct m_can_priv *priv)
{
	m_can_write(priv, M_CAN_ILE, ILE_EINT0 | ILE_EINT1);
}

static inline void m_can_disable_all_interrupts(const struct m_can_priv *priv)
{
	m_can_write(priv, M_CAN_ILE, 0x0);
}

static void m_can_read_fifo(struct net_device *dev, u32 rxfs)
{
	struct net_device_stats *stats = &dev->stats;
	struct m_can_priv *priv = netdev_priv(dev);
	struct canfd_frame *cf;
	struct sk_buff *skb;
	u32 id, fgi, dlc;
	int i;

	/* calculate the fifo get index for where to read data */
	fgi = (rxfs & RXFS_FGI_MASK) >> RXFS_FGI_OFF;
	dlc = m_can_fifo_read(priv, fgi, M_CAN_FIFO_DLC);
	if (dlc & RX_BUF_EDL)
		skb = alloc_canfd_skb(dev, &cf);
	else
		skb = alloc_can_skb(dev, (struct can_frame **)&cf);
	if (!skb) {
		stats->rx_dropped++;
		return;
	}

	if (dlc & RX_BUF_EDL)
		cf->len = can_dlc2len((dlc >> 16) & 0x0F);
	else
		cf->len = get_can_dlc((dlc >> 16) & 0x0F);

	id = m_can_fifo_read(priv, fgi, M_CAN_FIFO_ID);
	if (id & RX_BUF_XTD)
		cf->can_id = (id & CAN_EFF_MASK) | CAN_EFF_FLAG;
	else
		cf->can_id = (id >> 18) & CAN_SFF_MASK;

	if (id & RX_BUF_ESI) {
		cf->flags |= CANFD_ESI;
		netdev_dbg(dev, "ESI Error\n");
	}

	if (!(dlc & RX_BUF_EDL) && (id & RX_BUF_RTR)) {
		cf->can_id |= CAN_RTR_FLAG;
	} else {
		if (dlc & RX_BUF_BRS)
			cf->flags |= CANFD_BRS;

		for (i = 0; i < cf->len; i += 4)
			*(u32 *)(cf->data + i) =
				m_can_fifo_read(priv, fgi,
						M_CAN_FIFO_DATA(i / 4));
	}

	/* acknowledge rx fifo 0 */
	m_can_write(priv, M_CAN_RXF0A, fgi);

	stats->rx_packets++;
	stats->rx_bytes += cf->len;

	netif_receive_skb(skb);
}

static int m_can_do_rx_poll(struct net_device *dev, int quota)
{
	struct m_can_priv *priv = netdev_priv(dev);
	u32 pkts = 0;
	u32 rxfs;

	rxfs = m_can_read(priv, M_CAN_RXF0S);
	if (!(rxfs & RXFS_FFL_MASK)) {
		netdev_dbg(dev, "no messages in fifo0\n");
		return 0;
	}

	while ((rxfs & RXFS_FFL_MASK) && (quota > 0)) {
		if (rxfs & RXFS_RFL)
			netdev_warn(dev, "Rx FIFO 0 Message Lost\n");

		m_can_read_fifo(dev, rxfs);

		quota--;
		pkts++;
		rxfs = m_can_read(priv, M_CAN_RXF0S);
	}

	if (pkts)
		can_led_event(dev, CAN_LED_EVENT_RX);

	return pkts;
}

static int m_can_handle_lost_msg(struct net_device *dev)
{
	struct net_device_stats *stats = &dev->stats;
	struct sk_buff *skb;
	struct can_frame *frame;

	netdev_err(dev, "msg lost in rxf0\n");

	stats->rx_errors++;
	stats->rx_over_errors++;

	skb = alloc_can_err_skb(dev, &frame);
	if (unlikely(!skb))
		return 0;

	frame->can_id |= CAN_ERR_CRTL;
	frame->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;

	netif_receive_skb(skb);

	return 1;
}

static int m_can_handle_lec_err(struct net_device *dev,
				enum m_can_lec_type lec_type)
{
	struct m_can_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;

	priv->can.can_stats.bus_error++;
	stats->rx_errors++;

	/* propagate the error condition to the CAN stack */
	skb = alloc_can_err_skb(dev, &cf);
	if (unlikely(!skb))
		return 0;

	/* check for 'last error code' which tells us the
	 * type of the last error to occur on the CAN bus
	 */
	cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;
	cf->data[2] |= CAN_ERR_PROT_UNSPEC;

	switch (lec_type) {
	case LEC_STUFF_ERROR:
		netdev_dbg(dev, "stuff error\n");
		cf->data[2] |= CAN_ERR_PROT_STUFF;
		break;
	case LEC_FORM_ERROR:
		netdev_dbg(dev, "form error\n");
		cf->data[2] |= CAN_ERR_PROT_FORM;
		break;
	case LEC_ACK_ERROR:
		netdev_dbg(dev, "ack error\n");
		cf->data[3] |= (CAN_ERR_PROT_LOC_ACK |
				CAN_ERR_PROT_LOC_ACK_DEL);
		break;
	case LEC_BIT1_ERROR:
		netdev_dbg(dev, "bit1 error\n");
		cf->data[2] |= CAN_ERR_PROT_BIT1;
		break;
	case LEC_BIT0_ERROR:
		netdev_dbg(dev, "bit0 error\n");
		cf->data[2] |= CAN_ERR_PROT_BIT0;
		break;
	case LEC_CRC_ERROR:
		netdev_dbg(dev, "CRC error\n");
		cf->data[3] |= (CAN_ERR_PROT_LOC_CRC_SEQ |
				CAN_ERR_PROT_LOC_CRC_DEL);
		break;
	default:
		break;
	}

	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;
	netif_receive_skb(skb);

	return 1;
}

static int __m_can_get_berr_counter(const struct net_device *dev,
				    struct can_berr_counter *bec)
{
	struct m_can_priv *priv = netdev_priv(dev);
	unsigned int ecr;

	ecr = m_can_read(priv, M_CAN_ECR);
	bec->rxerr = (ecr & ECR_REC_MASK) >> ECR_REC_SHIFT;
	bec->txerr = ecr & ECR_TEC_MASK;

	return 0;
}

static int m_can_get_berr_counter(const struct net_device *dev,
				  struct can_berr_counter *bec)
{
	struct m_can_priv *priv = netdev_priv(dev);
	int err;

	err = clk_prepare_enable(priv->hclk);
	if (err)
		return err;

	err = clk_prepare_enable(priv->cclk);
	if (err) {
		clk_disable_unprepare(priv->hclk);
		return err;
	}

	__m_can_get_berr_counter(dev, bec);

	clk_disable_unprepare(priv->cclk);
	clk_disable_unprepare(priv->hclk);

	return 0;
}

static int m_can_handle_state_change(struct net_device *dev,
				     enum can_state new_state)
{
	struct m_can_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	struct can_berr_counter bec;
	unsigned int ecr;

	switch (new_state) {
	case CAN_STATE_ERROR_ACTIVE:
		/* error warning state */
		priv->can.can_stats.error_warning++;
		priv->can.state = CAN_STATE_ERROR_WARNING;
		break;
	case CAN_STATE_ERROR_PASSIVE:
		/* error passive state */
		priv->can.can_stats.error_passive++;
		priv->can.state = CAN_STATE_ERROR_PASSIVE;
		break;
	case CAN_STATE_BUS_OFF:
		/* bus-off state */
		priv->can.state = CAN_STATE_BUS_OFF;
		m_can_disable_all_interrupts(priv);
		priv->can.can_stats.bus_off++;
		can_bus_off(dev);
		break;
	default:
		break;
	}

	/* propagate the error condition to the CAN stack */
	skb = alloc_can_err_skb(dev, &cf);
	if (unlikely(!skb))
		return 0;

	__m_can_get_berr_counter(dev, &bec);

	switch (new_state) {
	case CAN_STATE_ERROR_ACTIVE:
		/* error warning state */
		cf->can_id |= CAN_ERR_CRTL;
		cf->data[1] = (bec.txerr > bec.rxerr) ?
			CAN_ERR_CRTL_TX_WARNING :
			CAN_ERR_CRTL_RX_WARNING;
		cf->data[6] = bec.txerr;
		cf->data[7] = bec.rxerr;
		break;
	case CAN_STATE_ERROR_PASSIVE:
		/* error passive state */
		cf->can_id |= CAN_ERR_CRTL;
		ecr = m_can_read(priv, M_CAN_ECR);
		if (ecr & ECR_RP)
			cf->data[1] |= CAN_ERR_CRTL_RX_PASSIVE;
		if (bec.txerr > 127)
			cf->data[1] |= CAN_ERR_CRTL_TX_PASSIVE;
		cf->data[6] = bec.txerr;
		cf->data[7] = bec.rxerr;
		break;
	case CAN_STATE_BUS_OFF:
		/* bus-off state */
		cf->can_id |= CAN_ERR_BUSOFF;
		break;
	default:
		break;
	}

	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;
	netif_receive_skb(skb);

	return 1;
}

static int m_can_handle_state_errors(struct net_device *dev, u32 psr)
{
	struct m_can_priv *priv = netdev_priv(dev);
	int work_done = 0;

	if ((psr & PSR_EW) &&
	    (priv->can.state != CAN_STATE_ERROR_WARNING)) {
		netdev_dbg(dev, "entered error warning state\n");
		work_done += m_can_handle_state_change(dev,
						       CAN_STATE_ERROR_WARNING);
	}

	if ((psr & PSR_EP) &&
	    (priv->can.state != CAN_STATE_ERROR_PASSIVE)) {
		netdev_dbg(dev, "entered error passive state\n");
		work_done += m_can_handle_state_change(dev,
						       CAN_STATE_ERROR_PASSIVE);
	}

	if ((psr & PSR_BO) &&
	    (priv->can.state != CAN_STATE_BUS_OFF)) {
		netdev_dbg(dev, "entered error bus off state\n");
		work_done += m_can_handle_state_change(dev,
						       CAN_STATE_BUS_OFF);
	}

	return work_done;
}

static void m_can_handle_other_err(struct net_device *dev, u32 irqstatus)
{
	if (irqstatus & IR_WDI)
		netdev_err(dev, "Message RAM Watchdog event due to missing READY\n");
	if (irqstatus & IR_ELO)
		netdev_err(dev, "Error Logging Overflow\n");
	if (irqstatus & IR_BEU)
		netdev_err(dev, "Bit Error Uncorrected\n");
	if (irqstatus & IR_BEC)
		netdev_err(dev, "Bit Error Corrected\n");
	if (irqstatus & IR_TOO)
		netdev_err(dev, "Timeout reached\n");
	if (irqstatus & IR_MRAF)
		netdev_err(dev, "Message RAM access failure occurred\n");
}

static inline bool is_lec_err(u32 psr)
{
	psr &= LEC_UNUSED;

	return psr && (psr != LEC_UNUSED);
}

static int m_can_handle_bus_errors(struct net_device *dev, u32 irqstatus,
				   u32 psr)
{
	struct m_can_priv *priv = netdev_priv(dev);
	int work_done = 0;

	if (irqstatus & IR_RF0L)
		work_done += m_can_handle_lost_msg(dev);

	/* handle lec errors on the bus */
	if ((priv->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING) &&
	    is_lec_err(psr))
		work_done += m_can_handle_lec_err(dev, psr & LEC_UNUSED);

	/* other unproccessed error interrupts */
	m_can_handle_other_err(dev, irqstatus);

	return work_done;
}

static int m_can_poll(struct napi_struct *napi, int quota)
{
	struct net_device *dev = napi->dev;
	struct m_can_priv *priv = netdev_priv(dev);
	int work_done = 0;
	u32 irqstatus, psr;

	irqstatus = priv->irqstatus | m_can_read(priv, M_CAN_IR);
	if (!irqstatus)
		goto end;

	psr = m_can_read(priv, M_CAN_PSR);
	if (irqstatus & IR_ERR_STATE)
		work_done += m_can_handle_state_errors(dev, psr);

	if (irqstatus & IR_ERR_BUS)
		work_done += m_can_handle_bus_errors(dev, irqstatus, psr);

	if (irqstatus & IR_RF0N)
		work_done += m_can_do_rx_poll(dev, (quota - work_done));

	if (work_done < quota) {
		napi_complete(napi);
		m_can_enable_all_interrupts(priv);
	}

end:
	return work_done;
}

static irqreturn_t m_can_isr(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct m_can_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	u32 ir;

	ir = m_can_read(priv, M_CAN_IR);
	if (!ir)
		return IRQ_NONE;

	/* ACK all irqs */
	if (ir & IR_ALL_INT)
		m_can_write(priv, M_CAN_IR, ir);

	/* schedule NAPI in case of
	 * - rx IRQ
	 * - state change IRQ
	 * - bus error IRQ and bus error reporting
	 */
	if ((ir & IR_RF0N) || (ir & IR_ERR_ALL)) {
		priv->irqstatus = ir;
		m_can_disable_all_interrupts(priv);
		napi_schedule(&priv->napi);
	}

	/* transmission complete interrupt */
	if (ir & IR_TC) {
		stats->tx_bytes += can_get_echo_skb(dev, 0);
		stats->tx_packets++;
		can_led_event(dev, CAN_LED_EVENT_TX);
		netif_wake_queue(dev);
	}

	return IRQ_HANDLED;
}

static const struct can_bittiming_const m_can_bittiming_const = {
	.name = KBUILD_MODNAME,
	.tseg1_min = 2,		/* Time segment 1 = prop_seg + phase_seg1 */
	.tseg1_max = 64,
	.tseg2_min = 1,		/* Time segment 2 = phase_seg2 */
	.tseg2_max = 16,
	.sjw_max = 16,
	.brp_min = 1,
	.brp_max = 1024,
	.brp_inc = 1,
};

static const struct can_bittiming_const m_can_data_bittiming_const = {
	.name = KBUILD_MODNAME,
	.tseg1_min = 2,		/* Time segment 1 = prop_seg + phase_seg1 */
	.tseg1_max = 16,
	.tseg2_min = 1,		/* Time segment 2 = phase_seg2 */
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 32,
	.brp_inc = 1,
};

static int m_can_set_bittiming(struct net_device *dev)
{
	struct m_can_priv *priv = netdev_priv(dev);
	const struct can_bittiming *bt = &priv->can.bittiming;
	const struct can_bittiming *dbt = &priv->can.data_bittiming;
	u16 brp, sjw, tseg1, tseg2;
	u32 reg_btp;

	brp = bt->brp - 1;
	sjw = bt->sjw - 1;
	tseg1 = bt->prop_seg + bt->phase_seg1 - 1;
	tseg2 = bt->phase_seg2 - 1;
	reg_btp = (brp << BTR_BRP_SHIFT) | (sjw << BTR_SJW_SHIFT) |
			(tseg1 << BTR_TSEG1_SHIFT) | (tseg2 << BTR_TSEG2_SHIFT);
	m_can_write(priv, M_CAN_BTP, reg_btp);

	if (priv->can.ctrlmode & CAN_CTRLMODE_FD) {
		brp = dbt->brp - 1;
		sjw = dbt->sjw - 1;
		tseg1 = dbt->prop_seg + dbt->phase_seg1 - 1;
		tseg2 = dbt->phase_seg2 - 1;
		reg_btp = (brp << FBTR_FBRP_SHIFT) | (sjw << FBTR_FSJW_SHIFT) |
				(tseg1 << FBTR_FTSEG1_SHIFT) |
				(tseg2 << FBTR_FTSEG2_SHIFT);
		m_can_write(priv, M_CAN_FBTP, reg_btp);
	}

	return 0;
}

/* Configure M_CAN chip:
 * - set rx buffer/fifo element size
 * - configure rx fifo
 * - accept non-matching frame into fifo 0
 * - configure tx buffer
 * - configure mode
 * - setup bittiming
 */
static void m_can_chip_config(struct net_device *dev)
{
	struct m_can_priv *priv = netdev_priv(dev);
	u32 cccr, test;

	m_can_config_endisable(priv, true);

	/* RX Buffer/FIFO Element Size 64 bytes data field */
	m_can_write(priv, M_CAN_RXESC, M_CAN_RXESC_64BYTES);

	/* Accept Non-matching Frames Into FIFO 0 */
	m_can_write(priv, M_CAN_GFC, 0x0);

	/* only support one Tx Buffer currently */
	m_can_write(priv, M_CAN_TXBC, (1 << TXBC_NDTB_OFF) |
		    priv->mcfg[MRAM_TXB].off);

	/* support 64 bytes payload */
	m_can_write(priv, M_CAN_TXESC, TXESC_TBDS_64BYTES);

	m_can_write(priv, M_CAN_TXEFC, (1 << TXEFC_EFS_OFF) |
		    priv->mcfg[MRAM_TXE].off);

	/* rx fifo configuration, blocking mode, fifo size 1 */
	m_can_write(priv, M_CAN_RXF0C,
		    (priv->mcfg[MRAM_RXF0].num << RXFC_FS_OFF) |
		    RXFC_FWM_1 | priv->mcfg[MRAM_RXF0].off);

	m_can_write(priv, M_CAN_RXF1C,
		    (priv->mcfg[MRAM_RXF1].num << RXFC_FS_OFF) |
		    RXFC_FWM_1 | priv->mcfg[MRAM_RXF1].off);

	cccr = m_can_read(priv, M_CAN_CCCR);
	cccr &= ~(CCCR_TEST | CCCR_MON | (CCCR_CMR_MASK << CCCR_CMR_SHIFT) |
		(CCCR_CME_MASK << CCCR_CME_SHIFT));
	test = m_can_read(priv, M_CAN_TEST);
	test &= ~TEST_LBCK;

	if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		cccr |= CCCR_MON;

	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK) {
		cccr |= CCCR_TEST;
		test |= TEST_LBCK;
	}

	if (priv->can.ctrlmode & CAN_CTRLMODE_FD)
		cccr |= CCCR_CME_CANFD_BRS << CCCR_CME_SHIFT;

	m_can_write(priv, M_CAN_CCCR, cccr);
	m_can_write(priv, M_CAN_TEST, test);

	/* enable interrupts */
	m_can_write(priv, M_CAN_IR, IR_ALL_INT);
	if (!(priv->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING))
		m_can_write(priv, M_CAN_IE, IR_ALL_INT & ~IR_ERR_LEC);
	else
		m_can_write(priv, M_CAN_IE, IR_ALL_INT);

	/* route all interrupts to INT0 */
	m_can_write(priv, M_CAN_ILS, ILS_ALL_INT0);

	/* set bittiming params */
	m_can_set_bittiming(dev);

	m_can_config_endisable(priv, false);
}

static void m_can_start(struct net_device *dev)
{
	struct m_can_priv *priv = netdev_priv(dev);

	/* basic m_can configuration */
	m_can_chip_config(dev);

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	m_can_enable_all_interrupts(priv);
}

static int m_can_set_mode(struct net_device *dev, enum can_mode mode)
{
	switch (mode) {
	case CAN_MODE_START:
		m_can_start(dev);
		netif_wake_queue(dev);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static void free_m_can_dev(struct net_device *dev)
{
	free_candev(dev);
}

static struct net_device *alloc_m_can_dev(void)
{
	struct net_device *dev;
	struct m_can_priv *priv;

	dev = alloc_candev(sizeof(*priv), 1);
	if (!dev)
		return NULL;

	priv = netdev_priv(dev);
	netif_napi_add(dev, &priv->napi, m_can_poll, M_CAN_NAPI_WEIGHT);

	priv->dev = dev;
	priv->can.bittiming_const = &m_can_bittiming_const;
	priv->can.data_bittiming_const = &m_can_data_bittiming_const;
	priv->can.do_set_mode = m_can_set_mode;
	priv->can.do_get_berr_counter = m_can_get_berr_counter;

	/* CAN_CTRLMODE_FD_NON_ISO is fixed with M_CAN IP v3.0.1 */
	priv->can.ctrlmode = CAN_CTRLMODE_FD_NON_ISO;

	/* CAN_CTRLMODE_FD_NON_ISO can not be changed with M_CAN IP v3.0.1 */
	priv->can.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK |
					CAN_CTRLMODE_LISTENONLY |
					CAN_CTRLMODE_BERR_REPORTING |
					CAN_CTRLMODE_FD;

	return dev;
}

static int m_can_open(struct net_device *dev)
{
	struct m_can_priv *priv = netdev_priv(dev);
	int err;

	err = clk_prepare_enable(priv->hclk);
	if (err)
		return err;

	err = clk_prepare_enable(priv->cclk);
	if (err)
		goto exit_disable_hclk;

	/* open the can device */
	err = open_candev(dev);
	if (err) {
		netdev_err(dev, "failed to open can device\n");
		goto exit_disable_cclk;
	}

	/* register interrupt handler */
	err = request_irq(dev->irq, m_can_isr, IRQF_SHARED, dev->name,
			  dev);
	if (err < 0) {
		netdev_err(dev, "failed to request interrupt\n");
		goto exit_irq_fail;
	}

	/* start the m_can controller */
	m_can_start(dev);

	can_led_event(dev, CAN_LED_EVENT_OPEN);
	napi_enable(&priv->napi);
	netif_start_queue(dev);

	return 0;

exit_irq_fail:
	close_candev(dev);
exit_disable_cclk:
	clk_disable_unprepare(priv->cclk);
exit_disable_hclk:
	clk_disable_unprepare(priv->hclk);
	return err;
}

static void m_can_stop(struct net_device *dev)
{
	struct m_can_priv *priv = netdev_priv(dev);

	/* disable all interrupts */
	m_can_disable_all_interrupts(priv);

	clk_disable_unprepare(priv->hclk);
	clk_disable_unprepare(priv->cclk);

	/* set the state as STOPPED */
	priv->can.state = CAN_STATE_STOPPED;
}

static int m_can_close(struct net_device *dev)
{
	struct m_can_priv *priv = netdev_priv(dev);

	netif_stop_queue(dev);
	napi_disable(&priv->napi);
	m_can_stop(dev);
	free_irq(dev->irq, dev);
	close_candev(dev);
	can_led_event(dev, CAN_LED_EVENT_STOP);

	return 0;
}

static netdev_tx_t m_can_start_xmit(struct sk_buff *skb,
				    struct net_device *dev)
{
	struct m_can_priv *priv = netdev_priv(dev);
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;
	u32 id, cccr;
	int i;

	if (can_dropped_invalid_skb(dev, skb))
		return NETDEV_TX_OK;

	netif_stop_queue(dev);

	if (cf->can_id & CAN_EFF_FLAG) {
		id = cf->can_id & CAN_EFF_MASK;
		id |= TX_BUF_XTD;
	} else {
		id = ((cf->can_id & CAN_SFF_MASK) << 18);
	}

	if (cf->can_id & CAN_RTR_FLAG)
		id |= TX_BUF_RTR;

	/* message ram configuration */
	m_can_fifo_write(priv, 0, M_CAN_FIFO_ID, id);
	m_can_fifo_write(priv, 0, M_CAN_FIFO_DLC, can_len2dlc(cf->len) << 16);

	for (i = 0; i < cf->len; i += 4)
		m_can_fifo_write(priv, 0, M_CAN_FIFO_DATA(i / 4),
				 *(u32 *)(cf->data + i));

	can_put_echo_skb(skb, dev, 0);

	if (priv->can.ctrlmode & CAN_CTRLMODE_FD) {
		cccr = m_can_read(priv, M_CAN_CCCR);
		cccr &= ~(CCCR_CMR_MASK << CCCR_CMR_SHIFT);
		if (can_is_canfd_skb(skb)) {
			if (cf->flags & CANFD_BRS)
				cccr |= CCCR_CMR_CANFD_BRS << CCCR_CMR_SHIFT;
			else
				cccr |= CCCR_CMR_CANFD << CCCR_CMR_SHIFT;
		} else {
			cccr |= CCCR_CMR_CAN << CCCR_CMR_SHIFT;
		}
		m_can_write(priv, M_CAN_CCCR, cccr);
	}

	/* enable first TX buffer to start transfer  */
	m_can_write(priv, M_CAN_TXBTIE, 0x1);
	m_can_write(priv, M_CAN_TXBAR, 0x1);

	return NETDEV_TX_OK;
}

static const struct net_device_ops m_can_netdev_ops = {
	.ndo_open = m_can_open,
	.ndo_stop = m_can_close,
	.ndo_start_xmit = m_can_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static int register_m_can_dev(struct net_device *dev)
{
	dev->flags |= IFF_ECHO;	/* we support local echo */
	dev->netdev_ops = &m_can_netdev_ops;

	return register_candev(dev);
}

static int m_can_of_parse_mram(struct platform_device *pdev,
			       struct m_can_priv *priv)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	void __iomem *addr;
	u32 out_val[MRAM_CFG_LEN];
	int i, start, end, ret;

	/* message ram could be shared */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "message_ram");
	if (!res)
		return -ENODEV;

	addr = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!addr)
		return -ENOMEM;

	/* get message ram configuration */
	ret = of_property_read_u32_array(np, "bosch,mram-cfg",
					 out_val, sizeof(out_val) / 4);
	if (ret) {
		dev_err(&pdev->dev, "can not get message ram configuration\n");
		return -ENODEV;
	}

	priv->mram_base = addr;
	priv->mcfg[MRAM_SIDF].off = out_val[0];
	priv->mcfg[MRAM_SIDF].num = out_val[1];
	priv->mcfg[MRAM_XIDF].off = priv->mcfg[MRAM_SIDF].off +
			priv->mcfg[MRAM_SIDF].num * SIDF_ELEMENT_SIZE;
	priv->mcfg[MRAM_XIDF].num = out_val[2];
	priv->mcfg[MRAM_RXF0].off = priv->mcfg[MRAM_XIDF].off +
			priv->mcfg[MRAM_XIDF].num * XIDF_ELEMENT_SIZE;
	priv->mcfg[MRAM_RXF0].num = out_val[3] & RXFC_FS_MASK;
	priv->mcfg[MRAM_RXF1].off = priv->mcfg[MRAM_RXF0].off +
			priv->mcfg[MRAM_RXF0].num * RXF0_ELEMENT_SIZE;
	priv->mcfg[MRAM_RXF1].num = out_val[4] & RXFC_FS_MASK;
	priv->mcfg[MRAM_RXB].off = priv->mcfg[MRAM_RXF1].off +
			priv->mcfg[MRAM_RXF1].num * RXF1_ELEMENT_SIZE;
	priv->mcfg[MRAM_RXB].num = out_val[5];
	priv->mcfg[MRAM_TXE].off = priv->mcfg[MRAM_RXB].off +
			priv->mcfg[MRAM_RXB].num * RXB_ELEMENT_SIZE;
	priv->mcfg[MRAM_TXE].num = out_val[6];
	priv->mcfg[MRAM_TXB].off = priv->mcfg[MRAM_TXE].off +
			priv->mcfg[MRAM_TXE].num * TXE_ELEMENT_SIZE;
	priv->mcfg[MRAM_TXB].num = out_val[7] & TXBC_NDTB_MASK;

	dev_dbg(&pdev->dev, "mram_base %p sidf 0x%x %d xidf 0x%x %d rxf0 0x%x %d rxf1 0x%x %d rxb 0x%x %d txe 0x%x %d txb 0x%x %d\n",
		priv->mram_base,
		priv->mcfg[MRAM_SIDF].off, priv->mcfg[MRAM_SIDF].num,
		priv->mcfg[MRAM_XIDF].off, priv->mcfg[MRAM_XIDF].num,
		priv->mcfg[MRAM_RXF0].off, priv->mcfg[MRAM_RXF0].num,
		priv->mcfg[MRAM_RXF1].off, priv->mcfg[MRAM_RXF1].num,
		priv->mcfg[MRAM_RXB].off, priv->mcfg[MRAM_RXB].num,
		priv->mcfg[MRAM_TXE].off, priv->mcfg[MRAM_TXE].num,
		priv->mcfg[MRAM_TXB].off, priv->mcfg[MRAM_TXB].num);

	/* initialize the entire Message RAM in use to avoid possible
	 * ECC/parity checksum errors when reading an uninitialized buffer
	 */
	start = priv->mcfg[MRAM_SIDF].off;
	end = priv->mcfg[MRAM_TXB].off +
		priv->mcfg[MRAM_TXB].num * TXB_ELEMENT_SIZE;
	for (i = start; i < end; i += 4)
		writel(0x0, priv->mram_base + i);

	return 0;
}

static int m_can_plat_probe(struct platform_device *pdev)
{
	struct net_device *dev;
	struct m_can_priv *priv;
	struct resource *res;
	void __iomem *addr;
	struct clk *hclk, *cclk;
	int irq, ret;

	hclk = devm_clk_get(&pdev->dev, "hclk");
	cclk = devm_clk_get(&pdev->dev, "cclk");
	if (IS_ERR(hclk) || IS_ERR(cclk)) {
		dev_err(&pdev->dev, "no clock find\n");
		return -ENODEV;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "m_can");
	addr = devm_ioremap_resource(&pdev->dev, res);
	irq = platform_get_irq_byname(pdev, "int0");
	if (IS_ERR(addr) || irq < 0)
		return -EINVAL;

	/* allocate the m_can device */
	dev = alloc_m_can_dev();
	if (!dev)
		return -ENOMEM;

	priv = netdev_priv(dev);
	dev->irq = irq;
	priv->base = addr;
	priv->device = &pdev->dev;
	priv->hclk = hclk;
	priv->cclk = cclk;
	priv->can.clock.freq = clk_get_rate(cclk);

	ret = m_can_of_parse_mram(pdev, priv);
	if (ret)
		goto failed_free_dev;

	platform_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	ret = register_m_can_dev(dev);
	if (ret) {
		dev_err(&pdev->dev, "registering %s failed (err=%d)\n",
			KBUILD_MODNAME, ret);
		goto failed_free_dev;
	}

	devm_can_led_init(dev);

	dev_info(&pdev->dev, "%s device registered (regs=%p, irq=%d)\n",
		 KBUILD_MODNAME, priv->base, dev->irq);

	return 0;

failed_free_dev:
	free_m_can_dev(dev);
	return ret;
}

static __maybe_unused int m_can_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct m_can_priv *priv = netdev_priv(ndev);

	if (netif_running(ndev)) {
		netif_stop_queue(ndev);
		netif_device_detach(ndev);
	}

	/* TODO: enter low power */

	priv->can.state = CAN_STATE_SLEEPING;

	return 0;
}

static __maybe_unused int m_can_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct m_can_priv *priv = netdev_priv(ndev);

	/* TODO: exit low power */

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	if (netif_running(ndev)) {
		netif_device_attach(ndev);
		netif_start_queue(ndev);
	}

	return 0;
}

static void unregister_m_can_dev(struct net_device *dev)
{
	unregister_candev(dev);
}

static int m_can_plat_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);

	unregister_m_can_dev(dev);
	platform_set_drvdata(pdev, NULL);

	free_m_can_dev(dev);

	return 0;
}

static const struct dev_pm_ops m_can_pmops = {
	SET_SYSTEM_SLEEP_PM_OPS(m_can_suspend, m_can_resume)
};

static const struct of_device_id m_can_of_table[] = {
	{ .compatible = "bosch,m_can", .data = NULL },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, m_can_of_table);

static struct platform_driver m_can_plat_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = m_can_of_table,
		.pm     = &m_can_pmops,
	},
	.probe = m_can_plat_probe,
	.remove = m_can_plat_remove,
};

module_platform_driver(m_can_plat_driver);

MODULE_AUTHOR("Dong Aisheng <b29396@freescale.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CAN bus driver for Bosch M_CAN controller");
