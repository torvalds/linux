#
# Makefile for Beceem USB Wimax card
#

obj-$(CONFIG_BCM_WIMAX) +=	bcm_wimax.o

bcm_wimax-y :=  InterfaceDld.o InterfaceIdleMode.o InterfaceInit.o InterfaceRx.o \
		InterfaceIsr.o InterfaceMisc.o InterfaceTx.o \
		CmHost.o IPv6Protocol.o Qos.o Transmit.o\
		Bcmnet.o DDRInit.o HandleControlPacket.o\
		LeakyBucket.o Misc.o sort.o Bcmchar.o hostmibs.o PHSModule.o\
		led_control.o nvm.o vendorspecificextn.o
