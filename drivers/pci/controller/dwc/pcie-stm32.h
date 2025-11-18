/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ST PCIe driver definitions for STM32-MP25 SoC
 *
 * Copyright (C) 2025 STMicroelectronics - All Rights Reserved
 * Author: Christian Bruel <christian.bruel@foss.st.com>
 */

#define to_stm32_pcie(x)	dev_get_drvdata((x)->dev)

#define STM32MP25_PCIECR_TYPE_MASK	GENMASK(11, 8)
#define STM32MP25_PCIECR_EP		0
#define STM32MP25_PCIECR_LTSSM_EN	BIT(2)
#define STM32MP25_PCIECR_RC		BIT(10)

#define SYSCFG_PCIECR			0x6000
