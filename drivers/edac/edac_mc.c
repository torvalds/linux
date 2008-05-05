/*
 * edac_mc kernel module
 * (C) 2005, 2006 Linux Networx (http://lnxi.com)
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * Written by Thayne Harbaugh
 * Based on work by Dan Hollis <goemon at anime dot net> and others.
 *	http://www.anime.net/~goemon/linux-ecc/
 *
 * Modified by Dave Peterson and Doug Thompson
 *
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/sysctl.h>
#include <linux/highmem.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/sysdev.h>
#include <linux/ctype.h>
#include <linux/edac.h>
#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/edac.h>
#include "edac_core.h"
#include "edac_module.h"

/* lock to memory controller's control array */
static DEFINE_MUTEX(mem_ctls_mutex);
static LIST_HEAD(mc_devices);

#ifdef CONFIG_EDAC_DEBUG

static void edac_mc_dump_channel(struct channel_info *chan)
{
	debugf4("\tchannel = %p\n", chan);
	debugf4("\tchannel->chan_idx = %d\n", chan->chan_idx);
	debugf4("\tchannel->ce_count = %d\n", chan->ce_count);
	debugf4("\tchannel->label = '%s'\n", chan->label);
	debugf4("\tchannel->csrow = %p\n\n", chan->csrow);
}

static void edac_mc_dump_csrow(struct csrow_info *csrow)
{
	debugf4("\tcsrow = %p\n", csrow);
	debugf4("\tcsrow->csrow_idx = %d\n", csrow->csrow_idx);
	debugf4("\tcsrow->first_page = 0x%lx\n", csrow->first_page);
	debugf4("\tcsrow->last_page = 0x%lx\n", csrow->last_page);
	debugf4("\tcsrow->page_mask = 0x%lx\n", csrow->page_mask);
	debugf4("\tcsrow->nr_pages = 0x%x\n", csrow->nr_pages);
	debugf4("\tcsrow->nr_channels = %d\n", csrow->nr_channels);
	debugf4("\tcsrow->channels = %p\n", csrow->channels);
	debugf4("\tcsrow->mci = %p\n\n", csrow->mci);
}

static void edac_mc_dump_mci(struct mem_ctl_info *mci)
{
	debugf3("\tmci = %p\n", mci);
	debugf3("\tmci->mtype_cap = %lx\n", mci->mtype_cap);
	debugf3("\tmci->edac_ctl_cap = %lx\n", mci->edac_ctl_cap);
	debugf3("\tmci->edac_cap = %lx\n", mci->edac_cap);
	debugf4("\tmci->edac_check = %p\n", mci->edac_check);
	debugf3("\tmci->nr_csrows = %d, csrows = %p\n",
		mci->nr_csrows, mci->csrows);
	debugf3("\tdev = %p\n", mci->dev);
	debugf3("\tmod_name:ctl_name = %s:%s\n", mci->mod_name, mci->ctl_name);
	debugf3("\tpvt_info = %p\n\n", mci->pvt_info);
}

#endif				/* CONFIG_EDAC_DEBUG */

/* 'ptr' points to a possibly unaligned item X such that sizeof(X) is 'size'.
 * Adjust 'ptr' so that its alignment is at least as stringent as what the
 * compiler would provide for X and return the aligned result.
 *
 * If 'size' is a constant, the compiler will optimize this whole function
 * down to either a no-op or the addition of a constant to the value of 'ptr'.
 */
void *edac_align_ptr(void *ptr, unsigned size)
{
	unsigned align, r;

	/* Here we assume that the alignment of a "long long" is the most
	 * stringent alignment that the compiler will ever provide by default.
	 * As far as I know, this is a reasonable assumption.
	 */
	if (size > sizeof(long))
		align = sizeof(long long);
	else if (size > sizeof(int))
		align = sizeof(long);
	else if (size > sizeof(short))
		align = sizeof(int);
	else if (size > sizeof(char))
		align = sizeof(short);
	else
		return (char *)ptr;

	r = size % align;

	if (r == 0)
		return (char *)ptr;

	return (void *)(((unsigned long)ptr) + align - r);
}

/**
 * edac_mc_alloc: Allocate a struct mem_ctl_info structure
 * @size_pvt:	size of private storage needed
 * @nr_csrows:	Number of CWROWS needed for this MC
 * @nr_chans:	Number of channels for the MC
 *
 * Everything is kmalloc'ed as one big chunk - more efficient.
 * Only can be used if all structures have the same lifetime - otherwise
 * you have to allocate and initialize your own structures.
 *
 * Use edac_mc_free() to free mc structures allocated by this function.
 *
 * Returns:
 *	NULL allocation failed
 *	struct mem_ctl_info pointer
 */
struct mem_ctl_info *edac_mc_alloc(unsigned sz_pvt, unsigned nr_csrows,
				unsigned nr_chans, int edac_index)
{
	struct mem_ctl_info *mci;
	struct csrow_info *csi, *csrow;
	struct channel_info *chi, *chp, *chan;
	void *pvt;
	unsigned size;
	int row, chn;
	int err;

	/* Figure out the offsets of the various items from the start of an mc
	 * structure.  We want the alignment of each item to be at least as
	 * stringent as what the compiler would provide if we could simply
	 * hardcode everything into a single struct.
	 */
	mci = (struct mem_ctl_info *)0;
	csi = edac_align_ptr(&mci[1], sizeof(*csi));
	chi = edac_align_ptr(&csi[nr_csrows], sizeof(*chi));
	pvt = edac_align_ptr(&chi[nr_chans * nr_csrows], sz_pvt);
	size = ((unsigned long)pvt) + sz_pvt;

	mci = kzalloc(size, GFP_KERNEL);
	if (mci == NULL)
		return NULL;

	/* Adjust pointers so they point within the memory we just allocated
	 * rather than an imaginary chunk of memory located at address 0.
	 */
	csi = (struct csrow_info *)(((char *)mci) + ((unsigned long)csi));
	chi = (struct channel_info *)(((char *)mci) + ((unsigned long)chi));
	pvt = sz_pvt ? (((char *)mci) + ((unsigned long)pvt)) : NULL;

	/* setup index and various internal pointers */
	mci->mc_idx = edac_index;
	mci->csrows = csi;
	mci->pvt_info = pvt;
	mci->nr_csrows = nr_csrows;

	for (row = 0; row < nr_csrows; row++) {
		csrow = &csi[row];
		csrow->csrow_idx = row;
		csrow->mci = mci;
		csrow->nr_channels = nr_chans;
		chp = &chi[row * nr_chans];
		csrow->channels = chp;

		for (chn = 0; chn < nr_chans; chn++) {
			chan = &chp[chn];
			chan->chan_idx = chn;
			chan->csrow = csrow;
		}
	}

	mci->op_state = OP_ALLOC;

	/*
	 * Initialize the 'root' kobj for the edac_mc controller
	 */
	err = edac_mc_register_sysfs_main_kobj(mci);
	if (err) {
		kfree(mci);
		return NULL;
	}

	/* at this point, the root kobj is valid, and in order to
	 * 'free' the object, then the function:
	 *      edac_mc_unregister_sysfs_main_kobj() must be called
	 * which will perform kobj unregistration and the actual free
	 * will occur during the kobject callback operation
	 */
	return mci;
}
EXPORT_SYMBOL_GPL(edac_mc_alloc);

/**
 * edac_mc_free
 *	'Free' a previously allocated 'mci' structure
 * @mci: pointer to a struct mem_ctl_info structure
 */
void edac_mc_free(struct mem_ctl_info *mci)
{
	edac_mc_unregister_sysfs_main_kobj(mci);
}
EXPORT_SYMBOL_GPL(edac_mc_free);


/*
 * find_mci_by_dev
 *
 *	scan list of controllers looking for the one that manages
 *	the 'dev' device
 */
static struct mem_ctl_info *find_mci_by_dev(struct device *dev)
{
	struct mem_ctl_info *mci;
	struct list_head *item;

	debugf3("%s()\n", __func__);

	list_for_each(item, &mc_devices) {
		mci = list_entry(item, struct mem_ctl_info, link);

		if (mci->dev == dev)
			return mci;
	}

	return NULL;
}

/*
 * handler for EDAC to check if NMI type handler has asserted interrupt
 */
static int edac_mc_assert_error_check_and_clear(void)
{
	int old_state;

	if (edac_op_state == EDAC_OPSTATE_POLL)
		return 1;

	old_state = edac_err_assert;
	edac_err_assert = 0;

	return old_state;
}

/*
 * edac_mc_workq_function
 *	performs the operation scheduled by a workq request
 */
static void edac_mc_workq_function(struct work_struct *work_req)
{
	struct delayed_work *d_work = (struct delayed_work *)work_req;
	struct mem_ctl_info *mci = to_edac_mem_ctl_work(d_work);

	mutex_lock(&mem_ctls_mutex);

	/* if this control struct has movd to offline state, we are done */
	if (mci->op_state == OP_OFFLINE) {
		mutex_unlock(&mem_ctls_mutex);
		return;
	}

	/* Only poll controllers that are running polled and have a check */
	if (edac_mc_assert_error_check_and_clear() && (mci->edac_check != NULL))
		mci->edac_check(mci);

	mutex_unlock(&mem_ctls_mutex);

	/* Reschedule */
	queue_delayed_work(edac_workqueue, &mci->work,
			msecs_to_jiffies(edac_mc_get_poll_msec()));
}

/*
 * edac_mc_workq_setup
 *	initialize a workq item for this mci
 *	passing in the new delay period in msec
 *
 *	locking model:
 *
 *		called with the mem_ctls_mutex held
 */
static void edac_mc_workq_setup(struct mem_ctl_info *mci, unsigned msec)
{
	debugf0("%s()\n", __func__);

	/* if this instance is not in the POLL state, then simply return */
	if (mci->op_state != OP_RUNNING_POLL)
		return;

	INIT_DELAYED_WORK(&mci->work, edac_mc_workq_function);
	queue_delayed_work(edac_workqueue, &mci->work, msecs_to_jiffies(msec));
}

/*
 * edac_mc_workq_teardown
 *	stop the workq processing on this mci
 *
 *	locking model:
 *
 *		called WITHOUT lock held
 */
static void edac_mc_workq_teardown(struct mem_ctl_info *mci)
{
	int status;

	status = cancel_delayed_work(&mci->work);
	if (status == 0) {
		debugf0("%s() not canceled, flush the queue\n",
			__func__);

		/* workq instance might be running, wait for it */
		flush_workqueue(edac_workqueue);
	}
}

/*
 * edac_mc_reset_delay_period(unsigned long value)
 *
 *	user space has updated our poll period value, need to
 *	reset our workq delays
 */
void edac_mc_reset_delay_period(int value)
{
	struct mem_ctl_info *mci;
	struct list_head *item;

	mutex_lock(&mem_ctls_mutex);

	/* scan the list and turn off all workq timers, doing so under lock
	 */
	list_for_each(item, &mc_devices) {
		mci = list_entry(item, struct mem_ctl_info, link);

		if (mci->op_state == OP_RUNNING_POLL)
			cancel_delayed_work(&mci->work);
	}

	mutex_unlock(&mem_ctls_mutex);


	/* re-walk the list, and reset the poll delay */
	mutex_lock(&mem_ctls_mutex);

	list_for_each(item, &mc_devices) {
		mci = list_entry(item, struct mem_ctl_info, link);

		edac_mc_workq_setup(mci, (unsigned long) value);
	}

	mutex_unlock(&mem_ctls_mutex);
}



/* Return 0 on success, 1 on failure.
 * Before calling this function, caller must
 * assign a unique value to mci->mc_idx.
 *
 *	locking model:
 *
 *		called with the mem_ctls_mutex lock held
 */
static int add_mc_to_global_list(struct mem_ctl_info *mci)
{
	struct list_head *item, *insert_before;
	struct mem_ctl_info *p;

	insert_before = &mc_devices;

	p = find_mci_by_dev(mci->dev);
	if (unlikely(p != NULL))
		goto fail0;

	list_for_each(item, &mc_devices) {
		p = list_entry(item, struct mem_ctl_info, link);

		if (p->mc_idx >= mci->mc_idx) {
			if (unlikely(p->mc_idx == mci->mc_idx))
				goto fail1;

			insert_before = item;
			break;
		}
	}

	list_add_tail_rcu(&mci->link, insert_before);
	atomic_inc(&edac_handlers);
	return 0;

fail0:
	edac_printk(KERN_WARNING, EDAC_MC,
		"%s (%s) %s %s already assigned %d\n", p->dev->bus_id,
		edac_dev_name(mci), p->mod_name, p->ctl_name, p->mc_idx);
	return 1;

fail1:
	edac_printk(KERN_WARNING, EDAC_MC,
		"bug in low-level driver: attempt to assign\n"
		"    duplicate mc_idx %d in %s()\n", p->mc_idx, __func__);
	return 1;
}

static void complete_mc_list_del(struct rcu_head *head)
{
	struct mem_ctl_info *mci;

	mci = container_of(head, struct mem_ctl_info, rcu);
	INIT_LIST_HEAD(&mci->link);
	complete(&mci->complete);
}

static void del_mc_from_global_list(struct mem_ctl_info *mci)
{
	atomic_dec(&edac_handlers);
	list_del_rcu(&mci->link);
	init_completion(&mci->complete);
	call_rcu(&mci->rcu, complete_mc_list_del);
	wait_for_completion(&mci->complete);
}

/**
 * edac_mc_find: Search for a mem_ctl_info structure whose index is 'idx'.
 *
 * If found, return a pointer to the structure.
 * Else return NULL.
 *
 * Caller must hold mem_ctls_mutex.
 */
struct mem_ctl_info *edac_mc_find(int idx)
{
	struct list_head *item;
	struct mem_ctl_info *mci;

	list_for_each(item, &mc_devices) {
		mci = list_entry(item, struct mem_ctl_info, link);

		if (mci->mc_idx >= idx) {
			if (mci->mc_idx == idx)
				return mci;

			break;
		}
	}

	return NULL;
}
EXPORT_SYMBOL(edac_mc_find);

/**
 * edac_mc_add_mc: Insert the 'mci' structure into the mci global list and
 *                 create sysfs entries associated with mci structure
 * @mci: pointer to the mci structure to be added to the list
 * @mc_idx: A unique numeric identifier to be assigned to the 'mci' structure.
 *
 * Return:
 *	0	Success
 *	!0	Failure
 */

/* FIXME - should a warning be printed if no error detection? correction? */
int edac_mc_add_mc(struct mem_ctl_info *mci)
{
	debugf0("%s()\n", __func__);

#ifdef CONFIG_EDAC_DEBUG
	if (edac_debug_level >= 3)
		edac_mc_dump_mci(mci);

	if (edac_debug_level >= 4) {
		int i;

		for (i = 0; i < mci->nr_csrows; i++) {
			int j;

			edac_mc_dump_csrow(&mci->csrows[i]);
			for (j = 0; j < mci->csrows[i].nr_channels; j++)
				edac_mc_dump_channel(&mci->csrows[i].
						channels[j]);
		}
	}
#endif
	mutex_lock(&mem_ctls_mutex);

	if (add_mc_to_global_list(mci))
		goto fail0;

	/* set load time so that error rate can be tracked */
	mci->start_time = jiffies;

	if (edac_create_sysfs_mci_device(mci)) {
		edac_mc_printk(mci, KERN_WARNING,
			"failed to create sysfs device\n");
		goto fail1;
	}

	/* If there IS a check routine, then we are running POLLED */
	if (mci->edac_check != NULL) {
		/* This instance is NOW RUNNING */
		mci->op_state = OP_RUNNING_POLL;

		edac_mc_workq_setup(mci, edac_mc_get_poll_msec());
	} else {
		mci->op_state = OP_RUNNING_INTERRUPT;
	}

	/* Report action taken */
	edac_mc_printk(mci, KERN_INFO, "Giving out device to '%s' '%s':"
		" DEV %s\n", mci->mod_name, mci->ctl_name, edac_dev_name(mci));

	mutex_unlock(&mem_ctls_mutex);
	return 0;

fail1:
	del_mc_from_global_list(mci);

fail0:
	mutex_unlock(&mem_ctls_mutex);
	return 1;
}
EXPORT_SYMBOL_GPL(edac_mc_add_mc);

/**
 * edac_mc_del_mc: Remove sysfs entries for specified mci structure and
 *                 remove mci structure from global list
 * @pdev: Pointer to 'struct device' representing mci structure to remove.
 *
 * Return pointer to removed mci structure, or NULL if device not found.
 */
struct mem_ctl_info *edac_mc_del_mc(struct device *dev)
{
	struct mem_ctl_info *mci;

	debugf0("%s()\n", __func__);

	mutex_lock(&mem_ctls_mutex);

	/* find the requested mci struct in the global list */
	mci = find_mci_by_dev(dev);
	if (mci == NULL) {
		mutex_unlock(&mem_ctls_mutex);
		return NULL;
	}

	/* marking MCI offline */
	mci->op_state = OP_OFFLINE;

	del_mc_from_global_list(mci);
	mutex_unlock(&mem_ctls_mutex);

	/* flush workq processes and remove sysfs */
	edac_mc_workq_teardown(mci);
	edac_remove_sysfs_mci_device(mci);

	edac_printk(KERN_INFO, EDAC_MC,
		"Removed device %d for %s %s: DEV %s\n", mci->mc_idx,
		mci->mod_name, mci->ctl_name, edac_dev_name(mci));

	return mci;
}
EXPORT_SYMBOL_GPL(edac_mc_del_mc);

static void edac_mc_scrub_block(unsigned long page, unsigned long offset,
				u32 size)
{
	struct page *pg;
	void *virt_addr;
	unsigned long flags = 0;

	debugf3("%s()\n", __func__);

	/* ECC error page was not in our memory. Ignore it. */
	if (!pfn_valid(page))
		return;

	/* Find the actual page structure then map it and fix */
	pg = pfn_to_page(page);

	if (PageHighMem(pg))
		local_irq_save(flags);

	virt_addr = kmap_atomic(pg, KM_BOUNCE_READ);

	/* Perform architecture specific atomic scrub operation */
	atomic_scrub(virt_addr + offset, size);

	/* Unmap and complete */
	kunmap_atomic(virt_addr, KM_BOUNCE_READ);

	if (PageHighMem(pg))
		local_irq_restore(flags);
}

/* FIXME - should return -1 */
int edac_mc_find_csrow_by_page(struct mem_ctl_info *mci, unsigned long page)
{
	struct csrow_info *csrows = mci->csrows;
	int row, i;

	debugf1("MC%d: %s(): 0x%lx\n", mci->mc_idx, __func__, page);
	row = -1;

	for (i = 0; i < mci->nr_csrows; i++) {
		struct csrow_info *csrow = &csrows[i];

		if (csrow->nr_pages == 0)
			continue;

		debugf3("MC%d: %s(): first(0x%lx) page(0x%lx) last(0x%lx) "
			"mask(0x%lx)\n", mci->mc_idx, __func__,
			csrow->first_page, page, csrow->last_page,
			csrow->page_mask);

		if ((page >= csrow->first_page) &&
		    (page <= csrow->last_page) &&
		    ((page & csrow->page_mask) ==
		     (csrow->first_page & csrow->page_mask))) {
			row = i;
			break;
		}
	}

	if (row == -1)
		edac_mc_printk(mci, KERN_ERR,
			"could not look up page error address %lx\n",
			(unsigned long)page);

	return row;
}
EXPORT_SYMBOL_GPL(edac_mc_find_csrow_by_page);

/* FIXME - setable log (warning/emerg) levels */
/* FIXME - integrate with evlog: http://evlog.sourceforge.net/ */
void edac_mc_handle_ce(struct mem_ctl_info *mci,
		unsigned long page_frame_number,
		unsigned long offset_in_page, unsigned long syndrome,
		int row, int channel, const char *msg)
{
	unsigned long remapped_page;

	debugf3("MC%d: %s()\n", mci->mc_idx, __func__);

	/* FIXME - maybe make panic on INTERNAL ERROR an option */
	if (row >= mci->nr_csrows || row < 0) {
		/* something is wrong */
		edac_mc_printk(mci, KERN_ERR,
			"INTERNAL ERROR: row out of range "
			"(%d >= %d)\n", row, mci->nr_csrows);
		edac_mc_handle_ce_no_info(mci, "INTERNAL ERROR");
		return;
	}

	if (channel >= mci->csrows[row].nr_channels || channel < 0) {
		/* something is wrong */
		edac_mc_printk(mci, KERN_ERR,
			"INTERNAL ERROR: channel out of range "
			"(%d >= %d)\n", channel,
			mci->csrows[row].nr_channels);
		edac_mc_handle_ce_no_info(mci, "INTERNAL ERROR");
		return;
	}

	if (edac_mc_get_log_ce())
		/* FIXME - put in DIMM location */
		edac_mc_printk(mci, KERN_WARNING,
			"CE page 0x%lx, offset 0x%lx, grain %d, syndrome "
			"0x%lx, row %d, channel %d, label \"%s\": %s\n",
			page_frame_number, offset_in_page,
			mci->csrows[row].grain, syndrome, row, channel,
			mci->csrows[row].channels[channel].label, msg);

	mci->ce_count++;
	mci->csrows[row].ce_count++;
	mci->csrows[row].channels[channel].ce_count++;

	if (mci->scrub_mode & SCRUB_SW_SRC) {
		/*
		 * Some MC's can remap memory so that it is still available
		 * at a different address when PCI devices map into memory.
		 * MC's that can't do this lose the memory where PCI devices
		 * are mapped.  This mapping is MC dependant and so we call
		 * back into the MC driver for it to map the MC page to
		 * a physical (CPU) page which can then be mapped to a virtual
		 * page - which can then be scrubbed.
		 */
		remapped_page = mci->ctl_page_to_phys ?
			mci->ctl_page_to_phys(mci, page_frame_number) :
			page_frame_number;

		edac_mc_scrub_block(remapped_page, offset_in_page,
				mci->csrows[row].grain);
	}
}
EXPORT_SYMBOL_GPL(edac_mc_handle_ce);

void edac_mc_handle_ce_no_info(struct mem_ctl_info *mci, const char *msg)
{
	if (edac_mc_get_log_ce())
		edac_mc_printk(mci, KERN_WARNING,
			"CE - no information available: %s\n", msg);

	mci->ce_noinfo_count++;
	mci->ce_count++;
}
EXPORT_SYMBOL_GPL(edac_mc_handle_ce_no_info);

void edac_mc_handle_ue(struct mem_ctl_info *mci,
		unsigned long page_frame_number,
		unsigned long offset_in_page, int row, const char *msg)
{
	int len = EDAC_MC_LABEL_LEN * 4;
	char labels[len + 1];
	char *pos = labels;
	int chan;
	int chars;

	debugf3("MC%d: %s()\n", mci->mc_idx, __func__);

	/* FIXME - maybe make panic on INTERNAL ERROR an option */
	if (row >= mci->nr_csrows || row < 0) {
		/* something is wrong */
		edac_mc_printk(mci, KERN_ERR,
			"INTERNAL ERROR: row out of range "
			"(%d >= %d)\n", row, mci->nr_csrows);
		edac_mc_handle_ue_no_info(mci, "INTERNAL ERROR");
		return;
	}

	chars = snprintf(pos, len + 1, "%s",
			 mci->csrows[row].channels[0].label);
	len -= chars;
	pos += chars;

	for (chan = 1; (chan < mci->csrows[row].nr_channels) && (len > 0);
		chan++) {
		chars = snprintf(pos, len + 1, ":%s",
				 mci->csrows[row].channels[chan].label);
		len -= chars;
		pos += chars;
	}

	if (edac_mc_get_log_ue())
		edac_mc_printk(mci, KERN_EMERG,
			"UE page 0x%lx, offset 0x%lx, grain %d, row %d, "
			"labels \"%s\": %s\n", page_frame_number,
			offset_in_page, mci->csrows[row].grain, row,
			labels, msg);

	if (edac_mc_get_panic_on_ue())
		panic("EDAC MC%d: UE page 0x%lx, offset 0x%lx, grain %d, "
			"row %d, labels \"%s\": %s\n", mci->mc_idx,
			page_frame_number, offset_in_page,
			mci->csrows[row].grain, row, labels, msg);

	mci->ue_count++;
	mci->csrows[row].ue_count++;
}
EXPORT_SYMBOL_GPL(edac_mc_handle_ue);

void edac_mc_handle_ue_no_info(struct mem_ctl_info *mci, const char *msg)
{
	if (edac_mc_get_panic_on_ue())
		panic("EDAC MC%d: Uncorrected Error", mci->mc_idx);

	if (edac_mc_get_log_ue())
		edac_mc_printk(mci, KERN_WARNING,
			"UE - no information available: %s\n", msg);
	mci->ue_noinfo_count++;
	mci->ue_count++;
}
EXPORT_SYMBOL_GPL(edac_mc_handle_ue_no_info);

/*************************************************************
 * On Fully Buffered DIMM modules, this help function is
 * called to process UE events
 */
void edac_mc_handle_fbd_ue(struct mem_ctl_info *mci,
			unsigned int csrow,
			unsigned int channela,
			unsigned int channelb, char *msg)
{
	int len = EDAC_MC_LABEL_LEN * 4;
	char labels[len + 1];
	char *pos = labels;
	int chars;

	if (csrow >= mci->nr_csrows) {
		/* something is wrong */
		edac_mc_printk(mci, KERN_ERR,
			"INTERNAL ERROR: row out of range (%d >= %d)\n",
			csrow, mci->nr_csrows);
		edac_mc_handle_ue_no_info(mci, "INTERNAL ERROR");
		return;
	}

	if (channela >= mci->csrows[csrow].nr_channels) {
		/* something is wrong */
		edac_mc_printk(mci, KERN_ERR,
			"INTERNAL ERROR: channel-a out of range "
			"(%d >= %d)\n",
			channela, mci->csrows[csrow].nr_channels);
		edac_mc_handle_ue_no_info(mci, "INTERNAL ERROR");
		return;
	}

	if (channelb >= mci->csrows[csrow].nr_channels) {
		/* something is wrong */
		edac_mc_printk(mci, KERN_ERR,
			"INTERNAL ERROR: channel-b out of range "
			"(%d >= %d)\n",
			channelb, mci->csrows[csrow].nr_channels);
		edac_mc_handle_ue_no_info(mci, "INTERNAL ERROR");
		return;
	}

	mci->ue_count++;
	mci->csrows[csrow].ue_count++;

	/* Generate the DIMM labels from the specified channels */
	chars = snprintf(pos, len + 1, "%s",
			 mci->csrows[csrow].channels[channela].label);
	len -= chars;
	pos += chars;
	chars = snprintf(pos, len + 1, "-%s",
			 mci->csrows[csrow].channels[channelb].label);

	if (edac_mc_get_log_ue())
		edac_mc_printk(mci, KERN_EMERG,
			"UE row %d, channel-a= %d channel-b= %d "
			"labels \"%s\": %s\n", csrow, channela, channelb,
			labels, msg);

	if (edac_mc_get_panic_on_ue())
		panic("UE row %d, channel-a= %d channel-b= %d "
			"labels \"%s\": %s\n", csrow, channela,
			channelb, labels, msg);
}
EXPORT_SYMBOL(edac_mc_handle_fbd_ue);

/*************************************************************
 * On Fully Buffered DIMM modules, this help function is
 * called to process CE events
 */
void edac_mc_handle_fbd_ce(struct mem_ctl_info *mci,
			unsigned int csrow, unsigned int channel, char *msg)
{

	/* Ensure boundary values */
	if (csrow >= mci->nr_csrows) {
		/* something is wrong */
		edac_mc_printk(mci, KERN_ERR,
			"INTERNAL ERROR: row out of range (%d >= %d)\n",
			csrow, mci->nr_csrows);
		edac_mc_handle_ce_no_info(mci, "INTERNAL ERROR");
		return;
	}
	if (channel >= mci->csrows[csrow].nr_channels) {
		/* something is wrong */
		edac_mc_printk(mci, KERN_ERR,
			"INTERNAL ERROR: channel out of range (%d >= %d)\n",
			channel, mci->csrows[csrow].nr_channels);
		edac_mc_handle_ce_no_info(mci, "INTERNAL ERROR");
		return;
	}

	if (edac_mc_get_log_ce())
		/* FIXME - put in DIMM location */
		edac_mc_printk(mci, KERN_WARNING,
			"CE row %d, channel %d, label \"%s\": %s\n",
			csrow, channel,
			mci->csrows[csrow].channels[channel].label, msg);

	mci->ce_count++;
	mci->csrows[csrow].ce_count++;
	mci->csrows[csrow].channels[channel].ce_count++;
}
EXPORT_SYMBOL(edac_mc_handle_fbd_ce);
