# SPDX-License-Identifier: GPL-2.0

OBJECT_FILES_NON_STANDARD := y

purgatory-y := head.o purgatory.o string.o sha256.o mem.o

targets += $(purgatory-y) purgatory.lds purgatory purgatory.chk purgatory.ro
PURGATORY_OBJS = $(addprefix $(obj)/,$(purgatory-y))

$(obj)/sha256.o: $(srctree)/lib/crypto/sha256.c FORCE
	$(call if_changed_rule,cc_o_c)

CFLAGS_sha256.o := -D__DISABLE_EXPORTS

$(obj)/mem.o: $(srctree)/arch/s390/lib/mem.S FORCE
	$(call if_changed_rule,as_o_S)

KCOV_INSTRUMENT := n
GCOV_PROFILE := n
UBSAN_SANITIZE := n
KASAN_SANITIZE := n
KCSAN_SANITIZE := n

KBUILD_CFLAGS := -fno-strict-aliasing -Wall -Wstrict-prototypes
KBUILD_CFLAGS += -Wno-pointer-sign -Wno-sign-compare
KBUILD_CFLAGS += -fno-zero-initialized-in-bss -fno-builtin -ffreestanding
KBUILD_CFLAGS += -c -MD -Os -m64 -msoft-float -fno-common
KBUILD_CFLAGS += -fno-stack-protector
KBUILD_CFLAGS += $(CLANG_FLAGS)
KBUILD_CFLAGS += $(call cc-option,-fno-PIE)
KBUILD_AFLAGS := $(filter-out -DCC_USING_EXPOLINE,$(KBUILD_AFLAGS))

# Since we link purgatory with -r unresolved symbols are not checked, so we
# also link a purgatory.chk binary without -r to check for unresolved symbols.
PURGATORY_LDFLAGS := -nostdlib -z nodefaultlib
LDFLAGS_purgatory := -r $(PURGATORY_LDFLAGS) -T
LDFLAGS_purgatory.chk := -e purgatory_start $(PURGATORY_LDFLAGS)
$(obj)/purgatory: $(obj)/purgatory.lds $(PURGATORY_OBJS) FORCE
		$(call if_changed,ld)

$(obj)/purgatory.chk: $(obj)/purgatory FORCE
		$(call if_changed,ld)

OBJCOPYFLAGS_purgatory.ro := -O elf64-s390
OBJCOPYFLAGS_purgatory.ro += --remove-section='*debug*'
OBJCOPYFLAGS_purgatory.ro += --remove-section='.comment'
OBJCOPYFLAGS_purgatory.ro += --remove-section='.note.*'
$(obj)/purgatory.ro: $(obj)/purgatory $(obj)/purgatory.chk FORCE
		$(call if_changed,objcopy)

$(obj)/kexec-purgatory.o: $(obj)/kexec-purgatory.S $(obj)/purgatory.ro FORCE
	$(call if_changed_rule,as_o_S)

obj-$(CONFIG_ARCH_HAS_KEXEC_PURGATORY) += kexec-purgatory.o
