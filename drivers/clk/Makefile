# common clock types
obj-$(CONFIG_HAVE_CLK)		+= clk-devres.o
obj-$(CONFIG_CLKDEV_LOOKUP)	+= clkdev.o
obj-$(CONFIG_COMMON_CLK)	+= clk.o clk-fixed-rate.o clk-gate.o \
				   clk-mux.o clk-divider.o clk-fixed-factor.o
# SoCs specific
obj-$(CONFIG_ARCH_BCM2835)	+= clk-bcm2835.o
obj-$(CONFIG_ARCH_NOMADIK)	+= clk-nomadik.o
obj-$(CONFIG_ARCH_HIGHBANK)	+= clk-highbank.o
obj-$(CONFIG_ARCH_MXS)		+= mxs/
obj-$(CONFIG_ARCH_SOCFPGA)	+= socfpga/
obj-$(CONFIG_PLAT_SPEAR)	+= spear/
obj-$(CONFIG_ARCH_U300)		+= clk-u300.o
obj-$(CONFIG_COMMON_CLK_VERSATILE) += versatile/
obj-$(CONFIG_ARCH_PRIMA2)	+= clk-prima2.o
ifeq ($(CONFIG_COMMON_CLK), y)
obj-$(CONFIG_ARCH_MMP)		+= mmp/
endif
obj-$(CONFIG_MACH_LOONGSON1)	+= clk-ls1x.o
obj-$(CONFIG_ARCH_U8500)	+= ux500/
obj-$(CONFIG_ARCH_VT8500)	+= clk-vt8500.o

# Chip specific
obj-$(CONFIG_COMMON_CLK_WM831X) += clk-wm831x.o
obj-$(CONFIG_COMMON_CLK_MAX77686) += clk-max77686.o
obj-$(CONFIG_CLK_TWL6040)	+= clk-twl6040.o
