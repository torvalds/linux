#
# arch/cris/boot/compressed/Makefile
#

# asflags-$(CONFIG_ETRAX_ARCH_V32) += -I$(srctree)/include/asm/mach \
# 				    -I$(srctree)/include/asm/arch
# ccflags-$(CONFIG_ETRAX_ARCH_V32) += -O2 -I$(srctree)/include/asm/mach
# 					-I$(srctree)/include/asm/arch

arch-$(CONFIG_ETRAX_ARCH_V10) = v10
arch-$(CONFIG_ETRAX_ARCH_V32) = v32

ldflags-y += -T $(srctree)/$(src)/decompress_$(arch-y).lds

OBJECTS-$(CONFIG_ETRAX_ARCH_V32) = $(obj)/head_v32.o
OBJECTS-$(CONFIG_ETRAX_ARCH_V10) = $(obj)/head_v10.o
OBJECTS= $(OBJECTS-y) $(obj)/misc.o
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
