#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <plat/io.h>
#include <mach/io.h>
#include <asm/smp_scu.h>
#include <asm/hardware/gic.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>
#include <asm/cacheflush.h>
#include <asm/mach-types.h>
#include "common.h"
#ifdef CONFIG_MESON_TRUSTZONE
#include <mach/meson-secure.h>
#endif
int meson_cpu_kill(unsigned int cpu)
{
	return 1;
}

/*
 * platform-specific code to shutdown a CPU
 *
 * Called with IRQs disabled
 */
void meson_cpu_die(unsigned int cpu)
{
	//aml_write_reg32((IO_AHB_BASE + 0x1ff80),0);
	meson_set_cpu_ctrl_reg(0);
	flush_cache_all();
	dsb();
	dmb();

#ifdef CONFIG_MESON_TRUSTZONE
	meson_smc1(TRUSTZONE_MON_CORE_OFF, 1);
#ifdef CONFIG_MESON6_SMP_HOTPLUG
extern  void v7_invalidate_dcache_all(void);
		v7_invalidate_dcache_all();
#endif

#else
	for (;;) {
		/*
		 * Execute WFI
		 */
	    pr_debug("CPU%u: Enter WFI\n", cpu);
	        __asm__ __volatile__ ("wfi" : : : "memory");


		if (smp_processor_id() == cpu) {
			/*
			 * OK, proper wakeup, we're done
			 */
			if((aml_read_reg32(IO_AHB_BASE + 0x1ff80)&0x3) == ((1 << cpu) | 1))
			{
#ifdef CONFIG_MESON6_SMP_HOTPLUG
				/*
				 * Need invalidate data cache because of disable cpu0 fw
				 */
				extern  void v7_invalidate_dcache_all(void);
				v7_invalidate_dcache_all();
#endif
				break;
			}
		}
		pr_debug("CPU%u: spurious wakeup call\n", cpu);
	}
#endif
}

int meson_cpu_disable(unsigned int cpu)
{
	/*
	 * we don't allow CPU 0 to be shutdown (it is still too special
	 * e.g. clock tick interrupts)
	 */
	return cpu == 0 ? -EPERM : 0;
}

