/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _ASM_S390_TYPES_H
#define _ASM_S390_TYPES_H

#include <uapi/asm/types.h>

#ifndef __ASSEMBLER__

union register_pair {
	unsigned __int128 pair;
	struct {
		unsigned long even;
		unsigned long odd;
	};
};

#endif /* __ASSEMBLER__ */
#endif /* _ASM_S390_TYPES_H */
