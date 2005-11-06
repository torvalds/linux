/*
 *
 *
 * Copyright (c) 2005 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it 
 * under the terms of version 2 of the GNU General Public License 
 * as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 * 
 * Further, this software is distributed without any warranty that it is 
 * free of the rightful claim of any third person regarding infringement 
 * or the like.  Any license provided herein, whether implied or 
 * otherwise, applies only to this software file.  Patent licenses, if 
 * any, provided herein do not apply to combinations of this program with 
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public 
 * License along with this program; if not, write the Free Software 
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information:  Silicon Graphics, Inc., 1600 Amphitheatre Pkwy, 
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
 */

#include <linux/interrupt.h>
#include <asm/sn/pda.h>
#include <asm/sn/leds.h>

extern void sn_lb_int_war_check(void);
extern irqreturn_t timer_interrupt(int irq, void *dev_id, struct pt_regs *regs);

#define SN_LB_INT_WAR_INTERVAL 100

void sn_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	/* LED blinking */
	if (!pda->hb_count--) {
		pda->hb_count = HZ / 2;
		set_led_bits(pda->hb_state ^=
			     LED_CPU_HEARTBEAT, LED_CPU_HEARTBEAT);
	}

	if (is_shub1()) {
		if (enable_shub_wars_1_1()) {
			/* Bugfix code for SHUB 1.1 */
			if (pda->pio_shub_war_cam_addr)
				*pda->pio_shub_war_cam_addr = 0x8000000000000010UL;
		}
		if (pda->sn_lb_int_war_ticks == 0)
			sn_lb_int_war_check();
		pda->sn_lb_int_war_ticks++;
		if (pda->sn_lb_int_war_ticks >= SN_LB_INT_WAR_INTERVAL)
			pda->sn_lb_int_war_ticks = 0;
	}
}
