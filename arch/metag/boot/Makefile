#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2007,2012  Imagination Technologies Ltd.
#

suffix-y := bin
suffix-$(CONFIG_KERNEL_GZIP)	:= gz
suffix-$(CONFIG_KERNEL_BZIP2)	:= bz2
suffix-$(CONFIG_KERNEL_XZ)	:= xz
suffix-$(CONFIG_KERNEL_LZO)	:= lzo

targets += vmlinux.bin
targets += uImage
targets += uImage.gz
targets += uImage.bz2
targets += uImage.xz
targets += uImage.lzo
targets += uImage.bin

extra-y += vmlinux.bin
extra-y += vmlinux.bin.gz
extra-y += vmlinux.bin.bz2
extra-y += vmlinux.bin.xz
extra-y += vmlinux.bin.lzo

UIMAGE_LOADADDR = $(CONFIG_PAGE_OFFSET)

ifeq ($(CONFIG_FUNCTION_TRACER),y)
orig_cflags := $(KBUILD_CFLAGS)
KBUILD_CFLAGS = $(subst -pg, , $(orig_cflags))
endif

$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/vmlinux.bin.gz: $(obj)/vmlinux.bin FORCE
	$(call if_changed,gzip)

$(obj)/vmlinux.bin.bz2: $(obj)/vmlinux.bin FORCE
	$(call if_changed,bzip2)

$(obj)/vmlinux.bin.xz: $(obj)/vmlinux.bin FORCE
	$(call if_changed,xzkern)

$(obj)/vmlinux.bin.lzo: $(obj)/vmlinux.bin FORCE
	$(call if_changed,lzo)

$(obj)/uImage.gz: $(obj)/vmlinux.bin.gz FORCE
	$(call if_changed,uimage,gzip)

$(obj)/uImage.bz2: $(obj)/vmlinux.bin.bz2 FORCE
	$(call if_changed,uimage,bzip2)

$(obj)/uImage.xz: $(obj)/vmlinux.bin.xz FORCE
	$(call if_changed,uimage,xz)

$(obj)/uImage.lzo: $(obj)/vmlinux.bin.lzo FORCE
	$(call if_changed,uimage,lzo)

$(obj)/uImage.bin: $(obj)/vmlinux.bin FORCE
	$(call if_changed,uimage,none)

$(obj)/uImage: $(obj)/uImage.$(suffix-y)
	@ln -sf $(notdir $<) $@
	@echo '  Image $@ is ready'
