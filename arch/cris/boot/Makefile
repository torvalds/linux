#
# arch/cris/boot/Makefile
#

objcopyflags-$(CONFIG_ETRAX_ARCH_V10) += -R .note -R .comment
objcopyflags-$(CONFIG_ETRAX_ARCH_V32) += --remove-section=.bss --remove-section=.note.gnu.build-id

OBJCOPYFLAGS = -O binary $(objcopyflags-y)


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
