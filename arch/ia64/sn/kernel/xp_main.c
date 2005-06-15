/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2004-2005 Silicon Graphics, Inc.  All Rights Reserved.
 */


/*
 * Cross Partition (XP) base.
 *
 *	XP provides a base from which its users can interact
 *	with XPC, yet not be dependent on XPC.
 *
 */


#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <asm/sn/intr.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/xp.h>


/*
 * Target of nofault PIO read.
 */
u64 xp_nofault_PIOR_target;


/*
 * xpc_registrations[] keeps track of xpc_connect()'s done by the kernel-level
 * users of XPC.
 */
struct xpc_registration xpc_registrations[XPC_NCHANNELS];


/*
 * Initialize the XPC interface to indicate that XPC isn't loaded.
 */
static enum xpc_retval xpc_notloaded(void) { return xpcNotLoaded; }

struct xpc_interface xpc_interface = {
	(void (*)(int)) xpc_notloaded,
	(void (*)(int)) xpc_notloaded,
	(enum xpc_retval (*)(partid_t, int, u32, void **)) xpc_notloaded,
	(enum xpc_retval (*)(partid_t, int, void *)) xpc_notloaded,
	(enum xpc_retval (*)(partid_t, int, void *, xpc_notify_func, void *))
							xpc_notloaded,
	(void (*)(partid_t, int, void *)) xpc_notloaded,
	(enum xpc_retval (*)(partid_t, void *)) xpc_notloaded
};


/*
 * XPC calls this when it (the XPC module) has been loaded.
 */
void
xpc_set_interface(void (*connect)(int),
		void (*disconnect)(int),
		enum xpc_retval (*allocate)(partid_t, int, u32, void **),
		enum xpc_retval (*send)(partid_t, int, void *),
		enum xpc_retval (*send_notify)(partid_t, int, void *,
						xpc_notify_func, void *),
		void (*received)(partid_t, int, void *),
		enum xpc_retval (*partid_to_nasids)(partid_t, void *))
{
	xpc_interface.connect = connect;
	xpc_interface.disconnect = disconnect;
	xpc_interface.allocate = allocate;
	xpc_interface.send = send;
	xpc_interface.send_notify = send_notify;
	xpc_interface.received = received;
	xpc_interface.partid_to_nasids = partid_to_nasids;
}


/*
 * XPC calls this when it (the XPC module) is being unloaded.
 */
void
xpc_clear_interface(void)
{
	xpc_interface.connect = (void (*)(int)) xpc_notloaded;
	xpc_interface.disconnect = (void (*)(int)) xpc_notloaded;
	xpc_interface.allocate = (enum xpc_retval (*)(partid_t, int, u32,
					void **)) xpc_notloaded;
	xpc_interface.send = (enum xpc_retval (*)(partid_t, int, void *))
					xpc_notloaded;
	xpc_interface.send_notify = (enum xpc_retval (*)(partid_t, int, void *,
				    xpc_notify_func, void *)) xpc_notloaded;
	xpc_interface.received = (void (*)(partid_t, int, void *))
					xpc_notloaded;
	xpc_interface.partid_to_nasids = (enum xpc_retval (*)(partid_t, void *))
					xpc_notloaded;
}


/*
 * Register for automatic establishment of a channel connection whenever
 * a partition comes up.
 *
 * Arguments:
 *
 *	ch_number - channel # to register for connection.
 *	func - function to call for asynchronous notification of channel
 *	       state changes (i.e., connection, disconnection, error) and
 *	       the arrival of incoming messages.
 *      key - pointer to optional user-defined value that gets passed back
 *	      to the user on any callouts made to func.
 *	payload_size - size in bytes of the XPC message's payload area which
 *		       contains a user-defined message. The user should make
 *		       this large enough to hold their largest message.
 *	nentries - max #of XPC message entries a message queue can contain.
 *		   The actual number, which is determined when a connection
 * 		   is established and may be less then requested, will be
 *		   passed to the user via the xpcConnected callout.
 *	assigned_limit - max number of kthreads allowed to be processing
 * 			 messages (per connection) at any given instant.
 *	idle_limit - max number of kthreads allowed to be idle at any given
 * 		     instant.
 */
enum xpc_retval
xpc_connect(int ch_number, xpc_channel_func func, void *key, u16 payload_size,
		u16 nentries, u32 assigned_limit, u32 idle_limit)
{
	struct xpc_registration *registration;


	DBUG_ON(ch_number < 0 || ch_number >= XPC_NCHANNELS);
	DBUG_ON(payload_size == 0 || nentries == 0);
	DBUG_ON(func == NULL);
	DBUG_ON(assigned_limit == 0 || idle_limit > assigned_limit);

	registration = &xpc_registrations[ch_number];

	if (down_interruptible(&registration->sema) != 0) {
		return xpcInterrupted;
	}

	/* if XPC_CHANNEL_REGISTERED(ch_number) */
	if (registration->func != NULL) {
		up(&registration->sema);
		return xpcAlreadyRegistered;
	}

	/* register the channel for connection */
	registration->msg_size = XPC_MSG_SIZE(payload_size);
	registration->nentries = nentries;
	registration->assigned_limit = assigned_limit;
	registration->idle_limit = idle_limit;
	registration->key = key;
	registration->func = func;

	up(&registration->sema);

	xpc_interface.connect(ch_number);

	return xpcSuccess;
}


/*
 * Remove the registration for automatic connection of the specified channel
 * when a partition comes up.
 *
 * Before returning this xpc_disconnect() will wait for all connections on the
 * specified channel have been closed/torndown. So the caller can be assured
 * that they will not be receiving any more callouts from XPC to their
 * function registered via xpc_connect().
 *
 * Arguments:
 *
 *	ch_number - channel # to unregister.
 */
void
xpc_disconnect(int ch_number)
{
	struct xpc_registration *registration;


	DBUG_ON(ch_number < 0 || ch_number >= XPC_NCHANNELS);

	registration = &xpc_registrations[ch_number];

	/*
	 * We've decided not to make this a down_interruptible(), since we
	 * figured XPC's users will just turn around and call xpc_disconnect()
	 * again anyways, so we might as well wait, if need be.
	 */
	down(&registration->sema);

	/* if !XPC_CHANNEL_REGISTERED(ch_number) */
	if (registration->func == NULL) {
		up(&registration->sema);
		return;
	}

	/* remove the connection registration for the specified channel */
	registration->func = NULL;
	registration->key = NULL;
	registration->nentries = 0;
	registration->msg_size = 0;
	registration->assigned_limit = 0;
	registration->idle_limit = 0;

	xpc_interface.disconnect(ch_number);

	up(&registration->sema);

	return;
}


int __init
xp_init(void)
{
	int ret, ch_number;
	u64 func_addr = *(u64 *) xp_nofault_PIOR;
	u64 err_func_addr = *(u64 *) xp_error_PIOR;


	if (!ia64_platform_is("sn2")) {
		return -ENODEV;
	}

	/*
	 * Register a nofault code region which performs a cross-partition
	 * PIO read. If the PIO read times out, the MCA handler will consume
	 * the error and return to a kernel-provided instruction to indicate
	 * an error. This PIO read exists because it is guaranteed to timeout
	 * if the destination is down (AMO operations do not timeout on at
	 * least some CPUs on Shubs <= v1.2, which unfortunately we have to
	 * work around).
	 */
	if ((ret = sn_register_nofault_code(func_addr, err_func_addr,
						err_func_addr, 1, 1)) != 0) {
		printk(KERN_ERR "XP: can't register nofault code, error=%d\n",
			ret);
	}
	/*
	 * Setup the nofault PIO read target. (There is no special reason why
	 * SH_IPI_ACCESS was selected.)
	 */
	if (is_shub2()) {
		xp_nofault_PIOR_target = SH2_IPI_ACCESS0;
	} else {
		xp_nofault_PIOR_target = SH1_IPI_ACCESS;
	}

	/* initialize the connection registration semaphores */
	for (ch_number = 0; ch_number < XPC_NCHANNELS; ch_number++) {
		sema_init(&xpc_registrations[ch_number].sema, 1);  /* mutex */
	}

	return 0;
}
module_init(xp_init);


void __exit
xp_exit(void)
{
	u64 func_addr = *(u64 *) xp_nofault_PIOR;
	u64 err_func_addr = *(u64 *) xp_error_PIOR;


	/* unregister the PIO read nofault code region */
	(void) sn_register_nofault_code(func_addr, err_func_addr,
					err_func_addr, 1, 0);
}
module_exit(xp_exit);


MODULE_AUTHOR("Silicon Graphics, Inc.");
MODULE_DESCRIPTION("Cross Partition (XP) base");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(xp_nofault_PIOR);
EXPORT_SYMBOL(xp_nofault_PIOR_target);
EXPORT_SYMBOL(xpc_registrations);
EXPORT_SYMBOL(xpc_interface);
EXPORT_SYMBOL(xpc_clear_interface);
EXPORT_SYMBOL(xpc_set_interface);
EXPORT_SYMBOL(xpc_connect);
EXPORT_SYMBOL(xpc_disconnect);

