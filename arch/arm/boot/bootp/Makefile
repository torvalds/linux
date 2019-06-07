# SPDX-License-Identifier: GPL-2.0
#
# linux/arch/arm/boot/bootp/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#

GCOV_PROFILE	:= n

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

$(obj)/initrd.o: $(INITRD) FORCE

PHONY += $(INITRD)
