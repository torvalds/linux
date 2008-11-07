#
# Makefile for the Ethernet Bonding driver
#

obj-$(CONFIG_BONDING) += bonding.o

bonding-objs := bond_main.o bond_3ad.o bond_alb.o bond_sysfs.o

ipv6-$(subst m,y,$(CONFIG_IPV6)) += bond_ipv6.o
bonding-objs += $(ipv6-y)

