#
# Makefile for the linux kernel.
#

ifeq ($(CONFIG_PPC64),y)
EXTRA_CFLAGS	+= -mno-minimal-toc
endif

extra-$(CONFIG_PPC_STD_MMU)	:= head.o
extra_$(CONFIG_PPC64)		:= head_64.o
extra-$(CONFIG_40x)		:= head_4xx.o
extra-$(CONFIG_44x)		:= head_44x.o
extra-$(CONFIG_FSL_BOOKE)	:= head_fsl_booke.o
extra-$(CONFIG_8xx)		:= head_8xx.o
extra-$(CONFIG_6xx)		+= idle_6xx.o
extra-$(CONFIG_PPC_FPU)		+= fpu.o
extra-y				+= vmlinux.lds

obj-y				:= traps.o
obj-$(CONFIG_PPC32)		+= semaphore.o process.o
obj-$(CONFIG_PPC64)		+= idle_power4.o
ifeq ($(CONFIG_PPC32),y)
obj-$(CONFIG_MODULES)		+= ppc_ksyms.o
endif
obj-$(CONFIG_ALTIVEC)		+= vecemu.o vector.o
