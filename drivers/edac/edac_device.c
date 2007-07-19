
/*
 * edac_device.c
 * (C) 2007 www.douglaskthompson.com
 *
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * Written by Doug Thompson <norsk5@xmission.com>
 *
 * edac_device API implementation
 * 19 Jan 2007
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/sysctl.h>
#include <linux/highmem.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/sysdev.h>
#include <linux/ctype.h>
#include <linux/workqueue.h>
#include <asm/uaccess.h>
#include <asm/page.h>

#include "edac_core.h"
#include "edac_module.h"

/* lock to memory controller's control array */
static DECLARE_MUTEX(device_ctls_mutex);
static struct list_head edac_device_list = LIST_HEAD_INIT(edac_device_list);


static inline void lock_device_list(void)
{
	down(&device_ctls_mutex);
}

static inline void unlock_device_list(void)
{
	up(&device_ctls_mutex);
}


#ifdef CONFIG_EDAC_DEBUG
static void edac_device_dump_device(struct edac_device_ctl_info *edac_dev)
{
	debugf3("\tedac_dev = %p dev_idx=%d \n", edac_dev,edac_dev->dev_idx);
	debugf4("\tedac_dev->edac_check = %p\n", edac_dev->edac_check);
	debugf3("\tdev = %p\n", edac_dev->dev);
	debugf3("\tmod_name:ctl_name = %s:%s\n",
		edac_dev->mod_name, edac_dev->ctl_name);
	debugf3("\tpvt_info = %p\n\n", edac_dev->pvt_info);
}
#endif  /* CONFIG_EDAC_DEBUG */

/*
 * The alloc() and free() functions for the 'edac_device' control info
 * structure. A MC driver will allocate one of these for each edac_device
 * it is going to control/register with the EDAC CORE.
 */
struct edac_device_ctl_info *edac_device_alloc_ctl_info(
	unsigned sz_private,
	char *edac_device_name,
	unsigned nr_instances,
	char *edac_block_name,
	unsigned nr_blocks,
	unsigned offset_value,
	struct edac_attrib_spec *attrib_spec,
	unsigned nr_attribs)
{
	struct edac_device_ctl_info *dev_ctl;
	struct edac_device_instance *dev_inst, *inst;
	struct edac_device_block *dev_blk, *blk_p, *blk;
	struct edac_attrib *dev_attrib, *attrib_p, *attrib;
	unsigned total_size;
	unsigned count;
	unsigned instance, block, attr;
	void *pvt;

	debugf1("%s() instances=%d blocks=%d\n",
		__func__,nr_instances,nr_blocks);

	/* Figure out the offsets of the various items from the start of an
	 * ctl_info structure.  We want the alignment of each item
	 * to be at least as stringent as what the compiler would
	 * provide if we could simply hardcode everything into a single struct.
	 */
	dev_ctl = (struct edac_device_ctl_info *) 0;

	/* Calc the 'end' offset past the ctl_info structure */
	dev_inst = (struct edac_device_instance *)
			edac_align_ptr(&dev_ctl[1],sizeof(*dev_inst));

	/* Calc the 'end' offset past the instance array */
	dev_blk = (struct edac_device_block *)
			edac_align_ptr(&dev_inst[nr_instances],sizeof(*dev_blk));

	/* Calc the 'end' offset past the dev_blk array */
	count = nr_instances * nr_blocks;
	dev_attrib = (struct edac_attrib *)
			edac_align_ptr(&dev_blk[count],sizeof(*dev_attrib));

	/* Check for case of NO attributes specified */
	if (nr_attribs > 0)
		count *= nr_attribs;

	/* Calc the 'end' offset past the attributes array */
	pvt = edac_align_ptr(&dev_attrib[count],sz_private);
	total_size = ((unsigned long) pvt) + sz_private;

	/* Allocate the amount of memory for the set of control structures */
	if ((dev_ctl = kmalloc(total_size, GFP_KERNEL)) == NULL)
		return NULL;

	/* Adjust pointers so they point within the memory we just allocated
	 * rather than an imaginary chunk of memory located at address 0.
	 */
	dev_inst = (struct edac_device_instance *)
			(((char *) dev_ctl) + ((unsigned long) dev_inst));
	dev_blk = (struct edac_device_block *)
			(((char *) dev_ctl) + ((unsigned long) dev_blk));
	dev_attrib = (struct edac_attrib *)
			(((char *) dev_ctl) + ((unsigned long) dev_attrib));
	pvt = sz_private ?
			(((char *) dev_ctl) + ((unsigned long) pvt)) : NULL;

	memset(dev_ctl, 0, total_size);		/* clear all fields */
	dev_ctl->nr_instances = nr_instances;
	dev_ctl->instances = dev_inst;
	dev_ctl->pvt_info = pvt;

	/* Name of this edac device, ensure null terminated */
	snprintf(dev_ctl->name,sizeof(dev_ctl->name),"%s", edac_device_name);
	dev_ctl->name[sizeof(dev_ctl->name)-1] = '\0';

	/* Initialize every Instance */
	for (instance = 0; instance < nr_instances; instance++) {
		inst = &dev_inst[instance];
		inst->ctl = dev_ctl;
		inst->nr_blocks = nr_blocks;
		blk_p = &dev_blk[instance * nr_blocks];
		inst->blocks = blk_p;

		/* name of this instance */
		snprintf(inst->name, sizeof(inst->name),
			"%s%u", edac_device_name, instance);
		inst->name[sizeof(inst->name)-1] = '\0';

		/* Initialize every block in each instance */
		for (		block = 0;
				block < nr_blocks;
				block++) {
			blk = &blk_p[block];
			blk->instance = inst;
			blk->nr_attribs = nr_attribs;
			attrib_p = &dev_attrib[block * nr_attribs];
			blk->attribs = attrib_p;
			snprintf(blk->name, sizeof(blk->name),
				"%s%d", edac_block_name,block+1);
			blk->name[sizeof(blk->name)-1] = '\0';

			debugf1("%s() instance=%d block=%d name=%s\n",
				__func__, instance,block,blk->name);

			if (attrib_spec != NULL) {
				/* when there is an attrib_spec passed int then
				 * Initialize every attrib of each block
				 */
				for (attr = 0; attr < nr_attribs; attr++) {
					attrib = &attrib_p[attr];
					attrib->block = blk;

					/* Link each attribute to the caller's
					 * spec entry, for name and type
				 	 */
					attrib->spec = &attrib_spec[attr];
				}
			}
		}
	}

	/* Mark this instance as merely ALLOCATED */
	dev_ctl->op_state = OP_ALLOC;

	return dev_ctl;
}
EXPORT_SYMBOL_GPL(edac_device_alloc_ctl_info);

/*
 * edac_device_free_ctl_info()
 *	frees the memory allocated by the edac_device_alloc_ctl_info()
 *	function
 */
void edac_device_free_ctl_info( struct edac_device_ctl_info *ctl_info) {
	kfree(ctl_info);
}
EXPORT_SYMBOL_GPL(edac_device_free_ctl_info);



/*
 * find_edac_device_by_dev
 *	scans the edac_device list for a specific 'struct device *'
 */
static struct edac_device_ctl_info *
find_edac_device_by_dev(struct device *dev)
{
	struct edac_device_ctl_info *edac_dev;
	struct list_head *item;

	debugf3("%s()\n", __func__);

	list_for_each(item, &edac_device_list) {
		edac_dev = list_entry(item, struct edac_device_ctl_info, link);

		if (edac_dev->dev == dev)
			return edac_dev;
	}

	return NULL;
}

/*
 * add_edac_dev_to_global_list
 *	Before calling this function, caller must
 *	assign a unique value to edac_dev->dev_idx.
 *	Return:
 *		0 on success
 *		1 on failure.
 */
static int add_edac_dev_to_global_list (struct edac_device_ctl_info *edac_dev)
{
	struct list_head *item, *insert_before;
	struct edac_device_ctl_info *rover;

	insert_before = &edac_device_list;

	/* Determine if already on the list */
	if (unlikely((rover = find_edac_device_by_dev(edac_dev->dev)) != NULL))
		goto fail0;

	/* Insert in ascending order by 'dev_idx', so find position */
	list_for_each(item, &edac_device_list) {
		rover = list_entry(item, struct edac_device_ctl_info, link);

		if (rover->dev_idx >= edac_dev->dev_idx) {
			if (unlikely(rover->dev_idx == edac_dev->dev_idx))
				goto fail1;

			insert_before = item;
			break;
		}
	}

	list_add_tail_rcu(&edac_dev->link, insert_before);
	return 0;

fail0:
	edac_printk(KERN_WARNING, EDAC_MC,
		"%s (%s) %s %s already assigned %d\n",
		rover->dev->bus_id, dev_name(rover),
		rover->mod_name, rover->ctl_name, rover->dev_idx);
	return 1;

fail1:
	edac_printk(KERN_WARNING, EDAC_MC,
		"bug in low-level driver: attempt to assign\n"
		"    duplicate dev_idx %d in %s()\n", rover->dev_idx, __func__);
	return 1;
}

/*
 * complete_edac_device_list_del
 */
static void complete_edac_device_list_del(struct rcu_head *head)
{
	struct edac_device_ctl_info *edac_dev;

	edac_dev = container_of(head, struct edac_device_ctl_info, rcu);
	INIT_LIST_HEAD(&edac_dev->link);
	complete(&edac_dev->complete);
}

/*
 * del_edac_device_from_global_list
 */
static void del_edac_device_from_global_list(
			struct edac_device_ctl_info *edac_device)
{
	list_del_rcu(&edac_device->link);
	init_completion(&edac_device->complete);
	call_rcu(&edac_device->rcu, complete_edac_device_list_del);
	wait_for_completion(&edac_device->complete);
}

/**
 * edac_device_find
 *	Search for a edac_device_ctl_info structure whose index is 'idx'.
 *
 * If found, return a pointer to the structure.
 * Else return NULL.
 *
 * Caller must hold device_ctls_mutex.
 */
struct edac_device_ctl_info * edac_device_find(int idx)
{
	struct list_head *item;
	struct edac_device_ctl_info *edac_dev;

	/* Iterate over list, looking for exact match of ID */
	list_for_each(item, &edac_device_list) {
		edac_dev = list_entry(item, struct edac_device_ctl_info, link);

		if (edac_dev->dev_idx >= idx) {
			if (edac_dev->dev_idx == idx)
				return edac_dev;

			/* not on list, so terminate early */
			break;
		}
	}

	return NULL;
}
EXPORT_SYMBOL(edac_device_find);


/*
 * edac_device_workq_function
 *	performs the operation scheduled by a workq request
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
static void edac_device_workq_function(struct work_struct *work_req)
{
	struct delayed_work *d_work = (struct delayed_work*) work_req;
	struct edac_device_ctl_info *edac_dev =
		to_edac_device_ctl_work(d_work);
#else
static void edac_device_workq_function(void *ptr)
{
	struct edac_device_ctl_info *edac_dev =
		(struct edac_device_ctl_info *) ptr;
#endif

	//debugf0("%s() here and running\n", __func__);
	lock_device_list();

	/* Only poll controllers that are running polled and have a check */
	if ((edac_dev->op_state == OP_RUNNING_POLL) &&
					(edac_dev->edac_check != NULL)) {
		edac_dev->edac_check(edac_dev);
	}

	unlock_device_list();

	/* Reschedule */
	queue_delayed_work(edac_workqueue,&edac_dev->work, edac_dev->delay);
}

/*
 * edac_device_workq_setup
 *	initialize a workq item for this edac_device instance
 *	passing in the new delay period in msec
 */
void edac_device_workq_setup(struct edac_device_ctl_info *edac_dev,
		unsigned msec)
{
	debugf0("%s()\n", __func__);

	edac_dev->poll_msec = msec;
	edac_calc_delay(edac_dev);	/* Calc delay jiffies */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
	INIT_DELAYED_WORK(&edac_dev->work, edac_device_workq_function);
#else
	INIT_WORK(&edac_dev->work, edac_device_workq_function, edac_dev);
#endif
	queue_delayed_work(edac_workqueue, &edac_dev->work, edac_dev->delay);
}

/*
 * edac_device_workq_teardown
 *	stop the workq processing on this edac_dev
 */
void edac_device_workq_teardown(struct edac_device_ctl_info *edac_dev)
{
	int status;

	status = cancel_delayed_work(&edac_dev->work);
	if (status == 0) {
		/* workq instance might be running, wait for it */
		flush_workqueue(edac_workqueue);
	}
}

/*
 * edac_device_reset_delay_period
 */

void edac_device_reset_delay_period(
		struct edac_device_ctl_info *edac_dev,
		unsigned long value)
{
	lock_device_list();

	/* cancel the current workq request */
	edac_device_workq_teardown(edac_dev);

	/* restart the workq request, with new delay value */
	edac_device_workq_setup(edac_dev, value);

	unlock_device_list();
}

/**
 * edac_device_add_device: Insert the 'edac_dev' structure into the
 * edac_device global list and create sysfs entries associated with
 * edac_device structure.
 * @edac_device: pointer to the edac_device structure to be added to the list
 * @edac_idx: A unique numeric identifier to be assigned to the
 * 'edac_device' structure.
 *
 * Return:
 *	0	Success
 *	!0	Failure
 */
int edac_device_add_device(struct edac_device_ctl_info *edac_dev, int edac_idx)
{
	debugf0("%s()\n", __func__);

	edac_dev->dev_idx = edac_idx;
#ifdef CONFIG_EDAC_DEBUG
	if (edac_debug_level >= 3)
		edac_device_dump_device(edac_dev);
#endif
	lock_device_list();

	if (add_edac_dev_to_global_list(edac_dev))
		goto fail0;

	/* set load time so that error rate can be tracked */
	edac_dev->start_time = jiffies;

	/* create this instance's sysfs entries */
	if (edac_device_create_sysfs(edac_dev)) {
		edac_device_printk(edac_dev, KERN_WARNING,
			"failed to create sysfs device\n");
		goto fail1;
	}

	/* If there IS a check routine, then we are running POLLED */
	if (edac_dev->edac_check != NULL) {
		/* This instance is NOW RUNNING */
		edac_dev->op_state = OP_RUNNING_POLL;

		/*
		 * enable workq processing on this instance,
		 * default = 1000 msec
		 */
		edac_device_workq_setup(edac_dev, 1000);
	} else {
		edac_dev->op_state = OP_RUNNING_INTERRUPT;
	}


	/* Report action taken */
	edac_device_printk(edac_dev, KERN_INFO,
		"Giving out device to module '%s' controller '%s': DEV '%s' (%s)\n",
		edac_dev->mod_name,
		edac_dev->ctl_name,
		dev_name(edac_dev),
		edac_op_state_toString(edac_dev->op_state)
		);

	unlock_device_list();
	return 0;

fail1:
	/* Some error, so remove the entry from the lsit */
	del_edac_device_from_global_list(edac_dev);

fail0:
	unlock_device_list();
	return 1;
}
EXPORT_SYMBOL_GPL(edac_device_add_device);

/**
 * edac_device_del_device:
 *	Remove sysfs entries for specified edac_device structure and
 *	then remove edac_device structure from global list
 *
 * @pdev:
 *	Pointer to 'struct device' representing edac_device
 *	structure to remove.
 *
 * Return:
 *	Pointer to removed edac_device structure,
 *	OR NULL if device not found.
 */
struct edac_device_ctl_info * edac_device_del_device(struct device *dev)
{
	struct edac_device_ctl_info *edac_dev;

	debugf0("MC: %s()\n", __func__);

	lock_device_list();

	if ((edac_dev = find_edac_device_by_dev(dev)) == NULL) {
		unlock_device_list();
		return NULL;
	}

	/* mark this instance as OFFLINE */
	edac_dev->op_state = OP_OFFLINE;

	/* clear workq processing on this instance */
	edac_device_workq_teardown(edac_dev);

	/* Tear down the sysfs entries for this instance */
	edac_device_remove_sysfs(edac_dev);

	/* deregister from global list */
	del_edac_device_from_global_list(edac_dev);

	unlock_device_list();

	edac_printk(KERN_INFO, EDAC_MC,
		"Removed device %d for %s %s: DEV %s\n",
		edac_dev->dev_idx,
		edac_dev->mod_name,
		edac_dev->ctl_name,
		dev_name(edac_dev));

	return edac_dev;
}
EXPORT_SYMBOL_GPL(edac_device_del_device);


static inline int edac_device_get_log_ce(struct edac_device_ctl_info *edac_dev)
{
	return edac_dev->log_ce;
}

static inline int edac_device_get_log_ue(struct edac_device_ctl_info *edac_dev)
{
	return edac_dev->log_ue;
}

static inline int edac_device_get_panic_on_ue(
		struct edac_device_ctl_info *edac_dev)
{
	return edac_dev->panic_on_ue;
}

/*
 * edac_device_handle_ce
 *	perform a common output and handling of an 'edac_dev' CE event
 */
void edac_device_handle_ce(struct edac_device_ctl_info *edac_dev,
		int inst_nr, int block_nr, const char *msg)
{
	struct edac_device_instance *instance;
	struct edac_device_block *block = NULL;

	if ((inst_nr >= edac_dev->nr_instances) || (inst_nr < 0)) {
		edac_device_printk(edac_dev, KERN_ERR,
			"INTERNAL ERROR: 'instance' out of range "
			"(%d >= %d)\n", inst_nr, edac_dev->nr_instances);
		return;
	}

	instance = edac_dev->instances + inst_nr;

	if ((block_nr >= instance->nr_blocks) || (block_nr < 0)) {
		edac_device_printk(edac_dev, KERN_ERR,
			"INTERNAL ERROR: instance %d 'block' out of range "
			"(%d >= %d)\n", inst_nr, block_nr, instance->nr_blocks);
		return;
	}

	if (instance->nr_blocks > 0) {
		block = instance->blocks + block_nr;
		block->counters.ce_count++;
	}

	/* Propogate the count up the 'totals' tree */
	instance->counters.ce_count++;
	edac_dev->counters.ce_count++;

	if (edac_device_get_log_ce(edac_dev))
		edac_device_printk(edac_dev, KERN_WARNING,
		"CE ctl: %s, instance: %s, block: %s: %s\n",
		edac_dev->ctl_name, instance->name,
		block ? block->name : "N/A", msg);
}
EXPORT_SYMBOL_GPL(edac_device_handle_ce);

/*
 * edac_device_handle_ue
 *	perform a common output and handling of an 'edac_dev' UE event
 */
void edac_device_handle_ue(struct edac_device_ctl_info *edac_dev,
		int inst_nr, int block_nr, const char *msg)
{
	struct edac_device_instance *instance;
	struct edac_device_block *block = NULL;

	if ((inst_nr >= edac_dev->nr_instances) || (inst_nr < 0)) {
		edac_device_printk(edac_dev, KERN_ERR,
			"INTERNAL ERROR: 'instance' out of range "
			"(%d >= %d)\n", inst_nr, edac_dev->nr_instances);
		return;
	}

	instance = edac_dev->instances + inst_nr;

	if ((block_nr >= instance->nr_blocks) || (block_nr < 0)) {
		edac_device_printk(edac_dev, KERN_ERR,
			"INTERNAL ERROR: instance %d 'block' out of range "
			"(%d >= %d)\n", inst_nr, block_nr, instance->nr_blocks);
		return;
	}

	if (instance->nr_blocks > 0) {
		block = instance->blocks + block_nr;
		block->counters.ue_count++;
	}

	/* Propogate the count up the 'totals' tree */
	instance->counters.ue_count++;
	edac_dev->counters.ue_count++;

	if (edac_device_get_log_ue(edac_dev))
		edac_device_printk(edac_dev, KERN_EMERG,
		"UE ctl: %s, instance: %s, block: %s: %s\n",
		edac_dev->ctl_name, instance->name,
		block ? block->name : "N/A", msg);

	if (edac_device_get_panic_on_ue(edac_dev))
		panic("EDAC %s: UE instance: %s, block %s: %s\n",
			edac_dev->ctl_name, instance->name,
			block ? block->name : "N/A", msg);
}
EXPORT_SYMBOL_GPL(edac_device_handle_ue);

