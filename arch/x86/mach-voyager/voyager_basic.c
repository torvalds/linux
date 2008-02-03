/* Copyright (C) 1999,2001 
 *
 * Author: J.E.J.Bottomley@HansenPartnership.com
 *
 * linux/arch/i386/kernel/voyager.c
 *
 * This file contains all the voyager specific routines for getting
 * initialisation of the architecture to function.  For additional
 * features see:
 *
 *	voyager_cat.c - Voyager CAT bus interface
 *	voyager_smp.c - Voyager SMP hal (emulates linux smp.c)
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/sysrq.h>
#include <linux/smp.h>
#include <linux/nodemask.h>
#include <asm/io.h>
#include <asm/voyager.h>
#include <asm/vic.h>
#include <linux/pm.h>
#include <asm/tlbflush.h>
#include <asm/arch_hooks.h>
#include <asm/i8253.h>

/*
 * Power off function, if any
 */
void (*pm_power_off) (void);
EXPORT_SYMBOL(pm_power_off);

int voyager_level = 0;

struct voyager_SUS *voyager_SUS = NULL;

#ifdef CONFIG_SMP
static void voyager_dump(int dummy1, struct tty_struct *dummy3)
{
	/* get here via a sysrq */
	voyager_smp_dump();
}

static struct sysrq_key_op sysrq_voyager_dump_op = {
	.handler = voyager_dump,
	.help_msg = "Voyager",
	.action_msg = "Dump Voyager Status",
};
#endif

void voyager_detect(struct voyager_bios_info *bios)
{
	if (bios->len != 0xff) {
		int class = (bios->class_1 << 8)
		    | (bios->class_2 & 0xff);

		printk("Voyager System detected.\n"
		       "        Class %x, Revision %d.%d\n",
		       class, bios->major, bios->minor);
		if (class == VOYAGER_LEVEL4)
			voyager_level = 4;
		else if (class < VOYAGER_LEVEL5_AND_ABOVE)
			voyager_level = 3;
		else
			voyager_level = 5;
		printk("        Architecture Level %d\n", voyager_level);
		if (voyager_level < 4)
			printk
			    ("\n**WARNING**: Voyager HAL only supports Levels 4 and 5 Architectures at the moment\n\n");
		/* install the power off handler */
		pm_power_off = voyager_power_off;
#ifdef CONFIG_SMP
		register_sysrq_key('v', &sysrq_voyager_dump_op);
#endif
	} else {
		printk("\n\n**WARNING**: No Voyager Subsystem Found\n");
	}
}

void voyager_system_interrupt(int cpl, void *dev_id)
{
	printk("Voyager: detected system interrupt\n");
}

/* Routine to read information from the extended CMOS area */
__u8 voyager_extended_cmos_read(__u16 addr)
{
	outb(addr & 0xff, 0x74);
	outb((addr >> 8) & 0xff, 0x75);
	return inb(0x76);
}

/* internal definitions for the SUS Click Map of memory */

#define CLICK_ENTRIES	16
#define CLICK_SIZE	4096	/* click to byte conversion for Length */

typedef struct ClickMap {
	struct Entry {
		__u32 Address;
		__u32 Length;
	} Entry[CLICK_ENTRIES];
} ClickMap_t;

/* This routine is pretty much an awful hack to read the bios clickmap by
 * mapping it into page 0.  There are usually three regions in the map:
 * 	Base Memory
 * 	Extended Memory
 *	zero length marker for end of map
 *
 * Returns are 0 for failure and 1 for success on extracting region.
 */
int __init voyager_memory_detect(int region, __u32 * start, __u32 * length)
{
	int i;
	int retval = 0;
	__u8 cmos[4];
	ClickMap_t *map;
	unsigned long map_addr;
	unsigned long old;

	if (region >= CLICK_ENTRIES) {
		printk("Voyager: Illegal ClickMap region %d\n", region);
		return 0;
	}

	for (i = 0; i < sizeof(cmos); i++)
		cmos[i] =
		    voyager_extended_cmos_read(VOYAGER_MEMORY_CLICKMAP + i);

	map_addr = *(unsigned long *)cmos;

	/* steal page 0 for this */
	old = pg0[0];
	pg0[0] = ((map_addr & PAGE_MASK) | _PAGE_RW | _PAGE_PRESENT);
	local_flush_tlb();
	/* now clear everything out but page 0 */
	map = (ClickMap_t *) (map_addr & (~PAGE_MASK));

	/* zero length is the end of the clickmap */
	if (map->Entry[region].Length != 0) {
		*length = map->Entry[region].Length * CLICK_SIZE;
		*start = map->Entry[region].Address;
		retval = 1;
	}

	/* replace the mapping */
	pg0[0] = old;
	local_flush_tlb();
	return retval;
}

/* voyager specific handling code for timer interrupts.  Used to hand
 * off the timer tick to the SMP code, since the VIC doesn't have an
 * internal timer (The QIC does, but that's another story). */
void voyager_timer_interrupt(void)
{
	if ((jiffies & 0x3ff) == 0) {

		/* There seems to be something flaky in either
		 * hardware or software that is resetting the timer 0
		 * count to something much higher than it should be
		 * This seems to occur in the boot sequence, just
		 * before root is mounted.  Therefore, every 10
		 * seconds or so, we sanity check the timer zero count
		 * and kick it back to where it should be.
		 *
		 * FIXME: This is the most awful hack yet seen.  I
		 * should work out exactly what is interfering with
		 * the timer count settings early in the boot sequence
		 * and swiftly introduce it to something sharp and
		 * pointy.  */
		__u16 val;

		spin_lock(&i8253_lock);

		outb_p(0x00, 0x43);
		val = inb_p(0x40);
		val |= inb(0x40) << 8;
		spin_unlock(&i8253_lock);

		if (val > LATCH) {
			printk
			    ("\nVOYAGER: countdown timer value too high (%d), resetting\n\n",
			     val);
			spin_lock(&i8253_lock);
			outb(0x34, 0x43);
			outb_p(LATCH & 0xff, 0x40);	/* LSB */
			outb(LATCH >> 8, 0x40);	/* MSB */
			spin_unlock(&i8253_lock);
		}
	}
#ifdef CONFIG_SMP
	smp_vic_timer_interrupt();
#endif
}

void voyager_power_off(void)
{
	printk("VOYAGER Power Off\n");

	if (voyager_level == 5) {
		voyager_cat_power_off();
	} else if (voyager_level == 4) {
		/* This doesn't apparently work on most L4 machines,
		 * but the specs say to do this to get automatic power
		 * off.  Unfortunately, if it doesn't power off the
		 * machine, it ends up doing a cold restart, which
		 * isn't really intended, so comment out the code */
#if 0
		int port;

		/* enable the voyager Configuration Space */
		outb((inb(VOYAGER_MC_SETUP) & 0xf0) | 0x8, VOYAGER_MC_SETUP);
		/* the port for the power off flag is an offset from the
		   floating base */
		port = (inb(VOYAGER_SSPB_RELOCATION_PORT) << 8) + 0x21;
		/* set the power off flag */
		outb(inb(port) | 0x1, port);
#endif
	}
	/* and wait for it to happen */
	local_irq_disable();
	for (;;)
		halt();
}

/* copied from process.c */
static inline void kb_wait(void)
{
	int i;

	for (i = 0; i < 0x10000; i++)
		if ((inb_p(0x64) & 0x02) == 0)
			break;
}

void machine_shutdown(void)
{
	/* Architecture specific shutdown needed before a kexec */
}

void machine_restart(char *cmd)
{
	printk("Voyager Warm Restart\n");
	kb_wait();

	if (voyager_level == 5) {
		/* write magic values to the RTC to inform system that
		 * shutdown is beginning */
		outb(0x8f, 0x70);
		outb(0x5, 0x71);

		udelay(50);
		outb(0xfe, 0x64);	/* pull reset low */
	} else if (voyager_level == 4) {
		__u16 catbase = inb(VOYAGER_SSPB_RELOCATION_PORT) << 8;
		__u8 basebd = inb(VOYAGER_MC_SETUP);

		outb(basebd | 0x08, VOYAGER_MC_SETUP);
		outb(0x02, catbase + 0x21);
	}
	local_irq_disable();
	for (;;)
		halt();
}

void machine_emergency_restart(void)
{
	/*for now, just hook this to a warm restart */
	machine_restart(NULL);
}

void mca_nmi_hook(void)
{
	__u8 dumpval __maybe_unused = inb(0xf823);
	__u8 swnmi __maybe_unused = inb(0xf813);

	/* FIXME: assume dump switch pressed */
	/* check to see if the dump switch was pressed */
	VDEBUG(("VOYAGER: dumpval = 0x%x, swnmi = 0x%x\n", dumpval, swnmi));
	/* clear swnmi */
	outb(0xff, 0xf813);
	/* tell SUS to ignore dump */
	if (voyager_level == 5 && voyager_SUS != NULL) {
		if (voyager_SUS->SUS_mbox == VOYAGER_DUMP_BUTTON_NMI) {
			voyager_SUS->kernel_mbox = VOYAGER_NO_COMMAND;
			voyager_SUS->kernel_flags |= VOYAGER_OS_IN_PROGRESS;
			udelay(1000);
			voyager_SUS->kernel_mbox = VOYAGER_IGNORE_DUMP;
			voyager_SUS->kernel_flags &= ~VOYAGER_OS_IN_PROGRESS;
		}
	}
	printk(KERN_ERR
	       "VOYAGER: Dump switch pressed, printing CPU%d tracebacks\n",
	       smp_processor_id());
	show_stack(NULL, NULL);
	show_state();
}

void machine_halt(void)
{
	/* treat a halt like a power off */
	machine_power_off();
}

void machine_power_off(void)
{
	if (pm_power_off)
		pm_power_off();
}
