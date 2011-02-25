/* arch/arm/mach-rk29/include/mach/system.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
 
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
extern void rk29_arch_reset(int mode, const char *cmd);

static inline void arch_reset(int  mode, const char *cmd)
{
	
	/*
	*  debug trace
	*/
	restart_dbg("%s->%s->%d->mode=%c cmd=%s",__FILE__,__FUNCTION__,__LINE__,mode,cmd);

	rk29_arch_reset(mode, cmd);
}

static inline void arch_idle(void)
{
	cpu_do_idle();
}
