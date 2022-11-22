// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Rockchip Electronics Co. Ltd.
 *
 * Author: Steven Liu <steven.liu@rock-chips.com>
 */

#include "pinctrl_rkx110_x120.h"

/********************* Private MACRO Definition ******************************/

#define _PINCTRL_GENMASK(w)		((1U << (w)) - 1U)
#define _PINCTRL_OFFSET(gp, w)		((gp) * (w))
#define _PINCTRL_GENVAL(gp, v, w)	((_PINCTRL_GENMASK(w) << (_PINCTRL_OFFSET(gp, w) + 16)) \
					| (((v) & _PINCTRL_GENMASK(w)) << _PINCTRL_OFFSET(gp, w)))

#define RKX110_GRF_BASE			0x00010000U
#define RKX120_GRF_BASE			0x01010000U

#define RKX110_GRF_REG(x)		((x) + RKX110_GRF_BASE)
#define RKX120_GRF_REG(x)		((x) + RKX120_GRF_BASE)

/************************** SER_GRF Register Define ***************************/
#define RKX110_GRF_GPIO0A_IOMUX_L	RKX110_GRF_REG(0x0000)
#define RKX110_GRF_GPIO0A_IOMUX_H	RKX110_GRF_REG(0x0004)
#define RKX110_GRF_GPIO0B_IOMUX_L	RKX110_GRF_REG(0x0008)
#define RKX110_GRF_GPIO0B_IOMUX_H	RKX110_GRF_REG(0x000c)
#define RKX110_GRF_GPIO0C_IOMUX_L	RKX110_GRF_REG(0x0010)
#define RKX110_GRF_GPIO0C_IOMUX_H	RKX110_GRF_REG(0x0014)

#define RKX110_GRF_GPIO0A_P		RKX110_GRF_REG(0x0020)
#define RKX110_GRF_GPIO0B_P		RKX110_GRF_REG(0x0024)
#define RKX110_GRF_GPIO0C_P		RKX110_GRF_REG(0x0028)

#define RKX110_GRF_GPIO1A_IOMUX		RKX110_GRF_REG(0x0080)
#define RKX110_GRF_GPIO1B_IOMUX		RKX110_GRF_REG(0x0084)
#define RKX110_GRF_GPIO1C_IOMUX		RKX110_GRF_REG(0x0088)

#define RKX110_GRF_GPIO1A_P		RKX110_GRF_REG(0x0090)
#define RKX110_GRF_GPIO1B_P		RKX110_GRF_REG(0x0094)
#define RKX110_GRF_GPIO1C_P		RKX110_GRF_REG(0x0098)

#define RKX110_GRF_GPIO1A_SMT		RKX110_GRF_REG(0x00A0)
#define RKX110_GRF_GPIO1B_SMT		RKX110_GRF_REG(0x00A4)
#define RKX110_GRF_GPIO1C_SMT		RKX110_GRF_REG(0x00A8)

#define RKX110_GRF_GPIO1A_E		RKX110_GRF_REG(0x00B0)
#define RKX110_GRF_GPIO1B_E		RKX110_GRF_REG(0x00B4)
#define RKX110_GRF_GPIO1C_E		RKX110_GRF_REG(0x00B8)

#define RKX110_GRF_GPIO1A_IE		RKX110_GRF_REG(0x00C0)
#define RKX110_GRF_GPIO1B_IE		RKX110_GRF_REG(0x00C4)
#define RKX110_GRF_GPIO1C_IE		RKX110_GRF_REG(0x00C8)

/************************** DES_GRF Register Define ***************************/
#define RKX120_GRF_GPIO0A_IOMUX_L	RKX120_GRF_REG(0x0000)
#define RKX120_GRF_GPIO0A_IOMUX_H	RKX120_GRF_REG(0x0004)
#define RKX120_GRF_GPIO0B_IOMUX_L	RKX120_GRF_REG(0x0008)
#define RKX120_GRF_GPIO0B_IOMUX_H	RKX120_GRF_REG(0x000C)
#define RKX120_GRF_GPIO0C_IOMUX_L	RKX120_GRF_REG(0x0010)
#define RKX120_GRF_GPIO0C_IOMUX_H	RKX120_GRF_REG(0x0014)

#define RKX120_GRF_GPIO0A_P		RKX120_GRF_REG(0x0020)
#define RKX120_GRF_GPIO0B_P		RKX120_GRF_REG(0x0024)
#define RKX120_GRF_GPIO0C_P		RKX120_GRF_REG(0x0028)

#define RKX120_GRF_GPIO1A_IOMUX		RKX120_GRF_REG(0x0080)
#define RKX120_GRF_GPIO1B_IOMUX		RKX120_GRF_REG(0x0084)
#define RKX120_GRF_GPIO1C_IOMUX		RKX120_GRF_REG(0x0088)

#define RKX120_GRF_GPIO1A_P		RKX120_GRF_REG(0x0090)
#define RKX120_GRF_GPIO1B_P		RKX120_GRF_REG(0x0094)
#define RKX120_GRF_GPIO1C_P		RKX120_GRF_REG(0x0098)

#define RKX120_GRF_GPIO1A_SMT		RKX120_GRF_REG(0x00A0)
#define RKX120_GRF_GPIO1B_SMT		RKX120_GRF_REG(0x00A4)
#define RKX120_GRF_GPIO1C_SMT		RKX120_GRF_REG(0x00A8)

#define RKX120_GRF_GPIO1A_E		RKX120_GRF_REG(0x00B0)
#define RKX120_GRF_GPIO1B_E		RKX120_GRF_REG(0x00B4)
#define RKX120_GRF_GPIO1C_E		RKX120_GRF_REG(0x00B8)

#define RKX120_GRF_GPIO1A_IE		RKX120_GRF_REG(0x00C0)
#define RKX120_GRF_GPIO1B_IE		RKX120_GRF_REG(0x00C4)
#define RKX120_GRF_GPIO1C_IE		RKX120_GRF_REG(0x00C8)

#define IOMUX_3_BIT_PER_PIN			(3)
#define IOMUX_PER_REG				(8)
#define RKX110_IOMUX_L(__B, __G)		(RKX110_GRF_GPIO##__B##__G##_IOMUX_L)
#define RKX110_IOMUX_H(__B, __G)		(RKX110_GRF_GPIO##__B##__G##_IOMUX_H)
#define RKX110_SET_IOMUX_L(_HW, _B, _G, _GP, _V, _W)				\
{										\
	HAL_PINCTRL_Write(_HW, RKX110_IOMUX_L(_B, _G), _PINCTRL_GENVAL(_GP, _V, _W));	\
}
#define RKX110_SET_IOMUX_H(_HW, _B, _G, _GP, _V, _W)				\
{										\
	HAL_PINCTRL_Write(_HW, RKX110_IOMUX_H(_B, _G), _PINCTRL_GENVAL(_GP, _V, _W));	\
}
#define HAL_RKX110_SET_IOMUX_L(HW, B, G, BP, V)					\
	RKX110_SET_IOMUX_L(HW, B, G, BP % IOMUX_PER_REG, V, IOMUX_3_BIT_PER_PIN)
#define HAL_RKX110_SET_IOMUX_H(HW, B, G, BP, V)					\
	RKX110_SET_IOMUX_H(HW, B, G, (BP % IOMUX_PER_REG - 5), V, IOMUX_3_BIT_PER_PIN)

#define RKX120_IOMUX_L(__B, __G)		(RKX120_GRF_GPIO##__B##__G##_IOMUX_L)
#define RKX120_IOMUX_H(__B, __G)		(RKX120_GRF_GPIO##__B##__G##_IOMUX_H)
#define RKX120_SET_IOMUX_L(_HW, _B, _G, _GP, _V, _W)				\
{										\
	HAL_PINCTRL_Write(_HW, RKX120_IOMUX_L(_B, _G), _PINCTRL_GENVAL(_GP, _V, _W));	\
}
#define RKX120_SET_IOMUX_H(_HW, _B, _G, _GP, _V, _W)				\
{										\
	HAL_PINCTRL_Write(_HW, RKX120_IOMUX_H(_B, _G), _PINCTRL_GENVAL(_GP, _V, _W));	\
}
#define HAL_RKX120_SET_IOMUX_L(HW, B, G, BP, V)					\
	RKX120_SET_IOMUX_L(HW, B, G, BP % IOMUX_PER_REG, V, IOMUX_3_BIT_PER_PIN)
#define HAL_RKX120_SET_IOMUX_H(HW, B, G, BP, V)					\
	RKX120_SET_IOMUX_H(HW, B, G, (BP % IOMUX_PER_REG - 5), V, IOMUX_3_BIT_PER_PIN)

#define IOMUX_2_BIT_PER_PIN			(2)
#define IOMUX_8_PIN_PER_REG			(8)
#define RKX110_IOMUX_0(__B, __G)		(RKX110_GRF_GPIO##__B##__G##_IOMUX)

#define RKX110_SET_IOMUX_0(_HW, _B, _G, _GP, _V, _W)				\
{										\
	HAL_PINCTRL_Write(_HW, RKX110_IOMUX_0(_B, _G), _PINCTRL_GENVAL(_GP, _V, _W));	\
}

#define HAL_RKX110_SET_IOMUX_0(HW, B, G, BP, V)					\
	RKX110_SET_IOMUX_0(HW, B, G, BP % IOMUX_8_PIN_PER_REG, V, IOMUX_2_BIT_PER_PIN)

#define RKX120_IOMUX_0(__B, __G)		(RKX120_GRF_GPIO##__B##__G##_IOMUX)

#define RKX120_SET_IOMUX_0(_HW, _B, _G, _GP, _V, _W)				\
{										\
	HAL_PINCTRL_Write(_HW, RKX120_IOMUX_0(_B, _G), _PINCTRL_GENVAL(_GP, _V, _W));	\
}

#define HAL_RKX120_SET_IOMUX_0(HW, B, G, BP, V)					\
	RKX120_SET_IOMUX_0(HW, B, G, BP % IOMUX_8_PIN_PER_REG, V, IOMUX_2_BIT_PER_PIN)

#define PULL_2_BIT_PER_PIN			(2)
#define PULL_8_PIN_PER_REG			(8)
#define RKX110_PULL_0(__B, __G)			(RKX110_GRF_GPIO##__B##__G##_P)

#define RKX110_SET_PULL_0(_HW, _B, _G, _GP, _V, _W)				\
{										\
	HAL_PINCTRL_Write(_HW, RKX110_PULL_0(_B, _G), _PINCTRL_GENVAL(_GP, _V, _W));	\
}

#define HAL_RKX110_SET_PULL_0(HW, B, G, BP, V)					\
	RKX110_SET_PULL_0(HW, B, G, BP % PULL_8_PIN_PER_REG, V, PULL_2_BIT_PER_PIN)

#define RKX120_PULL_0(__B, __G)			(RKX120_GRF_GPIO##__B##__G##_P)

#define RKX120_SET_PULL_0(_HW, _B, _G, _GP, _V, _W)				\
{										\
	HAL_PINCTRL_Write(_HW, RKX120_PULL_0(_B, _G), _PINCTRL_GENVAL(_GP, _V, _W));	\
}

#define HAL_RKX120_SET_PULL_0(HW, B, G, BP, V)					\
	RKX120_SET_PULL_0(HW, B, G, BP % PULL_8_PIN_PER_REG, V, PULL_2_BIT_PER_PIN)

#define PULLEN_1_BIT_PER_PIN			(1)
#define PULLEN_8_PIN_PER_REG			(8)
#define RKX110_PULLEN_0(__B, __G)		(RKX110_GRF_GPIO##__B##__G##_P)

#define RKX110_SET_PULLEN_0(_HW, _B, _G, _GP, _V, _W)				\
{										\
	HAL_PINCTRL_Write(_HW, RKX110_PULLEN_0(_B, _G), _PINCTRL_GENVAL(_GP, _V, _W));	\
}

#define HAL_RKX110_SET_PULLEN_0(HW, B, G, BP, V)				\
	RKX110_SET_PULLEN_0(HW, B, G, BP % PULLEN_8_PIN_PER_REG, V, PULLEN_1_BIT_PER_PIN)

#define RKX120_PULLEN_0(__B, __G)		(RKX120_GRF_GPIO##__B##__G##_P)

#define RKX120_SET_PULLEN_0(_HW, _B, _G, _GP, _V, _W)				\
{										\
	HAL_PINCTRL_Write(_HW, RKX120_PULLEN_0(_B, _G), _PINCTRL_GENVAL(_GP, _V, _W));	\
}

#define HAL_RKX120_SET_PULLEN_0(HW, B, G, BP, V)				\
	RKX120_SET_PULLEN_0(HW, B, G, BP % PULLEN_8_PIN_PER_REG, V, PULLEN_1_BIT_PER_PIN)

#define DRV_2_BIT_PER_PIN			(2)
#define DRV_8_PIN_PER_REG			(8)
#define RKX110_DRV_0(__B, __G)			(RKX110_GRF_GPIO##__B##__G##_E)

#define RKX110_SET_DRV_0(_HW, _B, _G, _GP, _V, _W)				\
{										\
	HAL_PINCTRL_Write(_HW, RKX110_DRV_0(_B, _G), _PINCTRL_GENVAL(_GP, _V, _W));	\
}

#define HAL_RKX110_SET_DRV_0(HW, B, G, BP, V)					\
	RKX110_SET_DRV_0(HW, B, G, BP % DRV_8_PIN_PER_REG, V, DRV_2_BIT_PER_PIN)

#define RKX120_DRV_0(__B, __G)			(RKX120_GRF_GPIO##__B##__G##_E)

#define RKX120_SET_DRV_0(_HW, _B, _G, _GP, _V, _W)				\
{										\
	HAL_PINCTRL_Write(_HW, RKX120_DRV_0(_B, _G), _PINCTRL_GENVAL(_GP, _V, _W));	\
}

#define HAL_RKX120_SET_DRV_0(HW, B, G, BP, V)					\
	RKX120_SET_DRV_0(HW, B, G, BP % DRV_8_PIN_PER_REG, V, DRV_2_BIT_PER_PIN)

#define SMT_1_BIT_PER_PIN			(1)
#define SMT_8_PIN_PER_REG			(8)
#define RKX110_SMT_0(__B, __G)			(RKX110_GRF_GPIO##__B##__G##_SMT)

#define RKX110_SET_SMT_0(_HW, _B, _G, _GP, _V, _W)				\
{										\
	HAL_PINCTRL_Write(_HW, RKX110_SMT_0(_B, _G), _PINCTRL_GENVAL(_GP, _V, _W));	\
}

#define HAL_RKX110_SET_SMT_0(HW, B, G, BP, V)					\
	RKX110_SET_SMT_0(HW, B, G, BP % SMT_8_PIN_PER_REG, V, SMT_1_BIT_PER_PIN)

#define RKX120_SMT_0(__B, __G)			(RKX120_GRF_GPIO##__B##__G##_SMT)

#define RKX120_SET_SMT_0(_HW, _B, _G, _GP, _V, _W)				\
{										\
	HAL_PINCTRL_Write(_HW, RKX120_SMT_0(_B, _G), _PINCTRL_GENVAL(_GP, _V, _W));	\
}

#define HAL_RKX120_SET_SMT_0(HW, B, G, BP, V)					\
	RKX120_SET_SMT_0(HW, B, G, BP % SMT_8_PIN_PER_REG, V, SMT_1_BIT_PER_PIN)

/********************* Private Function Definition ***************************/

static HAL_Status RKX110_PINCTRL_SetIOMUX(struct hwpin *hw, uint8_t pin, uint32_t val)
{
	switch (hw->bank) {
	case 0:
		if (pin < 5) {
			HAL_RKX110_SET_IOMUX_L(hw, 0, A, pin, val);
		} else if (pin < 8) {
			HAL_RKX110_SET_IOMUX_H(hw, 0, A, pin, val);
		} else if (pin < 13) {
			HAL_RKX110_SET_IOMUX_L(hw, 0, B, pin, val);
		} else if (pin < 16) {
			HAL_RKX110_SET_IOMUX_H(hw, 0, B, pin, val);
		} else if (pin < 21) {
			HAL_RKX110_SET_IOMUX_L(hw, 0, C, pin, val);
		} else if (pin < 24) {
			HAL_RKX110_SET_IOMUX_H(hw, 0, C, pin, val);
		} else {
			HAL_ERR("PINCTRL: iomux unknown gpio pin-%d\n", pin);
		}
		break;
	case 1:
		if (pin < 8) {
			HAL_RKX110_SET_IOMUX_0(hw, 1, A, pin, val);
		} else if (pin < 16) {
			HAL_RKX110_SET_IOMUX_0(hw, 1, B, pin, val);
		} else if (pin < 24) {
			HAL_RKX110_SET_IOMUX_0(hw, 1, C, pin, val);
		} else {
			HAL_ERR("PINCTRL: iomux unknown gpio pin-%d\n", pin);
		}
		break;
	default:
		HAL_ERR("PINCTRL: iomux unknown gpio bank-%d\n", hw->bank);
		break;
	}

	return HAL_OK;
}

static HAL_Status RKX110_PINCTRL_SetPULL(struct hwpin *hw, uint8_t pin, uint32_t val)
{
	switch (hw->bank) {
	case 1:
		if (pin < 8) {
			HAL_RKX110_SET_PULL_0(hw, 1, A, pin, val);
		} else if (pin < 16) {
			HAL_RKX110_SET_PULL_0(hw, 1, B, pin, val);
		} else if (pin < 24) {
			HAL_RKX110_SET_PULL_0(hw, 1, C, pin, val);
		} else {
			HAL_ERR("PINCTRL: pull unknown gpio pin-%d\n", pin);
		}
		break;
	default:
		HAL_ERR("PINCTRL: pull unknown gpio bank-%d\n", hw->bank);
		break;
	}

	return HAL_OK;
}

static HAL_Status RKX110_PINCTRL_SetPULLEN(struct hwpin *hw, uint8_t pin, uint32_t val)
{
	switch (hw->bank) {
	case 0:
		if (pin < 8) {
			HAL_RKX110_SET_PULLEN_0(hw, 0, A, pin, val);
		} else if (pin < 16) {
			HAL_RKX110_SET_PULLEN_0(hw, 0, B, pin, val);
		} else if (pin < 24) {
			HAL_RKX110_SET_PULLEN_0(hw, 0, C, pin, val);
		} else {
			HAL_ERR("PINCTRL: pull enable unknown gpio pin-%d\n", pin);
		}
		break;
	default:
		HAL_ERR("PINCTRL: pull enable unknown gpio bank-%d\n", hw->bank);
		break;
	}

	return HAL_OK;
}

static HAL_Status RKX110_PINCTRL_SetDRV(struct hwpin *hw, uint8_t pin, uint32_t val)
{
	switch (hw->bank) {
	case 1:
		if (pin < 8) {
			HAL_RKX110_SET_DRV_0(hw, 1, A, pin, val);
		} else if (pin < 16) {
			HAL_RKX110_SET_DRV_0(hw, 1, B, pin, val);
		} else if (pin < 24) {
			HAL_RKX110_SET_DRV_0(hw, 1, C, pin, val);
		} else {
			HAL_ERR("PINCTRL: drive strengh unknown gpio pin-%d\n", pin);
		}
		break;
	default:
		HAL_ERR("PINCTRL: drive strengh gpio bank-%d\n", hw->bank);
		break;
	}

	return HAL_OK;
}

static HAL_Status RKX110_PINCTRL_SetSMT(struct hwpin *hw, uint8_t pin, uint32_t val)
{
	switch (hw->bank) {
	case 1:
		if (pin < 8) {
			HAL_RKX110_SET_SMT_0(hw, 1, A, pin, val);
		} else if (pin < 16) {
			HAL_RKX110_SET_SMT_0(hw, 1, B, pin, val);
		} else if (pin < 24) {
			HAL_RKX110_SET_SMT_0(hw, 1, C, pin, val);
		} else {
			HAL_ERR("PINCTRL: smt gpio pin-%d\n", pin);
		}
		break;
	default:
		HAL_ERR("PINCTRL: smt gpio bank-%d\n", hw->bank);
		break;
	}

	return HAL_OK;
}

static HAL_Status RKX120_PINCTRL_SetIOMUX(struct hwpin *hw, uint8_t pin, uint32_t val)
{
	switch (hw->bank) {
	case 5:
		if (pin < 5) {
			HAL_RKX120_SET_IOMUX_L(hw, 0, A, pin, val);
		} else if (pin < 8) {
			HAL_RKX120_SET_IOMUX_H(hw, 0, A, pin, val);
		} else if (pin < 13) {
			HAL_RKX120_SET_IOMUX_L(hw, 0, B, pin, val);
		} else if (pin < 16) {
			HAL_RKX120_SET_IOMUX_H(hw, 0, B, pin, val);
		} else if (pin < 21) {
			HAL_RKX120_SET_IOMUX_L(hw, 0, C, pin, val);
		} else if (pin < 24) {
			HAL_RKX120_SET_IOMUX_H(hw, 0, C, pin, val);
		} else {
			HAL_ERR("PINCTRL: iomux unknown gpio pin-%d\n", pin);
		}
		break;
	case 6:
		if (pin < 8) {
			HAL_RKX120_SET_IOMUX_0(hw, 1, A, pin, val);
		} else if (pin < 16) {
			HAL_RKX120_SET_IOMUX_0(hw, 1, B, pin, val);
		} else if (pin < 24) {
			HAL_RKX120_SET_IOMUX_0(hw, 1, C, pin, val);
		} else {
			HAL_ERR("PINCTRL: iomux unknown gpio pin-%d\n", pin);
		}
		break;
	default:
		HAL_ERR("PINCTRL: iomux unknown gpio bank-%d\n", hw->bank);
		break;
	}

	return HAL_OK;
}

static HAL_Status RKX120_PINCTRL_SetPULL(struct hwpin *hw, uint8_t pin, uint32_t val)
{
	switch (hw->bank) {
	case 6:
		if (pin < 8) {
			HAL_RKX120_SET_PULL_0(hw, 1, A, pin, val);
		} else if (pin < 16) {
			HAL_RKX120_SET_PULL_0(hw, 1, B, pin, val);
		} else if (pin < 24) {
			HAL_RKX120_SET_PULL_0(hw, 1, C, pin, val);
		} else {
			HAL_ERR("PINCTRL: pull unknown gpio pin-%d\n", pin);
		}
		break;
	default:
		HAL_ERR("PINCTRL: pull unknown gpio bank-%d\n", hw->bank);
		break;
	}

	return HAL_OK;
}

static HAL_Status RKX120_PINCTRL_SetPULLEN(struct hwpin *hw, uint8_t pin, uint32_t val)
{
	switch (hw->bank) {
	case 5:
		if (pin < 8) {
			HAL_RKX120_SET_PULLEN_0(hw, 0, A, pin, val);
		} else if (pin < 16) {
			HAL_RKX120_SET_PULLEN_0(hw, 0, B, pin, val);
		} else if (pin < 24) {
			HAL_RKX120_SET_PULLEN_0(hw, 0, C, pin, val);
		} else {
			HAL_ERR("PINCTRL: pull enable unknown gpio pin-%d\n", pin);
		}
		break;
	default:
		HAL_ERR("PINCTRL: pull enable unknown gpio bank-%d\n", hw->bank);
		break;
	}

	return HAL_OK;
}

static HAL_Status RKX120_PINCTRL_SetDRV(struct hwpin *hw, uint8_t pin, uint32_t val)
{
	switch (hw->bank) {
	case 6:
		if (pin < 8) {
			HAL_RKX120_SET_DRV_0(hw, 1, A, pin, val);
		} else if (pin < 16) {
			HAL_RKX120_SET_DRV_0(hw, 1, B, pin, val);
		} else if (pin < 24) {
			HAL_RKX120_SET_DRV_0(hw, 1, C, pin, val);
		} else {
			HAL_ERR("PINCTRL: drive strengh unknown gpio pin-%d\n", pin);
		}
		break;
	default:
		HAL_ERR("PINCTRL: drive strengh gpio bank-%d\n", hw->bank);
		break;
	}

	return HAL_OK;
}

static HAL_Status RKX120_PINCTRL_SetSMT(struct hwpin *hw, uint8_t pin, uint32_t val)
{
	switch (hw->bank) {
	case 6:
		if (pin < 8) {
			HAL_RKX120_SET_SMT_0(hw, 1, A, pin, val);
		} else if (pin < 16) {
			HAL_RKX120_SET_SMT_0(hw, 1, B, pin, val);
		} else if (pin < 24) {
			HAL_RKX120_SET_SMT_0(hw, 1, C, pin, val);
		} else {
			HAL_ERR("PINCTRL: smt gpio pin-%d\n", pin);
		}
		break;
	default:
		HAL_ERR("PINCTRL: smt gpio bank-%d\n", hw->bank);
		break;
	}

	return HAL_OK;
}

static HAL_Status PINCTRL_SetPinParam(struct hwpin *hw, uint8_t pin, uint32_t param)
{
	HAL_Status rc = HAL_OK;
	uint8_t val;

	if (hw->type == PIN_RKX110) {
		if (param & RK_SERDES_FLAG_MUX) {
			val = (param & RK_SERDES_MASK_MUX) >> RK_SERDES_SHIFT_MUX;
			rc |= RKX110_PINCTRL_SetIOMUX(hw, pin, val);
		}

		if (param & RK_SERDES_FLAG_PUL) {
			val = (param & RK_SERDES_MASK_PUL) >> RK_SERDES_SHIFT_PUL;
			rc |= RKX110_PINCTRL_SetPULL(hw, pin, val);
		}

		if (param & RK_SERDES_FLAG_PEN) {
			val = (param & RK_SERDES_MASK_PEN) >> RK_SERDES_SHIFT_PEN;
			rc |= RKX110_PINCTRL_SetPULLEN(hw, pin, val);
		}

		if (param & RK_SERDES_FLAG_DRV) {
			val = (param & RK_SERDES_MASK_DRV) >> RK_SERDES_SHIFT_DRV;
			rc |= RKX110_PINCTRL_SetDRV(hw, pin, val);
		}

		if (param & RK_SERDES_FLAG_SMT) {
			val = (param & RK_SERDES_MASK_SMT) >> RK_SERDES_SHIFT_SMT;
			rc |= RKX110_PINCTRL_SetSMT(hw, pin, val);
		}
	} else if (hw->type == PIN_RKX120) {
		if (param & RK_SERDES_FLAG_MUX) {
			val = (param & RK_SERDES_MASK_MUX) >> RK_SERDES_SHIFT_MUX;
			rc |= RKX120_PINCTRL_SetIOMUX(hw, pin, val);
		}

		if (param & RK_SERDES_FLAG_PUL) {
			val = (param & RK_SERDES_MASK_PUL) >> RK_SERDES_SHIFT_PUL;
			rc |= RKX120_PINCTRL_SetPULL(hw, pin, val);
		}

		if (param & RK_SERDES_FLAG_PEN) {
			val = (param & RK_SERDES_MASK_PEN) >> RK_SERDES_SHIFT_PEN;
			rc |= RKX120_PINCTRL_SetPULLEN(hw, pin, val);
		}

		if (param & RK_SERDES_FLAG_DRV) {
			val = (param & RK_SERDES_MASK_DRV) >> RK_SERDES_SHIFT_DRV;
			rc |= RKX120_PINCTRL_SetDRV(hw, pin, val);
		}

		if (param & RK_SERDES_FLAG_SMT) {
			val = (param & RK_SERDES_MASK_SMT) >> RK_SERDES_SHIFT_SMT;
			rc |= RKX120_PINCTRL_SetSMT(hw, pin, val);
		}
	} else {
		HAL_ERR("PINCTRL: error pin type %d\n", hw->type);
	}

	return rc;
}

/********************* Public Function Definition ***************************/

uint32_t HAL_PINCTRL_Read(struct hwpin *hw, uint32_t reg)
{
	uint32_t val;
	int ret;

	ret = hw->xfer.read(hw->xfer.client, reg, &val);
	if (ret) {
		HAL_ERR("%s: read reg=0x%08x failed, ret=%d\n", hw->name, reg, ret);
	}

	HAL_DBG("%s: %s: reg=0x%08x, val=0x%08x\n", __func__, hw->name, reg, val);

	return val;
}

uint32_t HAL_PINCTRL_Write(struct hwpin *hw, uint32_t reg, uint32_t val)
{
	int ret;

	HAL_DBG("%s: %s: reg=0x%08x, val=0x%08x\n", __func__, hw->name, reg, val);

	ret = hw->xfer.write(hw->xfer.client, reg, val);
	if (ret) {
		HAL_ERR("%s: write reg=0x%08x, val=0x%08x failed, ret=%d\n",
			hw->name, reg, val, ret);
	}

	return ret;
}

HAL_Status HAL_PINCTRL_Init(void)
{
	return HAL_OK;
}

HAL_Status HAL_PINCTRL_SetParam(struct hwpin *hw, uint32_t mPins, uint32_t param)
{
	uint8_t pin;
	HAL_Status rc;

	if (!(param & (RK_SERDES_FLAG_MUX | RK_SERDES_FLAG_PUL | RK_SERDES_FLAG_PEN |
		       RK_SERDES_FLAG_DRV | RK_SERDES_FLAG_SMT))) {
		HAL_ERR("PINCTRL: no parameter!\n");

		return HAL_ERROR;
	}

	for (pin = 0; pin < 32; pin++) {
		if (mPins & (1 << pin)) {
			rc = PINCTRL_SetPinParam(hw, pin, param);
			if (rc) {
				return rc;
			}
		}
	}

	return HAL_OK;
}

HAL_Status HAL_PINCTRL_SetIOMUX(struct hwpin *hw, uint32_t mPins, uint32_t param)
{
	return HAL_PINCTRL_SetParam(hw, mPins, param);
}

