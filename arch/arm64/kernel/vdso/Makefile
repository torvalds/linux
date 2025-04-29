# SPDX-License-Identifier: GPL-2.0
#
# Building a vDSO image for AArch64.
#
# Author: Will Deacon <will.deacon@arm.com>
# Heavily based on the vDSO Makefiles for other archs.
#

# Include the generic Makefile to check the built vdso.
include $(srctree)/lib/vdso/Makefile.include

obj-vdso := vgettimeofday.o note.o sigreturn.o vgetrandom.o vgetrandom-chacha.o

# Build rules
targets := $(obj-vdso) vdso.so vdso.so.dbg
obj-vdso := $(addprefix $(obj)/, $(obj-vdso))

btildflags-$(CONFIG_ARM64_BTI_KERNEL) += -z force-bti

# -Bsymbolic has been added for consistency with arm, the compat vDSO and
# potential future proofing if we end up with internal calls to the exported
# routines, as x86 does (see 6f121e548f83 ("x86, vdso: Reimplement vdso.so
# preparation in build-time C")).
ldflags-y := -shared -soname=linux-vdso.so.1 \
	     -Bsymbolic --build-id=sha1 -n $(btildflags-y)

ifdef CONFIG_LD_ORPHAN_WARN
  ldflags-y += --orphan-handling=$(CONFIG_LD_ORPHAN_WARN_LEVEL)
endif

ldflags-y += -T

ccflags-y := -fno-common -fno-builtin -fno-stack-protector -ffixed-x18
ccflags-y += -DDISABLE_BRANCH_PROFILING -DBUILD_VDSO

# -Wmissing-prototypes and -Wmissing-declarations are removed from
# the CFLAGS to make possible to build the kernel with CONFIG_WERROR enabled.
CC_FLAGS_REMOVE_VDSO := $(CC_FLAGS_FTRACE) -Os $(CC_FLAGS_SCS) \
			$(RANDSTRUCT_CFLAGS) $(GCC_PLUGINS_CFLAGS) \
			$(CC_FLAGS_LTO) $(CC_FLAGS_CFI) \
			-Wmissing-prototypes -Wmissing-declarations

CC_FLAGS_ADD_VDSO := -O2 -mcmodel=tiny -fasynchronous-unwind-tables

CFLAGS_REMOVE_vgettimeofday.o = $(CC_FLAGS_REMOVE_VDSO)
CFLAGS_REMOVE_vgetrandom.o = $(CC_FLAGS_REMOVE_VDSO)

CFLAGS_vgettimeofday.o = $(CC_FLAGS_ADD_VDSO)
CFLAGS_vgetrandom.o = $(CC_FLAGS_ADD_VDSO)

ifneq ($(c-gettimeofday-y),)
  CFLAGS_vgettimeofday.o += -include $(c-gettimeofday-y)
endif

ifneq ($(c-getrandom-y),)
  CFLAGS_vgetrandom.o += -include $(c-getrandom-y)
endif

targets += vdso.lds
CPPFLAGS_vdso.lds += -P -C -U$(ARCH)

# Link rule for the .so file, .lds has to be first
$(obj)/vdso.so.dbg: $(obj)/vdso.lds $(obj-vdso) FORCE
	$(call if_changed,vdsold_and_vdso_check)

# Strip rule for the .so file
$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

# Generate VDSO offsets using helper script
gen-vdsosym := $(src)/gen_vdso_offsets.sh
quiet_cmd_vdsosym = VDSOSYM $@
      cmd_vdsosym = $(NM) $< | $(gen-vdsosym) | LC_ALL=C sort > $@

include/generated/vdso-offsets.h: $(obj)/vdso.so.dbg FORCE
	$(call if_changed,vdsosym)

# Actual build commands
quiet_cmd_vdsold_and_vdso_check = LD      $@
      cmd_vdsold_and_vdso_check = $(cmd_ld); $(cmd_vdso_check)
