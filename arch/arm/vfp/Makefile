#
# linux/arch/arm/vfp/Makefile
#
# Copyright (C) 2001 ARM Limited
#

# EXTRA_CFLAGS := -DDEBUG
# EXTRA_AFLAGS := -DDEBUG

AFLAGS		:=$(AFLAGS:-msoft-float=-Wa,-mfpu=softvfp+vfp)
LDFLAGS		+=--no-warn-mismatch

obj-y			+= vfp.o

vfp-$(CONFIG_VFP)	+= vfpmodule.o entry.o vfphw.o vfpsingle.o vfpdouble.o
