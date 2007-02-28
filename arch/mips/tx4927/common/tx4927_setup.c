/*
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * Copyright 2001-2002 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/irq.h>
#include <linux/bitops.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/system.h>
#include <asm/time.h>
#include <asm/tx4927/tx4927.h>


#undef DEBUG

void __init tx4927_time_init(void);
void dump_cp0(char *key);


void __init plat_mem_setup(void)
{
	board_time_init = tx4927_time_init;

#ifdef CONFIG_TOSHIBA_RBTX4927
	{
		extern void toshiba_rbtx4927_setup(void);
		toshiba_rbtx4927_setup();
	}
#endif
}

void __init tx4927_time_init(void)
{

#ifdef CONFIG_TOSHIBA_RBTX4927
	{
		extern void toshiba_rbtx4927_time_init(void);
		toshiba_rbtx4927_time_init();
	}
#endif

	return;
}


void __init plat_timer_setup(struct irqaction *irq)
{
	setup_irq(TX4927_IRQ_CPU_TIMER, irq);

#ifdef CONFIG_TOSHIBA_RBTX4927
	{
		extern void toshiba_rbtx4927_timer_setup(struct irqaction
							 *irq);
		toshiba_rbtx4927_timer_setup(irq);
	}
#endif

	return;
}


#ifdef DEBUG
void print_cp0(char *key, int num, char *name, u32 val)
{
	printk("%s cp0:%02d:%s=0x%08x\n", key, num, name, val);
	return;
}

void
dump_cp0(char *key)
{
	if (key == NULL)
		key = "";

	print_cp0(key, 0, "INDEX   ", read_c0_index());
	print_cp0(key, 2, "ENTRYLO1", read_c0_entrylo0());
	print_cp0(key, 3, "ENTRYLO2", read_c0_entrylo1());
	print_cp0(key, 4, "CONTEXT ", read_c0_context());
	print_cp0(key, 5, "PAGEMASK", read_c0_pagemask());
	print_cp0(key, 6, "WIRED   ", read_c0_wired());
	//print_cp0(key, 8, "BADVADDR",  read_c0_badvaddr());
	print_cp0(key, 9, "COUNT   ", read_c0_count());
	print_cp0(key, 10, "ENTRYHI ", read_c0_entryhi());
	print_cp0(key, 11, "COMPARE ", read_c0_compare());
	print_cp0(key, 12, "STATUS  ", read_c0_status());
	print_cp0(key, 13, "CAUSE   ", read_c0_cause() & 0xffff87ff);
	print_cp0(key, 16, "CONFIG  ", read_c0_config());
	return;
}

void print_pic(char *key, u32 reg, char *name)
{
	printk("%s pic:0x%08x:%s=0x%08x\n", key, reg, name,
	       TX4927_RD(reg));
	return;
}


void dump_pic(char *key)
{
	if (key == NULL)
		key = "";

	print_pic(key, 0xff1ff600, "IRDEN    ");
	print_pic(key, 0xff1ff604, "IRDM0    ");
	print_pic(key, 0xff1ff608, "IRDM1    ");

	print_pic(key, 0xff1ff610, "IRLVL0   ");
	print_pic(key, 0xff1ff614, "IRLVL1   ");
	print_pic(key, 0xff1ff618, "IRLVL2   ");
	print_pic(key, 0xff1ff61c, "IRLVL3   ");
	print_pic(key, 0xff1ff620, "IRLVL4   ");
	print_pic(key, 0xff1ff624, "IRLVL5   ");
	print_pic(key, 0xff1ff628, "IRLVL6   ");
	print_pic(key, 0xff1ff62c, "IRLVL7   ");

	print_pic(key, 0xff1ff640, "IRMSK    ");
	print_pic(key, 0xff1ff660, "IREDC    ");
	print_pic(key, 0xff1ff680, "IRPND    ");
	print_pic(key, 0xff1ff6a0, "IRCS     ");

	print_pic(key, 0xff1ff514, "IRFLAG1  ");	/* don't read IRLAG0 -- it hangs system */

	print_pic(key, 0xff1ff518, "IRPOL    ");
	print_pic(key, 0xff1ff51c, "IRRCNT   ");
	print_pic(key, 0xff1ff520, "IRMASKINT");
	print_pic(key, 0xff1ff524, "IRMASKEXT");

	return;
}


void print_addr(char *hdr, char *key, u32 addr)
{
	printk("%s %s:0x%08x=0x%08x\n", hdr, key, addr, TX4927_RD(addr));
	return;
}


void dump_180(char *key)
{
	u32 i;

	for (i = 0x80000180; i < 0x80000180 + 0x80; i += 4) {
		print_addr("180", key, i);
	}
	return;
}


void dump_eh0(char *key)
{
	int i;
	extern unsigned long exception_handlers[];

	for (i = (int) exception_handlers;
	     i < (int) (exception_handlers + 20); i += 4) {
		print_addr("eh0", key, i);
	}

	return;
}

void pk0(void)
{
	volatile u32 val;

	__asm__ __volatile__("ori %0, $26, 0":"=r"(val)
	    );
	printk("k0=[0x%08x]\n", val);
}
#endif
