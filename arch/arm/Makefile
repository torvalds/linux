#
# arch/arm/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995-2001 by Russell King

LDFLAGS_vmlinux	:= --no-undefined -X --pic-veneer -z norelro
ifeq ($(CONFIG_CPU_ENDIAN_BE8),y)
LDFLAGS_vmlinux	+= --be8
KBUILD_LDFLAGS_MODULE	+= --be8
endif

GZFLAGS		:=-9
#KBUILD_CFLAGS	+=-pipe

# Never generate .eh_frame
KBUILD_CFLAGS	+= $(call cc-option,-fno-dwarf2-cfi-asm)

# Disable FDPIC ABI
KBUILD_CFLAGS	+= $(call cc-option,-mno-fdpic)

# This should work on most of the modern platforms
KBUILD_DEFCONFIG := multi_v7_defconfig

# defines filename extension depending memory management type.
ifeq ($(CONFIG_MMU),)
MMUEXT		:= -nommu
KBUILD_CFLAGS	+= $(call cc-option,-mno-unaligned-access)
endif

ifeq ($(CONFIG_FRAME_POINTER),y)
KBUILD_CFLAGS	+=-fno-omit-frame-pointer
ifeq ($(CONFIG_CC_IS_GCC),y)
KBUILD_CFLAGS += -mapcs -mno-sched-prolog
endif
endif

ifeq ($(CONFIG_CPU_BIG_ENDIAN),y)
KBUILD_CPPFLAGS	+= -mbig-endian
CHECKFLAGS	+= -D__ARMEB__
KBUILD_LDFLAGS	+= -EB
else
KBUILD_CPPFLAGS	+= -mlittle-endian
CHECKFLAGS	+= -D__ARMEL__
KBUILD_LDFLAGS	+= -EL
endif

#
# The Scalar Replacement of Aggregates (SRA) optimization pass in GCC 4.9 and
# later may result in code being generated that handles signed short and signed
# char struct members incorrectly. So disable it.
# (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=65932)
#
KBUILD_CFLAGS	+= $(call cc-option,-fno-ipa-sra)

# This selects which instruction set is used.
arch-$(CONFIG_CPU_32v7M)	:=-march=armv7-m
arch-$(CONFIG_CPU_32v7)		:=-march=armv7-a
arch-$(CONFIG_CPU_32v6)		:=-march=armv6
# Only override the compiler option if ARMv6. The ARMv6K extensions are
# always available in ARMv7
ifeq ($(CONFIG_CPU_32v6),y)
arch-$(CONFIG_CPU_32v6K)	:=-march=armv6k
endif
arch-$(CONFIG_CPU_32v5)		:=-march=armv5te
arch-$(CONFIG_CPU_32v4T)	:=-march=armv4t
arch-$(CONFIG_CPU_32v4)		:=-march=armv4
arch-$(CONFIG_CPU_32v3)		:=-march=armv3m

# Note that GCC does not numerically define an architecture version
# macro, but instead defines a whole series of macros which makes
# testing for a specific architecture or later rather impossible.
cpp-$(CONFIG_CPU_32v7M)		:=-D__LINUX_ARM_ARCH__=7
cpp-$(CONFIG_CPU_32v7)		:=-D__LINUX_ARM_ARCH__=7
cpp-$(CONFIG_CPU_32v6)		:=-D__LINUX_ARM_ARCH__=6
# Only override the compiler option if ARMv6. The ARMv6K extensions are
# always available in ARMv7
ifeq ($(CONFIG_CPU_32v6),y)
cpp-$(CONFIG_CPU_32v6K)		:=-D__LINUX_ARM_ARCH__=6
endif
cpp-$(CONFIG_CPU_32v5)		:=-D__LINUX_ARM_ARCH__=5
cpp-$(CONFIG_CPU_32v4T)		:=-D__LINUX_ARM_ARCH__=4
cpp-$(CONFIG_CPU_32v4)		:=-D__LINUX_ARM_ARCH__=4
cpp-$(CONFIG_CPU_32v3)		:=-D__LINUX_ARM_ARCH__=3

# This selects how we optimise for the processor.
tune-$(CONFIG_CPU_ARM7TDMI)	:=-mtune=arm7tdmi
tune-$(CONFIG_CPU_ARM720T)	:=-mtune=arm7tdmi
tune-$(CONFIG_CPU_ARM740T)	:=-mtune=arm7tdmi
tune-$(CONFIG_CPU_ARM9TDMI)	:=-mtune=arm9tdmi
tune-$(CONFIG_CPU_ARM940T)	:=-mtune=arm9tdmi
tune-$(CONFIG_CPU_ARM946E)	:=-mtune=arm9e
tune-$(CONFIG_CPU_ARM920T)	:=-mtune=arm9tdmi
tune-$(CONFIG_CPU_ARM922T)	:=-mtune=arm9tdmi
tune-$(CONFIG_CPU_ARM925T)	:=-mtune=arm9tdmi
tune-$(CONFIG_CPU_ARM926T)	:=-mtune=arm9tdmi
tune-$(CONFIG_CPU_FA526)	:=-mtune=arm9tdmi
tune-$(CONFIG_CPU_SA110)	:=-mtune=strongarm110
tune-$(CONFIG_CPU_SA1100)	:=-mtune=strongarm1100
tune-$(CONFIG_CPU_XSCALE)	:=-mtune=xscale
tune-$(CONFIG_CPU_XSC3)		:=-mtune=xscale
tune-$(CONFIG_CPU_FEROCEON)	:=-mtune=xscale
tune-$(CONFIG_CPU_V6)		:=-mtune=arm1136j-s
tune-$(CONFIG_CPU_V6K)		:=-mtune=arm1136j-s

ifeq ($(CONFIG_AEABI),y)
CFLAGS_ABI	:=-mabi=aapcs-linux -mfpu=vfp
else
CFLAGS_ABI	:=$(call cc-option,-mapcs-32,-mabi=apcs-gnu) $(call cc-option,-mno-thumb-interwork,)
endif

ifeq ($(CONFIG_ARM_UNWIND),y)
CFLAGS_ABI	+=-funwind-tables
endif

ifeq ($(CONFIG_CC_IS_CLANG),y)
CFLAGS_ABI	+= -meabi gnu
endif

ifeq ($(CONFIG_CURRENT_POINTER_IN_TPIDRURO),y)
KBUILD_CFLAGS	+= -mtp=cp15
endif

# Accept old syntax despite ".syntax unified"
AFLAGS_NOWARN	:=$(call as-option,-Wa$(comma)-mno-warn-deprecated,-Wa$(comma)-W)

ifeq ($(CONFIG_THUMB2_KERNEL),y)
CFLAGS_ISA	:=-Wa,-mimplicit-it=always $(AFLAGS_NOWARN)
AFLAGS_ISA	:=$(CFLAGS_ISA) -Wa$(comma)-mthumb
CFLAGS_ISA	+=-mthumb
else
CFLAGS_ISA	:=$(call cc-option,-marm,) $(AFLAGS_NOWARN)
AFLAGS_ISA	:=$(CFLAGS_ISA)
endif

# Need -Uarm for gcc < 3.x
KBUILD_CPPFLAGS	+=$(cpp-y)
KBUILD_CFLAGS	+=$(CFLAGS_ABI) $(CFLAGS_ISA) $(arch-y) $(tune-y) $(call cc-option,-mshort-load-bytes,$(call cc-option,-malignment-traps,)) -msoft-float -Uarm
KBUILD_AFLAGS	+=$(CFLAGS_ABI) $(AFLAGS_ISA) -Wa,$(arch-y) $(tune-y) -include asm/unified.h -msoft-float

CHECKFLAGS	+= -D__arm__

# Text offset. This list is sorted numerically by address in order to
# provide a means to avoid/resolve conflicts in multi-arch kernels.
# Note: the 32kB below this value is reserved for use by the kernel
# during boot, and this offset is critical to the functioning of
# kexec-tools.
textofs-y	:= 0x00008000
# RTD1195 has Boot ROM at start of address space
textofs-$(CONFIG_ARCH_REALTEK)  := 0x00108000
# SA1111 DMA bug: we don't want the kernel to live in precious DMA-able memory
ifeq ($(CONFIG_ARCH_SA1100),y)
textofs-$(CONFIG_SA1111) := 0x00208000
endif
textofs-$(CONFIG_ARCH_IPQ40XX) := 0x00208000
textofs-$(CONFIG_ARCH_MSM8X60) := 0x00208000
textofs-$(CONFIG_ARCH_MSM8960) := 0x00208000
textofs-$(CONFIG_ARCH_MESON) := 0x00208000
textofs-$(CONFIG_ARCH_AXXIA) := 0x00308000

# Machine directory name.  This list is sorted alphanumerically
# by CONFIG_* macro name.
machine-$(CONFIG_ARCH_ACTIONS)		+= actions
machine-$(CONFIG_ARCH_AIROHA)		+= airoha
machine-$(CONFIG_ARCH_ALPINE)		+= alpine
machine-$(CONFIG_ARCH_ARTPEC)		+= artpec
machine-$(CONFIG_ARCH_ASPEED)           += aspeed
machine-$(CONFIG_ARCH_AT91)		+= at91
machine-$(CONFIG_ARCH_AXXIA)		+= axxia
machine-$(CONFIG_ARCH_BCM)		+= bcm
machine-$(CONFIG_ARCH_BERLIN)		+= berlin
machine-$(CONFIG_ARCH_CLPS711X)		+= clps711x
machine-$(CONFIG_ARCH_DAVINCI)		+= davinci
machine-$(CONFIG_ARCH_DIGICOLOR)	+= digicolor
machine-$(CONFIG_ARCH_DOVE)		+= dove
machine-$(CONFIG_ARCH_EP93XX)		+= ep93xx
machine-$(CONFIG_ARCH_EXYNOS)		+= exynos
machine-$(CONFIG_ARCH_FOOTBRIDGE)	+= footbridge
machine-$(CONFIG_ARCH_GEMINI)		+= gemini
machine-$(CONFIG_ARCH_HIGHBANK)		+= highbank
machine-$(CONFIG_ARCH_HISI)		+= hisi
machine-$(CONFIG_ARCH_HPE)		+= hpe
machine-$(CONFIG_ARCH_IXP4XX)		+= ixp4xx
machine-$(CONFIG_ARCH_KEYSTONE)		+= keystone
machine-$(CONFIG_ARCH_LPC18XX)		+= lpc18xx
machine-$(CONFIG_ARCH_LPC32XX)		+= lpc32xx
machine-$(CONFIG_ARCH_MESON)		+= meson
machine-$(CONFIG_ARCH_MMP)		+= mmp
machine-$(CONFIG_ARCH_MOXART)		+= moxart
machine-$(CONFIG_ARCH_MV78XX0)		+= mv78xx0
machine-$(CONFIG_ARCH_MVEBU)		+= mvebu
machine-$(CONFIG_ARCH_MXC)		+= imx
machine-$(CONFIG_ARCH_MEDIATEK)		+= mediatek
machine-$(CONFIG_ARCH_MILBEAUT)		+= milbeaut
machine-$(CONFIG_ARCH_MXS)		+= mxs
machine-$(CONFIG_ARCH_MSTARV7)		+= mstar
machine-$(CONFIG_ARCH_NOMADIK)		+= nomadik
machine-$(CONFIG_ARCH_NPCM)		+= npcm
machine-$(CONFIG_ARCH_NSPIRE)		+= nspire
machine-$(CONFIG_ARCH_OMAP1)		+= omap1
machine-$(CONFIG_ARCH_OMAP2PLUS)	+= omap2
machine-$(CONFIG_ARCH_ORION5X)		+= orion5x
machine-$(CONFIG_ARCH_PXA)		+= pxa
machine-$(CONFIG_ARCH_QCOM)		+= qcom
machine-$(CONFIG_ARCH_REALTEK)		+= realtek
machine-$(CONFIG_ARCH_ROCKCHIP)		+= rockchip
machine-$(CONFIG_ARCH_RPC)		+= rpc
machine-$(CONFIG_PLAT_SAMSUNG)		+= s3c
machine-$(CONFIG_ARCH_S5PV210)		+= s5pv210
machine-$(CONFIG_ARCH_SA1100)		+= sa1100
machine-$(CONFIG_ARCH_RENESAS)	 	+= shmobile
machine-$(CONFIG_ARCH_INTEL_SOCFPGA)	+= socfpga
machine-$(CONFIG_ARCH_STI)		+= sti
machine-$(CONFIG_ARCH_STM32)		+= stm32
machine-$(CONFIG_ARCH_SUNPLUS)		+= sunplus
machine-$(CONFIG_ARCH_SUNXI)		+= sunxi
machine-$(CONFIG_ARCH_TEGRA)		+= tegra
machine-$(CONFIG_ARCH_U8500)		+= ux500
machine-$(CONFIG_ARCH_VT8500)		+= vt8500
machine-$(CONFIG_ARCH_ZYNQ)		+= zynq
machine-$(CONFIG_PLAT_VERSATILE)	+= versatile
machine-$(CONFIG_PLAT_SPEAR)		+= spear

# legacy platforms provide their own mach/*.h headers globally,
# these three are mutually exclusive
machdirs-$(CONFIG_ARCH_FOOTBRIDGE)	+= arch/arm/mach-footbridge
machdirs-$(CONFIG_ARCH_RPC)		+= arch/arm/mach-rpc
machdirs-$(CONFIG_ARCH_SA1100)		+= arch/arm/mach-sa1100
KBUILD_CPPFLAGS += $(patsubst %,-I$(srctree)/%/include,$(machdirs-y))

# The byte offset of the kernel image in RAM from the start of RAM.
TEXT_OFFSET := $(textofs-y)

export	TEXT_OFFSET GZFLAGS MMUEXT

# If we have a machine-specific directory, then include it in the build.
core-y				+= $(patsubst %,arch/arm/mach-%/,$(machine-y))
# For cleaning
core-				+= $(patsubst %,arch/arm/mach-%/,$(machine-))

core-$(CONFIG_PLAT_ORION)	+= arch/arm/plat-orion/

libs-y				:= arch/arm/lib/ $(libs-y)

# Default target when executing plain make
boot := arch/arm/boot
ifeq ($(CONFIG_XIP_KERNEL),y)
KBUILD_IMAGE := $(boot)/xipImage
else
KBUILD_IMAGE := $(boot)/zImage
endif

ifeq ($(CONFIG_STACKPROTECTOR_PER_TASK),y)
prepare: stack_protector_prepare
ifeq ($(CONFIG_CC_HAVE_STACKPROTECTOR_TLS),y)
stack_protector_prepare: prepare0
	$(eval KBUILD_CFLAGS += \
		-mstack-protector-guard=tls \
		-mstack-protector-guard-offset=$(shell	\
			awk '{if ($$2 == "TSK_STACK_CANARY") print $$3;}'\
				include/generated/asm-offsets.h))
else
stack_protector_prepare: prepare0
	$(eval SSP_PLUGIN_CFLAGS := \
		-fplugin-arg-arm_ssp_per_task_plugin-offset=$(shell	\
			awk '{if ($$2 == "TSK_STACK_CANARY") print $$3;}'\
				include/generated/asm-offsets.h))
	$(eval KBUILD_CFLAGS += $(SSP_PLUGIN_CFLAGS))
	$(eval GCC_PLUGINS_CFLAGS += $(SSP_PLUGIN_CFLAGS))
endif
endif

all:	$(notdir $(KBUILD_IMAGE))


archheaders:
	$(Q)$(MAKE) $(build)=arch/arm/tools uapi

archprepare:
	$(Q)$(MAKE) $(build)=arch/arm/tools kapi

# Convert bzImage to zImage
bzImage: zImage

BOOT_TARGETS	= zImage Image xipImage bootpImage uImage
INSTALL_TARGETS	= zinstall uinstall install

PHONY += bzImage $(BOOT_TARGETS) $(INSTALL_TARGETS)

bootpImage uImage: zImage
zImage: Image

$(BOOT_TARGETS): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@
	@$(kecho) '  Kernel: $(boot)/$@ is ready'

$(INSTALL_TARGETS): KBUILD_IMAGE = $(boot)/$(patsubst %install,%Image,$@)
$(INSTALL_TARGETS):
	$(call cmd,install)

vdso-install-$(CONFIG_VDSO) += arch/arm/vdso/vdso.so.dbg

# My testing targets (bypasses dependencies)
bp:;	$(Q)$(MAKE) $(build)=$(boot) $(boot)/bootpImage

include $(srctree)/scripts/Makefile.defconf
PHONY += multi_v7_lpae_defconfig
multi_v7_lpae_defconfig:
	$(call merge_into_defconfig,multi_v7_defconfig,lpae)

define archhelp
  echo  '* zImage        - Compressed kernel image (arch/$(ARCH)/boot/zImage)'
  echo  '  Image         - Uncompressed kernel image (arch/$(ARCH)/boot/Image)'
  echo  '* xipImage      - XIP kernel image, if configured (arch/$(ARCH)/boot/xipImage)'
  echo  '  uImage        - U-Boot wrapped zImage'
  echo  '  bootpImage    - Combined zImage and initial RAM disk'
  echo  '                  (supply initrd image via make variable INITRD=<path>)'
  echo  '  install       - Install uncompressed kernel'
  echo  '  zinstall      - Install compressed kernel'
  echo  '  uinstall      - Install U-Boot wrapped compressed kernel'
  echo  '                  Install using (your) ~/bin/$(INSTALLKERNEL) or'
  echo  '                  (distribution) /sbin/$(INSTALLKERNEL) or'
  echo  '                  install to $$(INSTALL_PATH) and run lilo'
  echo
  echo  '  multi_v7_lpae_defconfig     - multi_v7_defconfig with CONFIG_ARM_LPAE enabled'
endef
