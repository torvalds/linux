// SPDX-License-Identifier: GPL-2.0
/*
 * ASCII values for a number of symbolic constants, printing functions,
 * etc.
 * Additions for SCSI 2 and Linux 2.2.x by D. Gilbert (990422)
 * Additions for SCSI 3+ (SPC-3 T10/1416-D Rev 07 3 May 2002)
 *   by D. Gilbert and aeb (20020609)
 * Updated to SPC-4 T10/1713-D Rev 36g, D. Gilbert 20130701
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
#define THIRD_PARTY_COPY_OUT 0x83
#define THIRD_PARTY_COPY_IN 0x84

struct sa_name_list {
	int opcode;
	const struct value_name_pair *arr;
	int arr_sz;
};

struct value_name_pair {
	int value;
	const char * name;
};

static const char * cdb_byte0_names[] = {
/* 00-03 */ "Test Unit Ready", "Rezero Unit/Rewind", NULL, "Request Sense",
/* 04-07 */ "Format Unit/Medium", "Read Block Limits", NULL,
	    "Reassign Blocks",
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
/* 42-48 */ "Unmap/Read sub-channel", "Read TOC/PMA/ATIP",
	    "Read density support", "Play audio(10)", "Get configuration",
	    "Play audio msf", "Sanitize/Play audio track/index",
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
/* 78-7f */ NULL, NULL, NULL, NULL, NULL, NULL, "Extended CDB",
	    "Variable length",
/* 80-84 */ "Xdwrite(16)", "Rebuild(16)", "Regenerate(16)",
	    "Third party copy out", "Third party copy in",
/* 85-89 */ "ATA command pass through(16)", "Access control in",
	    "Access control out", "Read(16)", "Compare and Write",
/* 8a-8f */ "Write(16)", "ORWrite", "Read attributes", "Write attributes",
            "Write and verify(16)", "Verify(16)",
/* 90-94 */ "Pre-fetch(16)", "Synchronize cache(16)",
            "Lock/unlock cache(16)", "Write same(16)", NULL,
/* 95-99 */ NULL, NULL, NULL, NULL, NULL,
/* 9a-9f */ NULL, NULL, NULL, "Service action bidirectional",
	    "Service action in(16)", "Service action out(16)",
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

static const struct value_name_pair maint_in_arr[] = {
	{0x5, "Report identifying information"},
	{0xa, "Report target port groups"},
	{0xb, "Report aliases"},
	{0xc, "Report supported operation codes"},
	{0xd, "Report supported task management functions"},
	{0xe, "Report priority"},
	{0xf, "Report timestamp"},
	{0x10, "Management protocol in"},
};
#define MAINT_IN_SZ ARRAY_SIZE(maint_in_arr)

static const struct value_name_pair maint_out_arr[] = {
	{0x6, "Set identifying information"},
	{0xa, "Set target port groups"},
	{0xb, "Change aliases"},
	{0xc, "Remove I_T nexus"},
	{0xe, "Set priority"},
	{0xf, "Set timestamp"},
	{0x10, "Management protocol out"},
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

static const struct value_name_pair serv_bidi_arr[] = {
	{-1, "dummy entry"},
};
#define SERV_BIDI_SZ ARRAY_SIZE(serv_bidi_arr)

static const struct value_name_pair serv_in16_arr[] = {
	{0x10, "Read capacity(16)"},
	{0x11, "Read long(16)"},
	{0x12, "Get LBA status"},
	{0x13, "Report referrals"},
};
#define SERV_IN16_SZ ARRAY_SIZE(serv_in16_arr)

static const struct value_name_pair serv_out16_arr[] = {
	{0x11, "Write long(16)"},
	{0x1f, "Notify data transfer device(16)"},
};
#define SERV_OUT16_SZ ARRAY_SIZE(serv_out16_arr)

static const struct value_name_pair pr_in_arr[] = {
	{0x0, "Persistent reserve in, read keys"},
	{0x1, "Persistent reserve in, read reservation"},
	{0x2, "Persistent reserve in, report capabilities"},
	{0x3, "Persistent reserve in, read full status"},
};
#define PR_IN_SZ ARRAY_SIZE(pr_in_arr)

static const struct value_name_pair pr_out_arr[] = {
	{0x0, "Persistent reserve out, register"},
	{0x1, "Persistent reserve out, reserve"},
	{0x2, "Persistent reserve out, release"},
	{0x3, "Persistent reserve out, clear"},
	{0x4, "Persistent reserve out, preempt"},
	{0x5, "Persistent reserve out, preempt and abort"},
	{0x6, "Persistent reserve out, register and ignore existing key"},
	{0x7, "Persistent reserve out, register and move"},
};
#define PR_OUT_SZ ARRAY_SIZE(pr_out_arr)

/* SPC-4 rev 34 renamed the Extended Copy opcode to Third Party Copy Out.
   LID1 (List Identifier length: 1 byte) is the Extended Copy found in SPC-2
   and SPC-3 */
static const struct value_name_pair tpc_out_arr[] = {
	{0x0, "Extended copy(LID1)"},
	{0x1, "Extended copy(LID4)"},
	{0x10, "Populate token"},
	{0x11, "Write using token"},
	{0x1c, "Copy operation abort"},
};
#define TPC_OUT_SZ ARRAY_SIZE(tpc_out_arr)

static const struct value_name_pair tpc_in_arr[] = {
	{0x0, "Receive copy status(LID1)"},
	{0x1, "Receive copy data(LID1)"},
	{0x3, "Receive copy operating parameters"},
	{0x4, "Receive copy failure details(LID1)"},
	{0x5, "Receive copy status(LID4)"},
	{0x6, "Receive copy data(LID4)"},
	{0x7, "Receive ROD token information"},
	{0x8, "Report all ROD tokens"},
};
#define TPC_IN_SZ ARRAY_SIZE(tpc_in_arr)


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

static struct sa_name_list sa_names_arr[] = {
	{VARIABLE_LENGTH_CMD, variable_length_arr, VARIABLE_LENGTH_SZ},
	{MAINTENANCE_IN, maint_in_arr, MAINT_IN_SZ},
	{MAINTENANCE_OUT, maint_out_arr, MAINT_OUT_SZ},
	{PERSISTENT_RESERVE_IN, pr_in_arr, PR_IN_SZ},
	{PERSISTENT_RESERVE_OUT, pr_out_arr, PR_OUT_SZ},
	{SERVICE_ACTION_IN_12, serv_in12_arr, SERV_IN12_SZ},
	{SERVICE_ACTION_OUT_12, serv_out12_arr, SERV_OUT12_SZ},
	{SERVICE_ACTION_BIDIRECTIONAL, serv_bidi_arr, SERV_BIDI_SZ},
	{SERVICE_ACTION_IN_16, serv_in16_arr, SERV_IN16_SZ},
	{SERVICE_ACTION_OUT_16, serv_out16_arr, SERV_OUT16_SZ},
	{THIRD_PARTY_COPY_IN, tpc_in_arr, TPC_IN_SZ},
	{THIRD_PARTY_COPY_OUT, tpc_out_arr, TPC_OUT_SZ},
	{0, NULL, 0},
};

bool scsi_opcode_sa_name(int opcode, int service_action,
			 const char **cdb_name, const char **sa_name)
{
	struct sa_name_list *sa_name_ptr;
	const struct value_name_pair *arr = NULL;
	int arr_sz, k;

	*cdb_name = NULL;
	if (opcode >= VENDOR_SPECIFIC_CDB)
		return false;

	if (opcode < ARRAY_SIZE(cdb_byte0_names))
		*cdb_name = cdb_byte0_names[opcode];

	for (sa_name_ptr = sa_names_arr; sa_name_ptr->arr; ++sa_name_ptr) {
		if (sa_name_ptr->opcode == opcode) {
			arr = sa_name_ptr->arr;
			arr_sz = sa_name_ptr->arr_sz;
			break;
		}
	}
	if (!arr)
		return false;

	for (k = 0; k < arr_sz; ++k, ++arr) {
		if (service_action == arr->value)
			break;
	}
	if (k < arr_sz)
		*sa_name = arr->name;

	return true;
}

struct error_info {
	unsigned short code12;	/* 0x0302 looks better than 0x03,0x02 */
	unsigned short size;
};

/*
 * There are 700+ entries in this table. To save space, we don't store
 * (code, pointer) pairs, which would make sizeof(struct
 * error_info)==16 on 64 bits. Rather, the second element just stores
 * the size (including \0) of the corresponding string, and we use the
 * sum of these to get the appropriate offset into additional_text
 * defined below. This approach saves 12 bytes per entry.
 */
static const struct error_info additional[] =
{
#define SENSE_CODE(c, s) {c, sizeof(s)},
#include "sense_codes.h"
#undef SENSE_CODE
};

static const char *additional_text =
#define SENSE_CODE(c, s) s "\0"
#include "sense_codes.h"
#undef SENSE_CODE
	;

struct error_info2 {
	unsigned char code1, code2_min, code2_max;
	const char * str;
	const char * fmt;
};

static const struct error_info2 additional2[] =
{
	{0x40, 0x00, 0x7f, "Ram failure", ""},
	{0x40, 0x80, 0xff, "Diagnostic failure on component", ""},
	{0x41, 0x00, 0xff, "Data path failure", ""},
	{0x42, 0x00, 0xff, "Power-on or self-test failure", ""},
	{0x4D, 0x00, 0xff, "Tagged overlapped commands", "task tag "},
	{0x70, 0x00, 0xff, "Decompression exception", "short algorithm id of "},
	{0, 0, 0, NULL, NULL}
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
	"Equal",	    /* C: A SEARCH DATA command found data equal,
				  reserved in SPC-4 rev 36 */
	"Volume Overflow",  /* D: Medium full with still data to be written */
	"Miscompare",	    /* E: Source data and data on the medium
				  do not agree */
	"Completed",	    /* F: command completed sense data reported,
				  may occur for successful command */
};

/* Get sense key string or NULL if not available */
const char *
scsi_sense_key_string(unsigned char key)
{
	if (key < ARRAY_SIZE(snstext))
		return snstext[key];
	return NULL;
}
EXPORT_SYMBOL(scsi_sense_key_string);

/*
 * Get additional sense code string or NULL if not available.
 * This string may contain a "%x" and should be printed with ascq as arg.
 */
const char *
scsi_extd_sense_format(unsigned char asc, unsigned char ascq, const char **fmt)
{
	int i;
	unsigned short code = ((asc << 8) | ascq);
	unsigned offset = 0;

	*fmt = NULL;
	for (i = 0; i < ARRAY_SIZE(additional); i++) {
		if (additional[i].code12 == code)
			return additional_text + offset;
		offset += additional[i].size;
	}
	for (i = 0; additional2[i].fmt; i++) {
		if (additional2[i].code1 == asc &&
		    ascq >= additional2[i].code2_min &&
		    ascq <= additional2[i].code2_max) {
			*fmt = additional2[i].fmt;
			return additional2[i].str;
		}
	}
	return NULL;
}
EXPORT_SYMBOL(scsi_extd_sense_format);

static const char * const hostbyte_table[]={
"DID_OK", "DID_NO_CONNECT", "DID_BUS_BUSY", "DID_TIME_OUT", "DID_BAD_TARGET",
"DID_ABORT", "DID_PARITY", "DID_ERROR", "DID_RESET", "DID_BAD_INTR",
"DID_PASSTHROUGH", "DID_SOFT_ERROR", "DID_IMM_RETRY", "DID_REQUEUE",
"DID_TRANSPORT_DISRUPTED", "DID_TRANSPORT_FAILFAST", "DID_TARGET_FAILURE",
"DID_NEXUS_FAILURE", "DID_ALLOC_FAILURE", "DID_MEDIUM_ERROR" };

const char *scsi_hostbyte_string(int result)
{
	enum scsi_host_status hb = host_byte(result);
	const char *hb_string = NULL;

	if (hb < ARRAY_SIZE(hostbyte_table))
		hb_string = hostbyte_table[hb];
	return hb_string;
}
EXPORT_SYMBOL(scsi_hostbyte_string);

#define scsi_mlreturn_name(result)	{ result, #result }
static const struct value_name_pair scsi_mlreturn_arr[] = {
	scsi_mlreturn_name(NEEDS_RETRY),
	scsi_mlreturn_name(SUCCESS),
	scsi_mlreturn_name(FAILED),
	scsi_mlreturn_name(QUEUED),
	scsi_mlreturn_name(SOFT_ERROR),
	scsi_mlreturn_name(ADD_TO_MLQUEUE),
	scsi_mlreturn_name(TIMEOUT_ERROR),
	scsi_mlreturn_name(SCSI_RETURN_NOT_HANDLED),
	scsi_mlreturn_name(FAST_IO_FAIL)
};

const char *scsi_mlreturn_string(int result)
{
	const struct value_name_pair *arr = scsi_mlreturn_arr;
	int k;

	for (k = 0; k < ARRAY_SIZE(scsi_mlreturn_arr); ++k, ++arr) {
		if (result == arr->value)
			return arr->name;
	}
	return NULL;
}
EXPORT_SYMBOL(scsi_mlreturn_string);
