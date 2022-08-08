/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Unified implementation of memcpy, memmove and the __copy_user backend.
 *
 * Copyright (C) 1998, 99, 2000, 01, 2002 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999, 2000, 01, 2002 Silicon Graphics, Inc.
 * Copyright (C) 2002 Broadcom, Inc.
 *   memcpy/copy_user author: Mark Vandevoorde
 *
 * Mnemonic names for arguments to memcpy/__copy_user
 */

#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/export.h>
#include <asm/regdef.h>

#define dst a0
#define src a1
#define len a2

/*
 * Spec
 *
 * memcpy copies len bytes from src to dst and sets v0 to dst.
 * It assumes that
 *   - src and dst don't overlap
 *   - src is readable
 *   - dst is writable
 * memcpy uses the standard calling convention
 *
 * __copy_user copies up to len bytes from src to dst and sets a2 (len) to
 * the number of uncopied bytes due to an exception caused by a read or write.
 * __copy_user assumes that src and dst don't overlap, and that the call is
 * implementing one of the following:
 *   copy_to_user
 *     - src is readable  (no exceptions when reading src)
 *   copy_from_user
 *     - dst is writable  (no exceptions when writing dst)
 * __copy_user uses a non-standard calling convention; see
 * arch/mips/include/asm/uaccess.h
 *
 * When an exception happens on a load, the handler must
 # ensure that all of the destination buffer is overwritten to prevent
 * leaking information to user mode programs.
 */

/*
 * Implementation
 */

/*
 * The exception handler for loads requires that:
 *  1- AT contain the address of the byte just past the end of the source
 *     of the copy,
 *  2- src_entry <= src < AT, and
 *  3- (dst - src) == (dst_entry - src_entry),
 * The _entry suffix denotes values when __copy_user was called.
 *
 * (1) is set up up by uaccess.h and maintained by not writing AT in copy_user
 * (2) is met by incrementing src by the number of bytes copied
 * (3) is met by not doing loads between a pair of increments of dst and src
 *
 * The exception handlers for stores adjust len (if necessary) and return.
 * These handlers do not need to overwrite any data.
 *
 * For __rmemcpy and memmove an exception is always a kernel bug, therefore
 * they're not protected.
 */

#define EXC(inst_reg,addr,handler)		\
9:	inst_reg, addr;				\
	.section __ex_table,"a";		\
	PTR	9b, handler;			\
	.previous

/*
 * Only on the 64-bit kernel we can made use of 64-bit registers.
 */

#define LOAD   ld
#define LOADL  ldl
#define LOADR  ldr
#define STOREL sdl
#define STORER sdr
#define STORE  sd
#define ADD    daddu
#define SUB    dsubu
#define SRL    dsrl
#define SRA    dsra
#define SLL    dsll
#define SLLV   dsllv
#define SRLV   dsrlv
#define NBYTES 8
#define LOG_NBYTES 3

/*
 * As we are sharing code base with the mips32 tree (which use the o32 ABI
 * register definitions). We need to redefine the register definitions from
 * the n64 ABI register naming to the o32 ABI register naming.
 */
#undef t0
#undef t1
#undef t2
#undef t3
#define t0	$8
#define t1	$9
#define t2	$10
#define t3	$11
#define t4	$12
#define t5	$13
#define t6	$14
#define t7	$15

#ifdef CONFIG_CPU_LITTLE_ENDIAN
#define LDFIRST LOADR
#define LDREST	LOADL
#define STFIRST STORER
#define STREST	STOREL
#define SHIFT_DISCARD SLLV
#else
#define LDFIRST LOADL
#define LDREST	LOADR
#define STFIRST STOREL
#define STREST	STORER
#define SHIFT_DISCARD SRLV
#endif

#define FIRST(unit) ((unit)*NBYTES)
#define REST(unit)  (FIRST(unit)+NBYTES-1)
#define UNIT(unit)  FIRST(unit)

#define ADDRMASK (NBYTES-1)

	.text
	.set	noreorder
	.set	noat

/*
 * A combined memcpy/__copy_user
 * __copy_user sets len to 0 for success; else to an upper bound of
 * the number of uncopied bytes.
 * memcpy sets v0 to dst.
 */
	.align	5
LEAF(memcpy)					/* a0=dst a1=src a2=len */
EXPORT_SYMBOL(memcpy)
	move	v0, dst				/* return value */
__memcpy:
FEXPORT(__raw_copy_from_user)
EXPORT_SYMBOL(__raw_copy_from_user)
FEXPORT(__raw_copy_to_user)
EXPORT_SYMBOL(__raw_copy_to_user)
FEXPORT(__raw_copy_in_user)
EXPORT_SYMBOL(__raw_copy_in_user)
	/*
	 * Note: dst & src may be unaligned, len may be 0
	 * Temps
	 */
	#
	# Octeon doesn't care if the destination is unaligned. The hardware
	# can fix it faster than we can special case the assembly.
	#
	pref	0, 0(src)
	sltu	t0, len, NBYTES		# Check if < 1 word
	bnez	t0, copy_bytes_checklen
	 and	t0, src, ADDRMASK	# Check if src unaligned
	bnez	t0, src_unaligned
	 sltu	t0, len, 4*NBYTES	# Check if < 4 words
	bnez	t0, less_than_4units
	 sltu	t0, len, 8*NBYTES	# Check if < 8 words
	bnez	t0, less_than_8units
	 sltu	t0, len, 16*NBYTES	# Check if < 16 words
	bnez	t0, cleanup_both_aligned
	 sltu	t0, len, 128+1		# Check if len < 129
	bnez	t0, 1f			# Skip prefetch if len is too short
	 sltu	t0, len, 256+1		# Check if len < 257
	bnez	t0, 1f			# Skip prefetch if len is too short
	 pref	0, 128(src)		# We must not prefetch invalid addresses
	#
	# This is where we loop if there is more than 128 bytes left
2:	pref	0, 256(src)		# We must not prefetch invalid addresses
	#
	# This is where we loop if we can't prefetch anymore
1:
EXC(	LOAD	t0, UNIT(0)(src),	l_exc)
EXC(	LOAD	t1, UNIT(1)(src),	l_exc_copy)
EXC(	LOAD	t2, UNIT(2)(src),	l_exc_copy)
EXC(	LOAD	t3, UNIT(3)(src),	l_exc_copy)
	SUB	len, len, 16*NBYTES
EXC(	STORE	t0, UNIT(0)(dst),	s_exc_p16u)
EXC(	STORE	t1, UNIT(1)(dst),	s_exc_p15u)
EXC(	STORE	t2, UNIT(2)(dst),	s_exc_p14u)
EXC(	STORE	t3, UNIT(3)(dst),	s_exc_p13u)
EXC(	LOAD	t0, UNIT(4)(src),	l_exc_copy)
EXC(	LOAD	t1, UNIT(5)(src),	l_exc_copy)
EXC(	LOAD	t2, UNIT(6)(src),	l_exc_copy)
EXC(	LOAD	t3, UNIT(7)(src),	l_exc_copy)
EXC(	STORE	t0, UNIT(4)(dst),	s_exc_p12u)
EXC(	STORE	t1, UNIT(5)(dst),	s_exc_p11u)
EXC(	STORE	t2, UNIT(6)(dst),	s_exc_p10u)
	ADD	src, src, 16*NBYTES
EXC(	STORE	t3, UNIT(7)(dst),	s_exc_p9u)
	ADD	dst, dst, 16*NBYTES
EXC(	LOAD	t0, UNIT(-8)(src),	l_exc_copy_rewind16)
EXC(	LOAD	t1, UNIT(-7)(src),	l_exc_copy_rewind16)
EXC(	LOAD	t2, UNIT(-6)(src),	l_exc_copy_rewind16)
EXC(	LOAD	t3, UNIT(-5)(src),	l_exc_copy_rewind16)
EXC(	STORE	t0, UNIT(-8)(dst),	s_exc_p8u)
EXC(	STORE	t1, UNIT(-7)(dst),	s_exc_p7u)
EXC(	STORE	t2, UNIT(-6)(dst),	s_exc_p6u)
EXC(	STORE	t3, UNIT(-5)(dst),	s_exc_p5u)
EXC(	LOAD	t0, UNIT(-4)(src),	l_exc_copy_rewind16)
EXC(	LOAD	t1, UNIT(-3)(src),	l_exc_copy_rewind16)
EXC(	LOAD	t2, UNIT(-2)(src),	l_exc_copy_rewind16)
EXC(	LOAD	t3, UNIT(-1)(src),	l_exc_copy_rewind16)
EXC(	STORE	t0, UNIT(-4)(dst),	s_exc_p4u)
EXC(	STORE	t1, UNIT(-3)(dst),	s_exc_p3u)
EXC(	STORE	t2, UNIT(-2)(dst),	s_exc_p2u)
EXC(	STORE	t3, UNIT(-1)(dst),	s_exc_p1u)
	sltu	t0, len, 256+1		# See if we can prefetch more
	beqz	t0, 2b
	 sltu	t0, len, 128		# See if we can loop more time
	beqz	t0, 1b
	 nop
	#
	# Jump here if there are less than 16*NBYTES left.
	#
cleanup_both_aligned:
	beqz	len, done
	 sltu	t0, len, 8*NBYTES
	bnez	t0, less_than_8units
	 nop
EXC(	LOAD	t0, UNIT(0)(src),	l_exc)
EXC(	LOAD	t1, UNIT(1)(src),	l_exc_copy)
EXC(	LOAD	t2, UNIT(2)(src),	l_exc_copy)
EXC(	LOAD	t3, UNIT(3)(src),	l_exc_copy)
	SUB	len, len, 8*NBYTES
EXC(	STORE	t0, UNIT(0)(dst),	s_exc_p8u)
EXC(	STORE	t1, UNIT(1)(dst),	s_exc_p7u)
EXC(	STORE	t2, UNIT(2)(dst),	s_exc_p6u)
EXC(	STORE	t3, UNIT(3)(dst),	s_exc_p5u)
EXC(	LOAD	t0, UNIT(4)(src),	l_exc_copy)
EXC(	LOAD	t1, UNIT(5)(src),	l_exc_copy)
EXC(	LOAD	t2, UNIT(6)(src),	l_exc_copy)
EXC(	LOAD	t3, UNIT(7)(src),	l_exc_copy)
EXC(	STORE	t0, UNIT(4)(dst),	s_exc_p4u)
EXC(	STORE	t1, UNIT(5)(dst),	s_exc_p3u)
EXC(	STORE	t2, UNIT(6)(dst),	s_exc_p2u)
EXC(	STORE	t3, UNIT(7)(dst),	s_exc_p1u)
	ADD	src, src, 8*NBYTES
	beqz	len, done
	 ADD	dst, dst, 8*NBYTES
	#
	# Jump here if there are less than 8*NBYTES left.
	#
less_than_8units:
	sltu	t0, len, 4*NBYTES
	bnez	t0, less_than_4units
	 nop
EXC(	LOAD	t0, UNIT(0)(src),	l_exc)
EXC(	LOAD	t1, UNIT(1)(src),	l_exc_copy)
EXC(	LOAD	t2, UNIT(2)(src),	l_exc_copy)
EXC(	LOAD	t3, UNIT(3)(src),	l_exc_copy)
	SUB	len, len, 4*NBYTES
EXC(	STORE	t0, UNIT(0)(dst),	s_exc_p4u)
EXC(	STORE	t1, UNIT(1)(dst),	s_exc_p3u)
EXC(	STORE	t2, UNIT(2)(dst),	s_exc_p2u)
EXC(	STORE	t3, UNIT(3)(dst),	s_exc_p1u)
	ADD	src, src, 4*NBYTES
	beqz	len, done
	 ADD	dst, dst, 4*NBYTES
	#
	# Jump here if there are less than 4*NBYTES left. This means
	# we may need to copy up to 3 NBYTES words.
	#
less_than_4units:
	sltu	t0, len, 1*NBYTES
	bnez	t0, copy_bytes_checklen
	 nop
	#
	# 1) Copy NBYTES, then check length again
	#
EXC(	LOAD	t0, 0(src),		l_exc)
	SUB	len, len, NBYTES
	sltu	t1, len, 8
EXC(	STORE	t0, 0(dst),		s_exc_p1u)
	ADD	src, src, NBYTES
	bnez	t1, copy_bytes_checklen
	 ADD	dst, dst, NBYTES
	#
	# 2) Copy NBYTES, then check length again
	#
EXC(	LOAD	t0, 0(src),		l_exc)
	SUB	len, len, NBYTES
	sltu	t1, len, 8
EXC(	STORE	t0, 0(dst),		s_exc_p1u)
	ADD	src, src, NBYTES
	bnez	t1, copy_bytes_checklen
	 ADD	dst, dst, NBYTES
	#
	# 3) Copy NBYTES, then check length again
	#
EXC(	LOAD	t0, 0(src),		l_exc)
	SUB	len, len, NBYTES
	ADD	src, src, NBYTES
	ADD	dst, dst, NBYTES
	b copy_bytes_checklen
EXC(	 STORE	t0, -8(dst),		s_exc_p1u)

src_unaligned:
#define rem t8
	SRL	t0, len, LOG_NBYTES+2	 # +2 for 4 units/iter
	beqz	t0, cleanup_src_unaligned
	 and	rem, len, (4*NBYTES-1)	 # rem = len % 4*NBYTES
1:
/*
 * Avoid consecutive LD*'s to the same register since some mips
 * implementations can't issue them in the same cycle.
 * It's OK to load FIRST(N+1) before REST(N) because the two addresses
 * are to the same unit (unless src is aligned, but it's not).
 */
EXC(	LDFIRST t0, FIRST(0)(src),	l_exc)
EXC(	LDFIRST t1, FIRST(1)(src),	l_exc_copy)
	SUB	len, len, 4*NBYTES
EXC(	LDREST	t0, REST(0)(src),	l_exc_copy)
EXC(	LDREST	t1, REST(1)(src),	l_exc_copy)
EXC(	LDFIRST t2, FIRST(2)(src),	l_exc_copy)
EXC(	LDFIRST t3, FIRST(3)(src),	l_exc_copy)
EXC(	LDREST	t2, REST(2)(src),	l_exc_copy)
EXC(	LDREST	t3, REST(3)(src),	l_exc_copy)
	ADD	src, src, 4*NBYTES
EXC(	STORE	t0, UNIT(0)(dst),	s_exc_p4u)
EXC(	STORE	t1, UNIT(1)(dst),	s_exc_p3u)
EXC(	STORE	t2, UNIT(2)(dst),	s_exc_p2u)
EXC(	STORE	t3, UNIT(3)(dst),	s_exc_p1u)
	bne	len, rem, 1b
	 ADD	dst, dst, 4*NBYTES

cleanup_src_unaligned:
	beqz	len, done
	 and	rem, len, NBYTES-1  # rem = len % NBYTES
	beq	rem, len, copy_bytes
	 nop
1:
EXC(	LDFIRST t0, FIRST(0)(src),	l_exc)
EXC(	LDREST	t0, REST(0)(src),	l_exc_copy)
	SUB	len, len, NBYTES
EXC(	STORE	t0, 0(dst),		s_exc_p1u)
	ADD	src, src, NBYTES
	bne	len, rem, 1b
	 ADD	dst, dst, NBYTES

copy_bytes_checklen:
	beqz	len, done
	 nop
copy_bytes:
	/* 0 < len < NBYTES  */
#define COPY_BYTE(N)			\
EXC(	lb	t0, N(src), l_exc);	\
	SUB	len, len, 1;		\
	beqz	len, done;		\
EXC(	 sb	t0, N(dst), s_exc_p1)

	COPY_BYTE(0)
	COPY_BYTE(1)
	COPY_BYTE(2)
	COPY_BYTE(3)
	COPY_BYTE(4)
	COPY_BYTE(5)
EXC(	lb	t0, NBYTES-2(src), l_exc)
	SUB	len, len, 1
	jr	ra
EXC(	 sb	t0, NBYTES-2(dst), s_exc_p1)
done:
	jr	ra
	 nop
	END(memcpy)

l_exc_copy_rewind16:
	/* Rewind src and dst by 16*NBYTES for l_exc_copy */
	SUB	src, src, 16*NBYTES
	SUB	dst, dst, 16*NBYTES
l_exc_copy:
	/*
	 * Copy bytes from src until faulting load address (or until a
	 * lb faults)
	 *
	 * When reached by a faulting LDFIRST/LDREST, THREAD_BUADDR($28)
	 * may be more than a byte beyond the last address.
	 * Hence, the lb below may get an exception.
	 *
	 * Assumes src < THREAD_BUADDR($28)
	 */
	LOAD	t0, TI_TASK($28)
	LOAD	t0, THREAD_BUADDR(t0)
1:
EXC(	lb	t1, 0(src),	l_exc)
	ADD	src, src, 1
	sb	t1, 0(dst)	# can't fault -- we're copy_from_user
	bne	src, t0, 1b
	 ADD	dst, dst, 1
l_exc:
	LOAD	t0, TI_TASK($28)
	LOAD	t0, THREAD_BUADDR(t0)	# t0 is just past last good address
	SUB	len, AT, t0		# len number of uncopied bytes
	jr	ra
	 nop


#define SEXC(n)				\
s_exc_p ## n ## u:			\
	jr	ra;			\
	 ADD	len, len, n*NBYTES

SEXC(16)
SEXC(15)
SEXC(14)
SEXC(13)
SEXC(12)
SEXC(11)
SEXC(10)
SEXC(9)
SEXC(8)
SEXC(7)
SEXC(6)
SEXC(5)
SEXC(4)
SEXC(3)
SEXC(2)
SEXC(1)

s_exc_p1:
	jr	ra
	 ADD	len, len, 1
s_exc:
	jr	ra
	 nop

	.align	5
LEAF(memmove)
EXPORT_SYMBOL(memmove)
	ADD	t0, a0, a2
	ADD	t1, a1, a2
	sltu	t0, a1, t0			# dst + len <= src -> memcpy
	sltu	t1, a0, t1			# dst >= src + len -> memcpy
	and	t0, t1
	beqz	t0, __memcpy
	 move	v0, a0				/* return value */
	beqz	a2, r_out
	END(memmove)

	/* fall through to __rmemcpy */
LEAF(__rmemcpy)					/* a0=dst a1=src a2=len */
	 sltu	t0, a1, a0
	beqz	t0, r_end_bytes_up		# src >= dst
	 nop
	ADD	a0, a2				# dst = dst + len
	ADD	a1, a2				# src = src + len

r_end_bytes:
	lb	t0, -1(a1)
	SUB	a2, a2, 0x1
	sb	t0, -1(a0)
	SUB	a1, a1, 0x1
	bnez	a2, r_end_bytes
	 SUB	a0, a0, 0x1

r_out:
	jr	ra
	 move	a2, zero

r_end_bytes_up:
	lb	t0, (a1)
	SUB	a2, a2, 0x1
	sb	t0, (a0)
	ADD	a1, a1, 0x1
	bnez	a2, r_end_bytes_up
	 ADD	a0, a0, 0x1

	jr	ra
	 move	a2, zero
	END(__rmemcpy)
