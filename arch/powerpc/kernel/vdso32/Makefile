# SPDX-License-Identifier: GPL-2.0

# List of files in the vdso, has to be asm only for now

ARCH_REL_TYPE_ABS := R_PPC_JUMP_SLOT|R_PPC_GLOB_DAT|R_PPC_ADDR32|R_PPC_ADDR24|R_PPC_ADDR16|R_PPC_ADDR16_LO|R_PPC_ADDR16_HI|R_PPC_ADDR16_HA|R_PPC_ADDR14|R_PPC_ADDR14_BRTAKEN|R_PPC_ADDR14_BRNTAKEN|R_PPC_REL24
include $(srctree)/lib/vdso/Makefile

obj-vdso32 = sigtramp32-32.o gettimeofday-32.o datapage-32.o cacheflush-32.o note-32.o getcpu-32.o

ifneq ($(c-gettimeofday-y),)
  CFLAGS_vgettimeofday-32.o += -include $(c-gettimeofday-y)
  CFLAGS_vgettimeofday-32.o += $(DISABLE_LATENT_ENTROPY_PLUGIN)
  CFLAGS_vgettimeofday-32.o += $(call cc-option, -fno-stack-protector)
  CFLAGS_vgettimeofday-32.o += -DDISABLE_BRANCH_PROFILING
  CFLAGS_vgettimeofday-32.o += -ffreestanding -fasynchronous-unwind-tables
  CFLAGS_REMOVE_vgettimeofday-32.o = $(CC_FLAGS_FTRACE)
  CFLAGS_REMOVE_vgettimeofday-32.o += -mcmodel=medium -mabi=elfv1 -mabi=elfv2 -mcall-aixdesc
endif

# Build rules

ifdef CROSS32_COMPILE
    VDSOCC := $(CROSS32_COMPILE)gcc
else
    VDSOCC := $(CC)
endif

targets := $(obj-vdso32) vdso32.so.dbg vgettimeofday-32.o
obj-vdso32 := $(addprefix $(obj)/, $(obj-vdso32))

GCOV_PROFILE := n
KCOV_INSTRUMENT := n
UBSAN_SANITIZE := n
KASAN_SANITIZE := n

ccflags-y := -shared -fno-common -fno-builtin -nostdlib -Wl,--hash-style=both

CC32FLAGS := -Wl,-soname=linux-vdso32.so.1 -m32
AS32FLAGS := -D__VDSO32__ -s

targets += vdso32.lds
CPPFLAGS_vdso32.lds += -P -C -Upowerpc

# link rule for the .so file, .lds has to be first
$(obj)/vdso32.so.dbg: $(src)/vdso32.lds $(obj-vdso32) $(obj)/vgettimeofday-32.o FORCE
	$(call if_changed,vdso32ld_and_check)

# assembly rules for the .S files
$(obj-vdso32): %-32.o: %.S FORCE
	$(call if_changed_dep,vdso32as)
$(obj)/vgettimeofday-32.o: %-32.o: %.c FORCE
	$(call if_changed_dep,vdso32cc)

# Generate VDSO offsets using helper script
gen-vdso32sym := $(srctree)/$(src)/gen_vdso32_offsets.sh
quiet_cmd_vdso32sym = VDSO32SYM $@
      cmd_vdso32sym = $(NM) $< | $(gen-vdso32sym) | LC_ALL=C sort > $@

include/generated/vdso32-offsets.h: $(obj)/vdso32.so.dbg FORCE
	$(call if_changed,vdso32sym)

# actual build commands
quiet_cmd_vdso32ld_and_check = VDSO32L $@
      cmd_vdso32ld_and_check = $(VDSOCC) $(c_flags) $(CC32FLAGS) -o $@ -Wl,-T$(filter %.lds,$^) $(filter %.o,$^) ; $(cmd_vdso_check)
quiet_cmd_vdso32as = VDSO32A $@
      cmd_vdso32as = $(VDSOCC) $(a_flags) $(CC32FLAGS) $(AS32FLAGS) -c -o $@ $<
quiet_cmd_vdso32cc = VDSO32C $@
      cmd_vdso32cc = $(VDSOCC) $(c_flags) $(CC32FLAGS) -c -o $@ $<
