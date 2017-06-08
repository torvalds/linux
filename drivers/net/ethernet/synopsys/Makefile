#
# Makefile for the Synopsys network device drivers.
#

obj-$(CONFIG_DWC_XLGMAC) += dwc-xlgmac.o
dwc-xlgmac-objs := dwc-xlgmac-net.o dwc-xlgmac-desc.o \
		   dwc-xlgmac-hw.o dwc-xlgmac-common.o \
		   dwc-xlgmac-ethtool.o

dwc-xlgmac-$(CONFIG_DWC_XLGMAC_PCI) += dwc-xlgmac-pci.o
