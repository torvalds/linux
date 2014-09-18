ifneq ($(CONFIG_DVB_B2C2_FLEXCOP_PCI),)
b2c2-flexcop-pci-objs += flexcop-dma.o
endif

b2c2-flexcop-pci-objs += flexcop-pci.o
obj-$(CONFIG_DVB_B2C2_FLEXCOP_PCI) += b2c2-flexcop-pci.o

ccflags-y += -Idrivers/media/dvb-core/
ccflags-y += -Idrivers/media/common/b2c2/
