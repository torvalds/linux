#
# Makefile for the linux/parisc floating point code
#

# See arch/parisc/math-emu/README
ccflags-y := -Wno-parentheses -Wno-implicit-function-declaration \
	-Wno-uninitialized -Wno-strict-prototypes -Wno-return-type \
	-Wno-implicit-int

obj-y	 := frnd.o driver.o decode_exc.o fpudispatch.o denormal.o \
		dfmpy.o sfmpy.o sfsqrt.o dfsqrt.o dfadd.o fmpyfadd.o \
		sfadd.o dfsub.o sfsub.o fcnvfxt.o fcnvff.o fcnvxf.o \
		fcnvfx.o fcnvuf.o fcnvfu.o fcnvfut.o dfdiv.o sfdiv.o \
		dfrem.o sfrem.o dfcmp.o sfcmp.o

# Math emulation code beyond the FRND is required for 712/80i and
# other very old or stripped-down PA-RISC CPUs -- not currently supported

obj-$(CONFIG_MATH_EMULATION)	+= unimplemented-math-emulation.o
