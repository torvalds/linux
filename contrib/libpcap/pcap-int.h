/*
 * Copyright (c) 1994, 1995, 1996
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

#ifndef pcap_int_h
#define	pcap_int_h

#include <signal.h>

#include <pcap/pcap.h>

#include "varattrs.h"
#include "fmtutils.h"

/*
 * Version string.
 * Uses PACKAGE_VERSION from config.h.
 */
#define PCAP_VERSION_STRING "libpcap version " PACKAGE_VERSION

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MSDOS
  #include <fcntl.h>
  #include <io.h>
#endif

/*
 * Swap byte ordering of unsigned long long timestamp on a big endian
 * machine.
 */
#define SWAPLL(ull)  ((ull & 0xff00000000000000ULL) >> 56) | \
                      ((ull & 0x00ff000000000000ULL) >> 40) | \
                      ((ull & 0x0000ff0000000000ULL) >> 24) | \
                      ((ull & 0x000000ff00000000ULL) >> 8)  | \
                      ((ull & 0x00000000ff000000ULL) << 8)  | \
                      ((ull & 0x0000000000ff0000ULL) << 24) | \
                      ((ull & 0x000000000000ff00ULL) << 40) | \
                      ((ull & 0x00000000000000ffULL) << 56)

/*
 * Maximum snapshot length.
 *
 * Somewhat arbitrary, but chosen to be:
 *
 *    1) big enough for maximum-size Linux loopback packets (65549)
 *       and some USB packets captured with USBPcap:
 *
 *           http://desowin.org/usbpcap/
 *
 *       (> 131072, < 262144)
 *
 * and
 *
 *    2) small enough not to cause attempts to allocate huge amounts of
 *       memory; some applications might use the snapshot length in a
 *       savefile header to control the size of the buffer they allocate,
 *       so a size of, say, 2^31-1 might not work well.
 *
 * We don't enforce this in pcap_set_snaplen(), but we use it internally.
 */
#define MAXIMUM_SNAPLEN		262144

struct pcap_opt {
	char	*device;
	int	timeout;	/* timeout for buffering */
	u_int	buffer_size;
	int	promisc;
	int	rfmon;		/* monitor mode */
	int	immediate;	/* immediate mode - deliver packets as soon as they arrive */
	int	nonblock;	/* non-blocking mode - don't wait for packets to be delivered, return "no packets available" */
	int	tstamp_type;
	int	tstamp_precision;

	/*
	 * Platform-dependent options.
	 */
#ifdef __linux__
	int	protocol;	/* protocol to use when creating PF_PACKET socket */
#endif
#ifdef _WIN32
	int	nocapture_local;/* disable NPF loopback */
#endif
};

typedef int	(*activate_op_t)(pcap_t *);
typedef int	(*can_set_rfmon_op_t)(pcap_t *);
typedef int	(*read_op_t)(pcap_t *, int cnt, pcap_handler, u_char *);
typedef int	(*next_packet_op_t)(pcap_t *, struct pcap_pkthdr *, u_char **);
typedef int	(*inject_op_t)(pcap_t *, const void *, size_t);
typedef void	(*save_current_filter_op_t)(pcap_t *, const char *);
typedef int	(*setfilter_op_t)(pcap_t *, struct bpf_program *);
typedef int	(*setdirection_op_t)(pcap_t *, pcap_direction_t);
typedef int	(*set_datalink_op_t)(pcap_t *, int);
typedef int	(*getnonblock_op_t)(pcap_t *);
typedef int	(*setnonblock_op_t)(pcap_t *, int);
typedef int	(*stats_op_t)(pcap_t *, struct pcap_stat *);
#ifdef _WIN32
typedef struct pcap_stat *(*stats_ex_op_t)(pcap_t *, int *);
typedef int	(*setbuff_op_t)(pcap_t *, int);
typedef int	(*setmode_op_t)(pcap_t *, int);
typedef int	(*setmintocopy_op_t)(pcap_t *, int);
typedef HANDLE	(*getevent_op_t)(pcap_t *);
typedef int	(*oid_get_request_op_t)(pcap_t *, bpf_u_int32, void *, size_t *);
typedef int	(*oid_set_request_op_t)(pcap_t *, bpf_u_int32, const void *, size_t *);
typedef u_int	(*sendqueue_transmit_op_t)(pcap_t *, pcap_send_queue *, int);
typedef int	(*setuserbuffer_op_t)(pcap_t *, int);
typedef int	(*live_dump_op_t)(pcap_t *, char *, int, int);
typedef int	(*live_dump_ended_op_t)(pcap_t *, int);
typedef PAirpcapHandle	(*get_airpcap_handle_op_t)(pcap_t *);
#endif
typedef void	(*cleanup_op_t)(pcap_t *);

/*
 * We put all the stuff used in the read code path at the beginning,
 * to try to keep it together in the same cache line or lines.
 */
struct pcap {
	/*
	 * Method to call to read packets on a live capture.
	 */
	read_op_t read_op;

	/*
	 * Method to call to read the next packet from a savefile.
	 */
	next_packet_op_t next_packet_op;

#ifdef _WIN32
	HANDLE handle;
#else
	int fd;
#endif /* _WIN32 */

	/*
	 * Read buffer.
	 */
	u_int bufsize;
	void *buffer;
	u_char *bp;
	int cc;

	sig_atomic_t break_loop; /* flag set to force break from packet-reading loop */

	void *priv;		/* private data for methods */

#ifdef ENABLE_REMOTE
	struct pcap_samp rmt_samp;	/* parameters related to the sampling process. */
#endif

	int swapped;
	FILE *rfile;		/* null if live capture, non-null if savefile */
	u_int fddipad;
	struct pcap *next;	/* list of open pcaps that need stuff cleared on close */

	/*
	 * File version number; meaningful only for a savefile, but we
	 * keep it here so that apps that (mistakenly) ask for the
	 * version numbers will get the same zero values that they
	 * always did.
	 */
	int version_major;
	int version_minor;

	int snapshot;
	int linktype;		/* Network linktype */
	int linktype_ext;       /* Extended information stored in the linktype field of a file */
	int tzoff;		/* timezone offset */
	int offset;		/* offset for proper alignment */
	int activated;		/* true if the capture is really started */
	int oldstyle;		/* if we're opening with pcap_open_live() */

	struct pcap_opt opt;

	/*
	 * Place holder for pcap_next().
	 */
	u_char *pkt;

#ifdef _WIN32
	struct pcap_stat stat;	/* used for pcap_stats_ex() */
#endif

	/* We're accepting only packets in this direction/these directions. */
	pcap_direction_t direction;

	/*
	 * Flags to affect BPF code generation.
	 */
	int bpf_codegen_flags;

#if !defined(_WIN32) && !defined(MSDOS)
	int selectable_fd;	/* FD on which select()/poll()/epoll_wait()/kevent()/etc. can be done */

	/*
	 * In case there either is no selectable FD, or there is but
	 * it doesn't necessarily work (e.g., if it doesn't get notified
	 * if the packet capture timeout expires before the buffer
	 * fills up), this points to a timeout that should be used
	 * in select()/poll()/epoll_wait()/kevent() call.  The pcap_t should
	 * be put into non-blocking mode, and, if the timeout expires on
	 * the call, an attempt should be made to read packets from all
	 * pcap_t's with a required timeout, and the code must be
	 * prepared not to see any packets from the attempt.
	 */
	struct timeval *required_select_timeout;
#endif

	/*
	 * Placeholder for filter code if bpf not in kernel.
	 */
	struct bpf_program fcode;

	char errbuf[PCAP_ERRBUF_SIZE + 1];
	int dlt_count;
	u_int *dlt_list;
	int tstamp_type_count;
	u_int *tstamp_type_list;
	int tstamp_precision_count;
	u_int *tstamp_precision_list;

	struct pcap_pkthdr pcap_header;	/* This is needed for the pcap_next_ex() to work */

	/*
	 * More methods.
	 */
	activate_op_t activate_op;
	can_set_rfmon_op_t can_set_rfmon_op;
	inject_op_t inject_op;
	save_current_filter_op_t save_current_filter_op;
	setfilter_op_t setfilter_op;
	setdirection_op_t setdirection_op;
	set_datalink_op_t set_datalink_op;
	getnonblock_op_t getnonblock_op;
	setnonblock_op_t setnonblock_op;
	stats_op_t stats_op;

	/*
	 * Routine to use as callback for pcap_next()/pcap_next_ex().
	 */
	pcap_handler oneshot_callback;

#ifdef _WIN32
	/*
	 * These are, at least currently, specific to the Win32 NPF
	 * driver.
	 */
	stats_ex_op_t stats_ex_op;
	setbuff_op_t setbuff_op;
	setmode_op_t setmode_op;
	setmintocopy_op_t setmintocopy_op;
	getevent_op_t getevent_op;
	oid_get_request_op_t oid_get_request_op;
	oid_set_request_op_t oid_set_request_op;
	sendqueue_transmit_op_t sendqueue_transmit_op;
	setuserbuffer_op_t setuserbuffer_op;
	live_dump_op_t live_dump_op;
	live_dump_ended_op_t live_dump_ended_op;
	get_airpcap_handle_op_t get_airpcap_handle_op;
#endif
	cleanup_op_t cleanup_op;
};

/*
 * BPF code generation flags.
 */
#define BPF_SPECIAL_VLAN_HANDLING	0x00000001	/* special VLAN handling for Linux */

/*
 * This is a timeval as stored in a savefile.
 * It has to use the same types everywhere, independent of the actual
 * `struct timeval'; `struct timeval' has 32-bit tv_sec values on some
 * platforms and 64-bit tv_sec values on other platforms, and writing
 * out native `struct timeval' values would mean files could only be
 * read on systems with the same tv_sec size as the system on which
 * the file was written.
 */

struct pcap_timeval {
    bpf_int32 tv_sec;		/* seconds */
    bpf_int32 tv_usec;		/* microseconds */
};

/*
 * This is a `pcap_pkthdr' as actually stored in a savefile.
 *
 * Do not change the format of this structure, in any way (this includes
 * changes that only affect the length of fields in this structure),
 * and do not make the time stamp anything other than seconds and
 * microseconds (e.g., seconds and nanoseconds).  Instead:
 *
 *	introduce a new structure for the new format;
 *
 *	send mail to "tcpdump-workers@lists.tcpdump.org", requesting
 *	a new magic number for your new capture file format, and, when
 *	you get the new magic number, put it in "savefile.c";
 *
 *	use that magic number for save files with the changed record
 *	header;
 *
 *	make the code in "savefile.c" capable of reading files with
 *	the old record header as well as files with the new record header
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

struct pcap_sf_pkthdr {
    struct pcap_timeval ts;	/* time stamp */
    bpf_u_int32 caplen;		/* length of portion present */
    bpf_u_int32 len;		/* length this packet (off wire) */
};

/*
 * How a `pcap_pkthdr' is actually stored in savefiles written
 * by some patched versions of libpcap (e.g. the ones in Red
 * Hat Linux 6.1 and 6.2).
 *
 * Do not change the format of this structure, in any way (this includes
 * changes that only affect the length of fields in this structure).
 * Instead, introduce a new structure, as per the above.
 */

struct pcap_sf_patched_pkthdr {
    struct pcap_timeval ts;	/* time stamp */
    bpf_u_int32 caplen;		/* length of portion present */
    bpf_u_int32 len;		/* length this packet (off wire) */
    int		index;
    unsigned short protocol;
    unsigned char pkt_type;
};

/*
 * User data structure for the one-shot callback used for pcap_next()
 * and pcap_next_ex().
 */
struct oneshot_userdata {
	struct pcap_pkthdr *hdr;
	const u_char **pkt;
	pcap_t *pd;
};

#ifndef min
#define min(a, b) ((a) > (b) ? (b) : (a))
#endif

int	pcap_offline_read(pcap_t *, int, pcap_handler, u_char *);

#include <stdarg.h>

#include "portability.h"

/*
 * Does the packet count argument to a module's read routine say
 * "supply packets until you run out of packets"?
 */
#define PACKET_COUNT_IS_UNLIMITED(count)	((count) <= 0)

/*
 * Routines that most pcap implementations can use for non-blocking mode.
 */
#if !defined(_WIN32) && !defined(MSDOS)
int	pcap_getnonblock_fd(pcap_t *);
int	pcap_setnonblock_fd(pcap_t *p, int);
#endif

/*
 * Internal interfaces for "pcap_create()".
 *
 * "pcap_create_interface()" is the routine to do a pcap_create on
 * a regular network interface.  There are multiple implementations
 * of this, one for each platform type (Linux, BPF, DLPI, etc.),
 * with the one used chosen by the configure script.
 *
 * "pcap_create_common()" allocates and fills in a pcap_t, for use
 * by pcap_create routines.
 */
pcap_t	*pcap_create_interface(const char *, char *);
pcap_t	*pcap_create_common(char *, size_t);
int	pcap_do_addexit(pcap_t *);
void	pcap_add_to_pcaps_to_close(pcap_t *);
void	pcap_remove_from_pcaps_to_close(pcap_t *);
void	pcap_cleanup_live_common(pcap_t *);
int	pcap_check_activated(pcap_t *);

/*
 * Internal interfaces for "pcap_findalldevs()".
 *
 * A pcap_if_list_t * is a reference to a list of devices.
 *
 * A get_if_flags_func is a platform-dependent function called to get
 * additional interface flags.
 *
 * "pcap_platform_finddevs()" is the platform-dependent routine to
 * find local network interfaces.
 *
 * "pcap_findalldevs_interfaces()" is a helper to find those interfaces
 * using the "standard" mechanisms (SIOCGIFCONF, "getifaddrs()", etc.).
 *
 * "add_dev()" adds an entry to a pcap_if_list_t.
 *
 * "find_dev()" tries to find a device, by name, in a pcap_if_list_t.
 *
 * "find_or_add_dev()" checks whether a device is already in a pcap_if_list_t
 * and, if not, adds an entry for it.
 */
struct pcap_if_list;
typedef struct pcap_if_list pcap_if_list_t;
typedef int (*get_if_flags_func)(const char *, bpf_u_int32 *, char *);
int	pcap_platform_finddevs(pcap_if_list_t *, char *);
#if !defined(_WIN32) && !defined(MSDOS)
int	pcap_findalldevs_interfaces(pcap_if_list_t *, char *,
	    int (*)(const char *), get_if_flags_func);
#endif
pcap_if_t *find_or_add_dev(pcap_if_list_t *, const char *, bpf_u_int32,
	    get_if_flags_func, const char *, char *);
pcap_if_t *find_dev(pcap_if_list_t *, const char *);
pcap_if_t *add_dev(pcap_if_list_t *, const char *, bpf_u_int32, const char *,
	    char *);
int	add_addr_to_dev(pcap_if_t *, struct sockaddr *, size_t,
	    struct sockaddr *, size_t, struct sockaddr *, size_t,
	    struct sockaddr *dstaddr, size_t, char *errbuf);
#ifndef _WIN32
pcap_if_t *find_or_add_if(pcap_if_list_t *, const char *, bpf_u_int32,
	    get_if_flags_func, char *);
int	add_addr_to_if(pcap_if_list_t *, const char *, bpf_u_int32,
	    get_if_flags_func,
	    struct sockaddr *, size_t, struct sockaddr *, size_t,
	    struct sockaddr *, size_t, struct sockaddr *, size_t, char *);
#endif

/*
 * Internal interfaces for "pcap_open_offline()".
 *
 * "pcap_open_offline_common()" allocates and fills in a pcap_t, for use
 * by pcap_open_offline routines.
 *
 * "sf_cleanup()" closes the file handle associated with a pcap_t, if
 * appropriate, and frees all data common to all modules for handling
 * savefile types.
 */
pcap_t	*pcap_open_offline_common(char *ebuf, size_t size);
void	sf_cleanup(pcap_t *p);

/*
 * Internal interfaces for both "pcap_create()" and routines that
 * open savefiles.
 *
 * "pcap_oneshot()" is the standard one-shot callback for "pcap_next()"
 * and "pcap_next_ex()".
 */
void	pcap_oneshot(u_char *, const struct pcap_pkthdr *, const u_char *);

#ifdef _WIN32
void	pcap_win32_err_to_str(DWORD, char *);
#endif

int	install_bpf_program(pcap_t *, struct bpf_program *);

int	pcap_strcasecmp(const char *, const char *);

#ifdef YYDEBUG
extern int pcap_debug;
#endif

#ifdef __cplusplus
}
#endif

#endif
