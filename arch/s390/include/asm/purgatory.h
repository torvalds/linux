/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 2018
 *
 * Author(s): Philipp Rudo <prudo@linux.vnet.ibm.com>
 */

#ifndef _S390_PURGATORY_H_
#define _S390_PURGATORY_H_
#ifndef __ASSEMBLY__

#include <linux/purgatory.h>

int verify_sha256_digest(void);

extern u64 kernel_entry;
extern u64 kernel_type;

extern u64 crash_start;
extern u64 crash_size;

#endif	/* __ASSEMBLY__ */
#endif /* _S390_PURGATORY_H_ */
