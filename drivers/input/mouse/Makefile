#
# Makefile for the mouse drivers.
#

# Each configuration option enables a list of files.

obj-$(CONFIG_MOUSE_AMIGA)		+= amimouse.o
obj-$(CONFIG_MOUSE_APPLETOUCH)		+= appletouch.o
obj-$(CONFIG_MOUSE_ATARI)		+= atarimouse.o
obj-$(CONFIG_MOUSE_BCM5974)		+= bcm5974.o
obj-$(CONFIG_MOUSE_CYAPA)		+= cyapatp.o
obj-$(CONFIG_MOUSE_ELAN_I2C)		+= elan_i2c.o
obj-$(CONFIG_MOUSE_GPIO)		+= gpio_mouse.o
obj-$(CONFIG_MOUSE_INPORT)		+= inport.o
obj-$(CONFIG_MOUSE_LOGIBM)		+= logibm.o
obj-$(CONFIG_MOUSE_MAPLE)		+= maplemouse.o
obj-$(CONFIG_MOUSE_NAVPOINT_PXA27x)	+= navpoint.o
obj-$(CONFIG_MOUSE_PC110PAD)		+= pc110pad.o
obj-$(CONFIG_MOUSE_PS2)			+= psmouse.o
obj-$(CONFIG_MOUSE_PXA930_TRKBALL)	+= pxa930_trkball.o
obj-$(CONFIG_MOUSE_RISCPC)		+= rpcmouse.o
obj-$(CONFIG_MOUSE_SERIAL)		+= sermouse.o
obj-$(CONFIG_MOUSE_SYNAPTICS_I2C)	+= synaptics_i2c.o
obj-$(CONFIG_MOUSE_SYNAPTICS_USB)	+= synaptics_usb.o
obj-$(CONFIG_MOUSE_VSXXXAA)		+= vsxxxaa.o

cyapatp-objs := cyapa.o cyapa_gen3.o cyapa_gen5.o cyapa_gen6.o
psmouse-objs := psmouse-base.o synaptics.o focaltech.o

psmouse-$(CONFIG_MOUSE_PS2_ALPS)	+= alps.o
psmouse-$(CONFIG_MOUSE_PS2_ELANTECH)	+= elantech.o
psmouse-$(CONFIG_MOUSE_PS2_OLPC)	+= hgpk.o
psmouse-$(CONFIG_MOUSE_PS2_LOGIPS2PP)	+= logips2pp.o
psmouse-$(CONFIG_MOUSE_PS2_LIFEBOOK)	+= lifebook.o
psmouse-$(CONFIG_MOUSE_PS2_SENTELIC)	+= sentelic.o
psmouse-$(CONFIG_MOUSE_PS2_TRACKPOINT)	+= trackpoint.o
psmouse-$(CONFIG_MOUSE_PS2_TOUCHKIT)	+= touchkit_ps2.o
psmouse-$(CONFIG_MOUSE_PS2_CYPRESS)	+= cypress_ps2.o
psmouse-$(CONFIG_MOUSE_PS2_VMMOUSE)	+= vmmouse.o

elan_i2c-objs := elan_i2c_core.o
elan_i2c-$(CONFIG_MOUSE_ELAN_I2C_I2C)	+= elan_i2c_i2c.o
elan_i2c-$(CONFIG_MOUSE_ELAN_I2C_SMBUS)	+= elan_i2c_smbus.o
