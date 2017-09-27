#
# linux/arch/c6x/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

KBUILD_DEFCONFIG := dsk6455_defconfig

cflags-y += -mno-dsbt -msdata=none -D__linux__

cflags-$(CONFIG_C6X_BIG_KERNEL) += -mlong-calls

CFLAGS_MODULE   += -mlong-calls -mno-dsbt -msdata=none

CHECKFLAGS      +=

KBUILD_CFLAGS   += $(cflags-y)
KBUILD_AFLAGS   += $(cflags-y)

ifdef CONFIG_CPU_BIG_ENDIAN
KBUILD_CFLAGS   += -mbig-endian
KBUILD_AFLAGS   += -mbig-endian
LINKFLAGS       += -mbig-endian
KBUILD_LDFLAGS  += -mbig-endian
LDFLAGS += -EB
endif

head-y          := arch/c6x/kernel/head.o
core-y          += arch/c6x/kernel/ arch/c6x/mm/ arch/c6x/platforms/
libs-y          += arch/c6x/lib/

# Default to vmlinux.bin, override when needed
all: vmlinux.bin

boot := arch/$(ARCH)/boot

# Are we making a dtbImage.<boardname> target? If so, crack out the boardname
DTB:=$(subst dtbImage.,,$(filter dtbImage.%, $(MAKECMDGOALS)))
export DTB

ifneq ($(DTB),)
core-y	+= $(boot)/dts/
endif

# With make 3.82 we cannot mix normal and wildcard targets

vmlinux.bin: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(patsubst %,$(boot)/%,$@)

dtbImage.%: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(patsubst %,$(boot)/%,$@)

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

define archhelp
  @echo '  vmlinux.bin     - Binary kernel image (arch/$(ARCH)/boot/vmlinux.bin)'
  @echo '  dtbImage.<dt>   - ELF image with $(arch)/boot/dts/<dt>.dts linked in'
  @echo '                  - stripped elf with fdt blob'
endef
