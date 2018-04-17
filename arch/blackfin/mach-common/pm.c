/*
 * Blackfin power management
 *
 * Copyright 2006-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2
 * based on arm/mach-omap/pm.c
 *    Copyright 2001, Cliff Brake <cbrake@accelent.com> and others
 */

#include <linux/suspend.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/delay.h>

#include <asm/cplb.h>
#include <asm/dma.h>
#include <asm/dpmc.h>
#include <asm/pm.h>
#include <asm/gpio.h>

#ifdef CONFIG_BF60x
struct bfin_cpu_pm_fns *bfin_cpu_pm;
#endif

void bfin_pm_suspend_standby_enter(void)
{
#if !BFIN_GPIO_PINT
	bfin_pm_standby_setup();
#endif

#ifdef CONFIG_BF60x
	bfin_cpu_pm->enter(PM_SUSPEND_STANDBY);
#else
# ifdef CONFIG_PM_BFIN_SLEEP_DEEPER
	sleep_deeper(bfin_sic_iwr[0], bfin_sic_iwr[1], bfin_sic_iwr[2]);
# else
	sleep_mode(bfin_sic_iwr[0], bfin_sic_iwr[1], bfin_sic_iwr[2]);
# endif
#endif

#if !BFIN_GPIO_PINT
	bfin_pm_standby_restore();
#endif

#ifndef CONFIG_BF60x
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

#endif
}

int bf53x_suspend_l1_mem(unsigned char *memptr)
{
	dma_memcpy_nocache(memptr, (const void *) L1_CODE_START,
			L1_CODE_LENGTH);
	dma_memcpy_nocache(memptr + L1_CODE_LENGTH,
			(const void *) L1_DATA_A_START, L1_DATA_A_LENGTH);
	dma_memcpy_nocache(memptr + L1_CODE_LENGTH + L1_DATA_A_LENGTH,
			(const void *) L1_DATA_B_START, L1_DATA_B_LENGTH);
	memcpy(memptr + L1_CODE_LENGTH + L1_DATA_A_LENGTH +
			L1_DATA_B_LENGTH, (const void *) L1_SCRATCH_START,
			L1_SCRATCH_LENGTH);

	return 0;
}

int bf53x_resume_l1_mem(unsigned char *memptr)
{
	dma_memcpy_nocache((void *) L1_CODE_START, memptr, L1_CODE_LENGTH);
	dma_memcpy_nocache((void *) L1_DATA_A_START, memptr + L1_CODE_LENGTH,
			L1_DATA_A_LENGTH);
	dma_memcpy_nocache((void *) L1_DATA_B_START, memptr + L1_CODE_LENGTH +
			L1_DATA_A_LENGTH, L1_DATA_B_LENGTH);
	memcpy((void *) L1_SCRATCH_START, memptr + L1_CODE_LENGTH +
			L1_DATA_A_LENGTH + L1_DATA_B_LENGTH, L1_SCRATCH_LENGTH);

	return 0;
}

#if defined(CONFIG_BFIN_EXTMEM_WRITEBACK) || defined(CONFIG_BFIN_L2_WRITEBACK)
# ifdef CONFIG_BF60x
__attribute__((l1_text))
# endif
static void flushinv_all_dcache(void)
{
	register u32 way, bank, subbank, set;
	register u32 status, addr;
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

int bfin_pm_suspend_mem_enter(void)
{
	int ret;
#ifndef CONFIG_BF60x
	int wakeup;
#endif

	unsigned char *memptr = kmalloc(L1_CODE_LENGTH + L1_DATA_A_LENGTH
					 + L1_DATA_B_LENGTH + L1_SCRATCH_LENGTH,
					  GFP_ATOMIC);

	if (memptr == NULL) {
		panic("bf53x_suspend_l1_mem malloc failed");
		return -ENOMEM;
	}

#ifndef CONFIG_BF60x
	wakeup = bfin_read_VR_CTL() & ~FREQ;
	wakeup |= SCKELOW;

#ifdef CONFIG_PM_BFIN_WAKE_PH6
	wakeup |= PHYWE;
#endif
#ifdef CONFIG_PM_BFIN_WAKE_GP
	wakeup |= GPWE;
#endif
#endif

	ret = blackfin_dma_suspend();

	if (ret) {
		kfree(memptr);
		return ret;
	}

#ifdef CONFIG_GPIO_ADI
	bfin_gpio_pm_hibernate_suspend();
#endif

#if defined(CONFIG_BFIN_EXTMEM_WRITEBACK) || defined(CONFIG_BFIN_L2_WRITEBACK)
	flushinv_all_dcache();
	udelay(1);
#endif
	_disable_dcplb();
	_disable_icplb();
	bf53x_suspend_l1_mem(memptr);

#ifndef CONFIG_BF60x
	do_hibernate(wakeup | vr_wakeup);	/* See you later! */
#else
	bfin_cpu_pm->enter(PM_SUSPEND_MEM);
#endif

	bf53x_resume_l1_mem(memptr);

	_enable_icplb();
	_enable_dcplb();

#ifdef CONFIG_GPIO_ADI
	bfin_gpio_pm_hibernate_restore();
#endif
	blackfin_dma_resume();

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

#ifdef CONFIG_BFIN_PM_WAKEUP_TIME_BENCH
void bfin_pm_end(void)
{
	u32 cycle, cycle2;
	u64 usec64;
	u32 usec;

	__asm__ __volatile__ (
		"1: %0 = CYCLES2\n"
		"%1 = CYCLES\n"
		"%2 = CYCLES2\n"
		"CC = %2 == %0\n"
		"if ! CC jump 1b\n"
		: "=d,a" (cycle2), "=d,a" (cycle), "=d,a" (usec) : : "CC"
	);

	usec64 = ((u64)cycle2 << 32) + cycle;
	do_div(usec64, get_cclk() / USEC_PER_SEC);
	usec = usec64;
	if (usec == 0)
		usec = 1;

	pr_info("PM: resume of kernel completes after  %ld msec %03ld usec\n",
		usec / USEC_PER_MSEC, usec % USEC_PER_MSEC);
}
#endif

static const struct platform_suspend_ops bfin_pm_ops = {
	.enter = bfin_pm_enter,
	.valid	= bfin_pm_valid,
#ifdef CONFIG_BFIN_PM_WAKEUP_TIME_BENCH
	.end = bfin_pm_end,
#endif
};

static int __init bfin_pm_init(void)
{
	suspend_set_ops(&bfin_pm_ops);
	return 0;
}

__initcall(bfin_pm_init);
