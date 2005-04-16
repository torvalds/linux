EXTRA_CFLAGS += -Idrivers/infiniband/include

obj-$(CONFIG_INFINIBAND_IPOIB)			+= ib_ipoib.o

ib_ipoib-y					:= ipoib_main.o \
						   ipoib_ib.o \
						   ipoib_multicast.o \
						   ipoib_verbs.o \
						   ipoib_vlan.o
ib_ipoib-$(CONFIG_INFINIBAND_IPOIB_DEBUG)	+= ipoib_fs.o

