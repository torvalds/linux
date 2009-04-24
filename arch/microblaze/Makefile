UTS_SYSNAME = -DUTS_SYSNAME=\"uClinux\"

# What CPU vesion are we building for, and crack it open
# as major.minor.rev
CPU_VER=$(subst ",,$(CONFIG_XILINX_MICROBLAZE0_HW_VER) )
CPU_MAJOR=$(shell echo $(CPU_VER) | cut -d '.' -f 1)
CPU_MINOR=$(shell echo $(CPU_VER) | cut -d '.' -f 2)
CPU_REV=$(shell echo $(CPU_VER) | cut -d '.' -f 3)

export CPU_VER CPU_MAJOR CPU_MINOR CPU_REV

# Use cpu-related CONFIG_ vars to set compile options.

# Work out HW multipler support.  This is icky.
# 1. Spartan2 has no HW multiplers.
# 2. MicroBlaze v3.x always uses them, except in Spartan 2
# 3. All other FPGa/CPU ver combos, we can trust the CONFIG_ settings
ifeq (,$(findstring spartan2,$(CONFIG_XILINX_MICROBLAZE0_FAMILY)))
  ifeq ($(CPU_MAJOR),3)
    CPUFLAGS-1 += -mno-xl-soft-mul
  else
    # USE_HW_MUL can be 0, 1, or 2, defining a heirarchy of HW Mul support.
    CPUFLAGS-$(subst 1,,$(CONFIG_XILINX_MICROBLAZE0_USE_HW_MUL)) += -mxl-multiply-high
    CPUFLAGS-$(CONFIG_XILINX_MICROBLAZE0_USE_HW_MUL) += -mno-xl-soft-mul
  endif
endif
CPUFLAGS-$(CONFIG_XILINX_MICROBLAZE0_USE_DIV) += -mno-xl-soft-div
CPUFLAGS-$(CONFIG_XILINX_MICROBLAZE0_USE_BARREL) += -mxl-barrel-shift
CPUFLAGS-$(CONFIG_XILINX_MICROBLAZE0_USE_PCMP) += -mxl-pattern-compare

CPUFLAGS-1 += $(call cc-option,-mcpu=v$(CPU_VER))

# The various CONFIG_XILINX cpu features options are integers 0/1/2...
# rather than bools y/n
CFLAGS += $(CPUFLAGS-1)
CFLAGS += $(CPUFLAGS-2)

# r31 holds current when in kernel mode
CFLAGS += -ffixed-r31

LDFLAGS_BLOB := --format binary --oformat elf32-microblaze

LIBGCC := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

head-y		:= arch/microblaze/kernel/head.o
libs-y		+= arch/microblaze/lib/ $(LIBGCC)
core-y		+= arch/microblaze/kernel/ arch/microblaze/mm/ \
		   arch/microblaze/platform/

boot := arch/$(ARCH)/boot

# defines filename extension depending memory management type
ifeq ($(CONFIG_MMU),)
MMUEXT		:= -nommu
endif
export	MMUEXT

all: linux.bin

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

linux.bin linux.bin.gz: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

define archhelp
  echo  '* linux.bin    - Create raw binary'
  echo  '  linux.bin.gz - Create compressed raw binary'
endef
