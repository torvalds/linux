#
# arch/arm64/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995-2001 by Russell King

LDFLAGS_vmlinux	:=-p --no-undefined -X
CPPFLAGS_vmlinux.lds = -DTEXT_OFFSET=$(TEXT_OFFSET)
OBJCOPYFLAGS	:=-O binary -R .note -R .note.gnu.build-id -R .comment -S
GZFLAGS		:=-9

LIBGCC 		:= $(shell $(CC) $(KBUILD_CFLAGS) -print-libgcc-file-name)

KBUILD_DEFCONFIG := defconfig

KBUILD_CFLAGS	+= -mgeneral-regs-only
KBUILD_CFLAGS	+= -fno-pic
ifeq ($(CONFIG_CPU_BIG_ENDIAN), y)
KBUILD_CPPFLAGS	+= -mbig-endian
AS		+= -EB
LD		+= -EB
else
KBUILD_CPPFLAGS	+= -mlittle-endian
AS		+= -EL
LD		+= -EL
endif

comma = ,

CHECKFLAGS	+= -D__aarch64__

# Default value
head-y		:= arch/arm64/kernel/head.o

# The byte offset of the kernel image in RAM from the start of RAM.
ifeq ($(CONFIG_ARM64_RANDOMIZE_TEXT_OFFSET), y)
TEXT_OFFSET := $(shell awk 'BEGIN {srand(); printf "0x%03x000\n", int(512 * rand())}')
else
TEXT_OFFSET := 0x00080000
endif

export	TEXT_OFFSET GZFLAGS

core-y		+= arch/arm64/kernel/ arch/arm64/mm/
core-$(CONFIG_ARCH_ROCKCHIP) += arch/arm64/mach-rockchip/
core-$(CONFIG_KVM) += arch/arm64/kvm/
core-$(CONFIG_CRYPTO) += arch/arm64/crypto/
libs-y		:= arch/arm64/lib/ $(libs-y)
libs-y		+= $(LIBGCC)

# Default target when executing plain make
ifeq ($(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE),y)
KBUILD_IMAGE	:= Image.gz-dtb
else
KBUILD_IMAGE	:= Image.gz
endif

KBUILD_DTBS	:= dtbs

all:	$(KBUILD_IMAGE) $(KBUILD_DTBS)

boot := arch/arm64/boot

Image Image.gz: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

zinstall install: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $@

%.dtb: scripts
	$(Q)$(MAKE) $(build)=$(boot)/dts $(boot)/dts/$@

dtbs: scripts
	$(Q)$(MAKE) $(build)=$(boot)/dts dtbs

Image.gz-dtb: vmlinux scripts dtbs
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

PHONY += vdso_install
vdso_install:
	$(Q)$(MAKE) $(build)=arch/arm64/kernel/vdso $@

# We use MRPROPER_FILES and CLEAN_FILES now
archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

define archhelp
  echo  '* Image.gz      - Compressed kernel image (arch/$(ARCH)/boot/Image.gz)'
  echo  '  Image         - Uncompressed kernel image (arch/$(ARCH)/boot/Image)'
  echo  '* dtbs          - Build device tree blobs for enabled boards'
  echo  '  install       - Install uncompressed kernel'
  echo  '  zinstall      - Install compressed kernel'
  echo  '                  Install using (your) ~/bin/installkernel or'
  echo  '                  (distribution) /sbin/installkernel or'
  echo  '                  install to $$(INSTALL_PATH) and run lilo'
endef

kernel.img: Image
	$(Q)$(srctree)/mkkrnlimg $(objtree)/arch/arm64/boot/Image $(objtree)/kernel.img >/dev/null
	@echo '  Image:  kernel.img is ready'

LOGO := $(notdir $(wildcard $(srctree)/logo.bmp))
LOGO_KERNEL := $(notdir $(wildcard $(srctree)/logo_kernel.bmp))
%.img: %.dtb kernel.img $(LOGO) $(LOGO_KERNEL)
	$(Q)$(srctree)/resource_tool $(objtree)/arch/arm64/boot/dts/$*.dtb $(LOGO) $(LOGO_KERNEL)
	@echo '  Image:  resource.img (with $*.dtb $(LOGO) $(LOGO_KERNEL)) is ready'
