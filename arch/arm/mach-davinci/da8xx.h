/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Chip specific defines for DA8XX/OMAP L1XX SoC
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2007, 2009-2010 (c) MontaVista Software, Inc.
 */
#ifndef __ASM_ARCH_DAVINCI_DA8XX_H
#define __ASM_ARCH_DAVINCI_DA8XX_H

#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <linux/reboot.h>
#include <linux/regmap.h>

#include "hardware.h"
#include "pm.h"

#include <media/davinci/vpif_types.h>

extern void __iomem *da8xx_syscfg0_base;
extern void __iomem *da8xx_syscfg1_base;

/*
 * The cp_intc interrupt controller for the da8xx isn't in the same
 * chunk of physical memory space as the other registers (like it is
 * on the davincis) so it needs to be mapped separately.  It will be
 * mapped early on when the I/O space is mapped and we'll put it just
 * before the I/O space in the processor's virtual memory space.
 */
#define DA8XX_CP_INTC_BASE	0xfffee000
#define DA8XX_CP_INTC_SIZE	SZ_8K
#define DA8XX_CP_INTC_VIRT	(IO_VIRT - DA8XX_CP_INTC_SIZE - SZ_4K)

#define DA8XX_SYSCFG0_BASE	(IO_PHYS + 0x14000)
#define DA8XX_SYSCFG0_VIRT(x)	(da8xx_syscfg0_base + (x))
#define DA8XX_JTAG_ID_REG	0x18
#define DA8XX_HOST1CFG_REG	0x44
#define DA8XX_CHIPSIG_REG	0x174
#define DA8XX_CFGCHIP0_REG	0x17c
#define DA8XX_CFGCHIP1_REG	0x180
#define DA8XX_CFGCHIP2_REG	0x184
#define DA8XX_CFGCHIP3_REG	0x188
#define DA8XX_CFGCHIP4_REG	0x18c

#define DA8XX_SYSCFG1_BASE	(IO_PHYS + 0x22C000)
#define DA8XX_SYSCFG1_VIRT(x)	(da8xx_syscfg1_base + (x))
#define DA8XX_DEEPSLEEP_REG	0x8
#define DA8XX_PWRDN_REG		0x18

#define DA8XX_PSC0_BASE		0x01c10000
#define DA8XX_PLL0_BASE		0x01c11000
#define DA8XX_TIMER64P0_BASE	0x01c20000
#define DA8XX_TIMER64P1_BASE	0x01c21000
#define DA8XX_VPIF_BASE		0x01e17000
#define DA8XX_GPIO_BASE		0x01e26000
#define DA8XX_PSC1_BASE		0x01e27000

#define DA8XX_DSP_L2_RAM_BASE	0x11800000
#define DA8XX_DSP_L1P_RAM_BASE	(DA8XX_DSP_L2_RAM_BASE + 0x600000)
#define DA8XX_DSP_L1D_RAM_BASE	(DA8XX_DSP_L2_RAM_BASE + 0x700000)

#define DA8XX_AEMIF_CS2_BASE	0x60000000
#define DA8XX_AEMIF_CS3_BASE	0x62000000
#define DA8XX_AEMIF_CTL_BASE	0x68000000
#define DA8XX_SHARED_RAM_BASE	0x80000000
#define DA8XX_ARM_RAM_BASE	0xffff0000

void da830_init(void);

void da850_init(void);

int da850_register_vpif_display
			(struct vpif_display_config *display_config);
int da850_register_vpif_capture
			(struct vpif_capture_config *capture_config);
struct regmap *da8xx_get_cfgchip(void);
void __iomem *da8xx_get_mem_ctlr(void);

#endif /* __ASM_ARCH_DAVINCI_DA8XX_H */
