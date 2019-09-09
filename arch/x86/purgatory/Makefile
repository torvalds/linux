# SPDX-License-Identifier: GPL-2.0
OBJECT_FILES_NON_STANDARD := y

purgatory-y := purgatory.o stack.o setup-x86_$(BITS).o sha256.o entry64.o string.o

targets += $(purgatory-y)
PURGATORY_OBJS = $(addprefix $(obj)/,$(purgatory-y))

$(obj)/string.o: $(srctree)/arch/x86/boot/compressed/string.c FORCE
	$(call if_changed_rule,cc_o_c)

$(obj)/sha256.o: $(srctree)/lib/sha256.c FORCE
	$(call if_changed_rule,cc_o_c)

LDFLAGS_purgatory.ro := -e purgatory_start -r --no-undefined -nostdlib -z nodefaultlib
targets += purgatory.ro

KASAN_SANITIZE	:= n
KCOV_INSTRUMENT := n

# Default KBUILD_CFLAGS can have -pg option set when FTRACE is enabled. That
# in turn leaves some undefined symbols like __fentry__ in purgatory and not
# sure how to relocate those.
ifdef CONFIG_FUNCTION_TRACER
CFLAGS_REMOVE_sha256.o		+= $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_purgatory.o	+= $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_string.o		+= $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_kexec-purgatory.o	+= $(CC_FLAGS_FTRACE)
endif

ifdef CONFIG_STACKPROTECTOR
CFLAGS_REMOVE_sha256.o		+= -fstack-protector
CFLAGS_REMOVE_purgatory.o	+= -fstack-protector
CFLAGS_REMOVE_string.o		+= -fstack-protector
CFLAGS_REMOVE_kexec-purgatory.o	+= -fstack-protector
endif

ifdef CONFIG_STACKPROTECTOR_STRONG
CFLAGS_REMOVE_sha256.o		+= -fstack-protector-strong
CFLAGS_REMOVE_purgatory.o	+= -fstack-protector-strong
CFLAGS_REMOVE_string.o		+= -fstack-protector-strong
CFLAGS_REMOVE_kexec-purgatory.o	+= -fstack-protector-strong
endif

ifdef CONFIG_RETPOLINE
CFLAGS_REMOVE_sha256.o		+= $(RETPOLINE_CFLAGS)
CFLAGS_REMOVE_purgatory.o	+= $(RETPOLINE_CFLAGS)
CFLAGS_REMOVE_string.o		+= $(RETPOLINE_CFLAGS)
CFLAGS_REMOVE_kexec-purgatory.o	+= $(RETPOLINE_CFLAGS)
endif

$(obj)/purgatory.ro: $(PURGATORY_OBJS) FORCE
		$(call if_changed,ld)

targets += kexec-purgatory.c

quiet_cmd_bin2c = BIN2C   $@
      cmd_bin2c = $(objtree)/scripts/bin2c kexec_purgatory < $< > $@

$(obj)/kexec-purgatory.c: $(obj)/purgatory.ro FORCE
	$(call if_changed,bin2c)

obj-$(CONFIG_KEXEC_FILE)	+= kexec-purgatory.o
