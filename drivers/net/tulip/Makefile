#
# Makefile for the Linux "Tulip" family network device drivers.
#

obj-$(CONFIG_PCMCIA_XIRTULIP)	+= xircom_tulip_cb.o
obj-$(CONFIG_PCMCIA_XIRCOM)	+= xircom_cb.o
obj-$(CONFIG_DM9102)		+= dmfe.o
obj-$(CONFIG_WINBOND_840)	+= winbond-840.o
obj-$(CONFIG_DE2104X)		+= de2104x.o
obj-$(CONFIG_TULIP)		+= tulip.o
obj-$(CONFIG_DE4X5)		+= de4x5.o
obj-$(CONFIG_ULI526X)		+= uli526x.o

# Declare multi-part drivers.

tulip-objs			:= eeprom.o interrupt.o media.o \
				   timer.o tulip_core.o		\
				   21142.o pnic.o pnic2.o
