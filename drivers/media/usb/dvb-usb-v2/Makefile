# SPDX-License-Identifier: GPL-2.0
dvb_usb_v2-objs := dvb_usb_core.o dvb_usb_urb.o usb_urb.o
obj-$(CONFIG_DVB_USB_V2) += dvb_usb_v2.o

dvb-usb-af9015-objs := af9015.o
obj-$(CONFIG_DVB_USB_AF9015) += dvb-usb-af9015.o

dvb-usb-af9035-objs := af9035.o
obj-$(CONFIG_DVB_USB_AF9035) += dvb-usb-af9035.o

dvb-usb-anysee-objs := anysee.o
obj-$(CONFIG_DVB_USB_ANYSEE) += dvb-usb-anysee.o

dvb-usb-au6610-objs := au6610.o
obj-$(CONFIG_DVB_USB_AU6610) += dvb-usb-au6610.o

dvb-usb-az6007-objs := az6007.o
obj-$(CONFIG_DVB_USB_AZ6007) += dvb-usb-az6007.o

dvb-usb-ce6230-objs := ce6230.o
obj-$(CONFIG_DVB_USB_CE6230) += dvb-usb-ce6230.o

dvb-usb-ec168-objs := ec168.o
obj-$(CONFIG_DVB_USB_EC168) += dvb-usb-ec168.o

dvb-usb-lmedm04-objs := lmedm04.o
obj-$(CONFIG_DVB_USB_LME2510) += dvb-usb-lmedm04.o

dvb-usb-gl861-objs := gl861.o
obj-$(CONFIG_DVB_USB_GL861) += dvb-usb-gl861.o

dvb-usb-mxl111sf-objs += mxl111sf.o mxl111sf-phy.o mxl111sf-i2c.o
dvb-usb-mxl111sf-objs += mxl111sf-gpio.o
obj-$(CONFIG_DVB_USB_MXL111SF) += dvb-usb-mxl111sf.o
obj-$(CONFIG_DVB_USB_MXL111SF) += mxl111sf-demod.o
obj-$(CONFIG_DVB_USB_MXL111SF) += mxl111sf-tuner.o

dvb-usb-rtl28xxu-objs := rtl28xxu.o
obj-$(CONFIG_DVB_USB_RTL28XXU) += dvb-usb-rtl28xxu.o

dvb-usb-dvbsky-objs := dvbsky.o
obj-$(CONFIG_DVB_USB_DVBSKY) += dvb-usb-dvbsky.o

dvb-usb-zd1301-objs := zd1301.o
obj-$(CONFIG_DVB_USB_ZD1301) += zd1301.o

ccflags-y += -I$(srctree)/drivers/media/dvb-frontends
ccflags-y += -I$(srctree)/drivers/media/tuners
ccflags-y += -I$(srctree)/drivers/media/common
