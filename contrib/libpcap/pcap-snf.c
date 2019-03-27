#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef _WIN32
#include <sys/param.h>
#endif /* !_WIN32 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <ctype.h>
#ifndef _WIN32
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif /* !_WIN32 */

#include <snf.h>
#if SNF_VERSION_API >= 0x0003
#define SNF_HAVE_INJECT_API
#endif

#include "pcap-int.h"
#include "pcap-snf.h"

/*
 * Private data for capturing on SNF devices.
 */
struct pcap_snf {
	snf_handle_t snf_handle; /* opaque device handle */
	snf_ring_t   snf_ring;   /* opaque device ring handle */
#ifdef SNF_HAVE_INJECT_API
	snf_inject_t snf_inj;    /* inject handle, if inject is used */
#endif
	int          snf_timeout;
	int          snf_boardnum;
};

static int
snf_set_datalink(pcap_t *p, int dlt)
{
	p->linktype = dlt;
	return (0);
}

static int
snf_pcap_stats(pcap_t *p, struct pcap_stat *ps)
{
	struct snf_ring_stats stats;
	struct pcap_snf *snfps = p->priv;
	int rc;

	if ((rc = snf_ring_getstats(snfps->snf_ring, &stats))) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    rc, "snf_get_stats");
		return -1;
	}
	ps->ps_recv = stats.ring_pkt_recv + stats.ring_pkt_overflow;
	ps->ps_drop = stats.ring_pkt_overflow;
	ps->ps_ifdrop = stats.nic_pkt_overflow + stats.nic_pkt_bad;
	return 0;
}

static void
snf_platform_cleanup(pcap_t *p)
{
	struct pcap_snf *ps = p->priv;

#ifdef SNF_HAVE_INJECT_API
	if (ps->snf_inj)
		snf_inject_close(ps->snf_inj);
#endif
	snf_ring_close(ps->snf_ring);
	snf_close(ps->snf_handle);
	pcap_cleanup_live_common(p);
}

static int
snf_getnonblock(pcap_t *p)
{
	struct pcap_snf *ps = p->priv;

	return (ps->snf_timeout == 0);
}

static int
snf_setnonblock(pcap_t *p, int nonblock)
{
	struct pcap_snf *ps = p->priv;

	if (nonblock)
		ps->snf_timeout = 0;
	else {
		if (p->opt.timeout <= 0)
			ps->snf_timeout = -1; /* forever */
		else
			ps->snf_timeout = p->opt.timeout;
	}
	return (0);
}

#define _NSEC_PER_SEC 1000000000

static inline
struct timeval
snf_timestamp_to_timeval(const int64_t ts_nanosec, const int tstamp_precision)
{
	struct timeval tv;
	long tv_nsec;
        const static struct timeval zero_timeval;

        if (ts_nanosec == 0)
                return zero_timeval;

	tv.tv_sec = ts_nanosec / _NSEC_PER_SEC;
	tv_nsec = (ts_nanosec % _NSEC_PER_SEC);

	/* libpcap expects tv_usec to be nanos if using nanosecond precision. */
	if (tstamp_precision == PCAP_TSTAMP_PRECISION_NANO)
		tv.tv_usec = tv_nsec;
	else
		tv.tv_usec = tv_nsec / 1000;

	return tv;
}

static int
snf_read(pcap_t *p, int cnt, pcap_handler callback, u_char *user)
{
	struct pcap_snf *ps = p->priv;
	struct pcap_pkthdr hdr;
	int i, flags, err, caplen, n;
	struct snf_recv_req req;
	int nonblock, timeout;

	if (!p)
		return -1;

	n = 0;
	timeout = ps->snf_timeout;
	while (n < cnt || PACKET_COUNT_IS_UNLIMITED(cnt)) {
		/*
		 * Has "pcap_breakloop()" been called?
		 */
		if (p->break_loop) {
			if (n == 0) {
				p->break_loop = 0;
				return (-2);
			} else {
				return (n);
			}
		}

		err = snf_ring_recv(ps->snf_ring, timeout, &req);

		if (err) {
			if (err == EBUSY || err == EAGAIN) {
				return (n);
			}
			else if (err == EINTR) {
				timeout = 0;
				continue;
			}
			else {
				pcap_fmt_errmsg_for_errno(p->errbuf,
				    PCAP_ERRBUF_SIZE, err, "snf_read");
				return -1;
			}
		}

		caplen = req.length;
		if (caplen > p->snapshot)
			caplen = p->snapshot;

		if ((p->fcode.bf_insns == NULL) ||
		     bpf_filter(p->fcode.bf_insns, req.pkt_addr, req.length, caplen)) {
			hdr.ts = snf_timestamp_to_timeval(req.timestamp, p->opt.tstamp_precision);
			hdr.caplen = caplen;
			hdr.len = req.length;
			callback(user, &hdr, req.pkt_addr);
		}
		n++;

		/* After one successful packet is received, we won't block
		* again for that timeout. */
		if (timeout != 0)
			timeout = 0;
	}
	return (n);
}

static int
snf_setfilter(pcap_t *p, struct bpf_program *fp)
{
	if (!p)
		return -1;
	if (!fp) {
		strncpy(p->errbuf, "setfilter: No filter specified",
			sizeof(p->errbuf));
		return -1;
	}

	/* Make our private copy of the filter */

	if (install_bpf_program(p, fp) < 0)
		return -1;

	return (0);
}

static int
snf_inject(pcap_t *p, const void *buf _U_, size_t size _U_)
{
#ifdef SNF_HAVE_INJECT_API
	struct pcap_snf *ps = p->priv;
	int rc;
	if (ps->snf_inj == NULL) {
		rc = snf_inject_open(ps->snf_boardnum, 0, &ps->snf_inj);
		if (rc) {
			pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
			    rc, "snf_inject_open");
			return (-1);
		}
	}

	rc = snf_inject_send(ps->snf_inj, -1, 0, buf, size);
	if (!rc) {
		return (size);
	}
	else {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    rc, "snf_inject_send");
		return (-1);
	}
#else
	strlcpy(p->errbuf, "Sending packets isn't supported with this snf version",
	    PCAP_ERRBUF_SIZE);
	return (-1);
#endif
}

static int
snf_activate(pcap_t* p)
{
	struct pcap_snf *ps = p->priv;
	char *device = p->opt.device;
	const char *nr = NULL;
	int err;
	int flags = -1, ring_id = -1;

	if (device == NULL) {
		pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "device is NULL");
		return -1;
	}

	/* In Libpcap, we set pshared by default if NUM_RINGS is set to > 1.
	 * Since libpcap isn't thread-safe */
	if ((nr = getenv("SNF_FLAGS")) && *nr)
		flags = strtol(nr, NULL, 0);
	else if ((nr = getenv("SNF_NUM_RINGS")) && *nr && atoi(nr) > 1)
		flags = SNF_F_PSHARED;
	else
		nr = NULL;


        /* Allow pcap_set_buffer_size() to set dataring_size.
         * Default is zero which allows setting from env SNF_DATARING_SIZE.
         * pcap_set_buffer_size() is in bytes while snf_open() accepts values
         * between 0 and 1048576 in Megabytes. Values in this range are
         * mapped to 1MB.
         */
	err = snf_open(ps->snf_boardnum,
			0, /* let SNF API parse SNF_NUM_RINGS, if set */
			NULL, /* default RSS, or use SNF_RSS_FLAGS env */
                        (p->opt.buffer_size > 0 && p->opt.buffer_size < 1048576) ? 1048576 : p->opt.buffer_size, /* default to SNF_DATARING_SIZE from env */
			flags, /* may want pshared */
			&ps->snf_handle);
	if (err != 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    err, "snf_open failed");
		return -1;
	}

	if ((nr = getenv("SNF_PCAP_RING_ID")) && *nr) {
		ring_id = (int) strtol(nr, NULL, 0);
	}
	err = snf_ring_open_id(ps->snf_handle, ring_id, &ps->snf_ring);
	if (err != 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    err, "snf_ring_open_id(ring=%d) failed", ring_id);
		return -1;
	}

	/*
	 * Turn a negative snapshot value (invalid), a snapshot value of
	 * 0 (unspecified), or a value bigger than the normal maximum
	 * value, into the maximum allowed value.
	 *
	 * If some application really *needs* a bigger snapshot
	 * length, we should just increase MAXIMUM_SNAPLEN.
	 */
	if (p->snapshot <= 0 || p->snapshot > MAXIMUM_SNAPLEN)
		p->snapshot = MAXIMUM_SNAPLEN;

	if (p->opt.timeout <= 0)
		ps->snf_timeout = -1;
	else
		ps->snf_timeout = p->opt.timeout;

	err = snf_start(ps->snf_handle);
	if (err != 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    err, "snf_start failed");
		return -1;
	}

	/*
	 * "select()" and "poll()" don't work on snf descriptors.
	 */
#ifndef _WIN32
	p->selectable_fd = -1;
#endif /* !_WIN32 */
	p->linktype = DLT_EN10MB;
	p->read_op = snf_read;
	p->inject_op = snf_inject;
	p->setfilter_op = snf_setfilter;
	p->setdirection_op = NULL; /* Not implemented.*/
	p->set_datalink_op = snf_set_datalink;
	p->getnonblock_op = snf_getnonblock;
	p->setnonblock_op = snf_setnonblock;
	p->stats_op = snf_pcap_stats;
	p->cleanup_op = snf_platform_cleanup;
#ifdef SNF_HAVE_INJECT_API
	ps->snf_inj = NULL;
#endif
	return 0;
}

#define MAX_DESC_LENGTH 128
int
snf_findalldevs(pcap_if_list_t *devlistp, char *errbuf)
{
	pcap_if_t *dev;
#ifdef _WIN32
	struct sockaddr_in addr;
#endif
	struct snf_ifaddrs *ifaddrs, *ifa;
	char name[MAX_DESC_LENGTH];
	char desc[MAX_DESC_LENGTH];
	int ret, allports = 0, merge = 0;
	const char *nr = NULL;

	if (snf_init(SNF_VERSION_API)) {
		(void)pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE,
		    "snf_getifaddrs: snf_init failed");
		return (-1);
	}

	if (snf_getifaddrs(&ifaddrs) || ifaddrs == NULL)
	{
		pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
		    errno, "snf_getifaddrs");
		return (-1);
	}
	if ((nr = getenv("SNF_FLAGS")) && *nr) {
		errno = 0;
		merge = strtol(nr, NULL, 0);
		if (errno) {
			(void)pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE,
				"snf_getifaddrs: SNF_FLAGS is not a valid number");
			return (-1);
		}
		merge = merge & SNF_F_AGGREGATE_PORTMASK;
	}

	for (ifa = ifaddrs; ifa != NULL; ifa = ifa->snf_ifa_next) {
		/*
		 * Myricom SNF adapter ports may appear as regular
		 * network interfaces, which would already have been
		 * added to the list of adapters by pcap_platform_finddevs()
		 * if this isn't an SNF-only version of libpcap.
		 *
		 * Our create routine intercepts pcap_create() calls for
		 * those interfaces and arranges that they will be
		 * opened using the SNF API instead.
		 *
		 * So if we already have an entry for the device, we
		 * don't add an additional entry for it, we just
		 * update the description for it, if any, to indicate
		 * which snfN device it is.  Otherwise, we add an entry
		 * for it.
		 *
		 * In either case, if SNF_F_AGGREGATE_PORTMASK is set
		 * in SNF_FLAGS, we add this port to the bitmask
		 * of ports, which we use to generate a device
		 * we can use to capture on all ports.
		 *
		 * Generate the description string.  If port aggregation
		 * is set, use 2^{port number} as the unit number,
		 * rather than {port number}.
		 *
		 * XXX - do entries in this list have IP addresses for
		 * the port?  If so, should we add them to the
		 * entry for the device, if they're not already in the
		 * list of IP addresses for the device?
 		 */
		(void)pcap_snprintf(desc,MAX_DESC_LENGTH,"Myricom %ssnf%d",
			merge ? "Merge Bitmask Port " : "",
			merge ? 1 << ifa->snf_ifa_portnum : ifa->snf_ifa_portnum);
		/*
		 * Add the port to the bitmask.
		 */
		if (merge)
			allports |= 1 << ifa->snf_ifa_portnum;
		/*
		 * See if there's already an entry for the device
		 * with the name ifa->snf_ifa_name.
		 */
		dev = find_dev(devlistp, ifa->snf_ifa_name);
		if (dev != NULL) {
			/*
			 * Yes.  Update its description.
			 */
			char *desc_str;

			desc_str = strdup(desc);
			if (desc_str == NULL) {
				pcap_fmt_errmsg_for_errno(errbuf,
				    PCAP_ERRBUF_SIZE, errno,
				    "snf_findalldevs strdup");
				return -1;
			}
			free(dev->description);
			dev->description = desc_str;
		} else {
			/*
			 * No.  Add an entry for it.
			 *
			 * XXX - is there a notion of "up" or "running",
			 * and can we determine whether something's
			 * plugged into the adapter and set
			 * PCAP_IF_CONNECTION_STATUS_CONNECTED or
			 * PCAP_IF_CONNECTION_STATUS_DISCONNECTED?
			 */
			dev = add_dev(devlistp, ifa->snf_ifa_name, 0, desc,
			    errbuf);
			if (dev == NULL)
				return -1;
#ifdef _WIN32
			/*
			 * On Windows, fill in IP# from device name
			 */
                        ret = inet_pton(AF_INET, dev->name, &addr.sin_addr);
                        if (ret == 1) {
                        	/*
                        	 * Successful conversion of device name
                        	 * to IPv4 address.
                        	 */
	                        addr.sin_family = AF_INET;
        	                if (add_addr_to_dev(dev, &addr, sizeof(addr),
                	            NULL, 0, NULL, 0, NULL, 0, errbuf) == -1)
                        		return -1;
                        } else if (ret == -1) {
				/*
				 * Error.
				 */
				pcap_fmt_errmsg_for_errno(errbuf,
				    PCAP_ERRBUF_SIZE, errno,
				    "sinf_findalldevs inet_pton");
                                return -1;
                        }
#endif _WIN32
		}
	}
	snf_freeifaddrs(ifaddrs);
	/*
	 * Create a snfX entry if port aggregation is enabled
       	 */
	if (merge) {
		/*
		 * Add a new entry with all ports bitmask
		 */
		(void)pcap_snprintf(name,MAX_DESC_LENGTH,"snf%d",allports);
		(void)pcap_snprintf(desc,MAX_DESC_LENGTH,"Myricom Merge Bitmask All Ports snf%d",
			allports);
		/*
		 * XXX - is there any notion of "up" and "running" that
		 * would apply to this device, given that it handles
		 * multiple ports?
		 *
		 * Presumably, there's no notion of "connected" vs.
		 * "disconnected", as "is this plugged into a network?"
		 * would be a per-port property.
		 */
		if (add_dev(devlistp, name,
		    PCAP_IF_CONNECTION_STATUS_NOT_APPLICABLE, desc,
		    errbuf) == NULL)
			return (-1);
		/*
		 * XXX - should we give it a list of addresses with all
		 * the addresses for all the ports?
		 */
	}

	return 0;
}

pcap_t *
snf_create(const char *device, char *ebuf, int *is_ours)
{
	pcap_t *p;
	int boardnum = -1;
	struct snf_ifaddrs *ifaddrs, *ifa;
	size_t devlen;
	struct pcap_snf *ps;

	if (snf_init(SNF_VERSION_API)) {
		/* Can't initialize the API, so no SNF devices */
		*is_ours = 0;
		return NULL;
	}

	/*
	 * Match a given interface name to our list of interface names, from
	 * which we can obtain the intended board number
	 */
	if (snf_getifaddrs(&ifaddrs) || ifaddrs == NULL) {
		/* Can't get SNF addresses */
		*is_ours = 0;
		return NULL;
	}
	devlen = strlen(device) + 1;
	ifa = ifaddrs;
	while (ifa) {
		if (strncmp(device, ifa->snf_ifa_name, devlen) == 0) {
			boardnum = ifa->snf_ifa_boardnum;
			break;
		}
		ifa = ifa->snf_ifa_next;
	}
	snf_freeifaddrs(ifaddrs);

	if (ifa == NULL) {
		/*
		 * If we can't find the device by name, support the name "snfX"
		 * and "snf10gX" where X is the board number.
		 */
		if (sscanf(device, "snf10g%d", &boardnum) != 1 &&
		    sscanf(device, "snf%d", &boardnum) != 1) {
			/* Nope, not a supported name */
			*is_ours = 0;
			return NULL;
		}
	}

	/* OK, it's probably ours. */
	*is_ours = 1;

	p = pcap_create_common(ebuf, sizeof (struct pcap_snf));
	if (p == NULL)
		return NULL;
	ps = p->priv;

	/*
	 * We support microsecond and nanosecond time stamps.
	 */
	p->tstamp_precision_count = 2;
	p->tstamp_precision_list = malloc(2 * sizeof(u_int));
	if (p->tstamp_precision_list == NULL) {
		pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE, errno,
		    "malloc");
		pcap_close(p);
		return NULL;
	}
	p->tstamp_precision_list[0] = PCAP_TSTAMP_PRECISION_MICRO;
	p->tstamp_precision_list[1] = PCAP_TSTAMP_PRECISION_NANO;

	p->activate_op = snf_activate;
	ps->snf_boardnum = boardnum;
	return p;
}

#ifdef SNF_ONLY
/*
 * This libpcap build supports only SNF cards, not regular network
 * interfaces..
 */

/*
 * There are no regular interfaces, just SNF interfaces.
 */
int
pcap_platform_finddevs(pcap_if_list_t *devlistp, char *errbuf)
{
	return (0);
}

/*
 * Attempts to open a regular interface fail.
 */
pcap_t *
pcap_create_interface(const char *device, char *errbuf)
{
	pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE,
	    "This version of libpcap only supports SNF cards");
	return NULL;
}

/*
 * Libpcap version string.
 */
const char *
pcap_lib_version(void)
{
	return (PCAP_VERSION_STRING " (SNF-only)");
}
#endif
