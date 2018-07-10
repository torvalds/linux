/*
 * Copyright 2010 Rob Landley <rob@landley.net>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config NBDCLIENT
//config:	bool "nbd-client (4.6 kb)"
//config:	default y
//config:	help
//config:	Network block device client

//applet:IF_NBDCLIENT(APPLET_NOEXEC(nbd-client, nbdclient, BB_DIR_USR_SBIN, BB_SUID_DROP, nbdclient))

//kbuild:lib-$(CONFIG_NBDCLIENT) += nbd-client.o

#include "libbb.h"
#include <netinet/tcp.h>
#include <linux/fs.h>

#define NBD_SET_SOCK          _IO(0xab, 0)
#define NBD_SET_BLKSIZE       _IO(0xab, 1)
#define NBD_SET_SIZE          _IO(0xab, 2)
#define NBD_DO_IT             _IO(0xab, 3)
#define NBD_CLEAR_SOCK        _IO(0xab, 4)
#define NBD_CLEAR_QUEUE       _IO(0xab, 5)
#define NBD_PRINT_DEBUG       _IO(0xab, 6)
#define NBD_SET_SIZE_BLOCKS   _IO(0xab, 7)
#define NBD_DISCONNECT        _IO(0xab, 8)
#define NBD_SET_TIMEOUT       _IO(0xab, 9)

//usage:#define nbdclient_trivial_usage
//usage:       "HOST PORT BLOCKDEV"
//usage:#define nbdclient_full_usage "\n\n"
//usage:       "Connect to HOST and provide a network block device on BLOCKDEV"

//TODO: more compat with nbd-client version 2.9.13 -
//Usage: nbd-client [bs=blocksize] [timeout=sec] host port nbd_device [-swap] [-persist] [-nofork]
//Or   : nbd-client -d nbd_device
//Or   : nbd-client -c nbd_device
//Default value for blocksize is 1024 (recommended for ethernet)
//Allowed values for blocksize are 512,1024,2048,4096
//Note, that kernel 2.4.2 and older ones do not work correctly with
//blocksizes other than 1024 without patches

int nbdclient_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int nbdclient_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned long timeout = 0;
#if BB_MMU
	int nofork = 0;
#endif
	char *host, *port, *device;
	struct nbd_header_t {
		uint64_t magic1; // "NBDMAGIC"
		uint64_t magic2; // 0x420281861253 big endian
		uint64_t devsize;
		uint32_t flags;
		char data[124];
	} nbd_header;

	BUILD_BUG_ON(offsetof(struct nbd_header_t, data) != 8+8+8+4);

	// Parse command line stuff (just a stub now)
	if (!argv[1] || !argv[2] || !argv[3] || argv[4])
		bb_show_usage();

#if !BB_MMU
	bb_daemonize_or_rexec(DAEMON_CLOSE_EXTRA_FDS, argv);
#endif

	host = argv[1];
	port = argv[2];
	device = argv[3];

	// Repeat until spanked (-persist behavior)
	for (;;) {
		int sock, nbd;
		int ro;

		// Make sure the /dev/nbd exists
		nbd = xopen(device, O_RDWR);

		// Find and connect to server
		sock = create_and_connect_stream_or_die(host, xatou16(port));
		setsockopt_1(sock, IPPROTO_TCP, TCP_NODELAY);

		// Log on to the server
		xread(sock, &nbd_header, 8+8+8+4 + 124);
		if (memcmp(&nbd_header.magic1, "NBDMAGIC""\x00\x00\x42\x02\x81\x86\x12\x53", 16) != 0)
			bb_error_msg_and_die("login failed");

		// Set 4k block size.  Everything uses that these days
		ioctl(nbd, NBD_SET_BLKSIZE, 4096);
		ioctl(nbd, NBD_SET_SIZE_BLOCKS, SWAP_BE64(nbd_header.devsize) / 4096);
		ioctl(nbd, NBD_CLEAR_SOCK);

		// If the sucker was exported read only, respect that locally
		ro = (nbd_header.flags & SWAP_BE32(2)) / SWAP_BE32(2);
		if (ioctl(nbd, BLKROSET, &ro) < 0)
			bb_perror_msg_and_die("BLKROSET");

		if (timeout)
			if (ioctl(nbd, NBD_SET_TIMEOUT, timeout))
				bb_perror_msg_and_die("NBD_SET_TIMEOUT");
		if (ioctl(nbd, NBD_SET_SOCK, sock))
			bb_perror_msg_and_die("NBD_SET_SOCK");

		// if (swap) mlockall(MCL_CURRENT|MCL_FUTURE);

#if BB_MMU
		// Open the device to force reread of the partition table.
		// Need to do it in a separate process, since open(device)
		// needs some other process to sit in ioctl(nbd, NBD_DO_IT).
		if (fork() == 0) {
			char *s = strrchr(device, '/');
			sprintf(nbd_header.data, "/sys/block/%.32s/pid", s ? s + 1 : device);
			// Is it up yet?
			for (;;) {
				int fd = open(nbd_header.data, O_RDONLY);
				if (fd >= 0) {
					//close(fd);
					break;
				}
				sleep(1);
			}
			open(device, O_RDONLY);
			return 0;
		}

		// Daemonize here
		if (!nofork) {
			daemon(0, 0);
			nofork = 1;
		}
#endif

		// This turns us (the process that calls this ioctl)
		// into a dedicated NBD request handler.
		// We block here for a long time.
		// When exactly ioctl returns? On a signal,
		// or if someone does ioctl(NBD_DISCONNECT) [nbd-client -d].
		if (ioctl(nbd, NBD_DO_IT) >= 0 || errno == EBADR) {
			// Flush queue and exit
			ioctl(nbd, NBD_CLEAR_QUEUE);
			ioctl(nbd, NBD_CLEAR_SOCK);
			break;
		}

		close(sock);
		close(nbd);
	}

	return 0;
}
