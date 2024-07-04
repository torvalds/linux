# SPDX-License-Identifier: GPL-2.0
#
# linux/arch/arm/boot/bootp/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#

ifdef PHYS_OFFSET
add_hex = $(shell printf 0x%x $$(( $(1) + $(2) )) )

# If PHYS_OFFSET is set, INITRD_PHYS and PARAMS_PHYS can be derived,
# otherwise they must be passed on the command line.
#
# Note: the following conditions must always be true:
#   PARAMS_PHYS must be within 4MB of ZRELADDR
#   INITRD_PHYS must be in RAM

PARAMS_PHYS := $(call add_hex, $(PHYS_OFFSET), 0x100)

# guess an initrd location if possible
initrd_offset-$(CONFIG_ARCH_FOOTBRIDGE)	+= 0x00800000
initrd_offset-$(CONFIG_ARCH_SA1100)	+= 0x00800000
initrd_offset-$(CONFIG_ARCH_RPC)	+= 0x08000000
INITRD_OFFSET := $(initrd_offset-y)
ifdef INITRD_OFFSET
INITRD_PHYS := $(call add_hex, $(PHYS_OFFSET), $(INITRD_OFFSET))
endif

endif

PHONY += initrd
initrd:
	@test "$(PARAMS_PHYS)" != "" || \
	(echo bootpImage: You must specify PHYS_OFFSET of PARAMS_PHYS ; exit -1)
	@test "$(INITRD_PHYS)" != "" || \
	(echo bootpImage: You must specify INITRD_OFFSET or INITRD_PHYS ; exit -1)
	@test "$(INITRD)" != "" || \
	(echo bootpImage: You must specify INITRD; exit -1)

LDFLAGS_bootp	:= --no-undefined -X \
		 --defsym initrd_phys=$(INITRD_PHYS) \
		 --defsym params_phys=$(PARAMS_PHYS) -T
AFLAGS_initrd.o :=-DINITRD=\"$(INITRD)\"

targets	:= bootp init.o kernel.o initrd.o

# Note that bootp.lds picks up kernel.o and initrd.o
$(obj)/bootp:	$(src)/bootp.lds $(addprefix $(obj)/,init.o kernel.o initrd.o) FORCE
	$(call if_changed,ld)

# kernel.o and initrd.o includes a binary image using
# .incbin, a dependency which is not tracked automatically

$(obj)/kernel.o: arch/arm/boot/zImage FORCE

$(obj)/initrd.o: initrd $(INITRD) FORCE

PHONY += $(INITRD)
