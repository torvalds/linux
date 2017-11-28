# SPDX-License-Identifier: GPL-2.0
#
# Makefile for TBX library files..
#

asflags-y		+= -mmetac=2.1 -Wa,-mfpu=metac21 -mdsp
asflags-$(CONFIG_SMP)	+= -DTBX_PERCPU_SP_SAVE

ccflags-y		+= -mmetac=2.1

lib-y			+= tbicore.o
lib-y			+= tbictx.o
lib-y			+= tbidefr.o
lib-y			+= tbilogf.o
lib-y			+= tbipcx.o
lib-y			+= tbiroot.o
lib-y			+= tbisoft.o
lib-y			+= tbistring.o
lib-y			+= tbitimer.o

lib-$(CONFIG_METAG_DSP)	+= tbidspram.o
lib-$(CONFIG_METAG_FPU)	+= tbictxfpu.o
