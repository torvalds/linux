# Include the generic Makefile to check the built vdso.
include $(srctree)/lib/vdso/Makefile.include

KCOV_INSTRUMENT := n

# Disable gcov profiling, ubsan and kasan for VDSO code
GCOV_PROFILE := n
UBSAN_SANITIZE := n
KASAN_SANITIZE := n
KCSAN_SANITIZE := n

obj-vdso64 = note.o sigtramp.o restart_syscall.o
obj-cvdso64 = vdso64_generic.o

# Build rules

targets := $(obj-vdso64) $(obj-cvdso64) vdso64.so
obj-vdso64  := $(addprefix $(obj)/, $(obj-vdso64))
obj-cvdso64 := $(addprefix $(obj)/, $(obj-cvdso64))

VDSO_CFLAGS_REMOVE := -pg $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_vdso64_generic.o = $(VDSO_CFLAGS_REMOVE)

ccflags-y := -shared -fno-common -fno-builtin
ccflags-y += -nostdlib -Wl,-soname=linux-vdso64.so.1 \
		$(call ld-option, -Wl$(comma)--hash-style=sysv)
asflags-y := -D__VDSO64__ -s

KBUILD_AFLAGS += -DBUILD_VDSO
KBUILD_CFLAGS += -DBUILD_VDSO -DDISABLE_BRANCH_PROFILING

VDSO_LIBGCC := $(shell $(CC) -print-libgcc-file-name)

obj-y += vdso64_wrapper.o
targets += vdso64.lds
CPPFLAGS_vdso64.lds += -P -C -U$(ARCH)

$(obj)/vdso64_wrapper.o : $(obj)/vdso64.so FORCE

# Force dependency (incbin is bad)
# link rule for the .so file, .lds has to be first
$(obj)/vdso64.so: $(obj)/vdso64.lds $(obj-vdso64) $(obj-cvdso64) $(VDSO_LIBGCC) FORCE
	$(call if_changed,vdso64ld)

# assembly rules for the .S files
$(obj-vdso64): %.o: %.S FORCE
	$(call if_changed_dep,vdso64as)
$(obj-cvdso64): %.o: %.c FORCE
	$(call if_changed_dep,vdso64cc)

# actual build commands
quiet_cmd_vdso64ld = VDSO64L $@
      cmd_vdso64ld = $(CC) $(c_flags) -Wl,-T $(filter-out FORCE, $^) -o $@
quiet_cmd_vdso64as = VDSO64A $@
      cmd_vdso64as = $(CC) $(a_flags) -c -o $@ $<
quiet_cmd_vdso64cc = VDSO64C $@
      cmd_vdso64cc = $(CC) $(c_flags) -c -o $@ $<

# Generate VDSO offsets using helper script
gen-vdsosym := $(src)/gen_vdso_offsets.sh
quiet_cmd_vdsosym = VDSOSYM $@
	cmd_vdsosym = $(NM) $< | $(gen-vdsosym) | LC_ALL=C sort > $@

include/generated/vdso64-offsets.h: $(obj)/vdso64.so FORCE
	$(call if_changed,vdsosym)
