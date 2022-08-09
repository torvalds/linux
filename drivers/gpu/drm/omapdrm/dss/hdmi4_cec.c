// SPDX-License-Identifier: GPL-2.0-only
/*
 * HDMI CEC
 *
 * Based on the CEC code from hdmi_ti_4xxx_ip.c from Android.
 *
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - https://www.ti.com/
 * Authors: Yong Zhi
 *	Mythri pk <mythripk@ti.com>
 *
 * Heavily modified to use the linux CEC framework:
 *
 * Copyright 2016-2017 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "dss.h"
#include "hdmi.h"
#include "hdmi4_core.h"
#include "hdmi4_cec.h"

/* HDMI CEC */
#define HDMI_CEC_DEV_ID                         0x900
#define HDMI_CEC_SPEC                           0x904

/* Not really a debug register, more a low-level control register */
#define HDMI_CEC_DBG_3                          0x91C
#define HDMI_CEC_TX_INIT                        0x920
#define HDMI_CEC_TX_DEST                        0x924
#define HDMI_CEC_SETUP                          0x938
#define HDMI_CEC_TX_COMMAND                     0x93C
#define HDMI_CEC_TX_OPERAND                     0x940
#define HDMI_CEC_TRANSMIT_DATA                  0x97C
#define HDMI_CEC_CA_7_0                         0x988
#define HDMI_CEC_CA_15_8                        0x98C
#define HDMI_CEC_INT_STATUS_0                   0x998
#define HDMI_CEC_INT_STATUS_1                   0x99C
#define HDMI_CEC_INT_ENABLE_0                   0x990
#define HDMI_CEC_INT_ENABLE_1                   0x994
#define HDMI_CEC_RX_CONTROL                     0x9B0
#define HDMI_CEC_RX_COUNT                       0x9B4
#define HDMI_CEC_RX_CMD_HEADER                  0x9B8
#define HDMI_CEC_RX_COMMAND                     0x9BC
#define HDMI_CEC_RX_OPERAND                     0x9C0

#define HDMI_CEC_TX_FIFO_INT_MASK		0x64
#define HDMI_CEC_RETRANSMIT_CNT_INT_MASK	0x2

#define HDMI_CORE_CEC_RETRY    200

static void hdmi_cec_received_msg(struct hdmi_core_data *core)
{
	u32 cnt = hdmi_read_reg(core->base, HDMI_CEC_RX_COUNT) & 0xff;

	/* While there are CEC frames in the FIFO */
	while (cnt & 0x70) {
		/* and the frame doesn't have an error */
		if (!(cnt & 0x80)) {
			struct cec_msg msg = {};
			unsigned int i;

			/* then read the message */
			msg.len = cnt & 0xf;
			if (msg.len > CEC_MAX_MSG_SIZE - 2)
				msg.len = CEC_MAX_MSG_SIZE - 2;
			msg.msg[0] = hdmi_read_reg(core->base,
						   HDMI_CEC_RX_CMD_HEADER);
			msg.msg[1] = hdmi_read_reg(core->base,
						   HDMI_CEC_RX_COMMAND);
			for (i = 0; i < msg.len; i++) {
				unsigned int reg = HDMI_CEC_RX_OPERAND + i * 4;

				msg.msg[2 + i] =
					hdmi_read_reg(core->base, reg);
			}
			msg.len += 2;
			cec_received_msg(core->adap, &msg);
		}
		/* Clear the current frame from the FIFO */
		hdmi_write_reg(core->base, HDMI_CEC_RX_CONTROL, 1);
		/* Wait until the current frame is cleared */
		while (hdmi_read_reg(core->base, HDMI_CEC_RX_CONTROL) & 1)
			udelay(1);
		/*
		 * Re-read the count register and loop to see if there are
		 * more messages in the FIFO.
		 */
		cnt = hdmi_read_reg(core->base, HDMI_CEC_RX_COUNT) & 0xff;
	}
}

void hdmi4_cec_irq(struct hdmi_core_data *core)
{
	u32 stat0 = hdmi_read_reg(core->base, HDMI_CEC_INT_STATUS_0);
	u32 stat1 = hdmi_read_reg(core->base, HDMI_CEC_INT_STATUS_1);

	hdmi_write_reg(core->base, HDMI_CEC_INT_STATUS_0, stat0);
	hdmi_write_reg(core->base, HDMI_CEC_INT_STATUS_1, stat1);

	if (stat0 & 0x20) {
		cec_transmit_done(core->adap, CEC_TX_STATUS_OK,
				  0, 0, 0, 0);
		REG_FLD_MOD(core->base, HDMI_CEC_DBG_3, 0x1, 7, 7);
	} else if (stat1 & 0x02) {
		u32 dbg3 = hdmi_read_reg(core->base, HDMI_CEC_DBG_3);

		cec_transmit_done(core->adap,
				  CEC_TX_STATUS_NACK |
				  CEC_TX_STATUS_MAX_RETRIES,
				  0, (dbg3 >> 4) & 7, 0, 0);
		REG_FLD_MOD(core->base, HDMI_CEC_DBG_3, 0x1, 7, 7);
	}
	if (stat0 & 0x02)
		hdmi_cec_received_msg(core);
}

static bool hdmi_cec_clear_tx_fifo(struct cec_adapter *adap)
{
	struct hdmi_core_data *core = cec_get_drvdata(adap);
	int retry = HDMI_CORE_CEC_RETRY;
	int temp;

	REG_FLD_MOD(core->base, HDMI_CEC_DBG_3, 0x1, 7, 7);
	while (retry) {
		temp = hdmi_read_reg(core->base, HDMI_CEC_DBG_3);
		if (FLD_GET(temp, 7, 7) == 0)
			break;
		retry--;
	}
	return retry != 0;
}

static bool hdmi_cec_clear_rx_fifo(struct cec_adapter *adap)
{
	struct hdmi_core_data *core = cec_get_drvdata(adap);
	int retry = HDMI_CORE_CEC_RETRY;
	int temp;

	hdmi_write_reg(core->base, HDMI_CEC_RX_CONTROL, 0x3);
	retry = HDMI_CORE_CEC_RETRY;
	while (retry) {
		temp = hdmi_read_reg(core->base, HDMI_CEC_RX_CONTROL);
		if (FLD_GET(temp, 1, 0) == 0)
			break;
		retry--;
	}
	return retry != 0;
}

static int hdmi_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct hdmi_core_data *core = cec_get_drvdata(adap);
	int temp, err;

	if (!enable) {
		hdmi_write_reg(core->base, HDMI_CEC_INT_ENABLE_0, 0);
		hdmi_write_reg(core->base, HDMI_CEC_INT_ENABLE_1, 0);
		REG_FLD_MOD(core->base, HDMI_CORE_SYS_INTR_UNMASK4, 0, 3, 3);
		hdmi_wp_clear_irqenable(core->wp, HDMI_IRQ_CORE);
		hdmi_wp_set_irqstatus(core->wp, HDMI_IRQ_CORE);
		REG_FLD_MOD(core->wp->base, HDMI_WP_CLK, 0, 5, 0);
		hdmi4_core_disable(core);
		return 0;
	}
	err = hdmi4_core_enable(core);
	if (err)
		return err;

	/*
	 * Initialize CEC clock divider: CEC needs 2MHz clock hence
	 * set the divider to 24 to get 48/24=2MHz clock
	 */
	REG_FLD_MOD(core->wp->base, HDMI_WP_CLK, 0x18, 5, 0);

	/* Clear TX FIFO */
	if (!hdmi_cec_clear_tx_fifo(adap)) {
		pr_err("cec-%s: could not clear TX FIFO\n", adap->name);
		err = -EIO;
		goto err_disable_clk;
	}

	/* Clear RX FIFO */
	if (!hdmi_cec_clear_rx_fifo(adap)) {
		pr_err("cec-%s: could not clear RX FIFO\n", adap->name);
		err = -EIO;
		goto err_disable_clk;
	}

	/* Clear CEC interrupts */
	hdmi_write_reg(core->base, HDMI_CEC_INT_STATUS_1,
		hdmi_read_reg(core->base, HDMI_CEC_INT_STATUS_1));
	hdmi_write_reg(core->base, HDMI_CEC_INT_STATUS_0,
		hdmi_read_reg(core->base, HDMI_CEC_INT_STATUS_0));

	/* Enable HDMI core interrupts */
	hdmi_wp_set_irqenable(core->wp, HDMI_IRQ_CORE);
	/* Unmask CEC interrupt */
	REG_FLD_MOD(core->base, HDMI_CORE_SYS_INTR_UNMASK4, 0x1, 3, 3);
	/*
	 * Enable CEC interrupts:
	 * Transmit Buffer Full/Empty Change event
	 * Receiver FIFO Not Empty event
	 */
	hdmi_write_reg(core->base, HDMI_CEC_INT_ENABLE_0, 0x22);
	/*
	 * Enable CEC interrupts:
	 * Frame Retransmit Count Exceeded event
	 */
	hdmi_write_reg(core->base, HDMI_CEC_INT_ENABLE_1, 0x02);

	/* cec calibration enable (self clearing) */
	hdmi_write_reg(core->base, HDMI_CEC_SETUP, 0x03);
	msleep(20);
	hdmi_write_reg(core->base, HDMI_CEC_SETUP, 0x04);

	temp = hdmi_read_reg(core->base, HDMI_CEC_SETUP);
	if (FLD_GET(temp, 4, 4) != 0) {
		temp = FLD_MOD(temp, 0, 4, 4);
		hdmi_write_reg(core->base, HDMI_CEC_SETUP, temp);

		/*
		 * If we enabled CEC in middle of a CEC message on the bus,
		 * we could have start bit irregularity and/or short
		 * pulse event. Clear them now.
		 */
		temp = hdmi_read_reg(core->base, HDMI_CEC_INT_STATUS_1);
		temp = FLD_MOD(0x0, 0x5, 2, 0);
		hdmi_write_reg(core->base, HDMI_CEC_INT_STATUS_1, temp);
	}
	return 0;

err_disable_clk:
	REG_FLD_MOD(core->wp->base, HDMI_WP_CLK, 0, 5, 0);
	hdmi4_core_disable(core);

	return err;
}

static int hdmi_cec_adap_log_addr(struct cec_adapter *adap, u8 log_addr)
{
	struct hdmi_core_data *core = cec_get_drvdata(adap);
	u32 v;

	if (log_addr == CEC_LOG_ADDR_INVALID) {
		hdmi_write_reg(core->base, HDMI_CEC_CA_7_0, 0);
		hdmi_write_reg(core->base, HDMI_CEC_CA_15_8, 0);
		return 0;
	}
	if (log_addr <= 7) {
		v = hdmi_read_reg(core->base, HDMI_CEC_CA_7_0);
		v |= 1 << log_addr;
		hdmi_write_reg(core->base, HDMI_CEC_CA_7_0, v);
	} else {
		v = hdmi_read_reg(core->base, HDMI_CEC_CA_15_8);
		v |= 1 << (log_addr - 8);
		hdmi_write_reg(core->base, HDMI_CEC_CA_15_8, v);
	}
	return 0;
}

static int hdmi_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				   u32 signal_free_time, struct cec_msg *msg)
{
	struct hdmi_core_data *core = cec_get_drvdata(adap);
	int temp;
	u32 i;

	/* Clear TX FIFO */
	if (!hdmi_cec_clear_tx_fifo(adap)) {
		pr_err("cec-%s: could not clear TX FIFO for transmit\n",
		       adap->name);
		return -EIO;
	}

	/* Clear TX interrupts */
	hdmi_write_reg(core->base, HDMI_CEC_INT_STATUS_0,
		       HDMI_CEC_TX_FIFO_INT_MASK);

	hdmi_write_reg(core->base, HDMI_CEC_INT_STATUS_1,
		       HDMI_CEC_RETRANSMIT_CNT_INT_MASK);

	/* Set the retry count */
	REG_FLD_MOD(core->base, HDMI_CEC_DBG_3, attempts - 1, 6, 4);

	/* Set the initiator addresses */
	hdmi_write_reg(core->base, HDMI_CEC_TX_INIT, cec_msg_initiator(msg));

	/* Set destination id */
	temp = cec_msg_destination(msg);
	if (msg->len == 1)
		temp |= 0x80;
	hdmi_write_reg(core->base, HDMI_CEC_TX_DEST, temp);
	if (msg->len == 1)
		return 0;

	/* Setup command and arguments for the command */
	hdmi_write_reg(core->base, HDMI_CEC_TX_COMMAND, msg->msg[1]);

	for (i = 0; i < msg->len - 2; i++)
		hdmi_write_reg(core->base, HDMI_CEC_TX_OPERAND + i * 4,
			       msg->msg[2 + i]);

	/* Operand count */
	hdmi_write_reg(core->base, HDMI_CEC_TRANSMIT_DATA,
		       (msg->len - 2) | 0x10);
	return 0;
}

static const struct cec_adap_ops hdmi_cec_adap_ops = {
	.adap_enable = hdmi_cec_adap_enable,
	.adap_log_addr = hdmi_cec_adap_log_addr,
	.adap_transmit = hdmi_cec_adap_transmit,
};

void hdmi4_cec_set_phys_addr(struct hdmi_core_data *core, u16 pa)
{
	cec_s_phys_addr(core->adap, pa, false);
}

int hdmi4_cec_init(struct platform_device *pdev, struct hdmi_core_data *core,
		  struct hdmi_wp_data *wp)
{
	const u32 caps = CEC_CAP_TRANSMIT | CEC_CAP_LOG_ADDRS |
			 CEC_CAP_PASSTHROUGH | CEC_CAP_RC;
	int ret;

	core->adap = cec_allocate_adapter(&hdmi_cec_adap_ops, core,
		"omap4", caps, CEC_MAX_LOG_ADDRS);
	ret = PTR_ERR_OR_ZERO(core->adap);
	if (ret < 0)
		return ret;
	core->wp = wp;

	/* Disable clock initially, hdmi_cec_adap_enable() manages it */
	REG_FLD_MOD(core->wp->base, HDMI_WP_CLK, 0, 5, 0);

	ret = cec_register_adapter(core->adap, &pdev->dev);
	if (ret < 0) {
		cec_delete_adapter(core->adap);
		return ret;
	}
	return 0;
}

void hdmi4_cec_uninit(struct hdmi_core_data *core)
{
	cec_unregister_adapter(core->adap);
}
