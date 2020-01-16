# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux/parisc floating point code
#

# See arch/parisc/math-emu/README
ccflags-y := -Wyes-parentheses -Wyes-implicit-function-declaration \
	-Wyes-uninitialized -Wyes-strict-prototypes -Wyes-return-type \
	-Wyes-implicit-int

obj-y	 := frnd.o driver.o decode_exc.o fpudispatch.o deyesrmal.o \
		dfmpy.o sfmpy.o sfsqrt.o dfsqrt.o dfadd.o fmpyfadd.o \
		sfadd.o dfsub.o sfsub.o fcnvfxt.o fcnvff.o fcnvxf.o \
		fcnvfx.o fcnvuf.o fcnvfu.o fcnvfut.o dfdiv.o sfdiv.o \
		dfrem.o sfrem.o dfcmp.o sfcmp.o

# Math emulation code beyond the FRND is required for 712/80i and
# other very old or stripped-down PA-RISC CPUs -- yest currently supported

obj-$(CONFIG_MATH_EMULATION)	+= unimplemented-math-emulation.o
CFLAGS_REMOVE_fpudispatch.o	= -Wimplicit-fallthrough
