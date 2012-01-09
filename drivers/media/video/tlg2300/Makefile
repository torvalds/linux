poseidon-objs := pd-video.o pd-alsa.o pd-dvb.o pd-radio.o pd-main.o

obj-$(CONFIG_VIDEO_TLG2300) += poseidon.o

ccflags-y += -Idrivers/media/video
ccflags-y += -Idrivers/media/common/tuners
ccflags-y += -Idrivers/media/dvb/dvb-core
ccflags-y += -Idrivers/media/dvb/frontends

