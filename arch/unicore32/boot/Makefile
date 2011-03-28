#
# arch/unicore32/boot/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2001~2010 GUAN Xue-tao
#

MKIMAGE := $(srctree)/scripts/mkuboot.sh

targets := Image zImage uImage

$(obj)/Image: vmlinux FORCE
	$(call if_changed,objcopy)
	@echo '  Kernel: $@ is ready'

$(obj)/compressed/vmlinux: $(obj)/Image FORCE
	$(Q)$(MAKE) $(build)=$(obj)/compressed $@

$(obj)/zImage: $(obj)/compressed/vmlinux FORCE
	$(call if_changed,objcopy)
	@echo '  Kernel: $@ is ready'

quiet_cmd_uimage = UIMAGE  $@
      cmd_uimage = $(CONFIG_SHELL) $(MKIMAGE) -A unicore -O linux -T kernel \
		   -C none -a $(LOADADDR) -e $(STARTADDR) \
		   -n 'Linux-$(KERNELRELEASE)' -d $< $@

$(obj)/uImage: LOADADDR=0x0

$(obj)/uImage: STARTADDR=$(LOADADDR)

$(obj)/uImage: $(obj)/zImage FORCE
	$(call if_changed,uimage)
	@echo '  Image $@ is ready'

PHONY += initrd FORCE
initrd:
	@test "$(INITRD)" != "" || \
	(echo You must specify INITRD; exit -1)

subdir- := compressed
