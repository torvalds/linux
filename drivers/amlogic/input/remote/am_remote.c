/*
 * linux/drivers/input/irremote/remote_kbd.c
 *
 * Remote Driver
 *
 * Copyright (C) 2009 Amlogic Corporation
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * author :   jianfeng_wang
 */
/*
 * !!caution: if you use remote ,you should disable card1 used for  ata_enable pin.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <mach/am_regs.h>
#include <mach/pinmux.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of_platform.h>
//#include "plat/remote.h"
#include "am_remote.h"

#undef NEW_BOARD_LEARNING_MODE

#define IR_CONTROL_HOLD_LAST_KEY    (1<<6)
#define IR_CONTROL_DECODER_MODE     (3<<7)
#define IR_CONTROL_SKIP_HEADER      (1<<7)
#define IR_CONTROL_RESET            (1<<0)
#define KEY_RELEASE_DELAY    200

type_printk input_dbg;
#ifdef CONFIG_AML_HDMI_TX
#ifdef CONFIG_ARCH_MESON6
unsigned char cec_repeat = 10;
#endif
#endif

static DEFINE_MUTEX(remote_enable_mutex);
static DEFINE_MUTEX(remote_file_mutex);
static void remote_tasklet(unsigned long);
static int remote_enable;
static int NEC_REMOTE_IRQ_NO = INT_REMOTE;
unsigned int g_remote_base;
static int repeat_flag;
DECLARE_TASKLET_DISABLED(tasklet, remote_tasklet, 0);
	 
static struct remote *gp_remote = NULL;
char *remote_log_buf;
static __u16 key_map[512];
static __u16 mouse_map[6];	/*Left Right Up Down + middlewheel up &down */

static bool key_pointer_switch = true;
static unsigned int FN_KEY_SCANCODE = 0;
static unsigned int LEFT_KEY_SCANCODE = 0;
static unsigned int RIGHT_KEY_SCANCODE = 0;
static unsigned int UP_KEY_SCANCODE = 0;
static unsigned int DOWN_KEY_SCANCODE = 0;
static unsigned int OK_KEY_SCANCODE = 0;
static unsigned int PAGEUP_KEY_SCANCODE = 0;
static unsigned int PAGEDOWN_KEY_SCANCODE = 0;

int remote_printk(const char *fmt, ...)
{
	va_list args;
	int r;

	if (gp_remote->debug_enable == 0) {
		return 0;
	}
	va_start(args, fmt);
	r = vprintk(fmt, args);
	va_end(args);
	return r;
}

static int remote_mouse_event(struct input_dev *dev, unsigned int scancode, unsigned int type)
{
	__u16 mouse_code = REL_X;
	__s32 mouse_value = 0;
	static unsigned int repeat_count = 0;
	//__s32 move_accelerate[] = { 0, 1, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9 };
	__s32 move_accelerate[] = {0, 2, 2, 4, 4, 6, 8, 10, 12, 14, 16, 18};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mouse_map); i++)
		if (mouse_map[i] == scancode) {
			break;
		}

	if (i >= ARRAY_SIZE(mouse_map)) {
		return -1;
	}
	switch (type) {
	case 1:		//press
		repeat_count = 0;
		break;
	case 2:		//repeat
		if (repeat_count >= ARRAY_SIZE(move_accelerate) - 1) {
			repeat_count = ARRAY_SIZE(move_accelerate) - 1;
		} else {
			repeat_count++;
		}
	}
	switch (i) {
	case 0:
		mouse_code = REL_X;
		mouse_value = -(1 + move_accelerate[repeat_count]);
		break;
	case 1:
		mouse_code = REL_X;
		mouse_value = 1 + move_accelerate[repeat_count];
		break;
	case 2:
		mouse_code = REL_Y;
		mouse_value = -(1 + move_accelerate[repeat_count]);
		break;
	case 3:
		mouse_code = REL_Y;
		mouse_value = 1 + move_accelerate[repeat_count];
		break;
	case 4:		//up
		mouse_code = REL_WHEEL;
		mouse_value = 0x1;
		break;
	case 5:
		mouse_code = REL_WHEEL;
		mouse_value = 0xffffffff;
		break;

	}
	if (type) {
		input_event(dev, EV_REL, mouse_code, mouse_value);
		input_sync(dev);
		switch (mouse_code) {
		case REL_X:
		case REL_Y:
			input_dbg("mouse be %s moved %d.\n",
			          mouse_code == REL_X ? "horizontal" : "vertical", mouse_value);
			break;
		case REL_WHEEL:
			input_dbg("mouse wheel move %s .\n", mouse_value == 0x1 ? "up" : "down");
			break;
		}

	}
	return 0;
}

void remote_send_key(struct input_dev *dev, unsigned int scancode, unsigned int type)
{
	if(scancode == FN_KEY_SCANCODE && type == 1)
        {
                // switch from key to pointer
                if(key_pointer_switch)
                {
                        mouse_map[0] = LEFT_KEY_SCANCODE;
                        mouse_map[1] = RIGHT_KEY_SCANCODE;
                        mouse_map[2] = UP_KEY_SCANCODE;
                        mouse_map[3] = DOWN_KEY_SCANCODE;
                        mouse_map[4] = PAGEUP_KEY_SCANCODE;
                        mouse_map[5] = PAGEDOWN_KEY_SCANCODE;

                        key_pointer_switch = false;
                }
                // switch from pointer to key
                else
                {
                        mouse_map[0] = mouse_map[1] =
                        mouse_map[2] = mouse_map[3] =
                        mouse_map[4] = mouse_map[5] = 0xFFFF;

                        key_pointer_switch = true;
                }

                input_event(dev, EV_KEY, key_map[scancode], type);
                input_sync(dev);

                return;
        }

        if(scancode == OK_KEY_SCANCODE && key_pointer_switch == false)
        {
                input_event(dev, EV_KEY, BTN_MOUSE, type);
                input_sync(dev);

                return;
        }

	if (remote_mouse_event(dev, scancode, type)) {
		if (scancode > ARRAY_SIZE(key_map)) {
			input_dbg("scancode is 0x%04x, out of key mapping.\n", scancode);
			return;
		}
		if ((key_map[scancode] >= KEY_MAX)
		    || (key_map[scancode] == KEY_RESERVED)) {
			input_dbg("scancode is 0x%04x, invalid key is 0x%04x.\n", scancode, key_map[scancode]);
			return;
		}
		input_event(dev, EV_KEY, key_map[scancode], type);
		input_sync(dev);
		switch (type) {
		case 0:
			input_dbg("release ircode = 0x%02x, scancode = 0x%04x\n", scancode, key_map[scancode]);
			break;
		case 1:
			input_dbg("press ircode = 0x%02x, scancode = 0x%04x\n", scancode, key_map[scancode]);
			break;
		case 2:
			input_dbg("repeat ircode = 0x%02x, scancode = 0x%04x\n", scancode, key_map[scancode]);
			break;
		}
	}
}

static void disable_remote_irq(void)
{
	if ((!(gp_remote->work_mode && REMOTE_WORK_MODE_FIQ))
	    && (!(gp_remote->work_mode && REMOTE_WORK_MODE_FIQ_RCMM))) {
		disable_irq(NEC_REMOTE_IRQ_NO);
	}

}

static void enable_remote_irq(void)
{
	if ((!(gp_remote->work_mode && REMOTE_WORK_MODE_FIQ))
	    && (!(gp_remote->work_mode && REMOTE_WORK_MODE_FIQ_RCMM))) {
		enable_irq(NEC_REMOTE_IRQ_NO);
	}

}

static void remote_repeat_sr(unsigned long data)
{
	struct remote *remote_data = (struct remote *)data;
	u32 status;
	u32 timer_period;
 
	status = am_remote_read_reg(AM_IR_DEC_STATUS);
	switch (status & REMOTE_HW_DECODER_STATUS_MASK) {
	case REMOTE_HW_DECODER_STATUS_OK:
		remote_send_key(remote_data->input, (remote_data->cur_keycode >> 16) & 0xff, 0);
		repeat_flag = 0;
		break;
	default: 
		am_remote_set_mask(AM_IR_DEC_REG1, 1);	//reset ir deocoder
		am_remote_clear_mask(AM_IR_DEC_REG1, 1);

		if (remote_data->repeat_tick != 0) {	//new key coming in.
			timer_period = jiffies + 10;	//timer peroid waiting for a stable state.
		} else {	//repeat key check
			if (remote_data->repeat_enable) {
				remote_send_key(remote_data->input, (remote_data->cur_keycode >> 16) & 0xff, 2);
			}
			timer_period = jiffies + msecs_to_jiffies(remote_data->repeat_peroid);
		}
		mod_timer(&remote_data->repeat_timer, timer_period);
		remote_data->repeat_tick = 0;
		break;
	}

}

static void remote_timer_sr(unsigned long data)
{
	struct remote *remote_data = (struct remote *)data;
	if (remote_data->work_mode == REMOTE_WORK_MODE_FIQ_RCMM) {
		if (remote_data->bit_count == 32) {
			remote_send_key(remote_data->input, 0x100 | (remote_data->cur_keycode >> (remote_data->bit_count - 8)), 1);
		} else {
			remote_send_key(remote_data->input, remote_data->cur_keycode >> (remote_data->bit_count - 8), 0);
		}
	} else if (remote_data->work_mode == REMOTE_WORK_MODE_RC5 || remote_data->work_mode == REMOTE_WORK_MODE_RC6) {
		remote_send_key(remote_data->input, remote_data->cur_keycode, 0);
	} else if (remote_data->work_mode == REMOTE_WORK_MODE_COMCAST) {
		remote_data->repeate_flag = 0;
		remote_send_key(remote_data->input, remote_data->last_keycode, 0);
	} else {
		remote_send_key(remote_data->input, (remote_data->cur_keycode >> 16) & 0xff, 0);
		repeat_flag = 0;
	}
	if (!(remote_data->work_mode & REMOTE_WORK_MODE_HW)) {
		remote_data->step = REMOTE_STATUS_WAIT;
	}
}

static irqreturn_t remote_interrupt(int irq, void *dev_id)
{
	/* disable keyboard interrupt and schedule for handling */
	//  input_dbg("===trigger one  remoteads interupt \r\n");
	tasklet_schedule(&tasklet);

	return IRQ_HANDLED;
}

static void remote_fiq_interrupt(void)
{
	if (gp_remote->work_mode == REMOTE_WORK_MODE_RC6) {
		remote_rc6_reprot_key((unsigned long)gp_remote);
	} else if (gp_remote->work_mode == REMOTE_WORK_MODE_RC5) {
		remote_rc5_reprot_key((unsigned long)gp_remote);
	} else {
		remote_sw_reprot_key((unsigned long)gp_remote);
	}
	WRITE_MPEG_REG(IRQ_CLR_REG(NEC_REMOTE_IRQ_NO), 1 << IRQ_BIT(NEC_REMOTE_IRQ_NO));
}

static inline int remote_hw_reprot_key(struct remote *remote_data)
{
	int key_index;
	unsigned int status, scan_code;
	static int last_scan_code, key_hold;
	static int last_custom_code;

	// 1        get  scan code
	scan_code = am_remote_read_reg(AM_IR_DEC_FRAME);
	status = am_remote_read_reg(AM_IR_DEC_STATUS);
	//printk("scan_code:%x\n", scan_code);
	//printk("status:%x\n", status);
	//printk("reg_0:%x\n", am_remote_read_reg(AM_IR_DEC_REG0));
	//printk("reg_1:%x\n", am_remote_read_reg(AM_IR_DEC_REG1));
	key_index = 0;
	key_hold = -1;
	if (scan_code) {	//key first press
		last_custom_code = scan_code & 0xffff;
		if (remote_data->custom_code != last_custom_code) {
			input_dbg("Wrong custom code is 0x%08x\n", scan_code);
			return -1;
		}
		//add for skyworth remote.
		if (remote_data->work_mode == REMOTE_TOSHIBA_HW) {	//we start  repeat timer for check repeat.
			if (remote_data->repeat_timer.expires > jiffies) {	//release last key.
				remote_send_key(remote_data->input, (remote_data->cur_keycode >> 16) & 0xff, 0);
				repeat_flag = 0;
			}
			remote_send_key(remote_data->input, (scan_code >> 16) & 0xff, 1);
			repeat_flag = 1;
			last_scan_code = scan_code;
			remote_data->cur_keycode = last_scan_code;
			remote_data->repeat_timer.data = (unsigned long)remote_data;
			//here repeat  delay is time interval from the first frame end to first repeat end.
			remote_data->repeat_tick = jiffies;
			mod_timer(&remote_data->repeat_timer, jiffies + msecs_to_jiffies(remote_data->repeat_delay));
			return 0;
		} else {
			if (remote_data->timer.expires > jiffies) {
				remote_send_key(remote_data->input, (remote_data->cur_keycode >> 16) & 0xff, 0);
				repeat_flag = 0;
			}
			remote_send_key(remote_data->input, (scan_code >> 16) & 0xff, 1);
			repeat_flag = 1;
			if (remote_data->repeat_enable) {
				remote_data->repeat_tick = jiffies + msecs_to_jiffies(remote_data->input->rep[REP_DELAY]);
			}
		}

	} else if (scan_code == 0 && status & 0x1) {	//repeate key
		scan_code = last_scan_code;
		if (remote_data->custom_code != last_custom_code) {
			return -1;
		}
#ifdef CONFIG_AML_HDMI_TX
#ifdef CONFIG_ARCH_MESON6
		//printk("last_scan_code:%x\n", last_scan_code);
		if((((scan_code >> 16) & 0xff) == 0x1a) && (!cec_repeat)) {
            extern int rc_long_press_pwr_key;
            rc_long_press_pwr_key = 1;
		    cec_repeat = 10;
		    mdelay(20);
		}
		if(((scan_code >> 16) & 0xff) == 0x1a)
 		    cec_repeat--;
#endif
#endif
		if (remote_data->repeat_enable) {
			if ((remote_data->repeat_tick < jiffies)&&(repeat_flag == 1)) {
				remote_send_key(remote_data->input, (scan_code >> 16) & 0xff, 2);
				remote_data->repeat_tick += msecs_to_jiffies(remote_data->input->rep[REP_PERIOD]);
			}
		} else {
			if (remote_data->timer.expires > jiffies) {
				mod_timer(&remote_data->timer, jiffies + msecs_to_jiffies(remote_data->release_delay));
			}
			return -1;
		}
	}
	last_scan_code = scan_code;
	remote_data->cur_keycode = last_scan_code;
	remote_data->timer.data = (unsigned long)remote_data;
	mod_timer(&remote_data->timer, jiffies + msecs_to_jiffies(remote_data->release_delay));

	return 0;
}

static void remote_tasklet(unsigned long data)
{
	struct remote *remote_data = (struct remote *)data;

	if (remote_data->work_mode & REMOTE_WORK_MODE_HW) {
		remote_hw_reprot_key(remote_data);
	} else {
		remote_sw_reprot_key(data);
	}
}

static ssize_t remote_log_buffer_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = 0;
	ret = sprintf(buf, "%s\n", remote_log_buf);
	//printk(remote_log_buf);
	remote_log_buf[0] = '\0';
	return ret;
}

static ssize_t remote_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", remote_enable);
}

//control var by sysfs .
static ssize_t remote_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int state;

	if (sscanf(buf, "%u", &state) != 1) {
		return -EINVAL;
	}

	if ((state != 1) && (state != 0)) {
		return -EINVAL;
	}

	mutex_lock(&remote_enable_mutex);
	if (state != remote_enable) {
		if (state) {
			enable_remote_irq();
		} else {
			disable_remote_irq();
		}
		remote_enable = state;
	}
	mutex_unlock(&remote_enable_mutex);

	return strnlen(buf, count);
}

static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR, remote_enable_show, remote_enable_store);
static DEVICE_ATTR(log_buffer, S_IRUGO | S_IWUSR, remote_log_buffer_show, NULL);

/*****************************************************************
**
** func : hardware init
**       in this function will do pin configuration and and initialize for hardware
**       decoder mode .
**
********************************************************************/
static int hardware_init(struct platform_device *pdev)
{
	//struct aml_remote_platdata *remote_pdata;
	unsigned int control_value, status, data_value;
	 struct pinctrl *p;
	//step 0: set pinmux to remote
	//remote_pdata = (struct aml_remote_platdata *)pdev->dev.platform_data;
	//if (!remote_pdata) {
	//	printk("Remote platform_data not found.\n");
	//	return -1;
	//}
	//if (remote_pdata->pinmux_setup)
	//	remote_pdata->pinmux_setup();
	p=devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(p))
		return -1;
	//step 1 :set reg IR_DEC_CONTROL
	control_value = 3 << 28 | (0xFA0 << 12) | 0x13;
	am_remote_write_reg(AM_IR_DEC_REG0, control_value);
	control_value = am_remote_read_reg(AM_IR_DEC_REG1);
	am_remote_write_reg(AM_IR_DEC_REG1, control_value | IR_CONTROL_HOLD_LAST_KEY);
	status = am_remote_read_reg(AM_IR_DEC_STATUS);
	data_value = am_remote_read_reg(AM_IR_DEC_FRAME);

		printk("Remote date_valye======%x,status == %x\n",data_value,status);
	//step 2 : request nec_remote irq  & enable it
	return request_irq(NEC_REMOTE_IRQ_NO, remote_interrupt, IRQF_SHARED, "keypad", (void *)remote_interrupt);
}

void remote_comcast_config(int baserate)
{
	// time_window_0:  bits of data to modulation
	// time_window_1 < value 0 range <= time_window_2
	// time_window_2 < value 1 range <= time_window_3
	// ...

	// value n pulse = 1.085*(195+699+n*126)/(baserate+1)
#if 0
	float basetime = 1.085*(195.0+699.0);
	float factor = 1.085*126.0;
	int i;
	
	gp_remote->time_window[0] = 4;
	for(i=0; i<=16; i++) {
		gp_remote->time_window[i+1] = (unsigned int)((basetime-0.5*factor+factor*i)/(1.0+baserate));
	//	input_dbg("time_window[%d]=%d,  %f.\n", i+1, gp_remote->time_window[i+1],  pulse);
	}	
#endif
	gp_remote->repeat_delay = 200;  
	if(baserate == 0x04) 
	{
		gp_remote->time_window[0] = 4;
		gp_remote->time_window[1] = 179;
		gp_remote->time_window[2] = 208;
		gp_remote->time_window[3] = 235;
		gp_remote->time_window[4] = 262;
		gp_remote->time_window[5] = 290;
		gp_remote->time_window[6] = 317;
		gp_remote->time_window[7] = 344;
		gp_remote->time_window[8] = 372;
		gp_remote->time_window[9] = 399;
		gp_remote->time_window[10] = 426;
		gp_remote->time_window[11] = 454;
		gp_remote->time_window[12] = 481;
		gp_remote->time_window[13] = 508;
		gp_remote->time_window[14] = 536;
		gp_remote->time_window[15] = 563;
		gp_remote->time_window[16] = 590;
		gp_remote->time_window[17] = 618;
	}
	else  //	if(baserate == 0x13) 
	{
		gp_remote->time_window[0] = 4;
		gp_remote->time_window[1] = 44;
		gp_remote->time_window[2] = 52;
		gp_remote->time_window[3] = 59;
		gp_remote->time_window[4] = 66;
		gp_remote->time_window[5] = 72;
		gp_remote->time_window[6] = 79;
		gp_remote->time_window[7] = 86;
		gp_remote->time_window[8] = 93;
		gp_remote->time_window[9] = 100;
		gp_remote->time_window[10] = 107;
		gp_remote->time_window[11] = 113;
		gp_remote->time_window[12] = 120;
		gp_remote->time_window[13] = 127;
		gp_remote->time_window[14] = 134;
		gp_remote->time_window[15] = 141;
		gp_remote->time_window[16] = 148;
		gp_remote->time_window[17] = 155;
	}
}

static int work_mode_config(unsigned int cur_mode)
{
	unsigned int control_value;
	static unsigned int last_mode = REMOTE_WORK_MODE_INV;
	struct irq_desc *desc = irq_to_desc(NEC_REMOTE_IRQ_NO);
	int ret;

	if (last_mode == cur_mode) {
		return -1;
	}
	if (cur_mode & REMOTE_WORK_MODE_HW) {
		control_value = 0xbe40;	//ignore  custom code .
		am_remote_write_reg(AM_IR_DEC_REG1, control_value | IR_CONTROL_HOLD_LAST_KEY);
	} else {
		control_value = 0x8578;
		am_remote_write_reg(AM_IR_DEC_REG1, control_value);
	}
	if(cur_mode == REMOTE_WORK_MODE_COMCAST) {
		control_value = am_remote_read_reg(AM_IR_DEC_REG0)&0xfff;
		remote_comcast_config(control_value);
	}
	
	switch (cur_mode & REMOTE_WORK_MODE_MASK) {
	case REMOTE_WORK_MODE_HW:
	case REMOTE_WORK_MODE_SW:
		if ((last_mode == REMOTE_WORK_MODE_FIQ)
		    || (last_mode == REMOTE_WORK_MODE_FIQ_RCMM)) {
			//disable fiq and enable common irq
			unregister_fiq_bridge_handle(&gp_remote->fiq_handle_item);
			free_fiq(NEC_REMOTE_IRQ_NO, &remote_fiq_interrupt);
			ret = request_irq(NEC_REMOTE_IRQ_NO, remote_interrupt, IRQF_SHARED, "keypad", (void *)remote_interrupt);
			if (ret < 0) {
				printk(KERN_ERR "Remote: request_irq failed, ret=%d.\n", ret);
				return ret;
			}
		}
		break;
	case REMOTE_WORK_MODE_FIQ:
	case REMOTE_WORK_MODE_FIQ_RCMM:
		if ((last_mode == REMOTE_WORK_MODE_FIQ)
		    || (last_mode == REMOTE_WORK_MODE_FIQ_RCMM)) {
			break;
		}
		//disable common irq and enable fiq.
		free_irq(NEC_REMOTE_IRQ_NO, remote_interrupt);
		if (cur_mode == REMOTE_WORK_MODE_RC6) {
			gp_remote->fiq_handle_item.handle = remote_rc6_bridge_isr;
		} else if (cur_mode == REMOTE_WORK_MODE_RC5) {
			gp_remote->fiq_handle_item.handle = remote_rc5_bridge_isr;
		} else {
			gp_remote->fiq_handle_item.handle = remote_bridge_isr;
		}
		gp_remote->fiq_handle_item.key = (u32) gp_remote;
		gp_remote->fiq_handle_item.name = "remote_bridge";
		register_fiq_bridge_handle(&gp_remote->fiq_handle_item);
		//workaround to fix fiq mechanism bug.
		desc->depth++;
		request_fiq(NEC_REMOTE_IRQ_NO, &remote_fiq_interrupt);
		break;
	}
	last_mode = cur_mode;
	//add for skyworth remote
	if (cur_mode == REMOTE_TOSHIBA_HW) {
		setup_timer(&gp_remote->repeat_timer, remote_repeat_sr, 0);
	} else {
		del_timer(&gp_remote->repeat_timer);
	}
	return 0;
}

static int remote_config_open(struct inode *inode, struct file *file)
{
	file->private_data = gp_remote;
	return 0;
}

static long remote_config_ioctl(struct file *filp, unsigned int cmd, unsigned long args)
{
	struct remote *remote = (struct remote *)filp->private_data;
	void __user *argp = (void __user *)args;
	unsigned int val, i;
	unsigned int ret;

	if (args) {
		ret = copy_from_user(&val, argp, sizeof(unsigned long));
	}
	mutex_lock(&remote_file_mutex);
	//cmd input
	switch (cmd) {
		// 1 set part
	case REMOTE_IOC_RESET_KEY_MAPPING:
		for (i = 0; i < ARRAY_SIZE(key_map); i++) {
			key_map[i] = KEY_RESERVED;
		}
		for (i = 0; i < ARRAY_SIZE(mouse_map); i++) {
			mouse_map[i] = 0xffff;
		}
		break;
	case REMOTE_IOC_SET_KEY_MAPPING:
		if ((val >> 16) >= ARRAY_SIZE(key_map)) {
			mutex_unlock(&remote_file_mutex);
			return -1;
		}
		key_map[val >> 16] = val & 0xffff;
		break;
	case REMOTE_IOC_SET_MOUSE_MAPPING:
		if ((val >> 16) >= ARRAY_SIZE(mouse_map)) {
			mutex_unlock(&remote_file_mutex);
			return -1;
		}
		mouse_map[val >> 16] = val & 0xff;
		break;
	case REMOTE_IOC_SET_REPEAT_DELAY:
		ret = copy_from_user(&remote->repeat_delay, argp, sizeof(long));
		break;
	case REMOTE_IOC_SET_REPEAT_PERIOD:
		ret = copy_from_user(&remote->repeat_peroid, argp, sizeof(long));
		break;
	case REMOTE_IOC_SET_REPEAT_ENABLE:
		ret = copy_from_user(&remote->repeat_enable, argp, sizeof(long));
		break;
	case REMOTE_IOC_SET_DEBUG_ENABLE:
		ret = copy_from_user(&remote->debug_enable, argp, sizeof(long));
		break;
	case REMOTE_IOC_SET_MODE:
		ret = copy_from_user(&remote->work_mode, argp, sizeof(long));
		break;
	case REMOTE_IOC_SET_BIT_COUNT:
		ret = copy_from_user(&remote->bit_count, argp, sizeof(long));
		if (remote->bit_count > 32) {
			remote->bit_count = 32;
		}
		break;
	case REMOTE_IOC_SET_CUSTOMCODE:
		ret = copy_from_user(&remote->custom_code, argp, sizeof(long));
		break;
	case REMOTE_IOC_SET_REG_BASE_GEN:
		am_remote_write_reg(AM_IR_DEC_REG0, val);
		if(gp_remote->work_mode == REMOTE_WORK_MODE_COMCAST)
			remote_comcast_config(val&0xfff);
		break;
	case REMOTE_IOC_SET_REG_CONTROL:
		am_remote_write_reg(AM_IR_DEC_REG1, val);
		break;
	case REMOTE_IOC_SET_REG_LEADER_ACT:
		am_remote_write_reg(AM_IR_DEC_LDR_ACTIVE, val);
		break;
	case REMOTE_IOC_SET_REG_LEADER_IDLE:
		am_remote_write_reg(AM_IR_DEC_LDR_IDLE, val);
		break;
	case REMOTE_IOC_SET_REG_REPEAT_LEADER:
		am_remote_write_reg(AM_IR_DEC_LDR_REPEAT, val);
		break;
	case REMOTE_IOC_SET_REG_BIT0_TIME:
		am_remote_write_reg(AM_IR_DEC_BIT_0, val);
		break;
	case REMOTE_IOC_SET_RELEASE_DELAY:
		ret = copy_from_user(&remote->release_delay, argp, sizeof(long));
		break;
		//SW
	case REMOTE_IOC_SET_TW_LEADER_ACT:
		remote->time_window[0] = val & 0xffff;
		remote->time_window[1] = (val >> 16) & 0xffff;
		break;
	case REMOTE_IOC_SET_TW_BIT0_TIME:
		remote->time_window[2] = val & 0xffff;
		remote->time_window[3] = (val >> 16) & 0xffff;
		break;
	case REMOTE_IOC_SET_TW_BIT1_TIME:
		remote->time_window[4] = val & 0xffff;
		remote->time_window[5] = (val >> 16) & 0xffff;
		break;
	case REMOTE_IOC_SET_TW_REPEATE_LEADER:
		remote->time_window[6] = val & 0xffff;
		remote->time_window[7] = (val >> 16) & 0xffff;
		break;
	case REMOTE_IOC_SET_TW_BIT2_TIME:
		remote->time_window[8] = val & 0xffff;
		remote->time_window[9] = (val >> 16) & 0xffff;
		break;
	case REMOTE_IOC_SET_TW_BIT3_TIME:
		remote->time_window[10] = val & 0xffff;
		remote->time_window[11] = (val >> 16) & 0xffff;
		break;
		// 2 get  part
	case REMOTE_IOC_GET_REG_BASE_GEN:
		val = am_remote_read_reg(AM_IR_DEC_REG0);
		break;
	case REMOTE_IOC_GET_REG_CONTROL:
		val = am_remote_read_reg(AM_IR_DEC_REG1);
		break;
	case REMOTE_IOC_GET_REG_LEADER_ACT:
		val = am_remote_read_reg(AM_IR_DEC_LDR_ACTIVE);
		break;
	case REMOTE_IOC_GET_REG_LEADER_IDLE:
		val = am_remote_read_reg(AM_IR_DEC_LDR_IDLE);
		break;
	case REMOTE_IOC_GET_REG_REPEAT_LEADER:
		val = am_remote_read_reg(AM_IR_DEC_LDR_REPEAT);
		break;
	case REMOTE_IOC_GET_REG_BIT0_TIME:
		val = am_remote_read_reg(AM_IR_DEC_BIT_0);
		break;
	case REMOTE_IOC_GET_REG_FRAME_DATA:
		val = am_remote_read_reg(AM_IR_DEC_FRAME);
		break;
	case REMOTE_IOC_GET_REG_FRAME_STATUS:
		val = am_remote_read_reg(AM_IR_DEC_STATUS);
		break;
		//sw
	case REMOTE_IOC_GET_TW_LEADER_ACT:
		val = remote->time_window[0] | (remote->time_window[1] << 16);
		break;
	case REMOTE_IOC_GET_TW_BIT0_TIME:
		val = remote->time_window[2] | (remote->time_window[3] << 16);
		break;
	case REMOTE_IOC_GET_TW_BIT1_TIME:
		val = remote->time_window[4] | (remote->time_window[5] << 16);
		break;
	case REMOTE_IOC_GET_TW_REPEATE_LEADER:
		val = remote->time_window[6] | (remote->time_window[7] << 16);
		break;

	case REMOTE_IOC_SET_FN_KEY_SCANCODE:
        	FN_KEY_SCANCODE = val;
                break;

        case REMOTE_IOC_SET_LEFT_KEY_SCANCODE:
                LEFT_KEY_SCANCODE = val;
                break;

        case REMOTE_IOC_SET_RIGHT_KEY_SCANCODE:
                RIGHT_KEY_SCANCODE = val;
                break;

        case REMOTE_IOC_SET_UP_KEY_SCANCODE:
                UP_KEY_SCANCODE = val;
                break;

        case REMOTE_IOC_SET_DOWN_KEY_SCANCODE:
                DOWN_KEY_SCANCODE = val;
                break;

        case REMOTE_IOC_SET_OK_KEY_SCANCODE:
                OK_KEY_SCANCODE = val;
                break;

        case REMOTE_IOC_SET_PAGEUP_KEY_SCANCODE:
                PAGEUP_KEY_SCANCODE = val;
                break;

        case REMOTE_IOC_SET_PAGEDOWN_KEY_SCANCODE:
                PAGEDOWN_KEY_SCANCODE = val;
                break;
	}
	//output result
	switch (cmd) {
	case REMOTE_IOC_SET_REPEAT_ENABLE:
		if (remote->repeat_enable) {
			remote->input->rep[REP_DELAY] = remote->repeat_delay;
			remote->input->rep[REP_PERIOD] = remote->repeat_peroid;
		} else {
			remote->input->rep[REP_DELAY] = 0xffffffff;
			remote->input->rep[REP_PERIOD] = 0xffffffff;
		}
		break;
	case REMOTE_IOC_SET_MODE:
		work_mode_config(remote->work_mode);
		break;
	case REMOTE_IOC_GET_REG_BASE_GEN:
	case REMOTE_IOC_GET_REG_CONTROL:
	case REMOTE_IOC_GET_REG_LEADER_ACT:
	case REMOTE_IOC_GET_REG_LEADER_IDLE:
	case REMOTE_IOC_GET_REG_REPEAT_LEADER:
	case REMOTE_IOC_GET_REG_BIT0_TIME:
	case REMOTE_IOC_GET_REG_FRAME_DATA:
	case REMOTE_IOC_GET_REG_FRAME_STATUS:
	case REMOTE_IOC_GET_TW_LEADER_ACT:
	case REMOTE_IOC_GET_TW_BIT0_TIME:
	case REMOTE_IOC_GET_TW_BIT1_TIME:
	case REMOTE_IOC_GET_TW_REPEATE_LEADER:
		ret = copy_to_user(argp, &val, sizeof(long));
		break;
	}
	mutex_unlock(&remote_file_mutex);
	return 0;
}

static int remote_config_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static const struct file_operations remote_fops = {
	.owner = THIS_MODULE,
	.open = remote_config_open,
	.unlocked_ioctl = remote_config_ioctl,
	.release = remote_config_release,
};

static int register_remote_dev(struct remote *remote)
{
	int ret = 0;
	strcpy(remote->config_name, "amremote");
	ret = register_chrdev(0, remote->config_name, &remote_fops);
	if (ret <= 0) {
		printk("register char dev tv error\r\n");
		return ret;
	}
	remote->config_major = ret;
	printk("remote config major:%d\r\n", ret);
	remote->config_class = class_create(THIS_MODULE, remote->config_name);
	remote->config_dev = device_create(remote->config_class, NULL, MKDEV(remote->config_major, 0), NULL, remote->config_name);
	return ret;
}

static const struct of_device_id remote_dt_match[]={
	{	.compatible 	= "amlogic,aml_remote",
	},
	{},
};

static int remote_probe(struct platform_device *pdev)
{
	struct remote *remote;
	struct input_dev *input_dev;
	unsigned int ao_baseaddr;
	int i, ret;

	 if (!pdev->dev.of_node) {
		printk("aml_remote: pdev->dev.of_node == NULL!\n");
		return -1;
	}
	ret = of_property_read_u32(pdev->dev.of_node,"ao_baseaddr",&ao_baseaddr);
	if(ret){
		printk("don't find  match ao_baseaddr\n");
		return -1;
	}

	g_remote_base = ao_baseaddr;
	printk("Remote platform_data g_remote_base=%x\n",ao_baseaddr);
	
	remote_enable = 1;
	remote = kzalloc(sizeof(struct remote), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!remote || !input_dev) {
		kfree(remote);
		input_free_device(input_dev);
		return -ENOMEM;
	}
	gp_remote = remote;
	remote->debug_enable = 0;

	input_dbg = remote_printk;
	platform_set_drvdata(pdev, remote);
	remote->work_mode = REMOTE_NEC_HW;
	remote->input = input_dev;
	remote->release_delay = KEY_RELEASE_DELAY;
	remote->custom_code = 0xff00;
	remote->bit_count = 32;	//default 32bit for sw mode.
	remote->last_jiffies = 0xffffffff;
	remote->last_pulse_width = 0;

	remote->step = REMOTE_STATUS_WAIT;
	remote->time_window[0] = 0x1;
	remote->time_window[1] = 0x1;
	remote->time_window[2] = 0x1;
	remote->time_window[3] = 0x1;
	remote->time_window[4] = 0x1;
	remote->time_window[5] = 0x1;
	remote->time_window[6] = 0x1;
	remote->time_window[7] = 0x1;

	/* Disable the interrupt for the MPUIO keyboard */
	for (i = 0; i < ARRAY_SIZE(key_map); i++) {
		key_map[i] = KEY_RESERVED;
	}
	for (i = 0; i < ARRAY_SIZE(mouse_map); i++) {
		mouse_map[i] = 0xffff;
	}
	remote->repeat_delay = 250;
	remote->repeat_peroid = 33;

	/* get the irq and init timer */
	input_dbg("set drvdata completed\r\n");
	tasklet_enable(&tasklet);
	tasklet.data = (unsigned long)remote;
	setup_timer(&remote->timer, remote_timer_sr, 0);

	ret = device_create_file(&pdev->dev, &dev_attr_enable);
	if (ret < 0) {
		goto err1;
	}
	ret = device_create_file(&pdev->dev, &dev_attr_log_buffer);
	if (ret < 0) {
		device_remove_file(&pdev->dev, &dev_attr_enable);
		goto err1;
	}

	input_dbg("device_create_file completed \r\n");
	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
	input_dev->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_RIGHT) | BIT_MASK(BTN_MIDDLE);
	input_dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y) | BIT_MASK(REL_WHEEL);
	input_dev->keybit[BIT_WORD(BTN_MOUSE)] |= BIT_MASK(BTN_SIDE) | BIT_MASK(BTN_EXTRA);
	for (i = 0; i < KEY_MAX; i++) {
		set_bit(i, input_dev->keybit);
	}

	//clear_bit(0,input_dev->keybit);
	input_dev->name = "aml_keypad";
	input_dev->phys = "keypad/input0";
	//input_dev->cdev.dev = &pdev->dev;
	//input_dev->private = remote;
	input_dev->dev.parent = &pdev->dev;

	input_dev->id.bustype = BUS_ISA;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;
	remote->repeat_enable = 0;

	input_dev->rep[REP_DELAY] = 0xffffffff;
	input_dev->rep[REP_PERIOD] = 0xffffffff;

	input_dev->keycodesize = sizeof(unsigned short);
	input_dev->keycodemax = 0x1ff;

	ret = input_register_device(remote->input);
	if (ret < 0) {
		printk(KERN_ERR "Unable to register keypad input device\n");
		goto err2;
	}
	input_dbg("input_register_device completed \r\n");
	if (hardware_init(pdev)) {
		goto err3;
	}

	register_remote_dev(gp_remote);
	remote_log_buf = (char *)__get_free_pages(GFP_KERNEL, REMOTE_LOG_BUF_ORDER);
	remote_log_buf[0] = '\0';
	printk("physical address:0x%x\n", (unsigned int)virt_to_phys(remote_log_buf));
	return 0;
err3:
	//     free_irq(NEC_REMOTE_IRQ_NO,remote_interrupt);
	input_unregister_device(remote->input);
	input_dev = NULL;
err2:
	device_remove_file(&pdev->dev, &dev_attr_enable);
	device_remove_file(&pdev->dev, &dev_attr_log_buffer);
err1:

	kfree(remote);
	input_free_device(input_dev);

	return -EINVAL;
}

static int remote_remove(struct platform_device *pdev)
{
	struct remote *remote = platform_get_drvdata(pdev);

	/* disable keypad interrupt handling */
	input_dbg("remove remoteads \r\n");
	tasklet_disable(&tasklet);
	tasklet_kill(&tasklet);

	/* unregister everything */
	input_unregister_device(remote->input);
	free_pages((unsigned long)remote_log_buf, REMOTE_LOG_BUF_ORDER);
	device_remove_file(&pdev->dev, &dev_attr_enable);
	device_remove_file(&pdev->dev, &dev_attr_log_buffer);
	if ((remote->work_mode & REMOTE_WORK_MODE_FIQ)
	    || (remote->work_mode & REMOTE_WORK_MODE_FIQ_RCMM)) {
		free_fiq(NEC_REMOTE_IRQ_NO, &remote_fiq_interrupt);
		free_irq(BRIDGE_IRQ, gp_remote);
	} else {
		free_irq(NEC_REMOTE_IRQ_NO, remote_interrupt);
	}
	input_free_device(remote->input);

	unregister_chrdev(remote->config_major, remote->config_name);
	if (remote->config_class) {
		if (remote->config_dev) {
			device_destroy(remote->config_class, MKDEV(remote->config_major, 0));
		}
		class_destroy(remote->config_class);
	}

	kfree(remote);
	gp_remote = NULL;
	return 0;
}
static int remote_resume(struct platform_device *pdev)
{
	printk("resume_remote make sure uboot interrupt clear\n");
	am_remote_read_reg(AM_IR_DEC_FRAME);

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    /* 0x1234abcd : woke by power button. set by uboot
     * 0x12345678 : woke by alarm. set in pm.c
     */
    if (READ_AOBUS_REG(AO_RTI_STATUS_REG2) == 0x1234abcd) {
        // power button, not alarm
//        remote_send_key(gp_remote->input, 1);
//        remote_send_key(gp_remote->input, 0);

		input_event(gp_remote->input, EV_KEY, KEY_POWER, 1);
		input_sync(gp_remote->input);
		input_event(gp_remote->input, EV_KEY, KEY_POWER, 0);
		input_sync(gp_remote->input);
		
		//aml_write_reg32(P_AO_RTC_ADDR0, (aml_read_reg32(P_AO_RTC_ADDR0) | (0x0000f000)));
		WRITE_AOBUS_REG(AO_RTI_STATUS_REG2, 0);
    }
#endif
	
	return 0;
}
static struct platform_driver remote_driver = {
	.probe = remote_probe,
	.remove = remote_remove,
	.suspend = NULL,
	.resume = remote_resume,
	.driver = {
		.name = "meson-remote",
		.of_match_table = remote_dt_match,	
	},
};

static int __init remote_init(void)
{
	printk(KERN_INFO "Remote Driver\n");

	return platform_driver_register(&remote_driver);
}

static void __exit remote_exit(void)
{
	printk(KERN_INFO "Remote exit \n");
	platform_driver_unregister(&remote_driver);
}

module_init(remote_init);
module_exit(remote_exit);

MODULE_AUTHOR("jianfeng_wang");
MODULE_DESCRIPTION("Remote Driver");
MODULE_LICENSE("GPL");
