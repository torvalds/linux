/*
 * intel-mid.h: Intel MID specific setup code
 *
 * (C) Copyright 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#ifndef _ASM_X86_INTEL_MID_H
#define _ASM_X86_INTEL_MID_H

#include <linux/sfi.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

extern int intel_mid_pci_init(void);
extern int intel_mid_pci_set_power_state(struct pci_dev *pdev, pci_power_t state);
extern pci_power_t intel_mid_pci_get_power_state(struct pci_dev *pdev);

extern void intel_mid_pwr_power_off(void);

#define INTEL_MID_PWR_LSS_OFFSET	4
#define INTEL_MID_PWR_LSS_TYPE		(1 << 7)

extern int intel_mid_pwr_get_lss_id(struct pci_dev *pdev);

extern int get_gpio_by_name(const char *name);
extern void intel_scu_device_register(struct platform_device *pdev);
extern int __init sfi_parse_mrtc(struct sfi_table_header *table);
extern int __init sfi_parse_mtmr(struct sfi_table_header *table);
extern int sfi_mrtc_num;
extern struct sfi_rtc_table_entry sfi_mrtc_array[];

/*
 * Here defines the array of devices platform data that IAFW would export
 * through SFI "DEVS" table, we use name and type to match the device and
 * its platform data.
 */
struct devs_id {
	char name[SFI_NAME_LEN + 1];
	u8 type;
	u8 delay;
	void *(*get_platform_data)(void *info);
	/* Custom handler for devices */
	void (*device_handler)(struct sfi_device_table_entry *pentry,
			       struct devs_id *dev);
};

#define sfi_device(i)								\
	static const struct devs_id *const __intel_mid_sfi_##i##_dev __used	\
	__attribute__((__section__(".x86_intel_mid_dev.init"))) = &i

/**
* struct mid_sd_board_info - template for SD device creation
* @name:		identifies the driver
* @bus_num:		board-specific identifier for a given SD controller
* @max_clk:		the maximum frequency device supports
* @platform_data:	the particular data stored there is driver-specific
*/
struct mid_sd_board_info {
	char		name[SFI_NAME_LEN];
	int		bus_num;
	unsigned short	addr;
	u32		max_clk;
	void		*platform_data;
};

/*
 * Medfield is the follow-up of Moorestown, it combines two chip solution into
 * one. Other than that it also added always-on and constant tsc and lapic
 * timers. Medfield is the platform name, and the chip name is called Penwell
 * we treat Medfield/Penwell as a variant of Moorestown. Penwell can be
 * identified via MSRs.
 */
enum intel_mid_cpu_type {
	/* 1 was Moorestown */
	INTEL_MID_CPU_CHIP_PENWELL = 2,
	INTEL_MID_CPU_CHIP_CLOVERVIEW,
	INTEL_MID_CPU_CHIP_TANGIER,
};

extern enum intel_mid_cpu_type __intel_mid_cpu_chip;

/**
 * struct intel_mid_ops - Interface between intel-mid & sub archs
 * @arch_setup: arch_setup function to re-initialize platform
 *		structures (x86_init, x86_platform_init)
 *
 * This structure can be extended if any new interface is required
 * between intel-mid & its sub arch files.
 */
struct intel_mid_ops {
	void (*arch_setup)(void);
};

/* Helper API's for INTEL_MID_OPS_INIT */
#define DECLARE_INTEL_MID_OPS_INIT(cpuname, cpuid)				\
	[cpuid] = get_##cpuname##_ops

/* Maximum number of CPU ops */
#define MAX_CPU_OPS(a)			(sizeof(a)/sizeof(void *))

/*
 * For every new cpu addition, a weak get_<cpuname>_ops() function needs be
 * declared in arch/x86/platform/intel_mid/intel_mid_weak_decls.h.
 */
#define INTEL_MID_OPS_INIT {							\
	DECLARE_INTEL_MID_OPS_INIT(penwell, INTEL_MID_CPU_CHIP_PENWELL),	\
	DECLARE_INTEL_MID_OPS_INIT(cloverview, INTEL_MID_CPU_CHIP_CLOVERVIEW),	\
	DECLARE_INTEL_MID_OPS_INIT(tangier, INTEL_MID_CPU_CHIP_TANGIER)		\
};

#ifdef CONFIG_X86_INTEL_MID

static inline enum intel_mid_cpu_type intel_mid_identify_cpu(void)
{
	return __intel_mid_cpu_chip;
}

static inline bool intel_mid_has_msic(void)
{
	return (intel_mid_identify_cpu() == INTEL_MID_CPU_CHIP_PENWELL);
}

#else /* !CONFIG_X86_INTEL_MID */

#define intel_mid_identify_cpu()	0
#define intel_mid_has_msic()		0

#endif /* !CONFIG_X86_INTEL_MID */

enum intel_mid_timer_options {
	INTEL_MID_TIMER_DEFAULT,
	INTEL_MID_TIMER_APBT_ONLY,
	INTEL_MID_TIMER_LAPIC_APBT,
};

extern enum intel_mid_timer_options intel_mid_timer_options;

/*
 * Penwell uses spread spectrum clock, so the freq number is not exactly
 * the same as reported by MSR based on SDM.
 */
#define FSB_FREQ_83SKU			83200
#define FSB_FREQ_100SKU			99840
#define FSB_FREQ_133SKU			133000

#define FSB_FREQ_167SKU			167000
#define FSB_FREQ_200SKU			200000
#define FSB_FREQ_267SKU			267000
#define FSB_FREQ_333SKU			333000
#define FSB_FREQ_400SKU			400000

/* Bus Select SoC Fuse value */
#define BSEL_SOC_FUSE_MASK		0x7
/* FSB 133MHz */
#define BSEL_SOC_FUSE_001		0x1
/* FSB 100MHz */
#define BSEL_SOC_FUSE_101		0x5
/* FSB 83MHz */
#define BSEL_SOC_FUSE_111		0x7

#define SFI_MTMR_MAX_NUM		8
#define SFI_MRTC_MAX			8

extern void intel_scu_devices_create(void);
extern void intel_scu_devices_destroy(void);

/* VRTC timer */
#define MRST_VRTC_MAP_SZ		1024
/* #define MRST_VRTC_PGOFFSET		0xc00 */

extern void intel_mid_rtc_init(void);

/* The offset for the mapping of global gpio pin to irq */
#define INTEL_MID_IRQ_OFFSET		0x100

#endif /* _ASM_X86_INTEL_MID_H */
