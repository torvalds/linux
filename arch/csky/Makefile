# SPDX-License-Identifier: GPL-2.0-only
OBJCOPYFLAGS		:=-O binary
GZFLAGS			:=-9

ifdef CONFIG_CPU_HAS_FPU
FPUEXT = f
endif

ifdef CONFIG_CPU_HAS_VDSP
VDSPEXT = v
endif

ifdef CONFIG_CPU_HAS_TEE
TEEEXT = t
endif

ifdef CONFIG_CPU_CK610
CPUTYPE	= ck610
CSKYABI	= abiv1
endif

ifdef CONFIG_CPU_CK810
CPUTYPE = ck810
CSKYABI	= abiv2
endif

ifdef CONFIG_CPU_CK807
CPUTYPE = ck807
CSKYABI	= abiv2
endif

ifdef CONFIG_CPU_CK860
CPUTYPE = ck860
CSKYABI	= abiv2
endif

ifneq ($(CSKYABI),)
MCPU_STR = $(CPUTYPE)$(FPUEXT)$(VDSPEXT)$(TEEEXT)
KBUILD_CFLAGS += -mcpu=$(CPUTYPE) -Wa,-mcpu=$(MCPU_STR)
KBUILD_CFLAGS += -DCSKYCPU_DEF_NAME=\"$(MCPU_STR)\"
KBUILD_CFLAGS += -msoft-float -mdiv
KBUILD_CFLAGS += -fno-tree-vectorize
endif

KBUILD_CFLAGS += -pipe
ifeq ($(CSKYABI),abiv2)
KBUILD_CFLAGS += -mno-stack-size
endif

ifdef CONFIG_STACKTRACE
KBUILD_CFLAGS += -mbacktrace
endif

abidirs := $(patsubst %,arch/csky/%/,$(CSKYABI))
KBUILD_CFLAGS += $(patsubst %,-I$(srctree)/%inc,$(abidirs))

KBUILD_CPPFLAGS += -mlittle-endian
LDFLAGS += -EL

KBUILD_AFLAGS += $(KBUILD_CFLAGS)

head-y := arch/csky/kernel/head.o

core-y += arch/csky/kernel/
core-y += arch/csky/mm/
core-y += arch/csky/$(CSKYABI)/

libs-y += arch/csky/lib/ \
	$(shell $(CC) $(KBUILD_CFLAGS) $(KCFLAGS) -print-libgcc-file-name)

boot := arch/csky/boot
core-y += $(boot)/dts/

all: zImage

zImage Image uImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

define archhelp
  echo  '* zImage       - Compressed kernel image (arch/$(ARCH)/boot/zImage)'
  echo  '  Image        - Uncompressed kernel image (arch/$(ARCH)/boot/Image)'
  echo  '  uImage       - U-Boot wrapped zImage'
endef
