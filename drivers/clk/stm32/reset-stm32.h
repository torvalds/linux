/* SPDX-License-Identifier: GPL-2.0  */
/*
 * Copyright (C) STMicroelectronics 2022 - All Rights Reserved
 * Author: Gabriel Fernandez <gabriel.fernandez@foss.st.com> for STMicroelectronics.
 */

int stm32_rcc_reset_init(struct device *dev, const struct of_device_id *match,
			 void __iomem *base);
