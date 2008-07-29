/*
 * arch/arm/mach-loki/common.h
 *
 * Core functions for Marvell Loki (88RC8480) SoCs
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ARCH_LOKI_COMMON_H
#define __ARCH_LOKI_COMMON_H

struct mv643xx_eth_platform_data;

/*
 * Basic Loki init functions used early by machine-setup.
 */
void loki_map_io(void);
void loki_init(void);
void loki_init_irq(void);

extern struct mbus_dram_target_info loki_mbus_dram_info;
void loki_setup_cpu_mbus(void);
void loki_setup_dev_boot_win(u32 base, u32 size);

void loki_ge0_init(struct mv643xx_eth_platform_data *eth_data);
void loki_ge1_init(struct mv643xx_eth_platform_data *eth_data);
void loki_sas_init(void);
void loki_uart0_init(void);
void loki_uart1_init(void);

extern struct sys_timer loki_timer;


#endif
