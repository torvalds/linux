/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Secure boot definitions
 *
 * Copyright (C) 2019 IBM Corporation
 * Author: Nayna Jain
 */
#ifndef _ASM_POWER_SECURE_BOOT_H
#define _ASM_POWER_SECURE_BOOT_H

#ifdef CONFIG_PPC_SECURE_BOOT

bool is_ppc_secureboot_enabled(void);
bool is_ppc_trustedboot_enabled(void);

#else

static inline bool is_ppc_secureboot_enabled(void)
{
	return false;
}

static inline bool is_ppc_trustedboot_enabled(void)
{
	return false;
}

#endif
#endif
