#
# Makefile for the Ethernet Ipvlan driver
#

obj-$(CONFIG_IPVLAN) += ipvlan.o
obj-$(CONFIG_IPVTAP) += ipvtap.o

ipvlan-objs-$(CONFIG_IPVLAN_L3S) += ipvlan_l3s.o
ipvlan-objs := ipvlan_core.o ipvlan_main.o $(ipvlan-objs-y)
