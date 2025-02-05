# SPDX-License-Identifier: GPL-2.0
# This file was copied from arm64/kernel/pi/Makefile.

KBUILD_CFLAGS	:= $(subst $(CC_FLAGS_FTRACE),,$(KBUILD_CFLAGS)) -fpie \
		   -Os -DDISABLE_BRANCH_PROFILING $(DISABLE_STACKLEAK_PLUGIN) \
		   $(call cc-option,-mbranch-protection=none) \
		   -I$(srctree)/scripts/dtc/libfdt -fno-stack-protector \
		   -include $(srctree)/include/linux/hidden.h \
		   -D__DISABLE_EXPORTS -ffreestanding \
		   -fno-asynchronous-unwind-tables -fno-unwind-tables \
		   $(call cc-option,-fno-addrsig)

# Disable LTO
KBUILD_CFLAGS	:= $(filter-out $(CC_FLAGS_LTO), $(KBUILD_CFLAGS))

KBUILD_CFLAGS	+= -mcmodel=medany

CFLAGS_cmdline_early.o += -D__NO_FORTIFY
CFLAGS_fdt_early.o += -D__NO_FORTIFY
# lib/string.c already defines __NO_FORTIFY
CFLAGS_ctype.o += -D__NO_FORTIFY
CFLAGS_lib-fdt.o += -D__NO_FORTIFY
CFLAGS_lib-fdt_ro.o += -D__NO_FORTIFY
CFLAGS_archrandom_early.o += -D__NO_FORTIFY

$(obj)/%.pi.o: OBJCOPYFLAGS := --prefix-symbols=__pi_ \
			       --remove-section=.note.gnu.property \
			       --prefix-alloc-sections=.init.pi
$(obj)/%.pi.o: $(obj)/%.o FORCE
	$(call if_changed,objcopy)

$(obj)/lib-%.o: $(srctree)/lib/%.c FORCE
	$(call if_changed_rule,cc_o_c)

$(obj)/string.o: $(srctree)/lib/string.c FORCE
	$(call if_changed_rule,cc_o_c)

$(obj)/ctype.o: $(srctree)/lib/ctype.c FORCE
	$(call if_changed_rule,cc_o_c)

obj-y		:= cmdline_early.pi.o fdt_early.pi.o string.pi.o ctype.pi.o lib-fdt.pi.o lib-fdt_ro.pi.o archrandom_early.pi.o
extra-y		:= $(patsubst %.pi.o,%.o,$(obj-y))
