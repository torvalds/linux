# common clock types
obj-$(CONFIG_HAVE_CLK)		+= clk-devres.o
obj-$(CONFIG_CLKDEV_LOOKUP)	+= clkdev.o
obj-$(CONFIG_COMMON_CLK)	+= clk.o
obj-$(CONFIG_COMMON_CLK)	+= clk-divider.o
obj-$(CONFIG_COMMON_CLK)	+= clk-fixed-factor.o
obj-$(CONFIG_COMMON_CLK)	+= clk-fixed-rate.o
obj-$(CONFIG_COMMON_CLK)	+= clk-gate.o
obj-$(CONFIG_COMMON_CLK)	+= clk-mux.o
obj-$(CONFIG_COMMON_CLK)	+= clk-composite.o
obj-$(CONFIG_COMMON_CLK)	+= clk-fractional-divider.o
ifeq ($(CONFIG_OF), y)
obj-$(CONFIG_COMMON_CLK)	+= clk-conf.o
endif

# hardware specific clock types
# please keep this section sorted lexicographically by file/directory path name
obj-$(CONFIG_COMMON_CLK_AXI_CLKGEN)	+= clk-axi-clkgen.o
obj-$(CONFIG_ARCH_AXXIA)		+= clk-axm5516.o
obj-$(CONFIG_ARCH_BCM2835)		+= clk-bcm2835.o
obj-$(CONFIG_ARCH_CLPS711X)		+= clk-clps711x.o
obj-$(CONFIG_ARCH_EFM32)		+= clk-efm32gg.o
obj-$(CONFIG_ARCH_HIGHBANK)		+= clk-highbank.o
obj-$(CONFIG_MACH_LOONGSON1)		+= clk-ls1x.o
obj-$(CONFIG_COMMON_CLK_MAX77686)	+= clk-max77686.o
obj-$(CONFIG_ARCH_MOXART)		+= clk-moxart.o
obj-$(CONFIG_ARCH_NOMADIK)		+= clk-nomadik.o
obj-$(CONFIG_ARCH_NSPIRE)		+= clk-nspire.o
obj-$(CONFIG_COMMON_CLK_PALMAS)		+= clk-palmas.o
obj-$(CONFIG_CLK_PPC_CORENET)		+= clk-ppc-corenet.o
obj-$(CONFIG_COMMON_CLK_S2MPS11)	+= clk-s2mps11.o
obj-$(CONFIG_COMMON_CLK_SI5351)		+= clk-si5351.o
obj-$(CONFIG_COMMON_CLK_SI570)		+= clk-si570.o
obj-$(CONFIG_CLK_TWL6040)		+= clk-twl6040.o
obj-$(CONFIG_ARCH_U300)			+= clk-u300.o
obj-$(CONFIG_ARCH_VT8500)		+= clk-vt8500.o
obj-$(CONFIG_COMMON_CLK_WM831X)		+= clk-wm831x.o
obj-$(CONFIG_COMMON_CLK_XGENE)		+= clk-xgene.o
obj-$(CONFIG_COMMON_CLK_AT91)		+= at91/
obj-$(CONFIG_ARCH_BCM_MOBILE)		+= bcm/
obj-$(CONFIG_ARCH_BERLIN)		+= berlin/
obj-$(CONFIG_ARCH_HI3xxx)		+= hisilicon/
obj-$(CONFIG_ARCH_HIP04)		+= hisilicon/
obj-$(CONFIG_ARCH_HIX5HD2)		+= hisilicon/
obj-$(CONFIG_COMMON_CLK_KEYSTONE)	+= keystone/
ifeq ($(CONFIG_COMMON_CLK), y)
obj-$(CONFIG_ARCH_MMP)			+= mmp/
endif
obj-$(CONFIG_PLAT_ORION)		+= mvebu/
obj-$(CONFIG_ARCH_MXS)			+= mxs/
obj-$(CONFIG_ARCH_PXA)			+= pxa/
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
obj-$(CONFIG_X86)			+= x86/
obj-$(CONFIG_ARCH_ZYNQ)			+= zynq/
