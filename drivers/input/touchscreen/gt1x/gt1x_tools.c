/* drivers/input/touchscreen/goodix_tool.c
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

#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <generated/utsrelease.h>
#include "gt1x_generic.h"

static ssize_t gt1x_tool_read(struct file *filp, char __user *buffer, size_t count, loff_t *ppos);
static ssize_t gt1x_tool_write(struct file *filp, const char *buffer, size_t count, loff_t *ppos);


static int gt1x_tool_release(struct inode *inode, struct file *filp);
static int gt1x_tool_open(struct inode *inode, struct file *file);

#pragma pack(1)
typedef struct {
	u8 wr;
	u8 flag;
	u8 flag_addr[2];
	u8 flag_val;
	u8 flag_relation;
	u16 circle;
	u8 times;
	u8 retry;
	u16 delay;
	u16 data_len;
	u8 addr_len;
	u8 addr[2];
	u8 res[3];
	u8 *data;
} st_cmd_head;
#pragma pack()
st_cmd_head cmd_head;

s32 DATA_LENGTH;
s8 IC_TYPE[16] = "GT1X";

#define UPDATE_FUNCTIONS
#define DATA_LENGTH_UINT    512
#define CMD_HEAD_LENGTH     (sizeof(st_cmd_head) - sizeof(u8 *))

static char procname[20] = { 0 };

static struct proc_dir_entry *gt1x_tool_proc_entry;
static struct file_operations gt1x_tool_fops = {
	.read = gt1x_tool_read,
	.write = gt1x_tool_write,
  .open = gt1x_tool_open,
  .release = gt1x_tool_release,
  .owner = THIS_MODULE,
};

static void set_tool_node_name(char *procname)
{

	int v0 = 0, v1 = 0, v2 = 0;

	sscanf(UTS_RELEASE, "%d.%d.%d", &v0, &v1, &v2);
	sprintf(procname, "gmnode%02d%02d%02d", v0, v1, v2);
}

int gt1x_init_tool_node(void)
{
	memset(&cmd_head, 0, sizeof(cmd_head));
	/*if the first operation is read, will return fail.*/
	cmd_head.wr = 1;
	cmd_head.data = kzalloc(DATA_LENGTH_UINT, GFP_KERNEL);
	if (NULL == cmd_head.data) {
		GTP_ERROR("Apply for memory failed.");
		return -1;
	}
	GTP_INFO("Alloc memory size:%d.", DATA_LENGTH_UINT);
	DATA_LENGTH = DATA_LENGTH_UINT - GTP_ADDR_LENGTH;

	set_tool_node_name(procname);

	gt1x_tool_proc_entry = proc_create(procname, 0755, NULL, &gt1x_tool_fops);
	if (gt1x_tool_proc_entry == NULL) {
		GTP_ERROR("CAN't create proc entry /proc/%s.", procname);
		return -1;
	} else {
		GTP_INFO("Created proc entry /proc/%s.", procname);
	}
	return 0;
}

void gt1x_deinit_tool_node(void)
{
	remove_proc_entry(procname, NULL);
	kfree(cmd_head.data);
	cmd_head.data = NULL;
}

static s32 tool_i2c_read(u8 *buf, u16 len)
{
	u16 addr = (buf[0] << 8) + buf[1];
	if (!gt1x_i2c_read(addr, &buf[2], len)) {
		return 1;
	}
	return -1;
}

static s32 tool_i2c_write(u8 *buf, u16 len)
{
	u16 addr = (buf[0] << 8) + buf[1];
	if (!gt1x_i2c_write(addr, &buf[2], len - 2)) {
		return 1;
	}
	return -1;
}

static u8 relation(u8 src, u8 dst, u8 rlt)
{
	u8 ret = 0;

	switch (rlt) {
	case 0:
		ret = (src != dst) ? true : false;
		break;

	case 1:
		ret = (src == dst) ? true : false;
		GTP_DEBUG("equal:src:0x%02x   dst:0x%02x   ret:%d.", src, dst, (s32) ret);
		break;

	case 2:
		ret = (src > dst) ? true : false;
		break;

	case 3:
		ret = (src < dst) ? true : false;
		break;

	case 4:
		ret = (src & dst) ? true : false;
		break;

	case 5:
		ret = (!(src | dst)) ? true : false;
		break;

	default:
		ret = false;
		break;
	}

	return ret;
}

/*******************************************************
Function:
    Comfirm function.
Input:
  None.
Output:
    Return write length.
********************************************************/
static u8 comfirm(void)
{
	s32 i = 0;
	u8 buf[32];

	memcpy(buf, cmd_head.flag_addr, cmd_head.addr_len);

	for (i = 0; i < cmd_head.times; i++) {
		if (tool_i2c_read(buf, 1) <= 0) {
			GTP_ERROR("Read flag data failed!");
			return -1;
		}

		if (true == relation(buf[GTP_ADDR_LENGTH], cmd_head.flag_val, cmd_head.flag_relation)) {
			GTP_DEBUG("value at flag addr:0x%02x.", buf[GTP_ADDR_LENGTH]);
			GTP_DEBUG("flag value:0x%02x.", cmd_head.flag_val);
			break;
		}

		msleep(cmd_head.circle);
	}

	if (i >= cmd_head.times) {
		GTP_ERROR("Didn't get the flag to continue!");
		return -1;
	}

	return 0;
}

/*******************************************************
Function:
    Goodix tool write function.
Input:
  standard proc write function param.
Output:
    Return write length.
********************************************************/
static ssize_t gt1x_tool_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	u64 ret = 0;
	GTP_DEBUG_FUNC();
	GTP_DEBUG_ARRAY((u8 *) buff, len);

	ret = copy_from_user(&cmd_head, buff, CMD_HEAD_LENGTH);
	if (ret) {
		GTP_ERROR("copy_from_user failed.");
	}

	GTP_DEBUG("wr  :0x%02x.", cmd_head.wr);
	/*
	   GTP_DEBUG("flag:0x%02x.", cmd_head.flag);
	   GTP_DEBUG("flag addr:0x%02x%02x.", cmd_head.flag_addr[0], cmd_head.flag_addr[1]);
	   GTP_DEBUG("flag val:0x%02x.", cmd_head.flag_val);
	   GTP_DEBUG("flag rel:0x%02x.", cmd_head.flag_relation);
	   GTP_DEBUG("circle  :%d.", (s32)cmd_head.circle);
	   GTP_DEBUG("times   :%d.", (s32)cmd_head.times);
	   GTP_DEBUG("retry   :%d.", (s32)cmd_head.retry);
	   GTP_DEBUG("delay   :%d.", (s32)cmd_head.delay);
	   GTP_DEBUG("data len:%d.", (s32)cmd_head.data_len);
	   GTP_DEBUG("addr len:%d.", (s32)cmd_head.addr_len);
	   GTP_DEBUG("addr:0x%02x%02x.", cmd_head.addr[0], cmd_head.addr[1]);
	   GTP_DEBUG("len:%d.", (s32)len);
	   GTP_DEBUG("buf[20]:0x%02x.", buff[CMD_HEAD_LENGTH]);
	   */

	if (1 == cmd_head.wr) {
		u16 addr, data_len, pos;

		if (1 == cmd_head.flag) {
			if (comfirm()) {
				GTP_ERROR("[WRITE]Comfirm fail!");
				return -1;
			}
		} else if (2 == cmd_head.flag) {
			/*Need interrupt!*/
		}

		addr = (cmd_head.addr[0] << 8) + cmd_head.addr[1];
		data_len = cmd_head.data_len;
		pos = 0;
		while (data_len > 0) {
			len = data_len > DATA_LENGTH ? DATA_LENGTH : data_len;
			ret = copy_from_user(&cmd_head.data[GTP_ADDR_LENGTH], &buff[CMD_HEAD_LENGTH + pos], len);
			if (ret) {
				GTP_ERROR("[WRITE]copy_from_user failed.");
				return -1;
			}
			cmd_head.data[0] = ((addr >> 8) & 0xFF);
			cmd_head.data[1] = (addr & 0xFF);

			GTP_DEBUG_ARRAY(cmd_head.data, len + GTP_ADDR_LENGTH);

			if (tool_i2c_write(cmd_head.data, len + GTP_ADDR_LENGTH) <= 0) {
				GTP_ERROR("[WRITE]Write data failed!");
				return -1;
			}
			addr += len;
			pos += len;
			data_len -= len;
		}

		if (cmd_head.delay) {
			msleep(cmd_head.delay);
		}
		return cmd_head.data_len + CMD_HEAD_LENGTH;
	} else if (3 == cmd_head.wr) {	/* gt1x unused */
		memcpy(IC_TYPE, cmd_head.data, min((u16)sizeof(IC_TYPE), cmd_head.data_len));
		return cmd_head.data_len + CMD_HEAD_LENGTH;
	} else if (5 == cmd_head.wr) {	/* ? */
		/*memcpy(IC_TYPE, cmd_head.data, cmd_head.data_len);*/
		return cmd_head.data_len + CMD_HEAD_LENGTH;
	} else if (7 == cmd_head.wr) {	/* disable irq! */
		gt1x_irq_disable();
#if GTP_ESD_PROTECT
		gt1x_esd_switch(SWITCH_OFF);
#endif
		return CMD_HEAD_LENGTH;
	} else if (9 == cmd_head.wr) {	/* enable irq! */
		gt1x_irq_enable();
#if GTP_ESD_PROTECT
		gt1x_esd_switch(SWITCH_ON);
#endif
		return CMD_HEAD_LENGTH;
	} else if (17 == cmd_head.wr) {
		ret = copy_from_user(&cmd_head.data[GTP_ADDR_LENGTH], &buff[CMD_HEAD_LENGTH], cmd_head.data_len);
		if (ret) {
			GTP_ERROR("copy_from_user failed.");
			return -1;
		}

		if (cmd_head.data[GTP_ADDR_LENGTH]) {
			GTP_DEBUG("gtp enter rawdiff.");
			gt1x_rawdiff_mode = true;
		} else {
			gt1x_rawdiff_mode = false;
			GTP_DEBUG("gtp leave rawdiff.");
		}

		return CMD_HEAD_LENGTH;
	} else if (11 == cmd_head.wr) {
		gt1x_enter_update_mode();
	} else if (13 == cmd_head.wr) {
		gt1x_leave_update_mode();
	} else if (15 == cmd_head.wr) {
		struct task_struct *thrd = NULL;
		memset(cmd_head.data, 0, cmd_head.data_len + 1);
		memcpy(cmd_head.data, &buff[CMD_HEAD_LENGTH], cmd_head.data_len);
		GTP_DEBUG("update firmware, filename: %s", cmd_head.data);
		thrd = kthread_run(gt1x_update_firmware, (void *)cmd_head.data, "GT1x FW Update");
		if (IS_ERR(thrd)) {
			return PTR_ERR(thrd);
		}
	}
	return CMD_HEAD_LENGTH;
}

static u8 devicecount;
static int gt1x_tool_open(struct inode *inode, struct file *file)
{
	if (devicecount > 0) {
		return -ERESTARTSYS;
		GTP_ERROR("tools open failed!");
	}
	devicecount++;
	return 0;
}

static int gt1x_tool_release(struct inode *inode, struct file *filp)
{
	devicecount--;
	return 0;
}
/*******************************************************
Function:
    Goodix tool read function.
Input:
  standard proc read function param.
Output:
    Return read length.
********************************************************/
static ssize_t gt1x_tool_read(struct file *filp, char __user *buffer, size_t count, loff_t *ppos)
{
	GTP_DEBUG_FUNC();
	if (*ppos) {
		GTP_DEBUG("[PARAM]size: %zd, *ppos: %d", count, (int)*ppos);
		*ppos = 0;
		return 0;
	}

	if (cmd_head.wr % 2) {
		GTP_ERROR("[READ] invaild operator fail!");
		return -1;
	} else if (!cmd_head.wr) {
	    /* general  i2c read  */
		u16 addr, data_len, len, loc;

		if (1 == cmd_head.flag) {
			if (comfirm()) {
				GTP_ERROR("[READ]Comfirm fail!");
				return -1;
			}
		} else if (2 == cmd_head.flag) {
			/*Need interrupt!*/
		}

		addr = (cmd_head.addr[0] << 8) + cmd_head.addr[1];
		data_len = cmd_head.data_len;
		loc = 0;

		GTP_DEBUG("[READ] ADDR:0x%04X.", addr);
		GTP_DEBUG("[READ] Length: %d", data_len);

		if (cmd_head.delay) {
			msleep(cmd_head.delay);
		}

		while (data_len > 0) {
			len = data_len > DATA_LENGTH ? DATA_LENGTH : data_len;
			cmd_head.data[0] = (addr >> 8) & 0xFF;
			cmd_head.data[1] = (addr & 0xFF);
			if (tool_i2c_read(cmd_head.data, len) <= 0) {
				GTP_ERROR("[READ]Read data failed!");
				return -1;
			}
			memcpy(&buffer[loc], &cmd_head.data[GTP_ADDR_LENGTH], len);
			data_len -= len;
			addr += len;
			loc += len;
			GTP_DEBUG_ARRAY(&cmd_head.data[GTP_ADDR_LENGTH], len);
		}
		 *ppos += cmd_head.data_len;
		return cmd_head.data_len;
	} else if (2 == cmd_head.wr) {
		GTP_DEBUG("Return ic type:%s len:%d.", buffer, (s32) cmd_head.data_len);
		return -1;
	} else if (4 == cmd_head.wr) {
	    /* read fw update progress */
		buffer[0] = update_info.progress >> 8;
		buffer[1] = update_info.progress & 0xff;
		buffer[2] = update_info.max_progress >> 8;
		buffer[3] = update_info.max_progress & 0xff;
		*ppos += 4;
		return 4;
	} else if (6 == cmd_head.wr) {
		/*Read error code!*/
		return -1;
	} else if (8 == cmd_head.wr) {
	    /* Read driver version */
		s32 tmp_len;
		tmp_len = strlen(GTP_DRIVER_VERSION);
		memcpy(buffer, GTP_DRIVER_VERSION, tmp_len);
		buffer[tmp_len] = 0;
		*ppos += tmp_len + 1;
		return (tmp_len + 1);
	}
	*ppos += cmd_head.data_len;
	return cmd_head.data_len;
}
