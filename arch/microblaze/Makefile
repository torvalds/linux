# SPDX-License-Identifier: GPL-2.0
KBUILD_DEFCONFIG := mmu_defconfig

UTS_SYSNAME = -DUTS_SYSNAME=\"Linux\"

# What CPU version are we building for, and crack it open
# as major.minor.rev
CPU_VER   := $(CONFIG_XILINX_MICROBLAZE0_HW_VER)
CPU_MAJOR := $(word 1, $(subst ., , $(CPU_VER)))
CPU_MINOR := $(word 2, $(subst ., , $(CPU_VER)))
CPU_REV   := $(word 3, $(subst ., , $(CPU_VER)))

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
KBUILD_LDFLAGS += -EB
else
KBUILD_CFLAGS += -mlittle-endian
KBUILD_AFLAGS += -mlittle-endian
KBUILD_LDFLAGS += -EL
endif

CPUFLAGS-1 += $(call cc-option,-mcpu=v$(CPU_VER))

# r31 holds current when in kernel mode
KBUILD_CFLAGS += -ffixed-r31 $(CPUFLAGS-y) $(CPUFLAGS-1) $(CPUFLAGS-2)

libs-y += arch/microblaze/lib/

boot := arch/microblaze/boot

# Are we making a simpleImage.<boardname> target? If so, crack out the boardname
DTB:=$(subst simpleImage.,,$(filter simpleImage.%, $(MAKECMDGOALS)))

export DTB

all: linux.bin

archheaders:
	$(Q)$(MAKE) $(build)=arch/microblaze/kernel/syscalls all

PHONY += linux.bin linux.bin.gz linux.bin.ub
linux.bin.ub linux.bin.gz: linux.bin
linux.bin: vmlinux
linux.bin linux.bin.gz linux.bin.ub:
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@
	@echo 'Kernel: $(boot)/$@ is ready' ' (#'$(or $(KBUILD_BUILD_VERSION),`cat .version`)')'

PHONY += simpleImage.$(DTB)
simpleImage.$(DTB): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(addprefix $(boot)/$@., ub unstrip strip)
	@echo 'Kernel: $(boot)/$@ is ready' ' (#'$(or $(KBUILD_BUILD_VERSION),`cat .version`)')'

define archhelp
  echo '* linux.bin    - Create raw binary'
  echo '  linux.bin.gz - Create compressed raw binary'
  echo '  linux.bin.ub - Create U-Boot wrapped raw binary'
  echo '  simpleImage.<dt> - Create the following images with <dt>.dtb linked in'
  echo '                    simpleImage.<dt>        : raw image'
  echo '                    simpleImage.<dt>.ub     : raw image with U-Boot header'
  echo '                    simpleImage.<dt>.unstrip: ELF (identical to vmlinux)'
  echo '                    simpleImage.<dt>.strip  : stripped ELF'
  echo '  Targets with <dt> embed a device tree blob inside the image'
  echo '  These targets support board with firmware that does not'
  echo '  support passing a device tree directly. Replace <dt> with the'
  echo '  name of a dts file from the arch/microblaze/boot/dts/ directory'
  echo '  (minus the .dts extension).'
endef

MRPROPER_FILES += $(boot)/simpleImage.*
