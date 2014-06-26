#
# Makefile for Unisys visorchipset
#

obj-$(CONFIG_UNISYS_VISORCHIPSET)	+= visorchipset.o

visorchipset-y := visorchipset_main.o controlvm_direct.o file.o \
			parser.o

ccflags-y += -Idrivers/staging/unisys/include
ccflags-y += -Idrivers/staging/unisys/uislib
ccflags-y += -Idrivers/staging/unisys/visorchannel
ccflags-y += -Idrivers/staging/unisys/common-spar/include
ccflags-y += -Idrivers/staging/unisys/common-spar/include/channels
ccflags-y += -Idrivers/staging/unisys/visorutil
ccflags-y += -Iinclude/generated
