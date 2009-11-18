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
#include <mach/asp.h>
#include <mach/mmc.h>

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

#define DA8XX_PSC0_BASE		0x01c10000
#define DA8XX_PLL0_BASE		0x01c11000
#define DA8XX_JTAG_ID_REG	0x01c14018
#define DA8XX_TIMER64P0_BASE	0x01c20000
#define DA8XX_TIMER64P1_BASE	0x01c21000
#define DA8XX_GPIO_BASE		0x01e26000
#define DA8XX_PSC1_BASE		0x01e27000
#define DA8XX_LCD_CNTRL_BASE	0x01e13000
#define DA8XX_MMCSD0_BASE	0x01c40000
#define DA8XX_AEMIF_CS2_BASE	0x60000000
#define DA8XX_AEMIF_CS3_BASE	0x62000000
#define DA8XX_AEMIF_CTL_BASE	0x68000000

#define PINMUX0			0x00
#define PINMUX1			0x04
#define PINMUX2			0x08
#define PINMUX3			0x0c
#define PINMUX4			0x10
#define PINMUX5			0x14
#define PINMUX6			0x18
#define PINMUX7			0x1c
#define PINMUX8			0x20
#define PINMUX9			0x24
#define PINMUX10		0x28
#define PINMUX11		0x2c
#define PINMUX12		0x30
#define PINMUX13		0x34
#define PINMUX14		0x38
#define PINMUX15		0x3c
#define PINMUX16		0x40
#define PINMUX17		0x44
#define PINMUX18		0x48
#define PINMUX19		0x4c

void __init da830_init(void);
void __init da850_init(void);

int da8xx_register_edma(void);
int da8xx_register_i2c(int instance, struct davinci_i2c_platform_data *pdata);
int da8xx_register_watchdog(void);
int da8xx_register_emac(void);
int da8xx_register_lcdc(void);
int da8xx_register_mmcsd0(struct davinci_mmc_config *config);
void __init da8xx_init_mcasp(int id, struct snd_platform_data *pdata);

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

extern const short da850_uart0_pins[];
extern const short da850_uart1_pins[];
extern const short da850_uart2_pins[];
extern const short da850_i2c0_pins[];
extern const short da850_i2c1_pins[];
extern const short da850_cpgmac_pins[];
extern const short da850_mcasp_pins[];
extern const short da850_lcdcntl_pins[];
extern const short da850_mmcsd0_pins[];
extern const short da850_nand_pins[];
extern const short da850_nor_pins[];

int da8xx_pinmux_setup(const short pins[]);

#endif /* __ASM_ARCH_DAVINCI_DA8XX_H */
