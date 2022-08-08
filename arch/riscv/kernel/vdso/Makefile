# SPDX-License-Identifier: GPL-2.0-only
# Copied from arch/tile/kernel/vdso/Makefile

# Absolute relocation type $(ARCH_REL_TYPE_ABS) needs to be defined before
# the inclusion of generic Makefile.
ARCH_REL_TYPE_ABS := R_RISCV_32|R_RISCV_64|R_RISCV_JUMP_SLOT
include $(srctree)/lib/vdso/Makefile
# Symbols present in the vdso
vdso-syms  = rt_sigreturn
ifdef CONFIG_64BIT
vdso-syms += vgettimeofday
endif
vdso-syms += getcpu
vdso-syms += flush_icache

# Files to link into the vdso
obj-vdso = $(patsubst %, %.o, $(vdso-syms)) note.o

ccflags-y := -fno-stack-protector

ifneq ($(c-gettimeofday-y),)
  CFLAGS_vgettimeofday.o += -fPIC -include $(c-gettimeofday-y)
endif

# Build rules
targets := $(obj-vdso) vdso.so vdso.so.dbg vdso.lds vdso-dummy.o
obj-vdso := $(addprefix $(obj)/, $(obj-vdso))

obj-y += vdso.o vdso-syms.o
CPPFLAGS_vdso.lds += -P -C -U$(ARCH)

# Disable -pg to prevent insert call site
CFLAGS_REMOVE_vgettimeofday.o = $(CC_FLAGS_FTRACE) -Os

# Disable profiling and instrumentation for VDSO code
GCOV_PROFILE := n
KCOV_INSTRUMENT := n
KASAN_SANITIZE := n

# Force dependency
$(obj)/vdso.o: $(obj)/vdso.so

# link rule for the .so file, .lds has to be first
SYSCFLAGS_vdso.so.dbg = $(c_flags)
$(obj)/vdso.so.dbg: $(src)/vdso.lds $(obj-vdso) FORCE
	$(call if_changed,vdsold)
SYSCFLAGS_vdso.so.dbg = -shared -s -Wl,-soname=linux-vdso.so.1 \
	-Wl,--build-id=sha1 -Wl,--hash-style=both

# We also create a special relocatable object that should mirror the symbol
# table and layout of the linked DSO. With ld --just-symbols we can then
# refer to these symbols in the kernel code rather than hand-coded addresses.
$(obj)/vdso-syms.S: $(obj)/vdso.so FORCE
	$(call if_changed,so2s)

# strip rule for the .so file
$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

# actual build commands
# The DSO images are built using a special linker script
# Add -lgcc so rv32 gets static muldi3 and lshrdi3 definitions.
# Make sure only to export the intended __vdso_xxx symbol offsets.
quiet_cmd_vdsold = VDSOLD  $@
      cmd_vdsold = $(CC) $(KBUILD_CFLAGS) $(call cc-option, -no-pie) -nostdlib -nostartfiles $(SYSCFLAGS_$(@F)) \
                           -Wl,-T,$(filter-out FORCE,$^) -o $@.tmp && \
                   $(CROSS_COMPILE)objcopy \
                           $(patsubst %, -G __vdso_%, $(vdso-syms)) $@.tmp $@ && \
                   rm $@.tmp

# Extracts symbol offsets from the VDSO, converting them into an assembly file
# that contains the same symbols at the same offsets.
quiet_cmd_so2s = SO2S    $@
      cmd_so2s = $(NM) -D $< | $(srctree)/$(src)/so2s.sh > $@

# install commands for the unstripped file
quiet_cmd_vdso_install = INSTALL $@
      cmd_vdso_install = cp $(obj)/$@.dbg $(MODLIB)/vdso/$@

vdso.so: $(obj)/vdso.so.dbg
	@mkdir -p $(MODLIB)/vdso
	$(call cmd,vdso_install)

vdso_install: vdso.so
