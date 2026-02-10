// SPDX-License-Identifier: GPL-2.0
/*
 *    tape device discipline for 3490 tapes.
 *
 *    Copyright IBM Corp. 2001, 2009
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *		 Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#define pr_fmt(fmt) "tape_3490: " fmt

#include <linux/export.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/workqueue.h>
#include <linux/slab.h>

#define TAPE_DBF_AREA	tape_3490_dbf

#include "tape.h"
#include "tape_std.h"

/*
 * Pointer to debug area.
 */
debug_info_t *TAPE_DBF_AREA = NULL;
EXPORT_SYMBOL(TAPE_DBF_AREA);

struct tape_3490_block_id {
	unsigned int	unused		: 10;
	unsigned int	block		: 22;
};

/*
 * Medium sense for 3490 tapes. There is no 'real' medium sense call.
 * So we just do a normal sense.
 */
static void __tape_3490_medium_sense(struct tape_request *request)
{
	struct tape_device *device = request->device;
	unsigned char *sense;

	if (request->rc == 0) {
		sense = request->cpdata;

		/*
		 * This isn't quite correct. But since INTERVENTION_REQUIRED
		 * means that the drive is 'neither ready nor on-line' it is
		 * only slightly inaccurate to say there is no tape loaded if
		 * the drive isn't online...
		 */
		if (sense[0] & SENSE_INTERVENTION_REQUIRED)
			tape_med_state_set(device, MS_UNLOADED);
		else
			tape_med_state_set(device, MS_LOADED);

		if (sense[1] & SENSE_WRITE_PROTECT)
			device->tape_generic_status |= GMT_WR_PROT(~0);
		else
			device->tape_generic_status &= ~GMT_WR_PROT(~0);
	} else
		DBF_EVENT(4, "tape_3490: medium sense failed with rc=%d\n",
			request->rc);
	tape_free_request(request);
}

static int tape_3490_medium_sense(struct tape_device *device)
{
	struct tape_request *request;
	int rc;

	request = tape_alloc_request(1, 32);
	if (IS_ERR(request)) {
		DBF_EXCEPTION(6, "MSEN fail\n");
		return PTR_ERR(request);
	}

	request->op = TO_MSEN;
	tape_ccw_end(request->cpaddr, SENSE, 32, request->cpdata);
	rc = tape_do_io_interruptible(device, request);
	__tape_3490_medium_sense(request);
	return rc;
}

static void tape_3490_medium_sense_async(struct tape_device *device)
{
	struct tape_request *request;

	request = tape_alloc_request(1, 32);
	if (IS_ERR(request)) {
		DBF_EXCEPTION(6, "MSEN fail\n");
		return;
	}

	request->op = TO_MSEN;
	tape_ccw_end(request->cpaddr, SENSE, 32, request->cpdata);
	request->callback = (void *) __tape_3490_medium_sense;
	request->callback_data = NULL;
	tape_do_io_async(device, request);
}

struct tape_3490_work {
	struct tape_device	*device;
	enum tape_op		 op;
	struct work_struct	 work;
};

/*
 * These functions are currently used only to schedule a medium_sense for
 * later execution. This is because we get an interrupt whenever a medium
 * is inserted but cannot call tape_do_io* from an interrupt context.
 * Maybe that's useful for other actions we want to start from the
 * interrupt handler.
 * Note: the work handler is called by the system work queue. The tape
 * commands started by the handler need to be asynchrounous, otherwise
 * a deadlock can occur e.g. in case of a deferred cc=1 (see __tape_do_irq).
 */
static void
tape_3490_work_handler(struct work_struct *work)
{
	struct tape_3490_work *p =
		container_of(work, struct tape_3490_work, work);
	struct tape_device *device = p->device;

	switch(p->op) {
		case TO_MSEN:
			tape_3490_medium_sense_async(device);
			break;
		default:
			DBF_EVENT(3, "T3490: internal error: unknown work\n");
	}
	tape_put_device(device);
	kfree(p);
}

static int
tape_3490_schedule_work(struct tape_device *device, enum tape_op op)
{
	struct tape_3490_work *p;

	if ((p = kzalloc(sizeof(*p), GFP_ATOMIC)) == NULL)
		return -ENOMEM;

	INIT_WORK(&p->work, tape_3490_work_handler);

	p->device = tape_get_device(device);
	p->op     = op;

	schedule_work(&p->work);
	return 0;
}

/*
 * Done Handler is called when dev stat = DEVICE-END (successful operation)
 */
static inline int
tape_3490_done(struct tape_request *request)
{
	DBF_EVENT(6, "%s done\n", tape_op_verbose[request->op]);
	return TAPE_IO_SUCCESS;
}

static inline int
tape_3490_erp_failed(struct tape_request *request, int rc)
{
	DBF_EVENT(3, "Error recovery failed for %s (RC=%d)\n",
		  tape_op_verbose[request->op], rc);
	return rc;
}

static inline int
tape_3490_erp_succeeded(struct tape_request *request)
{
	DBF_EVENT(3, "Error Recovery successful for %s\n",
		  tape_op_verbose[request->op]);
	return tape_3490_done(request);
}

static inline int
tape_3490_erp_retry(struct tape_request *request)
{
	DBF_EVENT(3, "xerp retr %s\n", tape_op_verbose[request->op]);
	return TAPE_IO_RETRY;
}

/*
 * This function is called, when no request is outstanding and we get an
 * interrupt
 */
static int
tape_3490_unsolicited_irq(struct tape_device *device, struct irb *irb)
{
	if (irb->scsw.cmd.dstat == 0x85) { /* READY */
		/* A medium was inserted in the drive. */
		DBF_EVENT(6, "xuud med\n");
		tape_3490_schedule_work(device, TO_MSEN);
	} else {
		DBF_EVENT(3, "unsol.irq! dev end: %08x\n", device->cdev_id);
		tape_dump_sense_dbf(device, NULL, irb);
	}
	return TAPE_IO_SUCCESS;
}

static int
tape_3490_erp_bug(struct tape_device *device, struct tape_request *request,
		  struct irb *irb, int no)
{
	if (request->op != TO_ASSIGN) {
		dev_err(&device->cdev->dev, "An unexpected condition %d "
			"occurred in tape error recovery\n", no);
		tape_dump_sense_dbf(device, request, irb);
	}
	return tape_3490_erp_failed(request, -EIO);
}

/*
 * Handle data overrun between cu and drive. The channel speed might
 * be too slow.
 */
static int
tape_3490_erp_overrun(struct tape_device *device, struct tape_request *request,
		      struct irb *irb)
{
	if (irb->ecw[3] == 0x40) {
		dev_warn (&device->cdev->dev, "A data overrun occurred between"
			" the control unit and tape unit\n");
		return tape_3490_erp_failed(request, -EIO);
	}
	return tape_3490_erp_bug(device, request, irb, -1);
}

/*
 * Handle record sequence error.
 */
static int
tape_3490_erp_sequence(struct tape_device *device,
		       struct tape_request *request, struct irb *irb)
{
	if (irb->ecw[3] == 0x41) {
		/*
		 * cu detected incorrect block-id sequence on tape.
		 */
		dev_warn (&device->cdev->dev, "The block ID sequence on the "
			"tape is incorrect\n");
		return tape_3490_erp_failed(request, -EIO);
	}
	/*
	 * Record sequence error bit is set, but erpa does not
	 * show record sequence error.
	 */
	return tape_3490_erp_bug(device, request, irb, -2);
}

/*
 * This function analyses the tape's sense-data in case of a unit-check.
 * If possible, it tries to recover from the error. Else the user is
 * informed about the problem.
 */
static int
tape_3490_unit_check(struct tape_device *device, struct tape_request *request,
		     struct irb *irb)
{
	int inhibit_cu_recovery;
	__u8* sense;

	inhibit_cu_recovery = (*device->modeset_byte & 0x80) ? 1 : 0;
	sense = irb->ecw;

	if (
		sense[0] & SENSE_COMMAND_REJECT &&
		sense[1] & SENSE_WRITE_PROTECT
	) {
		if (
			request->op == TO_DSE ||
			request->op == TO_WRI ||
			request->op == TO_WTM
		) {
			/* medium is write protected */
			return tape_3490_erp_failed(request, -EACCES);
		} else {
			return tape_3490_erp_bug(device, request, irb, -3);
		}
	}

	/*
	 * Special cases for various tape-states when reaching
	 * end of recorded area
	 *
	 * FIXME: Maybe a special case of the special case:
	 *        sense[0] == SENSE_EQUIPMENT_CHECK &&
	 *        sense[1] == SENSE_DRIVE_ONLINE    &&
	 *        sense[3] == 0x47 (Volume Fenced)
	 *
	 *        This was caused by continued FSF or FSR after an
	 *        'End Of Data'.
	 */
	if ((
		sense[0] == SENSE_DATA_CHECK      ||
		sense[0] == SENSE_EQUIPMENT_CHECK ||
		sense[0] == (SENSE_EQUIPMENT_CHECK | SENSE_DEFERRED_UNIT_CHECK)
	) && (
		sense[1] == SENSE_DRIVE_ONLINE ||
		sense[1] == (SENSE_BEGINNING_OF_TAPE | SENSE_WRITE_MODE)
	)) {
		switch (request->op) {
		/*
		 * sense[0] == SENSE_DATA_CHECK   &&
		 * sense[1] == SENSE_DRIVE_ONLINE
		 * sense[3] == 0x36 (End Of Data)
		 *
		 * Further seeks might return a 'Volume Fenced'.
		 */
		case TO_FSF:
		case TO_FSB:
			/* Trying to seek beyond end of recorded area */
			return tape_3490_erp_failed(request, -ENOSPC);
		case TO_BSB:
			return tape_3490_erp_retry(request);

		/*
		 * sense[0] == SENSE_DATA_CHECK   &&
		 * sense[1] == SENSE_DRIVE_ONLINE &&
		 * sense[3] == 0x36 (End Of Data)
		 */
		case TO_LBL:
			/* Block could not be located. */
			return tape_3490_erp_failed(request, -EIO);

		case TO_RFO:
			/* Read beyond end of recorded area -> 0 bytes read */
			return tape_3490_erp_failed(request, 0);

		/*
		 * sense[0] == SENSE_EQUIPMENT_CHECK &&
		 * sense[1] == SENSE_DRIVE_ONLINE    &&
		 * sense[3] == 0x38 (Physical End Of Volume)
		 */
		case TO_WRI:
			/* Writing at physical end of volume */
			return tape_3490_erp_failed(request, -ENOSPC);
		default:
			return tape_3490_erp_failed(request, 0);
		}
	}

	/* Sensing special bits */
	if (sense[0] & SENSE_BUS_OUT_CHECK)
		return tape_3490_erp_retry(request);

	if (sense[0] & SENSE_DATA_CHECK) {
		/*
		 * hardware failure, damaged tape or improper
		 * operating conditions
		 */
		switch (sense[3]) {
		case 0x23:
			/* a read data check occurred */
			if ((sense[2] & SENSE_TAPE_SYNC_MODE) ||
			    inhibit_cu_recovery)
				// data check is not permanent, may be
				// recovered. We always use async-mode with
				// cu-recovery, so this should *never* happen.
				return tape_3490_erp_bug(device, request,
							 irb, -4);

			/* data check is permanent, CU recovery has failed */
			dev_warn (&device->cdev->dev, "A read error occurred "
				"that cannot be recovered\n");
			return tape_3490_erp_failed(request, -EIO);
		case 0x25:
			// a write data check occurred
			if ((sense[2] & SENSE_TAPE_SYNC_MODE) ||
			    inhibit_cu_recovery)
				// data check is not permanent, may be
				// recovered. We always use async-mode with
				// cu-recovery, so this should *never* happen.
				return tape_3490_erp_bug(device, request,
							 irb, -5);

			// data check is permanent, cu-recovery has failed
			dev_warn (&device->cdev->dev, "A write error on the "
				"tape cannot be recovered\n");
			return tape_3490_erp_failed(request, -EIO);
		case 0x28:
			/* ID-Mark at tape start couldn't be written */
			dev_warn (&device->cdev->dev, "Writing the ID-mark "
				"failed\n");
			return tape_3490_erp_failed(request, -EIO);
		case 0x31:
			/* Tape void. Tried to read beyond end of device. */
			dev_warn (&device->cdev->dev, "Reading the tape beyond"
				" the end of the recorded area failed\n");
			return tape_3490_erp_failed(request, -ENOSPC);
		case 0x41:
			/* Record sequence error. */
			dev_warn (&device->cdev->dev, "The tape contains an "
				"incorrect block ID sequence\n");
			return tape_3490_erp_failed(request, -EIO);
		}
	}

	if (sense[0] & SENSE_OVERRUN)
		return tape_3490_erp_overrun(device, request, irb);

	if (sense[1] & SENSE_RECORD_SEQUENCE_ERR)
		return tape_3490_erp_sequence(device, request, irb);

	/* Sensing erpa codes */
	switch (sense[3]) {
	case 0x00:
		/* Unit check with erpa code 0. Report and ignore. */
		return TAPE_IO_SUCCESS;
	case 0x27:
		/*
		 * Command reject. May indicate illegal channel program or
		 * buffer over/underrun. Since all channel programs are
		 * issued by this driver and ought be correct, we assume a
		 * over/underrun situation and retry the channel program.
		 */
		return tape_3490_erp_retry(request);
	case 0x29:
		/*
		 * Function incompatible. Either the tape is idrc compressed
		 * but the hardware isn't capable to do idrc, or a perform
		 * subsystem func is issued and the CU is not on-line.
		 */
		return tape_3490_erp_failed(request, -EIO);
	case 0x2b:
		/*
		 * Environmental data present. Indicates either unload
		 * completed ok or read buffered log command completed ok.
		 */
		if (request->op == TO_RUN) {
			/* Rewind unload completed ok. */
			tape_med_state_set(device, MS_UNLOADED);
			return tape_3490_erp_succeeded(request);
		}
		/* tape_3490 doesn't use read buffered log commands. */
		return tape_3490_erp_bug(device, request, irb, sense[3]);
	case 0x2c:
		/*
		 * Permanent equipment check. CU has tried recovery, but
		 * did not succeed.
		 */
		return tape_3490_erp_failed(request, -EIO);
	case 0x2d:
		/* Data security erase failure. */
		if (request->op == TO_DSE)
			return tape_3490_erp_failed(request, -EIO);
		/* Data security erase failure, but no such command issued. */
		return tape_3490_erp_bug(device, request, irb, sense[3]);
	case 0x2e:
		/*
		 * Not capable. This indicates either that the drive fails
		 * reading the format id mark or that format specified
		 * is not supported by the drive.
		 */
		dev_warn (&device->cdev->dev, "The tape unit cannot process "
			"the tape format\n");
		return tape_3490_erp_failed(request, -EMEDIUMTYPE);
	case 0x30:
		/* The medium is write protected. */
		dev_warn (&device->cdev->dev, "The tape medium is write-"
			"protected\n");
		return tape_3490_erp_failed(request, -EACCES);
	case 0x35:
		/*
		 * Drive equipment check. One of the following:
		 * - cu cannot recover from a drive detected error
		 * - a check code message is shown on drive display
		 * - the cartridge loader does not respond correctly
		 * - a failure occurs during an index, load, or unload cycle
		 */
		dev_warn (&device->cdev->dev, "An equipment check has occurred"
			" on the tape unit\n");
		return tape_3490_erp_failed(request, -EIO);
	case 0x36:
		/* End of data. */
		return tape_3490_erp_failed(request, -EIO);
	case 0x38:
		/*
		 * Physical end of tape. A read/write operation reached
		 * the physical end of tape.
		 */
		if (request->op==TO_WRI ||
		    request->op==TO_DSE ||
		    request->op==TO_WTM)
			return tape_3490_erp_failed(request, -ENOSPC);
		return tape_3490_erp_failed(request, -EIO);
	case 0x39:
		/* Backward at Beginning of tape. */
		return tape_3490_erp_failed(request, -EIO);
	case 0x42:
		/*
		 * Degraded mode. A condition that can cause degraded
		 * performance is detected.
		 */
		dev_warn (&device->cdev->dev, "The tape subsystem is running "
			"in degraded mode\n");
		return tape_3490_erp_retry(request);
	case 0x43:
		/* Drive not ready. */
		tape_med_state_set(device, MS_UNLOADED);
		/* Some commands commands are successful even in this case */
		if (sense[1] & SENSE_DRIVE_ONLINE) {
			switch(request->op) {
				case TO_ASSIGN:
				case TO_UNASSIGN:
				case TO_DIS:
				case TO_NOP:
					return tape_3490_done(request);
					break;
				default:
					break;
			}
		}
		return tape_3490_erp_failed(request, -ENOMEDIUM);
	case 0x44:
		/* Locate Block unsuccessful. */
		if (request->op != TO_BLOCK && request->op != TO_LBL)
			/* No locate block was issued. */
			return tape_3490_erp_bug(device, request,
						 irb, sense[3]);
		return tape_3490_erp_failed(request, -EIO);
	case 0x45:
		/* The drive is assigned to a different channel path. */
		dev_warn (&device->cdev->dev, "The tape unit is already "
			"assigned\n");
		return tape_3490_erp_failed(request, -EIO);
	case 0x47:
		/* Volume fenced. CU reports volume integrity is lost. */
		dev_warn (&device->cdev->dev, "The control unit has fenced "
			"access to the tape volume\n");
		return tape_3490_erp_failed(request, -EIO);
	case 0x48:
		/* Log sense data and retry request. */
		return tape_3490_erp_retry(request);
	case 0x4d:
		/*
		 * Resetting event received. Since the driver does
		 * not support resetting event recovery (which has to
		 * be handled by the I/O Layer), retry our command.
		 */
		return tape_3490_erp_retry(request);
	case 0x4e:
		/*
		 * Maximum block size exceeded. This indicates, that
		 * the block to be written is larger than allowed for
		 * buffered mode.
		 */
		dev_warn (&device->cdev->dev,
			  "The maximum block size for buffered mode is exceeded\n");
		return tape_3490_erp_failed(request, -ENOBUFS);
	case 0x50:
		/*
		 * Read buffered log (Overflow). CU is running in extended
		 * buffered log mode, and a counter overflows. This should
		 * never happen, since we're never running in extended
		 * buffered log mode.
		 */
		return tape_3490_erp_retry(request);
	case 0x51:
		/*
		 * Read buffered log (EOV). EOF processing occurs while the
		 * CU is in extended buffered log mode. This should never
		 * happen, since we're never running in extended buffered
		 * log mode.
		 */
		return tape_3490_erp_retry(request);
	case 0x52:
		/* End of Volume complete. Rewind unload completed ok. */
		if (request->op == TO_RUN) {
			tape_med_state_set(device, MS_UNLOADED);
			return tape_3490_erp_succeeded(request);
		}
		return tape_3490_erp_bug(device, request, irb, sense[3]);
	case 0x53:
		/* Global command intercept. */
		return tape_3490_erp_retry(request);
	case 0x54:
		/* Channel interface recovery (temporary). */
		return tape_3490_erp_retry(request);
	case 0x55:
		/* Channel interface recovery (permanent). */
		dev_warn (&device->cdev->dev, "A channel interface error cannot be"
			" recovered\n");
		return tape_3490_erp_failed(request, -EIO);
	case 0x56:
		/* Channel protocol error. */
		dev_warn (&device->cdev->dev, "A channel protocol error "
			"occurred\n");
		return tape_3490_erp_failed(request, -EIO);
	case 0x57:
		/* Global status intercept. */
		return tape_3490_erp_retry(request);
		/* The following erpas should have been covered earlier. */
	case 0x23: /* Read data check. */
	case 0x25: /* Write data check. */
	case 0x28: /* Write id mark check. */
	case 0x31: /* Tape void. */
	case 0x40: /* Overrun error. */
	case 0x41: /* Record sequence error. */
		/* All other erpas are reserved for future use. */
	default:
		return tape_3490_erp_bug(device, request, irb, sense[3]);
	}
}

/*
 * 3490 interrupt handler
 */
static int
tape_3490_irq(struct tape_device *device, struct tape_request *request,
	      struct irb *irb)
{
	if (request == NULL)
		return tape_3490_unsolicited_irq(device, irb);

	if ((irb->scsw.cmd.dstat & DEV_STAT_UNIT_EXCEP) &&
	    (irb->scsw.cmd.dstat & DEV_STAT_DEV_END) &&
	    (request->op == TO_WRI)) {
		/* Write at end of volume */
		return tape_3490_erp_failed(request, -ENOSPC);
	}

	if (irb->scsw.cmd.dstat & DEV_STAT_UNIT_CHECK)
		return tape_3490_unit_check(device, request, irb);

	if (irb->scsw.cmd.dstat & DEV_STAT_DEV_END) {
		/*
		 * A unit exception occurs on skipping over a tapemark block.
		 */
		if (irb->scsw.cmd.dstat & DEV_STAT_UNIT_EXCEP) {
			if (request->op == TO_BSB || request->op == TO_FSB)
				request->rescnt++;
			else
				DBF_EVENT(5, "Unit Exception!\n");
		}
		return tape_3490_done(request);
	}

	DBF_EVENT(6, "xunknownirq\n");
	tape_dump_sense_dbf(device, request, irb);
	return TAPE_IO_STOP;
}

static int
tape_3490_setup_device(struct tape_device * device)
{
	int rc;

	DBF_EVENT(6, "3490 device setup\n");
	if ((rc = tape_std_assign(device)) == 0) {
		if ((rc = tape_3490_medium_sense(device)) != 0) {
			DBF_LH(3, "3490 medium sense returned %d\n", rc);
		}
	}
	return rc;
}

static void
tape_3490_cleanup_device(struct tape_device *device)
{
	tape_std_unassign(device);
}


/*
 * MTTELL: Tell block. Return the number of block relative to current file.
 */
static int
tape_3490_mttell(struct tape_device *device, int mt_count)
{
	struct {
		struct tape_3490_block_id	cbid;
		struct tape_3490_block_id	dbid;
	} __attribute__ ((packed)) block_id;
	int rc;

	rc = tape_std_read_block_id(device, (__u64 *) &block_id);
	if (rc)
		return rc;

	return block_id.cbid.block;
}

/*
 * MTSEEK: seek to the specified block.
 */
static int
tape_3490_mtseek(struct tape_device *device, int mt_count)
{
	struct tape_request *request;
	struct tape_3490_block_id *	bid;

	if (mt_count > 0x3fffff) {
		DBF_EXCEPTION(6, "xsee parm\n");
		return -EINVAL;
	}
	request = tape_alloc_request(3, 4);
	if (IS_ERR(request))
		return PTR_ERR(request);

	/* setup ccws */
	request->op = TO_LBL;
	bid         = (struct tape_3490_block_id *) request->cpdata;
	bid->block  = mt_count;

	tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1, device->modeset_byte);
	tape_ccw_cc(request->cpaddr + 1, LOCATE, 4, request->cpdata);
	tape_ccw_end(request->cpaddr + 2, NOP, 0, NULL);

	/* execute it */
	return tape_do_io_free(device, request);
}

/*
 * List of 3490 tape commands.
 */
static tape_mtop_fn tape_3490_mtop[TAPE_NR_MTOPS] = {
	[MTRESET]	 = tape_std_mtreset,
	[MTFSF]		 = tape_std_mtfsf,
	[MTBSF]		 = tape_std_mtbsf,
	[MTFSR]		 = tape_std_mtfsr,
	[MTBSR]		 = tape_std_mtbsr,
	[MTWEOF]	 = tape_std_mtweof,
	[MTREW]		 = tape_std_mtrew,
	[MTOFFL]	 = tape_std_mtoffl,
	[MTNOP]		 = tape_std_mtnop,
	[MTRETEN]	 = tape_std_mtreten,
	[MTBSFM]	 = tape_std_mtbsfm,
	[MTFSFM]	 = tape_std_mtfsfm,
	[MTEOM]		 = tape_std_mteom,
	[MTERASE]	 = tape_std_mterase,
	[MTRAS1]	 = NULL,
	[MTRAS2]	 = NULL,
	[MTRAS3]	 = NULL,
	[MTSETBLK]	 = tape_std_mtsetblk,
	[MTSETDENSITY]	 = NULL,
	[MTSEEK]	 = tape_3490_mtseek,
	[MTTELL]	 = tape_3490_mttell,
	[MTSETDRVBUFFER] = NULL,
	[MTFSS]		 = NULL,
	[MTBSS]		 = NULL,
	[MTWSM]		 = NULL,
	[MTLOCK]	 = NULL,
	[MTUNLOCK]	 = NULL,
	[MTLOAD]	 = tape_std_mtload,
	[MTUNLOAD]	 = tape_std_mtunload,
	[MTCOMPRESSION]	 = tape_std_mtcompression,
	[MTSETPART]	 = NULL,
	[MTMKPART]	 = NULL
};

/*
 * Tape discipline structure for 3490.
 */
static struct tape_discipline tape_discipline_3490 = {
	.owner = THIS_MODULE,
	.setup_device = tape_3490_setup_device,
	.cleanup_device = tape_3490_cleanup_device,
	.process_eov = tape_std_process_eov,
	.irq = tape_3490_irq,
	.read_block = tape_std_read_block,
	.write_block = tape_std_write_block,
	.mtop_array = tape_3490_mtop
};

static struct ccw_device_id tape_3490_ids[] = {
	{ CCW_DEVICE_DEVTYPE(0x3490, 0, 0x3490, 0), .driver_info = tape_3490},
	{ /* end of list */ },
};

static int
tape_3490_online(struct ccw_device *cdev)
{
	return tape_generic_online(
		dev_get_drvdata(&cdev->dev),
		&tape_discipline_3490
	);
}

static struct ccw_driver tape_3490_driver = {
	.driver = {
		.name = "tape_3490",
		.owner = THIS_MODULE,
	},
	.ids = tape_3490_ids,
	.probe = tape_generic_probe,
	.remove = tape_generic_remove,
	.set_online = tape_3490_online,
	.set_offline = tape_generic_offline,
	.int_class = IRQIO_TAP,
};

int tape_3490_init(void)
{
	int rc;

	TAPE_DBF_AREA = debug_register ( "tape_3490", 2, 2, 4*sizeof(long));
	debug_register_view(TAPE_DBF_AREA, &debug_sprintf_view);
#ifdef DBF_LIKE_HELL
	debug_set_level(TAPE_DBF_AREA, 6);
#endif

	DBF_EVENT(3, "3490 init\n");
	/* Register driver for 3490 tapes. */
	rc = ccw_driver_register(&tape_3490_driver);
	if (rc)
		DBF_EVENT(3, "3490 init failed\n");
	else
		DBF_EVENT(3, "3490 registered\n");
	return rc;
}

void tape_3490_exit(void)
{
	ccw_driver_unregister(&tape_3490_driver);

	debug_unregister(TAPE_DBF_AREA);
}

MODULE_DEVICE_TABLE(ccw, tape_3490_ids);
