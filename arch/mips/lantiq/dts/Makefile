obj-$(CONFIG_DT_EASY50712) := easy50712.dtb.o

$(obj)/%.dtb: $(obj)/%.dts
	$(call if_changed,dtc)
