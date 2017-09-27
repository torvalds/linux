#
# Makefile for the PowerPC 4xx on-chip ethernet driver
#

obj-$(CONFIG_IBM_EMAC) += ibm_emac.o

ibm_emac-y := mal.o core.o phy.o
ibm_emac-$(CONFIG_IBM_EMAC_ZMII) += zmii.o
ibm_emac-$(CONFIG_IBM_EMAC_RGMII) += rgmii.o
ibm_emac-$(CONFIG_IBM_EMAC_TAH) += tah.o
