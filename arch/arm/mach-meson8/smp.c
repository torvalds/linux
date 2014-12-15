/*
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *  Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <plat/io.h>
#include <mach/io.h>
#include <mach/cpu.h>
#include <mach/smp.h>
#include <asm/smp_scu.h>
#include <asm/hardware/gic.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>
#include <asm/cacheflush.h>
#include <asm/mach-types.h>
#include <linux/percpu.h>

static DEFINE_SPINLOCK(boot_lock);
#if 0
static unsigned int cpu_entry_code[16];
/*
 * Write pen_release in a way that is guaranteed to be visible to all
 * observers, irrespective of whether they're taking part in coherency
 * or not.  This is necessary for the hotplug code to work reliably.
 */
void __init backup_cpu_entry_code(void)
{
	unsigned int* p = 0xc0000000;
	unsigned int i;
	unsigned int count = sizeof(cpu_entry_code)/sizeof(cpu_entry_code[0]);
	for(i=0; i<count; i++)
		cpu_entry_code[i] = p[i];
}

static void check_and_rewrite_cpu_entry(void)
{
	unsigned int i;
	unsigned int *p=0xc0000000;
	int changed=0;
	unsigned int count=sizeof(cpu_entry_code)/sizeof(cpu_entry_code[0]);
	for(i=0; i<count; i++){
		if(cpu_entry_code[i] != p[i]){
			changed=1;
			break;
		}
	}
	if(changed != 0){
		printk("!!!CPU boot warning: cpu entry code has been changed!\n");
		for(i=0, p=0xc0000000; i<count; i++)
			p[i]=cpu_entry_code[i];

		smp_wmb();
		__cpuc_flush_dcache_area((void *)p, sizeof(cpu_entry_code));
		outer_clean_range(__pa(p), __pa(p+count));
	}
}
#endif
static void write_pen_release(int val)
{
	pen_release = val;
	smp_wmb();
	__cpuc_flush_dcache_area((void *)&pen_release, sizeof(pen_release));
	outer_clean_range(__pa(&pen_release), __pa(&pen_release + 1));
}

static void meson_secondary_set(unsigned int cpu)
{
	meson_set_cpu_ctrl_addr(cpu,
		(const uint32_t)virt_to_phys(meson_secondary_startup));
	meson_set_cpu_ctrl_reg(cpu, 1);
	smp_wmb();	
	mb();
}

void __cpuinit meson_secondary_init(unsigned int cpu)
{

	/*
	 * if any interrupts are already enabled for the primary
	 * core (e.g. timer irq), then they will not have been enabled
	 * for us: do so
	 */
//	gic_secondary_init(0);
#ifdef CONFIG_MESON_ARM_GIC_FIQ	
extern void  init_fiq(void);	
	init_fiq();
#endif	
 
	/*
	 * let the primary processor know we're out of the
	 * pen, then head off into the C entry point
	 */
	write_pen_release(-1);
	smp_wmb();

	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);
}

int __cpuinit meson_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long timeout;

	/*
	* Set synchronisation state between this boot processor
	* and the secondary one
	*/
	spin_lock(&boot_lock);
	 
	/*
	 * The secondary processor is waiting to be released from
	 * the holding pen - release it, then wait for it to flag
	 * that it has been released by resetting pen_release.
	 */
	printk("write pen_release: %d\n",cpu_logical_map(cpu));
	write_pen_release(cpu_logical_map(cpu));

#ifndef CONFIG_MESON_TRUSTZONE
//	check_and_rewrite_cpu_entry();
	meson_set_cpu_ctrl_addr(cpu,
			(const uint32_t)virt_to_phys(meson_secondary_startup));
	meson_set_cpu_power_ctrl(cpu, 1);
	timeout = jiffies + (10* HZ);
	while(meson_get_cpu_ctrl_addr(cpu));
	{
		if(!time_before(jiffies, timeout))
			return -EPERM;
	}
#endif

	meson_secondary_set(cpu);
	dsb_sev();

//	smp_send_reschedule(cpu);
	timeout = jiffies + (10* HZ);
	while (time_before(jiffies, timeout)) {
		smp_rmb();
		if (pen_release == -1)
			break;
		udelay(10);
	}

	/*
	 * now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	spin_unlock(&boot_lock);
	return pen_release != -1 ? -ENOSYS : 0;
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system. The msm8x60
 * does not support the ARM SCU, so just set the possible cpu mask to
 * NR_CPUS.
 */
void __init meson_smp_init_cpus(void)
{
	unsigned int i;

	for (i = 0; i < NR_CPUS; i++)
		set_cpu_possible(i, true);

	 set_smp_cross_call(gic_raise_softirq);
}

void __init meson_smp_prepare_cpus(unsigned int max_cpus)
{
	int i;

	/*
	* Initialise the present map, which describes the set of CPUs
	* actually populated at the present time.
	*/
	for (i = 0; i < max_cpus; i++)
		set_cpu_present(i, true);
	/*
	* Initialise the SCU and wake up the secondary core using
	* wakeup_secondary().
	*/
	scu_enable((void __iomem *) IO_PERIPH_BASE);
}

struct smp_operations meson_smp_ops __initdata = {
	.smp_init_cpus		= meson_smp_init_cpus,
	.smp_prepare_cpus	= meson_smp_prepare_cpus,
	.smp_secondary_init	= meson_secondary_init,
	.smp_boot_secondary	= meson_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= meson_cpu_die,
	.cpu_kill         =meson_cpu_kill,
	.cpu_disable    =meson_cpu_disable,
#endif
};

#ifdef CONFIG_SMP
static DEFINE_PER_CPU(unsigned long, in_wait);
static unsigned long timeout_flag;
extern void cpu_maps_update_begin(void);
extern void cpu_maps_update_done(void);

typedef enum _ENUM_SMP_FLAG {
    SMP_FLAG_IDLE = 0,
    SMP_FLAG_GETED,
    SMP_FLAG_FINISHED,
    SMP_FLAG_NUM
} ENUM_SCAL_FLAG;

static void smp_wait(void * info)
{
/*This function is call under automic context. So, need not irq protect.*/
	unsigned int cpu;

	cpu = smp_processor_id();

	info = info;

	per_cpu(in_wait, cpu) = SMP_FLAG_GETED;

	printk("cpu%d stall.\n", cpu);
	while((per_cpu(in_wait, cpu) == SMP_FLAG_GETED) && !timeout_flag)//waiting until flag != SMP_FLAG_GETED
		cpu_relax();

	return;
}

/*
Try exclusive cpu run func, the others wait it for finishing.
 If try fail, you can try again.
 NOTE: It need call at non-automatic context, because of mutex_lock @ cpu_maps_update_begin*/
int try_exclu_cpu_exe(exl_call_func_t func, void * p_arg)
{
	unsigned int cpu;
	unsigned long irq_flags;
	unsigned long jiffy_timeout;
	unsigned long count=0;
	int ret;
	/*Protect hotplug scenary*/
	cpu_maps_update_begin();

	timeout_flag = 0; // clean timeout flag;

	for(cpu=0; cpu< CONFIG_NR_CPUS; cpu++)
		if(per_cpu(in_wait, cpu))
		{
			printk("The previous call is not complete yet!\n");
			ret = -1;
			goto finish2;
		}

	smp_call_function(/*(void (*) (void * info))*/smp_wait, NULL, 0);

	irq_flags = arch_local_irq_save();

	jiffy_timeout = jiffies + HZ/2; //0.5s
	while(count+1 != num_online_cpus())//the other cpus all in wait loop when count+1 == num_online_cpus()
	{
		if(time_after(jiffies, jiffy_timeout))
		{
			printk("Cannot stall other cpus. Timeout!\n");

			timeout_flag = 1;

			ret = -1;
			goto finish1;
		}

		for(cpu=0, count=0; cpu< CONFIG_NR_CPUS; cpu++)
			if(per_cpu(in_wait, cpu) == SMP_FLAG_GETED)
				count ++;
	}

	ret = func(p_arg);

finish1:
	for(cpu=0; cpu< CONFIG_NR_CPUS; cpu++)
		per_cpu(in_wait, cpu) = SMP_FLAG_IDLE;

	arch_local_irq_restore(irq_flags);

finish2:
	cpu_maps_update_done();
	return ret;
}
#else//CONFIG_SMP
int try_exclu_cpu_exe(exl_call_func_t func, void * p_arg)
{
	return func(p_arg);
}
#endif

