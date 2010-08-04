/*-
 * Copyright (c) 2003, 2004
 *	Damien Bergamini <damien.bergamini@free.fr>. All rights reserved.
 *
 * Copyright (c) 2005-2007 Matthieu Castet <castet.matthieu@free.fr>
 * Copyright (c) 2005-2007 Stanislaw Gruszka <stf_xl@wp.pl>
 *
 * This software is available to you under a choice of one of two
 * licenses. You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * GPL license :
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *
 * HISTORY : some part of the code was base on ueagle 1.3 BSD driver,
 * Damien Bergamini agree to put his code under a DUAL GPL/BSD license.
 *
 * The rest of the code was was rewritten from scratch.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/crc32.h>
#include <linux/usb.h>
#include <linux/firmware.h>
#include <linux/ctype.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/freezer.h>
#include <linux/slab.h>

#include <asm/unaligned.h>

#include "usbatm.h"

#define EAGLEUSBVERSION "ueagle 1.4"


/*
 * Debug macros
 */
#define uea_dbg(usb_dev, format, args...)	\
	do { \
		if (debug >= 1) \
			dev_dbg(&(usb_dev)->dev, \
				"[ueagle-atm dbg] %s: " format, \
					__func__, ##args); \
	} while (0)

#define uea_vdbg(usb_dev, format, args...)	\
	do { \
		if (debug >= 2) \
			dev_dbg(&(usb_dev)->dev, \
				"[ueagle-atm vdbg]  " format, ##args); \
	} while (0)

#define uea_enters(usb_dev) \
	uea_vdbg(usb_dev, "entering %s\n" , __func__)

#define uea_leaves(usb_dev) \
	uea_vdbg(usb_dev, "leaving  %s\n" , __func__)

#define uea_err(usb_dev, format, args...) \
	dev_err(&(usb_dev)->dev , "[UEAGLE-ATM] " format , ##args)

#define uea_warn(usb_dev, format, args...) \
	dev_warn(&(usb_dev)->dev , "[Ueagle-atm] " format, ##args)

#define uea_info(usb_dev, format, args...) \
	dev_info(&(usb_dev)->dev , "[ueagle-atm] " format, ##args)

struct intr_pkt;

/* cmv's from firmware */
struct uea_cmvs_v1 {
	u32 address;
	u16 offset;
	u32 data;
} __attribute__ ((packed));

struct uea_cmvs_v2 {
	u32 group;
	u32 address;
	u32 offset;
	u32 data;
} __attribute__ ((packed));

/* information about currently processed cmv */
struct cmv_dsc_e1 {
	u8 function;
	u16 idx;
	u32 address;
	u16 offset;
};

struct cmv_dsc_e4 {
	u16 function;
	u16 offset;
	u16 address;
	u16 group;
};

union cmv_dsc {
	struct cmv_dsc_e1 e1;
	struct cmv_dsc_e4 e4;
};

struct uea_softc {
	struct usb_device *usb_dev;
	struct usbatm_data *usbatm;

	int modem_index;
	unsigned int driver_info;
	int annex;
#define ANNEXA 0
#define ANNEXB 1

	int booting;
	int reset;

	wait_queue_head_t sync_q;

	struct task_struct *kthread;
	u32 data;
	u32 data1;

	int cmv_ack;
	union cmv_dsc cmv_dsc;

	struct work_struct task;
	struct workqueue_struct *work_q;
	u16 pageno;
	u16 ovl;

	const struct firmware *dsp_firm;
	struct urb *urb_int;

	void (*dispatch_cmv) (struct uea_softc *, struct intr_pkt *);
	void (*schedule_load_page) (struct uea_softc *, struct intr_pkt *);
	int (*stat) (struct uea_softc *);
	int (*send_cmvs) (struct uea_softc *);

	/* keep in sync with eaglectl */
	struct uea_stats {
		struct {
			u32 state;
			u32 flags;
			u32 mflags;
			u32 vidcpe;
			u32 vidco;
			u32 dsrate;
			u32 usrate;
			u32 dsunc;
			u32 usunc;
			u32 dscorr;
			u32 uscorr;
			u32 txflow;
			u32 rxflow;
			u32 usattenuation;
			u32 dsattenuation;
			u32 dsmargin;
			u32 usmargin;
			u32 firmid;
		} phy;
	} stats;
};

/*
 * Elsa IDs
 */
#define ELSA_VID		0x05CC
#define ELSA_PID_PSTFIRM	0x3350
#define ELSA_PID_PREFIRM	0x3351

#define ELSA_PID_A_PREFIRM	0x3352
#define ELSA_PID_A_PSTFIRM	0x3353
#define ELSA_PID_B_PREFIRM	0x3362
#define ELSA_PID_B_PSTFIRM	0x3363

/*
 * Devolo IDs : pots if (pid & 0x10)
 */
#define DEVOLO_VID			0x1039
#define DEVOLO_EAGLE_I_A_PID_PSTFIRM	0x2110
#define DEVOLO_EAGLE_I_A_PID_PREFIRM	0x2111

#define DEVOLO_EAGLE_I_B_PID_PSTFIRM	0x2100
#define DEVOLO_EAGLE_I_B_PID_PREFIRM	0x2101

#define DEVOLO_EAGLE_II_A_PID_PSTFIRM	0x2130
#define DEVOLO_EAGLE_II_A_PID_PREFIRM	0x2131

#define DEVOLO_EAGLE_II_B_PID_PSTFIRM	0x2120
#define DEVOLO_EAGLE_II_B_PID_PREFIRM	0x2121

/*
 * Reference design USB IDs
 */
#define ANALOG_VID		0x1110
#define ADI930_PID_PREFIRM	0x9001
#define ADI930_PID_PSTFIRM	0x9000

#define EAGLE_I_PID_PREFIRM	0x9010	/* Eagle I */
#define EAGLE_I_PID_PSTFIRM	0x900F	/* Eagle I */

#define EAGLE_IIC_PID_PREFIRM	0x9024	/* Eagle IIC */
#define EAGLE_IIC_PID_PSTFIRM	0x9023	/* Eagle IIC */

#define EAGLE_II_PID_PREFIRM	0x9022	/* Eagle II */
#define EAGLE_II_PID_PSTFIRM	0x9021	/* Eagle II */

#define EAGLE_III_PID_PREFIRM	0x9032	/* Eagle III */
#define EAGLE_III_PID_PSTFIRM	0x9031	/* Eagle III */

#define EAGLE_IV_PID_PREFIRM	0x9042  /* Eagle IV */
#define EAGLE_IV_PID_PSTFIRM	0x9041  /* Eagle IV */

/*
 * USR USB IDs
 */
#define USR_VID			0x0BAF
#define MILLER_A_PID_PREFIRM	0x00F2
#define MILLER_A_PID_PSTFIRM	0x00F1
#define MILLER_B_PID_PREFIRM	0x00FA
#define MILLER_B_PID_PSTFIRM	0x00F9
#define HEINEKEN_A_PID_PREFIRM	0x00F6
#define HEINEKEN_A_PID_PSTFIRM	0x00F5
#define HEINEKEN_B_PID_PREFIRM	0x00F8
#define HEINEKEN_B_PID_PSTFIRM	0x00F7

#define PREFIRM 0
#define PSTFIRM (1<<7)
#define AUTO_ANNEX_A (1<<8)
#define AUTO_ANNEX_B (1<<9)

enum {
	ADI930 = 0,
	EAGLE_I,
	EAGLE_II,
	EAGLE_III,
	EAGLE_IV
};

/* macros for both struct usb_device_id and struct uea_softc */
#define UEA_IS_PREFIRM(x) \
	(!((x)->driver_info & PSTFIRM))
#define UEA_CHIP_VERSION(x) \
	((x)->driver_info & 0xf)

#define IS_ISDN(x) \
	((x)->annex & ANNEXB)

#define INS_TO_USBDEV(ins) (ins->usb_dev)

#define GET_STATUS(data) \
	((data >> 8) & 0xf)

#define IS_OPERATIONAL(sc) \
	((UEA_CHIP_VERSION(sc) != EAGLE_IV) ? \
	(GET_STATUS(sc->stats.phy.state) == 2) : \
	(sc->stats.phy.state == 7))

/*
 * Set of macros to handle unaligned data in the firmware blob.
 * The FW_GET_BYTE() macro is provided only for consistency.
 */

#define FW_GET_BYTE(p) (*((__u8 *) (p)))

#define FW_DIR "ueagle-atm/"
#define UEA_FW_NAME_MAX 30
#define NB_MODEM 4

#define BULK_TIMEOUT 300
#define CTRL_TIMEOUT 1000

#define ACK_TIMEOUT msecs_to_jiffies(3000)

#define UEA_INTR_IFACE_NO	0
#define UEA_US_IFACE_NO		1
#define UEA_DS_IFACE_NO		2

#define FASTEST_ISO_INTF	8

#define UEA_BULK_DATA_PIPE	0x02
#define UEA_IDMA_PIPE		0x04
#define UEA_INTR_PIPE		0x04
#define UEA_ISO_DATA_PIPE	0x08

#define UEA_E1_SET_BLOCK	0x0001
#define UEA_E4_SET_BLOCK	0x002c
#define UEA_SET_MODE		0x0003
#define UEA_SET_2183_DATA	0x0004
#define UEA_SET_TIMEOUT		0x0011

#define UEA_LOOPBACK_OFF	0x0002
#define UEA_LOOPBACK_ON		0x0003
#define UEA_BOOT_IDMA		0x0006
#define UEA_START_RESET		0x0007
#define UEA_END_RESET		0x0008

#define UEA_SWAP_MAILBOX	(0x3fcd | 0x4000)
#define UEA_MPTX_START		(0x3fce | 0x4000)
#define UEA_MPTX_MAILBOX	(0x3fd6 | 0x4000)
#define UEA_MPRX_MAILBOX	(0x3fdf | 0x4000)

/* block information in eagle4 dsp firmware  */
struct block_index {
	__le32 PageOffset;
	__le32 NotLastBlock;
	__le32 dummy;
	__le32 PageSize;
	__le32 PageAddress;
	__le16 dummy1;
	__le16 PageNumber;
} __attribute__ ((packed));

#define E4_IS_BOOT_PAGE(PageSize) ((le32_to_cpu(PageSize)) & 0x80000000)
#define E4_PAGE_BYTES(PageSize) ((le32_to_cpu(PageSize) & 0x7fffffff) * 4)

#define E4_L1_STRING_HEADER 0x10
#define E4_MAX_PAGE_NUMBER 0x58
#define E4_NO_SWAPPAGE_HEADERS 0x31

/* l1_code is eagle4 dsp firmware format */
struct l1_code {
	u8 string_header[E4_L1_STRING_HEADER];
	u8 page_number_to_block_index[E4_MAX_PAGE_NUMBER];
	struct block_index page_header[E4_NO_SWAPPAGE_HEADERS];
	u8 code[0];
} __attribute__ ((packed));

/* structures describing a block within a DSP page */
struct block_info_e1 {
	__le16 wHdr;
	__le16 wAddress;
	__le16 wSize;
	__le16 wOvlOffset;
	__le16 wOvl;		/* overlay */
	__le16 wLast;
} __attribute__ ((packed));
#define E1_BLOCK_INFO_SIZE 12

struct block_info_e4 {
	__be16 wHdr;
	__u8 bBootPage;
	__u8 bPageNumber;
	__be32 dwSize;
	__be32 dwAddress;
	__be16 wReserved;
} __attribute__ ((packed));
#define E4_BLOCK_INFO_SIZE 14

#define UEA_BIHDR 0xabcd
#define UEA_RESERVED 0xffff

/* constants describing cmv type */
#define E1_PREAMBLE 0x535c
#define E1_MODEMTOHOST 0x01
#define E1_HOSTTOMODEM 0x10

#define E1_MEMACCESS 0x1
#define E1_ADSLDIRECTIVE 0x7
#define E1_FUNCTION_TYPE(f) ((f) >> 4)
#define E1_FUNCTION_SUBTYPE(f) ((f) & 0x0f)

#define E4_MEMACCESS 0
#define E4_ADSLDIRECTIVE 0xf
#define E4_FUNCTION_TYPE(f) ((f) >> 8)
#define E4_FUNCTION_SIZE(f) ((f) & 0x0f)
#define E4_FUNCTION_SUBTYPE(f) (((f) >> 4) & 0x0f)

/* for MEMACCESS */
#define E1_REQUESTREAD	0x0
#define E1_REQUESTWRITE	0x1
#define E1_REPLYREAD	0x2
#define E1_REPLYWRITE	0x3

#define E4_REQUESTREAD	0x0
#define E4_REQUESTWRITE	0x4
#define E4_REPLYREAD	(E4_REQUESTREAD | 1)
#define E4_REPLYWRITE	(E4_REQUESTWRITE | 1)

/* for ADSLDIRECTIVE */
#define E1_KERNELREADY 0x0
#define E1_MODEMREADY  0x1

#define E4_KERNELREADY 0x0
#define E4_MODEMREADY  0x1

#define E1_MAKEFUNCTION(t, s) (((t) & 0xf) << 4 | ((s) & 0xf))
#define E4_MAKEFUNCTION(t, st, s) (((t) & 0xf) << 8 | \
	((st) & 0xf) << 4 | ((s) & 0xf))

#define E1_MAKESA(a, b, c, d)						\
	(((c) & 0xff) << 24 |						\
	 ((d) & 0xff) << 16 |						\
	 ((a) & 0xff) << 8  |						\
	 ((b) & 0xff))

#define E1_GETSA1(a) ((a >> 8) & 0xff)
#define E1_GETSA2(a) (a & 0xff)
#define E1_GETSA3(a) ((a >> 24) & 0xff)
#define E1_GETSA4(a) ((a >> 16) & 0xff)

#define E1_SA_CNTL E1_MAKESA('C', 'N', 'T', 'L')
#define E1_SA_DIAG E1_MAKESA('D', 'I', 'A', 'G')
#define E1_SA_INFO E1_MAKESA('I', 'N', 'F', 'O')
#define E1_SA_OPTN E1_MAKESA('O', 'P', 'T', 'N')
#define E1_SA_RATE E1_MAKESA('R', 'A', 'T', 'E')
#define E1_SA_STAT E1_MAKESA('S', 'T', 'A', 'T')

#define E4_SA_CNTL 1
#define E4_SA_STAT 2
#define E4_SA_INFO 3
#define E4_SA_TEST 4
#define E4_SA_OPTN 5
#define E4_SA_RATE 6
#define E4_SA_DIAG 7
#define E4_SA_CNFG 8

/* structures representing a CMV (Configuration and Management Variable) */
struct cmv_e1 {
	__le16 wPreamble;
	__u8 bDirection;
	__u8 bFunction;
	__le16 wIndex;
	__le32 dwSymbolicAddress;
	__le16 wOffsetAddress;
	__le32 dwData;
} __attribute__ ((packed));

struct cmv_e4 {
	__be16 wGroup;
	__be16 wFunction;
	__be16 wOffset;
	__be16 wAddress;
	__be32 dwData[6];
} __attribute__ ((packed));

/* structures representing swap information */
struct swap_info_e1 {
	__u8 bSwapPageNo;
	__u8 bOvl;		/* overlay */
} __attribute__ ((packed));

struct swap_info_e4 {
	__u8 bSwapPageNo;
} __attribute__ ((packed));

/* structures representing interrupt data */
#define e1_bSwapPageNo	u.e1.s1.swapinfo.bSwapPageNo
#define e1_bOvl		u.e1.s1.swapinfo.bOvl
#define e4_bSwapPageNo  u.e4.s1.swapinfo.bSwapPageNo

#define INT_LOADSWAPPAGE 0x0001
#define INT_INCOMINGCMV  0x0002

union intr_data_e1 {
	struct {
		struct swap_info_e1 swapinfo;
		__le16 wDataSize;
	} __attribute__ ((packed)) s1;
	struct {
		struct cmv_e1 cmv;
		__le16 wDataSize;
	} __attribute__ ((packed)) s2;
} __attribute__ ((packed));

union intr_data_e4 {
	struct {
		struct swap_info_e4 swapinfo;
		__le16 wDataSize;
	} __attribute__ ((packed)) s1;
	struct {
		struct cmv_e4 cmv;
		__le16 wDataSize;
	} __attribute__ ((packed)) s2;
} __attribute__ ((packed));

struct intr_pkt {
	__u8 bType;
	__u8 bNotification;
	__le16 wValue;
	__le16 wIndex;
	__le16 wLength;
	__le16 wInterrupt;
	union {
		union intr_data_e1 e1;
		union intr_data_e4 e4;
	} u;
} __attribute__ ((packed));

#define E1_INTR_PKT_SIZE 28
#define E4_INTR_PKT_SIZE 64

static struct usb_driver uea_driver;
static DEFINE_MUTEX(uea_mutex);
static const char *chip_name[] = {"ADI930", "Eagle I", "Eagle II", "Eagle III",
								"Eagle IV"};

static int modem_index;
static unsigned int debug;
static unsigned int altsetting[NB_MODEM] = {
				[0 ... (NB_MODEM - 1)] = FASTEST_ISO_INTF};
static int sync_wait[NB_MODEM];
static char *cmv_file[NB_MODEM];
static int annex[NB_MODEM];

module_param(debug, uint, 0644);
MODULE_PARM_DESC(debug, "module debug level (0=off,1=on,2=verbose)");
module_param_array(altsetting, uint, NULL, 0644);
MODULE_PARM_DESC(altsetting, "alternate setting for incoming traffic: 0=bulk, "
			     "1=isoc slowest, ... , 8=isoc fastest (default)");
module_param_array(sync_wait, bool, NULL, 0644);
MODULE_PARM_DESC(sync_wait, "wait the synchronisation before starting ATM");
module_param_array(cmv_file, charp, NULL, 0644);
MODULE_PARM_DESC(cmv_file,
		"file name with configuration and management variables");
module_param_array(annex, uint, NULL, 0644);
MODULE_PARM_DESC(annex,
		"manually set annex a/b (0=auto, 1=annex a, 2=annex b)");

#define uea_wait(sc, cond, timeo) \
({ \
	int _r = wait_event_interruptible_timeout(sc->sync_q, \
			(cond) || kthread_should_stop(), timeo); \
	if (kthread_should_stop()) \
		_r = -ENODEV; \
	_r; \
})

#define UPDATE_ATM_STAT(type, val) \
	do { \
		if (sc->usbatm->atm_dev) \
			sc->usbatm->atm_dev->type = val; \
	} while (0)

/* Firmware loading */
#define LOAD_INTERNAL     0xA0
#define F8051_USBCS       0x7f92

/**
 * uea_send_modem_cmd - Send a command for pre-firmware devices.
 */
static int uea_send_modem_cmd(struct usb_device *usb,
			      u16 addr, u16 size, const u8 *buff)
{
	int ret = -ENOMEM;
	u8 *xfer_buff;

	xfer_buff = kmemdup(buff, size, GFP_KERNEL);
	if (xfer_buff) {
		ret = usb_control_msg(usb,
				      usb_sndctrlpipe(usb, 0),
				      LOAD_INTERNAL,
				      USB_DIR_OUT | USB_TYPE_VENDOR |
				      USB_RECIP_DEVICE, addr, 0, xfer_buff,
				      size, CTRL_TIMEOUT);
		kfree(xfer_buff);
	}

	if (ret < 0)
		return ret;

	return (ret == size) ? 0 : -EIO;
}

static void uea_upload_pre_firmware(const struct firmware *fw_entry,
								void *context)
{
	struct usb_device *usb = context;
	const u8 *pfw;
	u8 value;
	u32 crc = 0;
	int ret, size;

	uea_enters(usb);
	if (!fw_entry) {
		uea_err(usb, "firmware is not available\n");
		goto err;
	}

	pfw = fw_entry->data;
	size = fw_entry->size;
	if (size < 4)
		goto err_fw_corrupted;

	crc = get_unaligned_le32(pfw);
	pfw += 4;
	size -= 4;
	if (crc32_be(0, pfw, size) != crc)
		goto err_fw_corrupted;

	/*
	 * Start to upload firmware : send reset
	 */
	value = 1;
	ret = uea_send_modem_cmd(usb, F8051_USBCS, sizeof(value), &value);

	if (ret < 0) {
		uea_err(usb, "modem reset failed with error %d\n", ret);
		goto err;
	}

	while (size > 3) {
		u8 len = FW_GET_BYTE(pfw);
		u16 add = get_unaligned_le16(pfw + 1);

		size -= len + 3;
		if (size < 0)
			goto err_fw_corrupted;

		ret = uea_send_modem_cmd(usb, add, len, pfw + 3);
		if (ret < 0) {
			uea_err(usb, "uploading firmware data failed "
					"with error %d\n", ret);
			goto err;
		}
		pfw += len + 3;
	}

	if (size != 0)
		goto err_fw_corrupted;

	/*
	 * Tell the modem we finish : de-assert reset
	 */
	value = 0;
	ret = uea_send_modem_cmd(usb, F8051_USBCS, 1, &value);
	if (ret < 0)
		uea_err(usb, "modem de-assert failed with error %d\n", ret);
	else
		uea_info(usb, "firmware uploaded\n");

	goto err;

err_fw_corrupted:
	uea_err(usb, "firmware is corrupted\n");
err:
	release_firmware(fw_entry);
	uea_leaves(usb);
}

/**
 * uea_load_firmware - Load usb firmware for pre-firmware devices.
 */
static int uea_load_firmware(struct usb_device *usb, unsigned int ver)
{
	int ret;
	char *fw_name = FW_DIR "eagle.fw";

	uea_enters(usb);
	uea_info(usb, "pre-firmware device, uploading firmware\n");

	switch (ver) {
	case ADI930:
		fw_name = FW_DIR "adi930.fw";
		break;
	case EAGLE_I:
		fw_name = FW_DIR "eagleI.fw";
		break;
	case EAGLE_II:
		fw_name = FW_DIR "eagleII.fw";
		break;
	case EAGLE_III:
		fw_name = FW_DIR "eagleIII.fw";
		break;
	case EAGLE_IV:
		fw_name = FW_DIR "eagleIV.fw";
		break;
	}

	ret = request_firmware_nowait(THIS_MODULE, 1, fw_name, &usb->dev,
					GFP_KERNEL, usb,
					uea_upload_pre_firmware);
	if (ret)
		uea_err(usb, "firmware %s is not available\n", fw_name);
	else
		uea_info(usb, "loading firmware %s\n", fw_name);

	uea_leaves(usb);
	return ret;
}

/* modem management : dsp firmware, send/read CMV, monitoring statistic
 */

/*
 * Make sure that the DSP code provided is safe to use.
 */
static int check_dsp_e1(const u8 *dsp, unsigned int len)
{
	u8 pagecount, blockcount;
	u16 blocksize;
	u32 pageoffset;
	unsigned int i, j, p, pp;

	pagecount = FW_GET_BYTE(dsp);
	p = 1;

	/* enough space for page offsets? */
	if (p + 4 * pagecount > len)
		return 1;

	for (i = 0; i < pagecount; i++) {

		pageoffset = get_unaligned_le32(dsp + p);
		p += 4;

		if (pageoffset == 0)
			continue;

		/* enough space for blockcount? */
		if (pageoffset >= len)
			return 1;

		pp = pageoffset;
		blockcount = FW_GET_BYTE(dsp + pp);
		pp += 1;

		for (j = 0; j < blockcount; j++) {

			/* enough space for block header? */
			if (pp + 4 > len)
				return 1;

			pp += 2;	/* skip blockaddr */
			blocksize = get_unaligned_le16(dsp + pp);
			pp += 2;

			/* enough space for block data? */
			if (pp + blocksize > len)
				return 1;

			pp += blocksize;
		}
	}

	return 0;
}

static int check_dsp_e4(const u8 *dsp, int len)
{
	int i;
	struct l1_code *p = (struct l1_code *) dsp;
	unsigned int sum = p->code - dsp;

	if (len < sum)
		return 1;

	if (strcmp("STRATIPHY ANEXA", p->string_header) != 0 &&
	    strcmp("STRATIPHY ANEXB", p->string_header) != 0)
		return 1;

	for (i = 0; i < E4_MAX_PAGE_NUMBER; i++) {
		struct block_index *blockidx;
		u8 blockno = p->page_number_to_block_index[i];
		if (blockno >= E4_NO_SWAPPAGE_HEADERS)
			continue;

		do {
			u64 l;

			if (blockno >= E4_NO_SWAPPAGE_HEADERS)
				return 1;

			blockidx = &p->page_header[blockno++];
			if ((u8 *)(blockidx + 1) - dsp  >= len)
				return 1;

			if (le16_to_cpu(blockidx->PageNumber) != i)
				return 1;

			l = E4_PAGE_BYTES(blockidx->PageSize);
			sum += l;
			l += le32_to_cpu(blockidx->PageOffset);
			if (l > len)
				return 1;

		/* zero is zero regardless endianes */
		} while (blockidx->NotLastBlock);
	}

	return (sum == len) ? 0 : 1;
}

/*
 * send data to the idma pipe
 * */
static int uea_idma_write(struct uea_softc *sc, const void *data, u32 size)
{
	int ret = -ENOMEM;
	u8 *xfer_buff;
	int bytes_read;

	xfer_buff = kmemdup(data, size, GFP_KERNEL);
	if (!xfer_buff) {
		uea_err(INS_TO_USBDEV(sc), "can't allocate xfer_buff\n");
		return ret;
	}

	ret = usb_bulk_msg(sc->usb_dev,
			 usb_sndbulkpipe(sc->usb_dev, UEA_IDMA_PIPE),
			 xfer_buff, size, &bytes_read, BULK_TIMEOUT);

	kfree(xfer_buff);
	if (ret < 0)
		return ret;
	if (size != bytes_read) {
		uea_err(INS_TO_USBDEV(sc), "size != bytes_read %d %d\n", size,
		       bytes_read);
		return -EIO;
	}

	return 0;
}

static int request_dsp(struct uea_softc *sc)
{
	int ret;
	char *dsp_name;

	if (UEA_CHIP_VERSION(sc) == EAGLE_IV) {
		if (IS_ISDN(sc))
			dsp_name = FW_DIR "DSP4i.bin";
		else
			dsp_name = FW_DIR "DSP4p.bin";
	} else if (UEA_CHIP_VERSION(sc) == ADI930) {
		if (IS_ISDN(sc))
			dsp_name = FW_DIR "DSP9i.bin";
		else
			dsp_name = FW_DIR "DSP9p.bin";
	} else {
		if (IS_ISDN(sc))
			dsp_name = FW_DIR "DSPei.bin";
		else
			dsp_name = FW_DIR "DSPep.bin";
	}

	ret = request_firmware(&sc->dsp_firm, dsp_name, &sc->usb_dev->dev);
	if (ret < 0) {
		uea_err(INS_TO_USBDEV(sc),
		       "requesting firmware %s failed with error %d\n",
			dsp_name, ret);
		return ret;
	}

	if (UEA_CHIP_VERSION(sc) == EAGLE_IV)
		ret = check_dsp_e4(sc->dsp_firm->data, sc->dsp_firm->size);
	else
		ret = check_dsp_e1(sc->dsp_firm->data, sc->dsp_firm->size);

	if (ret) {
		uea_err(INS_TO_USBDEV(sc), "firmware %s is corrupted\n",
		       dsp_name);
		release_firmware(sc->dsp_firm);
		sc->dsp_firm = NULL;
		return -EILSEQ;
	}

	return 0;
}

/*
 * The uea_load_page() function must be called within a process context
 */
static void uea_load_page_e1(struct work_struct *work)
{
	struct uea_softc *sc = container_of(work, struct uea_softc, task);
	u16 pageno = sc->pageno;
	u16 ovl = sc->ovl;
	struct block_info_e1 bi;

	const u8 *p;
	u8 pagecount, blockcount;
	u16 blockaddr, blocksize;
	u32 pageoffset;
	int i;

	/* reload firmware when reboot start and it's loaded already */
	if (ovl == 0 && pageno == 0 && sc->dsp_firm) {
		release_firmware(sc->dsp_firm);
		sc->dsp_firm = NULL;
	}

	if (sc->dsp_firm == NULL && request_dsp(sc) < 0)
		return;

	p = sc->dsp_firm->data;
	pagecount = FW_GET_BYTE(p);
	p += 1;

	if (pageno >= pagecount)
		goto bad1;

	p += 4 * pageno;
	pageoffset = get_unaligned_le32(p);

	if (pageoffset == 0)
		goto bad1;

	p = sc->dsp_firm->data + pageoffset;
	blockcount = FW_GET_BYTE(p);
	p += 1;

	uea_dbg(INS_TO_USBDEV(sc),
	       "sending %u blocks for DSP page %u\n", blockcount, pageno);

	bi.wHdr = cpu_to_le16(UEA_BIHDR);
	bi.wOvl = cpu_to_le16(ovl);
	bi.wOvlOffset = cpu_to_le16(ovl | 0x8000);

	for (i = 0; i < blockcount; i++) {
		blockaddr = get_unaligned_le16(p);
		p += 2;

		blocksize = get_unaligned_le16(p);
		p += 2;

		bi.wSize = cpu_to_le16(blocksize);
		bi.wAddress = cpu_to_le16(blockaddr);
		bi.wLast = cpu_to_le16((i == blockcount - 1) ? 1 : 0);

		/* send block info through the IDMA pipe */
		if (uea_idma_write(sc, &bi, E1_BLOCK_INFO_SIZE))
			goto bad2;

		/* send block data through the IDMA pipe */
		if (uea_idma_write(sc, p, blocksize))
			goto bad2;

		p += blocksize;
	}

	return;

bad2:
	uea_err(INS_TO_USBDEV(sc), "sending DSP block %u failed\n", i);
	return;
bad1:
	uea_err(INS_TO_USBDEV(sc), "invalid DSP page %u requested\n", pageno);
}

static void __uea_load_page_e4(struct uea_softc *sc, u8 pageno, int boot)
{
	struct block_info_e4 bi;
	struct block_index *blockidx;
	struct l1_code *p = (struct l1_code *) sc->dsp_firm->data;
	u8 blockno = p->page_number_to_block_index[pageno];

	bi.wHdr = cpu_to_be16(UEA_BIHDR);
	bi.bBootPage = boot;
	bi.bPageNumber = pageno;
	bi.wReserved = cpu_to_be16(UEA_RESERVED);

	do {
		const u8 *blockoffset;
		unsigned int blocksize;

		blockidx = &p->page_header[blockno];
		blocksize = E4_PAGE_BYTES(blockidx->PageSize);
		blockoffset = sc->dsp_firm->data + le32_to_cpu(
							blockidx->PageOffset);

		bi.dwSize = cpu_to_be32(blocksize);
		bi.dwAddress = cpu_to_be32(le32_to_cpu(blockidx->PageAddress));

		uea_dbg(INS_TO_USBDEV(sc),
			"sending block %u for DSP page "
			"%u size %u address %x\n",
			blockno, pageno, blocksize,
			le32_to_cpu(blockidx->PageAddress));

		/* send block info through the IDMA pipe */
		if (uea_idma_write(sc, &bi, E4_BLOCK_INFO_SIZE))
			goto bad;

		/* send block data through the IDMA pipe */
		if (uea_idma_write(sc, blockoffset, blocksize))
			goto bad;

		blockno++;
	} while (blockidx->NotLastBlock);

	return;

bad:
	uea_err(INS_TO_USBDEV(sc), "sending DSP block %u failed\n", blockno);
	return;
}

static void uea_load_page_e4(struct work_struct *work)
{
	struct uea_softc *sc = container_of(work, struct uea_softc, task);
	u8 pageno = sc->pageno;
	int i;
	struct block_info_e4 bi;
	struct l1_code *p;

	uea_dbg(INS_TO_USBDEV(sc), "sending DSP page %u\n", pageno);

	/* reload firmware when reboot start and it's loaded already */
	if (pageno == 0 && sc->dsp_firm) {
		release_firmware(sc->dsp_firm);
		sc->dsp_firm = NULL;
	}

	if (sc->dsp_firm == NULL && request_dsp(sc) < 0)
		return;

	p = (struct l1_code *) sc->dsp_firm->data;
	if (pageno >= le16_to_cpu(p->page_header[0].PageNumber)) {
		uea_err(INS_TO_USBDEV(sc), "invalid DSP "
						"page %u requested\n", pageno);
		return;
	}

	if (pageno != 0) {
		__uea_load_page_e4(sc, pageno, 0);
		return;
	}

	uea_dbg(INS_TO_USBDEV(sc),
	       "sending Main DSP page %u\n", p->page_header[0].PageNumber);

	for (i = 0; i < le16_to_cpu(p->page_header[0].PageNumber); i++) {
		if (E4_IS_BOOT_PAGE(p->page_header[i].PageSize))
			__uea_load_page_e4(sc, i, 1);
	}

	uea_dbg(INS_TO_USBDEV(sc) , "sending start bi\n");

	bi.wHdr = cpu_to_be16(UEA_BIHDR);
	bi.bBootPage = 0;
	bi.bPageNumber = 0xff;
	bi.wReserved = cpu_to_be16(UEA_RESERVED);
	bi.dwSize = cpu_to_be32(E4_PAGE_BYTES(p->page_header[0].PageSize));
	bi.dwAddress = cpu_to_be32(le32_to_cpu(p->page_header[0].PageAddress));

	/* send block info through the IDMA pipe */
	if (uea_idma_write(sc, &bi, E4_BLOCK_INFO_SIZE))
		uea_err(INS_TO_USBDEV(sc), "sending DSP start bi failed\n");
}

static inline void wake_up_cmv_ack(struct uea_softc *sc)
{
	BUG_ON(sc->cmv_ack);
	sc->cmv_ack = 1;
	wake_up(&sc->sync_q);
}

static inline int wait_cmv_ack(struct uea_softc *sc)
{
	int ret = uea_wait(sc, sc->cmv_ack , ACK_TIMEOUT);

	sc->cmv_ack = 0;

	uea_dbg(INS_TO_USBDEV(sc), "wait_event_timeout : %d ms\n",
			jiffies_to_msecs(ret));

	if (ret < 0)
		return ret;

	return (ret == 0) ? -ETIMEDOUT : 0;
}

#define UCDC_SEND_ENCAPSULATED_COMMAND 0x00

static int uea_request(struct uea_softc *sc,
		u16 value, u16 index, u16 size, const void *data)
{
	u8 *xfer_buff;
	int ret = -ENOMEM;

	xfer_buff = kmemdup(data, size, GFP_KERNEL);
	if (!xfer_buff) {
		uea_err(INS_TO_USBDEV(sc), "can't allocate xfer_buff\n");
		return ret;
	}

	ret = usb_control_msg(sc->usb_dev, usb_sndctrlpipe(sc->usb_dev, 0),
			      UCDC_SEND_ENCAPSULATED_COMMAND,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      value, index, xfer_buff, size, CTRL_TIMEOUT);

	kfree(xfer_buff);
	if (ret < 0) {
		uea_err(INS_TO_USBDEV(sc), "usb_control_msg error %d\n", ret);
		return ret;
	}

	if (ret != size) {
		uea_err(INS_TO_USBDEV(sc),
		       "usb_control_msg send only %d bytes (instead of %d)\n",
		       ret, size);
		return -EIO;
	}

	return 0;
}

static int uea_cmv_e1(struct uea_softc *sc,
		u8 function, u32 address, u16 offset, u32 data)
{
	struct cmv_e1 cmv;
	int ret;

	uea_enters(INS_TO_USBDEV(sc));
	uea_vdbg(INS_TO_USBDEV(sc), "Function : %d-%d, Address : %c%c%c%c, "
			"offset : 0x%04x, data : 0x%08x\n",
			E1_FUNCTION_TYPE(function),
			E1_FUNCTION_SUBTYPE(function),
			E1_GETSA1(address), E1_GETSA2(address),
			E1_GETSA3(address),
			E1_GETSA4(address), offset, data);

	/* we send a request, but we expect a reply */
	sc->cmv_dsc.e1.function = function | 0x2;
	sc->cmv_dsc.e1.idx++;
	sc->cmv_dsc.e1.address = address;
	sc->cmv_dsc.e1.offset = offset;

	cmv.wPreamble = cpu_to_le16(E1_PREAMBLE);
	cmv.bDirection = E1_HOSTTOMODEM;
	cmv.bFunction = function;
	cmv.wIndex = cpu_to_le16(sc->cmv_dsc.e1.idx);
	put_unaligned_le32(address, &cmv.dwSymbolicAddress);
	cmv.wOffsetAddress = cpu_to_le16(offset);
	put_unaligned_le32(data >> 16 | data << 16, &cmv.dwData);

	ret = uea_request(sc, UEA_E1_SET_BLOCK, UEA_MPTX_START,
							sizeof(cmv), &cmv);
	if (ret < 0)
		return ret;
	ret = wait_cmv_ack(sc);
	uea_leaves(INS_TO_USBDEV(sc));
	return ret;
}

static int uea_cmv_e4(struct uea_softc *sc,
		u16 function, u16 group, u16 address, u16 offset, u32 data)
{
	struct cmv_e4 cmv;
	int ret;

	uea_enters(INS_TO_USBDEV(sc));
	memset(&cmv, 0, sizeof(cmv));

	uea_vdbg(INS_TO_USBDEV(sc), "Function : %d-%d, Group : 0x%04x, "
		 "Address : 0x%04x, offset : 0x%04x, data : 0x%08x\n",
		 E4_FUNCTION_TYPE(function), E4_FUNCTION_SUBTYPE(function),
		 group, address, offset, data);

	/* we send a request, but we expect a reply */
	sc->cmv_dsc.e4.function = function | (0x1 << 4);
	sc->cmv_dsc.e4.offset = offset;
	sc->cmv_dsc.e4.address = address;
	sc->cmv_dsc.e4.group = group;

	cmv.wFunction = cpu_to_be16(function);
	cmv.wGroup = cpu_to_be16(group);
	cmv.wAddress = cpu_to_be16(address);
	cmv.wOffset = cpu_to_be16(offset);
	cmv.dwData[0] = cpu_to_be32(data);

	ret = uea_request(sc, UEA_E4_SET_BLOCK, UEA_MPTX_START,
							sizeof(cmv), &cmv);
	if (ret < 0)
		return ret;
	ret = wait_cmv_ack(sc);
	uea_leaves(INS_TO_USBDEV(sc));
	return ret;
}

static inline int uea_read_cmv_e1(struct uea_softc *sc,
		u32 address, u16 offset, u32 *data)
{
	int ret = uea_cmv_e1(sc, E1_MAKEFUNCTION(E1_MEMACCESS, E1_REQUESTREAD),
			  address, offset, 0);
	if (ret < 0)
		uea_err(INS_TO_USBDEV(sc),
			"reading cmv failed with error %d\n", ret);
	else
		*data = sc->data;

	return ret;
}

static inline int uea_read_cmv_e4(struct uea_softc *sc,
		u8 size, u16 group, u16 address, u16 offset, u32 *data)
{
	int ret = uea_cmv_e4(sc, E4_MAKEFUNCTION(E4_MEMACCESS,
							E4_REQUESTREAD, size),
			  group, address, offset, 0);
	if (ret < 0)
		uea_err(INS_TO_USBDEV(sc),
			"reading cmv failed with error %d\n", ret);
	else {
		*data = sc->data;
		/* size is in 16-bit word quantities */
		if (size > 2)
			*(data + 1) = sc->data1;
	}
	return ret;
}

static inline int uea_write_cmv_e1(struct uea_softc *sc,
		u32 address, u16 offset, u32 data)
{
	int ret = uea_cmv_e1(sc, E1_MAKEFUNCTION(E1_MEMACCESS, E1_REQUESTWRITE),
			  address, offset, data);
	if (ret < 0)
		uea_err(INS_TO_USBDEV(sc),
			"writing cmv failed with error %d\n", ret);

	return ret;
}

static inline int uea_write_cmv_e4(struct uea_softc *sc,
		u8 size, u16 group, u16 address, u16 offset, u32 data)
{
	int ret = uea_cmv_e4(sc, E4_MAKEFUNCTION(E4_MEMACCESS,
							E4_REQUESTWRITE, size),
			  group, address, offset, data);
	if (ret < 0)
		uea_err(INS_TO_USBDEV(sc),
			"writing cmv failed with error %d\n", ret);

	return ret;
}

static void uea_set_bulk_timeout(struct uea_softc *sc, u32 dsrate)
{
	int ret;
	u16 timeout;

	/* in bulk mode the modem have problem with high rate
	 * changing internal timing could improve things, but the
	 * value is misterious.
	 * ADI930 don't support it (-EPIPE error).
	 */

	if (UEA_CHIP_VERSION(sc) == ADI930 ||
	    altsetting[sc->modem_index] > 0 ||
	    sc->stats.phy.dsrate == dsrate)
		return;

	/* Original timming (1Mbit/s) from ADI (used in windows driver) */
	timeout = (dsrate <= 1024*1024) ? 0 : 1;
	ret = uea_request(sc, UEA_SET_TIMEOUT, timeout, 0, NULL);
	uea_info(INS_TO_USBDEV(sc), "setting new timeout %d%s\n",
		 timeout,  ret < 0 ? " failed" : "");

}

/*
 * Monitor the modem and update the stat
 * return 0 if everything is ok
 * return < 0 if an error occurs (-EAGAIN reboot needed)
 */
static int uea_stat_e1(struct uea_softc *sc)
{
	u32 data;
	int ret;

	uea_enters(INS_TO_USBDEV(sc));
	data = sc->stats.phy.state;

	ret = uea_read_cmv_e1(sc, E1_SA_STAT, 0, &sc->stats.phy.state);
	if (ret < 0)
		return ret;

	switch (GET_STATUS(sc->stats.phy.state)) {
	case 0:		/* not yet synchronized */
		uea_dbg(INS_TO_USBDEV(sc),
		       "modem not yet synchronized\n");
		return 0;

	case 1:		/* initialization */
		uea_dbg(INS_TO_USBDEV(sc), "modem initializing\n");
		return 0;

	case 2:		/* operational */
		uea_vdbg(INS_TO_USBDEV(sc), "modem operational\n");
		break;

	case 3:		/* fail ... */
		uea_info(INS_TO_USBDEV(sc), "modem synchronization failed"
					" (may be try other cmv/dsp)\n");
		return -EAGAIN;

	case 4 ... 6:	/* test state */
		uea_warn(INS_TO_USBDEV(sc),
				"modem in test mode - not supported\n");
		return -EAGAIN;

	case 7:		/* fast-retain ... */
		uea_info(INS_TO_USBDEV(sc), "modem in fast-retain mode\n");
		return 0;
	default:
		uea_err(INS_TO_USBDEV(sc), "modem invalid SW mode %d\n",
			GET_STATUS(sc->stats.phy.state));
		return -EAGAIN;
	}

	if (GET_STATUS(data) != 2) {
		uea_request(sc, UEA_SET_MODE, UEA_LOOPBACK_OFF, 0, NULL);
		uea_info(INS_TO_USBDEV(sc), "modem operational\n");

		/* release the dsp firmware as it is not needed until
		 * the next failure
		 */
		if (sc->dsp_firm) {
			release_firmware(sc->dsp_firm);
			sc->dsp_firm = NULL;
		}
	}

	/* always update it as atm layer could not be init when we switch to
	 * operational state
	 */
	UPDATE_ATM_STAT(signal, ATM_PHY_SIG_FOUND);

	/* wake up processes waiting for synchronization */
	wake_up(&sc->sync_q);

	ret = uea_read_cmv_e1(sc, E1_SA_DIAG, 2, &sc->stats.phy.flags);
	if (ret < 0)
		return ret;
	sc->stats.phy.mflags |= sc->stats.phy.flags;

	/* in case of a flags ( for example delineation LOSS (& 0x10)),
	 * we check the status again in order to detect the failure earlier
	 */
	if (sc->stats.phy.flags) {
		uea_dbg(INS_TO_USBDEV(sc), "Stat flag = 0x%x\n",
		       sc->stats.phy.flags);
		return 0;
	}

	ret = uea_read_cmv_e1(sc, E1_SA_RATE, 0, &data);
	if (ret < 0)
		return ret;

	uea_set_bulk_timeout(sc, (data >> 16) * 32);
	sc->stats.phy.dsrate = (data >> 16) * 32;
	sc->stats.phy.usrate = (data & 0xffff) * 32;
	UPDATE_ATM_STAT(link_rate, sc->stats.phy.dsrate * 1000 / 424);

	ret = uea_read_cmv_e1(sc, E1_SA_DIAG, 23, &data);
	if (ret < 0)
		return ret;
	sc->stats.phy.dsattenuation = (data & 0xff) / 2;

	ret = uea_read_cmv_e1(sc, E1_SA_DIAG, 47, &data);
	if (ret < 0)
		return ret;
	sc->stats.phy.usattenuation = (data & 0xff) / 2;

	ret = uea_read_cmv_e1(sc, E1_SA_DIAG, 25, &sc->stats.phy.dsmargin);
	if (ret < 0)
		return ret;

	ret = uea_read_cmv_e1(sc, E1_SA_DIAG, 49, &sc->stats.phy.usmargin);
	if (ret < 0)
		return ret;

	ret = uea_read_cmv_e1(sc, E1_SA_DIAG, 51, &sc->stats.phy.rxflow);
	if (ret < 0)
		return ret;

	ret = uea_read_cmv_e1(sc, E1_SA_DIAG, 52, &sc->stats.phy.txflow);
	if (ret < 0)
		return ret;

	ret = uea_read_cmv_e1(sc, E1_SA_DIAG, 54, &sc->stats.phy.dsunc);
	if (ret < 0)
		return ret;

	/* only for atu-c */
	ret = uea_read_cmv_e1(sc, E1_SA_DIAG, 58, &sc->stats.phy.usunc);
	if (ret < 0)
		return ret;

	ret = uea_read_cmv_e1(sc, E1_SA_DIAG, 53, &sc->stats.phy.dscorr);
	if (ret < 0)
		return ret;

	/* only for atu-c */
	ret = uea_read_cmv_e1(sc, E1_SA_DIAG, 57, &sc->stats.phy.uscorr);
	if (ret < 0)
		return ret;

	ret = uea_read_cmv_e1(sc, E1_SA_INFO, 8, &sc->stats.phy.vidco);
	if (ret < 0)
		return ret;

	ret = uea_read_cmv_e1(sc, E1_SA_INFO, 13, &sc->stats.phy.vidcpe);
	if (ret < 0)
		return ret;

	return 0;
}

static int uea_stat_e4(struct uea_softc *sc)
{
	u32 data;
	u32 tmp_arr[2];
	int ret;

	uea_enters(INS_TO_USBDEV(sc));
	data = sc->stats.phy.state;

	/* XXX only need to be done before operationnal... */
	ret = uea_read_cmv_e4(sc, 1, E4_SA_STAT, 0, 0, &sc->stats.phy.state);
	if (ret < 0)
		return ret;

	switch (sc->stats.phy.state) {
	case 0x0:	/* not yet synchronized */
	case 0x1:
	case 0x3:
	case 0x4:
		uea_dbg(INS_TO_USBDEV(sc), "modem not yet "
						"synchronized\n");
		return 0;
	case 0x5:	/* initialization */
	case 0x6:
	case 0x9:
	case 0xa:
		uea_dbg(INS_TO_USBDEV(sc), "modem initializing\n");
		return 0;
	case 0x2:	/* fail ... */
		uea_info(INS_TO_USBDEV(sc), "modem synchronization "
				"failed (may be try other cmv/dsp)\n");
		return -EAGAIN;
	case 0x7:	/* operational */
		break;
	default:
		uea_warn(INS_TO_USBDEV(sc), "unknown state: %x\n",
						sc->stats.phy.state);
		return 0;
	}

	if (data != 7) {
		uea_request(sc, UEA_SET_MODE, UEA_LOOPBACK_OFF, 0, NULL);
		uea_info(INS_TO_USBDEV(sc), "modem operational\n");

		/* release the dsp firmware as it is not needed until
		 * the next failure
		 */
		if (sc->dsp_firm) {
			release_firmware(sc->dsp_firm);
			sc->dsp_firm = NULL;
		}
	}

	/* always update it as atm layer could not be init when we switch to
	 * operational state
	 */
	UPDATE_ATM_STAT(signal, ATM_PHY_SIG_FOUND);

	/* wake up processes waiting for synchronization */
	wake_up(&sc->sync_q);

	/* TODO improve this state machine :
	 * we need some CMV info : what they do and their unit
	 * we should find the equivalent of eagle3- CMV
	 */
	/* check flags */
	ret = uea_read_cmv_e4(sc, 1, E4_SA_DIAG, 0, 0, &sc->stats.phy.flags);
	if (ret < 0)
		return ret;
	sc->stats.phy.mflags |= sc->stats.phy.flags;

	/* in case of a flags ( for example delineation LOSS (& 0x10)),
	 * we check the status again in order to detect the failure earlier
	 */
	if (sc->stats.phy.flags) {
		uea_dbg(INS_TO_USBDEV(sc), "Stat flag = 0x%x\n",
		       sc->stats.phy.flags);
		if (sc->stats.phy.flags & 1) /* delineation LOSS */
			return -EAGAIN;
		if (sc->stats.phy.flags & 0x4000) /* Reset Flag */
			return -EAGAIN;
		return 0;
	}

	/* rate data may be in upper or lower half of 64 bit word, strange */
	ret = uea_read_cmv_e4(sc, 4, E4_SA_RATE, 0, 0, tmp_arr);
	if (ret < 0)
		return ret;
	data = (tmp_arr[0]) ? tmp_arr[0] : tmp_arr[1];
	sc->stats.phy.usrate = data / 1000;

	ret = uea_read_cmv_e4(sc, 4, E4_SA_RATE, 1, 0, tmp_arr);
	if (ret < 0)
		return ret;
	data = (tmp_arr[0]) ? tmp_arr[0] : tmp_arr[1];
	uea_set_bulk_timeout(sc, data / 1000);
	sc->stats.phy.dsrate = data / 1000;
	UPDATE_ATM_STAT(link_rate, sc->stats.phy.dsrate * 1000 / 424);

	ret = uea_read_cmv_e4(sc, 1, E4_SA_INFO, 68, 1, &data);
	if (ret < 0)
		return ret;
	sc->stats.phy.dsattenuation = data / 10;

	ret = uea_read_cmv_e4(sc, 1, E4_SA_INFO, 69, 1, &data);
	if (ret < 0)
		return ret;
	sc->stats.phy.usattenuation = data / 10;

	ret = uea_read_cmv_e4(sc, 1, E4_SA_INFO, 68, 3, &data);
	if (ret < 0)
		return ret;
	sc->stats.phy.dsmargin = data / 2;

	ret = uea_read_cmv_e4(sc, 1, E4_SA_INFO, 69, 3, &data);
	if (ret < 0)
		return ret;
	sc->stats.phy.usmargin = data / 10;

	return 0;
}

static void cmvs_file_name(struct uea_softc *sc, char *const cmv_name, int ver)
{
	char file_arr[] = "CMVxy.bin";
	char *file;

	/* set proper name corresponding modem version and line type */
	if (cmv_file[sc->modem_index] == NULL) {
		if (UEA_CHIP_VERSION(sc) == ADI930)
			file_arr[3] = '9';
		else if (UEA_CHIP_VERSION(sc) == EAGLE_IV)
			file_arr[3] = '4';
		else
			file_arr[3] = 'e';

		file_arr[4] = IS_ISDN(sc) ? 'i' : 'p';
		file = file_arr;
	} else
		file = cmv_file[sc->modem_index];

	strcpy(cmv_name, FW_DIR);
	strlcat(cmv_name, file, UEA_FW_NAME_MAX);
	if (ver == 2)
		strlcat(cmv_name, ".v2", UEA_FW_NAME_MAX);
}

static int request_cmvs_old(struct uea_softc *sc,
		 void **cmvs, const struct firmware **fw)
{
	int ret, size;
	u8 *data;
	char cmv_name[UEA_FW_NAME_MAX]; /* 30 bytes stack variable */

	cmvs_file_name(sc, cmv_name, 1);
	ret = request_firmware(fw, cmv_name, &sc->usb_dev->dev);
	if (ret < 0) {
		uea_err(INS_TO_USBDEV(sc),
		       "requesting firmware %s failed with error %d\n",
		       cmv_name, ret);
		return ret;
	}

	data = (u8 *) (*fw)->data;
	size = (*fw)->size;
	if (size < 1)
		goto err_fw_corrupted;

	if (size != *data * sizeof(struct uea_cmvs_v1) + 1)
		goto err_fw_corrupted;

	*cmvs = (void *)(data + 1);
	return *data;

err_fw_corrupted:
	uea_err(INS_TO_USBDEV(sc), "firmware %s is corrupted\n", cmv_name);
	release_firmware(*fw);
	return -EILSEQ;
}

static int request_cmvs(struct uea_softc *sc,
		 void **cmvs, const struct firmware **fw, int *ver)
{
	int ret, size;
	u32 crc;
	u8 *data;
	char cmv_name[UEA_FW_NAME_MAX]; /* 30 bytes stack variable */

	cmvs_file_name(sc, cmv_name, 2);
	ret = request_firmware(fw, cmv_name, &sc->usb_dev->dev);
	if (ret < 0) {
		/* if caller can handle old version, try to provide it */
		if (*ver == 1) {
			uea_warn(INS_TO_USBDEV(sc), "requesting "
							"firmware %s failed, "
				"try to get older cmvs\n", cmv_name);
			return request_cmvs_old(sc, cmvs, fw);
		}
		uea_err(INS_TO_USBDEV(sc),
		       "requesting firmware %s failed with error %d\n",
		       cmv_name, ret);
		return ret;
	}

	size = (*fw)->size;
	data = (u8 *) (*fw)->data;
	if (size < 4 || strncmp(data, "cmv2", 4) != 0) {
		if (*ver == 1) {
			uea_warn(INS_TO_USBDEV(sc), "firmware %s is corrupted,"
				" try to get older cmvs\n", cmv_name);
			release_firmware(*fw);
			return request_cmvs_old(sc, cmvs, fw);
		}
		goto err_fw_corrupted;
	}

	*ver = 2;

	data += 4;
	size -= 4;
	if (size < 5)
		goto err_fw_corrupted;

	crc = get_unaligned_le32(data);
	data += 4;
	size -= 4;
	if (crc32_be(0, data, size) != crc)
		goto err_fw_corrupted;

	if (size != *data * sizeof(struct uea_cmvs_v2) + 1)
		goto err_fw_corrupted;

	*cmvs = (void *) (data + 1);
	return *data;

err_fw_corrupted:
	uea_err(INS_TO_USBDEV(sc), "firmware %s is corrupted\n", cmv_name);
	release_firmware(*fw);
	return -EILSEQ;
}

static int uea_send_cmvs_e1(struct uea_softc *sc)
{
	int i, ret, len;
	void *cmvs_ptr;
	const struct firmware *cmvs_fw;
	int ver = 1; /* we can handle v1 cmv firmware version; */

	/* Enter in R-IDLE (cmv) until instructed otherwise */
	ret = uea_write_cmv_e1(sc, E1_SA_CNTL, 0, 1);
	if (ret < 0)
		return ret;

	/* Dump firmware version */
	ret = uea_read_cmv_e1(sc, E1_SA_INFO, 10, &sc->stats.phy.firmid);
	if (ret < 0)
		return ret;
	uea_info(INS_TO_USBDEV(sc), "ATU-R firmware version : %x\n",
			sc->stats.phy.firmid);

	/* get options */
	ret = len = request_cmvs(sc, &cmvs_ptr, &cmvs_fw, &ver);
	if (ret < 0)
		return ret;

	/* send options */
	if (ver == 1) {
		struct uea_cmvs_v1 *cmvs_v1 = cmvs_ptr;

		uea_warn(INS_TO_USBDEV(sc), "use deprecated cmvs version, "
			"please update your firmware\n");

		for (i = 0; i < len; i++) {
			ret = uea_write_cmv_e1(sc,
				get_unaligned_le32(&cmvs_v1[i].address),
				get_unaligned_le16(&cmvs_v1[i].offset),
				get_unaligned_le32(&cmvs_v1[i].data));
			if (ret < 0)
				goto out;
		}
	} else if (ver == 2) {
		struct uea_cmvs_v2 *cmvs_v2 = cmvs_ptr;

		for (i = 0; i < len; i++) {
			ret = uea_write_cmv_e1(sc,
				get_unaligned_le32(&cmvs_v2[i].address),
				(u16) get_unaligned_le32(&cmvs_v2[i].offset),
				get_unaligned_le32(&cmvs_v2[i].data));
			if (ret < 0)
				goto out;
		}
	} else {
		/* This realy should not happen */
		uea_err(INS_TO_USBDEV(sc), "bad cmvs version %d\n", ver);
		goto out;
	}

	/* Enter in R-ACT-REQ */
	ret = uea_write_cmv_e1(sc, E1_SA_CNTL, 0, 2);
	uea_vdbg(INS_TO_USBDEV(sc), "Entering in R-ACT-REQ state\n");
	uea_info(INS_TO_USBDEV(sc), "modem started, waiting "
						"synchronization...\n");
out:
	release_firmware(cmvs_fw);
	return ret;
}

static int uea_send_cmvs_e4(struct uea_softc *sc)
{
	int i, ret, len;
	void *cmvs_ptr;
	const struct firmware *cmvs_fw;
	int ver = 2; /* we can only handle v2 cmv firmware version; */

	/* Enter in R-IDLE (cmv) until instructed otherwise */
	ret = uea_write_cmv_e4(sc, 1, E4_SA_CNTL, 0, 0, 1);
	if (ret < 0)
		return ret;

	/* Dump firmware version */
	/* XXX don't read the 3th byte as it is always 6 */
	ret = uea_read_cmv_e4(sc, 2, E4_SA_INFO, 55, 0, &sc->stats.phy.firmid);
	if (ret < 0)
		return ret;
	uea_info(INS_TO_USBDEV(sc), "ATU-R firmware version : %x\n",
			sc->stats.phy.firmid);


	/* get options */
	ret = len = request_cmvs(sc, &cmvs_ptr, &cmvs_fw, &ver);
	if (ret < 0)
		return ret;

	/* send options */
	if (ver == 2) {
		struct uea_cmvs_v2 *cmvs_v2 = cmvs_ptr;

		for (i = 0; i < len; i++) {
			ret = uea_write_cmv_e4(sc, 1,
				get_unaligned_le32(&cmvs_v2[i].group),
				get_unaligned_le32(&cmvs_v2[i].address),
				get_unaligned_le32(&cmvs_v2[i].offset),
				get_unaligned_le32(&cmvs_v2[i].data));
			if (ret < 0)
				goto out;
		}
	} else {
		/* This realy should not happen */
		uea_err(INS_TO_USBDEV(sc), "bad cmvs version %d\n", ver);
		goto out;
	}

	/* Enter in R-ACT-REQ */
	ret = uea_write_cmv_e4(sc, 1, E4_SA_CNTL, 0, 0, 2);
	uea_vdbg(INS_TO_USBDEV(sc), "Entering in R-ACT-REQ state\n");
	uea_info(INS_TO_USBDEV(sc), "modem started, waiting "
						"synchronization...\n");
out:
	release_firmware(cmvs_fw);
	return ret;
}

/* Start boot post firmware modem:
 * - send reset commands through usb control pipe
 * - start workqueue for DSP loading
 * - send CMV options to modem
 */

static int uea_start_reset(struct uea_softc *sc)
{
	u16 zero = 0;	/* ;-) */
	int ret;

	uea_enters(INS_TO_USBDEV(sc));
	uea_info(INS_TO_USBDEV(sc), "(re)booting started\n");

	/* mask interrupt */
	sc->booting = 1;
	/* We need to set this here because, a ack timeout could have occured,
	 * but before we start the reboot, the ack occurs and set this to 1.
	 * So we will failed to wait Ready CMV.
	 */
	sc->cmv_ack = 0;
	UPDATE_ATM_STAT(signal, ATM_PHY_SIG_LOST);

	/* reset statistics */
	memset(&sc->stats, 0, sizeof(struct uea_stats));

	/* tell the modem that we want to boot in IDMA mode */
	uea_request(sc, UEA_SET_MODE, UEA_LOOPBACK_ON, 0, NULL);
	uea_request(sc, UEA_SET_MODE, UEA_BOOT_IDMA, 0, NULL);

	/* enter reset mode */
	uea_request(sc, UEA_SET_MODE, UEA_START_RESET, 0, NULL);

	/* original driver use 200ms, but windows driver use 100ms */
	ret = uea_wait(sc, 0, msecs_to_jiffies(100));
	if (ret < 0)
		return ret;

	/* leave reset mode */
	uea_request(sc, UEA_SET_MODE, UEA_END_RESET, 0, NULL);

	if (UEA_CHIP_VERSION(sc) != EAGLE_IV) {
		/* clear tx and rx mailboxes */
		uea_request(sc, UEA_SET_2183_DATA, UEA_MPTX_MAILBOX, 2, &zero);
		uea_request(sc, UEA_SET_2183_DATA, UEA_MPRX_MAILBOX, 2, &zero);
		uea_request(sc, UEA_SET_2183_DATA, UEA_SWAP_MAILBOX, 2, &zero);
	}

	ret = uea_wait(sc, 0, msecs_to_jiffies(1000));
	if (ret < 0)
		return ret;

	if (UEA_CHIP_VERSION(sc) == EAGLE_IV)
		sc->cmv_dsc.e4.function = E4_MAKEFUNCTION(E4_ADSLDIRECTIVE,
							E4_MODEMREADY, 1);
	else
		sc->cmv_dsc.e1.function = E1_MAKEFUNCTION(E1_ADSLDIRECTIVE,
							E1_MODEMREADY);

	/* demask interrupt */
	sc->booting = 0;

	/* start loading DSP */
	sc->pageno = 0;
	sc->ovl = 0;
	queue_work(sc->work_q, &sc->task);

	/* wait for modem ready CMV */
	ret = wait_cmv_ack(sc);
	if (ret < 0)
		return ret;

	uea_vdbg(INS_TO_USBDEV(sc), "Ready CMV received\n");

	ret = sc->send_cmvs(sc);
	if (ret < 0)
		return ret;

	sc->reset = 0;
	uea_leaves(INS_TO_USBDEV(sc));
	return ret;
}

/*
 * In case of an error wait 1s before rebooting the modem
 * if the modem don't request reboot (-EAGAIN).
 * Monitor the modem every 1s.
 */

static int uea_kthread(void *data)
{
	struct uea_softc *sc = data;
	int ret = -EAGAIN;

	set_freezable();
	uea_enters(INS_TO_USBDEV(sc));
	while (!kthread_should_stop()) {
		if (ret < 0 || sc->reset)
			ret = uea_start_reset(sc);
		if (!ret)
			ret = sc->stat(sc);
		if (ret != -EAGAIN)
			uea_wait(sc, 0, msecs_to_jiffies(1000));
		try_to_freeze();
	}
	uea_leaves(INS_TO_USBDEV(sc));
	return ret;
}

/* Load second usb firmware for ADI930 chip */
static int load_XILINX_firmware(struct uea_softc *sc)
{
	const struct firmware *fw_entry;
	int ret, size, u, ln;
	const u8 *pfw;
	u8 value;
	char *fw_name = FW_DIR "930-fpga.bin";

	uea_enters(INS_TO_USBDEV(sc));

	ret = request_firmware(&fw_entry, fw_name, &sc->usb_dev->dev);
	if (ret) {
		uea_err(INS_TO_USBDEV(sc), "firmware %s is not available\n",
		       fw_name);
		goto err0;
	}

	pfw = fw_entry->data;
	size = fw_entry->size;
	if (size != 0x577B) {
		uea_err(INS_TO_USBDEV(sc), "firmware %s is corrupted\n",
		       fw_name);
		ret = -EILSEQ;
		goto err1;
	}
	for (u = 0; u < size; u += ln) {
		ln = min(size - u, 64);
		ret = uea_request(sc, 0xe, 0, ln, pfw + u);
		if (ret < 0) {
			uea_err(INS_TO_USBDEV(sc),
			       "elsa download data failed (%d)\n", ret);
			goto err1;
		}
	}

	/* finish to send the fpga */
	ret = uea_request(sc, 0xe, 1, 0, NULL);
	if (ret < 0) {
		uea_err(INS_TO_USBDEV(sc),
				"elsa download data failed (%d)\n", ret);
		goto err1;
	}

	/* Tell the modem we finish : de-assert reset */
	value = 0;
	ret = uea_send_modem_cmd(sc->usb_dev, 0xe, 1, &value);
	if (ret < 0)
		uea_err(sc->usb_dev, "elsa de-assert failed with error"
								" %d\n", ret);

err1:
	release_firmware(fw_entry);
err0:
	uea_leaves(INS_TO_USBDEV(sc));
	return ret;
}

/* The modem send us an ack. First with check if it right */
static void uea_dispatch_cmv_e1(struct uea_softc *sc, struct intr_pkt *intr)
{
	struct cmv_dsc_e1 *dsc = &sc->cmv_dsc.e1;
	struct cmv_e1 *cmv = &intr->u.e1.s2.cmv;

	uea_enters(INS_TO_USBDEV(sc));
	if (le16_to_cpu(cmv->wPreamble) != E1_PREAMBLE)
		goto bad1;

	if (cmv->bDirection != E1_MODEMTOHOST)
		goto bad1;

	/* FIXME : ADI930 reply wrong preambule (func = 2, sub = 2) to
	 * the first MEMACCESS cmv. Ignore it...
	 */
	if (cmv->bFunction != dsc->function) {
		if (UEA_CHIP_VERSION(sc) == ADI930
				&& cmv->bFunction ==  E1_MAKEFUNCTION(2, 2)) {
			cmv->wIndex = cpu_to_le16(dsc->idx);
			put_unaligned_le32(dsc->address,
						&cmv->dwSymbolicAddress);
			cmv->wOffsetAddress = cpu_to_le16(dsc->offset);
		} else
			goto bad2;
	}

	if (cmv->bFunction == E1_MAKEFUNCTION(E1_ADSLDIRECTIVE,
							E1_MODEMREADY)) {
		wake_up_cmv_ack(sc);
		uea_leaves(INS_TO_USBDEV(sc));
		return;
	}

	/* in case of MEMACCESS */
	if (le16_to_cpu(cmv->wIndex) != dsc->idx ||
	    get_unaligned_le32(&cmv->dwSymbolicAddress) != dsc->address ||
	    le16_to_cpu(cmv->wOffsetAddress) != dsc->offset)
		goto bad2;

	sc->data = get_unaligned_le32(&cmv->dwData);
	sc->data = sc->data << 16 | sc->data >> 16;

	wake_up_cmv_ack(sc);
	uea_leaves(INS_TO_USBDEV(sc));
	return;

bad2:
	uea_err(INS_TO_USBDEV(sc), "unexpected cmv received, "
			"Function : %d, Subfunction : %d\n",
			E1_FUNCTION_TYPE(cmv->bFunction),
			E1_FUNCTION_SUBTYPE(cmv->bFunction));
	uea_leaves(INS_TO_USBDEV(sc));
	return;

bad1:
	uea_err(INS_TO_USBDEV(sc), "invalid cmv received, "
			"wPreamble %d, bDirection %d\n",
			le16_to_cpu(cmv->wPreamble), cmv->bDirection);
	uea_leaves(INS_TO_USBDEV(sc));
}

/* The modem send us an ack. First with check if it right */
static void uea_dispatch_cmv_e4(struct uea_softc *sc, struct intr_pkt *intr)
{
	struct cmv_dsc_e4 *dsc = &sc->cmv_dsc.e4;
	struct cmv_e4 *cmv = &intr->u.e4.s2.cmv;

	uea_enters(INS_TO_USBDEV(sc));
	uea_dbg(INS_TO_USBDEV(sc), "cmv %x %x %x %x %x %x\n",
		be16_to_cpu(cmv->wGroup), be16_to_cpu(cmv->wFunction),
		be16_to_cpu(cmv->wOffset), be16_to_cpu(cmv->wAddress),
		be32_to_cpu(cmv->dwData[0]), be32_to_cpu(cmv->dwData[1]));

	if (be16_to_cpu(cmv->wFunction) != dsc->function)
		goto bad2;

	if (be16_to_cpu(cmv->wFunction) == E4_MAKEFUNCTION(E4_ADSLDIRECTIVE,
						E4_MODEMREADY, 1)) {
		wake_up_cmv_ack(sc);
		uea_leaves(INS_TO_USBDEV(sc));
		return;
	}

	/* in case of MEMACCESS */
	if (be16_to_cpu(cmv->wOffset) != dsc->offset ||
	    be16_to_cpu(cmv->wGroup) != dsc->group ||
	    be16_to_cpu(cmv->wAddress) != dsc->address)
		goto bad2;

	sc->data = be32_to_cpu(cmv->dwData[0]);
	sc->data1 = be32_to_cpu(cmv->dwData[1]);
	wake_up_cmv_ack(sc);
	uea_leaves(INS_TO_USBDEV(sc));
	return;

bad2:
	uea_err(INS_TO_USBDEV(sc), "unexpected cmv received, "
			"Function : %d, Subfunction : %d\n",
			E4_FUNCTION_TYPE(cmv->wFunction),
			E4_FUNCTION_SUBTYPE(cmv->wFunction));
	uea_leaves(INS_TO_USBDEV(sc));
	return;
}

static void uea_schedule_load_page_e1(struct uea_softc *sc,
						struct intr_pkt *intr)
{
	sc->pageno = intr->e1_bSwapPageNo;
	sc->ovl = intr->e1_bOvl >> 4 | intr->e1_bOvl << 4;
	queue_work(sc->work_q, &sc->task);
}

static void uea_schedule_load_page_e4(struct uea_softc *sc,
						struct intr_pkt *intr)
{
	sc->pageno = intr->e4_bSwapPageNo;
	queue_work(sc->work_q, &sc->task);
}

/*
 * interrupt handler
 */
static void uea_intr(struct urb *urb)
{
	struct uea_softc *sc = urb->context;
	struct intr_pkt *intr = urb->transfer_buffer;
	int status = urb->status;

	uea_enters(INS_TO_USBDEV(sc));

	if (unlikely(status < 0)) {
		uea_err(INS_TO_USBDEV(sc), "uea_intr() failed with %d\n",
		       status);
		return;
	}

	/* device-to-host interrupt */
	if (intr->bType != 0x08 || sc->booting) {
		uea_err(INS_TO_USBDEV(sc), "wrong interrupt\n");
		goto resubmit;
	}

	switch (le16_to_cpu(intr->wInterrupt)) {
	case INT_LOADSWAPPAGE:
		sc->schedule_load_page(sc, intr);
		break;

	case INT_INCOMINGCMV:
		sc->dispatch_cmv(sc, intr);
		break;

	default:
		uea_err(INS_TO_USBDEV(sc), "unknown interrupt %u\n",
		       le16_to_cpu(intr->wInterrupt));
	}

resubmit:
	usb_submit_urb(sc->urb_int, GFP_ATOMIC);
}

/*
 * Start the modem : init the data and start kernel thread
 */
static int uea_boot(struct uea_softc *sc)
{
	int ret, size;
	struct intr_pkt *intr;

	uea_enters(INS_TO_USBDEV(sc));

	if (UEA_CHIP_VERSION(sc) == EAGLE_IV) {
		size = E4_INTR_PKT_SIZE;
		sc->dispatch_cmv = uea_dispatch_cmv_e4;
		sc->schedule_load_page = uea_schedule_load_page_e4;
		sc->stat = uea_stat_e4;
		sc->send_cmvs = uea_send_cmvs_e4;
		INIT_WORK(&sc->task, uea_load_page_e4);
	} else {
		size = E1_INTR_PKT_SIZE;
		sc->dispatch_cmv = uea_dispatch_cmv_e1;
		sc->schedule_load_page = uea_schedule_load_page_e1;
		sc->stat = uea_stat_e1;
		sc->send_cmvs = uea_send_cmvs_e1;
		INIT_WORK(&sc->task, uea_load_page_e1);
	}

	init_waitqueue_head(&sc->sync_q);

	sc->work_q = create_workqueue("ueagle-dsp");
	if (!sc->work_q) {
		uea_err(INS_TO_USBDEV(sc), "cannot allocate workqueue\n");
		uea_leaves(INS_TO_USBDEV(sc));
		return -ENOMEM;
	}

	if (UEA_CHIP_VERSION(sc) == ADI930)
		load_XILINX_firmware(sc);

	intr = kmalloc(size, GFP_KERNEL);
	if (!intr) {
		uea_err(INS_TO_USBDEV(sc),
		       "cannot allocate interrupt package\n");
		goto err0;
	}

	sc->urb_int = usb_alloc_urb(0, GFP_KERNEL);
	if (!sc->urb_int) {
		uea_err(INS_TO_USBDEV(sc), "cannot allocate interrupt URB\n");
		goto err1;
	}

	usb_fill_int_urb(sc->urb_int, sc->usb_dev,
			 usb_rcvintpipe(sc->usb_dev, UEA_INTR_PIPE),
			 intr, size, uea_intr, sc,
			 sc->usb_dev->actconfig->interface[0]->altsetting[0].
			 endpoint[0].desc.bInterval);

	ret = usb_submit_urb(sc->urb_int, GFP_KERNEL);
	if (ret < 0) {
		uea_err(INS_TO_USBDEV(sc),
		       "urb submition failed with error %d\n", ret);
		goto err1;
	}

	sc->kthread = kthread_run(uea_kthread, sc, "ueagle-atm");
	if (sc->kthread == ERR_PTR(-ENOMEM)) {
		uea_err(INS_TO_USBDEV(sc), "failed to create thread\n");
		goto err2;
	}

	uea_leaves(INS_TO_USBDEV(sc));
	return 0;

err2:
	usb_kill_urb(sc->urb_int);
err1:
	usb_free_urb(sc->urb_int);
	sc->urb_int = NULL;
	kfree(intr);
err0:
	destroy_workqueue(sc->work_q);
	uea_leaves(INS_TO_USBDEV(sc));
	return -ENOMEM;
}

/*
 * Stop the modem : kill kernel thread and free data
 */
static void uea_stop(struct uea_softc *sc)
{
	int ret;
	uea_enters(INS_TO_USBDEV(sc));
	ret = kthread_stop(sc->kthread);
	uea_dbg(INS_TO_USBDEV(sc), "kthread finish with status %d\n", ret);

	uea_request(sc, UEA_SET_MODE, UEA_LOOPBACK_ON, 0, NULL);

	usb_kill_urb(sc->urb_int);
	kfree(sc->urb_int->transfer_buffer);
	usb_free_urb(sc->urb_int);

	/* stop any pending boot process, when no one can schedule work */
	destroy_workqueue(sc->work_q);

	if (sc->dsp_firm)
		release_firmware(sc->dsp_firm);
	uea_leaves(INS_TO_USBDEV(sc));
}

/* syfs interface */
static struct uea_softc *dev_to_uea(struct device *dev)
{
	struct usb_interface *intf;
	struct usbatm_data *usbatm;

	intf = to_usb_interface(dev);
	if (!intf)
		return NULL;

	usbatm = usb_get_intfdata(intf);
	if (!usbatm)
		return NULL;

	return usbatm->driver_data;
}

static ssize_t read_status(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int ret = -ENODEV;
	struct uea_softc *sc;

	mutex_lock(&uea_mutex);
	sc = dev_to_uea(dev);
	if (!sc)
		goto out;
	ret = snprintf(buf, 10, "%08x\n", sc->stats.phy.state);
out:
	mutex_unlock(&uea_mutex);
	return ret;
}

static ssize_t reboot(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int ret = -ENODEV;
	struct uea_softc *sc;

	mutex_lock(&uea_mutex);
	sc = dev_to_uea(dev);
	if (!sc)
		goto out;
	sc->reset = 1;
	ret = count;
out:
	mutex_unlock(&uea_mutex);
	return ret;
}

static DEVICE_ATTR(stat_status, S_IWUGO | S_IRUGO, read_status, reboot);

static ssize_t read_human_status(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret = -ENODEV;
	int modem_state;
	struct uea_softc *sc;

	mutex_lock(&uea_mutex);
	sc = dev_to_uea(dev);
	if (!sc)
		goto out;

	if (UEA_CHIP_VERSION(sc) == EAGLE_IV) {
		switch (sc->stats.phy.state) {
		case 0x0:	/* not yet synchronized */
		case 0x1:
		case 0x3:
		case 0x4:
			modem_state = 0;
			break;
		case 0x5:	/* initialization */
		case 0x6:
		case 0x9:
		case 0xa:
			modem_state = 1;
			break;
		case 0x7:	/* operational */
			modem_state = 2;
			break;
		case 0x2:	/* fail ... */
			modem_state = 3;
			break;
		default:	/* unknown */
			modem_state = 4;
			break;
		}
	} else
		modem_state = GET_STATUS(sc->stats.phy.state);

	switch (modem_state) {
	case 0:
		ret = sprintf(buf, "Modem is booting\n");
		break;
	case 1:
		ret = sprintf(buf, "Modem is initializing\n");
		break;
	case 2:
		ret = sprintf(buf, "Modem is operational\n");
		break;
	case 3:
		ret = sprintf(buf, "Modem synchronization failed\n");
		break;
	default:
		ret = sprintf(buf, "Modem state is unknown\n");
		break;
	}
out:
	mutex_unlock(&uea_mutex);
	return ret;
}

static DEVICE_ATTR(stat_human_status, S_IWUGO | S_IRUGO,
				read_human_status, NULL);

static ssize_t read_delin(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int ret = -ENODEV;
	struct uea_softc *sc;
	char *delin = "GOOD";

	mutex_lock(&uea_mutex);
	sc = dev_to_uea(dev);
	if (!sc)
		goto out;

	if (UEA_CHIP_VERSION(sc) == EAGLE_IV) {
		if (sc->stats.phy.flags & 0x4000)
			delin = "RESET";
		else if (sc->stats.phy.flags & 0x0001)
			delin = "LOSS";
	} else {
		if (sc->stats.phy.flags & 0x0C00)
			delin = "ERROR";
		else if (sc->stats.phy.flags & 0x0030)
			delin = "LOSS";
	}

	ret = sprintf(buf, "%s\n", delin);
out:
	mutex_unlock(&uea_mutex);
	return ret;
}

static DEVICE_ATTR(stat_delin, S_IWUGO | S_IRUGO, read_delin, NULL);

#define UEA_ATTR(name, reset)					\
								\
static ssize_t read_##name(struct device *dev,			\
		struct device_attribute *attr, char *buf)	\
{								\
	int ret = -ENODEV;					\
	struct uea_softc *sc;					\
								\
	mutex_lock(&uea_mutex);					\
	sc = dev_to_uea(dev);					\
	if (!sc)						\
		goto out;					\
	ret = snprintf(buf, 10, "%08x\n", sc->stats.phy.name);	\
	if (reset)						\
		sc->stats.phy.name = 0;				\
out:								\
	mutex_unlock(&uea_mutex);				\
	return ret;						\
}								\
								\
static DEVICE_ATTR(stat_##name, S_IRUGO, read_##name, NULL)

UEA_ATTR(mflags, 1);
UEA_ATTR(vidcpe, 0);
UEA_ATTR(usrate, 0);
UEA_ATTR(dsrate, 0);
UEA_ATTR(usattenuation, 0);
UEA_ATTR(dsattenuation, 0);
UEA_ATTR(usmargin, 0);
UEA_ATTR(dsmargin, 0);
UEA_ATTR(txflow, 0);
UEA_ATTR(rxflow, 0);
UEA_ATTR(uscorr, 0);
UEA_ATTR(dscorr, 0);
UEA_ATTR(usunc, 0);
UEA_ATTR(dsunc, 0);
UEA_ATTR(firmid, 0);

/* Retrieve the device End System Identifier (MAC) */

#define htoi(x) (isdigit(x) ? x-'0' : toupper(x)-'A'+10)
static int uea_getesi(struct uea_softc *sc, u_char * esi)
{
	unsigned char mac_str[2 * ETH_ALEN + 1];
	int i;
	if (usb_string
	    (sc->usb_dev, sc->usb_dev->descriptor.iSerialNumber, mac_str,
	     sizeof(mac_str)) != 2 * ETH_ALEN)
		return 1;

	for (i = 0; i < ETH_ALEN; i++)
		esi[i] = htoi(mac_str[2 * i]) * 16 + htoi(mac_str[2 * i + 1]);

	return 0;
}

/* ATM stuff */
static int uea_atm_open(struct usbatm_data *usbatm, struct atm_dev *atm_dev)
{
	struct uea_softc *sc = usbatm->driver_data;

	return uea_getesi(sc, atm_dev->esi);
}

static int uea_heavy(struct usbatm_data *usbatm, struct usb_interface *intf)
{
	struct uea_softc *sc = usbatm->driver_data;

	wait_event_interruptible(sc->sync_q, IS_OPERATIONAL(sc));

	return 0;

}

static int claim_interface(struct usb_device *usb_dev,
			   struct usbatm_data *usbatm, int ifnum)
{
	int ret;
	struct usb_interface *intf = usb_ifnum_to_if(usb_dev, ifnum);

	if (!intf) {
		uea_err(usb_dev, "interface %d not found\n", ifnum);
		return -ENODEV;
	}

	ret = usb_driver_claim_interface(&uea_driver, intf, usbatm);
	if (ret != 0)
		uea_err(usb_dev, "can't claim interface %d, error %d\n", ifnum,
		       ret);
	return ret;
}

static struct attribute *attrs[] = {
	&dev_attr_stat_status.attr,
	&dev_attr_stat_mflags.attr,
	&dev_attr_stat_human_status.attr,
	&dev_attr_stat_delin.attr,
	&dev_attr_stat_vidcpe.attr,
	&dev_attr_stat_usrate.attr,
	&dev_attr_stat_dsrate.attr,
	&dev_attr_stat_usattenuation.attr,
	&dev_attr_stat_dsattenuation.attr,
	&dev_attr_stat_usmargin.attr,
	&dev_attr_stat_dsmargin.attr,
	&dev_attr_stat_txflow.attr,
	&dev_attr_stat_rxflow.attr,
	&dev_attr_stat_uscorr.attr,
	&dev_attr_stat_dscorr.attr,
	&dev_attr_stat_usunc.attr,
	&dev_attr_stat_dsunc.attr,
	&dev_attr_stat_firmid.attr,
	NULL,
};
static struct attribute_group attr_grp = {
	.attrs = attrs,
};

static int uea_bind(struct usbatm_data *usbatm, struct usb_interface *intf,
		   const struct usb_device_id *id)
{
	struct usb_device *usb = interface_to_usbdev(intf);
	struct uea_softc *sc;
	int ret, ifnum = intf->altsetting->desc.bInterfaceNumber;
	unsigned int alt;

	uea_enters(usb);

	/* interface 0 is for firmware/monitoring */
	if (ifnum != UEA_INTR_IFACE_NO)
		return -ENODEV;

	usbatm->flags = (sync_wait[modem_index] ? 0 : UDSL_SKIP_HEAVY_INIT);

	/* interface 1 is for outbound traffic */
	ret = claim_interface(usb, usbatm, UEA_US_IFACE_NO);
	if (ret < 0)
		return ret;

	/* ADI930 has only 2 interfaces and inbound traffic is on interface 1 */
	if (UEA_CHIP_VERSION(id) != ADI930) {
		/* interface 2 is for inbound traffic */
		ret = claim_interface(usb, usbatm, UEA_DS_IFACE_NO);
		if (ret < 0)
			return ret;
	}

	sc = kzalloc(sizeof(struct uea_softc), GFP_KERNEL);
	if (!sc) {
		uea_err(usb, "uea_init: not enough memory !\n");
		return -ENOMEM;
	}

	sc->usb_dev = usb;
	usbatm->driver_data = sc;
	sc->usbatm = usbatm;
	sc->modem_index = (modem_index < NB_MODEM) ? modem_index++ : 0;
	sc->driver_info = id->driver_info;

	/* first try to use module parameter */
	if (annex[sc->modem_index] == 1)
		sc->annex = ANNEXA;
	else if (annex[sc->modem_index] == 2)
		sc->annex = ANNEXB;
	/* try to autodetect annex */
	else if (sc->driver_info & AUTO_ANNEX_A)
		sc->annex = ANNEXA;
	else if (sc->driver_info & AUTO_ANNEX_B)
		sc->annex = ANNEXB;
	else
		sc->annex = (le16_to_cpu
		(sc->usb_dev->descriptor.bcdDevice) & 0x80) ? ANNEXB : ANNEXA;

	alt = altsetting[sc->modem_index];
	/* ADI930 don't support iso */
	if (UEA_CHIP_VERSION(id) != ADI930 && alt > 0) {
		if (alt <= 8 &&
			usb_set_interface(usb, UEA_DS_IFACE_NO, alt) == 0) {
			uea_dbg(usb, "set alternate %u for 2 interface\n", alt);
			uea_info(usb, "using iso mode\n");
			usbatm->flags |= UDSL_USE_ISOC | UDSL_IGNORE_EILSEQ;
		} else {
			uea_err(usb, "setting alternate %u failed for "
					"2 interface, using bulk mode\n", alt);
		}
	}

	ret = sysfs_create_group(&intf->dev.kobj, &attr_grp);
	if (ret < 0)
		goto error;

	ret = uea_boot(sc);
	if (ret < 0)
		goto error_rm_grp;

	return 0;

error_rm_grp:
	sysfs_remove_group(&intf->dev.kobj, &attr_grp);
error:
	kfree(sc);
	return ret;
}

static void uea_unbind(struct usbatm_data *usbatm, struct usb_interface *intf)
{
	struct uea_softc *sc = usbatm->driver_data;

	sysfs_remove_group(&intf->dev.kobj, &attr_grp);
	uea_stop(sc);
	kfree(sc);
}

static struct usbatm_driver uea_usbatm_driver = {
	.driver_name = "ueagle-atm",
	.bind = uea_bind,
	.atm_start = uea_atm_open,
	.unbind = uea_unbind,
	.heavy_init = uea_heavy,
	.bulk_in = UEA_BULK_DATA_PIPE,
	.bulk_out = UEA_BULK_DATA_PIPE,
	.isoc_in = UEA_ISO_DATA_PIPE,
};

static int uea_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *usb = interface_to_usbdev(intf);

	uea_enters(usb);
	uea_info(usb, "ADSL device founded vid (%#X) pid (%#X) Rev (%#X): %s\n",
		le16_to_cpu(usb->descriptor.idVendor),
		le16_to_cpu(usb->descriptor.idProduct),
		le16_to_cpu(usb->descriptor.bcdDevice),
		chip_name[UEA_CHIP_VERSION(id)]);

	usb_reset_device(usb);

	if (UEA_IS_PREFIRM(id))
		return uea_load_firmware(usb, UEA_CHIP_VERSION(id));

	return usbatm_usb_probe(intf, id, &uea_usbatm_driver);
}

static void uea_disconnect(struct usb_interface *intf)
{
	struct usb_device *usb = interface_to_usbdev(intf);
	int ifnum = intf->altsetting->desc.bInterfaceNumber;
	uea_enters(usb);

	/* ADI930 has 2 interfaces and eagle 3 interfaces.
	 * Pre-firmware device has one interface
	 */
	if (usb->config->desc.bNumInterfaces != 1 && ifnum == 0) {
		mutex_lock(&uea_mutex);
		usbatm_usb_disconnect(intf);
		mutex_unlock(&uea_mutex);
		uea_info(usb, "ADSL device removed\n");
	}

	uea_leaves(usb);
}

/*
 * List of supported VID/PID
 */
static const struct usb_device_id uea_ids[] = {
	{USB_DEVICE(ANALOG_VID,	ADI930_PID_PREFIRM),
		.driver_info = ADI930 | PREFIRM},
	{USB_DEVICE(ANALOG_VID,	ADI930_PID_PSTFIRM),
		.driver_info = ADI930 | PSTFIRM},
	{USB_DEVICE(ANALOG_VID,	EAGLE_I_PID_PREFIRM),
		.driver_info = EAGLE_I | PREFIRM},
	{USB_DEVICE(ANALOG_VID,	EAGLE_I_PID_PSTFIRM),
		.driver_info = EAGLE_I | PSTFIRM},
	{USB_DEVICE(ANALOG_VID,	EAGLE_II_PID_PREFIRM),
		.driver_info = EAGLE_II | PREFIRM},
	{USB_DEVICE(ANALOG_VID,	EAGLE_II_PID_PSTFIRM),
		.driver_info = EAGLE_II | PSTFIRM},
	{USB_DEVICE(ANALOG_VID,	EAGLE_IIC_PID_PREFIRM),
		.driver_info = EAGLE_II | PREFIRM},
	{USB_DEVICE(ANALOG_VID,	EAGLE_IIC_PID_PSTFIRM),
		.driver_info = EAGLE_II | PSTFIRM},
	{USB_DEVICE(ANALOG_VID,	EAGLE_III_PID_PREFIRM),
		.driver_info = EAGLE_III | PREFIRM},
	{USB_DEVICE(ANALOG_VID,	EAGLE_III_PID_PSTFIRM),
		.driver_info = EAGLE_III | PSTFIRM},
	{USB_DEVICE(ANALOG_VID,	EAGLE_IV_PID_PREFIRM),
		.driver_info = EAGLE_IV | PREFIRM},
	{USB_DEVICE(ANALOG_VID,	EAGLE_IV_PID_PSTFIRM),
		.driver_info = EAGLE_IV | PSTFIRM},
	{USB_DEVICE(DEVOLO_VID,	DEVOLO_EAGLE_I_A_PID_PREFIRM),
		.driver_info = EAGLE_I | PREFIRM},
	{USB_DEVICE(DEVOLO_VID,	DEVOLO_EAGLE_I_A_PID_PSTFIRM),
		.driver_info = EAGLE_I | PSTFIRM | AUTO_ANNEX_A},
	{USB_DEVICE(DEVOLO_VID,	DEVOLO_EAGLE_I_B_PID_PREFIRM),
		.driver_info = EAGLE_I | PREFIRM},
	{USB_DEVICE(DEVOLO_VID,	DEVOLO_EAGLE_I_B_PID_PSTFIRM),
		.driver_info = EAGLE_I | PSTFIRM | AUTO_ANNEX_B},
	{USB_DEVICE(DEVOLO_VID,	DEVOLO_EAGLE_II_A_PID_PREFIRM),
		.driver_info = EAGLE_II | PREFIRM},
	{USB_DEVICE(DEVOLO_VID,	DEVOLO_EAGLE_II_A_PID_PSTFIRM),
		.driver_info = EAGLE_II | PSTFIRM | AUTO_ANNEX_A},
	{USB_DEVICE(DEVOLO_VID,	DEVOLO_EAGLE_II_B_PID_PREFIRM),
		.driver_info = EAGLE_II | PREFIRM},
	{USB_DEVICE(DEVOLO_VID,	DEVOLO_EAGLE_II_B_PID_PSTFIRM),
		.driver_info = EAGLE_II | PSTFIRM | AUTO_ANNEX_B},
	{USB_DEVICE(ELSA_VID,	ELSA_PID_PREFIRM),
		.driver_info = ADI930 | PREFIRM},
	{USB_DEVICE(ELSA_VID,	ELSA_PID_PSTFIRM),
		.driver_info = ADI930 | PSTFIRM},
	{USB_DEVICE(ELSA_VID,	ELSA_PID_A_PREFIRM),
		.driver_info = ADI930 | PREFIRM},
	{USB_DEVICE(ELSA_VID,	ELSA_PID_A_PSTFIRM),
		.driver_info = ADI930 | PSTFIRM | AUTO_ANNEX_A},
	{USB_DEVICE(ELSA_VID,	ELSA_PID_B_PREFIRM),
		.driver_info = ADI930 | PREFIRM},
	{USB_DEVICE(ELSA_VID,	ELSA_PID_B_PSTFIRM),
		.driver_info = ADI930 | PSTFIRM | AUTO_ANNEX_B},
	{USB_DEVICE(USR_VID,	MILLER_A_PID_PREFIRM),
		.driver_info = EAGLE_I | PREFIRM},
	{USB_DEVICE(USR_VID,	MILLER_A_PID_PSTFIRM),
		.driver_info = EAGLE_I | PSTFIRM  | AUTO_ANNEX_A},
	{USB_DEVICE(USR_VID,	MILLER_B_PID_PREFIRM),
		.driver_info = EAGLE_I | PREFIRM},
	{USB_DEVICE(USR_VID,	MILLER_B_PID_PSTFIRM),
		.driver_info = EAGLE_I | PSTFIRM  | AUTO_ANNEX_B},
	{USB_DEVICE(USR_VID,	HEINEKEN_A_PID_PREFIRM),
		.driver_info = EAGLE_I | PREFIRM},
	{USB_DEVICE(USR_VID,	HEINEKEN_A_PID_PSTFIRM),
		.driver_info = EAGLE_I | PSTFIRM | AUTO_ANNEX_A},
	{USB_DEVICE(USR_VID,	HEINEKEN_B_PID_PREFIRM),
		.driver_info = EAGLE_I | PREFIRM},
	{USB_DEVICE(USR_VID,	HEINEKEN_B_PID_PSTFIRM),
		.driver_info = EAGLE_I | PSTFIRM | AUTO_ANNEX_B},
	{}
};

/*
 * USB driver descriptor
 */
static struct usb_driver uea_driver = {
	.name = "ueagle-atm",
	.id_table = uea_ids,
	.probe = uea_probe,
	.disconnect = uea_disconnect,
};

MODULE_DEVICE_TABLE(usb, uea_ids);

/**
 * uea_init - Initialize the module.
 *      Register to USB subsystem
 */
static int __init uea_init(void)
{
	printk(KERN_INFO "[ueagle-atm] driver " EAGLEUSBVERSION " loaded\n");

	usb_register(&uea_driver);

	return 0;
}

module_init(uea_init);

/**
 * uea_exit  -  Destroy module
 *    Deregister with USB subsystem
 */
static void __exit uea_exit(void)
{
	/*
	 * This calls automatically the uea_disconnect method if necessary:
	 */
	usb_deregister(&uea_driver);

	printk(KERN_INFO "[ueagle-atm] driver unloaded\n");
}

module_exit(uea_exit);

MODULE_AUTHOR("Damien Bergamini/Matthieu Castet/Stanislaw W. Gruszka");
MODULE_DESCRIPTION("ADI 930/Eagle USB ADSL Modem driver");
MODULE_LICENSE("Dual BSD/GPL");
