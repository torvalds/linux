// SPDX-License-Identifier: GPL-2.0-only
/*
 * WSM host interface (HI) implementation for
 * ST-Ericsson CW1200 mac80211 drivers.
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@lockless.no>
 */

#include <linux/skbuff.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/random.h>

#include "cw1200.h"
#include "wsm.h"
#include "bh.h"
#include "sta.h"
#include "debug.h"

#define WSM_CMD_TIMEOUT		(2 * HZ) /* With respect to interrupt loss */
#define WSM_CMD_START_TIMEOUT	(7 * HZ)
#define WSM_CMD_RESET_TIMEOUT	(3 * HZ) /* 2 sec. timeout was observed.   */
#define WSM_CMD_MAX_TIMEOUT	(3 * HZ)

#define WSM_SKIP(buf, size)						\
	do {								\
		if ((buf)->data + size > (buf)->end)			\
			goto underflow;					\
		(buf)->data += size;					\
	} while (0)

#define WSM_GET(buf, ptr, size)						\
	do {								\
		if ((buf)->data + size > (buf)->end)			\
			goto underflow;					\
		memcpy(ptr, (buf)->data, size);				\
		(buf)->data += size;					\
	} while (0)

#define __WSM_GET(buf, type, type2, cvt)				\
	({								\
		type val;						\
		if ((buf)->data + sizeof(type) > (buf)->end)		\
			goto underflow;					\
		val = cvt(*(type2 *)(buf)->data);			\
		(buf)->data += sizeof(type);				\
		val;							\
	})

#define WSM_GET8(buf)  __WSM_GET(buf, u8, u8, (u8))
#define WSM_GET16(buf) __WSM_GET(buf, u16, __le16, __le16_to_cpu)
#define WSM_GET32(buf) __WSM_GET(buf, u32, __le32, __le32_to_cpu)

#define WSM_PUT(buf, ptr, size)						\
	do {								\
		if ((buf)->data + size > (buf)->end)		\
			if (wsm_buf_reserve((buf), size))	\
				goto nomem;				\
		memcpy((buf)->data, ptr, size);				\
		(buf)->data += size;					\
	} while (0)

#define __WSM_PUT(buf, val, type, type2, cvt)				\
	do {								\
		if ((buf)->data + sizeof(type) > (buf)->end)		\
			if (wsm_buf_reserve((buf), sizeof(type))) \
				goto nomem;				\
		*(type2 *)(buf)->data = cvt(val);			\
		(buf)->data += sizeof(type);				\
	} while (0)

#define WSM_PUT8(buf, val)  __WSM_PUT(buf, val, u8, u8, (u8))
#define WSM_PUT16(buf, val) __WSM_PUT(buf, val, u16, __le16, __cpu_to_le16)
#define WSM_PUT32(buf, val) __WSM_PUT(buf, val, u32, __le32, __cpu_to_le32)

static void wsm_buf_reset(struct wsm_buf *buf);
static int wsm_buf_reserve(struct wsm_buf *buf, size_t extra_size);

static int wsm_cmd_send(struct cw1200_common *priv,
			struct wsm_buf *buf,
			void *arg, u16 cmd, long tmo);

#define wsm_cmd_lock(__priv) mutex_lock(&((__priv)->wsm_cmd_mux))
#define wsm_cmd_unlock(__priv) mutex_unlock(&((__priv)->wsm_cmd_mux))

/* ******************************************************************** */
/* WSM API implementation						*/

static int wsm_generic_confirm(struct cw1200_common *priv,
			     void *arg,
			     struct wsm_buf *buf)
{
	u32 status = WSM_GET32(buf);
	if (status != WSM_STATUS_SUCCESS)
		return -EINVAL;
	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

int wsm_configuration(struct cw1200_common *priv, struct wsm_configuration *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);

	WSM_PUT32(buf, arg->dot11MaxTransmitMsduLifeTime);
	WSM_PUT32(buf, arg->dot11MaxReceiveLifeTime);
	WSM_PUT32(buf, arg->dot11RtsThreshold);

	/* DPD block. */
	WSM_PUT16(buf, arg->dpdData_size + 12);
	WSM_PUT16(buf, 1); /* DPD version */
	WSM_PUT(buf, arg->dot11StationId, ETH_ALEN);
	WSM_PUT16(buf, 5); /* DPD flags */
	WSM_PUT(buf, arg->dpdData, arg->dpdData_size);

	ret = wsm_cmd_send(priv, buf, arg,
			   WSM_CONFIGURATION_REQ_ID, WSM_CMD_TIMEOUT);

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

static int wsm_configuration_confirm(struct cw1200_common *priv,
				     struct wsm_configuration *arg,
				     struct wsm_buf *buf)
{
	int i;
	int status;

	status = WSM_GET32(buf);
	if (WARN_ON(status != WSM_STATUS_SUCCESS))
		return -EINVAL;

	WSM_GET(buf, arg->dot11StationId, ETH_ALEN);
	arg->dot11FrequencyBandsSupported = WSM_GET8(buf);
	WSM_SKIP(buf, 1);
	arg->supportedRateMask = WSM_GET32(buf);
	for (i = 0; i < 2; ++i) {
		arg->txPowerRange[i].min_power_level = WSM_GET32(buf);
		arg->txPowerRange[i].max_power_level = WSM_GET32(buf);
		arg->txPowerRange[i].stepping = WSM_GET32(buf);
	}
	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

/* ******************************************************************** */

int wsm_reset(struct cw1200_common *priv, const struct wsm_reset *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;
	u16 cmd = WSM_RESET_REQ_ID | WSM_TX_LINK_ID(arg->link_id);

	wsm_cmd_lock(priv);

	WSM_PUT32(buf, arg->reset_statistics ? 0 : 1);
	ret = wsm_cmd_send(priv, buf, NULL, cmd, WSM_CMD_RESET_TIMEOUT);
	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

struct wsm_mib {
	u16 mib_id;
	void *buf;
	size_t buf_size;
};

int wsm_read_mib(struct cw1200_common *priv, u16 mib_id, void *_buf,
			size_t buf_size)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;
	struct wsm_mib mib_buf = {
		.mib_id = mib_id,
		.buf = _buf,
		.buf_size = buf_size,
	};
	wsm_cmd_lock(priv);

	WSM_PUT16(buf, mib_id);
	WSM_PUT16(buf, 0);

	ret = wsm_cmd_send(priv, buf, &mib_buf,
			   WSM_READ_MIB_REQ_ID, WSM_CMD_TIMEOUT);
	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

static int wsm_read_mib_confirm(struct cw1200_common *priv,
				struct wsm_mib *arg,
				struct wsm_buf *buf)
{
	u16 size;
	if (WARN_ON(WSM_GET32(buf) != WSM_STATUS_SUCCESS))
		return -EINVAL;

	if (WARN_ON(WSM_GET16(buf) != arg->mib_id))
		return -EINVAL;

	size = WSM_GET16(buf);
	if (size > arg->buf_size)
		size = arg->buf_size;

	WSM_GET(buf, arg->buf, size);
	arg->buf_size = size;
	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

/* ******************************************************************** */

int wsm_write_mib(struct cw1200_common *priv, u16 mib_id, void *_buf,
			size_t buf_size)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;
	struct wsm_mib mib_buf = {
		.mib_id = mib_id,
		.buf = _buf,
		.buf_size = buf_size,
	};

	wsm_cmd_lock(priv);

	WSM_PUT16(buf, mib_id);
	WSM_PUT16(buf, buf_size);
	WSM_PUT(buf, _buf, buf_size);

	ret = wsm_cmd_send(priv, buf, &mib_buf,
			   WSM_WRITE_MIB_REQ_ID, WSM_CMD_TIMEOUT);
	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

static int wsm_write_mib_confirm(struct cw1200_common *priv,
				struct wsm_mib *arg,
				struct wsm_buf *buf)
{
	int ret;

	ret = wsm_generic_confirm(priv, arg, buf);
	if (ret)
		return ret;

	if (arg->mib_id == WSM_MIB_ID_OPERATIONAL_POWER_MODE) {
		/* OperationalMode: update PM status. */
		const char *p = arg->buf;
		cw1200_enable_powersave(priv, (p[0] & 0x0F) ? true : false);
	}
	return 0;
}

/* ******************************************************************** */

int wsm_scan(struct cw1200_common *priv, const struct wsm_scan *arg)
{
	int i;
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	if (arg->num_channels > 48)
		return -EINVAL;

	if (arg->num_ssids > 2)
		return -EINVAL;

	if (arg->band > 1)
		return -EINVAL;

	wsm_cmd_lock(priv);

	WSM_PUT8(buf, arg->band);
	WSM_PUT8(buf, arg->type);
	WSM_PUT8(buf, arg->flags);
	WSM_PUT8(buf, arg->max_tx_rate);
	WSM_PUT32(buf, arg->auto_scan_interval);
	WSM_PUT8(buf, arg->num_probes);
	WSM_PUT8(buf, arg->num_channels);
	WSM_PUT8(buf, arg->num_ssids);
	WSM_PUT8(buf, arg->probe_delay);

	for (i = 0; i < arg->num_channels; ++i) {
		WSM_PUT16(buf, arg->ch[i].number);
		WSM_PUT16(buf, 0);
		WSM_PUT32(buf, arg->ch[i].min_chan_time);
		WSM_PUT32(buf, arg->ch[i].max_chan_time);
		WSM_PUT32(buf, 0);
	}

	for (i = 0; i < arg->num_ssids; ++i) {
		WSM_PUT32(buf, arg->ssids[i].length);
		WSM_PUT(buf, &arg->ssids[i].ssid[0],
			sizeof(arg->ssids[i].ssid));
	}

	ret = wsm_cmd_send(priv, buf, NULL,
			   WSM_START_SCAN_REQ_ID, WSM_CMD_TIMEOUT);
	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_stop_scan(struct cw1200_common *priv)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;
	wsm_cmd_lock(priv);
	ret = wsm_cmd_send(priv, buf, NULL,
			   WSM_STOP_SCAN_REQ_ID, WSM_CMD_TIMEOUT);
	wsm_cmd_unlock(priv);
	return ret;
}


static int wsm_tx_confirm(struct cw1200_common *priv,
			  struct wsm_buf *buf,
			  int link_id)
{
	struct wsm_tx_confirm tx_confirm;

	tx_confirm.packet_id = WSM_GET32(buf);
	tx_confirm.status = WSM_GET32(buf);
	tx_confirm.tx_rate = WSM_GET8(buf);
	tx_confirm.ack_failures = WSM_GET8(buf);
	tx_confirm.flags = WSM_GET16(buf);
	tx_confirm.media_delay = WSM_GET32(buf);
	tx_confirm.tx_queue_delay = WSM_GET32(buf);

	cw1200_tx_confirm_cb(priv, link_id, &tx_confirm);
	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

static int wsm_multi_tx_confirm(struct cw1200_common *priv,
				struct wsm_buf *buf, int link_id)
{
	int ret;
	int count;

	count = WSM_GET32(buf);
	if (WARN_ON(count <= 0))
		return -EINVAL;

	if (count > 1) {
		/* We already released one buffer, now for the rest */
		ret = wsm_release_tx_buffer(priv, count - 1);
		if (ret < 0)
			return ret;
		else if (ret > 0)
			cw1200_bh_wakeup(priv);
	}

	cw1200_debug_txed_multi(priv, count);
	do {
		ret = wsm_tx_confirm(priv, buf, link_id);
	} while (!ret && --count);

	return ret;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

/* ******************************************************************** */

static int wsm_join_confirm(struct cw1200_common *priv,
			    struct wsm_join_cnf *arg,
			    struct wsm_buf *buf)
{
	arg->status = WSM_GET32(buf);
	if (WARN_ON(arg->status) != WSM_STATUS_SUCCESS)
		return -EINVAL;

	arg->min_power_level = WSM_GET32(buf);
	arg->max_power_level = WSM_GET32(buf);

	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

int wsm_join(struct cw1200_common *priv, struct wsm_join *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;
	struct wsm_join_cnf resp;
	wsm_cmd_lock(priv);

	WSM_PUT8(buf, arg->mode);
	WSM_PUT8(buf, arg->band);
	WSM_PUT16(buf, arg->channel_number);
	WSM_PUT(buf, &arg->bssid[0], sizeof(arg->bssid));
	WSM_PUT16(buf, arg->atim_window);
	WSM_PUT8(buf, arg->preamble_type);
	WSM_PUT8(buf, arg->probe_for_join);
	WSM_PUT8(buf, arg->dtim_period);
	WSM_PUT8(buf, arg->flags);
	WSM_PUT32(buf, arg->ssid_len);
	WSM_PUT(buf, &arg->ssid[0], sizeof(arg->ssid));
	WSM_PUT32(buf, arg->beacon_interval);
	WSM_PUT32(buf, arg->basic_rate_set);

	priv->tx_burst_idx = -1;
	ret = wsm_cmd_send(priv, buf, &resp,
			   WSM_JOIN_REQ_ID, WSM_CMD_TIMEOUT);
	/* TODO:  Update state based on resp.min|max_power_level */

	priv->join_complete_status = resp.status;

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_set_bss_params(struct cw1200_common *priv,
		       const struct wsm_set_bss_params *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);

	WSM_PUT8(buf, (arg->reset_beacon_loss ?  0x1 : 0));
	WSM_PUT8(buf, arg->beacon_lost_count);
	WSM_PUT16(buf, arg->aid);
	WSM_PUT32(buf, arg->operational_rate_set);

	ret = wsm_cmd_send(priv, buf, NULL,
			   WSM_SET_BSS_PARAMS_REQ_ID, WSM_CMD_TIMEOUT);

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_add_key(struct cw1200_common *priv, const struct wsm_add_key *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);

	WSM_PUT(buf, arg, sizeof(*arg));

	ret = wsm_cmd_send(priv, buf, NULL,
			   WSM_ADD_KEY_REQ_ID, WSM_CMD_TIMEOUT);

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_remove_key(struct cw1200_common *priv, const struct wsm_remove_key *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);

	WSM_PUT8(buf, arg->index);
	WSM_PUT8(buf, 0);
	WSM_PUT16(buf, 0);

	ret = wsm_cmd_send(priv, buf, NULL,
			   WSM_REMOVE_KEY_REQ_ID, WSM_CMD_TIMEOUT);

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_set_tx_queue_params(struct cw1200_common *priv,
		const struct wsm_set_tx_queue_params *arg, u8 id)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;
	u8 queue_id_to_wmm_aci[] = {3, 2, 0, 1};

	wsm_cmd_lock(priv);

	WSM_PUT8(buf, queue_id_to_wmm_aci[id]);
	WSM_PUT8(buf, 0);
	WSM_PUT8(buf, arg->ackPolicy);
	WSM_PUT8(buf, 0);
	WSM_PUT32(buf, arg->maxTransmitLifetime);
	WSM_PUT16(buf, arg->allowedMediumTime);
	WSM_PUT16(buf, 0);

	ret = wsm_cmd_send(priv, buf, NULL, 0x0012, WSM_CMD_TIMEOUT);

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_set_edca_params(struct cw1200_common *priv,
				const struct wsm_edca_params *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);

	/* Implemented according to specification. */

	WSM_PUT16(buf, arg->params[3].cwmin);
	WSM_PUT16(buf, arg->params[2].cwmin);
	WSM_PUT16(buf, arg->params[1].cwmin);
	WSM_PUT16(buf, arg->params[0].cwmin);

	WSM_PUT16(buf, arg->params[3].cwmax);
	WSM_PUT16(buf, arg->params[2].cwmax);
	WSM_PUT16(buf, arg->params[1].cwmax);
	WSM_PUT16(buf, arg->params[0].cwmax);

	WSM_PUT8(buf, arg->params[3].aifns);
	WSM_PUT8(buf, arg->params[2].aifns);
	WSM_PUT8(buf, arg->params[1].aifns);
	WSM_PUT8(buf, arg->params[0].aifns);

	WSM_PUT16(buf, arg->params[3].txop_limit);
	WSM_PUT16(buf, arg->params[2].txop_limit);
	WSM_PUT16(buf, arg->params[1].txop_limit);
	WSM_PUT16(buf, arg->params[0].txop_limit);

	WSM_PUT32(buf, arg->params[3].max_rx_lifetime);
	WSM_PUT32(buf, arg->params[2].max_rx_lifetime);
	WSM_PUT32(buf, arg->params[1].max_rx_lifetime);
	WSM_PUT32(buf, arg->params[0].max_rx_lifetime);

	ret = wsm_cmd_send(priv, buf, NULL,
			   WSM_EDCA_PARAMS_REQ_ID, WSM_CMD_TIMEOUT);
	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_switch_channel(struct cw1200_common *priv,
			const struct wsm_switch_channel *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);

	WSM_PUT8(buf, arg->mode);
	WSM_PUT8(buf, arg->switch_count);
	WSM_PUT16(buf, arg->channel_number);

	priv->channel_switch_in_progress = 1;

	ret = wsm_cmd_send(priv, buf, NULL,
			   WSM_SWITCH_CHANNEL_REQ_ID, WSM_CMD_TIMEOUT);
	if (ret)
		priv->channel_switch_in_progress = 0;

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_set_pm(struct cw1200_common *priv, const struct wsm_set_pm *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;
	priv->ps_mode_switch_in_progress = 1;

	wsm_cmd_lock(priv);

	WSM_PUT8(buf, arg->mode);
	WSM_PUT8(buf, arg->fast_psm_idle_period);
	WSM_PUT8(buf, arg->ap_psm_change_period);
	WSM_PUT8(buf, arg->min_auto_pspoll_period);

	ret = wsm_cmd_send(priv, buf, NULL,
			   WSM_SET_PM_REQ_ID, WSM_CMD_TIMEOUT);

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_start(struct cw1200_common *priv, const struct wsm_start *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);

	WSM_PUT8(buf, arg->mode);
	WSM_PUT8(buf, arg->band);
	WSM_PUT16(buf, arg->channel_number);
	WSM_PUT32(buf, arg->ct_window);
	WSM_PUT32(buf, arg->beacon_interval);
	WSM_PUT8(buf, arg->dtim_period);
	WSM_PUT8(buf, arg->preamble);
	WSM_PUT8(buf, arg->probe_delay);
	WSM_PUT8(buf, arg->ssid_len);
	WSM_PUT(buf, arg->ssid, sizeof(arg->ssid));
	WSM_PUT32(buf, arg->basic_rate_set);

	priv->tx_burst_idx = -1;
	ret = wsm_cmd_send(priv, buf, NULL,
			   WSM_START_REQ_ID, WSM_CMD_START_TIMEOUT);

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_beacon_transmit(struct cw1200_common *priv,
			const struct wsm_beacon_transmit *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);

	WSM_PUT32(buf, arg->enable_beaconing ? 1 : 0);

	ret = wsm_cmd_send(priv, buf, NULL,
			   WSM_BEACON_TRANSMIT_REQ_ID, WSM_CMD_TIMEOUT);

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_start_find(struct cw1200_common *priv)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);
	ret = wsm_cmd_send(priv, buf, NULL, 0x0019, WSM_CMD_TIMEOUT);
	wsm_cmd_unlock(priv);
	return ret;
}

/* ******************************************************************** */

int wsm_stop_find(struct cw1200_common *priv)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);
	ret = wsm_cmd_send(priv, buf, NULL, 0x001A, WSM_CMD_TIMEOUT);
	wsm_cmd_unlock(priv);
	return ret;
}

/* ******************************************************************** */

int wsm_map_link(struct cw1200_common *priv, const struct wsm_map_link *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;
	u16 cmd = 0x001C | WSM_TX_LINK_ID(arg->link_id);

	wsm_cmd_lock(priv);

	WSM_PUT(buf, &arg->mac_addr[0], sizeof(arg->mac_addr));
	WSM_PUT16(buf, 0);

	ret = wsm_cmd_send(priv, buf, NULL, cmd, WSM_CMD_TIMEOUT);

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_update_ie(struct cw1200_common *priv,
		  const struct wsm_update_ie *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);

	WSM_PUT16(buf, arg->what);
	WSM_PUT16(buf, arg->count);
	WSM_PUT(buf, arg->ies, arg->length);

	ret = wsm_cmd_send(priv, buf, NULL, 0x001B, WSM_CMD_TIMEOUT);

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */
int wsm_set_probe_responder(struct cw1200_common *priv, bool enable)
{
	priv->rx_filter.probeResponder = enable;
	return wsm_set_rx_filter(priv, &priv->rx_filter);
}

/* ******************************************************************** */
/* WSM indication events implementation					*/
const char * const cw1200_fw_types[] = {
	"ETF",
	"WFM",
	"WSM",
	"HI test",
	"Platform test"
};

static int wsm_startup_indication(struct cw1200_common *priv,
					struct wsm_buf *buf)
{
	priv->wsm_caps.input_buffers     = WSM_GET16(buf);
	priv->wsm_caps.input_buffer_size = WSM_GET16(buf);
	priv->wsm_caps.hw_id	  = WSM_GET16(buf);
	priv->wsm_caps.hw_subid	  = WSM_GET16(buf);
	priv->wsm_caps.status	  = WSM_GET16(buf);
	priv->wsm_caps.fw_cap	  = WSM_GET16(buf);
	priv->wsm_caps.fw_type	  = WSM_GET16(buf);
	priv->wsm_caps.fw_api	  = WSM_GET16(buf);
	priv->wsm_caps.fw_build   = WSM_GET16(buf);
	priv->wsm_caps.fw_ver     = WSM_GET16(buf);
	WSM_GET(buf, priv->wsm_caps.fw_label, sizeof(priv->wsm_caps.fw_label));
	priv->wsm_caps.fw_label[sizeof(priv->wsm_caps.fw_label) - 1] = 0; /* Do not trust FW too much... */

	if (WARN_ON(priv->wsm_caps.status))
		return -EINVAL;

	if (WARN_ON(priv->wsm_caps.fw_type > 4))
		return -EINVAL;

	pr_info("CW1200 WSM init done.\n"
		"   Input buffers: %d x %d bytes\n"
		"   Hardware: %d.%d\n"
		"   %s firmware [%s], ver: %d, build: %d,"
		"   api: %d, cap: 0x%.4X\n",
		priv->wsm_caps.input_buffers,
		priv->wsm_caps.input_buffer_size,
		priv->wsm_caps.hw_id, priv->wsm_caps.hw_subid,
		cw1200_fw_types[priv->wsm_caps.fw_type],
		priv->wsm_caps.fw_label, priv->wsm_caps.fw_ver,
		priv->wsm_caps.fw_build,
		priv->wsm_caps.fw_api, priv->wsm_caps.fw_cap);

	/* Disable unsupported frequency bands */
	if (!(priv->wsm_caps.fw_cap & 0x1))
		priv->hw->wiphy->bands[NL80211_BAND_2GHZ] = NULL;
	if (!(priv->wsm_caps.fw_cap & 0x2))
		priv->hw->wiphy->bands[NL80211_BAND_5GHZ] = NULL;

	priv->firmware_ready = 1;
	wake_up(&priv->wsm_startup_done);
	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

static int wsm_receive_indication(struct cw1200_common *priv,
				  int link_id,
				  struct wsm_buf *buf,
				  struct sk_buff **skb_p)
{
	struct wsm_rx rx;
	struct ieee80211_hdr *hdr;
	size_t hdr_len;
	__le16 fctl;

	rx.status = WSM_GET32(buf);
	rx.channel_number = WSM_GET16(buf);
	rx.rx_rate = WSM_GET8(buf);
	rx.rcpi_rssi = WSM_GET8(buf);
	rx.flags = WSM_GET32(buf);

	/* FW Workaround: Drop probe resp or
	   beacon when RSSI is 0
	*/
	hdr = (struct ieee80211_hdr *)(*skb_p)->data;

	if (!rx.rcpi_rssi &&
	    (ieee80211_is_probe_resp(hdr->frame_control) ||
	     ieee80211_is_beacon(hdr->frame_control)))
		return 0;

	/* If no RSSI subscription has been made,
	 * convert RCPI to RSSI here
	 */
	if (!priv->cqm_use_rssi)
		rx.rcpi_rssi = rx.rcpi_rssi / 2 - 110;

	fctl = *(__le16 *)buf->data;
	hdr_len = buf->data - buf->begin;
	skb_pull(*skb_p, hdr_len);
	if (!rx.status && ieee80211_is_deauth(fctl)) {
		if (priv->join_status == CW1200_JOIN_STATUS_STA) {
			/* Shedule unjoin work */
			pr_debug("[WSM] Issue unjoin command (RX).\n");
			wsm_lock_tx_async(priv);
			if (queue_work(priv->workqueue,
				       &priv->unjoin_work) <= 0)
				wsm_unlock_tx(priv);
		}
	}
	cw1200_rx_cb(priv, &rx, link_id, skb_p);
	if (*skb_p)
		skb_push(*skb_p, hdr_len);

	return 0;

underflow:
	return -EINVAL;
}

static int wsm_event_indication(struct cw1200_common *priv, struct wsm_buf *buf)
{
	int first;
	struct cw1200_wsm_event *event;

	if (priv->mode == NL80211_IFTYPE_UNSPECIFIED) {
		/* STA is stopped. */
		return 0;
	}

	event = kzalloc(sizeof(struct cw1200_wsm_event), GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	event->evt.id = WSM_GET32(buf);
	event->evt.data = WSM_GET32(buf);

	pr_debug("[WSM] Event: %d(%d)\n",
		 event->evt.id, event->evt.data);

	spin_lock(&priv->event_queue_lock);
	first = list_empty(&priv->event_queue);
	list_add_tail(&event->link, &priv->event_queue);
	spin_unlock(&priv->event_queue_lock);

	if (first)
		queue_work(priv->workqueue, &priv->event_handler);

	return 0;

underflow:
	kfree(event);
	return -EINVAL;
}

static int wsm_channel_switch_indication(struct cw1200_common *priv,
					 struct wsm_buf *buf)
{
	WARN_ON(WSM_GET32(buf));

	priv->channel_switch_in_progress = 0;
	wake_up(&priv->channel_switch_done);

	wsm_unlock_tx(priv);

	return 0;

underflow:
	return -EINVAL;
}

static int wsm_set_pm_indication(struct cw1200_common *priv,
				 struct wsm_buf *buf)
{
	/* TODO:  Check buf (struct wsm_set_pm_complete) for validity */
	if (priv->ps_mode_switch_in_progress) {
		priv->ps_mode_switch_in_progress = 0;
		wake_up(&priv->ps_mode_switch_done);
	}
	return 0;
}

static int wsm_scan_started(struct cw1200_common *priv, void *arg,
			    struct wsm_buf *buf)
{
	u32 status = WSM_GET32(buf);
	if (status != WSM_STATUS_SUCCESS) {
		cw1200_scan_failed_cb(priv);
		return -EINVAL;
	}
	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

static int wsm_scan_complete_indication(struct cw1200_common *priv,
					struct wsm_buf *buf)
{
	struct wsm_scan_complete arg;
	arg.status = WSM_GET32(buf);
	arg.psm = WSM_GET8(buf);
	arg.num_channels = WSM_GET8(buf);
	cw1200_scan_complete_cb(priv, &arg);

	return 0;

underflow:
	return -EINVAL;
}

static int wsm_join_complete_indication(struct cw1200_common *priv,
					struct wsm_buf *buf)
{
	struct wsm_join_complete arg;
	arg.status = WSM_GET32(buf);
	pr_debug("[WSM] Join complete indication, status: %d\n", arg.status);
	cw1200_join_complete_cb(priv, &arg);

	return 0;

underflow:
	return -EINVAL;
}

static int wsm_find_complete_indication(struct cw1200_common *priv,
					struct wsm_buf *buf)
{
	pr_warn("Implement find_complete_indication\n");
	return 0;
}

static int wsm_ba_timeout_indication(struct cw1200_common *priv,
				     struct wsm_buf *buf)
{
	u8 tid;
	u8 addr[ETH_ALEN];

	WSM_GET32(buf);
	tid = WSM_GET8(buf);
	WSM_GET8(buf);
	WSM_GET(buf, addr, ETH_ALEN);

	pr_info("BlockACK timeout, tid %d, addr %pM\n",
		tid, addr);

	return 0;

underflow:
	return -EINVAL;
}

static int wsm_suspend_resume_indication(struct cw1200_common *priv,
					 int link_id, struct wsm_buf *buf)
{
	u32 flags;
	struct wsm_suspend_resume arg;

	flags = WSM_GET32(buf);
	arg.link_id = link_id;
	arg.stop = !(flags & 1);
	arg.multicast = !!(flags & 8);
	arg.queue = (flags >> 1) & 3;

	cw1200_suspend_resume(priv, &arg);

	return 0;

underflow:
	return -EINVAL;
}


/* ******************************************************************** */
/* WSM TX								*/

static int wsm_cmd_send(struct cw1200_common *priv,
			struct wsm_buf *buf,
			void *arg, u16 cmd, long tmo)
{
	size_t buf_len = buf->data - buf->begin;
	int ret;

	/* Don't bother if we're dead. */
	if (priv->bh_error) {
		ret = 0;
		goto done;
	}

	/* Block until the cmd buffer is completed.  Tortuous. */
	spin_lock(&priv->wsm_cmd.lock);
	while (!priv->wsm_cmd.done) {
		spin_unlock(&priv->wsm_cmd.lock);
		spin_lock(&priv->wsm_cmd.lock);
	}
	priv->wsm_cmd.done = 0;
	spin_unlock(&priv->wsm_cmd.lock);

	if (cmd == WSM_WRITE_MIB_REQ_ID ||
	    cmd == WSM_READ_MIB_REQ_ID)
		pr_debug("[WSM] >>> 0x%.4X [MIB: 0x%.4X] (%zu)\n",
			 cmd, __le16_to_cpu(((__le16 *)buf->begin)[2]),
			 buf_len);
	else
		pr_debug("[WSM] >>> 0x%.4X (%zu)\n", cmd, buf_len);

	/* Due to buggy SPI on CW1200, we need to
	 * pad the message by a few bytes to ensure
	 * that it's completely received.
	 */
	buf_len += 4;

	/* Fill HI message header */
	/* BH will add sequence number */
	((__le16 *)buf->begin)[0] = __cpu_to_le16(buf_len);
	((__le16 *)buf->begin)[1] = __cpu_to_le16(cmd);

	spin_lock(&priv->wsm_cmd.lock);
	BUG_ON(priv->wsm_cmd.ptr);
	priv->wsm_cmd.ptr = buf->begin;
	priv->wsm_cmd.len = buf_len;
	priv->wsm_cmd.arg = arg;
	priv->wsm_cmd.cmd = cmd;
	spin_unlock(&priv->wsm_cmd.lock);

	cw1200_bh_wakeup(priv);

	/* Wait for command completion */
	ret = wait_event_timeout(priv->wsm_cmd_wq,
				 priv->wsm_cmd.done, tmo);

	if (!ret && !priv->wsm_cmd.done) {
		spin_lock(&priv->wsm_cmd.lock);
		priv->wsm_cmd.done = 1;
		priv->wsm_cmd.ptr = NULL;
		spin_unlock(&priv->wsm_cmd.lock);
		if (priv->bh_error) {
			/* Return ok to help system cleanup */
			ret = 0;
		} else {
			pr_err("CMD req (0x%04x) stuck in firmware, killing BH\n", priv->wsm_cmd.cmd);
			print_hex_dump_bytes("REQDUMP: ", DUMP_PREFIX_NONE,
					     buf->begin, buf_len);
			pr_err("Outstanding outgoing frames:  %d\n", priv->hw_bufs_used);

			/* Kill BH thread to report the error to the top layer. */
			atomic_inc(&priv->bh_term);
			wake_up(&priv->bh_wq);
			ret = -ETIMEDOUT;
		}
	} else {
		spin_lock(&priv->wsm_cmd.lock);
		BUG_ON(!priv->wsm_cmd.done);
		ret = priv->wsm_cmd.ret;
		spin_unlock(&priv->wsm_cmd.lock);
	}
done:
	wsm_buf_reset(buf);
	return ret;
}

/* ******************************************************************** */
/* WSM TX port control							*/

void wsm_lock_tx(struct cw1200_common *priv)
{
	wsm_cmd_lock(priv);
	if (atomic_inc_return(&priv->tx_lock) == 1) {
		if (wsm_flush_tx(priv))
			pr_debug("[WSM] TX is locked.\n");
	}
	wsm_cmd_unlock(priv);
}

void wsm_lock_tx_async(struct cw1200_common *priv)
{
	if (atomic_inc_return(&priv->tx_lock) == 1)
		pr_debug("[WSM] TX is locked (async).\n");
}

bool wsm_flush_tx(struct cw1200_common *priv)
{
	unsigned long timestamp = jiffies;
	bool pending = false;
	long timeout;
	int i;

	/* Flush must be called with TX lock held. */
	BUG_ON(!atomic_read(&priv->tx_lock));

	/* First check if we really need to do something.
	 * It is safe to use unprotected access, as hw_bufs_used
	 * can only decrements.
	 */
	if (!priv->hw_bufs_used)
		return true;

	if (priv->bh_error) {
		/* In case of failure do not wait for magic. */
		pr_err("[WSM] Fatal error occurred, will not flush TX.\n");
		return false;
	} else {
		/* Get a timestamp of "oldest" frame */
		for (i = 0; i < 4; ++i)
			pending |= cw1200_queue_get_xmit_timestamp(
					&priv->tx_queue[i],
					&timestamp, 0xffffffff);
		/* If there's nothing pending, we're good */
		if (!pending)
			return true;

		timeout = timestamp + WSM_CMD_LAST_CHANCE_TIMEOUT - jiffies;
		if (timeout < 0 || wait_event_timeout(priv->bh_evt_wq,
						      !priv->hw_bufs_used,
						      timeout) <= 0) {
			/* Hmmm... Not good. Frame had stuck in firmware. */
			priv->bh_error = 1;
			wiphy_err(priv->hw->wiphy, "[WSM] TX Frames (%d) stuck in firmware, killing BH\n", priv->hw_bufs_used);
			wake_up(&priv->bh_wq);
			return false;
		}

		/* Ok, everything is flushed. */
		return true;
	}
}

void wsm_unlock_tx(struct cw1200_common *priv)
{
	int tx_lock;
	tx_lock = atomic_dec_return(&priv->tx_lock);
	BUG_ON(tx_lock < 0);

	if (tx_lock == 0) {
		if (!priv->bh_error)
			cw1200_bh_wakeup(priv);
		pr_debug("[WSM] TX is unlocked.\n");
	}
}

/* ******************************************************************** */
/* WSM RX								*/

int wsm_handle_exception(struct cw1200_common *priv, u8 *data, size_t len)
{
	struct wsm_buf buf;
	u32 reason;
	u32 reg[18];
	char fname[48];
	unsigned int i;

	static const char * const reason_str[] = {
		"undefined instruction",
		"prefetch abort",
		"data abort",
		"unknown error",
	};

	buf.begin = buf.data = data;
	buf.end = &buf.begin[len];

	reason = WSM_GET32(&buf);
	for (i = 0; i < ARRAY_SIZE(reg); ++i)
		reg[i] = WSM_GET32(&buf);
	WSM_GET(&buf, fname, sizeof(fname));

	if (reason < 4)
		wiphy_err(priv->hw->wiphy,
			  "Firmware exception: %s.\n",
			  reason_str[reason]);
	else
		wiphy_err(priv->hw->wiphy,
			  "Firmware assert at %.*s, line %d\n",
			  (int) sizeof(fname), fname, reg[1]);

	for (i = 0; i < 12; i += 4)
		wiphy_err(priv->hw->wiphy,
			  "R%d: 0x%.8X, R%d: 0x%.8X, R%d: 0x%.8X, R%d: 0x%.8X,\n",
			  i + 0, reg[i + 0], i + 1, reg[i + 1],
			  i + 2, reg[i + 2], i + 3, reg[i + 3]);
	wiphy_err(priv->hw->wiphy,
		  "R12: 0x%.8X, SP: 0x%.8X, LR: 0x%.8X, PC: 0x%.8X,\n",
		  reg[i + 0], reg[i + 1], reg[i + 2], reg[i + 3]);
	i += 4;
	wiphy_err(priv->hw->wiphy,
		  "CPSR: 0x%.8X, SPSR: 0x%.8X\n",
		  reg[i + 0], reg[i + 1]);

	print_hex_dump_bytes("R1: ", DUMP_PREFIX_NONE,
			     fname, sizeof(fname));
	return 0;

underflow:
	wiphy_err(priv->hw->wiphy, "Firmware exception.\n");
	print_hex_dump_bytes("Exception: ", DUMP_PREFIX_NONE,
			     data, len);
	return -EINVAL;
}

int wsm_handle_rx(struct cw1200_common *priv, u16 id,
		  struct wsm_hdr *wsm, struct sk_buff **skb_p)
{
	int ret = 0;
	struct wsm_buf wsm_buf;
	int link_id = (id >> 6) & 0x0F;

	/* Strip link id. */
	id &= ~WSM_TX_LINK_ID(WSM_TX_LINK_ID_MAX);

	wsm_buf.begin = (u8 *)&wsm[0];
	wsm_buf.data = (u8 *)&wsm[1];
	wsm_buf.end = &wsm_buf.begin[__le16_to_cpu(wsm->len)];

	pr_debug("[WSM] <<< 0x%.4X (%td)\n", id,
		 wsm_buf.end - wsm_buf.begin);

	if (id == WSM_TX_CONFIRM_IND_ID) {
		ret = wsm_tx_confirm(priv, &wsm_buf, link_id);
	} else if (id == WSM_MULTI_TX_CONFIRM_ID) {
		ret = wsm_multi_tx_confirm(priv, &wsm_buf, link_id);
	} else if (id & 0x0400) {
		void *wsm_arg;
		u16 wsm_cmd;

		/* Do not trust FW too much. Protection against repeated
		 * response and race condition removal (see above).
		 */
		spin_lock(&priv->wsm_cmd.lock);
		wsm_arg = priv->wsm_cmd.arg;
		wsm_cmd = priv->wsm_cmd.cmd &
				~WSM_TX_LINK_ID(WSM_TX_LINK_ID_MAX);
		priv->wsm_cmd.cmd = 0xFFFF;
		spin_unlock(&priv->wsm_cmd.lock);

		if (WARN_ON((id & ~0x0400) != wsm_cmd)) {
			/* Note that any non-zero is a fatal retcode. */
			ret = -EINVAL;
			goto out;
		}

		/* Note that wsm_arg can be NULL in case of timeout in
		 * wsm_cmd_send().
		 */

		switch (id) {
		case WSM_READ_MIB_RESP_ID:
			if (wsm_arg)
				ret = wsm_read_mib_confirm(priv, wsm_arg,
								&wsm_buf);
			break;
		case WSM_WRITE_MIB_RESP_ID:
			if (wsm_arg)
				ret = wsm_write_mib_confirm(priv, wsm_arg,
							    &wsm_buf);
			break;
		case WSM_START_SCAN_RESP_ID:
			if (wsm_arg)
				ret = wsm_scan_started(priv, wsm_arg, &wsm_buf);
			break;
		case WSM_CONFIGURATION_RESP_ID:
			if (wsm_arg)
				ret = wsm_configuration_confirm(priv, wsm_arg,
								&wsm_buf);
			break;
		case WSM_JOIN_RESP_ID:
			if (wsm_arg)
				ret = wsm_join_confirm(priv, wsm_arg, &wsm_buf);
			break;
		case WSM_STOP_SCAN_RESP_ID:
		case WSM_RESET_RESP_ID:
		case WSM_ADD_KEY_RESP_ID:
		case WSM_REMOVE_KEY_RESP_ID:
		case WSM_SET_PM_RESP_ID:
		case WSM_SET_BSS_PARAMS_RESP_ID:
		case 0x0412: /* set_tx_queue_params */
		case WSM_EDCA_PARAMS_RESP_ID:
		case WSM_SWITCH_CHANNEL_RESP_ID:
		case WSM_START_RESP_ID:
		case WSM_BEACON_TRANSMIT_RESP_ID:
		case 0x0419: /* start_find */
		case 0x041A: /* stop_find */
		case 0x041B: /* update_ie */
		case 0x041C: /* map_link */
			WARN_ON(wsm_arg != NULL);
			ret = wsm_generic_confirm(priv, wsm_arg, &wsm_buf);
			if (ret) {
				wiphy_warn(priv->hw->wiphy,
					   "wsm_generic_confirm failed for request 0x%04x.\n",
					   id & ~0x0400);

				/* often 0x407 and 0x410 occur, this means we're dead.. */
				if (priv->join_status >= CW1200_JOIN_STATUS_JOINING) {
					wsm_lock_tx(priv);
					if (queue_work(priv->workqueue, &priv->unjoin_work) <= 0)
						wsm_unlock_tx(priv);
				}
			}
			break;
		default:
			wiphy_warn(priv->hw->wiphy,
				   "Unrecognized confirmation 0x%04x\n",
				   id & ~0x0400);
		}

		spin_lock(&priv->wsm_cmd.lock);
		priv->wsm_cmd.ret = ret;
		priv->wsm_cmd.done = 1;
		spin_unlock(&priv->wsm_cmd.lock);

		ret = 0; /* Error response from device should ne stop BH. */

		wake_up(&priv->wsm_cmd_wq);
	} else if (id & 0x0800) {
		switch (id) {
		case WSM_STARTUP_IND_ID:
			ret = wsm_startup_indication(priv, &wsm_buf);
			break;
		case WSM_RECEIVE_IND_ID:
			ret = wsm_receive_indication(priv, link_id,
						     &wsm_buf, skb_p);
			break;
		case 0x0805:
			ret = wsm_event_indication(priv, &wsm_buf);
			break;
		case WSM_SCAN_COMPLETE_IND_ID:
			ret = wsm_scan_complete_indication(priv, &wsm_buf);
			break;
		case 0x0808:
			ret = wsm_ba_timeout_indication(priv, &wsm_buf);
			break;
		case 0x0809:
			ret = wsm_set_pm_indication(priv, &wsm_buf);
			break;
		case 0x080A:
			ret = wsm_channel_switch_indication(priv, &wsm_buf);
			break;
		case 0x080B:
			ret = wsm_find_complete_indication(priv, &wsm_buf);
			break;
		case 0x080C:
			ret = wsm_suspend_resume_indication(priv,
					link_id, &wsm_buf);
			break;
		case 0x080F:
			ret = wsm_join_complete_indication(priv, &wsm_buf);
			break;
		default:
			pr_warn("Unrecognised WSM ID %04x\n", id);
		}
	} else {
		WARN_ON(1);
		ret = -EINVAL;
	}
out:
	return ret;
}

static bool wsm_handle_tx_data(struct cw1200_common *priv,
			       struct wsm_tx *wsm,
			       const struct ieee80211_tx_info *tx_info,
			       const struct cw1200_txpriv *txpriv,
			       struct cw1200_queue *queue)
{
	bool handled = false;
	const struct ieee80211_hdr *frame =
		(struct ieee80211_hdr *)&((u8 *)wsm)[txpriv->offset];
	__le16 fctl = frame->frame_control;
	enum {
		do_probe,
		do_drop,
		do_wep,
		do_tx,
	} action = do_tx;

	switch (priv->mode) {
	case NL80211_IFTYPE_STATION:
		if (priv->join_status == CW1200_JOIN_STATUS_MONITOR)
			action = do_tx;
		else if (priv->join_status < CW1200_JOIN_STATUS_PRE_STA)
			action = do_drop;
		break;
	case NL80211_IFTYPE_AP:
		if (!priv->join_status) {
			action = do_drop;
		} else if (!(BIT(txpriv->raw_link_id) &
			     (BIT(0) | priv->link_id_map))) {
			wiphy_warn(priv->hw->wiphy,
				   "A frame with expired link id is dropped.\n");
			action = do_drop;
		}
		if (cw1200_queue_get_generation(wsm->packet_id) >
				CW1200_MAX_REQUEUE_ATTEMPTS) {
			/* HACK!!! WSM324 firmware has tendency to requeue
			 * multicast frames in a loop, causing performance
			 * drop and high power consumption of the driver.
			 * In this situation it is better just to drop
			 * the problematic frame.
			 */
			wiphy_warn(priv->hw->wiphy,
				   "Too many attempts to requeue a frame; dropped.\n");
			action = do_drop;
		}
		break;
	case NL80211_IFTYPE_ADHOC:
		if (priv->join_status != CW1200_JOIN_STATUS_IBSS)
			action = do_drop;
		break;
	case NL80211_IFTYPE_MESH_POINT:
		action = do_tx; /* TODO:  Test me! */
		break;
	case NL80211_IFTYPE_MONITOR:
	default:
		action = do_drop;
		break;
	}

	if (action == do_tx) {
		if (ieee80211_is_nullfunc(fctl)) {
			spin_lock(&priv->bss_loss_lock);
			if (priv->bss_loss_state) {
				priv->bss_loss_confirm_id = wsm->packet_id;
				wsm->queue_id = WSM_QUEUE_VOICE;
			}
			spin_unlock(&priv->bss_loss_lock);
		} else if (ieee80211_is_probe_req(fctl)) {
			action = do_probe;
		} else if (ieee80211_is_deauth(fctl) &&
			   priv->mode != NL80211_IFTYPE_AP) {
			pr_debug("[WSM] Issue unjoin command due to tx deauth.\n");
			wsm_lock_tx_async(priv);
			if (queue_work(priv->workqueue,
				       &priv->unjoin_work) <= 0)
				wsm_unlock_tx(priv);
		} else if (ieee80211_has_protected(fctl) &&
			   tx_info->control.hw_key &&
			   tx_info->control.hw_key->keyidx != priv->wep_default_key_id &&
			   (tx_info->control.hw_key->cipher == WLAN_CIPHER_SUITE_WEP40 ||
			    tx_info->control.hw_key->cipher == WLAN_CIPHER_SUITE_WEP104)) {
			action = do_wep;
		}
	}

	switch (action) {
	case do_probe:
		/* An interesting FW "feature". Device filters probe responses.
		 * The easiest way to get it back is to convert
		 * probe request into WSM start_scan command.
		 */
		pr_debug("[WSM] Convert probe request to scan.\n");
		wsm_lock_tx_async(priv);
		priv->pending_frame_id = wsm->packet_id;
		if (queue_delayed_work(priv->workqueue,
				       &priv->scan.probe_work, 0) <= 0)
			wsm_unlock_tx(priv);
		handled = true;
		break;
	case do_drop:
		pr_debug("[WSM] Drop frame (0x%.4X).\n", fctl);
		BUG_ON(cw1200_queue_remove(queue, wsm->packet_id));
		handled = true;
		break;
	case do_wep:
		pr_debug("[WSM] Issue set_default_wep_key.\n");
		wsm_lock_tx_async(priv);
		priv->wep_default_key_id = tx_info->control.hw_key->keyidx;
		priv->pending_frame_id = wsm->packet_id;
		if (queue_work(priv->workqueue, &priv->wep_key_work) <= 0)
			wsm_unlock_tx(priv);
		handled = true;
		break;
	case do_tx:
		pr_debug("[WSM] Transmit frame.\n");
		break;
	default:
		/* Do nothing */
		break;
	}
	return handled;
}

static int cw1200_get_prio_queue(struct cw1200_common *priv,
				 u32 link_id_map, int *total)
{
	static const int urgent = BIT(CW1200_LINK_ID_AFTER_DTIM) |
		BIT(CW1200_LINK_ID_UAPSD);
	struct wsm_edca_queue_params *edca;
	unsigned score, best = -1;
	int winner = -1;
	int queued;
	int i;

	/* search for a winner using edca params */
	for (i = 0; i < 4; ++i) {
		queued = cw1200_queue_get_num_queued(&priv->tx_queue[i],
				link_id_map);
		if (!queued)
			continue;
		*total += queued;
		edca = &priv->edca.params[i];
		score = ((edca->aifns + edca->cwmin) << 16) +
			((edca->cwmax - edca->cwmin) *
			 (get_random_int() & 0xFFFF));
		if (score < best && (winner < 0 || i != 3)) {
			best = score;
			winner = i;
		}
	}

	/* override winner if bursting */
	if (winner >= 0 && priv->tx_burst_idx >= 0 &&
	    winner != priv->tx_burst_idx &&
	    !cw1200_queue_get_num_queued(
		    &priv->tx_queue[winner],
		    link_id_map & urgent) &&
	    cw1200_queue_get_num_queued(
		    &priv->tx_queue[priv->tx_burst_idx],
		    link_id_map))
		winner = priv->tx_burst_idx;

	return winner;
}

static int wsm_get_tx_queue_and_mask(struct cw1200_common *priv,
				     struct cw1200_queue **queue_p,
				     u32 *tx_allowed_mask_p,
				     bool *more)
{
	int idx;
	u32 tx_allowed_mask;
	int total = 0;

	/* Search for a queue with multicast frames buffered */
	if (priv->tx_multicast) {
		tx_allowed_mask = BIT(CW1200_LINK_ID_AFTER_DTIM);
		idx = cw1200_get_prio_queue(priv,
				tx_allowed_mask, &total);
		if (idx >= 0) {
			*more = total > 1;
			goto found;
		}
	}

	/* Search for unicast traffic */
	tx_allowed_mask = ~priv->sta_asleep_mask;
	tx_allowed_mask |= BIT(CW1200_LINK_ID_UAPSD);
	if (priv->sta_asleep_mask) {
		tx_allowed_mask |= priv->pspoll_mask;
		tx_allowed_mask &= ~BIT(CW1200_LINK_ID_AFTER_DTIM);
	} else {
		tx_allowed_mask |= BIT(CW1200_LINK_ID_AFTER_DTIM);
	}
	idx = cw1200_get_prio_queue(priv,
			tx_allowed_mask, &total);
	if (idx < 0)
		return -ENOENT;

found:
	*queue_p = &priv->tx_queue[idx];
	*tx_allowed_mask_p = tx_allowed_mask;
	return 0;
}

int wsm_get_tx(struct cw1200_common *priv, u8 **data,
	       size_t *tx_len, int *burst)
{
	struct wsm_tx *wsm = NULL;
	struct ieee80211_tx_info *tx_info;
	struct cw1200_queue *queue = NULL;
	int queue_num;
	u32 tx_allowed_mask = 0;
	const struct cw1200_txpriv *txpriv = NULL;
	int count = 0;

	/* More is used only for broadcasts. */
	bool more = false;

	if (priv->wsm_cmd.ptr) { /* CMD request */
		++count;
		spin_lock(&priv->wsm_cmd.lock);
		BUG_ON(!priv->wsm_cmd.ptr);
		*data = priv->wsm_cmd.ptr;
		*tx_len = priv->wsm_cmd.len;
		*burst = 1;
		spin_unlock(&priv->wsm_cmd.lock);
	} else {
		for (;;) {
			int ret;

			if (atomic_add_return(0, &priv->tx_lock))
				break;

			spin_lock_bh(&priv->ps_state_lock);

			ret = wsm_get_tx_queue_and_mask(priv, &queue,
							&tx_allowed_mask, &more);
			queue_num = queue - priv->tx_queue;

			if (priv->buffered_multicasts &&
			    (ret || !more) &&
			    (priv->tx_multicast || !priv->sta_asleep_mask)) {
				priv->buffered_multicasts = false;
				if (priv->tx_multicast) {
					priv->tx_multicast = false;
					queue_work(priv->workqueue,
						   &priv->multicast_stop_work);
				}
			}

			spin_unlock_bh(&priv->ps_state_lock);

			if (ret)
				break;

			if (cw1200_queue_get(queue,
					     tx_allowed_mask,
					     &wsm, &tx_info, &txpriv))
				continue;

			if (wsm_handle_tx_data(priv, wsm,
					       tx_info, txpriv, queue))
				continue;  /* Handled by WSM */

			wsm->hdr.id &= __cpu_to_le16(
				~WSM_TX_LINK_ID(WSM_TX_LINK_ID_MAX));
			wsm->hdr.id |= cpu_to_le16(
				WSM_TX_LINK_ID(txpriv->raw_link_id));
			priv->pspoll_mask &= ~BIT(txpriv->raw_link_id);

			*data = (u8 *)wsm;
			*tx_len = __le16_to_cpu(wsm->hdr.len);

			/* allow bursting if txop is set */
			if (priv->edca.params[queue_num].txop_limit)
				*burst = min(*burst,
					     (int)cw1200_queue_get_num_queued(queue, tx_allowed_mask) + 1);
			else
				*burst = 1;

			/* store index of bursting queue */
			if (*burst > 1)
				priv->tx_burst_idx = queue_num;
			else
				priv->tx_burst_idx = -1;

			if (more) {
				struct ieee80211_hdr *hdr =
					(struct ieee80211_hdr *)
					&((u8 *)wsm)[txpriv->offset];
				/* more buffered multicast/broadcast frames
				 *  ==> set MoreData flag in IEEE 802.11 header
				 *  to inform PS STAs
				 */
				hdr->frame_control |=
					cpu_to_le16(IEEE80211_FCTL_MOREDATA);
			}

			pr_debug("[WSM] >>> 0x%.4X (%zu) %p %c\n",
				 0x0004, *tx_len, *data,
				 wsm->more ? 'M' : ' ');
			++count;
			break;
		}
	}

	return count;
}

void wsm_txed(struct cw1200_common *priv, u8 *data)
{
	if (data == priv->wsm_cmd.ptr) {
		spin_lock(&priv->wsm_cmd.lock);
		priv->wsm_cmd.ptr = NULL;
		spin_unlock(&priv->wsm_cmd.lock);
	}
}

/* ******************************************************************** */
/* WSM buffer								*/

void wsm_buf_init(struct wsm_buf *buf)
{
	BUG_ON(buf->begin);
	buf->begin = kmalloc(FWLOAD_BLOCK_SIZE, GFP_KERNEL | GFP_DMA);
	buf->end = buf->begin ? &buf->begin[FWLOAD_BLOCK_SIZE] : buf->begin;
	wsm_buf_reset(buf);
}

void wsm_buf_deinit(struct wsm_buf *buf)
{
	kfree(buf->begin);
	buf->begin = buf->data = buf->end = NULL;
}

static void wsm_buf_reset(struct wsm_buf *buf)
{
	if (buf->begin) {
		buf->data = &buf->begin[4];
		*(u32 *)buf->begin = 0;
	} else {
		buf->data = buf->begin;
	}
}

static int wsm_buf_reserve(struct wsm_buf *buf, size_t extra_size)
{
	size_t pos = buf->data - buf->begin;
	size_t size = pos + extra_size;
	u8 *tmp;

	size = round_up(size, FWLOAD_BLOCK_SIZE);

	tmp = krealloc(buf->begin, size, GFP_KERNEL | GFP_DMA);
	if (!tmp) {
		wsm_buf_deinit(buf);
		return -ENOMEM;
	}

	buf->begin = tmp;
	buf->data = &buf->begin[pos];
	buf->end = &buf->begin[size];
	return 0;
}
