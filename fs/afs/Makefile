# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Red Hat Linux AFS client.
#

afs-cache-$(CONFIG_AFS_FSCACHE) := cache.o

kafs-objs := \
	$(afs-cache-y) \
	addr_list.o \
	callback.o \
	cell.o \
	cmservice.o \
	dir.o \
	dir_edit.o \
	dynroot.o \
	file.o \
	flock.o \
	fsclient.o \
	inode.o \
	main.o \
	misc.o \
	mntpt.o \
	proc.o \
	rotate.o \
	rxrpc.o \
	security.o \
	server.o \
	server_list.o \
	super.o \
	netdevices.o \
	vlclient.o \
	volume.o \
	write.o \
	xattr.o

obj-$(CONFIG_AFS_FS)  := kafs.o
