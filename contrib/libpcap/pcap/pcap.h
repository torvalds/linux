/* -*- Mode: c; tab-width: 8; indent-tabs-mode: 1; c-basic-offset: 8; -*- */
/*
 * Copyright (c) 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Remote packet capture mechanisms and extensions from WinPcap:
 *
 * Copyright (c) 2002 - 2003
 * NetGroup, Politecnico di Torino (Italy)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Politecnico di Torino nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef lib_pcap_pcap_h
#define lib_pcap_pcap_h

#include <pcap/funcattrs.h>

#include <pcap/pcap-inttypes.h>

#if defined(_WIN32)
  #include <winsock2.h>		/* u_int, u_char etc. */
  #include <io.h>		/* _get_osfhandle() */
#elif defined(MSDOS)
  #include <sys/types.h>	/* u_int, u_char etc. */
  #include <sys/socket.h>
#else /* UN*X */
  #include <sys/types.h>	/* u_int, u_char etc. */
  #include <sys/time.h>
#endif /* _WIN32/MSDOS/UN*X */

#include <net/bpf.h>

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Version number of the current version of the pcap file format.
 *
 * NOTE: this is *NOT* the version number of the libpcap library.
 * To fetch the version information for the version of libpcap
 * you're using, use pcap_lib_version().
 */
#define PCAP_VERSION_MAJOR 2
#define PCAP_VERSION_MINOR 4

#define PCAP_ERRBUF_SIZE 256

/*
 * Compatibility for systems that have a bpf.h that
 * predates the bpf typedefs for 64-bit support.
 */
#if BPF_RELEASE - 0 < 199406
typedef	int bpf_int32;
typedef	u_int bpf_u_int32;
#endif

typedef struct pcap pcap_t;
typedef struct pcap_dumper pcap_dumper_t;
typedef struct pcap_if pcap_if_t;
typedef struct pcap_addr pcap_addr_t;

/*
 * The first record in the file contains saved values for some
 * of the flags used in the printout phases of tcpdump.
 * Many fields here are 32 bit ints so compilers won't insert unwanted
 * padding; these files need to be interchangeable across architectures.
 *
 * Do not change the layout of this structure, in any way (this includes
 * changes that only affect the length of fields in this structure).
 *
 * Also, do not change the interpretation of any of the members of this
 * structure, in any way (this includes using values other than
 * LINKTYPE_ values, as defined in "savefile.c", in the "linktype"
 * field).
 *
 * Instead:
 *
 *	introduce a new structure for the new format, if the layout
 *	of the structure changed;
 *
 *	send mail to "tcpdump-workers@lists.tcpdump.org", requesting
 *	a new magic number for your new capture file format, and, when
 *	you get the new magic number, put it in "savefile.c";
 *
 *	use that magic number for save files with the changed file
 *	header;
 *
 *	make the code in "savefile.c" capable of reading files with
 *	the old file header as well as files with the new file header
 *	(using the magic number to determine the header format).
 *
 * Then supply the changes by forking the branch at
 *
 *	https://github.com/the-tcpdump-group/libpcap/issues
 *
 * and issuing a pull request, so that future versions of libpcap and
 * programs that use it (such as tcpdump) will be able to read your new
 * capture file format.
 */
struct pcap_file_header {
	bpf_u_int32 magic;
	u_short version_major;
	u_short version_minor;
	bpf_int32 thiszone;	/* gmt to local correction */
	bpf_u_int32 sigfigs;	/* accuracy of timestamps */
	bpf_u_int32 snaplen;	/* max length saved portion of each pkt */
	bpf_u_int32 linktype;	/* data link type (LINKTYPE_*) */
};

/*
 * Macros for the value returned by pcap_datalink_ext().
 *
 * If LT_FCS_LENGTH_PRESENT(x) is true, the LT_FCS_LENGTH(x) macro
 * gives the FCS length of packets in the capture.
 */
#define LT_FCS_LENGTH_PRESENT(x)	((x) & 0x04000000)
#define LT_FCS_LENGTH(x)		(((x) & 0xF0000000) >> 28)
#define LT_FCS_DATALINK_EXT(x)		((((x) & 0xF) << 28) | 0x04000000)

typedef enum {
       PCAP_D_INOUT = 0,
       PCAP_D_IN,
       PCAP_D_OUT
} pcap_direction_t;

/*
 * Generic per-packet information, as supplied by libpcap.
 *
 * The time stamp can and should be a "struct timeval", regardless of
 * whether your system supports 32-bit tv_sec in "struct timeval",
 * 64-bit tv_sec in "struct timeval", or both if it supports both 32-bit
 * and 64-bit applications.  The on-disk format of savefiles uses 32-bit
 * tv_sec (and tv_usec); this structure is irrelevant to that.  32-bit
 * and 64-bit versions of libpcap, even if they're on the same platform,
 * should supply the appropriate version of "struct timeval", even if
 * that's not what the underlying packet capture mechanism supplies.
 */
struct pcap_pkthdr {
	struct timeval ts;	/* time stamp */
	bpf_u_int32 caplen;	/* length of portion present */
	bpf_u_int32 len;	/* length this packet (off wire) */
};

/*
 * As returned by the pcap_stats()
 */
struct pcap_stat {
	u_int ps_recv;		/* number of packets received */
	u_int ps_drop;		/* number of packets dropped */
	u_int ps_ifdrop;	/* drops by interface -- only supported on some platforms */
#ifdef _WIN32
	u_int ps_capt;		/* number of packets that reach the application */
	u_int ps_sent;		/* number of packets sent by the server on the network */
	u_int ps_netdrop;	/* number of packets lost on the network */
#endif /* _WIN32 */
};

#ifdef MSDOS
/*
 * As returned by the pcap_stats_ex()
 */
struct pcap_stat_ex {
       u_long  rx_packets;        /* total packets received       */
       u_long  tx_packets;        /* total packets transmitted    */
       u_long  rx_bytes;          /* total bytes received         */
       u_long  tx_bytes;          /* total bytes transmitted      */
       u_long  rx_errors;         /* bad packets received         */
       u_long  tx_errors;         /* packet transmit problems     */
       u_long  rx_dropped;        /* no space in Rx buffers       */
       u_long  tx_dropped;        /* no space available for Tx    */
       u_long  multicast;         /* multicast packets received   */
       u_long  collisions;

       /* detailed rx_errors: */
       u_long  rx_length_errors;
       u_long  rx_over_errors;    /* receiver ring buff overflow  */
       u_long  rx_crc_errors;     /* recv'd pkt with crc error    */
       u_long  rx_frame_errors;   /* recv'd frame alignment error */
       u_long  rx_fifo_errors;    /* recv'r fifo overrun          */
       u_long  rx_missed_errors;  /* recv'r missed packet         */

       /* detailed tx_errors */
       u_long  tx_aborted_errors;
       u_long  tx_carrier_errors;
       u_long  tx_fifo_errors;
       u_long  tx_heartbeat_errors;
       u_long  tx_window_errors;
     };
#endif

/*
 * Item in a list of interfaces.
 */
struct pcap_if {
	struct pcap_if *next;
	char *name;		/* name to hand to "pcap_open_live()" */
	char *description;	/* textual description of interface, or NULL */
	struct pcap_addr *addresses;
	bpf_u_int32 flags;	/* PCAP_IF_ interface flags */
};

#define PCAP_IF_LOOPBACK				0x00000001	/* interface is loopback */
#define PCAP_IF_UP					0x00000002	/* interface is up */
#define PCAP_IF_RUNNING					0x00000004	/* interface is running */
#define PCAP_IF_WIRELESS				0x00000008	/* interface is wireless (*NOT* necessarily Wi-Fi!) */
#define PCAP_IF_CONNECTION_STATUS			0x00000030	/* connection status: */
#define PCAP_IF_CONNECTION_STATUS_UNKNOWN		0x00000000	/* unknown */
#define PCAP_IF_CONNECTION_STATUS_CONNECTED		0x00000010	/* connected */
#define PCAP_IF_CONNECTION_STATUS_DISCONNECTED		0x00000020	/* disconnected */
#define PCAP_IF_CONNECTION_STATUS_NOT_APPLICABLE	0x00000030	/* not applicable */

/*
 * Representation of an interface address.
 */
struct pcap_addr {
	struct pcap_addr *next;
	struct sockaddr *addr;		/* address */
	struct sockaddr *netmask;	/* netmask for that address */
	struct sockaddr *broadaddr;	/* broadcast address for that address */
	struct sockaddr *dstaddr;	/* P2P destination address for that address */
};

typedef void (*pcap_handler)(u_char *, const struct pcap_pkthdr *,
			     const u_char *);

/*
 * Error codes for the pcap API.
 * These will all be negative, so you can check for the success or
 * failure of a call that returns these codes by checking for a
 * negative value.
 */
#define PCAP_ERROR			-1	/* generic error code */
#define PCAP_ERROR_BREAK		-2	/* loop terminated by pcap_breakloop */
#define PCAP_ERROR_NOT_ACTIVATED	-3	/* the capture needs to be activated */
#define PCAP_ERROR_ACTIVATED		-4	/* the operation can't be performed on already activated captures */
#define PCAP_ERROR_NO_SUCH_DEVICE	-5	/* no such device exists */
#define PCAP_ERROR_RFMON_NOTSUP		-6	/* this device doesn't support rfmon (monitor) mode */
#define PCAP_ERROR_NOT_RFMON		-7	/* operation supported only in monitor mode */
#define PCAP_ERROR_PERM_DENIED		-8	/* no permission to open the device */
#define PCAP_ERROR_IFACE_NOT_UP		-9	/* interface isn't up */
#define PCAP_ERROR_CANTSET_TSTAMP_TYPE	-10	/* this device doesn't support setting the time stamp type */
#define PCAP_ERROR_PROMISC_PERM_DENIED	-11	/* you don't have permission to capture in promiscuous mode */
#define PCAP_ERROR_TSTAMP_PRECISION_NOTSUP -12  /* the requested time stamp precision is not supported */

/*
 * Warning codes for the pcap API.
 * These will all be positive and non-zero, so they won't look like
 * errors.
 */
#define PCAP_WARNING			1	/* generic warning code */
#define PCAP_WARNING_PROMISC_NOTSUP	2	/* this device doesn't support promiscuous mode */
#define PCAP_WARNING_TSTAMP_TYPE_NOTSUP	3	/* the requested time stamp type is not supported */

/*
 * Value to pass to pcap_compile() as the netmask if you don't know what
 * the netmask is.
 */
#define PCAP_NETMASK_UNKNOWN	0xffffffff

/*
 * We're deprecating pcap_lookupdev() for various reasons (not
 * thread-safe, can behave weirdly with WinPcap).  Callers
 * should use pcap_findalldevs() and use the first device.
 */
PCAP_API char	*pcap_lookupdev(char *)
PCAP_DEPRECATED(pcap_lookupdev, "use 'pcap_findalldevs' and use the first device");

PCAP_API int	pcap_lookupnet(const char *, bpf_u_int32 *, bpf_u_int32 *, char *);

PCAP_API pcap_t	*pcap_create(const char *, char *);
PCAP_API int	pcap_set_snaplen(pcap_t *, int);
PCAP_API int	pcap_set_promisc(pcap_t *, int);
PCAP_API int	pcap_can_set_rfmon(pcap_t *);
PCAP_API int	pcap_set_rfmon(pcap_t *, int);
PCAP_API int	pcap_set_timeout(pcap_t *, int);
PCAP_API int	pcap_set_tstamp_type(pcap_t *, int);
PCAP_API int	pcap_set_immediate_mode(pcap_t *, int);
PCAP_API int	pcap_set_buffer_size(pcap_t *, int);
PCAP_API int	pcap_set_tstamp_precision(pcap_t *, int);
PCAP_API int	pcap_get_tstamp_precision(pcap_t *);
PCAP_API int	pcap_activate(pcap_t *);

PCAP_API int	pcap_list_tstamp_types(pcap_t *, int **);
PCAP_API void	pcap_free_tstamp_types(int *);
PCAP_API int	pcap_tstamp_type_name_to_val(const char *);
PCAP_API const char *pcap_tstamp_type_val_to_name(int);
PCAP_API const char *pcap_tstamp_type_val_to_description(int);

#ifdef __linux__
PCAP_API int	pcap_set_protocol(pcap_t *, int);
#endif

/*
 * Time stamp types.
 * Not all systems and interfaces will necessarily support all of these.
 *
 * A system that supports PCAP_TSTAMP_HOST is offering time stamps
 * provided by the host machine, rather than by the capture device,
 * but not committing to any characteristics of the time stamp;
 * it will not offer any of the PCAP_TSTAMP_HOST_ subtypes.
 *
 * PCAP_TSTAMP_HOST_LOWPREC is a time stamp, provided by the host machine,
 * that's low-precision but relatively cheap to fetch; it's normally done
 * using the system clock, so it's normally synchronized with times you'd
 * fetch from system calls.
 *
 * PCAP_TSTAMP_HOST_HIPREC is a time stamp, provided by the host machine,
 * that's high-precision; it might be more expensive to fetch.  It might
 * or might not be synchronized with the system clock, and might have
 * problems with time stamps for packets received on different CPUs,
 * depending on the platform.
 *
 * PCAP_TSTAMP_ADAPTER is a high-precision time stamp supplied by the
 * capture device; it's synchronized with the system clock.
 *
 * PCAP_TSTAMP_ADAPTER_UNSYNCED is a high-precision time stamp supplied by
 * the capture device; it's not synchronized with the system clock.
 *
 * Note that time stamps synchronized with the system clock can go
 * backwards, as the system clock can go backwards.  If a clock is
 * not in sync with the system clock, that could be because the
 * system clock isn't keeping accurate time, because the other
 * clock isn't keeping accurate time, or both.
 *
 * Note that host-provided time stamps generally correspond to the
 * time when the time-stamping code sees the packet; this could
 * be some unknown amount of time after the first or last bit of
 * the packet is received by the network adapter, due to batching
 * of interrupts for packet arrival, queueing delays, etc..
 */
#define PCAP_TSTAMP_HOST		0	/* host-provided, unknown characteristics */
#define PCAP_TSTAMP_HOST_LOWPREC	1	/* host-provided, low precision */
#define PCAP_TSTAMP_HOST_HIPREC		2	/* host-provided, high precision */
#define PCAP_TSTAMP_ADAPTER		3	/* device-provided, synced with the system clock */
#define PCAP_TSTAMP_ADAPTER_UNSYNCED	4	/* device-provided, not synced with the system clock */

/*
 * Time stamp resolution types.
 * Not all systems and interfaces will necessarily support all of these
 * resolutions when doing live captures; all of them can be requested
 * when reading a savefile.
 */
#define PCAP_TSTAMP_PRECISION_MICRO	0	/* use timestamps with microsecond precision, default */
#define PCAP_TSTAMP_PRECISION_NANO	1	/* use timestamps with nanosecond precision */

PCAP_API pcap_t	*pcap_open_live(const char *, int, int, int, char *);
PCAP_API pcap_t	*pcap_open_dead(int, int);
PCAP_API pcap_t	*pcap_open_dead_with_tstamp_precision(int, int, u_int);
PCAP_API pcap_t	*pcap_open_offline_with_tstamp_precision(const char *, u_int, char *);
PCAP_API pcap_t	*pcap_open_offline(const char *, char *);
#ifdef _WIN32
  PCAP_API pcap_t  *pcap_hopen_offline_with_tstamp_precision(intptr_t, u_int, char *);
  PCAP_API pcap_t  *pcap_hopen_offline(intptr_t, char *);
  /*
   * If we're building libpcap, these are internal routines in savefile.c,
   * so we must not define them as macros.
   *
   * If we're not building libpcap, given that the version of the C runtime
   * with which libpcap was built might be different from the version
   * of the C runtime with which an application using libpcap was built,
   * and that a FILE structure may differ between the two versions of the
   * C runtime, calls to _fileno() must use the version of _fileno() in
   * the C runtime used to open the FILE *, not the version in the C
   * runtime with which libpcap was built.  (Maybe once the Universal CRT
   * rules the world, this will cease to be a problem.)
   */
  #ifndef BUILDING_PCAP
    #define pcap_fopen_offline_with_tstamp_precision(f,p,b) \
	pcap_hopen_offline_with_tstamp_precision(_get_osfhandle(_fileno(f)), p, b)
    #define pcap_fopen_offline(f,b) \
	pcap_hopen_offline(_get_osfhandle(_fileno(f)), b)
  #endif
#else /*_WIN32*/
  PCAP_API pcap_t	*pcap_fopen_offline_with_tstamp_precision(FILE *, u_int, char *);
  PCAP_API pcap_t	*pcap_fopen_offline(FILE *, char *);
#endif /*_WIN32*/

PCAP_API void	pcap_close(pcap_t *);
PCAP_API int	pcap_loop(pcap_t *, int, pcap_handler, u_char *);
PCAP_API int	pcap_dispatch(pcap_t *, int, pcap_handler, u_char *);
PCAP_API const u_char *pcap_next(pcap_t *, struct pcap_pkthdr *);
PCAP_API int 	pcap_next_ex(pcap_t *, struct pcap_pkthdr **, const u_char **);
PCAP_API void	pcap_breakloop(pcap_t *);
PCAP_API int	pcap_stats(pcap_t *, struct pcap_stat *);
PCAP_API int	pcap_setfilter(pcap_t *, struct bpf_program *);
PCAP_API int 	pcap_setdirection(pcap_t *, pcap_direction_t);
PCAP_API int	pcap_getnonblock(pcap_t *, char *);
PCAP_API int	pcap_setnonblock(pcap_t *, int, char *);
PCAP_API int	pcap_inject(pcap_t *, const void *, size_t);
PCAP_API int	pcap_sendpacket(pcap_t *, const u_char *, int);
PCAP_API const char *pcap_statustostr(int);
PCAP_API const char *pcap_strerror(int);
PCAP_API char	*pcap_geterr(pcap_t *);
PCAP_API void	pcap_perror(pcap_t *, const char *);
PCAP_API int	pcap_compile(pcap_t *, struct bpf_program *, const char *, int,
	    bpf_u_int32);
PCAP_API int	pcap_compile_nopcap(int, int, struct bpf_program *,
	    const char *, int, bpf_u_int32);
PCAP_API void	pcap_freecode(struct bpf_program *);
PCAP_API int	pcap_offline_filter(const struct bpf_program *,
	    const struct pcap_pkthdr *, const u_char *);
PCAP_API int	pcap_datalink(pcap_t *);
PCAP_API int	pcap_datalink_ext(pcap_t *);
PCAP_API int	pcap_list_datalinks(pcap_t *, int **);
PCAP_API int	pcap_set_datalink(pcap_t *, int);
PCAP_API void	pcap_free_datalinks(int *);
PCAP_API int	pcap_datalink_name_to_val(const char *);
PCAP_API const char *pcap_datalink_val_to_name(int);
PCAP_API const char *pcap_datalink_val_to_description(int);
PCAP_API int	pcap_snapshot(pcap_t *);
PCAP_API int	pcap_is_swapped(pcap_t *);
PCAP_API int	pcap_major_version(pcap_t *);
PCAP_API int	pcap_minor_version(pcap_t *);
PCAP_API int	pcap_bufsize(pcap_t *);

/* XXX */
PCAP_API FILE	*pcap_file(pcap_t *);
PCAP_API int	pcap_fileno(pcap_t *);

#ifdef _WIN32
  PCAP_API int	pcap_wsockinit(void);
#endif

PCAP_API pcap_dumper_t *pcap_dump_open(pcap_t *, const char *);
PCAP_API pcap_dumper_t *pcap_dump_fopen(pcap_t *, FILE *fp);
PCAP_API pcap_dumper_t *pcap_dump_open_append(pcap_t *, const char *);
PCAP_API FILE	*pcap_dump_file(pcap_dumper_t *);
PCAP_API long	pcap_dump_ftell(pcap_dumper_t *);
PCAP_API int64_t	pcap_dump_ftell64(pcap_dumper_t *);
PCAP_API int	pcap_dump_flush(pcap_dumper_t *);
PCAP_API void	pcap_dump_close(pcap_dumper_t *);
PCAP_API void	pcap_dump(u_char *, const struct pcap_pkthdr *, const u_char *);

PCAP_API int	pcap_findalldevs(pcap_if_t **, char *);
PCAP_API void	pcap_freealldevs(pcap_if_t *);

/*
 * We return a pointer to the version string, rather than exporting the
 * version string directly.
 *
 * On at least some UNIXes, if you import data from a shared library into
 * an program, the data is bound into the program binary, so if the string
 * in the version of the library with which the program was linked isn't
 * the same as the string in the version of the library with which the
 * program is being run, various undesirable things may happen (warnings,
 * the string being the one from the version of the library with which the
 * program was linked, or even weirder things, such as the string being the
 * one from the library but being truncated).
 *
 * On Windows, the string is constructed at run time.
 */
PCAP_API const char *pcap_lib_version(void);

/*
 * On at least some versions of NetBSD and QNX, we don't want to declare
 * bpf_filter() here, as it's also be declared in <net/bpf.h>, with a
 * different signature, but, on other BSD-flavored UN*Xes, it's not
 * declared in <net/bpf.h>, so we *do* want to declare it here, so it's
 * declared when we build pcap-bpf.c.
 */
#if !defined(__NetBSD__) && !defined(__QNX__)
  PCAP_API u_int	bpf_filter(const struct bpf_insn *, const u_char *, u_int, u_int);
#endif
PCAP_API int	bpf_validate(const struct bpf_insn *f, int len);
PCAP_API char	*bpf_image(const struct bpf_insn *, int);
PCAP_API void	bpf_dump(const struct bpf_program *, int);

#if defined(_WIN32)

  /*
   * Win32 definitions
   */

  /*!
    \brief A queue of raw packets that will be sent to the network with pcap_sendqueue_transmit().
  */
  struct pcap_send_queue
  {
	u_int maxlen;	/* Maximum size of the queue, in bytes. This
			   variable contains the size of the buffer field. */
	u_int len;	/* Current size of the queue, in bytes. */
	char *buffer;	/* Buffer containing the packets to be sent. */
  };

  typedef struct pcap_send_queue pcap_send_queue;

  /*!
    \brief This typedef is a support for the pcap_get_airpcap_handle() function
  */
  #if !defined(AIRPCAP_HANDLE__EAE405F5_0171_9592_B3C2_C19EC426AD34__DEFINED_)
    #define AIRPCAP_HANDLE__EAE405F5_0171_9592_B3C2_C19EC426AD34__DEFINED_
    typedef struct _AirpcapHandle *PAirpcapHandle;
  #endif

  PCAP_API int pcap_setbuff(pcap_t *p, int dim);
  PCAP_API int pcap_setmode(pcap_t *p, int mode);
  PCAP_API int pcap_setmintocopy(pcap_t *p, int size);

  PCAP_API HANDLE pcap_getevent(pcap_t *p);

  PCAP_API int pcap_oid_get_request(pcap_t *, bpf_u_int32, void *, size_t *);
  PCAP_API int pcap_oid_set_request(pcap_t *, bpf_u_int32, const void *, size_t *);

  PCAP_API pcap_send_queue* pcap_sendqueue_alloc(u_int memsize);

  PCAP_API void pcap_sendqueue_destroy(pcap_send_queue* queue);

  PCAP_API int pcap_sendqueue_queue(pcap_send_queue* queue, const struct pcap_pkthdr *pkt_header, const u_char *pkt_data);

  PCAP_API u_int pcap_sendqueue_transmit(pcap_t *p, pcap_send_queue* queue, int sync);

  PCAP_API struct pcap_stat *pcap_stats_ex(pcap_t *p, int *pcap_stat_size);

  PCAP_API int pcap_setuserbuffer(pcap_t *p, int size);

  PCAP_API int pcap_live_dump(pcap_t *p, char *filename, int maxsize, int maxpacks);

  PCAP_API int pcap_live_dump_ended(pcap_t *p, int sync);

  PCAP_API int pcap_start_oem(char* err_str, int flags);

  PCAP_API PAirpcapHandle pcap_get_airpcap_handle(pcap_t *p);

  #define MODE_CAPT 0
  #define MODE_STAT 1
  #define MODE_MON 2

#elif defined(MSDOS)

  /*
   * MS-DOS definitions
   */

  PCAP_API int  pcap_stats_ex (pcap_t *, struct pcap_stat_ex *);
  PCAP_API void pcap_set_wait (pcap_t *p, void (*yield)(void), int wait);
  PCAP_API u_long pcap_mac_packets (void);

#else /* UN*X */

  /*
   * UN*X definitions
   */

  PCAP_API int	pcap_get_selectable_fd(pcap_t *);
  PCAP_API struct timeval *pcap_get_required_select_timeout(pcap_t *);

#endif /* _WIN32/MSDOS/UN*X */

#if 0	/* Remote capture is disabled on FreeBSD */
/*
 * Remote capture definitions.
 *
 * These routines are only present if libpcap has been configured to
 * include remote capture support.
 */

/*
 * The maximum buffer size in which address, port, interface names are kept.
 *
 * In case the adapter name or such is larger than this value, it is truncated.
 * This is not used by the user; however it must be aware that an hostname / interface
 * name longer than this value will be truncated.
 */
#define PCAP_BUF_SIZE 1024

/*
 * The type of input source, passed to pcap_open().
 */
#define PCAP_SRC_FILE		2	/* local savefile */
#define PCAP_SRC_IFLOCAL	3	/* local network interface */
#define PCAP_SRC_IFREMOTE	4	/* interface on a remote host, using RPCAP */

/*
 * The formats allowed by pcap_open() are the following:
 * - file://path_and_filename [opens a local file]
 * - rpcap://devicename [opens the selected device devices available on the local host, without using the RPCAP protocol]
 * - rpcap://host/devicename [opens the selected device available on a remote host]
 * - rpcap://host:port/devicename [opens the selected device available on a remote host, using a non-standard port for RPCAP]
 * - adaptername [to open a local adapter; kept for compability, but it is strongly discouraged]
 * - (NULL) [to open the first local adapter; kept for compability, but it is strongly discouraged]
 *
 * The formats allowed by the pcap_findalldevs_ex() are the following:
 * - file://folder/ [lists all the files in the given folder]
 * - rpcap:// [lists all local adapters]
 * - rpcap://host:port/ [lists the devices available on a remote host]
 *
 * Referring to the 'host' and 'port' parameters, they can be either numeric or literal. Since
 * IPv6 is fully supported, these are the allowed formats:
 *
 * - host (literal): e.g. host.foo.bar
 * - host (numeric IPv4): e.g. 10.11.12.13
 * - host (numeric IPv4, IPv6 style): e.g. [10.11.12.13]
 * - host (numeric IPv6): e.g. [1:2:3::4]
 * - port: can be either numeric (e.g. '80') or literal (e.g. 'http')
 *
 * Here you find some allowed examples:
 * - rpcap://host.foo.bar/devicename [everything literal, no port number]
 * - rpcap://host.foo.bar:1234/devicename [everything literal, with port number]
 * - rpcap://10.11.12.13/devicename [IPv4 numeric, no port number]
 * - rpcap://10.11.12.13:1234/devicename [IPv4 numeric, with port number]
 * - rpcap://[10.11.12.13]:1234/devicename [IPv4 numeric with IPv6 format, with port number]
 * - rpcap://[1:2:3::4]/devicename [IPv6 numeric, no port number]
 * - rpcap://[1:2:3::4]:1234/devicename [IPv6 numeric, with port number]
 * - rpcap://[1:2:3::4]:http/devicename [IPv6 numeric, with literal port number]
 */

/*
 * URL schemes for capture source.
 */
/*
 * This string indicates that the user wants to open a capture from a
 * local file.
 */
#define PCAP_SRC_FILE_STRING "file://"
/*
 * This string indicates that the user wants to open a capture from a
 * network interface.  This string does not necessarily involve the use
 * of the RPCAP protocol. If the interface required resides on the local
 * host, the RPCAP protocol is not involved and the local functions are used.
 */
#define PCAP_SRC_IF_STRING "rpcap://"

/*
 * Flags to pass to pcap_open().
 */

/*
 * Specifies whether promiscuous mode is to be used.
 */
#define PCAP_OPENFLAG_PROMISCUOUS		0x00000001

/*
 * Specifies, for an RPCAP capture, whether the data transfer (in
 * case of a remote capture) has to be done with UDP protocol.
 *
 * If it is '1' if you want a UDP data connection, '0' if you want
 * a TCP data connection; control connection is always TCP-based.
 * A UDP connection is much lighter, but it does not guarantee that all
 * the captured packets arrive to the client workstation. Moreover,
 * it could be harmful in case of network congestion.
 * This flag is meaningless if the source is not a remote interface.
 * In that case, it is simply ignored.
 */
#define PCAP_OPENFLAG_DATATX_UDP		0x00000002

/*
 * Specifies wheether the remote probe will capture its own generated
 * traffic.
 *
 * In case the remote probe uses the same interface to capture traffic
 * and to send data back to the caller, the captured traffic includes
 * the RPCAP traffic as well.  If this flag is turned on, the RPCAP
 * traffic is excluded from the capture, so that the trace returned
 * back to the collector is does not include this traffic.
 *
 * Has no effect on local interfaces or savefiles.
 */
#define PCAP_OPENFLAG_NOCAPTURE_RPCAP		0x00000004

/*
 * Specifies whether the local adapter will capture its own generated traffic.
 *
 * This flag tells the underlying capture driver to drop the packets
 * that were sent by itself.  This is useful when building applications
 * such as bridges that should ignore the traffic they just sent.
 *
 * Supported only on Windows.
 */
#define PCAP_OPENFLAG_NOCAPTURE_LOCAL		0x00000008

/*
 * This flag configures the adapter for maximum responsiveness.
 *
 * In presence of a large value for nbytes, WinPcap waits for the arrival
 * of several packets before copying the data to the user. This guarantees
 * a low number of system calls, i.e. lower processor usage, i.e. better
 * performance, which is good for applications like sniffers. If the user
 * sets the PCAP_OPENFLAG_MAX_RESPONSIVENESS flag, the capture driver will
 * copy the packets as soon as the application is ready to receive them.
 * This is suggested for real time applications (such as, for example,
 * a bridge) that need the best responsiveness.
 *
 * The equivalent with pcap_create()/pcap_activate() is "immediate mode".
 */
#define PCAP_OPENFLAG_MAX_RESPONSIVENESS	0x00000010

/*
 * Remote authentication methods.
 * These are used in the 'type' member of the pcap_rmtauth structure.
 */

/*
 * NULL authentication.
 *
 * The 'NULL' authentication has to be equal to 'zero', so that old
 * applications can just put every field of struct pcap_rmtauth to zero,
 * and it does work.
 */
#define RPCAP_RMTAUTH_NULL 0
/*
 * Username/password authentication.
 *
 * With this type of authentication, the RPCAP protocol will use the username/
 * password provided to authenticate the user on the remote machine. If the
 * authentication is successful (and the user has the right to open network
 * devices) the RPCAP connection will continue; otherwise it will be dropped.
 *
 * *******NOTE********: the username and password are sent over the network
 * to the capture server *IN CLEAR TEXT*.  Don't use this on a network
 * that you don't completely control!  (And be *really* careful in your
 * definition of "completely"!)
 */
#define RPCAP_RMTAUTH_PWD 1

/*
 * This structure keeps the information needed to autheticate the user
 * on a remote machine.
 *
 * The remote machine can either grant or refuse the access according
 * to the information provided.
 * In case the NULL authentication is required, both 'username' and
 * 'password' can be NULL pointers.
 *
 * This structure is meaningless if the source is not a remote interface;
 * in that case, the functions which requires such a structure can accept
 * a NULL pointer as well.
 */
struct pcap_rmtauth
{
	/*
	 * \brief Type of the authentication required.
	 *
	 * In order to provide maximum flexibility, we can support different types
	 * of authentication based on the value of this 'type' variable. The currently
	 * supported authentication methods are defined into the
	 * \link remote_auth_methods Remote Authentication Methods Section\endlink.
	 */
	int type;
	/*
	 * \brief Zero-terminated string containing the username that has to be
	 * used on the remote machine for authentication.
	 *
	 * This field is meaningless in case of the RPCAP_RMTAUTH_NULL authentication
	 * and it can be NULL.
	 */
	char *username;
	/*
	 * \brief Zero-terminated string containing the password that has to be
	 * used on the remote machine for authentication.
	 *
	 * This field is meaningless in case of the RPCAP_RMTAUTH_NULL authentication
	 * and it can be NULL.
	 */
	char *password;
};

/*
 * This routine can open a savefile, a local device, or a device on
 * a remote machine running an RPCAP server.
 *
 * For opening a savefile, the pcap_open_offline routines can be used,
 * and will work just as well; code using them will work on more
 * platforms than code using pcap_open() to open savefiles.
 *
 * For opening a local device, pcap_open_live() can be used; it supports
 * most of the capabilities that pcap_open() supports, and code using it
 * will work on more platforms than code using pcap_open().  pcap_create()
 * and pcap_activate() can also be used; they support all capabilities
 * that pcap_open() supports, except for the Windows-only
 * PCAP_OPENFLAG_NOCAPTURE_LOCAL, and support additional capabilities.
 *
 * For opening a remote capture, pcap_open() is currently the only
 * API available.
 */
PCAP_API pcap_t	*pcap_open(const char *source, int snaplen, int flags,
	    int read_timeout, struct pcap_rmtauth *auth, char *errbuf);
PCAP_API int	pcap_createsrcstr(char *source, int type, const char *host,
	    const char *port, const char *name, char *errbuf);
PCAP_API int	pcap_parsesrcstr(const char *source, int *type, char *host,
	    char *port, char *name, char *errbuf);

/*
 * This routine can scan a directory for savefiles, list local capture
 * devices, or list capture devices on a remote machine running an RPCAP
 * server.
 *
 * For scanning for savefiles, it can be used on both UN*X systems and
 * Windows systems; for each directory entry it sees, it tries to open
 * the file as a savefile using pcap_open_offline(), and only includes
 * it in the list of files if the open succeeds, so it filters out
 * files for which the user doesn't have read permission, as well as
 * files that aren't valid savefiles readable by libpcap.
 *
 * For listing local capture devices, it's just a wrapper around
 * pcap_findalldevs(); code using pcap_findalldevs() will work on more
 * platforms than code using pcap_findalldevs_ex().
 *
 * For listing remote capture devices, pcap_findalldevs_ex() is currently
 * the only API available.
 */
PCAP_API int	pcap_findalldevs_ex(char *source, struct pcap_rmtauth *auth,
	    pcap_if_t **alldevs, char *errbuf);

/*
 * Sampling methods.
 *
 * These allow pcap_loop(), pcap_dispatch(), pcap_next(), and pcap_next_ex()
 * to see only a sample of packets, rather than all packets.
 *
 * Currently, they work only on Windows local captures.
 */

/*
 * Specifies that no sampling is to be done on the current capture.
 *
 * In this case, no sampling algorithms are applied to the current capture.
 */
#define PCAP_SAMP_NOSAMP	0

/*
 * Specifies that only 1 out of N packets must be returned to the user.
 *
 * In this case, the 'value' field of the 'pcap_samp' structure indicates the
 * number of packets (minus 1) that must be discarded before one packet got
 * accepted.
 * In other words, if 'value = 10', the first packet is returned to the
 * caller, while the following 9 are discarded.
 */
#define PCAP_SAMP_1_EVERY_N	1

/*
 * Specifies that we have to return 1 packet every N milliseconds.
 *
 * In this case, the 'value' field of the 'pcap_samp' structure indicates
 * the 'waiting time' in milliseconds before one packet got accepted.
 * In other words, if 'value = 10', the first packet is returned to the
 * caller; the next returned one will be the first packet that arrives
 * when 10ms have elapsed.
 */
#define PCAP_SAMP_FIRST_AFTER_N_MS 2

/*
 * This structure defines the information related to sampling.
 *
 * In case the sampling is requested, the capturing device should read
 * only a subset of the packets coming from the source. The returned packets
 * depend on the sampling parameters.
 *
 * WARNING: The sampling process is applied *after* the filtering process.
 * In other words, packets are filtered first, then the sampling process
 * selects a subset of the 'filtered' packets and it returns them to the
 * caller.
 */
struct pcap_samp
{
	/*
	 * Method used for sampling; see above.
	 */
	int method;

	/*
	 * This value depends on the sampling method defined.
	 * For its meaning, see above.
	 */
	int value;
};

/*
 * New functions.
 */
PCAP_API struct pcap_samp *pcap_setsampling(pcap_t *p);

/*
 * RPCAP active mode.
 */

/* Maximum length of an host name (needed for the RPCAP active mode) */
#define RPCAP_HOSTLIST_SIZE 1024

/*
 * Some minor differences between UN*X sockets and and Winsock sockets.
 */
#ifndef _WIN32
  /*!
   * \brief In Winsock, a socket handle is of type SOCKET; in UN*X, it's
   * a file descriptor, and therefore a signed integer.
   * We define SOCKET to be a signed integer on UN*X, so that it can
   * be used on both platforms.
   */
  #define SOCKET int

  /*!
   * \brief In Winsock, the error return if socket() fails is INVALID_SOCKET;
   * in UN*X, it's -1.
   * We define INVALID_SOCKET to be -1 on UN*X, so that it can be used on
   * both platforms.
   */
  #define INVALID_SOCKET -1
#endif

PCAP_API SOCKET	pcap_remoteact_accept(const char *address, const char *port,
	    const char *hostlist, char *connectinghost,
	    struct pcap_rmtauth *auth, char *errbuf);
PCAP_API int	pcap_remoteact_list(char *hostlist, char sep, int size,
	    char *errbuf);
PCAP_API int	pcap_remoteact_close(const char *host, char *errbuf);
PCAP_API void	pcap_remoteact_cleanup(void);
#endif	/* Remote capture is disabled on FreeBSD */

#ifdef __cplusplus
}
#endif

#endif /* lib_pcap_pcap_h */
