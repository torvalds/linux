/*
 * Broadcom Dongle Host Driver (DHD), RTT
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
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
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: dhd_rtt.c 606280 2015-12-15 05:28:25Z $
 */
#ifdef RTT_SUPPORT
#include <typedefs.h>
#include <osl.h>

#include <epivers.h>
#include <bcmutils.h>

#include <bcmendian.h>
#include <linuxver.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/sort.h>
#include <dngl_stats.h>
#include <wlioctl.h>

#include <proto/bcmevent.h>
#include <dhd.h>
#include <dhd_rtt.h>
#include <dhd_dbg.h>
#define GET_RTTSTATE(dhd) ((rtt_status_info_t *)dhd->rtt_state)
static DEFINE_SPINLOCK(noti_list_lock);
#define NULL_CHECK(p, s, err)  \
			do { \
				if (!(p)) { \
					printf("NULL POINTER (%s) : %s\n", __FUNCTION__, (s)); \
					err = BCME_ERROR; \
					return err; \
				} \
			} while (0)

#define RTT_TWO_SIDED(capability) \
			do { \
				if ((capability & RTT_CAP_ONE_WAY) == (uint8) (RTT_CAP_ONE_WAY)) \
					return FALSE; \
				else \
					return TRUE; \
			} while (0)
#define TIMESPEC_TO_US(ts)  (((uint64)(ts).tv_sec * USEC_PER_SEC) + \
							(ts).tv_nsec / NSEC_PER_USEC)
struct rtt_noti_callback {
	struct list_head list;
	void *ctx;
	dhd_rtt_compl_noti_fn noti_fn;
};

typedef struct rtt_status_info {
	dhd_pub_t *dhd;
	int8 status;   /* current status for the current entry */
	int8 cur_idx; /* current entry to do RTT */
	int32 capability; /* rtt capability */
	struct mutex rtt_mutex;
	rtt_config_params_t rtt_config;
	struct work_struct work;
	struct list_head noti_fn_list;
	struct list_head rtt_results_cache; /* store results for RTT */
} rtt_status_info_t;

static int dhd_rtt_start(dhd_pub_t *dhd);

chanspec_t
dhd_rtt_convert_to_chspec(wifi_channel_info_t channel)
{
	int bw;
	/* set witdh to 20MHZ for 2.4G HZ */
	if (channel.center_freq >= 2400 && channel.center_freq <= 2500) {
		channel.width = WIFI_CHAN_WIDTH_20;
	}
	switch (channel.width) {
	case WIFI_CHAN_WIDTH_20:
		bw = WL_CHANSPEC_BW_20;
		break;
	case WIFI_CHAN_WIDTH_40:
		bw = WL_CHANSPEC_BW_40;
		break;
	case WIFI_CHAN_WIDTH_80:
		bw = WL_CHANSPEC_BW_80;
		break;
	case WIFI_CHAN_WIDTH_160:
		bw = WL_CHANSPEC_BW_160;
		break;
	default:
		DHD_ERROR(("doesn't support this bandwith : %d", channel.width));
		bw = -1;
		break;
	}
	return wf_channel2chspec(wf_mhz2channel(channel.center_freq, 0), bw);
}

int
dhd_rtt_set_cfg(dhd_pub_t *dhd, rtt_config_params_t *params)
{
	int err = BCME_OK;
	int idx;
	rtt_status_info_t *rtt_status;
	NULL_CHECK(params, "params is NULL", err);

	NULL_CHECK(dhd, "dhd is NULL", err);
	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	if (rtt_status->capability == RTT_CAP_NONE) {
		DHD_ERROR(("doesn't support RTT \n"));
		return BCME_ERROR;
	}
	if (rtt_status->status == RTT_STARTED) {
		DHD_ERROR(("rtt is already started\n"));
		return BCME_BUSY;
	}
	DHD_RTT(("%s enter\n", __FUNCTION__));
	bcopy(params, &rtt_status->rtt_config, sizeof(rtt_config_params_t));
	rtt_status->status = RTT_STARTED;
	/* start to measure RTT from 1th device */
	/* find next target to trigger RTT */
	for (idx = rtt_status->cur_idx; idx < rtt_status->rtt_config.rtt_target_cnt; idx++) {
		/* skip the disabled device */
		if (rtt_status->rtt_config.target_info[idx].disable) {
			continue;
		} else {
			/* set the idx to cur_idx */
			rtt_status->cur_idx = idx;
			break;
		}
	}
	if (idx < rtt_status->rtt_config.rtt_target_cnt) {
		DHD_RTT(("rtt_status->cur_idx : %d\n", rtt_status->cur_idx));
		schedule_work(&rtt_status->work);
	}
	return err;
}

int
dhd_rtt_stop(dhd_pub_t *dhd, struct ether_addr *mac_list, int mac_cnt)
{
	int err = BCME_OK;
	int i = 0, j = 0;
	rtt_status_info_t *rtt_status;

	NULL_CHECK(dhd, "dhd is NULL", err);
	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	if (rtt_status->status == RTT_STOPPED) {
		DHD_ERROR(("rtt is not started\n"));
		return BCME_OK;
	}
	DHD_RTT(("%s enter\n", __FUNCTION__));
	mutex_lock(&rtt_status->rtt_mutex);
	for (i = 0; i < mac_cnt; i++) {
		for (j = 0; j < rtt_status->rtt_config.rtt_target_cnt; j++) {
			if (!bcmp(&mac_list[i], &rtt_status->rtt_config.target_info[j].addr,
				ETHER_ADDR_LEN)) {
				rtt_status->rtt_config.target_info[j].disable = TRUE;
			}
		}
	}
	mutex_unlock(&rtt_status->rtt_mutex);
	return err;
}

static int
dhd_rtt_start(dhd_pub_t *dhd)
{
	int err = BCME_OK;
	int mpc = 0;
	int nss, mcs, bw;
	uint32 rspec = 0;
	int8 eabuf[ETHER_ADDR_STR_LEN];
	int8 chanbuf[CHANSPEC_STR_LEN];
	bool set_mpc = FALSE;
	wl_proxd_iovar_t proxd_iovar;
	wl_proxd_params_iovar_t proxd_params;
	wl_proxd_params_iovar_t proxd_tune;
	wl_proxd_params_tof_method_t *tof_params = &proxd_params.u.tof_params;
	rtt_status_info_t *rtt_status;
	rtt_target_info_t *rtt_target;
	NULL_CHECK(dhd, "dhd is NULL", err);

	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	/* turn off mpc in case of non-associted */
	if (!dhd_is_associated(dhd, 0, NULL)) {
		err = dhd_iovar(dhd, 0, "mpc", (char *)&mpc, sizeof(mpc), 1);
		if (err < 0) {
			DHD_ERROR(("%s : failed to set proxd_tune\n", __FUNCTION__));
			goto exit;
		}
		set_mpc = TRUE;
	}

	if (rtt_status->cur_idx >= rtt_status->rtt_config.rtt_target_cnt) {
		err = BCME_RANGE;
		goto exit;
	}
	DHD_RTT(("%s enter\n", __FUNCTION__));
	bzero(&proxd_tune, sizeof(proxd_tune));
	bzero(&proxd_params, sizeof(proxd_params));
	mutex_lock(&rtt_status->rtt_mutex);
	/* Get a target information */
	rtt_target = &rtt_status->rtt_config.target_info[rtt_status->cur_idx];
	mutex_unlock(&rtt_status->rtt_mutex);
	/* set role */
	proxd_iovar.method = PROXD_TOF_METHOD;
	proxd_iovar.mode = WL_PROXD_MODE_INITIATOR;

	/* make sure that proxd is stop */
	/* dhd_iovar(dhd, 0, "proxd_stop", (char *)NULL, 0, 1); */

	err = dhd_iovar(dhd, 0, "proxd", (char *)&proxd_iovar, sizeof(proxd_iovar), 1);
	if (err < 0 && err != BCME_BUSY) {
		DHD_ERROR(("%s : failed to set proxd %d\n", __FUNCTION__, err));
		goto exit;
	}
	if (err == BCME_BUSY) {
		DHD_RTT(("BCME_BUSY occurred\n"));
	}
	/* mac address */
	bcopy(&rtt_target->addr, &tof_params->tgt_mac, ETHER_ADDR_LEN);
	/* frame count */
	if (rtt_target->ftm_cnt > RTT_MAX_FRAME_CNT) {
		rtt_target->ftm_cnt = RTT_MAX_FRAME_CNT;
	}

	if (rtt_target->ftm_cnt) {
		tof_params->ftm_cnt = htol16(rtt_target->ftm_cnt);
	} else {
		tof_params->ftm_cnt = htol16(DEFAULT_FTM_CNT);
	}

	if (rtt_target->retry_cnt > RTT_MAX_RETRY_CNT) {
		rtt_target->retry_cnt = RTT_MAX_RETRY_CNT;
	}

	/* retry count */
	if (rtt_target->retry_cnt) {
		tof_params->retry_cnt = htol16(rtt_target->retry_cnt);
	} else {
		tof_params->retry_cnt = htol16(DEFAULT_RETRY_CNT);
	}

	/* chanspec */
	tof_params->chanspec = htol16(rtt_target->chanspec);
	/* set parameter */
	DHD_RTT(("Target addr(Idx %d) %s, Channel : %s for RTT (ftm_cnt %d, rety_cnt : %d)\n",
		rtt_status->cur_idx,
		bcm_ether_ntoa((const struct ether_addr *)&rtt_target->addr, eabuf),
		wf_chspec_ntoa(rtt_target->chanspec, chanbuf), rtt_target->ftm_cnt,
		rtt_target->retry_cnt));

	if (rtt_target->type == RTT_ONE_WAY) {
		proxd_tune.u.tof_tune.flags = htol32(WL_PROXD_FLAG_ONEWAY);
		/* report RTT results for initiator */
		proxd_tune.u.tof_tune.flags |= htol32(WL_PROXD_FLAG_INITIATOR_RPTRTT);
		proxd_tune.u.tof_tune.vhtack = 0;
		tof_params->tx_rate = htol16(WL_RATE_6M);
		tof_params->vht_rate = htol16((WL_RATE_6M >> 16));
	} else { /* RTT TWO WAY */
		/* initiator will send the rtt result to the target  */
		proxd_tune.u.tof_tune.flags = htol32(WL_PROXD_FLAG_INITIATOR_REPORT);
		tof_params->timeout = 10; /* 10ms for timeout */
		rspec = WL_RSPEC_ENCODE_VHT;	/* 11ac VHT */
		nss = 1; /* default Nss = 1 */
		mcs = 0; /* default MCS 0 */
		rspec |= (nss << WL_RSPEC_VHT_NSS_SHIFT) | mcs;
		bw = 0;
		switch (CHSPEC_BW(rtt_target->chanspec)) {
		case WL_CHANSPEC_BW_20:
			bw = WL_RSPEC_BW_20MHZ;
			break;
		case WL_CHANSPEC_BW_40:
			bw = WL_RSPEC_BW_40MHZ;
			break;
		case WL_CHANSPEC_BW_80:
			bw = WL_RSPEC_BW_80MHZ;
			break;
		case WL_CHANSPEC_BW_160:
			bw = WL_RSPEC_BW_160MHZ;
			break;
		default:
			DHD_ERROR(("CHSPEC_BW not supported : %d",
				CHSPEC_BW(rtt_target->chanspec)));
			goto exit;
		}
		rspec |= bw;
		tof_params->tx_rate = htol16(rspec & 0xffff);
		tof_params->vht_rate = htol16(rspec >> 16);
	}

	/* Set Method to TOF */
	proxd_tune.method = PROXD_TOF_METHOD;
	err = dhd_iovar(dhd, 0, "proxd_tune", (char *)&proxd_tune, sizeof(proxd_tune), 1);
	if (err < 0) {
		DHD_ERROR(("%s : failed to set proxd_tune %d\n", __FUNCTION__, err));
		goto exit;
	}

	/* Set Method to TOF */
	proxd_params.method = PROXD_TOF_METHOD;
	err = dhd_iovar(dhd, 0, "proxd_params", (char *)&proxd_params, sizeof(proxd_params), 1);
	if (err < 0) {
		DHD_ERROR(("%s : failed to set proxd_params %d\n", __FUNCTION__, err));
		goto exit;
	}
	err = dhd_iovar(dhd, 0, "proxd_find", (char *)NULL, 0, 1);
	if (err < 0) {
		DHD_ERROR(("%s : failed to set proxd_find %d\n", __FUNCTION__, err));
		goto exit;
	}
exit:
	if (err < 0) {
		rtt_status->status = RTT_STOPPED;
		if (set_mpc) {
			/* enable mpc again in case of error */
			mpc = 1;
			err = dhd_iovar(dhd, 0, "mpc", (char *)&mpc, sizeof(mpc), 1);
		}
	}
	return err;
}

int
dhd_rtt_register_noti_callback(dhd_pub_t *dhd, void *ctx, dhd_rtt_compl_noti_fn noti_fn)
{
	int err = BCME_OK;
	struct rtt_noti_callback *cb = NULL, *iter;
	rtt_status_info_t *rtt_status;
	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(noti_fn, "noti_fn is NULL", err);

	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	spin_lock_bh(&noti_list_lock);
	list_for_each_entry(iter, &rtt_status->noti_fn_list, list) {
		if (iter->noti_fn == noti_fn) {
			goto exit;
		}
	}
	cb = kmalloc(sizeof(struct rtt_noti_callback), GFP_ATOMIC);
	if (!cb) {
		err = -ENOMEM;
		goto exit;
	}
	cb->noti_fn = noti_fn;
	cb->ctx = ctx;
	list_add(&cb->list, &rtt_status->noti_fn_list);
exit:
	spin_unlock_bh(&noti_list_lock);
	return err;
}

int
dhd_rtt_unregister_noti_callback(dhd_pub_t *dhd, dhd_rtt_compl_noti_fn noti_fn)
{
	int err = BCME_OK;
	struct rtt_noti_callback *cb = NULL, *iter;
	rtt_status_info_t *rtt_status;
	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(noti_fn, "noti_fn is NULL", err);
	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	spin_lock_bh(&noti_list_lock);
	list_for_each_entry(iter, &rtt_status->noti_fn_list, list) {
		if (iter->noti_fn == noti_fn) {
			cb = iter;
			list_del(&cb->list);
			break;
		}
	}
	spin_unlock_bh(&noti_list_lock);
	if (cb) {
		kfree(cb);
	}
	return err;
}

static int
dhd_rtt_convert_to_host(rtt_result_t *rtt_results, const wl_proxd_event_data_t* evp)
{
	int err = BCME_OK;
	int i;
	char eabuf[ETHER_ADDR_STR_LEN];
	char diststr[40];
	struct timespec ts;
	NULL_CHECK(rtt_results, "rtt_results is NULL", err);
	NULL_CHECK(evp, "evp is NULL", err);
	DHD_RTT(("%s enter\n", __FUNCTION__));
	rtt_results->distance = ntoh32(evp->distance);
	rtt_results->sdrtt = ntoh32(evp->sdrtt);
	rtt_results->ftm_cnt = ntoh16(evp->ftm_cnt);
	rtt_results->avg_rssi = ntoh16(evp->avg_rssi);
	rtt_results->validfrmcnt = ntoh16(evp->validfrmcnt);
	rtt_results->meanrtt = ntoh32(evp->meanrtt);
	rtt_results->modertt = ntoh32(evp->modertt);
	rtt_results->medianrtt = ntoh32(evp->medianrtt);
	rtt_results->err_code = evp->err_code;
	rtt_results->tx_rate.preamble = (evp->OFDM_frame_type == TOF_FRAME_RATE_VHT)? 3 : 0;
	rtt_results->tx_rate.nss = 0; /* 1 x 1 */
	rtt_results->tx_rate.bw =
		(evp->bandwidth == TOF_BW_80MHZ)? 2 : (evp->bandwidth == TOF_BW_40MHZ)? 1 : 0;
	rtt_results->TOF_type = evp->TOF_type;
	if (evp->TOF_type == TOF_TYPE_ONE_WAY) {
		/* convert to 100kbps unit */
		rtt_results->tx_rate.bitrate = WL_RATE_6M * 5;
		rtt_results->tx_rate.rateMcsIdx = WL_RATE_6M;
	} else {
		rtt_results->tx_rate.bitrate = WL_RATE_6M * 5;
		rtt_results->tx_rate.rateMcsIdx = 0; /* MCS 0 */
	}
	memset(diststr, 0, sizeof(diststr));
	if (rtt_results->distance == 0xffffffff || rtt_results->distance == 0) {
		sprintf(diststr, "distance=-1m\n");
	} else {
		sprintf(diststr, "distance=%d.%d m\n",
			rtt_results->distance >> 4, ((rtt_results->distance & 0xf) * 125) >> 1);
	}

	if (ntoh32(evp->mode) == WL_PROXD_MODE_INITIATOR) {
		DHD_RTT(("Target:(%s) %s;\n", bcm_ether_ntoa((&evp->peer_mac), eabuf), diststr));
		DHD_RTT(("RTT : mean %d mode %d median %d\n", rtt_results->meanrtt,
			rtt_results->modertt, rtt_results->medianrtt));
	} else {
		DHD_RTT(("Initiator:(%s) %s; ", bcm_ether_ntoa((&evp->peer_mac), eabuf), diststr));
	}
	if (rtt_results->sdrtt > 0) {
		DHD_RTT(("sigma:%d.%d\n", rtt_results->sdrtt/10, rtt_results->sdrtt % 10));
	} else {
		DHD_RTT(("sigma:0\n"));
	}

	DHD_RTT(("rssi:%d validfrmcnt %d, err_code : %d\n", rtt_results->avg_rssi,
		rtt_results->validfrmcnt, evp->err_code));

	switch (evp->err_code) {
	case TOF_REASON_OK:
		rtt_results->err_code = RTT_REASON_SUCCESS;
		break;
	case TOF_REASON_TIMEOUT:
		rtt_results->err_code = RTT_REASON_TIMEOUT;
		break;
	case TOF_REASON_NOACK:
		rtt_results->err_code = RTT_REASON_NO_RSP;
		break;
	case TOF_REASON_ABORT:
		rtt_results->err_code = RTT_REASON_ABORT;
		break;
	default:
		rtt_results->err_code = RTT_REASON_FAILURE;
		break;
	}
	rtt_results->peer_mac = evp->peer_mac;
	/* get the time elapsed from boot time */
	get_monotonic_boottime(&ts);
	rtt_results->ts = (uint64) TIMESPEC_TO_US(ts);

	for (i = 0; i < rtt_results->ftm_cnt; i++) {
		rtt_results->ftm_buff[i].value = ltoh32(evp->ftm_buff[i].value);
		rtt_results->ftm_buff[i].rssi = ltoh32(evp->ftm_buff[i].rssi);
	}
	return err;
}

int
dhd_rtt_event_handler(dhd_pub_t *dhd, wl_event_msg_t *event, void *event_data)
{
	int err = BCME_OK;
	int len = 0;
	int idx;
	uint status, event_type, flags, reason, ftm_cnt;
	rtt_status_info_t *rtt_status;
	wl_proxd_event_data_t* evp;
	struct rtt_noti_callback *iter;
	rtt_result_t *rtt_result, *entry, *next;
	gfp_t kflags;
	NULL_CHECK(dhd, "dhd is NULL", err);
	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	event_type = ntoh32_ua((void *)&event->event_type);
	flags = ntoh16_ua((void *)&event->flags);
	status = ntoh32_ua((void *)&event->status);
	reason = ntoh32_ua((void *)&event->reason);

	if (event_type != WLC_E_PROXD) {
		goto exit;
	}
	kflags = in_softirq()? GFP_ATOMIC : GFP_KERNEL;
	evp = (wl_proxd_event_data_t*)event_data;
	DHD_RTT(("%s enter : mode: %s, reason :%d \n", __FUNCTION__,
		(ntoh16(evp->mode) == WL_PROXD_MODE_INITIATOR)?
		"initiator":"target", reason));
	switch (reason) {
	case WLC_E_PROXD_STOP:
		DHD_RTT(("WLC_E_PROXD_STOP\n"));
		break;
	case WLC_E_PROXD_ERROR:
	case WLC_E_PROXD_COMPLETED:
		if (reason == WLC_E_PROXD_ERROR) {
			DHD_RTT(("WLC_E_PROXD_ERROR\n"));
		} else {
			DHD_RTT(("WLC_E_PROXD_COMPLETED\n"));
		}

		if (!in_atomic()) {
			mutex_lock(&rtt_status->rtt_mutex);
		}
		ftm_cnt = ntoh16(evp->ftm_cnt);

		if (ftm_cnt > 0) {
			len = OFFSETOF(rtt_result_t, ftm_buff);
		} else {
			len = sizeof(rtt_result_t);
		}
		/* check whether the results is already reported or not */
		list_for_each_entry(entry, &rtt_status->rtt_results_cache, list) {
			if (!memcmp(&entry->peer_mac, &evp->peer_mac, ETHER_ADDR_LEN))	{
				if (!in_atomic()) {
					mutex_unlock(&rtt_status->rtt_mutex);
				}
				goto exit;
			}
		}
		rtt_result = kzalloc(len + sizeof(ftm_sample_t) * ftm_cnt, kflags);
		if (!rtt_result) {
			if (!in_atomic()) {
				mutex_unlock(&rtt_status->rtt_mutex);
			}
			err = -ENOMEM;
			goto exit;
		}
		/* point to target_info in status struct and increase pointer */
		rtt_result->target_info = &rtt_status->rtt_config.target_info[rtt_status->cur_idx];
		/* find next target to trigger RTT */
		for (idx = (rtt_status->cur_idx + 1);
			idx < rtt_status->rtt_config.rtt_target_cnt; idx++) {
			/* skip the disabled device */
			if (rtt_status->rtt_config.target_info[idx].disable) {
				continue;
			} else {
				/* set the idx to cur_idx */
				rtt_status->cur_idx = idx;
				break;
			}
		}
		/* convert the event results to host format */
		dhd_rtt_convert_to_host(rtt_result, evp);
		list_add_tail(&rtt_result->list, &rtt_status->rtt_results_cache);
		if (idx < rtt_status->rtt_config.rtt_target_cnt) {
			/* restart to measure RTT from next device */
			schedule_work(&rtt_status->work);
		} else {
			DHD_RTT(("RTT_STOPPED\n"));
			rtt_status->status = RTT_STOPPED;
			/* to turn on mpc mode */
			schedule_work(&rtt_status->work);
			/* notify the completed information to others */
			list_for_each_entry(iter, &rtt_status->noti_fn_list, list) {
				iter->noti_fn(iter->ctx, &rtt_status->rtt_results_cache);
			}
			/* remove the rtt results in cache */
			list_for_each_entry_safe(rtt_result, next,
				&rtt_status->rtt_results_cache, list) {
				list_del(&rtt_result->list);
				kfree(rtt_result);
			}
			/* reinit the HEAD */
			INIT_LIST_HEAD(&rtt_status->rtt_results_cache);
			/* clear information for rtt_config */
			bzero(&rtt_status->rtt_config, sizeof(rtt_status->rtt_config));
			rtt_status->cur_idx = 0;
		}
		if (!in_atomic()) {
			mutex_unlock(&rtt_status->rtt_mutex);
		}

		break;
	case WLC_E_PROXD_GONE:
		DHD_RTT(("WLC_E_PROXD_GONE\n"));
		break;
	case WLC_E_PROXD_START:
		/* event for targets / accesspoints  */
		DHD_RTT(("WLC_E_PROXD_START\n"));
		break;
	case WLC_E_PROXD_COLLECT_START:
		DHD_RTT(("WLC_E_PROXD_COLLECT_START\n"));
		break;
	case WLC_E_PROXD_COLLECT_STOP:
		DHD_RTT(("WLC_E_PROXD_COLLECT_STOP\n"));
		break;
	case WLC_E_PROXD_COLLECT_COMPLETED:
		DHD_RTT(("WLC_E_PROXD_COLLECT_COMPLETED\n"));
		break;
	case WLC_E_PROXD_COLLECT_ERROR:
		DHD_RTT(("WLC_E_PROXD_COLLECT_ERROR; "));
		break;
	default:
		DHD_ERROR(("WLC_E_PROXD: supported EVENT reason code:%d\n", reason));
		break;
	}

exit:
	return err;
}

static void
dhd_rtt_work(struct work_struct *work)
{
	rtt_status_info_t *rtt_status;
	dhd_pub_t *dhd;
	rtt_status = container_of(work, rtt_status_info_t, work);
	if (rtt_status == NULL) {
		DHD_ERROR(("%s : rtt_status is NULL\n", __FUNCTION__));
		return;
	}
	dhd = rtt_status->dhd;
	if (dhd == NULL) {
		DHD_ERROR(("%s : dhd is NULL\n", __FUNCTION__));
		return;
	}
	(void) dhd_rtt_start(dhd);
}

int
dhd_rtt_capability(dhd_pub_t *dhd, rtt_capabilities_t *capa)
{
	rtt_status_info_t *rtt_status;
	int err = BCME_OK;
	NULL_CHECK(dhd, "dhd is NULL", err);
	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	NULL_CHECK(capa, "capa is NULL", err);
	bzero(capa, sizeof(rtt_capabilities_t));

	if (rtt_status->capability & RTT_CAP_ONE_WAY) {
		capa->rtt_one_sided_supported = 1;
	}
	if (rtt_status->capability & RTT_CAP_11V_WAY) {
		capa->rtt_11v_supported = 1;
	}
	if (rtt_status->capability & RTT_CAP_11MC_WAY) {
		capa->rtt_ftm_supported = 1;
	}
	if (rtt_status->capability & RTT_CAP_VS_WAY) {
		capa->rtt_vs_supported = 1;
	}

	return err;
}

int
dhd_rtt_init(dhd_pub_t *dhd)
{
	int err = BCME_OK;
	rtt_status_info_t *rtt_status;
	NULL_CHECK(dhd, "dhd is NULL", err);
	if (dhd->rtt_state) {
		goto exit;
	}
	dhd->rtt_state = MALLOC(dhd->osh, sizeof(rtt_status_info_t));
	if (dhd->rtt_state == NULL) {
		DHD_ERROR(("failed to create rtt_state\n"));
		goto exit;
	}
	bzero(dhd->rtt_state, sizeof(rtt_status_info_t));
	rtt_status = GET_RTTSTATE(dhd);
	rtt_status->dhd = dhd;
	err = dhd_iovar(dhd, 0, "proxd_params", NULL, 0, 1);
	if (err != BCME_UNSUPPORTED) {
		rtt_status->capability |= RTT_CAP_ONE_WAY;
		rtt_status->capability |= RTT_CAP_VS_WAY;
		DHD_ERROR(("%s: Support RTT Service\n", __FUNCTION__));
	}
	mutex_init(&rtt_status->rtt_mutex);
	INIT_LIST_HEAD(&rtt_status->noti_fn_list);
	INIT_LIST_HEAD(&rtt_status->rtt_results_cache);
	INIT_WORK(&rtt_status->work, dhd_rtt_work);
exit:
	return err;
}

int
dhd_rtt_deinit(dhd_pub_t *dhd)
{
	int err = BCME_OK;
	rtt_status_info_t *rtt_status;
	rtt_result_t *rtt_result, *next;
	struct rtt_noti_callback *iter, *iter2;
	NULL_CHECK(dhd, "dhd is NULL", err);
	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	rtt_status->status = RTT_STOPPED;
	/* clear evt callback list */
	if (!list_empty(&rtt_status->noti_fn_list)) {
		list_for_each_entry_safe(iter, iter2, &rtt_status->noti_fn_list, list) {
			list_del(&iter->list);
			kfree(iter);
		}
	}
	/* remove the rtt results */
	if (!list_empty(&rtt_status->rtt_results_cache)) {
		list_for_each_entry_safe(rtt_result, next, &rtt_status->rtt_results_cache, list) {
			list_del(&rtt_result->list);
			kfree(rtt_result);
		}
	}
	MFREE(dhd->osh, dhd->rtt_state, sizeof(rtt_status_info_t));
	dhd->rtt_state = NULL;
	return err;
}
#endif /* RTT_SUPPORT */
