ivtv-objs	:= ivtv-routing.o ivtv-cards.o ivtv-controls.o \
		   ivtv-driver.o ivtv-fileops.o ivtv-firmware.o \
		   ivtv-gpio.o ivtv-i2c.o ivtv-ioctl.o ivtv-irq.o \
		   ivtv-mailbox.o ivtv-queue.o ivtv-streams.o ivtv-udma.o \
		   ivtv-vbi.o ivtv-yuv.o

obj-$(CONFIG_VIDEO_IVTV) += ivtv.o
obj-$(CONFIG_VIDEO_FB_IVTV) += ivtvfb.o

ccflags-y += -Idrivers/media/video
ccflags-y += -Idrivers/media/common/tuners
ccflags-y += -Idrivers/media/dvb/dvb-core
ccflags-y += -Idrivers/media/dvb/frontends

