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
 * Copyright (C) 2007  Maciej W. Rozycki
 * Copyright (C) 2014 Imagination Technologies Ltd.
 *
 * Mnemonic names for arguments to memcpy/__copy_user
 */

/*
 * Hack to resolve longstanding prefetch issue
 *
 * Prefetching may be fatal on some systems if we're prefetching beyond the
 * end of memory on some systems.  It's also a seriously bad idea on non
 * dma-coherent systems.
 */
#ifdef CONFIG_DMA_NONCOHERENT
#undef CONFIG_CPU_HAS_PREFETCH
#endif
#ifdef CONFIG_MIPS_MALTA
#undef CONFIG_CPU_HAS_PREFETCH
#endif
#ifdef CONFIG_CPU_MIPSR6
#undef CONFIG_CPU_HAS_PREFETCH
#endif

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
 * include/asm-mips/uaccess.h
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

/* Instruction type */
#define LD_INSN 1
#define ST_INSN 2
/* Pretech type */
#define SRC_PREFETCH 1
#define DST_PREFETCH 2
#define LEGACY_MODE 1
#define EVA_MODE    2
#define USEROP   1
#define KERNELOP 2

/*
 * Wrapper to add an entry in the exception table
 * in case the insn causes a memory exception.
 * Arguments:
 * insn    : Load/store instruction
 * type    : Instruction type
 * reg     : Register
 * addr    : Address
 * handler : Exception handler
 */

#define EXC(insn, type, reg, addr, handler)			\
	.if \mode == LEGACY_MODE;				\
9:		insn reg, addr;					\
		.section __ex_table,"a";			\
		PTR	9b, handler;				\
		.previous;					\
	/* This is assembled in EVA mode */			\
	.else;							\
		/* If loading from user or storing to user */	\
		.if ((\from == USEROP) && (type == LD_INSN)) || \
		    ((\to == USEROP) && (type == ST_INSN));	\
9:			__BUILD_EVA_INSN(insn##e, reg, addr);	\
			.section __ex_table,"a";		\
			PTR	9b, handler;			\
			.previous;				\
		.else;						\
			/*					\
			 *  Still in EVA, but no need for	\
			 * exception handler or EVA insn	\
			 */					\
			insn reg, addr;				\
		.endif;						\
	.endif

/*
 * Only on the 64-bit kernel we can made use of 64-bit registers.
 */
#ifdef CONFIG_64BIT
#define USE_DOUBLE
#endif

#ifdef USE_DOUBLE

#define LOADK ld /* No exception */
#define LOAD(reg, addr, handler)	EXC(ld, LD_INSN, reg, addr, handler)
#define LOADL(reg, addr, handler)	EXC(ldl, LD_INSN, reg, addr, handler)
#define LOADR(reg, addr, handler)	EXC(ldr, LD_INSN, reg, addr, handler)
#define STOREL(reg, addr, handler)	EXC(sdl, ST_INSN, reg, addr, handler)
#define STORER(reg, addr, handler)	EXC(sdr, ST_INSN, reg, addr, handler)
#define STORE(reg, addr, handler)	EXC(sd, ST_INSN, reg, addr, handler)
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

#else

#define LOADK lw /* No exception */
#define LOAD(reg, addr, handler)	EXC(lw, LD_INSN, reg, addr, handler)
#define LOADL(reg, addr, handler)	EXC(lwl, LD_INSN, reg, addr, handler)
#define LOADR(reg, addr, handler)	EXC(lwr, LD_INSN, reg, addr, handler)
#define STOREL(reg, addr, handler)	EXC(swl, ST_INSN, reg, addr, handler)
#define STORER(reg, addr, handler)	EXC(swr, ST_INSN, reg, addr, handler)
#define STORE(reg, addr, handler)	EXC(sw, ST_INSN, reg, addr, handler)
#define ADD    addu
#define SUB    subu
#define SRL    srl
#define SLL    sll
#define SRA    sra
#define SLLV   sllv
#define SRLV   srlv
#define NBYTES 4
#define LOG_NBYTES 2

#endif /* USE_DOUBLE */

#define LOADB(reg, addr, handler)	EXC(lb, LD_INSN, reg, addr, handler)
#define STOREB(reg, addr, handler)	EXC(sb, ST_INSN, reg, addr, handler)

#ifdef CONFIG_CPU_HAS_PREFETCH
# define _PREF(hint, addr, type)					\
	.if \mode == LEGACY_MODE;					\
		kernel_pref(hint, addr);				\
	.else;								\
		.if ((\from == USEROP) && (type == SRC_PREFETCH)) ||	\
		    ((\to == USEROP) && (type == DST_PREFETCH));	\
			/*						\
			 * PREFE has only 9 bits for the offset		\
			 * compared to PREF which has 16, so it may	\
			 * need to use the $at register but this	\
			 * register should remain intact because it's	\
			 * used later on. Therefore use $v1.		\
			 */						\
			.set at=v1;					\
			user_pref(hint, addr);				\
			.set noat;					\
		.else;							\
			kernel_pref(hint, addr);			\
		.endif;							\
	.endif
#else
# define _PREF(hint, addr, type)
#endif

#define PREFS(hint, addr) _PREF(hint, addr, SRC_PREFETCH)
#define PREFD(hint, addr) _PREF(hint, addr, DST_PREFETCH)

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
#ifndef CONFIG_CPU_DADDI_WORKAROUNDS
	.set	noat
#else
	.set	at=v1
#endif

	.align	5

	/*
	 * Macro to build the __copy_user common code
	 * Arguments:
	 * mode : LEGACY_MODE or EVA_MODE
	 * from : Source operand. USEROP or KERNELOP
	 * to   : Destination operand. USEROP or KERNELOP
	 */
	.macro __BUILD_COPY_USER mode, from, to

	/* initialize __memcpy if this the first time we execute this macro */
	.ifnotdef __memcpy
	.set __memcpy, 1
	.hidden __memcpy /* make sure it does not leak */
	.endif

	/*
	 * Note: dst & src may be unaligned, len may be 0
	 * Temps
	 */
#define rem t8

	R10KCBARRIER(0(ra))
	/*
	 * The "issue break"s below are very approximate.
	 * Issue delays for dcache fills will perturb the schedule, as will
	 * load queue full replay traps, etc.
	 *
	 * If len < NBYTES use byte operations.
	 */
	PREFS(	0, 0(src) )
	PREFD(	1, 0(dst) )
	sltu	t2, len, NBYTES
	and	t1, dst, ADDRMASK
	PREFS(	0, 1*32(src) )
	PREFD(	1, 1*32(dst) )
	bnez	t2, .Lcopy_bytes_checklen\@
	 and	t0, src, ADDRMASK
	PREFS(	0, 2*32(src) )
	PREFD(	1, 2*32(dst) )
#ifndef CONFIG_CPU_NO_LOAD_STORE_LR
	bnez	t1, .Ldst_unaligned\@
	 nop
	bnez	t0, .Lsrc_unaligned_dst_aligned\@
#else /* CONFIG_CPU_NO_LOAD_STORE_LR */
	or	t0, t0, t1
	bnez	t0, .Lcopy_unaligned_bytes\@
#endif /* CONFIG_CPU_NO_LOAD_STORE_LR */
	/*
	 * use delay slot for fall-through
	 * src and dst are aligned; need to compute rem
	 */
.Lboth_aligned\@:
	 SRL	t0, len, LOG_NBYTES+3	 # +3 for 8 units/iter
	beqz	t0, .Lcleanup_both_aligned\@ # len < 8*NBYTES
	 and	rem, len, (8*NBYTES-1)	 # rem = len % (8*NBYTES)
	PREFS(	0, 3*32(src) )
	PREFD(	1, 3*32(dst) )
	.align	4
1:
	R10KCBARRIER(0(ra))
	LOAD(t0, UNIT(0)(src), .Ll_exc\@)
	LOAD(t1, UNIT(1)(src), .Ll_exc_copy\@)
	LOAD(t2, UNIT(2)(src), .Ll_exc_copy\@)
	LOAD(t3, UNIT(3)(src), .Ll_exc_copy\@)
	SUB	len, len, 8*NBYTES
	LOAD(t4, UNIT(4)(src), .Ll_exc_copy\@)
	LOAD(t7, UNIT(5)(src), .Ll_exc_copy\@)
	STORE(t0, UNIT(0)(dst),	.Ls_exc_p8u\@)
	STORE(t1, UNIT(1)(dst),	.Ls_exc_p7u\@)
	LOAD(t0, UNIT(6)(src), .Ll_exc_copy\@)
	LOAD(t1, UNIT(7)(src), .Ll_exc_copy\@)
	ADD	src, src, 8*NBYTES
	ADD	dst, dst, 8*NBYTES
	STORE(t2, UNIT(-6)(dst), .Ls_exc_p6u\@)
	STORE(t3, UNIT(-5)(dst), .Ls_exc_p5u\@)
	STORE(t4, UNIT(-4)(dst), .Ls_exc_p4u\@)
	STORE(t7, UNIT(-3)(dst), .Ls_exc_p3u\@)
	STORE(t0, UNIT(-2)(dst), .Ls_exc_p2u\@)
	STORE(t1, UNIT(-1)(dst), .Ls_exc_p1u\@)
	PREFS(	0, 8*32(src) )
	PREFD(	1, 8*32(dst) )
	bne	len, rem, 1b
	 nop

	/*
	 * len == rem == the number of bytes left to copy < 8*NBYTES
	 */
.Lcleanup_both_aligned\@:
	beqz	len, .Ldone\@
	 sltu	t0, len, 4*NBYTES
	bnez	t0, .Lless_than_4units\@
	 and	rem, len, (NBYTES-1)	# rem = len % NBYTES
	/*
	 * len >= 4*NBYTES
	 */
	LOAD( t0, UNIT(0)(src),	.Ll_exc\@)
	LOAD( t1, UNIT(1)(src),	.Ll_exc_copy\@)
	LOAD( t2, UNIT(2)(src),	.Ll_exc_copy\@)
	LOAD( t3, UNIT(3)(src),	.Ll_exc_copy\@)
	SUB	len, len, 4*NBYTES
	ADD	src, src, 4*NBYTES
	R10KCBARRIER(0(ra))
	STORE(t0, UNIT(0)(dst),	.Ls_exc_p4u\@)
	STORE(t1, UNIT(1)(dst),	.Ls_exc_p3u\@)
	STORE(t2, UNIT(2)(dst),	.Ls_exc_p2u\@)
	STORE(t3, UNIT(3)(dst),	.Ls_exc_p1u\@)
	.set	reorder				/* DADDI_WAR */
	ADD	dst, dst, 4*NBYTES
	beqz	len, .Ldone\@
	.set	noreorder
.Lless_than_4units\@:
	/*
	 * rem = len % NBYTES
	 */
	beq	rem, len, .Lcopy_bytes\@
	 nop
1:
	R10KCBARRIER(0(ra))
	LOAD(t0, 0(src), .Ll_exc\@)
	ADD	src, src, NBYTES
	SUB	len, len, NBYTES
	STORE(t0, 0(dst), .Ls_exc_p1u\@)
	.set	reorder				/* DADDI_WAR */
	ADD	dst, dst, NBYTES
	bne	rem, len, 1b
	.set	noreorder

#ifndef CONFIG_CPU_NO_LOAD_STORE_LR
	/*
	 * src and dst are aligned, need to copy rem bytes (rem < NBYTES)
	 * A loop would do only a byte at a time with possible branch
	 * mispredicts.	 Can't do an explicit LOAD dst,mask,or,STORE
	 * because can't assume read-access to dst.  Instead, use
	 * STREST dst, which doesn't require read access to dst.
	 *
	 * This code should perform better than a simple loop on modern,
	 * wide-issue mips processors because the code has fewer branches and
	 * more instruction-level parallelism.
	 */
#define bits t2
	beqz	len, .Ldone\@
	 ADD	t1, dst, len	# t1 is just past last byte of dst
	li	bits, 8*NBYTES
	SLL	rem, len, 3	# rem = number of bits to keep
	LOAD(t0, 0(src), .Ll_exc\@)
	SUB	bits, bits, rem # bits = number of bits to discard
	SHIFT_DISCARD t0, t0, bits
	STREST(t0, -1(t1), .Ls_exc\@)
	jr	ra
	 move	len, zero
.Ldst_unaligned\@:
	/*
	 * dst is unaligned
	 * t0 = src & ADDRMASK
	 * t1 = dst & ADDRMASK; T1 > 0
	 * len >= NBYTES
	 *
	 * Copy enough bytes to align dst
	 * Set match = (src and dst have same alignment)
	 */
#define match rem
	LDFIRST(t3, FIRST(0)(src), .Ll_exc\@)
	ADD	t2, zero, NBYTES
	LDREST(t3, REST(0)(src), .Ll_exc_copy\@)
	SUB	t2, t2, t1	# t2 = number of bytes copied
	xor	match, t0, t1
	R10KCBARRIER(0(ra))
	STFIRST(t3, FIRST(0)(dst), .Ls_exc\@)
	beq	len, t2, .Ldone\@
	 SUB	len, len, t2
	ADD	dst, dst, t2
	beqz	match, .Lboth_aligned\@
	 ADD	src, src, t2

.Lsrc_unaligned_dst_aligned\@:
	SRL	t0, len, LOG_NBYTES+2	 # +2 for 4 units/iter
	PREFS(	0, 3*32(src) )
	beqz	t0, .Lcleanup_src_unaligned\@
	 and	rem, len, (4*NBYTES-1)	 # rem = len % 4*NBYTES
	PREFD(	1, 3*32(dst) )
1:
/*
 * Avoid consecutive LD*'s to the same register since some mips
 * implementations can't issue them in the same cycle.
 * It's OK to load FIRST(N+1) before REST(N) because the two addresses
 * are to the same unit (unless src is aligned, but it's not).
 */
	R10KCBARRIER(0(ra))
	LDFIRST(t0, FIRST(0)(src), .Ll_exc\@)
	LDFIRST(t1, FIRST(1)(src), .Ll_exc_copy\@)
	SUB	len, len, 4*NBYTES
	LDREST(t0, REST(0)(src), .Ll_exc_copy\@)
	LDREST(t1, REST(1)(src), .Ll_exc_copy\@)
	LDFIRST(t2, FIRST(2)(src), .Ll_exc_copy\@)
	LDFIRST(t3, FIRST(3)(src), .Ll_exc_copy\@)
	LDREST(t2, REST(2)(src), .Ll_exc_copy\@)
	LDREST(t3, REST(3)(src), .Ll_exc_copy\@)
	PREFS(	0, 9*32(src) )		# 0 is PREF_LOAD  (not streamed)
	ADD	src, src, 4*NBYTES
#ifdef CONFIG_CPU_SB1
	nop				# improves slotting
#endif
	STORE(t0, UNIT(0)(dst),	.Ls_exc_p4u\@)
	STORE(t1, UNIT(1)(dst),	.Ls_exc_p3u\@)
	STORE(t2, UNIT(2)(dst),	.Ls_exc_p2u\@)
	STORE(t3, UNIT(3)(dst),	.Ls_exc_p1u\@)
	PREFD(	1, 9*32(dst) )		# 1 is PREF_STORE (not streamed)
	.set	reorder				/* DADDI_WAR */
	ADD	dst, dst, 4*NBYTES
	bne	len, rem, 1b
	.set	noreorder

.Lcleanup_src_unaligned\@:
	beqz	len, .Ldone\@
	 and	rem, len, NBYTES-1  # rem = len % NBYTES
	beq	rem, len, .Lcopy_bytes\@
	 nop
1:
	R10KCBARRIER(0(ra))
	LDFIRST(t0, FIRST(0)(src), .Ll_exc\@)
	LDREST(t0, REST(0)(src), .Ll_exc_copy\@)
	ADD	src, src, NBYTES
	SUB	len, len, NBYTES
	STORE(t0, 0(dst), .Ls_exc_p1u\@)
	.set	reorder				/* DADDI_WAR */
	ADD	dst, dst, NBYTES
	bne	len, rem, 1b
	.set	noreorder

#endif /* !CONFIG_CPU_NO_LOAD_STORE_LR */
.Lcopy_bytes_checklen\@:
	beqz	len, .Ldone\@
	 nop
.Lcopy_bytes\@:
	/* 0 < len < NBYTES  */
	R10KCBARRIER(0(ra))
#define COPY_BYTE(N)			\
	LOADB(t0, N(src), .Ll_exc\@);	\
	SUB	len, len, 1;		\
	beqz	len, .Ldone\@;		\
	STOREB(t0, N(dst), .Ls_exc_p1\@)

	COPY_BYTE(0)
	COPY_BYTE(1)
#ifdef USE_DOUBLE
	COPY_BYTE(2)
	COPY_BYTE(3)
	COPY_BYTE(4)
	COPY_BYTE(5)
#endif
	LOADB(t0, NBYTES-2(src), .Ll_exc\@)
	SUB	len, len, 1
	jr	ra
	STOREB(t0, NBYTES-2(dst), .Ls_exc_p1\@)
.Ldone\@:
	jr	ra
	 nop

#ifdef CONFIG_CPU_NO_LOAD_STORE_LR
.Lcopy_unaligned_bytes\@:
1:
	COPY_BYTE(0)
	COPY_BYTE(1)
	COPY_BYTE(2)
	COPY_BYTE(3)
	COPY_BYTE(4)
	COPY_BYTE(5)
	COPY_BYTE(6)
	COPY_BYTE(7)
	ADD	src, src, 8
	b	1b
	 ADD	dst, dst, 8
#endif /* CONFIG_CPU_NO_LOAD_STORE_LR */
	.if __memcpy == 1
	END(memcpy)
	.set __memcpy, 0
	.hidden __memcpy
	.endif

.Ll_exc_copy\@:
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
	LOADK	t0, TI_TASK($28)
	 nop
	LOADK	t0, THREAD_BUADDR(t0)
1:
	LOADB(t1, 0(src), .Ll_exc\@)
	ADD	src, src, 1
	sb	t1, 0(dst)	# can't fault -- we're copy_from_user
	.set	reorder				/* DADDI_WAR */
	ADD	dst, dst, 1
	bne	src, t0, 1b
	.set	noreorder
.Ll_exc\@:
	LOADK	t0, TI_TASK($28)
	 nop
	LOADK	t0, THREAD_BUADDR(t0)	# t0 is just past last good address
	 nop
	SUB	len, AT, t0		# len number of uncopied bytes
	jr	ra
	 nop

#define SEXC(n)							\
	.set	reorder;			/* DADDI_WAR */ \
.Ls_exc_p ## n ## u\@:						\
	ADD	len, len, n*NBYTES;				\
	jr	ra;						\
	.set	noreorder

SEXC(8)
SEXC(7)
SEXC(6)
SEXC(5)
SEXC(4)
SEXC(3)
SEXC(2)
SEXC(1)

.Ls_exc_p1\@:
	.set	reorder				/* DADDI_WAR */
	ADD	len, len, 1
	jr	ra
	.set	noreorder
.Ls_exc\@:
	jr	ra
	 nop
	.endm

	.align	5
LEAF(memmove)
EXPORT_SYMBOL(memmove)
	ADD	t0, a0, a2
	ADD	t1, a1, a2
	sltu	t0, a1, t0			# dst + len <= src -> memcpy
	sltu	t1, a0, t1			# dst >= src + len -> memcpy
	and	t0, t1
	beqz	t0, .L__memcpy
	 move	v0, a0				/* return value */
	beqz	a2, .Lr_out
	END(memmove)

	/* fall through to __rmemcpy */
LEAF(__rmemcpy)					/* a0=dst a1=src a2=len */
	 sltu	t0, a1, a0
	beqz	t0, .Lr_end_bytes_up		# src >= dst
	 nop
	ADD	a0, a2				# dst = dst + len
	ADD	a1, a2				# src = src + len

.Lr_end_bytes:
	R10KCBARRIER(0(ra))
	lb	t0, -1(a1)
	SUB	a2, a2, 0x1
	sb	t0, -1(a0)
	SUB	a1, a1, 0x1
	.set	reorder				/* DADDI_WAR */
	SUB	a0, a0, 0x1
	bnez	a2, .Lr_end_bytes
	.set	noreorder

.Lr_out:
	jr	ra
	 move	a2, zero

.Lr_end_bytes_up:
	R10KCBARRIER(0(ra))
	lb	t0, (a1)
	SUB	a2, a2, 0x1
	sb	t0, (a0)
	ADD	a1, a1, 0x1
	.set	reorder				/* DADDI_WAR */
	ADD	a0, a0, 0x1
	bnez	a2, .Lr_end_bytes_up
	.set	noreorder

	jr	ra
	 move	a2, zero
	END(__rmemcpy)

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
.L__memcpy:
FEXPORT(__copy_user)
EXPORT_SYMBOL(__copy_user)
	/* Legacy Mode, user <-> user */
	__BUILD_COPY_USER LEGACY_MODE USEROP USEROP

#ifdef CONFIG_EVA

/*
 * For EVA we need distinct symbols for reading and writing to user space.
 * This is because we need to use specific EVA instructions to perform the
 * virtual <-> physical translation when a virtual address is actually in user
 * space
 */

/*
 * __copy_from_user (EVA)
 */

LEAF(__copy_from_user_eva)
EXPORT_SYMBOL(__copy_from_user_eva)
	__BUILD_COPY_USER EVA_MODE USEROP KERNELOP
END(__copy_from_user_eva)



/*
 * __copy_to_user (EVA)
 */

LEAF(__copy_to_user_eva)
EXPORT_SYMBOL(__copy_to_user_eva)
__BUILD_COPY_USER EVA_MODE KERNELOP USEROP
END(__copy_to_user_eva)

/*
 * __copy_in_user (EVA)
 */

LEAF(__copy_in_user_eva)
EXPORT_SYMBOL(__copy_in_user_eva)
__BUILD_COPY_USER EVA_MODE USEROP USEROP
END(__copy_in_user_eva)

#endif
