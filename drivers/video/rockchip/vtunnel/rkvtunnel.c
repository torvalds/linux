// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/freezer.h>
#include <linux/miscdevice.h>
#include <linux/of.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/dma-buf.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/kfifo.h>
#include <linux/debugfs.h>
#include <linux/random.h>
#include <linux/sync_file.h>
#include <linux/sched/task.h>
#include <linux/sched/clock.h>

#include <asm-generic/bug.h>

#include "rkvtunnel.h"

#define DEVICE_NAME				"rkvtunnel"
#define RKVT_MAX_NAME_LENGTH			128
#define RKVT_POOL_SIZE				32
#define RKVT_MAX_WAIT_MS			4
#define RKVT_FENCE_WAIT_MS			3000

#define RKVT_DBG_USER				(1U << 0)
#define RKVT_DBG_BUFFERS			(1U << 1)
#define RKVT_DBG_CMD				(1U << 2)
#define RKVT_DBG_FILE				(1U << 3)

#define rkvt_dbg(mask, x...)\
	do { if (unlikely(vt_dev_dbg & mask)) pr_info(x); } while (0)

enum rkvt_buf_status_e {
	RKVT_BUF_QUEUE,
	RKVT_BUF_DEQUEUE,
	RKVT_BUF_ACQUIRE,
	RKVT_BUF_RELEASE,
	RKVT_BUF_FREE,
	RKVT_BUF_BUTT,
};

union rkvt_ioc_arg {
	struct rkvt_alloc_id_data alloc_data;
	struct rkvt_ctrl_data ctrl_data;
	struct rkvt_buf_data buffer_data;
};

struct rkvt_dev {
	struct device *dev;
	struct miscdevice mdev;
	struct mutex inst_lock; /* protect inst_list and ints_idr */
	struct idr inst_idr;
	struct list_head list_inst; /* manage all instances */

	struct mutex session_lock; /* protect sessions */
	struct list_head list_session;

	char *dev_name;
	int inst_id_generator;
	atomic64_t cid_generator;
	struct dentry *debug_root;
};

struct rkvt_session {
	struct list_head dev_link;
	struct rkvt_dev *vt_dev;
	struct list_head list_inst; /* manage instance in session */

	enum rkvt_caller_e caller;
	pid_t pid;
	char name[RKVT_MAX_NAME_LENGTH];
	char disp_name[RKVT_MAX_NAME_LENGTH];
	int disp_serial;
	int cid;
	struct task_struct *task;
	struct dentry *debug_root;
};

struct rkvt_buffer {
	struct file *file_buf[MAX_BUF_HANDLE_FDS];
	int fds_pro[MAX_BUF_HANDLE_FDS];
	int fds_con[MAX_BUF_HANDLE_FDS];

	struct file *ready_render_fence;
	struct dma_fence *rendered_fence;
	struct rkvt_session *session_pro;
	int cid_pro;
	struct rkvt_buf_base base;
};

struct rkvt_instance {
	struct kref ref;
	int id;
	struct rkvt_dev *vt_dev;

	struct mutex lock;
	struct list_head dev_link;
	struct list_head session_link;
	struct rkvt_session *consumer;
	struct rkvt_session *producer;
	wait_queue_head_t wait_consumer;
	wait_queue_head_t wait_producer;

	struct dentry *debug_root;
	int fcount;

	DECLARE_KFIFO_PTR(fifo_to_consumer, struct rkvt_buffer*);
	DECLARE_KFIFO_PTR(fifo_to_producer, struct rkvt_buffer*);

	struct rkvt_buffer vt_buffers[RKVT_POOL_SIZE];

	atomic64_t buf_id_generator;
};

static unsigned int vt_dev_dbg;

module_param(vt_dev_dbg, uint, 0644);
MODULE_PARM_DESC(vt_dev_dbg, "bit switch for vt debug information");

static const char *
rkvt_dbg_buf_status_to_string(int status)
{
	const char *status_str;

	switch (status) {
	case RKVT_BUF_QUEUE:
		status_str = "queued";
		break;
	case RKVT_BUF_DEQUEUE:
		status_str = "dequeued";
		break;
	case RKVT_BUF_ACQUIRE:
		status_str = "acquired";
		break;
	case RKVT_BUF_RELEASE:
		status_str = "released";
		break;
	case RKVT_BUF_FREE:
		status_str = "free";
		break;
	default:
		status_str = "unknown";
	}

	return status_str;
}

static int rkvt_dbg_instance_show(struct seq_file *s, void *unused)
{
	struct rkvt_instance *inst = s->private;
	int i;
	int size_to_con;
	int size_to_pro;
	int ref_count;

	mutex_lock(&inst->lock);
	size_to_con = kfifo_len(&inst->fifo_to_consumer);
	size_to_pro = kfifo_len(&inst->fifo_to_producer);
	ref_count = kref_read(&inst->ref);

	seq_printf(s, "tunnel (%p) id=%d, ref=%d, fcount=%d\n",
		   inst, inst->id, ref_count, inst->fcount);
	seq_puts(s, "-----------------------------------------------\n");
	if (inst->consumer)
		seq_printf(s, "consumer session (%s) %p\n",
			   inst->consumer->disp_name, inst->consumer);
	if (inst->producer)
		seq_printf(s, "producer session (%s) %p\n",
			   inst->producer->disp_name, inst->producer);
	seq_puts(s, "-----------------------------------------------\n");

	seq_printf(s, "to consumer fifo size:%d\n", size_to_con);
	seq_printf(s, "to producer fifo size:%d\n", size_to_pro);
	seq_puts(s, "-----------------------------------------------\n");

	seq_puts(s, "buffers:\n");

	for (i = 0; i < RKVT_POOL_SIZE; i++) {
		struct rkvt_buffer *buffer = &inst->vt_buffers[i];
		int status = buffer->base.buf_status;

		seq_printf(s, "    buffer produce_fd[0](%d) status(%s)\n",
			   buffer->fds_pro[0],
			   rkvt_dbg_buf_status_to_string(status));
	}
	seq_puts(s, "-----------------------------------------------\n");
	mutex_unlock(&inst->lock);

	return 0;
}

static int
rkvt_dbg_instance_open(struct inode *inode, struct file *file)
{
	return single_open(file,
			   rkvt_dbg_instance_show,
			   inode->i_private);
}

static const struct file_operations dbg_instance_fops = {
	.open = rkvt_dbg_instance_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int rkvt_dbg_session_show(struct seq_file *s, void *unused)
{
	struct rkvt_session *session = s->private;

	seq_printf(s, "session(%s) %p role %s cid %d\n",
		   session->disp_name, session,
		   session->caller == RKVT_CALLER_PRODUCER ?
		   "producer" : (session->caller == RKVT_CALLER_CONSUMER ?
		   "consumer" : "invalid"), session->cid);
	seq_puts(s, "-----------------------------------------------\n");

	return 0;
}

static int rkvt_dbg_session_open(struct inode *inode, struct file *file)
{
	return single_open(file,
			   rkvt_dbg_session_show,
			   inode->i_private);
}

static const struct file_operations debug_session_fops = {
	.open = rkvt_dbg_session_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __rkvt_close_fd(struct files_struct *files, unsigned int fd)
{
	struct file *file;
	struct fdtable *fdt;

	spin_lock(&files->file_lock);

	fdt = files_fdtable(files);
	if (fd >= fdt->max_fds)
		goto out_unlock;
	file = fdt->fd[fd];
	if (!file)
		goto out_unlock;

	rcu_assign_pointer(fdt->fd[fd], NULL);
	spin_unlock(&files->file_lock);

	put_unused_fd(fd);
	return filp_close(file, files);

out_unlock:
	spin_unlock(&files->file_lock);
	return -EBADF;
}

static int rkvt_close_fd(struct rkvt_session *session, unsigned int fd)
{
	int ret;

	if (!session->task)
		return -ESRCH;

	ret = __rkvt_close_fd(session->task->files, fd);
	if (unlikely(ret == -ERESTARTSYS ||
		     ret == -ERESTARTNOINTR ||
		     ret == -ERESTARTNOHAND ||
		     ret == -ERESTART_RESTARTBLOCK))
		ret = -EINTR;

	return ret;
}

/* The function is responsible for fifo_to_consumer fifo operation
 * requires external use of rkvt_instance.lock protection
 */
static void rkvt_inst_clear_consumer(struct rkvt_instance *inst)
{
	struct rkvt_buffer *buffer = NULL;
	int i = 0;

	if (!inst)
		return;

	while (kfifo_get(&inst->fifo_to_consumer, &buffer)) {
		/* put file */
		for (i = 0; i < buffer->base.num_fds; i++) {
			if (buffer->file_buf[i]) {
				fput(buffer->file_buf[i]);
				buffer->file_buf[i] = NULL;
			}
			inst->fcount--;
		}
		if (buffer->ready_render_fence) {
			fput(buffer->ready_render_fence);
			buffer->ready_render_fence = NULL;
		}
		rkvt_dbg(RKVT_DBG_FILE,
			 "vt [%d] instance trim file(%p) buffer(%p) ino(%08lu) fcount=%d\n",
			 inst->id, buffer->file_buf, buffer,
			 buffer->file_buf[i] ?
			 file_inode(buffer->file_buf[i])->i_ino : 0,
			 inst->fcount);
		if (inst->producer != NULL) {
			buffer->base.buf_status = RKVT_BUF_RELEASE;
			kfifo_put(&inst->fifo_to_producer, buffer);
			wake_up_interruptible(&inst->wait_producer);
		} else {
			buffer->base.buf_status = RKVT_BUF_FREE;
		}
	}
}

/* The function is responsible for fifo_to_consumer fifo operation
 * requires external use of rkvt_instance.lock protection.
 */
static void rkvt_inst_clear_producer(struct rkvt_instance *inst)
{
	struct rkvt_buffer *buffer = NULL;

	if (!inst)
		return;

	while (kfifo_get(&inst->fifo_to_producer, &buffer)) {
		if (buffer->rendered_fence) {
			dma_fence_put(buffer->rendered_fence);
			buffer->rendered_fence = NULL;
		}
		buffer->base.buf_status = RKVT_BUF_FREE;
	}
}

static void rkvt_inst_destroy(struct kref *kref)
{
	struct rkvt_instance *inst =
		container_of(kref, struct rkvt_instance, ref);
	struct rkvt_dev *vt_dev = inst->vt_dev;

	list_del_init(&inst->dev_link);
	idr_remove(&vt_dev->inst_idr, inst->id);

	rkvt_dbg(RKVT_DBG_USER, "vt [%d] destroy\n", inst->id);

	mutex_lock(&inst->lock);
	rkvt_inst_clear_consumer(inst);
	rkvt_inst_clear_producer(inst);
	kfifo_free(&inst->fifo_to_consumer);
	kfifo_free(&inst->fifo_to_producer);
	mutex_unlock(&inst->lock);

	debugfs_remove_recursive(inst->debug_root);

	devm_kfree(vt_dev->dev, inst);
}

static struct rkvt_instance *rkvt_inst_create(struct rkvt_dev *vt_dev)
{
	struct rkvt_instance *inst;
	int status;
	int i;

	inst = devm_kzalloc(vt_dev->dev, sizeof(*inst), GFP_KERNEL);
	if (!inst)
		return ERR_PTR(-ENOMEM);

	inst->vt_dev = vt_dev;
	mutex_init(&inst->lock);
	INIT_LIST_HEAD(&inst->dev_link);
	INIT_LIST_HEAD(&inst->session_link);
	kref_init(&inst->ref);

	status = kfifo_alloc(&inst->fifo_to_consumer,
			     RKVT_POOL_SIZE, GFP_KERNEL);
	if (status)
		goto setup_fail;

	status = kfifo_alloc(&inst->fifo_to_producer,
			     RKVT_POOL_SIZE, GFP_KERNEL);
	if (status)
		goto fifo_alloc_fail;

	init_waitqueue_head(&inst->wait_producer);
	init_waitqueue_head(&inst->wait_consumer);

	for (i = 0; i < RKVT_POOL_SIZE; i++)
		inst->vt_buffers[i].base.buf_status = RKVT_BUF_FREE;

	/* insert it to dev instances list */
	mutex_lock(&vt_dev->inst_lock);
	list_add_tail(&inst->dev_link, &vt_dev->list_inst);
	mutex_unlock(&vt_dev->inst_lock);

	return inst;
fifo_alloc_fail:
	kfifo_free(&inst->fifo_to_consumer);
setup_fail:
	devm_kfree(vt_dev->dev, inst);
	return ERR_PTR(status);
}

/* The function protected by rkvt_dev.session_lock by caller */
static int
rkvt_get_session_serial(const struct list_head *sessions,
			const unsigned char *name)
{
	int serial = -1;
	struct rkvt_session *session, *n;

	list_for_each_entry_safe(session, n, sessions, dev_link) {
		if (strcmp(session->name, name))
			continue;
		serial = max(serial, session->disp_serial);
	}

	return serial + 1;
}

/* The function protected by rkvt_instance.lock by caller */
static void
rkvt_session_trim_locked(struct rkvt_session *session, struct rkvt_instance *inst)
{
	if (!session || !inst)
		return;

	if (inst->producer && inst->producer == session) {
		rkvt_inst_clear_producer(inst);
		inst->producer = NULL;
	}

	if (inst->consumer && inst->consumer == session) {
		rkvt_inst_clear_consumer(inst);
		inst->consumer = NULL;
	}
}

static int rkvt_inst_trim(struct rkvt_session *session)
{
	struct rkvt_dev *vt_dev = session->vt_dev;
	struct rkvt_instance *inst, *n;
	int i;

	mutex_lock(&vt_dev->inst_lock);
	list_for_each_entry_safe(inst, n, &vt_dev->list_inst, dev_link) {
		mutex_lock(&inst->lock);
		rkvt_session_trim_locked(session, inst);

		if (!inst->consumer && !inst->producer) {
			rkvt_inst_clear_producer(inst);
			rkvt_inst_clear_consumer(inst);

			for (i = 0; i < RKVT_POOL_SIZE; i++)
				inst->vt_buffers[i].base.buf_status = RKVT_BUF_FREE;
		}
		mutex_unlock(&inst->lock);
	}
	mutex_unlock(&vt_dev->inst_lock);

	return 0;
}

static struct rkvt_session *
rkvt_session_create(struct rkvt_dev *vt_dev, const char *name)
{
	struct rkvt_session *session;
	struct task_struct *task = NULL;

	if (!name) {
		dev_err(vt_dev->dev, "%s: Name can not be null\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	session = devm_kzalloc(vt_dev->dev, sizeof(*session), GFP_KERNEL);
	if (!session)
		return ERR_PTR(-ENOMEM);

	get_task_struct(current->group_leader);
	task_lock(current->group_leader);
	session->pid = task_pid_nr(current->group_leader);

	if (current->group_leader->flags & PF_KTHREAD) {
		put_task_struct(current->group_leader);
		task = NULL;
	} else {
		task = current->group_leader;
	}

	task_unlock(current->group_leader);

	session->vt_dev = vt_dev;
	session->task = task;
	session->caller = RKVT_CALLER_BUTT;
	INIT_LIST_HEAD(&session->dev_link);
	INIT_LIST_HEAD(&session->list_inst);
	snprintf(session->name, RKVT_MAX_NAME_LENGTH, "%s", name);

	mutex_lock(&vt_dev->session_lock);
	session->disp_serial = rkvt_get_session_serial(&vt_dev->list_session, name);
	snprintf(session->disp_name, RKVT_MAX_NAME_LENGTH, "%s-%d",
			 name, session->disp_serial);

	list_add_tail(&session->dev_link, &vt_dev->list_session);

	/* add debug fs */
	session->debug_root = debugfs_create_file(session->disp_name,
						  0664,
						  vt_dev->debug_root,
						  session,
						  &debug_session_fops);

	mutex_unlock(&vt_dev->session_lock);

	rkvt_dbg(RKVT_DBG_USER, "vt session %s create\n", session->disp_name);

	return session;
}

static void rkvt_session_destroy(struct rkvt_session *session)
{
	struct rkvt_dev *vt_dev = session->vt_dev;
	struct rkvt_instance *inst = NULL;

	rkvt_dbg(RKVT_DBG_USER, "vt session %s destroy\n", session->disp_name);

	mutex_lock(&vt_dev->inst_lock);
	while ((inst = list_first_entry_or_null(&session->list_inst,
						struct rkvt_instance, session_link))) {
		list_del_init(&inst->session_link);
		kref_put(&inst->ref, rkvt_inst_destroy);
	}
	mutex_unlock(&vt_dev->inst_lock);

	mutex_lock(&vt_dev->session_lock);
	if (session->task)
		put_task_struct(session->task);
	list_del_init(&session->dev_link);
	debugfs_remove_recursive(session->debug_root);
	mutex_unlock(&vt_dev->session_lock);

	rkvt_inst_trim(session);
	devm_kfree(vt_dev->dev, session);
}

static int rkvt_open(struct inode *inode, struct file *filep)
{
	struct miscdevice *miscdev = filep->private_data;
	struct rkvt_dev *vt_dev = container_of(miscdev, struct rkvt_dev, mdev);
	struct rkvt_session *session;
	char debug_name[64];

	snprintf(debug_name, sizeof(debug_name), "%u", task_pid_nr(current->group_leader));
	session = rkvt_session_create(vt_dev, debug_name);
	if (IS_ERR(session))
		return PTR_ERR(session);

	filep->private_data = session;

	return 0;
}

static int rkvt_release(struct inode *inode, struct file *filep)
{
	struct rkvt_session *session = filep->private_data;

	rkvt_session_destroy(session);
	filep->private_data = NULL;

	return 0;
}

static int rkvt_get_connected_id(struct rkvt_dev *vt_dev)
{
	return atomic64_inc_return(&vt_dev->cid_generator);
}

static struct rkvt_instance *
rkvt_inst_get_by_tid(struct rkvt_dev *vt_dev, int id)
{
	struct rkvt_instance *inst;

	mutex_lock(&vt_dev->inst_lock);
	inst = idr_find(&vt_dev->inst_idr, id);
	if (!inst) {
		mutex_unlock(&vt_dev->inst_lock);
		dev_err(vt_dev->dev, "find rkvt [%d] by device idr err, instance is null\n", id);
		return NULL;
	}
	kref_get(&inst->ref);
	mutex_unlock(&vt_dev->inst_lock);

	return inst;
}

static void rkvt_inst_put(struct rkvt_instance *inst)
{
	struct rkvt_dev *vt_dev;

	if (!inst)
		return;

	vt_dev = inst->vt_dev;

	mutex_lock(&vt_dev->inst_lock);
	kref_put(&inst->ref, rkvt_inst_destroy);
	mutex_unlock(&vt_dev->inst_lock);
}

static int
rkvt_connect_proc(struct rkvt_ctrl_data *data, struct rkvt_session *session)
{
	struct rkvt_dev *vt_dev = session->vt_dev;
	struct rkvt_instance *inst;
	int ret = 0;

	// ref get not put in function end, because connect need hold 1 refs.
	inst = rkvt_inst_get_by_tid(vt_dev, data->vt_id);
	if (!inst)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (data->caller == RKVT_CALLER_PRODUCER) {
		if (inst->producer && inst->producer != session) {
			dev_err(vt_dev->dev, "Connect to rkvt [%d] err, already has producer\n",
					data->vt_id);
			ret = -EINVAL;
			goto connect_fail;
		}
		inst->producer = session;
	} else if (data->caller == RKVT_CALLER_CONSUMER) {
		if (inst->consumer && inst->consumer != session) {
			dev_err(vt_dev->dev, "Connect to rkvt [%d] err, already has consumer\n",
					data->vt_id);
			ret = -EINVAL;
			goto connect_fail;
		}
		inst->consumer = session;
	}
	mutex_unlock(&inst->lock);
	session->cid = rkvt_get_connected_id(vt_dev);
	session->caller = data->caller;

	rkvt_dbg(RKVT_DBG_USER, "rkvt [%d] %s-%d connect, instance ref %d\n",
		 inst->id,
		 data->caller == RKVT_CALLER_PRODUCER ? "producer" : "consumer",
		 session->pid,
		 kref_read(&inst->ref));

	return 0;

connect_fail:
	mutex_unlock(&inst->lock);
	// ref put for rkvt_instance_get_by_tid
	rkvt_inst_put(inst);

	return ret;
}

static int
rkvt_disconnect_proc(struct rkvt_ctrl_data *data, struct rkvt_session *session)
{
	struct rkvt_dev *vt_dev = session->vt_dev;
	struct rkvt_instance *inst;

	inst = rkvt_inst_get_by_tid(vt_dev, data->vt_id);
	if (!inst)
		return -EINVAL;
	if (session->caller != data->caller)
		goto session_invail;

	mutex_lock(&inst->lock);
	if (data->caller == RKVT_CALLER_PRODUCER) {
		if (!inst->producer)
			goto disconnect_fail;
		if (inst->producer != session)
			goto disconnect_fail;

		rkvt_session_trim_locked(session, inst);
		inst->producer = NULL;
		wake_up_interruptible(&inst->wait_producer);
	} else if (data->caller == RKVT_CALLER_CONSUMER) {
		if (!inst->consumer)
			goto disconnect_fail;
		if (inst->consumer != session)
			goto disconnect_fail;

		rkvt_session_trim_locked(session, inst);
		inst->consumer = NULL;
		wake_up_interruptible(&inst->wait_consumer);
	}
	mutex_unlock(&inst->lock);

	rkvt_dbg(RKVT_DBG_USER, "rkvt [%d] %s-%d disconnect, instance ref %d\n",
		 inst->id,
		 data->caller == RKVT_CALLER_PRODUCER ? "producer" : "consumer",
		 session->pid,
		 kref_read(&inst->ref));
	// ref put for rkvt_instance_get_by_tid
	rkvt_inst_put(inst);
	// ref put for connect proc
	rkvt_inst_put(inst);
	session->cid = -1;

	return 0;

disconnect_fail:
	mutex_unlock(&inst->lock);
session_invail:
	// ref put for rkvt_instance_get_by_tid
	rkvt_inst_put(inst);

	return -EINVAL;
}

static int
rkvt_reset_proc(struct rkvt_ctrl_data *data, struct rkvt_session *session)
{
	struct rkvt_dev *vt_dev = session->vt_dev;
	struct rkvt_instance *inst;
	long long read_buf_id;

	inst = rkvt_inst_get_by_tid(vt_dev, data->vt_id);
	if (!inst)
		return -EINVAL;

	mutex_lock(&inst->lock);
	rkvt_inst_clear_consumer(inst);
	rkvt_inst_clear_producer(inst);
	read_buf_id = atomic64_read(&inst->buf_id_generator);
	read_buf_id += 0x100;
	read_buf_id &= ~0xff;
	atomic64_set(&inst->buf_id_generator, read_buf_id);
	mutex_unlock(&inst->lock);

	rkvt_inst_put(inst);

	return 0;
}

static int
rkvt_has_consumer_proc(struct rkvt_ctrl_data *data, struct rkvt_session *session)
{
	struct rkvt_dev *vt_dev = session->vt_dev;
	struct rkvt_instance *inst;

	inst = rkvt_inst_get_by_tid(vt_dev, data->vt_id);
	if (!inst)
		return -EINVAL;

	mutex_lock(&inst->lock);
	data->ctrl_data = inst->consumer != NULL ? 1 : 0;
	mutex_unlock(&inst->lock);

	rkvt_inst_put(inst);

	return 0;
}

static int
rkvt_ctrl_proc(struct rkvt_ctrl_data *data, struct rkvt_session *session)
{
	int id = data->vt_id;
	int ret = 0;

	if (id < 0)
		return -EINVAL;
	if (data->caller == RKVT_CALLER_BUTT)
		return -EINVAL;

	switch (data->ctrl_cmd) {
	case RKVT_CTRL_CONNECT: {
		ret = rkvt_connect_proc(data, session);
		break;
	}
	case RKVT_CTRL_DISCONNECT: {
		ret = rkvt_disconnect_proc(data, session);
		break;
	}
	case RKVT_CTRL_RESET: {
		ret = rkvt_reset_proc(data, session);
		break;
	}
	case RKVT_CTRL_HAS_CONSUMER: {
		ret = rkvt_has_consumer_proc(data, session);
		break;
	}
	default:
		pr_err("unknown rkvt cmd:%d\n", data->ctrl_cmd);
		return -EINVAL;
	}

	return ret;
}

static struct
rkvt_buffer *rkvt_buf_get(struct rkvt_instance *inst, int key)
{
	struct rkvt_buffer *buffer = NULL;
	int i;

	mutex_lock(&inst->lock);
	for (i = 0; i < RKVT_POOL_SIZE; i++) {
		buffer = &inst->vt_buffers[i];

		if (buffer->base.buf_status == RKVT_BUF_ACQUIRE &&
		    buffer->fds_con[0] == key)
			break;
	}
	mutex_unlock(&inst->lock);

	return buffer;
}

static int
rkvt_has_buf(struct rkvt_instance *inst, enum rkvt_caller_e caller)
{
	int ret = 0;

	if (caller == RKVT_CALLER_PRODUCER)
		ret = !kfifo_is_empty(&inst->fifo_to_producer);
	else
		ret = !kfifo_is_empty(&inst->fifo_to_consumer);

	return ret;
}

static int
rkvt_query_buf_and_wait(struct rkvt_instance *inst,
			enum rkvt_caller_e caller,
			int timeout_ms)
{
	int ret;
	wait_queue_head_t *wait_queue;

	if (caller == RKVT_CALLER_PRODUCER)
		wait_queue = &inst->wait_producer;
	else
		wait_queue = &inst->wait_consumer;
	if (caller == RKVT_CALLER_PRODUCER &&
	    !kfifo_is_empty(&inst->fifo_to_producer))
		return 0;
	if (caller == RKVT_CALLER_CONSUMER &&
	    !kfifo_is_empty(&inst->fifo_to_consumer))
		return 0;

	if (timeout_ms < 0)
		wait_event_interruptible(*wait_queue,
					 rkvt_has_buf(inst, caller));
	else if (timeout_ms > 0) {
		ret = wait_event_interruptible_timeout(*wait_queue,
							rkvt_has_buf(inst, caller),
							msecs_to_jiffies(timeout_ms));
		/* timeout */
		if (ret == 0)
			return -EAGAIN;
	} else
		return -EAGAIN;

	if (caller == RKVT_CALLER_PRODUCER &&
	    kfifo_is_empty(&inst->fifo_to_producer))
		return -EAGAIN;
	if (caller == RKVT_CALLER_CONSUMER &&
	    kfifo_is_empty(&inst->fifo_to_consumer))
		return -EAGAIN;

	return 0;
}

static struct rkvt_buffer *rkvt_get_free_buf(struct rkvt_instance *inst)
{
	struct rkvt_buffer *buffer = NULL;
	int i, status;

	mutex_lock(&inst->lock);
	for (i = 0; i < RKVT_POOL_SIZE; i++) {
		status = inst->vt_buffers[i].base.buf_status;
		if (status == RKVT_BUF_FREE || status == RKVT_BUF_DEQUEUE) {
			buffer = &inst->vt_buffers[i];
			memset(buffer->file_buf, 0, sizeof(buffer->file_buf));
			buffer->rendered_fence = NULL;
			break;
		}
	}
	mutex_unlock(&inst->lock);

	return buffer;
}

static int
rkvt_queue_buf(struct rkvt_buf_data *data, struct rkvt_session *session)
{
	struct rkvt_dev *vt_dev = session->vt_dev;
	struct rkvt_instance *inst = NULL;
	struct rkvt_buf_base *base = NULL;
	struct rkvt_buffer *buffer = NULL;
	int i;
	int ret = 0;

	inst = rkvt_inst_get_by_tid(vt_dev, data->vt_id);
	if (!inst)
		return -EINVAL;
	if (!inst->producer || inst->producer != session) {
		ret = -EINVAL;
		goto queue_fail;
	}
	if ((data->base.num_fds > MAX_BUF_HANDLE_FDS) ||
		(data->base.num_ints > MAX_BUF_HANDLE_INTS)) {
		ret = -EINVAL;
		goto queue_fail;
	}

	rkvt_dbg(RKVT_DBG_BUFFERS, "VTQB [%d] start\n", inst->id);

	base = &data->base;
	buffer = rkvt_get_free_buf(inst);
	for (i = 0; i < base->num_fds; i++) {
		buffer->fds_con[i] = -1;
		buffer->fds_pro[i] = base->fds[i];
		buffer->file_buf[i] = fget(base->fds[i]);

		if (!buffer->file_buf[i]) {
			ret = -EBADF;
			goto buf_fget_fail;
		}

		inst->fcount++;
		rkvt_dbg(RKVT_DBG_FILE,
			"VTQB [%d] fget file(%p) buf(%p) buf session(%p) ino(%08lu) fcount=%d\n",
			inst->id, buffer->file_buf[i], buffer, buffer->session_pro,
			buffer->file_buf[i] ? file_inode(buffer->file_buf[i])->i_ino : 0,
			inst->fcount);
	}

	if (base->fence_fd >= 0)
		buffer->ready_render_fence = fget(base->fence_fd);

	// buffer id is empty, generate a new id
	if (base->buffer_id == 0)
		base->buffer_id = atomic64_inc_return(&inst->buf_id_generator);
	buffer->base = *base;
	buffer->base.buf_status = RKVT_BUF_QUEUE;
	buffer->session_pro = session;
	buffer->cid_pro = session->cid;

	mutex_lock(&inst->lock);
	if (inst->consumer) {
		kfifo_put(&inst->fifo_to_consumer, buffer);
	} else {
		for (i = 0; i < buffer->base.num_fds; i++) {
			if (buffer->file_buf[i]) {
				fput(buffer->file_buf[i]);
				buffer->file_buf[i] = NULL;
			}
			inst->fcount--;
		}
		if (buffer->ready_render_fence) {
			fput(buffer->ready_render_fence);
			buffer->ready_render_fence = NULL;
		}
		buffer->base.buf_status = RKVT_BUF_RELEASE;
		kfifo_put(&inst->fifo_to_producer, buffer);
	}
	mutex_unlock(&inst->lock);

	if (inst->consumer)
		wake_up_interruptible(&inst->wait_consumer);
	else if (inst->producer)
		wake_up_interruptible(&inst->wait_producer);

	rkvt_dbg(RKVT_DBG_BUFFERS, "VTQB [%d] pfd[0]:%d end\n", inst->id, buffer->fds_pro[0]);

queue_fail:
	rkvt_inst_put(inst);

	return ret;
buf_fget_fail:
	for (i = 0; i < base->num_fds; i++) {
		if (buffer->file_buf[i]) {
			fput(buffer->file_buf[i]);
			buffer->file_buf[i] = NULL;
			inst->fcount--;
		}
	}
	rkvt_inst_put(inst);

	return ret;
}

static int
rkvt_deque_buf(struct rkvt_buf_data *data, struct rkvt_session *session)
{
	struct rkvt_dev *vt_dev = session->vt_dev;
	struct rkvt_instance *inst = NULL;
	struct rkvt_buffer *buffer = NULL;
	int ret = 0;
	unsigned long long cur_time, wait_time;
	int i;

	inst = rkvt_inst_get_by_tid(vt_dev, data->vt_id);
	if (!inst)
		return -EINVAL;
	if (!inst->producer || inst->producer != session) {
		ret = -EINVAL;
		goto deque_fail;
	}

	/* empty need wait */
	ret = rkvt_query_buf_and_wait(inst,
				      RKVT_CALLER_PRODUCER,
				      data->timeout_ms);
	if (ret)
		goto deque_fail;

	mutex_lock(&inst->lock);
	ret = kfifo_get(&inst->fifo_to_producer, &buffer);
	if (!ret || !buffer) {
		dev_err(vt_dev->dev, "VTDB [%d] got null buffer ret(%d)\n", inst->id, ret);
		mutex_unlock(&inst->lock);
		ret = -EAGAIN;
		goto deque_fail;
	}
	mutex_unlock(&inst->lock);

	/* it's previous connect buffer */
	if (buffer->cid_pro != session->cid) {
		if (buffer->rendered_fence) {
			dma_fence_put(buffer->rendered_fence);
			buffer->rendered_fence = NULL;
		}

		ret = -EAGAIN;
		goto deque_fail;
	}

	if (buffer->rendered_fence) {
		cur_time = sched_clock();
		ret = dma_fence_wait_timeout(buffer->rendered_fence, false,
					     msecs_to_jiffies(RKVT_FENCE_WAIT_MS));
		wait_time = sched_clock() - cur_time;
		rkvt_dbg(RKVT_DBG_BUFFERS,
			 "VTDB [%d] pfd[0]:%d rendered fence:%p fence_wait time %llu\n",
			 inst->id, buffer->fds_pro[0], buffer->rendered_fence, wait_time);

		if (ret < 0)
			dev_err(vt_dev->dev, "VTDB [%d] wait fence timeout\n", inst->id);

		dma_fence_put(buffer->rendered_fence);
		buffer->rendered_fence = NULL;
	}
	for (i = 0; i < buffer->base.num_fds; i++)
		rkvt_dbg(RKVT_DBG_FILE,
			"VTDB [%d] fget file(%p) buf(%p) buf session(%p) ino(%08lu) fcount=%d\n",
			inst->id, buffer->file_buf[i],
			buffer, buffer->session_pro,
			buffer->file_buf[i] ? file_inode(buffer->file_buf[i])->i_ino : 0,
			inst->fcount);

	buffer->base.vt_id = inst->id;
	/* return the buffer */
	data->base = buffer->base;
	buffer->base.buf_status = RKVT_BUF_DEQUEUE;

	rkvt_dbg(RKVT_DBG_BUFFERS, "VTDB [%d] end pfd[0]:%d\n", inst->id, buffer->fds_pro[0]);

deque_fail:
	rkvt_inst_put(inst);

	return ret;
}

static int
rkvt_acquire_buf(struct rkvt_buf_data *data, struct rkvt_session *session)
{
	struct rkvt_dev *vt_dev = session->vt_dev;
	struct rkvt_instance *inst = NULL;
	struct rkvt_buffer *buffer = NULL;
	int fd, ret = -1;
	int i;

	inst = rkvt_inst_get_by_tid(vt_dev, data->vt_id);
	if (!inst)
		return -EINVAL;
	if (!inst->consumer || inst->consumer != session) {
		ret = -EINVAL;
		goto acquire_fail;
	}
	if ((data->base.num_fds > MAX_BUF_HANDLE_FDS) ||
		(data->base.num_ints > MAX_BUF_HANDLE_INTS)) {
		ret = -EINVAL;
		goto acquire_fail;
	}

	/* empty need wait */
	ret = rkvt_query_buf_and_wait(inst,
				      RKVT_CALLER_CONSUMER,
				      data->timeout_ms);
	if (ret)
		goto acquire_fail;

	mutex_lock(&inst->lock);
	ret = kfifo_get(&inst->fifo_to_consumer, &buffer);
	mutex_unlock(&inst->lock);
	if (!ret || !buffer) {
		dev_err(vt_dev->dev, "VTAB [%d] got null buffer\n", inst->id);
		ret = -EAGAIN;
		goto acquire_fail;
	}

	/* get the fd in consumer */
	for (i = 0; i < buffer->base.num_fds; i++) {
		if (buffer->fds_con[i] <= 0) {
			fd = get_unused_fd_flags(O_CLOEXEC);
			if (fd < 0)
				goto no_memory;

			fd_install(fd, buffer->file_buf[i]);
			buffer->fds_con[i] = fd;
			buffer->base.fds[i] = fd;
		}
	}
	if (buffer->ready_render_fence) {
		fd = get_unused_fd_flags(O_CLOEXEC);
		if (fd < 0)
			goto no_memory;
		fd_install(fd, buffer->ready_render_fence);
		buffer->base.fence_fd = fd;
		buffer->ready_render_fence = NULL;
	} else {
		buffer->base.fence_fd = -1;
	}
	buffer->base.vt_id = inst->id;
	data->base = buffer->base;
	buffer->base.buf_status = RKVT_BUF_ACQUIRE;

	rkvt_dbg(RKVT_DBG_BUFFERS, "VTAB [%d] pfd[0](%d) buf(%p) buf session(%p)\n",
			inst->id, buffer->fds_pro[0], buffer, buffer->session_pro);

	rkvt_inst_put(inst);

	return 0;

no_memory:
	pr_info("VTAB [%d] install fd error\n", inst->id);
	mutex_lock(&inst->lock);
	for (i = 0; i < buffer->base.num_fds; i++) {
		rkvt_dbg(RKVT_DBG_FILE,
				"VTAB [%d] install fd error file(%p) buf(%p) ino(%08lu) fcount=%d\n",
				inst->id, buffer->file_buf[i], buffer,
				file_inode(buffer->file_buf[i])->i_ino, inst->fcount);
		if (buffer->file_buf[i]) {
			fput(buffer->file_buf[i]);
			buffer->file_buf[i] = NULL;
			inst->fcount--;
		}
	}
	if (buffer->ready_render_fence) {
		fput(buffer->ready_render_fence);
		buffer->ready_render_fence = NULL;
	}
	buffer->base.buf_status = RKVT_BUF_RELEASE;

	kfifo_put(&inst->fifo_to_producer, buffer);
	mutex_unlock(&inst->lock);
	if (inst->producer)
		wake_up_interruptible(&inst->wait_producer);
	ret = -ENOMEM;

acquire_fail:
	rkvt_inst_put(inst);

	return ret;
}

static int
rkvt_release_buf(struct rkvt_buf_data *data, struct rkvt_session *session)
{
	struct rkvt_dev *vt_dev = session->vt_dev;
	struct rkvt_instance *inst = NULL;
	struct rkvt_buf_base *buf_base = NULL;
	struct rkvt_buffer *buffer = NULL;
	int i;
	int ret = 0;
	long long read_buf_id;

	inst = rkvt_inst_get_by_tid(vt_dev, data->vt_id);
	if (!inst)
		return -EINVAL;
	if (!inst->consumer || inst->consumer != session) {
		ret = -EINVAL;
		goto release_fail;
	}

	buf_base = &data->base;
	buffer = rkvt_buf_get(inst, buf_base->fds[0]);
	if (!buffer) {
		ret = -EINVAL;
		goto release_fail;
	}

	if (buf_base->fence_fd >= 0)
		buffer->rendered_fence = sync_file_get_fence(buf_base->fence_fd);

	if (!buffer->rendered_fence)
		rkvt_dbg(RKVT_DBG_BUFFERS, "VTRB [%d] rendered fence file is null\n", inst->id);

	/* close the fds in consumer side */
	for (i = 0; i < buf_base->num_fds; i++) {
		rkvt_dbg(RKVT_DBG_FILE,
			"VTRB [%d] file(%p) buf(%p) buf session(%p) ino(%08lu) fcount=%d\n",
			inst->id, buffer->file_buf[i], buffer, buffer->session_pro,
			buffer->file_buf[i] ? file_inode(buffer->file_buf[i])->i_ino : 0,
			inst->fcount);
		rkvt_close_fd(session, buffer->fds_con[i]);
		inst->fcount--;
		buffer->base.fds[i] = buffer->fds_pro[i];
	}
	if (buffer->ready_render_fence) {
		fput(buffer->ready_render_fence);
		buffer->ready_render_fence = NULL;
	}

	buffer->base.crop = buf_base->crop;
	buffer->base.buf_status = RKVT_BUF_RELEASE;

	mutex_lock(&inst->lock);
	read_buf_id = atomic64_read(&inst->buf_id_generator);
	/* if producer has disconnect */
	if (!inst->producer) {
		rkvt_dbg(RKVT_DBG_BUFFERS, "VTRB [%d], buffer no producer\n", inst->id);
		buffer->base.buf_status = RKVT_BUF_FREE;
	} else if ((buffer->base.buffer_id >> 8) != (read_buf_id >> 8)) {
		dev_err(vt_dev->dev, "VTRB [%d] generation is different. cur(%lld) VS exp(%lld)\n",
			inst->id, buffer->base.buffer_id >> 8, read_buf_id >> 8);
		buffer->base.buf_status = RKVT_BUF_FREE;
	} else {
		if (buffer->session_pro &&
		    buffer->session_pro != inst->producer) {
			rkvt_dbg(RKVT_DBG_BUFFERS,
				"VTRB [%d] producer not valid, producer(%p), buf session(%p)\n",
				inst->id, inst->producer, buffer->session_pro);
			buffer->base.buf_status = RKVT_BUF_FREE;
		}

		kfifo_put(&inst->fifo_to_producer, buffer);
	}
	mutex_unlock(&inst->lock);

	if (inst->producer)
		wake_up_interruptible(&inst->wait_producer);

	rkvt_dbg(RKVT_DBG_BUFFERS, "VTRB [%d] pfd[0]:%d end\n", inst->id, buffer->fds_pro[0]);

release_fail:
	rkvt_inst_put(inst);

	return ret;
}

static int
rkvt_cancel_buf(struct rkvt_buf_data *data, struct rkvt_session *session)
{
	struct rkvt_dev *vt_dev = session->vt_dev;
	struct rkvt_instance *inst = NULL;
	struct rkvt_buf_base *buf_base = NULL;
	struct rkvt_buffer *buffer = NULL;
	int i;

	inst = rkvt_inst_get_by_tid(vt_dev, data->vt_id);
	if (!inst)
		return -EINVAL;
	if (!inst->producer || inst->producer != session) {
		rkvt_inst_put(inst);
		return -EINVAL;
	}

	rkvt_dbg(RKVT_DBG_BUFFERS, "VTCB [%d] start\n", inst->id);

	buf_base = &data->base;
	buffer = rkvt_get_free_buf(inst);
	for (i = 0; i < buf_base->num_fds; i++) {
		buffer->fds_con[i] = -1;
		buffer->fds_pro[i] = buf_base->fds[i];
		rkvt_dbg(RKVT_DBG_FILE,
			"VTCB [%d] fget file(%p) buf(%p) buf session(%p) fcount=%d\n",
			inst->id, buffer->file_buf[i], buffer,
			buffer->session_pro, inst->fcount);
	}
	// buffer id is empty, generate a new id
	if (buf_base->buffer_id == 0)
		buf_base->buffer_id = atomic64_inc_return(&inst->buf_id_generator);
	buffer->base = *buf_base;
	buffer->base.buf_status = RKVT_BUF_RELEASE;
	buffer->session_pro = session;
	buffer->cid_pro = session->cid;

	mutex_lock(&inst->lock);
	kfifo_put(&inst->fifo_to_producer, buffer);
	mutex_unlock(&inst->lock);

	if (inst->producer)
		wake_up_interruptible(&inst->wait_producer);

	rkvt_dbg(RKVT_DBG_BUFFERS, "VTCB [%d] pfd[0]:%d end\n", inst->id, buffer->fds_pro[0]);
	rkvt_inst_put(inst);

	return 0;
}

static unsigned int rkvt_ioctl_dir(unsigned int cmd)
{
	switch (cmd) {
	case RKVT_IOC_ALLOC_ID:
	case RKVT_IOC_DEQUE_BUF:
	case RKVT_IOC_ACQUIRE_BUF:
	case RKVT_IOC_CTRL:
		return _IOC_READ;
	case RKVT_IOC_QUEUE_BUF:
	case RKVT_IOC_RELEASE_BUF:
	case RKVT_IOC_CANCEL_BUF:
	case RKVT_IOC_FREE_ID:
		return _IOC_WRITE;
	default:
		return _IOC_DIR(cmd);
	}
}

static long rkvt_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	union rkvt_ioc_arg data;
	struct rkvt_session *session = filep->private_data;
	unsigned int dir = rkvt_ioctl_dir(cmd);
	struct rkvt_dev *vt_dev = session->vt_dev;
	struct rkvt_instance *inst = NULL;

	rkvt_dbg(RKVT_DBG_CMD, "rkvt ioctl cmd 0x%x size %d in\n", cmd, _IOC_SIZE(cmd));

	if (_IOC_SIZE(cmd) > sizeof(data))
		return -EINVAL;

	if (copy_from_user(&data, (void __user *)arg, _IOC_SIZE(cmd)))
		return -EFAULT;

	switch (cmd) {
	case RKVT_IOC_ALLOC_ID: {
		char name[64];

		inst = rkvt_inst_create(session->vt_dev);
		if (IS_ERR(inst))
			return PTR_ERR(inst);

		mutex_lock(&vt_dev->inst_lock);
		++vt_dev->inst_id_generator;
		ret = idr_alloc(&vt_dev->inst_idr, inst,
				vt_dev->inst_id_generator, 0, GFP_KERNEL);
		mutex_unlock(&vt_dev->inst_lock);
		if (ret < 0) {
			rkvt_inst_put(inst);
			return ret;
		}

		inst->id = ret;
		snprintf(name, sizeof(name), "instance-%d", inst->id);
		inst->debug_root =
			debugfs_create_file(name, 0664, vt_dev->debug_root,
					    inst, &dbg_instance_fops);

		mutex_lock(&vt_dev->inst_lock);
		list_add_tail(&inst->session_link, &session->list_inst);
		mutex_unlock(&vt_dev->inst_lock);

		data.alloc_data.vt_id = inst->id;
		rkvt_dbg(RKVT_DBG_USER, "rkvt alloc instance [%d], ref %d\n",
			 inst->id, kref_read(&inst->ref));
		break;
	}
	case RKVT_IOC_FREE_ID: {
		inst = rkvt_inst_get_by_tid(vt_dev, data.alloc_data.vt_id);
		/* to do free id operation check */
		if (!inst) {
			dev_err(vt_dev->dev, "destroy unknown videotunnel instance:%d\n",
			       data.alloc_data.vt_id);
			ret = -EINVAL;
		} else {
			rkvt_dbg(RKVT_DBG_USER, "rkvt free instance [%d], ref %d\n",
				 inst->id, kref_read(&inst->ref));

			mutex_lock(&vt_dev->inst_lock);
			list_del_init(&inst->session_link);
			mutex_unlock(&vt_dev->inst_lock);
			// ref put for rkvt_instance_get_by_tid
			rkvt_inst_put(inst);
			// ref put for kref_init in rkvt_inst_create
			rkvt_inst_put(inst);
		}
		break;
	}
	case RKVT_IOC_CTRL:
		ret = rkvt_ctrl_proc(&data.ctrl_data, session);
		break;
	case RKVT_IOC_QUEUE_BUF:
		ret = rkvt_queue_buf(&data.buffer_data, session);
		break;
	case RKVT_IOC_DEQUE_BUF:
		ret = rkvt_deque_buf(&data.buffer_data, session);
		break;
	case RKVT_IOC_RELEASE_BUF:
		ret = rkvt_release_buf(&data.buffer_data, session);
		break;
	case RKVT_IOC_ACQUIRE_BUF:
		ret = rkvt_acquire_buf(&data.buffer_data, session);
		break;
	case RKVT_IOC_CANCEL_BUF:
		ret = rkvt_cancel_buf(&data.buffer_data, session);
		break;
	default:
		dev_err(vt_dev->dev, "%s: cmd 0x%x not found.\n", __func__, cmd);
		return -ENOTTY;
	}

	if (dir & _IOC_READ) {
		if (copy_to_user((void __user *)arg, &data, _IOC_SIZE(cmd)))
			return -EFAULT;
	}

	return ret;
}

static const struct file_operations vt_fops = {
	.owner = THIS_MODULE,
	.open = rkvt_open,
	.release = rkvt_release,
	.unlocked_ioctl = rkvt_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = rkvt_ioctl,
#endif
};

static int rkvt_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct rkvt_dev *vdev = NULL;

	dev_info(dev, "probe start\n");
	vdev = devm_kzalloc(dev, sizeof(*vdev), GFP_KERNEL);
	if (!vdev)
		return -ENOMEM;

	vdev->dev = dev;
	vdev->dev_name = DEVICE_NAME;
	vdev->mdev.minor = MISC_DYNAMIC_MINOR;
	vdev->mdev.name = DEVICE_NAME;
	vdev->mdev.fops = &vt_fops;
	platform_set_drvdata(pdev, vdev);

	ret = misc_register(&vdev->mdev);
	if (ret) {
		dev_err(dev, "misc_register fail.\n");
		return ret;
	}

	mutex_init(&vdev->inst_lock);
	mutex_init(&vdev->session_lock);
	idr_init(&vdev->inst_idr);
	atomic64_set(&vdev->cid_generator, 0);
	INIT_LIST_HEAD(&vdev->list_inst);
	INIT_LIST_HEAD(&vdev->list_session);
	vdev->debug_root = debugfs_create_dir(DEVICE_NAME, NULL);
	if (!vdev->debug_root)
		dev_err(dev, "failed to create debugfs root directory.\n");

	dev_info(dev, "probe success\n");

	return 0;
}

static int rkvt_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rkvt_dev *vdev = platform_get_drvdata(pdev);

	dev_info(dev, "remove device\n");

	idr_destroy(&vdev->inst_idr);
	debugfs_remove_recursive(vdev->debug_root);
	misc_deregister(&vdev->mdev);

	return 0;
}

static const struct of_device_id rk_vt_match[] = {
	{
		.compatible = "rockchip,video-tunnel",
	},
	{ },
};

static struct platform_driver rk_vt_driver = {
	.probe = rkvt_probe,
	.remove = rkvt_remove,
	.driver = {
		.name = "rk_videotunnel_driver",
		.owner = THIS_MODULE,
		.of_match_table = rk_vt_match,
	},
};

module_platform_driver(rk_vt_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ROCKCHIP videotunnel driver");
