# SPDX-License-Identifier: GPL-2.0
# Objects to go into the VDSO.
obj-vdso-y := elf.o gettimeofday.o sigreturn.o

# Common compiler flags between ABIs.
ccflags-vdso := \
	$(filter -I%,$(KBUILD_CFLAGS)) \
	$(filter -E%,$(KBUILD_CFLAGS)) \
	$(filter -mmicromips,$(KBUILD_CFLAGS)) \
	$(filter -march=%,$(KBUILD_CFLAGS)) \
	$(filter -m%-float,$(KBUILD_CFLAGS)) \
	-D__VDSO__

ifeq ($(cc-name),clang)
ccflags-vdso += $(filter --target=%,$(KBUILD_CFLAGS))
endif

cflags-vdso := $(ccflags-vdso) \
	$(filter -W%,$(filter-out -Wa$(comma)%,$(KBUILD_CFLAGS))) \
	-O2 -g -fPIC -fno-strict-aliasing -fno-common -fno-builtin -G 0 \
	-DDISABLE_BRANCH_PROFILING \
	$(call cc-option, -fno-asynchronous-unwind-tables) \
	$(call cc-option, -fno-stack-protector)
aflags-vdso := $(ccflags-vdso) \
	-D__ASSEMBLY__ -Wa,-gdwarf-2

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
    obj-vdso-y := $(filter-out gettimeofday.o, $(obj-vdso-y))
    ccflags-vdso += -DDISABLE_MIPS_VDSO
  endif
endif

# VDSO linker flags.
VDSO_LDFLAGS := \
	-Wl,-Bsymbolic -Wl,--no-undefined -Wl,-soname=linux-vdso.so.1 \
	$(addprefix -Wl$(comma),$(filter -E%,$(KBUILD_CFLAGS))) \
	-nostdlib -shared \
	$(call cc-ldoption, -Wl$(comma)--hash-style=sysv) \
	$(call cc-ldoption, -Wl$(comma)--build-id)

GCOV_PROFILE := n

#
# Shared build commands.
#

quiet_cmd_vdsold = VDSO    $@
      cmd_vdsold = $(CC) $(c_flags) $(VDSO_LDFLAGS) \
                   -Wl,-T $(filter %.lds,$^) $(filter %.o,$^) -o $@

quiet_cmd_vdsoas_o_S = AS       $@
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
	$(call if_changed,vdsold)

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
	$(call if_changed,vdsold)

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
	$(call if_changed,vdsold)

$(obj)/vdso-n32-image.c: VDSO_NAME := n32
$(obj)/vdso-n32-image.c: $(obj)/vdso-n32.so.dbg.raw $(obj)/vdso-n32.so.raw \
                         $(obj)/genvdso FORCE
	$(call if_changed,genvdso)

obj-y += vdso-n32-image.o

endif

# FIXME: Need install rule for debug.
# Needs to deal with dependency for generation of dbg by cmd_genvdso...
