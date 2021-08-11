#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2001 - 2005  Tensilica Inc.
# Copyright (C) 2014 Cadence Design Systems Inc.
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture

# Core configuration.
# (Use VAR=<xtensa_config> to use another default compiler.)

variant-y := $(patsubst "%",%,$(CONFIG_XTENSA_VARIANT_NAME))

VARIANT = $(variant-y)

ifneq ($(VARIANT),)
  ifdef cross_compiling
    ifndef CROSS_COMPILE
      CROSS_COMPILE = xtensa_$(VARIANT)-
    endif
  endif
endif

# Platform configuration

platform-$(CONFIG_XTENSA_PLATFORM_XT2000)	:= xt2000
platform-$(CONFIG_XTENSA_PLATFORM_ISS)		:= iss
platform-$(CONFIG_XTENSA_PLATFORM_XTFPGA)	:= xtfpga

# temporarily until string.h is fixed
KBUILD_CFLAGS += -ffreestanding -D__linux__
KBUILD_CFLAGS += -pipe -mlongcalls -mtext-section-literals
KBUILD_CFLAGS += $(call cc-option,-mforce-no-pic,)
KBUILD_CFLAGS += $(call cc-option,-mno-serialize-volatile,)

KBUILD_AFLAGS += -mlongcalls -mtext-section-literals

ifneq ($(CONFIG_LD_NO_RELAX),)
KBUILD_LDFLAGS := --no-relax
endif

CHECKFLAGS += -D$(if $(CONFIG_CPU_BIG_ENDIAN),__XTENSA_EB__,__XTENSA_EL__)

vardirs := $(patsubst %,arch/xtensa/variants/%/,$(variant-y))
plfdirs := $(patsubst %,arch/xtensa/platforms/%/,$(platform-y))

KBUILD_CPPFLAGS += $(patsubst %,-I$(srctree)/%include,$(vardirs) $(plfdirs))

KBUILD_DEFCONFIG := iss_defconfig

# Find libgcc.a

LIBGCC := $(shell $(CC) $(KBUILD_CFLAGS) -print-libgcc-file-name)

head-y		:= arch/xtensa/kernel/head.o

libs-y		+= arch/xtensa/lib/ $(LIBGCC)

boot		:= arch/xtensa/boot

all Image zImage uImage xipImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $@

archheaders:
	$(Q)$(MAKE) $(build)=arch/xtensa/kernel/syscalls all

define archhelp
  @echo '* Image       - Kernel ELF image with reset vector'
  @echo '* zImage      - Compressed kernel image (arch/xtensa/boot/images/zImage.*)'
  @echo '* uImage      - U-Boot wrapped image'
  @echo '  xipImage    - XIP image'
endef
