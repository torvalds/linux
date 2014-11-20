#
# Makefile for Unisys visorchannel
#

obj-$(CONFIG_UNISYS_VISORCHANNEL)	+= visorchannel.o

visorchannel-y := visorchannel_main.o visorchannel_funcs.o

ccflags-y += -Idrivers/staging/unisys/include
ccflags-y += -Idrivers/staging/unisys/common-spar/include
ccflags-y += -Idrivers/staging/unisys/common-spar/include/channels
ccflags-y += -Idrivers/staging/unisys/visorutil
