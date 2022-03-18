// SPDX-License-Identifier: GPL-2.0
/* Marvell CN10K RPM driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include "cgx.h"
#include "lmac_common.h"

static struct mac_ops	rpm_mac_ops   = {
	.name		=       "rpm",
	.csr_offset     =       0x4e00,
	.lmac_offset    =       20,
	.int_register	=       RPMX_CMRX_SW_INT,
	.int_set_reg    =       RPMX_CMRX_SW_INT_ENA_W1S,
	.irq_offset     =       1,
	.int_ena_bit    =       BIT_ULL(0),
	.lmac_fwi	=	RPM_LMAC_FWI,
	.non_contiguous_serdes_lane = true,
	.rx_stats_cnt   =       43,
	.tx_stats_cnt   =       34,
	.get_nr_lmacs	=	rpm_get_nr_lmacs,
	.get_lmac_type  =       rpm_get_lmac_type,
	.mac_lmac_intl_lbk =    rpm_lmac_internal_loopback,
	.mac_get_rx_stats  =	rpm_get_rx_stats,
	.mac_get_tx_stats  =	rpm_get_tx_stats,
	.mac_enadis_rx_pause_fwding =	rpm_lmac_enadis_rx_pause_fwding,
	.mac_get_pause_frm_status =	rpm_lmac_get_pause_frm_status,
	.mac_enadis_pause_frm =		rpm_lmac_enadis_pause_frm,
	.mac_pause_frm_config =		rpm_lmac_pause_frm_config,
	.mac_enadis_ptp_config =	rpm_lmac_ptp_config,
	.mac_rx_tx_enable =		rpm_lmac_rx_tx_enable,
	.mac_tx_enable =		rpm_lmac_tx_enable,
};

struct mac_ops *rpm_get_mac_ops(void)
{
	return &rpm_mac_ops;
}

static void rpm_write(rpm_t *rpm, u64 lmac, u64 offset, u64 val)
{
	cgx_write(rpm, lmac, offset, val);
}

static u64 rpm_read(rpm_t *rpm, u64 lmac, u64 offset)
{
	return	cgx_read(rpm, lmac, offset);
}

int rpm_get_nr_lmacs(void *rpmd)
{
	rpm_t *rpm = rpmd;

	return hweight8(rpm_read(rpm, 0, CGXX_CMRX_RX_LMACS) & 0xFULL);
}

int rpm_lmac_tx_enable(void *rpmd, int lmac_id, bool enable)
{
	rpm_t *rpm = rpmd;
	u64 cfg, last;

	if (!is_lmac_valid(rpm, lmac_id))
		return -ENODEV;

	cfg = rpm_read(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG);
	last = cfg;
	if (enable)
		cfg |= RPM_TX_EN;
	else
		cfg &= ~(RPM_TX_EN);

	if (cfg != last)
		rpm_write(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG, cfg);
	return !!(last & RPM_TX_EN);
}

int rpm_lmac_rx_tx_enable(void *rpmd, int lmac_id, bool enable)
{
	rpm_t *rpm = rpmd;
	u64 cfg;

	if (!is_lmac_valid(rpm, lmac_id))
		return -ENODEV;

	cfg = rpm_read(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG);
	if (enable)
		cfg |= RPM_RX_EN | RPM_TX_EN;
	else
		cfg &= ~(RPM_RX_EN | RPM_TX_EN);
	rpm_write(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG, cfg);
	return 0;
}

void rpm_lmac_enadis_rx_pause_fwding(void *rpmd, int lmac_id, bool enable)
{
	rpm_t *rpm = rpmd;
	u64 cfg;

	if (!rpm)
		return;

	if (enable) {
		cfg = rpm_read(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG);
		cfg &= ~RPMX_MTI_MAC100X_COMMAND_CONFIG_PAUSE_IGNORE;
		rpm_write(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG, cfg);
	} else {
		cfg = rpm_read(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG);
		cfg |= RPMX_MTI_MAC100X_COMMAND_CONFIG_PAUSE_IGNORE;
		rpm_write(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG, cfg);
	}
}

int rpm_lmac_get_pause_frm_status(void *rpmd, int lmac_id,
				  u8 *tx_pause, u8 *rx_pause)
{
	rpm_t *rpm = rpmd;
	u64 cfg;

	if (!is_lmac_valid(rpm, lmac_id))
		return -ENODEV;

	cfg = rpm_read(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG);
	*rx_pause = !(cfg & RPMX_MTI_MAC100X_COMMAND_CONFIG_RX_P_DISABLE);

	cfg = rpm_read(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG);
	*tx_pause = !(cfg & RPMX_MTI_MAC100X_COMMAND_CONFIG_TX_P_DISABLE);
	return 0;
}

int rpm_lmac_enadis_pause_frm(void *rpmd, int lmac_id, u8 tx_pause,
			      u8 rx_pause)
{
	rpm_t *rpm = rpmd;
	u64 cfg;

	if (!is_lmac_valid(rpm, lmac_id))
		return -ENODEV;

	cfg = rpm_read(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG);
	cfg &= ~RPMX_MTI_MAC100X_COMMAND_CONFIG_RX_P_DISABLE;
	cfg |= rx_pause ? 0x0 : RPMX_MTI_MAC100X_COMMAND_CONFIG_RX_P_DISABLE;
	cfg &= ~RPMX_MTI_MAC100X_COMMAND_CONFIG_PAUSE_IGNORE;
	cfg |= rx_pause ? 0x0 : RPMX_MTI_MAC100X_COMMAND_CONFIG_PAUSE_IGNORE;
	rpm_write(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG, cfg);

	cfg = rpm_read(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG);
	cfg &= ~RPMX_MTI_MAC100X_COMMAND_CONFIG_TX_P_DISABLE;
	cfg |= tx_pause ? 0x0 : RPMX_MTI_MAC100X_COMMAND_CONFIG_TX_P_DISABLE;
	rpm_write(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG, cfg);

	cfg = rpm_read(rpm, 0, RPMX_CMR_RX_OVR_BP);
	if (tx_pause) {
		cfg &= ~RPMX_CMR_RX_OVR_BP_EN(lmac_id);
	} else {
		cfg |= RPMX_CMR_RX_OVR_BP_EN(lmac_id);
		cfg &= ~RPMX_CMR_RX_OVR_BP_BP(lmac_id);
	}
	rpm_write(rpm, 0, RPMX_CMR_RX_OVR_BP, cfg);
	return 0;
}

void rpm_lmac_pause_frm_config(void *rpmd, int lmac_id, bool enable)
{
	rpm_t *rpm = rpmd;
	u64 cfg;

	if (enable) {
		/* Enable 802.3 pause frame mode */
		cfg = rpm_read(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG);
		cfg &= ~RPMX_MTI_MAC100X_COMMAND_CONFIG_PFC_MODE;
		rpm_write(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG, cfg);

		/* Enable receive pause frames */
		cfg = rpm_read(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG);
		cfg &= ~RPMX_MTI_MAC100X_COMMAND_CONFIG_RX_P_DISABLE;
		rpm_write(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG, cfg);

		/* Enable forward pause to TX block */
		cfg = rpm_read(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG);
		cfg &= ~RPMX_MTI_MAC100X_COMMAND_CONFIG_PAUSE_IGNORE;
		rpm_write(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG, cfg);

		/* Enable pause frames transmission */
		cfg = rpm_read(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG);
		cfg &= ~RPMX_MTI_MAC100X_COMMAND_CONFIG_TX_P_DISABLE;
		rpm_write(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG, cfg);

		/* Set pause time and interval */
		cfg = rpm_read(rpm, lmac_id,
			       RPMX_MTI_MAC100X_CL01_PAUSE_QUANTA);
		cfg &= ~0xFFFFULL;
		rpm_write(rpm, lmac_id, RPMX_MTI_MAC100X_CL01_PAUSE_QUANTA,
			  cfg | RPM_DEFAULT_PAUSE_TIME);
		/* Set pause interval as the hardware default is too short */
		cfg = rpm_read(rpm, lmac_id,
			       RPMX_MTI_MAC100X_CL01_QUANTA_THRESH);
		cfg &= ~0xFFFFULL;
		rpm_write(rpm, lmac_id, RPMX_MTI_MAC100X_CL01_QUANTA_THRESH,
			  cfg | (RPM_DEFAULT_PAUSE_TIME / 2));

	} else {
		/* ALL pause frames received are completely ignored */
		cfg = rpm_read(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG);
		cfg |= RPMX_MTI_MAC100X_COMMAND_CONFIG_RX_P_DISABLE;
		rpm_write(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG, cfg);

		/* Disable forward pause to TX block */
		cfg = rpm_read(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG);
		cfg |= RPMX_MTI_MAC100X_COMMAND_CONFIG_PAUSE_IGNORE;
		rpm_write(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG, cfg);

		/* Disable pause frames transmission */
		cfg = rpm_read(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG);
		cfg |= RPMX_MTI_MAC100X_COMMAND_CONFIG_TX_P_DISABLE;
		rpm_write(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG, cfg);
	}
}

int rpm_get_rx_stats(void *rpmd, int lmac_id, int idx, u64 *rx_stat)
{
	rpm_t *rpm = rpmd;
	u64 val_lo, val_hi;

	if (!rpm || lmac_id >= rpm->lmac_count)
		return -ENODEV;

	mutex_lock(&rpm->lock);

	/* Update idx to point per lmac Rx statistics page */
	idx += lmac_id * rpm->mac_ops->rx_stats_cnt;

	/* Read lower 32 bits of counter */
	val_lo = rpm_read(rpm, 0, RPMX_MTI_STAT_RX_STAT_PAGES_COUNTERX +
			  (idx * 8));

	/* upon read of lower 32 bits, higher 32 bits are written
	 * to RPMX_MTI_STAT_DATA_HI_CDC
	 */
	val_hi = rpm_read(rpm, 0, RPMX_MTI_STAT_DATA_HI_CDC);

	*rx_stat = (val_hi << 32 | val_lo);

	mutex_unlock(&rpm->lock);
	return 0;
}

int rpm_get_tx_stats(void *rpmd, int lmac_id, int idx, u64 *tx_stat)
{
	rpm_t *rpm = rpmd;
	u64 val_lo, val_hi;

	if (!rpm || lmac_id >= rpm->lmac_count)
		return -ENODEV;

	mutex_lock(&rpm->lock);

	/* Update idx to point per lmac Tx statistics page */
	idx += lmac_id * rpm->mac_ops->tx_stats_cnt;

	val_lo = rpm_read(rpm, 0, RPMX_MTI_STAT_TX_STAT_PAGES_COUNTERX +
			    (idx * 8));
	val_hi = rpm_read(rpm, 0, RPMX_MTI_STAT_DATA_HI_CDC);

	*tx_stat = (val_hi << 32 | val_lo);

	mutex_unlock(&rpm->lock);
	return 0;
}

u8 rpm_get_lmac_type(void *rpmd, int lmac_id)
{
	rpm_t *rpm = rpmd;
	u64 req = 0, resp;
	int err;

	req = FIELD_SET(CMDREG_ID, CGX_CMD_GET_LINK_STS, req);
	err = cgx_fwi_cmd_generic(req, &resp, rpm, 0);
	if (!err)
		return FIELD_GET(RESP_LINKSTAT_LMAC_TYPE, resp);
	return err;
}

int rpm_lmac_internal_loopback(void *rpmd, int lmac_id, bool enable)
{
	rpm_t *rpm = rpmd;
	u8 lmac_type;
	u64 cfg;

	if (!rpm || lmac_id >= rpm->lmac_count)
		return -ENODEV;
	lmac_type = rpm->mac_ops->get_lmac_type(rpm, lmac_id);

	if (lmac_type == LMAC_MODE_QSGMII || lmac_type == LMAC_MODE_SGMII) {
		dev_err(&rpm->pdev->dev, "loopback not supported for LPC mode\n");
		return 0;
	}

	cfg = rpm_read(rpm, lmac_id, RPMX_MTI_PCS100X_CONTROL1);

	if (enable)
		cfg |= RPMX_MTI_PCS_LBK;
	else
		cfg &= ~RPMX_MTI_PCS_LBK;
	rpm_write(rpm, lmac_id, RPMX_MTI_PCS100X_CONTROL1, cfg);

	return 0;
}

void rpm_lmac_ptp_config(void *rpmd, int lmac_id, bool enable)
{
	rpm_t *rpm = rpmd;
	u64 cfg;

	if (!is_lmac_valid(rpm, lmac_id))
		return;

	cfg = rpm_read(rpm, lmac_id, RPMX_CMRX_CFG);
	if (enable)
		cfg |= RPMX_RX_TS_PREPEND;
	else
		cfg &= ~RPMX_RX_TS_PREPEND;
	rpm_write(rpm, lmac_id, RPMX_CMRX_CFG, cfg);
}
