# SPDX-License-Identifier: GPL-2.0
hostprogs-y := vdsomunge

obj-vdso := vgettimeofday.o datapage.o

# Build rules
targets := $(obj-vdso) vdso.so vdso.so.dbg vdso.so.raw vdso.lds
obj-vdso := $(addprefix $(obj)/, $(obj-vdso))

ccflags-y := -fPIC -fno-common -fno-builtin -fno-stack-protector
ccflags-y += -DDISABLE_BRANCH_PROFILING

VDSO_LDFLAGS := -Wl,-Bsymbolic -Wl,--no-undefined -Wl,-soname=linux-vdso.so.1
VDSO_LDFLAGS += -Wl,-z,max-page-size=4096 -Wl,-z,common-page-size=4096
VDSO_LDFLAGS += -nostdlib -shared
VDSO_LDFLAGS += $(call cc-ldoption, -Wl$(comma)--hash-style=sysv)
VDSO_LDFLAGS += $(call cc-ldoption, -Wl$(comma)--build-id)
VDSO_LDFLAGS += $(call cc-ldoption, -fuse-ld=bfd)

obj-$(CONFIG_VDSO) += vdso.o
extra-$(CONFIG_VDSO) += vdso.lds
CPPFLAGS_vdso.lds += -P -C -U$(ARCH)

CFLAGS_REMOVE_vdso.o = -pg

# Force -O2 to avoid libgcc dependencies
CFLAGS_REMOVE_vgettimeofday.o = -pg -Os
CFLAGS_vgettimeofday.o = -O2

# Disable gcov profiling for VDSO code
GCOV_PROFILE := n

# Prevents link failures: __sanitizer_cov_trace_pc() is not linked in.
KCOV_INSTRUMENT := n

# Force dependency
$(obj)/vdso.o : $(obj)/vdso.so

# Link rule for the .so file
$(obj)/vdso.so.raw: $(src)/vdso.lds $(obj-vdso) FORCE
	$(call if_changed,vdsold)

$(obj)/vdso.so.dbg: $(obj)/vdso.so.raw $(obj)/vdsomunge FORCE
	$(call if_changed,vdsomunge)

# Strip rule for the .so file
$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

# Actual build commands
quiet_cmd_vdsold = VDSO    $@
      cmd_vdsold = $(CC) $(c_flags) $(VDSO_LDFLAGS) \
                   -Wl,-T $(filter %.lds,$^) $(filter %.o,$^) -o $@

quiet_cmd_vdsomunge = MUNGE   $@
      cmd_vdsomunge = $(objtree)/$(obj)/vdsomunge $< $@

#
# Install the unstripped copy of vdso.so.dbg.  If our toolchain
# supports build-id, install .build-id links as well.
#
# Cribbed from arch/x86/vdso/Makefile.
#
quiet_cmd_vdso_install = INSTALL $<
define cmd_vdso_install
	cp $< "$(MODLIB)/vdso/vdso.so"; \
	if readelf -n $< | grep -q 'Build ID'; then \
	  buildid=`readelf -n $< |grep 'Build ID' |sed -e 's/^.*Build ID: \(.*\)$$/\1/'`; \
	  first=`echo $$buildid | cut -b-2`; \
	  last=`echo $$buildid | cut -b3-`; \
	  mkdir -p "$(MODLIB)/vdso/.build-id/$$first"; \
	  ln -sf "../../vdso.so" "$(MODLIB)/vdso/.build-id/$$first/$$last.debug"; \
	fi
endef

$(MODLIB)/vdso: FORCE
	@mkdir -p $(MODLIB)/vdso

PHONY += vdso_install
vdso_install: $(obj)/vdso.so.dbg $(MODLIB)/vdso
	$(call cmd,vdso_install)
