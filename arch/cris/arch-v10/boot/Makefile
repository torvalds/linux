#
# arch/cris/arch-v10/boot/Makefile
#

OBJCOPY = objcopy-cris
OBJCOPYFLAGS = -O binary --remove-section=.bss

subdir- := compressed rescue
targets := Image

$(obj)/Image: vmlinux FORCE
	$(call if_changed,objcopy)
	@echo '  Kernel: $@ is ready'

$(obj)/compressed/vmlinux: $(obj)/Image FORCE
	$(Q)$(MAKE) $(build)=$(obj)/compressed $@
	$(Q)$(MAKE) $(build)=$(obj)/rescue $(obj)/rescue/rescue.bin

$(obj)/zImage:  $(obj)/compressed/vmlinux
	@cp $< $@
	@echo '  Kernel: $@ is ready'
