/*
 * linux/arch/arm/mach-omap1/pm.c
 *
 * OMAP Power Management Routines
 *
 * Original code for the SA11x0:
 * Copyright (c) 2001 Cliff Brake <cbrake@accelent.com>
 *
 * Modified for the PXA250 by Nicolas Pitre:
 * Copyright (c) 2002 Monta Vista Software, Inc.
 *
 * Modified for the OMAP1510 by David Singleton:
 * Copyright (c) 2002 Monta Vista Software, Inc.
 *
 * Cleanup 2004 for OMAP1510/1610 by Dirk Behme <dirk.behme@de.bosch.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/suspend.h>
#include <linux/sched.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/atomic.h>
#include <linux/cpu.h>

#include <asm/fncpy.h>
#include <asm/system_misc.h>
#include <asm/irq.h>
#include <asm/mach/time.h>
#include <asm/mach/irq.h>

#include <linux/soc/ti/omap1-io.h>
#include "tc.h"
#include <linux/omap-dma.h>
#include <clocksource/timer-ti-dm.h>

#include "hardware.h"
#include "mux.h"
#include "irqs.h"
#include "iomap.h"
#include "clock.h"
#include "pm.h"
#include "soc.h"
#include "sram.h"

static unsigned int arm_sleep_save[ARM_SLEEP_SAVE_SIZE];
static unsigned short dsp_sleep_save[DSP_SLEEP_SAVE_SIZE];
static unsigned short ulpd_sleep_save[ULPD_SLEEP_SAVE_SIZE];
static unsigned int mpui1510_sleep_save[MPUI1510_SLEEP_SAVE_SIZE];
static unsigned int mpui1610_sleep_save[MPUI1610_SLEEP_SAVE_SIZE];

static unsigned short enable_dyn_sleep;

static ssize_t idle_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	return sprintf(buf, "%hu\n", enable_dyn_sleep);
}

static ssize_t idle_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char * buf, size_t n)
{
	unsigned short value;
	if (sscanf(buf, "%hu", &value) != 1 ||
	    (value != 0 && value != 1) ||
	    (value != 0 && !IS_ENABLED(CONFIG_OMAP_32K_TIMER))) {
		pr_err("idle_sleep_store: Invalid value\n");
		return -EINVAL;
	}
	enable_dyn_sleep = value;
	return n;
}

static struct kobj_attribute sleep_while_idle_attr =
	__ATTR(sleep_while_idle, 0644, idle_show, idle_store);


static void (*omap_sram_suspend)(unsigned long r0, unsigned long r1) = NULL;

/*
 * Let's power down on idle, but only if we are really
 * idle, because once we start down the path of
 * going idle we continue to do idle even if we get
 * a clock tick interrupt . .
 */
void omap1_pm_idle(void)
{
	extern __u32 arm_idlect1_mask;
	__u32 use_idlect1 = arm_idlect1_mask;

	local_fiq_disable();

#if defined(CONFIG_OMAP_MPU_TIMER) && !defined(CONFIG_OMAP_DM_TIMER)
	use_idlect1 = use_idlect1 & ~(1 << 9);
#endif

#ifdef CONFIG_OMAP_DM_TIMER
	use_idlect1 = omap_dm_timer_modify_idlect_mask(use_idlect1);
#endif

	if (omap_dma_running())
		use_idlect1 &= ~(1 << 6);

	/*
	 * We should be able to remove the do_sleep variable and multiple
	 * tests above as soon as drivers, timer and DMA code have been fixed.
	 * Even the sleep block count should become obsolete.
	 */
	if ((use_idlect1 != ~0) || !enable_dyn_sleep) {

		__u32 saved_idlect1 = omap_readl(ARM_IDLECT1);
		if (cpu_is_omap15xx())
			use_idlect1 &= OMAP1510_BIG_SLEEP_REQUEST;
		else
			use_idlect1 &= OMAP1610_IDLECT1_SLEEP_VAL;
		omap_writel(use_idlect1, ARM_IDLECT1);
		__asm__ volatile ("mcr	p15, 0, r0, c7, c0, 4");
		omap_writel(saved_idlect1, ARM_IDLECT1);

		local_fiq_enable();
		return;
	}
	omap_sram_suspend(omap_readl(ARM_IDLECT1),
			  omap_readl(ARM_IDLECT2));

	local_fiq_enable();
}

/*
 * Configuration of the wakeup event is board specific. For the
 * moment we put it into this helper function. Later it may move
 * to board specific files.
 */
static void omap_pm_wakeup_setup(void)
{
	u32 level1_wake = 0;
	u32 level2_wake = OMAP_IRQ_BIT(INT_UART2);

	/*
	 * Turn off all interrupts except GPIO bank 1, L1-2nd level cascade,
	 * and the L2 wakeup interrupts: keypad and UART2. Note that the
	 * drivers must still separately call omap_set_gpio_wakeup() to
	 * wake up to a GPIO interrupt.
	 */
	if (cpu_is_omap15xx())
		level1_wake = OMAP_IRQ_BIT(INT_GPIO_BANK1) |
			OMAP_IRQ_BIT(INT_1510_IH2_IRQ);
	else if (cpu_is_omap16xx())
		level1_wake = OMAP_IRQ_BIT(INT_GPIO_BANK1) |
			OMAP_IRQ_BIT(INT_1610_IH2_IRQ);

	omap_writel(~level1_wake, OMAP_IH1_MIR);

	if (cpu_is_omap15xx()) {
		level2_wake |= OMAP_IRQ_BIT(INT_KEYBOARD);
		omap_writel(~level2_wake,  OMAP_IH2_MIR);
	} else if (cpu_is_omap16xx()) {
		level2_wake |= OMAP_IRQ_BIT(INT_KEYBOARD);
		omap_writel(~level2_wake, OMAP_IH2_0_MIR);

		/* INT_1610_WAKE_UP_REQ is needed for GPIO wakeup... */
		omap_writel(~OMAP_IRQ_BIT(INT_1610_WAKE_UP_REQ),
			    OMAP_IH2_1_MIR);
		omap_writel(~0x0, OMAP_IH2_2_MIR);
		omap_writel(~0x0, OMAP_IH2_3_MIR);
	}

	/*  New IRQ agreement, recalculate in cascade order */
	omap_writel(1, OMAP_IH2_CONTROL);
	omap_writel(1, OMAP_IH1_CONTROL);
}

#define EN_DSPCK	13	/* ARM_CKCTL */
#define EN_APICK	6	/* ARM_IDLECT2 */
#define DSP_EN		1	/* ARM_RSTCT1 */

void omap1_pm_suspend(void)
{
	unsigned long arg0 = 0, arg1 = 0;

	printk(KERN_INFO "PM: OMAP%x is trying to enter deep sleep...\n",
		omap_rev());

	omap_serial_wake_trigger(1);

	if (!cpu_is_omap15xx())
		omap_writew(0xffff, ULPD_SOFT_DISABLE_REQ_REG);

	/*
	 * Step 1: turn off interrupts (FIXME: NOTE: already disabled)
	 */

	local_irq_disable();
	local_fiq_disable();

	/*
	 * Step 2: save registers
	 *
	 * The omap is a strange/beautiful device. The caches, memory
	 * and register state are preserved across power saves.
	 * We have to save and restore very little register state to
	 * idle the omap.
         *
	 * Save interrupt, MPUI, ARM and UPLD control registers.
	 */

	if (cpu_is_omap15xx()) {
		MPUI1510_SAVE(OMAP_IH1_MIR);
		MPUI1510_SAVE(OMAP_IH2_MIR);
		MPUI1510_SAVE(MPUI_CTRL);
		MPUI1510_SAVE(MPUI_DSP_BOOT_CONFIG);
		MPUI1510_SAVE(MPUI_DSP_API_CONFIG);
		MPUI1510_SAVE(EMIFS_CONFIG);
		MPUI1510_SAVE(EMIFF_SDRAM_CONFIG);
	} else if (cpu_is_omap16xx()) {
		MPUI1610_SAVE(OMAP_IH1_MIR);
		MPUI1610_SAVE(OMAP_IH2_0_MIR);
		MPUI1610_SAVE(OMAP_IH2_1_MIR);
		MPUI1610_SAVE(OMAP_IH2_2_MIR);
		MPUI1610_SAVE(OMAP_IH2_3_MIR);
		MPUI1610_SAVE(MPUI_CTRL);
		MPUI1610_SAVE(MPUI_DSP_BOOT_CONFIG);
		MPUI1610_SAVE(MPUI_DSP_API_CONFIG);
		MPUI1610_SAVE(EMIFS_CONFIG);
		MPUI1610_SAVE(EMIFF_SDRAM_CONFIG);
	}

	ARM_SAVE(ARM_CKCTL);
	ARM_SAVE(ARM_IDLECT1);
	ARM_SAVE(ARM_IDLECT2);
	if (!(cpu_is_omap15xx()))
		ARM_SAVE(ARM_IDLECT3);
	ARM_SAVE(ARM_EWUPCT);
	ARM_SAVE(ARM_RSTCT1);
	ARM_SAVE(ARM_RSTCT2);
	ARM_SAVE(ARM_SYSST);
	ULPD_SAVE(ULPD_CLOCK_CTRL);
	ULPD_SAVE(ULPD_STATUS_REQ);

	/* (Step 3 removed - we now allow deep sleep by default) */

	/*
	 * Step 4: OMAP DSP Shutdown
	 */

	/* stop DSP */
	omap_writew(omap_readw(ARM_RSTCT1) & ~(1 << DSP_EN), ARM_RSTCT1);

	/* shut down dsp_ck */
	omap_writew(omap_readw(ARM_CKCTL) & ~(1 << EN_DSPCK), ARM_CKCTL);

	/* temporarily enabling api_ck to access DSP registers */
	omap_writew(omap_readw(ARM_IDLECT2) | 1 << EN_APICK, ARM_IDLECT2);

	/* save DSP registers */
	DSP_SAVE(DSP_IDLECT2);

	/* Stop all DSP domain clocks */
	__raw_writew(0, DSP_IDLECT2);

	/*
	 * Step 5: Wakeup Event Setup
	 */

	omap_pm_wakeup_setup();

	/*
	 * Step 6: ARM and Traffic controller shutdown
	 */

	/* disable ARM watchdog */
	omap_writel(0x00F5, OMAP_WDT_TIMER_MODE);
	omap_writel(0x00A0, OMAP_WDT_TIMER_MODE);

	/*
	 * Step 6b: ARM and Traffic controller shutdown
	 *
	 * Step 6 continues here. Prepare jump to power management
	 * assembly code in internal SRAM.
	 *
	 * Since the omap_cpu_suspend routine has been copied to
	 * SRAM, we'll do an indirect procedure call to it and pass the
	 * contents of arm_idlect1 and arm_idlect2 so it can restore
	 * them when it wakes up and it will return.
	 */

	arg0 = arm_sleep_save[ARM_SLEEP_SAVE_ARM_IDLECT1];
	arg1 = arm_sleep_save[ARM_SLEEP_SAVE_ARM_IDLECT2];

	/*
	 * Step 6c: ARM and Traffic controller shutdown
	 *
	 * Jump to assembly code. The processor will stay there
	 * until wake up.
	 */
	omap_sram_suspend(arg0, arg1);

	/*
	 * If we are here, processor is woken up!
	 */

	/*
	 * Restore DSP clocks
	 */

	/* again temporarily enabling api_ck to access DSP registers */
	omap_writew(omap_readw(ARM_IDLECT2) | 1 << EN_APICK, ARM_IDLECT2);

	/* Restore DSP domain clocks */
	DSP_RESTORE(DSP_IDLECT2);

	/*
	 * Restore ARM state, except ARM_IDLECT1/2 which omap_cpu_suspend did
	 */

	if (!(cpu_is_omap15xx()))
		ARM_RESTORE(ARM_IDLECT3);
	ARM_RESTORE(ARM_CKCTL);
	ARM_RESTORE(ARM_EWUPCT);
	ARM_RESTORE(ARM_RSTCT1);
	ARM_RESTORE(ARM_RSTCT2);
	ARM_RESTORE(ARM_SYSST);
	ULPD_RESTORE(ULPD_CLOCK_CTRL);
	ULPD_RESTORE(ULPD_STATUS_REQ);

	if (cpu_is_omap15xx()) {
		MPUI1510_RESTORE(MPUI_CTRL);
		MPUI1510_RESTORE(MPUI_DSP_BOOT_CONFIG);
		MPUI1510_RESTORE(MPUI_DSP_API_CONFIG);
		MPUI1510_RESTORE(EMIFS_CONFIG);
		MPUI1510_RESTORE(EMIFF_SDRAM_CONFIG);
		MPUI1510_RESTORE(OMAP_IH1_MIR);
		MPUI1510_RESTORE(OMAP_IH2_MIR);
	} else if (cpu_is_omap16xx()) {
		MPUI1610_RESTORE(MPUI_CTRL);
		MPUI1610_RESTORE(MPUI_DSP_BOOT_CONFIG);
		MPUI1610_RESTORE(MPUI_DSP_API_CONFIG);
		MPUI1610_RESTORE(EMIFS_CONFIG);
		MPUI1610_RESTORE(EMIFF_SDRAM_CONFIG);

		MPUI1610_RESTORE(OMAP_IH1_MIR);
		MPUI1610_RESTORE(OMAP_IH2_0_MIR);
		MPUI1610_RESTORE(OMAP_IH2_1_MIR);
		MPUI1610_RESTORE(OMAP_IH2_2_MIR);
		MPUI1610_RESTORE(OMAP_IH2_3_MIR);
	}

	if (!cpu_is_omap15xx())
		omap_writew(0, ULPD_SOFT_DISABLE_REQ_REG);

	/*
	 * Re-enable interrupts
	 */

	local_irq_enable();
	local_fiq_enable();

	omap_serial_wake_trigger(0);

	printk(KERN_INFO "PM: OMAP%x is re-starting from deep sleep...\n",
		omap_rev());
}

#ifdef CONFIG_DEBUG_FS
/*
 * Read system PM registers for debugging
 */
static int omap_pm_debug_show(struct seq_file *m, void *v)
{
	ARM_SAVE(ARM_CKCTL);
	ARM_SAVE(ARM_IDLECT1);
	ARM_SAVE(ARM_IDLECT2);
	if (!(cpu_is_omap15xx()))
		ARM_SAVE(ARM_IDLECT3);
	ARM_SAVE(ARM_EWUPCT);
	ARM_SAVE(ARM_RSTCT1);
	ARM_SAVE(ARM_RSTCT2);
	ARM_SAVE(ARM_SYSST);

	ULPD_SAVE(ULPD_IT_STATUS);
	ULPD_SAVE(ULPD_CLOCK_CTRL);
	ULPD_SAVE(ULPD_SOFT_REQ);
	ULPD_SAVE(ULPD_STATUS_REQ);
	ULPD_SAVE(ULPD_DPLL_CTRL);
	ULPD_SAVE(ULPD_POWER_CTRL);

	if (cpu_is_omap15xx()) {
		MPUI1510_SAVE(MPUI_CTRL);
		MPUI1510_SAVE(MPUI_DSP_STATUS);
		MPUI1510_SAVE(MPUI_DSP_BOOT_CONFIG);
		MPUI1510_SAVE(MPUI_DSP_API_CONFIG);
		MPUI1510_SAVE(EMIFF_SDRAM_CONFIG);
		MPUI1510_SAVE(EMIFS_CONFIG);
	} else if (cpu_is_omap16xx()) {
		MPUI1610_SAVE(MPUI_CTRL);
		MPUI1610_SAVE(MPUI_DSP_STATUS);
		MPUI1610_SAVE(MPUI_DSP_BOOT_CONFIG);
		MPUI1610_SAVE(MPUI_DSP_API_CONFIG);
		MPUI1610_SAVE(EMIFF_SDRAM_CONFIG);
		MPUI1610_SAVE(EMIFS_CONFIG);
	}

	seq_printf(m,
		   "ARM_CKCTL_REG:            0x%-8x     \n"
		   "ARM_IDLECT1_REG:          0x%-8x     \n"
		   "ARM_IDLECT2_REG:          0x%-8x     \n"
		   "ARM_IDLECT3_REG:	      0x%-8x     \n"
		   "ARM_EWUPCT_REG:           0x%-8x     \n"
		   "ARM_RSTCT1_REG:           0x%-8x     \n"
		   "ARM_RSTCT2_REG:           0x%-8x     \n"
		   "ARM_SYSST_REG:            0x%-8x     \n"
		   "ULPD_IT_STATUS_REG:       0x%-4x     \n"
		   "ULPD_CLOCK_CTRL_REG:      0x%-4x     \n"
		   "ULPD_SOFT_REQ_REG:        0x%-4x     \n"
		   "ULPD_DPLL_CTRL_REG:       0x%-4x     \n"
		   "ULPD_STATUS_REQ_REG:      0x%-4x     \n"
		   "ULPD_POWER_CTRL_REG:      0x%-4x     \n",
		   ARM_SHOW(ARM_CKCTL),
		   ARM_SHOW(ARM_IDLECT1),
		   ARM_SHOW(ARM_IDLECT2),
		   ARM_SHOW(ARM_IDLECT3),
		   ARM_SHOW(ARM_EWUPCT),
		   ARM_SHOW(ARM_RSTCT1),
		   ARM_SHOW(ARM_RSTCT2),
		   ARM_SHOW(ARM_SYSST),
		   ULPD_SHOW(ULPD_IT_STATUS),
		   ULPD_SHOW(ULPD_CLOCK_CTRL),
		   ULPD_SHOW(ULPD_SOFT_REQ),
		   ULPD_SHOW(ULPD_DPLL_CTRL),
		   ULPD_SHOW(ULPD_STATUS_REQ),
		   ULPD_SHOW(ULPD_POWER_CTRL));

	if (cpu_is_omap15xx()) {
		seq_printf(m,
			   "MPUI1510_CTRL_REG             0x%-8x \n"
			   "MPUI1510_DSP_STATUS_REG:      0x%-8x \n"
			   "MPUI1510_DSP_BOOT_CONFIG_REG: 0x%-8x \n"
			   "MPUI1510_DSP_API_CONFIG_REG:  0x%-8x \n"
			   "MPUI1510_SDRAM_CONFIG_REG:    0x%-8x \n"
			   "MPUI1510_EMIFS_CONFIG_REG:    0x%-8x \n",
			   MPUI1510_SHOW(MPUI_CTRL),
			   MPUI1510_SHOW(MPUI_DSP_STATUS),
			   MPUI1510_SHOW(MPUI_DSP_BOOT_CONFIG),
			   MPUI1510_SHOW(MPUI_DSP_API_CONFIG),
			   MPUI1510_SHOW(EMIFF_SDRAM_CONFIG),
			   MPUI1510_SHOW(EMIFS_CONFIG));
	} else if (cpu_is_omap16xx()) {
		seq_printf(m,
			   "MPUI1610_CTRL_REG             0x%-8x \n"
			   "MPUI1610_DSP_STATUS_REG:      0x%-8x \n"
			   "MPUI1610_DSP_BOOT_CONFIG_REG: 0x%-8x \n"
			   "MPUI1610_DSP_API_CONFIG_REG:  0x%-8x \n"
			   "MPUI1610_SDRAM_CONFIG_REG:    0x%-8x \n"
			   "MPUI1610_EMIFS_CONFIG_REG:    0x%-8x \n",
			   MPUI1610_SHOW(MPUI_CTRL),
			   MPUI1610_SHOW(MPUI_DSP_STATUS),
			   MPUI1610_SHOW(MPUI_DSP_BOOT_CONFIG),
			   MPUI1610_SHOW(MPUI_DSP_API_CONFIG),
			   MPUI1610_SHOW(EMIFF_SDRAM_CONFIG),
			   MPUI1610_SHOW(EMIFS_CONFIG));
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(omap_pm_debug);

static void omap_pm_init_debugfs(void)
{
	struct dentry *d;

	d = debugfs_create_dir("pm_debug", NULL);
	debugfs_create_file("omap_pm", S_IWUSR | S_IRUGO, d, NULL,
			    &omap_pm_debug_fops);
}

#endif /* CONFIG_DEBUG_FS */

/*
 *	omap_pm_prepare - Do preliminary suspend work.
 *
 */
static int omap_pm_prepare(void)
{
	/* We cannot sleep in idle until we have resumed */
	cpu_idle_poll_ctrl(true);
	return 0;
}


/*
 *	omap_pm_enter - Actually enter a sleep state.
 *	@state:		State we're entering.
 *
 */

static int omap_pm_enter(suspend_state_t state)
{
	switch (state)
	{
	case PM_SUSPEND_MEM:
		omap1_pm_suspend();
		break;
	default:
		return -EINVAL;
	}

	return 0;
}


/**
 *	omap_pm_finish - Finish up suspend sequence.
 *
 *	This is called after we wake back up (or if entering the sleep state
 *	failed).
 */

static void omap_pm_finish(void)
{
	cpu_idle_poll_ctrl(false);
}


static irqreturn_t omap_wakeup_interrupt(int irq, void *dev)
{
	return IRQ_HANDLED;
}



static const struct platform_suspend_ops omap_pm_ops = {
	.prepare	= omap_pm_prepare,
	.enter		= omap_pm_enter,
	.finish		= omap_pm_finish,
	.valid		= suspend_valid_only_mem,
};

static int __init omap_pm_init(void)
{
	int error = 0;
	int irq;

	if (!cpu_class_is_omap1())
		return -ENODEV;

	pr_info("Power Management for TI OMAP.\n");

	if (!IS_ENABLED(CONFIG_OMAP_32K_TIMER))
		pr_info("OMAP1 PM: sleep states in idle disabled due to no 32KiHz timer\n");

	if (!IS_ENABLED(CONFIG_OMAP_DM_TIMER))
		pr_info("OMAP1 PM: sleep states in idle disabled due to no DMTIMER support\n");

	if (IS_ENABLED(CONFIG_OMAP_32K_TIMER) &&
	    IS_ENABLED(CONFIG_OMAP_DM_TIMER)) {
		/* OMAP16xx only */
		pr_info("OMAP1 PM: sleep states in idle enabled\n");
		enable_dyn_sleep = 1;
	}

	/*
	 * We copy the assembler sleep/wakeup routines to SRAM.
	 * These routines need to be in SRAM as that's the only
	 * memory the MPU can see when it wakes up.
	 */
	if (cpu_is_omap15xx()) {
		omap_sram_suspend = omap_sram_push(omap1510_cpu_suspend,
						   omap1510_cpu_suspend_sz);
	} else if (cpu_is_omap16xx()) {
		omap_sram_suspend = omap_sram_push(omap1610_cpu_suspend,
						   omap1610_cpu_suspend_sz);
	}

	if (omap_sram_suspend == NULL) {
		printk(KERN_ERR "PM not initialized: Missing SRAM support\n");
		return -ENODEV;
	}

	arm_pm_idle = omap1_pm_idle;

	if (cpu_is_omap16xx())
		irq = INT_1610_WAKE_UP_REQ;
	else
		irq = -1;

	if (irq >= 0) {
		if (request_irq(irq, omap_wakeup_interrupt, 0, "peripheral wakeup", NULL))
			pr_err("Failed to request irq %d (peripheral wakeup)\n", irq);
	}

	/* Program new power ramp-up time
	 * (0 for most boards since we don't lower voltage when in deep sleep)
	 */
	omap_writew(ULPD_SETUP_ANALOG_CELL_3_VAL, ULPD_SETUP_ANALOG_CELL_3);

	/* Setup ULPD POWER_CTRL_REG - enter deep sleep whenever possible */
	omap_writew(ULPD_POWER_CTRL_REG_VAL, ULPD_POWER_CTRL);

	/* Configure IDLECT3 */
	if (cpu_is_omap16xx())
		omap_writel(OMAP1610_IDLECT3_VAL, OMAP1610_IDLECT3);

	suspend_set_ops(&omap_pm_ops);

#ifdef CONFIG_DEBUG_FS
	omap_pm_init_debugfs();
#endif

	error = sysfs_create_file(power_kobj, &sleep_while_idle_attr.attr);
	if (error)
		pr_err("sysfs_create_file failed: %d\n", error);

	if (cpu_is_omap16xx()) {
		/* configure LOW_PWR pin */
		omap_cfg_reg(T20_1610_LOW_PWR);
	}

	return error;
}
__initcall(omap_pm_init);
