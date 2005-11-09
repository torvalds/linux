#
# Makefile for the linux kernel.
#
ifneq ($(CONFIG_PPC_MERGE),y)

extra-$(CONFIG_PPC_STD_MMU)	:= head.o
extra-$(CONFIG_40x)		:= head_4xx.o
extra-$(CONFIG_44x)		:= head_44x.o
extra-$(CONFIG_FSL_BOOKE)	:= head_fsl_booke.o
extra-$(CONFIG_8xx)		:= head_8xx.o
extra-$(CONFIG_6xx)		+= idle_6xx.o
extra-$(CONFIG_POWER4)		+= idle_power4.o
extra-y				+= vmlinux.lds

obj-y				:= entry.o traps.o idle.o time.o misc.o \
					process.o align.o \
					setup.o \
					ppc_htab.o
obj-$(CONFIG_6xx)		+= l2cr.o cpu_setup_6xx.o
obj-$(CONFIG_SOFTWARE_SUSPEND)	+= swsusp.o
obj-$(CONFIG_POWER4)		+= cpu_setup_power4.o
obj-$(CONFIG_MODULES)		+= module.o ppc_ksyms.o
obj-$(CONFIG_NOT_COHERENT_CACHE)	+= dma-mapping.o
obj-$(CONFIG_PCI)		+= pci.o
obj-$(CONFIG_RAPIDIO)		+= rio.o
obj-$(CONFIG_KGDB)		+= ppc-stub.o
obj-$(CONFIG_SMP)		+= smp.o smp-tbsync.o
obj-$(CONFIG_TAU)		+= temp.o
ifndef CONFIG_E200
obj-$(CONFIG_FSL_BOOKE)		+= perfmon_fsl_booke.o
endif
obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel.o

ifndef CONFIG_MATH_EMULATION
obj-$(CONFIG_8xx)		+= softemu8xx.o
endif

# These are here while we do the architecture merge

else
obj-y				:= idle.o align.o
obj-$(CONFIG_6xx)		+= l2cr.o cpu_setup_6xx.o
obj-$(CONFIG_SOFTWARE_SUSPEND)	+= swsusp.o
obj-$(CONFIG_MODULES)		+= module.o
obj-$(CONFIG_NOT_COHERENT_CACHE)	+= dma-mapping.o
obj-$(CONFIG_PCI)		+= pci.o
obj-$(CONFIG_KGDB)		+= ppc-stub.o
obj-$(CONFIG_TAU)		+= temp.o
ifndef CONFIG_E200
obj-$(CONFIG_FSL_BOOKE)		+= perfmon_fsl_booke.o
endif
obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel.o
endif
