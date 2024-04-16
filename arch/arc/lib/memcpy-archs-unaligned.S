/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * ARCv2 memcpy implementation optimized for unaligned memory access using.
 *
 * Copyright (C) 2019 Synopsys
 * Author: Eugeniy Paltsev <Eugeniy.Paltsev@synopsys.com>
 */

#include <linux/linkage.h>

#ifdef CONFIG_ARC_HAS_LL64
# define LOADX(DST,RX)		ldd.ab	DST, [RX, 8]
# define STOREX(SRC,RX)		std.ab	SRC, [RX, 8]
# define ZOLSHFT		5
# define ZOLAND			0x1F
#else
# define LOADX(DST,RX)		ld.ab	DST, [RX, 4]
# define STOREX(SRC,RX)		st.ab	SRC, [RX, 4]
# define ZOLSHFT		4
# define ZOLAND			0xF
#endif

ENTRY_CFI(memcpy)
	mov	r3, r0		; don;t clobber ret val

	lsr.f	lp_count, r2, ZOLSHFT
	lpnz	@.Lcopy32_64bytes
	;; LOOP START
	LOADX	(r6, r1)
	LOADX	(r8, r1)
	LOADX	(r10, r1)
	LOADX	(r4, r1)
	STOREX	(r6, r3)
	STOREX	(r8, r3)
	STOREX	(r10, r3)
	STOREX	(r4, r3)
.Lcopy32_64bytes:

	and.f	lp_count, r2, ZOLAND ;Last remaining 31 bytes
	lpnz	@.Lcopyremainingbytes
	;; LOOP START
	ldb.ab	r5, [r1, 1]
	stb.ab	r5, [r3, 1]
.Lcopyremainingbytes:

	j	[blink]
END_CFI(memcpy)
