// SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause

/* Interrupt related logic for Mellanox Gigabit Ethernet driver
 *
 * Copyright (C) 2020-2021 NVIDIA CORPORATION & AFFILIATES
 */

#include <linux/interrupt.h>

#include "mlxbf_gige.h"
#include "mlxbf_gige_regs.h"

static irqreturn_t mlxbf_gige_error_intr(int irq, void *dev_id)
{
	struct mlxbf_gige *priv;
	u64 int_status;

	priv = dev_id;

	priv->error_intr_count++;

	int_status = readq(priv->base + MLXBF_GIGE_INT_STATUS);

	if (int_status & MLXBF_GIGE_INT_STATUS_HW_ACCESS_ERROR)
		priv->stats.hw_access_errors++;

	if (int_status & MLXBF_GIGE_INT_STATUS_TX_CHECKSUM_INPUTS) {
		priv->stats.tx_invalid_checksums++;
		/* This error condition is latched into MLXBF_GIGE_INT_STATUS
		 * when the GigE silicon operates on the offending
		 * TX WQE. The write to MLXBF_GIGE_INT_STATUS at the bottom
		 * of this routine clears this error condition.
		 */
	}

	if (int_status & MLXBF_GIGE_INT_STATUS_TX_SMALL_FRAME_SIZE) {
		priv->stats.tx_small_frames++;
		/* This condition happens when the networking stack invokes
		 * this driver's "start_xmit()" method with a packet whose
		 * size < 60 bytes.  The GigE silicon will automatically pad
		 * this small frame up to a minimum-sized frame before it is
		 * sent. The "tx_small_frame" condition is latched into the
		 * MLXBF_GIGE_INT_STATUS register when the GigE silicon
		 * operates on the offending TX WQE. The write to
		 * MLXBF_GIGE_INT_STATUS at the bottom of this routine
		 * clears this condition.
		 */
	}

	if (int_status & MLXBF_GIGE_INT_STATUS_TX_PI_CI_EXCEED_WQ_SIZE)
		priv->stats.tx_index_errors++;

	if (int_status & MLXBF_GIGE_INT_STATUS_SW_CONFIG_ERROR)
		priv->stats.sw_config_errors++;

	if (int_status & MLXBF_GIGE_INT_STATUS_SW_ACCESS_ERROR)
		priv->stats.sw_access_errors++;

	/* Clear all error interrupts by writing '1' back to
	 * all the asserted bits in INT_STATUS.  Do not write
	 * '1' back to 'receive packet' bit, since that is
	 * managed separately.
	 */

	int_status &= ~MLXBF_GIGE_INT_STATUS_RX_RECEIVE_PACKET;

	writeq(int_status, priv->base + MLXBF_GIGE_INT_STATUS);

	return IRQ_HANDLED;
}

static irqreturn_t mlxbf_gige_rx_intr(int irq, void *dev_id)
{
	struct mlxbf_gige *priv;

	priv = dev_id;

	priv->rx_intr_count++;

	/* NOTE: GigE silicon automatically disables "packet rx" interrupt by
	 *       setting MLXBF_GIGE_INT_MASK bit0 upon triggering the interrupt
	 *       to the ARM cores.  Software needs to re-enable "packet rx"
	 *       interrupts by clearing MLXBF_GIGE_INT_MASK bit0.
	 */

	napi_schedule(&priv->napi);

	return IRQ_HANDLED;
}

static irqreturn_t mlxbf_gige_llu_plu_intr(int irq, void *dev_id)
{
	struct mlxbf_gige *priv;

	priv = dev_id;
	priv->llu_plu_intr_count++;

	return IRQ_HANDLED;
}

int mlxbf_gige_request_irqs(struct mlxbf_gige *priv)
{
	int err;

	err = request_irq(priv->error_irq, mlxbf_gige_error_intr, 0,
			  "mlxbf_gige_error", priv);
	if (err) {
		dev_err(priv->dev, "Request error_irq failure\n");
		return err;
	}

	err = request_irq(priv->rx_irq, mlxbf_gige_rx_intr, 0,
			  "mlxbf_gige_rx", priv);
	if (err) {
		dev_err(priv->dev, "Request rx_irq failure\n");
		goto free_error_irq;
	}

	err = request_irq(priv->llu_plu_irq, mlxbf_gige_llu_plu_intr, 0,
			  "mlxbf_gige_llu_plu", priv);
	if (err) {
		dev_err(priv->dev, "Request llu_plu_irq failure\n");
		goto free_rx_irq;
	}

	return 0;

free_rx_irq:
	free_irq(priv->rx_irq, priv);

free_error_irq:
	free_irq(priv->error_irq, priv);

	return err;
}

void mlxbf_gige_free_irqs(struct mlxbf_gige *priv)
{
	free_irq(priv->error_irq, priv);
	free_irq(priv->rx_irq, priv);
	free_irq(priv->llu_plu_irq, priv);
}
