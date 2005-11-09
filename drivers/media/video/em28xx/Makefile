em28xx-objs     := em28xx-video.o em28xx-i2c.o em28xx-cards.o em28xx-core.o \
		   em28xx-input.o

obj-$(CONFIG_VIDEO_EM28XX) += em28xx.o
obj-$(CONFIG_VIDEO_EM28XX) += saa711x.o tvp5150.o

EXTRA_CFLAGS += -I$(src)/..
