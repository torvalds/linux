/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * SVM helper functions
 *
 * Copyright 2018 Anshuman Khandual, IBM Corporation.
 */

#ifndef _ASM_POWERPC_SVM_H
#define _ASM_POWERPC_SVM_H

#ifdef CONFIG_PPC_SVM

static inline bool is_secure_guest(void)
{
	return mfmsr() & MSR_S;
}

#else /* CONFIG_PPC_SVM */

static inline bool is_secure_guest(void)
{
	return false;
}

#endif /* CONFIG_PPC_SVM */
#endif /* _ASM_POWERPC_SVM_H */
