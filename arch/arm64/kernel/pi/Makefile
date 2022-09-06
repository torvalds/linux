# SPDX-License-Identifier: GPL-2.0
# Copyright 2022 Google LLC

KBUILD_CFLAGS	:= $(subst $(CC_FLAGS_FTRACE),,$(KBUILD_CFLAGS)) -fpie \
		   -Os -DDISABLE_BRANCH_PROFILING $(DISABLE_STACKLEAK_PLUGIN) \
		   $(call cc-option,-mbranch-protection=none) \
		   -I$(srctree)/scripts/dtc/libfdt -fno-stack-protector \
		   -include $(srctree)/include/linux/hidden.h \
		   -D__DISABLE_EXPORTS -ffreestanding -D__NO_FORTIFY \
		   $(call cc-option,-fno-addrsig)

# remove SCS flags from all objects in this directory
KBUILD_CFLAGS	:= $(filter-out $(CC_FLAGS_SCS), $(KBUILD_CFLAGS))
# disable LTO
KBUILD_CFLAGS	:= $(filter-out $(CC_FLAGS_LTO), $(KBUILD_CFLAGS))

GCOV_PROFILE	:= n
KASAN_SANITIZE	:= n
KCSAN_SANITIZE	:= n
UBSAN_SANITIZE	:= n
KCOV_INSTRUMENT	:= n

$(obj)/%.pi.o: OBJCOPYFLAGS := --prefix-symbols=__pi_ \
			       --remove-section=.note.gnu.property \
			       --prefix-alloc-sections=.init
$(obj)/%.pi.o: $(obj)/%.o FORCE
	$(call if_changed,objcopy)

$(obj)/lib-%.o: $(srctree)/lib/%.c FORCE
	$(call if_changed_rule,cc_o_c)

obj-y		:= kaslr_early.pi.o lib-fdt.pi.o lib-fdt_ro.pi.o
extra-y		:= $(patsubst %.pi.o,%.o,$(obj-y))
