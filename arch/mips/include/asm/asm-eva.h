/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014 Imagination Technologies Ltd.
 *
 */

#ifndef __ASM_ASM_EVA_H
#define __ASM_ASM_EVA_H

#ifndef __ASSEMBLY__
#ifdef CONFIG_EVA

#define __BUILD_EVA_INSN(insn, reg, addr)				\
				"	.set	push\n"			\
				"	.set	mips0\n"		\
				"	.set	eva\n"			\
				"	"insn" "reg", "addr "\n"	\
				"	.set	pop\n"

#define user_cache(op, base)		__BUILD_EVA_INSN("cachee", op, base)
#define user_ll(reg, addr)		__BUILD_EVA_INSN("lle", reg, addr)
#define user_sc(reg, addr)		__BUILD_EVA_INSN("sce", reg, addr)
#define user_lw(reg, addr)		__BUILD_EVA_INSN("lwe", reg, addr)
#define user_lwl(reg, addr)		__BUILD_EVA_INSN("lwle", reg, addr)
#define user_lwr(reg, addr)		__BUILD_EVA_INSN("lwre", reg, addr)
#define user_lh(reg, addr)		__BUILD_EVA_INSN("lhe", reg, addr)
#define user_lb(reg, addr)		__BUILD_EVA_INSN("lbe", reg, addr)
#define user_lbu(reg, addr)		__BUILD_EVA_INSN("lbue", reg, addr)
/* No 64-bit EVA instruction for loading double words */
#define user_ld(reg, addr)		user_lw(reg, addr)
#define user_sw(reg, addr)		__BUILD_EVA_INSN("swe", reg, addr)
#define user_swl(reg, addr)		__BUILD_EVA_INSN("swle", reg, addr)
#define user_swr(reg, addr)		__BUILD_EVA_INSN("swre", reg, addr)
#define user_sh(reg, addr)		__BUILD_EVA_INSN("she", reg, addr)
#define user_sb(reg, addr)		__BUILD_EVA_INSN("sbe", reg, addr)
/* No 64-bit EVA instruction for storing double words */
#define user_sd(reg, addr)		user_sw(reg, addr)

#else

#define user_cache(op, base)		"cache " op ", " base "\n"
#define user_ll(reg, addr)		"ll " reg ", " addr "\n"
#define user_sc(reg, addr)		"sc " reg ", " addr "\n"
#define user_lw(reg, addr)		"lw " reg ", " addr "\n"
#define user_lwl(reg, addr)		"lwl " reg ", " addr "\n"
#define user_lwr(reg, addr)		"lwr " reg ", " addr "\n"
#define user_lh(reg, addr)		"lh " reg ", " addr "\n"
#define user_lb(reg, addr)		"lb " reg ", " addr "\n"
#define user_lbu(reg, addr)		"lbu " reg ", " addr "\n"
#define user_sw(reg, addr)		"sw " reg ", " addr "\n"
#define user_swl(reg, addr)		"swl " reg ", " addr "\n"
#define user_swr(reg, addr)		"swr " reg ", " addr "\n"
#define user_sh(reg, addr)		"sh " reg ", " addr "\n"
#define user_sb(reg, addr)		"sb " reg ", " addr "\n"

#ifdef CONFIG_32BIT
/*
 * No 'sd' or 'ld' instructions in 32-bit but the code will
 * do the correct thing
 */
#define user_sd(reg, addr)		user_sw(reg, addr)
#define user_ld(reg, addr)		user_lw(reg, addr)
#else
#define user_sd(reg, addr)		"sd " reg", " addr "\n"
#define user_ld(reg, addr)		"ld " reg", " addr "\n"
#endif /* CONFIG_32BIT */

#endif /* CONFIG_EVA */

#else /* __ASSEMBLY__ */

#ifdef CONFIG_EVA

#define __BUILD_EVA_INSN(insn, reg, addr)			\
				.set	push;			\
				.set	mips0;			\
				.set	eva;			\
				insn reg, addr;			\
				.set	pop;

#define user_cache(op, base)		__BUILD_EVA_INSN(cachee, op, base)
#define user_ll(reg, addr)		__BUILD_EVA_INSN(lle, reg, addr)
#define user_sc(reg, addr)		__BUILD_EVA_INSN(sce, reg, addr)
#define user_lw(reg, addr)		__BUILD_EVA_INSN(lwe, reg, addr)
#define user_lwl(reg, addr)		__BUILD_EVA_INSN(lwle, reg, addr)
#define user_lwr(reg, addr)		__BUILD_EVA_INSN(lwre, reg, addr)
#define user_lh(reg, addr)		__BUILD_EVA_INSN(lhe, reg, addr)
#define user_lb(reg, addr)		__BUILD_EVA_INSN(lbe, reg, addr)
#define user_lbu(reg, addr)		__BUILD_EVA_INSN(lbue, reg, addr)
/* No 64-bit EVA instruction for loading double words */
#define user_ld(reg, addr)		user_lw(reg, addr)
#define user_sw(reg, addr)		__BUILD_EVA_INSN(swe, reg, addr)
#define user_swl(reg, addr)		__BUILD_EVA_INSN(swle, reg, addr)
#define user_swr(reg, addr)		__BUILD_EVA_INSN(swre, reg, addr)
#define user_sh(reg, addr)		__BUILD_EVA_INSN(she, reg, addr)
#define user_sb(reg, addr)		__BUILD_EVA_INSN(sbe, reg, addr)
/* No 64-bit EVA instruction for loading double words */
#define user_sd(reg, addr)		user_sw(reg, addr)
#else

#define user_cache(op, base)		cache op, base
#define user_ll(reg, addr)		ll reg, addr
#define user_sc(reg, addr)		sc reg, addr
#define user_lw(reg, addr)		lw reg, addr
#define user_lwl(reg, addr)		lwl reg, addr
#define user_lwr(reg, addr)		lwr reg, addr
#define user_lh(reg, addr)		lh reg, addr
#define user_lb(reg, addr)		lb reg, addr
#define user_lbu(reg, addr)		lbu reg, addr
#define user_sw(reg, addr)		sw reg, addr
#define user_swl(reg, addr)		swl reg, addr
#define user_swr(reg, addr)		swr reg, addr
#define user_sh(reg, addr)		sh reg, addr
#define user_sb(reg, addr)		sb reg, addr

#ifdef CONFIG_32BIT
/*
 * No 'sd' or 'ld' instructions in 32-bit but the code will
 * do the correct thing
 */
#define user_sd(reg, addr)		user_sw(reg, addr)
#define user_ld(reg, addr)		user_lw(reg, addr)
#else
#define user_sd(reg, addr)		sd reg, addr
#define user_ld(reg, addr)		ld reg, addr
#endif /* CONFIG_32BIT */

#endif /* CONFIG_EVA */

#endif /* __ASSEMBLY__ */

#endif /* __ASM_ASM_EVA_H */
