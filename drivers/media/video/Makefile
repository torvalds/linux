#
# Makefile for the video capture/playback device drivers.
#

zoran-objs      :=	zr36120.o zr36120_i2c.o zr36120_mem.o
zr36067-objs	:=	zoran_procfs.o zoran_device.o \
			zoran_driver.o zoran_card.o
tuner-objs	:=	tuner-core.o tuner-types.o tuner-simple.o \
			mt20xx.o tda8290.o tea5767.o

msp3400-objs	:=	msp3400-driver.o msp3400-kthreads.o

obj-$(CONFIG_VIDEO_DEV) += videodev.o v4l2-common.o compat_ioctl32.o
obj-$(CONFIG_VIDEO_V4L1_COMPAT) += v4l1-compat.o

obj-$(CONFIG_VIDEO_BT848) += bt8xx/
obj-$(CONFIG_VIDEO_BT848) += tvaudio.o tda7432.o tda9875.o ir-kbd-i2c.o
obj-$(CONFIG_SOUND_TVMIXER) += tvmixer.o

obj-$(CONFIG_VIDEO_ZR36120) += zoran.o
obj-$(CONFIG_VIDEO_SAA6588) += saa6588.o
obj-$(CONFIG_VIDEO_SAA5246A) += saa5246a.o
obj-$(CONFIG_VIDEO_SAA5249) += saa5249.o
obj-$(CONFIG_VIDEO_CQCAM) += c-qcam.o
obj-$(CONFIG_VIDEO_BWQCAM) += bw-qcam.o
obj-$(CONFIG_VIDEO_W9966) += w9966.o
obj-$(CONFIG_VIDEO_ZORAN_BUZ) += saa7111.o saa7185.o zr36060.o
obj-$(CONFIG_VIDEO_ZORAN_DC10) += saa7110.o adv7175.o zr36060.o
obj-$(CONFIG_VIDEO_ZORAN_DC30) += adv7175.o vpx3220.o zr36050.o \
	zr36016.o
obj-$(CONFIG_VIDEO_ZORAN_LML33) += bt819.o bt856.o zr36060.o
obj-$(CONFIG_VIDEO_ZORAN_LML33R10) += saa7114.o adv7170.o zr36060.o
obj-$(CONFIG_VIDEO_ZORAN) += zr36067.o videocodec.o
obj-$(CONFIG_VIDEO_PMS) += pms.o
obj-$(CONFIG_VIDEO_PLANB) += planb.o
obj-$(CONFIG_VIDEO_VINO) += vino.o saa7191.o indycam.o
obj-$(CONFIG_VIDEO_STRADIS) += stradis.o
obj-$(CONFIG_VIDEO_CPIA) += cpia.o
obj-$(CONFIG_VIDEO_CPIA_PP) += cpia_pp.o
obj-$(CONFIG_VIDEO_CPIA_USB) += cpia_usb.o
obj-$(CONFIG_VIDEO_MEYE) += meye.o
obj-$(CONFIG_VIDEO_SAA7134) += ir-kbd-i2c.o saa7134/
obj-$(CONFIG_VIDEO_CX88) += cx88/
obj-$(CONFIG_VIDEO_EM28XX) += em28xx/
obj-$(CONFIG_VIDEO_EM28XX) += tvp5150.o
obj-$(CONFIG_VIDEO_MSP3400) += msp3400.o
obj-$(CONFIG_VIDEO_CS53L32A) += cs53l32a.o
obj-$(CONFIG_VIDEO_WM8775) += wm8775.o
obj-$(CONFIG_VIDEO_WM8739) += wm8739.o
obj-$(CONFIG_VIDEO_OVCAMCHIP) += ovcamchip/
obj-$(CONFIG_VIDEO_CPIA2) += cpia2/
obj-$(CONFIG_VIDEO_MXB) += saa7111.o tda9840.o tea6415c.o tea6420.o mxb.o
obj-$(CONFIG_VIDEO_HEXIUM_ORION) += hexium_orion.o
obj-$(CONFIG_VIDEO_HEXIUM_GEMINI) += hexium_gemini.o
obj-$(CONFIG_VIDEO_DPC) += saa7111.o dpc7146.o
obj-$(CONFIG_TUNER_3036) += tuner-3036.o

obj-$(CONFIG_VIDEO_TUNER) += tuner.o tda9887.o
obj-$(CONFIG_VIDEO_BUF)   += video-buf.o
obj-$(CONFIG_VIDEO_BUF_DVB) += video-buf-dvb.o
obj-$(CONFIG_VIDEO_BTCX)  += btcx-risc.o
obj-$(CONFIG_VIDEO_TVEEPROM) += tveeprom.o

obj-$(CONFIG_VIDEO_M32R_AR_M64278) += arv.o

obj-$(CONFIG_VIDEO_CX25840) += cx25840/
obj-$(CONFIG_VIDEO_SAA711X) += saa7115.o
obj-$(CONFIG_VIDEO_SAA7127) += saa7127.o
obj-$(CONFIG_VIDEO_UPD64031A) += upd64031a.o
obj-$(CONFIG_VIDEO_UPD64083) += upd64083.o

obj-$(CONFIG_USB_DABUSB)        += dabusb.o
obj-$(CONFIG_USB_DSBR)          += dsbr100.o
obj-$(CONFIG_USB_OV511)         += ov511.o
obj-$(CONFIG_USB_SE401)         += se401.o
obj-$(CONFIG_USB_STV680)        += stv680.o
obj-$(CONFIG_USB_W9968CF)       += w9968cf.o

obj-$(CONFIG_USB_SN9C102)       += sn9c102/
obj-$(CONFIG_USB_ET61X251)      += et61x251/
obj-$(CONFIG_USB_PWC)           += pwc/
obj-$(CONFIG_USB_ZC0301)        += zc0301/

obj-$(CONFIG_USB_IBMCAM)        += usbvideo/
obj-$(CONFIG_USB_KONICAWC)      += usbvideo/
obj-$(CONFIG_USB_VICAM)         += usbvideo/

EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core
