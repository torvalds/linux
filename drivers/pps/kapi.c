/*
 * kernel API
 *
 *
 * Copyright (C) 2005-2009   Rodolfo Giometti <giometti@linux.it>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/fs.h>
#include <linux/pps_kernel.h>
#include <linux/slab.h>

/*
 * Global variables
 */

DEFINE_SPINLOCK(pps_idr_lock);
DEFINE_IDR(pps_idr);

/*
 * Local functions
 */

static void pps_add_offset(struct pps_ktime *ts, struct pps_ktime *offset)
{
	ts->nsec += offset->nsec;
	while (ts->nsec >= NSEC_PER_SEC) {
		ts->nsec -= NSEC_PER_SEC;
		ts->sec++;
	}
	while (ts->nsec < 0) {
		ts->nsec += NSEC_PER_SEC;
		ts->sec--;
	}
	ts->sec += offset->sec;
}

/*
 * Exported functions
 */

/* pps_get_source - find a PPS source
 * @source: the PPS source ID.
 *
 * This function is used to find an already registered PPS source into the
 * system.
 *
 * The function returns NULL if found nothing, otherwise it returns a pointer
 * to the PPS source data struct (the refcounter is incremented by 1).
 */

struct pps_device *pps_get_source(int source)
{
	struct pps_device *pps;
	unsigned long flags;

	spin_lock_irqsave(&pps_idr_lock, flags);

	pps = idr_find(&pps_idr, source);
	if (pps != NULL)
		atomic_inc(&pps->usage);

	spin_unlock_irqrestore(&pps_idr_lock, flags);

	return pps;
}

/* pps_put_source - free the PPS source data
 * @pps: a pointer to the PPS source.
 *
 * This function is used to free a PPS data struct if its refcount is 0.
 */

void pps_put_source(struct pps_device *pps)
{
	unsigned long flags;

	spin_lock_irqsave(&pps_idr_lock, flags);
	BUG_ON(atomic_read(&pps->usage) == 0);

	if (!atomic_dec_and_test(&pps->usage)) {
		pps = NULL;
		goto exit;
	}

	/* No more reference to the PPS source. We can safely remove the
	 * PPS data struct.
	 */
	idr_remove(&pps_idr, pps->id);

exit:
	spin_unlock_irqrestore(&pps_idr_lock, flags);
	kfree(pps);
}

/* pps_register_source - add a PPS source in the system
 * @info: the PPS info struct
 * @default_params: the default PPS parameters of the new source
 *
 * This function is used to add a new PPS source in the system. The new
 * source is described by info's fields and it will have, as default PPS
 * parameters, the ones specified into default_params.
 *
 * The function returns, in case of success, the PPS source ID.
 */

int pps_register_source(struct pps_source_info *info, int default_params)
{
	struct pps_device *pps;
	int id;
	int err;

	/* Sanity checks */
	if ((info->mode & default_params) != default_params) {
		printk(KERN_ERR "pps: %s: unsupported default parameters\n",
					info->name);
		err = -EINVAL;
		goto pps_register_source_exit;
	}
	if ((info->mode & (PPS_ECHOASSERT | PPS_ECHOCLEAR)) != 0 &&
			info->echo == NULL) {
		printk(KERN_ERR "pps: %s: echo function is not defined\n",
					info->name);
		err = -EINVAL;
		goto pps_register_source_exit;
	}
	if ((info->mode & (PPS_TSFMT_TSPEC | PPS_TSFMT_NTPFP)) == 0) {
		printk(KERN_ERR "pps: %s: unspecified time format\n",
					info->name);
		err = -EINVAL;
		goto pps_register_source_exit;
	}

	/* Allocate memory for the new PPS source struct */
	pps = kzalloc(sizeof(struct pps_device), GFP_KERNEL);
	if (pps == NULL) {
		err = -ENOMEM;
		goto pps_register_source_exit;
	}

	/* These initializations must be done before calling idr_get_new()
	 * in order to avoid reces into pps_event().
	 */
	pps->params.api_version = PPS_API_VERS;
	pps->params.mode = default_params;
	pps->info = *info;

	init_waitqueue_head(&pps->queue);
	spin_lock_init(&pps->lock);
	atomic_set(&pps->usage, 1);

	/* Get new ID for the new PPS source */
	if (idr_pre_get(&pps_idr, GFP_KERNEL) == 0) {
		err = -ENOMEM;
		goto kfree_pps;
	}

	spin_lock_irq(&pps_idr_lock);

	/* Now really allocate the PPS source.
	 * After idr_get_new() calling the new source will be freely available
	 * into the kernel.
	 */
	err = idr_get_new(&pps_idr, pps, &id);
	if (err < 0) {
		spin_unlock_irq(&pps_idr_lock);
		goto kfree_pps;
	}

	id = id & MAX_ID_MASK;
	if (id >= PPS_MAX_SOURCES) {
		spin_unlock_irq(&pps_idr_lock);

		printk(KERN_ERR "pps: %s: too many PPS sources in the system\n",
					info->name);
		err = -EBUSY;
		goto free_idr;
	}
	pps->id = id;

	spin_unlock_irq(&pps_idr_lock);

	/* Create the char device */
	err = pps_register_cdev(pps);
	if (err < 0) {
		printk(KERN_ERR "pps: %s: unable to create char device\n",
					info->name);
		goto free_idr;
	}

	pr_info("new PPS source %s at ID %d\n", info->name, id);

	return id;

free_idr:
	spin_lock_irq(&pps_idr_lock);
	idr_remove(&pps_idr, id);
	spin_unlock_irq(&pps_idr_lock);

kfree_pps:
	kfree(pps);

pps_register_source_exit:
	printk(KERN_ERR "pps: %s: unable to register source\n", info->name);

	return err;
}
EXPORT_SYMBOL(pps_register_source);

/* pps_unregister_source - remove a PPS source from the system
 * @source: the PPS source ID
 *
 * This function is used to remove a previously registered PPS source from
 * the system.
 */

void pps_unregister_source(int source)
{
	struct pps_device *pps;

	spin_lock_irq(&pps_idr_lock);
	pps = idr_find(&pps_idr, source);

	if (!pps) {
		BUG();
		spin_unlock_irq(&pps_idr_lock);
		return;
	}
	spin_unlock_irq(&pps_idr_lock);

	pps_unregister_cdev(pps);
	pps_put_source(pps);
}
EXPORT_SYMBOL(pps_unregister_source);

/* pps_event - register a PPS event into the system
 * @source: the PPS source ID
 * @ts: the event timestamp
 * @event: the event type
 * @data: userdef pointer
 *
 * This function is used by each PPS client in order to register a new
 * PPS event into the system (it's usually called inside an IRQ handler).
 *
 * If an echo function is associated with the PPS source it will be called
 * as:
 *	pps->info.echo(source, event, data);
 */

void pps_event(int source, struct pps_ktime *ts, int event, void *data)
{
	struct pps_device *pps;
	unsigned long flags;
	int captured = 0;

	if ((event & (PPS_CAPTUREASSERT | PPS_CAPTURECLEAR)) == 0) {
		printk(KERN_ERR "pps: unknown event (%x) for source %d\n",
			event, source);
		return;
	}

	pps = pps_get_source(source);
	if (!pps)
		return;

	pr_debug("PPS event on source %d at %llu.%06u\n",
			pps->id, (unsigned long long) ts->sec, ts->nsec);

	spin_lock_irqsave(&pps->lock, flags);

	/* Must call the echo function? */
	if ((pps->params.mode & (PPS_ECHOASSERT | PPS_ECHOCLEAR)))
		pps->info.echo(source, event, data);

	/* Check the event */
	pps->current_mode = pps->params.mode;
	if ((event & PPS_CAPTUREASSERT) &
			(pps->params.mode & PPS_CAPTUREASSERT)) {
		/* We have to add an offset? */
		if (pps->params.mode & PPS_OFFSETASSERT)
			pps_add_offset(ts, &pps->params.assert_off_tu);

		/* Save the time stamp */
		pps->assert_tu = *ts;
		pps->assert_sequence++;
		pr_debug("capture assert seq #%u for source %d\n",
			pps->assert_sequence, source);

		captured = ~0;
	}
	if ((event & PPS_CAPTURECLEAR) &
			(pps->params.mode & PPS_CAPTURECLEAR)) {
		/* We have to add an offset? */
		if (pps->params.mode & PPS_OFFSETCLEAR)
			pps_add_offset(ts, &pps->params.clear_off_tu);

		/* Save the time stamp */
		pps->clear_tu = *ts;
		pps->clear_sequence++;
		pr_debug("capture clear seq #%u for source %d\n",
			pps->clear_sequence, source);

		captured = ~0;
	}

	/* Wake up iif captured somthing */
	if (captured) {
		pps->go = ~0;
		wake_up_interruptible(&pps->queue);

		kill_fasync(&pps->async_queue, SIGIO, POLL_IN);
	}

	spin_unlock_irqrestore(&pps->lock, flags);

	/* Now we can release the PPS source for (possible) deregistration */
	pps_put_source(pps);
}
EXPORT_SYMBOL(pps_event);
