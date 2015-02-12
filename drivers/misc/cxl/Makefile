cxl-y				+= main.o file.o irq.o fault.o native.o context.o sysfs.o debugfs.o pci.o trace.o
obj-$(CONFIG_CXL)		+= cxl.o
obj-$(CONFIG_CXL_BASE)		+= base.o

# For tracepoints to include our trace.h from tracepoint infrastructure:
CFLAGS_trace.o := -I$(src)
