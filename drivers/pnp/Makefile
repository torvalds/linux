#
# Makefile for the Linux Plug-and-Play Support.
#

obj-y		:= core.o card.o driver.o resource.o manager.o support.o interface.o quirks.o system.o

obj-$(CONFIG_PNPACPI)		+= pnpacpi/
obj-$(CONFIG_PNPBIOS)		+= pnpbios/
obj-$(CONFIG_ISAPNP)		+= isapnp/

ifeq ($(CONFIG_PNP_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif
