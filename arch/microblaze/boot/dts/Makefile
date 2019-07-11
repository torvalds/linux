# SPDX-License-Identifier: GPL-2.0
#

dtb-y := system.dtb

ifneq ($(DTB),)
obj-y += linked_dtb.o

# Ensure system.dtb exists
$(obj)/linked_dtb.o: $(obj)/system.dtb

# Generate system.dtb from $(DTB).dtb
ifneq ($(DTB),system)
$(obj)/system.dtb: $(obj)/$(DTB).dtb
	$(call if_changed,shipped)
endif
endif

# Rule to build device tree blobs
DTC_FLAGS := -p 1024
