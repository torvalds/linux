# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux/SCORE kernel.
#

extra-y	:= head.o vmlinux.lds

obj-y += entry.o irq.o process.o ptrace.o \
	setup.o signal.o sys_score.o time.o traps.o \
	sys_call_table.o

obj-$(CONFIG_MODULES) += module.o
