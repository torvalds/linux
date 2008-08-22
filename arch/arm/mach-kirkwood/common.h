/*
 * arch/arm/mach-kirkwood/common.h
 *
 * Core functions for Marvell Kirkwood SoCs
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ARCH_KIRKWOOD_COMMON_H
#define __ARCH_KIRKWOOD_COMMON_H

struct mv643xx_eth_platform_data;
struct mv_sata_platform_data;

/*
 * Basic Kirkwood init functions used early by machine-setup.
 */
void kirkwood_map_io(void);
void kirkwood_init(void);
void kirkwood_init_irq(void);

extern struct mbus_dram_target_info kirkwood_mbus_dram_info;
void kirkwood_setup_cpu_mbus(void);
void kirkwood_setup_pcie_io_win(int window, u32 base, u32 size,
				int maj, int min);
void kirkwood_setup_pcie_mem_win(int window, u32 base, u32 size,
				 int maj, int min);

void kirkwood_ehci_init(void);
void kirkwood_ge00_init(struct mv643xx_eth_platform_data *eth_data);
void kirkwood_pcie_init(void);
void kirkwood_rtc_init(void);
void kirkwood_sata_init(struct mv_sata_platform_data *sata_data);
void kirkwood_spi_init(void);
void kirkwood_uart0_init(void);
void kirkwood_uart1_init(void);
void kirkwood_xor0_init(void);
void kirkwood_xor1_init(void);

extern struct sys_timer kirkwood_timer;


#endif
