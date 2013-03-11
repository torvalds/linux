/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define FIMC_IS_I2C_COMPATIBLE	"samsung,exynos4212-i2c-isp"

int fimc_is_register_i2c_driver(void);
void fimc_is_unregister_i2c_driver(void);
