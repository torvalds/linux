#
# Makefile for the Linux Bluetooth RFCOMM layer.
#

obj-$(CONFIG_BACKPORT_BT_RFCOMM) += rfcomm.o

rfcomm-y			:= core.o sock.o
rfcomm-$(CONFIG_BACKPORT_BT_RFCOMM_TTY)	+= tty.o
