# 
# Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

obj-y = aio.o elf_aux.o file.o helper.o main.o mem.o process.o signal.o \
	start_up.o time.o trap.o tt.o tty.o uaccess.o umid.o user_syms.o \
	util.o drivers/ sys-$(SUBARCH)/

obj-$(CONFIG_MODE_SKAS) += skas/

USER_OBJS := aio.o elf_aux.o file.o helper.o main.o mem.o process.o signal.o \
	start_up.o time.o trap.o tt.o tty.o uaccess.o umid.o util.o

elf_aux.o: $(ARCH_DIR)/kernel-offsets.h
CFLAGS_elf_aux.o += -I$(objtree)/arch/um

CFLAGS_user_syms.o += -DSUBARCH_$(SUBARCH)

HAVE_AIO_ABI := $(shell [ -r /usr/include/linux/aio_abi.h ] && \
	echo -DHAVE_AIO_ABI )
CFLAGS_aio.o += $(HAVE_AIO_ABI)

include arch/um/scripts/Makefile.rules
