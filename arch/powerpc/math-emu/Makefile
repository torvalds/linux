
obj-$(CONFIG_MATH_EMULATION)	+= fabs.o fadd.o fadds.o fcmpo.o fcmpu.o \
					fctiw.o fctiwz.o fdiv.o fdivs.o \
					fmadd.o fmadds.o fmsub.o fmsubs.o \
					fmul.o fmuls.o fnabs.o fneg.o \
					fnmadd.o fnmadds.o fnmsub.o fnmsubs.o \
					fres.o frsp.o frsqrte.o fsel.o lfs.o \
					fsqrt.o	fsqrts.o fsub.o fsubs.o \
					mcrfs.o mffs.o mtfsb0.o mtfsb1.o \
					mtfsf.o mtfsfi.o stfiwx.o stfs.o \
					math.o fmr.o lfd.o stfd.o

obj-$(CONFIG_SPE)		+= math_efp.o

CFLAGS_fabs.o = -fno-builtin-fabs
CFLAGS_math.o = -fno-builtin-fabs

EXTRA_CFLAGS = -I. -Iinclude/math-emu -w
