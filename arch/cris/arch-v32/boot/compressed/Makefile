#
# arch/cris/arch-v32/boot/compressed/Makefile
#

asflags-y += -I $(srctree)/include/asm/mach/ -I $(srctree)/include/asm/arch
ccflags-y += -O2 -I $(srctree)/include/asm/mach/ -I $(srctree)/include/asm/arch
ldflags-y += -T $(srctree)/$(obj)/decompress.ld
OBJECTS = $(obj)/head.o $(obj)/misc.o
OBJCOPYFLAGS = -O binary --remove-section=.bss

quiet_cmd_image = BUILD   $@
cmd_image = cat $(obj)/decompress.bin $(obj)/piggy.gz > $@

targets := vmlinux piggy.gz decompress.o decompress.bin

$(obj)/decompress.o: $(OBJECTS) FORCE
	$(call if_changed,ld)

$(obj)/decompress.bin: $(obj)/decompress.o FORCE
	$(call if_changed,objcopy)

$(obj)/vmlinux: $(obj)/piggy.gz $(obj)/decompress.bin FORCE
	$(call if_changed,image)

$(obj)/piggy.gz: $(obj)/../Image FORCE
	$(call if_changed,gzip)
