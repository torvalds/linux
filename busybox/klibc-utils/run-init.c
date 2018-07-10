/*
 * run-init implementation for busybox
 *
 * Copyright (c) 2017 Denys Vlasenko <vda.linux@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config RUN_INIT
//config:	bool "run-init (7.5 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	The run-init utility is used from initramfs to select a new
//config:	root device. Under initramfs, you have to use this instead of
//config:	pivot_root.
//config:
//config:	Booting with initramfs extracts a gzipped cpio archive into rootfs
//config:	(which is a variant of ramfs/tmpfs). Because rootfs can't be moved
//config:	or unmounted, pivot_root will not work from initramfs. Instead,
//config:	run-init deletes everything out of rootfs (including itself),
//config:	does a mount --move that overmounts rootfs with the new root, and
//config:	then execs the specified init program.
//config:
//config:	util-linux has a similar tool, switch-root.
//config:	run-init differs by also having a "-d CAPS_TO_DROP" option.

/* applet and kbuild hooks are in switch_root.c */
