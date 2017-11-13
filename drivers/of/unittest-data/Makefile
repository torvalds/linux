# SPDX-License-Identifier: GPL-2.0
obj-y += testcases.dtb.o

targets += testcases.dtb testcases.dtb.S

ifdef CONFIG_OF_OVERLAY

obj-y += overlay.dtb.o
obj-y += overlay_bad_phandle.dtb.o
obj-y += overlay_bad_symbol.dtb.o
obj-y += overlay_base.dtb.o

targets += overlay.dtb overlay.dtb.S
targets += overlay_bad_phandle.dtb overlay_bad_phandle.dtb.S
targets += overlay_bad_symbol.dtb overlay_bad_symbol.dtb.S
targets += overlay_base.dtb overlay_base.dtb.S

# enable creation of __symbols__ node
DTC_FLAGS_overlay := -@
DTC_FLAGS_overlay_bad_phandle := -@
DTC_FLAGS_overlay_bad_symbol := -@
DTC_FLAGS_overlay_base := -@

endif

.PRECIOUS: \
	$(obj)/%.dtb.S \
	$(obj)/%.dtb
