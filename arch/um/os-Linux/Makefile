# SPDX-License-Identifier: GPL-2.0
# 
# Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
#

# Don't instrument UML-specific code
KCOV_INSTRUMENT                := n

obj-y = execvp.o file.o helper.o irq.o main.o mem.o process.o \
	registers.o sigio.o signal.o start_up.o time.o tty.o \
	umid.o user_syms.o util.o skas/

CFLAGS_signal.o += -Wframe-larger-than=4096

CFLAGS_main.o += -Wno-frame-larger-than

obj-$(CONFIG_ARCH_REUSE_HOST_VSYSCALL_AREA) += elf_aux.o

USER_OBJS := $(user-objs-y) elf_aux.o execvp.o file.o helper.o irq.o \
	main.o mem.o process.o registers.o sigio.o signal.o start_up.o time.o \
	tty.o umid.o util.o

include $(srctree)/arch/um/scripts/Makefile.rules
