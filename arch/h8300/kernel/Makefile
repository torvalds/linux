#
# Makefile for the linux kernel.
#

extra-y := vmlinux.lds

obj-y := process.o traps.o ptrace.o irq.o \
	 sys_h8300.o time.o signal.o \
         setup.o gpio.o init_task.o syscalls.o \
	 entry.o

obj-$(CONFIG_MODULES) += module.o h8300_ksyms.o 
