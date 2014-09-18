#
# Makefile for the VIA framebuffer driver (for Linux Kernel 2.6)
#

obj-$(CONFIG_FB_VIA) += viafb.o

viafb-y	:=viafbdev.o hw.o via_i2c.o dvi.o lcd.o ioctl.o accel.o \
	via_utility.o vt1636.o global.o tblDPASetting.o viamode.o \
	via-core.o via-gpio.o via_modesetting.o via_clock.o \
	via_aux.o via_aux_edid.o via_aux_vt1636.o via_aux_vt1632.o \
	via_aux_vt1631.o via_aux_vt1625.o via_aux_vt1622.o via_aux_vt1621.o \
	via_aux_sii164.o via_aux_ch7301.o
