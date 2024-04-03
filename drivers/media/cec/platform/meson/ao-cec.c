/*
 * Driver for Amlogic Meson AO CEC Controller
 *
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved
 * Copyright (C) 2017 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/reset.h>
#include <media/cec.h>
#include <media/cec-notifier.h>

/* CEC Registers */

/*
 * [2:1] cntl_clk
 *  - 0 = Disable clk (Power-off mode)
 *  - 1 = Enable gated clock (Normal mode)
 *  - 2 = Enable free-run clk (Debug mode)
 */
#define CEC_GEN_CNTL_REG		0x00

#define CEC_GEN_CNTL_RESET		BIT(0)
#define CEC_GEN_CNTL_CLK_DISABLE	0
#define CEC_GEN_CNTL_CLK_ENABLE		1
#define CEC_GEN_CNTL_CLK_ENABLE_DBG	2
#define CEC_GEN_CNTL_CLK_CTRL_MASK	GENMASK(2, 1)

/*
 * [7:0] cec_reg_addr
 * [15:8] cec_reg_wrdata
 * [16] cec_reg_wr
 *  - 0 = Read
 *  - 1 = Write
 * [23] bus free
 * [31:24] cec_reg_rddata
 */
#define CEC_RW_REG			0x04

#define CEC_RW_ADDR			GENMASK(7, 0)
#define CEC_RW_WR_DATA			GENMASK(15, 8)
#define CEC_RW_WRITE_EN			BIT(16)
#define CEC_RW_BUS_BUSY			BIT(23)
#define CEC_RW_RD_DATA			GENMASK(31, 24)

/*
 * [1] tx intr
 * [2] rx intr
 */
#define CEC_INTR_MASKN_REG		0x08
#define CEC_INTR_CLR_REG		0x0c
#define CEC_INTR_STAT_REG		0x10

#define CEC_INTR_TX			BIT(1)
#define CEC_INTR_RX			BIT(2)

/* CEC Commands */

#define CEC_TX_MSG_0_HEADER		0x00
#define CEC_TX_MSG_1_OPCODE		0x01
#define CEC_TX_MSG_2_OP1		0x02
#define CEC_TX_MSG_3_OP2		0x03
#define CEC_TX_MSG_4_OP3		0x04
#define CEC_TX_MSG_5_OP4		0x05
#define CEC_TX_MSG_6_OP5		0x06
#define CEC_TX_MSG_7_OP6		0x07
#define CEC_TX_MSG_8_OP7		0x08
#define CEC_TX_MSG_9_OP8		0x09
#define CEC_TX_MSG_A_OP9		0x0A
#define CEC_TX_MSG_B_OP10		0x0B
#define CEC_TX_MSG_C_OP11		0x0C
#define CEC_TX_MSG_D_OP12		0x0D
#define CEC_TX_MSG_E_OP13		0x0E
#define CEC_TX_MSG_F_OP14		0x0F
#define CEC_TX_MSG_LENGTH		0x10
#define CEC_TX_MSG_CMD			0x11
#define CEC_TX_WRITE_BUF		0x12
#define CEC_TX_CLEAR_BUF		0x13
#define CEC_RX_MSG_CMD			0x14
#define CEC_RX_CLEAR_BUF		0x15
#define CEC_LOGICAL_ADDR0		0x16
#define CEC_LOGICAL_ADDR1		0x17
#define CEC_LOGICAL_ADDR2		0x18
#define CEC_LOGICAL_ADDR3		0x19
#define CEC_LOGICAL_ADDR4		0x1A
#define CEC_CLOCK_DIV_H			0x1B
#define CEC_CLOCK_DIV_L			0x1C
#define CEC_QUIESCENT_25MS_BIT7_0	0x20
#define CEC_QUIESCENT_25MS_BIT11_8	0x21
#define CEC_STARTBITMINL2H_3MS5_BIT7_0	0x22
#define CEC_STARTBITMINL2H_3MS5_BIT8	0x23
#define CEC_STARTBITMAXL2H_3MS9_BIT7_0	0x24
#define CEC_STARTBITMAXL2H_3MS9_BIT8	0x25
#define CEC_STARTBITMINH_0MS6_BIT7_0	0x26
#define CEC_STARTBITMINH_0MS6_BIT8	0x27
#define CEC_STARTBITMAXH_1MS0_BIT7_0	0x28
#define CEC_STARTBITMAXH_1MS0_BIT8	0x29
#define CEC_STARTBITMINTOT_4MS3_BIT7_0	0x2A
#define CEC_STARTBITMINTOT_4MS3_BIT9_8	0x2B
#define CEC_STARTBITMAXTOT_4MS7_BIT7_0	0x2C
#define CEC_STARTBITMAXTOT_4MS7_BIT9_8	0x2D
#define CEC_LOGIC1MINL2H_0MS4_BIT7_0	0x2E
#define CEC_LOGIC1MINL2H_0MS4_BIT8	0x2F
#define CEC_LOGIC1MAXL2H_0MS8_BIT7_0	0x30
#define CEC_LOGIC1MAXL2H_0MS8_BIT8	0x31
#define CEC_LOGIC0MINL2H_1MS3_BIT7_0	0x32
#define CEC_LOGIC0MINL2H_1MS3_BIT8	0x33
#define CEC_LOGIC0MAXL2H_1MS7_BIT7_0	0x34
#define CEC_LOGIC0MAXL2H_1MS7_BIT8	0x35
#define CEC_LOGICMINTOTAL_2MS05_BIT7_0	0x36
#define CEC_LOGICMINTOTAL_2MS05_BIT9_8	0x37
#define CEC_LOGICMAXHIGH_2MS8_BIT7_0	0x38
#define CEC_LOGICMAXHIGH_2MS8_BIT8	0x39
#define CEC_LOGICERRLOW_3MS4_BIT7_0	0x3A
#define CEC_LOGICERRLOW_3MS4_BIT8	0x3B
#define CEC_NOMSMPPOINT_1MS05		0x3C
#define CEC_DELCNTR_LOGICERR		0x3E
#define CEC_TXTIME_17MS_BIT7_0		0x40
#define CEC_TXTIME_17MS_BIT10_8		0x41
#define CEC_TXTIME_2BIT_BIT7_0		0x42
#define CEC_TXTIME_2BIT_BIT10_8		0x43
#define CEC_TXTIME_4BIT_BIT7_0		0x44
#define CEC_TXTIME_4BIT_BIT10_8		0x45
#define CEC_STARTBITNOML2H_3MS7_BIT7_0	0x46
#define CEC_STARTBITNOML2H_3MS7_BIT8	0x47
#define CEC_STARTBITNOMH_0MS8_BIT7_0	0x48
#define CEC_STARTBITNOMH_0MS8_BIT8	0x49
#define CEC_LOGIC1NOML2H_0MS6_BIT7_0	0x4A
#define CEC_LOGIC1NOML2H_0MS6_BIT8	0x4B
#define CEC_LOGIC0NOML2H_1MS5_BIT7_0	0x4C
#define CEC_LOGIC0NOML2H_1MS5_BIT8	0x4D
#define CEC_LOGIC1NOMH_1MS8_BIT7_0	0x4E
#define CEC_LOGIC1NOMH_1MS8_BIT8	0x4F
#define CEC_LOGIC0NOMH_0MS9_BIT7_0	0x50
#define CEC_LOGIC0NOMH_0MS9_BIT8	0x51
#define CEC_LOGICERRLOW_3MS6_BIT7_0	0x52
#define CEC_LOGICERRLOW_3MS6_BIT8	0x53
#define CEC_CHKCONTENTION_0MS1		0x54
#define CEC_PREPARENXTBIT_0MS05_BIT7_0	0x56
#define CEC_PREPARENXTBIT_0MS05_BIT8	0x57
#define CEC_NOMSMPACKPOINT_0MS45	0x58
#define CEC_ACK0NOML2H_1MS5_BIT7_0	0x5A
#define CEC_ACK0NOML2H_1MS5_BIT8	0x5B
#define CEC_BUGFIX_DISABLE_0		0x60
#define CEC_BUGFIX_DISABLE_1		0x61
#define CEC_RX_MSG_0_HEADER		0x80
#define CEC_RX_MSG_1_OPCODE		0x81
#define CEC_RX_MSG_2_OP1		0x82
#define CEC_RX_MSG_3_OP2		0x83
#define CEC_RX_MSG_4_OP3		0x84
#define CEC_RX_MSG_5_OP4		0x85
#define CEC_RX_MSG_6_OP5		0x86
#define CEC_RX_MSG_7_OP6		0x87
#define CEC_RX_MSG_8_OP7		0x88
#define CEC_RX_MSG_9_OP8		0x89
#define CEC_RX_MSG_A_OP9		0x8A
#define CEC_RX_MSG_B_OP10		0x8B
#define CEC_RX_MSG_C_OP11		0x8C
#define CEC_RX_MSG_D_OP12		0x8D
#define CEC_RX_MSG_E_OP13		0x8E
#define CEC_RX_MSG_F_OP14		0x8F
#define CEC_RX_MSG_LENGTH		0x90
#define CEC_RX_MSG_STATUS		0x91
#define CEC_RX_NUM_MSG			0x92
#define CEC_TX_MSG_STATUS		0x93
#define CEC_TX_NUM_MSG			0x94


/* CEC_TX_MSG_CMD definition */
#define TX_NO_OP	0  /* No transaction */
#define TX_REQ_CURRENT	1  /* Transmit earliest message in buffer */
#define TX_ABORT	2  /* Abort transmitting earliest message */
#define TX_REQ_NEXT	3  /* Overwrite earliest msg, transmit next */

/* tx_msg_status definition */
#define TX_IDLE		0  /* No transaction */
#define TX_BUSY		1  /* Transmitter is busy */
#define TX_DONE		2  /* Message successfully transmitted */
#define TX_ERROR	3  /* Message transmitted with error */

/* rx_msg_cmd */
#define RX_NO_OP	0  /* No transaction */
#define RX_ACK_CURRENT	1  /* Read earliest message in buffer */
#define RX_DISABLE	2  /* Disable receiving latest message */
#define RX_ACK_NEXT	3  /* Clear earliest msg, read next */

/* rx_msg_status */
#define RX_IDLE		0  /* No transaction */
#define RX_BUSY		1  /* Receiver is busy */
#define RX_DONE		2  /* Message has been received successfully */
#define RX_ERROR	3  /* Message has been received with error */

/* RX_CLEAR_BUF options */
#define CLEAR_START	1
#define CLEAR_STOP	0

/* CEC_LOGICAL_ADDRx options */
#define LOGICAL_ADDR_MASK	0xf
#define LOGICAL_ADDR_VALID	BIT(4)
#define LOGICAL_ADDR_DISABLE	0

#define CEC_CLK_RATE		32768

struct meson_ao_cec_device {
	struct platform_device		*pdev;
	void __iomem			*base;
	struct clk			*core;
	spinlock_t			cec_reg_lock;
	struct cec_notifier		*notify;
	struct cec_adapter		*adap;
	struct cec_msg			rx_msg;
};

#define writel_bits_relaxed(mask, val, addr) \
	writel_relaxed((readl_relaxed(addr) & ~(mask)) | (val), addr)

static inline int meson_ao_cec_wait_busy(struct meson_ao_cec_device *ao_cec)
{
	ktime_t timeout = ktime_add_us(ktime_get(), 5000);

	while (readl_relaxed(ao_cec->base + CEC_RW_REG) & CEC_RW_BUS_BUSY) {
		if (ktime_compare(ktime_get(), timeout) > 0)
			return -ETIMEDOUT;
	}

	return 0;
}

static void meson_ao_cec_read(struct meson_ao_cec_device *ao_cec,
			     unsigned long address, u8 *data,
			     int *res)
{
	unsigned long flags;
	u32 reg = FIELD_PREP(CEC_RW_ADDR, address);
	int ret = 0;

	if (res && *res)
		return;

	spin_lock_irqsave(&ao_cec->cec_reg_lock, flags);

	ret = meson_ao_cec_wait_busy(ao_cec);
	if (ret)
		goto read_out;

	writel_relaxed(reg, ao_cec->base + CEC_RW_REG);

	ret = meson_ao_cec_wait_busy(ao_cec);
	if (ret)
		goto read_out;

	*data = FIELD_GET(CEC_RW_RD_DATA,
			  readl_relaxed(ao_cec->base + CEC_RW_REG));

read_out:
	spin_unlock_irqrestore(&ao_cec->cec_reg_lock, flags);

	if (res)
		*res = ret;
}

static void meson_ao_cec_write(struct meson_ao_cec_device *ao_cec,
			       unsigned long address, u8 data,
			       int *res)
{
	unsigned long flags;
	u32 reg = FIELD_PREP(CEC_RW_ADDR, address) |
		  FIELD_PREP(CEC_RW_WR_DATA, data) |
		  CEC_RW_WRITE_EN;
	int ret = 0;

	if (res && *res)
		return;

	spin_lock_irqsave(&ao_cec->cec_reg_lock, flags);

	ret = meson_ao_cec_wait_busy(ao_cec);
	if (ret)
		goto write_out;

	writel_relaxed(reg, ao_cec->base + CEC_RW_REG);

write_out:
	spin_unlock_irqrestore(&ao_cec->cec_reg_lock, flags);

	if (res)
		*res = ret;
}

static inline void meson_ao_cec_irq_setup(struct meson_ao_cec_device *ao_cec,
				      bool enable)
{
	u32 cfg = CEC_INTR_TX | CEC_INTR_RX;

	writel_bits_relaxed(cfg, enable ? cfg : 0,
			    ao_cec->base + CEC_INTR_MASKN_REG);
}

static inline int meson_ao_cec_clear(struct meson_ao_cec_device *ao_cec)
{
	int ret = 0;

	meson_ao_cec_write(ao_cec, CEC_RX_MSG_CMD, RX_DISABLE, &ret);
	meson_ao_cec_write(ao_cec, CEC_TX_MSG_CMD, TX_ABORT, &ret);
	meson_ao_cec_write(ao_cec, CEC_RX_CLEAR_BUF, 1, &ret);
	meson_ao_cec_write(ao_cec, CEC_TX_CLEAR_BUF, 1, &ret);
	if (ret)
		return ret;

	udelay(100);

	meson_ao_cec_write(ao_cec, CEC_RX_CLEAR_BUF, 0, &ret);
	meson_ao_cec_write(ao_cec, CEC_TX_CLEAR_BUF, 0, &ret);
	if (ret)
		return ret;

	udelay(100);

	meson_ao_cec_write(ao_cec, CEC_RX_MSG_CMD, RX_NO_OP, &ret);
	meson_ao_cec_write(ao_cec, CEC_TX_MSG_CMD, TX_NO_OP, &ret);

	return ret;
}

static int meson_ao_cec_arbit_bit_time_set(struct meson_ao_cec_device *ao_cec,
					   unsigned int bit_set,
					   unsigned int time_set)
{
	int ret = 0;

	switch (bit_set) {
	case CEC_SIGNAL_FREE_TIME_RETRY:
		meson_ao_cec_write(ao_cec, CEC_TXTIME_4BIT_BIT7_0,
				   time_set & 0xff, &ret);
		meson_ao_cec_write(ao_cec, CEC_TXTIME_4BIT_BIT10_8,
				   (time_set >> 8) & 0x7, &ret);
		break;

	case CEC_SIGNAL_FREE_TIME_NEW_INITIATOR:
		meson_ao_cec_write(ao_cec, CEC_TXTIME_2BIT_BIT7_0,
				   time_set & 0xff, &ret);
		meson_ao_cec_write(ao_cec, CEC_TXTIME_2BIT_BIT10_8,
				   (time_set >> 8) & 0x7, &ret);
		break;

	case CEC_SIGNAL_FREE_TIME_NEXT_XFER:
		meson_ao_cec_write(ao_cec, CEC_TXTIME_17MS_BIT7_0,
				   time_set & 0xff, &ret);
		meson_ao_cec_write(ao_cec, CEC_TXTIME_17MS_BIT10_8,
				   (time_set >> 8) & 0x7, &ret);
		break;
	}

	return ret;
}

static irqreturn_t meson_ao_cec_irq(int irq, void *data)
{
	struct meson_ao_cec_device *ao_cec = data;
	u32 stat = readl_relaxed(ao_cec->base + CEC_INTR_STAT_REG);

	if (stat)
		return IRQ_WAKE_THREAD;

	return IRQ_NONE;
}

static void meson_ao_cec_irq_tx(struct meson_ao_cec_device *ao_cec)
{
	unsigned long tx_status = 0;
	u8 stat;
	int ret = 0;

	meson_ao_cec_read(ao_cec, CEC_TX_MSG_STATUS, &stat, &ret);
	if (ret)
		goto tx_reg_err;

	switch (stat) {
	case TX_DONE:
		tx_status = CEC_TX_STATUS_OK;
		break;

	case TX_BUSY:
		tx_status = CEC_TX_STATUS_ARB_LOST;
		break;

	case TX_IDLE:
		tx_status = CEC_TX_STATUS_LOW_DRIVE;
		break;

	case TX_ERROR:
	default:
		tx_status = CEC_TX_STATUS_NACK;
		break;
	}

	/* Clear Interruption */
	writel_relaxed(CEC_INTR_TX, ao_cec->base + CEC_INTR_CLR_REG);

	/* Stop TX */
	meson_ao_cec_write(ao_cec, CEC_TX_MSG_CMD, TX_NO_OP, &ret);
	if (ret)
		goto tx_reg_err;

	cec_transmit_attempt_done(ao_cec->adap, tx_status);
	return;

tx_reg_err:
	cec_transmit_attempt_done(ao_cec->adap, CEC_TX_STATUS_ERROR);
}

static void meson_ao_cec_irq_rx(struct meson_ao_cec_device *ao_cec)
{
	int i, ret = 0;
	u8 reg;

	meson_ao_cec_read(ao_cec, CEC_RX_MSG_STATUS, &reg, &ret);
	if (reg != RX_DONE)
		goto rx_out;

	meson_ao_cec_read(ao_cec, CEC_RX_NUM_MSG, &reg, &ret);
	if (reg != 1)
		goto rx_out;

	meson_ao_cec_read(ao_cec, CEC_RX_MSG_LENGTH, &reg, &ret);

	ao_cec->rx_msg.len = reg + 1;
	if (ao_cec->rx_msg.len > CEC_MAX_MSG_SIZE)
		ao_cec->rx_msg.len = CEC_MAX_MSG_SIZE;

	for (i = 0; i < ao_cec->rx_msg.len; i++) {
		u8 byte;

		meson_ao_cec_read(ao_cec, CEC_RX_MSG_0_HEADER + i, &byte, &ret);

		ao_cec->rx_msg.msg[i] = byte;
	}

	if (ret)
		goto rx_out;

	cec_received_msg(ao_cec->adap, &ao_cec->rx_msg);

rx_out:
	/* Clear Interruption */
	writel_relaxed(CEC_INTR_RX, ao_cec->base + CEC_INTR_CLR_REG);

	/* Ack RX message */
	meson_ao_cec_write(ao_cec, CEC_RX_MSG_CMD, RX_ACK_CURRENT, &ret);
	meson_ao_cec_write(ao_cec, CEC_RX_MSG_CMD, RX_NO_OP, &ret);

	/* Clear RX buffer */
	meson_ao_cec_write(ao_cec, CEC_RX_CLEAR_BUF, CLEAR_START, &ret);
	meson_ao_cec_write(ao_cec, CEC_RX_CLEAR_BUF, CLEAR_STOP, &ret);
}

static irqreturn_t meson_ao_cec_irq_thread(int irq, void *data)
{
	struct meson_ao_cec_device *ao_cec = data;
	u32 stat = readl_relaxed(ao_cec->base + CEC_INTR_STAT_REG);

	if (stat & CEC_INTR_TX)
		meson_ao_cec_irq_tx(ao_cec);

	meson_ao_cec_irq_rx(ao_cec);

	return IRQ_HANDLED;
}

static int meson_ao_cec_set_log_addr(struct cec_adapter *adap, u8 logical_addr)
{
	struct meson_ao_cec_device *ao_cec = adap->priv;
	int ret = 0;

	meson_ao_cec_write(ao_cec, CEC_LOGICAL_ADDR0,
			   LOGICAL_ADDR_DISABLE, &ret);
	if (ret)
		return ret;

	ret = meson_ao_cec_clear(ao_cec);
	if (ret)
		return ret;

	if (logical_addr == CEC_LOG_ADDR_INVALID)
		return 0;

	meson_ao_cec_write(ao_cec, CEC_LOGICAL_ADDR0,
			   logical_addr & LOGICAL_ADDR_MASK, &ret);
	if (ret)
		return ret;

	udelay(100);

	meson_ao_cec_write(ao_cec, CEC_LOGICAL_ADDR0,
			   (logical_addr & LOGICAL_ADDR_MASK) |
			   LOGICAL_ADDR_VALID, &ret);

	return ret;
}

static int meson_ao_cec_transmit(struct cec_adapter *adap, u8 attempts,
				 u32 signal_free_time, struct cec_msg *msg)
{
	struct meson_ao_cec_device *ao_cec = adap->priv;
	int i, ret = 0;
	u8 reg;

	meson_ao_cec_read(ao_cec, CEC_TX_MSG_STATUS, &reg, &ret);
	if (ret)
		return ret;

	if (reg == TX_BUSY) {
		dev_dbg(&ao_cec->pdev->dev, "%s: busy TX: aborting\n",
			__func__);
		meson_ao_cec_write(ao_cec, CEC_TX_MSG_CMD, TX_ABORT, &ret);
	}

	for (i = 0; i < msg->len; i++) {
		meson_ao_cec_write(ao_cec, CEC_TX_MSG_0_HEADER + i,
				   msg->msg[i], &ret);
	}

	meson_ao_cec_write(ao_cec, CEC_TX_MSG_LENGTH, msg->len - 1, &ret);
	meson_ao_cec_write(ao_cec, CEC_TX_MSG_CMD, TX_REQ_CURRENT, &ret);

	return ret;
}

static int meson_ao_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct meson_ao_cec_device *ao_cec = adap->priv;
	int ret;

	meson_ao_cec_irq_setup(ao_cec, false);

	writel_bits_relaxed(CEC_GEN_CNTL_RESET, CEC_GEN_CNTL_RESET,
			    ao_cec->base + CEC_GEN_CNTL_REG);

	if (!enable)
		return 0;

	/* Enable gated clock (Normal mode). */
	writel_bits_relaxed(CEC_GEN_CNTL_CLK_CTRL_MASK,
			    FIELD_PREP(CEC_GEN_CNTL_CLK_CTRL_MASK,
				       CEC_GEN_CNTL_CLK_ENABLE),
			    ao_cec->base + CEC_GEN_CNTL_REG);

	udelay(100);

	/* Release Reset */
	writel_bits_relaxed(CEC_GEN_CNTL_RESET, 0,
			    ao_cec->base + CEC_GEN_CNTL_REG);

	/* Clear buffers */
	ret = meson_ao_cec_clear(ao_cec);
	if (ret)
		return ret;

	/* CEC arbitration 3/5/7 bit time set. */
	ret = meson_ao_cec_arbit_bit_time_set(ao_cec,
					CEC_SIGNAL_FREE_TIME_RETRY,
					0x118);
	if (ret)
		return ret;
	ret = meson_ao_cec_arbit_bit_time_set(ao_cec,
					CEC_SIGNAL_FREE_TIME_NEW_INITIATOR,
					0x000);
	if (ret)
		return ret;
	ret = meson_ao_cec_arbit_bit_time_set(ao_cec,
					CEC_SIGNAL_FREE_TIME_NEXT_XFER,
					0x2aa);
	if (ret)
		return ret;

	meson_ao_cec_irq_setup(ao_cec, true);

	return 0;
}

static const struct cec_adap_ops meson_ao_cec_ops = {
	.adap_enable = meson_ao_cec_adap_enable,
	.adap_log_addr = meson_ao_cec_set_log_addr,
	.adap_transmit = meson_ao_cec_transmit,
};

static int meson_ao_cec_probe(struct platform_device *pdev)
{
	struct meson_ao_cec_device *ao_cec;
	struct device *hdmi_dev;
	int ret, irq;

	hdmi_dev = cec_notifier_parse_hdmi_phandle(&pdev->dev);

	if (IS_ERR(hdmi_dev))
		return PTR_ERR(hdmi_dev);

	ao_cec = devm_kzalloc(&pdev->dev, sizeof(*ao_cec), GFP_KERNEL);
	if (!ao_cec)
		return -ENOMEM;

	spin_lock_init(&ao_cec->cec_reg_lock);

	ao_cec->adap = cec_allocate_adapter(&meson_ao_cec_ops, ao_cec,
					    "meson_ao_cec",
					    CEC_CAP_DEFAULTS |
					    CEC_CAP_CONNECTOR_INFO,
					    1); /* Use 1 for now */
	if (IS_ERR(ao_cec->adap))
		return PTR_ERR(ao_cec->adap);

	ao_cec->adap->owner = THIS_MODULE;

	ao_cec->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ao_cec->base)) {
		ret = PTR_ERR(ao_cec->base);
		goto out_probe_adapter;
	}

	irq = platform_get_irq(pdev, 0);
	ret = devm_request_threaded_irq(&pdev->dev, irq,
					meson_ao_cec_irq,
					meson_ao_cec_irq_thread,
					0, NULL, ao_cec);
	if (ret) {
		dev_err(&pdev->dev, "irq request failed\n");
		goto out_probe_adapter;
	}

	ao_cec->core = devm_clk_get(&pdev->dev, "core");
	if (IS_ERR(ao_cec->core)) {
		dev_err(&pdev->dev, "core clock request failed\n");
		ret = PTR_ERR(ao_cec->core);
		goto out_probe_adapter;
	}

	ret = clk_prepare_enable(ao_cec->core);
	if (ret) {
		dev_err(&pdev->dev, "core clock enable failed\n");
		goto out_probe_adapter;
	}

	ret = clk_set_rate(ao_cec->core, CEC_CLK_RATE);
	if (ret) {
		dev_err(&pdev->dev, "core clock set rate failed\n");
		goto out_probe_clk;
	}

	device_reset_optional(&pdev->dev);

	ao_cec->pdev = pdev;
	platform_set_drvdata(pdev, ao_cec);

	ao_cec->notify = cec_notifier_cec_adap_register(hdmi_dev, NULL,
							ao_cec->adap);
	if (!ao_cec->notify) {
		ret = -ENOMEM;
		goto out_probe_clk;
	}

	ret = cec_register_adapter(ao_cec->adap, &pdev->dev);
	if (ret < 0)
		goto out_probe_notify;

	/* Setup Hardware */
	writel_relaxed(CEC_GEN_CNTL_RESET,
		       ao_cec->base + CEC_GEN_CNTL_REG);

	return 0;

out_probe_notify:
	cec_notifier_cec_adap_unregister(ao_cec->notify, ao_cec->adap);

out_probe_clk:
	clk_disable_unprepare(ao_cec->core);

out_probe_adapter:
	cec_delete_adapter(ao_cec->adap);

	dev_err(&pdev->dev, "CEC controller registration failed\n");

	return ret;
}

static void meson_ao_cec_remove(struct platform_device *pdev)
{
	struct meson_ao_cec_device *ao_cec = platform_get_drvdata(pdev);

	clk_disable_unprepare(ao_cec->core);

	cec_notifier_cec_adap_unregister(ao_cec->notify, ao_cec->adap);
	cec_unregister_adapter(ao_cec->adap);
}

static const struct of_device_id meson_ao_cec_of_match[] = {
	{ .compatible = "amlogic,meson-gx-ao-cec", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, meson_ao_cec_of_match);

static struct platform_driver meson_ao_cec_driver = {
	.probe   = meson_ao_cec_probe,
	.remove_new = meson_ao_cec_remove,
	.driver  = {
		.name = "meson-ao-cec",
		.of_match_table = meson_ao_cec_of_match,
	},
};

module_platform_driver(meson_ao_cec_driver);

MODULE_DESCRIPTION("Meson AO CEC Controller driver");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("GPL");
