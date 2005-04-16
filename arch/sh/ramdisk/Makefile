#
# Makefile for a ramdisk image
#

obj-y += ramdisk.o


O_FORMAT = $(shell $(OBJDUMP) -i | head -n 2 | grep elf32)
img := $(subst ",,$(CONFIG_EMBEDDED_RAMDISK_IMAGE))
# add $(src) when $(img) is relative
img := $(subst $(src)//,/,$(src)/$(img))

quiet_cmd_ramdisk = LD      $@
define cmd_ramdisk
	$(LD) -T $(srctree)/$(src)/ld.script -b binary --oformat $(O_FORMAT) \
		-o $@ $(img)
endef

$(obj)/ramdisk.o: $(img) $(srctree)/$(src)/ld.script
	$(call cmd,ramdisk)
