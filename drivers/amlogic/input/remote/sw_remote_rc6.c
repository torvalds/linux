/*
 * linux/drivers/input/irremote/sw_remote_rc6.c
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
factory_code     =
work_mode        =0x22
reg_control      =0x8574
tw_bit0          =
bit_count        =8
repeat_enable    =0
release_delay    =120

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

//unit T=444.44us, base rate generator is 20us, so 1T=444.44/20 us
#define RC6_UNIT_T 22

#define RC6_STATUS_WAIT     100
#define RC6_STATUS_LEADER   101
#define RC6_STATUS_START    102
#define RC6_STATUS_MODE     103
#define RC6_STATUS_TRAILER  104
#define RC6_STATUS_CONTROL  105
#define RC6_STATUS_INFO     106
#define RC6_STATUS_FREE     107
#define RC6_STATUS_SYNC     108

extern char *remote_log_buf;

typedef struct {
	unsigned short time;
	unsigned char level;	//0-low level 1-high level
} win_time_t;

static win_time_t left_window = {.time = 0, .level = 1 };

static int sub_status;
static int trailer_level;
static int bit_0_1;

static int control_field;

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

static inline int rc6_bit0_high(win_time_t * win)
{
	if (win->level == 1 && win->time == 1) {
		left_window.time = 0;
		return 0;
	} else {
		return -1;
	}
}

static inline int rc6_bit0_low(win_time_t * win)
{
	if (win->level == 0 && win->time >= 1) {
		left_window.time = win->time - 1;
		return 0;
	} else {
		return -1;
	}
}

static inline int rc6_bit1_low(win_time_t * win)
{
	if (win->level == 0 && win->time == 1) {
		left_window.time = 0;
		return 0;
	} else {
		return -1;
	}
}

static inline int rc6_bit1_high(win_time_t * win)
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
	if (rc6_bit0_high(win) == 0) {
		return 0;
	}

	if (rc6_bit1_low(win) == 0) {
		return 1;
	}

	return -1;
}

//return width which base rate is RC6_UNIT_T
static inline int get_pulse_width(unsigned long data)
{
	struct remote *remote_data = (struct remote *)data;
	int pulse_width;
	const char *state;
	int num;
	int ret;
	unsigned int range[2] =
	{ remote_data->time_window[2] >> 1, remote_data->time_window[3] >> 1 };

	pulse_width = (am_remote_read_reg(AM_IR_DEC_REG1) & 0x1FFF0000) >> 16;
	num = pulse_width / RC6_UNIT_T;
	if (pulse_width > num * range[0] && pulse_width < num * range[1]) {
		ret = num;
	} else if (pulse_width > (num + 1) * range[0]
	           && pulse_width < (num + 1) * range[1]) {
		ret = num + 1;
	} else {
		ret = 0;
	}

	state = remote_data->step == RC6_STATUS_WAIT ? "wait" :
	        remote_data->step == RC6_STATUS_LEADER ? "leader" :
	        remote_data->step == RC6_STATUS_START ? "start" :
	        remote_data->step == RC6_STATUS_MODE ? "mode" :
	        remote_data->step == RC6_STATUS_TRAILER ? "trailer" :
	        remote_data->step == RC6_STATUS_CONTROL ? "control" :
	        remote_data->step == RC6_STATUS_INFO ? "information" :
	        remote_data->step == RC6_STATUS_FREE ? "free" : NULL;
	dbg_printk("%02d:pulse_wdith:%d==>%s\r\n",
	           remote_data->bit_count - remote_data->bit_num, ret, state);

	return ret;
}

static inline void kbd_rc6_wait(unsigned long data, win_time_t * window)
{
	//unsigned short pulse_width;
	struct remote *remote_data = (struct remote *)data;

	//pulse_width = get_pulse_width(data);//ignore first window
	remote_data->step = RC6_STATUS_LEADER;
	remote_data->cur_keycode = 0;
	remote_data->bit_num = remote_data->bit_count;
	left_window.level = 1;
	left_window.time = 0;
	sub_status = 0;
	control_field = 0;
}

/*
    sub_status: 0----->low 6T level,  1---->high 2T level
*/
static inline void kbd_rc6_leader(unsigned long data, win_time_t * window)
{
	if (window->time == 6 && window->level == 0 && sub_status == 0) {
		sub_status = 1;
		left_window.time = 0;
	} else if (window->time == 2 && window->level == 1 && sub_status == 1) {
		goto_status(data, RC6_STATUS_START);
		left_window.time = 0;
	} else {
		goto_status(data, RC6_STATUS_WAIT);
	}
}

/*
    start bit 1
    sub_status: 0----->low 1T level,  1----->high 1T level
*/
static inline void kbd_rc6_start(unsigned long data, win_time_t * window)
{
	if (rc6_bit1_low(window) == 0 && sub_status == 0) {
		sub_status = 1;
	} else if (rc6_bit1_high(window) == 1 && sub_status == 1) {
		goto_status(data, RC6_STATUS_MODE);
	} else {
		goto_status(data, RC6_STATUS_WAIT);
	}
}

/*
    mode bits: 3bit, we only handle 000 currently
    sub_status: 0----->high 1T level, 1----->low 1T level
                2----->high 1T level, 3----->low 1T level
                4----->high 1T level, 5----->low 1T level
*/
static inline void kbd_rc6_mode(unsigned long data, win_time_t * window)
{
	switch (sub_status) {
	case 0:
		if (rc6_bit0_high(window) == 0) {
			sub_status = 1;
			return;
		}
		break;
	case 1:
		if (rc6_bit0_low(window) == 0) {
			sub_status = 2;
			return;
		}
		break;
	case 2:
		if (rc6_bit0_high(window) == 0) {
			sub_status = 3;
			return;
		}
		break;
	case 3:
		if (rc6_bit0_low(window) == 0) {
			sub_status = 4;
			return;
		}
		break;
	case 4:
		if (rc6_bit0_high(window) == 0) {
			sub_status = 5;
			return;
		}
		break;
	case 5:
		if (rc6_bit0_low(window) == 0) {
			goto_status(data, RC6_STATUS_TRAILER);
			return;
		}
		break;
	}
	goto_status(data, RC6_STATUS_WAIT);
}

/*
    trailer mode
    sub_status: 0---->low or high 2T level, 1------>high or low 2T level
*/
static inline void kbd_rc6_trailer(unsigned long data, win_time_t * window)
{
	if (sub_status == 0 && window->time == 2) {
		sub_status = 1;
		trailer_level = window->level;
		left_window.time = 0;
	} else if (sub_status == 1 && window->time >= 2
	           && trailer_level != window->level) {
		left_window.time = window->time - 2;
		goto_status(data, RC6_STATUS_CONTROL);
	} else {
		goto_status(data, RC6_STATUS_WAIT);
	}
}

/*
    control field: 8 bit
    sub_status: 0,1----->1 bit,   2,3---->2 bit
                4,5----->3 bit,   6,7---->4 bit
                8,9----->5 bit,   10,11---->6 bit
                12,13----->7 bit, 14,15---->8 bit

*/
static inline void kbd_rc6_control(unsigned long data, win_time_t * window)
{
	if (sub_status % 2 == 0) {
		bit_0_1 = check_start_level(window);
		if (bit_0_1 == -1) {
			goto_status(data, RC6_STATUS_WAIT);
			return;
		}
	} else {
		if (bit_0_1 == 0 && rc6_bit0_low(window) == 0) {
			//do nothing
		} else if (bit_0_1 == 1 && rc6_bit1_high(window) == 0) {
			control_field |= (1 << (sub_status >> 1));

		} else {
			goto_status(data, RC6_STATUS_WAIT);
			return;
		}
	}

	sub_status++;
	if (sub_status == 16) {
		goto_status(data, RC6_STATUS_INFO);
	}
}

/*
    information bits: 8 bit
    sub_status: 0,1----->1 bit,   2,3---->2 bit
                4,5----->3 bit,   6,7---->4 bit
                8,9----->5 bit,   10,11---->6 bit
                12,13----->7 bit, 14,15---->8 bit

*/
static inline void kbd_rc6_info(unsigned long data, win_time_t * window)
{
	struct remote *remote_data = (struct remote *)data;

	if (sub_status % 2 == 0) {
		bit_0_1 = check_start_level(window);
		if (bit_0_1 == -1) {
			goto_status(data, RC6_STATUS_WAIT);
			return;
		}
	} else {
		if (bit_0_1 == 1 && rc6_bit0_low(window) == 0) {
			remote_data->bit_num--;
		} else if (bit_0_1 == 1 && rc6_bit1_high(window) == 0) {
			remote_data->cur_keycode |=
			    1 << (remote_data->bit_count - remote_data->bit_num);
			remote_data->bit_num--;
		} else {
			goto_status(data, RC6_STATUS_WAIT);
			return;
		}
	}

	if (remote_data->bit_num == 0) {	//send the key
		printk(KERN_INFO "send key cur_keycode[0x%x]\n",
		       remote_data->cur_keycode);
		remote_data->send_data = 1;
		fiq_bridge_pulse_trigger(&remote_data->fiq_handle_item);
	}

	sub_status++;
	if (sub_status == 16) {
		goto_status(data, RC6_STATUS_FREE);
	}
}

/*
    free bits: 6T high level pulse
*/
static inline void kbd_rc6_free(unsigned long data, win_time_t * window)
{

}

static void kbd_handle_sm(unsigned long data, win_time_t * window)
{
	struct remote *remote_data = (struct remote *)data;

	switch (remote_data->step) {
	case RC6_STATUS_WAIT:
		kbd_rc6_wait(data, window);
		break;
	case RC6_STATUS_LEADER:
		kbd_rc6_leader(data, window);
		break;
	case RC6_STATUS_START:
		kbd_rc6_start(data, window);
		break;
	case RC6_STATUS_MODE:
		kbd_rc6_mode(data, window);
		break;
	case RC6_STATUS_TRAILER:
		kbd_rc6_trailer(data, window);
		break;
	case RC6_STATUS_CONTROL:
		kbd_rc6_control(data, window);
		break;
	case RC6_STATUS_INFO:
		kbd_rc6_info(data, window);
		break;
	case RC6_STATUS_FREE:
		kbd_rc6_free(data, window);
		break;
	default:
		break;
	}

	if (left_window.time > 0) {
		kbd_handle_sm(data, &left_window);
	}
}

void remote_rc6_reprot_key(unsigned long data)
{
	struct remote *remote_data = (struct remote *)data;
	int current_jiffies = jiffies;

	if (((current_jiffies - remote_data->last_jiffies) > 20)
	    && (remote_data->step <= RC6_STATUS_FREE)) {
		remote_data->step = RC6_STATUS_WAIT;
	}

	remote_data->last_jiffies = current_jiffies;	//ignore a little msecs

	left_window.time = get_pulse_width(data);
	left_window.level = (left_window.level == 0) ? 1 : 0;
	kbd_handle_sm(data, &left_window);
}

irqreturn_t remote_rc6_bridge_isr(int irq, void *dev_id)
{
	struct remote *remote_data = (struct remote *)dev_id;
	printk(KERN_INFO "===remote_rc6_bridge_isr\n");

	if (remote_data->send_data) {	//report key
		remote_data->step = RC6_STATUS_SYNC;
		remote_send_key(remote_data->input, remote_data->cur_keycode, 1);
		remote_data->send_data = 0;
	}
	remote_data->timer.data = (unsigned long)remote_data;
	mod_timer(&remote_data->timer,
	          jiffies + msecs_to_jiffies(remote_data->release_delay));
	return IRQ_HANDLED;
}
