/*
 * Defines, structures, APIs for edac_device
 *
 * (C) 2007 Linux Networx (http://lnxi.com)
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * Written by Thayne Harbaugh
 * Based on work by Dan Hollis <goemon at anime dot net> and others.
 *	http://www.anime.net/~goemon/linux-ecc/
 *
 * NMI handling support added by
 *     Dave Peterson <dsp@llnl.gov> <dave_peterson@pobox.com>
 *
 * Refactored for multi-source files:
 *	Doug Thompson <norsk5@xmission.com>
 *
 * Please look at Documentation/driver-api/edac.rst for more info about
 * EDAC core structs and functions.
 */

#ifndef _EDAC_DEVICE_H_
#define _EDAC_DEVICE_H_

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/edac.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>


/*
 * The following are the structures to provide for a generic
 * or abstract 'edac_device'. This set of structures and the
 * code that implements the APIs for the same, provide for
 * registering EDAC type devices which are NOT standard memory.
 *
 * CPU caches (L1 and L2)
 * DMA engines
 * Core CPU switches
 * Fabric switch units
 * PCIe interface controllers
 * other EDAC/ECC type devices that can be monitored for
 * errors, etc.
 *
 * It allows for a 2 level set of hierarchy. For example:
 *
 * cache could be composed of L1, L2 and L3 levels of cache.
 * Each CPU core would have its own L1 cache, while sharing
 * L2 and maybe L3 caches.
 *
 * View them arranged, via the sysfs presentation:
 * /sys/devices/system/edac/..
 *
 *	mc/		<existing memory device directory>
 *	cpu/cpu0/..	<L1 and L2 block directory>
 *		/L1-cache/ce_count
 *			 /ue_count
 *		/L2-cache/ce_count
 *			 /ue_count
 *	cpu/cpu1/..	<L1 and L2 block directory>
 *		/L1-cache/ce_count
 *			 /ue_count
 *		/L2-cache/ce_count
 *			 /ue_count
 *	...
 *
 *	the L1 and L2 directories would be "edac_device_block's"
 */

struct edac_device_counter {
	u32 ue_count;
	u32 ce_count;
};

/* forward reference */
struct edac_device_ctl_info;
struct edac_device_block;

/* edac_dev_sysfs_attribute structure
 *	used for driver sysfs attributes in mem_ctl_info
 *	for extra controls and attributes:
 *		like high level error Injection controls
 */
struct edac_dev_sysfs_attribute {
	struct attribute attr;
	ssize_t (*show)(struct edac_device_ctl_info *, char *);
	ssize_t (*store)(struct edac_device_ctl_info *, const char *, size_t);
};

/* edac_dev_sysfs_block_attribute structure
 *
 *	used in leaf 'block' nodes for adding controls/attributes
 *
 *	each block in each instance of the containing control structure
 *	can have an array of the following. The show and store functions
 *	will be filled in with the show/store function in the
 *	low level driver.
 *
 *	The 'value' field will be the actual value field used for
 *	counting
 */
struct edac_dev_sysfs_block_attribute {
	struct attribute attr;
	ssize_t (*show)(struct kobject *, struct attribute *, char *);
	ssize_t (*store)(struct kobject *, struct attribute *,
			const char *, size_t);
	struct edac_device_block *block;

	unsigned int value;
};

/* device block control structure */
struct edac_device_block {
	struct edac_device_instance *instance;	/* Up Pointer */
	char name[EDAC_DEVICE_NAME_LEN + 1];

	struct edac_device_counter counters;	/* basic UE and CE counters */

	int nr_attribs;		/* how many attributes */

	/* this block's attributes, could be NULL */
	struct edac_dev_sysfs_block_attribute *block_attributes;

	/* edac sysfs device control */
	struct kobject kobj;
};

/* device instance control structure */
struct edac_device_instance {
	struct edac_device_ctl_info *ctl;	/* Up pointer */
	char name[EDAC_DEVICE_NAME_LEN + 4];

	struct edac_device_counter counters;	/* instance counters */

	u32 nr_blocks;		/* how many blocks */
	struct edac_device_block *blocks;	/* block array */

	/* edac sysfs device control */
	struct kobject kobj;
};


/*
 * Abstract edac_device control info structure
 *
 */
struct edac_device_ctl_info {
	/* for global list of edac_device_ctl_info structs */
	struct list_head link;

	struct module *owner;	/* Module owner of this control struct */

	int dev_idx;

	/* Per instance controls for this edac_device */
	int log_ue;		/* boolean for logging UEs */
	int log_ce;		/* boolean for logging CEs */
	int panic_on_ue;	/* boolean for panic'ing on an UE */
	unsigned poll_msec;	/* number of milliseconds to poll interval */
	unsigned long delay;	/* number of jiffies for poll_msec */

	/* Additional top controller level attributes, but specified
	 * by the low level driver.
	 *
	 * Set by the low level driver to provide attributes at the
	 * controller level, same level as 'ue_count' and 'ce_count' above.
	 * An array of structures, NULL terminated
	 *
	 * If attributes are desired, then set to array of attributes
	 * If no attributes are desired, leave NULL
	 */
	struct edac_dev_sysfs_attribute *sysfs_attributes;

	/* pointer to main 'edac' subsys in sysfs */
	struct bus_type *edac_subsys;

	/* the internal state of this controller instance */
	int op_state;
	/* work struct for this instance */
	struct delayed_work work;

	/* pointer to edac polling checking routine:
	 *      If NOT NULL: points to polling check routine
	 *      If NULL: Then assumes INTERRUPT operation, where
	 *              MC driver will receive events
	 */
	void (*edac_check) (struct edac_device_ctl_info * edac_dev);

	struct device *dev;	/* pointer to device structure */

	const char *mod_name;	/* module name */
	const char *ctl_name;	/* edac controller  name */
	const char *dev_name;	/* pci/platform/etc... name */

	void *pvt_info;		/* pointer to 'private driver' info */

	unsigned long start_time;	/* edac_device load start time (jiffies) */

	struct completion removal_complete;

	/* sysfs top name under 'edac' directory
	 * and instance name:
	 *      cpu/cpu0/...
	 *      cpu/cpu1/...
	 *      cpu/cpu2/...
	 *      ...
	 */
	char name[EDAC_DEVICE_NAME_LEN + 1];

	/* Number of instances supported on this control structure
	 * and the array of those instances
	 */
	u32 nr_instances;
	struct edac_device_instance *instances;

	/* Event counters for the this whole EDAC Device */
	struct edac_device_counter counters;

	/* edac sysfs device control for the 'name'
	 * device this structure controls
	 */
	struct kobject kobj;
};

/* To get from the instance's wq to the beginning of the ctl structure */
#define to_edac_mem_ctl_work(w) \
		container_of(w, struct mem_ctl_info, work)

#define to_edac_device_ctl_work(w) \
		container_of(w,struct edac_device_ctl_info,work)

/*
 * The alloc() and free() functions for the 'edac_device' control info
 * structure. A MC driver will allocate one of these for each edac_device
 * it is going to control/register with the EDAC CORE.
 */
extern struct edac_device_ctl_info *edac_device_alloc_ctl_info(
		unsigned sizeof_private,
		char *edac_device_name, unsigned nr_instances,
		char *edac_block_name, unsigned nr_blocks,
		unsigned offset_value,
		struct edac_dev_sysfs_block_attribute *block_attributes,
		unsigned nr_attribs,
		int device_index);

/* The offset value can be:
 *	-1 indicating no offset value
 *	0 for zero-based block numbers
 *	1 for 1-based block number
 *	other for other-based block number
 */
#define	BLOCK_OFFSET_VALUE_OFF	((unsigned) -1)

extern void edac_device_free_ctl_info(struct edac_device_ctl_info *ctl_info);

/**
 * edac_device_add_device - Insert the 'edac_dev' structure into the
 *	 edac_device global list and create sysfs entries associated with
 *	 edac_device structure.
 *
 * @edac_dev: pointer to edac_device structure to be added to the list
 *	'edac_device' structure.
 *
 * Returns:
 *	0 on Success, or an error code on failure
 */
extern int edac_device_add_device(struct edac_device_ctl_info *edac_dev);

/**
 * edac_device_del_device - Remove sysfs entries for specified edac_device
 *	structure and then remove edac_device structure from global list
 *
 * @dev:
 *	Pointer to struct &device representing the edac device
 *	structure to remove.
 *
 * Returns:
 *	Pointer to removed edac_device structure,
 *	or %NULL if device not found.
 */
extern struct edac_device_ctl_info *edac_device_del_device(struct device *dev);

/**
 * edac_device_handle_ce_count - Log correctable errors.
 *
 * @edac_dev: pointer to struct &edac_device_ctl_info
 * @inst_nr: number of the instance where the CE error happened
 * @count: Number of errors to log.
 * @block_nr: number of the block where the CE error happened
 * @msg: message to be printed
 */
void edac_device_handle_ce_count(struct edac_device_ctl_info *edac_dev,
				 unsigned int count, int inst_nr, int block_nr,
				 const char *msg);

/**
 * edac_device_handle_ue_count - Log uncorrectable errors.
 *
 * @edac_dev: pointer to struct &edac_device_ctl_info
 * @inst_nr: number of the instance where the CE error happened
 * @count: Number of errors to log.
 * @block_nr: number of the block where the CE error happened
 * @msg: message to be printed
 */
void edac_device_handle_ue_count(struct edac_device_ctl_info *edac_dev,
				 unsigned int count, int inst_nr, int block_nr,
				 const char *msg);

/**
 * edac_device_handle_ce(): Log a single correctable error
 *
 * @edac_dev: pointer to struct &edac_device_ctl_info
 * @inst_nr: number of the instance where the CE error happened
 * @block_nr: number of the block where the CE error happened
 * @msg: message to be printed
 */
static inline void
edac_device_handle_ce(struct edac_device_ctl_info *edac_dev, int inst_nr,
		      int block_nr, const char *msg)
{
	edac_device_handle_ce_count(edac_dev, 1, inst_nr, block_nr, msg);
}

/**
 * edac_device_handle_ue(): Log a single uncorrectable error
 *
 * @edac_dev: pointer to struct &edac_device_ctl_info
 * @inst_nr: number of the instance where the UE error happened
 * @block_nr: number of the block where the UE error happened
 * @msg: message to be printed
 */
static inline void
edac_device_handle_ue(struct edac_device_ctl_info *edac_dev, int inst_nr,
		      int block_nr, const char *msg)
{
	edac_device_handle_ue_count(edac_dev, 1, inst_nr, block_nr, msg);
}

/**
 * edac_device_alloc_index: Allocate a unique device index number
 *
 * Returns:
 *	allocated index number
 */
extern int edac_device_alloc_index(void);
extern const char *edac_layer_name[];
#endif
