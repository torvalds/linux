obj-$(CONFIG_HYPERV)		+= hv_vmbus.o hv_timesource.o
obj-$(CONFIG_HYPERV_STORAGE)	+= hv_storvsc.o
obj-$(CONFIG_HYPERV_BLOCK)	+= hv_blkvsc.o
obj-$(CONFIG_HYPERV_NET)	+= hv_netvsc.o
obj-$(CONFIG_HYPERV_UTILS)	+= hv_utils.o
obj-$(CONFIG_HYPERV_MOUSE)	+= hv_mouse.o

hv_vmbus-y := vmbus_drv.o \
		 hv.o connection.o channel.o \
		 channel_mgmt.o ring_buffer.o
hv_storvsc-y := storvsc_drv.o storvsc.o
hv_blkvsc-y := blkvsc_drv.o  storvsc.o
hv_netvsc-y := netvsc_drv.o netvsc.o rndis_filter.o
hv_utils-y := hv_util.o hv_kvp.o
