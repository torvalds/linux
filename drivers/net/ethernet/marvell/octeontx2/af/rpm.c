// SPDX-License-Identifier: GPL-2.0
/* Marvell CN10K RPM driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include "cgx.h"
#include "lmac_common.h"

static struct mac_ops		rpm_mac_ops   = {
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
	.dmac_filter_count =	32,
	.get_nr_lmacs	=	rpm_get_nr_lmacs,
	.get_lmac_type  =       rpm_get_lmac_type,
	.lmac_fifo_len	=	rpm_get_lmac_fifo_len,
	.mac_lmac_intl_lbk =    rpm_lmac_internal_loopback,
	.mac_get_rx_stats  =	rpm_get_rx_stats,
	.mac_get_tx_stats  =	rpm_get_tx_stats,
	.get_fec_stats	   =	rpm_get_fec_stats,
	.mac_enadis_rx_pause_fwding =	rpm_lmac_enadis_rx_pause_fwding,
	.mac_get_pause_frm_status =	rpm_lmac_get_pause_frm_status,
	.mac_enadis_pause_frm =		rpm_lmac_enadis_pause_frm,
	.mac_pause_frm_config =		rpm_lmac_pause_frm_config,
	.mac_enadis_ptp_config =	rpm_lmac_ptp_config,
	.mac_rx_tx_enable =		rpm_lmac_rx_tx_enable,
	.mac_tx_enable =		rpm_lmac_tx_enable,
	.pfc_config =                   rpm_lmac_pfc_config,
	.mac_get_pfc_frm_cfg   =        rpm_lmac_get_pfc_frm_cfg,
};

static struct mac_ops		rpm2_mac_ops   = {
	.name		=       "rpm",
	.csr_offset     =       RPM2_CSR_OFFSET,
	.lmac_offset    =       20,
	.int_register	=       RPM2_CMRX_SW_INT,
	.int_set_reg    =       RPM2_CMRX_SW_INT_ENA_W1S,
	.irq_offset     =       1,
	.int_ena_bit    =       BIT_ULL(0),
	.lmac_fwi	=	RPM_LMAC_FWI,
	.non_contiguous_serdes_lane = true,
	.rx_stats_cnt   =       43,
	.tx_stats_cnt   =       34,
	.dmac_filter_count =	64,
	.get_nr_lmacs	=	rpm2_get_nr_lmacs,
	.get_lmac_type  =       rpm_get_lmac_type,
	.lmac_fifo_len	=	rpm2_get_lmac_fifo_len,
	.mac_lmac_intl_lbk =    rpm_lmac_internal_loopback,
	.mac_get_rx_stats  =	rpm_get_rx_stats,
	.mac_get_tx_stats  =	rpm_get_tx_stats,
	.get_fec_stats	   =	rpm_get_fec_stats,
	.mac_enadis_rx_pause_fwding =	rpm_lmac_enadis_rx_pause_fwding,
	.mac_get_pause_frm_status =	rpm_lmac_get_pause_frm_status,
	.mac_enadis_pause_frm =		rpm_lmac_enadis_pause_frm,
	.mac_pause_frm_config =		rpm_lmac_pause_frm_config,
	.mac_enadis_ptp_config =	rpm_lmac_ptp_config,
	.mac_rx_tx_enable =		rpm_lmac_rx_tx_enable,
	.mac_tx_enable =		rpm_lmac_tx_enable,
	.pfc_config =                   rpm_lmac_pfc_config,
	.mac_get_pfc_frm_cfg   =        rpm_lmac_get_pfc_frm_cfg,
};

bool is_dev_rpm2(void *rpmd)
{
	rpm_t *rpm = rpmd;

	return (rpm->pdev->device == PCI_DEVID_CN10KB_RPM);
}

struct mac_ops *rpm_get_mac_ops(rpm_t *rpm)
{
	if (is_dev_rpm2(rpm))
		return &rpm2_mac_ops;
	else
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

/* Read HW major version to determine RPM
 * MAC type 100/USX
 */
static bool is_mac_rpmusx(void *rpmd)
{
	rpm_t *rpm = rpmd;

	return rpm_read(rpm, 0, RPMX_CONST1) & 0x700ULL;
}

int rpm_get_nr_lmacs(void *rpmd)
{
	rpm_t *rpm = rpmd;

	return hweight8(rpm_read(rpm, 0, CGXX_CMRX_RX_LMACS) & 0xFULL);
}

int rpm2_get_nr_lmacs(void *rpmd)
{
	rpm_t *rpm = rpmd;

	return hweight8(rpm_read(rpm, 0, RPM2_CMRX_RX_LMACS) & 0xFFULL);
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
	struct lmac *lmac;
	u64 cfg;

	if (!rpm)
		return;

	lmac = lmac_pdata(lmac_id, rpm);
	if (!lmac)
		return;

	/* Pause frames are not enabled just return */
	if (!bitmap_weight(lmac->rx_fc_pfvf_bmap.bmap, lmac->rx_fc_pfvf_bmap.max))
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
	if (!(cfg & RPMX_MTI_MAC100X_COMMAND_CONFIG_PFC_MODE)) {
		*rx_pause = !(cfg & RPMX_MTI_MAC100X_COMMAND_CONFIG_RX_P_DISABLE);
		*tx_pause = !(cfg & RPMX_MTI_MAC100X_COMMAND_CONFIG_TX_P_DISABLE);
	}

	return 0;
}

static void rpm_cfg_pfc_quanta_thresh(rpm_t *rpm, int lmac_id,
				      unsigned long pfc_en,
				      bool enable)
{
	u64 quanta_offset = 0, quanta_thresh = 0, cfg;
	int i, shift;

	/* Set pause time and interval */
	for_each_set_bit(i, &pfc_en, 16) {
		switch (i) {
		case 0:
		case 1:
			quanta_offset = RPMX_MTI_MAC100X_CL01_PAUSE_QUANTA;
			quanta_thresh = RPMX_MTI_MAC100X_CL01_QUANTA_THRESH;
			break;
		case 2:
		case 3:
			quanta_offset = RPMX_MTI_MAC100X_CL23_PAUSE_QUANTA;
			quanta_thresh = RPMX_MTI_MAC100X_CL23_QUANTA_THRESH;
			break;
		case 4:
		case 5:
			quanta_offset = RPMX_MTI_MAC100X_CL45_PAUSE_QUANTA;
			quanta_thresh = RPMX_MTI_MAC100X_CL45_QUANTA_THRESH;
			break;
		case 6:
		case 7:
			quanta_offset = RPMX_MTI_MAC100X_CL67_PAUSE_QUANTA;
			quanta_thresh = RPMX_MTI_MAC100X_CL67_QUANTA_THRESH;
			break;
		case 8:
		case 9:
			quanta_offset = RPMX_MTI_MAC100X_CL89_PAUSE_QUANTA;
			quanta_thresh = RPMX_MTI_MAC100X_CL89_QUANTA_THRESH;
			break;
		case 10:
		case 11:
			quanta_offset = RPMX_MTI_MAC100X_CL1011_PAUSE_QUANTA;
			quanta_thresh = RPMX_MTI_MAC100X_CL1011_QUANTA_THRESH;
			break;
		case 12:
		case 13:
			quanta_offset = RPMX_MTI_MAC100X_CL1213_PAUSE_QUANTA;
			quanta_thresh = RPMX_MTI_MAC100X_CL1213_QUANTA_THRESH;
			break;
		case 14:
		case 15:
			quanta_offset = RPMX_MTI_MAC100X_CL1415_PAUSE_QUANTA;
			quanta_thresh = RPMX_MTI_MAC100X_CL1415_QUANTA_THRESH;
			break;
		}

		if (!quanta_offset || !quanta_thresh)
			continue;

		shift = (i % 2) ? 1 : 0;
		cfg = rpm_read(rpm, lmac_id, quanta_offset);
		if (enable) {
			cfg |= ((u64)RPM_DEFAULT_PAUSE_TIME <<  shift * 16);
		} else {
			if (!shift)
				cfg &= ~GENMASK_ULL(15, 0);
			else
				cfg &= ~GENMASK_ULL(31, 16);
		}
		rpm_write(rpm, lmac_id, quanta_offset, cfg);

		cfg = rpm_read(rpm, lmac_id, quanta_thresh);
		if (enable) {
			cfg |= ((u64)(RPM_DEFAULT_PAUSE_TIME / 2) <<  shift * 16);
		} else {
			if (!shift)
				cfg &= ~GENMASK_ULL(15, 0);
			else
				cfg &= ~GENMASK_ULL(31, 16);
		}
		rpm_write(rpm, lmac_id, quanta_thresh, cfg);
	}
}

static void rpm2_lmac_cfg_bp(rpm_t *rpm, int lmac_id, u8 tx_pause, u8 rx_pause)
{
	u64 cfg;

	cfg = rpm_read(rpm, lmac_id, RPM2_CMR_RX_OVR_BP);
	if (tx_pause) {
		/* Configure CL0 Pause Quanta & threshold
		 * for 802.3X frames
		 */
		rpm_cfg_pfc_quanta_thresh(rpm, lmac_id, 1, true);
		cfg &= ~RPM2_CMR_RX_OVR_BP_EN;
	} else {
		/* Disable all Pause Quanta & threshold values */
		rpm_cfg_pfc_quanta_thresh(rpm, lmac_id, 0xffff, false);
		cfg |= RPM2_CMR_RX_OVR_BP_EN;
		cfg &= ~RPM2_CMR_RX_OVR_BP_BP;
	}
	rpm_write(rpm, lmac_id, RPM2_CMR_RX_OVR_BP, cfg);
}

static void rpm_lmac_cfg_bp(rpm_t *rpm, int lmac_id, u8 tx_pause, u8 rx_pause)
{
	u64 cfg;

	cfg = rpm_read(rpm, 0, RPMX_CMR_RX_OVR_BP);
	if (tx_pause) {
		/* Configure CL0 Pause Quanta & threshold for
		 * 802.3X frames
		 */
		rpm_cfg_pfc_quanta_thresh(rpm, lmac_id, 1, true);
		cfg &= ~RPMX_CMR_RX_OVR_BP_EN(lmac_id);
	} else {
		/* Disable all Pause Quanta & threshold values */
		rpm_cfg_pfc_quanta_thresh(rpm, lmac_id, 0xffff, false);
		cfg |= RPMX_CMR_RX_OVR_BP_EN(lmac_id);
		cfg &= ~RPMX_CMR_RX_OVR_BP_BP(lmac_id);
	}
	rpm_write(rpm, 0, RPMX_CMR_RX_OVR_BP, cfg);
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

	if (is_dev_rpm2(rpm))
		rpm2_lmac_cfg_bp(rpm, lmac_id, tx_pause, rx_pause);
	else
		rpm_lmac_cfg_bp(rpm, lmac_id, tx_pause, rx_pause);

	return 0;
}

void rpm_lmac_pause_frm_config(void *rpmd, int lmac_id, bool enable)
{
	rpm_t *rpm = rpmd;
	u64 cfg;

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

	/* Enable channel mask for all LMACS */
	if (is_dev_rpm2(rpm))
		rpm_write(rpm, lmac_id, RPM2_CMR_CHAN_MSK_OR, 0xffff);
	else
		rpm_write(rpm, 0, RPMX_CMR_CHAN_MSK_OR, ~0ULL);

	/* Disable all PFC classes */
	cfg = rpm_read(rpm, lmac_id, RPMX_CMRX_PRT_CBFC_CTL);
	cfg = FIELD_SET(RPM_PFC_CLASS_MASK, 0, cfg);
	rpm_write(rpm, lmac_id, RPMX_CMRX_PRT_CBFC_CTL, cfg);
}

int rpm_get_rx_stats(void *rpmd, int lmac_id, int idx, u64 *rx_stat)
{
	rpm_t *rpm = rpmd;
	u64 val_lo, val_hi;

	if (!is_lmac_valid(rpm, lmac_id))
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

	if (!is_lmac_valid(rpm, lmac_id))
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

u32 rpm_get_lmac_fifo_len(void *rpmd, int lmac_id)
{
	rpm_t *rpm = rpmd;
	u64 hi_perf_lmac;
	u8 num_lmacs;
	u32 fifo_len;

	fifo_len = rpm->mac_ops->fifo_len;
	num_lmacs = rpm->mac_ops->get_nr_lmacs(rpm);

	switch (num_lmacs) {
	case 1:
		return fifo_len;
	case 2:
		return fifo_len / 2;
	case 3:
		/* LMAC marked as hi_perf gets half of the FIFO and rest 1/4th */
		hi_perf_lmac = rpm_read(rpm, 0, CGXX_CMRX_RX_LMACS);
		hi_perf_lmac = (hi_perf_lmac >> 4) & 0x3ULL;
		if (lmac_id == hi_perf_lmac)
			return fifo_len / 2;
		return fifo_len / 4;
	case 4:
	default:
		return fifo_len / 4;
	}
	return 0;
}

static int rpmusx_lmac_internal_loopback(rpm_t *rpm, int lmac_id, bool enable)
{
	u64 cfg;

	cfg = rpm_read(rpm, lmac_id, RPM2_USX_PCSX_CONTROL1);

	if (enable)
		cfg |= RPM2_USX_PCS_LBK;
	else
		cfg &= ~RPM2_USX_PCS_LBK;
	rpm_write(rpm, lmac_id, RPM2_USX_PCSX_CONTROL1, cfg);

	return 0;
}

u32 rpm2_get_lmac_fifo_len(void *rpmd, int lmac_id)
{
	u64 hi_perf_lmac, lmac_info;
	rpm_t *rpm = rpmd;
	u8 num_lmacs;
	u32 fifo_len;

	lmac_info = rpm_read(rpm, 0, RPM2_CMRX_RX_LMACS);
	/* LMACs are divided into two groups and each group
	 * gets half of the FIFO
	 * Group0 lmac_id range {0..3}
	 * Group1 lmac_id range {4..7}
	 */
	fifo_len = rpm->mac_ops->fifo_len / 2;

	if (lmac_id < 4) {
		num_lmacs = hweight8(lmac_info & 0xF);
		hi_perf_lmac = (lmac_info >> 8) & 0x3ULL;
	} else {
		num_lmacs = hweight8(lmac_info & 0xF0);
		hi_perf_lmac = (lmac_info >> 10) & 0x3ULL;
		hi_perf_lmac += 4;
	}

	switch (num_lmacs) {
	case 1:
		return fifo_len;
	case 2:
		return fifo_len / 2;
	case 3:
		/* LMAC marked as hi_perf gets half of the FIFO
		 * and rest 1/4th
		 */
		if (lmac_id == hi_perf_lmac)
			return fifo_len / 2;
		return fifo_len / 4;
	case 4:
	default:
		return fifo_len / 4;
	}
	return 0;
}

int rpm_lmac_internal_loopback(void *rpmd, int lmac_id, bool enable)
{
	rpm_t *rpm = rpmd;
	u8 lmac_type;
	u64 cfg;

	if (!is_lmac_valid(rpm, lmac_id))
		return -ENODEV;
	lmac_type = rpm->mac_ops->get_lmac_type(rpm, lmac_id);

	if (lmac_type == LMAC_MODE_QSGMII || lmac_type == LMAC_MODE_SGMII) {
		dev_err(&rpm->pdev->dev, "loopback not supported for LPC mode\n");
		return 0;
	}

	if (is_dev_rpm2(rpm) && is_mac_rpmusx(rpm))
		return rpmusx_lmac_internal_loopback(rpm, lmac_id, enable);

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
	if (enable) {
		cfg |= RPMX_RX_TS_PREPEND;
		cfg |= RPMX_TX_PTP_1S_SUPPORT;
	} else {
		cfg &= ~RPMX_RX_TS_PREPEND;
		cfg &= ~RPMX_TX_PTP_1S_SUPPORT;
	}

	rpm_write(rpm, lmac_id, RPMX_CMRX_CFG, cfg);

	cfg = rpm_read(rpm, lmac_id, RPMX_MTI_MAC100X_XIF_MODE);

	if (enable) {
		cfg |= RPMX_ONESTEP_ENABLE;
		cfg &= ~RPMX_TS_BINARY_MODE;
	} else {
		cfg &= ~RPMX_ONESTEP_ENABLE;
	}

	rpm_write(rpm, lmac_id, RPMX_MTI_MAC100X_XIF_MODE, cfg);
}

int rpm_lmac_pfc_config(void *rpmd, int lmac_id, u8 tx_pause, u8 rx_pause, u16 pfc_en)
{
	u64 cfg, class_en, pfc_class_mask_cfg;
	rpm_t *rpm = rpmd;

	if (!is_lmac_valid(rpm, lmac_id))
		return -ENODEV;

	cfg = rpm_read(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG);
	class_en = rpm_read(rpm, lmac_id, RPMX_CMRX_PRT_CBFC_CTL);
	pfc_en |= FIELD_GET(RPM_PFC_CLASS_MASK, class_en);

	if (rx_pause) {
		cfg &= ~(RPMX_MTI_MAC100X_COMMAND_CONFIG_RX_P_DISABLE |
				RPMX_MTI_MAC100X_COMMAND_CONFIG_PAUSE_IGNORE |
				RPMX_MTI_MAC100X_COMMAND_CONFIG_PAUSE_FWD);
	} else {
		cfg |= (RPMX_MTI_MAC100X_COMMAND_CONFIG_RX_P_DISABLE |
				RPMX_MTI_MAC100X_COMMAND_CONFIG_PAUSE_IGNORE |
				RPMX_MTI_MAC100X_COMMAND_CONFIG_PAUSE_FWD);
	}

	if (tx_pause) {
		rpm_cfg_pfc_quanta_thresh(rpm, lmac_id, pfc_en, true);
		cfg &= ~RPMX_MTI_MAC100X_COMMAND_CONFIG_TX_P_DISABLE;
		class_en = FIELD_SET(RPM_PFC_CLASS_MASK, pfc_en, class_en);
	} else {
		rpm_cfg_pfc_quanta_thresh(rpm, lmac_id, 0xfff, false);
		cfg |= RPMX_MTI_MAC100X_COMMAND_CONFIG_TX_P_DISABLE;
		class_en = FIELD_SET(RPM_PFC_CLASS_MASK, 0, class_en);
	}

	if (!rx_pause && !tx_pause)
		cfg &= ~RPMX_MTI_MAC100X_COMMAND_CONFIG_PFC_MODE;
	else
		cfg |= RPMX_MTI_MAC100X_COMMAND_CONFIG_PFC_MODE;

	rpm_write(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG, cfg);

	pfc_class_mask_cfg = is_dev_rpm2(rpm) ? RPM2_CMRX_PRT_CBFC_CTL :
						RPMX_CMRX_PRT_CBFC_CTL;

	rpm_write(rpm, lmac_id, pfc_class_mask_cfg, class_en);

	return 0;
}

int  rpm_lmac_get_pfc_frm_cfg(void *rpmd, int lmac_id, u8 *tx_pause, u8 *rx_pause)
{
	rpm_t *rpm = rpmd;
	u64 cfg;

	if (!is_lmac_valid(rpm, lmac_id))
		return -ENODEV;

	cfg = rpm_read(rpm, lmac_id, RPMX_MTI_MAC100X_COMMAND_CONFIG);
	if (cfg & RPMX_MTI_MAC100X_COMMAND_CONFIG_PFC_MODE) {
		*rx_pause = !(cfg & RPMX_MTI_MAC100X_COMMAND_CONFIG_RX_P_DISABLE);
		*tx_pause = !(cfg & RPMX_MTI_MAC100X_COMMAND_CONFIG_TX_P_DISABLE);
	}

	return 0;
}

int rpm_get_fec_stats(void *rpmd, int lmac_id, struct cgx_fec_stats_rsp *rsp)
{
	u64 val_lo, val_hi;
	rpm_t *rpm = rpmd;
	u64 cfg;

	if (!is_lmac_valid(rpm, lmac_id))
		return -ENODEV;

	if (rpm->lmac_idmap[lmac_id]->link_info.fec == OTX2_FEC_NONE)
		return 0;

	if (rpm->lmac_idmap[lmac_id]->link_info.fec == OTX2_FEC_BASER) {
		val_lo = rpm_read(rpm, lmac_id, RPMX_MTI_FCFECX_VL0_CCW_LO);
		val_hi = rpm_read(rpm, lmac_id, RPMX_MTI_FCFECX_CW_HI);
		rsp->fec_corr_blks = (val_hi << 16 | val_lo);

		val_lo = rpm_read(rpm, lmac_id, RPMX_MTI_FCFECX_VL0_NCCW_LO);
		val_hi = rpm_read(rpm, lmac_id, RPMX_MTI_FCFECX_CW_HI);
		rsp->fec_uncorr_blks = (val_hi << 16 | val_lo);

		/* 50G uses 2 Physical serdes lines */
		if (rpm->lmac_idmap[lmac_id]->link_info.lmac_type_id ==
		    LMAC_MODE_50G_R) {
			val_lo = rpm_read(rpm, lmac_id,
					  RPMX_MTI_FCFECX_VL1_CCW_LO);
			val_hi = rpm_read(rpm, lmac_id,
					  RPMX_MTI_FCFECX_CW_HI);
			rsp->fec_corr_blks += (val_hi << 16 | val_lo);

			val_lo = rpm_read(rpm, lmac_id,
					  RPMX_MTI_FCFECX_VL1_NCCW_LO);
			val_hi = rpm_read(rpm, lmac_id,
					  RPMX_MTI_FCFECX_CW_HI);
			rsp->fec_uncorr_blks += (val_hi << 16 | val_lo);
		}
	} else {
		/* enable RS-FEC capture */
		cfg = rpm_read(rpm, 0, RPMX_MTI_STAT_STATN_CONTROL);
		cfg |= RPMX_RSFEC_RX_CAPTURE | BIT(lmac_id);
		rpm_write(rpm, 0, RPMX_MTI_STAT_STATN_CONTROL, cfg);

		val_lo = rpm_read(rpm, 0,
				  RPMX_MTI_RSFEC_STAT_COUNTER_CAPTURE_2);
		val_hi = rpm_read(rpm, 0, RPMX_MTI_STAT_DATA_HI_CDC);
		rsp->fec_corr_blks = (val_hi << 32 | val_lo);

		val_lo = rpm_read(rpm, 0,
				  RPMX_MTI_RSFEC_STAT_COUNTER_CAPTURE_3);
		val_hi = rpm_read(rpm, 0, RPMX_MTI_STAT_DATA_HI_CDC);
		rsp->fec_uncorr_blks = (val_hi << 32 | val_lo);
	}

	return 0;
}
