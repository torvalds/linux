#
# Makefile for the nios2 linux kernel.
#

extra-y	+= head.o
extra-y	+= vmlinux.lds

obj-y	+= cpuinfo.o
obj-y	+= entry.o
obj-y	+= insnemu.o
obj-y	+= irq.o
obj-y	+= nios2_ksyms.o
obj-y	+= process.o
obj-y	+= prom.o
obj-y	+= ptrace.o
obj-y	+= setup.o
obj-y	+= signal.o
obj-y	+= sys_nios2.o
obj-y	+= syscall_table.o
obj-y	+= time.o
obj-y	+= traps.o

obj-$(CONFIG_MODULES)			+= module.o
obj-$(CONFIG_NIOS2_ALIGNMENT_TRAP)	+= misaligned.o
