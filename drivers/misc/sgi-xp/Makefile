#
# Makefile for SGI's XP devices.
#

obj-$(CONFIG_SGI_XP)		+= xp.o
xp-y				:= xp_main.o xp_uv.o
xp-$(CONFIG_IA64)		+= xp_sn2.o xp_nofault.o

obj-$(CONFIG_SGI_XP)		+= xpc.o
xpc-y				:= xpc_main.o xpc_channel.o xpc_partition.o

obj-$(CONFIG_SGI_XP)		+= xpnet.o
