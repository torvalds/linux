#
# Makefile for the Linux IPX layer.
#

obj-$(CONFIG_IPX) += ipx.o

ipx-y			:= af_ipx.o ipx_route.o ipx_proc.o pe2.o
ipx-$(CONFIG_SYSCTL)	+= sysctl_net_ipx.o
