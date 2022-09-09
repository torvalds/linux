# SPDX-License-Identifier: GPL-2.0

mxc-clk-objs += clk.o
mxc-clk-objs += clk-busy.o
mxc-clk-objs += clk-composite-7ulp.o
mxc-clk-objs += clk-composite-8m.o
mxc-clk-objs += clk-composite-93.o
mxc-clk-objs += clk-fracn-gppll.o
mxc-clk-objs += clk-cpu.o
mxc-clk-objs += clk-divider-gate.o
mxc-clk-objs += clk-fixup-div.o
mxc-clk-objs += clk-fixup-mux.o
mxc-clk-objs += clk-frac-pll.o
mxc-clk-objs += clk-gate2.o
mxc-clk-objs += clk-gate-exclusive.o
mxc-clk-objs += clk-pfd.o
mxc-clk-objs += clk-pfdv2.o
mxc-clk-objs += clk-pllv1.o
mxc-clk-objs += clk-pllv2.o
mxc-clk-objs += clk-pllv3.o
mxc-clk-objs += clk-pllv4.o
mxc-clk-objs += clk-pll14xx.o
mxc-clk-objs += clk-sscg-pll.o
obj-$(CONFIG_MXC_CLK) += mxc-clk.o

obj-$(CONFIG_CLK_IMX8MM) += clk-imx8mm.o
obj-$(CONFIG_CLK_IMX8MN) += clk-imx8mn.o
obj-$(CONFIG_CLK_IMX8MP) += clk-imx8mp.o
obj-$(CONFIG_CLK_IMX8MQ) += clk-imx8mq.o

obj-$(CONFIG_CLK_IMX93) += clk-imx93.o

obj-$(CONFIG_MXC_CLK_SCU) += clk-imx-scu.o clk-imx-lpcg-scu.o
clk-imx-scu-$(CONFIG_CLK_IMX8QXP) += clk-scu.o clk-imx8qxp.o \
				     clk-imx8qxp-rsrc.o clk-imx8qm-rsrc.o \
				     clk-imx8dxl-rsrc.o
clk-imx-lpcg-scu-$(CONFIG_CLK_IMX8QXP) += clk-lpcg-scu.o clk-imx8qxp-lpcg.o

obj-$(CONFIG_CLK_IMX8ULP) += clk-imx8ulp.o

obj-$(CONFIG_CLK_IMX1)   += clk-imx1.o
obj-$(CONFIG_CLK_IMX25)  += clk-imx25.o
obj-$(CONFIG_CLK_IMX27)  += clk-imx27.o
obj-$(CONFIG_CLK_IMX31)  += clk-imx31.o
obj-$(CONFIG_CLK_IMX35)  += clk-imx35.o
obj-$(CONFIG_CLK_IMX5)   += clk-imx5.o
obj-$(CONFIG_CLK_IMX6Q)  += clk-imx6q.o
obj-$(CONFIG_CLK_IMX6SL) += clk-imx6sl.o
obj-$(CONFIG_CLK_IMX6SLL) += clk-imx6sll.o
obj-$(CONFIG_CLK_IMX6SX) += clk-imx6sx.o
obj-$(CONFIG_CLK_IMX6UL) += clk-imx6ul.o
obj-$(CONFIG_CLK_IMX7D)  += clk-imx7d.o
obj-$(CONFIG_CLK_IMX7ULP) += clk-imx7ulp.o
obj-$(CONFIG_CLK_IMXRT1050)  += clk-imxrt1050.o
obj-$(CONFIG_CLK_VF610)  += clk-vf610.o
