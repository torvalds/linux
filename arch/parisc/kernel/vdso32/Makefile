# List of files in the vdso, has to be asm only for now

obj-vdso32 = note.o sigtramp.o restart_syscall.o

# Build rules

targets := $(obj-vdso32) vdso32.so
obj-vdso32 := $(addprefix $(obj)/, $(obj-vdso32))

ccflags-y := -shared -fno-common -fbuiltin -mno-fast-indirect-calls -O2 -mno-long-calls
#  -march=1.1 -mschedule=7100LC
ccflags-y += -nostdlib -Wl,-soname=linux-vdso32.so.1 \
		$(call ld-option, -Wl$(comma)--hash-style=sysv)
asflags-y := -D__VDSO32__ -s

KBUILD_AFLAGS += -DBUILD_VDSO
KBUILD_CFLAGS += -DBUILD_VDSO -DDISABLE_BRANCH_PROFILING

VDSO_LIBGCC := $(shell $(CROSS32CC) -print-libgcc-file-name)

obj-y += vdso32_wrapper.o
extra-y += vdso32.lds
CPPFLAGS_vdso32.lds += -P -C  #  -U$(ARCH)

$(obj)/vdso32_wrapper.o : $(obj)/vdso32.so FORCE

# Force dependency (incbin is bad)
# link rule for the .so file, .lds has to be first
$(obj)/vdso32.so: $(src)/vdso32.lds $(obj-vdso32) $(obj-cvdso32) $(VDSO_LIBGCC) FORCE
	$(call if_changed,vdso32ld)

# assembly rules for the .S files
$(obj-vdso32): %.o: %.S FORCE
	$(call if_changed_dep,vdso32as)

$(obj-cvdso32): %.o: %.c FORCE
	$(call if_changed_dep,vdso32cc)

# actual build commands
quiet_cmd_vdso32ld = VDSO32L $@
      cmd_vdso32ld = $(CROSS32CC) $(c_flags) -Wl,-T $(filter-out FORCE, $^) -o $@
quiet_cmd_vdso32as = VDSO32A $@
      cmd_vdso32as = $(CROSS32CC) $(a_flags) -c -o $@ $<
quiet_cmd_vdso32cc = VDSO32C $@
      cmd_vdso32cc = $(CROSS32CC) $(c_flags) -c -fPIC -mno-fast-indirect-calls -o $@ $<

# Generate VDSO offsets using helper script
gen-vdsosym := $(srctree)/$(src)/gen_vdso_offsets.sh
quiet_cmd_vdsosym = VDSOSYM $@
	cmd_vdsosym = $(NM) $< | $(gen-vdsosym) | LC_ALL=C sort > $@

include/generated/vdso32-offsets.h: $(obj)/vdso32.so FORCE
	$(call if_changed,vdsosym)
