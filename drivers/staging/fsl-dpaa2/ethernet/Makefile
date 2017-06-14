#
# Makefile for the Freescale DPAA2 Ethernet controller
#

obj-$(CONFIG_FSL_DPAA2_ETH) += fsl-dpaa2-eth.o

fsl-dpaa2-eth-objs    := dpaa2-eth.o dpaa2-ethtool.o dpni.o

# Needed by the tracing framework
CFLAGS_dpaa2-eth.o := -I$(src)
