# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Red Hat Linux AFS client.
#

afs-cache-$(CONFIG_AFS_FSCACHE) := cache.o

kafs-objs := \
	$(afs-cache-y) \
	callback.o \
	cell.o \
	cmservice.o \
	dir.o \
	file.o \
	flock.o \
	fsclient.o \
	inode.o \
	main.o \
	misc.o \
	mntpt.o \
	proc.o \
	rxrpc.o \
	security.o \
	server.o \
	super.o \
	netdevices.o \
	vlclient.o \
	vlocation.o \
	vnode.o \
	volume.o \
	write.o \
	xattr.o

obj-$(CONFIG_AFS_FS)  := kafs.o
