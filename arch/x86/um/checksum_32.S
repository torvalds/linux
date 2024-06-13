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

#include <asm/errno.h>
#include <asm/asm.h>
#include <asm/export.h>
				
/*
 * computes a partial checksum, e.g. for TCP/UDP fragments
 */

/*	
unsigned int csum_partial(const unsigned char * buff, int len, unsigned int sum)
 */
		
.text
.align 4
.globl csum_partial
		
#ifndef CONFIG_X86_USE_PPRO_CHECKSUM

	  /*		
	   * Experiments with Ethernet and SLIP connections show that buff
	   * is aligned on either a 2-byte or 4-byte boundary.  We get at
	   * least a twofold speedup on 486 and Pentium if it is 4-byte aligned.
	   * Fortunately, it is easy to convert 2-byte alignment to 4-byte
	   * alignment for the unrolled loop.
	   */		
csum_partial:
	pushl %esi
	pushl %ebx
	movl 20(%esp),%eax	# Function arg: unsigned int sum
	movl 16(%esp),%ecx	# Function arg: int len
	movl 12(%esp),%esi	# Function arg: unsigned char *buff
	testl $2, %esi		# Check alignment.
	jz 2f			# Jump if alignment is ok.
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
	popl %ebx
	popl %esi
	RET

#else

/* Version for PentiumII/PPro */

csum_partial:
	pushl %esi
	pushl %ebx
	movl 20(%esp),%eax	# Function arg: unsigned int sum
	movl 16(%esp),%ecx	# Function arg: int len
	movl 12(%esp),%esi	# Function arg:	const unsigned char *buf

	testl $2, %esi         
	jnz 30f                 
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
	jmp *%ebx

	# Handle 2-byte-aligned regions
20:	addw (%esi), %ax
	lea 2(%esi), %esi
	adcl $0, %eax
	jmp 10b

30:	subl $2, %ecx          
	ja 20b                 
	je 32f
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
	popl %ebx
	popl %esi
	RET
				
#endif
	EXPORT_SYMBOL(csum_partial)
