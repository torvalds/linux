# SPDX-License-Identifier: GPL-2.0
# List of files in the vdso

KCOV_INSTRUMENT := n

# Include the generic Makefile to check the built vdso.
include $(srctree)/lib/vdso/Makefile
obj-vdso32 = vdso_user_wrapper-32.o note-32.o

# Build rules

targets := $(obj-vdso32) vdso32.so vdso32.so.dbg
obj-vdso32 := $(addprefix $(obj)/, $(obj-vdso32))

KBUILD_AFLAGS += -DBUILD_VDSO
KBUILD_CFLAGS += -DBUILD_VDSO -DDISABLE_BRANCH_PROFILING

KBUILD_AFLAGS_32 := $(filter-out -m64,$(KBUILD_AFLAGS))
KBUILD_AFLAGS_32 += -m31 -s

KBUILD_CFLAGS_32 := $(filter-out -m64,$(KBUILD_CFLAGS))
KBUILD_CFLAGS_32 := $(filter-out -mno-pic-data-is-text-relative,$(KBUILD_CFLAGS_32))
KBUILD_CFLAGS_32 += -m31 -fPIC -shared -fno-common -fno-builtin

LDFLAGS_vdso32.so.dbg += -fPIC -shared -soname=linux-vdso32.so.1 \
	--hash-style=both --build-id=sha1 -melf_s390 -T

$(targets:%=$(obj)/%.dbg): KBUILD_CFLAGS = $(KBUILD_CFLAGS_32)
$(targets:%=$(obj)/%.dbg): KBUILD_AFLAGS = $(KBUILD_AFLAGS_32)

obj-y += vdso32_wrapper.o
targets += vdso32.lds
CPPFLAGS_vdso32.lds += -P -C -U$(ARCH)

# Disable gcov profiling, ubsan and kasan for VDSO code
GCOV_PROFILE := n
UBSAN_SANITIZE := n
KASAN_SANITIZE := n
KCSAN_SANITIZE := n

# Force dependency (incbin is bad)
$(obj)/vdso32_wrapper.o : $(obj)/vdso32.so

quiet_cmd_vdso_and_check = VDSO    $@
      cmd_vdso_and_check = $(cmd_ld); $(cmd_vdso_check)

$(obj)/vdso32.so.dbg: $(src)/vdso32.lds $(obj-vdso32) FORCE
	$(call if_changed,vdso_and_check)

# strip rule for the .so file
$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

$(obj-vdso32): %-32.o: %.S FORCE
	$(call if_changed_dep,vdso32as)

# actual build commands
quiet_cmd_vdso32as = VDSO32A $@
      cmd_vdso32as = $(CC) $(a_flags) -c -o $@ $<
quiet_cmd_vdso32cc = VDSO32C $@
      cmd_vdso32cc = $(CC) $(c_flags) -c -o $@ $<

# Generate VDSO offsets using helper script
gen-vdsosym := $(srctree)/$(src)/gen_vdso_offsets.sh
quiet_cmd_vdsosym = VDSOSYM $@
	cmd_vdsosym = $(NM) $< | $(gen-vdsosym) | LC_ALL=C sort > $@

include/generated/vdso32-offsets.h: $(obj)/vdso32.so.dbg FORCE
	$(call if_changed,vdsosym)
