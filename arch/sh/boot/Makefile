#
# arch/sh/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1999 Stuart Menefy
#

MKIMAGE := $(srctree)/scripts/mkuboot.sh

#
# Assign safe dummy values if these variables are not defined,
# in order to suppress error message.
#
CONFIG_PAGE_OFFSET	?= 0x80000000
CONFIG_MEMORY_START	?= 0x0c000000
CONFIG_BOOT_LINK_OFFSET	?= 0x00800000
CONFIG_ZERO_PAGE_OFFSET	?= 0x00001000
CONFIG_ENTRY_OFFSET	?= 0x00001000

suffix-$(CONFIG_KERNEL_GZIP)  := gz
suffix-$(CONFIG_KERNEL_BZIP2) := bz2
suffix-$(CONFIG_KERNEL_LZMA)  := lzma

targets := zImage vmlinux.srec romImage uImage uImage.srec uImage.gz uImage.bz2 uImage.lzma
extra-y += vmlinux.bin vmlinux.bin.gz vmlinux.bin.bz2 vmlinux.bin.lzma
subdir- := compressed romimage

$(obj)/zImage: $(obj)/compressed/vmlinux FORCE
	$(call if_changed,objcopy)
	@echo '  Kernel: $@ is ready'

$(obj)/compressed/vmlinux: FORCE
	$(Q)$(MAKE) $(build)=$(obj)/compressed $@

$(obj)/romImage: $(obj)/romimage/vmlinux FORCE
	$(call if_changed,objcopy)
	@echo '  Kernel: $@ is ready'

$(obj)/romimage/vmlinux: $(obj)/zImage FORCE
	$(Q)$(MAKE) $(build)=$(obj)/romimage $@

KERNEL_MEMORY := 0x00000000
ifeq ($(CONFIG_PMB_FIXED),y)
KERNEL_MEMORY := $(shell /bin/bash -c 'printf "0x%08x" \
		     $$[$(CONFIG_MEMORY_START) & 0x1fffffff]')
endif
ifeq ($(CONFIG_29BIT),y)
KERNEL_MEMORY := $(shell /bin/bash -c 'printf "0x%08x" \
		     $$[$(CONFIG_MEMORY_START)]')
endif

KERNEL_LOAD	:= $(shell /bin/bash -c 'printf "0x%08x" \
		     $$[$(CONFIG_PAGE_OFFSET)  + \
			$(KERNEL_MEMORY) + \
			$(CONFIG_ZERO_PAGE_OFFSET)]')

KERNEL_ENTRY	:= $(shell /bin/bash -c 'printf "0x%08x" \
		     $$[$(CONFIG_PAGE_OFFSET)  + \
			$(KERNEL_MEMORY) + \
			$(CONFIG_ZERO_PAGE_OFFSET) + $(CONFIG_ENTRY_OFFSET)]')

quiet_cmd_uimage = UIMAGE  $@
      cmd_uimage = $(CONFIG_SHELL) $(MKIMAGE) -A sh -O linux -T kernel \
		   -C $(2) -a $(KERNEL_LOAD) -e $(KERNEL_ENTRY) \
		   -n 'Linux-$(KERNELRELEASE)' -d $< $@

$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/vmlinux.bin.gz: $(obj)/vmlinux.bin FORCE
	$(call if_changed,gzip)

$(obj)/vmlinux.bin.bz2: $(obj)/vmlinux.bin FORCE
	$(call if_changed,bzip2)

$(obj)/vmlinux.bin.lzma: $(obj)/vmlinux.bin FORCE
	$(call if_changed,lzma)

$(obj)/uImage.bz2: $(obj)/vmlinux.bin.bz2
	$(call if_changed,uimage,bzip2)

$(obj)/uImage.gz: $(obj)/vmlinux.bin.gz
	$(call if_changed,uimage,gzip)

$(obj)/uImage.lzma: $(obj)/vmlinux.bin.lzma
	$(call if_changed,uimage,lzma)

OBJCOPYFLAGS_vmlinux.srec := -I binary -O srec
$(obj)/vmlinux.srec: $(obj)/compressed/vmlinux
	$(call if_changed,objcopy)

OBJCOPYFLAGS_uImage.srec := -I binary -O srec
$(obj)/uImage.srec: $(obj)/uImage
	$(call if_changed,objcopy)

$(obj)/uImage: $(obj)/uImage.$(suffix-y)
	@ln -sf $(notdir $<) $@
	@echo '  Image $@ is ready'

export CONFIG_PAGE_OFFSET CONFIG_MEMORY_START CONFIG_BOOT_LINK_OFFSET \
       CONFIG_ZERO_PAGE_OFFSET CONFIG_ENTRY_OFFSET KERNEL_MEMORY suffix-y
