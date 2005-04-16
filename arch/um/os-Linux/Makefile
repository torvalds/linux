# 
# Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

obj-y = elf_aux.o file.o process.o signal.o time.o tty.o user_syms.o drivers/ \
	sys-$(SUBARCH)/

USER_OBJS := elf_aux.o file.o process.o signal.o time.o tty.o

CFLAGS_user_syms.o += -DSUBARCH_$(SUBARCH)

include arch/um/scripts/Makefile.rules
