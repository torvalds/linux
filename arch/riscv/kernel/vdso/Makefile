# Copied from arch/tile/kernel/vdso/Makefile

# Symbols present in the vdso
vdso-syms = rt_sigreturn

# Files to link into the vdso
obj-vdso = $(patsubst %, %.o, $(vdso-syms))

# Build rules
targets := $(obj-vdso) vdso.so vdso.so.dbg vdso.lds vdso-dummy.o
obj-vdso := $(addprefix $(obj)/, $(obj-vdso))

obj-y += vdso.o vdso-syms.o
CPPFLAGS_vdso.lds += -P -C -U$(ARCH)

# Disable gcov profiling for VDSO code
GCOV_PROFILE := n

# Force dependency
$(obj)/vdso.o: $(obj)/vdso.so

# link rule for the .so file, .lds has to be first
SYSCFLAGS_vdso.so.dbg = $(c_flags)
$(obj)/vdso.so.dbg: $(src)/vdso.lds $(obj-vdso) FORCE
	$(call if_changed,vdsold)

# We also create a special relocatable object that should mirror the symbol
# table and layout of the linked DSO.  With ld -R we can then refer to
# these symbols in the kernel code rather than hand-coded addresses.

SYSCFLAGS_vdso.so.dbg = -shared -s -Wl,-soname=linux-vdso.so.1 \
                            $(call cc-ldoption, -Wl$(comma)--hash-style=both)
$(obj)/vdso-dummy.o: $(src)/vdso.lds $(obj)/rt_sigreturn.o FORCE
	$(call if_changed,vdsold)

LDFLAGS_vdso-syms.o := -r -R
$(obj)/vdso-syms.o: $(obj)/vdso-dummy.o FORCE
	$(call if_changed,ld)

# strip rule for the .so file
$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

# actual build commands
# The DSO images are built using a special linker script
# Add -lgcc so rv32 gets static muldi3 and lshrdi3 definitions.
# Make sure only to export the intended __vdso_xxx symbol offsets.
quiet_cmd_vdsold = VDSOLD  $@
      cmd_vdsold = $(CC) $(KCFLAGS) -nostdlib $(SYSCFLAGS_$(@F)) \
                           -Wl,-T,$(filter-out FORCE,$^) -o $@.tmp -lgcc && \
                   $(CROSS_COMPILE)objcopy \
                           $(patsubst %, -G __vdso_%, $(vdso-syms)) $@.tmp $@

# install commands for the unstripped file
quiet_cmd_vdso_install = INSTALL $@
      cmd_vdso_install = cp $(obj)/$@.dbg $(MODLIB)/vdso/$@

vdso.so: $(obj)/vdso.so.dbg
	@mkdir -p $(MODLIB)/vdso
	$(call cmd,vdso_install)

vdso_install: vdso.so
