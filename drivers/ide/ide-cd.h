/*
 *  linux/drivers/ide/ide_cd.h
 *
 *  Copyright (C) 1996-98  Erik Andersen
 *  Copyright (C) 1998-2000 Jens Axboe
 */
#ifndef _IDE_CD_H
#define _IDE_CD_H

#include <linux/cdrom.h>
#include <asm/byteorder.h>

/* Turn this on to have the driver print out the meanings of the
   ATAPI error codes.  This will use up additional kernel-space
   memory, though. */

#ifndef VERBOSE_IDE_CD_ERRORS
#define VERBOSE_IDE_CD_ERRORS 1
#endif

/*
 * typical timeout for packet command
 */
#define ATAPI_WAIT_PC		(60 * HZ)
#define ATAPI_WAIT_WRITE_BUSY	(10 * HZ)

/************************************************************************/

#define SECTOR_BITS 		9
#ifndef SECTOR_SIZE
#define SECTOR_SIZE		(1 << SECTOR_BITS)
#endif
#define SECTORS_PER_FRAME	(CD_FRAMESIZE >> SECTOR_BITS)
#define SECTOR_BUFFER_SIZE	(CD_FRAMESIZE * 32)

/* Capabilities Page size including 8 bytes of Mode Page Header */
#define ATAPI_CAPABILITIES_PAGE_SIZE		(8 + 20)
#define ATAPI_CAPABILITIES_PAGE_PAD_SIZE	4

enum {
	/* Device sends an interrupt when ready for a packet command. */
	IDE_CD_FLAG_DRQ_INTERRUPT	= (1 << 0),
	/* Drive cannot lock the door. */
	IDE_CD_FLAG_NO_DOORLOCK		= (1 << 1),
	/* Drive cannot eject the disc. */
	IDE_CD_FLAG_NO_EJECT		= (1 << 2),
	/* Drive is a pre-1.2 NEC 260 drive. */
	IDE_CD_FLAG_NEC260		= (1 << 3),
	/* TOC addresses are in BCD. */
	IDE_CD_FLAG_TOCADDR_AS_BCD	= (1 << 4),
	/* TOC track numbers are in BCD. */
	IDE_CD_FLAG_TOCTRACKS_AS_BCD	= (1 << 5),
	/*
	 * Drive does not provide data in multiples of SECTOR_SIZE
	 * when more than one interrupt is needed.
	 */
	IDE_CD_FLAG_LIMIT_NFRAMES	= (1 << 6),
	/* Seeking in progress. */
	IDE_CD_FLAG_SEEKING		= (1 << 7),
	/* Driver has noticed a media change. */
	IDE_CD_FLAG_MEDIA_CHANGED	= (1 << 8),
	/* Saved TOC information is current. */
	IDE_CD_FLAG_TOC_VALID		= (1 << 9),
	/* We think that the drive door is locked. */
	IDE_CD_FLAG_DOOR_LOCKED		= (1 << 10),
	/* SET_CD_SPEED command is unsupported. */
	IDE_CD_FLAG_NO_SPEED_SELECT	= (1 << 11),
};

/* Structure of a MSF cdrom address. */
struct atapi_msf {
	byte reserved;
	byte minute;
	byte second;
	byte frame;
};

/* Space to hold the disk TOC. */
#define MAX_TRACKS 99
struct atapi_toc_header {
	unsigned short toc_length;
	byte first_track;
	byte last_track;
};

struct atapi_toc_entry {
	byte reserved1;
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 adr     : 4;
	__u8 control : 4;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 control : 4;
	__u8 adr     : 4;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	byte track;
	byte reserved2;
	union {
		unsigned lba;
		struct atapi_msf msf;
	} addr;
};

struct atapi_toc {
	int    last_session_lba;
	int    xa_flag;
	unsigned long capacity;
	struct atapi_toc_header hdr;
	struct atapi_toc_entry  ent[MAX_TRACKS+1];
	  /* One extra for the leadout. */
};

/* Extra per-device info for cdrom drives. */
struct cdrom_info {
	ide_drive_t	*drive;
	ide_driver_t	*driver;
	struct gendisk	*disk;
	struct kref	kref;

	/* Buffer for table of contents.  NULL if we haven't allocated
	   a TOC buffer for this device yet. */

	struct atapi_toc *toc;

	unsigned long	sector_buffered;
	unsigned long	nsectors_buffered;
	unsigned char	*buffer;

	/* The result of the last successful request sense command
	   on this device. */
	struct request_sense sense_data;

	struct request request_sense_request;
	int dma;
	unsigned long last_block;
	unsigned long start_seek;

	unsigned int cd_flags;

	u8 max_speed;		/* Max speed of the drive. */
	u8 current_speed;	/* Current speed of the drive. */

        /* Per-device info needed by cdrom.c generic driver. */
        struct cdrom_device_info devinfo;

	unsigned long write_timeout;
};

/****************************************************************************
 * Descriptions of ATAPI error codes.
 */

/* This stuff should be in cdrom.h, since it is now generic... */

/* ATAPI sense keys (from table 140 of ATAPI 2.6) */
#define NO_SENSE                0x00
#define RECOVERED_ERROR         0x01
#define NOT_READY               0x02
#define MEDIUM_ERROR            0x03
#define HARDWARE_ERROR          0x04
#define ILLEGAL_REQUEST         0x05
#define UNIT_ATTENTION          0x06
#define DATA_PROTECT            0x07
#define BLANK_CHECK             0x08
#define ABORTED_COMMAND         0x0b
#define MISCOMPARE              0x0e

 

/* This stuff should be in cdrom.h, since it is now generic... */
#if VERBOSE_IDE_CD_ERRORS

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
	{ GPCMD_GET_EVENT_STATUS_NOTIFICATION, "Get Event Status Notification" },
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
	{ 0x015d00, 
	    "Failure prediction threshold exceeded - Predicted logical unit failure" },
	{ 0x015d01, 
	    "Failure prediction threshold exceeded - Predicted media failure" },
	{ 0x015dff, "Failure prediction threshold exceeded - False" },
	{ 0x017301, "Power calibration area almost full" },
	{ 0x020400, "Logical unit not ready - cause not reportable" },
	/* Following is misspelled in ATAPI 2.6, _and_ in Mt. Fuji */
	{ 0x020401,
	  "Logical unit not ready - in progress [sic] of becoming ready" },
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
	{ 0x056f00, "Copy protection key exchange failure - Authentication failure" },
	{ 0x056f01, "Copy protection key exchange failure - Key not present" },
	{ 0x056f02, "Copy protection key exchange failure - Key not established" },
	{ 0x056f03, "Read of scrambled sector without authentication" },
	{ 0x056f04, "Media region code is mismatched to logical unit" },
	{ 0x056f05,  "Drive region must be permanent / region reset count error" },
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
#endif


#endif /* _IDE_CD_H */
