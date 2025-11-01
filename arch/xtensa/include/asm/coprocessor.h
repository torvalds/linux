/*
 * include/asm-xtensa/coprocessor.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 - 2007 Tensilica Inc.
 */


#ifndef _XTENSA_COPROCESSOR_H
#define _XTENSA_COPROCESSOR_H

#include <variant/tie.h>
#include <asm/core.h>
#include <asm/types.h>

#ifdef __ASSEMBLER__
# include <variant/tie-asm.h>

.macro	xchal_sa_start  a b
	.set .Lxchal_pofs_, 0
	.set .Lxchal_ofs_, 0
.endm

.macro	xchal_sa_align  ptr minofs maxofs ofsalign totalign
	.set	.Lxchal_ofs_, .Lxchal_ofs_ + .Lxchal_pofs_ + \totalign - 1
	.set	.Lxchal_ofs_, (.Lxchal_ofs_ & -\totalign) - .Lxchal_pofs_
.endm

#define _SELECT	(  XTHAL_SAS_TIE | XTHAL_SAS_OPT \
		 | XTHAL_SAS_CC \
		 | XTHAL_SAS_CALR | XTHAL_SAS_CALE )

.macro save_xtregs_opt ptr clb at1 at2 at3 at4 offset
	.if XTREGS_OPT_SIZE > 0
		addi	\clb, \ptr, \offset
		xchal_ncp_store \clb \at1 \at2 \at3 \at4 select=_SELECT
	.endif
.endm

.macro load_xtregs_opt ptr clb at1 at2 at3 at4 offset
	.if XTREGS_OPT_SIZE > 0
		addi	\clb, \ptr, \offset
		xchal_ncp_load \clb \at1 \at2 \at3 \at4 select=_SELECT
	.endif
.endm
#undef _SELECT

#define _SELECT	(  XTHAL_SAS_TIE | XTHAL_SAS_OPT \
		 | XTHAL_SAS_NOCC \
		 | XTHAL_SAS_CALR | XTHAL_SAS_CALE | XTHAL_SAS_GLOB )

.macro save_xtregs_user ptr clb at1 at2 at3 at4 offset
	.if XTREGS_USER_SIZE > 0
		addi	\clb, \ptr, \offset
		xchal_ncp_store \clb \at1 \at2 \at3 \at4 select=_SELECT
	.endif
.endm

.macro load_xtregs_user ptr clb at1 at2 at3 at4 offset
	.if XTREGS_USER_SIZE > 0
		addi	\clb, \ptr, \offset
		xchal_ncp_load \clb \at1 \at2 \at3 \at4 select=_SELECT
	.endif
.endm
#undef _SELECT



#endif	/* __ASSEMBLER__ */

/*
 * XTENSA_HAVE_COPROCESSOR(x) returns 1 if coprocessor x is configured.
 *
 * XTENSA_HAVE_IO_PORT(x) returns 1 if io-port x is configured.
 *
 */

#define XTENSA_HAVE_COPROCESSOR(x)					\
	((XCHAL_CP_MASK ^ XCHAL_CP_PORT_MASK) & (1 << (x)))
#define XTENSA_HAVE_COPROCESSORS					\
	(XCHAL_CP_MASK ^ XCHAL_CP_PORT_MASK)
#define XTENSA_HAVE_IO_PORT(x)						\
	(XCHAL_CP_PORT_MASK & (1 << (x)))
#define XTENSA_HAVE_IO_PORTS						\
	XCHAL_CP_PORT_MASK

#ifndef __ASSEMBLER__

/*
 * Additional registers.
 * We define three types of additional registers:
 *  ext: extra registers that are used by the compiler
 *  cpn: optional registers that can be used by a user application
 *  cpX: coprocessor registers that can only be used if the corresponding
 *       CPENABLE bit is set.
 */

#define XCHAL_SA_REG(list,cc,abi,type,y,name,z,align,size,...)	\
	__REG ## list (cc, abi, type, name, size, align)

#define __REG0(cc,abi,t,name,s,a)	__REG0_ ## cc (abi,name)
#define __REG1(cc,abi,t,name,s,a)	__REG1_ ## cc (name)
#define __REG2(cc,abi,type,...)		__REG2_ ## type (__VA_ARGS__)

#define __REG0_0(abi,name)
#define __REG0_1(abi,name)		__REG0_1 ## abi (name)
#define __REG0_10(name)	__u32 name;
#define __REG0_11(name)	__u32 name;
#define __REG0_12(name)

#define __REG1_0(name)	__u32 name;
#define __REG1_1(name)

#define __REG2_0(n,s,a)	__u32 name;
#define __REG2_1(n,s,a)	unsigned char n[s] __attribute__ ((aligned(a)));
#define __REG2_2(n,s,a) unsigned char n[s] __attribute__ ((aligned(a)));

typedef struct { XCHAL_NCP_SA_LIST(0) } xtregs_opt_t
	__attribute__ ((aligned (XCHAL_NCP_SA_ALIGN)));
typedef struct { XCHAL_NCP_SA_LIST(1) } xtregs_user_t
	__attribute__ ((aligned (XCHAL_NCP_SA_ALIGN)));

#if XTENSA_HAVE_COPROCESSORS

typedef struct { XCHAL_CP0_SA_LIST(2) } xtregs_cp0_t
	__attribute__ ((aligned (XCHAL_CP0_SA_ALIGN)));
typedef struct { XCHAL_CP1_SA_LIST(2) } xtregs_cp1_t
	__attribute__ ((aligned (XCHAL_CP1_SA_ALIGN)));
typedef struct { XCHAL_CP2_SA_LIST(2) } xtregs_cp2_t
	__attribute__ ((aligned (XCHAL_CP2_SA_ALIGN)));
typedef struct { XCHAL_CP3_SA_LIST(2) } xtregs_cp3_t
	__attribute__ ((aligned (XCHAL_CP3_SA_ALIGN)));
typedef struct { XCHAL_CP4_SA_LIST(2) } xtregs_cp4_t
	__attribute__ ((aligned (XCHAL_CP4_SA_ALIGN)));
typedef struct { XCHAL_CP5_SA_LIST(2) } xtregs_cp5_t
	__attribute__ ((aligned (XCHAL_CP5_SA_ALIGN)));
typedef struct { XCHAL_CP6_SA_LIST(2) } xtregs_cp6_t
	__attribute__ ((aligned (XCHAL_CP6_SA_ALIGN)));
typedef struct { XCHAL_CP7_SA_LIST(2) } xtregs_cp7_t
	__attribute__ ((aligned (XCHAL_CP7_SA_ALIGN)));

struct thread_info;
void coprocessor_flush(struct thread_info *ti, int cp_index);
void coprocessor_release_all(struct thread_info *ti);
void coprocessor_flush_all(struct thread_info *ti);
void coprocessor_flush_release_all(struct thread_info *ti);
void local_coprocessors_flush_release_all(void);

#endif	/* XTENSA_HAVE_COPROCESSORS */

#endif	/* !__ASSEMBLER__ */
#endif	/* _XTENSA_COPROCESSOR_H */
