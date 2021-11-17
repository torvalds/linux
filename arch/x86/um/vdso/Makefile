# SPDX-License-Identifier: GPL-2.0
#
# Building vDSO images for x86.
#

# Prevents link failures: __sanitizer_cov_trace_pc() is not linked in.
KCOV_INSTRUMENT                := n

VDSO64-y		:= y

vdso-install-$(VDSO64-y)	+= vdso.so


# files to link into the vdso
vobjs-y := vdso-note.o um_vdso.o

# files to link into kernel
obj-$(VDSO64-y)			+= vdso.o vma.o

vobjs := $(foreach F,$(vobjs-y),$(obj)/$F)

$(obj)/vdso.o: $(obj)/vdso.so

targets += vdso.so vdso.so.dbg vdso.lds $(vobjs-y)

CPPFLAGS_vdso.lds += -P -C

VDSO_LDFLAGS_vdso.lds = -m64 -Wl,-soname=linux-vdso.so.1 \
       -Wl,-z,max-page-size=4096

$(obj)/vdso.o: $(src)/vdso.S $(obj)/vdso.so

$(obj)/vdso.so.dbg: $(obj)/vdso.lds $(vobjs) FORCE
	$(call if_changed,vdso)

$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

#
# Don't omit frame pointers for ease of userspace debugging, but do
# optimize sibling calls.
#
CFL := $(PROFILING) -mcmodel=small -fPIC -O2 -fasynchronous-unwind-tables -m64 \
       $(filter -g%,$(KBUILD_CFLAGS)) -fno-stack-protector \
       -fno-omit-frame-pointer -foptimize-sibling-calls

$(vobjs): KBUILD_CFLAGS += $(CFL)

#
# vDSO code runs in userspace and -pg doesn't help with profiling anyway.
#
CFLAGS_REMOVE_vdso-note.o = -pg -fprofile-arcs -ftest-coverage
CFLAGS_REMOVE_um_vdso.o = -pg -fprofile-arcs -ftest-coverage

#
# The DSO images are built using a special linker script.
#
quiet_cmd_vdso = VDSO    $@
      cmd_vdso = $(CC) -nostdlib -o $@ \
		       $(VDSO_LDFLAGS) $(VDSO_LDFLAGS_$(filter %.lds,$(^F))) \
		       -Wl,-T,$(filter %.lds,$^) $(filter %.o,$^) && \
		 sh $(srctree)/$(src)/checkundef.sh '$(NM)' '$@'

VDSO_LDFLAGS = -fPIC -shared -Wl,--hash-style=sysv
GCOV_PROFILE := n

#
# Install the unstripped copy of vdso*.so listed in $(vdso-install-y).
#
quiet_cmd_vdso_install = INSTALL $@
      cmd_vdso_install = cp $(obj)/$@.dbg $(MODLIB)/vdso/$@
$(vdso-install-y): %.so: $(obj)/%.so.dbg FORCE
	@mkdir -p $(MODLIB)/vdso
	$(call cmd,vdso_install)

PHONY += vdso_install $(vdso-install-y)
vdso_install: $(vdso-install-y)
