#
# Makefile for the linux kernel.
#

extra-y	:= head.o vmlinux.lds init_task.o

obj-y	:= setup.o idle.o or32_ksyms.o process.o dma.o \
	   traps.o time.o irq.o entry.o ptrace.o signal.o sys_or32.o \
	   sys_call_table.o

obj-$(CONFIG_MODULES)		+= module.o
obj-$(CONFIG_OF)		+= prom.o

clean:
