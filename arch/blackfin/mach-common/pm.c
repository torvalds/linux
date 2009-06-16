/*
 * File:         arch/blackfin/mach-common/pm.c
 * Based on:     arm/mach-omap/pm.c
 * Author:       Cliff Brake <cbrake@accelent.com> Copyright (c) 2001
 *
 * Created:      2001
 * Description:  Blackfin power management
 *
 * Modified:     Nicolas Pitre - PXA250 support
 *                Copyright (c) 2002 Monta Vista Software, Inc.
 *               David Singleton - OMAP1510
 *                Copyright (c) 2002 Monta Vista Software, Inc.
 *               Dirk Behme <dirk.behme@de.bosch.com> - OMAP1510/1610
 *                Copyright 2004
 *               Copyright 2004-2008 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/suspend.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/io.h>
#include <linux/irq.h>

#include <asm/gpio.h>
#include <asm/dma.h>
#include <asm/dpmc.h>

#ifdef CONFIG_PM_WAKEUP_GPIO_POLAR_H
#define WAKEUP_TYPE	PM_WAKE_HIGH
#endif

#ifdef CONFIG_PM_WAKEUP_GPIO_POLAR_L
#define WAKEUP_TYPE	PM_WAKE_LOW
#endif

#ifdef CONFIG_PM_WAKEUP_GPIO_POLAR_EDGE_F
#define WAKEUP_TYPE	PM_WAKE_FALLING
#endif

#ifdef CONFIG_PM_WAKEUP_GPIO_POLAR_EDGE_R
#define WAKEUP_TYPE	PM_WAKE_RISING
#endif

#ifdef CONFIG_PM_WAKEUP_GPIO_POLAR_EDGE_B
#define WAKEUP_TYPE	PM_WAKE_BOTH_EDGES
#endif


void bfin_pm_suspend_standby_enter(void)
{
	unsigned long flags;

#ifdef CONFIG_PM_WAKEUP_BY_GPIO
	gpio_pm_wakeup_request(CONFIG_PM_WAKEUP_GPIO_NUMBER, WAKEUP_TYPE);
#endif

	local_irq_save_hw(flags);
	bfin_pm_standby_setup();

#ifdef CONFIG_PM_BFIN_SLEEP_DEEPER
	sleep_deeper(bfin_sic_iwr[0], bfin_sic_iwr[1], bfin_sic_iwr[2]);
#else
	sleep_mode(bfin_sic_iwr[0], bfin_sic_iwr[1], bfin_sic_iwr[2]);
#endif

	bfin_pm_standby_restore();

#ifdef SIC_IWR0
	bfin_write_SIC_IWR0(IWR_DISABLE_ALL);
# ifdef SIC_IWR1
	/* BF52x system reset does not properly reset SIC_IWR1 which
	 * will screw up the bootrom as it relies on MDMA0/1 waking it
	 * up from IDLE instructions.  See this report for more info:
	 * http://blackfin.uclinux.org/gf/tracker/4323
	 */
	if (ANOMALY_05000435)
		bfin_write_SIC_IWR1(IWR_ENABLE(10) | IWR_ENABLE(11));
	else
		bfin_write_SIC_IWR1(IWR_DISABLE_ALL);
# endif
# ifdef SIC_IWR2
	bfin_write_SIC_IWR2(IWR_DISABLE_ALL);
# endif
#else
	bfin_write_SIC_IWR(IWR_DISABLE_ALL);
#endif

	local_irq_restore_hw(flags);
}

int bf53x_suspend_l1_mem(unsigned char *memptr)
{
	dma_memcpy(memptr, (const void *) L1_CODE_START, L1_CODE_LENGTH);
	dma_memcpy(memptr + L1_CODE_LENGTH, (const void *) L1_DATA_A_START,
			L1_DATA_A_LENGTH);
	dma_memcpy(memptr + L1_CODE_LENGTH + L1_DATA_A_LENGTH,
			(const void *) L1_DATA_B_START, L1_DATA_B_LENGTH);
	memcpy(memptr + L1_CODE_LENGTH + L1_DATA_A_LENGTH +
			L1_DATA_B_LENGTH, (const void *) L1_SCRATCH_START,
			L1_SCRATCH_LENGTH);

	return 0;
}

int bf53x_resume_l1_mem(unsigned char *memptr)
{
	dma_memcpy((void *) L1_CODE_START, memptr, L1_CODE_LENGTH);
	dma_memcpy((void *) L1_DATA_A_START, memptr + L1_CODE_LENGTH,
			L1_DATA_A_LENGTH);
	dma_memcpy((void *) L1_DATA_B_START, memptr + L1_CODE_LENGTH +
			L1_DATA_A_LENGTH, L1_DATA_B_LENGTH);
	memcpy((void *) L1_SCRATCH_START, memptr + L1_CODE_LENGTH +
			L1_DATA_A_LENGTH + L1_DATA_B_LENGTH, L1_SCRATCH_LENGTH);

	return 0;
}

#if defined(CONFIG_BFIN_EXTMEM_WRITEBACK) || defined(CONFIG_BFIN_L2_WRITEBACK)
static void flushinv_all_dcache(void)
{
	u32 way, bank, subbank, set;
	u32 status, addr;
	u32 dmem_ctl = bfin_read_DMEM_CONTROL();

	for (bank = 0; bank < 2; ++bank) {
		if (!(dmem_ctl & (1 << (DMC1_P - bank))))
			continue;

		for (way = 0; way < 2; ++way)
			for (subbank = 0; subbank < 4; ++subbank)
				for (set = 0; set < 64; ++set) {

					bfin_write_DTEST_COMMAND(
						way << 26 |
						bank << 23 |
						subbank << 16 |
						set << 5
					);
					CSYNC();
					status = bfin_read_DTEST_DATA0();

					/* only worry about valid/dirty entries */
					if ((status & 0x3) != 0x3)
						continue;

					/* construct the address using the tag */
					addr = (status & 0xFFFFC800) | (subbank << 12) | (set << 5);

					/* flush it */
					__asm__ __volatile__("FLUSHINV[%0];" : : "a"(addr));
				}
	}
}
#endif

static inline void dcache_disable(void)
{
#ifdef CONFIG_BFIN_DCACHE
	unsigned long ctrl;

#if defined(CONFIG_BFIN_EXTMEM_WRITEBACK) || defined(CONFIG_BFIN_L2_WRITEBACK)
	flushinv_all_dcache();
#endif
	SSYNC();
	ctrl = bfin_read_DMEM_CONTROL();
	ctrl &= ~ENDCPLB;
	bfin_write_DMEM_CONTROL(ctrl);
	SSYNC();
#endif
}

static inline void dcache_enable(void)
{
#ifdef CONFIG_BFIN_DCACHE
	unsigned long ctrl;
	SSYNC();
	ctrl = bfin_read_DMEM_CONTROL();
	ctrl |= ENDCPLB;
	bfin_write_DMEM_CONTROL(ctrl);
	SSYNC();
#endif
}

static inline void icache_disable(void)
{
#ifdef CONFIG_BFIN_ICACHE
	unsigned long ctrl;
	SSYNC();
	ctrl = bfin_read_IMEM_CONTROL();
	ctrl &= ~ENICPLB;
	bfin_write_IMEM_CONTROL(ctrl);
	SSYNC();
#endif
}

static inline void icache_enable(void)
{
#ifdef CONFIG_BFIN_ICACHE
	unsigned long ctrl;
	SSYNC();
	ctrl = bfin_read_IMEM_CONTROL();
	ctrl |= ENICPLB;
	bfin_write_IMEM_CONTROL(ctrl);
	SSYNC();
#endif
}

int bfin_pm_suspend_mem_enter(void)
{
	unsigned long flags;
	int wakeup, ret;

	unsigned char *memptr = kmalloc(L1_CODE_LENGTH + L1_DATA_A_LENGTH
					 + L1_DATA_B_LENGTH + L1_SCRATCH_LENGTH,
					  GFP_KERNEL);

	if (memptr == NULL) {
		panic("bf53x_suspend_l1_mem malloc failed");
		return -ENOMEM;
	}

	wakeup = bfin_read_VR_CTL() & ~FREQ;
	wakeup |= SCKELOW;

#ifdef CONFIG_PM_BFIN_WAKE_PH6
	wakeup |= PHYWE;
#endif
#ifdef CONFIG_PM_BFIN_WAKE_GP
	wakeup |= GPWE;
#endif

	local_irq_save_hw(flags);

	ret = blackfin_dma_suspend();

	if (ret) {
		local_irq_restore_hw(flags);
		kfree(memptr);
		return ret;
	}

	bfin_gpio_pm_hibernate_suspend();

	dcache_disable();
	icache_disable();
	bf53x_suspend_l1_mem(memptr);

	do_hibernate(wakeup | vr_wakeup);	/* Goodbye */

	bf53x_resume_l1_mem(memptr);

	icache_enable();
	dcache_enable();

	bfin_gpio_pm_hibernate_restore();
	blackfin_dma_resume();

	local_irq_restore_hw(flags);
	kfree(memptr);

	return 0;
}

/*
 *	bfin_pm_valid - Tell the PM core that we only support the standby sleep
 *			state
 *	@state:		suspend state we're checking.
 *
 */
static int bfin_pm_valid(suspend_state_t state)
{
	return (state == PM_SUSPEND_STANDBY
#if !(defined(BF533_FAMILY) || defined(CONFIG_BF561))
	/*
	 * On BF533/2/1:
	 * If we enter Hibernate the SCKE Pin is driven Low,
	 * so that the SDRAM enters Self Refresh Mode.
	 * However when the reset sequence that follows hibernate
	 * state is executed, SCKE is driven High, taking the
	 * SDRAM out of Self Refresh.
	 *
	 * If you reconfigure and access the SDRAM "very quickly",
	 * you are likely to avoid errors, otherwise the SDRAM
	 * start losing its contents.
	 * An external HW workaround is possible using logic gates.
	 */
	|| state == PM_SUSPEND_MEM
#endif
	);
}

/*
 *	bfin_pm_enter - Actually enter a sleep state.
 *	@state:		State we're entering.
 *
 */
static int bfin_pm_enter(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_STANDBY:
		bfin_pm_suspend_standby_enter();
		break;
	case PM_SUSPEND_MEM:
		bfin_pm_suspend_mem_enter();
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

struct platform_suspend_ops bfin_pm_ops = {
	.enter = bfin_pm_enter,
	.valid	= bfin_pm_valid,
};

static int __init bfin_pm_init(void)
{
	suspend_set_ops(&bfin_pm_ops);
	return 0;
}

__initcall(bfin_pm_init);
