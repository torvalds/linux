obj-$(CONFIG_HYPERV)		+= hv_vmbus.o hv_timesource.o
obj-$(CONFIG_HYPERV_STORAGE)	+= hv_storvsc.o
obj-$(CONFIG_HYPERV_BLOCK)	+= hv_blkvsc.o
obj-$(CONFIG_HYPERV_NET)	+= hv_netvsc.o
obj-$(CONFIG_HYPERV_UTILS)	+= hv_utils.o

hv_vmbus-objs := vmbus_drv.o osd.o \
		 vmbus.o hv.o connection.o channel.o \
		 channel_mgmt.o channel_interface.o ring_buffer.o
hv_storvsc-objs := storvsc_drv.o storvsc.o
hv_blkvsc-objs := blkvsc_drv.o blkvsc.o
hv_netvsc-objs := netvsc_drv.o netvsc.o rndis_filter.o
