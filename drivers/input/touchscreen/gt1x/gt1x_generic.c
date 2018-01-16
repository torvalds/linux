/* drivers/input/touchscreen/gt1x_generic.c
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

/*#include "gt1x_tpd_custom.h"*/
#include "gt1x.h"
#include "gt1x_generic.h"
#if GTP_PROXIMITY && defined(PLATFORM_MTK)
#include <linux/hwmsensor.h>
#include <linux/hwmsen_dev.h>
#include <linux/sensors_io.h>
#endif
#if GTP_ICS_SLOT_REPORT
#include <linux/input/mt.h>
#endif

/*******************GLOBAL VARIABLE*********************/
struct i2c_client *gt1x_i2c_client;
static struct workqueue_struct *gt1x_workqueue;

u8 gt1x_config[GTP_CONFIG_MAX_LENGTH] = { 0 };
u32 gt1x_cfg_length = GTP_CONFIG_MAX_LENGTH;

CHIP_TYPE_T gt1x_chip_type = CHIP_TYPE_NONE;
struct gt1x_version_info gt1x_version = {
	.product_id = {0},
	.patch_id = 0,
	.mask_id = 0,
	.sensor_id = 0,
	.match_opt = 0
};

#ifndef TPD_HAVE_BUTTON
#define TPD_HAVE_BUTTON  0
#endif

#if GTP_HAVE_TOUCH_KEY
const u16 gt1x_touch_key_array[] = GTP_KEY_TAB;
#elif TPD_HAVE_BUTTON
struct key_map_t {
	int x;
	int y;
};
static struct key_map_t tpd_virtual_key_array[] = TPD_KEY_MAP_ARRAY;
#endif

#if GTP_WITH_STYLUS && GTP_HAVE_STYLUS_KEY
static const u16 gt1x_stylus_key_array[] = GTP_STYLUS_KEY_TAB;
#endif

#define GOODIX_SYSFS_DIR      "goodix"
static struct kobject *sysfs_rootdir;

volatile int gt1x_rawdiff_mode;
u8 gt1x_wakeup_level;
u8 gt1x_init_failed;
u8 gt1x_int_type;
u32 gt1x_abs_x_max;
u32 gt1x_abs_y_max;
int gt1x_halt;

#if GTP_DEBUG_NODE
static ssize_t gt1x_debug_read_proc(struct file *, char __user *, size_t, loff_t *);
static ssize_t gt1x_debug_write_proc(struct file *, const char __user *, size_t, loff_t *);

static struct proc_dir_entry *gt1x_debug_proc_entry;
static const struct file_operations gt1x_debug_fops = {
	.owner = THIS_MODULE,
	.read = gt1x_debug_read_proc,
	.write = gt1x_debug_write_proc,
};

static s32 gt1x_init_debug_node(void)
{
	gt1x_debug_proc_entry = proc_create(GT1X_DEBUG_PROC_FILE, 0660, NULL, &gt1x_debug_fops);
	if (gt1x_debug_proc_entry == NULL) {
		GTP_ERROR("Create proc entry /proc/%s FAILED!", GT1X_DEBUG_PROC_FILE);
		return -1;
	}
	GTP_INFO("Created proc entry /proc/%s.", GT1X_DEBUG_PROC_FILE);
	return 0;
}

static void gt1x_deinit_debug_node(void)
{
	if (gt1x_debug_proc_entry != NULL) {
		remove_proc_entry(GT1X_DEBUG_PROC_FILE, NULL);
	}
}

static ssize_t gt1x_debug_read_proc(struct file *file, char __user *page, size_t size, loff_t *ppos)
{
	char *ptr = page;
	char temp_data[GTP_CONFIG_MAX_LENGTH] = { 0 };
	int i;

	if (*ppos) {
		return 0;
	}

	ptr += sprintf(ptr, "==== GT1X default config setting in driver====\n");

	for (i = 0; i < GTP_CONFIG_MAX_LENGTH; i++) {
		ptr += sprintf(ptr, "0x%02X,", gt1x_config[i]);
		if (i % 10 == 9)
			ptr += sprintf(ptr, "\n");
	}

	ptr += sprintf(ptr, "\n");

	ptr += sprintf(ptr, "==== GT1X config read from chip====\n");
	i = gt1x_i2c_read(GTP_REG_CONFIG_DATA, temp_data, GTP_CONFIG_MAX_LENGTH);
	GTP_INFO("I2C TRANSFER: %d", i);
	for (i = 0; i < GTP_CONFIG_MAX_LENGTH; i++) {
		ptr += sprintf(ptr, "0x%02X,", temp_data[i]);

		if (i % 10 == 9)
			ptr += sprintf(ptr, "\n");
	}

	ptr += sprintf(ptr, "\n");
	/* Touch PID & VID */
	ptr += sprintf(ptr, "==== GT1X Version Info ====\n");

	gt1x_i2c_read(GTP_REG_VERSION, temp_data, 12);
	ptr += sprintf(ptr, "ProductID: GT%c%c%c%c\n", temp_data[0], temp_data[1], temp_data[2], temp_data[3]);
	ptr += sprintf(ptr, "PatchID: %02X%02X\n", temp_data[4], temp_data[5]);
	ptr += sprintf(ptr, "MaskID: %02X%02X\n", temp_data[7], temp_data[8]);
	ptr += sprintf(ptr, "SensorID: %02X\n", temp_data[10] & 0x0F);

	*ppos += ptr - page;
	return (ptr - page);
}

static ssize_t gt1x_debug_write_proc(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	s32 ret = 0;
	u8 buf[GTP_CONFIG_MAX_LENGTH] = { 0 };
	char mode_str[50] = { 0 };
	int mode;
	int cfg_len;
	char arg1[50] = { 0 };
	u8 temp_config[GTP_CONFIG_MAX_LENGTH] = { 0 };

	GTP_DEBUG("write count %ld\n", (unsigned long)count);

	if (count > GTP_CONFIG_MAX_LENGTH) {
		GTP_ERROR("Too much data, buffer size: %d, data:%ld", GTP_CONFIG_MAX_LENGTH, (unsigned long)count);
		return -EFAULT;
	}

	if (copy_from_user(buf, buffer, count)) {
		GTP_ERROR("copy from user fail!");
		return -EFAULT;
	}
	/*send config*/
	if (count == gt1x_cfg_length) {
		memcpy(gt1x_config, buf, count);
		ret = gt1x_send_cfg(gt1x_config, gt1x_cfg_length);
		if (ret < 0) {
			GTP_ERROR("send gt1x_config failed.");
			return -EFAULT;
		}
		gt1x_abs_x_max = (gt1x_config[RESOLUTION_LOC + 1] << 8) + gt1x_config[RESOLUTION_LOC];
		gt1x_abs_y_max = (gt1x_config[RESOLUTION_LOC + 3] << 8) + gt1x_config[RESOLUTION_LOC + 2];

		return count;
	}

	sscanf(buf, "%s %d", (char *)&mode_str, &mode);

	/*force clear gt1x_config*/
	if (strcmp(mode_str, "clear_config") == 0) {
		GTP_INFO("Force clear gt1x_config");
		gt1x_send_cmd(GTP_CMD_CLEAR_CFG, 0);
		return count;
	}
	if (strcmp(mode_str, "init") == 0) {
		GTP_INFO("Init panel");
		gt1x_init_panel();
		return count;
	}
	if (strcmp(mode_str, "chip") == 0) {
		GTP_INFO("Get chip type:");
		gt1x_get_chip_type();
		return count;
	}
	if (strcmp(mode_str, "int") == 0) {
		if (mode == 0) {
			GTP_INFO("Disable irq.");
			gt1x_irq_disable();
		} else {
			GTP_INFO("Enable irq.");
			gt1x_irq_enable();
		}
		return count;
	}

	if (strcmp(mode_str, "poweron") == 0) {
		gt1x_power_switch(1);
		return count;
	}

	if (strcmp(mode_str, "poweroff") == 0) {
		gt1x_power_switch(0);
		return count;
	}

	if (strcmp(mode_str, "version") == 0) {
		gt1x_read_version(NULL);
		return count;
	}

	if (strcmp(mode_str, "reset") == 0) {
		gt1x_irq_disable();
		gt1x_reset_guitar();
		gt1x_irq_enable();
		return count;
	}
#if GTP_CHARGER_SWITCH
	if (strcmp(mode_str, "charger") == 0) {
		gt1x_charger_config(mode);
		return count;
	}
#endif
	sscanf(buf, "%s %s", (char *)&mode_str, (char *)&arg1);
	if (strcmp(mode_str, "update") == 0) {
		gt1x_update_firmware(arg1);
		return count;
	}

	if (strcmp(mode_str, "sendconfig") == 0) {
		cfg_len = gt1x_parse_config(arg1, temp_config);
		if (cfg_len < 0) {
			return -1;
		}
		gt1x_send_cfg(temp_config, gt1x_cfg_length);
		return count;
	}

	if (strcmp(mode_str, "debug_gesture") == 0) {
#if GTP_GESTURE_WAKEUP
		gt1x_gesture_debug(!!mode);
#endif
	}

	if (strcmp(mode_str, "force_update") == 0) {
		update_info.force_update = !!mode;
	}
	return gt1x_debug_proc(buf, count);
}
#endif

static u8 ascii2hex(u8 a)
{
	s8 value = 0;
	if (a >= '0' && a <= '9') {
		value = a - '0';
	} else if (a >= 'A' && a <= 'F') {
		value = a - 'A' + 0x0A;
	} else if (a >= 'a' && a <= 'f') {
		value = a - 'a' + 0x0A;
	} else {
		value = 0xff;
	}
	return value;
}

int gt1x_parse_config(char *filename, u8 *config)
{
	mm_segment_t old_fs;
	struct file *fp = NULL;
	u8 *buf;
	int i;
	int len;
	int cur_len = -1;
	u8 high, low;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(filename, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		GTP_ERROR("Open config file error!(file: %s)", filename);
		goto parse_cfg_fail1;
	}
	len = fp->f_op->llseek(fp, 0, SEEK_END);
	if (len > GTP_CONFIG_MAX_LENGTH * 6 || len < GTP_CONFIG_MAX_LENGTH) {
		GTP_ERROR("Config is invalid!(length: %d)", len);
		goto parse_cfg_fail2;
	}
	buf = kzalloc(len, GFP_KERNEL);
	if (buf == NULL) {
		GTP_ERROR("Allocate memory failed!(size: %d)", len);
		goto parse_cfg_fail2;
	}
	fp->f_op->llseek(fp, 0, SEEK_SET);
	if (fp->f_op->read(fp, (char *)buf, len, &fp->f_pos) != len) {
		GTP_ERROR("Read %d bytes from file failed!", len);
	}

	GTP_INFO("Parse config file: %s (%d bytes)", filename, len);

	for (i = 0, cur_len = 0; i < len && cur_len < GTP_CONFIG_MAX_LENGTH;) {
		if (buf[i] == ' ' || buf[i] == '\r' || buf[i] == '\n' || buf[i] == ',') {
			i++;
			continue;
		}
		if (buf[i] == '0' && (buf[i + 1] == 'x' || buf[i + 1] == 'X')) {

			high = ascii2hex(buf[i + 2]);
			low = ascii2hex(buf[i + 3]);

			if (high != 0xFF && low != 0xFF) {
				config[cur_len++] = (high << 4) + low;
				i += 4;
				continue;
			}
		}
		GTP_ERROR("Illegal config file!");
		cur_len = -1;
		break;
	}

	if (cur_len < GTP_CONFIG_MIN_LENGTH || config[cur_len - 1] != 0x01) {
		cur_len = -1;
	} else {
		for (i = 0; i < cur_len; i++) {
			if (i % 10 == 0) {
				printk("\n<<GTP-DBG>>:");
			}
			printk("0x%02x,", config[i]);
		}
		printk("\n");
	}

	kfree(buf);
parse_cfg_fail2:
	filp_close(fp, NULL);
parse_cfg_fail1:
	set_fs(old_fs);

	return cur_len;
}

s32 _do_i2c_read(struct i2c_msg *msgs, u16 addr, u8 *buffer, s32 len)
{
	s32 ret = -1;
	s32 pos = 0;
	s32 data_length = len;
	s32 transfer_length = 0;
	u8 *data = NULL;
	u16 address = addr;

	data = kmalloc(IIC_MAX_TRANSFER_SIZE < (len + GTP_ADDR_LENGTH) ? IIC_MAX_TRANSFER_SIZE : (len + GTP_ADDR_LENGTH), GFP_KERNEL);
	if (data == NULL) {
		return ERROR_MEM;
	}
	msgs[1].buf = data;

	while (pos != data_length) {
		if ((data_length - pos) > IIC_MAX_TRANSFER_SIZE) {
			transfer_length = IIC_MAX_TRANSFER_SIZE;
		} else {
			transfer_length = data_length - pos;
		}
		msgs[0].buf[0] = (address >> 8) & 0xFF;
		msgs[0].buf[1] = address & 0xFF;
		msgs[1].len = transfer_length;

		ret = i2c_transfer(gt1x_i2c_client->adapter, msgs, 2);
		if (ret != 2) {
			GTP_ERROR("I2c Transfer error! (%d)", ret);
			kfree(data);
			return ERROR_IIC;
		}
		memcpy(&buffer[pos], msgs[1].buf, transfer_length);
		pos += transfer_length;
		address += transfer_length;
	}

	kfree(data);
	return 0;
}

s32 _do_i2c_write(struct i2c_msg *msg, u16 addr, u8 *buffer, s32 len)
{
	s32 ret = -1;
	s32 pos = 0;
	s32 data_length = len;
	s32 transfer_length = 0;
	u8 *data = NULL;
	u16 address = addr;

	data = kmalloc(IIC_MAX_TRANSFER_SIZE < (len + GTP_ADDR_LENGTH) ? IIC_MAX_TRANSFER_SIZE : (len + GTP_ADDR_LENGTH), GFP_KERNEL);
	if (data == NULL) {
		return ERROR_MEM;
	}
	msg->buf = data;

	while (pos != data_length) {
		if ((data_length - pos) > (IIC_MAX_TRANSFER_SIZE - GTP_ADDR_LENGTH)) {
			transfer_length = IIC_MAX_TRANSFER_SIZE - GTP_ADDR_LENGTH;
		} else {
			transfer_length = data_length - pos;
		}

		msg->buf[0] = (address >> 8) & 0xFF;
		msg->buf[1] = address & 0xFF;
		msg->len = transfer_length + GTP_ADDR_LENGTH;
		memcpy(&msg->buf[GTP_ADDR_LENGTH], &buffer[pos], transfer_length);

		ret = i2c_transfer(gt1x_i2c_client->adapter, msg, 1);
		if (ret != 1) {
			GTP_ERROR("I2c transfer error! (%d)", ret);
			kfree(data);
			return ERROR_IIC;
		}
		pos += transfer_length;
		address += transfer_length;
	}

	kfree(data);
	return 0;
}

#if !GTP_ESD_PROTECT
static s32 gt1x_i2c_test(void)
{
	u8 retry = 0;
	s32 ret = -1;
	u32 hw_info = 0;
	GTP_DEBUG_FUNC();

	while (retry++ < 3) {
		ret = gt1x_i2c_read(GTP_REG_HW_INFO, (u8 *) &hw_info, sizeof(hw_info));
		if (!ret) {
			GTP_INFO("Hardware Info:%08X", hw_info);
			return ret;
		}

		msleep(10);
		GTP_ERROR("Hardware Info:%08X", hw_info);
		GTP_ERROR("I2c failed%d.", retry);
	}

	return ERROR_RETRY;
}
#endif

/**
 * gt1x_i2c_read_dbl_check - read twice and double check
 * @addr: register address
 * @buffer: data buffer
 * @len: bytes to read
 * Return    <0: i2c error, 0: ok, 1:fail
 */
s32 gt1x_i2c_read_dbl_check(u16 addr, u8 *buffer, s32 len)
{
	u8 buf[16] = { 0 };
	u8 confirm_buf[16] = { 0 };
	int ret;

	if (len > 16) {
		GTP_ERROR("i2c_read_dbl_check length %d is too long, exceed %zu", len, sizeof(buf));
		return ERROR;
	}

	memset(buf, 0xAA, sizeof(buf));
	ret = gt1x_i2c_read(addr, buf, len);
	if (ret < 0) {
		return ret;
	}

	msleep(5);
	memset(confirm_buf, 0, sizeof(confirm_buf));
	ret = gt1x_i2c_read(addr, confirm_buf, len);
	if (ret < 0) {
		return ret;
	}

	if (!memcmp(buf, confirm_buf, len)) {
		memcpy(buffer, confirm_buf, len);
		return 0;
	}
	GTP_ERROR("i2c read 0x%04X, %d bytes, double check failed!", addr, len);
	return 1;
}

/**
 * gt1x_get_info - Get information from ic, such as resolution and
 * int trigger type
 * Return    <0: i2c failed, 0: i2c ok
 */
s32 gt1x_get_info(void)
{
	u8 opr_buf[4] = { 0 };
	s32 ret = 0;

	ret = gt1x_i2c_read(GTP_REG_CONFIG_DATA + 1, opr_buf, 4);
	if (ret < 0) {
		return ret;
	}

	gt1x_abs_x_max = (opr_buf[1] << 8) + opr_buf[0];
	gt1x_abs_y_max = (opr_buf[3] << 8) + opr_buf[2];

	ret = gt1x_i2c_read(GTP_REG_CONFIG_DATA + 6, opr_buf, 1);
	if (ret < 0) {
		return ret;
	}
	gt1x_int_type = opr_buf[0] & 0x03;

	GTP_INFO("X_MAX = %d, Y_MAX = %d, TRIGGER = 0x%02x", gt1x_abs_x_max, gt1x_abs_y_max, gt1x_int_type);

	return 0;
}

/**
 * gt1x_send_cfg - Send gt1x_config Function.
 * @config: pointer of the configuration array.
 * @cfg_len: length of configuration array.
 * Return 0--success,non-0--fail.
 */
s32 gt1x_send_cfg(u8 *config, int cfg_len)
{
#if GTP_DRIVER_SEND_CFG
	static DEFINE_MUTEX(mutex_cfg);
	int i;
	s32 ret = 0;
	s32 retry = 0;
	u16 checksum = 0;

	if (update_info.status) {
		GTP_DEBUG("Ignore cfg during fw update.");
		return -1;
	}
	mutex_lock(&mutex_cfg);
	GTP_DEBUG("Driver send config, length:%d", cfg_len);
	for (i = 0; i < cfg_len - 3; i += 2) {
		checksum += (config[i] << 8) + config[i + 1];
	}
	if (!checksum) {
		GTP_ERROR("Invalid config, all of the bytes is zero!");
		mutex_unlock(&mutex_cfg);
		return -1;
	}
	checksum = 0 - checksum;
	GTP_DEBUG("Config checksum: 0x%04X", checksum);
	config[cfg_len - 3] = (checksum >> 8) & 0xFF;
	config[cfg_len - 2] = checksum & 0xFF;
	config[cfg_len - 1] = 0x01;

	while (retry++ < 5) {
		ret = gt1x_i2c_write(GTP_REG_CONFIG_DATA, config, cfg_len);
		if (!ret) {
			msleep(200);	/* at least 200ms, wait for storing config into flash. */
			mutex_unlock(&mutex_cfg);
			GTP_DEBUG("Send config successfully!");
			return 0;
		}
	}
	GTP_ERROR("Send config failed!");
	mutex_unlock(&mutex_cfg);
	return ret;
#endif
	return 0;
}

/**
 * gt1x_init_panel - Prepare config data for touch ic, don't call this function
 * after initialization.
 *
 * Return 0--success,<0 --fail.
 */
s32 gt1x_init_panel(void)
{
	s32 ret = 0;
	u8 cfg_len = 0;

#if GTP_DRIVER_SEND_CFG
	u8 sensor_id = 0;

	const u8 cfg_grp0[] = GTP_CFG_GROUP0;
	const u8 cfg_grp1[] = GTP_CFG_GROUP1;
	const u8 cfg_grp2[] = GTP_CFG_GROUP2;
	const u8 cfg_grp3[] = GTP_CFG_GROUP3;
	const u8 cfg_grp4[] = GTP_CFG_GROUP4;
	const u8 cfg_grp5[] = GTP_CFG_GROUP5;
	const u8 *cfgs[] = {
		cfg_grp0, cfg_grp1, cfg_grp2,
		cfg_grp3, cfg_grp4, cfg_grp5
	};
	u8 cfg_lens[] = {
		CFG_GROUP_LEN(cfg_grp0),
		CFG_GROUP_LEN(cfg_grp1),
		CFG_GROUP_LEN(cfg_grp2),
		CFG_GROUP_LEN(cfg_grp3),
		CFG_GROUP_LEN(cfg_grp4),
		CFG_GROUP_LEN(cfg_grp5)
	};

	GTP_DEBUG("Config groups length:%d,%d,%d,%d,%d,%d", cfg_lens[0], cfg_lens[1], cfg_lens[2], cfg_lens[3], cfg_lens[4], cfg_lens[5]);

	sensor_id = gt1x_version.sensor_id;
	if (sensor_id >= 6 || cfg_lens[sensor_id] < GTP_CONFIG_MIN_LENGTH || cfg_lens[sensor_id] > GTP_CONFIG_MAX_LENGTH) {
		sensor_id = 0;
		gt1x_version.sensor_id = 0;
	}

	cfg_len = cfg_lens[sensor_id];

	GTP_INFO("Config group%d used, length:%d", sensor_id, cfg_len);

	if (cfg_len < GTP_CONFIG_MIN_LENGTH || cfg_len > GTP_CONFIG_MAX_LENGTH) {
		GTP_ERROR("Config group%d is INVALID! You need to check you header file CFG_GROUP section!", sensor_id + 1);
		return -1;
	}

	memset(gt1x_config, 0, sizeof(gt1x_config));
	memcpy(gt1x_config, cfgs[sensor_id], cfg_len);

	/* clear the flag, avoid failure when send the_config of driver. */
	gt1x_config[0] &= 0x7F;

#if GTP_CUSTOM_CFG
	gt1x_config[RESOLUTION_LOC] = (u8) GTP_MAX_WIDTH;
	gt1x_config[RESOLUTION_LOC + 1] = (u8) (GTP_MAX_WIDTH >> 8);
	gt1x_config[RESOLUTION_LOC + 2] = (u8) GTP_MAX_HEIGHT;
	gt1x_config[RESOLUTION_LOC + 3] = (u8) (GTP_MAX_HEIGHT >> 8);

	if (GTP_INT_TRIGGER == 0) {	/* RISING  */
		gt1x_config[TRIGGER_LOC] &= 0xfe;
	} else if (GTP_INT_TRIGGER == 1) {	/* FALLING */
		gt1x_config[TRIGGER_LOC] |= 0x01;
	}
	set_reg_bit(gt1x_config[MODULE_SWITCH3_LOC], 5, !gt1x_wakeup_level);
#endif /* END GTP_CUSTOM_CFG */

#else /* DRIVER NOT SEND CONFIG */
	cfg_len = GTP_CONFIG_MAX_LENGTH;
	ret = gt1x_i2c_read(GTP_REG_CONFIG_DATA, gt1x_config, cfg_len);
	if (ret < 0) {
		return ret;
	}
#endif /* END GTP_DRIVER_SEND_CFG */

	GTP_DEBUG_FUNC();
	/* match resolution when gt1x_abs_x_max & gt1x_abs_y_max have been set already */
	if ((gt1x_abs_x_max == 0) && (gt1x_abs_y_max == 0)) {
		gt1x_abs_x_max = (gt1x_config[RESOLUTION_LOC + 1] << 8) + gt1x_config[RESOLUTION_LOC];
		gt1x_abs_y_max = (gt1x_config[RESOLUTION_LOC + 3] << 8) + gt1x_config[RESOLUTION_LOC + 2];
		gt1x_int_type = (gt1x_config[TRIGGER_LOC]) & 0x03;
		gt1x_wakeup_level = !(gt1x_config[MODULE_SWITCH3_LOC] & 0x20);
	} else {
		gt1x_config[RESOLUTION_LOC] = (u8) gt1x_abs_x_max;
		gt1x_config[RESOLUTION_LOC + 1] = (u8) (gt1x_abs_x_max >> 8);
		gt1x_config[RESOLUTION_LOC + 2] = (u8) gt1x_abs_y_max;
		gt1x_config[RESOLUTION_LOC + 3] = (u8) (gt1x_abs_y_max >> 8);
		set_reg_bit(gt1x_config[MODULE_SWITCH3_LOC], 5, !gt1x_wakeup_level);
		gt1x_config[TRIGGER_LOC] = (gt1x_config[TRIGGER_LOC] & 0xFC) | gt1x_int_type;
	}

	GTP_INFO("X_MAX=%d,Y_MAX=%d,TRIGGER=0x%02x,WAKEUP_LEVEL=%d", gt1x_abs_x_max, gt1x_abs_y_max, gt1x_int_type, gt1x_wakeup_level);

	gt1x_cfg_length = cfg_len;
	ret = gt1x_send_cfg(gt1x_config, gt1x_cfg_length);
	return ret;
}

void gt1x_select_addr(void)
{
	GTP_GPIO_OUTPUT(GTP_RST_PORT, 0);
	GTP_GPIO_OUTPUT(GTP_INT_PORT, gt1x_i2c_client->addr == 0x14);
	msleep(2);
	GTP_GPIO_OUTPUT(GTP_RST_PORT, 1);
	msleep(2);
}

static s32 gt1x_set_reset_status(void)
{
	/* 0x8040 ~ 0x8043 */
	u8 value[] = {0xAA, 0x00, 0x56, 0xAA};
	int ret;

	GTP_DEBUG("Set reset status.");
	ret = gt1x_i2c_write(GTP_REG_CMD + 1, &value[1], 3);
	if (ret < 0)
		return ret;

	return gt1x_i2c_write(GTP_REG_CMD, value, 1);
}

#if GTP_INCELL_PANEL
int gt1x_write_and_readback(u16 addr, u8 *buffer, s32 len)
{
	int ret;
	u8 d[len];

	ret = gt1x_i2c_write(addr, buffer, len);
	if (ret < 0)
		return -1;

	ret = gt1x_i2c_read(addr, d, len);
	if (ret < 0 || memcmp(buffer, d, len))
		return -1;

	return 0;
}

int gt1x_incell_reset(void)
{
#define RST_RETRY       5
	int ret, retry = RST_RETRY;
	u8 d[2];

	do {
		/* select i2c address */
		gt1x_select_addr();

		/* test i2c */
		ret = gt1x_i2c_read(0x4220, d, 1);

	} while (--retry && ret < 0);

	if (ret < 0) {
		return -1;
	}

	/* Stop cpu of the touch ic */
	retry = RST_RETRY;
	do {
		d[0] = 0x0C;
		ret = gt1x_write_and_readback(0x4180, d, 1);

	} while (--retry && ret < 0);

	if (ret < 0) {
		GTP_ERROR("Hold error.");
		return -1;
	}

	/* skip sensor id check. [start] */
	retry = RST_RETRY;
	do {
		d[0] = 0x00;
		ret = gt1x_write_and_readback(0x4305, d, 1);
		if (ret < 0)
			continue;

		d[0] = 0x2B;
		d[1] = 0x24;
		ret = gt1x_write_and_readback(0x42c4, d, 2);
		if (ret < 0)
			continue;

		d[0] = 0xE1;
		d[1] = 0xD3;
		ret = gt1x_write_and_readback(0x42e4, d, 2);
		if (ret < 0)
			continue;
		d[0] = 0x01;
		ret = gt1x_write_and_readback(0x4305, d, 1);
		if (ret < 0)
			continue;
		else
			break;
	} while (--retry);

	if (!retry)
		return -1;
	/* skip sensor id check. [end] */

	/* release hold of cpu */
	retry = RST_RETRY;
	do {
		d[0] = 0x00;
		ret = gt1x_write_and_readback(0x4180, d, 1);

	} while (--retry && ret < 0);

	if (ret < 0)
		return -1;

	return 0;
}
#endif

s32 gt1x_reset_guitar(void)
{
	int ret;

	GTP_INFO("GTP RESET!");

#if GTP_INCELL_PANEL
	ret = gt1x_incell_reset();
	if (ret < 0)
		return ret;
#else
	gt1x_select_addr();
	msleep(8);     /* must >= 6ms */
#endif

	/* int synchronization */
	GTP_GPIO_OUTPUT(GTP_INT_PORT, 0);
	msleep(50);
	GTP_GPIO_AS_INT(GTP_INT_PORT);

	/* this operation is necessary even when the esd check
	   fucntion dose not turn on */
	ret = gt1x_set_reset_status();
	return ret;
}

/**
 * gt1x_read_version - Read gt1x version info.
 * @ver_info: address to store version info
 * Return 0-succeed.
 */
s32 gt1x_read_version(struct gt1x_version_info *ver_info)
{
	s32 ret = -1;
	u8 buf[12] = { 0 };
	u32 mask_id = 0;
	u32 patch_id = 0;
	u8 product_id[5] = { 0 };
	u8 sensor_id = 0;
	u8 match_opt = 0;
	int i, retry = 3;
	u8 checksum = 0;

	GTP_DEBUG_FUNC();

	while (retry--) {
		ret = gt1x_i2c_read_dbl_check(GTP_REG_VERSION, buf, sizeof(buf));
		if (!ret) {
			checksum = 0;

			for (i = 0; i < sizeof(buf); i++) {
				checksum += buf[i];
			}

			if (checksum == 0 &&	/* first 3 bytes must be number or char */
			    IS_NUM_OR_CHAR(buf[0]) && IS_NUM_OR_CHAR(buf[1]) && IS_NUM_OR_CHAR(buf[2]) && buf[10] != 0xFF) {	/*sensor id == 0xFF, retry */
				break;
			} else {
				GTP_ERROR("Read version failed!(checksum error)");
			}
		} else {
			GTP_ERROR("Read version failed!");
		}
		GTP_DEBUG("Read version : %d", retry);
		msleep(100);
	}

	if (retry <= 0) {
		if (ver_info)
			ver_info->sensor_id = 0;
		return -1;
	}

	mask_id = (u32) ((buf[7] << 16) | (buf[8] << 8) | buf[9]);
	patch_id = (u32) ((buf[4] << 16) | (buf[5] << 8) | buf[6]);
	memcpy(product_id, buf, 4);
	sensor_id = buf[10] & 0x0F;
	match_opt = (buf[10] >> 4) & 0x0F;

	GTP_INFO("IC VERSION:GT%s_%06X(Patch)_%04X(Mask)_%02X(SensorID)", product_id, patch_id, mask_id >> 8, sensor_id);

	if (ver_info != NULL) {
		ver_info->mask_id = mask_id;
		ver_info->patch_id = patch_id;
		memcpy(ver_info->product_id, product_id, 5);
		ver_info->sensor_id = sensor_id;
		ver_info->match_opt = match_opt;
	}
	return 0;
}

/**
 * gt1x_get_chip_type - get chip type .
 *
 * different chip synchronize in different way,
 */
s32 gt1x_get_chip_type(void)
{
	u8 opr_buf[4] = { 0x00 };
	u8 gt1x_data[] = { 0x02, 0x08, 0x90, 0x00 };
	u8 gt9l_data[] = { 0x03, 0x10, 0x90, 0x00 };
	s32 ret = -1;

	/* chip type already exist */
	if (gt1x_chip_type != CHIP_TYPE_NONE) {
		return 0;
	}

	/* read hardware */
	ret = gt1x_i2c_read_dbl_check(GTP_REG_HW_INFO, opr_buf, sizeof(opr_buf));
	if (ret) {
		GTP_ERROR("I2c communication error.");
		return -1;
	}

	/* find chip type */
	if (!memcmp(opr_buf, gt1x_data, sizeof(gt1x_data))) {
		gt1x_chip_type = CHIP_TYPE_GT1X;
	} else if (!memcmp(opr_buf, gt9l_data, sizeof(gt9l_data))) {
		gt1x_chip_type = CHIP_TYPE_GT2X;
	}

	if (gt1x_chip_type != CHIP_TYPE_NONE) {
		GTP_INFO("Chip Type: %s", (gt1x_chip_type == CHIP_TYPE_GT1X) ? "GT1X" : "GT2X");
		return 0;
	} else {
		return -1;
	}
}

/**
 * gt1x_enter_sleep - Eter sleep function.
 *
 * Returns  0--success,non-0--fail.
 */
static s32 gt1x_enter_sleep(void)
{
#if GTP_POWER_CTRL_SLEEP
	gt1x_power_switch(SWITCH_OFF);
	return 0;
#else
	{
		s32 retry = 0;
		if (gt1x_wakeup_level == 1) {	/* high level wakeup */
			GTP_GPIO_OUTPUT(GTP_INT_PORT, 0);
		}
		msleep(5);

		while (retry++ < 3) {
			if (!gt1x_send_cmd(GTP_CMD_SLEEP, 0)) {
				GTP_INFO("Enter sleep mode!");
				return 0;
			}
			msleep(10);
		}

		GTP_ERROR("Enter sleep mode failed.");
		return -1;
	}
#endif
}

/**
 * gt1x_wakeup_sleep - wakeup from sleep mode Function.
 *
 * Return: 0--success,non-0--fail.
 */
static s32 gt1x_wakeup_sleep(void)
{
#if !GTP_POWER_CTRL_SLEEP
	u8 retry = 0;
	s32 ret = -1;
	int flag = 0;
#endif

	GTP_DEBUG("Wake up begin.");
	gt1x_irq_disable();

#if GTP_POWER_CTRL_SLEEP	/* power manager unit control the procedure */
	gt1x_power_reset();
	GTP_INFO("Wakeup by poweron");
	return 0;
#else /* gesture wakeup & int port wakeup */
	while (retry++ < 2) {
#if GTP_GESTURE_WAKEUP
		if (gesture_enabled) {
			gesture_doze_status = DOZE_DISABLED;
			ret = gt1x_reset_guitar();
			if (!ret) {
				break;
			}
		} else
#endif
		{
			/* wake up through int port */
			GTP_GPIO_OUTPUT(GTP_INT_PORT, gt1x_wakeup_level);
			msleep(5);

			/* Synchronize int IO */
			GTP_GPIO_OUTPUT(GTP_INT_PORT, 0);
			msleep(50);
			GTP_GPIO_AS_INT(GTP_INT_PORT);
			flag = 1;

#if GTP_ESD_PROTECT
			ret = gt1x_set_reset_status();
#else
			ret = gt1x_i2c_test();
#endif
			if (!ret)
				break;
		} /* end int wakeup */
	}

	if (ret < 0 && flag) {		/* int  wakeup failed , try waking up by reset */
		while (retry--) {
			ret = gt1x_reset_guitar();
			if (!ret)
				break;
		}
	}

	if (ret) {
		GTP_ERROR("Wake up sleep failed.");
		return -1;
	} else {
		GTP_INFO("Wake up end.");
		return 0;
	}
#endif /* END GTP_POWER_CTRL_SLEEP */
}

/**
 * gt1x_send_cmd - seng cmd
 * must write data & checksum first
 * byte    content
 * 0       cmd
 * 1       data
 * 2       checksum
 * Returns 0 - succeed,non-0 - failed
 */
s32 gt1x_send_cmd(u8 cmd, u8 data)
{
	s32 ret;
	static DEFINE_MUTEX(cmd_mutex);
	u8 buffer[3] = { cmd, data, 0 };

	mutex_lock(&cmd_mutex);
	buffer[2] = (u8) ((0 - cmd - data) & 0xFF);
	ret = gt1x_i2c_write(GTP_REG_CMD + 1, &buffer[1], 2);
	ret |= gt1x_i2c_write(GTP_REG_CMD, &buffer[0], 1);
	msleep(50);
	mutex_unlock(&cmd_mutex);

	return ret;
}

void gt1x_power_reset(void)
{
	static int rst_flag;
	s32 i = 0;

	if (rst_flag || update_info.status) {
		return;
	}
	GTP_INFO("force_reset_guitar");
	rst_flag = 1;
	gt1x_irq_disable();
	gt1x_power_switch(SWITCH_OFF);
	msleep(30);
	gt1x_power_switch(SWITCH_ON);
	msleep(30);

	for (i = 0; i < 5; i++) {
		if (gt1x_reset_guitar()) {
			continue;
		}
		if (gt1x_send_cfg(gt1x_config, gt1x_cfg_length)) {
			msleep(500);
			continue;
		}
		break;
	}
	gt1x_irq_enable();
	rst_flag = 0;
}

s32 gt1x_request_event_handler(void)
{
	s32 ret = -1;
	u8 rqst_data = 0;

	ret = gt1x_i2c_read(GTP_REG_RQST, &rqst_data, 1);
	if (ret) {
		GTP_ERROR("I2C transfer error. errno:%d", ret);
		return -1;
	}
	GTP_DEBUG("Request state:0x%02x.", rqst_data);
	switch (rqst_data & 0x0F) {
	case GTP_RQST_CONFIG:
		GTP_INFO("Request Config.");
		ret = gt1x_send_cfg(gt1x_config, gt1x_cfg_length);
		if (ret) {
			GTP_ERROR("Send gt1x_config error.");
		} else {
			GTP_INFO("Send gt1x_config success.");
			rqst_data = GTP_RQST_RESPONDED;
			gt1x_i2c_write(GTP_REG_RQST, &rqst_data, 1);
		}
		break;
	case GTP_RQST_RESET:
		GTP_INFO("Request Reset.");
		gt1x_reset_guitar();
		rqst_data = GTP_RQST_RESPONDED;
		gt1x_i2c_write(GTP_REG_RQST, &rqst_data, 1);
		break;
	case GTP_RQST_BAK_REF:
		GTP_INFO("Request Ref.");
		break;
	case GTP_RQST_MAIN_CLOCK:
		GTP_INFO("Request main clock.");
		break;
#if GTP_HOTKNOT
	case GTP_RQST_HOTKNOT_CODE:
		GTP_INFO("Request HotKnot Code.");
		break;
#endif
	default:
		break;
	}
	return 0;
}

/**
 * gt1x_touch_event_handler - handle touch event
 * (pen event, key event, finger touch envent)
 * @data:
 * Return    <0: failed, 0: succeed
 */
s32 gt1x_touch_event_handler(u8 *data, struct input_dev *dev, struct input_dev *pen_dev)
{
	u8 touch_data[1 + 8 * GTP_MAX_TOUCH + 2] = { 0 };
	static u16 pre_event;
	static u16 pre_index;
	u8 touch_num = 0;
	u8 key_value = 0;
	u16 cur_event = 0;
	u8 *coor_data = NULL;
	u8 check_sum = 0;
	s32 input_x = 0;
	s32 input_y = 0;
	s32 input_w = 0;
	s32 id = 0;
	s32 i = 0;
	s32 ret = -1;

	GTP_DEBUG_FUNC();
	touch_num = data[0] & 0x0f;
	if (touch_num > GTP_MAX_TOUCH) {
		GTP_ERROR("Illegal finger number!");
		return ERROR_VALUE;
	}

	memcpy(touch_data, data, 11);

	/* read the remaining coor data
	 * 0x814E(touch status) + 8(every coordinate consist of 8 bytes data) * touch num +
	 * keycode + checksum
	 */
	if (touch_num > 1) {
		ret = gt1x_i2c_read((GTP_READ_COOR_ADDR + 11), &touch_data[11], 1 + 8 * touch_num + 2 - 11);
		if (ret) {
			return ret;
		}
	}

	/* cacl checksum */
	for (i = 0; i < 1 + 8 * touch_num + 2; i++) {
		check_sum += touch_data[i];
	}
	if (check_sum) { /* checksum error*/
		ret = gt1x_i2c_read(GTP_READ_COOR_ADDR, touch_data, 3 + 8 * touch_num);
		if (ret) {
			return ret;
		}

		for (i = 0, check_sum = 0; i < 3 + 8 * touch_num; i++) {
			check_sum += touch_data[i];
		}
		if (check_sum) {
			GTP_ERROR("Checksum error[%x]", check_sum);
			return ERROR_VALUE;
		}
	}
	/*
	 * cur_event , pre_event bit defination
	 * bits:     bit4	bit3		    bit2	 bit1	   bit0
	 * event:  hover  stylus_key  stylus    key    touch
	 */
	key_value = touch_data[1 + 8 * touch_num];
	/*  start check current event */
	if ((touch_data[0] & 0x10) && key_value) {
#if (GTP_HAVE_STYLUS_KEY || GTP_HAVE_TOUCH_KEY || TPD_HAVE_BUTTON)
		/* get current key states */
		if (key_value & 0xF0) {
			SET_BIT(cur_event, BIT_STYLUS_KEY);
		} else if (key_value & 0x0F) {
			SET_BIT(cur_event, BIT_TOUCH_KEY);
		}
#endif
	}
#if GTP_WITH_STYLUS
	else if (touch_data[1] & 0x80) {
		SET_BIT(cur_event, BIT_STYLUS);
	}
#endif
	else if (touch_num) {
		SET_BIT(cur_event, BIT_TOUCH);
	}

	/* start handle current event and pre-event */
#if GTP_HAVE_STYLUS_KEY
	if (CHK_BIT(cur_event, BIT_STYLUS_KEY) || CHK_BIT(pre_event, BIT_STYLUS_KEY)) {
		/*
		 * 0x10 -- stylus key0 down
		 * 0x20 -- stylus key1 down
		 * 0x40 -- stylus key0 & stylus key1 both down
		 */
		u8 temp = (key_value & 0x40) ? 0x30 : key_value;
		for (i = 4; i < 6; i++) {
			input_report_key(pen_dev, gt1x_stylus_key_array[i - 4], temp & (0x01 << i));
		}
		GTP_DEBUG("Stulus key event.");
	}
#endif

#if GTP_WITH_STYLUS
	if (CHK_BIT(cur_event, BIT_STYLUS)) {
		coor_data = &touch_data[1];
		id = coor_data[0] & 0x7F;
		input_x = coor_data[1] | (coor_data[2] << 8);
		input_y = coor_data[3] | (coor_data[4] << 8);
		input_w = coor_data[5] | (coor_data[6] << 8);

		input_x = GTP_WARP_X(gt1x_abs_x_max, input_x);
		input_y = GTP_WARP_Y(gt1x_abs_y_max, input_y);

		GTP_DEBUG("Pen touch DOWN.");
		gt1x_pen_down(input_x, input_y, input_w, 0);
	} else if (CHK_BIT(pre_event, BIT_STYLUS)) {
		GTP_DEBUG("Pen touch UP.");
		gt1x_pen_up(0);
	}
#endif

#if GTP_HAVE_TOUCH_KEY
	if (CHK_BIT(cur_event, BIT_TOUCH_KEY) || CHK_BIT(pre_event, BIT_TOUCH_KEY)) {
		for (i = 0; i < GTP_MAX_KEY_NUM; i++) {
			input_report_key(dev, gt1x_touch_key_array[i], key_value & (0x01 << i));
		}
		if (CHK_BIT(cur_event, BIT_TOUCH_KEY)) {
			GTP_DEBUG("Key Down.");
		} else {
			GTP_DEBUG("Key Up.");
		}
	}
#elif TPD_HAVE_BUTTON
	if (CHK_BIT(cur_event, BIT_TOUCH_KEY) || CHK_BIT(pre_event, BIT_TOUCH_KEY)) {
		for (i = 0; i < TPD_KEY_COUNT; i++) {
			if (key_value & (0x01 << i)) {
				gt1x_touch_down(tpd_virtual_key_array[i].x, tpd_virtual_key_array[i].y, 0, 0);
				GTP_DEBUG("Key Down.");
				break;
			}
		}
		if (i == TPD_KEY_COUNT) {
			gt1x_touch_up(0);
			GTP_DEBUG("Key Up.");
		}
	}
#endif

	/* finger touch event*/
	if (CHK_BIT(cur_event, BIT_TOUCH)) {
		u8 report_num = 0;
		coor_data = &touch_data[1];
		id = coor_data[0] & 0x0F;
		for (i = 0; i < GTP_MAX_TOUCH; i++) {
			if (i == id) {
				input_x = coor_data[1] | (coor_data[2] << 8);
				input_y = coor_data[3] | (coor_data[4] << 8);
				input_w = coor_data[5] | (coor_data[6] << 8);

				input_x = GTP_WARP_X(gt1x_abs_x_max, input_x);
				input_y = GTP_WARP_Y(gt1x_abs_y_max, input_y);

				GTP_DEBUG("(%d)(%d,%d)[%d]", id, input_x, input_y, input_w);
				gt1x_touch_down(input_x, input_y, input_w, i);
				if (report_num++ < touch_num) {
					coor_data += 8;
					id = coor_data[0] & 0x0F;
				}
				pre_index |= 0x01 << i;
			} else if (pre_index & (0x01 << i)) {
#if GTP_ICS_SLOT_REPORT
				gt1x_touch_up(i);
#endif
				pre_index &= ~(0x01 << i);
			}
		}
	} else if (CHK_BIT(pre_event, BIT_TOUCH)) {
#if GTP_ICS_SLOT_REPORT
		int cycles = pre_index < 3 ? 3 : GTP_MAX_TOUCH;
		for (i = 0; i < cycles; i++) {
			if (pre_index >> i & 0x01) {
				gt1x_touch_up(i);
			}
		}
#else
		gt1x_touch_up(0);
#endif
		GTP_DEBUG("Released Touch.");
		pre_index = 0;
	}

	/* start sync input report */
	if (CHK_BIT(cur_event, BIT_STYLUS_KEY | BIT_STYLUS)
			|| CHK_BIT(pre_event, BIT_STYLUS_KEY | BIT_STYLUS)) {
		input_sync(pen_dev);
	}

	if (CHK_BIT(cur_event, BIT_TOUCH_KEY | BIT_TOUCH)
			|| CHK_BIT(pre_event, BIT_TOUCH_KEY | BIT_TOUCH)) {
		input_sync(dev);
	}

	if (unlikely(!pre_event && !cur_event)) {
		GTP_DEBUG("Additional Int Pulse.");
	} else {
		pre_event = cur_event;
	}

	return 0;
}

#if GTP_WITH_STYLUS
struct input_dev *pen_dev;

static void gt1x_pen_init(void)
{
	s32 ret = 0;

	pen_dev = input_allocate_device();
	if (pen_dev == NULL) {
		GTP_ERROR("Failed to allocate input device for pen/stylus.");
		return;
	}

	pen_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	pen_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	set_bit(BTN_TOOL_PEN, pen_dev->keybit);
	set_bit(INPUT_PROP_DIRECT, pen_dev->propbit);

#if GTP_HAVE_STYLUS_KEY
	input_set_capability(pen_dev, EV_KEY, BTN_STYLUS);
	input_set_capability(pen_dev, EV_KEY, BTN_STYLUS2);
#endif

	input_set_abs_params(pen_dev, ABS_MT_POSITION_X, 0, gt1x_abs_x_max, 0, 0);
	input_set_abs_params(pen_dev, ABS_MT_POSITION_Y, 0, gt1x_abs_y_max, 0, 0);
	input_set_abs_params(pen_dev, ABS_MT_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(pen_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(pen_dev, ABS_MT_TRACKING_ID, 0, 255, 0, 0);

	pen_dev->name = "goodix-pen";
	pen_dev->phys = "input/ts";
	pen_dev->id.bustype = BUS_I2C;

	ret = input_register_device(pen_dev);
	if (ret) {
		GTP_ERROR("Register %s input device failed", pen_dev->name);
		return;
	}
}

void gt1x_pen_down(s32 x, s32 y, s32 size, s32 id)
{
	input_report_key(pen_dev, BTN_TOOL_PEN, 1);
#if GTP_CHANGE_X2Y
	GTP_SWAP(x, y);
#endif

#if GTP_ICS_SLOT_REPORT
	input_mt_slot(pen_dev, id);
	input_report_abs(pen_dev, ABS_MT_PRESSURE, size);
	input_report_abs(pen_dev, ABS_MT_TOUCH_MAJOR, size);
	input_report_abs(pen_dev, ABS_MT_TRACKING_ID, id);
	input_report_abs(pen_dev, ABS_MT_POSITION_X, x);
	input_report_abs(pen_dev, ABS_MT_POSITION_Y, y);
#else
	input_report_key(pen_dev, BTN_TOUCH, 1);
	if ((!size) && (!id)) {
		/* for virtual button */
		input_report_abs(pen_dev, ABS_MT_PRESSURE, 100);
		input_report_abs(pen_dev, ABS_MT_TOUCH_MAJOR, 100);
	} else {
		input_report_abs(pen_dev, ABS_MT_PRESSURE, size);
		input_report_abs(pen_dev, ABS_MT_TOUCH_MAJOR, size);
		input_report_abs(pen_dev, ABS_MT_TRACKING_ID, id);
	}
	input_report_abs(pen_dev, ABS_MT_POSITION_X, x);
	input_report_abs(pen_dev, ABS_MT_POSITION_Y, y);
	input_mt_sync(pen_dev);
#endif
}

void gt1x_pen_up(s32 id)
{
	input_report_key(pen_dev, BTN_TOOL_PEN, 0);
#if GTP_ICS_SLOT_REPORT
	input_mt_slot(pen_dev, id);
	input_report_abs(pen_dev, ABS_MT_TRACKING_ID, -1);
#else
	input_report_key(pen_dev, BTN_TOUCH, 0);
	input_mt_sync(pen_dev);
#endif
}
#endif

/**
 * Proximity Module
 */
#if GTP_PROXIMITY
#define GTP_PS_DEV_NAME             "goodix_proximity"
#define GTP_REG_PROXIMITY_ENABLE    0x8049
#define PS_FARAWAY                  1
#define PS_NEAR                     0
struct gt1x_ps_device{
    int enabled; /* module enabled/disabled */
    int state;   /* Faraway or Near */
#ifdef PLATFORM_MTK
    struct hwmsen_object obj_ps;
#else
    struct input_dev *input_dev;
    struct kobject *kobj;
#endif
};
static struct gt1x_ps_device *gt1x_ps_dev;

static void gt1x_ps_report(int state)
{
#ifdef PLATFORM_MTK
	s32 ret = -1;

	hwm_sensor_data sensor_data;
	/*map and store data to hwm_sensor_data*/
	sensor_data.values[0] = !!state;
	sensor_data.value_divide = 1;
	sensor_data.status = SENSOR_STATUS_ACCURACY_MEDIUM;
	/*report to the up-layer*/
	ret = hwmsen_get_interrupt_data(ID_PROXIMITY, &sensor_data);
	if (ret) {
		GTP_ERROR("Call hwmsen_get_interrupt_data fail = %d\n", ret);
	}
#else
	input_report_abs(gt1x_ps_dev->input_dev, ABS_DISTANCE, !!state);
	input_sync(gt1x_ps_dev->input_dev);
#endif /* End PLATFROM_MTK */

	GTP_INFO("Report proximity state: %s", state == PS_FARAWAY ? "FARAWAY":"NEAR");
}

static s32 gt1x_ps_enable(s32 enable)
{
	u8 state;
	s32 ret = -1;

	GTP_INFO("Proximity function to be %s.", enable ? "on" : "off");
	state = enable ? 1 : 0;
	if (gt1x_chip_type == CHIP_TYPE_GT1X)
		ret = gt1x_i2c_write(GTP_REG_PROXIMITY_ENABLE, &state, 1);
	else if (gt1x_chip_type == CHIP_TYPE_GT2X)
		ret = gt1x_send_cmd(state ? 0x12 : 0x13, 0);
	if (ret) {
		GTP_ERROR("GTP %s proximity cmd failed.", state ? "enable" : "disable");
	}

	if (!ret && enable) {
		gt1x_ps_dev->enabled = 1;
	} else {
		gt1x_ps_dev->enabled = 0;
	}
	gt1x_ps_dev->state = PS_FARAWAY;
	GTP_INFO("Proximity function %s %s.", state ? "enable" : "disable", ret ? "fail" : "success");
	return ret;
}

int gt1x_prox_event_handler(u8 *data)
{
	u8 ps = 0;

	if (gt1x_ps_dev && gt1x_ps_dev->enabled) {
		ps = (data[0] & 0x60) ? 0 : 1;
		if (ps != gt1x_ps_dev->state) {
			gt1x_ps_report(ps);
			gt1x_ps_dev->state = ps;
			GTP_DEBUG("REG INDEX[0x814E]:0x%02X\n", data[0]);
		}

		return (ps == PS_NEAR ? 1 : 0);
	}
	return -1;
}

#ifdef PLATFORM_MTK
static inline s32 gt1x_get_ps_value(void)
{
	return gt1x_ps_dev->state;
}

static s32 gt1x_ps_operate(void *self, u32 command, void *buff_in, s32 size_in, void *buff_out, s32 size_out, s32 *actualout)
{
	s32 err = 0;
	s32 value;
	hwm_sensor_data *sensor_data;

	GTP_INFO("psensor operator cmd:%d", command);
	switch (command) {
	case SENSOR_DELAY:
		if ((buff_in == NULL) || (size_in < sizeof(int))) {
			GTP_ERROR("Set delay parameter error!");
			err = -EINVAL;
		}
		/*Do nothing*/
		break;

	case SENSOR_ENABLE:
		if ((buff_in == NULL) || (size_in < sizeof(int))) {
			GTP_ERROR("Enable sensor parameter error!");
			err = -EINVAL;
		} else {
			value = *(int *)buff_in;
			err = gt1x_ps_enable(value);
		}

		break;

	case SENSOR_GET_DATA:
		if ((buff_out == NULL) || (size_out < sizeof(hwm_sensor_data))) {
			GTP_ERROR("Get sensor data parameter error!");
			err = -EINVAL;
		} else {
			sensor_data = (hwm_sensor_data *) buff_out;
			sensor_data->values[0] = gt1x_get_ps_value();
			sensor_data->value_divide = 1;
			sensor_data->status = SENSOR_STATUS_ACCURACY_MEDIUM;
		}

		break;

	default:
		GTP_ERROR("proxmy sensor operate function no this parameter %d!\n", command);
		err = -1;
		break;
	}

	return err;
}
#endif

#ifndef PLATFORM_MTK
static ssize_t gt1x_ps_enable_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d", gt1x_ps_dev->enabled);
}

static ssize_t gt1x_ps_enable_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	unsigned int input;
	if (sscanf(buf, "%u", &input) != 1) {
		return -EINVAL;
	}
	if (input == 1) {
		gt1x_ps_enable(1);
		gt1x_ps_report(PS_FARAWAY);
	} else if (input == 0) {
		gt1x_ps_report(PS_FARAWAY);
		gt1x_ps_enable(0);
	} else {
		return -EINVAL;
	}
	return count;
}

static ssize_t gt1x_ps_state_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d", gt1x_ps_dev->state);
}

static ssize_t gt1x_ps_state_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	unsigned int input;

	if (sscanf(buf, "%u", &input) != 1) {
		return -EINVAL;
	}

	if (!gt1x_ps_dev->enabled) {
		return -EINVAL;
	}

	if (input == 1) {
		gt1x_ps_dev->state = PS_FARAWAY;
	} else if (input == 0) {
		gt1x_ps_dev->state = PS_NEAR;
	} else {
		return -EINVAL;
	}

	gt1x_ps_report(gt1x_ps_dev->state);
	return count;
}

static struct kobj_attribute ps_attrs[] = {
	__ATTR(enable, S_IWUGO | S_IRUGO, gt1x_ps_enable_show, gt1x_ps_enable_store),
	__ATTR(state, S_IWUGO | S_IRUGO, gt1x_ps_state_show, gt1x_ps_state_store)
};

#endif /* End PLATFORM_MTK */

static int gt1x_ps_init(void)
{
	int err;

	gt1x_ps_dev = kzalloc(sizeof(struct gt1x_ps_device), GFP_KERNEL);
	if (!gt1x_ps_dev) {
		return  -ENOMEM;
	}

	gt1x_ps_dev->state = PS_FARAWAY;

#ifdef PLATFORM_MTK
	gt1x_ps_dev->obj_ps.polling = 0;	/* 0--interrupt mode;1--polling mode; */
	gt1x_ps_dev->obj_ps.sensor_operate = gt1x_ps_operate;

	if (hwmsen_attach(ID_PROXIMITY, &gt1x_ps_dev->obj_ps)) {
		GTP_ERROR("hwmsen attach fail, return:%d.", err);
		goto err_exit;
	}

	GTP_INFO("hwmsen attach OK.");
	return 0;
#else
	gt1x_ps_dev->input_dev = input_allocate_device();
	if (!gt1x_ps_dev->input_dev) {
		GTP_ERROR("Failed to alloc inpput device for proximity!");
		err = -ENOMEM;
		goto err_exit;
	}

	gt1x_ps_dev->input_dev->name = GTP_PS_DEV_NAME;
	gt1x_ps_dev->input_dev->phys = "goodix/proximity";
	gt1x_ps_dev->input_dev->id.bustype = BUS_I2C;
	gt1x_ps_dev->input_dev->id.vendor = 0xDEED;
	gt1x_ps_dev->input_dev->id.product = 0xBEEF;
	gt1x_ps_dev->input_dev->id.version = 1;
	set_bit(EV_ABS, gt1x_ps_dev->input_dev->evbit);
	input_set_abs_params(gt1x_ps_dev->input_dev, ABS_DISTANCE, 0, 1, 0, 0);

	err = input_register_device(gt1x_ps_dev->input_dev);
	if (err) {
		GTP_ERROR("Failed to register proximity input device: %s!", gt1x_ps_dev->input_dev->name);
		goto err_register_dev;
	}
	/* register sysfs interface  */
	if (!sysfs_rootdir) {
		sysfs_rootdir = kobject_create_and_add("goodix", NULL);
		if (!sysfs_rootdir) {
			GTP_ERROR("Failed to create and add sysfs interface: goodix.");
			err = -ENOMEM;
			goto err_register_dev;
		}
	}

	gt1x_ps_dev->kobj = kobject_create_and_add("proximity", sysfs_rootdir);
	if (!gt1x_ps_dev->kobj) {
		GTP_ERROR("Failed to create and add sysfs interface: proximity.");
		err = -ENOMEM;
		goto err_register_dev;
	}
	/*create sysfs files*/
	{
		int i;
		for (i = 0; i < sizeof(ps_attrs)/sizeof(ps_attrs[0]); i++) {
			if (sysfs_create_file(gt1x_ps_dev->kobj, &ps_attrs[i].attr)) {
				goto err_create_file;
			}
		}
	}

	GTP_INFO("Proximity sensor init OK.");
	return 0;
err_create_file:
	kobject_put(gt1x_ps_dev->kobj);
err_register_dev:
	input_free_device(gt1x_ps_dev->input_dev);
#endif  /* End PLATFROM_MTK */

err_exit:
	kfree(gt1x_ps_dev);
	gt1x_ps_dev = NULL;
	return err;
}

static void gt1x_ps_deinit(void)
{
	if (gt1x_ps_dev) {
#ifndef PLATFORM_MTK
		int i = 0;
		for (; i < sizeof(ps_attrs) / sizeof(ps_attrs[0]); i++) {
			sysfs_remove_file(gt1x_ps_dev->kobj, &ps_attrs[i].attr);
		}
		kobject_del(gt1x_ps_dev->kobj);
		input_free_device(gt1x_ps_dev->input_dev);
#endif
		kfree(gt1x_ps_dev);
	}
}

#endif /*GTP_PROXIMITY */

/**
 *			ESD Protect Module
 */
#if GTP_ESD_PROTECT
static int esd_work_cycle = 200;
static struct delayed_work esd_check_work;
static int esd_running;
static struct mutex esd_lock;
static void gt1x_esd_check_func(struct work_struct *);

void gt1x_init_esd_protect(void)
{
	esd_work_cycle = 2 * HZ;	/* HZ: clock ticks in 1 second generated by system */
	GTP_DEBUG("Clock ticks for an esd cycle: %d", esd_work_cycle);
	INIT_DELAYED_WORK(&esd_check_work, gt1x_esd_check_func);
	mutex_init(&esd_lock);
}

static void gt1x_deinit_esd_protect(void)
{
	gt1x_esd_switch(SWITCH_OFF);
}

void gt1x_esd_switch(s32 on)
{
	mutex_lock(&esd_lock);
	if (SWITCH_ON == on) {	/* switch on esd check */
		if (!esd_running) {
			esd_running = 1;
			GTP_INFO("Esd protector started!");
			queue_delayed_work(gt1x_workqueue, &esd_check_work, esd_work_cycle);
		}
	} else {		/* switch off esd check */
		if (esd_running) {
			esd_running = 0;
			GTP_INFO("Esd protector stoped!");
			cancel_delayed_work(&esd_check_work);
		}
	}
	mutex_unlock(&esd_lock);
}

static void gt1x_esd_check_func(struct work_struct *work)
{
	s32 i = 0;
	s32 ret = -1;
	u8 esd_buf[4] = { 0 };

	if (!esd_running) {
		GTP_INFO("Esd protector suspended!");
		return;
	}

	for (i = 0; i < 3; i++) {
		ret = gt1x_i2c_read(GTP_REG_CMD, esd_buf, 4);
		GTP_DEBUG("[Esd]0x8040 = 0x%02X, 0x8043 = 0x%02X", esd_buf[0], esd_buf[3]);
		if (!ret && esd_buf[0] != 0xAA && esd_buf[3] == 0xAA) {
			break;
		}
		msleep(50);
	}

	if (likely(i < 3)) {
		/* IC works normally, Write 0x8040 0xAA, feed the watchdog */
		gt1x_send_cmd(GTP_CMD_ESD, 0);
	} else {
		if (esd_running) {
			GTP_ERROR("IC works abnormally! Process reset guitar.");
			memset(esd_buf, 0x01, sizeof(esd_buf));
			gt1x_i2c_write(0x4226, esd_buf, sizeof(esd_buf));
			msleep(50);

			gt1x_power_reset();
		} else {
			GTP_INFO("Esd protector suspended, no need reset!");
		}
	}

	mutex_lock(&esd_lock);
	if (esd_running) {
		queue_delayed_work(gt1x_workqueue, &esd_check_work, esd_work_cycle);
	} else {
		GTP_INFO("Esd protector suspended!");
	}
	mutex_unlock(&esd_lock);
}
#endif

/**
 *              Smart Cover Module
 */
#if GTP_SMART_COVER
struct smart_cover_device{
    int enabled;
    int state; /* 0:cover faraway 1:near */
    int suspended;  /* suspended or woring */
    struct kobject *kobj;
    u8 config[GTP_CONFIG_MAX_LENGTH];
    int cfg_len;
};
static struct smart_cover_device *gt1x_sc_dev;

/**
 * gt1x_smart_cover_update_state - update smart cover config
 */
static int gt1x_smart_cover_update_state(void)
{
	int ret = 0;
	struct smart_cover_device *dev = gt1x_sc_dev;

	if (!dev) {
		return -ENODEV;
	}

	if (!dev->suspended) {
		if (dev->state) {  /* near */
			ret = gt1x_send_cfg(dev->config, dev->cfg_len);
		} else {
#if GTP_CHARGER_SWITCH
			gt1x_charger_config(1);  /*charger detector module check and*/
			/*send a config*/
#else
			ret = gt1x_send_cfg(gt1x_config, gt1x_cfg_length);
#endif
		}
		GTP_DEBUG("Update cover state %s.", dev->state ? "Nearby" : "Far away");
	} else {
		GTP_DEBUG("TP is suspended, do nothing.");
	}
	return ret;
}

static ssize_t smart_cover_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct smart_cover_device *dev = gt1x_sc_dev;

	if (!dev) {
		return -ENODEV;
	}

	return scnprintf(buf, PAGE_SIZE, "%d", dev->state);
}

static ssize_t smart_cover_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct smart_cover_device *dev = gt1x_sc_dev;
	int s = (buf[0] - '0');

	if (!dev || !dev->enabled || s > 1 || s == dev->state) {
		return count;
	}

	dev->state = s;
	gt1x_smart_cover_update_state();

	return count;
}

/**
 * gt1x_parse_sc_cfg - parse smart cover config
 * @sensor_id: sensor id of the hardware
 */
int gt1x_parse_sc_cfg(int sensor_id)
{
#undef _cfg_array_
#define _cfg_array_(n)   GTP_SMART_COVER_CFG_GROUP##n
	u8 *cfg;
	int *len;

	if (!gt1x_sc_dev)
		return -ENODEV;
	cfg = gt1x_sc_dev->config;
	len = &gt1x_sc_dev->cfg_len;

#if GTP_DRIVER_SEND_CFG
	do {
		u8 cfg_grp0[] = _cfg_array_(0);
		u8 cfg_grp1[] = _cfg_array_(1);
		u8 cfg_grp2[] = _cfg_array_(2);
		u8 cfg_grp3[] = _cfg_array_(3);
		u8 cfg_grp4[] = _cfg_array_(4);
		u8 cfg_grp5[] = _cfg_array_(5);
		u8 *cfgs[] = {
			cfg_grp0, cfg_grp1, cfg_grp2,
			cfg_grp3, cfg_grp4, cfg_grp5
		};
		u8 cfg_lens[] = {
			CFG_GROUP_LEN(cfg_grp0), CFG_GROUP_LEN(cfg_grp1),
			CFG_GROUP_LEN(cfg_grp2), CFG_GROUP_LEN(cfg_grp3),
			CFG_GROUP_LEN(cfg_grp4), CFG_GROUP_LEN(cfg_grp5)
		};

		if (sensor_id >= sizeof(cfgs) / sizeof(cfgs[0])) {
			GTP_ERROR("Invalid sensor id.");
			return -1;
		}

		*len = cfg_lens[sensor_id];
		if (*len == 0 || *len != gt1x_cfg_length) {
			memset(cfg, 0, GTP_CONFIG_MAX_LENGTH);
			*len = 0;
			GTP_ERROR("Length of config is incorrect.");
			return -1;
		}

		memcpy(cfg, cfgs[sensor_id], cfg_lens[sensor_id]);

		cfg[0] &= 0x7F;
		set_reg_bit(cfg[TRIGGER_LOC], 0, gt1x_int_type);
		set_reg_bit(cfg[MODULE_SWITCH3_LOC], 5, !gt1x_wakeup_level);
	} while (0);
#endif
	return 0;
}


static struct kobj_attribute sc_attr =
    __ATTR(state, S_IWUGO | S_IRUGO, smart_cover_show, smart_cover_store);
static int gt1x_smart_cover_init(void)
{
	int err = 0;

	gt1x_sc_dev = kzalloc(sizeof(struct smart_cover_device), GFP_KERNEL);
	if (!gt1x_sc_dev) {
		GTP_ERROR("SmartCover init failed in step: 1.");
		return -ENOMEM;
	}

	gt1x_sc_dev->enabled = 1;
	gt1x_parse_sc_cfg(gt1x_version.sensor_id);

	if (!sysfs_rootdir) {
		/*this kobject is shared between modules, do not free it when error occur*/
		sysfs_rootdir = kobject_create_and_add(GOODIX_SYSFS_DIR, NULL);
		if (!sysfs_rootdir) {
			err = -2;
			goto exit_free_mem;
		}
	}

	if (!gt1x_sc_dev->kobj)
		gt1x_sc_dev->kobj = kobject_create_and_add("smartcover", sysfs_rootdir);
	if (!gt1x_sc_dev->kobj) {
		err = -3;
		goto exit_free_mem;
	}

	if (sysfs_create_file(gt1x_sc_dev->kobj, &sc_attr.attr)) {
		err = -4;
		goto exit_put_kobj;
	}
	GTP_INFO("SmartCover module init OK.");
	return 0;
exit_put_kobj:
	kobject_put(gt1x_sc_dev->kobj);
exit_free_mem:
	kfree(gt1x_sc_dev);
	gt1x_sc_dev = NULL;
	GTP_ERROR("SmartCover init failed in step:%d", -err);
	return err;
}

static void gt1x_smart_cover_deinit(void)
{
	if (!gt1x_sc_dev) {
		return;
	}

	kobject_del(gt1x_sc_dev->kobj);
	kfree(gt1x_sc_dev);
	gt1x_sc_dev = NULL;
}
#endif

/**
 * Charger Detect & Switch Module
 */
#if GTP_CHARGER_SWITCH
static u8 gt1x_config_charger[GTP_CONFIG_MAX_LENGTH] = { 0 };
static struct delayed_work charger_switch_work;
static int charger_work_cycle = 200;
static spinlock_t charger_lock;
static int charger_running;
static void gt1x_charger_work_func(struct work_struct *);

/**
 * gt1x_parse_chr_cfg - parse  charger config
 * @sensor_id: sensor id of the hardware
 * Return:  0: succeed, <0 error
 */
int gt1x_parse_chr_cfg(int sensor_id)
{
#undef _cfg_array_
#define _cfg_array_(n)   GTP_CHARGER_CFG_GROUP##n
	u8 *cfg;
	int len;
	cfg = gt1x_config_charger;

#if GTP_DRIVER_SEND_CFG
	do {
		u8 cfg_grp0[] = _cfg_array_(0);
		u8 cfg_grp1[] = _cfg_array_(1);
		u8 cfg_grp2[] = _cfg_array_(2);
		u8 cfg_grp3[] = _cfg_array_(3);
		u8 cfg_grp4[] = _cfg_array_(4);
		u8 cfg_grp5[] = _cfg_array_(5);
		u8 *cfgs[] = {
			cfg_grp0, cfg_grp1, cfg_grp2,
			cfg_grp3, cfg_grp4, cfg_grp5
		};
		u8 cfg_lens[] = {
			CFG_GROUP_LEN(cfg_grp0), CFG_GROUP_LEN(cfg_grp1),
			CFG_GROUP_LEN(cfg_grp2), CFG_GROUP_LEN(cfg_grp3),
			CFG_GROUP_LEN(cfg_grp4), CFG_GROUP_LEN(cfg_grp5)
		};

		if (sensor_id >= sizeof(cfgs) / sizeof(cfgs[0])) {
			return -1;
		}

		len = cfg_lens[sensor_id];
		if (len == 0 || len != gt1x_cfg_length) {
			memset(cfg, 0, GTP_CONFIG_MAX_LENGTH);
			GTP_ERROR("Length of config is incorrect.");
			return -1;
		}

		memcpy(cfg, cfgs[sensor_id], cfg_lens[sensor_id]);

		cfg[0] &= 0x7F;
		cfg[RESOLUTION_LOC] = (u8) gt1x_abs_x_max;
		cfg[RESOLUTION_LOC + 1] = (u8) (gt1x_abs_x_max >> 8);
		cfg[RESOLUTION_LOC + 2] = (u8) gt1x_abs_y_max;
		cfg[RESOLUTION_LOC + 3] = (u8) (gt1x_abs_y_max >> 8);

		set_reg_bit(cfg[TRIGGER_LOC], 0, gt1x_int_type);
		set_reg_bit(cfg[MODULE_SWITCH3_LOC], 5, !gt1x_wakeup_level);
	} while (0);
#endif
	return 0;
}


static void gt1x_init_charger(void)
{
	charger_work_cycle = 2 * HZ;	/* HZ: clock ticks in 1 second generated by system */
	GTP_DEBUG("Clock ticks for an charger cycle: %d", charger_work_cycle);
	INIT_DELAYED_WORK(&charger_switch_work, gt1x_charger_work_func);
	spin_lock_init(&charger_lock);

	if (gt1x_parse_chr_cfg(gt1x_version.sensor_id) < 0) {
		GTP_ERROR("Error occured when parse charger config.");
	}
}

/**
 * gt1x_charger_switch - switch states of charging work thread
 *
 * @on: SWITCH_ON - start work thread, SWITCH_OFF: stop .
 */
void gt1x_charger_switch(s32 on)
{
	spin_lock(&charger_lock);
	if (SWITCH_ON == on) {
		if (!charger_running) {
			charger_running = 1;
			spin_unlock(&charger_lock);
			GTP_INFO("Charger checker started!");
			queue_delayed_work(gt1x_workqueue, &charger_switch_work, charger_work_cycle);
		} else {
			spin_unlock(&charger_lock);
		}
	} else {
		if (charger_running) {
			charger_running = 0;
			spin_unlock(&charger_lock);
			cancel_delayed_work(&charger_switch_work);
			GTP_INFO("Charger checker stoped!");
		} else {
			spin_unlock(&charger_lock);
		}
	}
}

/**
 * gt1x_charger_config - check and update charging status configuration
 * @dir_update
 * 	 0: check before send charging status configuration
 *  	 1: directly send charging status configuration
 *
 */
void gt1x_charger_config(s32 dir_update)
{
	static u8 chr_pluggedin;

#if GTP_SMART_COVER
	if (gt1x_sc_dev && gt1x_sc_dev->enabled
			&& gt1x_sc_dev->state) {
		return;
	}
#endif

	if (gt1x_get_charger_status()) {
		if (!chr_pluggedin || dir_update) {
			GTP_INFO("Charger Plugin.");
			if (gt1x_send_cfg(gt1x_config_charger, gt1x_cfg_length)) {
				GTP_ERROR("Send config for Charger Plugin failed!");
			}
			if (gt1x_send_cmd(GTP_CMD_CHARGER_ON, 0)) {
				GTP_ERROR("Update status for Charger Plugin failed!");
			}
			chr_pluggedin = 1;
		}
	} else {
		if (chr_pluggedin || dir_update) {
			GTP_INFO("Charger Plugout.");
			if (gt1x_send_cfg(gt1x_config, gt1x_cfg_length)) {
				GTP_ERROR("Send config for Charger Plugout failed!");
			}
			if (gt1x_send_cmd(GTP_CMD_CHARGER_OFF, 0)) {
				GTP_ERROR("Update status for Charger Plugout failed!");
			}
			chr_pluggedin = 0;
		}
	}
}

static void gt1x_charger_work_func(struct work_struct *work)
{
	if (!charger_running) {
		GTP_INFO("Charger checker suspended!");
		return;
	}

	gt1x_charger_config(0);

	GTP_DEBUG("Charger check done!");
	if (charger_running) {
		queue_delayed_work(gt1x_workqueue, &charger_switch_work, charger_work_cycle);
	}
}
#endif

int gt1x_suspend(void)
{
	s32 ret = -1;
#if GTP_HOTKNOT && !HOTKNOT_BLOCK_RW
	u8 buf[1] = { 0 };
#endif

	if (update_info.status) {
		return 0;
	}
#if GTP_SMART_COVER
	if (gt1x_sc_dev) {
		gt1x_sc_dev->suspended = 1;
	}
#endif
	GTP_INFO("Suspend start...");
#if GTP_PROXIMITY
	if (gt1x_ps_dev && gt1x_ps_dev->enabled) {
		GTP_INFO("proximity is detected!");
		return 0;
	}
#endif

#if GTP_HOTKNOT
	if (hotknot_enabled) {
#if HOTKNOT_BLOCK_RW
		if (hotknot_paired_flag) {
			GTP_INFO("hotknot is paired!");
			return 0;
		}
#else
		ret = gt1x_i2c_read_dbl_check(GTP_REG_HN_PAIRED, buf, sizeof(buf));
		if ((!ret && buf[0] == 0x55) || hotknot_transfer_mode) {
			GTP_DEBUG("0x81AA: 0x%02X", buf[0]);
			GTP_INFO("hotknot is paired!");
			return 0;
		}
#endif
	}
#endif

	gt1x_halt = 1;
#if GTP_ESD_PROTECT
	gt1x_esd_switch(SWITCH_OFF);
#endif
#if GTP_CHARGER_SWITCH
	gt1x_charger_switch(SWITCH_OFF);
#endif
	gt1x_irq_disable();

#if GTP_GESTURE_WAKEUP
	gesture_clear_wakeup_data();
	if (gesture_enabled) {
		gesture_enter_doze();
		gt1x_irq_enable();
		gt1x_halt = 0;
	} else
#endif
	{
		ret = gt1x_enter_sleep();
		if (ret < 0) {
			GTP_ERROR("Suspend failed.");
		}
	}

	/* to avoid waking up while not sleeping
	   delay 48 + 10ms to ensure reliability */
	msleep(58);
	GTP_INFO("Suspend end...");
	return 0;
}

int gt1x_resume(void)
{
	s32 ret = -1;

	if (update_info.status) {
		return 0;
	}

#if GTP_SMART_COVER
	if (gt1x_sc_dev) {
		gt1x_sc_dev->suspended = 0;
	}
#endif
	GTP_DEBUG("Resume start...");

#if GTP_PROXIMITY
	if (gt1x_ps_dev && gt1x_ps_dev->enabled) {
		GTP_INFO("Proximity is on!");
		return 0;
	}
#endif

#if GTP_HOTKNOT
	if (hotknot_enabled) {
#if HOTKNOT_BLOCK_RW
		if (hotknot_paired_flag) {
			hotknot_paired_flag = 0;
			GTP_INFO("Hotknot is paired!");
			return 0;
		}
#endif
	}
#endif

#if GTP_GESTURE_WAKEUP
	/* just return 0 if IC does not suspend */
	if (!gesture_enabled && !gt1x_halt)
		return 0;
#else
	if (!gt1x_halt)
		return 0;
#endif

	ret = gt1x_wakeup_sleep();
	if (ret < 0) {
		GTP_ERROR("Resume failed.");
	}
#if GTP_HOTKNOT
	if (!hotknot_enabled) {
		gt1x_send_cmd(GTP_CMD_HN_EXIT_SLAVE, 0);
	}
#endif

#if GTP_CHARGER_SWITCH
	gt1x_charger_config(0);
	gt1x_charger_switch(SWITCH_ON);
#endif

	gt1x_halt = 0;
	gt1x_irq_enable();

#if GTP_ESD_PROTECT
	gt1x_esd_switch(SWITCH_ON);
#endif

	GTP_DEBUG("Resume end.");
	return 0;
}

s32 gt1x_init(void)
{
	s32 ret = -1;
	s32 retry = 0;
	u8 reg_val[1];

	/* power on */
	gt1x_power_switch(SWITCH_ON);

	while (retry++ < 5) {
		gt1x_init_failed = 0;
		/* reset ic */
		ret = gt1x_reset_guitar();
		if (ret != 0) {
			GTP_ERROR("Reset guitar failed!");
			continue;
		}

		/* check main system firmware */
		ret = gt1x_i2c_read_dbl_check(GTP_REG_FW_CHK_MAINSYS, reg_val, 1);
		if (ret != 0) {
			continue;
		} else if (reg_val[0] != 0xBE) {
			GTP_ERROR("Check main system not pass[0x%2X].", reg_val[0]);
			gt1x_init_failed = 1;
		}

#if !GTP_AUTO_UPDATE
		/* debug info  */
		ret = gt1x_i2c_read_dbl_check(GTP_REG_FW_CHK_SUBSYS, reg_val, 1);
		if (!ret && reg_val[0] == 0xAA) {
			GTP_ERROR("Check subsystem not pass[0x%2X].", reg_val[0]);
		}
#endif
		break;
	}

	/* if the initialization fails, set default setting */
	ret |= gt1x_init_failed;
	if (ret) {
		GTP_ERROR("Init failed, use default setting");
		gt1x_abs_x_max = GTP_MAX_WIDTH;
		gt1x_abs_y_max = GTP_MAX_HEIGHT;
		gt1x_int_type = GTP_INT_TRIGGER;
		gt1x_wakeup_level = GTP_WAKEUP_LEVEL;
	}

	/* get chip type */
	ret = gt1x_get_chip_type();
	if (ret != 0) {
		GTP_ERROR("Get chip type failed!");
	}

	/* read version information */
	ret = gt1x_read_version(&gt1x_version);
	if (ret != 0) {
		GTP_ERROR("Get verision failed!");
	}

	/* init and send configs */
	ret = gt1x_init_panel();
	if (ret != 0) {
		GTP_ERROR("Init panel failed.");
	}

	gt1x_workqueue = create_singlethread_workqueue("gt1x_workthread");
	if (gt1x_workqueue == NULL) {
		GTP_ERROR("Create workqueue failed!");
	}

	/* init auxiliary  node and functions */
#if GTP_DEBUG_NODE
	gt1x_init_debug_node();
#endif

#if GTP_CREATE_WR_NODE
	gt1x_init_tool_node();
#endif

#if GTP_GESTURE_WAKEUP || GTP_HOTKNOT
	gt1x_init_node();
#endif

#if GTP_PROXIMITY
	gt1x_ps_init();
#endif

#if GTP_CHARGER_SWITCH
	gt1x_init_charger();
	gt1x_charger_config(1);
	gt1x_charger_switch(SWITCH_ON);
#endif

#if GTP_SMART_COVER
	gt1x_smart_cover_init();
#endif

#if GTP_WITH_STYLUS
	gt1x_pen_init();
#endif

	return ret;
}

void gt1x_deinit(void)
{
#if GTP_DEBUG_NODE
	gt1x_deinit_debug_node();
#endif

#if GTP_GESTURE_WAKEUP || GTP_HOTKNOT
	gt1x_deinit_node();
#endif

#if GTP_CREATE_WR_NODE
	gt1x_deinit_tool_node();
#endif

#if GTP_ESD_PROTECT
	gt1x_deinit_esd_protect();
#endif

#if GTP_CHARGER_SWITCH
	gt1x_charger_switch(SWITCH_OFF);
#endif

#if GTP_PROXIMITY
	gt1x_ps_deinit();
#endif

#if GTP_SMART_COVER
	gt1x_smart_cover_deinit();
#endif

	if (sysfs_rootdir) {
		kobject_del(sysfs_rootdir);
		sysfs_rootdir = NULL;
	}

	if (gt1x_workqueue) {
		destroy_workqueue(gt1x_workqueue);
	}

}

