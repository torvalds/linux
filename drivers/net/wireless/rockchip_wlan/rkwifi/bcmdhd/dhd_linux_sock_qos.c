/*
 * Source file for DHD QOS on Socket Flow.
 *
 * Defines a socket flow and maintains a table of socket flows
 * for further analysis in order to upgrade the QOS of the flow.

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
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 *
 */

#include <dhd_linux_priv.h>
#include <dhd_dbg.h>
#include <bcmstdlib_s.h>
#include <bcmendian.h>
#include <dhd_linux_sock_qos.h>
#include <dhd_qos_algo.h>
#include <dhd.h>

#include <net/sock.h>
#include <linux/sock_diag.h>
#include <linux/netlink.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/math64.h>
#include <linux/pkt_sched.h>
#include <linux_pkt.h>
#include <net/tcp.h>

/* Maximum number of Socket Flows supported */
#define MAX_SOCK_FLOW	(1024UL)

#define SOCK_FLOW_UPGRADE_THRESHOLD	(3)
/*
 * Mark a Socket Flow as inactive and free the resources
 * if there is no packet receied for SOCK_IDLE_THREASHOLD_MS
 * of time. Note that this parameter is in milli seconds.
 */
#define SOCK_IDLE_THRESHOLD_MS	(2000UL)

#define DSCP_TOS_CS7 0XE0u

extern uint dhd_watchdog_ms;

/* Defines Socket Flow */
struct dhd_sock_flow_info
{
	/* Unique identifiers */
	struct sock *sk;
	unsigned long ino;

	/* statistics */
	qos_stat_t stats;
	u64 last_pkt_ns;
	kuid_t uid;

	/* Elements related to upgrade management */

	/* 0 - No upgrade
	 * 1 - Upgrade
	 */
	unsigned int cur_up_state;
	unsigned int rcm_up_state;
	unsigned int bus_flow_id;

	/* TODO:
	 * Handling Out Of Order during upgrade
	 * Once an upgrade is decided we cannot handover the skb to
	 * FW in the upgraded Flow Ring ... it will create Out of Order Packets.
	 * Instead we can have a output_q per socket flow. Once the upgrade is
	 * decided, we can start adding skbs to the output_q. The last 'skb' given
	 * to the actual Flow ring should be remembered in 'last_skb_orig_fl'.
	 * Once we get a  Tx completion for last_skb_orig_fl we can flush the
	 * contents of output_q to the 'upgraded flowring'. In this solution,
	 * we should also handle the case where output_q hits the watermark
	 * before the completion for 'last_skb_orig_fl' is received. If this condition
	 * happens, not to worry about OOO and flush the contents of output_q.
	 * Probably the last_skb_orig_fl is not sent out due latency in the
	 * existing flow ... the actual problem we are trying to solve.
	 */

	/* Management elements */
	struct list_head list;
	unsigned int in_use;
};

typedef enum _frameburst_state
{
	FRMBRST_DISABLED = 0,
	FRMBRST_ENABLED = 1
} frameburst_state_t;

/* Sock QOS Module Structure */
typedef struct dhd_sock_qos_info
{
	/* Table of Socket Flows */
	struct dhd_sock_flow_info *sk_fl;
	/* maximum number for socket flows supported */
	uint32 max_sock_fl;

	/* TODO: need to make it per flow later on */
	/* global qos algo parameters */
	qos_algo_params_t qos_params;
	/* List in which active Socket Flows live */
	struct list_head sk_fl_list_head;
	void *list_lock;

	/* Time interval a socket flow resource is moved out of the active list */
	uint32 sock_idle_thresh;
	/*
	 * Keep track of number of flows upgraded.
	 * If it reaches a threshold we should stop ugrading
	 * This is to avoid the problem where we overwhelm
	 * the Dongle with upgraded traffic.
	 */
	int num_skfl_upgraded;
	int skfl_upgrade_thresh;

	/* flag that is set to true when the first flow is upgraded
	 * so that FW frameburst is disabled, and set to false
	 * when no more flows are in upgraded state, so that
	 * FW frameburst is re-enabled
	 */
	bool upgrade_active;
	/* fw frameburst state */
	frameburst_state_t frmbrst_state;

	atomic_t on_off;
	atomic_t force_upgrade;

	/* required for enabling/disabling watchdog timer at runtime */
	uint watchdog_ms;
} dhd_sock_qos_info_t;

#define SK_FL_LIST_LOCK(lock, flags)	(flags) = osl_spin_lock(lock)
#define SK_FL_LIST_UNLOCK(lock, flags)	osl_spin_unlock((lock), (flags))

int
dhd_init_sock_flows_buf(dhd_info_t *dhd, uint watchdog_ms)
{
	unsigned long sz;
	unsigned int i;
	struct dhd_sock_flow_info *sk_fl = NULL;
	int val = 0, ret = 0;

	if (dhd == NULL)
		return BCME_BADARG;

	dhd->psk_qos = MALLOCZ(dhd->pub.osh, sizeof(dhd_sock_qos_info_t));
	if (dhd->psk_qos == NULL) {
		DHD_ERROR(("%s(): Failed to alloc psk_qos ! \n", __FUNCTION__));
		return BCME_NOMEM;
	}
	dhd->psk_qos->max_sock_fl = MAX_SOCK_FLOW;
	sz = sizeof(struct dhd_sock_flow_info) * MAX_SOCK_FLOW;
	dhd->psk_qos->sk_fl = MALLOCZ(dhd->pub.osh, sz);
	if (dhd->psk_qos->sk_fl == NULL) {
		DHD_ERROR(("%s(): Failed to allocated sk_fl \r\n", __FUNCTION__));
		return BCME_NOMEM;
	}

	sk_fl = dhd->psk_qos->sk_fl;
	for (i = 0; i < MAX_SOCK_FLOW; i++, sk_fl++) {
		sk_fl->in_use = 0;
	}

	dhd->psk_qos->sock_idle_thresh = SOCK_IDLE_THRESHOLD_MS;

	dhd->psk_qos->skfl_upgrade_thresh = SOCK_FLOW_UPGRADE_THRESHOLD;

	INIT_LIST_HEAD(&dhd->psk_qos->sk_fl_list_head);
	dhd->psk_qos->list_lock = osl_spin_lock_init(dhd->pub.osh);

	dhd->psk_qos->watchdog_ms = watchdog_ms;
	/* feature is DISABLED by default */
	dhd_sock_qos_set_status(dhd, 0);

	qos_algo_params_init(&dhd->psk_qos->qos_params);

	dhd->psk_qos->frmbrst_state = FRMBRST_ENABLED;
	/* read the initial state of frameburst from FW, cannot
	 * assume that it will always be in enabled state by default.
	 * We will cache the FW frameburst state in host and change
	 * it everytime we change it from host during QoS upgrade.
	 * This decision is taken, because firing an iovar everytime
	 * to query FW frameburst state before deciding whether to
	 * changing the frameburst state or not from host, is sub-optimal,
	 * especially in the Tx path.
	 */
	ret = dhd_wl_ioctl_cmd(&dhd->pub, WLC_SET_FAKEFRAG, (char *)&val,
		sizeof(val), FALSE, 0);
	if (ret != BCME_OK) {
		DHD_ERROR(("%s: get fw frameburst failed,"
			" err=%d\n", __FUNCTION__, ret));
	} else {
		DHD_INFO(("%s:fw frameburst = %d", __FUNCTION__, val));
		dhd->psk_qos->frmbrst_state =
			(val == 1) ? FRMBRST_ENABLED : FRMBRST_DISABLED;
	}
	return BCME_OK;
}

int
dhd_deinit_sock_flows_buf(dhd_info_t *dhd)
{
	if (dhd == NULL)
		return BCME_BADARG;

	if (dhd->psk_qos->sk_fl) {
		MFREE(dhd->pub.osh, dhd->psk_qos->sk_fl,
			sizeof(struct dhd_sock_flow_info) * MAX_SOCK_FLOW);
		dhd->psk_qos->sk_fl = NULL;
	}

	osl_spin_lock_deinit(dhd->pub.osh, dhd->psk_qos->list_lock);
	MFREE(dhd->pub.osh, dhd->psk_qos, sizeof(dhd_sock_qos_info_t));
	dhd->psk_qos = NULL;

	return BCME_OK;
}

/* Caller should hold list_lock */
static inline struct dhd_sock_flow_info *
__dhd_find_sock_stream_info(dhd_sock_qos_info_t *psk_qos, unsigned long ino)
{
	struct dhd_sock_flow_info *sk_fl = NULL;
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	list_for_each_entry(sk_fl, &psk_qos->sk_fl_list_head,
			list)  {
		if (sk_fl && (sk_fl->ino == ino)) {
			return sk_fl;
		}
	} /* end of list iteration */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	/* If control comes here, the ino is not found */
	DHD_INFO(("%s(): ino:%lu not found \r\n", __FUNCTION__, ino));

	return NULL;
}

static struct dhd_sock_flow_info *
dhd_alloc_sock_stream_info(dhd_sock_qos_info_t *psk_qos)
{
	struct dhd_sock_flow_info *sk_fl = psk_qos->sk_fl;
	int i;

	for (i = 0; i < psk_qos->max_sock_fl; i++, sk_fl++) {
		if (sk_fl->in_use == 0) {
			DHD_ERROR(("%s: Use sk_fl %p \r\n", __FUNCTION__, sk_fl));
			return sk_fl;
		}
	}
	DHD_INFO(("No Free Socket Stream info \r\n"));
	return NULL;
}

/* Caller should hold list_lock */
static inline void
__dhd_free_sock_stream_info(dhd_sock_qos_info_t *psk_qos,
	struct dhd_sock_flow_info *sk_fl)
{
	/*
	 * If the socket flow getting freed is an upgraded socket flow,
	 * we can upgrade one more flow.
	 */
	if (sk_fl->cur_up_state == 1) {
		--psk_qos->num_skfl_upgraded;
		ASSERT(psk_qos->num_skfl_upgraded >= 0);
	}

	/* Remove the flow from active list */
	list_del(&sk_fl->list);

	DHD_ERROR(("%s(): Cleaning Socket Flow ino:%lu psk_qos->num_skfl_upgraded=%d\r\n",
		__FUNCTION__, sk_fl->ino, psk_qos->num_skfl_upgraded));

	/* Clear its content */
	memset_s(sk_fl, sizeof(*sk_fl), 0, sizeof(*sk_fl));

	return;
}

static void
dhd_clean_idle_sock_streams(dhd_sock_qos_info_t *psk_qos)
{
	struct dhd_sock_flow_info *sk_fl = NULL, *next = NULL;
	u64 now;
	u64 diff;
	unsigned long flags = 0;
	now = local_clock();

	SK_FL_LIST_LOCK(psk_qos->list_lock, flags);

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	list_for_each_entry_safe(sk_fl, next, &psk_qos->sk_fl_list_head, list)  {
		if (sk_fl) {

			if (sk_fl->in_use == 0) {
				DHD_ERROR_RLMT(("%s:Something wrong,"
					" a free sk_fl living in active stream\n",
					__FUNCTION__));
				DHD_ERROR_RLMT(("sk_fl:%p sk:%p ino:%lu \r\n",
					sk_fl, sk_fl->sk, sk_fl->ino));
				continue;
			}

			/* XXX: TODO: need to investigate properly in future.
			 * it is observed that in some hosts (FC25), the
			 * current timestamp is lesser than previous timestamp
			 * leading to false cleanups
			 */
			if (now <= sk_fl->last_pkt_ns)
				continue;

			diff = now - sk_fl->last_pkt_ns;

			/* Convert diff which is in ns to ms */
			diff = div64_u64(diff, 1000000UL);
			if (diff >= psk_qos->sock_idle_thresh) {
				DHD_ERROR(("sk_fl->sk:%p sk_fl->i_no:%lu \r\n",
					sk_fl->sk, sk_fl->ino));
				if (sk_fl->cur_up_state == 1 &&
					psk_qos->num_skfl_upgraded == 1) {
					psk_qos->upgrade_active = FALSE;
				}
				__dhd_free_sock_stream_info(psk_qos, sk_fl);
			}
		}
	} /* end of list iteration */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	SK_FL_LIST_UNLOCK(psk_qos->list_lock, flags);

}

static inline int
__dhd_upgrade_sock_flow(dhd_info_t *dhd,
	struct dhd_sock_flow_info *sk_fl,
	struct sk_buff *skb)
{
	dhd_sock_qos_info_t *psk_qos = dhd->psk_qos;
#ifdef DHD_HP2P
	dhd_pub_t *dhdp = &dhd->pub;
#endif
	uint8 *pktdat = NULL;
	struct ether_header *eh = NULL;
	struct iphdr *iph = NULL;

	/* Before upgrading a flow,
	 * Check the bound to control the number of flows getting upgraded
	 */
	if ((sk_fl->rcm_up_state == 1) && (sk_fl->cur_up_state == 0)) {
		if (psk_qos->num_skfl_upgraded >= psk_qos->skfl_upgrade_thresh) {
			DHD_ERROR_RLMT(("%s(): Thresh hit num_skfl_upgraded:%d"
				"skfl_upgrade_thresh:%d \r\n",
				__FUNCTION__, psk_qos->num_skfl_upgraded,
				psk_qos->skfl_upgrade_thresh));
			return BCME_ERROR;
		} else {
			if (psk_qos->num_skfl_upgraded == 0) {
				/* if no flows upgraded till now, and this is the
				 * first flow to be upgraded,
				 * then disable frameburst in FW.
				 * The actual iovar to disable frameburst cannot
				 * be fired here because Tx can happen in atomic context
				 * and dhd_iovar can sleep due to proto_block lock being
				 * held. Instead the flag is checked from
				 * 'dhd_analyze_sock_flows' which execs in non-atomic context
				 * and the iovar is fired from there
				 */
				DHD_TRACE(("%s: disable frameburst ..", __FUNCTION__));
				psk_qos->upgrade_active = TRUE;
			}
			++psk_qos->num_skfl_upgraded;
			DHD_ERROR_RLMT(("%s(): upgrade flow sk_fl %p,"
				"num_skfl_upgraded:%d skfl_upgrade_thresh:%d \r\n",
				__FUNCTION__, sk_fl, psk_qos->num_skfl_upgraded,
				psk_qos->skfl_upgrade_thresh));
		}
	}

	/* Upgrade the skb */
#ifdef DHD_HP2P
	if (dhdp->hp2p_capable)
		skb->priority = TC_PRIO_CONTROL;
	else
		skb->priority = TC_PRIO_INTERACTIVE;
#else
	skb->priority = TC_PRIO_INTERACTIVE;
#endif /* DHD_HP2P  */

	pktdat = PKTDATA(dhd->pub.osh, skb);
	eh = (struct ether_header *) pktdat;
	if (pktdat && (eh->ether_type == hton16(ETHER_TYPE_IP))) {
		/* 'upgrade' DSCP also, else it is observed that on
		 * AP side if DSCP value is not in sync with L2 prio
		 * then out of order packets are observed
		 */
		iph = (struct iphdr *)(pktdat + sizeof(struct ether_header));
		iph->tos = DSCP_TOS_CS7;
		/* re-compute ip hdr checksum
		 * NOTE: this takes around 1us, need to profile more
		 * accurately to get the number of cpu cycles it takes
		 * in order to get a better idea of the impact of
		 * re computing ip hdr chksum in data path
		 */
		ip_send_check(iph);
	 }

	/* Mark the Flow as 'upgraded' */
	if (sk_fl->cur_up_state == 0)
		sk_fl->cur_up_state = 1;

	return BCME_OK;
}

static inline int
__dhd_downgrade_sock_flow(dhd_info_t *dhd,
	struct dhd_sock_flow_info *sk_fl,
	struct sk_buff *skb)
{
	dhd_sock_qos_info_t *psk_qos = dhd->psk_qos;

	if ((sk_fl->rcm_up_state == 0) && (sk_fl->cur_up_state == 1)) {
		/* sanity check */
		ASSERT(psk_qos->num_skfl_upgraded > 0);
		if (psk_qos->num_skfl_upgraded <= 0) {
			DHD_ERROR_RLMT(("%s(): FATAL ! no upgraded flows !\n",
					__FUNCTION__));
			return BCME_ERROR;
		}

		if (psk_qos->num_skfl_upgraded == 1) {
			/* if this is the
			 * last flow to be downgraded,
			 * then re-enable frameburst in FW.
			 * The actual iovar to enable frameburst cannot
			 * be fired here because Tx can happen in atomic context
			 * and dhd_iovar can sleep due to proto_block lock being
			 * held. Instead the flag is checked from
			 * 'dhd_analyze_sock_flows' which execs in non-atomic context
			 * and the iovar is fired from there
			 */
			DHD_TRACE(("%s: enable frameburst ..", __FUNCTION__));
			psk_qos->upgrade_active = FALSE;
		}
		--psk_qos->num_skfl_upgraded;
		DHD_ERROR_RLMT(("%s(): downgrade flow sk_fl %p,"
			"num_skfl_upgraded:%d \r\n",
			__FUNCTION__, sk_fl, psk_qos->num_skfl_upgraded));
	}

	/* Mark the Flow as 'downgraded' */
	if (sk_fl->cur_up_state == 1)
		sk_fl->cur_up_state = 0;

	return BCME_OK;
}

/*
 * Update the stats of a Socket flow.
 * Create a new flow if need be.
 * If a socket flow has been recommended for upgrade, do so.
 */
void
dhd_update_sock_flows(dhd_info_t *dhd, struct sk_buff *skb)
{
	struct sock *sk = NULL;
	unsigned long ino = 0;
	struct dhd_sock_flow_info *sk_fl = NULL;
	dhd_sock_qos_info_t *psk_qos = NULL;
	unsigned long flags = 0;
	uint8 prio;

	BCM_REFERENCE(prio);

	if ((dhd == NULL) || (skb == NULL)) {
		DHD_ERROR_RLMT(("%s: Invalid args \n", __FUNCTION__));
		return;
	}

	/* If the Feature is disabled, return */
	if (dhd_sock_qos_get_status(dhd) == 0)
		return;

	psk_qos = dhd->psk_qos;
	sk = (struct sock *)PKTSOCK(dhd->pub.osh, skb);

	/* TODO:
	 * Some times sk is NULL, what does that mean ...
	 * is it a broadcast packet generated by Network Stack ????
	 */
	if (sk == NULL) {
		return;
	}
	ino = sock_i_ino(sk);

	/* TODO:
	 * List Lock need not be held for allocating sock stream .. optimize
	 */
	SK_FL_LIST_LOCK(psk_qos->list_lock, flags);

	sk_fl = __dhd_find_sock_stream_info(psk_qos, ino);
	if (sk_fl == NULL) {
		/* Allocate new sock stream */
		sk_fl = dhd_alloc_sock_stream_info(psk_qos);
		if (sk_fl == NULL) {
			SK_FL_LIST_UNLOCK(psk_qos->list_lock, flags);
			goto done;
		}
		else {
			/* SK Flow elements updated first time */
			sk_fl->in_use = 1;
			sk_fl->sk = sk;
			sk_fl->ino = ino;
			/* TODO: Seeing a Kernel Warning ... check */
			/* sk_fl->uid = sock_i_uid(sk); */
			sk_fl->cur_up_state = 0;
			list_add_tail(&sk_fl->list, &psk_qos->sk_fl_list_head);
			DHD_ERROR(("%s(): skb %p sk %p sk_fl %p ino %lu"
				" prio 0x%x \r\n", __FUNCTION__, skb,
				sk, sk_fl, ino, skb->priority));
		} /* end of new sk flow allocation */
	} /* end of case when sk flow is found */

	sk_fl->stats.tx_pkts++;
	sk_fl->stats.tx_bytes += skb->len;
	sk_fl->last_pkt_ns = local_clock();

	SK_FL_LIST_UNLOCK(psk_qos->list_lock, flags);

	if (sk_fl->rcm_up_state == 1) {
		__dhd_upgrade_sock_flow(dhd, sk_fl, skb);
	} else {
		__dhd_downgrade_sock_flow(dhd, sk_fl, skb);
	}

	prio = PKTPRIO(skb);
	DHD_INFO(("%s(): skb:%p skb->priority 0x%x prio %d sk_fl %p\r\n", __FUNCTION__, skb,
		skb->priority, prio, sk_fl));
done:
	return;
}

static int
dhd_change_frameburst_state(frameburst_state_t newstate, dhd_info_t *dhd)
{
	int ret = 0, val = 0;
	dhd_sock_qos_info_t *psk_qos = NULL;

	if (!dhd)
		return BCME_BADARG;
	if (!dhd->psk_qos)
		return BCME_BADARG;

	psk_qos = dhd->psk_qos;

	/* Check with the cached frameburst state on host
	 * instead of querying FW frameburst state.
	 * This decision is taken, because firing an iovar everytime
	 * to query FW frameburst state before deciding whether to
	 * changing the frameburst state or not is sub-optimal,
	 * especially in the Tx path.
	 */
	if (psk_qos->frmbrst_state == newstate)
		return BCME_BADOPTION;

	val = (newstate == FRMBRST_ENABLED) ? 1 : 0;
	ret = dhd_wl_ioctl_cmd(&dhd->pub, WLC_SET_FAKEFRAG, (char *)&val,
		sizeof(val), TRUE, 0);
	if (ret != BCME_OK) {
		DHD_ERROR_RLMT(("%s: set frameburst=%d failed,"
			" err=%d\n", __FUNCTION__, val, ret));
	} else {
		/* change the state */
		DHD_INFO(("%s: set frameburst=%d\n", __FUNCTION__, val));
		psk_qos->frmbrst_state = newstate;
	}

	return ret;
}

void dhd_analyze_sock_flows(dhd_info_t *dhd, uint32 watchdog_ms)
{
	struct dhd_sock_flow_info *sk_fl = NULL;
	dhd_sock_qos_info_t *psk_qos = NULL;
	unsigned long flags = 0;

	if (dhd == NULL) {
		DHD_ERROR_RLMT(("%s: Bad argument \r\n", __FUNCTION__));
		return;
	}

	/* Check whether the feature is disabled */
	if (dhd_sock_qos_get_status(dhd) == 0)
		return;

	psk_qos = dhd->psk_qos;

	dhd_clean_idle_sock_streams(dhd->psk_qos);

	/* TODO: Plug in the QoS Algorithm here */
	SK_FL_LIST_LOCK(psk_qos->list_lock, flags);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	list_for_each_entry(sk_fl, &psk_qos->sk_fl_list_head, list)  {

		sk_fl->rcm_up_state = dhd_qos_algo(dhd, &sk_fl->stats, &psk_qos->qos_params);

		/* TODO: Handle downgrades */

		/* update sk_flow previous elements on every sampling interval */
		sk_fl->stats.tx_pkts_prev = sk_fl->stats.tx_pkts;
		sk_fl->stats.tx_bytes_prev = sk_fl->stats.tx_bytes;

		/* TODO: Handle the condition where num_skfl_upgraded reaches the threshold */

		/* TODO: Handle the condition where we upgrade all the socket flows
		 * of the uid on which one flow is detected to be upgraded.
		 */

	} /* end of list iteration */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	SK_FL_LIST_UNLOCK(psk_qos->list_lock, flags);

	/* disable frameburst in FW on the first flow upgraded */
	if (psk_qos->upgrade_active) {
		dhd_change_frameburst_state(FRMBRST_DISABLED, dhd);
	} else {
		/* if no upgraded flows remain, either after cleanup,
		 * or after a downgrade,
		 * then re-enable frameburst in FW
		 */
		dhd_change_frameburst_state(FRMBRST_ENABLED, dhd);
	}

	return;
}

void dhd_sock_qos_update_bus_flowid(dhd_info_t *dhd, void *pktbuf,
	uint32 bus_flow_id)
{
	BCM_REFERENCE(dhd);
	BCM_REFERENCE(pktbuf);
	BCM_REFERENCE(bus_flow_id);
	return;
}

/* ================= Sysfs interfce support functions ======================== */

unsigned long dhd_sock_qos_get_status(dhd_info_t *dhd)
{
	if (dhd == NULL)
		return 0;

	return (atomic_read(&dhd->psk_qos->on_off));
}

void dhd_sock_qos_set_status(dhd_info_t *dhd, unsigned long on_off)
{
	if (dhd == NULL)
		return;

	atomic_set(&dhd->psk_qos->on_off, on_off);
	if (on_off) {
		dhd_watchdog_ms = QOS_SAMPLING_INTVL_MS;
		/* enable watchdog to monitor the socket flows */
		dhd_os_wd_timer(&dhd->pub, QOS_SAMPLING_INTVL_MS);
	} else {
		dhd_watchdog_ms = dhd->psk_qos->watchdog_ms;
		/* disable watchdog or set it back to the original value */
		dhd_os_wd_timer(&dhd->pub, dhd->psk_qos->watchdog_ms);
	}
	return;
}

ssize_t dhd_sock_qos_show_stats(dhd_info_t *dhd, char *buf,
	ssize_t sz)
{
	dhd_sock_qos_info_t *psk_qos = NULL;
	struct dhd_sock_flow_info *sk_fl = NULL;
	unsigned long flags = 0;
	ssize_t	ret = 0;
	char *p = buf;

	/* TODO: Should be actual record length */
	unsigned long rec_len = 100;

	if (dhd == NULL)
		return -1;

	psk_qos = dhd->psk_qos;

	ret += scnprintf(p, sz-ret-1, "\nino\t sk\t\t\t tx_pkts\t tx_bytes\t"
		"last_pkt_ns\r\n");
	p += ret;

	SK_FL_LIST_LOCK(psk_qos->list_lock, flags);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	list_for_each_entry(sk_fl, &psk_qos->sk_fl_list_head, list)  {
		/* Protect the buffer from over run */
		if (ret + rec_len >= sz)
			break;

		ret += scnprintf(p, sz-ret-1, "%lu\t %p\t %lu\t %lu\t %llu\t \r\n",
			sk_fl->ino, sk_fl->sk, sk_fl->stats.tx_pkts, sk_fl->stats.tx_bytes,
			sk_fl->last_pkt_ns);

		p += ret;

	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	SK_FL_LIST_UNLOCK(psk_qos->list_lock, flags);

	return ret + 1;
}

void dhd_sock_qos_clear_stats(dhd_info_t *dhd)
{
	dhd_sock_qos_info_t *psk_qos = NULL;
	struct dhd_sock_flow_info *sk_fl = NULL;
	unsigned long flags = 0;

	if (dhd == NULL)
		return;

	psk_qos = dhd->psk_qos;

	SK_FL_LIST_LOCK(psk_qos->list_lock, flags);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	list_for_each_entry(sk_fl, &psk_qos->sk_fl_list_head, list)  {
		sk_fl->stats.tx_pkts = 0;
		sk_fl->stats.tx_bytes = 0;
		sk_fl->stats.tx_pkts_prev = 0;
		sk_fl->stats.tx_bytes_prev = 0;
		sk_fl->last_pkt_ns = 0;
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	SK_FL_LIST_UNLOCK(psk_qos->list_lock, flags);

	return;
}

unsigned long dhd_sock_qos_get_force_upgrade(dhd_info_t *dhd)
{
	if (dhd == NULL)
		return 0;

	return (atomic_read(&dhd->psk_qos->force_upgrade));
}

void dhd_sock_qos_set_force_upgrade(dhd_info_t *dhd, unsigned long force_upgrade)
{
	if (dhd == NULL)
		return;

	atomic_set(&dhd->psk_qos->force_upgrade, force_upgrade);
	return;
}

int dhd_sock_qos_get_numfl_upgrd_thresh(dhd_info_t *dhd)
{
	if (dhd == NULL)
		return 0;

	return dhd->psk_qos->skfl_upgrade_thresh;
}

void dhd_sock_qos_set_numfl_upgrd_thresh(dhd_info_t *dhd,
		int upgrade_thresh)
{
	if (dhd == NULL)
		return;

	dhd->psk_qos->skfl_upgrade_thresh = upgrade_thresh;
	return;
}

void dhd_sock_qos_get_avgpktsize_thresh(dhd_info_t *dhd,
		unsigned long *avgpktsize_low,
		unsigned long *avgpktsize_high)
{
	qos_algo_params_t *pqos_params = NULL;

	if (dhd == NULL || avgpktsize_low == NULL ||
		avgpktsize_high == NULL) {
		return;
	}

	pqos_params = QOS_PARAMS(dhd);
	*avgpktsize_low = pqos_params->avg_pkt_size_low_thresh;
	*avgpktsize_high = pqos_params->avg_pkt_size_high_thresh;
	return;
}

void dhd_sock_qos_set_avgpktsize_thresh(dhd_info_t *dhd,
		unsigned long avgpktsize_low,
		unsigned long avgpktsize_high)
{
	qos_algo_params_t *pqos_params = NULL;

	if (dhd == NULL)
		return;

	pqos_params = QOS_PARAMS(dhd);
	pqos_params->avg_pkt_size_low_thresh = avgpktsize_low;
	pqos_params->avg_pkt_size_high_thresh = avgpktsize_high;
	return;
}

void dhd_sock_qos_get_numpkts_thresh(dhd_info_t *dhd,
		unsigned long *numpkts_low,
		unsigned long *numpkts_high)
{
	qos_algo_params_t *pqos_params = NULL;

	if (dhd == NULL || numpkts_low == NULL ||
		numpkts_high == NULL) {
		return;
	}

	pqos_params = QOS_PARAMS(dhd);
	*numpkts_low = pqos_params->num_pkts_low_thresh;
	*numpkts_high = pqos_params->num_pkts_high_thresh;
}

void dhd_sock_qos_set_numpkts_thresh(dhd_info_t *dhd,
		unsigned long numpkts_low,
		unsigned long numpkts_high)
{
	qos_algo_params_t *pqos_params = NULL;

	if (dhd == NULL)
		return;
	pqos_params = QOS_PARAMS(dhd);
	pqos_params->num_pkts_low_thresh = numpkts_low;
	pqos_params->num_pkts_high_thresh = numpkts_high;
	return;
}

void dhd_sock_qos_get_detectcnt_thresh(dhd_info_t *dhd,
		unsigned char *detectcnt_inc,
		unsigned char *detectcnt_dec)
{
	qos_algo_params_t *pqos_params = NULL;

	if (dhd == NULL || detectcnt_inc == NULL ||
		detectcnt_dec == NULL) {
		return;
	}

	pqos_params = QOS_PARAMS(dhd);
	*detectcnt_inc = pqos_params->detect_cnt_inc_thresh;
	*detectcnt_dec = pqos_params->detect_cnt_dec_thresh;
}

void dhd_sock_qos_set_detectcnt_thresh(dhd_info_t *dhd,
		unsigned char detectcnt_inc,
		unsigned char detectcnt_dec)
{
	qos_algo_params_t *pqos_params = NULL;

	if (dhd == NULL)
		return;

	pqos_params = QOS_PARAMS(dhd);
	pqos_params->detect_cnt_inc_thresh = detectcnt_inc;
	pqos_params->detect_cnt_dec_thresh = detectcnt_dec;
	return;
}

int dhd_sock_qos_get_detectcnt_upgrd_thresh(dhd_info_t *dhd)
{
	qos_algo_params_t *pqos_params = NULL;

	if (dhd == NULL)
		return 0;

	pqos_params = QOS_PARAMS(dhd);
	return pqos_params->detect_cnt_upgrade_thresh;
}

void dhd_sock_qos_set_detectcnt_upgrd_thresh(dhd_info_t *dhd,
		unsigned char detect_upgrd_thresh)
{
	qos_algo_params_t *pqos_params = NULL;

	if (dhd == NULL)
		return;

	pqos_params = QOS_PARAMS(dhd);
	pqos_params->detect_cnt_upgrade_thresh = detect_upgrd_thresh;
}

int dhd_sock_qos_get_maxfl(dhd_info_t *dhd)
{
	if (dhd == NULL)
		return 0;

	return dhd->psk_qos->max_sock_fl;
}

void dhd_sock_qos_set_maxfl(dhd_info_t *dhd,
		unsigned int maxfl)
{
	if (dhd == NULL)
		return;

	dhd->psk_qos->max_sock_fl = maxfl;
}
/* ================= End of Sysfs interfce support functions ======================== */

/* ================= QOS Algorithm ======================== */

/*
 * Operates on a flow and returns 1 for upgrade and 0 for
 * no up-grade - Has the potential of moving into a separate file
 * Takes the dhd pointer too in case if it has to access any platform
 * functions like MALLOC that takes dhd->pub.osh as argument.
 */
int dhd_qos_algo(dhd_info_t *dhd, qos_stat_t *qos, qos_algo_params_t *pqos_params)
{
	unsigned long tx_bytes, tx_pkts, tx_avg_pkt_size;

	if (!dhd || !qos || !pqos_params) {
		return 0;
	}

	/* if the user has set the sysfs variable to force upgrade */
	if (atomic_read(&dhd->psk_qos->force_upgrade) == 1) {
		return 1;
	}

	DHD_TRACE(("%s(): avgpktsize_thrsh %lu:%lu; "
		"numpkts_thrs %lu:%lu; detectcnt_thrs %d:%d;"
		" detectcnt_upgrd_thrs %d\n", __FUNCTION__,
		pqos_params->avg_pkt_size_low_thresh,
		pqos_params->avg_pkt_size_high_thresh,
		pqos_params->num_pkts_low_thresh,
		pqos_params->num_pkts_high_thresh,
		pqos_params->detect_cnt_inc_thresh,
		pqos_params->detect_cnt_dec_thresh,
		pqos_params->detect_cnt_upgrade_thresh));

	tx_bytes = qos->tx_bytes - qos->tx_bytes_prev;
	tx_pkts = qos->tx_pkts - qos->tx_pkts_prev;
	if ((tx_bytes == 0) || (tx_pkts == 0)) {
		return 0;
	}

	tx_avg_pkt_size = tx_bytes / tx_pkts;

	if ((tx_avg_pkt_size > pqos_params->avg_pkt_size_low_thresh) &&
		(tx_avg_pkt_size < pqos_params->avg_pkt_size_high_thresh) &&
		(tx_pkts > pqos_params->num_pkts_low_thresh) &&
		(tx_pkts < pqos_params->num_pkts_high_thresh)) {
		if (qos->lowlat_detect_count < pqos_params->detect_cnt_inc_thresh) {
			qos->lowlat_detect_count++;
		}
	} else if (qos->lowlat_detect_count > pqos_params->detect_cnt_dec_thresh) {
		qos->lowlat_detect_count--;
	}

	if (qos->lowlat_detect_count > pqos_params->detect_cnt_upgrade_thresh) {
		qos->lowlat_flow = TRUE;
	} else if (qos->lowlat_detect_count == 0) {
		qos->lowlat_flow = FALSE;
	}

	DHD_TRACE(("%s(): TX:%lu:%lu:%lu, PUBG:%d::%d\n",
		__FUNCTION__, tx_avg_pkt_size, tx_bytes, tx_pkts,
		qos->lowlat_detect_count, qos->lowlat_flow));

	return (qos->lowlat_flow == TRUE) ? 1 : 0;
}

int qos_algo_params_init(qos_algo_params_t *pqos_params)
{
	if (!pqos_params)
		return BCME_BADARG;

	memset(pqos_params, 0, sizeof(*pqos_params));
	pqos_params->avg_pkt_size_low_thresh = LOWLAT_AVG_PKT_SIZE_LOW;
	pqos_params->avg_pkt_size_high_thresh = LOWLAT_AVG_PKT_SIZE_HIGH;
	pqos_params->num_pkts_low_thresh = LOWLAT_NUM_PKTS_LOW;
	pqos_params->num_pkts_high_thresh = LOWLAT_NUM_PKTS_HIGH;
	pqos_params->detect_cnt_inc_thresh = LOWLAT_DETECT_CNT_INC_THRESH;
	pqos_params->detect_cnt_dec_thresh = LOWLAT_DETECT_CNT_DEC_THRESH;
	pqos_params->detect_cnt_upgrade_thresh = LOWLAT_DETECT_CNT_UPGRADE_THRESH;

	return BCME_OK;
}
/* ================= End of QOS Algorithm ======================== */
