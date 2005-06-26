#
# Makefile for a ramdisk image
#

BIG_ENDIAN	:= $(shell echo -e "\#ifdef __XTENSA_EL__\nint little;\n\#else\nint big;\n\#endif" | $(CC) -E -|grep -c big)

ifeq ($(BIG_ENDIAN),1)
OBJCOPY_ARGS    := -O elf32-xtensa-be
else
OBJCOPY_ARGS    := -O elf32-xtensa-le
endif

obj-y = ramdisk.o

RAMDISK_IMAGE = arch/$(ARCH)/boot/ramdisk/$(CONFIG_EMBEDDED_RAMDISK_IMAGE)

arch/$(ARCH)/boot/ramdisk/ramdisk.o:
	$(Q)echo -e "dummy:" | $(AS) -o $@;
	$(Q)$(OBJCOPY) $(OBJCOPY_ARGS)					    \
		--add-section .initrd=$(RAMDISK_IMAGE)			    \
		--set-section-flags .initrd=contents,alloc,load,load,data   \
		arch/$(ARCH)/boot/ramdisk/ramdisk.o $@

