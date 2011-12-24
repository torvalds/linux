/*
 * arch/arm/plat-orion/include/plat/common.h
 *
 * Marvell Orion SoC common setup code used by different mach-/common.c
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __PLAT_COMMON_H
#include <linux/mv643xx_eth.h>

struct dsa_platform_data;

void __init orion_uart0_init(unsigned int membase,
			     resource_size_t mapbase,
			     unsigned int irq,
			     struct clk *clk);

void __init orion_uart1_init(unsigned int membase,
			     resource_size_t mapbase,
			     unsigned int irq,
			     struct clk *clk);

void __init orion_uart2_init(unsigned int membase,
			     resource_size_t mapbase,
			     unsigned int irq,
			     struct clk *clk);

void __init orion_uart3_init(unsigned int membase,
			     resource_size_t mapbase,
			     unsigned int irq,
			     struct clk *clk);

void __init orion_rtc_init(unsigned long mapbase,
			   unsigned long irq);

void __init orion_ge00_init(struct mv643xx_eth_platform_data *eth_data,
			    unsigned long mapbase,
			    unsigned long irq,
			    unsigned long irq_err);

void __init orion_ge01_init(struct mv643xx_eth_platform_data *eth_data,
			    unsigned long mapbase,
			    unsigned long irq,
			    unsigned long irq_err);

void __init orion_ge10_init(struct mv643xx_eth_platform_data *eth_data,
			    unsigned long mapbase,
			    unsigned long irq,
			    unsigned long irq_err);

void __init orion_ge11_init(struct mv643xx_eth_platform_data *eth_data,
			    unsigned long mapbase,
			    unsigned long irq,
			    unsigned long irq_err);

void __init orion_ge00_switch_init(struct dsa_platform_data *d,
				   int irq);

void __init orion_i2c_init(unsigned long mapbase,
			   unsigned long irq,
			   unsigned long freq_m);

void __init orion_i2c_1_init(unsigned long mapbase,
			     unsigned long irq,
			     unsigned long freq_m);

void __init orion_spi_init(unsigned long mapbase);

void __init orion_spi_1_init(unsigned long mapbase);

void __init orion_wdt_init(void);

void __init orion_xor0_init(unsigned long mapbase_low,
			    unsigned long mapbase_high,
			    unsigned long irq_0,
			    unsigned long irq_1);

void __init orion_xor1_init(unsigned long mapbase_low,
			    unsigned long mapbase_high,
			    unsigned long irq_0,
			    unsigned long irq_1);

void __init orion_ehci_init(unsigned long mapbase,
			    unsigned long irq,
			    enum orion_ehci_phy_ver phy_version);

void __init orion_ehci_1_init(unsigned long mapbase,
			      unsigned long irq);

void __init orion_ehci_2_init(unsigned long mapbase,
			      unsigned long irq);

void __init orion_sata_init(struct mv_sata_platform_data *sata_data,
			    unsigned long mapbase,
			    unsigned long irq);

void __init orion_crypto_init(unsigned long mapbase,
			      unsigned long srambase,
			      unsigned long sram_size,
			      unsigned long irq);

void __init orion_clkdev_add(const char *con_id, const char *dev_id,
			     struct clk *clk);

void __init orion_clkdev_init(struct clk *tclk);
#endif
