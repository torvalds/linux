# SPDX-License-Identifier: GPL-2.0
KBUILD_DEFCONFIG := mmu_defconfig

ifeq ($(CONFIG_MMU),y)
UTS_SYSNAME = -DUTS_SYSNAME=\"Linux\"
else
UTS_SYSNAME = -DUTS_SYSNAME=\"uClinux\"
endif

# What CPU vesion are we building for, and crack it open
# as major.minor.rev
CPU_VER   := $(shell echo $(CONFIG_XILINX_MICROBLAZE0_HW_VER))
CPU_MAJOR := $(shell echo $(CPU_VER) | cut -d '.' -f 1)
CPU_MINOR := $(shell echo $(CPU_VER) | cut -d '.' -f 2)
CPU_REV   := $(shell echo $(CPU_VER) | cut -d '.' -f 3)

export CPU_VER CPU_MAJOR CPU_MINOR CPU_REV

# Use cpu-related CONFIG_ vars to set compile options.
# The various CONFIG_XILINX cpu features options are integers 0/1/2...
# rather than bools y/n

# Work out HW multipler support. This is tricky.
# 1. Spartan2 has no HW multipliers.
# 2. MicroBlaze v3.x always uses them, except in Spartan 2
# 3. All other FPGa/CPU ver combos, we can trust the CONFIG_ settings
ifeq (,$(findstring spartan2,$(CONFIG_XILINX_MICROBLAZE0_FAMILY)))
  ifeq ($(CPU_MAJOR),3)
    CPUFLAGS-1 += -mno-xl-soft-mul
  else
    # USE_HW_MUL can be 0, 1, or 2, defining a hierarchy of HW Mul support.
    CPUFLAGS-$(subst 1,,$(CONFIG_XILINX_MICROBLAZE0_USE_HW_MUL)) += -mxl-multiply-high
    CPUFLAGS-$(CONFIG_XILINX_MICROBLAZE0_USE_HW_MUL) += -mno-xl-soft-mul
  endif
endif
CPUFLAGS-$(CONFIG_XILINX_MICROBLAZE0_USE_DIV) += -mno-xl-soft-div
CPUFLAGS-$(CONFIG_XILINX_MICROBLAZE0_USE_BARREL) += -mxl-barrel-shift
CPUFLAGS-$(CONFIG_XILINX_MICROBLAZE0_USE_PCMP_INSTR) += -mxl-pattern-compare

ifdef CONFIG_CPU_BIG_ENDIAN
KBUILD_CFLAGS += -mbig-endian
KBUILD_AFLAGS += -mbig-endian
LDFLAGS += -EB
else
KBUILD_CFLAGS += -mlittle-endian
KBUILD_AFLAGS += -mlittle-endian
LDFLAGS += -EL
endif

CPUFLAGS-1 += $(call cc-option,-mcpu=v$(CPU_VER))

# r31 holds current when in kernel mode
KBUILD_CFLAGS += -ffixed-r31 $(CPUFLAGS-y) $(CPUFLAGS-1) $(CPUFLAGS-2)

head-y := arch/microblaze/kernel/head.o
libs-y += arch/microblaze/lib/
core-y += arch/microblaze/kernel/
core-y += arch/microblaze/mm/
core-$(CONFIG_PCI) += arch/microblaze/pci/

drivers-$(CONFIG_OPROFILE) += arch/microblaze/oprofile/

boot := arch/microblaze/boot

# Are we making a simpleImage.<boardname> target? If so, crack out the boardname
DTB:=$(subst simpleImage.,,$(filter simpleImage.%, $(MAKECMDGOALS)))

ifneq ($(DTB),)
	core-y	+= $(boot)/dts/
endif

# defines filename extension depending memory management type
ifeq ($(CONFIG_MMU),)
MMU := -nommu
endif

export MMU DTB

all: linux.bin

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

linux.bin linux.bin.gz linux.bin.ub: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

simpleImage.%: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

define archhelp
  echo '* linux.bin    - Create raw binary'
  echo '  linux.bin.gz - Create compressed raw binary'
  echo '  linux.bin.ub - Create U-Boot wrapped raw binary'
  echo '  simpleImage.<dt> - ELF image with $(arch)/boot/dts/<dt>.dts linked in'
  echo '                   - stripped elf with fdt blob'
  echo '  simpleImage.<dt>.unstrip - full ELF image with fdt blob'
  echo '  *_defconfig      - Select default config from arch/microblaze/configs'
  echo ''
  echo '  Targets with <dt> embed a device tree blob inside the image'
  echo '  These targets support board with firmware that does not'
  echo '  support passing a device tree directly. Replace <dt> with the'
  echo '  name of a dts file from the arch/microblaze/boot/dts/ directory'
  echo '  (minus the .dts extension).'
endef

MRPROPER_FILES += $(boot)/simpleImage.*
