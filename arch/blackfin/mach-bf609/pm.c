/*
 * Blackfin bf609 power management
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2
 */

#include <linux/suspend.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/syscore_ops.h>

#include <asm/dpmc.h>
#include <asm/pm.h>
#include <mach/pm.h>
#include <asm/blackfin.h>
#include <asm/mem_init.h>

/***********************************************************/
/*                                                         */
/* Wakeup Actions for DPM_RESTORE                          */
/*                                                         */
/***********************************************************/
#define BITP_ROM_WUA_CHKHDR             24
#define BITP_ROM_WUA_DDRLOCK            7
#define BITP_ROM_WUA_DDRDLLEN           6
#define BITP_ROM_WUA_DDR                5
#define BITP_ROM_WUA_CGU                4
#define BITP_ROM_WUA_MEMBOOT            2
#define BITP_ROM_WUA_EN                 1

#define BITM_ROM_WUA_CHKHDR             (0xFF000000)
#define ENUM_ROM_WUA_CHKHDR_AD                  0xAD000000

#define BITM_ROM_WUA_DDRLOCK            (0x00000080)
#define BITM_ROM_WUA_DDRDLLEN           (0x00000040)
#define BITM_ROM_WUA_DDR                (0x00000020)
#define BITM_ROM_WUA_CGU                (0x00000010)
#define BITM_ROM_WUA_MEMBOOT            (0x00000002)
#define BITM_ROM_WUA_EN                 (0x00000001)

/***********************************************************/
/*                                                         */
/* Syscontrol                                              */
/*                                                         */
/***********************************************************/
#define BITP_ROM_SYSCTRL_CGU_LOCKINGEN  28    /* unlocks CGU_CTL register */
#define BITP_ROM_SYSCTRL_WUA_OVERRIDE   24
#define BITP_ROM_SYSCTRL_WUA_DDRDLLEN   20    /* Saves the DDR DLL and PADS registers to the DPM registers */
#define BITP_ROM_SYSCTRL_WUA_DDR        19    /* Saves the DDR registers to the DPM registers */
#define BITP_ROM_SYSCTRL_WUA_CGU        18    /* Saves the CGU registers into DPM registers */
#define BITP_ROM_SYSCTRL_WUA_DPMWRITE   17    /* Saves the Syscontrol structure structure contents into DPM registers */
#define BITP_ROM_SYSCTRL_WUA_EN         16    /* reads current PLL and DDR configuration into structure */
#define BITP_ROM_SYSCTRL_DDR_WRITE      13    /* writes the DDR registers from Syscontrol structure for wakeup initialization of DDR */
#define BITP_ROM_SYSCTRL_DDR_READ       12    /* Read the DDR registers into the Syscontrol structure for storing prior to hibernate */
#define BITP_ROM_SYSCTRL_CGU_AUTODIS    11    /* Disables auto handling of UPDT and ALGN fields */
#define BITP_ROM_SYSCTRL_CGU_CLKOUTSEL  7    /* access CGU_CLKOUTSEL register */
#define BITP_ROM_SYSCTRL_CGU_DIV        6    /* access CGU_DIV register */
#define BITP_ROM_SYSCTRL_CGU_STAT       5    /* access CGU_STAT register */
#define BITP_ROM_SYSCTRL_CGU_CTL        4    /* access CGU_CTL register */
#define BITP_ROM_SYSCTRL_CGU_RTNSTAT    2    /* Update structure STAT field upon error */
#define BITP_ROM_SYSCTRL_WRITE          1    /* write registers */
#define BITP_ROM_SYSCTRL_READ           0    /* read registers */

#define BITM_ROM_SYSCTRL_CGU_READ       (0x00000001)    /* Read CGU registers */
#define BITM_ROM_SYSCTRL_CGU_WRITE      (0x00000002)    /* Write registers */
#define BITM_ROM_SYSCTRL_CGU_RTNSTAT    (0x00000004)    /* Update structure STAT field upon error or after a write operation */
#define BITM_ROM_SYSCTRL_CGU_CTL        (0x00000010)    /* Access CGU_CTL register */
#define BITM_ROM_SYSCTRL_CGU_STAT       (0x00000020)    /* Access CGU_STAT register */
#define BITM_ROM_SYSCTRL_CGU_DIV        (0x00000040)    /* Access CGU_DIV register */
#define BITM_ROM_SYSCTRL_CGU_CLKOUTSEL  (0x00000080)    /* Access CGU_CLKOUTSEL register */
#define BITM_ROM_SYSCTRL_CGU_AUTODIS    (0x00000800)    /* Disables auto handling of UPDT and ALGN fields */
#define BITM_ROM_SYSCTRL_DDR_READ       (0x00001000)    /* Reads the contents of the DDR registers and stores them into the structure */
#define BITM_ROM_SYSCTRL_DDR_WRITE      (0x00002000)    /* Writes the DDR registers from the structure, only really intented for wakeup functionality and not for full DDR configuration */
#define BITM_ROM_SYSCTRL_WUA_EN         (0x00010000)    /* Wakeup entry or exit opertation enable */
#define BITM_ROM_SYSCTRL_WUA_DPMWRITE   (0x00020000)    /* When set indicates a restore of the PLL and DDR is to be performed otherwise a save is required */
#define BITM_ROM_SYSCTRL_WUA_CGU        (0x00040000)    /* Only applicable for a PLL and DDR save operation to the DPM, saves the current settings if cleared or the contents of the structure if set */
#define BITM_ROM_SYSCTRL_WUA_DDR        (0x00080000)    /* Only applicable for a PLL and DDR save operation to the DPM, saves the current settings if cleared or the contents of the structure if set */
#define BITM_ROM_SYSCTRL_WUA_DDRDLLEN   (0x00100000)    /* Enables saving/restoring of the DDR DLLCTL register */
#define BITM_ROM_SYSCTRL_WUA_OVERRIDE   (0x01000000)
#define BITM_ROM_SYSCTRL_CGU_LOCKINGEN  (0x10000000)    /* Unlocks the CGU_CTL register */


/* Structures for the syscontrol() function */
struct STRUCT_ROM_SYSCTRL {
	uint32_t ulCGU_CTL;
	uint32_t ulCGU_STAT;
	uint32_t ulCGU_DIV;
	uint32_t ulCGU_CLKOUTSEL;
	uint32_t ulWUA_Flags;
	uint32_t ulWUA_BootAddr;
	uint32_t ulWUA_User;
	uint32_t ulDDR_CTL;
	uint32_t ulDDR_CFG;
	uint32_t ulDDR_TR0;
	uint32_t ulDDR_TR1;
	uint32_t ulDDR_TR2;
	uint32_t ulDDR_MR;
	uint32_t ulDDR_EMR1;
	uint32_t ulDDR_EMR2;
	uint32_t ulDDR_PADCTL;
	uint32_t ulDDR_DLLCTL;
	uint32_t ulReserved;
};

struct bfin_pm_data {
	uint32_t magic;
	uint32_t resume_addr;
	uint32_t sp;
};

struct bfin_pm_data bf609_pm_data;

struct STRUCT_ROM_SYSCTRL configvalues;
uint32_t dactionflags;

#define FUNC_ROM_SYSCONTROL 0xC8000080
__attribute__((l1_data))
static uint32_t (* const bfrom_SysControl)(uint32_t action_flags, struct STRUCT_ROM_SYSCTRL *settings, void *reserved) = (void *)FUNC_ROM_SYSCONTROL;

__attribute__((l1_text))
void bfin_cpu_suspend(void)
{
	__asm__ __volatile__( \
			".align 8;" \
			"idle;" \
			: : \
			);
}

__attribute__((l1_text))
void bf609_ddr_sr(void)
{
	dmc_enter_self_refresh();
}

__attribute__((l1_text))
void bf609_ddr_sr_exit(void)
{
	dmc_exit_self_refresh();

	/* After wake up from deep sleep and exit DDR from self refress mode,
	 * should wait till CGU PLL is locked.
	 */
	while (bfin_read32(CGU0_STAT) & CLKSALGN)
		continue;
}

__attribute__((l1_text))
void bf609_resume_ccbuf(void)
{
	bfin_write32(DPM0_CCBF_EN, 3);
	bfin_write32(DPM0_CTL, 2);

	while ((bfin_read32(DPM0_STAT) & 0xf) != 1);
}

__attribute__((l1_text))
void bfin_hibernate_syscontrol(void)
{
	configvalues.ulWUA_Flags = (0xAD000000 | BITM_ROM_WUA_EN
		| BITM_ROM_WUA_CGU | BITM_ROM_WUA_DDR | BITM_ROM_WUA_DDRDLLEN);

	dactionflags = (BITM_ROM_SYSCTRL_WUA_EN
		| BITM_ROM_SYSCTRL_WUA_DPMWRITE | BITM_ROM_SYSCTRL_WUA_CGU
		| BITM_ROM_SYSCTRL_WUA_DDR | BITM_ROM_SYSCTRL_WUA_DDRDLLEN);

	bfrom_SysControl(dactionflags, &configvalues, NULL);

	bfin_write32(DPM0_RESTORE5, bfin_read32(DPM0_RESTORE5) | 4);
}

#ifndef CONFIG_BF60x
# define SIC_SYSIRQ(irq)	(irq - (IRQ_CORETMR + 1))
#else
# define SIC_SYSIRQ(irq)	((irq) - IVG15)
#endif
asmlinkage void enter_deepsleep(void);

__attribute__((l1_text))
void bfin_deepsleep(unsigned long mask)
{
	bfin_write32(DPM0_WAKE_EN, 0x10);
	bfin_write32(DPM0_WAKE_POL, 0x10);
	SSYNC();
	enter_deepsleep();
}

void bfin_hibernate(unsigned long mask)
{
	bfin_write32(DPM0_WAKE_EN, 0x10);
	bfin_write32(DPM0_WAKE_POL, 0x10);
	bfin_write32(DPM0_PGCNTR, 0x0000FFFF);
	bfin_write32(DPM0_HIB_DIS, 0xFFFF);

	bf609_hibernate();
}

void bf609_cpu_pm_enter(suspend_state_t state)
{
	int error;
	unsigned long wakeup = 0;
	unsigned long wakeup_pol = 0;

#ifdef CONFIG_PM_BFIN_WAKE_PA15
	wakeup |= PA15WE;
# if CONFIG_PM_BFIN_WAKE_PA15_POL
	wakeup_pol |= PA15WE;
# endif
#endif

#ifdef CONFIG_PM_BFIN_WAKE_PB15
	wakeup |= PB15WE;
# if CONFIG_PM_BFIN_WAKE_PA15_POL
	wakeup_pol |= PB15WE;
# endif
#endif

#ifdef CONFIG_PM_BFIN_WAKE_PC15
	wakeup |= PC15WE;
# if CONFIG_PM_BFIN_WAKE_PC15_POL
	wakeup_pol |= PC15WE;
# endif
#endif

#ifdef CONFIG_PM_BFIN_WAKE_PD06
	wakeup |= PD06WE;
# if CONFIG_PM_BFIN_WAKE_PD06_POL
	wakeup_pol |= PD06WE;
# endif
#endif

#ifdef CONFIG_PM_BFIN_WAKE_PE12
	wakeup |= PE12WE;
# if CONFIG_PM_BFIN_WAKE_PE12_POL
	wakeup_pol |= PE12WE;
# endif
#endif

#ifdef CONFIG_PM_BFIN_WAKE_PG04
	wakeup |= PG04WE;
# if CONFIG_PM_BFIN_WAKE_PG04_POL
	wakeup_pol |= PG04WE;
# endif
#endif

#ifdef CONFIG_PM_BFIN_WAKE_PG13
	wakeup |= PG13WE;
# if CONFIG_PM_BFIN_WAKE_PG13_POL
	wakeup_pol |= PG13WE;
# endif
#endif

#ifdef CONFIG_PM_BFIN_WAKE_USB
	wakeup |= USBWE;
# if CONFIG_PM_BFIN_WAKE_USB_POL
	wakeup_pol |= USBWE;
# endif
#endif

	error = irq_set_irq_wake(255, 1);
	if(error < 0)
		printk(KERN_DEBUG "Unable to get irq wake\n");
	error = irq_set_irq_wake(231, 1);
	if (error < 0)
		printk(KERN_DEBUG "Unable to get irq wake\n");

	if (state == PM_SUSPEND_STANDBY)
		bfin_deepsleep(wakeup);
	else {
		bfin_hibernate(wakeup);
	}

}

int bf609_cpu_pm_prepare(void)
{
	return 0;
}

void bf609_cpu_pm_finish(void)
{

}

static struct bfin_cpu_pm_fns bf609_cpu_pm = {
	.enter          = bf609_cpu_pm_enter,
	.prepare        = bf609_cpu_pm_prepare,
	.finish         = bf609_cpu_pm_finish,
};

#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
static void smc_pm_syscore_suspend(void)
{
	bf609_nor_flash_exit();
}

static void smc_pm_syscore_resume(void)
{
	bf609_nor_flash_init();
}

static struct syscore_ops smc_pm_syscore_ops = {
	.suspend        = smc_pm_syscore_suspend,
	.resume         = smc_pm_syscore_resume,
};
#endif

static irqreturn_t test_isr(int irq, void *dev_id)
{
	printk(KERN_DEBUG "gpio irq %d\n", irq);
	return IRQ_HANDLED;
}

static irqreturn_t dpm0_isr(int irq, void *dev_id)
{
	bfin_write32(DPM0_WAKE_STAT, bfin_read32(DPM0_WAKE_STAT));
	bfin_write32(CGU0_STAT, bfin_read32(CGU0_STAT));
	return IRQ_HANDLED;
}
#endif

static int __init bf609_init_pm(void)
{
	int irq;
	int error;

#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
	register_syscore_ops(&smc_pm_syscore_ops);
#endif

#ifdef CONFIG_PM_BFIN_WAKE_PE12
	irq = gpio_to_irq(GPIO_PE12);
	if (irq < 0) {
		error = irq;
		printk(KERN_DEBUG "Unable to get irq number for GPIO %d, error %d\n",
				GPIO_PE12, error);
	}

	error = request_irq(irq, test_isr, IRQF_TRIGGER_RISING | IRQF_NO_SUSPEND, "gpiope12", NULL);
	if(error < 0)
		printk(KERN_DEBUG "Unable to get irq\n");
#endif

	error = request_irq(IRQ_CGU_EVT, dpm0_isr, IRQF_NO_SUSPEND, "cgu0 event", NULL);
	if(error < 0)
		printk(KERN_DEBUG "Unable to get irq\n");

	error = request_irq(IRQ_DPM, dpm0_isr, IRQF_NO_SUSPEND, "dpm0 event", NULL);
	if (error < 0)
		printk(KERN_DEBUG "Unable to get irq\n");

	bfin_cpu_pm = &bf609_cpu_pm;
	return 0;
}

late_initcall(bf609_init_pm);
