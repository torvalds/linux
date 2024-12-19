// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024, Intel Corporation. */

#include "health.h"
#include "ice.h"

#define ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, obj, name) \
	devlink_fmsg_put(fmsg, #name, (obj)->name)

/**
 * ice_devlink_health_report - boilerplate to call given @reporter
 *
 * @reporter: devlink health reporter to call, do nothing on NULL
 * @msg: message to pass up, "event name" is fine
 * @priv_ctx: typically some event struct
 */
static void ice_devlink_health_report(struct devlink_health_reporter *reporter,
				      const char *msg, void *priv_ctx)
{
	if (!reporter)
		return;

	/* We do not do auto recovering, so return value of the below function
	 * will always be 0, thus we do ignore it.
	 */
	devlink_health_report(reporter, msg, priv_ctx);
}

struct ice_mdd_event {
	enum ice_mdd_src src;
	u16 vf_num;
	u16 queue;
	u8 pf_num;
	u8 event;
};

static const char *ice_mdd_src_to_str(enum ice_mdd_src src)
{
	switch (src) {
	case ICE_MDD_SRC_TX_PQM:
		return "tx_pqm";
	case ICE_MDD_SRC_TX_TCLAN:
		return "tx_tclan";
	case ICE_MDD_SRC_TX_TDPU:
		return "tx_tdpu";
	case ICE_MDD_SRC_RX:
		return "rx";
	default:
		return "invalid";
	}
}

static int
ice_mdd_reporter_dump(struct devlink_health_reporter *reporter,
		      struct devlink_fmsg *fmsg, void *priv_ctx,
		      struct netlink_ext_ack *extack)
{
	struct ice_mdd_event *mdd_event = priv_ctx;
	const char *src;

	if (!mdd_event)
		return 0;

	src = ice_mdd_src_to_str(mdd_event->src);

	devlink_fmsg_obj_nest_start(fmsg);
	devlink_fmsg_put(fmsg, "src", src);
	ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, mdd_event, pf_num);
	ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, mdd_event, vf_num);
	ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, mdd_event, event);
	ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, mdd_event, queue);
	devlink_fmsg_obj_nest_end(fmsg);

	return 0;
}

/**
 * ice_report_mdd_event - Report an MDD event through devlink health
 * @pf: the PF device structure
 * @src: the HW block that was the source of this MDD event
 * @pf_num: the pf_num on which the MDD event occurred
 * @vf_num: the vf_num on which the MDD event occurred
 * @event: the event type of the MDD event
 * @queue: the queue on which the MDD event occurred
 *
 * Report an MDD event that has occurred on this PF.
 */
void ice_report_mdd_event(struct ice_pf *pf, enum ice_mdd_src src, u8 pf_num,
			  u16 vf_num, u8 event, u16 queue)
{
	struct ice_mdd_event ev = {
		.src = src,
		.pf_num = pf_num,
		.vf_num = vf_num,
		.event = event,
		.queue = queue,
	};

	ice_devlink_health_report(pf->health_reporters.mdd, "MDD event", &ev);
}

/**
 * ice_fmsg_put_ptr - put hex value of pointer into fmsg
 *
 * @fmsg: devlink fmsg under construction
 * @name: name to pass
 * @ptr: 64 bit value to print as hex and put into fmsg
 */
static void ice_fmsg_put_ptr(struct devlink_fmsg *fmsg, const char *name,
			     void *ptr)
{
	char buf[sizeof(ptr) * 3];

	sprintf(buf, "%p", ptr);
	devlink_fmsg_put(fmsg, name, buf);
}

struct ice_tx_hang_event {
	u32 head;
	u32 intr;
	u16 vsi_num;
	u16 queue;
	u16 next_to_clean;
	u16 next_to_use;
	struct ice_tx_ring *tx_ring;
};

static int ice_tx_hang_reporter_dump(struct devlink_health_reporter *reporter,
				     struct devlink_fmsg *fmsg, void *priv_ctx,
				     struct netlink_ext_ack *extack)
{
	struct ice_tx_hang_event *event = priv_ctx;
	struct sk_buff *skb;

	if (!event)
		return 0;

	skb = event->tx_ring->tx_buf->skb;
	devlink_fmsg_obj_nest_start(fmsg);
	ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, event, head);
	ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, event, intr);
	ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, event, vsi_num);
	ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, event, queue);
	ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, event, next_to_clean);
	ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, event, next_to_use);
	devlink_fmsg_put(fmsg, "irq-mapping", event->tx_ring->q_vector->name);
	ice_fmsg_put_ptr(fmsg, "desc-ptr", event->tx_ring->desc);
	ice_fmsg_put_ptr(fmsg, "dma-ptr", (void *)(long)event->tx_ring->dma);
	ice_fmsg_put_ptr(fmsg, "skb-ptr", skb);
	devlink_fmsg_binary_pair_put(fmsg, "desc", event->tx_ring->desc,
				     event->tx_ring->count * sizeof(struct ice_tx_desc));
	devlink_fmsg_dump_skb(fmsg, skb);
	devlink_fmsg_obj_nest_end(fmsg);

	return 0;
}

void ice_prep_tx_hang_report(struct ice_pf *pf, struct ice_tx_ring *tx_ring,
			     u16 vsi_num, u32 head, u32 intr)
{
	struct ice_health_tx_hang_buf *buf = &pf->health_reporters.tx_hang_buf;

	buf->tx_ring = tx_ring;
	buf->vsi_num = vsi_num;
	buf->head = head;
	buf->intr = intr;
}

void ice_report_tx_hang(struct ice_pf *pf)
{
	struct ice_health_tx_hang_buf *buf = &pf->health_reporters.tx_hang_buf;
	struct ice_tx_ring *tx_ring = buf->tx_ring;

	struct ice_tx_hang_event ev = {
		.head = buf->head,
		.intr = buf->intr,
		.vsi_num = buf->vsi_num,
		.queue = tx_ring->q_index,
		.next_to_clean = tx_ring->next_to_clean,
		.next_to_use = tx_ring->next_to_use,
		.tx_ring = tx_ring,
	};

	ice_devlink_health_report(pf->health_reporters.tx_hang, "Tx hang", &ev);
}

static struct devlink_health_reporter *
ice_init_devlink_rep(struct ice_pf *pf,
		     const struct devlink_health_reporter_ops *ops)
{
	struct devlink *devlink = priv_to_devlink(pf);
	struct devlink_health_reporter *rep;
	const u64 graceful_period = 0;

	rep = devl_health_reporter_create(devlink, ops, graceful_period, pf);
	if (IS_ERR(rep)) {
		struct device *dev = ice_pf_to_dev(pf);

		dev_err(dev, "failed to create devlink %s health report er",
			ops->name);
		return NULL;
	}
	return rep;
}

#define ICE_DEFINE_HEALTH_REPORTER_OPS(_name) \
	static const struct devlink_health_reporter_ops ice_ ## _name ## _reporter_ops = { \
	.name = #_name, \
	.dump = ice_ ## _name ## _reporter_dump, \
}

ICE_DEFINE_HEALTH_REPORTER_OPS(mdd);
ICE_DEFINE_HEALTH_REPORTER_OPS(tx_hang);

/**
 * ice_health_init - allocate and init all ice devlink health reporters and
 * accompanied data
 *
 * @pf: PF struct
 */
void ice_health_init(struct ice_pf *pf)
{
	struct ice_health *reps = &pf->health_reporters;

	reps->mdd = ice_init_devlink_rep(pf, &ice_mdd_reporter_ops);
	reps->tx_hang = ice_init_devlink_rep(pf, &ice_tx_hang_reporter_ops);
}

/**
 * ice_deinit_devl_reporter - destroy given devlink health reporter
 * @reporter: reporter to destroy
 */
static void ice_deinit_devl_reporter(struct devlink_health_reporter *reporter)
{
	if (reporter)
		devl_health_reporter_destroy(reporter);
}

/**
 * ice_health_deinit - deallocate all ice devlink health reporters and
 * accompanied data
 *
 * @pf: PF struct
 */
void ice_health_deinit(struct ice_pf *pf)
{
	ice_deinit_devl_reporter(pf->health_reporters.mdd);
	ice_deinit_devl_reporter(pf->health_reporters.tx_hang);
}

static
void ice_health_assign_healthy_state(struct devlink_health_reporter *reporter)
{
	if (reporter)
		devlink_health_reporter_state_update(reporter,
						     DEVLINK_HEALTH_REPORTER_STATE_HEALTHY);
}

/**
 * ice_health_clear - clear devlink health issues after a reset
 * @pf: the PF device structure
 *
 * Mark the PF in healthy state again after a reset has completed.
 */
void ice_health_clear(struct ice_pf *pf)
{
	ice_health_assign_healthy_state(pf->health_reporters.mdd);
	ice_health_assign_healthy_state(pf->health_reporters.tx_hang);
}
