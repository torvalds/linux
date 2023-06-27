/*
 *  arch/xtensa/lib/strnlen_user.S
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file "COPYING" in the main directory of
 *  this archive for more details.
 *
 *  Returns strnlen, including trailing zero terminator.
 *  Zero indicates error.
 *
 *  Copyright (C) 2002 Tensilica Inc.
 */

#include <linux/linkage.h>
#include <asm/asmmacro.h>
#include <asm/core.h>

/*
 * size_t __strnlen_user(const char *s, size_t len)
 */

#ifdef __XTENSA_EB__
# define MASK0 0xff000000
# define MASK1 0x00ff0000
# define MASK2 0x0000ff00
# define MASK3 0x000000ff
#else
# define MASK0 0x000000ff
# define MASK1 0x0000ff00
# define MASK2 0x00ff0000
# define MASK3 0xff000000
#endif

# Register use:
#   a2/ src
#   a3/ len
#   a4/ tmp
#   a5/ mask0
#   a6/ mask1
#   a7/ mask2
#   a8/ mask3
#   a9/ tmp
#   a10/ tmp

.text
ENTRY(__strnlen_user)

	abi_entry_default
	# a2/ s, a3/ len
	addi	a4, a2, -4	# because we overincrement at the end;
				# we compensate with load offsets of 4
	movi	a5, MASK0	# mask for byte 0
	movi	a6, MASK1	# mask for byte 1
	movi	a7, MASK2	# mask for byte 2
	movi	a8, MASK3	# mask for byte 3
	bbsi.l	a2, 0, .L1mod2	# if only  8-bit aligned
	bbsi.l	a2, 1, .L2mod4	# if only 16-bit aligned

/*
 * String is word-aligned.
 */
.Laligned:
	srli	a10, a3, 2	# number of loop iterations with 4B per loop
#if XCHAL_HAVE_LOOPS
	loopnez	a10, .Ldone
#else
	beqz	a10, .Ldone
	slli	a10, a10, 2
	add	a10, a10, a4	# a10 = end of last 4B chunk
#endif /* XCHAL_HAVE_LOOPS */
.Loop:
EX(10f)	l32i	a9, a4, 4		# get next word of string
	addi	a4, a4, 4		# advance string pointer
	bnone	a9, a5, .Lz0		# if byte 0 is zero
	bnone	a9, a6, .Lz1		# if byte 1 is zero
	bnone	a9, a7, .Lz2		# if byte 2 is zero
	bnone	a9, a8, .Lz3		# if byte 3 is zero
#if !XCHAL_HAVE_LOOPS
	blt	a4, a10, .Loop
#endif

.Ldone:
EX(10f)	l32i	a9, a4, 4	# load 4 bytes for remaining checks

	bbci.l	a3, 1, .L100
	# check two more bytes (bytes 0, 1 of word)
	addi	a4, a4, 2	# advance string pointer
	bnone	a9, a5, .Lz0	# if byte 0 is zero
	bnone	a9, a6, .Lz1	# if byte 1 is zero
.L100:
	bbci.l	a3, 0, .L101
	# check one more byte (byte 2 of word)
	# Actually, we don't need to check.  Zero or nonzero, we'll add one.
	# Do not add an extra one for the NULL terminator since we have
	#  exhausted the original len parameter.
	addi	a4, a4, 1	# advance string pointer
.L101:
	sub	a2, a4, a2	# compute length
	abi_ret_default

# NOTE that in several places below, we point to the byte just after
# the zero byte in order to include the NULL terminator in the count.

.Lz3:	# byte 3 is zero
	addi	a4, a4, 3	# point to zero byte
.Lz0:	# byte 0 is zero
	addi	a4, a4, 1	# point just beyond zero byte
	sub	a2, a4, a2	# subtract to get length
	abi_ret_default
.Lz1:	# byte 1 is zero
	addi	a4, a4, 1+1	# point just beyond zero byte
	sub	a2, a4, a2	# subtract to get length
	abi_ret_default
.Lz2:	# byte 2 is zero
	addi	a4, a4, 2+1	# point just beyond zero byte
	sub	a2, a4, a2	# subtract to get length
	abi_ret_default

.L1mod2:	# address is odd
EX(10f)	l8ui	a9, a4, 4		# get byte 0
	addi	a4, a4, 1		# advance string pointer
	beqz	a9, .Lz3		# if byte 0 is zero
	bbci.l	a4, 1, .Laligned	# if string pointer is now word-aligned

.L2mod4:	# address is 2 mod 4
	addi	a4, a4, 2	# advance ptr for aligned access
EX(10f)	l32i	a9, a4, 0	# get word with first two bytes of string
	bnone	a9, a7, .Lz2	# if byte 2 (of word, not string) is zero
	bany	a9, a8, .Laligned # if byte 3 (of word, not string) is nonzero
	# byte 3 is zero
	addi	a4, a4, 3+1	# point just beyond zero byte
	sub	a2, a4, a2	# subtract to get length
	abi_ret_default

ENDPROC(__strnlen_user)
EXPORT_SYMBOL(__strnlen_user)

	.section .fixup, "ax"
	.align	4
10:
	movi	a2, 0
	abi_ret_default
