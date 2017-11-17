/*
 * Copyright 2014-2016 Freescale Semiconductor Inc.
 * Copyright 2016 NXP
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in the
 *	 documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *	 names of its contributors may be used to endorse or promote products
 *	 derived from this software without specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <linux/types.h>
#include "../../include/mc.h"
#include "../../include/dpaa2-io.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

#include "dpio.h"
#include "qbman-portal.h"

struct dpaa2_io {
	struct dpaa2_io_desc dpio_desc;
	struct qbman_swp_desc swp_desc;
	struct qbman_swp *swp;
	struct list_head node;
	/* protect against multiple management commands */
	spinlock_t lock_mgmt_cmd;
	/* protect notifications list */
	spinlock_t lock_notifications;
	struct list_head notifications;
};

struct dpaa2_io_store {
	unsigned int max;
	dma_addr_t paddr;
	struct dpaa2_dq *vaddr;
	void *alloced_addr;    /* unaligned value from kmalloc() */
	unsigned int idx;      /* position of the next-to-be-returned entry */
	struct qbman_swp *swp; /* portal used to issue VDQCR */
	struct device *dev;    /* device used for DMA mapping */
};

/* keep a per cpu array of DPIOs for fast access */
static struct dpaa2_io *dpio_by_cpu[NR_CPUS];
static struct list_head dpio_list = LIST_HEAD_INIT(dpio_list);
static DEFINE_SPINLOCK(dpio_list_lock);

static inline struct dpaa2_io *service_select_by_cpu(struct dpaa2_io *d,
						     int cpu)
{
	if (d)
		return d;

	if (cpu != DPAA2_IO_ANY_CPU && cpu >= num_possible_cpus())
		return NULL;

	/*
	 * If cpu == -1, choose the current cpu, with no guarantees about
	 * potentially being migrated away.
	 */
	if (unlikely(cpu < 0))
		cpu = smp_processor_id();

	/* If a specific cpu was requested, pick it up immediately */
	return dpio_by_cpu[cpu];
}

static inline struct dpaa2_io *service_select(struct dpaa2_io *d)
{
	if (d)
		return d;

	spin_lock(&dpio_list_lock);
	d = list_entry(dpio_list.next, struct dpaa2_io, node);
	list_del(&d->node);
	list_add_tail(&d->node, &dpio_list);
	spin_unlock(&dpio_list_lock);

	return d;
}

/**
 * dpaa2_io_create() - create a dpaa2_io object.
 * @desc: the dpaa2_io descriptor
 *
 * Activates a "struct dpaa2_io" corresponding to the given config of an actual
 * DPIO object.
 *
 * Return a valid dpaa2_io object for success, or NULL for failure.
 */
struct dpaa2_io *dpaa2_io_create(const struct dpaa2_io_desc *desc)
{
	struct dpaa2_io *obj = kmalloc(sizeof(*obj), GFP_KERNEL);

	if (!obj)
		return NULL;

	/* check if CPU is out of range (-1 means any cpu) */
	if (desc->cpu != DPAA2_IO_ANY_CPU && desc->cpu >= num_possible_cpus()) {
		kfree(obj);
		return NULL;
	}

	obj->dpio_desc = *desc;
	obj->swp_desc.cena_bar = obj->dpio_desc.regs_cena;
	obj->swp_desc.cinh_bar = obj->dpio_desc.regs_cinh;
	obj->swp_desc.qman_version = obj->dpio_desc.qman_version;
	obj->swp = qbman_swp_init(&obj->swp_desc);

	if (!obj->swp) {
		kfree(obj);
		return NULL;
	}

	INIT_LIST_HEAD(&obj->node);
	spin_lock_init(&obj->lock_mgmt_cmd);
	spin_lock_init(&obj->lock_notifications);
	INIT_LIST_HEAD(&obj->notifications);

	/* For now only enable DQRR interrupts */
	qbman_swp_interrupt_set_trigger(obj->swp,
					QBMAN_SWP_INTERRUPT_DQRI);
	qbman_swp_interrupt_clear_status(obj->swp, 0xffffffff);
	if (obj->dpio_desc.receives_notifications)
		qbman_swp_push_set(obj->swp, 0, 1);

	spin_lock(&dpio_list_lock);
	list_add_tail(&obj->node, &dpio_list);
	if (desc->cpu >= 0 && !dpio_by_cpu[desc->cpu])
		dpio_by_cpu[desc->cpu] = obj;
	spin_unlock(&dpio_list_lock);

	return obj;
}

/**
 * dpaa2_io_down() - release the dpaa2_io object.
 * @d: the dpaa2_io object to be released.
 *
 * The "struct dpaa2_io" type can represent an individual DPIO object (as
 * described by "struct dpaa2_io_desc") or an instance of a "DPIO service",
 * which can be used to group/encapsulate multiple DPIO objects. In all cases,
 * each handle obtained should be released using this function.
 */
void dpaa2_io_down(struct dpaa2_io *d)
{
	kfree(d);
}

#define DPAA_POLL_MAX 32

/**
 * dpaa2_io_irq() - ISR for DPIO interrupts
 *
 * @obj: the given DPIO object.
 *
 * Return IRQ_HANDLED for success or IRQ_NONE if there
 * were no pending interrupts.
 */
irqreturn_t dpaa2_io_irq(struct dpaa2_io *obj)
{
	const struct dpaa2_dq *dq;
	int max = 0;
	struct qbman_swp *swp;
	u32 status;

	swp = obj->swp;
	status = qbman_swp_interrupt_read_status(swp);
	if (!status)
		return IRQ_NONE;

	dq = qbman_swp_dqrr_next(swp);
	while (dq) {
		if (qbman_result_is_SCN(dq)) {
			struct dpaa2_io_notification_ctx *ctx;
			u64 q64;

			q64 = qbman_result_SCN_ctx(dq);
			ctx = (void *)q64;
			ctx->cb(ctx);
		} else {
			pr_crit("fsl-mc-dpio: Unrecognised/ignored DQRR entry\n");
		}
		qbman_swp_dqrr_consume(swp, dq);
		++max;
		if (max > DPAA_POLL_MAX)
			goto done;
		dq = qbman_swp_dqrr_next(swp);
	}
done:
	qbman_swp_interrupt_clear_status(swp, status);
	qbman_swp_interrupt_set_inhibit(swp, 0);
	return IRQ_HANDLED;
}

/**
 * dpaa2_io_service_register() - Prepare for servicing of FQDAN or CDAN
 *                               notifications on the given DPIO service.
 * @d:   the given DPIO service.
 * @ctx: the notification context.
 *
 * The caller should make the MC command to attach a DPAA2 object to
 * a DPIO after this function completes successfully.  In that way:
 *    (a) The DPIO service is "ready" to handle a notification arrival
 *        (which might happen before the "attach" command to MC has
 *        returned control of execution back to the caller)
 *    (b) The DPIO service can provide back to the caller the 'dpio_id' and
 *        'qman64' parameters that it should pass along in the MC command
 *        in order for the object to be configured to produce the right
 *        notification fields to the DPIO service.
 *
 * Return 0 for success, or -ENODEV for failure.
 */
int dpaa2_io_service_register(struct dpaa2_io *d,
			      struct dpaa2_io_notification_ctx *ctx)
{
	unsigned long irqflags;

	d = service_select_by_cpu(d, ctx->desired_cpu);
	if (!d)
		return -ENODEV;

	ctx->dpio_id = d->dpio_desc.dpio_id;
	ctx->qman64 = (u64)ctx;
	ctx->dpio_private = d;
	spin_lock_irqsave(&d->lock_notifications, irqflags);
	list_add(&ctx->node, &d->notifications);
	spin_unlock_irqrestore(&d->lock_notifications, irqflags);

	/* Enable the generation of CDAN notifications */
	if (ctx->is_cdan)
		return qbman_swp_CDAN_set_context_enable(d->swp,
							 (u16)ctx->id,
							 ctx->qman64);
	return 0;
}
EXPORT_SYMBOL(dpaa2_io_service_register);

/**
 * dpaa2_io_service_deregister - The opposite of 'register'.
 * @service: the given DPIO service.
 * @ctx: the notification context.
 *
 * This function should be called only after sending the MC command to
 * to detach the notification-producing device from the DPIO.
 */
void dpaa2_io_service_deregister(struct dpaa2_io *service,
				 struct dpaa2_io_notification_ctx *ctx)
{
	struct dpaa2_io *d = ctx->dpio_private;
	unsigned long irqflags;

	if (ctx->is_cdan)
		qbman_swp_CDAN_disable(d->swp, (u16)ctx->id);

	spin_lock_irqsave(&d->lock_notifications, irqflags);
	list_del(&ctx->node);
	spin_unlock_irqrestore(&d->lock_notifications, irqflags);
}
EXPORT_SYMBOL(dpaa2_io_service_deregister);

/**
 * dpaa2_io_service_rearm() - Rearm the notification for the given DPIO service.
 * @d: the given DPIO service.
 * @ctx: the notification context.
 *
 * Once a FQDAN/CDAN has been produced, the corresponding FQ/channel is
 * considered "disarmed". Ie. the user can issue pull dequeue operations on that
 * traffic source for as long as it likes. Eventually it may wish to "rearm"
 * that source to allow it to produce another FQDAN/CDAN, that's what this
 * function achieves.
 *
 * Return 0 for success.
 */
int dpaa2_io_service_rearm(struct dpaa2_io *d,
			   struct dpaa2_io_notification_ctx *ctx)
{
	unsigned long irqflags;
	int err;

	d = service_select_by_cpu(d, ctx->desired_cpu);
	if (!unlikely(d))
		return -ENODEV;

	spin_lock_irqsave(&d->lock_mgmt_cmd, irqflags);
	if (ctx->is_cdan)
		err = qbman_swp_CDAN_enable(d->swp, (u16)ctx->id);
	else
		err = qbman_swp_fq_schedule(d->swp, ctx->id);
	spin_unlock_irqrestore(&d->lock_mgmt_cmd, irqflags);

	return err;
}
EXPORT_SYMBOL(dpaa2_io_service_rearm);

/**
 * dpaa2_io_service_pull_channel() - pull dequeue functions from a channel.
 * @d: the given DPIO service.
 * @channelid: the given channel id.
 * @s: the dpaa2_io_store object for the result.
 *
 * Return 0 for success, or error code for failure.
 */
int dpaa2_io_service_pull_channel(struct dpaa2_io *d, u32 channelid,
				  struct dpaa2_io_store *s)
{
	struct qbman_pull_desc pd;
	int err;

	qbman_pull_desc_clear(&pd);
	qbman_pull_desc_set_storage(&pd, s->vaddr, s->paddr, 1);
	qbman_pull_desc_set_numframes(&pd, (u8)s->max);
	qbman_pull_desc_set_channel(&pd, channelid, qbman_pull_type_prio);

	d = service_select(d);
	if (!d)
		return -ENODEV;

	s->swp = d->swp;
	err = qbman_swp_pull(d->swp, &pd);
	if (err)
		s->swp = NULL;

	return err;
}
EXPORT_SYMBOL(dpaa2_io_service_pull_channel);

/**
 * dpaa2_io_service_enqueue_qd() - Enqueue a frame to a QD.
 * @d: the given DPIO service.
 * @qdid: the given queuing destination id.
 * @prio: the given queuing priority.
 * @qdbin: the given queuing destination bin.
 * @fd: the frame descriptor which is enqueued.
 *
 * Return 0 for successful enqueue, or -EBUSY if the enqueue ring is not ready,
 * or -ENODEV if there is no dpio service.
 */
int dpaa2_io_service_enqueue_qd(struct dpaa2_io *d,
				u32 qdid, u8 prio, u16 qdbin,
				const struct dpaa2_fd *fd)
{
	struct qbman_eq_desc ed;

	d = service_select(d);
	if (!d)
		return -ENODEV;

	qbman_eq_desc_clear(&ed);
	qbman_eq_desc_set_no_orp(&ed, 0);
	qbman_eq_desc_set_qd(&ed, qdid, qdbin, prio);

	return qbman_swp_enqueue(d->swp, &ed, fd);
}
EXPORT_SYMBOL(dpaa2_io_service_enqueue_qd);

/**
 * dpaa2_io_service_release() - Release buffers to a buffer pool.
 * @d: the given DPIO object.
 * @bpid: the buffer pool id.
 * @buffers: the buffers to be released.
 * @num_buffers: the number of the buffers to be released.
 *
 * Return 0 for success, and negative error code for failure.
 */
int dpaa2_io_service_release(struct dpaa2_io *d,
			     u32 bpid,
			     const u64 *buffers,
			     unsigned int num_buffers)
{
	struct qbman_release_desc rd;

	d = service_select(d);
	if (!d)
		return -ENODEV;

	qbman_release_desc_clear(&rd);
	qbman_release_desc_set_bpid(&rd, bpid);

	return qbman_swp_release(d->swp, &rd, buffers, num_buffers);
}
EXPORT_SYMBOL(dpaa2_io_service_release);

/**
 * dpaa2_io_service_acquire() - Acquire buffers from a buffer pool.
 * @d: the given DPIO object.
 * @bpid: the buffer pool id.
 * @buffers: the buffer addresses for acquired buffers.
 * @num_buffers: the expected number of the buffers to acquire.
 *
 * Return a negative error code if the command failed, otherwise it returns
 * the number of buffers acquired, which may be less than the number requested.
 * Eg. if the buffer pool is empty, this will return zero.
 */
int dpaa2_io_service_acquire(struct dpaa2_io *d,
			     u32 bpid,
			     u64 *buffers,
			     unsigned int num_buffers)
{
	unsigned long irqflags;
	int err;

	d = service_select(d);
	if (!d)
		return -ENODEV;

	spin_lock_irqsave(&d->lock_mgmt_cmd, irqflags);
	err = qbman_swp_acquire(d->swp, bpid, buffers, num_buffers);
	spin_unlock_irqrestore(&d->lock_mgmt_cmd, irqflags);

	return err;
}
EXPORT_SYMBOL(dpaa2_io_service_acquire);

/*
 * 'Stores' are reusable memory blocks for holding dequeue results, and to
 * assist with parsing those results.
 */

/**
 * dpaa2_io_store_create() - Create the dma memory storage for dequeue result.
 * @max_frames: the maximum number of dequeued result for frames, must be <= 16.
 * @dev:        the device to allow mapping/unmapping the DMAable region.
 *
 * The size of the storage is "max_frames*sizeof(struct dpaa2_dq)".
 * The 'dpaa2_io_store' returned is a DPIO service managed object.
 *
 * Return pointer to dpaa2_io_store struct for successfully created storage
 * memory, or NULL on error.
 */
struct dpaa2_io_store *dpaa2_io_store_create(unsigned int max_frames,
					     struct device *dev)
{
	struct dpaa2_io_store *ret;
	size_t size;

	if (!max_frames || (max_frames > 16))
		return NULL;

	ret = kmalloc(sizeof(*ret), GFP_KERNEL);
	if (!ret)
		return NULL;

	ret->max = max_frames;
	size = max_frames * sizeof(struct dpaa2_dq) + 64;
	ret->alloced_addr = kzalloc(size, GFP_KERNEL);
	if (!ret->alloced_addr) {
		kfree(ret);
		return NULL;
	}

	ret->vaddr = PTR_ALIGN(ret->alloced_addr, 64);
	ret->paddr = dma_map_single(dev, ret->vaddr,
				    sizeof(struct dpaa2_dq) * max_frames,
				    DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, ret->paddr)) {
		kfree(ret->alloced_addr);
		kfree(ret);
		return NULL;
	}

	ret->idx = 0;
	ret->dev = dev;

	return ret;
}
EXPORT_SYMBOL(dpaa2_io_store_create);

/**
 * dpaa2_io_store_destroy() - Frees the dma memory storage for dequeue
 *                            result.
 * @s: the storage memory to be destroyed.
 */
void dpaa2_io_store_destroy(struct dpaa2_io_store *s)
{
	dma_unmap_single(s->dev, s->paddr, sizeof(struct dpaa2_dq) * s->max,
			 DMA_FROM_DEVICE);
	kfree(s->alloced_addr);
	kfree(s);
}
EXPORT_SYMBOL(dpaa2_io_store_destroy);

/**
 * dpaa2_io_store_next() - Determine when the next dequeue result is available.
 * @s: the dpaa2_io_store object.
 * @is_last: indicate whether this is the last frame in the pull command.
 *
 * When an object driver performs dequeues to a dpaa2_io_store, this function
 * can be used to determine when the next frame result is available. Once
 * this function returns non-NULL, a subsequent call to it will try to find
 * the next dequeue result.
 *
 * Note that if a pull-dequeue has a NULL result because the target FQ/channel
 * was empty, then this function will also return NULL (rather than expecting
 * the caller to always check for this. As such, "is_last" can be used to
 * differentiate between "end-of-empty-dequeue" and "still-waiting".
 *
 * Return dequeue result for a valid dequeue result, or NULL for empty dequeue.
 */
struct dpaa2_dq *dpaa2_io_store_next(struct dpaa2_io_store *s, int *is_last)
{
	int match;
	struct dpaa2_dq *ret = &s->vaddr[s->idx];

	match = qbman_result_has_new_result(s->swp, ret);
	if (!match) {
		*is_last = 0;
		return NULL;
	}

	s->idx++;

	if (dpaa2_dq_is_pull_complete(ret)) {
		*is_last = 1;
		s->idx = 0;
		/*
		 * If we get an empty dequeue result to terminate a zero-results
		 * vdqcr, return NULL to the caller rather than expecting him to
		 * check non-NULL results every time.
		 */
		if (!(dpaa2_dq_flags(ret) & DPAA2_DQ_STAT_VALIDFRAME))
			ret = NULL;
	} else {
		*is_last = 0;
	}

	return ret;
}
EXPORT_SYMBOL(dpaa2_io_store_next);
