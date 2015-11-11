#
# Makefile for the bus drivers.
#

# Interconnect bus drivers for ARM platforms
obj-$(CONFIG_ARM_CCI)		+= arm-cci.o
obj-$(CONFIG_ARM_CCN)		+= arm-ccn.o

obj-$(CONFIG_BRCMSTB_GISB_ARB)	+= brcmstb_gisb.o
obj-$(CONFIG_IMX_WEIM)		+= imx-weim.o
obj-$(CONFIG_MIPS_CDMM)		+= mips_cdmm.o
obj-$(CONFIG_MVEBU_MBUS) 	+= mvebu-mbus.o

# Interconnect bus driver for OMAP SoCs.
obj-$(CONFIG_OMAP_INTERCONNECT)	+= omap_l3_smx.o omap_l3_noc.o

obj-$(CONFIG_OMAP_OCP2SCP)	+= omap-ocp2scp.o
obj-$(CONFIG_SUNXI_RSB)		+= sunxi-rsb.o
obj-$(CONFIG_SIMPLE_PM_BUS)	+= simple-pm-bus.o
obj-$(CONFIG_VEXPRESS_CONFIG)	+= vexpress-config.o
