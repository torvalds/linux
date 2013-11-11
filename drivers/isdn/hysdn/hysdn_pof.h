/* $Id: hysdn_pof.h,v 1.2.6.1 2001/09/23 22:24:54 kai Exp $
 *
 * Linux driver for HYSDN cards, definitions used for handling pof-files.
 *
 * Author    Werner Cornelius (werner@titro.de) for Hypercope GmbH
 * Copyright 1999 by Werner Cornelius (werner@titro.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

/************************/
/* POF specific defines */
/************************/
#define BOOT_BUF_SIZE   0x1000	/* =4096, maybe moved to other h file */
#define CRYPT_FEEDTERM  0x8142
#define CRYPT_STARTTERM 0x81a5
/*  max. timeout time in seconds
 *  from end of booting to POF is ready
 */
#define POF_READY_TIME_OUT_SEC  10

/**********************************/
/* defines for 1.stage boot image */
/**********************************/

/*  the POF file record containing the boot loader image
 *  has 2 pages a 16KB:
 *  1. page contains the high 16-bit part of the 32-bit E1 words
 *  2. page contains the low  16-bit part of the 32-bit E1 words
 *
 *  In each 16KB page we assume the start of the boot loader code
 *  in the highest 2KB part (at offset 0x3800);
 *  the rest (0x0000..0x37FF) is assumed to contain 0 bytes.
 */

#define POF_BOOT_LOADER_PAGE_SIZE   0x4000	/* =16384U */
#define POF_BOOT_LOADER_TOTAL_SIZE  (2U * POF_BOOT_LOADER_PAGE_SIZE)

#define POF_BOOT_LOADER_CODE_SIZE   0x0800	/* =2KB =2048U */

/* offset in boot page, where loader code may start */
/* =0x3800= 14336U */
#define POF_BOOT_LOADER_OFF_IN_PAGE (POF_BOOT_LOADER_PAGE_SIZE-POF_BOOT_LOADER_CODE_SIZE)


/*--------------------------------------POF file record structs------------*/
typedef struct PofFileHdr_tag {	/* Pof file header */
	/*00 */ unsigned long Magic __attribute__((packed));
	/*04 */ unsigned long N_PofRecs __attribute__((packed));
/*08 */
} tPofFileHdr;

typedef struct PofRecHdr_tag {	/* Pof record header */
	/*00 */ unsigned short PofRecId __attribute__((packed));
	/*02 */ unsigned long PofRecDataLen __attribute__((packed));
/*06 */
} tPofRecHdr;

typedef struct PofTimeStamp_tag {
	/*00 */ unsigned long UnixTime __attribute__((packed));
	/*04 */ unsigned char DateTimeText[0x28];
	/* =40 */
/*2C */
} tPofTimeStamp;

/* tPofFileHdr.Magic value: */
#define TAGFILEMAGIC 0x464F501AUL
/* tPofRecHdr.PofRecId values: */
#define TAG_ABSDATA  0x1000	/* abs. data */
#define TAG_BOOTDTA  0x1001	/* boot data */
#define TAG_COMMENT  0x0020
#define TAG_SYSCALL  0x0021
#define TAG_FLOWCTRL 0x0022
#define TAG_TIMESTMP 0x0010	/* date/time stamp of version */
#define TAG_CABSDATA 0x1100	/* crypted abs. data */
#define TAG_CBOOTDTA 0x1101	/* crypted boot data */
