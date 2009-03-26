/*
 * File...........: linux/drivers/s390/block/dasd.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Horst Hummel <Horst.Hummel@de.ibm.com>
 *		    Carsten Otte <Cotte@de.ibm.com>
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999-2001
 *
 */

#include <linux/kmod.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ctype.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/hdreg.h>

#include <asm/ccwdev.h>
#include <asm/ebcdic.h>
#include <asm/idals.h>
#include <asm/todclk.h>

/* This is ugly... */
#define PRINTK_HEADER "dasd:"

#include "dasd_int.h"
/*
 * SECTION: Constant definitions to be used within this file
 */
#define DASD_CHANQ_MAX_SIZE 4

/*
 * SECTION: exported variables of dasd.c
 */
debug_info_t *dasd_debug_area;
struct dasd_discipline *dasd_diag_discipline_pointer;
void dasd_int_handler(struct ccw_device *, unsigned long, struct irb *);

MODULE_AUTHOR("Holger Smolinski <Holger.Smolinski@de.ibm.com>");
MODULE_DESCRIPTION("Linux on S/390 DASD device driver,"
		   " Copyright 2000 IBM Corporation");
MODULE_SUPPORTED_DEVICE("dasd");
MODULE_LICENSE("GPL");

/*
 * SECTION: prototypes for static functions of dasd.c
 */
static int  dasd_alloc_queue(struct dasd_block *);
static void dasd_setup_queue(struct dasd_block *);
static void dasd_free_queue(struct dasd_block *);
static void dasd_flush_request_queue(struct dasd_block *);
static int dasd_flush_block_queue(struct dasd_block *);
static void dasd_device_tasklet(struct dasd_device *);
static void dasd_block_tasklet(struct dasd_block *);
static void do_kick_device(struct work_struct *);
static void dasd_return_cqr_cb(struct dasd_ccw_req *, void *);
static void dasd_device_timeout(unsigned long);
static void dasd_block_timeout(unsigned long);

/*
 * SECTION: Operations on the device structure.
 */
static wait_queue_head_t dasd_init_waitq;
static wait_queue_head_t dasd_flush_wq;
static wait_queue_head_t generic_waitq;

/*
 * Allocate memory for a new device structure.
 */
struct dasd_device *dasd_alloc_device(void)
{
	struct dasd_device *device;

	device = kzalloc(sizeof(struct dasd_device), GFP_ATOMIC);
	if (!device)
		return ERR_PTR(-ENOMEM);

	/* Get two pages for normal block device operations. */
	device->ccw_mem = (void *) __get_free_pages(GFP_ATOMIC | GFP_DMA, 1);
	if (!device->ccw_mem) {
		kfree(device);
		return ERR_PTR(-ENOMEM);
	}
	/* Get one page for error recovery. */
	device->erp_mem = (void *) get_zeroed_page(GFP_ATOMIC | GFP_DMA);
	if (!device->erp_mem) {
		free_pages((unsigned long) device->ccw_mem, 1);
		kfree(device);
		return ERR_PTR(-ENOMEM);
	}

	dasd_init_chunklist(&device->ccw_chunks, device->ccw_mem, PAGE_SIZE*2);
	dasd_init_chunklist(&device->erp_chunks, device->erp_mem, PAGE_SIZE);
	spin_lock_init(&device->mem_lock);
	atomic_set(&device->tasklet_scheduled, 0);
	tasklet_init(&device->tasklet,
		     (void (*)(unsigned long)) dasd_device_tasklet,
		     (unsigned long) device);
	INIT_LIST_HEAD(&device->ccw_queue);
	init_timer(&device->timer);
	device->timer.function = dasd_device_timeout;
	device->timer.data = (unsigned long) device;
	INIT_WORK(&device->kick_work, do_kick_device);
	device->state = DASD_STATE_NEW;
	device->target = DASD_STATE_NEW;

	return device;
}

/*
 * Free memory of a device structure.
 */
void dasd_free_device(struct dasd_device *device)
{
	kfree(device->private);
	free_page((unsigned long) device->erp_mem);
	free_pages((unsigned long) device->ccw_mem, 1);
	kfree(device);
}

/*
 * Allocate memory for a new device structure.
 */
struct dasd_block *dasd_alloc_block(void)
{
	struct dasd_block *block;

	block = kzalloc(sizeof(*block), GFP_ATOMIC);
	if (!block)
		return ERR_PTR(-ENOMEM);
	/* open_count = 0 means device online but not in use */
	atomic_set(&block->open_count, -1);

	spin_lock_init(&block->request_queue_lock);
	atomic_set(&block->tasklet_scheduled, 0);
	tasklet_init(&block->tasklet,
		     (void (*)(unsigned long)) dasd_block_tasklet,
		     (unsigned long) block);
	INIT_LIST_HEAD(&block->ccw_queue);
	spin_lock_init(&block->queue_lock);
	init_timer(&block->timer);
	block->timer.function = dasd_block_timeout;
	block->timer.data = (unsigned long) block;

	return block;
}

/*
 * Free memory of a device structure.
 */
void dasd_free_block(struct dasd_block *block)
{
	kfree(block);
}

/*
 * Make a new device known to the system.
 */
static int dasd_state_new_to_known(struct dasd_device *device)
{
	int rc;

	/*
	 * As long as the device is not in state DASD_STATE_NEW we want to
	 * keep the reference count > 0.
	 */
	dasd_get_device(device);

	if (device->block) {
		rc = dasd_alloc_queue(device->block);
		if (rc) {
			dasd_put_device(device);
			return rc;
		}
	}
	device->state = DASD_STATE_KNOWN;
	return 0;
}

/*
 * Let the system forget about a device.
 */
static int dasd_state_known_to_new(struct dasd_device *device)
{
	/* Disable extended error reporting for this device. */
	dasd_eer_disable(device);
	/* Forget the discipline information. */
	if (device->discipline) {
		if (device->discipline->uncheck_device)
			device->discipline->uncheck_device(device);
		module_put(device->discipline->owner);
	}
	device->discipline = NULL;
	if (device->base_discipline)
		module_put(device->base_discipline->owner);
	device->base_discipline = NULL;
	device->state = DASD_STATE_NEW;

	if (device->block)
		dasd_free_queue(device->block);

	/* Give up reference we took in dasd_state_new_to_known. */
	dasd_put_device(device);
	return 0;
}

/*
 * Request the irq line for the device.
 */
static int dasd_state_known_to_basic(struct dasd_device *device)
{
	int rc;

	/* Allocate and register gendisk structure. */
	if (device->block) {
		rc = dasd_gendisk_alloc(device->block);
		if (rc)
			return rc;
	}
	/* register 'device' debug area, used for all DBF_DEV_XXX calls */
	device->debug_area = debug_register(dev_name(&device->cdev->dev), 1, 1,
					    8 * sizeof(long));
	debug_register_view(device->debug_area, &debug_sprintf_view);
	debug_set_level(device->debug_area, DBF_WARNING);
	DBF_DEV_EVENT(DBF_EMERG, device, "%s", "debug area created");

	device->state = DASD_STATE_BASIC;
	return 0;
}

/*
 * Release the irq line for the device. Terminate any running i/o.
 */
static int dasd_state_basic_to_known(struct dasd_device *device)
{
	int rc;
	if (device->block) {
		dasd_gendisk_free(device->block);
		dasd_block_clear_timer(device->block);
	}
	rc = dasd_flush_device_queue(device);
	if (rc)
		return rc;
	dasd_device_clear_timer(device);

	DBF_DEV_EVENT(DBF_EMERG, device, "%p debug area deleted", device);
	if (device->debug_area != NULL) {
		debug_unregister(device->debug_area);
		device->debug_area = NULL;
	}
	device->state = DASD_STATE_KNOWN;
	return 0;
}

/*
 * Do the initial analysis. The do_analysis function may return
 * -EAGAIN in which case the device keeps the state DASD_STATE_BASIC
 * until the discipline decides to continue the startup sequence
 * by calling the function dasd_change_state. The eckd disciplines
 * uses this to start a ccw that detects the format. The completion
 * interrupt for this detection ccw uses the kernel event daemon to
 * trigger the call to dasd_change_state. All this is done in the
 * discipline code, see dasd_eckd.c.
 * After the analysis ccw is done (do_analysis returned 0) the block
 * device is setup.
 * In case the analysis returns an error, the device setup is stopped
 * (a fake disk was already added to allow formatting).
 */
static int dasd_state_basic_to_ready(struct dasd_device *device)
{
	int rc;
	struct dasd_block *block;

	rc = 0;
	block = device->block;
	/* make disk known with correct capacity */
	if (block) {
		if (block->base->discipline->do_analysis != NULL)
			rc = block->base->discipline->do_analysis(block);
		if (rc) {
			if (rc != -EAGAIN)
				device->state = DASD_STATE_UNFMT;
			return rc;
		}
		dasd_setup_queue(block);
		set_capacity(block->gdp,
			     block->blocks << block->s2b_shift);
		device->state = DASD_STATE_READY;
		rc = dasd_scan_partitions(block);
		if (rc)
			device->state = DASD_STATE_BASIC;
	} else {
		device->state = DASD_STATE_READY;
	}
	return rc;
}

/*
 * Remove device from block device layer. Destroy dirty buffers.
 * Forget format information. Check if the target level is basic
 * and if it is create fake disk for formatting.
 */
static int dasd_state_ready_to_basic(struct dasd_device *device)
{
	int rc;

	device->state = DASD_STATE_BASIC;
	if (device->block) {
		struct dasd_block *block = device->block;
		rc = dasd_flush_block_queue(block);
		if (rc) {
			device->state = DASD_STATE_READY;
			return rc;
		}
		dasd_destroy_partitions(block);
		dasd_flush_request_queue(block);
		block->blocks = 0;
		block->bp_block = 0;
		block->s2b_shift = 0;
	}
	return 0;
}

/*
 * Back to basic.
 */
static int dasd_state_unfmt_to_basic(struct dasd_device *device)
{
	device->state = DASD_STATE_BASIC;
	return 0;
}

/*
 * Make the device online and schedule the bottom half to start
 * the requeueing of requests from the linux request queue to the
 * ccw queue.
 */
static int
dasd_state_ready_to_online(struct dasd_device * device)
{
	int rc;
	struct gendisk *disk;
	struct disk_part_iter piter;
	struct hd_struct *part;

	if (device->discipline->ready_to_online) {
		rc = device->discipline->ready_to_online(device);
		if (rc)
			return rc;
	}
	device->state = DASD_STATE_ONLINE;
	if (device->block) {
		dasd_schedule_block_bh(device->block);
		disk = device->block->bdev->bd_disk;
		disk_part_iter_init(&piter, disk, DISK_PITER_INCL_PART0);
		while ((part = disk_part_iter_next(&piter)))
			kobject_uevent(&part_to_dev(part)->kobj, KOBJ_CHANGE);
		disk_part_iter_exit(&piter);
	}
	return 0;
}

/*
 * Stop the requeueing of requests again.
 */
static int dasd_state_online_to_ready(struct dasd_device *device)
{
	int rc;
	struct gendisk *disk;
	struct disk_part_iter piter;
	struct hd_struct *part;

	if (device->discipline->online_to_ready) {
		rc = device->discipline->online_to_ready(device);
		if (rc)
			return rc;
	}
	device->state = DASD_STATE_READY;
	if (device->block) {
		disk = device->block->bdev->bd_disk;
		disk_part_iter_init(&piter, disk, DISK_PITER_INCL_PART0);
		while ((part = disk_part_iter_next(&piter)))
			kobject_uevent(&part_to_dev(part)->kobj, KOBJ_CHANGE);
		disk_part_iter_exit(&piter);
	}
	return 0;
}

/*
 * Device startup state changes.
 */
static int dasd_increase_state(struct dasd_device *device)
{
	int rc;

	rc = 0;
	if (device->state == DASD_STATE_NEW &&
	    device->target >= DASD_STATE_KNOWN)
		rc = dasd_state_new_to_known(device);

	if (!rc &&
	    device->state == DASD_STATE_KNOWN &&
	    device->target >= DASD_STATE_BASIC)
		rc = dasd_state_known_to_basic(device);

	if (!rc &&
	    device->state == DASD_STATE_BASIC &&
	    device->target >= DASD_STATE_READY)
		rc = dasd_state_basic_to_ready(device);

	if (!rc &&
	    device->state == DASD_STATE_UNFMT &&
	    device->target > DASD_STATE_UNFMT)
		rc = -EPERM;

	if (!rc &&
	    device->state == DASD_STATE_READY &&
	    device->target >= DASD_STATE_ONLINE)
		rc = dasd_state_ready_to_online(device);

	return rc;
}

/*
 * Device shutdown state changes.
 */
static int dasd_decrease_state(struct dasd_device *device)
{
	int rc;

	rc = 0;
	if (device->state == DASD_STATE_ONLINE &&
	    device->target <= DASD_STATE_READY)
		rc = dasd_state_online_to_ready(device);

	if (!rc &&
	    device->state == DASD_STATE_READY &&
	    device->target <= DASD_STATE_BASIC)
		rc = dasd_state_ready_to_basic(device);

	if (!rc &&
	    device->state == DASD_STATE_UNFMT &&
	    device->target <= DASD_STATE_BASIC)
		rc = dasd_state_unfmt_to_basic(device);

	if (!rc &&
	    device->state == DASD_STATE_BASIC &&
	    device->target <= DASD_STATE_KNOWN)
		rc = dasd_state_basic_to_known(device);

	if (!rc &&
	    device->state == DASD_STATE_KNOWN &&
	    device->target <= DASD_STATE_NEW)
		rc = dasd_state_known_to_new(device);

	return rc;
}

/*
 * This is the main startup/shutdown routine.
 */
static void dasd_change_state(struct dasd_device *device)
{
        int rc;

	if (device->state == device->target)
		/* Already where we want to go today... */
		return;
	if (device->state < device->target)
		rc = dasd_increase_state(device);
	else
		rc = dasd_decrease_state(device);
        if (rc && rc != -EAGAIN)
                device->target = device->state;

	if (device->state == device->target)
		wake_up(&dasd_init_waitq);

	/* let user-space know that the device status changed */
	kobject_uevent(&device->cdev->dev.kobj, KOBJ_CHANGE);
}

/*
 * Kick starter for devices that did not complete the startup/shutdown
 * procedure or were sleeping because of a pending state.
 * dasd_kick_device will schedule a call do do_kick_device to the kernel
 * event daemon.
 */
static void do_kick_device(struct work_struct *work)
{
	struct dasd_device *device = container_of(work, struct dasd_device, kick_work);
	dasd_change_state(device);
	dasd_schedule_device_bh(device);
	dasd_put_device(device);
}

void dasd_kick_device(struct dasd_device *device)
{
	dasd_get_device(device);
	/* queue call to dasd_kick_device to the kernel event daemon. */
	schedule_work(&device->kick_work);
}

/*
 * Set the target state for a device and starts the state change.
 */
void dasd_set_target_state(struct dasd_device *device, int target)
{
	/* If we are in probeonly mode stop at DASD_STATE_READY. */
	if (dasd_probeonly && target > DASD_STATE_READY)
		target = DASD_STATE_READY;
	if (device->target != target) {
                if (device->state == target)
			wake_up(&dasd_init_waitq);
		device->target = target;
	}
	if (device->state != device->target)
		dasd_change_state(device);
}

/*
 * Enable devices with device numbers in [from..to].
 */
static inline int _wait_for_device(struct dasd_device *device)
{
	return (device->state == device->target);
}

void dasd_enable_device(struct dasd_device *device)
{
	dasd_set_target_state(device, DASD_STATE_ONLINE);
	if (device->state <= DASD_STATE_KNOWN)
		/* No discipline for device found. */
		dasd_set_target_state(device, DASD_STATE_NEW);
	/* Now wait for the devices to come up. */
	wait_event(dasd_init_waitq, _wait_for_device(device));
}

/*
 * SECTION: device operation (interrupt handler, start i/o, term i/o ...)
 */
#ifdef CONFIG_DASD_PROFILE

struct dasd_profile_info_t dasd_global_profile;
unsigned int dasd_profile_level = DASD_PROFILE_OFF;

/*
 * Increments counter in global and local profiling structures.
 */
#define dasd_profile_counter(value, counter, block) \
{ \
	int index; \
	for (index = 0; index < 31 && value >> (2+index); index++); \
	dasd_global_profile.counter[index]++; \
	block->profile.counter[index]++; \
}

/*
 * Add profiling information for cqr before execution.
 */
static void dasd_profile_start(struct dasd_block *block,
			       struct dasd_ccw_req *cqr,
			       struct request *req)
{
	struct list_head *l;
	unsigned int counter;

	if (dasd_profile_level != DASD_PROFILE_ON)
		return;

	/* count the length of the chanq for statistics */
	counter = 0;
	list_for_each(l, &block->ccw_queue)
		if (++counter >= 31)
			break;
	dasd_global_profile.dasd_io_nr_req[counter]++;
	block->profile.dasd_io_nr_req[counter]++;
}

/*
 * Add profiling information for cqr after execution.
 */
static void dasd_profile_end(struct dasd_block *block,
			     struct dasd_ccw_req *cqr,
			     struct request *req)
{
	long strtime, irqtime, endtime, tottime;	/* in microseconds */
	long tottimeps, sectors;

	if (dasd_profile_level != DASD_PROFILE_ON)
		return;

	sectors = req->nr_sectors;
	if (!cqr->buildclk || !cqr->startclk ||
	    !cqr->stopclk || !cqr->endclk ||
	    !sectors)
		return;

	strtime = ((cqr->startclk - cqr->buildclk) >> 12);
	irqtime = ((cqr->stopclk - cqr->startclk) >> 12);
	endtime = ((cqr->endclk - cqr->stopclk) >> 12);
	tottime = ((cqr->endclk - cqr->buildclk) >> 12);
	tottimeps = tottime / sectors;

	if (!dasd_global_profile.dasd_io_reqs)
		memset(&dasd_global_profile, 0,
		       sizeof(struct dasd_profile_info_t));
	dasd_global_profile.dasd_io_reqs++;
	dasd_global_profile.dasd_io_sects += sectors;

	if (!block->profile.dasd_io_reqs)
		memset(&block->profile, 0,
		       sizeof(struct dasd_profile_info_t));
	block->profile.dasd_io_reqs++;
	block->profile.dasd_io_sects += sectors;

	dasd_profile_counter(sectors, dasd_io_secs, block);
	dasd_profile_counter(tottime, dasd_io_times, block);
	dasd_profile_counter(tottimeps, dasd_io_timps, block);
	dasd_profile_counter(strtime, dasd_io_time1, block);
	dasd_profile_counter(irqtime, dasd_io_time2, block);
	dasd_profile_counter(irqtime / sectors, dasd_io_time2ps, block);
	dasd_profile_counter(endtime, dasd_io_time3, block);
}
#else
#define dasd_profile_start(block, cqr, req) do {} while (0)
#define dasd_profile_end(block, cqr, req) do {} while (0)
#endif				/* CONFIG_DASD_PROFILE */

/*
 * Allocate memory for a channel program with 'cplength' channel
 * command words and 'datasize' additional space. There are two
 * variantes: 1) dasd_kmalloc_request uses kmalloc to get the needed
 * memory and 2) dasd_smalloc_request uses the static ccw memory
 * that gets allocated for each device.
 */
struct dasd_ccw_req *dasd_kmalloc_request(char *magic, int cplength,
					  int datasize,
					  struct dasd_device *device)
{
	struct dasd_ccw_req *cqr;

	/* Sanity checks */
	BUG_ON( magic == NULL || datasize > PAGE_SIZE ||
	     (cplength*sizeof(struct ccw1)) > PAGE_SIZE);

	cqr = kzalloc(sizeof(struct dasd_ccw_req), GFP_ATOMIC);
	if (cqr == NULL)
		return ERR_PTR(-ENOMEM);
	cqr->cpaddr = NULL;
	if (cplength > 0) {
		cqr->cpaddr = kcalloc(cplength, sizeof(struct ccw1),
				      GFP_ATOMIC | GFP_DMA);
		if (cqr->cpaddr == NULL) {
			kfree(cqr);
			return ERR_PTR(-ENOMEM);
		}
	}
	cqr->data = NULL;
	if (datasize > 0) {
		cqr->data = kzalloc(datasize, GFP_ATOMIC | GFP_DMA);
		if (cqr->data == NULL) {
			kfree(cqr->cpaddr);
			kfree(cqr);
			return ERR_PTR(-ENOMEM);
		}
	}
	strncpy((char *) &cqr->magic, magic, 4);
	ASCEBC((char *) &cqr->magic, 4);
	set_bit(DASD_CQR_FLAGS_USE_ERP, &cqr->flags);
	dasd_get_device(device);
	return cqr;
}

struct dasd_ccw_req *dasd_smalloc_request(char *magic, int cplength,
					  int datasize,
					  struct dasd_device *device)
{
	unsigned long flags;
	struct dasd_ccw_req *cqr;
	char *data;
	int size;

	/* Sanity checks */
	BUG_ON( magic == NULL || datasize > PAGE_SIZE ||
	     (cplength*sizeof(struct ccw1)) > PAGE_SIZE);

	size = (sizeof(struct dasd_ccw_req) + 7L) & -8L;
	if (cplength > 0)
		size += cplength * sizeof(struct ccw1);
	if (datasize > 0)
		size += datasize;
	spin_lock_irqsave(&device->mem_lock, flags);
	cqr = (struct dasd_ccw_req *)
		dasd_alloc_chunk(&device->ccw_chunks, size);
	spin_unlock_irqrestore(&device->mem_lock, flags);
	if (cqr == NULL)
		return ERR_PTR(-ENOMEM);
	memset(cqr, 0, sizeof(struct dasd_ccw_req));
	data = (char *) cqr + ((sizeof(struct dasd_ccw_req) + 7L) & -8L);
	cqr->cpaddr = NULL;
	if (cplength > 0) {
		cqr->cpaddr = (struct ccw1 *) data;
		data += cplength*sizeof(struct ccw1);
		memset(cqr->cpaddr, 0, cplength*sizeof(struct ccw1));
	}
	cqr->data = NULL;
	if (datasize > 0) {
		cqr->data = data;
 		memset(cqr->data, 0, datasize);
	}
	strncpy((char *) &cqr->magic, magic, 4);
	ASCEBC((char *) &cqr->magic, 4);
	set_bit(DASD_CQR_FLAGS_USE_ERP, &cqr->flags);
	dasd_get_device(device);
	return cqr;
}

/*
 * Free memory of a channel program. This function needs to free all the
 * idal lists that might have been created by dasd_set_cda and the
 * struct dasd_ccw_req itself.
 */
void dasd_kfree_request(struct dasd_ccw_req *cqr, struct dasd_device *device)
{
#ifdef CONFIG_64BIT
	struct ccw1 *ccw;

	/* Clear any idals used for the request. */
	ccw = cqr->cpaddr;
	do {
		clear_normalized_cda(ccw);
	} while (ccw++->flags & (CCW_FLAG_CC | CCW_FLAG_DC));
#endif
	kfree(cqr->cpaddr);
	kfree(cqr->data);
	kfree(cqr);
	dasd_put_device(device);
}

void dasd_sfree_request(struct dasd_ccw_req *cqr, struct dasd_device *device)
{
	unsigned long flags;

	spin_lock_irqsave(&device->mem_lock, flags);
	dasd_free_chunk(&device->ccw_chunks, cqr);
	spin_unlock_irqrestore(&device->mem_lock, flags);
	dasd_put_device(device);
}

/*
 * Check discipline magic in cqr.
 */
static inline int dasd_check_cqr(struct dasd_ccw_req *cqr)
{
	struct dasd_device *device;

	if (cqr == NULL)
		return -EINVAL;
	device = cqr->startdev;
	if (strncmp((char *) &cqr->magic, device->discipline->ebcname, 4)) {
		DEV_MESSAGE(KERN_WARNING, device,
			    " dasd_ccw_req 0x%08x magic doesn't match"
			    " discipline 0x%08x",
			    cqr->magic,
			    *(unsigned int *) device->discipline->name);
		return -EINVAL;
	}
	return 0;
}

/*
 * Terminate the current i/o and set the request to clear_pending.
 * Timer keeps device runnig.
 * ccw_device_clear can fail if the i/o subsystem
 * is in a bad mood.
 */
int dasd_term_IO(struct dasd_ccw_req *cqr)
{
	struct dasd_device *device;
	int retries, rc;

	/* Check the cqr */
	rc = dasd_check_cqr(cqr);
	if (rc)
		return rc;
	retries = 0;
	device = (struct dasd_device *) cqr->startdev;
	while ((retries < 5) && (cqr->status == DASD_CQR_IN_IO)) {
		rc = ccw_device_clear(device->cdev, (long) cqr);
		switch (rc) {
		case 0:	/* termination successful */
			cqr->retries--;
			cqr->status = DASD_CQR_CLEAR_PENDING;
			cqr->stopclk = get_clock();
			cqr->starttime = 0;
			DBF_DEV_EVENT(DBF_DEBUG, device,
				      "terminate cqr %p successful",
				      cqr);
			break;
		case -ENODEV:
			DBF_DEV_EVENT(DBF_ERR, device, "%s",
				      "device gone, retry");
			break;
		case -EIO:
			DBF_DEV_EVENT(DBF_ERR, device, "%s",
				      "I/O error, retry");
			break;
		case -EINVAL:
		case -EBUSY:
			DBF_DEV_EVENT(DBF_ERR, device, "%s",
				      "device busy, retry later");
			break;
		default:
			DEV_MESSAGE(KERN_ERR, device,
				    "line %d unknown RC=%d, please "
				    "report to linux390@de.ibm.com",
				    __LINE__, rc);
			BUG();
			break;
		}
		retries++;
	}
	dasd_schedule_device_bh(device);
	return rc;
}

/*
 * Start the i/o. This start_IO can fail if the channel is really busy.
 * In that case set up a timer to start the request later.
 */
int dasd_start_IO(struct dasd_ccw_req *cqr)
{
	struct dasd_device *device;
	int rc;

	/* Check the cqr */
	rc = dasd_check_cqr(cqr);
	if (rc)
		return rc;
	device = (struct dasd_device *) cqr->startdev;
	if (cqr->retries < 0) {
		DEV_MESSAGE(KERN_DEBUG, device,
			    "start_IO: request %p (%02x/%i) - no retry left.",
			    cqr, cqr->status, cqr->retries);
		cqr->status = DASD_CQR_ERROR;
		return -EIO;
	}
	cqr->startclk = get_clock();
	cqr->starttime = jiffies;
	cqr->retries--;
	rc = ccw_device_start(device->cdev, cqr->cpaddr, (long) cqr,
			      cqr->lpm, 0);
	switch (rc) {
	case 0:
		cqr->status = DASD_CQR_IN_IO;
		DBF_DEV_EVENT(DBF_DEBUG, device,
			      "start_IO: request %p started successful",
			      cqr);
		break;
	case -EBUSY:
		DBF_DEV_EVENT(DBF_ERR, device, "%s",
			      "start_IO: device busy, retry later");
		break;
	case -ETIMEDOUT:
		DBF_DEV_EVENT(DBF_ERR, device, "%s",
			      "start_IO: request timeout, retry later");
		break;
	case -EACCES:
		/* -EACCES indicates that the request used only a
		 * subset of the available pathes and all these
		 * pathes are gone.
		 * Do a retry with all available pathes.
		 */
		cqr->lpm = LPM_ANYPATH;
		DBF_DEV_EVENT(DBF_ERR, device, "%s",
			      "start_IO: selected pathes gone,"
			      " retry on all pathes");
		break;
	case -ENODEV:
	case -EIO:
		DBF_DEV_EVENT(DBF_ERR, device, "%s",
			      "start_IO: device gone, retry");
		break;
	default:
		DEV_MESSAGE(KERN_ERR, device,
			    "line %d unknown RC=%d, please report"
			    " to linux390@de.ibm.com", __LINE__, rc);
		BUG();
		break;
	}
	return rc;
}

/*
 * Timeout function for dasd devices. This is used for different purposes
 *  1) missing interrupt handler for normal operation
 *  2) delayed start of request where start_IO failed with -EBUSY
 *  3) timeout for missing state change interrupts
 * The head of the ccw queue will have status DASD_CQR_IN_IO for 1),
 * DASD_CQR_QUEUED for 2) and 3).
 */
static void dasd_device_timeout(unsigned long ptr)
{
	unsigned long flags;
	struct dasd_device *device;

	device = (struct dasd_device *) ptr;
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	/* re-activate request queue */
        device->stopped &= ~DASD_STOPPED_PENDING;
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
	dasd_schedule_device_bh(device);
}

/*
 * Setup timeout for a device in jiffies.
 */
void dasd_device_set_timer(struct dasd_device *device, int expires)
{
	if (expires == 0)
		del_timer(&device->timer);
	else
		mod_timer(&device->timer, jiffies + expires);
}

/*
 * Clear timeout for a device.
 */
void dasd_device_clear_timer(struct dasd_device *device)
{
	del_timer(&device->timer);
}

static void dasd_handle_killed_request(struct ccw_device *cdev,
				       unsigned long intparm)
{
	struct dasd_ccw_req *cqr;
	struct dasd_device *device;

	if (!intparm)
		return;
	cqr = (struct dasd_ccw_req *) intparm;
	if (cqr->status != DASD_CQR_IN_IO) {
		MESSAGE(KERN_DEBUG,
			"invalid status in handle_killed_request: "
			"bus_id %s, status %02x",
			dev_name(&cdev->dev), cqr->status);
		return;
	}

	device = (struct dasd_device *) cqr->startdev;
	if (device == NULL ||
	    device != dasd_device_from_cdev_locked(cdev) ||
	    strncmp(device->discipline->ebcname, (char *) &cqr->magic, 4)) {
		MESSAGE(KERN_DEBUG, "invalid device in request: bus_id %s",
			dev_name(&cdev->dev));
		return;
	}

	/* Schedule request to be retried. */
	cqr->status = DASD_CQR_QUEUED;

	dasd_device_clear_timer(device);
	dasd_schedule_device_bh(device);
	dasd_put_device(device);
}

void dasd_generic_handle_state_change(struct dasd_device *device)
{
	/* First of all start sense subsystem status request. */
	dasd_eer_snss(device);

	device->stopped &= ~DASD_STOPPED_PENDING;
	dasd_schedule_device_bh(device);
	if (device->block)
		dasd_schedule_block_bh(device->block);
}

/*
 * Interrupt handler for "normal" ssch-io based dasd devices.
 */
void dasd_int_handler(struct ccw_device *cdev, unsigned long intparm,
		      struct irb *irb)
{
	struct dasd_ccw_req *cqr, *next;
	struct dasd_device *device;
	unsigned long long now;
	int expires;

	if (IS_ERR(irb)) {
		switch (PTR_ERR(irb)) {
		case -EIO:
			break;
		case -ETIMEDOUT:
			printk(KERN_WARNING"%s(%s): request timed out\n",
			       __func__, dev_name(&cdev->dev));
			break;
		default:
			printk(KERN_WARNING"%s(%s): unknown error %ld\n",
			       __func__, dev_name(&cdev->dev), PTR_ERR(irb));
		}
		dasd_handle_killed_request(cdev, intparm);
		return;
	}

	now = get_clock();

	DBF_EVENT(DBF_ERR, "Interrupt: bus_id %s CS/DS %04x ip %08x",
		  dev_name(&cdev->dev), ((irb->scsw.cmd.cstat << 8) |
		  irb->scsw.cmd.dstat), (unsigned int) intparm);

	/* check for unsolicited interrupts */
	cqr = (struct dasd_ccw_req *) intparm;
	if (!cqr || ((irb->scsw.cmd.cc == 1) &&
		     (irb->scsw.cmd.fctl & SCSW_FCTL_START_FUNC) &&
		     (irb->scsw.cmd.stctl & SCSW_STCTL_STATUS_PEND))) {
		if (cqr && cqr->status == DASD_CQR_IN_IO)
			cqr->status = DASD_CQR_QUEUED;
		device = dasd_device_from_cdev_locked(cdev);
		if (!IS_ERR(device)) {
			dasd_device_clear_timer(device);
			device->discipline->handle_unsolicited_interrupt(device,
									 irb);
			dasd_put_device(device);
		}
		return;
	}

	device = (struct dasd_device *) cqr->startdev;
	if (!device ||
	    strncmp(device->discipline->ebcname, (char *) &cqr->magic, 4)) {
		MESSAGE(KERN_DEBUG, "invalid device in request: bus_id %s",
			dev_name(&cdev->dev));
		return;
	}

	/* Check for clear pending */
	if (cqr->status == DASD_CQR_CLEAR_PENDING &&
	    irb->scsw.cmd.fctl & SCSW_FCTL_CLEAR_FUNC) {
		cqr->status = DASD_CQR_CLEARED;
		dasd_device_clear_timer(device);
		wake_up(&dasd_flush_wq);
		dasd_schedule_device_bh(device);
		return;
	}

 	/* check status - the request might have been killed by dyn detach */
	if (cqr->status != DASD_CQR_IN_IO) {
		MESSAGE(KERN_DEBUG,
			"invalid status: bus_id %s, status %02x",
			dev_name(&cdev->dev), cqr->status);
		return;
	}
	DBF_DEV_EVENT(DBF_DEBUG, device, "Int: CS/DS 0x%04x for cqr %p",
		      ((irb->scsw.cmd.cstat << 8) | irb->scsw.cmd.dstat), cqr);
	next = NULL;
	expires = 0;
	if (irb->scsw.cmd.dstat == (DEV_STAT_CHN_END | DEV_STAT_DEV_END) &&
	    irb->scsw.cmd.cstat == 0 && !irb->esw.esw0.erw.cons) {
		/* request was completed successfully */
		cqr->status = DASD_CQR_SUCCESS;
		cqr->stopclk = now;
		/* Start first request on queue if possible -> fast_io. */
		if (cqr->devlist.next != &device->ccw_queue) {
			next = list_entry(cqr->devlist.next,
					  struct dasd_ccw_req, devlist);
		}
	} else {  /* error */
		memcpy(&cqr->irb, irb, sizeof(struct irb));
		if (device->features & DASD_FEATURE_ERPLOG) {
			dasd_log_sense(cqr, irb);
		}
		/*
		 * If we don't want complex ERP for this request, then just
		 * reset this and retry it in the fastpath
		 */
		if (!test_bit(DASD_CQR_FLAGS_USE_ERP, &cqr->flags) &&
		    cqr->retries > 0) {
			DEV_MESSAGE(KERN_DEBUG, device,
				    "default ERP in fastpath (%i retries left)",
				    cqr->retries);
			cqr->lpm    = LPM_ANYPATH;
			cqr->status = DASD_CQR_QUEUED;
			next = cqr;
		} else
			cqr->status = DASD_CQR_ERROR;
	}
	if (next && (next->status == DASD_CQR_QUEUED) &&
	    (!device->stopped)) {
		if (device->discipline->start_IO(next) == 0)
			expires = next->expires;
		else
			DEV_MESSAGE(KERN_DEBUG, device, "%s",
				    "Interrupt fastpath "
				    "failed!");
	}
	if (expires != 0)
		dasd_device_set_timer(device, expires);
	else
		dasd_device_clear_timer(device);
	dasd_schedule_device_bh(device);
}

/*
 * If we have an error on a dasd_block layer request then we cancel
 * and return all further requests from the same dasd_block as well.
 */
static void __dasd_device_recovery(struct dasd_device *device,
				   struct dasd_ccw_req *ref_cqr)
{
	struct list_head *l, *n;
	struct dasd_ccw_req *cqr;

	/*
	 * only requeue request that came from the dasd_block layer
	 */
	if (!ref_cqr->block)
		return;

	list_for_each_safe(l, n, &device->ccw_queue) {
		cqr = list_entry(l, struct dasd_ccw_req, devlist);
		if (cqr->status == DASD_CQR_QUEUED &&
		    ref_cqr->block == cqr->block) {
			cqr->status = DASD_CQR_CLEARED;
		}
	}
};

/*
 * Remove those ccw requests from the queue that need to be returned
 * to the upper layer.
 */
static void __dasd_device_process_ccw_queue(struct dasd_device *device,
					    struct list_head *final_queue)
{
	struct list_head *l, *n;
	struct dasd_ccw_req *cqr;

	/* Process request with final status. */
	list_for_each_safe(l, n, &device->ccw_queue) {
		cqr = list_entry(l, struct dasd_ccw_req, devlist);

		/* Stop list processing at the first non-final request. */
		if (cqr->status == DASD_CQR_QUEUED ||
		    cqr->status == DASD_CQR_IN_IO ||
		    cqr->status == DASD_CQR_CLEAR_PENDING)
			break;
		if (cqr->status == DASD_CQR_ERROR) {
			__dasd_device_recovery(device, cqr);
		}
		/* Rechain finished requests to final queue */
		list_move_tail(&cqr->devlist, final_queue);
	}
}

/*
 * the cqrs from the final queue are returned to the upper layer
 * by setting a dasd_block state and calling the callback function
 */
static void __dasd_device_process_final_queue(struct dasd_device *device,
					      struct list_head *final_queue)
{
	struct list_head *l, *n;
	struct dasd_ccw_req *cqr;
	struct dasd_block *block;
	void (*callback)(struct dasd_ccw_req *, void *data);
	void *callback_data;

	list_for_each_safe(l, n, final_queue) {
		cqr = list_entry(l, struct dasd_ccw_req, devlist);
		list_del_init(&cqr->devlist);
		block = cqr->block;
		callback = cqr->callback;
		callback_data = cqr->callback_data;
		if (block)
			spin_lock_bh(&block->queue_lock);
		switch (cqr->status) {
		case DASD_CQR_SUCCESS:
			cqr->status = DASD_CQR_DONE;
			break;
		case DASD_CQR_ERROR:
			cqr->status = DASD_CQR_NEED_ERP;
			break;
		case DASD_CQR_CLEARED:
			cqr->status = DASD_CQR_TERMINATED;
			break;
		default:
			DEV_MESSAGE(KERN_ERR, device,
				    "wrong cqr status in __dasd_process_final_queue "
				    "for cqr %p, status %x",
				    cqr, cqr->status);
			BUG();
		}
		if (cqr->callback != NULL)
			(callback)(cqr, callback_data);
		if (block)
			spin_unlock_bh(&block->queue_lock);
	}
}

/*
 * Take a look at the first request on the ccw queue and check
 * if it reached its expire time. If so, terminate the IO.
 */
static void __dasd_device_check_expire(struct dasd_device *device)
{
	struct dasd_ccw_req *cqr;

	if (list_empty(&device->ccw_queue))
		return;
	cqr = list_entry(device->ccw_queue.next, struct dasd_ccw_req, devlist);
	if ((cqr->status == DASD_CQR_IN_IO && cqr->expires != 0) &&
	    (time_after_eq(jiffies, cqr->expires + cqr->starttime))) {
		if (device->discipline->term_IO(cqr) != 0) {
			/* Hmpf, try again in 5 sec */
			DEV_MESSAGE(KERN_ERR, device,
				    "internal error - timeout (%is) expired "
				    "for cqr %p, termination failed, "
				    "retrying in 5s",
				    (cqr->expires/HZ), cqr);
			cqr->expires += 5*HZ;
			dasd_device_set_timer(device, 5*HZ);
		} else {
			DEV_MESSAGE(KERN_ERR, device,
				    "internal error - timeout (%is) expired "
				    "for cqr %p (%i retries left)",
				    (cqr->expires/HZ), cqr, cqr->retries);
		}
	}
}

/*
 * Take a look at the first request on the ccw queue and check
 * if it needs to be started.
 */
static void __dasd_device_start_head(struct dasd_device *device)
{
	struct dasd_ccw_req *cqr;
	int rc;

	if (list_empty(&device->ccw_queue))
		return;
	cqr = list_entry(device->ccw_queue.next, struct dasd_ccw_req, devlist);
	if (cqr->status != DASD_CQR_QUEUED)
		return;
	/* when device is stopped, return request to previous layer */
	if (device->stopped) {
		cqr->status = DASD_CQR_CLEARED;
		dasd_schedule_device_bh(device);
		return;
	}

	rc = device->discipline->start_IO(cqr);
	if (rc == 0)
		dasd_device_set_timer(device, cqr->expires);
	else if (rc == -EACCES) {
		dasd_schedule_device_bh(device);
	} else
		/* Hmpf, try again in 1/2 sec */
		dasd_device_set_timer(device, 50);
}

/*
 * Go through all request on the dasd_device request queue,
 * terminate them on the cdev if necessary, and return them to the
 * submitting layer via callback.
 * Note:
 * Make sure that all 'submitting layers' still exist when
 * this function is called!. In other words, when 'device' is a base
 * device then all block layer requests must have been removed before
 * via dasd_flush_block_queue.
 */
int dasd_flush_device_queue(struct dasd_device *device)
{
	struct dasd_ccw_req *cqr, *n;
	int rc;
	struct list_head flush_queue;

	INIT_LIST_HEAD(&flush_queue);
	spin_lock_irq(get_ccwdev_lock(device->cdev));
	rc = 0;
	list_for_each_entry_safe(cqr, n, &device->ccw_queue, devlist) {
		/* Check status and move request to flush_queue */
		switch (cqr->status) {
		case DASD_CQR_IN_IO:
			rc = device->discipline->term_IO(cqr);
			if (rc) {
				/* unable to terminate requeust */
				DEV_MESSAGE(KERN_ERR, device,
					    "dasd flush ccw_queue is unable "
					    " to terminate request %p",
					    cqr);
				/* stop flush processing */
				goto finished;
			}
			break;
		case DASD_CQR_QUEUED:
			cqr->stopclk = get_clock();
			cqr->status = DASD_CQR_CLEARED;
			break;
		default: /* no need to modify the others */
			break;
		}
		list_move_tail(&cqr->devlist, &flush_queue);
	}
finished:
	spin_unlock_irq(get_ccwdev_lock(device->cdev));
	/*
	 * After this point all requests must be in state CLEAR_PENDING,
	 * CLEARED, SUCCESS or ERROR. Now wait for CLEAR_PENDING to become
	 * one of the others.
	 */
	list_for_each_entry_safe(cqr, n, &flush_queue, devlist)
		wait_event(dasd_flush_wq,
			   (cqr->status != DASD_CQR_CLEAR_PENDING));
	/*
	 * Now set each request back to TERMINATED, DONE or NEED_ERP
	 * and call the callback function of flushed requests
	 */
	__dasd_device_process_final_queue(device, &flush_queue);
	return rc;
}

/*
 * Acquire the device lock and process queues for the device.
 */
static void dasd_device_tasklet(struct dasd_device *device)
{
	struct list_head final_queue;

	atomic_set (&device->tasklet_scheduled, 0);
	INIT_LIST_HEAD(&final_queue);
	spin_lock_irq(get_ccwdev_lock(device->cdev));
	/* Check expire time of first request on the ccw queue. */
	__dasd_device_check_expire(device);
	/* find final requests on ccw queue */
	__dasd_device_process_ccw_queue(device, &final_queue);
	spin_unlock_irq(get_ccwdev_lock(device->cdev));
	/* Now call the callback function of requests with final status */
	__dasd_device_process_final_queue(device, &final_queue);
	spin_lock_irq(get_ccwdev_lock(device->cdev));
	/* Now check if the head of the ccw queue needs to be started. */
	__dasd_device_start_head(device);
	spin_unlock_irq(get_ccwdev_lock(device->cdev));
	dasd_put_device(device);
}

/*
 * Schedules a call to dasd_tasklet over the device tasklet.
 */
void dasd_schedule_device_bh(struct dasd_device *device)
{
	/* Protect against rescheduling. */
	if (atomic_cmpxchg (&device->tasklet_scheduled, 0, 1) != 0)
		return;
	dasd_get_device(device);
	tasklet_hi_schedule(&device->tasklet);
}

/*
 * Queue a request to the head of the device ccw_queue.
 * Start the I/O if possible.
 */
void dasd_add_request_head(struct dasd_ccw_req *cqr)
{
	struct dasd_device *device;
	unsigned long flags;

	device = cqr->startdev;
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	cqr->status = DASD_CQR_QUEUED;
	list_add(&cqr->devlist, &device->ccw_queue);
	/* let the bh start the request to keep them in order */
	dasd_schedule_device_bh(device);
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
}

/*
 * Queue a request to the tail of the device ccw_queue.
 * Start the I/O if possible.
 */
void dasd_add_request_tail(struct dasd_ccw_req *cqr)
{
	struct dasd_device *device;
	unsigned long flags;

	device = cqr->startdev;
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	cqr->status = DASD_CQR_QUEUED;
	list_add_tail(&cqr->devlist, &device->ccw_queue);
	/* let the bh start the request to keep them in order */
	dasd_schedule_device_bh(device);
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
}

/*
 * Wakeup helper for the 'sleep_on' functions.
 */
static void dasd_wakeup_cb(struct dasd_ccw_req *cqr, void *data)
{
	wake_up((wait_queue_head_t *) data);
}

static inline int _wait_for_wakeup(struct dasd_ccw_req *cqr)
{
	struct dasd_device *device;
	int rc;

	device = cqr->startdev;
	spin_lock_irq(get_ccwdev_lock(device->cdev));
	rc = ((cqr->status == DASD_CQR_DONE ||
	       cqr->status == DASD_CQR_NEED_ERP ||
	       cqr->status == DASD_CQR_TERMINATED) &&
	      list_empty(&cqr->devlist));
	spin_unlock_irq(get_ccwdev_lock(device->cdev));
	return rc;
}

/*
 * Queue a request to the tail of the device ccw_queue and wait for
 * it's completion.
 */
int dasd_sleep_on(struct dasd_ccw_req *cqr)
{
	struct dasd_device *device;
	int rc;

	device = cqr->startdev;

	cqr->callback = dasd_wakeup_cb;
	cqr->callback_data = (void *) &generic_waitq;
	dasd_add_request_tail(cqr);
	wait_event(generic_waitq, _wait_for_wakeup(cqr));

	/* Request status is either done or failed. */
	rc = (cqr->status == DASD_CQR_DONE) ? 0 : -EIO;
	return rc;
}

/*
 * Queue a request to the tail of the device ccw_queue and wait
 * interruptible for it's completion.
 */
int dasd_sleep_on_interruptible(struct dasd_ccw_req *cqr)
{
	struct dasd_device *device;
	int rc;

	device = cqr->startdev;
	cqr->callback = dasd_wakeup_cb;
	cqr->callback_data = (void *) &generic_waitq;
	dasd_add_request_tail(cqr);
	rc = wait_event_interruptible(generic_waitq, _wait_for_wakeup(cqr));
	if (rc == -ERESTARTSYS) {
		dasd_cancel_req(cqr);
		/* wait (non-interruptible) for final status */
		wait_event(generic_waitq, _wait_for_wakeup(cqr));
	}
	rc = (cqr->status == DASD_CQR_DONE) ? 0 : -EIO;
	return rc;
}

/*
 * Whoa nelly now it gets really hairy. For some functions (e.g. steal lock
 * for eckd devices) the currently running request has to be terminated
 * and be put back to status queued, before the special request is added
 * to the head of the queue. Then the special request is waited on normally.
 */
static inline int _dasd_term_running_cqr(struct dasd_device *device)
{
	struct dasd_ccw_req *cqr;

	if (list_empty(&device->ccw_queue))
		return 0;
	cqr = list_entry(device->ccw_queue.next, struct dasd_ccw_req, devlist);
	return device->discipline->term_IO(cqr);
}

int dasd_sleep_on_immediatly(struct dasd_ccw_req *cqr)
{
	struct dasd_device *device;
	int rc;

	device = cqr->startdev;
	spin_lock_irq(get_ccwdev_lock(device->cdev));
	rc = _dasd_term_running_cqr(device);
	if (rc) {
		spin_unlock_irq(get_ccwdev_lock(device->cdev));
		return rc;
	}

	cqr->callback = dasd_wakeup_cb;
	cqr->callback_data = (void *) &generic_waitq;
	cqr->status = DASD_CQR_QUEUED;
	list_add(&cqr->devlist, &device->ccw_queue);

	/* let the bh start the request to keep them in order */
	dasd_schedule_device_bh(device);

	spin_unlock_irq(get_ccwdev_lock(device->cdev));

	wait_event(generic_waitq, _wait_for_wakeup(cqr));

	/* Request status is either done or failed. */
	rc = (cqr->status == DASD_CQR_DONE) ? 0 : -EIO;
	return rc;
}

/*
 * Cancels a request that was started with dasd_sleep_on_req.
 * This is useful to timeout requests. The request will be
 * terminated if it is currently in i/o.
 * Returns 1 if the request has been terminated.
 *	   0 if there was no need to terminate the request (not started yet)
 *	   negative error code if termination failed
 * Cancellation of a request is an asynchronous operation! The calling
 * function has to wait until the request is properly returned via callback.
 */
int dasd_cancel_req(struct dasd_ccw_req *cqr)
{
	struct dasd_device *device = cqr->startdev;
	unsigned long flags;
	int rc;

	rc = 0;
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	switch (cqr->status) {
	case DASD_CQR_QUEUED:
		/* request was not started - just set to cleared */
		cqr->status = DASD_CQR_CLEARED;
		break;
	case DASD_CQR_IN_IO:
		/* request in IO - terminate IO and release again */
		rc = device->discipline->term_IO(cqr);
		if (rc) {
			DEV_MESSAGE(KERN_ERR, device,
				    "dasd_cancel_req is unable "
				    " to terminate request %p, rc = %d",
				    cqr, rc);
		} else {
			cqr->stopclk = get_clock();
			rc = 1;
		}
		break;
	default: /* already finished or clear pending - do nothing */
		break;
	}
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
	dasd_schedule_device_bh(device);
	return rc;
}


/*
 * SECTION: Operations of the dasd_block layer.
 */

/*
 * Timeout function for dasd_block. This is used when the block layer
 * is waiting for something that may not come reliably, (e.g. a state
 * change interrupt)
 */
static void dasd_block_timeout(unsigned long ptr)
{
	unsigned long flags;
	struct dasd_block *block;

	block = (struct dasd_block *) ptr;
	spin_lock_irqsave(get_ccwdev_lock(block->base->cdev), flags);
	/* re-activate request queue */
	block->base->stopped &= ~DASD_STOPPED_PENDING;
	spin_unlock_irqrestore(get_ccwdev_lock(block->base->cdev), flags);
	dasd_schedule_block_bh(block);
}

/*
 * Setup timeout for a dasd_block in jiffies.
 */
void dasd_block_set_timer(struct dasd_block *block, int expires)
{
	if (expires == 0)
		del_timer(&block->timer);
	else
		mod_timer(&block->timer, jiffies + expires);
}

/*
 * Clear timeout for a dasd_block.
 */
void dasd_block_clear_timer(struct dasd_block *block)
{
	del_timer(&block->timer);
}

/*
 * posts the buffer_cache about a finalized request
 */
static inline void dasd_end_request(struct request *req, int error)
{
	if (__blk_end_request(req, error, blk_rq_bytes(req)))
		BUG();
}

/*
 * Process finished error recovery ccw.
 */
static inline void __dasd_block_process_erp(struct dasd_block *block,
					    struct dasd_ccw_req *cqr)
{
	dasd_erp_fn_t erp_fn;
	struct dasd_device *device = block->base;

	if (cqr->status == DASD_CQR_DONE)
		DBF_DEV_EVENT(DBF_NOTICE, device, "%s", "ERP successful");
	else
		DEV_MESSAGE(KERN_ERR, device, "%s", "ERP unsuccessful");
	erp_fn = device->discipline->erp_postaction(cqr);
	erp_fn(cqr);
}

/*
 * Fetch requests from the block device queue.
 */
static void __dasd_process_request_queue(struct dasd_block *block)
{
	struct request_queue *queue;
	struct request *req;
	struct dasd_ccw_req *cqr;
	struct dasd_device *basedev;
	unsigned long flags;
	queue = block->request_queue;
	basedev = block->base;
	/* No queue ? Then there is nothing to do. */
	if (queue == NULL)
		return;

	/*
	 * We requeue request from the block device queue to the ccw
	 * queue only in two states. In state DASD_STATE_READY the
	 * partition detection is done and we need to requeue requests
	 * for that. State DASD_STATE_ONLINE is normal block device
	 * operation.
	 */
	if (basedev->state < DASD_STATE_READY)
		return;
	/* Now we try to fetch requests from the request queue */
	while (!blk_queue_plugged(queue) &&
	       elv_next_request(queue)) {

		req = elv_next_request(queue);

		if (basedev->features & DASD_FEATURE_READONLY &&
		    rq_data_dir(req) == WRITE) {
			DBF_DEV_EVENT(DBF_ERR, basedev,
				      "Rejecting write request %p",
				      req);
			blkdev_dequeue_request(req);
			dasd_end_request(req, -EIO);
			continue;
		}
		cqr = basedev->discipline->build_cp(basedev, block, req);
		if (IS_ERR(cqr)) {
			if (PTR_ERR(cqr) == -EBUSY)
				break;	/* normal end condition */
			if (PTR_ERR(cqr) == -ENOMEM)
				break;	/* terminate request queue loop */
			if (PTR_ERR(cqr) == -EAGAIN) {
				/*
				 * The current request cannot be build right
				 * now, we have to try later. If this request
				 * is the head-of-queue we stop the device
				 * for 1/2 second.
				 */
				if (!list_empty(&block->ccw_queue))
					break;
				spin_lock_irqsave(get_ccwdev_lock(basedev->cdev), flags);
				basedev->stopped |= DASD_STOPPED_PENDING;
				spin_unlock_irqrestore(get_ccwdev_lock(basedev->cdev), flags);
				dasd_block_set_timer(block, HZ/2);
				break;
			}
			DBF_DEV_EVENT(DBF_ERR, basedev,
				      "CCW creation failed (rc=%ld) "
				      "on request %p",
				      PTR_ERR(cqr), req);
			blkdev_dequeue_request(req);
			dasd_end_request(req, -EIO);
			continue;
		}
		/*
		 *  Note: callback is set to dasd_return_cqr_cb in
		 * __dasd_block_start_head to cover erp requests as well
		 */
		cqr->callback_data = (void *) req;
		cqr->status = DASD_CQR_FILLED;
		blkdev_dequeue_request(req);
		list_add_tail(&cqr->blocklist, &block->ccw_queue);
		dasd_profile_start(block, cqr, req);
	}
}

static void __dasd_cleanup_cqr(struct dasd_ccw_req *cqr)
{
	struct request *req;
	int status;
	int error = 0;

	req = (struct request *) cqr->callback_data;
	dasd_profile_end(cqr->block, cqr, req);
	status = cqr->block->base->discipline->free_cp(cqr, req);
	if (status <= 0)
		error = status ? status : -EIO;
	dasd_end_request(req, error);
}

/*
 * Process ccw request queue.
 */
static void __dasd_process_block_ccw_queue(struct dasd_block *block,
					   struct list_head *final_queue)
{
	struct list_head *l, *n;
	struct dasd_ccw_req *cqr;
	dasd_erp_fn_t erp_fn;
	unsigned long flags;
	struct dasd_device *base = block->base;

restart:
	/* Process request with final status. */
	list_for_each_safe(l, n, &block->ccw_queue) {
		cqr = list_entry(l, struct dasd_ccw_req, blocklist);
		if (cqr->status != DASD_CQR_DONE &&
		    cqr->status != DASD_CQR_FAILED &&
		    cqr->status != DASD_CQR_NEED_ERP &&
		    cqr->status != DASD_CQR_TERMINATED)
			continue;

		if (cqr->status == DASD_CQR_TERMINATED) {
			base->discipline->handle_terminated_request(cqr);
			goto restart;
		}

		/*  Process requests that may be recovered */
		if (cqr->status == DASD_CQR_NEED_ERP) {
			erp_fn = base->discipline->erp_action(cqr);
			erp_fn(cqr);
			goto restart;
		}

		/* log sense for fatal error */
		if (cqr->status == DASD_CQR_FAILED) {
			dasd_log_sense(cqr, &cqr->irb);
		}

		/* First of all call extended error reporting. */
		if (dasd_eer_enabled(base) &&
		    cqr->status == DASD_CQR_FAILED) {
			dasd_eer_write(base, cqr, DASD_EER_FATALERROR);

			/* restart request  */
			cqr->status = DASD_CQR_FILLED;
			cqr->retries = 255;
			spin_lock_irqsave(get_ccwdev_lock(base->cdev), flags);
			base->stopped |= DASD_STOPPED_QUIESCE;
			spin_unlock_irqrestore(get_ccwdev_lock(base->cdev),
					       flags);
			goto restart;
		}

		/* Process finished ERP request. */
		if (cqr->refers) {
			__dasd_block_process_erp(block, cqr);
			goto restart;
		}

		/* Rechain finished requests to final queue */
		cqr->endclk = get_clock();
		list_move_tail(&cqr->blocklist, final_queue);
	}
}

static void dasd_return_cqr_cb(struct dasd_ccw_req *cqr, void *data)
{
	dasd_schedule_block_bh(cqr->block);
}

static void __dasd_block_start_head(struct dasd_block *block)
{
	struct dasd_ccw_req *cqr;

	if (list_empty(&block->ccw_queue))
		return;
	/* We allways begin with the first requests on the queue, as some
	 * of previously started requests have to be enqueued on a
	 * dasd_device again for error recovery.
	 */
	list_for_each_entry(cqr, &block->ccw_queue, blocklist) {
		if (cqr->status != DASD_CQR_FILLED)
			continue;
		/* Non-temporary stop condition will trigger fail fast */
		if (block->base->stopped & ~DASD_STOPPED_PENDING &&
		    test_bit(DASD_CQR_FLAGS_FAILFAST, &cqr->flags) &&
		    (!dasd_eer_enabled(block->base))) {
			cqr->status = DASD_CQR_FAILED;
			dasd_schedule_block_bh(block);
			continue;
		}
		/* Don't try to start requests if device is stopped */
		if (block->base->stopped)
			return;

		/* just a fail safe check, should not happen */
		if (!cqr->startdev)
			cqr->startdev = block->base;

		/* make sure that the requests we submit find their way back */
		cqr->callback = dasd_return_cqr_cb;

		dasd_add_request_tail(cqr);
	}
}

/*
 * Central dasd_block layer routine. Takes requests from the generic
 * block layer request queue, creates ccw requests, enqueues them on
 * a dasd_device and processes ccw requests that have been returned.
 */
static void dasd_block_tasklet(struct dasd_block *block)
{
	struct list_head final_queue;
	struct list_head *l, *n;
	struct dasd_ccw_req *cqr;

	atomic_set(&block->tasklet_scheduled, 0);
	INIT_LIST_HEAD(&final_queue);
	spin_lock(&block->queue_lock);
	/* Finish off requests on ccw queue */
	__dasd_process_block_ccw_queue(block, &final_queue);
	spin_unlock(&block->queue_lock);
	/* Now call the callback function of requests with final status */
	spin_lock_irq(&block->request_queue_lock);
	list_for_each_safe(l, n, &final_queue) {
		cqr = list_entry(l, struct dasd_ccw_req, blocklist);
		list_del_init(&cqr->blocklist);
		__dasd_cleanup_cqr(cqr);
	}
	spin_lock(&block->queue_lock);
	/* Get new request from the block device request queue */
	__dasd_process_request_queue(block);
	/* Now check if the head of the ccw queue needs to be started. */
	__dasd_block_start_head(block);
	spin_unlock(&block->queue_lock);
	spin_unlock_irq(&block->request_queue_lock);
	dasd_put_device(block->base);
}

static void _dasd_wake_block_flush_cb(struct dasd_ccw_req *cqr, void *data)
{
	wake_up(&dasd_flush_wq);
}

/*
 * Go through all request on the dasd_block request queue, cancel them
 * on the respective dasd_device, and return them to the generic
 * block layer.
 */
static int dasd_flush_block_queue(struct dasd_block *block)
{
	struct dasd_ccw_req *cqr, *n;
	int rc, i;
	struct list_head flush_queue;

	INIT_LIST_HEAD(&flush_queue);
	spin_lock_bh(&block->queue_lock);
	rc = 0;
restart:
	list_for_each_entry_safe(cqr, n, &block->ccw_queue, blocklist) {
		/* if this request currently owned by a dasd_device cancel it */
		if (cqr->status >= DASD_CQR_QUEUED)
			rc = dasd_cancel_req(cqr);
		if (rc < 0)
			break;
		/* Rechain request (including erp chain) so it won't be
		 * touched by the dasd_block_tasklet anymore.
		 * Replace the callback so we notice when the request
		 * is returned from the dasd_device layer.
		 */
		cqr->callback = _dasd_wake_block_flush_cb;
		for (i = 0; cqr != NULL; cqr = cqr->refers, i++)
			list_move_tail(&cqr->blocklist, &flush_queue);
		if (i > 1)
			/* moved more than one request - need to restart */
			goto restart;
	}
	spin_unlock_bh(&block->queue_lock);
	/* Now call the callback function of flushed requests */
restart_cb:
	list_for_each_entry_safe(cqr, n, &flush_queue, blocklist) {
		wait_event(dasd_flush_wq, (cqr->status < DASD_CQR_QUEUED));
		/* Process finished ERP request. */
		if (cqr->refers) {
			spin_lock_bh(&block->queue_lock);
			__dasd_block_process_erp(block, cqr);
			spin_unlock_bh(&block->queue_lock);
			/* restart list_for_xx loop since dasd_process_erp
			 * might remove multiple elements */
			goto restart_cb;
		}
		/* call the callback function */
		spin_lock_irq(&block->request_queue_lock);
		cqr->endclk = get_clock();
		list_del_init(&cqr->blocklist);
		__dasd_cleanup_cqr(cqr);
		spin_unlock_irq(&block->request_queue_lock);
	}
	return rc;
}

/*
 * Schedules a call to dasd_tasklet over the device tasklet.
 */
void dasd_schedule_block_bh(struct dasd_block *block)
{
	/* Protect against rescheduling. */
	if (atomic_cmpxchg(&block->tasklet_scheduled, 0, 1) != 0)
		return;
	/* life cycle of block is bound to it's base device */
	dasd_get_device(block->base);
	tasklet_hi_schedule(&block->tasklet);
}


/*
 * SECTION: external block device operations
 * (request queue handling, open, release, etc.)
 */

/*
 * Dasd request queue function. Called from ll_rw_blk.c
 */
static void do_dasd_request(struct request_queue *queue)
{
	struct dasd_block *block;

	block = queue->queuedata;
	spin_lock(&block->queue_lock);
	/* Get new request from the block device request queue */
	__dasd_process_request_queue(block);
	/* Now check if the head of the ccw queue needs to be started. */
	__dasd_block_start_head(block);
	spin_unlock(&block->queue_lock);
}

/*
 * Allocate and initialize request queue and default I/O scheduler.
 */
static int dasd_alloc_queue(struct dasd_block *block)
{
	int rc;

	block->request_queue = blk_init_queue(do_dasd_request,
					       &block->request_queue_lock);
	if (block->request_queue == NULL)
		return -ENOMEM;

	block->request_queue->queuedata = block;

	elevator_exit(block->request_queue->elevator);
	block->request_queue->elevator = NULL;
	rc = elevator_init(block->request_queue, "deadline");
	if (rc) {
		blk_cleanup_queue(block->request_queue);
		return rc;
	}
	return 0;
}

/*
 * Allocate and initialize request queue.
 */
static void dasd_setup_queue(struct dasd_block *block)
{
	int max;

	blk_queue_hardsect_size(block->request_queue, block->bp_block);
	max = block->base->discipline->max_blocks << block->s2b_shift;
	blk_queue_max_sectors(block->request_queue, max);
	blk_queue_max_phys_segments(block->request_queue, -1L);
	blk_queue_max_hw_segments(block->request_queue, -1L);
	blk_queue_max_segment_size(block->request_queue, -1L);
	blk_queue_segment_boundary(block->request_queue, -1L);
	blk_queue_ordered(block->request_queue, QUEUE_ORDERED_DRAIN, NULL);
}

/*
 * Deactivate and free request queue.
 */
static void dasd_free_queue(struct dasd_block *block)
{
	if (block->request_queue) {
		blk_cleanup_queue(block->request_queue);
		block->request_queue = NULL;
	}
}

/*
 * Flush request on the request queue.
 */
static void dasd_flush_request_queue(struct dasd_block *block)
{
	struct request *req;

	if (!block->request_queue)
		return;

	spin_lock_irq(&block->request_queue_lock);
	while ((req = elv_next_request(block->request_queue))) {
		blkdev_dequeue_request(req);
		dasd_end_request(req, -EIO);
	}
	spin_unlock_irq(&block->request_queue_lock);
}

static int dasd_open(struct block_device *bdev, fmode_t mode)
{
	struct dasd_block *block = bdev->bd_disk->private_data;
	struct dasd_device *base = block->base;
	int rc;

	atomic_inc(&block->open_count);
	if (test_bit(DASD_FLAG_OFFLINE, &base->flags)) {
		rc = -ENODEV;
		goto unlock;
	}

	if (!try_module_get(base->discipline->owner)) {
		rc = -EINVAL;
		goto unlock;
	}

	if (dasd_probeonly) {
		DEV_MESSAGE(KERN_INFO, base, "%s",
			    "No access to device due to probeonly mode");
		rc = -EPERM;
		goto out;
	}

	if (base->state <= DASD_STATE_BASIC) {
		DBF_DEV_EVENT(DBF_ERR, base, " %s",
			      " Cannot open unrecognized device");
		rc = -ENODEV;
		goto out;
	}

	return 0;

out:
	module_put(base->discipline->owner);
unlock:
	atomic_dec(&block->open_count);
	return rc;
}

static int dasd_release(struct gendisk *disk, fmode_t mode)
{
	struct dasd_block *block = disk->private_data;

	atomic_dec(&block->open_count);
	module_put(block->base->discipline->owner);
	return 0;
}

/*
 * Return disk geometry.
 */
static int dasd_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct dasd_block *block;
	struct dasd_device *base;

	block = bdev->bd_disk->private_data;
	base = block->base;
	if (!block)
		return -ENODEV;

	if (!base->discipline ||
	    !base->discipline->fill_geometry)
		return -EINVAL;

	base->discipline->fill_geometry(block, geo);
	geo->start = get_start_sect(bdev) >> block->s2b_shift;
	return 0;
}

struct block_device_operations
dasd_device_operations = {
	.owner		= THIS_MODULE,
	.open		= dasd_open,
	.release	= dasd_release,
	.locked_ioctl	= dasd_ioctl,
	.getgeo		= dasd_getgeo,
};

/*******************************************************************************
 * end of block device operations
 */

static void
dasd_exit(void)
{
#ifdef CONFIG_PROC_FS
	dasd_proc_exit();
#endif
	dasd_eer_exit();
        if (dasd_page_cache != NULL) {
		kmem_cache_destroy(dasd_page_cache);
		dasd_page_cache = NULL;
	}
	dasd_gendisk_exit();
	dasd_devmap_exit();
	if (dasd_debug_area != NULL) {
		debug_unregister(dasd_debug_area);
		dasd_debug_area = NULL;
	}
}

/*
 * SECTION: common functions for ccw_driver use
 */

/*
 * Initial attempt at a probe function. this can be simplified once
 * the other detection code is gone.
 */
int dasd_generic_probe(struct ccw_device *cdev,
		       struct dasd_discipline *discipline)
{
	int ret;

	ret = ccw_device_set_options(cdev, CCWDEV_DO_PATHGROUP);
	if (ret) {
		printk(KERN_WARNING
		       "dasd_generic_probe: could not set ccw-device options "
		       "for %s\n", dev_name(&cdev->dev));
		return ret;
	}
	ret = dasd_add_sysfs_files(cdev);
	if (ret) {
		printk(KERN_WARNING
		       "dasd_generic_probe: could not add sysfs entries "
		       "for %s\n", dev_name(&cdev->dev));
		return ret;
	}
	cdev->handler = &dasd_int_handler;

	/*
	 * Automatically online either all dasd devices (dasd_autodetect)
	 * or all devices specified with dasd= parameters during
	 * initial probe.
	 */
	if ((dasd_get_feature(cdev, DASD_FEATURE_INITIAL_ONLINE) > 0 ) ||
	    (dasd_autodetect && dasd_busid_known(dev_name(&cdev->dev)) != 0))
		ret = ccw_device_set_online(cdev);
	if (ret)
		printk(KERN_WARNING
		       "dasd_generic_probe: could not initially "
		       "online ccw-device %s; return code: %d\n",
		       dev_name(&cdev->dev), ret);
	return 0;
}

/*
 * This will one day be called from a global not_oper handler.
 * It is also used by driver_unregister during module unload.
 */
void dasd_generic_remove(struct ccw_device *cdev)
{
	struct dasd_device *device;
	struct dasd_block *block;

	cdev->handler = NULL;

	dasd_remove_sysfs_files(cdev);
	device = dasd_device_from_cdev(cdev);
	if (IS_ERR(device))
		return;
	if (test_and_set_bit(DASD_FLAG_OFFLINE, &device->flags)) {
		/* Already doing offline processing */
		dasd_put_device(device);
		return;
	}
	/*
	 * This device is removed unconditionally. Set offline
	 * flag to prevent dasd_open from opening it while it is
	 * no quite down yet.
	 */
	dasd_set_target_state(device, DASD_STATE_NEW);
	/* dasd_delete_device destroys the device reference. */
	block = device->block;
	device->block = NULL;
	dasd_delete_device(device);
	/*
	 * life cycle of block is bound to device, so delete it after
	 * device was safely removed
	 */
	if (block)
		dasd_free_block(block);
}

/*
 * Activate a device. This is called from dasd_{eckd,fba}_probe() when either
 * the device is detected for the first time and is supposed to be used
 * or the user has started activation through sysfs.
 */
int dasd_generic_set_online(struct ccw_device *cdev,
			    struct dasd_discipline *base_discipline)
{
	struct dasd_discipline *discipline;
	struct dasd_device *device;
	int rc;

	/* first online clears initial online feature flag */
	dasd_set_feature(cdev, DASD_FEATURE_INITIAL_ONLINE, 0);
	device = dasd_create_device(cdev);
	if (IS_ERR(device))
		return PTR_ERR(device);

	discipline = base_discipline;
	if (device->features & DASD_FEATURE_USEDIAG) {
	  	if (!dasd_diag_discipline_pointer) {
		        printk (KERN_WARNING
				"dasd_generic couldn't online device %s "
				"- discipline DIAG not available\n",
				dev_name(&cdev->dev));
			dasd_delete_device(device);
			return -ENODEV;
		}
		discipline = dasd_diag_discipline_pointer;
	}
	if (!try_module_get(base_discipline->owner)) {
		dasd_delete_device(device);
		return -EINVAL;
	}
	if (!try_module_get(discipline->owner)) {
		module_put(base_discipline->owner);
		dasd_delete_device(device);
		return -EINVAL;
	}
	device->base_discipline = base_discipline;
	device->discipline = discipline;

	/* check_device will allocate block device if necessary */
	rc = discipline->check_device(device);
	if (rc) {
		printk (KERN_WARNING
			"dasd_generic couldn't online device %s "
			"with discipline %s rc=%i\n",
			dev_name(&cdev->dev), discipline->name, rc);
		module_put(discipline->owner);
		module_put(base_discipline->owner);
		dasd_delete_device(device);
		return rc;
	}

	dasd_set_target_state(device, DASD_STATE_ONLINE);
	if (device->state <= DASD_STATE_KNOWN) {
		printk (KERN_WARNING
			"dasd_generic discipline not found for %s\n",
			dev_name(&cdev->dev));
		rc = -ENODEV;
		dasd_set_target_state(device, DASD_STATE_NEW);
		if (device->block)
			dasd_free_block(device->block);
		dasd_delete_device(device);
	} else
		pr_debug("dasd_generic device %s found\n",
				dev_name(&cdev->dev));

	/* FIXME: we have to wait for the root device but we don't want
	 * to wait for each single device but for all at once. */
	wait_event(dasd_init_waitq, _wait_for_device(device));

	dasd_put_device(device);

	return rc;
}

int dasd_generic_set_offline(struct ccw_device *cdev)
{
	struct dasd_device *device;
	struct dasd_block *block;
	int max_count, open_count;

	device = dasd_device_from_cdev(cdev);
	if (IS_ERR(device))
		return PTR_ERR(device);
	if (test_and_set_bit(DASD_FLAG_OFFLINE, &device->flags)) {
		/* Already doing offline processing */
		dasd_put_device(device);
		return 0;
	}
	/*
	 * We must make sure that this device is currently not in use.
	 * The open_count is increased for every opener, that includes
	 * the blkdev_get in dasd_scan_partitions. We are only interested
	 * in the other openers.
	 */
	if (device->block) {
		max_count = device->block->bdev ? 0 : -1;
		open_count = atomic_read(&device->block->open_count);
		if (open_count > max_count) {
			if (open_count > 0)
				printk(KERN_WARNING "Can't offline dasd "
				       "device with open count = %i.\n",
				       open_count);
			else
				printk(KERN_WARNING "%s",
				       "Can't offline dasd device due "
				       "to internal use\n");
			clear_bit(DASD_FLAG_OFFLINE, &device->flags);
			dasd_put_device(device);
			return -EBUSY;
		}
	}
	dasd_set_target_state(device, DASD_STATE_NEW);
	/* dasd_delete_device destroys the device reference. */
	block = device->block;
	device->block = NULL;
	dasd_delete_device(device);
	/*
	 * life cycle of block is bound to device, so delete it after
	 * device was safely removed
	 */
	if (block)
		dasd_free_block(block);
	return 0;
}

int dasd_generic_notify(struct ccw_device *cdev, int event)
{
	struct dasd_device *device;
	struct dasd_ccw_req *cqr;
	int ret;

	device = dasd_device_from_cdev_locked(cdev);
	if (IS_ERR(device))
		return 0;
	ret = 0;
	switch (event) {
	case CIO_GONE:
	case CIO_NO_PATH:
		/* First of all call extended error reporting. */
		dasd_eer_write(device, NULL, DASD_EER_NOPATH);

		if (device->state < DASD_STATE_BASIC)
			break;
		/* Device is active. We want to keep it. */
		list_for_each_entry(cqr, &device->ccw_queue, devlist)
			if (cqr->status == DASD_CQR_IN_IO) {
				cqr->status = DASD_CQR_QUEUED;
				cqr->retries++;
			}
		device->stopped |= DASD_STOPPED_DC_WAIT;
		dasd_device_clear_timer(device);
		dasd_schedule_device_bh(device);
		ret = 1;
		break;
	case CIO_OPER:
		/* FIXME: add a sanity check. */
		device->stopped &= ~DASD_STOPPED_DC_WAIT;
		dasd_schedule_device_bh(device);
		if (device->block)
			dasd_schedule_block_bh(device->block);
		ret = 1;
		break;
	}
	dasd_put_device(device);
	return ret;
}

static struct dasd_ccw_req *dasd_generic_build_rdc(struct dasd_device *device,
						   void *rdc_buffer,
						   int rdc_buffer_size,
						   char *magic)
{
	struct dasd_ccw_req *cqr;
	struct ccw1 *ccw;

	cqr = dasd_smalloc_request(magic, 1 /* RDC */, rdc_buffer_size, device);

	if (IS_ERR(cqr)) {
		DEV_MESSAGE(KERN_WARNING, device, "%s",
			    "Could not allocate RDC request");
		return cqr;
	}

	ccw = cqr->cpaddr;
	ccw->cmd_code = CCW_CMD_RDC;
	ccw->cda = (__u32)(addr_t)rdc_buffer;
	ccw->count = rdc_buffer_size;

	cqr->startdev = device;
	cqr->memdev = device;
	cqr->expires = 10*HZ;
	clear_bit(DASD_CQR_FLAGS_USE_ERP, &cqr->flags);
	cqr->retries = 2;
	cqr->buildclk = get_clock();
	cqr->status = DASD_CQR_FILLED;
	return cqr;
}


int dasd_generic_read_dev_chars(struct dasd_device *device, char *magic,
				void **rdc_buffer, int rdc_buffer_size)
{
	int ret;
	struct dasd_ccw_req *cqr;

	cqr = dasd_generic_build_rdc(device, *rdc_buffer, rdc_buffer_size,
				     magic);
	if (IS_ERR(cqr))
		return PTR_ERR(cqr);

	ret = dasd_sleep_on(cqr);
	dasd_sfree_request(cqr, cqr->memdev);
	return ret;
}
EXPORT_SYMBOL_GPL(dasd_generic_read_dev_chars);

static int __init dasd_init(void)
{
	int rc;

	init_waitqueue_head(&dasd_init_waitq);
	init_waitqueue_head(&dasd_flush_wq);
	init_waitqueue_head(&generic_waitq);

	/* register 'common' DASD debug area, used for all DBF_XXX calls */
	dasd_debug_area = debug_register("dasd", 1, 1, 8 * sizeof(long));
	if (dasd_debug_area == NULL) {
		rc = -ENOMEM;
		goto failed;
	}
	debug_register_view(dasd_debug_area, &debug_sprintf_view);
	debug_set_level(dasd_debug_area, DBF_WARNING);

	DBF_EVENT(DBF_EMERG, "%s", "debug area created");

	dasd_diag_discipline_pointer = NULL;

	rc = dasd_devmap_init();
	if (rc)
		goto failed;
	rc = dasd_gendisk_init();
	if (rc)
		goto failed;
	rc = dasd_parse();
	if (rc)
		goto failed;
	rc = dasd_eer_init();
	if (rc)
		goto failed;
#ifdef CONFIG_PROC_FS
	rc = dasd_proc_init();
	if (rc)
		goto failed;
#endif

	return 0;
failed:
	MESSAGE(KERN_INFO, "%s", "initialization not performed due to errors");
	dasd_exit();
	return rc;
}

module_init(dasd_init);
module_exit(dasd_exit);

EXPORT_SYMBOL(dasd_debug_area);
EXPORT_SYMBOL(dasd_diag_discipline_pointer);

EXPORT_SYMBOL(dasd_add_request_head);
EXPORT_SYMBOL(dasd_add_request_tail);
EXPORT_SYMBOL(dasd_cancel_req);
EXPORT_SYMBOL(dasd_device_clear_timer);
EXPORT_SYMBOL(dasd_block_clear_timer);
EXPORT_SYMBOL(dasd_enable_device);
EXPORT_SYMBOL(dasd_int_handler);
EXPORT_SYMBOL(dasd_kfree_request);
EXPORT_SYMBOL(dasd_kick_device);
EXPORT_SYMBOL(dasd_kmalloc_request);
EXPORT_SYMBOL(dasd_schedule_device_bh);
EXPORT_SYMBOL(dasd_schedule_block_bh);
EXPORT_SYMBOL(dasd_set_target_state);
EXPORT_SYMBOL(dasd_device_set_timer);
EXPORT_SYMBOL(dasd_block_set_timer);
EXPORT_SYMBOL(dasd_sfree_request);
EXPORT_SYMBOL(dasd_sleep_on);
EXPORT_SYMBOL(dasd_sleep_on_immediatly);
EXPORT_SYMBOL(dasd_sleep_on_interruptible);
EXPORT_SYMBOL(dasd_smalloc_request);
EXPORT_SYMBOL(dasd_start_IO);
EXPORT_SYMBOL(dasd_term_IO);

EXPORT_SYMBOL_GPL(dasd_generic_probe);
EXPORT_SYMBOL_GPL(dasd_generic_remove);
EXPORT_SYMBOL_GPL(dasd_generic_notify);
EXPORT_SYMBOL_GPL(dasd_generic_set_online);
EXPORT_SYMBOL_GPL(dasd_generic_set_offline);
EXPORT_SYMBOL_GPL(dasd_generic_handle_state_change);
EXPORT_SYMBOL_GPL(dasd_flush_device_queue);
EXPORT_SYMBOL_GPL(dasd_alloc_block);
EXPORT_SYMBOL_GPL(dasd_free_block);
