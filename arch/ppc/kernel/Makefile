#
# Makefile for the linux kernel.
#
extra-$(CONFIG_PPC_STD_MMU)	:= head.o
extra-$(CONFIG_40x)		:= head_4xx.o
extra-$(CONFIG_44x)		:= head_44x.o
extra-$(CONFIG_8xx)		:= head_8xx.o
extra-y				+= vmlinux.lds

obj-y				:= entry.o traps.o time.o misc.o \
					setup.o \
					ppc_htab.o
obj-$(CONFIG_MODULES)		+= ppc_ksyms.o
obj-$(CONFIG_PCI)		+= pci.o
obj-$(CONFIG_KGDB)		+= ppc-stub.o
obj-$(CONFIG_SMP)		+= smp.o smp-tbsync.o
obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel.o

ifndef CONFIG_MATH_EMULATION
obj-$(CONFIG_8xx)		+= softemu8xx.o
endif
