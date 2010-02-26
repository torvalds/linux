/*
 * Mini SCLP driver.
 *
 * Copyright IBM Corp. 2004,2009
 *
 *   Author(s):	Peter Oberparleiter <Peter.Oberparleiter@de.ibm.com>,
 *		Heiko Carstens <heiko.carstens@de.ibm.com>,
 *
 */

LC_EXT_NEW_PSW		= 0x58			# addr of ext int handler
LC_EXT_NEW_PSW_64	= 0x1b0			# addr of ext int handler 64 bit
LC_EXT_INT_PARAM	= 0x80			# addr of ext int parameter
LC_EXT_INT_CODE		= 0x86			# addr of ext int code
LC_AR_MODE_ID		= 0xa3

#
# Subroutine which waits synchronously until either an external interruption
# or a timeout occurs.
#
# Parameters:
#   R2	= 0 for no timeout, non-zero for timeout in (approximated) seconds
#
# Returns:
#   R2	= 0 on interrupt, 2 on timeout
#   R3	= external interruption parameter if R2=0
#

_sclp_wait_int:
	stm	%r6,%r15,24(%r15)		# save registers
	basr	%r13,0				# get base register
.LbaseS1:
	ahi	%r15,-96			# create stack frame
	la	%r8,LC_EXT_NEW_PSW		# register int handler
	la	%r9,.LextpswS1-.LbaseS1(%r13)
#ifdef CONFIG_64BIT
	tm	LC_AR_MODE_ID,1
	jno	.Lesa1
	la	%r8,LC_EXT_NEW_PSW_64		# register int handler 64 bit
	la	%r9,.LextpswS1_64-.LbaseS1(%r13)
.Lesa1:
#endif
	mvc	.LoldpswS1-.LbaseS1(16,%r13),0(%r8)
	mvc	0(16,%r8),0(%r9)
	lhi	%r6,0x0200			# cr mask for ext int (cr0.54)
	ltr	%r2,%r2
	jz	.LsetctS1
	ahi	%r6,0x0800			# cr mask for clock int (cr0.52)
	stck	.LtimeS1-.LbaseS1(%r13)		# initiate timeout
	al	%r2,.LtimeS1-.LbaseS1(%r13)
	st	%r2,.LtimeS1-.LbaseS1(%r13)
	sckc	.LtimeS1-.LbaseS1(%r13)

.LsetctS1:
	stctl	%c0,%c0,.LctlS1-.LbaseS1(%r13)	# enable required interrupts
	l	%r0,.LctlS1-.LbaseS1(%r13)
	lhi	%r1,~(0x200 | 0x800)		# clear old values
	nr	%r1,%r0
	or	%r1,%r6				# set new value
	st	%r1,.LctlS1-.LbaseS1(%r13)
	lctl	%c0,%c0,.LctlS1-.LbaseS1(%r13)
	st	%r0,.LctlS1-.LbaseS1(%r13)
	lhi	%r2,2				# return code for timeout
.LloopS1:
	lpsw	.LwaitpswS1-.LbaseS1(%r13)	# wait until interrupt
.LwaitS1:
	lh	%r7,LC_EXT_INT_CODE
	chi	%r7,0x1004			# timeout?
	je	.LtimeoutS1
	chi	%r7,0x2401			# service int?
	jne	.LloopS1
	sr	%r2,%r2
	l	%r3,LC_EXT_INT_PARAM
.LtimeoutS1:
	lctl	%c0,%c0,.LctlS1-.LbaseS1(%r13)	# restore interrupt setting
	# restore old handler
	mvc	0(16,%r8),.LoldpswS1-.LbaseS1(%r13)
	lm	%r6,%r15,120(%r15)		# restore registers
	br	%r14				# return to caller

	.align	8
.LoldpswS1:
	.long	0, 0, 0, 0			# old ext int PSW
.LextpswS1:
	.long	0x00080000, 0x80000000+.LwaitS1	# PSW to handle ext int
#ifdef CONFIG_64BIT
.LextpswS1_64:
	.quad	0x0000000180000000, .LwaitS1	# PSW to handle ext int, 64 bit
#endif
.LwaitpswS1:
	.long	0x010a0000, 0x00000000+.LloopS1	# PSW to wait for ext int
.LtimeS1:
	.quad	0				# current time
.LctlS1:
	.long	0				# CT0 contents

#
# Subroutine to synchronously issue a service call.
#
# Parameters:
#   R2	= command word
#   R3	= sccb address
#
# Returns:
#   R2	= 0 on success, 1 on failure
#   R3	= sccb response code if R2 = 0
#

_sclp_servc:
	stm	%r6,%r15,24(%r15)		# save registers
	ahi	%r15,-96			# create stack frame
	lr	%r6,%r2				# save command word
	lr	%r7,%r3				# save sccb address
.LretryS2:
	lhi	%r2,1				# error return code
	.insn	rre,0xb2200000,%r6,%r7		# servc
	brc	1,.LendS2			# exit if not operational
	brc	8,.LnotbusyS2			# go on if not busy
	sr	%r2,%r2				# wait until no longer busy
	bras	%r14,_sclp_wait_int
	j	.LretryS2			# retry
.LnotbusyS2:
	sr	%r2,%r2				# wait until result
	bras	%r14,_sclp_wait_int
	sr	%r2,%r2
	lh	%r3,6(%r7)
.LendS2:
	lm	%r6,%r15,120(%r15)		# restore registers
	br	%r14

#
# Subroutine to set up the SCLP interface.
#
# Parameters:
#   R2	= 0 to activate, non-zero to deactivate
#
# Returns:
#   R2	= 0 on success, non-zero on failure
#

_sclp_setup:
	stm	%r6,%r15,24(%r15)		# save registers
	ahi	%r15,-96			# create stack frame
	basr	%r13,0				# get base register
.LbaseS3:
	l	%r6,.LsccbS0-.LbaseS3(%r13)	# prepare init mask sccb
	mvc	0(.LinitendS3-.LinitsccbS3,%r6),.LinitsccbS3-.LbaseS3(%r13)
	ltr	%r2,%r2				# initialization?
	jz	.LdoinitS3			# go ahead
	# clear masks
	xc	.LinitmaskS3-.LinitsccbS3(8,%r6),.LinitmaskS3-.LinitsccbS3(%r6)
.LdoinitS3:
	l	%r2,.LwritemaskS3-.LbaseS3(%r13)# get command word
	lr	%r3,%r6				# get sccb address
	bras	%r14,_sclp_servc		# issue service call
	ltr	%r2,%r2				# servc successful?
	jnz	.LerrorS3
	chi	%r3,0x20			# write mask successful?
	jne	.LerrorS3
	# check masks
	la	%r2,.LinitmaskS3-.LinitsccbS3(%r6)
	l	%r1,0(%r2)			# receive mask ok?
	n	%r1,12(%r2)
	cl	%r1,0(%r2)
	jne	.LerrorS3
	l	%r1,4(%r2)			# send mask ok?
	n	%r1,8(%r2)
	cl	%r1,4(%r2)
	sr	%r2,%r2
	je	.LendS3
.LerrorS3:
	lhi	%r2,1				# error return code
.LendS3:
	lm	%r6,%r15,120(%r15)		# restore registers
	br	%r14
.LwritemaskS3:
	.long	0x00780005			# SCLP command for write mask
.LinitsccbS3:
	.word	.LinitendS3-.LinitsccbS3
	.byte	0,0,0,0
	.word	0
	.word	0
	.word	4
.LinitmaskS3:
	.long	0x80000000
	.long	0x40000000
	.long	0
	.long	0
.LinitendS3:

#
# Subroutine which prints a given text to the SCLP console.
#
# Parameters:
#   R2	= address of nil-terminated ASCII text
#
# Returns:
#   R2	= 0 on success, 1 on failure
#

_sclp_print:
	stm	%r6,%r15,24(%r15)		# save registers
	ahi	%r15,-96			# create stack frame
	basr	%r13,0				# get base register
.LbaseS4:
	l	%r8,.LsccbS0-.LbaseS4(%r13)	# prepare write data sccb
	mvc	0(.LmtoS4-.LwritesccbS4,%r8),.LwritesccbS4-.LbaseS4(%r13)
	la	%r7,.LmtoS4-.LwritesccbS4(%r8)	# current mto addr
	sr	%r0,%r0
	l	%r10,.Lascebc-.LbaseS4(%r13)	# address of translation table
.LinitmtoS4:
	# initialize mto
	mvc	0(.LmtoendS4-.LmtoS4,%r7),.LmtoS4-.LbaseS4(%r13)
	lhi	%r6,.LmtoendS4-.LmtoS4		# current mto length
.LloopS4:
	ic	%r0,0(%r2)			# get character
	ahi	%r2,1
	ltr	%r0,%r0				# end of string?
	jz	.LfinalizemtoS4
	chi	%r0,0x15			# end of line (NL)?
	jz	.LfinalizemtoS4
	stc	%r0,0(%r6,%r7)			# copy to mto
	la	%r11,0(%r6,%r7)
	tr	0(1,%r11),0(%r10)		# translate to EBCDIC
	ahi	%r6,1
	j	.LloopS4
.LfinalizemtoS4:
	sth	%r6,0(%r7)			# update mto length
	lh	%r9,.LmdbS4-.LwritesccbS4(%r8)	# update mdb length
	ar	%r9,%r6
	sth	%r9,.LmdbS4-.LwritesccbS4(%r8)
	lh	%r9,.LevbufS4-.LwritesccbS4(%r8)# update evbuf length
	ar	%r9,%r6
	sth	%r9,.LevbufS4-.LwritesccbS4(%r8)
	lh	%r9,0(%r8)			# update sccb length
	ar	%r9,%r6
	sth	%r9,0(%r8)
	ar	%r7,%r6				# update current mto adress
	ltr	%r0,%r0				# more characters?
	jnz	.LinitmtoS4
	l	%r2,.LwritedataS4-.LbaseS4(%r13)# write data
	lr	%r3,%r8
	bras	%r14,_sclp_servc
	ltr	%r2,%r2				# servc successful?
	jnz	.LendS4
	chi	%r3,0x20			# write data successful?
	je	.LendS4
	lhi	%r2,1				# error return code
.LendS4:
	lm	%r6,%r15,120(%r15)		# restore registers
	br	%r14

#
# Function which prints a given text to the SCLP console.
#
# Parameters:
#   R2	= address of nil-terminated ASCII text
#
# Returns:
#   R2	= 0 on success, 1 on failure
#

	.globl _sclp_print_early
_sclp_print_early:
	stm	%r6,%r15,24(%r15)		# save registers
	ahi	%r15,-96			# create stack frame
#ifdef CONFIG_64BIT
	tm	LC_AR_MODE_ID,1
	jno	.Lesa2
	ahi	%r15,-80
	stmh	%r6,%r15,96(%r15)		# store upper register halves
.Lesa2:
#endif
	lr	%r10,%r2			# save string pointer
	lhi	%r2,0
	bras	%r14,_sclp_setup		# enable console
	ltr	%r2,%r2
	jnz	.LendS5
	lr	%r2,%r10
	bras	%r14,_sclp_print		# print string
	ltr	%r2,%r2
	jnz	.LendS5
	lhi	%r2,1
	bras	%r14,_sclp_setup		# disable console
.LendS5:
#ifdef CONFIG_64BIT
	tm	LC_AR_MODE_ID,1
	jno	.Lesa3
	lmh	%r6,%r15,96(%r15)		# store upper register halves
	ahi	%r15,80
.Lesa3:
#endif
	lm	%r6,%r15,120(%r15)		# restore registers
	br	%r14

.LwritedataS4:
	.long	0x00760005			# SCLP command for write data
.LwritesccbS4:
	# sccb
	.word	.LmtoS4-.LwritesccbS4
	.byte	0
	.byte	0,0,0
	.word	0

	# evbuf
.LevbufS4:
	.word	.LmtoS4-.LevbufS4
	.byte	0x02
	.byte	0
	.word	0

.LmdbS4:
	# mdb
	.word	.LmtoS4-.LmdbS4
	.word	1
	.long	0xd4c4c240
	.long	1

	# go
.LgoS4:
	.word	.LmtoS4-.LgoS4
	.word	1
	.long	0
	.byte	0,0,0,0,0,0,0,0
	.byte	0,0,0
	.byte	0
	.byte	0,0,0,0,0,0,0
	.byte	0
	.word	0
	.byte	0,0,0,0,0,0,0,0,0,0
	.byte	0,0,0,0,0,0,0,0
	.byte	0,0,0,0,0,0,0,0

.LmtoS4:
	.word	.LmtoendS4-.LmtoS4
	.word	4
	.word	0x1000
	.byte	0
	.byte	0,0,0
.LmtoendS4:

	# Global constants
.LsccbS0:
	.long	_sclp_work_area
.Lascebc:
	.long	_ascebc

.section .data,"aw",@progbits
	.balign 4096
_sclp_work_area:
	.fill	4096
.previous
