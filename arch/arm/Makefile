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

# Ensure linker flags are correct
LDFLAGS		:=

LDFLAGS_vmlinux	:=-p --no-undefined -X
ifeq ($(CONFIG_CPU_ENDIAN_BE8),y)
LDFLAGS_vmlinux	+= --be8
LDFLAGS_MODULE	+= --be8
endif

OBJCOPYFLAGS	:=-O binary -R .comment -S
GZFLAGS		:=-9
#KBUILD_CFLAGS	+=-pipe

# Never generate .eh_frame
KBUILD_CFLAGS	+= $(call cc-option,-fno-dwarf2-cfi-asm)

# Do not use arch/arm/defconfig - it's always outdated.
# Select a platform tht is kept up-to-date
KBUILD_DEFCONFIG := versatile_defconfig

# defines filename extension depending memory management type.
ifeq ($(CONFIG_MMU),)
MMUEXT		:= -nommu
KBUILD_CFLAGS	+= $(call cc-option,-mno-unaligned-access)
endif

ifeq ($(CONFIG_FRAME_POINTER),y)
KBUILD_CFLAGS	+=-fno-omit-frame-pointer -mapcs -mno-sched-prolog
endif

ifeq ($(CONFIG_CPU_BIG_ENDIAN),y)
KBUILD_CPPFLAGS	+= -mbig-endian
AS		+= -EB
LD		+= -EB
else
KBUILD_CPPFLAGS	+= -mlittle-endian
AS		+= -EL
LD		+= -EL
endif

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
arch-$(CONFIG_CPU_32v3)		=-D__LINUX_ARM_ARCH__=3 -march=armv3

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
CFLAGS_ABI	:=-mabi=aapcs-linux -mno-thumb-interwork -mfpu=vfp
else
CFLAGS_ABI	:=$(call cc-option,-mapcs-32,-mabi=apcs-gnu) $(call cc-option,-mno-thumb-interwork,)
endif

ifeq ($(CONFIG_ARM_UNWIND),y)
CFLAGS_ABI	+=-funwind-tables
endif

ifeq ($(CONFIG_THUMB2_KERNEL),y)
AFLAGS_AUTOIT	:=$(call as-option,-Wa$(comma)-mimplicit-it=always,-Wa$(comma)-mauto-it)
AFLAGS_NOWARN	:=$(call as-option,-Wa$(comma)-mno-warn-deprecated,-Wa$(comma)-W)
CFLAGS_ISA	:=-mthumb $(AFLAGS_AUTOIT) $(AFLAGS_NOWARN)
AFLAGS_ISA	:=$(CFLAGS_ISA) -Wa$(comma)-mthumb
# Work around buggy relocation from gas if requested:
ifeq ($(CONFIG_THUMB2_AVOID_R_ARM_THM_JUMP11),y)
CFLAGS_MODULE	+=-fno-optimize-sibling-calls
endif
else
CFLAGS_ISA	:=$(call cc-option,-marm,)
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
textofs-y	:= 0x00008000
textofs-$(CONFIG_ARCH_CLPS711X) := 0x00028000
# We don't want the htc bootloader to corrupt kernel during resume
textofs-$(CONFIG_PM_H1940)      := 0x00108000
# SA1111 DMA bug: we don't want the kernel to live in precious DMA-able memory
ifeq ($(CONFIG_ARCH_SA1100),y)
textofs-$(CONFIG_SA1111) := 0x00208000
endif
textofs-$(CONFIG_ARCH_MSM7X30) := 0x00208000
textofs-$(CONFIG_ARCH_MSM8X60) := 0x00208000
textofs-$(CONFIG_ARCH_MSM8960) := 0x00208000
textofs-$(CONFIG_ARCH_AXXIA) := 0x00308000

# Machine directory name.  This list is sorted alphanumerically
# by CONFIG_* macro name.
machine-$(CONFIG_ARCH_AT91)		+= at91
machine-$(CONFIG_ARCH_AXXIA)		+= axxia
machine-$(CONFIG_ARCH_BCM)		+= bcm
machine-$(CONFIG_ARCH_BERLIN)		+= berlin
machine-$(CONFIG_ARCH_CLPS711X)		+= clps711x
machine-$(CONFIG_ARCH_CNS3XXX)		+= cns3xxx
machine-$(CONFIG_ARCH_DAVINCI)		+= davinci
machine-$(CONFIG_ARCH_DOVE)		+= dove
machine-$(CONFIG_ARCH_EBSA110)		+= ebsa110
machine-$(CONFIG_ARCH_EFM32)		+= efm32
machine-$(CONFIG_ARCH_EP93XX)		+= ep93xx
machine-$(CONFIG_ARCH_EXYNOS)		+= exynos
machine-$(CONFIG_ARCH_FOOTBRIDGE)	+= footbridge
machine-$(CONFIG_ARCH_GEMINI)		+= gemini
machine-$(CONFIG_ARCH_HIGHBANK)		+= highbank
machine-$(CONFIG_ARCH_HISI)		+= hisi
machine-$(CONFIG_ARCH_INTEGRATOR)	+= integrator
machine-$(CONFIG_ARCH_IOP13XX)		+= iop13xx
machine-$(CONFIG_ARCH_IOP32X)		+= iop32x
machine-$(CONFIG_ARCH_IOP33X)		+= iop33x
machine-$(CONFIG_ARCH_IXP4XX)		+= ixp4xx
machine-$(CONFIG_ARCH_KEYSTONE)		+= keystone
machine-$(CONFIG_ARCH_KS8695)		+= ks8695
machine-$(CONFIG_ARCH_LPC32XX)		+= lpc32xx
machine-$(CONFIG_ARCH_MESON)		+= meson
machine-$(CONFIG_ARCH_MMP)		+= mmp
machine-$(CONFIG_ARCH_MOXART)		+= moxart
machine-$(CONFIG_ARCH_MSM)		+= msm
machine-$(CONFIG_ARCH_MV78XX0)		+= mv78xx0
machine-$(CONFIG_ARCH_MVEBU)		+= mvebu
machine-$(CONFIG_ARCH_MXC)		+= imx
machine-$(CONFIG_ARCH_MEDIATEK)		+= mediatek
machine-$(CONFIG_ARCH_MXS)		+= mxs
machine-$(CONFIG_ARCH_NETX)		+= netx
machine-$(CONFIG_ARCH_NOMADIK)		+= nomadik
machine-$(CONFIG_ARCH_NSPIRE)		+= nspire
machine-$(CONFIG_ARCH_OMAP1)		+= omap1
machine-$(CONFIG_ARCH_OMAP2PLUS)	+= omap2
machine-$(CONFIG_ARCH_ORION5X)		+= orion5x
machine-$(CONFIG_ARCH_PICOXCELL)	+= picoxcell
machine-$(CONFIG_ARCH_PXA)		+= pxa
machine-$(CONFIG_ARCH_QCOM)		+= qcom
machine-$(CONFIG_ARCH_REALVIEW)		+= realview
machine-$(CONFIG_ARCH_ROCKCHIP)		+= rockchip
machine-$(CONFIG_ARCH_RPC)		+= rpc
machine-$(CONFIG_ARCH_S3C24XX)		+= s3c24xx
machine-$(CONFIG_ARCH_S3C64XX)		+= s3c64xx
machine-$(CONFIG_ARCH_S5PV210)		+= s5pv210
machine-$(CONFIG_ARCH_SA1100)		+= sa1100
machine-$(CONFIG_ARCH_SHMOBILE) 	+= shmobile
machine-$(CONFIG_ARCH_SIRF)		+= prima2
machine-$(CONFIG_ARCH_SOCFPGA)		+= socfpga
machine-$(CONFIG_ARCH_STI)		+= sti
machine-$(CONFIG_ARCH_SUNXI)		+= sunxi
machine-$(CONFIG_ARCH_TEGRA)		+= tegra
machine-$(CONFIG_ARCH_U300)		+= u300
machine-$(CONFIG_ARCH_U8500)		+= ux500
machine-$(CONFIG_ARCH_VERSATILE)	+= versatile
machine-$(CONFIG_ARCH_VEXPRESS)		+= vexpress
machine-$(CONFIG_ARCH_VT8500)		+= vt8500
machine-$(CONFIG_ARCH_W90X900)		+= w90x900
machine-$(CONFIG_ARCH_ZYNQ)		+= zynq
machine-$(CONFIG_PLAT_SPEAR)		+= spear

# Platform directory name.  This list is sorted alphanumerically
# by CONFIG_* macro name.
plat-$(CONFIG_ARCH_EXYNOS)	+= samsung
plat-$(CONFIG_ARCH_OMAP)	+= omap
plat-$(CONFIG_ARCH_S3C64XX)	+= samsung
plat-$(CONFIG_ARCH_S5PV210)	+= samsung
plat-$(CONFIG_PLAT_IOP)		+= iop
plat-$(CONFIG_PLAT_ORION)	+= orion
plat-$(CONFIG_PLAT_PXA)		+= pxa
plat-$(CONFIG_PLAT_S3C24XX)	+= samsung
plat-$(CONFIG_PLAT_VERSATILE)	+= versatile

ifeq ($(CONFIG_ARCH_EBSA110),y)
# This is what happens if you forget the IOCS16 line.
# PCMCIA cards stop working.
CFLAGS_3c589_cs.o :=-DISA_SIXTEEN_BIT_PERIPHERAL
export CFLAGS_3c589_cs.o
endif

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
ifeq ($(KBUILD_SRC),)
KBUILD_CPPFLAGS += $(patsubst %,-I%include,$(machdirs) $(platdirs))
else
KBUILD_CPPFLAGS += $(patsubst %,-I$(srctree)/%include,$(machdirs) $(platdirs))
endif
endif

export	TEXT_OFFSET GZFLAGS MMUEXT

# Do we have FASTFPE?
FASTFPE		:=arch/arm/fastfpe
ifeq ($(FASTFPE),$(wildcard $(FASTFPE)))
FASTFPE_OBJ	:=$(FASTFPE)/
endif

core-$(CONFIG_FPE_NWFPE)	+= arch/arm/nwfpe/
core-$(CONFIG_FPE_FASTFPE)	+= $(FASTFPE_OBJ)
core-$(CONFIG_VFP)		+= arch/arm/vfp/
core-$(CONFIG_XEN)		+= arch/arm/xen/
core-$(CONFIG_KVM_ARM_HOST) 	+= arch/arm/kvm/

# If we have a machine-specific directory, then include it in the build.
core-y				+= arch/arm/kernel/ arch/arm/mm/ arch/arm/common/
core-y				+= arch/arm/net/
core-y				+= arch/arm/crypto/
core-y				+= arch/arm/firmware/
core-y				+= $(machdirs) $(platdirs)

drivers-$(CONFIG_OPROFILE)      += arch/arm/oprofile/

libs-y				:= arch/arm/lib/ $(libs-y)

# Default target when executing plain make
ifeq ($(CONFIG_XIP_KERNEL),y)
KBUILD_IMAGE := xipImage
else
KBUILD_IMAGE := zImage
endif

# Build the DT binary blobs if we have OF configured
ifeq ($(CONFIG_USE_OF),y)
KBUILD_DTBS := dtbs
endif

all:	$(KBUILD_IMAGE) $(KBUILD_DTBS)

boot := arch/arm/boot

archprepare:
	$(Q)$(MAKE) $(build)=arch/arm/tools include/generated/mach-types.h

# Convert bzImage to zImage
bzImage: zImage

BOOT_TARGETS	= zImage Image xipImage bootpImage uImage
INSTALL_TARGETS	= zinstall uinstall install

PHONY += bzImage $(BOOT_TARGETS) $(INSTALL_TARGETS)

$(BOOT_TARGETS): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) MACHINE=$(MACHINE) $(boot)/$@

$(INSTALL_TARGETS):
	$(Q)$(MAKE) $(build)=$(boot) MACHINE=$(MACHINE) $@

%.dtb: | scripts
	$(Q)$(MAKE) $(build)=$(boot)/dts MACHINE=$(MACHINE) $(boot)/dts/$@

PHONY += dtbs dtbs_install
dtbs dtbs_install: prepare scripts
	$(Q)$(MAKE) $(build)=$(boot)/dts MACHINE=$(MACHINE) $@

# We use MRPROPER_FILES and CLEAN_FILES now
archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

# My testing targets (bypasses dependencies)
bp:;	$(Q)$(MAKE) $(build)=$(boot) MACHINE=$(MACHINE) $(boot)/bootpImage
i zi:;	$(Q)$(MAKE) $(build)=$(boot) MACHINE=$(MACHINE) $@


define archhelp
  echo  '* zImage        - Compressed kernel image (arch/$(ARCH)/boot/zImage)'
  echo  '  Image         - Uncompressed kernel image (arch/$(ARCH)/boot/Image)'
  echo  '* xipImage      - XIP kernel image, if configured (arch/$(ARCH)/boot/xipImage)'
  echo  '  uImage        - U-Boot wrapped zImage'
  echo  '  bootpImage    - Combined zImage and initial RAM disk'
  echo  '                  (supply initrd image via make variable INITRD=<path>)'
  echo  '* dtbs          - Build device tree blobs for enabled boards'
  echo  '  dtbs_install  - Install dtbs to $(INSTALL_DTBS_PATH)'
  echo  '  install       - Install uncompressed kernel'
  echo  '  zinstall      - Install compressed kernel'
  echo  '  uinstall      - Install U-Boot wrapped compressed kernel'
  echo  '                  Install using (your) ~/bin/$(INSTALLKERNEL) or'
  echo  '                  (distribution) /sbin/$(INSTALLKERNEL) or'
  echo  '                  install to $$(INSTALL_PATH) and run lilo'
endef
