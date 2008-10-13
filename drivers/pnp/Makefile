#
# Makefile for the Linux Plug-and-Play Support.
#

obj-y		:= core.o card.o driver.o resource.o manager.o support.o interface.o quirks.o

obj-$(CONFIG_PNPACPI)		+= pnpacpi/
obj-$(CONFIG_PNPBIOS)		+= pnpbios/
obj-$(CONFIG_ISAPNP)		+= isapnp/

# pnp_system_init goes after pnpacpi/pnpbios init
obj-y				+= system.o

ifeq ($(CONFIG_PNP_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif
