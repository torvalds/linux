# Makefile for the kernel ISDN subsystem and device drivers.

# Each configuration option enables a list of files.

obj-$(CONFIG_ISDN_I4L)		+= isdn.o
obj-$(CONFIG_ISDN_PPP_BSDCOMP)	+= isdn_bsdcomp.o

# Multipart objects.

isdn-y				:= isdn_net.o isdn_tty.o isdn_v110.o isdn_common.o

# Optional parts of multipart objects.

isdn-$(CONFIG_ISDN_PPP)		+= isdn_ppp.o
isdn-$(CONFIG_ISDN_X25)		+= isdn_concap.o isdn_x25iface.o
isdn-$(CONFIG_ISDN_AUDIO)		+= isdn_audio.o
isdn-$(CONFIG_ISDN_TTY_FAX)	+= isdn_ttyfax.o

