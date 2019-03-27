/*
 * pcap-dag.c: Packet capture interface for Emulex EndaceDAG cards.
 *
 * The functionality of this code attempts to mimic that of pcap-linux as much
 * as possible.  This code is compiled in several different ways depending on
 * whether DAG_ONLY and HAVE_DAG_API are defined.  If HAVE_DAG_API is not
 * defined it should not get compiled in, otherwise if DAG_ONLY is defined then
 * the 'dag_' function calls are renamed to 'pcap_' equivalents.  If DAG_ONLY
 * is not defined then nothing is altered - the dag_ functions will be
 * called as required from their pcap-linux/bpf equivalents.
 *
 * Authors: Richard Littin, Sean Irvine ({richard,sean}@reeltwo.com)
 * Modifications: Jesper Peterson
 *                Koryn Grant
 *                Stephen Donnelly <stephen.donnelly@emulex.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/param.h>			/* optionally get BSD define */

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "pcap-int.h"

#include <ctype.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

struct mbuf;		/* Squelch compiler warnings on some platforms for */
struct rtentry;		/* declarations in <net/if.h> */
#include <net/if.h>

#include "dagnew.h"
#include "dagapi.h"
#include "dagpci.h"
#include "dag_config_api.h"

#include "pcap-dag.h"

/*
 * DAG devices have names beginning with "dag", followed by a number
 * from 0 to DAG_MAX_BOARDS, then optionally a colon and a stream number
 * from 0 to DAG_STREAM_MAX.
 */
#ifndef DAG_MAX_BOARDS
#define DAG_MAX_BOARDS 32
#endif


#ifndef ERF_TYPE_AAL5
#define ERF_TYPE_AAL5               4
#endif

#ifndef ERF_TYPE_MC_HDLC
#define ERF_TYPE_MC_HDLC            5
#endif

#ifndef ERF_TYPE_MC_RAW
#define ERF_TYPE_MC_RAW             6
#endif

#ifndef ERF_TYPE_MC_ATM
#define ERF_TYPE_MC_ATM             7
#endif

#ifndef ERF_TYPE_MC_RAW_CHANNEL
#define ERF_TYPE_MC_RAW_CHANNEL     8
#endif

#ifndef ERF_TYPE_MC_AAL5
#define ERF_TYPE_MC_AAL5            9
#endif

#ifndef ERF_TYPE_COLOR_HDLC_POS
#define ERF_TYPE_COLOR_HDLC_POS     10
#endif

#ifndef ERF_TYPE_COLOR_ETH
#define ERF_TYPE_COLOR_ETH          11
#endif

#ifndef ERF_TYPE_MC_AAL2
#define ERF_TYPE_MC_AAL2            12
#endif

#ifndef ERF_TYPE_IP_COUNTER
#define ERF_TYPE_IP_COUNTER         13
#endif

#ifndef ERF_TYPE_TCP_FLOW_COUNTER
#define ERF_TYPE_TCP_FLOW_COUNTER   14
#endif

#ifndef ERF_TYPE_DSM_COLOR_HDLC_POS
#define ERF_TYPE_DSM_COLOR_HDLC_POS 15
#endif

#ifndef ERF_TYPE_DSM_COLOR_ETH
#define ERF_TYPE_DSM_COLOR_ETH      16
#endif

#ifndef ERF_TYPE_COLOR_MC_HDLC_POS
#define ERF_TYPE_COLOR_MC_HDLC_POS  17
#endif

#ifndef ERF_TYPE_AAL2
#define ERF_TYPE_AAL2               18
#endif

#ifndef ERF_TYPE_COLOR_HASH_POS
#define ERF_TYPE_COLOR_HASH_POS     19
#endif

#ifndef ERF_TYPE_COLOR_HASH_ETH
#define ERF_TYPE_COLOR_HASH_ETH     20
#endif

#ifndef ERF_TYPE_INFINIBAND
#define ERF_TYPE_INFINIBAND         21
#endif

#ifndef ERF_TYPE_IPV4
#define ERF_TYPE_IPV4               22
#endif

#ifndef ERF_TYPE_IPV6
#define ERF_TYPE_IPV6               23
#endif

#ifndef ERF_TYPE_RAW_LINK
#define ERF_TYPE_RAW_LINK           24
#endif

#ifndef ERF_TYPE_INFINIBAND_LINK
#define ERF_TYPE_INFINIBAND_LINK    25
#endif

#ifndef ERF_TYPE_META
#define ERF_TYPE_META               27
#endif

#ifndef ERF_TYPE_PAD
#define ERF_TYPE_PAD                48
#endif

#define ATM_CELL_SIZE		52
#define ATM_HDR_SIZE		4

/*
 * A header containing additional MTP information.
 */
#define MTP2_SENT_OFFSET		0	/* 1 byte */
#define MTP2_ANNEX_A_USED_OFFSET	1	/* 1 byte */
#define MTP2_LINK_NUMBER_OFFSET		2	/* 2 bytes */
#define MTP2_HDR_LEN			4	/* length of the header */

#define MTP2_ANNEX_A_NOT_USED      0
#define MTP2_ANNEX_A_USED          1
#define MTP2_ANNEX_A_USED_UNKNOWN  2

/* SunATM pseudo header */
struct sunatm_hdr {
	unsigned char	flags;		/* destination and traffic type */
	unsigned char	vpi;		/* VPI */
	unsigned short	vci;		/* VCI */
};

/*
 * Private data for capturing on DAG devices.
 */
struct pcap_dag {
	struct pcap_stat stat;
	u_char	*dag_mem_bottom;	/* DAG card current memory bottom pointer */
	u_char	*dag_mem_top;	/* DAG card current memory top pointer */
	int	dag_fcs_bits;	/* Number of checksum bits from link layer */
	int	dag_flags;	/* Flags */
	int	dag_stream;	/* DAG stream number */
	int	dag_timeout;	/* timeout specified to pcap_open_live.
				 * Same as in linux above, introduce
				 * generally? */
	dag_card_ref_t dag_ref; /* DAG Configuration/Status API card reference */
	dag_component_t dag_root;	/* DAG CSAPI Root component */
	attr_uuid_t drop_attr;  /* DAG Stream Drop Attribute handle, if available */
	struct timeval required_select_timeout;
				/* Timeout caller must use in event loops */
};

typedef struct pcap_dag_node {
	struct pcap_dag_node *next;
	pcap_t *p;
	pid_t pid;
} pcap_dag_node_t;

static pcap_dag_node_t *pcap_dags = NULL;
static int atexit_handler_installed = 0;
static const unsigned short endian_test_word = 0x0100;

#define IS_BIGENDIAN() (*((unsigned char *)&endian_test_word))

#define MAX_DAG_PACKET 65536

static unsigned char TempPkt[MAX_DAG_PACKET];

#ifndef HAVE_DAG_LARGE_STREAMS_API
#define dag_attach_stream64(a, b, c, d) dag_attach_stream(a, b, c, d)
#define dag_get_stream_poll64(a, b, c, d, e) dag_get_stream_poll(a, b, c, d, e)
#define dag_set_stream_poll64(a, b, c, d, e) dag_set_stream_poll(a, b, c, d, e)
#define dag_size_t uint32_t
#endif

static int dag_setfilter(pcap_t *p, struct bpf_program *fp);
static int dag_stats(pcap_t *p, struct pcap_stat *ps);
static int dag_set_datalink(pcap_t *p, int dlt);
static int dag_get_datalink(pcap_t *p);
static int dag_setnonblock(pcap_t *p, int nonblock);

static void
delete_pcap_dag(pcap_t *p)
{
	pcap_dag_node_t *curr = NULL, *prev = NULL;

	for (prev = NULL, curr = pcap_dags; curr != NULL && curr->p != p; prev = curr, curr = curr->next) {
		/* empty */
	}

	if (curr != NULL && curr->p == p) {
		if (prev != NULL) {
			prev->next = curr->next;
		} else {
			pcap_dags = curr->next;
		}
	}
}

/*
 * Performs a graceful shutdown of the DAG card, frees dynamic memory held
 * in the pcap_t structure, and closes the file descriptor for the DAG card.
 */

static void
dag_platform_cleanup(pcap_t *p)
{
	struct pcap_dag *pd = p->priv;

	if(dag_stop_stream(p->fd, pd->dag_stream) < 0)
		fprintf(stderr,"dag_stop_stream: %s\n", strerror(errno));

	if(dag_detach_stream(p->fd, pd->dag_stream) < 0)
		fprintf(stderr,"dag_detach_stream: %s\n", strerror(errno));

	if(pd->dag_ref != NULL) {
		dag_config_dispose(pd->dag_ref);
		p->fd = -1;
		pd->dag_ref = NULL;
	}
	delete_pcap_dag(p);
	pcap_cleanup_live_common(p);
	/* Note: don't need to call close(p->fd) or dag_close(p->fd) as dag_config_dispose(pd->dag_ref) does this. */
}

static void
atexit_handler(void)
{
	while (pcap_dags != NULL) {
		if (pcap_dags->pid == getpid()) {
			if (pcap_dags->p != NULL)
				dag_platform_cleanup(pcap_dags->p);
		} else {
			delete_pcap_dag(pcap_dags->p);
		}
	}
}

static int
new_pcap_dag(pcap_t *p)
{
	pcap_dag_node_t *node = NULL;

	if ((node = malloc(sizeof(pcap_dag_node_t))) == NULL) {
		return -1;
	}

	if (!atexit_handler_installed) {
		atexit(atexit_handler);
		atexit_handler_installed = 1;
	}

	node->next = pcap_dags;
	node->p = p;
	node->pid = getpid();

	pcap_dags = node;

	return 0;
}

static unsigned int
dag_erf_ext_header_count(uint8_t * erf, size_t len)
{
	uint32_t hdr_num = 0;
	uint8_t  hdr_type;

	/* basic sanity checks */
	if ( erf == NULL )
		return 0;
	if ( len < 16 )
		return 0;

	/* check if we have any extension headers */
	if ( (erf[8] & 0x80) == 0x00 )
		return 0;

	/* loop over the extension headers */
	do {

		/* sanity check we have enough bytes */
		if ( len < (24 + (hdr_num * 8)) )
			return hdr_num;

		/* get the header type */
		hdr_type = erf[(16 + (hdr_num * 8))];
		hdr_num++;

	} while ( hdr_type & 0x80 );

	return hdr_num;
}

/*
 *  Read at most max_packets from the capture stream and call the callback
 *  for each of them. Returns the number of packets handled, -1 if an
 *  error occured, or -2 if we were told to break out of the loop.
 */
static int
dag_read(pcap_t *p, int cnt, pcap_handler callback, u_char *user)
{
	struct pcap_dag *pd = p->priv;
	unsigned int processed = 0;
	unsigned int nonblocking = pd->dag_flags & DAGF_NONBLOCK;
	unsigned int num_ext_hdr = 0;
	unsigned int ticks_per_second;

	/* Get the next bufferful of packets (if necessary). */
	while (pd->dag_mem_top - pd->dag_mem_bottom < dag_record_size) {

		/*
		 * Has "pcap_breakloop()" been called?
		 */
		if (p->break_loop) {
			/*
			 * Yes - clear the flag that indicates that
			 * it has, and return -2 to indicate that
			 * we were told to break out of the loop.
			 */
			p->break_loop = 0;
			return -2;
		}

		/* dag_advance_stream() will block (unless nonblock is called)
		 * until 64kB of data has accumulated.
		 * If to_ms is set, it will timeout before 64kB has accumulated.
		 * We wait for 64kB because processing a few packets at a time
		 * can cause problems at high packet rates (>200kpps) due
		 * to inefficiencies.
		 * This does mean if to_ms is not specified the capture may 'hang'
		 * for long periods if the data rate is extremely slow (<64kB/sec)
		 * If non-block is specified it will return immediately. The user
		 * is then responsible for efficiency.
		 */
		if ( NULL == (pd->dag_mem_top = dag_advance_stream(p->fd, pd->dag_stream, &(pd->dag_mem_bottom))) ) {
		     return -1;
		}

		if (nonblocking && (pd->dag_mem_top - pd->dag_mem_bottom < dag_record_size))
		{
			/* Pcap is configured to process only available packets, and there aren't any, return immediately. */
			return 0;
		}

		if(!nonblocking &&
		   pd->dag_timeout &&
		   (pd->dag_mem_top - pd->dag_mem_bottom < dag_record_size))
		{
			/* Blocking mode, but timeout set and no data has arrived, return anyway.*/
			return 0;
		}

	}

	/* Process the packets. */
	while (pd->dag_mem_top - pd->dag_mem_bottom >= dag_record_size) {

		unsigned short packet_len = 0;
		int caplen = 0;
		struct pcap_pkthdr	pcap_header;

		dag_record_t *header = (dag_record_t *)(pd->dag_mem_bottom);

		u_char *dp = ((u_char *)header); /* + dag_record_size; */
		unsigned short rlen;

		/*
		 * Has "pcap_breakloop()" been called?
		 */
		if (p->break_loop) {
			/*
			 * Yes - clear the flag that indicates that
			 * it has, and return -2 to indicate that
			 * we were told to break out of the loop.
			 */
			p->break_loop = 0;
			return -2;
		}

		rlen = ntohs(header->rlen);
		if (rlen < dag_record_size)
		{
			strncpy(p->errbuf, "dag_read: record too small", PCAP_ERRBUF_SIZE);
			return -1;
		}
		pd->dag_mem_bottom += rlen;

		/* Count lost packets. */
		switch((header->type & 0x7f)) {
			/* in these types the color value overwrites the lctr */
		case ERF_TYPE_COLOR_HDLC_POS:
		case ERF_TYPE_COLOR_ETH:
		case ERF_TYPE_DSM_COLOR_HDLC_POS:
		case ERF_TYPE_DSM_COLOR_ETH:
		case ERF_TYPE_COLOR_MC_HDLC_POS:
		case ERF_TYPE_COLOR_HASH_ETH:
		case ERF_TYPE_COLOR_HASH_POS:
			break;

		default:
			if ( (pd->drop_attr == kNullAttributeUuid) && (header->lctr) ) {
				pd->stat.ps_drop += ntohs(header->lctr);
			}
		}

		if ((header->type & 0x7f) == ERF_TYPE_PAD) {
			continue;
		}

		num_ext_hdr = dag_erf_ext_header_count(dp, rlen);

		/* ERF encapsulation */
		/* The Extensible Record Format is not dropped for this kind of encapsulation,
		 * and will be handled as a pseudo header by the decoding application.
		 * The information carried in the ERF header and in the optional subheader (if present)
		 * could be merged with the libpcap information, to offer a better decoding.
		 * The packet length is
		 * o the length of the packet on the link (header->wlen),
		 * o plus the length of the ERF header (dag_record_size), as the length of the
		 *   pseudo header will be adjusted during the decoding,
		 * o plus the length of the optional subheader (if present).
		 *
		 * The capture length is header.rlen and the byte stuffing for alignment will be dropped
		 * if the capture length is greater than the packet length.
		 */
		if (p->linktype == DLT_ERF) {
			packet_len = ntohs(header->wlen) + dag_record_size;
			caplen = rlen;
			switch ((header->type & 0x7f)) {
			case ERF_TYPE_MC_AAL5:
			case ERF_TYPE_MC_ATM:
			case ERF_TYPE_MC_HDLC:
			case ERF_TYPE_MC_RAW_CHANNEL:
			case ERF_TYPE_MC_RAW:
			case ERF_TYPE_MC_AAL2:
			case ERF_TYPE_COLOR_MC_HDLC_POS:
				packet_len += 4; /* MC header */
				break;

			case ERF_TYPE_COLOR_HASH_ETH:
			case ERF_TYPE_DSM_COLOR_ETH:
			case ERF_TYPE_COLOR_ETH:
			case ERF_TYPE_ETH:
				packet_len += 2; /* ETH header */
				break;
			} /* switch type */

			/* Include ERF extension headers */
			packet_len += (8 * num_ext_hdr);

			if (caplen > packet_len) {
				caplen = packet_len;
			}
		} else {
			/* Other kind of encapsulation according to the header Type */

			/* Skip over generic ERF header */
			dp += dag_record_size;
			/* Skip over extension headers */
			dp += 8 * num_ext_hdr;

			switch((header->type & 0x7f)) {
			case ERF_TYPE_ATM:
			case ERF_TYPE_AAL5:
				if ((header->type & 0x7f) == ERF_TYPE_AAL5) {
					packet_len = ntohs(header->wlen);
					caplen = rlen - dag_record_size;
				}
			case ERF_TYPE_MC_ATM:
				if ((header->type & 0x7f) == ERF_TYPE_MC_ATM) {
					caplen = packet_len = ATM_CELL_SIZE;
					dp+=4;
				}
			case ERF_TYPE_MC_AAL5:
				if ((header->type & 0x7f) == ERF_TYPE_MC_AAL5) {
					packet_len = ntohs(header->wlen);
					caplen = rlen - dag_record_size - 4;
					dp+=4;
				}
				/* Skip over extension headers */
				caplen -= (8 * num_ext_hdr);

				if ((header->type & 0x7f) == ERF_TYPE_ATM) {
					caplen = packet_len = ATM_CELL_SIZE;
				}
				if (p->linktype == DLT_SUNATM) {
					struct sunatm_hdr *sunatm = (struct sunatm_hdr *)dp;
					unsigned long rawatm;

					rawatm = ntohl(*((unsigned long *)dp));
					sunatm->vci = htons((rawatm >>  4) & 0xffff);
					sunatm->vpi = (rawatm >> 20) & 0x00ff;
					sunatm->flags = ((header->flags.iface & 1) ? 0x80 : 0x00) |
						((sunatm->vpi == 0 && sunatm->vci == htons(5)) ? 6 :
						 ((sunatm->vpi == 0 && sunatm->vci == htons(16)) ? 5 :
						  ((dp[ATM_HDR_SIZE] == 0xaa &&
						    dp[ATM_HDR_SIZE+1] == 0xaa &&
						    dp[ATM_HDR_SIZE+2] == 0x03) ? 2 : 1)));

				} else if (p->linktype == DLT_ATM_RFC1483) {
					packet_len -= ATM_HDR_SIZE;
					caplen -= ATM_HDR_SIZE;
					dp += ATM_HDR_SIZE;
				} else
					continue;
				break;

			case ERF_TYPE_COLOR_HASH_ETH:
			case ERF_TYPE_DSM_COLOR_ETH:
			case ERF_TYPE_COLOR_ETH:
			case ERF_TYPE_ETH:
				if ((p->linktype != DLT_EN10MB) &&
				    (p->linktype != DLT_DOCSIS))
					continue;
				packet_len = ntohs(header->wlen);
				packet_len -= (pd->dag_fcs_bits >> 3);
				caplen = rlen - dag_record_size - 2;
				/* Skip over extension headers */
				caplen -= (8 * num_ext_hdr);
				if (caplen > packet_len) {
					caplen = packet_len;
				}
				dp += 2;
				break;

			case ERF_TYPE_COLOR_HASH_POS:
			case ERF_TYPE_DSM_COLOR_HDLC_POS:
			case ERF_TYPE_COLOR_HDLC_POS:
			case ERF_TYPE_HDLC_POS:
				if ((p->linktype != DLT_CHDLC) &&
				    (p->linktype != DLT_PPP_SERIAL) &&
				    (p->linktype != DLT_FRELAY))
					continue;
				packet_len = ntohs(header->wlen);
				packet_len -= (pd->dag_fcs_bits >> 3);
				caplen = rlen - dag_record_size;
				/* Skip over extension headers */
				caplen -= (8 * num_ext_hdr);
				if (caplen > packet_len) {
					caplen = packet_len;
				}
				break;

			case ERF_TYPE_COLOR_MC_HDLC_POS:
			case ERF_TYPE_MC_HDLC:
				if ((p->linktype != DLT_CHDLC) &&
				    (p->linktype != DLT_PPP_SERIAL) &&
				    (p->linktype != DLT_FRELAY) &&
				    (p->linktype != DLT_MTP2) &&
				    (p->linktype != DLT_MTP2_WITH_PHDR) &&
				    (p->linktype != DLT_LAPD))
					continue;
				packet_len = ntohs(header->wlen);
				packet_len -= (pd->dag_fcs_bits >> 3);
				caplen = rlen - dag_record_size - 4;
				/* Skip over extension headers */
				caplen -= (8 * num_ext_hdr);
				if (caplen > packet_len) {
					caplen = packet_len;
				}
				/* jump the MC_HDLC_HEADER */
				dp += 4;
#ifdef DLT_MTP2_WITH_PHDR
				if (p->linktype == DLT_MTP2_WITH_PHDR) {
					/* Add the MTP2 Pseudo Header */
					caplen += MTP2_HDR_LEN;
					packet_len += MTP2_HDR_LEN;

					TempPkt[MTP2_SENT_OFFSET] = 0;
					TempPkt[MTP2_ANNEX_A_USED_OFFSET] = MTP2_ANNEX_A_USED_UNKNOWN;
					*(TempPkt+MTP2_LINK_NUMBER_OFFSET) = ((header->rec.mc_hdlc.mc_header>>16)&0x01);
					*(TempPkt+MTP2_LINK_NUMBER_OFFSET+1) = ((header->rec.mc_hdlc.mc_header>>24)&0xff);
					memcpy(TempPkt+MTP2_HDR_LEN, dp, caplen);
					dp = TempPkt;
				}
#endif
				break;

			case ERF_TYPE_IPV4:
				if ((p->linktype != DLT_RAW) &&
				    (p->linktype != DLT_IPV4))
					continue;
				packet_len = ntohs(header->wlen);
				caplen = rlen - dag_record_size;
				/* Skip over extension headers */
				caplen -= (8 * num_ext_hdr);
				if (caplen > packet_len) {
					caplen = packet_len;
				}
				break;

			case ERF_TYPE_IPV6:
				if ((p->linktype != DLT_RAW) &&
				    (p->linktype != DLT_IPV6))
					continue;
				packet_len = ntohs(header->wlen);
				caplen = rlen - dag_record_size;
				/* Skip over extension headers */
				caplen -= (8 * num_ext_hdr);
				if (caplen > packet_len) {
					caplen = packet_len;
				}
				break;

			/* These types have no matching 'native' DLT, but can be used with DLT_ERF above */
			case ERF_TYPE_MC_RAW:
			case ERF_TYPE_MC_RAW_CHANNEL:
			case ERF_TYPE_IP_COUNTER:
			case ERF_TYPE_TCP_FLOW_COUNTER:
			case ERF_TYPE_INFINIBAND:
			case ERF_TYPE_RAW_LINK:
			case ERF_TYPE_INFINIBAND_LINK:
			default:
				/* Unhandled ERF type.
				 * Ignore rather than generating error
				 */
				continue;
			} /* switch type */

		} /* ERF encapsulation */

		if (caplen > p->snapshot)
			caplen = p->snapshot;

		/* Run the packet filter if there is one. */
		if ((p->fcode.bf_insns == NULL) || bpf_filter(p->fcode.bf_insns, dp, packet_len, caplen)) {

			/* convert between timestamp formats */
			register unsigned long long ts;

			if (IS_BIGENDIAN()) {
				ts = SWAPLL(header->ts);
			} else {
				ts = header->ts;
			}

			switch (p->opt.tstamp_precision) {
			case PCAP_TSTAMP_PRECISION_NANO:
				ticks_per_second = 1000000000;
				break;
			case PCAP_TSTAMP_PRECISION_MICRO:
			default:
				ticks_per_second = 1000000;
				break;

			}
			pcap_header.ts.tv_sec = ts >> 32;
			ts = (ts & 0xffffffffULL) * ticks_per_second;
			ts += 0x80000000; /* rounding */
			pcap_header.ts.tv_usec = ts >> 32;
			if (pcap_header.ts.tv_usec >= ticks_per_second) {
				pcap_header.ts.tv_usec -= ticks_per_second;
				pcap_header.ts.tv_sec++;
			}

			/* Fill in our own header data */
			pcap_header.caplen = caplen;
			pcap_header.len = packet_len;

			/* Count the packet. */
			pd->stat.ps_recv++;

			/* Call the user supplied callback function */
			callback(user, &pcap_header, dp);

			/* Only count packets that pass the filter, for consistency with standard Linux behaviour. */
			processed++;
			if (processed == cnt && !PACKET_COUNT_IS_UNLIMITED(cnt))
			{
				/* Reached the user-specified limit. */
				return cnt;
			}
		}
	}

	return processed;
}

static int
dag_inject(pcap_t *p, const void *buf _U_, size_t size _U_)
{
	strlcpy(p->errbuf, "Sending packets isn't supported on DAG cards",
	    PCAP_ERRBUF_SIZE);
	return (-1);
}

/*
 *  Get a handle for a live capture from the given DAG device.  Passing a NULL
 *  device will result in a failure.  The promisc flag is ignored because DAG
 *  cards are always promiscuous.  The to_ms parameter is used in setting the
 *  API polling parameters.
 *
 *  snaplen is now also ignored, until we get per-stream slen support. Set
 *  slen with approprite DAG tool BEFORE pcap_activate().
 *
 *  See also pcap(3).
 */
static int dag_activate(pcap_t* p)
{
	struct pcap_dag *pd = p->priv;
	char *s;
	int n;
	daginf_t* daginf;
	char * newDev = NULL;
	char * device = p->opt.device;
	dag_size_t mindata;
	struct timeval maxwait;
	struct timeval poll;

	if (device == NULL) {
		pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "device is NULL");
		return -1;
	}

	/* Initialize some components of the pcap structure. */
	newDev = (char *)malloc(strlen(device) + 16);
	if (newDev == NULL) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "Can't allocate string for device name");
		goto fail;
	}

	/* Parse input name to get dag device and stream number if provided */
	if (dag_parse_name(device, newDev, strlen(device) + 16, &pd->dag_stream) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "dag_parse_name");
		goto fail;
	}
	device = newDev;

	if (pd->dag_stream%2) {
		pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "dag_parse_name: tx (even numbered) streams not supported for capture");
		goto fail;
	}

	/* setup device parameters */
	if((pd->dag_ref = dag_config_init((char *)device)) == NULL) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "dag_config_init %s", device);
		goto fail;
	}

	if((p->fd = dag_config_get_card_fd(pd->dag_ref)) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "dag_config_get_card_fd %s", device);
		goto fail;
	}

	/* Open requested stream. Can fail if already locked or on error */
	if (dag_attach_stream64(p->fd, pd->dag_stream, 0, 0) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "dag_attach_stream");
		goto failclose;
	}

	/* Try to find Stream Drop attribute */
	pd->drop_attr = kNullAttributeUuid;
	pd->dag_root = dag_config_get_root_component(pd->dag_ref);
	if ( dag_component_get_subcomponent(pd->dag_root, kComponentStreamFeatures, 0) )
	{
		pd->drop_attr = dag_config_get_indexed_attribute_uuid(pd->dag_ref, kUint32AttributeStreamDropCount, pd->dag_stream/2);
	}

	/* Set up default poll parameters for stream
	 * Can be overridden by pcap_set_nonblock()
	 */
	if (dag_get_stream_poll64(p->fd, pd->dag_stream,
				&mindata, &maxwait, &poll) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "dag_get_stream_poll");
		goto faildetach;
	}

	/* Use the poll time as the required select timeout for callers
	 * who are using select()/etc. in an event loop waiting for
	 * packets to arrive.
	 */
	pd->required_select_timeout = poll;
	p->required_select_timeout = &pd->required_select_timeout;

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

	if (p->opt.immediate) {
		/* Call callback immediately.
		 * XXX - is this the right way to p this?
		 */
		mindata = 0;
	} else {
		/* Amount of data to collect in Bytes before calling callbacks.
		 * Important for efficiency, but can introduce latency
		 * at low packet rates if to_ms not set!
		 */
		mindata = 65536;
	}

	/* Obey opt.timeout (was to_ms) if supplied. This is a good idea!
	 * Recommend 10-100ms. Calls will time out even if no data arrived.
	 */
	maxwait.tv_sec = p->opt.timeout/1000;
	maxwait.tv_usec = (p->opt.timeout%1000) * 1000;

	if (dag_set_stream_poll64(p->fd, pd->dag_stream,
				mindata, &maxwait, &poll) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "dag_set_stream_poll");
		goto faildetach;
	}

        /* XXX Not calling dag_configure() to set slen; this is unsafe in
	 * multi-stream environments as the gpp config is global.
         * Once the firmware provides 'per-stream slen' this can be supported
	 * again via the Config API without side-effects */
#if 0
	/* set the card snap length to the specified snaplen parameter */
	/* This is a really bad idea, as different cards have different
	 * valid slen ranges. Should fix in Config API. */
	if (p->snapshot == 0 || p->snapshot > MAX_DAG_SNAPLEN) {
		p->snapshot = MAX_DAG_SNAPLEN;
	} else if (snaplen < MIN_DAG_SNAPLEN) {
		p->snapshot = MIN_DAG_SNAPLEN;
	}
	/* snap len has to be a multiple of 4 */
#endif

	if(dag_start_stream(p->fd, pd->dag_stream) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "dag_start_stream %s", device);
		goto faildetach;
	}

	/*
	 * Important! You have to ensure bottom is properly
	 * initialized to zero on startup, it won't give you
	 * a compiler warning if you make this mistake!
	 */
	pd->dag_mem_bottom = 0;
	pd->dag_mem_top = 0;

	/*
	 * Find out how many FCS bits we should strip.
	 * First, query the card to see if it strips the FCS.
	 */
	daginf = dag_info(p->fd);
	if ((0x4200 == daginf->device_code) || (0x4230 == daginf->device_code))	{
		/* DAG 4.2S and 4.23S already strip the FCS.  Stripping the final word again truncates the packet. */
		pd->dag_fcs_bits = 0;

		/* Note that no FCS will be supplied. */
		p->linktype_ext = LT_FCS_DATALINK_EXT(0);
	} else {
		/*
		 * Start out assuming it's 32 bits.
		 */
		pd->dag_fcs_bits = 32;

		/* Allow an environment variable to override. */
		if ((s = getenv("ERF_FCS_BITS")) != NULL) {
			if ((n = atoi(s)) == 0 || n == 16 || n == 32) {
				pd->dag_fcs_bits = n;
			} else {
				pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
					"pcap_activate %s: bad ERF_FCS_BITS value (%d) in environment", device, n);
				goto failstop;
			}
		}

		/*
		 * Did the user request that they not be stripped?
		 */
		if ((s = getenv("ERF_DONT_STRIP_FCS")) != NULL) {
			/* Yes.  Note the number of bytes that will be
			   supplied. */
			p->linktype_ext = LT_FCS_DATALINK_EXT(pd->dag_fcs_bits/16);

			/* And don't strip them. */
			pd->dag_fcs_bits = 0;
		}
	}

	pd->dag_timeout	= p->opt.timeout;

	p->linktype = -1;
	if (dag_get_datalink(p) < 0)
		goto failstop;

	p->bufsize = 0;

	if (new_pcap_dag(p) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "new_pcap_dag %s", device);
		goto failstop;
	}

	/*
	 * "select()" and "poll()" don't work on DAG device descriptors.
	 */
	p->selectable_fd = -1;

	if (newDev != NULL) {
		free((char *)newDev);
	}

	p->read_op = dag_read;
	p->inject_op = dag_inject;
	p->setfilter_op = dag_setfilter;
	p->setdirection_op = NULL; /* Not implemented.*/
	p->set_datalink_op = dag_set_datalink;
	p->getnonblock_op = pcap_getnonblock_fd;
	p->setnonblock_op = dag_setnonblock;
	p->stats_op = dag_stats;
	p->cleanup_op = dag_platform_cleanup;
	pd->stat.ps_drop = 0;
	pd->stat.ps_recv = 0;
	pd->stat.ps_ifdrop = 0;
	return 0;

failstop:
	if (dag_stop_stream(p->fd, pd->dag_stream) < 0) {
		fprintf(stderr,"dag_stop_stream: %s\n", strerror(errno));
	}

faildetach:
	if (dag_detach_stream(p->fd, pd->dag_stream) < 0)
		fprintf(stderr,"dag_detach_stream: %s\n", strerror(errno));

failclose:
	dag_config_dispose(pd->dag_ref);
	delete_pcap_dag(p);

fail:
	pcap_cleanup_live_common(p);
	if (newDev != NULL) {
		free((char *)newDev);
	}

	return PCAP_ERROR;
}

pcap_t *dag_create(const char *device, char *ebuf, int *is_ours)
{
	const char *cp;
	char *cpend;
	long devnum;
	pcap_t *p;
	long stream = 0;

	/* Does this look like a DAG device? */
	cp = strrchr(device, '/');
	if (cp == NULL)
		cp = device;
	/* Does it begin with "dag"? */
	if (strncmp(cp, "dag", 3) != 0) {
		/* Nope, doesn't begin with "dag" */
		*is_ours = 0;
		return NULL;
	}
	/* Yes - is "dag" followed by a number from 0 to DAG_MAX_BOARDS-1 */
	cp += 3;
	devnum = strtol(cp, &cpend, 10);
	if (*cpend == ':') {
		/* Followed by a stream number. */
		stream = strtol(++cpend, &cpend, 10);
	}

	if (cpend == cp || *cpend != '\0') {
		/* Not followed by a number. */
		*is_ours = 0;
		return NULL;
	}

	if (devnum < 0 || devnum >= DAG_MAX_BOARDS) {
		/* Followed by a non-valid number. */
		*is_ours = 0;
		return NULL;
	}

	if (stream <0 || stream >= DAG_STREAM_MAX) {
		/* Followed by a non-valid stream number. */
		*is_ours = 0;
		return NULL;
	}

	/* OK, it's probably ours. */
	*is_ours = 1;

	p = pcap_create_common(ebuf, sizeof (struct pcap_dag));
	if (p == NULL)
		return NULL;

	p->activate_op = dag_activate;

	/*
	 * We claim that we support microsecond and nanosecond time
	 * stamps.
	 *
	 * XXX Our native precision is 2^-32s, but libpcap doesn't support
	 * power of two precisions yet. We can convert to either MICRO or NANO.
	 */
	p->tstamp_precision_count = 2;
	p->tstamp_precision_list = malloc(2 * sizeof(u_int));
	if (p->tstamp_precision_list == NULL) {
		pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
		    errno, "malloc");
		pcap_close(p);
		return NULL;
	}
	p->tstamp_precision_list[0] = PCAP_TSTAMP_PRECISION_MICRO;
	p->tstamp_precision_list[1] = PCAP_TSTAMP_PRECISION_NANO;
	return p;
}

static int
dag_stats(pcap_t *p, struct pcap_stat *ps) {
	struct pcap_dag *pd = p->priv;
	uint32_t stream_drop;
	dag_err_t dag_error;

	/*
	 * Packet records received (ps_recv) are counted in dag_read().
	 * Packet records dropped (ps_drop) are read from Stream Drop attribute if present,
	 * otherwise integrate the ERF Header lctr counts (if available) in dag_read().
	 * We are reporting that no records are dropped by the card/driver (ps_ifdrop).
	 */

	if(pd->drop_attr != kNullAttributeUuid) {
		/* Note this counter is cleared at start of capture and will wrap at UINT_MAX.
		 * The application is responsible for polling ps_drop frequently enough
		 * to detect each wrap and integrate total drop with a wider counter */
		if ((dag_error = dag_config_get_uint32_attribute_ex(pd->dag_ref, pd->drop_attr, &stream_drop) == kDagErrNone)) {
			pd->stat.ps_drop = stream_drop;
		} else {
			pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "reading stream drop attribute: %s",
				 dag_config_strerror(dag_error));
			return -1;
		}
	}

	*ps = pd->stat;

	return 0;
}

/*
 * Add all DAG devices.
 */
int
dag_findalldevs(pcap_if_list_t *devlistp, char *errbuf)
{
	char name[12];	/* XXX - pick a size */
	int c;
	char dagname[DAGNAME_BUFSIZE];
	int dagstream;
	int dagfd;
	dag_card_inf_t *inf;
	char *description;
	int stream, rxstreams;

	/* Try all the DAGs 0-DAG_MAX_BOARDS */
	for (c = 0; c < DAG_MAX_BOARDS; c++) {
		pcap_snprintf(name, 12, "dag%d", c);
		if (-1 == dag_parse_name(name, dagname, DAGNAME_BUFSIZE, &dagstream))
		{
			(void) pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "dag: device name %s can't be parsed", name);
			return (-1);
		}
		if ( (dagfd = dag_open(dagname)) >= 0 ) {
			description = NULL;
			if ((inf = dag_pciinfo(dagfd)))
				description = dag_device_name(inf->device_code, 1);
			/*
			 * XXX - is there a way to determine whether
			 * the card is plugged into a network or not?
			 * If so, we should check that and set
			 * PCAP_IF_CONNECTION_STATUS_CONNECTED or
			 * PCAP_IF_CONNECTION_STATUS_DISCONNECTED.
			 *
			 * Also, are there notions of "up" and "running"?
			 */
			if (add_dev(devlistp, name, 0, description, errbuf) == NULL) {
				/*
				 * Failure.
				 */
				return (-1);
			}
			rxstreams = dag_rx_get_stream_count(dagfd);
			for(stream=0;stream<DAG_STREAM_MAX;stream+=2) {
				if (0 == dag_attach_stream(dagfd, stream, 0, 0)) {
					dag_detach_stream(dagfd, stream);

					pcap_snprintf(name,  10, "dag%d:%d", c, stream);
					if (add_dev(devlistp, name, 0, description, errbuf) == NULL) {
						/*
						 * Failure.
						 */
						return (-1);
					}

					rxstreams--;
					if(rxstreams <= 0) {
						break;
					}
				}
			}
			dag_close(dagfd);
		}

	}
	return (0);
}

/*
 * Installs the given bpf filter program in the given pcap structure.  There is
 * no attempt to store the filter in kernel memory as that is not supported
 * with DAG cards.
 */
static int
dag_setfilter(pcap_t *p, struct bpf_program *fp)
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
dag_set_datalink(pcap_t *p, int dlt)
{
	p->linktype = dlt;

	return (0);
}

static int
dag_setnonblock(pcap_t *p, int nonblock)
{
	struct pcap_dag *pd = p->priv;
	dag_size_t mindata;
	struct timeval maxwait;
	struct timeval poll;

	/*
	 * Set non-blocking mode on the FD.
	 * XXX - is that necessary?  If not, don't bother calling it,
	 * and have a "dag_getnonblock()" function that looks at
	 * "pd->dag_flags".
	 */
	if (pcap_setnonblock_fd(p, nonblock) < 0)
		return (-1);

	if (dag_get_stream_poll64(p->fd, pd->dag_stream,
				&mindata, &maxwait, &poll) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "dag_get_stream_poll");
		return -1;
	}

	/* Amount of data to collect in Bytes before calling callbacks.
	 * Important for efficiency, but can introduce latency
	 * at low packet rates if to_ms not set!
	 */
	if(nonblock)
		mindata = 0;
	else
		mindata = 65536;

	if (dag_set_stream_poll64(p->fd, pd->dag_stream,
				mindata, &maxwait, &poll) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "dag_set_stream_poll");
		return -1;
	}

	if (nonblock) {
		pd->dag_flags |= DAGF_NONBLOCK;
	} else {
		pd->dag_flags &= ~DAGF_NONBLOCK;
	}
	return (0);
}

static int
dag_get_datalink(pcap_t *p)
{
	struct pcap_dag *pd = p->priv;
	int index=0, dlt_index=0;
	uint8_t types[255];

	memset(types, 0, 255);

	if (p->dlt_list == NULL && (p->dlt_list = malloc(255*sizeof(*(p->dlt_list)))) == NULL) {
		pcap_fmt_errmsg_for_errno(p->errbuf, sizeof(p->errbuf),
		    errno, "malloc");
		return (-1);
	}

	p->linktype = 0;

#ifdef HAVE_DAG_GET_STREAM_ERF_TYPES
	/* Get list of possible ERF types for this card */
	if (dag_get_stream_erf_types(p->fd, pd->dag_stream, types, 255) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, sizeof(p->errbuf),
		    errno, "dag_get_stream_erf_types");
		return (-1);
	}

	while (types[index]) {

#elif defined HAVE_DAG_GET_ERF_TYPES
	/* Get list of possible ERF types for this card */
	if (dag_get_erf_types(p->fd, types, 255) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, sizeof(p->errbuf),
		    errno, "dag_get_erf_types");
		return (-1);
	}

	while (types[index]) {
#else
	/* Check the type through a dagapi call. */
	types[index] = dag_linktype(p->fd);

	{
#endif
		switch((types[index] & 0x7f)) {

		case ERF_TYPE_HDLC_POS:
		case ERF_TYPE_COLOR_HDLC_POS:
		case ERF_TYPE_DSM_COLOR_HDLC_POS:
		case ERF_TYPE_COLOR_HASH_POS:

			if (p->dlt_list != NULL) {
				p->dlt_list[dlt_index++] = DLT_CHDLC;
				p->dlt_list[dlt_index++] = DLT_PPP_SERIAL;
				p->dlt_list[dlt_index++] = DLT_FRELAY;
			}
			if(!p->linktype)
				p->linktype = DLT_CHDLC;
			break;

		case ERF_TYPE_ETH:
		case ERF_TYPE_COLOR_ETH:
		case ERF_TYPE_DSM_COLOR_ETH:
		case ERF_TYPE_COLOR_HASH_ETH:
			/*
			 * This is (presumably) a real Ethernet capture; give it a
			 * link-layer-type list with DLT_EN10MB and DLT_DOCSIS, so
			 * that an application can let you choose it, in case you're
			 * capturing DOCSIS traffic that a Cisco Cable Modem
			 * Termination System is putting out onto an Ethernet (it
			 * doesn't put an Ethernet header onto the wire, it puts raw
			 * DOCSIS frames out on the wire inside the low-level
			 * Ethernet framing).
			 */
			if (p->dlt_list != NULL) {
				p->dlt_list[dlt_index++] = DLT_EN10MB;
				p->dlt_list[dlt_index++] = DLT_DOCSIS;
			}
			if(!p->linktype)
				p->linktype = DLT_EN10MB;
			break;

		case ERF_TYPE_ATM:
		case ERF_TYPE_AAL5:
		case ERF_TYPE_MC_ATM:
		case ERF_TYPE_MC_AAL5:
			if (p->dlt_list != NULL) {
				p->dlt_list[dlt_index++] = DLT_ATM_RFC1483;
				p->dlt_list[dlt_index++] = DLT_SUNATM;
			}
			if(!p->linktype)
				p->linktype = DLT_ATM_RFC1483;
			break;

		case ERF_TYPE_COLOR_MC_HDLC_POS:
		case ERF_TYPE_MC_HDLC:
			if (p->dlt_list != NULL) {
				p->dlt_list[dlt_index++] = DLT_CHDLC;
				p->dlt_list[dlt_index++] = DLT_PPP_SERIAL;
				p->dlt_list[dlt_index++] = DLT_FRELAY;
				p->dlt_list[dlt_index++] = DLT_MTP2;
				p->dlt_list[dlt_index++] = DLT_MTP2_WITH_PHDR;
				p->dlt_list[dlt_index++] = DLT_LAPD;
			}
			if(!p->linktype)
				p->linktype = DLT_CHDLC;
			break;

		case ERF_TYPE_IPV4:
			if (p->dlt_list != NULL) {
				p->dlt_list[dlt_index++] = DLT_RAW;
				p->dlt_list[dlt_index++] = DLT_IPV4;
			}
			if(!p->linktype)
				p->linktype = DLT_RAW;
			break;

		case ERF_TYPE_IPV6:
			if (p->dlt_list != NULL) {
				p->dlt_list[dlt_index++] = DLT_RAW;
				p->dlt_list[dlt_index++] = DLT_IPV6;
			}
			if(!p->linktype)
				p->linktype = DLT_RAW;
			break;

		case ERF_TYPE_LEGACY:
		case ERF_TYPE_MC_RAW:
		case ERF_TYPE_MC_RAW_CHANNEL:
		case ERF_TYPE_IP_COUNTER:
		case ERF_TYPE_TCP_FLOW_COUNTER:
		case ERF_TYPE_INFINIBAND:
		case ERF_TYPE_RAW_LINK:
		case ERF_TYPE_INFINIBAND_LINK:
		case ERF_TYPE_META:
		default:
			/* Libpcap cannot deal with these types yet */
			/* Add no 'native' DLTs, but still covered by DLT_ERF */
			break;

		} /* switch */
		index++;
	}

	p->dlt_list[dlt_index++] = DLT_ERF;

	p->dlt_count = dlt_index;

	if(!p->linktype)
		p->linktype = DLT_ERF;

	return p->linktype;
}

#ifdef DAG_ONLY
/*
 * This libpcap build supports only DAG cards, not regular network
 * interfaces.
 */

/*
 * There are no regular interfaces, just DAG interfaces.
 */
int
pcap_platform_finddevs(pcap_if_list_t *devlistp _U_, char *errbuf)
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
	    "This version of libpcap only supports DAG cards");
	return NULL;
}

/*
 * Libpcap version string.
 */
const char *
pcap_lib_version(void)
{
	return (PCAP_VERSION_STRING " (DAG-only)");
}
#endif
