bttv-objs      :=      bttv-driver.o bttv-cards.o bttv-if.o \
		       bttv-risc.o bttv-vbi.o bttv-i2c.o bttv-gpio.o \
		       bttv-input.o bttv-audio-hook.o btcx-risc.o

obj-$(CONFIG_VIDEO_BT848) += bttv.o
obj-$(CONFIG_DVB_BT8XX) += bt878.o dvb-bt8xx.o dst.o dst_ca.o

ccflags-y += -Idrivers/media/dvb-core
ccflags-y += -Idrivers/media/dvb-frontends
ccflags-y += -Idrivers/media/i2c
ccflags-y += -Idrivers/media/common
ccflags-y += -Idrivers/media/tuners
