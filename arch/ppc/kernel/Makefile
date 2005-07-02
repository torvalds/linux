#
# Makefile for the linux kernel.
#

extra-$(CONFIG_PPC_STD_MMU)	:= head.o
extra-$(CONFIG_40x)		:= head_4xx.o
extra-$(CONFIG_44x)		:= head_44x.o
extra-$(CONFIG_FSL_BOOKE)	:= head_fsl_booke.o
extra-$(CONFIG_8xx)		:= head_8xx.o
extra-$(CONFIG_6xx)		+= idle_6xx.o
extra-$(CONFIG_POWER4)		+= idle_power4.o
extra-$(CONFIG_PPC_FPU)		+= fpu.o
extra-y				+= vmlinux.lds

obj-y				:= entry.o traps.o irq.o idle.o time.o misc.o \
					process.o signal.o ptrace.o align.o \
					semaphore.o syscalls.o setup.o \
					cputable.o ppc_htab.o perfmon.o
obj-$(CONFIG_6xx)		+= l2cr.o cpu_setup_6xx.o
obj-$(CONFIG_SOFTWARE_SUSPEND)	+= swsusp.o
obj-$(CONFIG_POWER4)		+= cpu_setup_power4.o
obj-$(CONFIG_MODULES)		+= module.o ppc_ksyms.o
obj-$(CONFIG_NOT_COHERENT_CACHE)	+= dma-mapping.o
obj-$(CONFIG_PCI)		+= pci.o
obj-$(CONFIG_KGDB)		+= ppc-stub.o
obj-$(CONFIG_SMP)		+= smp.o smp-tbsync.o
obj-$(CONFIG_TAU)		+= temp.o
obj-$(CONFIG_ALTIVEC)		+= vecemu.o vector.o
ifndef CONFIG_E200
obj-$(CONFIG_FSL_BOOKE)		+= perfmon_fsl_booke.o
endif
obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel.o

ifndef CONFIG_MATH_EMULATION
obj-$(CONFIG_8xx)		+= softemu8xx.o
endif

