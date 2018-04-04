# SPDX-License-Identifier: GPL-2.0
DTC_FLAGS_testcases := -Wno-interrupts_property
obj-y += testcases.dtb.o

obj-$(CONFIG_OF_OVERLAY) += overlay.dtb.o \
			    overlay_bad_phandle.dtb.o \
			    overlay_bad_symbol.dtb.o \
			    overlay_base.dtb.o

targets += $(foreach suffix, dtb dtb.S, $(patsubst %.dtb.o,%.$(suffix),$(obj-y)))

# enable creation of __symbols__ node
DTC_FLAGS_overlay := -@
DTC_FLAGS_overlay_bad_phandle := -@
DTC_FLAGS_overlay_bad_symbol := -@
DTC_FLAGS_overlay_base := -@

.PRECIOUS: \
	$(obj)/%.dtb.S \
	$(obj)/%.dtb
