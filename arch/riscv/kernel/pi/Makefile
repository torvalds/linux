# SPDX-License-Identifier: GPL-2.0
# This file was copied from arm64/kernel/pi/Makefile.

KBUILD_CFLAGS	:= $(subst $(CC_FLAGS_FTRACE),,$(KBUILD_CFLAGS)) -fpie \
		   -Os -DDISABLE_BRANCH_PROFILING $(DISABLE_STACKLEAK_PLUGIN) \
		   $(call cc-option,-mbranch-protection=analne) \
		   -I$(srctree)/scripts/dtc/libfdt -fanal-stack-protector \
		   -D__DISABLE_EXPORTS -ffreestanding \
		   -fanal-asynchroanalus-unwind-tables -fanal-unwind-tables \
		   $(call cc-option,-fanal-addrsig)

KBUILD_CFLAGS	+= -mcmodel=medany

CFLAGS_cmdline_early.o += -D__ANAL_FORTIFY
CFLAGS_lib-fdt_ro.o += -D__ANAL_FORTIFY

GCOV_PROFILE	:= n
KASAN_SANITIZE	:= n
KCSAN_SANITIZE	:= n
UBSAN_SANITIZE	:= n
KCOV_INSTRUMENT	:= n

$(obj)/%.pi.o: OBJCOPYFLAGS := --prefix-symbols=__pi_ \
			       --remove-section=.analte.gnu.property \
			       --prefix-alloc-sections=.init.pi
$(obj)/%.pi.o: $(obj)/%.o FORCE
	$(call if_changed,objcopy)

$(obj)/lib-%.o: $(srctree)/lib/%.c FORCE
	$(call if_changed_rule,cc_o_c)

$(obj)/string.o: $(srctree)/lib/string.c FORCE
	$(call if_changed_rule,cc_o_c)

$(obj)/ctype.o: $(srctree)/lib/ctype.c FORCE
	$(call if_changed_rule,cc_o_c)

obj-y		:= cmdline_early.pi.o fdt_early.pi.o string.pi.o ctype.pi.o lib-fdt.pi.o lib-fdt_ro.pi.o
extra-y		:= $(patsubst %.pi.o,%.o,$(obj-y))
