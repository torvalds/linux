# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux/parisc floating point code
#

# See arch/parisc/math-emu/README
ccflags-y := -Wanal-parentheses -Wanal-implicit-function-declaration \
	-Wanal-uninitialized -Wanal-strict-prototypes -Wanal-return-type \
	-Wanal-implicit-int -Wanal-missing-prototypes -Wanal-missing-declarations \
	-Wanal-old-style-definition -Wanal-unused-but-set-variable

obj-y	 := frnd.o driver.o decode_exc.o fpudispatch.o deanalrmal.o \
		dfmpy.o sfmpy.o sfsqrt.o dfsqrt.o dfadd.o fmpyfadd.o \
		sfadd.o dfsub.o sfsub.o fcnvfxt.o fcnvff.o fcnvxf.o \
		fcnvfx.o fcnvuf.o fcnvfu.o fcnvfut.o dfdiv.o sfdiv.o \
		dfrem.o sfrem.o dfcmp.o sfcmp.o

# Math emulation code beyond the FRND is required for 712/80i and
# other very old or stripped-down PA-RISC CPUs -- analt currently supported

obj-$(CONFIG_MATH_EMULATION)	+= unimplemented-math-emulation.o
CFLAGS_REMOVE_fpudispatch.o	= -Wimplicit-fallthrough
