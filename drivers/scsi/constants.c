/* 
 * ASCII values for a number of symbolic constants, printing functions,
 * etc.
 * Additions for SCSI 2 and Linux 2.2.x by D. Gilbert (990422)
 * Additions for SCSI 3+ (SPC-3 T10/1416-D Rev 07 3 May 2002)
 *   by D. Gilbert and aeb (20020609)
 * Additions for SPC-3 T10/1416-D Rev 21 22 Sept 2004, D. Gilbert 20041025
 * Update to SPC-4 T10/1713-D Rev 5a, 14 June 2006, D. Gilbert 20060702
 */

#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_dbg.h>



/* Commands with service actions that change the command name */
#define MAINTENANCE_IN 0xa3
#define MAINTENANCE_OUT 0xa4
#define SERVICE_ACTION_IN_12 0xab
#define SERVICE_ACTION_OUT_12 0xa9
#define SERVICE_ACTION_IN_16 0x9e
#define SERVICE_ACTION_OUT_16 0x9f
#define VARIABLE_LENGTH_CMD 0x7f



#ifdef CONFIG_SCSI_CONSTANTS
static const char * cdb_byte0_names[] = {
/* 00-03 */ "Test Unit Ready", "Rezero Unit/Rewind", NULL, "Request Sense",
/* 04-07 */ "Format Unit/Medium", "Read Block Limits", NULL,
	    "Reasssign Blocks",
/* 08-0d */ "Read(6)", NULL, "Write(6)", "Seek(6)", NULL, NULL,
/* 0e-12 */ NULL, "Read Reverse", "Write Filemarks", "Space", "Inquiry",  
/* 13-16 */ "Verify(6)", "Recover Buffered Data", "Mode Select(6)",
	    "Reserve(6)",
/* 17-1a */ "Release(6)", "Copy", "Erase", "Mode Sense(6)",
/* 1b-1d */ "Start/Stop Unit", "Receive Diagnostic", "Send Diagnostic", 
/* 1e-1f */ "Prevent/Allow Medium Removal", NULL,
/* 20-22 */  NULL, NULL, NULL,
/* 23-28 */ "Read Format Capacities", "Set Window",
	    "Read Capacity(10)", NULL, NULL, "Read(10)",
/* 29-2d */ "Read Generation", "Write(10)", "Seek(10)", "Erase(10)",
            "Read updated block",
/* 2e-31 */ "Write Verify(10)", "Verify(10)", "Search High", "Search Equal",
/* 32-34 */ "Search Low", "Set Limits", "Prefetch/Read Position", 
/* 35-37 */ "Synchronize Cache(10)", "Lock/Unlock Cache(10)",
	    "Read Defect Data(10)", 
/* 38-3c */ "Medium Scan", "Compare", "Copy Verify", "Write Buffer", 
            "Read Buffer", 
/* 3d-3f */ "Update Block", "Read Long(10)",  "Write Long(10)",
/* 40-41 */ "Change Definition", "Write Same(10)",
/* 42-48 */ "Read sub-channel", "Read TOC/PMA/ATIP", "Read density support",
            "Play audio(10)", "Get configuration", "Play audio msf",
            "Play audio track/index",
/* 49-4f */ "Play track relative(10)", "Get event status notification",
            "Pause/resume", "Log Select", "Log Sense", "Stop play/scan",
            NULL,
/* 50-55 */ "Xdwrite", "Xpwrite, Read disk info", "Xdread, Read track info",
            "Reserve track", "Send OPC info", "Mode Select(10)",
/* 56-5b */ "Reserve(10)", "Release(10)", "Repair track", "Read master cue",
            "Mode Sense(10)", "Close track/session",
/* 5c-5f */ "Read buffer capacity", "Send cue sheet", "Persistent reserve in",
            "Persistent reserve out",
/* 60-67 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 68-6f */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 70-77 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 78-7f */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, "Variable length",
/* 80-84 */ "Xdwrite(16)", "Rebuild(16)", "Regenerate(16)", "Extended copy",
            "Receive copy results",
/* 85-89 */ "ATA command pass through(16)", "Access control in",
	    "Access control out", "Read(16)", "Memory Export Out(16)",
/* 8a-8f */ "Write(16)", NULL, "Read attributes", "Write attributes",
            "Write and verify(16)", "Verify(16)",
/* 90-94 */ "Pre-fetch(16)", "Synchronize cache(16)",
            "Lock/unlock cache(16)", "Write same(16)", NULL,
/* 95-99 */ NULL, NULL, NULL, NULL, NULL,
/* 9a-9f */ NULL, NULL, NULL, NULL, "Service action in(16)",
            "Service action out(16)",
/* a0-a5 */ "Report luns", "ATA command pass through(12)/Blank",
            "Security protocol in", "Maintenance in", "Maintenance out",
	    "Move medium/play audio(12)",
/* a6-a9 */ "Exchange medium", "Move medium attached", "Read(12)",
            "Play track relative(12)",
/* aa-ae */ "Write(12)", NULL, "Erase(12), Get Performance",
            "Read DVD structure", "Write and verify(12)",
/* af-b1 */ "Verify(12)", "Search data high(12)", "Search data equal(12)",
/* b2-b4 */ "Search data low(12)", "Set limits(12)",
            "Read element status attached",
/* b5-b6 */ "Security protocol out", "Send volume tag, set streaming",
/* b7-b9 */ "Read defect data(12)", "Read element status", "Read CD msf",
/* ba-bc */ "Redundancy group (in), Scan",
            "Redundancy group (out), Set cd-rom speed", "Spare (in), Play cd",
/* bd-bf */ "Spare (out), Mechanism status", "Volume set (in), Read cd",
            "Volume set (out), Send DVD structure",
};

struct value_name_pair {
	int value;
	const char * name;
};

static const struct value_name_pair maint_in_arr[] = {
	{0x5, "Report device identifier"},
	{0xa, "Report target port groups"},
	{0xb, "Report aliases"},
	{0xc, "Report supported operation codes"},
	{0xd, "Report supported task management functions"},
	{0xe, "Report priority"},
	{0xf, "Report timestamp"},
};
#define MAINT_IN_SZ ARRAY_SIZE(maint_in_arr)

static const struct value_name_pair maint_out_arr[] = {
	{0x6, "Set device identifier"},
	{0xa, "Set target port groups"},
	{0xb, "Change aliases"},
	{0xe, "Set priority"},
	{0xe, "Set timestamp"},
};
#define MAINT_OUT_SZ ARRAY_SIZE(maint_out_arr)

static const struct value_name_pair serv_in12_arr[] = {
	{0x1, "Read media serial number"},
};
#define SERV_IN12_SZ ARRAY_SIZE(serv_in12_arr)

static const struct value_name_pair serv_out12_arr[] = {
	{-1, "dummy entry"},
};
#define SERV_OUT12_SZ ARRAY_SIZE(serv_out12_arr)

static const struct value_name_pair serv_in16_arr[] = {
	{0x10, "Read capacity(16)"},
	{0x11, "Read long(16)"},
};
#define SERV_IN16_SZ ARRAY_SIZE(serv_in16_arr)

static const struct value_name_pair serv_out16_arr[] = {
	{0x11, "Write long(16)"},
	{0x1f, "Notify data transfer device(16)"},
};
#define SERV_OUT16_SZ ARRAY_SIZE(serv_out16_arr)

static const struct value_name_pair variable_length_arr[] = {
	{0x1, "Rebuild(32)"},
	{0x2, "Regenerate(32)"},
	{0x3, "Xdread(32)"},
	{0x4, "Xdwrite(32)"},
	{0x5, "Xdwrite extended(32)"},
	{0x6, "Xpwrite(32)"},
	{0x7, "Xdwriteread(32)"},
	{0x8, "Xdwrite extended(64)"},
	{0x9, "Read(32)"},
	{0xa, "Verify(32)"},
	{0xb, "Write(32)"},
	{0xc, "Write an verify(32)"},
	{0xd, "Write same(32)"},
	{0x8801, "Format OSD"},
	{0x8802, "Create (osd)"},
	{0x8803, "List (osd)"},
	{0x8805, "Read (osd)"},
	{0x8806, "Write (osd)"},
	{0x8807, "Append (osd)"},
	{0x8808, "Flush (osd)"},
	{0x880a, "Remove (osd)"},
	{0x880b, "Create partition (osd)"},
	{0x880c, "Remove partition (osd)"},
	{0x880e, "Get attributes (osd)"},
	{0x880f, "Set attributes (osd)"},
	{0x8812, "Create and write (osd)"},
	{0x8815, "Create collection (osd)"},
	{0x8816, "Remove collection (osd)"},
	{0x8817, "List collection (osd)"},
	{0x8818, "Set key (osd)"},
	{0x8819, "Set master key (osd)"},
	{0x881a, "Flush collection (osd)"},
	{0x881b, "Flush partition (osd)"},
	{0x881c, "Flush OSD"},
	{0x8f7e, "Perform SCSI command (osd)"},
	{0x8f7f, "Perform task management function (osd)"},
};
#define VARIABLE_LENGTH_SZ ARRAY_SIZE(variable_length_arr)

static const char * get_sa_name(const struct value_name_pair * arr,
			        int arr_sz, int service_action)
{
	int k;

	for (k = 0; k < arr_sz; ++k, ++arr) {
		if (service_action == arr->value)
			break;
	}
	return (k < arr_sz) ? arr->name : NULL;
}

/* attempt to guess cdb length if cdb_len==0 . No trailing linefeed. */
static void print_opcode_name(unsigned char * cdbp, int cdb_len,
			      int start_of_line)
{
	int sa, len, cdb0;
	const char * name;
	const char * leadin = start_of_line ? KERN_INFO : "";

	cdb0 = cdbp[0];
	switch(cdb0) {
	case VARIABLE_LENGTH_CMD:
		len = cdbp[7] + 8;
		if (len < 10) {
			printk("%sshort variable length command, "
			       "len=%d ext_len=%d", leadin, len, cdb_len);
			break;
		}
		sa = (cdbp[8] << 8) + cdbp[9];
		name = get_sa_name(maint_in_arr, MAINT_IN_SZ, sa);
		if (name) {
			printk("%s%s", leadin, name);
			if ((cdb_len > 0) && (len != cdb_len))
				printk(", in_cdb_len=%d, ext_len=%d",
				       len, cdb_len);
		} else {
			printk("%scdb[0]=0x%x, sa=0x%x", leadin, cdb0, sa);
			if ((cdb_len > 0) && (len != cdb_len))
				printk(", in_cdb_len=%d, ext_len=%d",
				       len, cdb_len);
		}
		break;
	case MAINTENANCE_IN:
		sa = cdbp[1] & 0x1f;
		name = get_sa_name(maint_in_arr, MAINT_IN_SZ, sa);
		if (name)
			printk("%s%s", leadin, name);
		else
			printk("%scdb[0]=0x%x, sa=0x%x", leadin, cdb0, sa);
		break;
	case MAINTENANCE_OUT:
		sa = cdbp[1] & 0x1f;
		name = get_sa_name(maint_out_arr, MAINT_OUT_SZ, sa);
		if (name)
			printk("%s%s", leadin, name);
		else
			printk("%scdb[0]=0x%x, sa=0x%x", leadin, cdb0, sa);
		break;
	case SERVICE_ACTION_IN_12:
		sa = cdbp[1] & 0x1f;
		name = get_sa_name(serv_in12_arr, SERV_IN12_SZ, sa);
		if (name)
			printk("%s%s", leadin, name);
		else
			printk("%scdb[0]=0x%x, sa=0x%x", leadin, cdb0, sa);
		break;
	case SERVICE_ACTION_OUT_12:
		sa = cdbp[1] & 0x1f;
		name = get_sa_name(serv_out12_arr, SERV_OUT12_SZ, sa);
		if (name)
			printk("%s%s", leadin, name);
		else
			printk("%scdb[0]=0x%x, sa=0x%x", leadin, cdb0, sa);
		break;
	case SERVICE_ACTION_IN_16:
		sa = cdbp[1] & 0x1f;
		name = get_sa_name(serv_in16_arr, SERV_IN16_SZ, sa);
		if (name)
			printk("%s%s", leadin, name);
		else
			printk("%scdb[0]=0x%x, sa=0x%x", leadin, cdb0, sa);
		break;
	case SERVICE_ACTION_OUT_16:
		sa = cdbp[1] & 0x1f;
		name = get_sa_name(serv_out16_arr, SERV_OUT16_SZ, sa);
		if (name)
			printk("%s%s", leadin, name);
		else
			printk("%scdb[0]=0x%x, sa=0x%x", leadin, cdb0, sa);
		break;
	default:
		if (cdb0 < 0xc0) {
			name = cdb_byte0_names[cdb0];
			if (name)
				printk("%s%s", leadin, name);
			else
				printk("%scdb[0]=0x%x (reserved)",
				       leadin, cdb0);
		} else
			printk("%scdb[0]=0x%x (vendor)", leadin, cdb0);
		break;
	}
}

#else /* ifndef CONFIG_SCSI_CONSTANTS */

static void print_opcode_name(unsigned char * cdbp, int cdb_len,
			      int start_of_line)
{
	int sa, len, cdb0;
	const char * leadin = start_of_line ? KERN_INFO : "";

	cdb0 = cdbp[0];
	switch(cdb0) {
	case VARIABLE_LENGTH_CMD:
		len = cdbp[7] + 8;
		if (len < 10) {
			printk("%sshort opcode=0x%x command, len=%d "
			       "ext_len=%d", leadin, cdb0, len, cdb_len);
			break;
		}
		sa = (cdbp[8] << 8) + cdbp[9];
		printk("%scdb[0]=0x%x, sa=0x%x", leadin, cdb0, sa);
		if (len != cdb_len)
			printk(", in_cdb_len=%d, ext_len=%d", len, cdb_len);
		break;
	case MAINTENANCE_IN:
	case MAINTENANCE_OUT:
	case SERVICE_ACTION_IN_12:
	case SERVICE_ACTION_OUT_12:
	case SERVICE_ACTION_IN_16:
	case SERVICE_ACTION_OUT_16:
		sa = cdbp[1] & 0x1f;
		printk("%scdb[0]=0x%x, sa=0x%x", leadin, cdb0, sa);
		break;
	default:
		if (cdb0 < 0xc0)
			printk("%scdb[0]=0x%x", leadin, cdb0);
		else
			printk("%scdb[0]=0x%x (vendor)", leadin, cdb0);
		break;
	}
}
#endif  

void __scsi_print_command(unsigned char *command)
{
	int k, len;

	print_opcode_name(command, 0, 1);
	if (VARIABLE_LENGTH_CMD == command[0])
		len = command[7] + 8;
	else
		len = COMMAND_SIZE(command[0]);
	/* print out all bytes in cdb */
	for (k = 0; k < len; ++k) 
		printk(" %02x", command[k]);
	printk("\n");
}
EXPORT_SYMBOL(__scsi_print_command);

/* This function (perhaps with the addition of peripheral device type)
 * is more approriate than __scsi_print_command(). Perhaps that static
 * can be dropped later if it replaces the __scsi_print_command version.
 */
static void scsi_print_cdb(unsigned char *cdb, int cdb_len, int start_of_line)
{
	int k;

	print_opcode_name(cdb, cdb_len, start_of_line);
	/* print out all bytes in cdb */
	printk(":");
	for (k = 0; k < cdb_len; ++k) 
		printk(" %02x", cdb[k]);
	printk("\n");
}

/**
 *
 *	scsi_print_status - print scsi status description
 *	@scsi_status: scsi status value
 *
 *	If the status is recognized, the description is printed.
 *	Otherwise "Unknown status" is output. No trailing space.
 *	If CONFIG_SCSI_CONSTANTS is not set, then print status in hex
 *	(e.g. "0x2" for Check Condition).
 **/
void
scsi_print_status(unsigned char scsi_status) {
#ifdef CONFIG_SCSI_CONSTANTS
	const char * ccp;

	switch (scsi_status) {
	case 0:    ccp = "Good"; break;
	case 0x2:  ccp = "Check Condition"; break;
	case 0x4:  ccp = "Condition Met"; break;
	case 0x8:  ccp = "Busy"; break;
	case 0x10: ccp = "Intermediate"; break;
	case 0x14: ccp = "Intermediate-Condition Met"; break;
	case 0x18: ccp = "Reservation Conflict"; break;
	case 0x22: ccp = "Command Terminated"; break;	/* obsolete */
	case 0x28: ccp = "Task set Full"; break;	/* was: Queue Full */
	case 0x30: ccp = "ACA Active"; break;
	case 0x40: ccp = "Task Aborted"; break;
	default:   ccp = "Unknown status";
	}
	printk(KERN_INFO "%s", ccp);
#else
	printk(KERN_INFO "0x%0x", scsi_status);
#endif
}
EXPORT_SYMBOL(scsi_print_status);

#ifdef CONFIG_SCSI_CONSTANTS

struct error_info {
	unsigned short code12;	/* 0x0302 looks better than 0x03,0x02 */
	const char * text;
};

static struct error_info additional[] =
{
	{0x0000, "No additional sense information"},
	{0x0001, "Filemark detected"},
	{0x0002, "End-of-partition/medium detected"},
	{0x0003, "Setmark detected"},
	{0x0004, "Beginning-of-partition/medium detected"},
	{0x0005, "End-of-data detected"},
	{0x0006, "I/O process terminated"},
	{0x0011, "Audio play operation in progress"},
	{0x0012, "Audio play operation paused"},
	{0x0013, "Audio play operation successfully completed"},
	{0x0014, "Audio play operation stopped due to error"},
	{0x0015, "No current audio status to return"},
	{0x0016, "Operation in progress"},
	{0x0017, "Cleaning requested"},
	{0x0018, "Erase operation in progress"},
	{0x0019, "Locate operation in progress"},
	{0x001A, "Rewind operation in progress"},
	{0x001B, "Set capacity operation in progress"},
	{0x001C, "Verify operation in progress"},
	{0x001D, "ATA pass through information available"},

	{0x0100, "No index/sector signal"},

	{0x0200, "No seek complete"},

	{0x0300, "Peripheral device write fault"},
	{0x0301, "No write current"},
	{0x0302, "Excessive write errors"},

	{0x0400, "Logical unit not ready, cause not reportable"},
	{0x0401, "Logical unit is in process of becoming ready"},
	{0x0402, "Logical unit not ready, initializing command required"},
	{0x0403, "Logical unit not ready, manual intervention required"},
	{0x0404, "Logical unit not ready, format in progress"},
	{0x0405, "Logical unit not ready, rebuild in progress"},
	{0x0406, "Logical unit not ready, recalculation in progress"},
	{0x0407, "Logical unit not ready, operation in progress"},
	{0x0408, "Logical unit not ready, long write in progress"},
	{0x0409, "Logical unit not ready, self-test in progress"},
	{0x040A, "Logical unit not accessible, asymmetric access state "
	 "transition"},
	{0x040B, "Logical unit not accessible, target port in standby state"},
	{0x040C, "Logical unit not accessible, target port in unavailable "
	 "state"},
	{0x0410, "Logical unit not ready, auxiliary memory not accessible"},
	{0x0411, "Logical unit not ready, notify (enable spinup) required"},
	{0x0412, "Logical unit not ready, offline"},

	{0x0500, "Logical unit does not respond to selection"},

	{0x0600, "No reference position found"},

	{0x0700, "Multiple peripheral devices selected"},

	{0x0800, "Logical unit communication failure"},
	{0x0801, "Logical unit communication time-out"},
	{0x0802, "Logical unit communication parity error"},
	{0x0803, "Logical unit communication CRC error (Ultra-DMA/32)"},
	{0x0804, "Unreachable copy target"},

	{0x0900, "Track following error"},
	{0x0901, "Tracking servo failure"},
	{0x0902, "Focus servo failure"},
	{0x0903, "Spindle servo failure"},
	{0x0904, "Head select fault"},

	{0x0A00, "Error log overflow"},

	{0x0B00, "Warning"},
	{0x0B01, "Warning - specified temperature exceeded"},
	{0x0B02, "Warning - enclosure degraded"},
	{0x0B03, "Warning - background self-test failed"},
	{0x0B04, "Warning - background pre-scan detected medium error"},
	{0x0B05, "Warning - background medium scan detected medium error"},

	{0x0C00, "Write error"},
	{0x0C01, "Write error - recovered with auto reallocation"},
	{0x0C02, "Write error - auto reallocation failed"},
	{0x0C03, "Write error - recommend reassignment"},
	{0x0C04, "Compression check miscompare error"},
	{0x0C05, "Data expansion occurred during compression"},
	{0x0C06, "Block not compressible"},
	{0x0C07, "Write error - recovery needed"},
	{0x0C08, "Write error - recovery failed"},
	{0x0C09, "Write error - loss of streaming"},
	{0x0C0A, "Write error - padding blocks added"},
	{0x0C0B, "Auxiliary memory write error"},
	{0x0C0C, "Write error - unexpected unsolicited data"},
	{0x0C0D, "Write error - not enough unsolicited data"},
	{0x0C0F, "Defects in error window"},

	{0x0D00, "Error detected by third party temporary initiator"},
	{0x0D01, "Third party device failure"},
	{0x0D02, "Copy target device not reachable"},
	{0x0D03, "Incorrect copy target device type"},
	{0x0D04, "Copy target device data underrun"},
	{0x0D05, "Copy target device data overrun"},

	{0x0E00, "Invalid information unit"},
	{0x0E01, "Information unit too short"},
	{0x0E02, "Information unit too long"},
	{0x0E03, "Invalid field in command information unit"},

	{0x1000, "Id CRC or ECC error"},
	{0x1001, "Logical block guard check failed"},
	{0x1002, "Logical block application tag check failed"},
	{0x1003, "Logical block reference tag check failed"},

	{0x1100, "Unrecovered read error"},
	{0x1101, "Read retries exhausted"},
	{0x1102, "Error too long to correct"},
	{0x1103, "Multiple read errors"},
	{0x1104, "Unrecovered read error - auto reallocate failed"},
	{0x1105, "L-EC uncorrectable error"},
	{0x1106, "CIRC unrecovered error"},
	{0x1107, "Data re-synchronization error"},
	{0x1108, "Incomplete block read"},
	{0x1109, "No gap found"},
	{0x110A, "Miscorrected error"},
	{0x110B, "Unrecovered read error - recommend reassignment"},
	{0x110C, "Unrecovered read error - recommend rewrite the data"},
	{0x110D, "De-compression CRC error"},
	{0x110E, "Cannot decompress using declared algorithm"},
	{0x110F, "Error reading UPC/EAN number"},
	{0x1110, "Error reading ISRC number"},
	{0x1111, "Read error - loss of streaming"},
	{0x1112, "Auxiliary memory read error"},
	{0x1113, "Read error - failed retransmission request"},
	{0x1114, "Read error - lba marked bad by application client"},

	{0x1200, "Address mark not found for id field"},

	{0x1300, "Address mark not found for data field"},

	{0x1400, "Recorded entity not found"},
	{0x1401, "Record not found"},
	{0x1402, "Filemark or setmark not found"},
	{0x1403, "End-of-data not found"},
	{0x1404, "Block sequence error"},
	{0x1405, "Record not found - recommend reassignment"},
	{0x1406, "Record not found - data auto-reallocated"},
	{0x1407, "Locate operation failure"},

	{0x1500, "Random positioning error"},
	{0x1501, "Mechanical positioning error"},
	{0x1502, "Positioning error detected by read of medium"},

	{0x1600, "Data synchronization mark error"},
	{0x1601, "Data sync error - data rewritten"},
	{0x1602, "Data sync error - recommend rewrite"},
	{0x1603, "Data sync error - data auto-reallocated"},
	{0x1604, "Data sync error - recommend reassignment"},

	{0x1700, "Recovered data with no error correction applied"},
	{0x1701, "Recovered data with retries"},
	{0x1702, "Recovered data with positive head offset"},
	{0x1703, "Recovered data with negative head offset"},
	{0x1704, "Recovered data with retries and/or circ applied"},
	{0x1705, "Recovered data using previous sector id"},
	{0x1706, "Recovered data without ECC - data auto-reallocated"},
	{0x1707, "Recovered data without ECC - recommend reassignment"},
	{0x1708, "Recovered data without ECC - recommend rewrite"},
	{0x1709, "Recovered data without ECC - data rewritten"},

	{0x1800, "Recovered data with error correction applied"},
	{0x1801, "Recovered data with error corr. & retries applied"},
	{0x1802, "Recovered data - data auto-reallocated"},
	{0x1803, "Recovered data with CIRC"},
	{0x1804, "Recovered data with L-EC"},
	{0x1805, "Recovered data - recommend reassignment"},
	{0x1806, "Recovered data - recommend rewrite"},
	{0x1807, "Recovered data with ECC - data rewritten"},
	{0x1808, "Recovered data with linking"},

	{0x1900, "Defect list error"},
	{0x1901, "Defect list not available"},
	{0x1902, "Defect list error in primary list"},
	{0x1903, "Defect list error in grown list"},

	{0x1A00, "Parameter list length error"},

	{0x1B00, "Synchronous data transfer error"},

	{0x1C00, "Defect list not found"},
	{0x1C01, "Primary defect list not found"},
	{0x1C02, "Grown defect list not found"},

	{0x1D00, "Miscompare during verify operation"},

	{0x1E00, "Recovered id with ECC correction"},

	{0x1F00, "Partial defect list transfer"},

	{0x2000, "Invalid command operation code"},
	{0x2001, "Access denied - initiator pending-enrolled"},
	{0x2002, "Access denied - no access rights"},
	{0x2003, "Access denied - invalid mgmt id key"},
	{0x2004, "Illegal command while in write capable state"},
	{0x2005, "Obsolete"},
	{0x2006, "Illegal command while in explicit address mode"},
	{0x2007, "Illegal command while in implicit address mode"},
	{0x2008, "Access denied - enrollment conflict"},
	{0x2009, "Access denied - invalid LU identifier"},
	{0x200A, "Access denied - invalid proxy token"},
	{0x200B, "Access denied - ACL LUN conflict"},

	{0x2100, "Logical block address out of range"},
	{0x2101, "Invalid element address"},
	{0x2102, "Invalid address for write"},
	{0x2103, "Invalid write crossing layer jump"},

	{0x2200, "Illegal function (use 20 00, 24 00, or 26 00)"},

	{0x2400, "Invalid field in cdb"},
	{0x2401, "CDB decryption error"},
	{0x2402, "Obsolete"},
	{0x2403, "Obsolete"},
	{0x2404, "Security audit value frozen"},
	{0x2405, "Security working key frozen"},
	{0x2406, "Nonce not unique"},
	{0x2407, "Nonce timestamp out of range"},

	{0x2500, "Logical unit not supported"},

	{0x2600, "Invalid field in parameter list"},
	{0x2601, "Parameter not supported"},
	{0x2602, "Parameter value invalid"},
	{0x2603, "Threshold parameters not supported"},
	{0x2604, "Invalid release of persistent reservation"},
	{0x2605, "Data decryption error"},
	{0x2606, "Too many target descriptors"},
	{0x2607, "Unsupported target descriptor type code"},
	{0x2608, "Too many segment descriptors"},
	{0x2609, "Unsupported segment descriptor type code"},
	{0x260A, "Unexpected inexact segment"},
	{0x260B, "Inline data length exceeded"},
	{0x260C, "Invalid operation for copy source or destination"},
	{0x260D, "Copy segment granularity violation"},
	{0x260E, "Invalid parameter while port is enabled"},
	{0x260F, "Invalid data-out buffer integrity check value"},
	{0x2610, "Data decryption key fail limit reached"},
	{0x2611, "Incomplete key-associated data set"},
	{0x2612, "Vendor specific key reference not found"},

	{0x2700, "Write protected"},
	{0x2701, "Hardware write protected"},
	{0x2702, "Logical unit software write protected"},
	{0x2703, "Associated write protect"},
	{0x2704, "Persistent write protect"},
	{0x2705, "Permanent write protect"},
	{0x2706, "Conditional write protect"},

	{0x2800, "Not ready to ready change, medium may have changed"},
	{0x2801, "Import or export element accessed"},
	{0x2802, "Format-layer may have changed"},

	{0x2900, "Power on, reset, or bus device reset occurred"},
	{0x2901, "Power on occurred"},
	{0x2902, "Scsi bus reset occurred"},
	{0x2903, "Bus device reset function occurred"},
	{0x2904, "Device internal reset"},
	{0x2905, "Transceiver mode changed to single-ended"},
	{0x2906, "Transceiver mode changed to lvd"},
	{0x2907, "I_T nexus loss occurred"},

	{0x2A00, "Parameters changed"},
	{0x2A01, "Mode parameters changed"},
	{0x2A02, "Log parameters changed"},
	{0x2A03, "Reservations preempted"},
	{0x2A04, "Reservations released"},
	{0x2A05, "Registrations preempted"},
	{0x2A06, "Asymmetric access state changed"},
	{0x2A07, "Implicit asymmetric access state transition failed"},
	{0x2A08, "Priority changed"},
	{0x2A09, "Capacity data has changed"},
	{0x2A10, "Timestamp changed"},
	{0x2A11, "Data encryption parameters changed by another i_t nexus"},
	{0x2A12, "Data encryption parameters changed by vendor specific "
		 "event"},
	{0x2A13, "Data encryption key instance counter has changed"},

	{0x2B00, "Copy cannot execute since host cannot disconnect"},

	{0x2C00, "Command sequence error"},
	{0x2C01, "Too many windows specified"},
	{0x2C02, "Invalid combination of windows specified"},
	{0x2C03, "Current program area is not empty"},
	{0x2C04, "Current program area is empty"},
	{0x2C05, "Illegal power condition request"},
	{0x2C06, "Persistent prevent conflict"},
	{0x2C07, "Previous busy status"},
	{0x2C08, "Previous task set full status"},
	{0x2C09, "Previous reservation conflict status"},
	{0x2C0A, "Partition or collection contains user objects"},
	{0x2C0B, "Not reserved"},

	{0x2D00, "Overwrite error on update in place"},

	{0x2E00, "Insufficient time for operation"},

	{0x2F00, "Commands cleared by another initiator"},
	{0x2F01, "Commands cleared by power loss notification"},

	{0x3000, "Incompatible medium installed"},
	{0x3001, "Cannot read medium - unknown format"},
	{0x3002, "Cannot read medium - incompatible format"},
	{0x3003, "Cleaning cartridge installed"},
	{0x3004, "Cannot write medium - unknown format"},
	{0x3005, "Cannot write medium - incompatible format"},
	{0x3006, "Cannot format medium - incompatible medium"},
	{0x3007, "Cleaning failure"},
	{0x3008, "Cannot write - application code mismatch"},
	{0x3009, "Current session not fixated for append"},
	{0x300A, "Cleaning request rejected"},
	{0x300C, "WORM medium - overwrite attempted"},
	{0x300D, "WORM medium - integrity check"},
	{0x3010, "Medium not formatted"},

	{0x3100, "Medium format corrupted"},
	{0x3101, "Format command failed"},
	{0x3102, "Zoned formatting failed due to spare linking"},

	{0x3200, "No defect spare location available"},
	{0x3201, "Defect list update failure"},

	{0x3300, "Tape length error"},

	{0x3400, "Enclosure failure"},

	{0x3500, "Enclosure services failure"},
	{0x3501, "Unsupported enclosure function"},
	{0x3502, "Enclosure services unavailable"},
	{0x3503, "Enclosure services transfer failure"},
	{0x3504, "Enclosure services transfer refused"},
	{0x3505, "Enclosure services checksum error"},

	{0x3600, "Ribbon, ink, or toner failure"},

	{0x3700, "Rounded parameter"},

	{0x3800, "Event status notification"},
	{0x3802, "Esn - power management class event"},
	{0x3804, "Esn - media class event"},
	{0x3806, "Esn - device busy class event"},

	{0x3900, "Saving parameters not supported"},

	{0x3A00, "Medium not present"},
	{0x3A01, "Medium not present - tray closed"},
	{0x3A02, "Medium not present - tray open"},
	{0x3A03, "Medium not present - loadable"},
	{0x3A04, "Medium not present - medium auxiliary memory accessible"},

	{0x3B00, "Sequential positioning error"},
	{0x3B01, "Tape position error at beginning-of-medium"},
	{0x3B02, "Tape position error at end-of-medium"},
	{0x3B03, "Tape or electronic vertical forms unit not ready"},
	{0x3B04, "Slew failure"},
	{0x3B05, "Paper jam"},
	{0x3B06, "Failed to sense top-of-form"},
	{0x3B07, "Failed to sense bottom-of-form"},
	{0x3B08, "Reposition error"},
	{0x3B09, "Read past end of medium"},
	{0x3B0A, "Read past beginning of medium"},
	{0x3B0B, "Position past end of medium"},
	{0x3B0C, "Position past beginning of medium"},
	{0x3B0D, "Medium destination element full"},
	{0x3B0E, "Medium source element empty"},
	{0x3B0F, "End of medium reached"},
	{0x3B11, "Medium magazine not accessible"},
	{0x3B12, "Medium magazine removed"},
	{0x3B13, "Medium magazine inserted"},
	{0x3B14, "Medium magazine locked"},
	{0x3B15, "Medium magazine unlocked"},
	{0x3B16, "Mechanical positioning or changer error"},
	{0x3B17, "Read past end of user object"},

	{0x3D00, "Invalid bits in identify message"},

	{0x3E00, "Logical unit has not self-configured yet"},
	{0x3E01, "Logical unit failure"},
	{0x3E02, "Timeout on logical unit"},
	{0x3E03, "Logical unit failed self-test"},
	{0x3E04, "Logical unit unable to update self-test log"},

	{0x3F00, "Target operating conditions have changed"},
	{0x3F01, "Microcode has been changed"},
	{0x3F02, "Changed operating definition"},
	{0x3F03, "Inquiry data has changed"},
	{0x3F04, "Component device attached"},
	{0x3F05, "Device identifier changed"},
	{0x3F06, "Redundancy group created or modified"},
	{0x3F07, "Redundancy group deleted"},
	{0x3F08, "Spare created or modified"},
	{0x3F09, "Spare deleted"},
	{0x3F0A, "Volume set created or modified"},
	{0x3F0B, "Volume set deleted"},
	{0x3F0C, "Volume set deassigned"},
	{0x3F0D, "Volume set reassigned"},
	{0x3F0E, "Reported luns data has changed"},
	{0x3F0F, "Echo buffer overwritten"},
	{0x3F10, "Medium loadable"},
	{0x3F11, "Medium auxiliary memory accessible"},
	{0x3F12, "iSCSI IP address added"},
	{0x3F13, "iSCSI IP address removed"},
	{0x3F14, "iSCSI IP address changed"},
/*
 *	{0x40NN, "Ram failure"},
 *	{0x40NN, "Diagnostic failure on component nn"},
 *	{0x41NN, "Data path failure"},
 *	{0x42NN, "Power-on or self-test failure"},
 */
	{0x4300, "Message error"},

	{0x4400, "Internal target failure"},
	{0x4471, "ATA device failed set features"},

	{0x4500, "Select or reselect failure"},

	{0x4600, "Unsuccessful soft reset"},

	{0x4700, "Scsi parity error"},
	{0x4701, "Data phase CRC error detected"},
	{0x4702, "Scsi parity error detected during st data phase"},
	{0x4703, "Information unit iuCRC error detected"},
	{0x4704, "Asynchronous information protection error detected"},
	{0x4705, "Protocol service CRC error"},
	{0x4706, "Phy test function in progress"},
	{0x477f, "Some commands cleared by iSCSI Protocol event"},

	{0x4800, "Initiator detected error message received"},

	{0x4900, "Invalid message error"},

	{0x4A00, "Command phase error"},

	{0x4B00, "Data phase error"},
	{0x4B01, "Invalid target port transfer tag received"},
	{0x4B02, "Too much write data"},
	{0x4B03, "Ack/nak timeout"},
	{0x4B04, "Nak received"},
	{0x4B05, "Data offset error"},
	{0x4B06, "Initiator response timeout"},

	{0x4C00, "Logical unit failed self-configuration"},
/*
 *	{0x4DNN, "Tagged overlapped commands (nn = queue tag)"},
 */
	{0x4E00, "Overlapped commands attempted"},

	{0x5000, "Write append error"},
	{0x5001, "Write append position error"},
	{0x5002, "Position error related to timing"},

	{0x5100, "Erase failure"},
	{0x5101, "Erase failure - incomplete erase operation detected"},

	{0x5200, "Cartridge fault"},

	{0x5300, "Media load or eject failed"},
	{0x5301, "Unload tape failure"},
	{0x5302, "Medium removal prevented"},
	{0x5303, "Medium removal prevented by data transfer element"},
	{0x5304, "Medium thread or unthread failure"},

	{0x5400, "Scsi to host system interface failure"},

	{0x5500, "System resource failure"},
	{0x5501, "System buffer full"},
	{0x5502, "Insufficient reservation resources"},
	{0x5503, "Insufficient resources"},
	{0x5504, "Insufficient registration resources"},
	{0x5505, "Insufficient access control resources"},
	{0x5506, "Auxiliary memory out of space"},
	{0x5507, "Quota error"},
	{0x5508, "Maximum number of supplemental decryption keys exceeded"},

	{0x5700, "Unable to recover table-of-contents"},

	{0x5800, "Generation does not exist"},

	{0x5900, "Updated block read"},

	{0x5A00, "Operator request or state change input"},
	{0x5A01, "Operator medium removal request"},
	{0x5A02, "Operator selected write protect"},
	{0x5A03, "Operator selected write permit"},

	{0x5B00, "Log exception"},
	{0x5B01, "Threshold condition met"},
	{0x5B02, "Log counter at maximum"},
	{0x5B03, "Log list codes exhausted"},

	{0x5C00, "Rpl status change"},
	{0x5C01, "Spindles synchronized"},
	{0x5C02, "Spindles not synchronized"},

	{0x5D00, "Failure prediction threshold exceeded"},
	{0x5D01, "Media failure prediction threshold exceeded"},
	{0x5D02, "Logical unit failure prediction threshold exceeded"},
	{0x5D03, "Spare area exhaustion prediction threshold exceeded"},
	{0x5D10, "Hardware impending failure general hard drive failure"},
	{0x5D11, "Hardware impending failure drive error rate too high"},
	{0x5D12, "Hardware impending failure data error rate too high"},
	{0x5D13, "Hardware impending failure seek error rate too high"},
	{0x5D14, "Hardware impending failure too many block reassigns"},
	{0x5D15, "Hardware impending failure access times too high"},
	{0x5D16, "Hardware impending failure start unit times too high"},
	{0x5D17, "Hardware impending failure channel parametrics"},
	{0x5D18, "Hardware impending failure controller detected"},
	{0x5D19, "Hardware impending failure throughput performance"},
	{0x5D1A, "Hardware impending failure seek time performance"},
	{0x5D1B, "Hardware impending failure spin-up retry count"},
	{0x5D1C, "Hardware impending failure drive calibration retry count"},
	{0x5D20, "Controller impending failure general hard drive failure"},
	{0x5D21, "Controller impending failure drive error rate too high"},
	{0x5D22, "Controller impending failure data error rate too high"},
	{0x5D23, "Controller impending failure seek error rate too high"},
	{0x5D24, "Controller impending failure too many block reassigns"},
	{0x5D25, "Controller impending failure access times too high"},
	{0x5D26, "Controller impending failure start unit times too high"},
	{0x5D27, "Controller impending failure channel parametrics"},
	{0x5D28, "Controller impending failure controller detected"},
	{0x5D29, "Controller impending failure throughput performance"},
	{0x5D2A, "Controller impending failure seek time performance"},
	{0x5D2B, "Controller impending failure spin-up retry count"},
	{0x5D2C, "Controller impending failure drive calibration retry count"},
	{0x5D30, "Data channel impending failure general hard drive failure"},
	{0x5D31, "Data channel impending failure drive error rate too high"},
	{0x5D32, "Data channel impending failure data error rate too high"},
	{0x5D33, "Data channel impending failure seek error rate too high"},
	{0x5D34, "Data channel impending failure too many block reassigns"},
	{0x5D35, "Data channel impending failure access times too high"},
	{0x5D36, "Data channel impending failure start unit times too high"},
	{0x5D37, "Data channel impending failure channel parametrics"},
	{0x5D38, "Data channel impending failure controller detected"},
	{0x5D39, "Data channel impending failure throughput performance"},
	{0x5D3A, "Data channel impending failure seek time performance"},
	{0x5D3B, "Data channel impending failure spin-up retry count"},
	{0x5D3C, "Data channel impending failure drive calibration retry "
	 "count"},
	{0x5D40, "Servo impending failure general hard drive failure"},
	{0x5D41, "Servo impending failure drive error rate too high"},
	{0x5D42, "Servo impending failure data error rate too high"},
	{0x5D43, "Servo impending failure seek error rate too high"},
	{0x5D44, "Servo impending failure too many block reassigns"},
	{0x5D45, "Servo impending failure access times too high"},
	{0x5D46, "Servo impending failure start unit times too high"},
	{0x5D47, "Servo impending failure channel parametrics"},
	{0x5D48, "Servo impending failure controller detected"},
	{0x5D49, "Servo impending failure throughput performance"},
	{0x5D4A, "Servo impending failure seek time performance"},
	{0x5D4B, "Servo impending failure spin-up retry count"},
	{0x5D4C, "Servo impending failure drive calibration retry count"},
	{0x5D50, "Spindle impending failure general hard drive failure"},
	{0x5D51, "Spindle impending failure drive error rate too high"},
	{0x5D52, "Spindle impending failure data error rate too high"},
	{0x5D53, "Spindle impending failure seek error rate too high"},
	{0x5D54, "Spindle impending failure too many block reassigns"},
	{0x5D55, "Spindle impending failure access times too high"},
	{0x5D56, "Spindle impending failure start unit times too high"},
	{0x5D57, "Spindle impending failure channel parametrics"},
	{0x5D58, "Spindle impending failure controller detected"},
	{0x5D59, "Spindle impending failure throughput performance"},
	{0x5D5A, "Spindle impending failure seek time performance"},
	{0x5D5B, "Spindle impending failure spin-up retry count"},
	{0x5D5C, "Spindle impending failure drive calibration retry count"},
	{0x5D60, "Firmware impending failure general hard drive failure"},
	{0x5D61, "Firmware impending failure drive error rate too high"},
	{0x5D62, "Firmware impending failure data error rate too high"},
	{0x5D63, "Firmware impending failure seek error rate too high"},
	{0x5D64, "Firmware impending failure too many block reassigns"},
	{0x5D65, "Firmware impending failure access times too high"},
	{0x5D66, "Firmware impending failure start unit times too high"},
	{0x5D67, "Firmware impending failure channel parametrics"},
	{0x5D68, "Firmware impending failure controller detected"},
	{0x5D69, "Firmware impending failure throughput performance"},
	{0x5D6A, "Firmware impending failure seek time performance"},
	{0x5D6B, "Firmware impending failure spin-up retry count"},
	{0x5D6C, "Firmware impending failure drive calibration retry count"},
	{0x5DFF, "Failure prediction threshold exceeded (false)"},

	{0x5E00, "Low power condition on"},
	{0x5E01, "Idle condition activated by timer"},
	{0x5E02, "Standby condition activated by timer"},
	{0x5E03, "Idle condition activated by command"},
	{0x5E04, "Standby condition activated by command"},
	{0x5E41, "Power state change to active"},
	{0x5E42, "Power state change to idle"},
	{0x5E43, "Power state change to standby"},
	{0x5E45, "Power state change to sleep"},
	{0x5E47, "Power state change to device control"},

	{0x6000, "Lamp failure"},

	{0x6100, "Video acquisition error"},
	{0x6101, "Unable to acquire video"},
	{0x6102, "Out of focus"},

	{0x6200, "Scan head positioning error"},

	{0x6300, "End of user area encountered on this track"},
	{0x6301, "Packet does not fit in available space"},

	{0x6400, "Illegal mode for this track"},
	{0x6401, "Invalid packet size"},

	{0x6500, "Voltage fault"},

	{0x6600, "Automatic document feeder cover up"},
	{0x6601, "Automatic document feeder lift up"},
	{0x6602, "Document jam in automatic document feeder"},
	{0x6603, "Document miss feed automatic in document feeder"},

	{0x6700, "Configuration failure"},
	{0x6701, "Configuration of incapable logical units failed"},
	{0x6702, "Add logical unit failed"},
	{0x6703, "Modification of logical unit failed"},
	{0x6704, "Exchange of logical unit failed"},
	{0x6705, "Remove of logical unit failed"},
	{0x6706, "Attachment of logical unit failed"},
	{0x6707, "Creation of logical unit failed"},
	{0x6708, "Assign failure occurred"},
	{0x6709, "Multiply assigned logical unit"},
	{0x670A, "Set target port groups command failed"},
	{0x670B, "ATA device feature not enabled"},

	{0x6800, "Logical unit not configured"},

	{0x6900, "Data loss on logical unit"},
	{0x6901, "Multiple logical unit failures"},
	{0x6902, "Parity/data mismatch"},

	{0x6A00, "Informational, refer to log"},

	{0x6B00, "State change has occurred"},
	{0x6B01, "Redundancy level got better"},
	{0x6B02, "Redundancy level got worse"},

	{0x6C00, "Rebuild failure occurred"},

	{0x6D00, "Recalculate failure occurred"},

	{0x6E00, "Command to logical unit failed"},

	{0x6F00, "Copy protection key exchange failure - authentication "
	 "failure"},
	{0x6F01, "Copy protection key exchange failure - key not present"},
	{0x6F02, "Copy protection key exchange failure - key not established"},
	{0x6F03, "Read of scrambled sector without authentication"},
	{0x6F04, "Media region code is mismatched to logical unit region"},
	{0x6F05, "Drive region must be permanent/region reset count error"},
	{0x6F06, "Insufficient block count for binding nonce recording"},
	{0x6F07, "Conflict in binding nonce recording"},
/*
 *	{0x70NN, "Decompression exception short algorithm id of nn"},
 */
	{0x7100, "Decompression exception long algorithm id"},

	{0x7200, "Session fixation error"},
	{0x7201, "Session fixation error writing lead-in"},
	{0x7202, "Session fixation error writing lead-out"},
	{0x7203, "Session fixation error - incomplete track in session"},
	{0x7204, "Empty or partially written reserved track"},
	{0x7205, "No more track reservations allowed"},
	{0x7206, "RMZ extension is not allowed"},
	{0x7207, "No more test zone extensions are allowed"},

	{0x7300, "Cd control error"},
	{0x7301, "Power calibration area almost full"},
	{0x7302, "Power calibration area is full"},
	{0x7303, "Power calibration area error"},
	{0x7304, "Program memory area update failure"},
	{0x7305, "Program memory area is full"},
	{0x7306, "RMA/PMA is almost full"},
	{0x7310, "Current power calibration area almost full"},
	{0x7311, "Current power calibration area is full"},
	{0x7317, "RDZ is full"},

	{0x7400, "Security error"},
	{0x7401, "Unable to decrypt data"},
	{0x7402, "Unencrypted data encountered while decrypting"},
	{0x7403, "Incorrect data encryption key"},
	{0x7404, "Cryptographic integrity validation failed"},
	{0x7405, "Error decrypting data"},
	{0x7471, "Logical unit access not authorized"},

	{0, NULL}
};

struct error_info2 {
	unsigned char code1, code2_min, code2_max;
	const char * fmt;
};

static const struct error_info2 additional2[] =
{
	{0x40,0x00,0x7f,"Ram failure (%x)"},
	{0x40,0x80,0xff,"Diagnostic failure on component (%x)"},
	{0x41,0x00,0xff,"Data path failure (%x)"},
	{0x42,0x00,0xff,"Power-on or self-test failure (%x)"},
	{0x4D,0x00,0xff,"Tagged overlapped commands (queue tag %x)"},
	{0x70,0x00,0xff,"Decompression exception short algorithm id of %x"},
	{0, 0, 0, NULL}
};

/* description of the sense key values */
static const char * const snstext[] = {
	"No Sense",	    /* 0: There is no sense information */
	"Recovered Error",  /* 1: The last command completed successfully
				  but used error correction */
	"Not Ready",	    /* 2: The addressed target is not ready */
	"Medium Error",	    /* 3: Data error detected on the medium */
	"Hardware Error",   /* 4: Controller or device failure */
	"Illegal Request",  /* 5: Error in request */
	"Unit Attention",   /* 6: Removable medium was changed, or
				  the target has been reset, or ... */
	"Data Protect",	    /* 7: Access to the data is blocked */
	"Blank Check",	    /* 8: Reached unexpected written or unwritten
				  region of the medium */
	"Vendor Specific(9)",
	"Copy Aborted",	    /* A: COPY or COMPARE was aborted */
	"Aborted Command",  /* B: The target aborted the command */
	"Equal",	    /* C: A SEARCH DATA command found data equal */
	"Volume Overflow",  /* D: Medium full with still data to be written */
	"Miscompare",	    /* E: Source data and data on the medium
				  do not agree */
};
#endif

/* Get sense key string or NULL if not available */
const char *
scsi_sense_key_string(unsigned char key) {
#ifdef CONFIG_SCSI_CONSTANTS
	if (key <= 0xE)
		return snstext[key];
#endif
	return NULL;
}
EXPORT_SYMBOL(scsi_sense_key_string);

/*
 * Get additional sense code string or NULL if not available.
 * This string may contain a "%x" and should be printed with ascq as arg.
 */
const char *
scsi_extd_sense_format(unsigned char asc, unsigned char ascq) {
#ifdef CONFIG_SCSI_CONSTANTS
	int i;
	unsigned short code = ((asc << 8) | ascq);

	for (i=0; additional[i].text; i++)
		if (additional[i].code12 == code)
			return additional[i].text;
	for (i=0; additional2[i].fmt; i++)
		if (additional2[i].code1 == asc &&
		    additional2[i].code2_min >= ascq &&
		    additional2[i].code2_max <= ascq)
			return additional2[i].fmt;
#endif
	return NULL;
}
EXPORT_SYMBOL(scsi_extd_sense_format);

/* Print extended sense information; no leadin, no linefeed */
static void
scsi_show_extd_sense(unsigned char asc, unsigned char ascq)
{
	const char *extd_sense_fmt = scsi_extd_sense_format(asc, ascq);

	if (extd_sense_fmt) {
		if (strstr(extd_sense_fmt, "%x")) {
			printk("Additional sense: ");
			printk(extd_sense_fmt, ascq);
		} else
			printk("Additional sense: %s", extd_sense_fmt);
	} else {
		if (asc >= 0x80)
			printk("<<vendor>> ASC=0x%x ASCQ=0x%x", asc, ascq);
		if (ascq >= 0x80)
			printk("ASC=0x%x <<vendor>> ASCQ=0x%x", asc, ascq);
		else
			printk("ASC=0x%x ASCQ=0x%x", asc, ascq);
	}
}

void
scsi_print_sense_hdr(const char *name, struct scsi_sense_hdr *sshdr)
{
	const char *sense_txt;
	/* An example of deferred is when an earlier write to disk cache
	 * succeeded, but now the disk discovers that it cannot write the
	 * data to the magnetic media.
	 */
	const char *error = scsi_sense_is_deferred(sshdr) ? 
		"<<DEFERRED>>" : "Current";
	printk(KERN_INFO "%s: %s", name, error);
	if (sshdr->response_code >= 0x72)
		printk(" [descriptor]");

	sense_txt = scsi_sense_key_string(sshdr->sense_key);
	if (sense_txt)
		printk(": sense key: %s\n", sense_txt);
	else
		printk(": sense key=0x%x\n", sshdr->sense_key);
	printk(KERN_INFO "    ");
	scsi_show_extd_sense(sshdr->asc, sshdr->ascq);
	printk("\n");
}
EXPORT_SYMBOL(scsi_print_sense_hdr);

/* Print sense information */
void
__scsi_print_sense(const char *name, const unsigned char *sense_buffer,
		   int sense_len)
{
	int k, num, res;
	unsigned int info;
	struct scsi_sense_hdr ssh;
    
	res = scsi_normalize_sense(sense_buffer, sense_len, &ssh);
	if (0 == res) {
		/* this may be SCSI-1 sense data */
		num = (sense_len < 32) ? sense_len : 32;
		printk(KERN_INFO "Unrecognized sense data (in hex):");
		for (k = 0; k < num; ++k) {
			if (0 == (k % 16)) {
				printk("\n");
				printk(KERN_INFO "        ");
			}
			printk("%02x ", sense_buffer[k]);
		}
		printk("\n");
		return;
	}
	scsi_print_sense_hdr(name, &ssh);
	if (ssh.response_code < 0x72) {
		/* only decode extras for "fixed" format now */
		char buff[80];
		int blen, fixed_valid;

		fixed_valid = sense_buffer[0] & 0x80;
		info = ((sense_buffer[3] << 24) | (sense_buffer[4] << 16) |
			(sense_buffer[5] << 8) | sense_buffer[6]);
		res = 0;
		memset(buff, 0, sizeof(buff));
		blen = sizeof(buff) - 1;
		if (fixed_valid)
			res += snprintf(buff + res, blen - res,
					"Info fld=0x%x", info);
		if (sense_buffer[2] & 0x80) {
			/* current command has read a filemark */
			if (res > 0)
				res += snprintf(buff + res, blen - res, ", ");
			res += snprintf(buff + res, blen - res, "FMK");
		}
		if (sense_buffer[2] & 0x40) {
			/* end-of-medium condition exists */
			if (res > 0)
				res += snprintf(buff + res, blen - res, ", ");
			res += snprintf(buff + res, blen - res, "EOM");
		}
		if (sense_buffer[2] & 0x20) {
			/* incorrect block length requested */
			if (res > 0)
				res += snprintf(buff + res, blen - res, ", ");
			res += snprintf(buff + res, blen - res, "ILI");
		}
		if (res > 0)
			printk(KERN_INFO "%s\n", buff);
	} else if (ssh.additional_length > 0) {
		/* descriptor format with sense descriptors */
		num = 8 + ssh.additional_length;
		num = (sense_len < num) ? sense_len : num;
		printk(KERN_INFO "Descriptor sense data with sense "
		       "descriptors (in hex):");
		for (k = 0; k < num; ++k) {
			if (0 == (k % 16)) {
				printk("\n");
				printk(KERN_INFO "        ");
			}
			printk("%02x ", sense_buffer[k]);
		}
		printk("\n");
	}
}
EXPORT_SYMBOL(__scsi_print_sense);

void scsi_print_sense(const char *devclass, struct scsi_cmnd *cmd)
{
	const char *name = devclass;

	if (cmd->request->rq_disk)
		name = cmd->request->rq_disk->disk_name;
	__scsi_print_sense(name, cmd->sense_buffer, SCSI_SENSE_BUFFERSIZE);
}
EXPORT_SYMBOL(scsi_print_sense);

void scsi_print_command(struct scsi_cmnd *cmd)
{
	/* Assume appended output (i.e. not at start of line) */
	sdev_printk("", cmd->device, "\n");
	printk(KERN_INFO "        command: ");
	scsi_print_cdb(cmd->cmnd, cmd->cmd_len, 0);
}
EXPORT_SYMBOL(scsi_print_command);

#ifdef CONFIG_SCSI_CONSTANTS

static const char * const hostbyte_table[]={
"DID_OK", "DID_NO_CONNECT", "DID_BUS_BUSY", "DID_TIME_OUT", "DID_BAD_TARGET",
"DID_ABORT", "DID_PARITY", "DID_ERROR", "DID_RESET", "DID_BAD_INTR",
"DID_PASSTHROUGH", "DID_SOFT_ERROR", "DID_IMM_RETRY"};
#define NUM_HOSTBYTE_STRS ARRAY_SIZE(hostbyte_table)

void scsi_print_hostbyte(int scsiresult)
{
	int hb = host_byte(scsiresult);

	printk("Hostbyte=0x%02x", hb);
	if (hb < NUM_HOSTBYTE_STRS)
		printk("(%s) ", hostbyte_table[hb]);
	else
		printk("is invalid ");
}
#else
void scsi_print_hostbyte(int scsiresult)
{
	printk("Hostbyte=0x%02x ", host_byte(scsiresult));
}
#endif

#ifdef CONFIG_SCSI_CONSTANTS

static const char * const driverbyte_table[]={
"DRIVER_OK", "DRIVER_BUSY", "DRIVER_SOFT",  "DRIVER_MEDIA", "DRIVER_ERROR",
"DRIVER_INVALID", "DRIVER_TIMEOUT", "DRIVER_HARD", "DRIVER_SENSE"};
#define NUM_DRIVERBYTE_STRS ARRAY_SIZE(driverbyte_table)

static const char * const driversuggest_table[]={"SUGGEST_OK",
"SUGGEST_RETRY", "SUGGEST_ABORT", "SUGGEST_REMAP", "SUGGEST_DIE",
"SUGGEST_5", "SUGGEST_6", "SUGGEST_7", "SUGGEST_SENSE"};
#define NUM_SUGGEST_STRS ARRAY_SIZE(driversuggest_table)

void scsi_print_driverbyte(int scsiresult)
{
	int dr = (driver_byte(scsiresult) & DRIVER_MASK);
	int su = ((driver_byte(scsiresult) & SUGGEST_MASK) >> 4);

	printk("Driverbyte=0x%02x ", driver_byte(scsiresult));
	printk("(%s,%s) ",
	       (dr < NUM_DRIVERBYTE_STRS ? driverbyte_table[dr] : "invalid"),
	       (su < NUM_SUGGEST_STRS ? driversuggest_table[su] : "invalid"));
}
#else
void scsi_print_driverbyte(int scsiresult)
{
	printk("Driverbyte=0x%02x ", driver_byte(scsiresult));
}
#endif
