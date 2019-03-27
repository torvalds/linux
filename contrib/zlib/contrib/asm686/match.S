/* match.S -- x86 assembly version of the zlib longest_match() function.
 * Optimized for the Intel 686 chips (PPro and later).
 *
 * Copyright (C) 1998, 2007 Brian Raiter <breadbox@muppetlabs.com>
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the author be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#ifndef NO_UNDERLINE
#define	match_init	_match_init
#define	longest_match	_longest_match
#endif

#define	MAX_MATCH	(258)
#define	MIN_MATCH	(3)
#define	MIN_LOOKAHEAD	(MAX_MATCH + MIN_MATCH + 1)
#define	MAX_MATCH_8	((MAX_MATCH + 7) & ~7)

/* stack frame offsets */

#define	chainlenwmask		0	/* high word: current chain len	*/
					/* low word: s->wmask		*/
#define	window			4	/* local copy of s->window	*/
#define	windowbestlen		8	/* s->window + bestlen		*/
#define	scanstart		16	/* first two bytes of string	*/
#define	scanend			12	/* last two bytes of string	*/
#define	scanalign		20	/* dword-misalignment of string	*/
#define	nicematch		24	/* a good enough match size	*/
#define	bestlen			28	/* size of best match so far	*/
#define	scan			32	/* ptr to string wanting match	*/

#define	LocalVarsSize		(36)
/*	saved ebx		36 */
/*	saved edi		40 */
/*	saved esi		44 */
/*	saved ebp		48 */
/*	return address		52 */
#define	deflatestate		56	/* the function arguments	*/
#define	curmatch		60

/* All the +zlib1222add offsets are due to the addition of fields
 *  in zlib in the deflate_state structure since the asm code was first written
 * (if you compile with zlib 1.0.4 or older, use "zlib1222add equ (-4)").
 * (if you compile with zlib between 1.0.5 and 1.2.2.1, use "zlib1222add equ 0").
 * if you compile with zlib 1.2.2.2 or later , use "zlib1222add equ 8").
 */

#define zlib1222add		(8)

#define	dsWSize			(36+zlib1222add)
#define	dsWMask			(44+zlib1222add)
#define	dsWindow		(48+zlib1222add)
#define	dsPrev			(56+zlib1222add)
#define	dsMatchLen		(88+zlib1222add)
#define	dsPrevMatch		(92+zlib1222add)
#define	dsStrStart		(100+zlib1222add)
#define	dsMatchStart		(104+zlib1222add)
#define	dsLookahead		(108+zlib1222add)
#define	dsPrevLen		(112+zlib1222add)
#define	dsMaxChainLen		(116+zlib1222add)
#define	dsGoodMatch		(132+zlib1222add)
#define	dsNiceMatch		(136+zlib1222add)


.file "match.S"

.globl	match_init, longest_match

.text

/* uInt longest_match(deflate_state *deflatestate, IPos curmatch) */
.cfi_sections	.debug_frame

longest_match:

.cfi_startproc
/* Save registers that the compiler may be using, and adjust %esp to	*/
/* make room for our stack frame.					*/

		pushl	%ebp
		.cfi_def_cfa_offset 8
		.cfi_offset ebp, -8
		pushl	%edi
		.cfi_def_cfa_offset 12
		pushl	%esi
		.cfi_def_cfa_offset 16
		pushl	%ebx
		.cfi_def_cfa_offset 20
		subl	$LocalVarsSize, %esp
		.cfi_def_cfa_offset LocalVarsSize+20

/* Retrieve the function arguments. %ecx will hold cur_match		*/
/* throughout the entire function. %edx will hold the pointer to the	*/
/* deflate_state structure during the function's setup (before		*/
/* entering the main loop).						*/

		movl	deflatestate(%esp), %edx
		movl	curmatch(%esp), %ecx

/* uInt wmask = s->w_mask;						*/
/* unsigned chain_length = s->max_chain_length;				*/
/* if (s->prev_length >= s->good_match) {				*/
/*     chain_length >>= 2;						*/
/* }									*/
 
		movl	dsPrevLen(%edx), %eax
		movl	dsGoodMatch(%edx), %ebx
		cmpl	%ebx, %eax
		movl	dsWMask(%edx), %eax
		movl	dsMaxChainLen(%edx), %ebx
		jl	LastMatchGood
		shrl	$2, %ebx
LastMatchGood:

/* chainlen is decremented once beforehand so that the function can	*/
/* use the sign flag instead of the zero flag for the exit test.	*/
/* It is then shifted into the high word, to make room for the wmask	*/
/* value, which it will always accompany.				*/

		decl	%ebx
		shll	$16, %ebx
		orl	%eax, %ebx
		movl	%ebx, chainlenwmask(%esp)

/* if ((uInt)nice_match > s->lookahead) nice_match = s->lookahead;	*/

		movl	dsNiceMatch(%edx), %eax
		movl	dsLookahead(%edx), %ebx
		cmpl	%eax, %ebx
		jl	LookaheadLess
		movl	%eax, %ebx
LookaheadLess:	movl	%ebx, nicematch(%esp)

/* register Bytef *scan = s->window + s->strstart;			*/

		movl	dsWindow(%edx), %esi
		movl	%esi, window(%esp)
		movl	dsStrStart(%edx), %ebp
		lea	(%esi,%ebp), %edi
		movl	%edi, scan(%esp)

/* Determine how many bytes the scan ptr is off from being		*/
/* dword-aligned.							*/

		movl	%edi, %eax
		negl	%eax
		andl	$3, %eax
		movl	%eax, scanalign(%esp)

/* IPos limit = s->strstart > (IPos)MAX_DIST(s) ?			*/
/*     s->strstart - (IPos)MAX_DIST(s) : NIL;				*/

		movl	dsWSize(%edx), %eax
		subl	$MIN_LOOKAHEAD, %eax
		subl	%eax, %ebp
		jg	LimitPositive
		xorl	%ebp, %ebp
LimitPositive:

/* int best_len = s->prev_length;					*/

		movl	dsPrevLen(%edx), %eax
		movl	%eax, bestlen(%esp)

/* Store the sum of s->window + best_len in %esi locally, and in %esi.	*/

		addl	%eax, %esi
		movl	%esi, windowbestlen(%esp)

/* register ush scan_start = *(ushf*)scan;				*/
/* register ush scan_end   = *(ushf*)(scan+best_len-1);			*/
/* Posf *prev = s->prev;						*/

		movzwl	(%edi), %ebx
		movl	%ebx, scanstart(%esp)
		movzwl	-1(%edi,%eax), %ebx
		movl	%ebx, scanend(%esp)
		movl	dsPrev(%edx), %edi

/* Jump into the main loop.						*/

		movl	chainlenwmask(%esp), %edx
		jmp	LoopEntry

.balign 16

/* do {
 *     match = s->window + cur_match;
 *     if (*(ushf*)(match+best_len-1) != scan_end ||
 *         *(ushf*)match != scan_start) continue;
 *     [...]
 * } while ((cur_match = prev[cur_match & wmask]) > limit
 *          && --chain_length != 0);
 *
 * Here is the inner loop of the function. The function will spend the
 * majority of its time in this loop, and majority of that time will
 * be spent in the first ten instructions.
 *
 * Within this loop:
 * %ebx = scanend
 * %ecx = curmatch
 * %edx = chainlenwmask - i.e., ((chainlen << 16) | wmask)
 * %esi = windowbestlen - i.e., (window + bestlen)
 * %edi = prev
 * %ebp = limit
 */
LookupLoop:
		andl	%edx, %ecx
		movzwl	(%edi,%ecx,2), %ecx
		cmpl	%ebp, %ecx
		jbe	LeaveNow
		subl	$0x00010000, %edx
		js	LeaveNow
LoopEntry:	movzwl	-1(%esi,%ecx), %eax
		cmpl	%ebx, %eax
		jnz	LookupLoop
		movl	window(%esp), %eax
		movzwl	(%eax,%ecx), %eax
		cmpl	scanstart(%esp), %eax
		jnz	LookupLoop

/* Store the current value of chainlen.					*/

		movl	%edx, chainlenwmask(%esp)

/* Point %edi to the string under scrutiny, and %esi to the string we	*/
/* are hoping to match it up with. In actuality, %esi and %edi are	*/
/* both pointed (MAX_MATCH_8 - scanalign) bytes ahead, and %edx is	*/
/* initialized to -(MAX_MATCH_8 - scanalign).				*/

		movl	window(%esp), %esi
		movl	scan(%esp), %edi
		addl	%ecx, %esi
		movl	scanalign(%esp), %eax
		movl	$(-MAX_MATCH_8), %edx
		lea	MAX_MATCH_8(%edi,%eax), %edi
		lea	MAX_MATCH_8(%esi,%eax), %esi

/* Test the strings for equality, 8 bytes at a time. At the end,
 * adjust %edx so that it is offset to the exact byte that mismatched.
 *
 * We already know at this point that the first three bytes of the
 * strings match each other, and they can be safely passed over before
 * starting the compare loop. So what this code does is skip over 0-3
 * bytes, as much as necessary in order to dword-align the %edi
 * pointer. (%esi will still be misaligned three times out of four.)
 *
 * It should be confessed that this loop usually does not represent
 * much of the total running time. Replacing it with a more
 * straightforward "rep cmpsb" would not drastically degrade
 * performance.
 */
LoopCmps:
		movl	(%esi,%edx), %eax
		xorl	(%edi,%edx), %eax
		jnz	LeaveLoopCmps
		movl	4(%esi,%edx), %eax
		xorl	4(%edi,%edx), %eax
		jnz	LeaveLoopCmps4
		addl	$8, %edx
		jnz	LoopCmps
		jmp	LenMaximum
LeaveLoopCmps4:	addl	$4, %edx
LeaveLoopCmps:	testl	$0x0000FFFF, %eax
		jnz	LenLower
		addl	$2, %edx
		shrl	$16, %eax
LenLower:	subb	$1, %al
		adcl	$0, %edx

/* Calculate the length of the match. If it is longer than MAX_MATCH,	*/
/* then automatically accept it as the best possible match and leave.	*/

		lea	(%edi,%edx), %eax
		movl	scan(%esp), %edi
		subl	%edi, %eax
		cmpl	$MAX_MATCH, %eax
		jge	LenMaximum

/* If the length of the match is not longer than the best match we	*/
/* have so far, then forget it and return to the lookup loop.		*/

		movl	deflatestate(%esp), %edx
		movl	bestlen(%esp), %ebx
		cmpl	%ebx, %eax
		jg	LongerMatch
		movl	windowbestlen(%esp), %esi
		movl	dsPrev(%edx), %edi
		movl	scanend(%esp), %ebx
		movl	chainlenwmask(%esp), %edx
		jmp	LookupLoop

/*         s->match_start = cur_match;					*/
/*         best_len = len;						*/
/*         if (len >= nice_match) break;				*/
/*         scan_end = *(ushf*)(scan+best_len-1);			*/

LongerMatch:	movl	nicematch(%esp), %ebx
		movl	%eax, bestlen(%esp)
		movl	%ecx, dsMatchStart(%edx)
		cmpl	%ebx, %eax
		jge	LeaveNow
		movl	window(%esp), %esi
		addl	%eax, %esi
		movl	%esi, windowbestlen(%esp)
		movzwl	-1(%edi,%eax), %ebx
		movl	dsPrev(%edx), %edi
		movl	%ebx, scanend(%esp)
		movl	chainlenwmask(%esp), %edx
		jmp	LookupLoop

/* Accept the current string, with the maximum possible length.		*/

LenMaximum:	movl	deflatestate(%esp), %edx
		movl	$MAX_MATCH, bestlen(%esp)
		movl	%ecx, dsMatchStart(%edx)

/* if ((uInt)best_len <= s->lookahead) return (uInt)best_len;		*/
/* return s->lookahead;							*/

LeaveNow:
		movl	deflatestate(%esp), %edx
		movl	bestlen(%esp), %ebx
		movl	dsLookahead(%edx), %eax
		cmpl	%eax, %ebx
		jg	LookaheadRet
		movl	%ebx, %eax
LookaheadRet:

/* Restore the stack and return from whence we came.			*/

		addl	$LocalVarsSize, %esp
		.cfi_def_cfa_offset 20
		popl	%ebx
		.cfi_def_cfa_offset 16
		popl	%esi
		.cfi_def_cfa_offset 12
		popl	%edi
		.cfi_def_cfa_offset 8
		popl	%ebp
		.cfi_def_cfa_offset 4
.cfi_endproc
match_init:	ret
