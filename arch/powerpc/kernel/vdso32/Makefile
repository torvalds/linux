# SPDX-License-Identifier: GPL-2.0

# List of files in the vdso, has to be asm only for now

ARCH_REL_TYPE_ABS := R_PPC_JUMP_SLOT|R_PPC_GLOB_DAT|R_PPC_ADDR32|R_PPC_ADDR24|R_PPC_ADDR16|R_PPC_ADDR16_LO|R_PPC_ADDR16_HI|R_PPC_ADDR16_HA|R_PPC_ADDR14|R_PPC_ADDR14_BRTAKEN|R_PPC_ADDR14_BRNTAKEN|R_PPC_REL24
include $(srctree)/lib/vdso/Makefile

obj-vdso32 = sigtramp.o gettimeofday.o datapage.o cacheflush.o note.o getcpu.o

ifneq ($(c-gettimeofday-y),)
  CFLAGS_vgettimeofday.o += -include $(c-gettimeofday-y)
  CFLAGS_vgettimeofday.o += $(DISABLE_LATENT_ENTROPY_PLUGIN)
  CFLAGS_vgettimeofday.o += $(call cc-option, -fno-stack-protector)
  CFLAGS_vgettimeofday.o += -DDISABLE_BRANCH_PROFILING
  CFLAGS_vgettimeofday.o += -ffreestanding -fasynchronous-unwind-tables
  CFLAGS_REMOVE_vgettimeofday.o = $(CC_FLAGS_FTRACE)
endif

# Build rules

ifdef CROSS32_COMPILE
    VDSOCC := $(CROSS32_COMPILE)gcc
else
    VDSOCC := $(CC)
endif

CC32FLAGS :=
ifdef CONFIG_PPC64
CC32FLAGS += -m32
KBUILD_CFLAGS := $(filter-out -mcmodel=medium -mabi=elfv1 -mabi=elfv2 -mcall-aixdesc,$(KBUILD_CFLAGS))
endif

targets := $(obj-vdso32) vdso32.so.dbg vgettimeofday.o
obj-vdso32 := $(addprefix $(obj)/, $(obj-vdso32))

GCOV_PROFILE := n
KCOV_INSTRUMENT := n
UBSAN_SANITIZE := n
KASAN_SANITIZE := n

ccflags-y := -shared -fno-common -fno-builtin -nostdlib \
	-Wl,-soname=linux-vdso32.so.1 -Wl,--hash-style=both
asflags-y := -D__VDSO32__ -s

obj-y += vdso32_wrapper.o
targets += vdso32.lds
CPPFLAGS_vdso32.lds += -P -C -Upowerpc

# link rule for the .so file, .lds has to be first
$(obj)/vdso32.so.dbg: $(src)/vdso32.lds $(obj-vdso32) $(obj)/vgettimeofday.o FORCE
	$(call if_changed,vdso32ld_and_check)

# assembly rules for the .S files
$(obj-vdso32): %.o: %.S FORCE
	$(call if_changed_dep,vdso32as)
$(obj)/vgettimeofday.o: %.o: %.c FORCE
	$(call if_changed_dep,vdso32cc)

# Generate VDSO offsets using helper script
gen-vdsosym := $(srctree)/$(src)/gen_vdso_offsets.sh
quiet_cmd_vdsosym = VDSOSYM $@
      cmd_vdsosym = $(NM) $< | $(gen-vdsosym) | LC_ALL=C sort > $@

include/generated/vdso32-offsets.h: $(obj)/vdso32.so.dbg FORCE
	$(call if_changed,vdsosym)

# actual build commands
quiet_cmd_vdso32ld_and_check = VDSO32L $@
      cmd_vdso32ld_and_check = $(VDSOCC) $(c_flags) $(CC32FLAGS) -o $@ -Wl,-T$(filter %.lds,$^) $(filter %.o,$^) ; $(cmd_vdso_check)
quiet_cmd_vdso32as = VDSO32A $@
      cmd_vdso32as = $(VDSOCC) $(a_flags) $(CC32FLAGS) -c -o $@ $<
quiet_cmd_vdso32cc = VDSO32C $@
      cmd_vdso32cc = $(VDSOCC) $(c_flags) $(CC32FLAGS) -c -o $@ $<
