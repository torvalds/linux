/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MACH_MMP2_H
#define __ASM_MACH_MMP2_H

#include <linux/platform_data/pxa_sdhci.h>

extern void mmp2_timer_init(void);
extern void __init mmp2_init_icu(void);
extern void __init mmp2_init_irq(void);
extern void mmp2_clear_pmic_int(void);

#include <linux/i2c.h>
#include <linux/platform_data/i2c-pxa.h>
#include <linux/platform_data/dma-mmp_tdma.h>

#include "devices.h"

extern struct mmp_device_desc mmp2_device_uart1;
extern struct mmp_device_desc mmp2_device_uart2;
extern struct mmp_device_desc mmp2_device_uart3;
extern struct mmp_device_desc mmp2_device_uart4;
extern struct mmp_device_desc mmp2_device_twsi1;
extern struct mmp_device_desc mmp2_device_twsi2;
extern struct mmp_device_desc mmp2_device_twsi3;
extern struct mmp_device_desc mmp2_device_twsi4;
extern struct mmp_device_desc mmp2_device_twsi5;
extern struct mmp_device_desc mmp2_device_twsi6;
extern struct mmp_device_desc mmp2_device_sdh0;
extern struct mmp_device_desc mmp2_device_sdh1;
extern struct mmp_device_desc mmp2_device_sdh2;
extern struct mmp_device_desc mmp2_device_sdh3;
extern struct mmp_device_desc mmp2_device_asram;
extern struct mmp_device_desc mmp2_device_isram;

extern struct platform_device mmp2_device_gpio;

static inline int mmp2_add_uart(int id)
{
	struct mmp_device_desc *d = NULL;

	switch (id) {
	case 1: d = &mmp2_device_uart1; break;
	case 2: d = &mmp2_device_uart2; break;
	case 3: d = &mmp2_device_uart3; break;
	case 4: d = &mmp2_device_uart4; break;
	default:
		return -EINVAL;
	}

	return mmp_register_device(d, NULL, 0);
}

static inline int mmp2_add_twsi(int id, struct i2c_pxa_platform_data *data,
				  struct i2c_board_info *info, unsigned size)
{
	struct mmp_device_desc *d = NULL;
	int ret;

	switch (id) {
	case 1: d = &mmp2_device_twsi1; break;
	case 2: d = &mmp2_device_twsi2; break;
	case 3: d = &mmp2_device_twsi3; break;
	case 4: d = &mmp2_device_twsi4; break;
	case 5: d = &mmp2_device_twsi5; break;
	case 6: d = &mmp2_device_twsi6; break;
	default:
		return -EINVAL;
	}

	ret = i2c_register_board_info(id - 1, info, size);
	if (ret)
		return ret;

	return mmp_register_device(d, data, sizeof(*data));
}

static inline int mmp2_add_sdhost(int id, struct sdhci_pxa_platdata *data)
{
	struct mmp_device_desc *d = NULL;

	switch (id) {
	case 0: d = &mmp2_device_sdh0; break;
	case 1: d = &mmp2_device_sdh1; break;
	case 2: d = &mmp2_device_sdh2; break;
	case 3: d = &mmp2_device_sdh3; break;
	default:
		return -EINVAL;
	}

	return mmp_register_device(d, data, sizeof(*data));
}

static inline int mmp2_add_asram(struct sram_platdata *data)
{
	return mmp_register_device(&mmp2_device_asram, data, sizeof(*data));
}

static inline int mmp2_add_isram(struct sram_platdata *data)
{
	return mmp_register_device(&mmp2_device_isram, data, sizeof(*data));
}

#endif /* __ASM_MACH_MMP2_H */

