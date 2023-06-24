# SPDX-License-Identifier: GPL-2.0

KBUILD_CFLAGS += -fno-builtin

exec_format = $(firstword $(subst -, ,$(CONFIG_OUTPUT_FORMAT)))

ifneq (,$(filter elf64 elf32,$(exec_format)))
KBUILD_CFLAGS += -fPIC
else ifneq (,$(filter pe,$(exec_format)))
ifneq ($(CONFIG_OUTPUT_FORMAT),pe-x86-64)
prefix=_
KBUILD_CFLAGS += -fno-leading-underscore
endif
# workaround for #include_next<stdarg.h> errors
LINUXINCLUDE := -isystem arch/lkl/include/system $(LINUXINCLUDE)
# workaround for https://gcc.gnu.org/bugzilla/show_bug.cgi?id=52991
KBUILD_CFLAGS += -mno-ms-bitfields
else ifneq (,$(CONFIG_OUTPUT_FORMAT))
$(error Unrecognized platform: $(CONFIG_OUTPUT_FORMAT))
endif

ifeq ($(shell uname -s), Linux)
NPROC=$(shell nproc)
else ifneq ($(shell uname -s | grep MSYS),)
NPROC=${NUMBER_OF_PROCESSORS}
else # e.g., FreeBSD
NPROC=$(shell sysctl -n hw.ncpu)
endif

ifdef CONFIG_CC_IS_CLANG
# LKL fuzzing is based on libFuzzer which depends on clang compiler.
ifdef CONFIG_LKL_FUZZING
# This makes sure assembly code is compiled with clang's integerated assembler
# so the result object files are compatible with the fuzzing flag.
KBUILD_CFLAGS += -integrated-as

# We want debug symbols for fuzzing (e.g. better stack trace in gdb).
KBUILD_CFLAGS += -g

# Enabling libfuzzer instrumentation
KBUILD_CFLAGS += -fsanitize=fuzzer-no-link

KEEP_EH_FRAMES := true
endif

# This flag enables clang's extra instrumentation for visualizing line coverage.
ifdef CONFIG_LKL_LINE_COV
KBUILD_CFLAGS += -fprofile-instr-generate -fcoverage-mapping
endif
endif

LDFLAGS_vmlinux += -r
LKL_ENTRY_POINTS := lkl_start_kernel lkl_sys_halt lkl_syscall lkl_trigger_irq \
	lkl_get_free_irq lkl_put_irq lkl_is_running lkl_bug lkl_printf \
	lkl_init lkl_cleanup

ifeq ($(CONFIG_OUTPUT_FORMAT),elf32-i386)
LKL_ENTRY_POINTS += \
	__x86.get_pc_thunk.bx __x86.get_pc_thunk.dx __x86.get_pc_thunk.ax \
	__x86.get_pc_thunk.cx __x86.get_pc_thunk.si __x86.get_pc_thunk.di
endif

core-y += arch/lkl/kernel/
core-y += arch/lkl/mm/
core-y += arch/lkl/drivers/

all: lkl.o arch/lkl/include/generated/uapi/asm/syscall_defs.h

lkl.o: vmlinux
	$(OBJCOPY) $(if $(KEEP_EH_FRAMES),,-R .eh_frame) -R .syscall_defs $(foreach sym,$(LKL_ENTRY_POINTS),-G$(prefix)$(sym)) --prefix-symbols=$(prefix) vmlinux lkl.o

arch/lkl/include/generated/uapi/asm/syscall_defs.h: vmlinux
	$(OBJCOPY) -j .syscall_defs -O binary --set-section-flags .syscall_defs=alloc $< $@
	$(Q) export tmpfile=$(shell mktemp); \
	sed 's/\x0//g' $@ > $$tmpfile; mv $$tmpfile $@ ; rm -f $$tmpfile

install: scripts_unifdef
	@echo "  INSTALL	$(INSTALL_PATH)/lib/lkl.o"
	@mkdir -p $(INSTALL_PATH)/lib/
	@cp lkl.o $(INSTALL_PATH)/lib/
	@$(srctree)/arch/lkl/scripts/headers_install.py \
		$(subst -j,-j$(NPROC),$(findstring -j,$(MAKEFLAGS))) \
		$(INSTALL_PATH)/include

define archhelp
  echo '  install	- Install library and headers to INSTALL_PATH/{lib,include}'
endef



