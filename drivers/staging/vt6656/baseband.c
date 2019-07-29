// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * File: baseband.c
 *
 * Purpose: Implement functions to access baseband
 *
 * Author: Jerry Chen
 *
 * Date: Jun. 5, 2002
 *
 * Functions:
 *	vnt_get_frame_time	- Calculate data frame transmitting time
 *	vnt_get_phy_field	- Calculate PhyLength, PhyService and Phy
 *				  Signal parameter for baseband Tx
 *	vnt_vt3184_init		- VIA VT3184 baseband chip init code
 *
 * Revision History:
 *
 *
 */

#include "mac.h"
#include "baseband.h"
#include "rf.h"
#include "usbpipe.h"

static u8 vnt_vt3184_agc[] = {
	0x00, 0x00, 0x02, 0x02, 0x04, 0x04, 0x06, 0x06,
	0x08, 0x08, 0x0a, 0x0a, 0x0c, 0x0c, 0x0e, 0x0e, /* 0x0f */
	0x10, 0x10, 0x12, 0x12, 0x14, 0x14, 0x16, 0x16,
	0x18, 0x18, 0x1a, 0x1a, 0x1c, 0x1c, 0x1e, 0x1e, /* 0x1f */
	0x20, 0x20, 0x22, 0x22, 0x24, 0x24, 0x26, 0x26,
	0x28, 0x28, 0x2a, 0x2a, 0x2c, 0x2c, 0x2e, 0x2e, /* 0x2f */
	0x30, 0x30, 0x32, 0x32, 0x34, 0x34, 0x36, 0x36,
	0x38, 0x38, 0x3a, 0x3a, 0x3c, 0x3c, 0x3e, 0x3e  /* 0x3f */
};

static u8 vnt_vt3184_al2230[] = {
	0x31, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
	0x70, 0x45, 0x2a, 0x76, 0x00, 0x00, 0x80, 0x00, /* 0x0f */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x8e, 0x0a, 0x00, 0x00, 0x00, /* 0x1f */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x4a, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x4a, 0x00, 0x0c, /* 0x2f */
	0x26, 0x5b, 0x00, 0x00, 0x00, 0x00, 0xaa, 0xaa,
	0xff, 0xff, 0x79, 0x00, 0x00, 0x0b, 0x48, 0x04, /* 0x3f */
	0x00, 0x08, 0x00, 0x08, 0x08, 0x14, 0x05, 0x09,
	0x00, 0x00, 0x00, 0x00, 0x09, 0x73, 0x00, 0xc5, /* 0x4f */
	0x00, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0xd0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x5f */
	0xe4, 0x80, 0x00, 0x00, 0x00, 0x00, 0x98, 0x0a,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x01, 0x00, /* 0x6f */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x7f */
	0x8c, 0x01, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x08, 0x00, 0x1f, 0xb7, 0x88, 0x47, 0xaa, 0x00, /* 0x8f */
	0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xeb,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, /* 0x9f */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
	0x18, 0x00, 0x00, 0x00, 0x00, 0x15, 0x00, 0x18, /* 0xaf */
	0x38, 0x30, 0x00, 0x00, 0xff, 0x0f, 0xe4, 0xe2,
	0x00, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00, /* 0xbf */
	0x18, 0x20, 0x07, 0x18, 0xff, 0xff, 0x0e, 0x0a,
	0x0e, 0x00, 0x82, 0xa7, 0x3c, 0x10, 0x30, 0x05, /* 0xcf */
	0x40, 0x12, 0x00, 0x00, 0x10, 0x28, 0x80, 0x2a,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xdf */
	0x00, 0xf3, 0x00, 0x00, 0x00, 0x10, 0x00, 0x12,
	0x00, 0xf4, 0x00, 0xff, 0x79, 0x20, 0x30, 0x05, /* 0xef */
	0x00, 0x3e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  /* 0xff */
};

/* {{RobertYu:20060515, new BB setting for VT3226D0 */
static u8 vnt_vt3184_vt3226d0[] = {
	0x31, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
	0x70, 0x45, 0x2a, 0x76, 0x00, 0x00, 0x80, 0x00, /* 0x0f */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x8e, 0x0a, 0x00, 0x00, 0x00, /* 0x1f */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x4a, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x4a, 0x00, 0x0c, /* 0x2f */
	0x26, 0x5b, 0x00, 0x00, 0x00, 0x00, 0xaa, 0xaa,
	0xff, 0xff, 0x79, 0x00, 0x00, 0x0b, 0x48, 0x04, /* 0x3f */
	0x00, 0x08, 0x00, 0x08, 0x08, 0x14, 0x05, 0x09,
	0x00, 0x00, 0x00, 0x00, 0x09, 0x73, 0x00, 0xc5, /* 0x4f */
	0x00, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0xd0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x5f */
	0xe4, 0x80, 0x00, 0x00, 0x00, 0x00, 0x98, 0x0a,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x01, 0x00, /* 0x6f */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x7f */
	0x8c, 0x01, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x08, 0x00, 0x1f, 0xb7, 0x88, 0x47, 0xaa, 0x00, /* 0x8f */
	0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xeb,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, /* 0x9f */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
	0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, /* 0xaf */
	0x38, 0x30, 0x00, 0x00, 0xff, 0x0f, 0xe4, 0xe2,
	0x00, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00, /* 0xbf */
	0x18, 0x20, 0x07, 0x18, 0xff, 0xff, 0x10, 0x0a,
	0x0e, 0x00, 0x84, 0xa7, 0x3c, 0x10, 0x24, 0x05, /* 0xcf */
	0x40, 0x12, 0x00, 0x00, 0x10, 0x28, 0x80, 0x2a,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xdf */
	0x00, 0xf3, 0x00, 0x00, 0x00, 0x10, 0x00, 0x10,
	0x00, 0xf4, 0x00, 0xff, 0x79, 0x20, 0x30, 0x08, /* 0xef */
	0x00, 0x3e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  /* 0xff */
};

static const u16 vnt_frame_time[MAX_RATE] = {
	10, 20, 55, 110, 24, 36, 48, 72, 96, 144, 192, 216
};

/*
 * Description: Calculate data frame transmitting time
 *
 * Parameters:
 *  In:
 *	preamble_type	- Preamble Type
 *	pkt_type	- PK_TYPE_11A, PK_TYPE_11B, PK_TYPE_11GB, PK_TYPE_11GA
 *	frame_length	- Baseband Type
 *	tx_rate		- Tx Rate
 *  Out:
 *
 * Return Value: FrameTime
 *
 */
unsigned int vnt_get_frame_time(u8 preamble_type, u8 pkt_type,
				unsigned int frame_length, u16 tx_rate)
{
	unsigned int frame_time;
	unsigned int preamble;
	unsigned int tmp;
	unsigned int rate = 0;

	if (tx_rate > RATE_54M)
		return 0;

	rate = (unsigned int)vnt_frame_time[tx_rate];

	if (tx_rate <= 3) {
		if (preamble_type == 1)
			preamble = 96;
		else
			preamble = 192;

		frame_time = (frame_length * 80) / rate;
		tmp = (frame_time * rate) / 80;

		if (frame_length != tmp)
			frame_time++;

		return preamble + frame_time;
	}
	frame_time = (frame_length * 8 + 22) / rate;
	tmp = ((frame_time * rate) - 22) / 8;

	if (frame_length != tmp)
		frame_time++;

	frame_time = frame_time * 4;

	if (pkt_type != PK_TYPE_11A)
		frame_time += 6;
	return 20 + frame_time;
}

/*
 * Description: Calculate Length, Service, and Signal fields of Phy for Tx
 *
 * Parameters:
 *  In:
 *      priv         - Device Structure
 *      frame_length   - Tx Frame Length
 *      tx_rate           - Tx Rate
 *  Out:
 *	struct vnt_phy_field *phy
 *		- pointer to Phy Length field
 *		- pointer to Phy Service field
 *		- pointer to Phy Signal field
 *
 * Return Value: none
 *
 */
void vnt_get_phy_field(struct vnt_private *priv, u32 frame_length,
		       u16 tx_rate, u8 pkt_type, struct vnt_phy_field *phy)
{
	u32 bit_count;
	u32 count = 0;
	u32 tmp;
	int ext_bit;
	u8 preamble_type = priv->preamble_type;

	bit_count = frame_length * 8;
	ext_bit = false;

	switch (tx_rate) {
	case RATE_1M:
		count = bit_count;

		phy->signal = 0x00;

		break;
	case RATE_2M:
		count = bit_count / 2;

		if (preamble_type == 1)
			phy->signal = 0x09;
		else
			phy->signal = 0x01;

		break;
	case RATE_5M:
		count = (bit_count * 10) / 55;
		tmp = (count * 55) / 10;

		if (tmp != bit_count)
			count++;

		if (preamble_type == 1)
			phy->signal = 0x0a;
		else
			phy->signal = 0x02;

		break;
	case RATE_11M:
		count = bit_count / 11;
		tmp = count * 11;

		if (tmp != bit_count) {
			count++;

			if ((bit_count - tmp) <= 3)
				ext_bit = true;
		}

		if (preamble_type == 1)
			phy->signal = 0x0b;
		else
			phy->signal = 0x03;

		break;
	case RATE_6M:
		if (pkt_type == PK_TYPE_11A)
			phy->signal = 0x9b;
		else
			phy->signal = 0x8b;

		break;
	case RATE_9M:
		if (pkt_type == PK_TYPE_11A)
			phy->signal = 0x9f;
		else
			phy->signal = 0x8f;

		break;
	case RATE_12M:
		if (pkt_type == PK_TYPE_11A)
			phy->signal = 0x9a;
		else
			phy->signal = 0x8a;

		break;
	case RATE_18M:
		if (pkt_type == PK_TYPE_11A)
			phy->signal = 0x9e;
		else
			phy->signal = 0x8e;

		break;
	case RATE_24M:
		if (pkt_type == PK_TYPE_11A)
			phy->signal = 0x99;
		else
			phy->signal = 0x89;

		break;
	case RATE_36M:
		if (pkt_type == PK_TYPE_11A)
			phy->signal = 0x9d;
		else
			phy->signal = 0x8d;

		break;
	case RATE_48M:
		if (pkt_type == PK_TYPE_11A)
			phy->signal = 0x98;
		else
			phy->signal = 0x88;

		break;
	case RATE_54M:
		if (pkt_type == PK_TYPE_11A)
			phy->signal = 0x9c;
		else
			phy->signal = 0x8c;
		break;
	default:
		if (pkt_type == PK_TYPE_11A)
			phy->signal = 0x9c;
		else
			phy->signal = 0x8c;
		break;
	}

	if (pkt_type == PK_TYPE_11B) {
		phy->service = 0x00;
		if (ext_bit)
			phy->service |= 0x80;
		phy->len = cpu_to_le16((u16)count);
	} else {
		phy->service = 0x00;
		phy->len = cpu_to_le16((u16)frame_length);
	}
}

/*
 * Description: Set Antenna mode
 *
 * Parameters:
 *  In:
 *	priv		- Device Structure
 *	antenna_mode	- Antenna Mode
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
int vnt_set_antenna_mode(struct vnt_private *priv, u8 antenna_mode)
{
	switch (antenna_mode) {
	case ANT_TXA:
	case ANT_TXB:
		break;
	case ANT_RXA:
		priv->bb_rx_conf &= 0xFC;
		break;
	case ANT_RXB:
		priv->bb_rx_conf &= 0xFE;
		priv->bb_rx_conf |= 0x02;
		break;
	}

	return vnt_control_out(priv, MESSAGE_TYPE_SET_ANTMD,
			       (u16)antenna_mode, 0, 0, NULL);
}

/*
 * Description: Set Antenna mode
 *
 * Parameters:
 *  In:
 *      pDevice          - Device Structure
 *      byAntennaMode    - Antenna Mode
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */

int vnt_vt3184_init(struct vnt_private *priv)
{
	int ret = 0;
	u16 length;
	u8 *addr;
	u8 *agc;
	u16 length_agc;
	u8 array[256];
	u8 data;

	ret = vnt_control_in(priv, MESSAGE_TYPE_READ, 0, MESSAGE_REQUEST_EEPROM,
			     EEP_MAX_CONTEXT_SIZE, priv->eeprom);
	if (ret)
		goto end;

	priv->rf_type = priv->eeprom[EEP_OFS_RFTYPE];

	dev_dbg(&priv->usb->dev, "RF Type %d\n", priv->rf_type);

	if ((priv->rf_type == RF_AL2230) ||
	    (priv->rf_type == RF_AL2230S)) {
		priv->bb_rx_conf = vnt_vt3184_al2230[10];
		length = sizeof(vnt_vt3184_al2230);
		addr = vnt_vt3184_al2230;
		agc = vnt_vt3184_agc;
		length_agc = sizeof(vnt_vt3184_agc);

		priv->bb_vga[0] = 0x1C;
		priv->bb_vga[1] = 0x10;
		priv->bb_vga[2] = 0x0;
		priv->bb_vga[3] = 0x0;

	} else if (priv->rf_type == RF_AIROHA7230) {
		priv->bb_rx_conf = vnt_vt3184_al2230[10];
		length = sizeof(vnt_vt3184_al2230);
		addr = vnt_vt3184_al2230;
		agc = vnt_vt3184_agc;
		length_agc = sizeof(vnt_vt3184_agc);

		addr[0xd7] = 0x06;

		priv->bb_vga[0] = 0x1c;
		priv->bb_vga[1] = 0x10;
		priv->bb_vga[2] = 0x0;
		priv->bb_vga[3] = 0x0;

	} else if ((priv->rf_type == RF_VT3226) ||
			(priv->rf_type == RF_VT3226D0)) {
		priv->bb_rx_conf = vnt_vt3184_vt3226d0[10];
		length = sizeof(vnt_vt3184_vt3226d0);
		addr = vnt_vt3184_vt3226d0;
		agc = vnt_vt3184_agc;
		length_agc = sizeof(vnt_vt3184_agc);

		priv->bb_vga[0] = 0x20;
		priv->bb_vga[1] = 0x10;
		priv->bb_vga[2] = 0x0;
		priv->bb_vga[3] = 0x0;

		/* Fix VT3226 DFC system timing issue */
		ret = vnt_mac_reg_bits_on(priv, MAC_REG_SOFTPWRCTL2,
					  SOFTPWRCTL_RFLEOPT);
		if (ret)
			goto end;
	} else if (priv->rf_type == RF_VT3342A0) {
		priv->bb_rx_conf = vnt_vt3184_vt3226d0[10];
		length = sizeof(vnt_vt3184_vt3226d0);
		addr = vnt_vt3184_vt3226d0;
		agc = vnt_vt3184_agc;
		length_agc = sizeof(vnt_vt3184_agc);

		priv->bb_vga[0] = 0x20;
		priv->bb_vga[1] = 0x10;
		priv->bb_vga[2] = 0x0;
		priv->bb_vga[3] = 0x0;

		/* Fix VT3226 DFC system timing issue */
		ret = vnt_mac_reg_bits_on(priv, MAC_REG_SOFTPWRCTL2,
					  SOFTPWRCTL_RFLEOPT);
		if (ret)
			goto end;
	} else {
		goto end;
	}

	memcpy(array, addr, length);

	ret = vnt_control_out(priv, MESSAGE_TYPE_WRITE, 0,
			      MESSAGE_REQUEST_BBREG, length, array);
	if (ret)
		goto end;

	memcpy(array, agc, length_agc);

	ret = vnt_control_out(priv, MESSAGE_TYPE_WRITE, 0,
			      MESSAGE_REQUEST_BBAGC, length_agc, array);
	if (ret)
		goto end;

	if ((priv->rf_type == RF_VT3226) ||
	    (priv->rf_type == RF_VT3342A0)) {
		ret = vnt_control_out_u8(priv, MESSAGE_REQUEST_MACREG,
					 MAC_REG_ITRTMSET, 0x23);
		if (ret)
			goto end;

		ret = vnt_mac_reg_bits_on(priv, MAC_REG_PAPEDELAY, 0x01);
		if (ret)
			goto end;
	} else if (priv->rf_type == RF_VT3226D0) {
		ret = vnt_control_out_u8(priv, MESSAGE_REQUEST_MACREG,
					 MAC_REG_ITRTMSET, 0x11);
		if (ret)
			goto end;

		ret = vnt_mac_reg_bits_on(priv, MAC_REG_PAPEDELAY, 0x01);
		if (ret)
			goto end;
	}

	ret = vnt_control_out_u8(priv, MESSAGE_REQUEST_BBREG, 0x04, 0x7f);
	if (ret)
		goto end;

	ret = vnt_control_out_u8(priv, MESSAGE_REQUEST_BBREG, 0x0d, 0x01);
	if (ret)
		goto end;

	ret = vnt_rf_table_download(priv);
	if (ret)
		goto end;

	/* Fix for TX USB resets from vendors driver */
	ret = vnt_control_in(priv, MESSAGE_TYPE_READ, USB_REG4,
			     MESSAGE_REQUEST_MEM, sizeof(data), &data);
	if (ret)
		goto end;

	data |= 0x2;

	ret = vnt_control_out(priv, MESSAGE_TYPE_WRITE, USB_REG4,
			      MESSAGE_REQUEST_MEM, sizeof(data), &data);

end:
	return ret;
}

/*
 * Description: Set ShortSlotTime mode
 *
 * Parameters:
 *  In:
 *	priv	- Device Structure
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
int vnt_set_short_slot_time(struct vnt_private *priv)
{
	int ret = 0;
	u8 bb_vga = 0;

	if (priv->short_slot_time)
		priv->bb_rx_conf &= 0xdf;
	else
		priv->bb_rx_conf |= 0x20;

	ret = vnt_control_in_u8(priv, MESSAGE_REQUEST_BBREG, 0xe7, &bb_vga);
	if (ret)
		goto end;

	if (bb_vga == priv->bb_vga[0])
		priv->bb_rx_conf |= 0x20;

	ret = vnt_control_out_u8(priv, MESSAGE_REQUEST_BBREG, 0x0a,
				 priv->bb_rx_conf);

end:
	return ret;
}

void vnt_set_vga_gain_offset(struct vnt_private *priv, u8 data)
{
	vnt_control_out_u8(priv, MESSAGE_REQUEST_BBREG, 0xE7, data);

	/* patch for 3253B0 Baseband with Cardbus module */
	if (priv->short_slot_time)
		priv->bb_rx_conf &= 0xdf; /* 1101 1111 */
	else
		priv->bb_rx_conf |= 0x20; /* 0010 0000 */

	vnt_control_out_u8(priv, MESSAGE_REQUEST_BBREG, 0x0a, priv->bb_rx_conf);
}

/*
 * Description: vnt_set_deep_sleep
 *
 * Parameters:
 *  In:
 *	priv	- Device Structure
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
int vnt_set_deep_sleep(struct vnt_private *priv)
{
	int ret = 0;

	/* CR12 */
	ret = vnt_control_out_u8(priv, MESSAGE_REQUEST_BBREG, 0x0c, 0x17);
	if (ret)
		return ret;

	/* CR13 */
	return vnt_control_out_u8(priv, MESSAGE_REQUEST_BBREG, 0x0d, 0xB9);
}

int vnt_exit_deep_sleep(struct vnt_private *priv)
{
	int ret = 0;

	/* CR12 */
	ret = vnt_control_out_u8(priv, MESSAGE_REQUEST_BBREG, 0x0c, 0x00);
	if (ret)
		return ret;

	/* CR13 */
	return vnt_control_out_u8(priv, MESSAGE_REQUEST_BBREG, 0x0d, 0x01);
}

void vnt_update_pre_ed_threshold(struct vnt_private *priv, int scanning)
{
	u8 cr_201 = 0x0, cr_206 = 0x0;
	u8 ed_inx = priv->bb_pre_ed_index;

	switch (priv->rf_type) {
	case RF_AL2230:
	case RF_AL2230S:
	case RF_AIROHA7230:
		if (scanning) { /* Max sensitivity */
			ed_inx = 0;
			cr_206 = 0x30;
			break;
		}

		if (priv->bb_pre_ed_rssi <= 45) {
			ed_inx = 20;
			cr_201 = 0xff;
		} else if (priv->bb_pre_ed_rssi <= 46) {
			ed_inx = 19;
			cr_201 = 0x1a;
		} else if (priv->bb_pre_ed_rssi <= 47) {
			ed_inx = 18;
			cr_201 = 0x15;
		} else if (priv->bb_pre_ed_rssi <= 49) {
			ed_inx = 17;
			cr_201 = 0xe;
		} else if (priv->bb_pre_ed_rssi <= 51) {
			ed_inx = 16;
			cr_201 = 0x9;
		} else if (priv->bb_pre_ed_rssi <= 53) {
			ed_inx = 15;
			cr_201 = 0x6;
		} else if (priv->bb_pre_ed_rssi <= 55) {
			ed_inx = 14;
			cr_201 = 0x3;
		} else if (priv->bb_pre_ed_rssi <= 56) {
			ed_inx = 13;
			cr_201 = 0x2;
			cr_206 = 0xa0;
		} else if (priv->bb_pre_ed_rssi <= 57) {
			ed_inx = 12;
			cr_201 = 0x2;
			cr_206 = 0x20;
		} else if (priv->bb_pre_ed_rssi <= 58) {
			ed_inx = 11;
			cr_201 = 0x1;
			cr_206 = 0xa0;
		} else if (priv->bb_pre_ed_rssi <= 59) {
			ed_inx = 10;
			cr_201 = 0x1;
			cr_206 = 0x54;
		} else if (priv->bb_pre_ed_rssi <= 60) {
			ed_inx = 9;
			cr_201 = 0x1;
			cr_206 = 0x18;
		} else if (priv->bb_pre_ed_rssi <= 61) {
			ed_inx = 8;
			cr_206 = 0xe3;
		} else if (priv->bb_pre_ed_rssi <= 62) {
			ed_inx = 7;
			cr_206 = 0xb9;
		} else if (priv->bb_pre_ed_rssi <= 63) {
			ed_inx = 6;
			cr_206 = 0x93;
		} else if (priv->bb_pre_ed_rssi <= 64) {
			ed_inx = 5;
			cr_206 = 0x79;
		} else if (priv->bb_pre_ed_rssi <= 65) {
			ed_inx = 4;
			cr_206 = 0x62;
		} else if (priv->bb_pre_ed_rssi <= 66) {
			ed_inx = 3;
			cr_206 = 0x51;
		} else if (priv->bb_pre_ed_rssi <= 67) {
			ed_inx = 2;
			cr_206 = 0x43;
		} else if (priv->bb_pre_ed_rssi <= 68) {
			ed_inx = 1;
			cr_206 = 0x36;
		} else {
			ed_inx = 0;
			cr_206 = 0x30;
		}
		break;

	case RF_VT3226:
	case RF_VT3226D0:
		if (scanning)	{ /* Max sensitivity */
			ed_inx = 0;
			cr_206 = 0x24;
			break;
		}

		if (priv->bb_pre_ed_rssi <= 41) {
			ed_inx = 22;
			cr_201 = 0xff;
		} else if (priv->bb_pre_ed_rssi <= 42) {
			ed_inx = 21;
			cr_201 = 0x36;
		} else if (priv->bb_pre_ed_rssi <= 43) {
			ed_inx = 20;
			cr_201 = 0x26;
		} else if (priv->bb_pre_ed_rssi <= 45) {
			ed_inx = 19;
			cr_201 = 0x18;
		} else if (priv->bb_pre_ed_rssi <= 47) {
			ed_inx = 18;
			cr_201 = 0x11;
		} else if (priv->bb_pre_ed_rssi <= 49) {
			ed_inx = 17;
			cr_201 = 0xa;
		} else if (priv->bb_pre_ed_rssi <= 51) {
			ed_inx = 16;
			cr_201 = 0x7;
		} else if (priv->bb_pre_ed_rssi <= 53) {
			ed_inx = 15;
			cr_201 = 0x4;
		} else if (priv->bb_pre_ed_rssi <= 55) {
			ed_inx = 14;
			cr_201 = 0x2;
			cr_206 = 0xc0;
		} else if (priv->bb_pre_ed_rssi <= 56) {
			ed_inx = 13;
			cr_201 = 0x2;
			cr_206 = 0x30;
		} else if (priv->bb_pre_ed_rssi <= 57) {
			ed_inx = 12;
			cr_201 = 0x1;
			cr_206 = 0xb0;
		} else if (priv->bb_pre_ed_rssi <= 58) {
			ed_inx = 11;
			cr_201 = 0x1;
			cr_206 = 0x70;
		} else if (priv->bb_pre_ed_rssi <= 59) {
			ed_inx = 10;
			cr_201 = 0x1;
			cr_206 = 0x30;
		} else if (priv->bb_pre_ed_rssi <= 60) {
			ed_inx = 9;
			cr_206 = 0xea;
		} else if (priv->bb_pre_ed_rssi <= 61) {
			ed_inx = 8;
			cr_206 = 0xc0;
		} else if (priv->bb_pre_ed_rssi <= 62) {
			ed_inx = 7;
			cr_206 = 0x9c;
		} else if (priv->bb_pre_ed_rssi <= 63) {
			ed_inx = 6;
			cr_206 = 0x80;
		} else if (priv->bb_pre_ed_rssi <= 64) {
			ed_inx = 5;
			cr_206 = 0x68;
		} else if (priv->bb_pre_ed_rssi <= 65) {
			ed_inx = 4;
			cr_206 = 0x52;
		} else if (priv->bb_pre_ed_rssi <= 66) {
			ed_inx = 3;
			cr_206 = 0x43;
		} else if (priv->bb_pre_ed_rssi <= 67) {
			ed_inx = 2;
			cr_206 = 0x36;
		} else if (priv->bb_pre_ed_rssi <= 68) {
			ed_inx = 1;
			cr_206 = 0x2d;
		} else {
			ed_inx = 0;
			cr_206 = 0x24;
		}
		break;

	case RF_VT3342A0:
		if (scanning) { /* need Max sensitivity */
			ed_inx = 0;
			cr_206 = 0x38;
			break;
		}

		if (priv->bb_pre_ed_rssi <= 41) {
			ed_inx = 20;
			cr_201 = 0xff;
		} else if (priv->bb_pre_ed_rssi <= 42) {
			ed_inx = 19;
			cr_201 = 0x36;
		} else if (priv->bb_pre_ed_rssi <= 43) {
			ed_inx = 18;
			cr_201 = 0x26;
		} else if (priv->bb_pre_ed_rssi <= 45) {
			ed_inx = 17;
			cr_201 = 0x18;
		} else if (priv->bb_pre_ed_rssi <= 47) {
			ed_inx = 16;
			cr_201 = 0x11;
		} else if (priv->bb_pre_ed_rssi <= 49) {
			ed_inx = 15;
			cr_201 = 0xa;
		} else if (priv->bb_pre_ed_rssi <= 51) {
			ed_inx = 14;
			cr_201 = 0x7;
		} else if (priv->bb_pre_ed_rssi <= 53) {
			ed_inx = 13;
			cr_201 = 0x4;
		} else if (priv->bb_pre_ed_rssi <= 55) {
			ed_inx = 12;
			cr_201 = 0x2;
			cr_206 = 0xc0;
		} else if (priv->bb_pre_ed_rssi <= 56) {
			ed_inx = 11;
			cr_201 = 0x2;
			cr_206 = 0x30;
		} else if (priv->bb_pre_ed_rssi <= 57) {
			ed_inx = 10;
			cr_201 = 0x1;
			cr_206 = 0xb0;
		} else if (priv->bb_pre_ed_rssi <= 58) {
			ed_inx = 9;
			cr_201 = 0x1;
			cr_206 = 0x70;
		} else if (priv->bb_pre_ed_rssi <= 59) {
			ed_inx = 8;
			cr_201 = 0x1;
			cr_206 = 0x30;
		} else if (priv->bb_pre_ed_rssi <= 60) {
			ed_inx = 7;
			cr_206 = 0xea;
		} else if (priv->bb_pre_ed_rssi <= 61) {
			ed_inx = 6;
			cr_206 = 0xc0;
		} else if (priv->bb_pre_ed_rssi <= 62) {
			ed_inx = 5;
			cr_206 = 0x9c;
		} else if (priv->bb_pre_ed_rssi <= 63) {
			ed_inx = 4;
			cr_206 = 0x80;
		} else if (priv->bb_pre_ed_rssi <= 64) {
			ed_inx = 3;
			cr_206 = 0x68;
		} else if (priv->bb_pre_ed_rssi <= 65) {
			ed_inx = 2;
			cr_206 = 0x52;
		} else if (priv->bb_pre_ed_rssi <= 66) {
			ed_inx = 1;
			cr_206 = 0x43;
		} else {
			ed_inx = 0;
			cr_206 = 0x38;
		}
		break;
	}

	if (ed_inx == priv->bb_pre_ed_index && !scanning)
		return;

	priv->bb_pre_ed_index = ed_inx;

	dev_dbg(&priv->usb->dev, "%s bb_pre_ed_rssi %d\n",
		__func__, priv->bb_pre_ed_rssi);

	if (!cr_201 && !cr_206)
		return;

	vnt_control_out_u8(priv, MESSAGE_REQUEST_BBREG, 0xc9, cr_201);
	vnt_control_out_u8(priv, MESSAGE_REQUEST_BBREG, 0xce, cr_206);
}

