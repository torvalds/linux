# SPDX-License-Identifier: GPL-2.0
# Copyright 2022 Google LLC

KBUILD_CFLAGS	:= $(subst $(CC_FLAGS_FTRACE),,$(KBUILD_CFLAGS)) -fpie \
		   -Os -DDISABLE_BRANCH_PROFILING $(DISABLE_STACKLEAK_PLUGIN) \
		   $(DISABLE_LATENT_ENTROPY_PLUGIN) \
		   $(call cc-option,-mbranch-protection=none) \
		   -I$(srctree)/scripts/dtc/libfdt -fno-stack-protector \
		   -include $(srctree)/include/linux/hidden.h \
		   -D__DISABLE_EXPORTS -ffreestanding -D__NO_FORTIFY \
		   -fno-asynchronous-unwind-tables -fno-unwind-tables \
		   $(call cc-option,-fno-addrsig)

# this code may run with the MMU off so disable unaligned accesses
CFLAGS_map_range.o += -mstrict-align

# remove SCS flags from all objects in this directory
KBUILD_CFLAGS	:= $(filter-out $(CC_FLAGS_SCS), $(KBUILD_CFLAGS))
# disable LTO
KBUILD_CFLAGS	:= $(filter-out $(CC_FLAGS_LTO), $(KBUILD_CFLAGS))

GCOV_PROFILE	:= n
KASAN_SANITIZE	:= n
KCSAN_SANITIZE	:= n
UBSAN_SANITIZE	:= n
KCOV_INSTRUMENT	:= n

hostprogs	:= relacheck

quiet_cmd_piobjcopy = $(quiet_cmd_objcopy)
      cmd_piobjcopy = $(cmd_objcopy) && $(obj)/relacheck $(@) $(<)

$(obj)/%.pi.o: OBJCOPYFLAGS := --prefix-symbols=__pi_ \
			       --remove-section=.note.gnu.property
$(obj)/%.pi.o: $(obj)/%.o $(obj)/relacheck FORCE
	$(call if_changed,piobjcopy)

# ensure that all the lib- code ends up as __init code and data
$(obj)/lib-%.pi.o: OBJCOPYFLAGS += --prefix-alloc-sections=.init

$(obj)/lib-%.o: $(srctree)/lib/%.c FORCE
	$(call if_changed_rule,cc_o_c)

obj-y					:= idreg-override.pi.o \
					   map_kernel.pi.o map_range.pi.o \
					   lib-fdt.pi.o lib-fdt_ro.pi.o
obj-$(CONFIG_RELOCATABLE)		+= relocate.pi.o
obj-$(CONFIG_RANDOMIZE_BASE)		+= kaslr_early.pi.o
obj-$(CONFIG_UNWIND_PATCH_PAC_INTO_SCS)	+= patch-scs.pi.o
extra-y					:= $(patsubst %.pi.o,%.o,$(obj-y))
