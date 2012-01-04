saa7164-objs	:= saa7164-cards.o saa7164-core.o saa7164-i2c.o saa7164-dvb.o \
			saa7164-fw.o saa7164-bus.o saa7164-cmd.o saa7164-api.o \
			saa7164-buffer.o saa7164-encoder.o saa7164-vbi.o

obj-$(CONFIG_VIDEO_SAA7164) += saa7164.o

ccflags-y += -Idrivers/media/video
ccflags-y += -Idrivers/media/common/tuners
ccflags-y += -Idrivers/media/dvb/dvb-core
ccflags-y += -Idrivers/media/dvb/frontends

ccflags-y += $(extra-cflags-y) $(extra-cflags-m)
