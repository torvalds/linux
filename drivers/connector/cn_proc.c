// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * cn_proc.c - process events connector
 *
 * Copyright (C) Matt Helsley, IBM Corp. 2005
 * Based on cn_fork.c by Guillaume Thouvenin <guillaume.thouvenin@bull.net>
 * Original copyright notice follows:
 * Copyright (C) 2005 BULL SA.
 */

#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/init.h>
#include <linux/connector.h>
#include <linux/gfp.h>
#include <linux/ptrace.h>
#include <linux/atomic.h>
#include <linux/pid_namespace.h>

#include <linux/cn_proc.h>
#include <linux/local_lock.h>

/*
 * Size of a cn_msg followed by a proc_event structure.  Since the
 * sizeof struct cn_msg is a multiple of 4 bytes, but not 8 bytes, we
 * add one 4-byte word to the size here, and then start the actual
 * cn_msg structure 4 bytes into the stack buffer.  The result is that
 * the immediately following proc_event structure is aligned to 8 bytes.
 */
#define CN_PROC_MSG_SIZE (sizeof(struct cn_msg) + sizeof(struct proc_event) + 4)

/* See comment above; we test our assumption about sizeof struct cn_msg here. */
static inline struct cn_msg *buffer_to_cn_msg(__u8 *buffer)
{
	BUILD_BUG_ON(sizeof(struct cn_msg) != 20);
	return (struct cn_msg *)(buffer + 4);
}

static atomic_t proc_event_num_listeners = ATOMIC_INIT(0);
static struct cb_id cn_proc_event_id = { CN_IDX_PROC, CN_VAL_PROC };

/* local_event.count is used as the sequence number of the netlink message */
struct local_event {
	local_lock_t lock;
	__u32 count;
};
static DEFINE_PER_CPU(struct local_event, local_event) = {
	.lock = INIT_LOCAL_LOCK(lock),
};

static int cn_filter(struct sock *dsk, struct sk_buff *skb, void *data)
{
	__u32 what, exit_code, *ptr;
	enum proc_cn_mcast_op mc_op;
	uintptr_t val;

	if (!dsk || !dsk->sk_user_data || !data)
		return 0;

	ptr = (__u32 *)data;
	what = *ptr++;
	exit_code = *ptr;
	val = ((struct proc_input *)(dsk->sk_user_data))->event_type;
	mc_op = ((struct proc_input *)(dsk->sk_user_data))->mcast_op;

	if (mc_op == PROC_CN_MCAST_IGNORE)
		return 1;

	if ((__u32)val == PROC_EVENT_ALL)
		return 0;

	/*
	 * Drop packet if we have to report only non-zero exit status
	 * (PROC_EVENT_NONZERO_EXIT) and exit status is 0
	 */
	if (((__u32)val & PROC_EVENT_NONZERO_EXIT) &&
	    (what == PROC_EVENT_EXIT)) {
		if (exit_code)
			return 0;
	}

	if ((__u32)val & what)
		return 0;

	return 1;
}

static inline void send_msg(struct cn_msg *msg)
{
	__u32 filter_data[2];

	local_lock(&local_event.lock);

	msg->seq = __this_cpu_inc_return(local_event.count) - 1;
	((struct proc_event *)msg->data)->cpu = smp_processor_id();

	/*
	 * local_lock() disables preemption during send to ensure the messages
	 * are ordered according to their sequence numbers.
	 *
	 * If cn_netlink_send() fails, the data is not sent.
	 */
	filter_data[0] = ((struct proc_event *)msg->data)->what;
	if (filter_data[0] == PROC_EVENT_EXIT) {
		filter_data[1] =
		((struct proc_event *)msg->data)->event_data.exit.exit_code;
	} else {
		filter_data[1] = 0;
	}

	cn_netlink_send_mult(msg, msg->len, 0, CN_IDX_PROC, GFP_NOWAIT,
			     cn_filter, (void *)filter_data);

	local_unlock(&local_event.lock);
}

void proc_fork_connector(struct task_struct *task)
{
	struct cn_msg *msg;
	struct proc_event *ev;
	__u8 buffer[CN_PROC_MSG_SIZE] __aligned(8);
	struct task_struct *parent;

	if (atomic_read(&proc_event_num_listeners) < 1)
		return;

	msg = buffer_to_cn_msg(buffer);
	ev = (struct proc_event *)msg->data;
	memset(&ev->event_data, 0, sizeof(ev->event_data));
	ev->timestamp_ns = ktime_get_ns();
	ev->what = PROC_EVENT_FORK;
	rcu_read_lock();
	parent = rcu_dereference(task->real_parent);
	ev->event_data.fork.parent_pid = parent->pid;
	ev->event_data.fork.parent_tgid = parent->tgid;
	rcu_read_unlock();
	ev->event_data.fork.child_pid = task->pid;
	ev->event_data.fork.child_tgid = task->tgid;

	memcpy(&msg->id, &cn_proc_event_id, sizeof(msg->id));
	msg->ack = 0; /* not used */
	msg->len = sizeof(*ev);
	msg->flags = 0; /* not used */
	send_msg(msg);
}

void proc_exec_connector(struct task_struct *task)
{
	struct cn_msg *msg;
	struct proc_event *ev;
	__u8 buffer[CN_PROC_MSG_SIZE] __aligned(8);

	if (atomic_read(&proc_event_num_listeners) < 1)
		return;

	msg = buffer_to_cn_msg(buffer);
	ev = (struct proc_event *)msg->data;
	memset(&ev->event_data, 0, sizeof(ev->event_data));
	ev->timestamp_ns = ktime_get_ns();
	ev->what = PROC_EVENT_EXEC;
	ev->event_data.exec.process_pid = task->pid;
	ev->event_data.exec.process_tgid = task->tgid;

	memcpy(&msg->id, &cn_proc_event_id, sizeof(msg->id));
	msg->ack = 0; /* not used */
	msg->len = sizeof(*ev);
	msg->flags = 0; /* not used */
	send_msg(msg);
}

void proc_id_connector(struct task_struct *task, int which_id)
{
	struct cn_msg *msg;
	struct proc_event *ev;
	__u8 buffer[CN_PROC_MSG_SIZE] __aligned(8);
	const struct cred *cred;

	if (atomic_read(&proc_event_num_listeners) < 1)
		return;

	msg = buffer_to_cn_msg(buffer);
	ev = (struct proc_event *)msg->data;
	memset(&ev->event_data, 0, sizeof(ev->event_data));
	ev->what = which_id;
	ev->event_data.id.process_pid = task->pid;
	ev->event_data.id.process_tgid = task->tgid;
	rcu_read_lock();
	cred = __task_cred(task);
	if (which_id == PROC_EVENT_UID) {
		ev->event_data.id.r.ruid = from_kuid_munged(&init_user_ns, cred->uid);
		ev->event_data.id.e.euid = from_kuid_munged(&init_user_ns, cred->euid);
	} else if (which_id == PROC_EVENT_GID) {
		ev->event_data.id.r.rgid = from_kgid_munged(&init_user_ns, cred->gid);
		ev->event_data.id.e.egid = from_kgid_munged(&init_user_ns, cred->egid);
	} else {
		rcu_read_unlock();
		return;
	}
	rcu_read_unlock();
	ev->timestamp_ns = ktime_get_ns();

	memcpy(&msg->id, &cn_proc_event_id, sizeof(msg->id));
	msg->ack = 0; /* not used */
	msg->len = sizeof(*ev);
	msg->flags = 0; /* not used */
	send_msg(msg);
}

void proc_sid_connector(struct task_struct *task)
{
	struct cn_msg *msg;
	struct proc_event *ev;
	__u8 buffer[CN_PROC_MSG_SIZE] __aligned(8);

	if (atomic_read(&proc_event_num_listeners) < 1)
		return;

	msg = buffer_to_cn_msg(buffer);
	ev = (struct proc_event *)msg->data;
	memset(&ev->event_data, 0, sizeof(ev->event_data));
	ev->timestamp_ns = ktime_get_ns();
	ev->what = PROC_EVENT_SID;
	ev->event_data.sid.process_pid = task->pid;
	ev->event_data.sid.process_tgid = task->tgid;

	memcpy(&msg->id, &cn_proc_event_id, sizeof(msg->id));
	msg->ack = 0; /* not used */
	msg->len = sizeof(*ev);
	msg->flags = 0; /* not used */
	send_msg(msg);
}

void proc_ptrace_connector(struct task_struct *task, int ptrace_id)
{
	struct cn_msg *msg;
	struct proc_event *ev;
	__u8 buffer[CN_PROC_MSG_SIZE] __aligned(8);

	if (atomic_read(&proc_event_num_listeners) < 1)
		return;

	msg = buffer_to_cn_msg(buffer);
	ev = (struct proc_event *)msg->data;
	memset(&ev->event_data, 0, sizeof(ev->event_data));
	ev->timestamp_ns = ktime_get_ns();
	ev->what = PROC_EVENT_PTRACE;
	ev->event_data.ptrace.process_pid  = task->pid;
	ev->event_data.ptrace.process_tgid = task->tgid;
	if (ptrace_id == PTRACE_ATTACH) {
		ev->event_data.ptrace.tracer_pid  = current->pid;
		ev->event_data.ptrace.tracer_tgid = current->tgid;
	} else if (ptrace_id == PTRACE_DETACH) {
		ev->event_data.ptrace.tracer_pid  = 0;
		ev->event_data.ptrace.tracer_tgid = 0;
	} else
		return;

	memcpy(&msg->id, &cn_proc_event_id, sizeof(msg->id));
	msg->ack = 0; /* not used */
	msg->len = sizeof(*ev);
	msg->flags = 0; /* not used */
	send_msg(msg);
}

void proc_comm_connector(struct task_struct *task)
{
	struct cn_msg *msg;
	struct proc_event *ev;
	__u8 buffer[CN_PROC_MSG_SIZE] __aligned(8);

	if (atomic_read(&proc_event_num_listeners) < 1)
		return;

	msg = buffer_to_cn_msg(buffer);
	ev = (struct proc_event *)msg->data;
	memset(&ev->event_data, 0, sizeof(ev->event_data));
	ev->timestamp_ns = ktime_get_ns();
	ev->what = PROC_EVENT_COMM;
	ev->event_data.comm.process_pid  = task->pid;
	ev->event_data.comm.process_tgid = task->tgid;
	get_task_comm(ev->event_data.comm.comm, task);

	memcpy(&msg->id, &cn_proc_event_id, sizeof(msg->id));
	msg->ack = 0; /* not used */
	msg->len = sizeof(*ev);
	msg->flags = 0; /* not used */
	send_msg(msg);
}

void proc_coredump_connector(struct task_struct *task)
{
	struct cn_msg *msg;
	struct proc_event *ev;
	struct task_struct *parent;
	__u8 buffer[CN_PROC_MSG_SIZE] __aligned(8);

	if (atomic_read(&proc_event_num_listeners) < 1)
		return;

	msg = buffer_to_cn_msg(buffer);
	ev = (struct proc_event *)msg->data;
	memset(&ev->event_data, 0, sizeof(ev->event_data));
	ev->timestamp_ns = ktime_get_ns();
	ev->what = PROC_EVENT_COREDUMP;
	ev->event_data.coredump.process_pid = task->pid;
	ev->event_data.coredump.process_tgid = task->tgid;

	rcu_read_lock();
	if (pid_alive(task)) {
		parent = rcu_dereference(task->real_parent);
		ev->event_data.coredump.parent_pid = parent->pid;
		ev->event_data.coredump.parent_tgid = parent->tgid;
	}
	rcu_read_unlock();

	memcpy(&msg->id, &cn_proc_event_id, sizeof(msg->id));
	msg->ack = 0; /* not used */
	msg->len = sizeof(*ev);
	msg->flags = 0; /* not used */
	send_msg(msg);
}

void proc_exit_connector(struct task_struct *task)
{
	struct cn_msg *msg;
	struct proc_event *ev;
	struct task_struct *parent;
	__u8 buffer[CN_PROC_MSG_SIZE] __aligned(8);

	if (atomic_read(&proc_event_num_listeners) < 1)
		return;

	msg = buffer_to_cn_msg(buffer);
	ev = (struct proc_event *)msg->data;
	memset(&ev->event_data, 0, sizeof(ev->event_data));
	ev->timestamp_ns = ktime_get_ns();
	ev->what = PROC_EVENT_EXIT;
	ev->event_data.exit.process_pid = task->pid;
	ev->event_data.exit.process_tgid = task->tgid;
	ev->event_data.exit.exit_code = task->exit_code;
	ev->event_data.exit.exit_signal = task->exit_signal;

	rcu_read_lock();
	if (pid_alive(task)) {
		parent = rcu_dereference(task->real_parent);
		ev->event_data.exit.parent_pid = parent->pid;
		ev->event_data.exit.parent_tgid = parent->tgid;
	}
	rcu_read_unlock();

	memcpy(&msg->id, &cn_proc_event_id, sizeof(msg->id));
	msg->ack = 0; /* not used */
	msg->len = sizeof(*ev);
	msg->flags = 0; /* not used */
	send_msg(msg);
}

/*
 * Send an acknowledgement message to userspace
 *
 * Use 0 for success, EFOO otherwise.
 * Note: this is the negative of conventional kernel error
 * values because it's not being returned via syscall return
 * mechanisms.
 */
static void cn_proc_ack(int err, int rcvd_seq, int rcvd_ack)
{
	struct cn_msg *msg;
	struct proc_event *ev;
	__u8 buffer[CN_PROC_MSG_SIZE] __aligned(8);

	if (atomic_read(&proc_event_num_listeners) < 1)
		return;

	msg = buffer_to_cn_msg(buffer);
	ev = (struct proc_event *)msg->data;
	memset(&ev->event_data, 0, sizeof(ev->event_data));
	msg->seq = rcvd_seq;
	ev->timestamp_ns = ktime_get_ns();
	ev->cpu = -1;
	ev->what = PROC_EVENT_NONE;
	ev->event_data.ack.err = err;
	memcpy(&msg->id, &cn_proc_event_id, sizeof(msg->id));
	msg->ack = rcvd_ack + 1;
	msg->len = sizeof(*ev);
	msg->flags = 0; /* not used */
	send_msg(msg);
}

/**
 * cn_proc_mcast_ctl
 * @msg: message sent from userspace via the connector
 * @nsp: NETLINK_CB of the client's socket buffer
 */
static void cn_proc_mcast_ctl(struct cn_msg *msg,
			      struct netlink_skb_parms *nsp)
{
	enum proc_cn_mcast_op mc_op = 0, prev_mc_op = 0;
	struct proc_input *pinput = NULL;
	enum proc_cn_event ev_type = 0;
	int err = 0, initial = 0;
	struct sock *sk = NULL;

	/* 
	 * Events are reported with respect to the initial pid
	 * and user namespaces so ignore requestors from
	 * other namespaces.
	 */
	if ((current_user_ns() != &init_user_ns) ||
	    !task_is_in_init_pid_ns(current))
		return;

	if (msg->len == sizeof(*pinput)) {
		pinput = (struct proc_input *)msg->data;
		mc_op = pinput->mcast_op;
		ev_type = pinput->event_type;
	} else if (msg->len == sizeof(mc_op)) {
		mc_op = *((enum proc_cn_mcast_op *)msg->data);
		ev_type = PROC_EVENT_ALL;
	} else {
		return;
	}

	ev_type = valid_event((enum proc_cn_event)ev_type);

	if (ev_type == PROC_EVENT_NONE)
		ev_type = PROC_EVENT_ALL;

	if (nsp->sk) {
		sk = nsp->sk;
		if (sk->sk_user_data == NULL) {
			sk->sk_user_data = kzalloc(sizeof(struct proc_input),
						   GFP_KERNEL);
			if (sk->sk_user_data == NULL) {
				err = ENOMEM;
				goto out;
			}
			initial = 1;
		} else {
			prev_mc_op =
			((struct proc_input *)(sk->sk_user_data))->mcast_op;
		}
		((struct proc_input *)(sk->sk_user_data))->event_type =
			ev_type;
		((struct proc_input *)(sk->sk_user_data))->mcast_op = mc_op;
	}

	switch (mc_op) {
	case PROC_CN_MCAST_LISTEN:
		if (initial || (prev_mc_op != PROC_CN_MCAST_LISTEN))
			atomic_inc(&proc_event_num_listeners);
		break;
	case PROC_CN_MCAST_IGNORE:
		if (!initial && (prev_mc_op != PROC_CN_MCAST_IGNORE))
			atomic_dec(&proc_event_num_listeners);
		((struct proc_input *)(sk->sk_user_data))->event_type =
			PROC_EVENT_NONE;
		break;
	default:
		err = EINVAL;
		break;
	}

out:
	cn_proc_ack(err, msg->seq, msg->ack);
}

/*
 * cn_proc_init - initialization entry point
 *
 * Adds the connector callback to the connector driver.
 */
static int __init cn_proc_init(void)
{
	int err = cn_add_callback(&cn_proc_event_id,
				  "cn_proc",
				  &cn_proc_mcast_ctl);
	if (err) {
		pr_warn("cn_proc failed to register\n");
		return err;
	}
	return 0;
}
device_initcall(cn_proc_init);
