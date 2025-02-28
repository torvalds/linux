// SPDX-License-Identifier: GPL-2.0
// CAN bus driver for Bosch M_CAN controller
// Copyright (C) 2014 Freescale Semiconductor, Inc.
//      Dong Aisheng <b29396@freescale.com>
// Copyright (C) 2018-19 Texas Instruments Incorporated - http://www.ti.com/

/* Bosch M_CAN user manual can be obtained from:
 * https://github.com/linux-can/can-doc/tree/master/m_can
 */

#include <linux/bitfield.h>
#include <linux/can/dev.h>
#include <linux/ethtool.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "m_can.h"

/* registers definition */
enum m_can_reg {
	M_CAN_CREL	= 0x0,
	M_CAN_ENDN	= 0x4,
	M_CAN_CUST	= 0x8,
	M_CAN_DBTP	= 0xc,
	M_CAN_TEST	= 0x10,
	M_CAN_RWD	= 0x14,
	M_CAN_CCCR	= 0x18,
	M_CAN_NBTP	= 0x1c,
	M_CAN_TSCC	= 0x20,
	M_CAN_TSCV	= 0x24,
	M_CAN_TOCC	= 0x28,
	M_CAN_TOCV	= 0x2c,
	M_CAN_ECR	= 0x40,
	M_CAN_PSR	= 0x44,
	/* TDCR Register only available for version >=3.1.x */
	M_CAN_TDCR	= 0x48,
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

/* message ram configuration data length */
#define MRAM_CFG_LEN	8

/* Core Release Register (CREL) */
#define CREL_REL_MASK		GENMASK(31, 28)
#define CREL_STEP_MASK		GENMASK(27, 24)
#define CREL_SUBSTEP_MASK	GENMASK(23, 20)

/* Data Bit Timing & Prescaler Register (DBTP) */
#define DBTP_TDC		BIT(23)
#define DBTP_DBRP_MASK		GENMASK(20, 16)
#define DBTP_DTSEG1_MASK	GENMASK(12, 8)
#define DBTP_DTSEG2_MASK	GENMASK(7, 4)
#define DBTP_DSJW_MASK		GENMASK(3, 0)

/* Transmitter Delay Compensation Register (TDCR) */
#define TDCR_TDCO_MASK		GENMASK(14, 8)
#define TDCR_TDCF_MASK		GENMASK(6, 0)

/* Test Register (TEST) */
#define TEST_LBCK		BIT(4)

/* CC Control Register (CCCR) */
#define CCCR_TXP		BIT(14)
#define CCCR_TEST		BIT(7)
#define CCCR_DAR		BIT(6)
#define CCCR_MON		BIT(5)
#define CCCR_CSR		BIT(4)
#define CCCR_CSA		BIT(3)
#define CCCR_ASM		BIT(2)
#define CCCR_CCE		BIT(1)
#define CCCR_INIT		BIT(0)
/* for version 3.0.x */
#define CCCR_CMR_MASK		GENMASK(11, 10)
#define CCCR_CMR_CANFD		0x1
#define CCCR_CMR_CANFD_BRS	0x2
#define CCCR_CMR_CAN		0x3
#define CCCR_CME_MASK		GENMASK(9, 8)
#define CCCR_CME_CAN		0
#define CCCR_CME_CANFD		0x1
#define CCCR_CME_CANFD_BRS	0x2
/* for version >=3.1.x */
#define CCCR_EFBI		BIT(13)
#define CCCR_PXHD		BIT(12)
#define CCCR_BRSE		BIT(9)
#define CCCR_FDOE		BIT(8)
/* for version >=3.2.x */
#define CCCR_NISO		BIT(15)
/* for version >=3.3.x */
#define CCCR_WMM		BIT(11)
#define CCCR_UTSU		BIT(10)

/* Nominal Bit Timing & Prescaler Register (NBTP) */
#define NBTP_NSJW_MASK		GENMASK(31, 25)
#define NBTP_NBRP_MASK		GENMASK(24, 16)
#define NBTP_NTSEG1_MASK	GENMASK(15, 8)
#define NBTP_NTSEG2_MASK	GENMASK(6, 0)

/* Timestamp Counter Configuration Register (TSCC) */
#define TSCC_TCP_MASK		GENMASK(19, 16)
#define TSCC_TSS_MASK		GENMASK(1, 0)
#define TSCC_TSS_DISABLE	0x0
#define TSCC_TSS_INTERNAL	0x1
#define TSCC_TSS_EXTERNAL	0x2

/* Timestamp Counter Value Register (TSCV) */
#define TSCV_TSC_MASK		GENMASK(15, 0)

/* Error Counter Register (ECR) */
#define ECR_RP			BIT(15)
#define ECR_REC_MASK		GENMASK(14, 8)
#define ECR_TEC_MASK		GENMASK(7, 0)

/* Protocol Status Register (PSR) */
#define PSR_BO		BIT(7)
#define PSR_EW		BIT(6)
#define PSR_EP		BIT(5)
#define PSR_LEC_MASK	GENMASK(2, 0)
#define PSR_DLEC_MASK	GENMASK(10, 8)

/* Interrupt Register (IR) */
#define IR_ALL_INT	0xffffffff

/* Renamed bits for versions > 3.1.x */
#define IR_ARA		BIT(29)
#define IR_PED		BIT(28)
#define IR_PEA		BIT(27)

/* Bits for version 3.0.x */
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

/* Interrupts for version 3.0.x */
#define IR_ERR_LEC_30X	(IR_STE	| IR_FOE | IR_ACKE | IR_BE | IR_CRCE)
#define IR_ERR_BUS_30X	(IR_ERR_LEC_30X | IR_WDI | IR_BEU | IR_BEC | \
			 IR_TOO | IR_MRAF | IR_TSW | IR_TEFL | IR_RF1L | \
			 IR_RF0L)
#define IR_ERR_ALL_30X	(IR_ERR_STATE | IR_ERR_BUS_30X)

/* Interrupts for version >= 3.1.x */
#define IR_ERR_LEC_31X	(IR_PED | IR_PEA)
#define IR_ERR_BUS_31X	(IR_ERR_LEC_31X | IR_WDI | IR_BEU | IR_BEC | \
			 IR_TOO | IR_MRAF | IR_TSW | IR_TEFL | IR_RF1L | \
			 IR_RF0L)
#define IR_ERR_ALL_31X	(IR_ERR_STATE | IR_ERR_BUS_31X)

/* Interrupt Line Select (ILS) */
#define ILS_ALL_INT0	0x0
#define ILS_ALL_INT1	0xFFFFFFFF

/* Interrupt Line Enable (ILE) */
#define ILE_EINT1	BIT(1)
#define ILE_EINT0	BIT(0)

/* Rx FIFO 0/1 Configuration (RXF0C/RXF1C) */
#define RXFC_FWM_MASK	GENMASK(30, 24)
#define RXFC_FS_MASK	GENMASK(22, 16)

/* Rx FIFO 0/1 Status (RXF0S/RXF1S) */
#define RXFS_RFL	BIT(25)
#define RXFS_FF		BIT(24)
#define RXFS_FPI_MASK	GENMASK(21, 16)
#define RXFS_FGI_MASK	GENMASK(13, 8)
#define RXFS_FFL_MASK	GENMASK(6, 0)

/* Rx Buffer / FIFO Element Size Configuration (RXESC) */
#define RXESC_RBDS_MASK		GENMASK(10, 8)
#define RXESC_F1DS_MASK		GENMASK(6, 4)
#define RXESC_F0DS_MASK		GENMASK(2, 0)
#define RXESC_64B		0x7

/* Tx Buffer Configuration (TXBC) */
#define TXBC_TFQS_MASK		GENMASK(29, 24)
#define TXBC_NDTB_MASK		GENMASK(21, 16)

/* Tx FIFO/Queue Status (TXFQS) */
#define TXFQS_TFQF		BIT(21)
#define TXFQS_TFQPI_MASK	GENMASK(20, 16)
#define TXFQS_TFGI_MASK		GENMASK(12, 8)
#define TXFQS_TFFL_MASK		GENMASK(5, 0)

/* Tx Buffer Element Size Configuration (TXESC) */
#define TXESC_TBDS_MASK		GENMASK(2, 0)
#define TXESC_TBDS_64B		0x7

/* Tx Event FIFO Configuration (TXEFC) */
#define TXEFC_EFWM_MASK		GENMASK(29, 24)
#define TXEFC_EFS_MASK		GENMASK(21, 16)

/* Tx Event FIFO Status (TXEFS) */
#define TXEFS_TEFL		BIT(25)
#define TXEFS_EFF		BIT(24)
#define TXEFS_EFGI_MASK		GENMASK(12, 8)
#define TXEFS_EFFL_MASK		GENMASK(5, 0)

/* Tx Event FIFO Acknowledge (TXEFA) */
#define TXEFA_EFAI_MASK		GENMASK(4, 0)

/* Message RAM Configuration (in bytes) */
#define SIDF_ELEMENT_SIZE	4
#define XIDF_ELEMENT_SIZE	8
#define RXF0_ELEMENT_SIZE	72
#define RXF1_ELEMENT_SIZE	72
#define RXB_ELEMENT_SIZE	72
#define TXE_ELEMENT_SIZE	8
#define TXB_ELEMENT_SIZE	72

/* Message RAM Elements */
#define M_CAN_FIFO_ID		0x0
#define M_CAN_FIFO_DLC		0x4
#define M_CAN_FIFO_DATA		0x8

/* Rx Buffer Element */
/* R0 */
#define RX_BUF_ESI		BIT(31)
#define RX_BUF_XTD		BIT(30)
#define RX_BUF_RTR		BIT(29)
/* R1 */
#define RX_BUF_ANMF		BIT(31)
#define RX_BUF_FDF		BIT(21)
#define RX_BUF_BRS		BIT(20)
#define RX_BUF_RXTS_MASK	GENMASK(15, 0)

/* Tx Buffer Element */
/* T0 */
#define TX_BUF_ESI		BIT(31)
#define TX_BUF_XTD		BIT(30)
#define TX_BUF_RTR		BIT(29)
/* T1 */
#define TX_BUF_EFC		BIT(23)
#define TX_BUF_FDF		BIT(21)
#define TX_BUF_BRS		BIT(20)
#define TX_BUF_MM_MASK		GENMASK(31, 24)
#define TX_BUF_DLC_MASK		GENMASK(19, 16)

/* Tx event FIFO Element */
/* E1 */
#define TX_EVENT_MM_MASK	GENMASK(31, 24)
#define TX_EVENT_TXTS_MASK	GENMASK(15, 0)

/* Hrtimer polling interval */
#define HRTIMER_POLL_INTERVAL_MS		1

/* The ID and DLC registers are adjacent in M_CAN FIFO memory,
 * and we can save a (potentially slow) bus round trip by combining
 * reads and writes to them.
 */
struct id_and_dlc {
	u32 id;
	u32 dlc;
};

struct m_can_fifo_element {
	u32 id;
	u32 dlc;
	u8 data[CANFD_MAX_DLEN];
};

static inline u32 m_can_read(struct m_can_classdev *cdev, enum m_can_reg reg)
{
	return cdev->ops->read_reg(cdev, reg);
}

static inline void m_can_write(struct m_can_classdev *cdev, enum m_can_reg reg,
			       u32 val)
{
	cdev->ops->write_reg(cdev, reg, val);
}

static int
m_can_fifo_read(struct m_can_classdev *cdev,
		u32 fgi, unsigned int offset, void *val, size_t val_count)
{
	u32 addr_offset = cdev->mcfg[MRAM_RXF0].off + fgi * RXF0_ELEMENT_SIZE +
		offset;

	if (val_count == 0)
		return 0;

	return cdev->ops->read_fifo(cdev, addr_offset, val, val_count);
}

static int
m_can_fifo_write(struct m_can_classdev *cdev,
		 u32 fpi, unsigned int offset, const void *val, size_t val_count)
{
	u32 addr_offset = cdev->mcfg[MRAM_TXB].off + fpi * TXB_ELEMENT_SIZE +
		offset;

	if (val_count == 0)
		return 0;

	return cdev->ops->write_fifo(cdev, addr_offset, val, val_count);
}

static inline int m_can_fifo_write_no_off(struct m_can_classdev *cdev,
					  u32 fpi, u32 val)
{
	return cdev->ops->write_fifo(cdev, fpi, &val, 1);
}

static int
m_can_txe_fifo_read(struct m_can_classdev *cdev, u32 fgi, u32 offset, u32 *val)
{
	u32 addr_offset = cdev->mcfg[MRAM_TXE].off + fgi * TXE_ELEMENT_SIZE +
		offset;

	return cdev->ops->read_fifo(cdev, addr_offset, val, 1);
}

static int m_can_cccr_update_bits(struct m_can_classdev *cdev, u32 mask, u32 val)
{
	u32 val_before = m_can_read(cdev, M_CAN_CCCR);
	u32 val_after = (val_before & ~mask) | val;
	size_t tries = 10;

	if (!(mask & CCCR_INIT) && !(val_before & CCCR_INIT)) {
		dev_err(cdev->dev,
			"refusing to configure device when in normal mode\n");
		return -EBUSY;
	}

	/* The chip should be in standby mode when changing the CCCR register,
	 * and some chips set the CSR and CSA bits when in standby. Furthermore,
	 * the CSR and CSA bits should be written as zeros, even when they read
	 * ones.
	 */
	val_after &= ~(CCCR_CSR | CCCR_CSA);

	while (tries--) {
		u32 val_read;

		/* Write the desired value in each try, as setting some bits in
		 * the CCCR register require other bits to be set first. E.g.
		 * setting the NISO bit requires setting the CCE bit first.
		 */
		m_can_write(cdev, M_CAN_CCCR, val_after);

		val_read = m_can_read(cdev, M_CAN_CCCR) & ~(CCCR_CSR | CCCR_CSA);

		if (val_read == val_after)
			return 0;

		usleep_range(1, 5);
	}

	return -ETIMEDOUT;
}

static int m_can_config_enable(struct m_can_classdev *cdev)
{
	int err;

	/* CCCR_INIT must be set in order to set CCCR_CCE, but access to
	 * configuration registers should only be enabled when in standby mode,
	 * where CCCR_INIT is always set.
	 */
	err = m_can_cccr_update_bits(cdev, CCCR_CCE, CCCR_CCE);
	if (err)
		netdev_err(cdev->net, "failed to enable configuration mode\n");

	return err;
}

static int m_can_config_disable(struct m_can_classdev *cdev)
{
	int err;

	/* Only clear CCCR_CCE, since CCCR_INIT cannot be cleared while in
	 * standby mode
	 */
	err = m_can_cccr_update_bits(cdev, CCCR_CCE, 0);
	if (err)
		netdev_err(cdev->net, "failed to disable configuration registers\n");

	return err;
}

static void m_can_interrupt_enable(struct m_can_classdev *cdev, u32 interrupts)
{
	if (cdev->active_interrupts == interrupts)
		return;
	cdev->ops->write_reg(cdev, M_CAN_IE, interrupts);
	cdev->active_interrupts = interrupts;
}

static void m_can_coalescing_disable(struct m_can_classdev *cdev)
{
	u32 new_interrupts = cdev->active_interrupts | IR_RF0N | IR_TEFN;

	if (!cdev->net->irq)
		return;

	hrtimer_cancel(&cdev->hrtimer);
	m_can_interrupt_enable(cdev, new_interrupts);
}

static inline void m_can_enable_all_interrupts(struct m_can_classdev *cdev)
{
	if (!cdev->net->irq) {
		dev_dbg(cdev->dev, "Start hrtimer\n");
		hrtimer_start(&cdev->hrtimer,
			      ms_to_ktime(HRTIMER_POLL_INTERVAL_MS),
			      HRTIMER_MODE_REL_PINNED);
	}

	/* Only interrupt line 0 is used in this driver */
	m_can_write(cdev, M_CAN_ILE, ILE_EINT0);
}

static inline void m_can_disable_all_interrupts(struct m_can_classdev *cdev)
{
	m_can_coalescing_disable(cdev);
	m_can_write(cdev, M_CAN_ILE, 0x0);

	if (!cdev->net->irq) {
		dev_dbg(cdev->dev, "Stop hrtimer\n");
		hrtimer_try_to_cancel(&cdev->hrtimer);
	}
}

/* Retrieve internal timestamp counter from TSCV.TSC, and shift it to 32-bit
 * width.
 */
static u32 m_can_get_timestamp(struct m_can_classdev *cdev)
{
	u32 tscv;
	u32 tsc;

	tscv = m_can_read(cdev, M_CAN_TSCV);
	tsc = FIELD_GET(TSCV_TSC_MASK, tscv);

	return (tsc << 16);
}

static void m_can_clean(struct net_device *net)
{
	struct m_can_classdev *cdev = netdev_priv(net);
	unsigned long irqflags;

	if (cdev->tx_ops) {
		for (int i = 0; i != cdev->tx_fifo_size; ++i) {
			if (!cdev->tx_ops[i].skb)
				continue;

			net->stats.tx_errors++;
			cdev->tx_ops[i].skb = NULL;
		}
	}

	for (int i = 0; i != cdev->can.echo_skb_max; ++i)
		can_free_echo_skb(cdev->net, i, NULL);

	netdev_reset_queue(cdev->net);

	spin_lock_irqsave(&cdev->tx_handling_spinlock, irqflags);
	cdev->tx_fifo_in_flight = 0;
	spin_unlock_irqrestore(&cdev->tx_handling_spinlock, irqflags);
}

/* For peripherals, pass skb to rx-offload, which will push skb from
 * napi. For non-peripherals, RX is done in napi already, so push
 * directly. timestamp is used to ensure good skb ordering in
 * rx-offload and is ignored for non-peripherals.
 */
static void m_can_receive_skb(struct m_can_classdev *cdev,
			      struct sk_buff *skb,
			      u32 timestamp)
{
	if (cdev->is_peripheral) {
		struct net_device_stats *stats = &cdev->net->stats;
		int err;

		err = can_rx_offload_queue_timestamp(&cdev->offload, skb,
						     timestamp);
		if (err)
			stats->rx_fifo_errors++;
	} else {
		netif_receive_skb(skb);
	}
}

static int m_can_read_fifo(struct net_device *dev, u32 fgi)
{
	struct net_device_stats *stats = &dev->stats;
	struct m_can_classdev *cdev = netdev_priv(dev);
	struct canfd_frame *cf;
	struct sk_buff *skb;
	struct id_and_dlc fifo_header;
	u32 timestamp = 0;
	int err;

	err = m_can_fifo_read(cdev, fgi, M_CAN_FIFO_ID, &fifo_header, 2);
	if (err)
		goto out_fail;

	if (fifo_header.dlc & RX_BUF_FDF)
		skb = alloc_canfd_skb(dev, &cf);
	else
		skb = alloc_can_skb(dev, (struct can_frame **)&cf);
	if (!skb) {
		stats->rx_dropped++;
		return 0;
	}

	if (fifo_header.dlc & RX_BUF_FDF)
		cf->len = can_fd_dlc2len((fifo_header.dlc >> 16) & 0x0F);
	else
		cf->len = can_cc_dlc2len((fifo_header.dlc >> 16) & 0x0F);

	if (fifo_header.id & RX_BUF_XTD)
		cf->can_id = (fifo_header.id & CAN_EFF_MASK) | CAN_EFF_FLAG;
	else
		cf->can_id = (fifo_header.id >> 18) & CAN_SFF_MASK;

	if (fifo_header.id & RX_BUF_ESI) {
		cf->flags |= CANFD_ESI;
		netdev_dbg(dev, "ESI Error\n");
	}

	if (!(fifo_header.dlc & RX_BUF_FDF) && (fifo_header.id & RX_BUF_RTR)) {
		cf->can_id |= CAN_RTR_FLAG;
	} else {
		if (fifo_header.dlc & RX_BUF_BRS)
			cf->flags |= CANFD_BRS;

		err = m_can_fifo_read(cdev, fgi, M_CAN_FIFO_DATA,
				      cf->data, DIV_ROUND_UP(cf->len, 4));
		if (err)
			goto out_free_skb;

		stats->rx_bytes += cf->len;
	}
	stats->rx_packets++;

	timestamp = FIELD_GET(RX_BUF_RXTS_MASK, fifo_header.dlc) << 16;

	m_can_receive_skb(cdev, skb, timestamp);

	return 0;

out_free_skb:
	kfree_skb(skb);
out_fail:
	netdev_err(dev, "FIFO read returned %d\n", err);
	return err;
}

static int m_can_do_rx_poll(struct net_device *dev, int quota)
{
	struct m_can_classdev *cdev = netdev_priv(dev);
	u32 pkts = 0;
	u32 rxfs;
	u32 rx_count;
	u32 fgi;
	int ack_fgi = -1;
	int i;
	int err = 0;

	rxfs = m_can_read(cdev, M_CAN_RXF0S);
	if (!(rxfs & RXFS_FFL_MASK)) {
		netdev_dbg(dev, "no messages in fifo0\n");
		return 0;
	}

	rx_count = FIELD_GET(RXFS_FFL_MASK, rxfs);
	fgi = FIELD_GET(RXFS_FGI_MASK, rxfs);

	for (i = 0; i < rx_count && quota > 0; ++i) {
		err = m_can_read_fifo(dev, fgi);
		if (err)
			break;

		quota--;
		pkts++;
		ack_fgi = fgi;
		fgi = (++fgi >= cdev->mcfg[MRAM_RXF0].num ? 0 : fgi);
	}

	if (ack_fgi != -1)
		m_can_write(cdev, M_CAN_RXF0A, ack_fgi);

	if (err)
		return err;

	return pkts;
}

static int m_can_handle_lost_msg(struct net_device *dev)
{
	struct m_can_classdev *cdev = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct sk_buff *skb;
	struct can_frame *frame;
	u32 timestamp = 0;

	netdev_err(dev, "msg lost in rxf0\n");

	stats->rx_errors++;
	stats->rx_over_errors++;

	skb = alloc_can_err_skb(dev, &frame);
	if (unlikely(!skb))
		return 0;

	frame->can_id |= CAN_ERR_CRTL;
	frame->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;

	if (cdev->is_peripheral)
		timestamp = m_can_get_timestamp(cdev);

	m_can_receive_skb(cdev, skb, timestamp);

	return 1;
}

static int m_can_handle_lec_err(struct net_device *dev,
				enum m_can_lec_type lec_type)
{
	struct m_can_classdev *cdev = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	u32 timestamp = 0;

	cdev->can.can_stats.bus_error++;

	/* propagate the error condition to the CAN stack */
	skb = alloc_can_err_skb(dev, &cf);

	/* check for 'last error code' which tells us the
	 * type of the last error to occur on the CAN bus
	 */
	if (likely(skb))
		cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;

	switch (lec_type) {
	case LEC_STUFF_ERROR:
		netdev_dbg(dev, "stuff error\n");
		stats->rx_errors++;
		if (likely(skb))
			cf->data[2] |= CAN_ERR_PROT_STUFF;
		break;
	case LEC_FORM_ERROR:
		netdev_dbg(dev, "form error\n");
		stats->rx_errors++;
		if (likely(skb))
			cf->data[2] |= CAN_ERR_PROT_FORM;
		break;
	case LEC_ACK_ERROR:
		netdev_dbg(dev, "ack error\n");
		stats->tx_errors++;
		if (likely(skb))
			cf->data[3] = CAN_ERR_PROT_LOC_ACK;
		break;
	case LEC_BIT1_ERROR:
		netdev_dbg(dev, "bit1 error\n");
		stats->tx_errors++;
		if (likely(skb))
			cf->data[2] |= CAN_ERR_PROT_BIT1;
		break;
	case LEC_BIT0_ERROR:
		netdev_dbg(dev, "bit0 error\n");
		stats->tx_errors++;
		if (likely(skb))
			cf->data[2] |= CAN_ERR_PROT_BIT0;
		break;
	case LEC_CRC_ERROR:
		netdev_dbg(dev, "CRC error\n");
		stats->rx_errors++;
		if (likely(skb))
			cf->data[3] = CAN_ERR_PROT_LOC_CRC_SEQ;
		break;
	default:
		break;
	}

	if (unlikely(!skb))
		return 0;

	if (cdev->is_peripheral)
		timestamp = m_can_get_timestamp(cdev);

	m_can_receive_skb(cdev, skb, timestamp);

	return 1;
}

static int __m_can_get_berr_counter(const struct net_device *dev,
				    struct can_berr_counter *bec)
{
	struct m_can_classdev *cdev = netdev_priv(dev);
	unsigned int ecr;

	ecr = m_can_read(cdev, M_CAN_ECR);
	bec->rxerr = FIELD_GET(ECR_REC_MASK, ecr);
	bec->txerr = FIELD_GET(ECR_TEC_MASK, ecr);

	return 0;
}

static int m_can_clk_start(struct m_can_classdev *cdev)
{
	if (cdev->pm_clock_support == 0)
		return 0;

	return pm_runtime_resume_and_get(cdev->dev);
}

static void m_can_clk_stop(struct m_can_classdev *cdev)
{
	if (cdev->pm_clock_support)
		pm_runtime_put_sync(cdev->dev);
}

static int m_can_get_berr_counter(const struct net_device *dev,
				  struct can_berr_counter *bec)
{
	struct m_can_classdev *cdev = netdev_priv(dev);
	int err;

	err = m_can_clk_start(cdev);
	if (err)
		return err;

	__m_can_get_berr_counter(dev, bec);

	m_can_clk_stop(cdev);

	return 0;
}

static int m_can_handle_state_change(struct net_device *dev,
				     enum can_state new_state)
{
	struct m_can_classdev *cdev = netdev_priv(dev);
	struct can_frame *cf;
	struct sk_buff *skb;
	struct can_berr_counter bec;
	unsigned int ecr;
	u32 timestamp = 0;

	switch (new_state) {
	case CAN_STATE_ERROR_WARNING:
		/* error warning state */
		cdev->can.can_stats.error_warning++;
		cdev->can.state = CAN_STATE_ERROR_WARNING;
		break;
	case CAN_STATE_ERROR_PASSIVE:
		/* error passive state */
		cdev->can.can_stats.error_passive++;
		cdev->can.state = CAN_STATE_ERROR_PASSIVE;
		break;
	case CAN_STATE_BUS_OFF:
		/* bus-off state */
		cdev->can.state = CAN_STATE_BUS_OFF;
		m_can_disable_all_interrupts(cdev);
		cdev->can.can_stats.bus_off++;
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
	case CAN_STATE_ERROR_WARNING:
		/* error warning state */
		cf->can_id |= CAN_ERR_CRTL | CAN_ERR_CNT;
		cf->data[1] = (bec.txerr > bec.rxerr) ?
			CAN_ERR_CRTL_TX_WARNING :
			CAN_ERR_CRTL_RX_WARNING;
		cf->data[6] = bec.txerr;
		cf->data[7] = bec.rxerr;
		break;
	case CAN_STATE_ERROR_PASSIVE:
		/* error passive state */
		cf->can_id |= CAN_ERR_CRTL | CAN_ERR_CNT;
		ecr = m_can_read(cdev, M_CAN_ECR);
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

	if (cdev->is_peripheral)
		timestamp = m_can_get_timestamp(cdev);

	m_can_receive_skb(cdev, skb, timestamp);

	return 1;
}

static int m_can_handle_state_errors(struct net_device *dev, u32 psr)
{
	struct m_can_classdev *cdev = netdev_priv(dev);
	int work_done = 0;

	if (psr & PSR_EW && cdev->can.state != CAN_STATE_ERROR_WARNING) {
		netdev_dbg(dev, "entered error warning state\n");
		work_done += m_can_handle_state_change(dev,
						       CAN_STATE_ERROR_WARNING);
	}

	if (psr & PSR_EP && cdev->can.state != CAN_STATE_ERROR_PASSIVE) {
		netdev_dbg(dev, "entered error passive state\n");
		work_done += m_can_handle_state_change(dev,
						       CAN_STATE_ERROR_PASSIVE);
	}

	if (psr & PSR_BO && cdev->can.state != CAN_STATE_BUS_OFF) {
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
	if (irqstatus & IR_BEU)
		netdev_err(dev, "Bit Error Uncorrected\n");
	if (irqstatus & IR_BEC)
		netdev_err(dev, "Bit Error Corrected\n");
	if (irqstatus & IR_TOO)
		netdev_err(dev, "Timeout reached\n");
	if (irqstatus & IR_MRAF)
		netdev_err(dev, "Message RAM access failure occurred\n");
}

static inline bool is_lec_err(u8 lec)
{
	return lec != LEC_NO_ERROR && lec != LEC_NO_CHANGE;
}

static inline bool m_can_is_protocol_err(u32 irqstatus)
{
	return irqstatus & IR_ERR_LEC_31X;
}

static int m_can_handle_protocol_error(struct net_device *dev, u32 irqstatus)
{
	struct net_device_stats *stats = &dev->stats;
	struct m_can_classdev *cdev = netdev_priv(dev);
	struct can_frame *cf;
	struct sk_buff *skb;
	u32 timestamp = 0;

	/* propagate the error condition to the CAN stack */
	skb = alloc_can_err_skb(dev, &cf);

	/* update tx error stats since there is protocol error */
	stats->tx_errors++;

	/* update arbitration lost status */
	if (cdev->version >= 31 && (irqstatus & IR_PEA)) {
		netdev_dbg(dev, "Protocol error in Arbitration fail\n");
		cdev->can.can_stats.arbitration_lost++;
		if (skb) {
			cf->can_id |= CAN_ERR_LOSTARB;
			cf->data[0] |= CAN_ERR_LOSTARB_UNSPEC;
		}
	}

	if (unlikely(!skb)) {
		netdev_dbg(dev, "allocation of skb failed\n");
		return 0;
	}

	if (cdev->is_peripheral)
		timestamp = m_can_get_timestamp(cdev);

	m_can_receive_skb(cdev, skb, timestamp);

	return 1;
}

static int m_can_handle_bus_errors(struct net_device *dev, u32 irqstatus,
				   u32 psr)
{
	struct m_can_classdev *cdev = netdev_priv(dev);
	int work_done = 0;

	if (irqstatus & IR_RF0L)
		work_done += m_can_handle_lost_msg(dev);

	/* handle lec errors on the bus */
	if (cdev->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING) {
		u8 lec = FIELD_GET(PSR_LEC_MASK, psr);
		u8 dlec = FIELD_GET(PSR_DLEC_MASK, psr);

		if (is_lec_err(lec)) {
			netdev_dbg(dev, "Arbitration phase error detected\n");
			work_done += m_can_handle_lec_err(dev, lec);
		}

		if (is_lec_err(dlec)) {
			netdev_dbg(dev, "Data phase error detected\n");
			work_done += m_can_handle_lec_err(dev, dlec);
		}
	}

	/* handle protocol errors in arbitration phase */
	if ((cdev->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING) &&
	    m_can_is_protocol_err(irqstatus))
		work_done += m_can_handle_protocol_error(dev, irqstatus);

	/* other unproccessed error interrupts */
	m_can_handle_other_err(dev, irqstatus);

	return work_done;
}

static int m_can_rx_handler(struct net_device *dev, int quota, u32 irqstatus)
{
	struct m_can_classdev *cdev = netdev_priv(dev);
	int rx_work_or_err;
	int work_done = 0;

	if (!irqstatus)
		goto end;

	/* Errata workaround for issue "Needless activation of MRAF irq"
	 * During frame reception while the MCAN is in Error Passive state
	 * and the Receive Error Counter has the value MCAN_ECR.REC = 127,
	 * it may happen that MCAN_IR.MRAF is set although there was no
	 * Message RAM access failure.
	 * If MCAN_IR.MRAF is enabled, an interrupt to the Host CPU is generated
	 * The Message RAM Access Failure interrupt routine needs to check
	 * whether MCAN_ECR.RP = ’1’ and MCAN_ECR.REC = 127.
	 * In this case, reset MCAN_IR.MRAF. No further action is required.
	 */
	if (cdev->version <= 31 && irqstatus & IR_MRAF &&
	    m_can_read(cdev, M_CAN_ECR) & ECR_RP) {
		struct can_berr_counter bec;

		__m_can_get_berr_counter(dev, &bec);
		if (bec.rxerr == 127) {
			m_can_write(cdev, M_CAN_IR, IR_MRAF);
			irqstatus &= ~IR_MRAF;
		}
	}

	if (irqstatus & IR_ERR_STATE)
		work_done += m_can_handle_state_errors(dev,
						       m_can_read(cdev, M_CAN_PSR));

	if (irqstatus & IR_ERR_BUS_30X)
		work_done += m_can_handle_bus_errors(dev, irqstatus,
						     m_can_read(cdev, M_CAN_PSR));

	if (irqstatus & IR_RF0N) {
		rx_work_or_err = m_can_do_rx_poll(dev, (quota - work_done));
		if (rx_work_or_err < 0)
			return rx_work_or_err;

		work_done += rx_work_or_err;
	}
end:
	return work_done;
}

static int m_can_poll(struct napi_struct *napi, int quota)
{
	struct net_device *dev = napi->dev;
	struct m_can_classdev *cdev = netdev_priv(dev);
	int work_done;
	u32 irqstatus;

	irqstatus = cdev->irqstatus | m_can_read(cdev, M_CAN_IR);

	work_done = m_can_rx_handler(dev, quota, irqstatus);

	/* Don't re-enable interrupts if the driver had a fatal error
	 * (e.g., FIFO read failure).
	 */
	if (work_done >= 0 && work_done < quota) {
		napi_complete_done(napi, work_done);
		m_can_enable_all_interrupts(cdev);
	}

	return work_done;
}

/* Echo tx skb and update net stats. Peripherals use rx-offload for
 * echo. timestamp is used for peripherals to ensure correct ordering
 * by rx-offload, and is ignored for non-peripherals.
 */
static unsigned int m_can_tx_update_stats(struct m_can_classdev *cdev,
					  unsigned int msg_mark, u32 timestamp)
{
	struct net_device *dev = cdev->net;
	struct net_device_stats *stats = &dev->stats;
	unsigned int frame_len;

	if (cdev->is_peripheral)
		stats->tx_bytes +=
			can_rx_offload_get_echo_skb_queue_timestamp(&cdev->offload,
								    msg_mark,
								    timestamp,
								    &frame_len);
	else
		stats->tx_bytes += can_get_echo_skb(dev, msg_mark, &frame_len);

	stats->tx_packets++;

	return frame_len;
}

static void m_can_finish_tx(struct m_can_classdev *cdev, int transmitted,
			    unsigned int transmitted_frame_len)
{
	unsigned long irqflags;

	netdev_completed_queue(cdev->net, transmitted, transmitted_frame_len);

	spin_lock_irqsave(&cdev->tx_handling_spinlock, irqflags);
	if (cdev->tx_fifo_in_flight >= cdev->tx_fifo_size && transmitted > 0)
		netif_wake_queue(cdev->net);
	cdev->tx_fifo_in_flight -= transmitted;
	spin_unlock_irqrestore(&cdev->tx_handling_spinlock, irqflags);
}

static netdev_tx_t m_can_start_tx(struct m_can_classdev *cdev)
{
	unsigned long irqflags;
	int tx_fifo_in_flight;

	spin_lock_irqsave(&cdev->tx_handling_spinlock, irqflags);
	tx_fifo_in_flight = cdev->tx_fifo_in_flight + 1;
	if (tx_fifo_in_flight >= cdev->tx_fifo_size) {
		netif_stop_queue(cdev->net);
		if (tx_fifo_in_flight > cdev->tx_fifo_size) {
			netdev_err_once(cdev->net, "hard_xmit called while TX FIFO full\n");
			spin_unlock_irqrestore(&cdev->tx_handling_spinlock, irqflags);
			return NETDEV_TX_BUSY;
		}
	}
	cdev->tx_fifo_in_flight = tx_fifo_in_flight;
	spin_unlock_irqrestore(&cdev->tx_handling_spinlock, irqflags);

	return NETDEV_TX_OK;
}

static int m_can_echo_tx_event(struct net_device *dev)
{
	u32 txe_count = 0;
	u32 m_can_txefs;
	u32 fgi = 0;
	int ack_fgi = -1;
	int i = 0;
	int err = 0;
	unsigned int msg_mark;
	int processed = 0;
	unsigned int processed_frame_len = 0;

	struct m_can_classdev *cdev = netdev_priv(dev);

	/* read tx event fifo status */
	m_can_txefs = m_can_read(cdev, M_CAN_TXEFS);

	/* Get Tx Event fifo element count */
	txe_count = FIELD_GET(TXEFS_EFFL_MASK, m_can_txefs);
	fgi = FIELD_GET(TXEFS_EFGI_MASK, m_can_txefs);

	/* Get and process all sent elements */
	for (i = 0; i < txe_count; i++) {
		u32 txe, timestamp = 0;

		/* get message marker, timestamp */
		err = m_can_txe_fifo_read(cdev, fgi, 4, &txe);
		if (err) {
			netdev_err(dev, "TXE FIFO read returned %d\n", err);
			break;
		}

		msg_mark = FIELD_GET(TX_EVENT_MM_MASK, txe);
		timestamp = FIELD_GET(TX_EVENT_TXTS_MASK, txe) << 16;

		ack_fgi = fgi;
		fgi = (++fgi >= cdev->mcfg[MRAM_TXE].num ? 0 : fgi);

		/* update stats */
		processed_frame_len += m_can_tx_update_stats(cdev, msg_mark,
							     timestamp);

		++processed;
	}

	if (ack_fgi != -1)
		m_can_write(cdev, M_CAN_TXEFA, FIELD_PREP(TXEFA_EFAI_MASK,
							  ack_fgi));

	m_can_finish_tx(cdev, processed, processed_frame_len);

	return err;
}

static void m_can_coalescing_update(struct m_can_classdev *cdev, u32 ir)
{
	u32 new_interrupts = cdev->active_interrupts;
	bool enable_rx_timer = false;
	bool enable_tx_timer = false;

	if (!cdev->net->irq)
		return;

	if (cdev->rx_coalesce_usecs_irq > 0 && (ir & (IR_RF0N | IR_RF0W))) {
		enable_rx_timer = true;
		new_interrupts &= ~IR_RF0N;
	}
	if (cdev->tx_coalesce_usecs_irq > 0 && (ir & (IR_TEFN | IR_TEFW))) {
		enable_tx_timer = true;
		new_interrupts &= ~IR_TEFN;
	}
	if (!enable_rx_timer && !hrtimer_active(&cdev->hrtimer))
		new_interrupts |= IR_RF0N;
	if (!enable_tx_timer && !hrtimer_active(&cdev->hrtimer))
		new_interrupts |= IR_TEFN;

	m_can_interrupt_enable(cdev, new_interrupts);
	if (enable_rx_timer | enable_tx_timer)
		hrtimer_start(&cdev->hrtimer, cdev->irq_timer_wait,
			      HRTIMER_MODE_REL);
}

/* This interrupt handler is called either from the interrupt thread or a
 * hrtimer. This has implications like cancelling a timer won't be possible
 * blocking.
 */
static int m_can_interrupt_handler(struct m_can_classdev *cdev)
{
	struct net_device *dev = cdev->net;
	u32 ir = 0, ir_read;
	int ret;

	if (pm_runtime_suspended(cdev->dev))
		return IRQ_NONE;

	/* The m_can controller signals its interrupt status as a level, but
	 * depending in the integration the CPU may interpret the signal as
	 * edge-triggered (for example with m_can_pci). For these
	 * edge-triggered integrations, we must observe that IR is 0 at least
	 * once to be sure that the next interrupt will generate an edge.
	 */
	while ((ir_read = m_can_read(cdev, M_CAN_IR)) != 0) {
		ir |= ir_read;

		/* ACK all irqs */
		m_can_write(cdev, M_CAN_IR, ir);

		if (!cdev->irq_edge_triggered)
			break;
	}

	m_can_coalescing_update(cdev, ir);
	if (!ir)
		return IRQ_NONE;

	if (cdev->ops->clear_interrupts)
		cdev->ops->clear_interrupts(cdev);

	/* schedule NAPI in case of
	 * - rx IRQ
	 * - state change IRQ
	 * - bus error IRQ and bus error reporting
	 */
	if (ir & (IR_RF0N | IR_RF0W | IR_ERR_ALL_30X)) {
		cdev->irqstatus = ir;
		if (!cdev->is_peripheral) {
			m_can_disable_all_interrupts(cdev);
			napi_schedule(&cdev->napi);
		} else {
			ret = m_can_rx_handler(dev, NAPI_POLL_WEIGHT, ir);
			if (ret < 0)
				return ret;
		}
	}

	if (cdev->version == 30) {
		if (ir & IR_TC) {
			/* Transmission Complete Interrupt*/
			u32 timestamp = 0;
			unsigned int frame_len;

			if (cdev->is_peripheral)
				timestamp = m_can_get_timestamp(cdev);
			frame_len = m_can_tx_update_stats(cdev, 0, timestamp);
			m_can_finish_tx(cdev, 1, frame_len);
		}
	} else  {
		if (ir & (IR_TEFN | IR_TEFW)) {
			/* New TX FIFO Element arrived */
			ret = m_can_echo_tx_event(dev);
			if (ret != 0)
				return ret;
		}
	}

	if (cdev->is_peripheral)
		can_rx_offload_threaded_irq_finish(&cdev->offload);

	return IRQ_HANDLED;
}

static irqreturn_t m_can_isr(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct m_can_classdev *cdev = netdev_priv(dev);
	int ret;

	ret =  m_can_interrupt_handler(cdev);
	if (ret < 0) {
		m_can_disable_all_interrupts(cdev);
		return IRQ_HANDLED;
	}

	return ret;
}

static enum hrtimer_restart m_can_coalescing_timer(struct hrtimer *timer)
{
	struct m_can_classdev *cdev = container_of(timer, struct m_can_classdev, hrtimer);

	if (cdev->can.state == CAN_STATE_BUS_OFF ||
	    cdev->can.state == CAN_STATE_STOPPED)
		return HRTIMER_NORESTART;

	irq_wake_thread(cdev->net->irq, cdev->net);

	return HRTIMER_NORESTART;
}

static const struct can_bittiming_const m_can_bittiming_const_30X = {
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

static const struct can_bittiming_const m_can_data_bittiming_const_30X = {
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

static const struct can_bittiming_const m_can_bittiming_const_31X = {
	.name = KBUILD_MODNAME,
	.tseg1_min = 2,		/* Time segment 1 = prop_seg + phase_seg1 */
	.tseg1_max = 256,
	.tseg2_min = 2,		/* Time segment 2 = phase_seg2 */
	.tseg2_max = 128,
	.sjw_max = 128,
	.brp_min = 1,
	.brp_max = 512,
	.brp_inc = 1,
};

static const struct can_bittiming_const m_can_data_bittiming_const_31X = {
	.name = KBUILD_MODNAME,
	.tseg1_min = 1,		/* Time segment 1 = prop_seg + phase_seg1 */
	.tseg1_max = 32,
	.tseg2_min = 1,		/* Time segment 2 = phase_seg2 */
	.tseg2_max = 16,
	.sjw_max = 16,
	.brp_min = 1,
	.brp_max = 32,
	.brp_inc = 1,
};

static int m_can_set_bittiming(struct net_device *dev)
{
	struct m_can_classdev *cdev = netdev_priv(dev);
	const struct can_bittiming *bt = &cdev->can.bittiming;
	const struct can_bittiming *dbt = &cdev->can.data_bittiming;
	u16 brp, sjw, tseg1, tseg2;
	u32 reg_btp;

	brp = bt->brp - 1;
	sjw = bt->sjw - 1;
	tseg1 = bt->prop_seg + bt->phase_seg1 - 1;
	tseg2 = bt->phase_seg2 - 1;
	reg_btp = FIELD_PREP(NBTP_NBRP_MASK, brp) |
		  FIELD_PREP(NBTP_NSJW_MASK, sjw) |
		  FIELD_PREP(NBTP_NTSEG1_MASK, tseg1) |
		  FIELD_PREP(NBTP_NTSEG2_MASK, tseg2);
	m_can_write(cdev, M_CAN_NBTP, reg_btp);

	if (cdev->can.ctrlmode & CAN_CTRLMODE_FD) {
		reg_btp = 0;
		brp = dbt->brp - 1;
		sjw = dbt->sjw - 1;
		tseg1 = dbt->prop_seg + dbt->phase_seg1 - 1;
		tseg2 = dbt->phase_seg2 - 1;

		/* TDC is only needed for bitrates beyond 2.5 MBit/s.
		 * This is mentioned in the "Bit Time Requirements for CAN FD"
		 * paper presented at the International CAN Conference 2013
		 */
		if (dbt->bitrate > 2500000) {
			u32 tdco, ssp;

			/* Use the same value of secondary sampling point
			 * as the data sampling point
			 */
			ssp = dbt->sample_point;

			/* Equation based on Bosch's M_CAN User Manual's
			 * Transmitter Delay Compensation Section
			 */
			tdco = (cdev->can.clock.freq / 1000) *
				ssp / dbt->bitrate;

			/* Max valid TDCO value is 127 */
			if (tdco > 127) {
				netdev_warn(dev, "TDCO value of %u is beyond maximum. Using maximum possible value\n",
					    tdco);
				tdco = 127;
			}

			reg_btp |= DBTP_TDC;
			m_can_write(cdev, M_CAN_TDCR,
				    FIELD_PREP(TDCR_TDCO_MASK, tdco));
		}

		reg_btp |= FIELD_PREP(DBTP_DBRP_MASK, brp) |
			FIELD_PREP(DBTP_DSJW_MASK, sjw) |
			FIELD_PREP(DBTP_DTSEG1_MASK, tseg1) |
			FIELD_PREP(DBTP_DTSEG2_MASK, tseg2);

		m_can_write(cdev, M_CAN_DBTP, reg_btp);
	}

	return 0;
}

/* Configure M_CAN chip:
 * - set rx buffer/fifo element size
 * - configure rx fifo
 * - accept non-matching frame into fifo 0
 * - configure tx buffer
 *		- >= v3.1.x: TX FIFO is used
 * - configure mode
 * - setup bittiming
 * - configure timestamp generation
 */
static int m_can_chip_config(struct net_device *dev)
{
	struct m_can_classdev *cdev = netdev_priv(dev);
	u32 interrupts = IR_ALL_INT;
	u32 cccr, test;
	int err;

	err = m_can_init_ram(cdev);
	if (err) {
		dev_err(cdev->dev, "Message RAM configuration failed\n");
		return err;
	}

	/* Disable unused interrupts */
	interrupts &= ~(IR_ARA | IR_ELO | IR_DRX | IR_TEFF | IR_TFE | IR_TCF |
			IR_HPM | IR_RF1F | IR_RF1W | IR_RF1N | IR_RF0F |
			IR_TSW);

	err = m_can_config_enable(cdev);
	if (err)
		return err;

	/* RX Buffer/FIFO Element Size 64 bytes data field */
	m_can_write(cdev, M_CAN_RXESC,
		    FIELD_PREP(RXESC_RBDS_MASK, RXESC_64B) |
		    FIELD_PREP(RXESC_F1DS_MASK, RXESC_64B) |
		    FIELD_PREP(RXESC_F0DS_MASK, RXESC_64B));

	/* Accept Non-matching Frames Into FIFO 0 */
	m_can_write(cdev, M_CAN_GFC, 0x0);

	if (cdev->version == 30) {
		/* only support one Tx Buffer currently */
		m_can_write(cdev, M_CAN_TXBC, FIELD_PREP(TXBC_NDTB_MASK, 1) |
			    cdev->mcfg[MRAM_TXB].off);
	} else {
		/* TX FIFO is used for newer IP Core versions */
		m_can_write(cdev, M_CAN_TXBC,
			    FIELD_PREP(TXBC_TFQS_MASK,
				       cdev->mcfg[MRAM_TXB].num) |
			    cdev->mcfg[MRAM_TXB].off);
	}

	/* support 64 bytes payload */
	m_can_write(cdev, M_CAN_TXESC,
		    FIELD_PREP(TXESC_TBDS_MASK, TXESC_TBDS_64B));

	/* TX Event FIFO */
	if (cdev->version == 30) {
		m_can_write(cdev, M_CAN_TXEFC,
			    FIELD_PREP(TXEFC_EFS_MASK, 1) |
			    cdev->mcfg[MRAM_TXE].off);
	} else {
		/* Full TX Event FIFO is used */
		m_can_write(cdev, M_CAN_TXEFC,
			    FIELD_PREP(TXEFC_EFWM_MASK,
				       cdev->tx_max_coalesced_frames_irq) |
			    FIELD_PREP(TXEFC_EFS_MASK,
				       cdev->mcfg[MRAM_TXE].num) |
			    cdev->mcfg[MRAM_TXE].off);
	}

	/* rx fifo configuration, blocking mode, fifo size 1 */
	m_can_write(cdev, M_CAN_RXF0C,
		    FIELD_PREP(RXFC_FWM_MASK, cdev->rx_max_coalesced_frames_irq) |
		    FIELD_PREP(RXFC_FS_MASK, cdev->mcfg[MRAM_RXF0].num) |
		    cdev->mcfg[MRAM_RXF0].off);

	m_can_write(cdev, M_CAN_RXF1C,
		    FIELD_PREP(RXFC_FS_MASK, cdev->mcfg[MRAM_RXF1].num) |
		    cdev->mcfg[MRAM_RXF1].off);

	cccr = m_can_read(cdev, M_CAN_CCCR);
	test = m_can_read(cdev, M_CAN_TEST);
	test &= ~TEST_LBCK;
	if (cdev->version == 30) {
		/* Version 3.0.x */

		cccr &= ~(CCCR_TEST | CCCR_MON | CCCR_DAR |
			  FIELD_PREP(CCCR_CMR_MASK, FIELD_MAX(CCCR_CMR_MASK)) |
			  FIELD_PREP(CCCR_CME_MASK, FIELD_MAX(CCCR_CME_MASK)));

		if (cdev->can.ctrlmode & CAN_CTRLMODE_FD)
			cccr |= FIELD_PREP(CCCR_CME_MASK, CCCR_CME_CANFD_BRS);

	} else {
		/* Version 3.1.x or 3.2.x */
		cccr &= ~(CCCR_TEST | CCCR_MON | CCCR_BRSE | CCCR_FDOE |
			  CCCR_NISO | CCCR_DAR);

		/* Only 3.2.x has NISO Bit implemented */
		if (cdev->can.ctrlmode & CAN_CTRLMODE_FD_NON_ISO)
			cccr |= CCCR_NISO;

		if (cdev->can.ctrlmode & CAN_CTRLMODE_FD)
			cccr |= (CCCR_BRSE | CCCR_FDOE);
	}

	/* Loopback Mode */
	if (cdev->can.ctrlmode & CAN_CTRLMODE_LOOPBACK) {
		cccr |= CCCR_TEST | CCCR_MON;
		test |= TEST_LBCK;
	}

	/* Enable Monitoring (all versions) */
	if (cdev->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		cccr |= CCCR_MON;

	/* Disable Auto Retransmission (all versions) */
	if (cdev->can.ctrlmode & CAN_CTRLMODE_ONE_SHOT)
		cccr |= CCCR_DAR;

	/* Write config */
	m_can_write(cdev, M_CAN_CCCR, cccr);
	m_can_write(cdev, M_CAN_TEST, test);

	/* Enable interrupts */
	if (!(cdev->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING)) {
		if (cdev->version == 30)
			interrupts &= ~(IR_ERR_LEC_30X);
		else
			interrupts &= ~(IR_ERR_LEC_31X);
	}
	cdev->active_interrupts = 0;
	m_can_interrupt_enable(cdev, interrupts);

	/* route all interrupts to INT0 */
	m_can_write(cdev, M_CAN_ILS, ILS_ALL_INT0);

	/* set bittiming params */
	m_can_set_bittiming(dev);

	/* enable internal timestamp generation, with a prescaler of 16. The
	 * prescaler is applied to the nominal bit timing
	 */
	m_can_write(cdev, M_CAN_TSCC,
		    FIELD_PREP(TSCC_TCP_MASK, 0xf) |
		    FIELD_PREP(TSCC_TSS_MASK, TSCC_TSS_INTERNAL));

	err = m_can_config_disable(cdev);
	if (err)
		return err;

	if (cdev->ops->init)
		cdev->ops->init(cdev);

	return 0;
}

static int m_can_start(struct net_device *dev)
{
	struct m_can_classdev *cdev = netdev_priv(dev);
	int ret;

	/* basic m_can configuration */
	ret = m_can_chip_config(dev);
	if (ret)
		return ret;

	netdev_queue_set_dql_min_limit(netdev_get_tx_queue(cdev->net, 0),
				       cdev->tx_max_coalesced_frames);

	cdev->can.state = CAN_STATE_ERROR_ACTIVE;

	m_can_enable_all_interrupts(cdev);

	if (cdev->version > 30)
		cdev->tx_fifo_putidx = FIELD_GET(TXFQS_TFQPI_MASK,
						 m_can_read(cdev, M_CAN_TXFQS));

	ret = m_can_cccr_update_bits(cdev, CCCR_INIT, 0);
	if (ret)
		netdev_err(dev, "failed to enter normal mode\n");

	return ret;
}

static int m_can_set_mode(struct net_device *dev, enum can_mode mode)
{
	switch (mode) {
	case CAN_MODE_START:
		m_can_clean(dev);
		m_can_start(dev);
		netif_wake_queue(dev);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

/* Checks core release number of M_CAN
 * returns 0 if an unsupported device is detected
 * else it returns the release and step coded as:
 * return value = 10 * <release> + 1 * <step>
 */
static int m_can_check_core_release(struct m_can_classdev *cdev)
{
	u32 crel_reg;
	u8 rel;
	u8 step;
	int res;

	/* Read Core Release Version and split into version number
	 * Example: Version 3.2.1 => rel = 3; step = 2; substep = 1;
	 */
	crel_reg = m_can_read(cdev, M_CAN_CREL);
	rel = (u8)FIELD_GET(CREL_REL_MASK, crel_reg);
	step = (u8)FIELD_GET(CREL_STEP_MASK, crel_reg);

	if (rel == 3) {
		/* M_CAN v3.x.y: create return value */
		res = 30 + step;
	} else {
		/* Unsupported M_CAN version */
		res = 0;
	}

	return res;
}

/* Selectable Non ISO support only in version 3.2.x
 * Return 1 if the bit is writable, 0 if it is not, or negative on error.
 */
static int m_can_niso_supported(struct m_can_classdev *cdev)
{
	int ret, niso;

	ret = m_can_config_enable(cdev);
	if (ret)
		return ret;

	/* First try to set the NISO bit. */
	niso = m_can_cccr_update_bits(cdev, CCCR_NISO, CCCR_NISO);

	/* Then clear the it again. */
	ret = m_can_cccr_update_bits(cdev, CCCR_NISO, 0);
	if (ret) {
		dev_err(cdev->dev, "failed to revert the NON-ISO bit in CCCR\n");
		return ret;
	}

	ret = m_can_config_disable(cdev);
	if (ret)
		return ret;

	return niso == 0;
}

static int m_can_dev_setup(struct m_can_classdev *cdev)
{
	struct net_device *dev = cdev->net;
	int m_can_version, err, niso;

	m_can_version = m_can_check_core_release(cdev);
	/* return if unsupported version */
	if (!m_can_version) {
		dev_err(cdev->dev, "Unsupported version number: %2d",
			m_can_version);
		return -EINVAL;
	}

	/* Write the INIT bit, in case no hardware reset has happened before
	 * the probe (for example, it was observed that the Intel Elkhart Lake
	 * SoCs do not properly reset the CAN controllers on reboot)
	 */
	err = m_can_cccr_update_bits(cdev, CCCR_INIT, CCCR_INIT);
	if (err)
		return err;

	if (!cdev->is_peripheral)
		netif_napi_add(dev, &cdev->napi, m_can_poll);

	/* Shared properties of all M_CAN versions */
	cdev->version = m_can_version;
	cdev->can.do_set_mode = m_can_set_mode;
	cdev->can.do_get_berr_counter = m_can_get_berr_counter;

	/* Set M_CAN supported operations */
	cdev->can.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK |
		CAN_CTRLMODE_LISTENONLY |
		CAN_CTRLMODE_BERR_REPORTING |
		CAN_CTRLMODE_FD |
		CAN_CTRLMODE_ONE_SHOT;

	/* Set properties depending on M_CAN version */
	switch (cdev->version) {
	case 30:
		/* CAN_CTRLMODE_FD_NON_ISO is fixed with M_CAN IP v3.0.x */
		err = can_set_static_ctrlmode(dev, CAN_CTRLMODE_FD_NON_ISO);
		if (err)
			return err;
		cdev->can.bittiming_const = &m_can_bittiming_const_30X;
		cdev->can.data_bittiming_const = &m_can_data_bittiming_const_30X;
		break;
	case 31:
		/* CAN_CTRLMODE_FD_NON_ISO is fixed with M_CAN IP v3.1.x */
		err = can_set_static_ctrlmode(dev, CAN_CTRLMODE_FD_NON_ISO);
		if (err)
			return err;
		cdev->can.bittiming_const = &m_can_bittiming_const_31X;
		cdev->can.data_bittiming_const = &m_can_data_bittiming_const_31X;
		break;
	case 32:
	case 33:
		/* Support both MCAN version v3.2.x and v3.3.0 */
		cdev->can.bittiming_const = &m_can_bittiming_const_31X;
		cdev->can.data_bittiming_const = &m_can_data_bittiming_const_31X;

		niso = m_can_niso_supported(cdev);
		if (niso < 0)
			return niso;
		if (niso)
			cdev->can.ctrlmode_supported |= CAN_CTRLMODE_FD_NON_ISO;
		break;
	default:
		dev_err(cdev->dev, "Unsupported version number: %2d",
			cdev->version);
		return -EINVAL;
	}

	return 0;
}

static void m_can_stop(struct net_device *dev)
{
	struct m_can_classdev *cdev = netdev_priv(dev);
	int ret;

	/* disable all interrupts */
	m_can_disable_all_interrupts(cdev);

	/* Set init mode to disengage from the network */
	ret = m_can_cccr_update_bits(cdev, CCCR_INIT, CCCR_INIT);
	if (ret)
		netdev_err(dev, "failed to enter standby mode: %pe\n",
			   ERR_PTR(ret));

	/* set the state as STOPPED */
	cdev->can.state = CAN_STATE_STOPPED;

	if (cdev->ops->deinit) {
		ret = cdev->ops->deinit(cdev);
		if (ret)
			netdev_err(dev, "failed to deinitialize: %pe\n",
				   ERR_PTR(ret));
	}
}

static int m_can_close(struct net_device *dev)
{
	struct m_can_classdev *cdev = netdev_priv(dev);

	netif_stop_queue(dev);

	m_can_stop(dev);
	if (dev->irq)
		free_irq(dev->irq, dev);

	m_can_clean(dev);

	if (cdev->is_peripheral) {
		destroy_workqueue(cdev->tx_wq);
		cdev->tx_wq = NULL;
		can_rx_offload_disable(&cdev->offload);
	} else {
		napi_disable(&cdev->napi);
	}

	close_candev(dev);

	m_can_clk_stop(cdev);
	phy_power_off(cdev->transceiver);

	return 0;
}

static netdev_tx_t m_can_tx_handler(struct m_can_classdev *cdev,
				    struct sk_buff *skb)
{
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;
	u8 len_padded = DIV_ROUND_UP(cf->len, 4);
	struct m_can_fifo_element fifo_element;
	struct net_device *dev = cdev->net;
	u32 cccr, fdflags;
	int err;
	u32 putidx;
	unsigned int frame_len = can_skb_get_frame_len(skb);

	/* Generate ID field for TX buffer Element */
	/* Common to all supported M_CAN versions */
	if (cf->can_id & CAN_EFF_FLAG) {
		fifo_element.id = cf->can_id & CAN_EFF_MASK;
		fifo_element.id |= TX_BUF_XTD;
	} else {
		fifo_element.id = ((cf->can_id & CAN_SFF_MASK) << 18);
	}

	if (cf->can_id & CAN_RTR_FLAG)
		fifo_element.id |= TX_BUF_RTR;

	if (cdev->version == 30) {
		netif_stop_queue(dev);

		fifo_element.dlc = can_fd_len2dlc(cf->len) << 16;

		/* Write the frame ID, DLC, and payload to the FIFO element. */
		err = m_can_fifo_write(cdev, 0, M_CAN_FIFO_ID, &fifo_element, 2);
		if (err)
			goto out_fail;

		err = m_can_fifo_write(cdev, 0, M_CAN_FIFO_DATA,
				       cf->data, len_padded);
		if (err)
			goto out_fail;

		if (cdev->can.ctrlmode & CAN_CTRLMODE_FD) {
			cccr = m_can_read(cdev, M_CAN_CCCR);
			cccr &= ~CCCR_CMR_MASK;
			if (can_is_canfd_skb(skb)) {
				if (cf->flags & CANFD_BRS)
					cccr |= FIELD_PREP(CCCR_CMR_MASK,
							   CCCR_CMR_CANFD_BRS);
				else
					cccr |= FIELD_PREP(CCCR_CMR_MASK,
							   CCCR_CMR_CANFD);
			} else {
				cccr |= FIELD_PREP(CCCR_CMR_MASK, CCCR_CMR_CAN);
			}
			m_can_write(cdev, M_CAN_CCCR, cccr);
		}
		m_can_write(cdev, M_CAN_TXBTIE, 0x1);

		can_put_echo_skb(skb, dev, 0, frame_len);

		m_can_write(cdev, M_CAN_TXBAR, 0x1);
		/* End of xmit function for version 3.0.x */
	} else {
		/* Transmit routine for version >= v3.1.x */

		/* get put index for frame */
		putidx = cdev->tx_fifo_putidx;

		/* Construct DLC Field, with CAN-FD configuration.
		 * Use the put index of the fifo as the message marker,
		 * used in the TX interrupt for sending the correct echo frame.
		 */

		/* get CAN FD configuration of frame */
		fdflags = 0;
		if (can_is_canfd_skb(skb)) {
			fdflags |= TX_BUF_FDF;
			if (cf->flags & CANFD_BRS)
				fdflags |= TX_BUF_BRS;
		}

		fifo_element.dlc = FIELD_PREP(TX_BUF_MM_MASK, putidx) |
			FIELD_PREP(TX_BUF_DLC_MASK, can_fd_len2dlc(cf->len)) |
			fdflags | TX_BUF_EFC;

		memcpy_and_pad(fifo_element.data, CANFD_MAX_DLEN, &cf->data,
			       cf->len, 0);

		err = m_can_fifo_write(cdev, putidx, M_CAN_FIFO_ID,
				       &fifo_element, 2 + len_padded);
		if (err)
			goto out_fail;

		/* Push loopback echo.
		 * Will be looped back on TX interrupt based on message marker
		 */
		can_put_echo_skb(skb, dev, putidx, frame_len);

		if (cdev->is_peripheral) {
			/* Delay enabling TX FIFO element */
			cdev->tx_peripheral_submit |= BIT(putidx);
		} else {
			/* Enable TX FIFO element to start transfer  */
			m_can_write(cdev, M_CAN_TXBAR, BIT(putidx));
		}
		cdev->tx_fifo_putidx = (++cdev->tx_fifo_putidx >= cdev->can.echo_skb_max ?
					0 : cdev->tx_fifo_putidx);
	}

	return NETDEV_TX_OK;

out_fail:
	netdev_err(dev, "FIFO write returned %d\n", err);
	m_can_disable_all_interrupts(cdev);
	return NETDEV_TX_BUSY;
}

static void m_can_tx_submit(struct m_can_classdev *cdev)
{
	if (cdev->version == 30)
		return;
	if (!cdev->is_peripheral)
		return;

	m_can_write(cdev, M_CAN_TXBAR, cdev->tx_peripheral_submit);
	cdev->tx_peripheral_submit = 0;
}

static void m_can_tx_work_queue(struct work_struct *ws)
{
	struct m_can_tx_op *op = container_of(ws, struct m_can_tx_op, work);
	struct m_can_classdev *cdev = op->cdev;
	struct sk_buff *skb = op->skb;

	op->skb = NULL;
	m_can_tx_handler(cdev, skb);
	if (op->submit)
		m_can_tx_submit(cdev);
}

static void m_can_tx_queue_skb(struct m_can_classdev *cdev, struct sk_buff *skb,
			       bool submit)
{
	cdev->tx_ops[cdev->next_tx_op].skb = skb;
	cdev->tx_ops[cdev->next_tx_op].submit = submit;
	queue_work(cdev->tx_wq, &cdev->tx_ops[cdev->next_tx_op].work);

	++cdev->next_tx_op;
	if (cdev->next_tx_op >= cdev->tx_fifo_size)
		cdev->next_tx_op = 0;
}

static netdev_tx_t m_can_start_peripheral_xmit(struct m_can_classdev *cdev,
					       struct sk_buff *skb)
{
	bool submit;

	++cdev->nr_txs_without_submit;
	if (cdev->nr_txs_without_submit >= cdev->tx_max_coalesced_frames ||
	    !netdev_xmit_more()) {
		cdev->nr_txs_without_submit = 0;
		submit = true;
	} else {
		submit = false;
	}
	m_can_tx_queue_skb(cdev, skb, submit);

	return NETDEV_TX_OK;
}

static netdev_tx_t m_can_start_xmit(struct sk_buff *skb,
				    struct net_device *dev)
{
	struct m_can_classdev *cdev = netdev_priv(dev);
	unsigned int frame_len;
	netdev_tx_t ret;

	if (can_dev_dropped_skb(dev, skb))
		return NETDEV_TX_OK;

	frame_len = can_skb_get_frame_len(skb);

	if (cdev->can.state == CAN_STATE_BUS_OFF) {
		m_can_clean(cdev->net);
		return NETDEV_TX_OK;
	}

	ret = m_can_start_tx(cdev);
	if (ret != NETDEV_TX_OK)
		return ret;

	netdev_sent_queue(dev, frame_len);

	if (cdev->is_peripheral)
		ret = m_can_start_peripheral_xmit(cdev, skb);
	else
		ret = m_can_tx_handler(cdev, skb);

	if (ret != NETDEV_TX_OK)
		netdev_completed_queue(dev, 1, frame_len);

	return ret;
}

static enum hrtimer_restart hrtimer_callback(struct hrtimer *timer)
{
	struct m_can_classdev *cdev = container_of(timer, struct
						   m_can_classdev, hrtimer);
	int ret;

	if (cdev->can.state == CAN_STATE_BUS_OFF ||
	    cdev->can.state == CAN_STATE_STOPPED)
		return HRTIMER_NORESTART;

	ret = m_can_interrupt_handler(cdev);

	/* On error or if napi is scheduled to read, stop the timer */
	if (ret < 0 || napi_is_scheduled(&cdev->napi))
		return HRTIMER_NORESTART;

	hrtimer_forward_now(timer, ms_to_ktime(HRTIMER_POLL_INTERVAL_MS));

	return HRTIMER_RESTART;
}

static int m_can_open(struct net_device *dev)
{
	struct m_can_classdev *cdev = netdev_priv(dev);
	int err;

	err = phy_power_on(cdev->transceiver);
	if (err)
		return err;

	err = m_can_clk_start(cdev);
	if (err)
		goto out_phy_power_off;

	/* open the can device */
	err = open_candev(dev);
	if (err) {
		netdev_err(dev, "failed to open can device\n");
		goto exit_disable_clks;
	}

	if (cdev->is_peripheral)
		can_rx_offload_enable(&cdev->offload);
	else
		napi_enable(&cdev->napi);

	/* register interrupt handler */
	if (cdev->is_peripheral) {
		cdev->tx_wq = alloc_ordered_workqueue("mcan_wq",
						      WQ_FREEZABLE | WQ_MEM_RECLAIM);
		if (!cdev->tx_wq) {
			err = -ENOMEM;
			goto out_wq_fail;
		}

		for (int i = 0; i != cdev->tx_fifo_size; ++i) {
			cdev->tx_ops[i].cdev = cdev;
			INIT_WORK(&cdev->tx_ops[i].work, m_can_tx_work_queue);
		}

		err = request_threaded_irq(dev->irq, NULL, m_can_isr,
					   IRQF_ONESHOT,
					   dev->name, dev);
	} else if (dev->irq) {
		err = request_irq(dev->irq, m_can_isr, IRQF_SHARED, dev->name,
				  dev);
	}

	if (err < 0) {
		netdev_err(dev, "failed to request interrupt\n");
		goto exit_irq_fail;
	}

	/* start the m_can controller */
	err = m_can_start(dev);
	if (err)
		goto exit_start_fail;

	netif_start_queue(dev);

	return 0;

exit_start_fail:
	if (cdev->is_peripheral || dev->irq)
		free_irq(dev->irq, dev);
exit_irq_fail:
	if (cdev->is_peripheral)
		destroy_workqueue(cdev->tx_wq);
out_wq_fail:
	if (cdev->is_peripheral)
		can_rx_offload_disable(&cdev->offload);
	else
		napi_disable(&cdev->napi);
	close_candev(dev);
exit_disable_clks:
	m_can_clk_stop(cdev);
out_phy_power_off:
	phy_power_off(cdev->transceiver);
	return err;
}

static const struct net_device_ops m_can_netdev_ops = {
	.ndo_open = m_can_open,
	.ndo_stop = m_can_close,
	.ndo_start_xmit = m_can_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static int m_can_get_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *ec,
			      struct kernel_ethtool_coalesce *kec,
			      struct netlink_ext_ack *ext_ack)
{
	struct m_can_classdev *cdev = netdev_priv(dev);

	ec->rx_max_coalesced_frames_irq = cdev->rx_max_coalesced_frames_irq;
	ec->rx_coalesce_usecs_irq = cdev->rx_coalesce_usecs_irq;
	ec->tx_max_coalesced_frames = cdev->tx_max_coalesced_frames;
	ec->tx_max_coalesced_frames_irq = cdev->tx_max_coalesced_frames_irq;
	ec->tx_coalesce_usecs_irq = cdev->tx_coalesce_usecs_irq;

	return 0;
}

static int m_can_set_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *ec,
			      struct kernel_ethtool_coalesce *kec,
			      struct netlink_ext_ack *ext_ack)
{
	struct m_can_classdev *cdev = netdev_priv(dev);

	if (cdev->can.state != CAN_STATE_STOPPED) {
		netdev_err(dev, "Device is in use, please shut it down first\n");
		return -EBUSY;
	}

	if (ec->rx_max_coalesced_frames_irq > cdev->mcfg[MRAM_RXF0].num) {
		netdev_err(dev, "rx-frames-irq %u greater than the RX FIFO %u\n",
			   ec->rx_max_coalesced_frames_irq,
			   cdev->mcfg[MRAM_RXF0].num);
		return -EINVAL;
	}
	if ((ec->rx_max_coalesced_frames_irq == 0) != (ec->rx_coalesce_usecs_irq == 0)) {
		netdev_err(dev, "rx-frames-irq and rx-usecs-irq can only be set together\n");
		return -EINVAL;
	}
	if (ec->tx_max_coalesced_frames_irq > cdev->mcfg[MRAM_TXE].num) {
		netdev_err(dev, "tx-frames-irq %u greater than the TX event FIFO %u\n",
			   ec->tx_max_coalesced_frames_irq,
			   cdev->mcfg[MRAM_TXE].num);
		return -EINVAL;
	}
	if (ec->tx_max_coalesced_frames_irq > cdev->mcfg[MRAM_TXB].num) {
		netdev_err(dev, "tx-frames-irq %u greater than the TX FIFO %u\n",
			   ec->tx_max_coalesced_frames_irq,
			   cdev->mcfg[MRAM_TXB].num);
		return -EINVAL;
	}
	if ((ec->tx_max_coalesced_frames_irq == 0) != (ec->tx_coalesce_usecs_irq == 0)) {
		netdev_err(dev, "tx-frames-irq and tx-usecs-irq can only be set together\n");
		return -EINVAL;
	}
	if (ec->tx_max_coalesced_frames > cdev->mcfg[MRAM_TXE].num) {
		netdev_err(dev, "tx-frames %u greater than the TX event FIFO %u\n",
			   ec->tx_max_coalesced_frames,
			   cdev->mcfg[MRAM_TXE].num);
		return -EINVAL;
	}
	if (ec->tx_max_coalesced_frames > cdev->mcfg[MRAM_TXB].num) {
		netdev_err(dev, "tx-frames %u greater than the TX FIFO %u\n",
			   ec->tx_max_coalesced_frames,
			   cdev->mcfg[MRAM_TXB].num);
		return -EINVAL;
	}
	if (ec->rx_coalesce_usecs_irq != 0 && ec->tx_coalesce_usecs_irq != 0 &&
	    ec->rx_coalesce_usecs_irq != ec->tx_coalesce_usecs_irq) {
		netdev_err(dev, "rx-usecs-irq %u needs to be equal to tx-usecs-irq %u if both are enabled\n",
			   ec->rx_coalesce_usecs_irq,
			   ec->tx_coalesce_usecs_irq);
		return -EINVAL;
	}

	cdev->rx_max_coalesced_frames_irq = ec->rx_max_coalesced_frames_irq;
	cdev->rx_coalesce_usecs_irq = ec->rx_coalesce_usecs_irq;
	cdev->tx_max_coalesced_frames = ec->tx_max_coalesced_frames;
	cdev->tx_max_coalesced_frames_irq = ec->tx_max_coalesced_frames_irq;
	cdev->tx_coalesce_usecs_irq = ec->tx_coalesce_usecs_irq;

	if (cdev->rx_coalesce_usecs_irq)
		cdev->irq_timer_wait =
			ns_to_ktime(cdev->rx_coalesce_usecs_irq * NSEC_PER_USEC);
	else
		cdev->irq_timer_wait =
			ns_to_ktime(cdev->tx_coalesce_usecs_irq * NSEC_PER_USEC);

	return 0;
}

static const struct ethtool_ops m_can_ethtool_ops_coalescing = {
	.supported_coalesce_params = ETHTOOL_COALESCE_RX_USECS_IRQ |
		ETHTOOL_COALESCE_RX_MAX_FRAMES_IRQ |
		ETHTOOL_COALESCE_TX_USECS_IRQ |
		ETHTOOL_COALESCE_TX_MAX_FRAMES |
		ETHTOOL_COALESCE_TX_MAX_FRAMES_IRQ,
	.get_ts_info = ethtool_op_get_ts_info,
	.get_coalesce = m_can_get_coalesce,
	.set_coalesce = m_can_set_coalesce,
};

static const struct ethtool_ops m_can_ethtool_ops = {
	.get_ts_info = ethtool_op_get_ts_info,
};

static int register_m_can_dev(struct m_can_classdev *cdev)
{
	struct net_device *dev = cdev->net;

	dev->flags |= IFF_ECHO;	/* we support local echo */
	dev->netdev_ops = &m_can_netdev_ops;
	if (dev->irq && cdev->is_peripheral)
		dev->ethtool_ops = &m_can_ethtool_ops_coalescing;
	else
		dev->ethtool_ops = &m_can_ethtool_ops;

	return register_candev(dev);
}

int m_can_check_mram_cfg(struct m_can_classdev *cdev, u32 mram_max_size)
{
	u32 total_size;

	total_size = cdev->mcfg[MRAM_TXB].off - cdev->mcfg[MRAM_SIDF].off +
			cdev->mcfg[MRAM_TXB].num * TXB_ELEMENT_SIZE;
	if (total_size > mram_max_size) {
		dev_err(cdev->dev, "Total size of mram config(%u) exceeds mram(%u)\n",
			total_size, mram_max_size);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(m_can_check_mram_cfg);

static void m_can_of_parse_mram(struct m_can_classdev *cdev,
				const u32 *mram_config_vals)
{
	cdev->mcfg[MRAM_SIDF].off = mram_config_vals[0];
	cdev->mcfg[MRAM_SIDF].num = mram_config_vals[1];
	cdev->mcfg[MRAM_XIDF].off = cdev->mcfg[MRAM_SIDF].off +
		cdev->mcfg[MRAM_SIDF].num * SIDF_ELEMENT_SIZE;
	cdev->mcfg[MRAM_XIDF].num = mram_config_vals[2];
	cdev->mcfg[MRAM_RXF0].off = cdev->mcfg[MRAM_XIDF].off +
		cdev->mcfg[MRAM_XIDF].num * XIDF_ELEMENT_SIZE;
	cdev->mcfg[MRAM_RXF0].num = mram_config_vals[3] &
		FIELD_MAX(RXFC_FS_MASK);
	cdev->mcfg[MRAM_RXF1].off = cdev->mcfg[MRAM_RXF0].off +
		cdev->mcfg[MRAM_RXF0].num * RXF0_ELEMENT_SIZE;
	cdev->mcfg[MRAM_RXF1].num = mram_config_vals[4] &
		FIELD_MAX(RXFC_FS_MASK);
	cdev->mcfg[MRAM_RXB].off = cdev->mcfg[MRAM_RXF1].off +
		cdev->mcfg[MRAM_RXF1].num * RXF1_ELEMENT_SIZE;
	cdev->mcfg[MRAM_RXB].num = mram_config_vals[5];
	cdev->mcfg[MRAM_TXE].off = cdev->mcfg[MRAM_RXB].off +
		cdev->mcfg[MRAM_RXB].num * RXB_ELEMENT_SIZE;
	cdev->mcfg[MRAM_TXE].num = mram_config_vals[6];
	cdev->mcfg[MRAM_TXB].off = cdev->mcfg[MRAM_TXE].off +
		cdev->mcfg[MRAM_TXE].num * TXE_ELEMENT_SIZE;
	cdev->mcfg[MRAM_TXB].num = mram_config_vals[7] &
		FIELD_MAX(TXBC_NDTB_MASK);

	dev_dbg(cdev->dev,
		"sidf 0x%x %d xidf 0x%x %d rxf0 0x%x %d rxf1 0x%x %d rxb 0x%x %d txe 0x%x %d txb 0x%x %d\n",
		cdev->mcfg[MRAM_SIDF].off, cdev->mcfg[MRAM_SIDF].num,
		cdev->mcfg[MRAM_XIDF].off, cdev->mcfg[MRAM_XIDF].num,
		cdev->mcfg[MRAM_RXF0].off, cdev->mcfg[MRAM_RXF0].num,
		cdev->mcfg[MRAM_RXF1].off, cdev->mcfg[MRAM_RXF1].num,
		cdev->mcfg[MRAM_RXB].off, cdev->mcfg[MRAM_RXB].num,
		cdev->mcfg[MRAM_TXE].off, cdev->mcfg[MRAM_TXE].num,
		cdev->mcfg[MRAM_TXB].off, cdev->mcfg[MRAM_TXB].num);
}

int m_can_init_ram(struct m_can_classdev *cdev)
{
	int end, i, start;
	int err = 0;

	/* initialize the entire Message RAM in use to avoid possible
	 * ECC/parity checksum errors when reading an uninitialized buffer
	 */
	start = cdev->mcfg[MRAM_SIDF].off;
	end = cdev->mcfg[MRAM_TXB].off +
		cdev->mcfg[MRAM_TXB].num * TXB_ELEMENT_SIZE;

	for (i = start; i < end; i += 4) {
		err = m_can_fifo_write_no_off(cdev, i, 0x0);
		if (err)
			break;
	}

	return err;
}
EXPORT_SYMBOL_GPL(m_can_init_ram);

int m_can_class_get_clocks(struct m_can_classdev *cdev)
{
	int ret = 0;

	cdev->hclk = devm_clk_get(cdev->dev, "hclk");
	cdev->cclk = devm_clk_get(cdev->dev, "cclk");

	if (IS_ERR(cdev->hclk) || IS_ERR(cdev->cclk)) {
		dev_err(cdev->dev, "no clock found\n");
		ret = -ENODEV;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(m_can_class_get_clocks);

struct m_can_classdev *m_can_class_allocate_dev(struct device *dev,
						int sizeof_priv)
{
	struct m_can_classdev *class_dev = NULL;
	u32 mram_config_vals[MRAM_CFG_LEN];
	struct net_device *net_dev;
	u32 tx_fifo_size;
	int ret;

	ret = fwnode_property_read_u32_array(dev_fwnode(dev),
					     "bosch,mram-cfg",
					     mram_config_vals,
					     sizeof(mram_config_vals) / 4);
	if (ret) {
		dev_err(dev, "Could not get Message RAM configuration.");
		goto out;
	}

	/* Get TX FIFO size
	 * Defines the total amount of echo buffers for loopback
	 */
	tx_fifo_size = mram_config_vals[7];

	/* allocate the m_can device */
	net_dev = alloc_candev(sizeof_priv, tx_fifo_size);
	if (!net_dev) {
		dev_err(dev, "Failed to allocate CAN device");
		goto out;
	}

	class_dev = netdev_priv(net_dev);
	class_dev->net = net_dev;
	class_dev->dev = dev;
	SET_NETDEV_DEV(net_dev, dev);

	m_can_of_parse_mram(class_dev, mram_config_vals);
out:
	return class_dev;
}
EXPORT_SYMBOL_GPL(m_can_class_allocate_dev);

void m_can_class_free_dev(struct net_device *net)
{
	free_candev(net);
}
EXPORT_SYMBOL_GPL(m_can_class_free_dev);

int m_can_class_register(struct m_can_classdev *cdev)
{
	int ret;

	cdev->tx_fifo_size = max(1, min(cdev->mcfg[MRAM_TXB].num,
					cdev->mcfg[MRAM_TXE].num));
	if (cdev->is_peripheral) {
		cdev->tx_ops =
			devm_kzalloc(cdev->dev,
				     cdev->tx_fifo_size * sizeof(*cdev->tx_ops),
				     GFP_KERNEL);
		if (!cdev->tx_ops) {
			dev_err(cdev->dev, "Failed to allocate tx_ops for workqueue\n");
			return -ENOMEM;
		}
	}

	ret = m_can_clk_start(cdev);
	if (ret)
		return ret;

	if (cdev->is_peripheral) {
		ret = can_rx_offload_add_manual(cdev->net, &cdev->offload,
						NAPI_POLL_WEIGHT);
		if (ret)
			goto clk_disable;
	}

	if (!cdev->net->irq) {
		dev_dbg(cdev->dev, "Polling enabled, initialize hrtimer");
		hrtimer_init(&cdev->hrtimer, CLOCK_MONOTONIC,
			     HRTIMER_MODE_REL_PINNED);
		cdev->hrtimer.function = &hrtimer_callback;
	} else {
		hrtimer_init(&cdev->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		cdev->hrtimer.function = m_can_coalescing_timer;
	}

	ret = m_can_dev_setup(cdev);
	if (ret)
		goto rx_offload_del;

	ret = register_m_can_dev(cdev);
	if (ret) {
		dev_err(cdev->dev, "registering %s failed (err=%d)\n",
			cdev->net->name, ret);
		goto rx_offload_del;
	}

	of_can_transceiver(cdev->net);

	dev_info(cdev->dev, "%s device registered (irq=%d, version=%d)\n",
		 KBUILD_MODNAME, cdev->net->irq, cdev->version);

	/* Probe finished
	 * Stop clocks. They will be reactivated once the M_CAN device is opened
	 */
	m_can_clk_stop(cdev);

	return 0;

rx_offload_del:
	if (cdev->is_peripheral)
		can_rx_offload_del(&cdev->offload);
clk_disable:
	m_can_clk_stop(cdev);

	return ret;
}
EXPORT_SYMBOL_GPL(m_can_class_register);

void m_can_class_unregister(struct m_can_classdev *cdev)
{
	if (cdev->is_peripheral)
		can_rx_offload_del(&cdev->offload);
	unregister_candev(cdev->net);
}
EXPORT_SYMBOL_GPL(m_can_class_unregister);

int m_can_class_suspend(struct device *dev)
{
	struct m_can_classdev *cdev = dev_get_drvdata(dev);
	struct net_device *ndev = cdev->net;
	int ret = 0;

	if (netif_running(ndev)) {
		netif_stop_queue(ndev);
		netif_device_detach(ndev);

		/* leave the chip running with rx interrupt enabled if it is
		 * used as a wake-up source. Coalescing needs to be reset then,
		 * the timer is cancelled here, interrupts are done in resume.
		 */
		if (cdev->pm_wake_source) {
			hrtimer_cancel(&cdev->hrtimer);
			m_can_write(cdev, M_CAN_IE, IR_RF0N);

			if (cdev->ops->deinit)
				ret = cdev->ops->deinit(cdev);
		} else {
			m_can_stop(ndev);
		}

		m_can_clk_stop(cdev);
	}

	pinctrl_pm_select_sleep_state(dev);

	cdev->can.state = CAN_STATE_SLEEPING;

	return ret;
}
EXPORT_SYMBOL_GPL(m_can_class_suspend);

int m_can_class_resume(struct device *dev)
{
	struct m_can_classdev *cdev = dev_get_drvdata(dev);
	struct net_device *ndev = cdev->net;
	int ret = 0;

	pinctrl_pm_select_default_state(dev);

	cdev->can.state = CAN_STATE_ERROR_ACTIVE;

	if (netif_running(ndev)) {
		ret = m_can_clk_start(cdev);
		if (ret)
			return ret;

		if (cdev->pm_wake_source) {
			/* Restore active interrupts but disable coalescing as
			 * we may have missed important waterlevel interrupts
			 * between suspend and resume. Timers are already
			 * stopped in suspend. Here we enable all interrupts
			 * again.
			 */
			cdev->active_interrupts |= IR_RF0N | IR_TEFN;

			if (cdev->ops->init)
				ret = cdev->ops->init(cdev);

			m_can_write(cdev, M_CAN_IE, cdev->active_interrupts);
		} else {
			ret  = m_can_start(ndev);
			if (ret) {
				m_can_clk_stop(cdev);
				return ret;
			}
		}

		netif_device_attach(ndev);
		netif_start_queue(ndev);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(m_can_class_resume);

MODULE_AUTHOR("Dong Aisheng <b29396@freescale.com>");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CAN bus driver for Bosch M_CAN controller");
