/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2004-2007 Silicon Graphics, Inc.  All Rights Reserved.
 */


/*
 * The xp_nofault_PIOR function takes a pointer to a remote PIO register
 * and attempts to load and consume a value from it.  This function
 * will be registered as a nofault code block.  In the event that the
 * PIO read fails, the MCA handler will force the error to look
 * corrected and vector to the xp_error_PIOR which will return an error.
 *
 * The definition of "consumption" and the time it takes for an MCA
 * to surface is processor implementation specific.  This code
 * is sufficient on Itanium through the Montvale processor family.
 * It may need to be adjusted for future processor implementations.
 *
 *	extern int xp_nofault_PIOR(void *remote_register);
 */

	.global xp_nofault_PIOR
xp_nofault_PIOR:
	mov	r8=r0			// Stage a success return value
	ld8.acq	r9=[r32];;		// PIO Read the specified register
	adds	r9=1,r9;;		// Add to force consumption
	srlz.i;;			// Allow time for MCA to surface
	br.ret.sptk.many b0;;		// Return success

	.global xp_error_PIOR
xp_error_PIOR:
	mov	r8=1			// Return value of 1
	br.ret.sptk.many b0;;		// Return failure
