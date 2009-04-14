/*
 * arch/arm/mach-mv78xx0/common.h
 *
 * Core functions for Marvell MV78xx0 SoCs
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ARCH_MV78XX0_COMMON_H
#define __ARCH_MV78XX0_COMMON_H

struct mv643xx_eth_platform_data;
struct mv_sata_platform_data;

/*
 * Basic MV78xx0 init functions used early by machine-setup.
 */
int mv78xx0_core_index(void);
void mv78xx0_map_io(void);
void mv78xx0_init(void);
void mv78xx0_init_irq(void);

extern struct mbus_dram_target_info mv78xx0_mbus_dram_info;
void mv78xx0_setup_cpu_mbus(void);
void mv78xx0_setup_pcie_io_win(int window, u32 base, u32 size,
			       int maj, int min);
void mv78xx0_setup_pcie_mem_win(int window, u32 base, u32 size,
				int maj, int min);

void mv78xx0_pcie_id(u32 *dev, u32 *rev);

void mv78xx0_ehci0_init(void);
void mv78xx0_ehci1_init(void);
void mv78xx0_ehci2_init(void);
void mv78xx0_ge00_init(struct mv643xx_eth_platform_data *eth_data);
void mv78xx0_ge01_init(struct mv643xx_eth_platform_data *eth_data);
void mv78xx0_ge10_init(struct mv643xx_eth_platform_data *eth_data);
void mv78xx0_ge11_init(struct mv643xx_eth_platform_data *eth_data);
void mv78xx0_pcie_init(int init_port0, int init_port1);
void mv78xx0_sata_init(struct mv_sata_platform_data *sata_data);
void mv78xx0_uart0_init(void);
void mv78xx0_uart1_init(void);
void mv78xx0_uart2_init(void);
void mv78xx0_uart3_init(void);
void mv78xx0_i2c_init(void);

extern struct sys_timer mv78xx0_timer;


#endif
