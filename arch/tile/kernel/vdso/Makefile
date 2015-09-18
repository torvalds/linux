# Symbols present in the vdso
vdso-syms = rt_sigreturn gettimeofday

# Files to link into the vdso
obj-vdso = $(patsubst %, v%.o, $(vdso-syms))

# Build rules
targets := $(obj-vdso) vdso.so vdso.so.dbg vdso.lds
obj-vdso := $(addprefix $(obj)/, $(obj-vdso))

# vdso32 is only for tilegx -m32 compat task.
VDSO32-$(CONFIG_COMPAT) := y

obj-y += vdso.o
obj-$(VDSO32-y) += vdso32.o
extra-y += vdso.lds
CPPFLAGS_vdso.lds += -P -C -U$(ARCH)

# vDSO code runs in userspace and -pg doesn't help with profiling anyway.
CFLAGS_REMOVE_vdso.o = -pg
CFLAGS_REMOVE_vdso32.o = -pg
CFLAGS_REMOVE_vrt_sigreturn.o = -pg
CFLAGS_REMOVE_vrt_sigreturn32.o = -pg
CFLAGS_REMOVE_vgettimeofday.o = -pg
CFLAGS_REMOVE_vgettimeofday32.o = -pg

ifdef CONFIG_FEEDBACK_COLLECT
# vDSO code runs in userspace, not collecting feedback data.
CFLAGS_REMOVE_vdso.o = -ffeedback-generate
CFLAGS_REMOVE_vdso32.o = -ffeedback-generate
CFLAGS_REMOVE_vrt_sigreturn.o = -ffeedback-generate
CFLAGS_REMOVE_vrt_sigreturn32.o = -ffeedback-generate
CFLAGS_REMOVE_vgettimeofday.o = -ffeedback-generate
CFLAGS_REMOVE_vgettimeofday32.o = -ffeedback-generate
endif

# Disable gcov profiling for VDSO code
GCOV_PROFILE := n

# Force dependency
$(obj)/vdso.o: $(obj)/vdso.so

# link rule for the .so file, .lds has to be first
SYSCFLAGS_vdso.so.dbg = $(c_flags)
$(obj)/vdso.so.dbg: $(src)/vdso.lds $(obj-vdso)
	$(call if_changed,vdsold)


# We also create a special relocatable object that should mirror the symbol
# table and layout of the linked DSO.  With ld -R we can then refer to
# these symbols in the kernel code rather than hand-coded addresses.
extra-y += vdso-syms.o
$(obj)/built-in.o: $(obj)/vdso-syms.o
$(obj)/built-in.o: ld_flags += -R $(obj)/vdso-syms.o

SYSCFLAGS_vdso.so.dbg = -shared -s -Wl,-soname=linux-vdso.so.1 \
                            $(call cc-ldoption, -Wl$(comma)--hash-style=both)
SYSCFLAGS_vdso_syms.o = -r
$(obj)/vdso-syms.o: $(src)/vdso.lds $(obj)/vrt_sigreturn.o FORCE
	$(call if_changed,vdsold)


# strip rule for the .so file
$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

# actual build commands
# The DSO images are built using a special linker script
# Add -lgcc so tilepro gets static muldi3 and lshrdi3 definitions.
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

vdso32.so: $(obj)/vdso32.so.dbg
	$(call cmd,vdso_install)

vdso_install: vdso.so
vdso32_install: vdso32.so


KBUILD_AFLAGS_32 := $(filter-out -m64,$(KBUILD_AFLAGS))
KBUILD_AFLAGS_32 += -m32 -s
KBUILD_CFLAGS_32 := $(filter-out -m64,$(KBUILD_CFLAGS))
KBUILD_CFLAGS_32 += -m32 -fPIC -shared

obj-vdso32 = $(patsubst %, v%32.o, $(vdso-syms))
obj-vdso32 := $(addprefix $(obj)/, $(obj-vdso32))

targets += $(obj-vdso32) vdso32.so vdso32.so.dbg

$(obj-vdso32:%=%): KBUILD_AFLAGS = $(KBUILD_AFLAGS_32)
$(obj-vdso32:%=%): KBUILD_CFLAGS = $(KBUILD_CFLAGS_32)

$(obj)/vgettimeofday32.o: $(obj)/vgettimeofday.c
	$(call if_changed_rule,cc_o_c)

$(obj)/vrt_sigreturn32.o: $(obj)/vrt_sigreturn.S
	$(call if_changed,as_o_S)

# Force dependency
$(obj)/vdso32.o: $(obj)/vdso32.so

SYSCFLAGS_vdso32.so.dbg = -m32 -shared -s -Wl,-soname=linux-vdso32.so.1 \
			    $(call cc-ldoption, -Wl$(comma)--hash-style=both)
$(obj)/vdso32.so.dbg: $(src)/vdso.lds $(obj-vdso32)
	$(call if_changed,vdsold)
