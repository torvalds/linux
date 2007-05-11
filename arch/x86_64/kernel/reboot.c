/* Various gunk just to reboot the machine. */ 
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/pm.h>
#include <linux/kdebug.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/hw_irq.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/apic.h>

/*
 * Power off function, if any
 */
void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

static long no_idt[3];
static enum { 
	BOOT_TRIPLE = 't',
	BOOT_KBD = 'k'
} reboot_type = BOOT_KBD;
static int reboot_mode = 0;
int reboot_force;

/* reboot=t[riple] | k[bd] [, [w]arm | [c]old]
   warm   Don't set the cold reboot flag
   cold   Set the cold reboot flag
   triple Force a triple fault (init)
   kbd    Use the keyboard controller. cold reset (default)
   force  Avoid anything that could hang.
 */ 
static int __init reboot_setup(char *str)
{
	for (;;) {
		switch (*str) {
		case 'w': 
			reboot_mode = 0x1234;
			break;

		case 'c':
			reboot_mode = 0;
			break;

		case 't':
		case 'b':
		case 'k':
			reboot_type = *str;
			break;
		case 'f':
			reboot_force = 1;
			break;
		}
		if((str = strchr(str,',')) != NULL)
			str++;
		else
			break;
	}
	return 1;
}

__setup("reboot=", reboot_setup);

static inline void kb_wait(void)
{
	int i;

	for (i=0; i<0x10000; i++)
		if ((inb_p(0x64) & 0x02) == 0)
			break;
}

void machine_shutdown(void)
{
	unsigned long flags;
	/* Stop the cpus and apics */
#ifdef CONFIG_SMP
	int reboot_cpu_id;

	/* The boot cpu is always logical cpu 0 */
	reboot_cpu_id = 0;

	/* Make certain the cpu I'm about to reboot on is online */
	if (!cpu_isset(reboot_cpu_id, cpu_online_map)) {
		reboot_cpu_id = smp_processor_id();
	}

	/* Make certain I only run on the appropriate processor */
	set_cpus_allowed(current, cpumask_of_cpu(reboot_cpu_id));

	/* O.K Now that I'm on the appropriate processor,
	 * stop all of the others.
	 */
	smp_send_stop();
#endif

	local_irq_save(flags);

#ifndef CONFIG_SMP
	disable_local_APIC();
#endif

	disable_IO_APIC();

	local_irq_restore(flags);
}

void machine_emergency_restart(void)
{
	int i;

	/* Tell the BIOS if we want cold or warm reboot */
	*((unsigned short *)__va(0x472)) = reboot_mode;
       
	for (;;) {
		/* Could also try the reset bit in the Hammer NB */
		switch (reboot_type) { 
		case BOOT_KBD:
		for (i=0; i<10; i++) {
			kb_wait();
			udelay(50);
			outb(0xfe,0x64);         /* pulse reset low */
			udelay(50);
		}

		case BOOT_TRIPLE: 
			__asm__ __volatile__("lidt (%0)": :"r" (&no_idt));
			__asm__ __volatile__("int3");

			reboot_type = BOOT_KBD;
			break;
		}      
	}      
}

void machine_restart(char * __unused)
{
	printk("machine restart\n");

	if (!reboot_force) {
		machine_shutdown();
	}
	machine_emergency_restart();
}

void machine_halt(void)
{
}

void machine_power_off(void)
{
	if (pm_power_off) {
		if (!reboot_force) {
			machine_shutdown();
		}
		pm_power_off();
	}
}

