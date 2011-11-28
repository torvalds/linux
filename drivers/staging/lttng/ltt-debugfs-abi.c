/*
 * ltt-debugfs-abi.c
 *
 * Copyright 2010 (c) - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * LTTng debugfs ABI
 *
 * Mimic system calls for:
 * - session creation, returns a file descriptor or failure.
 *   - channel creation, returns a file descriptor or failure.
 *     - Operates on a session file descriptor
 *     - Takes all channel options as parameters.
 *   - stream get, returns a file descriptor or failure.
 *     - Operates on a channel file descriptor.
 *   - stream notifier get, returns a file descriptor or failure.
 *     - Operates on a channel file descriptor.
 *   - event creation, returns a file descriptor or failure.
 *     - Operates on a channel file descriptor
 *     - Takes an event name as parameter
 *     - Takes an instrumentation source as parameter
 *       - e.g. tracepoints, dynamic_probes...
 *     - Takes instrumentation source specific arguments.
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/proc_fs.h>
#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include "wrapper/vmalloc.h"	/* for wrapper_vmalloc_sync_all() */
#include "wrapper/ringbuffer/vfs.h"
#include "wrapper/poll.h"
#include "ltt-debugfs-abi.h"
#include "ltt-events.h"
#include "ltt-tracer.h"

/*
 * This is LTTng's own personal way to create a system call as an external
 * module. We use ioctl() on /sys/kernel/debug/lttng.
 */

static struct dentry *lttng_dentry;
static struct proc_dir_entry *lttng_proc_dentry;
static const struct file_operations lttng_fops;
static const struct file_operations lttng_session_fops;
static const struct file_operations lttng_channel_fops;
static const struct file_operations lttng_metadata_fops;
static const struct file_operations lttng_event_fops;

/*
 * Teardown management: opened file descriptors keep a refcount on the module,
 * so it can only exit when all file descriptors are closed.
 */

enum channel_type {
	PER_CPU_CHANNEL,
	METADATA_CHANNEL,
};

static
int lttng_abi_create_session(void)
{
	struct ltt_session *session;
	struct file *session_file;
	int session_fd, ret;

	session = ltt_session_create();
	if (!session)
		return -ENOMEM;
	session_fd = get_unused_fd();
	if (session_fd < 0) {
		ret = session_fd;
		goto fd_error;
	}
	session_file = anon_inode_getfile("[lttng_session]",
					  &lttng_session_fops,
					  session, O_RDWR);
	if (IS_ERR(session_file)) {
		ret = PTR_ERR(session_file);
		goto file_error;
	}
	session->file = session_file;
	fd_install(session_fd, session_file);
	return session_fd;

file_error:
	put_unused_fd(session_fd);
fd_error:
	ltt_session_destroy(session);
	return ret;
}

static
int lttng_abi_tracepoint_list(void)
{
	struct file *tracepoint_list_file;
	int file_fd, ret;

	file_fd = get_unused_fd();
	if (file_fd < 0) {
		ret = file_fd;
		goto fd_error;
	}

	tracepoint_list_file = anon_inode_getfile("[lttng_session]",
					  &lttng_tracepoint_list_fops,
					  NULL, O_RDWR);
	if (IS_ERR(tracepoint_list_file)) {
		ret = PTR_ERR(tracepoint_list_file);
		goto file_error;
	}
	ret = lttng_tracepoint_list_fops.open(NULL, tracepoint_list_file);
	if (ret < 0)
		goto open_error;
	fd_install(file_fd, tracepoint_list_file);
	if (file_fd < 0) {
		ret = file_fd;
		goto fd_error;
	}
	return file_fd;

open_error:
	fput(tracepoint_list_file);
file_error:
	put_unused_fd(file_fd);
fd_error:
	return ret;
}

static
long lttng_abi_tracer_version(struct file *file, 
	struct lttng_kernel_tracer_version __user *uversion_param)
{
	struct lttng_kernel_tracer_version v;

	v.version = LTTNG_VERSION;
	v.patchlevel = LTTNG_PATCHLEVEL;
	v.sublevel = LTTNG_SUBLEVEL;

	if (copy_to_user(uversion_param, &v, sizeof(v)))
		return -EFAULT;
	return 0;
}

static
long lttng_abi_add_context(struct file *file,
	struct lttng_kernel_context __user *ucontext_param,
	struct lttng_ctx **ctx, struct ltt_session *session)
{
	struct lttng_kernel_context context_param;

	if (session->been_active)
		return -EPERM;

	if (copy_from_user(&context_param, ucontext_param, sizeof(context_param)))
		return -EFAULT;

	switch (context_param.ctx) {
	case LTTNG_KERNEL_CONTEXT_PID:
		return lttng_add_pid_to_ctx(ctx);
	case LTTNG_KERNEL_CONTEXT_PRIO:
		return lttng_add_prio_to_ctx(ctx);
	case LTTNG_KERNEL_CONTEXT_NICE:
		return lttng_add_nice_to_ctx(ctx);
	case LTTNG_KERNEL_CONTEXT_VPID:
		return lttng_add_vpid_to_ctx(ctx);
	case LTTNG_KERNEL_CONTEXT_TID:
		return lttng_add_tid_to_ctx(ctx);
	case LTTNG_KERNEL_CONTEXT_VTID:
		return lttng_add_vtid_to_ctx(ctx);
	case LTTNG_KERNEL_CONTEXT_PPID:
		return lttng_add_ppid_to_ctx(ctx);
	case LTTNG_KERNEL_CONTEXT_VPPID:
		return lttng_add_vppid_to_ctx(ctx);
	case LTTNG_KERNEL_CONTEXT_PERF_COUNTER:
		context_param.u.perf_counter.name[LTTNG_SYM_NAME_LEN - 1] = '\0';
		return lttng_add_perf_counter_to_ctx(context_param.u.perf_counter.type,
				context_param.u.perf_counter.config,
				context_param.u.perf_counter.name,
				ctx);
	case LTTNG_KERNEL_CONTEXT_PROCNAME:
		return lttng_add_procname_to_ctx(ctx);
	default:
		return -EINVAL;
	}
}

/**
 *	lttng_ioctl - lttng syscall through ioctl
 *
 *	@file: the file
 *	@cmd: the command
 *	@arg: command arg
 *
 *	This ioctl implements lttng commands:
 *	LTTNG_KERNEL_SESSION
 *		Returns a LTTng trace session file descriptor
 *	LTTNG_KERNEL_TRACER_VERSION
 *		Returns the LTTng kernel tracer version
 *	LTTNG_KERNEL_TRACEPOINT_LIST
 *		Returns a file descriptor listing available tracepoints
 *	LTTNG_KERNEL_WAIT_QUIESCENT
 *		Returns after all previously running probes have completed
 *
 * The returned session will be deleted when its file descriptor is closed.
 */
static
long lttng_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case LTTNG_KERNEL_SESSION:
		return lttng_abi_create_session();
	case LTTNG_KERNEL_TRACER_VERSION:
		return lttng_abi_tracer_version(file,
				(struct lttng_kernel_tracer_version __user *) arg);
	case LTTNG_KERNEL_TRACEPOINT_LIST:
		return lttng_abi_tracepoint_list();
	case LTTNG_KERNEL_WAIT_QUIESCENT:
		synchronize_trace();
		return 0;
	case LTTNG_KERNEL_CALIBRATE:
	{
		struct lttng_kernel_calibrate __user *ucalibrate =
			(struct lttng_kernel_calibrate __user *) arg;
		struct lttng_kernel_calibrate calibrate;
		int ret;

		if (copy_from_user(&calibrate, ucalibrate, sizeof(calibrate)))
			return -EFAULT;
		ret = lttng_calibrate(&calibrate);
		if (copy_to_user(ucalibrate, &calibrate, sizeof(calibrate)))
			return -EFAULT;
		return ret;
	}
	default:
		return -ENOIOCTLCMD;
	}
}

static const struct file_operations lttng_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = lttng_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = lttng_ioctl,
#endif
};

/*
 * We tolerate no failure in this function (if one happens, we print a dmesg
 * error, but cannot return any error, because the channel information is
 * invariant.
 */
static
void lttng_metadata_create_events(struct file *channel_file)
{
	struct ltt_channel *channel = channel_file->private_data;
	static struct lttng_kernel_event metadata_params = {
		.instrumentation = LTTNG_KERNEL_TRACEPOINT,
		.name = "lttng_metadata",
	};
	struct ltt_event *event;

	/*
	 * We tolerate no failure path after event creation. It will stay
	 * invariant for the rest of the session.
	 */
	event = ltt_event_create(channel, &metadata_params, NULL, NULL);
	if (!event) {
		goto create_error;
	}
	return;

create_error:
	WARN_ON(1);
	return;		/* not allowed to return error */
}

static
int lttng_abi_create_channel(struct file *session_file,
			     struct lttng_kernel_channel __user *uchan_param,
			     enum channel_type channel_type)
{
	struct ltt_session *session = session_file->private_data;
	const struct file_operations *fops = NULL;
	const char *transport_name;
	struct ltt_channel *chan;
	struct file *chan_file;
	struct lttng_kernel_channel chan_param;
	int chan_fd;
	int ret = 0;

	if (copy_from_user(&chan_param, uchan_param, sizeof(chan_param)))
		return -EFAULT;
	chan_fd = get_unused_fd();
	if (chan_fd < 0) {
		ret = chan_fd;
		goto fd_error;
	}
	switch (channel_type) {
	case PER_CPU_CHANNEL:
		fops = &lttng_channel_fops;
		break;
	case METADATA_CHANNEL:
		fops = &lttng_metadata_fops;
		break;
	}
		
	chan_file = anon_inode_getfile("[lttng_channel]",
				       fops,
				       NULL, O_RDWR);
	if (IS_ERR(chan_file)) {
		ret = PTR_ERR(chan_file);
		goto file_error;
	}
	switch (channel_type) {
	case PER_CPU_CHANNEL:
		if (chan_param.output == LTTNG_KERNEL_SPLICE) {
			transport_name = chan_param.overwrite ?
				"relay-overwrite" : "relay-discard";
		} else if (chan_param.output == LTTNG_KERNEL_MMAP) {
			transport_name = chan_param.overwrite ?
				"relay-overwrite-mmap" : "relay-discard-mmap";
		} else {
			return -EINVAL;
		}
		break;
	case METADATA_CHANNEL:
		if (chan_param.output == LTTNG_KERNEL_SPLICE)
			transport_name = "relay-metadata";
		else if (chan_param.output == LTTNG_KERNEL_MMAP)
			transport_name = "relay-metadata-mmap";
		else
			return -EINVAL;
		break;
	default:
		transport_name = "<unknown>";
		break;
	}
	/*
	 * We tolerate no failure path after channel creation. It will stay
	 * invariant for the rest of the session.
	 */
	chan = ltt_channel_create(session, transport_name, NULL,
				  chan_param.subbuf_size,
				  chan_param.num_subbuf,
				  chan_param.switch_timer_interval,
				  chan_param.read_timer_interval);
	if (!chan) {
		ret = -EINVAL;
		goto chan_error;
	}
	chan->file = chan_file;
	chan_file->private_data = chan;
	fd_install(chan_fd, chan_file);
	if (channel_type == METADATA_CHANNEL) {
		session->metadata = chan;
		lttng_metadata_create_events(chan_file);
	}

	/* The channel created holds a reference on the session */
	atomic_long_inc(&session_file->f_count);

	return chan_fd;

chan_error:
	fput(chan_file);
file_error:
	put_unused_fd(chan_fd);
fd_error:
	return ret;
}

/**
 *	lttng_session_ioctl - lttng session fd ioctl
 *
 *	@file: the file
 *	@cmd: the command
 *	@arg: command arg
 *
 *	This ioctl implements lttng commands:
 *	LTTNG_KERNEL_CHANNEL
 *		Returns a LTTng channel file descriptor
 *	LTTNG_KERNEL_ENABLE
 *		Enables tracing for a session (weak enable)
 *	LTTNG_KERNEL_DISABLE
 *		Disables tracing for a session (strong disable)
 *	LTTNG_KERNEL_METADATA
 *		Returns a LTTng metadata file descriptor
 *
 * The returned channel will be deleted when its file descriptor is closed.
 */
static
long lttng_session_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ltt_session *session = file->private_data;

	switch (cmd) {
	case LTTNG_KERNEL_CHANNEL:
		return lttng_abi_create_channel(file,
				(struct lttng_kernel_channel __user *) arg,
				PER_CPU_CHANNEL);
	case LTTNG_KERNEL_SESSION_START:
	case LTTNG_KERNEL_ENABLE:
		return ltt_session_enable(session);
	case LTTNG_KERNEL_SESSION_STOP:
	case LTTNG_KERNEL_DISABLE:
		return ltt_session_disable(session);
	case LTTNG_KERNEL_METADATA:
		return lttng_abi_create_channel(file,
				(struct lttng_kernel_channel __user *) arg,
				METADATA_CHANNEL);
	default:
		return -ENOIOCTLCMD;
	}
}

/*
 * Called when the last file reference is dropped.
 *
 * Big fat note: channels and events are invariant for the whole session after
 * their creation. So this session destruction also destroys all channel and
 * event structures specific to this session (they are not destroyed when their
 * individual file is released).
 */
static
int lttng_session_release(struct inode *inode, struct file *file)
{
	struct ltt_session *session = file->private_data;

	if (session)
		ltt_session_destroy(session);
	return 0;
}

static const struct file_operations lttng_session_fops = {
	.owner = THIS_MODULE,
	.release = lttng_session_release,
	.unlocked_ioctl = lttng_session_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = lttng_session_ioctl,
#endif
};

static
int lttng_abi_open_stream(struct file *channel_file)
{
	struct ltt_channel *channel = channel_file->private_data;
	struct lib_ring_buffer *buf;
	int stream_fd, ret;
	struct file *stream_file;

	buf = channel->ops->buffer_read_open(channel->chan);
	if (!buf)
		return -ENOENT;

	stream_fd = get_unused_fd();
	if (stream_fd < 0) {
		ret = stream_fd;
		goto fd_error;
	}
	stream_file = anon_inode_getfile("[lttng_stream]",
					 &lib_ring_buffer_file_operations,
					 buf, O_RDWR);
	if (IS_ERR(stream_file)) {
		ret = PTR_ERR(stream_file);
		goto file_error;
	}
	/*
	 * OPEN_FMODE, called within anon_inode_getfile/alloc_file, don't honor
	 * FMODE_LSEEK, FMODE_PREAD nor FMODE_PWRITE. We need to read from this
	 * file descriptor, so we set FMODE_PREAD here.
	 */
	stream_file->f_mode |= FMODE_PREAD;
	fd_install(stream_fd, stream_file);
	/*
	 * The stream holds a reference to the channel within the generic ring
	 * buffer library, so no need to hold a refcount on the channel and
	 * session files here.
	 */
	return stream_fd;

file_error:
	put_unused_fd(stream_fd);
fd_error:
	channel->ops->buffer_read_close(buf);
	return ret;
}

static
int lttng_abi_create_event(struct file *channel_file,
			   struct lttng_kernel_event __user *uevent_param)
{
	struct ltt_channel *channel = channel_file->private_data;
	struct ltt_event *event;
	struct lttng_kernel_event event_param;
	int event_fd, ret;
	struct file *event_file;

	if (copy_from_user(&event_param, uevent_param, sizeof(event_param)))
		return -EFAULT;
	event_param.name[LTTNG_SYM_NAME_LEN - 1] = '\0';
	switch (event_param.instrumentation) {
	case LTTNG_KERNEL_KRETPROBE:
		event_param.u.kretprobe.symbol_name[LTTNG_SYM_NAME_LEN - 1] = '\0';
		break;
	case LTTNG_KERNEL_KPROBE:
		event_param.u.kprobe.symbol_name[LTTNG_SYM_NAME_LEN - 1] = '\0';
		break;
	case LTTNG_KERNEL_FUNCTION:
		event_param.u.ftrace.symbol_name[LTTNG_SYM_NAME_LEN - 1] = '\0';
		break;
	default:
		break;
	}
	switch (event_param.instrumentation) {
	default:
		event_fd = get_unused_fd();
		if (event_fd < 0) {
			ret = event_fd;
			goto fd_error;
		}
		event_file = anon_inode_getfile("[lttng_event]",
						&lttng_event_fops,
						NULL, O_RDWR);
		if (IS_ERR(event_file)) {
			ret = PTR_ERR(event_file);
			goto file_error;
		}
		/*
		 * We tolerate no failure path after event creation. It
		 * will stay invariant for the rest of the session.
		 */
		event = ltt_event_create(channel, &event_param, NULL, NULL);
		if (!event) {
			ret = -EINVAL;
			goto event_error;
		}
		event_file->private_data = event;
		fd_install(event_fd, event_file);
		/* The event holds a reference on the channel */
		atomic_long_inc(&channel_file->f_count);
		break;
	case LTTNG_KERNEL_SYSCALL:
		/*
		 * Only all-syscall tracing supported for now.
		 */
		if (event_param.name[0] != '\0')
			return -EINVAL;
		ret = lttng_syscalls_register(channel, NULL);
		if (ret)
			goto fd_error;
		event_fd = 0;
		break;
	}
	return event_fd;

event_error:
	fput(event_file);
file_error:
	put_unused_fd(event_fd);
fd_error:
	return ret;
}

/**
 *	lttng_channel_ioctl - lttng syscall through ioctl
 *
 *	@file: the file
 *	@cmd: the command
 *	@arg: command arg
 *
 *	This ioctl implements lttng commands:
 *      LTTNG_KERNEL_STREAM
 *              Returns an event stream file descriptor or failure.
 *              (typically, one event stream records events from one CPU)
 *	LTTNG_KERNEL_EVENT
 *		Returns an event file descriptor or failure.
 *	LTTNG_KERNEL_CONTEXT
 *		Prepend a context field to each event in the channel
 *	LTTNG_KERNEL_ENABLE
 *		Enable recording for events in this channel (weak enable)
 *	LTTNG_KERNEL_DISABLE
 *		Disable recording for events in this channel (strong disable)
 *
 * Channel and event file descriptors also hold a reference on the session.
 */
static
long lttng_channel_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ltt_channel *channel = file->private_data;

	switch (cmd) {
	case LTTNG_KERNEL_STREAM:
		return lttng_abi_open_stream(file);
	case LTTNG_KERNEL_EVENT:
		return lttng_abi_create_event(file, (struct lttng_kernel_event __user *) arg);
	case LTTNG_KERNEL_CONTEXT:
		return lttng_abi_add_context(file,
				(struct lttng_kernel_context __user *) arg,
				&channel->ctx, channel->session);
	case LTTNG_KERNEL_ENABLE:
		return ltt_channel_enable(channel);
	case LTTNG_KERNEL_DISABLE:
		return ltt_channel_disable(channel);
	default:
		return -ENOIOCTLCMD;
	}
}

/**
 *	lttng_metadata_ioctl - lttng syscall through ioctl
 *
 *	@file: the file
 *	@cmd: the command
 *	@arg: command arg
 *
 *	This ioctl implements lttng commands:
 *      LTTNG_KERNEL_STREAM
 *              Returns an event stream file descriptor or failure.
 *
 * Channel and event file descriptors also hold a reference on the session.
 */
static
long lttng_metadata_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case LTTNG_KERNEL_STREAM:
		return lttng_abi_open_stream(file);
	default:
		return -ENOIOCTLCMD;
	}
}

/**
 *	lttng_channel_poll - lttng stream addition/removal monitoring
 *
 *	@file: the file
 *	@wait: poll table
 */
unsigned int lttng_channel_poll(struct file *file, poll_table *wait)
{
	struct ltt_channel *channel = file->private_data;
	unsigned int mask = 0;

	if (file->f_mode & FMODE_READ) {
		poll_wait_set_exclusive(wait);
		poll_wait(file, channel->ops->get_hp_wait_queue(channel->chan),
			  wait);

		if (channel->ops->is_disabled(channel->chan))
			return POLLERR;
		if (channel->ops->is_finalized(channel->chan))
			return POLLHUP;
		if (channel->ops->buffer_has_read_closed_stream(channel->chan))
			return POLLIN | POLLRDNORM;
		return 0;
	}
	return mask;

}

static
int lttng_channel_release(struct inode *inode, struct file *file)
{
	struct ltt_channel *channel = file->private_data;

	if (channel)
		fput(channel->session->file);
	return 0;
}

static const struct file_operations lttng_channel_fops = {
	.owner = THIS_MODULE,
	.release = lttng_channel_release,
	.poll = lttng_channel_poll,
	.unlocked_ioctl = lttng_channel_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = lttng_channel_ioctl,
#endif
};

static const struct file_operations lttng_metadata_fops = {
	.owner = THIS_MODULE,
	.release = lttng_channel_release,
	.unlocked_ioctl = lttng_metadata_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = lttng_metadata_ioctl,
#endif
};

/**
 *	lttng_event_ioctl - lttng syscall through ioctl
 *
 *	@file: the file
 *	@cmd: the command
 *	@arg: command arg
 *
 *	This ioctl implements lttng commands:
 *	LTTNG_KERNEL_CONTEXT
 *		Prepend a context field to each record of this event
 *	LTTNG_KERNEL_ENABLE
 *		Enable recording for this event (weak enable)
 *	LTTNG_KERNEL_DISABLE
 *		Disable recording for this event (strong disable)
 */
static
long lttng_event_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ltt_event *event = file->private_data;

	switch (cmd) {
	case LTTNG_KERNEL_CONTEXT:
		return lttng_abi_add_context(file,
				(struct lttng_kernel_context __user *) arg,
				&event->ctx, event->chan->session);
	case LTTNG_KERNEL_ENABLE:
		return ltt_event_enable(event);
	case LTTNG_KERNEL_DISABLE:
		return ltt_event_disable(event);
	default:
		return -ENOIOCTLCMD;
	}
}

static
int lttng_event_release(struct inode *inode, struct file *file)
{
	struct ltt_event *event = file->private_data;

	if (event)
		fput(event->chan->file);
	return 0;
}

/* TODO: filter control ioctl */
static const struct file_operations lttng_event_fops = {
	.owner = THIS_MODULE,
	.release = lttng_event_release,
	.unlocked_ioctl = lttng_event_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = lttng_event_ioctl,
#endif
};

int __init ltt_debugfs_abi_init(void)
{
	int ret = 0;

	wrapper_vmalloc_sync_all();
	lttng_dentry = debugfs_create_file("lttng", S_IWUSR, NULL, NULL,
					&lttng_fops);
	if (IS_ERR(lttng_dentry))
		lttng_dentry = NULL;

	lttng_proc_dentry = proc_create_data("lttng", S_IWUSR, NULL,
					&lttng_fops, NULL);
	
	if (!lttng_dentry && !lttng_proc_dentry) {
		printk(KERN_ERR "Error creating LTTng control file\n");
		ret = -ENOMEM;
		goto error;
	}
error:
	return ret;
}

void __exit ltt_debugfs_abi_exit(void)
{
	if (lttng_dentry)
		debugfs_remove(lttng_dentry);
	if (lttng_proc_dentry)
		remove_proc_entry("lttng", NULL);
}
