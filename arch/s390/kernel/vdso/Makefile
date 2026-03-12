# SPDX-License-Identifier: GPL-2.0
# List of files in the vdso

# Include the generic Makefile to check the built vdso.
include $(srctree)/lib/vdso/Makefile.include
obj-vdso = vdso_user_wrapper.o note.o vgetrandom-chacha.o
obj-cvdso = vdso_generic.o getcpu.o vgetrandom.o
VDSO_CFLAGS_REMOVE := -pg $(CC_FLAGS_FTRACE) $(CC_FLAGS_EXPOLINE)
CFLAGS_REMOVE_getcpu.o = $(VDSO_CFLAGS_REMOVE)
CFLAGS_REMOVE_vgetrandom.o = $(VDSO_CFLAGS_REMOVE)
CFLAGS_REMOVE_vdso_generic.o = $(VDSO_CFLAGS_REMOVE)

ifneq ($(c-getrandom-y),)
	CFLAGS_vgetrandom.o += -include $(c-getrandom-y)
endif

# Build rules

targets := $(obj-vdso) $(obj-cvdso) vdso.so vdso.so.dbg
obj-vdso := $(addprefix $(obj)/, $(obj-vdso))
obj-cvdso := $(addprefix $(obj)/, $(obj-cvdso))

KBUILD_AFLAGS_VDSO := $(KBUILD_AFLAGS) -DBUILD_VDSO

KBUILD_CFLAGS_VDSO := $(KBUILD_CFLAGS) -DBUILD_VDSO -DDISABLE_BRANCH_PROFILING
KBUILD_CFLAGS_VDSO := $(filter-out -mpacked-stack,$(KBUILD_CFLAGS_VDSO))
KBUILD_CFLAGS_VDSO := $(filter-out -mno-pic-data-is-text-relative,$(KBUILD_CFLAGS_VDSO))
KBUILD_CFLAGS_VDSO := $(filter-out -munaligned-symbols,$(KBUILD_CFLAGS_VDSO))
KBUILD_CFLAGS_VDSO := $(filter-out -fno-asynchronous-unwind-tables,$(KBUILD_CFLAGS_VDSO))
KBUILD_CFLAGS_VDSO += -fPIC -fno-common -fno-builtin -fasynchronous-unwind-tables
KBUILD_CFLAGS_VDSO += -fno-stack-protector $(DISABLE_KSTACK_ERASE)
ldflags-y := -shared -soname=linux-vdso.so.1 \
	     --hash-style=both --build-id=sha1 -T

$(targets:%=$(obj)/%.dbg): KBUILD_CFLAGS = $(KBUILD_CFLAGS_VDSO)
$(targets:%=$(obj)/%.dbg): KBUILD_AFLAGS = $(KBUILD_AFLAGS_VDSO)

obj-y += vdso_wrapper.o
targets += vdso.lds
CPPFLAGS_vdso.lds += -P -C -U$(ARCH)

# Force dependency (incbin is bad)
$(obj)/vdso_wrapper.o : $(obj)/vdso.so

quiet_cmd_vdso_and_check = VDSO    $@
      cmd_vdso_and_check = $(cmd_ld); $(cmd_vdso_check)

# link rule for the .so file, .lds has to be first
$(obj)/vdso.so.dbg: $(obj)/vdso.lds $(obj-vdso) $(obj-cvdso) FORCE
	$(call if_changed,vdso_and_check)

# strip rule for the .so file
$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

# assembly rules for the .S files
$(obj-vdso): %.o: %.S FORCE
	$(call if_changed_dep,vdsoas)

$(obj-cvdso): %.o: %.c FORCE
	$(call if_changed_dep,vdsocc)

# actual build commands
quiet_cmd_vdsoas = VDSOA $@
      cmd_vdsoas = $(CC) $(a_flags) -c -o $@ $<
quiet_cmd_vdsocc = VDSOC $@
      cmd_vdsocc = $(CC) $(c_flags) -c -o $@ $<

# Generate VDSO offsets using helper script
gen-vdsosym := $(src)/gen_vdso_offsets.sh
quiet_cmd_vdsosym = VDSOSYM $@
	cmd_vdsosym = $(NM) $< | $(gen-vdsosym) | LC_ALL=C sort > $@

include/generated/vdso-offsets.h: $(obj)/vdso.so.dbg FORCE
	$(call if_changed,vdsosym)
