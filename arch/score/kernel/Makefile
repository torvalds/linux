#
# Makefile for the Linux/SCORE kernel.
#

extra-y	:= head.o vmlinux.lds

obj-y += entry.o init_task.o irq.o process.o ptrace.o \
	setup.o signal.o sys_score.o time.o traps.o

obj-$(CONFIG_MODULES) += module.o
