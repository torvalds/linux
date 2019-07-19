# SPDX-License-Identifier: GPL-2.0
#
# Building a vDSO image for AArch64.
#
# Author: Will Deacon <will.deacon@arm.com>
# Heavily based on the vDSO Makefiles for other archs.
#

# Absolute relocation type $(ARCH_REL_TYPE_ABS) needs to be defined before
# the inclusion of generic Makefile.
ARCH_REL_TYPE_ABS := R_AARCH64_JUMP_SLOT|R_AARCH64_GLOB_DAT|R_AARCH64_ABS64
include $(srctree)/lib/vdso/Makefile

obj-vdso := vgettimeofday.o note.o sigreturn.o

# Build rules
targets := $(obj-vdso) vdso.so vdso.so.dbg
obj-vdso := $(addprefix $(obj)/, $(obj-vdso))

ccflags-y := -shared -fno-common -fno-builtin
ccflags-y += -nostdlib -Wl,-soname=linux-vdso.so.1 \
		$(call cc-ldoption, -Wl$(comma)--hash-style=sysv)
ccflags-y += $(DISABLE_LTO)

CFLAGS_REMOVE_vgettimeofday.o += $(CC_FLAGS_SCS)

ccflags-y := -fno-common -fno-builtin -fno-stack-protector
ccflags-y += -DDISABLE_BRANCH_PROFILING

VDSO_LDFLAGS := -Bsymbolic

CFLAGS_REMOVE_vgettimeofday.o = $(CC_FLAGS_FTRACE) -Os
KBUILD_CFLAGS			+= $(DISABLE_LTO)
KASAN_SANITIZE			:= n
UBSAN_SANITIZE			:= n
OBJECT_FILES_NON_STANDARD	:= y
KCOV_INSTRUMENT			:= n

CFLAGS_vgettimeofday.o = -O2 -mcmodel=tiny

ifneq ($(c-gettimeofday-y),)
  CFLAGS_vgettimeofday.o += -include $(c-gettimeofday-y)
endif

# Clang versions less than 8 do not support -mcmodel=tiny
ifeq ($(CONFIG_CC_IS_CLANG), y)
  ifeq ($(shell test $(CONFIG_CLANG_VERSION) -lt 80000; echo $$?),0)
    CFLAGS_REMOVE_vgettimeofday.o += -mcmodel=tiny
  endif
endif

# Disable gcov profiling for VDSO code
GCOV_PROFILE := n

# Workaround for bare-metal (ELF) toolchains that neglect to pass -shared
# down to collect2, resulting in silent corruption of the vDSO image.
ccflags-y += -Wl,-shared

obj-y += vdso.o
extra-y += vdso.lds
CPPFLAGS_vdso.lds += -P -C -U$(ARCH)

# Force dependency (incbin is bad)
$(obj)/vdso.o : $(obj)/vdso.so

# Link rule for the .so file, .lds has to be first
$(obj)/vdso.so.dbg: $(src)/vdso.lds $(obj-vdso)
	$(call if_changed,vdsold)
	$(call if_changed,vdso_check)

# Strip rule for the .so file
$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

# Generate VDSO offsets using helper script
gen-vdsosym := $(srctree)/$(src)/gen_vdso_offsets.sh
quiet_cmd_vdsosym = VDSOSYM $@
define cmd_vdsosym
	$(NM) $< | $(gen-vdsosym) | LC_ALL=C sort > $@
endef

include/generated/vdso-offsets.h: $(obj)/vdso.so.dbg FORCE
	$(call if_changed,vdsosym)

# Actual build commands
quiet_cmd_vdsold = VDSOL   $@
      cmd_vdsold = $(CC) $(c_flags) -Wl,-n -Wl,-T $^ -o $@
quiet_cmd_vdsold_and_vdso_check = LD      $@
      cmd_vdsold_and_vdso_check = $(cmd_ld); $(cmd_vdso_check)

# Install commands for the unstripped file
quiet_cmd_vdso_install = INSTALL $@
      cmd_vdso_install = cp $(obj)/$@.dbg $(MODLIB)/vdso/$@

vdso.so: $(obj)/vdso.so.dbg
	@mkdir -p $(MODLIB)/vdso
	$(call cmd,vdso_install)

vdso_install: vdso.so
