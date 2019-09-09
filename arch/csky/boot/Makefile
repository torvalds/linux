# SPDX-License-Identifier: GPL-2.0-only
targets := Image zImage uImage
targets += $(dtb-y)

$(obj)/Image: vmlinux FORCE
	$(call if_changed,objcopy)
	@echo '  Kernel: $@ is ready'

compress-$(CONFIG_KERNEL_GZIP) = gzip
compress-$(CONFIG_KERNEL_LZO)  = lzo
compress-$(CONFIG_KERNEL_LZMA) = lzma
compress-$(CONFIG_KERNEL_XZ)   = xzkern
compress-$(CONFIG_KERNEL_LZ4)  = lz4

$(obj)/zImage:  $(obj)/Image FORCE
	$(call if_changed,$(compress-y))
	@echo '  Kernel: $@ is ready'

UIMAGE_ARCH		= sandbox
UIMAGE_COMPRESSION	= $(compress-y)
UIMAGE_LOADADDR		= $(shell $(NM) vmlinux | awk '$$NF == "_start" {print $$1}')

$(obj)/uImage: $(obj)/zImage
	$(call if_changed,uimage)
	@echo 'Image: $@ is ready'
