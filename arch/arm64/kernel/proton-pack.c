// SPDX-License-Identifier: GPL-2.0-only
/*
 * Handle detection, reporting and mitigation of Spectre v1, v2 and v4, as
 * detailed at:
 *
 *   https://developer.arm.com/support/arm-security-updates/speculative-processor-vulnerability
 *
 * This code was originally written hastily under an awful lot of stress and so
 * aspects of it are somewhat hacky. Unfortunately, changing anything in here
 * instantly makes me feel ill. Thanks, Jann. Thann.
 *
 * Copyright (C) 2018 ARM Ltd, All Rights Reserved.
 * Copyright (C) 2020 Google LLC
 *
 * "If there's something strange in your neighbourhood, who you gonna call?"
 *
 * Authors: Will Deacon <will@kernel.org> and Marc Zyngier <maz@kernel.org>
 */

#include <linux/device.h>

/*
 * Spectre v1.
 *
 * The kernel can't protect userspace for this one: it's each person for
 * themselves. Advertise what we're doing and be done with it.
 */
ssize_t cpu_show_spectre_v1(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "Mitigation: __user pointer sanitization\n");
}
