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

targets := Image zImage uImage

$(obj)/Image: vmlinux FORCE
	$(call if_changed,objcopy)
	@echo '  Kernel: $@ is ready'

$(obj)/compressed/vmlinux: $(obj)/Image FORCE
	$(Q)$(MAKE) $(build)=$(obj)/compressed $@

$(obj)/zImage: $(obj)/compressed/vmlinux FORCE
	$(call if_changed,objcopy)
	@echo '  Kernel: $@ is ready'

UIMAGE_ARCH = unicore
UIMAGE_LOADADDR = 0x0

$(obj)/uImage: $(obj)/zImage FORCE
	$(call if_changed,uimage)
	@echo '  Image $@ is ready'

PHONY += initrd FORCE
initrd:
	@test "$(INITRD)" != "" || \
	(echo You must specify INITRD; exit -1)

subdir- := compressed
