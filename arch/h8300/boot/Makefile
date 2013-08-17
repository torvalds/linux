# arch/h8300/boot/Makefile

targets := vmlinux.srec vmlinux.bin zImage
subdir- := compressed

OBJCOPYFLAGS_vmlinux.srec := -Osrec
OBJCOPYFLAGS_vmlinux.bin  := -Obinary
OBJCOPYFLAGS_zImage := -O binary -R .note -R .comment -R .stab -R .stabstr -S

$(obj)/vmlinux.srec $(obj)/vmlinux.bin:  vmlinux FORCE
	$(call if_changed,objcopy)
	@echo '  Kernel: $@ is ready'

$(obj)/zImage: $(obj)/compressed/vmlinux FORCE
	$(call if_changed,objcopy)
	@echo 'Kernel: $@ is ready'

$(obj)/compressed/vmlinux: FORCE
	$(Q)$(MAKE) $(build)=$(obj)/compressed $@

CLEAN_FILES += arch/$(ARCH)/vmlinux.bin arch/$(ARCH)/vmlinux.srec

