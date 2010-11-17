#
# arch/blackfin/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

ifeq ($(CROSS_COMPILE),)
CROSS_COMPILE    := bfin-uclinux-
endif
LDFLAGS_vmlinux  := -X
OBJCOPYFLAGS     := -O binary -R .note -R .comment -S
GZFLAGS          := -9

KBUILD_CFLAGS           += $(call cc-option,-mno-fdpic)
ifeq ($(CONFIG_ROMKERNEL),y)
KBUILD_CFLAGS           += -mlong-calls
endif
KBUILD_AFLAGS           += $(call cc-option,-mno-fdpic)
KBUILD_CFLAGS_MODULE    += -mlong-calls
LDFLAGS                 += -m elf32bfin
KALLSYMS         += --symbol-prefix=_

KBUILD_DEFCONFIG := BF537-STAMP_defconfig

# setup the machine name and the machine dependent settings
machine-$(CONFIG_BF512)  := bf518
machine-$(CONFIG_BF514)  := bf518
machine-$(CONFIG_BF516)  := bf518
machine-$(CONFIG_BF518)  := bf518
machine-$(CONFIG_BF522)  := bf527
machine-$(CONFIG_BF523)  := bf527
machine-$(CONFIG_BF524)  := bf527
machine-$(CONFIG_BF525)  := bf527
machine-$(CONFIG_BF526)  := bf527
machine-$(CONFIG_BF527)  := bf527
machine-$(CONFIG_BF531)  := bf533
machine-$(CONFIG_BF532)  := bf533
machine-$(CONFIG_BF533)  := bf533
machine-$(CONFIG_BF534)  := bf537
machine-$(CONFIG_BF536)  := bf537
machine-$(CONFIG_BF537)  := bf537
machine-$(CONFIG_BF538)  := bf538
machine-$(CONFIG_BF539)  := bf538
machine-$(CONFIG_BF542)  := bf548
machine-$(CONFIG_BF542M) := bf548
machine-$(CONFIG_BF544)  := bf548
machine-$(CONFIG_BF544M) := bf548
machine-$(CONFIG_BF547)  := bf548
machine-$(CONFIG_BF547M) := bf548
machine-$(CONFIG_BF548)  := bf548
machine-$(CONFIG_BF548M) := bf548
machine-$(CONFIG_BF549)  := bf548
machine-$(CONFIG_BF549M) := bf548
machine-$(CONFIG_BF561)  := bf561
MACHINE := $(machine-y)
export MACHINE

cpu-$(CONFIG_BF512)  := bf512
cpu-$(CONFIG_BF514)  := bf514
cpu-$(CONFIG_BF516)  := bf516
cpu-$(CONFIG_BF518)  := bf518
cpu-$(CONFIG_BF522)  := bf522
cpu-$(CONFIG_BF523)  := bf523
cpu-$(CONFIG_BF524)  := bf524
cpu-$(CONFIG_BF525)  := bf525
cpu-$(CONFIG_BF526)  := bf526
cpu-$(CONFIG_BF527)  := bf527
cpu-$(CONFIG_BF531)  := bf531
cpu-$(CONFIG_BF532)  := bf532
cpu-$(CONFIG_BF533)  := bf533
cpu-$(CONFIG_BF534)  := bf534
cpu-$(CONFIG_BF536)  := bf536
cpu-$(CONFIG_BF537)  := bf537
cpu-$(CONFIG_BF538)  := bf538
cpu-$(CONFIG_BF539)  := bf539
cpu-$(CONFIG_BF542)  := bf542
cpu-$(CONFIG_BF542M) := bf542m
cpu-$(CONFIG_BF544)  := bf544
cpu-$(CONFIG_BF544M) := bf544m
cpu-$(CONFIG_BF547)  := bf547
cpu-$(CONFIG_BF547M) := bf547m
cpu-$(CONFIG_BF548)  := bf548
cpu-$(CONFIG_BF548M) := bf548m
cpu-$(CONFIG_BF549)  := bf549
cpu-$(CONFIG_BF549M) := bf549m
cpu-$(CONFIG_BF561)  := bf561

rev-$(CONFIG_BF_REV_0_0)  := 0.0
rev-$(CONFIG_BF_REV_0_1)  := 0.1
rev-$(CONFIG_BF_REV_0_2)  := 0.2
rev-$(CONFIG_BF_REV_0_3)  := 0.3
rev-$(CONFIG_BF_REV_0_4)  := 0.4
rev-$(CONFIG_BF_REV_0_5)  := 0.5
rev-$(CONFIG_BF_REV_0_6)  := 0.6
rev-$(CONFIG_BF_REV_NONE) := none
rev-$(CONFIG_BF_REV_ANY)  := any

KBUILD_CFLAGS += -mcpu=$(cpu-y)-$(rev-y)
KBUILD_AFLAGS += -mcpu=$(cpu-y)-$(rev-y)

# - we utilize the silicon rev from the toolchain, so move it over to the checkflags
CHECKFLAGS_SILICON = $(shell echo "" | $(CPP) $(KBUILD_CFLAGS) -dD - 2>/dev/null | awk '$$2 == "__SILICON_REVISION__" { print $$3 }')
CHECKFLAGS += -D__SILICON_REVISION__=$(CHECKFLAGS_SILICON) -D__bfin__

head-y   := arch/$(ARCH)/kernel/init_task.o

core-y   += arch/$(ARCH)/kernel/ arch/$(ARCH)/mm/ arch/$(ARCH)/mach-common/

# If we have a machine-specific directory, then include it in the build.
ifneq ($(machine-y),)
core-y   += arch/$(ARCH)/mach-$(MACHINE)/
core-y   += arch/$(ARCH)/mach-$(MACHINE)/boards/
endif

ifeq ($(CONFIG_MPU),y)
core-y	+= arch/$(ARCH)/kernel/cplb-mpu/
else
core-y	+= arch/$(ARCH)/kernel/cplb-nompu/
endif

drivers-$(CONFIG_OPROFILE) += arch/$(ARCH)/oprofile/

libs-y   += arch/$(ARCH)/lib/

machdirs	:= $(patsubst %,arch/blackfin/mach-%/, $(machine-y))

KBUILD_CFLAGS += -Iarch/$(ARCH)/include/
KBUILD_CFLAGS += -Iarch/$(ARCH)/mach-$(MACHINE)/include

KBUILD_CPPFLAGS	+= $(patsubst %,-I$(srctree)/%include,$(machdirs))

CLEAN_FILES += \
	arch/$(ARCH)/kernel/asm-offsets.s \

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

INSTALL_PATH ?= /tftpboot
boot := arch/$(ARCH)/boot
BOOT_TARGETS = vmImage vmImage.bin vmImage.bz2 vmImage.gz vmImage.lzma vmImage.lzo vmImage.xip
PHONY += $(BOOT_TARGETS) install
KBUILD_IMAGE := $(boot)/vmImage

all: vmImage

$(BOOT_TARGETS): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

install:
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(KBUILD_IMAGE) install

define archhelp
  echo  '* vmImage         - Alias to selected kernel format (vmImage.gz by default)'
  echo  '  vmImage.bin     - Uncompressed Kernel-only image for U-Boot (arch/$(ARCH)/boot/vmImage.bin)'
  echo  '  vmImage.bz2     - Kernel-only image for U-Boot (arch/$(ARCH)/boot/vmImage.bz2)'
  echo  '* vmImage.gz      - Kernel-only image for U-Boot (arch/$(ARCH)/boot/vmImage.gz)'
  echo  '  vmImage.lzma    - Kernel-only image for U-Boot (arch/$(ARCH)/boot/vmImage.lzma)'
  echo  '  vmImage.lzo     - Kernel-only image for U-Boot (arch/$(ARCH)/boot/vmImage.lzo)'
  echo  '  vmImage.xip     - XIP Kernel-only image for U-Boot (arch/$(ARCH)/boot/vmImage.xip)'
  echo  '  install         - Install kernel using'
  echo  '                     (your) ~/bin/$(INSTALLKERNEL) or'
  echo  '                     (distribution) PATH: $(INSTALLKERNEL) or'
  echo  '                     install to $$(INSTALL_PATH)'
endef
