// SPDX-License-Identifier: GPL-2.0

#include <linux/ima.h>
#include <asm/boot_data.h>

bool arch_ima_get_secureboot(void)
{
	return ipl_secure_flag;
}

const char * const *arch_get_ima_policy(void)
{
	return NULL;
}
