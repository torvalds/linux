#
# Makefile for the Linux IEEE 1394 implementation
#

ieee1394-objs := ieee1394_core.o ieee1394_transactions.o hosts.o \
		 highlevel.o csr.o nodemgr.o dma.o iso.o \
		 csr1212.o config_roms.o

obj-$(CONFIG_IEEE1394) += ieee1394.o
obj-$(CONFIG_IEEE1394_PCILYNX) += pcilynx.o
obj-$(CONFIG_IEEE1394_OHCI1394) += ohci1394.o
obj-$(CONFIG_IEEE1394_VIDEO1394) += video1394.o
obj-$(CONFIG_IEEE1394_RAWIO) += raw1394.o
obj-$(CONFIG_IEEE1394_SBP2) += sbp2.o
obj-$(CONFIG_IEEE1394_DV1394) += dv1394.o
obj-$(CONFIG_IEEE1394_ETH1394) += eth1394.o

obj-$(CONFIG_PROVIDE_OHCI1394_DMA_INIT) += init_ohci1394_dma.o
