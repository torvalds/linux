/*
 * Driver for Rockchip Smart Card Reader Controller
 *
 * Copyright (C) 2012-2016 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/kthread.h>

#include "rk_scr.h"

#undef DEBUG_RK_SCR
#define DEBUG_RK_SCR 1

#if DEBUG_RK_SCR
#define DAL_LOGV(x...) pr_info("RK_SCR: "x)
#else
#define DAL_LOGV(x...) do { } while (0)
#endif

#define SMC_DEFAULT_TIMEOUT		2000 /*ms*/
#define SMC_RECEIVE_BUF_LEN		(64 * 1024)

struct rk_scr_device {
	int irq;
	struct clk *clk_scr;
	void __iomem *regs;
	struct scr_chip_info chip_info[RK_SCR_NUM];
	struct rk_scr scr[RK_SCR_NUM];
	struct completion is_done;
	struct mutex scr_mutex; /* mutex for scr operation */
	unsigned char *recv_buffer;
	unsigned recv_data_count;
	unsigned recv_data_offset;
	unsigned char atr_buffer[SMC_ATR_MAX_LENGTH];
	unsigned char atr_length;
};

static struct rk_scr_device *rk_scr;

static struct rk_scr *to_rk_scr(int id)
{
	if (id < RK_SCR_NUM)
		return &rk_scr->scr[id];

	return NULL;
}

static struct rk_scr *to_opened_rk_scr(int id)
{
	struct rk_scr *scr;

	scr = to_rk_scr(id);

	if (scr && scr->is_open)
		return scr;

	return NULL;
}

static void rk_scr_deactive(struct rk_scr *scr)
{
	struct scr_reg_t *scr_reg = scr->hw->reg_base;

	DAL_LOGV("Deactive card\n");
	scr_reg->CTRL2 |= DEACT;
	scr_reg->CTRL1 = 0;
	scr->is_active = false;
}

static void rk_scr_set_clk(struct rk_scr *scr)
{
	struct scr_reg_t *scr_reg = scr->hw->reg_base;
	unsigned int freq_mhz;

	freq_mhz = clk_get_rate(scr->clk) / 1000 / 1000;
	DAL_LOGV("freq_mhz = %d\n", freq_mhz);
	scr_reg->CGSCDIV = ((2 * freq_mhz / 13 - 1)
				+ (freq_mhz / 8 - 1) + 1) / 2;
	DAL_LOGV("scr_reg->CGSCDIV = %d\n", scr_reg->CGSCDIV);
}

static void rk_scr_set_work_waitingtime(struct rk_scr *scr,
					unsigned char wi)
{
	struct scr_reg_t *scr_reg = scr->hw->reg_base;
	unsigned int wt;

	DAL_LOGV("WI: %d\n", wi);
	wt = 960 * wi * scr->D;
	scr_reg->C2CLIM = (wt > 0x0FFFF) ? 0x0FFFF : wt;
}

static void rk_scr_set_etu_duration(struct rk_scr *scr,	unsigned int F,
				    unsigned int D)
{
	struct scr_reg_t *scr_reg = scr->hw->reg_base;

	DAL_LOGV("Set Etu F: %d D: %d\n", F, D);

	scr->F = F;
	scr->D = D;
	scr_reg->CGBITDIV = (scr_reg->CGSCDIV + 1) * (F / D) - 1;
	DAL_LOGV("scr_reg->CGBITDIV = %d\n", scr_reg->CGBITDIV);
	scr_reg->CGBITTUNE = 0;

	rk_scr_set_work_waitingtime(scr, 10);
}

static void rk_scr_set_scr_voltage(struct rk_scr *scr,
				   enum hal_scr_voltage_e level)
{
	struct scr_reg_t *scr_reg = scr->hw->reg_base;

	scr_reg->CTRL2 = 0;

	switch (level) {
	case HAL_SCR_VOLTAGE_CLASS_A:
		scr_reg->CTRL2 |= VCC50;
		break;

	case HAL_SCR_VOLTAGE_CLASS_B:
		scr_reg->CTRL2 |= VCC33;
		break;

	case HAL_SCR_VOLTAGE_CLASS_C:
		scr_reg->CTRL2 |= VCC18;
		break;

	case HAL_SCR_VOLTAGE_NULL:
		break;
	}
}

static void rk_scr_powerdown(struct rk_scr *scr)
{
	rk_scr_set_scr_voltage(scr, HAL_SCR_VOLTAGE_NULL);
}

static void rk_scr_set_clockstop_mode(struct rk_scr *scr,
				      enum hal_scr_clock_stop_mode_e mode)
{
	struct scr_reg_t *scr_reg = scr->hw->reg_base;

	if (mode == HAL_SCR_CLOCK_STOP_L)
		scr_reg->CTRL1 &= ~CLKSTOPVAL;
	else if (mode == HAL_SCR_CLOCK_STOP_H)
		scr_reg->CTRL1 |= CLKSTOPVAL;
}

static void rk_scr_clock_start(struct rk_scr *scr)
{
	struct scr_reg_t *scr_reg = scr->hw->reg_base;
	int time_out = 10000;

#ifdef SCR_DEBUG
	scr_reg->INTEN1 = CLKSTOPRUN;
#endif
	scr_reg->CTRL1 &= ~CLKSTOP;
#ifdef SCR_DEBUG
	if (scr_reg->CTRL1 & CLKSTOP)
		DAL_LOGV("Before clock is Stopped\n");
	else
		DAL_LOGV("Before clock is running\n");
#endif
	while ((scr_reg->CTRL1 & CLKSTOP) && (time_out-- > 0))
		usleep_range(100, 110);
}

static void rk_scr_clock_stop(struct rk_scr *scr)
{
	struct scr_reg_t *scr_reg = scr->hw->reg_base;
	int time_out = 10000;

#ifdef SCR_DEBUG
	scr_reg->INTEN1 = CLKSTOPRUN;
#endif
	scr_reg->CTRL1 |= CLKSTOP;
	DAL_LOGV("Stop Clock\n");
	if (scr->is_active) {
		while ((!(scr_reg->CTRL1 & CLKSTOP)) && (time_out-- > 0))
			usleep_range(100, 110);
	}
}

static void rk_scr_reset(struct rk_scr *scr, unsigned char *rx_buffer)
{
	struct scr_reg_t *scr_reg = scr->hw->reg_base;

	if (!rx_buffer)
		DAL_LOGV("_scr_reset: invalid argument\n");

	/*
	 * must disable all SCR interrupts.
	 * It will protect the global data.
	 */
	scr_reg->INTEN1 = 0;

	scr->rx_buf = rx_buffer;
	scr->rx_expected = 0xff;
	scr->rx_cnt = 0;

	/*
	 * must in the critical section. If we don't, when we have written CTRL2
	 * before enable expected interrupts, other interrupts occurred,
	 * we may miss expected interrupts.
	 */
	if (scr->is_active) {
		DAL_LOGV("Warm Reset\n");
		scr_reg->CTRL2 |= WARMRST;
	} else {
		DAL_LOGV("Active & Cold Reset\n");
		scr->is_active = true;
		scr_reg->CTRL1 = TXEN | RXEN | TS2FIFO | ATRSTFLUSH | GINTEN;
		scr_reg->CTRL2 |= ACT;
	}

	/*
	 * If we enable the interrupts before write CTRL2, we may get
	 * expected interrupts which belong to the last transfer not
	 * for the reset.This may damage the global data.
	 */
	scr_reg->RXFIFOTH = MAX_RXTHR;
	scr_reg->TXFIFOTH = MAX_TXTHR;
	scr_reg->INTEN1 = RXTHRESHOLD | RXFIFULL | RXPERR |
			C2CFULL | ATRFAIL | ATRDONE;
	DAL_LOGV("Start Rx\n");
}

static void rk_scr_write_bytes(struct rk_scr *scr)
{
	struct scr_reg_t *scr_reg = scr->hw->reg_base;
	int	count = FIFO_DEPTH - scr_reg->TXFIFOCNT;
	int	remainder = scr->tx_expected - scr->tx_cnt;
	int i = 0;

	if (remainder < count)
		count = remainder;

	while (i++ < count)
		scr_reg->FIFODATA = scr->tx_buf[scr->tx_cnt++];
}

static void rk_scr_read_bytes(struct rk_scr *scr)
{
	struct scr_reg_t *scr_reg = scr->hw->reg_base;
	int count = scr_reg->RXFIFOCNT;
	int remainder = scr->rx_expected - scr->rx_cnt;
	int i = 0;

	if (remainder < count)
		count = remainder;

	while (i++ < count)
		scr->rx_buf[scr->rx_cnt++] = (unsigned char)scr_reg->FIFODATA;
}

static irqreturn_t rk_scr_irqhandler(int irq, void *priv)
{
	struct rk_scr *scr = (struct rk_scr *)priv;
	struct scr_reg_t *scr_reg = scr->hw->reg_base;
	enum hal_scr_irq_cause_e user_cause = HAL_SCR_IRQ_INVALID;
	unsigned int stat;

	stat = (unsigned int)scr_reg->INTSTAT1;
	if (!stat)
		return 0;

	if (stat & TXFIEMPTY) {
		scr_reg->INTSTAT1 |= TXFIEMPTY;

		/* during this period, TXFIEMPTY may occurred. */
		rk_scr_write_bytes(scr);

		if (scr->tx_cnt == scr->tx_expected) {
			scr_reg->INTEN1 &= ~TXFIEMPTY;
			scr_reg->INTSTAT1 |= TXFIEMPTY;
		}
	}
#ifdef SCR_DEBUG
	else if (stat & CLKSTOPRUN) {
		scr_reg->INTSTAT1 |= CLKSTOPRUN;

		if (scr_reg->CTRL1 & CLKSTOP)
			DAL_LOGV("Clock	is stopped\n");
		else
			DAL_LOGV("Clock	is started\n");
	}
#endif
	else if ((stat & RXTHRESHOLD) || (stat & RXFIFULL)) {
		unsigned int threshold;

		scr_reg->INTEN1 &= ~RXTHRESHOLD;
		scr_reg->INTSTAT1 |= RXTHRESHOLD | RXFIFULL;

		if (scr->rx_cnt < scr->rx_expected) {
			rk_scr_read_bytes(scr);
			if (scr->rx_cnt < scr->rx_expected) {
				unsigned int remainder =
						scr->rx_expected - scr->rx_cnt;

				threshold = (remainder < MAX_RXTHR)
						? remainder : MAX_RXTHR;
			} else {
				scr_reg->INTEN1 &= ~C2CFULL;
				threshold = 1;
				if (scr->user_mask.rx_success)
					user_cause = HAL_SCR_RX_SUCCESS;
			}
		} else {
			threshold = 1;
			scr->rx_buf[scr->rx_cnt++] =
					(unsigned char)scr_reg->FIFODATA;
			if (scr->user_mask.extra_rx)
				user_cause = HAL_SCR_EXTRA_RX;
		}
		scr_reg->INTEN1 |= RXTHRESHOLD;
		/*
		 * when RX FIFO now is FULL,
		 * that will not generate RXTHRESHOLD interrupt.
		 * But it will generate RXFIFULL interrupt.
		 */
		scr_reg->RXFIFOTH = FIFO_DEPTH;
		scr_reg->RXFIFOTH = threshold;
	} else if (stat & ATRDONE) {
		DAL_LOGV("ATR Done\n");
		scr_reg->INTSTAT1 |= ATRDONE;
		scr_reg->INTEN1 = 0;
		rk_scr_read_bytes(scr);
		if (scr->user_mask.atr_success)
			user_cause = HAL_SCR_ATR_SUCCESS;
	} else if (stat & ATRFAIL) {
		DAL_LOGV("ATR Fail\n");

		scr_reg->INTSTAT1 |= ATRFAIL;
		scr_reg->INTEN1 = 0;

		if (scr->user_mask.reset_timeout)
			user_cause = HAL_SCR_RESET_TIMEOUT;
	} else if (stat & TXPERR) {
		DAL_LOGV("TXPERR\n");

		scr_reg->INTSTAT1 |= TXPERR;
		scr_reg->INTEN1 = 0;

		if (scr->user_mask.parity_error)
			user_cause = HAL_SCR_PARITY_ERROR;
	} else if (stat & RXPERR) {
		DAL_LOGV("RXPERR\n");
		scr_reg->INTSTAT1 |= RXPERR;
		scr_reg->INTEN1 = 0;
		rk_scr_read_bytes(scr);
		if (scr->user_mask.parity_error)
			user_cause = HAL_SCR_PARITY_ERROR;
	} else if (stat & C2CFULL) {
		DAL_LOGV("Timeout\n");
		scr_reg->INTSTAT1 |= C2CFULL;
		scr_reg->INTEN1 = 0;
		rk_scr_read_bytes(scr);

		if (scr->user_mask.wwt_timeout)
			user_cause = HAL_SCR_WWT_TIMEOUT;
	}

	if (user_cause != HAL_SCR_IRQ_INVALID) {
		scr->in_process = false;
		if (scr->user_handler)
			scr->user_handler(user_cause);
	}
	return 0;
}

static void _rk_scr_init(struct rk_scr *scr)
{
	struct scr_reg_t *scr_reg = scr->hw->reg_base;

	rk_scr_deactive(scr);
	rk_scr_set_clk(scr);
	rk_scr_set_etu_duration(scr, 372, 1);

	/* TXREPEAT = 3 & RXREPEAT = 3 */
	scr_reg->REPEAT = 0x33;

	/*
	 * Character LeadEdge to Character LeadEdge minimum waiting time
	 * in terms of ETUs. (GT)
	 */
	scr_reg->SCGT = 12;

	/*
	 * Character LeadEdge to Character LeadEdge maximum waiting time
	 * in terms of ETUs. (WT)
	 */
	scr_reg->C2CLIM = 9600;

	/*
	 * If no Vpp is necessary, the activation and deactivation part of Vpp
	 * can be omitted by clearing the AUTOADEAVPP bit in SCPADS register.
	 */
	scr_reg->SCPADS = 0;

	/*
	 * Activation / deactivation step time
	 * in terms of SmartCard Clock Cycles
	 */
	scr_reg->ADEATIME = 0;

	/*
	 * Duration of low state during Smart Card reset sequence
	 * in terms of smart card clock cycles
	 * require >
	 */
	scr_reg->LOWRSTTIME = 1000;

	/*
	 * ATR start limit - in terms of SmartCard Clock Cycles
	 * require 400 ~ 40000
	 */
	scr_reg->ATRSTARTLIMIT = 40000;

	/* enable the detect interrupt */
	scr_reg->INTEN1 = SCINS;
	scr_reg->INTEN2 = 0;

	scr_reg->INTSTAT1 = 0xffff;
	scr_reg->INTSTAT2 = 0xffff;

	scr_reg->FIFOCTRL = FC_TXFIFLUSH | FC_RXFIFLUSH;
	scr_reg->TXFIFOTH = 0;
	scr_reg->RXFIFOTH = 0;

	scr_reg->CTRL1 = 0;
	scr_reg->CTRL2 = 0;
}

static void _rk_scr_deinit(struct rk_scr *scr)
{
	struct scr_reg_t *scr_reg = scr->hw->reg_base;

	/* disable all interrupt */
	scr_reg->INTEN1 = 0;
	scr_reg->INTEN2 = 0;

	rk_scr_deactive(scr);
	rk_scr_powerdown(scr);
}

int rk_scr_freqchanged_notifiy(struct notifier_block *nb,
			       unsigned long action, void *data, int len)
{
	int idx;
	struct rk_scr *scr = NULL;
	/*alter by xieshufa not sure*/
	struct clk_notifier_data *msg;

	switch (action) {
	/*case ABORT_RATE_CHANGE:*/
	case POST_RATE_CHANGE:
		break;
	default:
		return 0;
	}

	msg = data;
	for (idx = 0; idx < RK_SCR_NUM; idx++) {
		struct rk_scr *p = to_rk_scr(idx);

		if (msg->clk == p->clk) {
			scr = p;
			break;
		}
	}

	if (scr) {
		rk_scr_set_clk(scr);
		rk_scr_set_etu_duration(scr, scr->F, scr->D);
	}

	return 0;
}

static int rk_scr_open(int id)
{
	struct rk_scr_device *rk_scr_dev = rk_scr;
	struct rk_scr *scr = to_rk_scr(id);
	struct hal_scr_irq_status_t
		default_scr_user_mask = {1, 1, 1, 1, 1, 1, 1, 1};
	int result = 0;

	if (!scr)
		return -1;

	rk_scr_dev->chip_info[id].reg_base = rk_scr_dev->regs;
	rk_scr_dev->chip_info[id].irq = rk_scr_dev->irq;

	scr->hw = &rk_scr_dev->chip_info[id];
	scr->clk = rk_scr_dev->clk_scr;

	result = clk_prepare_enable(scr->clk);
	DAL_LOGV("scr clk_enable result = %d\n", result);

	(&scr->freq_changed_notifier)->priority = 0;
	clk_notifier_register(scr->clk, &scr->freq_changed_notifier);
	scr->user_mask = default_scr_user_mask;

	_rk_scr_init(scr);

	scr->is_open = true;

	return 0;
}

static void rk_scr_close(int id)
{
	struct rk_scr *scr;

	scr = to_opened_rk_scr(id);
	if (!scr)
		return;

	scr->is_open = false;

	_rk_scr_deinit(scr);

	if (scr->clk) {
		clk_disable(scr->clk);
		clk_notifier_unregister(scr->clk, &scr->freq_changed_notifier);
	}
}

static int rk_scr_read(int id, unsigned int n_rx_byte,
		       unsigned char *p_rx_byte)
{
	struct rk_scr *scr;
	struct scr_reg_t *scr_reg;
	unsigned int inten1 = 0;

	scr = to_opened_rk_scr(id);
	if (!scr)
		return -1;

	if (!((n_rx_byte != 0) && (p_rx_byte))) {
		DAL_LOGV("rk_scr_read: invalid argument\n");
		return -1;
	}

	scr_reg = scr->hw->reg_base;

	/*
	 * must disable all SCR interrupts.
	 * It will protect the global data.
	 */
	scr_reg->INTEN1 = 0;

	scr->rx_buf = p_rx_byte;
	scr->rx_expected = n_rx_byte;
	scr->rx_cnt = 0;

	scr_reg->RXFIFOTH = (scr->rx_expected < MAX_RXTHR)
				? scr->rx_expected : MAX_RXTHR;
	inten1 = RXTHRESHOLD | RXFIFULL | RXPERR | C2CFULL;

	scr_reg->INTEN1 = inten1;

	return 0;
}

static int rk_scr_write(int id, unsigned int n_tx_byte,
			const unsigned char *p_tx_byte)
{
	struct rk_scr *scr;
	struct scr_reg_t *scr_reg;
	unsigned int inten1 = 0;
	unsigned timeout_count = 1500;
	unsigned long udelay = 0;

	timeout_count = 1500;
	udelay = msecs_to_jiffies(timeout_count) + jiffies;

	scr = to_opened_rk_scr(id);
	if (!scr)
		return -1;

	if (!((n_tx_byte != 0) && (p_tx_byte))) {
		DAL_LOGV("rk_scr_write: invalid argument\n");
		return -1;
	}

	scr_reg = scr->hw->reg_base;

	/*
	 * must disable all SCR interrupts.
	 * It will protect the global data.
	 */
	scr_reg->INTEN1 = 0;

	scr->tx_buf = p_tx_byte;
	scr->tx_expected = n_tx_byte;
	scr->tx_cnt = 0;

	scr_reg->FIFOCTRL = FC_TXFIFLUSH | FC_RXFIFLUSH;

	/* send data until FIFO full or send over. */
	while ((scr->tx_cnt < scr->tx_expected) &&
	       (time_before(jiffies, udelay))) {
		if (!(scr_reg->FIFOCTRL & FC_TXFIFULL))
			scr_reg->FIFODATA = scr->tx_buf[scr->tx_cnt++];
	}
	/* need enable tx interrupt to continue */
	if (scr->tx_cnt < scr->tx_expected) {
		pr_err("\n@rk_scr_write: FC_TXFIFULL@\n");
		inten1 |= TXFIEMPTY | TXPERR;
	}

	scr_reg->INTEN1 = inten1;

	return 0;
}

int rk_scr_transfer(int id,
		    unsigned int n_tx_byte, unsigned char *p_tx_byte,
		    unsigned int n_rx_byte, unsigned char *p_rx_byte)
{
	struct rk_scr *scr;
	struct scr_reg_t *scr_reg;
	unsigned int inten1;

	scr = to_opened_rk_scr(id);
	if (!scr)
		return -1;

	if (!((n_tx_byte != 0) &&
	      (p_tx_byte) &&
	      (n_rx_byte != 0) &&
	      (p_rx_byte))) {
		DAL_LOGV("rk_scr_transfer: invalid argument\n");
		return -1;
	}

	if (scr->in_process)
		return -1;

	scr->in_process = true;
	scr_reg = scr->hw->reg_base;

	/*
	 * must disable all SCR interrupts.
	 * It will protect the global data.
	 */
	scr_reg->INTEN1 = 0;
	rk_scr_clock_start(scr);

	scr->tx_buf = p_tx_byte;
	scr->tx_expected = n_tx_byte;
	scr->tx_cnt = 0;

	scr->rx_buf = p_rx_byte;
	scr->rx_expected = n_rx_byte;
	scr->rx_cnt = 0;

	scr_reg->FIFOCTRL = FC_TXFIFLUSH | FC_RXFIFLUSH;

	scr_reg->RXFIFOTH = (scr->rx_expected < MAX_RXTHR)
				? scr->rx_expected : MAX_RXTHR;
	scr_reg->TXFIFOTH = MAX_TXTHR;

	inten1 = RXTHRESHOLD | RXFIFULL | RXPERR | C2CFULL;

	/* send data until FIFO full or send over. */
	while ((scr->tx_cnt < scr->tx_expected) &&
	       !(scr_reg->FIFOCTRL & FC_TXFIFULL)) {
		scr_reg->FIFODATA = scr->tx_buf[scr->tx_cnt++];
	}

	/* need enable tx interrupt to continue */
	if (scr->tx_cnt < scr->tx_expected)
		inten1 |= TXFIEMPTY | TXPERR;

	scr_reg->INTEN1 = inten1;

	return 0;
}

static	enum hal_scr_id_e g_curr_sur_id = HAL_SCR_ID0;
void _scr_init(void)
{
	enum hal_scr_id_e id = g_curr_sur_id;

	rk_scr_open(id);
}

void _scr_close(void)
{
	enum hal_scr_id_e id = g_curr_sur_id;

	rk_scr_close(id);
}

bool _scr_set_voltage(enum hal_scr_voltage_e level)
{
	enum hal_scr_id_e id = g_curr_sur_id;
	struct rk_scr *scr = to_opened_rk_scr(id);

	if (scr) {
		rk_scr_set_scr_voltage(scr, level);
		return true;
	}

	return false;
}

void _scr_reset(unsigned char *rx_bytes)
{
	enum hal_scr_id_e id = g_curr_sur_id;
	struct rk_scr *scr = to_opened_rk_scr(id);

	if (scr)
		rk_scr_reset(scr, rx_bytes);
}

void _scr_set_etu_duration(unsigned int F, unsigned int D)
{
	enum hal_scr_id_e id = g_curr_sur_id;
	struct rk_scr *scr = to_opened_rk_scr(id);

	if (scr)
		rk_scr_set_etu_duration(scr, F, D);
}

void _scr_set_clock_stopmode(enum hal_scr_clock_stop_mode_e mode)
{
	enum hal_scr_id_e id = g_curr_sur_id;
	struct rk_scr *scr = to_opened_rk_scr(id);

	if (scr)
		rk_scr_set_clockstop_mode(scr, mode);
}

void _scr_set_work_waitingtime(unsigned char wi)
{
	enum hal_scr_id_e id = g_curr_sur_id;
	struct rk_scr *scr = to_opened_rk_scr(id);

	if (scr)
		rk_scr_set_work_waitingtime(scr, wi);
}

void _scr_clock_start(void)
{
	enum hal_scr_id_e id = g_curr_sur_id;
	struct rk_scr *scr = to_opened_rk_scr(id);

	if (scr)
		rk_scr_clock_start(scr);
}

void _scr_clock_stop(void)
{
	enum hal_scr_id_e id = g_curr_sur_id;
	struct rk_scr *scr = to_opened_rk_scr(id);

	if (scr)
		rk_scr_clock_stop(scr);
}

bool _scr_tx_byte(unsigned int n_tx_byte,
		  const unsigned char *p_tx_byte)
{
	enum hal_scr_id_e id = g_curr_sur_id;
	int ret = 0;

	ret = rk_scr_write(id, n_tx_byte, p_tx_byte);
	if (ret)
		return false;
	return true;
}

bool _scr_rx_byte(unsigned int n_rx_byte, unsigned char *p_rx_byte)
{
	enum hal_scr_id_e id = g_curr_sur_id;
	int ret = 0;

	ret = rk_scr_read(id, n_rx_byte, p_rx_byte);
	if (ret)
		return false;
	return true;
}

bool _scr_tx_byte_rx_byte(unsigned int n_tx_byte,
			  unsigned char *p_tx_byte,
			  unsigned int n_rx_byte,
			  unsigned char *p_rx_byte)
{
	enum hal_scr_id_e id = g_curr_sur_id;
	int ret;

	ret = rk_scr_transfer(id, n_tx_byte, p_tx_byte, n_rx_byte, p_rx_byte);
	if (ret)
		return false;

	return true;
}

unsigned int _scr_get_num_rx_bytes(void)
{
	enum hal_scr_id_e id = g_curr_sur_id;
	struct rk_scr *scr = to_opened_rk_scr(id);

	if (scr)
		return scr->rx_cnt;

	return 0;
}

unsigned int _scr_get_num_tx_bytes(void)
{
	enum hal_scr_id_e id = g_curr_sur_id;
	struct rk_scr *scr = to_opened_rk_scr(id);

	if (scr)
		return scr->tx_cnt;

	return 0;
}

void _scr_powerdown(void)
{
	enum hal_scr_id_e id = g_curr_sur_id;
	struct rk_scr *scr = to_opened_rk_scr(id);

	if (scr)
		rk_scr_powerdown(scr);
}

void _scr_irq_set_handler(hal_scr_irq_handler_t handler)
{
	enum hal_scr_id_e id = g_curr_sur_id;
	struct rk_scr *scr = to_rk_scr(id);

	if (scr)
		scr->user_handler = handler;
}

void _scr_irq_set_mask(struct hal_scr_irq_status_t mask)
{
	enum hal_scr_id_e id = g_curr_sur_id;
	struct rk_scr *scr = to_rk_scr(id);

	if (scr)
		scr->user_mask = mask;
}

struct hal_scr_irq_status_t _scr_irq_get_mask(void)
{
	enum hal_scr_id_e id = g_curr_sur_id;
	struct rk_scr *scr = to_rk_scr(id);
	struct hal_scr_irq_status_t user_mask = {0};

	if (scr)
		return scr->user_mask;

	return user_mask;
}

enum hal_scr_detect_status_e _scr_irq_get_detect_status(void)
{
	enum hal_scr_id_e id = g_curr_sur_id;
	struct rk_scr *scr = to_rk_scr(id);

	if (scr && (scr->hw->reg_base->SCPADS & SCPRESENT)) {
		DAL_LOGV("\n scr_check_card_insert: yes.\n");
		return SMC_DRV_INT_CARDIN;
	}

	DAL_LOGV("\n scr_check_card_insert: no.\n");
	return SMC_DRV_INT_CARDOUT;
}

unsigned char _scr_rx_done(void)
{
	enum hal_scr_id_e id = g_curr_sur_id;
	struct rk_scr *scr = to_rk_scr(id);

	if (scr->hw->reg_base->INTSTAT1 & RXDONE)
		return 1;
	else
		return 0;
}

void scr_set_etu_duration_struct(int f_and_d)
{
	switch (f_and_d) {
	case HAL_SCR_ETU_F_372_AND_D_1:
		_scr_set_etu_duration(372, 1);
		break;
	case HAL_SCR_ETU_F_512_AND_D_8:
		_scr_set_etu_duration(512, 8);
		break;
	case HAL_SCR_ETU_F_512_AND_D_4:
		_scr_set_etu_duration(512, 4);
		break;
	}
}

int scr_check_card_insert(void)
{
	int card_detect = -1;

	card_detect = _scr_irq_get_detect_status();
	if (card_detect)
		return SMC_DRV_INT_CARDIN;
	else
		return SMC_DRV_INT_CARDOUT;
}

static void scr_activate_card(void)
{
	_scr_init();
	_scr_set_voltage(HAL_SCR_VOLTAGE_CLASS_B);
}

static void scr_deactivate_card(void)
{
	_scr_close();
}

static void scr_isr_callback(enum hal_scr_irq_cause_e cause)
{
	complete(&rk_scr->is_done);
}

ssize_t scr_write(unsigned char *buf,
		  unsigned int write_cnt, unsigned int *to_read_cnt)
{
	unsigned time_out = SMC_DEFAULT_TIMEOUT;
	unsigned long udelay = 0;
	int ret;

	mutex_lock(&rk_scr->scr_mutex);
	if (scr_check_card_insert() == SMC_DRV_INT_CARDOUT) {
		mutex_unlock(&rk_scr->scr_mutex);
		return SMC_ERROR_CARD_NOT_INSERT;
	}

	udelay = msecs_to_jiffies(time_out) + jiffies;

	init_completion(&rk_scr->is_done);
	rk_scr->recv_data_count = 0;
	rk_scr->recv_data_offset = 0;
	_scr_clock_start();

	_scr_tx_byte(write_cnt, buf);
	if (*to_read_cnt != 0) {
		/* Set registers, ready to receive.*/
		_scr_rx_byte(*to_read_cnt, rk_scr->recv_buffer);

		ret = wait_for_completion_timeout(&rk_scr->is_done,
						  msecs_to_jiffies(time_out));
		rk_scr->recv_data_count = _scr_get_num_rx_bytes();
		if (ret == 0) {
			_scr_clock_stop();
			mutex_unlock(&rk_scr->scr_mutex);
			return TIMEOUT;
		}
	}
	_scr_clock_stop();
	mutex_unlock(&rk_scr->scr_mutex);
	return SUCCESSFUL;
}

ssize_t scr_read(unsigned char *buf, unsigned int to_read_cnt,
		 unsigned int *have_read_cnt)
{
	unsigned data_len = 0;
	unsigned data_remain = 0;
	unsigned data_available = 0;
	unsigned data_offset = 0;
	unsigned time_out_ms = SMC_DEFAULT_TIMEOUT;
	unsigned data_count = 0;
	unsigned char data_remain_flag = 0;
	unsigned long udelay = 0;

	if (!rk_scr->recv_buffer)
		return SMC_ERROR_RX_ERR;

	mutex_lock(&rk_scr->scr_mutex);
	if (scr_check_card_insert() == SMC_DRV_INT_CARDOUT) {
		mutex_unlock(&rk_scr->scr_mutex);
		return SMC_ERROR_CARD_NOT_INSERT;
	}

	udelay = msecs_to_jiffies(time_out_ms) + jiffies;
	data_remain = to_read_cnt;
	data_count = 0;
	_scr_clock_start();

	if (data_remain != 0xffffff)
		data_remain_flag = 1;

	while (time_before(jiffies, udelay)) {
		data_available = rk_scr->recv_data_count
				- rk_scr->recv_data_offset;
		if (data_available) {
			if (data_remain_flag)
				data_len = (data_available > data_remain)
					? (data_remain) : (data_available);
			else
				data_len = data_available;
			data_offset = rk_scr->recv_data_offset;
			memcpy(&buf[data_count],
			       &rk_scr->recv_buffer[data_offset],
			       data_len);
			data_count += data_len;
			rk_scr->recv_data_offset += data_len;
			if (data_remain_flag)
				data_remain -= data_len;
		}

		if (data_remain_flag && (data_remain == 0))
			break;
		msleep(50);
	}
	_scr_clock_stop();
	*have_read_cnt = data_count;
	mutex_unlock(&rk_scr->scr_mutex);

	return SUCCESSFUL;
}

int scr_open(void)
{
	mutex_lock(&rk_scr->scr_mutex);
	_scr_irq_set_handler(scr_isr_callback);
	if (!rk_scr->recv_buffer) {
		rk_scr->recv_buffer = kmalloc(SMC_RECEIVE_BUF_LEN, GFP_DMA);
		if (!rk_scr->recv_buffer)
			return NO_MEMORY;
	}
	memset(rk_scr->recv_buffer, 0, SMC_RECEIVE_BUF_LEN);
	init_completion(&rk_scr->is_done);
	rk_scr->recv_data_count = 0;
	rk_scr->recv_data_offset = 0;
	scr_activate_card();
	mutex_unlock(&rk_scr->scr_mutex);

	return SUCCESSFUL;
}

int scr_close(void)
{
	mutex_lock(&rk_scr->scr_mutex);
	scr_deactivate_card();
	kfree(rk_scr->recv_buffer);
	rk_scr->recv_buffer = NULL;
	mutex_unlock(&rk_scr->scr_mutex);

	return SUCCESSFUL;
}

int scr_reset(void)
{
	unsigned long timeout_ms = SMC_DEFAULT_TIMEOUT;
	int i = 0;
	int ret;

	DAL_LOGV("-----------------scr_reset------------------\n");
	mutex_lock(&rk_scr->scr_mutex);
	if (scr_check_card_insert() == SMC_DRV_INT_CARDOUT) {
		mutex_unlock(&rk_scr->scr_mutex);
		return SMC_ERROR_CARD_NOT_INSERT;
	}

	init_completion(&rk_scr->is_done);

	rk_scr->recv_data_count = 0;
	rk_scr->recv_data_offset = 0;
	memset(rk_scr->atr_buffer, 0, SMC_ATR_MAX_LENGTH);
	rk_scr->atr_length = 0;

	_scr_clock_start();
	_scr_reset(rk_scr->recv_buffer);

	ret = wait_for_completion_timeout(&rk_scr->is_done,
					  msecs_to_jiffies(timeout_ms));
	rk_scr->recv_data_count = _scr_get_num_rx_bytes();

	_scr_clock_stop();

	if ((rk_scr->recv_data_count <= SMC_ATR_MAX_LENGTH) &&
	    (rk_scr->recv_data_count > 0)) {
		memcpy(rk_scr->atr_buffer, rk_scr->recv_buffer,
		       rk_scr->recv_data_count);
		rk_scr->atr_length = rk_scr->recv_data_count;
	} else {
		DAL_LOGV("ATR error: rk_scr->recv_data_count = %d.\n",
			 rk_scr->recv_data_count);
		mutex_unlock(&rk_scr->scr_mutex);
		return SMC_ERROR_ATR_ERR;
	}

	DAL_LOGV("\n--------ATR start-----------\n");
	DAL_LOGV("rk_scr->atr_length = %d\n", rk_scr->atr_length);
	for (i = 0; i < rk_scr->recv_data_count; i++)
		DAL_LOGV("0x%2x\n", rk_scr->atr_buffer[i]);
	DAL_LOGV("\n--------ATR end-----------\n");
	mutex_unlock(&rk_scr->scr_mutex);

	return SUCCESSFUL;
}

int scr_get_atr_data(unsigned char *atr_buf, unsigned char *atr_len)
{
	if ((!atr_buf) || (!atr_len))
		return SMC_ERROR_BAD_PARAMETER;

	mutex_lock(&rk_scr->scr_mutex);
	if ((rk_scr->atr_length < SMC_ATR_MIN_LENGTH) ||
	    (rk_scr->atr_length > SMC_ATR_MAX_LENGTH)) {
		mutex_unlock(&rk_scr->scr_mutex);
		return SMC_ERROR_ATR_ERR;
	}

	memcpy(atr_buf, &rk_scr->atr_buffer[0], rk_scr->atr_length);
	*atr_len = rk_scr->atr_length;
	mutex_unlock(&rk_scr->scr_mutex);

	return SUCCESSFUL;
}

void scr_set_etu_duration(unsigned int F, unsigned int D)
{
	mutex_lock(&rk_scr->scr_mutex);
	_scr_set_etu_duration(F, D);
	mutex_unlock(&rk_scr->scr_mutex);
}

void scr_set_work_waitingtime(unsigned char wi)
{
	mutex_lock(&rk_scr->scr_mutex);
	_scr_set_work_waitingtime(wi);
	mutex_unlock(&rk_scr->scr_mutex);
}

static int scr_sysfs_value;

static ssize_t scr_sysfs_show(struct kobject *kobj,
			      struct kobj_attribute *attr,
			      char *buf)
{
	scr_open();
	scr_reset();
	scr_close();

	return sprintf(buf, "%d\n", scr_sysfs_value);
}

static ssize_t scr_sysfs_store(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       const char *buf, size_t count)
{
	int ret;

	ret = sscanf(buf, "%du", &scr_sysfs_value);
	if (ret != 1)
		return -EINVAL;

	return 0;
}

static struct kobj_attribute scr_sysfs_attribute =
		__ATTR(scr_sysfs, 0664, scr_sysfs_show, scr_sysfs_store);

struct attribute *rockchip_smartcard_attributes[] = {
	&scr_sysfs_attribute.attr,
	NULL
};

static const struct attribute_group rockchip_smartcard_group = {
	.attrs = rockchip_smartcard_attributes,
};

/* #define CONFIG_SMARTCARD_MUX_SEL_T0 */
/* #define CONFIG_SMARTCARD_MUX_SEL_T1 */
#define RK_SCR_CLK_NAME "g_pclk_sim_card"
static int rk_scr_probe(struct platform_device *pdev)
{
	struct rk_scr_device *rk_scr_dev = NULL;
	struct resource *res = NULL;
	struct device *dev = NULL;
	int ret = 0;

	dev = &pdev->dev;
	rk_scr_dev = devm_kzalloc(dev, sizeof(*rk_scr_dev), GFP_KERNEL);
	if (!rk_scr_dev) {
		dev_err(dev, "failed to allocate scr_device\n");
		return -ENOMEM;
	}
	rk_scr = rk_scr_dev;
	mutex_init(&rk_scr->scr_mutex);

	rk_scr_dev->irq = platform_get_irq(pdev, 0);
	if (rk_scr_dev->irq < 0) {
		dev_err(dev, "failed to get scr irq\n");
		return -ENOENT;
	}

	ret = devm_request_irq(dev, rk_scr_dev->irq, rk_scr_irqhandler,
			       0, "rockchip-scr",
			       (void *)&rk_scr->scr[g_curr_sur_id]);
	if (ret < 0) {
		dev_err(dev, "failed to attach scr irq\n");
		return ret;
	}

	rk_scr_dev->clk_scr = devm_clk_get(dev, RK_SCR_CLK_NAME);
	if (IS_ERR(rk_scr_dev->clk_scr)) {
		dev_err(dev, "failed to get scr clock\n");
		return PTR_ERR(rk_scr_dev->clk_scr);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rk_scr_dev->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(rk_scr_dev->regs))
		return PTR_ERR(rk_scr_dev->regs);

#ifdef CONFIG_SMARTCARD_MUX_SEL_T0
	writel_relaxed(((0x1 << 22) | (0x1 << 6)),
		       RK_GRF_VIRT + RK3288_GRF_SOC_CON2);
#endif

#ifdef CONFIG_SMARTCARD_MUX_SEL_T1
	pinctrl_select_state(dev->pins->p,
			     pinctrl_lookup_state(dev->pins->p, "sc_t1"));
	writel_relaxed(((0x1 << 22) | (0x0 << 6)),
		       (RK_GRF_VIRT + RK3288_GRF_SOC_CON2));
#endif

	dev_set_drvdata(dev, rk_scr_dev);

	ret = sysfs_create_group(&pdev->dev.kobj, &rockchip_smartcard_group);
	if (ret < 0)
		dev_err(&pdev->dev, "Create sysfs group failed (%d)\n", ret);
	DAL_LOGV("rk_scr_pdev->name = %s\n", pdev->name);
	DAL_LOGV("rk_scr_dev->irq = 0x%x\n", rk_scr_dev->irq);

	return ret;
}

#ifdef CONFIG_PM
static int rk_scr_suspend(struct device *dev)
{
	struct rk_scr_device *rk_scr_dev = dev_get_drvdata(dev);

	disable_irq(rk_scr_dev->irq);
	clk_disable(rk_scr_dev->clk_scr);

	return 0;
}

static int rk_scr_resume(struct device *dev)
{
	struct rk_scr_device *rk_scr_dev = dev_get_drvdata(dev);

	clk_enable(rk_scr_dev->clk_scr);
	enable_irq(rk_scr_dev->irq);

	return 0;
}
#else
#define rk_scr_suspend NULL
#define rk_scr_resume NULL
#endif

#ifdef CONFIG_OF
static const struct of_device_id rockchip_scr_dt_match[] = {
	{ .compatible = "rockchip-scr",},
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_scr_dt_match);
#endif /* CONFIG_OF */

static const struct dev_pm_ops scr_pm_ops = {
	.suspend	= rk_scr_suspend,
	.resume		= rk_scr_resume,
};

static struct platform_driver rk_scr_driver = {
	.driver		= {
		.name	= "rockchip-scr",
		.owner	= THIS_MODULE,
		.pm	= &scr_pm_ops,
		.of_match_table = of_match_ptr(rockchip_scr_dt_match),
	},
	.probe		= rk_scr_probe,
};

module_platform_driver(rk_scr_driver);

MODULE_DESCRIPTION("rockchip Smart Card controller driver");
MODULE_AUTHOR("<rockchip>");
MODULE_LICENSE("GPL");
