smiapp-objs			+= smiapp-core.o smiapp-regs.o \
				   smiapp-quirk.o smiapp-limits.o
obj-$(CONFIG_BACKPORT_VIDEO_SMIAPP)	+= smiapp.o

ccflags-y += -I$(backport_srctree)/drivers/media/i2c
