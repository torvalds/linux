/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright Novell Inc. 2010
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#ifndef __ASM_KVM_FPU_H__
#define __ASM_KVM_FPU_H__

#include <linux/types.h>

extern void fps_fres(struct thread_struct *t, u32 *dst, u32 *src1);
extern void fps_frsqrte(struct thread_struct *t, u32 *dst, u32 *src1);
extern void fps_fsqrts(struct thread_struct *t, u32 *dst, u32 *src1);

extern void fps_fadds(struct thread_struct *t, u32 *dst, u32 *src1, u32 *src2);
extern void fps_fdivs(struct thread_struct *t, u32 *dst, u32 *src1, u32 *src2);
extern void fps_fmuls(struct thread_struct *t, u32 *dst, u32 *src1, u32 *src2);
extern void fps_fsubs(struct thread_struct *t, u32 *dst, u32 *src1, u32 *src2);

extern void fps_fmadds(struct thread_struct *t, u32 *dst, u32 *src1, u32 *src2,
		       u32 *src3);
extern void fps_fmsubs(struct thread_struct *t, u32 *dst, u32 *src1, u32 *src2,
		       u32 *src3);
extern void fps_fnmadds(struct thread_struct *t, u32 *dst, u32 *src1, u32 *src2,
		        u32 *src3);
extern void fps_fnmsubs(struct thread_struct *t, u32 *dst, u32 *src1, u32 *src2,
		        u32 *src3);
extern void fps_fsel(struct thread_struct *t, u32 *dst, u32 *src1, u32 *src2,
		     u32 *src3);

#define FPD_ONE_IN(name) extern void fpd_ ## name(u64 *fpscr, u32 *cr, \
				u64 *dst, u64 *src1);
#define FPD_TWO_IN(name) extern void fpd_ ## name(u64 *fpscr, u32 *cr, \
				u64 *dst, u64 *src1, u64 *src2);
#define FPD_THREE_IN(name) extern void fpd_ ## name(u64 *fpscr, u32 *cr, \
				u64 *dst, u64 *src1, u64 *src2, u64 *src3);

extern void fpd_fcmpu(u64 *fpscr, u32 *cr, u64 *src1, u64 *src2);
extern void fpd_fcmpo(u64 *fpscr, u32 *cr, u64 *src1, u64 *src2);

FPD_ONE_IN(fsqrts)
FPD_ONE_IN(frsqrtes)
FPD_ONE_IN(fres)
FPD_ONE_IN(frsp)
FPD_ONE_IN(fctiw)
FPD_ONE_IN(fctiwz)
FPD_ONE_IN(fsqrt)
FPD_ONE_IN(fre)
FPD_ONE_IN(frsqrte)
FPD_ONE_IN(fneg)
FPD_ONE_IN(fabs)
FPD_TWO_IN(fadds)
FPD_TWO_IN(fsubs)
FPD_TWO_IN(fdivs)
FPD_TWO_IN(fmuls)
FPD_TWO_IN(fcpsgn)
FPD_TWO_IN(fdiv)
FPD_TWO_IN(fadd)
FPD_TWO_IN(fmul)
FPD_TWO_IN(fsub)
FPD_THREE_IN(fmsubs)
FPD_THREE_IN(fmadds)
FPD_THREE_IN(fnmsubs)
FPD_THREE_IN(fnmadds)
FPD_THREE_IN(fsel)
FPD_THREE_IN(fmsub)
FPD_THREE_IN(fmadd)
FPD_THREE_IN(fnmsub)
FPD_THREE_IN(fnmadd)

#endif
