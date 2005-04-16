#
# Makefile for Red Hat Linux AFS client.
#

#CFLAGS += -finstrument-functions

kafs-objs := \
	callback.o \
	cell.o \
	cmservice.o \
	dir.o \
	file.o \
	fsclient.o \
	inode.o \
	kafsasyncd.o \
	kafstimod.o \
	main.o \
	misc.o \
	mntpt.o \
	proc.o \
	server.o \
	super.o \
	vlclient.o \
	vlocation.o \
	vnode.o \
	volume.o

obj-$(CONFIG_AFS_FS)  := kafs.o
