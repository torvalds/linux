#
# Makefile for some libs needed by zImage.
#

zlib	:= infblock.c infcodes.c inffast.c inflate.c inftrees.c infutil.c

lib-y	+= $(zlib:.c=.o) zmem.o

EXTRA_CFLAGS	+= -Ilib/zlib_inflate

quiet_cmd_copy_zlib = COPY    $@
      cmd_copy_zlib = cat $< > $@

$(addprefix $(obj)/,$(zlib)): $(obj)/%: $(srctree)/lib/zlib_inflate/%
	$(call cmd,copy_zlib)

clean-files	:= $(zlib)
