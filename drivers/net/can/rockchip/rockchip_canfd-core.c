// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2023, 2024 Pengutronix,
//               Marc Kleine-Budde <kernel@pengutronix.de>
//
// Based on:
//
// Rockchip CANFD driver
//
// Copyright (c) 2020 Rockchip Electronics Co. Ltd.
//

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/string.h>

#include "rockchip_canfd.h"

static const struct rkcanfd_devtype_data rkcanfd_devtype_data_rk3568v2 = {
	.model = RKCANFD_MODEL_RK3568V2,
	.quirks = RKCANFD_QUIRK_RK3568_ERRATUM_1 | RKCANFD_QUIRK_RK3568_ERRATUM_2 |
		RKCANFD_QUIRK_RK3568_ERRATUM_3 | RKCANFD_QUIRK_RK3568_ERRATUM_4 |
		RKCANFD_QUIRK_RK3568_ERRATUM_5 | RKCANFD_QUIRK_RK3568_ERRATUM_6 |
		RKCANFD_QUIRK_RK3568_ERRATUM_7 | RKCANFD_QUIRK_RK3568_ERRATUM_8 |
		RKCANFD_QUIRK_RK3568_ERRATUM_9 | RKCANFD_QUIRK_RK3568_ERRATUM_10 |
		RKCANFD_QUIRK_RK3568_ERRATUM_11 | RKCANFD_QUIRK_RK3568_ERRATUM_12 |
		RKCANFD_QUIRK_CANFD_BROKEN,
};

/* The rk3568 CAN-FD errata sheet as of Tue 07 Nov 2023 11:25:31 +08:00
 * states that only the rk3568v2 is affected by erratum 5, but tests
 * with the rk3568v2 and rk3568v3 show that the RX_FIFO_CNT is
 * sometimes too high. In contrast to the errata sheet mark rk3568v3
 * as effected by erratum 5, too.
 */
static const struct rkcanfd_devtype_data rkcanfd_devtype_data_rk3568v3 = {
	.model = RKCANFD_MODEL_RK3568V3,
	.quirks = RKCANFD_QUIRK_RK3568_ERRATUM_1 | RKCANFD_QUIRK_RK3568_ERRATUM_2 |
		RKCANFD_QUIRK_RK3568_ERRATUM_5 | RKCANFD_QUIRK_RK3568_ERRATUM_7 |
		RKCANFD_QUIRK_RK3568_ERRATUM_8 | RKCANFD_QUIRK_RK3568_ERRATUM_10 |
		RKCANFD_QUIRK_RK3568_ERRATUM_11 | RKCANFD_QUIRK_RK3568_ERRATUM_12 |
		RKCANFD_QUIRK_CANFD_BROKEN,
};

static const char *__rkcanfd_get_model_str(enum rkcanfd_model model)
{
	switch (model) {
	case RKCANFD_MODEL_RK3568V2:
		return "rk3568v2";
	case RKCANFD_MODEL_RK3568V3:
		return "rk3568v3";
	}

	return "<unknown>";
}

static inline const char *
rkcanfd_get_model_str(const struct rkcanfd_priv *priv)
{
	return __rkcanfd_get_model_str(priv->devtype_data.model);
}

/* Note:
 *
 * The formula to calculate the CAN System Clock is:
 *
 * Tsclk = 2 x Tclk x (brp + 1)
 *
 * Double the data sheet's brp_min, brp_max and brp_inc values (both
 * for the arbitration and data bit timing) to take the "2 x" into
 * account.
 */
static const struct can_bittiming_const rkcanfd_bittiming_const = {
	.name = DEVICE_NAME,
	.tseg1_min = 1,
	.tseg1_max = 256,
	.tseg2_min = 1,
	.tseg2_max = 128,
	.sjw_max = 128,
	.brp_min = 2,	/* value from data sheet x2 */
	.brp_max = 512,	/* value from data sheet x2 */
	.brp_inc = 2,	/* value from data sheet x2 */
};

static const struct can_bittiming_const rkcanfd_data_bittiming_const = {
	.name = DEVICE_NAME,
	.tseg1_min = 1,
	.tseg1_max = 32,
	.tseg2_min = 1,
	.tseg2_max = 16,
	.sjw_max = 16,
	.brp_min = 2,	/* value from data sheet x2 */
	.brp_max = 512,	/* value from data sheet x2 */
	.brp_inc = 2,	/* value from data sheet x2 */
};

static void rkcanfd_chip_set_reset_mode(const struct rkcanfd_priv *priv)
{
	reset_control_assert(priv->reset);
	udelay(2);
	reset_control_deassert(priv->reset);

	rkcanfd_write(priv, RKCANFD_REG_MODE, 0x0);
}

static void rkcanfd_chip_set_work_mode(const struct rkcanfd_priv *priv)
{
	rkcanfd_write(priv, RKCANFD_REG_MODE, priv->reg_mode_default);
}

static int rkcanfd_set_bittiming(struct rkcanfd_priv *priv)
{
	const struct can_bittiming *dbt = &priv->can.data_bittiming;
	const struct can_bittiming *bt = &priv->can.bittiming;
	u32 reg_nbt, reg_dbt, reg_tdc;
	u32 tdco;

	reg_nbt = FIELD_PREP(RKCANFD_REG_FD_NOMINAL_BITTIMING_SJW,
			     bt->sjw - 1) |
		FIELD_PREP(RKCANFD_REG_FD_NOMINAL_BITTIMING_BRP,
			   (bt->brp / 2) - 1) |
		FIELD_PREP(RKCANFD_REG_FD_NOMINAL_BITTIMING_TSEG2,
			   bt->phase_seg2 - 1) |
		FIELD_PREP(RKCANFD_REG_FD_NOMINAL_BITTIMING_TSEG1,
			   bt->prop_seg + bt->phase_seg1 - 1);

	rkcanfd_write(priv, RKCANFD_REG_FD_NOMINAL_BITTIMING, reg_nbt);

	if (!(priv->can.ctrlmode & CAN_CTRLMODE_FD))
		return 0;

	reg_dbt = FIELD_PREP(RKCANFD_REG_FD_DATA_BITTIMING_SJW,
			     dbt->sjw - 1) |
		FIELD_PREP(RKCANFD_REG_FD_DATA_BITTIMING_BRP,
			   (dbt->brp / 2) - 1) |
		FIELD_PREP(RKCANFD_REG_FD_DATA_BITTIMING_TSEG2,
			   dbt->phase_seg2 - 1) |
		FIELD_PREP(RKCANFD_REG_FD_DATA_BITTIMING_TSEG1,
			   dbt->prop_seg + dbt->phase_seg1 - 1);

	rkcanfd_write(priv, RKCANFD_REG_FD_DATA_BITTIMING, reg_dbt);

	tdco = (priv->can.clock.freq / dbt->bitrate) * 2 / 3;
	tdco = min(tdco, FIELD_MAX(RKCANFD_REG_TRANSMIT_DELAY_COMPENSATION_TDC_OFFSET));

	reg_tdc = FIELD_PREP(RKCANFD_REG_TRANSMIT_DELAY_COMPENSATION_TDC_OFFSET, tdco) |
		RKCANFD_REG_TRANSMIT_DELAY_COMPENSATION_TDC_ENABLE;
	rkcanfd_write(priv, RKCANFD_REG_TRANSMIT_DELAY_COMPENSATION,
		      reg_tdc);

	return 0;
}

static void rkcanfd_get_berr_counter_corrected(struct rkcanfd_priv *priv,
					       struct can_berr_counter *bec)
{
	struct can_berr_counter bec_raw;
	u32 reg_state;

	bec->rxerr = rkcanfd_read(priv, RKCANFD_REG_RXERRORCNT);
	bec->txerr = rkcanfd_read(priv, RKCANFD_REG_TXERRORCNT);
	bec_raw = *bec;

	/* Tests show that sometimes both CAN bus error counters read
	 * 0x0, even if the controller is in warning mode
	 * (RKCANFD_REG_STATE_ERROR_WARNING_STATE in RKCANFD_REG_STATE
	 * set).
	 *
	 * In case both error counters read 0x0, use the struct
	 * priv->bec, otherwise save the read value to priv->bec.
	 *
	 * rkcanfd_handle_rx_int_one() handles the decrementing of
	 * priv->bec.rxerr for successfully RX'ed CAN frames.
	 *
	 * Luckily the controller doesn't decrement the RX CAN bus
	 * error counter in hardware for self received TX'ed CAN
	 * frames (RKCANFD_REG_MODE_RXSTX_MODE), so RXSTX doesn't
	 * interfere with proper RX CAN bus error counters.
	 *
	 * rkcanfd_handle_tx_done_one() handles the decrementing of
	 * priv->bec.txerr for successfully TX'ed CAN frames.
	 */
	if (!bec->rxerr && !bec->txerr)
		*bec = priv->bec;
	else
		priv->bec = *bec;

	reg_state = rkcanfd_read(priv, RKCANFD_REG_STATE);
	netdev_vdbg(priv->ndev,
		    "%s: Raw/Cor: txerr=%3u/%3u rxerr=%3u/%3u Bus Off=%u Warning=%u\n",
		    __func__,
		    bec_raw.txerr, bec->txerr, bec_raw.rxerr, bec->rxerr,
		    !!(reg_state & RKCANFD_REG_STATE_BUS_OFF_STATE),
		    !!(reg_state & RKCANFD_REG_STATE_ERROR_WARNING_STATE));
}

static int rkcanfd_get_berr_counter(const struct net_device *ndev,
				    struct can_berr_counter *bec)
{
	struct rkcanfd_priv *priv = netdev_priv(ndev);
	int err;

	err = pm_runtime_resume_and_get(ndev->dev.parent);
	if (err)
		return err;

	rkcanfd_get_berr_counter_corrected(priv, bec);

	pm_runtime_put(ndev->dev.parent);

	return 0;
}

static void rkcanfd_chip_interrupts_enable(const struct rkcanfd_priv *priv)
{
	rkcanfd_write(priv, RKCANFD_REG_INT_MASK, priv->reg_int_mask_default);

	netdev_dbg(priv->ndev, "%s: reg_int_mask=0x%08x\n", __func__,
		   rkcanfd_read(priv, RKCANFD_REG_INT_MASK));
}

static void rkcanfd_chip_interrupts_disable(const struct rkcanfd_priv *priv)
{
	rkcanfd_write(priv, RKCANFD_REG_INT_MASK, RKCANFD_REG_INT_ALL);
}

static void rkcanfd_chip_fifo_setup(struct rkcanfd_priv *priv)
{
	u32 reg;

	/* RX FIFO */
	reg = rkcanfd_read(priv, RKCANFD_REG_RX_FIFO_CTRL);
	reg |= RKCANFD_REG_RX_FIFO_CTRL_RX_FIFO_ENABLE;
	rkcanfd_write(priv, RKCANFD_REG_RX_FIFO_CTRL, reg);

	WRITE_ONCE(priv->tx_head, 0);
	WRITE_ONCE(priv->tx_tail, 0);
	netdev_reset_queue(priv->ndev);
}

static void rkcanfd_chip_start(struct rkcanfd_priv *priv)
{
	u32 reg;

	rkcanfd_chip_set_reset_mode(priv);

	/* Receiving Filter: accept all */
	rkcanfd_write(priv, RKCANFD_REG_IDCODE, 0x0);
	rkcanfd_write(priv, RKCANFD_REG_IDMASK, RKCANFD_REG_IDCODE_EXTENDED_FRAME_ID);

	/* enable:
	 * - CAN_FD: enable CAN-FD
	 * - AUTO_RETX_MODE: auto retransmission on TX error
	 * - COVER_MODE: RX-FIFO overwrite mode, do not send OVERLOAD frames
	 * - RXSTX_MODE: Receive Self Transmit data mode
	 * - WORK_MODE: transition from reset to working mode
	 */
	reg = rkcanfd_read(priv, RKCANFD_REG_MODE);
	priv->reg_mode_default = reg |
		RKCANFD_REG_MODE_CAN_FD_MODE_ENABLE |
		RKCANFD_REG_MODE_AUTO_RETX_MODE |
		RKCANFD_REG_MODE_COVER_MODE |
		RKCANFD_REG_MODE_RXSTX_MODE |
		RKCANFD_REG_MODE_WORK_MODE;

	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK)
		priv->reg_mode_default |= RKCANFD_REG_MODE_LBACK_MODE |
			RKCANFD_REG_MODE_SILENT_MODE |
			RKCANFD_REG_MODE_SELF_TEST;

	/* mask, i.e. ignore:
	 * - TIMESTAMP_COUNTER_OVERFLOW_INT - timestamp counter overflow interrupt
	 * - TX_ARBIT_FAIL_INT - TX arbitration fail interrupt
	 * - OVERLOAD_INT - CAN bus overload interrupt
	 * - TX_FINISH_INT - Transmit finish interrupt
	 */
	priv->reg_int_mask_default =
		RKCANFD_REG_INT_TIMESTAMP_COUNTER_OVERFLOW_INT |
		RKCANFD_REG_INT_TX_ARBIT_FAIL_INT |
		RKCANFD_REG_INT_OVERLOAD_INT |
		RKCANFD_REG_INT_TX_FINISH_INT;

	/* Do not mask the bus error interrupt if the bus error
	 * reporting is requested.
	 */
	if (!(priv->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING))
		priv->reg_int_mask_default |= RKCANFD_REG_INT_ERROR_INT;

	memset(&priv->bec, 0x0, sizeof(priv->bec));

	rkcanfd_chip_fifo_setup(priv);
	rkcanfd_timestamp_init(priv);
	rkcanfd_timestamp_start(priv);

	rkcanfd_set_bittiming(priv);

	rkcanfd_chip_interrupts_disable(priv);
	rkcanfd_chip_set_work_mode(priv);

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	netdev_dbg(priv->ndev, "%s: reg_mode=0x%08x\n", __func__,
		   rkcanfd_read(priv, RKCANFD_REG_MODE));
}

static void __rkcanfd_chip_stop(struct rkcanfd_priv *priv, const enum can_state state)
{
	priv->can.state = state;

	rkcanfd_chip_set_reset_mode(priv);
	rkcanfd_chip_interrupts_disable(priv);
}

static void rkcanfd_chip_stop(struct rkcanfd_priv *priv, const enum can_state state)
{
	priv->can.state = state;

	rkcanfd_timestamp_stop(priv);
	__rkcanfd_chip_stop(priv, state);
}

static void rkcanfd_chip_stop_sync(struct rkcanfd_priv *priv, const enum can_state state)
{
	priv->can.state = state;

	rkcanfd_timestamp_stop_sync(priv);
	__rkcanfd_chip_stop(priv, state);
}

static int rkcanfd_set_mode(struct net_device *ndev,
			    enum can_mode mode)
{
	struct rkcanfd_priv *priv = netdev_priv(ndev);

	switch (mode) {
	case CAN_MODE_START:
		rkcanfd_chip_start(priv);
		rkcanfd_chip_interrupts_enable(priv);
		netif_wake_queue(ndev);
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static struct sk_buff *
rkcanfd_alloc_can_err_skb(struct rkcanfd_priv *priv,
			  struct can_frame **cf, u32 *timestamp)
{
	struct sk_buff *skb;

	*timestamp = rkcanfd_get_timestamp(priv);

	skb = alloc_can_err_skb(priv->ndev, cf);
	if (skb)
		rkcanfd_skb_set_timestamp(priv, skb, *timestamp);

	return skb;
}

static const char *rkcanfd_get_error_type_str(unsigned int type)
{
	switch (type) {
	case RKCANFD_REG_ERROR_CODE_TYPE_BIT:
		return "Bit";
	case RKCANFD_REG_ERROR_CODE_TYPE_STUFF:
		return "Stuff";
	case RKCANFD_REG_ERROR_CODE_TYPE_FORM:
		return "Form";
	case RKCANFD_REG_ERROR_CODE_TYPE_ACK:
		return "ACK";
	case RKCANFD_REG_ERROR_CODE_TYPE_CRC:
		return "CRC";
	}

	return "<unknown>";
}

#define RKCAN_ERROR_CODE(reg_ec, code) \
	((reg_ec) & RKCANFD_REG_ERROR_CODE_##code ? __stringify(code) " " : "")

static void
rkcanfd_handle_error_int_reg_ec(struct rkcanfd_priv *priv, struct can_frame *cf,
				const u32 reg_ec)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	unsigned int type;
	u32 reg_state, reg_cmd;

	type = FIELD_GET(RKCANFD_REG_ERROR_CODE_TYPE, reg_ec);
	reg_cmd = rkcanfd_read(priv, RKCANFD_REG_CMD);
	reg_state = rkcanfd_read(priv, RKCANFD_REG_STATE);

	netdev_dbg(priv->ndev, "%s Error in %s %s Phase: %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s(0x%08x) CMD=%u RX=%u TX=%u Error-Warning=%u Bus-Off=%u\n",
		   rkcanfd_get_error_type_str(type),
		   reg_ec & RKCANFD_REG_ERROR_CODE_DIRECTION_RX ? "RX" : "TX",
		   reg_ec & RKCANFD_REG_ERROR_CODE_PHASE ? "Data" : "Arbitration",
		   RKCAN_ERROR_CODE(reg_ec, TX_OVERLOAD),
		   RKCAN_ERROR_CODE(reg_ec, TX_ERROR),
		   RKCAN_ERROR_CODE(reg_ec, TX_ACK),
		   RKCAN_ERROR_CODE(reg_ec, TX_ACK_EOF),
		   RKCAN_ERROR_CODE(reg_ec, TX_CRC),
		   RKCAN_ERROR_CODE(reg_ec, TX_STUFF_COUNT),
		   RKCAN_ERROR_CODE(reg_ec, TX_DATA),
		   RKCAN_ERROR_CODE(reg_ec, TX_SOF_DLC),
		   RKCAN_ERROR_CODE(reg_ec, TX_IDLE),
		   RKCAN_ERROR_CODE(reg_ec, RX_BUF_INT),
		   RKCAN_ERROR_CODE(reg_ec, RX_SPACE),
		   RKCAN_ERROR_CODE(reg_ec, RX_EOF),
		   RKCAN_ERROR_CODE(reg_ec, RX_ACK_LIM),
		   RKCAN_ERROR_CODE(reg_ec, RX_ACK),
		   RKCAN_ERROR_CODE(reg_ec, RX_CRC_LIM),
		   RKCAN_ERROR_CODE(reg_ec, RX_CRC),
		   RKCAN_ERROR_CODE(reg_ec, RX_STUFF_COUNT),
		   RKCAN_ERROR_CODE(reg_ec, RX_DATA),
		   RKCAN_ERROR_CODE(reg_ec, RX_DLC),
		   RKCAN_ERROR_CODE(reg_ec, RX_BRS_ESI),
		   RKCAN_ERROR_CODE(reg_ec, RX_RES),
		   RKCAN_ERROR_CODE(reg_ec, RX_FDF),
		   RKCAN_ERROR_CODE(reg_ec, RX_ID2_RTR),
		   RKCAN_ERROR_CODE(reg_ec, RX_SOF_IDE),
		   RKCAN_ERROR_CODE(reg_ec, RX_IDLE),
		   reg_ec, reg_cmd,
		   !!(reg_state & RKCANFD_REG_STATE_RX_PERIOD),
		   !!(reg_state & RKCANFD_REG_STATE_TX_PERIOD),
		   !!(reg_state & RKCANFD_REG_STATE_ERROR_WARNING_STATE),
		   !!(reg_state & RKCANFD_REG_STATE_BUS_OFF_STATE));

	priv->can.can_stats.bus_error++;

	if (reg_ec & RKCANFD_REG_ERROR_CODE_DIRECTION_RX)
		stats->rx_errors++;
	else
		stats->tx_errors++;

	if (!cf)
		return;

	if (reg_ec & RKCANFD_REG_ERROR_CODE_DIRECTION_RX) {
		if (reg_ec & RKCANFD_REG_ERROR_CODE_RX_SOF_IDE)
			cf->data[3] = CAN_ERR_PROT_LOC_SOF;
		else if (reg_ec & RKCANFD_REG_ERROR_CODE_RX_ID2_RTR)
			cf->data[3] = CAN_ERR_PROT_LOC_RTR;
		/* RKCANFD_REG_ERROR_CODE_RX_FDF */
		else if (reg_ec & RKCANFD_REG_ERROR_CODE_RX_RES)
			cf->data[3] = CAN_ERR_PROT_LOC_RES0;
		/* RKCANFD_REG_ERROR_CODE_RX_BRS_ESI */
		else if (reg_ec & RKCANFD_REG_ERROR_CODE_RX_DLC)
			cf->data[3] = CAN_ERR_PROT_LOC_DLC;
		else if (reg_ec & RKCANFD_REG_ERROR_CODE_RX_DATA)
			cf->data[3] = CAN_ERR_PROT_LOC_DATA;
		/* RKCANFD_REG_ERROR_CODE_RX_STUFF_COUNT */
		else if (reg_ec & RKCANFD_REG_ERROR_CODE_RX_CRC)
			cf->data[3] = CAN_ERR_PROT_LOC_CRC_SEQ;
		else if (reg_ec & RKCANFD_REG_ERROR_CODE_RX_CRC_LIM)
			cf->data[3] = CAN_ERR_PROT_LOC_ACK_DEL;
		else if (reg_ec & RKCANFD_REG_ERROR_CODE_RX_ACK)
			cf->data[3] = CAN_ERR_PROT_LOC_ACK;
		else if (reg_ec & RKCANFD_REG_ERROR_CODE_RX_ACK_LIM)
			cf->data[3] = CAN_ERR_PROT_LOC_ACK_DEL;
		else if (reg_ec & RKCANFD_REG_ERROR_CODE_RX_EOF)
			cf->data[3] = CAN_ERR_PROT_LOC_EOF;
		else if (reg_ec & RKCANFD_REG_ERROR_CODE_RX_SPACE)
			cf->data[3] = CAN_ERR_PROT_LOC_EOF;
		else if (reg_ec & RKCANFD_REG_ERROR_CODE_RX_BUF_INT)
			cf->data[3] = CAN_ERR_PROT_LOC_INTERM;
	} else {
		cf->data[2] |= CAN_ERR_PROT_TX;

		if (reg_ec & RKCANFD_REG_ERROR_CODE_TX_SOF_DLC)
			cf->data[3] = CAN_ERR_PROT_LOC_SOF;
		else if (reg_ec & RKCANFD_REG_ERROR_CODE_TX_DATA)
			cf->data[3] = CAN_ERR_PROT_LOC_DATA;
		/* RKCANFD_REG_ERROR_CODE_TX_STUFF_COUNT */
		else if (reg_ec & RKCANFD_REG_ERROR_CODE_TX_CRC)
			cf->data[3] = CAN_ERR_PROT_LOC_CRC_SEQ;
		else if (reg_ec & RKCANFD_REG_ERROR_CODE_TX_ACK_EOF)
			cf->data[3] = CAN_ERR_PROT_LOC_ACK_DEL;
		else if (reg_ec & RKCANFD_REG_ERROR_CODE_TX_ACK)
			cf->data[3] = CAN_ERR_PROT_LOC_ACK;
		/* RKCANFD_REG_ERROR_CODE_TX_ERROR */
		else if (reg_ec & RKCANFD_REG_ERROR_CODE_TX_OVERLOAD)
			cf->data[2] |= CAN_ERR_PROT_OVERLOAD;
	}

	switch (reg_ec & RKCANFD_REG_ERROR_CODE_TYPE) {
	case FIELD_PREP_CONST(RKCANFD_REG_ERROR_CODE_TYPE,
			      RKCANFD_REG_ERROR_CODE_TYPE_BIT):

		cf->data[2] |= CAN_ERR_PROT_BIT;
		break;
	case FIELD_PREP_CONST(RKCANFD_REG_ERROR_CODE_TYPE,
			      RKCANFD_REG_ERROR_CODE_TYPE_STUFF):
		cf->data[2] |= CAN_ERR_PROT_STUFF;
		break;
	case FIELD_PREP_CONST(RKCANFD_REG_ERROR_CODE_TYPE,
			      RKCANFD_REG_ERROR_CODE_TYPE_FORM):
		cf->data[2] |= CAN_ERR_PROT_FORM;
		break;
	case FIELD_PREP_CONST(RKCANFD_REG_ERROR_CODE_TYPE,
			      RKCANFD_REG_ERROR_CODE_TYPE_ACK):
		cf->can_id |= CAN_ERR_ACK;
		break;
	case FIELD_PREP_CONST(RKCANFD_REG_ERROR_CODE_TYPE,
			      RKCANFD_REG_ERROR_CODE_TYPE_CRC):
		cf->data[3] = CAN_ERR_PROT_LOC_CRC_SEQ;
		break;
	}
}

static int rkcanfd_handle_error_int(struct rkcanfd_priv *priv)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	struct can_frame *cf = NULL;
	u32 reg_ec, timestamp;
	struct sk_buff *skb;
	int err;

	reg_ec = rkcanfd_read(priv, RKCANFD_REG_ERROR_CODE);

	if (!reg_ec)
		return 0;

	if (priv->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING) {
		skb = rkcanfd_alloc_can_err_skb(priv, &cf, &timestamp);
		if (cf) {
			struct can_berr_counter bec;

			rkcanfd_get_berr_counter_corrected(priv, &bec);
			cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR | CAN_ERR_CNT;
			cf->data[6] = bec.txerr;
			cf->data[7] = bec.rxerr;
		}
	}

	rkcanfd_handle_error_int_reg_ec(priv, cf, reg_ec);

	if (!cf)
		return 0;

	err = can_rx_offload_queue_timestamp(&priv->offload, skb, timestamp);
	if (err)
		stats->rx_fifo_errors++;

	return 0;
}

static int rkcanfd_handle_state_error_int(struct rkcanfd_priv *priv)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	enum can_state new_state, rx_state, tx_state;
	struct net_device *ndev = priv->ndev;
	struct can_berr_counter bec;
	struct can_frame *cf = NULL;
	struct sk_buff *skb;
	u32 timestamp;
	int err;

	rkcanfd_get_berr_counter_corrected(priv, &bec);
	can_state_get_by_berr_counter(ndev, &bec, &tx_state, &rx_state);

	new_state = max(tx_state, rx_state);
	if (new_state == priv->can.state)
		return 0;

	/* The skb allocation might fail, but can_change_state()
	 * handles cf == NULL.
	 */
	skb = rkcanfd_alloc_can_err_skb(priv, &cf, &timestamp);
	can_change_state(ndev, cf, tx_state, rx_state);

	if (new_state == CAN_STATE_BUS_OFF) {
		rkcanfd_chip_stop(priv, CAN_STATE_BUS_OFF);
		can_bus_off(ndev);
	}

	if (!skb)
		return 0;

	if (new_state != CAN_STATE_BUS_OFF) {
		cf->can_id |= CAN_ERR_CNT;
		cf->data[6] = bec.txerr;
		cf->data[7] = bec.rxerr;
	}

	err = can_rx_offload_queue_timestamp(&priv->offload, skb, timestamp);
	if (err)
		stats->rx_fifo_errors++;

	return 0;
}

static int
rkcanfd_handle_rx_fifo_overflow_int(struct rkcanfd_priv *priv)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	struct can_berr_counter bec;
	struct can_frame *cf = NULL;
	struct sk_buff *skb;
	u32 timestamp;
	int err;

	stats->rx_over_errors++;
	stats->rx_errors++;

	netdev_dbg(priv->ndev, "RX-FIFO overflow\n");

	skb = rkcanfd_alloc_can_err_skb(priv, &cf, &timestamp);
	if (!skb)
		return 0;

	rkcanfd_get_berr_counter_corrected(priv, &bec);

	cf->can_id |= CAN_ERR_CRTL | CAN_ERR_CNT;
	cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
	cf->data[6] = bec.txerr;
	cf->data[7] = bec.rxerr;

	err = can_rx_offload_queue_timestamp(&priv->offload, skb, timestamp);
	if (err)
		stats->rx_fifo_errors++;

	return 0;
}

#define rkcanfd_handle(priv, irq, ...) \
({ \
	struct rkcanfd_priv *_priv = (priv); \
	int err; \
\
	err = rkcanfd_handle_##irq(_priv, ## __VA_ARGS__); \
	if (err) \
		netdev_err(_priv->ndev, \
			"IRQ handler rkcanfd_handle_%s() returned error: %pe\n", \
			   __stringify(irq), ERR_PTR(err)); \
	err; \
})

static irqreturn_t rkcanfd_irq(int irq, void *dev_id)
{
	struct rkcanfd_priv *priv = dev_id;
	u32 reg_int_unmasked, reg_int;

	reg_int_unmasked = rkcanfd_read(priv, RKCANFD_REG_INT);
	reg_int = reg_int_unmasked & ~priv->reg_int_mask_default;

	if (!reg_int)
		return IRQ_NONE;

	/* First ACK then handle, to avoid lost-IRQ race condition on
	 * fast re-occurring interrupts.
	 */
	rkcanfd_write(priv, RKCANFD_REG_INT, reg_int);

	if (reg_int & RKCANFD_REG_INT_RX_FINISH_INT)
		rkcanfd_handle(priv, rx_int);

	if (reg_int & RKCANFD_REG_INT_ERROR_INT)
		rkcanfd_handle(priv, error_int);

	if (reg_int & (RKCANFD_REG_INT_BUS_OFF_INT |
		       RKCANFD_REG_INT_PASSIVE_ERROR_INT |
		       RKCANFD_REG_INT_ERROR_WARNING_INT) ||
	    priv->can.state > CAN_STATE_ERROR_ACTIVE)
		rkcanfd_handle(priv, state_error_int);

	if (reg_int & RKCANFD_REG_INT_RX_FIFO_OVERFLOW_INT)
		rkcanfd_handle(priv, rx_fifo_overflow_int);

	if (reg_int & ~(RKCANFD_REG_INT_ALL_ERROR |
			RKCANFD_REG_INT_RX_FIFO_OVERFLOW_INT |
			RKCANFD_REG_INT_RX_FINISH_INT))
		netdev_err(priv->ndev, "%s: int=0x%08x\n", __func__, reg_int);

	if (reg_int & RKCANFD_REG_INT_WAKEUP_INT)
		netdev_info(priv->ndev, "%s: WAKEUP_INT\n", __func__);

	if (reg_int & RKCANFD_REG_INT_TXE_FIFO_FULL_INT)
		netdev_info(priv->ndev, "%s: TXE_FIFO_FULL_INT\n", __func__);

	if (reg_int & RKCANFD_REG_INT_TXE_FIFO_OV_INT)
		netdev_info(priv->ndev, "%s: TXE_FIFO_OV_INT\n", __func__);

	if (reg_int & RKCANFD_REG_INT_BUS_OFF_RECOVERY_INT)
		netdev_info(priv->ndev, "%s: BUS_OFF_RECOVERY_INT\n", __func__);

	if (reg_int & RKCANFD_REG_INT_RX_FIFO_FULL_INT)
		netdev_info(priv->ndev, "%s: RX_FIFO_FULL_INT\n", __func__);

	if (reg_int & RKCANFD_REG_INT_OVERLOAD_INT)
		netdev_info(priv->ndev, "%s: OVERLOAD_INT\n", __func__);

	can_rx_offload_irq_finish(&priv->offload);

	return IRQ_HANDLED;
}

static int rkcanfd_open(struct net_device *ndev)
{
	struct rkcanfd_priv *priv = netdev_priv(ndev);
	int err;

	err = open_candev(ndev);
	if (err)
		return err;

	err = pm_runtime_resume_and_get(ndev->dev.parent);
	if (err)
		goto out_close_candev;

	rkcanfd_chip_start(priv);
	can_rx_offload_enable(&priv->offload);

	err = request_irq(ndev->irq, rkcanfd_irq, IRQF_SHARED, ndev->name, priv);
	if (err)
		goto out_rkcanfd_chip_stop;

	rkcanfd_chip_interrupts_enable(priv);

	netif_start_queue(ndev);

	return 0;

out_rkcanfd_chip_stop:
	rkcanfd_chip_stop_sync(priv, CAN_STATE_STOPPED);
	pm_runtime_put(ndev->dev.parent);
out_close_candev:
	close_candev(ndev);
	return err;
}

static int rkcanfd_stop(struct net_device *ndev)
{
	struct rkcanfd_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);

	rkcanfd_chip_interrupts_disable(priv);
	free_irq(ndev->irq, priv);
	can_rx_offload_disable(&priv->offload);
	rkcanfd_chip_stop_sync(priv, CAN_STATE_STOPPED);
	close_candev(ndev);

	pm_runtime_put(ndev->dev.parent);

	return 0;
}

static const struct net_device_ops rkcanfd_netdev_ops = {
	.ndo_open = rkcanfd_open,
	.ndo_stop = rkcanfd_stop,
	.ndo_start_xmit = rkcanfd_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static int __maybe_unused rkcanfd_runtime_suspend(struct device *dev)
{
	struct rkcanfd_priv *priv = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(priv->clks_num, priv->clks);

	return 0;
}

static int __maybe_unused rkcanfd_runtime_resume(struct device *dev)
{
	struct rkcanfd_priv *priv = dev_get_drvdata(dev);

	return clk_bulk_prepare_enable(priv->clks_num, priv->clks);
}

static void rkcanfd_register_done(const struct rkcanfd_priv *priv)
{
	u32 dev_id;

	dev_id = rkcanfd_read(priv, RKCANFD_REG_RTL_VERSION);

	netdev_info(priv->ndev,
		    "Rockchip-CANFD %s rev%lu.%lu (errata 0x%04x) found\n",
		    rkcanfd_get_model_str(priv),
		    FIELD_GET(RKCANFD_REG_RTL_VERSION_MAJOR, dev_id),
		    FIELD_GET(RKCANFD_REG_RTL_VERSION_MINOR, dev_id),
		    priv->devtype_data.quirks);

	if (priv->devtype_data.quirks & RKCANFD_QUIRK_RK3568_ERRATUM_5 &&
	    priv->can.clock.freq < RKCANFD_ERRATUM_5_SYSCLOCK_HZ_MIN)
		netdev_info(priv->ndev,
			    "Erratum 5: CAN clock frequency (%luMHz) lower than known good (%luMHz), expect degraded performance\n",
			    priv->can.clock.freq / MEGA,
			    RKCANFD_ERRATUM_5_SYSCLOCK_HZ_MIN / MEGA);
}

static int rkcanfd_register(struct rkcanfd_priv *priv)
{
	struct net_device *ndev = priv->ndev;
	int err;

	pm_runtime_enable(ndev->dev.parent);

	err = pm_runtime_resume_and_get(ndev->dev.parent);
	if (err)
		goto out_pm_runtime_disable;

	rkcanfd_ethtool_init(priv);

	err = register_candev(ndev);
	if (err)
		goto out_pm_runtime_put_sync;

	rkcanfd_register_done(priv);

	pm_runtime_put(ndev->dev.parent);

	return 0;

out_pm_runtime_put_sync:
	pm_runtime_put_sync(ndev->dev.parent);
out_pm_runtime_disable:
	pm_runtime_disable(ndev->dev.parent);

	return err;
}

static inline void rkcanfd_unregister(struct rkcanfd_priv *priv)
{
	struct net_device *ndev	= priv->ndev;

	unregister_candev(ndev);
	pm_runtime_disable(ndev->dev.parent);
}

static const struct of_device_id rkcanfd_of_match[] = {
	{
		.compatible = "rockchip,rk3568v2-canfd",
		.data = &rkcanfd_devtype_data_rk3568v2,
	}, {
		.compatible = "rockchip,rk3568v3-canfd",
		.data = &rkcanfd_devtype_data_rk3568v3,
	}, {
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(of, rkcanfd_of_match);

static int rkcanfd_probe(struct platform_device *pdev)
{
	struct rkcanfd_priv *priv;
	struct net_device *ndev;
	const void *match;
	int err;

	ndev = alloc_candev(sizeof(struct rkcanfd_priv), RKCANFD_TXFIFO_DEPTH);
	if (!ndev)
		return -ENOMEM;

	priv = netdev_priv(ndev);

	ndev->irq = platform_get_irq(pdev, 0);
	if (ndev->irq < 0) {
		err = ndev->irq;
		goto out_free_candev;
	}

	priv->clks_num = devm_clk_bulk_get_all(&pdev->dev, &priv->clks);
	if (priv->clks_num < 0) {
		err = priv->clks_num;
		goto out_free_candev;
	}

	priv->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->regs)) {
		err = PTR_ERR(priv->regs);
		goto out_free_candev;
	}

	priv->reset = devm_reset_control_array_get_exclusive(&pdev->dev);
	if (IS_ERR(priv->reset)) {
		err = dev_err_probe(&pdev->dev, PTR_ERR(priv->reset),
				    "Failed to get reset line\n");
		goto out_free_candev;
	}

	SET_NETDEV_DEV(ndev, &pdev->dev);

	ndev->netdev_ops = &rkcanfd_netdev_ops;
	ndev->flags |= IFF_ECHO;

	platform_set_drvdata(pdev, priv);
	priv->can.clock.freq = clk_get_rate(priv->clks[0].clk);
	priv->can.bittiming_const = &rkcanfd_bittiming_const;
	priv->can.data_bittiming_const = &rkcanfd_data_bittiming_const;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK |
		CAN_CTRLMODE_BERR_REPORTING;
	priv->can.do_set_mode = rkcanfd_set_mode;
	priv->can.do_get_berr_counter = rkcanfd_get_berr_counter;
	priv->ndev = ndev;

	match = device_get_match_data(&pdev->dev);
	if (match) {
		priv->devtype_data = *(struct rkcanfd_devtype_data *)match;
		if (!(priv->devtype_data.quirks & RKCANFD_QUIRK_CANFD_BROKEN))
			priv->can.ctrlmode_supported |= CAN_CTRLMODE_FD;
	}

	err = can_rx_offload_add_manual(ndev, &priv->offload,
					RKCANFD_NAPI_WEIGHT);
	if (err)
		goto out_free_candev;

	err = rkcanfd_register(priv);
	if (err)
		goto out_can_rx_offload_del;

	return 0;

out_can_rx_offload_del:
	can_rx_offload_del(&priv->offload);
out_free_candev:
	free_candev(ndev);

	return err;
}

static void rkcanfd_remove(struct platform_device *pdev)
{
	struct rkcanfd_priv *priv = platform_get_drvdata(pdev);
	struct net_device *ndev = priv->ndev;

	rkcanfd_unregister(priv);
	can_rx_offload_del(&priv->offload);
	free_candev(ndev);
}

static const struct dev_pm_ops rkcanfd_pm_ops = {
	SET_RUNTIME_PM_OPS(rkcanfd_runtime_suspend,
			   rkcanfd_runtime_resume, NULL)
};

static struct platform_driver rkcanfd_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.pm = &rkcanfd_pm_ops,
		.of_match_table = rkcanfd_of_match,
	},
	.probe = rkcanfd_probe,
	.remove = rkcanfd_remove,
};
module_platform_driver(rkcanfd_driver);

MODULE_AUTHOR("Marc Kleine-Budde <mkl@pengutronix.de>");
MODULE_DESCRIPTION("Rockchip CAN-FD Driver");
MODULE_LICENSE("GPL");
