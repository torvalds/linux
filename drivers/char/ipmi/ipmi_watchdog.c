/*
 * ipmi_watchdog.c
 *
 * A watchdog timer based upon the IPMI interface.
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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/ipmi.h>
#include <linux/ipmi_smi.h>
#include <linux/mutex.h>
#include <linux/watchdog.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/kdebug.h>
#include <linux/rwsem.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/notifier.h>
#include <linux/nmi.h>
#include <linux/reboot.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/atomic.h>

#ifdef CONFIG_X86
/*
 * This is ugly, but I've determined that x86 is the only architecture
 * that can reasonably support the IPMI NMI watchdog timeout at this
 * time.  If another architecture adds this capability somehow, it
 * will have to be a somewhat different mechanism and I have no idea
 * how it will work.  So in the unlikely event that another
 * architecture supports this, we can figure out a good generic
 * mechanism for it at that time.
 */
#include <asm/kdebug.h>
#include <asm/nmi.h>
#define HAVE_DIE_NMI
#endif

#define	PFX "IPMI Watchdog: "

/*
 * The IPMI command/response information for the watchdog timer.
 */

/* values for byte 1 of the set command, byte 2 of the get response. */
#define WDOG_DONT_LOG		(1 << 7)
#define WDOG_DONT_STOP_ON_SET	(1 << 6)
#define WDOG_SET_TIMER_USE(byte, use) \
	byte = ((byte) & 0xf8) | ((use) & 0x7)
#define WDOG_GET_TIMER_USE(byte) ((byte) & 0x7)
#define WDOG_TIMER_USE_BIOS_FRB2	1
#define WDOG_TIMER_USE_BIOS_POST	2
#define WDOG_TIMER_USE_OS_LOAD		3
#define WDOG_TIMER_USE_SMS_OS		4
#define WDOG_TIMER_USE_OEM		5

/* values for byte 2 of the set command, byte 3 of the get response. */
#define WDOG_SET_PRETIMEOUT_ACT(byte, use) \
	byte = ((byte) & 0x8f) | (((use) & 0x7) << 4)
#define WDOG_GET_PRETIMEOUT_ACT(byte) (((byte) >> 4) & 0x7)
#define WDOG_PRETIMEOUT_NONE		0
#define WDOG_PRETIMEOUT_SMI		1
#define WDOG_PRETIMEOUT_NMI		2
#define WDOG_PRETIMEOUT_MSG_INT		3

/* Operations that can be performed on a pretimout. */
#define WDOG_PREOP_NONE		0
#define WDOG_PREOP_PANIC	1
/* Cause data to be available to read.  Doesn't work in NMI mode. */
#define WDOG_PREOP_GIVE_DATA	2

/* Actions to perform on a full timeout. */
#define WDOG_SET_TIMEOUT_ACT(byte, use) \
	byte = ((byte) & 0xf8) | ((use) & 0x7)
#define WDOG_GET_TIMEOUT_ACT(byte) ((byte) & 0x7)
#define WDOG_TIMEOUT_NONE		0
#define WDOG_TIMEOUT_RESET		1
#define WDOG_TIMEOUT_POWER_DOWN		2
#define WDOG_TIMEOUT_POWER_CYCLE	3

/*
 * Byte 3 of the get command, byte 4 of the get response is the
 * pre-timeout in seconds.
 */

/* Bits for setting byte 4 of the set command, byte 5 of the get response. */
#define WDOG_EXPIRE_CLEAR_BIOS_FRB2	(1 << 1)
#define WDOG_EXPIRE_CLEAR_BIOS_POST	(1 << 2)
#define WDOG_EXPIRE_CLEAR_OS_LOAD	(1 << 3)
#define WDOG_EXPIRE_CLEAR_SMS_OS	(1 << 4)
#define WDOG_EXPIRE_CLEAR_OEM		(1 << 5)

/*
 * Setting/getting the watchdog timer value.  This is for bytes 5 and
 * 6 (the timeout time) of the set command, and bytes 6 and 7 (the
 * timeout time) and 8 and 9 (the current countdown value) of the
 * response.  The timeout value is given in seconds (in the command it
 * is 100ms intervals).
 */
#define WDOG_SET_TIMEOUT(byte1, byte2, val) \
	(byte1) = (((val) * 10) & 0xff), (byte2) = (((val) * 10) >> 8)
#define WDOG_GET_TIMEOUT(byte1, byte2) \
	(((byte1) | ((byte2) << 8)) / 10)

#define IPMI_WDOG_RESET_TIMER		0x22
#define IPMI_WDOG_SET_TIMER		0x24
#define IPMI_WDOG_GET_TIMER		0x25

#define IPMI_WDOG_TIMER_NOT_INIT_RESP	0x80

static DEFINE_MUTEX(ipmi_watchdog_mutex);
static bool nowayout = WATCHDOG_NOWAYOUT;

static ipmi_user_t watchdog_user;
static int watchdog_ifnum;

/* Default the timeout to 10 seconds. */
static int timeout = 10;

/* The pre-timeout is disabled by default. */
static int pretimeout;

/* Default timeout to set on panic */
static int panic_wdt_timeout = 255;

/* Default action is to reset the board on a timeout. */
static unsigned char action_val = WDOG_TIMEOUT_RESET;

static char action[16] = "reset";

static unsigned char preaction_val = WDOG_PRETIMEOUT_NONE;

static char preaction[16] = "pre_none";

static unsigned char preop_val = WDOG_PREOP_NONE;

static char preop[16] = "preop_none";
static DEFINE_SPINLOCK(ipmi_read_lock);
static char data_to_read;
static DECLARE_WAIT_QUEUE_HEAD(read_q);
static struct fasync_struct *fasync_q;
static char pretimeout_since_last_heartbeat;
static char expect_close;

static int ifnum_to_use = -1;

/* Parameters to ipmi_set_timeout */
#define IPMI_SET_TIMEOUT_NO_HB			0
#define IPMI_SET_TIMEOUT_HB_IF_NECESSARY	1
#define IPMI_SET_TIMEOUT_FORCE_HB		2

static int ipmi_set_timeout(int do_heartbeat);
static void ipmi_register_watchdog(int ipmi_intf);
static void ipmi_unregister_watchdog(int ipmi_intf);

/*
 * If true, the driver will start running as soon as it is configured
 * and ready.
 */
static int start_now;

static int set_param_timeout(const char *val, const struct kernel_param *kp)
{
	char *endp;
	int  l;
	int  rv = 0;

	if (!val)
		return -EINVAL;
	l = simple_strtoul(val, &endp, 0);
	if (endp == val)
		return -EINVAL;

	*((int *)kp->arg) = l;
	if (watchdog_user)
		rv = ipmi_set_timeout(IPMI_SET_TIMEOUT_HB_IF_NECESSARY);

	return rv;
}

static const struct kernel_param_ops param_ops_timeout = {
	.set = set_param_timeout,
	.get = param_get_int,
};
#define param_check_timeout param_check_int

typedef int (*action_fn)(const char *intval, char *outval);

static int action_op(const char *inval, char *outval);
static int preaction_op(const char *inval, char *outval);
static int preop_op(const char *inval, char *outval);
static void check_parms(void);

static int set_param_str(const char *val, const struct kernel_param *kp)
{
	action_fn  fn = (action_fn) kp->arg;
	int        rv = 0;
	char       valcp[16];
	char       *s;

	strncpy(valcp, val, 16);
	valcp[15] = '\0';

	s = strstrip(valcp);

	rv = fn(s, NULL);
	if (rv)
		goto out;

	check_parms();
	if (watchdog_user)
		rv = ipmi_set_timeout(IPMI_SET_TIMEOUT_HB_IF_NECESSARY);

 out:
	return rv;
}

static int get_param_str(char *buffer, const struct kernel_param *kp)
{
	action_fn fn = (action_fn) kp->arg;
	int       rv;

	rv = fn(NULL, buffer);
	if (rv)
		return rv;
	return strlen(buffer);
}


static int set_param_wdog_ifnum(const char *val, const struct kernel_param *kp)
{
	int rv = param_set_int(val, kp);
	if (rv)
		return rv;
	if ((ifnum_to_use < 0) || (ifnum_to_use == watchdog_ifnum))
		return 0;

	ipmi_unregister_watchdog(watchdog_ifnum);
	ipmi_register_watchdog(ifnum_to_use);
	return 0;
}

static const struct kernel_param_ops param_ops_wdog_ifnum = {
	.set = set_param_wdog_ifnum,
	.get = param_get_int,
};

#define param_check_wdog_ifnum param_check_int

static const struct kernel_param_ops param_ops_str = {
	.set = set_param_str,
	.get = get_param_str,
};

module_param(ifnum_to_use, wdog_ifnum, 0644);
MODULE_PARM_DESC(ifnum_to_use, "The interface number to use for the watchdog "
		 "timer.  Setting to -1 defaults to the first registered "
		 "interface");

module_param(timeout, timeout, 0644);
MODULE_PARM_DESC(timeout, "Timeout value in seconds.");

module_param(pretimeout, timeout, 0644);
MODULE_PARM_DESC(pretimeout, "Pretimeout value in seconds.");

module_param(panic_wdt_timeout, timeout, 0644);
MODULE_PARM_DESC(timeout, "Timeout value on kernel panic in seconds.");

module_param_cb(action, &param_ops_str, action_op, 0644);
MODULE_PARM_DESC(action, "Timeout action. One of: "
		 "reset, none, power_cycle, power_off.");

module_param_cb(preaction, &param_ops_str, preaction_op, 0644);
MODULE_PARM_DESC(preaction, "Pretimeout action.  One of: "
		 "pre_none, pre_smi, pre_nmi, pre_int.");

module_param_cb(preop, &param_ops_str, preop_op, 0644);
MODULE_PARM_DESC(preop, "Pretimeout driver operation.  One of: "
		 "preop_none, preop_panic, preop_give_data.");

module_param(start_now, int, 0444);
MODULE_PARM_DESC(start_now, "Set to 1 to start the watchdog as"
		 "soon as the driver is loaded.");

module_param(nowayout, bool, 0644);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
		 "(default=CONFIG_WATCHDOG_NOWAYOUT)");

/* Default state of the timer. */
static unsigned char ipmi_watchdog_state = WDOG_TIMEOUT_NONE;

/* If shutting down via IPMI, we ignore the heartbeat. */
static int ipmi_ignore_heartbeat;

/* Is someone using the watchdog?  Only one user is allowed. */
static unsigned long ipmi_wdog_open;

/*
 * If set to 1, the heartbeat command will set the state to reset and
 * start the timer.  The timer doesn't normally run when the driver is
 * first opened until the heartbeat is set the first time, this
 * variable is used to accomplish this.
 */
static int ipmi_start_timer_on_heartbeat;

/* IPMI version of the BMC. */
static unsigned char ipmi_version_major;
static unsigned char ipmi_version_minor;

/* If a pretimeout occurs, this is used to allow only one panic to happen. */
static atomic_t preop_panic_excl = ATOMIC_INIT(-1);

#ifdef HAVE_DIE_NMI
static int testing_nmi;
static int nmi_handler_registered;
#endif

static int ipmi_heartbeat(void);

/*
 * We use a mutex to make sure that only one thing can send a set
 * timeout at one time, because we only have one copy of the data.
 * The mutex is claimed when the set_timeout is sent and freed
 * when both messages are free.
 */
static atomic_t set_timeout_tofree = ATOMIC_INIT(0);
static DEFINE_MUTEX(set_timeout_lock);
static DECLARE_COMPLETION(set_timeout_wait);
static void set_timeout_free_smi(struct ipmi_smi_msg *msg)
{
    if (atomic_dec_and_test(&set_timeout_tofree))
	    complete(&set_timeout_wait);
}
static void set_timeout_free_recv(struct ipmi_recv_msg *msg)
{
    if (atomic_dec_and_test(&set_timeout_tofree))
	    complete(&set_timeout_wait);
}
static struct ipmi_smi_msg set_timeout_smi_msg = {
	.done = set_timeout_free_smi
};
static struct ipmi_recv_msg set_timeout_recv_msg = {
	.done = set_timeout_free_recv
};

static int i_ipmi_set_timeout(struct ipmi_smi_msg  *smi_msg,
			      struct ipmi_recv_msg *recv_msg,
			      int                  *send_heartbeat_now)
{
	struct kernel_ipmi_msg            msg;
	unsigned char                     data[6];
	int                               rv;
	struct ipmi_system_interface_addr addr;
	int                               hbnow = 0;


	/* These can be cleared as we are setting the timeout. */
	pretimeout_since_last_heartbeat = 0;

	data[0] = 0;
	WDOG_SET_TIMER_USE(data[0], WDOG_TIMER_USE_SMS_OS);

	if ((ipmi_version_major > 1)
	    || ((ipmi_version_major == 1) && (ipmi_version_minor >= 5))) {
		/* This is an IPMI 1.5-only feature. */
		data[0] |= WDOG_DONT_STOP_ON_SET;
	} else if (ipmi_watchdog_state != WDOG_TIMEOUT_NONE) {
		/*
		 * In ipmi 1.0, setting the timer stops the watchdog, we
		 * need to start it back up again.
		 */
		hbnow = 1;
	}

	data[1] = 0;
	WDOG_SET_TIMEOUT_ACT(data[1], ipmi_watchdog_state);
	if ((pretimeout > 0) && (ipmi_watchdog_state != WDOG_TIMEOUT_NONE)) {
	    WDOG_SET_PRETIMEOUT_ACT(data[1], preaction_val);
	    data[2] = pretimeout;
	} else {
	    WDOG_SET_PRETIMEOUT_ACT(data[1], WDOG_PRETIMEOUT_NONE);
	    data[2] = 0; /* No pretimeout. */
	}
	data[3] = 0;
	WDOG_SET_TIMEOUT(data[4], data[5], timeout);

	addr.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	addr.channel = IPMI_BMC_CHANNEL;
	addr.lun = 0;

	msg.netfn = 0x06;
	msg.cmd = IPMI_WDOG_SET_TIMER;
	msg.data = data;
	msg.data_len = sizeof(data);
	rv = ipmi_request_supply_msgs(watchdog_user,
				      (struct ipmi_addr *) &addr,
				      0,
				      &msg,
				      NULL,
				      smi_msg,
				      recv_msg,
				      1);
	if (rv) {
		printk(KERN_WARNING PFX "set timeout error: %d\n",
		       rv);
	}

	if (send_heartbeat_now)
	    *send_heartbeat_now = hbnow;

	return rv;
}

static int ipmi_set_timeout(int do_heartbeat)
{
	int send_heartbeat_now;
	int rv;


	/* We can only send one of these at a time. */
	mutex_lock(&set_timeout_lock);

	atomic_set(&set_timeout_tofree, 2);

	rv = i_ipmi_set_timeout(&set_timeout_smi_msg,
				&set_timeout_recv_msg,
				&send_heartbeat_now);
	if (rv) {
		mutex_unlock(&set_timeout_lock);
		goto out;
	}

	wait_for_completion(&set_timeout_wait);

	mutex_unlock(&set_timeout_lock);

	if ((do_heartbeat == IPMI_SET_TIMEOUT_FORCE_HB)
	    || ((send_heartbeat_now)
		&& (do_heartbeat == IPMI_SET_TIMEOUT_HB_IF_NECESSARY)))
		rv = ipmi_heartbeat();

out:
	return rv;
}

static atomic_t panic_done_count = ATOMIC_INIT(0);

static void panic_smi_free(struct ipmi_smi_msg *msg)
{
	atomic_dec(&panic_done_count);
}
static void panic_recv_free(struct ipmi_recv_msg *msg)
{
	atomic_dec(&panic_done_count);
}

static struct ipmi_smi_msg panic_halt_heartbeat_smi_msg = {
	.done = panic_smi_free
};
static struct ipmi_recv_msg panic_halt_heartbeat_recv_msg = {
	.done = panic_recv_free
};

static void panic_halt_ipmi_heartbeat(void)
{
	struct kernel_ipmi_msg             msg;
	struct ipmi_system_interface_addr addr;
	int rv;

	/*
	 * Don't reset the timer if we have the timer turned off, that
	 * re-enables the watchdog.
	 */
	if (ipmi_watchdog_state == WDOG_TIMEOUT_NONE)
		return;

	addr.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	addr.channel = IPMI_BMC_CHANNEL;
	addr.lun = 0;

	msg.netfn = 0x06;
	msg.cmd = IPMI_WDOG_RESET_TIMER;
	msg.data = NULL;
	msg.data_len = 0;
	atomic_add(2, &panic_done_count);
	rv = ipmi_request_supply_msgs(watchdog_user,
				      (struct ipmi_addr *) &addr,
				      0,
				      &msg,
				      NULL,
				      &panic_halt_heartbeat_smi_msg,
				      &panic_halt_heartbeat_recv_msg,
				      1);
	if (rv)
		atomic_sub(2, &panic_done_count);
}

static struct ipmi_smi_msg panic_halt_smi_msg = {
	.done = panic_smi_free
};
static struct ipmi_recv_msg panic_halt_recv_msg = {
	.done = panic_recv_free
};

/*
 * Special call, doesn't claim any locks.  This is only to be called
 * at panic or halt time, in run-to-completion mode, when the caller
 * is the only CPU and the only thing that will be going is these IPMI
 * calls.
 */
static void panic_halt_ipmi_set_timeout(void)
{
	int send_heartbeat_now;
	int rv;

	/* Wait for the messages to be free. */
	while (atomic_read(&panic_done_count) != 0)
		ipmi_poll_interface(watchdog_user);
	atomic_add(2, &panic_done_count);
	rv = i_ipmi_set_timeout(&panic_halt_smi_msg,
				&panic_halt_recv_msg,
				&send_heartbeat_now);
	if (rv) {
		atomic_sub(2, &panic_done_count);
		printk(KERN_WARNING PFX
		       "Unable to extend the watchdog timeout.");
	} else {
		if (send_heartbeat_now)
			panic_halt_ipmi_heartbeat();
	}
	while (atomic_read(&panic_done_count) != 0)
		ipmi_poll_interface(watchdog_user);
}

/*
 * We use a mutex to make sure that only one thing can send a
 * heartbeat at one time, because we only have one copy of the data.
 * The semaphore is claimed when the set_timeout is sent and freed
 * when both messages are free.
 */
static atomic_t heartbeat_tofree = ATOMIC_INIT(0);
static DEFINE_MUTEX(heartbeat_lock);
static DECLARE_COMPLETION(heartbeat_wait);
static void heartbeat_free_smi(struct ipmi_smi_msg *msg)
{
    if (atomic_dec_and_test(&heartbeat_tofree))
	    complete(&heartbeat_wait);
}
static void heartbeat_free_recv(struct ipmi_recv_msg *msg)
{
    if (atomic_dec_and_test(&heartbeat_tofree))
	    complete(&heartbeat_wait);
}
static struct ipmi_smi_msg heartbeat_smi_msg = {
	.done = heartbeat_free_smi
};
static struct ipmi_recv_msg heartbeat_recv_msg = {
	.done = heartbeat_free_recv
};

static int ipmi_heartbeat(void)
{
	struct kernel_ipmi_msg            msg;
	int                               rv;
	struct ipmi_system_interface_addr addr;
	int				  timeout_retries = 0;

	if (ipmi_ignore_heartbeat)
		return 0;

	if (ipmi_start_timer_on_heartbeat) {
		ipmi_start_timer_on_heartbeat = 0;
		ipmi_watchdog_state = action_val;
		return ipmi_set_timeout(IPMI_SET_TIMEOUT_FORCE_HB);
	} else if (pretimeout_since_last_heartbeat) {
		/*
		 * A pretimeout occurred, make sure we set the timeout.
		 * We don't want to set the action, though, we want to
		 * leave that alone (thus it can't be combined with the
		 * above operation.
		 */
		return ipmi_set_timeout(IPMI_SET_TIMEOUT_HB_IF_NECESSARY);
	}

	mutex_lock(&heartbeat_lock);

restart:
	atomic_set(&heartbeat_tofree, 2);

	/*
	 * Don't reset the timer if we have the timer turned off, that
	 * re-enables the watchdog.
	 */
	if (ipmi_watchdog_state == WDOG_TIMEOUT_NONE) {
		mutex_unlock(&heartbeat_lock);
		return 0;
	}

	addr.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	addr.channel = IPMI_BMC_CHANNEL;
	addr.lun = 0;

	msg.netfn = 0x06;
	msg.cmd = IPMI_WDOG_RESET_TIMER;
	msg.data = NULL;
	msg.data_len = 0;
	rv = ipmi_request_supply_msgs(watchdog_user,
				      (struct ipmi_addr *) &addr,
				      0,
				      &msg,
				      NULL,
				      &heartbeat_smi_msg,
				      &heartbeat_recv_msg,
				      1);
	if (rv) {
		mutex_unlock(&heartbeat_lock);
		printk(KERN_WARNING PFX "heartbeat failure: %d\n",
		       rv);
		return rv;
	}

	/* Wait for the heartbeat to be sent. */
	wait_for_completion(&heartbeat_wait);

	if (heartbeat_recv_msg.msg.data[0] == IPMI_WDOG_TIMER_NOT_INIT_RESP)  {
		timeout_retries++;
		if (timeout_retries > 3) {
			printk(KERN_ERR PFX ": Unable to restore the IPMI"
			       " watchdog's settings, giving up.\n");
			rv = -EIO;
			goto out_unlock;
		}

		/*
		 * The timer was not initialized, that means the BMC was
		 * probably reset and lost the watchdog information.  Attempt
		 * to restore the timer's info.  Note that we still hold
		 * the heartbeat lock, to keep a heartbeat from happening
		 * in this process, so must say no heartbeat to avoid a
		 * deadlock on this mutex.
		 */
		rv = ipmi_set_timeout(IPMI_SET_TIMEOUT_NO_HB);
		if (rv) {
			printk(KERN_ERR PFX ": Unable to send the command to"
			       " set the watchdog's settings, giving up.\n");
			goto out_unlock;
		}

		/* We might need a new heartbeat, so do it now */
		goto restart;
	} else if (heartbeat_recv_msg.msg.data[0] != 0) {
		/*
		 * Got an error in the heartbeat response.  It was already
		 * reported in ipmi_wdog_msg_handler, but we should return
		 * an error here.
		 */
		rv = -EINVAL;
	}

out_unlock:
	mutex_unlock(&heartbeat_lock);

	return rv;
}

static struct watchdog_info ident = {
	.options	= 0,	/* WDIOF_SETTIMEOUT, */
	.firmware_version = 1,
	.identity	= "IPMI"
};

static int ipmi_ioctl(struct file *file,
		      unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int i;
	int val;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		i = copy_to_user(argp, &ident, sizeof(ident));
		return i ? -EFAULT : 0;

	case WDIOC_SETTIMEOUT:
		i = copy_from_user(&val, argp, sizeof(int));
		if (i)
			return -EFAULT;
		timeout = val;
		return ipmi_set_timeout(IPMI_SET_TIMEOUT_HB_IF_NECESSARY);

	case WDIOC_GETTIMEOUT:
		i = copy_to_user(argp, &timeout, sizeof(timeout));
		if (i)
			return -EFAULT;
		return 0;

	case WDIOC_SETPRETIMEOUT:
		i = copy_from_user(&val, argp, sizeof(int));
		if (i)
			return -EFAULT;
		pretimeout = val;
		return ipmi_set_timeout(IPMI_SET_TIMEOUT_HB_IF_NECESSARY);

	case WDIOC_GETPRETIMEOUT:
		i = copy_to_user(argp, &pretimeout, sizeof(pretimeout));
		if (i)
			return -EFAULT;
		return 0;

	case WDIOC_KEEPALIVE:
		return ipmi_heartbeat();

	case WDIOC_SETOPTIONS:
		i = copy_from_user(&val, argp, sizeof(int));
		if (i)
			return -EFAULT;
		if (val & WDIOS_DISABLECARD) {
			ipmi_watchdog_state = WDOG_TIMEOUT_NONE;
			ipmi_set_timeout(IPMI_SET_TIMEOUT_NO_HB);
			ipmi_start_timer_on_heartbeat = 0;
		}

		if (val & WDIOS_ENABLECARD) {
			ipmi_watchdog_state = action_val;
			ipmi_set_timeout(IPMI_SET_TIMEOUT_FORCE_HB);
		}
		return 0;

	case WDIOC_GETSTATUS:
		val = 0;
		i = copy_to_user(argp, &val, sizeof(val));
		if (i)
			return -EFAULT;
		return 0;

	default:
		return -ENOIOCTLCMD;
	}
}

static long ipmi_unlocked_ioctl(struct file *file,
				unsigned int cmd,
				unsigned long arg)
{
	int ret;

	mutex_lock(&ipmi_watchdog_mutex);
	ret = ipmi_ioctl(file, cmd, arg);
	mutex_unlock(&ipmi_watchdog_mutex);

	return ret;
}

static ssize_t ipmi_write(struct file *file,
			  const char  __user *buf,
			  size_t      len,
			  loff_t      *ppos)
{
	int rv;

	if (len) {
		if (!nowayout) {
			size_t i;

			/* In case it was set long ago */
			expect_close = 0;

			for (i = 0; i != len; i++) {
				char c;

				if (get_user(c, buf + i))
					return -EFAULT;
				if (c == 'V')
					expect_close = 42;
			}
		}
		rv = ipmi_heartbeat();
		if (rv)
			return rv;
	}
	return len;
}

static ssize_t ipmi_read(struct file *file,
			 char        __user *buf,
			 size_t      count,
			 loff_t      *ppos)
{
	int          rv = 0;
	wait_queue_t wait;

	if (count <= 0)
		return 0;

	/*
	 * Reading returns if the pretimeout has gone off, and it only does
	 * it once per pretimeout.
	 */
	spin_lock(&ipmi_read_lock);
	if (!data_to_read) {
		if (file->f_flags & O_NONBLOCK) {
			rv = -EAGAIN;
			goto out;
		}

		init_waitqueue_entry(&wait, current);
		add_wait_queue(&read_q, &wait);
		while (!data_to_read) {
			set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock(&ipmi_read_lock);
			schedule();
			spin_lock(&ipmi_read_lock);
		}
		remove_wait_queue(&read_q, &wait);

		if (signal_pending(current)) {
			rv = -ERESTARTSYS;
			goto out;
		}
	}
	data_to_read = 0;

 out:
	spin_unlock(&ipmi_read_lock);

	if (rv == 0) {
		if (copy_to_user(buf, &data_to_read, 1))
			rv = -EFAULT;
		else
			rv = 1;
	}

	return rv;
}

static int ipmi_open(struct inode *ino, struct file *filep)
{
	switch (iminor(ino)) {
	case WATCHDOG_MINOR:
		if (test_and_set_bit(0, &ipmi_wdog_open))
			return -EBUSY;


		/*
		 * Don't start the timer now, let it start on the
		 * first heartbeat.
		 */
		ipmi_start_timer_on_heartbeat = 1;
		return nonseekable_open(ino, filep);

	default:
		return (-ENODEV);
	}
}

static unsigned int ipmi_poll(struct file *file, poll_table *wait)
{
	unsigned int mask = 0;

	poll_wait(file, &read_q, wait);

	spin_lock(&ipmi_read_lock);
	if (data_to_read)
		mask |= (POLLIN | POLLRDNORM);
	spin_unlock(&ipmi_read_lock);

	return mask;
}

static int ipmi_fasync(int fd, struct file *file, int on)
{
	int result;

	result = fasync_helper(fd, file, on, &fasync_q);

	return (result);
}

static int ipmi_close(struct inode *ino, struct file *filep)
{
	if (iminor(ino) == WATCHDOG_MINOR) {
		if (expect_close == 42) {
			ipmi_watchdog_state = WDOG_TIMEOUT_NONE;
			ipmi_set_timeout(IPMI_SET_TIMEOUT_NO_HB);
		} else {
			printk(KERN_CRIT PFX
			       "Unexpected close, not stopping watchdog!\n");
			ipmi_heartbeat();
		}
		clear_bit(0, &ipmi_wdog_open);
	}

	expect_close = 0;

	return 0;
}

static const struct file_operations ipmi_wdog_fops = {
	.owner   = THIS_MODULE,
	.read    = ipmi_read,
	.poll    = ipmi_poll,
	.write   = ipmi_write,
	.unlocked_ioctl = ipmi_unlocked_ioctl,
	.open    = ipmi_open,
	.release = ipmi_close,
	.fasync  = ipmi_fasync,
	.llseek  = no_llseek,
};

static struct miscdevice ipmi_wdog_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &ipmi_wdog_fops
};

static void ipmi_wdog_msg_handler(struct ipmi_recv_msg *msg,
				  void                 *handler_data)
{
	if (msg->msg.cmd == IPMI_WDOG_RESET_TIMER &&
			msg->msg.data[0] == IPMI_WDOG_TIMER_NOT_INIT_RESP)
		printk(KERN_INFO PFX "response: The IPMI controller appears"
		       " to have been reset, will attempt to reinitialize"
		       " the watchdog timer\n");
	else if (msg->msg.data[0] != 0)
		printk(KERN_ERR PFX "response: Error %x on cmd %x\n",
		       msg->msg.data[0],
		       msg->msg.cmd);

	ipmi_free_recv_msg(msg);
}

static void ipmi_wdog_pretimeout_handler(void *handler_data)
{
	if (preaction_val != WDOG_PRETIMEOUT_NONE) {
		if (preop_val == WDOG_PREOP_PANIC) {
			if (atomic_inc_and_test(&preop_panic_excl))
				panic("Watchdog pre-timeout");
		} else if (preop_val == WDOG_PREOP_GIVE_DATA) {
			spin_lock(&ipmi_read_lock);
			data_to_read = 1;
			wake_up_interruptible(&read_q);
			kill_fasync(&fasync_q, SIGIO, POLL_IN);

			spin_unlock(&ipmi_read_lock);
		}
	}

	/*
	 * On some machines, the heartbeat will give an error and not
	 * work unless we re-enable the timer.  So do so.
	 */
	pretimeout_since_last_heartbeat = 1;
}

static struct ipmi_user_hndl ipmi_hndlrs = {
	.ipmi_recv_hndl           = ipmi_wdog_msg_handler,
	.ipmi_watchdog_pretimeout = ipmi_wdog_pretimeout_handler
};

static void ipmi_register_watchdog(int ipmi_intf)
{
	int rv = -EBUSY;

	if (watchdog_user)
		goto out;

	if ((ifnum_to_use >= 0) && (ifnum_to_use != ipmi_intf))
		goto out;

	watchdog_ifnum = ipmi_intf;

	rv = ipmi_create_user(ipmi_intf, &ipmi_hndlrs, NULL, &watchdog_user);
	if (rv < 0) {
		printk(KERN_CRIT PFX "Unable to register with ipmi\n");
		goto out;
	}

	ipmi_get_version(watchdog_user,
			 &ipmi_version_major,
			 &ipmi_version_minor);

	rv = misc_register(&ipmi_wdog_miscdev);
	if (rv < 0) {
		ipmi_destroy_user(watchdog_user);
		watchdog_user = NULL;
		printk(KERN_CRIT PFX "Unable to register misc device\n");
	}

#ifdef HAVE_DIE_NMI
	if (nmi_handler_registered) {
		int old_pretimeout = pretimeout;
		int old_timeout = timeout;
		int old_preop_val = preop_val;

		/*
		 * Set the pretimeout to go off in a second and give
		 * ourselves plenty of time to stop the timer.
		 */
		ipmi_watchdog_state = WDOG_TIMEOUT_RESET;
		preop_val = WDOG_PREOP_NONE; /* Make sure nothing happens */
		pretimeout = 99;
		timeout = 100;

		testing_nmi = 1;

		rv = ipmi_set_timeout(IPMI_SET_TIMEOUT_FORCE_HB);
		if (rv) {
			printk(KERN_WARNING PFX "Error starting timer to"
			       " test NMI: 0x%x.  The NMI pretimeout will"
			       " likely not work\n", rv);
			rv = 0;
			goto out_restore;
		}

		msleep(1500);

		if (testing_nmi != 2) {
			printk(KERN_WARNING PFX "IPMI NMI didn't seem to"
			       " occur.  The NMI pretimeout will"
			       " likely not work\n");
		}
 out_restore:
		testing_nmi = 0;
		preop_val = old_preop_val;
		pretimeout = old_pretimeout;
		timeout = old_timeout;
	}
#endif

 out:
	if ((start_now) && (rv == 0)) {
		/* Run from startup, so start the timer now. */
		start_now = 0; /* Disable this function after first startup. */
		ipmi_watchdog_state = action_val;
		ipmi_set_timeout(IPMI_SET_TIMEOUT_FORCE_HB);
		printk(KERN_INFO PFX "Starting now!\n");
	} else {
		/* Stop the timer now. */
		ipmi_watchdog_state = WDOG_TIMEOUT_NONE;
		ipmi_set_timeout(IPMI_SET_TIMEOUT_NO_HB);
	}
}

static void ipmi_unregister_watchdog(int ipmi_intf)
{
	int rv;

	if (!watchdog_user)
		goto out;

	if (watchdog_ifnum != ipmi_intf)
		goto out;

	/* Make sure no one can call us any more. */
	misc_deregister(&ipmi_wdog_miscdev);

	/*
	 * Wait to make sure the message makes it out.  The lower layer has
	 * pointers to our buffers, we want to make sure they are done before
	 * we release our memory.
	 */
	while (atomic_read(&set_timeout_tofree))
		schedule_timeout_uninterruptible(1);

	/* Disconnect from IPMI. */
	rv = ipmi_destroy_user(watchdog_user);
	if (rv) {
		printk(KERN_WARNING PFX "error unlinking from IPMI: %d\n",
		       rv);
	}
	watchdog_user = NULL;

 out:
	return;
}

#ifdef HAVE_DIE_NMI
static int
ipmi_nmi(unsigned int val, struct pt_regs *regs)
{
	/*
	 * If we get here, it's an NMI that's not a memory or I/O
	 * error.  We can't truly tell if it's from IPMI or not
	 * without sending a message, and sending a message is almost
	 * impossible because of locking.
	 */

	if (testing_nmi) {
		testing_nmi = 2;
		return NMI_HANDLED;
	}

	/* If we are not expecting a timeout, ignore it. */
	if (ipmi_watchdog_state == WDOG_TIMEOUT_NONE)
		return NMI_DONE;

	if (preaction_val != WDOG_PRETIMEOUT_NMI)
		return NMI_DONE;

	/*
	 * If no one else handled the NMI, we assume it was the IPMI
	 * watchdog.
	 */
	if (preop_val == WDOG_PREOP_PANIC) {
		/* On some machines, the heartbeat will give
		   an error and not work unless we re-enable
		   the timer.   So do so. */
		pretimeout_since_last_heartbeat = 1;
		if (atomic_inc_and_test(&preop_panic_excl))
			nmi_panic(regs, PFX "pre-timeout");
	}

	return NMI_HANDLED;
}
#endif

static int wdog_reboot_handler(struct notifier_block *this,
			       unsigned long         code,
			       void                  *unused)
{
	static int reboot_event_handled;

	if ((watchdog_user) && (!reboot_event_handled)) {
		/* Make sure we only do this once. */
		reboot_event_handled = 1;

		if (code == SYS_POWER_OFF || code == SYS_HALT) {
			/* Disable the WDT if we are shutting down. */
			ipmi_watchdog_state = WDOG_TIMEOUT_NONE;
			ipmi_set_timeout(IPMI_SET_TIMEOUT_NO_HB);
		} else if (ipmi_watchdog_state != WDOG_TIMEOUT_NONE) {
			/* Set a long timer to let the reboot happens, but
			   reboot if it hangs, but only if the watchdog
			   timer was already running. */
			timeout = 120;
			pretimeout = 0;
			ipmi_watchdog_state = WDOG_TIMEOUT_RESET;
			ipmi_set_timeout(IPMI_SET_TIMEOUT_NO_HB);
		}
	}
	return NOTIFY_OK;
}

static struct notifier_block wdog_reboot_notifier = {
	.notifier_call	= wdog_reboot_handler,
	.next		= NULL,
	.priority	= 0
};

static int wdog_panic_handler(struct notifier_block *this,
			      unsigned long         event,
			      void                  *unused)
{
	static int panic_event_handled;

	/* On a panic, if we have a panic timeout, make sure to extend
	   the watchdog timer to a reasonable value to complete the
	   panic, if the watchdog timer is running.  Plus the
	   pretimeout is meaningless at panic time. */
	if (watchdog_user && !panic_event_handled &&
	    ipmi_watchdog_state != WDOG_TIMEOUT_NONE) {
		/* Make sure we do this only once. */
		panic_event_handled = 1;

		timeout = panic_wdt_timeout;
		pretimeout = 0;
		panic_halt_ipmi_set_timeout();
	}

	return NOTIFY_OK;
}

static struct notifier_block wdog_panic_notifier = {
	.notifier_call	= wdog_panic_handler,
	.next		= NULL,
	.priority	= 150	/* priority: INT_MAX >= x >= 0 */
};


static void ipmi_new_smi(int if_num, struct device *device)
{
	ipmi_register_watchdog(if_num);
}

static void ipmi_smi_gone(int if_num)
{
	ipmi_unregister_watchdog(if_num);
}

static struct ipmi_smi_watcher smi_watcher = {
	.owner    = THIS_MODULE,
	.new_smi  = ipmi_new_smi,
	.smi_gone = ipmi_smi_gone
};

static int action_op(const char *inval, char *outval)
{
	if (outval)
		strcpy(outval, action);

	if (!inval)
		return 0;

	if (strcmp(inval, "reset") == 0)
		action_val = WDOG_TIMEOUT_RESET;
	else if (strcmp(inval, "none") == 0)
		action_val = WDOG_TIMEOUT_NONE;
	else if (strcmp(inval, "power_cycle") == 0)
		action_val = WDOG_TIMEOUT_POWER_CYCLE;
	else if (strcmp(inval, "power_off") == 0)
		action_val = WDOG_TIMEOUT_POWER_DOWN;
	else
		return -EINVAL;
	strcpy(action, inval);
	return 0;
}

static int preaction_op(const char *inval, char *outval)
{
	if (outval)
		strcpy(outval, preaction);

	if (!inval)
		return 0;

	if (strcmp(inval, "pre_none") == 0)
		preaction_val = WDOG_PRETIMEOUT_NONE;
	else if (strcmp(inval, "pre_smi") == 0)
		preaction_val = WDOG_PRETIMEOUT_SMI;
#ifdef HAVE_DIE_NMI
	else if (strcmp(inval, "pre_nmi") == 0)
		preaction_val = WDOG_PRETIMEOUT_NMI;
#endif
	else if (strcmp(inval, "pre_int") == 0)
		preaction_val = WDOG_PRETIMEOUT_MSG_INT;
	else
		return -EINVAL;
	strcpy(preaction, inval);
	return 0;
}

static int preop_op(const char *inval, char *outval)
{
	if (outval)
		strcpy(outval, preop);

	if (!inval)
		return 0;

	if (strcmp(inval, "preop_none") == 0)
		preop_val = WDOG_PREOP_NONE;
	else if (strcmp(inval, "preop_panic") == 0)
		preop_val = WDOG_PREOP_PANIC;
	else if (strcmp(inval, "preop_give_data") == 0)
		preop_val = WDOG_PREOP_GIVE_DATA;
	else
		return -EINVAL;
	strcpy(preop, inval);
	return 0;
}

static void check_parms(void)
{
#ifdef HAVE_DIE_NMI
	int do_nmi = 0;
	int rv;

	if (preaction_val == WDOG_PRETIMEOUT_NMI) {
		do_nmi = 1;
		if (preop_val == WDOG_PREOP_GIVE_DATA) {
			printk(KERN_WARNING PFX "Pretimeout op is to give data"
			       " but NMI pretimeout is enabled, setting"
			       " pretimeout op to none\n");
			preop_op("preop_none", NULL);
			do_nmi = 0;
		}
	}
	if (do_nmi && !nmi_handler_registered) {
		rv = register_nmi_handler(NMI_UNKNOWN, ipmi_nmi, 0,
						"ipmi");
		if (rv) {
			printk(KERN_WARNING PFX
			       "Can't register nmi handler\n");
			return;
		} else
			nmi_handler_registered = 1;
	} else if (!do_nmi && nmi_handler_registered) {
		unregister_nmi_handler(NMI_UNKNOWN, "ipmi");
		nmi_handler_registered = 0;
	}
#endif
}

static int __init ipmi_wdog_init(void)
{
	int rv;

	if (action_op(action, NULL)) {
		action_op("reset", NULL);
		printk(KERN_INFO PFX "Unknown action '%s', defaulting to"
		       " reset\n", action);
	}

	if (preaction_op(preaction, NULL)) {
		preaction_op("pre_none", NULL);
		printk(KERN_INFO PFX "Unknown preaction '%s', defaulting to"
		       " none\n", preaction);
	}

	if (preop_op(preop, NULL)) {
		preop_op("preop_none", NULL);
		printk(KERN_INFO PFX "Unknown preop '%s', defaulting to"
		       " none\n", preop);
	}

	check_parms();

	register_reboot_notifier(&wdog_reboot_notifier);
	atomic_notifier_chain_register(&panic_notifier_list,
			&wdog_panic_notifier);

	rv = ipmi_smi_watcher_register(&smi_watcher);
	if (rv) {
#ifdef HAVE_DIE_NMI
		if (nmi_handler_registered)
			unregister_nmi_handler(NMI_UNKNOWN, "ipmi");
#endif
		atomic_notifier_chain_unregister(&panic_notifier_list,
						 &wdog_panic_notifier);
		unregister_reboot_notifier(&wdog_reboot_notifier);
		printk(KERN_WARNING PFX "can't register smi watcher\n");
		return rv;
	}

	printk(KERN_INFO PFX "driver initialized\n");

	return 0;
}

static void __exit ipmi_wdog_exit(void)
{
	ipmi_smi_watcher_unregister(&smi_watcher);
	ipmi_unregister_watchdog(watchdog_ifnum);

#ifdef HAVE_DIE_NMI
	if (nmi_handler_registered)
		unregister_nmi_handler(NMI_UNKNOWN, "ipmi");
#endif

	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &wdog_panic_notifier);
	unregister_reboot_notifier(&wdog_reboot_notifier);
}
module_exit(ipmi_wdog_exit);
module_init(ipmi_wdog_init);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Corey Minyard <minyard@mvista.com>");
MODULE_DESCRIPTION("watchdog timer based upon the IPMI interface.");
