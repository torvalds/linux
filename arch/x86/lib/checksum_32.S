/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IP/TCP/UDP checksumming routines
 *
 * Authors:	Jorge Cwik, <jorge@laser.satlink.net>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Tom May, <ftom@netcom.com>
 *              Pentium Pro/II routines:
 *              Alexander Kjeldaas <astor@guardian.no>
 *              Finn Arne Gangstad <finnag@guardian.no>
 *		Lots of code moved from tcp.c and ip.c; see those files
 *		for more names.
 *
 * Changes:     Ingo Molnar, converted csum_partial_copy() to 2.1 exception
 *			     handling.
 *		Andi Kleen,  add zeroing on error
 *                   converted to pure assembler
 */

#include <linux/linkage.h>
#include <asm/errno.h>
#include <asm/asm.h>
#include <asm/export.h>
#include <asm/nospec-branch.h>

/*
 * computes a partial checksum, e.g. for TCP/UDP fragments
 */

/*	
unsigned int csum_partial(const unsigned char * buff, int len, unsigned int sum)
 */
		
.text
		
#ifndef CONFIG_X86_USE_PPRO_CHECKSUM

	  /*		
	   * Experiments with Ethernet and SLIP connections show that buff
	   * is aligned on either a 2-byte or 4-byte boundary.  We get at
	   * least a twofold speedup on 486 and Pentium if it is 4-byte aligned.
	   * Fortunately, it is easy to convert 2-byte alignment to 4-byte
	   * alignment for the unrolled loop.
	   */		
SYM_FUNC_START(csum_partial)
	pushl %esi
	pushl %ebx
	movl 20(%esp),%eax	# Function arg: unsigned int sum
	movl 16(%esp),%ecx	# Function arg: int len
	movl 12(%esp),%esi	# Function arg: unsigned char *buff
	testl $3, %esi		# Check alignment.
	jz 2f			# Jump if alignment is ok.
	testl $1, %esi		# Check alignment.
	jz 10f			# Jump if alignment is boundary of 2 bytes.

	# buf is odd
	dec %ecx
	jl 8f
	movzbl (%esi), %ebx
	adcl %ebx, %eax
	roll $8, %eax
	inc %esi
	testl $2, %esi
	jz 2f
10:
	subl $2, %ecx		# Alignment uses up two bytes.
	jae 1f			# Jump if we had at least two bytes.
	addl $2, %ecx		# ecx was < 2.  Deal with it.
	jmp 4f
1:	movw (%esi), %bx
	addl $2, %esi
	addw %bx, %ax
	adcl $0, %eax
2:
	movl %ecx, %edx
	shrl $5, %ecx
	jz 2f
	testl %esi, %esi
1:	movl (%esi), %ebx
	adcl %ebx, %eax
	movl 4(%esi), %ebx
	adcl %ebx, %eax
	movl 8(%esi), %ebx
	adcl %ebx, %eax
	movl 12(%esi), %ebx
	adcl %ebx, %eax
	movl 16(%esi), %ebx
	adcl %ebx, %eax
	movl 20(%esi), %ebx
	adcl %ebx, %eax
	movl 24(%esi), %ebx
	adcl %ebx, %eax
	movl 28(%esi), %ebx
	adcl %ebx, %eax
	lea 32(%esi), %esi
	dec %ecx
	jne 1b
	adcl $0, %eax
2:	movl %edx, %ecx
	andl $0x1c, %edx
	je 4f
	shrl $2, %edx		# This clears CF
3:	adcl (%esi), %eax
	lea 4(%esi), %esi
	dec %edx
	jne 3b
	adcl $0, %eax
4:	andl $3, %ecx
	jz 7f
	cmpl $2, %ecx
	jb 5f
	movw (%esi),%cx
	leal 2(%esi),%esi
	je 6f
	shll $16,%ecx
5:	movb (%esi),%cl
6:	addl %ecx,%eax
	adcl $0, %eax 
7:	
	testb $1, 12(%esp)
	jz 8f
	roll $8, %eax
8:
	popl %ebx
	popl %esi
	ret
SYM_FUNC_END(csum_partial)

#else

/* Version for PentiumII/PPro */

SYM_FUNC_START(csum_partial)
	pushl %esi
	pushl %ebx
	movl 20(%esp),%eax	# Function arg: unsigned int sum
	movl 16(%esp),%ecx	# Function arg: int len
	movl 12(%esp),%esi	# Function arg:	const unsigned char *buf

	testl $3, %esi         
	jnz 25f                 
10:
	movl %ecx, %edx
	movl %ecx, %ebx
	andl $0x7c, %ebx
	shrl $7, %ecx
	addl %ebx,%esi
	shrl $2, %ebx  
	negl %ebx
	lea 45f(%ebx,%ebx,2), %ebx
	testl %esi, %esi
	JMP_NOSPEC %ebx

	# Handle 2-byte-aligned regions
20:	addw (%esi), %ax
	lea 2(%esi), %esi
	adcl $0, %eax
	jmp 10b
25:
	testl $1, %esi         
	jz 30f                 
	# buf is odd
	dec %ecx
	jl 90f
	movzbl (%esi), %ebx
	addl %ebx, %eax
	adcl $0, %eax
	roll $8, %eax
	inc %esi
	testl $2, %esi
	jz 10b

30:	subl $2, %ecx          
	ja 20b                 
	je 32f
	addl $2, %ecx
	jz 80f
	movzbl (%esi),%ebx	# csumming 1 byte, 2-aligned
	addl %ebx, %eax
	adcl $0, %eax
	jmp 80f
32:
	addw (%esi), %ax	# csumming 2 bytes, 2-aligned
	adcl $0, %eax
	jmp 80f

40: 
	addl -128(%esi), %eax
	adcl -124(%esi), %eax
	adcl -120(%esi), %eax
	adcl -116(%esi), %eax   
	adcl -112(%esi), %eax   
	adcl -108(%esi), %eax
	adcl -104(%esi), %eax
	adcl -100(%esi), %eax
	adcl -96(%esi), %eax
	adcl -92(%esi), %eax
	adcl -88(%esi), %eax
	adcl -84(%esi), %eax
	adcl -80(%esi), %eax
	adcl -76(%esi), %eax
	adcl -72(%esi), %eax
	adcl -68(%esi), %eax
	adcl -64(%esi), %eax     
	adcl -60(%esi), %eax     
	adcl -56(%esi), %eax     
	adcl -52(%esi), %eax   
	adcl -48(%esi), %eax   
	adcl -44(%esi), %eax
	adcl -40(%esi), %eax
	adcl -36(%esi), %eax
	adcl -32(%esi), %eax
	adcl -28(%esi), %eax
	adcl -24(%esi), %eax
	adcl -20(%esi), %eax
	adcl -16(%esi), %eax
	adcl -12(%esi), %eax
	adcl -8(%esi), %eax
	adcl -4(%esi), %eax
45:
	lea 128(%esi), %esi
	adcl $0, %eax
	dec %ecx
	jge 40b
	movl %edx, %ecx
50:	andl $3, %ecx
	jz 80f

	# Handle the last 1-3 bytes without jumping
	notl %ecx		# 1->2, 2->1, 3->0, higher bits are masked
	movl $0xffffff,%ebx	# by the shll and shrl instructions
	shll $3,%ecx
	shrl %cl,%ebx
	andl -128(%esi),%ebx	# esi is 4-aligned so should be ok
	addl %ebx,%eax
	adcl $0,%eax
80: 
	testb $1, 12(%esp)
	jz 90f
	roll $8, %eax
90: 
	popl %ebx
	popl %esi
	ret
SYM_FUNC_END(csum_partial)
				
#endif
EXPORT_SYMBOL(csum_partial)

/*
unsigned int csum_partial_copy_generic (const char *src, char *dst,
				  int len, int sum, int *src_err_ptr, int *dst_err_ptr)
 */ 

/*
 * Copy from ds while checksumming, otherwise like csum_partial
 *
 * The macros SRC and DST specify the type of access for the instruction.
 * thus we can call a custom exception handler for all access types.
 *
 * FIXME: could someone double-check whether I haven't mixed up some SRC and
 *	  DST definitions? It's damn hard to trigger all cases.  I hope I got
 *	  them all but there's no guarantee.
 */

#define SRC(y...)			\
	9999: y;			\
	_ASM_EXTABLE_UA(9999b, 6001f)

#define DST(y...)			\
	9999: y;			\
	_ASM_EXTABLE_UA(9999b, 6002f)

#ifndef CONFIG_X86_USE_PPRO_CHECKSUM

#define ARGBASE 16		
#define FP		12
		
SYM_FUNC_START(csum_partial_copy_generic)
	subl  $4,%esp	
	pushl %edi
	pushl %esi
	pushl %ebx
	movl ARGBASE+16(%esp),%eax	# sum
	movl ARGBASE+12(%esp),%ecx	# len
	movl ARGBASE+4(%esp),%esi	# src
	movl ARGBASE+8(%esp),%edi	# dst

	testl $2, %edi			# Check alignment. 
	jz 2f				# Jump if alignment is ok.
	subl $2, %ecx			# Alignment uses up two bytes.
	jae 1f				# Jump if we had at least two bytes.
	addl $2, %ecx			# ecx was < 2.  Deal with it.
	jmp 4f
SRC(1:	movw (%esi), %bx	)
	addl $2, %esi
DST(	movw %bx, (%edi)	)
	addl $2, %edi
	addw %bx, %ax	
	adcl $0, %eax
2:
	movl %ecx, FP(%esp)
	shrl $5, %ecx
	jz 2f
	testl %esi, %esi
SRC(1:	movl (%esi), %ebx	)
SRC(	movl 4(%esi), %edx	)
	adcl %ebx, %eax
DST(	movl %ebx, (%edi)	)
	adcl %edx, %eax
DST(	movl %edx, 4(%edi)	)

SRC(	movl 8(%esi), %ebx	)
SRC(	movl 12(%esi), %edx	)
	adcl %ebx, %eax
DST(	movl %ebx, 8(%edi)	)
	adcl %edx, %eax
DST(	movl %edx, 12(%edi)	)

SRC(	movl 16(%esi), %ebx 	)
SRC(	movl 20(%esi), %edx	)
	adcl %ebx, %eax
DST(	movl %ebx, 16(%edi)	)
	adcl %edx, %eax
DST(	movl %edx, 20(%edi)	)

SRC(	movl 24(%esi), %ebx	)
SRC(	movl 28(%esi), %edx	)
	adcl %ebx, %eax
DST(	movl %ebx, 24(%edi)	)
	adcl %edx, %eax
DST(	movl %edx, 28(%edi)	)

	lea 32(%esi), %esi
	lea 32(%edi), %edi
	dec %ecx
	jne 1b
	adcl $0, %eax
2:	movl FP(%esp), %edx
	movl %edx, %ecx
	andl $0x1c, %edx
	je 4f
	shrl $2, %edx			# This clears CF
SRC(3:	movl (%esi), %ebx	)
	adcl %ebx, %eax
DST(	movl %ebx, (%edi)	)
	lea 4(%esi), %esi
	lea 4(%edi), %edi
	dec %edx
	jne 3b
	adcl $0, %eax
4:	andl $3, %ecx
	jz 7f
	cmpl $2, %ecx
	jb 5f
SRC(	movw (%esi), %cx	)
	leal 2(%esi), %esi
DST(	movw %cx, (%edi)	)
	leal 2(%edi), %edi
	je 6f
	shll $16,%ecx
SRC(5:	movb (%esi), %cl	)
DST(	movb %cl, (%edi)	)
6:	addl %ecx, %eax
	adcl $0, %eax
7:
5000:

# Exception handler:
.section .fixup, "ax"							

6001:
	movl ARGBASE+20(%esp), %ebx	# src_err_ptr
	movl $-EFAULT, (%ebx)

	# zero the complete destination - computing the rest
	# is too much work 
	movl ARGBASE+8(%esp), %edi	# dst
	movl ARGBASE+12(%esp), %ecx	# len
	xorl %eax,%eax
	rep ; stosb

	jmp 5000b

6002:
	movl ARGBASE+24(%esp), %ebx	# dst_err_ptr
	movl $-EFAULT,(%ebx)
	jmp 5000b

.previous

	popl %ebx
	popl %esi
	popl %edi
	popl %ecx			# equivalent to addl $4,%esp
	ret	
SYM_FUNC_END(csum_partial_copy_generic)

#else

/* Version for PentiumII/PPro */

#define ROUND1(x) \
	SRC(movl x(%esi), %ebx	)	;	\
	addl %ebx, %eax			;	\
	DST(movl %ebx, x(%edi)	)	; 

#define ROUND(x) \
	SRC(movl x(%esi), %ebx	)	;	\
	adcl %ebx, %eax			;	\
	DST(movl %ebx, x(%edi)	)	;

#define ARGBASE 12
		
SYM_FUNC_START(csum_partial_copy_generic)
	pushl %ebx
	pushl %edi
	pushl %esi
	movl ARGBASE+4(%esp),%esi	#src
	movl ARGBASE+8(%esp),%edi	#dst	
	movl ARGBASE+12(%esp),%ecx	#len
	movl ARGBASE+16(%esp),%eax	#sum
#	movl %ecx, %edx  
	movl %ecx, %ebx  
	movl %esi, %edx
	shrl $6, %ecx     
	andl $0x3c, %ebx  
	negl %ebx
	subl %ebx, %esi  
	subl %ebx, %edi  
	lea  -1(%esi),%edx
	andl $-32,%edx
	lea 3f(%ebx,%ebx), %ebx
	testl %esi, %esi 
	JMP_NOSPEC %ebx
1:	addl $64,%esi
	addl $64,%edi 
	SRC(movb -32(%edx),%bl)	; SRC(movb (%edx),%bl)
	ROUND1(-64) ROUND(-60) ROUND(-56) ROUND(-52)	
	ROUND (-48) ROUND(-44) ROUND(-40) ROUND(-36)	
	ROUND (-32) ROUND(-28) ROUND(-24) ROUND(-20)	
	ROUND (-16) ROUND(-12) ROUND(-8)  ROUND(-4)	
3:	adcl $0,%eax
	addl $64, %edx
	dec %ecx
	jge 1b
4:	movl ARGBASE+12(%esp),%edx	#len
	andl $3, %edx
	jz 7f
	cmpl $2, %edx
	jb 5f
SRC(	movw (%esi), %dx         )
	leal 2(%esi), %esi
DST(	movw %dx, (%edi)         )
	leal 2(%edi), %edi
	je 6f
	shll $16,%edx
5:
SRC(	movb (%esi), %dl         )
DST(	movb %dl, (%edi)         )
6:	addl %edx, %eax
	adcl $0, %eax
7:
.section .fixup, "ax"
6001:	movl	ARGBASE+20(%esp), %ebx	# src_err_ptr	
	movl $-EFAULT, (%ebx)
	# zero the complete destination (computing the rest is too much work)
	movl ARGBASE+8(%esp),%edi	# dst
	movl ARGBASE+12(%esp),%ecx	# len
	xorl %eax,%eax
	rep; stosb
	jmp 7b
6002:	movl ARGBASE+24(%esp), %ebx	# dst_err_ptr
	movl $-EFAULT, (%ebx)
	jmp  7b			
.previous				

	popl %esi
	popl %edi
	popl %ebx
	ret
SYM_FUNC_END(csum_partial_copy_generic)
				
#undef ROUND
#undef ROUND1		
		
#endif
EXPORT_SYMBOL(csum_partial_copy_generic)
