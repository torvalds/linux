// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Zorro Liu <zorro.liu@rock-chips.com>
 */

#ifndef EBC_PMIC_H
#define EBC_PMIC_H

#include "../ebc_dev.h"

#define VCOM_MIN_MV		0
#define VCOM_MAX_MV		5110

struct ebc_pmic {
	struct device *dev;
	char pmic_name[16];
	void *drvpar;
	void (*pmic_power_req)(struct ebc_pmic *pmic, bool up);
	void (*pmic_pm_suspend)(struct ebc_pmic *pmic);
	void (*pmic_pm_resume)(struct ebc_pmic *pmic);
	int (*pmic_read_temperature)(struct ebc_pmic *pmic, int *t);
	int (*pmic_get_vcom)(struct ebc_pmic *pmic);
	int (*pmic_set_vcom)(struct ebc_pmic *pmic, int value);
};

static inline void ebc_pmic_power_on(struct ebc_pmic *pmic)
{
	return pmic->pmic_power_req(pmic, 1);
}

static inline void ebc_pmic_power_off(struct ebc_pmic *pmic)
{
	return pmic->pmic_power_req(pmic, 0);
}

static inline void ebc_pmic_suspend(struct ebc_pmic *pmic)
{
	return pmic->pmic_pm_suspend(pmic);
}

static inline void ebc_pmic_resume(struct ebc_pmic *pmic)
{
	return pmic->pmic_pm_resume(pmic);
}

static inline int ebc_pmic_read_temp(struct ebc_pmic *pmic, int *t)
{
	return pmic->pmic_read_temperature(pmic, t);
}

static inline int ebc_pmic_get_vcom(struct ebc_pmic *pmic)
{
	return pmic->pmic_get_vcom(pmic);
}

int ebc_pmic_set_vcom(struct ebc_pmic *pmic, int value);
void ebc_pmic_verity_vcom(struct ebc_pmic *pmic);
#endif
