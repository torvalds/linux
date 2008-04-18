#
# Makefile for the linux kernel.
#

heads-y				:= head-uc-fr401.o head-uc-fr451.o head-uc-fr555.o
heads-$(CONFIG_MMU)		:= head-mmu-fr451.o

extra-y:= head.o init_task.o vmlinux.lds

obj-y := $(heads-y) entry.o entry-table.o break.o switch_to.o kernel_thread.o \
	 kernel_execve.o process.o traps.o ptrace.o signal.o dma.o \
	 sys_frv.o time.o setup.o frv_ksyms.o \
	 debug-stub.o irq.o sleep.o uaccess.o

obj-$(CONFIG_GDBSTUB)		+= gdb-stub.o gdb-io.o

obj-$(CONFIG_MB93091_VDK)	+= irq-mb93091.o
obj-$(CONFIG_PM)		+= pm.o cmode.o
obj-$(CONFIG_MB93093_PDK)	+= pm-mb93093.o
obj-$(CONFIG_FUJITSU_MB93493)	+= irq-mb93493.o
obj-$(CONFIG_SYSCTL)		+= sysctl.o
obj-$(CONFIG_FUTEX)		+= futex.o
obj-$(CONFIG_MODULES)		+= module.o
