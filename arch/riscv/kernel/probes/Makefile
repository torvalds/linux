# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_KPROBES)		+= kprobes.o decode-insn.o simulate-insn.o
obj-$(CONFIG_KPROBES)		+= kprobes_trampoline.o
CFLAGS_REMOVE_simulate-insn.o = $(CC_FLAGS_FTRACE)
