# arch/h8300/boot/Makefile

targets := vmlinux.srec vmlinux.bin

OBJCOPYFLAGS_vmlinux.srec := -Osrec
OBJCOPYFLAGS_vmlinux.bin  := -Obinary

$(obj)/vmlinux.srec $(obj)/vmlinux.bin:  vmlinux FORCE
	$(call if_changed,objcopy)
	@echo '  Kernel: $@ is ready'

CLEAN_FILES += arch/$(ARCH)/vmlinux.bin arch/$(ARCH)/vmlinux.srec
