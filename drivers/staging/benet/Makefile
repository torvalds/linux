#
# Makefile to build the network driver for ServerEngine's BladeEngine.
#
EXTRA_CFLAGS = -I$(src)/hwlib -I$(src)/fw

obj-$(CONFIG_BENET) += benet.o

benet-y :=  be_init.o be_int.o be_netif.o be_ethtool.o \
	hwlib/funcobj.o hwlib/cq.o hwlib/eq.o hwlib/mpu.o hwlib/eth.o
