|~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
|MOTOROLA MICROPROCESSOR & MEMORY TECHNOLOGY GROUP
|M68000 Hi-Performance Microprocessor Division
|M68060 Software Package
|Production Release P1.00 -- October 10, 1994
|
|M68060 Software Package Copyright © 1993, 1994 Motorola Inc.  All rights reserved.
|
|THE SOFTWARE is provided on an "AS IS" basis and without warranty.
|To the maximum extent permitted by applicable law,
|MOTOROLA DISCLAIMS ALL WARRANTIES WHETHER EXPRESS OR IMPLIED,
|INCLUDING IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
|and any warranty against infringement with regard to the SOFTWARE
|(INCLUDING ANY MODIFIED VERSIONS THEREOF) and any accompanying written materials.
|
|To the maximum extent permitted by applicable law,
|IN NO EVENT SHALL MOTOROLA BE LIABLE FOR ANY DAMAGES WHATSOEVER
|(INCLUDING WITHOUT LIMITATION, DAMAGES FOR LOSS OF BUSINESS PROFITS,
|BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION, OR OTHER PECUNIARY LOSS)
|ARISING OF THE USE OR INABILITY TO USE THE SOFTWARE.
|Motorola assumes no responsibility for the maintenance and support of the SOFTWARE.
|
|You are hereby granted a copyright license to use, modify, and distribute the SOFTWARE
|so long as this entire notice is retained without alteration in any modified and/or
|redistributed versions, and that such modified versions are clearly identified as such.
|No licenses are granted by implication, estoppel or otherwise under any patents
|or trademarks of Motorola, Inc.
|~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
| os.s
|
| This file contains:
|	- example "Call-Out"s required by both the ISP and FPSP.
|

#include <linux/linkage.h>

|################################
| EXAMPLE CALL-OUTS		#
|				#
| _060_dmem_write()		#
| _060_dmem_read()		#
| _060_imem_read()		#
| _060_dmem_read_byte()		#
| _060_dmem_read_word()		#
| _060_dmem_read_long()		#
| _060_imem_read_word()		#
| _060_imem_read_long()		#
| _060_dmem_write_byte()	#
| _060_dmem_write_word()	#
| _060_dmem_write_long()	#
|				#
| _060_real_trace()		#
| _060_real_access()		#
|################################

|
| Each IO routine checks to see if the memory write/read is to/from user
| or supervisor application space. The examples below use simple "move"
| instructions for supervisor mode applications and call _copyin()/_copyout()
| for user mode applications.
| When installing the 060SP, the _copyin()/_copyout() equivalents for a
| given operating system should be substituted.
|
| The addresses within the 060SP are guaranteed to be on the stack.
| The result is that Unix processes are allowed to sleep as a consequence
| of a page fault during a _copyout.
|
| Linux/68k: The _060_[id]mem_{read,write}_{byte,word,long} functions
| (i.e. all the known length <= 4) are implemented by single moves
| statements instead of (more expensive) copy{in,out} calls, if
| working in user space

|
| _060_dmem_write():
|
| Writes to data memory while in supervisor mode.
|
| INPUTS:
|	a0 - supervisor source address
|	a1 - user destination address
|	d0 - number of bytes to write
|	0x4(%a6),bit5 - 1 = supervisor mode, 0 = user mode
| OUTPUTS:
|	d1 - 0 = success, !0 = failure
|
	.global		_060_dmem_write
_060_dmem_write:
	subq.l		#1,%d0
	btst		#0x5,0x4(%a6)		| check for supervisor state
	beqs		user_write
super_write:
	move.b		(%a0)+,(%a1)+		| copy 1 byte
	dbra		%d0,super_write		| quit if --ctr < 0
	clr.l		%d1			| return success
	rts
user_write:
	move.b		(%a0)+,%d1		| copy 1 byte
copyoutae:
	movs.b		%d1,(%a1)+
	dbra		%d0,user_write		| quit if --ctr < 0
	clr.l		%d1			| return success
	rts

|
| _060_imem_read(), _060_dmem_read():
|
| Reads from data/instruction memory while in supervisor mode.
|
| INPUTS:
|	a0 - user source address
|	a1 - supervisor destination address
|	d0 - number of bytes to read
|	0x4(%a6),bit5 - 1 = supervisor mode, 0 = user mode
| OUTPUTS:
|	d1 - 0 = success, !0 = failure
|
	.global		_060_imem_read
	.global		_060_dmem_read
_060_imem_read:
_060_dmem_read:
	subq.l		#1,%d0
	btst		#0x5,0x4(%a6)		| check for supervisor state
	beqs		user_read
super_read:
	move.b		(%a0)+,(%a1)+		| copy 1 byte
	dbra		%d0,super_read		| quit if --ctr < 0
	clr.l		%d1			| return success
	rts
user_read:
copyinae:
	movs.b		(%a0)+,%d1
	move.b		%d1,(%a1)+		| copy 1 byte
	dbra		%d0,user_read		| quit if --ctr < 0
	clr.l		%d1			| return success
	rts

|
| _060_dmem_read_byte():
|
| Read a data byte from user memory.
|
| INPUTS:
|	a0 - user source address
|	0x4(%a6),bit5 - 1 = supervisor mode, 0 = user mode
| OUTPUTS:
|	d0 - data byte in d0
|	d1 - 0 = success, !0 = failure
|
	.global		_060_dmem_read_byte
_060_dmem_read_byte:
	clr.l		%d0			| clear whole longword
	clr.l		%d1			| assume success
	btst		#0x5,0x4(%a6)		| check for supervisor state
	bnes		dmrbs			| supervisor
dmrbuae:movs.b		(%a0),%d0		| fetch user byte
	rts
dmrbs:	move.b		(%a0),%d0		| fetch super byte
	rts

|
| _060_dmem_read_word():
|
| Read a data word from user memory.
|
| INPUTS:
|	a0 - user source address
|	0x4(%a6),bit5 - 1 = supervisor mode, 0 = user mode
| OUTPUTS:
|	d0 - data word in d0
|	d1 - 0 = success, !0 = failure
|
| _060_imem_read_word():
|
| Read an instruction word from user memory.
|
| INPUTS:
|	a0 - user source address
|	0x4(%a6),bit5 - 1 = supervisor mode, 0 = user mode
| OUTPUTS:
|	d0 - instruction word in d0
|	d1 - 0 = success, !0 = failure
|
	.global		_060_dmem_read_word
	.global		_060_imem_read_word
_060_dmem_read_word:
_060_imem_read_word:
	clr.l		%d1			| assume success
	clr.l		%d0			| clear whole longword
	btst		#0x5,0x4(%a6)		| check for supervisor state
	bnes		dmrws			| supervisor
dmrwuae:movs.w		(%a0), %d0		| fetch user word
	rts
dmrws:	move.w		(%a0), %d0		| fetch super word
	rts

|
| _060_dmem_read_long():
|

|
| INPUTS:
|	a0 - user source address
|	0x4(%a6),bit5 - 1 = supervisor mode, 0 = user mode
| OUTPUTS:
|	d0 - data longword in d0
|	d1 - 0 = success, !0 = failure
|
| _060_imem_read_long():
|
| Read an instruction longword from user memory.
|
| INPUTS:
|	a0 - user source address
|	0x4(%a6),bit5 - 1 = supervisor mode, 0 = user mode
| OUTPUTS:
|	d0 - instruction longword in d0
|	d1 - 0 = success, !0 = failure
|
	.global		_060_dmem_read_long
	.global		_060_imem_read_long
_060_dmem_read_long:
_060_imem_read_long:
	clr.l		%d1			| assume success
	btst		#0x5,0x4(%a6)		| check for supervisor state
	bnes		dmrls			| supervisor
dmrluae:movs.l		(%a0),%d0		| fetch user longword
	rts
dmrls:	move.l		(%a0),%d0		| fetch super longword
	rts

|
| _060_dmem_write_byte():
|
| Write a data byte to user memory.
|
| INPUTS:
|	a0 - user destination address
|	d0 - data byte in d0
|	0x4(%a6),bit5 - 1 = supervisor mode, 0 = user mode
| OUTPUTS:
|	d1 - 0 = success, !0 = failure
|
	.global		_060_dmem_write_byte
_060_dmem_write_byte:
	clr.l		%d1			| assume success
	btst		#0x5,0x4(%a6)		| check for supervisor state
	bnes		dmwbs			| supervisor
dmwbuae:movs.b		%d0,(%a0)		| store user byte
	rts
dmwbs:	move.b		%d0,(%a0)		| store super byte
	rts

|
| _060_dmem_write_word():
|
| Write a data word to user memory.
|
| INPUTS:
|	a0 - user destination address
|	d0 - data word in d0
|	0x4(%a6),bit5 - 1 = supervisor mode, 0 = user mode
| OUTPUTS:
|	d1 - 0 = success, !0 = failure
|
	.global		_060_dmem_write_word
_060_dmem_write_word:
	clr.l		%d1			| assume success
	btst		#0x5,0x4(%a6)		| check for supervisor state
	bnes		dmwws			| supervisor
dmwwu:
dmwwuae:movs.w		%d0,(%a0)		| store user word
	bras		dmwwr
dmwws:	move.w		%d0,(%a0)		| store super word
dmwwr:	clr.l		%d1			| return success
	rts

|
| _060_dmem_write_long():
|
| Write a data longword to user memory.
|
| INPUTS:
|	a0 - user destination address
|	d0 - data longword in d0
|	0x4(%a6),bit5 - 1 = supervisor mode, 0 = user mode
| OUTPUTS:
|	d1 - 0 = success, !0 = failure
|
	.global		_060_dmem_write_long
_060_dmem_write_long:
	clr.l		%d1			| assume success
	btst		#0x5,0x4(%a6)		| check for supervisor state
	bnes		dmwls			| supervisor
dmwluae:movs.l		%d0,(%a0)		| store user longword
	rts
dmwls:	move.l		%d0,(%a0)		| store super longword
	rts


#if 0
|###############################################

|
| Use these routines if your kernel doesn't have _copyout/_copyin equivalents.
| Assumes that D0/D1/A0/A1 are scratch registers. The _copyin/_copyout
| below assume that the SFC/DFC have been set previously.
|
| Linux/68k: These are basically non-inlined versions of
| memcpy_{to,from}fs, but without long-transfer optimization
| Note: Assumed that SFC/DFC are pointing correctly to user data
| space... Should be right, or are there any exceptions?

|
| int _copyout(supervisor_addr, user_addr, nbytes)
|
	.global		_copyout
_copyout:
	move.l		4(%sp),%a0		| source
	move.l		8(%sp),%a1		| destination
	move.l		12(%sp),%d0		| count
	subq.l		#1,%d0
moreout:
	move.b		(%a0)+,%d1		| fetch supervisor byte
copyoutae:
	movs.b		%d1,(%a1)+		| store user byte
	dbra		%d0,moreout		| are we through yet?
	moveq		#0,%d0			| return success
	rts

|
| int _copyin(user_addr, supervisor_addr, nbytes)
|
	.global		_copyin
_copyin:
	move.l		4(%sp),%a0		| source
	move.l		8(%sp),%a1		| destination
	move.l		12(%sp),%d0		| count
    subq.l      #1,%d0
morein:
copyinae:
	movs.b		(%a0)+,%d1		| fetch user byte
	move.b		%d1,(%a1)+		| write supervisor byte
	dbra		%d0,morein		| are we through yet?
	moveq		#0,%d0			| return success
	rts
#endif

|###########################################################################

|
| _060_real_trace():
|
| This is the exit point for the 060FPSP when an instruction is being traced
| and there are no other higher priority exceptions pending for this instruction
| or they have already been processed.
|
| The sample code below simply executes an "rte".
|
	.global		_060_real_trace
_060_real_trace:
	bral	trap

|
| _060_real_access():
|
| This is the exit point for the 060FPSP when an access error exception
| is encountered. The routine below should point to the operating system
| handler for access error exceptions. The exception stack frame is an
| 8-word access error frame.
|
| The sample routine below simply executes an "rte" instruction which
| is most likely the incorrect thing to do and could put the system
| into an infinite loop.
|
	.global		_060_real_access
_060_real_access:
	bral	buserr



| Execption handling for movs access to illegal memory
	.section .fixup,#alloc,#execinstr
	.even
1:	moveq		#-1,%d1
	rts
.section __ex_table,#alloc
	.align 4
	.long	dmrbuae,1b
	.long	dmrwuae,1b
	.long	dmrluae,1b
	.long	dmwbuae,1b
	.long	dmwwuae,1b
	.long	dmwluae,1b
	.long	copyoutae,1b
	.long	copyinae,1b
	.text
