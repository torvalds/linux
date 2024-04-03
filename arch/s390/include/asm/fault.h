/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    Copyright IBM Corp. 1999, 2023
 */
#ifndef _ASM_S390_FAULT_H
#define _ASM_S390_FAULT_H

union teid {
	unsigned long val;
	struct {
		unsigned long addr : 52; /* Translation-exception Address */
		unsigned long fsi  : 2;	 /* Access Exception Fetch/Store Indication */
		unsigned long	   : 2;
		unsigned long b56  : 1;
		unsigned long	   : 3;
		unsigned long b60  : 1;
		unsigned long b61  : 1;
		unsigned long as   : 2;	 /* ASCE Identifier */
	};
};

enum {
	TEID_FSI_UNKNOWN = 0, /* Unknown whether fetch or store */
	TEID_FSI_STORE	 = 1, /* Exception was due to store operation */
	TEID_FSI_FETCH	 = 2  /* Exception was due to fetch operation */
};

#endif /* _ASM_S390_FAULT_H */
