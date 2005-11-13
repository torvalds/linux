/*
 * ipmi_msghandler.c
 *
 * Incoming and outgoing message routing for an IPMI interface.
 *
 * Author: MontaVista Software, Inc.
 *         Corey Minyard <minyard@mvista.com>
 *         source@mvista.com
 *
 * Copyright 2002 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <asm/system.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/ipmi.h>
#include <linux/ipmi_smi.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/rcupdate.h>

#define PFX "IPMI message handler: "

#define IPMI_DRIVER_VERSION "36.0"

static struct ipmi_recv_msg *ipmi_alloc_recv_msg(void);
static int ipmi_init_msghandler(void);

static int initialized = 0;

#ifdef CONFIG_PROC_FS
struct proc_dir_entry *proc_ipmi_root = NULL;
#endif /* CONFIG_PROC_FS */

#define MAX_EVENTS_IN_QUEUE	25

/* Don't let a message sit in a queue forever, always time it with at lest
   the max message timer.  This is in milliseconds. */
#define MAX_MSG_TIMEOUT		60000


/*
 * The main "user" data structure.
 */
struct ipmi_user
{
	struct list_head link;

	/* Set to "0" when the user is destroyed. */
	int valid;

	struct kref refcount;

	/* The upper layer that handles receive messages. */
	struct ipmi_user_hndl *handler;
	void             *handler_data;

	/* The interface this user is bound to. */
	ipmi_smi_t intf;

	/* Does this interface receive IPMI events? */
	int gets_events;
};

struct cmd_rcvr
{
	struct list_head link;

	ipmi_user_t   user;
	unsigned char netfn;
	unsigned char cmd;

	/*
	 * This is used to form a linked lised during mass deletion.
	 * Since this is in an RCU list, we cannot use the link above
	 * or change any data until the RCU period completes.  So we
	 * use this next variable during mass deletion so we can have
	 * a list and don't have to wait and restart the search on
	 * every individual deletion of a command. */
	struct cmd_rcvr *next;
};

struct seq_table
{
	unsigned int         inuse : 1;
	unsigned int         broadcast : 1;

	unsigned long        timeout;
	unsigned long        orig_timeout;
	unsigned int         retries_left;

	/* To verify on an incoming send message response that this is
           the message that the response is for, we keep a sequence id
           and increment it every time we send a message. */
	long                 seqid;

	/* This is held so we can properly respond to the message on a
           timeout, and it is used to hold the temporary data for
           retransmission, too. */
	struct ipmi_recv_msg *recv_msg;
};

/* Store the information in a msgid (long) to allow us to find a
   sequence table entry from the msgid. */
#define STORE_SEQ_IN_MSGID(seq, seqid) (((seq&0xff)<<26) | (seqid&0x3ffffff))

#define GET_SEQ_FROM_MSGID(msgid, seq, seqid) \
	do {								\
		seq = ((msgid >> 26) & 0x3f);				\
		seqid = (msgid & 0x3fffff);				\
        } while (0)

#define NEXT_SEQID(seqid) (((seqid) + 1) & 0x3fffff)

struct ipmi_channel
{
	unsigned char medium;
	unsigned char protocol;

	/* My slave address.  This is initialized to IPMI_BMC_SLAVE_ADDR,
	   but may be changed by the user. */
	unsigned char address;

	/* My LUN.  This should generally stay the SMS LUN, but just in
	   case... */
	unsigned char lun;
};

#ifdef CONFIG_PROC_FS
struct ipmi_proc_entry
{
	char                   *name;
	struct ipmi_proc_entry *next;
};
#endif

#define IPMI_IPMB_NUM_SEQ	64
#define IPMI_MAX_CHANNELS       16
struct ipmi_smi
{
	/* What interface number are we? */
	int intf_num;

	struct kref refcount;

	/* The list of upper layers that are using me.  seq_lock
	 * protects this. */
	struct list_head users;

	/* Used for wake ups at startup. */
	wait_queue_head_t waitq;

	/* The IPMI version of the BMC on the other end. */
	unsigned char       version_major;
	unsigned char       version_minor;

	/* This is the lower-layer's sender routine. */
	struct ipmi_smi_handlers *handlers;
	void                     *send_info;

#ifdef CONFIG_PROC_FS
	/* A list of proc entries for this interface.  This does not
	   need a lock, only one thread creates it and only one thread
	   destroys it. */
	spinlock_t             proc_entry_lock;
	struct ipmi_proc_entry *proc_entries;
#endif

	/* A table of sequence numbers for this interface.  We use the
           sequence numbers for IPMB messages that go out of the
           interface to match them up with their responses.  A routine
           is called periodically to time the items in this list. */
	spinlock_t       seq_lock;
	struct seq_table seq_table[IPMI_IPMB_NUM_SEQ];
	int curr_seq;

	/* Messages that were delayed for some reason (out of memory,
           for instance), will go in here to be processed later in a
           periodic timer interrupt. */
	spinlock_t       waiting_msgs_lock;
	struct list_head waiting_msgs;

	/* The list of command receivers that are registered for commands
	   on this interface. */
	struct semaphore cmd_rcvrs_lock;
	struct list_head cmd_rcvrs;

	/* Events that were queues because no one was there to receive
           them. */
	spinlock_t       events_lock; /* For dealing with event stuff. */
	struct list_head waiting_events;
	unsigned int     waiting_events_count; /* How many events in queue? */

	/* The event receiver for my BMC, only really used at panic
	   shutdown as a place to store this. */
	unsigned char event_receiver;
	unsigned char event_receiver_lun;
	unsigned char local_sel_device;
	unsigned char local_event_generator;

	/* A cheap hack, if this is non-null and a message to an
	   interface comes in with a NULL user, call this routine with
	   it.  Note that the message will still be freed by the
	   caller.  This only works on the system interface. */
	void (*null_user_handler)(ipmi_smi_t intf, struct ipmi_recv_msg *msg);

	/* When we are scanning the channels for an SMI, this will
	   tell which channel we are scanning. */
	int curr_channel;

	/* Channel information */
	struct ipmi_channel channels[IPMI_MAX_CHANNELS];

	/* Proc FS stuff. */
	struct proc_dir_entry *proc_dir;
	char                  proc_dir_name[10];

	spinlock_t   counter_lock; /* For making counters atomic. */

	/* Commands we got that were invalid. */
	unsigned int sent_invalid_commands;

	/* Commands we sent to the MC. */
	unsigned int sent_local_commands;
	/* Responses from the MC that were delivered to a user. */
	unsigned int handled_local_responses;
	/* Responses from the MC that were not delivered to a user. */
	unsigned int unhandled_local_responses;

	/* Commands we sent out to the IPMB bus. */
	unsigned int sent_ipmb_commands;
	/* Commands sent on the IPMB that had errors on the SEND CMD */
	unsigned int sent_ipmb_command_errs;
	/* Each retransmit increments this count. */
	unsigned int retransmitted_ipmb_commands;
	/* When a message times out (runs out of retransmits) this is
           incremented. */
	unsigned int timed_out_ipmb_commands;

	/* This is like above, but for broadcasts.  Broadcasts are
           *not* included in the above count (they are expected to
           time out). */
	unsigned int timed_out_ipmb_broadcasts;

	/* Responses I have sent to the IPMB bus. */
	unsigned int sent_ipmb_responses;

	/* The response was delivered to the user. */
	unsigned int handled_ipmb_responses;
	/* The response had invalid data in it. */
	unsigned int invalid_ipmb_responses;
	/* The response didn't have anyone waiting for it. */
	unsigned int unhandled_ipmb_responses;

	/* Commands we sent out to the IPMB bus. */
	unsigned int sent_lan_commands;
	/* Commands sent on the IPMB that had errors on the SEND CMD */
	unsigned int sent_lan_command_errs;
	/* Each retransmit increments this count. */
	unsigned int retransmitted_lan_commands;
	/* When a message times out (runs out of retransmits) this is
           incremented. */
	unsigned int timed_out_lan_commands;

	/* Responses I have sent to the IPMB bus. */
	unsigned int sent_lan_responses;

	/* The response was delivered to the user. */
	unsigned int handled_lan_responses;
	/* The response had invalid data in it. */
	unsigned int invalid_lan_responses;
	/* The response didn't have anyone waiting for it. */
	unsigned int unhandled_lan_responses;

	/* The command was delivered to the user. */
	unsigned int handled_commands;
	/* The command had invalid data in it. */
	unsigned int invalid_commands;
	/* The command didn't have anyone waiting for it. */
	unsigned int unhandled_commands;

	/* Invalid data in an event. */
	unsigned int invalid_events;
	/* Events that were received with the proper format. */
	unsigned int events;
};

/* Used to mark an interface entry that cannot be used but is not a
 * free entry, either, primarily used at creation and deletion time so
 * a slot doesn't get reused too quickly. */
#define IPMI_INVALID_INTERFACE_ENTRY ((ipmi_smi_t) ((long) 1))
#define IPMI_INVALID_INTERFACE(i) (((i) == NULL) \
				   || (i == IPMI_INVALID_INTERFACE_ENTRY))

#define MAX_IPMI_INTERFACES 4
static ipmi_smi_t ipmi_interfaces[MAX_IPMI_INTERFACES];

/* Directly protects the ipmi_interfaces data structure. */
static DEFINE_SPINLOCK(interfaces_lock);

/* List of watchers that want to know when smi's are added and
   deleted. */
static struct list_head smi_watchers = LIST_HEAD_INIT(smi_watchers);
static DECLARE_RWSEM(smi_watchers_sem);


static void free_recv_msg_list(struct list_head *q)
{
	struct ipmi_recv_msg *msg, *msg2;

	list_for_each_entry_safe(msg, msg2, q, link) {
		list_del(&msg->link);
		ipmi_free_recv_msg(msg);
	}
}

static void clean_up_interface_data(ipmi_smi_t intf)
{
	int              i;
	struct cmd_rcvr  *rcvr, *rcvr2;
	struct list_head list;

	free_recv_msg_list(&intf->waiting_msgs);
	free_recv_msg_list(&intf->waiting_events);

	/* Wholesale remove all the entries from the list in the
	 * interface and wait for RCU to know that none are in use. */
	down(&intf->cmd_rcvrs_lock);
	list_add_rcu(&list, &intf->cmd_rcvrs);
	list_del_rcu(&intf->cmd_rcvrs);
	up(&intf->cmd_rcvrs_lock);
	synchronize_rcu();

	list_for_each_entry_safe(rcvr, rcvr2, &list, link)
		kfree(rcvr);

	for (i = 0; i < IPMI_IPMB_NUM_SEQ; i++) {
		if ((intf->seq_table[i].inuse)
		    && (intf->seq_table[i].recv_msg))
		{
			ipmi_free_recv_msg(intf->seq_table[i].recv_msg);
		}
	}
}

static void intf_free(struct kref *ref)
{
	ipmi_smi_t intf = container_of(ref, struct ipmi_smi, refcount);

	clean_up_interface_data(intf);
	kfree(intf);
}

int ipmi_smi_watcher_register(struct ipmi_smi_watcher *watcher)
{
	int           i;
	unsigned long flags;

	down_write(&smi_watchers_sem);
	list_add(&(watcher->link), &smi_watchers);
	up_write(&smi_watchers_sem);
	spin_lock_irqsave(&interfaces_lock, flags);
	for (i = 0; i < MAX_IPMI_INTERFACES; i++) {
		ipmi_smi_t intf = ipmi_interfaces[i];
		if (IPMI_INVALID_INTERFACE(intf))
			continue;
		spin_unlock_irqrestore(&interfaces_lock, flags);
		watcher->new_smi(i);
		spin_lock_irqsave(&interfaces_lock, flags);
	}
	spin_unlock_irqrestore(&interfaces_lock, flags);
	return 0;
}

int ipmi_smi_watcher_unregister(struct ipmi_smi_watcher *watcher)
{
	down_write(&smi_watchers_sem);
	list_del(&(watcher->link));
	up_write(&smi_watchers_sem);
	return 0;
}

static void
call_smi_watchers(int i)
{
	struct ipmi_smi_watcher *w;

	down_read(&smi_watchers_sem);
	list_for_each_entry(w, &smi_watchers, link) {
		if (try_module_get(w->owner)) {
			w->new_smi(i);
			module_put(w->owner);
		}
	}
	up_read(&smi_watchers_sem);
}

static int
ipmi_addr_equal(struct ipmi_addr *addr1, struct ipmi_addr *addr2)
{
	if (addr1->addr_type != addr2->addr_type)
		return 0;

	if (addr1->channel != addr2->channel)
		return 0;

	if (addr1->addr_type == IPMI_SYSTEM_INTERFACE_ADDR_TYPE) {
		struct ipmi_system_interface_addr *smi_addr1
		    = (struct ipmi_system_interface_addr *) addr1;
		struct ipmi_system_interface_addr *smi_addr2
		    = (struct ipmi_system_interface_addr *) addr2;
		return (smi_addr1->lun == smi_addr2->lun);
	}

	if ((addr1->addr_type == IPMI_IPMB_ADDR_TYPE)
	    || (addr1->addr_type == IPMI_IPMB_BROADCAST_ADDR_TYPE))
	{
		struct ipmi_ipmb_addr *ipmb_addr1
		    = (struct ipmi_ipmb_addr *) addr1;
		struct ipmi_ipmb_addr *ipmb_addr2
		    = (struct ipmi_ipmb_addr *) addr2;

		return ((ipmb_addr1->slave_addr == ipmb_addr2->slave_addr)
			&& (ipmb_addr1->lun == ipmb_addr2->lun));
	}

	if (addr1->addr_type == IPMI_LAN_ADDR_TYPE) {
		struct ipmi_lan_addr *lan_addr1
			= (struct ipmi_lan_addr *) addr1;
		struct ipmi_lan_addr *lan_addr2
		    = (struct ipmi_lan_addr *) addr2;

		return ((lan_addr1->remote_SWID == lan_addr2->remote_SWID)
			&& (lan_addr1->local_SWID == lan_addr2->local_SWID)
			&& (lan_addr1->session_handle
			    == lan_addr2->session_handle)
			&& (lan_addr1->lun == lan_addr2->lun));
	}

	return 1;
}

int ipmi_validate_addr(struct ipmi_addr *addr, int len)
{
	if (len < sizeof(struct ipmi_system_interface_addr)) {
		return -EINVAL;
	}

	if (addr->addr_type == IPMI_SYSTEM_INTERFACE_ADDR_TYPE) {
		if (addr->channel != IPMI_BMC_CHANNEL)
			return -EINVAL;
		return 0;
	}

	if ((addr->channel == IPMI_BMC_CHANNEL)
	    || (addr->channel >= IPMI_NUM_CHANNELS)
	    || (addr->channel < 0))
		return -EINVAL;

	if ((addr->addr_type == IPMI_IPMB_ADDR_TYPE)
	    || (addr->addr_type == IPMI_IPMB_BROADCAST_ADDR_TYPE))
	{
		if (len < sizeof(struct ipmi_ipmb_addr)) {
			return -EINVAL;
		}
		return 0;
	}

	if (addr->addr_type == IPMI_LAN_ADDR_TYPE) {
		if (len < sizeof(struct ipmi_lan_addr)) {
			return -EINVAL;
		}
		return 0;
	}

	return -EINVAL;
}

unsigned int ipmi_addr_length(int addr_type)
{
	if (addr_type == IPMI_SYSTEM_INTERFACE_ADDR_TYPE)
		return sizeof(struct ipmi_system_interface_addr);

	if ((addr_type == IPMI_IPMB_ADDR_TYPE)
	    || (addr_type == IPMI_IPMB_BROADCAST_ADDR_TYPE))
	{
		return sizeof(struct ipmi_ipmb_addr);
	}

	if (addr_type == IPMI_LAN_ADDR_TYPE)
		return sizeof(struct ipmi_lan_addr);

	return 0;
}

static void deliver_response(struct ipmi_recv_msg *msg)
{
	if (! msg->user) {
		ipmi_smi_t    intf = msg->user_msg_data;
		unsigned long flags;

		/* Special handling for NULL users. */
		if (intf->null_user_handler) {
			intf->null_user_handler(intf, msg);
			spin_lock_irqsave(&intf->counter_lock, flags);
			intf->handled_local_responses++;
			spin_unlock_irqrestore(&intf->counter_lock, flags);
		} else {
			/* No handler, so give up. */
			spin_lock_irqsave(&intf->counter_lock, flags);
			intf->unhandled_local_responses++;
			spin_unlock_irqrestore(&intf->counter_lock, flags);
		}
		ipmi_free_recv_msg(msg);
	} else {
		ipmi_user_t user = msg->user;
		user->handler->ipmi_recv_hndl(msg, user->handler_data);
	}
}

/* Find the next sequence number not being used and add the given
   message with the given timeout to the sequence table.  This must be
   called with the interface's seq_lock held. */
static int intf_next_seq(ipmi_smi_t           intf,
			 struct ipmi_recv_msg *recv_msg,
			 unsigned long        timeout,
			 int                  retries,
			 int                  broadcast,
			 unsigned char        *seq,
			 long                 *seqid)
{
	int          rv = 0;
	unsigned int i;

	for (i = intf->curr_seq;
	     (i+1)%IPMI_IPMB_NUM_SEQ != intf->curr_seq;
	     i = (i+1)%IPMI_IPMB_NUM_SEQ)
	{
		if (! intf->seq_table[i].inuse)
			break;
	}

	if (! intf->seq_table[i].inuse) {
		intf->seq_table[i].recv_msg = recv_msg;

		/* Start with the maximum timeout, when the send response
		   comes in we will start the real timer. */
		intf->seq_table[i].timeout = MAX_MSG_TIMEOUT;
		intf->seq_table[i].orig_timeout = timeout;
		intf->seq_table[i].retries_left = retries;
		intf->seq_table[i].broadcast = broadcast;
		intf->seq_table[i].inuse = 1;
		intf->seq_table[i].seqid = NEXT_SEQID(intf->seq_table[i].seqid);
		*seq = i;
		*seqid = intf->seq_table[i].seqid;
		intf->curr_seq = (i+1)%IPMI_IPMB_NUM_SEQ;
	} else {
		rv = -EAGAIN;
	}
	
	return rv;
}

/* Return the receive message for the given sequence number and
   release the sequence number so it can be reused.  Some other data
   is passed in to be sure the message matches up correctly (to help
   guard against message coming in after their timeout and the
   sequence number being reused). */
static int intf_find_seq(ipmi_smi_t           intf,
			 unsigned char        seq,
			 short                channel,
			 unsigned char        cmd,
			 unsigned char        netfn,
			 struct ipmi_addr     *addr,
			 struct ipmi_recv_msg **recv_msg)
{
	int           rv = -ENODEV;
	unsigned long flags;

	if (seq >= IPMI_IPMB_NUM_SEQ)
		return -EINVAL;

	spin_lock_irqsave(&(intf->seq_lock), flags);
	if (intf->seq_table[seq].inuse) {
		struct ipmi_recv_msg *msg = intf->seq_table[seq].recv_msg;

		if ((msg->addr.channel == channel)
		    && (msg->msg.cmd == cmd)
		    && (msg->msg.netfn == netfn)
		    && (ipmi_addr_equal(addr, &(msg->addr))))
		{
			*recv_msg = msg;
			intf->seq_table[seq].inuse = 0;
			rv = 0;
		}
	}
	spin_unlock_irqrestore(&(intf->seq_lock), flags);

	return rv;
}


/* Start the timer for a specific sequence table entry. */
static int intf_start_seq_timer(ipmi_smi_t intf,
				long       msgid)
{
	int           rv = -ENODEV;
	unsigned long flags;
	unsigned char seq;
	unsigned long seqid;


	GET_SEQ_FROM_MSGID(msgid, seq, seqid);

	spin_lock_irqsave(&(intf->seq_lock), flags);
	/* We do this verification because the user can be deleted
           while a message is outstanding. */
	if ((intf->seq_table[seq].inuse)
	    && (intf->seq_table[seq].seqid == seqid))
	{
		struct seq_table *ent = &(intf->seq_table[seq]);
		ent->timeout = ent->orig_timeout;
		rv = 0;
	}
	spin_unlock_irqrestore(&(intf->seq_lock), flags);

	return rv;
}

/* Got an error for the send message for a specific sequence number. */
static int intf_err_seq(ipmi_smi_t   intf,
			long         msgid,
			unsigned int err)
{
	int                  rv = -ENODEV;
	unsigned long        flags;
	unsigned char        seq;
	unsigned long        seqid;
	struct ipmi_recv_msg *msg = NULL;


	GET_SEQ_FROM_MSGID(msgid, seq, seqid);

	spin_lock_irqsave(&(intf->seq_lock), flags);
	/* We do this verification because the user can be deleted
           while a message is outstanding. */
	if ((intf->seq_table[seq].inuse)
	    && (intf->seq_table[seq].seqid == seqid))
	{
		struct seq_table *ent = &(intf->seq_table[seq]);

		ent->inuse = 0;
		msg = ent->recv_msg;
		rv = 0;
	}
	spin_unlock_irqrestore(&(intf->seq_lock), flags);

	if (msg) {
		msg->recv_type = IPMI_RESPONSE_RECV_TYPE;
		msg->msg_data[0] = err;
		msg->msg.netfn |= 1; /* Convert to a response. */
		msg->msg.data_len = 1;
		msg->msg.data = msg->msg_data;
		deliver_response(msg);
	}

	return rv;
}


int ipmi_create_user(unsigned int          if_num,
		     struct ipmi_user_hndl *handler,
		     void                  *handler_data,
		     ipmi_user_t           *user)
{
	unsigned long flags;
	ipmi_user_t   new_user;
	int           rv = 0;
	ipmi_smi_t    intf;

	/* There is no module usecount here, because it's not
           required.  Since this can only be used by and called from
           other modules, they will implicitly use this module, and
           thus this can't be removed unless the other modules are
           removed. */

	if (handler == NULL)
		return -EINVAL;

	/* Make sure the driver is actually initialized, this handles
	   problems with initialization order. */
	if (!initialized) {
		rv = ipmi_init_msghandler();
		if (rv)
			return rv;

		/* The init code doesn't return an error if it was turned
		   off, but it won't initialize.  Check that. */
		if (!initialized)
			return -ENODEV;
	}

	new_user = kmalloc(sizeof(*new_user), GFP_KERNEL);
	if (! new_user)
		return -ENOMEM;

	spin_lock_irqsave(&interfaces_lock, flags);
	intf = ipmi_interfaces[if_num];
	if ((if_num >= MAX_IPMI_INTERFACES) || IPMI_INVALID_INTERFACE(intf)) {
		spin_unlock_irqrestore(&interfaces_lock, flags);
		return -EINVAL;
	}

	/* Note that each existing user holds a refcount to the interface. */
	kref_get(&intf->refcount);
	spin_unlock_irqrestore(&interfaces_lock, flags);

	kref_init(&new_user->refcount);
	new_user->handler = handler;
	new_user->handler_data = handler_data;
	new_user->intf = intf;
	new_user->gets_events = 0;

	if (!try_module_get(intf->handlers->owner)) {
		rv = -ENODEV;
		goto out_err;
	}

	if (intf->handlers->inc_usecount) {
		rv = intf->handlers->inc_usecount(intf->send_info);
		if (rv) {
			module_put(intf->handlers->owner);
			goto out_err;
		}
	}

	new_user->valid = 1;
	spin_lock_irqsave(&intf->seq_lock, flags);
	list_add_rcu(&new_user->link, &intf->users);
	spin_unlock_irqrestore(&intf->seq_lock, flags);
	*user = new_user;
	return 0;

 out_err:
	kfree(new_user);
	kref_put(&intf->refcount, intf_free);
	return rv;
}

static void free_user(struct kref *ref)
{
	ipmi_user_t user = container_of(ref, struct ipmi_user, refcount);
	kfree(user);
}

int ipmi_destroy_user(ipmi_user_t user)
{
	int              rv = -ENODEV;
	ipmi_smi_t       intf = user->intf;
	int              i;
	unsigned long    flags;
	struct cmd_rcvr  *rcvr;
	struct list_head *entry1, *entry2;
	struct cmd_rcvr  *rcvrs = NULL;

	user->valid = 1;

	/* Remove the user from the interface's sequence table. */
	spin_lock_irqsave(&intf->seq_lock, flags);
	list_del_rcu(&user->link);

	for (i = 0; i < IPMI_IPMB_NUM_SEQ; i++) {
		if (intf->seq_table[i].inuse
		    && (intf->seq_table[i].recv_msg->user == user))
		{
			intf->seq_table[i].inuse = 0;
		}
	}
	spin_unlock_irqrestore(&intf->seq_lock, flags);

	/*
	 * Remove the user from the command receiver's table.  First
	 * we build a list of everything (not using the standard link,
	 * since other things may be using it till we do
	 * synchronize_rcu()) then free everything in that list.
	 */
	down(&intf->cmd_rcvrs_lock);
	list_for_each_safe_rcu(entry1, entry2, &intf->cmd_rcvrs) {
		rcvr = list_entry(entry1, struct cmd_rcvr, link);
		if (rcvr->user == user) {
			list_del_rcu(&rcvr->link);
			rcvr->next = rcvrs;
			rcvrs = rcvr;
		}
	}
	up(&intf->cmd_rcvrs_lock);
	synchronize_rcu();
	while (rcvrs) {
		rcvr = rcvrs;
		rcvrs = rcvr->next;
		kfree(rcvr);
	}

	module_put(intf->handlers->owner);
	if (intf->handlers->dec_usecount)
		intf->handlers->dec_usecount(intf->send_info);

	kref_put(&intf->refcount, intf_free);

	kref_put(&user->refcount, free_user);

	return rv;
}

void ipmi_get_version(ipmi_user_t   user,
		      unsigned char *major,
		      unsigned char *minor)
{
	*major = user->intf->version_major;
	*minor = user->intf->version_minor;
}

int ipmi_set_my_address(ipmi_user_t   user,
			unsigned int  channel,
			unsigned char address)
{
	if (channel >= IPMI_MAX_CHANNELS)
		return -EINVAL;
	user->intf->channels[channel].address = address;
	return 0;
}

int ipmi_get_my_address(ipmi_user_t   user,
			unsigned int  channel,
			unsigned char *address)
{
	if (channel >= IPMI_MAX_CHANNELS)
		return -EINVAL;
	*address = user->intf->channels[channel].address;
	return 0;
}

int ipmi_set_my_LUN(ipmi_user_t   user,
		    unsigned int  channel,
		    unsigned char LUN)
{
	if (channel >= IPMI_MAX_CHANNELS)
		return -EINVAL;
	user->intf->channels[channel].lun = LUN & 0x3;
	return 0;
}

int ipmi_get_my_LUN(ipmi_user_t   user,
		    unsigned int  channel,
		    unsigned char *address)
{
	if (channel >= IPMI_MAX_CHANNELS)
		return -EINVAL;
	*address = user->intf->channels[channel].lun;
	return 0;
}

int ipmi_set_gets_events(ipmi_user_t user, int val)
{
	unsigned long        flags;
	ipmi_smi_t           intf = user->intf;
	struct ipmi_recv_msg *msg, *msg2;
	struct list_head     msgs;

	INIT_LIST_HEAD(&msgs);

	spin_lock_irqsave(&intf->events_lock, flags);
	user->gets_events = val;

	if (val) {
		/* Deliver any queued events. */
		list_for_each_entry_safe(msg, msg2, &intf->waiting_events, link) {
			list_del(&msg->link);
			list_add_tail(&msg->link, &msgs);
		}
	}

	/* Hold the events lock while doing this to preserve order. */
	list_for_each_entry_safe(msg, msg2, &msgs, link) {
		msg->user = user;
		kref_get(&user->refcount);
		deliver_response(msg);
	}

	spin_unlock_irqrestore(&intf->events_lock, flags);

	return 0;
}

static struct cmd_rcvr *find_cmd_rcvr(ipmi_smi_t    intf,
				      unsigned char netfn,
				      unsigned char cmd)
{
	struct cmd_rcvr *rcvr;

	list_for_each_entry_rcu(rcvr, &intf->cmd_rcvrs, link) {
		if ((rcvr->netfn == netfn) && (rcvr->cmd == cmd))
			return rcvr;
	}
	return NULL;
}

int ipmi_register_for_cmd(ipmi_user_t   user,
			  unsigned char netfn,
			  unsigned char cmd)
{
	ipmi_smi_t      intf = user->intf;
	struct cmd_rcvr *rcvr;
	struct cmd_rcvr *entry;
	int             rv = 0;


	rcvr = kmalloc(sizeof(*rcvr), GFP_KERNEL);
	if (! rcvr)
		return -ENOMEM;
	rcvr->cmd = cmd;
	rcvr->netfn = netfn;
	rcvr->user = user;

	down(&intf->cmd_rcvrs_lock);
	/* Make sure the command/netfn is not already registered. */
	entry = find_cmd_rcvr(intf, netfn, cmd);
	if (entry) {
		rv = -EBUSY;
		goto out_unlock;
	}

	list_add_rcu(&rcvr->link, &intf->cmd_rcvrs);

 out_unlock:
	up(&intf->cmd_rcvrs_lock);
	if (rv)
		kfree(rcvr);

	return rv;
}

int ipmi_unregister_for_cmd(ipmi_user_t   user,
			    unsigned char netfn,
			    unsigned char cmd)
{
	ipmi_smi_t      intf = user->intf;
	struct cmd_rcvr *rcvr;

	down(&intf->cmd_rcvrs_lock);
	/* Make sure the command/netfn is not already registered. */
	rcvr = find_cmd_rcvr(intf, netfn, cmd);
	if ((rcvr) && (rcvr->user == user)) {
		list_del_rcu(&rcvr->link);
		up(&intf->cmd_rcvrs_lock);
		synchronize_rcu();
		kfree(rcvr);
		return 0;
	} else {
		up(&intf->cmd_rcvrs_lock);
		return -ENOENT;
	}
}

void ipmi_user_set_run_to_completion(ipmi_user_t user, int val)
{
	ipmi_smi_t intf = user->intf;
	intf->handlers->set_run_to_completion(intf->send_info, val);
}

static unsigned char
ipmb_checksum(unsigned char *data, int size)
{
	unsigned char csum = 0;
	
	for (; size > 0; size--, data++)
		csum += *data;

	return -csum;
}

static inline void format_ipmb_msg(struct ipmi_smi_msg   *smi_msg,
				   struct kernel_ipmi_msg *msg,
				   struct ipmi_ipmb_addr *ipmb_addr,
				   long                  msgid,
				   unsigned char         ipmb_seq,
				   int                   broadcast,
				   unsigned char         source_address,
				   unsigned char         source_lun)
{
	int i = broadcast;

	/* Format the IPMB header data. */
	smi_msg->data[0] = (IPMI_NETFN_APP_REQUEST << 2);
	smi_msg->data[1] = IPMI_SEND_MSG_CMD;
	smi_msg->data[2] = ipmb_addr->channel;
	if (broadcast)
		smi_msg->data[3] = 0;
	smi_msg->data[i+3] = ipmb_addr->slave_addr;
	smi_msg->data[i+4] = (msg->netfn << 2) | (ipmb_addr->lun & 0x3);
	smi_msg->data[i+5] = ipmb_checksum(&(smi_msg->data[i+3]), 2);
	smi_msg->data[i+6] = source_address;
	smi_msg->data[i+7] = (ipmb_seq << 2) | source_lun;
	smi_msg->data[i+8] = msg->cmd;

	/* Now tack on the data to the message. */
	if (msg->data_len > 0)
		memcpy(&(smi_msg->data[i+9]), msg->data,
		       msg->data_len);
	smi_msg->data_size = msg->data_len + 9;

	/* Now calculate the checksum and tack it on. */
	smi_msg->data[i+smi_msg->data_size]
		= ipmb_checksum(&(smi_msg->data[i+6]),
				smi_msg->data_size-6);

	/* Add on the checksum size and the offset from the
	   broadcast. */
	smi_msg->data_size += 1 + i;

	smi_msg->msgid = msgid;
}

static inline void format_lan_msg(struct ipmi_smi_msg   *smi_msg,
				  struct kernel_ipmi_msg *msg,
				  struct ipmi_lan_addr  *lan_addr,
				  long                  msgid,
				  unsigned char         ipmb_seq,
				  unsigned char         source_lun)
{
	/* Format the IPMB header data. */
	smi_msg->data[0] = (IPMI_NETFN_APP_REQUEST << 2);
	smi_msg->data[1] = IPMI_SEND_MSG_CMD;
	smi_msg->data[2] = lan_addr->channel;
	smi_msg->data[3] = lan_addr->session_handle;
	smi_msg->data[4] = lan_addr->remote_SWID;
	smi_msg->data[5] = (msg->netfn << 2) | (lan_addr->lun & 0x3);
	smi_msg->data[6] = ipmb_checksum(&(smi_msg->data[4]), 2);
	smi_msg->data[7] = lan_addr->local_SWID;
	smi_msg->data[8] = (ipmb_seq << 2) | source_lun;
	smi_msg->data[9] = msg->cmd;

	/* Now tack on the data to the message. */
	if (msg->data_len > 0)
		memcpy(&(smi_msg->data[10]), msg->data,
		       msg->data_len);
	smi_msg->data_size = msg->data_len + 10;

	/* Now calculate the checksum and tack it on. */
	smi_msg->data[smi_msg->data_size]
		= ipmb_checksum(&(smi_msg->data[7]),
				smi_msg->data_size-7);

	/* Add on the checksum size and the offset from the
	   broadcast. */
	smi_msg->data_size += 1;

	smi_msg->msgid = msgid;
}

/* Separate from ipmi_request so that the user does not have to be
   supplied in certain circumstances (mainly at panic time).  If
   messages are supplied, they will be freed, even if an error
   occurs. */
static int i_ipmi_request(ipmi_user_t          user,
			  ipmi_smi_t           intf,
			  struct ipmi_addr     *addr,
			  long                 msgid,
			  struct kernel_ipmi_msg *msg,
			  void                 *user_msg_data,
			  void                 *supplied_smi,
			  struct ipmi_recv_msg *supplied_recv,
			  int                  priority,
			  unsigned char        source_address,
			  unsigned char        source_lun,
			  int                  retries,
			  unsigned int         retry_time_ms)
{
	int                  rv = 0;
	struct ipmi_smi_msg  *smi_msg;
	struct ipmi_recv_msg *recv_msg;
	unsigned long        flags;


	if (supplied_recv) {
		recv_msg = supplied_recv;
	} else {
		recv_msg = ipmi_alloc_recv_msg();
		if (recv_msg == NULL) {
			return -ENOMEM;
		}
	}
	recv_msg->user_msg_data = user_msg_data;

	if (supplied_smi) {
		smi_msg = (struct ipmi_smi_msg *) supplied_smi;
	} else {
		smi_msg = ipmi_alloc_smi_msg();
		if (smi_msg == NULL) {
			ipmi_free_recv_msg(recv_msg);
			return -ENOMEM;
		}
	}

	recv_msg->user = user;
	if (user)
		kref_get(&user->refcount);
	recv_msg->msgid = msgid;
	/* Store the message to send in the receive message so timeout
	   responses can get the proper response data. */
	recv_msg->msg = *msg;

	if (addr->addr_type == IPMI_SYSTEM_INTERFACE_ADDR_TYPE) {
		struct ipmi_system_interface_addr *smi_addr;

		if (msg->netfn & 1) {
			/* Responses are not allowed to the SMI. */
			rv = -EINVAL;
			goto out_err;
		}

		smi_addr = (struct ipmi_system_interface_addr *) addr;
		if (smi_addr->lun > 3) {
			spin_lock_irqsave(&intf->counter_lock, flags);
			intf->sent_invalid_commands++;
			spin_unlock_irqrestore(&intf->counter_lock, flags);
			rv = -EINVAL;
			goto out_err;
		}

		memcpy(&recv_msg->addr, smi_addr, sizeof(*smi_addr));

		if ((msg->netfn == IPMI_NETFN_APP_REQUEST)
		    && ((msg->cmd == IPMI_SEND_MSG_CMD)
			|| (msg->cmd == IPMI_GET_MSG_CMD)
			|| (msg->cmd == IPMI_READ_EVENT_MSG_BUFFER_CMD)))
		{
			/* We don't let the user do these, since we manage
			   the sequence numbers. */
			spin_lock_irqsave(&intf->counter_lock, flags);
			intf->sent_invalid_commands++;
			spin_unlock_irqrestore(&intf->counter_lock, flags);
			rv = -EINVAL;
			goto out_err;
		}

		if ((msg->data_len + 2) > IPMI_MAX_MSG_LENGTH) {
			spin_lock_irqsave(&intf->counter_lock, flags);
			intf->sent_invalid_commands++;
			spin_unlock_irqrestore(&intf->counter_lock, flags);
			rv = -EMSGSIZE;
			goto out_err;
		}

		smi_msg->data[0] = (msg->netfn << 2) | (smi_addr->lun & 0x3);
		smi_msg->data[1] = msg->cmd;
		smi_msg->msgid = msgid;
		smi_msg->user_data = recv_msg;
		if (msg->data_len > 0)
			memcpy(&(smi_msg->data[2]), msg->data, msg->data_len);
		smi_msg->data_size = msg->data_len + 2;
		spin_lock_irqsave(&intf->counter_lock, flags);
		intf->sent_local_commands++;
		spin_unlock_irqrestore(&intf->counter_lock, flags);
	} else if ((addr->addr_type == IPMI_IPMB_ADDR_TYPE)
		   || (addr->addr_type == IPMI_IPMB_BROADCAST_ADDR_TYPE))
	{
		struct ipmi_ipmb_addr *ipmb_addr;
		unsigned char         ipmb_seq;
		long                  seqid;
		int                   broadcast = 0;

		if (addr->channel >= IPMI_MAX_CHANNELS) {
		        spin_lock_irqsave(&intf->counter_lock, flags);
			intf->sent_invalid_commands++;
			spin_unlock_irqrestore(&intf->counter_lock, flags);
			rv = -EINVAL;
			goto out_err;
		}

		if (intf->channels[addr->channel].medium
		    != IPMI_CHANNEL_MEDIUM_IPMB)
		{
			spin_lock_irqsave(&intf->counter_lock, flags);
			intf->sent_invalid_commands++;
			spin_unlock_irqrestore(&intf->counter_lock, flags);
			rv = -EINVAL;
			goto out_err;
		}

		if (retries < 0) {
		    if (addr->addr_type == IPMI_IPMB_BROADCAST_ADDR_TYPE)
			retries = 0; /* Don't retry broadcasts. */
		    else
			retries = 4;
		}
		if (addr->addr_type == IPMI_IPMB_BROADCAST_ADDR_TYPE) {
		    /* Broadcasts add a zero at the beginning of the
		       message, but otherwise is the same as an IPMB
		       address. */
		    addr->addr_type = IPMI_IPMB_ADDR_TYPE;
		    broadcast = 1;
		}


		/* Default to 1 second retries. */
		if (retry_time_ms == 0)
		    retry_time_ms = 1000;

		/* 9 for the header and 1 for the checksum, plus
                   possibly one for the broadcast. */
		if ((msg->data_len + 10 + broadcast) > IPMI_MAX_MSG_LENGTH) {
			spin_lock_irqsave(&intf->counter_lock, flags);
			intf->sent_invalid_commands++;
			spin_unlock_irqrestore(&intf->counter_lock, flags);
			rv = -EMSGSIZE;
			goto out_err;
		}

		ipmb_addr = (struct ipmi_ipmb_addr *) addr;
		if (ipmb_addr->lun > 3) {
			spin_lock_irqsave(&intf->counter_lock, flags);
			intf->sent_invalid_commands++;
			spin_unlock_irqrestore(&intf->counter_lock, flags);
			rv = -EINVAL;
			goto out_err;
		}

		memcpy(&recv_msg->addr, ipmb_addr, sizeof(*ipmb_addr));

		if (recv_msg->msg.netfn & 0x1) {
			/* It's a response, so use the user's sequence
                           from msgid. */
			spin_lock_irqsave(&intf->counter_lock, flags);
			intf->sent_ipmb_responses++;
			spin_unlock_irqrestore(&intf->counter_lock, flags);
			format_ipmb_msg(smi_msg, msg, ipmb_addr, msgid,
					msgid, broadcast,
					source_address, source_lun);

			/* Save the receive message so we can use it
			   to deliver the response. */
			smi_msg->user_data = recv_msg;
		} else {
			/* It's a command, so get a sequence for it. */

			spin_lock_irqsave(&(intf->seq_lock), flags);

			spin_lock(&intf->counter_lock);
			intf->sent_ipmb_commands++;
			spin_unlock(&intf->counter_lock);

			/* Create a sequence number with a 1 second
                           timeout and 4 retries. */
			rv = intf_next_seq(intf,
					   recv_msg,
					   retry_time_ms,
					   retries,
					   broadcast,
					   &ipmb_seq,
					   &seqid);
			if (rv) {
				/* We have used up all the sequence numbers,
				   probably, so abort. */
				spin_unlock_irqrestore(&(intf->seq_lock),
						       flags);
				goto out_err;
			}

			/* Store the sequence number in the message,
                           so that when the send message response
                           comes back we can start the timer. */
			format_ipmb_msg(smi_msg, msg, ipmb_addr,
					STORE_SEQ_IN_MSGID(ipmb_seq, seqid),
					ipmb_seq, broadcast,
					source_address, source_lun);

			/* Copy the message into the recv message data, so we
			   can retransmit it later if necessary. */
			memcpy(recv_msg->msg_data, smi_msg->data,
			       smi_msg->data_size);
			recv_msg->msg.data = recv_msg->msg_data;
			recv_msg->msg.data_len = smi_msg->data_size;

			/* We don't unlock until here, because we need
                           to copy the completed message into the
                           recv_msg before we release the lock.
                           Otherwise, race conditions may bite us.  I
                           know that's pretty paranoid, but I prefer
                           to be correct. */
			spin_unlock_irqrestore(&(intf->seq_lock), flags);
		}
	} else if (addr->addr_type == IPMI_LAN_ADDR_TYPE) {
		struct ipmi_lan_addr  *lan_addr;
		unsigned char         ipmb_seq;
		long                  seqid;

		if (addr->channel >= IPMI_NUM_CHANNELS) {
			spin_lock_irqsave(&intf->counter_lock, flags);
			intf->sent_invalid_commands++;
			spin_unlock_irqrestore(&intf->counter_lock, flags);
			rv = -EINVAL;
			goto out_err;
		}

		if ((intf->channels[addr->channel].medium
		    != IPMI_CHANNEL_MEDIUM_8023LAN)
		    && (intf->channels[addr->channel].medium
			!= IPMI_CHANNEL_MEDIUM_ASYNC))
		{
			spin_lock_irqsave(&intf->counter_lock, flags);
			intf->sent_invalid_commands++;
			spin_unlock_irqrestore(&intf->counter_lock, flags);
			rv = -EINVAL;
			goto out_err;
		}

		retries = 4;

		/* Default to 1 second retries. */
		if (retry_time_ms == 0)
		    retry_time_ms = 1000;

		/* 11 for the header and 1 for the checksum. */
		if ((msg->data_len + 12) > IPMI_MAX_MSG_LENGTH) {
			spin_lock_irqsave(&intf->counter_lock, flags);
			intf->sent_invalid_commands++;
			spin_unlock_irqrestore(&intf->counter_lock, flags);
			rv = -EMSGSIZE;
			goto out_err;
		}

		lan_addr = (struct ipmi_lan_addr *) addr;
		if (lan_addr->lun > 3) {
			spin_lock_irqsave(&intf->counter_lock, flags);
			intf->sent_invalid_commands++;
			spin_unlock_irqrestore(&intf->counter_lock, flags);
			rv = -EINVAL;
			goto out_err;
		}

		memcpy(&recv_msg->addr, lan_addr, sizeof(*lan_addr));

		if (recv_msg->msg.netfn & 0x1) {
			/* It's a response, so use the user's sequence
                           from msgid. */
			spin_lock_irqsave(&intf->counter_lock, flags);
			intf->sent_lan_responses++;
			spin_unlock_irqrestore(&intf->counter_lock, flags);
			format_lan_msg(smi_msg, msg, lan_addr, msgid,
				       msgid, source_lun);

			/* Save the receive message so we can use it
			   to deliver the response. */
			smi_msg->user_data = recv_msg;
		} else {
			/* It's a command, so get a sequence for it. */

			spin_lock_irqsave(&(intf->seq_lock), flags);

			spin_lock(&intf->counter_lock);
			intf->sent_lan_commands++;
			spin_unlock(&intf->counter_lock);

			/* Create a sequence number with a 1 second
                           timeout and 4 retries. */
			rv = intf_next_seq(intf,
					   recv_msg,
					   retry_time_ms,
					   retries,
					   0,
					   &ipmb_seq,
					   &seqid);
			if (rv) {
				/* We have used up all the sequence numbers,
				   probably, so abort. */
				spin_unlock_irqrestore(&(intf->seq_lock),
						       flags);
				goto out_err;
			}

			/* Store the sequence number in the message,
                           so that when the send message response
                           comes back we can start the timer. */
			format_lan_msg(smi_msg, msg, lan_addr,
				       STORE_SEQ_IN_MSGID(ipmb_seq, seqid),
				       ipmb_seq, source_lun);

			/* Copy the message into the recv message data, so we
			   can retransmit it later if necessary. */
			memcpy(recv_msg->msg_data, smi_msg->data,
			       smi_msg->data_size);
			recv_msg->msg.data = recv_msg->msg_data;
			recv_msg->msg.data_len = smi_msg->data_size;

			/* We don't unlock until here, because we need
                           to copy the completed message into the
                           recv_msg before we release the lock.
                           Otherwise, race conditions may bite us.  I
                           know that's pretty paranoid, but I prefer
                           to be correct. */
			spin_unlock_irqrestore(&(intf->seq_lock), flags);
		}
	} else {
	    /* Unknown address type. */
		spin_lock_irqsave(&intf->counter_lock, flags);
		intf->sent_invalid_commands++;
		spin_unlock_irqrestore(&intf->counter_lock, flags);
		rv = -EINVAL;
		goto out_err;
	}

#ifdef DEBUG_MSGING
	{
		int m;
		for (m = 0; m < smi_msg->data_size; m++)
			printk(" %2.2x", smi_msg->data[m]);
		printk("\n");
	}
#endif
	intf->handlers->sender(intf->send_info, smi_msg, priority);

	return 0;

 out_err:
	ipmi_free_smi_msg(smi_msg);
	ipmi_free_recv_msg(recv_msg);
	return rv;
}

static int check_addr(ipmi_smi_t       intf,
		      struct ipmi_addr *addr,
		      unsigned char    *saddr,
		      unsigned char    *lun)
{
	if (addr->channel >= IPMI_MAX_CHANNELS)
		return -EINVAL;
	*lun = intf->channels[addr->channel].lun;
	*saddr = intf->channels[addr->channel].address;
	return 0;
}

int ipmi_request_settime(ipmi_user_t      user,
			 struct ipmi_addr *addr,
			 long             msgid,
			 struct kernel_ipmi_msg  *msg,
			 void             *user_msg_data,
			 int              priority,
			 int              retries,
			 unsigned int     retry_time_ms)
{
	unsigned char saddr, lun;
	int           rv;

	if (! user)
		return -EINVAL;
	rv = check_addr(user->intf, addr, &saddr, &lun);
	if (rv)
		return rv;
	return i_ipmi_request(user,
			      user->intf,
			      addr,
			      msgid,
			      msg,
			      user_msg_data,
			      NULL, NULL,
			      priority,
			      saddr,
			      lun,
			      retries,
			      retry_time_ms);
}

int ipmi_request_supply_msgs(ipmi_user_t          user,
			     struct ipmi_addr     *addr,
			     long                 msgid,
			     struct kernel_ipmi_msg *msg,
			     void                 *user_msg_data,
			     void                 *supplied_smi,
			     struct ipmi_recv_msg *supplied_recv,
			     int                  priority)
{
	unsigned char saddr, lun;
	int           rv;

	if (! user)
		return -EINVAL;
	rv = check_addr(user->intf, addr, &saddr, &lun);
	if (rv)
		return rv;
	return i_ipmi_request(user,
			      user->intf,
			      addr,
			      msgid,
			      msg,
			      user_msg_data,
			      supplied_smi,
			      supplied_recv,
			      priority,
			      saddr,
			      lun,
			      -1, 0);
}

static int ipmb_file_read_proc(char *page, char **start, off_t off,
			       int count, int *eof, void *data)
{
	char       *out = (char *) page;
	ipmi_smi_t intf = data;
	int        i;
	int        rv= 0;

	for (i = 0; i < IPMI_MAX_CHANNELS; i++)
		rv += sprintf(out+rv, "%x ", intf->channels[i].address);
	out[rv-1] = '\n'; /* Replace the final space with a newline */
	out[rv] = '\0';
	rv++;
	return rv;
}

static int version_file_read_proc(char *page, char **start, off_t off,
				  int count, int *eof, void *data)
{
	char       *out = (char *) page;
	ipmi_smi_t intf = data;

	return sprintf(out, "%d.%d\n",
		       intf->version_major, intf->version_minor);
}

static int stat_file_read_proc(char *page, char **start, off_t off,
			       int count, int *eof, void *data)
{
	char       *out = (char *) page;
	ipmi_smi_t intf = data;

	out += sprintf(out, "sent_invalid_commands:       %d\n",
		       intf->sent_invalid_commands);
	out += sprintf(out, "sent_local_commands:         %d\n",
		       intf->sent_local_commands);
	out += sprintf(out, "handled_local_responses:     %d\n",
		       intf->handled_local_responses);
	out += sprintf(out, "unhandled_local_responses:   %d\n",
		       intf->unhandled_local_responses);
	out += sprintf(out, "sent_ipmb_commands:          %d\n",
		       intf->sent_ipmb_commands);
	out += sprintf(out, "sent_ipmb_command_errs:      %d\n",
		       intf->sent_ipmb_command_errs);
	out += sprintf(out, "retransmitted_ipmb_commands: %d\n",
		       intf->retransmitted_ipmb_commands);
	out += sprintf(out, "timed_out_ipmb_commands:     %d\n",
		       intf->timed_out_ipmb_commands);
	out += sprintf(out, "timed_out_ipmb_broadcasts:   %d\n",
		       intf->timed_out_ipmb_broadcasts);
	out += sprintf(out, "sent_ipmb_responses:         %d\n",
		       intf->sent_ipmb_responses);
	out += sprintf(out, "handled_ipmb_responses:      %d\n",
		       intf->handled_ipmb_responses);
	out += sprintf(out, "invalid_ipmb_responses:      %d\n",
		       intf->invalid_ipmb_responses);
	out += sprintf(out, "unhandled_ipmb_responses:    %d\n",
		       intf->unhandled_ipmb_responses);
	out += sprintf(out, "sent_lan_commands:           %d\n",
		       intf->sent_lan_commands);
	out += sprintf(out, "sent_lan_command_errs:       %d\n",
		       intf->sent_lan_command_errs);
	out += sprintf(out, "retransmitted_lan_commands:  %d\n",
		       intf->retransmitted_lan_commands);
	out += sprintf(out, "timed_out_lan_commands:      %d\n",
		       intf->timed_out_lan_commands);
	out += sprintf(out, "sent_lan_responses:          %d\n",
		       intf->sent_lan_responses);
	out += sprintf(out, "handled_lan_responses:       %d\n",
		       intf->handled_lan_responses);
	out += sprintf(out, "invalid_lan_responses:       %d\n",
		       intf->invalid_lan_responses);
	out += sprintf(out, "unhandled_lan_responses:     %d\n",
		       intf->unhandled_lan_responses);
	out += sprintf(out, "handled_commands:            %d\n",
		       intf->handled_commands);
	out += sprintf(out, "invalid_commands:            %d\n",
		       intf->invalid_commands);
	out += sprintf(out, "unhandled_commands:          %d\n",
		       intf->unhandled_commands);
	out += sprintf(out, "invalid_events:              %d\n",
		       intf->invalid_events);
	out += sprintf(out, "events:                      %d\n",
		       intf->events);

	return (out - ((char *) page));
}

int ipmi_smi_add_proc_entry(ipmi_smi_t smi, char *name,
			    read_proc_t *read_proc, write_proc_t *write_proc,
			    void *data, struct module *owner)
{
	int                    rv = 0;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry  *file;
	struct ipmi_proc_entry *entry;

	/* Create a list element. */
	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;
	entry->name = kmalloc(strlen(name)+1, GFP_KERNEL);
	if (!entry->name) {
		kfree(entry);
		return -ENOMEM;
	}
	strcpy(entry->name, name);

	file = create_proc_entry(name, 0, smi->proc_dir);
	if (!file) {
		kfree(entry->name);
		kfree(entry);
		rv = -ENOMEM;
	} else {
		file->nlink = 1;
		file->data = data;
		file->read_proc = read_proc;
		file->write_proc = write_proc;
		file->owner = owner;

		spin_lock(&smi->proc_entry_lock);
		/* Stick it on the list. */
		entry->next = smi->proc_entries;
		smi->proc_entries = entry;
		spin_unlock(&smi->proc_entry_lock);
	}
#endif /* CONFIG_PROC_FS */

	return rv;
}

static int add_proc_entries(ipmi_smi_t smi, int num)
{
	int rv = 0;

#ifdef CONFIG_PROC_FS
	sprintf(smi->proc_dir_name, "%d", num);
	smi->proc_dir = proc_mkdir(smi->proc_dir_name, proc_ipmi_root);
	if (!smi->proc_dir)
		rv = -ENOMEM;
	else {
		smi->proc_dir->owner = THIS_MODULE;
	}

	if (rv == 0)
		rv = ipmi_smi_add_proc_entry(smi, "stats",
					     stat_file_read_proc, NULL,
					     smi, THIS_MODULE);

	if (rv == 0)
		rv = ipmi_smi_add_proc_entry(smi, "ipmb",
					     ipmb_file_read_proc, NULL,
					     smi, THIS_MODULE);

	if (rv == 0)
		rv = ipmi_smi_add_proc_entry(smi, "version",
					     version_file_read_proc, NULL,
					     smi, THIS_MODULE);
#endif /* CONFIG_PROC_FS */

	return rv;
}

static void remove_proc_entries(ipmi_smi_t smi)
{
#ifdef CONFIG_PROC_FS
	struct ipmi_proc_entry *entry;

	spin_lock(&smi->proc_entry_lock);
	while (smi->proc_entries) {
		entry = smi->proc_entries;
		smi->proc_entries = entry->next;

		remove_proc_entry(entry->name, smi->proc_dir);
		kfree(entry->name);
		kfree(entry);
	}
	spin_unlock(&smi->proc_entry_lock);
	remove_proc_entry(smi->proc_dir_name, proc_ipmi_root);
#endif /* CONFIG_PROC_FS */
}

static int
send_channel_info_cmd(ipmi_smi_t intf, int chan)
{
	struct kernel_ipmi_msg            msg;
	unsigned char                     data[1];
	struct ipmi_system_interface_addr si;

	si.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	si.channel = IPMI_BMC_CHANNEL;
	si.lun = 0;

	msg.netfn = IPMI_NETFN_APP_REQUEST;
	msg.cmd = IPMI_GET_CHANNEL_INFO_CMD;
	msg.data = data;
	msg.data_len = 1;
	data[0] = chan;
	return i_ipmi_request(NULL,
			      intf,
			      (struct ipmi_addr *) &si,
			      0,
			      &msg,
			      intf,
			      NULL,
			      NULL,
			      0,
			      intf->channels[0].address,
			      intf->channels[0].lun,
			      -1, 0);
}

static void
channel_handler(ipmi_smi_t intf, struct ipmi_recv_msg *msg)
{
	int rv = 0;
	int chan;

	if ((msg->addr.addr_type == IPMI_SYSTEM_INTERFACE_ADDR_TYPE)
	    && (msg->msg.netfn == IPMI_NETFN_APP_RESPONSE)
	    && (msg->msg.cmd == IPMI_GET_CHANNEL_INFO_CMD))
	{
		/* It's the one we want */
		if (msg->msg.data[0] != 0) {
			/* Got an error from the channel, just go on. */

			if (msg->msg.data[0] == IPMI_INVALID_COMMAND_ERR) {
				/* If the MC does not support this
				   command, that is legal.  We just
				   assume it has one IPMB at channel
				   zero. */
				intf->channels[0].medium
					= IPMI_CHANNEL_MEDIUM_IPMB;
				intf->channels[0].protocol
					= IPMI_CHANNEL_PROTOCOL_IPMB;
				rv = -ENOSYS;

				intf->curr_channel = IPMI_MAX_CHANNELS;
				wake_up(&intf->waitq);
				goto out;
			}
			goto next_channel;
		}
		if (msg->msg.data_len < 4) {
			/* Message not big enough, just go on. */
			goto next_channel;
		}
		chan = intf->curr_channel;
		intf->channels[chan].medium = msg->msg.data[2] & 0x7f;
		intf->channels[chan].protocol = msg->msg.data[3] & 0x1f;

	next_channel:
		intf->curr_channel++;
		if (intf->curr_channel >= IPMI_MAX_CHANNELS)
			wake_up(&intf->waitq);
		else
			rv = send_channel_info_cmd(intf, intf->curr_channel);

		if (rv) {
			/* Got an error somehow, just give up. */
			intf->curr_channel = IPMI_MAX_CHANNELS;
			wake_up(&intf->waitq);

			printk(KERN_WARNING PFX
			       "Error sending channel information: %d\n",
			       rv);
		}
	}
 out:
	return;
}

int ipmi_register_smi(struct ipmi_smi_handlers *handlers,
		      void		       *send_info,
		      unsigned char            version_major,
		      unsigned char            version_minor,
		      unsigned char            slave_addr,
		      ipmi_smi_t               *new_intf)
{
	int              i, j;
	int              rv;
	ipmi_smi_t       intf;
	unsigned long    flags;


	/* Make sure the driver is actually initialized, this handles
	   problems with initialization order. */
	if (!initialized) {
		rv = ipmi_init_msghandler();
		if (rv)
			return rv;
		/* The init code doesn't return an error if it was turned
		   off, but it won't initialize.  Check that. */
		if (!initialized)
			return -ENODEV;
	}

	intf = kmalloc(sizeof(*intf), GFP_KERNEL);
	if (!intf)
		return -ENOMEM;
	memset(intf, 0, sizeof(*intf));
	intf->intf_num = -1;
	kref_init(&intf->refcount);
	intf->version_major = version_major;
	intf->version_minor = version_minor;
	for (j = 0; j < IPMI_MAX_CHANNELS; j++) {
		intf->channels[j].address = IPMI_BMC_SLAVE_ADDR;
		intf->channels[j].lun = 2;
	}
	if (slave_addr != 0)
		intf->channels[0].address = slave_addr;
	INIT_LIST_HEAD(&intf->users);
	intf->handlers = handlers;
	intf->send_info = send_info;
	spin_lock_init(&intf->seq_lock);
	for (j = 0; j < IPMI_IPMB_NUM_SEQ; j++) {
		intf->seq_table[j].inuse = 0;
		intf->seq_table[j].seqid = 0;
	}
	intf->curr_seq = 0;
#ifdef CONFIG_PROC_FS
	spin_lock_init(&intf->proc_entry_lock);
#endif
	spin_lock_init(&intf->waiting_msgs_lock);
	INIT_LIST_HEAD(&intf->waiting_msgs);
	spin_lock_init(&intf->events_lock);
	INIT_LIST_HEAD(&intf->waiting_events);
	intf->waiting_events_count = 0;
	init_MUTEX(&intf->cmd_rcvrs_lock);
	INIT_LIST_HEAD(&intf->cmd_rcvrs);
	init_waitqueue_head(&intf->waitq);

	spin_lock_init(&intf->counter_lock);
	intf->proc_dir = NULL;

	rv = -ENOMEM;
	spin_lock_irqsave(&interfaces_lock, flags);
	for (i = 0; i < MAX_IPMI_INTERFACES; i++) {
		if (ipmi_interfaces[i] == NULL) {
			intf->intf_num = i;
			/* Reserve the entry till we are done. */
			ipmi_interfaces[i] = IPMI_INVALID_INTERFACE_ENTRY;
			rv = 0;
			break;
		}
	}
	spin_unlock_irqrestore(&interfaces_lock, flags);
	if (rv)
		goto out;

	/* FIXME - this is an ugly kludge, this sets the intf for the
	   caller before sending any messages with it. */
	*new_intf = intf;

	if ((version_major > 1)
	    || ((version_major == 1) && (version_minor >= 5)))
	{
		/* Start scanning the channels to see what is
		   available. */
		intf->null_user_handler = channel_handler;
		intf->curr_channel = 0;
		rv = send_channel_info_cmd(intf, 0);
		if (rv)
			goto out;

		/* Wait for the channel info to be read. */
		wait_event(intf->waitq,
			   intf->curr_channel >= IPMI_MAX_CHANNELS);
	} else {
		/* Assume a single IPMB channel at zero. */
		intf->channels[0].medium = IPMI_CHANNEL_MEDIUM_IPMB;
		intf->channels[0].protocol = IPMI_CHANNEL_PROTOCOL_IPMB;
	}

	if (rv == 0)
		rv = add_proc_entries(intf, i);

 out:
	if (rv) {
		if (intf->proc_dir)
			remove_proc_entries(intf);
		kref_put(&intf->refcount, intf_free);
		if (i < MAX_IPMI_INTERFACES) {
			spin_lock_irqsave(&interfaces_lock, flags);
			ipmi_interfaces[i] = NULL;
			spin_unlock_irqrestore(&interfaces_lock, flags);
		}
	} else {
		spin_lock_irqsave(&interfaces_lock, flags);
		ipmi_interfaces[i] = intf;
		spin_unlock_irqrestore(&interfaces_lock, flags);
		call_smi_watchers(i);
	}

	return rv;
}

int ipmi_unregister_smi(ipmi_smi_t intf)
{
	int                     i;
	struct ipmi_smi_watcher *w;
	unsigned long           flags;

	spin_lock_irqsave(&interfaces_lock, flags);
	for (i = 0; i < MAX_IPMI_INTERFACES; i++) {
		if (ipmi_interfaces[i] == intf) {
			/* Set the interface number reserved until we
			 * are done. */
			ipmi_interfaces[i] = IPMI_INVALID_INTERFACE_ENTRY;
			intf->intf_num = -1;
			break;
		}
	}
	spin_unlock_irqrestore(&interfaces_lock,flags);

	if (i == MAX_IPMI_INTERFACES)
		return -ENODEV;

	remove_proc_entries(intf);

	/* Call all the watcher interfaces to tell them that
	   an interface is gone. */
	down_read(&smi_watchers_sem);
	list_for_each_entry(w, &smi_watchers, link)
		w->smi_gone(i);
	up_read(&smi_watchers_sem);

	/* Allow the entry to be reused now. */
	spin_lock_irqsave(&interfaces_lock, flags);
	ipmi_interfaces[i] = NULL;
	spin_unlock_irqrestore(&interfaces_lock,flags);

	kref_put(&intf->refcount, intf_free);
	return 0;
}

static int handle_ipmb_get_msg_rsp(ipmi_smi_t          intf,
				   struct ipmi_smi_msg *msg)
{
	struct ipmi_ipmb_addr ipmb_addr;
	struct ipmi_recv_msg  *recv_msg;
	unsigned long         flags;

	
	/* This is 11, not 10, because the response must contain a
	 * completion code. */
	if (msg->rsp_size < 11) {
		/* Message not big enough, just ignore it. */
		spin_lock_irqsave(&intf->counter_lock, flags);
		intf->invalid_ipmb_responses++;
		spin_unlock_irqrestore(&intf->counter_lock, flags);
		return 0;
	}

	if (msg->rsp[2] != 0) {
		/* An error getting the response, just ignore it. */
		return 0;
	}

	ipmb_addr.addr_type = IPMI_IPMB_ADDR_TYPE;
	ipmb_addr.slave_addr = msg->rsp[6];
	ipmb_addr.channel = msg->rsp[3] & 0x0f;
	ipmb_addr.lun = msg->rsp[7] & 3;

	/* It's a response from a remote entity.  Look up the sequence
	   number and handle the response. */
	if (intf_find_seq(intf,
			  msg->rsp[7] >> 2,
			  msg->rsp[3] & 0x0f,
			  msg->rsp[8],
			  (msg->rsp[4] >> 2) & (~1),
			  (struct ipmi_addr *) &(ipmb_addr),
			  &recv_msg))
	{
		/* We were unable to find the sequence number,
		   so just nuke the message. */
		spin_lock_irqsave(&intf->counter_lock, flags);
		intf->unhandled_ipmb_responses++;
		spin_unlock_irqrestore(&intf->counter_lock, flags);
		return 0;
	}

	memcpy(recv_msg->msg_data,
	       &(msg->rsp[9]),
	       msg->rsp_size - 9);
	/* THe other fields matched, so no need to set them, except
           for netfn, which needs to be the response that was
           returned, not the request value. */
	recv_msg->msg.netfn = msg->rsp[4] >> 2;
	recv_msg->msg.data = recv_msg->msg_data;
	recv_msg->msg.data_len = msg->rsp_size - 10;
	recv_msg->recv_type = IPMI_RESPONSE_RECV_TYPE;
	spin_lock_irqsave(&intf->counter_lock, flags);
	intf->handled_ipmb_responses++;
	spin_unlock_irqrestore(&intf->counter_lock, flags);
	deliver_response(recv_msg);

	return 0;
}

static int handle_ipmb_get_msg_cmd(ipmi_smi_t          intf,
				   struct ipmi_smi_msg *msg)
{
	struct cmd_rcvr          *rcvr;
	int                      rv = 0;
	unsigned char            netfn;
	unsigned char            cmd;
	ipmi_user_t              user = NULL;
	struct ipmi_ipmb_addr    *ipmb_addr;
	struct ipmi_recv_msg     *recv_msg;
	unsigned long            flags;

	if (msg->rsp_size < 10) {
		/* Message not big enough, just ignore it. */
		spin_lock_irqsave(&intf->counter_lock, flags);
		intf->invalid_commands++;
		spin_unlock_irqrestore(&intf->counter_lock, flags);
		return 0;
	}

	if (msg->rsp[2] != 0) {
		/* An error getting the response, just ignore it. */
		return 0;
	}

	netfn = msg->rsp[4] >> 2;
	cmd = msg->rsp[8];

	rcu_read_lock();
	rcvr = find_cmd_rcvr(intf, netfn, cmd);
	if (rcvr) {
		user = rcvr->user;
		kref_get(&user->refcount);
	} else
		user = NULL;
	rcu_read_unlock();

	if (user == NULL) {
		/* We didn't find a user, deliver an error response. */
		spin_lock_irqsave(&intf->counter_lock, flags);
		intf->unhandled_commands++;
		spin_unlock_irqrestore(&intf->counter_lock, flags);

		msg->data[0] = (IPMI_NETFN_APP_REQUEST << 2);
		msg->data[1] = IPMI_SEND_MSG_CMD;
		msg->data[2] = msg->rsp[3];
		msg->data[3] = msg->rsp[6];
                msg->data[4] = ((netfn + 1) << 2) | (msg->rsp[7] & 0x3);
		msg->data[5] = ipmb_checksum(&(msg->data[3]), 2);
		msg->data[6] = intf->channels[msg->rsp[3] & 0xf].address;
                /* rqseq/lun */
                msg->data[7] = (msg->rsp[7] & 0xfc) | (msg->rsp[4] & 0x3);
		msg->data[8] = msg->rsp[8]; /* cmd */
		msg->data[9] = IPMI_INVALID_CMD_COMPLETION_CODE;
		msg->data[10] = ipmb_checksum(&(msg->data[6]), 4);
		msg->data_size = 11;

#ifdef DEBUG_MSGING
	{
		int m;
		printk("Invalid command:");
		for (m = 0; m < msg->data_size; m++)
			printk(" %2.2x", msg->data[m]);
		printk("\n");
	}
#endif
		intf->handlers->sender(intf->send_info, msg, 0);

		rv = -1; /* We used the message, so return the value that
			    causes it to not be freed or queued. */
	} else {
		/* Deliver the message to the user. */
		spin_lock_irqsave(&intf->counter_lock, flags);
		intf->handled_commands++;
		spin_unlock_irqrestore(&intf->counter_lock, flags);

		recv_msg = ipmi_alloc_recv_msg();
		if (! recv_msg) {
			/* We couldn't allocate memory for the
                           message, so requeue it for handling
                           later. */
			rv = 1;
			kref_put(&user->refcount, free_user);
		} else {
			/* Extract the source address from the data. */
			ipmb_addr = (struct ipmi_ipmb_addr *) &recv_msg->addr;
			ipmb_addr->addr_type = IPMI_IPMB_ADDR_TYPE;
			ipmb_addr->slave_addr = msg->rsp[6];
			ipmb_addr->lun = msg->rsp[7] & 3;
			ipmb_addr->channel = msg->rsp[3] & 0xf;

			/* Extract the rest of the message information
			   from the IPMB header.*/
			recv_msg->user = user;
			recv_msg->recv_type = IPMI_CMD_RECV_TYPE;
			recv_msg->msgid = msg->rsp[7] >> 2;
			recv_msg->msg.netfn = msg->rsp[4] >> 2;
			recv_msg->msg.cmd = msg->rsp[8];
			recv_msg->msg.data = recv_msg->msg_data;

			/* We chop off 10, not 9 bytes because the checksum
			   at the end also needs to be removed. */
			recv_msg->msg.data_len = msg->rsp_size - 10;
			memcpy(recv_msg->msg_data,
			       &(msg->rsp[9]),
			       msg->rsp_size - 10);
			deliver_response(recv_msg);
		}
	}

	return rv;
}

static int handle_lan_get_msg_rsp(ipmi_smi_t          intf,
				  struct ipmi_smi_msg *msg)
{
	struct ipmi_lan_addr  lan_addr;
	struct ipmi_recv_msg  *recv_msg;
	unsigned long         flags;


	/* This is 13, not 12, because the response must contain a
	 * completion code. */
	if (msg->rsp_size < 13) {
		/* Message not big enough, just ignore it. */
		spin_lock_irqsave(&intf->counter_lock, flags);
		intf->invalid_lan_responses++;
		spin_unlock_irqrestore(&intf->counter_lock, flags);
		return 0;
	}

	if (msg->rsp[2] != 0) {
		/* An error getting the response, just ignore it. */
		return 0;
	}

	lan_addr.addr_type = IPMI_LAN_ADDR_TYPE;
	lan_addr.session_handle = msg->rsp[4];
	lan_addr.remote_SWID = msg->rsp[8];
	lan_addr.local_SWID = msg->rsp[5];
	lan_addr.channel = msg->rsp[3] & 0x0f;
	lan_addr.privilege = msg->rsp[3] >> 4;
	lan_addr.lun = msg->rsp[9] & 3;

	/* It's a response from a remote entity.  Look up the sequence
	   number and handle the response. */
	if (intf_find_seq(intf,
			  msg->rsp[9] >> 2,
			  msg->rsp[3] & 0x0f,
			  msg->rsp[10],
			  (msg->rsp[6] >> 2) & (~1),
			  (struct ipmi_addr *) &(lan_addr),
			  &recv_msg))
	{
		/* We were unable to find the sequence number,
		   so just nuke the message. */
		spin_lock_irqsave(&intf->counter_lock, flags);
		intf->unhandled_lan_responses++;
		spin_unlock_irqrestore(&intf->counter_lock, flags);
		return 0;
	}

	memcpy(recv_msg->msg_data,
	       &(msg->rsp[11]),
	       msg->rsp_size - 11);
	/* The other fields matched, so no need to set them, except
           for netfn, which needs to be the response that was
           returned, not the request value. */
	recv_msg->msg.netfn = msg->rsp[6] >> 2;
	recv_msg->msg.data = recv_msg->msg_data;
	recv_msg->msg.data_len = msg->rsp_size - 12;
	recv_msg->recv_type = IPMI_RESPONSE_RECV_TYPE;
	spin_lock_irqsave(&intf->counter_lock, flags);
	intf->handled_lan_responses++;
	spin_unlock_irqrestore(&intf->counter_lock, flags);
	deliver_response(recv_msg);

	return 0;
}

static int handle_lan_get_msg_cmd(ipmi_smi_t          intf,
				  struct ipmi_smi_msg *msg)
{
	struct cmd_rcvr          *rcvr;
	int                      rv = 0;
	unsigned char            netfn;
	unsigned char            cmd;
	ipmi_user_t              user = NULL;
	struct ipmi_lan_addr     *lan_addr;
	struct ipmi_recv_msg     *recv_msg;
	unsigned long            flags;

	if (msg->rsp_size < 12) {
		/* Message not big enough, just ignore it. */
		spin_lock_irqsave(&intf->counter_lock, flags);
		intf->invalid_commands++;
		spin_unlock_irqrestore(&intf->counter_lock, flags);
		return 0;
	}

	if (msg->rsp[2] != 0) {
		/* An error getting the response, just ignore it. */
		return 0;
	}

	netfn = msg->rsp[6] >> 2;
	cmd = msg->rsp[10];

	rcu_read_lock();
	rcvr = find_cmd_rcvr(intf, netfn, cmd);
	if (rcvr) {
		user = rcvr->user;
		kref_get(&user->refcount);
	} else
		user = NULL;
	rcu_read_unlock();

	if (user == NULL) {
		/* We didn't find a user, just give up. */
		spin_lock_irqsave(&intf->counter_lock, flags);
		intf->unhandled_commands++;
		spin_unlock_irqrestore(&intf->counter_lock, flags);

		rv = 0; /* Don't do anything with these messages, just
			   allow them to be freed. */
	} else {
		/* Deliver the message to the user. */
		spin_lock_irqsave(&intf->counter_lock, flags);
		intf->handled_commands++;
		spin_unlock_irqrestore(&intf->counter_lock, flags);

		recv_msg = ipmi_alloc_recv_msg();
		if (! recv_msg) {
			/* We couldn't allocate memory for the
                           message, so requeue it for handling
                           later. */
			rv = 1;
			kref_put(&user->refcount, free_user);
		} else {
			/* Extract the source address from the data. */
			lan_addr = (struct ipmi_lan_addr *) &recv_msg->addr;
			lan_addr->addr_type = IPMI_LAN_ADDR_TYPE;
			lan_addr->session_handle = msg->rsp[4];
			lan_addr->remote_SWID = msg->rsp[8];
			lan_addr->local_SWID = msg->rsp[5];
			lan_addr->lun = msg->rsp[9] & 3;
			lan_addr->channel = msg->rsp[3] & 0xf;
			lan_addr->privilege = msg->rsp[3] >> 4;

			/* Extract the rest of the message information
			   from the IPMB header.*/
			recv_msg->user = user;
			recv_msg->recv_type = IPMI_CMD_RECV_TYPE;
			recv_msg->msgid = msg->rsp[9] >> 2;
			recv_msg->msg.netfn = msg->rsp[6] >> 2;
			recv_msg->msg.cmd = msg->rsp[10];
			recv_msg->msg.data = recv_msg->msg_data;

			/* We chop off 12, not 11 bytes because the checksum
			   at the end also needs to be removed. */
			recv_msg->msg.data_len = msg->rsp_size - 12;
			memcpy(recv_msg->msg_data,
			       &(msg->rsp[11]),
			       msg->rsp_size - 12);
			deliver_response(recv_msg);
		}
	}

	return rv;
}

static void copy_event_into_recv_msg(struct ipmi_recv_msg *recv_msg,
				     struct ipmi_smi_msg  *msg)
{
	struct ipmi_system_interface_addr *smi_addr;
	
	recv_msg->msgid = 0;
	smi_addr = (struct ipmi_system_interface_addr *) &(recv_msg->addr);
	smi_addr->addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	smi_addr->channel = IPMI_BMC_CHANNEL;
	smi_addr->lun = msg->rsp[0] & 3;
	recv_msg->recv_type = IPMI_ASYNC_EVENT_RECV_TYPE;
	recv_msg->msg.netfn = msg->rsp[0] >> 2;
	recv_msg->msg.cmd = msg->rsp[1];
	memcpy(recv_msg->msg_data, &(msg->rsp[3]), msg->rsp_size - 3);
	recv_msg->msg.data = recv_msg->msg_data;
	recv_msg->msg.data_len = msg->rsp_size - 3;
}

static int handle_read_event_rsp(ipmi_smi_t          intf,
				 struct ipmi_smi_msg *msg)
{
	struct ipmi_recv_msg *recv_msg, *recv_msg2;
	struct list_head     msgs;
	ipmi_user_t          user;
	int                  rv = 0;
	int                  deliver_count = 0;
	unsigned long        flags;

	if (msg->rsp_size < 19) {
		/* Message is too small to be an IPMB event. */
		spin_lock_irqsave(&intf->counter_lock, flags);
		intf->invalid_events++;
		spin_unlock_irqrestore(&intf->counter_lock, flags);
		return 0;
	}

	if (msg->rsp[2] != 0) {
		/* An error getting the event, just ignore it. */
		return 0;
	}

	INIT_LIST_HEAD(&msgs);

	spin_lock_irqsave(&intf->events_lock, flags);

	spin_lock(&intf->counter_lock);
	intf->events++;
	spin_unlock(&intf->counter_lock);

	/* Allocate and fill in one message for every user that is getting
	   events. */
	rcu_read_lock();
	list_for_each_entry_rcu(user, &intf->users, link) {
		if (! user->gets_events)
			continue;

		recv_msg = ipmi_alloc_recv_msg();
		if (! recv_msg) {
			rcu_read_unlock();
			list_for_each_entry_safe(recv_msg, recv_msg2, &msgs, link) {
				list_del(&recv_msg->link);
				ipmi_free_recv_msg(recv_msg);
			}
			/* We couldn't allocate memory for the
                           message, so requeue it for handling
                           later. */
			rv = 1;
			goto out;
		}

		deliver_count++;

		copy_event_into_recv_msg(recv_msg, msg);
		recv_msg->user = user;
		kref_get(&user->refcount);
		list_add_tail(&(recv_msg->link), &msgs);
	}
	rcu_read_unlock();

	if (deliver_count) {
		/* Now deliver all the messages. */
		list_for_each_entry_safe(recv_msg, recv_msg2, &msgs, link) {
			list_del(&recv_msg->link);
			deliver_response(recv_msg);
		}
	} else if (intf->waiting_events_count < MAX_EVENTS_IN_QUEUE) {
		/* No one to receive the message, put it in queue if there's
		   not already too many things in the queue. */
		recv_msg = ipmi_alloc_recv_msg();
		if (! recv_msg) {
			/* We couldn't allocate memory for the
                           message, so requeue it for handling
                           later. */
			rv = 1;
			goto out;
		}

		copy_event_into_recv_msg(recv_msg, msg);
		list_add_tail(&(recv_msg->link), &(intf->waiting_events));
	} else {
		/* There's too many things in the queue, discard this
		   message. */
		printk(KERN_WARNING PFX "Event queue full, discarding an"
		       " incoming event\n");
	}

 out:
	spin_unlock_irqrestore(&(intf->events_lock), flags);

	return rv;
}

static int handle_bmc_rsp(ipmi_smi_t          intf,
			  struct ipmi_smi_msg *msg)
{
	struct ipmi_recv_msg *recv_msg;
	unsigned long        flags;
	struct ipmi_user     *user;

	recv_msg = (struct ipmi_recv_msg *) msg->user_data;
	if (recv_msg == NULL)
	{
		printk(KERN_WARNING"IPMI message received with no owner. This\n"
			"could be because of a malformed message, or\n"
			"because of a hardware error.  Contact your\n"
			"hardware vender for assistance\n");
		return 0;
	}

	user = recv_msg->user;
	/* Make sure the user still exists. */
	if (user && !user->valid) {
		/* The user for the message went away, so give up. */
		spin_lock_irqsave(&intf->counter_lock, flags);
		intf->unhandled_local_responses++;
		spin_unlock_irqrestore(&intf->counter_lock, flags);
		ipmi_free_recv_msg(recv_msg);
	} else {
		struct ipmi_system_interface_addr *smi_addr;

		spin_lock_irqsave(&intf->counter_lock, flags);
		intf->handled_local_responses++;
		spin_unlock_irqrestore(&intf->counter_lock, flags);
		recv_msg->recv_type = IPMI_RESPONSE_RECV_TYPE;
		recv_msg->msgid = msg->msgid;
		smi_addr = ((struct ipmi_system_interface_addr *)
			    &(recv_msg->addr));
		smi_addr->addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
		smi_addr->channel = IPMI_BMC_CHANNEL;
		smi_addr->lun = msg->rsp[0] & 3;
		recv_msg->msg.netfn = msg->rsp[0] >> 2;
		recv_msg->msg.cmd = msg->rsp[1];
		memcpy(recv_msg->msg_data,
		       &(msg->rsp[2]),
		       msg->rsp_size - 2);
		recv_msg->msg.data = recv_msg->msg_data;
		recv_msg->msg.data_len = msg->rsp_size - 2;
		deliver_response(recv_msg);
	}

	return 0;
}

/* Handle a new message.  Return 1 if the message should be requeued,
   0 if the message should be freed, or -1 if the message should not
   be freed or requeued. */
static int handle_new_recv_msg(ipmi_smi_t          intf,
			       struct ipmi_smi_msg *msg)
{
	int requeue;
	int chan;

#ifdef DEBUG_MSGING
	int m;
	printk("Recv:");
	for (m = 0; m < msg->rsp_size; m++)
		printk(" %2.2x", msg->rsp[m]);
	printk("\n");
#endif
	if (msg->rsp_size < 2) {
		/* Message is too small to be correct. */
		printk(KERN_WARNING PFX "BMC returned to small a message"
		       " for netfn %x cmd %x, got %d bytes\n",
		       (msg->data[0] >> 2) | 1, msg->data[1], msg->rsp_size);

		/* Generate an error response for the message. */
		msg->rsp[0] = msg->data[0] | (1 << 2);
		msg->rsp[1] = msg->data[1];
		msg->rsp[2] = IPMI_ERR_UNSPECIFIED;
		msg->rsp_size = 3;
	} else if (((msg->rsp[0] >> 2) != ((msg->data[0] >> 2) | 1))/* Netfn */
		   || (msg->rsp[1] != msg->data[1]))		  /* Command */
	{
		/* The response is not even marginally correct. */
		printk(KERN_WARNING PFX "BMC returned incorrect response,"
		       " expected netfn %x cmd %x, got netfn %x cmd %x\n",
		       (msg->data[0] >> 2) | 1, msg->data[1],
		       msg->rsp[0] >> 2, msg->rsp[1]);

		/* Generate an error response for the message. */
		msg->rsp[0] = msg->data[0] | (1 << 2);
		msg->rsp[1] = msg->data[1];
		msg->rsp[2] = IPMI_ERR_UNSPECIFIED;
		msg->rsp_size = 3;
	}

	if ((msg->rsp[0] == ((IPMI_NETFN_APP_REQUEST|1) << 2))
	    && (msg->rsp[1] == IPMI_SEND_MSG_CMD)
	    && (msg->user_data != NULL))
	{
		/* It's a response to a response we sent.  For this we
		   deliver a send message response to the user. */
		struct ipmi_recv_msg     *recv_msg = msg->user_data;

		requeue = 0;
		if (msg->rsp_size < 2)
			/* Message is too small to be correct. */
			goto out;

		chan = msg->data[2] & 0x0f;
		if (chan >= IPMI_MAX_CHANNELS)
			/* Invalid channel number */
			goto out;

		if (!recv_msg)
			goto out;

		/* Make sure the user still exists. */
		if (!recv_msg->user || !recv_msg->user->valid)
			goto out;

		recv_msg->recv_type = IPMI_RESPONSE_RESPONSE_TYPE;
		recv_msg->msg.data = recv_msg->msg_data;
		recv_msg->msg.data_len = 1;
		recv_msg->msg_data[0] = msg->rsp[2];
		deliver_response(recv_msg);
	} else if ((msg->rsp[0] == ((IPMI_NETFN_APP_REQUEST|1) << 2))
		   && (msg->rsp[1] == IPMI_GET_MSG_CMD))
	{
		/* It's from the receive queue. */
		chan = msg->rsp[3] & 0xf;
		if (chan >= IPMI_MAX_CHANNELS) {
			/* Invalid channel number */
			requeue = 0;
			goto out;
		}

		switch (intf->channels[chan].medium) {
		case IPMI_CHANNEL_MEDIUM_IPMB:
			if (msg->rsp[4] & 0x04) {
				/* It's a response, so find the
				   requesting message and send it up. */
				requeue = handle_ipmb_get_msg_rsp(intf, msg);
			} else {
				/* It's a command to the SMS from some other
				   entity.  Handle that. */
				requeue = handle_ipmb_get_msg_cmd(intf, msg);
			}
			break;

		case IPMI_CHANNEL_MEDIUM_8023LAN:
		case IPMI_CHANNEL_MEDIUM_ASYNC:
			if (msg->rsp[6] & 0x04) {
				/* It's a response, so find the
				   requesting message and send it up. */
				requeue = handle_lan_get_msg_rsp(intf, msg);
			} else {
				/* It's a command to the SMS from some other
				   entity.  Handle that. */
				requeue = handle_lan_get_msg_cmd(intf, msg);
			}
			break;

		default:
			/* We don't handle the channel type, so just
			 * free the message. */
			requeue = 0;
		}

	} else if ((msg->rsp[0] == ((IPMI_NETFN_APP_REQUEST|1) << 2))
		   && (msg->rsp[1] == IPMI_READ_EVENT_MSG_BUFFER_CMD))
	{
		/* It's an asyncronous event. */
		requeue = handle_read_event_rsp(intf, msg);
	} else {
		/* It's a response from the local BMC. */
		requeue = handle_bmc_rsp(intf, msg);
	}

 out:
	return requeue;
}

/* Handle a new message from the lower layer. */
void ipmi_smi_msg_received(ipmi_smi_t          intf,
			   struct ipmi_smi_msg *msg)
{
	unsigned long flags;
	int           rv;


	if ((msg->data_size >= 2)
	    && (msg->data[0] == (IPMI_NETFN_APP_REQUEST << 2))
	    && (msg->data[1] == IPMI_SEND_MSG_CMD)
	    && (msg->user_data == NULL))
	{
		/* This is the local response to a command send, start
                   the timer for these.  The user_data will not be
                   NULL if this is a response send, and we will let
                   response sends just go through. */

		/* Check for errors, if we get certain errors (ones
                   that mean basically we can try again later), we
                   ignore them and start the timer.  Otherwise we
                   report the error immediately. */
		if ((msg->rsp_size >= 3) && (msg->rsp[2] != 0)
		    && (msg->rsp[2] != IPMI_NODE_BUSY_ERR)
		    && (msg->rsp[2] != IPMI_LOST_ARBITRATION_ERR))
		{
			int chan = msg->rsp[3] & 0xf;

			/* Got an error sending the message, handle it. */
			spin_lock_irqsave(&intf->counter_lock, flags);
			if (chan >= IPMI_MAX_CHANNELS)
				; /* This shouldn't happen */
			else if ((intf->channels[chan].medium
				  == IPMI_CHANNEL_MEDIUM_8023LAN)
				 || (intf->channels[chan].medium
				     == IPMI_CHANNEL_MEDIUM_ASYNC))
				intf->sent_lan_command_errs++;
			else
				intf->sent_ipmb_command_errs++;
			spin_unlock_irqrestore(&intf->counter_lock, flags);
			intf_err_seq(intf, msg->msgid, msg->rsp[2]);
		} else {
			/* The message was sent, start the timer. */
			intf_start_seq_timer(intf, msg->msgid);
		}

		ipmi_free_smi_msg(msg);
		goto out;
	}

	/* To preserve message order, if the list is not empty, we
           tack this message onto the end of the list. */
	spin_lock_irqsave(&intf->waiting_msgs_lock, flags);
	if (!list_empty(&intf->waiting_msgs)) {
		list_add_tail(&msg->link, &intf->waiting_msgs);
		spin_unlock_irqrestore(&intf->waiting_msgs_lock, flags);
		goto out;
	}
	spin_unlock_irqrestore(&intf->waiting_msgs_lock, flags);
		
	rv = handle_new_recv_msg(intf, msg);
	if (rv > 0) {
		/* Could not handle the message now, just add it to a
                   list to handle later. */
		spin_lock_irqsave(&intf->waiting_msgs_lock, flags);
		list_add_tail(&msg->link, &intf->waiting_msgs);
		spin_unlock_irqrestore(&intf->waiting_msgs_lock, flags);
	} else if (rv == 0) {
		ipmi_free_smi_msg(msg);
	}

 out:
	return;
}

void ipmi_smi_watchdog_pretimeout(ipmi_smi_t intf)
{
	ipmi_user_t user;

	rcu_read_lock();
	list_for_each_entry_rcu(user, &intf->users, link) {
		if (! user->handler->ipmi_watchdog_pretimeout)
			continue;

		user->handler->ipmi_watchdog_pretimeout(user->handler_data);
	}
	rcu_read_unlock();
}

static void
handle_msg_timeout(struct ipmi_recv_msg *msg)
{
	msg->recv_type = IPMI_RESPONSE_RECV_TYPE;
	msg->msg_data[0] = IPMI_TIMEOUT_COMPLETION_CODE;
	msg->msg.netfn |= 1; /* Convert to a response. */
	msg->msg.data_len = 1;
	msg->msg.data = msg->msg_data;
	deliver_response(msg);
}

static struct ipmi_smi_msg *
smi_from_recv_msg(ipmi_smi_t intf, struct ipmi_recv_msg *recv_msg,
		  unsigned char seq, long seqid)
{
	struct ipmi_smi_msg *smi_msg = ipmi_alloc_smi_msg();
	if (!smi_msg)
		/* If we can't allocate the message, then just return, we
		   get 4 retries, so this should be ok. */
		return NULL;

	memcpy(smi_msg->data, recv_msg->msg.data, recv_msg->msg.data_len);
	smi_msg->data_size = recv_msg->msg.data_len;
	smi_msg->msgid = STORE_SEQ_IN_MSGID(seq, seqid);
		
#ifdef DEBUG_MSGING
	{
		int m;
		printk("Resend: ");
		for (m = 0; m < smi_msg->data_size; m++)
			printk(" %2.2x", smi_msg->data[m]);
		printk("\n");
	}
#endif
	return smi_msg;
}

static void check_msg_timeout(ipmi_smi_t intf, struct seq_table *ent,
			      struct list_head *timeouts, long timeout_period,
			      int slot, unsigned long *flags)
{
	struct ipmi_recv_msg *msg;

	if (!ent->inuse)
		return;

	ent->timeout -= timeout_period;
	if (ent->timeout > 0)
		return;

	if (ent->retries_left == 0) {
		/* The message has used all its retries. */
		ent->inuse = 0;
		msg = ent->recv_msg;
		list_add_tail(&msg->link, timeouts);
		spin_lock(&intf->counter_lock);
		if (ent->broadcast)
			intf->timed_out_ipmb_broadcasts++;
		else if (ent->recv_msg->addr.addr_type == IPMI_LAN_ADDR_TYPE)
			intf->timed_out_lan_commands++;
		else
			intf->timed_out_ipmb_commands++;
		spin_unlock(&intf->counter_lock);
	} else {
		struct ipmi_smi_msg *smi_msg;
		/* More retries, send again. */

		/* Start with the max timer, set to normal
		   timer after the message is sent. */
		ent->timeout = MAX_MSG_TIMEOUT;
		ent->retries_left--;
		spin_lock(&intf->counter_lock);
		if (ent->recv_msg->addr.addr_type == IPMI_LAN_ADDR_TYPE)
			intf->retransmitted_lan_commands++;
		else
			intf->retransmitted_ipmb_commands++;
		spin_unlock(&intf->counter_lock);

		smi_msg = smi_from_recv_msg(intf, ent->recv_msg, slot,
					    ent->seqid);
		if (! smi_msg)
			return;

		spin_unlock_irqrestore(&intf->seq_lock, *flags);
		/* Send the new message.  We send with a zero
		 * priority.  It timed out, I doubt time is
		 * that critical now, and high priority
		 * messages are really only for messages to the
		 * local MC, which don't get resent. */
		intf->handlers->sender(intf->send_info,
				       smi_msg, 0);
		spin_lock_irqsave(&intf->seq_lock, *flags);
	}
}

static void ipmi_timeout_handler(long timeout_period)
{
	ipmi_smi_t           intf;
	struct list_head     timeouts;
	struct ipmi_recv_msg *msg, *msg2;
	struct ipmi_smi_msg  *smi_msg, *smi_msg2;
	unsigned long        flags;
	int                  i, j;

	INIT_LIST_HEAD(&timeouts);

	spin_lock(&interfaces_lock);
	for (i = 0; i < MAX_IPMI_INTERFACES; i++) {
		intf = ipmi_interfaces[i];
		if (IPMI_INVALID_INTERFACE(intf))
			continue;
		kref_get(&intf->refcount);
		spin_unlock(&interfaces_lock);

		/* See if any waiting messages need to be processed. */
		spin_lock_irqsave(&intf->waiting_msgs_lock, flags);
		list_for_each_entry_safe(smi_msg, smi_msg2, &intf->waiting_msgs, link) {
			if (! handle_new_recv_msg(intf, smi_msg)) {
				list_del(&smi_msg->link);
				ipmi_free_smi_msg(smi_msg);
			} else {
				/* To preserve message order, quit if we
				   can't handle a message. */
				break;
			}
		}
		spin_unlock_irqrestore(&intf->waiting_msgs_lock, flags);

		/* Go through the seq table and find any messages that
		   have timed out, putting them in the timeouts
		   list. */
		spin_lock_irqsave(&intf->seq_lock, flags);
		for (j = 0; j < IPMI_IPMB_NUM_SEQ; j++)
			check_msg_timeout(intf, &(intf->seq_table[j]),
					  &timeouts, timeout_period, j,
					  &flags);
		spin_unlock_irqrestore(&intf->seq_lock, flags);

		list_for_each_entry_safe(msg, msg2, &timeouts, link)
			handle_msg_timeout(msg);

		kref_put(&intf->refcount, intf_free);
		spin_lock(&interfaces_lock);
	}
	spin_unlock(&interfaces_lock);
}

static void ipmi_request_event(void)
{
	ipmi_smi_t intf;
	int        i;

	spin_lock(&interfaces_lock);
	for (i = 0; i < MAX_IPMI_INTERFACES; i++) {
		intf = ipmi_interfaces[i];
		if (IPMI_INVALID_INTERFACE(intf))
			continue;

		intf->handlers->request_events(intf->send_info);
	}
	spin_unlock(&interfaces_lock);
}

static struct timer_list ipmi_timer;

/* Call every ~100 ms. */
#define IPMI_TIMEOUT_TIME	100

/* How many jiffies does it take to get to the timeout time. */
#define IPMI_TIMEOUT_JIFFIES	((IPMI_TIMEOUT_TIME * HZ) / 1000)

/* Request events from the queue every second (this is the number of
   IPMI_TIMEOUT_TIMES between event requests).  Hopefully, in the
   future, IPMI will add a way to know immediately if an event is in
   the queue and this silliness can go away. */
#define IPMI_REQUEST_EV_TIME	(1000 / (IPMI_TIMEOUT_TIME))

static atomic_t stop_operation;
static unsigned int ticks_to_req_ev = IPMI_REQUEST_EV_TIME;

static void ipmi_timeout(unsigned long data)
{
	if (atomic_read(&stop_operation))
		return;

	ticks_to_req_ev--;
	if (ticks_to_req_ev == 0) {
		ipmi_request_event();
		ticks_to_req_ev = IPMI_REQUEST_EV_TIME;
	}

	ipmi_timeout_handler(IPMI_TIMEOUT_TIME);

	mod_timer(&ipmi_timer, jiffies + IPMI_TIMEOUT_JIFFIES);
}


static atomic_t smi_msg_inuse_count = ATOMIC_INIT(0);
static atomic_t recv_msg_inuse_count = ATOMIC_INIT(0);

/* FIXME - convert these to slabs. */
static void free_smi_msg(struct ipmi_smi_msg *msg)
{
	atomic_dec(&smi_msg_inuse_count);
	kfree(msg);
}

struct ipmi_smi_msg *ipmi_alloc_smi_msg(void)
{
	struct ipmi_smi_msg *rv;
	rv = kmalloc(sizeof(struct ipmi_smi_msg), GFP_ATOMIC);
	if (rv) {
		rv->done = free_smi_msg;
		rv->user_data = NULL;
		atomic_inc(&smi_msg_inuse_count);
	}
	return rv;
}

static void free_recv_msg(struct ipmi_recv_msg *msg)
{
	atomic_dec(&recv_msg_inuse_count);
	kfree(msg);
}

struct ipmi_recv_msg *ipmi_alloc_recv_msg(void)
{
	struct ipmi_recv_msg *rv;

	rv = kmalloc(sizeof(struct ipmi_recv_msg), GFP_ATOMIC);
	if (rv) {
		rv->done = free_recv_msg;
		atomic_inc(&recv_msg_inuse_count);
	}
	return rv;
}

void ipmi_free_recv_msg(struct ipmi_recv_msg *msg)
{
	if (msg->user)
		kref_put(&msg->user->refcount, free_user);
	msg->done(msg);
}

#ifdef CONFIG_IPMI_PANIC_EVENT

static void dummy_smi_done_handler(struct ipmi_smi_msg *msg)
{
}

static void dummy_recv_done_handler(struct ipmi_recv_msg *msg)
{
}

#ifdef CONFIG_IPMI_PANIC_STRING
static void event_receiver_fetcher(ipmi_smi_t intf, struct ipmi_recv_msg *msg)
{
	if ((msg->addr.addr_type == IPMI_SYSTEM_INTERFACE_ADDR_TYPE)
	    && (msg->msg.netfn == IPMI_NETFN_SENSOR_EVENT_RESPONSE)
	    && (msg->msg.cmd == IPMI_GET_EVENT_RECEIVER_CMD)
	    && (msg->msg.data[0] == IPMI_CC_NO_ERROR))
	{
		/* A get event receiver command, save it. */
		intf->event_receiver = msg->msg.data[1];
		intf->event_receiver_lun = msg->msg.data[2] & 0x3;
	}
}

static void device_id_fetcher(ipmi_smi_t intf, struct ipmi_recv_msg *msg)
{
	if ((msg->addr.addr_type == IPMI_SYSTEM_INTERFACE_ADDR_TYPE)
	    && (msg->msg.netfn == IPMI_NETFN_APP_RESPONSE)
	    && (msg->msg.cmd == IPMI_GET_DEVICE_ID_CMD)
	    && (msg->msg.data[0] == IPMI_CC_NO_ERROR))
	{
		/* A get device id command, save if we are an event
		   receiver or generator. */
		intf->local_sel_device = (msg->msg.data[6] >> 2) & 1;
		intf->local_event_generator = (msg->msg.data[6] >> 5) & 1;
	}
}
#endif

static void send_panic_events(char *str)
{
	struct kernel_ipmi_msg            msg;
	ipmi_smi_t                        intf;
	unsigned char                     data[16];
	int                               i;
	struct ipmi_system_interface_addr *si;
	struct ipmi_addr                  addr;
	struct ipmi_smi_msg               smi_msg;
	struct ipmi_recv_msg              recv_msg;

	si = (struct ipmi_system_interface_addr *) &addr;
	si->addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	si->channel = IPMI_BMC_CHANNEL;
	si->lun = 0;

	/* Fill in an event telling that we have failed. */
	msg.netfn = 0x04; /* Sensor or Event. */
	msg.cmd = 2; /* Platform event command. */
	msg.data = data;
	msg.data_len = 8;
	data[0] = 0x21; /* Kernel generator ID, IPMI table 5-4 */
	data[1] = 0x03; /* This is for IPMI 1.0. */
	data[2] = 0x20; /* OS Critical Stop, IPMI table 36-3 */
	data[4] = 0x6f; /* Sensor specific, IPMI table 36-1 */
	data[5] = 0xa1; /* Runtime stop OEM bytes 2 & 3. */

	/* Put a few breadcrumbs in.  Hopefully later we can add more things
	   to make the panic events more useful. */
	if (str) {
		data[3] = str[0];
		data[6] = str[1];
		data[7] = str[2];
	}

	smi_msg.done = dummy_smi_done_handler;
	recv_msg.done = dummy_recv_done_handler;

	/* For every registered interface, send the event. */
	for (i = 0; i < MAX_IPMI_INTERFACES; i++) {
		intf = ipmi_interfaces[i];
		if (IPMI_INVALID_INTERFACE(intf))
			continue;

		/* Send the event announcing the panic. */
		intf->handlers->set_run_to_completion(intf->send_info, 1);
		i_ipmi_request(NULL,
			       intf,
			       &addr,
			       0,
			       &msg,
			       intf,
			       &smi_msg,
			       &recv_msg,
			       0,
			       intf->channels[0].address,
			       intf->channels[0].lun,
			       0, 1); /* Don't retry, and don't wait. */
	}

#ifdef CONFIG_IPMI_PANIC_STRING
	/* On every interface, dump a bunch of OEM event holding the
	   string. */
	if (!str) 
		return;

	for (i = 0; i < MAX_IPMI_INTERFACES; i++) {
		char                  *p = str;
		struct ipmi_ipmb_addr *ipmb;
		int                   j;

		intf = ipmi_interfaces[i];
		if (IPMI_INVALID_INTERFACE(intf))
			continue;

		/* First job here is to figure out where to send the
		   OEM events.  There's no way in IPMI to send OEM
		   events using an event send command, so we have to
		   find the SEL to put them in and stick them in
		   there. */

		/* Get capabilities from the get device id. */
		intf->local_sel_device = 0;
		intf->local_event_generator = 0;
		intf->event_receiver = 0;

		/* Request the device info from the local MC. */
		msg.netfn = IPMI_NETFN_APP_REQUEST;
		msg.cmd = IPMI_GET_DEVICE_ID_CMD;
		msg.data = NULL;
		msg.data_len = 0;
		intf->null_user_handler = device_id_fetcher;
		i_ipmi_request(NULL,
			       intf,
			       &addr,
			       0,
			       &msg,
			       intf,
			       &smi_msg,
			       &recv_msg,
			       0,
			       intf->channels[0].address,
			       intf->channels[0].lun,
			       0, 1); /* Don't retry, and don't wait. */

		if (intf->local_event_generator) {
			/* Request the event receiver from the local MC. */
			msg.netfn = IPMI_NETFN_SENSOR_EVENT_REQUEST;
			msg.cmd = IPMI_GET_EVENT_RECEIVER_CMD;
			msg.data = NULL;
			msg.data_len = 0;
			intf->null_user_handler = event_receiver_fetcher;
			i_ipmi_request(NULL,
				       intf,
				       &addr,
				       0,
				       &msg,
				       intf,
				       &smi_msg,
				       &recv_msg,
				       0,
				       intf->channels[0].address,
				       intf->channels[0].lun,
				       0, 1); /* no retry, and no wait. */
		}
		intf->null_user_handler = NULL;

		/* Validate the event receiver.  The low bit must not
		   be 1 (it must be a valid IPMB address), it cannot
		   be zero, and it must not be my address. */
                if (((intf->event_receiver & 1) == 0)
		    && (intf->event_receiver != 0)
		    && (intf->event_receiver != intf->channels[0].address))
		{
			/* The event receiver is valid, send an IPMB
			   message. */
			ipmb = (struct ipmi_ipmb_addr *) &addr;
			ipmb->addr_type = IPMI_IPMB_ADDR_TYPE;
			ipmb->channel = 0; /* FIXME - is this right? */
			ipmb->lun = intf->event_receiver_lun;
			ipmb->slave_addr = intf->event_receiver;
		} else if (intf->local_sel_device) {
			/* The event receiver was not valid (or was
			   me), but I am an SEL device, just dump it
			   in my SEL. */
			si = (struct ipmi_system_interface_addr *) &addr;
			si->addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
			si->channel = IPMI_BMC_CHANNEL;
			si->lun = 0;
		} else
			continue; /* No where to send the event. */

		
		msg.netfn = IPMI_NETFN_STORAGE_REQUEST; /* Storage. */
		msg.cmd = IPMI_ADD_SEL_ENTRY_CMD;
		msg.data = data;
		msg.data_len = 16;

		j = 0;
		while (*p) {
			int size = strlen(p);

			if (size > 11)
				size = 11;
			data[0] = 0;
			data[1] = 0;
			data[2] = 0xf0; /* OEM event without timestamp. */
			data[3] = intf->channels[0].address;
			data[4] = j++; /* sequence # */
			/* Always give 11 bytes, so strncpy will fill
			   it with zeroes for me. */
			strncpy(data+5, p, 11);
			p += size;

			i_ipmi_request(NULL,
				       intf,
				       &addr,
				       0,
				       &msg,
				       intf,
				       &smi_msg,
				       &recv_msg,
				       0,
				       intf->channels[0].address,
				       intf->channels[0].lun,
				       0, 1); /* no retry, and no wait. */
		}
	}	
#endif /* CONFIG_IPMI_PANIC_STRING */
}
#endif /* CONFIG_IPMI_PANIC_EVENT */

static int has_paniced = 0;

static int panic_event(struct notifier_block *this,
		       unsigned long         event,
                       void                  *ptr)
{
	int        i;
	ipmi_smi_t intf;

	if (has_paniced)
		return NOTIFY_DONE;
	has_paniced = 1;

	/* For every registered interface, set it to run to completion. */
	for (i = 0; i < MAX_IPMI_INTERFACES; i++) {
		intf = ipmi_interfaces[i];
		if (IPMI_INVALID_INTERFACE(intf))
			continue;

		intf->handlers->set_run_to_completion(intf->send_info, 1);
	}

#ifdef CONFIG_IPMI_PANIC_EVENT
	send_panic_events(ptr);
#endif

	return NOTIFY_DONE;
}

static struct notifier_block panic_block = {
	.notifier_call	= panic_event,
	.next		= NULL,
	.priority	= 200	/* priority: INT_MAX >= x >= 0 */
};

static int ipmi_init_msghandler(void)
{
	int i;

	if (initialized)
		return 0;

	printk(KERN_INFO "ipmi message handler version "
	       IPMI_DRIVER_VERSION "\n");

	for (i = 0; i < MAX_IPMI_INTERFACES; i++)
		ipmi_interfaces[i] = NULL;

#ifdef CONFIG_PROC_FS
	proc_ipmi_root = proc_mkdir("ipmi", NULL);
	if (!proc_ipmi_root) {
	    printk(KERN_ERR PFX "Unable to create IPMI proc dir");
	    return -ENOMEM;
	}

	proc_ipmi_root->owner = THIS_MODULE;
#endif /* CONFIG_PROC_FS */

	init_timer(&ipmi_timer);
	ipmi_timer.data = 0;
	ipmi_timer.function = ipmi_timeout;
	ipmi_timer.expires = jiffies + IPMI_TIMEOUT_JIFFIES;
	add_timer(&ipmi_timer);

	notifier_chain_register(&panic_notifier_list, &panic_block);

	initialized = 1;

	return 0;
}

static __init int ipmi_init_msghandler_mod(void)
{
	ipmi_init_msghandler();
	return 0;
}

static __exit void cleanup_ipmi(void)
{
	int count;

	if (!initialized)
		return;

	notifier_chain_unregister(&panic_notifier_list, &panic_block);

	/* This can't be called if any interfaces exist, so no worry about
	   shutting down the interfaces. */

	/* Tell the timer to stop, then wait for it to stop.  This avoids
	   problems with race conditions removing the timer here. */
	atomic_inc(&stop_operation);
	del_timer_sync(&ipmi_timer);

#ifdef CONFIG_PROC_FS
	remove_proc_entry(proc_ipmi_root->name, &proc_root);
#endif /* CONFIG_PROC_FS */

	initialized = 0;

	/* Check for buffer leaks. */
	count = atomic_read(&smi_msg_inuse_count);
	if (count != 0)
		printk(KERN_WARNING PFX "SMI message count %d at exit\n",
		       count);
	count = atomic_read(&recv_msg_inuse_count);
	if (count != 0)
		printk(KERN_WARNING PFX "recv message count %d at exit\n",
		       count);
}
module_exit(cleanup_ipmi);

module_init(ipmi_init_msghandler_mod);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Corey Minyard <minyard@mvista.com>");
MODULE_DESCRIPTION("Incoming and outgoing message routing for an IPMI interface.");
MODULE_VERSION(IPMI_DRIVER_VERSION);

EXPORT_SYMBOL(ipmi_create_user);
EXPORT_SYMBOL(ipmi_destroy_user);
EXPORT_SYMBOL(ipmi_get_version);
EXPORT_SYMBOL(ipmi_request_settime);
EXPORT_SYMBOL(ipmi_request_supply_msgs);
EXPORT_SYMBOL(ipmi_register_smi);
EXPORT_SYMBOL(ipmi_unregister_smi);
EXPORT_SYMBOL(ipmi_register_for_cmd);
EXPORT_SYMBOL(ipmi_unregister_for_cmd);
EXPORT_SYMBOL(ipmi_smi_msg_received);
EXPORT_SYMBOL(ipmi_smi_watchdog_pretimeout);
EXPORT_SYMBOL(ipmi_alloc_smi_msg);
EXPORT_SYMBOL(ipmi_addr_length);
EXPORT_SYMBOL(ipmi_validate_addr);
EXPORT_SYMBOL(ipmi_set_gets_events);
EXPORT_SYMBOL(ipmi_smi_watcher_register);
EXPORT_SYMBOL(ipmi_smi_watcher_unregister);
EXPORT_SYMBOL(ipmi_set_my_address);
EXPORT_SYMBOL(ipmi_get_my_address);
EXPORT_SYMBOL(ipmi_set_my_LUN);
EXPORT_SYMBOL(ipmi_get_my_LUN);
EXPORT_SYMBOL(ipmi_smi_add_proc_entry);
EXPORT_SYMBOL(proc_ipmi_root);
EXPORT_SYMBOL(ipmi_user_set_run_to_completion);
EXPORT_SYMBOL(ipmi_free_recv_msg);
