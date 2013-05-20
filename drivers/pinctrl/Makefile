# generic pinmux support

ccflags-$(CONFIG_DEBUG_PINCTRL)	+= -DDEBUG

obj-$(CONFIG_PINCTRL)		+= core.o
obj-$(CONFIG_PINMUX)		+= pinmux.o
obj-$(CONFIG_PINCONF)		+= pinconf.o
ifeq ($(CONFIG_OF),y)
obj-$(CONFIG_PINCTRL)		+= devicetree.o
endif
obj-$(CONFIG_GENERIC_PINCONF)	+= pinconf-generic.o
obj-$(CONFIG_PINCTRL_ABX500)	+= pinctrl-abx500.o
obj-$(CONFIG_PINCTRL_AB8500)	+= pinctrl-ab8500.o
obj-$(CONFIG_PINCTRL_AB8540)	+= pinctrl-ab8540.o
obj-$(CONFIG_PINCTRL_AB9540)	+= pinctrl-ab9540.o
obj-$(CONFIG_PINCTRL_AB8505)	+= pinctrl-ab8505.o
obj-$(CONFIG_PINCTRL_AT91)	+= pinctrl-at91.o
obj-$(CONFIG_PINCTRL_BCM2835)	+= pinctrl-bcm2835.o
obj-$(CONFIG_PINCTRL_IMX)	+= pinctrl-imx.o
obj-$(CONFIG_PINCTRL_IMX35)	+= pinctrl-imx35.o
obj-$(CONFIG_PINCTRL_IMX51)	+= pinctrl-imx51.o
obj-$(CONFIG_PINCTRL_IMX53)	+= pinctrl-imx53.o
obj-$(CONFIG_PINCTRL_IMX6Q)	+= pinctrl-imx6q.o
obj-$(CONFIG_PINCTRL_IMX6Q)	+= pinctrl-imx6dl.o
obj-$(CONFIG_PINCTRL_FALCON)	+= pinctrl-falcon.o
obj-$(CONFIG_PINCTRL_MXS)	+= pinctrl-mxs.o
obj-$(CONFIG_PINCTRL_IMX23)	+= pinctrl-imx23.o
obj-$(CONFIG_PINCTRL_IMX28)	+= pinctrl-imx28.o
obj-$(CONFIG_PINCTRL_NOMADIK)	+= pinctrl-nomadik.o
obj-$(CONFIG_PINCTRL_STN8815)	+= pinctrl-nomadik-stn8815.o
obj-$(CONFIG_PINCTRL_DB8500)	+= pinctrl-nomadik-db8500.o
obj-$(CONFIG_PINCTRL_DB8540)	+= pinctrl-nomadik-db8540.o
obj-$(CONFIG_PINCTRL_SINGLE)	+= pinctrl-single.o
obj-$(CONFIG_PINCTRL_SIRF)	+= pinctrl-sirf.o
obj-$(CONFIG_PINCTRL_SUNXI)	+= pinctrl-sunxi.o
obj-$(CONFIG_PINCTRL_TEGRA)	+= pinctrl-tegra.o
obj-$(CONFIG_PINCTRL_TEGRA20)	+= pinctrl-tegra20.o
obj-$(CONFIG_PINCTRL_TEGRA30)	+= pinctrl-tegra30.o
obj-$(CONFIG_PINCTRL_TEGRA114)	+= pinctrl-tegra114.o
obj-$(CONFIG_PINCTRL_U300)	+= pinctrl-u300.o
obj-$(CONFIG_PINCTRL_COH901)	+= pinctrl-coh901.o
obj-$(CONFIG_PINCTRL_SAMSUNG)	+= pinctrl-samsung.o
obj-$(CONFIG_PINCTRL_EXYNOS)	+= pinctrl-exynos.o
obj-$(CONFIG_PINCTRL_EXYNOS5440)	+= pinctrl-exynos5440.o
obj-$(CONFIG_PINCTRL_S3C24XX)	+= pinctrl-s3c24xx.o
obj-$(CONFIG_PINCTRL_S3C64XX)	+= pinctrl-s3c64xx.o
obj-$(CONFIG_PINCTRL_XWAY)	+= pinctrl-xway.o
obj-$(CONFIG_PINCTRL_LANTIQ)	+= pinctrl-lantiq.o

obj-$(CONFIG_PLAT_ORION)        += mvebu/
obj-$(CONFIG_ARCH_SHMOBILE)	+= sh-pfc/
obj-$(CONFIG_SUPERH)		+= sh-pfc/
obj-$(CONFIG_PLAT_SPEAR)	+= spear/
obj-$(CONFIG_ARCH_VT8500)	+= vt8500/
