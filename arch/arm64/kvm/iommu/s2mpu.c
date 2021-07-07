// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#include <linux/kvm_host.h>

int kvm_s2mpu_init(void)
{
	kvm_info("S2MPU driver initialized\n");
	return 0;
}
