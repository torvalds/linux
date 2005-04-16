/*
 * Implementation of Utility functions for all SCSI device types.
 *
 * Copyright (c) 1997, 1998, 1999 Justin T. Gibbs.
 * Copyright (c) 1997, 1998 Kenneth D. Merry.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/cam/scsi/scsi_all.c,v 1.38 2002/09/23 04:56:35 mjacob Exp $
 * $Id$
 */

#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/version.h>

/* Core SCSI definitions */
#include "scsi.h"
#include <scsi/scsi_host.h>
#include "aiclib.h"
#include "cam.h"

#ifndef FALSE
#define FALSE   0
#endif /* FALSE */
#ifndef TRUE
#define TRUE    1
#endif /* TRUE */
#ifndef ERESTART
#define ERESTART        -1              /* restart syscall */
#endif
#ifndef EJUSTRETURN
#define EJUSTRETURN     -2              /* don't modify regs, just return */
#endif

static int	ascentrycomp(const void *key, const void *member);
static int	senseentrycomp(const void *key, const void *member);
static void	fetchtableentries(int sense_key, int asc, int ascq,
				  struct scsi_inquiry_data *,
				  const struct sense_key_table_entry **,
				  const struct asc_table_entry **);
static void *	scsibsearch(const void *key, const void *base, size_t nmemb,
			    size_t size,
			    int (*compar)(const void *, const void *));
typedef int (cam_quirkmatch_t)(caddr_t, caddr_t);
static int	cam_strmatch(const u_int8_t *str, const u_int8_t *pattern,
			     int str_len);
static caddr_t	cam_quirkmatch(caddr_t target, caddr_t quirk_table,
			       int num_entries, int entry_size,
			       cam_quirkmatch_t *comp_func);

#define SCSI_NO_SENSE_STRINGS 1
#if !defined(SCSI_NO_SENSE_STRINGS)
#define SST(asc, ascq, action, desc) \
	asc, ascq, action, desc
#else 
static const char empty_string[] = "";

#define SST(asc, ascq, action, desc) \
	asc, ascq, action, empty_string
#endif 

static const struct sense_key_table_entry sense_key_table[] = 
{
	{ SSD_KEY_NO_SENSE, SS_NOP, "NO SENSE" },
	{ SSD_KEY_RECOVERED_ERROR, SS_NOP|SSQ_PRINT_SENSE, "RECOVERED ERROR" },
	{
	  SSD_KEY_NOT_READY, SS_TUR|SSQ_MANY|SSQ_DECREMENT_COUNT|EBUSY,
	  "NOT READY"
	},
	{ SSD_KEY_MEDIUM_ERROR, SS_RDEF, "MEDIUM ERROR" },
	{ SSD_KEY_HARDWARE_ERROR, SS_RDEF, "HARDWARE FAILURE" },
	{ SSD_KEY_ILLEGAL_REQUEST, SS_FATAL|EINVAL, "ILLEGAL REQUEST" },
	{ SSD_KEY_UNIT_ATTENTION, SS_FATAL|ENXIO, "UNIT ATTENTION" },
	{ SSD_KEY_DATA_PROTECT, SS_FATAL|EACCES, "DATA PROTECT" },
	{ SSD_KEY_BLANK_CHECK, SS_FATAL|ENOSPC, "BLANK CHECK" },
	{ SSD_KEY_Vendor_Specific, SS_FATAL|EIO, "Vendor Specific" },
	{ SSD_KEY_COPY_ABORTED, SS_FATAL|EIO, "COPY ABORTED" },
	{ SSD_KEY_ABORTED_COMMAND, SS_RDEF, "ABORTED COMMAND" },
	{ SSD_KEY_EQUAL, SS_NOP, "EQUAL" },
	{ SSD_KEY_VOLUME_OVERFLOW, SS_FATAL|EIO, "VOLUME OVERFLOW" },
	{ SSD_KEY_MISCOMPARE, SS_NOP, "MISCOMPARE" },
	{ SSD_KEY_RESERVED, SS_FATAL|EIO, "RESERVED" }
};

static const int sense_key_table_size =
    sizeof(sense_key_table)/sizeof(sense_key_table[0]);

static struct asc_table_entry quantum_fireball_entries[] = {
	{SST(0x04, 0x0b, SS_START|SSQ_DECREMENT_COUNT|ENXIO, 
	     "Logical unit not ready, initializing cmd. required")}
};

static struct asc_table_entry sony_mo_entries[] = {
	{SST(0x04, 0x00, SS_START|SSQ_DECREMENT_COUNT|ENXIO,
	     "Logical unit not ready, cause not reportable")}
};

static struct scsi_sense_quirk_entry sense_quirk_table[] = {
	{
		/*
		 * The Quantum Fireball ST and SE like to return 0x04 0x0b when
		 * they really should return 0x04 0x02.  0x04,0x0b isn't
		 * defined in any SCSI spec, and it isn't mentioned in the
		 * hardware manual for these drives.
		 */
		{T_DIRECT, SIP_MEDIA_FIXED, "QUANTUM", "FIREBALL S*", "*"},
		/*num_sense_keys*/0,
		sizeof(quantum_fireball_entries)/sizeof(struct asc_table_entry),
		/*sense key entries*/NULL,
		quantum_fireball_entries
	},
	{
		/*
		 * This Sony MO drive likes to return 0x04, 0x00 when it
		 * isn't spun up.
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "SONY", "SMO-*", "*"},
		/*num_sense_keys*/0,
		sizeof(sony_mo_entries)/sizeof(struct asc_table_entry),
		/*sense key entries*/NULL,
		sony_mo_entries
	}
};

static const int sense_quirk_table_size =
    sizeof(sense_quirk_table)/sizeof(sense_quirk_table[0]);

static struct asc_table_entry asc_table[] = {
/*
 * From File: ASC-NUM.TXT
 * SCSI ASC/ASCQ Assignments
 * Numeric Sorted Listing
 * as of  5/12/97
 *
 * D - DIRECT ACCESS DEVICE (SBC)                     device column key
 * .T - SEQUENTIAL ACCESS DEVICE (SSC)               -------------------
 * . L - PRINTER DEVICE (SSC)                           blank = reserved
 * .  P - PROCESSOR DEVICE (SPC)                     not blank = allowed
 * .  .W - WRITE ONCE READ MULTIPLE DEVICE (SBC)
 * .  . R - CD DEVICE (MMC)
 * .  .  S - SCANNER DEVICE (SGC)
 * .  .  .O - OPTICAL MEMORY DEVICE (SBC)
 * .  .  . M - MEDIA CHANGER DEVICE (SMC)
 * .  .  .  C - COMMUNICATION DEVICE (SSC)
 * .  .  .  .A - STORAGE ARRAY DEVICE (SCC)
 * .  .  .  . E - ENCLOSURE SERVICES DEVICE (SES)
 * DTLPWRSOMCAE        ASC   ASCQ  Action  Description
 * ------------        ----  ----  ------  -----------------------------------*/
/* DTLPWRSOMCAE */{SST(0x00, 0x00, SS_NOP,
			"No additional sense information") },
/*  T    S      */{SST(0x00, 0x01, SS_RDEF,
			"Filemark detected") },
/*  T    S      */{SST(0x00, 0x02, SS_RDEF,
			"End-of-partition/medium detected") },
/*  T           */{SST(0x00, 0x03, SS_RDEF,
			"Setmark detected") },
/*  T    S      */{SST(0x00, 0x04, SS_RDEF,
			"Beginning-of-partition/medium detected") },
/*  T    S      */{SST(0x00, 0x05, SS_RDEF,
			"End-of-data detected") },
/* DTLPWRSOMCAE */{SST(0x00, 0x06, SS_RDEF,
			"I/O process terminated") },
/*      R       */{SST(0x00, 0x11, SS_FATAL|EBUSY,
			"Audio play operation in progress") },
/*      R       */{SST(0x00, 0x12, SS_NOP,
			"Audio play operation paused") },
/*      R       */{SST(0x00, 0x13, SS_NOP,
			"Audio play operation successfully completed") },
/*      R       */{SST(0x00, 0x14, SS_RDEF,
			"Audio play operation stopped due to error") },
/*      R       */{SST(0x00, 0x15, SS_NOP,
			"No current audio status to return") },
/* DTLPWRSOMCAE */{SST(0x00, 0x16, SS_FATAL|EBUSY,
			"Operation in progress") },
/* DTL WRSOM AE */{SST(0x00, 0x17, SS_RDEF,
			"Cleaning requested") },
/* D   W  O     */{SST(0x01, 0x00, SS_RDEF,
			"No index/sector signal") },
/* D   WR OM    */{SST(0x02, 0x00, SS_RDEF,
			"No seek complete") },
/* DTL W SO     */{SST(0x03, 0x00, SS_RDEF,
			"Peripheral device write fault") },
/*  T           */{SST(0x03, 0x01, SS_RDEF,
			"No write current") },
/*  T           */{SST(0x03, 0x02, SS_RDEF,
			"Excessive write errors") },
/* DTLPWRSOMCAE */{SST(0x04, 0x00,
			SS_TUR|SSQ_DELAY|SSQ_MANY|SSQ_DECREMENT_COUNT|EIO,
			"Logical unit not ready, cause not reportable") },
/* DTLPWRSOMCAE */{SST(0x04, 0x01,
			SS_TUR|SSQ_DELAY|SSQ_MANY|SSQ_DECREMENT_COUNT|EBUSY,
			"Logical unit is in process of becoming ready") },
/* DTLPWRSOMCAE */{SST(0x04, 0x02, SS_START|SSQ_DECREMENT_COUNT|ENXIO,
			"Logical unit not ready, initializing cmd. required") },
/* DTLPWRSOMCAE */{SST(0x04, 0x03, SS_FATAL|ENXIO,
			"Logical unit not ready, manual intervention required")},
/* DTL    O     */{SST(0x04, 0x04, SS_FATAL|EBUSY,
			"Logical unit not ready, format in progress") },
/* DT  W  OMCA  */{SST(0x04, 0x05, SS_FATAL|EBUSY,
			"Logical unit not ready, rebuild in progress") },
/* DT  W  OMCA  */{SST(0x04, 0x06, SS_FATAL|EBUSY,
			"Logical unit not ready, recalculation in progress") },
/* DTLPWRSOMCAE */{SST(0x04, 0x07, SS_FATAL|EBUSY,
			"Logical unit not ready, operation in progress") },
/*      R       */{SST(0x04, 0x08, SS_FATAL|EBUSY,
			"Logical unit not ready, long write in progress") },
/* DTL WRSOMCAE */{SST(0x05, 0x00, SS_RDEF,
			"Logical unit does not respond to selection") },
/* D   WR OM    */{SST(0x06, 0x00, SS_RDEF,
			"No reference position found") },
/* DTL WRSOM    */{SST(0x07, 0x00, SS_RDEF,
			"Multiple peripheral devices selected") },
/* DTL WRSOMCAE */{SST(0x08, 0x00, SS_RDEF,
			"Logical unit communication failure") },
/* DTL WRSOMCAE */{SST(0x08, 0x01, SS_RDEF,
			"Logical unit communication time-out") },
/* DTL WRSOMCAE */{SST(0x08, 0x02, SS_RDEF,
			"Logical unit communication parity error") },
/* DT   R OM    */{SST(0x08, 0x03, SS_RDEF,
			"Logical unit communication crc error (ultra-dma/32)")},
/* DT  WR O     */{SST(0x09, 0x00, SS_RDEF,
			"Track following error") },
/*     WR O     */{SST(0x09, 0x01, SS_RDEF,
			"Tracking servo failure") },
/*     WR O     */{SST(0x09, 0x02, SS_RDEF,
			"Focus servo failure") },
/*     WR O     */{SST(0x09, 0x03, SS_RDEF,
			"Spindle servo failure") },
/* DT  WR O     */{SST(0x09, 0x04, SS_RDEF,
			"Head select fault") },
/* DTLPWRSOMCAE */{SST(0x0A, 0x00, SS_FATAL|ENOSPC,
			"Error log overflow") },
/* DTLPWRSOMCAE */{SST(0x0B, 0x00, SS_RDEF,
			"Warning") },
/* DTLPWRSOMCAE */{SST(0x0B, 0x01, SS_RDEF,
			"Specified temperature exceeded") },
/* DTLPWRSOMCAE */{SST(0x0B, 0x02, SS_RDEF,
			"Enclosure degraded") },
/*  T   RS      */{SST(0x0C, 0x00, SS_RDEF,
			"Write error") },
/* D   W  O     */{SST(0x0C, 0x01, SS_NOP|SSQ_PRINT_SENSE,
			"Write error - recovered with auto reallocation") },
/* D   W  O     */{SST(0x0C, 0x02, SS_RDEF,
			"Write error - auto reallocation failed") },
/* D   W  O     */{SST(0x0C, 0x03, SS_RDEF,
			"Write error - recommend reassignment") },
/* DT  W  O     */{SST(0x0C, 0x04, SS_RDEF,
			"Compression check miscompare error") },
/* DT  W  O     */{SST(0x0C, 0x05, SS_RDEF,
			"Data expansion occurred during compression") },
/* DT  W  O     */{SST(0x0C, 0x06, SS_RDEF,
			"Block not compressible") },
/*      R       */{SST(0x0C, 0x07, SS_RDEF,
			"Write error - recovery needed") },
/*      R       */{SST(0x0C, 0x08, SS_RDEF,
			"Write error - recovery failed") },
/*      R       */{SST(0x0C, 0x09, SS_RDEF,
			"Write error - loss of streaming") },
/*      R       */{SST(0x0C, 0x0A, SS_RDEF,
			"Write error - padding blocks added") },
/* D   W  O     */{SST(0x10, 0x00, SS_RDEF,
			"ID CRC or ECC error") },
/* DT  WRSO     */{SST(0x11, 0x00, SS_RDEF,
			"Unrecovered read error") },
/* DT  W SO     */{SST(0x11, 0x01, SS_RDEF,
			"Read retries exhausted") },
/* DT  W SO     */{SST(0x11, 0x02, SS_RDEF,
			"Error too long to correct") },
/* DT  W SO     */{SST(0x11, 0x03, SS_RDEF,
			"Multiple read errors") },
/* D   W  O     */{SST(0x11, 0x04, SS_RDEF,
			"Unrecovered read error - auto reallocate failed") },
/*     WR O     */{SST(0x11, 0x05, SS_RDEF,
			"L-EC uncorrectable error") },
/*     WR O     */{SST(0x11, 0x06, SS_RDEF,
			"CIRC unrecovered error") },
/*     W  O     */{SST(0x11, 0x07, SS_RDEF,
			"Data re-synchronization error") },
/*  T           */{SST(0x11, 0x08, SS_RDEF,
			"Incomplete block read") },
/*  T           */{SST(0x11, 0x09, SS_RDEF,
			"No gap found") },
/* DT     O     */{SST(0x11, 0x0A, SS_RDEF,
			"Miscorrected error") },
/* D   W  O     */{SST(0x11, 0x0B, SS_RDEF,
			"Unrecovered read error - recommend reassignment") },
/* D   W  O     */{SST(0x11, 0x0C, SS_RDEF,
			"Unrecovered read error - recommend rewrite the data")},
/* DT  WR O     */{SST(0x11, 0x0D, SS_RDEF,
			"De-compression CRC error") },
/* DT  WR O     */{SST(0x11, 0x0E, SS_RDEF,
			"Cannot decompress using declared algorithm") },
/*      R       */{SST(0x11, 0x0F, SS_RDEF,
			"Error reading UPC/EAN number") },
/*      R       */{SST(0x11, 0x10, SS_RDEF,
			"Error reading ISRC number") },
/*      R       */{SST(0x11, 0x11, SS_RDEF,
			"Read error - loss of streaming") },
/* D   W  O     */{SST(0x12, 0x00, SS_RDEF,
			"Address mark not found for id field") },
/* D   W  O     */{SST(0x13, 0x00, SS_RDEF,
			"Address mark not found for data field") },
/* DTL WRSO     */{SST(0x14, 0x00, SS_RDEF,
			"Recorded entity not found") },
/* DT  WR O     */{SST(0x14, 0x01, SS_RDEF,
			"Record not found") },
/*  T           */{SST(0x14, 0x02, SS_RDEF,
			"Filemark or setmark not found") },
/*  T           */{SST(0x14, 0x03, SS_RDEF,
			"End-of-data not found") },
/*  T           */{SST(0x14, 0x04, SS_RDEF,
			"Block sequence error") },
/* DT  W  O     */{SST(0x14, 0x05, SS_RDEF,
			"Record not found - recommend reassignment") },
/* DT  W  O     */{SST(0x14, 0x06, SS_RDEF,
			"Record not found - data auto-reallocated") },
/* DTL WRSOM    */{SST(0x15, 0x00, SS_RDEF,
			"Random positioning error") },
/* DTL WRSOM    */{SST(0x15, 0x01, SS_RDEF,
			"Mechanical positioning error") },
/* DT  WR O     */{SST(0x15, 0x02, SS_RDEF,
			"Positioning error detected by read of medium") },
/* D   W  O     */{SST(0x16, 0x00, SS_RDEF,
			"Data synchronization mark error") },
/* D   W  O     */{SST(0x16, 0x01, SS_RDEF,
			"Data sync error - data rewritten") },
/* D   W  O     */{SST(0x16, 0x02, SS_RDEF,
			"Data sync error - recommend rewrite") },
/* D   W  O     */{SST(0x16, 0x03, SS_NOP|SSQ_PRINT_SENSE,
			"Data sync error - data auto-reallocated") },
/* D   W  O     */{SST(0x16, 0x04, SS_RDEF,
			"Data sync error - recommend reassignment") },
/* DT  WRSO     */{SST(0x17, 0x00, SS_NOP|SSQ_PRINT_SENSE,
			"Recovered data with no error correction applied") },
/* DT  WRSO     */{SST(0x17, 0x01, SS_NOP|SSQ_PRINT_SENSE,
			"Recovered data with retries") },
/* DT  WR O     */{SST(0x17, 0x02, SS_NOP|SSQ_PRINT_SENSE,
			"Recovered data with positive head offset") },
/* DT  WR O     */{SST(0x17, 0x03, SS_NOP|SSQ_PRINT_SENSE,
			"Recovered data with negative head offset") },
/*     WR O     */{SST(0x17, 0x04, SS_NOP|SSQ_PRINT_SENSE,
			"Recovered data with retries and/or CIRC applied") },
/* D   WR O     */{SST(0x17, 0x05, SS_NOP|SSQ_PRINT_SENSE,
			"Recovered data using previous sector id") },
/* D   W  O     */{SST(0x17, 0x06, SS_NOP|SSQ_PRINT_SENSE,
			"Recovered data without ECC - data auto-reallocated") },
/* D   W  O     */{SST(0x17, 0x07, SS_NOP|SSQ_PRINT_SENSE,
			"Recovered data without ECC - recommend reassignment")},
/* D   W  O     */{SST(0x17, 0x08, SS_NOP|SSQ_PRINT_SENSE,
			"Recovered data without ECC - recommend rewrite") },
/* D   W  O     */{SST(0x17, 0x09, SS_NOP|SSQ_PRINT_SENSE,
			"Recovered data without ECC - data rewritten") },
/* D   W  O     */{SST(0x18, 0x00, SS_NOP|SSQ_PRINT_SENSE,
			"Recovered data with error correction applied") },
/* D   WR O     */{SST(0x18, 0x01, SS_NOP|SSQ_PRINT_SENSE,
			"Recovered data with error corr. & retries applied") },
/* D   WR O     */{SST(0x18, 0x02, SS_NOP|SSQ_PRINT_SENSE,
			"Recovered data - data auto-reallocated") },
/*      R       */{SST(0x18, 0x03, SS_NOP|SSQ_PRINT_SENSE,
			"Recovered data with CIRC") },
/*      R       */{SST(0x18, 0x04, SS_NOP|SSQ_PRINT_SENSE,
			"Recovered data with L-EC") },
/* D   WR O     */{SST(0x18, 0x05, SS_NOP|SSQ_PRINT_SENSE,
			"Recovered data - recommend reassignment") },
/* D   WR O     */{SST(0x18, 0x06, SS_NOP|SSQ_PRINT_SENSE,
			"Recovered data - recommend rewrite") },
/* D   W  O     */{SST(0x18, 0x07, SS_NOP|SSQ_PRINT_SENSE,
			"Recovered data with ECC - data rewritten") },
/* D      O     */{SST(0x19, 0x00, SS_RDEF,
			"Defect list error") },
/* D      O     */{SST(0x19, 0x01, SS_RDEF,
			"Defect list not available") },
/* D      O     */{SST(0x19, 0x02, SS_RDEF,
			"Defect list error in primary list") },
/* D      O     */{SST(0x19, 0x03, SS_RDEF,
			"Defect list error in grown list") },
/* DTLPWRSOMCAE */{SST(0x1A, 0x00, SS_RDEF,
			"Parameter list length error") },
/* DTLPWRSOMCAE */{SST(0x1B, 0x00, SS_RDEF,
			"Synchronous data transfer error") },
/* D      O     */{SST(0x1C, 0x00, SS_RDEF,
			"Defect list not found") },
/* D      O     */{SST(0x1C, 0x01, SS_RDEF,
			"Primary defect list not found") },
/* D      O     */{SST(0x1C, 0x02, SS_RDEF,
			"Grown defect list not found") },
/* D   W  O     */{SST(0x1D, 0x00, SS_FATAL,
			"Miscompare during verify operation" )},
/* D   W  O     */{SST(0x1E, 0x00, SS_NOP|SSQ_PRINT_SENSE,
			"Recovered id with ecc correction") },
/* D      O     */{SST(0x1F, 0x00, SS_RDEF,
			"Partial defect list transfer") },
/* DTLPWRSOMCAE */{SST(0x20, 0x00, SS_FATAL|EINVAL,
			"Invalid command operation code") },
/* DT  WR OM    */{SST(0x21, 0x00, SS_FATAL|EINVAL,
			"Logical block address out of range" )},
/* DT  WR OM    */{SST(0x21, 0x01, SS_FATAL|EINVAL,
			"Invalid element address") },
/* D            */{SST(0x22, 0x00, SS_FATAL|EINVAL,
			"Illegal function") }, /* Deprecated. Use 20 00, 24 00, or 26 00 instead */
/* DTLPWRSOMCAE */{SST(0x24, 0x00, SS_FATAL|EINVAL,
			"Invalid field in CDB") },
/* DTLPWRSOMCAE */{SST(0x25, 0x00, SS_FATAL|ENXIO,
			"Logical unit not supported") },
/* DTLPWRSOMCAE */{SST(0x26, 0x00, SS_FATAL|EINVAL,
			"Invalid field in parameter list") },
/* DTLPWRSOMCAE */{SST(0x26, 0x01, SS_FATAL|EINVAL,
			"Parameter not supported") },
/* DTLPWRSOMCAE */{SST(0x26, 0x02, SS_FATAL|EINVAL,
			"Parameter value invalid") },
/* DTLPWRSOMCAE */{SST(0x26, 0x03, SS_FATAL|EINVAL,
			"Threshold parameters not supported") },
/* DTLPWRSOMCAE */{SST(0x26, 0x04, SS_FATAL|EINVAL,
			"Invalid release of active persistent reservation") },
/* DT  W  O     */{SST(0x27, 0x00, SS_FATAL|EACCES,
			"Write protected") },
/* DT  W  O     */{SST(0x27, 0x01, SS_FATAL|EACCES,
			"Hardware write protected") },
/* DT  W  O     */{SST(0x27, 0x02, SS_FATAL|EACCES,
			"Logical unit software write protected") },
/*  T           */{SST(0x27, 0x03, SS_FATAL|EACCES,
			"Associated write protect") },
/*  T           */{SST(0x27, 0x04, SS_FATAL|EACCES,
			"Persistent write protect") },
/*  T           */{SST(0x27, 0x05, SS_FATAL|EACCES,
			"Permanent write protect") },
/* DTLPWRSOMCAE */{SST(0x28, 0x00, SS_RDEF,
			"Not ready to ready change, medium may have changed") },
/* DTLPWRSOMCAE */{SST(0x28, 0x01, SS_FATAL|ENXIO,
			"Import or export element accessed") },
/*
 * XXX JGibbs - All of these should use the same errno, but I don't think
 * ENXIO is the correct choice.  Should we borrow from the networking
 * errnos?  ECONNRESET anyone?
 */
/* DTLPWRSOMCAE */{SST(0x29, 0x00, SS_RDEF,
			"Power on, reset, or bus device reset occurred") },
/* DTLPWRSOMCAE */{SST(0x29, 0x01, SS_RDEF,
			"Power on occurred") },
/* DTLPWRSOMCAE */{SST(0x29, 0x02, SS_RDEF,
			"Scsi bus reset occurred") },
/* DTLPWRSOMCAE */{SST(0x29, 0x03, SS_RDEF,
			"Bus device reset function occurred") },
/* DTLPWRSOMCAE */{SST(0x29, 0x04, SS_RDEF,
			"Device internal reset") },
/* DTLPWRSOMCAE */{SST(0x29, 0x05, SS_RDEF,
			"Transceiver mode changed to single-ended") },
/* DTLPWRSOMCAE */{SST(0x29, 0x06, SS_RDEF,
			"Transceiver mode changed to LVD") },
/* DTL WRSOMCAE */{SST(0x2A, 0x00, SS_RDEF,
			"Parameters changed") },
/* DTL WRSOMCAE */{SST(0x2A, 0x01, SS_RDEF,
			"Mode parameters changed") },
/* DTL WRSOMCAE */{SST(0x2A, 0x02, SS_RDEF,
			"Log parameters changed") },
/* DTLPWRSOMCAE */{SST(0x2A, 0x03, SS_RDEF,
			"Reservations preempted") },
/* DTLPWRSO C   */{SST(0x2B, 0x00, SS_RDEF,
			"Copy cannot execute since host cannot disconnect") },
/* DTLPWRSOMCAE */{SST(0x2C, 0x00, SS_RDEF,
			"Command sequence error") },
/*       S      */{SST(0x2C, 0x01, SS_RDEF,
			"Too many windows specified") },
/*       S      */{SST(0x2C, 0x02, SS_RDEF,
			"Invalid combination of windows specified") },
/*      R       */{SST(0x2C, 0x03, SS_RDEF,
			"Current program area is not empty") },
/*      R       */{SST(0x2C, 0x04, SS_RDEF,
			"Current program area is empty") },
/*  T           */{SST(0x2D, 0x00, SS_RDEF,
			"Overwrite error on update in place") },
/* DTLPWRSOMCAE */{SST(0x2F, 0x00, SS_RDEF,
			"Commands cleared by another initiator") },
/* DT  WR OM    */{SST(0x30, 0x00, SS_RDEF,
			"Incompatible medium installed") },
/* DT  WR O     */{SST(0x30, 0x01, SS_RDEF,
			"Cannot read medium - unknown format") },
/* DT  WR O     */{SST(0x30, 0x02, SS_RDEF,
			"Cannot read medium - incompatible format") },
/* DT           */{SST(0x30, 0x03, SS_RDEF,
			"Cleaning cartridge installed") },
/* DT  WR O     */{SST(0x30, 0x04, SS_RDEF,
			"Cannot write medium - unknown format") },
/* DT  WR O     */{SST(0x30, 0x05, SS_RDEF,
			"Cannot write medium - incompatible format") },
/* DT  W  O     */{SST(0x30, 0x06, SS_RDEF,
			"Cannot format medium - incompatible medium") },
/* DTL WRSOM AE */{SST(0x30, 0x07, SS_RDEF,
			"Cleaning failure") },
/*      R       */{SST(0x30, 0x08, SS_RDEF,
			"Cannot write - application code mismatch") },
/*      R       */{SST(0x30, 0x09, SS_RDEF,
			"Current session not fixated for append") },
/* DT  WR O     */{SST(0x31, 0x00, SS_RDEF,
			"Medium format corrupted") },
/* D L  R O     */{SST(0x31, 0x01, SS_RDEF,
			"Format command failed") },
/* D   W  O     */{SST(0x32, 0x00, SS_RDEF,
			"No defect spare location available") },
/* D   W  O     */{SST(0x32, 0x01, SS_RDEF,
			"Defect list update failure") },
/*  T           */{SST(0x33, 0x00, SS_RDEF,
			"Tape length error") },
/* DTLPWRSOMCAE */{SST(0x34, 0x00, SS_RDEF,
			"Enclosure failure") },
/* DTLPWRSOMCAE */{SST(0x35, 0x00, SS_RDEF,
			"Enclosure services failure") },
/* DTLPWRSOMCAE */{SST(0x35, 0x01, SS_RDEF,
			"Unsupported enclosure function") },
/* DTLPWRSOMCAE */{SST(0x35, 0x02, SS_RDEF,
			"Enclosure services unavailable") },
/* DTLPWRSOMCAE */{SST(0x35, 0x03, SS_RDEF,
			"Enclosure services transfer failure") },
/* DTLPWRSOMCAE */{SST(0x35, 0x04, SS_RDEF,
			"Enclosure services transfer refused") },
/*   L          */{SST(0x36, 0x00, SS_RDEF,
			"Ribbon, ink, or toner failure") },
/* DTL WRSOMCAE */{SST(0x37, 0x00, SS_RDEF,
			"Rounded parameter") },
/* DTL WRSOMCAE */{SST(0x39, 0x00, SS_RDEF,
			"Saving parameters not supported") },
/* DTL WRSOM    */{SST(0x3A, 0x00, SS_NOP,
			"Medium not present") },
/* DT  WR OM    */{SST(0x3A, 0x01, SS_NOP,
			"Medium not present - tray closed") },
/* DT  WR OM    */{SST(0x3A, 0x01, SS_NOP,
			"Medium not present - tray open") },
/* DT  WR OM    */{SST(0x3A, 0x03, SS_NOP,
			"Medium not present - Loadable") },
/* DT  WR OM    */{SST(0x3A, 0x04, SS_NOP,
			"Medium not present - medium auxiliary "
			"memory accessible") },
/* DT  WR OM    */{SST(0x3A, 0xFF, SS_NOP, NULL) },/* Range 0x05->0xFF */
/*  TL          */{SST(0x3B, 0x00, SS_RDEF,
			"Sequential positioning error") },
/*  T           */{SST(0x3B, 0x01, SS_RDEF,
			"Tape position error at beginning-of-medium") },
/*  T           */{SST(0x3B, 0x02, SS_RDEF,
			"Tape position error at end-of-medium") },
/*   L          */{SST(0x3B, 0x03, SS_RDEF,
			"Tape or electronic vertical forms unit not ready") },
/*   L          */{SST(0x3B, 0x04, SS_RDEF,
			"Slew failure") },
/*   L          */{SST(0x3B, 0x05, SS_RDEF,
			"Paper jam") },
/*   L          */{SST(0x3B, 0x06, SS_RDEF,
			"Failed to sense top-of-form") },
/*   L          */{SST(0x3B, 0x07, SS_RDEF,
			"Failed to sense bottom-of-form") },
/*  T           */{SST(0x3B, 0x08, SS_RDEF,
			"Reposition error") },
/*       S      */{SST(0x3B, 0x09, SS_RDEF,
			"Read past end of medium") },
/*       S      */{SST(0x3B, 0x0A, SS_RDEF,
			"Read past beginning of medium") },
/*       S      */{SST(0x3B, 0x0B, SS_RDEF,
			"Position past end of medium") },
/*  T    S      */{SST(0x3B, 0x0C, SS_RDEF,
			"Position past beginning of medium") },
/* DT  WR OM    */{SST(0x3B, 0x0D, SS_FATAL|ENOSPC,
			"Medium destination element full") },
/* DT  WR OM    */{SST(0x3B, 0x0E, SS_RDEF,
			"Medium source element empty") },
/*      R       */{SST(0x3B, 0x0F, SS_RDEF,
			"End of medium reached") },
/* DT  WR OM    */{SST(0x3B, 0x11, SS_RDEF,
			"Medium magazine not accessible") },
/* DT  WR OM    */{SST(0x3B, 0x12, SS_RDEF,
			"Medium magazine removed") },
/* DT  WR OM    */{SST(0x3B, 0x13, SS_RDEF,
			"Medium magazine inserted") },
/* DT  WR OM    */{SST(0x3B, 0x14, SS_RDEF,
			"Medium magazine locked") },
/* DT  WR OM    */{SST(0x3B, 0x15, SS_RDEF,
			"Medium magazine unlocked") },
/* DTLPWRSOMCAE */{SST(0x3D, 0x00, SS_RDEF,
			"Invalid bits in identify message") },
/* DTLPWRSOMCAE */{SST(0x3E, 0x00, SS_RDEF,
			"Logical unit has not self-configured yet") },
/* DTLPWRSOMCAE */{SST(0x3E, 0x01, SS_RDEF,
			"Logical unit failure") },
/* DTLPWRSOMCAE */{SST(0x3E, 0x02, SS_RDEF,
			"Timeout on logical unit") },
/* DTLPWRSOMCAE */{SST(0x3F, 0x00, SS_RDEF,
			"Target operating conditions have changed") },
/* DTLPWRSOMCAE */{SST(0x3F, 0x01, SS_RDEF,
			"Microcode has been changed") },
/* DTLPWRSOMC   */{SST(0x3F, 0x02, SS_RDEF,
			"Changed operating definition") },
/* DTLPWRSOMCAE */{SST(0x3F, 0x03, SS_INQ_REFRESH|SSQ_DECREMENT_COUNT,
			"Inquiry data has changed") },
/* DT  WR OMCAE */{SST(0x3F, 0x04, SS_RDEF,
			"Component device attached") },
/* DT  WR OMCAE */{SST(0x3F, 0x05, SS_RDEF,
			"Device identifier changed") },
/* DT  WR OMCAE */{SST(0x3F, 0x06, SS_RDEF,
			"Redundancy group created or modified") },
/* DT  WR OMCAE */{SST(0x3F, 0x07, SS_RDEF,
			"Redundancy group deleted") },
/* DT  WR OMCAE */{SST(0x3F, 0x08, SS_RDEF,
			"Spare created or modified") },
/* DT  WR OMCAE */{SST(0x3F, 0x09, SS_RDEF,
			"Spare deleted") },
/* DT  WR OMCAE */{SST(0x3F, 0x0A, SS_RDEF,
			"Volume set created or modified") },
/* DT  WR OMCAE */{SST(0x3F, 0x0B, SS_RDEF,
			"Volume set deleted") },
/* DT  WR OMCAE */{SST(0x3F, 0x0C, SS_RDEF,
			"Volume set deassigned") },
/* DT  WR OMCAE */{SST(0x3F, 0x0D, SS_RDEF,
			"Volume set reassigned") },
/* DTLPWRSOMCAE */{SST(0x3F, 0x0E, SS_RDEF,
			"Reported luns data has changed") },
/* DTLPWRSOMCAE */{SST(0x3F, 0x0F, SS_RETRY|SSQ_DECREMENT_COUNT
				 | SSQ_DELAY_RANDOM|EBUSY,
			"Echo buffer overwritten") },
/* DT  WR OM   B*/{SST(0x3F, 0x0F, SS_RDEF, "Medium Loadable") },
/* DT  WR OM   B*/{SST(0x3F, 0x0F, SS_RDEF,
			"Medium auxiliary memory accessible") },
/* D            */{SST(0x40, 0x00, SS_RDEF,
			"Ram failure") }, /* deprecated - use 40 NN instead */
/* DTLPWRSOMCAE */{SST(0x40, 0x80, SS_RDEF,
			"Diagnostic failure: ASCQ = Component ID") },
/* DTLPWRSOMCAE */{SST(0x40, 0xFF, SS_RDEF|SSQ_RANGE,
			NULL) },/* Range 0x80->0xFF */
/* D            */{SST(0x41, 0x00, SS_RDEF,
			"Data path failure") }, /* deprecated - use 40 NN instead */
/* D            */{SST(0x42, 0x00, SS_RDEF,
			"Power-on or self-test failure") }, /* deprecated - use 40 NN instead */
/* DTLPWRSOMCAE */{SST(0x43, 0x00, SS_RDEF,
			"Message error") },
/* DTLPWRSOMCAE */{SST(0x44, 0x00, SS_RDEF,
			"Internal target failure") },
/* DTLPWRSOMCAE */{SST(0x45, 0x00, SS_RDEF,
			"Select or reselect failure") },
/* DTLPWRSOMC   */{SST(0x46, 0x00, SS_RDEF,
			"Unsuccessful soft reset") },
/* DTLPWRSOMCAE */{SST(0x47, 0x00, SS_RDEF|SSQ_FALLBACK,
			"SCSI parity error") },
/* DTLPWRSOMCAE */{SST(0x47, 0x01, SS_RDEF|SSQ_FALLBACK,
			"Data Phase CRC error detected") },
/* DTLPWRSOMCAE */{SST(0x47, 0x02, SS_RDEF|SSQ_FALLBACK,
			"SCSI parity error detected during ST data phase") },
/* DTLPWRSOMCAE */{SST(0x47, 0x03, SS_RDEF|SSQ_FALLBACK,
			"Information Unit iuCRC error") },
/* DTLPWRSOMCAE */{SST(0x47, 0x04, SS_RDEF|SSQ_FALLBACK,
			"Asynchronous information protection error detected") },
/* DTLPWRSOMCAE */{SST(0x47, 0x05, SS_RDEF|SSQ_FALLBACK,
			"Protocol server CRC error") },
/* DTLPWRSOMCAE */{SST(0x48, 0x00, SS_RDEF|SSQ_FALLBACK,
			"Initiator detected error message received") },
/* DTLPWRSOMCAE */{SST(0x49, 0x00, SS_RDEF,
			"Invalid message error") },
/* DTLPWRSOMCAE */{SST(0x4A, 0x00, SS_RDEF,
			"Command phase error") },
/* DTLPWRSOMCAE */{SST(0x4B, 0x00, SS_RDEF,
			"Data phase error") },
/* DTLPWRSOMCAE */{SST(0x4C, 0x00, SS_RDEF,
			"Logical unit failed self-configuration") },
/* DTLPWRSOMCAE */{SST(0x4D, 0x00, SS_RDEF,
			"Tagged overlapped commands: ASCQ = Queue tag ID") },
/* DTLPWRSOMCAE */{SST(0x4D, 0xFF, SS_RDEF|SSQ_RANGE,
			NULL)}, /* Range 0x00->0xFF */
/* DTLPWRSOMCAE */{SST(0x4E, 0x00, SS_RDEF,
			"Overlapped commands attempted") },
/*  T           */{SST(0x50, 0x00, SS_RDEF,
			"Write append error") },
/*  T           */{SST(0x50, 0x01, SS_RDEF,
			"Write append position error") },
/*  T           */{SST(0x50, 0x02, SS_RDEF,
			"Position error related to timing") },
/*  T     O     */{SST(0x51, 0x00, SS_RDEF,
			"Erase failure") },
/*  T           */{SST(0x52, 0x00, SS_RDEF,
			"Cartridge fault") },
/* DTL WRSOM    */{SST(0x53, 0x00, SS_RDEF,
			"Media load or eject failed") },
/*  T           */{SST(0x53, 0x01, SS_RDEF,
			"Unload tape failure") },
/* DT  WR OM    */{SST(0x53, 0x02, SS_RDEF,
			"Medium removal prevented") },
/*    P         */{SST(0x54, 0x00, SS_RDEF,
			"Scsi to host system interface failure") },
/*    P         */{SST(0x55, 0x00, SS_RDEF,
			"System resource failure") },
/* D      O     */{SST(0x55, 0x01, SS_FATAL|ENOSPC,
			"System buffer full") },
/*      R       */{SST(0x57, 0x00, SS_RDEF,
			"Unable to recover table-of-contents") },
/*        O     */{SST(0x58, 0x00, SS_RDEF,
			"Generation does not exist") },
/*        O     */{SST(0x59, 0x00, SS_RDEF,
			"Updated block read") },
/* DTLPWRSOM    */{SST(0x5A, 0x00, SS_RDEF,
			"Operator request or state change input") },
/* DT  WR OM    */{SST(0x5A, 0x01, SS_RDEF,
			"Operator medium removal request") },
/* DT  W  O     */{SST(0x5A, 0x02, SS_RDEF,
			"Operator selected write protect") },
/* DT  W  O     */{SST(0x5A, 0x03, SS_RDEF,
			"Operator selected write permit") },
/* DTLPWRSOM    */{SST(0x5B, 0x00, SS_RDEF,
			"Log exception") },
/* DTLPWRSOM    */{SST(0x5B, 0x01, SS_RDEF,
			"Threshold condition met") },
/* DTLPWRSOM    */{SST(0x5B, 0x02, SS_RDEF,
			"Log counter at maximum") },
/* DTLPWRSOM    */{SST(0x5B, 0x03, SS_RDEF,
			"Log list codes exhausted") },
/* D      O     */{SST(0x5C, 0x00, SS_RDEF,
			"RPL status change") },
/* D      O     */{SST(0x5C, 0x01, SS_NOP|SSQ_PRINT_SENSE,
			"Spindles synchronized") },
/* D      O     */{SST(0x5C, 0x02, SS_RDEF,
			"Spindles not synchronized") },
/* DTLPWRSOMCAE */{SST(0x5D, 0x00, SS_RDEF,
			"Failure prediction threshold exceeded") },
/* DTLPWRSOMCAE */{SST(0x5D, 0xFF, SS_RDEF,
			"Failure prediction threshold exceeded (false)") },
/* DTLPWRSO CA  */{SST(0x5E, 0x00, SS_RDEF,
			"Low power condition on") },
/* DTLPWRSO CA  */{SST(0x5E, 0x01, SS_RDEF,
			"Idle condition activated by timer") },
/* DTLPWRSO CA  */{SST(0x5E, 0x02, SS_RDEF,
			"Standby condition activated by timer") },
/* DTLPWRSO CA  */{SST(0x5E, 0x03, SS_RDEF,
			"Idle condition activated by command") },
/* DTLPWRSO CA  */{SST(0x5E, 0x04, SS_RDEF,
			"Standby condition activated by command") },
/*       S      */{SST(0x60, 0x00, SS_RDEF,
			"Lamp failure") },
/*       S      */{SST(0x61, 0x00, SS_RDEF,
			"Video acquisition error") },
/*       S      */{SST(0x61, 0x01, SS_RDEF,
			"Unable to acquire video") },
/*       S      */{SST(0x61, 0x02, SS_RDEF,
			"Out of focus") },
/*       S      */{SST(0x62, 0x00, SS_RDEF,
			"Scan head positioning error") },
/*      R       */{SST(0x63, 0x00, SS_RDEF,
			"End of user area encountered on this track") },
/*      R       */{SST(0x63, 0x01, SS_FATAL|ENOSPC,
			"Packet does not fit in available space") },
/*      R       */{SST(0x64, 0x00, SS_RDEF,
			"Illegal mode for this track") },
/*      R       */{SST(0x64, 0x01, SS_RDEF,
			"Invalid packet size") },
/* DTLPWRSOMCAE */{SST(0x65, 0x00, SS_RDEF,
			"Voltage fault") },
/*       S      */{SST(0x66, 0x00, SS_RDEF,
			"Automatic document feeder cover up") },
/*       S      */{SST(0x66, 0x01, SS_RDEF,
			"Automatic document feeder lift up") },
/*       S      */{SST(0x66, 0x02, SS_RDEF,
			"Document jam in automatic document feeder") },
/*       S      */{SST(0x66, 0x03, SS_RDEF,
			"Document miss feed automatic in document feeder") },
/*           A  */{SST(0x67, 0x00, SS_RDEF,
			"Configuration failure") },
/*           A  */{SST(0x67, 0x01, SS_RDEF,
			"Configuration of incapable logical units failed") },
/*           A  */{SST(0x67, 0x02, SS_RDEF,
			"Add logical unit failed") },
/*           A  */{SST(0x67, 0x03, SS_RDEF,
			"Modification of logical unit failed") },
/*           A  */{SST(0x67, 0x04, SS_RDEF,
			"Exchange of logical unit failed") },
/*           A  */{SST(0x67, 0x05, SS_RDEF,
			"Remove of logical unit failed") },
/*           A  */{SST(0x67, 0x06, SS_RDEF,
			"Attachment of logical unit failed") },
/*           A  */{SST(0x67, 0x07, SS_RDEF,
			"Creation of logical unit failed") },
/*           A  */{SST(0x68, 0x00, SS_RDEF,
			"Logical unit not configured") },
/*           A  */{SST(0x69, 0x00, SS_RDEF,
			"Data loss on logical unit") },
/*           A  */{SST(0x69, 0x01, SS_RDEF,
			"Multiple logical unit failures") },
/*           A  */{SST(0x69, 0x02, SS_RDEF,
			"Parity/data mismatch") },
/*           A  */{SST(0x6A, 0x00, SS_RDEF,
			"Informational, refer to log") },
/*           A  */{SST(0x6B, 0x00, SS_RDEF,
			"State change has occurred") },
/*           A  */{SST(0x6B, 0x01, SS_RDEF,
			"Redundancy level got better") },
/*           A  */{SST(0x6B, 0x02, SS_RDEF,
			"Redundancy level got worse") },
/*           A  */{SST(0x6C, 0x00, SS_RDEF,
			"Rebuild failure occurred") },
/*           A  */{SST(0x6D, 0x00, SS_RDEF,
			"Recalculate failure occurred") },
/*           A  */{SST(0x6E, 0x00, SS_RDEF,
			"Command to logical unit failed") },
/*  T           */{SST(0x70, 0x00, SS_RDEF,
			"Decompression exception short: ASCQ = Algorithm ID") },
/*  T           */{SST(0x70, 0xFF, SS_RDEF|SSQ_RANGE,
			NULL) }, /* Range 0x00 -> 0xFF */
/*  T           */{SST(0x71, 0x00, SS_RDEF,
			"Decompression exception long: ASCQ = Algorithm ID") },
/*  T           */{SST(0x71, 0xFF, SS_RDEF|SSQ_RANGE,
			NULL) }, /* Range 0x00 -> 0xFF */	
/*      R       */{SST(0x72, 0x00, SS_RDEF,
			"Session fixation error") },
/*      R       */{SST(0x72, 0x01, SS_RDEF,
			"Session fixation error writing lead-in") },
/*      R       */{SST(0x72, 0x02, SS_RDEF,
			"Session fixation error writing lead-out") },
/*      R       */{SST(0x72, 0x03, SS_RDEF,
			"Session fixation error - incomplete track in session") },
/*      R       */{SST(0x72, 0x04, SS_RDEF,
			"Empty or partially written reserved track") },
/*      R       */{SST(0x73, 0x00, SS_RDEF,
			"CD control error") },
/*      R       */{SST(0x73, 0x01, SS_RDEF,
			"Power calibration area almost full") },
/*      R       */{SST(0x73, 0x02, SS_FATAL|ENOSPC,
			"Power calibration area is full") },
/*      R       */{SST(0x73, 0x03, SS_RDEF,
			"Power calibration area error") },
/*      R       */{SST(0x73, 0x04, SS_RDEF,
			"Program memory area update failure") },
/*      R       */{SST(0x73, 0x05, SS_RDEF,
			"program memory area is full") }
};

static const int asc_table_size = sizeof(asc_table)/sizeof(asc_table[0]);

struct asc_key
{
	int asc;
	int ascq;
};

static int
ascentrycomp(const void *key, const void *member)
{
	int asc;
	int ascq;
	const struct asc_table_entry *table_entry;

	asc = ((const struct asc_key *)key)->asc;
	ascq = ((const struct asc_key *)key)->ascq;
	table_entry = (const struct asc_table_entry *)member;

	if (asc >= table_entry->asc) {

		if (asc > table_entry->asc)
			return (1);

		if (ascq <= table_entry->ascq) {
			/* Check for ranges */
			if (ascq == table_entry->ascq
		 	 || ((table_entry->action & SSQ_RANGE) != 0
		  	   && ascq >= (table_entry - 1)->ascq))
				return (0);
			return (-1);
		}
		return (1);
	}
	return (-1);
}

static int
senseentrycomp(const void *key, const void *member)
{
	int sense_key;
	const struct sense_key_table_entry *table_entry;

	sense_key = *((const int *)key);
	table_entry = (const struct sense_key_table_entry *)member;

	if (sense_key >= table_entry->sense_key) {
		if (sense_key == table_entry->sense_key)
			return (0);
		return (1);
	}
	return (-1);
}

static void
fetchtableentries(int sense_key, int asc, int ascq,
		  struct scsi_inquiry_data *inq_data,
		  const struct sense_key_table_entry **sense_entry,
		  const struct asc_table_entry **asc_entry)
{
	void *match;
	const struct asc_table_entry *asc_tables[2];
	const struct sense_key_table_entry *sense_tables[2];
	struct asc_key asc_ascq;
	size_t asc_tables_size[2];
	size_t sense_tables_size[2];
	int num_asc_tables;
	int num_sense_tables;
	int i;

	/* Default to failure */
	*sense_entry = NULL;
	*asc_entry = NULL;
	match = NULL;
	if (inq_data != NULL)
		match = cam_quirkmatch((void *)inq_data,
				       (void *)sense_quirk_table,
				       sense_quirk_table_size,
				       sizeof(*sense_quirk_table),
				       aic_inquiry_match);

	if (match != NULL) {
		struct scsi_sense_quirk_entry *quirk;

		quirk = (struct scsi_sense_quirk_entry *)match;
		asc_tables[0] = quirk->asc_info;
		asc_tables_size[0] = quirk->num_ascs;
		asc_tables[1] = asc_table;
		asc_tables_size[1] = asc_table_size;
		num_asc_tables = 2;
		sense_tables[0] = quirk->sense_key_info;
		sense_tables_size[0] = quirk->num_sense_keys;
		sense_tables[1] = sense_key_table;
		sense_tables_size[1] = sense_key_table_size;
		num_sense_tables = 2;
	} else {
		asc_tables[0] = asc_table;
		asc_tables_size[0] = asc_table_size;
		num_asc_tables = 1;
		sense_tables[0] = sense_key_table;
		sense_tables_size[0] = sense_key_table_size;
		num_sense_tables = 1;
	}

	asc_ascq.asc = asc;
	asc_ascq.ascq = ascq;
	for (i = 0; i < num_asc_tables; i++) {
		void *found_entry;

		found_entry = scsibsearch(&asc_ascq, asc_tables[i],
					  asc_tables_size[i],
					  sizeof(**asc_tables),
					  ascentrycomp);

		if (found_entry) {
			*asc_entry = (struct asc_table_entry *)found_entry;
			break;
		}
	}

	for (i = 0; i < num_sense_tables; i++) {
		void *found_entry;

		found_entry = scsibsearch(&sense_key, sense_tables[i],
					  sense_tables_size[i],
					  sizeof(**sense_tables),
					  senseentrycomp);

		if (found_entry) {
			*sense_entry =
			    (struct sense_key_table_entry *)found_entry;
			break;
		}
	}
}

static void *
scsibsearch(const void *key, const void *base, size_t nmemb, size_t size,
		 int (*compar)(const void *, const void *))
{
	const void *entry;
	u_int l;
	u_int u;
	u_int m;

	l = -1;
	u = nmemb;
	while (l + 1 != u) {
		m = (l + u) / 2;
		entry = base + m * size;
		if (compar(key, entry) > 0)
			l = m;
		else
			u = m;
	}

	entry = base + u * size;
	if (u == nmemb
	 || compar(key, entry) != 0)
		return (NULL);

	return ((void *)entry);
}

/*
 * Compare string with pattern, returning 0 on match.
 * Short pattern matches trailing blanks in name,
 * wildcard '*' in pattern matches rest of name,
 * wildcard '?' matches a single non-space character.
 */
static int
cam_strmatch(const uint8_t *str, const uint8_t *pattern, int str_len)
{

	while (*pattern != '\0'&& str_len > 0) {  

		if (*pattern == '*') {
			return (0);
		}
		if ((*pattern != *str)
		 && (*pattern != '?' || *str == ' ')) {
			return (1);
		}
		pattern++;
		str++;
		str_len--;
	}
	while (str_len > 0 && *str++ == ' ')
		str_len--;

	return (str_len);
}

static caddr_t
cam_quirkmatch(caddr_t target, caddr_t quirk_table, int num_entries,
	       int entry_size, cam_quirkmatch_t *comp_func)
{
	for (; num_entries > 0; num_entries--, quirk_table += entry_size) {
		if ((*comp_func)(target, quirk_table) == 0)
			return (quirk_table);
	}
	return (NULL);
}

void
aic_sense_desc(int sense_key, int asc, int ascq,
	       struct scsi_inquiry_data *inq_data,
	       const char **sense_key_desc, const char **asc_desc)
{
	const struct asc_table_entry *asc_entry;
	const struct sense_key_table_entry *sense_entry;

	fetchtableentries(sense_key, asc, ascq,
			  inq_data,
			  &sense_entry,
			  &asc_entry);

	*sense_key_desc = sense_entry->desc;

	if (asc_entry != NULL)
		*asc_desc = asc_entry->desc;
	else if (asc >= 0x80 && asc <= 0xff)
		*asc_desc = "Vendor Specific ASC";
	else if (ascq >= 0x80 && ascq <= 0xff)
		*asc_desc = "Vendor Specific ASCQ";
	else
		*asc_desc = "Reserved ASC/ASCQ pair";
}

/*
 * Given sense and device type information, return the appropriate action.
 * If we do not understand the specific error as identified by the ASC/ASCQ
 * pair, fall back on the more generic actions derived from the sense key.
 */
aic_sense_action
aic_sense_error_action(struct scsi_sense_data *sense_data,
		       struct scsi_inquiry_data *inq_data, uint32_t sense_flags)
{
	const struct asc_table_entry *asc_entry;
	const struct sense_key_table_entry *sense_entry;
	int error_code, sense_key, asc, ascq;
	aic_sense_action action;

	scsi_extract_sense(sense_data, &error_code, &sense_key, &asc, &ascq);

	if (error_code == SSD_DEFERRED_ERROR) {
		/*
		 * XXX dufault@FreeBSD.org
		 * This error doesn't relate to the command associated
		 * with this request sense.  A deferred error is an error
		 * for a command that has already returned GOOD status
		 * (see SCSI2 8.2.14.2).
		 *
		 * By my reading of that section, it looks like the current
		 * command has been cancelled, we should now clean things up
		 * (hopefully recovering any lost data) and then retry the
		 * current command.  There are two easy choices, both wrong:
		 *
		 * 1. Drop through (like we had been doing), thus treating
		 *    this as if the error were for the current command and
		 *    return and stop the current command.
		 * 
		 * 2. Issue a retry (like I made it do) thus hopefully
		 *    recovering the current transfer, and ignoring the
		 *    fact that we've dropped a command.
		 *
		 * These should probably be handled in a device specific
		 * sense handler or punted back up to a user mode daemon
		 */
		action = SS_RETRY|SSQ_DECREMENT_COUNT|SSQ_PRINT_SENSE;
	} else {
		fetchtableentries(sense_key, asc, ascq,
				  inq_data,
				  &sense_entry,
				  &asc_entry);

		/*
		 * Override the 'No additional Sense' entry (0,0)
		 * with the error action of the sense key.
		 */
		if (asc_entry != NULL
		 && (asc != 0 || ascq != 0))
			action = asc_entry->action;
		else
			action = sense_entry->action;

		if (sense_key == SSD_KEY_RECOVERED_ERROR) {
			/*
			 * The action succeeded but the device wants
			 * the user to know that some recovery action
			 * was required.
			 */
			action &= ~(SS_MASK|SSQ_MASK|SS_ERRMASK);
			action |= SS_NOP|SSQ_PRINT_SENSE;
		} else if (sense_key == SSD_KEY_ILLEGAL_REQUEST) {
			if ((sense_flags & SF_QUIET_IR) != 0)
				action &= ~SSQ_PRINT_SENSE;
		} else if (sense_key == SSD_KEY_UNIT_ATTENTION) {
			if ((sense_flags & SF_RETRY_UA) != 0
			 && (action & SS_MASK) == SS_FAIL) {
				action &= ~(SS_MASK|SSQ_MASK);
				action |= SS_RETRY|SSQ_DECREMENT_COUNT|
					  SSQ_PRINT_SENSE;
			}
		}
	}

	if ((sense_flags & SF_PRINT_ALWAYS) != 0)
		action |= SSQ_PRINT_SENSE;
	else if ((sense_flags & SF_NO_PRINT) != 0)
		action &= ~SSQ_PRINT_SENSE;

	return (action);
}

/*      
 * Try make as good a match as possible with
 * available sub drivers
 */
int
aic_inquiry_match(caddr_t inqbuffer, caddr_t table_entry)
{
	struct scsi_inquiry_pattern *entry;
	struct scsi_inquiry_data *inq;
 
	entry = (struct scsi_inquiry_pattern *)table_entry;
	inq = (struct scsi_inquiry_data *)inqbuffer;

	if (((SID_TYPE(inq) == entry->type)
	  || (entry->type == T_ANY))
	 && (SID_IS_REMOVABLE(inq) ? entry->media_type & SIP_MEDIA_REMOVABLE
				   : entry->media_type & SIP_MEDIA_FIXED)
	 && (cam_strmatch(inq->vendor, entry->vendor, sizeof(inq->vendor)) == 0)
	 && (cam_strmatch(inq->product, entry->product,
			  sizeof(inq->product)) == 0)
	 && (cam_strmatch(inq->revision, entry->revision,
			  sizeof(inq->revision)) == 0)) {
		return (0);
	}
        return (-1);
}

/*
 * Table of syncrates that don't follow the "divisible by 4"
 * rule. This table will be expanded in future SCSI specs.
 */
static struct {
	u_int period_factor;
	u_int period;	/* in 100ths of ns */
} scsi_syncrates[] = {
	{ 0x08, 625 },	/* FAST-160 */
	{ 0x09, 1250 },	/* FAST-80 */
	{ 0x0a, 2500 },	/* FAST-40 40MHz */
	{ 0x0b, 3030 },	/* FAST-40 33MHz */
	{ 0x0c, 5000 }	/* FAST-20 */
};

/*
 * Return the frequency in kHz corresponding to the given
 * sync period factor.
 */
u_int
aic_calc_syncsrate(u_int period_factor)
{
	int i;
	int num_syncrates;

	num_syncrates = sizeof(scsi_syncrates) / sizeof(scsi_syncrates[0]);
	/* See if the period is in the "exception" table */
	for (i = 0; i < num_syncrates; i++) {

		if (period_factor == scsi_syncrates[i].period_factor) {
			/* Period in kHz */
			return (100000000 / scsi_syncrates[i].period);
		}
	}

	/*
	 * Wasn't in the table, so use the standard
	 * 4 times conversion.
	 */
	return (10000000 / (period_factor * 4 * 10));
}

/*
 * Return speed in KB/s.
 */
u_int
aic_calc_speed(u_int width, u_int period, u_int offset, u_int min_rate)
{
	u_int freq;

	if (offset != 0 && period < min_rate)
		freq  = aic_calc_syncsrate(period);
	else
		/* Roughly 3.3MB/s for async */
		freq  = 3300;
	freq <<= width;
	return (freq);
}

uint32_t
aic_error_action(struct scsi_cmnd *cmd, struct scsi_inquiry_data *inq_data,
		 cam_status status, u_int scsi_status)
{
	aic_sense_action  err_action;
	int		  sense;

	sense  = (cmd->result >> 24) == DRIVER_SENSE;

	switch (status) {
	case CAM_REQ_CMP:
		err_action = SS_NOP;
		break;
	case CAM_AUTOSENSE_FAIL:
	case CAM_SCSI_STATUS_ERROR:

		switch (scsi_status) {
		case SCSI_STATUS_OK:
		case SCSI_STATUS_COND_MET:
		case SCSI_STATUS_INTERMED:
		case SCSI_STATUS_INTERMED_COND_MET:
			err_action = SS_NOP;
			break;
		case SCSI_STATUS_CMD_TERMINATED:
		case SCSI_STATUS_CHECK_COND:
			if (sense != 0) {
				struct scsi_sense_data *sense;

				sense = (struct scsi_sense_data *)
				    &cmd->sense_buffer;
				err_action =
				    aic_sense_error_action(sense, inq_data, 0);

			} else {
				err_action = SS_RETRY|SSQ_FALLBACK
					   | SSQ_DECREMENT_COUNT|EIO;
			}
			break;
		case SCSI_STATUS_QUEUE_FULL:
		case SCSI_STATUS_BUSY:
			err_action = SS_RETRY|SSQ_DELAY|SSQ_MANY
				   | SSQ_DECREMENT_COUNT|EBUSY;
			break;
		case SCSI_STATUS_RESERV_CONFLICT:
		default:
			err_action = SS_FAIL|EBUSY;
			break;
		}
		break;
	case CAM_CMD_TIMEOUT:
	case CAM_REQ_CMP_ERR:
	case CAM_UNEXP_BUSFREE:
	case CAM_UNCOR_PARITY:
	case CAM_DATA_RUN_ERR:
		err_action = SS_RETRY|SSQ_FALLBACK|EIO;
		break;
	case CAM_UA_ABORT:
	case CAM_UA_TERMIO:
	case CAM_MSG_REJECT_REC:
	case CAM_SEL_TIMEOUT:
		err_action = SS_FAIL|EIO;
		break;
	case CAM_REQ_INVALID:
	case CAM_PATH_INVALID:
	case CAM_DEV_NOT_THERE:
	case CAM_NO_HBA:
	case CAM_PROVIDE_FAIL:
	case CAM_REQ_TOO_BIG:		
	case CAM_RESRC_UNAVAIL:
	case CAM_BUSY:
	default:
		/* panic??  These should never occur in our application. */
		err_action = SS_FAIL|EIO;
		break;
	case CAM_SCSI_BUS_RESET:
	case CAM_BDR_SENT:		
	case CAM_REQUEUE_REQ:
		/* Unconditional requeue */
		err_action = SS_RETRY;
		break;
	}

	return (err_action);
}

char *
aic_parse_brace_option(char *opt_name, char *opt_arg, char *end, int depth,
		       aic_option_callback_t *callback, u_long callback_arg)
{
	char	*tok_end;
	char	*tok_end2;
	int      i;
	int      instance;
	int	 targ;
	int	 done;
	char	 tok_list[] = {'.', ',', '{', '}', '\0'};

	/* All options use a ':' name/arg separator */
	if (*opt_arg != ':')
		return (opt_arg);
	opt_arg++;
	instance = -1;
	targ = -1;
	done = FALSE;
	/*
	 * Restore separator that may be in
	 * the middle of our option argument.
	 */
	tok_end = strchr(opt_arg, '\0');
	if (tok_end < end)
		*tok_end = ',';
	while (!done) {
		switch (*opt_arg) {
		case '{':
			if (instance == -1) {
				instance = 0;
			} else {
				if (depth > 1) {
					if (targ == -1)
						targ = 0;
				} else {
					printf("Malformed Option %s\n",
					       opt_name);
					done = TRUE;
				}
			}
			opt_arg++;
			break;
		case '}':
			if (targ != -1)
				targ = -1;
			else if (instance != -1)
				instance = -1;
			opt_arg++;
			break;
		case ',':
		case '.':
			if (instance == -1)
				done = TRUE;
			else if (targ >= 0)
				targ++;
			else if (instance >= 0)
				instance++;
			opt_arg++;
			break;
		case '\0':
			done = TRUE;
			break;
		default:
			tok_end = end;
			for (i = 0; tok_list[i]; i++) {
				tok_end2 = strchr(opt_arg, tok_list[i]);
				if ((tok_end2) && (tok_end2 < tok_end))
					tok_end = tok_end2;
			}
			callback(callback_arg, instance, targ,
				 simple_strtol(opt_arg, NULL, 0));
			opt_arg = tok_end;
			break;
		}
	}
	return (opt_arg);
}
