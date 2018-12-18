/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2014-2016 Broadcom Corporation
 * Copyright (c) 2016-2017 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_ETHTOOL_H
#define BNXT_ETHTOOL_H

struct bnxt_led_cfg {
	u8 led_id;
	u8 led_state;
	u8 led_color;
	u8 unused;
	__le16 led_blink_on;
	__le16 led_blink_off;
	u8 led_group_id;
	u8 rsvd;
};

#define COREDUMP_LIST_BUF_LEN		2048
#define COREDUMP_RETRIEVE_BUF_LEN	4096

struct bnxt_coredump {
	void		*data;
	int		data_size;
	u16		total_segs;
};

struct bnxt_hwrm_dbg_dma_info {
	void *dest_buf;
	int dest_buf_size;
	u16 dma_len;
	u16 seq_off;
	u16 data_len_off;
	u16 segs;
};

struct hwrm_dbg_cmn_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le64 host_dest_addr;
	__le32 host_buf_len;
};

struct hwrm_dbg_cmn_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	u8 flags;
	#define HWRM_DBG_CMN_FLAGS_MORE	1
};

#define BNXT_LED_DFLT_ENA				\
	(PORT_LED_CFG_REQ_ENABLES_LED0_ID |		\
	 PORT_LED_CFG_REQ_ENABLES_LED0_STATE |		\
	 PORT_LED_CFG_REQ_ENABLES_LED0_BLINK_ON |	\
	 PORT_LED_CFG_REQ_ENABLES_LED0_BLINK_OFF |	\
	 PORT_LED_CFG_REQ_ENABLES_LED0_GROUP_ID)

#define BNXT_LED_DFLT_ENA_SHIFT	6

#define BNXT_LED_DFLT_ENABLES(x)			\
	cpu_to_le32(BNXT_LED_DFLT_ENA << (BNXT_LED_DFLT_ENA_SHIFT * (x)))

#define BNXT_FW_RESET_AP	0xfffe
#define BNXT_FW_RESET_CHIP	0xffff

extern const struct ethtool_ops bnxt_ethtool_ops;

u32 _bnxt_fw_to_ethtool_adv_spds(u16, u8);
u32 bnxt_fw_to_ethtool_speed(u16);
u16 bnxt_get_fw_auto_link_speeds(u32);
void bnxt_ethtool_init(struct bnxt *bp);
void bnxt_ethtool_free(struct bnxt *bp);

#endif
