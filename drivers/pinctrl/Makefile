# generic pinmux support

ccflags-$(CONFIG_DEBUG_PINCTRL)	+= -DDEBUG

obj-$(CONFIG_PINCTRL)		+= core.o pinctrl-utils.o
obj-$(CONFIG_PINMUX)		+= pinmux.o
obj-$(CONFIG_PINCONF)		+= pinconf.o
ifeq ($(CONFIG_OF),y)
obj-$(CONFIG_PINCTRL)		+= devicetree.o
endif
obj-$(CONFIG_GENERIC_PINCONF)	+= pinconf-generic.o
obj-$(CONFIG_PINCTRL_ADI2)	+= pinctrl-adi2.o
obj-$(CONFIG_PINCTRL_AS3722)	+= pinctrl-as3722.o
obj-$(CONFIG_PINCTRL_BF54x)	+= pinctrl-adi2-bf54x.o
obj-$(CONFIG_PINCTRL_BF60x)	+= pinctrl-adi2-bf60x.o
obj-$(CONFIG_PINCTRL_AT91)	+= pinctrl-at91.o
obj-$(CONFIG_PINCTRL_BCM2835)	+= pinctrl-bcm2835.o
obj-$(CONFIG_PINCTRL_BAYTRAIL)	+= pinctrl-baytrail.o
obj-$(CONFIG_PINCTRL_BCM281XX)	+= pinctrl-bcm281xx.o
obj-$(CONFIG_PINCTRL_IMX)	+= pinctrl-imx.o
obj-$(CONFIG_PINCTRL_IMX1_CORE)	+= pinctrl-imx1-core.o
obj-$(CONFIG_PINCTRL_IMX1)	+= pinctrl-imx1.o
obj-$(CONFIG_PINCTRL_IMX27)	+= pinctrl-imx27.o
obj-$(CONFIG_PINCTRL_IMX35)	+= pinctrl-imx35.o
obj-$(CONFIG_PINCTRL_IMX50)	+= pinctrl-imx50.o
obj-$(CONFIG_PINCTRL_IMX51)	+= pinctrl-imx51.o
obj-$(CONFIG_PINCTRL_IMX53)	+= pinctrl-imx53.o
obj-$(CONFIG_PINCTRL_IMX6Q)	+= pinctrl-imx6q.o
obj-$(CONFIG_PINCTRL_IMX6Q)	+= pinctrl-imx6dl.o
obj-$(CONFIG_PINCTRL_IMX6SL)	+= pinctrl-imx6sl.o
obj-$(CONFIG_PINCTRL_IMX6SX)	+= pinctrl-imx6sx.o
obj-$(CONFIG_PINCTRL_FALCON)	+= pinctrl-falcon.o
obj-$(CONFIG_PINCTRL_MXS)	+= pinctrl-mxs.o
obj-$(CONFIG_PINCTRL_IMX23)	+= pinctrl-imx23.o
obj-$(CONFIG_PINCTRL_IMX25)	+= pinctrl-imx25.o
obj-$(CONFIG_PINCTRL_IMX28)	+= pinctrl-imx28.o
obj-$(CONFIG_PINCTRL_PALMAS)	+= pinctrl-palmas.o
obj-$(CONFIG_PINCTRL_ROCKCHIP)	+= pinctrl-rockchip.o
obj-$(CONFIG_PINCTRL_SINGLE)	+= pinctrl-single.o
obj-$(CONFIG_PINCTRL_SIRF)	+= sirf/
obj-$(CONFIG_PINCTRL_TEGRA)	+= pinctrl-tegra.o
obj-$(CONFIG_PINCTRL_TEGRA20)	+= pinctrl-tegra20.o
obj-$(CONFIG_PINCTRL_TEGRA30)	+= pinctrl-tegra30.o
obj-$(CONFIG_PINCTRL_TEGRA114)	+= pinctrl-tegra114.o
obj-$(CONFIG_PINCTRL_TEGRA124)	+= pinctrl-tegra124.o
obj-$(CONFIG_PINCTRL_TZ1090)	+= pinctrl-tz1090.o
obj-$(CONFIG_PINCTRL_TZ1090_PDC)	+= pinctrl-tz1090-pdc.o
obj-$(CONFIG_PINCTRL_U300)	+= pinctrl-u300.o
obj-$(CONFIG_PINCTRL_COH901)	+= pinctrl-coh901.o
obj-$(CONFIG_PINCTRL_XWAY)	+= pinctrl-xway.o
obj-$(CONFIG_PINCTRL_LANTIQ)	+= pinctrl-lantiq.o
obj-$(CONFIG_PINCTRL_TB10X)	+= pinctrl-tb10x.o
obj-$(CONFIG_PINCTRL_ST) 	+= pinctrl-st.o
obj-$(CONFIG_PINCTRL_VF610)	+= pinctrl-vf610.o

obj-$(CONFIG_ARCH_BERLIN)	+= berlin/
obj-$(CONFIG_PLAT_ORION)        += mvebu/
obj-y				+= nomadik/
obj-$(CONFIG_ARCH_QCOM)		+= qcom/
obj-$(CONFIG_PLAT_SAMSUNG)	+= samsung/
obj-$(CONFIG_ARCH_SHMOBILE)	+= sh-pfc/
obj-$(CONFIG_SUPERH)		+= sh-pfc/
obj-$(CONFIG_PLAT_SPEAR)	+= spear/
obj-$(CONFIG_ARCH_SUNXI)	+= sunxi/
obj-$(CONFIG_ARCH_VT8500)	+= vt8500/
