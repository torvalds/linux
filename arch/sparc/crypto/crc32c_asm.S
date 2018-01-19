/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/visasm.h>
#include <asm/asi.h>

#include "opcodes.h"

ENTRY(crc32c_sparc64)
	/* %o0=crc32p, %o1=data_ptr, %o2=len */
	VISEntryHalf
	lda	[%o0] ASI_PL, %f1
1:	ldd	[%o1], %f2
	CRC32C(0,2,0)
	subcc	%o2, 8, %o2
	bne,pt	%icc, 1b
	 add	%o1, 0x8, %o1
	sta	%f1, [%o0] ASI_PL
	VISExitHalf
2:	retl
	 nop
ENDPROC(crc32c_sparc64)
