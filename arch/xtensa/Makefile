#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2001 - 2005  Tensilica Inc.
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture

# Core configuration.
# (Use CPU=<xtensa_config> to use another default compiler.)

cpu-$(CONFIG_XTENSA_CPU_LINUX_BE)	:= linux_be
cpu-$(CONFIG_XTENSA_CPU_LINUX_CUSTOM)	:= linux_custom

CPU = $(cpu-y)
export CPU

# Platform configuration

platform-$(CONFIG_XTENSA_PLATFORM_XT2000)	:= xt2000
platform-$(CONFIG_XTENSA_PLATFORM_ISS)		:= iss

PLATFORM = $(platform-y)
export PLATFORM

CPPFLAGS	+= $(if $(KBUILD_SRC),-I$(srctree)/include/asm-xtensa/)
CPPFLAGS	+= -Iinclude/asm
CFLAGS		+= -pipe -mlongcalls

KBUILD_DEFCONFIG := iss_defconfig

# ramdisk/initrd support
# You need a compressed ramdisk image, named ramdisk.gz in
# arch/xtensa/boot/ramdisk

core-$(CONFIG_EMBEDDED_RAMDISK)	+= arch/xtensa/boot/ramdisk/

# Test for cross compiling

ifneq ($(CPU),)
  COMPILE_ARCH = $(shell uname -m)

  ifneq ($(COMPILE_ARCH), xtensa)
    ifndef CROSS_COMPILE
      CROSS_COMPILE = xtensa_$(CPU)-
    endif
  endif
endif

#

LIBGCC := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

head-y		:= arch/xtensa/kernel/head.o
core-y		+= arch/xtensa/kernel/ arch/xtensa/mm/
ifneq ($(PLATFORM),)
core-y		+= arch/xtensa/platform-$(PLATFORM)/
endif
libs-y		+= arch/xtensa/lib/ $(LIBGCC)

boot		:= arch/xtensa/boot

archinc		:= include/asm-xtensa

archprepare: $(archinc)/.platform

# Update machine cpu and platform symlinks if something which affects
# them changed.

$(archinc)/.platform: $(wildcard include/config/arch/*.h) include/config/MARKER
	@echo '  SYMLINK $(archinc)/xtensa/config -> $(archinc)/xtensa/config-$(CPU)'
	$(Q)mkdir -p $(archinc)
	$(Q)mkdir -p $(archinc)/xtensa
	$(Q)ln -fsn $(srctree)/$(archinc)/xtensa/config-$(CPU) $(archinc)/xtensa/config
	@echo '  SYMLINK $(archinc)/platform -> $(archinc)/platform-$(PLATFORM)'
	$(Q)ln -fsn $(srctree)/$(archinc)/platform-$(PLATFORM) $(archinc)/platform
	@touch $@


all: zImage

bzImage : zImage

zImage zImage.initrd: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $@

CLEAN_FILES	+= arch/xtensa/vmlinux.lds                      \
		   $(archinc)/platform $(archinc)/xtensa/config \
		   $(archinc)/.platform

define archhelp
  @echo '* zImage      - Compressed kernel image (arch/xtensa/boot/images/zImage.*)'
endef

