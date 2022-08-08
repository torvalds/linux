# SPDX-License-Identifier: GPL-2.0

# Absolute relocation type $(ARCH_REL_TYPE_ABS) needs to be defined before
# the inclusion of generic Makefile.
ARCH_REL_TYPE_ABS := R_ARM_JUMP_SLOT|R_ARM_GLOB_DAT|R_ARM_ABS32
include $(srctree)/lib/vdso/Makefile

hostprogs := vdsomunge

obj-vdso := vgettimeofday.o datapage.o note.o

# Build rules
targets := $(obj-vdso) vdso.so vdso.so.dbg vdso.so.raw vdso.lds
obj-vdso := $(addprefix $(obj)/, $(obj-vdso))

ccflags-y := -fPIC -fno-common -fno-builtin -fno-stack-protector
ccflags-y += -DDISABLE_BRANCH_PROFILING -DBUILD_VDSO32

ldflags-$(CONFIG_CPU_ENDIAN_BE8) := --be8
ldflags-y := -Bsymbolic --no-undefined -soname=linux-vdso.so.1 \
	    -z max-page-size=4096 -nostdlib -shared $(ldflags-y) \
	    --hash-style=sysv --build-id=sha1 \
	    -T

obj-$(CONFIG_VDSO) += vdso.o
CPPFLAGS_vdso.lds += -P -C -U$(ARCH)

CFLAGS_REMOVE_vdso.o = -pg

# Force -O2 to avoid libgcc dependencies
CFLAGS_REMOVE_vgettimeofday.o = -pg -Os $(GCC_PLUGINS_CFLAGS)
ifeq ($(c-gettimeofday-y),)
CFLAGS_vgettimeofday.o = -O2
else
CFLAGS_vgettimeofday.o = -O2 -include $(c-gettimeofday-y)
endif

# Disable gcov profiling for VDSO code
GCOV_PROFILE := n

# Prevents link failures: __sanitizer_cov_trace_pc() is not linked in.
KCOV_INSTRUMENT := n

KASAN_SANITIZE := n

# Force dependency
$(obj)/vdso.o : $(obj)/vdso.so

# Link rule for the .so file
$(obj)/vdso.so.raw: $(obj)/vdso.lds $(obj-vdso) FORCE
	$(call if_changed,vdsold_and_vdso_check)

$(obj)/vdso.so.dbg: $(obj)/vdso.so.raw $(obj)/vdsomunge FORCE
	$(call if_changed,vdsomunge)

# Strip rule for the .so file
$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

# Actual build commands
quiet_cmd_vdsold_and_vdso_check = LD      $@
      cmd_vdsold_and_vdso_check = $(cmd_ld); $(cmd_vdso_check)

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
