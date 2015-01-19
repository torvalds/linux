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
#include <asm/smp_scu.h>
#include <asm/hardware/gic.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>
#include <asm/cacheflush.h>
#include <asm/mach-types.h>
#include <asm/cp15.h>

extern void meson_cleanup(void);

int meson_cpu_kill(unsigned int cpu)
{
	unsigned int value;
	unsigned int offset=(cpu<<3);
	do{
		udelay(10);
		value=aml_read_reg32(MESON_CPU_POWER_CTRL_REG);
	}while((value&(3<<offset)) != (3<<offset));

	udelay(10);
	meson_set_cpu_power_ctrl(cpu, 0);
	return 1;
}


void meson_cpu_die(unsigned int cpu)
{
	meson_set_cpu_ctrl_reg(cpu, 0);
	flush_cache_all();
	dsb();
	dmb();	

	meson_cleanup();
	aml_set_reg32_bits(MESON_CPU_POWER_CTRL_REG,0x3,(cpu << 3),2);
	asm volatile(
		"dsb\n"
		"wfi\n"
	);
	BUG();
}

int meson_cpu_disable(unsigned int cpu)
{
	/*
	 * we don't allow CPU 0 to be shutdown (it is still too special
	 * e.g. clock tick interrupts)
	 */
	return cpu == 0 ? -EPERM : 0;
}

