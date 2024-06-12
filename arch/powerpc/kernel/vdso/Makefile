# SPDX-License-Identifier: GPL-2.0

# List of files in the vdso, has to be asm only for now

# Include the generic Makefile to check the built vdso.
include $(srctree)/lib/vdso/Makefile

obj-vdso32 = sigtramp32-32.o gettimeofday-32.o datapage-32.o cacheflush-32.o note-32.o getcpu-32.o
obj-vdso64 = sigtramp64-64.o gettimeofday-64.o datapage-64.o cacheflush-64.o note-64.o getcpu-64.o

ifneq ($(c-gettimeofday-y),)
  CFLAGS_vgettimeofday-32.o += -include $(c-gettimeofday-y)
  CFLAGS_vgettimeofday-32.o += $(DISABLE_LATENT_ENTROPY_PLUGIN)
  CFLAGS_vgettimeofday-32.o += $(call cc-option, -fno-stack-protector)
  CFLAGS_vgettimeofday-32.o += -DDISABLE_BRANCH_PROFILING
  CFLAGS_vgettimeofday-32.o += -ffreestanding -fasynchronous-unwind-tables
  CFLAGS_REMOVE_vgettimeofday-32.o = $(CC_FLAGS_FTRACE)
  CFLAGS_REMOVE_vgettimeofday-32.o += -mcmodel=medium -mabi=elfv1 -mabi=elfv2 -mcall-aixdesc
  # This flag is supported by clang for 64-bit but not 32-bit so it will cause
  # an unused command line flag warning for this file.
  ifdef CONFIG_CC_IS_CLANG
  CFLAGS_REMOVE_vgettimeofday-32.o += -fno-stack-clash-protection
  endif
  CFLAGS_vgettimeofday-64.o += -include $(c-gettimeofday-y)
  CFLAGS_vgettimeofday-64.o += $(DISABLE_LATENT_ENTROPY_PLUGIN)
  CFLAGS_vgettimeofday-64.o += $(call cc-option, -fno-stack-protector)
  CFLAGS_vgettimeofday-64.o += -DDISABLE_BRANCH_PROFILING
  CFLAGS_vgettimeofday-64.o += -ffreestanding -fasynchronous-unwind-tables
  CFLAGS_REMOVE_vgettimeofday-64.o = $(CC_FLAGS_FTRACE)
# Go prior to 1.16.x assumes r30 is not clobbered by any VDSO code. That used to be true
# by accident when the VDSO was hand-written asm code, but may not be now that the VDSO is
# compiler generated. To avoid breaking Go tell GCC not to use r30. Impact on code
# generation is minimal, it will just use r29 instead.
  CFLAGS_vgettimeofday-64.o += $(call cc-option, -ffixed-r30)
endif

# Build rules

ifdef CROSS32_COMPILE
    VDSOCC := $(CROSS32_COMPILE)gcc
else
    VDSOCC := $(CC)
endif

targets := $(obj-vdso32) vdso32.so.dbg vgettimeofday-32.o
obj-vdso32 := $(addprefix $(obj)/, $(obj-vdso32))
targets += $(obj-vdso64) vdso64.so.dbg vgettimeofday-64.o
obj-vdso64 := $(addprefix $(obj)/, $(obj-vdso64))

ccflags-y := -fno-common -fno-builtin
ldflags-y := -Wl,--hash-style=both -nostdlib -shared -z noexecstack $(CLANG_FLAGS)
ldflags-$(CONFIG_LD_IS_LLD) += $(call cc-option,--ld-path=$(LD),-fuse-ld=lld)
ldflags-$(CONFIG_LD_ORPHAN_WARN) += -Wl,--orphan-handling=$(CONFIG_LD_ORPHAN_WARN_LEVEL)

# Filter flags that clang will warn are unused for linking
ldflags-y += $(filter-out $(CC_AUTO_VAR_INIT_ZERO_ENABLER) $(CC_FLAGS_FTRACE) -Wa$(comma)%, $(KBUILD_CFLAGS))

CC32FLAGS := -m32
LD32FLAGS := -Wl,-soname=linux-vdso32.so.1
AS32FLAGS := -D__VDSO32__

LD64FLAGS := -Wl,-soname=linux-vdso64.so.1
AS64FLAGS := -D__VDSO64__

targets += vdso32.lds
CPPFLAGS_vdso32.lds += -P -C -Upowerpc
targets += vdso64.lds
CPPFLAGS_vdso64.lds += -P -C

# link rule for the .so file, .lds has to be first
$(obj)/vdso32.so.dbg: $(obj)/vdso32.lds $(obj-vdso32) $(obj)/vgettimeofday-32.o FORCE
	$(call if_changed,vdso32ld_and_check)
$(obj)/vdso64.so.dbg: $(obj)/vdso64.lds $(obj-vdso64) $(obj)/vgettimeofday-64.o FORCE
	$(call if_changed,vdso64ld_and_check)

# assembly rules for the .S files
$(obj-vdso32): %-32.o: %.S FORCE
	$(call if_changed_dep,vdso32as)
$(obj)/vgettimeofday-32.o: %-32.o: %.c FORCE
	$(call if_changed_dep,vdso32cc)
$(obj-vdso64): %-64.o: %.S FORCE
	$(call if_changed_dep,vdso64as)
$(obj)/vgettimeofday-64.o: %-64.o: %.c FORCE
	$(call if_changed_dep,cc_o_c)

# Generate VDSO offsets using helper script
gen-vdso32sym := $(src)/gen_vdso32_offsets.sh
quiet_cmd_vdso32sym = VDSO32SYM $@
      cmd_vdso32sym = $(NM) $< | $(gen-vdso32sym) | LC_ALL=C sort > $@
gen-vdso64sym := $(src)/gen_vdso64_offsets.sh
quiet_cmd_vdso64sym = VDSO64SYM $@
      cmd_vdso64sym = $(NM) $< | $(gen-vdso64sym) | LC_ALL=C sort > $@

include/generated/vdso32-offsets.h: $(obj)/vdso32.so.dbg FORCE
	$(call if_changed,vdso32sym)
include/generated/vdso64-offsets.h: $(obj)/vdso64.so.dbg FORCE
	$(call if_changed,vdso64sym)

# actual build commands
quiet_cmd_vdso32ld_and_check = VDSO32L $@
      cmd_vdso32ld_and_check = $(VDSOCC) $(ldflags-y) $(CC32FLAGS) $(LD32FLAGS) -o $@ -Wl,-T$(filter %.lds,$^) $(filter %.o,$^); $(cmd_vdso_check)
quiet_cmd_vdso32as = VDSO32A $@
      cmd_vdso32as = $(VDSOCC) $(a_flags) $(CC32FLAGS) $(AS32FLAGS) -c -o $@ $<
quiet_cmd_vdso32cc = VDSO32C $@
      cmd_vdso32cc = $(VDSOCC) $(c_flags) $(CC32FLAGS) -c -o $@ $<

quiet_cmd_vdso64ld_and_check = VDSO64L $@
      cmd_vdso64ld_and_check = $(VDSOCC) $(ldflags-y) $(LD64FLAGS) -o $@ -Wl,-T$(filter %.lds,$^) $(filter %.o,$^); $(cmd_vdso_check)
quiet_cmd_vdso64as = VDSO64A $@
      cmd_vdso64as = $(VDSOCC) $(a_flags) $(AS64FLAGS) -c -o $@ $<
