/*	Cyclone-timer: 
 *		This code implements timer_ops for the cyclone counter found
 *		on IBM x440, x360, and other Summit based systems.
 *
 *	Copyright (C) 2002 IBM, John Stultz (johnstul@us.ibm.com)
 */


#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/timex.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/jiffies.h>

#include <asm/timer.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/fixmap.h>
#include <asm/i8253.h>

#include "io_ports.h"

/* Number of usecs that the last interrupt was delayed */
static int delay_at_last_interrupt;

#define CYCLONE_CBAR_ADDR 0xFEB00CD0
#define CYCLONE_PMCC_OFFSET 0x51A0
#define CYCLONE_MPMC_OFFSET 0x51D0
#define CYCLONE_MPCS_OFFSET 0x51A8
#define CYCLONE_TIMER_FREQ 100000000
#define CYCLONE_TIMER_MASK (((u64)1<<40)-1) /* 40 bit mask */
int use_cyclone = 0;

static u32* volatile cyclone_timer;	/* Cyclone MPMC0 register */
static u32 last_cyclone_low;
static u32 last_cyclone_high;
static unsigned long long monotonic_base;
static seqlock_t monotonic_lock = SEQLOCK_UNLOCKED;

/* helper macro to atomically read both cyclone counter registers */
#define read_cyclone_counter(low,high) \
	do{ \
		high = cyclone_timer[1]; low = cyclone_timer[0]; \
	} while (high != cyclone_timer[1]);


static void mark_offset_cyclone(void)
{
	unsigned long lost, delay;
	unsigned long delta = last_cyclone_low;
	int count;
	unsigned long long this_offset, last_offset;

	write_seqlock(&monotonic_lock);
	last_offset = ((unsigned long long)last_cyclone_high<<32)|last_cyclone_low;
	
	spin_lock(&i8253_lock);
	read_cyclone_counter(last_cyclone_low,last_cyclone_high);

	/* read values for delay_at_last_interrupt */
	outb_p(0x00, 0x43);     /* latch the count ASAP */

	count = inb_p(0x40);    /* read the latched count */
	count |= inb(0x40) << 8;

	/*
	 * VIA686a test code... reset the latch if count > max + 1
	 * from timer_pit.c - cjb
	 */
	if (count > LATCH) {
		outb_p(0x34, PIT_MODE);
		outb_p(LATCH & 0xff, PIT_CH0);
		outb(LATCH >> 8, PIT_CH0);
		count = LATCH - 1;
	}
	spin_unlock(&i8253_lock);

	/* lost tick compensation */
	delta = last_cyclone_low - delta;	
	delta /= (CYCLONE_TIMER_FREQ/1000000);
	delta += delay_at_last_interrupt;
	lost = delta/(1000000/HZ);
	delay = delta%(1000000/HZ);
	if (lost >= 2)
		jiffies_64 += lost-1;
	
	/* update the monotonic base value */
	this_offset = ((unsigned long long)last_cyclone_high<<32)|last_cyclone_low;
	monotonic_base += (this_offset - last_offset) & CYCLONE_TIMER_MASK;
	write_sequnlock(&monotonic_lock);

	/* calculate delay_at_last_interrupt */
	count = ((LATCH-1) - count) * TICK_SIZE;
	delay_at_last_interrupt = (count + LATCH/2) / LATCH;


	/* catch corner case where tick rollover occured 
	 * between cyclone and pit reads (as noted when 
	 * usec delta is > 90% # of usecs/tick)
	 */
	if (lost && abs(delay - delay_at_last_interrupt) > (900000/HZ))
		jiffies_64++;
}

static unsigned long get_offset_cyclone(void)
{
	u32 offset;

	if(!cyclone_timer)
		return delay_at_last_interrupt;

	/* Read the cyclone timer */
	offset = cyclone_timer[0];

	/* .. relative to previous jiffy */
	offset = offset - last_cyclone_low;

	/* convert cyclone ticks to microseconds */	
	/* XXX slow, can we speed this up? */
	offset = offset/(CYCLONE_TIMER_FREQ/1000000);

	/* our adjusted time offset in microseconds */
	return delay_at_last_interrupt + offset;
}

static unsigned long long monotonic_clock_cyclone(void)
{
	u32 now_low, now_high;
	unsigned long long last_offset, this_offset, base;
	unsigned long long ret;
	unsigned seq;

	/* atomically read monotonic base & last_offset */
	do {
		seq = read_seqbegin(&monotonic_lock);
		last_offset = ((unsigned long long)last_cyclone_high<<32)|last_cyclone_low;
		base = monotonic_base;
	} while (read_seqretry(&monotonic_lock, seq));


	/* Read the cyclone counter */
	read_cyclone_counter(now_low,now_high);
	this_offset = ((unsigned long long)now_high<<32)|now_low;

	/* convert to nanoseconds */
	ret = base + ((this_offset - last_offset)&CYCLONE_TIMER_MASK);
	return ret * (1000000000 / CYCLONE_TIMER_FREQ);
}

static int __init init_cyclone(char* override)
{
	u32* reg;	
	u32 base;		/* saved cyclone base address */
	u32 pageaddr;	/* page that contains cyclone_timer register */
	u32 offset;		/* offset from pageaddr to cyclone_timer register */
	int i;
	
	/* check clock override */
	if (override[0] && strncmp(override,"cyclone",7))
			return -ENODEV;

	/*make sure we're on a summit box*/
	if(!use_cyclone) return -ENODEV; 
	
	printk(KERN_INFO "Summit chipset: Starting Cyclone Counter.\n");

	/* find base address */
	pageaddr = (CYCLONE_CBAR_ADDR)&PAGE_MASK;
	offset = (CYCLONE_CBAR_ADDR)&(~PAGE_MASK);
	set_fixmap_nocache(FIX_CYCLONE_TIMER, pageaddr);
	reg = (u32*)(fix_to_virt(FIX_CYCLONE_TIMER) + offset);
	if(!reg){
		printk(KERN_ERR "Summit chipset: Could not find valid CBAR register.\n");
		return -ENODEV;
	}
	base = *reg;	
	if(!base){
		printk(KERN_ERR "Summit chipset: Could not find valid CBAR value.\n");
		return -ENODEV;
	}
	
	/* setup PMCC */
	pageaddr = (base + CYCLONE_PMCC_OFFSET)&PAGE_MASK;
	offset = (base + CYCLONE_PMCC_OFFSET)&(~PAGE_MASK);
	set_fixmap_nocache(FIX_CYCLONE_TIMER, pageaddr);
	reg = (u32*)(fix_to_virt(FIX_CYCLONE_TIMER) + offset);
	if(!reg){
		printk(KERN_ERR "Summit chipset: Could not find valid PMCC register.\n");
		return -ENODEV;
	}
	reg[0] = 0x00000001;

	/* setup MPCS */
	pageaddr = (base + CYCLONE_MPCS_OFFSET)&PAGE_MASK;
	offset = (base + CYCLONE_MPCS_OFFSET)&(~PAGE_MASK);
	set_fixmap_nocache(FIX_CYCLONE_TIMER, pageaddr);
	reg = (u32*)(fix_to_virt(FIX_CYCLONE_TIMER) + offset);
	if(!reg){
		printk(KERN_ERR "Summit chipset: Could not find valid MPCS register.\n");
		return -ENODEV;
	}
	reg[0] = 0x00000001;

	/* map in cyclone_timer */
	pageaddr = (base + CYCLONE_MPMC_OFFSET)&PAGE_MASK;
	offset = (base + CYCLONE_MPMC_OFFSET)&(~PAGE_MASK);
	set_fixmap_nocache(FIX_CYCLONE_TIMER, pageaddr);
	cyclone_timer = (u32*)(fix_to_virt(FIX_CYCLONE_TIMER) + offset);
	if(!cyclone_timer){
		printk(KERN_ERR "Summit chipset: Could not find valid MPMC register.\n");
		return -ENODEV;
	}

	/*quick test to make sure its ticking*/
	for(i=0; i<3; i++){
		u32 old = cyclone_timer[0];
		int stall = 100;
		while(stall--) barrier();
		if(cyclone_timer[0] == old){
			printk(KERN_ERR "Summit chipset: Counter not counting! DISABLED\n");
			cyclone_timer = 0;
			return -ENODEV;
		}
	}

	init_cpu_khz();

	/* Everything looks good! */
	return 0;
}


static void delay_cyclone(unsigned long loops)
{
	unsigned long bclock, now;
	if(!cyclone_timer)
		return;
	bclock = cyclone_timer[0];
	do {
		rep_nop();
		now = cyclone_timer[0];
	} while ((now-bclock) < loops);
}
/************************************************************/

/* cyclone timer_opts struct */
static struct timer_opts timer_cyclone = {
	.name = "cyclone",
	.mark_offset = mark_offset_cyclone, 
	.get_offset = get_offset_cyclone,
	.monotonic_clock =	monotonic_clock_cyclone,
	.delay = delay_cyclone,
};

struct init_timer_opts __initdata timer_cyclone_init = {
	.init = init_cyclone,
	.opts = &timer_cyclone,
};
