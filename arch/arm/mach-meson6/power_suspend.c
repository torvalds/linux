#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/spi/flash.h>
#include <mach/hardware.h>
#include <plat/platform.h>
#include <mach/memory.h>
#include <mach/clock.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/setup.h>
//#include <mach/lm.h>
#include <asm/memory.h>
#include <asm/mach/map.h>
#include <linux/i2c.h>
#include <linux/i2c-aml.h>
#include <mach/power_gate.h>
#include <linux/delay.h>
#include <mach/usbclock.h>
#include <mach/am_regs.h>
#include <linux/file.h>
#include <asm/cacheflush.h>
#include <plat/regops.h>
#include <linux/reboot.h>
#ifdef CONFIG_SUSPEND_WATCHDOG
#include <mach/watchdog.h>
#endif /* CONFIG_SUSPEND_WATCHDOG */

//appf functions
#define APPF_INITIALIZE             0
#define APPF_POWER_DOWN_CPU         1
#define APPF_POWER_UP_CPUS          2
//appf flags
#define APPF_SAVE_PMU          (1<<0)
#define APPF_SAVE_TIMERS       (1<<1)
#define APPF_SAVE_VFP          (1<<2)
#define APPF_SAVE_DEBUG        (1<<3)
#define APPF_SAVE_L2           (1<<4)

#if 0
#ifdef CONFIG_HARDWARE_WATCHDOG
void disable_watchdog(void)
{
	printk(KERN_INFO "** disable watchdog\n");
    aml_write_reg32(P_WATCHDOG_RESET, 0);
    aml_clr_reg32_mask(P_WATCHDOG_TC,(1 << WATCHDOG_ENABLE_BIT));
}
void enable_watchdog(void)
{
	printk(KERN_INFO "** enable watchdog\n");
    aml_write_reg32(P_WATCHDOG_RESET, 0);
    aml_write_reg32(P_WATCHDOG_TC, 1 << WATCHDOG_ENABLE_BIT | 0x1FFFFF);//about 20sec
    
    aml_write_reg32(P_AO_RTI_STATUS_REG1, MESON_NORMAL_BOOT);
}
void reset_watchdog(void)
{
	//printk(KERN_INFO "** reset watchdog\n");
    aml_write_reg32(P_WATCHDOG_RESET, 0);	
}
#endif /* CONFIG_HARDWARE_WATCHDOG */
#endif

#ifdef CONFIG_MESON_TRUSTZONE
#include <mach/meson-secure.h>
#endif

#ifdef CONFIG_MESON_TRUSTZONE
int meson_power_suspend(void)
{
	static int test_flag = 0;
	unsigned addr;
	unsigned p_addr;
	void (*pwrtest_entry)(unsigned,unsigned,unsigned,unsigned);

	addr = 0x9FF04400;//entry.s start
	p_addr = (unsigned)__phys_to_virt(addr);
	meson_suspend_firmware();
	printk("back to normal OS.\n");

	return 0;
}
#else
int meson_power_suspend(void)
{
	static int test_flag = 0;
	unsigned addr;
	unsigned p_addr;
	void	(*pwrtest_entry)(unsigned,unsigned,unsigned,unsigned);

	flush_cache_all();

	addr = 0x9FF04400;//entry.s start
	p_addr = (unsigned)__phys_to_virt(addr);
	pwrtest_entry = (void (*)(unsigned,unsigned,unsigned,unsigned))p_addr;
	if(test_flag != 1234){
		test_flag = 1234;
		printk("initial appf\n");
		pwrtest_entry(APPF_INITIALIZE,0,0,0);
	}
#ifdef CONFIG_SUSPEND_WATCHDOG
	DISABLE_SUSPEND_WATCHDOG;
#endif
	printk("power down cpu --\n");
	pwrtest_entry(APPF_POWER_DOWN_CPU,0,0,APPF_SAVE_PMU|APPF_SAVE_VFP|APPF_SAVE_L2);
#ifdef CONFIG_SUSPEND_WATCHDOG
	ENABLE_SUSPEND_WATCHDOG;
#endif
	return 0;
}
#endif
