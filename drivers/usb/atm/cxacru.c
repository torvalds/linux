// SPDX-License-Identifier: GPL-2.0+
/******************************************************************************
 *  cxacru.c  -  driver for USB ADSL modems based on
 *               Conexant AccessRunner chipset
 *
 *  Copyright (C) 2004 David Woodhouse, Duncan Sands, Roman Kagan
 *  Copyright (C) 2005 Duncan Sands, Roman Kagan (rkagan % mail ! ru)
 *  Copyright (C) 2007 Simon Arlott
 *  Copyright (C) 2009 Simon Arlott
 ******************************************************************************/

/*
 *  Credit is due for Josep Comas, who created the original patch to speedtch.c
 *  to support the different padding used by the AccessRunner (now generalized
 *  into usbatm), and the userspace firmware loading utility.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/mutex.h>
#include <asm/unaligned.h>

#include "usbatm.h"

#define DRIVER_AUTHOR	"Roman Kagan, David Woodhouse, Duncan Sands, Simon Arlott"
#define DRIVER_DESC	"Conexant AccessRunner ADSL USB modem driver"

static const char cxacru_driver_name[] = "cxacru";

#define CXACRU_EP_CMD		0x01	/* Bulk/interrupt in/out */
#define CXACRU_EP_DATA		0x02	/* Bulk in/out */

#define CMD_PACKET_SIZE		64	/* Should be maxpacket(ep)? */
#define CMD_MAX_CONFIG		((CMD_PACKET_SIZE / 4 - 1) / 2)

/* Addresses */
#define PLLFCLK_ADDR	0x00350068
#define PLLBCLK_ADDR	0x0035006c
#define SDRAMEN_ADDR	0x00350010
#define FW_ADDR		0x00801000
#define BR_ADDR		0x00180600
#define SIG_ADDR	0x00180500
#define BR_STACK_ADDR	0x00187f10

/* Values */
#define SDRAM_ENA	0x1

#define CMD_TIMEOUT	2000	/* msecs */
#define POLL_INTERVAL	1	/* secs */

/* commands for interaction with the modem through the control channel before
 * firmware is loaded  */
enum cxacru_fw_request {
	FW_CMD_ERR,
	FW_GET_VER,
	FW_READ_MEM,
	FW_WRITE_MEM,
	FW_RMW_MEM,
	FW_CHECKSUM_MEM,
	FW_GOTO_MEM,
};

/* commands for interaction with the modem through the control channel once
 * firmware is loaded  */
enum cxacru_cm_request {
	CM_REQUEST_UNDEFINED = 0x80,
	CM_REQUEST_TEST,
	CM_REQUEST_CHIP_GET_MAC_ADDRESS,
	CM_REQUEST_CHIP_GET_DP_VERSIONS,
	CM_REQUEST_CHIP_ADSL_LINE_START,
	CM_REQUEST_CHIP_ADSL_LINE_STOP,
	CM_REQUEST_CHIP_ADSL_LINE_GET_STATUS,
	CM_REQUEST_CHIP_ADSL_LINE_GET_SPEED,
	CM_REQUEST_CARD_INFO_GET,
	CM_REQUEST_CARD_DATA_GET,
	CM_REQUEST_CARD_DATA_SET,
	CM_REQUEST_COMMAND_HW_IO,
	CM_REQUEST_INTERFACE_HW_IO,
	CM_REQUEST_CARD_SERIAL_DATA_PATH_GET,
	CM_REQUEST_CARD_SERIAL_DATA_PATH_SET,
	CM_REQUEST_CARD_CONTROLLER_VERSION_GET,
	CM_REQUEST_CARD_GET_STATUS,
	CM_REQUEST_CARD_GET_MAC_ADDRESS,
	CM_REQUEST_CARD_GET_DATA_LINK_STATUS,
	CM_REQUEST_MAX,
};

/* commands for interaction with the flash memory
 *
 * read:  response is the contents of the first 60 bytes of flash memory
 * write: request contains the 60 bytes of data to write to flash memory
 *        response is the contents of the first 60 bytes of flash memory
 *
 * layout: PP PP VV VV  MM MM MM MM  MM MM ?? ??  SS SS SS SS  SS SS SS SS
 *         SS SS SS SS  SS SS SS SS  00 00 00 00  00 00 00 00  00 00 00 00
 *         00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00
 *
 *   P: le16  USB Product ID
 *   V: le16  USB Vendor ID
 *   M: be48  MAC Address
 *   S: le16  ASCII Serial Number
 */
enum cxacru_cm_flash {
	CM_FLASH_READ = 0xa1,
	CM_FLASH_WRITE = 0xa2
};

/* reply codes to the commands above */
enum cxacru_cm_status {
	CM_STATUS_UNDEFINED,
	CM_STATUS_SUCCESS,
	CM_STATUS_ERROR,
	CM_STATUS_UNSUPPORTED,
	CM_STATUS_UNIMPLEMENTED,
	CM_STATUS_PARAMETER_ERROR,
	CM_STATUS_DBG_LOOPBACK,
	CM_STATUS_MAX,
};

/* indices into CARD_INFO_GET return array */
enum cxacru_info_idx {
	CXINF_DOWNSTREAM_RATE,
	CXINF_UPSTREAM_RATE,
	CXINF_LINK_STATUS,
	CXINF_LINE_STATUS,
	CXINF_MAC_ADDRESS_HIGH,
	CXINF_MAC_ADDRESS_LOW,
	CXINF_UPSTREAM_SNR_MARGIN,
	CXINF_DOWNSTREAM_SNR_MARGIN,
	CXINF_UPSTREAM_ATTENUATION,
	CXINF_DOWNSTREAM_ATTENUATION,
	CXINF_TRANSMITTER_POWER,
	CXINF_UPSTREAM_BITS_PER_FRAME,
	CXINF_DOWNSTREAM_BITS_PER_FRAME,
	CXINF_STARTUP_ATTEMPTS,
	CXINF_UPSTREAM_CRC_ERRORS,
	CXINF_DOWNSTREAM_CRC_ERRORS,
	CXINF_UPSTREAM_FEC_ERRORS,
	CXINF_DOWNSTREAM_FEC_ERRORS,
	CXINF_UPSTREAM_HEC_ERRORS,
	CXINF_DOWNSTREAM_HEC_ERRORS,
	CXINF_LINE_STARTABLE,
	CXINF_MODULATION,
	CXINF_ADSL_HEADEND,
	CXINF_ADSL_HEADEND_ENVIRONMENT,
	CXINF_CONTROLLER_VERSION,
	/* dunno what the missing two mean */
	CXINF_MAX = 0x1c,
};

enum cxacru_poll_state {
	CXPOLL_STOPPING,
	CXPOLL_STOPPED,
	CXPOLL_POLLING,
	CXPOLL_SHUTDOWN
};

struct cxacru_modem_type {
	u32 pll_f_clk;
	u32 pll_b_clk;
	int boot_rom_patch;
};

struct cxacru_data {
	struct usbatm_data *usbatm;

	const struct cxacru_modem_type *modem_type;

	int line_status;
	struct mutex adsl_state_serialize;
	int adsl_status;
	struct delayed_work poll_work;
	u32 card_info[CXINF_MAX];
	struct mutex poll_state_serialize;
	enum cxacru_poll_state poll_state;

	/* contol handles */
	struct mutex cm_serialize;
	u8 *rcv_buf;
	u8 *snd_buf;
	struct urb *rcv_urb;
	struct urb *snd_urb;
	struct completion rcv_done;
	struct completion snd_done;
};

static int cxacru_cm(struct cxacru_data *instance, enum cxacru_cm_request cm,
	u8 *wdata, int wsize, u8 *rdata, int rsize);
static void cxacru_poll_status(struct work_struct *work);

/* Card info exported through sysfs */
#define CXACRU__ATTR_INIT(_name) \
static DEVICE_ATTR_RO(_name)

#define CXACRU_CMD_INIT(_name) \
static DEVICE_ATTR_RW(_name)

#define CXACRU_SET_INIT(_name) \
static DEVICE_ATTR_WO(_name)

#define CXACRU_ATTR_INIT(_value, _type, _name) \
static ssize_t _name##_show(struct device *dev, \
	struct device_attribute *attr, char *buf) \
{ \
	struct cxacru_data *instance = to_usbatm_driver_data(\
		to_usb_interface(dev)); \
\
	if (instance == NULL) \
		return -ENODEV; \
\
	return cxacru_sysfs_showattr_##_type(instance->card_info[_value], buf); \
} \
CXACRU__ATTR_INIT(_name)

#define CXACRU_ATTR_CREATE(_v, _t, _name) CXACRU_DEVICE_CREATE_FILE(_name)
#define CXACRU_CMD_CREATE(_name)          CXACRU_DEVICE_CREATE_FILE(_name)
#define CXACRU_SET_CREATE(_name)          CXACRU_DEVICE_CREATE_FILE(_name)
#define CXACRU__ATTR_CREATE(_name)        CXACRU_DEVICE_CREATE_FILE(_name)

#define CXACRU_ATTR_REMOVE(_v, _t, _name) CXACRU_DEVICE_REMOVE_FILE(_name)
#define CXACRU_CMD_REMOVE(_name)          CXACRU_DEVICE_REMOVE_FILE(_name)
#define CXACRU_SET_REMOVE(_name)          CXACRU_DEVICE_REMOVE_FILE(_name)
#define CXACRU__ATTR_REMOVE(_name)        CXACRU_DEVICE_REMOVE_FILE(_name)

static ssize_t cxacru_sysfs_showattr_u32(u32 value, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", value);
}

static ssize_t cxacru_sysfs_showattr_s8(s8 value, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", value);
}

static ssize_t cxacru_sysfs_showattr_dB(s16 value, char *buf)
{
	if (likely(value >= 0)) {
		return snprintf(buf, PAGE_SIZE, "%u.%02u\n",
					value / 100, value % 100);
	} else {
		value = -value;
		return snprintf(buf, PAGE_SIZE, "-%u.%02u\n",
					value / 100, value % 100);
	}
}

static ssize_t cxacru_sysfs_showattr_bool(u32 value, char *buf)
{
	static char *str[] = { "no", "yes" };

	if (unlikely(value >= ARRAY_SIZE(str)))
		return snprintf(buf, PAGE_SIZE, "%u\n", value);
	return snprintf(buf, PAGE_SIZE, "%s\n", str[value]);
}

static ssize_t cxacru_sysfs_showattr_LINK(u32 value, char *buf)
{
	static char *str[] = { NULL, "not connected", "connected", "lost" };

	if (unlikely(value >= ARRAY_SIZE(str) || str[value] == NULL))
		return snprintf(buf, PAGE_SIZE, "%u\n", value);
	return snprintf(buf, PAGE_SIZE, "%s\n", str[value]);
}

static ssize_t cxacru_sysfs_showattr_LINE(u32 value, char *buf)
{
	static char *str[] = { "down", "attempting to activate",
		"training", "channel analysis", "exchange", "up",
		"waiting", "initialising"
	};
	if (unlikely(value >= ARRAY_SIZE(str)))
		return snprintf(buf, PAGE_SIZE, "%u\n", value);
	return snprintf(buf, PAGE_SIZE, "%s\n", str[value]);
}

static ssize_t cxacru_sysfs_showattr_MODU(u32 value, char *buf)
{
	static char *str[] = {
			"",
			"ANSI T1.413",
			"ITU-T G.992.1 (G.DMT)",
			"ITU-T G.992.2 (G.LITE)"
	};
	if (unlikely(value >= ARRAY_SIZE(str)))
		return snprintf(buf, PAGE_SIZE, "%u\n", value);
	return snprintf(buf, PAGE_SIZE, "%s\n", str[value]);
}

/*
 * This could use MAC_ADDRESS_HIGH and MAC_ADDRESS_LOW, but since
 * this data is already in atm_dev there's no point.
 *
 * MAC_ADDRESS_HIGH = 0x????5544
 * MAC_ADDRESS_LOW  = 0x33221100
 * Where 00-55 are bytes 0-5 of the MAC.
 */
static ssize_t mac_address_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct cxacru_data *instance = to_usbatm_driver_data(
			to_usb_interface(dev));

	if (instance == NULL || instance->usbatm->atm_dev == NULL)
		return -ENODEV;

	return snprintf(buf, PAGE_SIZE, "%pM\n",
		instance->usbatm->atm_dev->esi);
}

static ssize_t adsl_state_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	static char *str[] = { "running", "stopped" };
	struct cxacru_data *instance = to_usbatm_driver_data(
			to_usb_interface(dev));
	u32 value;

	if (instance == NULL)
		return -ENODEV;

	value = instance->card_info[CXINF_LINE_STARTABLE];
	if (unlikely(value >= ARRAY_SIZE(str)))
		return snprintf(buf, PAGE_SIZE, "%u\n", value);
	return snprintf(buf, PAGE_SIZE, "%s\n", str[value]);
}

static ssize_t adsl_state_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct cxacru_data *instance = to_usbatm_driver_data(
			to_usb_interface(dev));
	int ret;
	int poll = -1;
	char str_cmd[8];
	int len = strlen(buf);

	if (!capable(CAP_NET_ADMIN))
		return -EACCES;

	ret = sscanf(buf, "%7s", str_cmd);
	if (ret != 1)
		return -EINVAL;
	ret = 0;

	if (instance == NULL)
		return -ENODEV;

	if (mutex_lock_interruptible(&instance->adsl_state_serialize))
		return -ERESTARTSYS;

	if (!strcmp(str_cmd, "stop") || !strcmp(str_cmd, "restart")) {
		ret = cxacru_cm(instance, CM_REQUEST_CHIP_ADSL_LINE_STOP, NULL, 0, NULL, 0);
		if (ret < 0) {
			atm_err(instance->usbatm, "change adsl state:"
				" CHIP_ADSL_LINE_STOP returned %d\n", ret);

			ret = -EIO;
		} else {
			ret = len;
			poll = CXPOLL_STOPPED;
		}
	}

	/* Line status is only updated every second
	 * and the device appears to only react to
	 * START/STOP every second too. Wait 1.5s to
	 * be sure that restart will have an effect. */
	if (!strcmp(str_cmd, "restart"))
		msleep(1500);

	if (!strcmp(str_cmd, "start") || !strcmp(str_cmd, "restart")) {
		ret = cxacru_cm(instance, CM_REQUEST_CHIP_ADSL_LINE_START, NULL, 0, NULL, 0);
		if (ret < 0) {
			atm_err(instance->usbatm, "change adsl state:"
				" CHIP_ADSL_LINE_START returned %d\n", ret);

			ret = -EIO;
		} else {
			ret = len;
			poll = CXPOLL_POLLING;
		}
	}

	if (!strcmp(str_cmd, "poll")) {
		ret = len;
		poll = CXPOLL_POLLING;
	}

	if (ret == 0) {
		ret = -EINVAL;
		poll = -1;
	}

	if (poll == CXPOLL_POLLING) {
		mutex_lock(&instance->poll_state_serialize);
		switch (instance->poll_state) {
		case CXPOLL_STOPPED:
			/* start polling */
			instance->poll_state = CXPOLL_POLLING;
			break;

		case CXPOLL_STOPPING:
			/* abort stop request */
			instance->poll_state = CXPOLL_POLLING;
			/* fall through */
		case CXPOLL_POLLING:
		case CXPOLL_SHUTDOWN:
			/* don't start polling */
			poll = -1;
		}
		mutex_unlock(&instance->poll_state_serialize);
	} else if (poll == CXPOLL_STOPPED) {
		mutex_lock(&instance->poll_state_serialize);
		/* request stop */
		if (instance->poll_state == CXPOLL_POLLING)
			instance->poll_state = CXPOLL_STOPPING;
		mutex_unlock(&instance->poll_state_serialize);
	}

	mutex_unlock(&instance->adsl_state_serialize);

	if (poll == CXPOLL_POLLING)
		cxacru_poll_status(&instance->poll_work.work);

	return ret;
}

/* CM_REQUEST_CARD_DATA_GET times out, so no show attribute */

static ssize_t adsl_config_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct cxacru_data *instance = to_usbatm_driver_data(
			to_usb_interface(dev));
	int len = strlen(buf);
	int ret, pos, num;
	__le32 data[CMD_PACKET_SIZE / 4];

	if (!capable(CAP_NET_ADMIN))
		return -EACCES;

	if (instance == NULL)
		return -ENODEV;

	pos = 0;
	num = 0;
	while (pos < len) {
		int tmp;
		u32 index;
		u32 value;

		ret = sscanf(buf + pos, "%x=%x%n", &index, &value, &tmp);
		if (ret < 2)
			return -EINVAL;
		if (index > 0x7f)
			return -EINVAL;
		if (tmp < 0 || tmp > len - pos)
			return -EINVAL;
		pos += tmp;

		/* skip trailing newline */
		if (buf[pos] == '\n' && pos == len-1)
			pos++;

		data[num * 2 + 1] = cpu_to_le32(index);
		data[num * 2 + 2] = cpu_to_le32(value);
		num++;

		/* send config values when data buffer is full
		 * or no more data
		 */
		if (pos >= len || num >= CMD_MAX_CONFIG) {
			char log[CMD_MAX_CONFIG * 12 + 1]; /* %02x=%08x */

			data[0] = cpu_to_le32(num);
			ret = cxacru_cm(instance, CM_REQUEST_CARD_DATA_SET,
				(u8 *) data, 4 + num * 8, NULL, 0);
			if (ret < 0) {
				atm_err(instance->usbatm,
					"set card data returned %d\n", ret);
				return -EIO;
			}

			for (tmp = 0; tmp < num; tmp++)
				snprintf(log + tmp*12, 13, " %02x=%08x",
					le32_to_cpu(data[tmp * 2 + 1]),
					le32_to_cpu(data[tmp * 2 + 2]));
			atm_info(instance->usbatm, "config%s\n", log);
			num = 0;
		}
	}

	return len;
}

/*
 * All device attributes are included in CXACRU_ALL_FILES
 * so that the same list can be used multiple times:
 *     INIT   (define the device attributes)
 *     CREATE (create all the device files)
 *     REMOVE (remove all the device files)
 *
 * With the last two being defined as needed in the functions
 * they are used in before calling CXACRU_ALL_FILES()
 */
#define CXACRU_ALL_FILES(_action) \
CXACRU_ATTR_##_action(CXINF_DOWNSTREAM_RATE,           u32,  downstream_rate); \
CXACRU_ATTR_##_action(CXINF_UPSTREAM_RATE,             u32,  upstream_rate); \
CXACRU_ATTR_##_action(CXINF_LINK_STATUS,               LINK, link_status); \
CXACRU_ATTR_##_action(CXINF_LINE_STATUS,               LINE, line_status); \
CXACRU__ATTR_##_action(                                      mac_address); \
CXACRU_ATTR_##_action(CXINF_UPSTREAM_SNR_MARGIN,       dB,   upstream_snr_margin); \
CXACRU_ATTR_##_action(CXINF_DOWNSTREAM_SNR_MARGIN,     dB,   downstream_snr_margin); \
CXACRU_ATTR_##_action(CXINF_UPSTREAM_ATTENUATION,      dB,   upstream_attenuation); \
CXACRU_ATTR_##_action(CXINF_DOWNSTREAM_ATTENUATION,    dB,   downstream_attenuation); \
CXACRU_ATTR_##_action(CXINF_TRANSMITTER_POWER,         s8,   transmitter_power); \
CXACRU_ATTR_##_action(CXINF_UPSTREAM_BITS_PER_FRAME,   u32,  upstream_bits_per_frame); \
CXACRU_ATTR_##_action(CXINF_DOWNSTREAM_BITS_PER_FRAME, u32,  downstream_bits_per_frame); \
CXACRU_ATTR_##_action(CXINF_STARTUP_ATTEMPTS,          u32,  startup_attempts); \
CXACRU_ATTR_##_action(CXINF_UPSTREAM_CRC_ERRORS,       u32,  upstream_crc_errors); \
CXACRU_ATTR_##_action(CXINF_DOWNSTREAM_CRC_ERRORS,     u32,  downstream_crc_errors); \
CXACRU_ATTR_##_action(CXINF_UPSTREAM_FEC_ERRORS,       u32,  upstream_fec_errors); \
CXACRU_ATTR_##_action(CXINF_DOWNSTREAM_FEC_ERRORS,     u32,  downstream_fec_errors); \
CXACRU_ATTR_##_action(CXINF_UPSTREAM_HEC_ERRORS,       u32,  upstream_hec_errors); \
CXACRU_ATTR_##_action(CXINF_DOWNSTREAM_HEC_ERRORS,     u32,  downstream_hec_errors); \
CXACRU_ATTR_##_action(CXINF_LINE_STARTABLE,            bool, line_startable); \
CXACRU_ATTR_##_action(CXINF_MODULATION,                MODU, modulation); \
CXACRU_ATTR_##_action(CXINF_ADSL_HEADEND,              u32,  adsl_headend); \
CXACRU_ATTR_##_action(CXINF_ADSL_HEADEND_ENVIRONMENT,  u32,  adsl_headend_environment); \
CXACRU_ATTR_##_action(CXINF_CONTROLLER_VERSION,        u32,  adsl_controller_version); \
CXACRU_CMD_##_action(                                        adsl_state); \
CXACRU_SET_##_action(                                        adsl_config);

CXACRU_ALL_FILES(INIT);

/* the following three functions are stolen from drivers/usb/core/message.c */
static void cxacru_blocking_completion(struct urb *urb)
{
	complete(urb->context);
}

struct cxacru_timer {
	struct timer_list timer;
	struct urb *urb;
};

static void cxacru_timeout_kill(struct timer_list *t)
{
	struct cxacru_timer *timer = from_timer(timer, t, timer);

	usb_unlink_urb(timer->urb);
}

static int cxacru_start_wait_urb(struct urb *urb, struct completion *done,
				 int *actual_length)
{
	struct cxacru_timer timer = {
		.urb = urb,
	};

	timer_setup_on_stack(&timer.timer, cxacru_timeout_kill, 0);
	mod_timer(&timer.timer, jiffies + msecs_to_jiffies(CMD_TIMEOUT));
	wait_for_completion(done);
	del_timer_sync(&timer.timer);
	destroy_timer_on_stack(&timer.timer);

	if (actual_length)
		*actual_length = urb->actual_length;
	return urb->status; /* must read status after completion */
}

static int cxacru_cm(struct cxacru_data *instance, enum cxacru_cm_request cm,
		     u8 *wdata, int wsize, u8 *rdata, int rsize)
{
	int ret, actlen;
	int offb, offd;
	const int stride = CMD_PACKET_SIZE - 4;
	u8 *wbuf = instance->snd_buf;
	u8 *rbuf = instance->rcv_buf;
	int wbuflen = ((wsize - 1) / stride + 1) * CMD_PACKET_SIZE;
	int rbuflen = ((rsize - 1) / stride + 1) * CMD_PACKET_SIZE;

	if (wbuflen > PAGE_SIZE || rbuflen > PAGE_SIZE) {
		if (printk_ratelimit())
			usb_err(instance->usbatm, "requested transfer size too large (%d, %d)\n",
				wbuflen, rbuflen);
		ret = -ENOMEM;
		goto err;
	}

	mutex_lock(&instance->cm_serialize);

	/* submit reading urb before the writing one */
	init_completion(&instance->rcv_done);
	ret = usb_submit_urb(instance->rcv_urb, GFP_KERNEL);
	if (ret < 0) {
		if (printk_ratelimit())
			usb_err(instance->usbatm, "submit of read urb for cm %#x failed (%d)\n",
				cm, ret);
		goto fail;
	}

	memset(wbuf, 0, wbuflen);
	/* handle wsize == 0 */
	wbuf[0] = cm;
	for (offb = offd = 0; offd < wsize; offd += stride, offb += CMD_PACKET_SIZE) {
		wbuf[offb] = cm;
		memcpy(wbuf + offb + 4, wdata + offd, min_t(int, stride, wsize - offd));
	}

	instance->snd_urb->transfer_buffer_length = wbuflen;
	init_completion(&instance->snd_done);
	ret = usb_submit_urb(instance->snd_urb, GFP_KERNEL);
	if (ret < 0) {
		if (printk_ratelimit())
			usb_err(instance->usbatm, "submit of write urb for cm %#x failed (%d)\n",
				cm, ret);
		goto fail;
	}

	ret = cxacru_start_wait_urb(instance->snd_urb, &instance->snd_done, NULL);
	if (ret < 0) {
		if (printk_ratelimit())
			usb_err(instance->usbatm, "send of cm %#x failed (%d)\n", cm, ret);
		goto fail;
	}

	ret = cxacru_start_wait_urb(instance->rcv_urb, &instance->rcv_done, &actlen);
	if (ret < 0) {
		if (printk_ratelimit())
			usb_err(instance->usbatm, "receive of cm %#x failed (%d)\n", cm, ret);
		goto fail;
	}
	if (actlen % CMD_PACKET_SIZE || !actlen) {
		if (printk_ratelimit())
			usb_err(instance->usbatm, "invalid response length to cm %#x: %d\n",
				cm, actlen);
		ret = -EIO;
		goto fail;
	}

	/* check the return status and copy the data to the output buffer, if needed */
	for (offb = offd = 0; offd < rsize && offb < actlen; offb += CMD_PACKET_SIZE) {
		if (rbuf[offb] != cm) {
			if (printk_ratelimit())
				usb_err(instance->usbatm, "wrong cm %#x in response to cm %#x\n",
					rbuf[offb], cm);
			ret = -EIO;
			goto fail;
		}
		if (rbuf[offb + 1] != CM_STATUS_SUCCESS) {
			if (printk_ratelimit())
				usb_err(instance->usbatm, "response to cm %#x failed: %#x\n",
					cm, rbuf[offb + 1]);
			ret = -EIO;
			goto fail;
		}
		if (offd >= rsize)
			break;
		memcpy(rdata + offd, rbuf + offb + 4, min_t(int, stride, rsize - offd));
		offd += stride;
	}

	ret = offd;
	usb_dbg(instance->usbatm, "cm %#x\n", cm);
fail:
	mutex_unlock(&instance->cm_serialize);
err:
	return ret;
}

static int cxacru_cm_get_array(struct cxacru_data *instance, enum cxacru_cm_request cm,
			       u32 *data, int size)
{
	int ret, len;
	__le32 *buf;
	int offb;
	unsigned int offd;
	const int stride = CMD_PACKET_SIZE / (4 * 2) - 1;
	int buflen =  ((size - 1) / stride + 1 + size * 2) * 4;

	buf = kmalloc(buflen, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = cxacru_cm(instance, cm, NULL, 0, (u8 *) buf, buflen);
	if (ret < 0)
		goto cleanup;

	/* len > 0 && len % 4 == 0 guaranteed by cxacru_cm() */
	len = ret / 4;
	for (offb = 0; offb < len; ) {
		int l = le32_to_cpu(buf[offb++]);

		if (l < 0 || l > stride || l > (len - offb) / 2) {
			if (printk_ratelimit())
				usb_err(instance->usbatm, "invalid data length from cm %#x: %d\n",
					cm, l);
			ret = -EIO;
			goto cleanup;
		}
		while (l--) {
			offd = le32_to_cpu(buf[offb++]);
			if (offd >= size) {
				if (printk_ratelimit())
					usb_err(instance->usbatm, "wrong index %#x in response to cm %#x\n",
						offd, cm);
				ret = -EIO;
				goto cleanup;
			}
			data[offd] = le32_to_cpu(buf[offb++]);
		}
	}

	ret = 0;

cleanup:
	kfree(buf);
	return ret;
}

static int cxacru_card_status(struct cxacru_data *instance)
{
	int ret = cxacru_cm(instance, CM_REQUEST_CARD_GET_STATUS, NULL, 0, NULL, 0);

	if (ret < 0) {		/* firmware not loaded */
		usb_dbg(instance->usbatm, "cxacru_adsl_start: CARD_GET_STATUS returned %d\n", ret);
		return ret;
	}
	return 0;
}

static void cxacru_remove_device_files(struct usbatm_data *usbatm_instance,
		struct atm_dev *atm_dev)
{
	struct usb_interface *intf = usbatm_instance->usb_intf;

	#define CXACRU_DEVICE_REMOVE_FILE(_name) \
		device_remove_file(&intf->dev, &dev_attr_##_name);
	CXACRU_ALL_FILES(REMOVE);
	#undef CXACRU_DEVICE_REMOVE_FILE
}

static int cxacru_atm_start(struct usbatm_data *usbatm_instance,
		struct atm_dev *atm_dev)
{
	struct cxacru_data *instance = usbatm_instance->driver_data;
	struct usb_interface *intf = usbatm_instance->usb_intf;
	int ret;
	int start_polling = 1;

	dev_dbg(&intf->dev, "%s\n", __func__);

	/* Read MAC address */
	ret = cxacru_cm(instance, CM_REQUEST_CARD_GET_MAC_ADDRESS, NULL, 0,
			atm_dev->esi, sizeof(atm_dev->esi));
	if (ret < 0) {
		atm_err(usbatm_instance, "cxacru_atm_start: CARD_GET_MAC_ADDRESS returned %d\n", ret);
		return ret;
	}

	#define CXACRU_DEVICE_CREATE_FILE(_name) \
		ret = device_create_file(&intf->dev, &dev_attr_##_name); \
		if (unlikely(ret)) \
			goto fail_sysfs;
	CXACRU_ALL_FILES(CREATE);
	#undef CXACRU_DEVICE_CREATE_FILE

	/* start ADSL */
	mutex_lock(&instance->adsl_state_serialize);
	ret = cxacru_cm(instance, CM_REQUEST_CHIP_ADSL_LINE_START, NULL, 0, NULL, 0);
	if (ret < 0)
		atm_err(usbatm_instance, "cxacru_atm_start: CHIP_ADSL_LINE_START returned %d\n", ret);

	/* Start status polling */
	mutex_lock(&instance->poll_state_serialize);
	switch (instance->poll_state) {
	case CXPOLL_STOPPED:
		/* start polling */
		instance->poll_state = CXPOLL_POLLING;
		break;

	case CXPOLL_STOPPING:
		/* abort stop request */
		instance->poll_state = CXPOLL_POLLING;
		/* fall through */
	case CXPOLL_POLLING:
	case CXPOLL_SHUTDOWN:
		/* don't start polling */
		start_polling = 0;
	}
	mutex_unlock(&instance->poll_state_serialize);
	mutex_unlock(&instance->adsl_state_serialize);

	printk(KERN_INFO "%s%d: %s %pM\n", atm_dev->type, atm_dev->number,
			usbatm_instance->description, atm_dev->esi);

	if (start_polling)
		cxacru_poll_status(&instance->poll_work.work);
	return 0;

fail_sysfs:
	usb_err(usbatm_instance, "cxacru_atm_start: device_create_file failed (%d)\n", ret);
	cxacru_remove_device_files(usbatm_instance, atm_dev);
	return ret;
}

static void cxacru_poll_status(struct work_struct *work)
{
	struct cxacru_data *instance =
		container_of(work, struct cxacru_data, poll_work.work);
	u32 buf[CXINF_MAX] = {};
	struct usbatm_data *usbatm = instance->usbatm;
	struct atm_dev *atm_dev = usbatm->atm_dev;
	int keep_polling = 1;
	int ret;

	ret = cxacru_cm_get_array(instance, CM_REQUEST_CARD_INFO_GET, buf, CXINF_MAX);
	if (ret < 0) {
		if (ret != -ESHUTDOWN)
			atm_warn(usbatm, "poll status: error %d\n", ret);

		mutex_lock(&instance->poll_state_serialize);
		if (instance->poll_state != CXPOLL_SHUTDOWN) {
			instance->poll_state = CXPOLL_STOPPED;

			if (ret != -ESHUTDOWN)
				atm_warn(usbatm, "polling disabled, set adsl_state"
						" to 'start' or 'poll' to resume\n");
		}
		mutex_unlock(&instance->poll_state_serialize);
		goto reschedule;
	}

	memcpy(instance->card_info, buf, sizeof(instance->card_info));

	if (instance->adsl_status != buf[CXINF_LINE_STARTABLE]) {
		instance->adsl_status = buf[CXINF_LINE_STARTABLE];

		switch (instance->adsl_status) {
		case 0:
			atm_printk(KERN_INFO, usbatm, "ADSL state: running\n");
			break;

		case 1:
			atm_printk(KERN_INFO, usbatm, "ADSL state: stopped\n");
			break;

		default:
			atm_printk(KERN_INFO, usbatm, "Unknown adsl status %02x\n", instance->adsl_status);
			break;
		}
	}

	if (instance->line_status == buf[CXINF_LINE_STATUS])
		goto reschedule;

	instance->line_status = buf[CXINF_LINE_STATUS];
	switch (instance->line_status) {
	case 0:
		atm_dev_signal_change(atm_dev, ATM_PHY_SIG_LOST);
		atm_info(usbatm, "ADSL line: down\n");
		break;

	case 1:
		atm_dev_signal_change(atm_dev, ATM_PHY_SIG_LOST);
		atm_info(usbatm, "ADSL line: attempting to activate\n");
		break;

	case 2:
		atm_dev_signal_change(atm_dev, ATM_PHY_SIG_LOST);
		atm_info(usbatm, "ADSL line: training\n");
		break;

	case 3:
		atm_dev_signal_change(atm_dev, ATM_PHY_SIG_LOST);
		atm_info(usbatm, "ADSL line: channel analysis\n");
		break;

	case 4:
		atm_dev_signal_change(atm_dev, ATM_PHY_SIG_LOST);
		atm_info(usbatm, "ADSL line: exchange\n");
		break;

	case 5:
		atm_dev->link_rate = buf[CXINF_DOWNSTREAM_RATE] * 1000 / 424;
		atm_dev_signal_change(atm_dev, ATM_PHY_SIG_FOUND);

		atm_info(usbatm, "ADSL line: up (%d kb/s down | %d kb/s up)\n",
		     buf[CXINF_DOWNSTREAM_RATE], buf[CXINF_UPSTREAM_RATE]);
		break;

	case 6:
		atm_dev_signal_change(atm_dev, ATM_PHY_SIG_LOST);
		atm_info(usbatm, "ADSL line: waiting\n");
		break;

	case 7:
		atm_dev_signal_change(atm_dev, ATM_PHY_SIG_LOST);
		atm_info(usbatm, "ADSL line: initializing\n");
		break;

	default:
		atm_dev_signal_change(atm_dev, ATM_PHY_SIG_UNKNOWN);
		atm_info(usbatm, "Unknown line state %02x\n", instance->line_status);
		break;
	}
reschedule:

	mutex_lock(&instance->poll_state_serialize);
	if (instance->poll_state == CXPOLL_STOPPING &&
				instance->adsl_status == 1 && /* stopped */
				instance->line_status == 0) /* down */
		instance->poll_state = CXPOLL_STOPPED;

	if (instance->poll_state == CXPOLL_STOPPED)
		keep_polling = 0;
	mutex_unlock(&instance->poll_state_serialize);

	if (keep_polling)
		schedule_delayed_work(&instance->poll_work,
				round_jiffies_relative(POLL_INTERVAL*HZ));
}

static int cxacru_fw(struct usb_device *usb_dev, enum cxacru_fw_request fw,
		     u8 code1, u8 code2, u32 addr, const u8 *data, int size)
{
	int ret;
	u8 *buf;
	int offd, offb;
	const int stride = CMD_PACKET_SIZE - 8;

	buf = (u8 *) __get_free_page(GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	offb = offd = 0;
	do {
		int l = min_t(int, stride, size - offd);

		buf[offb++] = fw;
		buf[offb++] = l;
		buf[offb++] = code1;
		buf[offb++] = code2;
		put_unaligned(cpu_to_le32(addr), (__le32 *)(buf + offb));
		offb += 4;
		addr += l;
		if (l)
			memcpy(buf + offb, data + offd, l);
		if (l < stride)
			memset(buf + offb + l, 0, stride - l);
		offb += stride;
		offd += stride;
		if ((offb >= PAGE_SIZE) || (offd >= size)) {
			ret = usb_bulk_msg(usb_dev, usb_sndbulkpipe(usb_dev, CXACRU_EP_CMD),
					   buf, offb, NULL, CMD_TIMEOUT);
			if (ret < 0) {
				dev_dbg(&usb_dev->dev, "sending fw %#x failed\n", fw);
				goto cleanup;
			}
			offb = 0;
		}
	} while (offd < size);
	dev_dbg(&usb_dev->dev, "sent fw %#x\n", fw);

	ret = 0;

cleanup:
	free_page((unsigned long) buf);
	return ret;
}

static void cxacru_upload_firmware(struct cxacru_data *instance,
				   const struct firmware *fw,
				   const struct firmware *bp)
{
	int ret;
	struct usbatm_data *usbatm = instance->usbatm;
	struct usb_device *usb_dev = usbatm->usb_dev;
	__le16 signature[] = { usb_dev->descriptor.idVendor,
			       usb_dev->descriptor.idProduct };
	__le32 val;

	usb_dbg(usbatm, "%s\n", __func__);

	/* FirmwarePllFClkValue */
	val = cpu_to_le32(instance->modem_type->pll_f_clk);
	ret = cxacru_fw(usb_dev, FW_WRITE_MEM, 0x2, 0x0, PLLFCLK_ADDR, (u8 *) &val, 4);
	if (ret) {
		usb_err(usbatm, "FirmwarePllFClkValue failed: %d\n", ret);
		return;
	}

	/* FirmwarePllBClkValue */
	val = cpu_to_le32(instance->modem_type->pll_b_clk);
	ret = cxacru_fw(usb_dev, FW_WRITE_MEM, 0x2, 0x0, PLLBCLK_ADDR, (u8 *) &val, 4);
	if (ret) {
		usb_err(usbatm, "FirmwarePllBClkValue failed: %d\n", ret);
		return;
	}

	/* Enable SDRAM */
	val = cpu_to_le32(SDRAM_ENA);
	ret = cxacru_fw(usb_dev, FW_WRITE_MEM, 0x2, 0x0, SDRAMEN_ADDR, (u8 *) &val, 4);
	if (ret) {
		usb_err(usbatm, "Enable SDRAM failed: %d\n", ret);
		return;
	}

	/* Firmware */
	usb_info(usbatm, "loading firmware\n");
	ret = cxacru_fw(usb_dev, FW_WRITE_MEM, 0x2, 0x0, FW_ADDR, fw->data, fw->size);
	if (ret) {
		usb_err(usbatm, "Firmware upload failed: %d\n", ret);
		return;
	}

	/* Boot ROM patch */
	if (instance->modem_type->boot_rom_patch) {
		usb_info(usbatm, "loading boot ROM patch\n");
		ret = cxacru_fw(usb_dev, FW_WRITE_MEM, 0x2, 0x0, BR_ADDR, bp->data, bp->size);
		if (ret) {
			usb_err(usbatm, "Boot ROM patching failed: %d\n", ret);
			return;
		}
	}

	/* Signature */
	ret = cxacru_fw(usb_dev, FW_WRITE_MEM, 0x2, 0x0, SIG_ADDR, (u8 *) signature, 4);
	if (ret) {
		usb_err(usbatm, "Signature storing failed: %d\n", ret);
		return;
	}

	usb_info(usbatm, "starting device\n");
	if (instance->modem_type->boot_rom_patch) {
		val = cpu_to_le32(BR_ADDR);
		ret = cxacru_fw(usb_dev, FW_WRITE_MEM, 0x2, 0x0, BR_STACK_ADDR, (u8 *) &val, 4);
	} else {
		ret = cxacru_fw(usb_dev, FW_GOTO_MEM, 0x0, 0x0, FW_ADDR, NULL, 0);
	}
	if (ret) {
		usb_err(usbatm, "Passing control to firmware failed: %d\n", ret);
		return;
	}

	/* Delay to allow firmware to start up. */
	msleep_interruptible(1000);

	usb_clear_halt(usb_dev, usb_sndbulkpipe(usb_dev, CXACRU_EP_CMD));
	usb_clear_halt(usb_dev, usb_rcvbulkpipe(usb_dev, CXACRU_EP_CMD));
	usb_clear_halt(usb_dev, usb_sndbulkpipe(usb_dev, CXACRU_EP_DATA));
	usb_clear_halt(usb_dev, usb_rcvbulkpipe(usb_dev, CXACRU_EP_DATA));

	ret = cxacru_cm(instance, CM_REQUEST_CARD_GET_STATUS, NULL, 0, NULL, 0);
	if (ret < 0) {
		usb_err(usbatm, "modem failed to initialize: %d\n", ret);
		return;
	}
}

static int cxacru_find_firmware(struct cxacru_data *instance,
				char *phase, const struct firmware **fw_p)
{
	struct usbatm_data *usbatm = instance->usbatm;
	struct device *dev = &usbatm->usb_intf->dev;
	char buf[16];

	sprintf(buf, "cxacru-%s.bin", phase);
	usb_dbg(usbatm, "cxacru_find_firmware: looking for %s\n", buf);

	if (request_firmware(fw_p, buf, dev)) {
		usb_dbg(usbatm, "no stage %s firmware found\n", phase);
		return -ENOENT;
	}

	usb_info(usbatm, "found firmware %s\n", buf);

	return 0;
}

static int cxacru_heavy_init(struct usbatm_data *usbatm_instance,
			     struct usb_interface *usb_intf)
{
	const struct firmware *fw, *bp;
	struct cxacru_data *instance = usbatm_instance->driver_data;
	int ret = cxacru_find_firmware(instance, "fw", &fw);

	if (ret) {
		usb_warn(usbatm_instance, "firmware (cxacru-fw.bin) unavailable (system misconfigured?)\n");
		return ret;
	}

	if (instance->modem_type->boot_rom_patch) {
		ret = cxacru_find_firmware(instance, "bp", &bp);
		if (ret) {
			usb_warn(usbatm_instance, "boot ROM patch (cxacru-bp.bin) unavailable (system misconfigured?)\n");
			release_firmware(fw);
			return ret;
		}
	}

	cxacru_upload_firmware(instance, fw, bp);

	if (instance->modem_type->boot_rom_patch)
		release_firmware(bp);
	release_firmware(fw);

	ret = cxacru_card_status(instance);
	if (ret)
		usb_dbg(usbatm_instance, "modem initialisation failed\n");
	else
		usb_dbg(usbatm_instance, "done setting up the modem\n");

	return ret;
}

static int cxacru_bind(struct usbatm_data *usbatm_instance,
		       struct usb_interface *intf, const struct usb_device_id *id)
{
	struct cxacru_data *instance;
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct usb_host_endpoint *cmd_ep = usb_dev->ep_in[CXACRU_EP_CMD];
	int ret;

	/* instance init */
	instance = kzalloc(sizeof(*instance), GFP_KERNEL);
	if (!instance)
		return -ENOMEM;

	instance->usbatm = usbatm_instance;
	instance->modem_type = (struct cxacru_modem_type *) id->driver_info;

	mutex_init(&instance->poll_state_serialize);
	instance->poll_state = CXPOLL_STOPPED;
	instance->line_status = -1;
	instance->adsl_status = -1;

	mutex_init(&instance->adsl_state_serialize);

	instance->rcv_buf = (u8 *) __get_free_page(GFP_KERNEL);
	if (!instance->rcv_buf) {
		usb_dbg(usbatm_instance, "cxacru_bind: no memory for rcv_buf\n");
		ret = -ENOMEM;
		goto fail;
	}
	instance->snd_buf = (u8 *) __get_free_page(GFP_KERNEL);
	if (!instance->snd_buf) {
		usb_dbg(usbatm_instance, "cxacru_bind: no memory for snd_buf\n");
		ret = -ENOMEM;
		goto fail;
	}
	instance->rcv_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!instance->rcv_urb) {
		ret = -ENOMEM;
		goto fail;
	}
	instance->snd_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!instance->snd_urb) {
		ret = -ENOMEM;
		goto fail;
	}

	if (!cmd_ep) {
		usb_dbg(usbatm_instance, "cxacru_bind: no command endpoint\n");
		ret = -ENODEV;
		goto fail;
	}

	if ((cmd_ep->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
			== USB_ENDPOINT_XFER_INT) {
		usb_fill_int_urb(instance->rcv_urb,
			usb_dev, usb_rcvintpipe(usb_dev, CXACRU_EP_CMD),
			instance->rcv_buf, PAGE_SIZE,
			cxacru_blocking_completion, &instance->rcv_done, 1);

		usb_fill_int_urb(instance->snd_urb,
			usb_dev, usb_sndintpipe(usb_dev, CXACRU_EP_CMD),
			instance->snd_buf, PAGE_SIZE,
			cxacru_blocking_completion, &instance->snd_done, 4);
	} else {
		usb_fill_bulk_urb(instance->rcv_urb,
			usb_dev, usb_rcvbulkpipe(usb_dev, CXACRU_EP_CMD),
			instance->rcv_buf, PAGE_SIZE,
			cxacru_blocking_completion, &instance->rcv_done);

		usb_fill_bulk_urb(instance->snd_urb,
			usb_dev, usb_sndbulkpipe(usb_dev, CXACRU_EP_CMD),
			instance->snd_buf, PAGE_SIZE,
			cxacru_blocking_completion, &instance->snd_done);
	}

	mutex_init(&instance->cm_serialize);

	INIT_DELAYED_WORK(&instance->poll_work, cxacru_poll_status);

	usbatm_instance->driver_data = instance;

	usbatm_instance->flags = (cxacru_card_status(instance) ? 0 : UDSL_SKIP_HEAVY_INIT);

	return 0;

 fail:
	free_page((unsigned long) instance->snd_buf);
	free_page((unsigned long) instance->rcv_buf);
	usb_free_urb(instance->snd_urb);
	usb_free_urb(instance->rcv_urb);
	kfree(instance);

	return ret;
}

static void cxacru_unbind(struct usbatm_data *usbatm_instance,
		struct usb_interface *intf)
{
	struct cxacru_data *instance = usbatm_instance->driver_data;
	int is_polling = 1;

	usb_dbg(usbatm_instance, "cxacru_unbind entered\n");

	if (!instance) {
		usb_dbg(usbatm_instance, "cxacru_unbind: NULL instance!\n");
		return;
	}

	mutex_lock(&instance->poll_state_serialize);
	BUG_ON(instance->poll_state == CXPOLL_SHUTDOWN);

	/* ensure that status polling continues unless
	 * it has already stopped */
	if (instance->poll_state == CXPOLL_STOPPED)
		is_polling = 0;

	/* stop polling from being stopped or started */
	instance->poll_state = CXPOLL_SHUTDOWN;
	mutex_unlock(&instance->poll_state_serialize);

	if (is_polling)
		cancel_delayed_work_sync(&instance->poll_work);

	usb_kill_urb(instance->snd_urb);
	usb_kill_urb(instance->rcv_urb);
	usb_free_urb(instance->snd_urb);
	usb_free_urb(instance->rcv_urb);

	free_page((unsigned long) instance->snd_buf);
	free_page((unsigned long) instance->rcv_buf);

	kfree(instance);

	usbatm_instance->driver_data = NULL;
}

static const struct cxacru_modem_type cxacru_cafe = {
	.pll_f_clk = 0x02d874df,
	.pll_b_clk = 0x0196a51a,
	.boot_rom_patch = 1,
};

static const struct cxacru_modem_type cxacru_cb00 = {
	.pll_f_clk = 0x5,
	.pll_b_clk = 0x3,
	.boot_rom_patch = 0,
};

static const struct usb_device_id cxacru_usb_ids[] = {
	{ /* V = Conexant			P = ADSL modem (Euphrates project)	*/
		USB_DEVICE(0x0572, 0xcafe),	.driver_info = (unsigned long) &cxacru_cafe
	},
	{ /* V = Conexant			P = ADSL modem (Hasbani project)	*/
		USB_DEVICE(0x0572, 0xcb00),	.driver_info = (unsigned long) &cxacru_cb00
	},
	{ /* V = Conexant			P = ADSL modem				*/
		USB_DEVICE(0x0572, 0xcb01),	.driver_info = (unsigned long) &cxacru_cb00
	},
	{ /* V = Conexant			P = ADSL modem (Well PTI-800) */
		USB_DEVICE(0x0572, 0xcb02),	.driver_info = (unsigned long) &cxacru_cb00
	},
	{ /* V = Conexant			P = ADSL modem				*/
		USB_DEVICE(0x0572, 0xcb06),	.driver_info = (unsigned long) &cxacru_cb00
	},
	{ /* V = Conexant			P = ADSL modem (ZTE ZXDSL 852)		*/
		USB_DEVICE(0x0572, 0xcb07),	.driver_info = (unsigned long) &cxacru_cb00
	},
	{ /* V = Olitec				P = ADSL modem version 2		*/
		USB_DEVICE(0x08e3, 0x0100),	.driver_info = (unsigned long) &cxacru_cafe
	},
	{ /* V = Olitec				P = ADSL modem version 3		*/
		USB_DEVICE(0x08e3, 0x0102),	.driver_info = (unsigned long) &cxacru_cb00
	},
	{ /* V = Trust/Amigo Technology Co.	P = AMX-CA86U				*/
		USB_DEVICE(0x0eb0, 0x3457),	.driver_info = (unsigned long) &cxacru_cafe
	},
	{ /* V = Zoom				P = 5510				*/
		USB_DEVICE(0x1803, 0x5510),	.driver_info = (unsigned long) &cxacru_cb00
	},
	{ /* V = Draytek			P = Vigor 318				*/
		USB_DEVICE(0x0675, 0x0200),	.driver_info = (unsigned long) &cxacru_cb00
	},
	{ /* V = Zyxel				P = 630-C1 aka OMNI ADSL USB (Annex A)	*/
		USB_DEVICE(0x0586, 0x330a),	.driver_info = (unsigned long) &cxacru_cb00
	},
	{ /* V = Zyxel				P = 630-C3 aka OMNI ADSL USB (Annex B)	*/
		USB_DEVICE(0x0586, 0x330b),	.driver_info = (unsigned long) &cxacru_cb00
	},
	{ /* V = Aethra				P = Starmodem UM1020			*/
		USB_DEVICE(0x0659, 0x0020),	.driver_info = (unsigned long) &cxacru_cb00
	},
	{ /* V = Aztech Systems			P = ? AKA Pirelli AUA-010		*/
		USB_DEVICE(0x0509, 0x0812),	.driver_info = (unsigned long) &cxacru_cb00
	},
	{ /* V = Netopia			P = Cayman 3341(Annex A)/3351(Annex B)	*/
		USB_DEVICE(0x100d, 0xcb01),	.driver_info = (unsigned long) &cxacru_cb00
	},
	{ /* V = Netopia			P = Cayman 3342(Annex A)/3352(Annex B)	*/
		USB_DEVICE(0x100d, 0x3342),	.driver_info = (unsigned long) &cxacru_cb00
	},
	{}
};

MODULE_DEVICE_TABLE(usb, cxacru_usb_ids);

static struct usbatm_driver cxacru_driver = {
	.driver_name	= cxacru_driver_name,
	.bind		= cxacru_bind,
	.heavy_init	= cxacru_heavy_init,
	.unbind		= cxacru_unbind,
	.atm_start	= cxacru_atm_start,
	.atm_stop	= cxacru_remove_device_files,
	.bulk_in	= CXACRU_EP_DATA,
	.bulk_out	= CXACRU_EP_DATA,
	.rx_padding	= 3,
	.tx_padding	= 11,
};

static int cxacru_usb_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	char buf[15];

	/* Avoid ADSL routers (cx82310_eth).
	 * Abort if bDeviceClass is 0xff and iProduct is "USB NET CARD".
	 */
	if (usb_dev->descriptor.bDeviceClass == USB_CLASS_VENDOR_SPEC
			&& usb_string(usb_dev, usb_dev->descriptor.iProduct,
				buf, sizeof(buf)) > 0) {
		if (!strcmp(buf, "USB NET CARD")) {
			dev_info(&intf->dev, "ignoring cx82310_eth device\n");
			return -ENODEV;
		}
	}

	return usbatm_usb_probe(intf, id, &cxacru_driver);
}

static struct usb_driver cxacru_usb_driver = {
	.name		= cxacru_driver_name,
	.probe		= cxacru_usb_probe,
	.disconnect	= usbatm_usb_disconnect,
	.id_table	= cxacru_usb_ids
};

module_usb_driver(cxacru_usb_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
