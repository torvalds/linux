ivtv-objs	:= ivtv-routing.o ivtv-cards.o ivtv-controls.o \
		   ivtv-driver.o ivtv-fileops.o ivtv-firmware.o \
		   ivtv-gpio.o ivtv-i2c.o ivtv-ioctl.o ivtv-irq.o \
		   ivtv-mailbox.o ivtv-queue.o ivtv-streams.o ivtv-udma.o \
		   ivtv-vbi.o ivtv-yuv.o

obj-$(CONFIG_VIDEO_IVTV) += ivtv.o
obj-$(CONFIG_VIDEO_FB_IVTV) += ivtvfb.o

EXTRA_CFLAGS += -Idrivers/media/video
EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core
EXTRA_CFLAGS += -Idrivers/media/dvb/frontends

