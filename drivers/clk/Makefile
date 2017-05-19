# common clock types
obj-$(CONFIG_HAVE_CLK)		+= clk-devres.o clk-bulk.o
obj-$(CONFIG_CLKDEV_LOOKUP)	+= clkdev.o
obj-$(CONFIG_COMMON_CLK)	+= clk.o
obj-$(CONFIG_COMMON_CLK)	+= clk-divider.o
obj-$(CONFIG_COMMON_CLK)	+= clk-fixed-factor.o
obj-$(CONFIG_COMMON_CLK)	+= clk-fixed-rate.o
obj-$(CONFIG_COMMON_CLK)	+= clk-gate.o
obj-$(CONFIG_COMMON_CLK)	+= clk-multiplier.o
obj-$(CONFIG_COMMON_CLK)	+= clk-mux.o
obj-$(CONFIG_COMMON_CLK)	+= clk-composite.o
obj-$(CONFIG_COMMON_CLK)	+= clk-fractional-divider.o
obj-$(CONFIG_COMMON_CLK)	+= clk-gpio.o
ifeq ($(CONFIG_OF), y)
obj-$(CONFIG_COMMON_CLK)	+= clk-conf.o
endif

# hardware specific clock types
# please keep this section sorted lexicographically by file/directory path name
obj-$(CONFIG_MACH_ASM9260)		+= clk-asm9260.o
obj-$(CONFIG_COMMON_CLK_AXI_CLKGEN)	+= clk-axi-clkgen.o
obj-$(CONFIG_ARCH_AXXIA)		+= clk-axm5516.o
obj-$(CONFIG_COMMON_CLK_CDCE706)	+= clk-cdce706.o
obj-$(CONFIG_ARCH_CLPS711X)		+= clk-clps711x.o
obj-$(CONFIG_ARCH_EFM32)		+= clk-efm32gg.o
obj-$(CONFIG_ARCH_HIGHBANK)		+= clk-highbank.o
obj-$(CONFIG_MACH_LOONGSON32)		+= clk-ls1x.o
obj-$(CONFIG_COMMON_CLK_MAX_GEN)	+= clk-max-gen.o
obj-$(CONFIG_COMMON_CLK_MAX77686)	+= clk-max77686.o
obj-$(CONFIG_COMMON_CLK_MAX77802)	+= clk-max77802.o
obj-$(CONFIG_ARCH_MB86S7X)		+= clk-mb86s7x.o
obj-$(CONFIG_ARCH_MOXART)		+= clk-moxart.o
obj-$(CONFIG_ARCH_NOMADIK)		+= clk-nomadik.o
obj-$(CONFIG_ARCH_NSPIRE)		+= clk-nspire.o
obj-$(CONFIG_COMMON_CLK_PALMAS)		+= clk-palmas.o
obj-$(CONFIG_CLK_QORIQ)			+= clk-qoriq.o
obj-$(CONFIG_COMMON_CLK_RK808)		+= clk-rk808.o
obj-$(CONFIG_COMMON_CLK_S2MPS11)	+= clk-s2mps11.o
obj-$(CONFIG_COMMON_CLK_SCPI)           += clk-scpi.o
obj-$(CONFIG_COMMON_CLK_SI5351)		+= clk-si5351.o
obj-$(CONFIG_COMMON_CLK_SI514)		+= clk-si514.o
obj-$(CONFIG_COMMON_CLK_SI570)		+= clk-si570.o
obj-$(CONFIG_COMMON_CLK_CDCE925)	+= clk-cdce925.o
obj-$(CONFIG_ARCH_STM32)		+= clk-stm32f4.o
obj-$(CONFIG_CLK_TWL6040)		+= clk-twl6040.o
obj-$(CONFIG_ARCH_U300)			+= clk-u300.o
obj-$(CONFIG_ARCH_VT8500)		+= clk-vt8500.o
obj-$(CONFIG_COMMON_CLK_WM831X)		+= clk-wm831x.o
obj-$(CONFIG_COMMON_CLK_XGENE)		+= clk-xgene.o
obj-$(CONFIG_COMMON_CLK_PWM)		+= clk-pwm.o
obj-$(CONFIG_COMMON_CLK_AT91)		+= at91/
obj-y					+= bcm/
obj-$(CONFIG_ARCH_BERLIN)		+= berlin/
obj-$(CONFIG_ARCH_HISI)			+= hisilicon/
obj-$(CONFIG_ARCH_MXC)			+= imx/
obj-$(CONFIG_MACH_INGENIC)		+= ingenic/
obj-$(CONFIG_COMMON_CLK_KEYSTONE)	+= keystone/
obj-$(CONFIG_ARCH_MEDIATEK)		+= mediatek/
ifeq ($(CONFIG_COMMON_CLK), y)
obj-$(CONFIG_ARCH_MMP)			+= mmp/
endif
obj-$(CONFIG_PLAT_ORION)		+= mvebu/
obj-$(CONFIG_ARCH_MESON)		+= meson/
obj-$(CONFIG_ARCH_MXS)			+= mxs/
obj-$(CONFIG_ARCH_LPC18XX)		+= nxp/
obj-$(CONFIG_MACH_PISTACHIO)		+= pistachio/
obj-$(CONFIG_COMMON_CLK_PXA)		+= pxa/
obj-$(CONFIG_COMMON_CLK_QCOM)		+= qcom/
obj-$(CONFIG_ARCH_ROCKCHIP)		+= rockchip/
obj-$(CONFIG_COMMON_CLK_SAMSUNG)	+= samsung/
obj-$(CONFIG_ARCH_SHMOBILE_MULTI)	+= shmobile/
obj-$(CONFIG_ARCH_SIRF)			+= sirf/
obj-$(CONFIG_ARCH_SOCFPGA)		+= socfpga/
obj-$(CONFIG_PLAT_SPEAR)		+= spear/
obj-$(CONFIG_ARCH_STI)			+= st/
obj-$(CONFIG_ARCH_SUNXI)		+= sunxi/
obj-$(CONFIG_ARCH_TEGRA)		+= tegra/
obj-$(CONFIG_ARCH_OMAP2PLUS)		+= ti/
obj-$(CONFIG_ARCH_U8500)		+= ux500/
obj-$(CONFIG_COMMON_CLK_VERSATILE)	+= versatile/
ifeq ($(CONFIG_COMMON_CLK), y)
obj-$(CONFIG_X86)			+= x86/
endif
obj-$(CONFIG_ARCH_ZX)			+= zte/
obj-$(CONFIG_ARCH_ZYNQ)			+= zynq/
obj-$(CONFIG_H8300)		+= h8300/
