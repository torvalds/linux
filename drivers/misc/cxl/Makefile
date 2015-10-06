ccflags-y := -Werror -Wno-unused-const-variable

cxl-y				+= main.o file.o irq.o fault.o native.o
cxl-y				+= context.o sysfs.o debugfs.o pci.o trace.o
cxl-y				+= vphb.o api.o
obj-$(CONFIG_CXL)		+= cxl.o
obj-$(CONFIG_CXL_BASE)		+= base.o

# For tracepoints to include our trace.h from tracepoint infrastructure:
CFLAGS_trace.o := -I$(src)
