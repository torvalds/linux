# List of files in the vdso, has to be asm only for now

obj-vdso64 = gettimeofday.o clock_getres.o clock_gettime.o note.o

# Build rules

targets := $(obj-vdso64) vdso64.so vdso64.so.dbg
obj-vdso64 := $(addprefix $(obj)/, $(obj-vdso64))

KBUILD_AFLAGS_64 := $(filter-out -m64,$(KBUILD_AFLAGS))
KBUILD_AFLAGS_64 += -m64 -s

KBUILD_CFLAGS_64 := $(filter-out -m64,$(KBUILD_CFLAGS))
KBUILD_CFLAGS_64 += -m64 -fPIC -shared -fno-common -fno-builtin
KBUILD_CFLAGS_64 += -nostdlib -Wl,-soname=linux-vdso64.so.1 \
			$(call cc-ldoption, -Wl$(comma)--hash-style=sysv)

$(targets:%=$(obj)/%.dbg): KBUILD_CFLAGS = $(KBUILD_CFLAGS_64)
$(targets:%=$(obj)/%.dbg): KBUILD_AFLAGS = $(KBUILD_AFLAGS_64)

obj-y += vdso64_wrapper.o
extra-y += vdso64.lds
CPPFLAGS_vdso64.lds += -P -C -U$(ARCH)

# Disable gcov profiling for VDSO code
GCOV_PROFILE := n

# Force dependency (incbin is bad)
$(obj)/vdso64_wrapper.o : $(obj)/vdso64.so

# link rule for the .so file, .lds has to be first
$(obj)/vdso64.so.dbg: $(src)/vdso64.lds $(obj-vdso64)
	$(call if_changed,vdso64ld)

# strip rule for the .so file
$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

# assembly rules for the .S files
$(obj-vdso64): %.o: %.S
	$(call if_changed_dep,vdso64as)

# actual build commands
quiet_cmd_vdso64ld = VDSO64L $@
      cmd_vdso64ld = $(CC) $(c_flags) -Wl,-T $^ -o $@
quiet_cmd_vdso64as = VDSO64A $@
      cmd_vdso64as = $(CC) $(a_flags) -c -o $@ $<

# install commands for the unstripped file
quiet_cmd_vdso_install = INSTALL $@
      cmd_vdso_install = cp $(obj)/$@.dbg $(MODLIB)/vdso/$@

vdso64.so: $(obj)/vdso64.so.dbg
	@mkdir -p $(MODLIB)/vdso
	$(call cmd,vdso_install)

vdso_install: vdso64.so
