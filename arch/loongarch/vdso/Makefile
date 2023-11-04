# SPDX-License-Identifier: GPL-2.0
# Objects to go into the VDSO.

KASAN_SANITIZE := n
KCOV_INSTRUMENT := n

# Include the generic Makefile to check the built vdso.
include $(srctree)/lib/vdso/Makefile

obj-vdso-y := elf.o vgetcpu.o vgettimeofday.o sigreturn.o

# Common compiler flags between ABIs.
ccflags-vdso := \
	$(filter -I%,$(KBUILD_CFLAGS)) \
	$(filter -E%,$(KBUILD_CFLAGS)) \
	$(filter -march=%,$(KBUILD_CFLAGS)) \
	$(filter -m%-float,$(KBUILD_CFLAGS)) \
	$(CLANG_FLAGS) \
	-D__VDSO__

cflags-vdso := $(ccflags-vdso) \
	-isystem $(shell $(CC) -print-file-name=include) \
	$(filter -W%,$(filter-out -Wa$(comma)%,$(KBUILD_CFLAGS))) \
	-O2 -g -fno-strict-aliasing -fno-common -fno-builtin \
	-fno-stack-protector -fno-jump-tables -DDISABLE_BRANCH_PROFILING \
	$(call cc-option, -fno-asynchronous-unwind-tables) \
	$(call cc-option, -fno-stack-protector)
aflags-vdso := $(ccflags-vdso) \
	-D__ASSEMBLY__ -Wa,-gdwarf-2

ifneq ($(c-gettimeofday-y),)
  CFLAGS_vgettimeofday.o += -include $(c-gettimeofday-y)
endif

# VDSO linker flags.
ldflags-y := -Bsymbolic --no-undefined -soname=linux-vdso.so.1 \
	$(filter -E%,$(KBUILD_CFLAGS)) -nostdlib -shared \
	--hash-style=sysv --build-id -T

GCOV_PROFILE := n

#
# Shared build commands.
#

quiet_cmd_vdsold_and_vdso_check = LD      $@
      cmd_vdsold_and_vdso_check = $(cmd_ld); $(cmd_vdso_check)

quiet_cmd_vdsoas_o_S = AS       $@
      cmd_vdsoas_o_S = $(CC) $(a_flags) -c -o $@ $<

# Generate VDSO offsets using helper script
gen-vdsosym := $(srctree)/$(src)/gen_vdso_offsets.sh
quiet_cmd_vdsosym = VDSOSYM $@
      cmd_vdsosym = $(NM) $< | $(gen-vdsosym) | LC_ALL=C sort > $@

include/generated/vdso-offsets.h: $(obj)/vdso.so.dbg FORCE
	$(call if_changed,vdsosym)

#
# Build native VDSO.
#

native-abi := $(filter -mabi=%,$(KBUILD_CFLAGS))

targets += $(obj-vdso-y)
targets += vdso.lds vdso.so.dbg vdso.so

obj-vdso := $(obj-vdso-y:%.o=$(obj)/%.o)

$(obj-vdso): KBUILD_CFLAGS := $(cflags-vdso) $(native-abi)
$(obj-vdso): KBUILD_AFLAGS := $(aflags-vdso) $(native-abi)

$(obj)/vdso.lds: KBUILD_CPPFLAGS := $(ccflags-vdso) $(native-abi)

$(obj)/vdso.so.dbg: $(obj)/vdso.lds $(obj-vdso) FORCE
	$(call if_changed,vdsold_and_vdso_check)

$(obj)/vdso.so: OBJCOPYFLAGS := -S
$(obj)/vdso.so: $(obj)/vdso.so.dbg FORCE
	$(call if_changed,objcopy)

obj-y += vdso.o

$(obj)/vdso.o : $(obj)/vdso.so
