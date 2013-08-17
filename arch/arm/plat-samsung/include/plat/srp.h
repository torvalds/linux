/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __EXYNOS_SRP_H_
#define __EXYNOS_SRP_H_

/* srp buffer information */
struct exynos_srp_buf {
	unsigned int base;
	unsigned long size;
	unsigned int offset;
	unsigned int num;
};

/* platform data to use srp core. */
struct exynos_srp_pdata {
/* If the h/w reset is needed */
#define SRP_HW_RESET		(0x0)
/* If the s/w reset is needed */
#define SRP_SW_RESET		(0x1)
	u32 type;
	bool use_iram;
	unsigned long iram_size;
	unsigned long icache_size;
	unsigned long dmem_size;
	unsigned long cmem_size;
	unsigned long commbox_size;

	struct exynos_srp_buf ibuf;
	struct exynos_srp_buf obuf;
	struct exynos_srp_buf idma;
};

#endif /* __EXYNOS_SRP_H_ */
