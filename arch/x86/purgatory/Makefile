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

# These are adjustments to the compiler flags used for objects that
# make up the standalone purgatory.ro

PURGATORY_CFLAGS_REMOVE := -mcmodel=kernel
PURGATORY_CFLAGS := -mcmodel=large -ffreestanding -fno-zero-initialized-in-bss

# Default KBUILD_CFLAGS can have -pg option set when FTRACE is enabled. That
# in turn leaves some undefined symbols like __fentry__ in purgatory and not
# sure how to relocate those.
ifdef CONFIG_FUNCTION_TRACER
PURGATORY_CFLAGS_REMOVE		+= $(CC_FLAGS_FTRACE)
endif

ifdef CONFIG_STACKPROTECTOR
PURGATORY_CFLAGS_REMOVE		+= -fstack-protector
endif

ifdef CONFIG_STACKPROTECTOR_STRONG
PURGATORY_CFLAGS_REMOVE		+= -fstack-protector-strong
endif

ifdef CONFIG_RETPOLINE
PURGATORY_CFLAGS_REMOVE		+= $(RETPOLINE_CFLAGS)
endif

CFLAGS_REMOVE_purgatory.o	+= $(PURGATORY_CFLAGS_REMOVE)
CFLAGS_purgatory.o		+= $(PURGATORY_CFLAGS)

CFLAGS_REMOVE_sha256.o		+= $(PURGATORY_CFLAGS_REMOVE)
CFLAGS_sha256.o			+= $(PURGATORY_CFLAGS)

CFLAGS_REMOVE_string.o		+= $(PURGATORY_CFLAGS_REMOVE)
CFLAGS_string.o			+= $(PURGATORY_CFLAGS)

$(obj)/purgatory.ro: $(PURGATORY_OBJS) FORCE
		$(call if_changed,ld)

targets += kexec-purgatory.c

quiet_cmd_bin2c = BIN2C   $@
      cmd_bin2c = $(objtree)/scripts/bin2c kexec_purgatory < $< > $@

$(obj)/kexec-purgatory.c: $(obj)/purgatory.ro FORCE
	$(call if_changed,bin2c)

obj-$(CONFIG_KEXEC_FILE)	+= kexec-purgatory.o
