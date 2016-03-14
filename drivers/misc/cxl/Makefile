ccflags-y			:= $(call cc-disable-warning, unused-const-variable)
ccflags-$(CONFIG_PPC_WERROR)	+= -Werror

cxl-y				+= main.o file.o irq.o fault.o native.o
cxl-y				+= context.o sysfs.o debugfs.o pci.o trace.o
cxl-y				+= vphb.o api.o
obj-$(CONFIG_CXL)		+= cxl.o
obj-$(CONFIG_CXL_BASE)		+= base.o

# For tracepoints to include our trace.h from tracepoint infrastructure:
CFLAGS_trace.o := -I$(src)
