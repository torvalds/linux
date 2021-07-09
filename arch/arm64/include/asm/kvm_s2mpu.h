/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#ifndef __ARM64_KVM_S2MPU_H__
#define __ARM64_KVM_S2MPU_H__

#define REG_NS_VERSION				0x60

#define VERSION_MAJOR_ARCH_VER_MASK		GENMASK(31, 28)
#define VERSION_MINOR_ARCH_VER_MASK		GENMASK(27, 24)
#define VERSION_REV_ARCH_VER_MASK		GENMASK(23, 16)
#define VERSION_RTL_VER_MASK			GENMASK(7, 0)

/* Ignore RTL version in driver version check. */
#define VERSION_CHECK_MASK			(VERSION_MAJOR_ARCH_VER_MASK | \
						 VERSION_MINOR_ARCH_VER_MASK | \
						 VERSION_REV_ARCH_VER_MASK)

enum s2mpu_version {
	S2MPU_VERSION_8 = 0x11000000,
	S2MPU_VERSION_9 = 0x20000000,
};

enum s2mpu_power_state {
	S2MPU_POWER_ALWAYS_ON,
	S2MPU_POWER_ON,
	S2MPU_POWER_OFF,
};

#endif /* __ARM64_KVM_S2MPU_H__ */
