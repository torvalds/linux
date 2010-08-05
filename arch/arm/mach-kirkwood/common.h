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

struct dsa_platform_data;
struct mv643xx_eth_platform_data;
struct mv_sata_platform_data;
struct mvsdio_platform_data;
struct mtd_partition;
struct mtd_info;
struct kirkwood_asoc_platform_data;

/*
 * Basic Kirkwood init functions used early by machine-setup.
 */
void kirkwood_map_io(void);
void kirkwood_init(void);
void kirkwood_init_irq(void);

extern struct mbus_dram_target_info kirkwood_mbus_dram_info;
void kirkwood_setup_cpu_mbus(void);

void kirkwood_pcie_id(u32 *dev, u32 *rev);

void kirkwood_ehci_init(void);
void kirkwood_ge00_init(struct mv643xx_eth_platform_data *eth_data);
void kirkwood_ge01_init(struct mv643xx_eth_platform_data *eth_data);
void kirkwood_ge00_switch_init(struct dsa_platform_data *d, int irq);
void kirkwood_pcie_init(void);
void kirkwood_sata_init(struct mv_sata_platform_data *sata_data);
void kirkwood_sdio_init(struct mvsdio_platform_data *mvsdio_data);
void kirkwood_spi_init(void);
void kirkwood_i2c_init(void);
void kirkwood_uart0_init(void);
void kirkwood_uart1_init(void);
void kirkwood_nand_init(struct mtd_partition *parts, int nr_parts, int delay);
void kirkwood_nand_init_rnb(struct mtd_partition *parts, int nr_parts, int (*dev_ready)(struct mtd_info *));
void kirkwood_audio_init(void);

extern int kirkwood_tclk;
extern struct sys_timer kirkwood_timer;

#define ARRAY_AND_SIZE(x)	(x), ARRAY_SIZE(x)

#endif
