/*
 * linux/arch/arm/mach-omap/pm.c
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

#include <linux/pm.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/pm.h>

#include <asm/io.h>
#include <asm/mach-types.h>
#include <asm/arch/omap16xx.h>
#include <asm/arch/pm.h>
#include <asm/arch/mux.h>
#include <asm/arch/tc.h>
#include <asm/arch/tps65010.h>

#include "clock.h"

static unsigned int arm_sleep_save[ARM_SLEEP_SAVE_SIZE];
static unsigned short ulpd_sleep_save[ULPD_SLEEP_SAVE_SIZE];
static unsigned int mpui1510_sleep_save[MPUI1510_SLEEP_SAVE_SIZE];
static unsigned int mpui1610_sleep_save[MPUI1610_SLEEP_SAVE_SIZE];

/*
 * Let's power down on idle, but only if we are really
 * idle, because once we start down the path of
 * going idle we continue to do idle even if we get
 * a clock tick interrupt . .
 */
void omap_pm_idle(void)
{
	int (*func_ptr)(void) = 0;
	unsigned int mask32 = 0;

	/*
	 * If the DSP is being used let's just idle the CPU, the overhead
	 * to wake up from Big Sleep is big, milliseconds versus micro
	 * seconds for wait for interrupt.
	 */

	local_irq_disable();
	local_fiq_disable();
	if (need_resched()) {
		local_fiq_enable();
		local_irq_enable();
		return;
	}
	mask32 = omap_readl(ARM_SYSST);
	local_fiq_enable();
	local_irq_enable();

#if defined(CONFIG_OMAP_32K_TIMER) && defined(CONFIG_NO_IDLE_HZ)
	/* Override timer to use VST for the next cycle */
	omap_32k_timer_next_vst_interrupt();
#endif

	if ((mask32 & DSP_IDLE) == 0) {
		__asm__ volatile ("mcr	p15, 0, r0, c7, c0, 4");
	} else {

		if (cpu_is_omap1510()) {
			func_ptr = (void *)(OMAP1510_SRAM_IDLE_SUSPEND);
		} else if (cpu_is_omap1610() || cpu_is_omap1710()) {
			func_ptr = (void *)(OMAP1610_SRAM_IDLE_SUSPEND);
		} else if (cpu_is_omap5912()) {
			func_ptr = (void *)(OMAP5912_SRAM_IDLE_SUSPEND);
		}

		func_ptr();
	}
}

/*
 * Configuration of the wakeup event is board specific. For the
 * moment we put it into this helper function. Later it may move
 * to board specific files.
 */
static void omap_pm_wakeup_setup(void)
{
	/*
	 * Enable ARM XOR clock and release peripheral from reset by
	 * writing 1 to PER_EN bit in ARM_RSTCT2, this is required
	 * for UART configuration to use UART2 to wake up.
	 */

	omap_writel(omap_readl(ARM_IDLECT2) | ENABLE_XORCLK, ARM_IDLECT2);
	omap_writel(omap_readl(ARM_RSTCT2) | PER_EN, ARM_RSTCT2);
	omap_writew(MODEM_32K_EN, ULPD_CLOCK_CTRL);

	/*
	 * Turn off all interrupts except L1-2nd level cascade,
	 * and the L2 wakeup interrupts: keypad and UART2.
	 */

	omap_writel(~IRQ_LEVEL2, OMAP_IH1_MIR);

	if (cpu_is_omap1510()) {
		omap_writel(~(IRQ_UART2 | IRQ_KEYBOARD),  OMAP_IH2_MIR);
	}

	if (cpu_is_omap16xx()) {
		omap_writel(~(IRQ_UART2 | IRQ_KEYBOARD), OMAP_IH2_0_MIR);

		omap_writel(~0x0, OMAP_IH2_1_MIR);
		omap_writel(~0x0, OMAP_IH2_2_MIR);
		omap_writel(~0x0, OMAP_IH2_3_MIR);
	}

	/*  New IRQ agreement */
 	omap_writel(1, OMAP_IH1_CONTROL);

	/* external PULL to down, bit 22 = 0 */
	omap_writel(omap_readl(PULL_DWN_CTRL_2) & ~(1<<22), PULL_DWN_CTRL_2);
}

void omap_pm_suspend(void)
{
	unsigned int mask32 = 0;
	unsigned long arg0 = 0, arg1 = 0;
	int (*func_ptr)(unsigned short, unsigned short) = 0;
	unsigned short save_dsp_idlect2;

	printk("PM: OMAP%x is entering deep sleep now ...\n", system_rev);

	if (machine_is_omap_osk()) {
		/* Stop LED1 (D9) blink */
		tps65010_set_led(LED1, OFF);
	}

	/*
	 * Step 1: turn off interrupts
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

	if (cpu_is_omap1510()) {
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
	ARM_SAVE(ARM_EWUPCT);
	ARM_SAVE(ARM_RSTCT1);
	ARM_SAVE(ARM_RSTCT2);
	ARM_SAVE(ARM_SYSST);
	ULPD_SAVE(ULPD_CLOCK_CTRL);
	ULPD_SAVE(ULPD_STATUS_REQ);

	/*
	 * Step 3: LOW_PWR signal enabling
	 *
	 * Allow the LOW_PWR signal to be visible on MPUIO5 ball.
	 */
	if (cpu_is_omap1510()) {
		/* POWER_CTRL_REG = 0x1 (LOW_POWER is available) */
		omap_writew(omap_readw(ULPD_POWER_CTRL) |
			    OMAP1510_ULPD_LOW_POWER_REQ, ULPD_POWER_CTRL);
	} else if (cpu_is_omap16xx()) {
		/* POWER_CTRL_REG = 0x1 (LOW_POWER is available) */
		omap_writew(omap_readw(ULPD_POWER_CTRL) |
			    OMAP1610_ULPD_LOW_POWER_REQ, ULPD_POWER_CTRL);
	}

	/* configure LOW_PWR pin */
	omap_cfg_reg(T20_1610_LOW_PWR);

	/*
	 * Step 4: OMAP DSP Shutdown
	 */

	/* Set DSP_RST = 1 and DSP_EN = 0, put DSP block into reset */
	omap_writel((omap_readl(ARM_RSTCT1) | DSP_RST) & ~DSP_ENABLE,
		    ARM_RSTCT1);

	/* Set DSP boot mode to DSP-IDLE, DSP_BOOT_MODE = 0x2 */
        omap_writel(DSP_IDLE_MODE, MPUI_DSP_BOOT_CONFIG);

	/* Set EN_DSPCK = 0, stop DSP block clock */
	omap_writel(omap_readl(ARM_CKCTL) & ~DSP_CLOCK_ENABLE, ARM_CKCTL);

	/* Stop any DSP domain clocks */
	omap_writel(omap_readl(ARM_IDLECT2) | (1<<EN_APICK), ARM_IDLECT2);
	save_dsp_idlect2 = __raw_readw(DSP_IDLECT2);
	__raw_writew(0, DSP_IDLECT2);

	/*
	 * Step 5: Wakeup Event Setup
	 */

	omap_pm_wakeup_setup();

	/*
	 * Step 6a: ARM and Traffic controller shutdown
	 *
	 * Step 6 starts here with clock and watchdog disable
	 */

	/* stop clocks */
	mask32 = omap_readl(ARM_IDLECT2);
	mask32 &= ~(1<<EN_WDTCK);  /* bit 0 -> 0 (WDT clock) */
	mask32 |=  (1<<EN_XORPCK); /* bit 1 -> 1 (XORPCK clock) */
	mask32 &= ~(1<<EN_PERCK);  /* bit 2 -> 0 (MPUPER_CK clock) */
	mask32 &= ~(1<<EN_LCDCK);  /* bit 3 -> 0 (LCDC clock) */
	mask32 &= ~(1<<EN_LBCK);   /* bit 4 -> 0 (local bus clock) */
	mask32 |=  (1<<EN_APICK);  /* bit 6 -> 1 (MPUI clock) */
	mask32 &= ~(1<<EN_TIMCK);  /* bit 7 -> 0 (MPU timer clock) */
	mask32 &= ~(1<<DMACK_REQ); /* bit 8 -> 0 (DMAC clock) */
	mask32 &= ~(1<<EN_GPIOCK); /* bit 9 -> 0 (GPIO clock) */
	omap_writel(mask32, ARM_IDLECT2);

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

	if (cpu_is_omap1510()) {
		func_ptr = (void *)(OMAP1510_SRAM_API_SUSPEND);
	} else if (cpu_is_omap1610() || cpu_is_omap1710()) {
		func_ptr = (void *)(OMAP1610_SRAM_API_SUSPEND);
	} else if (cpu_is_omap5912()) {
		func_ptr = (void *)(OMAP5912_SRAM_API_SUSPEND);
	}

	/*
	 * Step 6c: ARM and Traffic controller shutdown
	 *
	 * Jump to assembly code. The processor will stay there
 	 * until wake up.
	 */

        func_ptr(arg0, arg1);

	/*
	 * If we are here, processor is woken up!
	 */

	if (cpu_is_omap1510()) {
		/* POWER_CTRL_REG = 0x0 (LOW_POWER is disabled) */
		omap_writew(omap_readw(ULPD_POWER_CTRL) &
			    ~OMAP1510_ULPD_LOW_POWER_REQ, ULPD_POWER_CTRL);
	} else if (cpu_is_omap16xx()) {
		/* POWER_CTRL_REG = 0x0 (LOW_POWER is disabled) */
		omap_writew(omap_readw(ULPD_POWER_CTRL) &
			    ~OMAP1610_ULPD_LOW_POWER_REQ, ULPD_POWER_CTRL);
	}


	/* Restore DSP clocks */
	omap_writel(omap_readl(ARM_IDLECT2) | (1<<EN_APICK), ARM_IDLECT2);
	__raw_writew(save_dsp_idlect2, DSP_IDLECT2);
	ARM_RESTORE(ARM_IDLECT2);

	/*
	 * Restore ARM state, except ARM_IDLECT1/2 which omap_cpu_suspend did
	 */

	ARM_RESTORE(ARM_CKCTL);
	ARM_RESTORE(ARM_EWUPCT);
	ARM_RESTORE(ARM_RSTCT1);
	ARM_RESTORE(ARM_RSTCT2);
	ARM_RESTORE(ARM_SYSST);
	ULPD_RESTORE(ULPD_CLOCK_CTRL);
	ULPD_RESTORE(ULPD_STATUS_REQ);

	if (cpu_is_omap1510()) {
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

	/*
	 * Reenable interrupts
	 */

	local_irq_enable();
	local_fiq_enable();

	printk("PM: OMAP%x is re-starting from deep sleep...\n", system_rev);

	if (machine_is_omap_osk()) {
		/* Let LED1 (D9) blink again */
		tps65010_set_led(LED1, BLINK);
	}
}

#if defined(DEBUG) && defined(CONFIG_PROC_FS)
static int g_read_completed;

/*
 * Read system PM registers for debugging
 */
static int omap_pm_read_proc(
	char *page_buffer,
	char **my_first_byte,
	off_t virtual_start,
	int length,
	int *eof,
	void *data)
{
	int my_buffer_offset = 0;
	char * const my_base = page_buffer;

	ARM_SAVE(ARM_CKCTL);
	ARM_SAVE(ARM_IDLECT1);
	ARM_SAVE(ARM_IDLECT2);
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

	if (cpu_is_omap1510()) {
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

	if (virtual_start == 0) {
		g_read_completed = 0;

		my_buffer_offset += sprintf(my_base + my_buffer_offset,
		   "ARM_CKCTL_REG:            0x%-8x     \n"
		   "ARM_IDLECT1_REG:          0x%-8x     \n"
		   "ARM_IDLECT2_REG:          0x%-8x     \n"
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

		if (cpu_is_omap1510()) {
			my_buffer_offset += sprintf(my_base + my_buffer_offset,
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
			my_buffer_offset += sprintf(my_base + my_buffer_offset,
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

		g_read_completed++;
	} else if (g_read_completed >= 1) {
		 *eof = 1;
		 return 0;
	}
	g_read_completed++;

	*my_first_byte = page_buffer;
	return  my_buffer_offset;
}

static void omap_pm_init_proc(void)
{
	struct proc_dir_entry *entry;

	entry = create_proc_read_entry("driver/omap_pm",
				       S_IWUSR | S_IRUGO, NULL,
				       omap_pm_read_proc, 0);
}

#endif /* DEBUG && CONFIG_PROC_FS */

/*
 *	omap_pm_prepare - Do preliminary suspend work.
 *	@state:		suspend state we're entering.
 *
 */
//#include <asm/arch/hardware.h>

static int omap_pm_prepare(suspend_state_t state)
{
	int error = 0;

	switch (state)
	{
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		break;

	case PM_SUSPEND_DISK:
		return -ENOTSUPP;

	default:
		return -EINVAL;
	}

	return error;
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
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		omap_pm_suspend();
		break;

	case PM_SUSPEND_DISK:
		return -ENOTSUPP;

	default:
		return -EINVAL;
	}

	return 0;
}


/**
 *	omap_pm_finish - Finish up suspend sequence.
 *	@state:		State we're coming out of.
 *
 *	This is called after we wake back up (or if entering the sleep state
 *	failed).
 */

static int omap_pm_finish(suspend_state_t state)
{
	return 0;
}


struct pm_ops omap_pm_ops ={
	.pm_disk_mode = 0,
        .prepare        = omap_pm_prepare,
        .enter          = omap_pm_enter,
        .finish         = omap_pm_finish,
};

static int __init omap_pm_init(void)
{
	printk("Power Management for TI OMAP.\n");
	pm_idle = omap_pm_idle;
	/*
	 * We copy the assembler sleep/wakeup routines to SRAM.
	 * These routines need to be in SRAM as that's the only
	 * memory the MPU can see when it wakes up.
	 */

#ifdef	CONFIG_ARCH_OMAP1510
	if (cpu_is_omap1510()) {
		memcpy((void *)OMAP1510_SRAM_IDLE_SUSPEND,
		       omap1510_idle_loop_suspend,
		       omap1510_idle_loop_suspend_sz);
		memcpy((void *)OMAP1510_SRAM_API_SUSPEND, omap1510_cpu_suspend,
		       omap1510_cpu_suspend_sz);
	} else
#endif
	if (cpu_is_omap1610() || cpu_is_omap1710()) {
		memcpy((void *)OMAP1610_SRAM_IDLE_SUSPEND,
		       omap1610_idle_loop_suspend,
		       omap1610_idle_loop_suspend_sz);
		memcpy((void *)OMAP1610_SRAM_API_SUSPEND, omap1610_cpu_suspend,
		       omap1610_cpu_suspend_sz);
	} else if (cpu_is_omap5912()) {
		memcpy((void *)OMAP5912_SRAM_IDLE_SUSPEND,
		       omap1610_idle_loop_suspend,
		       omap1610_idle_loop_suspend_sz);
		memcpy((void *)OMAP5912_SRAM_API_SUSPEND, omap1610_cpu_suspend,
		       omap1610_cpu_suspend_sz);
	}

	pm_set_ops(&omap_pm_ops);

#if defined(DEBUG) && defined(CONFIG_PROC_FS)
	omap_pm_init_proc();
#endif

	return 0;
}
__initcall(omap_pm_init);

