# SPDX-License-Identifier: GPL-2.0

OBJECT_FILES_NON_STANDARD := y

purgatory-y := head.o purgatory.o string.o sha256.o mem.o

targets += $(purgatory-y) purgatory.ro kexec-purgatory.c
PURGATORY_OBJS = $(addprefix $(obj)/,$(purgatory-y))

$(obj)/sha256.o: $(srctree)/lib/sha256.c
	$(call if_changed_rule,cc_o_c)

$(obj)/mem.o: $(srctree)/arch/s390/lib/mem.S
	$(call if_changed_rule,as_o_S)

$(obj)/string.o: $(srctree)/arch/s390/lib/string.c
	$(call if_changed_rule,cc_o_c)

LDFLAGS_purgatory.ro := -e purgatory_start -r --no-undefined -nostdlib
LDFLAGS_purgatory.ro += -z nodefaultlib
KBUILD_CFLAGS := -fno-strict-aliasing -Wall -Wstrict-prototypes
KBUILD_CFLAGS += -Wno-pointer-sign -Wno-sign-compare
KBUILD_CFLAGS += -fno-zero-initialized-in-bss -fno-builtin -ffreestanding
KBUILD_CFLAGS += -c -MD -Os -m64
KBUILD_CFLAGS += $(call cc-option,-fno-PIE)

$(obj)/purgatory.ro: $(PURGATORY_OBJS) FORCE
		$(call if_changed,ld)

CMD_BIN2C = $(objtree)/scripts/basic/bin2c
quiet_cmd_bin2c = BIN2C   $@
      cmd_bin2c = $(CMD_BIN2C) kexec_purgatory < $< > $@

$(obj)/kexec-purgatory.c: $(obj)/purgatory.ro FORCE
	$(call if_changed,bin2c)

obj-$(CONFIG_ARCH_HAS_KEXEC_PURGATORY) += kexec-purgatory.o
