/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 - 1999 Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 *
 * Cache error handler
 */
#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>

/*
 * Game over.  Go to the button.  Press gently.	 Swear where allowed by
 * legislation.
 */
	LEAF(except_vec2_generic)
	.set	noreorder
	.set	noat
	.set	mips0
	/*
	 * This is a very bad place to be.  Our cache error
	 * detection has triggered.  If we have write-back data
	 * in the cache, we may not be able to recover.	 As a
	 * first-order desperate measure, turn off KSEG0 cacheing.
	 */
	mfc0	k0,CP0_CONFIG
	li	k1,~CONF_CM_CMASK
	and	k0,k0,k1
	ori	k0,k0,CONF_CM_UNCACHED
	mtc0	k0,CP0_CONFIG
	/* Give it a few cycles to sink in... */
	nop
	nop
	nop

	j	cache_parity_error
	nop
	END(except_vec2_generic)
