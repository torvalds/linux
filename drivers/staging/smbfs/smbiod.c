/*
 *  smbiod.c
 *
 *  Copyright (C) 2000, Charles Loep / Corel Corp.
 *  Copyright (C) 2001, Urban Widmark
 */


#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/dcache.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/kthread.h>
#include <net/ip.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include "smb_fs.h"
#include "smbno.h"
#include "smb_mount.h"
#include "smb_debug.h"
#include "request.h"
#include "proto.h"

enum smbiod_state {
	SMBIOD_DEAD,
	SMBIOD_STARTING,
	SMBIOD_RUNNING,
};

static enum smbiod_state smbiod_state = SMBIOD_DEAD;
static struct task_struct *smbiod_thread;
static DECLARE_WAIT_QUEUE_HEAD(smbiod_wait);
static LIST_HEAD(smb_servers);
static DEFINE_SPINLOCK(servers_lock);

#define SMBIOD_DATA_READY	(1<<0)
static unsigned long smbiod_flags;

static int smbiod(void *);
static int smbiod_start(void);

/*
 * called when there's work for us to do
 */
void smbiod_wake_up(void)
{
	if (smbiod_state == SMBIOD_DEAD)
		return;
	set_bit(SMBIOD_DATA_READY, &smbiod_flags);
	wake_up_interruptible(&smbiod_wait);
}

/*
 * start smbiod if none is running
 */
static int smbiod_start(void)
{
	struct task_struct *tsk;
	int err = 0;

	if (smbiod_state != SMBIOD_DEAD)
		return 0;
	smbiod_state = SMBIOD_STARTING;
	__module_get(THIS_MODULE);
	spin_unlock(&servers_lock);
	tsk = kthread_run(smbiod, NULL, "smbiod");
	if (IS_ERR(tsk)) {
		err = PTR_ERR(tsk);
		module_put(THIS_MODULE);
	}

	spin_lock(&servers_lock);
	if (err < 0) {
		smbiod_state = SMBIOD_DEAD;
		smbiod_thread = NULL;
	} else {
		smbiod_state = SMBIOD_RUNNING;
		smbiod_thread = tsk;
	}
	return err;
}

/*
 * register a server & start smbiod if necessary
 */
int smbiod_register_server(struct smb_sb_info *server)
{
	int ret;
	spin_lock(&servers_lock);
	list_add(&server->entry, &smb_servers);
	VERBOSE("%p\n", server);
	ret = smbiod_start();
	spin_unlock(&servers_lock);
	return ret;
}

/*
 * Unregister a server
 * Must be called with the server lock held.
 */
void smbiod_unregister_server(struct smb_sb_info *server)
{
	spin_lock(&servers_lock);
	list_del_init(&server->entry);
	VERBOSE("%p\n", server);
	spin_unlock(&servers_lock);

	smbiod_wake_up();
	smbiod_flush(server);
}

void smbiod_flush(struct smb_sb_info *server)
{
	struct list_head *tmp, *n;
	struct smb_request *req;

	list_for_each_safe(tmp, n, &server->xmitq) {
		req = list_entry(tmp, struct smb_request, rq_queue);
		req->rq_errno = -EIO;
		list_del_init(&req->rq_queue);
		smb_rput(req);
		wake_up_interruptible(&req->rq_wait);
	}
	list_for_each_safe(tmp, n, &server->recvq) {
		req = list_entry(tmp, struct smb_request, rq_queue);
		req->rq_errno = -EIO;
		list_del_init(&req->rq_queue);
		smb_rput(req);
		wake_up_interruptible(&req->rq_wait);
	}
}

/*
 * Wake up smbmount and make it reconnect to the server.
 * This must be called with the server locked.
 *
 * FIXME: add smbconnect version to this
 */
int smbiod_retry(struct smb_sb_info *server)
{
	struct list_head *head;
	struct smb_request *req;
	struct pid *pid = get_pid(server->conn_pid);
	int result = 0;

	VERBOSE("state: %d\n", server->state);
	if (server->state == CONN_VALID || server->state == CONN_RETRYING)
		goto out;

	smb_invalidate_inodes(server);

	/*
	 * Some requests are meaningless after a retry, so we abort them.
	 * One example are all requests using 'fileid' since the files are
	 * closed on retry.
	 */
	head = server->xmitq.next;
	while (head != &server->xmitq) {
		req = list_entry(head, struct smb_request, rq_queue);
		head = head->next;

		req->rq_bytes_sent = 0;
		if (req->rq_flags & SMB_REQ_NORETRY) {
			VERBOSE("aborting request %p on xmitq\n", req);
			req->rq_errno = -EIO;
			list_del_init(&req->rq_queue);
			smb_rput(req);
			wake_up_interruptible(&req->rq_wait);
		}
	}

	/*
	 * FIXME: test the code for retrying request we already sent
	 */
	head = server->recvq.next;
	while (head != &server->recvq) {
		req = list_entry(head, struct smb_request, rq_queue);
		head = head->next;
#if 0
		if (req->rq_flags & SMB_REQ_RETRY) {
			/* must move the request to the xmitq */
			VERBOSE("retrying request %p on recvq\n", req);
			list_move(&req->rq_queue, &server->xmitq);
			continue;
		}
#endif

		VERBOSE("aborting request %p on recvq\n", req);
		/* req->rq_rcls = ???; */ /* FIXME: set smb error code too? */
		req->rq_errno = -EIO;
		list_del_init(&req->rq_queue);
		smb_rput(req);
		wake_up_interruptible(&req->rq_wait);
	}

	smb_close_socket(server);

	if (!pid) {
		/* FIXME: this is fatal, umount? */
		printk(KERN_ERR "smb_retry: no connection process\n");
		server->state = CONN_RETRIED;
		goto out;
	}

	/*
	 * Change state so that only one retry per server will be started.
	 */
	server->state = CONN_RETRYING;

	/*
	 * Note: use the "priv" flag, as a user process may need to reconnect.
	 */
	result = kill_pid(pid, SIGUSR1, 1);
	if (result) {
		/* FIXME: this is most likely fatal, umount? */
		printk(KERN_ERR "smb_retry: signal failed [%d]\n", result);
		goto out;
	}
	VERBOSE("signalled pid %d\n", pid_nr(pid));

	/* FIXME: The retried requests should perhaps get a "time boost". */

out:
	put_pid(pid);
	return result;
}

/*
 * Currently handles lockingX packets.
 */
static void smbiod_handle_request(struct smb_sb_info *server)
{
	PARANOIA("smbiod got a request ... and we don't implement oplocks!\n");
	server->rstate = SMB_RECV_DROP;
}

/*
 * Do some IO for one server.
 */
static void smbiod_doio(struct smb_sb_info *server)
{
	int result;
	int maxwork = 7;

	if (server->state != CONN_VALID)
		goto out;

	do {
		result = smb_request_recv(server);
		if (result < 0) {
			server->state = CONN_INVALID;
			smbiod_retry(server);
			goto out;	/* reconnecting is slow */
		} else if (server->rstate == SMB_RECV_REQUEST)
			smbiod_handle_request(server);
	} while (result > 0 && maxwork-- > 0);

	/*
	 * If there is more to read then we want to be sure to wake up again.
	 */
	if (server->state != CONN_VALID)
		goto out;
	if (smb_recv_available(server) > 0)
		set_bit(SMBIOD_DATA_READY, &smbiod_flags);

	do {
		result = smb_request_send_server(server);
		if (result < 0) {
			server->state = CONN_INVALID;
			smbiod_retry(server);
			goto out;	/* reconnecting is slow */
		}
	} while (result > 0);

	/*
	 * If the last request was not sent out we want to wake up again.
	 */
	if (!list_empty(&server->xmitq))
		set_bit(SMBIOD_DATA_READY, &smbiod_flags);

out:
	return;
}

/*
 * smbiod kernel thread
 */
static int smbiod(void *unused)
{
	VERBOSE("SMB Kernel thread starting (%d) ...\n", current->pid);

	for (;;) {
		struct smb_sb_info *server;
		struct list_head *pos, *n;

		/* FIXME: Use poll? */
		wait_event_interruptible(smbiod_wait,
			 test_bit(SMBIOD_DATA_READY, &smbiod_flags));
		if (signal_pending(current)) {
			spin_lock(&servers_lock);
			smbiod_state = SMBIOD_DEAD;
			spin_unlock(&servers_lock);
			break;
		}

		clear_bit(SMBIOD_DATA_READY, &smbiod_flags);

		spin_lock(&servers_lock);
		if (list_empty(&smb_servers)) {
			smbiod_state = SMBIOD_DEAD;
			spin_unlock(&servers_lock);
			break;
		}

		list_for_each_safe(pos, n, &smb_servers) {
			server = list_entry(pos, struct smb_sb_info, entry);
			VERBOSE("checking server %p\n", server);

			if (server->state == CONN_VALID) {
				spin_unlock(&servers_lock);

				smb_lock_server(server);
				smbiod_doio(server);
				smb_unlock_server(server);

				spin_lock(&servers_lock);
			}
		}
		spin_unlock(&servers_lock);
	}

	VERBOSE("SMB Kernel thread exiting (%d) ...\n", current->pid);
	module_put_and_exit(0);
}
