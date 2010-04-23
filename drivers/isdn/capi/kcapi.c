/* $Id: kcapi.c,v 1.1.2.8 2004/03/26 19:57:20 armin Exp $
 * 
 * Kernel CAPI 2.0 Module
 * 
 * Copyright 1999 by Carsten Paeth <calle@calle.de>
 * Copyright 2002 by Kai Germaschewski <kai@germaschewski.name>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#define AVMB1_COMPAT

#include "kcapi.h"
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/capi.h>
#include <linux/kernelcapi.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/isdn/capicmd.h>
#include <linux/isdn/capiutil.h>
#ifdef AVMB1_COMPAT
#include <linux/b1lli.h>
#endif
#include <linux/mutex.h>
#include <linux/rcupdate.h>

static int showcapimsgs = 0;

MODULE_DESCRIPTION("CAPI4Linux: kernel CAPI layer");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");
module_param(showcapimsgs, uint, 0);

/* ------------------------------------------------------------- */

struct capictr_event {
	struct work_struct work;
	unsigned int type;
	u32 controller;
};

/* ------------------------------------------------------------- */

static struct capi_version driver_version = {2, 0, 1, 1<<4};
static char driver_serial[CAPI_SERIAL_LEN] = "0004711";
static char capi_manufakturer[64] = "AVM Berlin";

#define NCCI2CTRL(ncci)    (((ncci) >> 24) & 0x7f)

LIST_HEAD(capi_drivers);
DEFINE_MUTEX(capi_drivers_lock);

struct capi_ctr *capi_controller[CAPI_MAXCONTR];
DEFINE_MUTEX(capi_controller_lock);

struct capi20_appl *capi_applications[CAPI_MAXAPPL];

static int ncontrollers;

static BLOCKING_NOTIFIER_HEAD(ctr_notifier_list);

/* -------- controller ref counting -------------------------------------- */

static inline struct capi_ctr *
capi_ctr_get(struct capi_ctr *ctr)
{
	if (!try_module_get(ctr->owner))
		return NULL;
	return ctr;
}

static inline void
capi_ctr_put(struct capi_ctr *ctr)
{
	module_put(ctr->owner);
}

/* ------------------------------------------------------------- */

static inline struct capi_ctr *get_capi_ctr_by_nr(u16 contr)
{
	if (contr - 1 >= CAPI_MAXCONTR)
		return NULL;

	return capi_controller[contr - 1];
}

static inline struct capi20_appl *get_capi_appl_by_nr(u16 applid)
{
	if (applid - 1 >= CAPI_MAXAPPL)
		return NULL;

	return rcu_dereference(capi_applications[applid - 1]);
}

/* -------- util functions ------------------------------------ */

static inline int capi_cmd_valid(u8 cmd)
{
	switch (cmd) {
	case CAPI_ALERT:
	case CAPI_CONNECT:
	case CAPI_CONNECT_ACTIVE:
	case CAPI_CONNECT_B3_ACTIVE:
	case CAPI_CONNECT_B3:
	case CAPI_CONNECT_B3_T90_ACTIVE:
	case CAPI_DATA_B3:
	case CAPI_DISCONNECT_B3:
	case CAPI_DISCONNECT:
	case CAPI_FACILITY:
	case CAPI_INFO:
	case CAPI_LISTEN:
	case CAPI_MANUFACTURER:
	case CAPI_RESET_B3:
	case CAPI_SELECT_B_PROTOCOL:
		return 1;
	}
	return 0;
}

static inline int capi_subcmd_valid(u8 subcmd)
{
	switch (subcmd) {
	case CAPI_REQ:
	case CAPI_CONF:
	case CAPI_IND:
	case CAPI_RESP:
		return 1;
	}
	return 0;
}

/* ------------------------------------------------------------ */

static void
register_appl(struct capi_ctr *ctr, u16 applid, capi_register_params *rparam)
{
	ctr = capi_ctr_get(ctr);

	if (ctr)
		ctr->register_appl(ctr, applid, rparam);
	else
		printk(KERN_WARNING "%s: cannot get controller resources\n",
		       __func__);
}


static void release_appl(struct capi_ctr *ctr, u16 applid)
{
	DBG("applid %#x", applid);
	
	ctr->release_appl(ctr, applid);
	capi_ctr_put(ctr);
}

static void notify_up(u32 contr)
{
	struct capi20_appl *ap;
	struct capi_ctr *ctr;
	u16 applid;

	mutex_lock(&capi_controller_lock);

	if (showcapimsgs & 1)
	        printk(KERN_DEBUG "kcapi: notify up contr %d\n", contr);

	ctr = get_capi_ctr_by_nr(contr);
	if (ctr) {
		if (ctr->state == CAPI_CTR_RUNNING)
			goto unlock_out;

		ctr->state = CAPI_CTR_RUNNING;

		for (applid = 1; applid <= CAPI_MAXAPPL; applid++) {
			ap = get_capi_appl_by_nr(applid);
			if (!ap)
				continue;
			register_appl(ctr, applid, &ap->rparam);
		}

		wake_up_interruptible_all(&ctr->state_wait_queue);
	} else
		printk(KERN_WARNING "%s: invalid contr %d\n", __func__, contr);

unlock_out:
	mutex_unlock(&capi_controller_lock);
}

static void ctr_down(struct capi_ctr *ctr, int new_state)
{
	struct capi20_appl *ap;
	u16 applid;

	if (ctr->state == CAPI_CTR_DETECTED || ctr->state == CAPI_CTR_DETACHED)
		return;

	ctr->state = new_state;

	memset(ctr->manu, 0, sizeof(ctr->manu));
	memset(&ctr->version, 0, sizeof(ctr->version));
	memset(&ctr->profile, 0, sizeof(ctr->profile));
	memset(ctr->serial, 0, sizeof(ctr->serial));

	for (applid = 1; applid <= CAPI_MAXAPPL; applid++) {
		ap = get_capi_appl_by_nr(applid);
		if (ap)
			capi_ctr_put(ctr);
	}

	wake_up_interruptible_all(&ctr->state_wait_queue);
}

static void notify_down(u32 contr)
{
	struct capi_ctr *ctr;

	mutex_lock(&capi_controller_lock);

	if (showcapimsgs & 1)
		printk(KERN_DEBUG "kcapi: notify down contr %d\n", contr);

	ctr = get_capi_ctr_by_nr(contr);
	if (ctr)
		ctr_down(ctr, CAPI_CTR_DETECTED);
	else
		printk(KERN_WARNING "%s: invalid contr %d\n", __func__, contr);

	mutex_unlock(&capi_controller_lock);
}

static int
notify_handler(struct notifier_block *nb, unsigned long val, void *v)
{
	u32 contr = (long)v;

	switch (val) {
	case CAPICTR_UP:
		notify_up(contr);
		break;
	case CAPICTR_DOWN:
		notify_down(contr);
		break;
	}
	return NOTIFY_OK;
}

static void do_notify_work(struct work_struct *work)
{
	struct capictr_event *event =
		container_of(work, struct capictr_event, work);

	blocking_notifier_call_chain(&ctr_notifier_list, event->type,
				     (void *)(long)event->controller);
	kfree(event);
}

/*
 * The notifier will result in adding/deleteing of devices. Devices can
 * only removed in user process, not in bh.
 */
static int notify_push(unsigned int event_type, u32 controller)
{
	struct capictr_event *event = kmalloc(sizeof(*event), GFP_ATOMIC);

	if (!event)
		return -ENOMEM;

	INIT_WORK(&event->work, do_notify_work);
	event->type = event_type;
	event->controller = controller;

	schedule_work(&event->work);
	return 0;
}

int register_capictr_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&ctr_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(register_capictr_notifier);

int unregister_capictr_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&ctr_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(unregister_capictr_notifier);

/* -------- Receiver ------------------------------------------ */

static void recv_handler(struct work_struct *work)
{
	struct sk_buff *skb;
	struct capi20_appl *ap =
		container_of(work, struct capi20_appl, recv_work);

	if ((!ap) || (ap->release_in_progress))
		return;

	mutex_lock(&ap->recv_mtx);
	while ((skb = skb_dequeue(&ap->recv_queue))) {
		if (CAPIMSG_CMD(skb->data) == CAPI_DATA_B3_IND)
			ap->nrecvdatapkt++;
		else
			ap->nrecvctlpkt++;

		ap->recv_message(ap, skb);
	}
	mutex_unlock(&ap->recv_mtx);
}

/**
 * capi_ctr_handle_message() - handle incoming CAPI message
 * @ctr:	controller descriptor structure.
 * @appl:	application ID.
 * @skb:	message.
 *
 * Called by hardware driver to pass a CAPI message to the application.
 */

void capi_ctr_handle_message(struct capi_ctr *ctr, u16 appl,
			     struct sk_buff *skb)
{
	struct capi20_appl *ap;
	int showctl = 0;
	u8 cmd, subcmd;
	_cdebbuf *cdb;

	if (ctr->state != CAPI_CTR_RUNNING) {
		cdb = capi_message2str(skb->data);
		if (cdb) {
			printk(KERN_INFO "kcapi: controller [%03d] not active, got: %s",
				ctr->cnr, cdb->buf);
			cdebbuf_free(cdb);
		} else
			printk(KERN_INFO "kcapi: controller [%03d] not active, cannot trace\n",
				ctr->cnr);
		goto error;
	}

	cmd = CAPIMSG_COMMAND(skb->data);
        subcmd = CAPIMSG_SUBCOMMAND(skb->data);
	if (cmd == CAPI_DATA_B3 && subcmd == CAPI_IND) {
		ctr->nrecvdatapkt++;
		if (ctr->traceflag > 2)
			showctl |= 2;
	} else {
		ctr->nrecvctlpkt++;
		if (ctr->traceflag)
			showctl |= 2;
	}
	showctl |= (ctr->traceflag & 1);
	if (showctl & 2) {
		if (showctl & 1) {
			printk(KERN_DEBUG "kcapi: got [%03d] id#%d %s len=%u\n",
			       ctr->cnr, CAPIMSG_APPID(skb->data),
			       capi_cmd2str(cmd, subcmd),
			       CAPIMSG_LEN(skb->data));
		} else {
			cdb = capi_message2str(skb->data);
			if (cdb) {
				printk(KERN_DEBUG "kcapi: got [%03d] %s\n",
					ctr->cnr, cdb->buf);
				cdebbuf_free(cdb);
			} else
				printk(KERN_DEBUG "kcapi: got [%03d] id#%d %s len=%u, cannot trace\n",
					ctr->cnr, CAPIMSG_APPID(skb->data),
					capi_cmd2str(cmd, subcmd),
					CAPIMSG_LEN(skb->data));
		}

	}

	rcu_read_lock();
	ap = get_capi_appl_by_nr(CAPIMSG_APPID(skb->data));
	if (!ap) {
		rcu_read_unlock();
		cdb = capi_message2str(skb->data);
		if (cdb) {
			printk(KERN_ERR "kcapi: handle_message: applid %d state released (%s)\n",
			CAPIMSG_APPID(skb->data), cdb->buf);
			cdebbuf_free(cdb);
		} else
			printk(KERN_ERR "kcapi: handle_message: applid %d state released (%s) cannot trace\n",
				CAPIMSG_APPID(skb->data),
				capi_cmd2str(cmd, subcmd));
		goto error;
	}
	skb_queue_tail(&ap->recv_queue, skb);
	schedule_work(&ap->recv_work);
	rcu_read_unlock();

	return;

error:
	kfree_skb(skb);
}

EXPORT_SYMBOL(capi_ctr_handle_message);

/**
 * capi_ctr_ready() - signal CAPI controller ready
 * @ctr:	controller descriptor structure.
 *
 * Called by hardware driver to signal that the controller is up and running.
 */

void capi_ctr_ready(struct capi_ctr *ctr)
{
	printk(KERN_NOTICE "kcapi: controller [%03d] \"%s\" ready.\n",
	       ctr->cnr, ctr->name);

	notify_push(CAPICTR_UP, ctr->cnr);
}

EXPORT_SYMBOL(capi_ctr_ready);

/**
 * capi_ctr_down() - signal CAPI controller not ready
 * @ctr:	controller descriptor structure.
 *
 * Called by hardware driver to signal that the controller is down and
 * unavailable for use.
 */

void capi_ctr_down(struct capi_ctr *ctr)
{
	printk(KERN_NOTICE "kcapi: controller [%03d] down.\n", ctr->cnr);

	notify_push(CAPICTR_DOWN, ctr->cnr);
}

EXPORT_SYMBOL(capi_ctr_down);

/**
 * capi_ctr_suspend_output() - suspend controller
 * @ctr:	controller descriptor structure.
 *
 * Called by hardware driver to stop data flow.
 *
 * Note: The caller is responsible for synchronizing concurrent state changes
 * as well as invocations of capi_ctr_handle_message.
 */

void capi_ctr_suspend_output(struct capi_ctr *ctr)
{
	if (!ctr->blocked) {
		printk(KERN_DEBUG "kcapi: controller [%03d] suspend\n",
		       ctr->cnr);
		ctr->blocked = 1;
	}
}

EXPORT_SYMBOL(capi_ctr_suspend_output);

/**
 * capi_ctr_resume_output() - resume controller
 * @ctr:	controller descriptor structure.
 *
 * Called by hardware driver to resume data flow.
 *
 * Note: The caller is responsible for synchronizing concurrent state changes
 * as well as invocations of capi_ctr_handle_message.
 */

void capi_ctr_resume_output(struct capi_ctr *ctr)
{
	if (ctr->blocked) {
		printk(KERN_DEBUG "kcapi: controller [%03d] resumed\n",
		       ctr->cnr);
		ctr->blocked = 0;
	}
}

EXPORT_SYMBOL(capi_ctr_resume_output);

/* ------------------------------------------------------------- */

/**
 * attach_capi_ctr() - register CAPI controller
 * @ctr:	controller descriptor structure.
 *
 * Called by hardware driver to register a controller with the CAPI subsystem.
 * Return value: 0 on success, error code < 0 on error
 */

int attach_capi_ctr(struct capi_ctr *ctr)
{
	int i;

	mutex_lock(&capi_controller_lock);

	for (i = 0; i < CAPI_MAXCONTR; i++) {
		if (!capi_controller[i])
			break;
	}
	if (i == CAPI_MAXCONTR) {
		mutex_unlock(&capi_controller_lock);
		printk(KERN_ERR "kcapi: out of controller slots\n");
	   	return -EBUSY;
	}
	capi_controller[i] = ctr;

	ctr->nrecvctlpkt = 0;
	ctr->nrecvdatapkt = 0;
	ctr->nsentctlpkt = 0;
	ctr->nsentdatapkt = 0;
	ctr->cnr = i + 1;
	ctr->state = CAPI_CTR_DETECTED;
	ctr->blocked = 0;
	ctr->traceflag = showcapimsgs;
	init_waitqueue_head(&ctr->state_wait_queue);

	sprintf(ctr->procfn, "capi/controllers/%d", ctr->cnr);
	ctr->procent = proc_create_data(ctr->procfn, 0, NULL, ctr->proc_fops, ctr);

	ncontrollers++;

	mutex_unlock(&capi_controller_lock);

	printk(KERN_NOTICE "kcapi: controller [%03d]: %s attached\n",
			ctr->cnr, ctr->name);
	return 0;
}

EXPORT_SYMBOL(attach_capi_ctr);

/**
 * detach_capi_ctr() - unregister CAPI controller
 * @ctr:	controller descriptor structure.
 *
 * Called by hardware driver to remove the registration of a controller
 * with the CAPI subsystem.
 * Return value: 0 on success, error code < 0 on error
 */

int detach_capi_ctr(struct capi_ctr *ctr)
{
	int err = 0;

	mutex_lock(&capi_controller_lock);

	ctr_down(ctr, CAPI_CTR_DETACHED);

	if (capi_controller[ctr->cnr - 1] != ctr) {
		err = -EINVAL;
		goto unlock_out;
	}
	capi_controller[ctr->cnr - 1] = NULL;
	ncontrollers--;

	if (ctr->procent)
		remove_proc_entry(ctr->procfn, NULL);

	printk(KERN_NOTICE "kcapi: controller [%03d]: %s unregistered\n",
	       ctr->cnr, ctr->name);

unlock_out:
	mutex_unlock(&capi_controller_lock);

	return err;
}

EXPORT_SYMBOL(detach_capi_ctr);

/**
 * register_capi_driver() - register CAPI driver
 * @driver:	driver descriptor structure.
 *
 * Called by hardware driver to register itself with the CAPI subsystem.
 */

void register_capi_driver(struct capi_driver *driver)
{
	mutex_lock(&capi_drivers_lock);
	list_add_tail(&driver->list, &capi_drivers);
	mutex_unlock(&capi_drivers_lock);
}

EXPORT_SYMBOL(register_capi_driver);

/**
 * unregister_capi_driver() - unregister CAPI driver
 * @driver:	driver descriptor structure.
 *
 * Called by hardware driver to unregister itself from the CAPI subsystem.
 */

void unregister_capi_driver(struct capi_driver *driver)
{
	mutex_lock(&capi_drivers_lock);
	list_del(&driver->list);
	mutex_unlock(&capi_drivers_lock);
}

EXPORT_SYMBOL(unregister_capi_driver);

/* ------------------------------------------------------------- */
/* -------- CAPI2.0 Interface ---------------------------------- */
/* ------------------------------------------------------------- */

/**
 * capi20_isinstalled() - CAPI 2.0 operation CAPI_INSTALLED
 *
 * Return value: CAPI result code (CAPI_NOERROR if at least one ISDN controller
 *	is ready for use, CAPI_REGNOTINSTALLED otherwise)
 */

u16 capi20_isinstalled(void)
{
	u16 ret = CAPI_REGNOTINSTALLED;
	int i;

	mutex_lock(&capi_controller_lock);

	for (i = 0; i < CAPI_MAXCONTR; i++)
		if (capi_controller[i] &&
		    capi_controller[i]->state == CAPI_CTR_RUNNING) {
			ret = CAPI_NOERROR;
			break;
		}

	mutex_unlock(&capi_controller_lock);

	return ret;
}

EXPORT_SYMBOL(capi20_isinstalled);

/**
 * capi20_register() - CAPI 2.0 operation CAPI_REGISTER
 * @ap:		CAPI application descriptor structure.
 *
 * Register an application's presence with CAPI.
 * A unique application ID is assigned and stored in @ap->applid.
 * After this function returns successfully, the message receive
 * callback function @ap->recv_message() may be called at any time
 * until capi20_release() has been called for the same @ap.
 * Return value: CAPI result code
 */

u16 capi20_register(struct capi20_appl *ap)
{
	int i;
	u16 applid;

	DBG("");

	if (ap->rparam.datablklen < 128)
		return CAPI_LOGBLKSIZETOSMALL;

	ap->nrecvctlpkt = 0;
	ap->nrecvdatapkt = 0;
	ap->nsentctlpkt = 0;
	ap->nsentdatapkt = 0;
	mutex_init(&ap->recv_mtx);
	skb_queue_head_init(&ap->recv_queue);
	INIT_WORK(&ap->recv_work, recv_handler);
	ap->release_in_progress = 0;

	mutex_lock(&capi_controller_lock);

	for (applid = 1; applid <= CAPI_MAXAPPL; applid++) {
		if (capi_applications[applid - 1] == NULL)
			break;
	}
	if (applid > CAPI_MAXAPPL) {
		mutex_unlock(&capi_controller_lock);
		return CAPI_TOOMANYAPPLS;
	}

	ap->applid = applid;
	capi_applications[applid - 1] = ap;

	for (i = 0; i < CAPI_MAXCONTR; i++) {
		if (!capi_controller[i] ||
		    capi_controller[i]->state != CAPI_CTR_RUNNING)
			continue;
		register_appl(capi_controller[i], applid, &ap->rparam);
	}

	mutex_unlock(&capi_controller_lock);

	if (showcapimsgs & 1) {
		printk(KERN_DEBUG "kcapi: appl %d up\n", applid);
	}

	return CAPI_NOERROR;
}

EXPORT_SYMBOL(capi20_register);

/**
 * capi20_release() - CAPI 2.0 operation CAPI_RELEASE
 * @ap:		CAPI application descriptor structure.
 *
 * Terminate an application's registration with CAPI.
 * After this function returns successfully, the message receive
 * callback function @ap->recv_message() will no longer be called.
 * Return value: CAPI result code
 */

u16 capi20_release(struct capi20_appl *ap)
{
	int i;

	DBG("applid %#x", ap->applid);

	mutex_lock(&capi_controller_lock);

	ap->release_in_progress = 1;
	capi_applications[ap->applid - 1] = NULL;

	synchronize_rcu();

	for (i = 0; i < CAPI_MAXCONTR; i++) {
		if (!capi_controller[i] ||
		    capi_controller[i]->state != CAPI_CTR_RUNNING)
			continue;
		release_appl(capi_controller[i], ap->applid);
	}

	mutex_unlock(&capi_controller_lock);

	flush_scheduled_work();
	skb_queue_purge(&ap->recv_queue);

	if (showcapimsgs & 1) {
		printk(KERN_DEBUG "kcapi: appl %d down\n", ap->applid);
	}

	return CAPI_NOERROR;
}

EXPORT_SYMBOL(capi20_release);

/**
 * capi20_put_message() - CAPI 2.0 operation CAPI_PUT_MESSAGE
 * @ap:		CAPI application descriptor structure.
 * @skb:	CAPI message.
 *
 * Transfer a single message to CAPI.
 * Return value: CAPI result code
 */

u16 capi20_put_message(struct capi20_appl *ap, struct sk_buff *skb)
{
	struct capi_ctr *ctr;
	int showctl = 0;
	u8 cmd, subcmd;

	DBG("applid %#x", ap->applid);
 
	if (ncontrollers == 0)
		return CAPI_REGNOTINSTALLED;
	if ((ap->applid == 0) || ap->release_in_progress)
		return CAPI_ILLAPPNR;
	if (skb->len < 12
	    || !capi_cmd_valid(CAPIMSG_COMMAND(skb->data))
	    || !capi_subcmd_valid(CAPIMSG_SUBCOMMAND(skb->data)))
		return CAPI_ILLCMDORSUBCMDORMSGTOSMALL;

	/*
	 * The controller reference is protected by the existence of the
	 * application passed to us. We assume that the caller properly
	 * synchronizes this service with capi20_release.
	 */
	ctr = get_capi_ctr_by_nr(CAPIMSG_CONTROLLER(skb->data));
	if (!ctr || ctr->state != CAPI_CTR_RUNNING)
		return CAPI_REGNOTINSTALLED;
	if (ctr->blocked)
		return CAPI_SENDQUEUEFULL;

	cmd = CAPIMSG_COMMAND(skb->data);
        subcmd = CAPIMSG_SUBCOMMAND(skb->data);

	if (cmd == CAPI_DATA_B3 && subcmd== CAPI_REQ) {
		ctr->nsentdatapkt++;
		ap->nsentdatapkt++;
		if (ctr->traceflag > 2)
			showctl |= 2;
	} else {
		ctr->nsentctlpkt++;
		ap->nsentctlpkt++;
		if (ctr->traceflag)
			showctl |= 2;
	}
	showctl |= (ctr->traceflag & 1);
	if (showctl & 2) {
		if (showctl & 1) {
			printk(KERN_DEBUG "kcapi: put [%03d] id#%d %s len=%u\n",
			       CAPIMSG_CONTROLLER(skb->data),
			       CAPIMSG_APPID(skb->data),
			       capi_cmd2str(cmd, subcmd),
			       CAPIMSG_LEN(skb->data));
		} else {
			_cdebbuf *cdb = capi_message2str(skb->data);
			if (cdb) {
				printk(KERN_DEBUG "kcapi: put [%03d] %s\n",
					CAPIMSG_CONTROLLER(skb->data),
					cdb->buf);
				cdebbuf_free(cdb);
			} else
				printk(KERN_DEBUG "kcapi: put [%03d] id#%d %s len=%u cannot trace\n",
					CAPIMSG_CONTROLLER(skb->data),
					CAPIMSG_APPID(skb->data),
					capi_cmd2str(cmd, subcmd),
					CAPIMSG_LEN(skb->data));
		}
	}
	return ctr->send_message(ctr, skb);
}

EXPORT_SYMBOL(capi20_put_message);

/**
 * capi20_get_manufacturer() - CAPI 2.0 operation CAPI_GET_MANUFACTURER
 * @contr:	controller number.
 * @buf:	result buffer (64 bytes).
 *
 * Retrieve information about the manufacturer of the specified ISDN controller
 * or (for @contr == 0) the driver itself.
 * Return value: CAPI result code
 */

u16 capi20_get_manufacturer(u32 contr, u8 *buf)
{
	struct capi_ctr *ctr;
	u16 ret;

	if (contr == 0) {
		strlcpy(buf, capi_manufakturer, CAPI_MANUFACTURER_LEN);
		return CAPI_NOERROR;
	}

	mutex_lock(&capi_controller_lock);

	ctr = get_capi_ctr_by_nr(contr);
	if (ctr && ctr->state == CAPI_CTR_RUNNING) {
		strlcpy(buf, ctr->manu, CAPI_MANUFACTURER_LEN);
		ret = CAPI_NOERROR;
	} else
		ret = CAPI_REGNOTINSTALLED;

	mutex_unlock(&capi_controller_lock);
	return ret;
}

EXPORT_SYMBOL(capi20_get_manufacturer);

/**
 * capi20_get_version() - CAPI 2.0 operation CAPI_GET_VERSION
 * @contr:	controller number.
 * @verp:	result structure.
 *
 * Retrieve version information for the specified ISDN controller
 * or (for @contr == 0) the driver itself.
 * Return value: CAPI result code
 */

u16 capi20_get_version(u32 contr, struct capi_version *verp)
{
	struct capi_ctr *ctr;
	u16 ret;

	if (contr == 0) {
		*verp = driver_version;
		return CAPI_NOERROR;
	}

	mutex_lock(&capi_controller_lock);

	ctr = get_capi_ctr_by_nr(contr);
	if (ctr && ctr->state == CAPI_CTR_RUNNING) {
		memcpy(verp, &ctr->version, sizeof(capi_version));
		ret = CAPI_NOERROR;
	} else
		ret = CAPI_REGNOTINSTALLED;

	mutex_unlock(&capi_controller_lock);
	return ret;
}

EXPORT_SYMBOL(capi20_get_version);

/**
 * capi20_get_serial() - CAPI 2.0 operation CAPI_GET_SERIAL_NUMBER
 * @contr:	controller number.
 * @serial:	result buffer (8 bytes).
 *
 * Retrieve the serial number of the specified ISDN controller
 * or (for @contr == 0) the driver itself.
 * Return value: CAPI result code
 */

u16 capi20_get_serial(u32 contr, u8 *serial)
{
	struct capi_ctr *ctr;
	u16 ret;

	if (contr == 0) {
		strlcpy(serial, driver_serial, CAPI_SERIAL_LEN);
		return CAPI_NOERROR;
	}

	mutex_lock(&capi_controller_lock);

	ctr = get_capi_ctr_by_nr(contr);
	if (ctr && ctr->state == CAPI_CTR_RUNNING) {
		strlcpy(serial, ctr->serial, CAPI_SERIAL_LEN);
		ret = CAPI_NOERROR;
	} else
		ret = CAPI_REGNOTINSTALLED;

	mutex_unlock(&capi_controller_lock);
	return ret;
}

EXPORT_SYMBOL(capi20_get_serial);

/**
 * capi20_get_profile() - CAPI 2.0 operation CAPI_GET_PROFILE
 * @contr:	controller number.
 * @profp:	result structure.
 *
 * Retrieve capability information for the specified ISDN controller
 * or (for @contr == 0) the number of installed controllers.
 * Return value: CAPI result code
 */

u16 capi20_get_profile(u32 contr, struct capi_profile *profp)
{
	struct capi_ctr *ctr;
	u16 ret;

	if (contr == 0) {
		profp->ncontroller = ncontrollers;
		return CAPI_NOERROR;
	}

	mutex_lock(&capi_controller_lock);

	ctr = get_capi_ctr_by_nr(contr);
	if (ctr && ctr->state == CAPI_CTR_RUNNING) {
		memcpy(profp, &ctr->profile, sizeof(struct capi_profile));
		ret = CAPI_NOERROR;
	} else
		ret = CAPI_REGNOTINSTALLED;

	mutex_unlock(&capi_controller_lock);
	return ret;
}

EXPORT_SYMBOL(capi20_get_profile);

/* Must be called with capi_controller_lock held. */
static int wait_on_ctr_state(struct capi_ctr *ctr, unsigned int state)
{
	DEFINE_WAIT(wait);
	int retval = 0;

	ctr = capi_ctr_get(ctr);
	if (!ctr)
		return -ESRCH;

	for (;;) {
		prepare_to_wait(&ctr->state_wait_queue, &wait,
				TASK_INTERRUPTIBLE);

		if (ctr->state == state)
			break;
		if (ctr->state == CAPI_CTR_DETACHED) {
			retval = -ESRCH;
			break;
		}
		if (signal_pending(current)) {
			retval = -EINTR;
			break;
		}

		mutex_unlock(&capi_controller_lock);
		schedule();
		mutex_lock(&capi_controller_lock);
	}
	finish_wait(&ctr->state_wait_queue, &wait);

	capi_ctr_put(ctr);

	return retval;
}

#ifdef AVMB1_COMPAT
static int old_capi_manufacturer(unsigned int cmd, void __user *data)
{
	avmb1_loadandconfigdef ldef;
	avmb1_extcarddef cdef;
	avmb1_resetdef rdef;
	capicardparams cparams;
	struct capi_ctr *ctr;
	struct capi_driver *driver = NULL;
	capiloaddata ldata;
	struct list_head *l;
	int retval;

	switch (cmd) {
	case AVMB1_ADDCARD:
	case AVMB1_ADDCARD_WITH_TYPE:
		if (cmd == AVMB1_ADDCARD) {
		   if ((retval = copy_from_user(&cdef, data,
					    sizeof(avmb1_carddef))))
			   return retval;
		   cdef.cardtype = AVM_CARDTYPE_B1;
		} else {
		   if ((retval = copy_from_user(&cdef, data,
					    sizeof(avmb1_extcarddef))))
			   return retval;
		}
		cparams.port = cdef.port;
		cparams.irq = cdef.irq;
		cparams.cardnr = cdef.cardnr;

		mutex_lock(&capi_drivers_lock);

                switch (cdef.cardtype) {
			case AVM_CARDTYPE_B1:
				list_for_each(l, &capi_drivers) {
					driver = list_entry(l, struct capi_driver, list);
					if (strcmp(driver->name, "b1isa") == 0)
						break;
				}
				break;
			case AVM_CARDTYPE_T1:
				list_for_each(l, &capi_drivers) {
					driver = list_entry(l, struct capi_driver, list);
					if (strcmp(driver->name, "t1isa") == 0)
						break;
				}
				break;
			default:
				driver = NULL;
				break;
		}
		if (!driver) {
			printk(KERN_ERR "kcapi: driver not loaded.\n");
			retval = -EIO;
		} else if (!driver->add_card) {
			printk(KERN_ERR "kcapi: driver has no add card function.\n");
			retval = -EIO;
		} else
			retval = driver->add_card(driver, &cparams);

		mutex_unlock(&capi_drivers_lock);
		return retval;

	case AVMB1_LOAD:
	case AVMB1_LOAD_AND_CONFIG:

		if (cmd == AVMB1_LOAD) {
			if (copy_from_user(&ldef, data,
					   sizeof(avmb1_loaddef)))
				return -EFAULT;
			ldef.t4config.len = 0;
			ldef.t4config.data = NULL;
		} else {
			if (copy_from_user(&ldef, data,
					   sizeof(avmb1_loadandconfigdef)))
				return -EFAULT;
		}

		mutex_lock(&capi_controller_lock);

		ctr = get_capi_ctr_by_nr(ldef.contr);
		if (!ctr) {
			retval = -EINVAL;
			goto load_unlock_out;
		}

		if (ctr->load_firmware == NULL) {
			printk(KERN_DEBUG "kcapi: load: no load function\n");
			retval = -ESRCH;
			goto load_unlock_out;
		}

		if (ldef.t4file.len <= 0) {
			printk(KERN_DEBUG "kcapi: load: invalid parameter: length of t4file is %d ?\n", ldef.t4file.len);
			retval = -EINVAL;
			goto load_unlock_out;
		}
		if (ldef.t4file.data == NULL) {
			printk(KERN_DEBUG "kcapi: load: invalid parameter: dataptr is 0\n");
			retval = -EINVAL;
			goto load_unlock_out;
		}

		ldata.firmware.user = 1;
		ldata.firmware.data = ldef.t4file.data;
		ldata.firmware.len = ldef.t4file.len;
		ldata.configuration.user = 1;
		ldata.configuration.data = ldef.t4config.data;
		ldata.configuration.len = ldef.t4config.len;

		if (ctr->state != CAPI_CTR_DETECTED) {
			printk(KERN_INFO "kcapi: load: contr=%d not in detect state\n", ldef.contr);
			retval = -EBUSY;
			goto load_unlock_out;
		}
		ctr->state = CAPI_CTR_LOADING;

		retval = ctr->load_firmware(ctr, &ldata);
		if (retval) {
			ctr->state = CAPI_CTR_DETECTED;
			goto load_unlock_out;
		}

		retval = wait_on_ctr_state(ctr, CAPI_CTR_RUNNING);

load_unlock_out:
		mutex_unlock(&capi_controller_lock);
		return retval;

	case AVMB1_RESETCARD:
		if (copy_from_user(&rdef, data, sizeof(avmb1_resetdef)))
			return -EFAULT;

		retval = 0;

		mutex_lock(&capi_controller_lock);

		ctr = get_capi_ctr_by_nr(rdef.contr);
		if (!ctr) {
			retval = -ESRCH;
			goto reset_unlock_out;
		}

		if (ctr->state == CAPI_CTR_DETECTED)
			goto reset_unlock_out;

		ctr->reset_ctr(ctr);

		retval = wait_on_ctr_state(ctr, CAPI_CTR_DETECTED);

reset_unlock_out:
		mutex_unlock(&capi_controller_lock);
		return retval;
	}
	return -EINVAL;
}
#endif

/**
 * capi20_manufacturer() - CAPI 2.0 operation CAPI_MANUFACTURER
 * @cmd:	command.
 * @data:	parameter.
 *
 * Perform manufacturer specific command.
 * Return value: CAPI result code
 */

int capi20_manufacturer(unsigned int cmd, void __user *data)
{
	struct capi_ctr *ctr;
	int retval;

	switch (cmd) {
#ifdef AVMB1_COMPAT
	case AVMB1_LOAD:
	case AVMB1_LOAD_AND_CONFIG:
	case AVMB1_RESETCARD:
	case AVMB1_GET_CARDINFO:
	case AVMB1_REMOVECARD:
		return old_capi_manufacturer(cmd, data);
#endif
	case KCAPI_CMD_TRACE:
	{
		kcapi_flagdef fdef;

		if (copy_from_user(&fdef, data, sizeof(kcapi_flagdef)))
			return -EFAULT;

		mutex_lock(&capi_controller_lock);

		ctr = get_capi_ctr_by_nr(fdef.contr);
		if (ctr) {
			ctr->traceflag = fdef.flag;
			printk(KERN_INFO "kcapi: contr [%03d] set trace=%d\n",
			       ctr->cnr, ctr->traceflag);
			retval = 0;
		} else
			retval = -ESRCH;

		mutex_unlock(&capi_controller_lock);

		return retval;
	}
	case KCAPI_CMD_ADDCARD:
	{
		struct list_head *l;
		struct capi_driver *driver = NULL;
		capicardparams cparams;
		kcapi_carddef cdef;

		if ((retval = copy_from_user(&cdef, data, sizeof(cdef))))
			return retval;

		cparams.port = cdef.port;
		cparams.irq = cdef.irq;
		cparams.membase = cdef.membase;
		cparams.cardnr = cdef.cardnr;
		cparams.cardtype = 0;
		cdef.driver[sizeof(cdef.driver)-1] = 0;

		mutex_lock(&capi_drivers_lock);

		list_for_each(l, &capi_drivers) {
			driver = list_entry(l, struct capi_driver, list);
			if (strcmp(driver->name, cdef.driver) == 0)
				break;
		}
		if (driver == NULL) {
			printk(KERN_ERR "kcapi: driver \"%s\" not loaded.\n",
					cdef.driver);
			retval = -ESRCH;
		} else if (!driver->add_card) {
			printk(KERN_ERR "kcapi: driver \"%s\" has no add card function.\n", cdef.driver);
			retval = -EIO;
		} else
			retval = driver->add_card(driver, &cparams);

		mutex_unlock(&capi_drivers_lock);
		return retval;
	}

	default:
		printk(KERN_ERR "kcapi: manufacturer command %d unknown.\n",
					cmd);
		break;

	}
	return -EINVAL;
}

EXPORT_SYMBOL(capi20_manufacturer);

/* ------------------------------------------------------------- */
/* -------- Init & Cleanup ------------------------------------- */
/* ------------------------------------------------------------- */

/*
 * init / exit functions
 */

static struct notifier_block capictr_nb = {
	.notifier_call = notify_handler,
	.priority = INT_MAX,
};

static int __init kcapi_init(void)
{
	int err;

	register_capictr_notifier(&capictr_nb);

	err = cdebug_init();
	if (!err)
		kcapi_proc_init();
	return err;
}

static void __exit kcapi_exit(void)
{
        kcapi_proc_exit();

	/* make sure all notifiers are finished */
	flush_scheduled_work();
	cdebug_exit();
}

module_init(kcapi_init);
module_exit(kcapi_exit);
