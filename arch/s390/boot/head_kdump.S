/* SPDX-License-Identifier: GPL-2.0 */
/*
 * S390 kdump lowlevel functions (new kernel)
 *
 * Copyright IBM Corp. 2011
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#include <asm/sigp.h>

#define DATAMOVER_ADDR	0x4000
#define COPY_PAGE_ADDR	0x6000

#ifdef CONFIG_CRASH_DUMP

#
# kdump entry (new kernel - not yet relocated)
#
# Note: This code has to be position independent
#

SYM_CODE_START_LOCAL(startup_kdump)
	lhi	%r1,2				# mode 2 = esame (dump)
	sigp	%r1,%r0,SIGP_SET_ARCHITECTURE	# Switch to esame mode
	sam64					# Switch to 64 bit addressing
	basr	%r13,0
.Lbase:
	larl	%r2,.Lbase_addr			# Check, if we have been
	lg	%r2,0(%r2)			# already relocated:
	clgr	%r2,%r13			#
	jne	.Lrelocate			# No : Start data mover
	lghi	%r2,0				# Yes: Start kdump kernel
	brasl	%r14,startup_kdump_relocated

.Lrelocate:
	larl	%r4,startup
	lg	%r2,0x418(%r4)			# Get kdump base
	lg	%r3,0x420(%r4)			# Get kdump size

	larl	%r10,.Lcopy_start		# Source of data mover
	lghi	%r8,DATAMOVER_ADDR		# Target of data mover
	mvc	0(256,%r8),0(%r10)		# Copy data mover code

	agr	%r8,%r2				# Copy data mover to
	mvc	0(256,%r8),0(%r10)		# reserved mem

	lghi	%r14,DATAMOVER_ADDR		# Jump to copied data mover
	basr	%r14,%r14
.Lbase_addr:
	.quad	.Lbase

#
# kdump data mover code (runs at address DATAMOVER_ADDR)
#
# r2: kdump base address
# r3: kdump size
#
.Lcopy_start:
	basr	%r13,0				# Base
0:
	lgr	%r11,%r2			# Save kdump base address
	lgr	%r12,%r2
	agr	%r12,%r3			# Compute kdump end address

	lghi	%r5,0
	lghi	%r10,COPY_PAGE_ADDR		# Load copy page address
1:
	mvc	0(256,%r10),0(%r5)		# Copy old kernel to tmp
	mvc	0(256,%r5),0(%r11)		# Copy new kernel to old
	mvc	0(256,%r11),0(%r10)		# Copy tmp to new
	aghi	%r11,256
	aghi	%r5,256
	clgr	%r11,%r12
	jl	1b

	lg	%r14,.Lstartup_kdump-0b(%r13)
	basr	%r14,%r14			# Start relocated kernel
.Lstartup_kdump:
	.long	0x00000000,0x00000000 + startup_kdump_relocated
.Lcopy_end:

#
# Startup of kdump (relocated new kernel)
#
.align 2
startup_kdump_relocated:
	basr	%r13,0
0:	lpswe	.Lrestart_psw-0b(%r13)		# Start new kernel...
SYM_CODE_END(startup_kdump)
.align	8
.Lrestart_psw:
	.quad	0x0000000080000000,0x0000000000000000 + startup
#else
SYM_CODE_START_LOCAL(startup_kdump)
	larl	%r13,startup_kdump_crash
	lpswe	0(%r13)
SYM_CODE_END(startup_kdump)
.align 8
startup_kdump_crash:
	.quad	0x0002000080000000,0x0000000000000000 + startup_kdump_crash
#endif /* CONFIG_CRASH_DUMP */
