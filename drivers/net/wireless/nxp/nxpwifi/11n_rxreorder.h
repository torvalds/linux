/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NXP Wireless LAN device driver: 802.11n RX Re-ordering
 *
 * Copyright 2011-2024 NXP
 */

#ifndef _NXPWIFI_11N_RXREORDER_H_
#define _NXPWIFI_11N_RXREORDER_H_

#define MIN_FLUSH_TIMER_MS		50
#define MIN_FLUSH_TIMER_15_MS		15
#define NXPWIFI_BA_WIN_SIZE_32		32

#define PKT_TYPE_BAR 0xE7
#define MAX_TID_VALUE			(2 << 11)
#define TWOPOW11			(2 << 10)

#define BLOCKACKPARAM_TID_POS		2
#define BLOCKACKPARAM_WINSIZE_POS	6
#define DELBA_TID_POS			12
#define DELBA_INITIATOR_POS		11
#define TYPE_DELBA_SENT			1
#define TYPE_DELBA_RECEIVE		2
#define IMMEDIATE_BLOCK_ACK		0x2

#define ADDBA_RSP_STATUS_ACCEPT 0

#define NXPWIFI_DEF_11N_RX_SEQ_NUM	0xffff
#define BA_SETUP_MAX_PACKET_THRESHOLD	16
#define BA_SETUP_PACKET_OFFSET		16

enum nxpwifi_rxreor_flags {
	RXREOR_FORCE_NO_DROP		= 1 << 0,
	RXREOR_INIT_WINDOW_SHIFT	= 1 << 1,
};

static inline void nxpwifi_reset_11n_rx_seq_num(struct nxpwifi_private *priv)
{
	memset(priv->rx_seq, 0xff, sizeof(priv->rx_seq));
}

int nxpwifi_11n_rx_reorder_pkt(struct nxpwifi_private *priv,
			       u16 seq_num,
			       u16 tid, u8 *ta,
			       u8 pkttype, void *payload);
void nxpwifi_del_ba_tbl(struct nxpwifi_private *priv, int tid,
			u8 *peer_mac, u8 type, int initiator);
void nxpwifi_11n_ba_stream_timeout(struct nxpwifi_private *priv,
				   struct host_cmd_ds_11n_batimeout *event);
int nxpwifi_ret_11n_addba_resp(struct nxpwifi_private *priv,
			       struct host_cmd_ds_command
			       *resp);
int nxpwifi_cmd_11n_delba(struct host_cmd_ds_command *cmd,
			  void *data_buf);
int nxpwifi_cmd_11n_addba_rsp_gen(struct nxpwifi_private *priv,
				  struct host_cmd_ds_command *cmd,
				  struct host_cmd_ds_11n_addba_req
				  *cmd_addba_req);
int nxpwifi_cmd_11n_addba_req(struct host_cmd_ds_command *cmd,
			      void *data_buf);
void nxpwifi_11n_cleanup_reorder_tbl(struct nxpwifi_private *priv);
struct nxpwifi_rx_reorder_tbl *
nxpwifi_11n_get_rxreorder_tbl(struct nxpwifi_private *priv, int tid, u8 *ta);
struct nxpwifi_rx_reorder_tbl *
nxpwifi_11n_get_rx_reorder_tbl(struct nxpwifi_private *priv, int tid, u8 *ta);
void nxpwifi_11n_del_rx_reorder_tbl_by_ta(struct nxpwifi_private *priv, u8 *ta);
void nxpwifi_update_rxreor_flags(struct nxpwifi_adapter *adapter, u8 flags);
void nxpwifi_11n_rxba_sync_event(struct nxpwifi_private *priv,
				 u8 *event_buf, u16 len);
#endif /* _NXPWIFI_11N_RXREORDER_H_ */
