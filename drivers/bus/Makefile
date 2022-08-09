# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the bus drivers.
#

# Interconnect bus drivers for ARM platforms
obj-$(CONFIG_ARM_CCI)		+= arm-cci.o
obj-$(CONFIG_ARM_INTEGRATOR_LM)	+= arm-integrator-lm.o
obj-$(CONFIG_HISILICON_LPC)	+= hisi_lpc.o
obj-$(CONFIG_BRCMSTB_GISB_ARB)	+= brcmstb_gisb.o
obj-$(CONFIG_MOXTET)		+= moxtet.o

# DPAA2 fsl-mc bus
obj-$(CONFIG_FSL_MC_BUS)	+= fsl-mc/

obj-$(CONFIG_BT1_APB)		+= bt1-apb.o
obj-$(CONFIG_BT1_AXI)		+= bt1-axi.o
obj-$(CONFIG_IMX_WEIM)		+= imx-weim.o
obj-$(CONFIG_INTEL_IXP4XX_EB)	+= intel-ixp4xx-eb.o
obj-$(CONFIG_MIPS_CDMM)		+= mips_cdmm.o
obj-$(CONFIG_MVEBU_MBUS) 	+= mvebu-mbus.o

# Interconnect bus driver for OMAP SoCs.
obj-$(CONFIG_OMAP_INTERCONNECT)	+= omap_l3_smx.o omap_l3_noc.o

obj-$(CONFIG_OMAP_OCP2SCP)	+= omap-ocp2scp.o
obj-$(CONFIG_QCOM_EBI2)		+= qcom-ebi2.o
obj-$(CONFIG_SUN50I_DE2_BUS)	+= sun50i-de2.o
obj-$(CONFIG_SUNXI_RSB)		+= sunxi-rsb.o
obj-$(CONFIG_OF)		+= simple-pm-bus.o
obj-$(CONFIG_TEGRA_ACONNECT)	+= tegra-aconnect.o
obj-$(CONFIG_TEGRA_GMI)		+= tegra-gmi.o
obj-$(CONFIG_TI_PWMSS)		+= ti-pwmss.o
obj-$(CONFIG_TI_SYSC)		+= ti-sysc.o
obj-$(CONFIG_TS_NBUS)		+= ts-nbus.o
obj-$(CONFIG_UNIPHIER_SYSTEM_BUS)	+= uniphier-system-bus.o
obj-$(CONFIG_VEXPRESS_CONFIG)	+= vexpress-config.o

obj-$(CONFIG_DA8XX_MSTPRI)	+= da8xx-mstpri.o

# MHI
obj-y				+= mhi/
