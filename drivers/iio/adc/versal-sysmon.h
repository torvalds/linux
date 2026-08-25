/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AMD Versal SysMon driver
 *
 * Copyright (C) 2019 - 2022, Xilinx, Inc.
 * Copyright (C) 2022 - 2026, Advanced Micro Devices, Inc.
 */

#ifndef _VERSAL_SYSMON_H_
#define _VERSAL_SYSMON_H_

#include <linux/bits.h>
#include <linux/mutex.h>
#include <linux/spinlock_types.h>
#include <linux/workqueue.h>

struct device;
struct regmap;

/* Register offsets (sorted by address) */
#define SYSMON_NPI_LOCK			0x000C
#define SYSMON_ISR			0x0044
#define SYSMON_IMR			0x0048
#define SYSMON_IER			0x004C
#define SYSMON_IDR			0x0050
#define SYSMON_CONFIG			0x0100
#define SYSMON_TEMP_MAX			0x1030
#define SYSMON_TEMP_MIN			0x1034
#define SYSMON_SUPPLY_BASE		0x1040
#define SYSMON_ALARM_FLAG		0x1018
#define SYSMON_ALARM_REG		0x1940
#define SYSMON_SUPPLY_EN_AVG_BASE	0x1958
#define SYSMON_TEMP_TH_LOW		0x1970
#define SYSMON_TEMP_TH_UP		0x1974
#define SYSMON_SUPPLY_TH_LOW		0x1980
#define SYSMON_SUPPLY_TH_UP		0x1C80
#define SYSMON_TEMP_EV_CFG		0x1F84
#define SYSMON_TEMP_MIN_MIN		0x1F8C
#define SYSMON_TEMP_MAX_MAX		0x1F90
#define SYSMON_STATUS_RESET		0x1F94
#define SYSMON_TEMP_SAT_BASE		0x1FAC
#define SYSMON_TEMP_EN_AVG_BASE		0x24B4
#define SYSMON_MAX_REG			0x24C0

/* NPI unlock value written to SYSMON_NPI_LOCK */
#define SYSMON_NPI_UNLOCK_CODE		0xF9E8D7C6

/* Register stride: 4 bytes per 32-bit register */
#define SYSMON_REG_STRIDE		4

#define SYSMON_SUPPLY_IDX_MAX		159
#define SYSMON_TEMP_SAT_MAX		64
#define SYSMON_NO_OF_EVENTS		32
#define SYSMON_INTR_ALL_MASK		GENMASK(31, 0)

/* ISR/IMR temperature alarm mask (bit 9) */
#define SYSMON_TEMP_INTR_MASK		BIT(9)

/* SYSMON_CONFIG: supply oversampling ratio */
#define SYSMON_CONFIG_SUPPLY_OSR	GENMASK(17, 14)

/* SYSMON_CONFIG: temperature satellite oversampling ratio */
#define SYSMON_CONFIG_TEMP_SAT_OSR	GENMASK(27, 24)

/* Per-channel averaging enable register counts */
#define SYSMON_SUPPLY_EN_AVG_COUNT	5
#define SYSMON_TEMP_EN_AVG_COUNT	2

/* Supply voltage conversion register fields */
#define SYSMON_MANTISSA_MASK		GENMASK(15, 0)
#define SYSMON_FMT_MASK			BIT(16)
#define SYSMON_MODE_MASK		GENMASK(18, 17)

/* Q8.7 fractional shift */
#define SYSMON_FRACTIONAL_SHIFT		7U
#define SYSMON_SUPPLY_MANTISSA_BITS	16

/* Bits per alarm register */
#define SYSMON_ALARM_BITS_PER_REG	32

#define SYSMON_UNMASK_WORK_DELAY_MS	500

/**
 * struct sysmon - Driver data for Versal SysMon
 * @regmap: register map for hardware access
 * @lock: protects read-modify-write sequences on threshold registers
 *        and cached state that spans multiple regmap calls
 * @irq_lock: protects interrupt mask register updates (MMIO path only)
 * @masked_temp: currently masked temperature alarm bits
 * @temp_mask: temperature interrupt configuration mask
 * @temp_hysteresis: cached DEVICE_TEMP hysteresis in millicelsius
 * @sysmon_unmask_work: re-enables events after alarm condition clears
 * @temp_oversampling: current temp oversampling ratio
 * @supply_oversampling: current supply oversampling ratio
 */
struct sysmon {
	struct regmap *regmap;
	/*
	 * Protects read-modify-write sequences on threshold registers
	 * and cached state (oversampling ratios, hysteresis values)
	 * that spans multiple regmap calls.
	 */
	struct mutex lock;
	/*
	 * Protects interrupt mask register updates.  Only used on the
	 * MMIO path (fast_io regmap); I2C has no IRQ and never reaches
	 * the event code that takes this lock.
	 */
	spinlock_t irq_lock;
	unsigned int masked_temp;
	unsigned int temp_mask;
	int temp_hysteresis;
	struct delayed_work sysmon_unmask_work;
	unsigned int temp_oversampling;
	unsigned int supply_oversampling;
};

int devm_versal_sysmon_core_probe(struct device *dev, struct regmap *regmap);

#endif /* _VERSAL_SYSMON_H_ */
