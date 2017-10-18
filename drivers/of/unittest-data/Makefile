obj-y += testcases.dtb.o
obj-y += overlay.dtb.o
obj-y += overlay_bad_phandle.dtb.o
obj-y += overlay_base.dtb.o

targets += testcases.dtb testcases.dtb.S
targets += overlay.dtb overlay.dtb.S
targets += overlay_bad_phandle.dtb overlay_bad_phandle.dtb.S
targets += overlay_base.dtb overlay_base.dtb.S

.PRECIOUS: \
	$(obj)/%.dtb.S \
	$(obj)/%.dtb

# enable creation of __symbols__ node
DTC_FLAGS_overlay := -@
DTC_FLAGS_overlay_bad_phandle := -@
DTC_FLAGS_overlay_base := -@
