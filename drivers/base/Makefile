# Makefile for the Linux device tree

obj-y			:= core.o sys.o interface.o bus.o \
			   driver.o class.o class_simple.o platform.o \
			   cpu.o firmware.o init.o map.o dmapool.o \
			   attribute_container.o transport_class.o
obj-y			+= power/
obj-$(CONFIG_FW_LOADER)	+= firmware_class.o
obj-$(CONFIG_NUMA)	+= node.o

ifeq ($(CONFIG_DEBUG_DRIVER),y)
EXTRA_CFLAGS += -DDEBUG
endif

