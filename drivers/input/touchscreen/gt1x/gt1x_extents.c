/* drivers/input/touchscreen/gt1x_extents.c
 *
 * 2010 - 2014 Goodix Technology.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the GOODiX's CTP IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Version: 1.4
 * Release Date:  2015/07/10
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/input.h>

#include <linux/uaccess.h>
#include <linux/proc_fs.h>	/*proc */

#include <asm/ioctl.h>
#include "gt1x_generic.h"

#if GTP_GESTURE_WAKEUP

#define GESTURE_NODE "goodix_gesture"
#define GESTURE_MAX_POINT_COUNT    64

#pragma pack(1)
typedef struct {
	u8 ic_msg[6];		/*from the first byte */
	u8 gestures[4];
	u8 data[3 + GESTURE_MAX_POINT_COUNT * 4 + 80];	/*80 bytes for extra data */
} st_gesture_data;
#pragma pack()

#define SETBIT(longlong, bit)   (longlong[bit/8] |=  (1 << bit%8))
#define CLEARBIT(longlong, bit) (longlong[bit/8] &= (~(1 << bit%8)))
#define QUERYBIT(longlong, bit) (!!(longlong[bit/8] & (1 << bit%8)))

#define CHKBITS_32          32
#define CHKBITS_16          16
#define CHKBITS_8           8

int gesture_enabled;    /* module switch */
DOZE_T gesture_doze_status = DOZE_DISABLED; /* doze status */

static u8 gestures_flag[32]; /* gesture flag, every bit stands for a gesture */
static st_gesture_data gesture_data; /* gesture data buffer */
static struct mutex gesture_data_mutex; /* lock for gesture data */

static ssize_t gt1x_gesture_data_read(struct file *file, char __user *page, size_t size, loff_t *ppos)
{
	s32 ret = -1;
	GTP_DEBUG("visit gt1x_gesture_data_read. ppos:%d", (int)*ppos);
	if (*ppos) {
		return 0;
	}
	if (size == 4) {
		ret = copy_to_user(((u8 __user *) page), "GT1X", 4);
		return 4;
	}
	ret = simple_read_from_buffer(page, size, ppos, &gesture_data, sizeof(gesture_data));

	GTP_DEBUG("Got the gesture data.");
	return ret;
}

static ssize_t gt1x_gesture_data_write(struct file *filp, const char __user *buff, size_t len, loff_t *off)
{
	s32 ret = 0;

	GTP_DEBUG_FUNC();

	ret = copy_from_user(&gesture_enabled, buff, 1);
	if (ret) {
		GTP_ERROR("copy_from_user failed.");
		return -EPERM;
	}

	GTP_DEBUG("gesture enabled:%x, ret:%d", gesture_enabled, ret);

	return len;
}

/**
 * calc_checksum - Calc checksum.
 * @buf: data to be calc
 * @len: length of buf.
 * @bits: checkbits
 * Return true-pass, false:not pass.
 */
static bool calc_checksum(u8 *buf, int len, int bits)
{
	int i;

	if (bits == CHKBITS_16) {
		u16 chksum, *b = (u16 *)buf;

		if (len % 2) {
			return false;
		}

		len /= 2;
		for (i = 0, chksum = 0; i < len; i++) {
			if (i == len - 1)
				chksum += le16_to_cpu(b[i]);
			else
				chksum += be16_to_cpu(b[i]);
		}
		return chksum == 0 ? true : false;
	} else if (bits == CHKBITS_8) {
		u8 chksum;

		for (i = 0, chksum = 0; i < len; i++) {
			chksum += buf[i];
		}
		return chksum == 0 ? true : false;
	}
	return false;
}

int gesture_enter_doze(void)
{
	int retry = 0;

	GTP_DEBUG_FUNC();
	GTP_DEBUG("Entering doze mode...");
	while (retry++ < 5) {
		if (!gt1x_send_cmd(0x08, 0)) {
			gesture_doze_status = DOZE_ENABLED;
			GTP_DEBUG("Working in doze mode!");
			return 0;
		}
		usleep_range(10000, 11000);
	}
	GTP_ERROR("Send doze cmd failed.");
	return -1;
}

s32 gesture_event_handler(struct input_dev *dev)
{
	u8 doze_buf[4] = { 0 }, ges_type;
	static int err_flag1, err_flag2;
	int len, extra_len, need_chk;
	unsigned int key_code;
	s32 ret = 0;

	if (DOZE_ENABLED != gesture_doze_status) {
		return -1;
	}

	/** package: -head 4B + track points + extra info-
	 * - head -
	 *  doze_buf[0]: gesture type,
	 *  doze_buf[1]: number of gesture points ,
	 *  doze_buf[2]: protocol type,
	 *  doze_buf[3]: gesture extra data length.
	 */
	ret = gt1x_i2c_read(GTP_REG_WAKEUP_GESTURE, doze_buf, 4);
	if (ret < 0) {
		return 0;
	}

	ges_type = doze_buf[0];
	len = doze_buf[1];
	need_chk = doze_buf[2] & 0x80;
	extra_len = doze_buf[3];

	GTP_DEBUG("0x%x = 0x%02X,0x%02X,0x%02X,0x%02X", GTP_REG_WAKEUP_GESTURE,
			doze_buf[0], doze_buf[1], doze_buf[2], doze_buf[3]);

	if (len > GESTURE_MAX_POINT_COUNT) {
		GTP_ERROR("Gesture contain too many points!(%d)", len);
		len = GESTURE_MAX_POINT_COUNT;
	}

	if (extra_len > 32) {
		GTP_ERROR("Gesture contain too many extra data!(%d)", extra_len);
		extra_len = 32;
	}

	/* get gesture extra info */
	if (extra_len >= 0) {
		u8 ges_data[extra_len + 1];

		/* head 4 + extra data * 4 + chksum 1 */
		ret = gt1x_i2c_read(GTP_REG_WAKEUP_GESTURE + 4,
				ges_data, extra_len + 1);
		if (ret < 0) {
			GTP_ERROR("Read extra gesture data failed.");
			return 0;
		}

		if (likely(need_chk)) { /* calc checksum */
			bool val;

			ges_data[extra_len] += doze_buf[0] + doze_buf[1]
				+ doze_buf[2] + doze_buf[3];

			val = calc_checksum(ges_data, extra_len + 1, CHKBITS_8);
			if (unlikely(!val)) { /* check failed */
				GTP_ERROR("Gesture checksum error.");
				if (err_flag1) {
					err_flag1 = 0;
					ret = 0;
					goto clear_reg;
				} else {
					/* just return 0 without clear reg,
					   this will receive another int, we
					   check the data in the next frame */
					err_flag1 = 1;
					return 0;
				}
			}

			err_flag1 = 0;
		}

		mutex_lock(&gesture_data_mutex);
		memcpy(&gesture_data.data[4 + len * 4], ges_data, extra_len);
		mutex_unlock(&gesture_data_mutex);
	}

	/* check gesture type (if available?) */
	if (ges_type == 0 || !QUERYBIT(gestures_flag, ges_type)) {
		GTP_INFO("Gesture[0x%02X] has been disabled.", doze_buf[0]);
		doze_buf[0] = 0x00;
		gt1x_i2c_write(GTP_REG_WAKEUP_GESTURE, doze_buf, 1);
		gesture_enter_doze();
		return 0;
	}

	/* get gesture point data */
	if (len > 0) { /* coor num * 4 + chksum 2*/
		u8 ges_data[len * 4 + 2];

		ret = gt1x_i2c_read(GES_BUFFER_ADDR, ges_data, len * 4);
		if (ret < 0) {
			GTP_ERROR("Read gesture data failed.");
			return 0;
		}

		/* checksum reg for gesture point data */
		ret = gt1x_i2c_read(0x819F, &ges_data[len * 4], 2);
		if (ret < 0) {
			GTP_ERROR("Read gesture data failed.");
			return 0;
		}

		if (likely(need_chk)) {
			bool val = calc_checksum(ges_data,
					len * 4 + 2, CHKBITS_16);
			if (unlikely(!val)) { /* check failed */
				GTP_ERROR("Gesture checksum error.");
				if (err_flag2) {
					err_flag2 = 0;
					ret = 0;
					goto clear_reg;
				} else {
					err_flag2 = 1;
					return 0;
				}
			}

			err_flag2 = 0;
		}

		mutex_lock(&gesture_data_mutex);
		memcpy(&gesture_data.data[4], ges_data, len * 4);
		mutex_unlock(&gesture_data_mutex);
	}

	mutex_lock(&gesture_data_mutex);
	gesture_data.data[0] = ges_type;	/*gesture type*/
	gesture_data.data[1] = len;	        /*gesture points number*/
	gesture_data.data[2] = doze_buf[2] & 0x7F; /*protocol type*/
	gesture_data.data[3] = extra_len;   /*gesture date length*/
	mutex_unlock(&gesture_data_mutex);

	/* get key code */
	key_code = ges_type < 16 ? KEY_GES_CUSTOM : KEY_GES_REGULAR;
	GTP_DEBUG("Gesture: 0x%02X, points: %d", doze_buf[0], doze_buf[1]);

	input_report_key(dev, key_code, 1);
	input_sync(dev);
	input_report_key(dev, key_code, 0);
	input_sync(dev);

clear_reg:
	doze_buf[0] = 0; /*clear ges flag*/
	gt1x_i2c_write(GTP_REG_WAKEUP_GESTURE, doze_buf, 1);
	return ret;
}

void gesture_clear_wakeup_data(void)
{
	mutex_lock(&gesture_data_mutex);
	memset(gesture_data.data, 0, 4);
	mutex_unlock(&gesture_data_mutex);
}

void gt1x_gesture_debug(int on)
{
	if (on) {
		gesture_enabled = 1;
		memset(gestures_flag, 0xFF, sizeof(gestures_flag));
	} else {
		gesture_enabled = 0;
		memset(gestures_flag, 0x00, sizeof(gestures_flag));
		gesture_doze_status = DOZE_DISABLED;
	}
	GTP_DEBUG("Gesture debug %s", on ? "on":"off");
}

#endif /* GTP_GESTURE_WAKEUP */

/*HotKnot module*/
#if GTP_HOTKNOT
#define HOTKNOT_NODE "hotknot"
#define HOTKNOT_VERSION  "GOODIX,GT1X"
u8 hotknot_enabled;
u8 hotknot_transfer_mode;

static int hotknot_open(struct inode *node, struct file *flip)
{
	GTP_DEBUG("Hotknot is enabled.");
	hotknot_enabled = 1;
	return 0;
}

static int hotknot_release(struct inode *node, struct file *filp)
{
	GTP_DEBUG("Hotknot is disabled.");
	hotknot_enabled = 0;
	return 0;
}

static s32 hotknot_enter_transfer_mode(void)
{
	int ret = 0;
	u8 buffer[5] = { 0 };

	hotknot_transfer_mode = 1;
#if GTP_ESD_PROTECT
	gt1x_esd_switch(SWITCH_OFF);
#endif

	gt1x_irq_disable();
	gt1x_send_cmd(GTP_CMD_HN_TRANSFER, 0);
	msleep(100);
	gt1x_irq_enable();

	ret = gt1x_i2c_read(0x8140, buffer, sizeof(buffer));
	if (ret) {
		hotknot_transfer_mode = 0;
		return ret;
	}

	buffer[4] = 0;
	GTP_DEBUG("enter transfer mode: %s ", buffer);
	if (strcmp(buffer, "GHot")) {
		hotknot_transfer_mode = 0;
		return ERROR_HN_VER;
	}

	return 0;
}

static s32 hotknot_load_hotknot_subsystem(void)
{
	return hotknot_enter_transfer_mode();
}

static s32 hotknot_load_authentication_subsystem(void)
{
	s32 ret = 0;
	u8 buffer[5] = { 0 };
	ret = gt1x_hold_ss51_dsp_no_reset();
	if (ret < 0) {
		GTP_ERROR("Hold ss51 fail!");
		return ERROR;
	}

	if (gt1x_chip_type == CHIP_TYPE_GT1X) {
		GTP_INFO("hotknot load jump code.");
		ret = gt1x_load_patch(gt1x_patch_jump_fw, 4096, 0, 1024 * 8);
		if (ret < 0) {
			GTP_ERROR("Load jump code fail!");
			return ret;
		}
		GTP_INFO("hotknot load auth code.");
		ret = gt1x_load_patch(hotknot_auth_fw, 4096, 4096, 1024 * 8);
		if (ret < 0) {
			GTP_ERROR("Load auth system fail!");
			return ret;
		}
	} else { /* GT2X */
		GTP_INFO("hotknot load auth code.");
		ret = gt1x_load_patch(hotknot_auth_fw, 4096, 0, 1024 * 6);
		if (ret < 0) {
			GTP_ERROR("load auth system fail!");
			return ret;
		}
	}

	ret = gt1x_startup_patch();
	if (ret < 0) {
		GTP_ERROR("Startup auth system fail!");
		return ret;
	}
	ret = gt1x_i2c_read(GTP_REG_VERSION, buffer, 4);
	if (ret < 0) {
		GTP_ERROR("i2c read error!");
		return ERROR_IIC;
	}
	buffer[4] = 0;
	GTP_INFO("Current System version: %s", buffer);
	return 0;
}

static s32 hotknot_recovery_main_system(void)
{
	gt1x_irq_disable();
	gt1x_reset_guitar();
	gt1x_irq_enable();
#if GTP_ESD_PROTECT
	gt1x_esd_switch(SWITCH_ON);
#endif
	hotknot_transfer_mode = 0;
	return 0;
}

#if HOTKNOT_BLOCK_RW
DECLARE_WAIT_QUEUE_HEAD(bp_waiter);
static u8 got_hotknot_state;
static u8 got_hotknot_extra_state;
static u8 wait_hotknot_state;
static u8 force_wake_flag;
static u8 block_enable;
s32 hotknot_paired_flag;

static s32 hotknot_block_rw(u8 rqst_hotknot_state, s32 wait_hotknot_timeout)
{
	s32 ret = 0;

	wait_hotknot_state |= rqst_hotknot_state;
	GTP_DEBUG("Goodix tool received wait polling state:0x%x,timeout:%d, all wait state:0x%x", rqst_hotknot_state, wait_hotknot_timeout, wait_hotknot_state);
	got_hotknot_state &= (~rqst_hotknot_state);

	set_current_state(TASK_INTERRUPTIBLE);
	if (wait_hotknot_timeout <= 0) {
		wait_event_interruptible(bp_waiter, force_wake_flag || rqst_hotknot_state == (got_hotknot_state & rqst_hotknot_state));
	} else {
		wait_event_interruptible_timeout(bp_waiter, force_wake_flag || rqst_hotknot_state == (got_hotknot_state & rqst_hotknot_state), wait_hotknot_timeout);
	}

	wait_hotknot_state &= (~rqst_hotknot_state);

	if (rqst_hotknot_state != (got_hotknot_state & rqst_hotknot_state)) {
		GTP_ERROR("Wait 0x%x block polling waiter failed.", rqst_hotknot_state);
		ret = -1;
	}

	force_wake_flag = 0;
	return ret;
}

static void hotknot_wakeup_block(void)
{
	GTP_DEBUG("Manual wakeup all block polling waiter!");
	got_hotknot_state = 0;
	wait_hotknot_state = 0;
	force_wake_flag = 1;
	wake_up_interruptible(&bp_waiter);
}

s32 hotknot_event_handler(u8 *data)
{
	u8 hn_pxy_state = 0;
	u8 hn_pxy_state_bak = 0;
	static u8 hn_paired_cnt;
	u8 hn_state_buf[10] = { 0 };
	u8 finger = data[0];
	u8 id = 0;

	if (block_enable && !hotknot_paired_flag && (finger & 0x0F)) {
		id = data[1];
		hn_pxy_state = data[2] & 0x80;
		hn_pxy_state_bak = data[3] & 0x80;
		if ((32 == id) && (0x80 == hn_pxy_state) && (0x80 == hn_pxy_state_bak)) {
#ifdef HN_DBLCFM_PAIRED
			if (hn_paired_cnt++ < 2) {
				return 0;
			}
#endif
			GTP_DEBUG("HotKnot paired!");
			if (wait_hotknot_state & HN_DEVICE_PAIRED) {
				GTP_DEBUG("INT wakeup HN_DEVICE_PAIRED block polling waiter");
				got_hotknot_state |= HN_DEVICE_PAIRED;
				wake_up_interruptible(&bp_waiter);
			}
			block_enable = 0;
			hotknot_paired_flag = 1;
			return 0;
		} else {
			got_hotknot_state &= (~HN_DEVICE_PAIRED);
			hn_paired_cnt = 0;
		}
	}

	if (hotknot_paired_flag) {
		s32 ret = -1;
		ret = gt1x_i2c_read(GTP_REG_HN_STATE, hn_state_buf, 6);
		if (ret < 0) {
			GTP_ERROR("I2C transfer error. errno:%d\n ", ret);
			return 0;
		}

		got_hotknot_state = 0;

		GTP_DEBUG("wait_hotknot_state:%x", wait_hotknot_state);
		GTP_DEBUG("[0x8800~0x8803]=0x%x,0x%x,0x%x,0x%x", hn_state_buf[0], hn_state_buf[1], hn_state_buf[2], hn_state_buf[3]);

		if (wait_hotknot_state & HN_MASTER_SEND) {
			if ((0x03 == hn_state_buf[0]) || (0x04 == hn_state_buf[0])
			    || (0x07 == hn_state_buf[0])) {
				GTP_DEBUG("Wakeup HN_MASTER_SEND block polling waiter");
				got_hotknot_state |= HN_MASTER_SEND;
				got_hotknot_extra_state = hn_state_buf[0];
				wake_up_interruptible(&bp_waiter);
			}
		} else if (wait_hotknot_state & HN_SLAVE_RECEIVED) {
			if ((0x03 == hn_state_buf[1]) || (0x04 == hn_state_buf[1])
			    || (0x07 == hn_state_buf[1])) {
				GTP_DEBUG("Wakeup HN_SLAVE_RECEIVED block polling waiter:0x%x", hn_state_buf[1]);
				got_hotknot_state |= HN_SLAVE_RECEIVED;
				got_hotknot_extra_state = hn_state_buf[1];
				wake_up_interruptible(&bp_waiter);
			}
		} else if (wait_hotknot_state & HN_MASTER_DEPARTED) {
			if (0x07 == hn_state_buf[0]) {
				GTP_DEBUG("Wakeup HN_MASTER_DEPARTED block polling waiter");
				got_hotknot_state |= HN_MASTER_DEPARTED;
				wake_up_interruptible(&bp_waiter);
			}
		} else if (wait_hotknot_state & HN_SLAVE_DEPARTED) {
			if (0x07 == hn_state_buf[1]) {
				GTP_DEBUG("Wakeup HN_SLAVE_DEPARTED block polling waiter");
				got_hotknot_state |= HN_SLAVE_DEPARTED;
				wake_up_interruptible(&bp_waiter);
			}
		}
		return 0;
	}

	return -1;
}
#endif /*HOTKNOT_BLOCK_RW*/
#endif /*GTP_HOTKNOT*/

#define GOODIX_MAGIC_NUMBER        'G'
#define NEGLECT_SIZE_MASK           (~(_IOC_SIZEMASK << _IOC_SIZESHIFT))

#define GESTURE_ENABLE              _IO(GOODIX_MAGIC_NUMBER, 1)
#define GESTURE_DISABLE             _IO(GOODIX_MAGIC_NUMBER, 2)
#define GESTURE_FLAG_SET            _IO(GOODIX_MAGIC_NUMBER, 3)
#define GESTURE_FLAG_CLEAR          _IO(GOODIX_MAGIC_NUMBER, 4)
/*#define SET_ENABLED_GESTURE         (_IOW(GOODIX_MAGIC_NUMBER, 5, u8) & NEGLECT_SIZE_MASK)*/
#define GESTURE_DATA_OBTAIN         (_IOR(GOODIX_MAGIC_NUMBER, 6, u8) & NEGLECT_SIZE_MASK)
#define GESTURE_DATA_ERASE          _IO(GOODIX_MAGIC_NUMBER, 7)

/*#define HOTKNOT_LOAD_SUBSYSTEM      (_IOW(GOODIX_MAGIC_NUMBER, 6, u8) & NEGLECT_SIZE_MASK)*/
#define HOTKNOT_LOAD_HOTKNOT        _IO(GOODIX_MAGIC_NUMBER, 20)
#define HOTKNOT_LOAD_AUTHENTICATION _IO(GOODIX_MAGIC_NUMBER, 21)
#define HOTKNOT_RECOVERY_MAIN       _IO(GOODIX_MAGIC_NUMBER, 22)
/*#define HOTKNOT_BLOCK_RW            (_IOW(GOODIX_MAGIC_NUMBER, 6, u8) & NEGLECT_SIZE_MASK)*/
#define HOTKNOT_DEVICES_PAIRED      _IO(GOODIX_MAGIC_NUMBER, 23)
#define HOTKNOT_MASTER_SEND         _IO(GOODIX_MAGIC_NUMBER, 24)
#define HOTKNOT_SLAVE_RECEIVE       _IO(GOODIX_MAGIC_NUMBER, 25)
/*#define HOTKNOT_DEVICES_COMMUNICATION*/
#define HOTKNOT_MASTER_DEPARTED     _IO(GOODIX_MAGIC_NUMBER, 26)
#define HOTKNOT_SLAVE_DEPARTED      _IO(GOODIX_MAGIC_NUMBER, 27)
#define HOTKNOT_VENDOR_VERSION      (_IOR(GOODIX_MAGIC_NUMBER, 28, u8) & NEGLECT_SIZE_MASK)
#define HOTKNOT_WAKEUP_BLOCK        _IO(GOODIX_MAGIC_NUMBER, 29)

#define IO_IIC_READ                  (_IOR(GOODIX_MAGIC_NUMBER, 100, u8) & NEGLECT_SIZE_MASK)
#define IO_IIC_WRITE                 (_IOW(GOODIX_MAGIC_NUMBER, 101, u8) & NEGLECT_SIZE_MASK)
#define IO_RESET_GUITAR              _IO(GOODIX_MAGIC_NUMBER, 102)
#define IO_DISABLE_IRQ               _IO(GOODIX_MAGIC_NUMBER, 103)
#define IO_ENABLE_IRQ                _IO(GOODIX_MAGIC_NUMBER, 104)
#define IO_GET_VERISON               (_IOR(GOODIX_MAGIC_NUMBER, 110, u8) & NEGLECT_SIZE_MASK)
#define IO_PRINT                     (_IOW(GOODIX_MAGIC_NUMBER, 111, u8) & NEGLECT_SIZE_MASK)
#define IO_VERSION                   "V1.3-20150420"

#define CMD_HEAD_LENGTH             20
static s32 io_iic_read(u8 *data, void __user *arg)
{
	s32 err = ERROR;
	s32 data_length = 0;
	u16 addr = 0;

	err = copy_from_user(data, arg, CMD_HEAD_LENGTH);
	if (err) {
		GTP_ERROR("Can't access the memory.");
		return ERROR_MEM;
	}

	addr = data[0] << 8 | data[1];
	data_length = data[2] << 8 | data[3];

	err = gt1x_i2c_read(addr, &data[CMD_HEAD_LENGTH], data_length);
	if (!err) {
		err = copy_to_user(&((u8 __user *) arg)[CMD_HEAD_LENGTH], &data[CMD_HEAD_LENGTH], data_length);
		if (err) {
			GTP_ERROR("ERROR when copy to user.[addr: %04x], [read length:%d]", addr, data_length);
			return ERROR_MEM;
		}
		err = CMD_HEAD_LENGTH + data_length;
	}
	/*GTP_DEBUG("IIC_READ.addr:0x%4x, length:%d, ret:%d", addr, data_length, err);*/
	/*GTP_DEBUG_ARRAY((&data[CMD_HEAD_LENGTH]), data_length);*/

	return err;
}

static s32 io_iic_write(u8 *data)
{
	s32 err = ERROR;
	s32 data_length = 0;
	u16 addr = 0;

	addr = data[0] << 8 | data[1];
	data_length = data[2] << 8 | data[3];

	err = gt1x_i2c_write(addr, &data[CMD_HEAD_LENGTH], data_length);
	if (!err) {
		err = CMD_HEAD_LENGTH + data_length;
	}

	return err;
}

/*
 *@return, 0:operate successfully
 *        > 0: the length of memory size ioctl has accessed,
 *        error otherwise.
 */
static long gt1x_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	u32 value = 0;
	s32 ret = 0;
	u8 *data = NULL;
	int cnt = 30;

	/* Blocking when firmwaer updating */
	while (cnt-- && update_info.status) {
		ssleep(1);
	}
	/*GTP_DEBUG("IOCTL CMD:%x", cmd);*/
	/* GTP_DEBUG("command:%d, length:%d, rw:%s",
	   _IOC_NR(cmd),
	   _IOC_SIZE(cmd),
	   (_IOC_DIR(cmd) & _IOC_READ) ? "read" : (_IOC_DIR(cmd) & _IOC_WRITE) ? "write" : "-");
	   */
	if (_IOC_DIR(cmd)) {
		s32 err = -1;
		s32 data_length = _IOC_SIZE(cmd);
		data = kzalloc(data_length, GFP_KERNEL);
		memset(data, 0, data_length);

		if (_IOC_DIR(cmd) & _IOC_WRITE) {
			err = copy_from_user(data, (void __user *)arg, data_length);
			if (err) {
				GTP_ERROR("Can't access the memory.");
				kfree(data);
				return -1;
			}
		}
	} else {
		value = (u32) arg;
	}

	switch (cmd & NEGLECT_SIZE_MASK) {
	case IO_GET_VERISON:
		if ((u8 __user *) arg) {
			ret = copy_to_user(((u8 __user *) arg), IO_VERSION, sizeof(IO_VERSION));
			if (!ret) {
				ret = sizeof(IO_VERSION);
			}
			GTP_INFO("%s", IO_VERSION);
		}
		break;
	case IO_IIC_READ:
		ret = io_iic_read(data, (void __user *)arg);
		break;

	case IO_IIC_WRITE:
		ret = io_iic_write(data);
		break;

	case IO_RESET_GUITAR:
		gt1x_irq_disable();
		gt1x_reset_guitar();
		gt1x_irq_enable();
		break;

	case IO_DISABLE_IRQ:
		gt1x_irq_disable();
#if GTP_ESD_PROTECT
		gt1x_esd_switch(SWITCH_OFF);
#endif
		break;

	case IO_ENABLE_IRQ:
		gt1x_irq_enable();
#if GTP_ESD_PROTECT
		gt1x_esd_switch(SWITCH_ON);
#endif
		break;

		/*print a string to syc log messages between application and kernel.*/
	case IO_PRINT:
		if (data)
			GTP_INFO("%s", (char *)data);
		break;

#if GTP_GESTURE_WAKEUP
	case GESTURE_ENABLE:
		GTP_DEBUG("Gesture switch ON.");
		gesture_enabled = 1;
		break;

	case GESTURE_DISABLE:
		GTP_DEBUG("Gesture switch OFF.");
		gesture_enabled = 0;
		break;

	case GESTURE_FLAG_SET:
		SETBIT(gestures_flag, (u8) value);
		GTP_DEBUG("Gesture flag: 0x%02X enabled.", value);
		break;

	case GESTURE_FLAG_CLEAR:
		CLEARBIT(gestures_flag, (u8) value);
		GTP_DEBUG("Gesture flag: 0x%02X disabled.", value);
		break;

	case GESTURE_DATA_OBTAIN:
		GTP_DEBUG("Obtain gesture data.");
		mutex_lock(&gesture_data_mutex);
		ret = copy_to_user(((u8 __user *) arg), &gesture_data.data, 4 + gesture_data.data[1] * 4 + gesture_data.data[3]);
		if (ret) {
			GTP_ERROR("ERROR when copy gesture data to user.");
			ret = ERROR_MEM;
		} else {
			ret = 4 + gesture_data.data[1] * 4 + gesture_data.data[3];
		}
		mutex_unlock(&gesture_data_mutex);
		break;

	case GESTURE_DATA_ERASE:
		GTP_DEBUG("ERASE_GESTURE_DATA");
		gesture_clear_wakeup_data();
		break;
#endif /*GTP_GESTURE_WAKEUP*/

#if GTP_HOTKNOT
	case HOTKNOT_VENDOR_VERSION:
		ret =  copy_to_user(((u8 __user *) arg), HOTKNOT_VERSION, sizeof(HOTKNOT_VERSION));
		if (!ret) {
			ret = sizeof(HOTKNOT_VERSION);
		}
		break;
	case HOTKNOT_LOAD_HOTKNOT:
		ret = hotknot_load_hotknot_subsystem();
		break;

	case HOTKNOT_LOAD_AUTHENTICATION:
#if GTP_ESD_PROTECT
		gt1x_esd_switch(SWITCH_OFF);
#endif
		ret = hotknot_load_authentication_subsystem();
		break;

	case HOTKNOT_RECOVERY_MAIN:
		ret = hotknot_recovery_main_system();
		break;
#if HOTKNOT_BLOCK_RW
	case HOTKNOT_DEVICES_PAIRED:
		hotknot_paired_flag = 0;
		force_wake_flag = 0;
		block_enable = 1;
		ret = hotknot_block_rw(HN_DEVICE_PAIRED, (s32) value);
		break;

	case HOTKNOT_MASTER_SEND:
		ret = hotknot_block_rw(HN_MASTER_SEND, (s32) value);
		if (!ret)
			ret = got_hotknot_extra_state;
		break;

	case HOTKNOT_SLAVE_RECEIVE:
		ret = hotknot_block_rw(HN_SLAVE_RECEIVED, (s32) value);
		if (!ret)
			ret = got_hotknot_extra_state;
		break;

	case HOTKNOT_MASTER_DEPARTED:
		ret = hotknot_block_rw(HN_MASTER_DEPARTED, (s32) value);
		break;

	case HOTKNOT_SLAVE_DEPARTED:
		ret = hotknot_block_rw(HN_SLAVE_DEPARTED, (s32) value);
		break;

	case HOTKNOT_WAKEUP_BLOCK:
		hotknot_wakeup_block();
		break;
#endif /*HOTKNOT_BLOCK_RW*/
#endif /*GTP_HOTKNOT*/

	default:
		GTP_INFO("Unknown cmd.");
		ret = -1;
		break;
	}

	if (data != NULL) {
		kfree(data);
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long gt1x_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *arg32 = compat_ptr(arg);

	if (!file->f_op || !file->f_op->unlocked_ioctl)
		return -ENOTTY;

	return file->f_op->unlocked_ioctl(file, cmd, (unsigned long)arg32);
}
#endif

static const struct file_operations gt1x_fops = {
	.owner = THIS_MODULE,
#if GTP_GESTURE_WAKEUP
	.read = gt1x_gesture_data_read,
	.write = gt1x_gesture_data_write,
#endif
	.unlocked_ioctl = gt1x_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = gt1x_compat_ioctl,
#endif
};

#if GTP_HOTKNOT
static const struct file_operations hotknot_fops = {
	.open = hotknot_open,
	.release = hotknot_release,
	.unlocked_ioctl = gt1x_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = gt1x_compat_ioctl,
#endif
};

static struct miscdevice hotknot_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = HOTKNOT_NODE,
	.fops = &hotknot_fops,
};
#endif

s32 gt1x_init_node(void)
{
#if GTP_GESTURE_WAKEUP
	struct proc_dir_entry *proc_entry = NULL;
	mutex_init(&gesture_data_mutex);
	memset(gestures_flag, 0, sizeof(gestures_flag));
	memset((u8 *) &gesture_data, 0, sizeof(st_gesture_data));

	proc_entry = proc_create(GESTURE_NODE, 0755, NULL, &gt1x_fops);
	if (proc_entry == NULL) {
		GTP_ERROR("CAN't create proc entry /proc/%s.", GESTURE_NODE);
		return -1;
	} else {
		GTP_INFO("Created proc entry /proc/%s.", GESTURE_NODE);
	}
#endif

#if GTP_HOTKNOT
	if (misc_register(&hotknot_misc_device)) {
		GTP_ERROR("CAN't create misc device in /dev/hotknot.");
		return -1;
	} else {
		GTP_INFO("Created misc device in /dev/hotknot.");
	}
#endif
	return 0;
}

void gt1x_deinit_node(void)
{
#if GTP_GESTURE_WAKEUP
	remove_proc_entry(GESTURE_NODE, NULL);
#endif

#if GTP_HOTKNOT
	misc_deregister(&hotknot_misc_device);
#endif
}
