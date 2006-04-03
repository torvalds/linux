/*
 * scsi_scan.c
 *
 * Copyright (C) 2000 Eric Youngdale,
 * Copyright (C) 2002 Patrick Mansfield
 *
 * The general scanning/probing algorithm is as follows, exceptions are
 * made to it depending on device specific flags, compilation options, and
 * global variable (boot or module load time) settings.
 *
 * A specific LUN is scanned via an INQUIRY command; if the LUN has a
 * device attached, a scsi_device is allocated and setup for it.
 *
 * For every id of every channel on the given host:
 *
 * 	Scan LUN 0; if the target responds to LUN 0 (even if there is no
 * 	device or storage attached to LUN 0):
 *
 * 		If LUN 0 has a device attached, allocate and setup a
 * 		scsi_device for it.
 *
 * 		If target is SCSI-3 or up, issue a REPORT LUN, and scan
 * 		all of the LUNs returned by the REPORT LUN; else,
 * 		sequentially scan LUNs up until some maximum is reached,
 * 		or a LUN is seen that cannot have a device attached to it.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <asm/semaphore.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_driver.h>
#include <scsi/scsi_devinfo.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_request.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_eh.h>

#include "scsi_priv.h"
#include "scsi_logging.h"

#define ALLOC_FAILURE_MSG	KERN_ERR "%s: Allocation failure during" \
	" SCSI scanning, some SCSI devices might not be configured\n"

/*
 * Default timeout
 */
#define SCSI_TIMEOUT (2*HZ)

/*
 * Prefix values for the SCSI id's (stored in driverfs name field)
 */
#define SCSI_UID_SER_NUM 'S'
#define SCSI_UID_UNKNOWN 'Z'

/*
 * Return values of some of the scanning functions.
 *
 * SCSI_SCAN_NO_RESPONSE: no valid response received from the target, this
 * includes allocation or general failures preventing IO from being sent.
 *
 * SCSI_SCAN_TARGET_PRESENT: target responded, but no device is available
 * on the given LUN.
 *
 * SCSI_SCAN_LUN_PRESENT: target responded, and a device is available on a
 * given LUN.
 */
#define SCSI_SCAN_NO_RESPONSE		0
#define SCSI_SCAN_TARGET_PRESENT	1
#define SCSI_SCAN_LUN_PRESENT		2

static const char *scsi_null_device_strs = "nullnullnullnull";

#define MAX_SCSI_LUNS	512

#ifdef CONFIG_SCSI_MULTI_LUN
static unsigned int max_scsi_luns = MAX_SCSI_LUNS;
#else
static unsigned int max_scsi_luns = 1;
#endif

module_param_named(max_luns, max_scsi_luns, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(max_luns,
		 "last scsi LUN (should be between 1 and 2^32-1)");

/*
 * max_scsi_report_luns: the maximum number of LUNS that will be
 * returned from the REPORT LUNS command. 8 times this value must
 * be allocated. In theory this could be up to an 8 byte value, but
 * in practice, the maximum number of LUNs suppored by any device
 * is about 16k.
 */
static unsigned int max_scsi_report_luns = 511;

module_param_named(max_report_luns, max_scsi_report_luns, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(max_report_luns,
		 "REPORT LUNS maximum number of LUNS received (should be"
		 " between 1 and 16384)");

static unsigned int scsi_inq_timeout = SCSI_TIMEOUT/HZ+3;

module_param_named(inq_timeout, scsi_inq_timeout, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(inq_timeout, 
		 "Timeout (in seconds) waiting for devices to answer INQUIRY."
		 " Default is 5. Some non-compliant devices need more.");

/**
 * scsi_unlock_floptical - unlock device via a special MODE SENSE command
 * @sdev:	scsi device to send command to
 * @result:	area to store the result of the MODE SENSE
 *
 * Description:
 *     Send a vendor specific MODE SENSE (not a MODE SELECT) command.
 *     Called for BLIST_KEY devices.
 **/
static void scsi_unlock_floptical(struct scsi_device *sdev,
				  unsigned char *result)
{
	unsigned char scsi_cmd[MAX_COMMAND_SIZE];

	printk(KERN_NOTICE "scsi: unlocking floptical drive\n");
	scsi_cmd[0] = MODE_SENSE;
	scsi_cmd[1] = 0;
	scsi_cmd[2] = 0x2e;
	scsi_cmd[3] = 0;
	scsi_cmd[4] = 0x2a;     /* size */
	scsi_cmd[5] = 0;
	scsi_execute_req(sdev, scsi_cmd, DMA_FROM_DEVICE, result, 0x2a, NULL,
			 SCSI_TIMEOUT, 3);
}

/**
 * print_inquiry - printk the inquiry information
 * @inq_result:	printk this SCSI INQUIRY
 *
 * Description:
 *     printk the vendor, model, and other information found in the
 *     INQUIRY data in @inq_result.
 *
 * Notes:
 *     Remove this, and replace with a hotplug event that logs any
 *     relevant information.
 **/
static void print_inquiry(unsigned char *inq_result)
{
	int i;

	printk(KERN_NOTICE "  Vendor: ");
	for (i = 8; i < 16; i++)
		if (inq_result[i] >= 0x20 && i < inq_result[4] + 5)
			printk("%c", inq_result[i]);
		else
			printk(" ");

	printk("  Model: ");
	for (i = 16; i < 32; i++)
		if (inq_result[i] >= 0x20 && i < inq_result[4] + 5)
			printk("%c", inq_result[i]);
		else
			printk(" ");

	printk("  Rev: ");
	for (i = 32; i < 36; i++)
		if (inq_result[i] >= 0x20 && i < inq_result[4] + 5)
			printk("%c", inq_result[i]);
		else
			printk(" ");

	printk("\n");

	i = inq_result[0] & 0x1f;

	printk(KERN_NOTICE "  Type:   %s ",
	       i <
	       MAX_SCSI_DEVICE_CODE ? scsi_device_types[i] :
	       "Unknown          ");
	printk("                 ANSI SCSI revision: %02x",
	       inq_result[2] & 0x07);
	if ((inq_result[2] & 0x07) == 1 && (inq_result[3] & 0x0f) == 1)
		printk(" CCS\n");
	else
		printk("\n");
}

/**
 * scsi_alloc_sdev - allocate and setup a scsi_Device
 *
 * Description:
 *     Allocate, initialize for io, and return a pointer to a scsi_Device.
 *     Stores the @shost, @channel, @id, and @lun in the scsi_Device, and
 *     adds scsi_Device to the appropriate list.
 *
 * Return value:
 *     scsi_Device pointer, or NULL on failure.
 **/
static struct scsi_device *scsi_alloc_sdev(struct scsi_target *starget,
					   unsigned int lun, void *hostdata)
{
	struct scsi_device *sdev;
	int display_failure_msg = 1, ret;
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);

	sdev = kzalloc(sizeof(*sdev) + shost->transportt->device_size,
		       GFP_ATOMIC);
	if (!sdev)
		goto out;

	sdev->vendor = scsi_null_device_strs;
	sdev->model = scsi_null_device_strs;
	sdev->rev = scsi_null_device_strs;
	sdev->host = shost;
	sdev->id = starget->id;
	sdev->lun = lun;
	sdev->channel = starget->channel;
	sdev->sdev_state = SDEV_CREATED;
	INIT_LIST_HEAD(&sdev->siblings);
	INIT_LIST_HEAD(&sdev->same_target_siblings);
	INIT_LIST_HEAD(&sdev->cmd_list);
	INIT_LIST_HEAD(&sdev->starved_entry);
	spin_lock_init(&sdev->list_lock);

	sdev->sdev_gendev.parent = get_device(&starget->dev);
	sdev->sdev_target = starget;

	/* usually NULL and set by ->slave_alloc instead */
	sdev->hostdata = hostdata;

	/* if the device needs this changing, it may do so in the
	 * slave_configure function */
	sdev->max_device_blocked = SCSI_DEFAULT_DEVICE_BLOCKED;

	/*
	 * Some low level driver could use device->type
	 */
	sdev->type = -1;

	/*
	 * Assume that the device will have handshaking problems,
	 * and then fix this field later if it turns out it
	 * doesn't
	 */
	sdev->borken = 1;

	sdev->request_queue = scsi_alloc_queue(sdev);
	if (!sdev->request_queue) {
		/* release fn is set up in scsi_sysfs_device_initialise, so
		 * have to free and put manually here */
		put_device(&starget->dev);
		kfree(sdev);
		goto out;
	}

	sdev->request_queue->queuedata = sdev;
	scsi_adjust_queue_depth(sdev, 0, sdev->host->cmd_per_lun);

	scsi_sysfs_device_initialize(sdev);

	if (shost->hostt->slave_alloc) {
		ret = shost->hostt->slave_alloc(sdev);
		if (ret) {
			/*
			 * if LLDD reports slave not present, don't clutter
			 * console with alloc failure messages
			 */
			if (ret == -ENXIO)
				display_failure_msg = 0;
			goto out_device_destroy;
		}
	}

	return sdev;

out_device_destroy:
	transport_destroy_device(&sdev->sdev_gendev);
	put_device(&sdev->sdev_gendev);
out:
	if (display_failure_msg)
		printk(ALLOC_FAILURE_MSG, __FUNCTION__);
	return NULL;
}

static void scsi_target_dev_release(struct device *dev)
{
	struct device *parent = dev->parent;
	struct scsi_target *starget = to_scsi_target(dev);

	kfree(starget);
	put_device(parent);
}

int scsi_is_target_device(const struct device *dev)
{
	return dev->release == scsi_target_dev_release;
}
EXPORT_SYMBOL(scsi_is_target_device);

static struct scsi_target *__scsi_find_target(struct device *parent,
					      int channel, uint id)
{
	struct scsi_target *starget, *found_starget = NULL;
	struct Scsi_Host *shost = dev_to_shost(parent);
	/*
	 * Search for an existing target for this sdev.
	 */
	list_for_each_entry(starget, &shost->__targets, siblings) {
		if (starget->id == id &&
		    starget->channel == channel) {
			found_starget = starget;
			break;
		}
	}
	if (found_starget)
		get_device(&found_starget->dev);

	return found_starget;
}

static struct scsi_target *scsi_alloc_target(struct device *parent,
					     int channel, uint id)
{
	struct Scsi_Host *shost = dev_to_shost(parent);
	struct device *dev = NULL;
	unsigned long flags;
	const int size = sizeof(struct scsi_target)
		+ shost->transportt->target_size;
	struct scsi_target *starget;
	struct scsi_target *found_target;
	int error;

	starget = kzalloc(size, GFP_KERNEL);
	if (!starget) {
		printk(KERN_ERR "%s: allocation failure\n", __FUNCTION__);
		return NULL;
	}
	dev = &starget->dev;
	device_initialize(dev);
	starget->reap_ref = 1;
	dev->parent = get_device(parent);
	dev->release = scsi_target_dev_release;
	sprintf(dev->bus_id, "target%d:%d:%d",
		shost->host_no, channel, id);
	starget->id = id;
	starget->channel = channel;
	INIT_LIST_HEAD(&starget->siblings);
	INIT_LIST_HEAD(&starget->devices);
	starget->state = STARGET_RUNNING;
 retry:
	spin_lock_irqsave(shost->host_lock, flags);

	found_target = __scsi_find_target(parent, channel, id);
	if (found_target)
		goto found;

	list_add_tail(&starget->siblings, &shost->__targets);
	spin_unlock_irqrestore(shost->host_lock, flags);
	/* allocate and add */
	transport_setup_device(dev);
	error = device_add(dev);
	if (error) {
		dev_err(dev, "target device_add failed, error %d\n", error);
		spin_lock_irqsave(shost->host_lock, flags);
		list_del_init(&starget->siblings);
		spin_unlock_irqrestore(shost->host_lock, flags);
		transport_destroy_device(dev);
		put_device(parent);
		kfree(starget);
		return NULL;
	}
	transport_add_device(dev);
	if (shost->hostt->target_alloc) {
		error = shost->hostt->target_alloc(starget);

		if(error) {
			dev_printk(KERN_ERR, dev, "target allocation failed, error %d\n", error);
			/* don't want scsi_target_reap to do the final
			 * put because it will be under the host lock */
			get_device(dev);
			scsi_target_reap(starget);
			put_device(dev);
			return NULL;
		}
	}

	return starget;

 found:
	found_target->reap_ref++;
	spin_unlock_irqrestore(shost->host_lock, flags);
	put_device(parent);
	if (found_target->state != STARGET_DEL) {
		kfree(starget);
		return found_target;
	}
	/* Unfortunately, we found a dying target; need to
	 * wait until it's dead before we can get a new one */
	put_device(&found_target->dev);
	flush_scheduled_work();
	goto retry;
}

static void scsi_target_reap_usercontext(void *data)
{
	struct scsi_target *starget = data;
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	unsigned long flags;

	transport_remove_device(&starget->dev);
	device_del(&starget->dev);
	transport_destroy_device(&starget->dev);
	spin_lock_irqsave(shost->host_lock, flags);
	if (shost->hostt->target_destroy)
		shost->hostt->target_destroy(starget);
	list_del_init(&starget->siblings);
	spin_unlock_irqrestore(shost->host_lock, flags);
	put_device(&starget->dev);
}

/**
 * scsi_target_reap - check to see if target is in use and destroy if not
 *
 * @starget: target to be checked
 *
 * This is used after removing a LUN or doing a last put of the target
 * it checks atomically that nothing is using the target and removes
 * it if so.
 */
void scsi_target_reap(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);

	if (--starget->reap_ref == 0 && list_empty(&starget->devices)) {
		BUG_ON(starget->state == STARGET_DEL);
		starget->state = STARGET_DEL;
		spin_unlock_irqrestore(shost->host_lock, flags);
		execute_in_process_context(scsi_target_reap_usercontext,
					   starget, &starget->ew);
		return;

	}
	spin_unlock_irqrestore(shost->host_lock, flags);

	return;
}

/**
 * scsi_probe_lun - probe a single LUN using a SCSI INQUIRY
 * @sdev:	scsi_device to probe
 * @inq_result:	area to store the INQUIRY result
 * @result_len: len of inq_result
 * @bflags:	store any bflags found here
 *
 * Description:
 *     Probe the lun associated with @req using a standard SCSI INQUIRY;
 *
 *     If the INQUIRY is successful, zero is returned and the
 *     INQUIRY data is in @inq_result; the scsi_level and INQUIRY length
 *     are copied to the scsi_device any flags value is stored in *@bflags.
 **/
static int scsi_probe_lun(struct scsi_device *sdev, char *inq_result,
			  int result_len, int *bflags)
{
	unsigned char scsi_cmd[MAX_COMMAND_SIZE];
	int first_inquiry_len, try_inquiry_len, next_inquiry_len;
	int response_len = 0;
	int pass, count, result;
	struct scsi_sense_hdr sshdr;

	*bflags = 0;

	/* Perform up to 3 passes.  The first pass uses a conservative
	 * transfer length of 36 unless sdev->inquiry_len specifies a
	 * different value. */
	first_inquiry_len = sdev->inquiry_len ? sdev->inquiry_len : 36;
	try_inquiry_len = first_inquiry_len;
	pass = 1;

 next_pass:
	SCSI_LOG_SCAN_BUS(3, sdev_printk(KERN_INFO, sdev,
				"scsi scan: INQUIRY pass %d length %d\n",
				pass, try_inquiry_len));

	/* Each pass gets up to three chances to ignore Unit Attention */
	for (count = 0; count < 3; ++count) {
		memset(scsi_cmd, 0, 6);
		scsi_cmd[0] = INQUIRY;
		scsi_cmd[4] = (unsigned char) try_inquiry_len;

		memset(inq_result, 0, try_inquiry_len);

		result = scsi_execute_req(sdev,  scsi_cmd, DMA_FROM_DEVICE,
					  inq_result, try_inquiry_len, &sshdr,
					  HZ / 2 + HZ * scsi_inq_timeout, 3);

		SCSI_LOG_SCAN_BUS(3, printk(KERN_INFO "scsi scan: INQUIRY %s "
				"with code 0x%x\n",
				result ? "failed" : "successful", result));

		if (result) {
			/*
			 * not-ready to ready transition [asc/ascq=0x28/0x0]
			 * or power-on, reset [asc/ascq=0x29/0x0], continue.
			 * INQUIRY should not yield UNIT_ATTENTION
			 * but many buggy devices do so anyway. 
			 */
			if ((driver_byte(result) & DRIVER_SENSE) &&
			    scsi_sense_valid(&sshdr)) {
				if ((sshdr.sense_key == UNIT_ATTENTION) &&
				    ((sshdr.asc == 0x28) ||
				     (sshdr.asc == 0x29)) &&
				    (sshdr.ascq == 0))
					continue;
			}
		}
		break;
	}

	if (result == 0) {
		response_len = (unsigned char) inq_result[4] + 5;
		if (response_len > 255)
			response_len = first_inquiry_len;	/* sanity */

		/*
		 * Get any flags for this device.
		 *
		 * XXX add a bflags to scsi_device, and replace the
		 * corresponding bit fields in scsi_device, so bflags
		 * need not be passed as an argument.
		 */
		*bflags = scsi_get_device_flags(sdev, &inq_result[8],
				&inq_result[16]);

		/* When the first pass succeeds we gain information about
		 * what larger transfer lengths might work. */
		if (pass == 1) {
			if (BLIST_INQUIRY_36 & *bflags)
				next_inquiry_len = 36;
			else if (BLIST_INQUIRY_58 & *bflags)
				next_inquiry_len = 58;
			else if (sdev->inquiry_len)
				next_inquiry_len = sdev->inquiry_len;
			else
				next_inquiry_len = response_len;

			/* If more data is available perform the second pass */
			if (next_inquiry_len > try_inquiry_len) {
				try_inquiry_len = next_inquiry_len;
				pass = 2;
				goto next_pass;
			}
		}

	} else if (pass == 2) {
		printk(KERN_INFO "scsi scan: %d byte inquiry failed.  "
				"Consider BLIST_INQUIRY_36 for this device\n",
				try_inquiry_len);

		/* If this pass failed, the third pass goes back and transfers
		 * the same amount as we successfully got in the first pass. */
		try_inquiry_len = first_inquiry_len;
		pass = 3;
		goto next_pass;
	}

	/* If the last transfer attempt got an error, assume the
	 * peripheral doesn't exist or is dead. */
	if (result)
		return -EIO;

	/* Don't report any more data than the device says is valid */
	sdev->inquiry_len = min(try_inquiry_len, response_len);

	/*
	 * XXX Abort if the response length is less than 36? If less than
	 * 32, the lookup of the device flags (above) could be invalid,
	 * and it would be possible to take an incorrect action - we do
	 * not want to hang because of a short INQUIRY. On the flip side,
	 * if the device is spun down or becoming ready (and so it gives a
	 * short INQUIRY), an abort here prevents any further use of the
	 * device, including spin up.
	 *
	 * Related to the above issue:
	 *
	 * XXX Devices (disk or all?) should be sent a TEST UNIT READY,
	 * and if not ready, sent a START_STOP to start (maybe spin up) and
	 * then send the INQUIRY again, since the INQUIRY can change after
	 * a device is initialized.
	 *
	 * Ideally, start a device if explicitly asked to do so.  This
	 * assumes that a device is spun up on power on, spun down on
	 * request, and then spun up on request.
	 */

	/*
	 * The scanning code needs to know the scsi_level, even if no
	 * device is attached at LUN 0 (SCSI_SCAN_TARGET_PRESENT) so
	 * non-zero LUNs can be scanned.
	 */
	sdev->scsi_level = inq_result[2] & 0x07;
	if (sdev->scsi_level >= 2 ||
	    (sdev->scsi_level == 1 && (inq_result[3] & 0x0f) == 1))
		sdev->scsi_level++;
	sdev->sdev_target->scsi_level = sdev->scsi_level;

	return 0;
}

/**
 * scsi_add_lun - allocate and fully initialze a scsi_device
 * @sdevscan:	holds information to be stored in the new scsi_device
 * @sdevnew:	store the address of the newly allocated scsi_device
 * @inq_result:	holds the result of a previous INQUIRY to the LUN
 * @bflags:	black/white list flag
 *
 * Description:
 *     Allocate and initialize a scsi_device matching sdevscan. Optionally
 *     set fields based on values in *@bflags. If @sdevnew is not
 *     NULL, store the address of the new scsi_device in *@sdevnew (needed
 *     when scanning a particular LUN).
 *
 * Return:
 *     SCSI_SCAN_NO_RESPONSE: could not allocate or setup a scsi_device
 *     SCSI_SCAN_LUN_PRESENT: a new scsi_device was allocated and initialized
 **/
static int scsi_add_lun(struct scsi_device *sdev, char *inq_result, int *bflags)
{
	/*
	 * XXX do not save the inquiry, since it can change underneath us,
	 * save just vendor/model/rev.
	 *
	 * Rather than save it and have an ioctl that retrieves the saved
	 * value, have an ioctl that executes the same INQUIRY code used
	 * in scsi_probe_lun, let user level programs doing INQUIRY
	 * scanning run at their own risk, or supply a user level program
	 * that can correctly scan.
	 */
	sdev->inquiry = kmalloc(sdev->inquiry_len, GFP_ATOMIC);
	if (sdev->inquiry == NULL) {
		return SCSI_SCAN_NO_RESPONSE;
	}

	memcpy(sdev->inquiry, inq_result, sdev->inquiry_len);
	sdev->vendor = (char *) (sdev->inquiry + 8);
	sdev->model = (char *) (sdev->inquiry + 16);
	sdev->rev = (char *) (sdev->inquiry + 32);

	if (*bflags & BLIST_ISROM) {
		/*
		 * It would be better to modify sdev->type, and set
		 * sdev->removable, but then the print_inquiry() output
		 * would not show TYPE_ROM; if print_inquiry() is removed
		 * the issue goes away.
		 */
		inq_result[0] = TYPE_ROM;
		inq_result[1] |= 0x80;	/* removable */
	} else if (*bflags & BLIST_NO_ULD_ATTACH)
		sdev->no_uld_attach = 1;

	switch (sdev->type = (inq_result[0] & 0x1f)) {
	case TYPE_TAPE:
	case TYPE_DISK:
	case TYPE_PRINTER:
	case TYPE_MOD:
	case TYPE_PROCESSOR:
	case TYPE_SCANNER:
	case TYPE_MEDIUM_CHANGER:
	case TYPE_ENCLOSURE:
	case TYPE_COMM:
	case TYPE_RBC:
		sdev->writeable = 1;
		break;
	case TYPE_WORM:
	case TYPE_ROM:
		sdev->writeable = 0;
		break;
	default:
		printk(KERN_INFO "scsi: unknown device type %d\n", sdev->type);
	}

	print_inquiry(inq_result);

	/*
	 * For a peripheral qualifier (PQ) value of 1 (001b), the SCSI
	 * spec says: The device server is capable of supporting the
	 * specified peripheral device type on this logical unit. However,
	 * the physical device is not currently connected to this logical
	 * unit.
	 *
	 * The above is vague, as it implies that we could treat 001 and
	 * 011 the same. Stay compatible with previous code, and create a
	 * scsi_device for a PQ of 1
	 *
	 * Don't set the device offline here; rather let the upper
	 * level drivers eval the PQ to decide whether they should
	 * attach. So remove ((inq_result[0] >> 5) & 7) == 1 check.
	 */ 

	sdev->inq_periph_qual = (inq_result[0] >> 5) & 7;
	sdev->removable = (0x80 & inq_result[1]) >> 7;
	sdev->lockable = sdev->removable;
	sdev->soft_reset = (inq_result[7] & 1) && ((inq_result[3] & 7) == 2);

	if (sdev->scsi_level >= SCSI_3 || (sdev->inquiry_len > 56 &&
		inq_result[56] & 0x04))
		sdev->ppr = 1;
	if (inq_result[7] & 0x60)
		sdev->wdtr = 1;
	if (inq_result[7] & 0x10)
		sdev->sdtr = 1;

	/*
	 * End sysfs code.
	 */

	if ((sdev->scsi_level >= SCSI_2) && (inq_result[7] & 2) &&
	    !(*bflags & BLIST_NOTQ))
		sdev->tagged_supported = 1;
	/*
	 * Some devices (Texel CD ROM drives) have handshaking problems
	 * when used with the Seagate controllers. borken is initialized
	 * to 1, and then set it to 0 here.
	 */
	if ((*bflags & BLIST_BORKEN) == 0)
		sdev->borken = 0;

	/*
	 * Apparently some really broken devices (contrary to the SCSI
	 * standards) need to be selected without asserting ATN
	 */
	if (*bflags & BLIST_SELECT_NO_ATN)
		sdev->select_no_atn = 1;

	/*
	 * Some devices may not want to have a start command automatically
	 * issued when a device is added.
	 */
	if (*bflags & BLIST_NOSTARTONADD)
		sdev->no_start_on_add = 1;

	if (*bflags & BLIST_SINGLELUN)
		sdev->single_lun = 1;


	sdev->use_10_for_rw = 1;

	if (*bflags & BLIST_MS_SKIP_PAGE_08)
		sdev->skip_ms_page_8 = 1;

	if (*bflags & BLIST_MS_SKIP_PAGE_3F)
		sdev->skip_ms_page_3f = 1;

	if (*bflags & BLIST_USE_10_BYTE_MS)
		sdev->use_10_for_ms = 1;

	/* set the device running here so that slave configure
	 * may do I/O */
	scsi_device_set_state(sdev, SDEV_RUNNING);

	if (*bflags & BLIST_MS_192_BYTES_FOR_3F)
		sdev->use_192_bytes_for_3f = 1;

	if (*bflags & BLIST_NOT_LOCKABLE)
		sdev->lockable = 0;

	if (*bflags & BLIST_RETRY_HWERROR)
		sdev->retry_hwerror = 1;

	transport_configure_device(&sdev->sdev_gendev);

	if (sdev->host->hostt->slave_configure) {
		int ret = sdev->host->hostt->slave_configure(sdev);
		if (ret) {
			/*
			 * if LLDD reports slave not present, don't clutter
			 * console with alloc failure messages
			 */
			if (ret != -ENXIO) {
				sdev_printk(KERN_ERR, sdev,
					"failed to configure device\n");
			}
			return SCSI_SCAN_NO_RESPONSE;
		}
	}

	/*
	 * Ok, the device is now all set up, we can
	 * register it and tell the rest of the kernel
	 * about it.
	 */
	if (scsi_sysfs_add_sdev(sdev) != 0)
		return SCSI_SCAN_NO_RESPONSE;

	return SCSI_SCAN_LUN_PRESENT;
}

static inline void scsi_destroy_sdev(struct scsi_device *sdev)
{
	if (sdev->host->hostt->slave_destroy)
		sdev->host->hostt->slave_destroy(sdev);
	transport_destroy_device(&sdev->sdev_gendev);
	put_device(&sdev->sdev_gendev);
}


/**
 * scsi_probe_and_add_lun - probe a LUN, if a LUN is found add it
 * @starget:	pointer to target device structure
 * @lun:	LUN of target device
 * @sdevscan:	probe the LUN corresponding to this scsi_device
 * @sdevnew:	store the value of any new scsi_device allocated
 * @bflagsp:	store bflags here if not NULL
 *
 * Description:
 *     Call scsi_probe_lun, if a LUN with an attached device is found,
 *     allocate and set it up by calling scsi_add_lun.
 *
 * Return:
 *     SCSI_SCAN_NO_RESPONSE: could not allocate or setup a scsi_device
 *     SCSI_SCAN_TARGET_PRESENT: target responded, but no device is
 *         attached at the LUN
 *     SCSI_SCAN_LUN_PRESENT: a new scsi_device was allocated and initialized
 **/
static int scsi_probe_and_add_lun(struct scsi_target *starget,
				  uint lun, int *bflagsp,
				  struct scsi_device **sdevp, int rescan,
				  void *hostdata)
{
	struct scsi_device *sdev;
	unsigned char *result;
	int bflags, res = SCSI_SCAN_NO_RESPONSE, result_len = 256;
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);

	/*
	 * The rescan flag is used as an optimization, the first scan of a
	 * host adapter calls into here with rescan == 0.
	 */
	sdev = scsi_device_lookup_by_target(starget, lun);
	if (sdev) {
		if (rescan || sdev->sdev_state != SDEV_CREATED) {
			SCSI_LOG_SCAN_BUS(3, printk(KERN_INFO
				"scsi scan: device exists on %s\n",
				sdev->sdev_gendev.bus_id));
			if (sdevp)
				*sdevp = sdev;
			else
				scsi_device_put(sdev);

			if (bflagsp)
				*bflagsp = scsi_get_device_flags(sdev,
								 sdev->vendor,
								 sdev->model);
			return SCSI_SCAN_LUN_PRESENT;
		}
		scsi_device_put(sdev);
	} else
		sdev = scsi_alloc_sdev(starget, lun, hostdata);
	if (!sdev)
		goto out;

	result = kmalloc(result_len, GFP_ATOMIC |
			((shost->unchecked_isa_dma) ? __GFP_DMA : 0));
	if (!result)
		goto out_free_sdev;

	if (scsi_probe_lun(sdev, result, result_len, &bflags))
		goto out_free_result;

	if (bflagsp)
		*bflagsp = bflags;
	/*
	 * result contains valid SCSI INQUIRY data.
	 */
	if ((result[0] >> 5) == 3) {
		/*
		 * For a Peripheral qualifier 3 (011b), the SCSI
		 * spec says: The device server is not capable of
		 * supporting a physical device on this logical
		 * unit.
		 *
		 * For disks, this implies that there is no
		 * logical disk configured at sdev->lun, but there
		 * is a target id responding.
		 */
		SCSI_LOG_SCAN_BUS(3, printk(KERN_INFO
					"scsi scan: peripheral qualifier of 3,"
					" no device added\n"));
		res = SCSI_SCAN_TARGET_PRESENT;
		goto out_free_result;
	}

	/*
	 * Non-standard SCSI targets may set the PDT to 0x1f (unknown or
	 * no device type) instead of using the Peripheral Qualifier to
	 * indicate that no LUN is present.  For example, USB UFI does this.
	 */
	if (starget->pdt_1f_for_no_lun && (result[0] & 0x1f) == 0x1f) {
		SCSI_LOG_SCAN_BUS(3, printk(KERN_INFO
					"scsi scan: peripheral device type"
					" of 31, no device added\n"));
		res = SCSI_SCAN_TARGET_PRESENT;
		goto out_free_result;
	}

	res = scsi_add_lun(sdev, result, &bflags);
	if (res == SCSI_SCAN_LUN_PRESENT) {
		if (bflags & BLIST_KEY) {
			sdev->lockable = 0;
			scsi_unlock_floptical(sdev, result);
		}
	}

 out_free_result:
	kfree(result);
 out_free_sdev:
	if (res == SCSI_SCAN_LUN_PRESENT) {
		if (sdevp) {
			if (scsi_device_get(sdev) == 0) {
				*sdevp = sdev;
			} else {
				__scsi_remove_device(sdev);
				res = SCSI_SCAN_NO_RESPONSE;
			}
		}
	} else
		scsi_destroy_sdev(sdev);
 out:
	return res;
}

/**
 * scsi_sequential_lun_scan - sequentially scan a SCSI target
 * @starget:	pointer to target structure to scan
 * @bflags:	black/white list flag for LUN 0
 *
 * Description:
 *     Generally, scan from LUN 1 (LUN 0 is assumed to already have been
 *     scanned) to some maximum lun until a LUN is found with no device
 *     attached. Use the bflags to figure out any oddities.
 *
 *     Modifies sdevscan->lun.
 **/
static void scsi_sequential_lun_scan(struct scsi_target *starget,
				     int bflags, int scsi_level, int rescan)
{
	unsigned int sparse_lun, lun, max_dev_lun;
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);

	SCSI_LOG_SCAN_BUS(3, printk(KERN_INFO "scsi scan: Sequential scan of"
				    "%s\n", starget->dev.bus_id));

	max_dev_lun = min(max_scsi_luns, shost->max_lun);
	/*
	 * If this device is known to support sparse multiple units,
	 * override the other settings, and scan all of them. Normally,
	 * SCSI-3 devices should be scanned via the REPORT LUNS.
	 */
	if (bflags & BLIST_SPARSELUN) {
		max_dev_lun = shost->max_lun;
		sparse_lun = 1;
	} else
		sparse_lun = 0;

	/*
	 * If less than SCSI_1_CSS, and no special lun scaning, stop
	 * scanning; this matches 2.4 behaviour, but could just be a bug
	 * (to continue scanning a SCSI_1_CSS device).
	 *
	 * This test is broken.  We might not have any device on lun0 for
	 * a sparselun device, and if that's the case then how would we
	 * know the real scsi_level, eh?  It might make sense to just not
	 * scan any SCSI_1 device for non-0 luns, but that check would best
	 * go into scsi_alloc_sdev() and just have it return null when asked
	 * to alloc an sdev for lun > 0 on an already found SCSI_1 device.
	 *
	if ((sdevscan->scsi_level < SCSI_1_CCS) &&
	    ((bflags & (BLIST_FORCELUN | BLIST_SPARSELUN | BLIST_MAX5LUN))
	     == 0))
		return;
	 */
	/*
	 * If this device is known to support multiple units, override
	 * the other settings, and scan all of them.
	 */
	if (bflags & BLIST_FORCELUN)
		max_dev_lun = shost->max_lun;
	/*
	 * REGAL CDC-4X: avoid hang after LUN 4
	 */
	if (bflags & BLIST_MAX5LUN)
		max_dev_lun = min(5U, max_dev_lun);
	/*
	 * Do not scan SCSI-2 or lower device past LUN 7, unless
	 * BLIST_LARGELUN.
	 */
	if (scsi_level < SCSI_3 && !(bflags & BLIST_LARGELUN))
		max_dev_lun = min(8U, max_dev_lun);

	/*
	 * We have already scanned LUN 0, so start at LUN 1. Keep scanning
	 * until we reach the max, or no LUN is found and we are not
	 * sparse_lun.
	 */
	for (lun = 1; lun < max_dev_lun; ++lun)
		if ((scsi_probe_and_add_lun(starget, lun, NULL, NULL, rescan,
					    NULL) != SCSI_SCAN_LUN_PRESENT) &&
		    !sparse_lun)
			return;
}

/**
 * scsilun_to_int: convert a scsi_lun to an int
 * @scsilun:	struct scsi_lun to be converted.
 *
 * Description:
 *     Convert @scsilun from a struct scsi_lun to a four byte host byte-ordered
 *     integer, and return the result. The caller must check for
 *     truncation before using this function.
 *
 * Notes:
 *     The struct scsi_lun is assumed to be four levels, with each level
 *     effectively containing a SCSI byte-ordered (big endian) short; the
 *     addressing bits of each level are ignored (the highest two bits).
 *     For a description of the LUN format, post SCSI-3 see the SCSI
 *     Architecture Model, for SCSI-3 see the SCSI Controller Commands.
 *
 *     Given a struct scsi_lun of: 0a 04 0b 03 00 00 00 00, this function returns
 *     the integer: 0x0b030a04
 **/
static int scsilun_to_int(struct scsi_lun *scsilun)
{
	int i;
	unsigned int lun;

	lun = 0;
	for (i = 0; i < sizeof(lun); i += 2)
		lun = lun | (((scsilun->scsi_lun[i] << 8) |
			      scsilun->scsi_lun[i + 1]) << (i * 8));
	return lun;
}

/**
 * int_to_scsilun: reverts an int into a scsi_lun
 * @int:        integer to be reverted
 * @scsilun:	struct scsi_lun to be set.
 *
 * Description:
 *     Reverts the functionality of the scsilun_to_int, which packed
 *     an 8-byte lun value into an int. This routine unpacks the int
 *     back into the lun value.
 *     Note: the scsilun_to_int() routine does not truly handle all
 *     8bytes of the lun value. This functions restores only as much
 *     as was set by the routine.
 *
 * Notes:
 *     Given an integer : 0x0b030a04,  this function returns a
 *     scsi_lun of : struct scsi_lun of: 0a 04 0b 03 00 00 00 00
 *
 **/
void int_to_scsilun(unsigned int lun, struct scsi_lun *scsilun)
{
	int i;

	memset(scsilun->scsi_lun, 0, sizeof(scsilun->scsi_lun));

	for (i = 0; i < sizeof(lun); i += 2) {
		scsilun->scsi_lun[i] = (lun >> 8) & 0xFF;
		scsilun->scsi_lun[i+1] = lun & 0xFF;
		lun = lun >> 16;
	}
}
EXPORT_SYMBOL(int_to_scsilun);

/**
 * scsi_report_lun_scan - Scan using SCSI REPORT LUN results
 * @sdevscan:	scan the host, channel, and id of this scsi_device
 *
 * Description:
 *     If @sdevscan is for a SCSI-3 or up device, send a REPORT LUN
 *     command, and scan the resulting list of LUNs by calling
 *     scsi_probe_and_add_lun.
 *
 *     Modifies sdevscan->lun.
 *
 * Return:
 *     0: scan completed (or no memory, so further scanning is futile)
 *     1: no report lun scan, or not configured
 **/
static int scsi_report_lun_scan(struct scsi_target *starget, int bflags,
				int rescan)
{
	char devname[64];
	unsigned char scsi_cmd[MAX_COMMAND_SIZE];
	unsigned int length;
	unsigned int lun;
	unsigned int num_luns;
	unsigned int retries;
	int result;
	struct scsi_lun *lunp, *lun_data;
	u8 *data;
	struct scsi_sense_hdr sshdr;
	struct scsi_device *sdev;
	struct Scsi_Host *shost = dev_to_shost(&starget->dev);
	int ret = 0;

	/*
	 * Only support SCSI-3 and up devices if BLIST_NOREPORTLUN is not set.
	 * Also allow SCSI-2 if BLIST_REPORTLUN2 is set and host adapter does
	 * support more than 8 LUNs.
	 */
	if ((bflags & BLIST_NOREPORTLUN) || 
	     starget->scsi_level < SCSI_2 ||
	    (starget->scsi_level < SCSI_3 && 
	     (!(bflags & BLIST_REPORTLUN2) || shost->max_lun <= 8)) )
		return 1;
	if (bflags & BLIST_NOLUN)
		return 0;

	if (!(sdev = scsi_device_lookup_by_target(starget, 0))) {
		sdev = scsi_alloc_sdev(starget, 0, NULL);
		if (!sdev)
			return 0;
		if (scsi_device_get(sdev))
			return 0;
	}

	sprintf(devname, "host %d channel %d id %d",
		shost->host_no, sdev->channel, sdev->id);

	/*
	 * Allocate enough to hold the header (the same size as one scsi_lun)
	 * plus the max number of luns we are requesting.
	 *
	 * Reallocating and trying again (with the exact amount we need)
	 * would be nice, but then we need to somehow limit the size
	 * allocated based on the available memory and the limits of
	 * kmalloc - we don't want a kmalloc() failure of a huge value to
	 * prevent us from finding any LUNs on this target.
	 */
	length = (max_scsi_report_luns + 1) * sizeof(struct scsi_lun);
	lun_data = kmalloc(length, GFP_ATOMIC |
			   (sdev->host->unchecked_isa_dma ? __GFP_DMA : 0));
	if (!lun_data) {
		printk(ALLOC_FAILURE_MSG, __FUNCTION__);
		goto out;
	}

	scsi_cmd[0] = REPORT_LUNS;

	/*
	 * bytes 1 - 5: reserved, set to zero.
	 */
	memset(&scsi_cmd[1], 0, 5);

	/*
	 * bytes 6 - 9: length of the command.
	 */
	scsi_cmd[6] = (unsigned char) (length >> 24) & 0xff;
	scsi_cmd[7] = (unsigned char) (length >> 16) & 0xff;
	scsi_cmd[8] = (unsigned char) (length >> 8) & 0xff;
	scsi_cmd[9] = (unsigned char) length & 0xff;

	scsi_cmd[10] = 0;	/* reserved */
	scsi_cmd[11] = 0;	/* control */

	/*
	 * We can get a UNIT ATTENTION, for example a power on/reset, so
	 * retry a few times (like sd.c does for TEST UNIT READY).
	 * Experience shows some combinations of adapter/devices get at
	 * least two power on/resets.
	 *
	 * Illegal requests (for devices that do not support REPORT LUNS)
	 * should come through as a check condition, and will not generate
	 * a retry.
	 */
	for (retries = 0; retries < 3; retries++) {
		SCSI_LOG_SCAN_BUS(3, printk (KERN_INFO "scsi scan: Sending"
				" REPORT LUNS to %s (try %d)\n", devname,
				retries));

		result = scsi_execute_req(sdev, scsi_cmd, DMA_FROM_DEVICE,
					  lun_data, length, &sshdr,
					  SCSI_TIMEOUT + 4 * HZ, 3);

		SCSI_LOG_SCAN_BUS(3, printk (KERN_INFO "scsi scan: REPORT LUNS"
				" %s (try %d) result 0x%x\n", result
				?  "failed" : "successful", retries, result));
		if (result == 0)
			break;
		else if (scsi_sense_valid(&sshdr)) {
			if (sshdr.sense_key != UNIT_ATTENTION)
				break;
		}
	}

	if (result) {
		/*
		 * The device probably does not support a REPORT LUN command
		 */
		ret = 1;
		goto out_err;
	}

	/*
	 * Get the length from the first four bytes of lun_data.
	 */
	data = (u8 *) lun_data->scsi_lun;
	length = ((data[0] << 24) | (data[1] << 16) |
		  (data[2] << 8) | (data[3] << 0));

	num_luns = (length / sizeof(struct scsi_lun));
	if (num_luns > max_scsi_report_luns) {
		printk(KERN_WARNING "scsi: On %s only %d (max_scsi_report_luns)"
		       " of %d luns reported, try increasing"
		       " max_scsi_report_luns.\n", devname,
		       max_scsi_report_luns, num_luns);
		num_luns = max_scsi_report_luns;
	}

	SCSI_LOG_SCAN_BUS(3, sdev_printk (KERN_INFO, sdev,
		"scsi scan: REPORT LUN scan\n"));

	/*
	 * Scan the luns in lun_data. The entry at offset 0 is really
	 * the header, so start at 1 and go up to and including num_luns.
	 */
	for (lunp = &lun_data[1]; lunp <= &lun_data[num_luns]; lunp++) {
		lun = scsilun_to_int(lunp);

		/*
		 * Check if the unused part of lunp is non-zero, and so
		 * does not fit in lun.
		 */
		if (memcmp(&lunp->scsi_lun[sizeof(lun)], "\0\0\0\0", 4)) {
			int i;

			/*
			 * Output an error displaying the LUN in byte order,
			 * this differs from what linux would print for the
			 * integer LUN value.
			 */
			printk(KERN_WARNING "scsi: %s lun 0x", devname);
			data = (char *)lunp->scsi_lun;
			for (i = 0; i < sizeof(struct scsi_lun); i++)
				printk("%02x", data[i]);
			printk(" has a LUN larger than currently supported.\n");
		} else if (lun > sdev->host->max_lun) {
			printk(KERN_WARNING "scsi: %s lun%d has a LUN larger"
			       " than allowed by the host adapter\n",
			       devname, lun);
		} else {
			int res;

			res = scsi_probe_and_add_lun(starget,
				lun, NULL, NULL, rescan, NULL);
			if (res == SCSI_SCAN_NO_RESPONSE) {
				/*
				 * Got some results, but now none, abort.
				 */
				sdev_printk(KERN_ERR, sdev,
					"Unexpected response"
				        " from lun %d while scanning, scan"
				        " aborted\n", lun);
				break;
			}
		}
	}

 out_err:
	kfree(lun_data);
 out:
	scsi_device_put(sdev);
	if (sdev->sdev_state == SDEV_CREATED)
		/*
		 * the sdev we used didn't appear in the report luns scan
		 */
		scsi_destroy_sdev(sdev);
	return ret;
}

struct scsi_device *__scsi_add_device(struct Scsi_Host *shost, uint channel,
				      uint id, uint lun, void *hostdata)
{
	struct scsi_device *sdev = ERR_PTR(-ENODEV);
	struct device *parent = &shost->shost_gendev;
	struct scsi_target *starget;

	starget = scsi_alloc_target(parent, channel, id);
	if (!starget)
		return ERR_PTR(-ENOMEM);

	get_device(&starget->dev);
	mutex_lock(&shost->scan_mutex);
	if (scsi_host_scan_allowed(shost))
		scsi_probe_and_add_lun(starget, lun, NULL, &sdev, 1, hostdata);
	mutex_unlock(&shost->scan_mutex);
	scsi_target_reap(starget);
	put_device(&starget->dev);

	return sdev;
}
EXPORT_SYMBOL(__scsi_add_device);

int scsi_add_device(struct Scsi_Host *host, uint channel,
		    uint target, uint lun)
{
	struct scsi_device *sdev = 
		__scsi_add_device(host, channel, target, lun, NULL);
	if (IS_ERR(sdev))
		return PTR_ERR(sdev);

	scsi_device_put(sdev);
	return 0;
}
EXPORT_SYMBOL(scsi_add_device);

void scsi_rescan_device(struct device *dev)
{
	struct scsi_driver *drv;
	
	if (!dev->driver)
		return;

	drv = to_scsi_driver(dev->driver);
	if (try_module_get(drv->owner)) {
		if (drv->rescan)
			drv->rescan(dev);
		module_put(drv->owner);
	}
}
EXPORT_SYMBOL(scsi_rescan_device);

static void __scsi_scan_target(struct device *parent, unsigned int channel,
		unsigned int id, unsigned int lun, int rescan)
{
	struct Scsi_Host *shost = dev_to_shost(parent);
	int bflags = 0;
	int res;
	struct scsi_target *starget;

	if (shost->this_id == id)
		/*
		 * Don't scan the host adapter
		 */
		return;

	starget = scsi_alloc_target(parent, channel, id);
	if (!starget)
		return;

	get_device(&starget->dev);
	if (lun != SCAN_WILD_CARD) {
		/*
		 * Scan for a specific host/chan/id/lun.
		 */
		scsi_probe_and_add_lun(starget, lun, NULL, NULL, rescan, NULL);
		goto out_reap;
	}

	/*
	 * Scan LUN 0, if there is some response, scan further. Ideally, we
	 * would not configure LUN 0 until all LUNs are scanned.
	 */
	res = scsi_probe_and_add_lun(starget, 0, &bflags, NULL, rescan, NULL);
	if (res == SCSI_SCAN_LUN_PRESENT || res == SCSI_SCAN_TARGET_PRESENT) {
		if (scsi_report_lun_scan(starget, bflags, rescan) != 0)
			/*
			 * The REPORT LUN did not scan the target,
			 * do a sequential scan.
			 */
			scsi_sequential_lun_scan(starget, bflags,
						 starget->scsi_level, rescan);
	}

 out_reap:
	/* now determine if the target has any children at all
	 * and if not, nuke it */
	scsi_target_reap(starget);

	put_device(&starget->dev);
}

/**
 * scsi_scan_target - scan a target id, possibly including all LUNs on the
 *     target.
 * @parent:	host to scan
 * @channel:	channel to scan
 * @id:		target id to scan
 * @lun:	Specific LUN to scan or SCAN_WILD_CARD
 * @rescan:	passed to LUN scanning routines
 *
 * Description:
 *     Scan the target id on @parent, @channel, and @id. Scan at least LUN 0,
 *     and possibly all LUNs on the target id.
 *
 *     First try a REPORT LUN scan, if that does not scan the target, do a
 *     sequential scan of LUNs on the target id.
 **/
void scsi_scan_target(struct device *parent, unsigned int channel,
		      unsigned int id, unsigned int lun, int rescan)
{
	struct Scsi_Host *shost = dev_to_shost(parent);

	mutex_lock(&shost->scan_mutex);
	if (scsi_host_scan_allowed(shost))
		__scsi_scan_target(parent, channel, id, lun, rescan);
	mutex_unlock(&shost->scan_mutex);
}
EXPORT_SYMBOL(scsi_scan_target);

static void scsi_scan_channel(struct Scsi_Host *shost, unsigned int channel,
			      unsigned int id, unsigned int lun, int rescan)
{
	uint order_id;

	if (id == SCAN_WILD_CARD)
		for (id = 0; id < shost->max_id; ++id) {
			/*
			 * XXX adapter drivers when possible (FCP, iSCSI)
			 * could modify max_id to match the current max,
			 * not the absolute max.
			 *
			 * XXX add a shost id iterator, so for example,
			 * the FC ID can be the same as a target id
			 * without a huge overhead of sparse id's.
			 */
			if (shost->reverse_ordering)
				/*
				 * Scan from high to low id.
				 */
				order_id = shost->max_id - id - 1;
			else
				order_id = id;
			__scsi_scan_target(&shost->shost_gendev, channel,
					order_id, lun, rescan);
		}
	else
		__scsi_scan_target(&shost->shost_gendev, channel,
				id, lun, rescan);
}

int scsi_scan_host_selected(struct Scsi_Host *shost, unsigned int channel,
			    unsigned int id, unsigned int lun, int rescan)
{
	SCSI_LOG_SCAN_BUS(3, shost_printk (KERN_INFO, shost,
		"%s: <%u:%u:%u>\n",
		__FUNCTION__, channel, id, lun));

	if (((channel != SCAN_WILD_CARD) && (channel > shost->max_channel)) ||
	    ((id != SCAN_WILD_CARD) && (id > shost->max_id)) ||
	    ((lun != SCAN_WILD_CARD) && (lun > shost->max_lun)))
		return -EINVAL;

	mutex_lock(&shost->scan_mutex);
	if (scsi_host_scan_allowed(shost)) {
		if (channel == SCAN_WILD_CARD)
			for (channel = 0; channel <= shost->max_channel;
			     channel++)
				scsi_scan_channel(shost, channel, id, lun,
						  rescan);
		else
			scsi_scan_channel(shost, channel, id, lun, rescan);
	}
	mutex_unlock(&shost->scan_mutex);

	return 0;
}

/**
 * scsi_scan_host - scan the given adapter
 * @shost:	adapter to scan
 **/
void scsi_scan_host(struct Scsi_Host *shost)
{
	scsi_scan_host_selected(shost, SCAN_WILD_CARD, SCAN_WILD_CARD,
				SCAN_WILD_CARD, 0);
}
EXPORT_SYMBOL(scsi_scan_host);

void scsi_forget_host(struct Scsi_Host *shost)
{
	struct scsi_device *sdev;
	unsigned long flags;

 restart:
	spin_lock_irqsave(shost->host_lock, flags);
	list_for_each_entry(sdev, &shost->__devices, siblings) {
		if (sdev->sdev_state == SDEV_DEL)
			continue;
		spin_unlock_irqrestore(shost->host_lock, flags);
		__scsi_remove_device(sdev);
		goto restart;
	}
	spin_unlock_irqrestore(shost->host_lock, flags);
}

/*
 * Function:    scsi_get_host_dev()
 *
 * Purpose:     Create a scsi_device that points to the host adapter itself.
 *
 * Arguments:   SHpnt   - Host that needs a scsi_device
 *
 * Lock status: None assumed.
 *
 * Returns:     The scsi_device or NULL
 *
 * Notes:
 *	Attach a single scsi_device to the Scsi_Host - this should
 *	be made to look like a "pseudo-device" that points to the
 *	HA itself.
 *
 *	Note - this device is not accessible from any high-level
 *	drivers (including generics), which is probably not
 *	optimal.  We can add hooks later to attach 
 */
struct scsi_device *scsi_get_host_dev(struct Scsi_Host *shost)
{
	struct scsi_device *sdev = NULL;
	struct scsi_target *starget;

	mutex_lock(&shost->scan_mutex);
	if (!scsi_host_scan_allowed(shost))
		goto out;
	starget = scsi_alloc_target(&shost->shost_gendev, 0, shost->this_id);
	if (!starget)
		goto out;

	sdev = scsi_alloc_sdev(starget, 0, NULL);
	if (sdev) {
		sdev->sdev_gendev.parent = get_device(&starget->dev);
		sdev->borken = 0;
	}
	put_device(&starget->dev);
 out:
	mutex_unlock(&shost->scan_mutex);
	return sdev;
}
EXPORT_SYMBOL(scsi_get_host_dev);

/*
 * Function:    scsi_free_host_dev()
 *
 * Purpose:     Free a scsi_device that points to the host adapter itself.
 *
 * Arguments:   SHpnt   - Host that needs a scsi_device
 *
 * Lock status: None assumed.
 *
 * Returns:     Nothing
 *
 * Notes:
 */
void scsi_free_host_dev(struct scsi_device *sdev)
{
	BUG_ON(sdev->id != sdev->host->this_id);

	scsi_destroy_sdev(sdev);
}
EXPORT_SYMBOL(scsi_free_host_dev);

