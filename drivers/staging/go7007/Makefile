#obj-m += go7007.o go7007-usb.o snd-go7007.o wis-saa7115.o wis-tw9903.o \
		wis-uda1342.o wis-sony-tuner.o wis-saa7113.o wis-ov7640.o \
		wis-tw2804.o


obj-$(CONFIG_VIDEO_GO7007) += go7007.o
obj-$(CONFIG_VIDEO_GO7007_USB) += go7007-usb.o
obj-$(CONFIG_VIDEO_GO7007_USB_S2250_BOARD) += s2250.o

go7007-objs += go7007-v4l2.o go7007-driver.o go7007-i2c.o go7007-fw.o \
		snd-go7007.o wis-saa7113.o

s2250-objs += s2250-board.o s2250-loader.o

# Uncompile when the saa7134 patches get into upstream
#ifneq ($(CONFIG_VIDEO_SAA7134),)
#obj-$(CONFIG_VIDEO_SAA7134) += saa7134-go7007.o
#EXTRA_CFLAGS += -Idrivers/media/video/saa7134
#endif

ifneq ($(CONFIG_VIDEO_GO7007_USB_S2250_BOARD),)
EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-usb
endif

EXTRA_CFLAGS += -Idrivers/staging/saa7134
EXTRA_CFLAGS += -Idrivers/media/dvb/frontends
EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core
