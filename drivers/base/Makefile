# Makefile for the Linux device tree

obj-y			:= core.o sys.o bus.o dd.o \
			   driver.o class.o platform.o \
			   cpu.o firmware.o init.o map.o devres.o \
			   attribute_container.o transport_class.o
obj-y			+= power/
obj-$(CONFIG_HAS_DMA)	+= dma-mapping.o dmapool.o
obj-$(CONFIG_ISA)	+= isa.o
obj-$(CONFIG_FW_LOADER)	+= firmware_class.o
obj-$(CONFIG_NUMA)	+= node.o
obj-$(CONFIG_MEMORY_HOTPLUG_SPARSE) += memory.o
obj-$(CONFIG_SMP)	+= topology.o
ifeq ($(CONFIG_SYSFS),y)
obj-$(CONFIG_MODULES)	+= module.o
endif
obj-$(CONFIG_SYS_HYPERVISOR) += hypervisor.o

ifeq ($(CONFIG_DEBUG_DRIVER),y)
EXTRA_CFLAGS += -DDEBUG
endif

