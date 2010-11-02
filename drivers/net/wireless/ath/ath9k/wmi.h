/*
 * Copyright (c) 2010 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef WMI_H
#define WMI_H


struct wmi_event_txrate {
	__be32 txrate;
	struct {
		u8 rssi_thresh;
		u8 per;
	} rc_stats;
} __packed;

struct wmi_cmd_hdr {
	__be16 command_id;
	__be16 seq_no;
} __packed;

enum wmi_cmd_id {
	WMI_ECHO_CMDID = 0x0001,
	WMI_ACCESS_MEMORY_CMDID,

	/* Commands to Target */
	WMI_DISABLE_INTR_CMDID,
	WMI_ENABLE_INTR_CMDID,
	WMI_RX_LINK_CMDID,
	WMI_ATH_INIT_CMDID,
	WMI_ABORT_TXQ_CMDID,
	WMI_STOP_TX_DMA_CMDID,
	WMI_STOP_DMA_RECV_CMDID,
	WMI_ABORT_TX_DMA_CMDID,
	WMI_DRAIN_TXQ_CMDID,
	WMI_DRAIN_TXQ_ALL_CMDID,
	WMI_START_RECV_CMDID,
	WMI_STOP_RECV_CMDID,
	WMI_FLUSH_RECV_CMDID,
	WMI_SET_MODE_CMDID,
	WMI_RESET_CMDID,
	WMI_NODE_CREATE_CMDID,
	WMI_NODE_REMOVE_CMDID,
	WMI_VAP_REMOVE_CMDID,
	WMI_VAP_CREATE_CMDID,
	WMI_BEACON_UPDATE_CMDID,
	WMI_REG_READ_CMDID,
	WMI_REG_WRITE_CMDID,
	WMI_RC_STATE_CHANGE_CMDID,
	WMI_RC_RATE_UPDATE_CMDID,
	WMI_DEBUG_INFO_CMDID,
	WMI_HOST_ATTACH,
	WMI_TARGET_IC_UPDATE_CMDID,
	WMI_TGT_STATS_CMDID,
	WMI_TX_AGGR_ENABLE_CMDID,
	WMI_TGT_DETACH_CMDID,
	WMI_TGT_TXQ_ENABLE_CMDID,
	WMI_AGGR_LIMIT_CMD = 0x0026,
};

enum wmi_event_id {
	WMI_TGT_RDY_EVENTID = 0x1001,
	WMI_SWBA_EVENTID,
	WMI_FATAL_EVENTID,
	WMI_TXTO_EVENTID,
	WMI_BMISS_EVENTID,
	WMI_WLAN_TXCOMP_EVENTID,
	WMI_DELBA_EVENTID,
	WMI_TXRATE_EVENTID,
};

#define MAX_CMD_NUMBER 62

struct register_write {
	__be32 reg;
	__be32 val;
};

struct wmi {
	struct ath9k_htc_priv *drv_priv;
	struct htc_target *htc;
	enum htc_endpoint_id ctrl_epid;
	struct mutex op_mutex;
	struct completion cmd_wait;
	enum wmi_cmd_id last_cmd_id;
	u16 tx_seq_id;
	u8 *cmd_rsp_buf;
	u32 cmd_rsp_len;
	bool stopped;

	u8 beacon_pending;
	spinlock_t wmi_lock;

	atomic_t mwrite_cnt;
	struct register_write multi_write[MAX_CMD_NUMBER];
	u32 multi_write_idx;
	struct mutex multi_write_mutex;
};

struct wmi *ath9k_init_wmi(struct ath9k_htc_priv *priv);
void ath9k_deinit_wmi(struct ath9k_htc_priv *priv);
int ath9k_wmi_connect(struct htc_target *htc, struct wmi *wmi,
		      enum htc_endpoint_id *wmi_ctrl_epid);
int ath9k_wmi_cmd(struct wmi *wmi, enum wmi_cmd_id cmd_id,
		  u8 *cmd_buf, u32 cmd_len,
		  u8 *rsp_buf, u32 rsp_len,
		  u32 timeout);
void ath9k_wmi_tasklet(unsigned long data);

#define WMI_CMD(_wmi_cmd)						\
	do {								\
		ret = ath9k_wmi_cmd(priv->wmi, _wmi_cmd, NULL, 0,	\
				    (u8 *) &cmd_rsp,			\
				    sizeof(cmd_rsp), HZ*2);		\
	} while (0)

#define WMI_CMD_BUF(_wmi_cmd, _buf)					\
	do {								\
		ret = ath9k_wmi_cmd(priv->wmi, _wmi_cmd,		\
				    (u8 *) _buf, sizeof(*_buf),		\
				    &cmd_rsp, sizeof(cmd_rsp), HZ*2);	\
	} while (0)

#endif /* WMI_H */
