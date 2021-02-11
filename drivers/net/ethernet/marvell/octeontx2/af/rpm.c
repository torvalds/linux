// SPDX-License-Identifier: GPL-2.0
/*  Marvell OcteonTx2 RPM driver
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
	.get_nr_lmacs	=	rpm_get_nr_lmacs,
	.mac_enadis_rx_pause_fwding =	rpm_lmac_enadis_rx_pause_fwding,
	.mac_get_pause_frm_status =	rpm_lmac_get_pause_frm_status,
	.mac_enadis_pause_frm =		rpm_lmac_enadis_pause_frm,
	.mac_pause_frm_config =		rpm_lmac_pause_frm_config,
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

void rpm_lmac_enadis_rx_pause_fwding(void *rpmd, int lmac_id, bool enable)
{
	struct cgx *rpm = rpmd;
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
