#
# arch/blackfin/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#


CROSS_COMPILE    ?= bfin-uclinux-
LDFLAGS_vmlinux  := -X
OBJCOPYFLAGS     := -O binary -R .note -R .comment -S
GZFLAGS          := -9

KBUILD_CFLAGS           += $(call cc-option,-mno-fdpic)
KBUILD_AFLAGS           += $(call cc-option,-mno-fdpic)
CFLAGS_MODULE    += -mlong-calls
KALLSYMS         += --symbol-prefix=_

KBUILD_DEFCONFIG := BF537-STAMP_defconfig

# setup the machine name and the machine dependent settings
machine-$(CONFIG_BF522) := bf527
machine-$(CONFIG_BF525) := bf527
machine-$(CONFIG_BF527) := bf527
machine-$(CONFIG_BF531) := bf533
machine-$(CONFIG_BF532) := bf533
machine-$(CONFIG_BF533) := bf533
machine-$(CONFIG_BF534) := bf537
machine-$(CONFIG_BF536) := bf537
machine-$(CONFIG_BF537) := bf537
machine-$(CONFIG_BF542) := bf548
machine-$(CONFIG_BF544) := bf548
machine-$(CONFIG_BF548) := bf548
machine-$(CONFIG_BF549) := bf548
machine-$(CONFIG_BF561) := bf561
MACHINE := $(machine-y)
export MACHINE

cpu-$(CONFIG_BF522) := bf522
cpu-$(CONFIG_BF525) := bf525
cpu-$(CONFIG_BF527) := bf527
cpu-$(CONFIG_BF531) := bf531
cpu-$(CONFIG_BF532) := bf532
cpu-$(CONFIG_BF533) := bf533
cpu-$(CONFIG_BF534) := bf534
cpu-$(CONFIG_BF536) := bf536
cpu-$(CONFIG_BF537) := bf537
cpu-$(CONFIG_BF542) := bf542
cpu-$(CONFIG_BF544) := bf544
cpu-$(CONFIG_BF548) := bf548
cpu-$(CONFIG_BF549) := bf549
cpu-$(CONFIG_BF561) := bf561

rev-$(CONFIG_BF_REV_0_0)  := 0.0
rev-$(CONFIG_BF_REV_0_1)  := 0.1
rev-$(CONFIG_BF_REV_0_2)  := 0.2
rev-$(CONFIG_BF_REV_0_3)  := 0.3
rev-$(CONFIG_BF_REV_0_4)  := 0.4
rev-$(CONFIG_BF_REV_0_5)  := 0.5
rev-$(CONFIG_BF_REV_NONE) := none
rev-$(CONFIG_BF_REV_ANY)  := any

KBUILD_CFLAGS += -mcpu=$(cpu-y)-$(rev-y)
KBUILD_AFLAGS += -mcpu=$(cpu-y)-$(rev-y)

head-y   := arch/$(ARCH)/mach-$(MACHINE)/head.o arch/$(ARCH)/kernel/init_task.o

core-y   += arch/$(ARCH)/kernel/ arch/$(ARCH)/mm/ arch/$(ARCH)/mach-common/

# If we have a machine-specific directory, then include it in the build.
ifneq ($(machine-y),)
core-y   += arch/$(ARCH)/mach-$(MACHINE)/
core-y   += arch/$(ARCH)/mach-$(MACHINE)/boards/
endif

libs-y   += arch/$(ARCH)/lib/

drivers-$(CONFIG_OPROFILE) += arch/$(ARCH)/oprofile/



#	Update machine arch symlinks if something which affects
#	them changed.  We use .mach to indicate when they were updated
#	last, otherwise make uses the target directory mtime.

include/asm-blackfin/.mach: $(wildcard include/config/arch/*.h) include/config/auto.conf
	@echo '  SYMLINK include/asm-$(ARCH)/mach-$(MACHINE) -> include/asm-$(ARCH)/mach'
ifneq ($(KBUILD_SRC),)
	$(Q)mkdir -p include/asm-$(ARCH)
	$(Q)ln -fsn $(srctree)/include/asm-$(ARCH)/mach-$(MACHINE) include/asm-$(ARCH)/mach
else
	$(Q)ln -fsn mach-$(MACHINE) include/asm-$(ARCH)/mach
endif
	@touch $@

CLEAN_FILES += \
	include/asm-$(ARCH)/asm-offsets.h \
	arch/$(ARCH)/kernel/asm-offsets.s \
	include/asm-$(ARCH)/mach \
	include/asm-$(ARCH)/.mach

archprepare: include/asm-blackfin/.mach
archclean:
	$(Q)$(MAKE) $(clean)=$(boot)


boot := arch/$(ARCH)/boot
BOOT_TARGETS = vmImage
PHONY += $(BOOT_TARGETS) install
KBUILD_IMAGE := $(boot)/vmImage

all: vmImage

$(BOOT_TARGETS): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

install:
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(KBUILD_IMAGE) install

define archhelp
  echo  '* vmImage         - Kernel-only image for U-Boot (arch/$(ARCH)/boot/vmImage)'
  echo  '  install         - Install kernel using'
  echo  '                     (your) ~/bin/$(CROSS_COMPILE)installkernel or'
  echo  '                     (distribution) PATH: $(CROSS_COMPILE)installkernel or'
  echo  '                     install to $$(INSTALL_PATH)'
endef
