dtb-$(CONFIG_ARCH_VEXPRESS) += rtsm_ve-aemv8a.dtb foundation-v8.dtb fvp-base-gicv2legacy-psci.dtb fvp-base-gicv2-psci.dtb fvp-base-gicv3-psci.dtb fvp-foundation-gicv2legacy-psci.dtb fvp-foundation-gicv2-psci.dtb fvp-foundation-gicv3-psci.dtb
dtb-$(CONFIG_ARCH_XGENE) += apm-mustang.dtb

targets += dtbs
targets += $(dtb-y)

dtbs: $(addprefix $(obj)/, $(dtb-y))

clean-files := *.dtb
