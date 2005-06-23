#
# Chelsio 10Gb NIC driver for Linux.
#

obj-$(CONFIG_CHELSIO_T1) += cxgb.o

EXTRA_CFLAGS += -I$(TOPDIR)/drivers/net/chelsio $(DEBUG_FLAGS)


cxgb-objs := cxgb2.o espi.o pm3393.o sge.o subr.o mv88x201x.o

