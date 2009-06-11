#
# Makefile for the mouse drivers.
#

# Each configuration option enables a list of files.

obj-$(CONFIG_MOUSE_AMIGA)		+= amimouse.o
obj-$(CONFIG_MOUSE_APPLETOUCH)		+= appletouch.o
obj-$(CONFIG_MOUSE_ATARI)		+= atarimouse.o
obj-$(CONFIG_MOUSE_BCM5974)		+= bcm5974.o
obj-$(CONFIG_MOUSE_GPIO)		+= gpio_mouse.o
obj-$(CONFIG_MOUSE_HIL)			+= hil_ptr.o
obj-$(CONFIG_MOUSE_INPORT)		+= inport.o
obj-$(CONFIG_MOUSE_LOGIBM)		+= logibm.o
obj-$(CONFIG_MOUSE_MAPLE)		+= maplemouse.o
obj-$(CONFIG_MOUSE_PC110PAD)		+= pc110pad.o
obj-$(CONFIG_MOUSE_PS2)			+= psmouse.o
obj-$(CONFIG_MOUSE_PXA930_TRKBALL)	+= pxa930_trkball.o
obj-$(CONFIG_MOUSE_RISCPC)		+= rpcmouse.o
obj-$(CONFIG_MOUSE_SERIAL)		+= sermouse.o
obj-$(CONFIG_MOUSE_SYNAPTICS_I2C)	+= synaptics_i2c.o
obj-$(CONFIG_MOUSE_VSXXXAA)		+= vsxxxaa.o

psmouse-objs := psmouse-base.o synaptics.o

psmouse-$(CONFIG_MOUSE_PS2_ALPS)	+= alps.o
psmouse-$(CONFIG_MOUSE_PS2_ELANTECH)	+= elantech.o
psmouse-$(CONFIG_MOUSE_PS2_OLPC)	+= hgpk.o
psmouse-$(CONFIG_MOUSE_PS2_LOGIPS2PP)	+= logips2pp.o
psmouse-$(CONFIG_MOUSE_PS2_LIFEBOOK)	+= lifebook.o
psmouse-$(CONFIG_MOUSE_PS2_TRACKPOINT)	+= trackpoint.o
psmouse-$(CONFIG_MOUSE_PS2_TOUCHKIT)	+= touchkit_ps2.o
