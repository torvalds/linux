/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Broadcom USB remote download definitions
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: usbrdl.h 597933 2015-11-06 18:52:06Z $
 */

#ifndef _USB_RDL_H
#define _USB_RDL_H

/* Control messages: bRequest values */
#define DL_GETSTATE		0	/* returns the rdl_state_t struct */
#define DL_CHECK_CRC		1	/* currently unused */
#define DL_GO			2	/* execute downloaded image */
#define DL_START		3	/* initialize dl state */
#define DL_REBOOT		4	/* reboot the device in 2 seconds */
#define DL_GETVER		5	/* returns the bootrom_id_t struct */
#define DL_GO_PROTECTED		6	/* execute the downloaded code and set reset event
					 * to occur in 2 seconds.  It is the responsibility
					 * of the downloaded code to clear this event
					 */
#define DL_EXEC			7	/* jump to a supplied address */
#define DL_RESETCFG		8	/* To support single enum on dongle
					 * - Not used by bootloader
					 */
#define DL_DEFER_RESP_OK	9	/* Potentially defer the response to setup
					 * if resp unavailable
					 */
#define DL_CHGSPD		0x0A

#define	DL_HWCMD_MASK		0xfc	/* Mask for hardware read commands: */
#define	DL_RDHW			0x10	/* Read a hardware address (Ctl-in) */
#define	DL_RDHW32		0x10	/* Read a 32 bit word */
#define	DL_RDHW16		0x11	/* Read 16 bits */
#define	DL_RDHW8		0x12	/* Read an 8 bit byte */
#define	DL_WRHW			0x14	/* Write a hardware address (Ctl-out) */
#define DL_WRHW_BLK		0x13	/* Block write to hardware access */

#define DL_CMD_WRHW		2


/* states */
#define DL_WAITING	0	/* waiting to rx first pkt that includes the hdr info */
#define DL_READY	1	/* hdr was good, waiting for more of the compressed image */
#define DL_BAD_HDR	2	/* hdr was corrupted */
#define DL_BAD_CRC	3	/* compressed image was corrupted */
#define DL_RUNNABLE	4	/* download was successful, waiting for go cmd */
#define DL_START_FAIL	5	/* failed to initialize correctly */
#define DL_NVRAM_TOOBIG	6	/* host specified nvram data exceeds DL_NVRAM value */
#define DL_IMAGE_TOOBIG	7	/* download image too big (exceeds DATA_START for rdl) */

#define TIMEOUT		5000	/* Timeout for usb commands */

struct bcm_device_id {
	char	*name;
	uint32	vend;
	uint32	prod;
};

typedef struct {
	uint32	state;
	uint32	bytes;
} rdl_state_t;

typedef struct {
	uint32	chip;		/* Chip id */
	uint32	chiprev;	/* Chip rev */
	uint32  ramsize;    /* Size of RAM */
	uint32  remapbase;   /* Current remap base address */
	uint32  boardtype;   /* Type of board */
	uint32  boardrev;    /* Board revision */
} bootrom_id_t;

/* struct for backplane & jtag accesses */
typedef struct {
	uint32	cmd;		/* tag to identify the cmd */
	uint32	addr;		/* backplane address for write */
	uint32	len;		/* length of data: 1, 2, 4 bytes */
	uint32	data;		/* data to write */
} hwacc_t;


/* struct for querying nvram params from bootloader */
#define QUERY_STRING_MAX 32
typedef struct {
	uint32  cmd;                    /* tag to identify the cmd */
	char    var[QUERY_STRING_MAX];  /* param name */
} nvparam_t;

typedef void (*exec_fn_t)(void *sih);

#define USB_CTRL_IN (USB_TYPE_VENDOR | 0x80 | USB_RECIP_INTERFACE)
#define USB_CTRL_OUT (USB_TYPE_VENDOR | 0 | USB_RECIP_INTERFACE)

#define USB_CTRL_EP_TIMEOUT 500 /* Timeout used in USB control_msg transactions. */
#define USB_BULK_EP_TIMEOUT 500 /* Timeout used in USB bulk transactions. */

#define RDL_CHUNK_MAX	(64 * 1024)  /* max size of each dl transfer */
#define RDL_CHUNK	1500  /* size of each dl transfer */

/* bootloader makes special use of trx header "offsets" array */
#define TRX_OFFSETS_DLFWLEN_IDX	0	/* Size of the fw; used in uncompressed case */
#define TRX_OFFSETS_JUMPTO_IDX	1	/* RAM address for jumpto after download */
#define TRX_OFFSETS_NVM_LEN_IDX	2	/* Length of appended NVRAM data */
#ifdef BCMTRXV2
#define TRX_OFFSETS_DSG_LEN_IDX	3	/* Length of digital signature for the first image */
#define TRX_OFFSETS_CFG_LEN_IDX	4	/* Length of config region, which is not digitally signed */
#endif /* BCMTRXV2 */

#define TRX_OFFSETS_DLBASE_IDX  0       /* RAM start address for download */

#endif  /* _USB_RDL_H */
