# SPDX-License-Identifier: GPL-2.0
# List of files in the vdso

KCOV_INSTRUMENT := n
ARCH_REL_TYPE_ABS := R_390_COPY|R_390_GLOB_DAT|R_390_JMP_SLOT|R_390_RELATIVE
ARCH_REL_TYPE_ABS += R_390_GOT|R_390_PLT

include $(srctree)/lib/vdso/Makefile
obj-vdso64 = vdso_user_wrapper.o note.o
obj-cvdso64 = vdso64_generic.o getcpu.o
CFLAGS_REMOVE_getcpu.o = -pg $(CC_FLAGS_FTRACE) $(CC_FLAGS_EXPOLINE)
CFLAGS_REMOVE_vdso64_generic.o = -pg $(CC_FLAGS_FTRACE) $(CC_FLAGS_EXPOLINE)

# Build rules

targets := $(obj-vdso64) $(obj-cvdso64) vdso64.so vdso64.so.dbg
obj-vdso64 := $(addprefix $(obj)/, $(obj-vdso64))
obj-cvdso64 := $(addprefix $(obj)/, $(obj-cvdso64))

KBUILD_AFLAGS += -DBUILD_VDSO
KBUILD_CFLAGS += -DBUILD_VDSO -DDISABLE_BRANCH_PROFILING

KBUILD_AFLAGS_64 := $(filter-out -m64,$(KBUILD_AFLAGS))
KBUILD_AFLAGS_64 += -m64 -s

KBUILD_CFLAGS_64 := $(filter-out -m64,$(KBUILD_CFLAGS))
KBUILD_CFLAGS_64 += -m64 -fPIC -shared -fno-common -fno-builtin
ldflags-y := -fPIC -shared -nostdlib -soname=linux-vdso64.so.1 \
	     --hash-style=both --build-id=sha1 -T

$(targets:%=$(obj)/%.dbg): KBUILD_CFLAGS = $(KBUILD_CFLAGS_64)
$(targets:%=$(obj)/%.dbg): KBUILD_AFLAGS = $(KBUILD_AFLAGS_64)

obj-y += vdso64_wrapper.o
targets += vdso64.lds
CPPFLAGS_vdso64.lds += -P -C -U$(ARCH)

# Disable gcov profiling, ubsan and kasan for VDSO code
GCOV_PROFILE := n
UBSAN_SANITIZE := n
KASAN_SANITIZE := n

# Force dependency (incbin is bad)
$(obj)/vdso64_wrapper.o : $(obj)/vdso64.so

# link rule for the .so file, .lds has to be first
$(obj)/vdso64.so.dbg: $(src)/vdso64.lds $(obj-vdso64) $(obj-cvdso64) FORCE
	$(call if_changed,ld)

# strip rule for the .so file
$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

# assembly rules for the .S files
$(obj-vdso64): %.o: %.S FORCE
	$(call if_changed_dep,vdso64as)

$(obj-cvdso64): %.o: %.c FORCE
	$(call if_changed_dep,vdso64cc)

# actual build commands
quiet_cmd_vdso64as = VDSO64A $@
      cmd_vdso64as = $(CC) $(a_flags) -c -o $@ $<
quiet_cmd_vdso64cc = VDSO64C $@
      cmd_vdso64cc = $(CC) $(c_flags) -c -o $@ $<

# install commands for the unstripped file
quiet_cmd_vdso_install = INSTALL $@
      cmd_vdso_install = cp $(obj)/$@.dbg $(MODLIB)/vdso/$@

vdso64.so: $(obj)/vdso64.so.dbg
	@mkdir -p $(MODLIB)/vdso
	$(call cmd,vdso_install)

vdso_install: vdso64.so

# Generate VDSO offsets using helper script
gen-vdsosym := $(srctree)/$(src)/gen_vdso_offsets.sh
quiet_cmd_vdsosym = VDSOSYM $@
	cmd_vdsosym = $(NM) $< | $(gen-vdsosym) | LC_ALL=C sort > $@

include/generated/vdso64-offsets.h: $(obj)/vdso64.so.dbg FORCE
	$(call if_changed,vdsosym)
