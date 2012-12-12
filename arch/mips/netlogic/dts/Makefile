obj-$(CONFIG_DT_XLP_EVP) := xlp_evp.dtb.o

$(obj)/%.dtb: $(obj)/%.dts
	$(call if_changed,dtc)
