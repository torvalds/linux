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
# Note that GCC does not numerically define an architecture version
# macro, but instead defines a whole series of macros which makes
# testing for a specific architecture or later rather impossible.
arch-$(CONFIG_CPU_32v7M)	=-D__LINUX_ARM_ARCH__=7 -march=armv7-m -Wa,-march=armv7-m
arch-$(CONFIG_CPU_32v7)		=-D__LINUX_ARM_ARCH__=7 $(call cc-option,-march=armv7-a,-march=armv5t -Wa$(comma)-march=armv7-a)
arch-$(CONFIG_CPU_32v6)		=-D__LINUX_ARM_ARCH__=6 $(call cc-option,-march=armv6,-march=armv5t -Wa$(comma)-march=armv6)
# Only override the compiler option if ARMv6. The ARMv6K extensions are
# always available in ARMv7
ifeq ($(CONFIG_CPU_32v6),y)
arch-$(CONFIG_CPU_32v6K)	=-D__LINUX_ARM_ARCH__=6 $(call cc-option,-march=armv6k,-march=armv5t -Wa$(comma)-march=armv6k)
endif
arch-$(CONFIG_CPU_32v5)		=-D__LINUX_ARM_ARCH__=5 $(call cc-option,-march=armv5te,-march=armv4t)
arch-$(CONFIG_CPU_32v4T)	=-D__LINUX_ARM_ARCH__=4 -march=armv4t
arch-$(CONFIG_CPU_32v4)		=-D__LINUX_ARM_ARCH__=4 -march=armv4
arch-$(CONFIG_CPU_32v3)		=-D__LINUX_ARM_ARCH__=3 -march=armv3m

# Evaluate arch cc-option calls now
arch-y := $(arch-y)

# This selects how we optimise for the processor.
tune-$(CONFIG_CPU_ARM7TDMI)	=-mtune=arm7tdmi
tune-$(CONFIG_CPU_ARM720T)	=-mtune=arm7tdmi
tune-$(CONFIG_CPU_ARM740T)	=-mtune=arm7tdmi
tune-$(CONFIG_CPU_ARM9TDMI)	=-mtune=arm9tdmi
tune-$(CONFIG_CPU_ARM940T)	=-mtune=arm9tdmi
tune-$(CONFIG_CPU_ARM946E)	=$(call cc-option,-mtune=arm9e,-mtune=arm9tdmi)
tune-$(CONFIG_CPU_ARM920T)	=-mtune=arm9tdmi
tune-$(CONFIG_CPU_ARM922T)	=-mtune=arm9tdmi
tune-$(CONFIG_CPU_ARM925T)	=-mtune=arm9tdmi
tune-$(CONFIG_CPU_ARM926T)	=-mtune=arm9tdmi
tune-$(CONFIG_CPU_FA526)	=-mtune=arm9tdmi
tune-$(CONFIG_CPU_SA110)	=-mtune=strongarm110
tune-$(CONFIG_CPU_SA1100)	=-mtune=strongarm1100
tune-$(CONFIG_CPU_XSCALE)	=$(call cc-option,-mtune=xscale,-mtune=strongarm110) -Wa,-mcpu=xscale
tune-$(CONFIG_CPU_XSC3)		=$(call cc-option,-mtune=xscale,-mtune=strongarm110) -Wa,-mcpu=xscale
tune-$(CONFIG_CPU_FEROCEON)	=$(call cc-option,-mtune=marvell-f,-mtune=xscale)
tune-$(CONFIG_CPU_V6)		=$(call cc-option,-mtune=arm1136j-s,-mtune=strongarm)
tune-$(CONFIG_CPU_V6K)		=$(call cc-option,-mtune=arm1136j-s,-mtune=strongarm)

# Evaluate tune cc-option calls now
tune-y := $(tune-y)

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

# Accept old syntax despite ".syntax unified"
AFLAGS_NOWARN	:=$(call as-option,-Wa$(comma)-mno-warn-deprecated,-Wa$(comma)-W)

ifeq ($(CONFIG_THUMB2_KERNEL),y)
CFLAGS_ISA	:=-mthumb -Wa,-mimplicit-it=always $(AFLAGS_NOWARN)
AFLAGS_ISA	:=$(CFLAGS_ISA) -Wa$(comma)-mthumb
else
CFLAGS_ISA	:=$(call cc-option,-marm,) $(AFLAGS_NOWARN)
AFLAGS_ISA	:=$(CFLAGS_ISA)
endif

# Need -Uarm for gcc < 3.x
KBUILD_CFLAGS	+=$(CFLAGS_ABI) $(CFLAGS_ISA) $(arch-y) $(tune-y) $(call cc-option,-mshort-load-bytes,$(call cc-option,-malignment-traps,)) -msoft-float -Uarm
KBUILD_AFLAGS	+=$(CFLAGS_ABI) $(AFLAGS_ISA) $(arch-y) $(tune-y) -include asm/unified.h -msoft-float

CHECKFLAGS	+= -D__arm__

#Default value
head-y		:= arch/arm/kernel/head$(MMUEXT).o

# Text offset. This list is sorted numerically by address in order to
# provide a means to avoid/resolve conflicts in multi-arch kernels.
# Note: the 32kB below this value is reserved for use by the kernel
# during boot, and this offset is critical to the functioning of
# kexec-tools.
textofs-y	:= 0x00008000
# We don't want the htc bootloader to corrupt kernel during resume
textofs-$(CONFIG_PM_H1940)      := 0x00108000
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
machine-$(CONFIG_ARCH_ALPINE)		+= alpine
machine-$(CONFIG_ARCH_ARTPEC)		+= artpec
machine-$(CONFIG_ARCH_ASPEED)           += aspeed
machine-$(CONFIG_ARCH_AT91)		+= at91
machine-$(CONFIG_ARCH_AXXIA)		+= axxia
machine-$(CONFIG_ARCH_BCM)		+= bcm
machine-$(CONFIG_ARCH_BERLIN)		+= berlin
machine-$(CONFIG_ARCH_CLPS711X)		+= clps711x
machine-$(CONFIG_ARCH_CNS3XXX)		+= cns3xxx
machine-$(CONFIG_ARCH_DAVINCI)		+= davinci
machine-$(CONFIG_ARCH_DIGICOLOR)	+= digicolor
machine-$(CONFIG_ARCH_DOVE)		+= dove
machine-$(CONFIG_ARCH_EFM32)		+= efm32
machine-$(CONFIG_ARCH_EP93XX)		+= ep93xx
machine-$(CONFIG_ARCH_EXYNOS)		+= exynos
machine-$(CONFIG_ARCH_FOOTBRIDGE)	+= footbridge
machine-$(CONFIG_ARCH_GEMINI)		+= gemini
machine-$(CONFIG_ARCH_HIGHBANK)		+= highbank
machine-$(CONFIG_ARCH_HISI)		+= hisi
machine-$(CONFIG_ARCH_INTEGRATOR)	+= integrator
machine-$(CONFIG_ARCH_IOP32X)		+= iop32x
machine-$(CONFIG_ARCH_IXP4XX)		+= ixp4xx
machine-$(CONFIG_ARCH_KEYSTONE)		+= keystone
machine-$(CONFIG_ARCH_LPC18XX)		+= lpc18xx
machine-$(CONFIG_ARCH_LPC32XX)		+= lpc32xx
machine-$(CONFIG_ARCH_MESON)		+= meson
machine-$(CONFIG_ARCH_MMP)		+= mmp
machine-$(CONFIG_ARCH_MPS2)		+= vexpress
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
machine-$(CONFIG_ARCH_OXNAS)		+= oxnas
machine-$(CONFIG_ARCH_OMAP1)		+= omap1
machine-$(CONFIG_ARCH_OMAP2PLUS)	+= omap2
machine-$(CONFIG_ARCH_ORION5X)		+= orion5x
machine-$(CONFIG_ARCH_PICOXCELL)	+= picoxcell
machine-$(CONFIG_ARCH_PXA)		+= pxa
machine-$(CONFIG_ARCH_QCOM)		+= qcom
machine-$(CONFIG_ARCH_RDA)		+= rda
machine-$(CONFIG_ARCH_REALTEK)		+= realtek
machine-$(CONFIG_ARCH_REALVIEW)		+= realview
machine-$(CONFIG_ARCH_ROCKCHIP)		+= rockchip
machine-$(CONFIG_ARCH_RPC)		+= rpc
machine-$(CONFIG_PLAT_SAMSUNG)		+= s3c
machine-$(CONFIG_ARCH_S5PV210)		+= s5pv210
machine-$(CONFIG_ARCH_SA1100)		+= sa1100
machine-$(CONFIG_ARCH_RENESAS)	 	+= shmobile
machine-$(CONFIG_ARCH_SIRF)		+= prima2
machine-$(CONFIG_ARCH_SOCFPGA)		+= socfpga
machine-$(CONFIG_ARCH_STI)		+= sti
machine-$(CONFIG_ARCH_STM32)		+= stm32
machine-$(CONFIG_ARCH_SUNXI)		+= sunxi
machine-$(CONFIG_ARCH_TANGO)		+= tango
machine-$(CONFIG_ARCH_TEGRA)		+= tegra
machine-$(CONFIG_ARCH_U300)		+= u300
machine-$(CONFIG_ARCH_U8500)		+= ux500
machine-$(CONFIG_ARCH_VERSATILE)	+= versatile
machine-$(CONFIG_ARCH_VEXPRESS)		+= vexpress
machine-$(CONFIG_ARCH_VT8500)		+= vt8500
machine-$(CONFIG_ARCH_ZX)		+= zx
machine-$(CONFIG_ARCH_ZYNQ)		+= zynq
machine-$(CONFIG_PLAT_SPEAR)		+= spear

# Platform directory name.  This list is sorted alphanumerically
# by CONFIG_* macro name.
plat-$(CONFIG_ARCH_OMAP)	+= omap
plat-$(CONFIG_PLAT_ORION)	+= orion
plat-$(CONFIG_PLAT_PXA)		+= pxa
plat-$(CONFIG_PLAT_VERSATILE)	+= versatile

# The byte offset of the kernel image in RAM from the start of RAM.
TEXT_OFFSET := $(textofs-y)

# The first directory contains additional information for the boot setup code
ifneq ($(machine-y),)
MACHINE  := arch/arm/mach-$(word 1,$(machine-y))/
else
MACHINE  :=
endif
ifeq ($(CONFIG_ARCH_MULTIPLATFORM),y)
MACHINE  :=
endif

machdirs := $(patsubst %,arch/arm/mach-%/,$(machine-y))
platdirs := $(patsubst %,arch/arm/plat-%/,$(sort $(plat-y)))

ifneq ($(CONFIG_ARCH_MULTIPLATFORM),y)
ifneq ($(CONFIG_ARM_SINGLE_ARMV7M),y)
KBUILD_CPPFLAGS += $(patsubst %,-I$(srctree)/%include,$(machdirs) $(platdirs))
endif
endif

export	TEXT_OFFSET GZFLAGS MMUEXT

core-y				+= arch/arm/
# If we have a machine-specific directory, then include it in the build.
core-y				+= $(machdirs) $(platdirs)

# For cleaning
core-				+= $(patsubst %,arch/arm/mach-%/, $(machine-))
core-				+= $(patsubst %,arch/arm/plat-%/, $(plat-))

drivers-$(CONFIG_OPROFILE)      += arch/arm/oprofile/

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
stack_protector_prepare: prepare0
	$(eval SSP_PLUGIN_CFLAGS := \
		-fplugin-arg-arm_ssp_per_task_plugin-tso=$(shell	\
			awk '{if ($$2 == "THREAD_SZ_ORDER") print $$3;}'\
				include/generated/asm-offsets.h)	\
		-fplugin-arg-arm_ssp_per_task_plugin-offset=$(shell	\
			awk '{if ($$2 == "TI_STACK_CANARY") print $$3;}'\
				include/generated/asm-offsets.h))
	$(eval KBUILD_CFLAGS += $(SSP_PLUGIN_CFLAGS))
	$(eval GCC_PLUGINS_CFLAGS += $(SSP_PLUGIN_CFLAGS))
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
	$(Q)$(MAKE) $(build)=$(boot) MACHINE=$(MACHINE) $(boot)/$@
	@$(kecho) '  Kernel: $(boot)/$@ is ready'

$(INSTALL_TARGETS):
	$(Q)$(MAKE) $(build)=$(boot) MACHINE=$(MACHINE) $@

PHONY += vdso_install
vdso_install:
ifeq ($(CONFIG_VDSO),y)
	$(Q)$(MAKE) $(build)=arch/arm/vdso $@
endif

# We use MRPROPER_FILES and CLEAN_FILES now
archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

# My testing targets (bypasses dependencies)
bp:;	$(Q)$(MAKE) $(build)=$(boot) MACHINE=$(MACHINE) $(boot)/bootpImage


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
  echo  '  vdso_install  - Install unstripped vdso.so to $$(INSTALL_MOD_PATH)/vdso'
endef
