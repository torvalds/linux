ifneq ($(CONFIG_BACKPORT_DVB_B2C2_FLEXCOP_PCI),)
b2c2-flexcop-pci-objs += flexcop-dma.o
endif

b2c2-flexcop-pci-objs += flexcop-pci.o
obj-$(CONFIG_BACKPORT_DVB_B2C2_FLEXCOP_PCI) += b2c2-flexcop-pci.o

ccflags-y += -I$(backport_srctree)/drivers/media/dvb-core/
ccflags-y += -I$(backport_srctree)/drivers/media/common/b2c2/
