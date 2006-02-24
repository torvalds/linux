/* 
 * File...........: linux/drivers/s390/block/dasd_3990_erp.c
 * Author(s)......: Horst  Hummel    <Horst.Hummel@de.ibm.com> 
 *		    Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 2000, 2001
 *
 */

#include <linux/timer.h>
#include <linux/slab.h>
#include <asm/idals.h>
#include <asm/todclk.h>

#define PRINTK_HEADER "dasd_erp(3990): "

#include "dasd_int.h"
#include "dasd_eckd.h"


struct DCTL_data {
	unsigned char subcommand;  /* e.g Inhibit Write, Enable Write,... */
	unsigned char modifier;	   /* Subcommand modifier */
	unsigned short res;	   /* reserved */
} __attribute__ ((packed));

/*
 ***************************************************************************** 
 * SECTION ERP EXAMINATION
 ***************************************************************************** 
 */

/*
 * DASD_3990_ERP_EXAMINE_24 
 *
 * DESCRIPTION
 *   Checks only for fatal (unrecoverable) error. 
 *   A detailed examination of the sense data is done later outside
 *   the interrupt handler.
 *
 *   Each bit configuration leading to an action code 2 (Exit with
 *   programming error or unusual condition indication)
 *   are handled as fatal error´s.
 * 
 *   All other configurations are handled as recoverable errors.
 *
 * RETURN VALUES
 *   dasd_era_fatal	for all fatal (unrecoverable errors)
 *   dasd_era_recover	for all others.
 */
static dasd_era_t
dasd_3990_erp_examine_24(struct dasd_ccw_req * cqr, char *sense)
{

	struct dasd_device *device = cqr->device;

	/* check for 'Command Reject' */
	if ((sense[0] & SNS0_CMD_REJECT) &&
	    (!(sense[2] & SNS2_ENV_DATA_PRESENT))) {

		DEV_MESSAGE(KERN_ERR, device, "%s",
			    "EXAMINE 24: Command Reject detected - "
			    "fatal error");

		return dasd_era_fatal;
	}

	/* check for 'Invalid Track Format' */
	if ((sense[1] & SNS1_INV_TRACK_FORMAT) &&
	    (!(sense[2] & SNS2_ENV_DATA_PRESENT))) {

		DEV_MESSAGE(KERN_ERR, device, "%s",
			    "EXAMINE 24: Invalid Track Format detected "
			    "- fatal error");

		return dasd_era_fatal;
	}

	/* check for 'No Record Found' */
	if (sense[1] & SNS1_NO_REC_FOUND) {

                /* FIXME: fatal error ?!? */
		DEV_MESSAGE(KERN_ERR, device,
			    "EXAMINE 24: No Record Found detected %s",
                            device->state <= DASD_STATE_BASIC ?
			    " " : "- fatal error");

		return dasd_era_fatal;
	}

	/* return recoverable for all others */
	return dasd_era_recover;
}				/* END dasd_3990_erp_examine_24 */

/*
 * DASD_3990_ERP_EXAMINE_32 
 *
 * DESCRIPTION
 *   Checks only for fatal/no/recoverable error. 
 *   A detailed examination of the sense data is done later outside
 *   the interrupt handler.
 *
 * RETURN VALUES
 *   dasd_era_none	no error 
 *   dasd_era_fatal	for all fatal (unrecoverable errors)
 *   dasd_era_recover	for recoverable others.
 */
static dasd_era_t
dasd_3990_erp_examine_32(struct dasd_ccw_req * cqr, char *sense)
{

	struct dasd_device *device = cqr->device;

	switch (sense[25]) {
	case 0x00:
		return dasd_era_none;

	case 0x01:
		DEV_MESSAGE(KERN_ERR, device, "%s", "EXAMINE 32: fatal error");

		return dasd_era_fatal;

	default:

		return dasd_era_recover;
	}

}				/* end dasd_3990_erp_examine_32 */

/*
 * DASD_3990_ERP_EXAMINE 
 *
 * DESCRIPTION
 *   Checks only for fatal/no/recover error. 
 *   A detailed examination of the sense data is done later outside
 *   the interrupt handler.
 *
 *   The logic is based on the 'IBM 3990 Storage Control  Reference' manual
 *   'Chapter 7. Error Recovery Procedures'.
 *
 * RETURN VALUES
 *   dasd_era_none	no error 
 *   dasd_era_fatal	for all fatal (unrecoverable errors)
 *   dasd_era_recover	for all others.
 */
dasd_era_t
dasd_3990_erp_examine(struct dasd_ccw_req * cqr, struct irb * irb)
{

	char *sense = irb->ecw;
	dasd_era_t era = dasd_era_recover;
	struct dasd_device *device = cqr->device;

	/* check for successful execution first */
	if (irb->scsw.cstat == 0x00 &&
	    irb->scsw.dstat == (DEV_STAT_CHN_END | DEV_STAT_DEV_END))
		return dasd_era_none;

	/* distinguish between 24 and 32 byte sense data */
	if (sense[27] & DASD_SENSE_BIT_0) {

		era = dasd_3990_erp_examine_24(cqr, sense);

	} else {

		era = dasd_3990_erp_examine_32(cqr, sense);

	}

	/* log the erp chain if fatal error occurred */
	if ((era == dasd_era_fatal) && (device->state >= DASD_STATE_READY)) {
		dasd_log_sense(cqr, irb);
		dasd_log_ccw(cqr, 0, irb->scsw.cpa);
	}

	return era;

}				/* END dasd_3990_erp_examine */

/*
 ***************************************************************************** 
 * SECTION ERP HANDLING
 ***************************************************************************** 
 */
/*
 ***************************************************************************** 
 * 24 and 32 byte sense ERP functions
 ***************************************************************************** 
 */

/*
 * DASD_3990_ERP_CLEANUP 
 *
 * DESCRIPTION
 *   Removes the already build but not necessary ERP request and sets
 *   the status of the original cqr / erp to the given (final) status
 *
 *  PARAMETER
 *   erp		request to be blocked
 *   final_status	either DASD_CQR_DONE or DASD_CQR_FAILED 
 *
 * RETURN VALUES
 *   cqr		original cqr		   
 */
static struct dasd_ccw_req *
dasd_3990_erp_cleanup(struct dasd_ccw_req * erp, char final_status)
{
	struct dasd_ccw_req *cqr = erp->refers;

	dasd_free_erp_request(erp, erp->device);
	cqr->status = final_status;
	return cqr;

}				/* end dasd_3990_erp_cleanup */

/*
 * DASD_3990_ERP_BLOCK_QUEUE 
 *
 * DESCRIPTION
 *   Block the given device request queue to prevent from further
 *   processing until the started timer has expired or an related
 *   interrupt was received.
 */
static void
dasd_3990_erp_block_queue(struct dasd_ccw_req * erp, int expires)
{

	struct dasd_device *device = erp->device;

	DEV_MESSAGE(KERN_INFO, device,
		    "blocking request queue for %is", expires/HZ);

	device->stopped |= DASD_STOPPED_PENDING;
	erp->status = DASD_CQR_QUEUED;

	dasd_set_timer(device, expires);
}

/*
 * DASD_3990_ERP_INT_REQ 
 *
 * DESCRIPTION
 *   Handles 'Intervention Required' error.
 *   This means either device offline or not installed.
 *
 * PARAMETER
 *   erp		current erp
 * RETURN VALUES
 *   erp		modified erp
 */
static struct dasd_ccw_req *
dasd_3990_erp_int_req(struct dasd_ccw_req * erp)
{

	struct dasd_device *device = erp->device;

	/* first time set initial retry counter and erp_function */
	/* and retry once without blocking queue		 */
	/* (this enables easier enqueing of the cqr)		 */
	if (erp->function != dasd_3990_erp_int_req) {

		erp->retries = 256;
		erp->function = dasd_3990_erp_int_req;

	} else {

		/* issue a message and wait for 'device ready' interrupt */
		DEV_MESSAGE(KERN_ERR, device, "%s",
			    "is offline or not installed - "
			    "INTERVENTION REQUIRED!!");

		dasd_3990_erp_block_queue(erp, 60*HZ);
	}

	return erp;

}				/* end dasd_3990_erp_int_req */

/*
 * DASD_3990_ERP_ALTERNATE_PATH 
 *
 * DESCRIPTION
 *   Repeat the operation on a different channel path.
 *   If all alternate paths have been tried, the request is posted with a
 *   permanent error.
 *
 *  PARAMETER
 *   erp		pointer to the current ERP
 *
 * RETURN VALUES
 *   erp		modified pointer to the ERP
 */
static void
dasd_3990_erp_alternate_path(struct dasd_ccw_req * erp)
{
	struct dasd_device *device = erp->device;
	__u8 opm;

	/* try alternate valid path */
	opm = ccw_device_get_path_mask(device->cdev);
	//FIXME: start with get_opm ?
	if (erp->lpm == 0)
		erp->lpm = LPM_ANYPATH & ~(erp->irb.esw.esw0.sublog.lpum);
	else
		erp->lpm &= ~(erp->irb.esw.esw0.sublog.lpum);

	if ((erp->lpm & opm) != 0x00) {

		DEV_MESSAGE(KERN_DEBUG, device,
			    "try alternate lpm=%x (lpum=%x / opm=%x)",
			    erp->lpm, erp->irb.esw.esw0.sublog.lpum, opm);

		/* reset status to queued to handle the request again... */
		if (erp->status > DASD_CQR_QUEUED)
			erp->status = DASD_CQR_QUEUED;
		erp->retries = 1;
	} else {
		DEV_MESSAGE(KERN_ERR, device,
			    "No alternate channel path left (lpum=%x / "
			    "opm=%x) -> permanent error",
			    erp->irb.esw.esw0.sublog.lpum, opm);

		/* post request with permanent error */
		if (erp->status > DASD_CQR_QUEUED)
			erp->status = DASD_CQR_FAILED;
	}
}				/* end dasd_3990_erp_alternate_path */

/*
 * DASD_3990_ERP_DCTL
 *
 * DESCRIPTION
 *   Setup cqr to do the Diagnostic Control (DCTL) command with an 
 *   Inhibit Write subcommand (0x20) and the given modifier.
 *
 *  PARAMETER
 *   erp		pointer to the current (failed) ERP
 *   modifier		subcommand modifier
 *   
 * RETURN VALUES
 *   dctl_cqr		pointer to NEW dctl_cqr 
 *
 */
static struct dasd_ccw_req *
dasd_3990_erp_DCTL(struct dasd_ccw_req * erp, char modifier)
{

	struct dasd_device *device = erp->device;
	struct DCTL_data *DCTL_data;
	struct ccw1 *ccw;
	struct dasd_ccw_req *dctl_cqr;

	dctl_cqr = dasd_alloc_erp_request((char *) &erp->magic, 1,
					  sizeof (struct DCTL_data),
					  erp->device);
	if (IS_ERR(dctl_cqr)) {
		DEV_MESSAGE(KERN_ERR, device, "%s",
			    "Unable to allocate DCTL-CQR");
		erp->status = DASD_CQR_FAILED;
		return erp;
	}

	DCTL_data = dctl_cqr->data;

	DCTL_data->subcommand = 0x02;	/* Inhibit Write */
	DCTL_data->modifier = modifier;

	ccw = dctl_cqr->cpaddr;
	memset(ccw, 0, sizeof (struct ccw1));
	ccw->cmd_code = CCW_CMD_DCTL;
	ccw->count = 4;
	ccw->cda = (__u32)(addr_t) DCTL_data;
	dctl_cqr->function = dasd_3990_erp_DCTL;
	dctl_cqr->refers = erp;
	dctl_cqr->device = erp->device;
	dctl_cqr->magic = erp->magic;
	dctl_cqr->expires = 5 * 60 * HZ;
	dctl_cqr->retries = 2;

	dctl_cqr->buildclk = get_clock();

	dctl_cqr->status = DASD_CQR_FILLED;

	return dctl_cqr;

}				/* end dasd_3990_erp_DCTL */

/*
 * DASD_3990_ERP_ACTION_1 
 *
 * DESCRIPTION
 *   Setup ERP to do the ERP action 1 (see Reference manual).
 *   Repeat the operation on a different channel path.
 *   If all alternate paths have been tried, the request is posted with a
 *   permanent error.
 *   Note: duplex handling is not implemented (yet).
 *
 *  PARAMETER
 *   erp		pointer to the current ERP
 *
 * RETURN VALUES
 *   erp		pointer to the ERP
 *
 */
static struct dasd_ccw_req *
dasd_3990_erp_action_1(struct dasd_ccw_req * erp)
{

	erp->function = dasd_3990_erp_action_1;

	dasd_3990_erp_alternate_path(erp);

	return erp;

}				/* end dasd_3990_erp_action_1 */

/*
 * DASD_3990_ERP_ACTION_4 
 *
 * DESCRIPTION
 *   Setup ERP to do the ERP action 4 (see Reference manual).
 *   Set the current request to PENDING to block the CQR queue for that device
 *   until the state change interrupt appears.
 *   Use a timer (20 seconds) to retry the cqr if the interrupt is still
 *   missing.
 *
 *  PARAMETER
 *   sense		sense data of the actual error
 *   erp		pointer to the current ERP
 *
 * RETURN VALUES
 *   erp		pointer to the ERP
 *
 */
static struct dasd_ccw_req *
dasd_3990_erp_action_4(struct dasd_ccw_req * erp, char *sense)
{

	struct dasd_device *device = erp->device;

	/* first time set initial retry counter and erp_function    */
	/* and retry once without waiting for state change pending  */
	/* interrupt (this enables easier enqueing of the cqr)	    */
	if (erp->function != dasd_3990_erp_action_4) {

		DEV_MESSAGE(KERN_INFO, device, "%s",
			    "dasd_3990_erp_action_4: first time retry");

		erp->retries = 256;
		erp->function = dasd_3990_erp_action_4;

	} else {

		if (sense[25] == 0x1D) {	/* state change pending */

			DEV_MESSAGE(KERN_INFO, device, 
				    "waiting for state change pending "
				    "interrupt, %d retries left",
				    erp->retries);
			
			dasd_3990_erp_block_queue(erp, 30*HZ);

                } else if (sense[25] == 0x1E) {	/* busy */
			DEV_MESSAGE(KERN_INFO, device,
				    "busy - redriving request later, "
				    "%d retries left",
				    erp->retries);
                        dasd_3990_erp_block_queue(erp, HZ);
		} else {

			/* no state change pending - retry */
			DEV_MESSAGE (KERN_INFO, device, 
				     "redriving request immediately, "
				     "%d retries left", 
				     erp->retries);
			erp->status = DASD_CQR_QUEUED;
		}
	}

	return erp;

}				/* end dasd_3990_erp_action_4 */

/*
 ***************************************************************************** 
 * 24 byte sense ERP functions (only)
 ***************************************************************************** 
 */

/*
 * DASD_3990_ERP_ACTION_5 
 *
 * DESCRIPTION
 *   Setup ERP to do the ERP action 5 (see Reference manual).
 *   NOTE: Further handling is done in xxx_further_erp after the retries.
 *
 *  PARAMETER
 *   erp		pointer to the current ERP
 *
 * RETURN VALUES
 *   erp		pointer to the ERP
 *
 */
static struct dasd_ccw_req *
dasd_3990_erp_action_5(struct dasd_ccw_req * erp)
{

	/* first of all retry */
	erp->retries = 10;
	erp->function = dasd_3990_erp_action_5;

	return erp;

}				/* end dasd_3990_erp_action_5 */

/*
 * DASD_3990_HANDLE_ENV_DATA
 *
 * DESCRIPTION
 *   Handles 24 byte 'Environmental data present'.
 *   Does a analysis of the sense data (message Format)
 *   and prints the error messages.
 *
 * PARAMETER
 *   sense		current sense data
 *   
 * RETURN VALUES
 *   void
 */
static void
dasd_3990_handle_env_data(struct dasd_ccw_req * erp, char *sense)
{

	struct dasd_device *device = erp->device;
	char msg_format = (sense[7] & 0xF0);
	char msg_no = (sense[7] & 0x0F);

	switch (msg_format) {
	case 0x00:		/* Format 0 - Program or System Checks */

		if (sense[1] & 0x10) {	/* check message to operator bit */

			switch (msg_no) {
			case 0x00:	/* No Message */
				break;
			case 0x01:
				DEV_MESSAGE(KERN_WARNING, device, "%s",
					    "FORMAT 0 - Invalid Command");
				break;
			case 0x02:
				DEV_MESSAGE(KERN_WARNING, device, "%s",
					    "FORMAT 0 - Invalid Command "
					    "Sequence");
				break;
			case 0x03:
				DEV_MESSAGE(KERN_WARNING, device, "%s",
					    "FORMAT 0 - CCW Count less than "
					    "required");
				break;
			case 0x04:
				DEV_MESSAGE(KERN_WARNING, device, "%s",
					    "FORMAT 0 - Invalid Parameter");
				break;
			case 0x05:
				DEV_MESSAGE(KERN_WARNING, device, "%s",
					    "FORMAT 0 - Diagnostic of Sepecial"
					    " Command Violates File Mask");
				break;
			case 0x07:
				DEV_MESSAGE(KERN_WARNING, device, "%s",
					    "FORMAT 0 - Channel Returned with "
					    "Incorrect retry CCW");
				break;
			case 0x08:
				DEV_MESSAGE(KERN_WARNING, device, "%s",
					    "FORMAT 0 - Reset Notification");
				break;
			case 0x09:
				DEV_MESSAGE(KERN_WARNING, device, "%s",
					    "FORMAT 0 - Storage Path Restart");
				break;
			case 0x0A:
				DEV_MESSAGE(KERN_WARNING, device,
					    "FORMAT 0 - Channel requested "
					    "... %02x", sense[8]);
				break;
			case 0x0B:
				DEV_MESSAGE(KERN_WARNING, device, "%s",
					    "FORMAT 0 - Invalid Defective/"
					    "Alternate Track Pointer");
				break;
			case 0x0C:
				DEV_MESSAGE(KERN_WARNING, device, "%s",
					    "FORMAT 0 - DPS Installation "
					    "Check");
				break;
			case 0x0E:
				DEV_MESSAGE(KERN_WARNING, device, "%s",
					    "FORMAT 0 - Command Invalid on "
					    "Secondary Address");
				break;
			case 0x0F:
				DEV_MESSAGE(KERN_WARNING, device,
					    "FORMAT 0 - Status Not As "
					    "Required: reason %02x", sense[8]);
				break;
			default:
				DEV_MESSAGE(KERN_WARNING, device, "%s",
					    "FORMAT 0 - Reseved");
			}
		} else {
			switch (msg_no) {
			case 0x00:	/* No Message */
				break;
			case 0x01:
				DEV_MESSAGE(KERN_WARNING, device, "%s",
					    "FORMAT 0 - Device Error Source");
				break;
			case 0x02:
				DEV_MESSAGE(KERN_WARNING, device, "%s",
					    "FORMAT 0 - Reserved");
				break;
			case 0x03:
				DEV_MESSAGE(KERN_WARNING, device,
					    "FORMAT 0 - Device Fenced - "
					    "device = %02x", sense[4]);
				break;
			case 0x04:
				DEV_MESSAGE(KERN_WARNING, device, "%s",
					    "FORMAT 0 - Data Pinned for "
					    "Device");
				break;
			default:
				DEV_MESSAGE(KERN_WARNING, device, "%s",
					    "FORMAT 0 - Reserved");
			}
		}
		break;

	case 0x10:		/* Format 1 - Device Equipment Checks */
		switch (msg_no) {
		case 0x00:	/* No Message */
			break;
		case 0x01:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 1 - Device Status 1 not as "
				    "expected");
			break;
		case 0x03:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 1 - Index missing");
			break;
		case 0x04:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 1 - Interruption cannot be reset");
			break;
		case 0x05:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 1 - Device did not respond to "
				    "selection");
			break;
		case 0x06:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 1 - Device check-2 error or Set "
				    "Sector is not complete");
			break;
		case 0x07:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 1 - Head address does not "
				    "compare");
			break;
		case 0x08:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 1 - Device status 1 not valid");
			break;
		case 0x09:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 1 - Device not ready");
			break;
		case 0x0A:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 1 - Track physical address did "
				    "not compare");
			break;
		case 0x0B:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 1 - Missing device address bit");
			break;
		case 0x0C:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 1 - Drive motor switch is off");
			break;
		case 0x0D:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 1 - Seek incomplete");
			break;
		case 0x0E:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 1 - Cylinder address did not "
				    "compare");
			break;
		case 0x0F:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 1 - Offset active cannot be "
				    "reset");
			break;
		default:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 1 - Reserved");
		}
		break;

	case 0x20:		/* Format 2 - 3990 Equipment Checks */
		switch (msg_no) {
		case 0x08:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 2 - 3990 check-2 error");
			break;
		case 0x0E:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 2 - Support facility errors");
			break;
		case 0x0F:
			DEV_MESSAGE(KERN_WARNING, device,
				    "FORMAT 2 - Microcode detected error %02x",
				    sense[8]);
			break;
		default:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 2 - Reserved");
		}
		break;

	case 0x30:		/* Format 3 - 3990 Control Checks */
		switch (msg_no) {
		case 0x0F:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 3 - Allegiance terminated");
			break;
		default:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 3 - Reserved");
		}
		break;

	case 0x40:		/* Format 4 - Data Checks */
		switch (msg_no) {
		case 0x00:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 4 - Home address area error");
			break;
		case 0x01:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 4 - Count area error");
			break;
		case 0x02:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 4 - Key area error");
			break;
		case 0x03:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 4 - Data area error");
			break;
		case 0x04:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 4 - No sync byte in home address "
				    "area");
			break;
		case 0x05:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 4 - No sync byte in count address "
				    "area");
			break;
		case 0x06:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 4 - No sync byte in key area");
			break;
		case 0x07:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 4 - No sync byte in data area");
			break;
		case 0x08:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 4 - Home address area error; "
				    "offset active");
			break;
		case 0x09:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 4 - Count area error; offset "
				    "active");
			break;
		case 0x0A:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 4 - Key area error; offset "
				    "active");
			break;
		case 0x0B:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 4 - Data area error; "
				    "offset active");
			break;
		case 0x0C:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 4 - No sync byte in home "
				    "address area; offset active");
			break;
		case 0x0D:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 4 - No syn byte in count "
				    "address area; offset active");
			break;
		case 0x0E:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 4 - No sync byte in key area; "
				    "offset active");
			break;
		case 0x0F:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 4 - No syn byte in data area; "
				    "offset active");
			break;
		default:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 4 - Reserved");
		}
		break;

	case 0x50:  /* Format 5 - Data Check with displacement information */
		switch (msg_no) {
		case 0x00:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 5 - Data Check in the "
				    "home address area");
			break;
		case 0x01:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 5 - Data Check in the count area");
			break;
		case 0x02:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 5 - Data Check in the key area");
			break;
		case 0x03:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 5 - Data Check in the data area");
			break;
		case 0x08:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 5 - Data Check in the "
				    "home address area; offset active");
			break;
		case 0x09:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 5 - Data Check in the count area; "
				    "offset active");
			break;
		case 0x0A:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 5 - Data Check in the key area; "
				    "offset active");
			break;
		case 0x0B:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 5 - Data Check in the data area; "
				    "offset active");
			break;
		default:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 5 - Reserved");
		}
		break;

	case 0x60:  /* Format 6 - Usage Statistics/Overrun Errors */
		switch (msg_no) {
		case 0x00:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 6 - Overrun on channel A");
			break;
		case 0x01:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 6 - Overrun on channel B");
			break;
		case 0x02:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 6 - Overrun on channel C");
			break;
		case 0x03:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 6 - Overrun on channel D");
			break;
		case 0x04:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 6 - Overrun on channel E");
			break;
		case 0x05:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 6 - Overrun on channel F");
			break;
		case 0x06:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 6 - Overrun on channel G");
			break;
		case 0x07:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 6 - Overrun on channel H");
			break;
		default:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 6 - Reserved");
		}
		break;

	case 0x70:  /* Format 7 - Device Connection Control Checks */
		switch (msg_no) {
		case 0x00:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 7 - RCC initiated by a connection "
				    "check alert");
			break;
		case 0x01:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 7 - RCC 1 sequence not "
				    "successful");
			break;
		case 0x02:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 7 - RCC 1 and RCC 2 sequences not "
				    "successful");
			break;
		case 0x03:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 7 - Invalid tag-in during "
				    "selection sequence");
			break;
		case 0x04:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 7 - extra RCC required");
			break;
		case 0x05:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 7 - Invalid DCC selection "
				    "response or timeout");
			break;
		case 0x06:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 7 - Missing end operation; device "
				    "transfer complete");
			break;
		case 0x07:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 7 - Missing end operation; device "
				    "transfer incomplete");
			break;
		case 0x08:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 7 - Invalid tag-in for an "
				    "immediate command sequence");
			break;
		case 0x09:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 7 - Invalid tag-in for an "
				    "extended command sequence");
			break;
		case 0x0A:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 7 - 3990 microcode time out when "
				    "stopping selection");
			break;
		case 0x0B:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 7 - No response to selection "
				    "after a poll interruption");
			break;
		case 0x0C:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 7 - Permanent path error (DASD "
				    "controller not available)");
			break;
		case 0x0D:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 7 - DASD controller not available"
				    " on disconnected command chain");
			break;
		default:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 7 - Reserved");
		}
		break;

	case 0x80:  /* Format 8 - Additional Device Equipment Checks */
		switch (msg_no) {
		case 0x00:	/* No Message */
		case 0x01:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 8 - Error correction code "
				    "hardware fault");
			break;
		case 0x03:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 8 - Unexpected end operation "
				    "response code");
			break;
		case 0x04:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 8 - End operation with transfer "
				    "count not zero");
			break;
		case 0x05:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 8 - End operation with transfer "
				    "count zero");
			break;
		case 0x06:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 8 - DPS checks after a system "
				    "reset or selective reset");
			break;
		case 0x07:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 8 - DPS cannot be filled");
			break;
		case 0x08:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 8 - Short busy time-out during "
				    "device selection");
			break;
		case 0x09:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 8 - DASD controller failed to "
				    "set or reset the long busy latch");
			break;
		case 0x0A:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 8 - No interruption from device "
				    "during a command chain");
			break;
		default:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 8 - Reserved");
		}
		break;

	case 0x90:  /* Format 9 - Device Read, Write, and Seek Checks */
		switch (msg_no) {
		case 0x00:
			break;	/* No Message */
		case 0x06:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 9 - Device check-2 error");
			break;
		case 0x07:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 9 - Head address did not compare");
			break;
		case 0x0A:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 9 - Track physical address did "
				    "not compare while oriented");
			break;
		case 0x0E:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 9 - Cylinder address did not "
				    "compare");
			break;
		default:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT 9 - Reserved");
		}
		break;

	case 0xF0:		/* Format F - Cache Storage Checks */
		switch (msg_no) {
		case 0x00:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT F - Operation Terminated");
			break;
		case 0x01:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT F - Subsystem Processing Error");
			break;
		case 0x02:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT F - Cache or nonvolatile storage "
				    "equipment failure");
			break;
		case 0x04:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT F - Caching terminated");
			break;
		case 0x06:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT F - Cache fast write access not "
				    "authorized");
			break;
		case 0x07:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT F - Track format incorrect");
			break;
		case 0x09:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT F - Caching reinitiated");
			break;
		case 0x0A:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT F - Nonvolatile storage "
				    "terminated");
			break;
		case 0x0B:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT F - Volume is suspended duplex");
			break;
		case 0x0C:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT F - Subsystem status connot be "
				    "determined");
			break;
		case 0x0D:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT F - Caching status reset to "
				    "default");
			break;
		case 0x0E:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT F - DASD Fast Write inhibited");
			break;
		default:
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				    "FORMAT D - Reserved");
		}
		break;

	default:	/* unknown message format - should not happen */
	        DEV_MESSAGE (KERN_WARNING, device,
                             "unknown message format %02x",
                             msg_format);
		break;
	}			/* end switch message format */

}				/* end dasd_3990_handle_env_data */

/*
 * DASD_3990_ERP_COM_REJ
 *
 * DESCRIPTION
 *   Handles 24 byte 'Command Reject' error.
 *
 * PARAMETER
 *   erp		current erp_head
 *   sense		current sense data
 * 
 * RETURN VALUES
 *   erp		'new' erp_head - pointer to new ERP 
 */
static struct dasd_ccw_req *
dasd_3990_erp_com_rej(struct dasd_ccw_req * erp, char *sense)
{

	struct dasd_device *device = erp->device;

	erp->function = dasd_3990_erp_com_rej;

	/* env data present (ACTION 10 - retry should work) */
	if (sense[2] & SNS2_ENV_DATA_PRESENT) {

		DEV_MESSAGE(KERN_DEBUG, device, "%s",
			    "Command Reject - environmental data present");

		dasd_3990_handle_env_data(erp, sense);

		erp->retries = 5;

	} else {
		/* fatal error -  set status to FAILED */
		DEV_MESSAGE(KERN_ERR, device, "%s",
			    "Command Reject - Fatal error");

		erp = dasd_3990_erp_cleanup(erp, DASD_CQR_FAILED);
	}

	return erp;

}				/* end dasd_3990_erp_com_rej */

/*
 * DASD_3990_ERP_BUS_OUT 
 *
 * DESCRIPTION
 *   Handles 24 byte 'Bus Out Parity Check' error.
 *
 * PARAMETER
 *   erp		current erp_head
 * RETURN VALUES
 *   erp		new erp_head - pointer to new ERP
 */
static struct dasd_ccw_req *
dasd_3990_erp_bus_out(struct dasd_ccw_req * erp)
{

	struct dasd_device *device = erp->device;

	/* first time set initial retry counter and erp_function */
	/* and retry once without blocking queue		 */
	/* (this enables easier enqueing of the cqr)		 */
	if (erp->function != dasd_3990_erp_bus_out) {
		erp->retries = 256;
		erp->function = dasd_3990_erp_bus_out;

	} else {

		/* issue a message and wait for 'device ready' interrupt */
		DEV_MESSAGE(KERN_DEBUG, device, "%s",
			    "bus out parity error or BOPC requested by "
			    "channel");

		dasd_3990_erp_block_queue(erp, 60*HZ);

	}

	return erp;

}				/* end dasd_3990_erp_bus_out */

/*
 * DASD_3990_ERP_EQUIP_CHECK
 *
 * DESCRIPTION
 *   Handles 24 byte 'Equipment Check' error.
 *
 * PARAMETER
 *   erp		current erp_head
 * RETURN VALUES
 *   erp		new erp_head - pointer to new ERP
 */
static struct dasd_ccw_req *
dasd_3990_erp_equip_check(struct dasd_ccw_req * erp, char *sense)
{

	struct dasd_device *device = erp->device;

	erp->function = dasd_3990_erp_equip_check;

	if (sense[1] & SNS1_WRITE_INHIBITED) {

		DEV_MESSAGE(KERN_DEBUG, device, "%s",
			    "Write inhibited path encountered");

		/* vary path offline */
		DEV_MESSAGE(KERN_ERR, device, "%s",
			    "Path should be varied off-line. "
			    "This is not implemented yet \n - please report "
			    "to linux390@de.ibm.com");

		erp = dasd_3990_erp_action_1(erp);

	} else if (sense[2] & SNS2_ENV_DATA_PRESENT) {

		DEV_MESSAGE(KERN_DEBUG, device, "%s",
			    "Equipment Check - " "environmental data present");

		dasd_3990_handle_env_data(erp, sense);

		erp = dasd_3990_erp_action_4(erp, sense);

	} else if (sense[1] & SNS1_PERM_ERR) {

		DEV_MESSAGE(KERN_DEBUG, device, "%s",
			    "Equipment Check - retry exhausted or "
			    "undesirable");

		erp = dasd_3990_erp_action_1(erp);

	} else {
		/* all other equipment checks - Action 5 */
		/* rest is done when retries == 0 */
		DEV_MESSAGE(KERN_DEBUG, device, "%s",
			    "Equipment check or processing error");

		erp = dasd_3990_erp_action_5(erp);
	}

	return erp;

}				/* end dasd_3990_erp_equip_check */

/*
 * DASD_3990_ERP_DATA_CHECK
 *
 * DESCRIPTION
 *   Handles 24 byte 'Data Check' error.
 *
 * PARAMETER
 *   erp		current erp_head
 * RETURN VALUES
 *   erp		new erp_head - pointer to new ERP
 */
static struct dasd_ccw_req *
dasd_3990_erp_data_check(struct dasd_ccw_req * erp, char *sense)
{

	struct dasd_device *device = erp->device;

	erp->function = dasd_3990_erp_data_check;

	if (sense[2] & SNS2_CORRECTABLE) {	/* correctable data check */

		/* issue message that the data has been corrected */
		DEV_MESSAGE(KERN_EMERG, device, "%s",
			    "Data recovered during retry with PCI "
			    "fetch mode active");

		/* not possible to handle this situation in Linux */
		panic("No way to inform application about the possibly "
		      "incorrect data");

	} else if (sense[2] & SNS2_ENV_DATA_PRESENT) {

		DEV_MESSAGE(KERN_DEBUG, device, "%s",
			    "Uncorrectable data check recovered secondary "
			    "addr of duplex pair");

		erp = dasd_3990_erp_action_4(erp, sense);

	} else if (sense[1] & SNS1_PERM_ERR) {

		DEV_MESSAGE(KERN_DEBUG, device, "%s",
			    "Uncorrectable data check with internal "
			    "retry exhausted");

		erp = dasd_3990_erp_action_1(erp);

	} else {
		/* all other data checks */
		DEV_MESSAGE(KERN_DEBUG, device, "%s",
			    "Uncorrectable data check with retry count "
			    "exhausted...");

		erp = dasd_3990_erp_action_5(erp);
	}

	return erp;

}				/* end dasd_3990_erp_data_check */

/*
 * DASD_3990_ERP_OVERRUN
 *
 * DESCRIPTION
 *   Handles 24 byte 'Overrun' error.
 *
 * PARAMETER
 *   erp		current erp_head
 * RETURN VALUES
 *   erp		new erp_head - pointer to new ERP
 */
static struct dasd_ccw_req *
dasd_3990_erp_overrun(struct dasd_ccw_req * erp, char *sense)
{

	struct dasd_device *device = erp->device;

	erp->function = dasd_3990_erp_overrun;

	DEV_MESSAGE(KERN_DEBUG, device, "%s",
		    "Overrun - service overrun or overrun"
		    " error requested by channel");

	erp = dasd_3990_erp_action_5(erp);

	return erp;

}				/* end dasd_3990_erp_overrun */

/*
 * DASD_3990_ERP_INV_FORMAT
 *
 * DESCRIPTION
 *   Handles 24 byte 'Invalid Track Format' error.
 *
 * PARAMETER
 *   erp		current erp_head
 * RETURN VALUES
 *   erp		new erp_head - pointer to new ERP
 */
static struct dasd_ccw_req *
dasd_3990_erp_inv_format(struct dasd_ccw_req * erp, char *sense)
{

	struct dasd_device *device = erp->device;

	erp->function = dasd_3990_erp_inv_format;

	if (sense[2] & SNS2_ENV_DATA_PRESENT) {

		DEV_MESSAGE(KERN_DEBUG, device, "%s",
			    "Track format error when destaging or "
			    "staging data");

		dasd_3990_handle_env_data(erp, sense);

		erp = dasd_3990_erp_action_4(erp, sense);

	} else {
		DEV_MESSAGE(KERN_ERR, device, "%s",
			    "Invalid Track Format - Fatal error should have "
			    "been handled within the interrupt handler");

		erp = dasd_3990_erp_cleanup(erp, DASD_CQR_FAILED);
	}

	return erp;

}				/* end dasd_3990_erp_inv_format */

/*
 * DASD_3990_ERP_EOC
 *
 * DESCRIPTION
 *   Handles 24 byte 'End-of-Cylinder' error.
 *
 * PARAMETER
 *   erp		already added default erp
 * RETURN VALUES
 *   erp		pointer to original (failed) cqr.
 */
static struct dasd_ccw_req *
dasd_3990_erp_EOC(struct dasd_ccw_req * default_erp, char *sense)
{

	struct dasd_device *device = default_erp->device;

	DEV_MESSAGE(KERN_ERR, device, "%s",
		    "End-of-Cylinder - must never happen");

	/* implement action 7 - BUG */
	return dasd_3990_erp_cleanup(default_erp, DASD_CQR_FAILED);

}				/* end dasd_3990_erp_EOC */

/*
 * DASD_3990_ERP_ENV_DATA
 *
 * DESCRIPTION
 *   Handles 24 byte 'Environmental-Data Present' error.
 *
 * PARAMETER
 *   erp		current erp_head
 * RETURN VALUES
 *   erp		new erp_head - pointer to new ERP
 */
static struct dasd_ccw_req *
dasd_3990_erp_env_data(struct dasd_ccw_req * erp, char *sense)
{

	struct dasd_device *device = erp->device;

	erp->function = dasd_3990_erp_env_data;

	DEV_MESSAGE(KERN_DEBUG, device, "%s", "Environmental data present");

	dasd_3990_handle_env_data(erp, sense);

	/* don't retry on disabled interface */
	if (sense[7] != 0x0F) {

		erp = dasd_3990_erp_action_4(erp, sense);
	} else {

		erp = dasd_3990_erp_cleanup(erp, DASD_CQR_IN_IO);
	}

	return erp;

}				/* end dasd_3990_erp_env_data */

/*
 * DASD_3990_ERP_NO_REC
 *
 * DESCRIPTION
 *   Handles 24 byte 'No Record Found' error.
 *
 * PARAMETER
 *   erp		already added default ERP
 *		
 * RETURN VALUES
 *   erp		new erp_head - pointer to new ERP
 */
static struct dasd_ccw_req *
dasd_3990_erp_no_rec(struct dasd_ccw_req * default_erp, char *sense)
{

	struct dasd_device *device = default_erp->device;

	DEV_MESSAGE(KERN_ERR, device, "%s",
		    "No Record Found - Fatal error should "
		    "have been handled within the interrupt handler");

	return dasd_3990_erp_cleanup(default_erp, DASD_CQR_FAILED);

}				/* end dasd_3990_erp_no_rec */

/*
 * DASD_3990_ERP_FILE_PROT
 *
 * DESCRIPTION
 *   Handles 24 byte 'File Protected' error.
 *   Note: Seek related recovery is not implemented because
 *	   wee don't use the seek command yet.
 *
 * PARAMETER
 *   erp		current erp_head
 * RETURN VALUES
 *   erp		new erp_head - pointer to new ERP
 */
static struct dasd_ccw_req *
dasd_3990_erp_file_prot(struct dasd_ccw_req * erp)
{

	struct dasd_device *device = erp->device;

	DEV_MESSAGE(KERN_ERR, device, "%s", "File Protected");

	return dasd_3990_erp_cleanup(erp, DASD_CQR_FAILED);

}				/* end dasd_3990_erp_file_prot */

/*
 * DASD_3990_ERP_INSPECT_24 
 *
 * DESCRIPTION
 *   Does a detailed inspection of the 24 byte sense data
 *   and sets up a related error recovery action.  
 *
 * PARAMETER
 *   sense		sense data of the actual error
 *   erp		pointer to the currently created default ERP
 *
 * RETURN VALUES
 *   erp		pointer to the (addtitional) ERP
 */
static struct dasd_ccw_req *
dasd_3990_erp_inspect_24(struct dasd_ccw_req * erp, char *sense)
{

	struct dasd_ccw_req *erp_filled = NULL;

	/* Check sense for ....	   */
	/* 'Command Reject'	   */
	if ((erp_filled == NULL) && (sense[0] & SNS0_CMD_REJECT)) {
		erp_filled = dasd_3990_erp_com_rej(erp, sense);
	}
	/* 'Intervention Required' */
	if ((erp_filled == NULL) && (sense[0] & SNS0_INTERVENTION_REQ)) {
		erp_filled = dasd_3990_erp_int_req(erp);
	}
	/* 'Bus Out Parity Check'  */
	if ((erp_filled == NULL) && (sense[0] & SNS0_BUS_OUT_CHECK)) {
		erp_filled = dasd_3990_erp_bus_out(erp);
	}
	/* 'Equipment Check'	   */
	if ((erp_filled == NULL) && (sense[0] & SNS0_EQUIPMENT_CHECK)) {
		erp_filled = dasd_3990_erp_equip_check(erp, sense);
	}
	/* 'Data Check'		   */
	if ((erp_filled == NULL) && (sense[0] & SNS0_DATA_CHECK)) {
		erp_filled = dasd_3990_erp_data_check(erp, sense);
	}
	/* 'Overrun'		   */
	if ((erp_filled == NULL) && (sense[0] & SNS0_OVERRUN)) {
		erp_filled = dasd_3990_erp_overrun(erp, sense);
	}
	/* 'Invalid Track Format'  */
	if ((erp_filled == NULL) && (sense[1] & SNS1_INV_TRACK_FORMAT)) {
		erp_filled = dasd_3990_erp_inv_format(erp, sense);
	}
	/* 'End-of-Cylinder'	   */
	if ((erp_filled == NULL) && (sense[1] & SNS1_EOC)) {
		erp_filled = dasd_3990_erp_EOC(erp, sense);
	}
	/* 'Environmental Data'	   */
	if ((erp_filled == NULL) && (sense[2] & SNS2_ENV_DATA_PRESENT)) {
		erp_filled = dasd_3990_erp_env_data(erp, sense);
	}
	/* 'No Record Found'	   */
	if ((erp_filled == NULL) && (sense[1] & SNS1_NO_REC_FOUND)) {
		erp_filled = dasd_3990_erp_no_rec(erp, sense);
	}
	/* 'File Protected'	   */
	if ((erp_filled == NULL) && (sense[1] & SNS1_FILE_PROTECTED)) {
		erp_filled = dasd_3990_erp_file_prot(erp);
	}
	/* other (unknown) error - do default ERP */
	if (erp_filled == NULL) {

		erp_filled = erp;
	}

	return erp_filled;

}				/* END dasd_3990_erp_inspect_24 */

/*
 ***************************************************************************** 
 * 32 byte sense ERP functions (only)
 ***************************************************************************** 
 */

/*
 * DASD_3990_ERPACTION_10_32 
 *
 * DESCRIPTION
 *   Handles 32 byte 'Action 10' of Single Program Action Codes.
 *   Just retry and if retry doesn't work, return with error.
 *
 * PARAMETER
 *   erp		current erp_head
 *   sense		current sense data 
 * RETURN VALUES
 *   erp		modified erp_head
 */
static struct dasd_ccw_req *
dasd_3990_erp_action_10_32(struct dasd_ccw_req * erp, char *sense)
{

	struct dasd_device *device = erp->device;

	erp->retries = 256;
	erp->function = dasd_3990_erp_action_10_32;

	DEV_MESSAGE(KERN_DEBUG, device, "%s", "Perform logging requested");

	return erp;

}				/* end dasd_3990_erp_action_10_32 */

/*
 * DASD_3990_ERP_ACTION_1B_32
 *
 * DESCRIPTION
 *   Handles 32 byte 'Action 1B' of Single Program Action Codes.
 *   A write operation could not be finished because of an unexpected 
 *   condition.
 *   The already created 'default erp' is used to get the link to 
 *   the erp chain, but it can not be used for this recovery 
 *   action because it contains no DE/LO data space.
 *
 * PARAMETER
 *   default_erp	already added default erp.
 *   sense		current sense data 
 *
 * RETURN VALUES
 *   erp		new erp or 
 *			default_erp in case of imprecise ending or error
 */
static struct dasd_ccw_req *
dasd_3990_erp_action_1B_32(struct dasd_ccw_req * default_erp, char *sense)
{

	struct dasd_device *device = default_erp->device;
	__u32 cpa = 0;
	struct dasd_ccw_req *cqr;
	struct dasd_ccw_req *erp;
	struct DE_eckd_data *DE_data;
	char *LO_data;		/* LO_eckd_data_t */
	struct ccw1 *ccw;

	DEV_MESSAGE(KERN_DEBUG, device, "%s",
		    "Write not finished because of unexpected condition");

	default_erp->function = dasd_3990_erp_action_1B_32;

	/* determine the original cqr */
	cqr = default_erp;

	while (cqr->refers != NULL) {
		cqr = cqr->refers;
	}

	/* for imprecise ending just do default erp */
	if (sense[1] & 0x01) {

		DEV_MESSAGE(KERN_DEBUG, device, "%s",
			    "Imprecise ending is set - just retry");

		return default_erp;
	}

	/* determine the address of the CCW to be restarted */
	/* Imprecise ending is not set -> addr from IRB-SCSW */
	cpa = default_erp->refers->irb.scsw.cpa;

	if (cpa == 0) {

		DEV_MESSAGE(KERN_DEBUG, device, "%s",
			    "Unable to determine address of the CCW "
			    "to be restarted");

		return dasd_3990_erp_cleanup(default_erp, DASD_CQR_FAILED);
	}

	/* Build new ERP request including DE/LO */
	erp = dasd_alloc_erp_request((char *) &cqr->magic,
				     2 + 1,/* DE/LO + TIC */
				     sizeof (struct DE_eckd_data) +
				     sizeof (struct LO_eckd_data), device);

	if (IS_ERR(erp)) {
		DEV_MESSAGE(KERN_ERR, device, "%s", "Unable to allocate ERP");
		return dasd_3990_erp_cleanup(default_erp, DASD_CQR_FAILED);
	}

	/* use original DE */
	DE_data = erp->data;
	memcpy(DE_data, cqr->data, sizeof (struct DE_eckd_data));

	/* create LO */
	LO_data = erp->data + sizeof (struct DE_eckd_data);

	if ((sense[3] == 0x01) && (LO_data[1] & 0x01)) {

		DEV_MESSAGE(KERN_ERR, device, "%s",
			    "BUG - this should not happen");

		return dasd_3990_erp_cleanup(default_erp, DASD_CQR_FAILED);
	}

	if ((sense[7] & 0x3F) == 0x01) {
		/* operation code is WRITE DATA -> data area orientation */
		LO_data[0] = 0x81;

	} else if ((sense[7] & 0x3F) == 0x03) {
		/* operation code is FORMAT WRITE -> index orientation */
		LO_data[0] = 0xC3;

	} else {
		LO_data[0] = sense[7];	/* operation */
	}

	LO_data[1] = sense[8];	/* auxiliary */
	LO_data[2] = sense[9];
	LO_data[3] = sense[3];	/* count */
	LO_data[4] = sense[29];	/* seek_addr.cyl */
	LO_data[5] = sense[30];	/* seek_addr.cyl 2nd byte */
	LO_data[7] = sense[31];	/* seek_addr.head 2nd byte */

	memcpy(&(LO_data[8]), &(sense[11]), 8);

	/* create DE ccw */
	ccw = erp->cpaddr;
	memset(ccw, 0, sizeof (struct ccw1));
	ccw->cmd_code = DASD_ECKD_CCW_DEFINE_EXTENT;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 16;
	ccw->cda = (__u32)(addr_t) DE_data;

	/* create LO ccw */
	ccw++;
	memset(ccw, 0, sizeof (struct ccw1));
	ccw->cmd_code = DASD_ECKD_CCW_LOCATE_RECORD;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 16;
	ccw->cda = (__u32)(addr_t) LO_data;

	/* TIC to the failed ccw */
	ccw++;
	ccw->cmd_code = CCW_CMD_TIC;
	ccw->cda = cpa;

	/* fill erp related fields */
	erp->function = dasd_3990_erp_action_1B_32;
	erp->refers = default_erp->refers;
	erp->device = device;
	erp->magic = default_erp->magic;
	erp->expires = 0;
	erp->retries = 256;
	erp->buildclk = get_clock();
	erp->status = DASD_CQR_FILLED;

	/* remove the default erp */
	dasd_free_erp_request(default_erp, device);

	return erp;

}				/* end dasd_3990_erp_action_1B_32 */

/*
 * DASD_3990_UPDATE_1B
 *
 * DESCRIPTION
 *   Handles the update to the 32 byte 'Action 1B' of Single Program 
 *   Action Codes in case the first action was not successful.
 *   The already created 'previous_erp' is the currently not successful
 *   ERP. 
 *
 * PARAMETER
 *   previous_erp	already created previous erp.
 *   sense		current sense data 
 * RETURN VALUES
 *   erp		modified erp 
 */
static struct dasd_ccw_req *
dasd_3990_update_1B(struct dasd_ccw_req * previous_erp, char *sense)
{

	struct dasd_device *device = previous_erp->device;
	__u32 cpa = 0;
	struct dasd_ccw_req *cqr;
	struct dasd_ccw_req *erp;
	char *LO_data;		/* struct LO_eckd_data */
	struct ccw1 *ccw;

	DEV_MESSAGE(KERN_DEBUG, device, "%s",
		    "Write not finished because of unexpected condition"
		    " - follow on");

	/* determine the original cqr */
	cqr = previous_erp;

	while (cqr->refers != NULL) {
		cqr = cqr->refers;
	}

	/* for imprecise ending just do default erp */
	if (sense[1] & 0x01) {

		DEV_MESSAGE(KERN_DEBUG, device, "%s",
			    "Imprecise ending is set - just retry");

		previous_erp->status = DASD_CQR_QUEUED;

		return previous_erp;
	}

	/* determine the address of the CCW to be restarted */
	/* Imprecise ending is not set -> addr from IRB-SCSW */
	cpa = previous_erp->irb.scsw.cpa;

	if (cpa == 0) {

		DEV_MESSAGE(KERN_DEBUG, device, "%s",
			    "Unable to determine address of the CCW "
			    "to be restarted");

		previous_erp->status = DASD_CQR_FAILED;

		return previous_erp;
	}

	erp = previous_erp;

	/* update the LO with the new returned sense data  */
	LO_data = erp->data + sizeof (struct DE_eckd_data);

	if ((sense[3] == 0x01) && (LO_data[1] & 0x01)) {

		DEV_MESSAGE(KERN_ERR, device, "%s",
			    "BUG - this should not happen");

		previous_erp->status = DASD_CQR_FAILED;

		return previous_erp;
	}

	if ((sense[7] & 0x3F) == 0x01) {
		/* operation code is WRITE DATA -> data area orientation */
		LO_data[0] = 0x81;

	} else if ((sense[7] & 0x3F) == 0x03) {
		/* operation code is FORMAT WRITE -> index orientation */
		LO_data[0] = 0xC3;

	} else {
		LO_data[0] = sense[7];	/* operation */
	}

	LO_data[1] = sense[8];	/* auxiliary */
	LO_data[2] = sense[9];
	LO_data[3] = sense[3];	/* count */
	LO_data[4] = sense[29];	/* seek_addr.cyl */
	LO_data[5] = sense[30];	/* seek_addr.cyl 2nd byte */
	LO_data[7] = sense[31];	/* seek_addr.head 2nd byte */

	memcpy(&(LO_data[8]), &(sense[11]), 8);

	/* TIC to the failed ccw */
	ccw = erp->cpaddr;	/* addr of DE ccw */
	ccw++;			/* addr of LE ccw */
	ccw++;			/* addr of TIC ccw */
	ccw->cda = cpa;

	erp->status = DASD_CQR_QUEUED;

	return erp;

}				/* end dasd_3990_update_1B */

/*
 * DASD_3990_ERP_COMPOUND_RETRY 
 *
 * DESCRIPTION
 *   Handles the compound ERP action retry code.
 *   NOTE: At least one retry is done even if zero is specified
 *	   by the sense data. This makes enqueueing of the request
 *	   easier.
 *
 * PARAMETER
 *   sense		sense data of the actual error
 *   erp		pointer to the currently created ERP
 *
 * RETURN VALUES
 *   erp		modified ERP pointer
 *
 */
static void
dasd_3990_erp_compound_retry(struct dasd_ccw_req * erp, char *sense)
{

	switch (sense[25] & 0x03) {
	case 0x00:		/* no not retry */
		erp->retries = 1;
		break;

	case 0x01:		/* retry 2 times */
		erp->retries = 2;
		break;

	case 0x02:		/* retry 10 times */
		erp->retries = 10;
		break;

	case 0x03:		/* retry 256 times */
		erp->retries = 256;
		break;

	default:
		BUG();
	}

	erp->function = dasd_3990_erp_compound_retry;

}				/* end dasd_3990_erp_compound_retry */

/*
 * DASD_3990_ERP_COMPOUND_PATH 
 *
 * DESCRIPTION
 *   Handles the compound ERP action for retry on alternate
 *   channel path.
 *
 * PARAMETER
 *   sense		sense data of the actual error
 *   erp		pointer to the currently created ERP
 *
 * RETURN VALUES
 *   erp		modified ERP pointer
 *
 */
static void
dasd_3990_erp_compound_path(struct dasd_ccw_req * erp, char *sense)
{

	if (sense[25] & DASD_SENSE_BIT_3) {
		dasd_3990_erp_alternate_path(erp);

		if (erp->status == DASD_CQR_FAILED) {
			/* reset the lpm and the status to be able to 
			 * try further actions. */

			erp->lpm = 0;

			erp->status = DASD_CQR_ERROR;

		}
	}

	erp->function = dasd_3990_erp_compound_path;

}				/* end dasd_3990_erp_compound_path */

/*
 * DASD_3990_ERP_COMPOUND_CODE 
 *
 * DESCRIPTION
 *   Handles the compound ERP action for retry code.
 *
 * PARAMETER
 *   sense		sense data of the actual error
 *   erp		pointer to the currently created ERP
 *
 * RETURN VALUES
 *   erp		NEW ERP pointer
 *
 */
static struct dasd_ccw_req *
dasd_3990_erp_compound_code(struct dasd_ccw_req * erp, char *sense)
{

	if (sense[25] & DASD_SENSE_BIT_2) {

		switch (sense[28]) {
		case 0x17:
			/* issue a Diagnostic Control command with an 
			 * Inhibit Write subcommand and controler modifier */
			erp = dasd_3990_erp_DCTL(erp, 0x20);
			break;
			
		case 0x25:
			/* wait for 5 seconds and retry again */
			erp->retries = 1;
			
			dasd_3990_erp_block_queue (erp, 5*HZ);
			break;
			
		default:
			/* should not happen - continue */
			break;
		}
	}

	erp->function = dasd_3990_erp_compound_code;

	return erp;

}				/* end dasd_3990_erp_compound_code */

/*
 * DASD_3990_ERP_COMPOUND_CONFIG 
 *
 * DESCRIPTION
 *   Handles the compound ERP action for configruation
 *   dependent error.
 *   Note: duplex handling is not implemented (yet).
 *
 * PARAMETER
 *   sense		sense data of the actual error
 *   erp		pointer to the currently created ERP
 *
 * RETURN VALUES
 *   erp		modified ERP pointer
 *
 */
static void
dasd_3990_erp_compound_config(struct dasd_ccw_req * erp, char *sense)
{

	if ((sense[25] & DASD_SENSE_BIT_1) && (sense[26] & DASD_SENSE_BIT_2)) {

		/* set to suspended duplex state then restart */
		struct dasd_device *device = erp->device;

		DEV_MESSAGE(KERN_ERR, device, "%s",
			    "Set device to suspended duplex state should be "
			    "done!\n"
			    "This is not implemented yet (for compound ERP)"
			    " - please report to linux390@de.ibm.com");

	}

	erp->function = dasd_3990_erp_compound_config;

}				/* end dasd_3990_erp_compound_config */

/*
 * DASD_3990_ERP_COMPOUND 
 *
 * DESCRIPTION
 *   Does the further compound program action if 
 *   compound retry was not successful.
 *
 * PARAMETER
 *   sense		sense data of the actual error
 *   erp		pointer to the current (failed) ERP
 *
 * RETURN VALUES
 *   erp		(additional) ERP pointer
 *
 */
static struct dasd_ccw_req *
dasd_3990_erp_compound(struct dasd_ccw_req * erp, char *sense)
{

	if ((erp->function == dasd_3990_erp_compound_retry) &&
	    (erp->status == DASD_CQR_ERROR)) {

		dasd_3990_erp_compound_path(erp, sense);
	}

	if ((erp->function == dasd_3990_erp_compound_path) &&
	    (erp->status == DASD_CQR_ERROR)) {

		erp = dasd_3990_erp_compound_code(erp, sense);
	}

	if ((erp->function == dasd_3990_erp_compound_code) &&
	    (erp->status == DASD_CQR_ERROR)) {

		dasd_3990_erp_compound_config(erp, sense);
	}

	/* if no compound action ERP specified, the request failed */
	if (erp->status == DASD_CQR_ERROR) {

		erp->status = DASD_CQR_FAILED;
	}

	return erp;

}				/* end dasd_3990_erp_compound */

/*
 * DASD_3990_ERP_INSPECT_32 
 *
 * DESCRIPTION
 *   Does a detailed inspection of the 32 byte sense data
 *   and sets up a related error recovery action.  
 *
 * PARAMETER
 *   sense		sense data of the actual error
 *   erp		pointer to the currently created default ERP
 *
 * RETURN VALUES
 *   erp_filled		pointer to the ERP
 *
 */
static struct dasd_ccw_req *
dasd_3990_erp_inspect_32(struct dasd_ccw_req * erp, char *sense)
{

	struct dasd_device *device = erp->device;

	erp->function = dasd_3990_erp_inspect_32;

	if (sense[25] & DASD_SENSE_BIT_0) {

		/* compound program action codes (byte25 bit 0 == '1') */
		dasd_3990_erp_compound_retry(erp, sense);

	} else {

		/* single program action codes (byte25 bit 0 == '0') */
		switch (sense[25]) {

		case 0x00:	/* success - use default ERP for retries */
		        DEV_MESSAGE(KERN_DEBUG, device, "%s",
				    "ERP called for successful request"
				    " - just retry");
			break;

		case 0x01:	/* fatal error */
			DEV_MESSAGE(KERN_ERR, device, "%s",
				    "Fatal error should have been "
				    "handled within the interrupt handler");

			erp = dasd_3990_erp_cleanup(erp, DASD_CQR_FAILED);
			break;

		case 0x02:	/* intervention required */
		case 0x03:	/* intervention required during dual copy */
			erp = dasd_3990_erp_int_req(erp);
			break;

		case 0x0F:  /* length mismatch during update write command */
			DEV_MESSAGE(KERN_ERR, device, "%s",
				    "update write command error - should not "
				    "happen;\n"
				    "Please send this message together with "
				    "the above sense data to linux390@de."
				    "ibm.com");

			erp = dasd_3990_erp_cleanup(erp, DASD_CQR_FAILED);
			break;

		case 0x10:  /* logging required for other channel program */
			erp = dasd_3990_erp_action_10_32(erp, sense);
			break;

		case 0x15:	/* next track outside defined extend */
			DEV_MESSAGE(KERN_ERR, device, "%s",
				    "next track outside defined extend - "
				    "should not happen;\n"
				    "Please send this message together with "
				    "the above sense data to linux390@de."
				    "ibm.com");

			erp = dasd_3990_erp_cleanup(erp, DASD_CQR_FAILED);
			break;

		case 0x1B:	/* unexpected condition during write */

			erp = dasd_3990_erp_action_1B_32(erp, sense);
			break;

		case 0x1C:	/* invalid data */
			DEV_MESSAGE(KERN_EMERG, device, "%s",
				    "Data recovered during retry with PCI "
				    "fetch mode active");

			/* not possible to handle this situation in Linux */
			panic
			    ("Invalid data - No way to inform application "
			     "about the possibly incorrect data");
			break;

		case 0x1D:	/* state-change pending */
			DEV_MESSAGE(KERN_DEBUG, device, "%s",
				    "A State change pending condition exists "
				    "for the subsystem or device");

			erp = dasd_3990_erp_action_4(erp, sense);
			break;

		case 0x1E:	/* busy */
                        DEV_MESSAGE(KERN_DEBUG, device, "%s",
				    "Busy condition exists "
				    "for the subsystem or device");
                        erp = dasd_3990_erp_action_4(erp, sense);
			break;

		default:	/* all others errors - default erp  */
			break;
		}
	}

	return erp;

}				/* end dasd_3990_erp_inspect_32 */

/*
 ***************************************************************************** 
 * main ERP control fuctions (24 and 32 byte sense)
 ***************************************************************************** 
 */

/*
 * DASD_3990_ERP_INSPECT
 *
 * DESCRIPTION
 *   Does a detailed inspection for sense data by calling either
 *   the 24-byte or the 32-byte inspection routine.
 *
 * PARAMETER
 *   erp		pointer to the currently created default ERP
 * RETURN VALUES
 *   erp_new		contens was possibly modified 
 */
static struct dasd_ccw_req *
dasd_3990_erp_inspect(struct dasd_ccw_req * erp)
{

	struct dasd_ccw_req *erp_new = NULL;
	/* sense data are located in the refers record of the */
	/* already set up new ERP !			      */
	char *sense = erp->refers->irb.ecw;

	/* distinguish between 24 and 32 byte sense data */
	if (sense[27] & DASD_SENSE_BIT_0) {

		/* inspect the 24 byte sense data */
		erp_new = dasd_3990_erp_inspect_24(erp, sense);

	} else {

		/* inspect the 32 byte sense data */
		erp_new = dasd_3990_erp_inspect_32(erp, sense);

	}	/* end distinguish between 24 and 32 byte sense data */

	return erp_new;
}

/*
 * DASD_3990_ERP_ADD_ERP
 * 
 * DESCRIPTION
 *   This funtion adds an additional request block (ERP) to the head of
 *   the given cqr (or erp).
 *   This erp is initialized as an default erp (retry TIC)
 *
 * PARAMETER
 *   cqr		head of the current ERP-chain (or single cqr if 
 *			first error)
 * RETURN VALUES
 *   erp		pointer to new ERP-chain head
 */
static struct dasd_ccw_req *
dasd_3990_erp_add_erp(struct dasd_ccw_req * cqr)
{

	struct dasd_device *device = cqr->device;
	struct ccw1 *ccw;

	/* allocate additional request block */
	struct dasd_ccw_req *erp;

	erp = dasd_alloc_erp_request((char *) &cqr->magic, 2, 0, cqr->device);
	if (IS_ERR(erp)) {
                if (cqr->retries <= 0) {
		        DEV_MESSAGE(KERN_ERR, device, "%s",
				    "Unable to allocate ERP request");
			cqr->status = DASD_CQR_FAILED;
                        cqr->stopclk = get_clock ();
		} else {
                        DEV_MESSAGE (KERN_ERR, device,
                                     "Unable to allocate ERP request "
				     "(%i retries left)",
                                     cqr->retries);
			dasd_set_timer(device, (HZ << 3));
                }
		return cqr;
	}

	/* initialize request with default TIC to current ERP/CQR */
	ccw = erp->cpaddr;
	ccw->cmd_code = CCW_CMD_NOOP;
	ccw->flags = CCW_FLAG_CC;
	ccw++;
	ccw->cmd_code = CCW_CMD_TIC;
	ccw->cda      = (long)(cqr->cpaddr);
	erp->function = dasd_3990_erp_add_erp;
	erp->refers   = cqr;
	erp->device   = cqr->device;
	erp->magic    = cqr->magic;
	erp->expires  = 0;
	erp->retries  = 256;
	erp->buildclk = get_clock();

	erp->status = DASD_CQR_FILLED;

	return erp;
}

/*
 * DASD_3990_ERP_ADDITIONAL_ERP 
 * 
 * DESCRIPTION
 *   An additional ERP is needed to handle the current error.
 *   Add ERP to the head of the ERP-chain containing the ERP processing
 *   determined based on the sense data.
 *
 * PARAMETER
 *   cqr		head of the current ERP-chain (or single cqr if 
 *			first error)
 *
 * RETURN VALUES
 *   erp		pointer to new ERP-chain head
 */
static struct dasd_ccw_req *
dasd_3990_erp_additional_erp(struct dasd_ccw_req * cqr)
{

	struct dasd_ccw_req *erp = NULL;

	/* add erp and initialize with default TIC */
	erp = dasd_3990_erp_add_erp(cqr);

	/* inspect sense, determine specific ERP if possible */
	if (erp != cqr) {

		erp = dasd_3990_erp_inspect(erp);
	}

	return erp;

}				/* end dasd_3990_erp_additional_erp */

/*
 * DASD_3990_ERP_ERROR_MATCH
 *
 * DESCRIPTION
 *   Check if the device status of the given cqr is the same.
 *   This means that the failed CCW and the relevant sense data
 *   must match.
 *   I don't distinguish between 24 and 32 byte sense because in case of
 *   24 byte sense byte 25 and 27 is set as well.
 *
 * PARAMETER
 *   cqr1		first cqr, which will be compared with the 
 *   cqr2		second cqr.
 *
 * RETURN VALUES
 *   match		'boolean' for match found
 *			returns 1 if match found, otherwise 0.
 */
static int
dasd_3990_erp_error_match(struct dasd_ccw_req *cqr1, struct dasd_ccw_req *cqr2)
{

	/* check failed CCW */
	if (cqr1->irb.scsw.cpa != cqr2->irb.scsw.cpa) {
		//	return 0;	/* CCW doesn't match */
	}

	/* check sense data; byte 0-2,25,27 */
	if (!((memcmp (cqr1->irb.ecw, cqr2->irb.ecw, 3) == 0) &&
	      (cqr1->irb.ecw[27] == cqr2->irb.ecw[27]) &&
	      (cqr1->irb.ecw[25] == cqr2->irb.ecw[25]))) {

		return 0;	/* sense doesn't match */
	}

	return 1;		/* match */

}				/* end dasd_3990_erp_error_match */

/*
 * DASD_3990_ERP_IN_ERP
 *
 * DESCRIPTION
 *   check if the current error already happened before.
 *   quick exit if current cqr is not an ERP (cqr->refers=NULL)
 *
 * PARAMETER
 *   cqr		failed cqr (either original cqr or already an erp)
 *
 * RETURN VALUES
 *   erp		erp-pointer to the already defined error 
 *			recovery procedure OR
 *			NULL if a 'new' error occurred.
 */
static struct dasd_ccw_req *
dasd_3990_erp_in_erp(struct dasd_ccw_req *cqr)
{

	struct dasd_ccw_req *erp_head = cqr,	/* save erp chain head */
	*erp_match = NULL;	/* save erp chain head */
	int match = 0;		/* 'boolean' for matching error found */

	if (cqr->refers == NULL) {	/* return if not in erp */
		return NULL;
	}

	/* check the erp/cqr chain for current error */
	do {
		match = dasd_3990_erp_error_match(erp_head, cqr->refers);
		erp_match = cqr;	/* save possible matching erp  */
		cqr = cqr->refers;	/* check next erp/cqr in queue */

	} while ((cqr->refers != NULL) && (!match));

	if (!match) {
		return NULL;	/* no match was found */
	}

	return erp_match;	/* return address of matching erp */

}				/* END dasd_3990_erp_in_erp */

/*
 * DASD_3990_ERP_FURTHER_ERP (24 & 32 byte sense)
 *
 * DESCRIPTION
 *   No retry is left for the current ERP. Check what has to be done 
 *   with the ERP.
 *     - do further defined ERP action or
 *     - wait for interrupt or	
 *     - exit with permanent error
 *
 * PARAMETER
 *   erp		ERP which is in progress with no retry left
 *
 * RETURN VALUES
 *   erp		modified/additional ERP
 */
static struct dasd_ccw_req *
dasd_3990_erp_further_erp(struct dasd_ccw_req *erp)
{

	struct dasd_device *device = erp->device;
	char *sense = erp->irb.ecw;

	/* check for 24 byte sense ERP */
	if ((erp->function == dasd_3990_erp_bus_out) ||
	    (erp->function == dasd_3990_erp_action_1) ||
	    (erp->function == dasd_3990_erp_action_4)) {

		erp = dasd_3990_erp_action_1(erp);

	} else if (erp->function == dasd_3990_erp_action_5) {

		/* retries have not been successful */
		/* prepare erp for retry on different channel path */
		erp = dasd_3990_erp_action_1(erp);

		if (!(sense[2] & DASD_SENSE_BIT_0)) {

			/* issue a Diagnostic Control command with an 
			 * Inhibit Write subcommand */

			switch (sense[25]) {
			case 0x17:
			case 0x57:{	/* controller */
					erp = dasd_3990_erp_DCTL(erp, 0x20);
					break;
				}
			case 0x18:
			case 0x58:{	/* channel path */
					erp = dasd_3990_erp_DCTL(erp, 0x40);
					break;
				}
			case 0x19:
			case 0x59:{	/* storage director */
					erp = dasd_3990_erp_DCTL(erp, 0x80);
					break;
				}
			default:
				DEV_MESSAGE(KERN_DEBUG, device,
					    "invalid subcommand modifier 0x%x "
					    "for Diagnostic Control Command",
					    sense[25]);
			}
		}

		/* check for 32 byte sense ERP */
	} else if ((erp->function == dasd_3990_erp_compound_retry) ||
		   (erp->function == dasd_3990_erp_compound_path) ||
		   (erp->function == dasd_3990_erp_compound_code) ||
		   (erp->function == dasd_3990_erp_compound_config)) {

		erp = dasd_3990_erp_compound(erp, sense);

	} else {
		/* No retry left and no additional special handling */
		/*necessary */
		DEV_MESSAGE(KERN_ERR, device,
			    "no retries left for erp %p - "
			    "set status to FAILED", erp);

		erp->status = DASD_CQR_FAILED;
	}

	return erp;

}				/* end dasd_3990_erp_further_erp */

/*
 * DASD_3990_ERP_HANDLE_MATCH_ERP 
 *
 * DESCRIPTION
 *   An error occurred again and an ERP has been detected which is already
 *   used to handle this error (e.g. retries). 
 *   All prior ERP's are asumed to be successful and therefore removed
 *   from queue.
 *   If retry counter of matching erp is already 0, it is checked if further 
 *   action is needed (besides retry) or if the ERP has failed.
 *
 * PARAMETER
 *   erp_head		first ERP in ERP-chain
 *   erp		ERP that handles the actual error.
 *			(matching erp)
 *
 * RETURN VALUES
 *   erp		modified/additional ERP
 */
static struct dasd_ccw_req *
dasd_3990_erp_handle_match_erp(struct dasd_ccw_req *erp_head,
			       struct dasd_ccw_req *erp)
{

	struct dasd_device *device = erp_head->device;
	struct dasd_ccw_req *erp_done = erp_head;	/* finished req */
	struct dasd_ccw_req *erp_free = NULL;	/* req to be freed */

	/* loop over successful ERPs and remove them from chanq */
	while (erp_done != erp) {

		if (erp_done == NULL)	/* end of chain reached */
			panic(PRINTK_HEADER "Programming error in ERP! The "
			      "original request was lost\n");

		/* remove the request from the device queue */
		list_del(&erp_done->list);

		erp_free = erp_done;
		erp_done = erp_done->refers;

		/* free the finished erp request */
		dasd_free_erp_request(erp_free, erp_free->device);

	}			/* end while */

	if (erp->retries > 0) {

		char *sense = erp->refers->irb.ecw;

		/* check for special retries */
		if (erp->function == dasd_3990_erp_action_4) {

			erp = dasd_3990_erp_action_4(erp, sense);

		} else if (erp->function == dasd_3990_erp_action_1B_32) {

			erp = dasd_3990_update_1B(erp, sense);

		} else if (erp->function == dasd_3990_erp_int_req) {

			erp = dasd_3990_erp_int_req(erp);

		} else {
			/* simple retry	  */
			DEV_MESSAGE(KERN_DEBUG, device,
				    "%i retries left for erp %p",
				    erp->retries, erp);

			/* handle the request again... */
			erp->status = DASD_CQR_QUEUED;
		}

	} else {
		/* no retry left - check for further necessary action	 */
		/* if no further actions, handle rest as permanent error */
		erp = dasd_3990_erp_further_erp(erp);
	}

	return erp;

}				/* end dasd_3990_erp_handle_match_erp */

/*
 * DASD_3990_ERP_ACTION
 *
 * DESCRIPTION
 *   controll routine for 3990 erp actions.
 *   Has to be called with the queue lock (namely the s390_irq_lock) acquired.
 *
 * PARAMETER
 *   cqr		failed cqr (either original cqr or already an erp)
 *
 * RETURN VALUES
 *   erp		erp-pointer to the head of the ERP action chain.
 *			This means:
 *			 - either a ptr to an additional ERP cqr or
 *			 - the original given cqr (which's status might 
 *			   be modified)
 */
struct dasd_ccw_req *
dasd_3990_erp_action(struct dasd_ccw_req * cqr)
{

	struct dasd_ccw_req *erp = NULL;
	struct dasd_device *device = cqr->device;
	__u32 cpa = cqr->irb.scsw.cpa;

#ifdef ERP_DEBUG
	/* print current erp_chain */
	DEV_MESSAGE(KERN_ERR, device, "%s",
		    "ERP chain at BEGINNING of ERP-ACTION");
	{
		struct dasd_ccw_req *temp_erp = NULL;

		for (temp_erp = cqr;
		     temp_erp != NULL; temp_erp = temp_erp->refers) {

			DEV_MESSAGE(KERN_ERR, device,
				    "   erp %p (%02x) refers to %p",
				    temp_erp, temp_erp->status,
				    temp_erp->refers);
		}
	}
#endif				/* ERP_DEBUG */

	/* double-check if current erp/cqr was successfull */
	if ((cqr->irb.scsw.cstat == 0x00) &&
	    (cqr->irb.scsw.dstat == (DEV_STAT_CHN_END|DEV_STAT_DEV_END))) {

		DEV_MESSAGE(KERN_DEBUG, device,
			    "ERP called for successful request %p"
			    " - NO ERP necessary", cqr);

		cqr->status = DASD_CQR_DONE;

		return cqr;
	}
	/* check if sense data are available */
	if (!cqr->irb.ecw) {
		DEV_MESSAGE(KERN_DEBUG, device,
			    "ERP called witout sense data avail ..."
			    "request %p - NO ERP possible", cqr);

		cqr->status = DASD_CQR_FAILED;

		return cqr;

	}

	/* check if error happened before */
	erp = dasd_3990_erp_in_erp(cqr);

	if (erp == NULL) {
		/* no matching erp found - set up erp */
		erp = dasd_3990_erp_additional_erp(cqr);
	} else {
		/* matching erp found - set all leading erp's to DONE */
		erp = dasd_3990_erp_handle_match_erp(cqr, erp);
	}

#ifdef ERP_DEBUG
	/* print current erp_chain */
	DEV_MESSAGE(KERN_ERR, device, "%s", "ERP chain at END of ERP-ACTION");
	{
		struct dasd_ccw_req *temp_erp = NULL;
		for (temp_erp = erp;
		     temp_erp != NULL; temp_erp = temp_erp->refers) {

			DEV_MESSAGE(KERN_ERR, device,
				    "   erp %p (%02x) refers to %p",
				    temp_erp, temp_erp->status,
				    temp_erp->refers);
		}
	}
#endif				/* ERP_DEBUG */

	if (erp->status == DASD_CQR_FAILED)
		dasd_log_ccw(erp, 1, cpa);

	/* enqueue added ERP request */
	if (erp->status == DASD_CQR_FILLED) {
		erp->status = DASD_CQR_QUEUED;
		list_add(&erp->list, &device->ccw_queue);
	}

	return erp;

}				/* end dasd_3990_erp_action */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4 
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: 1
 * tab-width: 8
 * End:
 */
