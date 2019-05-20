# SPDX-License-Identifier: GPL-2.0
#
# Makefile for some libs needed by zImage.
#

zlib	:= inffast.c inflate.c inftrees.c

lib-y	+= $(zlib:.c=.o) zmem.o

ccflags-y	:= -I $(srctree)/lib/zlib_inflate
ifdef CONFIG_FUNCTION_TRACER
CFLAGS_REMOVE_inflate.o = -pg
CFLAGS_REMOVE_zmem.o = -pg
CFLAGS_REMOVE_inftrees.o = -pg
CFLAGS_REMOVE_inffast.o = -pg
endif

KASAN_SANITIZE := n

CFLAGS_REMOVE_inflate.o += -fstack-protector -fstack-protector-strong
CFLAGS_REMOVE_zmem.o += -fstack-protector -fstack-protector-strong
CFLAGS_REMOVE_inftrees.o += -fstack-protector -fstack-protector-strong
CFLAGS_REMOVE_inffast.o += -fstack-protector -fstack-protector-strong

quiet_cmd_copy_zlib = COPY    $@
      cmd_copy_zlib = cat $< > $@

$(addprefix $(obj)/,$(zlib)): $(obj)/%: $(srctree)/lib/zlib_inflate/%
	$(call cmd,copy_zlib)

clean-files	:= $(zlib)
