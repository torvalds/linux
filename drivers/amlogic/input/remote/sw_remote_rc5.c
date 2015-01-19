/*
 * linux/drivers/input/irremote/sw_remote_rc5.c
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
 * author :   jinlin xia
 */
/*
 * !!caution: if you use remote ,you should disable card1 used for  ata_enable pin.
 *
 */

/********remote.conf**************
work_mode        =0x32
reg_control      =0x8574
reg_base_gen     =0
tw_bit0          =0x00398x035a
bit_count        =6
repeat_enable    =0
release_delay    =180
debug_enable     =0

*********************************/

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

//unit T=888.889us
#define RC5_UNIT_T 889

#define RC5_STATUS_START          201
#define RC5_STATUS_INDICATION     202
#define RC5_STATUS_CONTROL        203
#define RC5_STATUS_SYSTEM         204
#define RC5_STATUS_COMMAND        205
#define RC5_STATUS_SYNC           206

extern char *remote_log_buf;

typedef struct {
	unsigned short time;
	unsigned char level;	//0-low level 1-high level
} win_time_t;

static win_time_t left_window = {.time = 0, .level = 1 };

static int sub_status;
static int bit_0_1;

static int system_field;

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

//clear sub status and set new status
static inline void goto_status(unsigned long data, int status)
{
	struct remote *remote_data = (struct remote *)data;
	sub_status = 0;
	remote_data->step = status;
}

static inline int rc5_bit1_high(win_time_t * win)
{
	if (win->level == 1 && win->time == 1) {
		left_window.time = 0;
		return 0;
	} else {
		return -1;
	}
}

static inline int rc5_bit1_low(win_time_t * win)
{
	if (win->level == 0 && win->time >= 1) {
		left_window.time = win->time - 1;
		return 0;
	} else {
		return -1;
	}
}

static inline int rc5_bit0_low(win_time_t * win)
{
	if (win->level == 0 && win->time == 1) {
		left_window.time = 0;
		return 0;
	} else {
		return -1;
	}
}

static inline int rc5_bit0_high(win_time_t * win)
{
	if (win->level == 1 && win->time >= 1) {
		left_window.time = win->time - 1;
		return 0;
	} else {
		return -1;
	}
}

/*
    check the pulse is the first half of 0 bit or 1 bit
    return: 0-----0 bit
            1-----1 bit
            -1----invalid pulse
*/
static inline int check_start_level(win_time_t * win)
{
	if (rc5_bit0_low(win) == 0) {
		return 0;
	}

	if (rc5_bit1_high(win) == 0) {
		return 1;
	}

	return -1;
}

//return width which base rate is RC5_UNIT_T
static inline int get_pulse_width(unsigned long data)
{
	struct remote *remote_data = (struct remote *)data;
	int pulse_width;
	const char *state;
	int num;
	int ret;
	unsigned int range[2] =
	{ remote_data->time_window[2], remote_data->time_window[3] };

	pulse_width = (am_remote_read_reg(AM_IR_DEC_REG1) & 0x1FFF0000) >> 16;
	num = pulse_width / RC5_UNIT_T;
	if (pulse_width > num * range[0] && pulse_width < num * range[1]) {
		ret = num;
	} else if (pulse_width > (num + 1) * range[0]
	           && pulse_width < (num + 1) * range[1]) {
		ret = num + 1;
	} else {
		ret = 0;
	}

	state = remote_data->step == REMOTE_STATUS_WAIT ? "wait" :
	        remote_data->step == RC5_STATUS_START ? "start" :
	        remote_data->step == RC5_STATUS_INDICATION ? "indication" :
	        remote_data->step == RC5_STATUS_CONTROL ? "control" :
	        remote_data->step == RC5_STATUS_SYSTEM ? "system" :
	        remote_data->step == RC5_STATUS_COMMAND ? "command" :
	        remote_data->step == RC5_STATUS_SYNC ? "sync" : NULL;
	dbg_printk("-----------------------pulse_wdith:%d  ret:%d   state:%s \n",
			pulse_width, ret, state);

	return ret;
}

static inline void kbd_rc5_wait(unsigned long data, win_time_t * window)
{
	//unsigned short pulse_width;
	struct remote *remote_data = (struct remote *)data;

	//pulse_width = get_pulse_width(data);//ignore first window
	remote_data->step = RC5_STATUS_START;
	remote_data->cur_keycode = 0;
	remote_data->bit_num = remote_data->bit_count;
	left_window.level = 1;
	left_window.time = 0;
	sub_status = 0;
	system_field = 0;
}

/*
    start bit 1
*/
static inline void kbd_rc5_start(unsigned long data, win_time_t * window)
{
	if (window->time == 1) {
		left_window.time = 0;
		goto_status(data, RC5_STATUS_INDICATION);
	} else {
		goto_status(data, REMOTE_STATUS_WAIT);
	}
}

/*
   indication bit 1
   sub_status: 0----->high 1T level, 1----->low 1T level
*/
static inline void kbd_rc5_indication(unsigned long data, win_time_t * window)
{
	if (rc5_bit1_high(window) == 0) {
		sub_status = 1;
	} else if (rc5_bit1_low(window) == 0 && sub_status == 1) {
		goto_status(data, RC5_STATUS_CONTROL);
	} else {
		goto_status(data, REMOTE_STATUS_WAIT);
	}
}

/*
    control bit: 1 bit, 0 or 1
    sub_status: 0---->low or high 1T level, 1------>high or low 1T level
*/
static inline void kbd_rc5_control(unsigned long data, win_time_t * window)
{
	if (sub_status == 0) {
		bit_0_1 = check_start_level(window);
		if (bit_0_1 == -1) {
			goto_status(data, REMOTE_STATUS_WAIT);
			return;
		}
		sub_status++;
	} else if (sub_status == 1) {
		if ((bit_0_1 == 0 && rc5_bit0_high(window) == 0)
		    || (bit_0_1 == 1 && rc5_bit1_low(window) == 0)) {
			goto_status(data, RC5_STATUS_SYSTEM);
		} else {
			goto_status(data, REMOTE_STATUS_WAIT);
		}
	} else {
		goto_status(data, REMOTE_STATUS_WAIT);
	}
}

/*
    system bits: 5 bit
    sub_status: 0,1----->1 bit,   2,3---->2 bit
                4,5----->3 bit,   6,7---->4 bit
                8,9----->5 bit
*/
static inline void kbd_rc5_system(unsigned long data, win_time_t * window)
{
	if (sub_status % 2 == 0) {
		bit_0_1 = check_start_level(window);
		if (bit_0_1 == -1) {
			goto_status(data, REMOTE_STATUS_WAIT);
			return;
		}
	} else {
		if (bit_0_1 == 0 && rc5_bit0_high(window) == 0) {
			//do nothing
		} else if (bit_0_1 == 1 && rc5_bit1_low(window) == 0) {
			system_field |= (1 << (sub_status >> 1));

		} else {
			goto_status(data, REMOTE_STATUS_WAIT);
			return;
		}
	}

	sub_status++;
	if (sub_status == 10) {
		dbg_printk("system value: 0x%x\n", system_field);
		goto_status(data, RC5_STATUS_COMMAND);
	}
}

/*
    command bits: 6 bit
    sub_status: 0,1----->1 bit,   2,3---->2 bit
                4,5----->3 bit,   6,7---->4 bit
                8,9----->5 bit,   10,11-->6 bit
*/
static inline void kbd_rc5_command(unsigned long data, win_time_t * window)
{
	struct remote *remote_data = (struct remote *)data;

	if (sub_status % 2 == 0) {
		bit_0_1 = check_start_level(window);
		if (bit_0_1 == -1) {
			goto_status(data, REMOTE_STATUS_WAIT);
			return;
		}
	} else {
		if (bit_0_1 == 0 && rc5_bit0_high(window) == 0) {
			remote_data->bit_num--;
		} else if (bit_0_1 == 1 && rc5_bit1_low(window) == 0) {
			remote_data->cur_keycode |=
			    1 << (remote_data->bit_count - remote_data->bit_num);
			remote_data->bit_num--;

		} else {
			goto_status(data, REMOTE_STATUS_WAIT);
			return;
		}
	}

	if (remote_data->bit_num == 0) {	//send the key
		dbg_printk("send key cur_keycode[0x%x]\n", remote_data->cur_keycode);
		remote_data->send_data = 1;
		fiq_bridge_pulse_trigger(&remote_data->fiq_handle_item);
	} else if (remote_data->bit_num == 1) {	//the last bit is 0, so the high level can not be captured
		if (sub_status == 10 && bit_0_1 == 0 && left_window.time == 0) {
			dbg_printk("11send key cur_keycode[0x%x]\n", remote_data->cur_keycode);
			remote_data->send_data = 1;
			fiq_bridge_pulse_trigger(&remote_data->fiq_handle_item);
		}
	}

	sub_status++;
	if (sub_status == 12) {
		goto_status(data, RC5_STATUS_SYNC);
	}

}

static inline void kbd_rc5_sync(unsigned long data, win_time_t * window)
{
	struct remote *remote_data = (struct remote *)data;

	if (window->time > 0) {
		window->time = 0;
	}
	remote_data->step = RC5_STATUS_SYNC;
	fiq_bridge_pulse_trigger(&remote_data->fiq_handle_item);
}

static void kbd_handle_sm(unsigned long data, win_time_t * window)
{
	struct remote *remote_data = (struct remote *)data;
	switch (remote_data->step) {
	case REMOTE_STATUS_WAIT:
		kbd_rc5_wait(data, window);
		break;
	case RC5_STATUS_START:
		kbd_rc5_start(data, window);
		break;
	case RC5_STATUS_INDICATION:
		kbd_rc5_indication(data, window);
		break;
	case RC5_STATUS_CONTROL:
		kbd_rc5_control(data, window);
		break;
	case RC5_STATUS_SYSTEM:
		kbd_rc5_system(data, window);
		break;
	case RC5_STATUS_COMMAND:
		kbd_rc5_command(data, window);
		break;
	case RC5_STATUS_SYNC:
		kbd_rc5_sync(data, window);
		break;
	default:
		break;
	}

	if (left_window.time > 0) {
		kbd_handle_sm(data, &left_window);
	}
}

void remote_rc5_reprot_key(unsigned long data)
{
	struct remote *remote_data = (struct remote *)data;
	int current_jiffies = jiffies;

	if (((current_jiffies - remote_data->last_jiffies) > 20)
	    && (remote_data->step <= RC5_STATUS_SYNC)) {
		remote_data->step = REMOTE_STATUS_WAIT;
	}

	remote_data->last_jiffies = current_jiffies;	//ignore a little msecs

	left_window.time = get_pulse_width(data);
	left_window.level = (left_window.level == 0) ? 1 : 0;
	kbd_handle_sm(data, &left_window);
}

irqreturn_t remote_rc5_bridge_isr(int irq, void *dev_id)
{
	struct remote *remote_data = (struct remote *)dev_id;

	if (remote_data->send_data) {	//report key
		remote_data->step = RC5_STATUS_SYNC;
		remote_send_key(remote_data->input, remote_data->cur_keycode, 1);
		remote_data->send_data = 0;
	}
	remote_data->timer.data = (unsigned long)remote_data;
	mod_timer(&remote_data->timer,
	          jiffies + msecs_to_jiffies(remote_data->release_delay));
	return IRQ_HANDLED;
}
