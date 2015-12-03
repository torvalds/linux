include arch/lkl/auto.conf

KBUILD_CFLAGS += -fno-builtin

ifneq (,$(filter $(OUTPUT_FORMAT),elf64-x86-64 elf64-x86-64-freebsd))
KBUILD_CFLAGS += -fPIC
else ifeq ($(OUTPUT_FORMAT),pe-i386)
prefix=_
# workaround for #include_next<stdarg.h> errors
LINUXINCLUDE := -isystem arch/lkl/include/system $(LINUXINCLUDE)
else
$(error Unrecognized platform: $(OUTPUT_FORMAT))
endif

ifneq (,$(filter $(OUTPUT_FORMAT),elf64-x86-64-freebsd))
NPROC=$(shell sysctl -n hw.ncpu)
else
NPROC=$(shell nproc)
endif

LDFLAGS_vmlinux += -r
LKL_ENTRY_POINTS := lkl_start_kernel lkl_sys_halt lkl_syscall lkl_trigger_irq \
	lkl_get_free_irq lkl_put_irq

core-y += arch/lkl/kernel/

all: lkl.o

lkl.o: vmlinux
	$(OBJCOPY) -R .eh_frame $(foreach sym,$(LKL_ENTRY_POINTS),-G$(prefix)$(sym)) vmlinux lkl.o

install: lkl.o __headers
	@echo "  INSTALL	$(INSTALL_PATH)/lib/lkl.o"
	@cp lkl.o $(INSTALL_PATH)/lib/
	@arch/lkl/scripts/headers_install.py \
		$(subst -j,-j$(NPROC),$(findstring -j,$(MAKEFLAGS))) \
		$(INSTALL_PATH)/include

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

define archhelp
  echo '  install	- Install library and headers to INSTALL_PATH/{lib,include}'
endef



