dvb-usb-objs = dvb-usb-firmware.o dvb-usb-init.o dvb-usb-urb.o dvb-usb-i2c.o dvb-usb-dvb.o dvb-usb-remote.o
obj-$(CONFIG_DVB_USB) += dvb-usb.o

dvb-usb-vp7045-objs = vp7045.o vp7045-fe.o
obj-$(CONFIG_DVB_USB_VP7045) += dvb-usb-vp7045.o

dvb-usb-vp702x-objs = vp702x.o vp702x-fe.o
obj-$(CONFIG_DVB_USB_VP702X) += dvb-usb-vp702x.o

dvb-usb-dtt200u-objs = dtt200u.o dtt200u-fe.o
obj-$(CONFIG_DVB_USB_DTT200U) += dvb-usb-dtt200u.o

dvb-usb-dibusb-common-objs = dibusb-common.o

dvb-usb-a800-objs = a800.o
obj-$(CONFIG_DVB_USB_A800) += dvb-usb-dibusb-common.o dvb-usb-a800.o

dvb-usb-dibusb-mb-objs = dibusb-mb.o
obj-$(CONFIG_DVB_USB_DIBUSB_MB) += dvb-usb-dibusb-common.o dvb-usb-dibusb-mb.o

dvb-usb-dibusb-mc-objs = dibusb-mc.o
obj-$(CONFIG_DVB_USB_DIBUSB_MC) += dvb-usb-dibusb-common.o dvb-usb-dibusb-mc.o

dvb-usb-nova-t-usb2-objs = nova-t-usb2.o
obj-$(CONFIG_DVB_USB_NOVA_T_USB2) += dvb-usb-dibusb-common.o dvb-usb-nova-t-usb2.o

dvb-usb-umt-010-objs = umt-010.o
obj-$(CONFIG_DVB_USB_UMT_010) += dvb-usb-dibusb-common.o dvb-usb-umt-010.o

dvb-usb-digitv-objs = digitv.o
obj-$(CONFIG_DVB_USB_DIGITV) += dvb-usb-digitv.o

dvb-usb-cxusb-objs = cxusb.o
obj-$(CONFIG_DVB_USB_CXUSB) += dvb-usb-cxusb.o

EXTRA_CFLAGS = -Idrivers/media/dvb/dvb-core/ -Idrivers/media/dvb/frontends/
