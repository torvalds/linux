/*-
 * Copyright (c) 2003, 2004
 *	Damien Bergamini <damien.bergamini@free.fr>. All rights reserved.
 *
 * Copyright (c) 2005 Matthieu Castet <castet.matthieu@free.fr>
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
#include <linux/kthread.h>
#include <linux/version.h>
#include <linux/mutex.h>
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
					__FUNCTION__, ##args); \
	} while (0)

#define uea_vdbg(usb_dev, format, args...)	\
	do { \
		if (debug >= 2) \
			dev_dbg(&(usb_dev)->dev, \
				"[ueagle-atm vdbg]  " format, ##args); \
	} while (0)

#define uea_enters(usb_dev) \
	uea_vdbg(usb_dev, "entering %s\n", __FUNCTION__)

#define uea_leaves(usb_dev) \
	uea_vdbg(usb_dev, "leaving  %s\n", __FUNCTION__)

#define uea_err(usb_dev, format,args...) \
	dev_err(&(usb_dev)->dev ,"[UEAGLE-ATM] " format , ##args)

#define uea_warn(usb_dev, format,args...) \
	dev_warn(&(usb_dev)->dev ,"[Ueagle-atm] " format, ##args)

#define uea_info(usb_dev, format,args...) \
	dev_info(&(usb_dev)->dev ,"[ueagle-atm] " format, ##args)

struct uea_cmvs {
	u32 address;
	u16 offset;
	u32 data;
} __attribute__ ((packed));

struct uea_softc {
	struct usb_device *usb_dev;
	struct usbatm_data *usbatm;

	int modem_index;
	unsigned int driver_info;

	int booting;
	int reset;

	wait_queue_head_t sync_q;

	struct task_struct *kthread;
	u32 data;
	wait_queue_head_t cmv_ack_wait;
	int cmv_ack;

	struct work_struct task;
	u16 pageno;
	u16 ovl;

	const struct firmware *dsp_firm;
	struct urb *urb_int;

	u8 cmv_function;
	u16 cmv_idx;
	u32 cmv_address;
	u16 cmv_offset;

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

/*
 * Sagem USB IDs
 */
#define EAGLE_VID		0x1110
#define EAGLE_I_PID_PREFIRM	0x9010	/* Eagle I */
#define EAGLE_I_PID_PSTFIRM	0x900F	/* Eagle I */

#define EAGLE_IIC_PID_PREFIRM	0x9024	/* Eagle IIC */
#define EAGLE_IIC_PID_PSTFIRM	0x9023	/* Eagle IIC */

#define EAGLE_II_PID_PREFIRM	0x9022	/* Eagle II */
#define EAGLE_II_PID_PSTFIRM	0x9021	/* Eagle II */

/*
 *  Eagle III Pid
 */
#define EAGLE_III_PID_PREFIRM	0x9032	/* Eagle III */
#define EAGLE_III_PID_PSTFIRM	0x9031	/* Eagle III */

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
enum {
	ADI930 = 0,
	EAGLE_I,
	EAGLE_II,
	EAGLE_III
};

/* macros for both struct usb_device_id and struct uea_softc */
#define UEA_IS_PREFIRM(x) \
	(!((x)->driver_info & PSTFIRM))
#define UEA_CHIP_VERSION(x) \
	((x)->driver_info & 0xf)

#define IS_ISDN(usb_dev) \
	(le16_to_cpu((usb_dev)->descriptor.bcdDevice) & 0x80)

#define INS_TO_USBDEV(ins) ins->usb_dev

#define GET_STATUS(data) \
	((data >> 8) & 0xf)
#define IS_OPERATIONAL(sc) \
	(GET_STATUS(sc->stats.phy.state) == 2)

/*
 * Set of macros to handle unaligned data in the firmware blob.
 * The FW_GET_BYTE() macro is provided only for consistency.
 */

#define FW_GET_BYTE(p)	*((__u8 *) (p))
#define FW_GET_WORD(p)	le16_to_cpu(get_unaligned((__le16 *) (p)))
#define FW_GET_LONG(p)	le32_to_cpu(get_unaligned((__le32 *) (p)))

#define FW_DIR "ueagle-atm/"
#define NB_MODEM 4

#define BULK_TIMEOUT 300
#define CTRL_TIMEOUT 1000

#define ACK_TIMEOUT msecs_to_jiffies(3000)

#define UEA_INTR_IFACE_NO 	0
#define UEA_US_IFACE_NO		1
#define UEA_DS_IFACE_NO		2

#define FASTEST_ISO_INTF	8

#define UEA_BULK_DATA_PIPE	0x02
#define UEA_IDMA_PIPE		0x04
#define UEA_INTR_PIPE		0x04
#define UEA_ISO_DATA_PIPE	0x08

#define UEA_SET_BLOCK    	0x0001
#define UEA_SET_MODE     	0x0003
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

/* structure describing a block within a DSP page */
struct block_info {
	__le16 wHdr;
#define UEA_BIHDR 0xabcd
	__le16 wAddress;
	__le16 wSize;
	__le16 wOvlOffset;
	__le16 wOvl;		/* overlay */
	__le16 wLast;
} __attribute__ ((packed));
#define BLOCK_INFO_SIZE 12

/* structure representing a CMV (Configuration and Management Variable) */
struct cmv {
	__le16 wPreamble;
#define PREAMBLE 0x535c
	__u8 bDirection;
#define MODEMTOHOST 0x01
#define HOSTTOMODEM 0x10
	__u8 bFunction;
#define FUNCTION_TYPE(f)    ((f) >> 4)
#define MEMACCESS	0x1
#define ADSLDIRECTIVE	0x7

#define FUNCTION_SUBTYPE(f) ((f) & 0x0f)
/* for MEMACCESS */
#define REQUESTREAD	0x0
#define REQUESTWRITE	0x1
#define REPLYREAD	0x2
#define REPLYWRITE	0x3
/* for ADSLDIRECTIVE */
#define KERNELREADY	0x0
#define MODEMREADY	0x1

#define MAKEFUNCTION(t, s) (((t) & 0xf) << 4 | ((s) & 0xf))
	__le16 wIndex;
	__le32 dwSymbolicAddress;
#define MAKESA(a, b, c, d)						\
	(((c) & 0xff) << 24 |						\
	 ((d) & 0xff) << 16 |						\
	 ((a) & 0xff) << 8  |						\
	 ((b) & 0xff))
#define GETSA1(a) ((a >> 8) & 0xff)
#define GETSA2(a) (a & 0xff)
#define GETSA3(a) ((a >> 24) & 0xff)
#define GETSA4(a) ((a >> 16) & 0xff)

#define SA_CNTL MAKESA('C', 'N', 'T', 'L')
#define SA_DIAG MAKESA('D', 'I', 'A', 'G')
#define SA_INFO MAKESA('I', 'N', 'F', 'O')
#define SA_OPTN MAKESA('O', 'P', 'T', 'N')
#define SA_RATE MAKESA('R', 'A', 'T', 'E')
#define SA_STAT MAKESA('S', 'T', 'A', 'T')
	__le16 wOffsetAddress;
	__le32 dwData;
} __attribute__ ((packed));
#define CMV_SIZE 16

/* structure representing swap information */
struct swap_info {
	__u8 bSwapPageNo;
	__u8 bOvl;		/* overlay */
} __attribute__ ((packed));

/* structure representing interrupt data */
struct intr_pkt {
	__u8 bType;
	__u8 bNotification;
	__le16 wValue;
	__le16 wIndex;
	__le16 wLength;
	__le16 wInterrupt;
#define INT_LOADSWAPPAGE 0x0001
#define INT_INCOMINGCMV  0x0002
	union {
		struct {
			struct swap_info swapinfo;
			__le16 wDataSize;
		} __attribute__ ((packed)) s1;

		struct {
			struct cmv cmv;
			__le16 wDataSize;
		} __attribute__ ((packed)) s2;
	} __attribute__ ((packed)) u;
#define bSwapPageNo	u.s1.swapinfo.bSwapPageNo
#define bOvl		u.s1.swapinfo.bOvl
} __attribute__ ((packed));
#define INTR_PKT_SIZE 28

static struct usb_driver uea_driver;
static DEFINE_MUTEX(uea_mutex);
static const char *chip_name[] = {"ADI930", "Eagle I", "Eagle II", "Eagle III"};

static int modem_index;
static unsigned int debug;
static int use_iso[NB_MODEM] = {[0 ... (NB_MODEM - 1)] = 1};
static int sync_wait[NB_MODEM];
static char *cmv_file[NB_MODEM];

module_param(debug, uint, 0644);
MODULE_PARM_DESC(debug, "module debug level (0=off,1=on,2=verbose)");
module_param_array(use_iso, bool, NULL, 0644);
MODULE_PARM_DESC(use_iso, "use isochronous usb pipe for incoming traffic");
module_param_array(sync_wait, bool, NULL, 0644);
MODULE_PARM_DESC(sync_wait, "wait the synchronisation before starting ATM");
module_param_array(cmv_file, charp, NULL, 0644);
MODULE_PARM_DESC(cmv_file,
		"file name with configuration and management variables");

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
		u16 addr, u16 size, u8 * buff)
{
	int ret = -ENOMEM;
	u8 *xfer_buff;

	xfer_buff = kmalloc(size, GFP_KERNEL);
	if (xfer_buff) {
		memcpy(xfer_buff, buff, size);
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

static void uea_upload_pre_firmware(const struct firmware *fw_entry, void *context)
{
	struct usb_device *usb = context;
	u8 *pfw, value;
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

	crc = FW_GET_LONG(pfw);
	pfw += 4;
	size -= 4;
	if (crc32_be(0, pfw, size) != crc)
		goto err_fw_corrupted;

	/*
	 * Start to upload formware : send reset
	 */
	value = 1;
	ret = uea_send_modem_cmd(usb, F8051_USBCS, sizeof(value), &value);

	if (ret < 0) {
		uea_err(usb, "modem reset failed with error %d\n", ret);
		goto err;
	}

	while (size > 3) {
		u8 len = FW_GET_BYTE(pfw);
		u16 add = FW_GET_WORD(pfw + 1);

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

	uea_leaves(usb);
	return;

err_fw_corrupted:
	uea_err(usb, "firmware is corrupted\n");
err:
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
	}

	ret = request_firmware_nowait(THIS_MODULE, 1, fw_name, &usb->dev, usb, uea_upload_pre_firmware);
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
static int check_dsp(u8 *dsp, unsigned int len)
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

		pageoffset = FW_GET_LONG(dsp + p);
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
			blocksize = FW_GET_WORD(dsp + pp);
			pp += 2;

			/* enough space for block data? */
			if (pp + blocksize > len)
				return 1;

			pp += blocksize;
		}
	}

	return 0;
}

/*
 * send data to the idma pipe
 * */
static int uea_idma_write(struct uea_softc *sc, void *data, u32 size)
{
	int ret = -ENOMEM;
	u8 *xfer_buff;
	int bytes_read;

	xfer_buff = kmalloc(size, GFP_KERNEL);
	if (!xfer_buff) {
		uea_err(INS_TO_USBDEV(sc), "can't allocate xfer_buff\n");
		return ret;
	}

	memcpy(xfer_buff, data, size);

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

	if (UEA_CHIP_VERSION(sc) == ADI930) {
		if (IS_ISDN(sc->usb_dev))
			dsp_name = FW_DIR "DSP9i.bin";
		else
			dsp_name = FW_DIR "DSP9p.bin";
	} else {
		if (IS_ISDN(sc->usb_dev))
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

	if (check_dsp(sc->dsp_firm->data, sc->dsp_firm->size)) {
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
static void uea_load_page(void *xsc)
{
	struct uea_softc *sc = xsc;
	u16 pageno = sc->pageno;
	u16 ovl = sc->ovl;
	struct block_info bi;

	u8 *p;
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
	pageoffset = FW_GET_LONG(p);

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
		blockaddr = FW_GET_WORD(p);
		p += 2;

		blocksize = FW_GET_WORD(p);
		p += 2;

		bi.wSize = cpu_to_le16(blocksize);
		bi.wAddress = cpu_to_le16(blockaddr);
		bi.wLast = cpu_to_le16((i == blockcount - 1) ? 1 : 0);

		/* send block info through the IDMA pipe */
		if (uea_idma_write(sc, &bi, BLOCK_INFO_SIZE))
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

static inline void wake_up_cmv_ack(struct uea_softc *sc)
{
	BUG_ON(sc->cmv_ack);
	sc->cmv_ack = 1;
	wake_up(&sc->cmv_ack_wait);
}

static inline int wait_cmv_ack(struct uea_softc *sc)
{
	int ret = wait_event_interruptible_timeout(sc->cmv_ack_wait,
						   sc->cmv_ack, ACK_TIMEOUT);
	sc->cmv_ack = 0;

	uea_dbg(INS_TO_USBDEV(sc), "wait_event_timeout : %d ms\n",
			jiffies_to_msecs(ret));

	if (ret < 0)
		return ret;

	return (ret == 0) ? -ETIMEDOUT : 0;
}

#define UCDC_SEND_ENCAPSULATED_COMMAND 0x00

static int uea_request(struct uea_softc *sc,
		u16 value, u16 index, u16 size, void *data)
{
	u8 *xfer_buff;
	int ret = -ENOMEM;

	xfer_buff = kmalloc(size, GFP_KERNEL);
	if (!xfer_buff) {
		uea_err(INS_TO_USBDEV(sc), "can't allocate xfer_buff\n");
		return ret;
	}
	memcpy(xfer_buff, data, size);

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

static int uea_cmv(struct uea_softc *sc,
		u8 function, u32 address, u16 offset, u32 data)
{
	struct cmv cmv;
	int ret;

	uea_enters(INS_TO_USBDEV(sc));
	uea_vdbg(INS_TO_USBDEV(sc), "Function : %d-%d, Address : %c%c%c%c, "
			"offset : 0x%04x, data : 0x%08x\n",
			FUNCTION_TYPE(function), FUNCTION_SUBTYPE(function),
			GETSA1(address), GETSA2(address), GETSA3(address),
			GETSA4(address), offset, data);
	/* we send a request, but we expect a reply */
	sc->cmv_function = function | 0x2;
	sc->cmv_idx++;
	sc->cmv_address = address;
	sc->cmv_offset = offset;

	cmv.wPreamble = cpu_to_le16(PREAMBLE);
	cmv.bDirection = HOSTTOMODEM;
	cmv.bFunction = function;
	cmv.wIndex = cpu_to_le16(sc->cmv_idx);
	put_unaligned(cpu_to_le32(address), &cmv.dwSymbolicAddress);
	cmv.wOffsetAddress = cpu_to_le16(offset);
	put_unaligned(cpu_to_le32(data >> 16 | data << 16), &cmv.dwData);

	ret = uea_request(sc, UEA_SET_BLOCK, UEA_MPTX_START, CMV_SIZE, &cmv);
	if (ret < 0)
		return ret;
	ret = wait_cmv_ack(sc);
	uea_leaves(INS_TO_USBDEV(sc));
	return ret;
}

static inline int uea_read_cmv(struct uea_softc *sc,
		u32 address, u16 offset, u32 *data)
{
	int ret = uea_cmv(sc, MAKEFUNCTION(MEMACCESS, REQUESTREAD),
			  address, offset, 0);
	if (ret < 0)
		uea_err(INS_TO_USBDEV(sc),
			"reading cmv failed with error %d\n", ret);
	else
	 	*data = sc->data;

	return ret;
}

static inline int uea_write_cmv(struct uea_softc *sc,
		u32 address, u16 offset, u32 data)
{
	int ret = uea_cmv(sc, MAKEFUNCTION(MEMACCESS, REQUESTWRITE),
			  address, offset, data);
	if (ret < 0)
		uea_err(INS_TO_USBDEV(sc),
			"writing cmv failed with error %d\n", ret);

	return ret;
}

/*
 * Monitor the modem and update the stat
 * return 0 if everything is ok
 * return < 0 if an error occurs (-EAGAIN reboot needed)
 */
static int uea_stat(struct uea_softc *sc)
{
	u32 data;
	int ret;

	uea_enters(INS_TO_USBDEV(sc));
	data = sc->stats.phy.state;

	ret = uea_read_cmv(sc, SA_STAT, 0, &sc->stats.phy.state);
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

	ret = uea_read_cmv(sc, SA_DIAG, 2, &sc->stats.phy.flags);
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

	ret = uea_read_cmv(sc, SA_RATE, 0, &data);
	if (ret < 0)
		return ret;

	/* in bulk mode the modem have problem with high rate
	 * changing internal timing could improve things, but the
	 * value is misterious.
	 * ADI930 don't support it (-EPIPE error).
	 */
	if (UEA_CHIP_VERSION(sc) != ADI930
		    && !use_iso[sc->modem_index]
		    && sc->stats.phy.dsrate != (data >> 16) * 32) {
		/* Original timming from ADI(used in windows driver)
		 * 0x20ffff>>16 * 32 = 32 * 32 = 1Mbits
		 */
		u16 timeout = (data <= 0x20ffff) ? 0 : 1;
		ret = uea_request(sc, UEA_SET_TIMEOUT, timeout, 0, NULL);
		uea_info(INS_TO_USBDEV(sc),
				"setting new timeout %d%s\n", timeout,
				ret < 0?" failed":"");
	}
	sc->stats.phy.dsrate = (data >> 16) * 32;
	sc->stats.phy.usrate = (data & 0xffff) * 32;
	UPDATE_ATM_STAT(link_rate, sc->stats.phy.dsrate * 1000 / 424);

	ret = uea_read_cmv(sc, SA_DIAG, 23, &data);
	if (ret < 0)
		return ret;
	sc->stats.phy.dsattenuation = (data & 0xff) / 2;

	ret = uea_read_cmv(sc, SA_DIAG, 47, &data);
	if (ret < 0)
		return ret;
	sc->stats.phy.usattenuation = (data & 0xff) / 2;

	ret = uea_read_cmv(sc, SA_DIAG, 25, &sc->stats.phy.dsmargin);
	if (ret < 0)
		return ret;

	ret = uea_read_cmv(sc, SA_DIAG, 49, &sc->stats.phy.usmargin);
	if (ret < 0)
		return ret;

	ret = uea_read_cmv(sc, SA_DIAG, 51, &sc->stats.phy.rxflow);
	if (ret < 0)
		return ret;

	ret = uea_read_cmv(sc, SA_DIAG, 52, &sc->stats.phy.txflow);
	if (ret < 0)
		return ret;

	ret = uea_read_cmv(sc, SA_DIAG, 54, &sc->stats.phy.dsunc);
	if (ret < 0)
		return ret;

	/* only for atu-c */
	ret = uea_read_cmv(sc, SA_DIAG, 58, &sc->stats.phy.usunc);
	if (ret < 0)
		return ret;

	ret = uea_read_cmv(sc, SA_DIAG, 53, &sc->stats.phy.dscorr);
	if (ret < 0)
		return ret;

	/* only for atu-c */
	ret = uea_read_cmv(sc, SA_DIAG, 57, &sc->stats.phy.uscorr);
	if (ret < 0)
		return ret;

	ret = uea_read_cmv(sc, SA_INFO, 8, &sc->stats.phy.vidco);
	if (ret < 0)
		return ret;

	ret = uea_read_cmv(sc, SA_INFO, 13, &sc->stats.phy.vidcpe);
	if (ret < 0)
		return ret;

	return 0;
}

static int request_cmvs(struct uea_softc *sc,
		 struct uea_cmvs **cmvs, const struct firmware **fw)
{
	int ret, size;
	u8 *data;
	char *file;
	char cmv_name[FIRMWARE_NAME_MAX]; /* 30 bytes stack variable */

	if (cmv_file[sc->modem_index] == NULL) {
		if (UEA_CHIP_VERSION(sc) == ADI930)
			file = (IS_ISDN(sc->usb_dev)) ? "CMV9i.bin" : "CMV9p.bin";
		else
			file = (IS_ISDN(sc->usb_dev)) ? "CMVei.bin" : "CMVep.bin";
	} else
		file = cmv_file[sc->modem_index];

	strcpy(cmv_name, FW_DIR);
	strlcat(cmv_name, file, sizeof(cmv_name));

	ret = request_firmware(fw, cmv_name, &sc->usb_dev->dev);
	if (ret < 0) {
		uea_err(INS_TO_USBDEV(sc),
		       "requesting firmware %s failed with error %d\n",
		       cmv_name, ret);
		return ret;
	}

	data = (u8 *) (*fw)->data;
	size = *data * sizeof(struct uea_cmvs) + 1;
	if (size != (*fw)->size) {
		uea_err(INS_TO_USBDEV(sc), "firmware %s is corrupted\n",
		       cmv_name);
		release_firmware(*fw);
		return -EILSEQ;
	}

	*cmvs = (struct uea_cmvs *)(data + 1);
	return *data;
}

/* Start boot post firmware modem:
 * - send reset commands through usb control pipe
 * - start workqueue for DSP loading
 * - send CMV options to modem
 */

static int uea_start_reset(struct uea_softc *sc)
{
	u16 zero = 0;	/* ;-) */
	int i, len, ret;
	struct uea_cmvs *cmvs;
	const struct firmware *cmvs_fw;

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
	msleep(100);

	/* leave reset mode */
	uea_request(sc, UEA_SET_MODE, UEA_END_RESET, 0, NULL);

 	/* clear tx and rx mailboxes */
	uea_request(sc, UEA_SET_2183_DATA, UEA_MPTX_MAILBOX, 2, &zero);
	uea_request(sc, UEA_SET_2183_DATA, UEA_MPRX_MAILBOX, 2, &zero);
	uea_request(sc, UEA_SET_2183_DATA, UEA_SWAP_MAILBOX, 2, &zero);

	msleep(1000);
	sc->cmv_function = MAKEFUNCTION(ADSLDIRECTIVE, MODEMREADY);
	/* demask interrupt */
	sc->booting = 0;

	/* start loading DSP */
	sc->pageno = 0;
	sc->ovl = 0;
	schedule_work(&sc->task);

	/* wait for modem ready CMV */
	ret = wait_cmv_ack(sc);
	if (ret < 0)
		return ret;

	uea_vdbg(INS_TO_USBDEV(sc), "Ready CMV received\n");

	/* Enter in R-IDLE (cmv) until instructed otherwise */
	ret = uea_write_cmv(sc, SA_CNTL, 0, 1);
	if (ret < 0)
		return ret;

	/* Dump firmware version */
	ret = uea_read_cmv(sc, SA_INFO, 10, &sc->stats.phy.firmid);
	if (ret < 0)
		return ret;
	uea_info(INS_TO_USBDEV(sc), "ATU-R firmware version : %x\n",
			sc->stats.phy.firmid);

	/* get options */
 	ret = len = request_cmvs(sc, &cmvs, &cmvs_fw);
	if (ret < 0)
		return ret;

	/* send options */
	for (i = 0; i < len; i++) {
		ret = uea_write_cmv(sc, FW_GET_LONG(&cmvs[i].address),
					FW_GET_WORD(&cmvs[i].offset),
					FW_GET_LONG(&cmvs[i].data));
		if (ret < 0)
			goto out;
	}
	/* Enter in R-ACT-REQ */
	ret = uea_write_cmv(sc, SA_CNTL, 0, 2);
	uea_vdbg(INS_TO_USBDEV(sc), "Entering in R-ACT-REQ state\n");
	uea_info(INS_TO_USBDEV(sc), "Modem started, "
		"waiting synchronization\n");
out:
	release_firmware(cmvs_fw);
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

	uea_enters(INS_TO_USBDEV(sc));
	while (!kthread_should_stop()) {
		if (ret < 0 || sc->reset)
			ret = uea_start_reset(sc);
		if (!ret)
			ret = uea_stat(sc);
		if (ret != -EAGAIN)
			msleep_interruptible(1000);
 		if (try_to_freeze())
			uea_err(INS_TO_USBDEV(sc), "suspend/resume not supported, "
				"please unplug/replug your modem\n");
	}
	uea_leaves(INS_TO_USBDEV(sc));
	return ret;
}

/* Load second usb firmware for ADI930 chip */
static int load_XILINX_firmware(struct uea_softc *sc)
{
	const struct firmware *fw_entry;
	int ret, size, u, ln;
	u8 *pfw, value;
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
		uea_err(sc->usb_dev, "elsa de-assert failed with error %d\n", ret);


err1:
	release_firmware(fw_entry);
err0:
	uea_leaves(INS_TO_USBDEV(sc));
	return ret;
}

/* The modem send us an ack. First with check if it right */
static void uea_dispatch_cmv(struct uea_softc *sc, struct cmv* cmv)
{
	uea_enters(INS_TO_USBDEV(sc));
	if (le16_to_cpu(cmv->wPreamble) != PREAMBLE)
		goto bad1;

	if (cmv->bDirection != MODEMTOHOST)
		goto bad1;

	/* FIXME : ADI930 reply wrong preambule (func = 2, sub = 2) to
	 * the first MEMACESS cmv. Ignore it...
	 */
	if (cmv->bFunction != sc->cmv_function) {
		if (UEA_CHIP_VERSION(sc) == ADI930
				&& cmv->bFunction ==  MAKEFUNCTION(2, 2)) {
			cmv->wIndex = cpu_to_le16(sc->cmv_idx);
			put_unaligned(cpu_to_le32(sc->cmv_address), &cmv->dwSymbolicAddress);
			cmv->wOffsetAddress = cpu_to_le16(sc->cmv_offset);
		}
		else
			goto bad2;
	}

	if (cmv->bFunction == MAKEFUNCTION(ADSLDIRECTIVE, MODEMREADY)) {
		wake_up_cmv_ack(sc);
		uea_leaves(INS_TO_USBDEV(sc));
		return;
	}

	/* in case of MEMACCESS */
	if (le16_to_cpu(cmv->wIndex) != sc->cmv_idx ||
	    le32_to_cpu(get_unaligned(&cmv->dwSymbolicAddress)) !=
	    sc->cmv_address
	    || le16_to_cpu(cmv->wOffsetAddress) != sc->cmv_offset)
		goto bad2;

	sc->data = le32_to_cpu(get_unaligned(&cmv->dwData));
	sc->data = sc->data << 16 | sc->data >> 16;

	wake_up_cmv_ack(sc);
	uea_leaves(INS_TO_USBDEV(sc));
	return;

bad2:
	uea_err(INS_TO_USBDEV(sc), "unexpected cmv received,"
			"Function : %d, Subfunction : %d\n",
			FUNCTION_TYPE(cmv->bFunction),
			FUNCTION_SUBTYPE(cmv->bFunction));
	uea_leaves(INS_TO_USBDEV(sc));
	return;

bad1:
	uea_err(INS_TO_USBDEV(sc), "invalid cmv received, "
			"wPreamble %d, bDirection %d\n",
			le16_to_cpu(cmv->wPreamble), cmv->bDirection);
	uea_leaves(INS_TO_USBDEV(sc));
}

/*
 * interrupt handler
 */
static void uea_intr(struct urb *urb)
{
	struct uea_softc *sc = urb->context;
	struct intr_pkt *intr = urb->transfer_buffer;
	uea_enters(INS_TO_USBDEV(sc));

	if (unlikely(urb->status < 0)) {
		uea_err(INS_TO_USBDEV(sc), "uea_intr() failed with %d\n",
		       urb->status);
		return;
	}

	/* device-to-host interrupt */
	if (intr->bType != 0x08 || sc->booting) {
		uea_err(INS_TO_USBDEV(sc), "wrong interrupt\n");
		goto resubmit;
	}

	switch (le16_to_cpu(intr->wInterrupt)) {
	case INT_LOADSWAPPAGE:
		sc->pageno = intr->bSwapPageNo;
		sc->ovl = intr->bOvl >> 4 | intr->bOvl << 4;
		schedule_work(&sc->task);
		break;

	case INT_INCOMINGCMV:
		uea_dispatch_cmv(sc, &intr->u.s2.cmv);
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
	int ret;
	struct intr_pkt *intr;

	uea_enters(INS_TO_USBDEV(sc));

	INIT_WORK(&sc->task, uea_load_page, sc);
	init_waitqueue_head(&sc->sync_q);
	init_waitqueue_head(&sc->cmv_ack_wait);

	if (UEA_CHIP_VERSION(sc) == ADI930)
		load_XILINX_firmware(sc);

	intr = kmalloc(INTR_PKT_SIZE, GFP_KERNEL);
	if (!intr) {
		uea_err(INS_TO_USBDEV(sc),
		       "cannot allocate interrupt package\n");
		uea_leaves(INS_TO_USBDEV(sc));
		return -ENOMEM;
	}

	sc->urb_int = usb_alloc_urb(0, GFP_KERNEL);
	if (!sc->urb_int) {
		uea_err(INS_TO_USBDEV(sc), "cannot allocate interrupt URB\n");
		goto err;
	}

	usb_fill_int_urb(sc->urb_int, sc->usb_dev,
			 usb_rcvintpipe(sc->usb_dev, UEA_INTR_PIPE),
			 intr, INTR_PKT_SIZE, uea_intr, sc,
			 sc->usb_dev->actconfig->interface[0]->altsetting[0].
			 endpoint[0].desc.bInterval);

	ret = usb_submit_urb(sc->urb_int, GFP_KERNEL);
	if (ret < 0) {
		uea_err(INS_TO_USBDEV(sc),
		       "urb submition failed with error %d\n", ret);
		goto err;
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
err:
	usb_free_urb(sc->urb_int);
	sc->urb_int = NULL;
	kfree(intr);
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

	/* stop any pending boot process */
	flush_scheduled_work();

	uea_request(sc, UEA_SET_MODE, UEA_LOOPBACK_ON, 0, NULL);

	usb_kill_urb(sc->urb_int);
	kfree(sc->urb_int->transfer_buffer);
	usb_free_urb(sc->urb_int);

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

static ssize_t read_human_status(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int ret = -ENODEV;
	struct uea_softc *sc;

	mutex_lock(&uea_mutex);
	sc = dev_to_uea(dev);
	if (!sc)
		goto out;

	switch (GET_STATUS(sc->stats.phy.state)) {
	case 0:
		ret = sprintf(buf, "Modem is booting\n");
		break;
	case 1:
		ret = sprintf(buf, "Modem is initializing\n");
		break;
	case 2:
		ret = sprintf(buf, "Modem is operational\n");
		break;
	default:
		ret = sprintf(buf, "Modem synchronization failed\n");
		break;
	}
out:
	mutex_unlock(&uea_mutex);
	return ret;
}

static DEVICE_ATTR(stat_human_status, S_IWUGO | S_IRUGO, read_human_status, NULL);

static ssize_t read_delin(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int ret = -ENODEV;
	struct uea_softc *sc;

	mutex_lock(&uea_mutex);
	sc = dev_to_uea(dev);
	if (!sc)
		goto out;

	if (sc->stats.phy.flags & 0x0C00)
		ret = sprintf(buf, "ERROR\n");
	else if (sc->stats.phy.flags & 0x0030)
		ret = sprintf(buf, "LOSS\n");
	else
		ret = sprintf(buf, "GOOD\n");
out:
	mutex_unlock(&uea_mutex);
	return ret;
}

static DEVICE_ATTR(stat_delin, S_IWUGO | S_IRUGO, read_delin, NULL);

#define UEA_ATTR(name, reset) 					\
								\
static ssize_t read_##name(struct device *dev, 			\
		struct device_attribute *attr, char *buf)	\
{ 								\
	int ret = -ENODEV; 					\
	struct uea_softc *sc; 					\
 								\
	mutex_lock(&uea_mutex); 				\
	sc = dev_to_uea(dev);					\
	if (!sc) 						\
		goto out; 					\
	ret = snprintf(buf, 10, "%08x\n", sc->stats.phy.name);	\
	if (reset)						\
		sc->stats.phy.name = 0;				\
out: 								\
	mutex_unlock(&uea_mutex); 				\
	return ret; 						\
} 								\
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

	/* ADI930 don't support iso */
	if (UEA_CHIP_VERSION(id) != ADI930 && use_iso[sc->modem_index]) {
		int i;

		/* try set fastest alternate for inbound traffic interface */
		for (i = FASTEST_ISO_INTF; i > 0; i--)
			if (usb_set_interface(usb, UEA_DS_IFACE_NO, i) == 0)
				break;

		if (i > 0) {
			uea_dbg(usb, "set alternate %d for 2 interface\n", i);
			uea_info(usb, "using iso mode\n");
			usbatm->flags |= UDSL_USE_ISOC | UDSL_IGNORE_EILSEQ;
		} else {
			uea_err(usb, "setting any alternate failed for "
					"2 interface, using bulk mode\n");
		}
	}

	ret = sysfs_create_group(&intf->dev.kobj, &attr_grp);
	if (ret < 0)
		goto error;

	ret = uea_boot(sc);
	if (ret < 0)
		goto error;

	return 0;
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
	uea_info(usb, "ADSL device founded vid (%#X) pid (%#X) : %s %s\n",
	       le16_to_cpu(usb->descriptor.idVendor),
	       le16_to_cpu(usb->descriptor.idProduct),
	       chip_name[UEA_CHIP_VERSION(id)], IS_ISDN(usb)?"isdn":"pots");

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
	{USB_DEVICE(ELSA_VID,	ELSA_PID_PREFIRM),	.driver_info = ADI930 | PREFIRM},
	{USB_DEVICE(ELSA_VID,	ELSA_PID_PSTFIRM),	.driver_info = ADI930 | PSTFIRM},
	{USB_DEVICE(EAGLE_VID,	EAGLE_I_PID_PREFIRM),	.driver_info = EAGLE_I | PREFIRM},
	{USB_DEVICE(EAGLE_VID,	EAGLE_I_PID_PSTFIRM),	.driver_info = EAGLE_I | PSTFIRM},
	{USB_DEVICE(EAGLE_VID,	EAGLE_II_PID_PREFIRM),	.driver_info = EAGLE_II | PREFIRM},
	{USB_DEVICE(EAGLE_VID,	EAGLE_II_PID_PSTFIRM),	.driver_info = EAGLE_II | PSTFIRM},
	{USB_DEVICE(EAGLE_VID,	EAGLE_IIC_PID_PREFIRM),	.driver_info = EAGLE_II | PREFIRM},
	{USB_DEVICE(EAGLE_VID,	EAGLE_IIC_PID_PSTFIRM),	.driver_info = EAGLE_II | PSTFIRM},
	{USB_DEVICE(EAGLE_VID,	EAGLE_III_PID_PREFIRM),	.driver_info = EAGLE_III | PREFIRM},
	{USB_DEVICE(EAGLE_VID,	EAGLE_III_PID_PSTFIRM),	.driver_info = EAGLE_III | PSTFIRM},
	{USB_DEVICE(USR_VID,	MILLER_A_PID_PREFIRM),	.driver_info = EAGLE_I | PREFIRM},
	{USB_DEVICE(USR_VID,	MILLER_A_PID_PSTFIRM),	.driver_info = EAGLE_I | PSTFIRM},
	{USB_DEVICE(USR_VID,	MILLER_B_PID_PREFIRM),	.driver_info = EAGLE_I | PREFIRM},
	{USB_DEVICE(USR_VID,	MILLER_B_PID_PSTFIRM),	.driver_info = EAGLE_I | PSTFIRM},
	{USB_DEVICE(USR_VID,	HEINEKEN_A_PID_PREFIRM),.driver_info = EAGLE_I | PREFIRM},
	{USB_DEVICE(USR_VID,	HEINEKEN_A_PID_PSTFIRM),.driver_info = EAGLE_I | PSTFIRM},
	{USB_DEVICE(USR_VID,	HEINEKEN_B_PID_PREFIRM),.driver_info = EAGLE_I | PREFIRM},
	{USB_DEVICE(USR_VID,	HEINEKEN_B_PID_PSTFIRM),.driver_info = EAGLE_I | PSTFIRM},
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
