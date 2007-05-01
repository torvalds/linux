#
# Makefile for the mouse drivers.
#

# Each configuration option enables a list of files.

obj-$(CONFIG_MOUSE_AMIGA)	+= amimouse.o
obj-$(CONFIG_MOUSE_ATARI)	+= atarimouse.o
obj-$(CONFIG_MOUSE_RISCPC)	+= rpcmouse.o
obj-$(CONFIG_MOUSE_INPORT)	+= inport.o
obj-$(CONFIG_MOUSE_LOGIBM)	+= logibm.o
obj-$(CONFIG_MOUSE_PC110PAD)	+= pc110pad.o
obj-$(CONFIG_MOUSE_PS2)		+= psmouse.o
obj-$(CONFIG_MOUSE_SERIAL)	+= sermouse.o
obj-$(CONFIG_MOUSE_HIL)		+= hil_ptr.o
obj-$(CONFIG_MOUSE_VSXXXAA)	+= vsxxxaa.o

psmouse-objs  := psmouse-base.o alps.o logips2pp.o synaptics.o lifebook.o trackpoint.o
