#
# Makefile for the PowerPC 4xx on-chip ethernet driver
#

obj-$(CONFIG_IBM_EMAC) += ibm_emac.o

ibm_emac-objs := ibm_emac_mal.o ibm_emac_core.o ibm_emac_phy.o 
ibm_emac-$(CONFIG_IBM_EMAC_ZMII) += ibm_emac_zmii.o
ibm_emac-$(CONFIG_IBM_EMAC_RGMII) += ibm_emac_rgmii.o
ibm_emac-$(CONFIG_IBM_EMAC_TAH) += ibm_emac_tah.o
ibm_emac-$(CONFIG_IBM_EMAC_DEBUG) += ibm_emac_debug.o
