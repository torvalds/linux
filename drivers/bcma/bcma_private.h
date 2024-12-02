/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_BCMA_PRIVATE_H_
#define LINUX_BCMA_PRIVATE_H_

#ifndef pr_fmt
#define pr_fmt(fmt)		KBUILD_MODNAME ": " fmt
#endif

#include <linux/bcma/bcma.h>
#include <linux/delay.h>

#define bcma_err(bus, fmt, ...) \
	dev_err((bus)->dev, "bus%d: " fmt, (bus)->num, ##__VA_ARGS__)
#define bcma_warn(bus, fmt, ...) \
	dev_warn((bus)->dev, "bus%d: " fmt, (bus)->num, ##__VA_ARGS__)
#define bcma_info(bus, fmt, ...) \
	dev_info((bus)->dev, "bus%d: " fmt, (bus)->num, ##__VA_ARGS__)
#define bcma_debug(bus, fmt, ...) \
	dev_dbg((bus)->dev, "bus%d: " fmt, (bus)->num, ##__VA_ARGS__)

struct bcma_bus;

/* main.c */
bool bcma_wait_value(struct bcma_device *core, u16 reg, u32 mask, u32 value,
		     int timeout);
void bcma_prepare_core(struct bcma_bus *bus, struct bcma_device *core);
void bcma_init_bus(struct bcma_bus *bus);
void bcma_unregister_cores(struct bcma_bus *bus);
int bcma_bus_register(struct bcma_bus *bus);
void bcma_bus_unregister(struct bcma_bus *bus);
int __init bcma_bus_early_register(struct bcma_bus *bus);
#ifdef CONFIG_PM
int bcma_bus_suspend(struct bcma_bus *bus);
int bcma_bus_resume(struct bcma_bus *bus);
#endif

/* scan.c */
void bcma_detect_chip(struct bcma_bus *bus);
int bcma_bus_scan(struct bcma_bus *bus);

/* sprom.c */
int bcma_sprom_get(struct bcma_bus *bus);

/* driver_chipcommon.c */
void bcma_core_chipcommon_early_init(struct bcma_drv_cc *cc);
void bcma_core_chipcommon_init(struct bcma_drv_cc *cc);
void bcma_chipco_bcm4331_ext_pa_lines_ctl(struct bcma_drv_cc *cc, bool enable);
#ifdef CONFIG_BCMA_DRIVER_MIPS
void bcma_chipco_serial_init(struct bcma_drv_cc *cc);
#endif /* CONFIG_BCMA_DRIVER_MIPS */

/* driver_chipcommon_b.c */
int bcma_core_chipcommon_b_init(struct bcma_drv_cc_b *ccb);
void bcma_core_chipcommon_b_free(struct bcma_drv_cc_b *ccb);

/* driver_chipcommon_pmu.c */
void bcma_pmu_early_init(struct bcma_drv_cc *cc);
void bcma_pmu_init(struct bcma_drv_cc *cc);
u32 bcma_pmu_get_alp_clock(struct bcma_drv_cc *cc);
u32 bcma_pmu_get_cpu_clock(struct bcma_drv_cc *cc);

/**************************************************
 * driver_chipcommon_sflash.c
 **************************************************/

#ifdef CONFIG_BCMA_PFLASH
extern struct platform_device bcma_pflash_dev;
int bcma_pflash_init(struct bcma_drv_cc *cc);
#else
static inline int bcma_pflash_init(struct bcma_drv_cc *cc)
{
	bcma_err(cc->core->bus, "Parallel flash not supported\n");
	return 0;
}
#endif /* CONFIG_BCMA_PFLASH */

#ifdef CONFIG_BCMA_SFLASH
/* driver_chipcommon_sflash.c */
int bcma_sflash_init(struct bcma_drv_cc *cc);
extern struct platform_device bcma_sflash_dev;
#else
static inline int bcma_sflash_init(struct bcma_drv_cc *cc)
{
	bcma_err(cc->core->bus, "Serial flash not supported\n");
	return 0;
}
#endif /* CONFIG_BCMA_SFLASH */

#ifdef CONFIG_BCMA_NFLASH
/* driver_chipcommon_nflash.c */
int bcma_nflash_init(struct bcma_drv_cc *cc);
extern struct platform_device bcma_nflash_dev;
#else
static inline int bcma_nflash_init(struct bcma_drv_cc *cc)
{
	bcma_err(cc->core->bus, "NAND flash not supported\n");
	return 0;
}
#endif /* CONFIG_BCMA_NFLASH */

#ifdef CONFIG_BCMA_HOST_PCI
/* host_pci.c */
extern int __init bcma_host_pci_init(void);
extern void __exit bcma_host_pci_exit(void);
#endif /* CONFIG_BCMA_HOST_PCI */

/* host_soc.c */
#if defined(CONFIG_BCMA_HOST_SOC) && defined(CONFIG_OF)
extern int __init bcma_host_soc_register_driver(void);
extern void __exit bcma_host_soc_unregister_driver(void);
#else
static inline int __init bcma_host_soc_register_driver(void)
{
	return 0;
}
static inline void __exit bcma_host_soc_unregister_driver(void)
{
}
#endif /* CONFIG_BCMA_HOST_SOC && CONFIG_OF */

/* driver_pci.c */
#ifdef CONFIG_BCMA_DRIVER_PCI
u32 bcma_pcie_read(struct bcma_drv_pci *pc, u32 address);
void bcma_core_pci_early_init(struct bcma_drv_pci *pc);
void bcma_core_pci_init(struct bcma_drv_pci *pc);
void bcma_core_pci_up(struct bcma_drv_pci *pc);
void bcma_core_pci_down(struct bcma_drv_pci *pc);
#else
static inline void bcma_core_pci_early_init(struct bcma_drv_pci *pc)
{
	WARN_ON(pc->core->bus->hosttype == BCMA_HOSTTYPE_PCI);
}
static inline void bcma_core_pci_init(struct bcma_drv_pci *pc)
{
	/* Initialization is required for PCI hosted bus */
	WARN_ON(pc->core->bus->hosttype == BCMA_HOSTTYPE_PCI);
}
#endif

/* driver_pcie2.c */
#ifdef CONFIG_BCMA_DRIVER_PCI
void bcma_core_pcie2_init(struct bcma_drv_pcie2 *pcie2);
void bcma_core_pcie2_up(struct bcma_drv_pcie2 *pcie2);
#else
static inline void bcma_core_pcie2_init(struct bcma_drv_pcie2 *pcie2)
{
	/* Initialization is required for PCI hosted bus */
	WARN_ON(pcie2->core->bus->hosttype == BCMA_HOSTTYPE_PCI);
}
#endif

extern int bcma_chipco_watchdog_register(struct bcma_drv_cc *cc);

#ifdef CONFIG_BCMA_DRIVER_PCI_HOSTMODE
bool bcma_core_pci_is_in_hostmode(struct bcma_drv_pci *pc);
void bcma_core_pci_hostmode_init(struct bcma_drv_pci *pc);
#else
static inline bool bcma_core_pci_is_in_hostmode(struct bcma_drv_pci *pc)
{
	return false;
}
static inline void bcma_core_pci_hostmode_init(struct bcma_drv_pci *pc)
{
}
#endif /* CONFIG_BCMA_DRIVER_PCI_HOSTMODE */

/**************************************************
 * driver_mips.c
 **************************************************/

#ifdef CONFIG_BCMA_DRIVER_MIPS
unsigned int bcma_core_mips_irq(struct bcma_device *dev);
void bcma_core_mips_early_init(struct bcma_drv_mips *mcore);
void bcma_core_mips_init(struct bcma_drv_mips *mcore);
#else
static inline unsigned int bcma_core_mips_irq(struct bcma_device *dev)
{
	return 0;
}
static inline void bcma_core_mips_early_init(struct bcma_drv_mips *mcore)
{
}
static inline void bcma_core_mips_init(struct bcma_drv_mips *mcore)
{
}
#endif

/**************************************************
 * driver_gmac_cmn.c
 **************************************************/

#ifdef CONFIG_BCMA_DRIVER_GMAC_CMN
void bcma_core_gmac_cmn_init(struct bcma_drv_gmac_cmn *gc);
#else
static inline void bcma_core_gmac_cmn_init(struct bcma_drv_gmac_cmn *gc)
{
}
#endif

#ifdef CONFIG_BCMA_DRIVER_GPIO
/* driver_gpio.c */
int bcma_gpio_init(struct bcma_drv_cc *cc);
int bcma_gpio_unregister(struct bcma_drv_cc *cc);
#else
static inline int bcma_gpio_init(struct bcma_drv_cc *cc)
{
	return -ENOTSUPP;
}
static inline int bcma_gpio_unregister(struct bcma_drv_cc *cc)
{
	return 0;
}
#endif /* CONFIG_BCMA_DRIVER_GPIO */

#endif
