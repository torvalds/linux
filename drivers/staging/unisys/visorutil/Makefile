#
# Makefile for Unisys timskmod
#

obj-$(CONFIG_UNISYS_VISORUTIL)	+= visorutil.o

visorutil-y := charqueue.o  easyproc.o  periodic_work.o  procobjecttree.o  \
		memregion_direct.o visorkmodutils.o

ccflags-y += -Idrivers/staging/unisys/include
ccflags-y += -DCONFIG_SPAR_GUEST -DGUESTDRIVERBUILD -DNOAUTOVERSION
