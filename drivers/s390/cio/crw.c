/*
 *   Channel report handling code
 *
 *    Copyright IBM Corp. 2000,2009
 *    Author(s): Ingo Adlung <adlung@de.ibm.com>,
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *		 Cornelia Huck <cornelia.huck@de.ibm.com>,
 *		 Heiko Carstens <heiko.carstens@de.ibm.com>,
 */

#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/init.h>
#include <asm/crw.h>

static struct semaphore crw_semaphore;
static DEFINE_MUTEX(crw_handler_mutex);
static crw_handler_t crw_handlers[NR_RSCS];

/**
 * crw_register_handler() - register a channel report word handler
 * @rsc: reporting source code to handle
 * @handler: handler to be registered
 *
 * Returns %0 on success and a negative error value otherwise.
 */
int crw_register_handler(int rsc, crw_handler_t handler)
{
	int rc = 0;

	if ((rsc < 0) || (rsc >= NR_RSCS))
		return -EINVAL;
	mutex_lock(&crw_handler_mutex);
	if (crw_handlers[rsc])
		rc = -EBUSY;
	else
		crw_handlers[rsc] = handler;
	mutex_unlock(&crw_handler_mutex);
	return rc;
}

/**
 * crw_unregister_handler() - unregister a channel report word handler
 * @rsc: reporting source code to handle
 */
void crw_unregister_handler(int rsc)
{
	if ((rsc < 0) || (rsc >= NR_RSCS))
		return;
	mutex_lock(&crw_handler_mutex);
	crw_handlers[rsc] = NULL;
	mutex_unlock(&crw_handler_mutex);
}

/*
 * Retrieve CRWs and call function to handle event.
 */
static int crw_collect_info(void *unused)
{
	struct crw crw[2];
	int ccode;
	unsigned int chain;
	int ignore;

repeat:
	ignore = down_interruptible(&crw_semaphore);
	chain = 0;
	while (1) {
		crw_handler_t handler;

		if (unlikely(chain > 1)) {
			struct crw tmp_crw;

			printk(KERN_WARNING"%s: Code does not support more "
			       "than two chained crws; please report to "
			       "linux390@de.ibm.com!\n", __func__);
			ccode = stcrw(&tmp_crw);
			printk(KERN_WARNING"%s: crw reports slct=%d, oflw=%d, "
			       "chn=%d, rsc=%X, anc=%d, erc=%X, rsid=%X\n",
			       __func__, tmp_crw.slct, tmp_crw.oflw,
			       tmp_crw.chn, tmp_crw.rsc, tmp_crw.anc,
			       tmp_crw.erc, tmp_crw.rsid);
			printk(KERN_WARNING"%s: This was crw number %x in the "
			       "chain\n", __func__, chain);
			if (ccode != 0)
				break;
			chain = tmp_crw.chn ? chain + 1 : 0;
			continue;
		}
		ccode = stcrw(&crw[chain]);
		if (ccode != 0)
			break;
		printk(KERN_DEBUG "crw_info : CRW reports slct=%d, oflw=%d, "
		       "chn=%d, rsc=%X, anc=%d, erc=%X, rsid=%X\n",
		       crw[chain].slct, crw[chain].oflw, crw[chain].chn,
		       crw[chain].rsc, crw[chain].anc, crw[chain].erc,
		       crw[chain].rsid);
		/* Check for overflows. */
		if (crw[chain].oflw) {
			int i;

			pr_debug("%s: crw overflow detected!\n", __func__);
			mutex_lock(&crw_handler_mutex);
			for (i = 0; i < NR_RSCS; i++) {
				if (crw_handlers[i])
					crw_handlers[i](NULL, NULL, 1);
			}
			mutex_unlock(&crw_handler_mutex);
			chain = 0;
			continue;
		}
		if (crw[0].chn && !chain) {
			chain++;
			continue;
		}
		mutex_lock(&crw_handler_mutex);
		handler = crw_handlers[crw[chain].rsc];
		if (handler)
			handler(&crw[0], chain ? &crw[1] : NULL, 0);
		mutex_unlock(&crw_handler_mutex);
		/* chain is always 0 or 1 here. */
		chain = crw[chain].chn ? chain + 1 : 0;
	}
	goto repeat;
	return 0;
}

void crw_handle_channel_report(void)
{
	up(&crw_semaphore);
}

/*
 * Separate initcall needed for semaphore initialization since
 * crw_handle_channel_report might be called before crw_machine_check_init.
 */
static int __init crw_init_semaphore(void)
{
	init_MUTEX_LOCKED(&crw_semaphore);
	return 0;
}
pure_initcall(crw_init_semaphore);

/*
 * Machine checks for the channel subsystem must be enabled
 * after the channel subsystem is initialized
 */
static int __init crw_machine_check_init(void)
{
	struct task_struct *task;

	task = kthread_run(crw_collect_info, NULL, "kmcheck");
	if (IS_ERR(task))
		return PTR_ERR(task);
	ctl_set_bit(14, 28);	/* enable channel report MCH */
	return 0;
}
device_initcall(crw_machine_check_init);
