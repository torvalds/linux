easycap-objs := easycap_main.o
easycap-objs += easycap_low.o
easycap-objs += easycap_ioctl.o
easycap-objs += easycap_settings.o
easycap-objs += easycap_testcard.o
easycap-objs += easycap_sound.o
easycap-$(CONFIG_EASYCAP_OSS) += easycap_sound_oss.o

obj-$(CONFIG_EASYCAP)       += easycap.o

ccflags-y := -Wall

