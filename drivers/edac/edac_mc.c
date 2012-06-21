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

static void edac_mc_dump_channel(struct rank_info *chan)
{
	debugf4("\tchannel = %p\n", chan);
	debugf4("\tchannel->chan_idx = %d\n", chan->chan_idx);
	debugf4("\tchannel->csrow = %p\n\n", chan->csrow);
	debugf4("\tchannel->dimm = %p\n", chan->dimm);
}

static void edac_mc_dump_dimm(struct dimm_info *dimm)
{
	int i;

	debugf4("\tdimm = %p\n", dimm);
	debugf4("\tdimm->label = '%s'\n", dimm->label);
	debugf4("\tdimm->nr_pages = 0x%x\n", dimm->nr_pages);
	debugf4("\tdimm location ");
	for (i = 0; i < dimm->mci->n_layers; i++) {
		printk(KERN_CONT "%d", dimm->location[i]);
		if (i < dimm->mci->n_layers - 1)
			printk(KERN_CONT ".");
	}
	printk(KERN_CONT "\n");
	debugf4("\tdimm->grain = %d\n", dimm->grain);
	debugf4("\tdimm->nr_pages = 0x%x\n", dimm->nr_pages);
}

static void edac_mc_dump_csrow(struct csrow_info *csrow)
{
	debugf4("\tcsrow = %p\n", csrow);
	debugf4("\tcsrow->csrow_idx = %d\n", csrow->csrow_idx);
	debugf4("\tcsrow->first_page = 0x%lx\n", csrow->first_page);
	debugf4("\tcsrow->last_page = 0x%lx\n", csrow->last_page);
	debugf4("\tcsrow->page_mask = 0x%lx\n", csrow->page_mask);
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
	debugf3("\tmci->nr_dimms = %d, dimms = %p\n",
		mci->tot_dimms, mci->dimms);
	debugf3("\tdev = %p\n", mci->dev);
	debugf3("\tmod_name:ctl_name = %s:%s\n", mci->mod_name, mci->ctl_name);
	debugf3("\tpvt_info = %p\n\n", mci->pvt_info);
}

#endif				/* CONFIG_EDAC_DEBUG */

/*
 * keep those in sync with the enum mem_type
 */
const char *edac_mem_types[] = {
	"Empty csrow",
	"Reserved csrow type",
	"Unknown csrow type",
	"Fast page mode RAM",
	"Extended data out RAM",
	"Burst Extended data out RAM",
	"Single data rate SDRAM",
	"Registered single data rate SDRAM",
	"Double data rate SDRAM",
	"Registered Double data rate SDRAM",
	"Rambus DRAM",
	"Unbuffered DDR2 RAM",
	"Fully buffered DDR2",
	"Registered DDR2 RAM",
	"Rambus XDR",
	"Unbuffered DDR3 RAM",
	"Registered DDR3 RAM",
};
EXPORT_SYMBOL_GPL(edac_mem_types);

/**
 * edac_align_ptr - Prepares the pointer offsets for a single-shot allocation
 * @p:		pointer to a pointer with the memory offset to be used. At
 *		return, this will be incremented to point to the next offset
 * @size:	Size of the data structure to be reserved
 * @n_elems:	Number of elements that should be reserved
 *
 * If 'size' is a constant, the compiler will optimize this whole function
 * down to either a no-op or the addition of a constant to the value of '*p'.
 *
 * The 'p' pointer is absolutely needed to keep the proper advancing
 * further in memory to the proper offsets when allocating the struct along
 * with its embedded structs, as edac_device_alloc_ctl_info() does it
 * above, for example.
 *
 * At return, the pointer 'p' will be incremented to be used on a next call
 * to this function.
 */
void *edac_align_ptr(void **p, unsigned size, int n_elems)
{
	unsigned align, r;
	void *ptr = *p;

	*p += size * n_elems;

	/*
	 * 'p' can possibly be an unaligned item X such that sizeof(X) is
	 * 'size'.  Adjust 'p' so that its alignment is at least as
	 * stringent as what the compiler would provide for X and return
	 * the aligned result.
	 * Here we assume that the alignment of a "long long" is the most
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

	r = (unsigned long)p % align;

	if (r == 0)
		return (char *)ptr;

	*p += align - r;

	return (void *)(((unsigned long)ptr) + align - r);
}

/**
 * edac_mc_alloc: Allocate and partially fill a struct mem_ctl_info structure
 * @mc_num:		Memory controller number
 * @n_layers:		Number of MC hierarchy layers
 * layers:		Describes each layer as seen by the Memory Controller
 * @size_pvt:		size of private storage needed
 *
 *
 * Everything is kmalloc'ed as one big chunk - more efficient.
 * Only can be used if all structures have the same lifetime - otherwise
 * you have to allocate and initialize your own structures.
 *
 * Use edac_mc_free() to free mc structures allocated by this function.
 *
 * NOTE: drivers handle multi-rank memories in different ways: in some
 * drivers, one multi-rank memory stick is mapped as one entry, while, in
 * others, a single multi-rank memory stick would be mapped into several
 * entries. Currently, this function will allocate multiple struct dimm_info
 * on such scenarios, as grouping the multiple ranks require drivers change.
 *
 * Returns:
 *	On failure: NULL
 *	On success: struct mem_ctl_info pointer
 */
struct mem_ctl_info *edac_mc_alloc(unsigned mc_num,
				   unsigned n_layers,
				   struct edac_mc_layer *layers,
				   unsigned sz_pvt)
{
	struct mem_ctl_info *mci;
	struct edac_mc_layer *layer;
	struct csrow_info *csi, *csr;
	struct rank_info *chi, *chp, *chan;
	struct dimm_info *dimm;
	u32 *ce_per_layer[EDAC_MAX_LAYERS], *ue_per_layer[EDAC_MAX_LAYERS];
	unsigned pos[EDAC_MAX_LAYERS];
	unsigned size, tot_dimms = 1, count = 1;
	unsigned tot_csrows = 1, tot_channels = 1, tot_errcount = 0;
	void *pvt, *p, *ptr = NULL;
	int i, j, err, row, chn, n, len;
	bool per_rank = false;

	BUG_ON(n_layers > EDAC_MAX_LAYERS || n_layers == 0);
	/*
	 * Calculate the total amount of dimms and csrows/cschannels while
	 * in the old API emulation mode
	 */
	for (i = 0; i < n_layers; i++) {
		tot_dimms *= layers[i].size;
		if (layers[i].is_virt_csrow)
			tot_csrows *= layers[i].size;
		else
			tot_channels *= layers[i].size;

		if (layers[i].type == EDAC_MC_LAYER_CHIP_SELECT)
			per_rank = true;
	}

	/* Figure out the offsets of the various items from the start of an mc
	 * structure.  We want the alignment of each item to be at least as
	 * stringent as what the compiler would provide if we could simply
	 * hardcode everything into a single struct.
	 */
	mci = edac_align_ptr(&ptr, sizeof(*mci), 1);
	layer = edac_align_ptr(&ptr, sizeof(*layer), n_layers);
	csi = edac_align_ptr(&ptr, sizeof(*csi), tot_csrows);
	chi = edac_align_ptr(&ptr, sizeof(*chi), tot_csrows * tot_channels);
	dimm = edac_align_ptr(&ptr, sizeof(*dimm), tot_dimms);
	for (i = 0; i < n_layers; i++) {
		count *= layers[i].size;
		debugf4("%s: errcount layer %d size %d\n", __func__, i, count);
		ce_per_layer[i] = edac_align_ptr(&ptr, sizeof(u32), count);
		ue_per_layer[i] = edac_align_ptr(&ptr, sizeof(u32), count);
		tot_errcount += 2 * count;
	}

	debugf4("%s: allocating %d error counters\n", __func__, tot_errcount);
	pvt = edac_align_ptr(&ptr, sz_pvt, 1);
	size = ((unsigned long)pvt) + sz_pvt;

	debugf1("%s(): allocating %u bytes for mci data (%d %s, %d csrows/channels)\n",
		__func__, size,
		tot_dimms,
		per_rank ? "ranks" : "dimms",
		tot_csrows * tot_channels);
	mci = kzalloc(size, GFP_KERNEL);
	if (mci == NULL)
		return NULL;

	/* Adjust pointers so they point within the memory we just allocated
	 * rather than an imaginary chunk of memory located at address 0.
	 */
	layer = (struct edac_mc_layer *)(((char *)mci) + ((unsigned long)layer));
	csi = (struct csrow_info *)(((char *)mci) + ((unsigned long)csi));
	chi = (struct rank_info *)(((char *)mci) + ((unsigned long)chi));
	dimm = (struct dimm_info *)(((char *)mci) + ((unsigned long)dimm));
	for (i = 0; i < n_layers; i++) {
		mci->ce_per_layer[i] = (u32 *)((char *)mci + ((unsigned long)ce_per_layer[i]));
		mci->ue_per_layer[i] = (u32 *)((char *)mci + ((unsigned long)ue_per_layer[i]));
	}
	pvt = sz_pvt ? (((char *)mci) + ((unsigned long)pvt)) : NULL;

	/* setup index and various internal pointers */
	mci->mc_idx = mc_num;
	mci->csrows = csi;
	mci->dimms  = dimm;
	mci->tot_dimms = tot_dimms;
	mci->pvt_info = pvt;
	mci->n_layers = n_layers;
	mci->layers = layer;
	memcpy(mci->layers, layers, sizeof(*layer) * n_layers);
	mci->nr_csrows = tot_csrows;
	mci->num_cschannel = tot_channels;
	mci->mem_is_per_rank = per_rank;

	/*
	 * Fill the csrow struct
	 */
	for (row = 0; row < tot_csrows; row++) {
		csr = &csi[row];
		csr->csrow_idx = row;
		csr->mci = mci;
		csr->nr_channels = tot_channels;
		chp = &chi[row * tot_channels];
		csr->channels = chp;

		for (chn = 0; chn < tot_channels; chn++) {
			chan = &chp[chn];
			chan->chan_idx = chn;
			chan->csrow = csr;
		}
	}

	/*
	 * Fill the dimm struct
	 */
	memset(&pos, 0, sizeof(pos));
	row = 0;
	chn = 0;
	debugf4("%s: initializing %d %s\n", __func__, tot_dimms,
		per_rank ? "ranks" : "dimms");
	for (i = 0; i < tot_dimms; i++) {
		chan = &csi[row].channels[chn];
		dimm = EDAC_DIMM_PTR(layer, mci->dimms, n_layers,
			       pos[0], pos[1], pos[2]);
		dimm->mci = mci;

		debugf2("%s: %d: %s%zd (%d:%d:%d): row %d, chan %d\n", __func__,
			i, per_rank ? "rank" : "dimm", (dimm - mci->dimms),
			pos[0], pos[1], pos[2], row, chn);

		/*
		 * Copy DIMM location and initialize it.
		 */
		len = sizeof(dimm->label);
		p = dimm->label;
		n = snprintf(p, len, "mc#%u", mc_num);
		p += n;
		len -= n;
		for (j = 0; j < n_layers; j++) {
			n = snprintf(p, len, "%s#%u",
				     edac_layer_name[layers[j].type],
				     pos[j]);
			p += n;
			len -= n;
			dimm->location[j] = pos[j];

			if (len <= 0)
				break;
		}

		/* Link it to the csrows old API data */
		chan->dimm = dimm;
		dimm->csrow = row;
		dimm->cschannel = chn;

		/* Increment csrow location */
		row++;
		if (row == tot_csrows) {
			row = 0;
			chn++;
		}

		/* Increment dimm location */
		for (j = n_layers - 1; j >= 0; j--) {
			pos[j]++;
			if (pos[j] < layers[j].size)
				break;
			pos[j] = 0;
		}
	}

	mci->op_state = OP_ALLOC;
	INIT_LIST_HEAD(&mci->grp_kobj_list);

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
	debugf1("%s()\n", __func__);

	edac_mc_unregister_sysfs_main_kobj(mci);

	/* free the mci instance memory here */
	kfree(mci);
}
EXPORT_SYMBOL_GPL(edac_mc_free);


/**
 * find_mci_by_dev
 *
 *	scan list of controllers looking for the one that manages
 *	the 'dev' device
 * @dev: pointer to a struct device related with the MCI
 */
struct mem_ctl_info *find_mci_by_dev(struct device *dev)
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
EXPORT_SYMBOL_GPL(find_mci_by_dev);

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
	struct delayed_work *d_work = to_delayed_work(work_req);
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

	if (mci->op_state != OP_RUNNING_POLL)
		return;

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
		"%s (%s) %s %s already assigned %d\n", dev_name(p->dev),
		edac_dev_name(mci), p->mod_name, p->ctl_name, p->mc_idx);
	return 1;

fail1:
	edac_printk(KERN_WARNING, EDAC_MC,
		"bug in low-level driver: attempt to assign\n"
		"    duplicate mc_idx %d in %s()\n", p->mc_idx, __func__);
	return 1;
}

static void del_mc_from_global_list(struct mem_ctl_info *mci)
{
	atomic_dec(&edac_handlers);
	list_del_rcu(&mci->link);

	/* these are for safe removal of devices from global list while
	 * NMI handlers may be traversing list
	 */
	synchronize_rcu();
	INIT_LIST_HEAD(&mci->link);
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
		for (i = 0; i < mci->tot_dimms; i++)
			edac_mc_dump_dimm(&mci->dimms[i]);
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

	del_mc_from_global_list(mci);
	mutex_unlock(&mem_ctls_mutex);

	/* flush workq processes */
	edac_mc_workq_teardown(mci);

	/* marking MCI offline */
	mci->op_state = OP_OFFLINE;

	/* remove from sysfs */
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

	virt_addr = kmap_atomic(pg);

	/* Perform architecture specific atomic scrub operation */
	atomic_scrub(virt_addr + offset, size);

	/* Unmap and complete */
	kunmap_atomic(virt_addr);

	if (PageHighMem(pg))
		local_irq_restore(flags);
}

/* FIXME - should return -1 */
int edac_mc_find_csrow_by_page(struct mem_ctl_info *mci, unsigned long page)
{
	struct csrow_info *csrows = mci->csrows;
	int row, i, j, n;

	debugf1("MC%d: %s(): 0x%lx\n", mci->mc_idx, __func__, page);
	row = -1;

	for (i = 0; i < mci->nr_csrows; i++) {
		struct csrow_info *csrow = &csrows[i];
		n = 0;
		for (j = 0; j < csrow->nr_channels; j++) {
			struct dimm_info *dimm = csrow->channels[j].dimm;
			n += dimm->nr_pages;
		}
		if (n == 0)
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

const char *edac_layer_name[] = {
	[EDAC_MC_LAYER_BRANCH] = "branch",
	[EDAC_MC_LAYER_CHANNEL] = "channel",
	[EDAC_MC_LAYER_SLOT] = "slot",
	[EDAC_MC_LAYER_CHIP_SELECT] = "csrow",
};
EXPORT_SYMBOL_GPL(edac_layer_name);

static void edac_inc_ce_error(struct mem_ctl_info *mci,
				    bool enable_per_layer_report,
				    const int pos[EDAC_MAX_LAYERS])
{
	int i, index = 0;

	mci->ce_mc++;

	if (!enable_per_layer_report) {
		mci->ce_noinfo_count++;
		return;
	}

	for (i = 0; i < mci->n_layers; i++) {
		if (pos[i] < 0)
			break;
		index += pos[i];
		mci->ce_per_layer[i][index]++;

		if (i < mci->n_layers - 1)
			index *= mci->layers[i + 1].size;
	}
}

static void edac_inc_ue_error(struct mem_ctl_info *mci,
				    bool enable_per_layer_report,
				    const int pos[EDAC_MAX_LAYERS])
{
	int i, index = 0;

	mci->ue_mc++;

	if (!enable_per_layer_report) {
		mci->ce_noinfo_count++;
		return;
	}

	for (i = 0; i < mci->n_layers; i++) {
		if (pos[i] < 0)
			break;
		index += pos[i];
		mci->ue_per_layer[i][index]++;

		if (i < mci->n_layers - 1)
			index *= mci->layers[i + 1].size;
	}
}

static void edac_ce_error(struct mem_ctl_info *mci,
			  const int pos[EDAC_MAX_LAYERS],
			  const char *msg,
			  const char *location,
			  const char *label,
			  const char *detail,
			  const char *other_detail,
			  const bool enable_per_layer_report,
			  const unsigned long page_frame_number,
			  const unsigned long offset_in_page,
			  u32 grain)
{
	unsigned long remapped_page;

	if (edac_mc_get_log_ce()) {
		if (other_detail && *other_detail)
			edac_mc_printk(mci, KERN_WARNING,
				       "CE %s on %s (%s%s - %s)\n",
				       msg, label, location,
				       detail, other_detail);
		else
			edac_mc_printk(mci, KERN_WARNING,
				       "CE %s on %s (%s%s)\n",
				       msg, label, location,
				       detail);
	}
	edac_inc_ce_error(mci, enable_per_layer_report, pos);

	if (mci->scrub_mode & SCRUB_SW_SRC) {
		/*
			* Some memory controllers (called MCs below) can remap
			* memory so that it is still available at a different
			* address when PCI devices map into memory.
			* MC's that can't do this, lose the memory where PCI
			* devices are mapped. This mapping is MC-dependent
			* and so we call back into the MC driver for it to
			* map the MC page to a physical (CPU) page which can
			* then be mapped to a virtual page - which can then
			* be scrubbed.
			*/
		remapped_page = mci->ctl_page_to_phys ?
			mci->ctl_page_to_phys(mci, page_frame_number) :
			page_frame_number;

		edac_mc_scrub_block(remapped_page,
					offset_in_page, grain);
	}
}

static void edac_ue_error(struct mem_ctl_info *mci,
			  const int pos[EDAC_MAX_LAYERS],
			  const char *msg,
			  const char *location,
			  const char *label,
			  const char *detail,
			  const char *other_detail,
			  const bool enable_per_layer_report)
{
	if (edac_mc_get_log_ue()) {
		if (other_detail && *other_detail)
			edac_mc_printk(mci, KERN_WARNING,
				       "UE %s on %s (%s%s - %s)\n",
			               msg, label, location, detail,
				       other_detail);
		else
			edac_mc_printk(mci, KERN_WARNING,
				       "UE %s on %s (%s%s)\n",
			               msg, label, location, detail);
	}

	if (edac_mc_get_panic_on_ue()) {
		if (other_detail && *other_detail)
			panic("UE %s on %s (%s%s - %s)\n",
			      msg, label, location, detail, other_detail);
		else
			panic("UE %s on %s (%s%s)\n",
			      msg, label, location, detail);
	}

	edac_inc_ue_error(mci, enable_per_layer_report, pos);
}

#define OTHER_LABEL " or "
void edac_mc_handle_error(const enum hw_event_mc_err_type type,
			  struct mem_ctl_info *mci,
			  const unsigned long page_frame_number,
			  const unsigned long offset_in_page,
			  const unsigned long syndrome,
			  const int layer0,
			  const int layer1,
			  const int layer2,
			  const char *msg,
			  const char *other_detail,
			  const void *mcelog)
{
	/* FIXME: too much for stack: move it to some pre-alocated area */
	char detail[80], location[80];
	char label[(EDAC_MC_LABEL_LEN + 1 + sizeof(OTHER_LABEL)) * mci->tot_dimms];
	char *p;
	int row = -1, chan = -1;
	int pos[EDAC_MAX_LAYERS] = { layer0, layer1, layer2 };
	int i;
	u32 grain;
	bool enable_per_layer_report = false;

	debugf3("MC%d: %s()\n", mci->mc_idx, __func__);

	/*
	 * Check if the event report is consistent and if the memory
	 * location is known. If it is known, enable_per_layer_report will be
	 * true, the DIMM(s) label info will be filled and the per-layer
	 * error counters will be incremented.
	 */
	for (i = 0; i < mci->n_layers; i++) {
		if (pos[i] >= (int)mci->layers[i].size) {
			if (type == HW_EVENT_ERR_CORRECTED)
				p = "CE";
			else
				p = "UE";

			edac_mc_printk(mci, KERN_ERR,
				       "INTERNAL ERROR: %s value is out of range (%d >= %d)\n",
				       edac_layer_name[mci->layers[i].type],
				       pos[i], mci->layers[i].size);
			/*
			 * Instead of just returning it, let's use what's
			 * known about the error. The increment routines and
			 * the DIMM filter logic will do the right thing by
			 * pointing the likely damaged DIMMs.
			 */
			pos[i] = -1;
		}
		if (pos[i] >= 0)
			enable_per_layer_report = true;
	}

	/*
	 * Get the dimm label/grain that applies to the match criteria.
	 * As the error algorithm may not be able to point to just one memory
	 * stick, the logic here will get all possible labels that could
	 * pottentially be affected by the error.
	 * On FB-DIMM memory controllers, for uncorrected errors, it is common
	 * to have only the MC channel and the MC dimm (also called "branch")
	 * but the channel is not known, as the memory is arranged in pairs,
	 * where each memory belongs to a separate channel within the same
	 * branch.
	 */
	grain = 0;
	p = label;
	*p = '\0';
	for (i = 0; i < mci->tot_dimms; i++) {
		struct dimm_info *dimm = &mci->dimms[i];

		if (layer0 >= 0 && layer0 != dimm->location[0])
			continue;
		if (layer1 >= 0 && layer1 != dimm->location[1])
			continue;
		if (layer2 >= 0 && layer2 != dimm->location[2])
			continue;

		/* get the max grain, over the error match range */
		if (dimm->grain > grain)
			grain = dimm->grain;

		/*
		 * If the error is memory-controller wide, there's no need to
		 * seek for the affected DIMMs because the whole
		 * channel/memory controller/...  may be affected.
		 * Also, don't show errors for empty DIMM slots.
		 */
		if (enable_per_layer_report && dimm->nr_pages) {
			if (p != label) {
				strcpy(p, OTHER_LABEL);
				p += strlen(OTHER_LABEL);
			}
			strcpy(p, dimm->label);
			p += strlen(p);
			*p = '\0';

			/*
			 * get csrow/channel of the DIMM, in order to allow
			 * incrementing the compat API counters
			 */
			debugf4("%s: %s csrows map: (%d,%d)\n",
				__func__,
				mci->mem_is_per_rank ? "rank" : "dimm",
				dimm->csrow, dimm->cschannel);

			if (row == -1)
				row = dimm->csrow;
			else if (row >= 0 && row != dimm->csrow)
				row = -2;

			if (chan == -1)
				chan = dimm->cschannel;
			else if (chan >= 0 && chan != dimm->cschannel)
				chan = -2;
		}
	}

	if (!enable_per_layer_report) {
		strcpy(label, "any memory");
	} else {
		debugf4("%s: csrow/channel to increment: (%d,%d)\n",
			__func__, row, chan);
		if (p == label)
			strcpy(label, "unknown memory");
		if (type == HW_EVENT_ERR_CORRECTED) {
			if (row >= 0) {
				mci->csrows[row].ce_count++;
				if (chan >= 0)
					mci->csrows[row].channels[chan].ce_count++;
			}
		} else
			if (row >= 0)
				mci->csrows[row].ue_count++;
	}

	/* Fill the RAM location data */
	p = location;
	for (i = 0; i < mci->n_layers; i++) {
		if (pos[i] < 0)
			continue;

		p += sprintf(p, "%s:%d ",
			     edac_layer_name[mci->layers[i].type],
			     pos[i]);
	}

	/* Memory type dependent details about the error */
	if (type == HW_EVENT_ERR_CORRECTED) {
		snprintf(detail, sizeof(detail),
			"page:0x%lx offset:0x%lx grain:%d syndrome:0x%lx",
			page_frame_number, offset_in_page,
			grain, syndrome);
		edac_ce_error(mci, pos, msg, location, label, detail,
			      other_detail, enable_per_layer_report,
			      page_frame_number, offset_in_page, grain);
	} else {
		snprintf(detail, sizeof(detail),
			"page:0x%lx offset:0x%lx grain:%d",
			page_frame_number, offset_in_page, grain);

		edac_ue_error(mci, pos, msg, location, label, detail,
			      other_detail, enable_per_layer_report);
	}
}
EXPORT_SYMBOL_GPL(edac_mc_handle_error);
