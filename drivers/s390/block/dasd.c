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

MODULE_AUTHOR("Holger Smolinski <Holger.Smolinski@de.ibm.com>");
MODULE_DESCRIPTION("Linux on S/390 DASD device driver,"
		   " Copyright 2000 IBM Corporation");
MODULE_SUPPORTED_DEVICE("dasd");
MODULE_LICENSE("GPL");

/*
 * SECTION: prototypes for static functions of dasd.c
 */
static int  dasd_alloc_queue(struct dasd_device * device);
static void dasd_setup_queue(struct dasd_device * device);
static void dasd_free_queue(struct dasd_device * device);
static void dasd_flush_request_queue(struct dasd_device *);
static void dasd_int_handler(struct ccw_device *, unsigned long, struct irb *);
static int dasd_flush_ccw_queue(struct dasd_device *, int);
static void dasd_tasklet(struct dasd_device *);
static void do_kick_device(void *data);

/*
 * SECTION: Operations on the device structure.
 */
static wait_queue_head_t dasd_init_waitq;
static wait_queue_head_t dasd_flush_wq;

/*
 * Allocate memory for a new device structure.
 */
struct dasd_device *
dasd_alloc_device(void)
{
	struct dasd_device *device;

	device = kzalloc(sizeof (struct dasd_device), GFP_ATOMIC);
	if (device == NULL)
		return ERR_PTR(-ENOMEM);
	/* open_count = 0 means device online but not in use */
	atomic_set(&device->open_count, -1);

	/* Get two pages for normal block device operations. */
	device->ccw_mem = (void *) __get_free_pages(GFP_ATOMIC | GFP_DMA, 1);
	if (device->ccw_mem == NULL) {
		kfree(device);
		return ERR_PTR(-ENOMEM);
	}
	/* Get one page for error recovery. */
	device->erp_mem = (void *) get_zeroed_page(GFP_ATOMIC | GFP_DMA);
	if (device->erp_mem == NULL) {
		free_pages((unsigned long) device->ccw_mem, 1);
		kfree(device);
		return ERR_PTR(-ENOMEM);
	}

	dasd_init_chunklist(&device->ccw_chunks, device->ccw_mem, PAGE_SIZE*2);
	dasd_init_chunklist(&device->erp_chunks, device->erp_mem, PAGE_SIZE);
	spin_lock_init(&device->mem_lock);
	spin_lock_init(&device->request_queue_lock);
	atomic_set (&device->tasklet_scheduled, 0);
	tasklet_init(&device->tasklet,
		     (void (*)(unsigned long)) dasd_tasklet,
		     (unsigned long) device);
	INIT_LIST_HEAD(&device->ccw_queue);
	init_timer(&device->timer);
	INIT_WORK(&device->kick_work, do_kick_device, device);
	device->state = DASD_STATE_NEW;
	device->target = DASD_STATE_NEW;

	return device;
}

/*
 * Free memory of a device structure.
 */
void
dasd_free_device(struct dasd_device *device)
{
	kfree(device->private);
	free_page((unsigned long) device->erp_mem);
	free_pages((unsigned long) device->ccw_mem, 1);
	kfree(device);
}

/*
 * Make a new device known to the system.
 */
static int
dasd_state_new_to_known(struct dasd_device *device)
{
	int rc;

	/*
	 * As long as the device is not in state DASD_STATE_NEW we want to
	 * keep the reference count > 0.
	 */
	dasd_get_device(device);

	rc = dasd_alloc_queue(device);
	if (rc) {
		dasd_put_device(device);
		return rc;
	}

	device->state = DASD_STATE_KNOWN;
	return 0;
}

/*
 * Let the system forget about a device.
 */
static int
dasd_state_known_to_new(struct dasd_device * device)
{
	/* Disable extended error reporting for this device. */
	dasd_eer_disable(device);
	/* Forget the discipline information. */
	if (device->discipline)
		module_put(device->discipline->owner);
	device->discipline = NULL;
	if (device->base_discipline)
		module_put(device->base_discipline->owner);
	device->base_discipline = NULL;
	device->state = DASD_STATE_NEW;

	dasd_free_queue(device);

	/* Give up reference we took in dasd_state_new_to_known. */
	dasd_put_device(device);
	return 0;
}

/*
 * Request the irq line for the device.
 */
static int
dasd_state_known_to_basic(struct dasd_device * device)
{
	int rc;

	/* Allocate and register gendisk structure. */
	rc = dasd_gendisk_alloc(device);
	if (rc)
		return rc;

	/* register 'device' debug area, used for all DBF_DEV_XXX calls */
	device->debug_area = debug_register(device->cdev->dev.bus_id, 1, 2,
					    8 * sizeof (long));
	debug_register_view(device->debug_area, &debug_sprintf_view);
	debug_set_level(device->debug_area, DBF_WARNING);
	DBF_DEV_EVENT(DBF_EMERG, device, "%s", "debug area created");

	device->state = DASD_STATE_BASIC;
	return 0;
}

/*
 * Release the irq line for the device. Terminate any running i/o.
 */
static int
dasd_state_basic_to_known(struct dasd_device * device)
{
	int rc;

	dasd_gendisk_free(device);
	rc = dasd_flush_ccw_queue(device, 1);
	if (rc)
		return rc;
	dasd_clear_timer(device);

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
static int
dasd_state_basic_to_ready(struct dasd_device * device)
{
	int rc;

	rc = 0;
	if (device->discipline->do_analysis != NULL)
		rc = device->discipline->do_analysis(device);
	if (rc) {
		if (rc != -EAGAIN)
			device->state = DASD_STATE_UNFMT;
		return rc;
	}
	/* make disk known with correct capacity */
	dasd_setup_queue(device);
	set_capacity(device->gdp, device->blocks << device->s2b_shift);
	device->state = DASD_STATE_READY;
	rc = dasd_scan_partitions(device);
	if (rc)
		device->state = DASD_STATE_BASIC;
	return rc;
}

/*
 * Remove device from block device layer. Destroy dirty buffers.
 * Forget format information. Check if the target level is basic
 * and if it is create fake disk for formatting.
 */
static int
dasd_state_ready_to_basic(struct dasd_device * device)
{
	int rc;

	rc = dasd_flush_ccw_queue(device, 0);
	if (rc)
		return rc;
	dasd_destroy_partitions(device);
	dasd_flush_request_queue(device);
	device->blocks = 0;
	device->bp_block = 0;
	device->s2b_shift = 0;
	device->state = DASD_STATE_BASIC;
	return 0;
}

/*
 * Back to basic.
 */
static int
dasd_state_unfmt_to_basic(struct dasd_device * device)
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
	device->state = DASD_STATE_ONLINE;
	dasd_schedule_bh(device);
	return 0;
}

/*
 * Stop the requeueing of requests again.
 */
static int
dasd_state_online_to_ready(struct dasd_device * device)
{
	device->state = DASD_STATE_READY;
	return 0;
}

/*
 * Device startup state changes.
 */
static int
dasd_increase_state(struct dasd_device *device)
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
static int
dasd_decrease_state(struct dasd_device *device)
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
static void
dasd_change_state(struct dasd_device *device)
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
}

/*
 * Kick starter for devices that did not complete the startup/shutdown
 * procedure or were sleeping because of a pending state.
 * dasd_kick_device will schedule a call do do_kick_device to the kernel
 * event daemon.
 */
static void
do_kick_device(void *data)
{
	struct dasd_device *device;

	device = (struct dasd_device *) data;
	dasd_change_state(device);
	dasd_schedule_bh(device);
	dasd_put_device(device);
}

void
dasd_kick_device(struct dasd_device *device)
{
	dasd_get_device(device);
	/* queue call to dasd_kick_device to the kernel event daemon. */
	schedule_work(&device->kick_work);
}

/*
 * Set the target state for a device and starts the state change.
 */
void
dasd_set_target_state(struct dasd_device *device, int target)
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
static inline int
_wait_for_device(struct dasd_device *device)
{
	return (device->state == device->target);
}

void
dasd_enable_device(struct dasd_device *device)
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
#define dasd_profile_counter(value, counter, device) \
{ \
	int index; \
	for (index = 0; index < 31 && value >> (2+index); index++); \
	dasd_global_profile.counter[index]++; \
	device->profile.counter[index]++; \
}

/*
 * Add profiling information for cqr before execution.
 */
static inline void
dasd_profile_start(struct dasd_device *device, struct dasd_ccw_req * cqr,
		   struct request *req)
{
	struct list_head *l;
	unsigned int counter;

	if (dasd_profile_level != DASD_PROFILE_ON)
		return;

	/* count the length of the chanq for statistics */
	counter = 0;
	list_for_each(l, &device->ccw_queue)
		if (++counter >= 31)
			break;
	dasd_global_profile.dasd_io_nr_req[counter]++;
	device->profile.dasd_io_nr_req[counter]++;
}

/*
 * Add profiling information for cqr after execution.
 */
static inline void
dasd_profile_end(struct dasd_device *device, struct dasd_ccw_req * cqr,
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
		       sizeof (struct dasd_profile_info_t));
	dasd_global_profile.dasd_io_reqs++;
	dasd_global_profile.dasd_io_sects += sectors;

	if (!device->profile.dasd_io_reqs)
		memset(&device->profile, 0,
		       sizeof (struct dasd_profile_info_t));
	device->profile.dasd_io_reqs++;
	device->profile.dasd_io_sects += sectors;

	dasd_profile_counter(sectors, dasd_io_secs, device);
	dasd_profile_counter(tottime, dasd_io_times, device);
	dasd_profile_counter(tottimeps, dasd_io_timps, device);
	dasd_profile_counter(strtime, dasd_io_time1, device);
	dasd_profile_counter(irqtime, dasd_io_time2, device);
	dasd_profile_counter(irqtime / sectors, dasd_io_time2ps, device);
	dasd_profile_counter(endtime, dasd_io_time3, device);
}
#else
#define dasd_profile_start(device, cqr, req) do {} while (0)
#define dasd_profile_end(device, cqr, req) do {} while (0)
#endif				/* CONFIG_DASD_PROFILE */

/*
 * Allocate memory for a channel program with 'cplength' channel
 * command words and 'datasize' additional space. There are two
 * variantes: 1) dasd_kmalloc_request uses kmalloc to get the needed
 * memory and 2) dasd_smalloc_request uses the static ccw memory
 * that gets allocated for each device.
 */
struct dasd_ccw_req *
dasd_kmalloc_request(char *magic, int cplength, int datasize,
		   struct dasd_device * device)
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

struct dasd_ccw_req *
dasd_smalloc_request(char *magic, int cplength, int datasize,
		   struct dasd_device * device)
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
void
dasd_kfree_request(struct dasd_ccw_req * cqr, struct dasd_device * device)
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

void
dasd_sfree_request(struct dasd_ccw_req * cqr, struct dasd_device * device)
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
static inline int
dasd_check_cqr(struct dasd_ccw_req *cqr)
{
	struct dasd_device *device;

	if (cqr == NULL)
		return -EINVAL;
	device = cqr->device;
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
int
dasd_term_IO(struct dasd_ccw_req * cqr)
{
	struct dasd_device *device;
	int retries, rc;

	/* Check the cqr */
	rc = dasd_check_cqr(cqr);
	if (rc)
		return rc;
	retries = 0;
	device = (struct dasd_device *) cqr->device;
	while ((retries < 5) && (cqr->status == DASD_CQR_IN_IO)) {
		rc = ccw_device_clear(device->cdev, (long) cqr);
		switch (rc) {
		case 0:	/* termination successful */
			cqr->retries--;
			cqr->status = DASD_CQR_CLEAR;
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
	dasd_schedule_bh(device);
	return rc;
}

/*
 * Start the i/o. This start_IO can fail if the channel is really busy.
 * In that case set up a timer to start the request later.
 */
int
dasd_start_IO(struct dasd_ccw_req * cqr)
{
	struct dasd_device *device;
	int rc;

	/* Check the cqr */
	rc = dasd_check_cqr(cqr);
	if (rc)
		return rc;
	device = (struct dasd_device *) cqr->device;
	if (cqr->retries < 0) {
		DEV_MESSAGE(KERN_DEBUG, device,
			    "start_IO: request %p (%02x/%i) - no retry left.",
			    cqr, cqr->status, cqr->retries);
		cqr->status = DASD_CQR_FAILED;
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
static void
dasd_timeout_device(unsigned long ptr)
{
	unsigned long flags;
	struct dasd_device *device;

	device = (struct dasd_device *) ptr;
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	/* re-activate request queue */
        device->stopped &= ~DASD_STOPPED_PENDING;
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
	dasd_schedule_bh(device);
}

/*
 * Setup timeout for a device in jiffies.
 */
void
dasd_set_timer(struct dasd_device *device, int expires)
{
	if (expires == 0) {
		if (timer_pending(&device->timer))
			del_timer(&device->timer);
		return;
	}
	if (timer_pending(&device->timer)) {
		if (mod_timer(&device->timer, jiffies + expires))
			return;
	}
	device->timer.function = dasd_timeout_device;
	device->timer.data = (unsigned long) device;
	device->timer.expires = jiffies + expires;
	add_timer(&device->timer);
}

/*
 * Clear timeout for a device.
 */
void
dasd_clear_timer(struct dasd_device *device)
{
	if (timer_pending(&device->timer))
		del_timer(&device->timer);
}

static void
dasd_handle_killed_request(struct ccw_device *cdev, unsigned long intparm)
{
	struct dasd_ccw_req *cqr;
	struct dasd_device *device;

	cqr = (struct dasd_ccw_req *) intparm;
	if (cqr->status != DASD_CQR_IN_IO) {
		MESSAGE(KERN_DEBUG,
			"invalid status in handle_killed_request: "
			"bus_id %s, status %02x",
			cdev->dev.bus_id, cqr->status);
		return;
	}

	device = (struct dasd_device *) cqr->device;
	if (device == NULL ||
	    device != dasd_device_from_cdev_locked(cdev) ||
	    strncmp(device->discipline->ebcname, (char *) &cqr->magic, 4)) {
		MESSAGE(KERN_DEBUG, "invalid device in request: bus_id %s",
			cdev->dev.bus_id);
		return;
	}

	/* Schedule request to be retried. */
	cqr->status = DASD_CQR_QUEUED;

	dasd_clear_timer(device);
	dasd_schedule_bh(device);
	dasd_put_device(device);
}

static void
dasd_handle_state_change_pending(struct dasd_device *device)
{
	struct dasd_ccw_req *cqr;
	struct list_head *l, *n;

	/* First of all start sense subsystem status request. */
	dasd_eer_snss(device);

	device->stopped &= ~DASD_STOPPED_PENDING;

        /* restart all 'running' IO on queue */
	list_for_each_safe(l, n, &device->ccw_queue) {
		cqr = list_entry(l, struct dasd_ccw_req, list);
                if (cqr->status == DASD_CQR_IN_IO) {
                        cqr->status = DASD_CQR_QUEUED;
		}
        }
	dasd_clear_timer(device);
	dasd_schedule_bh(device);
}

/*
 * Interrupt handler for "normal" ssch-io based dasd devices.
 */
void
dasd_int_handler(struct ccw_device *cdev, unsigned long intparm,
		 struct irb *irb)
{
	struct dasd_ccw_req *cqr, *next;
	struct dasd_device *device;
	unsigned long long now;
	int expires;
	dasd_era_t era;
	char mask;

	if (IS_ERR(irb)) {
		switch (PTR_ERR(irb)) {
		case -EIO:
			dasd_handle_killed_request(cdev, intparm);
			break;
		case -ETIMEDOUT:
			printk(KERN_WARNING"%s(%s): request timed out\n",
			       __FUNCTION__, cdev->dev.bus_id);
			//FIXME - dasd uses own timeout interface...
			break;
		default:
			printk(KERN_WARNING"%s(%s): unknown error %ld\n",
			       __FUNCTION__, cdev->dev.bus_id, PTR_ERR(irb));
		}
		return;
	}

	now = get_clock();

	DBF_EVENT(DBF_ERR, "Interrupt: bus_id %s CS/DS %04x ip %08x",
		  cdev->dev.bus_id, ((irb->scsw.cstat<<8)|irb->scsw.dstat),
		  (unsigned int) intparm);

	/* first of all check for state change pending interrupt */
	mask = DEV_STAT_ATTENTION | DEV_STAT_DEV_END | DEV_STAT_UNIT_EXCEP;
	if ((irb->scsw.dstat & mask) == mask) {
		device = dasd_device_from_cdev_locked(cdev);
		if (!IS_ERR(device)) {
			dasd_handle_state_change_pending(device);
			dasd_put_device(device);
		}
		return;
	}

	cqr = (struct dasd_ccw_req *) intparm;

	/* check for unsolicited interrupts */
	if (cqr == NULL) {
		MESSAGE(KERN_DEBUG,
			"unsolicited interrupt received: bus_id %s",
			cdev->dev.bus_id);
		return;
	}

	device = (struct dasd_device *) cqr->device;
	if (device == NULL ||
	    strncmp(device->discipline->ebcname, (char *) &cqr->magic, 4)) {
		MESSAGE(KERN_DEBUG, "invalid device in request: bus_id %s",
			cdev->dev.bus_id);
		return;
	}

	/* Check for clear pending */
	if (cqr->status == DASD_CQR_CLEAR &&
	    irb->scsw.fctl & SCSW_FCTL_CLEAR_FUNC) {
		cqr->status = DASD_CQR_QUEUED;
		dasd_clear_timer(device);
		wake_up(&dasd_flush_wq);
		dasd_schedule_bh(device);
		return;
	}

 	/* check status - the request might have been killed by dyn detach */
	if (cqr->status != DASD_CQR_IN_IO) {
		MESSAGE(KERN_DEBUG,
			"invalid status: bus_id %s, status %02x",
			cdev->dev.bus_id, cqr->status);
		return;
	}
	DBF_DEV_EVENT(DBF_DEBUG, device, "Int: CS/DS 0x%04x for cqr %p",
		      ((irb->scsw.cstat << 8) | irb->scsw.dstat), cqr);

 	/* Find out the appropriate era_action. */
	if (irb->scsw.fctl & SCSW_FCTL_HALT_FUNC)
		era = dasd_era_fatal;
	else if (irb->scsw.dstat == (DEV_STAT_CHN_END | DEV_STAT_DEV_END) &&
		 irb->scsw.cstat == 0 &&
		 !irb->esw.esw0.erw.cons)
		era = dasd_era_none;
	else if (!test_bit(DASD_CQR_FLAGS_USE_ERP, &cqr->flags))
 	        era = dasd_era_fatal; /* don't recover this request */
	else if (irb->esw.esw0.erw.cons)
		era = device->discipline->examine_error(cqr, irb);
	else
		era = dasd_era_recover;

	DBF_DEV_EVENT(DBF_DEBUG, device, "era_code %d", era);
	expires = 0;
	if (era == dasd_era_none) {
		cqr->status = DASD_CQR_DONE;
		cqr->stopclk = now;
		/* Start first request on queue if possible -> fast_io. */
		if (cqr->list.next != &device->ccw_queue) {
			next = list_entry(cqr->list.next,
					  struct dasd_ccw_req, list);
			if ((next->status == DASD_CQR_QUEUED) &&
			    (!device->stopped)) {
				if (device->discipline->start_IO(next) == 0)
					expires = next->expires;
				else
					DEV_MESSAGE(KERN_DEBUG, device, "%s",
						    "Interrupt fastpath "
						    "failed!");
			}
		}
	} else {		/* error */
		memcpy(&cqr->irb, irb, sizeof (struct irb));
#ifdef ERP_DEBUG
		/* dump sense data */
		dasd_log_sense(cqr, irb);
#endif
		switch (era) {
		case dasd_era_fatal:
			cqr->status = DASD_CQR_FAILED;
			cqr->stopclk = now;
			break;
		case dasd_era_recover:
			cqr->status = DASD_CQR_ERROR;
			break;
		default:
			BUG();
		}
	}
	if (expires != 0)
		dasd_set_timer(device, expires);
	else
		dasd_clear_timer(device);
	dasd_schedule_bh(device);
}

/*
 * posts the buffer_cache about a finalized request
 */
static inline void
dasd_end_request(struct request *req, int uptodate)
{
	if (end_that_request_first(req, uptodate, req->hard_nr_sectors))
		BUG();
	add_disk_randomness(req->rq_disk);
	end_that_request_last(req, uptodate);
}

/*
 * Process finished error recovery ccw.
 */
static inline void
__dasd_process_erp(struct dasd_device *device, struct dasd_ccw_req *cqr)
{
	dasd_erp_fn_t erp_fn;

	if (cqr->status == DASD_CQR_DONE)
		DBF_DEV_EVENT(DBF_NOTICE, device, "%s", "ERP successful");
	else
		DEV_MESSAGE(KERN_ERR, device, "%s", "ERP unsuccessful");
	erp_fn = device->discipline->erp_postaction(cqr);
	erp_fn(cqr);
}

/*
 * Process ccw request queue.
 */
static inline void
__dasd_process_ccw_queue(struct dasd_device * device,
			 struct list_head *final_queue)
{
	struct list_head *l, *n;
	struct dasd_ccw_req *cqr;
	dasd_erp_fn_t erp_fn;

restart:
	/* Process request with final status. */
	list_for_each_safe(l, n, &device->ccw_queue) {
		cqr = list_entry(l, struct dasd_ccw_req, list);
		/* Stop list processing at the first non-final request. */
		if (cqr->status != DASD_CQR_DONE &&
		    cqr->status != DASD_CQR_FAILED &&
		    cqr->status != DASD_CQR_ERROR)
			break;
		/*  Process requests with DASD_CQR_ERROR */
		if (cqr->status == DASD_CQR_ERROR) {
			if (cqr->irb.scsw.fctl & SCSW_FCTL_HALT_FUNC) {
				cqr->status = DASD_CQR_FAILED;
				cqr->stopclk = get_clock();
			} else {
				if (cqr->irb.esw.esw0.erw.cons) {
					erp_fn = device->discipline->
						erp_action(cqr);
					erp_fn(cqr);
				} else
					dasd_default_erp_action(cqr);
			}
			goto restart;
		}

		/* First of all call extended error reporting. */
		if (dasd_eer_enabled(device) &&
		    cqr->status == DASD_CQR_FAILED) {
			dasd_eer_write(device, cqr, DASD_EER_FATALERROR);

			/* restart request  */
			cqr->status = DASD_CQR_QUEUED;
			cqr->retries = 255;
			device->stopped |= DASD_STOPPED_QUIESCE;
			goto restart;
		}

		/* Process finished ERP request. */
		if (cqr->refers) {
			__dasd_process_erp(device, cqr);
			goto restart;
		}

		/* Rechain finished requests to final queue */
		cqr->endclk = get_clock();
		list_move_tail(&cqr->list, final_queue);
	}
}

static void
dasd_end_request_cb(struct dasd_ccw_req * cqr, void *data)
{
	struct request *req;
	struct dasd_device *device;
	int status;

	req = (struct request *) data;
	device = cqr->device;
	dasd_profile_end(device, cqr, req);
	status = cqr->device->discipline->free_cp(cqr,req);
	spin_lock_irq(&device->request_queue_lock);
	dasd_end_request(req, status);
	spin_unlock_irq(&device->request_queue_lock);
}


/*
 * Fetch requests from the block device queue.
 */
static inline void
__dasd_process_blk_queue(struct dasd_device * device)
{
	request_queue_t *queue;
	struct request *req;
	struct dasd_ccw_req *cqr;
	int nr_queued;

	queue = device->request_queue;
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
	if (device->state != DASD_STATE_READY &&
	    device->state != DASD_STATE_ONLINE)
		return;
	nr_queued = 0;
	/* Now we try to fetch requests from the request queue */
	list_for_each_entry(cqr, &device->ccw_queue, list)
		if (cqr->status == DASD_CQR_QUEUED)
			nr_queued++;
	while (!blk_queue_plugged(queue) &&
	       elv_next_request(queue) &&
		nr_queued < DASD_CHANQ_MAX_SIZE) {
		req = elv_next_request(queue);

		if (device->features & DASD_FEATURE_READONLY &&
		    rq_data_dir(req) == WRITE) {
			DBF_DEV_EVENT(DBF_ERR, device,
				      "Rejecting write request %p",
				      req);
			blkdev_dequeue_request(req);
			dasd_end_request(req, 0);
			continue;
		}
		if (device->stopped & DASD_STOPPED_DC_EIO) {
			blkdev_dequeue_request(req);
			dasd_end_request(req, 0);
			continue;
		}
		cqr = device->discipline->build_cp(device, req);
		if (IS_ERR(cqr)) {
			if (PTR_ERR(cqr) == -ENOMEM)
				break;	/* terminate request queue loop */
			DBF_DEV_EVENT(DBF_ERR, device,
				      "CCW creation failed (rc=%ld) "
				      "on request %p",
				      PTR_ERR(cqr), req);
			blkdev_dequeue_request(req);
			dasd_end_request(req, 0);
			continue;
		}
		cqr->callback = dasd_end_request_cb;
		cqr->callback_data = (void *) req;
		cqr->status = DASD_CQR_QUEUED;
		blkdev_dequeue_request(req);
		list_add_tail(&cqr->list, &device->ccw_queue);
		dasd_profile_start(device, cqr, req);
		nr_queued++;
	}
}

/*
 * Take a look at the first request on the ccw queue and check
 * if it reached its expire time. If so, terminate the IO.
 */
static inline void
__dasd_check_expire(struct dasd_device * device)
{
	struct dasd_ccw_req *cqr;

	if (list_empty(&device->ccw_queue))
		return;
	cqr = list_entry(device->ccw_queue.next, struct dasd_ccw_req, list);
	if ((cqr->status == DASD_CQR_IN_IO && cqr->expires != 0) &&
	    (time_after_eq(jiffies, cqr->expires + cqr->starttime))) {
		if (device->discipline->term_IO(cqr) != 0) {
			/* Hmpf, try again in 5 sec */
			dasd_set_timer(device, 5*HZ);
			DEV_MESSAGE(KERN_ERR, device,
				    "internal error - timeout (%is) expired "
				    "for cqr %p, termination failed, "
				    "retrying in 5s",
				    (cqr->expires/HZ), cqr);
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
static inline void
__dasd_start_head(struct dasd_device * device)
{
	struct dasd_ccw_req *cqr;
	int rc;

	if (list_empty(&device->ccw_queue))
		return;
	cqr = list_entry(device->ccw_queue.next, struct dasd_ccw_req, list);
	if (cqr->status != DASD_CQR_QUEUED)
		return;
	/* Non-temporary stop condition will trigger fail fast */
	if (device->stopped & ~DASD_STOPPED_PENDING &&
	    test_bit(DASD_CQR_FLAGS_FAILFAST, &cqr->flags) &&
	    (!dasd_eer_enabled(device))) {
		cqr->status = DASD_CQR_FAILED;
		dasd_schedule_bh(device);
		return;
	}
	/* Don't try to start requests if device is stopped */
	if (device->stopped)
		return;

	rc = device->discipline->start_IO(cqr);
	if (rc == 0)
		dasd_set_timer(device, cqr->expires);
	else if (rc == -EACCES) {
		dasd_schedule_bh(device);
	} else
		/* Hmpf, try again in 1/2 sec */
		dasd_set_timer(device, 50);
}

static inline int
_wait_for_clear(struct dasd_ccw_req *cqr)
{
	return (cqr->status == DASD_CQR_QUEUED);
}

/*
 * Remove all requests from the ccw queue (all = '1') or only block device
 * requests in case all = '0'.
 * Take care of the erp-chain (chained via cqr->refers) and remove either
 * the whole erp-chain or none of the erp-requests.
 * If a request is currently running, term_IO is called and the request
 * is re-queued. Prior to removing the terminated request we need to wait
 * for the clear-interrupt.
 * In case termination is not possible we stop processing and just finishing
 * the already moved requests.
 */
static int
dasd_flush_ccw_queue(struct dasd_device * device, int all)
{
	struct dasd_ccw_req *cqr, *orig, *n;
	int rc, i;

	struct list_head flush_queue;

	INIT_LIST_HEAD(&flush_queue);
	spin_lock_irq(get_ccwdev_lock(device->cdev));
	rc = 0;
restart:
	list_for_each_entry_safe(cqr, n, &device->ccw_queue, list) {
		/* get original request of erp request-chain */
		for (orig = cqr; orig->refers != NULL; orig = orig->refers);

		/* Flush all request or only block device requests? */
		if (all == 0 && cqr->callback != dasd_end_request_cb &&
		    orig->callback != dasd_end_request_cb) {
			continue;
		}
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
		case DASD_CQR_ERROR:
			/* set request to FAILED */
			cqr->stopclk = get_clock();
			cqr->status = DASD_CQR_FAILED;
			break;
		default: /* do not touch the others */
			break;
		}
		/* Rechain request (including erp chain) */
		for (i = 0; cqr != NULL; cqr = cqr->refers, i++) {
			cqr->endclk = get_clock();
			list_move_tail(&cqr->list, &flush_queue);
		}
		if (i > 1)
			/* moved more than one request - need to restart */
			goto restart;
	}

finished:
	spin_unlock_irq(get_ccwdev_lock(device->cdev));
	/* Now call the callback function of flushed requests */
restart_cb:
	list_for_each_entry_safe(cqr, n, &flush_queue, list) {
		if (cqr->status == DASD_CQR_CLEAR) {
			/* wait for clear interrupt! */
			wait_event(dasd_flush_wq, _wait_for_clear(cqr));
			cqr->status = DASD_CQR_FAILED;
		}
		/* Process finished ERP request. */
		if (cqr->refers) {
			__dasd_process_erp(device, cqr);
			/* restart list_for_xx loop since dasd_process_erp
			 * might remove multiple elements */
			goto restart_cb;
		}
		/* call the callback function */
		cqr->endclk = get_clock();
		if (cqr->callback != NULL)
			(cqr->callback)(cqr, cqr->callback_data);
	}
	return rc;
}

/*
 * Acquire the device lock and process queues for the device.
 */
static void
dasd_tasklet(struct dasd_device * device)
{
	struct list_head final_queue;
	struct list_head *l, *n;
	struct dasd_ccw_req *cqr;

	atomic_set (&device->tasklet_scheduled, 0);
	INIT_LIST_HEAD(&final_queue);
	spin_lock_irq(get_ccwdev_lock(device->cdev));
	/* Check expire time of first request on the ccw queue. */
	__dasd_check_expire(device);
	/* Finish off requests on ccw queue */
	__dasd_process_ccw_queue(device, &final_queue);
	spin_unlock_irq(get_ccwdev_lock(device->cdev));
	/* Now call the callback function of requests with final status */
	list_for_each_safe(l, n, &final_queue) {
		cqr = list_entry(l, struct dasd_ccw_req, list);
		list_del_init(&cqr->list);
		if (cqr->callback != NULL)
			(cqr->callback)(cqr, cqr->callback_data);
	}
	spin_lock_irq(&device->request_queue_lock);
	spin_lock(get_ccwdev_lock(device->cdev));
	/* Get new request from the block device request queue */
	__dasd_process_blk_queue(device);
	/* Now check if the head of the ccw queue needs to be started. */
	__dasd_start_head(device);
	spin_unlock(get_ccwdev_lock(device->cdev));
	spin_unlock_irq(&device->request_queue_lock);
	dasd_put_device(device);
}

/*
 * Schedules a call to dasd_tasklet over the device tasklet.
 */
void
dasd_schedule_bh(struct dasd_device * device)
{
	/* Protect against rescheduling. */
	if (atomic_cmpxchg (&device->tasklet_scheduled, 0, 1) != 0)
		return;
	dasd_get_device(device);
	tasklet_hi_schedule(&device->tasklet);
}

/*
 * Queue a request to the head of the ccw_queue. Start the I/O if
 * possible.
 */
void
dasd_add_request_head(struct dasd_ccw_req *req)
{
	struct dasd_device *device;
	unsigned long flags;

	device = req->device;
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	req->status = DASD_CQR_QUEUED;
	req->device = device;
	list_add(&req->list, &device->ccw_queue);
	/* let the bh start the request to keep them in order */
	dasd_schedule_bh(device);
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
}

/*
 * Queue a request to the tail of the ccw_queue. Start the I/O if
 * possible.
 */
void
dasd_add_request_tail(struct dasd_ccw_req *req)
{
	struct dasd_device *device;
	unsigned long flags;

	device = req->device;
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	req->status = DASD_CQR_QUEUED;
	req->device = device;
	list_add_tail(&req->list, &device->ccw_queue);
	/* let the bh start the request to keep them in order */
	dasd_schedule_bh(device);
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
}

/*
 * Wakeup callback.
 */
static void
dasd_wakeup_cb(struct dasd_ccw_req *cqr, void *data)
{
	wake_up((wait_queue_head_t *) data);
}

static inline int
_wait_for_wakeup(struct dasd_ccw_req *cqr)
{
	struct dasd_device *device;
	int rc;

	device = cqr->device;
	spin_lock_irq(get_ccwdev_lock(device->cdev));
	rc = ((cqr->status == DASD_CQR_DONE ||
	       cqr->status == DASD_CQR_FAILED) &&
	      list_empty(&cqr->list));
	spin_unlock_irq(get_ccwdev_lock(device->cdev));
	return rc;
}

/*
 * Attempts to start a special ccw queue and waits for its completion.
 */
int
dasd_sleep_on(struct dasd_ccw_req * cqr)
{
	wait_queue_head_t wait_q;
	struct dasd_device *device;
	int rc;

	device = cqr->device;
	spin_lock_irq(get_ccwdev_lock(device->cdev));

	init_waitqueue_head (&wait_q);
	cqr->callback = dasd_wakeup_cb;
	cqr->callback_data = (void *) &wait_q;
	cqr->status = DASD_CQR_QUEUED;
	list_add_tail(&cqr->list, &device->ccw_queue);

	/* let the bh start the request to keep them in order */
	dasd_schedule_bh(device);

	spin_unlock_irq(get_ccwdev_lock(device->cdev));

	wait_event(wait_q, _wait_for_wakeup(cqr));

	/* Request status is either done or failed. */
	rc = (cqr->status == DASD_CQR_FAILED) ? -EIO : 0;
	return rc;
}

/*
 * Attempts to start a special ccw queue and wait interruptible
 * for its completion.
 */
int
dasd_sleep_on_interruptible(struct dasd_ccw_req * cqr)
{
	wait_queue_head_t wait_q;
	struct dasd_device *device;
	int rc, finished;

	device = cqr->device;
	spin_lock_irq(get_ccwdev_lock(device->cdev));

	init_waitqueue_head (&wait_q);
	cqr->callback = dasd_wakeup_cb;
	cqr->callback_data = (void *) &wait_q;
	cqr->status = DASD_CQR_QUEUED;
	list_add_tail(&cqr->list, &device->ccw_queue);

	/* let the bh start the request to keep them in order */
	dasd_schedule_bh(device);
	spin_unlock_irq(get_ccwdev_lock(device->cdev));

	finished = 0;
	while (!finished) {
		rc = wait_event_interruptible(wait_q, _wait_for_wakeup(cqr));
		if (rc != -ERESTARTSYS) {
			/* Request is final (done or failed) */
			rc = (cqr->status == DASD_CQR_DONE) ? 0 : -EIO;
			break;
		}
		spin_lock_irq(get_ccwdev_lock(device->cdev));
		switch (cqr->status) {
		case DASD_CQR_IN_IO:
                        /* terminate runnig cqr */
			if (device->discipline->term_IO) {
				cqr->retries = -1;
				device->discipline->term_IO(cqr);
				/* wait (non-interruptible) for final status
				 * because signal ist still pending */
				spin_unlock_irq(get_ccwdev_lock(device->cdev));
				wait_event(wait_q, _wait_for_wakeup(cqr));
				spin_lock_irq(get_ccwdev_lock(device->cdev));
				rc = (cqr->status == DASD_CQR_DONE) ? 0 : -EIO;
				finished = 1;
			}
			break;
		case DASD_CQR_QUEUED:
			/* request  */
			list_del_init(&cqr->list);
			rc = -EIO;
			finished = 1;
			break;
		default:
			/* cqr with 'non-interruptable' status - just wait */
			break;
		}
		spin_unlock_irq(get_ccwdev_lock(device->cdev));
	}
	return rc;
}

/*
 * Whoa nelly now it gets really hairy. For some functions (e.g. steal lock
 * for eckd devices) the currently running request has to be terminated
 * and be put back to status queued, before the special request is added
 * to the head of the queue. Then the special request is waited on normally.
 */
static inline int
_dasd_term_running_cqr(struct dasd_device *device)
{
	struct dasd_ccw_req *cqr;

	if (list_empty(&device->ccw_queue))
		return 0;
	cqr = list_entry(device->ccw_queue.next, struct dasd_ccw_req, list);
	return device->discipline->term_IO(cqr);
}

int
dasd_sleep_on_immediatly(struct dasd_ccw_req * cqr)
{
	wait_queue_head_t wait_q;
	struct dasd_device *device;
	int rc;

	device = cqr->device;
	spin_lock_irq(get_ccwdev_lock(device->cdev));
	rc = _dasd_term_running_cqr(device);
	if (rc) {
		spin_unlock_irq(get_ccwdev_lock(device->cdev));
		return rc;
	}

	init_waitqueue_head (&wait_q);
	cqr->callback = dasd_wakeup_cb;
	cqr->callback_data = (void *) &wait_q;
	cqr->status = DASD_CQR_QUEUED;
	list_add(&cqr->list, &device->ccw_queue);

	/* let the bh start the request to keep them in order */
	dasd_schedule_bh(device);

	spin_unlock_irq(get_ccwdev_lock(device->cdev));

	wait_event(wait_q, _wait_for_wakeup(cqr));

	/* Request status is either done or failed. */
	rc = (cqr->status == DASD_CQR_FAILED) ? -EIO : 0;
	return rc;
}

/*
 * Cancels a request that was started with dasd_sleep_on_req.
 * This is useful to timeout requests. The request will be
 * terminated if it is currently in i/o.
 * Returns 1 if the request has been terminated.
 */
int
dasd_cancel_req(struct dasd_ccw_req *cqr)
{
	struct dasd_device *device = cqr->device;
	unsigned long flags;
	int rc;

	rc = 0;
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	switch (cqr->status) {
	case DASD_CQR_QUEUED:
		/* request was not started - just set to failed */
		cqr->status = DASD_CQR_FAILED;
		break;
	case DASD_CQR_IN_IO:
		/* request in IO - terminate IO and release again */
		if (device->discipline->term_IO(cqr) != 0)
			/* what to do if unable to terminate ??????
			   e.g. not _IN_IO */
			cqr->status = DASD_CQR_FAILED;
		cqr->stopclk = get_clock();
		rc = 1;
		break;
	case DASD_CQR_DONE:
	case DASD_CQR_FAILED:
		/* already finished - do nothing */
		break;
	default:
		DEV_MESSAGE(KERN_ALERT, device,
			    "invalid status %02x in request",
			    cqr->status);
		BUG();

	}
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
	dasd_schedule_bh(device);
	return rc;
}

/*
 * SECTION: Block device operations (request queue, partitions, open, release).
 */

/*
 * Dasd request queue function. Called from ll_rw_blk.c
 */
static void
do_dasd_request(request_queue_t * queue)
{
	struct dasd_device *device;

	device = (struct dasd_device *) queue->queuedata;
	spin_lock(get_ccwdev_lock(device->cdev));
	/* Get new request from the block device request queue */
	__dasd_process_blk_queue(device);
	/* Now check if the head of the ccw queue needs to be started. */
	__dasd_start_head(device);
	spin_unlock(get_ccwdev_lock(device->cdev));
}

/*
 * Allocate and initialize request queue and default I/O scheduler.
 */
static int
dasd_alloc_queue(struct dasd_device * device)
{
	int rc;

	device->request_queue = blk_init_queue(do_dasd_request,
					       &device->request_queue_lock);
	if (device->request_queue == NULL)
		return -ENOMEM;

	device->request_queue->queuedata = device;

	elevator_exit(device->request_queue->elevator);
	rc = elevator_init(device->request_queue, "deadline");
	if (rc) {
		blk_cleanup_queue(device->request_queue);
		return rc;
	}
	return 0;
}

/*
 * Allocate and initialize request queue.
 */
static void
dasd_setup_queue(struct dasd_device * device)
{
	int max;

	blk_queue_hardsect_size(device->request_queue, device->bp_block);
	max = device->discipline->max_blocks << device->s2b_shift;
	blk_queue_max_sectors(device->request_queue, max);
	blk_queue_max_phys_segments(device->request_queue, -1L);
	blk_queue_max_hw_segments(device->request_queue, -1L);
	blk_queue_max_segment_size(device->request_queue, -1L);
	blk_queue_segment_boundary(device->request_queue, -1L);
	blk_queue_ordered(device->request_queue, QUEUE_ORDERED_TAG, NULL);
}

/*
 * Deactivate and free request queue.
 */
static void
dasd_free_queue(struct dasd_device * device)
{
	if (device->request_queue) {
		blk_cleanup_queue(device->request_queue);
		device->request_queue = NULL;
	}
}

/*
 * Flush request on the request queue.
 */
static void
dasd_flush_request_queue(struct dasd_device * device)
{
	struct request *req;

	if (!device->request_queue)
		return;

	spin_lock_irq(&device->request_queue_lock);
	while ((req = elv_next_request(device->request_queue))) {
		blkdev_dequeue_request(req);
		dasd_end_request(req, 0);
	}
	spin_unlock_irq(&device->request_queue_lock);
}

static int
dasd_open(struct inode *inp, struct file *filp)
{
	struct gendisk *disk = inp->i_bdev->bd_disk;
	struct dasd_device *device = disk->private_data;
	int rc;

        atomic_inc(&device->open_count);
	if (test_bit(DASD_FLAG_OFFLINE, &device->flags)) {
		rc = -ENODEV;
		goto unlock;
	}

	if (!try_module_get(device->discipline->owner)) {
		rc = -EINVAL;
		goto unlock;
	}

	if (dasd_probeonly) {
		DEV_MESSAGE(KERN_INFO, device, "%s",
			    "No access to device due to probeonly mode");
		rc = -EPERM;
		goto out;
	}

	if (device->state <= DASD_STATE_BASIC) {
		DBF_DEV_EVENT(DBF_ERR, device, " %s",
			      " Cannot open unrecognized device");
		rc = -ENODEV;
		goto out;
	}

	return 0;

out:
	module_put(device->discipline->owner);
unlock:
	atomic_dec(&device->open_count);
	return rc;
}

static int
dasd_release(struct inode *inp, struct file *filp)
{
	struct gendisk *disk = inp->i_bdev->bd_disk;
	struct dasd_device *device = disk->private_data;

	atomic_dec(&device->open_count);
	module_put(device->discipline->owner);
	return 0;
}

/*
 * Return disk geometry.
 */
static int
dasd_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct dasd_device *device;

	device = bdev->bd_disk->private_data;
	if (!device)
		return -ENODEV;

	if (!device->discipline ||
	    !device->discipline->fill_geometry)
		return -EINVAL;

	device->discipline->fill_geometry(device, geo);
	geo->start = get_start_sect(bdev) >> device->s2b_shift;
	return 0;
}

struct block_device_operations
dasd_device_operations = {
	.owner		= THIS_MODULE,
	.open		= dasd_open,
	.release	= dasd_release,
	.ioctl		= dasd_ioctl,
	.compat_ioctl	= dasd_compat_ioctl,
	.getgeo		= dasd_getgeo,
};


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
int
dasd_generic_probe (struct ccw_device *cdev,
		    struct dasd_discipline *discipline)
{
	int ret;

	ret = ccw_device_set_options(cdev, CCWDEV_DO_PATHGROUP);
	if (ret) {
		printk(KERN_WARNING
		       "dasd_generic_probe: could not set ccw-device options "
		       "for %s\n", cdev->dev.bus_id);
		return ret;
	}
	ret = dasd_add_sysfs_files(cdev);
	if (ret) {
		printk(KERN_WARNING
		       "dasd_generic_probe: could not add sysfs entries "
		       "for %s\n", cdev->dev.bus_id);
		return ret;
	}
	cdev->handler = &dasd_int_handler;

	/*
	 * Automatically online either all dasd devices (dasd_autodetect)
	 * or all devices specified with dasd= parameters during
	 * initial probe.
	 */
	if ((dasd_get_feature(cdev, DASD_FEATURE_INITIAL_ONLINE) > 0 ) ||
	    (dasd_autodetect && dasd_busid_known(cdev->dev.bus_id) != 0))
		ret = ccw_device_set_online(cdev);
	if (ret)
		printk(KERN_WARNING
		       "dasd_generic_probe: could not initially online "
		       "ccw-device %s\n", cdev->dev.bus_id);
	return ret;
}

/*
 * This will one day be called from a global not_oper handler.
 * It is also used by driver_unregister during module unload.
 */
void
dasd_generic_remove (struct ccw_device *cdev)
{
	struct dasd_device *device;

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
	dasd_delete_device(device);
}

/*
 * Activate a device. This is called from dasd_{eckd,fba}_probe() when either
 * the device is detected for the first time and is supposed to be used
 * or the user has started activation through sysfs.
 */
int
dasd_generic_set_online (struct ccw_device *cdev,
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
				cdev->dev.bus_id);
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

	rc = discipline->check_device(device);
	if (rc) {
		printk (KERN_WARNING
			"dasd_generic couldn't online device %s "
			"with discipline %s rc=%i\n",
			cdev->dev.bus_id, discipline->name, rc);
		module_put(discipline->owner);
		module_put(base_discipline->owner);
		dasd_delete_device(device);
		return rc;
	}

	dasd_set_target_state(device, DASD_STATE_ONLINE);
	if (device->state <= DASD_STATE_KNOWN) {
		printk (KERN_WARNING
			"dasd_generic discipline not found for %s\n",
			cdev->dev.bus_id);
		rc = -ENODEV;
		dasd_set_target_state(device, DASD_STATE_NEW);
		dasd_delete_device(device);
	} else
		pr_debug("dasd_generic device %s found\n",
				cdev->dev.bus_id);

	/* FIXME: we have to wait for the root device but we don't want
	 * to wait for each single device but for all at once. */
	wait_event(dasd_init_waitq, _wait_for_device(device));

	dasd_put_device(device);

	return rc;
}

int
dasd_generic_set_offline (struct ccw_device *cdev)
{
	struct dasd_device *device;
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
	max_count = device->bdev ? 0 : -1;
	open_count = (int) atomic_read(&device->open_count);
	if (open_count > max_count) {
		if (open_count > 0)
			printk (KERN_WARNING "Can't offline dasd device with "
				"open count = %i.\n",
				open_count);
		else
			printk (KERN_WARNING "%s",
				"Can't offline dasd device due to internal "
				"use\n");
		clear_bit(DASD_FLAG_OFFLINE, &device->flags);
		dasd_put_device(device);
		return -EBUSY;
	}
	dasd_set_target_state(device, DASD_STATE_NEW);
	/* dasd_delete_device destroys the device reference. */
	dasd_delete_device(device);

	return 0;
}

int
dasd_generic_notify(struct ccw_device *cdev, int event)
{
	struct dasd_device *device;
	struct dasd_ccw_req *cqr;
	unsigned long flags;
	int ret;

	device = dasd_device_from_cdev(cdev);
	if (IS_ERR(device))
		return 0;
	spin_lock_irqsave(get_ccwdev_lock(cdev), flags);
	ret = 0;
	switch (event) {
	case CIO_GONE:
	case CIO_NO_PATH:
		/* First of all call extended error reporting. */
		dasd_eer_write(device, NULL, DASD_EER_NOPATH);

		if (device->state < DASD_STATE_BASIC)
			break;
		/* Device is active. We want to keep it. */
		if (test_bit(DASD_FLAG_DSC_ERROR, &device->flags)) {
			list_for_each_entry(cqr, &device->ccw_queue, list)
				if (cqr->status == DASD_CQR_IN_IO)
					cqr->status = DASD_CQR_FAILED;
			device->stopped |= DASD_STOPPED_DC_EIO;
		} else {
			list_for_each_entry(cqr, &device->ccw_queue, list)
				if (cqr->status == DASD_CQR_IN_IO) {
					cqr->status = DASD_CQR_QUEUED;
					cqr->retries++;
				}
			device->stopped |= DASD_STOPPED_DC_WAIT;
			dasd_set_timer(device, 0);
		}
		dasd_schedule_bh(device);
		ret = 1;
		break;
	case CIO_OPER:
		/* FIXME: add a sanity check. */
		device->stopped &= ~(DASD_STOPPED_DC_WAIT|DASD_STOPPED_DC_EIO);
		dasd_schedule_bh(device);
		ret = 1;
		break;
	}
	spin_unlock_irqrestore(get_ccwdev_lock(cdev), flags);
	dasd_put_device(device);
	return ret;
}


static int __init
dasd_init(void)
{
	int rc;

	init_waitqueue_head(&dasd_init_waitq);
	init_waitqueue_head(&dasd_flush_wq);

	/* register 'common' DASD debug area, used for all DBF_XXX calls */
	dasd_debug_area = debug_register("dasd", 1, 2, 8 * sizeof (long));
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
EXPORT_SYMBOL(dasd_clear_timer);
EXPORT_SYMBOL(dasd_enable_device);
EXPORT_SYMBOL(dasd_int_handler);
EXPORT_SYMBOL(dasd_kfree_request);
EXPORT_SYMBOL(dasd_kick_device);
EXPORT_SYMBOL(dasd_kmalloc_request);
EXPORT_SYMBOL(dasd_schedule_bh);
EXPORT_SYMBOL(dasd_set_target_state);
EXPORT_SYMBOL(dasd_set_timer);
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

