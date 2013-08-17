au0828-objs	:= au0828-core.o au0828-i2c.o au0828-cards.o au0828-dvb.o au0828-video.o au0828-vbi.o

obj-$(CONFIG_VIDEO_AU0828) += au0828.o

ccflags-y += -Idrivers/media/common/tuners
ccflags-y += -Idrivers/media/dvb/dvb-core
ccflags-y += -Idrivers/media/dvb/frontends

ccflags-y += $(extra-cflags-y) $(extra-cflags-m)
