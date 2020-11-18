# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the x86 low level entry code
#

KASAN_SANITIZE := n
UBSAN_SANITIZE := n
KCOV_INSTRUMENT := n

CFLAGS_REMOVE_common.o		= $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_syscall_64.o	= $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_syscall_32.o	= $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_syscall_x32.o	= $(CC_FLAGS_FTRACE)

CFLAGS_common.o			+= -fno-stack-protector
CFLAGS_syscall_64.o		+= -fno-stack-protector
CFLAGS_syscall_32.o		+= -fno-stack-protector
CFLAGS_syscall_x32.o		+= -fno-stack-protector

CFLAGS_syscall_64.o		+= $(call cc-option,-Wno-override-init,)
CFLAGS_syscall_32.o		+= $(call cc-option,-Wno-override-init,)
CFLAGS_syscall_x32.o		+= $(call cc-option,-Wno-override-init,)

obj-y				:= entry_$(BITS).o thunk_$(BITS).o syscall_$(BITS).o
obj-y				+= common.o

obj-y				+= vdso/
obj-y				+= vsyscall/

obj-$(CONFIG_IA32_EMULATION)	+= entry_64_compat.o syscall_32.o
obj-$(CONFIG_X86_X32_ABI)	+= syscall_x32.o

