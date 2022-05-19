# SPDX-License-Identifier: GPL-2.0
#
# Makefile for vdso32
#

# Absolute relocation type $(ARCH_REL_TYPE_ABS) needs to be defined before
# the inclusion of generic Makefile.
ARCH_REL_TYPE_ABS := R_ARM_JUMP_SLOT|R_ARM_GLOB_DAT|R_ARM_ABS32
include $(srctree)/lib/vdso/Makefile

# Same as cc-*option, but using CC_COMPAT instead of CC
ifeq ($(CONFIG_CC_IS_CLANG), y)
CC_COMPAT ?= $(CC)
CC_COMPAT += --target=arm-linux-gnueabi
else
CC_COMPAT ?= $(CROSS_COMPILE_COMPAT)gcc
endif

ifeq ($(CONFIG_LD_IS_LLD), y)
LD_COMPAT ?= $(LD)
else
LD_COMPAT ?= $(CROSS_COMPILE_COMPAT)ld
endif

cc32-option = $(call try-run,\
        $(CC_COMPAT) $(1) -c -x c /dev/null -o "$$TMP",$(1),$(2))
cc32-disable-warning = $(call try-run,\
	$(CC_COMPAT) -W$(strip $(1)) -c -x c /dev/null -o "$$TMP",-Wno-$(strip $(1)))
cc32-as-instr = $(call try-run,\
	printf "%b\n" "$(1)" | $(CC_COMPAT) $(VDSO_AFLAGS) -c -x assembler -o "$$TMP" -,$(2),$(3))

# We cannot use the global flags to compile the vDSO files, the main reason
# being that the 32-bit compiler may be older than the main (64-bit) compiler
# and therefore may not understand flags set using $(cc-option ...). Besides,
# arch-specific options should be taken from the arm Makefile instead of the
# arm64 one.
# As a result we set our own flags here.

# KBUILD_CPPFLAGS and NOSTDINC_FLAGS from top-level Makefile
VDSO_CPPFLAGS := -DBUILD_VDSO -D__KERNEL__ -nostdinc
VDSO_CPPFLAGS += -isystem $(shell $(CC_COMPAT) -print-file-name=include 2>/dev/null)
VDSO_CPPFLAGS += $(LINUXINCLUDE)

# Common C and assembly flags
# From top-level Makefile
VDSO_CAFLAGS := $(VDSO_CPPFLAGS)
VDSO_CAFLAGS += $(call cc32-option,-fno-PIE)
ifdef CONFIG_DEBUG_INFO
VDSO_CAFLAGS += -g
endif

# From arm Makefile
VDSO_CAFLAGS += $(call cc32-option,-fno-dwarf2-cfi-asm)
VDSO_CAFLAGS += -mabi=aapcs-linux -mfloat-abi=soft
ifeq ($(CONFIG_CPU_BIG_ENDIAN), y)
VDSO_CAFLAGS += -mbig-endian
else
VDSO_CAFLAGS += -mlittle-endian
endif

# From arm vDSO Makefile
VDSO_CAFLAGS += -fPIC -fno-builtin -fno-stack-protector
VDSO_CAFLAGS += -DDISABLE_BRANCH_PROFILING


# Try to compile for ARMv8. If the compiler is too old and doesn't support it,
# fall back to v7. There is no easy way to check for what architecture the code
# is being compiled, so define a macro specifying that (see arch/arm/Makefile).
VDSO_CAFLAGS += $(call cc32-option,-march=armv8-a -D__LINUX_ARM_ARCH__=8,\
                                   -march=armv7-a -D__LINUX_ARM_ARCH__=7)

VDSO_CFLAGS := $(VDSO_CAFLAGS)
VDSO_CFLAGS += -DENABLE_COMPAT_VDSO=1
# KBUILD_CFLAGS from top-level Makefile
VDSO_CFLAGS += -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs \
               -fno-strict-aliasing -fno-common \
               -Werror-implicit-function-declaration \
               -Wno-format-security \
               -std=gnu89
VDSO_CFLAGS  += -O2
# Some useful compiler-dependent flags from top-level Makefile
VDSO_CFLAGS += $(call cc32-option,-Wdeclaration-after-statement,)
VDSO_CFLAGS += $(call cc32-option,-Wno-pointer-sign)
VDSO_CFLAGS += -fno-strict-overflow
VDSO_CFLAGS += $(call cc32-option,-Werror=strict-prototypes)
VDSO_CFLAGS += -Werror=date-time
VDSO_CFLAGS += $(call cc32-option,-Werror=incompatible-pointer-types)

# The 32-bit compiler does not provide 128-bit integers, which are used in
# some headers that are indirectly included from the vDSO code.
# This hack makes the compiler happy and should trigger a warning/error if
# variables of such type are referenced.
VDSO_CFLAGS += -D__uint128_t='void*'
# Silence some warnings coming from headers that operate on long's
# (on GCC 4.8 or older, there is unfortunately no way to silence this warning)
VDSO_CFLAGS += $(call cc32-disable-warning,shift-count-overflow)
VDSO_CFLAGS += -Wno-int-to-pointer-cast

# Compile as THUMB2 or ARM. Unwinding via frame-pointers in THUMB2 is
# unreliable.
ifeq ($(CONFIG_THUMB2_COMPAT_VDSO), y)
VDSO_CFLAGS += -mthumb -fomit-frame-pointer
else
VDSO_CFLAGS += -marm
endif

VDSO_AFLAGS := $(VDSO_CAFLAGS)
VDSO_AFLAGS += -D__ASSEMBLY__

# Check for binutils support for dmb ishld
dmbinstr := $(call cc32-as-instr,dmb ishld,-DCONFIG_AS_DMB_ISHLD=1)

VDSO_CFLAGS += $(dmbinstr)
VDSO_AFLAGS += $(dmbinstr)

# From arm vDSO Makefile
VDSO_LDFLAGS += -Bsymbolic --no-undefined -soname=linux-vdso.so.1
VDSO_LDFLAGS += -z max-page-size=4096 -z common-page-size=4096
VDSO_LDFLAGS += -nostdlib -shared --hash-style=sysv --build-id=sha1


# Borrow vdsomunge.c from the arm vDSO
# We have to use a relative path because scripts/Makefile.host prefixes
# $(hostprogs) with $(obj)
munge := ../../../arm/vdso/vdsomunge
hostprogs := $(munge)

c-obj-vdso := note.o
c-obj-vdso-gettimeofday := vgettimeofday.o

ifneq ($(c-gettimeofday-y),)
VDSO_CFLAGS_gettimeofday_o += -include $(c-gettimeofday-y)
endif

VDSO_CFLAGS_REMOVE_vgettimeofday.o = $(CC_FLAGS_FTRACE) -Os

# Build rules
targets := $(c-obj-vdso) $(c-obj-vdso-gettimeofday) $(asm-obj-vdso) vdso.so vdso.so.dbg vdso.so.raw
c-obj-vdso := $(addprefix $(obj)/, $(c-obj-vdso))
c-obj-vdso-gettimeofday := $(addprefix $(obj)/, $(c-obj-vdso-gettimeofday))
asm-obj-vdso := $(addprefix $(obj)/, $(asm-obj-vdso))
obj-vdso := $(c-obj-vdso) $(c-obj-vdso-gettimeofday) $(asm-obj-vdso)

targets += vdso.lds
CPPFLAGS_vdso.lds += -P -C -U$(ARCH)

include/generated/vdso32-offsets.h: $(obj)/vdso.so.dbg FORCE
	$(call if_changed,vdsosym)

# Strip rule for vdso.so
$(obj)/vdso.so: OBJCOPYFLAGS := -S
$(obj)/vdso.so: $(obj)/vdso.so.dbg FORCE
	$(call if_changed,objcopy)

$(obj)/vdso.so.dbg: $(obj)/vdso.so.raw $(obj)/$(munge) FORCE
	$(call if_changed,vdsomunge)

# Link rule for the .so file, .lds has to be first
$(obj)/vdso.so.raw: $(src)/vdso.lds $(obj-vdso) FORCE
	$(call if_changed,vdsold_and_vdso_check)

# Compilation rules for the vDSO sources
$(c-obj-vdso): %.o: %.c FORCE
	$(call if_changed_dep,vdsocc)
$(c-obj-vdso-gettimeofday): %.o: %.c FORCE
	$(call if_changed_dep,vdsocc_gettimeofday)
$(asm-obj-vdso): %.o: %.S FORCE
	$(call if_changed_dep,vdsoas)

# Actual build commands
quiet_cmd_vdsold_and_vdso_check = LD32    $@
      cmd_vdsold_and_vdso_check = $(cmd_vdsold); $(cmd_vdso_check)

quiet_cmd_vdsold = LD32    $@
      cmd_vdsold = $(LD_COMPAT) $(VDSO_LDFLAGS) \
                   -T $(filter %.lds,$^) $(filter %.o,$^) -o $@
quiet_cmd_vdsocc = CC32    $@
      cmd_vdsocc = $(CC_COMPAT) -Wp,-MD,$(depfile) $(VDSO_CFLAGS) -c -o $@ $<
quiet_cmd_vdsocc_gettimeofday = CC32    $@
      cmd_vdsocc_gettimeofday = $(CC_COMPAT) -Wp,-MD,$(depfile) $(VDSO_CFLAGS) $(VDSO_CFLAGS_gettimeofday_o) -c -o $@ $<
quiet_cmd_vdsoas = AS32    $@
      cmd_vdsoas = $(CC_COMPAT) -Wp,-MD,$(depfile) $(VDSO_AFLAGS) -c -o $@ $<

quiet_cmd_vdsomunge = MUNGE   $@
      cmd_vdsomunge = $(obj)/$(munge) $< $@

# Generate vDSO offsets using helper script (borrowed from the 64-bit vDSO)
gen-vdsosym := $(srctree)/$(src)/../vdso/gen_vdso_offsets.sh
quiet_cmd_vdsosym = VDSOSYM $@
# The AArch64 nm should be able to read an AArch32 binary
      cmd_vdsosym = $(NM) $< | $(gen-vdsosym) | LC_ALL=C sort > $@

# Install commands for the unstripped file
quiet_cmd_vdso_install = INSTALL32 $@
      cmd_vdso_install = cp $(obj)/$@.dbg $(MODLIB)/vdso/vdso32.so

vdso.so: $(obj)/vdso.so.dbg
	@mkdir -p $(MODLIB)/vdso
	$(call cmd,vdso_install)

vdso_install: vdso.so
