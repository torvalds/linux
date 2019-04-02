#
# linux/arch/arm/vfp/Makefile
#
# Copyright (C) 2001 ARM Limited
#

# ccflags-y := -DDE
# asflags-y := -DDE

KBUILD_AFLAGS	:=$(KBUILD_AFLAGS:-msoft-float=-Wa,-mfpu=softvfp+vfp -mfloat-abi=soft)

obj-y		+= vfpmodule.o entry.o vfphw.o vfpsingle.o vfpdouble.o
