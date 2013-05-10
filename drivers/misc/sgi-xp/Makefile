#
# Makefile for SGI's XP devices.
#

obj-$(CONFIG_SGI_XP)		+= xp.o
xp-y				:= xp_main.o
xp-$(CONFIG_IA64_SGI_SN2)	+= xp_sn2.o xp_nofault.o
xp-$(CONFIG_IA64_GENERIC)	+= xp_sn2.o xp_nofault.o
xp-$(CONFIG_IA64_SGI_UV)	+= xp_uv.o
xp-$(CONFIG_X86_64)		+= xp_uv.o

obj-$(CONFIG_SGI_XP)		+= xpc.o
xpc-y				:= xpc_main.o xpc_channel.o xpc_partition.o
xpc-$(CONFIG_IA64_SGI_SN2)	+= xpc_sn2.o
xpc-$(CONFIG_IA64_GENERIC)	+= xpc_sn2.o
xpc-$(CONFIG_IA64_SGI_UV) 	+= xpc_uv.o
xpc-$(CONFIG_X86_64)		+= xpc_uv.o

obj-$(CONFIG_SGI_XP)		+= xpnet.o
