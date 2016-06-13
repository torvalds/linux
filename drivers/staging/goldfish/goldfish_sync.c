/*
 * Copyright (C) 2016 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <linux/interrupt.h>
#include <linux/kref.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <linux/io.h>
#include <linux/mm.h>
#include <linux/acpi.h>

#include <linux/string.h>
#include <linux/syscalls.h>

#include "sw_sync.h"
#include "sync.h"

#define ERR(...) printk(KERN_ERR __VA_ARGS__);

#define INFO(...) printk(KERN_INFO __VA_ARGS__);

#define DPRINT(...) pr_debug(__VA_ARGS__);

#define DTRACE() DPRINT("%s: enter", __func__)

/* The Goldfish sync driver is designed to provide a interface
 * between the underlying host's sync device and the kernel's
 * sw_sync.
 * The purpose of the device/driver is to enable lightweight
 * creation and signaling of timelines and fences
 * in order to synchronize the guest with host-side graphics events.
 *
 * Each time the interrupt trips, the driver
 * may perform a sw_sync operation.
 */

/* The operations are: */

/* Ready signal - used to mark when irq should lower */
#define CMD_SYNC_READY            0

/* Create a new timeline. writes timeline handle */
#define CMD_CREATE_SYNC_TIMELINE  1

/* Create a fence object. reads timeline handle and time argument.
 * Writes fence fd to the SYNC_REG_HANDLE register. */
#define CMD_CREATE_SYNC_FENCE     2

/* Increments timeline. reads timeline handle and time argument */
#define CMD_SYNC_TIMELINE_INC     3

/* Destroys a timeline. reads timeline handle */
#define CMD_DESTROY_SYNC_TIMELINE 4

/* Starts a wait on the host with
 * the given glsync object and sync thread handle. */
#define CMD_TRIGGER_HOST_WAIT     5

/* The register layout is: */

#define SYNC_REG_BATCH_COMMAND                0x00 /* host->guest batch commands */
#define SYNC_REG_BATCH_GUESTCOMMAND           0x04 /* guest->host batch commands */
#define SYNC_REG_BATCH_COMMAND_ADDR           0x08 /* communicate physical address of host->guest batch commands */
#define SYNC_REG_BATCH_COMMAND_ADDR_HIGH      0x0c /* 64-bit part */
#define SYNC_REG_BATCH_GUESTCOMMAND_ADDR      0x10 /* communicate physical address of guest->host commands */
#define SYNC_REG_BATCH_GUESTCOMMAND_ADDR_HIGH 0x14 /* 64-bit part */
#define SYNC_REG_INIT                         0x18 /* signals that the device has been probed */

/* There is an ioctl associated with goldfish sync driver.
 * Make it conflict with ioctls that are not likely to be used
 * in the emulator.
 *
 * '@'	00-0F	linux/radeonfb.h	conflict!
 * '@'	00-0F	drivers/video/aty/aty128fb.c	conflict!
 */
#define GOLDFISH_SYNC_IOC_MAGIC	'@'

#define GOLDFISH_SYNC_IOC_QUEUE_WORK	_IOWR(GOLDFISH_SYNC_IOC_MAGIC, 0, struct goldfish_sync_ioctl_info)

/* The above definitions (command codes, register layout, ioctl definitions)
 * need to be in sync with the following files:
 *
 * Host-side (emulator):
 * external/qemu/android/emulation/goldfish_sync.h
 * external/qemu-android/hw/misc/goldfish_sync.c
 *
 * Guest-side (system image):
 * device/generic/goldfish-opengl/system/egl/goldfish_sync.h
 * device/generic/goldfish/ueventd.ranchu.rc
 * platform/build/target/board/generic/sepolicy/file_contexts
 */
struct goldfish_sync_hostcmd {
	/* sorted for alignment */
	uint64_t handle;
	uint64_t hostcmd_handle;
	uint32_t cmd;
	uint32_t time_arg;
};

struct goldfish_sync_guestcmd {
	uint64_t host_command; /* uint64_t for alignment */
	uint64_t glsync_handle;
	uint64_t thread_handle;
	uint64_t guest_timeline_handle;
};

#define GOLDFISH_SYNC_MAX_CMDS 64

struct goldfish_sync_state {
	char __iomem *reg_base;
	int irq;

	/* Spinlock protects |to_do| / |to_do_end|. */
	spinlock_t lock;
	/* |mutex_lock| protects all concurrent access
	 * to timelines for both kernel and user space. */
	struct mutex mutex_lock;

	/* Buffer holding commands issued from host. */
	struct goldfish_sync_hostcmd to_do[GOLDFISH_SYNC_MAX_CMDS];
	uint32_t to_do_end;

	/* Addresses for the reading or writing
	 * of individual commands. The host can directly write
	 * to |batch_hostcmd| (and then this driver immediately
	 * copies contents to |to_do|). This driver either replies
	 * through |batch_hostcmd| or simply issues a
	 * guest->host command through |batch_guestcmd|.
	 */
	struct goldfish_sync_hostcmd *batch_hostcmd;
	struct goldfish_sync_guestcmd *batch_guestcmd;

	/* Used to give this struct itself to a work queue
	 * function for executing actual sync commands. */
	struct work_struct work_item;
};

static struct goldfish_sync_state global_sync_state[1];

struct goldfish_sync_timeline_obj {
	struct sw_sync_timeline *sw_sync_tl;
	uint32_t current_time;
	/* We need to be careful about when we deallocate
	 * this |goldfish_sync_timeline_obj| struct.
	 * In order to ensure proper cleanup, we need to
	 * consider the triggered host-side wait that may
	 * still be in flight when the guest close()'s a
	 * goldfish_sync device's sync context fd (and
	 * destroys the |sw_sync_tl| field above).
	 * The host-side wait may raise IRQ
	 * and tell the kernel to increment the timeline _after_
	 * the |sw_sync_tl| has already been set to null.
	 *
	 * From observations on OpenGL apps and CTS tests, this
	 * happens at some very low probability upon context
	 * destruction or process close, but it does happen
	 * and it needs to be handled properly. Otherwise,
	 * if we clean up the surrounding |goldfish_sync_timeline_obj|
	 * too early, any |handle| field of any host->guest command
	 * might not even point to a null |sw_sync_tl| field,
	 * but to garbage memory or even a reclaimed |sw_sync_tl|.
	 * If we do not count such "pending waits" and kfree the object
	 * immediately upon |goldfish_sync_timeline_destroy|,
	 * we might get mysterous RCU stalls after running a long
	 * time because the garbage memory that is being read
	 * happens to be interpretable as a |spinlock_t| struct
	 * that is currently in the locked state.
	 *
	 * To track when to free the |goldfish_sync_timeline_obj|
	 * itself, we maintain a kref.
	 * The kref essentially counts the timeline itself plus
	 * the number of waits in flight. kref_init/kref_put
	 * are issued on
	 * |goldfish_sync_timeline_create|/|goldfish_sync_timeline_destroy|
	 * and kref_get/kref_put are issued on
	 * |goldfish_sync_fence_create|/|goldfish_sync_timeline_inc|.
	 *
	 * The timeline is destroyed after reference count
	 * reaches zero, which would happen after
	 * |goldfish_sync_timeline_destroy| and all pending
	 * |goldfish_sync_timeline_inc|'s are fulfilled.
	 *
	 * NOTE (1): We assume that |fence_create| and
	 * |timeline_inc| calls are 1:1, otherwise the kref scheme
	 * will not work. This is a valid assumption as long
	 * as the host-side virtual device implementation
	 * does not insert any timeline increments
	 * that we did not trigger from here.
	 *
	 * NOTE (2): The use of kref by itself requires no locks,
	 * but this does not mean everything works without locks.
	 * Related timeline operations do require a lock of some sort,
	 * or at least are not proven to work without it.
	 * In particualr, we assume that all the operations
	 * done on the |kref| field above are done in contexts where
	 * |global_sync_state->mutex_lock| is held. Do not
	 * remove that lock until everything is proven to work
	 * without it!!! */
	struct kref kref;
};

/* We will call |delete_timeline_obj| when the last reference count
 * of the kref is decremented. This deletes the sw_sync
 * timeline object along with the wrapper itself. */
static void delete_timeline_obj(struct kref* kref) {
	struct goldfish_sync_timeline_obj* obj =
		container_of(kref, struct goldfish_sync_timeline_obj, kref);

	sync_timeline_destroy(&obj->sw_sync_tl->obj);
	obj->sw_sync_tl = NULL;
	kfree(obj);
}

static uint64_t gensym_ctr;
static void gensym(char *dst)
{
	sprintf(dst, "goldfish_sync:gensym:%llu", gensym_ctr);
	gensym_ctr++;
}

/* |goldfish_sync_timeline_create| assumes that |global_sync_state->mutex_lock|
 * is held. */
static struct goldfish_sync_timeline_obj*
goldfish_sync_timeline_create(void)
{

	char timeline_name[256];
	struct sw_sync_timeline *res_sync_tl = NULL;
	struct goldfish_sync_timeline_obj *res;

	DTRACE();

	gensym(timeline_name);

	res_sync_tl = sw_sync_timeline_create(timeline_name);
	if (!res_sync_tl) {
		ERR("Failed to create sw_sync timeline.");
		return NULL;
	}

	res = kzalloc(sizeof(struct goldfish_sync_timeline_obj), GFP_KERNEL);
	res->sw_sync_tl = res_sync_tl;
	res->current_time = 0;
	kref_init(&res->kref);

	DPRINT("new timeline_obj=0x%p", res);
	return res;
}

/* |goldfish_sync_fence_create| assumes that |global_sync_state->mutex_lock|
 * is held. */
static int
goldfish_sync_fence_create(struct goldfish_sync_timeline_obj *obj,
							uint32_t val)
{

	int fd;
	char fence_name[256];
	struct sync_pt *syncpt = NULL;
	struct sync_fence *sync_obj = NULL;
	struct sw_sync_timeline *tl;

	DTRACE();

	if (!obj) return -1;

	tl = obj->sw_sync_tl;

	syncpt = sw_sync_pt_create(tl, val);
	if (!syncpt) {
		ERR("could not create sync point! "
			"sync_timeline=0x%p val=%d",
			   tl, val);
		return -1;
	}

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		ERR("could not get unused fd for sync fence. "
			"errno=%d", fd);
		goto err_cleanup_pt;
	}

	gensym(fence_name);

	sync_obj = sync_fence_create(fence_name, syncpt);
	if (!sync_obj) {
		ERR("could not create sync fence! "
			"sync_timeline=0x%p val=%d sync_pt=0x%p",
			   tl, val, syncpt);
		goto err_cleanup_fd_pt;
	}

	DPRINT("installing sync fence into fd %d sync_obj=0x%p", fd, sync_obj);
	sync_fence_install(sync_obj, fd);
	kref_get(&obj->kref);

	return fd;

err_cleanup_fd_pt:
	put_unused_fd(fd);
err_cleanup_pt:
	sync_pt_free(syncpt);
	return -1;
}

/* |goldfish_sync_timeline_inc| assumes that |global_sync_state->mutex_lock|
 * is held. */
static void
goldfish_sync_timeline_inc(struct goldfish_sync_timeline_obj *obj, uint32_t inc)
{
	DTRACE();
	/* Just give up if someone else nuked the timeline.
	 * Whoever it was won't care that it doesn't get signaled. */
	if (!obj) return;

	DPRINT("timeline_obj=0x%p", obj);
	sw_sync_timeline_inc(obj->sw_sync_tl, inc);
	DPRINT("incremented timeline. increment max_time");
	obj->current_time += inc;

	/* Here, we will end up deleting the timeline object if it
	 * turns out that this call was a pending increment after
	 * |goldfish_sync_timeline_destroy| was called. */
	kref_put(&obj->kref, delete_timeline_obj);
	DPRINT("done");
}

/* |goldfish_sync_timeline_destroy| assumes
 * that |global_sync_state->mutex_lock| is held. */
static void
goldfish_sync_timeline_destroy(struct goldfish_sync_timeline_obj *obj)
{
	DTRACE();
	/* See description of |goldfish_sync_timeline_obj| for why we
	 * should not immediately destroy |obj| */
	kref_put(&obj->kref, delete_timeline_obj);
}

static inline void
goldfish_sync_cmd_queue(struct goldfish_sync_state *sync_state,
						uint32_t cmd,
						uint64_t handle,
						uint32_t time_arg,
						uint64_t hostcmd_handle)
{
	struct goldfish_sync_hostcmd *to_add;

	DTRACE();

	BUG_ON(sync_state->to_do_end == GOLDFISH_SYNC_MAX_CMDS);

	to_add = &sync_state->to_do[sync_state->to_do_end];

	to_add->cmd = cmd;
	to_add->handle = handle;
	to_add->time_arg = time_arg;
	to_add->hostcmd_handle = hostcmd_handle;

	sync_state->to_do_end += 1;
}

static inline void
goldfish_sync_hostcmd_reply(struct goldfish_sync_state *sync_state,
							uint32_t cmd,
							uint64_t handle,
							uint32_t time_arg,
							uint64_t hostcmd_handle)
{
	unsigned long irq_flags;
	struct goldfish_sync_hostcmd *batch_hostcmd =
		sync_state->batch_hostcmd;

	DTRACE();

	spin_lock_irqsave(&sync_state->lock, irq_flags);

	batch_hostcmd->cmd = cmd;
	batch_hostcmd->handle = handle;
	batch_hostcmd->time_arg = time_arg;
	batch_hostcmd->hostcmd_handle = hostcmd_handle;
	writel(0, sync_state->reg_base + SYNC_REG_BATCH_COMMAND);

	spin_unlock_irqrestore(&sync_state->lock, irq_flags);
}

static inline void
goldfish_sync_send_guestcmd(struct goldfish_sync_state *sync_state,
							uint32_t cmd,
							uint64_t glsync_handle,
							uint64_t thread_handle,
							uint64_t timeline_handle)
{
	unsigned long irq_flags;
	struct goldfish_sync_guestcmd *batch_guestcmd =
		sync_state->batch_guestcmd;

	DTRACE();

	spin_lock_irqsave(&sync_state->lock, irq_flags);

	batch_guestcmd->host_command = (uint64_t)cmd;
	batch_guestcmd->glsync_handle = (uint64_t)glsync_handle;
	batch_guestcmd->thread_handle = (uint64_t)thread_handle;
	batch_guestcmd->guest_timeline_handle = (uint64_t)timeline_handle;
	writel(0, sync_state->reg_base + SYNC_REG_BATCH_GUESTCOMMAND);

	spin_unlock_irqrestore(&sync_state->lock, irq_flags);
}

/* |goldfish_sync_interrupt| handles IRQ raises from the virtual device.
 * In the context of OpenGL, this interrupt will fire whenever we need
 * to signal a fence fd in the guest, with the command
 * |CMD_SYNC_TIMELINE_INC|.
 * However, because this function will be called in an interrupt context,
 * it is necessary to do the actual work of signaling off of interrupt context.
 * The shared work queue is used for this purpose. At the end when
 * all pending commands are intercepted by the interrupt handler,
 * we call |schedule_work|, which will later run the actual
 * desired sync command in |goldfish_sync_work_item_fn|.
 */
static irqreturn_t goldfish_sync_interrupt(int irq, void *dev_id)
{

	struct goldfish_sync_state *sync_state = dev_id;

	uint32_t nextcmd;
	uint32_t command_r;
	uint64_t handle_rw;
	uint32_t time_r;
	uint64_t hostcmd_handle_rw;

	int count = 0;

	DTRACE();

	sync_state = dev_id;

	spin_lock(&sync_state->lock);

	for (;;) {

		readl(sync_state->reg_base + SYNC_REG_BATCH_COMMAND);
		nextcmd = sync_state->batch_hostcmd->cmd;

		if (nextcmd == 0)
			break;

		command_r = nextcmd;
		handle_rw = sync_state->batch_hostcmd->handle;
		time_r = sync_state->batch_hostcmd->time_arg;
		hostcmd_handle_rw = sync_state->batch_hostcmd->hostcmd_handle;

		goldfish_sync_cmd_queue(
				sync_state,
				command_r,
				handle_rw,
				time_r,
				hostcmd_handle_rw);

		count++;
	}

	spin_unlock(&sync_state->lock);

	schedule_work(&sync_state->work_item);

	return (count == 0) ? IRQ_NONE : IRQ_HANDLED;
}

/* |goldfish_sync_work_item_fn| does the actual work of servicing
 * host->guest sync commands. This function is triggered whenever
 * the IRQ for the goldfish sync device is raised. Once it starts
 * running, it grabs the contents of the buffer containing the
 * commands it needs to execute (there may be multiple, because
 * our IRQ is active high and not edge triggered), and then
 * runs all of them one after the other.
 */
static void goldfish_sync_work_item_fn(struct work_struct *input)
{

	struct goldfish_sync_state *sync_state;
	int sync_fence_fd;

	struct goldfish_sync_timeline_obj *timeline;
	uint64_t timeline_ptr;

	uint64_t hostcmd_handle;

	uint32_t cmd;
	uint64_t handle;
	uint32_t time_arg;

	struct goldfish_sync_hostcmd *todo;
	uint32_t todo_end;

	unsigned long irq_flags;

	struct goldfish_sync_hostcmd to_run[GOLDFISH_SYNC_MAX_CMDS];
	uint32_t i = 0;

	sync_state = container_of(input, struct goldfish_sync_state, work_item);

	mutex_lock(&sync_state->mutex_lock);

	spin_lock_irqsave(&sync_state->lock, irq_flags); {

		todo_end = sync_state->to_do_end;

		DPRINT("num sync todos: %u", sync_state->to_do_end);

		for (i = 0; i < todo_end; i++)
			to_run[i] = sync_state->to_do[i];

		/* We expect that commands will come in at a slow enough rate
		 * so that incoming items will not be more than
		 * GOLDFISH_SYNC_MAX_CMDS.
		 *
		 * This is because the way the sync device is used,
		 * it's only for managing buffer data transfers per frame,
		 * with a sequential dependency between putting things in
		 * to_do and taking them out. Once a set of commands is
		 * queued up in to_do, the user of the device waits for
		 * them to be processed before queuing additional commands,
		 * which limits the rate at which commands come in
		 * to the rate at which we take them out here.
		 *
		 * We also don't expect more than MAX_CMDS to be issued
		 * at once; there is a correspondence between
		 * which buffers need swapping to the (display / buffer queue)
		 * to particular commands, and we don't expect there to be
		 * enough display or buffer queues in operation at once
		 * to overrun GOLDFISH_SYNC_MAX_CMDS.
		 */
		sync_state->to_do_end = 0;

	} spin_unlock_irqrestore(&sync_state->lock, irq_flags);

	for (i = 0; i < todo_end; i++) {
		DPRINT("todo index: %u", i);

		todo = &to_run[i];

		cmd = todo->cmd;

		handle = (uint64_t)todo->handle;
		time_arg = todo->time_arg;
		hostcmd_handle = (uint64_t)todo->hostcmd_handle;

		DTRACE();

		timeline = (struct goldfish_sync_timeline_obj *)(uintptr_t)handle;

		switch (cmd) {
		case CMD_SYNC_READY:
			break;
		case CMD_CREATE_SYNC_TIMELINE:
			DPRINT("exec CMD_CREATE_SYNC_TIMELINE: "
					"handle=0x%llx time_arg=%d",
					handle, time_arg);
			timeline = goldfish_sync_timeline_create();
			timeline_ptr = (uintptr_t)timeline;
			goldfish_sync_hostcmd_reply(sync_state, CMD_CREATE_SYNC_TIMELINE,
										timeline_ptr,
										0,
										hostcmd_handle);
			DPRINT("sync timeline created: %p", timeline);
			break;
		case CMD_CREATE_SYNC_FENCE:
			DPRINT("exec CMD_CREATE_SYNC_FENCE: "
					"handle=0x%llx time_arg=%d",
					handle, time_arg);
			sync_fence_fd = goldfish_sync_fence_create(timeline, time_arg);
			goldfish_sync_hostcmd_reply(sync_state, CMD_CREATE_SYNC_FENCE,
										sync_fence_fd,
										0,
										hostcmd_handle);
			break;
		case CMD_SYNC_TIMELINE_INC:
			DPRINT("exec CMD_SYNC_TIMELINE_INC: "
					"handle=0x%llx time_arg=%d",
					handle, time_arg);
			goldfish_sync_timeline_inc(timeline, time_arg);
			break;
		case CMD_DESTROY_SYNC_TIMELINE:
			DPRINT("exec CMD_DESTROY_SYNC_TIMELINE: "
					"handle=0x%llx time_arg=%d",
					handle, time_arg);
			goldfish_sync_timeline_destroy(timeline);
			break;
		}
		DPRINT("Done executing sync command");
	}
	mutex_unlock(&sync_state->mutex_lock);
}

/* Guest-side interface: file operations */

/* Goldfish sync context and ioctl info.
 *
 * When a sync context is created by open()-ing the goldfish sync device, we
 * create a sync context (|goldfish_sync_context|).
 *
 * Currently, the only data required to track is the sync timeline itself
 * along with the current time, which are all packed up in the
 * |goldfish_sync_timeline_obj| field. We use a |goldfish_sync_context|
 * as the filp->private_data.
 *
 * Next, when a sync context user requests that work be queued and a fence
 * fd provided, we use the |goldfish_sync_ioctl_info| struct, which holds
 * information about which host handles to touch for this particular
 * queue-work operation. We need to know about the host-side sync thread
 * and the particular host-side GLsync object. We also possibly write out
 * a file descriptor.
 */
struct goldfish_sync_context {
	struct goldfish_sync_timeline_obj *timeline;
};

struct goldfish_sync_ioctl_info {
	uint64_t host_glsync_handle_in;
	uint64_t host_syncthread_handle_in;
	int fence_fd_out;
};

static int goldfish_sync_open(struct inode *inode, struct file *file)
{

	struct goldfish_sync_context *sync_context;

	DTRACE();

	mutex_lock(&global_sync_state->mutex_lock);

	sync_context = kzalloc(sizeof(struct goldfish_sync_context), GFP_KERNEL);

	if (sync_context == NULL) {
		ERR("Creation of goldfish sync context failed!");
		mutex_unlock(&global_sync_state->mutex_lock);
		return -ENOMEM;
	}

	sync_context->timeline = NULL;

	file->private_data = sync_context;

	DPRINT("successfully create a sync context @0x%p", sync_context);

	mutex_unlock(&global_sync_state->mutex_lock);

	return 0;
}

static int goldfish_sync_release(struct inode *inode, struct file *file)
{

	struct goldfish_sync_context *sync_context;

	DTRACE();

	mutex_lock(&global_sync_state->mutex_lock);

	sync_context = file->private_data;

	if (sync_context->timeline)
		goldfish_sync_timeline_destroy(sync_context->timeline);

	sync_context->timeline = NULL;

	kfree(sync_context);

	mutex_unlock(&global_sync_state->mutex_lock);

	return 0;
}

/* |goldfish_sync_ioctl| is the guest-facing interface of goldfish sync
 * and is used in conjunction with eglCreateSyncKHR to queue up the
 * actual work of waiting for the EGL sync command to complete,
 * possibly returning a fence fd to the guest.
 */
static long goldfish_sync_ioctl(struct file *file,
								unsigned int cmd,
								unsigned long arg)
{
	struct goldfish_sync_context *sync_context_data;
	struct goldfish_sync_timeline_obj *timeline;
	int fd_out;
	struct goldfish_sync_ioctl_info ioctl_data;

	DTRACE();

	sync_context_data = file->private_data;
	fd_out = -1;

	switch (cmd) {
	case GOLDFISH_SYNC_IOC_QUEUE_WORK:

		DPRINT("exec GOLDFISH_SYNC_IOC_QUEUE_WORK");

		mutex_lock(&global_sync_state->mutex_lock);

		if (copy_from_user(&ioctl_data,
						(void __user *)arg,
						sizeof(ioctl_data))) {
			ERR("Failed to copy memory for ioctl_data from user.");
			mutex_unlock(&global_sync_state->mutex_lock);
			return -EFAULT;
		}

		if (ioctl_data.host_syncthread_handle_in == 0) {
			DPRINT("Error: zero host syncthread handle!!!");
			mutex_unlock(&global_sync_state->mutex_lock);
			return -EFAULT;
		}

		if (!sync_context_data->timeline) {
			DPRINT("no timeline yet, create one.");
			sync_context_data->timeline = goldfish_sync_timeline_create();
			DPRINT("timeline: 0x%p", &sync_context_data->timeline);
		}

		timeline = sync_context_data->timeline;
		fd_out = goldfish_sync_fence_create(timeline,
											timeline->current_time + 1);
		DPRINT("Created fence with fd %d and current time %u (timeline: 0x%p)",
			   fd_out,
			   sync_context_data->timeline->current_time + 1,
			   sync_context_data->timeline);

		ioctl_data.fence_fd_out = fd_out;

		if (copy_to_user((void __user *)arg,
						&ioctl_data,
						sizeof(ioctl_data))) {
			DPRINT("Error, could not copy to user!!!");

			sys_close(fd_out);
			/* We won't be doing an increment, kref_put immediately. */
			kref_put(&timeline->kref, delete_timeline_obj);
			mutex_unlock(&global_sync_state->mutex_lock);
			return -EFAULT;
		}

		/* We are now about to trigger a host-side wait;
		 * accumulate on |pending_waits|. */
		goldfish_sync_send_guestcmd(global_sync_state,
				CMD_TRIGGER_HOST_WAIT,
				ioctl_data.host_glsync_handle_in,
				ioctl_data.host_syncthread_handle_in,
				(uint64_t)(uintptr_t)(sync_context_data->timeline));

		mutex_unlock(&global_sync_state->mutex_lock);
		return 0;
	default:
		return -ENOTTY;
	}
}

static const struct file_operations goldfish_sync_fops = {
	.owner = THIS_MODULE,
	.open = goldfish_sync_open,
	.release = goldfish_sync_release,
	.unlocked_ioctl = goldfish_sync_ioctl,
	.compat_ioctl = goldfish_sync_ioctl,
};

static struct miscdevice goldfish_sync_device = {
	.name = "goldfish_sync",
	.fops = &goldfish_sync_fops,
};


static bool setup_verify_batch_cmd_addr(struct goldfish_sync_state *sync_state,
										void *batch_addr,
										uint32_t addr_offset,
										uint32_t addr_offset_high)
{
	uint64_t batch_addr_phys;
	uint32_t batch_addr_phys_test_lo;
	uint32_t batch_addr_phys_test_hi;

	if (!batch_addr) {
		ERR("Could not use batch command address!");
		return false;
	}

	batch_addr_phys = virt_to_phys(batch_addr);
	writel((uint32_t)(batch_addr_phys),
			sync_state->reg_base + addr_offset);
	writel((uint32_t)(batch_addr_phys >> 32),
			sync_state->reg_base + addr_offset_high);

	batch_addr_phys_test_lo =
		readl(sync_state->reg_base + addr_offset);
	batch_addr_phys_test_hi =
		readl(sync_state->reg_base + addr_offset_high);

	if (virt_to_phys(batch_addr) !=
			(((uint64_t)batch_addr_phys_test_hi << 32) |
			 batch_addr_phys_test_lo)) {
		ERR("Invalid batch command address!");
		return false;
	}

	return true;
}

int goldfish_sync_probe(struct platform_device *pdev)
{
	struct resource *ioresource;
	struct goldfish_sync_state *sync_state = global_sync_state;
	int status;

	DTRACE();

	sync_state->to_do_end = 0;

	spin_lock_init(&sync_state->lock);
	mutex_init(&sync_state->mutex_lock);

	platform_set_drvdata(pdev, sync_state);

	ioresource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (ioresource == NULL) {
		ERR("platform_get_resource failed");
		return -ENODEV;
	}

	sync_state->reg_base = devm_ioremap(&pdev->dev, ioresource->start, PAGE_SIZE);
	if (sync_state->reg_base == NULL) {
		ERR("Could not ioremap");
		return -ENOMEM;
	}

	sync_state->irq = platform_get_irq(pdev, 0);
	if (sync_state->irq < 0) {
		ERR("Could not platform_get_irq");
		return -ENODEV;
	}

	status = devm_request_irq(&pdev->dev,
							sync_state->irq,
							goldfish_sync_interrupt,
							IRQF_SHARED,
							pdev->name,
							sync_state);
	if (status) {
		ERR("request_irq failed");
		return -ENODEV;
	}

	INIT_WORK(&sync_state->work_item,
			  goldfish_sync_work_item_fn);

	misc_register(&goldfish_sync_device);

	/* Obtain addresses for batch send/recv of commands. */
	{
		struct goldfish_sync_hostcmd *batch_addr_hostcmd;
		struct goldfish_sync_guestcmd *batch_addr_guestcmd;

		batch_addr_hostcmd = devm_kzalloc(&pdev->dev, sizeof(struct goldfish_sync_hostcmd),
				GFP_KERNEL);
		batch_addr_guestcmd = devm_kzalloc(&pdev->dev, sizeof(struct goldfish_sync_guestcmd),
				GFP_KERNEL);

		if (!setup_verify_batch_cmd_addr(sync_state,
					batch_addr_hostcmd,
					SYNC_REG_BATCH_COMMAND_ADDR,
					SYNC_REG_BATCH_COMMAND_ADDR_HIGH)) {
			ERR("goldfish_sync: Could not setup batch command address");
			return -ENODEV;
		}

		if (!setup_verify_batch_cmd_addr(sync_state,
					batch_addr_guestcmd,
					SYNC_REG_BATCH_GUESTCOMMAND_ADDR,
					SYNC_REG_BATCH_GUESTCOMMAND_ADDR_HIGH)) {
			ERR("goldfish_sync: Could not setup batch guest command address");
			return -ENODEV;
		}

		sync_state->batch_hostcmd = batch_addr_hostcmd;
		sync_state->batch_guestcmd = batch_addr_guestcmd;
	}

	INFO("goldfish_sync: Initialized goldfish sync device");

	writel(0, sync_state->reg_base + SYNC_REG_INIT);

	return 0;
}

static int goldfish_sync_remove(struct platform_device *pdev)
{
	struct goldfish_sync_state *sync_state = global_sync_state;

	DTRACE();

	misc_deregister(&goldfish_sync_device);
	memset(sync_state, 0, sizeof(struct goldfish_sync_state));
	return 0;
}

static const struct of_device_id goldfish_sync_of_match[] = {
	{ .compatible = "google,goldfish-sync", },
	{},
};
MODULE_DEVICE_TABLE(of, goldfish_sync_of_match);

static const struct acpi_device_id goldfish_sync_acpi_match[] = {
	{ "GFSH0006", 0 },
	{ },
};

MODULE_DEVICE_TABLE(acpi, goldfish_sync_acpi_match);

static struct platform_driver goldfish_sync = {
	.probe = goldfish_sync_probe,
	.remove = goldfish_sync_remove,
	.driver = {
		.name = "goldfish_sync",
		.of_match_table = goldfish_sync_of_match,
		.acpi_match_table = ACPI_PTR(goldfish_sync_acpi_match),
	}
};

module_platform_driver(goldfish_sync);

MODULE_AUTHOR("Google, Inc.");
MODULE_DESCRIPTION("Android QEMU Sync Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");

/* This function is only to run a basic test of sync framework.
 * It creates a timeline and fence object whose signal point is at 1.
 * The timeline is incremented, and we use the sync framework's
 * sync_fence_wait on that fence object. If everything works out,
 * we should not hang in the wait and return immediately.
 * There is no way to explicitly run this test yet, but it
 * can be used by inserting it at the end of goldfish_sync_probe.
 */
void test_kernel_sync(void)
{
	struct goldfish_sync_timeline_obj *test_timeline;
	int test_fence_fd;

	DTRACE();

	DPRINT("test sw_sync");

	test_timeline = goldfish_sync_timeline_create();
	DPRINT("sw_sync_timeline_create -> 0x%p", test_timeline);

	test_fence_fd = goldfish_sync_fence_create(test_timeline, 1);
	DPRINT("sync_fence_create -> %d", test_fence_fd);

	DPRINT("incrementing test timeline");
	goldfish_sync_timeline_inc(test_timeline, 1);

	DPRINT("test waiting (should NOT hang)");
	sync_fence_wait(
			sync_fence_fdget(test_fence_fd), -1);

	DPRINT("test waiting (afterward)");
}
