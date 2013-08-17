/*
 * Copyright (c) 2012 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the Gnu General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef _MACH_EXYNOS_EXYNOS5_BUS_H_
#define _MACH_EXYNOS_EXYNOS5_BUS_H_

struct exynos5_bus_mif_platform_data {
	unsigned long max_freq;
};

struct exynos5_bus_mif_handle;
struct exynos5_bus_int_handle;

#ifdef CONFIG_ARM_EXYNOS5_BUS_DEVFREQ
void exynos5_mif_multiple_windows(bool state);
void exynos5_mif_used_dev(bool power_on);
struct exynos5_bus_mif_handle *exynos5_bus_mif_get(unsigned long min_freq);
int exynos5_bus_mif_put(struct exynos5_bus_mif_handle *handle);
int exynos5_bus_mif_update(struct exynos5_bus_mif_handle *handle,
		unsigned long min_freq);

static inline
struct exynos5_bus_mif_handle *exynos5_bus_mif_min(unsigned long min_freq)
{
	return exynos5_bus_mif_get(min_freq);
}

struct exynos5_bus_int_handle *exynos5_bus_int_get(unsigned long min_freq,
		bool poll);
int exynos5_bus_int_put(struct exynos5_bus_int_handle *handle);

static inline struct exynos5_bus_int_handle *exynos5_bus_int_poll(void)
{
	return exynos5_bus_int_get(0, true);
}

static inline
struct exynos5_bus_int_handle *exynos5_bus_int_min(unsigned long min_freq)
{
	return exynos5_bus_int_get(min_freq, false);
}

void exynos5_ppmu_trace(void);
#else
static inline void exynos5_mif_multiple_windows(bool state)
{
}

static inline void exynos5_mif_used_dev(bool power_on)
{
}

static inline
struct exynos5_bus_mif_handle *exynos5_bus_mif_get(unsigned long min_freq)
{
	return NULL;
}

static inline
int exynos5_bus_mif_put(struct exynos5_bus_mif_handle *handle)
{
	return 0;
}

static inline
struct exynos5_bus_mif_handle *exynos5_bus_mif_min(unsigned long min_freq)
{
	return NULL;
}

static inline
struct exynos5_bus_int_handle *exynos5_bus_int_get(unsigned long min_freq,
		bool poll)
{
	return NULL;
}

static inline int exynos5_bus_int_put(struct exynos5_bus_int_handle *handle)
{
	return 0;
}

static inline struct exynos5_bus_int_handle *exynos5_bus_int_poll(void)
{
	return NULL;
}

static inline
struct exynos5_bus_int_handle *exynos5_bus_int_min(unsigned long min_freq)
{
	return exynos5_bus_int_get(min_freq, false);
}

static inline
int exynos5_bus_mif_update(struct exynos5_bus_mif_handle *handle,
		unsigned long min_freq)
{
	return 0;
}

static inline void exynos5_ppmu_trace(void)
{
}
#endif
#endif /* _MACH_EXYNOS_EXYNOS5_BUS_H_ */
