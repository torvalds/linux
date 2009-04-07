/*
 * arch/arm/plat-omap/include/mach/pm.h
 *
 * Header file for OMAP Power Management Routines
 *
 * Author: MontaVista Software, Inc.
 *	   support@mvista.com
 *
 * Copyright 2002 MontaVista Software Inc.
 *
 * Cleanup 2004 for Linux 2.6 by Dirk Behme <dirk.behme@de.bosch.com>
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

#ifndef __ASM_ARCH_OMAP_PM_H
#define __ASM_ARCH_OMAP_PM_H

/*
 * ----------------------------------------------------------------------------
 * Register and offset definitions to be used in PM assembler code
 * ----------------------------------------------------------------------------
 */
#define CLKGEN_REG_ASM_BASE		IO_ADDRESS(0xfffece00)
#define ARM_IDLECT1_ASM_OFFSET		0x04
#define ARM_IDLECT2_ASM_OFFSET		0x08

#define TCMIF_ASM_BASE			IO_ADDRESS(0xfffecc00)
#define EMIFS_CONFIG_ASM_OFFSET		0x0c
#define EMIFF_SDRAM_CONFIG_ASM_OFFSET	0x20

/*
 * ----------------------------------------------------------------------------
 * Power management bitmasks
 * ----------------------------------------------------------------------------
 */
#define IDLE_WAIT_CYCLES		0x00000fff
#define PERIPHERAL_ENABLE		0x2

#define SELF_REFRESH_MODE		0x0c000001
#define IDLE_EMIFS_REQUEST		0xc
#define MODEM_32K_EN			0x1
#define PER_EN				0x1

#define CPU_SUSPEND_SIZE		200
#define ULPD_LOW_PWR_EN			0x0001
#define ULPD_DEEP_SLEEP_TRANSITION_EN	0x0010
#define ULPD_SETUP_ANALOG_CELL_3_VAL	0
#define ULPD_POWER_CTRL_REG_VAL		0x0219

#define DSP_IDLE_DELAY			10
#define DSP_IDLE			0x0040
#define DSP_RST				0x0004
#define DSP_ENABLE			0x0002
#define SUFFICIENT_DSP_RESET_TIME	1000
#define DEFAULT_MPUI_CONFIG		0x05cf
#define ENABLE_XORCLK			0x2
#define DSP_CLOCK_ENABLE		0x2000
#define DSP_IDLE_MODE			0x2
#define TC_IDLE_REQUEST			(0x0000000c)

#define IRQ_LEVEL2			(1<<0)
#define IRQ_KEYBOARD			(1<<1)
#define IRQ_UART2			(1<<15)

#define PDE_BIT				0x08
#define PWD_EN_BIT			0x04
#define EN_PERCK_BIT			0x04

#define OMAP1510_DEEP_SLEEP_REQUEST	0x0ec7
#define OMAP1510_BIG_SLEEP_REQUEST	0x0cc5
#define OMAP1510_IDLE_LOOP_REQUEST	0x0c00
#define OMAP1510_IDLE_CLOCK_DOMAINS	0x2

/* Both big sleep and deep sleep use same values. Difference is in ULPD. */
#define OMAP1610_IDLECT1_SLEEP_VAL	0x13c7
#define OMAP1610_IDLECT2_SLEEP_VAL	0x09c7
#define OMAP1610_IDLECT3_VAL		0x3f
#define OMAP1610_IDLECT3_SLEEP_ORMASK	0x2c
#define OMAP1610_IDLECT3		0xfffece24
#define OMAP1610_IDLE_LOOP_REQUEST	0x0400

#define OMAP730_IDLECT1_SLEEP_VAL	0x16c7
#define OMAP730_IDLECT2_SLEEP_VAL	0x09c7
#define OMAP730_IDLECT3_VAL		0x3f
#define OMAP730_IDLECT3		0xfffece24
#define OMAP730_IDLE_LOOP_REQUEST	0x0C00

#if     !defined(CONFIG_ARCH_OMAP730) && \
	!defined(CONFIG_ARCH_OMAP15XX) && \
	!defined(CONFIG_ARCH_OMAP16XX) && \
	!defined(CONFIG_ARCH_OMAP24XX)
#warning "Power management for this processor not implemented yet"
#endif

#ifndef __ASSEMBLER__

#include <linux/clk.h>

extern void prevent_idle_sleep(void);
extern void allow_idle_sleep(void);

extern void omap_pm_idle(void);
extern void omap_pm_suspend(void);
extern void omap730_cpu_suspend(unsigned short, unsigned short);
extern void omap1510_cpu_suspend(unsigned short, unsigned short);
extern void omap1610_cpu_suspend(unsigned short, unsigned short);
extern void omap24xx_cpu_suspend(u32 dll_ctrl, void __iomem *sdrc_dlla_ctrl,
					void __iomem *sdrc_power);
extern void omap730_idle_loop_suspend(void);
extern void omap1510_idle_loop_suspend(void);
extern void omap1610_idle_loop_suspend(void);
extern void omap24xx_idle_loop_suspend(void);

extern unsigned int omap730_cpu_suspend_sz;
extern unsigned int omap1510_cpu_suspend_sz;
extern unsigned int omap1610_cpu_suspend_sz;
extern unsigned int omap24xx_cpu_suspend_sz;
extern unsigned int omap730_idle_loop_suspend_sz;
extern unsigned int omap1510_idle_loop_suspend_sz;
extern unsigned int omap1610_idle_loop_suspend_sz;
extern unsigned int omap24xx_idle_loop_suspend_sz;

#ifdef CONFIG_OMAP_SERIAL_WAKE
extern void omap_serial_wake_trigger(int enable);
#else
#define omap_serial_wakeup_init()	{}
#define omap_serial_wake_trigger(x)	{}
#endif	/* CONFIG_OMAP_SERIAL_WAKE */

#define ARM_SAVE(x) arm_sleep_save[ARM_SLEEP_SAVE_##x] = omap_readl(x)
#define ARM_RESTORE(x) omap_writel((arm_sleep_save[ARM_SLEEP_SAVE_##x]), (x))
#define ARM_SHOW(x) arm_sleep_save[ARM_SLEEP_SAVE_##x]

#define DSP_SAVE(x) dsp_sleep_save[DSP_SLEEP_SAVE_##x] = __raw_readw(x)
#define DSP_RESTORE(x) __raw_writew((dsp_sleep_save[DSP_SLEEP_SAVE_##x]), (x))
#define DSP_SHOW(x) dsp_sleep_save[DSP_SLEEP_SAVE_##x]

#define ULPD_SAVE(x) ulpd_sleep_save[ULPD_SLEEP_SAVE_##x] = omap_readw(x)
#define ULPD_RESTORE(x) omap_writew((ulpd_sleep_save[ULPD_SLEEP_SAVE_##x]), (x))
#define ULPD_SHOW(x) ulpd_sleep_save[ULPD_SLEEP_SAVE_##x]

#define MPUI730_SAVE(x) mpui730_sleep_save[MPUI730_SLEEP_SAVE_##x] = omap_readl(x)
#define MPUI730_RESTORE(x) omap_writel((mpui730_sleep_save[MPUI730_SLEEP_SAVE_##x]), (x))
#define MPUI730_SHOW(x) mpui730_sleep_save[MPUI730_SLEEP_SAVE_##x]

#define MPUI1510_SAVE(x) mpui1510_sleep_save[MPUI1510_SLEEP_SAVE_##x] = omap_readl(x)
#define MPUI1510_RESTORE(x) omap_writel((mpui1510_sleep_save[MPUI1510_SLEEP_SAVE_##x]), (x))
#define MPUI1510_SHOW(x) mpui1510_sleep_save[MPUI1510_SLEEP_SAVE_##x]

#define MPUI1610_SAVE(x) mpui1610_sleep_save[MPUI1610_SLEEP_SAVE_##x] = omap_readl(x)
#define MPUI1610_RESTORE(x) omap_writel((mpui1610_sleep_save[MPUI1610_SLEEP_SAVE_##x]), (x))
#define MPUI1610_SHOW(x) mpui1610_sleep_save[MPUI1610_SLEEP_SAVE_##x]

#define OMAP24XX_SAVE(x) omap24xx_sleep_save[OMAP24XX_SLEEP_SAVE_##x] = x
#define OMAP24XX_RESTORE(x) x = omap24xx_sleep_save[OMAP24XX_SLEEP_SAVE_##x]
#define OMAP24XX_SHOW(x) omap24xx_sleep_save[OMAP24XX_SLEEP_SAVE_##x]

/*
 * List of global OMAP registers to preserve.
 * More ones like CP and general purpose register values are preserved
 * with the stack pointer in sleep.S.
 */

enum arm_save_state {
	ARM_SLEEP_SAVE_START = 0,
	/*
	 * MPU control registers 32 bits
	 */
	ARM_SLEEP_SAVE_ARM_CKCTL,
	ARM_SLEEP_SAVE_ARM_IDLECT1,
	ARM_SLEEP_SAVE_ARM_IDLECT2,
	ARM_SLEEP_SAVE_ARM_IDLECT3,
	ARM_SLEEP_SAVE_ARM_EWUPCT,
	ARM_SLEEP_SAVE_ARM_RSTCT1,
	ARM_SLEEP_SAVE_ARM_RSTCT2,
	ARM_SLEEP_SAVE_ARM_SYSST,
	ARM_SLEEP_SAVE_SIZE
};

enum dsp_save_state {
	DSP_SLEEP_SAVE_START = 0,
	/*
	 * DSP registers 16 bits
	 */
	DSP_SLEEP_SAVE_DSP_IDLECT2,
	DSP_SLEEP_SAVE_SIZE
};

enum ulpd_save_state {
	ULPD_SLEEP_SAVE_START = 0,
	/*
	 * ULPD registers 16 bits
	 */
	ULPD_SLEEP_SAVE_ULPD_IT_STATUS,
	ULPD_SLEEP_SAVE_ULPD_CLOCK_CTRL,
	ULPD_SLEEP_SAVE_ULPD_SOFT_REQ,
	ULPD_SLEEP_SAVE_ULPD_STATUS_REQ,
	ULPD_SLEEP_SAVE_ULPD_DPLL_CTRL,
	ULPD_SLEEP_SAVE_ULPD_POWER_CTRL,
	ULPD_SLEEP_SAVE_SIZE
};

enum mpui1510_save_state {
	MPUI1510_SLEEP_SAVE_START = 0,
	/*
	 * MPUI registers 32 bits
	 */
	MPUI1510_SLEEP_SAVE_MPUI_CTRL,
	MPUI1510_SLEEP_SAVE_MPUI_DSP_BOOT_CONFIG,
	MPUI1510_SLEEP_SAVE_MPUI_DSP_API_CONFIG,
	MPUI1510_SLEEP_SAVE_MPUI_DSP_STATUS,
	MPUI1510_SLEEP_SAVE_EMIFF_SDRAM_CONFIG,
	MPUI1510_SLEEP_SAVE_EMIFS_CONFIG,
	MPUI1510_SLEEP_SAVE_OMAP_IH1_MIR,
	MPUI1510_SLEEP_SAVE_OMAP_IH2_MIR,
#if defined(CONFIG_ARCH_OMAP15XX)
	MPUI1510_SLEEP_SAVE_SIZE
#else
	MPUI1510_SLEEP_SAVE_SIZE = 0
#endif
};

enum mpui730_save_state {
	MPUI730_SLEEP_SAVE_START = 0,
	/*
	 * MPUI registers 32 bits
	 */
	MPUI730_SLEEP_SAVE_MPUI_CTRL,
	MPUI730_SLEEP_SAVE_MPUI_DSP_BOOT_CONFIG,
	MPUI730_SLEEP_SAVE_MPUI_DSP_API_CONFIG,
	MPUI730_SLEEP_SAVE_MPUI_DSP_STATUS,
	MPUI730_SLEEP_SAVE_EMIFF_SDRAM_CONFIG,
	MPUI730_SLEEP_SAVE_EMIFS_CONFIG,
	MPUI730_SLEEP_SAVE_OMAP_IH1_MIR,
	MPUI730_SLEEP_SAVE_OMAP_IH2_0_MIR,
	MPUI730_SLEEP_SAVE_OMAP_IH2_1_MIR,
#if defined(CONFIG_ARCH_OMAP730)
	MPUI730_SLEEP_SAVE_SIZE
#else
	MPUI730_SLEEP_SAVE_SIZE = 0
#endif
};

enum mpui1610_save_state {
	MPUI1610_SLEEP_SAVE_START = 0,
	/*
	 * MPUI registers 32 bits
	 */
	MPUI1610_SLEEP_SAVE_MPUI_CTRL,
	MPUI1610_SLEEP_SAVE_MPUI_DSP_BOOT_CONFIG,
	MPUI1610_SLEEP_SAVE_MPUI_DSP_API_CONFIG,
	MPUI1610_SLEEP_SAVE_MPUI_DSP_STATUS,
	MPUI1610_SLEEP_SAVE_EMIFF_SDRAM_CONFIG,
	MPUI1610_SLEEP_SAVE_EMIFS_CONFIG,
	MPUI1610_SLEEP_SAVE_OMAP_IH1_MIR,
	MPUI1610_SLEEP_SAVE_OMAP_IH2_0_MIR,
	MPUI1610_SLEEP_SAVE_OMAP_IH2_1_MIR,
	MPUI1610_SLEEP_SAVE_OMAP_IH2_2_MIR,
	MPUI1610_SLEEP_SAVE_OMAP_IH2_3_MIR,
#if defined(CONFIG_ARCH_OMAP16XX)
	MPUI1610_SLEEP_SAVE_SIZE
#else
	MPUI1610_SLEEP_SAVE_SIZE = 0
#endif
};

enum omap24xx_save_state {
	OMAP24XX_SLEEP_SAVE_START = 0,
	OMAP24XX_SLEEP_SAVE_INTC_MIR0,
	OMAP24XX_SLEEP_SAVE_INTC_MIR1,
	OMAP24XX_SLEEP_SAVE_INTC_MIR2,

	OMAP24XX_SLEEP_SAVE_CM_CLKSTCTRL_MPU,
	OMAP24XX_SLEEP_SAVE_CM_CLKSTCTRL_CORE,
	OMAP24XX_SLEEP_SAVE_CM_CLKSTCTRL_GFX,
	OMAP24XX_SLEEP_SAVE_CM_CLKSTCTRL_DSP,
	OMAP24XX_SLEEP_SAVE_CM_CLKSTCTRL_MDM,

	OMAP24XX_SLEEP_SAVE_PM_PWSTCTRL_MPU,
	OMAP24XX_SLEEP_SAVE_PM_PWSTCTRL_CORE,
	OMAP24XX_SLEEP_SAVE_PM_PWSTCTRL_GFX,
	OMAP24XX_SLEEP_SAVE_PM_PWSTCTRL_DSP,
	OMAP24XX_SLEEP_SAVE_PM_PWSTCTRL_MDM,

	OMAP24XX_SLEEP_SAVE_CM_IDLEST1_CORE,
	OMAP24XX_SLEEP_SAVE_CM_IDLEST2_CORE,
	OMAP24XX_SLEEP_SAVE_CM_IDLEST3_CORE,
	OMAP24XX_SLEEP_SAVE_CM_IDLEST4_CORE,
	OMAP24XX_SLEEP_SAVE_CM_IDLEST_GFX,
	OMAP24XX_SLEEP_SAVE_CM_IDLEST_WKUP,
	OMAP24XX_SLEEP_SAVE_CM_IDLEST_CKGEN,
	OMAP24XX_SLEEP_SAVE_CM_IDLEST_DSP,
	OMAP24XX_SLEEP_SAVE_CM_IDLEST_MDM,

	OMAP24XX_SLEEP_SAVE_CM_AUTOIDLE1_CORE,
	OMAP24XX_SLEEP_SAVE_CM_AUTOIDLE2_CORE,
	OMAP24XX_SLEEP_SAVE_CM_AUTOIDLE3_CORE,
	OMAP24XX_SLEEP_SAVE_CM_AUTOIDLE4_CORE,
	OMAP24XX_SLEEP_SAVE_CM_AUTOIDLE_WKUP,
	OMAP24XX_SLEEP_SAVE_CM_AUTOIDLE_PLL,
	OMAP24XX_SLEEP_SAVE_CM_AUTOIDLE_DSP,
	OMAP24XX_SLEEP_SAVE_CM_AUTOIDLE_MDM,

	OMAP24XX_SLEEP_SAVE_CM_FCLKEN1_CORE,
	OMAP24XX_SLEEP_SAVE_CM_FCLKEN2_CORE,
	OMAP24XX_SLEEP_SAVE_CM_ICLKEN1_CORE,
	OMAP24XX_SLEEP_SAVE_CM_ICLKEN2_CORE,
	OMAP24XX_SLEEP_SAVE_CM_ICLKEN3_CORE,
	OMAP24XX_SLEEP_SAVE_CM_ICLKEN4_CORE,
	OMAP24XX_SLEEP_SAVE_GPIO1_IRQENABLE1,
	OMAP24XX_SLEEP_SAVE_GPIO2_IRQENABLE1,
	OMAP24XX_SLEEP_SAVE_GPIO3_IRQENABLE1,
	OMAP24XX_SLEEP_SAVE_GPIO4_IRQENABLE1,
	OMAP24XX_SLEEP_SAVE_GPIO3_OE,
	OMAP24XX_SLEEP_SAVE_GPIO4_OE,
	OMAP24XX_SLEEP_SAVE_GPIO3_RISINGDETECT,
	OMAP24XX_SLEEP_SAVE_GPIO3_FALLINGDETECT,
	OMAP24XX_SLEEP_SAVE_CONTROL_PADCONF_SPI1_NCS2,
	OMAP24XX_SLEEP_SAVE_CONTROL_PADCONF_MCBSP1_DX,
	OMAP24XX_SLEEP_SAVE_CONTROL_PADCONF_SSI1_FLAG_TX,
	OMAP24XX_SLEEP_SAVE_CONTROL_PADCONF_SYS_NIRQW0,
	OMAP24XX_SLEEP_SAVE_SIZE
};

#endif /* ASSEMBLER */
#endif /* __ASM_ARCH_OMAP_PM_H */
