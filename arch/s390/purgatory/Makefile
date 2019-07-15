# SPDX-License-Identifier: GPL-2.0

OBJECT_FILES_NON_STANDARD := y

purgatory-y := head.o purgatory.o string.o sha256.o mem.o

targets += $(purgatory-y) purgatory.lds purgatory purgatory.ro
PURGATORY_OBJS = $(addprefix $(obj)/,$(purgatory-y))

$(obj)/sha256.o: $(srctree)/lib/sha256.c FORCE
	$(call if_changed_rule,cc_o_c)

$(obj)/mem.o: $(srctree)/arch/s390/lib/mem.S FORCE
	$(call if_changed_rule,as_o_S)

$(obj)/string.o: $(srctree)/arch/s390/lib/string.c FORCE
	$(call if_changed_rule,cc_o_c)

KBUILD_CFLAGS := -fno-strict-aliasing -Wall -Wstrict-prototypes
KBUILD_CFLAGS += -Wno-pointer-sign -Wno-sign-compare
KBUILD_CFLAGS += -fno-zero-initialized-in-bss -fno-builtin -ffreestanding
KBUILD_CFLAGS += -c -MD -Os -m64 -msoft-float -fno-common
KBUILD_CFLAGS += $(CLANG_FLAGS)
KBUILD_CFLAGS += $(call cc-option,-fno-PIE)
KBUILD_AFLAGS := $(filter-out -DCC_USING_EXPOLINE,$(KBUILD_AFLAGS))

LDFLAGS_purgatory := -r --no-undefined -nostdlib -z nodefaultlib -T
$(obj)/purgatory: $(obj)/purgatory.lds $(PURGATORY_OBJS) FORCE
		$(call if_changed,ld)

OBJCOPYFLAGS_purgatory.ro := -O elf64-s390
OBJCOPYFLAGS_purgatory.ro += --remove-section='*debug*'
OBJCOPYFLAGS_purgatory.ro += --remove-section='.comment'
OBJCOPYFLAGS_purgatory.ro += --remove-section='.note.*'
$(obj)/purgatory.ro: $(obj)/purgatory FORCE
		$(call if_changed,objcopy)

$(obj)/kexec-purgatory.o: $(obj)/kexec-purgatory.S $(obj)/purgatory.ro FORCE
	$(call if_changed_rule,as_o_S)

obj-$(CONFIG_ARCH_HAS_KEXEC_PURGATORY) += kexec-purgatory.o
