#
# Makefile for some libs needed by zImage.
#

CFLAGS_kbd.o	:= -Idrivers/char
CFLAGS_vreset.o := -I$(srctree)/arch/ppc/boot/include

zlib  := infblock.c infcodes.c inffast.c inflate.c inftrees.c infutil.c
	 
lib-y += $(zlib:.c=.o) div64.o
lib-$(CONFIG_VGA_CONSOLE) += vreset.o kbd.o


# zlib files needs header from their original place
EXTRA_CFLAGS += -Ilib/zlib_inflate

quiet_cmd_copy_zlib = COPY    $@
      cmd_copy_zlib = cat $< > $@

$(addprefix $(obj)/,$(zlib)): $(obj)/%: $(srctree)/lib/zlib_inflate/%
	$(call cmd,copy_zlib)

clean-files := $(zlib)
