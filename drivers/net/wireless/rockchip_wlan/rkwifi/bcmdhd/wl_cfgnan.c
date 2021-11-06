/*
 * Neighbor Awareness Networking
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifdef WL_NAN
#include <bcmutils.h>
#include <bcmendian.h>
#include <bcmwifi_channels.h>
#include <nan.h>
#include <bcmiov.h>
#include <net/rtnetlink.h>

#include <wl_cfg80211.h>
#include <wl_cfgscan.h>
#include <wl_android.h>
#include <wl_cfgnan.h>

#if defined(BCMDONGLEHOST)
#include <dngl_stats.h>
#include <dhd.h>
#endif /* BCMDONGLEHOST */
#include <wl_cfgvendor.h>
#include <bcmbloom.h>
#include <wl_cfgp2p.h>
#include <wl_cfgvif.h>
#ifdef RTT_SUPPORT
#include <dhd_rtt.h>
#endif /* RTT_SUPPORT */
#include <bcmstdlib_s.h>

#define NAN_RANGE_REQ_EVNT 1
#define NAN_RAND_MAC_RETRIES 10
#define NAN_SCAN_DWELL_TIME_DELTA_MS 10

#ifdef WL_NAN_DISC_CACHE
/* Disc Cache Parameters update Flags */
#define NAN_DISC_CACHE_PARAM_SDE_CONTROL	0x0001
static int wl_cfgnan_cache_disc_result(struct bcm_cfg80211 *cfg, void * data,
	u16 *disc_cache_update_flags);
static int wl_cfgnan_remove_disc_result(struct bcm_cfg80211 * cfg, uint8 local_subid);
static nan_disc_result_cache * wl_cfgnan_get_disc_result(struct bcm_cfg80211 *cfg,
	uint8 remote_pubid, struct ether_addr *peer);
#endif /* WL_NAN_DISC_CACHE */

static int wl_cfgnan_set_if_addr(struct bcm_cfg80211 *cfg);
static int wl_cfgnan_get_capability(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_hal_capabilities_t *capabilities);
static void wl_cfgnan_clear_nan_event_data(struct bcm_cfg80211 *cfg,
	nan_event_data_t *nan_event_data);
void wl_cfgnan_data_remove_peer(struct bcm_cfg80211 *cfg,
        struct ether_addr *peer_addr);
static void wl_cfgnan_send_stop_event(struct bcm_cfg80211 *cfg);
static void wl_cfgnan_disable_cleanup(struct bcm_cfg80211 *cfg);
static s32 wl_cfgnan_get_ndi_idx(struct bcm_cfg80211 *cfg);
static int wl_cfgnan_init(struct bcm_cfg80211 *cfg);
static int wl_cfgnan_deinit(struct bcm_cfg80211 *cfg, uint8 busstate);
static void wl_cfgnan_update_dp_info(struct bcm_cfg80211 *cfg, bool add,
	nan_data_path_id ndp_id);
static void wl_cfgnan_data_set_peer_dp_state(struct bcm_cfg80211 *cfg,
	struct ether_addr *peer_addr, nan_peer_dp_state_t state);
static nan_ndp_peer_t* wl_cfgnan_data_get_peer(struct bcm_cfg80211 *cfg,
	struct ether_addr *peer_addr);
static int wl_cfgnan_disable(struct bcm_cfg80211 *cfg);
static s32 wl_cfgnan_del_ndi_data(struct bcm_cfg80211 *cfg, char *name);
static s32 wl_cfgnan_add_ndi_data(struct bcm_cfg80211 *cfg, s32 idx, char *name);

#ifdef RTT_SUPPORT
static int wl_cfgnan_clear_disc_cache(struct bcm_cfg80211 *cfg, wl_nan_instance_id_t sub_id);
static int32 wl_cfgnan_notify_disc_with_ranging(struct bcm_cfg80211 *cfg,
	nan_ranging_inst_t *rng_inst, nan_event_data_t *nan_event_data, uint32 distance);
static void wl_cfgnan_disc_result_on_geofence_cancel(struct bcm_cfg80211 *cfg,
	nan_ranging_inst_t *rng_inst);
static void wl_cfgnan_terminate_ranging_session(struct bcm_cfg80211 *cfg,
	nan_ranging_inst_t *ranging_inst);
static s32 wl_cfgnan_clear_peer_ranging(struct bcm_cfg80211 * cfg,
	nan_ranging_inst_t *rng_inst, int reason);
static s32 wl_cfgnan_handle_dp_ranging_concurrency(struct bcm_cfg80211 *cfg,
	struct ether_addr *peer, int reason);
static void wl_cfgnan_terminate_all_obsolete_ranging_sessions(struct bcm_cfg80211 *cfg);
static bool wl_ranging_geofence_session_with_peer(struct bcm_cfg80211 *cfg,
	struct ether_addr *peer_addr);
static void wl_cfgnan_reset_remove_ranging_instance(struct bcm_cfg80211 *cfg,
	nan_ranging_inst_t *ranging_inst);
static void wl_cfgnan_remove_ranging_instance(struct bcm_cfg80211 *cfg,
	nan_ranging_inst_t *ranging_inst);
#endif /* RTT_SUPPORT */

static const char *
nan_role_to_str(u8 role)
{
	const char *id2str;

	switch (role) {
		C2S(WL_NAN_ROLE_AUTO);
			break;
		C2S(WL_NAN_ROLE_NON_MASTER_NON_SYNC);
			break;
		C2S(WL_NAN_ROLE_NON_MASTER_SYNC);
			break;
		C2S(WL_NAN_ROLE_MASTER);
			break;
		C2S(WL_NAN_ROLE_ANCHOR_MASTER);
			break;
		default:
			id2str = "WL_NAN_ROLE_UNKNOWN";
	}

	return id2str;
}

const char *
nan_event_to_str(u16 cmd)
{
	const char *id2str;

	switch (cmd) {
	C2S(WL_NAN_EVENT_START);
		break;
	C2S(WL_NAN_EVENT_JOIN);
		break;
	C2S(WL_NAN_EVENT_ROLE);
		break;
	C2S(WL_NAN_EVENT_SCAN_COMPLETE);
		break;
	C2S(WL_NAN_EVENT_DISCOVERY_RESULT);
		break;
	C2S(WL_NAN_EVENT_REPLIED);
		break;
	C2S(WL_NAN_EVENT_TERMINATED);
		break;
	C2S(WL_NAN_EVENT_RECEIVE);
		break;
	C2S(WL_NAN_EVENT_STATUS_CHG);
		break;
	C2S(WL_NAN_EVENT_MERGE);
		break;
	C2S(WL_NAN_EVENT_STOP);
		break;
	C2S(WL_NAN_EVENT_P2P);
		break;
	C2S(WL_NAN_EVENT_WINDOW_BEGIN_P2P);
		break;
	C2S(WL_NAN_EVENT_WINDOW_BEGIN_MESH);
		break;
	C2S(WL_NAN_EVENT_WINDOW_BEGIN_IBSS);
		break;
	C2S(WL_NAN_EVENT_WINDOW_BEGIN_RANGING);
		break;
	C2S(WL_NAN_EVENT_POST_DISC);
		break;
	C2S(WL_NAN_EVENT_DATA_IF_ADD);
		break;
	C2S(WL_NAN_EVENT_DATA_PEER_ADD);
		break;
	C2S(WL_NAN_EVENT_PEER_DATAPATH_IND);
		break;
	C2S(WL_NAN_EVENT_DATAPATH_ESTB);
		break;
	C2S(WL_NAN_EVENT_SDF_RX);
		break;
	C2S(WL_NAN_EVENT_DATAPATH_END);
		break;
	C2S(WL_NAN_EVENT_BCN_RX);
		break;
	C2S(WL_NAN_EVENT_PEER_DATAPATH_RESP);
		break;
	C2S(WL_NAN_EVENT_PEER_DATAPATH_CONF);
		break;
	C2S(WL_NAN_EVENT_RNG_REQ_IND);
		break;
	C2S(WL_NAN_EVENT_RNG_RPT_IND);
		break;
	C2S(WL_NAN_EVENT_RNG_TERM_IND);
		break;
	C2S(WL_NAN_EVENT_PEER_DATAPATH_SEC_INST);
		break;
	C2S(WL_NAN_EVENT_TXS);
		break;
	C2S(WL_NAN_EVENT_DW_START);
		break;
	C2S(WL_NAN_EVENT_DW_END);
		break;
	C2S(WL_NAN_EVENT_CHAN_BOUNDARY);
		break;
	C2S(WL_NAN_EVENT_MR_CHANGED);
		break;
	C2S(WL_NAN_EVENT_RNG_RESP_IND);
		break;
	C2S(WL_NAN_EVENT_PEER_SCHED_UPD_NOTIF);
		break;
	C2S(WL_NAN_EVENT_PEER_SCHED_REQ);
		break;
	C2S(WL_NAN_EVENT_PEER_SCHED_RESP);
		break;
	C2S(WL_NAN_EVENT_PEER_SCHED_CONF);
		break;
	C2S(WL_NAN_EVENT_SENT_DATAPATH_END);
		break;
	C2S(WL_NAN_EVENT_SLOT_START);
		break;
	C2S(WL_NAN_EVENT_SLOT_END);
		break;
	C2S(WL_NAN_EVENT_HOST_ASSIST_REQ);
		break;
	C2S(WL_NAN_EVENT_RX_MGMT_FRM);
		break;
	C2S(WL_NAN_EVENT_DISC_CACHE_TIMEOUT);
		break;
	C2S(WL_NAN_EVENT_OOB_AF_TXS);
		break;
	C2S(WL_NAN_EVENT_OOB_AF_RX);
		break;
	C2S(WL_NAN_EVENT_INVALID);
		break;

	default:
		id2str = "WL_NAN_EVENT_UNKNOWN";
	}

	return id2str;
}

static const char *
nan_frm_type_to_str(u16 frm_type)
{
	const char *id2str;

	switch (frm_type) {
	C2S(WL_NAN_FRM_TYPE_PUBLISH);
		break;
	C2S(WL_NAN_FRM_TYPE_SUBSCRIBE);
		break;
	C2S(WL_NAN_FRM_TYPE_FOLLOWUP);
		break;

	C2S(WL_NAN_FRM_TYPE_DP_REQ);
		break;
	C2S(WL_NAN_FRM_TYPE_DP_RESP);
		break;
	C2S(WL_NAN_FRM_TYPE_DP_CONF);
		break;
	C2S(WL_NAN_FRM_TYPE_DP_INSTALL);
		break;
	C2S(WL_NAN_FRM_TYPE_DP_END);
		break;

	C2S(WL_NAN_FRM_TYPE_SCHED_REQ);
		break;
	C2S(WL_NAN_FRM_TYPE_SCHED_RESP);
		break;
	C2S(WL_NAN_FRM_TYPE_SCHED_CONF);
		break;
	C2S(WL_NAN_FRM_TYPE_SCHED_UPD);
		break;

	C2S(WL_NAN_FRM_TYPE_RNG_REQ);
		break;
	C2S(WL_NAN_FRM_TYPE_RNG_RESP);
		break;
	C2S(WL_NAN_FRM_TYPE_RNG_TERM);
		break;
	C2S(WL_NAN_FRM_TYPE_RNG_REPORT);
		break;

	default:
		id2str = "WL_NAN_FRM_TYPE_UNKNOWN";
	}

	return id2str;
}

static const char *
nan_event_cause_to_str(u8 cause)
{
	const char *id2str;

	switch (cause) {
	C2S(WL_NAN_DP_TERM_WITH_INACTIVITY);
		break;
	C2S(WL_NAN_DP_TERM_WITH_FSM_DESTROY);
		break;
	C2S(WL_NAN_DP_TERM_WITH_PEER_DP_END);
		break;
	C2S(WL_NAN_DP_TERM_WITH_STALE_NDP);
		break;
	C2S(WL_NAN_DP_TERM_WITH_DISABLE);
		break;
	C2S(WL_NAN_DP_TERM_WITH_NDI_DEL);
		break;
	C2S(WL_NAN_DP_TERM_WITH_PEER_HB_FAIL);
		break;
	C2S(WL_NAN_DP_TERM_WITH_HOST_IOVAR);
		break;
	C2S(WL_NAN_DP_TERM_WITH_ESTB_FAIL);
		break;
	C2S(WL_NAN_DP_TERM_WITH_SCHED_REJECT);
		break;

	default:
		id2str = "WL_NAN_EVENT_CAUSE_UNKNOWN";
	}

	return id2str;
}

static int wl_cfgnan_execute_ioctl(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, bcm_iov_batch_buf_t *nan_buf,
	uint16 nan_buf_size, uint32 *status, uint8 *resp_buf,
	uint16 resp_buf_len);
int
wl_cfgnan_generate_inst_id(struct bcm_cfg80211 *cfg, uint8 *p_inst_id)
{
	s32 ret = BCME_OK;
	uint8 i = 0;
	wl_nancfg_t *nancfg = cfg->nancfg;

	if (p_inst_id == NULL) {
		WL_ERR(("Invalid arguments\n"));
		ret = -EINVAL;
		goto exit;
	}

	if (nancfg->inst_id_start == NAN_ID_MAX) {
		WL_ERR(("Consumed all IDs, resetting the counter\n"));
		nancfg->inst_id_start = 0;
	}

	for (i = nancfg->inst_id_start; i < NAN_ID_MAX; i++) {
		if (isclr(nancfg->svc_inst_id_mask, i)) {
			setbit(nancfg->svc_inst_id_mask, i);
			*p_inst_id = i + 1;
			nancfg->inst_id_start = *p_inst_id;
			WL_DBG(("Instance ID=%d\n", *p_inst_id));
			goto exit;
		}
	}
	WL_ERR(("Allocated maximum IDs\n"));
	ret = BCME_NORESOURCE;
exit:
	return ret;
}

int
wl_cfgnan_remove_inst_id(struct bcm_cfg80211 *cfg, uint8 inst_id)
{
	s32 ret = BCME_OK;
	WL_DBG(("%s: Removing svc instance id %d\n", __FUNCTION__, inst_id));
	clrbit(cfg->nancfg->svc_inst_id_mask, inst_id-1);
	return ret;
}
s32 wl_cfgnan_parse_sdea_data(osl_t *osh, const uint8 *p_attr,
		uint16 len, nan_event_data_t *tlv_data)
{
	const wifi_nan_svc_desc_ext_attr_t *nan_svc_desc_ext_attr = NULL;
	uint8 offset;
	s32 ret = BCME_OK;

	/* service descriptor ext attributes */
	nan_svc_desc_ext_attr = (const wifi_nan_svc_desc_ext_attr_t *)p_attr;

	/* attribute ID */
	WL_TRACE(("> attr id: 0x%02x\n", nan_svc_desc_ext_attr->id));

	/* attribute length */
	WL_TRACE(("> attr len: 0x%x\n", nan_svc_desc_ext_attr->len));
	if (nan_svc_desc_ext_attr->instance_id == tlv_data->pub_id) {
		tlv_data->sde_control_flag = nan_svc_desc_ext_attr->control;
	}
	offset = sizeof(*nan_svc_desc_ext_attr);
	if (offset > len) {
		WL_ERR(("Invalid event buffer len\n"));
		ret = BCME_BUFTOOSHORT;
		goto fail;
	}
	p_attr += offset;
	len -= offset;

	if (tlv_data->sde_control_flag & NAN_SC_RANGE_LIMITED) {
		WL_TRACE(("> svc_control: range limited present\n"));
	}
	if (tlv_data->sde_control_flag & NAN_SDE_CF_SVC_UPD_IND_PRESENT) {
		WL_TRACE(("> svc_control: sdea svc specific info present\n"));
		tlv_data->sde_svc_info.dlen = (p_attr[1] | (p_attr[2] << 8));
		WL_TRACE(("> sdea svc info len: 0x%02x\n", tlv_data->sde_svc_info.dlen));
		if (!tlv_data->sde_svc_info.dlen ||
				tlv_data->sde_svc_info.dlen > NAN_MAX_SERVICE_SPECIFIC_INFO_LEN) {
			/* must be able to handle null msg which is not error */
			tlv_data->sde_svc_info.dlen = 0;
			WL_ERR(("sde data length is invalid\n"));
			ret = BCME_BADLEN;
			goto fail;
		}

		if (tlv_data->sde_svc_info.dlen > 0) {
			tlv_data->sde_svc_info.data = MALLOCZ(osh, tlv_data->sde_svc_info.dlen);
			if (!tlv_data->sde_svc_info.data) {
				WL_ERR(("%s: memory allocation failed\n", __FUNCTION__));
				tlv_data->sde_svc_info.dlen = 0;
				ret = BCME_NOMEM;
				goto fail;
			}
			/* advance read pointer, consider sizeof of Service Update Indicator */
			offset = sizeof(tlv_data->sde_svc_info.dlen) - 1;
			if (offset > len) {
				WL_ERR(("Invalid event buffer len\n"));
				ret = BCME_BUFTOOSHORT;
				goto fail;
			}
			p_attr += offset;
			len -= offset;
			ret = memcpy_s(tlv_data->sde_svc_info.data, tlv_data->sde_svc_info.dlen,
				p_attr, tlv_data->sde_svc_info.dlen);
			if (ret != BCME_OK) {
				WL_ERR(("Failed to copy sde_svc_info\n"));
				goto fail;
			}
		} else {
			/* must be able to handle null msg which is not error */
			tlv_data->sde_svc_info.dlen = 0;
			WL_DBG(("%s: sdea svc info length is zero, null info data\n",
				__FUNCTION__));
		}
	}
	return ret;
fail:
	if (tlv_data->sde_svc_info.data) {
		MFREE(osh, tlv_data->sde_svc_info.data,
				tlv_data->sde_svc_info.dlen);
		tlv_data->sde_svc_info.data = NULL;
	}

	WL_DBG(("Parse SDEA event data, status = %d\n", ret));
	return ret;
}

/*
 * This attribute contains some mandatory fields and some optional fields
 * depending on the content of the service discovery request.
 */
s32
wl_cfgnan_parse_sda_data(osl_t *osh, const uint8 *p_attr,
		uint16 len, nan_event_data_t *tlv_data)
{
	uint8 svc_control = 0, offset = 0;
	s32 ret = BCME_OK;
	const wifi_nan_svc_descriptor_attr_t *nan_svc_desc_attr = NULL;

	/* service descriptor attributes */
	nan_svc_desc_attr = (const wifi_nan_svc_descriptor_attr_t *)p_attr;
	/* attribute ID */
	WL_TRACE(("> attr id: 0x%02x\n", nan_svc_desc_attr->id));

	/* attribute length */
	WL_TRACE(("> attr len: 0x%x\n", nan_svc_desc_attr->len));

	/* service ID */
	ret = memcpy_s(tlv_data->svc_name, sizeof(tlv_data->svc_name),
		nan_svc_desc_attr->svc_hash, NAN_SVC_HASH_LEN);
	if (ret != BCME_OK) {
		WL_ERR(("Failed to copy svc_hash_name:\n"));
		return ret;
	}
	WL_TRACE(("> svc_hash_name: " MACDBG "\n", MAC2STRDBG(tlv_data->svc_name)));

	/* local instance ID */
	tlv_data->local_inst_id = nan_svc_desc_attr->instance_id;
	WL_TRACE(("> local instance id: 0x%02x\n", tlv_data->local_inst_id));

	/* requestor instance ID */
	tlv_data->requestor_id = nan_svc_desc_attr->requestor_id;
	WL_TRACE(("> requestor id: 0x%02x\n", tlv_data->requestor_id));

	/* service control */
	svc_control = nan_svc_desc_attr->svc_control;
	if ((svc_control & NAN_SVC_CONTROL_TYPE_MASK) == NAN_SC_PUBLISH) {
		WL_TRACE(("> Service control type: NAN_SC_PUBLISH\n"));
	} else if ((svc_control & NAN_SVC_CONTROL_TYPE_MASK) == NAN_SC_SUBSCRIBE) {
		WL_TRACE(("> Service control type: NAN_SC_SUBSCRIBE\n"));
	} else if ((svc_control & NAN_SVC_CONTROL_TYPE_MASK) == NAN_SC_FOLLOWUP) {
		WL_TRACE(("> Service control type: NAN_SC_FOLLOWUP\n"));
	}
	offset = sizeof(*nan_svc_desc_attr);
	if (offset > len) {
		WL_ERR(("Invalid event buffer len\n"));
		ret = BCME_BUFTOOSHORT;
		goto fail;
	}
	p_attr += offset;
	len -= offset;

	/*
	 * optional fields:
	 * must be in order following by service descriptor attribute format
	 */

	/* binding bitmap */
	if (svc_control & NAN_SC_BINDING_BITMAP_PRESENT) {
		uint16 bitmap = 0;
		WL_TRACE(("> svc_control: binding bitmap present\n"));

		/* Copy binding bitmap */
		ret = memcpy_s(&bitmap, sizeof(bitmap),
			p_attr, NAN_BINDING_BITMAP_LEN);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy bit map\n"));
			return ret;
		}
		WL_TRACE(("> sc binding bitmap: 0x%04x\n", bitmap));

		if (NAN_BINDING_BITMAP_LEN > len) {
			WL_ERR(("Invalid event buffer len\n"));
			ret = BCME_BUFTOOSHORT;
			goto fail;
		}
		p_attr += NAN_BINDING_BITMAP_LEN;
		len -= NAN_BINDING_BITMAP_LEN;
	}

	/* matching filter */
	if (svc_control & NAN_SC_MATCHING_FILTER_PRESENT) {
		WL_TRACE(("> svc_control: matching filter present\n"));

		tlv_data->tx_match_filter.dlen = *p_attr++;
		WL_TRACE(("> matching filter len: 0x%02x\n",
				tlv_data->tx_match_filter.dlen));

		if (!tlv_data->tx_match_filter.dlen ||
				tlv_data->tx_match_filter.dlen > MAX_MATCH_FILTER_LEN) {
			tlv_data->tx_match_filter.dlen = 0;
			WL_ERR(("tx match filter length is invalid\n"));
			ret = -EINVAL;
			goto fail;
		}
		tlv_data->tx_match_filter.data =
			MALLOCZ(osh, tlv_data->tx_match_filter.dlen);
		if (!tlv_data->tx_match_filter.data) {
			WL_ERR(("%s: memory allocation failed\n", __FUNCTION__));
			tlv_data->tx_match_filter.dlen = 0;
			ret = -ENOMEM;
			goto fail;
		}
		ret = memcpy_s(tlv_data->tx_match_filter.data, tlv_data->tx_match_filter.dlen,
				p_attr, tlv_data->tx_match_filter.dlen);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy tx match filter data\n"));
			goto fail;
		}
		/* advance read pointer */
		offset = tlv_data->tx_match_filter.dlen;
		if (offset > len) {
			WL_ERR(("Invalid event buffer\n"));
			ret = BCME_BUFTOOSHORT;
			goto fail;
		}
		p_attr += offset;
		len -= offset;
	}

	/* service response filter */
	if (svc_control & NAN_SC_SR_FILTER_PRESENT) {
		WL_TRACE(("> svc_control: service response filter present\n"));

		tlv_data->rx_match_filter.dlen = *p_attr++;
		WL_TRACE(("> sr match filter len: 0x%02x\n",
				tlv_data->rx_match_filter.dlen));

		if (!tlv_data->rx_match_filter.dlen ||
				tlv_data->rx_match_filter.dlen > MAX_MATCH_FILTER_LEN) {
			tlv_data->rx_match_filter.dlen = 0;
			WL_ERR(("%s: sr matching filter length is invalid\n",
					__FUNCTION__));
			ret = BCME_BADLEN;
			goto fail;
		}
		tlv_data->rx_match_filter.data =
			MALLOCZ(osh, tlv_data->rx_match_filter.dlen);
		if (!tlv_data->rx_match_filter.data) {
			WL_ERR(("%s: memory allocation failed\n", __FUNCTION__));
			tlv_data->rx_match_filter.dlen = 0;
			ret = BCME_NOMEM;
			goto fail;
		}

		ret = memcpy_s(tlv_data->rx_match_filter.data, tlv_data->rx_match_filter.dlen,
				p_attr, tlv_data->rx_match_filter.dlen);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy rx match filter data\n"));
			goto fail;
		}

		/* advance read pointer */
		offset = tlv_data->rx_match_filter.dlen;
		if (offset > len) {
			WL_ERR(("Invalid event buffer len\n"));
			ret = BCME_BUFTOOSHORT;
			goto fail;
		}
		p_attr += offset;
		len -= offset;
	}

	/* service specific info */
	if (svc_control & NAN_SC_SVC_INFO_PRESENT) {
		WL_TRACE(("> svc_control: svc specific info present\n"));

		tlv_data->svc_info.dlen = *p_attr++;
		WL_TRACE(("> svc info len: 0x%02x\n", tlv_data->svc_info.dlen));

		if (!tlv_data->svc_info.dlen ||
				tlv_data->svc_info.dlen > NAN_MAX_SERVICE_SPECIFIC_INFO_LEN) {
			/* must be able to handle null msg which is not error */
			tlv_data->svc_info.dlen = 0;
			WL_ERR(("sde data length is invalid\n"));
			ret = BCME_BADLEN;
			goto fail;
		}

		if (tlv_data->svc_info.dlen > 0) {
			tlv_data->svc_info.data =
				MALLOCZ(osh, tlv_data->svc_info.dlen);
			if (!tlv_data->svc_info.data) {
				WL_ERR(("%s: memory allocation failed\n", __FUNCTION__));
				tlv_data->svc_info.dlen = 0;
				ret = BCME_NOMEM;
				goto fail;
			}
			ret = memcpy_s(tlv_data->svc_info.data, tlv_data->svc_info.dlen,
					p_attr, tlv_data->svc_info.dlen);
			if (ret != BCME_OK) {
				WL_ERR(("Failed to copy svc info\n"));
				goto fail;
			}

			/* advance read pointer */
			offset = tlv_data->svc_info.dlen;
			if (offset > len) {
				WL_ERR(("Invalid event buffer len\n"));
				ret = BCME_BUFTOOSHORT;
				goto fail;
			}
			p_attr += offset;
			len -= offset;
		} else {
			/* must be able to handle null msg which is not error */
			tlv_data->svc_info.dlen = 0;
			WL_TRACE(("%s: svc info length is zero, null info data\n",
					__FUNCTION__));
		}
	}

	/*
	 * discovery range limited:
	 * If set to 1, the pub/sub msg is limited in range to close proximity.
	 * If set to 0, the pub/sub msg is not limited in range.
	 * Valid only when the message is either of a publish or a sub.
	 */
	if (svc_control & NAN_SC_RANGE_LIMITED) {
		if (((svc_control & NAN_SVC_CONTROL_TYPE_MASK) == NAN_SC_PUBLISH) ||
				((svc_control & NAN_SVC_CONTROL_TYPE_MASK) == NAN_SC_SUBSCRIBE)) {
			WL_TRACE(("> svc_control: range limited present\n"));
		} else {
			WL_TRACE(("range limited is only valid on pub or sub\n"));
		}

		/* TODO: send up */

		/* advance read pointer */
		p_attr++;
	}
	return ret;
fail:
	if (tlv_data->tx_match_filter.data) {
		MFREE(osh, tlv_data->tx_match_filter.data,
				tlv_data->tx_match_filter.dlen);
		tlv_data->tx_match_filter.data = NULL;
	}
	if (tlv_data->rx_match_filter.data) {
		MFREE(osh, tlv_data->rx_match_filter.data,
				tlv_data->rx_match_filter.dlen);
		tlv_data->rx_match_filter.data = NULL;
	}
	if (tlv_data->svc_info.data) {
		MFREE(osh, tlv_data->svc_info.data,
				tlv_data->svc_info.dlen);
		tlv_data->svc_info.data = NULL;
	}

	WL_DBG(("Parse SDA event data, status = %d\n", ret));
	return ret;
}

static s32
wl_cfgnan_parse_sd_attr_data(osl_t *osh, uint16 len, const uint8 *data,
	nan_event_data_t *tlv_data, uint16 type) {
	const uint8 *p_attr = data;
	uint16 offset = 0;
	s32 ret = BCME_OK;
	const wl_nan_event_disc_result_t *ev_disc = NULL;
	const wl_nan_event_replied_t *ev_replied = NULL;
	const wl_nan_ev_receive_t *ev_fup = NULL;

	/*
	 * Mapping wifi_nan_svc_descriptor_attr_t, and svc controls are optional.
	 */
	if (type == WL_NAN_XTLV_SD_DISC_RESULTS) {
		u8 iter;
		ev_disc = (const wl_nan_event_disc_result_t *)p_attr;

		WL_DBG((">> WL_NAN_XTLV_RESULTS: Discovery result\n"));

		tlv_data->pub_id = (wl_nan_instance_id_t)ev_disc->pub_id;
		tlv_data->sub_id = (wl_nan_instance_id_t)ev_disc->sub_id;
		tlv_data->publish_rssi = ev_disc->publish_rssi;
		ret = memcpy_s(&tlv_data->remote_nmi, ETHER_ADDR_LEN,
				&ev_disc->pub_mac, ETHER_ADDR_LEN);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy remote nmi\n"));
			goto fail;
		}

		WL_TRACE(("publish id: %d\n", ev_disc->pub_id));
		WL_TRACE(("subscribe d: %d\n", ev_disc->sub_id));
		WL_TRACE(("publish mac addr: " MACDBG "\n",
				MAC2STRDBG(ev_disc->pub_mac.octet)));
		WL_TRACE(("publish rssi: %d\n", (int8)ev_disc->publish_rssi));
		WL_TRACE(("attribute no: %d\n", ev_disc->attr_num));
		WL_TRACE(("attribute len: %d\n", (uint16)ev_disc->attr_list_len));

		/* advance to the service descricptor */
		offset = OFFSETOF(wl_nan_event_disc_result_t, attr_list[0]);
		if (offset > len) {
			WL_ERR(("Invalid event buffer len\n"));
			ret = BCME_BUFTOOSHORT;
			goto fail;
		}
		p_attr += offset;
		len -= offset;

		iter = ev_disc->attr_num;
		while (iter) {
			if ((uint8)*p_attr == NAN_ATTR_SVC_DESCRIPTOR) {
				WL_TRACE(("> attr id: 0x%02x\n", (uint8)*p_attr));
				ret = wl_cfgnan_parse_sda_data(osh, p_attr, len, tlv_data);
				if (unlikely(ret)) {
					WL_ERR(("wl_cfgnan_parse_sda_data failed,"
							"error = %d \n", ret));
					goto fail;
				}
			}

			if ((uint8)*p_attr == NAN_ATTR_SVC_DESC_EXTENSION) {
				WL_TRACE(("> attr id: 0x%02x\n", (uint8)*p_attr));
				ret = wl_cfgnan_parse_sdea_data(osh, p_attr, len, tlv_data);
				if (unlikely(ret)) {
					WL_ERR(("wl_cfgnan_parse_sdea_data failed,"
							"error = %d \n", ret));
					goto fail;
				}
			}
			offset = (sizeof(*p_attr) +
					sizeof(ev_disc->attr_list_len) +
					(p_attr[1] | (p_attr[2] << 8)));
			if (offset > len) {
				WL_ERR(("Invalid event buffer len\n"));
				ret = BCME_BUFTOOSHORT;
				goto fail;
			}
			p_attr += offset;
			len -= offset;
			iter--;
		}
	} else if (type == WL_NAN_XTLV_SD_FUP_RECEIVED) {
		uint8 iter;
		ev_fup = (const wl_nan_ev_receive_t *)p_attr;

		WL_TRACE((">> WL_NAN_XTLV_SD_FUP_RECEIVED: Transmit follow-up\n"));

		tlv_data->local_inst_id = (wl_nan_instance_id_t)ev_fup->local_id;
		tlv_data->requestor_id = (wl_nan_instance_id_t)ev_fup->remote_id;
		tlv_data->fup_rssi = ev_fup->fup_rssi;
		ret = memcpy_s(&tlv_data->remote_nmi, ETHER_ADDR_LEN,
				&ev_fup->remote_addr, ETHER_ADDR_LEN);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy remote nmi\n"));
			goto fail;
		}

		WL_TRACE(("local id: %d\n", ev_fup->local_id));
		WL_TRACE(("remote id: %d\n", ev_fup->remote_id));
		WL_TRACE(("peer mac addr: " MACDBG "\n",
				MAC2STRDBG(ev_fup->remote_addr.octet)));
		WL_TRACE(("peer rssi: %d\n", (int8)ev_fup->fup_rssi));
		WL_TRACE(("attribute no: %d\n", ev_fup->attr_num));
		WL_TRACE(("attribute len: %d\n", ev_fup->attr_list_len));

		/* advance to the service descriptor which is attr_list[0] */
		offset = OFFSETOF(wl_nan_ev_receive_t, attr_list[0]);
		if (offset > len) {
			WL_ERR(("Invalid event buffer len\n"));
			ret = BCME_BUFTOOSHORT;
			goto fail;
		}
		p_attr += offset;
		len -= offset;

		iter = ev_fup->attr_num;
		while (iter) {
			if ((uint8)*p_attr == NAN_ATTR_SVC_DESCRIPTOR) {
				WL_TRACE(("> attr id: 0x%02x\n", (uint8)*p_attr));
				ret = wl_cfgnan_parse_sda_data(osh, p_attr, len, tlv_data);
				if (unlikely(ret)) {
					WL_ERR(("wl_cfgnan_parse_sda_data failed,"
							"error = %d \n", ret));
					goto fail;
				}
			}

			if ((uint8)*p_attr == NAN_ATTR_SVC_DESC_EXTENSION) {
				WL_TRACE(("> attr id: 0x%02x\n", (uint8)*p_attr));
				ret = wl_cfgnan_parse_sdea_data(osh, p_attr, len, tlv_data);
				if (unlikely(ret)) {
					WL_ERR(("wl_cfgnan_parse_sdea_data failed,"
							"error = %d \n", ret));
					goto fail;
				}
			}
			offset = (sizeof(*p_attr) +
					sizeof(ev_fup->attr_list_len) +
					(p_attr[1] | (p_attr[2] << 8)));
			if (offset > len) {
				WL_ERR(("Invalid event buffer len\n"));
				ret = BCME_BUFTOOSHORT;
				goto fail;
			}
			p_attr += offset;
			len -= offset;
			iter--;
		}
	} else if (type == WL_NAN_XTLV_SD_SDF_RX) {
		/*
		 * SDF followed by nan2_pub_act_frame_t and wifi_nan_svc_descriptor_attr_t,
		 * and svc controls are optional.
		 */
		const nan2_pub_act_frame_t *nan_pub_af =
			(const nan2_pub_act_frame_t *)p_attr;

		WL_TRACE((">> WL_NAN_XTLV_SD_SDF_RX\n"));

		/* nan2_pub_act_frame_t */
		WL_TRACE(("pub category: 0x%02x\n", nan_pub_af->category_id));
		WL_TRACE(("pub action: 0x%02x\n", nan_pub_af->action_field));
		WL_TRACE(("nan oui: %2x-%2x-%2x\n",
				nan_pub_af->oui[0], nan_pub_af->oui[1], nan_pub_af->oui[2]));
		WL_TRACE(("oui type: 0x%02x\n", nan_pub_af->oui_type));
		WL_TRACE(("oui subtype: 0x%02x\n", nan_pub_af->oui_sub_type));

		offset = sizeof(*nan_pub_af);
		if (offset > len) {
			WL_ERR(("Invalid event buffer len\n"));
			ret = BCME_BUFTOOSHORT;
			goto fail;
		}
		p_attr += offset;
		len -= offset;
	} else if (type == WL_NAN_XTLV_SD_REPLIED) {
		ev_replied = (const wl_nan_event_replied_t *)p_attr;

		WL_TRACE((">> WL_NAN_XTLV_SD_REPLIED: Replied Event\n"));

		tlv_data->pub_id = (wl_nan_instance_id_t)ev_replied->pub_id;
		tlv_data->sub_id = (wl_nan_instance_id_t)ev_replied->sub_id;
		tlv_data->sub_rssi = ev_replied->sub_rssi;
		ret = memcpy_s(&tlv_data->remote_nmi, ETHER_ADDR_LEN,
				&ev_replied->sub_mac, ETHER_ADDR_LEN);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy remote nmi\n"));
			goto fail;
		}

		WL_TRACE(("publish id: %d\n", ev_replied->pub_id));
		WL_TRACE(("subscribe d: %d\n", ev_replied->sub_id));
		WL_TRACE(("Subscriber mac addr: " MACDBG "\n",
				MAC2STRDBG(ev_replied->sub_mac.octet)));
		WL_TRACE(("subscribe rssi: %d\n", (int8)ev_replied->sub_rssi));
		WL_TRACE(("attribute no: %d\n", ev_replied->attr_num));
		WL_TRACE(("attribute len: %d\n", (uint16)ev_replied->attr_list_len));

		/* advance to the service descriptor which is attr_list[0] */
		offset = OFFSETOF(wl_nan_event_replied_t, attr_list[0]);
		if (offset > len) {
			WL_ERR(("Invalid event buffer len\n"));
			ret = BCME_BUFTOOSHORT;
			goto fail;
		}
		p_attr += offset;
		len -= offset;
		ret = wl_cfgnan_parse_sda_data(osh, p_attr, len, tlv_data);
		if (unlikely(ret)) {
			WL_ERR(("wl_cfgnan_parse_sdea_data failed,"
				"error = %d \n", ret));
		}
	}

fail:
	return ret;
}

/* Based on each case of tlv type id, fill into tlv data */
static int
wl_cfgnan_set_vars_cbfn(void *ctx, const uint8 *data, uint16 type, uint16 len)
{
	nan_parse_event_ctx_t *ctx_tlv_data = ((nan_parse_event_ctx_t *)(ctx));
	nan_event_data_t *tlv_data = ((nan_event_data_t *)(ctx_tlv_data->nan_evt_data));
	int ret = BCME_OK;

	NAN_DBG_ENTER();
	if (!data || !len) {
		WL_ERR(("data length is invalid\n"));
		ret = BCME_ERROR;
		goto fail;
	}

	switch (type) {
	/*
	 * Need to parse service descript attributes including service control,
	 * when Follow up or Discovery result come
	 */
	case WL_NAN_XTLV_SD_FUP_RECEIVED:
	case WL_NAN_XTLV_SD_DISC_RESULTS: {
		ret = wl_cfgnan_parse_sd_attr_data(ctx_tlv_data->cfg->osh,
			len, data, tlv_data, type);
		break;
	}
	case WL_NAN_XTLV_SD_NDPE_TLV_LIST:
		/* Intentional fall through NDPE TLV list and SVC INFO is sent in same container
		 * to upper layers
		 */
	case WL_NAN_XTLV_SD_SVC_INFO: {
		tlv_data->svc_info.data =
			MALLOCZ(ctx_tlv_data->cfg->osh, len);
		if (!tlv_data->svc_info.data) {
			WL_ERR(("%s: memory allocation failed\n", __FUNCTION__));
			tlv_data->svc_info.dlen = 0;
			ret = BCME_NOMEM;
			goto fail;
		}
		tlv_data->svc_info.dlen = len;
		ret = memcpy_s(tlv_data->svc_info.data, tlv_data->svc_info.dlen,
				data, tlv_data->svc_info.dlen);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy svc info data\n"));
			goto fail;
		}
		break;
	}
	case WL_NAN_XTLV_SD_NAN_AF:
	case WL_NAN_XTLV_DAM_NA_ATTR:
		/* No action -intentionally added to avoid prints when these events are rcvd */
		break;
	default:
		WL_ERR(("Not available for tlv type = 0x%x\n", type));
		ret = BCME_ERROR;
		break;
	}
fail:
	NAN_DBG_EXIT();
	return ret;
}

int
wl_cfg_nan_check_cmd_len(uint16 nan_iov_len, uint16 data_size,
		uint16 *subcmd_len)
{
	s32 ret = BCME_OK;

	if (subcmd_len != NULL) {
		*subcmd_len = OFFSETOF(bcm_iov_batch_subcmd_t, data) +
				ALIGN_SIZE(data_size, 4);
		if (*subcmd_len > nan_iov_len) {
			WL_ERR(("%s: Buf short, requested:%d, available:%d\n",
					__FUNCTION__, *subcmd_len, nan_iov_len));
			ret = BCME_NOMEM;
		}
	} else {
		WL_ERR(("Invalid subcmd_len\n"));
		ret = BCME_ERROR;
	}
	return ret;
}

int
wl_cfgnan_config_eventmask(struct net_device *ndev, struct bcm_cfg80211 *cfg,
	uint8 event_ind_flag, bool disable_events)
{
	bcm_iov_batch_buf_t *nan_buf = NULL;
	s32 ret = BCME_OK;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	uint16 subcmd_len;
	uint32 status;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	bcm_iov_batch_subcmd_t *sub_cmd_resp = NULL;
	uint8 event_mask[WL_NAN_EVMASK_EXTN_LEN];
	wl_nan_evmask_extn_t *evmask;
	uint16 evmask_cmd_len;
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE];

	NAN_DBG_ENTER();

	/* same src and dest len here */
	bzero(event_mask, sizeof(event_mask));
	evmask_cmd_len = OFFSETOF(wl_nan_evmask_extn_t, evmask) +
		sizeof(event_mask);
	ret = wl_add_remove_eventmsg(ndev, WLC_E_NAN, true);
	if (unlikely(ret)) {
		WL_ERR((" nan event enable failed, error = %d \n", ret));
		goto fail;
	}

	nan_buf = MALLOCZ(cfg->osh, nan_buf_size);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_buf_size -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);
	sub_cmd = (bcm_iov_batch_subcmd_t*)(uint8 *)(&nan_buf->cmds[0]);

	ret = wl_cfg_nan_check_cmd_len(nan_buf_size,
			evmask_cmd_len, &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		goto fail;
	}

	sub_cmd->id = htod16(WL_NAN_CMD_CFG_EVENT_MASK);
	sub_cmd->len = sizeof(sub_cmd->u.options) + evmask_cmd_len;
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);
	evmask = (wl_nan_evmask_extn_t *)sub_cmd->data;
	evmask->ver = WL_NAN_EVMASK_EXTN_VER;
	evmask->len = WL_NAN_EVMASK_EXTN_LEN;
	nan_buf_size -= subcmd_len;
	nan_buf->count = 1;

	if (disable_events) {
		WL_DBG(("Disabling all nan events..except stop event\n"));
		setbit(event_mask, NAN_EVENT_MAP(WL_NAN_EVENT_STOP));
	} else {
		/*
		 * Android framework event mask configuration.
		 */
		nan_buf->is_set = false;
		memset(resp_buf, 0, sizeof(resp_buf));
		ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, nan_buf_size, &status,
				(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
		if (unlikely(ret) || unlikely(status)) {
			WL_ERR(("get nan event mask failed ret %d status %d \n",
				ret, status));
			goto fail;
		}
		sub_cmd_resp = &((bcm_iov_batch_buf_t *)(resp_buf))->cmds[0];
		evmask = (wl_nan_evmask_extn_t *)sub_cmd_resp->data;

		/* check the response buff */
		/* same src and dest len here */
		(void)memcpy_s(&event_mask, WL_NAN_EVMASK_EXTN_LEN,
				(uint8*)&evmask->evmask, WL_NAN_EVMASK_EXTN_LEN);

		if (event_ind_flag) {
			/* FIXME:BIT0 - Disable disc mac addr change event indication */
			if (CHECK_BIT(event_ind_flag, WL_NAN_EVENT_DIC_MAC_ADDR_BIT)) {
				WL_DBG(("Need to add disc mac addr change event\n"));
			}
			/* BIT2 - Disable nan cluster join indication (OTA). */
			if (CHECK_BIT(event_ind_flag, WL_NAN_EVENT_JOIN_EVENT)) {
				clrbit(event_mask, NAN_EVENT_MAP(WL_NAN_EVENT_MERGE));
			}
		}

		setbit(event_mask, NAN_EVENT_MAP(WL_NAN_EVENT_DISCOVERY_RESULT));
		setbit(event_mask, NAN_EVENT_MAP(WL_NAN_EVENT_RECEIVE));
		setbit(event_mask, NAN_EVENT_MAP(WL_NAN_EVENT_TERMINATED));
		setbit(event_mask, NAN_EVENT_MAP(WL_NAN_EVENT_STOP));
		setbit(event_mask, NAN_EVENT_MAP(WL_NAN_EVENT_TXS));
		setbit(event_mask, NAN_EVENT_MAP(WL_NAN_EVENT_PEER_DATAPATH_IND));
		setbit(event_mask, NAN_EVENT_MAP(WL_NAN_EVENT_DATAPATH_ESTB));
		setbit(event_mask, NAN_EVENT_MAP(WL_NAN_EVENT_DATAPATH_END));
		setbit(event_mask, NAN_EVENT_MAP(WL_NAN_EVENT_RNG_REQ_IND));
		setbit(event_mask, NAN_EVENT_MAP(WL_NAN_EVENT_RNG_TERM_IND));
		setbit(event_mask, NAN_EVENT_MAP(WL_NAN_EVENT_DISC_CACHE_TIMEOUT));
		/* Disable below events by default */
		clrbit(event_mask, NAN_EVENT_MAP(WL_NAN_EVENT_PEER_SCHED_UPD_NOTIF));
		clrbit(event_mask, NAN_EVENT_MAP(WL_NAN_EVENT_RNG_RPT_IND));
		clrbit(event_mask, NAN_EVENT_MAP(WL_NAN_EVENT_DW_END));
	}

	nan_buf->is_set = true;
	evmask = (wl_nan_evmask_extn_t *)sub_cmd->data;
	/* same src and dest len here */
	(void)memcpy_s((uint8*)&evmask->evmask, sizeof(event_mask),
		&event_mask, sizeof(event_mask));

	nan_buf_size = (NAN_IOCTL_BUF_SIZE - nan_buf_size);
	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, nan_buf_size, &status,
			(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
	if (unlikely(ret) || unlikely(status)) {
		WL_ERR(("set nan event mask failed ret %d status %d \n", ret, status));
		goto fail;
	}
	WL_DBG(("set nan event mask successfull\n"));

fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, NAN_IOCTL_BUF_SIZE);
	}
	NAN_DBG_EXIT();
	return ret;
}

static int
wl_cfgnan_set_nan_avail(struct net_device *ndev,
		struct bcm_cfg80211 *cfg, nan_avail_cmd_data *cmd_data, uint8 avail_type)
{
	bcm_iov_batch_buf_t *nan_buf = NULL;
	s32 ret = BCME_OK;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	uint16 subcmd_len;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	wl_nan_iov_t *nan_iov_data = NULL;
	wl_avail_t *avail = NULL;
	wl_avail_entry_t *entry;	/* used for filling entry structure */
	uint8 *p;	/* tracking pointer */
	uint8 i;
	u32 status;
	int c;
	char ndc_id[ETHER_ADDR_LEN] = { 0x50, 0x6f, 0x9a, 0x01, 0x0, 0x0 };
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(ndev);
	char *a = WL_AVAIL_BIT_MAP;
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE];

	NAN_DBG_ENTER();

	/* Do not disturb avail if dam is supported */
	if (FW_SUPPORTED(dhdp, autodam)) {
		WL_DBG(("DAM is supported, avail modification not allowed\n"));
		return ret;
	}

	if (avail_type < WL_AVAIL_LOCAL || avail_type > WL_AVAIL_TYPE_MAX) {
		WL_ERR(("Invalid availability type\n"));
		ret = BCME_USAGE_ERROR;
		goto fail;
	}

	nan_buf = MALLOCZ(cfg->osh, nan_buf_size);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_iov_data = MALLOCZ(cfg->osh, sizeof(*nan_iov_data));
	if (!nan_iov_data) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_iov_data->nan_iov_len = NAN_IOCTL_BUF_SIZE;
	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_iov_data->nan_iov_buf = (uint8 *)(&nan_buf->cmds[0]);
	nan_iov_data->nan_iov_len -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);

	sub_cmd = (bcm_iov_batch_subcmd_t*)(nan_iov_data->nan_iov_buf);
	ret = wl_cfg_nan_check_cmd_len(nan_iov_data->nan_iov_len,
			sizeof(*avail), &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		goto fail;
	}
	avail = (wl_avail_t *)sub_cmd->data;

	/* populate wl_avail_type */
	avail->flags = avail_type;
	if (avail_type == WL_AVAIL_RANGING) {
		ret = memcpy_s(&avail->addr, ETHER_ADDR_LEN,
			&cmd_data->peer_nmi, ETHER_ADDR_LEN);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy peer nmi\n"));
			goto fail;
		}
	}

	sub_cmd->len = sizeof(sub_cmd->u.options) + subcmd_len;
	sub_cmd->id = htod16(WL_NAN_CMD_CFG_AVAIL);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);

	nan_buf->is_set = false;
	nan_buf->count++;
	nan_iov_data->nan_iov_len -= subcmd_len;
	nan_buf_size = (NAN_IOCTL_BUF_SIZE - nan_iov_data->nan_iov_len);

	WL_TRACE(("Read wl nan avail status\n"));

	bzero(resp_buf, sizeof(resp_buf));
	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, nan_buf_size, &status,
			(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
	if (unlikely(ret)) {
		WL_ERR(("\n Get nan avail failed ret %d, status %d \n", ret, status));
		goto fail;
	}

	if (status == BCME_NOTFOUND) {
		nan_buf->count = 0;
		nan_iov_data->nan_iov_buf = (uint8 *)(&nan_buf->cmds[0]);
		nan_iov_data->nan_iov_len -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);

		sub_cmd = (bcm_iov_batch_subcmd_t*)(nan_iov_data->nan_iov_buf);

		avail = (wl_avail_t *)sub_cmd->data;
		p = avail->entry;

		/* populate wl_avail fields */
		avail->length = OFFSETOF(wl_avail_t, entry);
		avail->flags = avail_type;
		avail->num_entries = 0;
		avail->id = 0;
		entry = (wl_avail_entry_t*)p;
		entry->flags = WL_AVAIL_ENTRY_COM;

		/* set default values for optional parameters */
		entry->start_offset = 0;
		entry->u.band = 0;

		if (cmd_data->avail_period) {
			entry->period = cmd_data->avail_period;
		} else {
			entry->period = WL_AVAIL_PERIOD_1024;
		}

		if (cmd_data->duration != NAN_BAND_INVALID) {
			entry->flags |= (3 << WL_AVAIL_ENTRY_USAGE_SHIFT) |
				(cmd_data->duration << WL_AVAIL_ENTRY_BIT_DUR_SHIFT);
		} else {
			entry->flags |= (3 << WL_AVAIL_ENTRY_USAGE_SHIFT) |
				(WL_AVAIL_BIT_DUR_16 << WL_AVAIL_ENTRY_BIT_DUR_SHIFT);
		}
		entry->bitmap_len = 0;

		if (avail_type == WL_AVAIL_LOCAL) {
			entry->flags |= 1 << WL_AVAIL_ENTRY_CHAN_SHIFT;
			/* Check for 5g support, based on that choose 5g channel */
			if (cfg->nancfg->support_5g) {
				entry->u.channel_info =
					htod32(wf_channel2chspec(WL_AVAIL_CHANNEL_5G,
						WL_AVAIL_BANDWIDTH_5G));
			} else {
				entry->u.channel_info =
					htod32(wf_channel2chspec(WL_AVAIL_CHANNEL_2G,
						WL_AVAIL_BANDWIDTH_2G));
			}
			entry->flags = htod16(entry->flags);
		}

		if (cfg->nancfg->support_5g) {
			a = WL_5G_AVAIL_BIT_MAP;
		}

		/* point to bitmap value for processing */
		if (cmd_data->bmap) {
			for (c = (WL_NAN_EVENT_CLEAR_BIT-1); c >= 0; c--) {
				i = cmd_data->bmap >> c;
				if (i & 1) {
					setbit(entry->bitmap, (WL_NAN_EVENT_CLEAR_BIT-c-1));
				}
			}
		} else {
			for (i = 0; i < strlen(WL_AVAIL_BIT_MAP); i++) {
				if (*a == '1') {
					setbit(entry->bitmap, i);
				}
				a++;
			}
		}

		/* account for partially filled most significant byte */
		entry->bitmap_len = ((WL_NAN_EVENT_CLEAR_BIT) + NBBY - 1) / NBBY;
		if (avail_type == WL_AVAIL_NDC) {
			ret = memcpy_s(&avail->addr, ETHER_ADDR_LEN,
					ndc_id, ETHER_ADDR_LEN);
			if (ret != BCME_OK) {
				WL_ERR(("Failed to copy ndc id\n"));
				goto fail;
			}
		} else if (avail_type == WL_AVAIL_RANGING) {
			ret = memcpy_s(&avail->addr, ETHER_ADDR_LEN,
					&cmd_data->peer_nmi, ETHER_ADDR_LEN);
			if (ret != BCME_OK) {
				WL_ERR(("Failed to copy peer nmi\n"));
				goto fail;
			}
		}
		/* account for partially filled most significant byte */

		/* update wl_avail and populate wl_avail_entry */
		entry->length = OFFSETOF(wl_avail_entry_t, bitmap) + entry->bitmap_len;
		avail->num_entries++;
		avail->length += entry->length;
		/* advance pointer for next entry */
		p += entry->length;

		/* convert to dongle endianness */
		entry->length = htod16(entry->length);
		entry->start_offset = htod16(entry->start_offset);
		entry->u.channel_info = htod32(entry->u.channel_info);
		entry->flags = htod16(entry->flags);
		/* update avail_len only if
		 * there are avail entries
		 */
		if (avail->num_entries) {
			nan_iov_data->nan_iov_len -= avail->length;
			avail->length = htod16(avail->length);
			avail->flags = htod16(avail->flags);
		}
		avail->length = htod16(avail->length);

		sub_cmd->id = htod16(WL_NAN_CMD_CFG_AVAIL);
		sub_cmd->len = sizeof(sub_cmd->u.options) + avail->length;
		sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);

		nan_buf->is_set = true;
		nan_buf->count++;

		/* Reduce the iov_len size by subcmd_len */
		nan_iov_data->nan_iov_len -= subcmd_len;
		nan_buf_size = (NAN_IOCTL_BUF_SIZE - nan_iov_data->nan_iov_len);

		ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, nan_buf_size, &status,
				(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
		if (unlikely(ret) || unlikely(status)) {
			WL_ERR(("\n set nan avail failed ret %d status %d \n", ret, status));
			ret = status;
			goto fail;
		}
	} else if (status == BCME_OK) {
		WL_DBG(("Avail type [%d] found to be configured\n", avail_type));
	} else {
		WL_ERR(("set nan avail failed ret %d status %d \n", ret, status));
	}

fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, NAN_IOCTL_BUF_SIZE);
	}
	if (nan_iov_data) {
		MFREE(cfg->osh, nan_iov_data, sizeof(*nan_iov_data));
	}

	NAN_DBG_EXIT();
	return ret;
}

/* API to configure nan ctrl and nan ctrl2 commands */
static int
wl_cfgnan_config_control_flag(struct net_device *ndev, struct bcm_cfg80211 *cfg,
	uint32 flag1, uint32 flag2, uint16 cmd_id, uint32 *status, bool set)
{
	bcm_iov_batch_buf_t *nan_buf = NULL;
	s32 ret = BCME_OK;
	uint16 nan_iov_start, nan_iov_end;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	uint16 subcmd_len;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	bcm_iov_batch_subcmd_t *sub_cmd_resp = NULL;
	wl_nan_iov_t *nan_iov_data = NULL;
	uint32 *cfg_ctrl;
	uint16 cfg_ctrl_size;
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE];

	NAN_DBG_ENTER();
	nan_buf = MALLOCZ(cfg->osh, nan_buf_size);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_iov_data = MALLOCZ(cfg->osh, sizeof(*nan_iov_data));
	if (!nan_iov_data) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	if (cmd_id == WL_NAN_CMD_CFG_NAN_CONFIG) {
		cfg_ctrl_size = sizeof(wl_nan_cfg_ctrl_t);
	} else if (cmd_id == WL_NAN_CMD_CFG_NAN_CONFIG2) {
		cfg_ctrl_size = sizeof(wl_nan_cfg_ctrl2_t);
	} else {
		ret = BCME_BADARG;
		goto fail;
	}

	nan_iov_data->nan_iov_len = nan_iov_start = NAN_IOCTL_BUF_SIZE;
	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_iov_data->nan_iov_buf = (uint8 *)(&nan_buf->cmds[0]);
	nan_iov_data->nan_iov_len -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);
	sub_cmd = (bcm_iov_batch_subcmd_t*)(nan_iov_data->nan_iov_buf);

	ret = wl_cfg_nan_check_cmd_len(nan_iov_data->nan_iov_len,
			cfg_ctrl_size, &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		goto fail;
	}

	sub_cmd->id = htod16(cmd_id);
	sub_cmd->len = sizeof(sub_cmd->u.options) + cfg_ctrl_size;
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);

	nan_buf->is_set = false;
	nan_buf->count++;

	/* Reduce the iov_len size by subcmd_len */
	nan_iov_data->nan_iov_len -= subcmd_len;
	nan_iov_end = nan_iov_data->nan_iov_len;
	nan_buf_size = (nan_iov_start - nan_iov_end);

	bzero(resp_buf, sizeof(resp_buf));
	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, nan_buf_size, status,
			(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
	if (unlikely(ret) || unlikely(*status)) {
		WL_ERR(("get nan cfg ctrl failed ret %d status %d \n", ret, *status));
		goto fail;
	}
	sub_cmd_resp = &((bcm_iov_batch_buf_t *)(resp_buf))->cmds[0];

	/* check the response buff */
	if (cmd_id == WL_NAN_CMD_CFG_NAN_CONFIG) {
		wl_nan_cfg_ctrl_t *cfg_ctrl1;
		cfg_ctrl1 = ((uint32 *)&sub_cmd_resp->data[0]);
		if (set) {
			*cfg_ctrl1 |= flag1;
		} else {
			*cfg_ctrl1 &= ~flag1;
		}
		cfg_ctrl = cfg_ctrl1;
		WL_INFORM_MEM(("%s: Modifying nan ctrl flag %x val %d\n",
				__FUNCTION__, flag1, set));
	} else {
		wl_nan_cfg_ctrl2_t *cfg_ctrl2;
		cfg_ctrl2 = ((wl_nan_cfg_ctrl2_t *)&sub_cmd_resp->data[0]);
		if (set) {
			cfg_ctrl2->flags1 |= flag1;
			cfg_ctrl2->flags2 |= flag2;
		} else {
			cfg_ctrl2->flags1 &= ~flag1;
			cfg_ctrl2->flags2 &= ~flag2;
		}
		cfg_ctrl = (uint32 *)cfg_ctrl2;
		WL_INFORM_MEM(("%s: Modifying nan ctrl2 flag1 %x flag2 %x val %d\n",
				__FUNCTION__, flag1, flag2, set));
	}
	ret = memcpy_s(sub_cmd->data, cfg_ctrl_size, cfg_ctrl, cfg_ctrl_size);
	if (ret != BCME_OK) {
		WL_ERR(("Failed to copy cfg ctrl\n"));
		goto fail;
	}

	nan_buf->is_set = true;
	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, nan_buf_size, status,
			(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
	if (unlikely(ret) || unlikely(*status)) {
		WL_ERR(("set nan cfg ctrl failed ret %d status %d \n", ret, *status));
		goto fail;
	}
	WL_DBG(("set nan cfg ctrl successfull\n"));
fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, NAN_IOCTL_BUF_SIZE);
	}
	if (nan_iov_data) {
		MFREE(cfg->osh, nan_iov_data, sizeof(*nan_iov_data));
	}

	NAN_DBG_EXIT();
	return ret;
}

static int
wl_cfgnan_get_iovars_status(void *ctx, const uint8 *data, uint16 type, uint16 len)
{
	bcm_iov_batch_buf_t *b_resp = (bcm_iov_batch_buf_t *)ctx;
	uint32 status;
	/* if all tlvs are parsed, we should not be here */
	if (b_resp->count == 0) {
		return BCME_BADLEN;
	}

	/*  cbfn params may be used in f/w */
	if (len < sizeof(status)) {
		return BCME_BUFTOOSHORT;
	}

	/* first 4 bytes consists status */
	if (memcpy_s(&status, sizeof(status),
			data, sizeof(uint32)) != BCME_OK) {
		WL_ERR(("Failed to copy status\n"));
		goto exit;
	}

	status = dtoh32(status);

	/* If status is non zero */
	if (status != BCME_OK) {
		printf("cmd type %d failed, status: %04x\n", type, status);
		goto exit;
	}

	if (b_resp->count > 0) {
		b_resp->count--;
	}

	if (!b_resp->count) {
		status = BCME_IOV_LAST_CMD;
	}
exit:
	return status;
}

static int
wl_cfgnan_execute_ioctl(struct net_device *ndev, struct bcm_cfg80211 *cfg,
	bcm_iov_batch_buf_t *nan_buf, uint16 nan_buf_size, uint32 *status,
	uint8 *resp_buf, uint16 resp_buf_size)
{
	int ret = BCME_OK;
	uint16 tlvs_len;
	int res = BCME_OK;
	bcm_iov_batch_buf_t *p_resp = NULL;
	char *iov = "nan";
	int max_resp_len = WLC_IOCTL_MAXLEN;

	WL_DBG(("Enter:\n"));
	if (nan_buf->is_set) {
		ret = wldev_iovar_setbuf(ndev, "nan", nan_buf, nan_buf_size,
			resp_buf, resp_buf_size, NULL);
		p_resp = (bcm_iov_batch_buf_t *)(resp_buf + strlen(iov) + 1);
	} else {
		ret = wldev_iovar_getbuf(ndev, "nan", nan_buf, nan_buf_size,
			resp_buf, resp_buf_size, NULL);
		p_resp = (bcm_iov_batch_buf_t *)(resp_buf);
	}
	if (unlikely(ret)) {
		WL_ERR((" nan execute ioctl failed, error = %d \n", ret));
		goto fail;
	}

	p_resp->is_set = nan_buf->is_set;
	tlvs_len = max_resp_len - OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);

	/* Extract the tlvs and print their resp in cb fn */
	res = bcm_unpack_xtlv_buf((void *)p_resp, (const uint8 *)&p_resp->cmds[0],
		tlvs_len, BCM_IOV_CMD_OPT_ALIGN32, wl_cfgnan_get_iovars_status);

	if (res == BCME_IOV_LAST_CMD) {
		res = BCME_OK;
	}
fail:
	*status = res;
	WL_DBG((" nan ioctl ret %d status %d \n", ret, *status));
	return ret;

}

static int
wl_cfgnan_if_addr_handler(void *p_buf, uint16 *nan_buf_size,
		struct ether_addr *if_addr)
{
	/* nan enable */
	s32 ret = BCME_OK;
	uint16 subcmd_len;

	NAN_DBG_ENTER();

	if (p_buf != NULL) {
		bcm_iov_batch_subcmd_t *sub_cmd = (bcm_iov_batch_subcmd_t*)(p_buf);

		ret = wl_cfg_nan_check_cmd_len(*nan_buf_size,
				sizeof(*if_addr), &subcmd_len);
		if (unlikely(ret)) {
			WL_ERR(("nan_sub_cmd check failed\n"));
			goto fail;
		}

		/* Fill the sub_command block */
		sub_cmd->id = htod16(WL_NAN_CMD_CFG_IF_ADDR);
		sub_cmd->len = sizeof(sub_cmd->u.options) + sizeof(*if_addr);
		sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);
		ret = memcpy_s(sub_cmd->data, sizeof(*if_addr),
				(uint8 *)if_addr, sizeof(*if_addr));
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy if addr\n"));
			goto fail;
		}

		*nan_buf_size -= subcmd_len;
	} else {
		WL_ERR(("nan_iov_buf is NULL\n"));
		ret = BCME_ERROR;
		goto fail;
	}

fail:
	NAN_DBG_EXIT();
	return ret;
}

static int
wl_cfgnan_get_ver(struct net_device *ndev, struct bcm_cfg80211 *cfg)
{
	bcm_iov_batch_buf_t *nan_buf = NULL;
	s32 ret = BCME_OK;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	wl_nan_ver_t *nan_ver = NULL;
	uint16 subcmd_len;
	uint32 status;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	bcm_iov_batch_subcmd_t *sub_cmd_resp = NULL;
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE];

	NAN_DBG_ENTER();
	nan_buf = MALLOCZ(cfg->osh, nan_buf_size);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_buf_size -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);
	sub_cmd = (bcm_iov_batch_subcmd_t*)(uint8 *)(&nan_buf->cmds[0]);

	ret = wl_cfg_nan_check_cmd_len(nan_buf_size,
			sizeof(*nan_ver), &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		goto fail;
	}

	nan_ver = (wl_nan_ver_t *)sub_cmd->data;
	sub_cmd->id = htod16(WL_NAN_CMD_GLB_NAN_VER);
	sub_cmd->len = sizeof(sub_cmd->u.options) + sizeof(*nan_ver);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);
	nan_buf_size -= subcmd_len;
	nan_buf->count = 1;

	nan_buf->is_set = false;
	bzero(resp_buf, sizeof(resp_buf));
	nan_buf_size = NAN_IOCTL_BUF_SIZE - nan_buf_size;

	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, nan_buf_size, &status,
			(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
	if (unlikely(ret) || unlikely(status)) {
		WL_ERR(("get nan ver failed ret %d status %d \n",
				ret, status));
		goto fail;
	}

	sub_cmd_resp = &((bcm_iov_batch_buf_t *)(resp_buf))->cmds[0];
	nan_ver = ((wl_nan_ver_t *)&sub_cmd_resp->data[0]);
	if (!nan_ver) {
		ret = BCME_NOTFOUND;
		WL_ERR(("nan_ver not found: err = %d\n", ret));
		goto fail;
	}
	cfg->nancfg->version = *nan_ver;
	WL_INFORM_MEM(("Nan Version is %d\n", cfg->nancfg->version));

fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, NAN_IOCTL_BUF_SIZE);
	}
	NAN_DBG_EXIT();
	return ret;

}

static int
wl_cfgnan_set_if_addr(struct bcm_cfg80211 *cfg)
{
	s32 ret = BCME_OK;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	uint32 status;
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE];
	struct ether_addr if_addr;
	uint8 buf[NAN_IOCTL_BUF_SIZE];
	bcm_iov_batch_buf_t *nan_buf = (bcm_iov_batch_buf_t*)buf;
	bool rand_mac = cfg->nancfg->mac_rand;

	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_buf_size -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);
	if (rand_mac) {
		RANDOM_BYTES(if_addr.octet, 6);
		/* restore mcast and local admin bits to 0 and 1 */
		ETHER_SET_UNICAST(if_addr.octet);
		ETHER_SET_LOCALADDR(if_addr.octet);
	} else {
		/* Use primary MAC with the locally administered bit for the
		 * NAN NMI I/F
		 */
		if (wl_get_vif_macaddr(cfg, WL_IF_TYPE_NAN_NMI,
				if_addr.octet) != BCME_OK) {
			ret = -EINVAL;
			WL_ERR(("Failed to get mac addr for NMI\n"));
			goto fail;
		}
	}
	WL_INFORM_MEM(("%s: NMI " MACDBG "\n",
			__FUNCTION__, MAC2STRDBG(if_addr.octet)));
	ret = wl_cfgnan_if_addr_handler(&nan_buf->cmds[0],
			&nan_buf_size, &if_addr);
	if (unlikely(ret)) {
		WL_ERR(("Nan if addr handler sub_cmd set failed\n"));
		goto fail;
	}
	nan_buf->count++;
	nan_buf->is_set = true;
	nan_buf_size = NAN_IOCTL_BUF_SIZE - nan_buf_size;
	bzero(resp_buf, sizeof(resp_buf));
	ret = wl_cfgnan_execute_ioctl(bcmcfg_to_prmry_ndev(cfg), cfg,
			nan_buf, nan_buf_size, &status,
			(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
	if (unlikely(ret) || unlikely(status)) {
		WL_ERR(("nan if addr handler failed ret %d status %d\n",
				ret, status));
		goto fail;
	}
	ret = memcpy_s(cfg->nancfg->nan_nmi_mac, ETH_ALEN,
			if_addr.octet, ETH_ALEN);
	if (ret != BCME_OK) {
		WL_ERR(("Failed to copy nmi addr\n"));
		goto fail;
	}
	return ret;
fail:
	if (!rand_mac) {
		wl_release_vif_macaddr(cfg, if_addr.octet, WL_IF_TYPE_NAN_NMI);
	}

	return ret;
}

static int
wl_cfgnan_init_handler(void *p_buf, uint16 *nan_buf_size, bool val)
{
	/* nan enable */
	s32 ret = BCME_OK;
	uint16 subcmd_len;

	NAN_DBG_ENTER();

	if (p_buf != NULL) {
		bcm_iov_batch_subcmd_t *sub_cmd = (bcm_iov_batch_subcmd_t*)(p_buf);

		ret = wl_cfg_nan_check_cmd_len(*nan_buf_size,
				sizeof(val), &subcmd_len);
		if (unlikely(ret)) {
			WL_ERR(("nan_sub_cmd check failed\n"));
			goto fail;
		}

		/* Fill the sub_command block */
		sub_cmd->id = htod16(WL_NAN_CMD_CFG_NAN_INIT);
		sub_cmd->len = sizeof(sub_cmd->u.options) + sizeof(uint8);
		sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);
		ret = memcpy_s(sub_cmd->data, sizeof(uint8),
				(uint8*)&val, sizeof(uint8));
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy init value\n"));
			goto fail;
		}

		*nan_buf_size -= subcmd_len;
	} else {
		WL_ERR(("nan_iov_buf is NULL\n"));
		ret = BCME_ERROR;
		goto fail;
	}

fail:
	NAN_DBG_EXIT();
	return ret;
}

static int
wl_cfgnan_enable_handler(wl_nan_iov_t *nan_iov_data, bool val)
{
	/* nan enable */
	s32 ret = BCME_OK;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	uint16 subcmd_len;

	NAN_DBG_ENTER();

	sub_cmd = (bcm_iov_batch_subcmd_t*)(nan_iov_data->nan_iov_buf);

	ret = wl_cfg_nan_check_cmd_len(nan_iov_data->nan_iov_len,
			sizeof(val), &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		return ret;
	}

	/* Fill the sub_command block */
	sub_cmd->id = htod16(WL_NAN_CMD_CFG_NAN_ENAB);
	sub_cmd->len = sizeof(sub_cmd->u.options) + sizeof(uint8);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);
	ret = memcpy_s(sub_cmd->data, sizeof(uint8),
			(uint8*)&val, sizeof(uint8));
	if (ret != BCME_OK) {
		WL_ERR(("Failed to copy enab value\n"));
		return ret;
	}

	nan_iov_data->nan_iov_len -= subcmd_len;
	nan_iov_data->nan_iov_buf += subcmd_len;
	NAN_DBG_EXIT();
	return ret;
}

static int
wl_cfgnan_warmup_time_handler(nan_config_cmd_data_t *cmd_data,
		wl_nan_iov_t *nan_iov_data)
{
	/* wl nan warm_up_time */
	s32 ret = BCME_OK;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	wl_nan_warmup_time_ticks_t *wup_ticks = NULL;
	uint16 subcmd_len;
	NAN_DBG_ENTER();

	sub_cmd = (bcm_iov_batch_subcmd_t*)(nan_iov_data->nan_iov_buf);
	wup_ticks = (wl_nan_warmup_time_ticks_t *)sub_cmd->data;

	ret = wl_cfg_nan_check_cmd_len(nan_iov_data->nan_iov_len,
			sizeof(*wup_ticks), &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		return ret;
	}
	/* Fill the sub_command block */
	sub_cmd->id = htod16(WL_NAN_CMD_CFG_WARMUP_TIME);
	sub_cmd->len = sizeof(sub_cmd->u.options) +
		sizeof(*wup_ticks);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);
	*wup_ticks = cmd_data->warmup_time;

	nan_iov_data->nan_iov_len -= subcmd_len;
	nan_iov_data->nan_iov_buf += subcmd_len;

	NAN_DBG_EXIT();
	return ret;
}

static int
wl_cfgnan_set_election_metric(nan_config_cmd_data_t *cmd_data,
		wl_nan_iov_t *nan_iov_data, uint32 nan_attr_mask)
{
	s32 ret = BCME_OK;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	wl_nan_election_metric_config_t *metrics = NULL;
	uint16 subcmd_len;
	NAN_DBG_ENTER();

	sub_cmd =
		(bcm_iov_batch_subcmd_t*)(nan_iov_data->nan_iov_buf);
	ret = wl_cfg_nan_check_cmd_len(nan_iov_data->nan_iov_len,
			sizeof(*metrics), &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		goto fail;
	}

	metrics = (wl_nan_election_metric_config_t *)sub_cmd->data;

	if (nan_attr_mask & NAN_ATTR_RAND_FACTOR_CONFIG) {
		metrics->random_factor = (uint8)cmd_data->metrics.random_factor;
	}

	if ((!cmd_data->metrics.master_pref) ||
		(cmd_data->metrics.master_pref > NAN_MAXIMUM_MASTER_PREFERENCE)) {
		WL_TRACE(("Master Pref is 0 or greater than 254, hence sending random value\n"));
		/* Master pref for mobile devices can be from 1 - 127 as per Spec AppendixC */
		metrics->master_pref = (RANDOM32()%(NAN_MAXIMUM_MASTER_PREFERENCE/2)) + 1;
	} else {
		metrics->master_pref = (uint8)cmd_data->metrics.master_pref;
	}
	sub_cmd->id = htod16(WL_NAN_CMD_ELECTION_METRICS_CONFIG);
	sub_cmd->len = sizeof(sub_cmd->u.options) +
		sizeof(*metrics);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);

	nan_iov_data->nan_iov_len -= subcmd_len;
	nan_iov_data->nan_iov_buf += subcmd_len;

fail:
	NAN_DBG_EXIT();
	return ret;
}

static int
wl_cfgnan_set_rssi_proximity(nan_config_cmd_data_t *cmd_data,
		wl_nan_iov_t *nan_iov_data, uint32 nan_attr_mask)
{
	s32 ret = BCME_OK;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	wl_nan_rssi_notif_thld_t *rssi_notif_thld = NULL;
	uint16 subcmd_len;

	NAN_DBG_ENTER();
	sub_cmd = (bcm_iov_batch_subcmd_t*)(nan_iov_data->nan_iov_buf);

	rssi_notif_thld = (wl_nan_rssi_notif_thld_t *)sub_cmd->data;

	ret = wl_cfg_nan_check_cmd_len(nan_iov_data->nan_iov_len,
			sizeof(*rssi_notif_thld), &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		return ret;
	}
	if (nan_attr_mask & NAN_ATTR_RSSI_PROXIMITY_2G_CONFIG) {
		rssi_notif_thld->bcn_rssi_2g =
			cmd_data->rssi_attr.rssi_proximity_2dot4g_val;
	} else {
		/* Keeping RSSI threshold value to be -70dBm */
		rssi_notif_thld->bcn_rssi_2g = NAN_DEF_RSSI_NOTIF_THRESH;
	}

	if (nan_attr_mask & NAN_ATTR_RSSI_PROXIMITY_5G_CONFIG) {
		rssi_notif_thld->bcn_rssi_5g =
			cmd_data->rssi_attr.rssi_proximity_5g_val;
	} else {
		/* Keeping RSSI threshold value to be -70dBm */
		rssi_notif_thld->bcn_rssi_5g = NAN_DEF_RSSI_NOTIF_THRESH;
	}

	sub_cmd->id = htod16(WL_NAN_CMD_SYNC_BCN_RSSI_NOTIF_THRESHOLD);
	sub_cmd->len = htod16(sizeof(sub_cmd->u.options) + sizeof(*rssi_notif_thld));
	sub_cmd->u.options = htod32(BCM_XTLV_OPTION_ALIGN32);

	nan_iov_data->nan_iov_len -= subcmd_len;
	nan_iov_data->nan_iov_buf += subcmd_len;

	NAN_DBG_EXIT();
	return ret;
}

static int
wl_cfgnan_set_rssi_mid_or_close(nan_config_cmd_data_t *cmd_data,
		wl_nan_iov_t *nan_iov_data, uint32 nan_attr_mask)
{
	s32 ret = BCME_OK;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	wl_nan_rssi_thld_t *rssi_thld = NULL;
	uint16 subcmd_len;

	NAN_DBG_ENTER();
	sub_cmd = (bcm_iov_batch_subcmd_t*)(nan_iov_data->nan_iov_buf);
	rssi_thld = (wl_nan_rssi_thld_t *)sub_cmd->data;

	ret = wl_cfg_nan_check_cmd_len(nan_iov_data->nan_iov_len,
			sizeof(*rssi_thld), &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		return ret;
	}

	/*
	 * Keeping RSSI mid value -75dBm for both 2G and 5G
	 * Keeping RSSI close value -60dBm for both 2G and 5G
	 */
	if (nan_attr_mask & NAN_ATTR_RSSI_MIDDLE_2G_CONFIG) {
		rssi_thld->rssi_mid_2g =
			cmd_data->rssi_attr.rssi_middle_2dot4g_val;
	} else {
		rssi_thld->rssi_mid_2g = NAN_DEF_RSSI_MID;
	}

	if (nan_attr_mask & NAN_ATTR_RSSI_MIDDLE_5G_CONFIG) {
		rssi_thld->rssi_mid_5g =
			cmd_data->rssi_attr.rssi_middle_5g_val;
	} else {
		rssi_thld->rssi_mid_5g = NAN_DEF_RSSI_MID;
	}

	if (nan_attr_mask & NAN_ATTR_RSSI_CLOSE_CONFIG) {
		rssi_thld->rssi_close_2g =
			cmd_data->rssi_attr.rssi_close_2dot4g_val;
	} else {
		rssi_thld->rssi_close_2g = NAN_DEF_RSSI_CLOSE;
	}

	if (nan_attr_mask & NAN_ATTR_RSSI_CLOSE_5G_CONFIG) {
		rssi_thld->rssi_close_5g =
			cmd_data->rssi_attr.rssi_close_5g_val;
	} else {
		rssi_thld->rssi_close_5g = NAN_DEF_RSSI_CLOSE;
	}

	sub_cmd->id = htod16(WL_NAN_CMD_ELECTION_RSSI_THRESHOLD);
	sub_cmd->len = htod16(sizeof(sub_cmd->u.options) + sizeof(*rssi_thld));
	sub_cmd->u.options = htod32(BCM_XTLV_OPTION_ALIGN32);

	nan_iov_data->nan_iov_len -= subcmd_len;
	nan_iov_data->nan_iov_buf += subcmd_len;

	NAN_DBG_EXIT();
	return ret;
}

static int
check_for_valid_5gchan(struct net_device *ndev, uint8 chan)
{
	s32 ret = BCME_OK;
	uint bitmap;
	u8 ioctl_buf[WLC_IOCTL_SMLEN];
	uint32 chanspec_arg;
	NAN_DBG_ENTER();

	chanspec_arg = CH20MHZ_CHSPEC(chan);
	chanspec_arg = wl_chspec_host_to_driver(chanspec_arg);
	bzero(ioctl_buf, WLC_IOCTL_SMLEN);
	ret = wldev_iovar_getbuf(ndev, "per_chan_info",
			(void *)&chanspec_arg, sizeof(chanspec_arg),
			ioctl_buf, WLC_IOCTL_SMLEN, NULL);
	if (ret != BCME_OK) {
		WL_ERR(("Chaninfo for channel = %d, error %d\n", chan, ret));
		goto exit;
	}

	bitmap = dtoh32(*(uint *)ioctl_buf);
	if (!(bitmap & WL_CHAN_VALID_HW)) {
		WL_ERR(("Invalid channel\n"));
		ret = BCME_BADCHAN;
		goto exit;
	}

	if (!(bitmap & WL_CHAN_VALID_SW)) {
		WL_ERR(("Not supported in current locale\n"));
		ret = BCME_BADCHAN;
		goto exit;
	}
exit:
	NAN_DBG_EXIT();
	return ret;
}

static int
wl_cfgnan_set_nan_soc_chans(struct net_device *ndev, nan_config_cmd_data_t *cmd_data,
	wl_nan_iov_t *nan_iov_data, uint32 nan_attr_mask)
{
	s32 ret = BCME_OK;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	wl_nan_social_channels_t *soc_chans = NULL;
	uint16 subcmd_len;

	NAN_DBG_ENTER();

	sub_cmd = (bcm_iov_batch_subcmd_t*)(nan_iov_data->nan_iov_buf);
	soc_chans =
		(wl_nan_social_channels_t *)sub_cmd->data;

	ret = wl_cfg_nan_check_cmd_len(nan_iov_data->nan_iov_len,
			sizeof(*soc_chans), &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		return ret;
	}

	sub_cmd->id = htod16(WL_NAN_CMD_SYNC_SOCIAL_CHAN);
	sub_cmd->len = sizeof(sub_cmd->u.options) +
		sizeof(*soc_chans);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);
	if (nan_attr_mask & NAN_ATTR_2G_CHAN_CONFIG) {
		soc_chans->soc_chan_2g = cmd_data->chanspec[1];
	} else {
		soc_chans->soc_chan_2g = NAN_DEF_SOCIAL_CHAN_2G;
	}

	if (cmd_data->support_5g) {
		if (nan_attr_mask & NAN_ATTR_5G_CHAN_CONFIG) {
			soc_chans->soc_chan_5g = cmd_data->chanspec[2];
		} else {
			soc_chans->soc_chan_5g = NAN_DEF_SOCIAL_CHAN_5G;
		}
		ret = check_for_valid_5gchan(ndev, soc_chans->soc_chan_5g);
		if (ret != BCME_OK) {
			ret = check_for_valid_5gchan(ndev, NAN_DEF_SEC_SOCIAL_CHAN_5G);
			if (ret == BCME_OK) {
				soc_chans->soc_chan_5g = NAN_DEF_SEC_SOCIAL_CHAN_5G;
			} else {
				soc_chans->soc_chan_5g = 0;
				ret = BCME_OK;
				WL_ERR(("Current locale doesn't support 5G op"
					"continuing with 2G only operation\n"));
			}
		}
	} else {
		WL_DBG(("5G support is disabled\n"));
	}
	nan_iov_data->nan_iov_len -= subcmd_len;
	nan_iov_data->nan_iov_buf += subcmd_len;

	NAN_DBG_EXIT();
	return ret;
}

static int
wl_cfgnan_set_nan_scan_params(struct net_device *ndev, struct bcm_cfg80211 *cfg,
	nan_config_cmd_data_t *cmd_data, uint8 band_index, uint32 nan_attr_mask)
{
	bcm_iov_batch_buf_t *nan_buf = NULL;
	s32 ret = BCME_OK;
	uint16 nan_iov_start, nan_iov_end;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	uint16 subcmd_len;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	wl_nan_iov_t *nan_iov_data = NULL;
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE];
	wl_nan_scan_params_t *scan_params = NULL;
	uint32 status;

	NAN_DBG_ENTER();

	nan_buf = MALLOCZ(cfg->osh, nan_buf_size);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_iov_data = MALLOCZ(cfg->osh, sizeof(*nan_iov_data));
	if (!nan_iov_data) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_iov_data->nan_iov_len = nan_iov_start = NAN_IOCTL_BUF_SIZE;
	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_iov_data->nan_iov_buf = (uint8 *)(&nan_buf->cmds[0]);
	nan_iov_data->nan_iov_len -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);
	sub_cmd = (bcm_iov_batch_subcmd_t*)(nan_iov_data->nan_iov_buf);

	ret = wl_cfg_nan_check_cmd_len(nan_iov_data->nan_iov_len,
			sizeof(*scan_params), &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		goto fail;
	}
	scan_params = (wl_nan_scan_params_t *)sub_cmd->data;

	sub_cmd->id = htod16(WL_NAN_CMD_CFG_SCAN_PARAMS);
	sub_cmd->len = sizeof(sub_cmd->u.options) + sizeof(*scan_params);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);

	if (!band_index) {
		/* Fw default: Dwell time for 2G is 210 */
		if ((nan_attr_mask & NAN_ATTR_2G_DWELL_TIME_CONFIG) &&
			cmd_data->dwell_time[0]) {
			scan_params->dwell_time = cmd_data->dwell_time[0] +
				NAN_SCAN_DWELL_TIME_DELTA_MS;
		}
		/* Fw default: Scan period for 2G is 10 */
		if (nan_attr_mask & NAN_ATTR_2G_SCAN_PERIOD_CONFIG) {
			scan_params->scan_period = cmd_data->scan_period[0];
		}
	} else {
		if ((nan_attr_mask & NAN_ATTR_5G_DWELL_TIME_CONFIG) &&
			cmd_data->dwell_time[1]) {
			scan_params->dwell_time = cmd_data->dwell_time[1] +
				NAN_SCAN_DWELL_TIME_DELTA_MS;
		}
		if (nan_attr_mask & NAN_ATTR_5G_SCAN_PERIOD_CONFIG) {
			scan_params->scan_period = cmd_data->scan_period[1];
		}
	}
	scan_params->band_index = band_index;
	nan_buf->is_set = true;
	nan_buf->count++;

	/* Reduce the iov_len size by subcmd_len */
	nan_iov_data->nan_iov_len -= subcmd_len;
	nan_iov_end = nan_iov_data->nan_iov_len;
	nan_buf_size = (nan_iov_start - nan_iov_end);

	bzero(resp_buf, sizeof(resp_buf));
	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, nan_buf_size, &status,
			(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
	if (unlikely(ret) || unlikely(status)) {
		WL_ERR(("set nan scan params failed ret %d status %d \n", ret, status));
		goto fail;
	}
	WL_DBG(("set nan scan params successfull\n"));
fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, NAN_IOCTL_BUF_SIZE);
	}
	if (nan_iov_data) {
		MFREE(cfg->osh, nan_iov_data, sizeof(*nan_iov_data));
	}

	NAN_DBG_EXIT();
	return ret;
}

static uint16
wl_cfgnan_gen_rand_cluster_id(uint16 low_val, uint16 high_val)
{
	uint16 random_id;
	ulong random_seed;

	/* In negative case also, assigning to cluster_high value */
	if (low_val >= high_val)
	{
		random_id = high_val;
	} else {
		RANDOM_BYTES(&random_seed, sizeof(random_seed));
		random_id = (uint16)((random_seed % ((high_val + 1) -
				low_val)) + low_val);
	}
	return random_id;
}

static int
wl_cfgnan_set_cluster_id(nan_config_cmd_data_t *cmd_data,
		wl_nan_iov_t *nan_iov_data)
{
	s32 ret = BCME_OK;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	uint16 subcmd_len;

	NAN_DBG_ENTER();

	sub_cmd = (bcm_iov_batch_subcmd_t*)(nan_iov_data->nan_iov_buf);

	ret = wl_cfg_nan_check_cmd_len(nan_iov_data->nan_iov_len,
			(sizeof(cmd_data->clus_id) - sizeof(uint8)), &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		return ret;
	}

	cmd_data->clus_id.octet[0] = 0x50;
	cmd_data->clus_id.octet[1] = 0x6F;
	cmd_data->clus_id.octet[2] = 0x9A;
	cmd_data->clus_id.octet[3] = 0x01;
	hton16_ua_store(wl_cfgnan_gen_rand_cluster_id(cmd_data->cluster_low,
			cmd_data->cluster_high), &cmd_data->clus_id.octet[4]);

	WL_TRACE(("cluster_id = " MACDBG "\n", MAC2STRDBG(cmd_data->clus_id.octet)));

	sub_cmd->id = htod16(WL_NAN_CMD_CFG_CID);
	sub_cmd->len = sizeof(sub_cmd->u.options) + sizeof(cmd_data->clus_id);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);
	ret = memcpy_s(sub_cmd->data, sizeof(cmd_data->clus_id),
			(uint8 *)&cmd_data->clus_id,
			sizeof(cmd_data->clus_id));
	if (ret != BCME_OK) {
		WL_ERR(("Failed to copy clus id\n"));
		return ret;
	}

	nan_iov_data->nan_iov_len -= subcmd_len;
	nan_iov_data->nan_iov_buf += subcmd_len;

	NAN_DBG_EXIT();
	return ret;
}

static int
wl_cfgnan_set_hop_count_limit(nan_config_cmd_data_t *cmd_data,
		wl_nan_iov_t *nan_iov_data)
{
	s32 ret = BCME_OK;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	wl_nan_hop_count_t *hop_limit = NULL;
	uint16 subcmd_len;

	NAN_DBG_ENTER();

	sub_cmd = (bcm_iov_batch_subcmd_t*)(nan_iov_data->nan_iov_buf);
	hop_limit = (wl_nan_hop_count_t *)sub_cmd->data;

	ret = wl_cfg_nan_check_cmd_len(nan_iov_data->nan_iov_len,
			sizeof(*hop_limit), &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		return ret;
	}

	*hop_limit = cmd_data->hop_count_limit;
	sub_cmd->id = htod16(WL_NAN_CMD_CFG_HOP_LIMIT);
	sub_cmd->len = sizeof(sub_cmd->u.options) + sizeof(*hop_limit);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);

	nan_iov_data->nan_iov_len -= subcmd_len;
	nan_iov_data->nan_iov_buf += subcmd_len;

	NAN_DBG_EXIT();
	return ret;
}

static int
wl_cfgnan_set_sid_beacon_val(nan_config_cmd_data_t *cmd_data,
	wl_nan_iov_t *nan_iov_data, uint32 nan_attr_mask)
{
	s32 ret = BCME_OK;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	wl_nan_sid_beacon_control_t *sid_beacon = NULL;
	uint16 subcmd_len;

	NAN_DBG_ENTER();

	sub_cmd = (bcm_iov_batch_subcmd_t*)(nan_iov_data->nan_iov_buf);

	ret = wl_cfg_nan_check_cmd_len(nan_iov_data->nan_iov_len,
			sizeof(*sid_beacon), &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		return ret;
	}

	sid_beacon = (wl_nan_sid_beacon_control_t *)sub_cmd->data;
	sid_beacon->sid_enable = cmd_data->sid_beacon.sid_enable;
	/* Need to have separate flag for sub beacons
	 * sid_beacon->sub_sid_enable = cmd_data->sid_beacon.sub_sid_enable;
	 */
	if (nan_attr_mask & NAN_ATTR_SID_BEACON_CONFIG) {
		/* Limit for number of publish SIDs to be included in Beacons */
		sid_beacon->sid_count = cmd_data->sid_beacon.sid_count;
	}
	if (nan_attr_mask & NAN_ATTR_SUB_SID_BEACON_CONFIG) {
		/* Limit for number of subscribe SIDs to be included in Beacons */
		sid_beacon->sub_sid_count = cmd_data->sid_beacon.sub_sid_count;
	}
	sub_cmd->id = htod16(WL_NAN_CMD_CFG_SID_BEACON);
	sub_cmd->len = sizeof(sub_cmd->u.options) +
		sizeof(*sid_beacon);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);

	nan_iov_data->nan_iov_len -= subcmd_len;
	nan_iov_data->nan_iov_buf += subcmd_len;
	NAN_DBG_EXIT();
	return ret;
}

static int
wl_cfgnan_set_nan_oui(nan_config_cmd_data_t *cmd_data,
		wl_nan_iov_t *nan_iov_data)
{
	s32 ret = BCME_OK;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	uint16 subcmd_len;

	NAN_DBG_ENTER();

	sub_cmd = (bcm_iov_batch_subcmd_t*)(nan_iov_data->nan_iov_buf);

	ret = wl_cfg_nan_check_cmd_len(nan_iov_data->nan_iov_len,
			sizeof(cmd_data->nan_oui), &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		return ret;
	}

	sub_cmd->id = htod16(WL_NAN_CMD_CFG_OUI);
	sub_cmd->len = sizeof(sub_cmd->u.options) + sizeof(cmd_data->nan_oui);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);
	ret = memcpy_s(sub_cmd->data, sizeof(cmd_data->nan_oui),
			(uint32 *)&cmd_data->nan_oui,
			sizeof(cmd_data->nan_oui));
	if (ret != BCME_OK) {
		WL_ERR(("Failed to copy nan oui\n"));
		return ret;
	}

	nan_iov_data->nan_iov_len -= subcmd_len;
	nan_iov_data->nan_iov_buf += subcmd_len;
	NAN_DBG_EXIT();
	return ret;
}

static int
wl_cfgnan_set_awake_dws(struct net_device *ndev, nan_config_cmd_data_t *cmd_data,
		wl_nan_iov_t *nan_iov_data, struct bcm_cfg80211 *cfg, uint32 nan_attr_mask)
{
	s32 ret = BCME_OK;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	wl_nan_awake_dws_t *awake_dws = NULL;
	uint16 subcmd_len;
	NAN_DBG_ENTER();

	sub_cmd =
		(bcm_iov_batch_subcmd_t*)(nan_iov_data->nan_iov_buf);
	ret = wl_cfg_nan_check_cmd_len(nan_iov_data->nan_iov_len,
			sizeof(*awake_dws), &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		return ret;
	}

	awake_dws = (wl_nan_awake_dws_t *)sub_cmd->data;

	if (nan_attr_mask & NAN_ATTR_2G_DW_CONFIG) {
		awake_dws->dw_interval_2g = cmd_data->awake_dws.dw_interval_2g;
		if (!awake_dws->dw_interval_2g) {
			/* Set 2G awake dw value to fw default value 1 */
			awake_dws->dw_interval_2g = NAN_SYNC_DEF_AWAKE_DW;
		}
	} else {
		/* Set 2G awake dw value to fw default value 1 */
		awake_dws->dw_interval_2g = NAN_SYNC_DEF_AWAKE_DW;
	}

	if (cfg->nancfg->support_5g) {
		if (nan_attr_mask & NAN_ATTR_5G_DW_CONFIG) {
			awake_dws->dw_interval_5g = cmd_data->awake_dws.dw_interval_5g;
			/* config sync/discovery beacons on 5G band */
			ret = wl_cfgnan_config_control_flag(ndev, cfg,
					WL_NAN_CTRL_DISC_BEACON_TX_5G |
					WL_NAN_CTRL_SYNC_BEACON_TX_5G,
					0, WL_NAN_CMD_CFG_NAN_CONFIG,
					&(cmd_data->status),
					awake_dws->dw_interval_5g);
			if (unlikely(ret) || unlikely(cmd_data->status)) {
				WL_ERR((" nan control set config handler, ret = %d"
					" status = %d \n", ret, cmd_data->status));
				goto fail;
			}
		} else {
			/* Set 5G awake dw value to fw default value 1 */
			awake_dws->dw_interval_5g = NAN_SYNC_DEF_AWAKE_DW;
		}
	}

	sub_cmd->id = htod16(WL_NAN_CMD_SYNC_AWAKE_DWS);
	sub_cmd->len = sizeof(sub_cmd->u.options) +
		sizeof(*awake_dws);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);

	nan_iov_data->nan_iov_len -= subcmd_len;
	nan_iov_data->nan_iov_buf += subcmd_len;

fail:
	NAN_DBG_EXIT();
	return ret;
}

int
wl_cfgnan_set_enable_merge(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, uint8 enable, uint32 *status)
{
	bcm_iov_batch_buf_t *nan_buf = NULL;
	s32 ret = BCME_OK;
	uint16 nan_iov_start, nan_iov_end;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	uint16 subcmd_len;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	wl_nan_iov_t *nan_iov_data = NULL;
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE];
	wl_nan_merge_enable_t merge_enable;
	uint8 size_of_iov;

	NAN_DBG_ENTER();

	nan_buf = MALLOCZ(cfg->osh, nan_buf_size);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_iov_data = MALLOCZ(cfg->osh, sizeof(*nan_iov_data));
	if (!nan_iov_data) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	merge_enable = (wl_nan_merge_enable_t)enable;
	size_of_iov = sizeof(wl_nan_merge_enable_t);

	nan_iov_data->nan_iov_len = nan_iov_start = NAN_IOCTL_BUF_SIZE;
	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_iov_data->nan_iov_buf = (uint8 *)(&nan_buf->cmds[0]);
	nan_iov_data->nan_iov_len -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);
	sub_cmd = (bcm_iov_batch_subcmd_t*)(nan_iov_data->nan_iov_buf);

	ret = wl_cfg_nan_check_cmd_len(nan_iov_data->nan_iov_len,
		size_of_iov, &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		goto fail;
	}

	sub_cmd->id = htod16(WL_NAN_CMD_ELECTION_MERGE);
	sub_cmd->len = sizeof(sub_cmd->u.options) + size_of_iov;
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);

	/* Reduce the iov_len size by subcmd_len */
	nan_iov_data->nan_iov_len -= subcmd_len;
	nan_iov_end = nan_iov_data->nan_iov_len;
	nan_buf_size = (nan_iov_start - nan_iov_end);

	(void)memcpy_s(sub_cmd->data, nan_iov_data->nan_iov_len,
		&merge_enable, size_of_iov);

	nan_buf->is_set = true;
	nan_buf->count++;
	bzero(resp_buf, sizeof(resp_buf));
	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, nan_buf_size, status,
			(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
	if (unlikely(ret) || unlikely(*status)) {
		WL_ERR(("Merge enable %d failed ret %d status %d \n", merge_enable, ret, *status));
		goto fail;
	}
fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, NAN_IOCTL_BUF_SIZE);
	}
	if (nan_iov_data) {
		MFREE(cfg->osh, nan_iov_data, sizeof(*nan_iov_data));
	}
	NAN_DBG_EXIT();
	return ret;
}

static int
wl_cfgnan_set_disc_beacon_interval_handler(struct net_device *ndev, struct bcm_cfg80211 *cfg,
	wl_nan_disc_bcn_interval_t disc_beacon_interval)
{
	bcm_iov_batch_buf_t *nan_buf = NULL;
	s32 ret = BCME_OK;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	wl_nan_iov_t *nan_iov_data = NULL;
	uint32 status;
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE];
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	uint16 subcmd_len;
	uint8 size_of_iov;

	NAN_DBG_ENTER();
	NAN_MUTEX_LOCK();

	nan_buf = MALLOCZ(cfg->osh, nan_buf_size);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_iov_data = MALLOCZ(cfg->osh, sizeof(*nan_iov_data));
	if (!nan_iov_data) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	size_of_iov = sizeof(wl_nan_disc_bcn_interval_t);
	nan_iov_data->nan_iov_len = NAN_IOCTL_BUF_SIZE;
	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_iov_data->nan_iov_buf = (uint8 *)(&nan_buf->cmds[0]);
	nan_iov_data->nan_iov_len -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);

	sub_cmd = (bcm_iov_batch_subcmd_t*)(nan_iov_data->nan_iov_buf);
	ret = wl_cfg_nan_check_cmd_len(nan_iov_data->nan_iov_len,
			size_of_iov, &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		goto fail;
	}

	/* Choose default value discovery beacon interval  if value is zero */
	if (!disc_beacon_interval) {
		disc_beacon_interval = cfg->nancfg->support_5g ? NAN_DISC_BCN_INTERVAL_5G_DEF:
			NAN_DISC_BCN_INTERVAL_2G_DEF;
	}

	/* Fill the sub_command block */
	sub_cmd->id = htod16(WL_NAN_CMD_CFG_BCN_INTERVAL);
	sub_cmd->len = sizeof(sub_cmd->u.options) + size_of_iov;
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);
	ret = memcpy_s(sub_cmd->data, nan_iov_data->nan_iov_len,
			&disc_beacon_interval, size_of_iov);
	if (ret != BCME_OK) {
		WL_ERR(("Failed to copy disc_beacon_interval\n"));
		goto fail;
	}

	nan_iov_data->nan_iov_len -= subcmd_len;
	nan_iov_data->nan_iov_buf += subcmd_len;

	nan_buf->count++;
	nan_buf->is_set = true;
	nan_buf_size -= nan_iov_data->nan_iov_len;
	bzero(resp_buf, sizeof(resp_buf));
	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, nan_buf_size, &status,
			(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
	if (unlikely(ret) || unlikely(status)) {
		WL_ERR(("Failed to set disc beacon interval, ret = %d status = %d\n",
			ret, status));
		goto fail;
	}

fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, NAN_IOCTL_BUF_SIZE);
	}
	if (nan_iov_data) {
		MFREE(cfg->osh, nan_iov_data, sizeof(*nan_iov_data));
	}

	NAN_MUTEX_UNLOCK();
	NAN_DBG_EXIT();
	return ret;
}

void
wl_cfgnan_immediate_nan_disable_pending(struct bcm_cfg80211 *cfg)
{
	if (delayed_work_pending(&cfg->nancfg->nan_disable)) {
		WL_DBG(("Do immediate nan_disable work\n"));
		DHD_NAN_WAKE_UNLOCK(cfg->pub);
		if (cancel_delayed_work(&cfg->nancfg->nan_disable)) {
			schedule_delayed_work(&cfg->nancfg->nan_disable, 0);
		}
	}
}

int
wl_cfgnan_check_nan_disable_pending(struct bcm_cfg80211 *cfg,
	bool force_disable, bool is_sync_reqd)
{
	int ret = BCME_OK;
	struct net_device *ndev = NULL;

	if (delayed_work_pending(&cfg->nancfg->nan_disable)) {
		WL_DBG(("Cancel nan_disable work\n"));
		/*
		 * Nan gets disabled from dhd_stop(dev_close) and other frameworks contexts.
		 * Can't use cancel_work_sync from dhd_stop context for
		 * wl_cfgnan_delayed_disable since both contexts uses
		 * rtnl_lock resulting in deadlock. If dhd_stop gets invoked,
		 * rely on dhd_stop context to do the nan clean up work and
		 * just do return from delayed WQ based on state check.
		 */

		DHD_NAN_WAKE_UNLOCK(cfg->pub);

		if (is_sync_reqd == true) {
			cancel_delayed_work_sync(&cfg->nancfg->nan_disable);
		} else {
			cancel_delayed_work(&cfg->nancfg->nan_disable);
		}
		force_disable = true;
	}
	if ((force_disable == true) && (cfg->nancfg->nan_enable == true)) {
		ret = wl_cfgnan_disable(cfg);
		if (ret != BCME_OK) {
			WL_ERR(("failed to disable nan, error[%d]\n", ret));
		}
		/* Intentional fall through to cleanup framework */
		if (cfg->nancfg->notify_user == true) {
			ndev = bcmcfg_to_prmry_ndev(cfg);
			wl_cfgvendor_nan_send_async_disable_resp(ndev->ieee80211_ptr);
		}
	}
	return ret;
}

int
wl_cfgnan_start_handler(struct net_device *ndev, struct bcm_cfg80211 *cfg,
	nan_config_cmd_data_t *cmd_data, uint32 nan_attr_mask)
{
	s32 ret = BCME_OK;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	bcm_iov_batch_buf_t *nan_buf = NULL;
	wl_nan_iov_t *nan_iov_data = NULL;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(ndev);
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE];
	int i;
	s32 timeout = 0;
	nan_hal_capabilities_t capabilities;
	uint32 cfg_ctrl1_flags = 0;
	uint32 cfg_ctrl2_flags1 = 0;
	wl_nancfg_t *nancfg = cfg->nancfg;

	NAN_DBG_ENTER();

	if (!dhdp->up) {
		WL_ERR(("bus is already down, hence blocking nan start\n"));
		return BCME_ERROR;
	}

	/* Protect discovery creation. Ensure proper mutex precedence.
	 * If if_sync & nan_mutex comes together in same context, nan_mutex
	 * should follow if_sync.
	 */
	mutex_lock(&cfg->if_sync);
	NAN_MUTEX_LOCK();

#ifdef WL_IFACE_MGMT
	if ((ret = wl_cfg80211_handle_if_role_conflict(cfg, WL_IF_TYPE_NAN_NMI)) != BCME_OK) {
		WL_ERR(("Conflicting iface is present, cant support nan\n"));
		NAN_MUTEX_UNLOCK();
		mutex_unlock(&cfg->if_sync);
		goto fail;
	}
#endif /* WL_IFACE_MGMT */

	/* disable TDLS on NAN init  */
	wl_cfg80211_tdls_config(cfg, TDLS_STATE_NMI_CREATE, false);

	WL_INFORM_MEM(("Initializing NAN\n"));
	ret = wl_cfgnan_init(cfg);
	if (ret != BCME_OK) {
		WL_ERR(("failed to initialize NAN[%d]\n", ret));
		NAN_MUTEX_UNLOCK();
		mutex_unlock(&cfg->if_sync);
		goto fail;
	}

	ret = wl_cfgnan_get_ver(ndev, cfg);
	if (ret != BCME_OK) {
		WL_ERR(("failed to Nan IOV version[%d]\n", ret));
		NAN_MUTEX_UNLOCK();
		mutex_unlock(&cfg->if_sync);
		goto fail;
	}

	/* set nmi addr */
	ret = wl_cfgnan_set_if_addr(cfg);
	if (ret != BCME_OK) {
		WL_ERR(("Failed to set nmi address \n"));
		NAN_MUTEX_UNLOCK();
		mutex_unlock(&cfg->if_sync);
		goto fail;
	}
	nancfg->nan_event_recvd = false;
	NAN_MUTEX_UNLOCK();
	mutex_unlock(&cfg->if_sync);

	nan_buf = MALLOCZ(cfg->osh, nan_buf_size);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_iov_data = MALLOCZ(cfg->osh, sizeof(*nan_iov_data));
	if (!nan_iov_data) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_iov_data->nan_iov_len = NAN_IOCTL_BUF_SIZE;
	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_iov_data->nan_iov_buf = (uint8 *)(&nan_buf->cmds[0]);
	nan_iov_data->nan_iov_len -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);

	if (nan_attr_mask & NAN_ATTR_SYNC_DISC_2G_BEACON_CONFIG) {
		/* config sync/discovery beacons on 2G band */
		/* 2g is mandatory */
		if (!cmd_data->beacon_2g_val) {
			WL_ERR(("Invalid NAN config...2G is mandatory\n"));
			ret = BCME_BADARG;
		}
		cfg_ctrl1_flags |= (WL_NAN_CTRL_DISC_BEACON_TX_2G | WL_NAN_CTRL_SYNC_BEACON_TX_2G);
	}
	if (nan_attr_mask & NAN_ATTR_SYNC_DISC_5G_BEACON_CONFIG) {
		/* config sync/discovery beacons on 5G band */
		cfg_ctrl1_flags |= (WL_NAN_CTRL_DISC_BEACON_TX_5G | WL_NAN_CTRL_SYNC_BEACON_TX_5G);
	}

	if (cmd_data->warmup_time) {
		ret = wl_cfgnan_warmup_time_handler(cmd_data, nan_iov_data);
		if (unlikely(ret)) {
			WL_ERR(("warm up time handler sub_cmd set failed\n"));
			goto fail;
		}
		nan_buf->count++;
	}
	/* setting master preference and random factor */
	ret = wl_cfgnan_set_election_metric(cmd_data, nan_iov_data, nan_attr_mask);
	if (unlikely(ret)) {
		WL_ERR(("election_metric sub_cmd set failed\n"));
		goto fail;
	} else {
		nan_buf->count++;
	}

	/* setting nan social channels */
	ret = wl_cfgnan_set_nan_soc_chans(ndev, cmd_data, nan_iov_data, nan_attr_mask);
	if (unlikely(ret)) {
		WL_ERR(("nan social channels set failed\n"));
		goto fail;
	} else {
		/* Storing 5g capability which is reqd for avail chan config. */
		nancfg->support_5g = cmd_data->support_5g;
		nan_buf->count++;
	}

	if ((cmd_data->support_2g) && ((cmd_data->dwell_time[0]) ||
			(cmd_data->scan_period[0]))) {
		/* setting scan params */
		ret = wl_cfgnan_set_nan_scan_params(ndev, cfg, cmd_data, 0, nan_attr_mask);
		if (unlikely(ret)) {
			WL_ERR(("scan params set failed for 2g\n"));
			goto fail;
		}
	}

	if ((cmd_data->support_5g) && ((cmd_data->dwell_time[1]) ||
			(cmd_data->scan_period[1]))) {
		/* setting scan params */
		ret = wl_cfgnan_set_nan_scan_params(ndev, cfg, cmd_data,
			cmd_data->support_5g, nan_attr_mask);
		if (unlikely(ret)) {
			WL_ERR(("scan params set failed for 5g\n"));
			goto fail;
		}
	}

	/*
	 * A cluster_low value matching cluster_high indicates a request
	 * to join a cluster with that value.
	 * If the requested cluster is not found the
	 * device will start its own cluster
	 */
	/* For Debug purpose, using clust id compulsion */
	if (cmd_data->cluster_low == cmd_data->cluster_high) {
		/* device will merge to configured CID only */
		cfg_ctrl1_flags |= (WL_NAN_CTRL_MERGE_CONF_CID_ONLY);
	}
	/* setting cluster ID */
	ret = wl_cfgnan_set_cluster_id(cmd_data, nan_iov_data);
	if (unlikely(ret)) {
		WL_ERR(("cluster_id sub_cmd set failed\n"));
		goto fail;
	}
	nan_buf->count++;

	/* setting rssi proximaty values for 2.4GHz and 5GHz */
	ret = wl_cfgnan_set_rssi_proximity(cmd_data, nan_iov_data, nan_attr_mask);
	if (unlikely(ret)) {
		WL_ERR(("2.4GHz/5GHz rssi proximity threshold set failed\n"));
		goto fail;
	} else {
		nan_buf->count++;
	}

	/* setting rssi middle/close values for 2.4GHz and 5GHz */
	ret = wl_cfgnan_set_rssi_mid_or_close(cmd_data, nan_iov_data, nan_attr_mask);
	if (unlikely(ret)) {
		WL_ERR(("2.4GHz/5GHz rssi middle and close set failed\n"));
		goto fail;
	} else {
		nan_buf->count++;
	}

	/* setting hop count limit or threshold */
	if (nan_attr_mask & NAN_ATTR_HOP_COUNT_LIMIT_CONFIG) {
		ret = wl_cfgnan_set_hop_count_limit(cmd_data, nan_iov_data);
		if (unlikely(ret)) {
			WL_ERR(("hop_count_limit sub_cmd set failed\n"));
			goto fail;
		}
		nan_buf->count++;
	}

	/* setting sid beacon val */
	if ((nan_attr_mask & NAN_ATTR_SID_BEACON_CONFIG) ||
		(nan_attr_mask & NAN_ATTR_SUB_SID_BEACON_CONFIG)) {
		ret = wl_cfgnan_set_sid_beacon_val(cmd_data, nan_iov_data, nan_attr_mask);
		if (unlikely(ret)) {
			WL_ERR(("sid_beacon sub_cmd set failed\n"));
			goto fail;
		}
		nan_buf->count++;
	}

	/* setting nan oui */
	if (nan_attr_mask & NAN_ATTR_OUI_CONFIG) {
		ret = wl_cfgnan_set_nan_oui(cmd_data, nan_iov_data);
		if (unlikely(ret)) {
			WL_ERR(("nan_oui sub_cmd set failed\n"));
			goto fail;
		}
		nan_buf->count++;
	}

	/* setting nan awake dws */
	ret = wl_cfgnan_set_awake_dws(ndev, cmd_data,
			nan_iov_data, cfg, nan_attr_mask);
	if (unlikely(ret)) {
		WL_ERR(("nan awake dws set failed\n"));
		goto fail;
	} else {
		nan_buf->count++;
	}

	/* enable events */
	ret = wl_cfgnan_config_eventmask(ndev, cfg, cmd_data->disc_ind_cfg, false);
	if (unlikely(ret)) {
		WL_ERR(("Failed to config disc ind flag in event_mask, ret = %d\n", ret));
		goto fail;
	}

	/* setting nan enable sub_cmd */
	ret = wl_cfgnan_enable_handler(nan_iov_data, true);
	if (unlikely(ret)) {
		WL_ERR(("enable handler sub_cmd set failed\n"));
		goto fail;
	}
	nan_buf->count++;
	nan_buf->is_set = true;

	nan_buf_size -= nan_iov_data->nan_iov_len;
	memset(resp_buf, 0, sizeof(resp_buf));
	/* Reset conditon variable */
	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, nan_buf_size,
			&(cmd_data->status), (void*)resp_buf, NAN_IOCTL_BUF_SIZE);
	if (unlikely(ret) || unlikely(cmd_data->status)) {
		WL_ERR((" nan start handler, enable failed, ret = %d status = %d \n",
				ret, cmd_data->status));
		goto fail;
	}

	timeout = wait_event_timeout(nancfg->nan_event_wait,
		nancfg->nan_event_recvd, msecs_to_jiffies(NAN_START_STOP_TIMEOUT));
	if (!timeout) {
		WL_ERR(("Timed out while Waiting for WL_NAN_EVENT_START event !!!\n"));
		ret = BCME_ERROR;
		goto fail;
	}

	/* Default flags: set NAN proprietary rates and auto datapath confirm
	 * If auto datapath confirms is set, then DPCONF will be sent by FW
	 */
	cfg_ctrl1_flags |= (WL_NAN_CTRL_AUTO_DPCONF | WL_NAN_CTRL_PROP_RATE);

	/* set CFG CTRL flags */
	ret = wl_cfgnan_config_control_flag(ndev, cfg, cfg_ctrl1_flags,
			0, WL_NAN_CMD_CFG_NAN_CONFIG,
			&(cmd_data->status), true);
	if (unlikely(ret) || unlikely(cmd_data->status)) {
		WL_ERR((" nan ctrl1 config flags setting failed, ret = %d status = %d \n",
				ret, cmd_data->status));
		goto fail;
	}

	/* malloc for ndp peer list */
	if ((ret = wl_cfgnan_get_capablities_handler(ndev, cfg, &capabilities))
			== BCME_OK) {
		nancfg->max_ndp_count = capabilities.max_ndp_sessions;
		nancfg->max_ndi_supported = capabilities.max_ndi_interfaces;
		nancfg->nan_ndp_peer_info = MALLOCZ(cfg->osh,
				nancfg->max_ndp_count * sizeof(nan_ndp_peer_t));
		if (!nancfg->nan_ndp_peer_info) {
			WL_ERR(("%s: memory allocation failed\n", __func__));
			ret = BCME_NOMEM;
			goto fail;
		}

		if (!nancfg->ndi) {
			nancfg->ndi = MALLOCZ(cfg->osh,
					nancfg->max_ndi_supported * sizeof(*nancfg->ndi));
			if (!nancfg->ndi) {
				WL_ERR(("%s: memory allocation failed\n", __func__));
				ret = BCME_NOMEM;
				goto fail;
			}
		}
	} else {
		WL_ERR(("wl_cfgnan_get_capablities_handler failed, ret = %d\n", ret));
		goto fail;
	}

	BCM_REFERENCE(i);
#ifdef NAN_IFACE_CREATE_ON_UP
	for (i = 0; i < nancfg->max_ndi_supported; i++) {
		/* Create NDI using the information provided by user space */
		if (nancfg->ndi[i].in_use && !nancfg->ndi[i].created) {
			ret = wl_cfgnan_data_path_iface_create_delete_handler(ndev, cfg,
				nancfg->ndi[i].ifname,
				NAN_WIFI_SUBCMD_DATA_PATH_IFACE_CREATE, dhdp->up);
			if (ret) {
				WL_ERR(("failed to create ndp interface [%d]\n", ret));
				goto fail;
			}
			nancfg->ndi[i].created = true;
		}
	}
#endif /* NAN_IFACE_CREATE_ON_UP */

	/* Check if NDPE is capable and use_ndpe_attr is set by framework */
	/* TODO: For now enabling NDPE by default as framework is not setting use_ndpe_attr
	 * When (cmd_data->use_ndpe_attr) is set by framework, Add additional check for
	 * (cmd_data->use_ndpe_attr) as below
	 * if (capabilities.ndpe_attr_supported && cmd_data->use_ndpe_attr)
	 */
	if (capabilities.ndpe_attr_supported)
	{
		cfg_ctrl2_flags1 |= WL_NAN_CTRL2_FLAG1_NDPE_CAP;
		nancfg->ndpe_enabled = true;
	} else {
		/* reset NDPE capability in FW */
		ret = wl_cfgnan_config_control_flag(ndev, cfg, WL_NAN_CTRL2_FLAG1_NDPE_CAP,
				0, WL_NAN_CMD_CFG_NAN_CONFIG2,
				&(cmd_data->status), false);
		if (unlikely(ret) || unlikely(cmd_data->status)) {
			WL_ERR((" nan ctrl2 config flags resetting failed, ret = %d status = %d \n",
					ret, cmd_data->status));
			goto fail;
		}
		nancfg->ndpe_enabled = false;
	}

	/* set CFG CTRL2 flags1 and flags2 */
	ret = wl_cfgnan_config_control_flag(ndev, cfg, cfg_ctrl2_flags1,
			0, WL_NAN_CMD_CFG_NAN_CONFIG2,
			&(cmd_data->status), true);
	if (unlikely(ret) || unlikely(cmd_data->status)) {
		WL_ERR((" nan ctrl2 config flags setting failed, ret = %d status = %d \n",
				ret, cmd_data->status));
		goto fail;
	}

#ifdef RTT_SUPPORT
	/* Initialize geofence cfg */
	dhd_rtt_initialize_geofence_cfg(cfg->pub);
#endif /* RTT_SUPPORT */

	if (cmd_data->dw_early_termination > 0) {
		WL_ERR(("dw early termination is not supported, ignoring for now\n"));
	}

	if (nan_attr_mask & NAN_ATTR_DISC_BEACON_INTERVAL) {
		ret = wl_cfgnan_set_disc_beacon_interval_handler(ndev, cfg,
			cmd_data->disc_bcn_interval);
		if (unlikely(ret)) {
			WL_ERR(("Failed to set beacon interval\n"));
			goto fail;
		}
	}

	nancfg->nan_enable = true;
	WL_INFORM_MEM(("[NAN] Enable successfull \n"));

fail:
	/* Enable back TDLS if connected interface is <= 1 */
	wl_cfg80211_tdls_config(cfg, TDLS_STATE_IF_DELETE, false);

	/* reset conditon variable */
	nancfg->nan_event_recvd = false;
	if (unlikely(ret) || unlikely(cmd_data->status)) {
		nancfg->nan_enable = false;
		mutex_lock(&cfg->if_sync);
		ret = wl_cfg80211_delete_iface(cfg, WL_IF_TYPE_NAN);
		if (ret != BCME_OK) {
			WL_ERR(("failed to delete NDI[%d]\n", ret));
		}
		mutex_unlock(&cfg->if_sync);
		if (nancfg->nan_ndp_peer_info) {
			MFREE(cfg->osh, nancfg->nan_ndp_peer_info,
					nancfg->max_ndp_count * sizeof(nan_ndp_peer_t));
			nancfg->nan_ndp_peer_info = NULL;
		}
		if (nancfg->ndi) {
			MFREE(cfg->osh, nancfg->ndi,
					nancfg->max_ndi_supported * sizeof(*nancfg->ndi));
			nancfg->ndi = NULL;
		}
	}
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, NAN_IOCTL_BUF_SIZE);
	}
	if (nan_iov_data) {
		MFREE(cfg->osh, nan_iov_data, sizeof(*nan_iov_data));
	}

	NAN_DBG_EXIT();
	return ret;
}

static int
wl_cfgnan_disable(struct bcm_cfg80211 *cfg)
{
	s32 ret = BCME_OK;
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);

	NAN_DBG_ENTER();
	if ((cfg->nancfg->nan_init_state == TRUE) &&
			(cfg->nancfg->nan_enable == TRUE)) {
		struct net_device *ndev;
		ndev = bcmcfg_to_prmry_ndev(cfg);

		/* We have to remove NDIs so that P2P/Softap can work */
		ret = wl_cfg80211_delete_iface(cfg, WL_IF_TYPE_NAN);
		if (ret != BCME_OK) {
			WL_ERR(("failed to delete NDI[%d]\n", ret));
		}

		ret = wl_cfgnan_stop_handler(ndev, cfg);
		if (ret == -ENODEV) {
			WL_ERR(("Bus is down, no need to proceed\n"));
		} else if (ret != BCME_OK) {
			WL_ERR(("failed to stop nan, error[%d]\n", ret));
		}
		ret = wl_cfgnan_deinit(cfg, dhdp->up);
		if (ret != BCME_OK) {
			WL_ERR(("failed to de-initialize NAN[%d]\n", ret));
			if (!dhd_query_bus_erros(dhdp)) {
				ASSERT(0);
			}
		}
		wl_cfgnan_disable_cleanup(cfg);
	}
	NAN_DBG_EXIT();
	return ret;
}

static void
wl_cfgnan_send_stop_event(struct bcm_cfg80211 *cfg)
{
	s32 ret = BCME_OK;
	nan_event_data_t *nan_event_data = NULL;

	NAN_DBG_ENTER();

	nan_event_data = MALLOCZ(cfg->osh, sizeof(nan_event_data_t));
	if (!nan_event_data) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto exit;
	}
	bzero(nan_event_data, sizeof(nan_event_data_t));

	nan_event_data->status = NAN_STATUS_SUCCESS;
	ret = memcpy_s(nan_event_data->nan_reason, NAN_ERROR_STR_LEN,
			"NAN_STATUS_SUCCESS", strlen("NAN_STATUS_SUCCESS"));
	if (ret != BCME_OK) {
		WL_ERR(("Failed to copy nan reason string, ret = %d\n", ret));
		goto exit;
	}
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 13, 0)) || defined(WL_VENDOR_EXT_SUPPORT)
	ret = wl_cfgvendor_send_nan_event(cfg->wdev->wiphy, bcmcfg_to_prmry_ndev(cfg),
			GOOGLE_NAN_EVENT_DISABLED, nan_event_data);
	if (ret != BCME_OK) {
		WL_ERR(("Failed to send event to nan hal, (%d)\n",
				GOOGLE_NAN_EVENT_DISABLED));
	}
#endif /* (LINUX_VERSION_CODE > KERNEL_VERSION(3, 13, 0)) || defined(WL_VENDOR_EXT_SUPPORT) */
exit:
	if (nan_event_data) {
		MFREE(cfg->osh, nan_event_data, sizeof(nan_event_data_t));
	}
	NAN_DBG_EXIT();
	return;
}

static void
wl_cfgnan_disable_cleanup(struct bcm_cfg80211 *cfg)
{
	int i = 0;
	wl_nancfg_t *nancfg = cfg->nancfg;
#ifdef RTT_SUPPORT
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhdp);
	rtt_target_info_t *target_info = NULL;

	/* Delete the geofence rtt target list */
	dhd_rtt_delete_geofence_target_list(dhdp);
	/* Cancel pending retry timer if any */
	if (delayed_work_pending(&rtt_status->rtt_retry_timer)) {
		cancel_delayed_work_sync(&rtt_status->rtt_retry_timer);
	}
	/* Remove if any pending proxd timeout for nan-rtt */
	target_info = &rtt_status->rtt_config.target_info[rtt_status->cur_idx];
	if (target_info && target_info->peer == RTT_PEER_NAN) {
		/* Cancel pending proxd timeout work if any */
		if (delayed_work_pending(&rtt_status->proxd_timeout)) {
			cancel_delayed_work_sync(&rtt_status->proxd_timeout);
		}
	}
	/* Delete if any directed nan rtt session */
	dhd_rtt_delete_nan_session(dhdp);
#endif /* RTT_SUPPORT */
	/* Clear the NDP ID array and dp count */
	for (i = 0; i < NAN_MAX_NDP_PEER; i++) {
		nancfg->ndp_id[i] = 0;
	}
	nancfg->nan_dp_count = 0;
	if (nancfg->nan_ndp_peer_info) {
		MFREE(cfg->osh, nancfg->nan_ndp_peer_info,
			nancfg->max_ndp_count * sizeof(nan_ndp_peer_t));
		nancfg->nan_ndp_peer_info = NULL;
	}
	if (nancfg->ndi) {
		MFREE(cfg->osh, nancfg->ndi,
			nancfg->max_ndi_supported * sizeof(*nancfg->ndi));
		nancfg->ndi = NULL;
	}
	wl_cfg80211_concurrent_roam(cfg, false);
	return;
}

/*
 * Deferred nan disable work,
 * scheduled with NAN_DISABLE_CMD_DELAY
 * delay in order to remove any active nan dps
 */
void
wl_cfgnan_delayed_disable(struct work_struct *work)
{
	struct bcm_cfg80211 *cfg = NULL;
	struct net_device *ndev = NULL;
	wl_nancfg_t *nancfg = NULL;

	BCM_SET_CONTAINER_OF(nancfg, work, wl_nancfg_t, nan_disable.work);

	cfg = nancfg->cfg;

	rtnl_lock();
	if (nancfg->nan_enable == true) {
		wl_cfgnan_disable(cfg);
		ndev = bcmcfg_to_prmry_ndev(cfg);
		wl_cfgvendor_nan_send_async_disable_resp(ndev->ieee80211_ptr);
	} else {
		WL_INFORM_MEM(("nan is in disabled state\n"));
	}
	rtnl_unlock();

	DHD_NAN_WAKE_UNLOCK(cfg->pub);

	return;
}

int
wl_cfgnan_stop_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg)
{
	bcm_iov_batch_buf_t *nan_buf = NULL;
	s32 ret = BCME_OK;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	wl_nan_iov_t *nan_iov_data = NULL;
	uint32 status;
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE];
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
	wl_nancfg_t *nancfg = cfg->nancfg;

	NAN_DBG_ENTER();
	NAN_MUTEX_LOCK();

	if (!nancfg->nan_enable) {
		WL_INFORM(("Nan is not enabled\n"));
		ret = BCME_OK;
		goto fail;
	}

	if (dhdp->up != DHD_BUS_DOWN) {
		/*
		 * Framework doing cleanup(iface remove) on disable command,
		 * so avoiding event to prevent iface delete calls again
		 */
		WL_INFORM_MEM(("[NAN] Disabling Nan events\n"));
		wl_cfgnan_config_eventmask(ndev, cfg, 0, true);

		nan_buf = MALLOCZ(cfg->osh, nan_buf_size);
		if (!nan_buf) {
			WL_ERR(("%s: memory allocation failed\n", __func__));
			ret = BCME_NOMEM;
			goto fail;
		}

		nan_iov_data = MALLOCZ(cfg->osh, sizeof(*nan_iov_data));
		if (!nan_iov_data) {
			WL_ERR(("%s: memory allocation failed\n", __func__));
			ret = BCME_NOMEM;
			goto fail;
		}

		nan_iov_data->nan_iov_len = NAN_IOCTL_BUF_SIZE;
		nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
		nan_buf->count = 0;
		nan_iov_data->nan_iov_buf = (uint8 *)(&nan_buf->cmds[0]);
		nan_iov_data->nan_iov_len -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);

		ret = wl_cfgnan_enable_handler(nan_iov_data, false);
		if (unlikely(ret)) {
			WL_ERR(("nan disable handler failed\n"));
			goto fail;
		}
		nan_buf->count++;
		nan_buf->is_set = true;
		nan_buf_size -= nan_iov_data->nan_iov_len;
		bzero(resp_buf, sizeof(resp_buf));
		ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, nan_buf_size, &status,
				(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
		if (unlikely(ret) || unlikely(status)) {
			WL_ERR(("nan disable failed ret = %d status = %d\n", ret, status));
			goto fail;
		}
		/* Enable back TDLS if connected interface is <= 1 */
		wl_cfg80211_tdls_config(cfg, TDLS_STATE_IF_DELETE, false);
	}

	if (!nancfg->notify_user) {
		wl_cfgnan_send_stop_event(cfg);
	}
fail:
	/* Resetting instance ID mask */
	nancfg->inst_id_start = 0;
	memset(nancfg->svc_inst_id_mask, 0, sizeof(nancfg->svc_inst_id_mask));
	memset(nancfg->svc_info, 0, NAN_MAX_SVC_INST * sizeof(nan_svc_info_t));
	nancfg->nan_enable = false;
	WL_INFORM_MEM(("[NAN] Disable done\n"));

	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, NAN_IOCTL_BUF_SIZE);
	}
	if (nan_iov_data) {
		MFREE(cfg->osh, nan_iov_data, sizeof(*nan_iov_data));
	}

	NAN_MUTEX_UNLOCK();
	NAN_DBG_EXIT();
	return ret;
}

int
wl_cfgnan_config_handler(struct net_device *ndev, struct bcm_cfg80211 *cfg,
	nan_config_cmd_data_t *cmd_data, uint32 nan_attr_mask)
{
	bcm_iov_batch_buf_t *nan_buf = NULL;
	s32 ret = BCME_OK;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	wl_nan_iov_t *nan_iov_data = NULL;
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE];

	NAN_DBG_ENTER();

	/* Nan need to be enabled before configuring/updating params */
	if (!cfg->nancfg->nan_enable) {
		WL_INFORM(("nan is not enabled\n"));
		ret = BCME_NOTENABLED;
		goto fail;
	}

	nan_buf = MALLOCZ(cfg->osh, nan_buf_size);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_iov_data = MALLOCZ(cfg->osh, sizeof(*nan_iov_data));
	if (!nan_iov_data) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_iov_data->nan_iov_len = NAN_IOCTL_BUF_SIZE;
	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_iov_data->nan_iov_buf = (uint8 *)(&nan_buf->cmds[0]);
	nan_iov_data->nan_iov_len -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);

	/* setting sid beacon val */
	if ((nan_attr_mask & NAN_ATTR_SID_BEACON_CONFIG) ||
		(nan_attr_mask & NAN_ATTR_SUB_SID_BEACON_CONFIG)) {
		ret = wl_cfgnan_set_sid_beacon_val(cmd_data, nan_iov_data, nan_attr_mask);
		if (unlikely(ret)) {
			WL_ERR(("sid_beacon sub_cmd set failed\n"));
			goto fail;
		}
		nan_buf->count++;
	}

	/* setting master preference and random factor */
	if (cmd_data->metrics.random_factor ||
		cmd_data->metrics.master_pref) {
		ret = wl_cfgnan_set_election_metric(cmd_data, nan_iov_data,
				nan_attr_mask);
		if (unlikely(ret)) {
			WL_ERR(("election_metric sub_cmd set failed\n"));
			goto fail;
		} else {
			nan_buf->count++;
		}
	}

	/* setting hop count limit or threshold */
	if (nan_attr_mask & NAN_ATTR_HOP_COUNT_LIMIT_CONFIG) {
		ret = wl_cfgnan_set_hop_count_limit(cmd_data, nan_iov_data);
		if (unlikely(ret)) {
			WL_ERR(("hop_count_limit sub_cmd set failed\n"));
			goto fail;
		}
		nan_buf->count++;
	}

	/* setting rssi proximaty values for 2.4GHz and 5GHz */
	ret = wl_cfgnan_set_rssi_proximity(cmd_data, nan_iov_data,
			nan_attr_mask);
	if (unlikely(ret)) {
		WL_ERR(("2.4GHz/5GHz rssi proximity threshold set failed\n"));
		goto fail;
	} else {
		nan_buf->count++;
	}

	/* setting nan awake dws */
	ret = wl_cfgnan_set_awake_dws(ndev, cmd_data, nan_iov_data,
		cfg, nan_attr_mask);
	if (unlikely(ret)) {
		WL_ERR(("nan awake dws set failed\n"));
		goto fail;
	} else {
		nan_buf->count++;
	}

	/* TODO: Add below code once use_ndpe_attr is being updated by framework
	 * If NDPE is enabled (cfg.nancfg.ndpe_enabled) and use_ndpe_attr is reset
	 * by framework, then disable NDPE using nan ctrl2 configuration setting.
	 * Else if NDPE is disabled and use_ndpe_attr is set by framework enable NDPE in FW
	 */

	if (cmd_data->disc_ind_cfg) {
		/* Disable events */
		WL_TRACE(("Disable events based on flag\n"));
		ret = wl_cfgnan_config_eventmask(ndev, cfg,
			cmd_data->disc_ind_cfg, false);
		if (unlikely(ret)) {
			WL_ERR(("Failed to config disc ind flag in event_mask, ret = %d\n",
				ret));
			goto fail;
		}
	}

	if ((cfg->nancfg->support_5g) && ((cmd_data->dwell_time[1]) ||
			(cmd_data->scan_period[1]))) {
		/* setting scan params */
		ret = wl_cfgnan_set_nan_scan_params(ndev, cfg,
				cmd_data, cfg->nancfg->support_5g, nan_attr_mask);
		if (unlikely(ret)) {
			WL_ERR(("scan params set failed for 5g\n"));
			goto fail;
		}
	}
	if ((cmd_data->dwell_time[0]) ||
			(cmd_data->scan_period[0])) {
		ret = wl_cfgnan_set_nan_scan_params(ndev, cfg, cmd_data, 0, nan_attr_mask);
		if (unlikely(ret)) {
			WL_ERR(("scan params set failed for 2g\n"));
			goto fail;
		}
	}
	nan_buf->is_set = true;
	nan_buf_size -= nan_iov_data->nan_iov_len;

	if (nan_buf->count) {
		bzero(resp_buf, sizeof(resp_buf));
		ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, nan_buf_size,
				&(cmd_data->status),
				(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
		if (unlikely(ret) || unlikely(cmd_data->status)) {
			WL_ERR((" nan config handler failed ret = %d status = %d\n",
				ret, cmd_data->status));
			goto fail;
		}
	} else {
		WL_DBG(("No commands to send\n"));
	}

	if ((!cmd_data->bmap) || (cmd_data->avail_params.duration == NAN_BAND_INVALID) ||
			(!cmd_data->chanspec[0])) {
		WL_TRACE(("mandatory arguments are not present to set avail\n"));
		ret = BCME_OK;
	} else {
		cmd_data->avail_params.chanspec[0] = cmd_data->chanspec[0];
		cmd_data->avail_params.bmap = cmd_data->bmap;
		/* 1=local, 2=peer, 3=ndc, 4=immutable, 5=response, 6=counter */
		ret = wl_cfgnan_set_nan_avail(bcmcfg_to_prmry_ndev(cfg),
				cfg, &cmd_data->avail_params, WL_AVAIL_LOCAL);
		if (unlikely(ret)) {
			WL_ERR(("Failed to set avail value with type local\n"));
			goto fail;
		}

		ret = wl_cfgnan_set_nan_avail(bcmcfg_to_prmry_ndev(cfg),
				cfg, &cmd_data->avail_params, WL_AVAIL_NDC);
		if (unlikely(ret)) {
			WL_ERR(("Failed to set avail value with type ndc\n"));
			goto fail;
		}
	}

	if (cmd_data->nmi_rand_intvl > 0) {
#ifdef WL_NAN_ENABLE_MERGE
		/* Cluster merge enable/disable are being set using nmi random interval config param
		 * If MSB(31st bit) is set that indicates cluster merge enable/disable config is set
		 * MSB 30th bit indicates cluser merge enable/disable value to set in firmware
		 */
		if (cmd_data->nmi_rand_intvl & NAN_NMI_RAND_PVT_CMD_VENDOR) {
			uint8 merge_enable;
			uint8 lwt_mode_enable;
			int status = BCME_OK;

			merge_enable = !!(cmd_data->nmi_rand_intvl &
					NAN_NMI_RAND_CLUSTER_MERGE_ENAB);
			ret = wl_cfgnan_set_enable_merge(bcmcfg_to_prmry_ndev(cfg), cfg,
					merge_enable, &status);
			if (unlikely(ret) || unlikely(status)) {
				WL_ERR(("Enable merge: failed to set config request  [%d]\n", ret));
				/* As there is no cmd_reply, check if error is in status or ret */
				if (status) {
					ret = status;
				}
				goto fail;
			}

			lwt_mode_enable = !!(cmd_data->nmi_rand_intvl &
					NAN_NMI_RAND_AUTODAM_LWT_MODE_ENAB);

			/* set CFG CTRL2 flags1 and flags2 */
			ret = wl_cfgnan_config_control_flag(ndev, cfg,
					WL_NAN_CTRL2_FLAG1_AUTODAM_LWT_MODE,
					0, WL_NAN_CMD_CFG_NAN_CONFIG2,
					&status, lwt_mode_enable);
			if (unlikely(ret) || unlikely(status)) {
				WL_ERR(("Enable dam lwt mode: "
					"failed to set config request  [%d]\n", ret));
				/* As there is no cmd_reply, check if error is in status or ret */
				if (status) {
					ret = status;
				}
				goto fail;
			}

			/* reset pvt merge enable bits */
			cmd_data->nmi_rand_intvl &= ~(NAN_NMI_RAND_PVT_CMD_VENDOR |
					NAN_NMI_RAND_CLUSTER_MERGE_ENAB |
					NAN_NMI_RAND_AUTODAM_LWT_MODE_ENAB);
		}
#endif /* WL_NAN_ENABLE_MERGE */

		if (cmd_data->nmi_rand_intvl) {
			/* run time nmi rand not supported as of now.
			 * Only during nan enable/iface-create rand mac is used
			 */
			WL_ERR(("run time nmi rand not supported, ignoring for now\n"));
		}
	}

	if (cmd_data->dw_early_termination > 0) {
		WL_ERR(("dw early termination is not supported, ignoring for now\n"));
	}

	if (nan_attr_mask & NAN_ATTR_DISC_BEACON_INTERVAL) {
		ret = wl_cfgnan_set_disc_beacon_interval_handler(ndev, cfg,
			cmd_data->disc_bcn_interval);
		if (unlikely(ret)) {
			WL_ERR(("Failed to set beacon interval\n"));
			goto fail;
		}
	}

fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, NAN_IOCTL_BUF_SIZE);
	}
	if (nan_iov_data) {
		MFREE(cfg->osh, nan_iov_data, sizeof(*nan_iov_data));
	}

	NAN_DBG_EXIT();
	return ret;
}

int
wl_cfgnan_support_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_config_cmd_data_t *cmd_data)
{
	/* TODO: */
	return BCME_OK;
}

int
wl_cfgnan_status_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_config_cmd_data_t *cmd_data)
{
	/* TODO: */
	return BCME_OK;
}

#ifdef WL_NAN_DISC_CACHE
static
nan_svc_info_t *
wl_cfgnan_get_svc_inst(struct bcm_cfg80211 *cfg,
	wl_nan_instance_id svc_inst_id, uint8 ndp_id)
{
	uint8 i, j;
	wl_nancfg_t *nancfg = cfg->nancfg;
	if (ndp_id) {
		for (i = 0; i < NAN_MAX_SVC_INST; i++) {
			for (j = 0; j < NAN_MAX_SVC_INST; j++) {
				if (nancfg->svc_info[i].ndp_id[j] == ndp_id) {
					return &nancfg->svc_info[i];
				}
			}
		}
	} else if (svc_inst_id) {
		for (i = 0; i < NAN_MAX_SVC_INST; i++) {
			if (nancfg->svc_info[i].svc_id == svc_inst_id) {
				return &nancfg->svc_info[i];
			}
		}

	}
	return NULL;
}

static int
wl_cfgnan_svc_inst_add_ndp(struct bcm_cfg80211 *cfg,
	wl_nan_instance_id svc_inst_id, uint8 ndp_id)
{
	int ret = BCME_OK, i;
	nan_svc_info_t *svc_info;

	svc_info = wl_cfgnan_get_svc_inst(cfg, svc_inst_id, 0);
	if (svc_info) {
		for (i = 0; i < NAN_MAX_SVC_INST; i++) {
			if (!svc_info->ndp_id[i]) {
				WL_TRACE(("Found empty field\n"));
				break;
			}
		}
		if (i == NAN_MAX_SVC_INST) {
			WL_ERR(("%s:cannot accommadate ndp id\n", __FUNCTION__));
			ret = BCME_NORESOURCE;
			goto done;
		}
		svc_info->ndp_id[i] = ndp_id;
	}

done:
	return ret;
}

static int
wl_cfgnan_svc_inst_del_ndp(struct bcm_cfg80211 *cfg,
	wl_nan_instance_id svc_inst_id, uint8 ndp_id)
{
	int ret = BCME_OK, i;
	nan_svc_info_t *svc_info;

	svc_info = wl_cfgnan_get_svc_inst(cfg, svc_inst_id, 0);

	if (svc_info) {
		for (i = 0; i < NAN_MAX_SVC_INST; i++) {
			if (svc_info->ndp_id[i] == ndp_id) {
				svc_info->ndp_id[i] = 0;
				break;
			}
		}
		if (i == NAN_MAX_SVC_INST) {
			WL_ERR(("couldn't find entry for ndp id = %d\n", ndp_id));
			ret = BCME_NOTFOUND;
		}
	}
	return ret;
}

nan_ranging_inst_t *
wl_cfgnan_check_for_ranging(struct bcm_cfg80211 *cfg, struct ether_addr *peer)
{
	uint8 i;
	if (peer) {
		for (i = 0; i < NAN_MAX_RANGING_INST; i++) {
			if (!memcmp(peer, &cfg->nancfg->nan_ranging_info[i].peer_addr,
				ETHER_ADDR_LEN)) {
				return &(cfg->nancfg->nan_ranging_info[i]);
			}
		}
	}
	return NULL;
}

nan_ranging_inst_t *
wl_cfgnan_get_rng_inst_by_id(struct bcm_cfg80211 *cfg, uint8 rng_id)
{
	uint8 i;
	if (rng_id) {
		for (i = 0; i < NAN_MAX_RANGING_INST; i++) {
			if (cfg->nancfg->nan_ranging_info[i].range_id == rng_id)
			{
				return &(cfg->nancfg->nan_ranging_info[i]);
			}
		}
	}
	WL_ERR(("Couldn't find the ranging instance for rng_id %d\n", rng_id));
	return NULL;
}

/*
 * Find ranging inst for given peer,
 * On not found, create one
 * with given range role
 */
nan_ranging_inst_t *
wl_cfgnan_get_ranging_inst(struct bcm_cfg80211 *cfg, struct ether_addr *peer,
	nan_range_role_t range_role)
{
	nan_ranging_inst_t *ranging_inst = NULL;
	uint8 i;

	if (!peer) {
		WL_ERR(("Peer address is NULL"));
		goto done;
	}

	ranging_inst = wl_cfgnan_check_for_ranging(cfg, peer);
	if (ranging_inst) {
		goto done;
	}
	WL_TRACE(("Creating Ranging instance \n"));

	for (i =  0; i < NAN_MAX_RANGING_INST; i++) {
		if (cfg->nancfg->nan_ranging_info[i].in_use == FALSE) {
			break;
		}
	}

	if (i == NAN_MAX_RANGING_INST) {
		WL_ERR(("No buffer available for the ranging instance"));
		goto done;
	}
	ranging_inst = &cfg->nancfg->nan_ranging_info[i];
	memcpy(&ranging_inst->peer_addr, peer, ETHER_ADDR_LEN);
	ranging_inst->range_status = NAN_RANGING_REQUIRED;
	ranging_inst->prev_distance_mm = INVALID_DISTANCE;
	ranging_inst->range_role = range_role;
	ranging_inst->in_use = TRUE;

done:
	return ranging_inst;
}
#endif /* WL_NAN_DISC_CACHE */

static int
process_resp_buf(void *iov_resp,
	uint8 *instance_id, uint16 sub_cmd_id)
{
	int res = BCME_OK;
	NAN_DBG_ENTER();

	if (sub_cmd_id == WL_NAN_CMD_DATA_DATAREQ) {
		wl_nan_dp_req_ret_t *dpreq_ret = NULL;
		dpreq_ret = (wl_nan_dp_req_ret_t *)(iov_resp);
		*instance_id = dpreq_ret->ndp_id;
		WL_TRACE(("%s: Initiator NDI: " MACDBG "\n",
			__FUNCTION__, MAC2STRDBG(dpreq_ret->indi.octet)));
	} else if (sub_cmd_id == WL_NAN_CMD_RANGE_REQUEST) {
		wl_nan_range_id *range_id = NULL;
		range_id = (wl_nan_range_id *)(iov_resp);
		*instance_id = *range_id;
		WL_TRACE(("Range id: %d\n", *range_id));
	}
	WL_DBG(("instance_id: %d\n", *instance_id));
	NAN_DBG_EXIT();
	return res;
}

int
wl_cfgnan_cancel_ranging(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, uint8 *range_id, uint8 flags, uint32 *status)
{
	bcm_iov_batch_buf_t *nan_buf = NULL;
	s32 ret = BCME_OK;
	uint16 nan_iov_start, nan_iov_end;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	uint16 subcmd_len;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	wl_nan_iov_t *nan_iov_data = NULL;
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE];
	wl_nan_range_cancel_ext_t rng_cncl;
	uint8 size_of_iov;

	NAN_DBG_ENTER();

	if (*range_id == 0) {
		WL_ERR(("Invalid Range ID\n"));
		ret = BCME_BADARG;
		goto fail;
	}

	if (cfg->nancfg->version >= NAN_RANGE_EXT_CANCEL_SUPPORT_VER) {
		size_of_iov = sizeof(rng_cncl);
	} else {
		size_of_iov = sizeof(*range_id);
	}

	bzero(&rng_cncl, sizeof(rng_cncl));
	rng_cncl.range_id = *range_id;
	rng_cncl.flags = flags;

	nan_buf = MALLOCZ(cfg->osh, nan_buf_size);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_iov_data = MALLOCZ(cfg->osh, sizeof(*nan_iov_data));
	if (!nan_iov_data) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_iov_data->nan_iov_len = nan_iov_start = NAN_IOCTL_BUF_SIZE;
	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_iov_data->nan_iov_buf = (uint8 *)(&nan_buf->cmds[0]);
	nan_iov_data->nan_iov_len -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);
	sub_cmd = (bcm_iov_batch_subcmd_t*)(nan_iov_data->nan_iov_buf);

	ret = wl_cfg_nan_check_cmd_len(nan_iov_data->nan_iov_len,
		size_of_iov, &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		goto fail;
	}

	sub_cmd->id = htod16(WL_NAN_CMD_RANGE_CANCEL);
	sub_cmd->len = sizeof(sub_cmd->u.options) + size_of_iov;
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);

	/* Reduce the iov_len size by subcmd_len */
	nan_iov_data->nan_iov_len -= subcmd_len;
	nan_iov_end = nan_iov_data->nan_iov_len;
	nan_buf_size = (nan_iov_start - nan_iov_end);

	if (size_of_iov >= sizeof(rng_cncl)) {
		(void)memcpy_s(sub_cmd->data, nan_iov_data->nan_iov_len,
			&rng_cncl, size_of_iov);
	} else {
		(void)memcpy_s(sub_cmd->data, nan_iov_data->nan_iov_len,
			range_id, size_of_iov);
	}

	nan_buf->is_set = true;
	nan_buf->count++;
	bzero(resp_buf, sizeof(resp_buf));
	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, nan_buf_size, status,
			(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
	if (unlikely(ret) || unlikely(*status)) {
		WL_ERR(("Range ID %d cancel failed ret %d status %d \n", *range_id, ret, *status));
		goto fail;
	}
	WL_MEM(("Range cancel with Range ID [%d] successfull\n", *range_id));

	/* Resetting range id */
	*range_id = 0;
fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, NAN_IOCTL_BUF_SIZE);
	}
	if (nan_iov_data) {
		MFREE(cfg->osh, nan_iov_data, sizeof(*nan_iov_data));
	}
	NAN_DBG_EXIT();
	return ret;
}

#ifdef WL_NAN_DISC_CACHE
static void
wl_cfgnan_clear_svc_cache(struct bcm_cfg80211 *cfg,
	wl_nan_instance_id svc_id)
{
	nan_svc_info_t *svc;
	svc = wl_cfgnan_get_svc_inst(cfg, svc_id, 0);
	if (svc) {
		WL_DBG(("clearing cached svc info for svc id %d\n", svc_id));
		memset(svc, 0, sizeof(*svc));
	}
}

static int
wl_cfgnan_cache_svc_info(struct bcm_cfg80211 *cfg,
	nan_discover_cmd_data_t *cmd_data, uint16 cmd_id, bool update)
{
	int ret = BCME_OK;
	int i;
	nan_svc_info_t *svc_info;
	uint8 svc_id = (cmd_id == WL_NAN_CMD_SD_SUBSCRIBE) ? cmd_data->sub_id :
		cmd_data->pub_id;
	wl_nancfg_t *nancfg = cfg->nancfg;

	for (i = 0; i < NAN_MAX_SVC_INST; i++) {
		if (update) {
			if (nancfg->svc_info[i].svc_id == svc_id) {
				svc_info = &nancfg->svc_info[i];
				break;
			} else {
				continue;
			}
		}
		if (!nancfg->svc_info[i].svc_id) {
			svc_info = &nancfg->svc_info[i];
			break;
		}
	}
	if (i == NAN_MAX_SVC_INST) {
		WL_ERR(("%s:cannot accomodate ranging session\n", __FUNCTION__));
		ret = BCME_NORESOURCE;
		goto fail;
	}
	if (cmd_data->sde_control_flag & NAN_SDE_CF_RANGING_REQUIRED) {
		WL_TRACE(("%s: updating ranging info, enabling", __FUNCTION__));
		svc_info->status = 1;
		svc_info->ranging_interval = cmd_data->ranging_intvl_msec;
		svc_info->ranging_ind = cmd_data->ranging_indication;
		svc_info->ingress_limit = cmd_data->ingress_limit;
		svc_info->egress_limit = cmd_data->egress_limit;
		svc_info->ranging_required = 1;
	} else {
		WL_TRACE(("%s: updating ranging info, disabling", __FUNCTION__));
		svc_info->status = 0;
		svc_info->ranging_interval = 0;
		svc_info->ranging_ind = 0;
		svc_info->ingress_limit = 0;
		svc_info->egress_limit = 0;
		svc_info->ranging_required = 0;
	}

	/* Reset Range status flags on svc creation/update */
	svc_info->svc_range_status = 0;
	svc_info->flags = cmd_data->flags;

	if (cmd_id == WL_NAN_CMD_SD_SUBSCRIBE) {
		svc_info->svc_id = cmd_data->sub_id;
		if ((cmd_data->flags & WL_NAN_SUB_ACTIVE) &&
			(cmd_data->tx_match.dlen)) {
			ret = memcpy_s(svc_info->tx_match_filter, sizeof(svc_info->tx_match_filter),
				cmd_data->tx_match.data, cmd_data->tx_match.dlen);
			if (ret != BCME_OK) {
				WL_ERR(("Failed to copy tx match filter data\n"));
				goto fail;
			}
			svc_info->tx_match_filter_len = cmd_data->tx_match.dlen;
		}
	} else {
		svc_info->svc_id = cmd_data->pub_id;
	}
	ret = memcpy_s(svc_info->svc_hash, sizeof(svc_info->svc_hash),
			cmd_data->svc_hash.data, WL_NAN_SVC_HASH_LEN);
	if (ret != BCME_OK) {
		WL_ERR(("Failed to copy svc hash\n"));
	}
fail:
	return ret;

}

#ifdef RTT_SUPPORT
/*
 * Reset for Initiator
 * Remove for Responder if no pending
 * geofence target or else reset
 */
static void
wl_cfgnan_reset_remove_ranging_instance(struct bcm_cfg80211 *cfg,
        nan_ranging_inst_t *ranging_inst)
{
	dhd_pub_t *dhd = (struct dhd_pub *)(cfg->pub);
	int8 index;
	rtt_geofence_target_info_t* geofence_target;

	ASSERT(ranging_inst);
	if (!ranging_inst) {
		return;
	}

	if ((ranging_inst->range_role == NAN_RANGING_ROLE_RESPONDER) ||
		(ranging_inst->range_type == RTT_TYPE_NAN_DIRECTED)) {
		/* Remove ranging instance for responder */
		geofence_target = dhd_rtt_get_geofence_target(dhd,
				&ranging_inst->peer_addr, &index);
		if (!geofence_target) {
			/* Remove rng inst if no pend target */
			WL_INFORM_MEM(("Removing Ranging Instance "
				"peer: " MACDBG "\n",
				MAC2STRDBG(&ranging_inst->peer_addr)));
			bzero(ranging_inst, sizeof(*ranging_inst));
		} else {
			ranging_inst->range_status = NAN_RANGING_REQUIRED;
			/* resolve range role concurrency */
			WL_INFORM_MEM(("Resolving Role Concurrency constraint, peer : "
				MACDBG "\n", MAC2STRDBG(&ranging_inst->peer_addr)));
			ranging_inst->role_concurrency_status = FALSE;
		}
	} else {
		/* For geofence Initiator */
		ranging_inst->range_status = NAN_RANGING_REQUIRED;
	}
}

/*
 * Forcecully Remove Ranging instance
 * Remove if any corresponding Geofence Target
 */
static void
wl_cfgnan_remove_ranging_instance(struct bcm_cfg80211 *cfg,
		nan_ranging_inst_t *ranging_inst)
{
	dhd_pub_t *dhd = (struct dhd_pub *)(cfg->pub);
	int8 index;
	rtt_geofence_target_info_t* geofence_target;

	ASSERT(ranging_inst);
	if (!ranging_inst) {
		return;
	}

	geofence_target = dhd_rtt_get_geofence_target(dhd,
			&ranging_inst->peer_addr, &index);
	if (geofence_target) {
		dhd_rtt_remove_geofence_target(dhd,
			&geofence_target->peer_addr);
	}
	WL_INFORM_MEM(("Removing Ranging Instance " MACDBG "\n",
		MAC2STRDBG(&(ranging_inst->peer_addr))));
	bzero(ranging_inst, sizeof(nan_ranging_inst_t));

	return;
}

static bool
wl_cfgnan_clear_svc_from_ranging_inst(struct bcm_cfg80211 *cfg,
	nan_ranging_inst_t *ranging_inst, nan_svc_info_t *svc)
{
	int i = 0;
	bool cleared = FALSE;

	if (svc && ranging_inst->in_use) {
		for (i = 0; i < MAX_SUBSCRIBES; i++) {
			if (svc == ranging_inst->svc_idx[i]) {
				ranging_inst->num_svc_ctx--;
				ranging_inst->svc_idx[i] = NULL;
				cleared = TRUE;
				/*
				 * This list is maintained dupes free,
				 * hence can break
				 */
				break;
			}
		}
	}
	return cleared;
}

static int
wl_cfgnan_clear_svc_from_all_ranging_inst(struct bcm_cfg80211 *cfg, uint8 svc_id)
{
	nan_ranging_inst_t *ranging_inst;
	int i = 0;
	int ret = BCME_OK;

	nan_svc_info_t *svc = wl_cfgnan_get_svc_inst(cfg, svc_id, 0);
	if (!svc) {
		WL_ERR(("\n svc not found \n"));
		ret = BCME_NOTFOUND;
		goto done;
	}
	for (i = 0; i < NAN_MAX_RANGING_INST; i++) {
		ranging_inst = &(cfg->nancfg->nan_ranging_info[i]);
		wl_cfgnan_clear_svc_from_ranging_inst(cfg, ranging_inst, svc);
	}

done:
	return ret;
}

static int
wl_cfgnan_ranging_clear_publish(struct bcm_cfg80211 *cfg,
	struct ether_addr *peer, uint8 svc_id)
{
	nan_ranging_inst_t *ranging_inst = NULL;
	nan_svc_info_t *svc = NULL;
	bool cleared = FALSE;
	int ret = BCME_OK;

	ranging_inst = wl_cfgnan_check_for_ranging(cfg, peer);
	if (!ranging_inst || !ranging_inst->in_use) {
		goto done;
	}

	WL_INFORM_MEM(("Check clear Ranging for pub update, sub id = %d,"
		" range_id = %d, peer addr = " MACDBG " \n", svc_id,
		ranging_inst->range_id, MAC2STRDBG(peer)));
	svc = wl_cfgnan_get_svc_inst(cfg, svc_id, 0);
	if (!svc) {
		WL_ERR(("\n svc not found, svc_id = %d\n", svc_id));
		ret = BCME_NOTFOUND;
		goto done;
	}

	cleared = wl_cfgnan_clear_svc_from_ranging_inst(cfg, ranging_inst, svc);
	if (!cleared) {
		/* Only if this svc was cleared, any update needed */
		ret = BCME_NOTFOUND;
		goto done;
	}

	wl_cfgnan_terminate_ranging_session(cfg, ranging_inst);
	wl_cfgnan_reset_geofence_ranging(cfg, NULL,
		RTT_SCHED_RNG_TERM_PUB_RNG_CLEAR, TRUE);

done:
	return ret;
}

/* API to terminate/clear all directed nan-rtt sessions.
* Can be called from framework RTT stop context
*/
int
wl_cfgnan_terminate_directed_rtt_sessions(struct net_device *ndev,
	struct bcm_cfg80211 *cfg)
{
	nan_ranging_inst_t *ranging_inst;
	int i, ret = BCME_OK;
	uint32 status;

	for (i = 0; i < NAN_MAX_RANGING_INST; i++) {
		ranging_inst = &cfg->nancfg->nan_ranging_info[i];
		if (ranging_inst->range_id && ranging_inst->range_type == RTT_TYPE_NAN_DIRECTED) {
			if (NAN_RANGING_IS_IN_PROG(ranging_inst->range_status)) {
				ret =  wl_cfgnan_cancel_ranging(ndev, cfg, &ranging_inst->range_id,
					NAN_RNG_TERM_FLAG_IMMEDIATE, &status);
				if (unlikely(ret) || unlikely(status)) {
					WL_ERR(("nan range cancel failed ret = %d status = %d\n",
						ret, status));
				}
			}
			wl_cfgnan_reset_geofence_ranging(cfg, ranging_inst,
				RTT_SHCED_HOST_DIRECTED_TERM, FALSE);
		}
	}
	return ret;
}

/*
 * suspend ongoing geofence ranging session
 * with a peer if on-going ranging is with given peer
 * If peer NULL,
 * Suspend all on-going ranging sessions blindly
 * Do nothing on:
 * If ranging is not in progress
 * If ranging in progress but not with given peer
 */
int
wl_cfgnan_suspend_geofence_rng_session(struct net_device *ndev,
	struct ether_addr *peer, int suspend_reason, u8 cancel_flags)
{
	int ret = BCME_OK;
	uint32 status;
	nan_ranging_inst_t *ranging_inst = NULL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	int suspend_req_dropped_at = 0;
	dhd_pub_t *dhd = (struct dhd_pub *)(cfg->pub);

	UNUSED_PARAMETER(suspend_req_dropped_at);

	ASSERT(peer);
	if (!peer) {
		WL_DBG(("Incoming Peer is NULL, suspend req dropped\n"));
		suspend_req_dropped_at = 1;
		goto exit;
	}

	if (!wl_ranging_geofence_session_with_peer(cfg, peer)) {
		WL_DBG(("Geofence Ranging not in progress with given peer,"
			" suspend req dropped\n"));
		suspend_req_dropped_at = 2;
		goto exit;
	}

	ranging_inst = wl_cfgnan_check_for_ranging(cfg, peer);
	if (ranging_inst) {
		cancel_flags |= NAN_RNG_TERM_FLAG_IMMEDIATE;
		ret =  wl_cfgnan_cancel_ranging(ndev, cfg,
				&ranging_inst->range_id, cancel_flags, &status);
		if (unlikely(ret) || unlikely(status)) {
			WL_ERR(("Geofence Range suspended failed, err = %d, status = %d,"
				"suspend_reason = %d, peer: " MACDBG " \n",
				ret, status, suspend_reason, MAC2STRDBG(peer)));
		}

		ranging_inst->range_status = NAN_RANGING_REQUIRED;
		dhd_rtt_update_geofence_sessions_cnt(dhd, FALSE,
			&ranging_inst->peer_addr);

		if (ranging_inst->range_role == NAN_RANGING_ROLE_RESPONDER &&
			ranging_inst->role_concurrency_status) {
			/* resolve range role concurrency */
			WL_INFORM_MEM(("Resolving Role Concurrency constraint, peer : "
				MACDBG "\n", MAC2STRDBG(&ranging_inst->peer_addr)));
			ranging_inst->role_concurrency_status = FALSE;
		}

		WL_INFORM_MEM(("Geofence Range suspended, "
			" suspend_reason = %d, peer: " MACDBG " \n",
			suspend_reason, MAC2STRDBG(peer)));
	}

exit:
	/* Post pending discovery results */
	if (ranging_inst &&
		((suspend_reason == RTT_GEO_SUSPN_HOST_NDP_TRIGGER) ||
		(suspend_reason == RTT_GEO_SUSPN_PEER_NDP_TRIGGER))) {
		wl_cfgnan_disc_result_on_geofence_cancel(cfg, ranging_inst);
	}

	if (suspend_req_dropped_at) {
		if (ranging_inst) {
			WL_INFORM_MEM(("Ranging Suspend Req with peer: " MACDBG
				", dropped at = %d\n", MAC2STRDBG(&ranging_inst->peer_addr),
				suspend_req_dropped_at));
		} else {
			WL_INFORM_MEM(("Ranging Suspend Req dropped at = %d\n",
				suspend_req_dropped_at));
		}
	}
	return ret;
}

/*
 * suspends all geofence ranging sessions
 * including initiators and responders
 */
void
wl_cfgnan_suspend_all_geofence_rng_sessions(struct net_device *ndev,
		int suspend_reason, u8 cancel_flags)
{

	uint8 i = 0;
	int ret = BCME_OK;
	uint32 status;
	nan_ranging_inst_t *ranging_inst = NULL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	dhd_pub_t *dhd = (struct dhd_pub *)(cfg->pub);

	WL_INFORM_MEM(("Suspending all geofence sessions: "
		"suspend_reason = %d\n", suspend_reason));

	cancel_flags |= NAN_RNG_TERM_FLAG_IMMEDIATE;
	for (i = 0; i < NAN_MAX_RANGING_INST; i++) {
		ranging_inst = &cfg->nancfg->nan_ranging_info[i];
		/* Cancel Ranging if in progress for rang_inst */
		if (ranging_inst->in_use &&
				NAN_RANGING_IS_IN_PROG(ranging_inst->range_status)) {
			ret =  wl_cfgnan_cancel_ranging(bcmcfg_to_prmry_ndev(cfg),
					cfg, &ranging_inst->range_id,
					NAN_RNG_TERM_FLAG_IMMEDIATE, &status);
			if (unlikely(ret) || unlikely(status)) {
				WL_ERR(("wl_cfgnan_suspend_all_geofence_rng_sessions: "
					"nan range cancel failed ret = %d status = %d\n",
					ret, status));
			} else {
				dhd_rtt_update_geofence_sessions_cnt(dhd, FALSE,
					&ranging_inst->peer_addr);
				wl_cfgnan_reset_remove_ranging_instance(cfg, ranging_inst);
			}
		}
	}

	return;

}

/*
 * Terminate given ranging instance
 * if no pending ranging sub service
 */
static void
wl_cfgnan_terminate_ranging_session(struct bcm_cfg80211 *cfg,
	nan_ranging_inst_t *ranging_inst)
{
	int ret = BCME_OK;
	uint32 status;
	dhd_pub_t *dhd = (struct dhd_pub *)(cfg->pub);

	if (ranging_inst->num_svc_ctx != 0) {
		/*
		 * Make sure to remove all svc_insts for range_inst
		 * in order to cancel ranging and remove target in caller
		 */
		return;
	}

	/* Cancel Ranging if in progress for rang_inst */
	if (NAN_RANGING_IS_IN_PROG(ranging_inst->range_status)) {
		ret =  wl_cfgnan_cancel_ranging(bcmcfg_to_prmry_ndev(cfg),
				cfg, &ranging_inst->range_id,
				NAN_RNG_TERM_FLAG_IMMEDIATE, &status);
		if (unlikely(ret) || unlikely(status)) {
			WL_ERR(("%s:nan range cancel failed ret = %d status = %d\n",
				__FUNCTION__, ret, status));
		} else {
			WL_DBG(("Range cancelled \n"));
			dhd_rtt_update_geofence_sessions_cnt(dhd, FALSE,
				&ranging_inst->peer_addr);
		}
	}

	/* Remove ranging instance and clean any corresponding target */
	wl_cfgnan_remove_ranging_instance(cfg, ranging_inst);
}

/*
 * Terminate all ranging sessions
 * with no pending ranging sub service
 */
static void
wl_cfgnan_terminate_all_obsolete_ranging_sessions(
	struct bcm_cfg80211 *cfg)
{
	/* cancel all related ranging instances */
	uint8 i = 0;
	nan_ranging_inst_t *ranging_inst = NULL;

	for (i = 0; i < NAN_MAX_RANGING_INST; i++) {
		ranging_inst = &cfg->nancfg->nan_ranging_info[i];
		if (ranging_inst->in_use) {
			wl_cfgnan_terminate_ranging_session(cfg, ranging_inst);
		}
	}

	return;
}

/*
 * Store svc_ctx for processing during RNG_RPT
 * Return BCME_OK only when svc is added
 */
static int
wl_cfgnan_update_ranging_svc_inst(nan_ranging_inst_t *ranging_inst,
	nan_svc_info_t *svc)
{
	int ret = BCME_OK;
	int i = 0;

	for (i = 0; i < MAX_SUBSCRIBES; i++) {
		if (ranging_inst->svc_idx[i] == svc) {
			WL_DBG(("SVC Ctx for ranging already present, "
			" Duplication not supported: sub_id: %d\n", svc->svc_id));
			ret = BCME_UNSUPPORTED;
			goto done;
		}
	}
	for (i = 0; i < MAX_SUBSCRIBES; i++) {
		if (ranging_inst->svc_idx[i]) {
			continue;
		} else {
			WL_DBG(("Adding SVC Ctx for ranging..svc_id %d\n", svc->svc_id));
			ranging_inst->svc_idx[i] = svc;
			ranging_inst->num_svc_ctx++;
			ret = BCME_OK;
			goto done;
		}
	}
	if (i == MAX_SUBSCRIBES) {
		WL_ERR(("wl_cfgnan_update_ranging_svc_inst: "
			"No resource to hold Ref SVC ctx..svc_id %d\n", svc->svc_id));
		ret = BCME_NORESOURCE;
		goto done;
	}
done:
	return ret;
}

bool
wl_ranging_geofence_session_with_peer(struct bcm_cfg80211 *cfg,
		struct ether_addr *peer_addr)
{
	bool ret = FALSE;
	nan_ranging_inst_t *rng_inst = NULL;

	rng_inst = wl_cfgnan_check_for_ranging(cfg,
		peer_addr);
	if (rng_inst &&
			(NAN_RANGING_IS_IN_PROG(rng_inst->range_status))) {
		ret = TRUE;
	}

	return ret;
}

int
wl_cfgnan_trigger_geofencing_ranging(struct net_device *dev,
		struct ether_addr *peer_addr)
{
	int ret = BCME_OK;
	int err_at = 0;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	dhd_pub_t *dhd = (struct dhd_pub *)(cfg->pub);
	nan_ranging_inst_t *ranging_inst;
	ranging_inst = wl_cfgnan_check_for_ranging(cfg, peer_addr);

	if (!ranging_inst) {
		WL_INFORM_MEM(("Ranging Entry for peer:" MACDBG ", not found\n",
			MAC2STRDBG(peer_addr)));
		ASSERT(0);
		/* Ranging inst should have been added before adding target */
		dhd_rtt_remove_geofence_target(dhd, peer_addr);
		ret = BCME_ERROR;
		err_at = 1;
		goto exit;
	}

	if (!NAN_RANGING_IS_IN_PROG(ranging_inst->range_status)) {
		WL_DBG(("Trigger range request with first svc in svc list of range inst\n"));
		ret = wl_cfgnan_trigger_ranging(bcmcfg_to_prmry_ndev(cfg),
				cfg, ranging_inst, ranging_inst->svc_idx[0],
				NAN_RANGE_REQ_CMD, TRUE);
		if (ret != BCME_OK) {
			/* Unsupported is for already ranging session for peer */
			if (ret == BCME_BUSY) {
				/* TODO: Attempt again over a timer */
				err_at = 2;
			} else {
				/* Remove target and clean ranging inst */
				wl_cfgnan_remove_ranging_instance(cfg, ranging_inst);
				err_at = 3;
				goto exit;
			}
		} else {
			ranging_inst->range_type = RTT_TYPE_NAN_GEOFENCE;
			ranging_inst->range_role = NAN_RANGING_ROLE_INITIATOR;
		}
	} else if (ranging_inst->range_role != NAN_RANGING_ROLE_RESPONDER) {
		/* already in progress but not as responder.. This should not happen */
		ASSERT(!NAN_RANGING_IS_IN_PROG(ranging_inst->range_status));
		ret = BCME_ERROR;
		err_at = 4;
		goto exit;
	} else {
		/* Already in progress as responder, bail out */
		goto exit;
	}

exit:
	if (ret) {
		WL_ERR(("wl_cfgnan_trigger_geofencing_ranging: Failed to "
			"trigger ranging, peer: " MACDBG " ret"
			" = (%d), err_at = %d\n", MAC2STRDBG(peer_addr),
			ret, err_at));
	}
	return ret;
}

static int
wl_cfgnan_check_disc_result_for_ranging(struct bcm_cfg80211 *cfg,
		nan_event_data_t* nan_event_data, bool *send_disc_result)
{
	nan_svc_info_t *svc;
	int ret = BCME_OK;
	rtt_geofence_target_info_t geofence_target;
	dhd_pub_t *dhd = (struct dhd_pub *)(cfg->pub);
	uint8 index, rtt_invalid_reason = RTT_STATE_VALID;
	bool add_target;

	*send_disc_result = TRUE;
	svc = wl_cfgnan_get_svc_inst(cfg, nan_event_data->sub_id, 0);

	if (svc && svc->ranging_required) {
		nan_ranging_inst_t *ranging_inst;
		ranging_inst = wl_cfgnan_get_ranging_inst(cfg,
				&nan_event_data->remote_nmi,
				NAN_RANGING_ROLE_INITIATOR);
		if (!ranging_inst) {
			ret = BCME_NORESOURCE;
			goto exit;
		}
		ASSERT(ranging_inst->range_role != NAN_RANGING_ROLE_INVALID);

		/* For responder role, range state should be in progress only */
		ASSERT((ranging_inst->range_role == NAN_RANGING_ROLE_INITIATOR) ||
			NAN_RANGING_IS_IN_PROG(ranging_inst->range_status));

		/*
		 * On rec disc result with ranging required, add target, if
		 * ranging role is responder (range state has to be in prog always)
		 * Or ranging role is initiator and ranging is not already in prog
		 */
		add_target = ((ranging_inst->range_role ==  NAN_RANGING_ROLE_RESPONDER) ||
			((ranging_inst->range_role ==  NAN_RANGING_ROLE_INITIATOR) &&
			(!NAN_RANGING_IS_IN_PROG(ranging_inst->range_status))));
		if (add_target) {
			WL_DBG(("Add Range request to geofence target list\n"));
			memcpy(&geofence_target.peer_addr, &nan_event_data->remote_nmi,
					ETHER_ADDR_LEN);
			/* check if target is already added */
			if (!dhd_rtt_get_geofence_target(dhd, &nan_event_data->remote_nmi, &index))
			{
				ret = dhd_rtt_add_geofence_target(dhd, &geofence_target);
				if (unlikely(ret)) {
					WL_ERR(("Failed to add geofence Tgt, ret = (%d)\n", ret));
					bzero(ranging_inst, sizeof(*ranging_inst));
					goto exit;
				} else {
					WL_INFORM_MEM(("Geofence Tgt Added:" MACDBG " sub_id:%d\n",
						MAC2STRDBG(&geofence_target.peer_addr),
						svc->svc_id));
				}
			}
			if (wl_cfgnan_update_ranging_svc_inst(ranging_inst, svc)
					!= BCME_OK) {
					goto exit;
			}
			if (ranging_inst->range_role == NAN_RANGING_ROLE_RESPONDER) {
				/* Adding RTT target while responder, leads to role concurrency */
				WL_INFORM_MEM(("Entering Role Concurrency constraint, peer : "
					MACDBG "\n", MAC2STRDBG(&ranging_inst->peer_addr)));
				ranging_inst->role_concurrency_status = TRUE;
			} else {
				/* Trigger/Reset geofence RTT */
				wl_cfgnan_reset_geofence_ranging(cfg, ranging_inst,
					RTT_SCHED_SUB_MATCH, TRUE);
			}
		} else {
			/* Target already added, check & add svc_inst ref to rang_inst */
			wl_cfgnan_update_ranging_svc_inst(ranging_inst, svc);
		}
		/* Disc event will be given on receving range_rpt event */
		WL_TRACE(("Disc event will given when Range RPT event is recvd"));
	} else {
		ret = BCME_UNSUPPORTED;
	}

exit:
	if (ret == BCME_OK) {
		/* Check if we have to send disc result immediately or not */
		rtt_invalid_reason = dhd_rtt_invalid_states
			(bcmcfg_to_prmry_ndev(cfg),  &nan_event_data->remote_nmi);
		/*
		 * If instant RTT not possible (RTT postpone),
		 * send discovery result instantly like
		 * incase of invalid rtt state as
		 * ndp connected/connecting,
		 * or role_concurrency active with peer.
		 * Otherwise, result should be posted
		 * on ranging report event after RTT done
		 */
		if ((rtt_invalid_reason == RTT_STATE_VALID) &&
			(!wl_cfgnan_check_role_concurrency(cfg,
			&nan_event_data->remote_nmi))) {
			/* Avoid sending disc result instantly */
			*send_disc_result = FALSE;
		}
	}

	return ret;
}

bool
wl_cfgnan_ranging_allowed(struct bcm_cfg80211 *cfg)
{
	int i = 0;
	uint8 rng_progress_count = 0;
	nan_ranging_inst_t *ranging_inst = NULL;

	for (i =  0; i < NAN_MAX_RANGING_INST; i++) {
		ranging_inst = &cfg->nancfg->nan_ranging_info[i];
		if (NAN_RANGING_IS_IN_PROG(ranging_inst->range_status)) {
			rng_progress_count++;
		}
	}

	if (rng_progress_count >= NAN_MAX_RANGING_SSN_ALLOWED) {
		return FALSE;
	}
	return TRUE;
}

uint8
wl_cfgnan_cancel_rng_responders(struct net_device *ndev)
{
	int i = 0;
	uint8 num_resp_cancelled = 0;
	int status, ret;
	nan_ranging_inst_t *ranging_inst = NULL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);

	for (i =  0; i < NAN_MAX_RANGING_INST; i++) {
		ranging_inst = &cfg->nancfg->nan_ranging_info[i];
		if (NAN_RANGING_IS_IN_PROG(ranging_inst->range_status) &&
			(ranging_inst->range_role == NAN_RANGING_ROLE_RESPONDER)) {
			num_resp_cancelled++;
			ret = wl_cfgnan_cancel_ranging(bcmcfg_to_prmry_ndev(cfg), cfg,
				&ranging_inst->range_id, NAN_RNG_TERM_FLAG_IMMEDIATE, &status);
			if (unlikely(ret) || unlikely(status)) {
				WL_ERR(("wl_cfgnan_cancel_rng_responders: Failed to cancel"
					" existing ranging, ret = (%d)\n", ret));
			}
			WL_INFORM_MEM(("Removing Ranging Instance " MACDBG "\n",
				MAC2STRDBG(&(ranging_inst->peer_addr))));
			bzero(ranging_inst, sizeof(*ranging_inst));
		}
	}
	return num_resp_cancelled;
}

/* ranging reqeust event handler */
static int
wl_cfgnan_handle_ranging_ind(struct bcm_cfg80211 *cfg,
		wl_nan_ev_rng_req_ind_t *rng_ind)
{
	int ret = BCME_OK;
	nan_ranging_inst_t *ranging_inst = NULL;
	uint8 cancel_flags = 0;
	bool accept = TRUE;
	nan_ranging_inst_t tmp_rng_inst;
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	struct ether_addr * peer_addr = &(rng_ind->peer_m_addr);
	uint8 rtt_invalid_state;
	dhd_pub_t *dhd = (struct dhd_pub *)(cfg->pub);
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhd);
	int err_at = 0;

	WL_DBG(("Trigger range response\n"));

	/* Check if ranging is allowed */
	rtt_invalid_state = dhd_rtt_invalid_states(ndev, peer_addr);
	if (rtt_invalid_state != RTT_STATE_VALID) {
		WL_INFORM_MEM(("Cannot allow ranging due to reason %d \n", rtt_invalid_state));
		ret = BCME_NORESOURCE;
		err_at = 1;
		goto done;
	}

	mutex_lock(&rtt_status->rtt_mutex);

	if (rtt_status && !RTT_IS_STOPPED(rtt_status)) {
		WL_INFORM_MEM(("Direcetd RTT in progress..reject RNG_REQ\n"));
		ret = BCME_NORESOURCE;
		err_at = 2;
		goto done;
	}

	/* Check if ranging set up in progress */
	if (dhd_rtt_is_geofence_setup_inprog(dhd)) {
		WL_INFORM_MEM(("Ranging set up already in progress, "
			"RNG IND event dropped\n"));
		err_at = 3;
		ret = BCME_NOTREADY;
		goto done;
	}

	/* check if we are already having any ranging session with peer.
	* If so below are the policies
	* If we are already a Geofence Initiator or responder w.r.t the peer
	* then silently teardown the current session and accept the REQ.
	* If we are in direct rtt initiator role then reject.
	*/
	ranging_inst = wl_cfgnan_check_for_ranging(cfg, peer_addr);
	if (ranging_inst) {
		if (NAN_RANGING_IS_IN_PROG(ranging_inst->range_status)) {
			if (ranging_inst->range_type == RTT_TYPE_NAN_GEOFENCE ||
					ranging_inst->range_role == NAN_RANGING_ROLE_RESPONDER) {
				WL_INFORM_MEM(("Already responder/geofence for the Peer, cancel "
					"current ssn and accept new one,"
					" range_type = %d, role = %d\n",
					ranging_inst->range_type, ranging_inst->range_role));
				cancel_flags = NAN_RNG_TERM_FLAG_IMMEDIATE |
					NAN_RNG_TERM_FLAG_SILENT_TEARDOWN;
					wl_cfgnan_suspend_geofence_rng_session(ndev,
						&(rng_ind->peer_m_addr),
						RTT_GEO_SUSPN_PEER_RTT_TRIGGER, cancel_flags);
			} else {
				WL_ERR(("Reject the RNG_REQ_IND in direct rtt initiator role\n"));
				err_at = 4;
				ret = BCME_BUSY;
				goto done;
			}
		} else {
			/* Check if new Ranging session is allowed */
			if (dhd_rtt_geofence_sessions_maxed_out(dhd)) {
				WL_ERR(("Cannot allow more ranging sessions\n"));
				err_at = 5;
				ret = BCME_NORESOURCE;
				goto done;
			}
		}
		/* reset ranging instance for responder role */
		ranging_inst->range_status = NAN_RANGING_REQUIRED;
		ranging_inst->range_role = NAN_RANGING_ROLE_RESPONDER;
		ranging_inst->range_type = 0;
	} else {
		/* Check if new Ranging session is allowed */
		if (dhd_rtt_geofence_sessions_maxed_out(dhd)) {
			WL_ERR(("Cannot allow more ranging sessions\n"));
			err_at = 6;
			ret = BCME_NORESOURCE;
			goto done;
		}

		ranging_inst = wl_cfgnan_get_ranging_inst(cfg, &rng_ind->peer_m_addr,
				NAN_RANGING_ROLE_RESPONDER);
		ASSERT(ranging_inst);
		if (!ranging_inst) {
			WL_ERR(("Failed to create ranging instance \n"));
			err_at = 7;
			ret = BCME_NORESOURCE;
			goto done;
		}
	}

done:
	if (ret != BCME_OK) {
		/* reject the REQ using temp ranging instance */
		bzero(&tmp_rng_inst, sizeof(tmp_rng_inst));
		ranging_inst = &tmp_rng_inst;
		(void)memcpy_s(&tmp_rng_inst.peer_addr, ETHER_ADDR_LEN,
			&rng_ind->peer_m_addr, ETHER_ADDR_LEN);
		accept = FALSE;
	}

	ranging_inst->range_id = rng_ind->rng_id;

	WL_INFORM_MEM(("Trigger Ranging at Responder, ret = %d, err_at = %d, "
		"accept = %d, rng_id = %d\n", ret, err_at,
		accept, rng_ind->rng_id));
	ret = wl_cfgnan_trigger_ranging(ndev, cfg, ranging_inst,
		NULL, NAN_RANGE_REQ_EVNT, accept);
	if (unlikely(ret) || !accept) {
		WL_ERR(("Failed to trigger ranging while handling range request, "
			" ret = %d, rng_id = %d, accept %d\n", ret,
			rng_ind->rng_id, accept));
		wl_cfgnan_reset_remove_ranging_instance(cfg, ranging_inst);
	} else {
		dhd_rtt_set_geofence_setup_status(dhd, TRUE,
			&ranging_inst->peer_addr);
	}
	mutex_unlock(&rtt_status->rtt_mutex);
	return ret;
}

/* ranging quest and response iovar handler */
int
wl_cfgnan_trigger_ranging(struct net_device *ndev, struct bcm_cfg80211 *cfg,
		void *ranging_ctxt, nan_svc_info_t *svc,
		uint8 range_cmd, bool accept_req)
{
	s32 ret = BCME_OK;
	bcm_iov_batch_buf_t *nan_buf = NULL;
	wl_nan_range_req_t *range_req = NULL;
	wl_nan_range_resp_t *range_resp = NULL;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	uint32 status;
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE_MED];
	nan_ranging_inst_t *ranging_inst = (nan_ranging_inst_t *)ranging_ctxt;
	nan_avail_cmd_data cmd_data;

	NAN_DBG_ENTER();

	bzero(&cmd_data, sizeof(cmd_data));
	ret = memcpy_s(&cmd_data.peer_nmi, ETHER_ADDR_LEN,
			&ranging_inst->peer_addr, ETHER_ADDR_LEN);
	if (ret != BCME_OK) {
		WL_ERR(("Failed to copy ranging peer addr\n"));
		goto fail;
	}

	cmd_data.avail_period = NAN_RANGING_PERIOD;
	ret = wl_cfgnan_set_nan_avail(bcmcfg_to_prmry_ndev(cfg),
			cfg, &cmd_data, WL_AVAIL_LOCAL);
	if (ret != BCME_OK) {
		WL_ERR(("Failed to set avail value with type [WL_AVAIL_LOCAL]\n"));
		goto fail;
	}

	ret = wl_cfgnan_set_nan_avail(bcmcfg_to_prmry_ndev(cfg),
			cfg, &cmd_data, WL_AVAIL_RANGING);
	if (unlikely(ret)) {
		WL_ERR(("Failed to set avail value with type [WL_AVAIL_RANGING]\n"));
		goto fail;
	}

	nan_buf = MALLOCZ(cfg->osh, nan_buf_size);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_buf_size -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);

	sub_cmd = (bcm_iov_batch_subcmd_t*)(&nan_buf->cmds[0]);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);
	if (range_cmd == NAN_RANGE_REQ_CMD) {
		sub_cmd->id = htod16(WL_NAN_CMD_RANGE_REQUEST);
		sub_cmd->len = sizeof(sub_cmd->u.options) + sizeof(wl_nan_range_req_t);
		range_req = (wl_nan_range_req_t *)(sub_cmd->data);
		/* ranging config */
		range_req->peer = ranging_inst->peer_addr;
		if (svc) {
			range_req->interval = svc->ranging_interval;
			/* Limits are in cm from host */
			range_req->ingress = svc->ingress_limit;
			range_req->egress = svc->egress_limit;
		}
		range_req->indication = NAN_RANGING_INDICATE_CONTINUOUS_MASK;
	} else {
		/* range response config */
		sub_cmd->id = htod16(WL_NAN_CMD_RANGE_RESPONSE);
		sub_cmd->len = sizeof(sub_cmd->u.options) + sizeof(wl_nan_range_resp_t);
		range_resp = (wl_nan_range_resp_t *)(sub_cmd->data);
		range_resp->range_id = ranging_inst->range_id;
		range_resp->indication = NAN_RANGING_INDICATE_CONTINUOUS_MASK;
		if (accept_req) {
			range_resp->status = NAN_RNG_REQ_ACCEPTED_BY_HOST;
		} else {
			range_resp->status = NAN_RNG_REQ_REJECTED_BY_HOST;
		}
		nan_buf->is_set = true;
	}

	nan_buf_size -= (sub_cmd->len +
			OFFSETOF(bcm_iov_batch_subcmd_t, u.options));
	nan_buf->count++;

	bzero(resp_buf, sizeof(resp_buf));
	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, nan_buf_size,
			&status,
			(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
	if (unlikely(ret) || unlikely(status)) {
		WL_ERR(("nan ranging failed ret = %d status = %d\n",
				ret, status));
		ret = (ret == BCME_OK) ? status : ret;
		goto fail;
	}
	WL_TRACE(("nan ranging trigger successful\n"));
	if (range_cmd == NAN_RANGE_REQ_CMD) {
		WL_INFORM_MEM(("Ranging Req Triggered"
			" peer: " MACDBG ", ind : %d, ingress : %d, egress : %d\n",
			MAC2STRDBG(&ranging_inst->peer_addr), range_req->indication,
			range_req->ingress, range_req->egress));
	} else {
		WL_INFORM_MEM(("Ranging Resp Triggered"
			" peer: " MACDBG ", ind : %d, ingress : %d, egress : %d\n",
			MAC2STRDBG(&ranging_inst->peer_addr), range_resp->indication,
			range_resp->ingress, range_resp->egress));
	}

	/* check the response buff for request */
	if (range_cmd == NAN_RANGE_REQ_CMD) {
		ret = process_resp_buf(resp_buf + WL_NAN_OBUF_DATA_OFFSET,
				&ranging_inst->range_id, WL_NAN_CMD_RANGE_REQUEST);
		WL_INFORM_MEM(("ranging instance returned %d\n", ranging_inst->range_id));
	}

	/* Move Ranging instance to set up in progress state */
	ranging_inst->range_status = NAN_RANGING_SETUP_IN_PROGRESS;

fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, NAN_IOCTL_BUF_SIZE);
	}

	NAN_DBG_EXIT();
	return ret;
}

bool
wl_cfgnan_ranging_is_in_prog_for_peer(struct bcm_cfg80211 *cfg, struct ether_addr *peer_addr)
{
	nan_ranging_inst_t *rng_inst = NULL;

	rng_inst = wl_cfgnan_check_for_ranging(cfg, peer_addr);

	return (rng_inst && NAN_RANGING_IS_IN_PROG(rng_inst->range_status));
}

#endif /* RTT_SUPPORT */
#endif /* WL_NAN_DISC_CACHE */

static void *wl_nan_bloom_alloc(void *ctx, uint size)
{
	uint8 *buf;
	BCM_REFERENCE(ctx);

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		buf = NULL;
	}
	return buf;
}

static void wl_nan_bloom_free(void *ctx, void *buf, uint size)
{
	BCM_REFERENCE(ctx);
	BCM_REFERENCE(size);
	if (buf) {
		kfree(buf);
	}
}

static uint wl_nan_hash(void *ctx, uint index, const uint8 *input, uint input_len)
{
	uint8* filter_idx = (uint8*)ctx;
	uint8 i = (*filter_idx * WL_NAN_HASHES_PER_BLOOM) + (uint8)index;
	uint b = 0;

	/* Steps 1 and 2 as explained in Section 6.2 */
	/* Concatenate index to input and run CRC32 by calling hndcrc32 twice */
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	b = hndcrc32(&i, sizeof(uint8), CRC32_INIT_VALUE);
	b = hndcrc32((uint8*)input, input_len, b);
	GCC_DIAGNOSTIC_POP();
	/* Obtain the last 2 bytes of the CRC32 output */
	b &= NAN_BLOOM_CRC32_MASK;

	/* Step 3 is completed by bcmbloom functions */
	return b;
}

static int wl_nan_bloom_create(bcm_bloom_filter_t **bp, uint *idx, uint size)
{
	uint i;
	int err;

	err = bcm_bloom_create(wl_nan_bloom_alloc, wl_nan_bloom_free,
			idx, WL_NAN_HASHES_PER_BLOOM, size, bp);
	if (err != BCME_OK) {
		goto exit;
	}

	/* Populate bloom filter with hash functions */
	for (i = 0; i < WL_NAN_HASHES_PER_BLOOM; i++) {
		err = bcm_bloom_add_hash(*bp, wl_nan_hash, &i);
		if (err) {
			WL_ERR(("bcm_bloom_add_hash failed\n"));
			goto exit;
		}
	}
exit:
	return err;
}

static int
wl_cfgnan_sd_params_handler(struct net_device *ndev,
	nan_discover_cmd_data_t *cmd_data, uint16 cmd_id,
	void *p_buf, uint16 *nan_buf_size)
{
	s32 ret = BCME_OK;
	uint8 *pxtlv, *srf = NULL, *srf_mac = NULL, *srftmp = NULL;
	uint16 buflen_avail;
	bcm_iov_batch_subcmd_t *sub_cmd = (bcm_iov_batch_subcmd_t*)(p_buf);
	wl_nan_sd_params_t *sd_params = (wl_nan_sd_params_t *)sub_cmd->data;
	uint16 srf_size = 0;
	uint bloom_size, a;
	bcm_bloom_filter_t *bp = NULL;
	/* Bloom filter index default, indicates it has not been set */
	uint bloom_idx = 0xFFFFFFFF;
	uint16 bloom_len = NAN_BLOOM_LENGTH_DEFAULT;
	/* srf_ctrl_size = bloom_len + src_control field */
	uint16 srf_ctrl_size = bloom_len + 1;

	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(ndev);
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	BCM_REFERENCE(cfg);

	NAN_DBG_ENTER();

	if (cmd_data->period) {
		sd_params->awake_dw = cmd_data->period;
	}
	sd_params->period = 1;

	if (cmd_data->ttl) {
		sd_params->ttl = cmd_data->ttl;
	} else {
		sd_params->ttl = WL_NAN_TTL_UNTIL_CANCEL;
	}

	sd_params->flags = 0;
	sd_params->flags = cmd_data->flags;

	/* Nan Service Based event suppression Flags */
	if (cmd_data->recv_ind_flag) {
		/* BIT0 - If set, host wont rec event "terminated" */
		if (CHECK_BIT(cmd_data->recv_ind_flag, WL_NAN_EVENT_SUPPRESS_TERMINATE_BIT)) {
			sd_params->flags |= WL_NAN_SVC_CTRL_SUPPRESS_EVT_TERMINATED;
		}

		/* BIT1 - If set, host wont receive match expiry evt */
		/* TODO: Exp not yet supported */
		if (CHECK_BIT(cmd_data->recv_ind_flag, WL_NAN_EVENT_SUPPRESS_MATCH_EXP_BIT)) {
			WL_DBG(("Need to add match expiry event\n"));
		}
		/* BIT2 - If set, host wont rec event "receive"  */
		if (CHECK_BIT(cmd_data->recv_ind_flag, WL_NAN_EVENT_SUPPRESS_RECEIVE_BIT)) {
			sd_params->flags |= WL_NAN_SVC_CTRL_SUPPRESS_EVT_RECEIVE;
		}
		/* BIT3 - If set, host wont rec event "replied" */
		if (CHECK_BIT(cmd_data->recv_ind_flag, WL_NAN_EVENT_SUPPRESS_REPLIED_BIT)) {
			sd_params->flags |= WL_NAN_SVC_CTRL_SUPPRESS_EVT_REPLIED;
		}
	}
	if (cmd_id == WL_NAN_CMD_SD_PUBLISH) {
		sd_params->instance_id = cmd_data->pub_id;
		if (cmd_data->service_responder_policy) {
			/* Do not disturb avail if dam is supported */
			if (FW_SUPPORTED(dhdp, autodam)) {
				/* Nan Accept policy: Per service basis policy
				 * Based on this policy(ALL/NONE), responder side
				 * will send ACCEPT/REJECT
				 * If set, auto datapath responder will be sent by FW
				 */
				sd_params->flags |= WL_NAN_SVC_CTRL_AUTO_DPRESP;
			} else  {
				WL_ERR(("svc specifiv auto dp resp is not"
						" supported in non-auto dam fw\n"));
			}
		}
	} else if (cmd_id == WL_NAN_CMD_SD_SUBSCRIBE) {
		sd_params->instance_id = cmd_data->sub_id;
	} else {
		ret = BCME_USAGE_ERROR;
		WL_ERR(("wrong command id = %d \n", cmd_id));
		goto fail;
	}

	if ((cmd_data->svc_hash.dlen == WL_NAN_SVC_HASH_LEN) &&
			(cmd_data->svc_hash.data)) {
		ret = memcpy_s((uint8*)sd_params->svc_hash,
				sizeof(sd_params->svc_hash),
				cmd_data->svc_hash.data,
				cmd_data->svc_hash.dlen);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy svc hash\n"));
			goto fail;
		}
#ifdef WL_NAN_DEBUG
		prhex("hashed svc name", cmd_data->svc_hash.data,
				cmd_data->svc_hash.dlen);
#endif /* WL_NAN_DEBUG */
	} else {
		ret = BCME_ERROR;
		WL_ERR(("invalid svc hash data or length = %d\n",
				cmd_data->svc_hash.dlen));
		goto fail;
	}

	/* check if ranging support is present in firmware */
	if ((cmd_data->sde_control_flag & NAN_SDE_CF_RANGING_REQUIRED) &&
		!FW_SUPPORTED(dhdp, nanrange)) {
		WL_ERR(("Service requires ranging but fw doesnt support it\n"));
		ret = BCME_UNSUPPORTED;
		goto fail;
	}

	/* Optional parameters: fill the sub_command block with service descriptor attr */
	sub_cmd->id = htod16(cmd_id);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);
	sub_cmd->len = sizeof(sub_cmd->u.options) +
		OFFSETOF(wl_nan_sd_params_t, optional[0]);
	pxtlv = (uint8*)&sd_params->optional[0];

	*nan_buf_size -= sub_cmd->len;
	buflen_avail = *nan_buf_size;

	if (cmd_data->svc_info.data && cmd_data->svc_info.dlen) {
		WL_TRACE(("optional svc_info present, pack it\n"));
		ret = bcm_pack_xtlv_entry(&pxtlv, nan_buf_size,
				WL_NAN_XTLV_SD_SVC_INFO,
				cmd_data->svc_info.dlen,
				cmd_data->svc_info.data, BCM_XTLV_OPTION_ALIGN32);
		if (unlikely(ret)) {
			WL_ERR(("%s: fail to pack WL_NAN_XTLV_SD_SVC_INFO\n", __FUNCTION__));
			goto fail;
		}
	}

	if (cmd_data->sde_svc_info.data && cmd_data->sde_svc_info.dlen) {
		WL_TRACE(("optional sdea svc_info present, pack it, %d\n",
			cmd_data->sde_svc_info.dlen));
		ret = bcm_pack_xtlv_entry(&pxtlv, nan_buf_size,
				WL_NAN_XTLV_SD_SDE_SVC_INFO,
				cmd_data->sde_svc_info.dlen,
				cmd_data->sde_svc_info.data, BCM_XTLV_OPTION_ALIGN32);
		if (unlikely(ret)) {
			WL_ERR(("%s: fail to pack sdea svc info\n", __FUNCTION__));
			goto fail;
		}
	}

	if (cmd_data->tx_match.dlen) {
		WL_TRACE(("optional tx match filter presnet (len=%d)\n",
				cmd_data->tx_match.dlen));
		ret = bcm_pack_xtlv_entry(&pxtlv, nan_buf_size,
				WL_NAN_XTLV_CFG_MATCH_TX, cmd_data->tx_match.dlen,
				cmd_data->tx_match.data, BCM_XTLV_OPTION_ALIGN32);
		if (unlikely(ret)) {
			WL_ERR(("%s: failed on xtlv_pack for tx match filter\n", __FUNCTION__));
			goto fail;
		}
	}

	if (cmd_data->life_count) {
		WL_TRACE(("optional life count is present, pack it\n"));
		ret = bcm_pack_xtlv_entry(&pxtlv, nan_buf_size, WL_NAN_XTLV_CFG_SVC_LIFE_COUNT,
				sizeof(cmd_data->life_count), &cmd_data->life_count,
				BCM_XTLV_OPTION_ALIGN32);
		if (unlikely(ret)) {
			WL_ERR(("%s: failed to WL_NAN_XTLV_CFG_SVC_LIFE_COUNT\n", __FUNCTION__));
			goto fail;
		}
	}

	if (cmd_data->use_srf) {
		uint8 srf_control = 0;
		/* set include bit */
		if (cmd_data->srf_include == true) {
			srf_control |= 0x2;
		}

		if (!ETHER_ISNULLADDR(&cmd_data->mac_list.list) &&
				(cmd_data->mac_list.num_mac_addr
				 < NAN_SRF_MAX_MAC)) {
			if (cmd_data->srf_type == SRF_TYPE_SEQ_MAC_ADDR) {
				/* mac list */
				srf_size = (cmd_data->mac_list.num_mac_addr
						* ETHER_ADDR_LEN) + NAN_SRF_CTRL_FIELD_LEN;
				WL_TRACE(("srf size = %d\n", srf_size));

				srf_mac = MALLOCZ(cfg->osh, srf_size);
				if (srf_mac == NULL) {
					WL_ERR(("%s: memory allocation failed\n", __FUNCTION__));
					ret = -ENOMEM;
					goto fail;
				}
				ret = memcpy_s(srf_mac, NAN_SRF_CTRL_FIELD_LEN,
						&srf_control, NAN_SRF_CTRL_FIELD_LEN);
				if (ret != BCME_OK) {
					WL_ERR(("Failed to copy srf control\n"));
					goto fail;
				}
				ret = memcpy_s(srf_mac+1, (srf_size - NAN_SRF_CTRL_FIELD_LEN),
						cmd_data->mac_list.list,
						(srf_size - NAN_SRF_CTRL_FIELD_LEN));
				if (ret != BCME_OK) {
					WL_ERR(("Failed to copy srf control mac list\n"));
					goto fail;
				}
				ret = bcm_pack_xtlv_entry(&pxtlv, nan_buf_size,
						WL_NAN_XTLV_CFG_SR_FILTER, srf_size, srf_mac,
						BCM_XTLV_OPTION_ALIGN32);
				if (unlikely(ret)) {
					WL_ERR(("%s: failed to WL_NAN_XTLV_CFG_SR_FILTER\n",
							__FUNCTION__));
					goto fail;
				}
			} else if (cmd_data->srf_type == SRF_TYPE_BLOOM_FILTER) {
				/* Create bloom filter */
				srf = MALLOCZ(cfg->osh, srf_ctrl_size);
				if (srf == NULL) {
					WL_ERR(("%s: memory allocation failed\n", __FUNCTION__));
					ret = -ENOMEM;
					goto fail;
				}
				/* Bloom filter */
				srf_control |= 0x1;
				/* Instance id must be from 1 to 255, 0 is Reserved */
				if (sd_params->instance_id == NAN_ID_RESERVED) {
					WL_ERR(("Invalid instance id: %d\n",
							sd_params->instance_id));
					ret = BCME_BADARG;
					goto fail;
				}
				if (bloom_idx == 0xFFFFFFFF) {
					bloom_idx = sd_params->instance_id % 4;
				} else {
					WL_ERR(("Invalid bloom_idx\n"));
					ret = BCME_BADARG;
					goto fail;

				}
				srf_control |= bloom_idx << 2;

				ret = wl_nan_bloom_create(&bp, &bloom_idx, bloom_len);
				if (unlikely(ret)) {
					WL_ERR(("%s: Bloom create failed\n", __FUNCTION__));
					goto fail;
				}

				srftmp = cmd_data->mac_list.list;
				for (a = 0;
					a < cmd_data->mac_list.num_mac_addr; a++) {
					ret = bcm_bloom_add_member(bp, srftmp, ETHER_ADDR_LEN);
					if (unlikely(ret)) {
						WL_ERR(("%s: Cannot add to bloom filter\n",
								__FUNCTION__));
						goto fail;
					}
					srftmp += ETHER_ADDR_LEN;
				}

				ret = memcpy_s(srf, NAN_SRF_CTRL_FIELD_LEN,
						&srf_control, NAN_SRF_CTRL_FIELD_LEN);
				if (ret != BCME_OK) {
					WL_ERR(("Failed to copy srf control\n"));
					goto fail;
				}
				ret = bcm_bloom_get_filter_data(bp, bloom_len,
						(srf + NAN_SRF_CTRL_FIELD_LEN),
						&bloom_size);
				if (unlikely(ret)) {
					WL_ERR(("%s: Cannot get filter data\n", __FUNCTION__));
					goto fail;
				}
				ret = bcm_pack_xtlv_entry(&pxtlv, nan_buf_size,
						WL_NAN_XTLV_CFG_SR_FILTER, srf_ctrl_size,
						srf, BCM_XTLV_OPTION_ALIGN32);
				if (ret != BCME_OK) {
					WL_ERR(("Failed to pack SR FILTER data, ret = %d\n", ret));
					goto fail;
				}
			} else {
				WL_ERR(("Invalid SRF Type = %d !!!\n",
						cmd_data->srf_type));
				goto fail;
			}
		} else {
			WL_ERR(("Invalid MAC Addr/Too many mac addr = %d !!!\n",
					cmd_data->mac_list.num_mac_addr));
			goto fail;
		}
	}

	if (cmd_data->rx_match.dlen) {
		WL_TRACE(("optional rx match filter is present, pack it\n"));
		ret = bcm_pack_xtlv_entry(&pxtlv, nan_buf_size,
				WL_NAN_XTLV_CFG_MATCH_RX, cmd_data->rx_match.dlen,
				cmd_data->rx_match.data, BCM_XTLV_OPTION_ALIGN32);
		if (unlikely(ret)) {
			WL_ERR(("%s: failed on xtlv_pack for rx match filter\n", __func__));
			goto fail;
		}
	}

	/* Security elements */
	if (cmd_data->csid) {
		WL_TRACE(("Cipher suite type is present, pack it\n"));
		ret = bcm_pack_xtlv_entry(&pxtlv, nan_buf_size,
				WL_NAN_XTLV_CFG_SEC_CSID, sizeof(nan_sec_csid_e),
				(uint8*)&cmd_data->csid, BCM_XTLV_OPTION_ALIGN32);
		if (unlikely(ret)) {
			WL_ERR(("%s: fail to pack on csid\n", __FUNCTION__));
			goto fail;
		}
	}

	if (cmd_data->ndp_cfg.security_cfg) {
		if ((cmd_data->key_type == NAN_SECURITY_KEY_INPUT_PMK) ||
			(cmd_data->key_type == NAN_SECURITY_KEY_INPUT_PASSPHRASE)) {
			if (cmd_data->key.data && cmd_data->key.dlen) {
				WL_TRACE(("optional pmk present, pack it\n"));
				ret = bcm_pack_xtlv_entry(&pxtlv, nan_buf_size,
					WL_NAN_XTLV_CFG_SEC_PMK, cmd_data->key.dlen,
					cmd_data->key.data, BCM_XTLV_OPTION_ALIGN32);
				if (unlikely(ret)) {
					WL_ERR(("%s: fail to pack WL_NAN_XTLV_CFG_SEC_PMK\n",
						__FUNCTION__));
					goto fail;
				}
			}
		} else {
			WL_ERR(("Invalid security key type\n"));
			ret = BCME_BADARG;
			goto fail;
		}
	}

	if (cmd_data->scid.data && cmd_data->scid.dlen) {
		WL_TRACE(("optional scid present, pack it\n"));
		ret = bcm_pack_xtlv_entry(&pxtlv, nan_buf_size, WL_NAN_XTLV_CFG_SEC_SCID,
			cmd_data->scid.dlen, cmd_data->scid.data, BCM_XTLV_OPTION_ALIGN32);
		if (unlikely(ret)) {
			WL_ERR(("%s: fail to pack WL_NAN_XTLV_CFG_SEC_SCID\n", __FUNCTION__));
			goto fail;
		}
	}

	if (cmd_data->sde_control_config) {
		ret = bcm_pack_xtlv_entry(&pxtlv, nan_buf_size,
				WL_NAN_XTLV_SD_SDE_CONTROL,
				sizeof(uint16), (uint8*)&cmd_data->sde_control_flag,
				BCM_XTLV_OPTION_ALIGN32);
		if (ret != BCME_OK) {
			WL_ERR(("%s: fail to pack WL_NAN_XTLV_SD_SDE_CONTROL\n", __FUNCTION__));
			goto fail;
		}
	}

	sub_cmd->len += (buflen_avail - *nan_buf_size);

fail:
	if (srf) {
		MFREE(cfg->osh, srf, srf_ctrl_size);
	}

	if (srf_mac) {
		MFREE(cfg->osh, srf_mac, srf_size);
	}
	NAN_DBG_EXIT();
	return ret;
}

static int
wl_cfgnan_aligned_data_size_of_opt_disc_params(uint16 *data_size, nan_discover_cmd_data_t *cmd_data)
{
	s32 ret = BCME_OK;
	if (cmd_data->svc_info.dlen)
		*data_size += ALIGN_SIZE(cmd_data->svc_info.dlen + NAN_XTLV_ID_LEN_SIZE, 4);
	if (cmd_data->sde_svc_info.dlen)
		*data_size += ALIGN_SIZE(cmd_data->sde_svc_info.dlen + NAN_XTLV_ID_LEN_SIZE, 4);
	if (cmd_data->tx_match.dlen)
		*data_size += ALIGN_SIZE(cmd_data->tx_match.dlen + NAN_XTLV_ID_LEN_SIZE, 4);
	if (cmd_data->rx_match.dlen)
		*data_size += ALIGN_SIZE(cmd_data->rx_match.dlen + NAN_XTLV_ID_LEN_SIZE, 4);
	if (cmd_data->use_srf) {
		if (cmd_data->srf_type == SRF_TYPE_SEQ_MAC_ADDR) {
			*data_size += (cmd_data->mac_list.num_mac_addr * ETHER_ADDR_LEN)
					+ NAN_SRF_CTRL_FIELD_LEN;
		} else { /* Bloom filter type */
			*data_size += NAN_BLOOM_LENGTH_DEFAULT + 1;
		}
		*data_size += ALIGN_SIZE(*data_size + NAN_XTLV_ID_LEN_SIZE, 4);
	}
	if (cmd_data->csid)
		*data_size +=  ALIGN_SIZE(sizeof(nan_sec_csid_e) + NAN_XTLV_ID_LEN_SIZE, 4);
	if (cmd_data->key.dlen)
		*data_size += ALIGN_SIZE(cmd_data->key.dlen + NAN_XTLV_ID_LEN_SIZE, 4);
	if (cmd_data->scid.dlen)
		*data_size += ALIGN_SIZE(cmd_data->scid.dlen + NAN_XTLV_ID_LEN_SIZE, 4);
	if (cmd_data->sde_control_config)
		*data_size += ALIGN_SIZE(sizeof(uint16) + NAN_XTLV_ID_LEN_SIZE, 4);
	if (cmd_data->life_count)
		*data_size += ALIGN_SIZE(sizeof(cmd_data->life_count) + NAN_XTLV_ID_LEN_SIZE, 4);
	return ret;
}

static int
wl_cfgnan_aligned_data_size_of_opt_dp_params(struct bcm_cfg80211 *cfg, uint16 *data_size,
	nan_datapath_cmd_data_t *cmd_data)
{
	s32 ret = BCME_OK;
	if (cmd_data->svc_info.dlen) {
		*data_size += ALIGN_SIZE(cmd_data->svc_info.dlen + NAN_XTLV_ID_LEN_SIZE, 4);
		/* When NDPE is enabled, adding this extra data_size to provide backward
		 * compatability for non-ndpe devices. Duplicating NDP specific info and sending it
		 * to FW in SD SVCINFO and NDPE TLV list as host doesn't know peer's NDPE capability
		 */
		if (cfg->nancfg->ndpe_enabled) {
			*data_size += ALIGN_SIZE(cmd_data->svc_info.dlen + NAN_XTLV_ID_LEN_SIZE, 4);
		}
	}
	if (cmd_data->key.dlen)
		*data_size += ALIGN_SIZE(cmd_data->key.dlen + NAN_XTLV_ID_LEN_SIZE, 4);
	if (cmd_data->csid)
		*data_size += ALIGN_SIZE(sizeof(nan_sec_csid_e) + NAN_XTLV_ID_LEN_SIZE, 4);

	*data_size += ALIGN_SIZE(WL_NAN_SVC_HASH_LEN + NAN_XTLV_ID_LEN_SIZE, 4);
	return ret;
}
int
wl_cfgnan_svc_get_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, uint16 cmd_id, nan_discover_cmd_data_t *cmd_data)
{
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	uint32 instance_id;
	s32 ret = BCME_OK;
	bcm_iov_batch_buf_t *nan_buf = NULL;

	uint8 *resp_buf = NULL;
	uint16 data_size = WL_NAN_OBUF_DATA_OFFSET + sizeof(instance_id);

	NAN_DBG_ENTER();

	nan_buf = MALLOCZ(cfg->osh, data_size);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	resp_buf = MALLOCZ(cfg->osh, NAN_IOCTL_BUF_SIZE_LARGE);
	if (!resp_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}
	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 1;
	/* check if service is present */
	nan_buf->is_set = false;
	sub_cmd = (bcm_iov_batch_subcmd_t*)(&nan_buf->cmds[0]);
	if (cmd_id == WL_NAN_CMD_SD_PUBLISH) {
		instance_id = cmd_data->pub_id;
	} else if (cmd_id == WL_NAN_CMD_SD_SUBSCRIBE) {
		instance_id = cmd_data->sub_id;
	}  else {
		ret = BCME_USAGE_ERROR;
		WL_ERR(("wrong command id = %u\n", cmd_id));
		goto fail;
	}
	/* Fill the sub_command block */
	sub_cmd->id = htod16(cmd_id);
	sub_cmd->len = sizeof(sub_cmd->u.options) + sizeof(instance_id);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);

	ret = memcpy_s(sub_cmd->data, (data_size - WL_NAN_OBUF_DATA_OFFSET),
			&instance_id, sizeof(instance_id));
	if (ret != BCME_OK) {
		WL_ERR(("Failed to copy instance id, ret = %d\n", ret));
		goto fail;
	}

	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, data_size,
			&(cmd_data->status), resp_buf, NAN_IOCTL_BUF_SIZE_LARGE);

	if (unlikely(ret) || unlikely(cmd_data->status)) {
		WL_ERR(("nan svc check failed ret = %d status = %d\n", ret, cmd_data->status));
		goto fail;
	} else {
		WL_DBG(("nan svc check successful..proceed to update\n"));
	}

fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, data_size);
	}

	if (resp_buf) {
		MFREE(cfg->osh, resp_buf, NAN_IOCTL_BUF_SIZE_LARGE);
	}
	NAN_DBG_EXIT();
	return ret;

}

int
wl_cfgnan_svc_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, uint16 cmd_id, nan_discover_cmd_data_t *cmd_data)
{
	s32 ret = BCME_OK;
	bcm_iov_batch_buf_t *nan_buf = NULL;
	uint16 nan_buf_size;
	uint8 *resp_buf = NULL;
	/* Considering fixed params */
	uint16 data_size = WL_NAN_OBUF_DATA_OFFSET +
		OFFSETOF(wl_nan_sd_params_t, optional[0]);

	if (cmd_data->svc_update) {
		ret = wl_cfgnan_svc_get_handler(ndev, cfg, cmd_id, cmd_data);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to update svc handler, ret = %d\n", ret));
			goto fail;
		} else {
			/* Ignoring any other svc get error */
			if (cmd_data->status == WL_NAN_E_BAD_INSTANCE) {
				WL_ERR(("Bad instance status, failed to update svc handler\n"));
				goto fail;
			}
		}
	}

	ret = wl_cfgnan_aligned_data_size_of_opt_disc_params(&data_size, cmd_data);
	if (unlikely(ret)) {
		WL_ERR(("Failed to get alligned size of optional params\n"));
		goto fail;
	}
	nan_buf_size = data_size;
	NAN_DBG_ENTER();

	nan_buf = MALLOCZ(cfg->osh, data_size);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	resp_buf = MALLOCZ(cfg->osh, data_size + NAN_IOVAR_NAME_SIZE);
	if (!resp_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}
	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_buf->is_set = true;

	ret = wl_cfgnan_sd_params_handler(ndev, cmd_data, cmd_id,
			&nan_buf->cmds[0], &nan_buf_size);
	if (unlikely(ret)) {
		WL_ERR((" Service discovery params handler failed, ret = %d\n", ret));
		goto fail;
	}

	nan_buf->count++;
	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, data_size,
			&(cmd_data->status), resp_buf, data_size + NAN_IOVAR_NAME_SIZE);
	if (cmd_data->svc_update && (cmd_data->status == BCME_DATA_NOTFOUND)) {
		/* return OK if update tlv data is not present
		* which means nothing to update
		*/
		cmd_data->status = BCME_OK;
	}
	if (unlikely(ret) || unlikely(cmd_data->status)) {
		WL_ERR(("nan svc failed ret = %d status = %d\n", ret, cmd_data->status));
		goto fail;
	} else {
		WL_DBG(("nan svc successful\n"));
#ifdef WL_NAN_DISC_CACHE
		ret = wl_cfgnan_cache_svc_info(cfg, cmd_data, cmd_id, cmd_data->svc_update);
		if (ret < 0) {
			WL_ERR(("%s: fail to cache svc info, ret=%d\n",
				__FUNCTION__, ret));
			goto fail;
		}
#endif /* WL_NAN_DISC_CACHE */
	}

fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, data_size);
	}

	if (resp_buf) {
		MFREE(cfg->osh, resp_buf, data_size + NAN_IOVAR_NAME_SIZE);
	}
	NAN_DBG_EXIT();
	return ret;
}

int
wl_cfgnan_publish_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_discover_cmd_data_t *cmd_data)
{
	int ret = BCME_OK;

	NAN_DBG_ENTER();
	NAN_MUTEX_LOCK();
	/*
	 * proceed only if mandatory arguments are present - subscriber id,
	 * service hash
	 */
	if ((!cmd_data->pub_id) || (!cmd_data->svc_hash.data) ||
		(!cmd_data->svc_hash.dlen)) {
		WL_ERR(("mandatory arguments are not present\n"));
		ret = BCME_BADARG;
		goto fail;
	}

	ret = wl_cfgnan_svc_handler(ndev, cfg, WL_NAN_CMD_SD_PUBLISH, cmd_data);
	if (ret < 0) {
		WL_ERR(("%s: fail to handle pub, ret=%d\n", __FUNCTION__, ret));
		goto fail;
	}
	WL_INFORM_MEM(("[NAN] Service published for instance id:%d is_update %d\n",
		cmd_data->pub_id, cmd_data->svc_update));

fail:
	NAN_MUTEX_UNLOCK();
	NAN_DBG_EXIT();
	return ret;
}

int
wl_cfgnan_subscribe_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_discover_cmd_data_t *cmd_data)
{
	int ret = BCME_OK;
#ifdef WL_NAN_DISC_CACHE
	nan_svc_info_t *svc_info;
#ifdef RTT_SUPPORT
	uint8 upd_ranging_required;
#endif /* RTT_SUPPORT */
#endif /* WL_NAN_DISC_CACHE */

#ifdef RTT_SUPPORT
#ifdef RTT_GEOFENCE_CONT
	dhd_pub_t *dhd = (struct dhd_pub *)(cfg->pub);
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhd);
#endif /* RTT_GEOFENCE_CONT */
#endif /* RTT_SUPPORT */

	NAN_DBG_ENTER();
	NAN_MUTEX_LOCK();

	/*
	 * proceed only if mandatory arguments are present - subscriber id,
	 * service hash
	 */
	if ((!cmd_data->sub_id) || (!cmd_data->svc_hash.data) ||
		(!cmd_data->svc_hash.dlen)) {
		WL_ERR(("mandatory arguments are not present\n"));
		ret = BCME_BADARG;
		goto fail;
	}

	/* Check for ranging sessions if any */
	if (cmd_data->svc_update) {
#ifdef WL_NAN_DISC_CACHE
		svc_info = wl_cfgnan_get_svc_inst(cfg, cmd_data->sub_id, 0);
		if (svc_info) {
#ifdef RTT_SUPPORT
			wl_cfgnan_clear_svc_from_all_ranging_inst(cfg, cmd_data->sub_id);
			/* terminate ranging sessions for this svc, avoid clearing svc cache */
			wl_cfgnan_terminate_all_obsolete_ranging_sessions(cfg);
			/* Attempt RTT for current geofence target */
			wl_cfgnan_reset_geofence_ranging(cfg, NULL,
				RTT_SCHED_RNG_TERM_SUB_SVC_UPD, TRUE);
			WL_DBG(("Ranging sessions handled for svc update\n"));
			upd_ranging_required = !!(cmd_data->sde_control_flag &
					NAN_SDE_CF_RANGING_REQUIRED);
			if ((svc_info->ranging_required ^ upd_ranging_required) ||
					(svc_info->ingress_limit != cmd_data->ingress_limit) ||
					(svc_info->egress_limit != cmd_data->egress_limit)) {
				/* Clear cache info in Firmware */
				ret = wl_cfgnan_clear_disc_cache(cfg, cmd_data->sub_id);
				if (ret != BCME_OK) {
					WL_ERR(("couldn't send clear cache to FW \n"));
					goto fail;
				}
				/* Invalidate local cache info */
				wl_cfgnan_remove_disc_result(cfg, cmd_data->sub_id);
			}
#endif /* RTT_SUPPORT */
		}
#endif /* WL_NAN_DISC_CACHE */
	}

#ifdef RTT_SUPPORT
#ifdef RTT_GEOFENCE_CONT
	/* Override ranging Indication */
	if (rtt_status->geofence_cfg.geofence_cont) {
		if (cmd_data->ranging_indication !=
				NAN_RANGE_INDICATION_NONE) {
			cmd_data->ranging_indication = NAN_RANGE_INDICATION_CONT;
		}
	}
#endif /* RTT_GEOFENCE_CONT */
#endif /* RTT_SUPPORT */
	ret = wl_cfgnan_svc_handler(ndev, cfg, WL_NAN_CMD_SD_SUBSCRIBE, cmd_data);
	if (ret < 0) {
		WL_ERR(("%s: fail to handle svc, ret=%d\n", __FUNCTION__, ret));
		goto fail;
	}
	WL_INFORM_MEM(("[NAN] Service subscribed for instance id:%d is_update %d\n",
		cmd_data->sub_id, cmd_data->svc_update));

fail:
	NAN_MUTEX_UNLOCK();
	NAN_DBG_EXIT();
	return ret;
}

static int
wl_cfgnan_cancel_handler(nan_discover_cmd_data_t *cmd_data,
	uint16 cmd_id, void *p_buf, uint16 *nan_buf_size)
{
	s32 ret = BCME_OK;

	NAN_DBG_ENTER();

	if (p_buf != NULL) {
		bcm_iov_batch_subcmd_t *sub_cmd = (bcm_iov_batch_subcmd_t*)(p_buf);
		wl_nan_instance_id_t instance_id;

		if (cmd_id == WL_NAN_CMD_SD_CANCEL_PUBLISH) {
			instance_id = cmd_data->pub_id;
		} else if (cmd_id == WL_NAN_CMD_SD_CANCEL_SUBSCRIBE) {
			instance_id = cmd_data->sub_id;
		}  else {
			ret = BCME_USAGE_ERROR;
			WL_ERR(("wrong command id = %u\n", cmd_id));
			goto fail;
		}

		/* Fill the sub_command block */
		sub_cmd->id = htod16(cmd_id);
		sub_cmd->len = sizeof(sub_cmd->u.options) + sizeof(instance_id);
		sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);
		ret = memcpy_s(sub_cmd->data, *nan_buf_size,
				&instance_id, sizeof(instance_id));
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy instance id, ret = %d\n", ret));
			goto fail;
		}
		/* adjust iov data len to the end of last data record */
		*nan_buf_size -= (sub_cmd->len +
				OFFSETOF(bcm_iov_batch_subcmd_t, u.options));
		WL_INFORM_MEM(("[NAN] Service with instance id:%d cancelled\n", instance_id));
	} else {
		WL_ERR(("nan_iov_buf is NULL\n"));
		ret = BCME_ERROR;
		goto fail;
	}

fail:
	NAN_DBG_EXIT();
	return ret;
}

int
wl_cfgnan_cancel_pub_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_discover_cmd_data_t *cmd_data)
{
	bcm_iov_batch_buf_t *nan_buf = NULL;
	s32 ret = BCME_OK;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE];

	NAN_DBG_ENTER();
	NAN_MUTEX_LOCK();

	nan_buf = MALLOCZ(cfg->osh, nan_buf_size);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_buf_size -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);

	/* proceed only if mandatory argument is present - publisher id */
	if (!cmd_data->pub_id) {
		WL_ERR(("mandatory argument is not present\n"));
		ret = BCME_BADARG;
		goto fail;
	}

#ifdef WL_NAN_DISC_CACHE
	wl_cfgnan_clear_svc_cache(cfg, cmd_data->pub_id);
#endif /* WL_NAN_DISC_CACHE */
	ret = wl_cfgnan_cancel_handler(cmd_data, WL_NAN_CMD_SD_CANCEL_PUBLISH,
			&nan_buf->cmds[0], &nan_buf_size);
	if (unlikely(ret)) {
		WL_ERR(("cancel publish failed\n"));
		goto fail;
	}
	nan_buf->is_set = true;
	nan_buf->count++;

	bzero(resp_buf, sizeof(resp_buf));
	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, nan_buf_size,
			&(cmd_data->status),
			(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
	if (unlikely(ret) || unlikely(cmd_data->status)) {
		WL_ERR(("nan cancel publish failed ret = %d status = %d\n",
			ret, cmd_data->status));
		goto fail;
	}
	WL_DBG(("nan cancel publish successfull\n"));
	wl_cfgnan_remove_inst_id(cfg, cmd_data->pub_id);
fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, NAN_IOCTL_BUF_SIZE);
	}

	NAN_MUTEX_UNLOCK();
	NAN_DBG_EXIT();
	return ret;
}

int
wl_cfgnan_cancel_sub_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_discover_cmd_data_t *cmd_data)
{
	bcm_iov_batch_buf_t *nan_buf = NULL;
	s32 ret = BCME_OK;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE];

	NAN_DBG_ENTER();
	NAN_MUTEX_LOCK();

	nan_buf = MALLOCZ(cfg->osh, nan_buf_size);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_buf_size -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);

	/* proceed only if mandatory argument is present - subscriber id */
	if (!cmd_data->sub_id) {
		WL_ERR(("mandatory argument is not present\n"));
		ret = BCME_BADARG;
		goto fail;
	}

#ifdef WL_NAN_DISC_CACHE
#ifdef RTT_SUPPORT
	/* terminate ranging sessions for this svc */
	wl_cfgnan_clear_svc_from_all_ranging_inst(cfg, cmd_data->sub_id);
	wl_cfgnan_terminate_all_obsolete_ranging_sessions(cfg);
	wl_cfgnan_reset_geofence_ranging(cfg, NULL,
		RTT_SCHED_RNG_TERM_SUB_SVC_CANCEL, TRUE);
#endif /* RTT_SUPPORT */
	/* clear svc cache for the service */
	wl_cfgnan_clear_svc_cache(cfg, cmd_data->sub_id);
	wl_cfgnan_remove_disc_result(cfg, cmd_data->sub_id);
#endif /* WL_NAN_DISC_CACHE */

	ret = wl_cfgnan_cancel_handler(cmd_data, WL_NAN_CMD_SD_CANCEL_SUBSCRIBE,
			&nan_buf->cmds[0], &nan_buf_size);
	if (unlikely(ret)) {
		WL_ERR(("cancel subscribe failed\n"));
		goto fail;
	}
	nan_buf->is_set = true;
	nan_buf->count++;

	bzero(resp_buf, sizeof(resp_buf));
	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, nan_buf_size,
			&(cmd_data->status),
			(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
	if (unlikely(ret) || unlikely(cmd_data->status)) {
		WL_ERR(("nan cancel subscribe failed ret = %d status = %d\n",
			ret, cmd_data->status));
		goto fail;
	}
	WL_DBG(("subscribe cancel successfull\n"));
	wl_cfgnan_remove_inst_id(cfg, cmd_data->sub_id);
fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, NAN_IOCTL_BUF_SIZE);
	}

	NAN_MUTEX_UNLOCK();
	NAN_DBG_EXIT();
	return ret;
}

int
wl_cfgnan_transmit_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_discover_cmd_data_t *cmd_data)
{
	s32 ret = BCME_OK;
	bcm_iov_batch_buf_t *nan_buf = NULL;
	wl_nan_sd_transmit_t *sd_xmit = NULL;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	bool is_lcl_id = FALSE;
	bool is_dest_id = FALSE;
	bool is_dest_mac = FALSE;
	uint16 buflen_avail;
	uint8 *pxtlv;
	uint16 nan_buf_size;
	uint8 *resp_buf = NULL;
	/* Considering fixed params */
	uint16 data_size = WL_NAN_OBUF_DATA_OFFSET +
		OFFSETOF(wl_nan_sd_transmit_t, opt_tlv);
	data_size = ALIGN_SIZE(data_size, 4);
	ret = wl_cfgnan_aligned_data_size_of_opt_disc_params(&data_size, cmd_data);
	if (unlikely(ret)) {
		WL_ERR(("Failed to get alligned size of optional params\n"));
		goto fail;
	}
	NAN_DBG_ENTER();
	NAN_MUTEX_LOCK();
	nan_buf_size = data_size;
	nan_buf = MALLOCZ(cfg->osh, data_size);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	resp_buf = MALLOCZ(cfg->osh, data_size + NAN_IOVAR_NAME_SIZE);
	if (!resp_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	/* nan transmit */
	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_buf_size -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);
	/*
	 * proceed only if mandatory arguments are present - subscriber id,
	 * publisher id, mac address
	 */
	if ((!cmd_data->local_id) || (!cmd_data->remote_id) ||
			ETHER_ISNULLADDR(&cmd_data->mac_addr.octet)) {
		WL_ERR(("mandatory arguments are not present\n"));
		ret = -EINVAL;
		goto fail;
	}

	sub_cmd = (bcm_iov_batch_subcmd_t*)(&nan_buf->cmds[0]);
	sd_xmit = (wl_nan_sd_transmit_t *)(sub_cmd->data);

	/* local instance id must be from 1 to 255, 0 is reserved */
	if (cmd_data->local_id == NAN_ID_RESERVED) {
		WL_ERR(("Invalid local instance id: %d\n", cmd_data->local_id));
		ret = BCME_BADARG;
		goto fail;
	}
	sd_xmit->local_service_id = cmd_data->local_id;
	is_lcl_id = TRUE;

	/* remote instance id must be from 1 to 255, 0 is reserved */
	if (cmd_data->remote_id == NAN_ID_RESERVED) {
		WL_ERR(("Invalid remote instance id: %d\n", cmd_data->remote_id));
		ret = BCME_BADARG;
		goto fail;
	}

	sd_xmit->requestor_service_id = cmd_data->remote_id;
	is_dest_id = TRUE;

	if (!ETHER_ISNULLADDR(&cmd_data->mac_addr.octet)) {
		ret = memcpy_s(&sd_xmit->destination_addr, ETHER_ADDR_LEN,
				&cmd_data->mac_addr, ETHER_ADDR_LEN);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy dest mac address\n"));
			goto fail;
		}
	} else {
		WL_ERR(("Invalid ether addr provided\n"));
		ret = BCME_BADARG;
		goto fail;
	}
	is_dest_mac = TRUE;

	if (cmd_data->priority) {
		sd_xmit->priority = cmd_data->priority;
	}
	sd_xmit->token = cmd_data->token;

	if (cmd_data->recv_ind_flag) {
		/* BIT0 - If set, host wont rec event "txs"  */
		if (CHECK_BIT(cmd_data->recv_ind_flag,
				WL_NAN_EVENT_SUPPRESS_FOLLOWUP_RECEIVE_BIT)) {
			sd_xmit->flags = WL_NAN_FUP_SUPR_EVT_TXS;
		}
	}
	/* Optional parameters: fill the sub_command block with service descriptor attr */
	sub_cmd->id = htod16(WL_NAN_CMD_SD_TRANSMIT);
	sub_cmd->len = sizeof(sub_cmd->u.options) +
		OFFSETOF(wl_nan_sd_transmit_t, opt_tlv);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);
	pxtlv = (uint8 *)&sd_xmit->opt_tlv;

	nan_buf_size -= (sub_cmd->len +
			OFFSETOF(bcm_iov_batch_subcmd_t, u.options));

	buflen_avail = nan_buf_size;

	if (cmd_data->svc_info.data && cmd_data->svc_info.dlen) {
		bcm_xtlv_t *pxtlv_svc_info = (bcm_xtlv_t *)pxtlv;
		ret = bcm_pack_xtlv_entry(&pxtlv, &nan_buf_size,
				WL_NAN_XTLV_SD_SVC_INFO, cmd_data->svc_info.dlen,
				cmd_data->svc_info.data, BCM_XTLV_OPTION_ALIGN32);
		if (unlikely(ret)) {
			WL_ERR(("%s: fail to pack on bcm_pack_xtlv_entry, ret=%d\n",
				__FUNCTION__, ret));
			goto fail;
		}

		/* 0xFF is max length for svc_info */
		if (pxtlv_svc_info->len > 0xFF) {
			WL_ERR(("Invalid service info length %d\n",
				(pxtlv_svc_info->len)));
			ret = BCME_USAGE_ERROR;
			goto fail;
		}
		sd_xmit->opt_len = (uint8)(pxtlv_svc_info->len);
	}
	if (cmd_data->sde_svc_info.data && cmd_data->sde_svc_info.dlen) {
		WL_TRACE(("optional sdea svc_info present, pack it\n"));
		ret = bcm_pack_xtlv_entry(&pxtlv, &nan_buf_size,
				WL_NAN_XTLV_SD_SDE_SVC_INFO, cmd_data->sde_svc_info.dlen,
				cmd_data->sde_svc_info.data, BCM_XTLV_OPTION_ALIGN32);
		if (unlikely(ret)) {
			WL_ERR(("%s: fail to pack sdea svc info\n", __FUNCTION__));
			goto fail;
		}
	}

	/* Check if all mandatory params are provided */
	if (is_lcl_id && is_dest_id && is_dest_mac) {
		nan_buf->count++;
		sub_cmd->len += (buflen_avail - nan_buf_size);
	} else {
		WL_ERR(("Missing parameters\n"));
		ret = BCME_USAGE_ERROR;
	}
	nan_buf->is_set = TRUE;
	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, data_size,
			&(cmd_data->status), resp_buf, data_size + NAN_IOVAR_NAME_SIZE);
	if (unlikely(ret) || unlikely(cmd_data->status)) {
		WL_ERR(("nan transmit failed for token %d ret = %d status = %d\n",
			sd_xmit->token, ret, cmd_data->status));
		goto fail;
	}
	WL_INFORM_MEM(("nan transmit successful for token %d\n", sd_xmit->token));
fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, data_size);
	}
	if (resp_buf) {
		MFREE(cfg->osh, resp_buf, data_size + NAN_IOVAR_NAME_SIZE);
	}
	NAN_MUTEX_UNLOCK();
	NAN_DBG_EXIT();
	return ret;
}

static int
wl_cfgnan_get_capability(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_hal_capabilities_t *capabilities)
{
	bcm_iov_batch_buf_t *nan_buf = NULL;
	s32 ret = BCME_OK;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	wl_nan_fw_cap_t *fw_cap = NULL;
	uint16 subcmd_len;
	uint32 status;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	bcm_iov_batch_subcmd_t *sub_cmd_resp = NULL;
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE];
	const bcm_xtlv_t *xtlv;
	uint16 type = 0;
	int len = 0;

	NAN_DBG_ENTER();
	nan_buf = MALLOCZ(cfg->osh, nan_buf_size);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_buf_size -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);
	sub_cmd = (bcm_iov_batch_subcmd_t*)(uint8 *)(&nan_buf->cmds[0]);

	ret = wl_cfg_nan_check_cmd_len(nan_buf_size,
			sizeof(*fw_cap), &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		goto fail;
	}

	fw_cap = (wl_nan_fw_cap_t *)sub_cmd->data;
	sub_cmd->id = htod16(WL_NAN_CMD_GEN_FW_CAP);
	sub_cmd->len = sizeof(sub_cmd->u.options) + sizeof(*fw_cap);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);
	nan_buf_size -= subcmd_len;
	nan_buf->count = 1;

	nan_buf->is_set = false;
	memset(resp_buf, 0, sizeof(resp_buf));
	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, nan_buf_size, &status,
			(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
	if (unlikely(ret) || unlikely(status)) {
		WL_ERR(("get nan fw cap failed ret %d status %d \n",
				ret, status));
		goto fail;
	}

	sub_cmd_resp = &((bcm_iov_batch_buf_t *)(resp_buf))->cmds[0];

	/* check the response buff */
	xtlv = ((const bcm_xtlv_t *)&sub_cmd_resp->data[0]);
	if (!xtlv) {
		ret = BCME_NOTFOUND;
		WL_ERR(("xtlv not found: err = %d\n", ret));
		goto fail;
	}
	bcm_xtlv_unpack_xtlv(xtlv, &type, (uint16*)&len, NULL, BCM_XTLV_OPTION_ALIGN32);
	do
	{
		switch (type) {
			case WL_NAN_XTLV_GEN_FW_CAP:
				if (len > sizeof(wl_nan_fw_cap_t)) {
					ret = BCME_BADARG;
					goto fail;
				}
				GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
				fw_cap = (wl_nan_fw_cap_t*)xtlv->data;
				GCC_DIAGNOSTIC_POP();
				break;
			default:
				WL_ERR(("Unknown xtlv: id %u\n", type));
				ret = BCME_ERROR;
				break;
		}
		if (ret != BCME_OK) {
			goto fail;
		}
	} while ((xtlv = bcm_next_xtlv(xtlv, &len, BCM_XTLV_OPTION_ALIGN32)));

	memset(capabilities, 0, sizeof(nan_hal_capabilities_t));
	capabilities->max_publishes = fw_cap->max_svc_publishes;
	capabilities->max_subscribes = fw_cap->max_svc_subscribes;
	capabilities->max_ndi_interfaces = fw_cap->max_lcl_ndi_interfaces;
	capabilities->max_ndp_sessions = fw_cap->max_ndp_sessions;
	capabilities->max_concurrent_nan_clusters = fw_cap->max_concurrent_nan_clusters;
	capabilities->max_service_name_len = fw_cap->max_service_name_len;
	capabilities->max_match_filter_len = fw_cap->max_match_filter_len;
	capabilities->max_total_match_filter_len = fw_cap->max_total_match_filter_len;
	capabilities->max_service_specific_info_len = fw_cap->max_service_specific_info_len;
	capabilities->max_app_info_len = fw_cap->max_app_info_len;
	capabilities->max_sdea_service_specific_info_len = fw_cap->max_sdea_svc_specific_info_len;
	capabilities->max_queued_transmit_followup_msgs = fw_cap->max_queued_tx_followup_msgs;
	capabilities->max_subscribe_address = fw_cap->max_subscribe_address;
	capabilities->is_ndp_security_supported = fw_cap->is_ndp_security_supported;
	capabilities->ndp_supported_bands = fw_cap->ndp_supported_bands;
	capabilities->cipher_suites_supported = fw_cap->cipher_suites_supported_mask;
	if (fw_cap->flags1 & WL_NAN_FW_CAP_FLAG1_NDPE) {
		capabilities->ndpe_attr_supported = true;
	}

fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, NAN_IOCTL_BUF_SIZE);
	}
	NAN_DBG_EXIT();
	return ret;
}

int
wl_cfgnan_get_capablities_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_hal_capabilities_t *capabilities)
{
	s32 ret = BCME_OK;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(ndev);

	NAN_DBG_ENTER();

	/* Do not query fw about nan if feature is not supported */
	if (!FW_SUPPORTED(dhdp, nan)) {
		WL_DBG(("NAN is not supported\n"));
		return ret;
	}

	if (cfg->nancfg->nan_init_state) {
		ret = wl_cfgnan_get_capability(ndev, cfg, capabilities);
		if (ret != BCME_OK) {
			WL_ERR(("NAN init state: %d, failed to get capability from FW[%d]\n",
					cfg->nancfg->nan_init_state, ret));
			goto exit;
		}
	} else {
		/* Initialize NAN before sending iovar */
		WL_ERR(("Initializing NAN\n"));
		ret = wl_cfgnan_init(cfg);
		if (ret != BCME_OK) {
			WL_ERR(("failed to initialize NAN[%d]\n", ret));
			goto fail;
		}

		ret = wl_cfgnan_get_capability(ndev, cfg, capabilities);
		if (ret != BCME_OK) {
			WL_ERR(("NAN init state: %d, failed to get capability from FW[%d]\n",
					cfg->nancfg->nan_init_state, ret));
			goto exit;
		}
		WL_ERR(("De-Initializing NAN\n"));
		ret = wl_cfgnan_deinit(cfg, dhdp->up);
		if (ret != BCME_OK) {
			WL_ERR(("failed to de-initialize NAN[%d]\n", ret));
			goto fail;
		}
	}
fail:
	NAN_DBG_EXIT();
	return ret;
exit:
	/* Keeping backward campatibility */
	capabilities->max_concurrent_nan_clusters = MAX_CONCURRENT_NAN_CLUSTERS;
	capabilities->max_publishes = MAX_PUBLISHES;
	capabilities->max_subscribes = MAX_SUBSCRIBES;
	capabilities->max_service_name_len = MAX_SVC_NAME_LEN;
	capabilities->max_match_filter_len = MAX_MATCH_FILTER_LEN;
	capabilities->max_total_match_filter_len = MAX_TOTAL_MATCH_FILTER_LEN;
	capabilities->max_service_specific_info_len = NAN_MAX_SERVICE_SPECIFIC_INFO_LEN;
	capabilities->max_ndi_interfaces = NAN_MAX_NDI;
	capabilities->max_ndp_sessions = MAX_NDP_SESSIONS;
	capabilities->max_app_info_len = MAX_APP_INFO_LEN;
	capabilities->max_queued_transmit_followup_msgs = MAX_QUEUED_TX_FOLLOUP_MSGS;
	capabilities->max_sdea_service_specific_info_len = MAX_SDEA_SVC_INFO_LEN;
	capabilities->max_subscribe_address = MAX_SUBSCRIBE_ADDRESS;
	capabilities->cipher_suites_supported = WL_NAN_CIPHER_SUITE_SHARED_KEY_128_MASK;
	capabilities->max_scid_len = MAX_SCID_LEN;
	capabilities->is_ndp_security_supported = true;
	capabilities->ndp_supported_bands = NDP_SUPPORTED_BANDS;
	capabilities->ndpe_attr_supported = false;
	ret = BCME_OK;
	NAN_DBG_EXIT();
	return ret;
}

bool wl_cfgnan_is_enabled(struct bcm_cfg80211 *cfg)
{
	wl_nancfg_t *nancfg = cfg->nancfg;
	if (nancfg) {
		if (nancfg->nan_init_state && nancfg->nan_enable) {
			return TRUE;
		}
	}

	return FALSE;
}

static int
wl_cfgnan_init(struct bcm_cfg80211 *cfg)
{
	s32 ret = BCME_OK;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	uint32 status;
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE];
	uint8 buf[NAN_IOCTL_BUF_SIZE];
	bcm_iov_batch_buf_t *nan_buf = (bcm_iov_batch_buf_t*)buf;

	NAN_DBG_ENTER();
	if (cfg->nancfg->nan_init_state) {
		WL_ERR(("nan initialized/nmi exists\n"));
		return BCME_OK;
	}
	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_buf_size -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);
	ret = wl_cfgnan_init_handler(&nan_buf->cmds[0], &nan_buf_size, true);
	if (unlikely(ret)) {
		WL_ERR(("init handler sub_cmd set failed\n"));
		goto fail;
	}
	nan_buf->count++;
	nan_buf->is_set = true;

	bzero(resp_buf, sizeof(resp_buf));
	ret = wl_cfgnan_execute_ioctl(bcmcfg_to_prmry_ndev(cfg), cfg,
			nan_buf, nan_buf_size, &status,
			(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
	if (unlikely(ret) || unlikely(status)) {
		WL_ERR(("nan init handler failed ret %d status %d\n",
				ret, status));
		goto fail;
	}

#ifdef WL_NAN_DISC_CACHE
	/* malloc for disc result */
	cfg->nancfg->nan_disc_cache = MALLOCZ(cfg->osh,
			NAN_MAX_CACHE_DISC_RESULT * sizeof(nan_disc_result_cache));
	if (!cfg->nancfg->nan_disc_cache) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}
#endif /* WL_NAN_DISC_CACHE */
	cfg->nancfg->nan_init_state = true;
	return ret;
fail:
	NAN_DBG_EXIT();
	return ret;
}

static void
wl_cfgnan_deinit_cleanup(struct bcm_cfg80211 *cfg)
{
	uint8 i = 0;
	wl_nancfg_t *nancfg = cfg->nancfg;

	nancfg->nan_dp_count = 0;
	nancfg->nan_init_state = false;
#ifdef WL_NAN_DISC_CACHE
	if (nancfg->nan_disc_cache) {
		for (i = 0; i < NAN_MAX_CACHE_DISC_RESULT; i++) {
			if (nancfg->nan_disc_cache[i].tx_match_filter.data) {
				MFREE(cfg->osh, nancfg->nan_disc_cache[i].tx_match_filter.data,
					nancfg->nan_disc_cache[i].tx_match_filter.dlen);
			}
			if (nancfg->nan_disc_cache[i].svc_info.data) {
				MFREE(cfg->osh, nancfg->nan_disc_cache[i].svc_info.data,
					nancfg->nan_disc_cache[i].svc_info.dlen);
			}
		}
		MFREE(cfg->osh, nancfg->nan_disc_cache,
			NAN_MAX_CACHE_DISC_RESULT * sizeof(nan_disc_result_cache));
		nancfg->nan_disc_cache = NULL;
	}
	nancfg->nan_disc_count = 0;
	bzero(nancfg->svc_info, NAN_MAX_SVC_INST * sizeof(nan_svc_info_t));
	bzero(nancfg->nan_ranging_info, NAN_MAX_RANGING_INST * sizeof(nan_ranging_inst_t));
#endif /* WL_NAN_DISC_CACHE */
	return;
}

static int
wl_cfgnan_deinit(struct bcm_cfg80211 *cfg, uint8 busstate)
{
	s32 ret = BCME_OK;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	uint32 status;
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE];
	uint8 buf[NAN_IOCTL_BUF_SIZE];
	bcm_iov_batch_buf_t *nan_buf = (bcm_iov_batch_buf_t*)buf;
	wl_nancfg_t *nancfg = cfg->nancfg;

	NAN_DBG_ENTER();
	NAN_MUTEX_LOCK();

	if (!nancfg->nan_init_state) {
		WL_ERR(("nan is not initialized/nmi doesnt exists\n"));
		ret = BCME_OK;
		goto fail;
	}

	if (busstate != DHD_BUS_DOWN) {
		nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
		nan_buf->count = 0;
		nan_buf_size -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);

		WL_DBG(("nan deinit\n"));
		ret = wl_cfgnan_init_handler(&nan_buf->cmds[0], &nan_buf_size, false);
		if (unlikely(ret)) {
			WL_ERR(("deinit handler sub_cmd set failed\n"));
		} else {
			nan_buf->count++;
			nan_buf->is_set = true;
			bzero(resp_buf, sizeof(resp_buf));
			ret = wl_cfgnan_execute_ioctl(cfg->wdev->netdev, cfg,
				nan_buf, nan_buf_size, &status,
				(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
			if (unlikely(ret) || unlikely(status)) {
				WL_ERR(("nan init handler failed ret %d status %d\n",
					ret, status));
			}
		}
	}
	wl_cfgnan_deinit_cleanup(cfg);

fail:
	if (!nancfg->mac_rand && !ETHER_ISNULLADDR(nancfg->nan_nmi_mac)) {
		wl_release_vif_macaddr(cfg, nancfg->nan_nmi_mac, WL_IF_TYPE_NAN_NMI);
	}
	NAN_MUTEX_UNLOCK();
	NAN_DBG_EXIT();
	return ret;
}

static int
wl_cfgnan_get_ndi_macaddr(struct bcm_cfg80211 *cfg, u8* mac_addr)
{
	int i = 0;
	int ret = BCME_OK;
	bool rand_mac = cfg->nancfg->mac_rand;
	BCM_REFERENCE(i);

	if (rand_mac) {
		/* ensure nmi != ndi */
		do {
			RANDOM_BYTES(mac_addr, ETHER_ADDR_LEN);
			/* restore mcast and local admin bits to 0 and 1 */
			ETHER_SET_UNICAST(mac_addr);
			ETHER_SET_LOCALADDR(mac_addr);
			i++;
			if (i == NAN_RAND_MAC_RETRIES) {
				break;
			}
		} while (eacmp(cfg->nancfg->nan_nmi_mac, mac_addr) == 0);

		if (i == NAN_RAND_MAC_RETRIES) {
			if (eacmp(cfg->nancfg->nan_nmi_mac, mac_addr) == 0) {
				WL_ERR(("\nCouldn't generate rand NDI which != NMI\n"));
				ret = BCME_NORESOURCE;
				goto fail;
			}
		}
	} else {
		if (wl_get_vif_macaddr(cfg, WL_IF_TYPE_NAN,
			mac_addr) != BCME_OK) {
			ret = -EINVAL;
			WL_ERR(("Failed to get mac addr for NDI\n"));
			goto fail;
		}
	}

fail:
	return ret;
}

int
wl_cfgnan_data_path_iface_create_delete_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, char *ifname, uint16 type, uint8 busstate)
{
	u8 mac_addr[ETH_ALEN];
	s32 ret = BCME_OK;
	s32 idx;
	struct wireless_dev *wdev;
	NAN_DBG_ENTER();

	if (busstate != DHD_BUS_DOWN) {
		ASSERT(cfg->nancfg->ndi);
		if (type == NAN_WIFI_SUBCMD_DATA_PATH_IFACE_CREATE) {
			if ((idx = wl_cfgnan_get_ndi_idx(cfg)) < 0) {
				WL_ERR(("No free idx for NAN NDI\n"));
				ret = BCME_NORESOURCE;
				goto fail;
			}

			ret = wl_cfgnan_get_ndi_macaddr(cfg, mac_addr);
			if (ret != BCME_OK) {
				WL_ERR(("Couldn't get mac addr for NDI ret %d\n", ret));
				goto fail;
			}
			wdev = wl_cfg80211_add_if(cfg, ndev, WL_IF_TYPE_NAN,
				ifname, mac_addr);
			if (!wdev) {
				ret = -ENODEV;
				WL_ERR(("Failed to create NDI iface = %s, wdev is NULL\n", ifname));
				goto fail;
			}
			/* Store the iface name to pub data so that it can be used
			 * during NAN enable
			 */
			wl_cfgnan_add_ndi_data(cfg, idx, ifname);
			cfg->nancfg->ndi[idx].created = true;
			/* Store nan ndev */
			cfg->nancfg->ndi[idx].nan_ndev = wdev_to_ndev(wdev);

		} else if (type == NAN_WIFI_SUBCMD_DATA_PATH_IFACE_DELETE) {
			ret = wl_cfg80211_del_if(cfg, ndev, NULL, ifname);
			if (ret == BCME_OK) {
				if (wl_cfgnan_del_ndi_data(cfg, ifname) < 0) {
					WL_ERR(("Failed to find matching data for ndi:%s\n",
					ifname));
				}
			} else if (ret == -ENODEV) {
				WL_INFORM(("Already deleted: %s\n", ifname));
				ret = BCME_OK;
			} else if (ret != BCME_OK) {
				WL_ERR(("failed to delete NDI[%d]\n", ret));
			}
		}
	} else {
		ret = -ENODEV;
		WL_ERR(("Bus is already down, no dev found to remove, ret = %d\n", ret));
	}
fail:
	NAN_DBG_EXIT();
	return ret;
}

/*
 * Return data peer from peer list
 * for peer_addr
 * NULL if not found
 */
static nan_ndp_peer_t *
wl_cfgnan_data_get_peer(struct bcm_cfg80211 *cfg,
	struct ether_addr *peer_addr)
{
	uint8 i;
	nan_ndp_peer_t* peer = cfg->nancfg->nan_ndp_peer_info;

	if (!peer) {
		WL_ERR(("wl_cfgnan_data_get_peer: nan_ndp_peer_info is NULL\n"));
		goto exit;
	}
	for (i = 0; i < cfg->nancfg->max_ndp_count; i++) {
		if (peer[i].peer_dp_state != NAN_PEER_DP_NOT_CONNECTED &&
			(!memcmp(peer_addr, &peer[i].peer_addr, ETHER_ADDR_LEN))) {
			return &peer[i];
		}
	}

exit:
	return NULL;
}

/*
 * Returns True if
 * datapath exists for nan cfg
 * for given peer
 */
bool
wl_cfgnan_data_dp_exists_with_peer(struct bcm_cfg80211 *cfg,
		struct ether_addr *peer_addr)
{
	bool ret = FALSE;
	nan_ndp_peer_t* peer = NULL;

	if ((cfg->nancfg->nan_init_state == FALSE) ||
		(cfg->nancfg->nan_enable == FALSE)) {
		goto exit;
	}

	/* check for peer exist */
	peer = wl_cfgnan_data_get_peer(cfg, peer_addr);
	if (peer) {
		ret = TRUE;
	}

exit:
	return ret;
}

/*
 * As of now API only available
 * for setting state to CONNECTED
 * if applicable
 */
static void
wl_cfgnan_data_set_peer_dp_state(struct bcm_cfg80211 *cfg,
		struct ether_addr *peer_addr, nan_peer_dp_state_t state)
{
	nan_ndp_peer_t* peer = NULL;
	/* check for peer exist */
	peer = wl_cfgnan_data_get_peer(cfg, peer_addr);
	if (!peer) {
		goto end;
	}
	peer->peer_dp_state = state;
end:
	return;
}

/* Adds peer to nan data peer list */
void
wl_cfgnan_data_add_peer(struct bcm_cfg80211 *cfg,
		struct ether_addr *peer_addr)
{
	uint8 i;
	nan_ndp_peer_t* peer = NULL;
	/* check for peer exist */
	peer = wl_cfgnan_data_get_peer(cfg, peer_addr);
	if (peer) {
		peer->dp_count++;
		goto end;
	}
	peer = cfg->nancfg->nan_ndp_peer_info;
	for (i = 0; i < cfg->nancfg->max_ndp_count; i++) {
		if (peer[i].peer_dp_state == NAN_PEER_DP_NOT_CONNECTED) {
			break;
		}
	}
	if (i == NAN_MAX_NDP_PEER) {
		WL_DBG(("DP Peer list full, Droopping add peer req\n"));
		goto end;
	}
	/* Add peer to list */
	memcpy(&peer[i].peer_addr, peer_addr, ETHER_ADDR_LEN);
	peer[i].dp_count = 1;
	peer[i].peer_dp_state = NAN_PEER_DP_CONNECTING;

end:
	return;
}

/* Removes nan data peer from peer list */
void
wl_cfgnan_data_remove_peer(struct bcm_cfg80211 *cfg,
		struct ether_addr *peer_addr)
{
	nan_ndp_peer_t* peer = NULL;
	/* check for peer exist */
	peer = wl_cfgnan_data_get_peer(cfg, peer_addr);
	if (!peer) {
		WL_DBG(("DP Peer not present in list, "
			"Droopping remove peer req\n"));
		goto end;
	}
	peer->dp_count--;
	if (peer->dp_count == 0) {
		/* No more NDPs, delete entry */
		memset(peer, 0, sizeof(nan_ndp_peer_t));
	} else {
		/* Set peer dp state to connected if any ndp still exits */
		peer->peer_dp_state = NAN_PEER_DP_CONNECTED;
	}
end:
	return;
}

int
wl_cfgnan_data_path_request_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_datapath_cmd_data_t *cmd_data,
	uint8 *ndp_instance_id)
{
	s32 ret = BCME_OK;
	bcm_iov_batch_buf_t *nan_buf = NULL;
	wl_nan_dp_req_t *datareq = NULL;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	uint16 buflen_avail;
	uint8 *pxtlv;
	struct wireless_dev *wdev;
	uint16 nan_buf_size;
	uint8 *resp_buf = NULL;
	/* Considering fixed params */
	uint16 data_size = WL_NAN_OBUF_DATA_OFFSET +
		OFFSETOF(wl_nan_dp_req_t, tlv_params);
	data_size = ALIGN_SIZE(data_size, 4);

	ret = wl_cfgnan_aligned_data_size_of_opt_dp_params(cfg, &data_size, cmd_data);
	if (unlikely(ret)) {
		WL_ERR(("Failed to get alligned size of optional params\n"));
		goto fail;
	}

	nan_buf_size = data_size;
	NAN_DBG_ENTER();

	mutex_lock(&cfg->if_sync);
	NAN_MUTEX_LOCK();
#ifdef WL_IFACE_MGMT
	if ((ret = wl_cfg80211_handle_if_role_conflict(cfg, WL_IF_TYPE_NAN)) < 0) {
		WL_ERR(("Conflicting iface found to be active\n"));
		ret = BCME_UNSUPPORTED;
		goto fail;
	}
#endif /* WL_IFACE_MGMT */

#ifdef RTT_SUPPORT
	/* cancel any ongoing RTT session with peer
	* as we donot support DP and RNG to same peer
	*/
	wl_cfgnan_handle_dp_ranging_concurrency(cfg, &cmd_data->mac_addr,
		RTT_GEO_SUSPN_HOST_NDP_TRIGGER);
#endif /* RTT_SUPPORT */

	nan_buf = MALLOCZ(cfg->osh, data_size);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	resp_buf = MALLOCZ(cfg->osh, data_size + NAN_IOVAR_NAME_SIZE);
	if (!resp_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	ret = wl_cfgnan_set_nan_avail(bcmcfg_to_prmry_ndev(cfg),
			cfg, &cmd_data->avail_params, WL_AVAIL_LOCAL);
	if (unlikely(ret)) {
		WL_ERR(("Failed to set avail value with type local\n"));
		goto fail;
	}

	ret = wl_cfgnan_set_nan_avail(bcmcfg_to_prmry_ndev(cfg),
			cfg, &cmd_data->avail_params, WL_AVAIL_NDC);
	if (unlikely(ret)) {
		WL_ERR(("Failed to set avail value with type ndc\n"));
		goto fail;
	}

	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_buf_size -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);

	sub_cmd = (bcm_iov_batch_subcmd_t*)(&nan_buf->cmds[0]);
	datareq = (wl_nan_dp_req_t *)(sub_cmd->data);

	/* setting default data path type to unicast */
	datareq->type = WL_NAN_DP_TYPE_UNICAST;

	if (cmd_data->pub_id) {
		datareq->pub_id = cmd_data->pub_id;
	}

	if (!ETHER_ISNULLADDR(&cmd_data->mac_addr.octet)) {
		ret = memcpy_s(&datareq->peer_mac, ETHER_ADDR_LEN,
				&cmd_data->mac_addr, ETHER_ADDR_LEN);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy ether addr provided\n"));
			goto fail;
		}
	} else {
		WL_ERR(("Invalid ether addr provided\n"));
		ret = BCME_BADARG;
		goto fail;
	}

	/* Retrieve mac from given iface name */
	wdev = wl_cfg80211_get_wdev_from_ifname(cfg,
		(char *)cmd_data->ndp_iface);
	if (!wdev || ETHER_ISNULLADDR(wdev->netdev->dev_addr)) {
		ret = -EINVAL;
		WL_ERR(("Failed to retrieve wdev/dev addr for ndp_iface = %s\n",
			(char *)cmd_data->ndp_iface));
		goto fail;
	}

	if (!ETHER_ISNULLADDR(wdev->netdev->dev_addr)) {
		ret = memcpy_s(&datareq->ndi, ETHER_ADDR_LEN,
				wdev->netdev->dev_addr, ETHER_ADDR_LEN);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy ether addr provided\n"));
			goto fail;
		}
		WL_TRACE(("%s: Retrieved ndi mac " MACDBG "\n",
			__FUNCTION__, MAC2STRDBG(datareq->ndi.octet)));
	} else {
		WL_ERR(("Invalid NDI addr retrieved\n"));
		ret = BCME_BADARG;
		goto fail;
	}

	datareq->ndl_qos.min_slots = NAN_NDL_QOS_MIN_SLOT_NO_PREF;
	datareq->ndl_qos.max_latency = NAN_NDL_QOS_MAX_LAT_NO_PREF;

	/* Fill the sub_command block */
	sub_cmd->id = htod16(WL_NAN_CMD_DATA_DATAREQ);
	sub_cmd->len = sizeof(sub_cmd->u.options) +
		OFFSETOF(wl_nan_dp_req_t, tlv_params);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);
	pxtlv = (uint8 *)&datareq->tlv_params;

	nan_buf_size -= (sub_cmd->len +
			OFFSETOF(bcm_iov_batch_subcmd_t, u.options));
	buflen_avail = nan_buf_size;

	if (cmd_data->svc_info.data && cmd_data->svc_info.dlen) {
		ret = bcm_pack_xtlv_entry(&pxtlv, &nan_buf_size,
				WL_NAN_XTLV_SD_SVC_INFO, cmd_data->svc_info.dlen,
				cmd_data->svc_info.data,
				BCM_XTLV_OPTION_ALIGN32);
		if (ret != BCME_OK) {
			WL_ERR(("unable to process svc_spec_info: %d\n", ret));
			goto fail;
		}
		/* If NDPE is enabled, duplicating svc_info and sending it as part of NDPE TLV list
		 * too along with SD SVC INFO, as FW is considering both of them as different
		 * entities where as framework is sending both of them in same variable
		 * (cmd_data->svc_info). FW will decide which one to use based on
		 * peer's capability (NDPE capable or not)
		 */
		if (cfg->nancfg->ndpe_enabled) {
			ret = bcm_pack_xtlv_entry(&pxtlv, &nan_buf_size,
					WL_NAN_XTLV_SD_NDPE_TLV_LIST, cmd_data->svc_info.dlen,
					cmd_data->svc_info.data,
					BCM_XTLV_OPTION_ALIGN32);
			if (ret != BCME_OK) {
				WL_ERR(("unable to process NDPE TLV list: %d\n", ret));
				goto fail;
			}
		}
		datareq->flags |= WL_NAN_DP_FLAG_SVC_INFO;
	}

	/* Security elements */

	if (cmd_data->csid) {
		WL_TRACE(("Cipher suite type is present, pack it\n"));
		ret = bcm_pack_xtlv_entry(&pxtlv, &nan_buf_size,
				WL_NAN_XTLV_CFG_SEC_CSID, sizeof(nan_sec_csid_e),
				(uint8*)&cmd_data->csid, BCM_XTLV_OPTION_ALIGN32);
		if (unlikely(ret)) {
			WL_ERR(("%s: fail to pack on csid\n", __FUNCTION__));
			goto fail;
		}
	}

	if (cmd_data->ndp_cfg.security_cfg) {
		if ((cmd_data->key_type == NAN_SECURITY_KEY_INPUT_PMK) ||
			(cmd_data->key_type == NAN_SECURITY_KEY_INPUT_PASSPHRASE)) {
			if (cmd_data->key.data && cmd_data->key.dlen) {
				WL_TRACE(("optional pmk present, pack it\n"));
				ret = bcm_pack_xtlv_entry(&pxtlv, &nan_buf_size,
					WL_NAN_XTLV_CFG_SEC_PMK, cmd_data->key.dlen,
					cmd_data->key.data, BCM_XTLV_OPTION_ALIGN32);
				if (unlikely(ret)) {
					WL_ERR(("%s: fail to pack on WL_NAN_XTLV_CFG_SEC_PMK\n",
						__FUNCTION__));
					goto fail;
				}
			}
		} else {
			WL_ERR(("Invalid security key type\n"));
			ret = BCME_BADARG;
			goto fail;
		}

		if ((cmd_data->svc_hash.dlen == WL_NAN_SVC_HASH_LEN) &&
				(cmd_data->svc_hash.data)) {
			WL_TRACE(("svc hash present, pack it\n"));
			ret = bcm_pack_xtlv_entry(&pxtlv, &nan_buf_size,
					WL_NAN_XTLV_CFG_SVC_HASH, WL_NAN_SVC_HASH_LEN,
					cmd_data->svc_hash.data, BCM_XTLV_OPTION_ALIGN32);
			if (ret != BCME_OK) {
				WL_ERR(("%s: fail to pack WL_NAN_XTLV_CFG_SVC_HASH\n",
						__FUNCTION__));
				goto fail;
			}
		} else {
#ifdef WL_NAN_DISC_CACHE
			/* check in cache */
			nan_disc_result_cache *cache;
			cache = wl_cfgnan_get_disc_result(cfg,
				datareq->pub_id, &datareq->peer_mac);
			if (!cache) {
				ret = BCME_ERROR;
				WL_ERR(("invalid svc hash data or length = %d\n",
					cmd_data->svc_hash.dlen));
				goto fail;
			}
			WL_TRACE(("svc hash present, pack it\n"));
			ret = bcm_pack_xtlv_entry(&pxtlv, &nan_buf_size,
					WL_NAN_XTLV_CFG_SVC_HASH, WL_NAN_SVC_HASH_LEN,
					cache->svc_hash, BCM_XTLV_OPTION_ALIGN32);
			if (ret != BCME_OK) {
				WL_ERR(("%s: fail to pack WL_NAN_XTLV_CFG_SVC_HASH\n",
						__FUNCTION__));
				goto fail;
			}
#else
			ret = BCME_ERROR;
			WL_ERR(("invalid svc hash data or length = %d\n",
					cmd_data->svc_hash.dlen));
			goto fail;
#endif /* WL_NAN_DISC_CACHE */
		}
		/* If the Data req is for secure data connection */
		datareq->flags |= WL_NAN_DP_FLAG_SECURITY;
	}

	sub_cmd->len += (buflen_avail - nan_buf_size);
	nan_buf->is_set = false;
	nan_buf->count++;

	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, data_size,
			&(cmd_data->status), resp_buf, data_size + NAN_IOVAR_NAME_SIZE);
	if (unlikely(ret) || unlikely(cmd_data->status)) {
		WL_ERR(("nan data path request handler failed, ret = %d,"
			" status %d, peer: " MACDBG "\n",
			ret, cmd_data->status, MAC2STRDBG(&(cmd_data->mac_addr))));
		goto fail;
	}

	/* check the response buff */
	if (ret == BCME_OK) {
		ret = process_resp_buf(resp_buf + WL_NAN_OBUF_DATA_OFFSET,
				ndp_instance_id, WL_NAN_CMD_DATA_DATAREQ);
		cmd_data->ndp_instance_id = *ndp_instance_id;
	}
	WL_INFORM_MEM(("[NAN] DP request successfull (ndp_id:%d), peer: " MACDBG " \n",
		cmd_data->ndp_instance_id, MAC2STRDBG(&cmd_data->mac_addr)));
	/* Add peer to data ndp peer list */
	wl_cfgnan_data_add_peer(cfg, &datareq->peer_mac);

fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, data_size);
	}

	if (resp_buf) {
		MFREE(cfg->osh, resp_buf, data_size + NAN_IOVAR_NAME_SIZE);
	}
	NAN_MUTEX_UNLOCK();
	mutex_unlock(&cfg->if_sync);
	NAN_DBG_EXIT();
	return ret;
}

int
wl_cfgnan_data_path_response_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_datapath_cmd_data_t *cmd_data)
{
	s32 ret = BCME_OK;
	bcm_iov_batch_buf_t *nan_buf = NULL;
	wl_nan_dp_resp_t *dataresp = NULL;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	uint16 buflen_avail;
	uint8 *pxtlv;
	struct wireless_dev *wdev;
	uint16 nan_buf_size;
	uint8 *resp_buf = NULL;

	/* Considering fixed params */
	uint16 data_size = WL_NAN_OBUF_DATA_OFFSET +
		OFFSETOF(wl_nan_dp_resp_t, tlv_params);
	data_size = ALIGN_SIZE(data_size, 4);
	ret = wl_cfgnan_aligned_data_size_of_opt_dp_params(cfg, &data_size, cmd_data);
	if (unlikely(ret)) {
		WL_ERR(("Failed to get alligned size of optional params\n"));
		goto fail;
	}
	nan_buf_size = data_size;

	NAN_DBG_ENTER();

	mutex_lock(&cfg->if_sync);
	NAN_MUTEX_LOCK();
#ifdef WL_IFACE_MGMT
	if ((ret = wl_cfg80211_handle_if_role_conflict(cfg, WL_IF_TYPE_NAN)) < 0) {
		WL_ERR(("Conflicting iface found to be active\n"));
		ret = BCME_UNSUPPORTED;
		goto fail;
	}
#endif /* WL_IFACE_MGMT */

	nan_buf = MALLOCZ(cfg->osh, data_size);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	resp_buf = MALLOCZ(cfg->osh, data_size + NAN_IOVAR_NAME_SIZE);
	if (!resp_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	ret = wl_cfgnan_set_nan_avail(bcmcfg_to_prmry_ndev(cfg),
			cfg, &cmd_data->avail_params, WL_AVAIL_LOCAL);
	if (unlikely(ret)) {
		WL_ERR(("Failed to set avail value with type local\n"));
		goto fail;
	}

	ret = wl_cfgnan_set_nan_avail(bcmcfg_to_prmry_ndev(cfg),
			cfg, &cmd_data->avail_params, WL_AVAIL_NDC);
	if (unlikely(ret)) {
		WL_ERR(("Failed to set avail value with type ndc\n"));
		goto fail;
	}

	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_buf_size -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);

	sub_cmd = (bcm_iov_batch_subcmd_t*)(&nan_buf->cmds[0]);
	dataresp = (wl_nan_dp_resp_t *)(sub_cmd->data);

	/* Setting default data path type to unicast */
	dataresp->type = WL_NAN_DP_TYPE_UNICAST;
	/* Changing status value as per fw convention */
	dataresp->status = cmd_data->rsp_code ^= 1;
	dataresp->reason_code = 0;

	/* ndp instance id must be from 1 to 255, 0 is reserved */
	if (cmd_data->ndp_instance_id < NAN_ID_MIN ||
			cmd_data->ndp_instance_id > NAN_ID_MAX) {
		WL_ERR(("Invalid ndp instance id: %d\n", cmd_data->ndp_instance_id));
		ret = BCME_BADARG;
		goto fail;
	}
	dataresp->ndp_id = cmd_data->ndp_instance_id;

	/* Retrieved initiator ndi from NanDataPathRequestInd */
	if (!ETHER_ISNULLADDR(&cfg->nancfg->initiator_ndi.octet)) {
		ret = memcpy_s(&dataresp->mac_addr, ETHER_ADDR_LEN,
				&cfg->nancfg->initiator_ndi, ETHER_ADDR_LEN);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy initiator ndi\n"));
			goto fail;
		}
	} else {
		WL_ERR(("Invalid ether addr retrieved\n"));
		ret = BCME_BADARG;
		goto fail;
	}

	/* Interface is not mandatory, when it is a reject from framework */
	if (dataresp->status != WL_NAN_DP_STATUS_REJECTED) {
#ifdef RTT_SUPPORT
		/* cancel any ongoing RTT session with peer
		* as we donot support DP and RNG to same peer
		*/
		wl_cfgnan_handle_dp_ranging_concurrency(cfg, &cmd_data->mac_addr,
			RTT_GEO_SUSPN_HOST_NDP_TRIGGER);
#endif /* RTT_SUPPORT */
		/* Retrieve mac from given iface name */
		wdev = wl_cfg80211_get_wdev_from_ifname(cfg,
				(char *)cmd_data->ndp_iface);
		if (!wdev || ETHER_ISNULLADDR(wdev->netdev->dev_addr)) {
			ret = -EINVAL;
			WL_ERR(("Failed to retrieve wdev/dev addr for ndp_iface = %s\n",
				(char *)cmd_data->ndp_iface));
			goto fail;
		}

		if (!ETHER_ISNULLADDR(wdev->netdev->dev_addr)) {
			ret = memcpy_s(&dataresp->ndi, ETHER_ADDR_LEN,
					wdev->netdev->dev_addr, ETHER_ADDR_LEN);
			if (ret != BCME_OK) {
				WL_ERR(("Failed to copy responder ndi\n"));
				goto fail;
			}
			WL_TRACE(("%s: Retrieved ndi mac " MACDBG "\n",
					__FUNCTION__, MAC2STRDBG(dataresp->ndi.octet)));
		} else {
			WL_ERR(("Invalid NDI addr retrieved\n"));
			ret = BCME_BADARG;
			goto fail;
		}
	}

	dataresp->ndl_qos.min_slots = NAN_NDL_QOS_MIN_SLOT_NO_PREF;
	dataresp->ndl_qos.max_latency = NAN_NDL_QOS_MAX_LAT_NO_PREF;

	/* Fill the sub_command block */
	sub_cmd->id = htod16(WL_NAN_CMD_DATA_DATARESP);
	sub_cmd->len = sizeof(sub_cmd->u.options) +
		OFFSETOF(wl_nan_dp_resp_t, tlv_params);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);
	pxtlv = (uint8 *)&dataresp->tlv_params;

	nan_buf_size -= (sub_cmd->len +
			OFFSETOF(bcm_iov_batch_subcmd_t, u.options));
	buflen_avail = nan_buf_size;

	if (cmd_data->svc_info.data && cmd_data->svc_info.dlen) {
		ret = bcm_pack_xtlv_entry(&pxtlv, &nan_buf_size,
				WL_NAN_XTLV_SD_SVC_INFO, cmd_data->svc_info.dlen,
				cmd_data->svc_info.data,
				BCM_XTLV_OPTION_ALIGN32);
		if (ret != BCME_OK) {
			WL_ERR(("unable to process svc_spec_info: %d\n", ret));
			goto fail;
		}
		/* If NDPE is enabled, duplicating svc_info and sending it as part of NDPE TLV list
		 * too along with SD SVC INFO, as FW is considering both of them as different
		 * entities where as framework is sending both of them in same variable
		 * (cmd_data->svc_info). FW will decide which one to use based on
		 * peer's capability (NDPE capable or not)
		 */
		if (cfg->nancfg->ndpe_enabled) {
			ret = bcm_pack_xtlv_entry(&pxtlv, &nan_buf_size,
					WL_NAN_XTLV_SD_NDPE_TLV_LIST, cmd_data->svc_info.dlen,
					cmd_data->svc_info.data,
					BCM_XTLV_OPTION_ALIGN32);
			if (ret != BCME_OK) {
				WL_ERR(("unable to process NDPE TLV list: %d\n", ret));
				goto fail;
			}
		}
		dataresp->flags |= WL_NAN_DP_FLAG_SVC_INFO;
	}

	/* Security elements */
	if (cmd_data->csid) {
		WL_TRACE(("Cipher suite type is present, pack it\n"));
		ret = bcm_pack_xtlv_entry(&pxtlv, &nan_buf_size,
				WL_NAN_XTLV_CFG_SEC_CSID, sizeof(nan_sec_csid_e),
				(uint8*)&cmd_data->csid, BCM_XTLV_OPTION_ALIGN32);
		if (unlikely(ret)) {
			WL_ERR(("%s: fail to pack csid\n", __FUNCTION__));
			goto fail;
		}
	}

	if (cmd_data->ndp_cfg.security_cfg) {
		if ((cmd_data->key_type == NAN_SECURITY_KEY_INPUT_PMK) ||
			(cmd_data->key_type == NAN_SECURITY_KEY_INPUT_PASSPHRASE)) {
			if (cmd_data->key.data && cmd_data->key.dlen) {
				WL_TRACE(("optional pmk present, pack it\n"));
				ret = bcm_pack_xtlv_entry(&pxtlv, &nan_buf_size,
					WL_NAN_XTLV_CFG_SEC_PMK, cmd_data->key.dlen,
					cmd_data->key.data, BCM_XTLV_OPTION_ALIGN32);
				if (unlikely(ret)) {
					WL_ERR(("%s: fail to pack WL_NAN_XTLV_CFG_SEC_PMK\n",
						__FUNCTION__));
					goto fail;
				}
			}
		} else {
			WL_ERR(("Invalid security key type\n"));
			ret = BCME_BADARG;
			goto fail;
		}

		if ((cmd_data->svc_hash.dlen == WL_NAN_SVC_HASH_LEN) &&
				(cmd_data->svc_hash.data)) {
			WL_TRACE(("svc hash present, pack it\n"));
			ret = bcm_pack_xtlv_entry(&pxtlv, &nan_buf_size,
					WL_NAN_XTLV_CFG_SVC_HASH, WL_NAN_SVC_HASH_LEN,
					cmd_data->svc_hash.data,
					BCM_XTLV_OPTION_ALIGN32);
			if (ret != BCME_OK) {
				WL_ERR(("%s: fail to pack WL_NAN_XTLV_CFG_SVC_HASH\n",
						__FUNCTION__));
				goto fail;
			}
		}
		/* If the Data resp is for secure data connection */
		dataresp->flags |= WL_NAN_DP_FLAG_SECURITY;
	}

	sub_cmd->len += (buflen_avail - nan_buf_size);

	nan_buf->is_set = false;
	nan_buf->count++;
	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, data_size,
			&(cmd_data->status), resp_buf, data_size + NAN_IOVAR_NAME_SIZE);
	if (unlikely(ret) || unlikely(cmd_data->status)) {
		WL_ERR(("nan data path response handler failed, error = %d, status %d\n",
				ret, cmd_data->status));
		goto fail;
	}

	WL_INFORM_MEM(("[NAN] DP response successfull (ndp_id:%d)\n", dataresp->ndp_id));

fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, data_size);
	}

	if (resp_buf) {
		MFREE(cfg->osh, resp_buf, data_size + NAN_IOVAR_NAME_SIZE);
	}
	NAN_MUTEX_UNLOCK();
	mutex_unlock(&cfg->if_sync);

	NAN_DBG_EXIT();
	return ret;
}

int wl_cfgnan_data_path_end_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_data_path_id ndp_instance_id,
	int *status)
{
	bcm_iov_batch_buf_t *nan_buf = NULL;
	wl_nan_dp_end_t *dataend = NULL;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	s32 ret = BCME_OK;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE];

	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(ndev);

	NAN_DBG_ENTER();
	NAN_MUTEX_LOCK();

	if (!dhdp->up) {
		WL_ERR(("bus is already down, hence blocking nan dp end\n"));
		ret = BCME_OK;
		goto fail;
	}

	if (!cfg->nancfg->nan_enable) {
		WL_ERR(("nan is not enabled, nan dp end blocked\n"));
		ret = BCME_OK;
		goto fail;
	}

	/* ndp instance id must be from 1 to 255, 0 is reserved */
	if (ndp_instance_id < NAN_ID_MIN ||
		ndp_instance_id > NAN_ID_MAX) {
		WL_ERR(("Invalid ndp instance id: %d\n", ndp_instance_id));
		ret = BCME_BADARG;
		goto fail;
	}

	nan_buf = MALLOCZ(cfg->osh, nan_buf_size);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_buf_size -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);

	sub_cmd = (bcm_iov_batch_subcmd_t*)(&nan_buf->cmds[0]);
	dataend = (wl_nan_dp_end_t *)(sub_cmd->data);

	/* Fill sub_cmd block */
	sub_cmd->id = htod16(WL_NAN_CMD_DATA_DATAEND);
	sub_cmd->len = sizeof(sub_cmd->u.options) +
		sizeof(*dataend);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);

	dataend->lndp_id = ndp_instance_id;

	/*
	 * Currently fw requires ndp_id and reason to end the data path
	 * But wifi_nan.h takes ndp_instances_count and ndp_id.
	 * Will keep reason = accept always.
	 */

	dataend->status = 1;

	nan_buf->is_set = true;
	nan_buf->count++;

	nan_buf_size -= (sub_cmd->len +
		OFFSETOF(bcm_iov_batch_subcmd_t, u.options));
	bzero(resp_buf, sizeof(resp_buf));
	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, nan_buf_size,
			status, (void*)resp_buf, NAN_IOCTL_BUF_SIZE);
	if (unlikely(ret) || unlikely(*status)) {
		WL_ERR(("nan data path end handler failed, error = %d status %d\n",
			ret, *status));
		goto fail;
	}
	WL_INFORM_MEM(("[NAN] DP end successfull (ndp_id:%d)\n",
		dataend->lndp_id));
fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, NAN_IOCTL_BUF_SIZE);
	}

	NAN_MUTEX_UNLOCK();
	NAN_DBG_EXIT();
	return ret;
}

#ifdef WL_NAN_DISC_CACHE
int wl_cfgnan_sec_info_handler(struct bcm_cfg80211 *cfg,
		nan_datapath_sec_info_cmd_data_t *cmd_data, nan_hal_resp_t *nan_req_resp)
{
	s32 ret = BCME_NOTFOUND;
	/* check in cache */
	nan_disc_result_cache *disc_cache = NULL;
	nan_svc_info_t *svc_info = NULL;

	NAN_DBG_ENTER();
	NAN_MUTEX_LOCK();

	if (!cfg->nancfg->nan_init_state) {
		WL_ERR(("nan is not initialized/nmi doesnt exists\n"));
		ret = BCME_NOTENABLED;
		goto fail;
	}

	/* datapath request context */
	if (cmd_data->pub_id && !ETHER_ISNULLADDR(&cmd_data->mac_addr)) {
		disc_cache = wl_cfgnan_get_disc_result(cfg,
			cmd_data->pub_id, &cmd_data->mac_addr);
		WL_DBG(("datapath request: PUB ID: = %d\n",
			cmd_data->pub_id));
		if (disc_cache) {
			(void)memcpy_s(nan_req_resp->svc_hash, WL_NAN_SVC_HASH_LEN,
					disc_cache->svc_hash, WL_NAN_SVC_HASH_LEN);
			ret = BCME_OK;
		} else {
			WL_ERR(("disc_cache is NULL\n"));
			goto fail;
		}
	}

	/* datapath response context */
	if (cmd_data->ndp_instance_id) {
		WL_DBG(("datapath response: NDP ID: = %d\n",
			cmd_data->ndp_instance_id));
		svc_info = wl_cfgnan_get_svc_inst(cfg, 0, cmd_data->ndp_instance_id);
		/* Note: svc_info will not be present in OOB cases
		* In such case send NMI alone and let HAL handle if
		* svc_hash is mandatory
		*/
		if (svc_info) {
			WL_DBG(("svc hash present, pack it\n"));
			(void)memcpy_s(nan_req_resp->svc_hash, WL_NAN_SVC_HASH_LEN,
					svc_info->svc_hash, WL_NAN_SVC_HASH_LEN);
		} else {
			WL_INFORM_MEM(("svc_info not present..assuming OOB DP\n"));
		}
		/* Always send NMI */
		(void)memcpy_s(nan_req_resp->pub_nmi, ETHER_ADDR_LEN,
				cfg->nancfg->nan_nmi_mac, ETHER_ADDR_LEN);
		ret = BCME_OK;
	}
fail:
	NAN_MUTEX_UNLOCK();
	NAN_DBG_EXIT();
	return ret;
}
#endif /* WL_NAN_DISC_CACHE */

#ifdef RTT_SUPPORT
static s32 wl_nan_cache_to_event_data(nan_disc_result_cache *cache,
	nan_event_data_t *nan_event_data, osl_t *osh)
{
	s32 ret = BCME_OK;
	NAN_DBG_ENTER();

	nan_event_data->pub_id = cache->pub_id;
	nan_event_data->sub_id = cache->sub_id;
	nan_event_data->publish_rssi = cache->publish_rssi;
	nan_event_data->peer_cipher_suite = cache->peer_cipher_suite;
	ret = memcpy_s(&nan_event_data->remote_nmi, ETHER_ADDR_LEN,
			&cache->peer, ETHER_ADDR_LEN);
	if (ret != BCME_OK) {
		WL_ERR(("Failed to copy cached peer nan nmi\n"));
		goto fail;
	}

	if (cache->svc_info.dlen && cache->svc_info.data) {
		nan_event_data->svc_info.dlen = cache->svc_info.dlen;
		nan_event_data->svc_info.data =
			MALLOCZ(osh, nan_event_data->svc_info.dlen);
		if (!nan_event_data->svc_info.data) {
			WL_ERR(("%s: memory allocation failed\n", __FUNCTION__));
			nan_event_data->svc_info.dlen = 0;
			ret = -ENOMEM;
			goto fail;
		}
		ret = memcpy_s(nan_event_data->svc_info.data, nan_event_data->svc_info.dlen,
			cache->svc_info.data, cache->svc_info.dlen);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy cached svc info data\n"));
			goto fail;
		}
	}
	if (cache->tx_match_filter.dlen && cache->tx_match_filter.data) {
		nan_event_data->tx_match_filter.dlen = cache->tx_match_filter.dlen;
		nan_event_data->tx_match_filter.data =
			MALLOCZ(osh, nan_event_data->tx_match_filter.dlen);
		if (!nan_event_data->tx_match_filter.data) {
			WL_ERR(("%s: memory allocation failed\n", __FUNCTION__));
			nan_event_data->tx_match_filter.dlen = 0;
			ret = -ENOMEM;
			goto fail;
		}
		ret = memcpy_s(nan_event_data->tx_match_filter.data,
				nan_event_data->tx_match_filter.dlen,
				cache->tx_match_filter.data, cache->tx_match_filter.dlen);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy cached tx match filter data\n"));
			goto fail;
		}
	}
fail:
	NAN_DBG_EXIT();
	return ret;
}

/*
 * API to cancel the ranging for given instance
 * For geofence initiator, suspend ranging.
 * for directed RTT initiator , report fail result, cancel ranging
 * and clear ranging instance
 * For responder, cancel ranging and clear ranging instance
 */
static s32
wl_cfgnan_clear_peer_ranging(struct bcm_cfg80211 *cfg,
		nan_ranging_inst_t *rng_inst, int reason)
{
	uint32 status = 0;
	int err = BCME_OK;
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);

	if (rng_inst->range_type == RTT_TYPE_NAN_GEOFENCE &&
		rng_inst->range_role == NAN_RANGING_ROLE_INITIATOR) {
		err = wl_cfgnan_suspend_geofence_rng_session(ndev,
				&rng_inst->peer_addr, reason, 0);
	} else {
		if (rng_inst->range_type == RTT_TYPE_NAN_DIRECTED) {
			dhd_rtt_handle_nan_rtt_session_end(dhdp,
				&rng_inst->peer_addr);
		}
		/* responder */
		err = wl_cfgnan_cancel_ranging(ndev, cfg,
			&rng_inst->range_id,
			NAN_RNG_TERM_FLAG_IMMEDIATE, &status);
		wl_cfgnan_reset_remove_ranging_instance(cfg, rng_inst);
	}

	if (err) {
		WL_ERR(("Failed to stop ranging with peer, err : %d\n", err));
	}

	return err;
}

/*
 * Handle NDP-Ranging Concurrency,
 * for incoming DP Reuest
 * Cancel Ranging with same peer
 * Cancel Ranging for set up in prog
 * for all other peers
 */
static s32
wl_cfgnan_handle_dp_ranging_concurrency(struct bcm_cfg80211 *cfg,
		struct ether_addr *peer, int reason)
{
	uint8 i = 0;
	nan_ranging_inst_t *cur_rng_inst = NULL;
	nan_ranging_inst_t *rng_inst = NULL;
	int err = BCME_OK;

	/*
	 * FixMe:
	 * DP Ranging Concurrency will need more
	 * than what has been addressed till now
	 * Poll max rng sessions and update it
	 * take relevant actions accordingly
	 */

	cur_rng_inst = wl_cfgnan_check_for_ranging(cfg, peer);

	for (i = 0; i < NAN_MAX_RANGING_INST; i++) {
		rng_inst = &cfg->nancfg->nan_ranging_info[i];
		if (rng_inst->in_use) {
			if ((cur_rng_inst && cur_rng_inst == rng_inst) &&
				NAN_RANGING_IS_IN_PROG(rng_inst->range_status)) {
				err = wl_cfgnan_clear_peer_ranging(cfg, rng_inst,
						RTT_GEO_SUSPN_HOST_NDP_TRIGGER);
			}
		}
	}

	if (err) {
		WL_ERR(("Failed to handle dp ranging concurrency, err : %d\n", err));
	}

	return err;
}

bool
wl_cfgnan_check_role_concurrency(struct bcm_cfg80211 *cfg,
	struct ether_addr *peer_addr)
{
	nan_ranging_inst_t *rng_inst = NULL;
	bool role_conc_status = FALSE;

	rng_inst = wl_cfgnan_check_for_ranging(cfg, peer_addr);
	if (rng_inst) {
		role_conc_status = rng_inst->role_concurrency_status;
	}

	return role_conc_status;
}
#endif /* RTT_SUPPORT */

static s32
wl_nan_dp_cmn_event_data(struct bcm_cfg80211 *cfg, void *event_data,
		uint16 data_len, uint16 *tlvs_offset,
		uint16 *nan_opts_len, uint32 event_num,
		int *hal_event_id, nan_event_data_t *nan_event_data)
{
	s32 ret = BCME_OK;
	uint8 i;
	wl_nan_ev_datapath_cmn_t *ev_dp;
	nan_svc_info_t *svc_info;
	bcm_xtlv_t *xtlv = (bcm_xtlv_t *)event_data;
#ifdef RTT_SUPPORT
	nan_ranging_inst_t *rng_inst = NULL;
#endif /* RTT_SUPPORT */

	if (xtlv->id == WL_NAN_XTLV_DATA_DP_INFO) {
		ev_dp = (wl_nan_ev_datapath_cmn_t *)xtlv->data;
		NAN_DBG_ENTER();

		BCM_REFERENCE(svc_info);
		BCM_REFERENCE(i);
		/* Mapping to common struct between DHD and HAL */
		WL_TRACE(("Event type: %d\n", ev_dp->type));
		nan_event_data->type = ev_dp->type;
		WL_TRACE(("pub_id: %d\n", ev_dp->pub_id));
		nan_event_data->pub_id = ev_dp->pub_id;
		WL_TRACE(("security: %d\n", ev_dp->security));
		nan_event_data->security = ev_dp->security;

		/* Store initiator_ndi, required for data_path_response_request */
		ret = memcpy_s(&cfg->nancfg->initiator_ndi, ETHER_ADDR_LEN,
				&ev_dp->initiator_ndi, ETHER_ADDR_LEN);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy event's initiator addr\n"));
			goto fail;
		}
		if (ev_dp->type == NAN_DP_SESSION_UNICAST) {
			WL_INFORM_MEM(("NDP ID: %d\n", ev_dp->ndp_id));
			nan_event_data->ndp_id = ev_dp->ndp_id;
			WL_TRACE(("INITIATOR_NDI: " MACDBG "\n",
					MAC2STRDBG(ev_dp->initiator_ndi.octet)));
			WL_TRACE(("RESPONDOR_NDI: " MACDBG "\n",
					MAC2STRDBG(ev_dp->responder_ndi.octet)));
			WL_TRACE(("PEER NMI: " MACDBG "\n",
					MAC2STRDBG(ev_dp->peer_nmi.octet)));
			ret = memcpy_s(&nan_event_data->remote_nmi, ETHER_ADDR_LEN,
					&ev_dp->peer_nmi, ETHER_ADDR_LEN);
			if (ret != BCME_OK) {
				WL_ERR(("Failed to copy event's peer nmi\n"));
				goto fail;
			}
		} else {
			/* type is multicast */
			WL_INFORM_MEM(("NDP ID: %d\n", ev_dp->mc_id));
			nan_event_data->ndp_id = ev_dp->mc_id;
			WL_TRACE(("PEER NMI: " MACDBG "\n",
					MAC2STRDBG(ev_dp->peer_nmi.octet)));
			ret = memcpy_s(&nan_event_data->remote_nmi, ETHER_ADDR_LEN,
					&ev_dp->peer_nmi,
					ETHER_ADDR_LEN);
			if (ret != BCME_OK) {
				WL_ERR(("Failed to copy event's peer nmi\n"));
				goto fail;
			}
		}
		*tlvs_offset = OFFSETOF(wl_nan_ev_datapath_cmn_t, opt_tlvs) +
			OFFSETOF(bcm_xtlv_t, data);
		*nan_opts_len = data_len - *tlvs_offset;
		if (event_num == WL_NAN_EVENT_PEER_DATAPATH_IND) {
			*hal_event_id = GOOGLE_NAN_EVENT_DATA_REQUEST;
#ifdef WL_NAN_DISC_CACHE
			ret = wl_cfgnan_svc_inst_add_ndp(cfg, nan_event_data->pub_id,
					nan_event_data->ndp_id);
			if (ret != BCME_OK) {
				goto fail;
			}
#endif /* WL_NAN_DISC_CACHE */
			/* Add peer to data ndp peer list */
			wl_cfgnan_data_add_peer(cfg, &ev_dp->peer_nmi);
#ifdef RTT_SUPPORT
			/* cancel any ongoing RTT session with peer
			 * as we donot support DP and RNG to same peer
			 */
			wl_cfgnan_handle_dp_ranging_concurrency(cfg, &ev_dp->peer_nmi,
					RTT_GEO_SUSPN_PEER_NDP_TRIGGER);
#endif /* RTT_SUPPORT */
		} else if (event_num == WL_NAN_EVENT_DATAPATH_ESTB) {
			*hal_event_id = GOOGLE_NAN_EVENT_DATA_CONFIRMATION;
			if (ev_dp->role == NAN_DP_ROLE_INITIATOR) {
				ret = memcpy_s(&nan_event_data->responder_ndi, ETHER_ADDR_LEN,
						&ev_dp->responder_ndi,
						ETHER_ADDR_LEN);
				if (ret != BCME_OK) {
					WL_ERR(("Failed to copy event's responder ndi\n"));
					goto fail;
				}
				WL_TRACE(("REMOTE_NDI: " MACDBG "\n",
						MAC2STRDBG(ev_dp->responder_ndi.octet)));
				WL_TRACE(("Initiator status %d\n", nan_event_data->status));
			} else {
				ret = memcpy_s(&nan_event_data->responder_ndi, ETHER_ADDR_LEN,
						&ev_dp->initiator_ndi,
						ETHER_ADDR_LEN);
				if (ret != BCME_OK) {
					WL_ERR(("Failed to copy event's responder ndi\n"));
					goto fail;
				}
				WL_TRACE(("REMOTE_NDI: " MACDBG "\n",
						MAC2STRDBG(ev_dp->initiator_ndi.octet)));
			}
			if (ev_dp->status == NAN_NDP_STATUS_ACCEPT) {
				nan_event_data->status = NAN_DP_REQUEST_ACCEPT;
				wl_cfgnan_data_set_peer_dp_state(cfg, &ev_dp->peer_nmi,
					NAN_PEER_DP_CONNECTED);
				wl_cfgnan_update_dp_info(cfg, true, nan_event_data->ndp_id);
				wl_cfgnan_get_stats(cfg);
			} else if (ev_dp->status == NAN_NDP_STATUS_REJECT) {
				nan_event_data->status = NAN_DP_REQUEST_REJECT;
#ifdef WL_NAN_DISC_CACHE
				if (ev_dp->role != NAN_DP_ROLE_INITIATOR) {
					/* Only at Responder side,
					 * If dp is ended,
					 * clear the resp ndp id from the svc info cache
					 */
					ret = wl_cfgnan_svc_inst_del_ndp(cfg,
							nan_event_data->pub_id,
							nan_event_data->ndp_id);
					if (ret != BCME_OK) {
						goto fail;
					}
				}
#endif /* WL_NAN_DISC_CACHE */
				/* Remove peer from data ndp peer list */
				wl_cfgnan_data_remove_peer(cfg, &ev_dp->peer_nmi);
#ifdef RTT_SUPPORT
				rng_inst = wl_cfgnan_check_for_ranging(cfg, &ev_dp->peer_nmi);
				if (rng_inst) {
					/* Trigger/Reset geofence RTT */
					wl_cfgnan_reset_geofence_ranging(cfg,
						rng_inst, RTT_SCHED_DP_REJECTED, TRUE);
				}
#endif /* RTT_SUPPORT */
			} else {
				WL_ERR(("%s:Status code = %x not expected\n",
						__FUNCTION__, ev_dp->status));
				ret = BCME_ERROR;
				goto fail;
			}
			WL_TRACE(("Responder status %d\n", nan_event_data->status));
		} else if (event_num == WL_NAN_EVENT_DATAPATH_END) {
			/* Mapping to common struct between DHD and HAL */
			*hal_event_id = GOOGLE_NAN_EVENT_DATA_END;
#ifdef WL_NAN_DISC_CACHE
			if (ev_dp->role != NAN_DP_ROLE_INITIATOR) {
				/* Only at Responder side,
				 * If dp is ended,
				 * clear the resp ndp id from the svc info cache
				 */
				ret = wl_cfgnan_svc_inst_del_ndp(cfg,
						nan_event_data->pub_id,
						nan_event_data->ndp_id);
				if (ret != BCME_OK) {
					goto fail;
				}
			}
#endif /* WL_NAN_DISC_CACHE */
			/* Remove peer from data ndp peer list */
			wl_cfgnan_data_remove_peer(cfg, &ev_dp->peer_nmi);
			wl_cfgnan_update_dp_info(cfg, false, nan_event_data->ndp_id);
			WL_INFORM_MEM(("DP_END for REMOTE_NMI: " MACDBG " with %s\n",
				MAC2STRDBG(&ev_dp->peer_nmi),
				nan_event_cause_to_str(ev_dp->event_cause)));
#ifdef RTT_SUPPORT
			rng_inst = wl_cfgnan_check_for_ranging(cfg, &ev_dp->peer_nmi);
			if (rng_inst) {
				/* Trigger/Reset geofence RTT */
				WL_INFORM_MEM(("sched geofence rtt from DP_END ctx: " MACDBG "\n",
						MAC2STRDBG(&rng_inst->peer_addr)));
				wl_cfgnan_reset_geofence_ranging(cfg, rng_inst,
					RTT_SCHED_DP_END, TRUE);
			}
#endif /* RTT_SUPPORT */
		}
	} else {
		/* Follow though, not handling other IDs as of now */
		WL_DBG(("%s:ID = 0x%02x not supported\n", __FUNCTION__, xtlv->id));
	}
fail:
	NAN_DBG_EXIT();
	return ret;
}

#ifdef RTT_SUPPORT
static int
wl_cfgnan_event_disc_result(struct bcm_cfg80211 *cfg,
		nan_event_data_t *nan_event_data)
{
	int ret = BCME_OK;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 13, 0)) || defined(WL_VENDOR_EXT_SUPPORT)
	ret = wl_cfgvendor_send_nan_event(cfg->wdev->wiphy, bcmcfg_to_prmry_ndev(cfg),
		GOOGLE_NAN_EVENT_SUBSCRIBE_MATCH, nan_event_data);
	if (ret != BCME_OK) {
		WL_ERR(("Failed to send event to nan hal\n"));
	}
#endif /* (LINUX_VERSION_CODE > KERNEL_VERSION(3, 13, 0)) || defined(WL_VENDOR_EXT_SUPPORT) */
	return ret;
}

#define IN_GEOFENCE(ingress, egress, distance) (((distance) <= (ingress)) && \
	((distance) >= (egress)))
#define IS_INGRESS_VAL(ingress, distance) ((distance) < (ingress))
#define IS_EGRESS_VAL(egress, distance) ((distance) > (egress))

static bool
wl_cfgnan_check_ranging_cond(nan_svc_info_t *svc_info, uint32 distance,
	uint8 *ranging_ind, uint32 prev_distance)
{
	uint8 svc_ind = svc_info->ranging_ind;
	bool notify = FALSE;
	bool range_rep_ev_once =
		!!(svc_info->svc_range_status & SVC_RANGE_REP_EVENT_ONCE);
	uint32 ingress_limit = svc_info->ingress_limit;
	uint32 egress_limit = svc_info->egress_limit;

	if (svc_ind & NAN_RANGE_INDICATION_CONT) {
		*ranging_ind = NAN_RANGE_INDICATION_CONT;
		notify = TRUE;
		WL_ERR(("\n%s :Svc has continous Ind %d\n",
				__FUNCTION__, __LINE__));
		goto done;
	}

	if (svc_ind == (NAN_RANGE_INDICATION_INGRESS |
		NAN_RANGE_INDICATION_EGRESS)) {
		if (IN_GEOFENCE(ingress_limit, egress_limit, distance)) {
			/* if not already in geofence */
			if ((range_rep_ev_once == FALSE) ||
				(!IN_GEOFENCE(ingress_limit, egress_limit,
				prev_distance))) {
				notify = TRUE;
				if (distance > prev_distance) {
					*ranging_ind = NAN_RANGE_INDICATION_EGRESS;
				} else {
					*ranging_ind = NAN_RANGE_INDICATION_INGRESS;
				}
				WL_ERR(("\n%s :Svc has geofence Ind %d res_ind %d\n",
					__FUNCTION__, __LINE__, *ranging_ind));
			}
		}
		goto done;
	}

	if (svc_ind == NAN_RANGE_INDICATION_INGRESS) {
		if (IS_INGRESS_VAL(ingress_limit, distance)) {
			if ((range_rep_ev_once == FALSE) ||
				(prev_distance == INVALID_DISTANCE) ||
				!IS_INGRESS_VAL(ingress_limit, prev_distance)) {
				notify = TRUE;
				*ranging_ind = NAN_RANGE_INDICATION_INGRESS;
				WL_ERR(("\n%s :Svc has ingress Ind %d\n",
					__FUNCTION__, __LINE__));
			}
		}
		goto done;
	}

	if (svc_ind == NAN_RANGE_INDICATION_EGRESS) {
		if (IS_EGRESS_VAL(egress_limit, distance)) {
			if ((range_rep_ev_once == FALSE) ||
				(prev_distance == INVALID_DISTANCE) ||
				!IS_EGRESS_VAL(egress_limit, prev_distance)) {
				notify = TRUE;
				*ranging_ind = NAN_RANGE_INDICATION_EGRESS;
				WL_ERR(("\n%s :Svc has egress Ind %d\n",
					__FUNCTION__, __LINE__));
			}
		}
		goto done;
	}
done:
	WL_INFORM_MEM(("SVC ranging Ind %d distance %d prev_distance %d, "
		"range_rep_ev_once %d ingress_limit %d egress_limit %d notify %d\n",
		svc_ind, distance, prev_distance, range_rep_ev_once,
		ingress_limit, egress_limit, notify));
	svc_info->svc_range_status |= SVC_RANGE_REP_EVENT_ONCE;
	return notify;
}

static int32
wl_cfgnan_notify_disc_with_ranging(struct bcm_cfg80211 *cfg,
	nan_ranging_inst_t *rng_inst, nan_event_data_t *nan_event_data, uint32 distance)
{
	nan_svc_info_t *svc_info;
	bool notify_svc = TRUE;
	nan_disc_result_cache *disc_res = cfg->nancfg->nan_disc_cache;
	uint8 ranging_ind = 0;
	int ret = BCME_OK;
	int i = 0, j = 0;
	uint8 result_present = nan_event_data->ranging_result_present;

	for (i = 0; i < MAX_SUBSCRIBES; i++) {
		svc_info = rng_inst->svc_idx[i];
		if (svc_info && svc_info->ranging_required) {
			/* if ranging_result is present notify disc result if
			* result satisfies the conditions.
			* if ranging_result is not present, then notify disc
			* result with out ranging info.
			*/
			if (result_present) {
				notify_svc = wl_cfgnan_check_ranging_cond(svc_info, distance,
					&ranging_ind, rng_inst->prev_distance_mm);
				nan_event_data->ranging_ind = ranging_ind;
			}
			WL_DBG(("Ranging notify for svc_id %d, notify %d and ind %d"
				" distance_mm %d result_present %d\n", svc_info->svc_id, notify_svc,
				ranging_ind, distance, result_present));
		} else {
			continue;
		}
		if (notify_svc) {
			for (j = 0; j < NAN_MAX_CACHE_DISC_RESULT; j++) {
				if (!memcmp(&disc_res[j].peer,
					&(rng_inst->peer_addr), ETHER_ADDR_LEN) &&
					(svc_info->svc_id == disc_res[j].sub_id)) {
					ret = wl_nan_cache_to_event_data(&disc_res[j],
						nan_event_data, cfg->osh);
					ret = wl_cfgnan_event_disc_result(cfg, nan_event_data);
					/* If its not match once, clear it as the FW indicates
					 * again.
					 */
					if (!(svc_info->flags & WL_NAN_MATCH_ONCE)) {
						wl_cfgnan_remove_disc_result(cfg, svc_info->svc_id);
					}
				}
			}
		}
	}
	WL_DBG(("notify_disc_with_ranging done ret %d\n", ret));
	return ret;
}

static int32
wl_cfgnan_handle_directed_rtt_report(struct bcm_cfg80211 *cfg,
	nan_ranging_inst_t *rng_inst)
{
	int ret = BCME_OK;
	uint32 status;
	dhd_pub_t *dhd = (struct dhd_pub *)(cfg->pub);

	ret = wl_cfgnan_cancel_ranging(bcmcfg_to_prmry_ndev(cfg), cfg,
			&rng_inst->range_id, NAN_RNG_TERM_FLAG_IMMEDIATE, &status);
	if (unlikely(ret) || unlikely(status)) {
		WL_ERR(("nan range cancel failed ret = %d status = %d\n", ret, status));
	}
	dhd_rtt_handle_nan_rtt_session_end(dhd, &rng_inst->peer_addr);
	dhd_rtt_nan_update_directed_sessions_cnt(dhd, FALSE);

	wl_cfgnan_reset_remove_ranging_instance(cfg, rng_inst);

	WL_DBG(("Ongoing ranging session is cancelled \n"));
	return ret;
}

static void
wl_cfgnan_disc_result_on_geofence_cancel(struct bcm_cfg80211 *cfg,
		nan_ranging_inst_t *rng_inst)
{
	nan_event_data_t *nan_event_data = NULL;

	nan_event_data = MALLOCZ(cfg->osh, sizeof(*nan_event_data));
	if (!nan_event_data) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		goto exit;
	}

	wl_cfgnan_notify_disc_with_ranging(cfg, rng_inst, nan_event_data, 0);

exit:
	wl_cfgnan_clear_nan_event_data(cfg, nan_event_data);

	return;
}

void
wl_cfgnan_process_range_report(struct bcm_cfg80211 *cfg,
		wl_nan_ev_rng_rpt_ind_t *range_res, int status)
{
	nan_ranging_inst_t *rng_inst = NULL;
	nan_event_data_t nan_event_data;
	dhd_pub_t *dhd = (struct dhd_pub *)(cfg->pub);

	UNUSED_PARAMETER(nan_event_data);
	rng_inst = wl_cfgnan_check_for_ranging(cfg, &range_res->peer_m_addr);
	if (!rng_inst) {
		WL_ERR(("No ranging instance but received RNG RPT event..check \n"));
		goto exit;
	}

	if (rng_inst->range_status != NAN_RANGING_SESSION_IN_PROGRESS) {
		WL_ERR(("SSN not in prog but received RNG RPT event..ignore \n"));
		goto exit;
	}

#ifdef NAN_RTT_DBG
	DUMP_NAN_RTT_INST(rng_inst);
	DUMP_NAN_RTT_RPT(range_res);
#endif
	range_res->rng_id = rng_inst->range_id;
	bzero(&nan_event_data, sizeof(nan_event_data));

	if (status == BCME_OK) {
		nan_event_data.ranging_result_present = 1;
		nan_event_data.range_measurement_cm = range_res->dist_mm;
		nan_event_data.ranging_ind = range_res->indication;
	}

	(void)memcpy_s(&nan_event_data.remote_nmi, ETHER_ADDR_LEN,
			&range_res->peer_m_addr, ETHER_ADDR_LEN);

	if (rng_inst->range_type == RTT_TYPE_NAN_GEOFENCE) {
		/* check in cache and event match to host */
		wl_cfgnan_notify_disc_with_ranging(cfg, rng_inst, &nan_event_data,
				range_res->dist_mm);
		rng_inst->prev_distance_mm = range_res->dist_mm;
		/* Reset geof retry count on valid measurement */
		rng_inst->geof_retry_count = 0;
		/*
		 * Suspend and trigger other targets,
		 * if running sessions maxed out and more
		 * pending targets waiting for trigger
		 */
		if (dhd_rtt_geofence_sessions_maxed_out(dhd) &&
			(dhd_rtt_get_geofence_target_cnt(dhd) >=
				dhd_rtt_get_geofence_max_sessions(dhd))) {
			/*
			 * Update the target idx first, before suspending current target
			 * or else current target will become eligible again
			 * and will get scheduled again on reset ranging
			 */
			wl_cfgnan_update_geofence_target_idx(cfg);
			wl_cfgnan_suspend_geofence_rng_session(bcmcfg_to_prmry_ndev(cfg),
				&rng_inst->peer_addr, RTT_GEO_SUSPN_RANGE_RES_REPORTED, 0);
		}
		wl_cfgnan_reset_geofence_ranging(cfg,
			rng_inst, RTT_SCHED_RNG_RPT_GEOFENCE, TRUE);

	} else if (rng_inst->range_type == RTT_TYPE_NAN_DIRECTED) {
		wl_cfgnan_handle_directed_rtt_report(cfg, rng_inst);
	}
	rng_inst->ftm_ssn_retry_count = 0;

exit:
	return;
}
#endif /* RTT_SUPPORT */

static void
wl_nan_print_status(wl_nan_conf_status_t *nstatus)
{
	WL_INFORM_MEM(("> NMI: " MACDBG " Cluster_ID: " MACDBG "\n",
		MAC2STRDBG(nstatus->nmi.octet),
		MAC2STRDBG(nstatus->cid.octet)));

	WL_INFORM_MEM(("> NAN Device Role %s\n", nan_role_to_str(nstatus->role)));
	WL_INFORM_MEM(("> Social channels: %d, %d\n",
		nstatus->social_chans[0], nstatus->social_chans[1]));

	WL_INFORM_MEM(("> Master_rank: " NMRSTR " AMR : " NMRSTR " Hop Count : %d, AMBTT : %d\n",
		NMR2STR(nstatus->mr),
		NMR2STR(nstatus->amr),
		nstatus->hop_count,
		nstatus->ambtt));

	WL_INFORM_MEM(("> Cluster TSF_H: %x , Cluster TSF_L: %x\n",
		nstatus->cluster_tsf_h, nstatus->cluster_tsf_l));
}

static void
wl_cfgnan_clear_nan_event_data(struct bcm_cfg80211 *cfg,
	nan_event_data_t *nan_event_data)
{
	if (nan_event_data) {
		if (nan_event_data->tx_match_filter.data) {
			MFREE(cfg->osh, nan_event_data->tx_match_filter.data,
					nan_event_data->tx_match_filter.dlen);
			nan_event_data->tx_match_filter.data = NULL;
		}
		if (nan_event_data->rx_match_filter.data) {
			MFREE(cfg->osh, nan_event_data->rx_match_filter.data,
					nan_event_data->rx_match_filter.dlen);
			nan_event_data->rx_match_filter.data = NULL;
		}
		if (nan_event_data->svc_info.data) {
			MFREE(cfg->osh, nan_event_data->svc_info.data,
					nan_event_data->svc_info.dlen);
			nan_event_data->svc_info.data = NULL;
		}
		if (nan_event_data->sde_svc_info.data) {
			MFREE(cfg->osh, nan_event_data->sde_svc_info.data,
					nan_event_data->sde_svc_info.dlen);
			nan_event_data->sde_svc_info.data = NULL;
		}
		MFREE(cfg->osh, nan_event_data, sizeof(*nan_event_data));
	}

}

#ifdef RTT_SUPPORT
bool
wl_cfgnan_update_geofence_target_idx(struct bcm_cfg80211 *cfg)
{
	int8 i = 0, target_cnt = 0;
	int8 cur_idx = DHD_RTT_INVALID_TARGET_INDEX;
	rtt_geofence_target_info_t  *geofence_target_info = NULL;
	bool found = false;
	nan_ranging_inst_t *rng_inst = NULL;
	dhd_pub_t *dhd = (struct dhd_pub *)(cfg->pub);
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhd);

	target_cnt = dhd_rtt_get_geofence_target_cnt(dhd);
	ASSERT(target_cnt);
	if (target_cnt == 0) {
		WL_DBG(("No geofence targets to schedule\n"));
		dhd_rtt_set_geofence_cur_target_idx(dhd,
			DHD_RTT_INVALID_TARGET_INDEX);
		goto exit;
	}

	/* cur idx is validated too, in the following API */
	cur_idx = dhd_rtt_get_geofence_cur_target_idx(dhd);
	if (cur_idx == DHD_RTT_INVALID_TARGET_INDEX) {
		WL_DBG(("invalid current target index, start looking from first\n"));
		cur_idx = 0;
	}

	geofence_target_info = rtt_status->geofence_cfg.geofence_target_info;

	/* Loop through to find eligible target idx */
	i = cur_idx;
	do {
		if (geofence_target_info[i].valid == TRUE) {
			rng_inst = wl_cfgnan_check_for_ranging(cfg,
					&geofence_target_info[i].peer_addr);
			if (rng_inst &&
				(!NAN_RANGING_IS_IN_PROG(rng_inst->range_status)) &&
				(!wl_cfgnan_check_role_concurrency(cfg,
					&rng_inst->peer_addr))) {
				found = TRUE;
				break;
			}
		}
		i++;
		if (i == target_cnt) {
			i = 0;
		}
	} while (i != cur_idx);

	if (found) {
		dhd_rtt_set_geofence_cur_target_idx(dhd, i);
		WL_DBG(("Updated cur index, cur_idx = %d, target_cnt = %d\n",
			i, target_cnt));
	} else {
		dhd_rtt_set_geofence_cur_target_idx(dhd,
			DHD_RTT_INVALID_TARGET_INDEX);
		WL_DBG(("Invalidated cur_idx, as either no target present, or all "
			"target already running, target_cnt = %d\n", target_cnt));

	}

exit:
	return found;
}

/*
 * Triggers rtt work thread
 * if set up not in prog already
 * and max sessions not maxed out,
 * after setting next eligible target index
 */
void
wl_cfgnan_reset_geofence_ranging(struct bcm_cfg80211 *cfg,
		nan_ranging_inst_t * rng_inst, int sched_reason,
		bool need_rtt_mutex)
{
	dhd_pub_t *dhd = (struct dhd_pub *)(cfg->pub);
	u8 rtt_invalid_reason = RTT_STATE_VALID;
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhd);
	int8 target_cnt = 0;
	int reset_req_drop = 0;

	if (need_rtt_mutex == TRUE) {
		mutex_lock(&rtt_status->rtt_mutex);
	}

	WL_INFORM_MEM(("wl_cfgnan_reset_geofence_ranging: "
		"sched_reason = %d, cur_idx = %d, target_cnt = %d\n",
		sched_reason, rtt_status->geofence_cfg.cur_target_idx,
		rtt_status->geofence_cfg.geofence_target_cnt));

	if (rtt_status->rtt_sched == TRUE) {
		reset_req_drop = 1;
		goto exit;
	}

	target_cnt = dhd_rtt_get_geofence_target_cnt(dhd);
	if (target_cnt == 0) {
		WL_DBG(("No geofence targets to schedule\n"));
		/*
		 * FIXME:
		 * No Geofence target
		 * Remove all valid ranging inst
		 */
		if (rng_inst) {
			WL_INFORM_MEM(("Removing Ranging Instance " MACDBG "\n",
				MAC2STRDBG(&(rng_inst->peer_addr))));
			bzero(rng_inst, sizeof(*rng_inst));
		}
		/* Cancel pending retry timer if any */
		if (delayed_work_pending(&rtt_status->rtt_retry_timer)) {
			cancel_delayed_work(&rtt_status->rtt_retry_timer);
		}

		/* invalidate current index as there are no targets */
		dhd_rtt_set_geofence_cur_target_idx(dhd,
			DHD_RTT_INVALID_TARGET_INDEX);
		reset_req_drop = 2;
		goto exit;
	}

	if (dhd_rtt_is_geofence_setup_inprog(dhd)) {
		/* Will be called again for schedule once lock is removed */
		reset_req_drop = 3;
		goto exit;
	}

	/* Avoid schedule if
	 * already geofence running
	 * or Directed RTT in progress
	 * or Invalid RTT state like
	 * NDP with Peer
	 */
	if ((!RTT_IS_STOPPED(rtt_status)) ||
		(rtt_invalid_reason != RTT_STATE_VALID)) {
		/* Not in valid RTT state, avoid schedule */
		reset_req_drop = 4;
		goto exit;
	}

	if (dhd_rtt_geofence_sessions_maxed_out(dhd)) {
		reset_req_drop = 5;
		goto exit;
	}

	if (!wl_cfgnan_update_geofence_target_idx(cfg)) {
		reset_req_drop = 6;
		goto exit;
	}

	/*
	 * FixMe: Retry geofence target over a timer Logic
	 * to be brought back later again
	 * in accordance to new multipeer implementation
	 */

	/* schedule RTT */
	dhd_rtt_schedule_rtt_work_thread(dhd, sched_reason);

exit:
	if (reset_req_drop) {
		WL_INFORM_MEM(("reset geofence req dropped, reason = %d\n",
			reset_req_drop));
	}
	if (need_rtt_mutex == TRUE) {
		mutex_unlock(&rtt_status->rtt_mutex);
	}
	return;
}

void
wl_cfgnan_reset_geofence_ranging_for_cur_target(dhd_pub_t *dhd, int sched_reason)
{
	struct net_device *dev = dhd_linux_get_primary_netdev(dhd);
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	rtt_geofence_target_info_t  *geofence_target = NULL;
	nan_ranging_inst_t *ranging_inst = NULL;

	geofence_target = dhd_rtt_get_geofence_current_target(dhd);
	if (!geofence_target) {
		WL_DBG(("reset ranging request dropped: geofence target null\n"));
		goto exit;
	}

	ranging_inst = wl_cfgnan_check_for_ranging(cfg,
			&geofence_target->peer_addr);
	if (!ranging_inst) {
		WL_DBG(("reset ranging request dropped: ranging instance null\n"));
		goto exit;
	}

	if (NAN_RANGING_IS_IN_PROG(ranging_inst->range_status) &&
		(ranging_inst->range_type == RTT_TYPE_NAN_GEOFENCE)) {
		WL_DBG(("Ranging is already in progress for Current target "
			MACDBG " \n", MAC2STRDBG(&ranging_inst->peer_addr)));
		goto exit;
	}

	wl_cfgnan_reset_geofence_ranging(cfg, ranging_inst, sched_reason, TRUE);

exit:
	return;
}

static bool
wl_cfgnan_geofence_retry_check(nan_ranging_inst_t *rng_inst, uint8 reason_code)
{
	bool geof_retry = FALSE;

	switch (reason_code) {
		case NAN_RNG_TERM_IDLE_TIMEOUT:
		/* Fallthrough: Keep adding more reason code if needed */
		case NAN_RNG_TERM_RNG_RESP_TIMEOUT:
		case NAN_RNG_TERM_RNG_RESP_REJ:
		case NAN_RNG_TERM_RNG_TXS_FAIL:
			if (rng_inst->geof_retry_count <
					NAN_RNG_GEOFENCE_MAX_RETRY_CNT) {
				rng_inst->geof_retry_count++;
				geof_retry = TRUE;
			}
			break;
		default:
			/* FALSE for any other case */
			break;
	}

	return geof_retry;
}
#endif /* RTT_SUPPORT */

s32
wl_cfgnan_notify_nan_status(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *event, void *event_data)
{
	uint16 data_len;
	uint32 event_num;
	s32 event_type;
	int hal_event_id = 0;
	nan_event_data_t *nan_event_data = NULL;
	nan_parse_event_ctx_t nan_event_ctx;
	uint16 tlvs_offset = 0;
	uint16 nan_opts_len = 0;
	uint8 *tlv_buf;
	s32 ret = BCME_OK;
	bcm_xtlv_opts_t xtlv_opt = BCM_IOV_CMD_OPT_ALIGN32;
	uint32 status;
	nan_svc_info_t *svc;
#ifdef RTT_SUPPORT
	dhd_pub_t *dhd = (struct dhd_pub *)(cfg->pub);
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhd);
	UNUSED_PARAMETER(dhd);
	UNUSED_PARAMETER(rtt_status);
	if (rtt_status == NULL) {
		return -EINVAL;
	}
#endif /* RTT_SUPPORT */

	UNUSED_PARAMETER(wl_nan_print_status);
	UNUSED_PARAMETER(status);
	NAN_DBG_ENTER();

	if (!event || !event_data) {
		WL_ERR(("event data is NULL\n"));
		return -EINVAL;
	}

	event_type = ntoh32(event->event_type);
	event_num = ntoh32(event->reason);
	data_len = ntoh32(event->datalen);

#ifdef RTT_SUPPORT
	if (event_num == WL_NAN_EVENT_RNG_REQ_IND)
	{
		/* Flush any RTT work  to avoid any
		* inconsistencies & ensure RNG REQ
		* is handling in a stable RTT state.
		* Note new RTT work can be enqueued from
		* a. host command context - synchronized over rtt_mutex & state
		* b. event context - event processing is synchronized/serialised
		*/
		flush_work(&rtt_status->work);
	}
#endif /* RTT_SUPPORT */

	NAN_MUTEX_LOCK();

	if (NAN_INVALID_EVENT(event_num)) {
		WL_ERR(("unsupported event, num: %d, event type: %d\n", event_num, event_type));
		ret = -EINVAL;
		goto exit;
	}

	WL_DBG((">> Nan Event Received: %s (num=%d, len=%d)\n",
			nan_event_to_str(event_num), event_num, data_len));

#ifdef WL_NAN_DEBUG
	prhex("nan_event_data:", event_data, data_len);
#endif /* WL_NAN_DEBUG */

	if (!cfg->nancfg->nan_init_state) {
		WL_ERR(("nan is not in initialized state, dropping nan related events\n"));
		ret = BCME_OK;
		goto exit;
	}

	nan_event_data = MALLOCZ(cfg->osh, sizeof(*nan_event_data));
	if (!nan_event_data) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		goto exit;
	}

	nan_event_ctx.cfg = cfg;
	nan_event_ctx.nan_evt_data = nan_event_data;
	/*
	 * send as preformatted hex string
	 * EVENT_NAN <event_type> <tlv_hex_string>
	 */
	switch (event_num) {
	case WL_NAN_EVENT_START:
	case WL_NAN_EVENT_MERGE:
	case WL_NAN_EVENT_ROLE:	{
		/* get nan status info as-is */
		bcm_xtlv_t *xtlv = (bcm_xtlv_t *)event_data;
		wl_nan_conf_status_t *nstatus = (wl_nan_conf_status_t *)xtlv->data;
		WL_INFORM_MEM((">> Nan Mac Event Received: %s (num=%d, len=%d)\n",
			nan_event_to_str(event_num), event_num, data_len));
		WL_INFORM_MEM(("Nan Device Role %s\n", nan_role_to_str(nstatus->role)));
		/* Mapping to common struct between DHD and HAL */
		nan_event_data->enabled = nstatus->enabled;
		ret = memcpy_s(&nan_event_data->local_nmi, ETHER_ADDR_LEN,
			&nstatus->nmi, ETHER_ADDR_LEN);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy nmi\n"));
			goto exit;
		}
		ret = memcpy_s(&nan_event_data->clus_id, ETHER_ADDR_LEN,
			&nstatus->cid, ETHER_ADDR_LEN);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy cluster id\n"));
			goto exit;
		}
		nan_event_data->nan_de_evt_type = event_num;
		if (event_num == WL_NAN_EVENT_ROLE) {
			wl_nan_print_status(nstatus);
		}

		if (event_num == WL_NAN_EVENT_START) {
			OSL_SMP_WMB();
			cfg->nancfg->nan_event_recvd = true;
			OSL_SMP_WMB();
			wake_up(&cfg->nancfg->nan_event_wait);
		}
		hal_event_id = GOOGLE_NAN_EVENT_DE_EVENT;
		break;
	}
	case WL_NAN_EVENT_TERMINATED: {
		bcm_xtlv_t *xtlv = (bcm_xtlv_t *)event_data;
		wl_nan_ev_terminated_t *pev = (wl_nan_ev_terminated_t *)xtlv->data;

		/* Mapping to common struct between DHD and HAL */
		WL_TRACE(("Instance ID: %d\n", pev->instance_id));
		nan_event_data->local_inst_id = pev->instance_id;
		WL_TRACE(("Service Type: %d\n", pev->svctype));

#ifdef WL_NAN_DISC_CACHE
		wl_cfgnan_clear_svc_cache(cfg, pev->instance_id);
		/* if we have to store disc_res even after sub_cancel
		* donot call below api..but need to device on the criteria to expire
		*/
		if (pev->svctype == NAN_SC_SUBSCRIBE) {
			wl_cfgnan_remove_disc_result(cfg, pev->instance_id);
		}
#endif /* WL_NAN_DISC_CACHE */
		/* Mapping reason code of FW to status code of framework */
		if (pev->reason == NAN_TERM_REASON_TIMEOUT ||
				pev->reason == NAN_TERM_REASON_USER_REQ ||
				pev->reason == NAN_TERM_REASON_COUNT_REACHED) {
			nan_event_data->status = NAN_STATUS_SUCCESS;
			ret = memcpy_s(nan_event_data->nan_reason,
				sizeof(nan_event_data->nan_reason),
				"NAN_STATUS_SUCCESS",
				strlen("NAN_STATUS_SUCCESS"));
			if (ret != BCME_OK) {
				WL_ERR(("Failed to copy nan_reason\n"));
				goto exit;
			}
		} else {
			nan_event_data->status = NAN_STATUS_INTERNAL_FAILURE;
			ret = memcpy_s(nan_event_data->nan_reason,
				sizeof(nan_event_data->nan_reason),
				"NAN_STATUS_INTERNAL_FAILURE",
				strlen("NAN_STATUS_INTERNAL_FAILURE"));
			if (ret != BCME_OK) {
				WL_ERR(("Failed to copy nan_reason\n"));
				goto exit;
			}
		}

		if (pev->svctype == NAN_SC_SUBSCRIBE) {
			hal_event_id = GOOGLE_NAN_EVENT_SUBSCRIBE_TERMINATED;
		} else {
			hal_event_id = GOOGLE_NAN_EVENT_PUBLISH_TERMINATED;
		}
#ifdef WL_NAN_DISC_CACHE
#ifdef RTT_SUPPORT
		if (pev->reason != NAN_TERM_REASON_USER_REQ) {
			wl_cfgnan_clear_svc_from_all_ranging_inst(cfg, pev->instance_id);
			/* terminate ranging sessions */
			wl_cfgnan_terminate_all_obsolete_ranging_sessions(cfg);
		}
#endif /* RTT_SUPPORT */
#endif /* WL_NAN_DISC_CACHE */
		break;
	}

	case WL_NAN_EVENT_RECEIVE: {
		nan_opts_len = data_len;
		hal_event_id = GOOGLE_NAN_EVENT_FOLLOWUP;
		xtlv_opt = BCM_IOV_CMD_OPT_ALIGN_NONE;
		break;
	}

	case WL_NAN_EVENT_TXS: {
		bcm_xtlv_t *xtlv = (bcm_xtlv_t *)event_data;
		wl_nan_event_txs_t *txs = (wl_nan_event_txs_t *)xtlv->data;
		wl_nan_event_sd_txs_t *txs_sd = NULL;
		if (txs->status == WL_NAN_TXS_SUCCESS) {
			WL_INFORM_MEM(("TXS success for type %s(%d) token %d\n",
				nan_frm_type_to_str(txs->type), txs->type, txs->host_seq));
			nan_event_data->status = NAN_STATUS_SUCCESS;
			ret = memcpy_s(nan_event_data->nan_reason,
				sizeof(nan_event_data->nan_reason),
				"NAN_STATUS_SUCCESS",
				strlen("NAN_STATUS_SUCCESS"));
			if (ret != BCME_OK) {
				WL_ERR(("Failed to copy nan_reason\n"));
				goto exit;
			}
		} else {
			/* TODO : populate status based on reason codes
			For now adding it as no ACK, so that app/framework can retry
			*/
			WL_INFORM_MEM(("TXS failed for type %s(%d) status %d token %d\n",
				nan_frm_type_to_str(txs->type), txs->type, txs->status,
				txs->host_seq));
			nan_event_data->status = NAN_STATUS_NO_OTA_ACK;
			ret = memcpy_s(nan_event_data->nan_reason,
				sizeof(nan_event_data->nan_reason),
				"NAN_STATUS_NO_OTA_ACK",
				strlen("NAN_STATUS_NO_OTA_ACK"));
			if (ret != BCME_OK) {
				WL_ERR(("Failed to copy nan_reason\n"));
				goto exit;
			}
		}
		nan_event_data->reason = txs->reason_code;
		nan_event_data->token = txs->host_seq;
		if (txs->type == WL_NAN_FRM_TYPE_FOLLOWUP) {
			hal_event_id = GOOGLE_NAN_EVENT_TRANSMIT_FOLLOWUP_IND;
			xtlv = (bcm_xtlv_t *)(txs->opt_tlvs);
			if (txs->opt_tlvs_len && xtlv->id == WL_NAN_XTLV_SD_TXS) {
				txs_sd = (wl_nan_event_sd_txs_t*)xtlv->data;
				nan_event_data->local_inst_id = txs_sd->inst_id;
			} else {
				WL_ERR(("Invalid params in TX status for trasnmit followup"));
				ret = -EINVAL;
				goto exit;
			}
#ifdef RTT_SUPPORT
		} else if (txs->type == WL_NAN_FRM_TYPE_RNG_RESP) {
			xtlv = (bcm_xtlv_t *)(txs->opt_tlvs);
			if (txs->opt_tlvs_len && xtlv->id == WL_NAN_XTLV_RNG_TXS) {
				wl_nan_range_txs_t* txs_rng_resp = (wl_nan_range_txs_t*)xtlv->data;
				nan_ranging_inst_t *rng_inst =
					wl_cfgnan_get_rng_inst_by_id(cfg, txs_rng_resp->range_id);
				if (rng_inst &&
					NAN_RANGING_SETUP_IS_IN_PROG(rng_inst->range_status)) {
					/* Unset ranging set up in progress */
					dhd_rtt_update_geofence_sessions_cnt(dhd, FALSE,
						&rng_inst->peer_addr);
					if (txs->status == WL_NAN_TXS_SUCCESS) {
						/* range set up is over, move range in progress */
						rng_inst->range_status =
							NAN_RANGING_SESSION_IN_PROGRESS;
						 /* Increment geofence session count */
						dhd_rtt_update_geofence_sessions_cnt(dhd,
							TRUE, NULL);
						WL_DBG(("Txs for range resp, rng_id = %d\n",
							rng_inst->range_id));
					} else {
						wl_cfgnan_reset_remove_ranging_instance(cfg,
							rng_inst);
					}
				}
			} else {
				WL_ERR(("Invalid params in TX status for range response"));
				ret = -EINVAL;
				goto exit;
			}
#endif /* RTT_SUPPORT */
		} else { /* TODO: add for other frame types if required */
			ret = -EINVAL;
			goto exit;
		}
		break;
	}

	case WL_NAN_EVENT_DISCOVERY_RESULT: {
		nan_opts_len = data_len;
		hal_event_id = GOOGLE_NAN_EVENT_SUBSCRIBE_MATCH;
		xtlv_opt = BCM_IOV_CMD_OPT_ALIGN_NONE;
		break;
	}
#ifdef WL_NAN_DISC_CACHE
	case WL_NAN_EVENT_DISC_CACHE_TIMEOUT: {
		bcm_xtlv_t *xtlv = (bcm_xtlv_t *)event_data;
		wl_nan_ev_disc_cache_timeout_t *cache_data =
				(wl_nan_ev_disc_cache_timeout_t *)xtlv->data;
		wl_nan_disc_expired_cache_entry_t *cache_entry = NULL;
		uint16 xtlv_len = xtlv->len;
		uint8 entry_idx = 0;

		if (xtlv->id == WL_NAN_XTLV_SD_DISC_CACHE_TIMEOUT) {
			xtlv_len = xtlv_len -
					OFFSETOF(wl_nan_ev_disc_cache_timeout_t, cache_exp_list);
			while ((entry_idx < cache_data->count) &&
					(xtlv_len >= sizeof(*cache_entry))) {
				cache_entry = &cache_data->cache_exp_list[entry_idx];
				/* Handle ranging cases for cache timeout */
				WL_INFORM_MEM(("WL_NAN_EVENT_DISC_CACHE_TIMEOUT peer: " MACDBG
					" l_id:%d r_id:%d\n", MAC2STRDBG(&cache_entry->r_nmi_addr),
					cache_entry->l_sub_id, cache_entry->r_pub_id));
#ifdef RTT_SUPPORT
				wl_cfgnan_ranging_clear_publish(cfg, &cache_entry->r_nmi_addr,
					cache_entry->l_sub_id);
#endif /* RTT_SUPPORT */
				/* Invalidate local cache info */
				wl_cfgnan_remove_disc_result(cfg, cache_entry->l_sub_id);
				xtlv_len = xtlv_len - sizeof(*cache_entry);
				entry_idx++;
			}
		}
		break;
	}
#ifdef RTT_SUPPORT
	case WL_NAN_EVENT_RNG_REQ_IND: {
		wl_nan_ev_rng_req_ind_t *rng_ind;
		bcm_xtlv_t *xtlv = (bcm_xtlv_t *)event_data;

		nan_opts_len = data_len;
		rng_ind = (wl_nan_ev_rng_req_ind_t *)xtlv->data;
		xtlv_opt = BCM_IOV_CMD_OPT_ALIGN_NONE;
		WL_INFORM_MEM(("Received WL_NAN_EVENT_RNG_REQ_IND range_id %d"
			" peer:" MACDBG "\n", rng_ind->rng_id,
			MAC2STRDBG(&rng_ind->peer_m_addr)));
		ret = wl_cfgnan_handle_ranging_ind(cfg, rng_ind);
		/* no need to event to HAL */
		goto exit;
	}

	case WL_NAN_EVENT_RNG_TERM_IND: {
		bcm_xtlv_t *xtlv = (bcm_xtlv_t *)event_data;
		nan_ranging_inst_t *rng_inst;
		wl_nan_ev_rng_term_ind_t *range_term = (wl_nan_ev_rng_term_ind_t *)xtlv->data;
		int rng_sched_reason = 0;
		int8 index = -1;
		rtt_geofence_target_info_t* geofence_target;
		BCM_REFERENCE(dhd);
		WL_INFORM_MEM(("Received WL_NAN_EVENT_RNG_TERM_IND peer: " MACDBG ", "
			" Range ID:%d Reason Code:%d\n", MAC2STRDBG(&range_term->peer_m_addr),
			range_term->rng_id, range_term->reason_code));
		rng_inst = wl_cfgnan_get_rng_inst_by_id(cfg, range_term->rng_id);
		if (rng_inst) {
			if (!NAN_RANGING_IS_IN_PROG(rng_inst->range_status)) {
				WL_DBG(("Late or unsynchronized nan term indicator event\n"));
				break;
			}
			rng_sched_reason = RTT_SCHED_RNG_TERM;
			if (rng_inst->range_role == NAN_RANGING_ROLE_RESPONDER) {
				dhd_rtt_update_geofence_sessions_cnt(dhd, FALSE,
					&rng_inst->peer_addr);
				wl_cfgnan_reset_remove_ranging_instance(cfg, rng_inst);
			} else {
				if (rng_inst->range_type == RTT_TYPE_NAN_DIRECTED) {
					dhd_rtt_handle_nan_rtt_session_end(dhd,
						&rng_inst->peer_addr);
					if (dhd_rtt_nan_is_directed_setup_in_prog_with_peer(dhd,
						&rng_inst->peer_addr)) {
						dhd_rtt_nan_update_directed_setup_inprog(dhd,
							NULL, FALSE);
					} else {
						dhd_rtt_nan_update_directed_sessions_cnt(dhd,
							FALSE);
					}
				} else if (rng_inst->range_type == RTT_TYPE_NAN_GEOFENCE) {
					rng_inst->range_status = NAN_RANGING_REQUIRED;
					dhd_rtt_update_geofence_sessions_cnt(dhd, FALSE,
						&rng_inst->peer_addr);
					if (!wl_cfgnan_geofence_retry_check(rng_inst,
							range_term->reason_code)) {
						/* Report on ranging failure */
						wl_cfgnan_disc_result_on_geofence_cancel(cfg,
							rng_inst);
						WL_TRACE(("Reset the state on terminate\n"));
						geofence_target = dhd_rtt_get_geofence_target(dhd,
							&rng_inst->peer_addr, &index);
						if (geofence_target) {
							dhd_rtt_remove_geofence_target(dhd,
								&geofence_target->peer_addr);
						}
					}
				}
			}
			/* Reset Ranging Instance and trigger ranging if applicable */
			wl_cfgnan_reset_geofence_ranging(cfg, rng_inst, rng_sched_reason, TRUE);
		} else {
			/*
			 * This can happen in some scenarios
			 * like receiving term after a fail txs for range resp
			 * where ranging instance is already cleared
			 */
			WL_DBG(("Term Indication recieved for a peer without rng inst\n"));
		}
		break;
	}

	case WL_NAN_EVENT_RNG_RESP_IND: {
		bcm_xtlv_t *xtlv = (bcm_xtlv_t *)event_data;
		nan_ranging_inst_t *rng_inst;
		wl_nan_ev_rng_resp_t *range_resp = (wl_nan_ev_rng_resp_t *)xtlv->data;

		WL_INFORM_MEM(("Received WL_NAN_EVENT_RNG_RESP_IND peer: " MACDBG ", "
			" Range ID:%d Ranging Status:%d\n", MAC2STRDBG(&range_resp->peer_m_addr),
			range_resp->rng_id, range_resp->status));
		rng_inst = wl_cfgnan_get_rng_inst_by_id(cfg, range_resp->rng_id);
		if (!rng_inst) {
			WL_DBG(("Late or unsynchronized resp indicator event\n"));
			break;
		}
		//ASSERT(NAN_RANGING_SETUP_IS_IN_PROG(rng_inst->range_status));
		if (!NAN_RANGING_SETUP_IS_IN_PROG(rng_inst->range_status)) {
			WL_INFORM_MEM(("Resp Indicator received for not in prog range inst\n"));
			break;
		}
		/* range set up is over now, move to range in progress */
		rng_inst->range_status = NAN_RANGING_SESSION_IN_PROGRESS;
		if (rng_inst->range_type == RTT_TYPE_NAN_DIRECTED) {
			/* FixMe: Ideally, all below like update session cnt
			 * should be appilicabe to nan rtt and not specific to
			 * geofence. To be fixed in next RB
			 */
			dhd_rtt_nan_update_directed_setup_inprog(dhd, NULL, FALSE);
			/*
			 * Increase session count here,
			 * failure status is followed by Term Ind
			 * and handled accordingly
			 */
			dhd_rtt_nan_update_directed_sessions_cnt(dhd, TRUE);
			/*
			 * If pending targets to be triggered,
			 * and max sessions, not running already,
			 * schedule next target for RTT
			 */
			if ((!dhd_rtt_nan_all_directed_sessions_triggered(dhd)) &&
					dhd_rtt_nan_directed_sessions_allowed(dhd)) {
				/* Find and set next directed target */
				dhd_rtt_set_next_target_idx(dhd,
					(dhd_rtt_get_cur_target_idx(dhd) + 1));
				/* schedule RTT */
				dhd_rtt_schedule_rtt_work_thread(dhd,
					RTT_SCHED_RNG_RESP_IND);
			}
			break;
		}
		/*
		ASSERT(dhd_rtt_is_geofence_setup_inprog_with_peer(dhd,
			&rng_inst->peer_addr));
		*/
		if (!dhd_rtt_is_geofence_setup_inprog_with_peer(dhd,
			&rng_inst->peer_addr)) {
			WL_INFORM_MEM(("Resp Indicator received for not in prog range peer\n"));
			break;
		}
		/* Unset geof ranging setup status */
		dhd_rtt_update_geofence_sessions_cnt(dhd, FALSE, &rng_inst->peer_addr);
		/* Increase geofence session count */
		dhd_rtt_update_geofence_sessions_cnt(dhd, TRUE, NULL);
		wl_cfgnan_reset_geofence_ranging(cfg,
			rng_inst, RTT_SCHED_RNG_RESP_IND, TRUE);
		break;
	}
#endif /* RTT_SUPPORT */
#endif /* WL_NAN_DISC_CACHE */
	/*
	 * Data path events data are received in common event struct,
	 * Handling all the events as part of one case, hence fall through is intentional
	 */
	case WL_NAN_EVENT_PEER_DATAPATH_IND:
	case WL_NAN_EVENT_DATAPATH_ESTB:
	case WL_NAN_EVENT_DATAPATH_END: {
		ret = wl_nan_dp_cmn_event_data(cfg, event_data, data_len,
				&tlvs_offset, &nan_opts_len,
				event_num, &hal_event_id, nan_event_data);
		/* Avoiding optional param parsing for DP END Event */
		if (event_num == WL_NAN_EVENT_DATAPATH_END) {
			nan_opts_len = 0;
			xtlv_opt = BCM_IOV_CMD_OPT_ALIGN_NONE;
		}
		if (unlikely(ret)) {
			WL_ERR(("nan dp common event data parse failed\n"));
			goto exit;
		}
		break;
	}
	case WL_NAN_EVENT_PEER_DATAPATH_RESP:
	{
		/* No action -intentionally added to avoid prints when this event is rcvd */
		break;
	}
	default:
		WL_ERR_RLMT(("WARNING: unimplemented NAN APP EVENT = %d\n", event_num));
		ret = BCME_ERROR;
		goto exit;
	}

	if (nan_opts_len) {
		tlv_buf = (uint8 *)event_data + tlvs_offset;
		/* Extract event data tlvs and pass their resp to cb fn */
		ret = bcm_unpack_xtlv_buf((void *)&nan_event_ctx, (const uint8*)tlv_buf,
			nan_opts_len, xtlv_opt, wl_cfgnan_set_vars_cbfn);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to unpack tlv data, ret=%d\n", ret));
		}
	}

#ifdef WL_NAN_DISC_CACHE
	if (hal_event_id == GOOGLE_NAN_EVENT_SUBSCRIBE_MATCH) {
#ifdef RTT_SUPPORT
		bool send_disc_result;
#endif /* RTT_SUPPORT */
		u16 update_flags = 0;

		WL_TRACE(("Cache disc res\n"));
		ret = wl_cfgnan_cache_disc_result(cfg, nan_event_data, &update_flags);
		if (ret) {
			WL_ERR(("Failed to cache disc result ret %d\n", ret));
		}
#ifdef RTT_SUPPORT
		if (nan_event_data->sde_control_flag & NAN_SDE_CF_RANGING_REQUIRED) {
			ret = wl_cfgnan_check_disc_result_for_ranging(cfg,
				nan_event_data, &send_disc_result);
			if ((ret == BCME_OK) && (send_disc_result == FALSE)) {
				/* Avoid sending disc result instantly and exit */
				goto exit;
			} else {
				/* TODO: should we terminate service if ranging fails ? */
				WL_INFORM_MEM(("Ranging failed or not required, " MACDBG
					" sub_id:%d , pub_id:%d, ret = %d, send_disc_result = %d\n",
					MAC2STRDBG(&nan_event_data->remote_nmi),
					nan_event_data->sub_id, nan_event_data->pub_id,
					ret, send_disc_result));
			}
		} else {
			nan_svc_info_t *svc_info = wl_cfgnan_get_svc_inst(cfg,
				nan_event_data->sub_id, 0);
			if (svc_info && svc_info->ranging_required &&
				(update_flags & NAN_DISC_CACHE_PARAM_SDE_CONTROL)) {
				wl_cfgnan_ranging_clear_publish(cfg,
					&nan_event_data->remote_nmi, nan_event_data->sub_id);
			}
		}
#endif /* RTT_SUPPORT */

		/*
		* If tx match filter is present as part of active subscribe, keep same filter
		* values in discovery results also.
		*/
		if (nan_event_data->sub_id == nan_event_data->requestor_id) {
			svc = wl_cfgnan_get_svc_inst(cfg, nan_event_data->sub_id, 0);
			if (svc && svc->tx_match_filter_len) {
				nan_event_data->tx_match_filter.dlen = svc->tx_match_filter_len;
				nan_event_data->tx_match_filter.data =
					MALLOCZ(cfg->osh, svc->tx_match_filter_len);
				if (!nan_event_data->tx_match_filter.data) {
					WL_ERR(("%s: tx_match_filter_data alloc failed\n",
							__FUNCTION__));
					nan_event_data->tx_match_filter.dlen = 0;
					ret = -ENOMEM;
					goto exit;
				}
				ret = memcpy_s(nan_event_data->tx_match_filter.data,
						nan_event_data->tx_match_filter.dlen,
						svc->tx_match_filter, svc->tx_match_filter_len);
				if (ret != BCME_OK) {
					WL_ERR(("Failed to copy tx match filter data\n"));
					goto exit;
				}
			}
		}
	}
#endif /* WL_NAN_DISC_CACHE */

	WL_TRACE(("Send up %s (%d) data to HAL, hal_event_id=%d\n",
			nan_event_to_str(event_num), event_num, hal_event_id));
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 13, 0)) || defined(WL_VENDOR_EXT_SUPPORT)
	ret = wl_cfgvendor_send_nan_event(cfg->wdev->wiphy, bcmcfg_to_prmry_ndev(cfg),
			hal_event_id, nan_event_data);
	if (ret != BCME_OK) {
		WL_ERR(("Failed to send event to nan hal, %s (%d)\n",
				nan_event_to_str(event_num), event_num));
	}
#endif /* (LINUX_VERSION_CODE > KERNEL_VERSION(3, 13, 0)) || defined(WL_VENDOR_EXT_SUPPORT) */

exit:
	wl_cfgnan_clear_nan_event_data(cfg, nan_event_data);

	NAN_MUTEX_UNLOCK();
	NAN_DBG_EXIT();
	return ret;
}

#ifdef WL_NAN_DISC_CACHE
static int
wl_cfgnan_cache_disc_result(struct bcm_cfg80211 *cfg, void * data,
	u16 *disc_cache_update_flags)
{
	nan_event_data_t* disc = (nan_event_data_t*)data;
	int i, add_index = 0;
	int ret = BCME_OK;
	wl_nancfg_t *nancfg = cfg->nancfg;
	nan_disc_result_cache *disc_res = nancfg->nan_disc_cache;
	*disc_cache_update_flags = 0;

	if (!nancfg->nan_enable) {
		WL_DBG(("nan not enabled"));
		return BCME_NOTENABLED;
	}
	if (nancfg->nan_disc_count == NAN_MAX_CACHE_DISC_RESULT) {
		WL_DBG(("cache full"));
		ret = BCME_NORESOURCE;
		goto done;
	}

	for (i = 0; i < NAN_MAX_CACHE_DISC_RESULT; i++) {
		if (!disc_res[i].valid) {
			add_index = i;
			continue;
		}
		if (!memcmp(&disc_res[i].peer, &disc->remote_nmi, ETHER_ADDR_LEN) &&
			!memcmp(disc_res[i].svc_hash, disc->svc_name, WL_NAN_SVC_HASH_LEN)) {
			WL_DBG(("cache entry already present, i = %d", i));
			/* Update needed parameters here */
			if (disc_res[i].sde_control_flag != disc->sde_control_flag) {
				disc_res[i].sde_control_flag = disc->sde_control_flag;
				*disc_cache_update_flags |= NAN_DISC_CACHE_PARAM_SDE_CONTROL;
			}
			ret = BCME_OK; /* entry already present */
			goto done;
		}
	}
	WL_DBG(("adding cache entry: add_index = %d\n", add_index));
	disc_res[add_index].valid = 1;
	disc_res[add_index].pub_id = disc->pub_id;
	disc_res[add_index].sub_id = disc->sub_id;
	disc_res[add_index].publish_rssi = disc->publish_rssi;
	disc_res[add_index].peer_cipher_suite = disc->peer_cipher_suite;
	disc_res[add_index].sde_control_flag = disc->sde_control_flag;
	ret = memcpy_s(&disc_res[add_index].peer, ETHER_ADDR_LEN,
			&disc->remote_nmi, ETHER_ADDR_LEN);
	if (ret != BCME_OK) {
		WL_ERR(("Failed to copy remote nmi\n"));
		goto done;
	}
	ret = memcpy_s(disc_res[add_index].svc_hash, WL_NAN_SVC_HASH_LEN,
			disc->svc_name, WL_NAN_SVC_HASH_LEN);
	if (ret != BCME_OK) {
		WL_ERR(("Failed to copy svc hash\n"));
		goto done;
	}

	if (disc->svc_info.dlen && disc->svc_info.data) {
		disc_res[add_index].svc_info.dlen = disc->svc_info.dlen;
		disc_res[add_index].svc_info.data =
			MALLOCZ(cfg->osh, disc_res[add_index].svc_info.dlen);
		if (!disc_res[add_index].svc_info.data) {
			WL_ERR(("%s: memory allocation failed\n", __FUNCTION__));
			disc_res[add_index].svc_info.dlen = 0;
			ret = BCME_NOMEM;
			goto done;
		}
		ret = memcpy_s(disc_res[add_index].svc_info.data, disc_res[add_index].svc_info.dlen,
				disc->svc_info.data, disc->svc_info.dlen);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy svc info\n"));
			goto done;
		}
	}
	if (disc->tx_match_filter.dlen && disc->tx_match_filter.data) {
		disc_res[add_index].tx_match_filter.dlen = disc->tx_match_filter.dlen;
		disc_res[add_index].tx_match_filter.data =
			MALLOCZ(cfg->osh, disc_res[add_index].tx_match_filter.dlen);
		if (!disc_res[add_index].tx_match_filter.data) {
			WL_ERR(("%s: memory allocation failed\n", __FUNCTION__));
			disc_res[add_index].tx_match_filter.dlen = 0;
			ret = BCME_NOMEM;
			goto done;
		}
		ret = memcpy_s(disc_res[add_index].tx_match_filter.data,
			disc_res[add_index].tx_match_filter.dlen,
			disc->tx_match_filter.data, disc->tx_match_filter.dlen);
		if (ret != BCME_OK) {
			WL_ERR(("Failed to copy tx match filter\n"));
			goto done;
		}
	}
	nancfg->nan_disc_count++;
	WL_DBG(("cfg->nan_disc_count = %d\n", nancfg->nan_disc_count));

done:
	return ret;
}

#ifdef RTT_SUPPORT
/* Sending command to FW for clearing discovery cache info in FW */
static int
wl_cfgnan_clear_disc_cache(struct bcm_cfg80211 *cfg, wl_nan_instance_id_t sub_id)
{
	s32 ret = BCME_OK;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	uint32 status;
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE];
	uint8 buf[NAN_IOCTL_BUF_SIZE];
	bcm_iov_batch_buf_t *nan_buf;
	bcm_iov_batch_subcmd_t *sub_cmd;
	uint16 subcmd_len;

	bzero(buf, sizeof(buf));
	nan_buf = (bcm_iov_batch_buf_t*)buf;

	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_buf_size -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);

	sub_cmd = (bcm_iov_batch_subcmd_t *)(&nan_buf->cmds[0]);
	ret = wl_cfg_nan_check_cmd_len(nan_buf_size,
			sizeof(sub_id), &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		goto fail;
	}

	/* Fill the sub_command block */
	sub_cmd->id = htod16(WL_NAN_CMD_SD_DISC_CACHE_CLEAR);
	sub_cmd->len = sizeof(sub_cmd->u.options) + sizeof(sub_id);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);
	/* Data size len vs buffer len check is already done above.
	 * So, short buffer error is impossible.
	 */
	(void)memcpy_s(sub_cmd->data, (nan_buf_size - OFFSETOF(bcm_iov_batch_subcmd_t, data)),
			&sub_id, sizeof(sub_id));
	/* adjust iov data len to the end of last data record */
	nan_buf_size -= (subcmd_len);

	nan_buf->count++;
	nan_buf->is_set = true;
	nan_buf_size = NAN_IOCTL_BUF_SIZE - nan_buf_size;
	/* Same src and dest len here */
	bzero(resp_buf, sizeof(resp_buf));
	ret = wl_cfgnan_execute_ioctl(bcmcfg_to_prmry_ndev(cfg), cfg,
			nan_buf, nan_buf_size, &status,
			(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
	if (unlikely(ret) || unlikely(status)) {
		WL_ERR(("Disc cache clear handler failed ret %d status %d\n",
				ret, status));
		goto fail;
	}

fail:
	return ret;
}
#endif /* RTT_SUPPORT */

static int wl_cfgnan_remove_disc_result(struct bcm_cfg80211 *cfg,
		uint8 local_subid)
{
	int i;
	int ret = BCME_NOTFOUND;
	nan_disc_result_cache *disc_res = cfg->nancfg->nan_disc_cache;
	if (!cfg->nancfg->nan_enable) {
		WL_DBG(("nan not enabled\n"));
		ret = BCME_NOTENABLED;
		goto done;
	}
	for (i = 0; i < NAN_MAX_CACHE_DISC_RESULT; i++) {
		if ((disc_res[i].valid) && (disc_res[i].sub_id == local_subid)) {
			WL_TRACE(("make cache entry invalid\n"));
			if (disc_res[i].tx_match_filter.data) {
				MFREE(cfg->osh, disc_res[i].tx_match_filter.data,
					disc_res[i].tx_match_filter.dlen);
			}
			if (disc_res[i].svc_info.data) {
				MFREE(cfg->osh, disc_res[i].svc_info.data,
					disc_res[i].svc_info.dlen);
			}
			bzero(&disc_res[i], sizeof(disc_res[i]));
			cfg->nancfg->nan_disc_count--;
			ret = BCME_OK;
		}
	}
	WL_DBG(("couldn't find entry\n"));
done:
	return ret;
}

static nan_disc_result_cache *
wl_cfgnan_get_disc_result(struct bcm_cfg80211 *cfg, uint8 remote_pubid,
	struct ether_addr *peer)
{
	int i;
	nan_disc_result_cache *disc_res = cfg->nancfg->nan_disc_cache;
	if (remote_pubid) {
		for (i = 0; i < NAN_MAX_CACHE_DISC_RESULT; i++) {
			if ((disc_res[i].pub_id == remote_pubid) &&
					!memcmp(&disc_res[i].peer, peer, ETHER_ADDR_LEN)) {
				WL_DBG(("Found entry: i = %d\n", i));
				return &disc_res[i];
			}
		}
	} else {
		for (i = 0; i < NAN_MAX_CACHE_DISC_RESULT; i++) {
			if (!memcmp(&disc_res[i].peer, peer, ETHER_ADDR_LEN)) {
				WL_DBG(("Found entry: %d\n", i));
				return &disc_res[i];
			}
		}
	}
	return NULL;
}
#endif /* WL_NAN_DISC_CACHE */

static void
wl_cfgnan_update_dp_info(struct bcm_cfg80211 *cfg, bool add,
	nan_data_path_id ndp_id)
{
	uint8 i;
	bool match_found = false;
	wl_nancfg_t *nancfg = cfg->nancfg;
	/* As of now, we don't see a need to know which ndp is active.
	 * so just keep tracking of ndp via count. If we need to know
	 * the status of each ndp based on ndp id, we need to change
	 * this implementation to use a bit mask.
	 */

	if (add) {
		/* On first NAN DP establishment, disable ARP. */
		for (i = 0; i < NAN_MAX_NDP_PEER; i++) {
			if (!nancfg->ndp_id[i]) {
				WL_TRACE(("Found empty field\n"));
				break;
			}
		}

		if (i == NAN_MAX_NDP_PEER) {
			WL_ERR(("%s:cannot accommodate ndp id\n", __FUNCTION__));
			return;
		}
		if (ndp_id) {
			nancfg->nan_dp_count++;
			nancfg->ndp_id[i] = ndp_id;
			WL_DBG(("%s:Added ndp id = [%d] at i = %d\n",
					__FUNCTION__, nancfg->ndp_id[i], i));
			wl_cfg80211_concurrent_roam(cfg, true);
		}
	} else {
		ASSERT(nancfg->nan_dp_count);
		if (ndp_id) {
			for (i = 0; i < NAN_MAX_NDP_PEER; i++) {
				if (nancfg->ndp_id[i] == ndp_id) {
					nancfg->ndp_id[i] = 0;
					WL_DBG(("%s:Removed ndp id = [%d] from i = %d\n",
						__FUNCTION__, ndp_id, i));
					match_found = true;
					if (nancfg->nan_dp_count) {
						nancfg->nan_dp_count--;
					}
					break;
				} else {
					WL_DBG(("couldn't find entry for ndp id = %d\n",
						ndp_id));
				}
			}
			if (match_found == false) {
				WL_ERR(("Received unsaved NDP Id = %d !!\n", ndp_id));
			} else {
				if (nancfg->nan_dp_count == 0) {
					wl_cfg80211_concurrent_roam(cfg, false);
					wl_cfgnan_immediate_nan_disable_pending(cfg);
				}
			}

		}
	}
	WL_INFORM_MEM(("NAN_DP_COUNT: %d\n", nancfg->nan_dp_count));
}

bool
wl_cfgnan_is_dp_active(struct net_device *ndev)
{
	struct bcm_cfg80211 *cfg;
	bool nan_dp;

	if (!ndev || !ndev->ieee80211_ptr) {
		WL_ERR(("ndev/wdev null\n"));
		return false;
	}

	cfg =  wiphy_priv(ndev->ieee80211_ptr->wiphy);
	nan_dp = cfg->nancfg->nan_dp_count ? true : false;

	WL_DBG(("NAN DP status:%d\n", nan_dp));
	return nan_dp;
}

static s32
wl_cfgnan_get_ndi_idx(struct bcm_cfg80211 *cfg)
{
	int i;
	for (i = 0; i < cfg->nancfg->max_ndi_supported; i++) {
		if (!cfg->nancfg->ndi[i].in_use) {
			/* Free interface, use it */
			return i;
		}
	}
	/* Don't have a free interface */
	return WL_INVALID;
}

static s32
wl_cfgnan_add_ndi_data(struct bcm_cfg80211 *cfg, s32 idx, char *name)
{
	u16 len;
	wl_nancfg_t *nancfg = cfg->nancfg;
	if (!name || (idx < 0) || (idx >= cfg->nancfg->max_ndi_supported)) {
		return -EINVAL;
	}

	/* Ensure ifname string size <= IFNAMSIZ including null termination */
	len = MIN(strlen(name), (IFNAMSIZ - 1));
	strncpy(nancfg->ndi[idx].ifname, name, len);
	nancfg->ndi[idx].ifname[len] = '\0';
	nancfg->ndi[idx].in_use = true;
	nancfg->ndi[idx].created = false;

	/* Don't have a free interface */
	return WL_INVALID;
}

static s32
wl_cfgnan_del_ndi_data(struct bcm_cfg80211 *cfg, char *name)
{
	u16 len;
	int i;
	wl_nancfg_t *nancfg = cfg->nancfg;

	if (!name) {
		return -EINVAL;
	}

	len = MIN(strlen(name), IFNAMSIZ);
	for (i = 0; i < cfg->nancfg->max_ndi_supported; i++) {
		if (strncmp(nancfg->ndi[i].ifname, name, len) == 0) {
			bzero(&nancfg->ndi[i].ifname, IFNAMSIZ);
			nancfg->ndi[i].in_use = false;
			nancfg->ndi[i].created = false;
			nancfg->ndi[i].nan_ndev = NULL;
			return i;
		}
	}
	return -EINVAL;
}

s32
wl_cfgnan_delete_ndp(struct bcm_cfg80211 *cfg,
	struct net_device *nan_ndev)
{
	s32 ret = BCME_OK;
	uint8 i = 0;
	wl_nancfg_t *nancfg = cfg->nancfg;

	for (i = 0; i < cfg->nancfg->max_ndi_supported; i++) {
		if (nancfg->ndi[i].in_use && nancfg->ndi[i].created &&
			(nancfg->ndi[i].nan_ndev == nan_ndev)) {
			WL_INFORM_MEM(("iface name: %s, cfg->nancfg->ndi[i].nan_ndev = %p"
					"  and nan_ndev = %p\n",
						(char*)nancfg->ndi[i].ifname,
						nancfg->ndi[i].nan_ndev, nan_ndev));
			ret = _wl_cfg80211_del_if(cfg, nan_ndev, NULL,
					(char*)nancfg->ndi[i].ifname);
			if (ret) {
				WL_ERR(("failed to del ndi [%d]\n", ret));
			}
			/*
			 * Intentional fall through to clear the host data structs
			 * Unconditionally delete the ndi data and states
			 */
			if (wl_cfgnan_del_ndi_data(cfg,
				(char*)nancfg->ndi[i].ifname) < 0) {
				WL_ERR(("Failed to find matching data for ndi:%s\n",
					(char*)nancfg->ndi[i].ifname));
			}
		}
	}
	return ret;
}

int
wl_cfgnan_get_status(struct net_device *ndev, wl_nan_conf_status_t *nan_status)
{
	bcm_iov_batch_buf_t *nan_buf = NULL;
	uint16 subcmd_len;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	bcm_iov_batch_subcmd_t *sub_cmd_resp = NULL;
	uint8 resp_buf[NAN_IOCTL_BUF_SIZE];
	wl_nan_conf_status_t *nstatus = NULL;
	uint32 status;
	s32 ret = BCME_OK;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	NAN_DBG_ENTER();

	nan_buf = MALLOCZ(cfg->osh, NAN_IOCTL_BUF_SIZE);
	if (!nan_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_buf_size -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);
	sub_cmd = (bcm_iov_batch_subcmd_t*)(uint8 *)(&nan_buf->cmds[0]);

	ret = wl_cfg_nan_check_cmd_len(nan_buf_size,
			sizeof(*nstatus), &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		goto fail;
	}

	nstatus = (wl_nan_conf_status_t *)sub_cmd->data;
	sub_cmd->id = htod16(WL_NAN_CMD_CFG_STATUS);
	sub_cmd->len = sizeof(sub_cmd->u.options) + sizeof(*nstatus);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);
	nan_buf_size -= subcmd_len;
	nan_buf->count = 1;
	nan_buf->is_set = false;

	bzero(resp_buf, sizeof(resp_buf));
	ret = wl_cfgnan_execute_ioctl(ndev, cfg, nan_buf, nan_buf_size, &status,
			(void*)resp_buf, NAN_IOCTL_BUF_SIZE);
	if (unlikely(ret) || unlikely(status)) {
		WL_ERR(("get nan status failed ret %d status %d \n",
			ret, status));
		goto fail;
	}
	sub_cmd_resp = &((bcm_iov_batch_buf_t *)(resp_buf))->cmds[0];
	/* WL_NAN_CMD_CFG_STATUS return value doesn't use xtlv package */
	nstatus = ((wl_nan_conf_status_t *)&sub_cmd_resp->data[0]);
	ret = memcpy_s(nan_status, sizeof(wl_nan_conf_status_t),
			nstatus, sizeof(wl_nan_conf_status_t));
	if (ret != BCME_OK) {
		WL_ERR(("Failed to copy tx match filter\n"));
		goto fail;
	}

fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, NAN_IOCTL_BUF_SIZE);
	}
	NAN_DBG_EXIT();
	return ret;
}

s32
wl_nan_print_avail_stats(const uint8 *data)
{
	int idx;
	s32 ret = BCME_OK;
	int s_chan = 0;
	char pbuf[NAN_IOCTL_BUF_SIZE_MED];
	const wl_nan_stats_sched_t *sched = (const wl_nan_stats_sched_t *)data;
#define SLOT_PRINT_SIZE 4

	char *buf = pbuf;
	int remained_len = 0, bytes_written = 0;
	bzero(pbuf, sizeof(pbuf));

	if ((sched->num_slot * SLOT_PRINT_SIZE) > (sizeof(pbuf)-1)) {
		WL_ERR(("overflowed slot number %d detected\n",
			sched->num_slot));
		ret = BCME_BUFTOOSHORT;
		goto exit;
	}

	remained_len = NAN_IOCTL_BUF_SIZE_MED;
	bytes_written = snprintf(buf, remained_len, "Map ID:%u, %u/%u, Slot#:%u ",
		sched->map_id, sched->period, sched->slot_dur, sched->num_slot);

	for (idx = 0; idx < sched->num_slot; idx++) {
		const wl_nan_stats_sched_slot_t *slot;
		slot = &sched->slot[idx];
		s_chan = 0;

		if (!wf_chspec_malformed(slot->chanspec)) {
			s_chan = wf_chspec_ctlchan(slot->chanspec);
		}

		buf += bytes_written;
		remained_len -= bytes_written;
		bytes_written = snprintf(buf, remained_len, "%03d|", s_chan);

	}
	WL_INFORM_MEM(("%s\n", pbuf));
exit:
	return ret;
}

static int
wl_nan_print_stats_tlvs(void *ctx, const uint8 *data, uint16 type, uint16 len)
{
	int err = BCME_OK;

	switch (type) {
		/* Avail stats xtlvs */
		case WL_NAN_XTLV_GEN_AVAIL_STATS_SCHED:
			err = wl_nan_print_avail_stats(data);
			break;
		default:
			err = BCME_BADARG;
			WL_ERR(("Unknown xtlv type received: %x\n", type));
			break;
	}

	return err;
}

int
wl_cfgnan_get_stats(struct bcm_cfg80211 *cfg)
{
	bcm_iov_batch_buf_t *nan_buf = NULL;
	uint16 subcmd_len;
	bcm_iov_batch_subcmd_t *sub_cmd = NULL;
	bcm_iov_batch_subcmd_t *sub_cmd_resp = NULL;
	uint8 *resp_buf = NULL;
	wl_nan_cmn_get_stat_t *get_stat = NULL;
	wl_nan_cmn_stat_t *stats = NULL;
	uint32 status;
	s32 ret = BCME_OK;
	uint16 nan_buf_size = NAN_IOCTL_BUF_SIZE;
	NAN_DBG_ENTER();

	nan_buf = MALLOCZ(cfg->osh, NAN_IOCTL_BUF_SIZE);
	resp_buf = MALLOCZ(cfg->osh, NAN_IOCTL_BUF_SIZE_LARGE);
	if (!nan_buf || !resp_buf) {
		WL_ERR(("%s: memory allocation failed\n", __func__));
		ret = BCME_NOMEM;
		goto fail;
	}

	nan_buf->version = htol16(WL_NAN_IOV_BATCH_VERSION);
	nan_buf->count = 0;
	nan_buf_size -= OFFSETOF(bcm_iov_batch_buf_t, cmds[0]);
	sub_cmd = (bcm_iov_batch_subcmd_t*)(uint8 *)(&nan_buf->cmds[0]);

	ret = wl_cfg_nan_check_cmd_len(nan_buf_size,
			sizeof(*get_stat), &subcmd_len);
	if (unlikely(ret)) {
		WL_ERR(("nan_sub_cmd check failed\n"));
		goto fail;
	}

	get_stat = (wl_nan_cmn_get_stat_t *)sub_cmd->data;
	/* get only local availabiity stats */
	get_stat->modules_btmap = (1 << NAN_AVAIL);
	get_stat->operation = WLA_NAN_STATS_GET;

	sub_cmd->id = htod16(WL_NAN_CMD_GEN_STATS);
	sub_cmd->len = sizeof(sub_cmd->u.options) + sizeof(*get_stat);
	sub_cmd->u.options = htol32(BCM_XTLV_OPTION_ALIGN32);
	nan_buf_size -= subcmd_len;
	nan_buf->count = 1;
	nan_buf->is_set = false;

	ret = wl_cfgnan_execute_ioctl(bcmcfg_to_prmry_ndev(cfg),
			cfg, nan_buf, nan_buf_size, &status,
			(void*)resp_buf, NAN_IOCTL_BUF_SIZE_LARGE);
	if (unlikely(ret) || unlikely(status)) {
		WL_ERR(("get nan stats failed ret %d status %d \n",
			ret, status));
		goto fail;
	}

	sub_cmd_resp = &((bcm_iov_batch_buf_t *)(resp_buf))->cmds[0];

	stats = (wl_nan_cmn_stat_t *)&sub_cmd_resp->data[0];

	if (stats->n_stats) {
		WL_INFORM_MEM((" == Aware Local Avail Schedule ==\n"));
		ret = bcm_unpack_xtlv_buf((void *)&stats->n_stats,
				(const uint8 *)&stats->stats_tlvs,
				stats->totlen - 8, BCM_IOV_CMD_OPT_ALIGN32,
				wl_nan_print_stats_tlvs);
	}
fail:
	if (nan_buf) {
		MFREE(cfg->osh, nan_buf, NAN_IOCTL_BUF_SIZE);
	}
	if (resp_buf) {
		MFREE(cfg->osh, resp_buf, NAN_IOCTL_BUF_SIZE_LARGE);
	}

	NAN_DBG_EXIT();
	return ret;
}

int
wl_cfgnan_attach(struct bcm_cfg80211 *cfg)
{
	int err = BCME_OK;
	wl_nancfg_t *nancfg = NULL;

	if (cfg) {
		cfg->nancfg = (wl_nancfg_t *)MALLOCZ(cfg->osh, sizeof(wl_nancfg_t));
		if (cfg->nancfg == NULL) {
			err = BCME_NOMEM;
			goto done;
		}
		cfg->nancfg->cfg = cfg;
	} else {
		err = BCME_BADARG;
		goto done;
	}

	nancfg = cfg->nancfg;
	mutex_init(&nancfg->nan_sync);
	init_waitqueue_head(&nancfg->nan_event_wait);
	INIT_DELAYED_WORK(&nancfg->nan_disable, wl_cfgnan_delayed_disable);
	nancfg->nan_dp_state = NAN_DP_STATE_DISABLED;
	init_waitqueue_head(&nancfg->ndp_if_change_event);

done:
	return err;

}

void
wl_cfgnan_detach(struct bcm_cfg80211 *cfg)
{
	if (cfg && cfg->nancfg) {
		if (delayed_work_pending(&cfg->nancfg->nan_disable)) {
			WL_DBG(("Cancel nan_disable work\n"));
			DHD_NAN_WAKE_UNLOCK(cfg->pub);
			cancel_delayed_work_sync(&cfg->nancfg->nan_disable);
		}
		MFREE(cfg->osh, cfg->nancfg, sizeof(wl_nancfg_t));
		cfg->nancfg = NULL;
	}

}
#endif /* WL_NAN */
