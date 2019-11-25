# SPDX-License-Identifier: GPL-2.0
# Objects to go into the VDSO.

# Absolute relocation type $(ARCH_REL_TYPE_ABS) needs to be defined before
# the inclusion of generic Makefile.
ARCH_REL_TYPE_ABS := R_MIPS_JUMP_SLOT|R_MIPS_GLOB_DAT
include $(srctree)/lib/vdso/Makefile

obj-vdso-y := elf.o vgettimeofday.o sigreturn.o

# Common compiler flags between ABIs.
ccflags-vdso := \
	$(filter -I%,$(KBUILD_CFLAGS)) \
	$(filter -E%,$(KBUILD_CFLAGS)) \
	$(filter -mmicromips,$(KBUILD_CFLAGS)) \
	$(filter -march=%,$(KBUILD_CFLAGS)) \
	$(filter -m%-float,$(KBUILD_CFLAGS)) \
	$(filter -mno-loongson-%,$(KBUILD_CFLAGS)) \
	-D__VDSO__

ifdef CONFIG_CC_IS_CLANG
ccflags-vdso += $(filter --target=%,$(KBUILD_CFLAGS))
endif

#
# The -fno-jump-tables flag only prevents the compiler from generating
# jump tables but does not prevent the compiler from emitting absolute
# offsets.
cflags-vdso := $(ccflags-vdso) \
	$(filter -W%,$(filter-out -Wa$(comma)%,$(KBUILD_CFLAGS))) \
	-O3 -g -fPIC -fno-strict-aliasing -fno-common -fno-builtin -G 0 \
	-fno-stack-protector -fno-jump-tables -DDISABLE_BRANCH_PROFILING \
	$(call cc-option, -fno-asynchronous-unwind-tables) \
	$(call cc-option, -fno-stack-protector)
aflags-vdso := $(ccflags-vdso) \
	-D__ASSEMBLY__ -Wa,-gdwarf-2

ifneq ($(c-gettimeofday-y),)
CFLAGS_vgettimeofday.o = -include $(c-gettimeofday-y)

# config-n32-o32-env.c prepares the environment to build a 32bit vDSO
# library on a 64bit kernel.
# Note: Needs to be included before than the generic library.
CFLAGS_vgettimeofday-o32.o = -include $(srctree)/$(src)/config-n32-o32-env.c -include $(c-gettimeofday-y)
CFLAGS_vgettimeofday-n32.o = -include $(srctree)/$(src)/config-n32-o32-env.c -include $(c-gettimeofday-y)
endif

CFLAGS_REMOVE_vgettimeofday.o = -pg

#
# For the pre-R6 code in arch/mips/vdso/vdso.h for locating
# the base address of VDSO, the linker will emit a R_MIPS_PC32
# relocation in binutils > 2.25 but it will fail with older versions
# because that relocation is not supported for that symbol. As a result
# of which we are forced to disable the VDSO symbols when building
# with < 2.25 binutils on pre-R6 kernels. For more references on why we
# can't use other methods to get the base address of VDSO please refer to
# the comments on that file.
#
ifndef CONFIG_CPU_MIPSR6
  ifeq ($(call ld-ifversion, -lt, 225000000, y),y)
    $(warning MIPS VDSO requires binutils >= 2.25)
    obj-vdso-y := $(filter-out vgettimeofday.o, $(obj-vdso-y))
    ccflags-vdso += -DDISABLE_MIPS_VDSO
  endif
endif

# VDSO linker flags.
VDSO_LDFLAGS := \
	-Wl,-Bsymbolic -Wl,--no-undefined -Wl,-soname=linux-vdso.so.1 \
	$(addprefix -Wl$(comma),$(filter -E%,$(KBUILD_CFLAGS))) \
	-nostdlib -shared -Wl,--hash-style=sysv -Wl,--build-id

CFLAGS_REMOVE_vdso.o = -pg

GCOV_PROFILE := n
UBSAN_SANITIZE := n

#
# Shared build commands.
#

quiet_cmd_vdsold_and_vdso_check = LD      $@
      cmd_vdsold_and_vdso_check = $(cmd_vdsold); $(cmd_vdso_check)

quiet_cmd_vdsold = VDSO    $@
      cmd_vdsold = $(CC) $(c_flags) $(VDSO_LDFLAGS) \
                   -Wl,-T $(filter %.lds,$^) $(filter %.o,$^) -o $@

quiet_cmd_vdsoas_o_S = AS      $@
      cmd_vdsoas_o_S = $(CC) $(a_flags) -c -o $@ $<

# Strip rule for the raw .so files
$(obj)/%.so.raw: OBJCOPYFLAGS := -S
$(obj)/%.so.raw: $(obj)/%.so.dbg.raw FORCE
	$(call if_changed,objcopy)

hostprogs-y := genvdso

quiet_cmd_genvdso = GENVDSO $@
define cmd_genvdso
	$(foreach file,$(filter %.raw,$^),cp $(file) $(file:%.raw=%) &&) \
	$(obj)/genvdso $(<:%.raw=%) $(<:%.dbg.raw=%) $@ $(VDSO_NAME)
endef

#
# Build native VDSO.
#

native-abi := $(filter -mabi=%,$(KBUILD_CFLAGS))

targets += $(obj-vdso-y)
targets += vdso.lds
targets += vdso.so.dbg.raw vdso.so.raw
targets += vdso.so.dbg vdso.so
targets += vdso-image.c

obj-vdso := $(obj-vdso-y:%.o=$(obj)/%.o)

$(obj-vdso): KBUILD_CFLAGS := $(cflags-vdso) $(native-abi)
$(obj-vdso): KBUILD_AFLAGS := $(aflags-vdso) $(native-abi)

$(obj)/vdso.lds: KBUILD_CPPFLAGS := $(ccflags-vdso) $(native-abi)

$(obj)/vdso.so.dbg.raw: $(obj)/vdso.lds $(obj-vdso) FORCE
	$(call if_changed,vdsold_and_vdso_check)

$(obj)/vdso-image.c: $(obj)/vdso.so.dbg.raw $(obj)/vdso.so.raw \
                     $(obj)/genvdso FORCE
	$(call if_changed,genvdso)

obj-y += vdso-image.o

#
# Build O32 VDSO.
#

# Define these outside the ifdef to ensure they are picked up by clean.
targets += $(obj-vdso-y:%.o=%-o32.o)
targets += vdso-o32.lds
targets += vdso-o32.so.dbg.raw vdso-o32.so.raw
targets += vdso-o32.so.dbg vdso-o32.so
targets += vdso-o32-image.c

ifdef CONFIG_MIPS32_O32

obj-vdso-o32 := $(obj-vdso-y:%.o=$(obj)/%-o32.o)

$(obj-vdso-o32): KBUILD_CFLAGS := $(cflags-vdso) -mabi=32
$(obj-vdso-o32): KBUILD_AFLAGS := $(aflags-vdso) -mabi=32

$(obj)/%-o32.o: $(src)/%.S FORCE
	$(call if_changed_dep,vdsoas_o_S)

$(obj)/%-o32.o: $(src)/%.c FORCE
	$(call cmd,force_checksrc)
	$(call if_changed_rule,cc_o_c)

$(obj)/vdso-o32.lds: KBUILD_CPPFLAGS := $(ccflags-vdso) -mabi=32
$(obj)/vdso-o32.lds: $(src)/vdso.lds.S FORCE
	$(call if_changed_dep,cpp_lds_S)

$(obj)/vdso-o32.so.dbg.raw: $(obj)/vdso-o32.lds $(obj-vdso-o32) FORCE
	$(call if_changed,vdsold_and_vdso_check)

$(obj)/vdso-o32-image.c: VDSO_NAME := o32
$(obj)/vdso-o32-image.c: $(obj)/vdso-o32.so.dbg.raw $(obj)/vdso-o32.so.raw \
                         $(obj)/genvdso FORCE
	$(call if_changed,genvdso)

obj-y += vdso-o32-image.o

endif

#
# Build N32 VDSO.
#

targets += $(obj-vdso-y:%.o=%-n32.o)
targets += vdso-n32.lds
targets += vdso-n32.so.dbg.raw vdso-n32.so.raw
targets += vdso-n32.so.dbg vdso-n32.so
targets += vdso-n32-image.c

ifdef CONFIG_MIPS32_N32

obj-vdso-n32 := $(obj-vdso-y:%.o=$(obj)/%-n32.o)

$(obj-vdso-n32): KBUILD_CFLAGS := $(cflags-vdso) -mabi=n32
$(obj-vdso-n32): KBUILD_AFLAGS := $(aflags-vdso) -mabi=n32

$(obj)/%-n32.o: $(src)/%.S FORCE
	$(call if_changed_dep,vdsoas_o_S)

$(obj)/%-n32.o: $(src)/%.c FORCE
	$(call cmd,force_checksrc)
	$(call if_changed_rule,cc_o_c)

$(obj)/vdso-n32.lds: KBUILD_CPPFLAGS := $(ccflags-vdso) -mabi=n32
$(obj)/vdso-n32.lds: $(src)/vdso.lds.S FORCE
	$(call if_changed_dep,cpp_lds_S)

$(obj)/vdso-n32.so.dbg.raw: $(obj)/vdso-n32.lds $(obj-vdso-n32) FORCE
	$(call if_changed,vdsold_and_vdso_check)

$(obj)/vdso-n32-image.c: VDSO_NAME := n32
$(obj)/vdso-n32-image.c: $(obj)/vdso-n32.so.dbg.raw $(obj)/vdso-n32.so.raw \
                         $(obj)/genvdso FORCE
	$(call if_changed,genvdso)

obj-y += vdso-n32-image.o

endif

# FIXME: Need install rule for debug.
# Needs to deal with dependency for generation of dbg by cmd_genvdso...
