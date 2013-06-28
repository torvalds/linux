#
# metag/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" cleaning up for this architecture.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#               2007,2008,2012 by Imagination Technologies Ltd.
#

LDFLAGS					:=
OBJCOPYFLAGS				:= -O binary -R .note -R .comment -S

checkflags-$(CONFIG_METAG_META12)	+= -DMETAC_1_2
checkflags-$(CONFIG_METAG_META21)	+= -DMETAC_2_1
CHECKFLAGS				+= -D__metag__ $(checkflags-y)

KBUILD_DEFCONFIG			:= meta2_defconfig

sflags-$(CONFIG_METAG_META12)		+= -mmetac=1.2
ifeq ($(CONFIG_METAG_META12),y)
# Only use TBI API 1.4 if DSP is enabled for META12 cores
sflags-$(CONFIG_METAG_DSP)		+= -DTBI_1_4
endif
sflags-$(CONFIG_METAG_META21)		+= -mmetac=2.1 -DTBI_1_4

cflags-$(CONFIG_METAG_FUNCTION_TRACE)	+= -mhwtrace-leaf -mhwtrace-retpc
cflags-$(CONFIG_METAG_META21)		+= -mextensions=bex

KBUILD_CFLAGS				+= -pipe
KBUILD_CFLAGS				+= -ffunction-sections

KBUILD_CFLAGS				+= $(sflags-y) $(cflags-y)
KBUILD_AFLAGS				+= $(sflags-y)

LDFLAGS_vmlinux				:= $(ldflags-y)

head-y					:= arch/metag/kernel/head.o

core-y					+= arch/metag/boot/dts/
core-y					+= arch/metag/kernel/
core-y					+= arch/metag/mm/

libs-y					+= arch/metag/lib/
libs-y					+= arch/metag/tbx/

drivers-$(CONFIG_OPROFILE)		+= arch/metag/oprofile/

boot					:= arch/metag/boot

boot_targets				+= uImage
boot_targets				+= uImage.gz
boot_targets				+= uImage.bz2
boot_targets				+= uImage.xz
boot_targets				+= uImage.lzo
boot_targets				+= uImage.bin
boot_targets				+= vmlinux.bin

PHONY					+= $(boot_targets)

all: vmlinux.bin

$(boot_targets): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

%.dtb %.dtb.S %.dtb.o: scripts
	$(Q)$(MAKE) $(build)=$(boot)/dts $(boot)/dts/$@

dtbs: scripts
	$(Q)$(MAKE) $(build)=$(boot)/dts dtbs

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

define archhelp
  echo  '* vmlinux.bin	- Binary kernel image (arch/$(ARCH)/boot/vmlinux.bin)'
  @echo '  uImage  	- Alias to bootable U-Boot image'
  @echo '  uImage.bin	- Kernel-only image for U-Boot (bin)'
  @echo '  uImage.gz	- Kernel-only image for U-Boot (gzip)'
  @echo '  uImage.bz2	- Kernel-only image for U-Boot (bzip2)'
  @echo '  uImage.xz	- Kernel-only image for U-Boot (xz)'
  @echo '  uImage.lzo	- Kernel-only image for U-Boot (lzo)'
  @echo '  dtbs		- Build device tree blobs for enabled boards'
endef
