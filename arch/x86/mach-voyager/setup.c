/*
 *	Machine specific setup for generic
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/arch_hooks.h>
#include <asm/voyager.h>
#include <asm/e820.h>
#include <asm/io.h>
#include <asm/setup.h>

void __init pre_intr_init_hook(void)
{
	init_ISA_irqs();
}

/*
 * IRQ2 is cascade interrupt to second interrupt controller
 */
static struct irqaction irq2 = {
	.handler = no_action,
	.mask = CPU_MASK_NONE,
	.name = "cascade",
};

void __init intr_init_hook(void)
{
#ifdef CONFIG_SMP
	smp_intr_init();
#endif

	setup_irq(2, &irq2);
}

void __init pre_setup_arch_hook(void)
{
	/* Voyagers run their CPUs from independent clocks, so disable
	 * the TSC code because we can't sync them */
	tsc_disable = 1;
}

void __init trap_init_hook(void)
{
}

static struct irqaction irq0  = {
	.handler = timer_interrupt,
	.flags = IRQF_DISABLED | IRQF_NOBALANCING | IRQF_IRQPOLL,
	.mask = CPU_MASK_NONE,
	.name = "timer"
};

void __init time_init_hook(void)
{
	irq0.mask = cpumask_of_cpu(safe_smp_processor_id());
	setup_irq(0, &irq0);
}

/* Hook for machine specific memory setup. */

char * __init machine_specific_memory_setup(void)
{
	char *who;

	who = "NOT VOYAGER";

	if(voyager_level == 5) {
		__u32 addr, length;
		int i;

		who = "Voyager-SUS";

		e820.nr_map = 0;
		for(i=0; voyager_memory_detect(i, &addr, &length); i++) {
			add_memory_region(addr, length, E820_RAM);
		}
		return who;
	} else if(voyager_level == 4) {
		__u32 tom;
		__u16 catbase = inb(VOYAGER_SSPB_RELOCATION_PORT)<<8;
		/* select the DINO config space */
		outb(VOYAGER_DINO, VOYAGER_CAT_CONFIG_PORT);
		/* Read DINO top of memory register */
		tom = ((inb(catbase + 0x4) & 0xf0) << 16)
			+ ((inb(catbase + 0x5) & 0x7f) << 24);

		if(inb(catbase) != VOYAGER_DINO) {
			printk(KERN_ERR "Voyager: Failed to get DINO for L4, setting tom to EXT_MEM_K\n");
			tom = (boot_params.screen_info.ext_mem_k)<<10;
		}
		who = "Voyager-TOM";
		add_memory_region(0, 0x9f000, E820_RAM);
		/* map from 1M to top of memory */
		add_memory_region(1*1024*1024, tom - 1*1024*1024, E820_RAM);
		/* FIXME: Should check the ASICs to see if I need to
		 * take out the 8M window.  Just do it at the moment
		 * */
		add_memory_region(8*1024*1024, 8*1024*1024, E820_RESERVED);
		return who;
	}

	who = "BIOS-e820";

	/*
	 * Try to copy the BIOS-supplied E820-map.
	 *
	 * Otherwise fake a memory map; one section from 0k->640k,
	 * the next section from 1mb->appropriate_mem_k
	 */
	sanitize_e820_map(boot_params.e820_map, &boot_params.e820_entries);
	if (copy_e820_map(boot_params.e820_map, boot_params.e820_entries)
	    < 0) {
		unsigned long mem_size;

		/* compare results from other methods and take the greater */
		if (boot_params.alt_mem_k
		    < boot_params.screen_info.ext_mem_k) {
			mem_size = boot_params.screen_info.ext_mem_k;
			who = "BIOS-88";
		} else {
			mem_size = boot_params.alt_mem_k;
			who = "BIOS-e801";
		}

		e820.nr_map = 0;
		add_memory_region(0, LOWMEMSIZE(), E820_RAM);
		add_memory_region(HIGH_MEMORY, mem_size << 10, E820_RAM);
  	}
	return who;
}
