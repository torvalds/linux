# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_KPROBES)		+= kprobes.o decode-insn.o simulate-insn.o
obj-$(CONFIG_RETHOOK)		+= rethook.o rethook_trampoline.o
obj-$(CONFIG_UPROBES)		+= uprobes.o decode-insn.o simulate-insn.o
CFLAGS_REMOVE_simulate-insn.o = $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_rethook.o = $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_rethook_trampoline.o = $(CC_FLAGS_FTRACE)
