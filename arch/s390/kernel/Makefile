#
# Makefile for the linux kernel.
#

EXTRA_AFLAGS	:= -traditional

obj-y	:=  bitmap.o traps.o time.o process.o base.o early.o \
            setup.o sys_s390.o ptrace.o signal.o cpcmd.o ebcdic.o \
	    semaphore.o s390_ext.o debug.o irq.o ipl.o dis.o diag.o

obj-y	+= $(if $(CONFIG_64BIT),entry64.o,entry.o)
obj-y	+= $(if $(CONFIG_64BIT),reipl64.o,reipl.o)

extra-y				+= head.o init_task.o vmlinux.lds

obj-$(CONFIG_MODULES)		+= s390_ksyms.o module.o
obj-$(CONFIG_SMP)		+= smp.o

obj-$(CONFIG_AUDIT)		+= audit.o
compat-obj-$(CONFIG_AUDIT)	+= compat_audit.o
obj-$(CONFIG_COMPAT)		+= compat_linux.o compat_signal.o \
					compat_wrapper.o compat_exec_domain.o \
					binfmt_elf32.o $(compat-obj-y)

obj-$(CONFIG_VIRT_TIMER)	+= vtime.o
obj-$(CONFIG_STACKTRACE)	+= stacktrace.o
obj-$(CONFIG_KPROBES)		+= kprobes.o

# Kexec part
S390_KEXEC_OBJS := machine_kexec.o crash.o
S390_KEXEC_OBJS += $(if $(CONFIG_64BIT),relocate_kernel64.o,relocate_kernel.o)
obj-$(CONFIG_KEXEC) += $(S390_KEXEC_OBJS)

#
# This is just to get the dependencies...
#
binfmt_elf32.o:	$(TOPDIR)/fs/binfmt_elf.c
