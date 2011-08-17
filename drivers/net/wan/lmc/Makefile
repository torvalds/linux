#
# Makefile for the Lan Media 21140 based WAN cards
# Specifically the 1000,1200,5200,5245
#

obj-$(CONFIG_LANMEDIA) += lmc.o

lmc-objs := lmc_debug.o lmc_media.o lmc_main.o lmc_proto.o

# Like above except every packet gets echoed to KERN_DEBUG
# in hex
#
# DBDEF = \
# -DDEBUG \
# -DLMC_PACKET_LOG

ccflags-y := -I. $(DBGDEF)
