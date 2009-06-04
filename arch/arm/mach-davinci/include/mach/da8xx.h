/*
 * Chip specific defines for DA8XX/OMAP L1XX SoC
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2007, 2009 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef __ASM_ARCH_DAVINCI_DA8XX_H
#define __ASM_ARCH_DAVINCI_DA8XX_H

#include <mach/serial.h>
#include <mach/edma.h>
#include <mach/i2c.h>
#include <mach/emac.h>

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

#define DA8XX_BOOT_CFG_BASE	(IO_PHYS + 0x14000)

void __init da830_init(void);

int da8xx_register_edma(void);
int da8xx_register_i2c(int instance, struct davinci_i2c_platform_data *pdata);
int da8xx_register_watchdog(void);
int da8xx_register_emac(void);

extern struct platform_device da8xx_serial_device;
extern struct emac_platform_data da8xx_emac_pdata;

extern const short da830_emif25_pins[];
extern const short da830_spi0_pins[];
extern const short da830_spi1_pins[];
extern const short da830_mmc_sd_pins[];
extern const short da830_uart0_pins[];
extern const short da830_uart1_pins[];
extern const short da830_uart2_pins[];
extern const short da830_usb20_pins[];
extern const short da830_usb11_pins[];
extern const short da830_uhpi_pins[];
extern const short da830_cpgmac_pins[];
extern const short da830_emif3c_pins[];
extern const short da830_mcasp0_pins[];
extern const short da830_mcasp1_pins[];
extern const short da830_mcasp2_pins[];
extern const short da830_i2c0_pins[];
extern const short da830_i2c1_pins[];
extern const short da830_lcdcntl_pins[];
extern const short da830_pwm_pins[];
extern const short da830_ecap0_pins[];
extern const short da830_ecap1_pins[];
extern const short da830_ecap2_pins[];
extern const short da830_eqep0_pins[];
extern const short da830_eqep1_pins[];

int da830_pinmux_setup(const short pins[]);

#endif /* __ASM_ARCH_DAVINCI_DA8XX_H */
