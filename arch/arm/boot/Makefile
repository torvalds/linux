#
# arch/arm/boot/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995-2002 Russell King
#

ifneq ($(MACHINE),)
include $(srctree)/$(MACHINE)/Makefile.boot
endif

# Note: the following conditions must always be true:
#   ZRELADDR == virt_to_phys(PAGE_OFFSET + TEXT_OFFSET)
#   PARAMS_PHYS must be within 4MB of ZRELADDR
#   INITRD_PHYS must be in RAM
ZRELADDR    := $(zreladdr-y)
PARAMS_PHYS := $(params_phys-y)
INITRD_PHYS := $(initrd_phys-y)

ifeq ($(CONFIG_KERNEL_LZMA),y)
UIMAGE_COMPRESSION ?= lzma
kcomp=lzma
karg="-z"
else ifeq ($(CONFIG_KERNEL_LZO),y)
UIMAGE_COMPRESSION ?= lzo
kcomp=lzop
karg="-9"
else ifeq ($(CONFIG_KERNEL_GZIP),y)
UIMAGE_COMPRESSION ?= gzip
kcomp=gzip
karg="-S.gzip"
else ifeq ($(CONFIG_KERNEL_XZ),y)
UIMAGE_COMPRESSION ?= xz
kcomp=xz
karg="-z"
endif

export ZRELADDR INITRD_PHYS PARAMS_PHYS UIMAGE_COMPRESSION

targets := Image zImage xipImage bootpImage uImage

DTB_NAMES := $(subst $\",,$(CONFIG_BUILD_ARM_APPENDED_DTB_IMAGE_NAMES))
ifneq ($(DTB_NAMES),)
DTB_LIST := $(addsuffix .dtb,$(DTB_NAMES))
else
DTB_LIST := $(dtb-y)
endif
DTB_OBJS := $(addprefix $(obj)/dts/,$(DTB_LIST))

ifeq ($(CONFIG_XIP_KERNEL),y)

$(obj)/xipImage: vmlinux FORCE
	$(call if_changed,objcopy)
	@$(kecho) '  Kernel: $@ is ready (physical address: $(CONFIG_XIP_PHYS_ADDR))'

$(obj)/Image $(obj)/zImage: FORCE
	@echo 'Kernel configured for XIP (CONFIG_XIP_KERNEL=y)'
	@echo 'Only the xipImage target is available in this case'
	@false

else

$(obj)/xipImage: FORCE
	@echo 'Kernel not configured for XIP (CONFIG_XIP_KERNEL!=y)'
	@false

$(obj)/Image: vmlinux FORCE
	$(call if_changed,objcopy)
	@$(kecho) '  Kernel: $@ is ready'

$(obj)/compressed/vmlinux: $(obj)/Image FORCE
	$(Q)$(MAKE) $(build)=$(obj)/compressed $@

$(obj)/zImage:	$(obj)/compressed/vmlinux FORCE
	$(call if_changed,objcopy)
	@$(kecho) '  Kernel: $@ is ready'

$(obj)/zImage-dtb:	$(obj)/zImage $(DTB_OBJS) FORCE
	$(call if_changed,cat)
	@echo '  Kernel: $@ is ready'

endif

ifneq ($(LOADADDR),)
  UIMAGE_LOADADDR=$(LOADADDR)
else
  ifeq ($(CONFIG_ZBOOT_ROM),y)
    UIMAGE_LOADADDR=$(CONFIG_ZBOOT_ROM_TEXT)
  else
    UIMAGE_LOADADDR=$(ZRELADDR)
  endif
endif

check_for_multiple_loadaddr = \
if [ $(words $(UIMAGE_LOADADDR)) -ne 1 ]; then \
	echo 'multiple (or no) load addresses: $(UIMAGE_LOADADDR)'; \
	echo 'This is incompatible with uImages'; \
	echo 'Specify LOADADDR on the commandline to build an uImage'; \
	false; \
fi

#get uncompressed uImage from 'make UIMAGE_COMPRESSION=none uImage'
CIMAGE_NAME=ccImage
ifeq ($(UIMAGE_COMPRESSION), none)
	F_CIMAGE_NAME=$(CIMAGE_NAME)
else
	C_CIMAGE_NAME=ccImage.$(UIMAGE_COMPRESSION)
	F_CIMAGE_NAME=$(C_CIMAGE_NAME)
endif

$(obj)/$(CIMAGE_NAME): vmlinux FORCE
	$(call if_changed,objcopy)
	@echo '  Kernel: $@ is ready'

$(obj)/$(C_CIMAGE_NAME):$(obj)/$(CIMAGE_NAME) FORCE
	@-rm -rf $(obj)/$(C_CIMAGE_NAME)
	@$(kcomp) $(karg) $(obj)/$(CIMAGE_NAME)
	@echo '  Image $@ is ready'

$(obj)/uImage:	$(obj)/$(F_CIMAGE_NAME) FORCE
	@$(check_for_multiple_loadaddr)
	$(call if_changed,uimage)
	@$(kecho) '  Image $@ is ready'

$(obj)/bootp/bootp: $(obj)/zImage initrd FORCE
	$(Q)$(MAKE) $(build)=$(obj)/bootp $@
	@:

$(obj)/bootpImage: $(obj)/bootp/bootp FORCE
	$(call if_changed,objcopy)
	@$(kecho) '  Kernel: $@ is ready'

PHONY += initrd FORCE
initrd:
	@test "$(INITRD_PHYS)" != "" || \
	(echo This machine does not support INITRD; exit -1)
	@test "$(INITRD)" != "" || \
	(echo You must specify INITRD; exit -1)

install: $(obj)/Image
	$(CONFIG_SHELL) $(srctree)/$(src)/install.sh $(KERNELRELEASE) \
	$(obj)/Image System.map "$(INSTALL_PATH)"

zinstall: $(obj)/zImage
	$(CONFIG_SHELL) $(srctree)/$(src)/install.sh $(KERNELRELEASE) \
	$(obj)/zImage System.map "$(INSTALL_PATH)"

uinstall: $(obj)/uImage
	$(CONFIG_SHELL) $(srctree)/$(src)/install.sh $(KERNELRELEASE) \
	$(obj)/uImage System.map "$(INSTALL_PATH)"

zi:
	$(CONFIG_SHELL) $(srctree)/$(src)/install.sh $(KERNELRELEASE) \
	$(obj)/zImage System.map "$(INSTALL_PATH)"

i:
	$(CONFIG_SHELL) $(srctree)/$(src)/install.sh $(KERNELRELEASE) \
	$(obj)/Image System.map "$(INSTALL_PATH)"

subdir-	    := bootp compressed dts
