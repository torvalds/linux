#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2004-2006 Atmel Corporation.

# Default target when executing plain make
.PHONY: all
all: uImage vmlinux.elf

KBUILD_DEFCONFIG	:= atstk1002_defconfig

KBUILD_CFLAGS	+= -pipe -fno-builtin -mno-pic
KBUILD_AFLAGS	+= -mrelax -mno-pic
CFLAGS_MODULE	+= -mno-relax
LDFLAGS_vmlinux	+= --relax

cpuflags-$(CONFIG_CPU_AT32AP7000)	+= -mcpu=ap7000

KBUILD_CFLAGS	+= $(cpuflags-y)
KBUILD_AFLAGS	+= $(cpuflags-y)

CHECKFLAGS	+= -D__avr32__ -D__BIG_ENDIAN

head-$(CONFIG_LOADER_U_BOOT)		+= arch/avr32/boot/u-boot/head.o
head-y					+= arch/avr32/kernel/head.o
core-$(CONFIG_PLATFORM_AT32AP)		+= arch/avr32/mach-at32ap/
core-$(CONFIG_BOARD_ATSTK1000)		+= arch/avr32/boards/atstk1000/
core-$(CONFIG_BOARD_ATNGW100)		+= arch/avr32/boards/atngw100/
core-$(CONFIG_LOADER_U_BOOT)		+= arch/avr32/boot/u-boot/
core-y					+= arch/avr32/kernel/
core-y					+= arch/avr32/mm/
libs-y					+= arch/avr32/lib/

archincdir-$(CONFIG_PLATFORM_AT32AP)	:= arch-at32ap

include/asm-avr32/.arch: $(wildcard include/config/platform/*.h) include/config/auto.conf
	@echo '  SYMLINK include/asm-avr32/arch -> include/asm-avr32/$(archincdir-y)'
ifneq ($(KBUILD_SRC),)
	$(Q)mkdir -p include/asm-avr32
	$(Q)ln -fsn $(srctree)/include/asm-avr32/$(archincdir-y) include/asm-avr32/arch
else
	$(Q)ln -fsn $(archincdir-y) include/asm-avr32/arch
endif
	@touch $@

archprepare: include/asm-avr32/.arch

CLEAN_FILES += include/asm-avr32/.arch include/asm-avr32/arch

BOOT_TARGETS := vmlinux.elf vmlinux.bin uImage uImage.srec

.PHONY: $(BOOT_TARGETS) install

boot := arch/$(ARCH)/boot/images

             KBUILD_IMAGE := $(boot)/uImage
vmlinux.elf: KBUILD_IMAGE := $(boot)/vmlinux.elf
vmlinux.cso: KBUILD_IMAGE := $(boot)/vmlinux.cso
uImage.srec: KBUILD_IMAGE := $(boot)/uImage.srec
uImage:      KBUILD_IMAGE := $(boot)/uImage

quiet_cmd_listing = LST     $@
      cmd_listing = avr32-linux-objdump $(OBJDUMPFLAGS) -lS $< > $@
quiet_cmd_disasm  = DIS     $@
      cmd_disasm  = avr32-linux-objdump $(OBJDUMPFLAGS) -d $< > $@

vmlinux.elf vmlinux.bin uImage.srec uImage vmlinux.cso: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

install: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(KBUILD_IMAGE) $@

vmlinux.s: vmlinux
	$(call if_changed,disasm)

vmlinux.lst: vmlinux
	$(call if_changed,listing)

CLEAN_FILES += vmlinux.s vmlinux.lst

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

define archhelp
  @echo '* vmlinux.elf		- ELF image with load address 0'
  @echo '  vmlinux.cso		- PathFinder CSO image'
  @echo '* uImage		- Create a bootable image for U-Boot'
endef
