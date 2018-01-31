/* SPDX-License-Identifier: GPL-2.0 */

#ifndef ROCKCHIP_H
#define ROCKCHIP_H

#include <mach/gpio.h>

#define CT36X_TS_I2C_BUS			2	// I2C Bus
#define CT36X_TS_I2C_ADDRESS			0x01
#define CT36X_TS_I2C_SPEED			400000

#if defined(CONFIG_MACH_RK3188M_F304) || defined(CONFIG_MACH_RK3168M_F304)
#define CT36X_TS_IRQ_PIN                       RK30_PIN1_PB7
#define CT36X_TS_RST_PIN                       RK30_PIN0_PB6
#else
#define CT36X_TS_IRQ_PIN			RK30_PIN4_PC2
#define CT36X_TS_RST_PIN			RK30_PIN4_PD0
#endif

#endif

