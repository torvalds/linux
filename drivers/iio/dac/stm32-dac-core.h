/*
 * This file is part of STM32 DAC driver
 *
 * Copyright (C) 2017, STMicroelectronics - All Rights Reserved
 * Author: Fabrice Gasnier <fabrice.gasnier@st.com>.
 *
 * License type: GPLv2
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
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
