#
# Makefile to build the network driver for ServerEngine's BladeEngine
#
obj-$(CONFIG_BENET) += benet.o

benet-y :=  be_init.o \
			be_int.o \
			be_netif.o \
			be_ethtool.o \
			funcobj.o \
			cq.o \
			eq.o \
			mpu.o \
			eth.o
