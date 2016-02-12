/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * File: card.c
 * Purpose: Provide functions to setup NIC operation mode
 * Functions:
 *      vnt_set_rspinf - Set RSPINF
 *      vnt_update_ifs - Update slotTime,SIFS,DIFS, and EIFS
 *      vnt_update_top_rates - Update BasicTopRate
 *      vnt_add_basic_rate - Add to BasicRateSet
 *      vnt_ofdm_min_rate - Check if any OFDM rate is in BasicRateSet
 *      vnt_get_tsf_offset - Calculate TSFOffset
 *      vnt_get_current_tsf - Read Current NIC TSF counter
 *      vnt_get_next_tbtt - Calculate Next Beacon TSF counter
 *      vnt_reset_next_tbtt - Set NIC Beacon time
 *      vnt_update_next_tbtt - Sync. NIC Beacon time
 *      vnt_radio_power_off - Turn Off NIC Radio Power
 *      vnt_radio_power_on - Turn On NIC Radio Power
 *
 * Revision History:
 *      06-10-2003 Bryan YC Fan:  Re-write codes to support VT3253 spec.
 *      08-26-2003 Kyle Hsu:      Modify the definition type of dwIoBase.
 *      09-01-2003 Bryan YC Fan:  Add vnt_update_ifs().
 *
 */

#include "device.h"
#include "card.h"
#include "baseband.h"
#include "mac.h"
#include "desc.h"
#include "rf.h"
#include "power.h"
#include "key.h"
#include "usbpipe.h"

/* const u16 cwRXBCNTSFOff[MAX_RATE] =
   {17, 34, 96, 192, 34, 23, 17, 11, 8, 5, 4, 3}; */

static const u16 cwRXBCNTSFOff[MAX_RATE] = {
	192, 96, 34, 17, 34, 23, 17, 11, 8, 5, 4, 3
};

/*
 * Description: Set NIC media channel
 *
 * Parameters:
 *  In:
 *      pDevice             - The adapter to be set
 *      connection_channel  - Channel to be set
 *  Out:
 *      none
 */
void vnt_set_channel(struct vnt_private *priv, u32 connection_channel)
{

	if (connection_channel > CB_MAX_CHANNEL || !connection_channel)
		return;

	/* clear NAV */
	vnt_mac_reg_bits_on(priv, MAC_REG_MACCR, MACCR_CLRNAV);

	/* Set Channel[7] = 0 to tell H/W channel is changing now. */
	vnt_mac_reg_bits_off(priv, MAC_REG_CHANNEL, 0xb0);

	vnt_control_out(priv, MESSAGE_TYPE_SELECT_CHANNEL,
					connection_channel, 0, 0, NULL);

	vnt_control_out_u8(priv, MESSAGE_REQUEST_MACREG, MAC_REG_CHANNEL,
		(u8)(connection_channel | 0x80));
}

/*
 * Description: Get CCK mode basic rate
 *
 * Parameters:
 *  In:
 *      priv		- The adapter to be set
 *      rate_idx	- Receiving data rate
 *  Out:
 *      none
 *
 * Return Value: response Control frame rate
 *
 */
static u16 vnt_get_cck_rate(struct vnt_private *priv, u16 rate_idx)
{
	u16 ui = rate_idx;

	while (ui > RATE_1M) {
		if (priv->basic_rates & (1 << ui))
			return ui;
		ui--;
	}

	return RATE_1M;
}

/*
 * Description: Get OFDM mode basic rate
 *
 * Parameters:
 *  In:
 *      priv		- The adapter to be set
 *      rate_idx	- Receiving data rate
 *  Out:
 *      none
 *
 * Return Value: response Control frame rate
 *
 */
static u16 vnt_get_ofdm_rate(struct vnt_private *priv, u16 rate_idx)
{
	u16 ui = rate_idx;

	dev_dbg(&priv->usb->dev, "%s basic rate: %d\n",
					__func__,  priv->basic_rates);

	if (!vnt_ofdm_min_rate(priv)) {
		dev_dbg(&priv->usb->dev, "%s (NO OFDM) %d\n",
						__func__, rate_idx);
		if (rate_idx > RATE_24M)
			rate_idx = RATE_24M;
		return rate_idx;
	}

	while (ui > RATE_11M) {
		if (priv->basic_rates & (1 << ui)) {
			dev_dbg(&priv->usb->dev, "%s rate: %d\n",
							__func__, ui);
			return ui;
		}
		ui--;
	}

	dev_dbg(&priv->usb->dev, "%s basic rate: 24M\n", __func__);

	return RATE_24M;
}

/*
 * Description: Calculate TxRate and RsvTime fields for RSPINF in OFDM mode.
 *
 * Parameters:
 * In:
 *	rate	- Tx Rate
 *	bb_type	- Tx Packet type
 * Out:
 *	tx_rate	- pointer to RSPINF TxRate field
 *	rsv_time- pointer to RSPINF RsvTime field
 *
 * Return Value: none
 *
 */
static void vnt_calculate_ofdm_rate(u16 rate, u8 bb_type,
					u8 *tx_rate, u8 *rsv_time)
{

	switch (rate) {
	case RATE_6M:
		if (bb_type == BB_TYPE_11A) {
			*tx_rate = 0x9b;
			*rsv_time = 24;
		} else {
			*tx_rate = 0x8b;
			*rsv_time = 30;
		}
			break;
	case RATE_9M:
		if (bb_type == BB_TYPE_11A) {
			*tx_rate = 0x9f;
			*rsv_time = 16;
		} else {
			*tx_rate = 0x8f;
			*rsv_time = 22;
		}
		break;
	case RATE_12M:
		if (bb_type == BB_TYPE_11A) {
			*tx_rate = 0x9a;
			*rsv_time = 12;
		} else {
			*tx_rate = 0x8a;
			*rsv_time = 18;
		}
		break;
	case RATE_18M:
		if (bb_type == BB_TYPE_11A) {
			*tx_rate = 0x9e;
			*rsv_time = 8;
		} else {
			*tx_rate = 0x8e;
			*rsv_time = 14;
		}
		break;
	case RATE_36M:
		if (bb_type == BB_TYPE_11A) {
			*tx_rate = 0x9d;
			*rsv_time = 4;
		} else {
			*tx_rate = 0x8d;
			*rsv_time = 10;
		}
		break;
	case RATE_48M:
		if (bb_type == BB_TYPE_11A) {
			*tx_rate = 0x98;
			*rsv_time = 4;
		} else {
			*tx_rate = 0x88;
			*rsv_time = 10;
		}
		break;
	case RATE_54M:
		if (bb_type == BB_TYPE_11A) {
			*tx_rate = 0x9c;
			*rsv_time = 4;
		} else {
			*tx_rate = 0x8c;
			*rsv_time = 10;
		}
		break;
	case RATE_24M:
	default:
		if (bb_type == BB_TYPE_11A) {
			*tx_rate = 0x99;
			*rsv_time = 8;
		} else {
			*tx_rate = 0x89;
			*rsv_time = 14;
		}
		break;
	}
}

/*
 * Description: Set RSPINF
 *
 * Parameters:
 *  In:
 *      pDevice             - The adapter to be set
 *  Out:
 *      none
 *
 * Return Value: None.
 *
 */

void vnt_set_rspinf(struct vnt_private *priv, u8 bb_type)
{
	struct vnt_phy_field phy[4];
	u8 tx_rate[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0}; /* For OFDM */
	u8 rsv_time[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
	u8 data[34];
	int i;

	/*RSPINF_b_1*/
	vnt_get_phy_field(priv, 14,
		vnt_get_cck_rate(priv, RATE_1M), PK_TYPE_11B, &phy[0]);

	/*RSPINF_b_2*/
	vnt_get_phy_field(priv, 14,
		vnt_get_cck_rate(priv, RATE_2M), PK_TYPE_11B, &phy[1]);

	/*RSPINF_b_5*/
	vnt_get_phy_field(priv, 14,
		vnt_get_cck_rate(priv, RATE_5M), PK_TYPE_11B, &phy[2]);

	/*RSPINF_b_11*/
	vnt_get_phy_field(priv, 14,
		vnt_get_cck_rate(priv, RATE_11M), PK_TYPE_11B, &phy[3]);

	/*RSPINF_a_6*/
	vnt_calculate_ofdm_rate(RATE_6M, bb_type, &tx_rate[0], &rsv_time[0]);

	/*RSPINF_a_9*/
	vnt_calculate_ofdm_rate(RATE_9M, bb_type, &tx_rate[1], &rsv_time[1]);

	/*RSPINF_a_12*/
	vnt_calculate_ofdm_rate(RATE_12M, bb_type, &tx_rate[2], &rsv_time[2]);

	/*RSPINF_a_18*/
	vnt_calculate_ofdm_rate(RATE_18M, bb_type, &tx_rate[3], &rsv_time[3]);

	/*RSPINF_a_24*/
	vnt_calculate_ofdm_rate(RATE_24M, bb_type, &tx_rate[4], &rsv_time[4]);

	/*RSPINF_a_36*/
	vnt_calculate_ofdm_rate(vnt_get_ofdm_rate(priv, RATE_36M),
					bb_type, &tx_rate[5], &rsv_time[5]);

	/*RSPINF_a_48*/
	vnt_calculate_ofdm_rate(vnt_get_ofdm_rate(priv, RATE_48M),
					bb_type, &tx_rate[6], &rsv_time[6]);

	/*RSPINF_a_54*/
	vnt_calculate_ofdm_rate(vnt_get_ofdm_rate(priv, RATE_54M),
					bb_type, &tx_rate[7], &rsv_time[7]);

	/*RSPINF_a_72*/
	vnt_calculate_ofdm_rate(vnt_get_ofdm_rate(priv, RATE_54M),
					bb_type, &tx_rate[8], &rsv_time[8]);

	put_unaligned(phy[0].len, (u16 *)&data[0]);
	data[2] = phy[0].signal;
	data[3] = phy[0].service;

	put_unaligned(phy[1].len, (u16 *)&data[4]);
	data[6] = phy[1].signal;
	data[7] = phy[1].service;

	put_unaligned(phy[2].len, (u16 *)&data[8]);
	data[10] = phy[2].signal;
	data[11] = phy[2].service;

	put_unaligned(phy[3].len, (u16 *)&data[12]);
	data[14] = phy[3].signal;
	data[15] = phy[3].service;

	for (i = 0; i < 9; i++) {
		data[16 + i * 2] = tx_rate[i];
		data[16 + i * 2 + 1] = rsv_time[i];
	}

	vnt_control_out(priv, MESSAGE_TYPE_WRITE,
		MAC_REG_RSPINF_B_1, MESSAGE_REQUEST_MACREG, 34, &data[0]);
}

/*
 * Description: Update IFS
 *
 * Parameters:
 *  In:
 *	priv - The adapter to be set
 * Out:
 *	none
 *
 * Return Value: None.
 *
 */
void vnt_update_ifs(struct vnt_private *priv)
{
	u8 max_min = 0;
	u8 data[4];

	if (priv->packet_type == PK_TYPE_11A) {
		priv->slot = C_SLOT_SHORT;
		priv->sifs = C_SIFS_A;
		priv->difs = C_SIFS_A + 2 * C_SLOT_SHORT;
		max_min = 4;
	} else if (priv->packet_type == PK_TYPE_11B) {
		priv->slot = C_SLOT_LONG;
		priv->sifs = C_SIFS_BG;
		priv->difs = C_SIFS_BG + 2 * C_SLOT_LONG;
		max_min = 5;
	} else {/* PK_TYPE_11GA & PK_TYPE_11GB */
		bool ofdm_rate = false;
		unsigned int ii = 0;

		priv->sifs = C_SIFS_BG;

		if (priv->short_slot_time)
			priv->slot = C_SLOT_SHORT;
		else
			priv->slot = C_SLOT_LONG;

		priv->difs = C_SIFS_BG + 2 * priv->slot;

		for (ii = RATE_54M; ii >= RATE_6M; ii--) {
			if (priv->basic_rates & ((u32)(0x1 << ii))) {
				ofdm_rate = true;
				break;
			}
		}

		if (ofdm_rate)
			max_min = 4;
		else
			max_min = 5;
	}

	priv->eifs = C_EIFS;

	switch (priv->rf_type) {
	case RF_VT3226D0:
		if (priv->bb_type != BB_TYPE_11B) {
			priv->sifs -= 1;
			priv->difs -= 1;
			break;
		}
	case RF_AIROHA7230:
	case RF_AL2230:
	case RF_AL2230S:
		if (priv->bb_type != BB_TYPE_11B)
			break;
	case RF_RFMD2959:
	case RF_VT3226:
	case RF_VT3342A0:
		priv->sifs -= 3;
		priv->difs -= 3;
		break;
	case RF_MAXIM2829:
		if (priv->bb_type == BB_TYPE_11A) {
			priv->sifs -= 5;
			priv->difs -= 5;
		} else {
			priv->sifs -= 2;
			priv->difs -= 2;
		}

		break;
	}

	data[0] = (u8)priv->sifs;
	data[1] = (u8)priv->difs;
	data[2] = (u8)priv->eifs;
	data[3] = (u8)priv->slot;

	vnt_control_out(priv, MESSAGE_TYPE_WRITE, MAC_REG_SIFS,
		MESSAGE_REQUEST_MACREG, 4, &data[0]);

	max_min |= 0xa0;

	vnt_control_out(priv, MESSAGE_TYPE_WRITE, MAC_REG_CWMAXMIN0,
		MESSAGE_REQUEST_MACREG, 1, &max_min);
}

void vnt_update_top_rates(struct vnt_private *priv)
{
	u8 top_ofdm = RATE_24M, top_cck = RATE_1M;
	u8 i;

	/*Determines the highest basic rate.*/
	for (i = RATE_54M; i >= RATE_6M; i--) {
		if (priv->basic_rates & (u16)(1 << i)) {
			top_ofdm = i;
			break;
		}
	}

	priv->top_ofdm_basic_rate = top_ofdm;

	for (i = RATE_11M;; i--) {
		if (priv->basic_rates & (u16)(1 << i)) {
			top_cck = i;
			break;
		}
		if (i == RATE_1M)
			break;
	}

	priv->top_cck_basic_rate = top_cck;
}

int vnt_ofdm_min_rate(struct vnt_private *priv)
{
	int ii;

	for (ii = RATE_54M; ii >= RATE_6M; ii--) {
		if ((priv->basic_rates) & ((u16)BIT(ii)))
			return true;
	}

	return false;
}

u8 vnt_get_pkt_type(struct vnt_private *priv)
{

	if (priv->bb_type == BB_TYPE_11A || priv->bb_type == BB_TYPE_11B)
		return (u8)priv->bb_type;
	else if (vnt_ofdm_min_rate(priv))
		return PK_TYPE_11GA;
	return PK_TYPE_11GB;
}

/*
 * Description: Calculate TSF offset of two TSF input
 *              Get TSF Offset from RxBCN's TSF and local TSF
 *
 * Parameters:
 *  In:
 *      rx_rate	- rx rate.
 *      tsf1	- Rx BCN's TSF
 *      tsf2	- Local TSF
 *  Out:
 *      none
 *
 * Return Value: TSF Offset value
 *
 */
u64 vnt_get_tsf_offset(u8 rx_rate, u64 tsf1, u64 tsf2)
{
	u64 tsf_offset = 0;
	u16 rx_bcn_offset;

	rx_bcn_offset = cwRXBCNTSFOff[rx_rate % MAX_RATE];

	tsf2 += (u64)rx_bcn_offset;

	tsf_offset = tsf1 - tsf2;

	return tsf_offset;
}

/*
 * Description: Sync. TSF counter to BSS
 *              Get TSF offset and write to HW
 *
 * Parameters:
 *  In:
 *      priv		- The adapter to be sync.
 *      time_stamp	- Rx BCN's TSF
 *      local_tsf	- Local TSF
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void vnt_adjust_tsf(struct vnt_private *priv, u8 rx_rate,
		u64 time_stamp, u64 local_tsf)
{
	u64 tsf_offset = 0;
	u8 data[8];

	tsf_offset = vnt_get_tsf_offset(rx_rate, time_stamp, local_tsf);

	data[0] = (u8)tsf_offset;
	data[1] = (u8)(tsf_offset >> 8);
	data[2] = (u8)(tsf_offset >> 16);
	data[3] = (u8)(tsf_offset >> 24);
	data[4] = (u8)(tsf_offset >> 32);
	data[5] = (u8)(tsf_offset >> 40);
	data[6] = (u8)(tsf_offset >> 48);
	data[7] = (u8)(tsf_offset >> 56);

	vnt_control_out(priv, MESSAGE_TYPE_SET_TSFTBTT,
		MESSAGE_REQUEST_TSF, 0, 8, data);
}
/*
 * Description: Read NIC TSF counter
 *              Get local TSF counter
 *
 * Parameters:
 *  In:
 *	priv		- The adapter to be read
 *  Out:
 *	current_tsf	- Current TSF counter
 *
 * Return Value: true if success; otherwise false
 *
 */
bool vnt_get_current_tsf(struct vnt_private *priv, u64 *current_tsf)
{

	*current_tsf = priv->current_tsf;

	return true;
}

/*
 * Description: Clear NIC TSF counter
 *              Clear local TSF counter
 *
 * Parameters:
 *  In:
 *      priv	- The adapter to be read
 *
 * Return Value: true if success; otherwise false
 *
 */
bool vnt_clear_current_tsf(struct vnt_private *priv)
{

	vnt_mac_reg_bits_on(priv, MAC_REG_TFTCTL, TFTCTL_TSFCNTRST);

	priv->current_tsf = 0;

	return true;
}

/*
 * Description: Read NIC TSF counter
 *              Get NEXTTBTT from adjusted TSF and Beacon Interval
 *
 * Parameters:
 *  In:
 *      tsf		- Current TSF counter
 *      beacon_interval - Beacon Interval
 *  Out:
 *      tsf		- Current TSF counter
 *
 * Return Value: TSF value of next Beacon
 *
 */
u64 vnt_get_next_tbtt(u64 tsf, u16 beacon_interval)
{
	u32 beacon_int;

	beacon_int = beacon_interval * 1024;

	/* Next TBTT =
	*	((local_current_TSF / beacon_interval) + 1) * beacon_interval
	*/
	if (beacon_int) {
		do_div(tsf, beacon_int);
		tsf += 1;
		tsf *= beacon_int;
	}

	return tsf;
}

/*
 * Description: Set NIC TSF counter for first Beacon time
 *              Get NEXTTBTT from adjusted TSF and Beacon Interval
 *
 * Parameters:
 *  In:
 *      dwIoBase        - IO Base
 *	beacon_interval - Beacon Interval
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void vnt_reset_next_tbtt(struct vnt_private *priv, u16 beacon_interval)
{
	u64 next_tbtt = 0;
	u8 data[8];

	vnt_clear_current_tsf(priv);

	next_tbtt = vnt_get_next_tbtt(next_tbtt, beacon_interval);

	data[0] = (u8)next_tbtt;
	data[1] = (u8)(next_tbtt >> 8);
	data[2] = (u8)(next_tbtt >> 16);
	data[3] = (u8)(next_tbtt >> 24);
	data[4] = (u8)(next_tbtt >> 32);
	data[5] = (u8)(next_tbtt >> 40);
	data[6] = (u8)(next_tbtt >> 48);
	data[7] = (u8)(next_tbtt >> 56);

	vnt_control_out(priv, MESSAGE_TYPE_SET_TSFTBTT,
		MESSAGE_REQUEST_TBTT, 0, 8, data);
}

/*
 * Description: Sync NIC TSF counter for Beacon time
 *              Get NEXTTBTT and write to HW
 *
 * Parameters:
 *  In:
 *	priv		- The adapter to be set
 *      tsf		- Current TSF counter
 *      beacon_interval - Beacon Interval
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void vnt_update_next_tbtt(struct vnt_private *priv, u64 tsf,
			u16 beacon_interval)
{
	u8 data[8];

	tsf = vnt_get_next_tbtt(tsf, beacon_interval);

	data[0] = (u8)tsf;
	data[1] = (u8)(tsf >> 8);
	data[2] = (u8)(tsf >> 16);
	data[3] = (u8)(tsf >> 24);
	data[4] = (u8)(tsf >> 32);
	data[5] = (u8)(tsf >> 40);
	data[6] = (u8)(tsf >> 48);
	data[7] = (u8)(tsf >> 56);

	vnt_control_out(priv, MESSAGE_TYPE_SET_TSFTBTT,
			MESSAGE_REQUEST_TBTT, 0, 8, data);

	dev_dbg(&priv->usb->dev, "%s TBTT: %8llx\n", __func__, tsf);
}

/*
 * Description: Turn off Radio power
 *
 * Parameters:
 *  In:
 *      priv         - The adapter to be turned off
 *  Out:
 *      none
 *
 * Return Value: true if success; otherwise false
 *
 */
int vnt_radio_power_off(struct vnt_private *priv)
{
	int ret = true;

	switch (priv->rf_type) {
	case RF_AL2230:
	case RF_AL2230S:
	case RF_AIROHA7230:
	case RF_VT3226:
	case RF_VT3226D0:
	case RF_VT3342A0:
		vnt_mac_reg_bits_off(priv, MAC_REG_SOFTPWRCTL,
				(SOFTPWRCTL_SWPE2 | SOFTPWRCTL_SWPE3));
		break;
	}

	vnt_mac_reg_bits_off(priv, MAC_REG_HOSTCR, HOSTCR_RXON);

	vnt_set_deep_sleep(priv);

	vnt_mac_reg_bits_on(priv, MAC_REG_GPIOCTL1, GPIO3_INTMD);

	return ret;
}

/*
 * Description: Turn on Radio power
 *
 * Parameters:
 *  In:
 *      priv         - The adapter to be turned on
 *  Out:
 *      none
 *
 * Return Value: true if success; otherwise false
 *
 */
int vnt_radio_power_on(struct vnt_private *priv)
{
	int ret = true;

	vnt_exit_deep_sleep(priv);

	vnt_mac_reg_bits_on(priv, MAC_REG_HOSTCR, HOSTCR_RXON);

	switch (priv->rf_type) {
	case RF_AL2230:
	case RF_AL2230S:
	case RF_AIROHA7230:
	case RF_VT3226:
	case RF_VT3226D0:
	case RF_VT3342A0:
		vnt_mac_reg_bits_on(priv, MAC_REG_SOFTPWRCTL,
			(SOFTPWRCTL_SWPE2 | SOFTPWRCTL_SWPE3));
		break;
	}

	vnt_mac_reg_bits_off(priv, MAC_REG_GPIOCTL1, GPIO3_INTMD);

	return ret;
}

void vnt_set_bss_mode(struct vnt_private *priv)
{
	if (priv->rf_type == RF_AIROHA7230 && priv->bb_type == BB_TYPE_11A)
		vnt_mac_set_bb_type(priv, BB_TYPE_11G);
	else
		vnt_mac_set_bb_type(priv, priv->bb_type);

	priv->packet_type = vnt_get_pkt_type(priv);

	if (priv->bb_type == BB_TYPE_11A)
		vnt_control_out_u8(priv, MESSAGE_REQUEST_BBREG, 0x88, 0x03);
	else if (priv->bb_type == BB_TYPE_11B)
		vnt_control_out_u8(priv, MESSAGE_REQUEST_BBREG, 0x88, 0x02);
	else if (priv->bb_type == BB_TYPE_11G)
		vnt_control_out_u8(priv, MESSAGE_REQUEST_BBREG, 0x88, 0x08);

	vnt_update_ifs(priv);
	vnt_set_rspinf(priv, (u8)priv->bb_type);

	if (priv->bb_type == BB_TYPE_11A) {
		if (priv->rf_type == RF_AIROHA7230) {
			priv->bb_vga[0] = 0x20;

			vnt_control_out_u8(priv, MESSAGE_REQUEST_BBREG,
						0xe7, priv->bb_vga[0]);
		}

		priv->bb_vga[2] = 0x10;
		priv->bb_vga[3] = 0x10;
	} else {
		if (priv->rf_type == RF_AIROHA7230) {
			priv->bb_vga[0] = 0x1c;

			vnt_control_out_u8(priv, MESSAGE_REQUEST_BBREG,
						0xe7, priv->bb_vga[0]);
		}

		priv->bb_vga[2] = 0x0;
		priv->bb_vga[3] = 0x0;
	}

	vnt_set_vga_gain_offset(priv, priv->bb_vga[0]);
}
