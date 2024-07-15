# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for compat_vdso
#

# Symbols present in the compat_vdso
compat_vdso-syms  = rt_sigreturn
compat_vdso-syms += getcpu
compat_vdso-syms += flush_icache

COMPAT_CC := $(CC)
COMPAT_LD := $(LD)

# binutils 2.35 does not support the zifencei extension, but in the ISA
# spec 20191213, G stands for IMAFD_ZICSR_ZIFENCEI.
ifdef CONFIG_TOOLCHAIN_NEEDS_EXPLICIT_ZICSR_ZIFENCEI
	COMPAT_CC_FLAGS := -march=rv32g -mabi=ilp32
else
	COMPAT_CC_FLAGS := -march=rv32imafd -mabi=ilp32
endif
COMPAT_LD_FLAGS := -melf32lriscv

# Disable attributes, as they're useless and break the build.
COMPAT_CC_FLAGS += $(call cc-option,-mno-riscv-attribute)
COMPAT_CC_FLAGS += $(call as-option,-Wa$(comma)-mno-arch-attr)

# Files to link into the compat_vdso
obj-compat_vdso = $(patsubst %, %.o, $(compat_vdso-syms)) note.o

# Build rules
targets := $(obj-compat_vdso) compat_vdso.so compat_vdso.so.dbg compat_vdso.lds
obj-compat_vdso := $(addprefix $(obj)/, $(obj-compat_vdso))

obj-y += compat_vdso.o
CPPFLAGS_compat_vdso.lds += -P -C -DCOMPAT_VDSO -U$(ARCH)

# Force dependency
$(obj)/compat_vdso.o: $(obj)/compat_vdso.so

# link rule for the .so file, .lds has to be first
$(obj)/compat_vdso.so.dbg: $(obj)/compat_vdso.lds $(obj-compat_vdso) FORCE
	$(call if_changed,compat_vdsold)
LDFLAGS_compat_vdso.so.dbg = -shared -S -soname=linux-compat_vdso.so.1 \
	--build-id=sha1 --hash-style=both --eh-frame-hdr

$(obj-compat_vdso): %.o: %.S FORCE
	$(call if_changed_dep,compat_vdsoas)

# strip rule for the .so file
$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

# Generate VDSO offsets using helper script
gen-compat_vdsosym := $(src)/gen_compat_vdso_offsets.sh
quiet_cmd_compat_vdsosym = VDSOSYM $@
	cmd_compat_vdsosym = $(NM) $< | $(gen-compat_vdsosym) | LC_ALL=C sort > $@

include/generated/compat_vdso-offsets.h: $(obj)/compat_vdso.so.dbg FORCE
	$(call if_changed,compat_vdsosym)

# actual build commands
# The DSO images are built using a special linker script
# Make sure only to export the intended __compat_vdso_xxx symbol offsets.
quiet_cmd_compat_vdsold = VDSOLD  $@
      cmd_compat_vdsold = $(COMPAT_LD) $(ld_flags) $(COMPAT_LD_FLAGS) -T $(filter-out FORCE,$^) -o $@.tmp && \
                   $(OBJCOPY) $(patsubst %, -G __compat_vdso_%, $(compat_vdso-syms)) $@.tmp $@ && \
                   rm $@.tmp

# actual build commands
quiet_cmd_compat_vdsoas = VDSOAS  $@
      cmd_compat_vdsoas = $(COMPAT_CC) $(a_flags) $(COMPAT_CC_FLAGS) -c -o $@ $<
