
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

#include <asm/page.h>
#include <linux/uaccess.h>
#include <linux/ctype.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/sysctl.h>
#include <linux/timer.h>

#include "edac_device.h"
#include "edac_module.h"

/* lock for the list: 'edac_device_list', manipulation of this list
 * is protected by the 'device_ctls_mutex' lock
 */
static DEFINE_MUTEX(device_ctls_mutex);
static LIST_HEAD(edac_device_list);

#ifdef CONFIG_EDAC_DEBUG
static void edac_device_dump_device(struct edac_device_ctl_info *edac_dev)
{
	edac_dbg(3, "\tedac_dev = %p dev_idx=%d\n",
		 edac_dev, edac_dev->dev_idx);
	edac_dbg(4, "\tedac_dev->edac_check = %p\n", edac_dev->edac_check);
	edac_dbg(3, "\tdev = %p\n", edac_dev->dev);
	edac_dbg(3, "\tmod_name:ctl_name = %s:%s\n",
		 edac_dev->mod_name, edac_dev->ctl_name);
	edac_dbg(3, "\tpvt_info = %p\n\n", edac_dev->pvt_info);
}
#endif				/* CONFIG_EDAC_DEBUG */

/*
 * @off_val: zero, 1, or other based offset
 */
struct edac_device_ctl_info *
edac_device_alloc_ctl_info(unsigned pvt_sz, char *dev_name, unsigned nr_instances,
			   char *blk_name, unsigned nr_blocks, unsigned off_val,
			   struct edac_dev_sysfs_block_attribute *attrib_spec,
			   unsigned nr_attrib, int device_index)
{
	struct edac_dev_sysfs_block_attribute *dev_attrib, *attrib_p, *attrib;
	struct edac_device_block *dev_blk, *blk_p, *blk;
	struct edac_device_instance *dev_inst, *inst;
	struct edac_device_ctl_info *dev_ctl;
	unsigned instance, block, attr;
	void *pvt;
	int err;

	edac_dbg(4, "instances=%d blocks=%d\n", nr_instances, nr_blocks);

	dev_ctl = kzalloc(sizeof(struct edac_device_ctl_info), GFP_KERNEL);
	if (!dev_ctl)
		return NULL;

	dev_inst = kcalloc(nr_instances, sizeof(struct edac_device_instance), GFP_KERNEL);
	if (!dev_inst)
		goto free;

	dev_ctl->instances = dev_inst;

	dev_blk = kcalloc(nr_instances * nr_blocks, sizeof(struct edac_device_block), GFP_KERNEL);
	if (!dev_blk)
		goto free;

	dev_ctl->blocks = dev_blk;

	if (nr_attrib) {
		dev_attrib = kcalloc(nr_attrib, sizeof(struct edac_dev_sysfs_block_attribute),
				     GFP_KERNEL);
		if (!dev_attrib)
			goto free;

		dev_ctl->attribs = dev_attrib;
	}

	if (pvt_sz) {
		pvt = kzalloc(pvt_sz, GFP_KERNEL);
		if (!pvt)
			goto free;

		dev_ctl->pvt_info = pvt;
	}

	dev_ctl->dev_idx	= device_index;
	dev_ctl->nr_instances	= nr_instances;

	/* Default logging of CEs and UEs */
	dev_ctl->log_ce = 1;
	dev_ctl->log_ue = 1;

	/* Name of this edac device */
	snprintf(dev_ctl->name, sizeof(dev_ctl->name),"%s", dev_name);

	/* Initialize every Instance */
	for (instance = 0; instance < nr_instances; instance++) {
		inst = &dev_inst[instance];
		inst->ctl = dev_ctl;
		inst->nr_blocks = nr_blocks;
		blk_p = &dev_blk[instance * nr_blocks];
		inst->blocks = blk_p;

		/* name of this instance */
		snprintf(inst->name, sizeof(inst->name), "%s%u", dev_name, instance);

		/* Initialize every block in each instance */
		for (block = 0; block < nr_blocks; block++) {
			blk = &blk_p[block];
			blk->instance = inst;
			snprintf(blk->name, sizeof(blk->name),
				 "%s%d", blk_name, block + off_val);

			edac_dbg(4, "instance=%d inst_p=%p block=#%d block_p=%p name='%s'\n",
				 instance, inst, block, blk, blk->name);

			/* if there are NO attributes OR no attribute pointer
			 * then continue on to next block iteration
			 */
			if ((nr_attrib == 0) || (attrib_spec == NULL))
				continue;

			/* setup the attribute array for this block */
			blk->nr_attribs = nr_attrib;
			attrib_p = &dev_attrib[block*nr_instances*nr_attrib];
			blk->block_attributes = attrib_p;

			edac_dbg(4, "THIS BLOCK_ATTRIB=%p\n",
				 blk->block_attributes);

			/* Initialize every user specified attribute in this
			 * block with the data the caller passed in
			 * Each block gets its own copy of pointers,
			 * and its unique 'value'
			 */
			for (attr = 0; attr < nr_attrib; attr++) {
				attrib = &attrib_p[attr];

				/* populate the unique per attrib
				 * with the code pointers and info
				 */
				attrib->attr = attrib_spec[attr].attr;
				attrib->show = attrib_spec[attr].show;
				attrib->store = attrib_spec[attr].store;

				attrib->block = blk;	/* up link */

				edac_dbg(4, "alloc-attrib=%p attrib_name='%s' attrib-spec=%p spec-name=%s\n",
					 attrib, attrib->attr.name,
					 &attrib_spec[attr],
					 attrib_spec[attr].attr.name
					);
			}
		}
	}

	/* Mark this instance as merely ALLOCATED */
	dev_ctl->op_state = OP_ALLOC;

	/*
	 * Initialize the 'root' kobj for the edac_device controller
	 */
	err = edac_device_register_sysfs_main_kobj(dev_ctl);
	if (err)
		goto free;

	/* at this point, the root kobj is valid, and in order to
	 * 'free' the object, then the function:
	 *	edac_device_unregister_sysfs_main_kobj() must be called
	 * which will perform kobj unregistration and the actual free
	 * will occur during the kobject callback operation
	 */

	return dev_ctl;

free:
	__edac_device_free_ctl_info(dev_ctl);

	return NULL;
}
EXPORT_SYMBOL_GPL(edac_device_alloc_ctl_info);

void edac_device_free_ctl_info(struct edac_device_ctl_info *ctl_info)
{
	edac_device_unregister_sysfs_main_kobj(ctl_info);
}
EXPORT_SYMBOL_GPL(edac_device_free_ctl_info);

/*
 * find_edac_device_by_dev
 *	scans the edac_device list for a specific 'struct device *'
 *
 *	lock to be held prior to call:	device_ctls_mutex
 *
 *	Return:
 *		pointer to control structure managing 'dev'
 *		NULL if not found on list
 */
static struct edac_device_ctl_info *find_edac_device_by_dev(struct device *dev)
{
	struct edac_device_ctl_info *edac_dev;
	struct list_head *item;

	edac_dbg(0, "\n");

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
 *
 *	lock to be held prior to call:	device_ctls_mutex
 *
 *	Return:
 *		0 on success
 *		1 on failure.
 */
static int add_edac_dev_to_global_list(struct edac_device_ctl_info *edac_dev)
{
	struct list_head *item, *insert_before;
	struct edac_device_ctl_info *rover;

	insert_before = &edac_device_list;

	/* Determine if already on the list */
	rover = find_edac_device_by_dev(edac_dev->dev);
	if (unlikely(rover != NULL))
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
			dev_name(rover->dev), edac_dev_name(rover),
			rover->mod_name, rover->ctl_name, rover->dev_idx);
	return 1;

fail1:
	edac_printk(KERN_WARNING, EDAC_MC,
			"bug in low-level driver: attempt to assign\n"
			"    duplicate dev_idx %d in %s()\n", rover->dev_idx,
			__func__);
	return 1;
}

/*
 * del_edac_device_from_global_list
 */
static void del_edac_device_from_global_list(struct edac_device_ctl_info
						*edac_device)
{
	list_del_rcu(&edac_device->link);

	/* these are for safe removal of devices from global list while
	 * NMI handlers may be traversing list
	 */
	synchronize_rcu();
	INIT_LIST_HEAD(&edac_device->link);
}

/*
 * edac_device_workq_function
 *	performs the operation scheduled by a workq request
 *
 *	this workq is embedded within an edac_device_ctl_info
 *	structure, that needs to be polled for possible error events.
 *
 *	This operation is to acquire the list mutex lock
 *	(thus preventing insertation or deletion)
 *	and then call the device's poll function IFF this device is
 *	running polled and there is a poll function defined.
 */
static void edac_device_workq_function(struct work_struct *work_req)
{
	struct delayed_work *d_work = to_delayed_work(work_req);
	struct edac_device_ctl_info *edac_dev = to_edac_device_ctl_work(d_work);

	mutex_lock(&device_ctls_mutex);

	/* If we are being removed, bail out immediately */
	if (edac_dev->op_state == OP_OFFLINE) {
		mutex_unlock(&device_ctls_mutex);
		return;
	}

	/* Only poll controllers that are running polled and have a check */
	if ((edac_dev->op_state == OP_RUNNING_POLL) &&
		(edac_dev->edac_check != NULL)) {
			edac_dev->edac_check(edac_dev);
	}

	mutex_unlock(&device_ctls_mutex);

	/* Reschedule the workq for the next time period to start again
	 * if the number of msec is for 1 sec, then adjust to the next
	 * whole one second to save timers firing all over the period
	 * between integral seconds
	 */
	if (edac_dev->poll_msec == 1000)
		edac_queue_work(&edac_dev->work, round_jiffies_relative(edac_dev->delay));
	else
		edac_queue_work(&edac_dev->work, edac_dev->delay);
}

/*
 * edac_device_workq_setup
 *	initialize a workq item for this edac_device instance
 *	passing in the new delay period in msec
 */
static void edac_device_workq_setup(struct edac_device_ctl_info *edac_dev,
				    unsigned msec)
{
	edac_dbg(0, "\n");

	/* take the arg 'msec' and set it into the control structure
	 * to used in the time period calculation
	 * then calc the number of jiffies that represents
	 */
	edac_dev->poll_msec = msec;
	edac_dev->delay = msecs_to_jiffies(msec);

	INIT_DELAYED_WORK(&edac_dev->work, edac_device_workq_function);

	/* optimize here for the 1 second case, which will be normal value, to
	 * fire ON the 1 second time event. This helps reduce all sorts of
	 * timers firing on sub-second basis, while they are happy
	 * to fire together on the 1 second exactly
	 */
	if (edac_dev->poll_msec == 1000)
		edac_queue_work(&edac_dev->work, round_jiffies_relative(edac_dev->delay));
	else
		edac_queue_work(&edac_dev->work, edac_dev->delay);
}

/*
 * edac_device_workq_teardown
 *	stop the workq processing on this edac_dev
 */
static void edac_device_workq_teardown(struct edac_device_ctl_info *edac_dev)
{
	if (!edac_dev->edac_check)
		return;

	edac_dev->op_state = OP_OFFLINE;

	edac_stop_work(&edac_dev->work);
}

/*
 * edac_device_reset_delay_period
 *
 *	need to stop any outstanding workq queued up at this time
 *	because we will be resetting the sleep time.
 *	Then restart the workq on the new delay
 */
void edac_device_reset_delay_period(struct edac_device_ctl_info *edac_dev,
				    unsigned long msec)
{
	edac_dev->poll_msec = msec;
	edac_dev->delay	    = msecs_to_jiffies(msec);

	/* See comment in edac_device_workq_setup() above */
	if (edac_dev->poll_msec == 1000)
		edac_mod_work(&edac_dev->work, round_jiffies_relative(edac_dev->delay));
	else
		edac_mod_work(&edac_dev->work, edac_dev->delay);
}

int edac_device_alloc_index(void)
{
	static atomic_t device_indexes = ATOMIC_INIT(0);

	return atomic_inc_return(&device_indexes) - 1;
}
EXPORT_SYMBOL_GPL(edac_device_alloc_index);

int edac_device_add_device(struct edac_device_ctl_info *edac_dev)
{
	edac_dbg(0, "\n");

#ifdef CONFIG_EDAC_DEBUG
	if (edac_debug_level >= 3)
		edac_device_dump_device(edac_dev);
#endif
	mutex_lock(&device_ctls_mutex);

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
		"Giving out device to module %s controller %s: DEV %s (%s)\n",
		edac_dev->mod_name, edac_dev->ctl_name, edac_dev->dev_name,
		edac_op_state_to_string(edac_dev->op_state));

	mutex_unlock(&device_ctls_mutex);
	return 0;

fail1:
	/* Some error, so remove the entry from the lsit */
	del_edac_device_from_global_list(edac_dev);

fail0:
	mutex_unlock(&device_ctls_mutex);
	return 1;
}
EXPORT_SYMBOL_GPL(edac_device_add_device);

struct edac_device_ctl_info *edac_device_del_device(struct device *dev)
{
	struct edac_device_ctl_info *edac_dev;

	edac_dbg(0, "\n");

	mutex_lock(&device_ctls_mutex);

	/* Find the structure on the list, if not there, then leave */
	edac_dev = find_edac_device_by_dev(dev);
	if (edac_dev == NULL) {
		mutex_unlock(&device_ctls_mutex);
		return NULL;
	}

	/* mark this instance as OFFLINE */
	edac_dev->op_state = OP_OFFLINE;

	/* deregister from global list */
	del_edac_device_from_global_list(edac_dev);

	mutex_unlock(&device_ctls_mutex);

	/* clear workq processing on this instance */
	edac_device_workq_teardown(edac_dev);

	/* Tear down the sysfs entries for this instance */
	edac_device_remove_sysfs(edac_dev);

	edac_printk(KERN_INFO, EDAC_MC,
		"Removed device %d for %s %s: DEV %s\n",
		edac_dev->dev_idx,
		edac_dev->mod_name, edac_dev->ctl_name, edac_dev_name(edac_dev));

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

static inline int edac_device_get_panic_on_ue(struct edac_device_ctl_info
					*edac_dev)
{
	return edac_dev->panic_on_ue;
}

void edac_device_handle_ce_count(struct edac_device_ctl_info *edac_dev,
				 unsigned int count, int inst_nr, int block_nr,
				 const char *msg)
{
	struct edac_device_instance *instance;
	struct edac_device_block *block = NULL;

	if (!count)
		return;

	if ((inst_nr >= edac_dev->nr_instances) || (inst_nr < 0)) {
		edac_device_printk(edac_dev, KERN_ERR,
				"INTERNAL ERROR: 'instance' out of range "
				"(%d >= %d)\n", inst_nr,
				edac_dev->nr_instances);
		return;
	}

	instance = edac_dev->instances + inst_nr;

	if ((block_nr >= instance->nr_blocks) || (block_nr < 0)) {
		edac_device_printk(edac_dev, KERN_ERR,
				"INTERNAL ERROR: instance %d 'block' "
				"out of range (%d >= %d)\n",
				inst_nr, block_nr,
				instance->nr_blocks);
		return;
	}

	if (instance->nr_blocks > 0) {
		block = instance->blocks + block_nr;
		block->counters.ce_count += count;
	}

	/* Propagate the count up the 'totals' tree */
	instance->counters.ce_count += count;
	edac_dev->counters.ce_count += count;

	if (edac_device_get_log_ce(edac_dev))
		edac_device_printk(edac_dev, KERN_WARNING,
				   "CE: %s instance: %s block: %s count: %d '%s'\n",
				   edac_dev->ctl_name, instance->name,
				   block ? block->name : "N/A", count, msg);
}
EXPORT_SYMBOL_GPL(edac_device_handle_ce_count);

void edac_device_handle_ue_count(struct edac_device_ctl_info *edac_dev,
				 unsigned int count, int inst_nr, int block_nr,
				 const char *msg)
{
	struct edac_device_instance *instance;
	struct edac_device_block *block = NULL;

	if (!count)
		return;

	if ((inst_nr >= edac_dev->nr_instances) || (inst_nr < 0)) {
		edac_device_printk(edac_dev, KERN_ERR,
				"INTERNAL ERROR: 'instance' out of range "
				"(%d >= %d)\n", inst_nr,
				edac_dev->nr_instances);
		return;
	}

	instance = edac_dev->instances + inst_nr;

	if ((block_nr >= instance->nr_blocks) || (block_nr < 0)) {
		edac_device_printk(edac_dev, KERN_ERR,
				"INTERNAL ERROR: instance %d 'block' "
				"out of range (%d >= %d)\n",
				inst_nr, block_nr,
				instance->nr_blocks);
		return;
	}

	if (instance->nr_blocks > 0) {
		block = instance->blocks + block_nr;
		block->counters.ue_count += count;
	}

	/* Propagate the count up the 'totals' tree */
	instance->counters.ue_count += count;
	edac_dev->counters.ue_count += count;

	if (edac_device_get_log_ue(edac_dev))
		edac_device_printk(edac_dev, KERN_EMERG,
				   "UE: %s instance: %s block: %s count: %d '%s'\n",
				   edac_dev->ctl_name, instance->name,
				   block ? block->name : "N/A", count, msg);

	if (edac_device_get_panic_on_ue(edac_dev))
		panic("EDAC %s: UE instance: %s block %s count: %d '%s'\n",
		      edac_dev->ctl_name, instance->name,
		      block ? block->name : "N/A", count, msg);
}
EXPORT_SYMBOL_GPL(edac_device_handle_ue_count);
