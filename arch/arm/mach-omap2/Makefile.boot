  zreladdr-y		+= 0x80008000
params_phys-y		:= 0x80000100
initrd_phys-y		:= 0x80800000

dtb-$(CONFIG_SOC_OMAP2420)	+= omap2420-h4.dtb
dtb-$(CONFIG_ARCH_OMAP3)	+= omap3-beagle-xm.dtb omap3-evm.dtb omap3-tobi.dtb
dtb-$(CONFIG_ARCH_OMAP4)	+= omap4-panda.dtb omap4-pandaES.dtb
dtb-$(CONFIG_ARCH_OMAP4)	+= omap4-var_som.dtb omap4-sdp.dtb
dtb-$(CONFIG_SOC_OMAP5)		+= omap5-evm.dtb
