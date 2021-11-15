// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries.
 * All rights reserved.
 */

#include <linux/bitfield.h>
#include "wlan_if.h"
#include "wlan.h"
#include "wlan_cfg.h"
#include "netdev.h"

enum cfg_cmd_type {
	CFG_BYTE_CMD	= 0,
	CFG_HWORD_CMD	= 1,
	CFG_WORD_CMD	= 2,
	CFG_STR_CMD	= 3,
	CFG_BIN_CMD	= 4
};

static const struct wilc_cfg_byte g_cfg_byte[] = {
	{WID_STATUS, 0},
	{WID_RSSI, 0},
	{WID_LINKSPEED, 0},
	{WID_WOWLAN_TRIGGER, 0},
	{WID_NIL, 0}
};

static const struct wilc_cfg_hword g_cfg_hword[] = {
	{WID_NIL, 0}
};

static const struct wilc_cfg_word g_cfg_word[] = {
	{WID_FAILED_COUNT, 0},
	{WID_RECEIVED_FRAGMENT_COUNT, 0},
	{WID_SUCCESS_FRAME_COUNT, 0},
	{WID_GET_INACTIVE_TIME, 0},
	{WID_NIL, 0}

};

static const struct wilc_cfg_str g_cfg_str[] = {
	{WID_FIRMWARE_VERSION, NULL},
	{WID_MAC_ADDR, NULL},
	{WID_ASSOC_RES_INFO, NULL},
	{WID_NIL, NULL}
};

#define WILC_RESP_MSG_TYPE_CONFIG_REPLY		'R'
#define WILC_RESP_MSG_TYPE_STATUS_INFO		'I'
#define WILC_RESP_MSG_TYPE_NETWORK_INFO		'N'
#define WILC_RESP_MSG_TYPE_SCAN_COMPLETE	'S'

/********************************************
 *
 *      Configuration Functions
 *
 ********************************************/

static int wilc_wlan_cfg_set_byte(u8 *frame, u32 offset, u16 id, u8 val8)
{
	if ((offset + 4) >= WILC_MAX_CFG_FRAME_SIZE)
		return 0;

	put_unaligned_le16(id, &frame[offset]);
	put_unaligned_le16(1, &frame[offset + 2]);
	frame[offset + 4] = val8;
	return 5;
}

static int wilc_wlan_cfg_set_hword(u8 *frame, u32 offset, u16 id, u16 val16)
{
	if ((offset + 5) >= WILC_MAX_CFG_FRAME_SIZE)
		return 0;

	put_unaligned_le16(id, &frame[offset]);
	put_unaligned_le16(2, &frame[offset + 2]);
	put_unaligned_le16(val16, &frame[offset + 4]);

	return 6;
}

static int wilc_wlan_cfg_set_word(u8 *frame, u32 offset, u16 id, u32 val32)
{
	if ((offset + 7) >= WILC_MAX_CFG_FRAME_SIZE)
		return 0;

	put_unaligned_le16(id, &frame[offset]);
	put_unaligned_le16(4, &frame[offset + 2]);
	put_unaligned_le32(val32, &frame[offset + 4]);

	return 8;
}

static int wilc_wlan_cfg_set_str(u8 *frame, u32 offset, u16 id, u8 *str,
				 u32 size)
{
	if ((offset + size + 4) >= WILC_MAX_CFG_FRAME_SIZE)
		return 0;

	put_unaligned_le16(id, &frame[offset]);
	put_unaligned_le16(size, &frame[offset + 2]);
	if (str && size != 0)
		memcpy(&frame[offset + 4], str, size);

	return (size + 4);
}

static int wilc_wlan_cfg_set_bin(u8 *frame, u32 offset, u16 id, u8 *b, u32 size)
{
	u32 i;
	u8 checksum = 0;

	if ((offset + size + 5) >= WILC_MAX_CFG_FRAME_SIZE)
		return 0;

	put_unaligned_le16(id, &frame[offset]);
	put_unaligned_le16(size, &frame[offset + 2]);

	if ((b) && size != 0) {
		memcpy(&frame[offset + 4], b, size);
		for (i = 0; i < size; i++)
			checksum += frame[offset + i + 4];
	}

	frame[offset + size + 4] = checksum;

	return (size + 5);
}

/********************************************
 *
 *      Configuration Response Functions
 *
 ********************************************/

static void wilc_wlan_parse_response_frame(struct wilc *wl, u8 *info, int size)
{
	u16 wid;
	u32 len = 0, i = 0;
	struct wilc_cfg *cfg = &wl->cfg;

	while (size > 0) {
		i = 0;
		wid = get_unaligned_le16(info);

		switch (FIELD_GET(WILC_WID_TYPE, wid)) {
		case WID_CHAR:
			while (cfg->b[i].id != WID_NIL && cfg->b[i].id != wid)
				i++;

			if (cfg->b[i].id == wid)
				cfg->b[i].val = info[4];

			len = 3;
			break;

		case WID_SHORT:
			while (cfg->hw[i].id != WID_NIL && cfg->hw[i].id != wid)
				i++;

			if (cfg->hw[i].id == wid)
				cfg->hw[i].val = get_unaligned_le16(&info[4]);

			len = 4;
			break;

		case WID_INT:
			while (cfg->w[i].id != WID_NIL && cfg->w[i].id != wid)
				i++;

			if (cfg->w[i].id == wid)
				cfg->w[i].val = get_unaligned_le32(&info[4]);

			len = 6;
			break;

		case WID_STR:
			while (cfg->s[i].id != WID_NIL && cfg->s[i].id != wid)
				i++;

			if (cfg->s[i].id == wid)
				memcpy(cfg->s[i].str, &info[2], info[2] + 2);

			len = 2 + info[2];
			break;

		default:
			break;
		}
		size -= (2 + len);
		info += (2 + len);
	}
}

static void wilc_wlan_parse_info_frame(struct wilc *wl, u8 *info)
{
	u32 wid, len;

	wid = get_unaligned_le16(info);

	len = info[2];

	if (len == 1 && wid == WID_STATUS) {
		int i = 0;

		while (wl->cfg.b[i].id != WID_NIL &&
		       wl->cfg.b[i].id != wid)
			i++;

		if (wl->cfg.b[i].id == wid)
			wl->cfg.b[i].val = info[3];
	}
}

/********************************************
 *
 *      Configuration Exported Functions
 *
 ********************************************/

int wilc_wlan_cfg_set_wid(u8 *frame, u32 offset, u16 id, u8 *buf, int size)
{
	u8 type = FIELD_GET(WILC_WID_TYPE, id);
	int ret = 0;

	switch (type) {
	case CFG_BYTE_CMD:
		if (size >= 1)
			ret = wilc_wlan_cfg_set_byte(frame, offset, id, *buf);
		break;

	case CFG_HWORD_CMD:
		if (size >= 2)
			ret = wilc_wlan_cfg_set_hword(frame, offset, id,
						      *((u16 *)buf));
		break;

	case CFG_WORD_CMD:
		if (size >= 4)
			ret = wilc_wlan_cfg_set_word(frame, offset, id,
						     *((u32 *)buf));
		break;

	case CFG_STR_CMD:
		ret = wilc_wlan_cfg_set_str(frame, offset, id, buf, size);
		break;

	case CFG_BIN_CMD:
		ret = wilc_wlan_cfg_set_bin(frame, offset, id, buf, size);
		break;
	}

	return ret;
}

int wilc_wlan_cfg_get_wid(u8 *frame, u32 offset, u16 id)
{
	if ((offset + 2) >= WILC_MAX_CFG_FRAME_SIZE)
		return 0;

	put_unaligned_le16(id, &frame[offset]);

	return 2;
}

int wilc_wlan_cfg_get_val(struct wilc *wl, u16 wid, u8 *buffer,
			  u32 buffer_size)
{
	u8 type = FIELD_GET(WILC_WID_TYPE, wid);
	int i, ret = 0;
	struct wilc_cfg *cfg = &wl->cfg;

	i = 0;
	if (type == CFG_BYTE_CMD) {
		while (cfg->b[i].id != WID_NIL && cfg->b[i].id != wid)
			i++;

		if (cfg->b[i].id == wid) {
			memcpy(buffer, &cfg->b[i].val, 1);
			ret = 1;
		}
	} else if (type == CFG_HWORD_CMD) {
		while (cfg->hw[i].id != WID_NIL && cfg->hw[i].id != wid)
			i++;

		if (cfg->hw[i].id == wid) {
			memcpy(buffer, &cfg->hw[i].val, 2);
			ret = 2;
		}
	} else if (type == CFG_WORD_CMD) {
		while (cfg->w[i].id != WID_NIL && cfg->w[i].id != wid)
			i++;

		if (cfg->w[i].id == wid) {
			memcpy(buffer, &cfg->w[i].val, 4);
			ret = 4;
		}
	} else if (type == CFG_STR_CMD) {
		while (cfg->s[i].id != WID_NIL && cfg->s[i].id != wid)
			i++;

		if (cfg->s[i].id == wid) {
			u16 size = get_unaligned_le16(cfg->s[i].str);

			if (buffer_size >= size) {
				memcpy(buffer, &cfg->s[i].str[2], size);
				ret = size;
			}
		}
	}
	return ret;
}

void wilc_wlan_cfg_indicate_rx(struct wilc *wilc, u8 *frame, int size,
			       struct wilc_cfg_rsp *rsp)
{
	u8 msg_type;
	u8 msg_id;

	msg_type = frame[0];
	msg_id = frame[1];      /* seq no */
	frame += 4;
	size -= 4;
	rsp->type = 0;

	switch (msg_type) {
	case WILC_RESP_MSG_TYPE_CONFIG_REPLY:
		wilc_wlan_parse_response_frame(wilc, frame, size);
		rsp->type = WILC_CFG_RSP;
		rsp->seq_no = msg_id;
		break;

	case WILC_RESP_MSG_TYPE_STATUS_INFO:
		wilc_wlan_parse_info_frame(wilc, frame);
		rsp->type = WILC_CFG_RSP_STATUS;
		rsp->seq_no = msg_id;
		/* call host interface info parse as well */
		wilc_gnrl_async_info_received(wilc, frame - 4, size + 4);
		break;

	case WILC_RESP_MSG_TYPE_NETWORK_INFO:
		wilc_network_info_received(wilc, frame - 4, size + 4);
		break;

	case WILC_RESP_MSG_TYPE_SCAN_COMPLETE:
		wilc_scan_complete_received(wilc, frame - 4, size + 4);
		break;

	default:
		rsp->seq_no = msg_id;
		break;
	}
}

int wilc_wlan_cfg_init(struct wilc *wl)
{
	struct wilc_cfg_str_vals *str_vals;
	int i = 0;

	wl->cfg.b = kmemdup(g_cfg_byte, sizeof(g_cfg_byte), GFP_KERNEL);
	if (!wl->cfg.b)
		return -ENOMEM;

	wl->cfg.hw = kmemdup(g_cfg_hword, sizeof(g_cfg_hword), GFP_KERNEL);
	if (!wl->cfg.hw)
		goto out_b;

	wl->cfg.w = kmemdup(g_cfg_word, sizeof(g_cfg_word), GFP_KERNEL);
	if (!wl->cfg.w)
		goto out_hw;

	wl->cfg.s = kmemdup(g_cfg_str, sizeof(g_cfg_str), GFP_KERNEL);
	if (!wl->cfg.s)
		goto out_w;

	str_vals = kzalloc(sizeof(*str_vals), GFP_KERNEL);
	if (!str_vals)
		goto out_s;

	wl->cfg.str_vals = str_vals;
	/* store the string cfg parameters */
	wl->cfg.s[i].id = WID_FIRMWARE_VERSION;
	wl->cfg.s[i].str = str_vals->firmware_version;
	i++;
	wl->cfg.s[i].id = WID_MAC_ADDR;
	wl->cfg.s[i].str = str_vals->mac_address;
	i++;
	wl->cfg.s[i].id = WID_ASSOC_RES_INFO;
	wl->cfg.s[i].str = str_vals->assoc_rsp;
	i++;
	wl->cfg.s[i].id = WID_NIL;
	wl->cfg.s[i].str = NULL;
	return 0;

out_s:
	kfree(wl->cfg.s);
out_w:
	kfree(wl->cfg.w);
out_hw:
	kfree(wl->cfg.hw);
out_b:
	kfree(wl->cfg.b);
	return -ENOMEM;
}

void wilc_wlan_cfg_deinit(struct wilc *wl)
{
	kfree(wl->cfg.b);
	kfree(wl->cfg.hw);
	kfree(wl->cfg.w);
	kfree(wl->cfg.s);
	kfree(wl->cfg.str_vals);
}
