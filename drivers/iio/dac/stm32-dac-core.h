/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is part of STM32 DAC driver
 *
 * Copyright (C) 2017, STMicroelectronics - All Rights Reserved
 * Author: Fabrice Gasnier <fabrice.gasnier@st.com>.
 */

#ifndef __STM32_DAC_CORE_H
#define __STM32_DAC_CORE_H

#include <linux/regmap.h>

/* STM32 DAC registers */
#define STM32_DAC_CR		0x00
#define STM32_DAC_DHR12R1	0x08
#define STM32_DAC_DHR12R2	0x14
#define STM32_DAC_DOR1		0x2C
#define STM32_DAC_DOR2		0x30

/* STM32_DAC_CR bit fields */
#define STM32_DAC_CR_EN1		BIT(0)
#define STM32H7_DAC_CR_HFSEL		BIT(15)
#define STM32_DAC_CR_EN2		BIT(16)

/**
 * struct stm32_dac_common - stm32 DAC driver common data (for all instances)
 * @regmap: DAC registers shared via regmap
 * @vref_mv: reference voltage (mv)
 * @hfsel: high speed bus clock selected
 */
struct stm32_dac_common {
	struct regmap			*regmap;
	int				vref_mv;
	bool				hfsel;
};

#endif
