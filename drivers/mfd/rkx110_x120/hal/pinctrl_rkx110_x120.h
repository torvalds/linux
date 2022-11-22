/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Rockchip Electronics Co. Ltd.
 *
 * Author: Steven Liu <steven.liu@rock-chips.com>
 */

#ifndef _PINCTRL_CORE_H_
#define _PINCTRL_CORE_H_

#include <dt-bindings/mfd/rockchip-serdes.h>

#include "hal_def.h"
#include "hal_os_def.h"

typedef enum {
	PIN_UNDEF,
	PIN_RKX110,
	PIN_RKX120,
	PIN_ALL,
	PIN_MAX,
} HAL_PinType;

struct hwpin;

struct xferpin {
	HAL_PinType type;
	char *name; /* slave addr is expected */
	void *client;
	uint32_t bank;
	uint32_t mpins;
	uint32_t param;
	HAL_RegRead_t *read;
	HAL_RegWrite_t *write;
};

struct hwpin {
	char name[32];
	HAL_PinType type;
	uint32_t grf_base;
	uint32_t bank;
	uint32_t mpins;
	uint32_t param;
	struct xferpin xfer;
};

typedef enum {
	GPIO0_A0 = 0,
	GPIO0_A1,
	GPIO0_A2,
	GPIO0_A3,
	GPIO0_A4,
	GPIO0_A5,
	GPIO0_A6,
	GPIO0_A7,
	GPIO0_B0 = 8,
	GPIO0_B1,
	GPIO0_B2,
	GPIO0_B3,
	GPIO0_B4,
	GPIO0_B5,
	GPIO0_B6,
	GPIO0_B7,
	GPIO0_C0 = 16,
	GPIO0_C1,
	GPIO0_C2,
	GPIO0_C3,
	GPIO0_C4,
	GPIO0_C5,
	GPIO0_C6,
	GPIO0_C7,
	GPIO0_D0 = 24,
	GPIO0_D1,
	GPIO0_D2,
	GPIO0_D3,
	GPIO0_D4,
	GPIO0_D5,
	GPIO0_D6,
	GPIO0_D7,
	GPIO1_A0 = 32,
	GPIO1_A1,
	GPIO1_A2,
	GPIO1_A3,
	GPIO1_A4,
	GPIO1_A5,
	GPIO1_A6,
	GPIO1_A7,
	GPIO1_B0 = 40,
	GPIO1_B1,
	GPIO1_B2,
	GPIO1_B3,
	GPIO1_B4,
	GPIO1_B5,
	GPIO1_B6,
	GPIO1_B7,
	GPIO1_C0 = 48,
	GPIO1_C1,
	GPIO1_C2,
	GPIO1_C3,
	GPIO1_C4,
	GPIO1_C5,
	GPIO1_C6,
	GPIO1_C7,
	GPIO1_D0 = 56,
	GPIO1_D1,
	GPIO1_D2,
	GPIO1_D3,
	GPIO1_D4,
	GPIO1_D5,
	GPIO1_D6,
	GPIO1_D7,
	GPIO_NUM_MAX
} ePINCTRL_PIN;

typedef enum {
	PINCTRL_IOMUX_FUNC0,
	PINCTRL_IOMUX_FUNC1,
	PINCTRL_IOMUX_FUNC2,
	PINCTRL_IOMUX_FUNC3,
	PINCTRL_IOMUX_FUNC4,
	PINCTRL_IOMUX_FUNC5,
	PINCTRL_IOMUX_FUNC6,
	PINCTRL_IOMUX_FUNC7,
} ePINCTRL_iomuxFunc;

typedef enum {
	PINCTRL_PULL_NORMAL,
	PINCTRL_PULL_UP,
	PINCTRL_PULL_DOWN,
	PINCTRL_PULL_KEEP
} ePINCTRL_pullMode;

/*
 * Special pull configuration.
 * Only enable and disable.
 * The specific pull-up or pull-down can not be configured.
 */
typedef enum {
	PINCTRL_PULL_DIS,
	PINCTRL_PULL_EN
} ePINCTRL_pullEnable;

typedef enum {
	PINCTRL_DRIVE_LEVEL0,
	PINCTRL_DRIVE_LEVEL1,
	PINCTRL_DRIVE_LEVEL2,
	PINCTRL_DRIVE_LEVEL3,
	PINCTRL_DRIVE_LEVEL4,
	PINCTRL_DRIVE_LEVEL5,
	PINCTRL_DRIVE_LEVEL6,
	PINCTRL_DRIVE_LEVEL7
} ePINCTRL_driveLevel;

typedef enum {
	PINCTRL_SCHMITT_DIS,
	PINCTRL_SCHMITT_EN
} ePINCTRL_schmitt;

uint32_t HAL_PINCTRL_Read(struct hwpin *hw, uint32_t reg);
uint32_t HAL_PINCTRL_Write(struct hwpin *hw, uint32_t reg, uint32_t val);

HAL_Status HAL_PINCTRL_Init(void);
HAL_Status HAL_PINCTRL_SetParam(struct hwpin *hw, uint32_t mPins, uint32_t param);
HAL_Status HAL_PINCTRL_SetIOMUX(struct hwpin *hw, uint32_t mPins, uint32_t param);

#endif /* _PINCTRL_CORE_H_ */
