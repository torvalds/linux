# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the linux parisc-specific parts of the boot image creator.
#

targets := image
targets += bzImage
subdir- := compressed

$(obj)/image: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/bzImage: $(if $(CONFIG_KERNEL_UNCOMPRESSED),$(objtree)/vmlinux,$(obj)/compressed/vmlinux) FORCE
	$(call if_changed,objcopy)

$(obj)/compressed/vmlinux: FORCE
	$(Q)$(MAKE) $(build)=$(obj)/compressed $@
