#
# Makefile for Unisys channelstub
#

obj-$(CONFIG_UNISYS_CHANNELSTUB)	+= visorchannelstub.o

visorchannelstub-y := channel.o chanstub.o

ccflags-y += -Idrivers/staging/unisys/include
ccflags-y += -Idrivers/staging/unisys/common-spar/include
ccflags-y += -Idrivers/staging/unisys/common-spar/include/channels
ccflags-y += -DCONFIG_SPAR_GUEST -DGUESTDRIVERBUILD -DNOAUTOVERSION

