// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-3-Clause)
/*
 * Copyright (c) 2014-2025, Advanced Micro Devices, Inc.
 * Copyright (c) 2014, Synopsys, Inc.
 * All rights reserved
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/kmod.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/mutex.h>

#include "xgbe.h"
#include "xgbe-common.h"

#define XGBE_ABORT_COUNT	500
#define XGBE_DISABLE_COUNT	1000

#define XGBE_STD_SPEED		1

#define XGBE_INTR_RX_FULL	BIT(IC_RAW_INTR_STAT_RX_FULL_INDEX)
#define XGBE_INTR_TX_EMPTY	BIT(IC_RAW_INTR_STAT_TX_EMPTY_INDEX)
#define XGBE_INTR_TX_ABRT	BIT(IC_RAW_INTR_STAT_TX_ABRT_INDEX)
#define XGBE_INTR_STOP_DET	BIT(IC_RAW_INTR_STAT_STOP_DET_INDEX)
#define XGBE_DEFAULT_INT_MASK	(XGBE_INTR_RX_FULL  |	\
				 XGBE_INTR_TX_EMPTY |	\
				 XGBE_INTR_TX_ABRT  |	\
				 XGBE_INTR_STOP_DET)

#define XGBE_I2C_READ		BIT(8)
#define XGBE_I2C_STOP		BIT(9)

static int xgbe_i2c_abort(struct xgbe_prv_data *pdata)
{
	unsigned int wait = XGBE_ABORT_COUNT;

	/* Must be enabled to recognize the abort request */
	XI2C_IOWRITE_BITS(pdata, IC_ENABLE, EN, 1);

	/* Issue the abort */
	XI2C_IOWRITE_BITS(pdata, IC_ENABLE, ABORT, 1);

	while (wait--) {
		if (!XI2C_IOREAD_BITS(pdata, IC_ENABLE, ABORT))
			return 0;

		usleep_range(500, 600);
	}

	return -EBUSY;
}

static int xgbe_i2c_set_enable(struct xgbe_prv_data *pdata, bool enable)
{
	unsigned int wait = XGBE_DISABLE_COUNT;
	unsigned int mode = enable ? 1 : 0;

	while (wait--) {
		XI2C_IOWRITE_BITS(pdata, IC_ENABLE, EN, mode);
		if (XI2C_IOREAD_BITS(pdata, IC_ENABLE_STATUS, EN) == mode)
			return 0;

		usleep_range(100, 110);
	}

	return -EBUSY;
}

static int xgbe_i2c_disable(struct xgbe_prv_data *pdata)
{
	int ret;

	ret = xgbe_i2c_set_enable(pdata, false);
	if (ret) {
		/* Disable failed, try an abort */
		ret = xgbe_i2c_abort(pdata);
		if (ret)
			return ret;

		/* Abort succeeded, try to disable again */
		ret = xgbe_i2c_set_enable(pdata, false);
	}

	return ret;
}

static int xgbe_i2c_enable(struct xgbe_prv_data *pdata)
{
	return xgbe_i2c_set_enable(pdata, true);
}

static void xgbe_i2c_clear_all_interrupts(struct xgbe_prv_data *pdata)
{
	XI2C_IOREAD(pdata, IC_CLR_INTR);
}

static void xgbe_i2c_disable_interrupts(struct xgbe_prv_data *pdata)
{
	XI2C_IOWRITE(pdata, IC_INTR_MASK, 0);
}

static void xgbe_i2c_enable_interrupts(struct xgbe_prv_data *pdata)
{
	XI2C_IOWRITE(pdata, IC_INTR_MASK, XGBE_DEFAULT_INT_MASK);
}

static void xgbe_i2c_write(struct xgbe_prv_data *pdata)
{
	struct xgbe_i2c_op_state *state = &pdata->i2c.op_state;
	unsigned int tx_slots;
	unsigned int cmd;

	/* Configured to never receive Rx overflows, so fill up Tx fifo */
	tx_slots = pdata->i2c.tx_fifo_size - XI2C_IOREAD(pdata, IC_TXFLR);
	while (tx_slots && state->tx_len) {
		if (state->op->cmd == XGBE_I2C_CMD_READ)
			cmd = XGBE_I2C_READ;
		else
			cmd = *state->tx_buf++;

		if (state->tx_len == 1)
			XI2C_SET_BITS(cmd, IC_DATA_CMD, STOP, 1);

		XI2C_IOWRITE(pdata, IC_DATA_CMD, cmd);

		tx_slots--;
		state->tx_len--;
	}

	/* No more Tx operations, so ignore TX_EMPTY and return */
	if (!state->tx_len)
		XI2C_IOWRITE_BITS(pdata, IC_INTR_MASK, TX_EMPTY, 0);
}

static void xgbe_i2c_read(struct xgbe_prv_data *pdata)
{
	struct xgbe_i2c_op_state *state = &pdata->i2c.op_state;
	unsigned int rx_slots;

	/* Anything to be read? */
	if (state->op->cmd != XGBE_I2C_CMD_READ)
		return;

	rx_slots = XI2C_IOREAD(pdata, IC_RXFLR);
	while (rx_slots && state->rx_len) {
		*state->rx_buf++ = XI2C_IOREAD(pdata, IC_DATA_CMD);
		state->rx_len--;
		rx_slots--;
	}
}

static void xgbe_i2c_clear_isr_interrupts(struct xgbe_prv_data *pdata,
					  unsigned int isr)
{
	struct xgbe_i2c_op_state *state = &pdata->i2c.op_state;

	if (isr & XGBE_INTR_TX_ABRT) {
		state->tx_abort_source = XI2C_IOREAD(pdata, IC_TX_ABRT_SOURCE);
		XI2C_IOREAD(pdata, IC_CLR_TX_ABRT);
	}

	if (isr & XGBE_INTR_STOP_DET)
		XI2C_IOREAD(pdata, IC_CLR_STOP_DET);
}

static void xgbe_i2c_isr_bh_work(struct work_struct *work)
{
	struct xgbe_prv_data *pdata = from_work(pdata, work, i2c_bh_work);
	struct xgbe_i2c_op_state *state = &pdata->i2c.op_state;
	unsigned int isr;

	isr = XI2C_IOREAD(pdata, IC_RAW_INTR_STAT);
	if (!isr)
		goto reissue_check;

	netif_dbg(pdata, intr, pdata->netdev,
		  "I2C interrupt received: status=%#010x\n", isr);

	xgbe_i2c_clear_isr_interrupts(pdata, isr);

	if (isr & XGBE_INTR_TX_ABRT) {
		netif_dbg(pdata, link, pdata->netdev,
			  "I2C TX_ABRT received (%#010x) for target %#04x\n",
			  state->tx_abort_source, state->op->target);

		xgbe_i2c_disable_interrupts(pdata);

		state->ret = -EIO;
		goto out;
	}

	/* Check for data in the Rx fifo */
	xgbe_i2c_read(pdata);

	/* Fill up the Tx fifo next */
	xgbe_i2c_write(pdata);

out:
	/* Complete on an error or STOP condition */
	if (state->ret || XI2C_GET_BITS(isr, IC_RAW_INTR_STAT, STOP_DET))
		complete(&pdata->i2c_complete);

reissue_check:
	/* Reissue interrupt if status is not clear */
	if (pdata->vdata->irq_reissue_support)
		XP_IOWRITE(pdata, XP_INT_REISSUE_EN, 1 << 2);
}

static irqreturn_t xgbe_i2c_isr(int irq, void *data)
{
	struct xgbe_prv_data *pdata = (struct xgbe_prv_data *)data;

	if (pdata->isr_as_bh_work)
		queue_work(system_bh_wq, &pdata->i2c_bh_work);
	else
		xgbe_i2c_isr_bh_work(&pdata->i2c_bh_work);

	return IRQ_HANDLED;
}

static void xgbe_i2c_set_mode(struct xgbe_prv_data *pdata)
{
	unsigned int reg;

	reg = XI2C_IOREAD(pdata, IC_CON);
	XI2C_SET_BITS(reg, IC_CON, MASTER_MODE, 1);
	XI2C_SET_BITS(reg, IC_CON, SLAVE_DISABLE, 1);
	XI2C_SET_BITS(reg, IC_CON, RESTART_EN, 1);
	XI2C_SET_BITS(reg, IC_CON, SPEED, XGBE_STD_SPEED);
	XI2C_SET_BITS(reg, IC_CON, RX_FIFO_FULL_HOLD, 1);
	XI2C_IOWRITE(pdata, IC_CON, reg);
}

static void xgbe_i2c_get_features(struct xgbe_prv_data *pdata)
{
	struct xgbe_i2c *i2c = &pdata->i2c;
	unsigned int reg;

	reg = XI2C_IOREAD(pdata, IC_COMP_PARAM_1);
	i2c->max_speed_mode = XI2C_GET_BITS(reg, IC_COMP_PARAM_1,
					    MAX_SPEED_MODE);
	i2c->rx_fifo_size = XI2C_GET_BITS(reg, IC_COMP_PARAM_1,
					  RX_BUFFER_DEPTH);
	i2c->tx_fifo_size = XI2C_GET_BITS(reg, IC_COMP_PARAM_1,
					  TX_BUFFER_DEPTH);

	if (netif_msg_probe(pdata))
		dev_dbg(pdata->dev, "I2C features: %s=%u, %s=%u, %s=%u\n",
			"MAX_SPEED_MODE", i2c->max_speed_mode,
			"RX_BUFFER_DEPTH", i2c->rx_fifo_size,
			"TX_BUFFER_DEPTH", i2c->tx_fifo_size);
}

static void xgbe_i2c_set_target(struct xgbe_prv_data *pdata, unsigned int addr)
{
	XI2C_IOWRITE(pdata, IC_TAR, addr);
}

static irqreturn_t xgbe_i2c_combined_isr(struct xgbe_prv_data *pdata)
{
	xgbe_i2c_isr_bh_work(&pdata->i2c_bh_work);

	return IRQ_HANDLED;
}

static int xgbe_i2c_xfer(struct xgbe_prv_data *pdata, struct xgbe_i2c_op *op)
{
	struct xgbe_i2c_op_state *state = &pdata->i2c.op_state;
	int ret;

	mutex_lock(&pdata->i2c_mutex);

	reinit_completion(&pdata->i2c_complete);

	ret = xgbe_i2c_disable(pdata);
	if (ret) {
		netdev_err(pdata->netdev, "failed to disable i2c master\n");
		goto unlock;
	}

	xgbe_i2c_set_target(pdata, op->target);

	memset(state, 0, sizeof(*state));
	state->op = op;
	state->tx_len = op->len;
	state->tx_buf = op->buf;
	state->rx_len = op->len;
	state->rx_buf = op->buf;

	xgbe_i2c_clear_all_interrupts(pdata);
	ret = xgbe_i2c_enable(pdata);
	if (ret) {
		netdev_err(pdata->netdev, "failed to enable i2c master\n");
		goto unlock;
	}

	/* Enabling the interrupts will cause the TX FIFO empty interrupt to
	 * fire and begin to process the command via the ISR.
	 */
	xgbe_i2c_enable_interrupts(pdata);

	if (!wait_for_completion_timeout(&pdata->i2c_complete, HZ)) {
		netdev_err(pdata->netdev, "i2c operation timed out\n");
		ret = -ETIMEDOUT;
		goto disable;
	}

	ret = state->ret;
	if (ret) {
		if (state->tx_abort_source & IC_TX_ABRT_7B_ADDR_NOACK)
			ret = -ENOTCONN;
		else if (state->tx_abort_source & IC_TX_ABRT_ARB_LOST)
			ret = -EAGAIN;
	}

disable:
	xgbe_i2c_disable_interrupts(pdata);
	xgbe_i2c_disable(pdata);

unlock:
	mutex_unlock(&pdata->i2c_mutex);

	return ret;
}

static void xgbe_i2c_stop(struct xgbe_prv_data *pdata)
{
	if (!pdata->i2c.started)
		return;

	netif_dbg(pdata, link, pdata->netdev, "stopping I2C\n");

	pdata->i2c.started = 0;

	xgbe_i2c_disable_interrupts(pdata);
	xgbe_i2c_disable(pdata);
	xgbe_i2c_clear_all_interrupts(pdata);

	if (pdata->dev_irq != pdata->i2c_irq) {
		devm_free_irq(pdata->dev, pdata->i2c_irq, pdata);
		cancel_work_sync(&pdata->i2c_bh_work);
	}
}

static int xgbe_i2c_start(struct xgbe_prv_data *pdata)
{
	int ret;

	if (pdata->i2c.started)
		return 0;

	netif_dbg(pdata, link, pdata->netdev, "starting I2C\n");

	/* If we have a separate I2C irq, enable it */
	if (pdata->dev_irq != pdata->i2c_irq) {
		INIT_WORK(&pdata->i2c_bh_work, xgbe_i2c_isr_bh_work);

		ret = devm_request_irq(pdata->dev, pdata->i2c_irq,
				       xgbe_i2c_isr, 0, pdata->i2c_name,
				       pdata);
		if (ret) {
			netdev_err(pdata->netdev, "i2c irq request failed\n");
			return ret;
		}
	}

	pdata->i2c.started = 1;

	return 0;
}

static int xgbe_i2c_init(struct xgbe_prv_data *pdata)
{
	int ret;

	xgbe_i2c_disable_interrupts(pdata);

	ret = xgbe_i2c_disable(pdata);
	if (ret) {
		dev_err(pdata->dev, "failed to disable i2c master\n");
		return ret;
	}

	xgbe_i2c_get_features(pdata);

	xgbe_i2c_set_mode(pdata);

	xgbe_i2c_clear_all_interrupts(pdata);

	return 0;
}

void xgbe_init_function_ptrs_i2c(struct xgbe_i2c_if *i2c_if)
{
	i2c_if->i2c_init		= xgbe_i2c_init;

	i2c_if->i2c_start		= xgbe_i2c_start;
	i2c_if->i2c_stop		= xgbe_i2c_stop;

	i2c_if->i2c_xfer		= xgbe_i2c_xfer;

	i2c_if->i2c_isr			= xgbe_i2c_combined_isr;
}
