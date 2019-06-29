# SPDX-License-Identifier: GPL-2.0
#
# Building a vDSO image for AArch64.
#
# Author: Will Deacon <will.deacon@arm.com>
# Heavily based on the vDSO Makefiles for other archs.
#

obj-vdso := gettimeofday.o note.o sigreturn.o

# Build rules
targets := $(obj-vdso) vdso.so vdso.so.dbg
obj-vdso := $(addprefix $(obj)/, $(obj-vdso))

ldflags-y := -shared -nostdlib -soname=linux-vdso.so.1 --hash-style=sysv \
		--build-id -n -T

# Disable gcov profiling for VDSO code
GCOV_PROFILE := n

obj-y += vdso.o
extra-y += vdso.lds
CPPFLAGS_vdso.lds += -P -C -U$(ARCH)

# Force dependency (incbin is bad)
$(obj)/vdso.o : $(obj)/vdso.so

# Link rule for the .so file, .lds has to be first
$(obj)/vdso.so.dbg: $(obj)/vdso.lds $(obj-vdso) FORCE
	$(call if_changed,ld)

# Strip rule for the .so file
$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

# Generate VDSO offsets using helper script
gen-vdsosym := $(srctree)/$(src)/gen_vdso_offsets.sh
quiet_cmd_vdsosym = VDSOSYM $@
      cmd_vdsosym = $(NM) $< | $(gen-vdsosym) | LC_ALL=C sort > $@

include/generated/vdso-offsets.h: $(obj)/vdso.so.dbg FORCE
	$(call if_changed,vdsosym)

# Assembly rules for the .S files
$(obj-vdso): %.o: %.S FORCE
	$(call if_changed_dep,vdsoas)

# Actual build commands
quiet_cmd_vdsoas = VDSOA   $@
      cmd_vdsoas = $(CC) $(a_flags) -c -o $@ $<

# Install commands for the unstripped file
quiet_cmd_vdso_install = INSTALL $@
      cmd_vdso_install = cp $(obj)/$@.dbg $(MODLIB)/vdso/$@

vdso.so: $(obj)/vdso.so.dbg
	@mkdir -p $(MODLIB)/vdso
	$(call cmd,vdso_install)

vdso_install: vdso.so
