// SPDX-License-Identifier: GPL-2.0+
/*
 * PowerPC Memory Protection Keys management
 *
 * Copyright 2017, Ram Pai, IBM Corporation.
 */

#include <linux/pkeys.h>

DEFINE_STATIC_KEY_TRUE(pkey_disabled);
bool pkey_execute_disable_supported;

int pkey_initialize(void)
{
	/*
	 * Disable the pkey system till everything is in place. A subsequent
	 * patch will enable it.
	 */
	static_branch_enable(&pkey_disabled);

	/*
	 * Disable execute_disable support for now. A subsequent patch will
	 * enable it.
	 */
	pkey_execute_disable_supported = false;
	return 0;
}

arch_initcall(pkey_initialize);
