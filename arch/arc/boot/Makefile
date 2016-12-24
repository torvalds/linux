targets := vmlinux.bin vmlinux.bin.gz uImage

# uImage build relies on mkimage being availble on your host for ARC target
# You will need to build u-boot for ARC, rename mkimage to arc-elf32-mkimage
# and make sure it's reacable from your PATH

OBJCOPYFLAGS= -O binary -R .note -R .note.gnu.build-id -R .comment -S

LINUX_START_TEXT = $$(readelf -h vmlinux | \
			grep "Entry point address" | grep -o 0x.*)

UIMAGE_LOADADDR    = $(CONFIG_LINUX_LINK_BASE)
UIMAGE_ENTRYADDR   = $(LINUX_START_TEXT)

suffix-y := bin
suffix-$(CONFIG_KERNEL_GZIP)	:= gz
suffix-$(CONFIG_KERNEL_LZMA)	:= lzma

targets += uImage
targets += uImage.bin
targets += uImage.gz
targets += uImage.lzma
extra-y += vmlinux.bin
extra-y += vmlinux.bin.gz
extra-y += vmlinux.bin.lzma

$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/vmlinux.bin.gz: $(obj)/vmlinux.bin FORCE
	$(call if_changed,gzip)

$(obj)/vmlinux.bin.lzma: $(obj)/vmlinux.bin FORCE
	$(call if_changed,lzma)

$(obj)/uImage.bin: $(obj)/vmlinux.bin FORCE
	$(call if_changed,uimage,none)

$(obj)/uImage.gz: $(obj)/vmlinux.bin.gz FORCE
	$(call if_changed,uimage,gzip)

$(obj)/uImage.lzma: $(obj)/vmlinux.bin.lzma FORCE
	$(call if_changed,uimage,lzma)

$(obj)/uImage: $(obj)/uImage.$(suffix-y)
	@ln -sf $(notdir $<) $@
	@echo '  Image $@ is ready'
