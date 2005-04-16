#
# Makefile for the IBM PPC4xx EMAC controllers
#

obj-$(CONFIG_IBM_EMAC) += ibm_emac.o

ibm_emac-objs := ibm_emac_mal.o ibm_emac_core.o ibm_emac_phy.o

# Only need this if you want to see additional debug messages
ifeq ($(CONFIG_IBM_EMAC_ERRMSG), y)
ibm_emac-objs += ibm_emac_debug.o
endif
