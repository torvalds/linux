#
# Makefile for the linux kernel.
#

EXTRA_AFLAGS	:= -traditional

obj-y	:=  bitmap.o traps.o time.o process.o \
            setup.o sys_s390.o ptrace.o signal.o cpcmd.o ebcdic.o \
            semaphore.o s390_ext.o debug.o profile.o irq.o

extra-$(CONFIG_ARCH_S390_31)	+= head.o 
extra-$(CONFIG_ARCH_S390X)	+= head64.o 
extra-y				+= init_task.o vmlinux.lds

obj-$(CONFIG_MODULES)		+= s390_ksyms.o module.o
obj-$(CONFIG_SMP)		+= smp.o

obj-$(CONFIG_S390_SUPPORT)	+= compat_linux.o compat_signal.o \
					compat_ioctl.o compat_wrapper.o \
					compat_exec_domain.o
obj-$(CONFIG_BINFMT_ELF32)	+= binfmt_elf32.o

obj-$(CONFIG_ARCH_S390_31)	+= entry.o reipl.o
obj-$(CONFIG_ARCH_S390X)	+= entry64.o reipl64.o

obj-$(CONFIG_VIRT_TIMER)	+= vtime.o

#
# This is just to get the dependencies...
#
binfmt_elf32.o:	$(TOPDIR)/fs/binfmt_elf.c
