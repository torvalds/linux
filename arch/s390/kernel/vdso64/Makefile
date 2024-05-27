# SPDX-License-Identifier: GPL-2.0
# List of files in the vdso

# Include the generic Makefile to check the built vdso.
include $(srctree)/lib/vdso/Makefile
obj-vdso64 = vdso_user_wrapper.o note.o
obj-cvdso64 = vdso64_generic.o getcpu.o
VDSO_CFLAGS_REMOVE := -pg $(CC_FLAGS_FTRACE) $(CC_FLAGS_EXPOLINE) $(CC_FLAGS_CHECK_STACK)
CFLAGS_REMOVE_getcpu.o = $(VDSO_CFLAGS_REMOVE)
CFLAGS_REMOVE_vdso64_generic.o = $(VDSO_CFLAGS_REMOVE)

# Build rules

targets := $(obj-vdso64) $(obj-cvdso64) vdso64.so vdso64.so.dbg
obj-vdso64 := $(addprefix $(obj)/, $(obj-vdso64))
obj-cvdso64 := $(addprefix $(obj)/, $(obj-cvdso64))

KBUILD_AFLAGS += -DBUILD_VDSO
KBUILD_CFLAGS += -DBUILD_VDSO -DDISABLE_BRANCH_PROFILING

KBUILD_AFLAGS_64 := $(filter-out -m64,$(KBUILD_AFLAGS))
KBUILD_AFLAGS_64 += -m64

KBUILD_CFLAGS_64 := $(filter-out -m64,$(KBUILD_CFLAGS))
KBUILD_CFLAGS_64 := $(filter-out -mpacked-stack,$(KBUILD_CFLAGS_64))
KBUILD_CFLAGS_64 := $(filter-out -mno-pic-data-is-text-relative,$(KBUILD_CFLAGS_64))
KBUILD_CFLAGS_64 := $(filter-out -munaligned-symbols,$(KBUILD_CFLAGS_64))
KBUILD_CFLAGS_64 := $(filter-out -fno-asynchronous-unwind-tables,$(KBUILD_CFLAGS_64))
KBUILD_CFLAGS_64 += -m64 -fPIC -fno-common -fno-builtin -fasynchronous-unwind-tables
ldflags-y := -shared -soname=linux-vdso64.so.1 \
	     --hash-style=both --build-id=sha1 -T

$(targets:%=$(obj)/%.dbg): KBUILD_CFLAGS = $(KBUILD_CFLAGS_64)
$(targets:%=$(obj)/%.dbg): KBUILD_AFLAGS = $(KBUILD_AFLAGS_64)

obj-y += vdso64_wrapper.o
targets += vdso64.lds
CPPFLAGS_vdso64.lds += -P -C -U$(ARCH)

# Force dependency (incbin is bad)
$(obj)/vdso64_wrapper.o : $(obj)/vdso64.so

quiet_cmd_vdso_and_check = VDSO    $@
      cmd_vdso_and_check = $(cmd_ld); $(cmd_vdso_check)

# link rule for the .so file, .lds has to be first
$(obj)/vdso64.so.dbg: $(obj)/vdso64.lds $(obj-vdso64) $(obj-cvdso64) FORCE
	$(call if_changed,vdso_and_check)

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

# Generate VDSO offsets using helper script
gen-vdsosym := $(src)/gen_vdso_offsets.sh
quiet_cmd_vdsosym = VDSOSYM $@
	cmd_vdsosym = $(NM) $< | $(gen-vdsosym) | LC_ALL=C sort > $@

include/generated/vdso64-offsets.h: $(obj)/vdso64.so.dbg FORCE
	$(call if_changed,vdsosym)
