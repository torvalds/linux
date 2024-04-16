# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_KPROBES)		+= kprobes.o decode-insn.o simulate-insn.o
obj-$(CONFIG_KPROBES)		+= kprobes_trampoline.o
obj-$(CONFIG_KPROBES_ON_FTRACE)	+= ftrace.o
obj-$(CONFIG_UPROBES)		+= uprobes.o decode-insn.o simulate-insn.o

CFLAGS_REMOVE_simulate-insn.o = $(CC_FLAGS_FTRACE)
