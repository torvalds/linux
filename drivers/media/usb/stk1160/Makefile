obj-stk1160-ac97-$(CONFIG_VIDEO_STK1160_AC97) := stk1160-ac97.o

stk1160-y := 	stk1160-core.o \
		stk1160-v4l.o \
		stk1160-video.o \
		stk1160-i2c.o \
		$(obj-stk1160-ac97-y)

obj-$(CONFIG_VIDEO_STK1160) += stk1160.o

ccflags-y += -Idrivers/media/i2c
