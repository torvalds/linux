/*
 * linux/drivers/input/irremote/sw_remote_kbd.c
 *
 * Keypad Driver
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
#include "am_remote.h"

extern char *remote_log_buf;

static int dbg_printk(const char *fmt, ...)
{
	char buf[100];
	va_list args;

	va_start(args, fmt);
	vscnprintf(buf, 100, fmt, args);
	if (strlen(remote_log_buf) + (strlen(buf) + 64) > REMOTE_LOG_BUF_LEN) {
		remote_log_buf[0] = '\0';
	}
	strcat(remote_log_buf, buf);
	va_end(args);
	return 0;
}

int checkKeyCode(unsigned int key, unsigned int bits) 
{
	int checksum = 0;
	unsigned int base = (1<<bits);

	while(key != 0) {
		checksum += key&(base-1);
		key = key >> bits;
//		printk("checksum=%x, key=%x.\n", checksum, key);
	}

	return checksum%base;
}

static int get_pulse_width(unsigned long data)
{
	struct remote *remote_data = (struct remote *)data;
	int pulse_width;
	const char *state;

	pulse_width = (am_remote_read_reg(AM_IR_DEC_REG1) & 0x1FFF0000) >> 16;
	state = remote_data->step == REMOTE_STATUS_WAIT ? "wait" :
	        remote_data->step == REMOTE_STATUS_LEADER ? "leader" :
	        remote_data->step == REMOTE_STATUS_DATA ? "data" :
	        remote_data->step == REMOTE_STATUS_SYNC ? "sync" : NULL;
	dbg_printk("%02d:pulse_wdith:%d==>%s\r\n",
	           remote_data->bit_count - remote_data->bit_num, pulse_width, state);
	//sometimes we found remote  pulse width==0.        in order to sync machine state we modify it .
	if (pulse_width == 0) {
		switch (remote_data->step) {
		case REMOTE_STATUS_LEADER:
			pulse_width = remote_data->time_window[0] + 1;
			break;
		case REMOTE_STATUS_DATA:
			pulse_width = remote_data->time_window[2] + 1;
			break;
		}
	}
	return pulse_width;
}

static inline void kbd_software_mode_remote_wait(unsigned long data)
{
//	unsigned short pulse_width;
	struct remote *remote_data = (struct remote *)data;

//	pulse_width = get_pulse_width(data);

	if(remote_data->work_mode == REMOTE_WORK_MODE_COMCAST) {
		remote_data->step = REMOTE_STATUS_DATA;
		remote_data->send_data = 1;
	}
	else
		remote_data->step = REMOTE_STATUS_LEADER;
	remote_data->cur_keycode = 0;
	remote_data->bit_num = remote_data->bit_count;
}

static inline void kbd_software_mode_remote_leader(unsigned long data)
{
	unsigned short pulse_width;
	struct remote *remote_data = (struct remote *)data;

	if(remote_data->work_mode == REMOTE_WORK_MODE_COMCAST) {
		remote_data->step = REMOTE_STATUS_DATA;
		return;
	}
	pulse_width = get_pulse_width(data);
	if ((pulse_width > remote_data->time_window[0])
	    && (pulse_width < remote_data->time_window[1])) {
		remote_data->step = REMOTE_STATUS_DATA;
	} else {
		remote_data->step = REMOTE_STATUS_WAIT;
	}

	remote_data->cur_keycode = 0;
	remote_data->bit_num = remote_data->bit_count;
}

static inline void kbd_software_mode_remote_send_key(unsigned long data)
{
	struct remote *remote_data = (struct remote *)data;
	unsigned int reort_key_code = remote_data->cur_keycode >> 16 & 0xffff;

	remote_data->step = REMOTE_STATUS_SYNC;
	if (remote_data->repeate_flag) {
		if (remote_data->custom_code != (remote_data->cur_keycode & 0xffff)) {
			return;
		}
		if (((reort_key_code & 0xff) ^ (reort_key_code >> 8 & 0xff)) !=
		    0xff) {
			return;
		}
		if (remote_data->repeat_tick < jiffies) {
			remote_send_key(remote_data->input,
			                (remote_data->cur_keycode >> 16) & 0xff, 2);
			remote_data->repeat_tick +=
			    msecs_to_jiffies(remote_data->input->rep[REP_PERIOD]);
		}
	} else {
		switch (remote_data->work_mode) {
		case REMOTE_WORK_MODE_FIQ_RCMM:
			if (remote_data->custom_code !=
			    (remote_data->cur_keycode & 0xfff)) {
				input_dbg("Wrong custom code is 0x%08x\n",
				          remote_data->cur_keycode);
				return;
			}
			if (remote_data->bit_count == 32)
				remote_send_key(remote_data->input,
				                0x100 | (remote_data->cur_keycode >>
				                         (remote_data->bit_count - 8)),
				                1);
			else
				remote_send_key(remote_data->input,
				                remote_data->
				                cur_keycode >> (remote_data->bit_count -
				                                8), 1);
			break;
		default:
			if (remote_data->custom_code !=
			    (remote_data->cur_keycode & 0xffff)) {
				input_dbg("Wrong custom code is 0x%08x\n",
				          remote_data->cur_keycode);
				return;
			}
			if (((reort_key_code & 0xff) ^
			     (reort_key_code >> 8 & 0xff)) == 0xff)
				remote_send_key(remote_data->input,
				                (remote_data->cur_keycode >> 16) & 0xff,
				                1);
		}
		if (remote_data->repeat_enable)
			remote_data->repeat_tick =
			    jiffies +
			    msecs_to_jiffies(remote_data->input->rep[REP_DELAY]);
	}
}

static inline void kbd_software_mode_remote_data(unsigned long data)
{
	unsigned short pulse_width;
	struct remote *remote_data = (struct remote *)data;

	pulse_width = get_pulse_width(data);
	remote_data->step = REMOTE_STATUS_DATA;

	switch (remote_data->work_mode) {
	case REMOTE_WORK_MODE_SW:
	case REMOTE_WORK_MODE_FIQ:
		if ((pulse_width > remote_data->time_window[2])
		    && (pulse_width < remote_data->time_window[3])) {
			remote_data->bit_num--;
		} else if ((pulse_width > remote_data->time_window[4])
		           && (pulse_width < remote_data->time_window[5])) {
			remote_data->cur_keycode |=
			    1 << (remote_data->bit_count - remote_data->bit_num);
			remote_data->bit_num--;
		} else {
			remote_data->step = REMOTE_STATUS_WAIT;
		}
		if (remote_data->bit_num == 0) {
			remote_data->repeate_flag = 0;
			remote_data->send_data = 1;
			if (remote_data->work_mode == REMOTE_WORK_MODE_FIQ)
				fiq_bridge_pulse_trigger
				(&remote_data->fiq_handle_item);
			else {
				remote_bridge_isr(0, remote_data);
			}
		}
		break;
	case REMOTE_WORK_MODE_FIQ_RCMM:
		if ((pulse_width > remote_data->time_window[2])
		    && (pulse_width < remote_data->time_window[3])) {
			if ((remote_data->bit_num == 12) && (remote_data->bit_count == 24)) {	/*sub mode is remote control */
				remote_data->bit_count += 8;
				remote_data->bit_num += 8;
			}
			remote_data->bit_num -= 2;
		} else if ((pulse_width > remote_data->time_window[4])
		           && (pulse_width < remote_data->time_window[5])) {
			remote_data->cur_keycode |=
			    1 << (remote_data->bit_count - remote_data->bit_num);
			remote_data->bit_num -= 2;
		} else if ((pulse_width > remote_data->time_window[8])
		           && (pulse_width < remote_data->time_window[9])) {
			remote_data->cur_keycode |=
			    2 << (remote_data->bit_count - remote_data->bit_num);
			if ((remote_data->bit_num == 20) && (remote_data->bit_count == 32)) {	/*sub mode is keyboard */
				remote_data->bit_count -= 8;
				remote_data->bit_num -= 8;
			}
			remote_data->bit_num -= 2;
		} else if ((pulse_width > remote_data->time_window[10])
		           && (pulse_width < remote_data->time_window[11])) {
			remote_data->cur_keycode |=
			    3 << (remote_data->bit_count - remote_data->bit_num);
			remote_data->bit_num -= 2;
		} else {
			remote_data->step = REMOTE_STATUS_WAIT;
		}
		if (remote_data->bit_num == 0) {
			remote_data->repeate_flag = 0;
			remote_data->send_data = 1;
			fiq_bridge_pulse_trigger(&remote_data->fiq_handle_item);
		}
		break;
	case REMOTE_WORK_MODE_COMCAST: 
	{
		int i;
		int base = (1<<remote_data->time_window[0]);

		for(i=0; i<base; i++) {
			if(pulse_width <= remote_data->time_window[2+i])
			{
				remote_data->bit_num -= remote_data->time_window[0];
				remote_data->cur_keycode |= (i << remote_data->bit_num);
				break;
			}
		}
		if(i >= base) {
			remote_data->bit_num = 0;
		}
		if (remote_data->bit_num == 0) {
			if(remote_data->send_data == 1)  {
				if(i >= base) 
					remote_data->step = REMOTE_STATUS_DATA;
				else
					remote_data->step = REMOTE_STATUS_LEADER;
				remote_data->bit_num = remote_data->bit_count;
				remote_data->register_data = remote_data->cur_keycode;
				remote_data->cur_keycode = 0;
				remote_data->send_data = 0;
			}
			else {
				int valid = checkKeyCode(remote_data->cur_keycode, remote_data->time_window[0]);
				unsigned int cur_jiffies = jiffies;
				unsigned int cur_keycode = (remote_data->cur_keycode&0xff00)>>8;
				remote_data->step = REMOTE_STATUS_WAIT;
				if(valid || ((remote_data->register_data&0xffff) != remote_data->custom_code)) {
					input_dbg("***invalid code=%08x-%08x, customer=%08x.\n", remote_data->register_data, remote_data->cur_keycode, remote_data->custom_code);
					break;
				}
				// press A, press B before A release time out, so we need to send A release
				if((remote_data->timer.expires > cur_jiffies) && (remote_data->last_keycode != cur_keycode)) {
					remote_send_key(remote_data->input, remote_data->last_keycode, 0);
				}
				// set repeate key delay as key is too sensitive
				if((remote_data->repeat_tick > cur_jiffies) && (remote_data->last_keycode == cur_keycode))
					break;
				if(remote_data->repeate_flag && (remote_data->last_keycode == cur_keycode))
					remote_send_key(remote_data->input, cur_keycode, 2);
				else
					remote_send_key(remote_data->input, cur_keycode, 1);
				remote_data->repeate_flag = 1;
				remote_data->last_keycode = cur_keycode;
				remote_data->repeat_tick = cur_jiffies+msecs_to_jiffies(remote_data->repeat_delay);
				remote_data->timer.data = (unsigned long)remote_data;
				mod_timer(&remote_data->timer, cur_jiffies + msecs_to_jiffies(remote_data->release_delay));
			}
		}
		break;
	}
	}

}

static inline void kbd_software_mode_remote_sync(unsigned long data)
{
	unsigned short pulse_width;
	struct remote *remote_data = (struct remote *)data;

	pulse_width = get_pulse_width(data);
	if ((pulse_width > remote_data->time_window[6])
	    && (pulse_width < remote_data->time_window[7])) {
		remote_data->repeate_flag = 1;
		if (remote_data->repeat_enable) {
			remote_data->send_data = 1;
		} else {
			remote_data->step = REMOTE_STATUS_SYNC;
			return;
		}
	}
	remote_data->step = REMOTE_STATUS_SYNC;
	if ((remote_data->work_mode == REMOTE_WORK_MODE_FIQ)
	    || (remote_data->work_mode == REMOTE_WORK_MODE_FIQ_RCMM)) {
		fiq_bridge_pulse_trigger(&remote_data->fiq_handle_item);
	} else {
		remote_bridge_isr(0, remote_data);
	}
}

void remote_sw_reprot_key(unsigned long data)
{
	struct remote *remote_data = (struct remote *)data;
	int current_jiffies = jiffies;

	if (((current_jiffies - remote_data->last_jiffies) > 20)
	    && (remote_data->step <= REMOTE_STATUS_SYNC)) {
		remote_data->step = REMOTE_STATUS_WAIT;
	}
	remote_data->last_jiffies = current_jiffies;	//ignore a little msecs
	switch (remote_data->step) {
	case REMOTE_STATUS_WAIT:
		kbd_software_mode_remote_wait(data);
		break;
	case REMOTE_STATUS_LEADER:
		kbd_software_mode_remote_leader(data);
		break;
	case REMOTE_STATUS_DATA:
		kbd_software_mode_remote_data(data);
		break;
	case REMOTE_STATUS_SYNC:
		kbd_software_mode_remote_sync(data);
		break;
	default:
		break;
	}
}

irqreturn_t remote_bridge_isr(int irq, void *dev_id)
{
	struct remote *remote_data = (struct remote *)dev_id;

	if (remote_data->send_data) {	//report key
		kbd_software_mode_remote_send_key((unsigned long)remote_data);
		remote_data->send_data = 0;
	}
	remote_data->timer.data = (unsigned long)remote_data;
	mod_timer(&remote_data->timer,
	          jiffies + msecs_to_jiffies(remote_data->release_delay));
	return IRQ_HANDLED;
}
