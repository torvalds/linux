#
# Makefile for the Freescale DPAA Ethernet controllers
#

# Include FMan headers
FMAN        = $(srctree)/drivers/net/ethernet/freescale/fman
ccflags-y += -I$(FMAN)

obj-$(CONFIG_FSL_DPAA_ETH) += fsl_dpa.o

fsl_dpa-objs += dpaa_eth.o dpaa_ethtool.o dpaa_eth_sysfs.o
CFLAGS_dpaa_eth.o := -I$(src)
