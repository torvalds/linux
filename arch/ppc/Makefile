# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
# Changes for PPC by Gary Thomas
# Rewritten by Cort Dougan and Paul Mackerras
#

# This must match PAGE_OFFSET in include/asm-ppc/page.h.
KERNELLOAD	:= $(CONFIG_KERNEL_START)

HAS_BIARCH	:= $(call cc-option-yn, -m32)
ifeq ($(HAS_BIARCH),y)
AS		:= $(AS) -a32
LD		:= $(LD) -m elf32ppc
CC		:= $(CC) -m32
endif

LDFLAGS_vmlinux	:= -Ttext $(KERNELLOAD) -Bstatic
# The -Iarch/$(ARCH)/include is temporary while we are merging
CPPFLAGS	+= -Iarch/$(ARCH) -Iarch/$(ARCH)/include
AFLAGS		+= -Iarch/$(ARCH)
CFLAGS		+= -Iarch/$(ARCH) -msoft-float -pipe \
		-ffixed-r2 -mmultiple

# No AltiVec instruction when building kernel
CFLAGS		+= $(call cc-option, -mno-altivec)

CPP		= $(CC) -E $(CFLAGS)
# Temporary hack until we have migrated to asm-powerpc
LINUXINCLUDE    += -Iarch/$(ARCH)/include

CHECKFLAGS	+= -D__powerpc__

ifndef CONFIG_FSL_BOOKE
CFLAGS		+= -mstring
endif

cpu-as-$(CONFIG_PPC64BRIDGE)	+= -Wa,-mppc64bridge
cpu-as-$(CONFIG_4xx)		+= -Wa,-m405
cpu-as-$(CONFIG_6xx)		+= -Wa,-maltivec
cpu-as-$(CONFIG_POWER4)		+= -Wa,-maltivec
cpu-as-$(CONFIG_E500)		+= -Wa,-me500
cpu-as-$(CONFIG_E200)		+= -Wa,-me200

AFLAGS += $(cpu-as-y)
CFLAGS += $(cpu-as-y)

# Default to the common case.
KBUILD_DEFCONFIG := common_defconfig

head-y				:= arch/ppc/kernel/head.o
head-$(CONFIG_8xx)		:= arch/ppc/kernel/head_8xx.o
head-$(CONFIG_4xx)		:= arch/ppc/kernel/head_4xx.o
head-$(CONFIG_44x)		:= arch/ppc/kernel/head_44x.o
head-$(CONFIG_FSL_BOOKE)	:= arch/ppc/kernel/head_fsl_booke.o

head-$(CONFIG_6xx)		+= arch/ppc/kernel/idle_6xx.o
head-$(CONFIG_POWER4)		+= arch/ppc/kernel/idle_power4.o
head-$(CONFIG_PPC_FPU)		+= arch/powerpc/kernel/fpu.o

core-y				+= arch/ppc/kernel/ arch/powerpc/kernel/ \
				   arch/ppc/platforms/ \
				   arch/ppc/mm/ arch/ppc/lib/ \
				   arch/ppc/syslib/ arch/powerpc/sysdev/ \
				   arch/powerpc/lib/
core-$(CONFIG_4xx)		+= arch/ppc/platforms/4xx/
core-$(CONFIG_83xx)		+= arch/ppc/platforms/83xx/
core-$(CONFIG_85xx)		+= arch/ppc/platforms/85xx/
core-$(CONFIG_MATH_EMULATION)	+= arch/ppc/math-emu/
core-$(CONFIG_XMON)		+= arch/ppc/xmon/
core-$(CONFIG_APUS)		+= arch/ppc/amiga/
drivers-$(CONFIG_8xx)		+= arch/ppc/8xx_io/
drivers-$(CONFIG_4xx)		+= arch/ppc/4xx_io/
drivers-$(CONFIG_CPM2)		+= arch/ppc/8260_io/

drivers-$(CONFIG_OPROFILE)	+= arch/powerpc/oprofile/

BOOT_TARGETS = zImage zImage.initrd znetboot znetboot.initrd vmlinux.sm

.PHONY: $(BOOT_TARGETS)

all: uImage zImage

CPPFLAGS_vmlinux.lds	:= -Upowerpc

# All the instructions talk about "make bzImage".
bzImage: zImage

boot := arch/$(ARCH)/boot

$(BOOT_TARGETS): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $@

uImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot)/images $(boot)/images/$@

define archhelp
  @echo '* zImage          - Compressed kernel image (arch/$(ARCH)/boot/images/zImage.*)'
  @echo '  uImage          - Create a bootable image for U-Boot / PPCBoot'
  @echo '  install         - Install kernel using'
  @echo '                    (your) ~/bin/installkernel or'
  @echo '                    (distribution) /sbin/installkernel or'
  @echo '                    install to $$(INSTALL_PATH) and run lilo'
  @echo '  *_defconfig     - Select default config from arch/$(ARCH)/ppc/configs'
endef

archclean:
	$(Q)$(MAKE) $(clean)=arch/ppc/boot
	# Temporary hack until we have migrated to asm-powerpc
	$(Q)rm -rf arch/$(ARCH)/include

archprepare: checkbin

# Temporary hack until we have migrated to asm-powerpc
include/asm: arch/$(ARCH)/include/asm
arch/$(ARCH)/include/asm:
	$(Q)if [ ! -d arch/$(ARCH)/include ]; then mkdir -p arch/$(ARCH)/include; fi
	$(Q)ln -fsn $(srctree)/include/asm-powerpc arch/$(ARCH)/include/asm

# Use the file '.tmp_gas_check' for binutils tests, as gas won't output
# to stdout and these checks are run even on install targets.
TOUT	:= .tmp_gas_check
# Ensure this is binutils 2.12.1 (or 2.12.90.0.7) or later for altivec
# instructions.
# gcc-3.4 and binutils-2.14 are a fatal combination.
GCC_VERSION	:= $(call cc-version)

checkbin:
	@if test "$(GCC_VERSION)" = "0304" ; then \
		if ! /bin/echo mftb 5 | $(AS) -v -mppc -many -o $(TOUT) >/dev/null 2>&1 ; then \
			echo -n '*** ${VERSION}.${PATCHLEVEL} kernels no longer build '; \
			echo 'correctly with gcc-3.4 and your version of binutils.'; \
			echo '*** Please upgrade your binutils or downgrade your gcc'; \
			false; \
		fi ; \
	fi
	@if ! /bin/echo dssall | $(AS) -many -o $(TOUT) >/dev/null 2>&1 ; then \
		echo -n '*** ${VERSION}.${PATCHLEVEL} kernels no longer build ' ; \
		echo 'correctly with old versions of binutils.' ; \
		echo '*** Please upgrade your binutils to 2.12.1 or newer' ; \
		false ; \
	fi

CLEAN_FILES += $(TOUT)

