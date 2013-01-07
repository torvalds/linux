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

#define KW_PCIE0	(1 << 0)
#define KW_PCIE1	(1 << 1)

/*
 * Basic Kirkwood init functions used early by machine-setup.
 */
void kirkwood_map_io(void);
void kirkwood_init(void);
void kirkwood_init_early(void);
void kirkwood_init_irq(void);

void kirkwood_setup_cpu_mbus(void);

void kirkwood_enable_pcie(void);
void kirkwood_pcie_id(u32 *dev, u32 *rev);

void kirkwood_ehci_init(void);
void kirkwood_ge00_init(struct mv643xx_eth_platform_data *eth_data);
void kirkwood_ge01_init(struct mv643xx_eth_platform_data *eth_data);
void kirkwood_ge00_switch_init(struct dsa_platform_data *d, int irq);
void kirkwood_pcie_init(unsigned int portmask);
void kirkwood_sata_init(struct mv_sata_platform_data *sata_data);
void kirkwood_sdio_init(struct mvsdio_platform_data *mvsdio_data);
void kirkwood_spi_init(void);
void kirkwood_i2c_init(void);
void kirkwood_uart0_init(void);
void kirkwood_uart1_init(void);
void kirkwood_nand_init(struct mtd_partition *parts, int nr_parts, int delay);
void kirkwood_nand_init_rnb(struct mtd_partition *parts, int nr_parts,
			    int (*dev_ready)(struct mtd_info *));
void kirkwood_audio_init(void);
void kirkwood_restart(char, const char *);
void kirkwood_clk_init(void);

/* board init functions for boards not fully converted to fdt */
#ifdef CONFIG_MACH_DREAMPLUG_DT
void dreamplug_init(void);
#else
static inline void dreamplug_init(void) {};
#endif
#ifdef CONFIG_MACH_TS219_DT
void qnap_dt_ts219_init(void);
#else
static inline void qnap_dt_ts219_init(void) {};
#endif

#ifdef CONFIG_MACH_DLINK_KIRKWOOD_DT
void dnskw_init(void);
#else
static inline void dnskw_init(void) {};
#endif

#ifdef CONFIG_MACH_ICONNECT_DT
void iconnect_init(void);
#else
static inline void iconnect_init(void) {};
#endif

#ifdef CONFIG_MACH_IB62X0_DT
void ib62x0_init(void);
#else
static inline void ib62x0_init(void) {};
#endif

#ifdef CONFIG_MACH_DOCKSTAR_DT
void dockstar_dt_init(void);
#else
static inline void dockstar_dt_init(void) {};
#endif

#ifdef CONFIG_MACH_GOFLEXNET_DT
void goflexnet_init(void);
#else
static inline void goflexnet_init(void) {};
#endif

#ifdef CONFIG_MACH_LSXL_DT
void lsxl_init(void);
#else
static inline void lsxl_init(void) {};
#endif

#ifdef CONFIG_MACH_IOMEGA_IX2_200_DT
void iomega_ix2_200_init(void);
#else
static inline void iomega_ix2_200_init(void) {};
#endif

#ifdef CONFIG_MACH_KM_KIRKWOOD_DT
void km_kirkwood_init(void);
#else
static inline void km_kirkwood_init(void) {};
#endif

#ifdef CONFIG_MACH_MPLCEC4_DT
void mplcec4_init(void);
#else
static inline void mplcec4_init(void) {};
#endif

#if defined(CONFIG_MACH_INETSPACE_V2_DT) || \
	defined(CONFIG_MACH_NETSPACE_V2_DT) || \
	defined(CONFIG_MACH_NETSPACE_MAX_V2_DT) || \
	defined(CONFIG_MACH_NETSPACE_LITE_V2_DT) || \
	defined(CONFIG_MACH_NETSPACE_MINI_V2_DT)
void ns2_init(void);
#else
static inline void ns2_init(void) {};
#endif

#ifdef CONFIG_MACH_NSA310_DT
void nsa310_init(void);
#else
static inline void nsa310_init(void) {};
#endif

#ifdef CONFIG_MACH_OPENBLOCKS_A6_DT
void openblocks_a6_init(void);
#else
static inline void openblocks_a6_init(void) {};
#endif

#ifdef CONFIG_MACH_TOPKICK_DT
void usi_topkick_init(void);
#else
static inline void usi_topkick_init(void) {};
#endif

/* early init functions not converted to fdt yet */
char *kirkwood_id(void);
void kirkwood_l2_init(void);
void kirkwood_wdt_init(void);
void kirkwood_xor0_init(void);
void kirkwood_xor1_init(void);
void kirkwood_crypto_init(void);

extern int kirkwood_tclk;
extern struct sys_timer kirkwood_timer;

#define ARRAY_AND_SIZE(x)	(x), ARRAY_SIZE(x)

#endif
