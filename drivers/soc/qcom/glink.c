/* Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <asm/arch_timer.h>
#include <linux/err.h>
#include <linux/ipc_logging.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/rwsem.h>
#include <linux/pm_qos.h>
#include <soc/qcom/glink.h>
#include <soc/qcom/tracer_pkt.h>
#include "glink_core_if.h"
#include "glink_private.h"
#include "glink_xprt_if.h"

/* Number of internal IPC Logging log pages */
#define NUM_LOG_PAGES	10
#define GLINK_PM_QOS_HOLDOFF_MS		10
#define GLINK_QOS_DEF_NUM_TOKENS	10
#define GLINK_QOS_DEF_NUM_PRIORITY	1
#define GLINK_QOS_DEF_MTU		2048

/**
 * struct glink_qos_priority_bin - Packet Scheduler's priority bucket
 * @max_rate_kBps:	Maximum rate supported by the priority bucket.
 * @power_state:	Transport power state for this priority bin.
 * @tx_ready:		List of channels ready for tx in the priority bucket.
 * @active_ch_cnt:	Active channels of this priority.
 */
struct glink_qos_priority_bin {
	unsigned long max_rate_kBps;
	uint32_t power_state;
	struct list_head tx_ready;
	uint32_t active_ch_cnt;
};

/**
 * struct glink_core_xprt_ctx - transport representation structure
 * @xprt_state_lhb0:		controls read/write access to transport state
 * @list_node:			used to chain this transport in a global
 *				transport list
 * @name:			name of this transport
 * @edge:			what this transport connects to
 * @id:				the id to use for channel migration
 * @versions:			array of transport versions this implementation
 *				supports
 * @versions_entries:		number of entries in @versions
 * @local_version_idx:		local version index into @versions this
 *				transport is currently running
 * @remote_version_idx:		remote version index into @versions this
 *				transport is currently running
 * @l_features:			Features negotiated by the local side
 * @capabilities:		Capabilities of underlying transport
 * @ops:			transport defined implementation of common
 *				operations
 * @local_state:		value from local_channel_state_e representing
 *				the local state of this transport
 * @remote_neg_completed:	is the version negotiation with the remote end
 *				completed
 * @xprt_ctx_lock_lhb1		lock to protect @next_lcid and @channels
 * @next_lcid:			logical channel identifier to assign to the next
 *				created channel
 * @max_cid:			maximum number of channel identifiers supported
 * @max_iid:			maximum number of intent identifiers supported
 * @tx_work:			work item to process @tx_ready
 * @tx_wq:			workqueue to run @tx_work
 * @channels:			list of all existing channels on this transport
 * @mtu:			MTU supported by this transport.
 * @token_count:		Number of tokens to be assigned per assignment.
 * @curr_qos_rate_kBps:		Aggregate of currently supported QoS requests.
 * @threshold_rate_kBps:	Maximum Rate allocated for QoS traffic.
 * @num_priority:		Number of priority buckets in the transport.
 * @tx_ready_lock_lhb2:	lock to protect @tx_ready
 * @active_high_prio:		Highest priority of active channels.
 * @prio_bin:			Pointer to priority buckets.
 * @pm_qos_req:			power management QoS request for TX path
 * @qos_req_active:		a vote is active with the PM QoS system
 * @tx_path_activity:		transmit activity has occurred
 * @pm_qos_work:		removes PM QoS vote due to inactivity
 * @xprt_dbgfs_lock_lhb3:	debugfs channel structure lock
 * @log_ctx:			IPC logging context for this transport.
 */
struct glink_core_xprt_ctx {
	struct rwref_lock xprt_state_lhb0;
	struct list_head list_node;
	char name[GLINK_NAME_SIZE];
	char edge[GLINK_NAME_SIZE];
	uint16_t id;
	const struct glink_core_version *versions;
	size_t versions_entries;
	uint32_t local_version_idx;
	uint32_t remote_version_idx;
	uint32_t l_features;
	uint32_t capabilities;
	struct glink_transport_if *ops;
	enum transport_state_e local_state;
	bool remote_neg_completed;

	spinlock_t xprt_ctx_lock_lhb1;
	struct list_head channels;
	uint32_t next_lcid;
	struct list_head free_lcid_list;

	uint32_t max_cid;
	uint32_t max_iid;
	struct work_struct tx_work;
	struct workqueue_struct *tx_wq;

	size_t mtu;
	uint32_t token_count;
	unsigned long curr_qos_rate_kBps;
	unsigned long threshold_rate_kBps;
	uint32_t num_priority;
	spinlock_t tx_ready_lock_lhb2;
	uint32_t active_high_prio;
	struct glink_qos_priority_bin *prio_bin;

	struct pm_qos_request pm_qos_req;
	bool qos_req_active;
	bool tx_path_activity;
	struct delayed_work pm_qos_work;

	struct mutex xprt_dbgfs_lock_lhb3;
	void *log_ctx;
};

/**
 * Channel Context
 * @xprt_state_lhb0:	controls read/write access to channel state
 * @port_list_node:	channel list node used by transport "channels" list
 * @tx_ready_list_node:	channels that have data ready to transmit
 * @name:		name of the channel
 *
 * @user_priv:		user opaque data type passed into glink_open()
 * @notify_rx:		RX notification function
 * @notify_tx_done:	TX-done notification function (remote side is done)
 * @notify_state:	Channel state (connected / disconnected) notifications
 * @notify_rx_intent_req: Request from remote side for an intent
 * @notify_rxv:		RX notification function (for io buffer chain)
 * @notify_rx_sigs:	RX signal change notification
 * @notify_rx_abort:	Channel close RX Intent aborted
 * @notify_tx_abort:	Channel close TX aborted
 * @notify_rx_tracer_pkt:	Receive notification for tracer packet
 * @notify_remote_rx_intent:	Receive notification for remote-queued RX intent
 *
 * @transport_ptr:		Transport this channel uses
 * @lcid:			Local channel ID
 * @rcid:			Remote channel ID
 * @local_open_state:		Local channel state
 * @remote_opened:		Remote channel state (opened or closed)
 * @int_req_ack:		Remote side intent request ACK state
 * @int_req_ack_complete:	Intent tracking completion - received remote ACK
 * @int_req_complete:		Intent tracking completion - received intent
 * @rx_intent_req_timeout_jiffies:	Timeout for requesting an RX intent, in
 *			jiffies; if set to 0, timeout is infinite
 *
 * @local_rx_intent_lst_lock_lhc1:	RX intent list lock
 * @local_rx_intent_list:		Active RX Intents queued by client
 * @local_rx_intent_ntfy_list:		Client notified, waiting for rx_done()
 * @local_rx_intent_free_list:		Available intent container structure
 *
 * @rmt_rx_intent_lst_lock_lhc2:	Remote RX intent list lock
 * @rmt_rx_intent_list:			Remote RX intent list
 *
 * @max_used_liid:			Maximum Local Intent ID used
 * @dummy_riid:				Dummy remote intent ID
 *
 * @tx_lists_lock_lhc3:		TX list lock
 * @tx_active:				Ready to transmit
 *
 * @tx_pending_rmt_done_lock_lhc4:	Remote-done list lock
 * @tx_pending_remote_done:		Transmitted, waiting for remote done
 * @lsigs:				Local signals
 * @rsigs:				Remote signals
 * @pending_delete:			waiting for channel to be deleted
 * @no_migrate:				The local client does not want to
 *					migrate transports
 * @local_xprt_req:			The transport the local side requested
 * @local_xprt_resp:			The response to @local_xprt_req
 * @remote_xprt_req:			The transport the remote side requested
 * @remote_xprt_resp:			The response to @remote_xprt_req
 * @curr_priority:			Channel's current priority.
 * @initial_priority:			Channel's initial priority.
 * @token_count:			Tokens for consumption by packet.
 * @txd_len:				Transmitted data size in the current
 *					token assignment cycle.
 * @token_start_time:			Time at which tokens are assigned.
 * @req_rate_kBps:			Current QoS request by the channel.
 * @tx_intent_cnt:			Intent count to transmit soon in future.
 * @tx_cnt:				Packets to be picked by tx scheduler.
 */
struct channel_ctx {
	struct rwref_lock ch_state_lhc0;
	struct list_head port_list_node;
	struct list_head tx_ready_list_node;
	char name[GLINK_NAME_SIZE];

	/* user info */
	void *user_priv;
	void (*notify_rx)(void *handle, const void *priv, const void *pkt_priv,
			const void *ptr, size_t size);
	void (*notify_tx_done)(void *handle, const void *priv,
			const void *pkt_priv, const void *ptr);
	void (*notify_state)(void *handle, const void *priv, unsigned event);
	bool (*notify_rx_intent_req)(void *handle, const void *priv,
			size_t req_size);
	void (*notify_rxv)(void *handle, const void *priv, const void *pkt_priv,
			   void *iovec, size_t size,
			   void * (*vbuf_provider)(void *iovec, size_t offset,
						  size_t *size),
			   void * (*pbuf_provider)(void *iovec, size_t offset,
						  size_t *size));
	void (*notify_rx_sigs)(void *handle, const void *priv,
			uint32_t old_sigs, uint32_t new_sigs);
	void (*notify_rx_abort)(void *handle, const void *priv,
				const void *pkt_priv);
	void (*notify_tx_abort)(void *handle, const void *priv,
				const void *pkt_priv);
	void (*notify_rx_tracer_pkt)(void *handle, const void *priv,
			const void *pkt_priv, const void *ptr, size_t size);
	void (*notify_remote_rx_intent)(void *handle, const void *priv,
					size_t size);

	/* internal port state */
	struct glink_core_xprt_ctx *transport_ptr;
	uint32_t lcid;
	uint32_t rcid;
	enum local_channel_state_e local_open_state;
	bool remote_opened;
	bool int_req_ack;
	struct completion int_req_ack_complete;
	struct completion int_req_complete;
	unsigned long rx_intent_req_timeout_jiffies;

	spinlock_t local_rx_intent_lst_lock_lhc1;
	struct list_head local_rx_intent_list;
	struct list_head local_rx_intent_ntfy_list;
	struct list_head local_rx_intent_free_list;

	spinlock_t rmt_rx_intent_lst_lock_lhc2;
	struct list_head rmt_rx_intent_list;

	uint32_t max_used_liid;
	uint32_t dummy_riid;

	spinlock_t tx_lists_lock_lhc3;
	struct list_head tx_active;

	spinlock_t tx_pending_rmt_done_lock_lhc4;
	struct list_head tx_pending_remote_done;

	uint32_t lsigs;
	uint32_t rsigs;
	bool pending_delete;

	bool no_migrate;
	uint16_t local_xprt_req;
	uint16_t local_xprt_resp;
	uint16_t remote_xprt_req;
	uint16_t remote_xprt_resp;

	uint32_t curr_priority;
	uint32_t initial_priority;
	uint32_t token_count;
	size_t txd_len;
	unsigned long token_start_time;
	unsigned long req_rate_kBps;
	uint32_t tx_intent_cnt;
	uint32_t tx_cnt;
};

static struct glink_core_if core_impl;
static void *log_ctx;
static unsigned glink_debug_mask = QCOM_GLINK_INFO;
module_param_named(debug_mask, glink_debug_mask,
		   uint, S_IRUGO | S_IWUSR | S_IWGRP);

static unsigned glink_pm_qos;
module_param_named(pm_qos_enable, glink_pm_qos,
		   uint, S_IRUGO | S_IWUSR | S_IWGRP);


static LIST_HEAD(transport_list);

/*
 * Used while notifying the clients about link state events. Since the clients
 * need to store the callback information temporarily and since all the
 * existing accesses to transport list are in non-IRQ context, defining the
 * transport_list_lock as a mutex.
 */
static DEFINE_MUTEX(transport_list_lock_lha0);

struct link_state_notifier_info {
	struct list_head list;
	char transport[GLINK_NAME_SIZE];
	char edge[GLINK_NAME_SIZE];
	void (*glink_link_state_notif_cb)(
			struct glink_link_state_cb_info *cb_info, void *priv);
	void *priv;
};
static LIST_HEAD(link_state_notifier_list);
static DEFINE_MUTEX(link_state_notifier_lock_lha1);

static struct glink_core_xprt_ctx *find_open_transport(const char *edge,
						       const char *name,
						       bool initial_xprt,
						       uint16_t *best_id);

static bool xprt_is_fully_opened(struct glink_core_xprt_ctx *xprt);

static struct channel_ctx *xprt_lcid_to_ch_ctx_get(
					struct glink_core_xprt_ctx *xprt_ctx,
					uint32_t lcid);

static struct channel_ctx *xprt_rcid_to_ch_ctx_get(
					struct glink_core_xprt_ctx *xprt_ctx,
					uint32_t rcid);

static void xprt_schedule_tx(struct glink_core_xprt_ctx *xprt_ptr,
			     struct channel_ctx *ch_ptr,
			     struct glink_core_tx_pkt *tx_info);

static int xprt_single_threaded_tx(struct glink_core_xprt_ctx *xprt_ptr,
			     struct channel_ctx *ch_ptr,
			     struct glink_core_tx_pkt *tx_info);

static void tx_work_func(struct work_struct *work);

static struct channel_ctx *ch_name_to_ch_ctx_create(
					struct glink_core_xprt_ctx *xprt_ctx,
					const char *name);

static void ch_push_remote_rx_intent(struct channel_ctx *ctx, size_t size,
							uint32_t riid);

static int ch_pop_remote_rx_intent(struct channel_ctx *ctx, size_t size,
				uint32_t *riid_ptr, size_t *intent_size);

static struct glink_core_rx_intent *ch_push_local_rx_intent(
		struct channel_ctx *ctx, const void *pkt_priv, size_t size);

static void ch_remove_local_rx_intent(struct channel_ctx *ctx, uint32_t liid);

static struct glink_core_rx_intent *ch_get_local_rx_intent(
		struct channel_ctx *ctx, uint32_t liid);

static void ch_set_local_rx_intent_notified(struct channel_ctx *ctx,
				struct glink_core_rx_intent *intent_ptr);

static struct glink_core_rx_intent *ch_get_local_rx_intent_notified(
		struct channel_ctx *ctx, const void *ptr);

static void ch_remove_local_rx_intent_notified(struct channel_ctx *ctx,
			struct glink_core_rx_intent *liid_ptr, bool reuse);

static struct glink_core_rx_intent *ch_get_free_local_rx_intent(
		struct channel_ctx *ctx);

static void ch_purge_intent_lists(struct channel_ctx *ctx);

static void ch_add_rcid(struct glink_core_xprt_ctx *xprt_ctx,
			struct channel_ctx *ctx,
			uint32_t rcid);

static bool ch_is_fully_opened(struct channel_ctx *ctx);
static bool ch_is_fully_closed(struct channel_ctx *ctx);

struct glink_core_tx_pkt *ch_get_tx_pending_remote_done(struct channel_ctx *ctx,
							uint32_t riid);

static void ch_remove_tx_pending_remote_done(struct channel_ctx *ctx,
					struct glink_core_tx_pkt *tx_pkt);

static void glink_core_rx_cmd_rx_intent_req_ack(struct glink_transport_if
					*if_ptr, uint32_t rcid, bool granted);

static bool glink_core_remote_close_common(struct channel_ctx *ctx);

static void check_link_notifier_and_notify(struct glink_core_xprt_ctx *xprt_ptr,
					   enum glink_link_state link_state);

static void glink_core_channel_cleanup(struct glink_core_xprt_ctx *xprt_ptr);
static void glink_pm_qos_vote(struct glink_core_xprt_ctx *xprt_ptr);
static void glink_pm_qos_unvote(struct glink_core_xprt_ctx *xprt_ptr);
static void glink_pm_qos_cancel_worker(struct work_struct *work);
static bool ch_update_local_state(struct channel_ctx *ctx,
			enum local_channel_state_e lstate);
static bool ch_update_rmt_state(struct channel_ctx *ctx, bool rstate);
static void glink_core_deinit_xprt_qos_cfg(
			struct glink_core_xprt_ctx *xprt_ptr);

#define glink_prio_to_power_state(xprt_ctx, priority) \
		((xprt_ctx)->prio_bin[priority].power_state)

#define GLINK_GET_CH_TX_STATE(ctx) \
		((ctx)->tx_intent_cnt || (ctx)->tx_cnt)

/**
 * glink_ssr() - Clean up locally for SSR by simulating remote close
 * @subsystem:	The name of the subsystem being restarted
 *
 * Call into the transport using the ssr(if_ptr) function to allow it to
 * clean up any necessary structures, then simulate a remote close from
 * subsystem for all channels on that edge.
 *
 * Return: Standard error codes.
 */
int glink_ssr(const char *subsystem)
{
	int ret = 0;
	bool transport_found = false;
	struct glink_core_xprt_ctx *xprt_ctx = NULL;
	struct channel_ctx *ch_ctx, *temp_ch_ctx;
	uint32_t i;
	unsigned long flags;

	mutex_lock(&transport_list_lock_lha0);
	list_for_each_entry(xprt_ctx, &transport_list, list_node) {
		if (!strcmp(subsystem, xprt_ctx->edge) &&
				xprt_is_fully_opened(xprt_ctx)) {
			GLINK_INFO_XPRT(xprt_ctx, "%s: SSR\n", __func__);
			spin_lock_irqsave(&xprt_ctx->tx_ready_lock_lhb2,
					  flags);
			for (i = 0; i < xprt_ctx->num_priority; i++)
				list_for_each_entry_safe(ch_ctx, temp_ch_ctx,
						&xprt_ctx->prio_bin[i].tx_ready,
						tx_ready_list_node)
					list_del_init(
						&ch_ctx->tx_ready_list_node);
			spin_unlock_irqrestore(&xprt_ctx->tx_ready_lock_lhb2,
						flags);

			xprt_ctx->ops->ssr(xprt_ctx->ops);
			transport_found = true;
		}
	}
	mutex_unlock(&transport_list_lock_lha0);

	if (!transport_found)
		ret = -ENODEV;

	return ret;
}
EXPORT_SYMBOL(glink_ssr);

/**
 * glink_core_ch_close_ack_common() - handles the common operations during
 *                                    close ack.
 * @ctx:	Pointer to channel instance.
 *
 * Return: True if the channel is fully closed after the state change,
 *	false otherwise.
 */
static bool glink_core_ch_close_ack_common(struct channel_ctx *ctx)
{
	bool is_fully_closed;

	if (ctx == NULL)
		return false;
	is_fully_closed = ch_update_local_state(ctx, GLINK_CHANNEL_CLOSED);
	GLINK_INFO_PERF_CH(ctx,
		"%s: local:GLINK_CHANNEL_CLOSING->GLINK_CHANNEL_CLOSED\n",
		__func__);

	if (ctx->notify_state) {
		ctx->notify_state(ctx, ctx->user_priv,
			GLINK_LOCAL_DISCONNECTED);
		ch_purge_intent_lists(ctx);
		GLINK_INFO_PERF_CH(ctx,
		"%s: notify state: GLINK_LOCAL_DISCONNECTED\n",
		__func__);
	}

	return is_fully_closed;
}

/**
 * glink_core_remote_close_common() - Handles the common operations during
 *                                    a remote close.
 * @ctx:	Pointer to channel instance.
 *
 * Return: True if the channel is fully closed after the state change,
 *	false otherwise.
 */
static bool glink_core_remote_close_common(struct channel_ctx *ctx)
{
	bool is_fully_closed;

	if (ctx == NULL)
		return false;
	is_fully_closed = ch_update_rmt_state(ctx, false);
	ctx->rcid = 0;

	if (ctx->local_open_state != GLINK_CHANNEL_CLOSED &&
		ctx->local_open_state != GLINK_CHANNEL_CLOSING) {
		if (ctx->notify_state)
			ctx->notify_state(ctx, ctx->user_priv,
				GLINK_REMOTE_DISCONNECTED);
		GLINK_INFO_CH(ctx,
				"%s: %s: GLINK_REMOTE_DISCONNECTED\n",
				__func__, "notify state");
	}

	if (ctx->local_open_state == GLINK_CHANNEL_CLOSED)
		GLINK_INFO_CH(ctx,
			"%s: %s, %s\n", __func__,
			"Did not send GLINK_REMOTE_DISCONNECTED",
			"local state is already CLOSED");

	ctx->int_req_ack = false;
	complete_all(&ctx->int_req_ack_complete);
	complete_all(&ctx->int_req_complete);
	ch_purge_intent_lists(ctx);

	return is_fully_closed;
}

/**
 * glink_qos_calc_rate_kBps() - Calculate the transmit rate in kBps
 * @pkt_size:		Worst case packet size per transmission.
 * @interval_us:	Packet transmit interval in us.
 *
 * This function is used to calculate the rate of transmission rate of
 * a channel in kBps.
 *
 * Return: Transmission rate in kBps.
 */
static unsigned long glink_qos_calc_rate_kBps(size_t pkt_size,
				       unsigned long interval_us)
{
	unsigned long rate_kBps, rem;

	rate_kBps = pkt_size * USEC_PER_SEC;
	rem = do_div(rate_kBps, (interval_us * 1024));
	return rate_kBps;
}

/**
 * glink_qos_check_feasibility() - Feasibility test on a QoS Request
 * @xprt_ctx:		Transport in which the QoS request is made.
 * @req_rate_kBps:	QoS Request.
 *
 * This function is used to perform the schedulability test on a QoS request
 * over a specific transport.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
static int glink_qos_check_feasibility(struct glink_core_xprt_ctx *xprt_ctx,
				       unsigned long req_rate_kBps)
{
	unsigned long new_rate_kBps;

	if (xprt_ctx->num_priority == GLINK_QOS_DEF_NUM_PRIORITY)
		return -EOPNOTSUPP;

	new_rate_kBps = xprt_ctx->curr_qos_rate_kBps + req_rate_kBps;
	if (new_rate_kBps > xprt_ctx->threshold_rate_kBps) {
		GLINK_ERR_XPRT(xprt_ctx,
			"New_rate(%lu + %lu) > threshold_rate(%lu)\n",
			xprt_ctx->curr_qos_rate_kBps, req_rate_kBps,
			xprt_ctx->threshold_rate_kBps);
		return -EBUSY;
	}
	return 0;
}

/**
 * glink_qos_update_ch_prio() - Update the channel priority
 * @ctx:		Channel context whose priority is updated.
 * @new_priority:	New priority of the channel.
 *
 * This function is called to update the channel priority during QoS request,
 * QoS Cancel or Priority evaluation by packet scheduler. This function must
 * be called with transport's tx_ready_lock_lhb2 lock and channel's
 * tx_lists_lock_lhc3 locked.
 */
static void glink_qos_update_ch_prio(struct channel_ctx *ctx,
				     uint32_t new_priority)
{
	uint32_t old_priority;

	if (unlikely(!ctx))
		return;

	old_priority = ctx->curr_priority;
	if (!list_empty(&ctx->tx_ready_list_node)) {
		ctx->transport_ptr->prio_bin[old_priority].active_ch_cnt--;
		list_move(&ctx->tx_ready_list_node,
			  &ctx->transport_ptr->prio_bin[new_priority].tx_ready);
		ctx->transport_ptr->prio_bin[new_priority].active_ch_cnt++;
	}
	ctx->curr_priority = new_priority;
}

/**
 * glink_qos_assign_priority() - Assign priority to a channel
 * @ctx:		Channel for which the priority has to be assigned.
 * @req_rate_kBps:	QoS request by the channel.
 *
 * This function is used to assign a priority to the channel depending on its
 * QoS Request.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
static int glink_qos_assign_priority(struct channel_ctx *ctx,
				     unsigned long req_rate_kBps)
{
	int ret;
	uint32_t i;
	unsigned long flags;

	spin_lock_irqsave(&ctx->transport_ptr->tx_ready_lock_lhb2, flags);
	if (ctx->req_rate_kBps) {
		spin_unlock_irqrestore(&ctx->transport_ptr->tx_ready_lock_lhb2,
					flags);
		GLINK_ERR_CH(ctx, "%s: QoS Request already exists\n", __func__);
		return -EINVAL;
	}

	ret = glink_qos_check_feasibility(ctx->transport_ptr, req_rate_kBps);
	if (ret < 0) {
		spin_unlock_irqrestore(&ctx->transport_ptr->tx_ready_lock_lhb2,
					flags);
		return ret;
	}

	spin_lock(&ctx->tx_lists_lock_lhc3);
	i = ctx->transport_ptr->num_priority - 1;
	while (i > 0 &&
	       ctx->transport_ptr->prio_bin[i-1].max_rate_kBps >= req_rate_kBps)
		i--;

	ctx->initial_priority = i;
	glink_qos_update_ch_prio(ctx, i);
	ctx->req_rate_kBps = req_rate_kBps;
	if (i > 0) {
		ctx->transport_ptr->curr_qos_rate_kBps += req_rate_kBps;
		ctx->token_count = ctx->transport_ptr->token_count;
		ctx->txd_len = 0;
		ctx->token_start_time = arch_counter_get_cntpct();
	}
	spin_unlock(&ctx->tx_lists_lock_lhc3);
	spin_unlock_irqrestore(&ctx->transport_ptr->tx_ready_lock_lhb2, flags);
	return 0;
}

/**
 * glink_qos_reset_priority() - Reset the channel priority
 * @ctx:	Channel for which the priority is reset.
 *
 * This function is used to reset the channel priority when the QoS request
 * is cancelled by the channel.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
static int glink_qos_reset_priority(struct channel_ctx *ctx)
{
	unsigned long flags;

	spin_lock_irqsave(&ctx->transport_ptr->tx_ready_lock_lhb2, flags);
	spin_lock(&ctx->tx_lists_lock_lhc3);
	if (ctx->initial_priority > 0) {
		ctx->initial_priority = 0;
		glink_qos_update_ch_prio(ctx, 0);
		ctx->transport_ptr->curr_qos_rate_kBps -= ctx->req_rate_kBps;
		ctx->txd_len = 0;
		ctx->req_rate_kBps = 0;
	}
	spin_unlock(&ctx->tx_lists_lock_lhc3);
	spin_unlock_irqrestore(&ctx->transport_ptr->tx_ready_lock_lhb2, flags);
	return 0;
}

/**
 * glink_qos_ch_vote_xprt() - Vote the transport that channel is active
 * @ctx:	Channel context which is active.
 *
 * This function is called to vote for the transport either when the channel
 * is transmitting or when it shows an intention to transmit sooner. This
 * function must be called with transport's tx_ready_lock_lhb2 lock and
 * channel's tx_lists_lock_lhc3 locked.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
static int glink_qos_ch_vote_xprt(struct channel_ctx *ctx)
{
	uint32_t prio;

	if (unlikely(!ctx || !ctx->transport_ptr))
		return -EINVAL;

	prio = ctx->curr_priority;
	ctx->transport_ptr->prio_bin[prio].active_ch_cnt++;

	if (ctx->transport_ptr->prio_bin[prio].active_ch_cnt == 1 &&
	    ctx->transport_ptr->active_high_prio < prio) {
		/*
		 * One active channel in this priority and this is the
		 * highest active priority bucket
		 */
		ctx->transport_ptr->active_high_prio = prio;
		return ctx->transport_ptr->ops->power_vote(
				ctx->transport_ptr->ops,
				glink_prio_to_power_state(ctx->transport_ptr,
							  prio));
	}
	return 0;
}

/**
 * glink_qos_ch_unvote_xprt() - Unvote the transport when channel is inactive
 * @ctx:	Channel context which is inactive.
 *
 * This function is called to unvote for the transport either when all the
 * packets queued by the channel are transmitted by the scheduler. This
 * function must be called with transport's tx_ready_lock_lhb2 lock and
 * channel's tx_lists_lock_lhc3 locked.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
static int glink_qos_ch_unvote_xprt(struct channel_ctx *ctx)
{
	uint32_t prio;

	if (unlikely(!ctx || !ctx->transport_ptr))
		return -EINVAL;

	prio = ctx->curr_priority;
	ctx->transport_ptr->prio_bin[prio].active_ch_cnt--;

	if (ctx->transport_ptr->prio_bin[prio].active_ch_cnt ||
	    ctx->transport_ptr->active_high_prio > prio)
		return 0;

	/*
	 * No active channel in this priority and this is the
	 * highest active priority bucket
	 */
	while (prio > 0) {
		prio--;
		if (!ctx->transport_ptr->prio_bin[prio].active_ch_cnt)
			continue;

		ctx->transport_ptr->active_high_prio = prio;
		return ctx->transport_ptr->ops->power_vote(
				ctx->transport_ptr->ops,
				glink_prio_to_power_state(ctx->transport_ptr,
							  prio));
	}
	return ctx->transport_ptr->ops->power_unvote(ctx->transport_ptr->ops);
}

/**
 * glink_qos_add_ch_tx_intent() - Add the channel's intention to transmit soon
 * @ctx:	Channel context which is going to be active.
 *
 * This function is called to update the channel state when it is intending to
 * transmit sooner. This function must be called with transport's
 * tx_ready_lock_lhb2 lock and channel's tx_lists_lock_lhc3 locked.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
static int glink_qos_add_ch_tx_intent(struct channel_ctx *ctx)
{
	bool active_tx;

	if (unlikely(!ctx))
		return -EINVAL;

	active_tx = GLINK_GET_CH_TX_STATE(ctx);
	ctx->tx_intent_cnt++;
	if (!active_tx)
		glink_qos_ch_vote_xprt(ctx);
	return 0;
}

/**
 * glink_qos_do_ch_tx() - Update the channel's state that it is transmitting
 * @ctx:	Channel context which is transmitting.
 *
 * This function is called to update the channel state when it is queueing a
 * packet to transmit. This function must be called with transport's
 * tx_ready_lock_lhb2 lock and channel's tx_lists_lock_lhc3 locked.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
static int glink_qos_do_ch_tx(struct channel_ctx *ctx)
{
	bool active_tx;

	if (unlikely(!ctx))
		return -EINVAL;

	active_tx = GLINK_GET_CH_TX_STATE(ctx);
	ctx->tx_cnt++;
	if (ctx->tx_intent_cnt)
		ctx->tx_intent_cnt--;
	if (!active_tx)
		glink_qos_ch_vote_xprt(ctx);
	return 0;
}

/**
 * glink_qos_done_ch_tx() - Update the channel's state when transmission is done
 * @ctx:	Channel context for which all packets are transmitted.
 *
 * This function is called to update the channel state when all packets in its
 * transmit queue are successfully transmitted. This function must be called
 * with transport's tx_ready_lock_lhb2 lock and channel's tx_lists_lock_lhc3
 * locked.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
static int glink_qos_done_ch_tx(struct channel_ctx *ctx)
{
	bool active_tx;

	if (unlikely(!ctx))
		return -EINVAL;

	WARN_ON(ctx->tx_cnt == 0);
	ctx->tx_cnt = 0;
	active_tx = GLINK_GET_CH_TX_STATE(ctx);
	if (!active_tx)
		glink_qos_ch_unvote_xprt(ctx);
	return 0;
}

/**
 * tx_linear_vbuf_provider() - Virtual Buffer Provider for linear buffers
 * @iovec:	Pointer to the beginning of the linear buffer.
 * @offset:	Offset into the buffer whose address is needed.
 * @size:	Pointer to hold the length of the contiguous buffer space.
 *
 * This function is used when a linear buffer is transmitted.
 *
 * Return: Address of the buffer which is at offset "offset" from the beginning
 *         of the buffer.
 */
static void *tx_linear_vbuf_provider(void *iovec, size_t offset, size_t *size)
{
	struct glink_core_tx_pkt *tx_info = (struct glink_core_tx_pkt *)iovec;

	if (unlikely(!iovec || !size))
		return NULL;

	if (offset >= tx_info->size)
		return NULL;

	if (unlikely(OVERFLOW_ADD_UNSIGNED(void *, tx_info->data, offset)))
		return NULL;

	*size = tx_info->size - offset;

	return (void *)tx_info->data + offset;
}

/**
 * linearize_vector() - Linearize the vector buffer
 * @iovec:	Pointer to the vector buffer.
 * @size:	Size of data in the vector buffer.
 * vbuf_provider:	Virtual address-space Buffer Provider for the vector.
 * pbuf_provider:	Physical address-space Buffer Provider for the vector.
 *
 * This function is used to linearize the vector buffer provided by the
 * transport when the client has registered to receive only the vector
 * buffer.
 *
 * Return: address of the linear buffer on success, NULL on failure.
 */
static void *linearize_vector(void *iovec, size_t size,
	void * (*vbuf_provider)(void *iovec, size_t offset, size_t *buf_size),
	void * (*pbuf_provider)(void *iovec, size_t offset, size_t *buf_size))
{
	void *bounce_buf;
	void *pdata;
	void *vdata;
	size_t data_size;
	size_t offset = 0;

	bounce_buf = kmalloc(size, GFP_KERNEL);
	if (!bounce_buf)
		return ERR_PTR(-ENOMEM);

	do {
		if (vbuf_provider) {
			vdata = vbuf_provider(iovec, offset, &data_size);
		} else {
			pdata = pbuf_provider(iovec, offset, &data_size);
			vdata = phys_to_virt((unsigned long)pdata);
		}

		if (!vdata)
			break;

		if (OVERFLOW_ADD_UNSIGNED(size_t, data_size, offset)) {
			GLINK_ERR("%s: overflow data_size %zu + offset %zu\n",
				  __func__, data_size, offset);
			goto err;
		}

		memcpy(bounce_buf + offset, vdata, data_size);
		offset += data_size;
	} while (offset < size);

	if (offset != size) {
		GLINK_ERR("%s: Error size_copied %zu != total_size %zu\n",
			  __func__, offset, size);
		goto err;
	}
	return bounce_buf;

err:
	kfree(bounce_buf);
	return NULL;
}

/**
 * xprt_lcid_to_ch_ctx_get() - lookup a channel by local id
 * @xprt_ctx:	Transport to search for a matching channel.
 * @lcid:	Local channel identifier corresponding to the desired channel.
 *
 * If the channel is found, the reference count is incremented to ensure the
 * lifetime of the channel context.  The caller must call rwref_put() when done.
 *
 * Return: The channel corresponding to @lcid or NULL if a matching channel
 *	is not found.
 */
static struct channel_ctx *xprt_lcid_to_ch_ctx_get(
					struct glink_core_xprt_ctx *xprt_ctx,
					uint32_t lcid)
{
	struct channel_ctx *entry;
	unsigned long flags;

	spin_lock_irqsave(&xprt_ctx->xprt_ctx_lock_lhb1, flags);
	list_for_each_entry(entry, &xprt_ctx->channels, port_list_node)
		if (entry->lcid == lcid) {
			rwref_get(&entry->ch_state_lhc0);
			spin_unlock_irqrestore(&xprt_ctx->xprt_ctx_lock_lhb1,
					flags);
			return entry;
		}
	spin_unlock_irqrestore(&xprt_ctx->xprt_ctx_lock_lhb1, flags);

	return NULL;
}

/**
 * xprt_rcid_to_ch_ctx_get() - lookup a channel by remote id
 * @xprt_ctx:	Transport to search for a matching channel.
 * @rcid:	Remote channel identifier corresponding to the desired channel.
 *
 * If the channel is found, the reference count is incremented to ensure the
 * lifetime of the channel context.  The caller must call rwref_put() when done.
 *
 * Return: The channel corresponding to @rcid or NULL if a matching channel
 *	is not found.
 */
static struct channel_ctx *xprt_rcid_to_ch_ctx_get(
					struct glink_core_xprt_ctx *xprt_ctx,
					uint32_t rcid)
{
	struct channel_ctx *entry;
	unsigned long flags;

	spin_lock_irqsave(&xprt_ctx->xprt_ctx_lock_lhb1, flags);
	list_for_each_entry(entry, &xprt_ctx->channels, port_list_node)
		if (entry->rcid == rcid) {
			rwref_get(&entry->ch_state_lhc0);
			spin_unlock_irqrestore(&xprt_ctx->xprt_ctx_lock_lhb1,
					flags);
			return entry;
		}
	spin_unlock_irqrestore(&xprt_ctx->xprt_ctx_lock_lhb1, flags);

	return NULL;
}

/**
 * ch_check_duplicate_riid() - Checks for duplicate riid
 * @ctx:	Local channel context
 * @riid:	Remote intent ID
 *
 * This functions check the riid is present in the remote_rx_list or not
 */
bool ch_check_duplicate_riid(struct channel_ctx *ctx, int riid)
{
	struct glink_core_rx_intent *intent;
	unsigned long flags;

	spin_lock_irqsave(&ctx->rmt_rx_intent_lst_lock_lhc2, flags);
	list_for_each_entry(intent, &ctx->rmt_rx_intent_list, list) {
		if (riid == intent->id) {
			spin_unlock_irqrestore(
				&ctx->rmt_rx_intent_lst_lock_lhc2, flags);
			return true;
		}
	}
	spin_unlock_irqrestore(&ctx->rmt_rx_intent_lst_lock_lhc2, flags);
	return false;
}

/**
 * ch_pop_remote_rx_intent() - Finds a matching RX intent
 * @ctx:	Local channel context
 * @size:	Size of Intent
 * @riid_ptr:	Pointer to return value of remote intent ID
 *
 * This functions searches for an RX intent that is >= to the requested size.
 */
int ch_pop_remote_rx_intent(struct channel_ctx *ctx, size_t size,
		uint32_t *riid_ptr, size_t *intent_size)
{
	struct glink_core_rx_intent *intent;
	struct glink_core_rx_intent *intent_tmp;
	unsigned long flags;

	if (GLINK_MAX_PKT_SIZE < size) {
		GLINK_ERR_CH(ctx, "%s: R[]:%zu Invalid size.\n", __func__,
				size);
		return -EINVAL;
	}

	if (riid_ptr == NULL)
		return -EINVAL;

	*riid_ptr = 0;
	spin_lock_irqsave(&ctx->rmt_rx_intent_lst_lock_lhc2, flags);
	if (ctx->transport_ptr->capabilities & GCAP_INTENTLESS) {
		*riid_ptr = ++ctx->dummy_riid;
		spin_unlock_irqrestore(&ctx->rmt_rx_intent_lst_lock_lhc2,
					flags);
		return 0;
	}
	list_for_each_entry_safe(intent, intent_tmp, &ctx->rmt_rx_intent_list,
			list) {
		if (intent->intent_size >= size) {
			list_del(&intent->list);
			GLINK_DBG_CH(ctx,
					"%s: R[%u]:%zu Removed remote intent\n",
					__func__,
					intent->id,
					intent->intent_size);
			*riid_ptr = intent->id;
			*intent_size = intent->intent_size;
			kfree(intent);
			spin_unlock_irqrestore(
				&ctx->rmt_rx_intent_lst_lock_lhc2, flags);
			return 0;
		}
	}
	spin_unlock_irqrestore(&ctx->rmt_rx_intent_lst_lock_lhc2, flags);
	return -EAGAIN;
}

/**
 * ch_push_remote_rx_intent() - Registers a remote RX intent
 * @ctx:	Local channel context
 * @size:	Size of Intent
 * @riid:	Remote intent ID
 *
 * This functions adds a remote RX intent to the remote RX intent list.
 */
void ch_push_remote_rx_intent(struct channel_ctx *ctx, size_t size,
		uint32_t riid)
{
	struct glink_core_rx_intent *intent;
	unsigned long flags;
	gfp_t gfp_flag;

	if (GLINK_MAX_PKT_SIZE < size) {
		GLINK_ERR_CH(ctx, "%s: R[%u]:%zu Invalid size.\n", __func__,
				riid, size);
		return;
	}

	if (ch_check_duplicate_riid(ctx, riid)) {
		GLINK_ERR_CH(ctx, "%s: R[%d]:%zu Duplicate RIID found\n",
				__func__, riid, size);
		return;
	}

	gfp_flag = (ctx->transport_ptr->capabilities & GCAP_AUTO_QUEUE_RX_INT) ?
							GFP_ATOMIC : GFP_KERNEL;
	intent = kzalloc(sizeof(struct glink_core_rx_intent), gfp_flag);
	if (!intent) {
		GLINK_ERR_CH(ctx,
			"%s: R[%u]:%zu Memory allocation for intent failed\n",
			__func__, riid, size);
		return;
	}
	intent->id = riid;
	intent->intent_size = size;

	spin_lock_irqsave(&ctx->rmt_rx_intent_lst_lock_lhc2, flags);
	list_add_tail(&intent->list, &ctx->rmt_rx_intent_list);

	complete_all(&ctx->int_req_complete);
	if (ctx->notify_remote_rx_intent)
		ctx->notify_remote_rx_intent(ctx, ctx->user_priv, size);
	spin_unlock_irqrestore(&ctx->rmt_rx_intent_lst_lock_lhc2, flags);

	GLINK_DBG_CH(ctx, "%s: R[%u]:%zu Pushed remote intent\n", __func__,
			intent->id,
			intent->intent_size);
}

/**
 * ch_push_local_rx_intent() - Create an rx_intent
 * @ctx:	Local channel context
 * @pkt_priv:	Opaque private pointer provided by client to be returned later
 * @size:	Size of intent
 *
 * This functions creates a local intent and adds it to the local
 * intent list.
 */
struct glink_core_rx_intent *ch_push_local_rx_intent(struct channel_ctx *ctx,
		const void *pkt_priv, size_t size)
{
	struct glink_core_rx_intent *intent;
	unsigned long flags;
	int ret;

	if (GLINK_MAX_PKT_SIZE < size) {
		GLINK_ERR_CH(ctx,
			"%s: L[]:%zu Invalid size\n", __func__, size);
		return NULL;
	}

	intent = ch_get_free_local_rx_intent(ctx);
	if (!intent) {
		if (ctx->max_used_liid >= ctx->transport_ptr->max_iid) {
			GLINK_ERR_CH(ctx,
				"%s: All intents are in USE max_iid[%d]",
				__func__, ctx->transport_ptr->max_iid);
			return NULL;
		}

		intent = kzalloc(sizeof(struct glink_core_rx_intent),
								GFP_KERNEL);
		if (!intent) {
			GLINK_ERR_CH(ctx,
			"%s: Memory Allocation for local rx_intent failed",
				__func__);
			return NULL;
		}
		intent->id = ++ctx->max_used_liid;
	}

	/* transport is responsible for allocating/reserving for the intent */
	ret = ctx->transport_ptr->ops->allocate_rx_intent(
					ctx->transport_ptr->ops, size, intent);
	if (ret < 0) {
		/* intent data allocation failure */
		GLINK_ERR_CH(ctx, "%s: unable to allocate intent sz[%zu] %d",
			__func__, size, ret);
		spin_lock_irqsave(&ctx->local_rx_intent_lst_lock_lhc1, flags);
		list_add_tail(&intent->list,
				&ctx->local_rx_intent_free_list);
		spin_unlock_irqrestore(&ctx->local_rx_intent_lst_lock_lhc1,
				flags);
		return NULL;
	}

	intent->pkt_priv = pkt_priv;
	intent->intent_size = size;
	intent->write_offset = 0;
	intent->pkt_size = 0;
	intent->bounce_buf = NULL;

	spin_lock_irqsave(&ctx->local_rx_intent_lst_lock_lhc1, flags);
	list_add_tail(&intent->list, &ctx->local_rx_intent_list);
	spin_unlock_irqrestore(&ctx->local_rx_intent_lst_lock_lhc1, flags);
	GLINK_DBG_CH(ctx, "%s: L[%u]:%zu Pushed intent\n", __func__,
			intent->id,
			intent->intent_size);
	return intent;
}

/**
 * ch_remove_local_rx_intent() - Find and remove RX Intent from list
 * @ctx:	Local channel context
 * @liid:	Local channel Intent ID
 *
 * This functions parses the local intent list for a specific channel
 * and checks for the intent using the intent ID. If found, the intent
 * is deleted from the list.
 */
void ch_remove_local_rx_intent(struct channel_ctx *ctx, uint32_t liid)
{
	struct glink_core_rx_intent *intent, *tmp_intent;
	unsigned long flags;

	if (ctx->transport_ptr->max_iid < liid) {
		GLINK_ERR_CH(ctx, "%s: L[%u] Invalid ID.\n", __func__,
				liid);
		return;
	}

	spin_lock_irqsave(&ctx->local_rx_intent_lst_lock_lhc1, flags);
	list_for_each_entry_safe(intent, tmp_intent, &ctx->local_rx_intent_list,
									list) {
		if (liid == intent->id) {
			list_del(&intent->list);
			list_add_tail(&intent->list,
					&ctx->local_rx_intent_free_list);
			spin_unlock_irqrestore(
					&ctx->local_rx_intent_lst_lock_lhc1,
					flags);
			GLINK_DBG_CH(ctx,
			"%s: L[%u]:%zu moved intent to Free/unused list\n",
				__func__,
				intent->id,
				intent->intent_size);
			return;
		}
	}
	spin_unlock_irqrestore(&ctx->local_rx_intent_lst_lock_lhc1, flags);
	GLINK_ERR_CH(ctx, "%s: L[%u] Intent not found.\n", __func__,
			liid);
}

/**
 * ch_get_dummy_rx_intent() - Get a dummy rx_intent
 * @ctx:	Local channel context
 * @liid:	Local channel Intent ID
 *
 * This functions parses the local intent list for a specific channel and
 * returns either a matching intent or allocates a dummy one if no matching
 * intents can be found.
 *
 * Return: Pointer to the intent if intent is found else NULL
 */
struct glink_core_rx_intent *ch_get_dummy_rx_intent(struct channel_ctx *ctx,
		uint32_t liid)
{
	struct glink_core_rx_intent *intent;
	unsigned long flags;

	spin_lock_irqsave(&ctx->local_rx_intent_lst_lock_lhc1, flags);
	if (!list_empty(&ctx->local_rx_intent_list)) {
		intent = list_first_entry(&ctx->local_rx_intent_list,
					  struct glink_core_rx_intent, list);
		spin_unlock_irqrestore(&ctx->local_rx_intent_lst_lock_lhc1,
					flags);
		return intent;
	}
	spin_unlock_irqrestore(&ctx->local_rx_intent_lst_lock_lhc1, flags);

	intent = ch_get_free_local_rx_intent(ctx);
	if (!intent) {
		intent = kzalloc(sizeof(struct glink_core_rx_intent),
								GFP_ATOMIC);
		if (!intent) {
			GLINK_ERR_CH(ctx,
			"%s: Memory Allocation for local rx_intent failed",
				__func__);
			return NULL;
		}
		intent->id = ++ctx->max_used_liid;
	}
	intent->intent_size = 0;
	intent->write_offset = 0;
	intent->pkt_size = 0;
	intent->bounce_buf = NULL;
	intent->pkt_priv = NULL;

	spin_lock_irqsave(&ctx->local_rx_intent_lst_lock_lhc1, flags);
	list_add_tail(&intent->list, &ctx->local_rx_intent_list);
	spin_unlock_irqrestore(&ctx->local_rx_intent_lst_lock_lhc1, flags);
	GLINK_DBG_CH(ctx, "%s: L[%u]:%zu Pushed intent\n", __func__,
			intent->id,
			intent->intent_size);
	return intent;
}

/**
 * ch_get_local_rx_intent() - Search for an rx_intent
 * @ctx:	Local channel context
 * @liid:	Local channel Intent ID
 *
 * This functions parses the local intent list for a specific channel
 * and checks for the intent using the intent ID. If found, pointer to
 * the intent is returned.
 *
 * Return: Pointer to the intent if intent is found else NULL
 */
struct glink_core_rx_intent *ch_get_local_rx_intent(struct channel_ctx *ctx,
		uint32_t liid)
{
	struct glink_core_rx_intent *intent;
	unsigned long flags;

	if (ctx->transport_ptr->max_iid < liid) {
		GLINK_ERR_CH(ctx, "%s: L[%u] Invalid ID.\n", __func__,
				liid);
		return NULL;
	}

	if (ctx->transport_ptr->capabilities & GCAP_INTENTLESS)
		return ch_get_dummy_rx_intent(ctx, liid);

	spin_lock_irqsave(&ctx->local_rx_intent_lst_lock_lhc1, flags);
	list_for_each_entry(intent, &ctx->local_rx_intent_list, list) {
		if (liid == intent->id) {
			spin_unlock_irqrestore(
				&ctx->local_rx_intent_lst_lock_lhc1, flags);
			return intent;
		}
	}
	spin_unlock_irqrestore(&ctx->local_rx_intent_lst_lock_lhc1, flags);
	GLINK_ERR_CH(ctx, "%s: L[%u] Intent not found.\n", __func__,
			liid);
	return NULL;
}

/**
 * ch_set_local_rx_intent_notified() - Add a rx intent to local intent
 *					notified list
 * @ctx:	Local channel context
 * @intent_ptr:	Pointer to the local intent
 *
 * This functions parses the local intent list for a specific channel
 * and checks for the intent. If found, the function deletes the intent
 * from local_rx_intent list and adds it to local_rx_intent_notified list.
 */
void ch_set_local_rx_intent_notified(struct channel_ctx *ctx,
		struct glink_core_rx_intent *intent_ptr)
{
	struct glink_core_rx_intent *tmp_intent, *intent;
	unsigned long flags;

	spin_lock_irqsave(&ctx->local_rx_intent_lst_lock_lhc1, flags);
	list_for_each_entry_safe(intent, tmp_intent, &ctx->local_rx_intent_list,
									list) {
		if (intent == intent_ptr) {
			list_del(&intent->list);
			list_add_tail(&intent->list,
				&ctx->local_rx_intent_ntfy_list);
			GLINK_DBG_CH(ctx,
				"%s: L[%u]:%zu Moved intent %s",
				__func__,
				intent_ptr->id,
				intent_ptr->intent_size,
				"from local to notify list\n");
			spin_unlock_irqrestore(
					&ctx->local_rx_intent_lst_lock_lhc1,
					flags);
			return;
		}
	}
	spin_unlock_irqrestore(&ctx->local_rx_intent_lst_lock_lhc1, flags);
	GLINK_ERR_CH(ctx, "%s: L[%u] Intent not found.\n", __func__,
			intent_ptr->id);
}

/**
 * ch_get_local_rx_intent_notified() - Find rx intent in local notified list
 * @ctx:	Local channel context
 * @ptr:	Pointer to the rx intent
 *
 * This functions parses the local intent notify list for a specific channel
 * and checks for the intent.
 *
 * Return: Pointer to the intent if intent is found else NULL.
 */
struct glink_core_rx_intent *ch_get_local_rx_intent_notified(
	struct channel_ctx *ctx, const void *ptr)
{
	struct glink_core_rx_intent *ptr_intent;
	unsigned long flags;

	spin_lock_irqsave(&ctx->local_rx_intent_lst_lock_lhc1, flags);
	list_for_each_entry(ptr_intent, &ctx->local_rx_intent_ntfy_list,
								list) {
		if (ptr_intent->data == ptr || ptr_intent->iovec == ptr ||
		    ptr_intent->bounce_buf == ptr) {
			spin_unlock_irqrestore(
					&ctx->local_rx_intent_lst_lock_lhc1,
					flags);
			return ptr_intent;
		}
	}
	spin_unlock_irqrestore(&ctx->local_rx_intent_lst_lock_lhc1, flags);
	GLINK_ERR_CH(ctx, "%s: Local intent not found\n", __func__);
	return NULL;
}

/**
 * ch_remove_local_rx_intent_notified() - Remove a rx intent in local intent
 *					notified list
 * @ctx:	Local channel context
 * @ptr:	Pointer to the rx intent
 * @reuse:	Reuse the rx intent
 *
 * This functions parses the local intent notify list for a specific channel
 * and checks for the intent. If found, the function deletes the intent
 * from local_rx_intent_notified list and adds it to local_rx_intent_free list.
 */
void ch_remove_local_rx_intent_notified(struct channel_ctx *ctx,
	struct glink_core_rx_intent *liid_ptr, bool reuse)
{
	struct glink_core_rx_intent *ptr_intent, *tmp_intent;
	unsigned long flags;

	spin_lock_irqsave(&ctx->local_rx_intent_lst_lock_lhc1, flags);
	list_for_each_entry_safe(ptr_intent, tmp_intent,
				&ctx->local_rx_intent_ntfy_list, list) {
		if (ptr_intent == liid_ptr) {
			list_del(&ptr_intent->list);
			GLINK_DBG_CH(ctx,
				"%s: L[%u]:%zu Removed intent from notify list\n",
				__func__,
				ptr_intent->id,
				ptr_intent->intent_size);
			kfree(ptr_intent->bounce_buf);
			ptr_intent->bounce_buf = NULL;
			ptr_intent->write_offset = 0;
			ptr_intent->pkt_size = 0;
			if (reuse)
				list_add_tail(&ptr_intent->list,
					&ctx->local_rx_intent_list);
			else
				list_add_tail(&ptr_intent->list,
					&ctx->local_rx_intent_free_list);
			spin_unlock_irqrestore(
					&ctx->local_rx_intent_lst_lock_lhc1,
					flags);
			return;
		}
	}
	spin_unlock_irqrestore(&ctx->local_rx_intent_lst_lock_lhc1, flags);
	GLINK_ERR_CH(ctx, "%s: L[%u] Intent not found.\n", __func__,
			liid_ptr->id);
}

/**
 * ch_get_free_local_rx_intent() - Return a rx intent in local intent
 *					free list
 * @ctx:	Local channel context
 *
 * This functions parses the local_rx_intent_free list for a specific channel
 * and checks for the free unused intent. If found, the function returns
 * the free intent pointer else NULL pointer.
 */
struct glink_core_rx_intent *ch_get_free_local_rx_intent(
	struct channel_ctx *ctx)
{
	struct glink_core_rx_intent *ptr_intent = NULL;
	unsigned long flags;

	spin_lock_irqsave(&ctx->local_rx_intent_lst_lock_lhc1, flags);
	if (!list_empty(&ctx->local_rx_intent_free_list)) {
		ptr_intent = list_first_entry(&ctx->local_rx_intent_free_list,
				struct glink_core_rx_intent,
				list);
		list_del(&ptr_intent->list);
	}
	spin_unlock_irqrestore(&ctx->local_rx_intent_lst_lock_lhc1, flags);
	return ptr_intent;
}

/**
 * ch_purge_intent_lists() - Remove all intents for a channel
 *
 * @ctx:	Local channel context
 *
 * This functions parses the local intent lists for a specific channel and
 * removes and frees all intents.
 */
void ch_purge_intent_lists(struct channel_ctx *ctx)
{
	struct glink_core_rx_intent *ptr_intent, *tmp_intent;
	struct glink_core_tx_pkt *tx_info, *tx_info_temp;
	unsigned long flags;

	spin_lock_irqsave(&ctx->tx_lists_lock_lhc3, flags);
	list_for_each_entry_safe(tx_info, tx_info_temp, &ctx->tx_active,
			list_node) {
		ctx->notify_tx_abort(ctx, ctx->user_priv,
				tx_info->pkt_priv);
		rwref_put(&tx_info->pkt_ref);
	}
	spin_unlock_irqrestore(&ctx->tx_lists_lock_lhc3, flags);

	spin_lock_irqsave(&ctx->local_rx_intent_lst_lock_lhc1, flags);
	list_for_each_entry_safe(ptr_intent, tmp_intent,
				&ctx->local_rx_intent_list, list) {
		ctx->notify_rx_abort(ctx, ctx->user_priv,
				ptr_intent->pkt_priv);
		list_del(&ptr_intent->list);
		kfree(ptr_intent);
	}

	if (!list_empty(&ctx->local_rx_intent_ntfy_list))
		/*
		 * The client is still processing an rx_notify() call and has
		 * not yet called glink_rx_done() to return the pointer to us.
		 * glink_rx_done() will do the appropriate cleanup when this
		 * call occurs, but log a message here just for internal state
		 * tracking.
		 */
		GLINK_INFO_CH(ctx, "%s: waiting on glink_rx_done()\n",
				__func__);

	list_for_each_entry_safe(ptr_intent, tmp_intent,
				&ctx->local_rx_intent_free_list, list) {
		list_del(&ptr_intent->list);
		kfree(ptr_intent);
	}
	ctx->max_used_liid = 0;
	spin_unlock_irqrestore(&ctx->local_rx_intent_lst_lock_lhc1, flags);

	spin_lock_irqsave(&ctx->rmt_rx_intent_lst_lock_lhc2, flags);
	list_for_each_entry_safe(ptr_intent, tmp_intent,
			&ctx->rmt_rx_intent_list, list) {
		list_del(&ptr_intent->list);
		kfree(ptr_intent);
	}
	spin_unlock_irqrestore(&ctx->rmt_rx_intent_lst_lock_lhc2, flags);
}

/**
 * ch_get_tx_pending_remote_done() - Lookup for a packet that is waiting for
 *                                   the remote-done notification.
 * @ctx:	Pointer to the channel context
 * @riid:	riid of transmit packet
 *
 * This function adds a packet to the tx_pending_remote_done list.
 *
 * The tx_lists_lock_lhc3 lock needs to be held while calling this function.
 *
 * Return: Pointer to the tx packet
 */
struct glink_core_tx_pkt *ch_get_tx_pending_remote_done(
	struct channel_ctx *ctx, uint32_t riid)
{
	struct glink_core_tx_pkt *tx_pkt;
	unsigned long flags;

	if (!ctx) {
		GLINK_ERR("%s: Invalid context pointer", __func__);
		return NULL;
	}

	spin_lock_irqsave(&ctx->tx_pending_rmt_done_lock_lhc4, flags);
	list_for_each_entry(tx_pkt, &ctx->tx_pending_remote_done, list_done) {
		if (tx_pkt->riid == riid) {
			if (tx_pkt->size_remaining) {
				GLINK_ERR_CH(ctx, "%s: R[%u] TX not complete",
						__func__, riid);
				tx_pkt = NULL;
			}
			spin_unlock_irqrestore(
				&ctx->tx_pending_rmt_done_lock_lhc4, flags);
			return tx_pkt;
		}
	}
	spin_unlock_irqrestore(&ctx->tx_pending_rmt_done_lock_lhc4, flags);

	GLINK_ERR_CH(ctx, "%s: R[%u] Tx packet for intent not found.\n",
			__func__, riid);
	return NULL;
}

/**
 * ch_remove_tx_pending_remote_done() - Removes a packet transmit context for a
 *                     packet that is waiting for the remote-done notification
 * @ctx:	Pointer to the channel context
 * @tx_pkt:	Pointer to the transmit packet
 *
 * This function parses through tx_pending_remote_done and removes a
 * packet that matches with the tx_pkt.
 */
void ch_remove_tx_pending_remote_done(struct channel_ctx *ctx,
	struct glink_core_tx_pkt *tx_pkt)
{
	struct glink_core_tx_pkt *local_tx_pkt, *tmp_tx_pkt;
	unsigned long flags;

	if (!ctx || !tx_pkt) {
		GLINK_ERR("%s: Invalid input", __func__);
		return;
	}

	spin_lock_irqsave(&ctx->tx_pending_rmt_done_lock_lhc4, flags);
	list_for_each_entry_safe(local_tx_pkt, tmp_tx_pkt,
			&ctx->tx_pending_remote_done, list_done) {
		if (tx_pkt == local_tx_pkt) {
			list_del_init(&tx_pkt->list_done);
			GLINK_DBG_CH(ctx,
				"%s: R[%u] Removed Tx packet for intent\n",
				__func__,
				tx_pkt->riid);
			rwref_put(&tx_pkt->pkt_ref);
			spin_unlock_irqrestore(
				&ctx->tx_pending_rmt_done_lock_lhc4, flags);
			return;
		}
	}
	spin_unlock_irqrestore(&ctx->tx_pending_rmt_done_lock_lhc4, flags);

	GLINK_ERR_CH(ctx, "%s: R[%u] Tx packet for intent not found", __func__,
			tx_pkt->riid);
}

/**
 * glink_add_free_lcid_list() - adds the lcid of a to be deleted channel to
 *				available lcid list
 * @ctx:	Pointer to channel context.
 */
static void glink_add_free_lcid_list(struct channel_ctx *ctx)
{
	struct channel_lcid *free_lcid;
	unsigned long flags;

	free_lcid = kzalloc(sizeof(*free_lcid), GFP_KERNEL);
	if (!free_lcid) {
		GLINK_ERR(
			"%s: allocation failed on xprt:edge [%s:%s] for lcid [%d]\n",
			__func__, ctx->transport_ptr->name,
			ctx->transport_ptr->edge, ctx->lcid);
		return;
	}
	free_lcid->lcid = ctx->lcid;
	spin_lock_irqsave(&ctx->transport_ptr->xprt_ctx_lock_lhb1, flags);
	list_add_tail(&free_lcid->list_node,
			&ctx->transport_ptr->free_lcid_list);
	spin_unlock_irqrestore(&ctx->transport_ptr->xprt_ctx_lock_lhb1,
					flags);
}

/**
 * glink_ch_ctx_release - Free the channel context
 * @ch_st_lock:	handle to the rwref_lock associated with the chanel
 *
 * This should only be called when the reference count associated with the
 * channel goes to zero.
 */
static void glink_ch_ctx_release(struct rwref_lock *ch_st_lock)
{
	struct channel_ctx *ctx = container_of(ch_st_lock, struct channel_ctx,
						ch_state_lhc0);
	ctx->transport_ptr = NULL;
	kfree(ctx);
	GLINK_INFO("%s: freed the channel ctx in pid [%d]\n", __func__,
			current->pid);
	ctx = NULL;
}

/**
 * ch_name_to_ch_ctx_create() - lookup a channel by name, create the channel if
 *                              it is not found.
 * @xprt_ctx:	Transport to search for a matching channel.
 * @name:	Name of the desired channel.
 *
 * Return: The channel corresponding to @name, NULL if a matching channel was
 *         not found AND a new channel could not be created.
 */
static struct channel_ctx *ch_name_to_ch_ctx_create(
					struct glink_core_xprt_ctx *xprt_ctx,
					const char *name)
{
	struct channel_ctx *entry;
	struct channel_ctx *ctx;
	struct channel_ctx *temp;
	unsigned long flags;
	struct channel_lcid *flcid;

	ctx = kzalloc(sizeof(struct channel_ctx), GFP_KERNEL);
	if (!ctx) {
		GLINK_ERR_XPRT(xprt_ctx, "%s: Failed to allocated ctx, %s",
			"checking if there is one existing\n",
			__func__);
		goto check_ctx;
	}

	ctx->local_open_state = GLINK_CHANNEL_CLOSED;
	strlcpy(ctx->name, name, GLINK_NAME_SIZE);
	rwref_lock_init(&ctx->ch_state_lhc0, glink_ch_ctx_release);
	INIT_LIST_HEAD(&ctx->tx_ready_list_node);
	init_completion(&ctx->int_req_ack_complete);
	init_completion(&ctx->int_req_complete);
	INIT_LIST_HEAD(&ctx->local_rx_intent_list);
	INIT_LIST_HEAD(&ctx->local_rx_intent_ntfy_list);
	INIT_LIST_HEAD(&ctx->local_rx_intent_free_list);
	spin_lock_init(&ctx->local_rx_intent_lst_lock_lhc1);
	INIT_LIST_HEAD(&ctx->rmt_rx_intent_list);
	spin_lock_init(&ctx->rmt_rx_intent_lst_lock_lhc2);
	INIT_LIST_HEAD(&ctx->tx_active);
	spin_lock_init(&ctx->tx_pending_rmt_done_lock_lhc4);
	INIT_LIST_HEAD(&ctx->tx_pending_remote_done);
	spin_lock_init(&ctx->tx_lists_lock_lhc3);

check_ctx:
	rwref_write_get(&xprt_ctx->xprt_state_lhb0);
	if (xprt_ctx->local_state != GLINK_XPRT_OPENED) {
		kfree(ctx);
		rwref_write_put(&xprt_ctx->xprt_state_lhb0);
		return NULL;
	}
	spin_lock_irqsave(&xprt_ctx->xprt_ctx_lock_lhb1, flags);
	list_for_each_entry_safe(entry, temp, &xprt_ctx->channels,
		    port_list_node)
		if (!strcmp(entry->name, name) && !entry->pending_delete) {
			spin_unlock_irqrestore(&xprt_ctx->xprt_ctx_lock_lhb1,
					flags);
			kfree(ctx);
			rwref_write_put(&xprt_ctx->xprt_state_lhb0);
			return entry;
		}

	if (ctx) {
		if (list_empty(&xprt_ctx->free_lcid_list)) {
			if (xprt_ctx->next_lcid > xprt_ctx->max_cid) {
				/* no more channels available */
				GLINK_ERR_XPRT(xprt_ctx,
					"%s: unable to exceed %u channels\n",
					__func__, xprt_ctx->max_cid);
				spin_unlock_irqrestore(
						&xprt_ctx->xprt_ctx_lock_lhb1,
						flags);
				kfree(ctx);
				rwref_write_put(&xprt_ctx->xprt_state_lhb0);
				return NULL;
			} else {
				ctx->lcid = xprt_ctx->next_lcid++;
			}
		} else {
			flcid = list_first_entry(&xprt_ctx->free_lcid_list,
						struct channel_lcid, list_node);
			ctx->lcid = flcid->lcid;
			list_del(&flcid->list_node);
			kfree(flcid);
		}

		list_add_tail(&ctx->port_list_node, &xprt_ctx->channels);

		GLINK_INFO_PERF_CH_XPRT(ctx, xprt_ctx,
			"%s: local:GLINK_CHANNEL_CLOSED\n",
			__func__);
	}
	spin_unlock_irqrestore(&xprt_ctx->xprt_ctx_lock_lhb1, flags);
	rwref_write_put(&xprt_ctx->xprt_state_lhb0);
	mutex_lock(&xprt_ctx->xprt_dbgfs_lock_lhb3);
	if (ctx != NULL)
		glink_debugfs_add_channel(ctx, xprt_ctx);
	mutex_unlock(&xprt_ctx->xprt_dbgfs_lock_lhb3);
	return ctx;
}

/**
 * ch_add_rcid() - add a remote channel identifier to an existing channel
 * @xprt_ctx:	Transport the channel resides on.
 * @ctx:	Channel receiving the identifier.
 * @rcid:	The remote channel identifier.
 */
static void ch_add_rcid(struct glink_core_xprt_ctx *xprt_ctx,
			struct channel_ctx *ctx,
			uint32_t rcid)
{
	ctx->rcid = rcid;
}

/**
 * ch_update_local_state() - Update the local channel state
 * @ctx:	Pointer to channel context.
 * @lstate:	Local channel state.
 *
 * Return: True if the channel is fully closed as a result of this update,
 *	false otherwise.
 */
static bool ch_update_local_state(struct channel_ctx *ctx,
					enum local_channel_state_e lstate)
{
	bool is_fully_closed;

	rwref_write_get(&ctx->ch_state_lhc0);
	ctx->local_open_state = lstate;
	is_fully_closed = ch_is_fully_closed(ctx);
	rwref_write_put(&ctx->ch_state_lhc0);

	return is_fully_closed;
}

/**
 * ch_update_local_state() - Update the local channel state
 * @ctx:	Pointer to channel context.
 * @rstate:	Remote Channel state.
 *
 * Return: True if the channel is fully closed as result of this update,
 *	false otherwise.
 */
static bool ch_update_rmt_state(struct channel_ctx *ctx, bool rstate)
{
	bool is_fully_closed;

	rwref_write_get(&ctx->ch_state_lhc0);
	ctx->remote_opened = rstate;
	is_fully_closed = ch_is_fully_closed(ctx);
	rwref_write_put(&ctx->ch_state_lhc0);

	return is_fully_closed;
}

/*
 * ch_is_fully_opened() - Verify if a channel is open
 * ctx:	Pointer to channel context
 *
 * Return: True if open, else flase
 */
static bool ch_is_fully_opened(struct channel_ctx *ctx)
{
	if (ctx->remote_opened && ctx->local_open_state == GLINK_CHANNEL_OPENED)
		return true;

	return false;
}

/*
 * ch_is_fully_closed() - Verify if a channel is closed on both sides
 * @ctx: Pointer to channel context
 * @returns: True if open, else flase
 */
static bool ch_is_fully_closed(struct channel_ctx *ctx)
{
	if (!ctx->remote_opened &&
			ctx->local_open_state == GLINK_CHANNEL_CLOSED)
		return true;

	return false;
}

/**
 * find_open_transport() - find a specific open transport
 * @edge:		Edge the transport is on.
 * @name:		Name of the transport (or NULL if no preference)
 * @initial_xprt:	The specified transport is the start for migration
 * @best_id:		The best transport found for this connection
 *
 * Find an open transport corresponding to the specified @name and @edge.  @edge
 * is expected to be valid.  @name is expected to be NULL (unspecified) or
 * valid.  If @name is not specified, then the best transport found on the
 * specified edge will be returned.
 *
 * Return: Transport with the specified name on the specified edge, if open.
 *	NULL if the transport exists, but is not fully open.  ENODEV if no such
 *	transport exists.
 */
static struct glink_core_xprt_ctx *find_open_transport(const char *edge,
						       const char *name,
						       bool initial_xprt,
						       uint16_t *best_id)
{
	struct glink_core_xprt_ctx *xprt;
	struct glink_core_xprt_ctx *best_xprt = NULL;
	struct glink_core_xprt_ctx *ret;
	bool first = true;

	ret = (struct glink_core_xprt_ctx *)ERR_PTR(-ENODEV);
	*best_id = USHRT_MAX;

	mutex_lock(&transport_list_lock_lha0);
	list_for_each_entry(xprt, &transport_list, list_node) {
		if (strcmp(edge, xprt->edge))
			continue;
		if (first) {
			first = false;
			ret = NULL;
		}
		if (!xprt_is_fully_opened(xprt))
			continue;

		if (xprt->id < *best_id) {
			*best_id = xprt->id;
			best_xprt = xprt;
		}

		/*
		 * Braces are required in this instacne because the else will
		 * attach to the wrong if otherwise.
		 */
		if (name) {
			if (!strcmp(name, xprt->name))
				ret = xprt;
		} else {
			ret = best_xprt;
		}
	}

	mutex_unlock(&transport_list_lock_lha0);

	if (IS_ERR_OR_NULL(ret))
		return ret;
	if (!initial_xprt)
		*best_id = ret->id;

	return ret;
}

/**
 * xprt_is_fully_opened() - check the open status of a transport
 * @xprt:	Transport being checked.
 *
 * Return: True if the transport is fully opened, false otherwise.
 */
static bool xprt_is_fully_opened(struct glink_core_xprt_ctx *xprt)
{
	if (xprt->remote_neg_completed &&
					xprt->local_state == GLINK_XPRT_OPENED)
		return true;

	return false;
}

/**
 * glink_dummy_notify_rx_intent_req() - Dummy RX Request
 *
 * @handle:	Channel handle (ignored)
 * @priv:	Private data pointer (ignored)
 * @req_size:	Requested size (ignored)
 *
 * Dummy RX intent request if client does not implement the optional callback
 * function.
 *
 * Return:  False
 */
static bool glink_dummy_notify_rx_intent_req(void *handle, const void *priv,
	size_t req_size)
{
	return false;
}

/**
 * glink_dummy_notify_rx_sigs() - Dummy signal callback
 *
 * @handle:	Channel handle (ignored)
 * @priv:	Private data pointer (ignored)
 * @req_size:	Requested size (ignored)
 *
 * Dummy signal callback if client does not implement the optional callback
 * function.
 *
 * Return:  False
 */
static void glink_dummy_notify_rx_sigs(void *handle, const void *priv,
				uint32_t old_sigs, uint32_t new_sigs)
{
	/* intentionally left blank */
}

/**
 * glink_dummy_rx_abort() - Dummy rx abort callback
 *
 * handle:	Channel handle (ignored)
 * priv:	Private data pointer (ignored)
 * pkt_priv:	Private intent data pointer (ignored)
 *
 * Dummy rx abort callback if client does not implement the optional callback
 * function.
 */
static void glink_dummy_notify_rx_abort(void *handle, const void *priv,
				const void *pkt_priv)
{
	/* intentionally left blank */
}

/**
 * glink_dummy_tx_abort() - Dummy tx abort callback
 *
 * @handle:	Channel handle (ignored)
 * @priv:	Private data pointer (ignored)
 * @pkt_priv:	Private intent data pointer (ignored)
 *
 * Dummy tx abort callback if client does not implement the optional callback
 * function.
 */
static void glink_dummy_notify_tx_abort(void *handle, const void *priv,
				const void *pkt_priv)
{
	/* intentionally left blank */
}

/**
 * dummy_poll() - a dummy poll() for transports that don't define one
 * @if_ptr:	The transport interface handle for this transport.
 * @lcid:	The channel to poll.
 *
 * Return: An error to indicate that this operation is unsupported.
 */
static int dummy_poll(struct glink_transport_if *if_ptr, uint32_t lcid)
{
	return -EOPNOTSUPP;
}

/**
 * dummy_reuse_rx_intent() - a dummy reuse_rx_intent() for transports that
 *			     don't define one
 * @if_ptr:	The transport interface handle for this transport.
 * @intent:	The intent to reuse.
 *
 * Return: Success.
 */
static int dummy_reuse_rx_intent(struct glink_transport_if *if_ptr,
				 struct glink_core_rx_intent *intent)
{
	return 0;
}

/**
 * dummy_mask_rx_irq() - a dummy mask_rx_irq() for transports that don't define
 *			 one
 * @if_ptr:	The transport interface handle for this transport.
 * @lcid:	The local channel id for this channel.
 * @mask:	True to mask the irq, false to unmask.
 * @pstruct:	Platform defined structure with data necessary for masking.
 *
 * Return: An error to indicate that this operation is unsupported.
 */
static int dummy_mask_rx_irq(struct glink_transport_if *if_ptr, uint32_t lcid,
			     bool mask, void *pstruct)
{
	return -EOPNOTSUPP;
}

/**
 * dummy_wait_link_down() - a dummy wait_link_down() for transports that don't
 *			define one
 * @if_ptr:	The transport interface handle for this transport.
 *
 * Return: An error to indicate that this operation is unsupported.
 */
static int dummy_wait_link_down(struct glink_transport_if *if_ptr)
{
	return -EOPNOTSUPP;
}

/**
 * dummy_allocate_rx_intent() - a dummy RX intent allocation function that does
 *				not allocate anything
 * @if_ptr:	The transport the intent is associated with.
 * @size:	Size of intent.
 * @intent:	Pointer to the intent structure.
 *
 * Return:	Success.
 */
static int dummy_allocate_rx_intent(struct glink_transport_if *if_ptr,
			size_t size, struct glink_core_rx_intent *intent)
{
	return 0;
}

/**
 * dummy_tx_cmd_tracer_pkt() - a dummy tracer packet tx cmd for transports
 *                             that don't define one
 * @if_ptr:	The transport interface handle for this transport.
 * @lcid:	The channel in which the tracer packet is transmitted.
 * @pctx:	Context of the packet to be transmitted.
 *
 * Return: 0.
 */
static int dummy_tx_cmd_tracer_pkt(struct glink_transport_if *if_ptr,
		uint32_t lcid, struct glink_core_tx_pkt *pctx)
{
	pctx->size_remaining = 0;
	return 0;
}

/**
 * dummy_deallocate_rx_intent() - a dummy rx intent deallocation function that
 *				does not deallocate anything
 * @if_ptr:	The transport the intent is associated with.
 * @intent:	Pointer to the intent structure.
 *
 * Return:	Success.
 */
static int dummy_deallocate_rx_intent(struct glink_transport_if *if_ptr,
				struct glink_core_rx_intent *intent)
{
	return 0;
}

/**
 * dummy_tx_cmd_local_rx_intent() - dummy local rx intent request
 * @if_ptr:	The transport to transmit on.
 * @lcid:	The local channel id to encode.
 * @size:	The intent size to encode.
 * @liid:	The local intent id to encode.
 *
 * Return:	Success.
 */
static int dummy_tx_cmd_local_rx_intent(struct glink_transport_if *if_ptr,
				uint32_t lcid, size_t size, uint32_t liid)
{
	return 0;
}

/**
 * dummy_tx_cmd_local_rx_done() - dummy rx done command
 * @if_ptr:	The transport to transmit on.
 * @lcid:	The local channel id to encode.
 * @liid:	The local intent id to encode.
 * @reuse:	Reuse the consumed intent.
 */
static void dummy_tx_cmd_local_rx_done(struct glink_transport_if *if_ptr,
				uint32_t lcid, uint32_t liid, bool reuse)
{
	/* intentionally left blank */
}

/**
 * dummy_tx() - dummy tx() that does not send anything
 * @if_ptr:	The transport to transmit on.
 * @lcid:	The local channel id to encode.
 * @pctx:	The data to encode.
 *
 * Return: Number of bytes written i.e. zero.
 */
static int dummy_tx(struct glink_transport_if *if_ptr, uint32_t lcid,
				struct glink_core_tx_pkt *pctx)
{
	return 0;
}

/**
 * dummy_tx_cmd_rx_intent_req() - dummy rx intent request functon
 * @if_ptr:	The transport to transmit on.
 * @lcid:	The local channel id to encode.
 * @size:	The requested intent size to encode.
 *
 * Return:	Success.
 */
static int dummy_tx_cmd_rx_intent_req(struct glink_transport_if *if_ptr,
				uint32_t lcid, size_t size)
{
	return 0;
}

/**
 * dummy_tx_cmd_rx_intent_req_ack() - dummy rx intent request ack
 * @if_ptr:	The transport to transmit on.
 * @lcid:	The local channel id to encode.
 * @granted:	The request response to encode.
 *
 * Return:	Success.
 */
static int dummy_tx_cmd_remote_rx_intent_req_ack(
					struct glink_transport_if *if_ptr,
					uint32_t lcid, bool granted)
{
	return 0;
}

/**
 * dummy_tx_cmd_set_sigs() - dummy signals ack transmit function
 * @if_ptr:	The transport to transmit on.
 * @lcid:	The local channel id to encode.
 * @sigs:	The signals to encode.
 *
 * Return:	Success.
 */
static int dummy_tx_cmd_set_sigs(struct glink_transport_if *if_ptr,
				uint32_t lcid, uint32_t sigs)
{
	return 0;
}

/**
 * dummy_tx_cmd_ch_close() - dummy channel close transmit function
 * @if_ptr:	The transport to transmit on.
 * @lcid:	The local channel id to encode.
 *
 * Return:	Success.
 */
static int dummy_tx_cmd_ch_close(struct glink_transport_if *if_ptr,
				uint32_t lcid)
{
	return 0;
}

/**
 * dummy_tx_cmd_ch_remote_close_ack() - dummy channel close ack sending function
 * @if_ptr:	The transport to transmit on.
 * @rcid:	The remote channel id to encode.
 */
static void dummy_tx_cmd_ch_remote_close_ack(struct glink_transport_if *if_ptr,
				       uint32_t rcid)
{
	/* intentionally left blank */
}

/**
 * dummy_get_power_vote_ramp_time() - Dummy Power vote ramp time
 * @if_ptr:	The transport to transmit on.
 * @state:	The power state being requested from the transport.
 */
static unsigned long dummy_get_power_vote_ramp_time(
		struct glink_transport_if *if_ptr, uint32_t state)
{
	return (unsigned long)-EOPNOTSUPP;
}

/**
 * dummy_power_vote() - Dummy Power vote operation
 * @if_ptr:	The transport to transmit on.
 * @state:	The power state being requested from the transport.
 */
static int dummy_power_vote(struct glink_transport_if *if_ptr,
			    uint32_t state)
{
	return -EOPNOTSUPP;
}

/**
 * dummy_power_unvote() - Dummy Power unvote operation
 * @if_ptr:	The transport to transmit on.
 */
static int dummy_power_unvote(struct glink_transport_if *if_ptr)
{
	return -EOPNOTSUPP;
}

/**
 * notif_if_up_all_xprts() - Check and notify existing transport state if up
 * @notif_info:	Data structure containing transport information to be notified.
 *
 * This function is called when the client registers a notifier to know about
 * the state of a transport. This function matches the existing transports with
 * the transport in the "notif_info" parameter. When a matching transport is
 * found, the callback function in the "notif_info" parameter is called with
 * the state of the matching transport.
 *
 * If an edge or transport is not defined, then all edges and/or transports
 * will be matched and will receive up notifications.
 */
static void notif_if_up_all_xprts(
		struct link_state_notifier_info *notif_info)
{
	struct glink_core_xprt_ctx *xprt_ptr;
	struct glink_link_state_cb_info cb_info;

	cb_info.link_state = GLINK_LINK_STATE_UP;
	mutex_lock(&transport_list_lock_lha0);
	list_for_each_entry(xprt_ptr, &transport_list, list_node) {
		if (strlen(notif_info->edge) &&
		    strcmp(notif_info->edge, xprt_ptr->edge))
			continue;

		if (strlen(notif_info->transport) &&
		    strcmp(notif_info->transport, xprt_ptr->name))
			continue;

		if (!xprt_is_fully_opened(xprt_ptr))
			continue;

		cb_info.transport = xprt_ptr->name;
		cb_info.edge = xprt_ptr->edge;
		notif_info->glink_link_state_notif_cb(&cb_info,
						notif_info->priv);
	}
	mutex_unlock(&transport_list_lock_lha0);
}

/**
 * check_link_notifier_and_notify() - Check and notify clients about link state
 * @xprt_ptr:	Transport whose state to be notified.
 * @link_state:	State of the transport to be notified.
 *
 * This function is called when the state of the transport changes. This
 * function matches the transport with the clients that have registered to
 * be notified about the state changes. When a matching client notifier is
 * found, the callback function in the client notifier is called with the
 * new state of the transport.
 */
static void check_link_notifier_and_notify(struct glink_core_xprt_ctx *xprt_ptr,
					   enum glink_link_state link_state)
{
	struct link_state_notifier_info *notif_info;
	struct glink_link_state_cb_info cb_info;

	cb_info.link_state = link_state;
	mutex_lock(&link_state_notifier_lock_lha1);
	list_for_each_entry(notif_info, &link_state_notifier_list, list) {
		if (strlen(notif_info->edge) &&
		    strcmp(notif_info->edge, xprt_ptr->edge))
			continue;

		if (strlen(notif_info->transport) &&
		    strcmp(notif_info->transport, xprt_ptr->name))
			continue;

		cb_info.transport = xprt_ptr->name;
		cb_info.edge = xprt_ptr->edge;
		notif_info->glink_link_state_notif_cb(&cb_info,
						notif_info->priv);
	}
	mutex_unlock(&link_state_notifier_lock_lha1);
}

/**
 * Open GLINK channel.
 *
 * @cfg_ptr:	Open configuration structure (the structure is copied before
 *		glink_open returns).  All unused fields should be zero-filled.
 *
 * This should not be called from link state callback context by clients.
 * It is recommended that client should invoke this function from their own
 * thread.
 *
 * Return:  Pointer to channel on success, PTR_ERR() with standard Linux
 * error code on failure.
 */
void *glink_open(const struct glink_open_config *cfg)
{
	struct channel_ctx *ctx = NULL;
	struct glink_core_xprt_ctx *transport_ptr;
	size_t len;
	int ret;
	uint16_t best_id;

	if (!cfg->edge || !cfg->name) {
		GLINK_ERR("%s: !cfg->edge || !cfg->name\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	len = strlen(cfg->edge);
	if (len == 0 || len >= GLINK_NAME_SIZE) {
		GLINK_ERR("%s: [EDGE] len == 0 || len >= GLINK_NAME_SIZE\n",
				__func__);
		return ERR_PTR(-EINVAL);
	}

	len = strlen(cfg->name);
	if (len == 0 || len >= GLINK_NAME_SIZE) {
		GLINK_ERR("%s: [NAME] len == 0 || len >= GLINK_NAME_SIZE\n",
				__func__);
		return ERR_PTR(-EINVAL);
	}

	if (cfg->transport) {
		len = strlen(cfg->transport);
		if (len == 0 || len >= GLINK_NAME_SIZE) {
			GLINK_ERR("%s: [TRANSPORT] len == 0 || %s\n",
				__func__,
				"len >= GLINK_NAME_SIZE");
			return ERR_PTR(-EINVAL);
		}
	}

	/* confirm required notification parameters */
	if (!(cfg->notify_rx || cfg->notify_rxv) || !cfg->notify_tx_done
		|| !cfg->notify_state
		|| ((cfg->options & GLINK_OPT_RX_INTENT_NOTIF)
			&& !cfg->notify_remote_rx_intent)) {
		GLINK_ERR("%s: Incorrect notification parameters\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	/* find transport */
	transport_ptr = find_open_transport(cfg->edge, cfg->transport,
					cfg->options & GLINK_OPT_INITIAL_XPORT,
					&best_id);
	if (IS_ERR_OR_NULL(transport_ptr)) {
		GLINK_ERR("%s:%s %s: Error %d - unable to find transport\n",
				cfg->transport, cfg->edge, __func__,
				(unsigned)PTR_ERR(transport_ptr));
		return ERR_PTR(-ENODEV);
	}

	/*
	 * look for an existing port structure which can occur in
	 * reopen and remote-open-first cases
	 */
	ctx = ch_name_to_ch_ctx_create(transport_ptr, cfg->name);
	if (ctx == NULL) {
		GLINK_ERR("%s:%s %s: Error - unable to allocate new channel\n",
				cfg->transport, cfg->edge, __func__);
		return ERR_PTR(-ENOMEM);
	}

	/* port already exists */
	if (ctx->local_open_state != GLINK_CHANNEL_CLOSED) {
		/* not ready to be re-opened */
		GLINK_INFO_CH_XPRT(ctx, transport_ptr,
		"%s: Channel not ready to be re-opened. State: %u\n",
		__func__, ctx->local_open_state);
		return ERR_PTR(-EBUSY);
	}

	/* initialize port structure */
	ctx->user_priv = cfg->priv;
	ctx->rx_intent_req_timeout_jiffies =
		msecs_to_jiffies(cfg->rx_intent_req_timeout_ms);
	ctx->notify_rx = cfg->notify_rx;
	ctx->notify_tx_done = cfg->notify_tx_done;
	ctx->notify_state = cfg->notify_state;
	ctx->notify_rx_intent_req = cfg->notify_rx_intent_req;
	ctx->notify_rxv = cfg->notify_rxv;
	ctx->notify_rx_sigs = cfg->notify_rx_sigs;
	ctx->notify_rx_abort = cfg->notify_rx_abort;
	ctx->notify_tx_abort = cfg->notify_tx_abort;
	ctx->notify_rx_tracer_pkt = cfg->notify_rx_tracer_pkt;
	ctx->notify_remote_rx_intent = cfg->notify_remote_rx_intent;

	if (!ctx->notify_rx_intent_req)
		ctx->notify_rx_intent_req = glink_dummy_notify_rx_intent_req;
	if (!ctx->notify_rx_sigs)
		ctx->notify_rx_sigs = glink_dummy_notify_rx_sigs;
	if (!ctx->notify_rx_abort)
		ctx->notify_rx_abort = glink_dummy_notify_rx_abort;
	if (!ctx->notify_tx_abort)
		ctx->notify_tx_abort = glink_dummy_notify_tx_abort;

	if (!ctx->rx_intent_req_timeout_jiffies)
		ctx->rx_intent_req_timeout_jiffies = MAX_SCHEDULE_TIMEOUT;

	ctx->local_xprt_req = best_id;
	ctx->no_migrate = cfg->transport &&
				!(cfg->options & GLINK_OPT_INITIAL_XPORT);
	ctx->transport_ptr = transport_ptr;
	ctx->local_open_state = GLINK_CHANNEL_OPENING;
	GLINK_INFO_PERF_CH(ctx,
		"%s: local:GLINK_CHANNEL_CLOSED->GLINK_CHANNEL_OPENING\n",
		__func__);

	/* start local-open sequence */
	ret = ctx->transport_ptr->ops->tx_cmd_ch_open(ctx->transport_ptr->ops,
		ctx->lcid, cfg->name, best_id);
	if (ret) {
		/* failure to send open command (transport failure) */
		ctx->local_open_state = GLINK_CHANNEL_CLOSED;
		GLINK_ERR_CH(ctx, "%s: Unable to send open command %d\n",
			__func__, ret);
		return ERR_PTR(ret);
	}

	GLINK_INFO_CH(ctx, "%s: Created channel, sent OPEN command. ctx %p\n",
			__func__, ctx);

	return ctx;
}
EXPORT_SYMBOL(glink_open);

/**
 * glink_get_channel_id_for_handle() - Get logical channel ID
 *
 * @handle:	handle of channel
 *
 * Used internally by G-Link debugfs.
 *
 * Return:  Logical Channel ID or standard Linux error code
 */
int glink_get_channel_id_for_handle(void *handle)
{
	struct channel_ctx *ctx = (struct channel_ctx *)handle;
	if (ctx == NULL)
		return -EINVAL;

	return ctx->lcid;
}
EXPORT_SYMBOL(glink_get_channel_id_for_handle);

/**
 * glink_get_channel_name_for_handle() - return channel name
 *
 * @handle:	handle of channel
 *
 * Used internally by G-Link debugfs.
 *
 * Return:  Channel name or NULL
 */
char *glink_get_channel_name_for_handle(void *handle)
{
	struct channel_ctx *ctx = (struct channel_ctx *)handle;
	if (ctx == NULL)
		return NULL;

	return ctx->name;
}
EXPORT_SYMBOL(glink_get_channel_name_for_handle);

/**
 * glink_delete_ch_from_list() -  delete the channel from the list
 * @ctx:	Pointer to channel context.
 * @add_flcid:	Boolean value to decide whether the lcid should be added or not.
 *
 * This function deletes the channel from the list along with the debugfs
 * information associated with it. It also adds the channel lcid to the free
 * lcid list except if the channel is deleted in case of ssr/unregister case.
 * It can only called when channel is fully closed.
 */
static void glink_delete_ch_from_list(struct channel_ctx *ctx, bool add_flcid)
{
	unsigned long flags;
	spin_lock_irqsave(&ctx->transport_ptr->xprt_ctx_lock_lhb1,
				flags);
	if (!list_empty(&ctx->port_list_node))
		list_del_init(&ctx->port_list_node);
	spin_unlock_irqrestore(
			&ctx->transport_ptr->xprt_ctx_lock_lhb1,
			flags);
	if (add_flcid)
		glink_add_free_lcid_list(ctx);
	mutex_lock(&ctx->transport_ptr->xprt_dbgfs_lock_lhb3);
	glink_debugfs_remove_channel(ctx, ctx->transport_ptr);
	mutex_unlock(&ctx->transport_ptr->xprt_dbgfs_lock_lhb3);
	rwref_put(&ctx->ch_state_lhc0);
}

/**
 * glink_close() - Close a previously opened channel.
 *
 * @handle:	handle to close
 *
 * Once the closing process has been completed, the GLINK_LOCAL_DISCONNECTED
 * state event will be sent and the channel can be reopened.
 *
 * Return:  0 on success; -EINVAL for invalid handle, -EBUSY is close is
 * already in progress, standard Linux Error code otherwise.
 */
int glink_close(void *handle)
{
	struct glink_core_xprt_ctx *xprt_ctx = NULL;
	struct channel_ctx *ctx = (struct channel_ctx *)handle;
	int ret = 0;
	unsigned long flags;

	if (!ctx)
		return -EINVAL;

	GLINK_INFO_CH(ctx, "%s: Closing channel, ctx: %p\n", __func__, ctx);
	if (ctx->local_open_state == GLINK_CHANNEL_CLOSED)
		return 0;

	if (ctx->local_open_state == GLINK_CHANNEL_CLOSING) {
		/* close already pending */
		return -EBUSY;
	}

	/* Set the channel state before removing it from xprt's list(s) */
	GLINK_INFO_PERF_CH(ctx,
		"%s: local:%u->GLINK_CHANNEL_CLOSING\n",
		__func__, ctx->local_open_state);
	ctx->local_open_state = GLINK_CHANNEL_CLOSING;

	ctx->pending_delete = true;
	ctx->int_req_ack = false;
	complete_all(&ctx->int_req_ack_complete);
	complete_all(&ctx->int_req_complete);

	spin_lock_irqsave(&ctx->transport_ptr->tx_ready_lock_lhb2, flags);
	if (!list_empty(&ctx->tx_ready_list_node))
		list_del_init(&ctx->tx_ready_list_node);
	spin_unlock_irqrestore(&ctx->transport_ptr->tx_ready_lock_lhb2, flags);

	if (ctx->transport_ptr->local_state != GLINK_XPRT_DOWN) {
		glink_qos_reset_priority(ctx);
		ret = ctx->transport_ptr->ops->tx_cmd_ch_close(
				ctx->transport_ptr->ops,
				ctx->lcid);
	} else if (!strcmp(ctx->transport_ptr->name, "dummy")) {
		/*
		 * This check will avoid any race condition when clients call
		 * glink_close before the dummy xprt swapping happens in link
		 * down scenario.
		 */
		ret = 0;
		xprt_ctx = ctx->transport_ptr;
		rwref_write_get(&xprt_ctx->xprt_state_lhb0);
		glink_core_ch_close_ack_common(ctx);
		if (ch_is_fully_closed(ctx)) {
			glink_delete_ch_from_list(ctx, false);
			rwref_put(&xprt_ctx->xprt_state_lhb0);
			if (list_empty(&xprt_ctx->channels))
				/* For the xprt reference */
				rwref_put(&xprt_ctx->xprt_state_lhb0);
		} else {
			GLINK_ERR_CH(ctx,
			"channel Not closed yet local state [%d] remote_state [%d]\n",
			ctx->local_open_state, ctx->remote_opened);
		}
		rwref_write_put(&xprt_ctx->xprt_state_lhb0);
	}

	return ret;
}
EXPORT_SYMBOL(glink_close);

/**
 * glink_tx_pkt_release() - Release a packet's transmit information
 * @tx_pkt_ref:	Packet information which needs to be released.
 *
 * This function is called when all the references to a packet information
 * is dropped.
 */
static void glink_tx_pkt_release(struct rwref_lock *tx_pkt_ref)
{
	struct glink_core_tx_pkt *tx_info = container_of(tx_pkt_ref,
						struct glink_core_tx_pkt,
						pkt_ref);
	if (!list_empty(&tx_info->list_done))
		list_del_init(&tx_info->list_done);
	if (!list_empty(&tx_info->list_node))
		list_del_init(&tx_info->list_node);
	kfree(tx_info);
}

/**
 * glink_tx_common() - Common TX implementation
 *
 * @handle:	handle returned by glink_open()
 * @pkt_priv:	opaque data value that will be returned to client with
 *		notify_tx_done notification
 * @data:	pointer to the data
 * @size:	size of data
 * @vbuf_provider: Virtual Address-space Buffer Provider for the tx buffer.
 * @vbuf_provider: Physical Address-space Buffer Provider for the tx buffer.
 * @tx_flags:	Flags to indicate transmit options
 *
 * Return:	-EINVAL for invalid handle; -EBUSY if channel isn't ready for
 *		transmit operation (not fully opened); -EAGAIN if remote side
 *		has not provided a receive intent that is big enough.
 */
static int glink_tx_common(void *handle, void *pkt_priv,
	void *data, void *iovec, size_t size,
	void * (*vbuf_provider)(void *iovec, size_t offset, size_t *size),
	void * (*pbuf_provider)(void *iovec, size_t offset, size_t *size),
	uint32_t tx_flags)
{
	struct channel_ctx *ctx = (struct channel_ctx *)handle;
	uint32_t riid;
	int ret = 0;
	struct glink_core_tx_pkt *tx_info;
	size_t intent_size;
	bool is_atomic =
		tx_flags & (GLINK_TX_SINGLE_THREADED | GLINK_TX_ATOMIC);
	unsigned long flags;

	if (!size)
		return -EINVAL;

	if (!ctx)
		return -EINVAL;

	rwref_get(&ctx->ch_state_lhc0);
	if (!(vbuf_provider || pbuf_provider)) {
		rwref_put(&ctx->ch_state_lhc0);
		return -EINVAL;
	}

	if (!ch_is_fully_opened(ctx)) {
		rwref_put(&ctx->ch_state_lhc0);
		return -EBUSY;
	}

	if (size > GLINK_MAX_PKT_SIZE) {
		rwref_put(&ctx->ch_state_lhc0);
		return -EINVAL;
	}

	if (unlikely(tx_flags & GLINK_TX_TRACER_PKT)) {
		if (!(ctx->transport_ptr->capabilities & GCAP_TRACER_PKT)) {
			rwref_put(&ctx->ch_state_lhc0);
			return -EOPNOTSUPP;
		}
		tracer_pkt_log_event(data, GLINK_CORE_TX);
	}

	/* find matching rx intent (first-fit algorithm for now) */
	if (ch_pop_remote_rx_intent(ctx, size, &riid, &intent_size)) {
		if (!(tx_flags & GLINK_TX_REQ_INTENT)) {
			/* no rx intent available */
			GLINK_ERR_CH(ctx,
				"%s: R[%u]:%zu Intent not present for lcid\n",
				__func__, riid, size);
			rwref_put(&ctx->ch_state_lhc0);
			return -EAGAIN;
		}
		if (is_atomic && !(ctx->transport_ptr->capabilities &
					  GCAP_AUTO_QUEUE_RX_INT)) {
			GLINK_ERR_CH(ctx,
				"%s: Cannot request intent in atomic context\n",
				__func__);
			rwref_put(&ctx->ch_state_lhc0);
			return -EINVAL;
		}

		/* request intent of correct size */
		reinit_completion(&ctx->int_req_ack_complete);
		ret = ctx->transport_ptr->ops->tx_cmd_rx_intent_req(
				ctx->transport_ptr->ops, ctx->lcid, size);
		if (ret) {
			GLINK_ERR_CH(ctx, "%s: Request intent failed %d\n",
					__func__, ret);
			rwref_put(&ctx->ch_state_lhc0);
			return ret;
		}

		while (ch_pop_remote_rx_intent(ctx, size, &riid,
						&intent_size)) {
			if (is_atomic) {
				GLINK_ERR_CH(ctx,
				    "%s Intent of size %zu not ready\n",
				    __func__, size);
				rwref_put(&ctx->ch_state_lhc0);
				return -EAGAIN;
			}

			if (ctx->transport_ptr->local_state == GLINK_XPRT_DOWN
			    || !ch_is_fully_opened(ctx)) {
				GLINK_ERR_CH(ctx,
					"%s: Channel closed while waiting for intent\n",
					__func__);
				rwref_put(&ctx->ch_state_lhc0);
				return -EBUSY;
			}

			/* wait for the remote intent req ack */
			if (!wait_for_completion_timeout(
					&ctx->int_req_ack_complete,
					ctx->rx_intent_req_timeout_jiffies)) {
				GLINK_ERR_CH(ctx,
					"%s: Intent request ack with size: %zu not granted for lcid\n",
					__func__, size);
				rwref_put(&ctx->ch_state_lhc0);
				return -ETIMEDOUT;
			}

			if (!ctx->int_req_ack) {
				GLINK_ERR_CH(ctx,
				    "%s: Intent Request with size: %zu %s",
				    __func__, size,
				    "not granted for lcid\n");
				rwref_put(&ctx->ch_state_lhc0);
				return -EAGAIN;
			}

			/* wait for the rx_intent from remote side */
			if (!wait_for_completion_timeout(
					&ctx->int_req_complete,
					ctx->rx_intent_req_timeout_jiffies)) {
				GLINK_ERR_CH(ctx,
					"%s: Intent request with size: %zu not granted for lcid\n",
					__func__, size);
				rwref_put(&ctx->ch_state_lhc0);
				return -ETIMEDOUT;
			}

			reinit_completion(&ctx->int_req_complete);
		}
	}

	if (!is_atomic) {
		spin_lock_irqsave(&ctx->transport_ptr->tx_ready_lock_lhb2,
				  flags);
		glink_pm_qos_vote(ctx->transport_ptr);
		spin_unlock_irqrestore(&ctx->transport_ptr->tx_ready_lock_lhb2,
					flags);
	}

	GLINK_INFO_PERF_CH(ctx, "%s: R[%u]:%zu data[%p], size[%zu]. TID %u\n",
			__func__, riid, intent_size,
			data ? data : iovec, size, current->pid);
	tx_info = kzalloc(sizeof(struct glink_core_tx_pkt),
				is_atomic ? GFP_ATOMIC : GFP_KERNEL);
	if (!tx_info) {
		GLINK_ERR_CH(ctx, "%s: No memory for allocation\n", __func__);
		ch_push_remote_rx_intent(ctx, intent_size, riid);
		rwref_put(&ctx->ch_state_lhc0);
		return -ENOMEM;
	}
	rwref_lock_init(&tx_info->pkt_ref, glink_tx_pkt_release);
	INIT_LIST_HEAD(&tx_info->list_done);
	INIT_LIST_HEAD(&tx_info->list_node);
	tx_info->pkt_priv = pkt_priv;
	tx_info->data = data;
	tx_info->riid = riid;
	tx_info->rcid = ctx->rcid;
	tx_info->size = size;
	tx_info->size_remaining = size;
	tx_info->tracer_pkt = tx_flags & GLINK_TX_TRACER_PKT ? true : false;
	tx_info->iovec = iovec ? iovec : (void *)tx_info;
	tx_info->vprovider = vbuf_provider;
	tx_info->pprovider = pbuf_provider;
	tx_info->intent_size = intent_size;

	/* schedule packet for transmit */
	if ((tx_flags & GLINK_TX_SINGLE_THREADED) &&
	    (ctx->transport_ptr->capabilities & GCAP_INTENTLESS))
		ret = xprt_single_threaded_tx(ctx->transport_ptr,
					       ctx, tx_info);
	else
		xprt_schedule_tx(ctx->transport_ptr, ctx, tx_info);

	rwref_put(&ctx->ch_state_lhc0);
	return ret;
}

/**
 * glink_tx() - Transmit packet.
 *
 * @handle:	handle returned by glink_open()
 * @pkt_priv:	opaque data value that will be returned to client with
 *		notify_tx_done notification
 * @data:	pointer to the data
 * @size:	size of data
 * @tx_flags:	Flags to specify transmit specific options
 *
 * Return:	-EINVAL for invalid handle; -EBUSY if channel isn't ready for
 *		transmit operation (not fully opened); -EAGAIN if remote side
 *		has not provided a receive intent that is big enough.
 */
int glink_tx(void *handle, void *pkt_priv, void *data, size_t size,
							uint32_t tx_flags)
{
	return glink_tx_common(handle, pkt_priv, data, NULL, size,
			       tx_linear_vbuf_provider, NULL, tx_flags);
}
EXPORT_SYMBOL(glink_tx);

/**
 * glink_queue_rx_intent() - Register an intent to receive data.
 *
 * @handle:	handle returned by glink_open()
 * @pkt_priv:	opaque data type that is returned when a packet is received
 * size:	maximum size of data to receive
 *
 * Return: 0 for success; standard Linux error code for failure case
 */
int glink_queue_rx_intent(void *handle, const void *pkt_priv, size_t size)
{
	struct channel_ctx *ctx = (struct channel_ctx *)handle;
	struct glink_core_rx_intent *intent_ptr;
	int ret = 0;

	if (!ctx)
		return -EINVAL;

	if (!ch_is_fully_opened(ctx)) {
		/* Can only queue rx intents if channel is fully opened */
		GLINK_ERR_CH(ctx, "%s: Channel is not fully opened\n",
			__func__);
		return -EBUSY;
	}

	intent_ptr = ch_push_local_rx_intent(ctx, pkt_priv, size);
	if (!intent_ptr) {
		GLINK_ERR_CH(ctx,
			"%s: Intent pointer allocation failed size[%zu]\n",
			__func__, size);
		return -ENOMEM;
	}
	GLINK_DBG_CH(ctx, "%s: L[%u]:%zu\n", __func__, intent_ptr->id,
			intent_ptr->intent_size);

	if (ctx->transport_ptr->capabilities & GCAP_INTENTLESS)
		return ret;

	/* notify remote side of rx intent */
	ret = ctx->transport_ptr->ops->tx_cmd_local_rx_intent(
		ctx->transport_ptr->ops, ctx->lcid, size, intent_ptr->id);
	if (ret)
		/* unable to transmit, dequeue intent */
		ch_remove_local_rx_intent(ctx, intent_ptr->id);

	return ret;
}
EXPORT_SYMBOL(glink_queue_rx_intent);

/**
 * glink_rx_intent_exists() - Check if an intent exists.
 *
 * @handle:	handle returned by glink_open()
 * @size:	size of an intent to check or 0 for any intent
 *
 * Return:	TRUE if an intent exists with greater than or equal to the size
 *		else FALSE
 */
bool glink_rx_intent_exists(void *handle, size_t size)
{
	struct channel_ctx *ctx = (struct channel_ctx *)handle;
	struct glink_core_rx_intent *intent;
	unsigned long flags;

	if (!ctx || !ch_is_fully_opened(ctx))
		return false;

	spin_lock_irqsave(&ctx->local_rx_intent_lst_lock_lhc1, flags);
	list_for_each_entry(intent, &ctx->local_rx_intent_list, list) {
		if (size <= intent->intent_size) {
			spin_unlock_irqrestore(
				&ctx->local_rx_intent_lst_lock_lhc1, flags);
			return true;
		}
	}
	spin_unlock_irqrestore(&ctx->local_rx_intent_lst_lock_lhc1, flags);

	return false;
}
EXPORT_SYMBOL(glink_rx_intent_exists);

/**
 * glink_rx_done() - Return receive buffer to remote side.
 *
 * @handle:	handle returned by glink_open()
 * @ptr:	data pointer provided in the notify_rx() call
 * @reuse:	if true, receive intent is re-used
 *
 * Return: 0 for success; standard Linux error code for failure case
 */
int glink_rx_done(void *handle, const void *ptr, bool reuse)
{
	struct channel_ctx *ctx = (struct channel_ctx *)handle;
	struct glink_core_rx_intent *liid_ptr;
	uint32_t id;
	int ret = 0;

	liid_ptr = ch_get_local_rx_intent_notified(ctx, ptr);

	if (IS_ERR_OR_NULL(liid_ptr)) {
		/* invalid pointer */
		GLINK_ERR_CH(ctx, "%s: Invalid pointer %p\n", __func__, ptr);
		return -EINVAL;
	}

	GLINK_INFO_PERF_CH(ctx, "%s: L[%u]: data[%p]. TID %u\n",
			__func__, liid_ptr->id, ptr, current->pid);
	id = liid_ptr->id;
	if (reuse) {
		ret = ctx->transport_ptr->ops->reuse_rx_intent(
					ctx->transport_ptr->ops, liid_ptr);
		if (ret) {
			GLINK_ERR_CH(ctx, "%s: Intent reuse err %d for %p\n",
					__func__, ret, ptr);
			ret = -ENOBUFS;
			reuse = false;
			ctx->transport_ptr->ops->deallocate_rx_intent(
					ctx->transport_ptr->ops, liid_ptr);
		}
	} else {
		ctx->transport_ptr->ops->deallocate_rx_intent(
					ctx->transport_ptr->ops, liid_ptr);
	}
	ch_remove_local_rx_intent_notified(ctx, liid_ptr, reuse);
	/* send rx done */
	ctx->transport_ptr->ops->tx_cmd_local_rx_done(ctx->transport_ptr->ops,
			ctx->lcid, id, reuse);

	return ret;
}
EXPORT_SYMBOL(glink_rx_done);

/**
 * glink_txv() - Transmit a packet in vector form.
 *
 * @handle:	handle returned by glink_open()
 * @pkt_priv:	opaque data value that will be returned to client with
 *		notify_tx_done notification
 * @iovec:	pointer to the vector (must remain valid until notify_tx_done
 *		notification)
 * @size:	size of data/vector
 * @vbuf_provider: Client provided helper function to iterate the vector
 *		in physical address space
 * @pbuf_provider: Client provided helper function to iterate the vector
 *		in virtual address space
 * @tx_flags:	Flags to specify transmit specific options
 *
 * Return: -EINVAL for invalid handle; -EBUSY if channel isn't ready for
 *           transmit operation (not fully opened); -EAGAIN if remote side has
 *           not provided a receive intent that is big enough.
 */
int glink_txv(void *handle, void *pkt_priv,
	void *iovec, size_t size,
	void * (*vbuf_provider)(void *iovec, size_t offset, size_t *size),
	void * (*pbuf_provider)(void *iovec, size_t offset, size_t *size),
	uint32_t tx_flags)
{
	return glink_tx_common(handle, pkt_priv, NULL, iovec, size,
			vbuf_provider, pbuf_provider, tx_flags);
}
EXPORT_SYMBOL(glink_txv);

/**
 * glink_sigs_set() - Set the local signals for the GLINK channel
 *
 * @handle:	handle returned by glink_open()
 * @sigs:	modified signal value
 *
 * Return: 0 for success; standard Linux error code for failure case
 */
int glink_sigs_set(void *handle, uint32_t sigs)
{
	struct channel_ctx *ctx = (struct channel_ctx *)handle;
	int ret;

	if (!ctx)
		return -EINVAL;

	if (!ch_is_fully_opened(ctx)) {
		GLINK_ERR_CH(ctx, "%s: Channel is not fully opened\n",
			__func__);
		return -EBUSY;
	}

	ctx->lsigs = sigs;

	ret = ctx->transport_ptr->ops->tx_cmd_set_sigs(ctx->transport_ptr->ops,
			ctx->lcid, ctx->lsigs);
	GLINK_INFO_CH(ctx, "%s: Sent SIGNAL SET command\n", __func__);

	return ret;
}
EXPORT_SYMBOL(glink_sigs_set);

/**
 * glink_sigs_local_get() - Get the local signals for the GLINK channel
 *
 * handle:	handle returned by glink_open()
 * sigs:	Pointer to hold the signals
 *
 * Return: 0 for success; standard Linux error code for failure case
 */
int glink_sigs_local_get(void *handle, uint32_t *sigs)
{
	struct channel_ctx *ctx = (struct channel_ctx *)handle;

	if (!ctx || !sigs)
		return -EINVAL;

	if (!ch_is_fully_opened(ctx)) {
		GLINK_ERR_CH(ctx, "%s: Channel is not fully opened\n",
			__func__);
		return -EBUSY;
	}

	*sigs = ctx->lsigs;
	return 0;
}
EXPORT_SYMBOL(glink_sigs_local_get);

/**
 * glink_sigs_remote_get() - Get the Remote signals for the GLINK channel
 *
 * handle:	handle returned by glink_open()
 * sigs:	Pointer to hold the signals
 *
 * Return: 0 for success; standard Linux error code for failure case
 */
int glink_sigs_remote_get(void *handle, uint32_t *sigs)
{
	struct channel_ctx *ctx = (struct channel_ctx *)handle;

	if (!ctx || !sigs)
		return -EINVAL;

	if (!ch_is_fully_opened(ctx)) {
		GLINK_ERR_CH(ctx, "%s: Channel is not fully opened\n",
			__func__);
		return -EBUSY;
	}

	*sigs = ctx->rsigs;
	return 0;
}
EXPORT_SYMBOL(glink_sigs_remote_get);

/**
 * glink_register_link_state_cb() - Register for link state notification
 * @link_info:	Data structure containing the link identification and callback.
 * @priv:	Private information to be passed with the callback.
 *
 * This function is used to register a notifier to receive the updates about a
 * link's/transport's state. This notifier needs to be registered first before
 * an attempt to open a channel.
 *
 * Return: a reference to the notifier handle.
 */
void *glink_register_link_state_cb(struct glink_link_info *link_info,
				   void *priv)
{
	struct link_state_notifier_info *notif_info;

	if (!link_info || !link_info->glink_link_state_notif_cb)
		return ERR_PTR(-EINVAL);

	notif_info = kzalloc(sizeof(*notif_info), GFP_KERNEL);
	if (!notif_info) {
		GLINK_ERR("%s: Error allocating link state notifier info\n",
			  __func__);
		return ERR_PTR(-ENOMEM);
	}
	if (link_info->transport)
		strlcpy(notif_info->transport, link_info->transport,
			GLINK_NAME_SIZE);

	if (link_info->edge)
		strlcpy(notif_info->edge, link_info->edge, GLINK_NAME_SIZE);
	notif_info->priv = priv;
	notif_info->glink_link_state_notif_cb =
				link_info->glink_link_state_notif_cb;

	mutex_lock(&link_state_notifier_lock_lha1);
	list_add_tail(&notif_info->list, &link_state_notifier_list);
	mutex_unlock(&link_state_notifier_lock_lha1);

	notif_if_up_all_xprts(notif_info);
	return notif_info;
}
EXPORT_SYMBOL(glink_register_link_state_cb);

/**
 * glink_unregister_link_state_cb() - Unregister the link state notification
 * notif_handle:	Handle to be unregistered.
 *
 * This function is used to unregister a notifier to stop receiving the updates
 * about a link's/ transport's state.
 */
void glink_unregister_link_state_cb(void *notif_handle)
{
	struct link_state_notifier_info *notif_info, *tmp_notif_info;

	if (IS_ERR_OR_NULL(notif_handle))
		return;

	mutex_lock(&link_state_notifier_lock_lha1);
	list_for_each_entry_safe(notif_info, tmp_notif_info,
				 &link_state_notifier_list, list) {
		if (notif_info == notif_handle) {
			list_del(&notif_info->list);
			mutex_unlock(&link_state_notifier_lock_lha1);
			kfree(notif_info);
			return;
		}
	}
	mutex_unlock(&link_state_notifier_lock_lha1);
	return;
}
EXPORT_SYMBOL(glink_unregister_link_state_cb);

/**
 * glink_qos_latency() - Register the latency QoS requirement
 * @handle:	Channel handle in which the latency is required.
 * @latency_us:	Latency requirement in units of micro-seconds.
 * @pkt_size:	Worst case packet size for which the latency is required.
 *
 * This function is used to register the latency requirement for a channel
 * and ensures that the latency requirement for this channel is met without
 * impacting the existing latency requirements of other channels.
 *
 * Return: 0 if QoS request is achievable, standard Linux error codes on error
 */
int glink_qos_latency(void *handle, unsigned long latency_us, size_t pkt_size)
{
	struct channel_ctx *ctx = (struct channel_ctx *)handle;
	int ret;
	unsigned long req_rate_kBps;

	if (!ctx || !latency_us || !pkt_size)
		return -EINVAL;

	if (!ch_is_fully_opened(ctx)) {
		GLINK_ERR_CH(ctx, "%s: Channel is not fully opened\n",
			__func__);
		return -EBUSY;
	}

	req_rate_kBps = glink_qos_calc_rate_kBps(pkt_size, latency_us);

	ret = glink_qos_assign_priority(ctx, req_rate_kBps);
	if (ret < 0)
		GLINK_ERR_CH(ctx, "%s: QoS %lu:%zu cannot be met\n",
			     __func__, latency_us, pkt_size);

	return ret;
}
EXPORT_SYMBOL(glink_qos_latency);

/**
 * glink_qos_cancel() - Cancel or unregister the QoS request
 * @handle:	Channel handle for which the QoS request is cancelled.
 *
 * This function is used to cancel/unregister the QoS requests for a channel.
 *
 * Return: 0 on success, standard Linux error codes on failure
 */
int glink_qos_cancel(void *handle)
{
	struct channel_ctx *ctx = (struct channel_ctx *)handle;
	int ret;

	if (!ctx)
		return -EINVAL;

	if (!ch_is_fully_opened(ctx)) {
		GLINK_ERR_CH(ctx, "%s: Channel is not fully opened\n",
			__func__);
		return -EBUSY;
	}

	ret = glink_qos_reset_priority(ctx);
	return ret;
}
EXPORT_SYMBOL(glink_qos_cancel);

/**
 * glink_qos_start() - Start of the transmission requiring QoS
 * @handle:	Channel handle in which the transmit activity is performed.
 *
 * This function is called by the clients to indicate G-Link regarding the
 * start of the transmission which requires a certain QoS. The clients
 * must account for the QoS ramp time to ensure meeting the QoS.
 *
 * Return: 0 on success, standard Linux error codes on failure
 */
int glink_qos_start(void *handle)
{
	struct channel_ctx *ctx = (struct channel_ctx *)handle;
	int ret;
	unsigned long flags;

	if (!ctx)
		return -EINVAL;

	if (!ch_is_fully_opened(ctx)) {
		GLINK_ERR_CH(ctx, "%s: Channel is not fully opened\n",
			__func__);
		return -EBUSY;
	}

	spin_lock_irqsave(&ctx->transport_ptr->tx_ready_lock_lhb2, flags);
	spin_lock(&ctx->tx_lists_lock_lhc3);
	ret = glink_qos_add_ch_tx_intent(ctx);
	spin_unlock(&ctx->tx_lists_lock_lhc3);
	spin_unlock_irqrestore(&ctx->transport_ptr->tx_ready_lock_lhb2, flags);
	return ret;
}
EXPORT_SYMBOL(glink_qos_start);

/**
 * glink_qos_get_ramp_time() - Get the QoS ramp time
 * @handle:	Channel handle for which the QoS ramp time is required.
 * @pkt_size:	Worst case packet size.
 *
 * This function is called by the clients to obtain the ramp time required
 * to meet the QoS requirements.
 *
 * Return: QoS ramp time is returned in units of micro-seconds on success,
 *	   standard Linux error codes cast to unsigned long on error.
 */
unsigned long glink_qos_get_ramp_time(void *handle, size_t pkt_size)
{
	struct channel_ctx *ctx = (struct channel_ctx *)handle;

	if (!ctx)
		return (unsigned long)-EINVAL;

	if (!ch_is_fully_opened(ctx)) {
		GLINK_ERR_CH(ctx, "%s: Channel is not fully opened\n",
			__func__);
		return (unsigned long)-EBUSY;
	}

	return ctx->transport_ptr->ops->get_power_vote_ramp_time(
			ctx->transport_ptr->ops,
			glink_prio_to_power_state(ctx->transport_ptr,
						ctx->initial_priority));
}
EXPORT_SYMBOL(glink_qos_get_ramp_time);

/**
 * glink_rpm_rx_poll() - Poll and receive any available events
 * @handle:	Channel handle in which this operation is performed.
 *
 * This function is used to poll and receive events and packets while the
 * receive interrupt from RPM is disabled.
 *
 * Note that even if a return value > 0 is returned indicating that some events
 * were processed, clients should only use the notification functions passed
 * into glink_open() to determine if an entire packet has been received since
 * some events may be internal details that are not visible to clients.
 *
 * Return: 0 for no packets available; > 0 for events available; standard
 * Linux error codes on failure.
 */
int glink_rpm_rx_poll(void *handle)
{
	struct channel_ctx *ctx = (struct channel_ctx *)handle;

	if (!ctx)
		return -EINVAL;

	if (!ch_is_fully_opened(ctx))
		return -EBUSY;

	if (!ctx->transport_ptr ||
	    !(ctx->transport_ptr->capabilities & GCAP_INTENTLESS))
		return -EOPNOTSUPP;

	return ctx->transport_ptr->ops->poll(ctx->transport_ptr->ops,
					     ctx->lcid);
}
EXPORT_SYMBOL(glink_rpm_rx_poll);

/**
 * glink_rpm_mask_rx_interrupt() - Mask or unmask the RPM receive interrupt
 * @handle:	Channel handle in which this operation is performed.
 * @mask:	Flag to mask or unmask the interrupt.
 * @pstruct:	Pointer to any platform specific data.
 *
 * This function is used to mask or unmask the receive interrupt from RPM.
 * "mask" set to true indicates masking the interrupt and when set to false
 * indicates unmasking the interrupt.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
int glink_rpm_mask_rx_interrupt(void *handle, bool mask, void *pstruct)
{
	struct channel_ctx *ctx = (struct channel_ctx *)handle;

	if (!ctx)
		return -EINVAL;

	if (!ch_is_fully_opened(ctx))
		return -EBUSY;

	if (!ctx->transport_ptr ||
	    !(ctx->transport_ptr->capabilities & GCAP_INTENTLESS))
		return -EOPNOTSUPP;

	return ctx->transport_ptr->ops->mask_rx_irq(ctx->transport_ptr->ops,
						    ctx->lcid, mask, pstruct);

}
EXPORT_SYMBOL(glink_rpm_mask_rx_interrupt);

/**
 * glink_wait_link_down() - Get status of link
 * @handle:	Channel handle in which this operation is performed
 *
 * This function will query the transport for its status, to allow clients to
 * proceed in cleanup operations.
 */
int glink_wait_link_down(void *handle)
{
	struct channel_ctx *ctx = (struct channel_ctx *)handle;

	if (!ctx)
		return -EINVAL;
	if (!ctx->transport_ptr)
		return -EOPNOTSUPP;

	return ctx->transport_ptr->ops->wait_link_down(ctx->transport_ptr->ops);
}
EXPORT_SYMBOL(glink_wait_link_down);

/**
 * glink_xprt_ctx_release - Free the transport context
 * @ch_st_lock:	handle to the rwref_lock associated with the transport
 *
 * This should only be called when the reference count associated with the
 * transport goes to zero.
 */
void glink_xprt_ctx_release(struct rwref_lock *xprt_st_lock)
{
	struct glink_dbgfs xprt_rm_dbgfs;
	struct glink_core_xprt_ctx *xprt_ctx = container_of(xprt_st_lock,
				struct glink_core_xprt_ctx, xprt_state_lhb0);
	GLINK_INFO("%s: freeing transport [%s->%s]context\n", __func__,
				xprt_ctx->name,
				xprt_ctx->edge);
	xprt_rm_dbgfs.curr_name = xprt_ctx->name;
	xprt_rm_dbgfs.par_name = "xprt";
	glink_debugfs_remove_recur(&xprt_rm_dbgfs);
	GLINK_INFO("%s: xprt debugfs removec\n", __func__);
	destroy_workqueue(xprt_ctx->tx_wq);
	glink_core_deinit_xprt_qos_cfg(xprt_ctx);
	kfree(xprt_ctx);
	xprt_ctx = NULL;
}

/**
 * glink_dummy_xprt_ctx_release - free the dummy transport context
 * @xprt_st_lock:	Handle to the rwref_lock associated with the transport.
 *
 * The release function is called when all the channels on this dummy
 * transport are closed and the reference count goes to zero.
 */
static void glink_dummy_xprt_ctx_release(struct rwref_lock *xprt_st_lock)
{
	struct glink_core_xprt_ctx *xprt_ctx = container_of(xprt_st_lock,
				struct glink_core_xprt_ctx, xprt_state_lhb0);
	GLINK_INFO("%s: freeing transport [%s->%s]context\n", __func__,
				xprt_ctx->name,
				xprt_ctx->edge);
	kfree(xprt_ctx);
}

/**
 * glink_xprt_name_to_id() - convert transport name to id
 * @name:	Name of the transport.
 * @id:		Assigned id.
 *
 * Return: 0 on success or standard Linux error code.
 */
int glink_xprt_name_to_id(const char *name, uint16_t *id)
{
	if (!strcmp(name, "smem")) {
		*id = SMEM_XPRT_ID;
		return 0;
	}
	if (!strcmp(name, "mailbox")) {
		*id = SMEM_XPRT_ID;
		return 0;
	}
	if (!strcmp(name, "smd_trans")) {
		*id = SMD_TRANS_XPRT_ID;
		return 0;
	}
	if (!strcmp(name, "lloop")) {
		*id = LLOOP_XPRT_ID;
		return 0;
	}
	if (!strcmp(name, "mock")) {
		*id = MOCK_XPRT_ID;
		return 0;
	}
	if (!strcmp(name, "mock_low")) {
		*id = MOCK_XPRT_LOW_ID;
		return 0;
	}
	if (!strcmp(name, "mock_high")) {
		*id = MOCK_XPRT_HIGH_ID;
		return 0;
	}
	return -ENODEV;
}
EXPORT_SYMBOL(glink_xprt_name_to_id);

/**
 * of_get_glink_core_qos_cfg() - Parse the qos related dt entries
 * @phandle:	The handle to the qos related node in DT.
 * @cfg:	The transport configuration to be filled.
 *
 * Return: 0 on Success, standard Linux error otherwise.
 */
int of_get_glink_core_qos_cfg(struct device_node *phandle,
				struct glink_core_transport_cfg *cfg)
{
	int rc, i;
	char *key;
	uint32_t num_flows;
	uint32_t *arr32;

	if (!phandle) {
		GLINK_ERR("%s: phandle is NULL\n", __func__);
		return -EINVAL;
	}

	key = "qcom,mtu-size";
	rc = of_property_read_u32(phandle, key, (uint32_t *)&cfg->mtu);
	if (rc) {
		GLINK_ERR("%s: missing key %s\n", __func__, key);
		return -ENODEV;
	}

	key = "qcom,tput-stats-cycle";
	rc = of_property_read_u32(phandle, key, &cfg->token_count);
	if (rc) {
		GLINK_ERR("%s: missing key %s\n", __func__, key);
		rc = -ENODEV;
		goto error;
	}

	key = "qcom,flow-info";
	if (!of_find_property(phandle, key, &num_flows)) {
		GLINK_ERR("%s: missing key %s\n", __func__, key);
		rc = -ENODEV;
		goto error;
	}

	num_flows /= sizeof(uint32_t);
	if (num_flows % 2) {
		GLINK_ERR("%s: Invalid flow info length\n", __func__);
		rc = -EINVAL;
		goto error;
	}

	num_flows /= 2;
	cfg->num_flows = num_flows;

	cfg->flow_info = kmalloc_array(num_flows, sizeof(*(cfg->flow_info)),
					GFP_KERNEL);
	if (!cfg->flow_info) {
		GLINK_ERR("%s: Memory allocation for flow info failed\n",
				__func__);
		rc = -ENOMEM;
		goto error;
	}
	arr32 = kmalloc_array(num_flows * 2, sizeof(uint32_t), GFP_KERNEL);
	if (!arr32) {
		GLINK_ERR("%s: Memory allocation for temporary array failed\n",
				__func__);
		rc = -ENOMEM;
		goto temp_mem_alloc_fail;
	}

	of_property_read_u32_array(phandle, key, arr32, num_flows * 2);

	for (i = 0; i < num_flows; i++) {
		cfg->flow_info[i].mtu_tx_time_us = arr32[2 * i];
		cfg->flow_info[i].power_state = arr32[2 * i + 1];
	}

	kfree(arr32);
	of_node_put(phandle);
	return 0;

temp_mem_alloc_fail:
	kfree(cfg->flow_info);
error:
	cfg->mtu = 0;
	cfg->token_count = 0;
	cfg->num_flows = 0;
	cfg->flow_info = NULL;
	return rc;
}
EXPORT_SYMBOL(of_get_glink_core_qos_cfg);

/**
 * glink_core_init_xprt_qos_cfg() - Initialize a transport's QoS configuration
 * @xprt_ptr:	Transport to be initialized with QoS configuration.
 * @cfg:	Data structure containing QoS configuration.
 *
 * This function is used during the transport registration to initialize it
 * with QoS configuration.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
static int glink_core_init_xprt_qos_cfg(struct glink_core_xprt_ctx *xprt_ptr,
					 struct glink_core_transport_cfg *cfg)
{
	int i;

	xprt_ptr->mtu = cfg->mtu ? cfg->mtu : GLINK_QOS_DEF_MTU;
	xprt_ptr->num_priority = cfg->num_flows ? cfg->num_flows :
					GLINK_QOS_DEF_NUM_PRIORITY;
	xprt_ptr->token_count = cfg->token_count ? cfg->token_count :
					GLINK_QOS_DEF_NUM_TOKENS;

	xprt_ptr->prio_bin = kzalloc(xprt_ptr->num_priority *
				sizeof(struct glink_qos_priority_bin),
				GFP_KERNEL);
	if (!xprt_ptr->prio_bin) {
		GLINK_ERR("%s: unable to allocate priority bins\n", __func__);
		return -ENOMEM;
	}
	for (i = 1; i < xprt_ptr->num_priority; i++) {
		xprt_ptr->prio_bin[i].max_rate_kBps =
			glink_qos_calc_rate_kBps(xprt_ptr->mtu,
				cfg->flow_info[i].mtu_tx_time_us);
		xprt_ptr->prio_bin[i].power_state =
				cfg->flow_info[i].power_state;
		INIT_LIST_HEAD(&xprt_ptr->prio_bin[i].tx_ready);
	}
	xprt_ptr->prio_bin[0].max_rate_kBps = 0;
	if (cfg->flow_info)
		xprt_ptr->prio_bin[0].power_state =
						cfg->flow_info[0].power_state;
	INIT_LIST_HEAD(&xprt_ptr->prio_bin[0].tx_ready);
	xprt_ptr->threshold_rate_kBps =
		xprt_ptr->prio_bin[xprt_ptr->num_priority - 1].max_rate_kBps;

	return 0;
}

/**
 * glink_core_deinit_xprt_qos_cfg() - Reset a transport's QoS configuration
 * @xprt_ptr: Transport to be deinitialized.
 *
 * This function is used during the time of transport unregistration to
 * de-initialize the QoS configuration from a transport.
 */
static void glink_core_deinit_xprt_qos_cfg(struct glink_core_xprt_ctx *xprt_ptr)
{
	kfree(xprt_ptr->prio_bin);
	xprt_ptr->prio_bin = NULL;
	xprt_ptr->mtu = 0;
	xprt_ptr->num_priority = 0;
	xprt_ptr->token_count = 0;
	xprt_ptr->threshold_rate_kBps = 0;
}

/**
 * glink_core_register_transport() - register a new transport
 * @if_ptr:	The interface to the transport.
 * @cfg:	Description and configuration of the transport.
 *
 * Return: 0 on success, EINVAL for invalid input.
 */
int glink_core_register_transport(struct glink_transport_if *if_ptr,
				  struct glink_core_transport_cfg *cfg)
{
	struct glink_core_xprt_ctx *xprt_ptr;
	size_t len;
	uint16_t id;
	int ret;
	char log_name[GLINK_NAME_SIZE*2+2] = {0};

	if (!if_ptr || !cfg || !cfg->name || !cfg->edge)
		return -EINVAL;

	len = strlen(cfg->name);
	if (len == 0 || len >= GLINK_NAME_SIZE)
		return -EINVAL;

	len = strlen(cfg->edge);
	if (len == 0 || len >= GLINK_NAME_SIZE)
		return -EINVAL;

	if (cfg->versions_entries < 1)
		return -EINVAL;

	ret = glink_xprt_name_to_id(cfg->name, &id);
	if (ret)
		return ret;

	xprt_ptr = kzalloc(sizeof(struct glink_core_xprt_ctx), GFP_KERNEL);
	if (xprt_ptr == NULL)
		return -ENOMEM;

	xprt_ptr->id = id;
	rwref_lock_init(&xprt_ptr->xprt_state_lhb0,
			glink_xprt_ctx_release);
	strlcpy(xprt_ptr->name, cfg->name, GLINK_NAME_SIZE);
	strlcpy(xprt_ptr->edge, cfg->edge, GLINK_NAME_SIZE);
	xprt_ptr->versions = cfg->versions;
	xprt_ptr->versions_entries = cfg->versions_entries;
	xprt_ptr->local_version_idx = cfg->versions_entries - 1;
	xprt_ptr->remote_version_idx = cfg->versions_entries - 1;
	xprt_ptr->l_features =
			cfg->versions[cfg->versions_entries - 1].features;
	if (!if_ptr->poll)
		if_ptr->poll = dummy_poll;
	if (!if_ptr->mask_rx_irq)
		if_ptr->mask_rx_irq = dummy_mask_rx_irq;
	if (!if_ptr->reuse_rx_intent)
		if_ptr->reuse_rx_intent = dummy_reuse_rx_intent;
	if (!if_ptr->wait_link_down)
		if_ptr->wait_link_down = dummy_wait_link_down;
	if (!if_ptr->tx_cmd_tracer_pkt)
		if_ptr->tx_cmd_tracer_pkt = dummy_tx_cmd_tracer_pkt;
	if (!if_ptr->get_power_vote_ramp_time)
		if_ptr->get_power_vote_ramp_time =
					dummy_get_power_vote_ramp_time;
	if (!if_ptr->power_vote)
		if_ptr->power_vote = dummy_power_vote;
	if (!if_ptr->power_unvote)
		if_ptr->power_unvote = dummy_power_unvote;
	xprt_ptr->capabilities = 0;
	xprt_ptr->ops = if_ptr;
	spin_lock_init(&xprt_ptr->xprt_ctx_lock_lhb1);
	xprt_ptr->next_lcid = 1; /* 0 reserved for default unconfigured */
	INIT_LIST_HEAD(&xprt_ptr->free_lcid_list);
	xprt_ptr->max_cid = cfg->max_cid;
	xprt_ptr->max_iid = cfg->max_iid;
	xprt_ptr->local_state = GLINK_XPRT_DOWN;
	xprt_ptr->remote_neg_completed = false;
	INIT_LIST_HEAD(&xprt_ptr->channels);
	ret = glink_core_init_xprt_qos_cfg(xprt_ptr, cfg);
	if (ret < 0) {
		kfree(xprt_ptr);
		return ret;
	}
	spin_lock_init(&xprt_ptr->tx_ready_lock_lhb2);
	mutex_init(&xprt_ptr->xprt_dbgfs_lock_lhb3);
	INIT_WORK(&xprt_ptr->tx_work, tx_work_func);
	xprt_ptr->tx_wq = create_singlethread_workqueue("glink_tx");
	if (IS_ERR_OR_NULL(xprt_ptr->tx_wq)) {
		GLINK_ERR("%s: unable to allocate workqueue\n", __func__);
		glink_core_deinit_xprt_qos_cfg(xprt_ptr);
		kfree(xprt_ptr);
		return -ENOMEM;
	}
	INIT_DELAYED_WORK(&xprt_ptr->pm_qos_work, glink_pm_qos_cancel_worker);
	pm_qos_add_request(&xprt_ptr->pm_qos_req, PM_QOS_CPU_DMA_LATENCY,
			PM_QOS_DEFAULT_VALUE);

	if_ptr->glink_core_priv = xprt_ptr;
	if_ptr->glink_core_if_ptr = &core_impl;

	mutex_lock(&transport_list_lock_lha0);
	list_add_tail(&xprt_ptr->list_node, &transport_list);
	mutex_unlock(&transport_list_lock_lha0);
	glink_debugfs_add_xprt(xprt_ptr);
	snprintf(log_name, sizeof(log_name), "%s_%s",
			xprt_ptr->edge, xprt_ptr->name);
	xprt_ptr->log_ctx = ipc_log_context_create(NUM_LOG_PAGES, log_name, 0);
	if (!xprt_ptr->log_ctx)
		GLINK_ERR("%s: unable to create log context for [%s:%s]\n",
				__func__, xprt_ptr->edge, xprt_ptr->name);

	return 0;
}
EXPORT_SYMBOL(glink_core_register_transport);

/**
 * glink_core_unregister_transport() - unregister a transport
 *
 * @if_ptr:	The interface to the transport.
 */
void glink_core_unregister_transport(struct glink_transport_if *if_ptr)
{
	struct glink_core_xprt_ctx *xprt_ptr = if_ptr->glink_core_priv;

	GLINK_DBG_XPRT(xprt_ptr, "%s: destroying transport\n", __func__);
	if (xprt_ptr->local_state != GLINK_XPRT_DOWN) {
		GLINK_ERR_XPRT(xprt_ptr,
		"%s: link_down should have been called before this\n",
		__func__);
		return;
	}

	mutex_lock(&transport_list_lock_lha0);
	list_del(&xprt_ptr->list_node);
	mutex_unlock(&transport_list_lock_lha0);
	flush_delayed_work(&xprt_ptr->pm_qos_work);
	pm_qos_remove_request(&xprt_ptr->pm_qos_req);
	ipc_log_context_destroy(xprt_ptr->log_ctx);
	xprt_ptr->log_ctx = NULL;
	rwref_put(&xprt_ptr->xprt_state_lhb0);
}
EXPORT_SYMBOL(glink_core_unregister_transport);

/**
 * glink_core_link_up() - transport link-up notification
 *
 * @if_ptr:	pointer to transport interface
 */
static void glink_core_link_up(struct glink_transport_if *if_ptr)
{
	struct glink_core_xprt_ctx *xprt_ptr = if_ptr->glink_core_priv;

	/* start local negotiation */
	xprt_ptr->local_state = GLINK_XPRT_NEGOTIATING;
	xprt_ptr->local_version_idx = xprt_ptr->versions_entries - 1;
	xprt_ptr->l_features =
		xprt_ptr->versions[xprt_ptr->local_version_idx].features;
	if_ptr->tx_cmd_version(if_ptr,
		    xprt_ptr->versions[xprt_ptr->local_version_idx].version,
		    xprt_ptr->versions[xprt_ptr->local_version_idx].features);

}

/**
 * glink_core_link_down() - transport link-down notification
 *
 * @if_ptr:	pointer to transport interface
 */
static void glink_core_link_down(struct glink_transport_if *if_ptr)
{
	struct glink_core_xprt_ctx *xprt_ptr = if_ptr->glink_core_priv;

	rwref_write_get(&xprt_ptr->xprt_state_lhb0);
	xprt_ptr->next_lcid = 1;
	xprt_ptr->local_state = GLINK_XPRT_DOWN;
	xprt_ptr->local_version_idx = xprt_ptr->versions_entries - 1;
	xprt_ptr->remote_version_idx = xprt_ptr->versions_entries - 1;
	xprt_ptr->l_features =
		xprt_ptr->versions[xprt_ptr->local_version_idx].features;
	xprt_ptr->remote_neg_completed = false;
	rwref_write_put(&xprt_ptr->xprt_state_lhb0);
	GLINK_DBG_XPRT(xprt_ptr,
		"%s: Flushing work from tx_wq. Thread: %u\n", __func__,
		current->pid);
	flush_workqueue(xprt_ptr->tx_wq);
	glink_core_channel_cleanup(xprt_ptr);
	check_link_notifier_and_notify(xprt_ptr, GLINK_LINK_STATE_DOWN);
}

/**
 * glink_create_dummy_xprt_ctx() - create a dummy transport that replaces all
 *				the transport interface functions with a dummy
 * @orig_xprt_ctx:	Pointer to the original transport context.
 *
 * The dummy transport is used only when it is swapped with the actual transport
 * pointer in ssr/unregister case.
 *
 * Return:	Pointer to dummy transport context.
 */
static struct glink_core_xprt_ctx *glink_create_dummy_xprt_ctx(
				struct glink_core_xprt_ctx *orig_xprt_ctx)
{

	struct glink_core_xprt_ctx *xprt_ptr;
	struct glink_transport_if *if_ptr;

	xprt_ptr = kzalloc(sizeof(*xprt_ptr), GFP_KERNEL);
	if (!xprt_ptr)
		return ERR_PTR(-ENOMEM);
	if_ptr = kmalloc(sizeof(*if_ptr), GFP_KERNEL);
	if (!if_ptr) {
		kfree(xprt_ptr);
		return ERR_PTR(-ENOMEM);
	}
	rwref_lock_init(&xprt_ptr->xprt_state_lhb0,
			glink_dummy_xprt_ctx_release);

	strlcpy(xprt_ptr->name, "dummy", GLINK_NAME_SIZE);
	strlcpy(xprt_ptr->edge, orig_xprt_ctx->edge, GLINK_NAME_SIZE);
	if_ptr->poll = dummy_poll;
	if_ptr->mask_rx_irq = dummy_mask_rx_irq;
	if_ptr->reuse_rx_intent = dummy_reuse_rx_intent;
	if_ptr->wait_link_down = dummy_wait_link_down;
	if_ptr->allocate_rx_intent = dummy_allocate_rx_intent;
	if_ptr->deallocate_rx_intent = dummy_deallocate_rx_intent;
	if_ptr->tx_cmd_local_rx_intent = dummy_tx_cmd_local_rx_intent;
	if_ptr->tx_cmd_local_rx_done = dummy_tx_cmd_local_rx_done;
	if_ptr->tx = dummy_tx;
	if_ptr->tx_cmd_rx_intent_req = dummy_tx_cmd_rx_intent_req;
	if_ptr->tx_cmd_remote_rx_intent_req_ack =
				dummy_tx_cmd_remote_rx_intent_req_ack;
	if_ptr->tx_cmd_set_sigs = dummy_tx_cmd_set_sigs;
	if_ptr->tx_cmd_ch_close = dummy_tx_cmd_ch_close;
	if_ptr->tx_cmd_ch_remote_close_ack = dummy_tx_cmd_ch_remote_close_ack;

	xprt_ptr->ops = if_ptr;
	xprt_ptr->log_ctx = log_ctx;
	spin_lock_init(&xprt_ptr->xprt_ctx_lock_lhb1);
	INIT_LIST_HEAD(&xprt_ptr->free_lcid_list);
	xprt_ptr->local_state = GLINK_XPRT_DOWN;
	xprt_ptr->remote_neg_completed = false;
	INIT_LIST_HEAD(&xprt_ptr->channels);
	spin_lock_init(&xprt_ptr->tx_ready_lock_lhb2);
	mutex_init(&xprt_ptr->xprt_dbgfs_lock_lhb3);
	return xprt_ptr;
}

/**
 * glink_core_channel_cleanup() - cleanup all channels for the transport
 *
 * @xprt_ptr:	pointer to transport context
 *
 * This function should be called either from link_down or ssr
 */
static void glink_core_channel_cleanup(struct glink_core_xprt_ctx *xprt_ptr)
{
	unsigned long flags, d_flags;
	struct channel_ctx *ctx, *tmp_ctx;
	struct channel_lcid *temp_lcid, *temp_lcid1;
	struct glink_core_xprt_ctx *dummy_xprt_ctx;

	dummy_xprt_ctx = glink_create_dummy_xprt_ctx(xprt_ptr);
	if (IS_ERR_OR_NULL(dummy_xprt_ctx)) {
		GLINK_ERR("%s: Dummy Transport creation failed\n", __func__);
		return;
	}

	rwref_get(&dummy_xprt_ctx->xprt_state_lhb0);
	spin_lock_irqsave(&xprt_ptr->xprt_ctx_lock_lhb1, flags);
	list_for_each_entry_safe(ctx, tmp_ctx, &xprt_ptr->channels,
						port_list_node) {
		rwref_get(&ctx->ch_state_lhc0);
		if (ctx->local_open_state == GLINK_CHANNEL_OPENED ||
			ctx->local_open_state == GLINK_CHANNEL_OPENING) {
			rwref_get(&dummy_xprt_ctx->xprt_state_lhb0);
			spin_lock_irqsave(&dummy_xprt_ctx->xprt_ctx_lock_lhb1,
						d_flags);
			list_move_tail(&ctx->port_list_node,
					&dummy_xprt_ctx->channels);
			spin_unlock_irqrestore(
				&dummy_xprt_ctx->xprt_ctx_lock_lhb1, d_flags);
			ctx->transport_ptr = dummy_xprt_ctx;
		} else {
			/* local state is in either CLOSED or CLOSING */
			spin_unlock_irqrestore(&xprt_ptr->xprt_ctx_lock_lhb1,
							flags);
			glink_core_remote_close_common(ctx);
			if (ctx->local_open_state == GLINK_CHANNEL_CLOSING)
				glink_core_ch_close_ack_common(ctx);
			/* Channel should be fully closed now. Delete here */
			if (ch_is_fully_closed(ctx))
				glink_delete_ch_from_list(ctx, false);
			spin_lock_irqsave(&xprt_ptr->xprt_ctx_lock_lhb1, flags);
		}
		rwref_put(&ctx->ch_state_lhc0);
	}
	list_for_each_entry_safe(temp_lcid, temp_lcid1,
			&xprt_ptr->free_lcid_list, list_node) {
		list_del(&temp_lcid->list_node);
		kfree(&temp_lcid->list_node);
	}
	spin_unlock_irqrestore(&xprt_ptr->xprt_ctx_lock_lhb1, flags);

	spin_lock_irqsave(&dummy_xprt_ctx->xprt_ctx_lock_lhb1, d_flags);
	list_for_each_entry_safe(ctx, tmp_ctx, &dummy_xprt_ctx->channels,
						port_list_node) {
		rwref_get(&ctx->ch_state_lhc0);
		spin_unlock_irqrestore(&dummy_xprt_ctx->xprt_ctx_lock_lhb1,
				d_flags);
		glink_core_remote_close_common(ctx);
		spin_lock_irqsave(&dummy_xprt_ctx->xprt_ctx_lock_lhb1,
				d_flags);
		rwref_put(&ctx->ch_state_lhc0);
	}
	spin_unlock_irqrestore(&dummy_xprt_ctx->xprt_ctx_lock_lhb1, d_flags);
	rwref_put(&dummy_xprt_ctx->xprt_state_lhb0);
}
/**
 * glink_core_rx_cmd_version() - receive version/features from remote system
 *
 * @if_ptr:	pointer to transport interface
 * @r_version:	remote version
 * @r_features:	remote features
 *
 * This function is called in response to a remote-initiated version/feature
 * negotiation sequence.
 */
static void glink_core_rx_cmd_version(struct glink_transport_if *if_ptr,
	uint32_t r_version, uint32_t r_features)
{
	struct glink_core_xprt_ctx *xprt_ptr = if_ptr->glink_core_priv;
	const struct glink_core_version *versions = xprt_ptr->versions;
	bool neg_complete = false;
	uint32_t l_version;

	if (xprt_is_fully_opened(xprt_ptr)) {
		GLINK_ERR_XPRT(xprt_ptr,
			"%s: Negotiation already complete\n", __func__);
		return;
	}

	l_version = versions[xprt_ptr->remote_version_idx].version;

	GLINK_INFO_XPRT(xprt_ptr,
		"%s: [local]%x:%08x [remote]%x:%08x\n", __func__,
		l_version, xprt_ptr->l_features, r_version, r_features);

	if (l_version > r_version) {
		/* Find matching version */
		while (true) {
			uint32_t rver_idx;

			if (xprt_ptr->remote_version_idx == 0) {
				/* version negotiation failed */
				GLINK_ERR_XPRT(xprt_ptr,
					"%s: Transport negotiation failed\n",
					__func__);
				l_version = 0;
				xprt_ptr->l_features = 0;
				break;
			}
			--xprt_ptr->remote_version_idx;
			rver_idx = xprt_ptr->remote_version_idx;

			if (versions[rver_idx].version <= r_version) {
				/* found a potential match */
				l_version = versions[rver_idx].version;
				xprt_ptr->l_features =
					versions[rver_idx].features;
				break;
			}
		}
	}

	if (l_version == r_version) {
		GLINK_INFO_XPRT(xprt_ptr,
			"%s: Remote and local version are matched %x:%08x\n",
			__func__, r_version, r_features);
		if (xprt_ptr->l_features != r_features) {
			uint32_t rver_idx = xprt_ptr->remote_version_idx;

			xprt_ptr->l_features = versions[rver_idx]
						.negotiate_features(if_ptr,
					&xprt_ptr->versions[rver_idx],
					r_features);
			GLINK_INFO_XPRT(xprt_ptr,
				"%s: negotiate features %x:%08x\n",
				__func__, l_version, xprt_ptr->l_features);
		}
		neg_complete = true;
	}
	if_ptr->tx_cmd_version_ack(if_ptr, l_version, xprt_ptr->l_features);

	if (neg_complete) {
		GLINK_INFO_XPRT(xprt_ptr,
			"%s: Remote negotiation complete %x:%08x\n", __func__,
			l_version, xprt_ptr->l_features);

		if (xprt_ptr->local_state == GLINK_XPRT_OPENED) {
			xprt_ptr->capabilities = if_ptr->set_version(if_ptr,
							l_version,
							xprt_ptr->l_features);
		}
		if_ptr->glink_core_priv->remote_neg_completed = true;
		if (xprt_is_fully_opened(xprt_ptr))
			check_link_notifier_and_notify(xprt_ptr,
						       GLINK_LINK_STATE_UP);
	}
}

/**
 * glink_core_rx_cmd_version_ack() - receive negotiation ack from remote system
 *
 * @if_ptr:	pointer to transport interface
 * @r_version:	remote version response
 * @r_features:	remote features response
 *
 * This function is called in response to a local-initiated version/feature
 * negotiation sequence and is the counter-offer from the remote side based
 * upon the initial version and feature set requested.
 */
static void glink_core_rx_cmd_version_ack(struct glink_transport_if *if_ptr,
	uint32_t r_version, uint32_t r_features)
{
	struct glink_core_xprt_ctx *xprt_ptr = if_ptr->glink_core_priv;
	const struct glink_core_version *versions = xprt_ptr->versions;
	uint32_t l_version;
	bool neg_complete = false;

	if (xprt_is_fully_opened(xprt_ptr)) {
		GLINK_ERR_XPRT(xprt_ptr,
			"%s: Negotiation already complete\n", __func__);
		return;
	}

	l_version = versions[xprt_ptr->local_version_idx].version;

	GLINK_INFO_XPRT(xprt_ptr,
		"%s: [local]%x:%08x [remote]%x:%08x\n", __func__,
		 l_version, xprt_ptr->l_features, r_version, r_features);

	if (l_version > r_version) {
		/* find matching version */
		while (true) {
			uint32_t lver_idx = xprt_ptr->local_version_idx;

			if (xprt_ptr->local_version_idx == 0) {
				/* version negotiation failed */
				xprt_ptr->local_state = GLINK_XPRT_FAILED;
				GLINK_ERR_XPRT(xprt_ptr,
					"%s: Transport negotiation failed\n",
					__func__);
				l_version = 0;
				xprt_ptr->l_features = 0;
				break;
			}
			--xprt_ptr->local_version_idx;
			lver_idx = xprt_ptr->local_version_idx;

			if (versions[lver_idx].version <= r_version) {
				/* found a potential match */
				l_version = versions[lver_idx].version;
				xprt_ptr->l_features =
					versions[lver_idx].features;
				break;
			}
		}
	} else if (l_version == r_version) {
		if (xprt_ptr->l_features != r_features) {
			/* version matches, negotiate features */
			uint32_t lver_idx = xprt_ptr->local_version_idx;

			xprt_ptr->l_features = versions[lver_idx]
						.negotiate_features(if_ptr,
							&versions[lver_idx],
							r_features);
			GLINK_INFO_XPRT(xprt_ptr,
				"%s: negotiation features %x:%08x\n",
				__func__, l_version, xprt_ptr->l_features);
		} else {
			neg_complete = true;
		}
	} else {
		/*
		 * r_version > l_version
		 *
		 * Remote responded with a version greater than what we
		 * requested which is invalid and is treated as failure of the
		 * negotiation algorithm.
		 */
		GLINK_ERR_XPRT(xprt_ptr,
			"%s: [local]%x:%08x [remote]%x:%08x neg failure\n",
			__func__, l_version, xprt_ptr->l_features, r_version,
			r_features);
		xprt_ptr->local_state = GLINK_XPRT_FAILED;
		l_version = 0;
		xprt_ptr->l_features = 0;
	}

	if (neg_complete) {
		/* negotiation complete */
		GLINK_INFO_XPRT(xprt_ptr,
			"%s: Local negotiation complete %x:%08x\n",
			__func__, l_version, xprt_ptr->l_features);

		if (xprt_ptr->remote_neg_completed) {
			xprt_ptr->capabilities = if_ptr->set_version(if_ptr,
							l_version,
							xprt_ptr->l_features);
		}

		xprt_ptr->local_state = GLINK_XPRT_OPENED;
		if (xprt_is_fully_opened(xprt_ptr))
			check_link_notifier_and_notify(xprt_ptr,
						       GLINK_LINK_STATE_UP);
	} else {
		if_ptr->tx_cmd_version(if_ptr, l_version, xprt_ptr->l_features);
	}
}

/**
 * find_l_ctx_get() - find a local channel context based on a remote one
 * @r_ctx:	The remote channel to use as a lookup key.
 *
 * If the channel is found, the reference count is incremented to ensure the
 * lifetime of the channel context.  The caller must call rwref_put() when done.
 *
 * Return: The corresponding local ctx or NULL is not found.
 */
static struct channel_ctx *find_l_ctx_get(struct channel_ctx *r_ctx)
{
	struct glink_core_xprt_ctx *xprt;
	struct channel_ctx *ctx;
	unsigned long flags;
	struct channel_ctx *l_ctx = NULL;

	mutex_lock(&transport_list_lock_lha0);
	list_for_each_entry(xprt, &transport_list, list_node)
		if (!strcmp(r_ctx->transport_ptr->edge, xprt->edge)) {
			rwref_write_get(&xprt->xprt_state_lhb0);
			if (xprt->local_state != GLINK_XPRT_OPENED) {
				rwref_write_put(&xprt->xprt_state_lhb0);
				continue;
			}
			spin_lock_irqsave(&xprt->xprt_ctx_lock_lhb1, flags);
			list_for_each_entry(ctx, &xprt->channels,
							port_list_node)
				if (!strcmp(ctx->name, r_ctx->name) &&
							ctx->local_xprt_req &&
							ctx->local_xprt_resp) {
					l_ctx = ctx;
					rwref_get(&l_ctx->ch_state_lhc0);
				}
			spin_unlock_irqrestore(&xprt->xprt_ctx_lock_lhb1,
									flags);
			rwref_write_put(&xprt->xprt_state_lhb0);
		}
	mutex_unlock(&transport_list_lock_lha0);

	return l_ctx;
}

/**
 * find_r_ctx_get() - find a remote channel context based on a local one
 * @l_ctx:	The local channel to use as a lookup key.
 *
 * If the channel is found, the reference count is incremented to ensure the
 * lifetime of the channel context.  The caller must call rwref_put() when done.
 *
 * Return: The corresponding remote ctx or NULL is not found.
 */
static struct channel_ctx *find_r_ctx_get(struct channel_ctx *l_ctx)
{
	struct glink_core_xprt_ctx *xprt;
	struct channel_ctx *ctx;
	unsigned long flags;
	struct channel_ctx *r_ctx = NULL;

	mutex_lock(&transport_list_lock_lha0);
	list_for_each_entry(xprt, &transport_list, list_node)
		if (!strcmp(l_ctx->transport_ptr->edge, xprt->edge)) {
			rwref_write_get(&xprt->xprt_state_lhb0);
			if (xprt->local_state != GLINK_XPRT_OPENED) {
				rwref_write_put(&xprt->xprt_state_lhb0);
				continue;
			}
			spin_lock_irqsave(&xprt->xprt_ctx_lock_lhb1, flags);
			list_for_each_entry(ctx, &xprt->channels,
							port_list_node)
				if (!strcmp(ctx->name, l_ctx->name) &&
							ctx->remote_xprt_req &&
							ctx->remote_xprt_resp) {
					r_ctx = ctx;
					rwref_get(&r_ctx->ch_state_lhc0);
				}
			spin_unlock_irqrestore(&xprt->xprt_ctx_lock_lhb1,
									flags);
			rwref_write_put(&xprt->xprt_state_lhb0);
		}
	mutex_unlock(&transport_list_lock_lha0);

	return r_ctx;
}

/**
 * will_migrate() - will a channel migrate to a different transport
 * @l_ctx:	The local channel to migrate.
 * @r_ctx:	The remote channel to migrate.
 *
 * One of the channel contexts can be NULL if not known, but at least one ctx
 * must be provided.
 *
 * Return: Bool indicating if migration will occur.
 */
static bool will_migrate(struct channel_ctx *l_ctx, struct channel_ctx *r_ctx)
{
	uint16_t new_xprt;
	bool migrate = false;

	if (!r_ctx)
		r_ctx = find_r_ctx_get(l_ctx);
	else
		rwref_get(&r_ctx->ch_state_lhc0);
	if (!r_ctx)
		return migrate;

	if (!l_ctx)
		l_ctx = find_l_ctx_get(r_ctx);
	else
		rwref_get(&l_ctx->ch_state_lhc0);
	if (!l_ctx)
		goto exit;

	if (l_ctx->local_xprt_req == r_ctx->remote_xprt_req &&
			l_ctx->local_xprt_req == l_ctx->transport_ptr->id)
		goto exit;
	if (l_ctx->no_migrate)
		goto exit;

	if (l_ctx->local_xprt_req > r_ctx->transport_ptr->id)
		l_ctx->local_xprt_req = r_ctx->transport_ptr->id;

	new_xprt = max(l_ctx->local_xprt_req, r_ctx->remote_xprt_req);

	if (new_xprt == l_ctx->transport_ptr->id)
		goto exit;

	migrate = true;
exit:
	if (l_ctx)
		rwref_put(&l_ctx->ch_state_lhc0);
	if (r_ctx)
		rwref_put(&r_ctx->ch_state_lhc0);

	return migrate;
}

/**
 * ch_migrate() - migrate a channel to a different transport
 * @l_ctx:	The local channel to migrate.
 * @r_ctx:	The remote channel to migrate.
 *
 * One of the channel contexts can be NULL if not known, but at least one ctx
 * must be provided.
 *
 * Return: Bool indicating if migration occurred.
 */
static bool ch_migrate(struct channel_ctx *l_ctx, struct channel_ctx *r_ctx)
{
	uint16_t new_xprt;
	struct glink_core_xprt_ctx *xprt;
	unsigned long flags;
	struct channel_lcid *flcid;
	uint16_t best_xprt = USHRT_MAX;
	struct channel_ctx *ctx_clone;
	bool migrated = false;

	if (!r_ctx)
		r_ctx = find_r_ctx_get(l_ctx);
	else
		rwref_get(&r_ctx->ch_state_lhc0);
	if (!r_ctx)
		return migrated;

	if (!l_ctx)
		l_ctx = find_l_ctx_get(r_ctx);
	else
		rwref_get(&l_ctx->ch_state_lhc0);
	if (!l_ctx) {
		rwref_put(&r_ctx->ch_state_lhc0);
		return migrated;
	}

	if (l_ctx->local_xprt_req == r_ctx->remote_xprt_req &&
			l_ctx->local_xprt_req == l_ctx->transport_ptr->id)
		goto exit;
	if (l_ctx->no_migrate)
		goto exit;

	if (l_ctx->local_xprt_req > r_ctx->transport_ptr->id)
		l_ctx->local_xprt_req = r_ctx->transport_ptr->id;

	new_xprt = max(l_ctx->local_xprt_req, r_ctx->remote_xprt_req);

	if (new_xprt == l_ctx->transport_ptr->id)
		goto exit;

	ctx_clone = kmalloc(sizeof(*ctx_clone), GFP_KERNEL);
	if (!ctx_clone)
		goto exit;

	mutex_lock(&transport_list_lock_lha0);
	list_for_each_entry(xprt, &transport_list, list_node)
		if (!strcmp(l_ctx->transport_ptr->edge, xprt->edge))
			if (xprt->id == new_xprt)
				break;
	mutex_unlock(&transport_list_lock_lha0);

	spin_lock_irqsave(&l_ctx->transport_ptr->xprt_ctx_lock_lhb1, flags);
	list_del_init(&l_ctx->port_list_node);
	spin_unlock_irqrestore(&l_ctx->transport_ptr->xprt_ctx_lock_lhb1,
									flags);

	memcpy(ctx_clone, l_ctx, sizeof(*ctx_clone));
	ctx_clone->local_xprt_req = 0;
	ctx_clone->local_xprt_resp = 0;
	ctx_clone->remote_xprt_req = 0;
	ctx_clone->remote_xprt_resp = 0;
	ctx_clone->notify_state = NULL;
	ctx_clone->local_open_state = GLINK_CHANNEL_CLOSING;
	rwref_lock_init(&ctx_clone->ch_state_lhc0, glink_ch_ctx_release);
	init_completion(&ctx_clone->int_req_ack_complete);
	init_completion(&ctx_clone->int_req_complete);
	spin_lock_init(&ctx_clone->local_rx_intent_lst_lock_lhc1);
	spin_lock_init(&ctx_clone->rmt_rx_intent_lst_lock_lhc2);
	INIT_LIST_HEAD(&ctx_clone->tx_ready_list_node);
	INIT_LIST_HEAD(&ctx_clone->local_rx_intent_list);
	INIT_LIST_HEAD(&ctx_clone->local_rx_intent_ntfy_list);
	INIT_LIST_HEAD(&ctx_clone->local_rx_intent_free_list);
	INIT_LIST_HEAD(&ctx_clone->rmt_rx_intent_list);
	INIT_LIST_HEAD(&ctx_clone->tx_active);
	spin_lock_init(&ctx_clone->tx_pending_rmt_done_lock_lhc4);
	INIT_LIST_HEAD(&ctx_clone->tx_pending_remote_done);
	spin_lock_init(&ctx_clone->tx_lists_lock_lhc3);
	spin_lock_irqsave(&l_ctx->transport_ptr->xprt_ctx_lock_lhb1, flags);
	list_add_tail(&ctx_clone->port_list_node,
					&l_ctx->transport_ptr->channels);
	spin_unlock_irqrestore(&l_ctx->transport_ptr->xprt_ctx_lock_lhb1,
									flags);

	l_ctx->transport_ptr->ops->tx_cmd_ch_close(l_ctx->transport_ptr->ops,
								l_ctx->lcid);

	l_ctx->transport_ptr = xprt;
	l_ctx->local_xprt_req = 0;
	l_ctx->local_xprt_resp = 0;
	if (new_xprt != r_ctx->transport_ptr->id) {
		r_ctx->local_xprt_req = 0;
		r_ctx->local_xprt_resp = 0;
		r_ctx->remote_xprt_req = 0;
		r_ctx->remote_xprt_resp = 0;

		l_ctx->remote_xprt_req = 0;
		l_ctx->remote_xprt_resp = 0;
		l_ctx->remote_opened = false;

		rwref_write_get(&xprt->xprt_state_lhb0);
		spin_lock_irqsave(&xprt->xprt_ctx_lock_lhb1, flags);
		if (list_empty(&xprt->free_lcid_list)) {
			l_ctx->lcid = xprt->next_lcid++;
		} else {
			flcid = list_first_entry(&xprt->free_lcid_list,
						struct channel_lcid, list_node);
			l_ctx->lcid = flcid->lcid;
			list_del(&flcid->list_node);
			kfree(flcid);
		}
		list_add_tail(&l_ctx->port_list_node, &xprt->channels);
		spin_unlock_irqrestore(&xprt->xprt_ctx_lock_lhb1, flags);
		rwref_write_put(&xprt->xprt_state_lhb0);
	} else {
		l_ctx->lcid = r_ctx->lcid;
		l_ctx->rcid = r_ctx->rcid;
		l_ctx->remote_opened = r_ctx->remote_opened;
		l_ctx->remote_xprt_req = r_ctx->remote_xprt_req;
		l_ctx->remote_xprt_resp = r_ctx->remote_xprt_resp;
		glink_delete_ch_from_list(r_ctx, false);

		spin_lock_irqsave(&xprt->xprt_ctx_lock_lhb1, flags);
		list_add_tail(&l_ctx->port_list_node, &xprt->channels);
		spin_unlock_irqrestore(&xprt->xprt_ctx_lock_lhb1, flags);
	}


	mutex_lock(&transport_list_lock_lha0);
	list_for_each_entry(xprt, &transport_list, list_node)
		if (!strcmp(l_ctx->transport_ptr->edge, xprt->edge))
			if (xprt->id < best_xprt)
				best_xprt = xprt->id;
	mutex_unlock(&transport_list_lock_lha0);
	l_ctx->local_open_state = GLINK_CHANNEL_OPENING;
	l_ctx->local_xprt_req = best_xprt;
	l_ctx->transport_ptr->ops->tx_cmd_ch_open(l_ctx->transport_ptr->ops,
					l_ctx->lcid, l_ctx->name, best_xprt);

	migrated = true;
exit:
	rwref_put(&l_ctx->ch_state_lhc0);
	rwref_put(&r_ctx->ch_state_lhc0);

	return migrated;
}

/**
 * calculate_xprt_resp() - calculate the response to a remote xprt request
 * @r_ctx:	The channel the remote xprt request is for.
 *
 * Return: The calculated response.
 */
static uint16_t calculate_xprt_resp(struct channel_ctx *r_ctx)
{
	struct channel_ctx *l_ctx;

	l_ctx = find_l_ctx_get(r_ctx);
	if (!l_ctx) {
		r_ctx->remote_xprt_resp = r_ctx->transport_ptr->id;
	} else if (r_ctx->remote_xprt_req == r_ctx->transport_ptr->id) {
		r_ctx->remote_xprt_resp = r_ctx->remote_xprt_req;
	} else {
		if (!l_ctx->local_xprt_req)
			r_ctx->remote_xprt_resp = r_ctx->remote_xprt_req;
		else if (l_ctx->no_migrate)
			r_ctx->remote_xprt_resp = l_ctx->local_xprt_req;
		else
			r_ctx->remote_xprt_resp = max(l_ctx->local_xprt_req,
							r_ctx->remote_xprt_req);
	}

	if (l_ctx)
		rwref_put(&l_ctx->ch_state_lhc0);

	return r_ctx->remote_xprt_resp;
}

/**
 * glink_core_rx_cmd_ch_remote_open() - Remote-initiated open command
 *
 * @if_ptr:	Pointer to transport instance
 * @rcid:	Remote Channel ID
 * @name:	Channel name
 * @req_xprt:	Requested transport to migrate to
 */
static void glink_core_rx_cmd_ch_remote_open(struct glink_transport_if *if_ptr,
	uint32_t rcid, const char *name, uint16_t req_xprt)
{
	struct channel_ctx *ctx;
	uint16_t xprt_resp;
	bool do_migrate;

	ctx = ch_name_to_ch_ctx_create(if_ptr->glink_core_priv, name);
	if (ctx == NULL) {
		GLINK_ERR_XPRT(if_ptr->glink_core_priv,
		       "%s: invalid rcid %u received, name '%s'\n",
		       __func__, rcid, name);
		return;
	}

	/* port already exists */
	if (ctx->remote_opened) {
		GLINK_ERR_CH(ctx,
		       "%s: Duplicate remote open for rcid %u, name '%s'\n",
		       __func__, rcid, name);
		return;
	}

	ctx->remote_opened = true;
	ch_add_rcid(if_ptr->glink_core_priv, ctx, rcid);
	ctx->transport_ptr = if_ptr->glink_core_priv;

	ctx->remote_xprt_req = req_xprt;
	xprt_resp = calculate_xprt_resp(ctx);

	do_migrate = will_migrate(NULL, ctx);
	GLINK_INFO_CH(ctx, "%s: remote: CLOSED->OPENED ; xprt req:resp %u:%u\n",
			__func__, req_xprt, xprt_resp);

	if_ptr->tx_cmd_ch_remote_open_ack(if_ptr, rcid, xprt_resp);
	if (!do_migrate && ch_is_fully_opened(ctx))
		ctx->notify_state(ctx, ctx->user_priv, GLINK_CONNECTED);


	if (do_migrate)
		ch_migrate(NULL, ctx);
}

/**
 * glink_core_rx_cmd_ch_open_ack() - Receive ack to previously sent open request
 *
 * if_ptr:	Pointer to transport instance
 * lcid:	Local Channel ID
 * @xprt_resp:	Response to the transport migration request
 */
static void glink_core_rx_cmd_ch_open_ack(struct glink_transport_if *if_ptr,
	uint32_t lcid, uint16_t xprt_resp)
{
	struct channel_ctx *ctx;

	ctx = xprt_lcid_to_ch_ctx_get(if_ptr->glink_core_priv, lcid);
	if (!ctx) {
		/* unknown LCID received - this shouldn't happen */
		GLINK_ERR_XPRT(if_ptr->glink_core_priv,
				"%s: invalid lcid %u received\n", __func__,
				(unsigned)lcid);
		return;
	}

	if (ctx->local_open_state != GLINK_CHANNEL_OPENING) {
		GLINK_ERR_CH(ctx,
			"%s: unexpected open ack receive for lcid. Current state: %u. Thread: %u\n",
				__func__, ctx->local_open_state, current->pid);
		rwref_put(&ctx->ch_state_lhc0);
		return;
	}

	ctx->local_xprt_resp = xprt_resp;
	if (!ch_migrate(ctx, NULL)) {
		ctx->local_open_state = GLINK_CHANNEL_OPENED;
		GLINK_INFO_PERF_CH(ctx,
			"%s: local:GLINK_CHANNEL_OPENING_WAIT->GLINK_CHANNEL_OPENED\n",
			__func__);

		if (ch_is_fully_opened(ctx)) {
			ctx->notify_state(ctx, ctx->user_priv, GLINK_CONNECTED);
			GLINK_INFO_PERF_CH(ctx,
					"%s: notify state: GLINK_CONNECTED\n",
					__func__);
		}
	}
	rwref_put(&ctx->ch_state_lhc0);
}

/**
 * glink_core_rx_cmd_ch_remote_close() - Receive remote close command
 *
 * if_ptr:	Pointer to transport instance
 * rcid:	Remote Channel ID
 */
static void glink_core_rx_cmd_ch_remote_close(
		struct glink_transport_if *if_ptr, uint32_t rcid)
{
	struct channel_ctx *ctx;
	bool is_ch_fully_closed;

	ctx = xprt_rcid_to_ch_ctx_get(if_ptr->glink_core_priv, rcid);
	if (!ctx) {
		/* unknown LCID received - this shouldn't happen */
		GLINK_ERR_XPRT(if_ptr->glink_core_priv,
				"%s: invalid rcid %u received\n", __func__,
				(unsigned)rcid);
		return;
	}

	if (!ctx->remote_opened) {
		GLINK_ERR_CH(ctx,
			"%s: unexpected remote close receive for rcid %u\n",
			__func__, (unsigned)rcid);
		rwref_put(&ctx->ch_state_lhc0);
		return;
	}
	GLINK_INFO_CH(ctx, "%s: remote: OPENED->CLOSED\n", __func__);

	is_ch_fully_closed = glink_core_remote_close_common(ctx);

	ctx->pending_delete = true;
	if_ptr->tx_cmd_ch_remote_close_ack(if_ptr, rcid);

	if (is_ch_fully_closed) {
		glink_delete_ch_from_list(ctx, true);
		flush_workqueue(ctx->transport_ptr->tx_wq);
	}
	rwref_put(&ctx->ch_state_lhc0);
}

/**
 * glink_core_rx_cmd_ch_close_ack() - Receive locally-request close ack
 *
 * if_ptr:	Pointer to transport instance
 * lcid:	Local Channel ID
 */
static void glink_core_rx_cmd_ch_close_ack(struct glink_transport_if *if_ptr,
	uint32_t lcid)
{
	struct channel_ctx *ctx;
	bool is_ch_fully_closed;

	ctx = xprt_lcid_to_ch_ctx_get(if_ptr->glink_core_priv, lcid);
	if (!ctx) {
		/* unknown LCID received - this shouldn't happen */
		GLINK_ERR_XPRT(if_ptr->glink_core_priv,
				"%s: invalid lcid %u received\n", __func__,
				(unsigned)lcid);
		return;
	}

	if (ctx->local_open_state != GLINK_CHANNEL_CLOSING) {
		GLINK_ERR_CH(ctx,
			"%s: unexpected close ack receive for lcid %u\n",
			__func__, (unsigned)lcid);
		rwref_put(&ctx->ch_state_lhc0);
		return;
	}

	is_ch_fully_closed = glink_core_ch_close_ack_common(ctx);
	if (is_ch_fully_closed) {
		glink_delete_ch_from_list(ctx, true);
		flush_workqueue(ctx->transport_ptr->tx_wq);
	}
	rwref_put(&ctx->ch_state_lhc0);
}

/**
 * glink_core_remote_rx_intent_put() - Receive remove intent
 *
 * @if_ptr:	Pointer to transport instance
 * @rcid:	Remote Channel ID
 * @riid:	Remote Intent ID
 * @size:	Size of the remote intent ID
 */
static void glink_core_remote_rx_intent_put(struct glink_transport_if *if_ptr,
		uint32_t rcid, uint32_t riid, size_t size)
{
	struct channel_ctx *ctx;

	ctx = xprt_rcid_to_ch_ctx_get(if_ptr->glink_core_priv, rcid);
	if (!ctx) {
		/* unknown rcid received - this shouldn't happen */
		GLINK_ERR_XPRT(if_ptr->glink_core_priv,
				"%s: invalid rcid received %u\n", __func__,
				(unsigned)rcid);
		return;
	}

	ch_push_remote_rx_intent(ctx, size, riid);
	rwref_put(&ctx->ch_state_lhc0);
}

/**
 * glink_core_rx_cmd_remote_rx_intent_req() - Receive a request for rx_intent
 *                                            from remote side
 * if_ptr:	Pointer to the transport interface
 * rcid:	Remote channel ID
 * size:	size of the intent
 *
 * The function searches for the local channel to which the request for
 * rx_intent has arrived and informs this request to the local channel through
 * notify_rx_intent_req callback registered by the local channel.
 */
static void glink_core_rx_cmd_remote_rx_intent_req(
	struct glink_transport_if *if_ptr, uint32_t rcid, size_t size)
{
	struct channel_ctx *ctx;
	bool cb_ret;

	ctx = xprt_rcid_to_ch_ctx_get(if_ptr->glink_core_priv, rcid);
	if (!ctx) {
		GLINK_ERR_XPRT(if_ptr->glink_core_priv,
				"%s: invalid rcid received %u\n", __func__,
				(unsigned)rcid);
		return;
	}
	if (!ctx->notify_rx_intent_req) {
		GLINK_ERR_CH(ctx,
			"%s: Notify function not defined for local channel",
			__func__);
		rwref_put(&ctx->ch_state_lhc0);
		return;
	}

	cb_ret = ctx->notify_rx_intent_req(ctx, ctx->user_priv, size);
	if_ptr->tx_cmd_remote_rx_intent_req_ack(if_ptr, ctx->lcid, cb_ret);
	rwref_put(&ctx->ch_state_lhc0);
}

/**
 * glink_core_rx_cmd_remote_rx_intent_req_ack()- Receive ack from remote side
 *						for a local rx_intent request
 * if_ptr:	Pointer to the transport interface
 * rcid:	Remote channel ID
 * size:	size of the intent
 *
 * This function receives the ack for rx_intent request from local channel.
 */
static void glink_core_rx_cmd_rx_intent_req_ack(struct glink_transport_if
					*if_ptr, uint32_t rcid, bool granted)
{
	struct channel_ctx *ctx;

	ctx = xprt_rcid_to_ch_ctx_get(if_ptr->glink_core_priv, rcid);
	if (!ctx) {
		GLINK_ERR_XPRT(if_ptr->glink_core_priv,
				"%s: Invalid rcid received %u\n", __func__,
				(unsigned)rcid);
		return;
	}
	ctx->int_req_ack = granted;
	complete_all(&ctx->int_req_ack_complete);
	rwref_put(&ctx->ch_state_lhc0);
}

/**
 * glink_core_rx_get_pkt_ctx() - lookup RX intent structure
 *
 * if_ptr:	Pointer to the transport interface
 * rcid:	Remote channel ID
 * liid:	Local RX Intent ID
 *
 * Note that this function is designed to always be followed by a call to
 * glink_core_rx_put_pkt_ctx() to complete an RX operation by the transport.
 *
 * Return: Pointer to RX intent structure (or NULL if none found)
 */
static struct glink_core_rx_intent *glink_core_rx_get_pkt_ctx(
		struct glink_transport_if *if_ptr, uint32_t rcid, uint32_t liid)
{
	struct channel_ctx *ctx;
	struct glink_core_rx_intent *intent_ptr;

	ctx = xprt_rcid_to_ch_ctx_get(if_ptr->glink_core_priv, rcid);
	if (!ctx) {
		/* unknown LCID received - this shouldn't happen */
		GLINK_ERR_XPRT(if_ptr->glink_core_priv,
				"%s: invalid rcid received %u\n", __func__,
				(unsigned)rcid);
		return NULL;
	}

	/* match pending intent */
	intent_ptr = ch_get_local_rx_intent(ctx, liid);
	if (intent_ptr == NULL) {
		GLINK_ERR_CH(ctx,
			"%s: L[%u]: No matching rx intent\n",
			__func__, liid);
		rwref_put(&ctx->ch_state_lhc0);
		return NULL;
	}

	rwref_put(&ctx->ch_state_lhc0);
	return intent_ptr;
}

/**
 * glink_core_rx_put_pkt_ctx() - lookup RX intent structure
 *
 * if_ptr:	Pointer to the transport interface
 * rcid:	Remote channel ID
 * intent_ptr:	Pointer to the RX intent
 * complete:	Packet has been completely received
 *
 * Note that this function should always be preceded by a call to
 * glink_core_rx_get_pkt_ctx().
 */
void glink_core_rx_put_pkt_ctx(struct glink_transport_if *if_ptr,
	uint32_t rcid, struct glink_core_rx_intent *intent_ptr, bool complete)
{
	struct channel_ctx *ctx;

	if (!complete) {
		GLINK_DBG_XPRT(if_ptr->glink_core_priv,
			"%s: rcid[%u] liid[%u] pkt_size[%zu] write_offset[%zu] Fragment received\n",
				__func__, rcid, intent_ptr->id,
				intent_ptr->pkt_size,
				intent_ptr->write_offset);
		return;
	}

	/* packet complete */
	ctx = xprt_rcid_to_ch_ctx_get(if_ptr->glink_core_priv, rcid);
	if (!ctx) {
		/* unknown LCID received - this shouldn't happen */
		GLINK_ERR_XPRT(if_ptr->glink_core_priv,
			       "%s: invalid rcid received %u\n", __func__,
			       (unsigned)rcid);
		return;
	}

	if (unlikely(intent_ptr->tracer_pkt)) {
		tracer_pkt_log_event(intent_ptr->data, GLINK_CORE_RX);
		ch_set_local_rx_intent_notified(ctx, intent_ptr);
		if (ctx->notify_rx_tracer_pkt)
			ctx->notify_rx_tracer_pkt(ctx, ctx->user_priv,
				intent_ptr->pkt_priv, intent_ptr->data,
				intent_ptr->pkt_size);
		rwref_put(&ctx->ch_state_lhc0);
		return;
	}

	GLINK_PERF_CH(ctx, "%s: L[%u]: data[%p] size[%zu]\n",
		__func__, intent_ptr->id,
		intent_ptr->data ? intent_ptr->data : intent_ptr->iovec,
		intent_ptr->write_offset);
	if (!intent_ptr->data && !ctx->notify_rxv) {
		/* Received a vector, but client can't handle a vector */
		intent_ptr->bounce_buf = linearize_vector(intent_ptr->iovec,
						intent_ptr->pkt_size,
						intent_ptr->vprovider,
						intent_ptr->pprovider);
		if (IS_ERR_OR_NULL(intent_ptr->bounce_buf)) {
			GLINK_ERR_XPRT(if_ptr->glink_core_priv,
				"%s: Error %ld linearizing vector\n", __func__,
				PTR_ERR(intent_ptr->bounce_buf));
			BUG();
			rwref_put(&ctx->ch_state_lhc0);
			return;
		}
	}

	ch_set_local_rx_intent_notified(ctx, intent_ptr);
	if (ctx->notify_rx && (intent_ptr->data || intent_ptr->bounce_buf)) {
		ctx->notify_rx(ctx, ctx->user_priv, intent_ptr->pkt_priv,
			       intent_ptr->data ?
				intent_ptr->data : intent_ptr->bounce_buf,
			       intent_ptr->pkt_size);
	} else if (ctx->notify_rxv) {
		ctx->notify_rxv(ctx, ctx->user_priv, intent_ptr->pkt_priv,
				intent_ptr->iovec, intent_ptr->pkt_size,
				intent_ptr->vprovider, intent_ptr->pprovider);
	} else {
		GLINK_ERR_XPRT(if_ptr->glink_core_priv,
				"%s: Unable to process rx data\n", __func__);
		BUG();
	}
	rwref_put(&ctx->ch_state_lhc0);
}

/**
 * glink_core_rx_cmd_tx_done() - Receive Transmit Done Command
 * @xprt_ptr:	Transport to send packet on.
 * @rcid:	Remote channel ID
 * @riid:	Remote intent ID
 * @reuse:	Reuse the consumed intent
 */
void glink_core_rx_cmd_tx_done(struct glink_transport_if *if_ptr,
			       uint32_t rcid, uint32_t riid, bool reuse)
{
	struct channel_ctx *ctx;
	struct glink_core_tx_pkt *tx_pkt;
	unsigned long flags;
	size_t intent_size;

	ctx = xprt_rcid_to_ch_ctx_get(if_ptr->glink_core_priv, rcid);
	if (!ctx) {
		/* unknown RCID received - this shouldn't happen */
		GLINK_ERR_XPRT(if_ptr->glink_core_priv,
				"%s: invalid rcid %u received\n", __func__,
				rcid);
		return;
	}

	spin_lock_irqsave(&ctx->tx_lists_lock_lhc3, flags);
	tx_pkt = ch_get_tx_pending_remote_done(ctx, riid);
	if (IS_ERR_OR_NULL(tx_pkt)) {
		/*
		 * FUTURE - in the case of a zero-copy transport, this is a
		 * fatal protocol failure since memory corruption could occur
		 * in this case.  Prevent this by adding code in glink_close()
		 * to recall any buffers in flight / wait for them to be
		 * returned.
		 */
		GLINK_ERR_CH(ctx, "%s: R[%u]: No matching tx\n",
				__func__,
				(unsigned)riid);
		spin_unlock_irqrestore(&ctx->tx_lists_lock_lhc3, flags);
		rwref_put(&ctx->ch_state_lhc0);
		return;
	}

	/* notify client */
	ctx->notify_tx_done(ctx, ctx->user_priv, tx_pkt->pkt_priv,
			    tx_pkt->data ? tx_pkt->data : tx_pkt->iovec);
	intent_size = tx_pkt->intent_size;
	ch_remove_tx_pending_remote_done(ctx, tx_pkt);
	spin_unlock_irqrestore(&ctx->tx_lists_lock_lhc3, flags);

	if (reuse)
		ch_push_remote_rx_intent(ctx, intent_size, riid);
	rwref_put(&ctx->ch_state_lhc0);
}

/**
 * xprt_schedule_tx() - Schedules packet for transmit.
 * @xprt_ptr:	Transport to send packet on.
 * @ch_ptr:	Channel to send packet on.
 * @tx_info:	Packet to transmit.
 */
static void xprt_schedule_tx(struct glink_core_xprt_ctx *xprt_ptr,
			     struct channel_ctx *ch_ptr,
			     struct glink_core_tx_pkt *tx_info)
{
	unsigned long flags;

	if (unlikely(xprt_ptr->local_state == GLINK_XPRT_DOWN)) {
		GLINK_ERR_CH(ch_ptr, "%s: Error XPRT is down\n", __func__);
		kfree(tx_info);
		return;
	}

	spin_lock_irqsave(&xprt_ptr->tx_ready_lock_lhb2, flags);
	if (unlikely(!ch_is_fully_opened(ch_ptr))) {
		spin_unlock_irqrestore(&xprt_ptr->tx_ready_lock_lhb2, flags);
		GLINK_ERR_CH(ch_ptr, "%s: Channel closed before tx\n",
			     __func__);
		kfree(tx_info);
		return;
	}
	if (list_empty(&ch_ptr->tx_ready_list_node))
		list_add_tail(&ch_ptr->tx_ready_list_node,
			&xprt_ptr->prio_bin[ch_ptr->curr_priority].tx_ready);

	spin_lock(&ch_ptr->tx_lists_lock_lhc3);
	list_add_tail(&tx_info->list_node, &ch_ptr->tx_active);
	glink_qos_do_ch_tx(ch_ptr);
	if (unlikely(tx_info->tracer_pkt))
		tracer_pkt_log_event((void *)(tx_info->data),
				     GLINK_QUEUE_TO_SCHEDULER);

	spin_unlock(&ch_ptr->tx_lists_lock_lhc3);
	spin_unlock_irqrestore(&xprt_ptr->tx_ready_lock_lhb2, flags);

	queue_work(xprt_ptr->tx_wq, &xprt_ptr->tx_work);
}

/**
 * xprt_single_threaded_tx() - Transmit in the context of sender.
 * @xprt_ptr:	Transport to send packet on.
 * @ch_ptr:	Channel to send packet on.
 * @tx_info:	Packet to transmit.
 */
static int xprt_single_threaded_tx(struct glink_core_xprt_ctx *xprt_ptr,
			     struct channel_ctx *ch_ptr,
			     struct glink_core_tx_pkt *tx_info)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&ch_ptr->tx_pending_rmt_done_lock_lhc4, flags);
	do {
		ret = xprt_ptr->ops->tx(ch_ptr->transport_ptr->ops,
					ch_ptr->lcid, tx_info);
	} while (ret == -EAGAIN);
	if (ret < 0 || tx_info->size_remaining) {
		GLINK_ERR_CH(ch_ptr, "%s: Error %d writing data\n",
			     __func__, ret);
		kfree(tx_info);
	} else {
		list_add_tail(&tx_info->list_done,
			      &ch_ptr->tx_pending_remote_done);
		ret = 0;
	}
	spin_unlock_irqrestore(&ch_ptr->tx_pending_rmt_done_lock_lhc4, flags);
	return ret;
}

/**
 * glink_scheduler_eval_prio() - Evaluate the channel priority
 * @ctx:	Channel whose priority is evaluated.
 * @xprt_ctx:	Transport in which the channel is part of.
 *
 * This function is called by the packet scheduler to measure the traffic
 * rate observed in the channel and compare it against the traffic rate
 * requested by the channel. The comparison result is used to evaluate the
 * priority of the channel.
 */
static void glink_scheduler_eval_prio(struct channel_ctx *ctx,
			struct glink_core_xprt_ctx *xprt_ctx)
{
	unsigned long token_end_time;
	unsigned long token_consume_time, rem;
	unsigned long obs_rate_kBps;

	if (ctx->initial_priority == 0)
		return;

	if (ctx->token_count)
		return;

	token_end_time = arch_counter_get_cntpct();

	token_consume_time = NSEC_PER_SEC;
	rem = do_div(token_consume_time, arch_timer_get_rate());
	token_consume_time = (token_end_time - ctx->token_start_time) *
				token_consume_time;
	rem = do_div(token_consume_time, 1000);
	obs_rate_kBps = glink_qos_calc_rate_kBps(ctx->txd_len,
				token_consume_time);
	if (obs_rate_kBps > ctx->req_rate_kBps) {
		GLINK_INFO_CH(ctx, "%s: Obs. Rate (%lu) > Req. Rate (%lu)\n",
			__func__, obs_rate_kBps, ctx->req_rate_kBps);
		glink_qos_update_ch_prio(ctx, 0);
	} else {
		glink_qos_update_ch_prio(ctx, ctx->initial_priority);
	}

	ctx->token_count = xprt_ctx->token_count;
	ctx->txd_len = 0;
	ctx->token_start_time = arch_counter_get_cntpct();
}

/**
 * glink_scheduler_tx() - Transmit operation by the scheduler
 * @ctx:	Channel which is scheduled for transmission.
 * @xprt_ctx:	Transport context in which the transmission is performed.
 *
 * This function is called by the scheduler after scheduling a channel for
 * transmission over the transport.
 *
 * Return: return value as returned by the transport on success,
 *         standard Linux error codes on failure.
 */
static int glink_scheduler_tx(struct channel_ctx *ctx,
			struct glink_core_xprt_ctx *xprt_ctx)
{
	unsigned long flags;
	struct glink_core_tx_pkt *tx_info;
	size_t txd_len = 0;
	size_t tx_len = 0;
	uint32_t num_pkts = 0;
	int ret;

	spin_lock_irqsave(&ctx->tx_lists_lock_lhc3, flags);
	while (txd_len < xprt_ctx->mtu &&
		!list_empty(&ctx->tx_active)) {
		tx_info = list_first_entry(&ctx->tx_active,
				struct glink_core_tx_pkt, list_node);
		rwref_get(&tx_info->pkt_ref);

		spin_lock(&ctx->tx_pending_rmt_done_lock_lhc4);
		if (list_empty(&tx_info->list_done))
			list_add(&tx_info->list_done,
				 &ctx->tx_pending_remote_done);
		spin_unlock(&ctx->tx_pending_rmt_done_lock_lhc4);
		spin_unlock_irqrestore(&ctx->tx_lists_lock_lhc3, flags);

		if (unlikely(tx_info->tracer_pkt)) {
			tracer_pkt_log_event((void *)(tx_info->data),
					      GLINK_SCHEDULER_TX);
			ret = xprt_ctx->ops->tx_cmd_tracer_pkt(xprt_ctx->ops,
						ctx->lcid, tx_info);
		} else {
			tx_len = tx_info->size_remaining <
				 (xprt_ctx->mtu - txd_len) ?
				 tx_info->size_remaining :
				 (xprt_ctx->mtu - txd_len);
			tx_info->tx_len = tx_len;
			ret = xprt_ctx->ops->tx(xprt_ctx->ops,
						ctx->lcid, tx_info);
		}
		spin_lock_irqsave(&ctx->tx_lists_lock_lhc3, flags);
		if (ret == -EAGAIN) {
			/*
			 * transport unable to send at the moment and will call
			 * tx_resume() when it can send again.
			 */
			rwref_put(&tx_info->pkt_ref);
			break;
		} else if (ret < 0) {
			/*
			 * General failure code that indicates that the
			 * transport is unable to recover.  In this case, the
			 * communication failure will be detected at a higher
			 * level and a subsystem restart of the affected system
			 * will be triggered.
			 */
			GLINK_ERR_XPRT(xprt_ctx,
					"%s: unrecoverable xprt failure %d\n",
					__func__, ret);
			rwref_put(&tx_info->pkt_ref);
			break;
		} else if (!ret && tx_info->size_remaining) {
			/*
			 * Transport unable to send any data on this channel.
			 * Break out of the loop so that the scheduler can
			 * continue with the next channel.
			 */
			break;
		} else {
			txd_len += tx_len;
		}

		if (!tx_info->size_remaining) {
			num_pkts++;
			list_del_init(&tx_info->list_node);
			rwref_put(&tx_info->pkt_ref);
		}
	}

	ctx->txd_len += txd_len;
	if (txd_len) {
		if (num_pkts >= ctx->token_count)
			ctx->token_count = 0;
		else if (num_pkts)
			ctx->token_count -= num_pkts;
		else
			ctx->token_count--;
	}
	spin_unlock_irqrestore(&ctx->tx_lists_lock_lhc3, flags);

	return ret;
}

/**
 * tx_work_func() - Transmit worker
 * @work:	Linux work structure
 */
static void tx_work_func(struct work_struct *work)
{
	struct glink_core_xprt_ctx *xprt_ptr =
			container_of(work, struct glink_core_xprt_ctx, tx_work);
	struct channel_ctx *ch_ptr;
	uint32_t prio;
	uint32_t tx_ready_head_prio = 0;
	int ret = 0;
	struct channel_ctx *tx_ready_head = NULL;
	bool transmitted_successfully = true;
	unsigned long flags;

	GLINK_PERF("%s: worker starting\n", __func__);

	while (1) {
		prio = xprt_ptr->num_priority - 1;
		spin_lock_irqsave(&xprt_ptr->tx_ready_lock_lhb2, flags);
		while (list_empty(&xprt_ptr->prio_bin[prio].tx_ready)) {
			if (prio == 0) {
				spin_unlock_irqrestore(
					&xprt_ptr->tx_ready_lock_lhb2, flags);
				return;
			}
			prio--;
		}
		glink_pm_qos_vote(xprt_ptr);
		ch_ptr = list_first_entry(&xprt_ptr->prio_bin[prio].tx_ready,
				struct channel_ctx, tx_ready_list_node);
		spin_unlock_irqrestore(&xprt_ptr->tx_ready_lock_lhb2, flags);

		if (tx_ready_head == NULL || tx_ready_head_prio < prio) {
			tx_ready_head = ch_ptr;
			tx_ready_head_prio = prio;
		}

		if (ch_ptr == tx_ready_head && !transmitted_successfully) {
			GLINK_ERR_XPRT(xprt_ptr,
				"%s: Unable to send data on this transport.\n",
				__func__);
			break;
		}
		transmitted_successfully = false;

		ret = glink_scheduler_tx(ch_ptr, xprt_ptr);
		if (ret == -EAGAIN) {
			/*
			 * transport unable to send at the moment and will call
			 * tx_resume() when it can send again.
			 */
			break;
		} else if (ret < 0) {
			/*
			 * General failure code that indicates that the
			 * transport is unable to recover.  In this case, the
			 * communication failure will be detected at a higher
			 * level and a subsystem restart of the affected system
			 * will be triggered.
			 */
			GLINK_ERR_XPRT(xprt_ptr,
					"%s: unrecoverable xprt failure %d\n",
					__func__, ret);
			break;
		} else if (!ret) {
			/*
			 * Transport unable to send any data on this channel,
			 * but didn't return an error. Move to the next channel
			 * and continue.
			 */
			spin_lock_irqsave(&xprt_ptr->tx_ready_lock_lhb2, flags);
			list_rotate_left(&xprt_ptr->prio_bin[prio].tx_ready);
			spin_unlock_irqrestore(&xprt_ptr->tx_ready_lock_lhb2,
						flags);
			continue;
		}

		spin_lock_irqsave(&xprt_ptr->tx_ready_lock_lhb2, flags);
		spin_lock(&ch_ptr->tx_lists_lock_lhc3);

		glink_scheduler_eval_prio(ch_ptr, xprt_ptr);
		if (list_empty(&ch_ptr->tx_active)) {
			list_del_init(&ch_ptr->tx_ready_list_node);
			glink_qos_done_ch_tx(ch_ptr);
		}

		spin_unlock(&ch_ptr->tx_lists_lock_lhc3);
		spin_unlock_irqrestore(&xprt_ptr->tx_ready_lock_lhb2, flags);

		tx_ready_head = NULL;
		transmitted_successfully = true;
	}
	glink_pm_qos_unvote(xprt_ptr);
	GLINK_PERF("%s: worker exiting\n", __func__);
}

static void glink_core_tx_resume(struct glink_transport_if *if_ptr)
{
	queue_work(if_ptr->glink_core_priv->tx_wq,
					&if_ptr->glink_core_priv->tx_work);
}

/**
 * glink_pm_qos_vote() - Add Power Management QoS Vote
 * @xprt_ptr:	Transport for power vote
 *
 * Note - must be called with tx_ready_lock_lhb2 locked.
 */
static void glink_pm_qos_vote(struct glink_core_xprt_ctx *xprt_ptr)
{
	if (glink_pm_qos && !xprt_ptr->qos_req_active) {
		GLINK_PERF("%s: qos vote %u us\n", __func__, glink_pm_qos);
		pm_qos_update_request(&xprt_ptr->pm_qos_req, glink_pm_qos);
		xprt_ptr->qos_req_active = true;
	}
	xprt_ptr->tx_path_activity = true;
}

/**
 * glink_pm_qos_unvote() - Schedule Power Management QoS Vote Removal
 * @xprt_ptr:	Transport for power vote removal
 *
 * Note - must be called with tx_ready_lock_lhb2 locked.
 */
static void glink_pm_qos_unvote(struct glink_core_xprt_ctx *xprt_ptr)
{
	xprt_ptr->tx_path_activity = false;
	if (xprt_ptr->qos_req_active) {
		GLINK_PERF("%s: qos unvote\n", __func__);
		schedule_delayed_work(&xprt_ptr->pm_qos_work,
				msecs_to_jiffies(GLINK_PM_QOS_HOLDOFF_MS));
	}
}

/**
 * glink_pm_qos_cancel_worker() - Remove Power Management QoS Vote
 * @work:	Delayed work structure
 *
 * Removes PM QoS vote if no additional transmit activity has occurred between
 * the unvote and when this worker runs.
 */
static void glink_pm_qos_cancel_worker(struct work_struct *work)
{
	struct glink_core_xprt_ctx *xprt_ptr;
	unsigned long flags;

	xprt_ptr = container_of(to_delayed_work(work),
			struct glink_core_xprt_ctx, pm_qos_work);

	spin_lock_irqsave(&xprt_ptr->tx_ready_lock_lhb2, flags);
	if (!xprt_ptr->tx_path_activity) {
		/* no more tx activity */
		GLINK_PERF("%s: qos off\n", __func__);
		pm_qos_update_request(&xprt_ptr->pm_qos_req,
				PM_QOS_DEFAULT_VALUE);
		xprt_ptr->qos_req_active = false;
	}
	xprt_ptr->tx_path_activity = false;
	spin_unlock_irqrestore(&xprt_ptr->tx_ready_lock_lhb2, flags);
}

/**
 * glink_core_rx_cmd_remote_sigs() - Receive remote channel signal command
 *
 * if_ptr:	Pointer to transport instance
 * rcid:	Remote Channel ID
 */
static void glink_core_rx_cmd_remote_sigs(struct glink_transport_if *if_ptr,
					uint32_t rcid, uint32_t sigs)
{
	struct channel_ctx *ctx;
	uint32_t old_sigs;

	ctx = xprt_rcid_to_ch_ctx_get(if_ptr->glink_core_priv, rcid);
	if (!ctx) {
		/* unknown LCID received - this shouldn't happen */
		GLINK_ERR_XPRT(if_ptr->glink_core_priv,
				"%s: invalid rcid %u received\n", __func__,
				(unsigned)rcid);
		return;
	}

	if (!ch_is_fully_opened(ctx)) {
		GLINK_ERR_CH(ctx, "%s: Channel is not fully opened\n",
			__func__);
		rwref_put(&ctx->ch_state_lhc0);
		return;
	}

	old_sigs = ctx->rsigs;
	ctx->rsigs = sigs;
	if (ctx->notify_rx_sigs) {
		ctx->notify_rx_sigs(ctx, ctx->user_priv, old_sigs, ctx->rsigs);
		GLINK_INFO_CH(ctx, "%s: notify rx sigs old:0x%x new:0x%x\n",
				__func__, old_sigs, ctx->rsigs);
	}
	rwref_put(&ctx->ch_state_lhc0);
}

static struct glink_core_if core_impl = {
	.link_up = glink_core_link_up,
	.link_down = glink_core_link_down,
	.rx_cmd_version = glink_core_rx_cmd_version,
	.rx_cmd_version_ack = glink_core_rx_cmd_version_ack,
	.rx_cmd_ch_remote_open = glink_core_rx_cmd_ch_remote_open,
	.rx_cmd_ch_open_ack = glink_core_rx_cmd_ch_open_ack,
	.rx_cmd_ch_remote_close = glink_core_rx_cmd_ch_remote_close,
	.rx_cmd_ch_close_ack = glink_core_rx_cmd_ch_close_ack,
	.rx_get_pkt_ctx = glink_core_rx_get_pkt_ctx,
	.rx_put_pkt_ctx = glink_core_rx_put_pkt_ctx,
	.rx_cmd_remote_rx_intent_put = glink_core_remote_rx_intent_put,
	.rx_cmd_remote_rx_intent_req = glink_core_rx_cmd_remote_rx_intent_req,
	.rx_cmd_rx_intent_req_ack = glink_core_rx_cmd_rx_intent_req_ack,
	.rx_cmd_tx_done = glink_core_rx_cmd_tx_done,
	.tx_resume = glink_core_tx_resume,
	.rx_cmd_remote_sigs = glink_core_rx_cmd_remote_sigs,
};

/**
 * glink_xprt_ctx_iterator_init() - Initializes the transport context list iterator
 * @xprt_i:	pointer to the transport context iterator.
 *
 * This function acquires the transport context lock which must then be
 * released by glink_xprt_ctx_iterator_end()
 */
void glink_xprt_ctx_iterator_init(struct xprt_ctx_iterator *xprt_i)
{
	if (xprt_i == NULL)
		return;

	mutex_lock(&transport_list_lock_lha0);
	xprt_i->xprt_list = &transport_list;
	xprt_i->i_curr = list_entry(&transport_list,
			struct glink_core_xprt_ctx, list_node);
}
EXPORT_SYMBOL(glink_xprt_ctx_iterator_init);

/**
 * glink_xprt_ctx_iterator_end() - Ends the transport context list iteration
 * @xprt_i:	pointer to the transport context iterator.
 */
void glink_xprt_ctx_iterator_end(struct xprt_ctx_iterator *xprt_i)
{
	if (xprt_i == NULL)
		return;

	xprt_i->xprt_list = NULL;
	xprt_i->i_curr = NULL;
	mutex_unlock(&transport_list_lock_lha0);
}
EXPORT_SYMBOL(glink_xprt_ctx_iterator_end);

/**
 * glink_xprt_ctx_iterator_next() - iterates element by element in transport context list
 * @xprt_i:	pointer to the transport context iterator.
 *
 * Return: pointer to the transport context structure
 */
struct glink_core_xprt_ctx *glink_xprt_ctx_iterator_next(
			struct xprt_ctx_iterator *xprt_i)
{
	struct glink_core_xprt_ctx *xprt_ctx = NULL;

	if (xprt_i == NULL)
		return xprt_ctx;

	if (list_empty(xprt_i->xprt_list))
		return xprt_ctx;

	list_for_each_entry_continue(xprt_i->i_curr,
			xprt_i->xprt_list, list_node) {
		xprt_ctx = xprt_i->i_curr;
		break;
	}
	return xprt_ctx;
}
EXPORT_SYMBOL(glink_xprt_ctx_iterator_next);

/**
 * glink_get_xprt_name() - get the transport name
 * @xprt_ctx:	pointer to the transport context.
 *
 * Return: name of the transport
 */
char *glink_get_xprt_name(struct glink_core_xprt_ctx *xprt_ctx)
{
	if (xprt_ctx == NULL)
		return NULL;

	return xprt_ctx->name;
}
EXPORT_SYMBOL(glink_get_xprt_name);

/**
 * glink_get_xprt_name() - get the name of the remote processor/edge
 *				of the transport
 * @xprt_ctx:	pointer to the transport context.
 *
 * Return: Name of the remote processor/edge
 */
char *glink_get_xprt_edge_name(struct glink_core_xprt_ctx *xprt_ctx)
{
	if (xprt_ctx == NULL)
		return NULL;
	return xprt_ctx->edge;
}
EXPORT_SYMBOL(glink_get_xprt_edge_name);

/**
 * glink_get_xprt_state() - get the state of the transport
 * @xprt_ctx:	pointer to the transport context.
 *
 * Return: Name of the transport state, NULL in case of invalid input
 */
const char *glink_get_xprt_state(struct glink_core_xprt_ctx *xprt_ctx)
{
	if (xprt_ctx == NULL)
		return NULL;

	return glink_get_xprt_state_string(xprt_ctx->local_state);
}
EXPORT_SYMBOL(glink_get_xprt_state);

/**
 * glink_get_xprt_version_features() - get the version and feature set
 *					of local transport in glink
 * @xprt_ctx:	pointer to the transport context.
 *
 * Return: pointer to the glink_core_version
 */
const struct glink_core_version *glink_get_xprt_version_features(
		struct glink_core_xprt_ctx *xprt_ctx)
{
	const struct glink_core_version *ver = NULL;
	if (xprt_ctx == NULL)
		return ver;

	ver = &xprt_ctx->versions[xprt_ctx->local_version_idx];
	return ver;
}
EXPORT_SYMBOL(glink_get_xprt_version_features);

/**
 * glink_ch_ctx_iterator_init() - Initializes the channel context list iterator
 * @ch_iter:	pointer to the channel context iterator.
 * xprt:	pointer to the transport context that holds the channel list
 *
 * This function acquires the channel context lock which must then be
 * released by glink_ch_ctx_iterator_end()
 */
void glink_ch_ctx_iterator_init(struct ch_ctx_iterator *ch_iter,
		struct glink_core_xprt_ctx *xprt)
{
	unsigned long flags;

	if (ch_iter == NULL || xprt == NULL)
		return;

	spin_lock_irqsave(&xprt->xprt_ctx_lock_lhb1, flags);
	ch_iter->ch_list = &(xprt->channels);
	ch_iter->i_curr = list_entry(&(xprt->channels),
				struct channel_ctx, port_list_node);
	ch_iter->ch_list_flags = flags;
}
EXPORT_SYMBOL(glink_ch_ctx_iterator_init);

/**
 * glink_ch_ctx_iterator_end() - Ends the channel context list iteration
 * @ch_iter:	pointer to the channel context iterator.
 */
void glink_ch_ctx_iterator_end(struct ch_ctx_iterator *ch_iter,
				struct glink_core_xprt_ctx *xprt)
{
	if (ch_iter == NULL || xprt == NULL)
		return;

	spin_unlock_irqrestore(&xprt->xprt_ctx_lock_lhb1,
			ch_iter->ch_list_flags);
	ch_iter->ch_list = NULL;
	ch_iter->i_curr = NULL;
}
EXPORT_SYMBOL(glink_ch_ctx_iterator_end);

/**
 * glink_ch_ctx_iterator_next() - iterates element by element in channel context list
 * @c_i:	pointer to the channel context iterator.
 *
 * Return: pointer to the channel context structure
 */
struct channel_ctx *glink_ch_ctx_iterator_next(struct ch_ctx_iterator *c_i)
{
	struct channel_ctx *ch_ctx = NULL;

	if (c_i == NULL)
		return ch_ctx;

	if (list_empty(c_i->ch_list))
		return ch_ctx;

	list_for_each_entry_continue(c_i->i_curr,
			c_i->ch_list, port_list_node) {
		ch_ctx = c_i->i_curr;
		break;
	}
	return ch_ctx;
}
EXPORT_SYMBOL(glink_ch_ctx_iterator_next);

/**
 * glink_get_ch_name() - get the channel name
 * @ch_ctx:	pointer to the channel context.
 *
 * Return: name of the channel, NULL in case of invalid input
 */
char *glink_get_ch_name(struct channel_ctx *ch_ctx)
{
	if (ch_ctx == NULL)
		return NULL;

	return ch_ctx->name;
}
EXPORT_SYMBOL(glink_get_ch_name);

/**
 * glink_get_ch_edge_name() - get the edge on whcih channel is created
 * @ch_ctx:	pointer to the channel context.
 *
 * Return: name of the edge, NULL in case of invalid input
 */
char *glink_get_ch_edge_name(struct channel_ctx *ch_ctx)
{
	if (ch_ctx == NULL)
		return NULL;

	return ch_ctx->transport_ptr->edge;
}
EXPORT_SYMBOL(glink_get_ch_edge_name);

/**
 * glink_get_ch_lcid() - get the local channel ID
 * @c_i:	pointer to the channel context.
 *
 * Return: local channel id, -EINVAL in case of invalid input
 */
int glink_get_ch_lcid(struct channel_ctx *ch_ctx)
{
	if (ch_ctx == NULL)
		return -EINVAL;

	return ch_ctx->lcid;
}
EXPORT_SYMBOL(glink_get_ch_lcid);

/**
 * glink_get_ch_rcid() - get the remote channel ID
 * @ch_ctx:	pointer to the channel context.
 *
 * Return: remote channel id, -EINVAL in case of invalid input
 */
int glink_get_ch_rcid(struct channel_ctx *ch_ctx)
{
	if (ch_ctx == NULL)
		return -EINVAL;

	return ch_ctx->rcid;
}
EXPORT_SYMBOL(glink_get_ch_rcid);

/**
 * glink_get_ch_lstate() - get the local channel state
 * @ch_ctx:	pointer to the channel context.
 *
 * Return: Name of the local channel state, NUll in case of invalid input
 */
const char *glink_get_ch_lstate(struct channel_ctx *ch_ctx)
{
	if (ch_ctx == NULL)
		return NULL;

	return glink_get_ch_state_string(ch_ctx->local_open_state);
}
EXPORT_SYMBOL(glink_get_ch_lstate);

/**
 * glink_get_ch_rstate() - get the remote channel state
 * @ch_ctx:	pointer to the channel context.
 *
 * Return: true if remote side is opened false otherwise
 */
bool glink_get_ch_rstate(struct channel_ctx *ch_ctx)
{
	if (ch_ctx == NULL)
		return NULL;

	return ch_ctx->remote_opened;
}
EXPORT_SYMBOL(glink_get_ch_rstate);

/**
 * glink_get_ch_xprt_name() - get the name of the transport to which
 *				the channel belongs
 * @ch_ctx:	pointer to the channel context.
 *
 * Return: name of the export, NULL in case of invalid input
 */
char *glink_get_ch_xprt_name(struct channel_ctx *ch_ctx)
{
	if (ch_ctx == NULL)
		return NULL;

	return ch_ctx->transport_ptr->name;
}
EXPORT_SYMBOL(glink_get_ch_xprt_name);

/**
 * glink_get_tx_pkt_count() - get the total number of packets sent
 *				through this channel
 * @ch_ctx:	pointer to the channel context.
 *
 * Return: number of packets transmitted, -EINVAL in case of invalid input
 */
int glink_get_ch_tx_pkt_count(struct channel_ctx *ch_ctx)
{
	if (ch_ctx == NULL)
		return -EINVAL;

	/* FUTURE: packet stats not yet implemented */

	return -ENOSYS;
}
EXPORT_SYMBOL(glink_get_ch_tx_pkt_count);

/**
 * glink_get_ch_rx_pkt_count() - get the total number of packets
 *				recieved at this channel
 * @ch_ctx:	pointer to the channel context.
 *
 * Return: number of packets recieved, -EINVAL in case of invalid input
 */
int glink_get_ch_rx_pkt_count(struct channel_ctx *ch_ctx)
{
	if (ch_ctx == NULL)
		return -EINVAL;

	/* FUTURE: packet stats not yet implemented */

	return -ENOSYS;
}
EXPORT_SYMBOL(glink_get_ch_rx_pkt_count);

/**
 * glink_get_ch_lintents_queued() - get the total number of intents queued
 *				at local side
 * @ch_ctx:	pointer to the channel context.
 *
 * Return: number of intents queued, -EINVAL in case of invalid input
 */
int glink_get_ch_lintents_queued(struct channel_ctx *ch_ctx)
{
	struct glink_core_rx_intent *intent;
	int ilrx_count = 0;

	if (ch_ctx == NULL)
		return -EINVAL;

	list_for_each_entry(intent, &ch_ctx->local_rx_intent_list, list)
		ilrx_count++;

	return ilrx_count;
}
EXPORT_SYMBOL(glink_get_ch_lintents_queued);

/**
 * glink_get_ch_rintents_queued() - get the total number of intents queued
 *				from remote side
 * @ch_ctx:	pointer to the channel context.
 *
 * Return: number of intents queued, -EINVAL in case of invalid input
 */
int glink_get_ch_rintents_queued(struct channel_ctx *ch_ctx)
{
	struct glink_core_rx_intent *intent;
	int irrx_count = 0;

	if (ch_ctx == NULL)
		return -EINVAL;

	list_for_each_entry(intent, &ch_ctx->rmt_rx_intent_list, list)
		irrx_count++;

	return irrx_count;
}
EXPORT_SYMBOL(glink_get_ch_rintents_queued);

/**
 * glink_get_ch_intent_info() - get the intent details of a channel
 * @ch_ctx:	pointer to the channel context.
 * ch_ctx_i:	pointer to a structure that will contain intent details
 *
 * This function is used to get all the channel intent details including locks.
 */
void glink_get_ch_intent_info(struct channel_ctx *ch_ctx,
			struct glink_ch_intent_info *ch_ctx_i)
{
	if (ch_ctx == NULL || ch_ctx_i == NULL)
		return;

	ch_ctx_i->li_lst_lock = &ch_ctx->local_rx_intent_lst_lock_lhc1;
	ch_ctx_i->li_avail_list = &ch_ctx->local_rx_intent_list;
	ch_ctx_i->li_used_list = &ch_ctx->local_rx_intent_ntfy_list;
	ch_ctx_i->ri_lst_lock = &ch_ctx->rmt_rx_intent_lst_lock_lhc2;
	ch_ctx_i->ri_list = &ch_ctx->rmt_rx_intent_list;
}
EXPORT_SYMBOL(glink_get_ch_intent_info);

/**
 * glink_get_debug_mask() - Return debug mask attribute
 *
 * Return: debug mask attribute
 */
unsigned glink_get_debug_mask(void)
{
	return glink_debug_mask;
}
EXPORT_SYMBOL(glink_get_debug_mask);

/**
 * glink_get_log_ctx() - Return log context for other GLINK modules.
 *
 * Return: Log context or NULL if none.
 */
void *glink_get_log_ctx(void)
{
	return log_ctx;
}
EXPORT_SYMBOL(glink_get_log_ctx);

/**
 * glink_get_xprt_log_ctx() - Return log context for GLINK xprts.
 *
 * Return: Log context or NULL if none.
 */
void *glink_get_xprt_log_ctx(struct glink_core_xprt_ctx *xprt)
{
	if (xprt)
		return xprt->log_ctx;
	else
		return NULL;
}
EXPORT_SYMBOL(glink_get_xprt_log_ctx);

static int glink_init(void)
{
	log_ctx = ipc_log_context_create(NUM_LOG_PAGES, "glink", 0);
	if (!log_ctx)
		GLINK_ERR("%s: unable to create log context\n", __func__);
	glink_debugfs_init();

	return 0;
}
arch_initcall(glink_init);

MODULE_DESCRIPTION("MSM Generic Link (G-Link) Transport");
MODULE_LICENSE("GPL v2");
