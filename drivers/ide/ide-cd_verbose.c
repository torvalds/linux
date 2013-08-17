/*
 * Verbose error logging for ATAPI CD/DVD devices.
 *
 * Copyright (C) 1994-1996  Scott Snyder <snyder@fnald0.fnal.gov>
 * Copyright (C) 1996-1998  Erik Andersen <andersee@debian.org>
 * Copyright (C) 1998-2000  Jens Axboe <axboe@suse.de>
 */

#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/cdrom.h>
#include <scsi/scsi.h>

#ifndef CONFIG_BLK_DEV_IDECD_VERBOSE_ERRORS
void ide_cd_log_error(const char *name, struct request *failed_command,
		      struct request_sense *sense)
{
	/* Suppress printing unit attention and `in progress of becoming ready'
	   errors when we're not being verbose. */
	if (sense->sense_key == UNIT_ATTENTION ||
	    (sense->sense_key == NOT_READY && (sense->asc == 4 ||
						sense->asc == 0x3a)))
		return;

	printk(KERN_ERR "%s: error code: 0x%02x  sense_key: 0x%02x  "
			"asc: 0x%02x  ascq: 0x%02x\n",
			name, sense->error_code, sense->sense_key,
			sense->asc, sense->ascq);
}
#else
/* The generic packet command opcodes for CD/DVD Logical Units,
 * From Table 57 of the SFF8090 Ver. 3 (Mt. Fuji) draft standard. */
static const struct {
	unsigned short packet_command;
	const char * const text;
} packet_command_texts[] = {
	{ GPCMD_TEST_UNIT_READY, "Test Unit Ready" },
	{ GPCMD_REQUEST_SENSE, "Request Sense" },
	{ GPCMD_FORMAT_UNIT, "Format Unit" },
	{ GPCMD_INQUIRY, "Inquiry" },
	{ GPCMD_START_STOP_UNIT, "Start/Stop Unit" },
	{ GPCMD_PREVENT_ALLOW_MEDIUM_REMOVAL, "Prevent/Allow Medium Removal" },
	{ GPCMD_READ_FORMAT_CAPACITIES, "Read Format Capacities" },
	{ GPCMD_READ_CDVD_CAPACITY, "Read Cd/Dvd Capacity" },
	{ GPCMD_READ_10, "Read 10" },
	{ GPCMD_WRITE_10, "Write 10" },
	{ GPCMD_SEEK, "Seek" },
	{ GPCMD_WRITE_AND_VERIFY_10, "Write and Verify 10" },
	{ GPCMD_VERIFY_10, "Verify 10" },
	{ GPCMD_FLUSH_CACHE, "Flush Cache" },
	{ GPCMD_READ_SUBCHANNEL, "Read Subchannel" },
	{ GPCMD_READ_TOC_PMA_ATIP, "Read Table of Contents" },
	{ GPCMD_READ_HEADER, "Read Header" },
	{ GPCMD_PLAY_AUDIO_10, "Play Audio 10" },
	{ GPCMD_GET_CONFIGURATION, "Get Configuration" },
	{ GPCMD_PLAY_AUDIO_MSF, "Play Audio MSF" },
	{ GPCMD_PLAYAUDIO_TI, "Play Audio TrackIndex" },
	{ GPCMD_GET_EVENT_STATUS_NOTIFICATION,
		"Get Event Status Notification" },
	{ GPCMD_PAUSE_RESUME, "Pause/Resume" },
	{ GPCMD_STOP_PLAY_SCAN, "Stop Play/Scan" },
	{ GPCMD_READ_DISC_INFO, "Read Disc Info" },
	{ GPCMD_READ_TRACK_RZONE_INFO, "Read Track Rzone Info" },
	{ GPCMD_RESERVE_RZONE_TRACK, "Reserve Rzone Track" },
	{ GPCMD_SEND_OPC, "Send OPC" },
	{ GPCMD_MODE_SELECT_10, "Mode Select 10" },
	{ GPCMD_REPAIR_RZONE_TRACK, "Repair Rzone Track" },
	{ GPCMD_MODE_SENSE_10, "Mode Sense 10" },
	{ GPCMD_CLOSE_TRACK, "Close Track" },
	{ GPCMD_BLANK, "Blank" },
	{ GPCMD_SEND_EVENT, "Send Event" },
	{ GPCMD_SEND_KEY, "Send Key" },
	{ GPCMD_REPORT_KEY, "Report Key" },
	{ GPCMD_LOAD_UNLOAD, "Load/Unload" },
	{ GPCMD_SET_READ_AHEAD, "Set Read-ahead" },
	{ GPCMD_READ_12, "Read 12" },
	{ GPCMD_GET_PERFORMANCE, "Get Performance" },
	{ GPCMD_SEND_DVD_STRUCTURE, "Send DVD Structure" },
	{ GPCMD_READ_DVD_STRUCTURE, "Read DVD Structure" },
	{ GPCMD_SET_STREAMING, "Set Streaming" },
	{ GPCMD_READ_CD_MSF, "Read CD MSF" },
	{ GPCMD_SCAN, "Scan" },
	{ GPCMD_SET_SPEED, "Set Speed" },
	{ GPCMD_PLAY_CD, "Play CD" },
	{ GPCMD_MECHANISM_STATUS, "Mechanism Status" },
	{ GPCMD_READ_CD, "Read CD" },
};

/* From Table 303 of the SFF8090 Ver. 3 (Mt. Fuji) draft standard. */
static const char * const sense_key_texts[16] = {
	"No sense data",
	"Recovered error",
	"Not ready",
	"Medium error",
	"Hardware error",
	"Illegal request",
	"Unit attention",
	"Data protect",
	"Blank check",
	"(reserved)",
	"(reserved)",
	"Aborted command",
	"(reserved)",
	"(reserved)",
	"Miscompare",
	"(reserved)",
};

/* From Table 304 of the SFF8090 Ver. 3 (Mt. Fuji) draft standard. */
static const struct {
	unsigned long asc_ascq;
	const char * const text;
} sense_data_texts[] = {
	{ 0x000000, "No additional sense information" },
	{ 0x000011, "Play operation in progress" },
	{ 0x000012, "Play operation paused" },
	{ 0x000013, "Play operation successfully completed" },
	{ 0x000014, "Play operation stopped due to error" },
	{ 0x000015, "No current audio status to return" },
	{ 0x010c0a, "Write error - padding blocks added" },
	{ 0x011700, "Recovered data with no error correction applied" },
	{ 0x011701, "Recovered data with retries" },
	{ 0x011702, "Recovered data with positive head offset" },
	{ 0x011703, "Recovered data with negative head offset" },
	{ 0x011704, "Recovered data with retries and/or CIRC applied" },
	{ 0x011705, "Recovered data using previous sector ID" },
	{ 0x011800, "Recovered data with error correction applied" },
	{ 0x011801, "Recovered data with error correction and retries applied"},
	{ 0x011802, "Recovered data - the data was auto-reallocated" },
	{ 0x011803, "Recovered data with CIRC" },
	{ 0x011804, "Recovered data with L-EC" },
	{ 0x015d00, "Failure prediction threshold exceeded"
		    " - Predicted logical unit failure" },
	{ 0x015d01, "Failure prediction threshold exceeded"
		    " - Predicted media failure" },
	{ 0x015dff, "Failure prediction threshold exceeded - False" },
	{ 0x017301, "Power calibration area almost full" },
	{ 0x020400, "Logical unit not ready - cause not reportable" },
	/* Following is misspelled in ATAPI 2.6, _and_ in Mt. Fuji */
	{ 0x020401, "Logical unit not ready"
		    " - in progress [sic] of becoming ready" },
	{ 0x020402, "Logical unit not ready - initializing command required" },
	{ 0x020403, "Logical unit not ready - manual intervention required" },
	{ 0x020404, "Logical unit not ready - format in progress" },
	{ 0x020407, "Logical unit not ready - operation in progress" },
	{ 0x020408, "Logical unit not ready - long write in progress" },
	{ 0x020600, "No reference position found (media may be upside down)" },
	{ 0x023000, "Incompatible medium installed" },
	{ 0x023a00, "Medium not present" },
	{ 0x025300, "Media load or eject failed" },
	{ 0x025700, "Unable to recover table of contents" },
	{ 0x030300, "Peripheral device write fault" },
	{ 0x030301, "No write current" },
	{ 0x030302, "Excessive write errors" },
	{ 0x030c00, "Write error" },
	{ 0x030c01, "Write error - Recovered with auto reallocation" },
	{ 0x030c02, "Write error - auto reallocation failed" },
	{ 0x030c03, "Write error - recommend reassignment" },
	{ 0x030c04, "Compression check miscompare error" },
	{ 0x030c05, "Data expansion occurred during compress" },
	{ 0x030c06, "Block not compressible" },
	{ 0x030c07, "Write error - recovery needed" },
	{ 0x030c08, "Write error - recovery failed" },
	{ 0x030c09, "Write error - loss of streaming" },
	{ 0x031100, "Unrecovered read error" },
	{ 0x031106, "CIRC unrecovered error" },
	{ 0x033101, "Format command failed" },
	{ 0x033200, "No defect spare location available" },
	{ 0x033201, "Defect list update failure" },
	{ 0x035100, "Erase failure" },
	{ 0x037200, "Session fixation error" },
	{ 0x037201, "Session fixation error writin lead-in" },
	{ 0x037202, "Session fixation error writin lead-out" },
	{ 0x037300, "CD control error" },
	{ 0x037302, "Power calibration area is full" },
	{ 0x037303, "Power calibration area error" },
	{ 0x037304, "Program memory area / RMA update failure" },
	{ 0x037305, "Program memory area / RMA is full" },
	{ 0x037306, "Program memory area / RMA is (almost) full" },
	{ 0x040200, "No seek complete" },
	{ 0x040300, "Write fault" },
	{ 0x040900, "Track following error" },
	{ 0x040901, "Tracking servo failure" },
	{ 0x040902, "Focus servo failure" },
	{ 0x040903, "Spindle servo failure" },
	{ 0x041500, "Random positioning error" },
	{ 0x041501, "Mechanical positioning or changer error" },
	{ 0x041502, "Positioning error detected by read of medium" },
	{ 0x043c00, "Mechanical positioning or changer error" },
	{ 0x044000, "Diagnostic failure on component (ASCQ)" },
	{ 0x044400, "Internal CD/DVD logical unit failure" },
	{ 0x04b600, "Media load mechanism failed" },
	{ 0x051a00, "Parameter list length error" },
	{ 0x052000, "Invalid command operation code" },
	{ 0x052100, "Logical block address out of range" },
	{ 0x052102, "Invalid address for write" },
	{ 0x052400, "Invalid field in command packet" },
	{ 0x052600, "Invalid field in parameter list" },
	{ 0x052601, "Parameter not supported" },
	{ 0x052602, "Parameter value invalid" },
	{ 0x052700, "Write protected media" },
	{ 0x052c00, "Command sequence error" },
	{ 0x052c03, "Current program area is not empty" },
	{ 0x052c04, "Current program area is empty" },
	{ 0x053001, "Cannot read medium - unknown format" },
	{ 0x053002, "Cannot read medium - incompatible format" },
	{ 0x053900, "Saving parameters not supported" },
	{ 0x054e00, "Overlapped commands attempted" },
	{ 0x055302, "Medium removal prevented" },
	{ 0x055500, "System resource failure" },
	{ 0x056300, "End of user area encountered on this track" },
	{ 0x056400, "Illegal mode for this track or incompatible medium" },
	{ 0x056f00, "Copy protection key exchange failure"
		    " - Authentication failure" },
	{ 0x056f01, "Copy protection key exchange failure - Key not present" },
	{ 0x056f02, "Copy protection key exchange failure"
		     " - Key not established" },
	{ 0x056f03, "Read of scrambled sector without authentication" },
	{ 0x056f04, "Media region code is mismatched to logical unit" },
	{ 0x056f05, "Drive region must be permanent"
		    " / region reset count error" },
	{ 0x057203, "Session fixation error - incomplete track in session" },
	{ 0x057204, "Empty or partially written reserved track" },
	{ 0x057205, "No more RZONE reservations are allowed" },
	{ 0x05bf00, "Loss of streaming" },
	{ 0x062800, "Not ready to ready transition, medium may have changed" },
	{ 0x062900, "Power on, reset or hardware reset occurred" },
	{ 0x062a00, "Parameters changed" },
	{ 0x062a01, "Mode parameters changed" },
	{ 0x062e00, "Insufficient time for operation" },
	{ 0x063f00, "Logical unit operating conditions have changed" },
	{ 0x063f01, "Microcode has been changed" },
	{ 0x065a00, "Operator request or state change input (unspecified)" },
	{ 0x065a01, "Operator medium removal request" },
	{ 0x0bb900, "Play operation aborted" },
	/* Here we use 0xff for the key (not a valid key) to signify
	 * that these can have _any_ key value associated with them... */
	{ 0xff0401, "Logical unit is in process of becoming ready" },
	{ 0xff0400, "Logical unit not ready, cause not reportable" },
	{ 0xff0402, "Logical unit not ready, initializing command required" },
	{ 0xff0403, "Logical unit not ready, manual intervention required" },
	{ 0xff0500, "Logical unit does not respond to selection" },
	{ 0xff0800, "Logical unit communication failure" },
	{ 0xff0802, "Logical unit communication parity error" },
	{ 0xff0801, "Logical unit communication time-out" },
	{ 0xff2500, "Logical unit not supported" },
	{ 0xff4c00, "Logical unit failed self-configuration" },
	{ 0xff3e00, "Logical unit has not self-configured yet" },
};

void ide_cd_log_error(const char *name, struct request *failed_command,
		      struct request_sense *sense)
{
	int i;
	const char *s = "bad sense key!";
	char buf[80];

	printk(KERN_ERR "ATAPI device %s:\n", name);
	if (sense->error_code == 0x70)
		printk(KERN_CONT "  Error: ");
	else if (sense->error_code == 0x71)
		printk("  Deferred Error: ");
	else if (sense->error_code == 0x7f)
		printk(KERN_CONT "  Vendor-specific Error: ");
	else
		printk(KERN_CONT "  Unknown Error Type: ");

	if (sense->sense_key < ARRAY_SIZE(sense_key_texts))
		s = sense_key_texts[sense->sense_key];

	printk(KERN_CONT "%s -- (Sense key=0x%02x)\n", s, sense->sense_key);

	if (sense->asc == 0x40) {
		sprintf(buf, "Diagnostic failure on component 0x%02x",
			sense->ascq);
		s = buf;
	} else {
		int lo = 0, mid, hi = ARRAY_SIZE(sense_data_texts);
		unsigned long key = (sense->sense_key << 16);

		key |= (sense->asc << 8);
		if (!(sense->ascq >= 0x80 && sense->ascq <= 0xdd))
			key |= sense->ascq;
		s = NULL;

		while (hi > lo) {
			mid = (lo + hi) / 2;
			if (sense_data_texts[mid].asc_ascq == key ||
			    sense_data_texts[mid].asc_ascq == (0xff0000|key)) {
				s = sense_data_texts[mid].text;
				break;
			} else if (sense_data_texts[mid].asc_ascq > key)
				hi = mid;
			else
				lo = mid + 1;
		}
	}

	if (s == NULL) {
		if (sense->asc > 0x80)
			s = "(vendor-specific error)";
		else
			s = "(reserved error code)";
	}

	printk(KERN_ERR "  %s -- (asc=0x%02x, ascq=0x%02x)\n",
			s, sense->asc, sense->ascq);

	if (failed_command != NULL) {
		int lo = 0, mid, hi = ARRAY_SIZE(packet_command_texts);
		s = NULL;

		while (hi > lo) {
			mid = (lo + hi) / 2;
			if (packet_command_texts[mid].packet_command ==
			    failed_command->cmd[0]) {
				s = packet_command_texts[mid].text;
				break;
			}
			if (packet_command_texts[mid].packet_command >
			    failed_command->cmd[0])
				hi = mid;
			else
				lo = mid + 1;
		}

		printk(KERN_ERR "  The failed \"%s\" packet command "
				"was: \n  \"", s);
		for (i = 0; i < BLK_MAX_CDB; i++)
			printk(KERN_CONT "%02x ", failed_command->cmd[i]);
		printk(KERN_CONT "\"\n");
	}

	/* The SKSV bit specifies validity of the sense_key_specific
	 * in the next two commands. It is bit 7 of the first byte.
	 * In the case of NOT_READY, if SKSV is set the drive can
	 * give us nice ETA readings.
	 */
	if (sense->sense_key == NOT_READY && (sense->sks[0] & 0x80)) {
		int progress = (sense->sks[1] << 8 | sense->sks[2]) * 100;

		printk(KERN_ERR "  Command is %02d%% complete\n",
				progress / 0xffff);
	}

	if (sense->sense_key == ILLEGAL_REQUEST &&
	    (sense->sks[0] & 0x80) != 0) {
		printk(KERN_ERR "  Error in %s byte %d",
				(sense->sks[0] & 0x40) != 0 ?
				"command packet" : "command data",
				(sense->sks[1] << 8) + sense->sks[2]);

		if ((sense->sks[0] & 0x40) != 0)
			printk(KERN_CONT " bit %d", sense->sks[0] & 0x07);

		printk(KERN_CONT "\n");
	}
}
#endif
