#
# Copyright (C) 2004-2006 Atmel Corporation
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

MKIMAGE		:= $(srctree)/scripts/mkuboot.sh

extra-y		:= vmlinux.bin vmlinux.gz

OBJCOPYFLAGS_vmlinux.bin := -O binary
$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/vmlinux.gz: $(obj)/vmlinux.bin FORCE
	$(call if_changed,gzip)

quiet_cmd_uimage = UIMAGE $@
      cmd_uimage = $(CONFIG_SHELL) $(MKIMAGE) -A avr32 -O linux -T kernel	\
		-C gzip -a $(CONFIG_LOAD_ADDRESS) -e $(CONFIG_ENTRY_ADDRESS)	\
		-n 'Linux-$(KERNELRELEASE)' -d $< $@

targets += uImage uImage.srec
$(obj)/uImage: $(obj)/vmlinux.gz
	$(call if_changed,uimage)
	@echo '  Image $@ is ready'

OBJCOPYFLAGS_uImage.srec := -I binary -O srec
$(obj)/uImage.srec: $(obj)/uImage
	$(call if_changed,objcopy)

OBJCOPYFLAGS_vmlinux.elf := --change-section-lma .text-0x80000000 \
			    --change-section-lma __ex_table-0x80000000 \
			    --change-section-lma .rodata-0x80000000 \
			    --change-section-lma .data-0x80000000 \
			    --change-section-lma .init-0x80000000 \
			    --change-section-lma .bss-0x80000000 \
			    --change-section-lma __param-0x80000000 \
			    --change-section-lma __ksymtab-0x80000000 \
			    --change-section-lma __ksymtab_gpl-0x80000000 \
			    --change-section-lma __kcrctab-0x80000000 \
			    --change-section-lma __kcrctab_gpl-0x80000000 \
			    --change-section-lma __ksymtab_strings-0x80000000 \
			    --set-start 0xa0000000
$(obj)/vmlinux.elf: vmlinux FORCE
	$(call if_changed,objcopy)

quiet_cmd_sfdwarf = SFDWARF $@
      cmd_sfdwarf = sfdwarf $< TO $@ GNUAVR IW $(SFDWARF_FLAGS) > $(obj)/sfdwarf.log

$(obj)/vmlinux.cso: $(obj)/vmlinux.elf FORCE
	$(call if_changed,sfdwarf)

install: $(BOOTIMAGE)
	sh $(srctree)/install-kernel.sh $<

# Generated files to be removed upon make clean
clean-files	:= vmlinux.elf vmlinux.bin vmlinux.gz uImage uImage.srec
