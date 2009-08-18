obj-$(CONFIG_HYPERV)		+= hv_vmbus.o
obj-$(CONFIG_HYPERV_STORAGE)	+= hv_storvsc.o
obj-$(CONFIG_HYPERV_BLOCK)	+= hv_blkvsc.o
obj-$(CONFIG_HYPERV_NET)	+= hv_netvsc.o

hv_vmbus-objs := vmbus_drv.o osd.o \
		 Vmbus.o Hv.o Connection.o Channel.o \
		 ChannelMgmt.o ChannelInterface.o RingBuffer.o
hv_storvsc-objs := storvsc_drv.o StorVsc.o
hv_blkvsc-objs := blkvsc_drv.o BlkVsc.o
hv_netvsc-objs := netvsc_drv.o NetVsc.o RndisFilter.o
