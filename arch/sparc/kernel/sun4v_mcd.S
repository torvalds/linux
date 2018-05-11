/* sun4v_mcd.S: Sun4v memory corruption detected precise exception handler
 *
 * Copyright (c) 2015 Oracle and/or its affiliates. All rights reserved.
 * Authors: Bob Picco <bob.picco@oracle.com>,
 *          Khalid Aziz <khalid.aziz@oracle.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 */
	.text
	.align 32

sun4v_mcd_detect_precise:
	mov	%l4, %o1
	mov 	%l5, %o2
	call	sun4v_mem_corrupt_detect_precise
	 add	%sp, PTREGS_OFF, %o0
	ba,a,pt	%xcc, rtrap
	 nop
