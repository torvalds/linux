/*
* Copyright (C) 2010 ROCKCHIP, Inc.
*
* This software is licensed under the terms of the GNU General Public
* License version 2, as published by the Free Software Foundation, and
* may be copied, distributed, and modified under those terms.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
* GNU General Public License for more details.
*
*/

#include <linux/types.h>
#include <linux/module.h>

#include <linux/init.h>
#include <linux/irqflags.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/memory.h>
//#include <asm/cpu-single.h>
#include <mach/gpio.h>
#include <mach/hardware.h>
#include <mach/rk2818_iomap.h>
#include <mach/scu.h>

/***************
*	 DEBUG
****************/
#define RESTART_DEBUG
#ifdef RESTART_DEBUG
#define restart_dbg(format, arg...) \
				printk("RESTART_DEBUG : " format "\n" , ## arg)
#else
#define restart_dbg(format, arg...) do {} while (0)
#endif

extern void(*rk2818_reboot)(void );
extern void setup_mm_for_reboot(char mode);
//extern void cpu_proc_fin(void);


/* 
 * reset: 0 : normal reset 1: panic , 2: hard reset.
 * boot : 0: normal , 1: loader , 2: maskrom , 3:recovery
 *
 */
void rk2818_soft_restart( void )
{
	scu_set_clk_for_reboot( );   // MUST slow down ddr freq
	rk2818_reboot( );	// normal 
}



static void rk_reboot( void)
{
	local_irq_disable();
//	cpu_proc_fin();
       setup_mm_for_reboot('r');
	rk2818_soft_restart();
}





 int rk2818_restart( int	mode, const char *cmd) 
{
		restart_dbg("%s->%s->%d",__FILE__,__FUNCTION__,__LINE__);
		switch ( mode ) {
		case 0:
				rk_reboot( );
				break;
		case 1:
				//rk28_usb();
				//kld_reboot( 0 , type );	 // loader usb 
				break;
		case 2:
				//rk28_usb();
				//kld_reboot( 0 , type );	 // maksrom usb
				break;
		case 3:
				//kld_reboot( 0 , type );	 // normal and recover
				break;
		case 4:
				*(int*)(0xfe04c0fa) = 0xe5e6e700;
				break;
		default:
				{
				void(*deader)(void) = (void(*)(void))0xc600c400;
				deader();
				}
				break;
		}
		return 0x24;
}
EXPORT_SYMBOL(rk2818_restart);

