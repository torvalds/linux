# SPDX-License-Identifier: GPL-2.0
# List of files in the vdso

KCOV_INSTRUMENT := n
ARCH_REL_TYPE_ABS := R_390_COPY|R_390_GLOB_DAT|R_390_JMP_SLOT|R_390_RELATIVE
ARCH_REL_TYPE_ABS += R_390_GOT|R_390_PLT

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

$(obj)/vdso32.so.dbg: $(src)/vdso32.lds $(obj-vdso32) FORCE
	$(call if_changed,ld)

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

# install commands for the unstripped file
quiet_cmd_vdso_install = INSTALL $@
      cmd_vdso_install = cp $(obj)/$@.dbg $(MODLIB)/vdso/$@

vdso32.so: $(obj)/vdso32.so.dbg
	@mkdir -p $(MODLIB)/vdso
	$(call cmd,vdso_install)

vdso_install: vdso32.so

# Generate VDSO offsets using helper script
gen-vdsosym := $(srctree)/$(src)/gen_vdso_offsets.sh
quiet_cmd_vdsosym = VDSOSYM $@
	cmd_vdsosym = $(NM) $< | $(gen-vdsosym) | LC_ALL=C sort > $@

include/generated/vdso32-offsets.h: $(obj)/vdso32.so.dbg FORCE
	$(call if_changed,vdsosym)
