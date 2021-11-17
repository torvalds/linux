# SPDX-License-Identifier: GPL-2.0
# arch/h8300/boot/Makefile

targets := vmlinux.srec vmlinux.bin zImage
subdir- := compressed

OBJCOPYFLAGS_vmlinux.srec := -Osrec
OBJCOPYFLAGS_vmlinux.bin  := -Obinary
OBJCOPYFLAGS_zImage := -O binary -R .note -R .comment -R .stab -R .stabstr -S

UIMAGE_LOADADDR = $(CONFIG_RAMBASE)
UIMAGE_ENTRYADDR = $(shell /bin/bash -c 'printf "0x%08x" \
	$$[$(CONFIG_RAMBASE) + $(CONFIG_OFFSET)]')

$(obj)/vmlinux.srec $(obj)/vmlinux.bin:  vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/zImage: $(obj)/compressed/vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/compressed/vmlinux: FORCE
	$(Q)$(MAKE) $(build)=$(obj)/compressed $@

$(obj)/uImage.bin: $(obj)/vmlinux.bin
	$(call if_changed,uimage,none)

CLEAN_FILES += arch/$(ARCH)/vmlinux.bin arch/$(ARCH)/vmlinux.srec arch/$(ARCH)/uImage.bin
