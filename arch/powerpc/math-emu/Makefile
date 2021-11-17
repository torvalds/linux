# SPDX-License-Identifier: GPL-2.0
math-emu-common-objs = math.o fre.o fsqrt.o fsqrts.o frsqrtes.o mtfsf.o mtfsfi.o
obj-$(CONFIG_MATH_EMULATION_HW_UNIMPLEMENTED) += $(math-emu-common-objs)
obj-$(CONFIG_MATH_EMULATION_FULL) += $(math-emu-common-objs) fabs.o fadd.o \
					fadds.o fcmpo.o fcmpu.o fctiw.o \
					fctiwz.o fdiv.o fdivs.o  fmadd.o \
					fmadds.o fmsub.o fmsubs.o fmul.o \
					fmuls.o fnabs.o fneg.o fnmadd.o \
					fnmadds.o fnmsub.o fnmsubs.o fres.o \
					frsp.o fsel.o lfs.o frsqrte.o fsub.o \
					fsubs.o  mcrfs.o mffs.o mtfsb0.o \
					mtfsb1.o stfiwx.o stfs.o math.o \
					fmr.o lfd.o stfd.o

obj-$(CONFIG_SPE)		+= math_efp.o

CFLAGS_fabs.o = -fno-builtin-fabs
CFLAGS_math.o = -fno-builtin-fabs

ccflags-y = -w
