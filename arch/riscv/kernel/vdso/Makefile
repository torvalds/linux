# SPDX-License-Identifier: GPL-2.0-only
# Copied from arch/tile/kernel/vdso/Makefile

# Include the generic Makefile to check the built vdso.
include $(srctree)/lib/vdso/Makefile
# Symbols present in the vdso
vdso-syms  = rt_sigreturn
ifdef CONFIG_64BIT
vdso-syms += vgettimeofday
endif
vdso-syms += getcpu
vdso-syms += flush_icache
vdso-syms += hwprobe
vdso-syms += sys_hwprobe

# Files to link into the vdso
obj-vdso = $(patsubst %, %.o, $(vdso-syms)) note.o

ccflags-y := -fno-stack-protector
ccflags-y += -DDISABLE_BRANCH_PROFILING

ifneq ($(c-gettimeofday-y),)
  CFLAGS_vgettimeofday.o += -fPIC -include $(c-gettimeofday-y)
endif

CFLAGS_hwprobe.o += -fPIC

# Build rules
targets := $(obj-vdso) vdso.so vdso.so.dbg vdso.lds
obj-vdso := $(addprefix $(obj)/, $(obj-vdso))

obj-y += vdso.o
CPPFLAGS_vdso.lds += -P -C -U$(ARCH)
ifneq ($(filter vgettimeofday, $(vdso-syms)),)
CPPFLAGS_vdso.lds += -DHAS_VGETTIMEOFDAY
endif

# Disable -pg to prevent insert call site
CFLAGS_REMOVE_vgettimeofday.o = $(CC_FLAGS_FTRACE) $(CC_FLAGS_SCS)
CFLAGS_REMOVE_hwprobe.o = $(CC_FLAGS_FTRACE) $(CC_FLAGS_SCS)

# Disable profiling and instrumentation for VDSO code
GCOV_PROFILE := n
KCOV_INSTRUMENT := n
KASAN_SANITIZE := n
UBSAN_SANITIZE := n

# Force dependency
$(obj)/vdso.o: $(obj)/vdso.so

# link rule for the .so file, .lds has to be first
$(obj)/vdso.so.dbg: $(obj)/vdso.lds $(obj-vdso) FORCE
	$(call if_changed,vdsold)
LDFLAGS_vdso.so.dbg = -shared -S -soname=linux-vdso.so.1 \
	--build-id=sha1 --hash-style=both --eh-frame-hdr

# strip rule for the .so file
$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

# Generate VDSO offsets using helper script
gen-vdsosym := $(srctree)/$(src)/gen_vdso_offsets.sh
quiet_cmd_vdsosym = VDSOSYM $@
	cmd_vdsosym = $(NM) $< | $(gen-vdsosym) | LC_ALL=C sort > $@

include/generated/vdso-offsets.h: $(obj)/vdso.so.dbg FORCE
	$(call if_changed,vdsosym)

# actual build commands
# The DSO images are built using a special linker script
# Make sure only to export the intended __vdso_xxx symbol offsets.
quiet_cmd_vdsold = VDSOLD  $@
      cmd_vdsold = $(LD) $(ld_flags) -T $(filter-out FORCE,$^) -o $@.tmp && \
                   $(OBJCOPY) $(patsubst %, -G __vdso_%, $(vdso-syms)) $@.tmp $@ && \
                   rm $@.tmp
