// SPDX-License-Identifier: GPL-2.0+
/*
 * PowerPC Memory Protection Keys management
 *
 * Copyright 2017, Ram Pai, IBM Corporation.
 */

#include <linux/pkeys.h>

DEFINE_STATIC_KEY_TRUE(pkey_disabled);
bool pkey_execute_disable_supported;
int  pkeys_total;		/* Total pkeys as per device tree */
u32  initial_allocation_mask;	/* Bits set for reserved keys */

int pkey_initialize(void)
{
	int os_reserved, i;

	/*
	 * Disable the pkey system till everything is in place. A subsequent
	 * patch will enable it.
	 */
	static_branch_enable(&pkey_disabled);

	/* Lets assume 32 keys */
	pkeys_total = 32;

	/*
	 * Adjust the upper limit, based on the number of bits supported by
	 * arch-neutral code.
	 */
	pkeys_total = min_t(int, pkeys_total,
			(ARCH_VM_PKEY_FLAGS >> VM_PKEY_SHIFT));

	/*
	 * Disable execute_disable support for now. A subsequent patch will
	 * enable it.
	 */
	pkey_execute_disable_supported = false;

#ifdef CONFIG_PPC_4K_PAGES
	/*
	 * The OS can manage only 8 pkeys due to its inability to represent them
	 * in the Linux 4K PTE.
	 */
	os_reserved = pkeys_total - 8;
#else
	os_reserved = 0;
#endif
	/*
	 * Bits are in LE format. NOTE: 1, 0 are reserved.
	 * key 0 is the default key, which allows read/write/execute.
	 * key 1 is recommended not to be used. PowerISA(3.0) page 1015,
	 * programming note.
	 */
	initial_allocation_mask = ~0x0;
	for (i = 2; i < (pkeys_total - os_reserved); i++)
		initial_allocation_mask &= ~(0x1 << i);
	return 0;
}

arch_initcall(pkey_initialize);

void pkey_mm_init(struct mm_struct *mm)
{
	if (static_branch_likely(&pkey_disabled))
		return;
	mm_pkey_allocation_map(mm) = initial_allocation_mask;
}
