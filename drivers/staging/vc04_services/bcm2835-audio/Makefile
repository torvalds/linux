obj-$(CONFIG_SND_BCM2835)	+= snd-bcm2835.o
snd-bcm2835-objs		:= bcm2835.o bcm2835-ctl.o bcm2835-pcm.o bcm2835-vchiq.o

ccflags-y += -I $(srctree)/$(src)/.. -D__VCCOREVER__=0x04000000
