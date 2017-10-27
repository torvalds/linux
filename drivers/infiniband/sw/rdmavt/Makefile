#
# rdmavt driver
#
#
#
# Called from the kernel module build system.
#
obj-$(CONFIG_INFINIBAND_RDMAVT) += rdmavt.o

rdmavt-y := vt.o ah.o cq.o mad.o mcast.o mmap.o mr.o pd.o qp.o \
	rc.o srq.o trace.o

CFLAGS_trace.o = -I$(src)
