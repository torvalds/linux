#
# This dir holds all of the images for PPC machines.
# Tom Rini	January 2001

MKIMAGE		:= $(srctree)/scripts/mkuboot.sh

extra-y		:= vmlinux.bin vmlinux.gz

OBJCOPYFLAGS_vmlinux.bin := -O binary
$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/vmlinux.gz: $(obj)/vmlinux.bin FORCE
	$(call if_changed,gzip)

quiet_cmd_uimage = UIMAGE  $@
      cmd_uimage = $(CONFIG_SHELL) $(MKIMAGE) -A ppc -O linux -T kernel \
               -C gzip -a 00000000 -e 00000000 -n 'Linux-$(KERNELRELEASE)' \
               -d $< $@

targets += uImage
$(obj)/uImage: $(obj)/vmlinux.gz
	$(call if_changed,uimage)
	@echo '  Image $@ is ready'

# Files generated that shall be removed upon make clean
clean-files	:= sImage vmapus vmlinux* miboot* zImage* uImage
