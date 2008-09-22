# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
# Changes for PPC by Gary Thomas
# Rewritten by Cort Dougan and Paul Mackerras
#

HAS_BIARCH	:= $(call cc-option-yn, -m32)

# Set default 32 bits cross compilers for vdso and boot wrapper
CROSS32_COMPILE ?=

CROSS32CC		:= $(CROSS32_COMPILE)gcc
CROSS32AR		:= $(CROSS32_COMPILE)ar

ifeq ($(HAS_BIARCH),y)
ifeq ($(CROSS32_COMPILE),)
CROSS32CC	:= $(CC) -m32
CROSS32AR	:= GNUTARGET=elf32-powerpc $(AR)
endif
endif

export CROSS32CC CROSS32AR

ifeq ($(CROSS_COMPILE),)
KBUILD_DEFCONFIG := $(shell uname -m)_defconfig
else
KBUILD_DEFCONFIG := ppc64_defconfig
endif

ifeq ($(CONFIG_PPC64),y)
OLDARCH	:= ppc64

new_nm := $(shell if $(NM) --help 2>&1 | grep -- '--synthetic' > /dev/null; then echo y; else echo n; fi)

ifeq ($(new_nm),y)
NM		:= $(NM) --synthetic
endif

else
OLDARCH	:= ppc
endif

# It seems there are times we use this Makefile without
# including the config file, but this replicates the old behaviour
ifeq ($(CONFIG_WORD_SIZE),)
CONFIG_WORD_SIZE := 32
endif

UTS_MACHINE := $(OLDARCH)

ifeq ($(HAS_BIARCH),y)
override AS	+= -a$(CONFIG_WORD_SIZE)
override LD	+= -m elf$(CONFIG_WORD_SIZE)ppc
override CC	+= -m$(CONFIG_WORD_SIZE)
override AR	:= GNUTARGET=elf$(CONFIG_WORD_SIZE)-powerpc $(AR)
endif

LDFLAGS_vmlinux	:= -Bstatic

CFLAGS-$(CONFIG_PPC64)	:= -mminimal-toc -mtraceback=none  -mcall-aixdesc
CFLAGS-$(CONFIG_PPC32)	:= -ffixed-r2 -mmultiple
KBUILD_CPPFLAGS	+= -Iarch/$(ARCH)
KBUILD_AFLAGS	+= -Iarch/$(ARCH)
KBUILD_CFLAGS	+= -msoft-float -pipe -Iarch/$(ARCH) $(CFLAGS-y)
CPP		= $(CC) -E $(KBUILD_CFLAGS)

CHECKFLAGS	+= -m$(CONFIG_WORD_SIZE) -D__powerpc__ -D__powerpc$(CONFIG_WORD_SIZE)__

ifeq ($(CONFIG_PPC64),y)
GCC_BROKEN_VEC	:= $(shell if [ $(call cc-version) -lt 0400 ] ; then echo "y"; fi)

ifeq ($(CONFIG_POWER4_ONLY),y)
ifeq ($(CONFIG_ALTIVEC),y)
ifeq ($(GCC_BROKEN_VEC),y)
	KBUILD_CFLAGS += $(call cc-option,-mcpu=970)
else
	KBUILD_CFLAGS += $(call cc-option,-mcpu=power4)
endif
else
	KBUILD_CFLAGS += $(call cc-option,-mcpu=power4)
endif
else
	KBUILD_CFLAGS += $(call cc-option,-mtune=power4)
endif
else
LDFLAGS_MODULE	+= arch/powerpc/lib/crtsavres.o
endif

ifeq ($(CONFIG_TUNE_CELL),y)
	KBUILD_CFLAGS += $(call cc-option,-mtune=cell)
endif

# No AltiVec instruction when building kernel
KBUILD_CFLAGS += $(call cc-option,-mno-altivec)

# No SPE instruction when building kernel
KBUILD_CFLAGS += $(call cc-option,-mno-spe)

# Enable unit-at-a-time mode when possible. It shrinks the
# kernel considerably.
KBUILD_CFLAGS += $(call cc-option,-funit-at-a-time)

# Never use string load/store instructions as they are
# often slow when they are implemented at all
KBUILD_CFLAGS		+= -mno-string

ifeq ($(CONFIG_6xx),y)
KBUILD_CFLAGS		+= -mcpu=powerpc
endif

# Work around a gcc code-gen bug with -fno-omit-frame-pointer.
ifeq ($(CONFIG_FTRACE),y)
KBUILD_CFLAGS		+= -mno-sched-epilog
endif

cpu-as-$(CONFIG_4xx)		+= -Wa,-m405
cpu-as-$(CONFIG_6xx)		+= -Wa,-maltivec
cpu-as-$(CONFIG_POWER4)		+= -Wa,-maltivec
cpu-as-$(CONFIG_E500)		+= -Wa,-me500
cpu-as-$(CONFIG_E200)		+= -Wa,-me200

KBUILD_AFLAGS += $(cpu-as-y)
KBUILD_CFLAGS += $(cpu-as-y)

head-y				:= arch/powerpc/kernel/head_$(CONFIG_WORD_SIZE).o
head-$(CONFIG_8xx)		:= arch/powerpc/kernel/head_8xx.o
head-$(CONFIG_40x)		:= arch/powerpc/kernel/head_40x.o
head-$(CONFIG_44x)		:= arch/powerpc/kernel/head_44x.o
head-$(CONFIG_FSL_BOOKE)	:= arch/powerpc/kernel/head_fsl_booke.o

head-$(CONFIG_PPC64)		+= arch/powerpc/kernel/entry_64.o
head-$(CONFIG_PPC_FPU)		+= arch/powerpc/kernel/fpu.o

core-y				+= arch/powerpc/kernel/ \
				   arch/powerpc/mm/ \
				   arch/powerpc/lib/ \
				   arch/powerpc/sysdev/ \
				   arch/powerpc/platforms/
core-$(CONFIG_MATH_EMULATION)	+= arch/powerpc/math-emu/
core-$(CONFIG_XMON)		+= arch/powerpc/xmon/
core-$(CONFIG_KVM) 		+= arch/powerpc/kvm/

drivers-$(CONFIG_OPROFILE)	+= arch/powerpc/oprofile/

# Default to zImage, override when needed
all: zImage

CPPFLAGS_vmlinux.lds	:= -Upowerpc

BOOT_TARGETS = zImage zImage.initrd uImage zImage% dtbImage% treeImage.% cuImage.% simpleImage.%

PHONY += $(BOOT_TARGETS)

boot := arch/$(ARCH)/boot

$(BOOT_TARGETS): vmlinux
	$(Q)$(MAKE) ARCH=ppc64 $(build)=$(boot) $(patsubst %,$(boot)/%,$@)

bootwrapper_install %.dtb:
	$(Q)$(MAKE) ARCH=ppc64 $(build)=$(boot) $(patsubst %,$(boot)/%,$@)

define archhelp
  @echo '* zImage          - Build default images selected by kernel config'
  @echo '  zImage.*        - Compressed kernel image (arch/$(ARCH)/boot/zImage.*)'
  @echo '  uImage          - U-Boot native image format'
  @echo '  cuImage.<dt>    - Backwards compatible U-Boot image for older'
  @echo '                    versions which do not support device trees'
  @echo '  dtbImage.<dt>   - zImage with an embedded device tree blob'
  @echo '  simpleImage.<dt> - Firmware independent image.'
  @echo '  treeImage.<dt>  - Support for older IBM 4xx firmware (not U-Boot)'
  @echo '  install         - Install kernel using'
  @echo '                    (your) ~/bin/installkernel or'
  @echo '                    (distribution) /sbin/installkernel or'
  @echo '                    install to $$(INSTALL_PATH) and run lilo'
  @echo '  *_defconfig     - Select default config from arch/$(ARCH)/configs'
  @echo ''
  @echo '  Targets with <dt> embed a device tree blob inside the image'
  @echo '  These targets support board with firmware that does not'
  @echo '  support passing a device tree directly.  Replace <dt> with the'
  @echo '  name of a dts file from the arch/$(ARCH)/boot/dts/ directory'
  @echo '  (minus the .dts extension).'
endef

install:
	$(Q)$(MAKE) $(build)=$(boot) install

vdso_install:
ifeq ($(CONFIG_PPC64),y)
	$(Q)$(MAKE) $(build)=arch/$(ARCH)/kernel/vdso64 $@
endif
	$(Q)$(MAKE) $(build)=arch/$(ARCH)/kernel/vdso32 $@

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

archprepare: checkbin

# Use the file '.tmp_gas_check' for binutils tests, as gas won't output
# to stdout and these checks are run even on install targets.
TOUT	:= .tmp_gas_check
# Ensure this is binutils 2.12.1 (or 2.12.90.0.7) or later for altivec
# instructions.
# gcc-3.4 and binutils-2.14 are a fatal combination.

checkbin:
	@if test "$(call cc-version)" = "0304" ; then \
		if ! /bin/echo mftb 5 | $(AS) -v -mppc -many -o $(TOUT) >/dev/null 2>&1 ; then \
			echo -n '*** ${VERSION}.${PATCHLEVEL} kernels no longer build '; \
			echo 'correctly with gcc-3.4 and your version of binutils.'; \
			echo '*** Please upgrade your binutils or downgrade your gcc'; \
			false; \
		fi ; \
	fi
	@if test "$(call cc-fullversion)" = "040200" \
	    && test "x${CONFIG_MODULES}${CONFIG_PPC64}" = "xyy" ; then \
		echo -n '*** GCC-4.2.0 cannot compile the 64-bit powerpc ' ; \
		echo 'kernel with modules enabled.' ; \
		echo -n '*** Please use a different GCC version or ' ; \
		echo 'disable kernel modules' ; \
		false ; \
	fi
	@if ! /bin/echo dssall | $(AS) -many -o $(TOUT) >/dev/null 2>&1 ; then \
		echo -n '*** ${VERSION}.${PATCHLEVEL} kernels no longer build ' ; \
		echo 'correctly with old versions of binutils.' ; \
		echo '*** Please upgrade your binutils to 2.12.1 or newer' ; \
		false ; \
	fi

CLEAN_FILES += $(TOUT)

