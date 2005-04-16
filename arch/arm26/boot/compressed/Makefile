#
# linux/arch/arm26/boot/compressed/Makefile
#
# create a compressed vmlinuz image from the original vmlinux
#
# Note! ZTEXTADDR, ZBSSADDR and ZRELADDR are now exported
# from arch/arm26/boot/Makefile
#

HEAD	= head.o
OBJS	= misc.o
FONTC	= drivers/video/console/font_acorn_8x8.c

OBJS		+= ll_char_wr.o font.o
CFLAGS_misc.o	:= -DPARAMS_PHYS=$(PARAMS_PHYS)

targets       := vmlinux vmlinux.lds piggy piggy.gz piggy.o font.o head.o $(OBJS)

SEDFLAGS	= s/TEXT_START/$(ZTEXTADDR)/;s/LOAD_ADDR/$(ZRELADDR)/;s/BSS_START/$(ZBSSADDR)/

EXTRA_CFLAGS  := $(CFLAGS_BOOT) -fpic
EXTRA_AFLAGS  := -traditional

LDFLAGS_vmlinux := -p -X \
	$(shell $(CC) $(CFLAGS)) -T

$(obj)/vmlinux: $(obj)/vmlinux.lds $(obj)/$(HEAD) $(obj)/piggy.o \
	 	$(addprefix $(obj)/, $(OBJS)) FORCE
	$(call if_changed,ld)
	@:


$(obj)/piggy: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/piggy.gz: $(obj)/piggy FORCE
	$(call if_changed,gzip)

LDFLAGS_piggy.o := -r -b binary
$(obj)/piggy.o:  $(obj)/piggy.gz FORCE
	$(call if_changed,ld)

$(obj)/font.o: $(FONTC)
	$(CC) $(CFLAGS) -Dstatic= -c $(FONTC) -o $(obj)/font.o

$(obj)/vmlinux.lds: $(obj)/vmlinux.lds.in Makefile arch/arm26/boot/Makefile .config
	@sed "$(SEDFLAGS)" < $< > $@

$(obj)/misc.o: $(obj)/misc.c $(obj)/uncompress.h lib/inflate.c

