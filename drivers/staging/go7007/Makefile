#obj-m += go7007.o go7007-usb.o snd-go7007.o wis-saa7115.o wis-tw9903.o \
		wis-uda1342.o wis-sony-tuner.o wis-saa7113.o wis-ov7640.o \
		wis-tw2804.o


obj-$(CONFIG_VIDEO_GO7007) += go7007.o
obj-$(CONFIG_VIDEO_GO7007_USB) += go7007-usb.o

go7007-objs += go7007-v4l2.o go7007-driver.o go7007-i2c.o go7007-fw.o snd-go7007.o


#ifneq ($(SAA7134_BUILD),)
#obj-m += saa7134-go7007.o
#endif

EXTRA_CFLAGS += -Idrivers/staging/saa7134
EXTRA_CFLAGS += -Idrivers/media/dvb/frontends
EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core
