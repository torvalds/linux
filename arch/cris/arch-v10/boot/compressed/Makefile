#
# arch/cris/arch-v10/boot/compressed/Makefile
#

CC = gcc-cris -melf $(LINUXINCLUDE)
ccflags-y += -O2
LD = ld-cris
ldflags-y += -T $(obj)/decompress.ld
OBJECTS = $(obj)/head.o $(obj)/misc.o
OBJCOPY = objcopy-cris
OBJCOPYFLAGS = -O binary --remove-section=.bss

quiet_cmd_image = BUILD   $@
cmd_image = cat $(obj)/decompress.bin $(obj)/piggy.gz > $@

targets := vmlinux piggy.gz decompress.o decompress.bin

$(obj)/decompress.o: $(OBJECTS) FORCE
	$(call if_changed,ld)

$(obj)/decompress.bin: $(obj)/decompress.o FORCE
	$(call if_changed,objcopy)

$(obj)/head.o: $(obj)/head.S .config
	@$(CC) -D__ASSEMBLY__ -traditional -c $< -o $@

$(obj)/misc.o: $(obj)/misc.c .config
	@$(CC) -D__KERNEL__ -c $< -o $@

$(obj)/vmlinux: $(obj)/piggy.gz $(obj)/decompress.bin FORCE
	$(call if_changed,image)

$(obj)/piggy.gz: $(obj)/../Image FORCE
	$(call if_changed,gzip)

