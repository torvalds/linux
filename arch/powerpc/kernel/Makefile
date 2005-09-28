#
# Makefile for the linux kernel.
#

extra-$(CONFIG_PPC_STD_MMU)	:= head.o
extra_$(CONFIG_PPC64)		:= head_64.o
extra-$(CONFIG_40x)		:= head_4xx.o
extra-$(CONFIG_44x)		:= head_44x.o
extra-$(CONFIG_FSL_BOOKE)	:= head_fsl_booke.o
extra-$(CONFIG_8xx)		:= head_8xx.o
extra-$(CONFIG_6xx)		+= idle_6xx.o
extra-$(CONFIG_POWER4)		+= idle_power4.o
extra-$(CONFIG_PPC_FPU)		+= fpu.o
extra-y				+= vmlinux.lds

obj-y				:= semaphore.o traps.o process.o
obj-$(CONFIG_MODULES)		+= ppc_ksyms.o
obj-$(CONFIG_ALTIVEC)		+= vecemu.o vector.o
