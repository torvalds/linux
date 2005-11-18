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
CROSS32AS		:= $(CROSS32_COMPILE)as
CROSS32LD		:= $(CROSS32_COMPILE)ld
CROSS32OBJCOPY		:= $(CROSS32_COMPILE)objcopy

ifeq ($(HAS_BIARCH),y)
ifeq ($(CROSS32_COMPILE),)
CROSS32CC	:= $(CC) -m32
CROSS32AS	:= $(AS) -a32
CROSS32LD	:= $(LD) -m elf32ppc
CROSS32OBJCOPY	:= $(OBJCOPY)
endif
endif

export CROSS32CC CROSS32AS CROSS32LD CROSS32OBJCOPY

KBUILD_DEFCONFIG := $(shell uname -m)_defconfig

ifeq ($(CONFIG_PPC64),y)
OLDARCH	:= ppc64
SZ	:= 64

new_nm := $(shell if $(NM) --help 2>&1 | grep -- '--synthetic' > /dev/null; then echo y; else echo n; fi)

ifeq ($(new_nm),y)
NM		:= $(NM) --synthetic
endif

else
OLDARCH	:= ppc
SZ	:= 32
endif

UTS_MACHINE := $(OLDARCH)

ifeq ($(HAS_BIARCH),y)
override AS	+= -a$(SZ)
override LD	+= -m elf$(SZ)ppc
override CC	+= -m$(SZ)
endif

LDFLAGS_vmlinux	:= -Bstatic

# The -Iarch/$(ARCH)/include is temporary while we are merging
CPPFLAGS	+= -Iarch/$(ARCH) -Iarch/$(ARCH)/include
AFLAGS		+= -Iarch/$(ARCH)
CFLAGS		+= -Iarch/$(ARCH) -msoft-float -pipe
CFLAGS-$(CONFIG_PPC64)	:= -mminimal-toc -mtraceback=none  -mcall-aixdesc
CFLAGS-$(CONFIG_PPC32)	:= -ffixed-r2 -mmultiple
CFLAGS		+= $(CFLAGS-y)
CPP		= $(CC) -E $(CFLAGS)
# Temporary hack until we have migrated to asm-powerpc
LINUXINCLUDE    += -Iarch/$(ARCH)/include

CHECKFLAGS	+= -m$(SZ) -D__powerpc__ -D__powerpc$(SZ)__

ifeq ($(CONFIG_PPC64),y)
GCC_VERSION     := $(call cc-version)
GCC_BROKEN_VEC	:= $(shell if [ $(GCC_VERSION) -lt 0400 ] ; then echo "y"; fi)

ifeq ($(CONFIG_POWER4_ONLY),y)
ifeq ($(CONFIG_ALTIVEC),y)
ifeq ($(GCC_BROKEN_VEC),y)
	CFLAGS += $(call cc-option,-mcpu=970)
else
	CFLAGS += $(call cc-option,-mcpu=power4)
endif
else
	CFLAGS += $(call cc-option,-mcpu=power4)
endif
else
	CFLAGS += $(call cc-option,-mtune=power4)
endif
endif

# No AltiVec instruction when building kernel
CFLAGS += $(call cc-option,-mno-altivec)

# Enable unit-at-a-time mode when possible. It shrinks the
# kernel considerably.
CFLAGS += $(call cc-option,-funit-at-a-time)

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

head-y				:= arch/powerpc/kernel/head_32.o
head-$(CONFIG_PPC64)		:= arch/powerpc/kernel/head_64.o
head-$(CONFIG_8xx)		:= arch/powerpc/kernel/head_8xx.o
head-$(CONFIG_4xx)		:= arch/powerpc/kernel/head_4xx.o
head-$(CONFIG_44x)		:= arch/powerpc/kernel/head_44x.o
head-$(CONFIG_FSL_BOOKE)	:= arch/powerpc/kernel/head_fsl_booke.o

head-$(CONFIG_PPC64)		+= arch/powerpc/kernel/entry_64.o
head-$(CONFIG_PPC_FPU)		+= arch/powerpc/kernel/fpu.o

core-y				+= arch/powerpc/kernel/ \
				   arch/powerpc/mm/ \
				   arch/powerpc/lib/ \
				   arch/powerpc/sysdev/ \
				   arch/powerpc/platforms/
core-$(CONFIG_PPC32)		+= arch/ppc/kernel/
core-$(CONFIG_MATH_EMULATION)	+= arch/ppc/math-emu/
core-$(CONFIG_XMON)		+= arch/powerpc/xmon/
core-$(CONFIG_APUS)		+= arch/ppc/amiga/
drivers-$(CONFIG_8xx)		+= arch/ppc/8xx_io/
drivers-$(CONFIG_4xx)		+= arch/ppc/4xx_io/
drivers-$(CONFIG_CPM2)		+= arch/ppc/8260_io/

drivers-$(CONFIG_OPROFILE)	+= arch/powerpc/oprofile/

defaultimage-$(CONFIG_PPC32)	:= zImage
defaultimage-$(CONFIG_PPC_ISERIES) := vmlinux
defaultimage-$(CONFIG_PPC_PSERIES) := zImage
KBUILD_IMAGE := $(defaultimage-y)
all: $(KBUILD_IMAGE)

CPPFLAGS_vmlinux.lds	:= -Upowerpc

# All the instructions talk about "make bzImage".
bzImage: zImage

BOOT_TARGETS = zImage zImage.initrd znetboot znetboot.initrd vmlinux.sm

.PHONY: $(BOOT_TARGETS)

boot := arch/$(ARCH)/boot

$(BOOT_TARGETS): vmlinux
	$(Q)$(MAKE) ARCH=ppc64 $(build)=$(boot) $(patsubst %,$(boot)/%,$@)

define archhelp
  @echo '* zImage          - Compressed kernel image (arch/$(ARCH)/boot/zImage.*)'
  @echo '  install         - Install kernel using'
  @echo '                    (your) ~/bin/installkernel or'
  @echo '                    (distribution) /sbin/installkernel or'
  @echo '                    install to $$(INSTALL_PATH) and run lilo'
  @echo '  *_defconfig     - Select default config from arch/$(ARCH)/configs'
endef

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)
	$(Q)rm -rf arch/$(ARCH)/include

archprepare: checkbin

# Temporary hack until we have migrated to asm-powerpc
include/asm: arch/$(ARCH)/include/asm
arch/$(ARCH)/include/asm: FORCE
	$(Q)if [ ! -d arch/$(ARCH)/include ]; then mkdir -p arch/$(ARCH)/include; fi
	$(Q)ln -fsn $(srctree)/include/asm-$(OLDARCH) arch/$(ARCH)/include/asm

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

