/*
 *  pcap-linux.c: Packet capture interface to the Linux kernel
 *
 *  Copyright (c) 2000 Torsten Landschoff <torsten@debian.org>
 *  		       Sebastian Krahmer  <krahmer@cs.uni-potsdam.de>
 *
 *  License: BSD
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *  3. The names of the authors may not be used to endorse or promote
 *     products derived from this software without specific prior
 *     written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 *  IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 *  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *  Modifications:     Added PACKET_MMAP support
 *                     Paolo Abeni <paolo.abeni@email.it>
 *                     Added TPACKET_V3 support
 *                     Gabor Tatarka <gabor.tatarka@ericsson.com>
 *
 *                     based on previous works of:
 *                     Simon Patarin <patarin@cs.unibo.it>
 *                     Phil Wood <cpw@lanl.gov>
 *
 * Monitor-mode support for mac80211 includes code taken from the iw
 * command; the copyright notice for that code is
 *
 * Copyright (c) 2007, 2008	Johannes Berg
 * Copyright (c) 2007		Andy Lutomirski
 * Copyright (c) 2007		Mike Kershaw
 * Copyright (c) 2008		Gábor Stefanik
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Known problems with 2.0[.x] kernels:
 *
 *   - The loopback device gives every packet twice; on 2.2[.x] kernels,
 *     if we use PF_PACKET, we can filter out the transmitted version
 *     of the packet by using data in the "sockaddr_ll" returned by
 *     "recvfrom()", but, on 2.0[.x] kernels, we have to use
 *     PF_INET/SOCK_PACKET, which means "recvfrom()" supplies a
 *     "sockaddr_pkt" which doesn't give us enough information to let
 *     us do that.
 *
 *   - We have to set the interface's IFF_PROMISC flag ourselves, if
 *     we're to run in promiscuous mode, which means we have to turn
 *     it off ourselves when we're done; the kernel doesn't keep track
 *     of how many sockets are listening promiscuously, which means
 *     it won't get turned off automatically when no sockets are
 *     listening promiscuously.  We catch "pcap_close()" and, for
 *     interfaces we put into promiscuous mode, take them out of
 *     promiscuous mode - which isn't necessarily the right thing to
 *     do, if another socket also requested promiscuous mode between
 *     the time when we opened the socket and the time when we close
 *     the socket.
 *
 *   - MSG_TRUNC isn't supported, so you can't specify that "recvfrom()"
 *     return the amount of data that you could have read, rather than
 *     the amount that was returned, so we can't just allocate a buffer
 *     whose size is the snapshot length and pass the snapshot length
 *     as the byte count, and also pass MSG_TRUNC, so that the return
 *     value tells us how long the packet was on the wire.
 *
 *     This means that, if we want to get the actual size of the packet,
 *     so we can return it in the "len" field of the packet header,
 *     we have to read the entire packet, not just the part that fits
 *     within the snapshot length, and thus waste CPU time copying data
 *     from the kernel that our caller won't see.
 *
 *     We have to get the actual size, and supply it in "len", because
 *     otherwise, the IP dissector in tcpdump, for example, will complain
 *     about "truncated-ip", as the packet will appear to have been
 *     shorter, on the wire, than the IP header said it should have been.
 */


#define _GNU_SOURCE

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <sys/mman.h>
#include <linux/if.h>
#include <linux/if_packet.h>
#include <linux/sockios.h>
#include <netinet/in.h>
#include <linux/if_ether.h>
#include <net/if_arp.h>
#include <poll.h>
#include <dirent.h>

#include "pcap-int.h"
#include "pcap/sll.h"
#include "pcap/vlan.h"

/*
 * If PF_PACKET is defined, we can use {SOCK_RAW,SOCK_DGRAM}/PF_PACKET
 * sockets rather than SOCK_PACKET sockets.
 *
 * To use them, we include <linux/if_packet.h> rather than
 * <netpacket/packet.h>; we do so because
 *
 *	some Linux distributions (e.g., Slackware 4.0) have 2.2 or
 *	later kernels and libc5, and don't provide a <netpacket/packet.h>
 *	file;
 *
 *	not all versions of glibc2 have a <netpacket/packet.h> file
 *	that defines stuff needed for some of the 2.4-or-later-kernel
 *	features, so if the system has a 2.4 or later kernel, we
 *	still can't use those features.
 *
 * We're already including a number of other <linux/XXX.h> headers, and
 * this code is Linux-specific (no other OS has PF_PACKET sockets as
 * a raw packet capture mechanism), so it's not as if you gain any
 * useful portability by using <netpacket/packet.h>
 *
 * XXX - should we just include <linux/if_packet.h> even if PF_PACKET
 * isn't defined?  It only defines one data structure in 2.0.x, so
 * it shouldn't cause any problems.
 */
#ifdef PF_PACKET
# include <linux/if_packet.h>

 /*
  * On at least some Linux distributions (for example, Red Hat 5.2),
  * there's no <netpacket/packet.h> file, but PF_PACKET is defined if
  * you include <sys/socket.h>, but <linux/if_packet.h> doesn't define
  * any of the PF_PACKET stuff such as "struct sockaddr_ll" or any of
  * the PACKET_xxx stuff.
  *
  * So we check whether PACKET_HOST is defined, and assume that we have
  * PF_PACKET sockets only if it is defined.
  */
# ifdef PACKET_HOST
#  define HAVE_PF_PACKET_SOCKETS
#  ifdef PACKET_AUXDATA
#   define HAVE_PACKET_AUXDATA
#  endif /* PACKET_AUXDATA */
# endif /* PACKET_HOST */


 /* check for memory mapped access avaibility. We assume every needed
  * struct is defined if the macro TPACKET_HDRLEN is defined, because it
  * uses many ring related structs and macros */
# ifdef PCAP_SUPPORT_PACKET_RING
# ifdef TPACKET_HDRLEN
#  define HAVE_PACKET_RING
#  ifdef TPACKET3_HDRLEN
#   define HAVE_TPACKET3
#  endif /* TPACKET3_HDRLEN */
#  ifdef TPACKET2_HDRLEN
#   define HAVE_TPACKET2
#  else  /* TPACKET2_HDRLEN */
#   define TPACKET_V1	0    /* Old kernel with only V1, so no TPACKET_Vn defined */
#  endif /* TPACKET2_HDRLEN */
# endif /* TPACKET_HDRLEN */
# endif /* PCAP_SUPPORT_PACKET_RING */
#endif /* PF_PACKET */

#ifdef SO_ATTACH_FILTER
#include <linux/types.h>
#include <linux/filter.h>
#endif

#ifdef HAVE_LINUX_NET_TSTAMP_H
#include <linux/net_tstamp.h>
#endif

#ifdef HAVE_LINUX_SOCKIOS_H
#include <linux/sockios.h>
#endif

#ifdef HAVE_LINUX_IF_BONDING_H
#include <linux/if_bonding.h>

/*
 * The ioctl code to use to check whether a device is a bonding device.
 */
#if defined(SIOCBONDINFOQUERY)
	#define BOND_INFO_QUERY_IOCTL SIOCBONDINFOQUERY
#elif defined(BOND_INFO_QUERY_OLD)
	#define BOND_INFO_QUERY_IOCTL BOND_INFO_QUERY_OLD
#endif
#endif /* HAVE_LINUX_IF_BONDING_H */

/*
 * Got Wireless Extensions?
 */
#ifdef HAVE_LINUX_WIRELESS_H
#include <linux/wireless.h>
#endif /* HAVE_LINUX_WIRELESS_H */

/*
 * Got libnl?
 */
#ifdef HAVE_LIBNL
#include <linux/nl80211.h>

#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <netlink/msg.h>
#include <netlink/attr.h>
#endif /* HAVE_LIBNL */

/*
 * Got ethtool support?
 */
#ifdef HAVE_LINUX_ETHTOOL_H
#include <linux/ethtool.h>
#endif

#ifndef HAVE_SOCKLEN_T
typedef int		socklen_t;
#endif

#ifndef MSG_TRUNC
/*
 * This is being compiled on a system that lacks MSG_TRUNC; define it
 * with the value it has in the 2.2 and later kernels, so that, on
 * those kernels, when we pass it in the flags argument to "recvfrom()"
 * we're passing the right value and thus get the MSG_TRUNC behavior
 * we want.  (We don't get that behavior on 2.0[.x] kernels, because
 * they didn't support MSG_TRUNC.)
 */
#define MSG_TRUNC	0x20
#endif

#ifndef SOL_PACKET
/*
 * This is being compiled on a system that lacks SOL_PACKET; define it
 * with the value it has in the 2.2 and later kernels, so that we can
 * set promiscuous mode in the good modern way rather than the old
 * 2.0-kernel crappy way.
 */
#define SOL_PACKET	263
#endif

#define MAX_LINKHEADER_SIZE	256

/*
 * When capturing on all interfaces we use this as the buffer size.
 * Should be bigger then all MTUs that occur in real life.
 * 64kB should be enough for now.
 */
#define BIGGER_THAN_ALL_MTUS	(64*1024)

/*
 * Private data for capturing on Linux SOCK_PACKET or PF_PACKET sockets.
 */
struct pcap_linux {
	u_int	packets_read;	/* count of packets read with recvfrom() */
	long	proc_dropped;	/* packets reported dropped by /proc/net/dev */
	struct pcap_stat stat;

	char	*device;	/* device name */
	int	filter_in_userland; /* must filter in userland */
	int	blocks_to_filter_in_userland;
	int	must_do_on_close; /* stuff we must do when we close */
	int	timeout;	/* timeout for buffering */
	int	sock_packet;	/* using Linux 2.0 compatible interface */
	int	cooked;		/* using SOCK_DGRAM rather than SOCK_RAW */
	int	ifindex;	/* interface index of device we're bound to */
	int	lo_ifindex;	/* interface index of the loopback device */
	bpf_u_int32 oldmode;	/* mode to restore when turning monitor mode off */
	char	*mondevice;	/* mac80211 monitor device we created */
	u_char	*mmapbuf;	/* memory-mapped region pointer */
	size_t	mmapbuflen;	/* size of region */
	int	vlan_offset;	/* offset at which to insert vlan tags; if -1, don't insert */
	u_int	tp_version;	/* version of tpacket_hdr for mmaped ring */
	u_int	tp_hdrlen;	/* hdrlen of tpacket_hdr for mmaped ring */
	u_char	*oneshot_buffer; /* buffer for copy of packet */
	int	poll_timeout;	/* timeout to use in poll() */
#ifdef HAVE_TPACKET3
	unsigned char *current_packet; /* Current packet within the TPACKET_V3 block. Move to next block if NULL. */
	int packets_left; /* Unhandled packets left within the block from previous call to pcap_read_linux_mmap_v3 in case of TPACKET_V3. */
#endif
};

/*
 * Stuff to do when we close.
 */
#define MUST_CLEAR_PROMISC	0x00000001	/* clear promiscuous mode */
#define MUST_CLEAR_RFMON	0x00000002	/* clear rfmon (monitor) mode */
#define MUST_DELETE_MONIF	0x00000004	/* delete monitor-mode interface */

/*
 * Prototypes for internal functions and methods.
 */
static int get_if_flags(const char *, bpf_u_int32 *, char *);
static int is_wifi(int, const char *);
static void map_arphrd_to_dlt(pcap_t *, int, int, const char *, int);
#ifdef HAVE_PF_PACKET_SOCKETS
static short int map_packet_type_to_sll_type(short int);
#endif
static int pcap_activate_linux(pcap_t *);
static int activate_old(pcap_t *);
static int activate_new(pcap_t *);
static int activate_mmap(pcap_t *, int *);
static int pcap_can_set_rfmon_linux(pcap_t *);
static int pcap_read_linux(pcap_t *, int, pcap_handler, u_char *);
static int pcap_read_packet(pcap_t *, pcap_handler, u_char *);
static int pcap_inject_linux(pcap_t *, const void *, size_t);
static int pcap_stats_linux(pcap_t *, struct pcap_stat *);
static int pcap_setfilter_linux(pcap_t *, struct bpf_program *);
static int pcap_setdirection_linux(pcap_t *, pcap_direction_t);
static int pcap_set_datalink_linux(pcap_t *, int);
static void pcap_cleanup_linux(pcap_t *);

/*
 * This is what the header structure looks like in a 64-bit kernel;
 * we use this, rather than struct tpacket_hdr, if we're using
 * TPACKET_V1 in 32-bit code running on a 64-bit kernel.
 */
struct tpacket_hdr_64 {
	uint64_t	tp_status;
	unsigned int	tp_len;
	unsigned int	tp_snaplen;
	unsigned short	tp_mac;
	unsigned short	tp_net;
	unsigned int	tp_sec;
	unsigned int	tp_usec;
};

/*
 * We use this internally as the tpacket version for TPACKET_V1 in
 * 32-bit code on a 64-bit kernel.
 */
#define TPACKET_V1_64 99

union thdr {
	struct tpacket_hdr		*h1;
	struct tpacket_hdr_64		*h1_64;
#ifdef HAVE_TPACKET2
	struct tpacket2_hdr		*h2;
#endif
#ifdef HAVE_TPACKET3
	struct tpacket_block_desc	*h3;
#endif
	void				*raw;
};

#ifdef HAVE_PACKET_RING
#define RING_GET_FRAME_AT(h, offset) (((union thdr **)h->buffer)[(offset)])
#define RING_GET_CURRENT_FRAME(h) RING_GET_FRAME_AT(h, h->offset)

static void destroy_ring(pcap_t *handle);
static int create_ring(pcap_t *handle, int *status);
static int prepare_tpacket_socket(pcap_t *handle);
static void pcap_cleanup_linux_mmap(pcap_t *);
static int pcap_read_linux_mmap_v1(pcap_t *, int, pcap_handler , u_char *);
static int pcap_read_linux_mmap_v1_64(pcap_t *, int, pcap_handler , u_char *);
#ifdef HAVE_TPACKET2
static int pcap_read_linux_mmap_v2(pcap_t *, int, pcap_handler , u_char *);
#endif
#ifdef HAVE_TPACKET3
static int pcap_read_linux_mmap_v3(pcap_t *, int, pcap_handler , u_char *);
#endif
static int pcap_setfilter_linux_mmap(pcap_t *, struct bpf_program *);
static int pcap_setnonblock_mmap(pcap_t *p, int nonblock);
static int pcap_getnonblock_mmap(pcap_t *p);
static void pcap_oneshot_mmap(u_char *user, const struct pcap_pkthdr *h,
    const u_char *bytes);
#endif

/*
 * In pre-3.0 kernels, the tp_vlan_tci field is set to whatever the
 * vlan_tci field in the skbuff is.  0 can either mean "not on a VLAN"
 * or "on VLAN 0".  There is no flag set in the tp_status field to
 * distinguish between them.
 *
 * In 3.0 and later kernels, if there's a VLAN tag present, the tp_vlan_tci
 * field is set to the VLAN tag, and the TP_STATUS_VLAN_VALID flag is set
 * in the tp_status field, otherwise the tp_vlan_tci field is set to 0 and
 * the TP_STATUS_VLAN_VALID flag isn't set in the tp_status field.
 *
 * With a pre-3.0 kernel, we cannot distinguish between packets with no
 * VLAN tag and packets on VLAN 0, so we will mishandle some packets, and
 * there's nothing we can do about that.
 *
 * So, on those systems, which never set the TP_STATUS_VLAN_VALID flag, we
 * continue the behavior of earlier libpcaps, wherein we treated packets
 * with a VLAN tag of 0 as being packets without a VLAN tag rather than packets
 * on VLAN 0.  We do this by treating packets with a tp_vlan_tci of 0 and
 * with the TP_STATUS_VLAN_VALID flag not set in tp_status as not having
 * VLAN tags.  This does the right thing on 3.0 and later kernels, and
 * continues the old unfixably-imperfect behavior on pre-3.0 kernels.
 *
 * If TP_STATUS_VLAN_VALID isn't defined, we test it as the 0x10 bit; it
 * has that value in 3.0 and later kernels.
 */
#ifdef TP_STATUS_VLAN_VALID
  #define VLAN_VALID(hdr, hv)	((hv)->tp_vlan_tci != 0 || ((hdr)->tp_status & TP_STATUS_VLAN_VALID))
#else
  /*
   * This is being compiled on a system that lacks TP_STATUS_VLAN_VALID,
   * so we testwith the value it has in the 3.0 and later kernels, so
   * we can test it if we're running on a system that has it.  (If we're
   * running on a system that doesn't have it, it won't be set in the
   * tp_status field, so the tests of it will always fail; that means
   * we behave the way we did before we introduced this macro.)
   */
  #define VLAN_VALID(hdr, hv)	((hv)->tp_vlan_tci != 0 || ((hdr)->tp_status & 0x10))
#endif

#ifdef TP_STATUS_VLAN_TPID_VALID
# define VLAN_TPID(hdr, hv)	(((hv)->tp_vlan_tpid || ((hdr)->tp_status & TP_STATUS_VLAN_TPID_VALID)) ? (hv)->tp_vlan_tpid : ETH_P_8021Q)
#else
# define VLAN_TPID(hdr, hv)	ETH_P_8021Q
#endif

/*
 * Wrap some ioctl calls
 */
#ifdef HAVE_PF_PACKET_SOCKETS
static int	iface_get_id(int fd, const char *device, char *ebuf);
#endif /* HAVE_PF_PACKET_SOCKETS */
static int	iface_get_mtu(int fd, const char *device, char *ebuf);
static int 	iface_get_arptype(int fd, const char *device, char *ebuf);
#ifdef HAVE_PF_PACKET_SOCKETS
static int 	iface_bind(int fd, int ifindex, char *ebuf, int protocol);
#ifdef IW_MODE_MONITOR
static int	has_wext(int sock_fd, const char *device, char *ebuf);
#endif /* IW_MODE_MONITOR */
static int	enter_rfmon_mode(pcap_t *handle, int sock_fd,
    const char *device);
#endif /* HAVE_PF_PACKET_SOCKETS */
#if defined(HAVE_LINUX_NET_TSTAMP_H) && defined(PACKET_TIMESTAMP)
static int	iface_ethtool_get_ts_info(const char *device, pcap_t *handle,
    char *ebuf);
#endif
#ifdef HAVE_PACKET_RING
static int	iface_get_offload(pcap_t *handle);
#endif
static int 	iface_bind_old(int fd, const char *device, char *ebuf);

#ifdef SO_ATTACH_FILTER
static int	fix_program(pcap_t *handle, struct sock_fprog *fcode,
    int is_mapped);
static int	fix_offset(struct bpf_insn *p);
static int	set_kernel_filter(pcap_t *handle, struct sock_fprog *fcode);
static int	reset_kernel_filter(pcap_t *handle);

static struct sock_filter	total_insn
	= BPF_STMT(BPF_RET | BPF_K, 0);
static struct sock_fprog	total_fcode
	= { 1, &total_insn };
#endif /* SO_ATTACH_FILTER */

pcap_t *
pcap_create_interface(const char *device, char *ebuf)
{
	pcap_t *handle;

	handle = pcap_create_common(ebuf, sizeof (struct pcap_linux));
	if (handle == NULL)
		return NULL;

	handle->activate_op = pcap_activate_linux;
	handle->can_set_rfmon_op = pcap_can_set_rfmon_linux;

#if defined(HAVE_LINUX_NET_TSTAMP_H) && defined(PACKET_TIMESTAMP)
	/*
	 * See what time stamp types we support.
	 */
	if (iface_ethtool_get_ts_info(device, handle, ebuf) == -1) {
		pcap_close(handle);
		return NULL;
	}
#endif

#if defined(SIOCGSTAMPNS) && defined(SO_TIMESTAMPNS)
	/*
	 * We claim that we support microsecond and nanosecond time
	 * stamps.
	 *
	 * XXX - with adapter-supplied time stamps, can we choose
	 * microsecond or nanosecond time stamps on arbitrary
	 * adapters?
	 */
	handle->tstamp_precision_count = 2;
	handle->tstamp_precision_list = malloc(2 * sizeof(u_int));
	if (handle->tstamp_precision_list == NULL) {
		pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
		    errno, "malloc");
		pcap_close(handle);
		return NULL;
	}
	handle->tstamp_precision_list[0] = PCAP_TSTAMP_PRECISION_MICRO;
	handle->tstamp_precision_list[1] = PCAP_TSTAMP_PRECISION_NANO;
#endif /* defined(SIOCGSTAMPNS) && defined(SO_TIMESTAMPNS) */

	return handle;
}

#ifdef HAVE_LIBNL
/*
 * If interface {if} is a mac80211 driver, the file
 * /sys/class/net/{if}/phy80211 is a symlink to
 * /sys/class/ieee80211/{phydev}, for some {phydev}.
 *
 * On Fedora 9, with a 2.6.26.3-29 kernel, my Zydas stick, at
 * least, has a "wmaster0" device and a "wlan0" device; the
 * latter is the one with the IP address.  Both show up in
 * "tcpdump -D" output.  Capturing on the wmaster0 device
 * captures with 802.11 headers.
 *
 * airmon-ng searches through /sys/class/net for devices named
 * monN, starting with mon0; as soon as one *doesn't* exist,
 * it chooses that as the monitor device name.  If the "iw"
 * command exists, it does "iw dev {if} interface add {monif}
 * type monitor", where {monif} is the monitor device.  It
 * then (sigh) sleeps .1 second, and then configures the
 * device up.  Otherwise, if /sys/class/ieee80211/{phydev}/add_iface
 * is a file, it writes {mondev}, without a newline, to that file,
 * and again (sigh) sleeps .1 second, and then iwconfig's that
 * device into monitor mode and configures it up.  Otherwise,
 * you can't do monitor mode.
 *
 * All these devices are "glued" together by having the
 * /sys/class/net/{device}/phy80211 links pointing to the same
 * place, so, given a wmaster, wlan, or mon device, you can
 * find the other devices by looking for devices with
 * the same phy80211 link.
 *
 * To turn monitor mode off, delete the monitor interface,
 * either with "iw dev {monif} interface del" or by sending
 * {monif}, with no NL, down /sys/class/ieee80211/{phydev}/remove_iface
 *
 * Note: if you try to create a monitor device named "monN", and
 * there's already a "monN" device, it fails, as least with
 * the netlink interface (which is what iw uses), with a return
 * value of -ENFILE.  (Return values are negative errnos.)  We
 * could probably use that to find an unused device.
 *
 * Yes, you can have multiple monitor devices for a given
 * physical device.
 */

/*
 * Is this a mac80211 device?  If so, fill in the physical device path and
 * return 1; if not, return 0.  On an error, fill in handle->errbuf and
 * return PCAP_ERROR.
 */
static int
get_mac80211_phydev(pcap_t *handle, const char *device, char *phydev_path,
    size_t phydev_max_pathlen)
{
	char *pathstr;
	ssize_t bytes_read;

	/*
	 * Generate the path string for the symlink to the physical device.
	 */
	if (asprintf(&pathstr, "/sys/class/net/%s/phy80211", device) == -1) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
		    "%s: Can't generate path name string for /sys/class/net device",
		    device);
		return PCAP_ERROR;
	}
	bytes_read = readlink(pathstr, phydev_path, phydev_max_pathlen);
	if (bytes_read == -1) {
		if (errno == ENOENT || errno == EINVAL) {
			/*
			 * Doesn't exist, or not a symlink; assume that
			 * means it's not a mac80211 device.
			 */
			free(pathstr);
			return 0;
		}
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "%s: Can't readlink %s", device, pathstr);
		free(pathstr);
		return PCAP_ERROR;
	}
	free(pathstr);
	phydev_path[bytes_read] = '\0';
	return 1;
}

#ifdef HAVE_LIBNL_SOCKETS
#define get_nl_errmsg	nl_geterror
#else
/* libnl 2.x compatibility code */

#define nl_sock nl_handle

static inline struct nl_handle *
nl_socket_alloc(void)
{
	return nl_handle_alloc();
}

static inline void
nl_socket_free(struct nl_handle *h)
{
	nl_handle_destroy(h);
}

#define get_nl_errmsg	strerror

static inline int
__genl_ctrl_alloc_cache(struct nl_handle *h, struct nl_cache **cache)
{
	struct nl_cache *tmp = genl_ctrl_alloc_cache(h);
	if (!tmp)
		return -ENOMEM;
	*cache = tmp;
	return 0;
}
#define genl_ctrl_alloc_cache __genl_ctrl_alloc_cache
#endif /* !HAVE_LIBNL_SOCKETS */

struct nl80211_state {
	struct nl_sock *nl_sock;
	struct nl_cache *nl_cache;
	struct genl_family *nl80211;
};

static int
nl80211_init(pcap_t *handle, struct nl80211_state *state, const char *device)
{
	int err;

	state->nl_sock = nl_socket_alloc();
	if (!state->nl_sock) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
		    "%s: failed to allocate netlink handle", device);
		return PCAP_ERROR;
	}

	if (genl_connect(state->nl_sock)) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
		    "%s: failed to connect to generic netlink", device);
		goto out_handle_destroy;
	}

	err = genl_ctrl_alloc_cache(state->nl_sock, &state->nl_cache);
	if (err < 0) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
		    "%s: failed to allocate generic netlink cache: %s",
		    device, get_nl_errmsg(-err));
		goto out_handle_destroy;
	}

	state->nl80211 = genl_ctrl_search_by_name(state->nl_cache, "nl80211");
	if (!state->nl80211) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
		    "%s: nl80211 not found", device);
		goto out_cache_free;
	}

	return 0;

out_cache_free:
	nl_cache_free(state->nl_cache);
out_handle_destroy:
	nl_socket_free(state->nl_sock);
	return PCAP_ERROR;
}

static void
nl80211_cleanup(struct nl80211_state *state)
{
	genl_family_put(state->nl80211);
	nl_cache_free(state->nl_cache);
	nl_socket_free(state->nl_sock);
}

static int
del_mon_if(pcap_t *handle, int sock_fd, struct nl80211_state *state,
    const char *device, const char *mondevice);

static int
add_mon_if(pcap_t *handle, int sock_fd, struct nl80211_state *state,
    const char *device, const char *mondevice)
{
	struct pcap_linux *handlep = handle->priv;
	int ifindex;
	struct nl_msg *msg;
	int err;

	ifindex = iface_get_id(sock_fd, device, handle->errbuf);
	if (ifindex == -1)
		return PCAP_ERROR;

	msg = nlmsg_alloc();
	if (!msg) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
		    "%s: failed to allocate netlink msg", device);
		return PCAP_ERROR;
	}

	genlmsg_put(msg, 0, 0, genl_family_get_id(state->nl80211), 0,
		    0, NL80211_CMD_NEW_INTERFACE, 0);
	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, ifindex);
	NLA_PUT_STRING(msg, NL80211_ATTR_IFNAME, mondevice);
	NLA_PUT_U32(msg, NL80211_ATTR_IFTYPE, NL80211_IFTYPE_MONITOR);

	err = nl_send_auto_complete(state->nl_sock, msg);
	if (err < 0) {
#if defined HAVE_LIBNL_NLE
		if (err == -NLE_FAILURE) {
#else
		if (err == -ENFILE) {
#endif
			/*
			 * Device not available; our caller should just
			 * keep trying.  (libnl 2.x maps ENFILE to
			 * NLE_FAILURE; it can also map other errors
			 * to that, but there's not much we can do
			 * about that.)
			 */
			nlmsg_free(msg);
			return 0;
		} else {
			/*
			 * Real failure, not just "that device is not
			 * available.
			 */
			pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
			    "%s: nl_send_auto_complete failed adding %s interface: %s",
			    device, mondevice, get_nl_errmsg(-err));
			nlmsg_free(msg);
			return PCAP_ERROR;
		}
	}
	err = nl_wait_for_ack(state->nl_sock);
	if (err < 0) {
#if defined HAVE_LIBNL_NLE
		if (err == -NLE_FAILURE) {
#else
		if (err == -ENFILE) {
#endif
			/*
			 * Device not available; our caller should just
			 * keep trying.  (libnl 2.x maps ENFILE to
			 * NLE_FAILURE; it can also map other errors
			 * to that, but there's not much we can do
			 * about that.)
			 */
			nlmsg_free(msg);
			return 0;
		} else {
			/*
			 * Real failure, not just "that device is not
			 * available.
			 */
			pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
			    "%s: nl_wait_for_ack failed adding %s interface: %s",
			    device, mondevice, get_nl_errmsg(-err));
			nlmsg_free(msg);
			return PCAP_ERROR;
		}
	}

	/*
	 * Success.
	 */
	nlmsg_free(msg);

	/*
	 * Try to remember the monitor device.
	 */
	handlep->mondevice = strdup(mondevice);
	if (handlep->mondevice == NULL) {
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "strdup");
		/*
		 * Get rid of the monitor device.
		 */
		del_mon_if(handle, sock_fd, state, device, mondevice);
		return PCAP_ERROR;
	}
	return 1;

nla_put_failure:
	pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
	    "%s: nl_put failed adding %s interface",
	    device, mondevice);
	nlmsg_free(msg);
	return PCAP_ERROR;
}

static int
del_mon_if(pcap_t *handle, int sock_fd, struct nl80211_state *state,
    const char *device, const char *mondevice)
{
	int ifindex;
	struct nl_msg *msg;
	int err;

	ifindex = iface_get_id(sock_fd, mondevice, handle->errbuf);
	if (ifindex == -1)
		return PCAP_ERROR;

	msg = nlmsg_alloc();
	if (!msg) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
		    "%s: failed to allocate netlink msg", device);
		return PCAP_ERROR;
	}

	genlmsg_put(msg, 0, 0, genl_family_get_id(state->nl80211), 0,
		    0, NL80211_CMD_DEL_INTERFACE, 0);
	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, ifindex);

	err = nl_send_auto_complete(state->nl_sock, msg);
	if (err < 0) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
		    "%s: nl_send_auto_complete failed deleting %s interface: %s",
		    device, mondevice, get_nl_errmsg(-err));
		nlmsg_free(msg);
		return PCAP_ERROR;
	}
	err = nl_wait_for_ack(state->nl_sock);
	if (err < 0) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
		    "%s: nl_wait_for_ack failed adding %s interface: %s",
		    device, mondevice, get_nl_errmsg(-err));
		nlmsg_free(msg);
		return PCAP_ERROR;
	}

	/*
	 * Success.
	 */
	nlmsg_free(msg);
	return 1;

nla_put_failure:
	pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
	    "%s: nl_put failed deleting %s interface",
	    device, mondevice);
	nlmsg_free(msg);
	return PCAP_ERROR;
}

static int
enter_rfmon_mode_mac80211(pcap_t *handle, int sock_fd, const char *device)
{
	struct pcap_linux *handlep = handle->priv;
	int ret;
	char phydev_path[PATH_MAX+1];
	struct nl80211_state nlstate;
	struct ifreq ifr;
	u_int n;

	/*
	 * Is this a mac80211 device?
	 */
	ret = get_mac80211_phydev(handle, device, phydev_path, PATH_MAX);
	if (ret < 0)
		return ret;	/* error */
	if (ret == 0)
		return 0;	/* no error, but not mac80211 device */

	/*
	 * XXX - is this already a monN device?
	 * If so, we're done.
	 * Is that determined by old Wireless Extensions ioctls?
	 */

	/*
	 * OK, it's apparently a mac80211 device.
	 * Try to find an unused monN device for it.
	 */
	ret = nl80211_init(handle, &nlstate, device);
	if (ret != 0)
		return ret;
	for (n = 0; n < UINT_MAX; n++) {
		/*
		 * Try mon{n}.
		 */
		char mondevice[3+10+1];	/* mon{UINT_MAX}\0 */

		pcap_snprintf(mondevice, sizeof mondevice, "mon%u", n);
		ret = add_mon_if(handle, sock_fd, &nlstate, device, mondevice);
		if (ret == 1) {
			/*
			 * Success.  We don't clean up the libnl state
			 * yet, as we'll be using it later.
			 */
			goto added;
		}
		if (ret < 0) {
			/*
			 * Hard failure.  Just return ret; handle->errbuf
			 * has already been set.
			 */
			nl80211_cleanup(&nlstate);
			return ret;
		}
	}

	pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
	    "%s: No free monN interfaces", device);
	nl80211_cleanup(&nlstate);
	return PCAP_ERROR;

added:

#if 0
	/*
	 * Sleep for .1 seconds.
	 */
	delay.tv_sec = 0;
	delay.tv_nsec = 500000000;
	nanosleep(&delay, NULL);
#endif

	/*
	 * If we haven't already done so, arrange to have
	 * "pcap_close_all()" called when we exit.
	 */
	if (!pcap_do_addexit(handle)) {
		/*
		 * "atexit()" failed; don't put the interface
		 * in rfmon mode, just give up.
		 */
		del_mon_if(handle, sock_fd, &nlstate, device,
		    handlep->mondevice);
		nl80211_cleanup(&nlstate);
		return PCAP_ERROR;
	}

	/*
	 * Now configure the monitor interface up.
	 */
	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, handlep->mondevice, sizeof(ifr.ifr_name));
	if (ioctl(sock_fd, SIOCGIFFLAGS, &ifr) == -1) {
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "%s: Can't get flags for %s", device,
		    handlep->mondevice);
		del_mon_if(handle, sock_fd, &nlstate, device,
		    handlep->mondevice);
		nl80211_cleanup(&nlstate);
		return PCAP_ERROR;
	}
	ifr.ifr_flags |= IFF_UP|IFF_RUNNING;
	if (ioctl(sock_fd, SIOCSIFFLAGS, &ifr) == -1) {
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "%s: Can't set flags for %s", device,
		    handlep->mondevice);
		del_mon_if(handle, sock_fd, &nlstate, device,
		    handlep->mondevice);
		nl80211_cleanup(&nlstate);
		return PCAP_ERROR;
	}

	/*
	 * Success.  Clean up the libnl state.
	 */
	nl80211_cleanup(&nlstate);

	/*
	 * Note that we have to delete the monitor device when we close
	 * the handle.
	 */
	handlep->must_do_on_close |= MUST_DELETE_MONIF;

	/*
	 * Add this to the list of pcaps to close when we exit.
	 */
	pcap_add_to_pcaps_to_close(handle);

	return 1;
}
#endif /* HAVE_LIBNL */

#ifdef IW_MODE_MONITOR
/*
 * Bonding devices mishandle unknown ioctls; they fail with ENODEV
 * rather than ENOTSUP, EOPNOTSUPP, or ENOTTY, so Wireless Extensions
 * will fail with ENODEV if we try to do them on a bonding device,
 * making us return a "no such device" indication rather than just
 * saying "no Wireless Extensions".
 *
 * So we check for bonding devices, if we can, before trying those
 * ioctls, by trying a bonding device information query ioctl to see
 * whether it succeeds.
 */
static int
is_bonding_device(int fd, const char *device)
{
#ifdef BOND_INFO_QUERY_IOCTL
	struct ifreq ifr;
	ifbond ifb;

	memset(&ifr, 0, sizeof ifr);
	strlcpy(ifr.ifr_name, device, sizeof ifr.ifr_name);
	memset(&ifb, 0, sizeof ifb);
	ifr.ifr_data = (caddr_t)&ifb;
	if (ioctl(fd, BOND_INFO_QUERY_IOCTL, &ifr) == 0)
		return 1;	/* success, so it's a bonding device */
#endif /* BOND_INFO_QUERY_IOCTL */

	return 0;	/* no, it's not a bonding device */
}
#endif /* IW_MODE_MONITOR */

static int pcap_protocol(pcap_t *handle)
{
	int protocol;

	protocol = handle->opt.protocol;
	if (protocol == 0)
		protocol = ETH_P_ALL;

	return htons(protocol);
}

static int
pcap_can_set_rfmon_linux(pcap_t *handle)
{
#ifdef HAVE_LIBNL
	char phydev_path[PATH_MAX+1];
	int ret;
#endif
#ifdef IW_MODE_MONITOR
	int sock_fd;
	struct iwreq ireq;
#endif

	if (strcmp(handle->opt.device, "any") == 0) {
		/*
		 * Monitor mode makes no sense on the "any" device.
		 */
		return 0;
	}

#ifdef HAVE_LIBNL
	/*
	 * Bleah.  There doesn't seem to be a way to ask a mac80211
	 * device, through libnl, whether it supports monitor mode;
	 * we'll just check whether the device appears to be a
	 * mac80211 device and, if so, assume the device supports
	 * monitor mode.
	 *
	 * wmaster devices don't appear to support the Wireless
	 * Extensions, but we can create a mon device for a
	 * wmaster device, so we don't bother checking whether
	 * a mac80211 device supports the Wireless Extensions.
	 */
	ret = get_mac80211_phydev(handle, handle->opt.device, phydev_path,
	    PATH_MAX);
	if (ret < 0)
		return ret;	/* error */
	if (ret == 1)
		return 1;	/* mac80211 device */
#endif

#ifdef IW_MODE_MONITOR
	/*
	 * Bleah.  There doesn't appear to be an ioctl to use to ask
	 * whether a device supports monitor mode; we'll just do
	 * SIOCGIWMODE and, if it succeeds, assume the device supports
	 * monitor mode.
	 *
	 * Open a socket on which to attempt to get the mode.
	 * (We assume that if we have Wireless Extensions support
	 * we also have PF_PACKET support.)
	 */
	sock_fd = socket(PF_PACKET, SOCK_RAW, pcap_protocol(handle));
	if (sock_fd == -1) {
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "socket");
		return PCAP_ERROR;
	}

	if (is_bonding_device(sock_fd, handle->opt.device)) {
		/* It's a bonding device, so don't even try. */
		close(sock_fd);
		return 0;
	}

	/*
	 * Attempt to get the current mode.
	 */
	strlcpy(ireq.ifr_ifrn.ifrn_name, handle->opt.device,
	    sizeof ireq.ifr_ifrn.ifrn_name);
	if (ioctl(sock_fd, SIOCGIWMODE, &ireq) != -1) {
		/*
		 * Well, we got the mode; assume we can set it.
		 */
		close(sock_fd);
		return 1;
	}
	if (errno == ENODEV) {
		/* The device doesn't even exist. */
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "SIOCGIWMODE failed");
		close(sock_fd);
		return PCAP_ERROR_NO_SUCH_DEVICE;
	}
	close(sock_fd);
#endif
	return 0;
}

/*
 * Grabs the number of dropped packets by the interface from /proc/net/dev.
 *
 * XXX - what about /sys/class/net/{interface name}/rx_*?  There are
 * individual devices giving, in ASCII, various rx_ and tx_ statistics.
 *
 * Or can we get them in binary form from netlink?
 */
static long int
linux_if_drops(const char * if_name)
{
	char buffer[512];
	char * bufptr;
	FILE * file;
	int field_to_convert = 3, if_name_sz = strlen(if_name);
	long int dropped_pkts = 0;

	file = fopen("/proc/net/dev", "r");
	if (!file)
		return 0;

	while (!dropped_pkts && fgets( buffer, sizeof(buffer), file ))
	{
		/* 	search for 'bytes' -- if its in there, then
			that means we need to grab the fourth field. otherwise
			grab the third field. */
		if (field_to_convert != 4 && strstr(buffer, "bytes"))
		{
			field_to_convert = 4;
			continue;
		}

		/* find iface and make sure it actually matches -- space before the name and : after it */
		if ((bufptr = strstr(buffer, if_name)) &&
			(bufptr == buffer || *(bufptr-1) == ' ') &&
			*(bufptr + if_name_sz) == ':')
		{
			bufptr = bufptr + if_name_sz + 1;

			/* grab the nth field from it */
			while( --field_to_convert && *bufptr != '\0')
			{
				while (*bufptr != '\0' && *(bufptr++) == ' ');
				while (*bufptr != '\0' && *(bufptr++) != ' ');
			}

			/* get rid of any final spaces */
			while (*bufptr != '\0' && *bufptr == ' ') bufptr++;

			if (*bufptr != '\0')
				dropped_pkts = strtol(bufptr, NULL, 10);

			break;
		}
	}

	fclose(file);
	return dropped_pkts;
}


/*
 * With older kernels promiscuous mode is kind of interesting because we
 * have to reset the interface before exiting. The problem can't really
 * be solved without some daemon taking care of managing usage counts.
 * If we put the interface into promiscuous mode, we set a flag indicating
 * that we must take it out of that mode when the interface is closed,
 * and, when closing the interface, if that flag is set we take it out
 * of promiscuous mode.
 *
 * Even with newer kernels, we have the same issue with rfmon mode.
 */

static void	pcap_cleanup_linux( pcap_t *handle )
{
	struct pcap_linux *handlep = handle->priv;
	struct ifreq	ifr;
#ifdef HAVE_LIBNL
	struct nl80211_state nlstate;
	int ret;
#endif /* HAVE_LIBNL */
#ifdef IW_MODE_MONITOR
	int oldflags;
	struct iwreq ireq;
#endif /* IW_MODE_MONITOR */

	if (handlep->must_do_on_close != 0) {
		/*
		 * There's something we have to do when closing this
		 * pcap_t.
		 */
		if (handlep->must_do_on_close & MUST_CLEAR_PROMISC) {
			/*
			 * We put the interface into promiscuous mode;
			 * take it out of promiscuous mode.
			 *
			 * XXX - if somebody else wants it in promiscuous
			 * mode, this code cannot know that, so it'll take
			 * it out of promiscuous mode.  That's not fixable
			 * in 2.0[.x] kernels.
			 */
			memset(&ifr, 0, sizeof(ifr));
			strlcpy(ifr.ifr_name, handlep->device,
			    sizeof(ifr.ifr_name));
			if (ioctl(handle->fd, SIOCGIFFLAGS, &ifr) == -1) {
				fprintf(stderr,
				    "Can't restore interface %s flags (SIOCGIFFLAGS failed: %s).\n"
				    "Please adjust manually.\n"
				    "Hint: This can't happen with Linux >= 2.2.0.\n",
				    handlep->device, strerror(errno));
			} else {
				if (ifr.ifr_flags & IFF_PROMISC) {
					/*
					 * Promiscuous mode is currently on;
					 * turn it off.
					 */
					ifr.ifr_flags &= ~IFF_PROMISC;
					if (ioctl(handle->fd, SIOCSIFFLAGS,
					    &ifr) == -1) {
						fprintf(stderr,
						    "Can't restore interface %s flags (SIOCSIFFLAGS failed: %s).\n"
						    "Please adjust manually.\n"
						    "Hint: This can't happen with Linux >= 2.2.0.\n",
						    handlep->device,
						    strerror(errno));
					}
				}
			}
		}

#ifdef HAVE_LIBNL
		if (handlep->must_do_on_close & MUST_DELETE_MONIF) {
			ret = nl80211_init(handle, &nlstate, handlep->device);
			if (ret >= 0) {
				ret = del_mon_if(handle, handle->fd, &nlstate,
				    handlep->device, handlep->mondevice);
				nl80211_cleanup(&nlstate);
			}
			if (ret < 0) {
				fprintf(stderr,
				    "Can't delete monitor interface %s (%s).\n"
				    "Please delete manually.\n",
				    handlep->mondevice, handle->errbuf);
			}
		}
#endif /* HAVE_LIBNL */

#ifdef IW_MODE_MONITOR
		if (handlep->must_do_on_close & MUST_CLEAR_RFMON) {
			/*
			 * We put the interface into rfmon mode;
			 * take it out of rfmon mode.
			 *
			 * XXX - if somebody else wants it in rfmon
			 * mode, this code cannot know that, so it'll take
			 * it out of rfmon mode.
			 */

			/*
			 * First, take the interface down if it's up;
			 * otherwise, we might get EBUSY.
			 * If we get errors, just drive on and print
			 * a warning if we can't restore the mode.
			 */
			oldflags = 0;
			memset(&ifr, 0, sizeof(ifr));
			strlcpy(ifr.ifr_name, handlep->device,
			    sizeof(ifr.ifr_name));
			if (ioctl(handle->fd, SIOCGIFFLAGS, &ifr) != -1) {
				if (ifr.ifr_flags & IFF_UP) {
					oldflags = ifr.ifr_flags;
					ifr.ifr_flags &= ~IFF_UP;
					if (ioctl(handle->fd, SIOCSIFFLAGS, &ifr) == -1)
						oldflags = 0;	/* didn't set, don't restore */
				}
			}

			/*
			 * Now restore the mode.
			 */
			strlcpy(ireq.ifr_ifrn.ifrn_name, handlep->device,
			    sizeof ireq.ifr_ifrn.ifrn_name);
			ireq.u.mode = handlep->oldmode;
			if (ioctl(handle->fd, SIOCSIWMODE, &ireq) == -1) {
				/*
				 * Scientist, you've failed.
				 */
				fprintf(stderr,
				    "Can't restore interface %s wireless mode (SIOCSIWMODE failed: %s).\n"
				    "Please adjust manually.\n",
				    handlep->device, strerror(errno));
			}

			/*
			 * Now bring the interface back up if we brought
			 * it down.
			 */
			if (oldflags != 0) {
				ifr.ifr_flags = oldflags;
				if (ioctl(handle->fd, SIOCSIFFLAGS, &ifr) == -1) {
					fprintf(stderr,
					    "Can't bring interface %s back up (SIOCSIFFLAGS failed: %s).\n"
					    "Please adjust manually.\n",
					    handlep->device, strerror(errno));
				}
			}
		}
#endif /* IW_MODE_MONITOR */

		/*
		 * Take this pcap out of the list of pcaps for which we
		 * have to take the interface out of some mode.
		 */
		pcap_remove_from_pcaps_to_close(handle);
	}

	if (handlep->mondevice != NULL) {
		free(handlep->mondevice);
		handlep->mondevice = NULL;
	}
	if (handlep->device != NULL) {
		free(handlep->device);
		handlep->device = NULL;
	}
	pcap_cleanup_live_common(handle);
}

/*
 * Set the timeout to be used in poll() with memory-mapped packet capture.
 */
static void
set_poll_timeout(struct pcap_linux *handlep)
{
#ifdef HAVE_TPACKET3
	struct utsname utsname;
	char *version_component, *endp;
	int major, minor;
	int broken_tpacket_v3 = 1;

	/*
	 * Some versions of TPACKET_V3 have annoying bugs/misfeatures
	 * around which we have to work.  Determine if we have those
	 * problems or not.
	 */
	if (uname(&utsname) == 0) {
		/*
		 * 3.19 is the first release with a fixed version of
		 * TPACKET_V3.  We treat anything before that as
		 * not haveing a fixed version; that may really mean
		 * it has *no* version.
		 */
		version_component = utsname.release;
		major = strtol(version_component, &endp, 10);
		if (endp != version_component && *endp == '.') {
			/*
			 * OK, that was a valid major version.
			 * Get the minor version.
			 */
			version_component = endp + 1;
			minor = strtol(version_component, &endp, 10);
			if (endp != version_component &&
			    (*endp == '.' || *endp == '\0')) {
				/*
				 * OK, that was a valid minor version.
				 * Is this 3.19 or newer?
				 */
				if (major >= 4 || (major == 3 && minor >= 19)) {
					/* Yes. TPACKET_V3 works correctly. */
					broken_tpacket_v3 = 0;
				}
			}
		}
	}
#endif
	if (handlep->timeout == 0) {
#ifdef HAVE_TPACKET3
		/*
		 * XXX - due to a set of (mis)features in the TPACKET_V3
		 * kernel code prior to the 3.19 kernel, blocking forever
		 * with a TPACKET_V3 socket can, if few packets are
		 * arriving and passing the socket filter, cause most
		 * packets to be dropped.  See libpcap issue #335 for the
		 * full painful story.
		 *
		 * The workaround is to have poll() time out very quickly,
		 * so we grab the frames handed to us, and return them to
		 * the kernel, ASAP.
		 */
		if (handlep->tp_version == TPACKET_V3 && broken_tpacket_v3)
			handlep->poll_timeout = 1;	/* don't block for very long */
		else
#endif
			handlep->poll_timeout = -1;	/* block forever */
	} else if (handlep->timeout > 0) {
#ifdef HAVE_TPACKET3
		/*
		 * For TPACKET_V3, the timeout is handled by the kernel,
		 * so block forever; that way, we don't get extra timeouts.
		 * Don't do that if we have a broken TPACKET_V3, though.
		 */
		if (handlep->tp_version == TPACKET_V3 && !broken_tpacket_v3)
			handlep->poll_timeout = -1;	/* block forever, let TPACKET_V3 wake us up */
		else
#endif
			handlep->poll_timeout = handlep->timeout;	/* block for that amount of time */
	} else {
		/*
		 * Non-blocking mode; we call poll() to pick up error
		 * indications, but we don't want it to wait for
		 * anything.
		 */
		handlep->poll_timeout = 0;
	}
}

/*
 *  Get a handle for a live capture from the given device. You can
 *  pass NULL as device to get all packages (without link level
 *  information of course). If you pass 1 as promisc the interface
 *  will be set to promiscous mode (XXX: I think this usage should
 *  be deprecated and functions be added to select that later allow
 *  modification of that values -- Torsten).
 */
static int
pcap_activate_linux(pcap_t *handle)
{
	struct pcap_linux *handlep = handle->priv;
	const char	*device;
	struct ifreq	ifr;
	int		status = 0;
	int		ret;

	device = handle->opt.device;

	/*
	 * Make sure the name we were handed will fit into the ioctls we
	 * might perform on the device; if not, return a "No such device"
	 * indication, as the Linux kernel shouldn't support creating
	 * a device whose name won't fit into those ioctls.
	 *
	 * "Will fit" means "will fit, complete with a null terminator",
	 * so if the length, which does *not* include the null terminator,
	 * is greater than *or equal to* the size of the field into which
	 * we'll be copying it, that won't fit.
	 */
	if (strlen(device) >= sizeof(ifr.ifr_name)) {
		status = PCAP_ERROR_NO_SUCH_DEVICE;
		goto fail;
	}

	/*
	 * Turn a negative snapshot value (invalid), a snapshot value of
	 * 0 (unspecified), or a value bigger than the normal maximum
	 * value, into the maximum allowed value.
	 *
	 * If some application really *needs* a bigger snapshot
	 * length, we should just increase MAXIMUM_SNAPLEN.
	 */
	if (handle->snapshot <= 0 || handle->snapshot > MAXIMUM_SNAPLEN)
		handle->snapshot = MAXIMUM_SNAPLEN;

	handle->inject_op = pcap_inject_linux;
	handle->setfilter_op = pcap_setfilter_linux;
	handle->setdirection_op = pcap_setdirection_linux;
	handle->set_datalink_op = pcap_set_datalink_linux;
	handle->getnonblock_op = pcap_getnonblock_fd;
	handle->setnonblock_op = pcap_setnonblock_fd;
	handle->cleanup_op = pcap_cleanup_linux;
	handle->read_op = pcap_read_linux;
	handle->stats_op = pcap_stats_linux;

	/*
	 * The "any" device is a special device which causes us not
	 * to bind to a particular device and thus to look at all
	 * devices.
	 */
	if (strcmp(device, "any") == 0) {
		if (handle->opt.promisc) {
			handle->opt.promisc = 0;
			/* Just a warning. */
			pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
			    "Promiscuous mode not supported on the \"any\" device");
			status = PCAP_WARNING_PROMISC_NOTSUP;
		}
	}

	handlep->device	= strdup(device);
	if (handlep->device == NULL) {
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "strdup");
		return PCAP_ERROR;
	}

	/* copy timeout value */
	handlep->timeout = handle->opt.timeout;

	/*
	 * If we're in promiscuous mode, then we probably want
	 * to see when the interface drops packets too, so get an
	 * initial count from /proc/net/dev
	 */
	if (handle->opt.promisc)
		handlep->proc_dropped = linux_if_drops(handlep->device);

	/*
	 * Current Linux kernels use the protocol family PF_PACKET to
	 * allow direct access to all packets on the network while
	 * older kernels had a special socket type SOCK_PACKET to
	 * implement this feature.
	 * While this old implementation is kind of obsolete we need
	 * to be compatible with older kernels for a while so we are
	 * trying both methods with the newer method preferred.
	 */
	ret = activate_new(handle);
	if (ret < 0) {
		/*
		 * Fatal error with the new way; just fail.
		 * ret has the error return; if it's PCAP_ERROR,
		 * handle->errbuf has been set appropriately.
		 */
		status = ret;
		goto fail;
	}
	if (ret == 1) {
		/*
		 * Success.
		 * Try to use memory-mapped access.
		 */
		switch (activate_mmap(handle, &status)) {

		case 1:
			/*
			 * We succeeded.  status has been
			 * set to the status to return,
			 * which might be 0, or might be
			 * a PCAP_WARNING_ value.
			 *
			 * Set the timeout to use in poll() before
			 * returning.
			 */
			set_poll_timeout(handlep);
			return status;

		case 0:
			/*
			 * Kernel doesn't support it - just continue
			 * with non-memory-mapped access.
			 */
			break;

		case -1:
			/*
			 * We failed to set up to use it, or the kernel
			 * supports it, but we failed to enable it.
			 * ret has been set to the error status to
			 * return and, if it's PCAP_ERROR, handle->errbuf
			 * contains the error message.
			 */
			status = ret;
			goto fail;
		}
	}
	else if (ret == 0) {
		/* Non-fatal error; try old way */
		if ((ret = activate_old(handle)) != 1) {
			/*
			 * Both methods to open the packet socket failed.
			 * Tidy up and report our failure (handle->errbuf
			 * is expected to be set by the functions above).
			 */
			status = ret;
			goto fail;
		}
	}

	/*
	 * We set up the socket, but not with memory-mapped access.
	 */
	if (handle->opt.buffer_size != 0) {
		/*
		 * Set the socket buffer size to the specified value.
		 */
		if (setsockopt(handle->fd, SOL_SOCKET, SO_RCVBUF,
		    &handle->opt.buffer_size,
		    sizeof(handle->opt.buffer_size)) == -1) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "SO_RCVBUF");
			status = PCAP_ERROR;
			goto fail;
		}
	}

	/* Allocate the buffer */

	handle->buffer	 = malloc(handle->bufsize + handle->offset);
	if (!handle->buffer) {
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "malloc");
		status = PCAP_ERROR;
		goto fail;
	}

	/*
	 * "handle->fd" is a socket, so "select()" and "poll()"
	 * should work on it.
	 */
	handle->selectable_fd = handle->fd;

	return status;

fail:
	pcap_cleanup_linux(handle);
	return status;
}

/*
 *  Read at most max_packets from the capture stream and call the callback
 *  for each of them. Returns the number of packets handled or -1 if an
 *  error occured.
 */
static int
pcap_read_linux(pcap_t *handle, int max_packets _U_, pcap_handler callback, u_char *user)
{
	/*
	 * Currently, on Linux only one packet is delivered per read,
	 * so we don't loop.
	 */
	return pcap_read_packet(handle, callback, user);
}

static int
pcap_set_datalink_linux(pcap_t *handle, int dlt)
{
	handle->linktype = dlt;
	return 0;
}

/*
 * linux_check_direction()
 *
 * Do checks based on packet direction.
 */
static inline int
linux_check_direction(const pcap_t *handle, const struct sockaddr_ll *sll)
{
	struct pcap_linux	*handlep = handle->priv;

	if (sll->sll_pkttype == PACKET_OUTGOING) {
		/*
		 * Outgoing packet.
		 * If this is from the loopback device, reject it;
		 * we'll see the packet as an incoming packet as well,
		 * and we don't want to see it twice.
		 */
		if (sll->sll_ifindex == handlep->lo_ifindex)
			return 0;

		/*
		 * If this is an outgoing CAN or CAN FD frame, and
		 * the user doesn't only want outgoing packets,
		 * reject it; CAN devices and drivers, and the CAN
		 * stack, always arrange to loop back transmitted
		 * packets, so they also appear as incoming packets.
		 * We don't want duplicate packets, and we can't
		 * easily distinguish packets looped back by the CAN
		 * layer than those received by the CAN layer, so we
		 * eliminate this packet instead.
		 */
		if ((sll->sll_protocol == LINUX_SLL_P_CAN ||
		     sll->sll_protocol == LINUX_SLL_P_CANFD) &&
		     handle->direction != PCAP_D_OUT)
			return 0;

		/*
		 * If the user only wants incoming packets, reject it.
		 */
		if (handle->direction == PCAP_D_IN)
			return 0;
	} else {
		/*
		 * Incoming packet.
		 * If the user only wants outgoing packets, reject it.
		 */
		if (handle->direction == PCAP_D_OUT)
			return 0;
	}
	return 1;
}

/*
 *  Read a packet from the socket calling the handler provided by
 *  the user. Returns the number of packets received or -1 if an
 *  error occured.
 */
static int
pcap_read_packet(pcap_t *handle, pcap_handler callback, u_char *userdata)
{
	struct pcap_linux	*handlep = handle->priv;
	u_char			*bp;
	int			offset;
#ifdef HAVE_PF_PACKET_SOCKETS
	struct sockaddr_ll	from;
	struct sll_header	*hdrp;
#else
	struct sockaddr		from;
#endif
#if defined(HAVE_PACKET_AUXDATA) && defined(HAVE_STRUCT_TPACKET_AUXDATA_TP_VLAN_TCI)
	struct iovec		iov;
	struct msghdr		msg;
	struct cmsghdr		*cmsg;
	union {
		struct cmsghdr	cmsg;
		char		buf[CMSG_SPACE(sizeof(struct tpacket_auxdata))];
	} cmsg_buf;
#else /* defined(HAVE_PACKET_AUXDATA) && defined(HAVE_STRUCT_TPACKET_AUXDATA_TP_VLAN_TCI) */
	socklen_t		fromlen;
#endif /* defined(HAVE_PACKET_AUXDATA) && defined(HAVE_STRUCT_TPACKET_AUXDATA_TP_VLAN_TCI) */
	int			packet_len, caplen;
	struct pcap_pkthdr	pcap_header;

        struct bpf_aux_data     aux_data;
#ifdef HAVE_PF_PACKET_SOCKETS
	/*
	 * If this is a cooked device, leave extra room for a
	 * fake packet header.
	 */
	if (handlep->cooked)
		offset = SLL_HDR_LEN;
	else
		offset = 0;
#else
	/*
	 * This system doesn't have PF_PACKET sockets, so it doesn't
	 * support cooked devices.
	 */
	offset = 0;
#endif

	/*
	 * Receive a single packet from the kernel.
	 * We ignore EINTR, as that might just be due to a signal
	 * being delivered - if the signal should interrupt the
	 * loop, the signal handler should call pcap_breakloop()
	 * to set handle->break_loop (we ignore it on other
	 * platforms as well).
	 * We also ignore ENETDOWN, so that we can continue to
	 * capture traffic if the interface goes down and comes
	 * back up again; comments in the kernel indicate that
	 * we'll just block waiting for packets if we try to
	 * receive from a socket that delivered ENETDOWN, and,
	 * if we're using a memory-mapped buffer, we won't even
	 * get notified of "network down" events.
	 */
	bp = (u_char *)handle->buffer + handle->offset;

#if defined(HAVE_PACKET_AUXDATA) && defined(HAVE_STRUCT_TPACKET_AUXDATA_TP_VLAN_TCI)
	msg.msg_name		= &from;
	msg.msg_namelen		= sizeof(from);
	msg.msg_iov		= &iov;
	msg.msg_iovlen		= 1;
	msg.msg_control		= &cmsg_buf;
	msg.msg_controllen	= sizeof(cmsg_buf);
	msg.msg_flags		= 0;

	iov.iov_len		= handle->bufsize - offset;
	iov.iov_base		= bp + offset;
#endif /* defined(HAVE_PACKET_AUXDATA) && defined(HAVE_STRUCT_TPACKET_AUXDATA_TP_VLAN_TCI) */

	do {
		/*
		 * Has "pcap_breakloop()" been called?
		 */
		if (handle->break_loop) {
			/*
			 * Yes - clear the flag that indicates that it has,
			 * and return PCAP_ERROR_BREAK as an indication that
			 * we were told to break out of the loop.
			 */
			handle->break_loop = 0;
			return PCAP_ERROR_BREAK;
		}

#if defined(HAVE_PACKET_AUXDATA) && defined(HAVE_STRUCT_TPACKET_AUXDATA_TP_VLAN_TCI)
		packet_len = recvmsg(handle->fd, &msg, MSG_TRUNC);
#else /* defined(HAVE_PACKET_AUXDATA) && defined(HAVE_STRUCT_TPACKET_AUXDATA_TP_VLAN_TCI) */
		fromlen = sizeof(from);
		packet_len = recvfrom(
			handle->fd, bp + offset,
			handle->bufsize - offset, MSG_TRUNC,
			(struct sockaddr *) &from, &fromlen);
#endif /* defined(HAVE_PACKET_AUXDATA) && defined(HAVE_STRUCT_TPACKET_AUXDATA_TP_VLAN_TCI) */
	} while (packet_len == -1 && errno == EINTR);

	/* Check if an error occured */

	if (packet_len == -1) {
		switch (errno) {

		case EAGAIN:
			return 0;	/* no packet there */

		case ENETDOWN:
			/*
			 * The device on which we're capturing went away.
			 *
			 * XXX - we should really return
			 * PCAP_ERROR_IFACE_NOT_UP, but pcap_dispatch()
			 * etc. aren't defined to return that.
			 */
			pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
				"The interface went down");
			return PCAP_ERROR;

		default:
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "recvfrom");
			return PCAP_ERROR;
		}
	}

#ifdef HAVE_PF_PACKET_SOCKETS
	if (!handlep->sock_packet) {
		/*
		 * Unfortunately, there is a window between socket() and
		 * bind() where the kernel may queue packets from any
		 * interface.  If we're bound to a particular interface,
		 * discard packets not from that interface.
		 *
		 * (If socket filters are supported, we could do the
		 * same thing we do when changing the filter; however,
		 * that won't handle packet sockets without socket
		 * filter support, and it's a bit more complicated.
		 * It would save some instructions per packet, however.)
		 */
		if (handlep->ifindex != -1 &&
		    from.sll_ifindex != handlep->ifindex)
			return 0;

		/*
		 * Do checks based on packet direction.
		 * We can only do this if we're using PF_PACKET; the
		 * address returned for SOCK_PACKET is a "sockaddr_pkt"
		 * which lacks the relevant packet type information.
		 */
		if (!linux_check_direction(handle, &from))
			return 0;
	}
#endif

#ifdef HAVE_PF_PACKET_SOCKETS
	/*
	 * If this is a cooked device, fill in the fake packet header.
	 */
	if (handlep->cooked) {
		/*
		 * Add the length of the fake header to the length
		 * of packet data we read.
		 */
		packet_len += SLL_HDR_LEN;

		hdrp = (struct sll_header *)bp;
		hdrp->sll_pkttype = map_packet_type_to_sll_type(from.sll_pkttype);
		hdrp->sll_hatype = htons(from.sll_hatype);
		hdrp->sll_halen = htons(from.sll_halen);
		memcpy(hdrp->sll_addr, from.sll_addr,
		    (from.sll_halen > SLL_ADDRLEN) ?
		      SLL_ADDRLEN :
		      from.sll_halen);
		hdrp->sll_protocol = from.sll_protocol;
	}

	/*
	 * Start out with no VLAN information.
	 */
	aux_data.vlan_tag_present = 0;
	aux_data.vlan_tag = 0;
#if defined(HAVE_PACKET_AUXDATA) && defined(HAVE_STRUCT_TPACKET_AUXDATA_TP_VLAN_TCI)
	if (handlep->vlan_offset != -1) {
		for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
			struct tpacket_auxdata *aux;
			unsigned int len;
			struct vlan_tag *tag;

			if (cmsg->cmsg_len < CMSG_LEN(sizeof(struct tpacket_auxdata)) ||
			    cmsg->cmsg_level != SOL_PACKET ||
			    cmsg->cmsg_type != PACKET_AUXDATA) {
				/*
				 * This isn't a PACKET_AUXDATA auxiliary
				 * data item.
				 */
				continue;
			}

			aux = (struct tpacket_auxdata *)CMSG_DATA(cmsg);
			if (!VLAN_VALID(aux, aux)) {
				/*
				 * There is no VLAN information in the
				 * auxiliary data.
				 */
				continue;
			}

			len = (u_int)packet_len > iov.iov_len ? iov.iov_len : (u_int)packet_len;
			if (len < (u_int)handlep->vlan_offset)
				break;

			/*
			 * Move everything in the header, except the
			 * type field, down VLAN_TAG_LEN bytes, to
			 * allow us to insert the VLAN tag between
			 * that stuff and the type field.
			 */
			bp -= VLAN_TAG_LEN;
			memmove(bp, bp + VLAN_TAG_LEN, handlep->vlan_offset);

			/*
			 * Now insert the tag.
			 */
			tag = (struct vlan_tag *)(bp + handlep->vlan_offset);
			tag->vlan_tpid = htons(VLAN_TPID(aux, aux));
			tag->vlan_tci = htons(aux->tp_vlan_tci);

			/*
			 * Save a flag indicating that we have a VLAN tag,
			 * and the VLAN TCI, to bpf_aux_data struct for
			 * use by the BPF filter if we're doing the
			 * filtering in userland.
			 */
			aux_data.vlan_tag_present = 1;
			aux_data.vlan_tag = htons(aux->tp_vlan_tci) & 0x0fff;

			/*
			 * Add the tag to the packet lengths.
			 */
			packet_len += VLAN_TAG_LEN;
		}
	}
#endif /* defined(HAVE_PACKET_AUXDATA) && defined(HAVE_STRUCT_TPACKET_AUXDATA_TP_VLAN_TCI) */
#endif /* HAVE_PF_PACKET_SOCKETS */

	/*
	 * XXX: According to the kernel source we should get the real
	 * packet len if calling recvfrom with MSG_TRUNC set. It does
	 * not seem to work here :(, but it is supported by this code
	 * anyway.
	 * To be honest the code RELIES on that feature so this is really
	 * broken with 2.2.x kernels.
	 * I spend a day to figure out what's going on and I found out
	 * that the following is happening:
	 *
	 * The packet comes from a random interface and the packet_rcv
	 * hook is called with a clone of the packet. That code inserts
	 * the packet into the receive queue of the packet socket.
	 * If a filter is attached to that socket that filter is run
	 * first - and there lies the problem. The default filter always
	 * cuts the packet at the snaplen:
	 *
	 * # tcpdump -d
	 * (000) ret      #68
	 *
	 * So the packet filter cuts down the packet. The recvfrom call
	 * says "hey, it's only 68 bytes, it fits into the buffer" with
	 * the result that we don't get the real packet length. This
	 * is valid at least until kernel 2.2.17pre6.
	 *
	 * We currently handle this by making a copy of the filter
	 * program, fixing all "ret" instructions with non-zero
	 * operands to have an operand of MAXIMUM_SNAPLEN so that the
	 * filter doesn't truncate the packet, and supplying that modified
	 * filter to the kernel.
	 */

	caplen = packet_len;
	if (caplen > handle->snapshot)
		caplen = handle->snapshot;

	/* Run the packet filter if not using kernel filter */
	if (handlep->filter_in_userland && handle->fcode.bf_insns) {
		if (bpf_filter_with_aux_data(handle->fcode.bf_insns, bp,
		    packet_len, caplen, &aux_data) == 0) {
			/* rejected by filter */
			return 0;
		}
	}

	/* Fill in our own header data */

	/* get timestamp for this packet */
#if defined(SIOCGSTAMPNS) && defined(SO_TIMESTAMPNS)
	if (handle->opt.tstamp_precision == PCAP_TSTAMP_PRECISION_NANO) {
		if (ioctl(handle->fd, SIOCGSTAMPNS, &pcap_header.ts) == -1) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "SIOCGSTAMPNS");
			return PCAP_ERROR;
		}
        } else
#endif
	{
		if (ioctl(handle->fd, SIOCGSTAMP, &pcap_header.ts) == -1) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "SIOCGSTAMP");
			return PCAP_ERROR;
		}
        }

	pcap_header.caplen	= caplen;
	pcap_header.len		= packet_len;

	/*
	 * Count the packet.
	 *
	 * Arguably, we should count them before we check the filter,
	 * as on many other platforms "ps_recv" counts packets
	 * handed to the filter rather than packets that passed
	 * the filter, but if filtering is done in the kernel, we
	 * can't get a count of packets that passed the filter,
	 * and that would mean the meaning of "ps_recv" wouldn't
	 * be the same on all Linux systems.
	 *
	 * XXX - it's not the same on all systems in any case;
	 * ideally, we should have a "get the statistics" call
	 * that supplies more counts and indicates which of them
	 * it supplies, so that we supply a count of packets
	 * handed to the filter only on platforms where that
	 * information is available.
	 *
	 * We count them here even if we can get the packet count
	 * from the kernel, as we can only determine at run time
	 * whether we'll be able to get it from the kernel (if
	 * HAVE_STRUCT_TPACKET_STATS isn't defined, we can't get it from
	 * the kernel, but if it is defined, the library might
	 * have been built with a 2.4 or later kernel, but we
	 * might be running on a 2.2[.x] kernel without Alexey
	 * Kuznetzov's turbopacket patches, and thus the kernel
	 * might not be able to supply those statistics).  We
	 * could, I guess, try, when opening the socket, to get
	 * the statistics, and if we can not increment the count
	 * here, but it's not clear that always incrementing
	 * the count is more expensive than always testing a flag
	 * in memory.
	 *
	 * We keep the count in "handlep->packets_read", and use that
	 * for "ps_recv" if we can't get the statistics from the kernel.
	 * We do that because, if we *can* get the statistics from
	 * the kernel, we use "handlep->stat.ps_recv" and
	 * "handlep->stat.ps_drop" as running counts, as reading the
	 * statistics from the kernel resets the kernel statistics,
	 * and if we directly increment "handlep->stat.ps_recv" here,
	 * that means it will count packets *twice* on systems where
	 * we can get kernel statistics - once here, and once in
	 * pcap_stats_linux().
	 */
	handlep->packets_read++;

	/* Call the user supplied callback function */
	callback(userdata, &pcap_header, bp);

	return 1;
}

static int
pcap_inject_linux(pcap_t *handle, const void *buf, size_t size)
{
	struct pcap_linux *handlep = handle->priv;
	int ret;

#ifdef HAVE_PF_PACKET_SOCKETS
	if (!handlep->sock_packet) {
		/* PF_PACKET socket */
		if (handlep->ifindex == -1) {
			/*
			 * We don't support sending on the "any" device.
			 */
			strlcpy(handle->errbuf,
			    "Sending packets isn't supported on the \"any\" device",
			    PCAP_ERRBUF_SIZE);
			return (-1);
		}

		if (handlep->cooked) {
			/*
			 * We don't support sending on the "any" device.
			 *
			 * XXX - how do you send on a bound cooked-mode
			 * socket?
			 * Is a "sendto()" required there?
			 */
			strlcpy(handle->errbuf,
			    "Sending packets isn't supported in cooked mode",
			    PCAP_ERRBUF_SIZE);
			return (-1);
		}
	}
#endif

	ret = send(handle->fd, buf, size, 0);
	if (ret == -1) {
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "send");
		return (-1);
	}
	return (ret);
}

/*
 *  Get the statistics for the given packet capture handle.
 *  Reports the number of dropped packets iff the kernel supports
 *  the PACKET_STATISTICS "getsockopt()" argument (2.4 and later
 *  kernels, and 2.2[.x] kernels with Alexey Kuznetzov's turbopacket
 *  patches); otherwise, that information isn't available, and we lie
 *  and report 0 as the count of dropped packets.
 */
static int
pcap_stats_linux(pcap_t *handle, struct pcap_stat *stats)
{
	struct pcap_linux *handlep = handle->priv;
#ifdef HAVE_STRUCT_TPACKET_STATS
#ifdef HAVE_TPACKET3
	/*
	 * For sockets using TPACKET_V1 or TPACKET_V2, the extra
	 * stuff at the end of a struct tpacket_stats_v3 will not
	 * be filled in, and we don't look at it so this is OK even
	 * for those sockets.  In addition, the PF_PACKET socket
	 * code in the kernel only uses the length parameter to
	 * compute how much data to copy out and to indicate how
	 * much data was copied out, so it's OK to base it on the
	 * size of a struct tpacket_stats.
	 *
	 * XXX - it's probably OK, in fact, to just use a
	 * struct tpacket_stats for V3 sockets, as we don't
	 * care about the tp_freeze_q_cnt stat.
	 */
	struct tpacket_stats_v3 kstats;
#else /* HAVE_TPACKET3 */
	struct tpacket_stats kstats;
#endif /* HAVE_TPACKET3 */
	socklen_t len = sizeof (struct tpacket_stats);
#endif /* HAVE_STRUCT_TPACKET_STATS */

	long if_dropped = 0;

	/*
	 *	To fill in ps_ifdrop, we parse /proc/net/dev for the number
	 */
	if (handle->opt.promisc)
	{
		if_dropped = handlep->proc_dropped;
		handlep->proc_dropped = linux_if_drops(handlep->device);
		handlep->stat.ps_ifdrop += (handlep->proc_dropped - if_dropped);
	}

#ifdef HAVE_STRUCT_TPACKET_STATS
	/*
	 * Try to get the packet counts from the kernel.
	 */
	if (getsockopt(handle->fd, SOL_PACKET, PACKET_STATISTICS,
			&kstats, &len) > -1) {
		/*
		 * On systems where the PACKET_STATISTICS "getsockopt()"
		 * argument is supported on PF_PACKET sockets:
		 *
		 *	"ps_recv" counts only packets that *passed* the
		 *	filter, not packets that didn't pass the filter.
		 *	This includes packets later dropped because we
		 *	ran out of buffer space.
		 *
		 *	"ps_drop" counts packets dropped because we ran
		 *	out of buffer space.  It doesn't count packets
		 *	dropped by the interface driver.  It counts only
		 *	packets that passed the filter.
		 *
		 *	See above for ps_ifdrop.
		 *
		 *	Both statistics include packets not yet read from
		 *	the kernel by libpcap, and thus not yet seen by
		 *	the application.
		 *
		 * In "linux/net/packet/af_packet.c", at least in the
		 * 2.4.9 kernel, "tp_packets" is incremented for every
		 * packet that passes the packet filter *and* is
		 * successfully queued on the socket; "tp_drops" is
		 * incremented for every packet dropped because there's
		 * not enough free space in the socket buffer.
		 *
		 * When the statistics are returned for a PACKET_STATISTICS
		 * "getsockopt()" call, "tp_drops" is added to "tp_packets",
		 * so that "tp_packets" counts all packets handed to
		 * the PF_PACKET socket, including packets dropped because
		 * there wasn't room on the socket buffer - but not
		 * including packets that didn't pass the filter.
		 *
		 * In the BSD BPF, the count of received packets is
		 * incremented for every packet handed to BPF, regardless
		 * of whether it passed the filter.
		 *
		 * We can't make "pcap_stats()" work the same on both
		 * platforms, but the best approximation is to return
		 * "tp_packets" as the count of packets and "tp_drops"
		 * as the count of drops.
		 *
		 * Keep a running total because each call to
		 *    getsockopt(handle->fd, SOL_PACKET, PACKET_STATISTICS, ....
		 * resets the counters to zero.
		 */
		handlep->stat.ps_recv += kstats.tp_packets;
		handlep->stat.ps_drop += kstats.tp_drops;
		*stats = handlep->stat;
		return 0;
	}
	else
	{
		/*
		 * If the error was EOPNOTSUPP, fall through, so that
		 * if you build the library on a system with
		 * "struct tpacket_stats" and run it on a system
		 * that doesn't, it works as it does if the library
		 * is built on a system without "struct tpacket_stats".
		 */
		if (errno != EOPNOTSUPP) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "pcap_stats");
			return -1;
		}
	}
#endif
	/*
	 * On systems where the PACKET_STATISTICS "getsockopt()" argument
	 * is not supported on PF_PACKET sockets:
	 *
	 *	"ps_recv" counts only packets that *passed* the filter,
	 *	not packets that didn't pass the filter.  It does not
	 *	count packets dropped because we ran out of buffer
	 *	space.
	 *
	 *	"ps_drop" is not supported.
	 *
	 *	"ps_ifdrop" is supported. It will return the number
	 *	of drops the interface reports in /proc/net/dev,
	 *	if that is available.
	 *
	 *	"ps_recv" doesn't include packets not yet read from
	 *	the kernel by libpcap.
	 *
	 * We maintain the count of packets processed by libpcap in
	 * "handlep->packets_read", for reasons described in the comment
	 * at the end of pcap_read_packet().  We have no idea how many
	 * packets were dropped by the kernel buffers -- but we know
	 * how many the interface dropped, so we can return that.
	 */

	stats->ps_recv = handlep->packets_read;
	stats->ps_drop = 0;
	stats->ps_ifdrop = handlep->stat.ps_ifdrop;
	return 0;
}

static int
add_linux_if(pcap_if_list_t *devlistp, const char *ifname, int fd, char *errbuf)
{
	const char *p;
	char name[512];	/* XXX - pick a size */
	char *q, *saveq;
	struct ifreq ifrflags;

	/*
	 * Get the interface name.
	 */
	p = ifname;
	q = &name[0];
	while (*p != '\0' && isascii(*p) && !isspace(*p)) {
		if (*p == ':') {
			/*
			 * This could be the separator between a
			 * name and an alias number, or it could be
			 * the separator between a name with no
			 * alias number and the next field.
			 *
			 * If there's a colon after digits, it
			 * separates the name and the alias number,
			 * otherwise it separates the name and the
			 * next field.
			 */
			saveq = q;
			while (isascii(*p) && isdigit(*p))
				*q++ = *p++;
			if (*p != ':') {
				/*
				 * That was the next field,
				 * not the alias number.
				 */
				q = saveq;
			}
			break;
		} else
			*q++ = *p++;
	}
	*q = '\0';

	/*
	 * Get the flags for this interface.
	 */
	strlcpy(ifrflags.ifr_name, name, sizeof(ifrflags.ifr_name));
	if (ioctl(fd, SIOCGIFFLAGS, (char *)&ifrflags) < 0) {
		if (errno == ENXIO || errno == ENODEV)
			return (0);	/* device doesn't actually exist - ignore it */
		pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
		    errno, "SIOCGIFFLAGS: %.*s",
		    (int)sizeof(ifrflags.ifr_name),
		    ifrflags.ifr_name);
		return (-1);
	}

	/*
	 * Add an entry for this interface, with no addresses, if it's
	 * not already in the list.
	 */
	if (find_or_add_if(devlistp, name, ifrflags.ifr_flags,
	    get_if_flags, errbuf) == NULL) {
		/*
		 * Failure.
		 */
		return (-1);
	}

	return (0);
}

/*
 * Get from "/sys/class/net" all interfaces listed there; if they're
 * already in the list of interfaces we have, that won't add another
 * instance, but if they're not, that'll add them.
 *
 * We don't bother getting any addresses for them; it appears you can't
 * use SIOCGIFADDR on Linux to get IPv6 addresses for interfaces, and,
 * although some other types of addresses can be fetched with SIOCGIFADDR,
 * we don't bother with them for now.
 *
 * We also don't fail if we couldn't open "/sys/class/net"; we just leave
 * the list of interfaces as is, and return 0, so that we can try
 * scanning /proc/net/dev.
 *
 * Otherwise, we return 1 if we don't get an error and -1 if we do.
 */
static int
scan_sys_class_net(pcap_if_list_t *devlistp, char *errbuf)
{
	DIR *sys_class_net_d;
	int fd;
	struct dirent *ent;
	char subsystem_path[PATH_MAX+1];
	struct stat statb;
	int ret = 1;

	sys_class_net_d = opendir("/sys/class/net");
	if (sys_class_net_d == NULL) {
		/*
		 * Don't fail if it doesn't exist at all.
		 */
		if (errno == ENOENT)
			return (0);

		/*
		 * Fail if we got some other error.
		 */
		pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
		    errno, "Can't open /sys/class/net");
		return (-1);
	}

	/*
	 * Create a socket from which to fetch interface information.
	 */
	fd = socket(PF_UNIX, SOCK_RAW, 0);
	if (fd < 0) {
		pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
		    errno, "socket");
		(void)closedir(sys_class_net_d);
		return (-1);
	}

	for (;;) {
		errno = 0;
		ent = readdir(sys_class_net_d);
		if (ent == NULL) {
			/*
			 * Error or EOF; if errno != 0, it's an error.
			 */
			break;
		}

		/*
		 * Ignore "." and "..".
		 */
		if (strcmp(ent->d_name, ".") == 0 ||
		    strcmp(ent->d_name, "..") == 0)
			continue;

		/*
		 * Ignore plain files; they do not have subdirectories
		 * and thus have no attributes.
		 */
		if (ent->d_type == DT_REG)
			continue;

		/*
		 * Is there an "ifindex" file under that name?
		 * (We don't care whether it's a directory or
		 * a symlink; older kernels have directories
		 * for devices, newer kernels have symlinks to
		 * directories.)
		 */
		pcap_snprintf(subsystem_path, sizeof subsystem_path,
		    "/sys/class/net/%s/ifindex", ent->d_name);
		if (lstat(subsystem_path, &statb) != 0) {
			/*
			 * Stat failed.  Either there was an error
			 * other than ENOENT, and we don't know if
			 * this is an interface, or it's ENOENT,
			 * and either some part of "/sys/class/net/{if}"
			 * disappeared, in which case it probably means
			 * the interface disappeared, or there's no
			 * "ifindex" file, which means it's not a
			 * network interface.
			 */
			continue;
		}

		/*
		 * Attempt to add the interface.
		 */
		if (add_linux_if(devlistp, &ent->d_name[0], fd, errbuf) == -1) {
			/* Fail. */
			ret = -1;
			break;
		}
	}
	if (ret != -1) {
		/*
		 * Well, we didn't fail for any other reason; did we
		 * fail due to an error reading the directory?
		 */
		if (errno != 0) {
			pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
			    errno, "Error reading /sys/class/net");
			ret = -1;
		}
	}

	(void)close(fd);
	(void)closedir(sys_class_net_d);
	return (ret);
}

/*
 * Get from "/proc/net/dev" all interfaces listed there; if they're
 * already in the list of interfaces we have, that won't add another
 * instance, but if they're not, that'll add them.
 *
 * See comments from scan_sys_class_net().
 */
static int
scan_proc_net_dev(pcap_if_list_t *devlistp, char *errbuf)
{
	FILE *proc_net_f;
	int fd;
	char linebuf[512];
	int linenum;
	char *p;
	int ret = 0;

	proc_net_f = fopen("/proc/net/dev", "r");
	if (proc_net_f == NULL) {
		/*
		 * Don't fail if it doesn't exist at all.
		 */
		if (errno == ENOENT)
			return (0);

		/*
		 * Fail if we got some other error.
		 */
		pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
		    errno, "Can't open /proc/net/dev");
		return (-1);
	}

	/*
	 * Create a socket from which to fetch interface information.
	 */
	fd = socket(PF_UNIX, SOCK_RAW, 0);
	if (fd < 0) {
		pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
		    errno, "socket");
		(void)fclose(proc_net_f);
		return (-1);
	}

	for (linenum = 1;
	    fgets(linebuf, sizeof linebuf, proc_net_f) != NULL; linenum++) {
		/*
		 * Skip the first two lines - they're headers.
		 */
		if (linenum <= 2)
			continue;

		p = &linebuf[0];

		/*
		 * Skip leading white space.
		 */
		while (*p != '\0' && isascii(*p) && isspace(*p))
			p++;
		if (*p == '\0' || *p == '\n')
			continue;	/* blank line */

		/*
		 * Attempt to add the interface.
		 */
		if (add_linux_if(devlistp, p, fd, errbuf) == -1) {
			/* Fail. */
			ret = -1;
			break;
		}
	}
	if (ret != -1) {
		/*
		 * Well, we didn't fail for any other reason; did we
		 * fail due to an error reading the file?
		 */
		if (ferror(proc_net_f)) {
			pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
			    errno, "Error reading /proc/net/dev");
			ret = -1;
		}
	}

	(void)close(fd);
	(void)fclose(proc_net_f);
	return (ret);
}

/*
 * Description string for the "any" device.
 */
static const char any_descr[] = "Pseudo-device that captures on all interfaces";

/*
 * A SOCK_PACKET or PF_PACKET socket can be bound to any network interface.
 */
static int
can_be_bound(const char *name _U_)
{
	return (1);
}

/*
 * Get additional flags for a device, using SIOCGIFMEDIA.
 */
static int
get_if_flags(const char *name, bpf_u_int32 *flags, char *errbuf)
{
	int sock;
	FILE *fh;
	unsigned int arptype;
	struct ifreq ifr;
	struct ethtool_value info;

	if (*flags & PCAP_IF_LOOPBACK) {
		/*
		 * Loopback devices aren't wireless, and "connected"/
		 * "disconnected" doesn't apply to them.
		 */
		*flags |= PCAP_IF_CONNECTION_STATUS_NOT_APPLICABLE;
		return 0;
	}

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1) {
		pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE, errno,
		    "Can't create socket to get ethtool information for %s",
		    name);
		return -1;
	}

	/*
	 * OK, what type of network is this?
	 * In particular, is it wired or wireless?
	 */
	if (is_wifi(sock, name)) {
		/*
		 * Wi-Fi, hence wireless.
		 */
		*flags |= PCAP_IF_WIRELESS;
	} else {
		/*
		 * OK, what does /sys/class/net/{if}/type contain?
		 * (We don't use that for Wi-Fi, as it'll report
		 * "Ethernet", i.e. ARPHRD_ETHER, for non-monitor-
		 * mode devices.)
		 */
		char *pathstr;

		if (asprintf(&pathstr, "/sys/class/net/%s/type", name) == -1) {
			pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "%s: Can't generate path name string for /sys/class/net device",
			    name);
			close(sock);
			return -1;
		}
		fh = fopen(pathstr, "r");
		if (fh != NULL) {
			if (fscanf(fh, "%u", &arptype) == 1) {
				/*
				 * OK, we got an ARPHRD_ type; what is it?
				 */
				switch (arptype) {

#ifdef ARPHRD_LOOPBACK
				case ARPHRD_LOOPBACK:
					/*
					 * These are types to which
					 * "connected" and "disconnected"
					 * don't apply, so don't bother
					 * asking about it.
					 *
					 * XXX - add other types?
					 */
					close(sock);
					fclose(fh);
					free(pathstr);
					return 0;
#endif

				case ARPHRD_IRDA:
				case ARPHRD_IEEE80211:
				case ARPHRD_IEEE80211_PRISM:
				case ARPHRD_IEEE80211_RADIOTAP:
#ifdef ARPHRD_IEEE802154
				case ARPHRD_IEEE802154:
#endif
#ifdef ARPHRD_IEEE802154_MONITOR
				case ARPHRD_IEEE802154_MONITOR:
#endif
#ifdef ARPHRD_6LOWPAN
				case ARPHRD_6LOWPAN:
#endif
					/*
					 * Various wireless types.
					 */
					*flags |= PCAP_IF_WIRELESS;
					break;
				}
			}
			fclose(fh);
			free(pathstr);
		}
	}

#ifdef ETHTOOL_GLINK
	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	info.cmd = ETHTOOL_GLINK;
	ifr.ifr_data = (caddr_t)&info;
	if (ioctl(sock, SIOCETHTOOL, &ifr) == -1) {
		int save_errno = errno;

		switch (save_errno) {

		case EOPNOTSUPP:
		case EINVAL:
			/*
			 * OK, this OS version or driver doesn't support
			 * asking for this information.
			 * XXX - distinguish between "this doesn't
			 * support ethtool at all because it's not
			 * that type of device" vs. "this doesn't
			 * support ethtool even though it's that
			 * type of device", and return "unknown".
			 */
			*flags |= PCAP_IF_CONNECTION_STATUS_NOT_APPLICABLE;
			close(sock);
			return 0;

		case ENODEV:
			/*
			 * OK, no such device.
			 * The user will find that out when they try to
			 * activate the device; just say "OK" and
			 * don't set anything.
			 */
			close(sock);
			return 0;

		default:
			/*
			 * Other error.
			 */
			pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
			    save_errno,
			    "%s: SIOCETHTOOL(ETHTOOL_GLINK) ioctl failed",
			    name);
			close(sock);
			return -1;
		}
	}

	/*
	 * Is it connected?
	 */
	if (info.data) {
		/*
		 * It's connected.
		 */
		*flags |= PCAP_IF_CONNECTION_STATUS_CONNECTED;
	} else {
		/*
		 * It's disconnected.
		 */
		*flags |= PCAP_IF_CONNECTION_STATUS_DISCONNECTED;
	}
#endif

	close(sock);
	return 0;
}

int
pcap_platform_finddevs(pcap_if_list_t *devlistp, char *errbuf)
{
	int ret;

	/*
	 * Get the list of regular interfaces first.
	 */
	if (pcap_findalldevs_interfaces(devlistp, errbuf, can_be_bound,
	    get_if_flags) == -1)
		return (-1);	/* failure */

	/*
	 * Read "/sys/class/net", and add to the list of interfaces all
	 * interfaces listed there that we don't already have, because,
	 * on Linux, SIOCGIFCONF reports only interfaces with IPv4 addresses,
	 * and even getifaddrs() won't return information about
	 * interfaces with no addresses, so you need to read "/sys/class/net"
	 * to get the names of the rest of the interfaces.
	 */
	ret = scan_sys_class_net(devlistp, errbuf);
	if (ret == -1)
		return (-1);	/* failed */
	if (ret == 0) {
		/*
		 * No /sys/class/net; try reading /proc/net/dev instead.
		 */
		if (scan_proc_net_dev(devlistp, errbuf) == -1)
			return (-1);
	}

	/*
	 * Add the "any" device.
	 * As it refers to all network devices, not to any particular
	 * network device, the notion of "connected" vs. "disconnected"
	 * doesn't apply.
	 */
	if (add_dev(devlistp, "any",
	    PCAP_IF_UP|PCAP_IF_RUNNING|PCAP_IF_CONNECTION_STATUS_NOT_APPLICABLE,
	    any_descr, errbuf) == NULL)
		return (-1);

	return (0);
}

/*
 *  Attach the given BPF code to the packet capture device.
 */
static int
pcap_setfilter_linux_common(pcap_t *handle, struct bpf_program *filter,
    int is_mmapped)
{
	struct pcap_linux *handlep;
#ifdef SO_ATTACH_FILTER
	struct sock_fprog	fcode;
	int			can_filter_in_kernel;
	int			err = 0;
#endif

	if (!handle)
		return -1;
	if (!filter) {
	        strlcpy(handle->errbuf, "setfilter: No filter specified",
			PCAP_ERRBUF_SIZE);
		return -1;
	}

	handlep = handle->priv;

	/* Make our private copy of the filter */

	if (install_bpf_program(handle, filter) < 0)
		/* install_bpf_program() filled in errbuf */
		return -1;

	/*
	 * Run user level packet filter by default. Will be overriden if
	 * installing a kernel filter succeeds.
	 */
	handlep->filter_in_userland = 1;

	/* Install kernel level filter if possible */

#ifdef SO_ATTACH_FILTER
#ifdef USHRT_MAX
	if (handle->fcode.bf_len > USHRT_MAX) {
		/*
		 * fcode.len is an unsigned short for current kernel.
		 * I have yet to see BPF-Code with that much
		 * instructions but still it is possible. So for the
		 * sake of correctness I added this check.
		 */
		fprintf(stderr, "Warning: Filter too complex for kernel\n");
		fcode.len = 0;
		fcode.filter = NULL;
		can_filter_in_kernel = 0;
	} else
#endif /* USHRT_MAX */
	{
		/*
		 * Oh joy, the Linux kernel uses struct sock_fprog instead
		 * of struct bpf_program and of course the length field is
		 * of different size. Pointed out by Sebastian
		 *
		 * Oh, and we also need to fix it up so that all "ret"
		 * instructions with non-zero operands have MAXIMUM_SNAPLEN
		 * as the operand if we're not capturing in memory-mapped
		 * mode, and so that, if we're in cooked mode, all memory-
		 * reference instructions use special magic offsets in
		 * references to the link-layer header and assume that the
		 * link-layer payload begins at 0; "fix_program()" will do
		 * that.
		 */
		switch (fix_program(handle, &fcode, is_mmapped)) {

		case -1:
		default:
			/*
			 * Fatal error; just quit.
			 * (The "default" case shouldn't happen; we
			 * return -1 for that reason.)
			 */
			return -1;

		case 0:
			/*
			 * The program performed checks that we can't make
			 * work in the kernel.
			 */
			can_filter_in_kernel = 0;
			break;

		case 1:
			/*
			 * We have a filter that'll work in the kernel.
			 */
			can_filter_in_kernel = 1;
			break;
		}
	}

	/*
	 * NOTE: at this point, we've set both the "len" and "filter"
	 * fields of "fcode".  As of the 2.6.32.4 kernel, at least,
	 * those are the only members of the "sock_fprog" structure,
	 * so we initialize every member of that structure.
	 *
	 * If there is anything in "fcode" that is not initialized,
	 * it is either a field added in a later kernel, or it's
	 * padding.
	 *
	 * If a new field is added, this code needs to be updated
	 * to set it correctly.
	 *
	 * If there are no other fields, then:
	 *
	 *	if the Linux kernel looks at the padding, it's
	 *	buggy;
	 *
	 *	if the Linux kernel doesn't look at the padding,
	 *	then if some tool complains that we're passing
	 *	uninitialized data to the kernel, then the tool
	 *	is buggy and needs to understand that it's just
	 *	padding.
	 */
	if (can_filter_in_kernel) {
		if ((err = set_kernel_filter(handle, &fcode)) == 0)
		{
			/*
			 * Installation succeded - using kernel filter,
			 * so userland filtering not needed.
			 */
			handlep->filter_in_userland = 0;
		}
		else if (err == -1)	/* Non-fatal error */
		{
			/*
			 * Print a warning if we weren't able to install
			 * the filter for a reason other than "this kernel
			 * isn't configured to support socket filters.
			 */
			if (errno != ENOPROTOOPT && errno != EOPNOTSUPP) {
				fprintf(stderr,
				    "Warning: Kernel filter failed: %s\n",
					pcap_strerror(errno));
			}
		}
	}

	/*
	 * If we're not using the kernel filter, get rid of any kernel
	 * filter that might've been there before, e.g. because the
	 * previous filter could work in the kernel, or because some other
	 * code attached a filter to the socket by some means other than
	 * calling "pcap_setfilter()".  Otherwise, the kernel filter may
	 * filter out packets that would pass the new userland filter.
	 */
	if (handlep->filter_in_userland) {
		if (reset_kernel_filter(handle) == -1) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno,
			    "can't remove kernel filter");
			err = -2;	/* fatal error */
		}
	}

	/*
	 * Free up the copy of the filter that was made by "fix_program()".
	 */
	if (fcode.filter != NULL)
		free(fcode.filter);

	if (err == -2)
		/* Fatal error */
		return -1;
#endif /* SO_ATTACH_FILTER */

	return 0;
}

static int
pcap_setfilter_linux(pcap_t *handle, struct bpf_program *filter)
{
	return pcap_setfilter_linux_common(handle, filter, 0);
}


/*
 * Set direction flag: Which packets do we accept on a forwarding
 * single device? IN, OUT or both?
 */
static int
pcap_setdirection_linux(pcap_t *handle, pcap_direction_t d)
{
#ifdef HAVE_PF_PACKET_SOCKETS
	struct pcap_linux *handlep = handle->priv;

	if (!handlep->sock_packet) {
		handle->direction = d;
		return 0;
	}
#endif
	/*
	 * We're not using PF_PACKET sockets, so we can't determine
	 * the direction of the packet.
	 */
	pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
	    "Setting direction is not supported on SOCK_PACKET sockets");
	return -1;
}

#ifdef HAVE_PF_PACKET_SOCKETS
/*
 * Map the PACKET_ value to a LINUX_SLL_ value; we
 * want the same numerical value to be used in
 * the link-layer header even if the numerical values
 * for the PACKET_ #defines change, so that programs
 * that look at the packet type field will always be
 * able to handle DLT_LINUX_SLL captures.
 */
static short int
map_packet_type_to_sll_type(short int sll_pkttype)
{
	switch (sll_pkttype) {

	case PACKET_HOST:
		return htons(LINUX_SLL_HOST);

	case PACKET_BROADCAST:
		return htons(LINUX_SLL_BROADCAST);

	case PACKET_MULTICAST:
		return  htons(LINUX_SLL_MULTICAST);

	case PACKET_OTHERHOST:
		return htons(LINUX_SLL_OTHERHOST);

	case PACKET_OUTGOING:
		return htons(LINUX_SLL_OUTGOING);

	default:
		return -1;
	}
}
#endif

static int
is_wifi(int sock_fd
#ifndef IW_MODE_MONITOR
_U_
#endif
, const char *device)
{
	char *pathstr;
	struct stat statb;
#ifdef IW_MODE_MONITOR
	char errbuf[PCAP_ERRBUF_SIZE];
#endif

	/*
	 * See if there's a sysfs wireless directory for it.
	 * If so, it's a wireless interface.
	 */
	if (asprintf(&pathstr, "/sys/class/net/%s/wireless", device) == -1) {
		/*
		 * Just give up here.
		 */
		return 0;
	}
	if (stat(pathstr, &statb) == 0) {
		free(pathstr);
		return 1;
	}
	free(pathstr);

#ifdef IW_MODE_MONITOR
	/*
	 * OK, maybe it's not wireless, or maybe this kernel doesn't
	 * support sysfs.  Try the wireless extensions.
	 */
	if (has_wext(sock_fd, device, errbuf) == 1) {
		/*
		 * It supports the wireless extensions, so it's a Wi-Fi
		 * device.
		 */
		return 1;
	}
#endif
	return 0;
}

/*
 *  Linux uses the ARP hardware type to identify the type of an
 *  interface. pcap uses the DLT_xxx constants for this. This
 *  function takes a pointer to a "pcap_t", and an ARPHRD_xxx
 *  constant, as arguments, and sets "handle->linktype" to the
 *  appropriate DLT_XXX constant and sets "handle->offset" to
 *  the appropriate value (to make "handle->offset" plus link-layer
 *  header length be a multiple of 4, so that the link-layer payload
 *  will be aligned on a 4-byte boundary when capturing packets).
 *  (If the offset isn't set here, it'll be 0; add code as appropriate
 *  for cases where it shouldn't be 0.)
 *
 *  If "cooked_ok" is non-zero, we can use DLT_LINUX_SLL and capture
 *  in cooked mode; otherwise, we can't use cooked mode, so we have
 *  to pick some type that works in raw mode, or fail.
 *
 *  Sets the link type to -1 if unable to map the type.
 */
static void map_arphrd_to_dlt(pcap_t *handle, int sock_fd, int arptype,
			      const char *device, int cooked_ok)
{
	static const char cdma_rmnet[] = "cdma_rmnet";

	switch (arptype) {

	case ARPHRD_ETHER:
		/*
		 * For various annoying reasons having to do with DHCP
		 * software, some versions of Android give the mobile-
		 * phone-network interface an ARPHRD_ value of
		 * ARPHRD_ETHER, even though the packets supplied by
		 * that interface have no link-layer header, and begin
		 * with an IP header, so that the ARPHRD_ value should
		 * be ARPHRD_NONE.
		 *
		 * Detect those devices by checking the device name, and
		 * use DLT_RAW for them.
		 */
		if (strncmp(device, cdma_rmnet, sizeof cdma_rmnet - 1) == 0) {
			handle->linktype = DLT_RAW;
			return;
		}

		/*
		 * Is this a real Ethernet device?  If so, give it a
		 * link-layer-type list with DLT_EN10MB and DLT_DOCSIS, so
		 * that an application can let you choose it, in case you're
		 * capturing DOCSIS traffic that a Cisco Cable Modem
		 * Termination System is putting out onto an Ethernet (it
		 * doesn't put an Ethernet header onto the wire, it puts raw
		 * DOCSIS frames out on the wire inside the low-level
		 * Ethernet framing).
		 *
		 * XXX - are there any other sorts of "fake Ethernet" that
		 * have ARPHRD_ETHER but that shouldn't offer DLT_DOCSIS as
		 * a Cisco CMTS won't put traffic onto it or get traffic
		 * bridged onto it?  ISDN is handled in "activate_new()",
		 * as we fall back on cooked mode there, and we use
		 * is_wifi() to check for 802.11 devices; are there any
		 * others?
		 */
		if (!is_wifi(sock_fd, device)) {
			/*
			 * It's not a Wi-Fi device; offer DOCSIS.
			 */
			handle->dlt_list = (u_int *) malloc(sizeof(u_int) * 2);
			/*
			 * If that fails, just leave the list empty.
			 */
			if (handle->dlt_list != NULL) {
				handle->dlt_list[0] = DLT_EN10MB;
				handle->dlt_list[1] = DLT_DOCSIS;
				handle->dlt_count = 2;
			}
		}
		/* FALLTHROUGH */

	case ARPHRD_METRICOM:
	case ARPHRD_LOOPBACK:
		handle->linktype = DLT_EN10MB;
		handle->offset = 2;
		break;

	case ARPHRD_EETHER:
		handle->linktype = DLT_EN3MB;
		break;

	case ARPHRD_AX25:
		handle->linktype = DLT_AX25_KISS;
		break;

	case ARPHRD_PRONET:
		handle->linktype = DLT_PRONET;
		break;

	case ARPHRD_CHAOS:
		handle->linktype = DLT_CHAOS;
		break;
#ifndef ARPHRD_CAN
#define ARPHRD_CAN 280
#endif
	case ARPHRD_CAN:
		/*
		 * Map this to DLT_LINUX_SLL; that way, CAN frames will
		 * have ETH_P_CAN/LINUX_SLL_P_CAN as the protocol and
		 * CAN FD frames will have ETH_P_CANFD/LINUX_SLL_P_CANFD
		 * as the protocol, so they can be distinguished by the
		 * protocol in the SLL header.
		 */
		handle->linktype = DLT_LINUX_SLL;
		break;

#ifndef ARPHRD_IEEE802_TR
#define ARPHRD_IEEE802_TR 800	/* From Linux 2.4 */
#endif
	case ARPHRD_IEEE802_TR:
	case ARPHRD_IEEE802:
		handle->linktype = DLT_IEEE802;
		handle->offset = 2;
		break;

	case ARPHRD_ARCNET:
		handle->linktype = DLT_ARCNET_LINUX;
		break;

#ifndef ARPHRD_FDDI	/* From Linux 2.2.13 */
#define ARPHRD_FDDI	774
#endif
	case ARPHRD_FDDI:
		handle->linktype = DLT_FDDI;
		handle->offset = 3;
		break;

#ifndef ARPHRD_ATM  /* FIXME: How to #include this? */
#define ARPHRD_ATM 19
#endif
	case ARPHRD_ATM:
		/*
		 * The Classical IP implementation in ATM for Linux
		 * supports both what RFC 1483 calls "LLC Encapsulation",
		 * in which each packet has an LLC header, possibly
		 * with a SNAP header as well, prepended to it, and
		 * what RFC 1483 calls "VC Based Multiplexing", in which
		 * different virtual circuits carry different network
		 * layer protocols, and no header is prepended to packets.
		 *
		 * They both have an ARPHRD_ type of ARPHRD_ATM, so
		 * you can't use the ARPHRD_ type to find out whether
		 * captured packets will have an LLC header, and,
		 * while there's a socket ioctl to *set* the encapsulation
		 * type, there's no ioctl to *get* the encapsulation type.
		 *
		 * This means that
		 *
		 *	programs that dissect Linux Classical IP frames
		 *	would have to check for an LLC header and,
		 *	depending on whether they see one or not, dissect
		 *	the frame as LLC-encapsulated or as raw IP (I
		 *	don't know whether there's any traffic other than
		 *	IP that would show up on the socket, or whether
		 *	there's any support for IPv6 in the Linux
		 *	Classical IP code);
		 *
		 *	filter expressions would have to compile into
		 *	code that checks for an LLC header and does
		 *	the right thing.
		 *
		 * Both of those are a nuisance - and, at least on systems
		 * that support PF_PACKET sockets, we don't have to put
		 * up with those nuisances; instead, we can just capture
		 * in cooked mode.  That's what we'll do, if we can.
		 * Otherwise, we'll just fail.
		 */
		if (cooked_ok)
			handle->linktype = DLT_LINUX_SLL;
		else
			handle->linktype = -1;
		break;

#ifndef ARPHRD_IEEE80211  /* From Linux 2.4.6 */
#define ARPHRD_IEEE80211 801
#endif
	case ARPHRD_IEEE80211:
		handle->linktype = DLT_IEEE802_11;
		break;

#ifndef ARPHRD_IEEE80211_PRISM  /* From Linux 2.4.18 */
#define ARPHRD_IEEE80211_PRISM 802
#endif
	case ARPHRD_IEEE80211_PRISM:
		handle->linktype = DLT_PRISM_HEADER;
		break;

#ifndef ARPHRD_IEEE80211_RADIOTAP /* new */
#define ARPHRD_IEEE80211_RADIOTAP 803
#endif
	case ARPHRD_IEEE80211_RADIOTAP:
		handle->linktype = DLT_IEEE802_11_RADIO;
		break;

	case ARPHRD_PPP:
		/*
		 * Some PPP code in the kernel supplies no link-layer
		 * header whatsoever to PF_PACKET sockets; other PPP
		 * code supplies PPP link-layer headers ("syncppp.c");
		 * some PPP code might supply random link-layer
		 * headers (PPP over ISDN - there's code in Ethereal,
		 * for example, to cope with PPP-over-ISDN captures
		 * with which the Ethereal developers have had to cope,
		 * heuristically trying to determine which of the
		 * oddball link-layer headers particular packets have).
		 *
		 * As such, we just punt, and run all PPP interfaces
		 * in cooked mode, if we can; otherwise, we just treat
		 * it as DLT_RAW, for now - if somebody needs to capture,
		 * on a 2.0[.x] kernel, on PPP devices that supply a
		 * link-layer header, they'll have to add code here to
		 * map to the appropriate DLT_ type (possibly adding a
		 * new DLT_ type, if necessary).
		 */
		if (cooked_ok)
			handle->linktype = DLT_LINUX_SLL;
		else {
			/*
			 * XXX - handle ISDN types here?  We can't fall
			 * back on cooked sockets, so we'd have to
			 * figure out from the device name what type of
			 * link-layer encapsulation it's using, and map
			 * that to an appropriate DLT_ value, meaning
			 * we'd map "isdnN" devices to DLT_RAW (they
			 * supply raw IP packets with no link-layer
			 * header) and "isdY" devices to a new DLT_I4L_IP
			 * type that has only an Ethernet packet type as
			 * a link-layer header.
			 *
			 * But sometimes we seem to get random crap
			 * in the link-layer header when capturing on
			 * ISDN devices....
			 */
			handle->linktype = DLT_RAW;
		}
		break;

#ifndef ARPHRD_CISCO
#define ARPHRD_CISCO 513 /* previously ARPHRD_HDLC */
#endif
	case ARPHRD_CISCO:
		handle->linktype = DLT_C_HDLC;
		break;

	/* Not sure if this is correct for all tunnels, but it
	 * works for CIPE */
	case ARPHRD_TUNNEL:
#ifndef ARPHRD_SIT
#define ARPHRD_SIT 776	/* From Linux 2.2.13 */
#endif
	case ARPHRD_SIT:
	case ARPHRD_CSLIP:
	case ARPHRD_SLIP6:
	case ARPHRD_CSLIP6:
	case ARPHRD_ADAPT:
	case ARPHRD_SLIP:
#ifndef ARPHRD_RAWHDLC
#define ARPHRD_RAWHDLC 518
#endif
	case ARPHRD_RAWHDLC:
#ifndef ARPHRD_DLCI
#define ARPHRD_DLCI 15
#endif
	case ARPHRD_DLCI:
		/*
		 * XXX - should some of those be mapped to DLT_LINUX_SLL
		 * instead?  Should we just map all of them to DLT_LINUX_SLL?
		 */
		handle->linktype = DLT_RAW;
		break;

#ifndef ARPHRD_FRAD
#define ARPHRD_FRAD 770
#endif
	case ARPHRD_FRAD:
		handle->linktype = DLT_FRELAY;
		break;

	case ARPHRD_LOCALTLK:
		handle->linktype = DLT_LTALK;
		break;

	case 18:
		/*
		 * RFC 4338 defines an encapsulation for IP and ARP
		 * packets that's compatible with the RFC 2625
		 * encapsulation, but that uses a different ARP
		 * hardware type and hardware addresses.  That
		 * ARP hardware type is 18; Linux doesn't define
		 * any ARPHRD_ value as 18, but if it ever officially
		 * supports RFC 4338-style IP-over-FC, it should define
		 * one.
		 *
		 * For now, we map it to DLT_IP_OVER_FC, in the hopes
		 * that this will encourage its use in the future,
		 * should Linux ever officially support RFC 4338-style
		 * IP-over-FC.
		 */
		handle->linktype = DLT_IP_OVER_FC;
		break;

#ifndef ARPHRD_FCPP
#define ARPHRD_FCPP	784
#endif
	case ARPHRD_FCPP:
#ifndef ARPHRD_FCAL
#define ARPHRD_FCAL	785
#endif
	case ARPHRD_FCAL:
#ifndef ARPHRD_FCPL
#define ARPHRD_FCPL	786
#endif
	case ARPHRD_FCPL:
#ifndef ARPHRD_FCFABRIC
#define ARPHRD_FCFABRIC	787
#endif
	case ARPHRD_FCFABRIC:
		/*
		 * Back in 2002, Donald Lee at Cray wanted a DLT_ for
		 * IP-over-FC:
		 *
		 *	http://www.mail-archive.com/tcpdump-workers@sandelman.ottawa.on.ca/msg01043.html
		 *
		 * and one was assigned.
		 *
		 * In a later private discussion (spun off from a message
		 * on the ethereal-users list) on how to get that DLT_
		 * value in libpcap on Linux, I ended up deciding that
		 * the best thing to do would be to have him tweak the
		 * driver to set the ARPHRD_ value to some ARPHRD_FCxx
		 * type, and map all those types to DLT_IP_OVER_FC:
		 *
		 *	I've checked into the libpcap and tcpdump CVS tree
		 *	support for DLT_IP_OVER_FC.  In order to use that,
		 *	you'd have to modify your modified driver to return
		 *	one of the ARPHRD_FCxxx types, in "fcLINUXfcp.c" -
		 *	change it to set "dev->type" to ARPHRD_FCFABRIC, for
		 *	example (the exact value doesn't matter, it can be
		 *	any of ARPHRD_FCPP, ARPHRD_FCAL, ARPHRD_FCPL, or
		 *	ARPHRD_FCFABRIC).
		 *
		 * 11 years later, Christian Svensson wanted to map
		 * various ARPHRD_ values to DLT_FC_2 and
		 * DLT_FC_2_WITH_FRAME_DELIMS for raw Fibre Channel
		 * frames:
		 *
		 *	https://github.com/mcr/libpcap/pull/29
		 *
		 * There doesn't seem to be any network drivers that uses
		 * any of the ARPHRD_FC* values for IP-over-FC, and
		 * it's not exactly clear what the "Dummy types for non
		 * ARP hardware" are supposed to mean (link-layer
		 * header type?  Physical network type?), so it's
		 * not exactly clear why the ARPHRD_FC* types exist
		 * in the first place.
		 *
		 * For now, we map them to DLT_FC_2, and provide an
		 * option of DLT_FC_2_WITH_FRAME_DELIMS, as well as
		 * DLT_IP_OVER_FC just in case there's some old
		 * driver out there that uses one of those types for
		 * IP-over-FC on which somebody wants to capture
		 * packets.
		 */
		handle->dlt_list = (u_int *) malloc(sizeof(u_int) * 3);
		/*
		 * If that fails, just leave the list empty.
		 */
		if (handle->dlt_list != NULL) {
			handle->dlt_list[0] = DLT_FC_2;
			handle->dlt_list[1] = DLT_FC_2_WITH_FRAME_DELIMS;
			handle->dlt_list[2] = DLT_IP_OVER_FC;
			handle->dlt_count = 3;
		}
		handle->linktype = DLT_FC_2;
		break;

#ifndef ARPHRD_IRDA
#define ARPHRD_IRDA	783
#endif
	case ARPHRD_IRDA:
		/* Don't expect IP packet out of this interfaces... */
		handle->linktype = DLT_LINUX_IRDA;
		/* We need to save packet direction for IrDA decoding,
		 * so let's use "Linux-cooked" mode. Jean II
		 *
		 * XXX - this is handled in activate_new(). */
		/* handlep->cooked = 1; */
		break;

	/* ARPHRD_LAPD is unofficial and randomly allocated, if reallocation
	 * is needed, please report it to <daniele@orlandi.com> */
#ifndef ARPHRD_LAPD
#define ARPHRD_LAPD	8445
#endif
	case ARPHRD_LAPD:
		/* Don't expect IP packet out of this interfaces... */
		handle->linktype = DLT_LINUX_LAPD;
		break;

#ifndef ARPHRD_NONE
#define ARPHRD_NONE	0xFFFE
#endif
	case ARPHRD_NONE:
		/*
		 * No link-layer header; packets are just IP
		 * packets, so use DLT_RAW.
		 */
		handle->linktype = DLT_RAW;
		break;

#ifndef ARPHRD_IEEE802154
#define ARPHRD_IEEE802154      804
#endif
       case ARPHRD_IEEE802154:
               handle->linktype =  DLT_IEEE802_15_4_NOFCS;
               break;

#ifndef ARPHRD_NETLINK
#define ARPHRD_NETLINK	824
#endif
	case ARPHRD_NETLINK:
		handle->linktype = DLT_NETLINK;
		/*
		 * We need to use cooked mode, so that in sll_protocol we
		 * pick up the netlink protocol type such as NETLINK_ROUTE,
		 * NETLINK_GENERIC, NETLINK_FIB_LOOKUP, etc.
		 *
		 * XXX - this is handled in activate_new().
		 */
		/* handlep->cooked = 1; */
		break;

#ifndef ARPHRD_VSOCKMON
#define ARPHRD_VSOCKMON	826
#endif
	case ARPHRD_VSOCKMON:
		handle->linktype = DLT_VSOCK;
		break;

	default:
		handle->linktype = -1;
		break;
	}
}

/* ===== Functions to interface to the newer kernels ================== */

/*
 * Try to open a packet socket using the new kernel PF_PACKET interface.
 * Returns 1 on success, 0 on an error that means the new interface isn't
 * present (so the old SOCK_PACKET interface should be tried), and a
 * PCAP_ERROR_ value on an error that means that the old mechanism won't
 * work either (so it shouldn't be tried).
 */
static int
activate_new(pcap_t *handle)
{
#ifdef HAVE_PF_PACKET_SOCKETS
	struct pcap_linux *handlep = handle->priv;
	const char		*device = handle->opt.device;
	int			is_any_device = (strcmp(device, "any") == 0);
	int			protocol = pcap_protocol(handle);
	int			sock_fd = -1, arptype;
#ifdef HAVE_PACKET_AUXDATA
	int			val;
#endif
	int			err = 0;
	struct packet_mreq	mr;
#if defined(SO_BPF_EXTENSIONS) && defined(SKF_AD_VLAN_TAG_PRESENT)
	int			bpf_extensions;
	socklen_t		len = sizeof(bpf_extensions);
#endif

	/*
	 * Open a socket with protocol family packet. If the
	 * "any" device was specified, we open a SOCK_DGRAM
	 * socket for the cooked interface, otherwise we first
	 * try a SOCK_RAW socket for the raw interface.
	 */
	sock_fd = is_any_device ?
		socket(PF_PACKET, SOCK_DGRAM, protocol) :
		socket(PF_PACKET, SOCK_RAW, protocol);

	if (sock_fd == -1) {
		if (errno == EINVAL || errno == EAFNOSUPPORT) {
			/*
			 * We don't support PF_PACKET/SOCK_whatever
			 * sockets; try the old mechanism.
			 */
			return 0;
		}

		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "socket");
		if (errno == EPERM || errno == EACCES) {
			/*
			 * You don't have permission to open the
			 * socket.
			 */
			return PCAP_ERROR_PERM_DENIED;
		} else {
			/*
			 * Other error.
			 */
			return PCAP_ERROR;
		}
	}

	/* It seems the kernel supports the new interface. */
	handlep->sock_packet = 0;

	/*
	 * Get the interface index of the loopback device.
	 * If the attempt fails, don't fail, just set the
	 * "handlep->lo_ifindex" to -1.
	 *
	 * XXX - can there be more than one device that loops
	 * packets back, i.e. devices other than "lo"?  If so,
	 * we'd need to find them all, and have an array of
	 * indices for them, and check all of them in
	 * "pcap_read_packet()".
	 */
	handlep->lo_ifindex = iface_get_id(sock_fd, "lo", handle->errbuf);

	/*
	 * Default value for offset to align link-layer payload
	 * on a 4-byte boundary.
	 */
	handle->offset	 = 0;

	/*
	 * What kind of frames do we have to deal with? Fall back
	 * to cooked mode if we have an unknown interface type
	 * or a type we know doesn't work well in raw mode.
	 */
	if (!is_any_device) {
		/* Assume for now we don't need cooked mode. */
		handlep->cooked = 0;

		if (handle->opt.rfmon) {
			/*
			 * We were asked to turn on monitor mode.
			 * Do so before we get the link-layer type,
			 * because entering monitor mode could change
			 * the link-layer type.
			 */
			err = enter_rfmon_mode(handle, sock_fd, device);
			if (err < 0) {
				/* Hard failure */
				close(sock_fd);
				return err;
			}
			if (err == 0) {
				/*
				 * Nothing worked for turning monitor mode
				 * on.
				 */
				close(sock_fd);
				return PCAP_ERROR_RFMON_NOTSUP;
			}

			/*
			 * Either monitor mode has been turned on for
			 * the device, or we've been given a different
			 * device to open for monitor mode.  If we've
			 * been given a different device, use it.
			 */
			if (handlep->mondevice != NULL)
				device = handlep->mondevice;
		}
		arptype	= iface_get_arptype(sock_fd, device, handle->errbuf);
		if (arptype < 0) {
			close(sock_fd);
			return arptype;
		}
		map_arphrd_to_dlt(handle, sock_fd, arptype, device, 1);
		if (handle->linktype == -1 ||
		    handle->linktype == DLT_LINUX_SLL ||
		    handle->linktype == DLT_LINUX_IRDA ||
		    handle->linktype == DLT_LINUX_LAPD ||
		    handle->linktype == DLT_NETLINK ||
		    (handle->linktype == DLT_EN10MB &&
		     (strncmp("isdn", device, 4) == 0 ||
		      strncmp("isdY", device, 4) == 0))) {
			/*
			 * Unknown interface type (-1), or a
			 * device we explicitly chose to run
			 * in cooked mode (e.g., PPP devices),
			 * or an ISDN device (whose link-layer
			 * type we can only determine by using
			 * APIs that may be different on different
			 * kernels) - reopen in cooked mode.
			 */
			if (close(sock_fd) == -1) {
				pcap_fmt_errmsg_for_errno(handle->errbuf,
				    PCAP_ERRBUF_SIZE, errno, "close");
				return PCAP_ERROR;
			}
			sock_fd = socket(PF_PACKET, SOCK_DGRAM, protocol);
			if (sock_fd == -1) {
				pcap_fmt_errmsg_for_errno(handle->errbuf,
				    PCAP_ERRBUF_SIZE, errno, "socket");
				if (errno == EPERM || errno == EACCES) {
					/*
					 * You don't have permission to
					 * open the socket.
					 */
					return PCAP_ERROR_PERM_DENIED;
				} else {
					/*
					 * Other error.
					 */
					return PCAP_ERROR;
				}
			}
			handlep->cooked = 1;

			/*
			 * Get rid of any link-layer type list
			 * we allocated - this only supports cooked
			 * capture.
			 */
			if (handle->dlt_list != NULL) {
				free(handle->dlt_list);
				handle->dlt_list = NULL;
				handle->dlt_count = 0;
			}

			if (handle->linktype == -1) {
				/*
				 * Warn that we're falling back on
				 * cooked mode; we may want to
				 * update "map_arphrd_to_dlt()"
				 * to handle the new type.
				 */
				pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
					"arptype %d not "
					"supported by libpcap - "
					"falling back to cooked "
					"socket",
					arptype);
			}

			/*
			 * IrDA capture is not a real "cooked" capture,
			 * it's IrLAP frames, not IP packets.  The
			 * same applies to LAPD capture.
			 */
			if (handle->linktype != DLT_LINUX_IRDA &&
			    handle->linktype != DLT_LINUX_LAPD &&
			    handle->linktype != DLT_NETLINK)
				handle->linktype = DLT_LINUX_SLL;
		}

		handlep->ifindex = iface_get_id(sock_fd, device,
		    handle->errbuf);
		if (handlep->ifindex == -1) {
			close(sock_fd);
			return PCAP_ERROR;
		}

		if ((err = iface_bind(sock_fd, handlep->ifindex,
		    handle->errbuf, protocol)) != 1) {
		    	close(sock_fd);
			if (err < 0)
				return err;
			else
				return 0;	/* try old mechanism */
		}
	} else {
		/*
		 * The "any" device.
		 */
		if (handle->opt.rfmon) {
			/*
			 * It doesn't support monitor mode.
			 */
			close(sock_fd);
			return PCAP_ERROR_RFMON_NOTSUP;
		}

		/*
		 * It uses cooked mode.
		 */
		handlep->cooked = 1;
		handle->linktype = DLT_LINUX_SLL;

		/*
		 * We're not bound to a device.
		 * For now, we're using this as an indication
		 * that we can't transmit; stop doing that only
		 * if we figure out how to transmit in cooked
		 * mode.
		 */
		handlep->ifindex = -1;
	}

	/*
	 * Select promiscuous mode on if "promisc" is set.
	 *
	 * Do not turn allmulti mode on if we don't select
	 * promiscuous mode - on some devices (e.g., Orinoco
	 * wireless interfaces), allmulti mode isn't supported
	 * and the driver implements it by turning promiscuous
	 * mode on, and that screws up the operation of the
	 * card as a normal networking interface, and on no
	 * other platform I know of does starting a non-
	 * promiscuous capture affect which multicast packets
	 * are received by the interface.
	 */

	/*
	 * Hmm, how can we set promiscuous mode on all interfaces?
	 * I am not sure if that is possible at all.  For now, we
	 * silently ignore attempts to turn promiscuous mode on
	 * for the "any" device (so you don't have to explicitly
	 * disable it in programs such as tcpdump).
	 */

	if (!is_any_device && handle->opt.promisc) {
		memset(&mr, 0, sizeof(mr));
		mr.mr_ifindex = handlep->ifindex;
		mr.mr_type    = PACKET_MR_PROMISC;
		if (setsockopt(sock_fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP,
		    &mr, sizeof(mr)) == -1) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "setsockopt");
			close(sock_fd);
			return PCAP_ERROR;
		}
	}

	/* Enable auxillary data if supported and reserve room for
	 * reconstructing VLAN headers. */
#ifdef HAVE_PACKET_AUXDATA
	val = 1;
	if (setsockopt(sock_fd, SOL_PACKET, PACKET_AUXDATA, &val,
		       sizeof(val)) == -1 && errno != ENOPROTOOPT) {
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "setsockopt");
		close(sock_fd);
		return PCAP_ERROR;
	}
	handle->offset += VLAN_TAG_LEN;
#endif /* HAVE_PACKET_AUXDATA */

	/*
	 * This is a 2.2[.x] or later kernel (we know that
	 * because we're not using a SOCK_PACKET socket -
	 * PF_PACKET is supported only in 2.2 and later
	 * kernels).
	 *
	 * We can safely pass "recvfrom()" a byte count
	 * based on the snapshot length.
	 *
	 * If we're in cooked mode, make the snapshot length
	 * large enough to hold a "cooked mode" header plus
	 * 1 byte of packet data (so we don't pass a byte
	 * count of 0 to "recvfrom()").
	 */
	if (handlep->cooked) {
		if (handle->snapshot < SLL_HDR_LEN + 1)
			handle->snapshot = SLL_HDR_LEN + 1;
	}
	handle->bufsize = handle->snapshot;

	/*
	 * Set the offset at which to insert VLAN tags.
	 * That should be the offset of the type field.
	 */
	switch (handle->linktype) {

	case DLT_EN10MB:
		/*
		 * The type field is after the destination and source
		 * MAC address.
		 */
		handlep->vlan_offset = 2 * ETH_ALEN;
		break;

	case DLT_LINUX_SLL:
		/*
		 * The type field is in the last 2 bytes of the
		 * DLT_LINUX_SLL header.
		 */
		handlep->vlan_offset = SLL_HDR_LEN - 2;
		break;

	default:
		handlep->vlan_offset = -1; /* unknown */
		break;
	}

#if defined(SIOCGSTAMPNS) && defined(SO_TIMESTAMPNS)
	if (handle->opt.tstamp_precision == PCAP_TSTAMP_PRECISION_NANO) {
		int nsec_tstamps = 1;

		if (setsockopt(sock_fd, SOL_SOCKET, SO_TIMESTAMPNS, &nsec_tstamps, sizeof(nsec_tstamps)) < 0) {
			pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE, "setsockopt: unable to set SO_TIMESTAMPNS");
			close(sock_fd);
			return PCAP_ERROR;
		}
	}
#endif /* defined(SIOCGSTAMPNS) && defined(SO_TIMESTAMPNS) */

	/*
	 * We've succeeded. Save the socket FD in the pcap structure.
	 */
	handle->fd = sock_fd;

#if defined(SO_BPF_EXTENSIONS) && defined(SKF_AD_VLAN_TAG_PRESENT)
	/*
	 * Can we generate special code for VLAN checks?
	 * (XXX - what if we need the special code but it's not supported
	 * by the OS?  Is that possible?)
	 */
	if (getsockopt(sock_fd, SOL_SOCKET, SO_BPF_EXTENSIONS,
	    &bpf_extensions, &len) == 0) {
		if (bpf_extensions >= SKF_AD_VLAN_TAG_PRESENT) {
			/*
			 * Yes, we can.  Request that we do so.
			 */
			handle->bpf_codegen_flags |= BPF_SPECIAL_VLAN_HANDLING;
		}
	}
#endif /* defined(SO_BPF_EXTENSIONS) && defined(SKF_AD_VLAN_TAG_PRESENT) */

	return 1;
#else /* HAVE_PF_PACKET_SOCKETS */
	strlcpy(ebuf,
		"New packet capturing interface not supported by build "
		"environment", PCAP_ERRBUF_SIZE);
	return 0;
#endif /* HAVE_PF_PACKET_SOCKETS */
}

#ifdef HAVE_PACKET_RING
/*
 * Attempt to activate with memory-mapped access.
 *
 * On success, returns 1, and sets *status to 0 if there are no warnings
 * or to a PCAP_WARNING_ code if there is a warning.
 *
 * On failure due to lack of support for memory-mapped capture, returns
 * 0.
 *
 * On error, returns -1, and sets *status to the appropriate error code;
 * if that is PCAP_ERROR, sets handle->errbuf to the appropriate message.
 */
static int
activate_mmap(pcap_t *handle, int *status)
{
	struct pcap_linux *handlep = handle->priv;
	int ret;

	/*
	 * Attempt to allocate a buffer to hold the contents of one
	 * packet, for use by the oneshot callback.
	 */
	handlep->oneshot_buffer = malloc(handle->snapshot);
	if (handlep->oneshot_buffer == NULL) {
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "can't allocate oneshot buffer");
		*status = PCAP_ERROR;
		return -1;
	}

	if (handle->opt.buffer_size == 0) {
		/* by default request 2M for the ring buffer */
		handle->opt.buffer_size = 2*1024*1024;
	}
	ret = prepare_tpacket_socket(handle);
	if (ret == -1) {
		free(handlep->oneshot_buffer);
		*status = PCAP_ERROR;
		return ret;
	}
	ret = create_ring(handle, status);
	if (ret == 0) {
		/*
		 * We don't support memory-mapped capture; our caller
		 * will fall back on reading from the socket.
		 */
		free(handlep->oneshot_buffer);
		return 0;
	}
	if (ret == -1) {
		/*
		 * Error attempting to enable memory-mapped capture;
		 * fail.  create_ring() has set *status.
		 */
		free(handlep->oneshot_buffer);
		return -1;
	}

	/*
	 * Success.  *status has been set either to 0 if there are no
	 * warnings or to a PCAP_WARNING_ value if there is a warning.
	 *
	 * Override some defaults and inherit the other fields from
	 * activate_new.
	 * handle->offset is used to get the current position into the rx ring.
	 * handle->cc is used to store the ring size.
	 */

	switch (handlep->tp_version) {
	case TPACKET_V1:
		handle->read_op = pcap_read_linux_mmap_v1;
		break;
	case TPACKET_V1_64:
		handle->read_op = pcap_read_linux_mmap_v1_64;
		break;
#ifdef HAVE_TPACKET2
	case TPACKET_V2:
		handle->read_op = pcap_read_linux_mmap_v2;
		break;
#endif
#ifdef HAVE_TPACKET3
	case TPACKET_V3:
		handle->read_op = pcap_read_linux_mmap_v3;
		break;
#endif
	}
	handle->cleanup_op = pcap_cleanup_linux_mmap;
	handle->setfilter_op = pcap_setfilter_linux_mmap;
	handle->setnonblock_op = pcap_setnonblock_mmap;
	handle->getnonblock_op = pcap_getnonblock_mmap;
	handle->oneshot_callback = pcap_oneshot_mmap;
	handle->selectable_fd = handle->fd;
	return 1;
}
#else /* HAVE_PACKET_RING */
static int
activate_mmap(pcap_t *handle _U_, int *status _U_)
{
	return 0;
}
#endif /* HAVE_PACKET_RING */

#ifdef HAVE_PACKET_RING

#if defined(HAVE_TPACKET2) || defined(HAVE_TPACKET3)
/*
 * Attempt to set the socket to the specified version of the memory-mapped
 * header.
 *
 * Return 0 if we succeed; return 1 if we fail because that version isn't
 * supported; return -1 on any other error, and set handle->errbuf.
 */
static int
init_tpacket(pcap_t *handle, int version, const char *version_str)
{
	struct pcap_linux *handlep = handle->priv;
	int val = version;
	socklen_t len = sizeof(val);

	/*
	 * Probe whether kernel supports the specified TPACKET version;
	 * this also gets the length of the header for that version.
	 */
	if (getsockopt(handle->fd, SOL_PACKET, PACKET_HDRLEN, &val, &len) < 0) {
		if (errno == ENOPROTOOPT || errno == EINVAL)
			return 1;	/* no */

		/* Failed to even find out; this is a fatal error. */
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "can't get %s header len on packet socket",
		    version_str);
		return -1;
	}
	handlep->tp_hdrlen = val;

	val = version;
	if (setsockopt(handle->fd, SOL_PACKET, PACKET_VERSION, &val,
			   sizeof(val)) < 0) {
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "can't activate %s on packet socket", version_str);
		return -1;
	}
	handlep->tp_version = version;

	/* Reserve space for VLAN tag reconstruction */
	val = VLAN_TAG_LEN;
	if (setsockopt(handle->fd, SOL_PACKET, PACKET_RESERVE, &val,
			   sizeof(val)) < 0) {
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "can't set up reserve on packet socket");
		return -1;
	}

	return 0;
}
#endif /* defined HAVE_TPACKET2 || defined HAVE_TPACKET3 */

/*
 * If the instruction set for which we're compiling has both 32-bit
 * and 64-bit versions, and Linux support for the 64-bit version
 * predates TPACKET_V2, define ISA_64_BIT as the .machine value
 * you get from uname() for the 64-bit version.  Otherwise, leave
 * it undefined.  (This includes ARM, which has a 64-bit version,
 * but Linux support for it appeared well after TPACKET_V2 support
 * did, so there should never be a case where 32-bit ARM code is
 * running o a 64-bit kernel that only supports TPACKET_V1.)
 *
 * If we've omitted your favorite such architecture, please contribute
 * a patch.  (No patch is needed for architectures that are 32-bit-only
 * or for which Linux has no support for 32-bit userland - or for which,
 * as noted, 64-bit support appeared in Linux after TPACKET_V2 support
 * did.)
 */
#if defined(__i386__)
#define ISA_64_BIT	"x86_64"
#elif defined(__ppc__)
#define ISA_64_BIT	"ppc64"
#elif defined(__sparc__)
#define ISA_64_BIT	"sparc64"
#elif defined(__s390__)
#define ISA_64_BIT	"s390x"
#elif defined(__mips__)
#define ISA_64_BIT	"mips64"
#elif defined(__hppa__)
#define ISA_64_BIT	"parisc64"
#endif

/*
 * Attempt to set the socket to version 3 of the memory-mapped header and,
 * if that fails because version 3 isn't supported, attempt to fall
 * back to version 2.  If version 2 isn't supported, just leave it at
 * version 1.
 *
 * Return 1 if we succeed or if we fail because neither version 2 nor 3 is
 * supported; return -1 on any other error, and set handle->errbuf.
 */
static int
prepare_tpacket_socket(pcap_t *handle)
{
	struct pcap_linux *handlep = handle->priv;
#if defined(HAVE_TPACKET2) || defined(HAVE_TPACKET3)
	int ret;
#endif

#ifdef HAVE_TPACKET3
	/*
	 * Try setting the version to TPACKET_V3.
	 *
	 * The only mode in which buffering is done on PF_PACKET
	 * sockets, so that packets might not be delivered
	 * immediately, is TPACKET_V3 mode.
	 *
	 * The buffering cannot be disabled in that mode, so
	 * if the user has requested immediate mode, we don't
	 * use TPACKET_V3.
	 */
	if (!handle->opt.immediate) {
		ret = init_tpacket(handle, TPACKET_V3, "TPACKET_V3");
		if (ret == 0) {
			/*
			 * Success.
			 */
			return 1;
		}
		if (ret == -1) {
			/*
			 * We failed for some reason other than "the
			 * kernel doesn't support TPACKET_V3".
			 */
			return -1;
		}
	}
#endif /* HAVE_TPACKET3 */

#ifdef HAVE_TPACKET2
	/*
	 * Try setting the version to TPACKET_V2.
	 */
	ret = init_tpacket(handle, TPACKET_V2, "TPACKET_V2");
	if (ret == 0) {
		/*
		 * Success.
		 */
		return 1;
	}
	if (ret == -1) {
		/*
		 * We failed for some reason other than "the
		 * kernel doesn't support TPACKET_V2".
		 */
		return -1;
	}
#endif /* HAVE_TPACKET2 */

	/*
	 * OK, we're using TPACKET_V1, as that's all the kernel supports.
	 */
	handlep->tp_version = TPACKET_V1;
	handlep->tp_hdrlen = sizeof(struct tpacket_hdr);

#ifdef ISA_64_BIT
	/*
	 * 32-bit userspace + 64-bit kernel + TPACKET_V1 are not compatible with
	 * each other due to platform-dependent data type size differences.
	 *
	 * If we have a 32-bit userland and a 64-bit kernel, use an
	 * internally-defined TPACKET_V1_64, with which we use a 64-bit
	 * version of the data structures.
	 */
	if (sizeof(long) == 4) {
		/*
		 * This is 32-bit code.
		 */
		struct utsname utsname;

		if (uname(&utsname) == -1) {
			/*
			 * Failed.
			 */
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "uname failed");
			return -1;
		}
		if (strcmp(utsname.machine, ISA_64_BIT) == 0) {
			/*
			 * uname() tells us the machine is 64-bit,
			 * so we presumably have a 64-bit kernel.
			 *
			 * XXX - this presumes that uname() won't lie
			 * in 32-bit code and claim that the machine
			 * has the 32-bit version of the ISA.
			 */
			handlep->tp_version = TPACKET_V1_64;
			handlep->tp_hdrlen = sizeof(struct tpacket_hdr_64);
		}
	}
#endif

	return 1;
}

#define MAX(a,b) ((a)>(b)?(a):(b))

/*
 * Attempt to set up memory-mapped access.
 *
 * On success, returns 1, and sets *status to 0 if there are no warnings
 * or to a PCAP_WARNING_ code if there is a warning.
 *
 * On failure due to lack of support for memory-mapped capture, returns
 * 0.
 *
 * On error, returns -1, and sets *status to the appropriate error code;
 * if that is PCAP_ERROR, sets handle->errbuf to the appropriate message.
 */
static int
create_ring(pcap_t *handle, int *status)
{
	struct pcap_linux *handlep = handle->priv;
	unsigned i, j, frames_per_block;
#ifdef HAVE_TPACKET3
	/*
	 * For sockets using TPACKET_V1 or TPACKET_V2, the extra
	 * stuff at the end of a struct tpacket_req3 will be
	 * ignored, so this is OK even for those sockets.
	 */
	struct tpacket_req3 req;
#else
	struct tpacket_req req;
#endif
	socklen_t len;
	unsigned int sk_type, tp_reserve, maclen, tp_hdrlen, netoff, macoff;
	unsigned int frame_size;

	/*
	 * Start out assuming no warnings or errors.
	 */
	*status = 0;

	switch (handlep->tp_version) {

	case TPACKET_V1:
	case TPACKET_V1_64:
#ifdef HAVE_TPACKET2
	case TPACKET_V2:
#endif
		/* Note that with large snapshot length (say 256K, which is
		 * the default for recent versions of tcpdump, Wireshark,
		 * TShark, dumpcap or 64K, the value that "-s 0" has given for
		 * a long time with tcpdump), if we use the snapshot
		 * length to calculate the frame length, only a few frames
		 * will be available in the ring even with pretty
		 * large ring size (and a lot of memory will be unused).
		 *
		 * Ideally, we should choose a frame length based on the
		 * minimum of the specified snapshot length and the maximum
		 * packet size.  That's not as easy as it sounds; consider,
		 * for example, an 802.11 interface in monitor mode, where
		 * the frame would include a radiotap header, where the
		 * maximum radiotap header length is device-dependent.
		 *
		 * So, for now, we just do this for Ethernet devices, where
		 * there's no metadata header, and the link-layer header is
		 * fixed length.  We can get the maximum packet size by
		 * adding 18, the Ethernet header length plus the CRC length
		 * (just in case we happen to get the CRC in the packet), to
		 * the MTU of the interface; we fetch the MTU in the hopes
		 * that it reflects support for jumbo frames.  (Even if the
		 * interface is just being used for passive snooping, the
		 * driver might set the size of buffers in the receive ring
		 * based on the MTU, so that the MTU limits the maximum size
		 * of packets that we can receive.)
		 *
		 * If segmentation/fragmentation or receive offload are
		 * enabled, we can get reassembled/aggregated packets larger
		 * than MTU, but bounded to 65535 plus the Ethernet overhead,
		 * due to kernel and protocol constraints */
		frame_size = handle->snapshot;
		if (handle->linktype == DLT_EN10MB) {
			unsigned int max_frame_len;
			int mtu;
			int offload;

			mtu = iface_get_mtu(handle->fd, handle->opt.device,
			    handle->errbuf);
			if (mtu == -1) {
				*status = PCAP_ERROR;
				return -1;
			}
			offload = iface_get_offload(handle);
			if (offload == -1) {
				*status = PCAP_ERROR;
				return -1;
			}
			if (offload)
				max_frame_len = MAX(mtu, 65535);
			else
				max_frame_len = mtu;
			max_frame_len += 18;

			if (frame_size > max_frame_len)
				frame_size = max_frame_len;
		}

		/* NOTE: calculus matching those in tpacket_rcv()
		 * in linux-2.6/net/packet/af_packet.c
		 */
		len = sizeof(sk_type);
		if (getsockopt(handle->fd, SOL_SOCKET, SO_TYPE, &sk_type,
		    &len) < 0) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "getsockopt");
			*status = PCAP_ERROR;
			return -1;
		}
#ifdef PACKET_RESERVE
		len = sizeof(tp_reserve);
		if (getsockopt(handle->fd, SOL_PACKET, PACKET_RESERVE,
		    &tp_reserve, &len) < 0) {
			if (errno != ENOPROTOOPT) {
				/*
				 * ENOPROTOOPT means "kernel doesn't support
				 * PACKET_RESERVE", in which case we fall back
				 * as best we can.
				 */
				pcap_fmt_errmsg_for_errno(handle->errbuf,
				    PCAP_ERRBUF_SIZE, errno, "getsockopt");
				*status = PCAP_ERROR;
				return -1;
			}
			tp_reserve = 0;	/* older kernel, reserve not supported */
		}
#else
		tp_reserve = 0;	/* older kernel, reserve not supported */
#endif
		maclen = (sk_type == SOCK_DGRAM) ? 0 : MAX_LINKHEADER_SIZE;
			/* XXX: in the kernel maclen is calculated from
			 * LL_ALLOCATED_SPACE(dev) and vnet_hdr.hdr_len
			 * in:  packet_snd()           in linux-2.6/net/packet/af_packet.c
			 * then packet_alloc_skb()     in linux-2.6/net/packet/af_packet.c
			 * then sock_alloc_send_pskb() in linux-2.6/net/core/sock.c
			 * but I see no way to get those sizes in userspace,
			 * like for instance with an ifreq ioctl();
			 * the best thing I've found so far is MAX_HEADER in
			 * the kernel part of linux-2.6/include/linux/netdevice.h
			 * which goes up to 128+48=176; since pcap-linux.c
			 * defines a MAX_LINKHEADER_SIZE of 256 which is
			 * greater than that, let's use it.. maybe is it even
			 * large enough to directly replace macoff..
			 */
		tp_hdrlen = TPACKET_ALIGN(handlep->tp_hdrlen) + sizeof(struct sockaddr_ll) ;
		netoff = TPACKET_ALIGN(tp_hdrlen + (maclen < 16 ? 16 : maclen)) + tp_reserve;
			/* NOTE: AFAICS tp_reserve may break the TPACKET_ALIGN
			 * of netoff, which contradicts
			 * linux-2.6/Documentation/networking/packet_mmap.txt
			 * documenting that:
			 * "- Gap, chosen so that packet data (Start+tp_net)
			 * aligns to TPACKET_ALIGNMENT=16"
			 */
			/* NOTE: in linux-2.6/include/linux/skbuff.h:
			 * "CPUs often take a performance hit
			 *  when accessing unaligned memory locations"
			 */
		macoff = netoff - maclen;
		req.tp_frame_size = TPACKET_ALIGN(macoff + frame_size);
		/*
		 * Round the buffer size up to a multiple of the
		 * frame size (rather than rounding down, which
		 * would give a buffer smaller than our caller asked
		 * for, and possibly give zero frames if the requested
		 * buffer size is too small for one frame).
		 */
		req.tp_frame_nr = (handle->opt.buffer_size + req.tp_frame_size - 1)/req.tp_frame_size;
		break;

#ifdef HAVE_TPACKET3
	case TPACKET_V3:
		/* The "frames" for this are actually buffers that
		 * contain multiple variable-sized frames.
		 *
		 * We pick a "frame" size of MAXIMUM_SNAPLEN to leave
		 * enough room for at least one reasonably-sized packet
		 * in the "frame". */
		req.tp_frame_size = MAXIMUM_SNAPLEN;
		/*
		 * Round the buffer size up to a multiple of the
		 * "frame" size (rather than rounding down, which
		 * would give a buffer smaller than our caller asked
		 * for, and possibly give zero "frames" if the requested
		 * buffer size is too small for one "frame").
		 */
		req.tp_frame_nr = (handle->opt.buffer_size + req.tp_frame_size - 1)/req.tp_frame_size;
		break;
#endif
	default:
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
		    "Internal error: unknown TPACKET_ value %u",
		    handlep->tp_version);
		*status = PCAP_ERROR;
		return -1;
	}

	/* compute the minumum block size that will handle this frame.
	 * The block has to be page size aligned.
	 * The max block size allowed by the kernel is arch-dependent and
	 * it's not explicitly checked here. */
	req.tp_block_size = getpagesize();
	while (req.tp_block_size < req.tp_frame_size)
		req.tp_block_size <<= 1;

	frames_per_block = req.tp_block_size/req.tp_frame_size;

	/*
	 * PACKET_TIMESTAMP was added after linux/net_tstamp.h was,
	 * so we check for PACKET_TIMESTAMP.  We check for
	 * linux/net_tstamp.h just in case a system somehow has
	 * PACKET_TIMESTAMP but not linux/net_tstamp.h; that might
	 * be unnecessary.
	 *
	 * SIOCSHWTSTAMP was introduced in the patch that introduced
	 * linux/net_tstamp.h, so we don't bother checking whether
	 * SIOCSHWTSTAMP is defined (if your Linux system has
	 * linux/net_tstamp.h but doesn't define SIOCSHWTSTAMP, your
	 * Linux system is badly broken).
	 */
#if defined(HAVE_LINUX_NET_TSTAMP_H) && defined(PACKET_TIMESTAMP)
	/*
	 * If we were told to do so, ask the kernel and the driver
	 * to use hardware timestamps.
	 *
	 * Hardware timestamps are only supported with mmapped
	 * captures.
	 */
	if (handle->opt.tstamp_type == PCAP_TSTAMP_ADAPTER ||
	    handle->opt.tstamp_type == PCAP_TSTAMP_ADAPTER_UNSYNCED) {
		struct hwtstamp_config hwconfig;
		struct ifreq ifr;
		int timesource;

		/*
		 * Ask for hardware time stamps on all packets,
		 * including transmitted packets.
		 */
		memset(&hwconfig, 0, sizeof(hwconfig));
		hwconfig.tx_type = HWTSTAMP_TX_ON;
		hwconfig.rx_filter = HWTSTAMP_FILTER_ALL;

		memset(&ifr, 0, sizeof(ifr));
		strlcpy(ifr.ifr_name, handle->opt.device, sizeof(ifr.ifr_name));
		ifr.ifr_data = (void *)&hwconfig;

		if (ioctl(handle->fd, SIOCSHWTSTAMP, &ifr) < 0) {
			switch (errno) {

			case EPERM:
				/*
				 * Treat this as an error, as the
				 * user should try to run this
				 * with the appropriate privileges -
				 * and, if they can't, shouldn't
				 * try requesting hardware time stamps.
				 */
				*status = PCAP_ERROR_PERM_DENIED;
				return -1;

			case EOPNOTSUPP:
			case ERANGE:
				/*
				 * Treat this as a warning, as the
				 * only way to fix the warning is to
				 * get an adapter that supports hardware
				 * time stamps for *all* packets.
				 * (ERANGE means "we support hardware
				 * time stamps, but for packets matching
				 * that particular filter", so it means
				 * "we don't support hardware time stamps
				 * for all incoming packets" here.)
				 *
				 * We'll just fall back on the standard
				 * host time stamps.
				 */
				*status = PCAP_WARNING_TSTAMP_TYPE_NOTSUP;
				break;

			default:
				pcap_fmt_errmsg_for_errno(handle->errbuf,
				    PCAP_ERRBUF_SIZE, errno,
				    "SIOCSHWTSTAMP failed");
				*status = PCAP_ERROR;
				return -1;
			}
		} else {
			/*
			 * Well, that worked.  Now specify the type of
			 * hardware time stamp we want for this
			 * socket.
			 */
			if (handle->opt.tstamp_type == PCAP_TSTAMP_ADAPTER) {
				/*
				 * Hardware timestamp, synchronized
				 * with the system clock.
				 */
				timesource = SOF_TIMESTAMPING_SYS_HARDWARE;
			} else {
				/*
				 * PCAP_TSTAMP_ADAPTER_UNSYNCED - hardware
				 * timestamp, not synchronized with the
				 * system clock.
				 */
				timesource = SOF_TIMESTAMPING_RAW_HARDWARE;
			}
			if (setsockopt(handle->fd, SOL_PACKET, PACKET_TIMESTAMP,
				(void *)&timesource, sizeof(timesource))) {
				pcap_fmt_errmsg_for_errno(handle->errbuf,
				    PCAP_ERRBUF_SIZE, errno,
				    "can't set PACKET_TIMESTAMP");
				*status = PCAP_ERROR;
				return -1;
			}
		}
	}
#endif /* HAVE_LINUX_NET_TSTAMP_H && PACKET_TIMESTAMP */

	/* ask the kernel to create the ring */
retry:
	req.tp_block_nr = req.tp_frame_nr / frames_per_block;

	/* req.tp_frame_nr is requested to match frames_per_block*req.tp_block_nr */
	req.tp_frame_nr = req.tp_block_nr * frames_per_block;

#ifdef HAVE_TPACKET3
	/* timeout value to retire block - use the configured buffering timeout, or default if <0. */
	req.tp_retire_blk_tov = (handlep->timeout>=0)?handlep->timeout:0;
	/* private data not used */
	req.tp_sizeof_priv = 0;
	/* Rx ring - feature request bits - none (rxhash will not be filled) */
	req.tp_feature_req_word = 0;
#endif

	if (setsockopt(handle->fd, SOL_PACKET, PACKET_RX_RING,
					(void *) &req, sizeof(req))) {
		if ((errno == ENOMEM) && (req.tp_block_nr > 1)) {
			/*
			 * Memory failure; try to reduce the requested ring
			 * size.
			 *
			 * We used to reduce this by half -- do 5% instead.
			 * That may result in more iterations and a longer
			 * startup, but the user will be much happier with
			 * the resulting buffer size.
			 */
			if (req.tp_frame_nr < 20)
				req.tp_frame_nr -= 1;
			else
				req.tp_frame_nr -= req.tp_frame_nr/20;
			goto retry;
		}
		if (errno == ENOPROTOOPT) {
			/*
			 * We don't have ring buffer support in this kernel.
			 */
			return 0;
		}
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "can't create rx ring on packet socket");
		*status = PCAP_ERROR;
		return -1;
	}

	/* memory map the rx ring */
	handlep->mmapbuflen = req.tp_block_nr * req.tp_block_size;
	handlep->mmapbuf = mmap(0, handlep->mmapbuflen,
	    PROT_READ|PROT_WRITE, MAP_SHARED, handle->fd, 0);
	if (handlep->mmapbuf == MAP_FAILED) {
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "can't mmap rx ring");

		/* clear the allocated ring on error*/
		destroy_ring(handle);
		*status = PCAP_ERROR;
		return -1;
	}

	/* allocate a ring for each frame header pointer*/
	handle->cc = req.tp_frame_nr;
	handle->buffer = malloc(handle->cc * sizeof(union thdr *));
	if (!handle->buffer) {
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "can't allocate ring of frame headers");

		destroy_ring(handle);
		*status = PCAP_ERROR;
		return -1;
	}

	/* fill the header ring with proper frame ptr*/
	handle->offset = 0;
	for (i=0; i<req.tp_block_nr; ++i) {
		void *base = &handlep->mmapbuf[i*req.tp_block_size];
		for (j=0; j<frames_per_block; ++j, ++handle->offset) {
			RING_GET_CURRENT_FRAME(handle) = base;
			base += req.tp_frame_size;
		}
	}

	handle->bufsize = req.tp_frame_size;
	handle->offset = 0;
	return 1;
}

/* free all ring related resources*/
static void
destroy_ring(pcap_t *handle)
{
	struct pcap_linux *handlep = handle->priv;

	/* tell the kernel to destroy the ring*/
	struct tpacket_req req;
	memset(&req, 0, sizeof(req));
	/* do not test for setsockopt failure, as we can't recover from any error */
	(void)setsockopt(handle->fd, SOL_PACKET, PACKET_RX_RING,
				(void *) &req, sizeof(req));

	/* if ring is mapped, unmap it*/
	if (handlep->mmapbuf) {
		/* do not test for mmap failure, as we can't recover from any error */
		(void)munmap(handlep->mmapbuf, handlep->mmapbuflen);
		handlep->mmapbuf = NULL;
	}
}

/*
 * Special one-shot callback, used for pcap_next() and pcap_next_ex(),
 * for Linux mmapped capture.
 *
 * The problem is that pcap_next() and pcap_next_ex() expect the packet
 * data handed to the callback to be valid after the callback returns,
 * but pcap_read_linux_mmap() has to release that packet as soon as
 * the callback returns (otherwise, the kernel thinks there's still
 * at least one unprocessed packet available in the ring, so a select()
 * will immediately return indicating that there's data to process), so,
 * in the callback, we have to make a copy of the packet.
 *
 * Yes, this means that, if the capture is using the ring buffer, using
 * pcap_next() or pcap_next_ex() requires more copies than using
 * pcap_loop() or pcap_dispatch().  If that bothers you, don't use
 * pcap_next() or pcap_next_ex().
 */
static void
pcap_oneshot_mmap(u_char *user, const struct pcap_pkthdr *h,
    const u_char *bytes)
{
	struct oneshot_userdata *sp = (struct oneshot_userdata *)user;
	pcap_t *handle = sp->pd;
	struct pcap_linux *handlep = handle->priv;

	*sp->hdr = *h;
	memcpy(handlep->oneshot_buffer, bytes, h->caplen);
	*sp->pkt = handlep->oneshot_buffer;
}

static void
pcap_cleanup_linux_mmap( pcap_t *handle )
{
	struct pcap_linux *handlep = handle->priv;

	destroy_ring(handle);
	if (handlep->oneshot_buffer != NULL) {
		free(handlep->oneshot_buffer);
		handlep->oneshot_buffer = NULL;
	}
	pcap_cleanup_linux(handle);
}


static int
pcap_getnonblock_mmap(pcap_t *handle)
{
	struct pcap_linux *handlep = handle->priv;

	/* use negative value of timeout to indicate non blocking ops */
	return (handlep->timeout<0);
}

static int
pcap_setnonblock_mmap(pcap_t *handle, int nonblock)
{
	struct pcap_linux *handlep = handle->priv;

	/*
	 * Set the file descriptor to non-blocking mode, as we use
	 * it for sending packets.
	 */
	if (pcap_setnonblock_fd(handle, nonblock) == -1)
		return -1;

	/*
	 * Map each value to their corresponding negation to
	 * preserve the timeout value provided with pcap_set_timeout.
	 */
	if (nonblock) {
		if (handlep->timeout >= 0) {
			/*
			 * Indicate that we're switching to
			 * non-blocking mode.
			 */
			handlep->timeout = ~handlep->timeout;
		}
	} else {
		if (handlep->timeout < 0) {
			handlep->timeout = ~handlep->timeout;
		}
	}
	/* Update the timeout to use in poll(). */
	set_poll_timeout(handlep);
	return 0;
}

/*
 * Get the status field of the ring buffer frame at a specified offset.
 */
static inline int
pcap_get_ring_frame_status(pcap_t *handle, int offset)
{
	struct pcap_linux *handlep = handle->priv;
	union thdr h;

	h.raw = RING_GET_FRAME_AT(handle, offset);
	switch (handlep->tp_version) {
	case TPACKET_V1:
		return (h.h1->tp_status);
		break;
	case TPACKET_V1_64:
		return (h.h1_64->tp_status);
		break;
#ifdef HAVE_TPACKET2
	case TPACKET_V2:
		return (h.h2->tp_status);
		break;
#endif
#ifdef HAVE_TPACKET3
	case TPACKET_V3:
		return (h.h3->hdr.bh1.block_status);
		break;
#endif
	}
	/* This should not happen. */
	return 0;
}

#ifndef POLLRDHUP
#define POLLRDHUP 0
#endif

/*
 * Block waiting for frames to be available.
 */
static int pcap_wait_for_frames_mmap(pcap_t *handle)
{
	struct pcap_linux *handlep = handle->priv;
	char c;
	struct pollfd pollinfo;
	int ret;

	pollinfo.fd = handle->fd;
	pollinfo.events = POLLIN;

	do {
		/*
		 * Yes, we do this even in non-blocking mode, as it's
		 * the only way to get error indications from a
		 * tpacket socket.
		 *
		 * The timeout is 0 in non-blocking mode, so poll()
		 * returns immediately.
		 */
		ret = poll(&pollinfo, 1, handlep->poll_timeout);
		if (ret < 0 && errno != EINTR) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno,
			    "can't poll on packet socket");
			return PCAP_ERROR;
		} else if (ret > 0 &&
			(pollinfo.revents & (POLLHUP|POLLRDHUP|POLLERR|POLLNVAL))) {
			/*
			 * There's some indication other than
			 * "you can read on this descriptor" on
			 * the descriptor.
			 */
			if (pollinfo.revents & (POLLHUP | POLLRDHUP)) {
				pcap_snprintf(handle->errbuf,
					PCAP_ERRBUF_SIZE,
					"Hangup on packet socket");
				return PCAP_ERROR;
			}
			if (pollinfo.revents & POLLERR) {
				/*
				 * A recv() will give us the actual error code.
				 *
				 * XXX - make the socket non-blocking?
				 */
				if (recv(handle->fd, &c, sizeof c,
					MSG_PEEK) != -1)
					continue;	/* what, no error? */
				if (errno == ENETDOWN) {
					/*
					 * The device on which we're
					 * capturing went away.
					 *
					 * XXX - we should really return
					 * PCAP_ERROR_IFACE_NOT_UP, but
					 * pcap_dispatch() etc. aren't
					 * defined to return that.
					 */
					pcap_snprintf(handle->errbuf,
						PCAP_ERRBUF_SIZE,
						"The interface went down");
				} else {
					pcap_fmt_errmsg_for_errno(handle->errbuf,
					    PCAP_ERRBUF_SIZE, errno,
					    "Error condition on packet socket");
				}
				return PCAP_ERROR;
			}
			if (pollinfo.revents & POLLNVAL) {
				pcap_snprintf(handle->errbuf,
					PCAP_ERRBUF_SIZE,
					"Invalid polling request on packet socket");
				return PCAP_ERROR;
			}
		}
		/* check for break loop condition on interrupted syscall*/
		if (handle->break_loop) {
			handle->break_loop = 0;
			return PCAP_ERROR_BREAK;
		}
	} while (ret < 0);
	return 0;
}

/* handle a single memory mapped packet */
static int pcap_handle_packet_mmap(
		pcap_t *handle,
		pcap_handler callback,
		u_char *user,
		unsigned char *frame,
		unsigned int tp_len,
		unsigned int tp_mac,
		unsigned int tp_snaplen,
		unsigned int tp_sec,
		unsigned int tp_usec,
		int tp_vlan_tci_valid,
		__u16 tp_vlan_tci,
		__u16 tp_vlan_tpid)
{
	struct pcap_linux *handlep = handle->priv;
	unsigned char *bp;
	struct sockaddr_ll *sll;
	struct pcap_pkthdr pcaphdr;
	unsigned int snaplen = tp_snaplen;

	/* perform sanity check on internal offset. */
	if (tp_mac + tp_snaplen > handle->bufsize) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
			"corrupted frame on kernel ring mac "
			"offset %u + caplen %u > frame len %d",
			tp_mac, tp_snaplen, handle->bufsize);
		return -1;
	}

	/* run filter on received packet
	 * If the kernel filtering is enabled we need to run the
	 * filter until all the frames present into the ring
	 * at filter creation time are processed.
	 * In this case, blocks_to_filter_in_userland is used
	 * as a counter for the packet we need to filter.
	 * Note: alternatively it could be possible to stop applying
	 * the filter when the ring became empty, but it can possibly
	 * happen a lot later... */
	bp = frame + tp_mac;

	/* if required build in place the sll header*/
	sll = (void *)frame + TPACKET_ALIGN(handlep->tp_hdrlen);
	if (handlep->cooked) {
		struct sll_header *hdrp;

		/*
		 * The kernel should have left us with enough
		 * space for an sll header; back up the packet
		 * data pointer into that space, as that'll be
		 * the beginning of the packet we pass to the
		 * callback.
		 */
		bp -= SLL_HDR_LEN;

		/*
		 * Let's make sure that's past the end of
		 * the tpacket header, i.e. >=
		 * ((u_char *)thdr + TPACKET_HDRLEN), so we
		 * don't step on the header when we construct
		 * the sll header.
		 */
		if (bp < (u_char *)frame +
				   TPACKET_ALIGN(handlep->tp_hdrlen) +
				   sizeof(struct sockaddr_ll)) {
			pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
				"cooked-mode frame doesn't have room for sll header");
			return -1;
		}

		/*
		 * OK, that worked; construct the sll header.
		 */
		hdrp = (struct sll_header *)bp;
		hdrp->sll_pkttype = map_packet_type_to_sll_type(
						sll->sll_pkttype);
		hdrp->sll_hatype = htons(sll->sll_hatype);
		hdrp->sll_halen = htons(sll->sll_halen);
		memcpy(hdrp->sll_addr, sll->sll_addr, SLL_ADDRLEN);
		hdrp->sll_protocol = sll->sll_protocol;

		snaplen += sizeof(struct sll_header);
	}

	if (handlep->filter_in_userland && handle->fcode.bf_insns) {
		struct bpf_aux_data aux_data;

		aux_data.vlan_tag_present = tp_vlan_tci_valid;
		aux_data.vlan_tag = tp_vlan_tci & 0x0fff;

		if (bpf_filter_with_aux_data(handle->fcode.bf_insns,
					     bp,
					     tp_len,
					     snaplen,
					     &aux_data) == 0)
			return 0;
	}

	if (!linux_check_direction(handle, sll))
		return 0;

	/* get required packet info from ring header */
	pcaphdr.ts.tv_sec = tp_sec;
	pcaphdr.ts.tv_usec = tp_usec;
	pcaphdr.caplen = tp_snaplen;
	pcaphdr.len = tp_len;

	/* if required build in place the sll header*/
	if (handlep->cooked) {
		/* update packet len */
		pcaphdr.caplen += SLL_HDR_LEN;
		pcaphdr.len += SLL_HDR_LEN;
	}

#if defined(HAVE_TPACKET2) || defined(HAVE_TPACKET3)
	if (tp_vlan_tci_valid &&
		handlep->vlan_offset != -1 &&
		tp_snaplen >= (unsigned int) handlep->vlan_offset)
	{
		struct vlan_tag *tag;

		/*
		 * Move everything in the header, except the type field,
		 * down VLAN_TAG_LEN bytes, to allow us to insert the
		 * VLAN tag between that stuff and the type field.
		 */
		bp -= VLAN_TAG_LEN;
		memmove(bp, bp + VLAN_TAG_LEN, handlep->vlan_offset);

		/*
		 * Now insert the tag.
		 */
		tag = (struct vlan_tag *)(bp + handlep->vlan_offset);
		tag->vlan_tpid = htons(tp_vlan_tpid);
		tag->vlan_tci = htons(tp_vlan_tci);

		/*
		 * Add the tag to the packet lengths.
		 */
		pcaphdr.caplen += VLAN_TAG_LEN;
		pcaphdr.len += VLAN_TAG_LEN;
	}
#endif

	/*
	 * The only way to tell the kernel to cut off the
	 * packet at a snapshot length is with a filter program;
	 * if there's no filter program, the kernel won't cut
	 * the packet off.
	 *
	 * Trim the snapshot length to be no longer than the
	 * specified snapshot length.
	 */
	if (pcaphdr.caplen > (bpf_u_int32)handle->snapshot)
		pcaphdr.caplen = handle->snapshot;

	/* pass the packet to the user */
	callback(user, &pcaphdr, bp);

	return 1;
}

static int
pcap_read_linux_mmap_v1(pcap_t *handle, int max_packets, pcap_handler callback,
		u_char *user)
{
	struct pcap_linux *handlep = handle->priv;
	union thdr h;
	int pkts = 0;
	int ret;

	/* wait for frames availability.*/
	h.raw = RING_GET_CURRENT_FRAME(handle);
	if (h.h1->tp_status == TP_STATUS_KERNEL) {
		/*
		 * The current frame is owned by the kernel; wait for
		 * a frame to be handed to us.
		 */
		ret = pcap_wait_for_frames_mmap(handle);
		if (ret) {
			return ret;
		}
	}

	/* non-positive values of max_packets are used to require all
	 * packets currently available in the ring */
	while ((pkts < max_packets) || PACKET_COUNT_IS_UNLIMITED(max_packets)) {
		/*
		 * Get the current ring buffer frame, and break if
		 * it's still owned by the kernel.
		 */
		h.raw = RING_GET_CURRENT_FRAME(handle);
		if (h.h1->tp_status == TP_STATUS_KERNEL)
			break;

		ret = pcap_handle_packet_mmap(
				handle,
				callback,
				user,
				h.raw,
				h.h1->tp_len,
				h.h1->tp_mac,
				h.h1->tp_snaplen,
				h.h1->tp_sec,
				h.h1->tp_usec,
				0,
				0,
				0);
		if (ret == 1) {
			pkts++;
			handlep->packets_read++;
		} else if (ret < 0) {
			return ret;
		}

		/*
		 * Hand this block back to the kernel, and, if we're
		 * counting blocks that need to be filtered in userland
		 * after having been filtered by the kernel, count
		 * the one we've just processed.
		 */
		h.h1->tp_status = TP_STATUS_KERNEL;
		if (handlep->blocks_to_filter_in_userland > 0) {
			handlep->blocks_to_filter_in_userland--;
			if (handlep->blocks_to_filter_in_userland == 0) {
				/*
				 * No more blocks need to be filtered
				 * in userland.
				 */
				handlep->filter_in_userland = 0;
			}
		}

		/* next block */
		if (++handle->offset >= handle->cc)
			handle->offset = 0;

		/* check for break loop condition*/
		if (handle->break_loop) {
			handle->break_loop = 0;
			return PCAP_ERROR_BREAK;
		}
	}
	return pkts;
}

static int
pcap_read_linux_mmap_v1_64(pcap_t *handle, int max_packets, pcap_handler callback,
		u_char *user)
{
	struct pcap_linux *handlep = handle->priv;
	union thdr h;
	int pkts = 0;
	int ret;

	/* wait for frames availability.*/
	h.raw = RING_GET_CURRENT_FRAME(handle);
	if (h.h1_64->tp_status == TP_STATUS_KERNEL) {
		/*
		 * The current frame is owned by the kernel; wait for
		 * a frame to be handed to us.
		 */
		ret = pcap_wait_for_frames_mmap(handle);
		if (ret) {
			return ret;
		}
	}

	/* non-positive values of max_packets are used to require all
	 * packets currently available in the ring */
	while ((pkts < max_packets) || PACKET_COUNT_IS_UNLIMITED(max_packets)) {
		/*
		 * Get the current ring buffer frame, and break if
		 * it's still owned by the kernel.
		 */
		h.raw = RING_GET_CURRENT_FRAME(handle);
		if (h.h1_64->tp_status == TP_STATUS_KERNEL)
			break;

		ret = pcap_handle_packet_mmap(
				handle,
				callback,
				user,
				h.raw,
				h.h1_64->tp_len,
				h.h1_64->tp_mac,
				h.h1_64->tp_snaplen,
				h.h1_64->tp_sec,
				h.h1_64->tp_usec,
				0,
				0,
				0);
		if (ret == 1) {
			pkts++;
			handlep->packets_read++;
		} else if (ret < 0) {
			return ret;
		}

		/*
		 * Hand this block back to the kernel, and, if we're
		 * counting blocks that need to be filtered in userland
		 * after having been filtered by the kernel, count
		 * the one we've just processed.
		 */
		h.h1_64->tp_status = TP_STATUS_KERNEL;
		if (handlep->blocks_to_filter_in_userland > 0) {
			handlep->blocks_to_filter_in_userland--;
			if (handlep->blocks_to_filter_in_userland == 0) {
				/*
				 * No more blocks need to be filtered
				 * in userland.
				 */
				handlep->filter_in_userland = 0;
			}
		}

		/* next block */
		if (++handle->offset >= handle->cc)
			handle->offset = 0;

		/* check for break loop condition*/
		if (handle->break_loop) {
			handle->break_loop = 0;
			return PCAP_ERROR_BREAK;
		}
	}
	return pkts;
}

#ifdef HAVE_TPACKET2
static int
pcap_read_linux_mmap_v2(pcap_t *handle, int max_packets, pcap_handler callback,
		u_char *user)
{
	struct pcap_linux *handlep = handle->priv;
	union thdr h;
	int pkts = 0;
	int ret;

	/* wait for frames availability.*/
	h.raw = RING_GET_CURRENT_FRAME(handle);
	if (h.h2->tp_status == TP_STATUS_KERNEL) {
		/*
		 * The current frame is owned by the kernel; wait for
		 * a frame to be handed to us.
		 */
		ret = pcap_wait_for_frames_mmap(handle);
		if (ret) {
			return ret;
		}
	}

	/* non-positive values of max_packets are used to require all
	 * packets currently available in the ring */
	while ((pkts < max_packets) || PACKET_COUNT_IS_UNLIMITED(max_packets)) {
		/*
		 * Get the current ring buffer frame, and break if
		 * it's still owned by the kernel.
		 */
		h.raw = RING_GET_CURRENT_FRAME(handle);
		if (h.h2->tp_status == TP_STATUS_KERNEL)
			break;

		ret = pcap_handle_packet_mmap(
				handle,
				callback,
				user,
				h.raw,
				h.h2->tp_len,
				h.h2->tp_mac,
				h.h2->tp_snaplen,
				h.h2->tp_sec,
				handle->opt.tstamp_precision == PCAP_TSTAMP_PRECISION_NANO ? h.h2->tp_nsec : h.h2->tp_nsec / 1000,
				VLAN_VALID(h.h2, h.h2),
				h.h2->tp_vlan_tci,
				VLAN_TPID(h.h2, h.h2));
		if (ret == 1) {
			pkts++;
			handlep->packets_read++;
		} else if (ret < 0) {
			return ret;
		}

		/*
		 * Hand this block back to the kernel, and, if we're
		 * counting blocks that need to be filtered in userland
		 * after having been filtered by the kernel, count
		 * the one we've just processed.
		 */
		h.h2->tp_status = TP_STATUS_KERNEL;
		if (handlep->blocks_to_filter_in_userland > 0) {
			handlep->blocks_to_filter_in_userland--;
			if (handlep->blocks_to_filter_in_userland == 0) {
				/*
				 * No more blocks need to be filtered
				 * in userland.
				 */
				handlep->filter_in_userland = 0;
			}
		}

		/* next block */
		if (++handle->offset >= handle->cc)
			handle->offset = 0;

		/* check for break loop condition*/
		if (handle->break_loop) {
			handle->break_loop = 0;
			return PCAP_ERROR_BREAK;
		}
	}
	return pkts;
}
#endif /* HAVE_TPACKET2 */

#ifdef HAVE_TPACKET3
static int
pcap_read_linux_mmap_v3(pcap_t *handle, int max_packets, pcap_handler callback,
		u_char *user)
{
	struct pcap_linux *handlep = handle->priv;
	union thdr h;
	int pkts = 0;
	int ret;

again:
	if (handlep->current_packet == NULL) {
		/* wait for frames availability.*/
		h.raw = RING_GET_CURRENT_FRAME(handle);
		if (h.h3->hdr.bh1.block_status == TP_STATUS_KERNEL) {
			/*
			 * The current frame is owned by the kernel; wait
			 * for a frame to be handed to us.
			 */
			ret = pcap_wait_for_frames_mmap(handle);
			if (ret) {
				return ret;
			}
		}
	}
	h.raw = RING_GET_CURRENT_FRAME(handle);
	if (h.h3->hdr.bh1.block_status == TP_STATUS_KERNEL) {
		if (pkts == 0 && handlep->timeout == 0) {
			/* Block until we see a packet. */
			goto again;
		}
		return pkts;
	}

	/* non-positive values of max_packets are used to require all
	 * packets currently available in the ring */
	while ((pkts < max_packets) || PACKET_COUNT_IS_UNLIMITED(max_packets)) {
		int packets_to_read;

		if (handlep->current_packet == NULL) {
			h.raw = RING_GET_CURRENT_FRAME(handle);
			if (h.h3->hdr.bh1.block_status == TP_STATUS_KERNEL)
				break;

			handlep->current_packet = h.raw + h.h3->hdr.bh1.offset_to_first_pkt;
			handlep->packets_left = h.h3->hdr.bh1.num_pkts;
		}
		packets_to_read = handlep->packets_left;

		if (!PACKET_COUNT_IS_UNLIMITED(max_packets) &&
		    packets_to_read > (max_packets - pkts)) {
			/*
			 * We've been given a maximum number of packets
			 * to process, and there are more packets in
			 * this buffer than that.  Only process enough
			 * of them to get us up to that maximum.
			 */
			packets_to_read = max_packets - pkts;
		}

		while (packets_to_read-- && !handle->break_loop) {
			struct tpacket3_hdr* tp3_hdr = (struct tpacket3_hdr*) handlep->current_packet;
			ret = pcap_handle_packet_mmap(
					handle,
					callback,
					user,
					handlep->current_packet,
					tp3_hdr->tp_len,
					tp3_hdr->tp_mac,
					tp3_hdr->tp_snaplen,
					tp3_hdr->tp_sec,
					handle->opt.tstamp_precision == PCAP_TSTAMP_PRECISION_NANO ? tp3_hdr->tp_nsec : tp3_hdr->tp_nsec / 1000,
					VLAN_VALID(tp3_hdr, &tp3_hdr->hv1),
					tp3_hdr->hv1.tp_vlan_tci,
					VLAN_TPID(tp3_hdr, &tp3_hdr->hv1));
			if (ret == 1) {
				pkts++;
				handlep->packets_read++;
			} else if (ret < 0) {
				handlep->current_packet = NULL;
				return ret;
			}
			handlep->current_packet += tp3_hdr->tp_next_offset;
			handlep->packets_left--;
		}

		if (handlep->packets_left <= 0) {
			/*
			 * Hand this block back to the kernel, and, if
			 * we're counting blocks that need to be
			 * filtered in userland after having been
			 * filtered by the kernel, count the one we've
			 * just processed.
			 */
			h.h3->hdr.bh1.block_status = TP_STATUS_KERNEL;
			if (handlep->blocks_to_filter_in_userland > 0) {
				handlep->blocks_to_filter_in_userland--;
				if (handlep->blocks_to_filter_in_userland == 0) {
					/*
					 * No more blocks need to be filtered
					 * in userland.
					 */
					handlep->filter_in_userland = 0;
				}
			}

			/* next block */
			if (++handle->offset >= handle->cc)
				handle->offset = 0;

			handlep->current_packet = NULL;
		}

		/* check for break loop condition*/
		if (handle->break_loop) {
			handle->break_loop = 0;
			return PCAP_ERROR_BREAK;
		}
	}
	if (pkts == 0 && handlep->timeout == 0) {
		/* Block until we see a packet. */
		goto again;
	}
	return pkts;
}
#endif /* HAVE_TPACKET3 */

static int
pcap_setfilter_linux_mmap(pcap_t *handle, struct bpf_program *filter)
{
	struct pcap_linux *handlep = handle->priv;
	int n, offset;
	int ret;

	/*
	 * Don't rewrite "ret" instructions; we don't need to, as
	 * we're not reading packets with recvmsg(), and we don't
	 * want to, as, by not rewriting them, the kernel can avoid
	 * copying extra data.
	 */
	ret = pcap_setfilter_linux_common(handle, filter, 1);
	if (ret < 0)
		return ret;

	/*
	 * If we're filtering in userland, there's nothing to do;
	 * the new filter will be used for the next packet.
	 */
	if (handlep->filter_in_userland)
		return ret;

	/*
	 * We're filtering in the kernel; the packets present in
	 * all blocks currently in the ring were already filtered
	 * by the old filter, and so will need to be filtered in
	 * userland by the new filter.
	 *
	 * Get an upper bound for the number of such blocks; first,
	 * walk the ring backward and count the free blocks.
	 */
	offset = handle->offset;
	if (--offset < 0)
		offset = handle->cc - 1;
	for (n=0; n < handle->cc; ++n) {
		if (--offset < 0)
			offset = handle->cc - 1;
		if (pcap_get_ring_frame_status(handle, offset) != TP_STATUS_KERNEL)
			break;
	}

	/*
	 * If we found free blocks, decrement the count of free
	 * blocks by 1, just in case we lost a race with another
	 * thread of control that was adding a packet while
	 * we were counting and that had run the filter before
	 * we changed it.
	 *
	 * XXX - could there be more than one block added in
	 * this fashion?
	 *
	 * XXX - is there a way to avoid that race, e.g. somehow
	 * wait for all packets that passed the old filter to
	 * be added to the ring?
	 */
	if (n != 0)
		n--;

	/*
	 * Set the count of blocks worth of packets to filter
	 * in userland to the total number of blocks in the
	 * ring minus the number of free blocks we found, and
	 * turn on userland filtering.  (The count of blocks
	 * worth of packets to filter in userland is guaranteed
	 * not to be zero - n, above, couldn't be set to a
	 * value > handle->cc, and if it were equal to
	 * handle->cc, it wouldn't be zero, and thus would
	 * be decremented to handle->cc - 1.)
	 */
	handlep->blocks_to_filter_in_userland = handle->cc - n;
	handlep->filter_in_userland = 1;
	return ret;
}

#endif /* HAVE_PACKET_RING */


#ifdef HAVE_PF_PACKET_SOCKETS
/*
 *  Return the index of the given device name. Fill ebuf and return
 *  -1 on failure.
 */
static int
iface_get_id(int fd, const char *device, char *ebuf)
{
	struct ifreq	ifr;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));

	if (ioctl(fd, SIOCGIFINDEX, &ifr) == -1) {
		pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
		    errno, "SIOCGIFINDEX");
		return -1;
	}

	return ifr.ifr_ifindex;
}

/*
 *  Bind the socket associated with FD to the given device.
 *  Return 1 on success, 0 if we should try a SOCK_PACKET socket,
 *  or a PCAP_ERROR_ value on a hard error.
 */
static int
iface_bind(int fd, int ifindex, char *ebuf, int protocol)
{
	struct sockaddr_ll	sll;
	int			err;
	socklen_t		errlen = sizeof(err);

	memset(&sll, 0, sizeof(sll));
	sll.sll_family		= AF_PACKET;
	sll.sll_ifindex		= ifindex;
	sll.sll_protocol	= protocol;

	if (bind(fd, (struct sockaddr *) &sll, sizeof(sll)) == -1) {
		if (errno == ENETDOWN) {
			/*
			 * Return a "network down" indication, so that
			 * the application can report that rather than
			 * saying we had a mysterious failure and
			 * suggest that they report a problem to the
			 * libpcap developers.
			 */
			return PCAP_ERROR_IFACE_NOT_UP;
		} else {
			pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
			    errno, "bind");
			return PCAP_ERROR;
		}
	}

	/* Any pending errors, e.g., network is down? */

	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) == -1) {
		pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
		    errno, "getsockopt");
		return 0;
	}

	if (err == ENETDOWN) {
		/*
		 * Return a "network down" indication, so that
		 * the application can report that rather than
		 * saying we had a mysterious failure and
		 * suggest that they report a problem to the
		 * libpcap developers.
		 */
		return PCAP_ERROR_IFACE_NOT_UP;
	} else if (err > 0) {
		pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
		    err, "bind");
		return 0;
	}

	return 1;
}

#ifdef IW_MODE_MONITOR
/*
 * Check whether the device supports the Wireless Extensions.
 * Returns 1 if it does, 0 if it doesn't, PCAP_ERROR_NO_SUCH_DEVICE
 * if the device doesn't even exist.
 */
static int
has_wext(int sock_fd, const char *device, char *ebuf)
{
	struct iwreq ireq;

	if (is_bonding_device(sock_fd, device))
		return 0;	/* bonding device, so don't even try */

	strlcpy(ireq.ifr_ifrn.ifrn_name, device,
	    sizeof ireq.ifr_ifrn.ifrn_name);
	if (ioctl(sock_fd, SIOCGIWNAME, &ireq) >= 0)
		return 1;	/* yes */
	pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE, errno,
	    "%s: SIOCGIWNAME", device);
	if (errno == ENODEV)
		return PCAP_ERROR_NO_SUCH_DEVICE;
	return 0;
}

/*
 * Per me si va ne la citta dolente,
 * Per me si va ne l'etterno dolore,
 *	...
 * Lasciate ogne speranza, voi ch'intrate.
 *
 * XXX - airmon-ng does special stuff with the Orinoco driver and the
 * wlan-ng driver.
 */
typedef enum {
	MONITOR_WEXT,
	MONITOR_HOSTAP,
	MONITOR_PRISM,
	MONITOR_PRISM54,
	MONITOR_ACX100,
	MONITOR_RT2500,
	MONITOR_RT2570,
	MONITOR_RT73,
	MONITOR_RTL8XXX
} monitor_type;

/*
 * Use the Wireless Extensions, if we have them, to try to turn monitor mode
 * on if it's not already on.
 *
 * Returns 1 on success, 0 if we don't support the Wireless Extensions
 * on this device, or a PCAP_ERROR_ value if we do support them but
 * we weren't able to turn monitor mode on.
 */
static int
enter_rfmon_mode_wext(pcap_t *handle, int sock_fd, const char *device)
{
	/*
	 * XXX - at least some adapters require non-Wireless Extensions
	 * mechanisms to turn monitor mode on.
	 *
	 * Atheros cards might require that a separate "monitor virtual access
	 * point" be created, with later versions of the madwifi driver.
	 * airmon-ng does "wlanconfig ath create wlandev {if} wlanmode
	 * monitor -bssid", which apparently spits out a line "athN"
	 * where "athN" is the monitor mode device.  To leave monitor
	 * mode, it destroys the monitor mode device.
	 *
	 * Some Intel Centrino adapters might require private ioctls to get
	 * radio headers; the ipw2200 and ipw3945 drivers allow you to
	 * configure a separate "rtapN" interface to capture in monitor
	 * mode without preventing the adapter from operating normally.
	 * (airmon-ng doesn't appear to use that, though.)
	 *
	 * It would be Truly Wonderful if mac80211 and nl80211 cleaned this
	 * up, and if all drivers were converted to mac80211 drivers.
	 *
	 * If interface {if} is a mac80211 driver, the file
	 * /sys/class/net/{if}/phy80211 is a symlink to
	 * /sys/class/ieee80211/{phydev}, for some {phydev}.
	 *
	 * On Fedora 9, with a 2.6.26.3-29 kernel, my Zydas stick, at
	 * least, has a "wmaster0" device and a "wlan0" device; the
	 * latter is the one with the IP address.  Both show up in
	 * "tcpdump -D" output.  Capturing on the wmaster0 device
	 * captures with 802.11 headers.
	 *
	 * airmon-ng searches through /sys/class/net for devices named
	 * monN, starting with mon0; as soon as one *doesn't* exist,
	 * it chooses that as the monitor device name.  If the "iw"
	 * command exists, it does "iw dev {if} interface add {monif}
	 * type monitor", where {monif} is the monitor device.  It
	 * then (sigh) sleeps .1 second, and then configures the
	 * device up.  Otherwise, if /sys/class/ieee80211/{phydev}/add_iface
	 * is a file, it writes {mondev}, without a newline, to that file,
	 * and again (sigh) sleeps .1 second, and then iwconfig's that
	 * device into monitor mode and configures it up.  Otherwise,
	 * you can't do monitor mode.
	 *
	 * All these devices are "glued" together by having the
	 * /sys/class/net/{device}/phy80211 links pointing to the same
	 * place, so, given a wmaster, wlan, or mon device, you can
	 * find the other devices by looking for devices with
	 * the same phy80211 link.
	 *
	 * To turn monitor mode off, delete the monitor interface,
	 * either with "iw dev {monif} interface del" or by sending
	 * {monif}, with no NL, down /sys/class/ieee80211/{phydev}/remove_iface
	 *
	 * Note: if you try to create a monitor device named "monN", and
	 * there's already a "monN" device, it fails, as least with
	 * the netlink interface (which is what iw uses), with a return
	 * value of -ENFILE.  (Return values are negative errnos.)  We
	 * could probably use that to find an unused device.
	 */
	struct pcap_linux *handlep = handle->priv;
	int err;
	struct iwreq ireq;
	struct iw_priv_args *priv;
	monitor_type montype;
	int i;
	__u32 cmd;
	struct ifreq ifr;
	int oldflags;
	int args[2];
	int channel;

	/*
	 * Does this device *support* the Wireless Extensions?
	 */
	err = has_wext(sock_fd, device, handle->errbuf);
	if (err <= 0)
		return err;	/* either it doesn't or the device doesn't even exist */
	/*
	 * Start out assuming we have no private extensions to control
	 * radio metadata.
	 */
	montype = MONITOR_WEXT;
	cmd = 0;

	/*
	 * Try to get all the Wireless Extensions private ioctls
	 * supported by this device.
	 *
	 * First, get the size of the buffer we need, by supplying no
	 * buffer and a length of 0.  If the device supports private
	 * ioctls, it should return E2BIG, with ireq.u.data.length set
	 * to the length we need.  If it doesn't support them, it should
	 * return EOPNOTSUPP.
	 */
	memset(&ireq, 0, sizeof ireq);
	strlcpy(ireq.ifr_ifrn.ifrn_name, device,
	    sizeof ireq.ifr_ifrn.ifrn_name);
	ireq.u.data.pointer = (void *)args;
	ireq.u.data.length = 0;
	ireq.u.data.flags = 0;
	if (ioctl(sock_fd, SIOCGIWPRIV, &ireq) != -1) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
		    "%s: SIOCGIWPRIV with a zero-length buffer didn't fail!",
		    device);
		return PCAP_ERROR;
	}
	if (errno != EOPNOTSUPP) {
		/*
		 * OK, it's not as if there are no private ioctls.
		 */
		if (errno != E2BIG) {
			/*
			 * Failed.
			 */
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "%s: SIOCGIWPRIV", device);
			return PCAP_ERROR;
		}

		/*
		 * OK, try to get the list of private ioctls.
		 */
		priv = malloc(ireq.u.data.length * sizeof (struct iw_priv_args));
		if (priv == NULL) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "malloc");
			return PCAP_ERROR;
		}
		ireq.u.data.pointer = (void *)priv;
		if (ioctl(sock_fd, SIOCGIWPRIV, &ireq) == -1) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "%s: SIOCGIWPRIV", device);
			free(priv);
			return PCAP_ERROR;
		}

		/*
		 * Look for private ioctls to turn monitor mode on or, if
		 * monitor mode is on, to set the header type.
		 */
		for (i = 0; i < ireq.u.data.length; i++) {
			if (strcmp(priv[i].name, "monitor_type") == 0) {
				/*
				 * Hostap driver, use this one.
				 * Set monitor mode first.
				 * You can set it to 0 to get DLT_IEEE80211,
				 * 1 to get DLT_PRISM, 2 to get
				 * DLT_IEEE80211_RADIO_AVS, and, with more
				 * recent versions of the driver, 3 to get
				 * DLT_IEEE80211_RADIO.
				 */
				if ((priv[i].set_args & IW_PRIV_TYPE_MASK) != IW_PRIV_TYPE_INT)
					break;
				if (!(priv[i].set_args & IW_PRIV_SIZE_FIXED))
					break;
				if ((priv[i].set_args & IW_PRIV_SIZE_MASK) != 1)
					break;
				montype = MONITOR_HOSTAP;
				cmd = priv[i].cmd;
				break;
			}
			if (strcmp(priv[i].name, "set_prismhdr") == 0) {
				/*
				 * Prism54 driver, use this one.
				 * Set monitor mode first.
				 * You can set it to 2 to get DLT_IEEE80211
				 * or 3 or get DLT_PRISM.
				 */
				if ((priv[i].set_args & IW_PRIV_TYPE_MASK) != IW_PRIV_TYPE_INT)
					break;
				if (!(priv[i].set_args & IW_PRIV_SIZE_FIXED))
					break;
				if ((priv[i].set_args & IW_PRIV_SIZE_MASK) != 1)
					break;
				montype = MONITOR_PRISM54;
				cmd = priv[i].cmd;
				break;
			}
			if (strcmp(priv[i].name, "forceprismheader") == 0) {
				/*
				 * RT2570 driver, use this one.
				 * Do this after turning monitor mode on.
				 * You can set it to 1 to get DLT_PRISM or 2
				 * to get DLT_IEEE80211.
				 */
				if ((priv[i].set_args & IW_PRIV_TYPE_MASK) != IW_PRIV_TYPE_INT)
					break;
				if (!(priv[i].set_args & IW_PRIV_SIZE_FIXED))
					break;
				if ((priv[i].set_args & IW_PRIV_SIZE_MASK) != 1)
					break;
				montype = MONITOR_RT2570;
				cmd = priv[i].cmd;
				break;
			}
			if (strcmp(priv[i].name, "forceprism") == 0) {
				/*
				 * RT73 driver, use this one.
				 * Do this after turning monitor mode on.
				 * Its argument is a *string*; you can
				 * set it to "1" to get DLT_PRISM or "2"
				 * to get DLT_IEEE80211.
				 */
				if ((priv[i].set_args & IW_PRIV_TYPE_MASK) != IW_PRIV_TYPE_CHAR)
					break;
				if (priv[i].set_args & IW_PRIV_SIZE_FIXED)
					break;
				montype = MONITOR_RT73;
				cmd = priv[i].cmd;
				break;
			}
			if (strcmp(priv[i].name, "prismhdr") == 0) {
				/*
				 * One of the RTL8xxx drivers, use this one.
				 * It can only be done after monitor mode
				 * has been turned on.  You can set it to 1
				 * to get DLT_PRISM or 0 to get DLT_IEEE80211.
				 */
				if ((priv[i].set_args & IW_PRIV_TYPE_MASK) != IW_PRIV_TYPE_INT)
					break;
				if (!(priv[i].set_args & IW_PRIV_SIZE_FIXED))
					break;
				if ((priv[i].set_args & IW_PRIV_SIZE_MASK) != 1)
					break;
				montype = MONITOR_RTL8XXX;
				cmd = priv[i].cmd;
				break;
			}
			if (strcmp(priv[i].name, "rfmontx") == 0) {
				/*
				 * RT2500 or RT61 driver, use this one.
				 * It has one one-byte parameter; set
				 * u.data.length to 1 and u.data.pointer to
				 * point to the parameter.
				 * It doesn't itself turn monitor mode on.
				 * You can set it to 1 to allow transmitting
				 * in monitor mode(?) and get DLT_IEEE80211,
				 * or set it to 0 to disallow transmitting in
				 * monitor mode(?) and get DLT_PRISM.
				 */
				if ((priv[i].set_args & IW_PRIV_TYPE_MASK) != IW_PRIV_TYPE_INT)
					break;
				if ((priv[i].set_args & IW_PRIV_SIZE_MASK) != 2)
					break;
				montype = MONITOR_RT2500;
				cmd = priv[i].cmd;
				break;
			}
			if (strcmp(priv[i].name, "monitor") == 0) {
				/*
				 * Either ACX100 or hostap, use this one.
				 * It turns monitor mode on.
				 * If it takes two arguments, it's ACX100;
				 * the first argument is 1 for DLT_PRISM
				 * or 2 for DLT_IEEE80211, and the second
				 * argument is the channel on which to
				 * run.  If it takes one argument, it's
				 * HostAP, and the argument is 2 for
				 * DLT_IEEE80211 and 3 for DLT_PRISM.
				 *
				 * If we see this, we don't quit, as this
				 * might be a version of the hostap driver
				 * that also supports "monitor_type".
				 */
				if ((priv[i].set_args & IW_PRIV_TYPE_MASK) != IW_PRIV_TYPE_INT)
					break;
				if (!(priv[i].set_args & IW_PRIV_SIZE_FIXED))
					break;
				switch (priv[i].set_args & IW_PRIV_SIZE_MASK) {

				case 1:
					montype = MONITOR_PRISM;
					cmd = priv[i].cmd;
					break;

				case 2:
					montype = MONITOR_ACX100;
					cmd = priv[i].cmd;
					break;

				default:
					break;
				}
			}
		}
		free(priv);
	}

	/*
	 * XXX - ipw3945?  islism?
	 */

	/*
	 * Get the old mode.
	 */
	strlcpy(ireq.ifr_ifrn.ifrn_name, device,
	    sizeof ireq.ifr_ifrn.ifrn_name);
	if (ioctl(sock_fd, SIOCGIWMODE, &ireq) == -1) {
		/*
		 * We probably won't be able to set the mode, either.
		 */
		return PCAP_ERROR_RFMON_NOTSUP;
	}

	/*
	 * Is it currently in monitor mode?
	 */
	if (ireq.u.mode == IW_MODE_MONITOR) {
		/*
		 * Yes.  Just leave things as they are.
		 * We don't offer multiple link-layer types, as
		 * changing the link-layer type out from under
		 * somebody else capturing in monitor mode would
		 * be considered rude.
		 */
		return 1;
	}
	/*
	 * No.  We have to put the adapter into rfmon mode.
	 */

	/*
	 * If we haven't already done so, arrange to have
	 * "pcap_close_all()" called when we exit.
	 */
	if (!pcap_do_addexit(handle)) {
		/*
		 * "atexit()" failed; don't put the interface
		 * in rfmon mode, just give up.
		 */
		return PCAP_ERROR_RFMON_NOTSUP;
	}

	/*
	 * Save the old mode.
	 */
	handlep->oldmode = ireq.u.mode;

	/*
	 * Put the adapter in rfmon mode.  How we do this depends
	 * on whether we have a special private ioctl or not.
	 */
	if (montype == MONITOR_PRISM) {
		/*
		 * We have the "monitor" private ioctl, but none of
		 * the other private ioctls.  Use this, and select
		 * the Prism header.
		 *
		 * If it fails, just fall back on SIOCSIWMODE.
		 */
		memset(&ireq, 0, sizeof ireq);
		strlcpy(ireq.ifr_ifrn.ifrn_name, device,
		    sizeof ireq.ifr_ifrn.ifrn_name);
		ireq.u.data.length = 1;	/* 1 argument */
		args[0] = 3;	/* request Prism header */
		memcpy(ireq.u.name, args, sizeof (int));
		if (ioctl(sock_fd, cmd, &ireq) != -1) {
			/*
			 * Success.
			 * Note that we have to put the old mode back
			 * when we close the device.
			 */
			handlep->must_do_on_close |= MUST_CLEAR_RFMON;

			/*
			 * Add this to the list of pcaps to close
			 * when we exit.
			 */
			pcap_add_to_pcaps_to_close(handle);

			return 1;
		}

		/*
		 * Failure.  Fall back on SIOCSIWMODE.
		 */
	}

	/*
	 * First, take the interface down if it's up; otherwise, we
	 * might get EBUSY.
	 */
	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));
	if (ioctl(sock_fd, SIOCGIFFLAGS, &ifr) == -1) {
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "%s: Can't get flags", device);
		return PCAP_ERROR;
	}
	oldflags = 0;
	if (ifr.ifr_flags & IFF_UP) {
		oldflags = ifr.ifr_flags;
		ifr.ifr_flags &= ~IFF_UP;
		if (ioctl(sock_fd, SIOCSIFFLAGS, &ifr) == -1) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "%s: Can't set flags",
			    device);
			return PCAP_ERROR;
		}
	}

	/*
	 * Then turn monitor mode on.
	 */
	strlcpy(ireq.ifr_ifrn.ifrn_name, device,
	    sizeof ireq.ifr_ifrn.ifrn_name);
	ireq.u.mode = IW_MODE_MONITOR;
	if (ioctl(sock_fd, SIOCSIWMODE, &ireq) == -1) {
		/*
		 * Scientist, you've failed.
		 * Bring the interface back up if we shut it down.
		 */
		ifr.ifr_flags = oldflags;
		if (ioctl(sock_fd, SIOCSIFFLAGS, &ifr) == -1) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "%s: Can't set flags",
			    device);
			return PCAP_ERROR;
		}
		return PCAP_ERROR_RFMON_NOTSUP;
	}

	/*
	 * XXX - airmon-ng does "iwconfig {if} key off" after setting
	 * monitor mode and setting the channel, and then does
	 * "iwconfig up".
	 */

	/*
	 * Now select the appropriate radio header.
	 */
	switch (montype) {

	case MONITOR_WEXT:
		/*
		 * We don't have any private ioctl to set the header.
		 */
		break;

	case MONITOR_HOSTAP:
		/*
		 * Try to select the radiotap header.
		 */
		memset(&ireq, 0, sizeof ireq);
		strlcpy(ireq.ifr_ifrn.ifrn_name, device,
		    sizeof ireq.ifr_ifrn.ifrn_name);
		args[0] = 3;	/* request radiotap header */
		memcpy(ireq.u.name, args, sizeof (int));
		if (ioctl(sock_fd, cmd, &ireq) != -1)
			break;	/* success */

		/*
		 * That failed.  Try to select the AVS header.
		 */
		memset(&ireq, 0, sizeof ireq);
		strlcpy(ireq.ifr_ifrn.ifrn_name, device,
		    sizeof ireq.ifr_ifrn.ifrn_name);
		args[0] = 2;	/* request AVS header */
		memcpy(ireq.u.name, args, sizeof (int));
		if (ioctl(sock_fd, cmd, &ireq) != -1)
			break;	/* success */

		/*
		 * That failed.  Try to select the Prism header.
		 */
		memset(&ireq, 0, sizeof ireq);
		strlcpy(ireq.ifr_ifrn.ifrn_name, device,
		    sizeof ireq.ifr_ifrn.ifrn_name);
		args[0] = 1;	/* request Prism header */
		memcpy(ireq.u.name, args, sizeof (int));
		ioctl(sock_fd, cmd, &ireq);
		break;

	case MONITOR_PRISM:
		/*
		 * The private ioctl failed.
		 */
		break;

	case MONITOR_PRISM54:
		/*
		 * Select the Prism header.
		 */
		memset(&ireq, 0, sizeof ireq);
		strlcpy(ireq.ifr_ifrn.ifrn_name, device,
		    sizeof ireq.ifr_ifrn.ifrn_name);
		args[0] = 3;	/* request Prism header */
		memcpy(ireq.u.name, args, sizeof (int));
		ioctl(sock_fd, cmd, &ireq);
		break;

	case MONITOR_ACX100:
		/*
		 * Get the current channel.
		 */
		memset(&ireq, 0, sizeof ireq);
		strlcpy(ireq.ifr_ifrn.ifrn_name, device,
		    sizeof ireq.ifr_ifrn.ifrn_name);
		if (ioctl(sock_fd, SIOCGIWFREQ, &ireq) == -1) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "%s: SIOCGIWFREQ", device);
			return PCAP_ERROR;
		}
		channel = ireq.u.freq.m;

		/*
		 * Select the Prism header, and set the channel to the
		 * current value.
		 */
		memset(&ireq, 0, sizeof ireq);
		strlcpy(ireq.ifr_ifrn.ifrn_name, device,
		    sizeof ireq.ifr_ifrn.ifrn_name);
		args[0] = 1;		/* request Prism header */
		args[1] = channel;	/* set channel */
		memcpy(ireq.u.name, args, 2*sizeof (int));
		ioctl(sock_fd, cmd, &ireq);
		break;

	case MONITOR_RT2500:
		/*
		 * Disallow transmission - that turns on the
		 * Prism header.
		 */
		memset(&ireq, 0, sizeof ireq);
		strlcpy(ireq.ifr_ifrn.ifrn_name, device,
		    sizeof ireq.ifr_ifrn.ifrn_name);
		args[0] = 0;	/* disallow transmitting */
		memcpy(ireq.u.name, args, sizeof (int));
		ioctl(sock_fd, cmd, &ireq);
		break;

	case MONITOR_RT2570:
		/*
		 * Force the Prism header.
		 */
		memset(&ireq, 0, sizeof ireq);
		strlcpy(ireq.ifr_ifrn.ifrn_name, device,
		    sizeof ireq.ifr_ifrn.ifrn_name);
		args[0] = 1;	/* request Prism header */
		memcpy(ireq.u.name, args, sizeof (int));
		ioctl(sock_fd, cmd, &ireq);
		break;

	case MONITOR_RT73:
		/*
		 * Force the Prism header.
		 */
		memset(&ireq, 0, sizeof ireq);
		strlcpy(ireq.ifr_ifrn.ifrn_name, device,
		    sizeof ireq.ifr_ifrn.ifrn_name);
		ireq.u.data.length = 1;	/* 1 argument */
		ireq.u.data.pointer = "1";
		ireq.u.data.flags = 0;
		ioctl(sock_fd, cmd, &ireq);
		break;

	case MONITOR_RTL8XXX:
		/*
		 * Force the Prism header.
		 */
		memset(&ireq, 0, sizeof ireq);
		strlcpy(ireq.ifr_ifrn.ifrn_name, device,
		    sizeof ireq.ifr_ifrn.ifrn_name);
		args[0] = 1;	/* request Prism header */
		memcpy(ireq.u.name, args, sizeof (int));
		ioctl(sock_fd, cmd, &ireq);
		break;
	}

	/*
	 * Now bring the interface back up if we brought it down.
	 */
	if (oldflags != 0) {
		ifr.ifr_flags = oldflags;
		if (ioctl(sock_fd, SIOCSIFFLAGS, &ifr) == -1) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "%s: Can't set flags",
			    device);

			/*
			 * At least try to restore the old mode on the
			 * interface.
			 */
			if (ioctl(handle->fd, SIOCSIWMODE, &ireq) == -1) {
				/*
				 * Scientist, you've failed.
				 */
				fprintf(stderr,
				    "Can't restore interface wireless mode (SIOCSIWMODE failed: %s).\n"
				    "Please adjust manually.\n",
				    strerror(errno));
			}
			return PCAP_ERROR;
		}
	}

	/*
	 * Note that we have to put the old mode back when we
	 * close the device.
	 */
	handlep->must_do_on_close |= MUST_CLEAR_RFMON;

	/*
	 * Add this to the list of pcaps to close when we exit.
	 */
	pcap_add_to_pcaps_to_close(handle);

	return 1;
}
#endif /* IW_MODE_MONITOR */

/*
 * Try various mechanisms to enter monitor mode.
 */
static int
enter_rfmon_mode(pcap_t *handle, int sock_fd, const char *device)
{
#if defined(HAVE_LIBNL) || defined(IW_MODE_MONITOR)
	int ret;
#endif

#ifdef HAVE_LIBNL
	ret = enter_rfmon_mode_mac80211(handle, sock_fd, device);
	if (ret < 0)
		return ret;	/* error attempting to do so */
	if (ret == 1)
		return 1;	/* success */
#endif /* HAVE_LIBNL */

#ifdef IW_MODE_MONITOR
	ret = enter_rfmon_mode_wext(handle, sock_fd, device);
	if (ret < 0)
		return ret;	/* error attempting to do so */
	if (ret == 1)
		return 1;	/* success */
#endif /* IW_MODE_MONITOR */

	/*
	 * Either none of the mechanisms we know about work or none
	 * of those mechanisms are available, so we can't do monitor
	 * mode.
	 */
	return 0;
}

#if defined(HAVE_LINUX_NET_TSTAMP_H) && defined(PACKET_TIMESTAMP)
/*
 * Map SOF_TIMESTAMPING_ values to PCAP_TSTAMP_ values.
 */
static const struct {
	int soft_timestamping_val;
	int pcap_tstamp_val;
} sof_ts_type_map[3] = {
	{ SOF_TIMESTAMPING_SOFTWARE, PCAP_TSTAMP_HOST },
	{ SOF_TIMESTAMPING_SYS_HARDWARE, PCAP_TSTAMP_ADAPTER },
	{ SOF_TIMESTAMPING_RAW_HARDWARE, PCAP_TSTAMP_ADAPTER_UNSYNCED }
};
#define NUM_SOF_TIMESTAMPING_TYPES	(sizeof sof_ts_type_map / sizeof sof_ts_type_map[0])

/*
 * Set the list of time stamping types to include all types.
 */
static void
iface_set_all_ts_types(pcap_t *handle)
{
	u_int i;

	handle->tstamp_type_count = NUM_SOF_TIMESTAMPING_TYPES;
	handle->tstamp_type_list = malloc(NUM_SOF_TIMESTAMPING_TYPES * sizeof(u_int));
	for (i = 0; i < NUM_SOF_TIMESTAMPING_TYPES; i++)
		handle->tstamp_type_list[i] = sof_ts_type_map[i].pcap_tstamp_val;
}

#ifdef ETHTOOL_GET_TS_INFO
/*
 * Get a list of time stamping capabilities.
 */
static int
iface_ethtool_get_ts_info(const char *device, pcap_t *handle, char *ebuf)
{
	int fd;
	struct ifreq ifr;
	struct ethtool_ts_info info;
	int num_ts_types;
	u_int i, j;

	/*
	 * This doesn't apply to the "any" device; you can't say "turn on
	 * hardware time stamping for all devices that exist now and arrange
	 * that it be turned on for any device that appears in the future",
	 * and not all devices even necessarily *support* hardware time
	 * stamping, so don't report any time stamp types.
	 */
	if (strcmp(device, "any") == 0) {
		handle->tstamp_type_list = NULL;
		return 0;
	}

	/*
	 * Create a socket from which to fetch time stamping capabilities.
	 */
	fd = socket(PF_UNIX, SOCK_RAW, 0);
	if (fd < 0) {
		pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
		    errno, "socket for SIOCETHTOOL(ETHTOOL_GET_TS_INFO)");
		return -1;
	}

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));
	memset(&info, 0, sizeof(info));
	info.cmd = ETHTOOL_GET_TS_INFO;
	ifr.ifr_data = (caddr_t)&info;
	if (ioctl(fd, SIOCETHTOOL, &ifr) == -1) {
		int save_errno = errno;

		close(fd);
		switch (save_errno) {

		case EOPNOTSUPP:
		case EINVAL:
			/*
			 * OK, this OS version or driver doesn't support
			 * asking for the time stamping types, so let's
			 * just return all the possible types.
			 */
			iface_set_all_ts_types(handle);
			return 0;

		case ENODEV:
			/*
			 * OK, no such device.
			 * The user will find that out when they try to
			 * activate the device; just return an empty
			 * list of time stamp types.
			 */
			handle->tstamp_type_list = NULL;
			return 0;

		default:
			/*
			 * Other error.
			 */
			pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
			    save_errno,
			    "%s: SIOCETHTOOL(ETHTOOL_GET_TS_INFO) ioctl failed",
			    device);
			return -1;
		}
	}
	close(fd);

	/*
	 * Do we support hardware time stamping of *all* packets?
	 */
	if (!(info.rx_filters & (1 << HWTSTAMP_FILTER_ALL))) {
		/*
		 * No, so don't report any time stamp types.
		 *
		 * XXX - some devices either don't report
		 * HWTSTAMP_FILTER_ALL when they do support it, or
		 * report HWTSTAMP_FILTER_ALL but map it to only
		 * time stamping a few PTP packets.  See
		 * http://marc.info/?l=linux-netdev&m=146318183529571&w=2
		 */
		handle->tstamp_type_list = NULL;
		return 0;
	}

	num_ts_types = 0;
	for (i = 0; i < NUM_SOF_TIMESTAMPING_TYPES; i++) {
		if (info.so_timestamping & sof_ts_type_map[i].soft_timestamping_val)
			num_ts_types++;
	}
	handle->tstamp_type_count = num_ts_types;
	if (num_ts_types != 0) {
		handle->tstamp_type_list = malloc(num_ts_types * sizeof(u_int));
		for (i = 0, j = 0; i < NUM_SOF_TIMESTAMPING_TYPES; i++) {
			if (info.so_timestamping & sof_ts_type_map[i].soft_timestamping_val) {
				handle->tstamp_type_list[j] = sof_ts_type_map[i].pcap_tstamp_val;
				j++;
			}
		}
	} else
		handle->tstamp_type_list = NULL;

	return 0;
}
#else /* ETHTOOL_GET_TS_INFO */
static int
iface_ethtool_get_ts_info(const char *device, pcap_t *handle, char *ebuf _U_)
{
	/*
	 * This doesn't apply to the "any" device; you can't say "turn on
	 * hardware time stamping for all devices that exist now and arrange
	 * that it be turned on for any device that appears in the future",
	 * and not all devices even necessarily *support* hardware time
	 * stamping, so don't report any time stamp types.
	 */
	if (strcmp(device, "any") == 0) {
		handle->tstamp_type_list = NULL;
		return 0;
	}

	/*
	 * We don't have an ioctl to use to ask what's supported,
	 * so say we support everything.
	 */
	iface_set_all_ts_types(handle);
	return 0;
}
#endif /* ETHTOOL_GET_TS_INFO */

#endif /* defined(HAVE_LINUX_NET_TSTAMP_H) && defined(PACKET_TIMESTAMP) */

#ifdef HAVE_PACKET_RING
/*
 * Find out if we have any form of fragmentation/reassembly offloading.
 *
 * We do so using SIOCETHTOOL checking for various types of offloading;
 * if SIOCETHTOOL isn't defined, or we don't have any #defines for any
 * of the types of offloading, there's nothing we can do to check, so
 * we just say "no, we don't".
 */
#if defined(SIOCETHTOOL) && (defined(ETHTOOL_GTSO) || defined(ETHTOOL_GUFO) || defined(ETHTOOL_GGSO) || defined(ETHTOOL_GFLAGS) || defined(ETHTOOL_GGRO))
static int
iface_ethtool_flag_ioctl(pcap_t *handle, int cmd, const char *cmdname)
{
	struct ifreq	ifr;
	struct ethtool_value eval;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, handle->opt.device, sizeof(ifr.ifr_name));
	eval.cmd = cmd;
	eval.data = 0;
	ifr.ifr_data = (caddr_t)&eval;
	if (ioctl(handle->fd, SIOCETHTOOL, &ifr) == -1) {
		if (errno == EOPNOTSUPP || errno == EINVAL) {
			/*
			 * OK, let's just return 0, which, in our
			 * case, either means "no, what we're asking
			 * about is not enabled" or "all the flags
			 * are clear (i.e., nothing is enabled)".
			 */
			return 0;
		}
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "%s: SIOCETHTOOL(%s) ioctl failed",
		    handle->opt.device, cmdname);
		return -1;
	}
	return eval.data;
}

static int
iface_get_offload(pcap_t *handle)
{
	int ret;

#ifdef ETHTOOL_GTSO
	ret = iface_ethtool_flag_ioctl(handle, ETHTOOL_GTSO, "ETHTOOL_GTSO");
	if (ret == -1)
		return -1;
	if (ret)
		return 1;	/* TCP segmentation offloading on */
#endif

#ifdef ETHTOOL_GUFO
	ret = iface_ethtool_flag_ioctl(handle, ETHTOOL_GUFO, "ETHTOOL_GUFO");
	if (ret == -1)
		return -1;
	if (ret)
		return 1;	/* UDP fragmentation offloading on */
#endif

#ifdef ETHTOOL_GGSO
	/*
	 * XXX - will this cause large unsegmented packets to be
	 * handed to PF_PACKET sockets on transmission?  If not,
	 * this need not be checked.
	 */
	ret = iface_ethtool_flag_ioctl(handle, ETHTOOL_GGSO, "ETHTOOL_GGSO");
	if (ret == -1)
		return -1;
	if (ret)
		return 1;	/* generic segmentation offloading on */
#endif

#ifdef ETHTOOL_GFLAGS
	ret = iface_ethtool_flag_ioctl(handle, ETHTOOL_GFLAGS, "ETHTOOL_GFLAGS");
	if (ret == -1)
		return -1;
	if (ret & ETH_FLAG_LRO)
		return 1;	/* large receive offloading on */
#endif

#ifdef ETHTOOL_GGRO
	/*
	 * XXX - will this cause large reassembled packets to be
	 * handed to PF_PACKET sockets on receipt?  If not,
	 * this need not be checked.
	 */
	ret = iface_ethtool_flag_ioctl(handle, ETHTOOL_GGRO, "ETHTOOL_GGRO");
	if (ret == -1)
		return -1;
	if (ret)
		return 1;	/* generic (large) receive offloading on */
#endif

	return 0;
}
#else /* SIOCETHTOOL */
static int
iface_get_offload(pcap_t *handle _U_)
{
	/*
	 * XXX - do we need to get this information if we don't
	 * have the ethtool ioctls?  If so, how do we do that?
	 */
	return 0;
}
#endif /* SIOCETHTOOL */

#endif /* HAVE_PACKET_RING */

#endif /* HAVE_PF_PACKET_SOCKETS */

/* ===== Functions to interface to the older kernels ================== */

/*
 * Try to open a packet socket using the old kernel interface.
 * Returns 1 on success and a PCAP_ERROR_ value on an error.
 */
static int
activate_old(pcap_t *handle)
{
	struct pcap_linux *handlep = handle->priv;
	int		err;
	int		arptype;
	struct ifreq	ifr;
	const char	*device = handle->opt.device;
	struct utsname	utsname;
	int		mtu;

	/* Open the socket */

	handle->fd = socket(PF_INET, SOCK_PACKET, htons(ETH_P_ALL));
	if (handle->fd == -1) {
		err = errno;
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    err, "socket");
		if (err == EPERM || err == EACCES) {
			/*
			 * You don't have permission to open the
			 * socket.
			 */
			return PCAP_ERROR_PERM_DENIED;
		} else {
			/*
			 * Other error.
			 */
			return PCAP_ERROR;
		}
	}

	/* It worked - we are using the old interface */
	handlep->sock_packet = 1;

	/* ...which means we get the link-layer header. */
	handlep->cooked = 0;

	/* Bind to the given device */

	if (strcmp(device, "any") == 0) {
		strlcpy(handle->errbuf, "pcap_activate: The \"any\" device isn't supported on 2.0[.x]-kernel systems",
			PCAP_ERRBUF_SIZE);
		return PCAP_ERROR;
	}
	if (iface_bind_old(handle->fd, device, handle->errbuf) == -1)
		return PCAP_ERROR;

	/*
	 * Try to get the link-layer type.
	 */
	arptype = iface_get_arptype(handle->fd, device, handle->errbuf);
	if (arptype < 0)
		return PCAP_ERROR;

	/*
	 * Try to find the DLT_ type corresponding to that
	 * link-layer type.
	 */
	map_arphrd_to_dlt(handle, handle->fd, arptype, device, 0);
	if (handle->linktype == -1) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
			 "unknown arptype %d", arptype);
		return PCAP_ERROR;
	}

	/* Go to promisc mode if requested */

	if (handle->opt.promisc) {
		memset(&ifr, 0, sizeof(ifr));
		strlcpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));
		if (ioctl(handle->fd, SIOCGIFFLAGS, &ifr) == -1) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "SIOCGIFFLAGS");
			return PCAP_ERROR;
		}
		if ((ifr.ifr_flags & IFF_PROMISC) == 0) {
			/*
			 * Promiscuous mode isn't currently on,
			 * so turn it on, and remember that
			 * we should turn it off when the
			 * pcap_t is closed.
			 */

			/*
			 * If we haven't already done so, arrange
			 * to have "pcap_close_all()" called when
			 * we exit.
			 */
			if (!pcap_do_addexit(handle)) {
				/*
				 * "atexit()" failed; don't put
				 * the interface in promiscuous
				 * mode, just give up.
				 */
				return PCAP_ERROR;
			}

			ifr.ifr_flags |= IFF_PROMISC;
			if (ioctl(handle->fd, SIOCSIFFLAGS, &ifr) == -1) {
				pcap_fmt_errmsg_for_errno(handle->errbuf,
				    PCAP_ERRBUF_SIZE, errno, "SIOCSIFFLAGS");
				return PCAP_ERROR;
			}
			handlep->must_do_on_close |= MUST_CLEAR_PROMISC;

			/*
			 * Add this to the list of pcaps
			 * to close when we exit.
			 */
			pcap_add_to_pcaps_to_close(handle);
		}
	}

	/*
	 * Compute the buffer size.
	 *
	 * We're using SOCK_PACKET, so this might be a 2.0[.x]
	 * kernel, and might require special handling - check.
	 */
	if (uname(&utsname) < 0 ||
	    strncmp(utsname.release, "2.0", 3) == 0) {
		/*
		 * Either we couldn't find out what kernel release
		 * this is, or it's a 2.0[.x] kernel.
		 *
		 * In the 2.0[.x] kernel, a "recvfrom()" on
		 * a SOCK_PACKET socket, with MSG_TRUNC set, will
		 * return the number of bytes read, so if we pass
		 * a length based on the snapshot length, it'll
		 * return the number of bytes from the packet
		 * copied to userland, not the actual length
		 * of the packet.
		 *
		 * This means that, for example, the IP dissector
		 * in tcpdump will get handed a packet length less
		 * than the length in the IP header, and will
		 * complain about "truncated-ip".
		 *
		 * So we don't bother trying to copy from the
		 * kernel only the bytes in which we're interested,
		 * but instead copy them all, just as the older
		 * versions of libpcap for Linux did.
		 *
		 * The buffer therefore needs to be big enough to
		 * hold the largest packet we can get from this
		 * device.  Unfortunately, we can't get the MRU
		 * of the network; we can only get the MTU.  The
		 * MTU may be too small, in which case a packet larger
		 * than the buffer size will be truncated *and* we
		 * won't get the actual packet size.
		 *
		 * However, if the snapshot length is larger than
		 * the buffer size based on the MTU, we use the
		 * snapshot length as the buffer size, instead;
		 * this means that with a sufficiently large snapshot
		 * length we won't artificially truncate packets
		 * to the MTU-based size.
		 *
		 * This mess just one of many problems with packet
		 * capture on 2.0[.x] kernels; you really want a
		 * 2.2[.x] or later kernel if you want packet capture
		 * to work well.
		 */
		mtu = iface_get_mtu(handle->fd, device, handle->errbuf);
		if (mtu == -1)
			return PCAP_ERROR;
		handle->bufsize = MAX_LINKHEADER_SIZE + mtu;
		if (handle->bufsize < (u_int)handle->snapshot)
			handle->bufsize = (u_int)handle->snapshot;
	} else {
		/*
		 * This is a 2.2[.x] or later kernel.
		 *
		 * We can safely pass "recvfrom()" a byte count
		 * based on the snapshot length.
		 */
		handle->bufsize = (u_int)handle->snapshot;
	}

	/*
	 * Default value for offset to align link-layer payload
	 * on a 4-byte boundary.
	 */
	handle->offset	 = 0;

	/*
	 * SOCK_PACKET sockets don't supply information from
	 * stripped VLAN tags.
	 */
	handlep->vlan_offset = -1; /* unknown */

	return 1;
}

/*
 *  Bind the socket associated with FD to the given device using the
 *  interface of the old kernels.
 */
static int
iface_bind_old(int fd, const char *device, char *ebuf)
{
	struct sockaddr	saddr;
	int		err;
	socklen_t	errlen = sizeof(err);

	memset(&saddr, 0, sizeof(saddr));
	strlcpy(saddr.sa_data, device, sizeof(saddr.sa_data));
	if (bind(fd, &saddr, sizeof(saddr)) == -1) {
		pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
		    errno, "bind");
		return -1;
	}

	/* Any pending errors, e.g., network is down? */

	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) == -1) {
		pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
		    errno, "getsockopt");
		return -1;
	}

	if (err > 0) {
		pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
		    err, "bind");
		return -1;
	}

	return 0;
}


/* ===== System calls available on all supported kernels ============== */

/*
 *  Query the kernel for the MTU of the given interface.
 */
static int
iface_get_mtu(int fd, const char *device, char *ebuf)
{
	struct ifreq	ifr;

	if (!device)
		return BIGGER_THAN_ALL_MTUS;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));

	if (ioctl(fd, SIOCGIFMTU, &ifr) == -1) {
		pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
		    errno, "SIOCGIFMTU");
		return -1;
	}

	return ifr.ifr_mtu;
}

/*
 *  Get the hardware type of the given interface as ARPHRD_xxx constant.
 */
static int
iface_get_arptype(int fd, const char *device, char *ebuf)
{
	struct ifreq	ifr;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));

	if (ioctl(fd, SIOCGIFHWADDR, &ifr) == -1) {
		pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
		    errno, "SIOCGIFHWADDR");
		if (errno == ENODEV) {
			/*
			 * No such device.
			 */
			return PCAP_ERROR_NO_SUCH_DEVICE;
		}
		return PCAP_ERROR;
	}

	return ifr.ifr_hwaddr.sa_family;
}

#ifdef SO_ATTACH_FILTER
static int
fix_program(pcap_t *handle, struct sock_fprog *fcode, int is_mmapped)
{
	struct pcap_linux *handlep = handle->priv;
	size_t prog_size;
	register int i;
	register struct bpf_insn *p;
	struct bpf_insn *f;
	int len;

	/*
	 * Make a copy of the filter, and modify that copy if
	 * necessary.
	 */
	prog_size = sizeof(*handle->fcode.bf_insns) * handle->fcode.bf_len;
	len = handle->fcode.bf_len;
	f = (struct bpf_insn *)malloc(prog_size);
	if (f == NULL) {
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "malloc");
		return -1;
	}
	memcpy(f, handle->fcode.bf_insns, prog_size);
	fcode->len = len;
	fcode->filter = (struct sock_filter *) f;

	for (i = 0; i < len; ++i) {
		p = &f[i];
		/*
		 * What type of instruction is this?
		 */
		switch (BPF_CLASS(p->code)) {

		case BPF_RET:
			/*
			 * It's a return instruction; are we capturing
			 * in memory-mapped mode?
			 */
			if (!is_mmapped) {
				/*
				 * No; is the snapshot length a constant,
				 * rather than the contents of the
				 * accumulator?
				 */
				if (BPF_MODE(p->code) == BPF_K) {
					/*
					 * Yes - if the value to be returned,
					 * i.e. the snapshot length, is
					 * anything other than 0, make it
					 * MAXIMUM_SNAPLEN, so that the packet
					 * is truncated by "recvfrom()",
					 * not by the filter.
					 *
					 * XXX - there's nothing we can
					 * easily do if it's getting the
					 * value from the accumulator; we'd
					 * have to insert code to force
					 * non-zero values to be
					 * MAXIMUM_SNAPLEN.
					 */
					if (p->k != 0)
						p->k = MAXIMUM_SNAPLEN;
				}
			}
			break;

		case BPF_LD:
		case BPF_LDX:
			/*
			 * It's a load instruction; is it loading
			 * from the packet?
			 */
			switch (BPF_MODE(p->code)) {

			case BPF_ABS:
			case BPF_IND:
			case BPF_MSH:
				/*
				 * Yes; are we in cooked mode?
				 */
				if (handlep->cooked) {
					/*
					 * Yes, so we need to fix this
					 * instruction.
					 */
					if (fix_offset(p) < 0) {
						/*
						 * We failed to do so.
						 * Return 0, so our caller
						 * knows to punt to userland.
						 */
						return 0;
					}
				}
				break;
			}
			break;
		}
	}
	return 1;	/* we succeeded */
}

static int
fix_offset(struct bpf_insn *p)
{
	/*
	 * What's the offset?
	 */
	if (p->k >= SLL_HDR_LEN) {
		/*
		 * It's within the link-layer payload; that starts at an
		 * offset of 0, as far as the kernel packet filter is
		 * concerned, so subtract the length of the link-layer
		 * header.
		 */
		p->k -= SLL_HDR_LEN;
	} else if (p->k == 0) {
		/*
		 * It's the packet type field; map it to the special magic
		 * kernel offset for that field.
		 */
		p->k = SKF_AD_OFF + SKF_AD_PKTTYPE;
	} else if (p->k == 14) {
		/*
		 * It's the protocol field; map it to the special magic
		 * kernel offset for that field.
		 */
		p->k = SKF_AD_OFF + SKF_AD_PROTOCOL;
	} else if ((bpf_int32)(p->k) > 0) {
		/*
		 * It's within the header, but it's not one of those
		 * fields; we can't do that in the kernel, so punt
		 * to userland.
		 */
		return -1;
	}
	return 0;
}

static int
set_kernel_filter(pcap_t *handle, struct sock_fprog *fcode)
{
	int total_filter_on = 0;
	int save_mode;
	int ret;
	int save_errno;

	/*
	 * The socket filter code doesn't discard all packets queued
	 * up on the socket when the filter is changed; this means
	 * that packets that don't match the new filter may show up
	 * after the new filter is put onto the socket, if those
	 * packets haven't yet been read.
	 *
	 * This means, for example, that if you do a tcpdump capture
	 * with a filter, the first few packets in the capture might
	 * be packets that wouldn't have passed the filter.
	 *
	 * We therefore discard all packets queued up on the socket
	 * when setting a kernel filter.  (This isn't an issue for
	 * userland filters, as the userland filtering is done after
	 * packets are queued up.)
	 *
	 * To flush those packets, we put the socket in read-only mode,
	 * and read packets from the socket until there are no more to
	 * read.
	 *
	 * In order to keep that from being an infinite loop - i.e.,
	 * to keep more packets from arriving while we're draining
	 * the queue - we put the "total filter", which is a filter
	 * that rejects all packets, onto the socket before draining
	 * the queue.
	 *
	 * This code deliberately ignores any errors, so that you may
	 * get bogus packets if an error occurs, rather than having
	 * the filtering done in userland even if it could have been
	 * done in the kernel.
	 */
	if (setsockopt(handle->fd, SOL_SOCKET, SO_ATTACH_FILTER,
		       &total_fcode, sizeof(total_fcode)) == 0) {
		char drain[1];

		/*
		 * Note that we've put the total filter onto the socket.
		 */
		total_filter_on = 1;

		/*
		 * Save the socket's current mode, and put it in
		 * non-blocking mode; we drain it by reading packets
		 * until we get an error (which is normally a
		 * "nothing more to be read" error).
		 */
		save_mode = fcntl(handle->fd, F_GETFL, 0);
		if (save_mode == -1) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno,
			    "can't get FD flags when changing filter");
			return -2;
		}
		if (fcntl(handle->fd, F_SETFL, save_mode | O_NONBLOCK) < 0) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno,
			    "can't set nonblocking mode when changing filter");
			return -2;
		}
		while (recv(handle->fd, &drain, sizeof drain, MSG_TRUNC) >= 0)
			;
		save_errno = errno;
		if (save_errno != EAGAIN) {
			/*
			 * Fatal error.
			 *
			 * If we can't restore the mode or reset the
			 * kernel filter, there's nothing we can do.
			 */
			(void)fcntl(handle->fd, F_SETFL, save_mode);
			(void)reset_kernel_filter(handle);
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, save_errno,
			    "recv failed when changing filter");
			return -2;
		}
		if (fcntl(handle->fd, F_SETFL, save_mode) == -1) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno,
			    "can't restore FD flags when changing filter");
			return -2;
		}
	}

	/*
	 * Now attach the new filter.
	 */
	ret = setsockopt(handle->fd, SOL_SOCKET, SO_ATTACH_FILTER,
			 fcode, sizeof(*fcode));
	if (ret == -1 && total_filter_on) {
		/*
		 * Well, we couldn't set that filter on the socket,
		 * but we could set the total filter on the socket.
		 *
		 * This could, for example, mean that the filter was
		 * too big to put into the kernel, so we'll have to
		 * filter in userland; in any case, we'll be doing
		 * filtering in userland, so we need to remove the
		 * total filter so we see packets.
		 */
		save_errno = errno;

		/*
		 * If this fails, we're really screwed; we have the
		 * total filter on the socket, and it won't come off.
		 * Report it as a fatal error.
		 */
		if (reset_kernel_filter(handle) == -1) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno,
			    "can't remove kernel total filter");
			return -2;	/* fatal error */
		}

		errno = save_errno;
	}
	return ret;
}

static int
reset_kernel_filter(pcap_t *handle)
{
	int ret;
	/*
	 * setsockopt() barfs unless it get a dummy parameter.
	 * valgrind whines unless the value is initialized,
	 * as it has no idea that setsockopt() ignores its
	 * parameter.
	 */
	int dummy = 0;

	ret = setsockopt(handle->fd, SOL_SOCKET, SO_DETACH_FILTER,
				   &dummy, sizeof(dummy));
	/*
	 * Ignore ENOENT - it means "we don't have a filter", so there
	 * was no filter to remove, and there's still no filter.
	 *
	 * Also ignore ENONET, as a lot of kernel versions had a
	 * typo where ENONET, rather than ENOENT, was returned.
	 */
	if (ret == -1 && errno != ENOENT && errno != ENONET)
		return -1;
	return 0;
}
#endif

int
pcap_set_protocol(pcap_t *p, int protocol)
{
	if (pcap_check_activated(p))
		return (PCAP_ERROR_ACTIVATED);
	p->opt.protocol = protocol;
	return (0);
}

/*
 * Libpcap version string.
 */
const char *
pcap_lib_version(void)
{
#ifdef HAVE_PACKET_RING
 #if defined(HAVE_TPACKET3)
	return (PCAP_VERSION_STRING " (with TPACKET_V3)");
 #elif defined(HAVE_TPACKET2)
	return (PCAP_VERSION_STRING " (with TPACKET_V2)");
 #else
	return (PCAP_VERSION_STRING " (with TPACKET_V1)");
 #endif
#else
	return (PCAP_VERSION_STRING " (without TPACKET)");
#endif
}
