// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include "hab.h"

struct virtual_channel *
hab_vchan_alloc(struct uhab_context *ctx, struct physical_channel *pchan,
				int openid)
{
	int id;
	struct virtual_channel *vchan;

	if (!pchan || !ctx)
		return NULL;

	vchan = kzalloc(sizeof(*vchan), GFP_KERNEL);
	if (!vchan)
		return NULL;

	/* This should be the first thing we do in this function */
	idr_preload(GFP_KERNEL);
	spin_lock_bh(&pchan->vid_lock);
	id = idr_alloc(&pchan->vchan_idr, vchan, 1,
		(HAB_VCID_ID_MASK >> HAB_VCID_ID_SHIFT) + 1, GFP_NOWAIT);
	spin_unlock_bh(&pchan->vid_lock);
	idr_preload_end();

	if (id <= 0) {
		pr_err("idr failed %d\n", id);
		kfree(vchan);
		return NULL;
	}
	mb(); /* id must be generated done before pchan_get */

	hab_pchan_get(pchan);
	vchan->pchan = pchan;
	/* vchan need both vcid and openid to be properly located */
	vchan->session_id = openid;
	write_lock(&pchan->vchans_lock);
	list_add_tail(&vchan->pnode, &pchan->vchannels);
	pchan->vcnt++;
	write_unlock(&pchan->vchans_lock);
	vchan->id = ((id << HAB_VCID_ID_SHIFT) & HAB_VCID_ID_MASK) |
		((pchan->habdev->id << HAB_VCID_MMID_SHIFT) &
			HAB_VCID_MMID_MASK) |
		((pchan->dom_id << HAB_VCID_DOMID_SHIFT) &
			HAB_VCID_DOMID_MASK);
	spin_lock_init(&vchan->rx_lock);
	INIT_LIST_HEAD(&vchan->rx_list);
	init_waitqueue_head(&vchan->rx_queue);

	kref_init(&vchan->refcount);

	vchan->otherend_closed = pchan->closed;

	hab_ctx_get(ctx);
	vchan->ctx = ctx;

	return vchan;
}

static void
hab_vchan_free(struct kref *ref)
{
	struct virtual_channel *vchan =
		container_of(ref, struct virtual_channel, refcount);
	struct hab_message *message, *msg_tmp;
	struct physical_channel *pchan = vchan->pchan;
	struct uhab_context *ctx = vchan->ctx;
	struct virtual_channel *vc, *vc_tmp;
	int irqs_disabled = irqs_disabled();

	hab_spin_lock(&vchan->rx_lock, irqs_disabled);
	list_for_each_entry_safe(message, msg_tmp, &vchan->rx_list, node) {
		list_del(&message->node);
		hab_msg_free(message);
	}
	hab_spin_unlock(&vchan->rx_lock, irqs_disabled);

	/* release vchan from pchan. no more msg for this vchan */
	hab_write_lock(&pchan->vchans_lock, irqs_disabled);
	list_for_each_entry_safe(vc, vc_tmp, &pchan->vchannels, pnode) {
		if (vchan == vc) {
			list_del(&vc->pnode);
			/* the ref is held in case of pchan is freed */
			pchan->vcnt--;
			break;
		}
	}
	hab_write_unlock(&pchan->vchans_lock, irqs_disabled);

	/* the release vchan from ctx was done earlier in vchan close() */
	hab_ctx_put(ctx); /* now ctx is not needed from this vchan's view */

	/* release idr at the last so same idr will not be used early */
	hab_spin_lock(&pchan->vid_lock, irqs_disabled);
	idr_remove(&pchan->vchan_idr, HAB_VCID_GET_ID(vchan->id));
	hab_spin_unlock(&pchan->vid_lock, irqs_disabled);

	hab_pchan_put(pchan); /* no more need for pchan from this vchan */

	kfree(vchan);
}

/*
 * only for msg recv path to retrieve vchan from vcid and openid based on
 * pchan's vchan list
 */
struct virtual_channel*
hab_vchan_get(struct physical_channel *pchan, struct hab_header *header)
{
	struct virtual_channel *vchan;
	uint32_t vchan_id = HAB_HEADER_GET_ID(*header);
	uint32_t session_id = HAB_HEADER_GET_SESSION_ID(*header);
	size_t sizebytes = HAB_HEADER_GET_SIZE(*header);
	uint32_t payload_type = HAB_HEADER_GET_TYPE(*header);
	int irqs_disabled = irqs_disabled();

	hab_spin_lock(&pchan->vid_lock, irqs_disabled);
	vchan = idr_find(&pchan->vchan_idr, HAB_VCID_GET_ID(vchan_id));
	if (vchan) {
		if (vchan->session_id != session_id)
			/*
			 * skipped if session is different even vcid
			 * is the same
			 */
			vchan = NULL;
		else if (!vchan->otherend_id /*&& !vchan->session_id*/) {
			/*
			 * not paired vchan can be fetched right after it is
			 * alloc'ed. so it has to be skipped during search
			 * for remote msg
			 */
			pr_warn("vcid %x is not paired yet session %d refcnt %d type %d sz %zd\n",
				vchan->id, vchan->otherend_id,
				get_refcnt(vchan->refcount),
				payload_type, sizebytes);
			vchan = NULL;
		} else if (vchan->otherend_closed || vchan->closed) {
			pr_debug("closed already remote %d local %d vcid %x remote %x session %d refcnt %d header %x session %d type %d sz %zd\n",
				vchan->otherend_closed, vchan->closed,
				vchan->id, vchan->otherend_id,
				vchan->session_id, get_refcnt(vchan->refcount),
				vchan_id, session_id, payload_type, sizebytes);
			vchan = NULL;
		} else if (!kref_get_unless_zero(&vchan->refcount)) {
			/*
			 * this happens when refcnt is already zero
			 * (put from other thread) or there is an actual error
			 */
			pr_err("failed to inc vcid %pK %x remote %x session %d refcnt %d header %x session %d type %d sz %zd\n",
				vchan, vchan->id, vchan->otherend_id,
				vchan->session_id, get_refcnt(vchan->refcount),
				vchan_id, session_id, payload_type, sizebytes);
			vchan = NULL;
		}
	}
	hab_spin_unlock(&pchan->vid_lock, irqs_disabled);

	return vchan;
}

/* wake up local waiting Q, so stop-vchan can be processed */
void hab_vchan_stop(struct virtual_channel *vchan)
{
	if (vchan) {
		vchan->otherend_closed = 1;
		wake_up(&vchan->rx_queue);
		if (vchan->ctx)
			if (vchan->pchan->mem_proto == 1)
				wake_up_interruptible(&vchan->ctx->imp_wq);
			else
				wake_up_interruptible(&vchan->ctx->exp_wq);
		else
			pr_err("NULL ctx for vchan %x\n", vchan->id);
	}
}

void hab_vchans_stop(struct physical_channel *pchan)
{
	struct virtual_channel *vchan, *tmp;

	read_lock(&pchan->vchans_lock);
	list_for_each_entry_safe(vchan, tmp, &pchan->vchannels, pnode) {
		hab_vchan_stop(vchan);
	}
	read_unlock(&pchan->vchans_lock);
}

/* send vchan close to remote and stop receiving anything locally */
void hab_vchan_stop_notify(struct virtual_channel *vchan)
{
	hab_send_close_msg(vchan);
	hab_vchan_stop(vchan);
}

static int hab_vchans_per_pchan_empty(struct physical_channel *pchan)
{
	int empty;

	read_lock(&pchan->vchans_lock);
	empty = list_empty(&pchan->vchannels);
	if (!empty) {
		struct virtual_channel *vchan;
		int vcnt = pchan->vcnt;

		list_for_each_entry(vchan, &pchan->vchannels, pnode) {
			/* discount open-pending unpaired vchan */
			if (!vchan->session_id)
				vcnt--;
			else
				pr_err("vchan %pK %x rm %x sn %d rf %d clsd %d rm clsd %d\n",
					vchan, vchan->id,
					vchan->otherend_id,
					vchan->session_id,
					get_refcnt(vchan->refcount),
					vchan->closed, vchan->otherend_closed);
		}
		if (!vcnt)
			empty = 1;/* unpaired vchan can exist at init time */
	}
	read_unlock(&pchan->vchans_lock);

	return empty;
}

static int hab_vchans_empty(int vmid)
{
	int i, empty = 1;
	struct physical_channel *pchan;
	struct hab_device *hab_dev;

	for (i = 0; i < hab_driver.ndevices; i++) {
		hab_dev = &hab_driver.devp[i];

		read_lock_bh(&hab_dev->pchan_lock);
		list_for_each_entry(pchan, &hab_dev->pchannels, node) {
			if (pchan->vmid_remote == vmid) {
				if (!hab_vchans_per_pchan_empty(pchan)) {
					empty = 0;
					pr_info("vmid %d %s's vchans are not closed\n",
							vmid, pchan->name);
					break;
				}
			}
		}
		read_unlock_bh(&hab_dev->pchan_lock);
	}

	return empty;
}

/*
 * block until all vchans of a given GVM are explicitly closed
 * with habmm_socket_close() by hab clients themselves
 */
void hab_vchans_empty_wait(int vmid)
{
	pr_info("waiting for GVM%d's sockets closure\n", vmid);

	while (!hab_vchans_empty(vmid))
		usleep_range(10000, 12000);

	pr_info("all of GVM%d's sockets are closed\n", vmid);
}

int hab_vchan_find_domid(struct virtual_channel *vchan)
{
	return vchan ? vchan->pchan->dom_id : -1;
}

void hab_vchan_put(struct virtual_channel *vchan)
{
	if (vchan)
		kref_put(&vchan->refcount, hab_vchan_free);
}

int hab_vchan_query(struct uhab_context *ctx, int32_t vcid, uint64_t *ids,
			   char *names, size_t name_size, uint32_t flags)
{
	struct virtual_channel *vchan;

	vchan = hab_get_vchan_fromvcid(vcid, ctx, 1);
	if (!vchan)
		return -EINVAL;

	if (vchan->otherend_closed) {
		hab_vchan_put(vchan);
		return -ENODEV;
	}

	*ids = vchan->pchan->vmid_local |
		((uint64_t)vchan->pchan->vmid_remote) << 32;
	names[0] = 0;
	names[name_size/2] = 0;

	hab_vchan_put(vchan);

	return 0;
}
