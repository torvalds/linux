/*
 * dvb_net.c
 *
 * Copyright (C) 2001 Convergence integrated media GmbH
 *                    Ralph Metzler <ralph@convergence.de>
 * Copyright (C) 2002 Ralph Metzler <rjkm@metzlerbros.de>
 *
 * ULE Decapsulation code:
 * Copyright (C) 2003, 2004 gcs - Global Communication & Services GmbH.
 *                      and Department of Scientific Computing
 *                          Paris Lodron University of Salzburg.
 *                          Hilmar Linder <hlinder@cosy.sbg.ac.at>
 *                      and Wolfram Stering <wstering@cosy.sbg.ac.at>
 *
 * ULE Decaps according to RFC 4326.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

/*
 * ULE ChangeLog:
 * Feb 2004: hl/ws v1: Implementing draft-fair-ipdvb-ule-01.txt
 *
 * Dec 2004: hl/ws v2: Implementing draft-ietf-ipdvb-ule-03.txt:
 *                       ULE Extension header handling.
 *                     Bugreports by Moritz Vieth and Hanno Tersteegen,
 *                       Fraunhofer Institute for Open Communication Systems
 *                       Competence Center for Advanced Satellite Communications.
 *                     Bugfixes and robustness improvements.
 *                     Filtering on dest MAC addresses, if present (D-Bit = 0)
 *                     ULE_DEBUG compile-time option.
 * Apr 2006: cp v3:    Bugfixes and compliency with RFC 4326 (ULE) by
 *                       Christian Praehauser <cpraehaus@cosy.sbg.ac.at>,
 *                       Paris Lodron University of Salzburg.
 */

/*
 * FIXME / TODO (dvb_net.c):
 *
 * Unloading does not work for 2.6.9 kernels: a refcount doesn't go to zero.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/dvb/net.h>
#include <linux/uio.h>
#include <asm/uaccess.h>
#include <linux/crc32.h>
#include <linux/mutex.h>
#include <linux/sched.h>

#include "dvb_demux.h"
#include "dvb_net.h"

static int dvb_net_debug;
module_param(dvb_net_debug, int, 0444);
MODULE_PARM_DESC(dvb_net_debug, "enable debug messages");

#define dprintk(x...) do { if (dvb_net_debug) printk(x); } while (0)


static inline __u32 iov_crc32( __u32 c, struct kvec *iov, unsigned int cnt )
{
	unsigned int j;
	for (j = 0; j < cnt; j++)
		c = crc32_be( c, iov[j].iov_base, iov[j].iov_len );
	return c;
}


#define DVB_NET_MULTICAST_MAX 10

#undef ULE_DEBUG

#ifdef ULE_DEBUG

#define MAC_ADDR_PRINTFMT "%.2x:%.2x:%.2x:%.2x:%.2x:%.2x"
#define MAX_ADDR_PRINTFMT_ARGS(macap) (macap)[0],(macap)[1],(macap)[2],(macap)[3],(macap)[4],(macap)[5]

#define isprint(c)	((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))

static void hexdump( const unsigned char *buf, unsigned short len )
{
	char str[80], octet[10];
	int ofs, i, l;

	for (ofs = 0; ofs < len; ofs += 16) {
		sprintf( str, "%03d: ", ofs );

		for (i = 0; i < 16; i++) {
			if ((i + ofs) < len)
				sprintf( octet, "%02x ", buf[ofs + i] );
			else
				strcpy( octet, "   " );

			strcat( str, octet );
		}
		strcat( str, "  " );
		l = strlen( str );

		for (i = 0; (i < 16) && ((i + ofs) < len); i++)
			str[l++] = isprint( buf[ofs + i] ) ? buf[ofs + i] : '.';

		str[l] = '\0';
		printk( KERN_WARNING "%s\n", str );
	}
}

#endif

struct dvb_net_priv {
	int in_use;
	u16 pid;
	struct net_device *net;
	struct dvb_net *host;
	struct dmx_demux *demux;
	struct dmx_section_feed *secfeed;
	struct dmx_section_filter *secfilter;
	struct dmx_ts_feed *tsfeed;
	int multi_num;
	struct dmx_section_filter *multi_secfilter[DVB_NET_MULTICAST_MAX];
	unsigned char multi_macs[DVB_NET_MULTICAST_MAX][6];
	int rx_mode;
#define RX_MODE_UNI 0
#define RX_MODE_MULTI 1
#define RX_MODE_ALL_MULTI 2
#define RX_MODE_PROMISC 3
	struct work_struct set_multicast_list_wq;
	struct work_struct restart_net_feed_wq;
	unsigned char feedtype;			/* Either FEED_TYPE_ or FEED_TYPE_ULE */
	int need_pusi;				/* Set to 1, if synchronization on PUSI required. */
	unsigned char tscc;			/* TS continuity counter after sync on PUSI. */
	struct sk_buff *ule_skb;		/* ULE SNDU decodes into this buffer. */
	unsigned char *ule_next_hdr;		/* Pointer into skb to next ULE extension header. */
	unsigned short ule_sndu_len;		/* ULE SNDU length in bytes, w/o D-Bit. */
	unsigned short ule_sndu_type;		/* ULE SNDU type field, complete. */
	unsigned char ule_sndu_type_1;		/* ULE SNDU type field, if split across 2 TS cells. */
	unsigned char ule_dbit;			/* Whether the DestMAC address present
						 * or not (bit is set). */
	unsigned char ule_bridged;		/* Whether the ULE_BRIDGED extension header was found. */
	int ule_sndu_remain;			/* Nr. of bytes still required for current ULE SNDU. */
	unsigned long ts_count;			/* Current ts cell counter. */
	struct mutex mutex;
};


/**
 *	Determine the packet's protocol ID. The rule here is that we
 *	assume 802.3 if the type field is short enough to be a length.
 *	This is normal practice and works for any 'now in use' protocol.
 *
 *  stolen from eth.c out of the linux kernel, hacked for dvb-device
 *  by Michael Holzt <kju@debian.org>
 */
static __be16 dvb_net_eth_type_trans(struct sk_buff *skb,
				      struct net_device *dev)
{
	struct ethhdr *eth;
	unsigned char *rawp;

	skb_reset_mac_header(skb);
	skb_pull(skb,dev->hard_header_len);
	eth = eth_hdr(skb);

	if (*eth->h_dest & 1) {
		if(memcmp(eth->h_dest,dev->broadcast, ETH_ALEN)==0)
			skb->pkt_type=PACKET_BROADCAST;
		else
			skb->pkt_type=PACKET_MULTICAST;
	}

	if (ntohs(eth->h_proto) >= 1536)
		return eth->h_proto;

	rawp = skb->data;

	/**
	 *	This is a magic hack to spot IPX packets. Older Novell breaks
	 *	the protocol design and runs IPX over 802.3 without an 802.2 LLC
	 *	layer. We look for FFFF which isn't a used 802.2 SSAP/DSAP. This
	 *	won't work for fault tolerant netware but does for the rest.
	 */
	if (*(unsigned short *)rawp == 0xFFFF)
		return htons(ETH_P_802_3);

	/**
	 *	Real 802.2 LLC
	 */
	return htons(ETH_P_802_2);
}

#define TS_SZ	188
#define TS_SYNC	0x47
#define TS_TEI	0x80
#define TS_SC	0xC0
#define TS_PUSI	0x40
#define TS_AF_A	0x20
#define TS_AF_D	0x10

/* ULE Extension Header handlers. */

#define ULE_TEST	0
#define ULE_BRIDGED	1

#define ULE_OPTEXTHDR_PADDING 0

static int ule_test_sndu( struct dvb_net_priv *p )
{
	return -1;
}

static int ule_bridged_sndu( struct dvb_net_priv *p )
{
	struct ethhdr *hdr = (struct ethhdr*) p->ule_next_hdr;
	if(ntohs(hdr->h_proto) < 1536) {
		int framelen = p->ule_sndu_len - ((p->ule_next_hdr+sizeof(struct ethhdr)) - p->ule_skb->data);
		/* A frame Type < 1536 for a bridged frame, introduces a LLC Length field. */
		if(framelen != ntohs(hdr->h_proto)) {
			return -1;
		}
	}
	/* Note:
	 * From RFC4326:
	 *  "A bridged SNDU is a Mandatory Extension Header of Type 1.
	 *   It must be the final (or only) extension header specified in the header chain of a SNDU."
	 * The 'ule_bridged' flag will cause the extension header processing loop to terminate.
	 */
	p->ule_bridged = 1;
	return 0;
}

static int ule_exthdr_padding(struct dvb_net_priv *p)
{
	return 0;
}

/** Handle ULE extension headers.
 *  Function is called after a successful CRC32 verification of an ULE SNDU to complete its decoding.
 *  Returns: >= 0: nr. of bytes consumed by next extension header
 *	     -1:   Mandatory extension header that is not recognized or TEST SNDU; discard.
 */
static int handle_one_ule_extension( struct dvb_net_priv *p )
{
	/* Table of mandatory extension header handlers.  The header type is the index. */
	static int (*ule_mandatory_ext_handlers[255])( struct dvb_net_priv *p ) =
		{ [0] = ule_test_sndu, [1] = ule_bridged_sndu, [2] = NULL,  };

	/* Table of optional extension header handlers.  The header type is the index. */
	static int (*ule_optional_ext_handlers[255])( struct dvb_net_priv *p ) =
		{ [0] = ule_exthdr_padding, [1] = NULL, };

	int ext_len = 0;
	unsigned char hlen = (p->ule_sndu_type & 0x0700) >> 8;
	unsigned char htype = p->ule_sndu_type & 0x00FF;

	/* Discriminate mandatory and optional extension headers. */
	if (hlen == 0) {
		/* Mandatory extension header */
		if (ule_mandatory_ext_handlers[htype]) {
			ext_len = ule_mandatory_ext_handlers[htype]( p );
			if(ext_len >= 0) {
				p->ule_next_hdr += ext_len;
				if (!p->ule_bridged) {
					p->ule_sndu_type = ntohs(*(__be16 *)p->ule_next_hdr);
					p->ule_next_hdr += 2;
				} else {
					p->ule_sndu_type = ntohs(*(__be16 *)(p->ule_next_hdr + ((p->ule_dbit ? 2 : 3) * ETH_ALEN)));
					/* This assures the extension handling loop will terminate. */
				}
			}
			// else: extension handler failed or SNDU should be discarded
		} else
			ext_len = -1;	/* SNDU has to be discarded. */
	} else {
		/* Optional extension header.  Calculate the length. */
		ext_len = hlen << 1;
		/* Process the optional extension header according to its type. */
		if (ule_optional_ext_handlers[htype])
			(void)ule_optional_ext_handlers[htype]( p );
		p->ule_next_hdr += ext_len;
		p->ule_sndu_type = ntohs( *(__be16 *)(p->ule_next_hdr-2) );
		/*
		 * note: the length of the next header type is included in the
		 * length of THIS optional extension header
		 */
	}

	return ext_len;
}

static int handle_ule_extensions( struct dvb_net_priv *p )
{
	int total_ext_len = 0, l;

	p->ule_next_hdr = p->ule_skb->data;
	do {
		l = handle_one_ule_extension( p );
		if (l < 0)
			return l;	/* Stop extension header processing and discard SNDU. */
		total_ext_len += l;
#ifdef ULE_DEBUG
		dprintk("handle_ule_extensions: ule_next_hdr=%p, ule_sndu_type=%i, "
			"l=%i, total_ext_len=%i\n", p->ule_next_hdr,
			(int) p->ule_sndu_type, l, total_ext_len);
#endif

	} while (p->ule_sndu_type < 1536);

	return total_ext_len;
}


/** Prepare for a new ULE SNDU: reset the decoder state. */
static inline void reset_ule( struct dvb_net_priv *p )
{
	p->ule_skb = NULL;
	p->ule_next_hdr = NULL;
	p->ule_sndu_len = 0;
	p->ule_sndu_type = 0;
	p->ule_sndu_type_1 = 0;
	p->ule_sndu_remain = 0;
	p->ule_dbit = 0xFF;
	p->ule_bridged = 0;
}

/**
 * Decode ULE SNDUs according to draft-ietf-ipdvb-ule-03.txt from a sequence of
 * TS cells of a single PID.
 */
static void dvb_net_ule( struct net_device *dev, const u8 *buf, size_t buf_len )
{
	struct dvb_net_priv *priv = netdev_priv(dev);
	unsigned long skipped = 0L;
	const u8 *ts, *ts_end, *from_where = NULL;
	u8 ts_remain = 0, how_much = 0, new_ts = 1;
	struct ethhdr *ethh = NULL;
	bool error = false;

#ifdef ULE_DEBUG
	/* The code inside ULE_DEBUG keeps a history of the last 100 TS cells processed. */
	static unsigned char ule_hist[100*TS_SZ];
	static unsigned char *ule_where = ule_hist, ule_dump;
#endif

	/* For all TS cells in current buffer.
	 * Appearently, we are called for every single TS cell.
	 */
	for (ts = buf, ts_end = buf + buf_len; ts < ts_end; /* no default incr. */ ) {

		if (new_ts) {
			/* We are about to process a new TS cell. */

#ifdef ULE_DEBUG
			if (ule_where >= &ule_hist[100*TS_SZ]) ule_where = ule_hist;
			memcpy( ule_where, ts, TS_SZ );
			if (ule_dump) {
				hexdump( ule_where, TS_SZ );
				ule_dump = 0;
			}
			ule_where += TS_SZ;
#endif

			/* Check TS error conditions: sync_byte, transport_error_indicator, scrambling_control . */
			if ((ts[0] != TS_SYNC) || (ts[1] & TS_TEI) || ((ts[3] & TS_SC) != 0)) {
				printk(KERN_WARNING "%lu: Invalid TS cell: SYNC %#x, TEI %u, SC %#x.\n",
				       priv->ts_count, ts[0], ts[1] & TS_TEI >> 7, ts[3] & 0xC0 >> 6);

				/* Drop partly decoded SNDU, reset state, resync on PUSI. */
				if (priv->ule_skb) {
					dev_kfree_skb( priv->ule_skb );
					/* Prepare for next SNDU. */
					dev->stats.rx_errors++;
					dev->stats.rx_frame_errors++;
				}
				reset_ule(priv);
				priv->need_pusi = 1;

				/* Continue with next TS cell. */
				ts += TS_SZ;
				priv->ts_count++;
				continue;
			}

			ts_remain = 184;
			from_where = ts + 4;
		}
		/* Synchronize on PUSI, if required. */
		if (priv->need_pusi) {
			if (ts[1] & TS_PUSI) {
				/* Find beginning of first ULE SNDU in current TS cell. */
				/* Synchronize continuity counter. */
				priv->tscc = ts[3] & 0x0F;
				/* There is a pointer field here. */
				if (ts[4] > ts_remain) {
					printk(KERN_ERR "%lu: Invalid ULE packet "
					       "(pointer field %d)\n", priv->ts_count, ts[4]);
					ts += TS_SZ;
					priv->ts_count++;
					continue;
				}
				/* Skip to destination of pointer field. */
				from_where = &ts[5] + ts[4];
				ts_remain -= 1 + ts[4];
				skipped = 0;
			} else {
				skipped++;
				ts += TS_SZ;
				priv->ts_count++;
				continue;
			}
		}

		if (new_ts) {
			/* Check continuity counter. */
			if ((ts[3] & 0x0F) == priv->tscc)
				priv->tscc = (priv->tscc + 1) & 0x0F;
			else {
				/* TS discontinuity handling: */
				printk(KERN_WARNING "%lu: TS discontinuity: got %#x, "
				       "expected %#x.\n", priv->ts_count, ts[3] & 0x0F, priv->tscc);
				/* Drop partly decoded SNDU, reset state, resync on PUSI. */
				if (priv->ule_skb) {
					dev_kfree_skb( priv->ule_skb );
					/* Prepare for next SNDU. */
					// reset_ule(priv);  moved to below.
					dev->stats.rx_errors++;
					dev->stats.rx_frame_errors++;
				}
				reset_ule(priv);
				/* skip to next PUSI. */
				priv->need_pusi = 1;
				continue;
			}
			/* If we still have an incomplete payload, but PUSI is
			 * set; some TS cells are missing.
			 * This is only possible here, if we missed exactly 16 TS
			 * cells (continuity counter wrap). */
			if (ts[1] & TS_PUSI) {
				if (! priv->need_pusi) {
					if (!(*from_where < (ts_remain-1)) || *from_where != priv->ule_sndu_remain) {
						/* Pointer field is invalid.  Drop this TS cell and any started ULE SNDU. */
						printk(KERN_WARNING "%lu: Invalid pointer "
						       "field: %u.\n", priv->ts_count, *from_where);

						/* Drop partly decoded SNDU, reset state, resync on PUSI. */
						if (priv->ule_skb) {
							error = true;
							dev_kfree_skb(priv->ule_skb);
						}

						if (error || priv->ule_sndu_remain) {
							dev->stats.rx_errors++;
							dev->stats.rx_frame_errors++;
							error = false;
						}

						reset_ule(priv);
						priv->need_pusi = 1;
						continue;
					}
					/* Skip pointer field (we're processing a
					 * packed payload). */
					from_where += 1;
					ts_remain -= 1;
				} else
					priv->need_pusi = 0;

				if (priv->ule_sndu_remain > 183) {
					/* Current SNDU lacks more data than there could be available in the
					 * current TS cell. */
					dev->stats.rx_errors++;
					dev->stats.rx_length_errors++;
					printk(KERN_WARNING "%lu: Expected %d more SNDU bytes, but "
					       "got PUSI (pf %d, ts_remain %d).  Flushing incomplete payload.\n",
					       priv->ts_count, priv->ule_sndu_remain, ts[4], ts_remain);
					dev_kfree_skb(priv->ule_skb);
					/* Prepare for next SNDU. */
					reset_ule(priv);
					/* Resync: go to where pointer field points to: start of next ULE SNDU. */
					from_where += ts[4];
					ts_remain -= ts[4];
				}
			}
		}

		/* Check if new payload needs to be started. */
		if (priv->ule_skb == NULL) {
			/* Start a new payload with skb.
			 * Find ULE header.  It is only guaranteed that the
			 * length field (2 bytes) is contained in the current
			 * TS.
			 * Check ts_remain has to be >= 2 here. */
			if (ts_remain < 2) {
				printk(KERN_WARNING "Invalid payload packing: only %d "
				       "bytes left in TS.  Resyncing.\n", ts_remain);
				priv->ule_sndu_len = 0;
				priv->need_pusi = 1;
				ts += TS_SZ;
				continue;
			}

			if (! priv->ule_sndu_len) {
				/* Got at least two bytes, thus extrace the SNDU length. */
				priv->ule_sndu_len = from_where[0] << 8 | from_where[1];
				if (priv->ule_sndu_len & 0x8000) {
					/* D-Bit is set: no dest mac present. */
					priv->ule_sndu_len &= 0x7FFF;
					priv->ule_dbit = 1;
				} else
					priv->ule_dbit = 0;

				if (priv->ule_sndu_len < 5) {
					printk(KERN_WARNING "%lu: Invalid ULE SNDU length %u. "
					       "Resyncing.\n", priv->ts_count, priv->ule_sndu_len);
					dev->stats.rx_errors++;
					dev->stats.rx_length_errors++;
					priv->ule_sndu_len = 0;
					priv->need_pusi = 1;
					new_ts = 1;
					ts += TS_SZ;
					priv->ts_count++;
					continue;
				}
				ts_remain -= 2;	/* consume the 2 bytes SNDU length. */
				from_where += 2;
			}

			priv->ule_sndu_remain = priv->ule_sndu_len + 2;
			/*
			 * State of current TS:
			 *   ts_remain (remaining bytes in the current TS cell)
			 *   0	ule_type is not available now, we need the next TS cell
			 *   1	the first byte of the ule_type is present
			 * >=2	full ULE header present, maybe some payload data as well.
			 */
			switch (ts_remain) {
				case 1:
					priv->ule_sndu_remain--;
					priv->ule_sndu_type = from_where[0] << 8;
					priv->ule_sndu_type_1 = 1; /* first byte of ule_type is set. */
					ts_remain -= 1; from_where += 1;
					/* Continue w/ next TS. */
				case 0:
					new_ts = 1;
					ts += TS_SZ;
					priv->ts_count++;
					continue;

				default: /* complete ULE header is present in current TS. */
					/* Extract ULE type field. */
					if (priv->ule_sndu_type_1) {
						priv->ule_sndu_type_1 = 0;
						priv->ule_sndu_type |= from_where[0];
						from_where += 1; /* points to payload start. */
						ts_remain -= 1;
					} else {
						/* Complete type is present in new TS. */
						priv->ule_sndu_type = from_where[0] << 8 | from_where[1];
						from_where += 2; /* points to payload start. */
						ts_remain -= 2;
					}
					break;
			}

			/* Allocate the skb (decoder target buffer) with the correct size, as follows:
			 * prepare for the largest case: bridged SNDU with MAC address (dbit = 0). */
			priv->ule_skb = dev_alloc_skb( priv->ule_sndu_len + ETH_HLEN + ETH_ALEN );
			if (priv->ule_skb == NULL) {
				printk(KERN_NOTICE "%s: Memory squeeze, dropping packet.\n",
				       dev->name);
				dev->stats.rx_dropped++;
				return;
			}

			/* This includes the CRC32 _and_ dest mac, if !dbit. */
			priv->ule_sndu_remain = priv->ule_sndu_len;
			priv->ule_skb->dev = dev;
			/* Leave space for Ethernet or bridged SNDU header (eth hdr plus one MAC addr). */
			skb_reserve( priv->ule_skb, ETH_HLEN + ETH_ALEN );
		}

		/* Copy data into our current skb. */
		how_much = min(priv->ule_sndu_remain, (int)ts_remain);
		memcpy(skb_put(priv->ule_skb, how_much), from_where, how_much);
		priv->ule_sndu_remain -= how_much;
		ts_remain -= how_much;
		from_where += how_much;

		/* Check for complete payload. */
		if (priv->ule_sndu_remain <= 0) {
			/* Check CRC32, we've got it in our skb already. */
			__be16 ulen = htons(priv->ule_sndu_len);
			__be16 utype = htons(priv->ule_sndu_type);
			const u8 *tail;
			struct kvec iov[3] = {
				{ &ulen, sizeof ulen },
				{ &utype, sizeof utype },
				{ priv->ule_skb->data, priv->ule_skb->len - 4 }
			};
			u32 ule_crc = ~0L, expected_crc;
			if (priv->ule_dbit) {
				/* Set D-bit for CRC32 verification,
				 * if it was set originally. */
				ulen |= htons(0x8000);
			}

			ule_crc = iov_crc32(ule_crc, iov, 3);
			tail = skb_tail_pointer(priv->ule_skb);
			expected_crc = *(tail - 4) << 24 |
				       *(tail - 3) << 16 |
				       *(tail - 2) << 8 |
				       *(tail - 1);
			if (ule_crc != expected_crc) {
				printk(KERN_WARNING "%lu: CRC32 check FAILED: %08x / %08x, SNDU len %d type %#x, ts_remain %d, next 2: %x.\n",
				       priv->ts_count, ule_crc, expected_crc, priv->ule_sndu_len, priv->ule_sndu_type, ts_remain, ts_remain > 2 ? *(unsigned short *)from_where : 0);

#ifdef ULE_DEBUG
				hexdump( iov[0].iov_base, iov[0].iov_len );
				hexdump( iov[1].iov_base, iov[1].iov_len );
				hexdump( iov[2].iov_base, iov[2].iov_len );

				if (ule_where == ule_hist) {
					hexdump( &ule_hist[98*TS_SZ], TS_SZ );
					hexdump( &ule_hist[99*TS_SZ], TS_SZ );
				} else if (ule_where == &ule_hist[TS_SZ]) {
					hexdump( &ule_hist[99*TS_SZ], TS_SZ );
					hexdump( ule_hist, TS_SZ );
				} else {
					hexdump( ule_where - TS_SZ - TS_SZ, TS_SZ );
					hexdump( ule_where - TS_SZ, TS_SZ );
				}
				ule_dump = 1;
#endif

				dev->stats.rx_errors++;
				dev->stats.rx_crc_errors++;
				dev_kfree_skb(priv->ule_skb);
			} else {
				/* CRC32 verified OK. */
				u8 dest_addr[ETH_ALEN];
				static const u8 bc_addr[ETH_ALEN] =
					{ [ 0 ... ETH_ALEN-1] = 0xff };

				/* CRC32 was OK. Remove it from skb. */
				priv->ule_skb->tail -= 4;
				priv->ule_skb->len -= 4;

				if (!priv->ule_dbit) {
					/*
					 * The destination MAC address is the
					 * next data in the skb.  It comes
					 * before any extension headers.
					 *
					 * Check if the payload of this SNDU
					 * should be passed up the stack.
					 */
					register int drop = 0;
					if (priv->rx_mode != RX_MODE_PROMISC) {
						if (priv->ule_skb->data[0] & 0x01) {
							/* multicast or broadcast */
							if (memcmp(priv->ule_skb->data, bc_addr, ETH_ALEN)) {
								/* multicast */
								if (priv->rx_mode == RX_MODE_MULTI) {
									int i;
									for(i = 0; i < priv->multi_num && memcmp(priv->ule_skb->data, priv->multi_macs[i], ETH_ALEN); i++)
										;
									if (i == priv->multi_num)
										drop = 1;
								} else if (priv->rx_mode != RX_MODE_ALL_MULTI)
									drop = 1; /* no broadcast; */
								/* else: all multicast mode: accept all multicast packets */
							}
							/* else: broadcast */
						}
						else if (memcmp(priv->ule_skb->data, dev->dev_addr, ETH_ALEN))
							drop = 1;
						/* else: destination address matches the MAC address of our receiver device */
					}
					/* else: promiscuous mode; pass everything up the stack */

					if (drop) {
#ifdef ULE_DEBUG
						dprintk("Dropping SNDU: MAC destination address does not match: dest addr: "MAC_ADDR_PRINTFMT", dev addr: "MAC_ADDR_PRINTFMT"\n",
							MAX_ADDR_PRINTFMT_ARGS(priv->ule_skb->data), MAX_ADDR_PRINTFMT_ARGS(dev->dev_addr));
#endif
						dev_kfree_skb(priv->ule_skb);
						goto sndu_done;
					}
					else
					{
						skb_copy_from_linear_data(priv->ule_skb,
							      dest_addr,
							      ETH_ALEN);
						skb_pull(priv->ule_skb, ETH_ALEN);
					}
				}

				/* Handle ULE Extension Headers. */
				if (priv->ule_sndu_type < 1536) {
					/* There is an extension header.  Handle it accordingly. */
					int l = handle_ule_extensions(priv);
					if (l < 0) {
						/* Mandatory extension header unknown or TEST SNDU.  Drop it. */
						// printk( KERN_WARNING "Dropping SNDU, extension headers.\n" );
						dev_kfree_skb(priv->ule_skb);
						goto sndu_done;
					}
					skb_pull(priv->ule_skb, l);
				}

				/*
				 * Construct/assure correct ethernet header.
				 * Note: in bridged mode (priv->ule_bridged !=
				 * 0) we already have the (original) ethernet
				 * header at the start of the payload (after
				 * optional dest. address and any extension
				 * headers).
				 */

				if (!priv->ule_bridged) {
					skb_push(priv->ule_skb, ETH_HLEN);
					ethh = (struct ethhdr *)priv->ule_skb->data;
					if (!priv->ule_dbit) {
						 /* dest_addr buffer is only valid if priv->ule_dbit == 0 */
						memcpy(ethh->h_dest, dest_addr, ETH_ALEN);
						memset(ethh->h_source, 0, ETH_ALEN);
					}
					else /* zeroize source and dest */
						memset( ethh, 0, ETH_ALEN*2 );

					ethh->h_proto = htons(priv->ule_sndu_type);
				}
				/* else:  skb is in correct state; nothing to do. */
				priv->ule_bridged = 0;

				/* Stuff into kernel's protocol stack. */
				priv->ule_skb->protocol = dvb_net_eth_type_trans(priv->ule_skb, dev);
				/* If D-bit is set (i.e. destination MAC address not present),
				 * receive the packet anyhow. */
				/* if (priv->ule_dbit && skb->pkt_type == PACKET_OTHERHOST)
					priv->ule_skb->pkt_type = PACKET_HOST; */
				dev->stats.rx_packets++;
				dev->stats.rx_bytes += priv->ule_skb->len;
				netif_rx(priv->ule_skb);
			}
			sndu_done:
			/* Prepare for next SNDU. */
			reset_ule(priv);
		}

		/* More data in current TS (look at the bytes following the CRC32)? */
		if (ts_remain >= 2 && *((unsigned short *)from_where) != 0xFFFF) {
			/* Next ULE SNDU starts right there. */
			new_ts = 0;
			priv->ule_skb = NULL;
			priv->ule_sndu_type_1 = 0;
			priv->ule_sndu_len = 0;
			// printk(KERN_WARNING "More data in current TS: [%#x %#x %#x %#x]\n",
			//	*(from_where + 0), *(from_where + 1),
			//	*(from_where + 2), *(from_where + 3));
			// printk(KERN_WARNING "ts @ %p, stopped @ %p:\n", ts, from_where + 0);
			// hexdump(ts, 188);
		} else {
			new_ts = 1;
			ts += TS_SZ;
			priv->ts_count++;
			if (priv->ule_skb == NULL) {
				priv->need_pusi = 1;
				priv->ule_sndu_type_1 = 0;
				priv->ule_sndu_len = 0;
			}
		}
	}	/* for all available TS cells */
}

static int dvb_net_ts_callback(const u8 *buffer1, size_t buffer1_len,
			       const u8 *buffer2, size_t buffer2_len,
			       struct dmx_ts_feed *feed, enum dmx_success success)
{
	struct net_device *dev = feed->priv;

	if (buffer2)
		printk(KERN_WARNING "buffer2 not NULL: %p.\n", buffer2);
	if (buffer1_len > 32768)
		printk(KERN_WARNING "length > 32k: %zu.\n", buffer1_len);
	/* printk("TS callback: %u bytes, %u TS cells @ %p.\n",
		  buffer1_len, buffer1_len / TS_SZ, buffer1); */
	dvb_net_ule(dev, buffer1, buffer1_len);
	return 0;
}


static void dvb_net_sec(struct net_device *dev,
			const u8 *pkt, int pkt_len)
{
	u8 *eth;
	struct sk_buff *skb;
	struct net_device_stats *stats = &dev->stats;
	int snap = 0;

	/* note: pkt_len includes a 32bit checksum */
	if (pkt_len < 16) {
		printk("%s: IP/MPE packet length = %d too small.\n",
			dev->name, pkt_len);
		stats->rx_errors++;
		stats->rx_length_errors++;
		return;
	}
/* it seems some ISPs manage to screw up here, so we have to
 * relax the error checks... */
#if 0
	if ((pkt[5] & 0xfd) != 0xc1) {
		/* drop scrambled or broken packets */
#else
	if ((pkt[5] & 0x3c) != 0x00) {
		/* drop scrambled */
#endif
		stats->rx_errors++;
		stats->rx_crc_errors++;
		return;
	}
	if (pkt[5] & 0x02) {
		/* handle LLC/SNAP, see rfc-1042 */
		if (pkt_len < 24 || memcmp(&pkt[12], "\xaa\xaa\x03\0\0\0", 6)) {
			stats->rx_dropped++;
			return;
		}
		snap = 8;
	}
	if (pkt[7]) {
		/* FIXME: assemble datagram from multiple sections */
		stats->rx_errors++;
		stats->rx_frame_errors++;
		return;
	}

	/* we have 14 byte ethernet header (ip header follows);
	 * 12 byte MPE header; 4 byte checksum; + 2 byte alignment, 8 byte LLC/SNAP
	 */
	if (!(skb = dev_alloc_skb(pkt_len - 4 - 12 + 14 + 2 - snap))) {
		//printk(KERN_NOTICE "%s: Memory squeeze, dropping packet.\n", dev->name);
		stats->rx_dropped++;
		return;
	}
	skb_reserve(skb, 2);    /* longword align L3 header */
	skb->dev = dev;

	/* copy L3 payload */
	eth = (u8 *) skb_put(skb, pkt_len - 12 - 4 + 14 - snap);
	memcpy(eth + 14, pkt + 12 + snap, pkt_len - 12 - 4 - snap);

	/* create ethernet header: */
	eth[0]=pkt[0x0b];
	eth[1]=pkt[0x0a];
	eth[2]=pkt[0x09];
	eth[3]=pkt[0x08];
	eth[4]=pkt[0x04];
	eth[5]=pkt[0x03];

	eth[6]=eth[7]=eth[8]=eth[9]=eth[10]=eth[11]=0;

	if (snap) {
		eth[12] = pkt[18];
		eth[13] = pkt[19];
	} else {
		/* protocol numbers are from rfc-1700 or
		 * http://www.iana.org/assignments/ethernet-numbers
		 */
		if (pkt[12] >> 4 == 6) { /* version field from IP header */
			eth[12] = 0x86;	/* IPv6 */
			eth[13] = 0xdd;
		} else {
			eth[12] = 0x08;	/* IPv4 */
			eth[13] = 0x00;
		}
	}

	skb->protocol = dvb_net_eth_type_trans(skb, dev);

	stats->rx_packets++;
	stats->rx_bytes+=skb->len;
	netif_rx(skb);
}

static int dvb_net_sec_callback(const u8 *buffer1, size_t buffer1_len,
		 const u8 *buffer2, size_t buffer2_len,
		 struct dmx_section_filter *filter,
		 enum dmx_success success)
{
	struct net_device *dev = filter->priv;

	/**
	 * we rely on the DVB API definition where exactly one complete
	 * section is delivered in buffer1
	 */
	dvb_net_sec (dev, buffer1, buffer1_len);
	return 0;
}

static int dvb_net_tx(struct sk_buff *skb, struct net_device *dev)
{
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static u8 mask_normal[6]={0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static u8 mask_allmulti[6]={0xff, 0xff, 0xff, 0x00, 0x00, 0x00};
static u8 mac_allmulti[6]={0x01, 0x00, 0x5e, 0x00, 0x00, 0x00};
static u8 mask_promisc[6]={0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static int dvb_net_filter_sec_set(struct net_device *dev,
		   struct dmx_section_filter **secfilter,
		   u8 *mac, u8 *mac_mask)
{
	struct dvb_net_priv *priv = netdev_priv(dev);
	int ret;

	*secfilter=NULL;
	ret = priv->secfeed->allocate_filter(priv->secfeed, secfilter);
	if (ret<0) {
		printk("%s: could not get filter\n", dev->name);
		return ret;
	}

	(*secfilter)->priv=(void *) dev;

	memset((*secfilter)->filter_value, 0x00, DMX_MAX_FILTER_SIZE);
	memset((*secfilter)->filter_mask,  0x00, DMX_MAX_FILTER_SIZE);
	memset((*secfilter)->filter_mode,  0xff, DMX_MAX_FILTER_SIZE);

	(*secfilter)->filter_value[0]=0x3e;
	(*secfilter)->filter_value[3]=mac[5];
	(*secfilter)->filter_value[4]=mac[4];
	(*secfilter)->filter_value[8]=mac[3];
	(*secfilter)->filter_value[9]=mac[2];
	(*secfilter)->filter_value[10]=mac[1];
	(*secfilter)->filter_value[11]=mac[0];

	(*secfilter)->filter_mask[0] = 0xff;
	(*secfilter)->filter_mask[3] = mac_mask[5];
	(*secfilter)->filter_mask[4] = mac_mask[4];
	(*secfilter)->filter_mask[8] = mac_mask[3];
	(*secfilter)->filter_mask[9] = mac_mask[2];
	(*secfilter)->filter_mask[10] = mac_mask[1];
	(*secfilter)->filter_mask[11]=mac_mask[0];

	dprintk("%s: filter mac=%pM\n", dev->name, mac);
	dprintk("%s: filter mask=%pM\n", dev->name, mac_mask);

	return 0;
}

static int dvb_net_feed_start(struct net_device *dev)
{
	int ret = 0, i;
	struct dvb_net_priv *priv = netdev_priv(dev);
	struct dmx_demux *demux = priv->demux;
	unsigned char *mac = (unsigned char *) dev->dev_addr;

	dprintk("%s: rx_mode %i\n", __func__, priv->rx_mode);
	mutex_lock(&priv->mutex);
	if (priv->tsfeed || priv->secfeed || priv->secfilter || priv->multi_secfilter[0])
		printk("%s: BUG %d\n", __func__, __LINE__);

	priv->secfeed=NULL;
	priv->secfilter=NULL;
	priv->tsfeed = NULL;

	if (priv->feedtype == DVB_NET_FEEDTYPE_MPE) {
		dprintk("%s: alloc secfeed\n", __func__);
		ret=demux->allocate_section_feed(demux, &priv->secfeed,
					 dvb_net_sec_callback);
		if (ret<0) {
			printk("%s: could not allocate section feed\n", dev->name);
			goto error;
		}

		ret = priv->secfeed->set(priv->secfeed, priv->pid, 32768, 1);

		if (ret<0) {
			printk("%s: could not set section feed\n", dev->name);
			priv->demux->release_section_feed(priv->demux, priv->secfeed);
			priv->secfeed=NULL;
			goto error;
		}

		if (priv->rx_mode != RX_MODE_PROMISC) {
			dprintk("%s: set secfilter\n", __func__);
			dvb_net_filter_sec_set(dev, &priv->secfilter, mac, mask_normal);
		}

		switch (priv->rx_mode) {
		case RX_MODE_MULTI:
			for (i = 0; i < priv->multi_num; i++) {
				dprintk("%s: set multi_secfilter[%d]\n", __func__, i);
				dvb_net_filter_sec_set(dev, &priv->multi_secfilter[i],
						       priv->multi_macs[i], mask_normal);
			}
			break;
		case RX_MODE_ALL_MULTI:
			priv->multi_num=1;
			dprintk("%s: set multi_secfilter[0]\n", __func__);
			dvb_net_filter_sec_set(dev, &priv->multi_secfilter[0],
					       mac_allmulti, mask_allmulti);
			break;
		case RX_MODE_PROMISC:
			priv->multi_num=0;
			dprintk("%s: set secfilter\n", __func__);
			dvb_net_filter_sec_set(dev, &priv->secfilter, mac, mask_promisc);
			break;
		}

		dprintk("%s: start filtering\n", __func__);
		priv->secfeed->start_filtering(priv->secfeed);
	} else if (priv->feedtype == DVB_NET_FEEDTYPE_ULE) {
		struct timespec timeout = { 0, 10000000 }; // 10 msec

		/* we have payloads encapsulated in TS */
		dprintk("%s: alloc tsfeed\n", __func__);
		ret = demux->allocate_ts_feed(demux, &priv->tsfeed, dvb_net_ts_callback);
		if (ret < 0) {
			printk("%s: could not allocate ts feed\n", dev->name);
			goto error;
		}

		/* Set netdevice pointer for ts decaps callback. */
		priv->tsfeed->priv = (void *)dev;
		ret = priv->tsfeed->set(priv->tsfeed,
					priv->pid, /* pid */
					TS_PACKET, /* type */
					DMX_TS_PES_OTHER, /* pes type */
					32768,     /* circular buffer size */
					timeout    /* timeout */
					);

		if (ret < 0) {
			printk("%s: could not set ts feed\n", dev->name);
			priv->demux->release_ts_feed(priv->demux, priv->tsfeed);
			priv->tsfeed = NULL;
			goto error;
		}

		dprintk("%s: start filtering\n", __func__);
		priv->tsfeed->start_filtering(priv->tsfeed);
	} else
		ret = -EINVAL;

error:
	mutex_unlock(&priv->mutex);
	return ret;
}

static int dvb_net_feed_stop(struct net_device *dev)
{
	struct dvb_net_priv *priv = netdev_priv(dev);
	int i, ret = 0;

	dprintk("%s\n", __func__);
	mutex_lock(&priv->mutex);
	if (priv->feedtype == DVB_NET_FEEDTYPE_MPE) {
		if (priv->secfeed) {
			if (priv->secfeed->is_filtering) {
				dprintk("%s: stop secfeed\n", __func__);
				priv->secfeed->stop_filtering(priv->secfeed);
			}

			if (priv->secfilter) {
				dprintk("%s: release secfilter\n", __func__);
				priv->secfeed->release_filter(priv->secfeed,
							      priv->secfilter);
				priv->secfilter=NULL;
			}

			for (i=0; i<priv->multi_num; i++) {
				if (priv->multi_secfilter[i]) {
					dprintk("%s: release multi_filter[%d]\n",
						__func__, i);
					priv->secfeed->release_filter(priv->secfeed,
								      priv->multi_secfilter[i]);
					priv->multi_secfilter[i] = NULL;
				}
			}

			priv->demux->release_section_feed(priv->demux, priv->secfeed);
			priv->secfeed = NULL;
		} else
			printk("%s: no feed to stop\n", dev->name);
	} else if (priv->feedtype == DVB_NET_FEEDTYPE_ULE) {
		if (priv->tsfeed) {
			if (priv->tsfeed->is_filtering) {
				dprintk("%s: stop tsfeed\n", __func__);
				priv->tsfeed->stop_filtering(priv->tsfeed);
			}
			priv->demux->release_ts_feed(priv->demux, priv->tsfeed);
			priv->tsfeed = NULL;
		}
		else
			printk("%s: no ts feed to stop\n", dev->name);
	} else
		ret = -EINVAL;
	mutex_unlock(&priv->mutex);
	return ret;
}


static int dvb_set_mc_filter(struct net_device *dev, unsigned char *addr)
{
	struct dvb_net_priv *priv = netdev_priv(dev);

	if (priv->multi_num == DVB_NET_MULTICAST_MAX)
		return -ENOMEM;

	memcpy(priv->multi_macs[priv->multi_num], addr, ETH_ALEN);

	priv->multi_num++;
	return 0;
}


static void wq_set_multicast_list (struct work_struct *work)
{
	struct dvb_net_priv *priv =
		container_of(work, struct dvb_net_priv, set_multicast_list_wq);
	struct net_device *dev = priv->net;

	dvb_net_feed_stop(dev);
	priv->rx_mode = RX_MODE_UNI;
	netif_addr_lock_bh(dev);

	if (dev->flags & IFF_PROMISC) {
		dprintk("%s: promiscuous mode\n", dev->name);
		priv->rx_mode = RX_MODE_PROMISC;
	} else if ((dev->flags & IFF_ALLMULTI)) {
		dprintk("%s: allmulti mode\n", dev->name);
		priv->rx_mode = RX_MODE_ALL_MULTI;
	} else if (!netdev_mc_empty(dev)) {
		struct netdev_hw_addr *ha;

		dprintk("%s: set_mc_list, %d entries\n",
			dev->name, netdev_mc_count(dev));

		priv->rx_mode = RX_MODE_MULTI;
		priv->multi_num = 0;

		netdev_for_each_mc_addr(ha, dev)
			dvb_set_mc_filter(dev, ha->addr);
	}

	netif_addr_unlock_bh(dev);
	dvb_net_feed_start(dev);
}


static void dvb_net_set_multicast_list (struct net_device *dev)
{
	struct dvb_net_priv *priv = netdev_priv(dev);
	schedule_work(&priv->set_multicast_list_wq);
}


static void wq_restart_net_feed (struct work_struct *work)
{
	struct dvb_net_priv *priv =
		container_of(work, struct dvb_net_priv, restart_net_feed_wq);
	struct net_device *dev = priv->net;

	if (netif_running(dev)) {
		dvb_net_feed_stop(dev);
		dvb_net_feed_start(dev);
	}
}


static int dvb_net_set_mac (struct net_device *dev, void *p)
{
	struct dvb_net_priv *priv = netdev_priv(dev);
	struct sockaddr *addr=p;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	if (netif_running(dev))
		schedule_work(&priv->restart_net_feed_wq);

	return 0;
}


static int dvb_net_open(struct net_device *dev)
{
	struct dvb_net_priv *priv = netdev_priv(dev);

	priv->in_use++;
	dvb_net_feed_start(dev);
	return 0;
}


static int dvb_net_stop(struct net_device *dev)
{
	struct dvb_net_priv *priv = netdev_priv(dev);

	priv->in_use--;
	return dvb_net_feed_stop(dev);
}

static const struct header_ops dvb_header_ops = {
	.create		= eth_header,
	.parse		= eth_header_parse,
	.rebuild	= eth_rebuild_header,
};


static const struct net_device_ops dvb_netdev_ops = {
	.ndo_open		= dvb_net_open,
	.ndo_stop		= dvb_net_stop,
	.ndo_start_xmit		= dvb_net_tx,
	.ndo_set_rx_mode	= dvb_net_set_multicast_list,
	.ndo_set_mac_address    = dvb_net_set_mac,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
};

static void dvb_net_setup(struct net_device *dev)
{
	ether_setup(dev);

	dev->header_ops		= &dvb_header_ops;
	dev->netdev_ops		= &dvb_netdev_ops;
	dev->mtu		= 4096;

	dev->flags |= IFF_NOARP;
}

static int get_if(struct dvb_net *dvbnet)
{
	int i;

	for (i=0; i<DVB_NET_DEVICES_MAX; i++)
		if (!dvbnet->state[i])
			break;

	if (i == DVB_NET_DEVICES_MAX)
		return -1;

	dvbnet->state[i]=1;
	return i;
}

static int dvb_net_add_if(struct dvb_net *dvbnet, u16 pid, u8 feedtype)
{
	struct net_device *net;
	struct dvb_net_priv *priv;
	int result;
	int if_num;

	if (feedtype != DVB_NET_FEEDTYPE_MPE && feedtype != DVB_NET_FEEDTYPE_ULE)
		return -EINVAL;
	if ((if_num = get_if(dvbnet)) < 0)
		return -EINVAL;

	net = alloc_netdev(sizeof(struct dvb_net_priv), "dvb", dvb_net_setup);
	if (!net)
		return -ENOMEM;

	if (dvbnet->dvbdev->id)
		snprintf(net->name, IFNAMSIZ, "dvb%d%u%d",
			 dvbnet->dvbdev->adapter->num, dvbnet->dvbdev->id, if_num);
	else
		/* compatibility fix to keep dvb0_0 format */
		snprintf(net->name, IFNAMSIZ, "dvb%d_%d",
			 dvbnet->dvbdev->adapter->num, if_num);

	net->addr_len = 6;
	memcpy(net->dev_addr, dvbnet->dvbdev->adapter->proposed_mac, 6);

	dvbnet->device[if_num] = net;

	priv = netdev_priv(net);
	priv->net = net;
	priv->demux = dvbnet->demux;
	priv->pid = pid;
	priv->rx_mode = RX_MODE_UNI;
	priv->need_pusi = 1;
	priv->tscc = 0;
	priv->feedtype = feedtype;
	reset_ule(priv);

	INIT_WORK(&priv->set_multicast_list_wq, wq_set_multicast_list);
	INIT_WORK(&priv->restart_net_feed_wq, wq_restart_net_feed);
	mutex_init(&priv->mutex);

	net->base_addr = pid;

	if ((result = register_netdev(net)) < 0) {
		dvbnet->device[if_num] = NULL;
		free_netdev(net);
		return result;
	}
	printk("dvb_net: created network interface %s\n", net->name);

	return if_num;
}

static int dvb_net_remove_if(struct dvb_net *dvbnet, unsigned long num)
{
	struct net_device *net = dvbnet->device[num];
	struct dvb_net_priv *priv;

	if (!dvbnet->state[num])
		return -EINVAL;
	priv = netdev_priv(net);
	if (priv->in_use)
		return -EBUSY;

	dvb_net_stop(net);
	flush_work_sync(&priv->set_multicast_list_wq);
	flush_work_sync(&priv->restart_net_feed_wq);
	printk("dvb_net: removed network interface %s\n", net->name);
	unregister_netdev(net);
	dvbnet->state[num]=0;
	dvbnet->device[num] = NULL;
	free_netdev(net);

	return 0;
}

static int dvb_net_do_ioctl(struct file *file,
		  unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_net *dvbnet = dvbdev->priv;

	if (((file->f_flags&O_ACCMODE)==O_RDONLY))
		return -EPERM;

	switch (cmd) {
	case NET_ADD_IF:
	{
		struct dvb_net_if *dvbnetif = parg;
		int result;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (!try_module_get(dvbdev->adapter->module))
			return -EPERM;

		result=dvb_net_add_if(dvbnet, dvbnetif->pid, dvbnetif->feedtype);
		if (result<0) {
			module_put(dvbdev->adapter->module);
			return result;
		}
		dvbnetif->if_num=result;
		break;
	}
	case NET_GET_IF:
	{
		struct net_device *netdev;
		struct dvb_net_priv *priv_data;
		struct dvb_net_if *dvbnetif = parg;

		if (dvbnetif->if_num >= DVB_NET_DEVICES_MAX ||
		    !dvbnet->state[dvbnetif->if_num])
			return -EINVAL;

		netdev = dvbnet->device[dvbnetif->if_num];

		priv_data = netdev_priv(netdev);
		dvbnetif->pid=priv_data->pid;
		dvbnetif->feedtype=priv_data->feedtype;
		break;
	}
	case NET_REMOVE_IF:
	{
		int ret;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if ((unsigned long) parg >= DVB_NET_DEVICES_MAX)
			return -EINVAL;
		ret = dvb_net_remove_if(dvbnet, (unsigned long) parg);
		if (!ret)
			module_put(dvbdev->adapter->module);
		return ret;
	}

	/* binary compatibility cruft */
	case __NET_ADD_IF_OLD:
	{
		struct __dvb_net_if_old *dvbnetif = parg;
		int result;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (!try_module_get(dvbdev->adapter->module))
			return -EPERM;

		result=dvb_net_add_if(dvbnet, dvbnetif->pid, DVB_NET_FEEDTYPE_MPE);
		if (result<0) {
			module_put(dvbdev->adapter->module);
			return result;
		}
		dvbnetif->if_num=result;
		break;
	}
	case __NET_GET_IF_OLD:
	{
		struct net_device *netdev;
		struct dvb_net_priv *priv_data;
		struct __dvb_net_if_old *dvbnetif = parg;

		if (dvbnetif->if_num >= DVB_NET_DEVICES_MAX ||
		    !dvbnet->state[dvbnetif->if_num])
			return -EINVAL;

		netdev = dvbnet->device[dvbnetif->if_num];

		priv_data = netdev_priv(netdev);
		dvbnetif->pid=priv_data->pid;
		break;
	}
	default:
		return -ENOTTY;
	}
	return 0;
}

static long dvb_net_ioctl(struct file *file,
	      unsigned int cmd, unsigned long arg)
{
	return dvb_usercopy(file, cmd, arg, dvb_net_do_ioctl);
}

static int dvb_net_close(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_net *dvbnet = dvbdev->priv;

	dvb_generic_release(inode, file);

	if(dvbdev->users == 1 && dvbnet->exit == 1) {
		fops_put(file->f_op);
		file->f_op = NULL;
		wake_up(&dvbdev->wait_queue);
	}
	return 0;
}


static const struct file_operations dvb_net_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = dvb_net_ioctl,
	.open =	dvb_generic_open,
	.release = dvb_net_close,
	.llseek = noop_llseek,
};

static struct dvb_device dvbdev_net = {
	.priv = NULL,
	.users = 1,
	.writers = 1,
	.fops = &dvb_net_fops,
};


void dvb_net_release (struct dvb_net *dvbnet)
{
	int i;

	dvbnet->exit = 1;
	if (dvbnet->dvbdev->users < 1)
		wait_event(dvbnet->dvbdev->wait_queue,
				dvbnet->dvbdev->users==1);

	dvb_unregister_device(dvbnet->dvbdev);

	for (i=0; i<DVB_NET_DEVICES_MAX; i++) {
		if (!dvbnet->state[i])
			continue;
		dvb_net_remove_if(dvbnet, i);
	}
}
EXPORT_SYMBOL(dvb_net_release);


int dvb_net_init (struct dvb_adapter *adap, struct dvb_net *dvbnet,
		  struct dmx_demux *dmx)
{
	int i;

	dvbnet->demux = dmx;

	for (i=0; i<DVB_NET_DEVICES_MAX; i++)
		dvbnet->state[i] = 0;

	return dvb_register_device(adap, &dvbnet->dvbdev, &dvbdev_net,
			     dvbnet, DVB_DEVICE_NET);
}
EXPORT_SYMBOL(dvb_net_init);
