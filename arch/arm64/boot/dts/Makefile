dtb-$(CONFIG_ARCH_THUNDER) += thunder-88xx.dtb
dtb-$(CONFIG_ARCH_VEXPRESS) += rtsm_ve-aemv8a.dtb foundation-v8.dtb
dtb-$(CONFIG_ARCH_XGENE) += apm-mustang.dtb

targets += dtbs dtbs_install
targets += $(dtb-y)

dtbs: $(addprefix $(obj)/, $(dtb-y))

clean-files := *.dtb

dtbs_install: $(addsuffix _dtbinst_, $(dtb-y))
