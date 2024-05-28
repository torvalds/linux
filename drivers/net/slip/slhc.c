/*
 * Routines to compress and uncompress tcp packets (for transmission
 * over low speed serial lines).
 *
 * Copyright (c) 1989 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	Van Jacobson (van@helios.ee.lbl.gov), Dec 31, 1989:
 *	- Initial distribution.
 *
 *
 * modified for KA9Q Internet Software Package by
 * Katie Stevens (dkstevens@ucdavis.edu)
 * University of California, Davis
 * Computing Services
 *	- 01-31-90	initial adaptation (from 1.19)
 *	PPP.05	02-15-90 [ks]
 *	PPP.08	05-02-90 [ks]	use PPP protocol field to signal compression
 *	PPP.15	09-90	 [ks]	improve mbuf handling
 *	PPP.16	11-02	 [karn]	substantially rewritten to use NOS facilities
 *
 *	- Feb 1991	Bill_Simpson@um.cc.umich.edu
 *			variable number of conversation slots
 *			allow zero or one slots
 *			separate routines
 *			status display
 *	- Jul 1994	Dmitry Gorodchanin
 *			Fixes for memory leaks.
 *      - Oct 1994      Dmitry Gorodchanin
 *                      Modularization.
 *	- Jan 1995	Bjorn Ekwall
 *			Use ip_fast_csum from ip.h
 *	- July 1995	Christos A. Polyzols
 *			Spotted bug in tcp option checking
 *
 *
 *	This module is a difficult issue. It's clearly inet code but it's also clearly
 *	driver code belonging close to PPP and SLIP
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <net/slhc_vj.h>

#ifdef CONFIG_INET
/* Entire module is for IP only */
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/termios.h>
#include <linux/in.h>
#include <linux/fcntl.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/icmp.h>
#include <net/tcp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <net/checksum.h>
#include <asm/unaligned.h>

static unsigned char *encode(unsigned char *cp, unsigned short n);
static long decode(unsigned char **cpp);
static unsigned char * put16(unsigned char *cp, unsigned short x);
static unsigned short pull16(unsigned char **cpp);

/* Allocate compression data structure
 *	slots must be in range 0 to 255 (zero meaning no compression)
 * Returns pointer to structure or ERR_PTR() on error.
 */
struct slcompress *
slhc_init(int rslots, int tslots)
{
	short i;
	struct cstate *ts;
	struct slcompress *comp;

	if (rslots < 0 || rslots > 255 || tslots < 0 || tslots > 255)
		return ERR_PTR(-EINVAL);

	comp = kzalloc(sizeof(struct slcompress), GFP_KERNEL);
	if (! comp)
		goto out_fail;

	if (rslots > 0) {
		size_t rsize = rslots * sizeof(struct cstate);
		comp->rstate = kzalloc(rsize, GFP_KERNEL);
		if (! comp->rstate)
			goto out_free;
		comp->rslot_limit = rslots - 1;
	}

	if (tslots > 0) {
		size_t tsize = tslots * sizeof(struct cstate);
		comp->tstate = kzalloc(tsize, GFP_KERNEL);
		if (! comp->tstate)
			goto out_free2;
		comp->tslot_limit = tslots - 1;
	}

	comp->xmit_oldest = 0;
	comp->xmit_current = 255;
	comp->recv_current = 255;
	/*
	 * don't accept any packets with implicit index until we get
	 * one with an explicit index.  Otherwise the uncompress code
	 * will try to use connection 255, which is almost certainly
	 * out of range
	 */
	comp->flags |= SLF_TOSS;

	if ( tslots > 0 ) {
		ts = comp->tstate;
		for(i = comp->tslot_limit; i > 0; --i){
			ts[i].cs_this = i;
			ts[i].next = &(ts[i - 1]);
		}
		ts[0].next = &(ts[comp->tslot_limit]);
		ts[0].cs_this = 0;
	}
	return comp;

out_free2:
	kfree(comp->rstate);
out_free:
	kfree(comp);
out_fail:
	return ERR_PTR(-ENOMEM);
}


/* Free a compression data structure */
void
slhc_free(struct slcompress *comp)
{
	if ( IS_ERR_OR_NULL(comp) )
		return;

	if ( comp->tstate != NULLSLSTATE )
		kfree( comp->tstate );

	if ( comp->rstate != NULLSLSTATE )
		kfree( comp->rstate );

	kfree( comp );
}


/* Put a short in host order into a char array in network order */
static inline unsigned char *
put16(unsigned char *cp, unsigned short x)
{
	*cp++ = x >> 8;
	*cp++ = x;

	return cp;
}


/* Encode a number */
static unsigned char *
encode(unsigned char *cp, unsigned short n)
{
	if(n >= 256 || n == 0){
		*cp++ = 0;
		cp = put16(cp,n);
	} else {
		*cp++ = n;
	}
	return cp;
}

/* Pull a 16-bit integer in host order from buffer in network byte order */
static unsigned short
pull16(unsigned char **cpp)
{
	short rval;

	rval = *(*cpp)++;
	rval <<= 8;
	rval |= *(*cpp)++;
	return rval;
}

/* Decode a number */
static long
decode(unsigned char **cpp)
{
	int x;

	x = *(*cpp)++;
	if(x == 0){
		return pull16(cpp) & 0xffff;	/* pull16 returns -1 on error */
	} else {
		return x & 0xff;		/* -1 if PULLCHAR returned error */
	}
}

/*
 * icp and isize are the original packet.
 * ocp is a place to put a copy if necessary.
 * cpp is initially a pointer to icp.  If the copy is used,
 *    change it to ocp.
 */

int
slhc_compress(struct slcompress *comp, unsigned char *icp, int isize,
	unsigned char *ocp, unsigned char **cpp, int compress_cid)
{
	struct cstate *ocs = &(comp->tstate[comp->xmit_oldest]);
	struct cstate *lcs = ocs;
	struct cstate *cs = lcs->next;
	unsigned long deltaS, deltaA;
	short changes = 0;
	int nlen, hlen;
	unsigned char new_seq[16];
	unsigned char *cp = new_seq;
	struct iphdr *ip;
	struct tcphdr *th, *oth;
	__sum16 csum;


	/*
	 *	Don't play with runt packets.
	 */

	if(isize<sizeof(struct iphdr))
		return isize;

	ip = (struct iphdr *) icp;
	if (ip->version != 4 || ip->ihl < 5)
		return isize;

	/* Bail if this packet isn't TCP, or is an IP fragment */
	if (ip->protocol != IPPROTO_TCP || (ntohs(ip->frag_off) & 0x3fff)) {
		/* Send as regular IP */
		if(ip->protocol != IPPROTO_TCP)
			comp->sls_o_nontcp++;
		else
			comp->sls_o_tcp++;
		return isize;
	}
	nlen = ip->ihl * 4;
	if (isize < nlen + sizeof(*th))
		return isize;

	th = (struct tcphdr *)(icp + nlen);
	if (th->doff < sizeof(struct tcphdr) / 4)
		return isize;
	hlen = nlen + th->doff * 4;

	/*  Bail if the TCP packet isn't `compressible' (i.e., ACK isn't set or
	 *  some other control bit is set). Also uncompressible if
	 *  it's a runt.
	 */
	if(hlen > isize || th->syn || th->fin || th->rst ||
	    ! (th->ack)){
		/* TCP connection stuff; send as regular IP */
		comp->sls_o_tcp++;
		return isize;
	}
	/*
	 * Packet is compressible -- we're going to send either a
	 * COMPRESSED_TCP or UNCOMPRESSED_TCP packet.  Either way,
	 * we need to locate (or create) the connection state.
	 *
	 * States are kept in a circularly linked list with
	 * xmit_oldest pointing to the end of the list.  The
	 * list is kept in lru order by moving a state to the
	 * head of the list whenever it is referenced.  Since
	 * the list is short and, empirically, the connection
	 * we want is almost always near the front, we locate
	 * states via linear search.  If we don't find a state
	 * for the datagram, the oldest state is (re-)used.
	 */
	for ( ; ; ) {
		if( ip->saddr == cs->cs_ip.saddr
		 && ip->daddr == cs->cs_ip.daddr
		 && th->source == cs->cs_tcp.source
		 && th->dest == cs->cs_tcp.dest)
			goto found;

		/* if current equal oldest, at end of list */
		if ( cs == ocs )
			break;
		lcs = cs;
		cs = cs->next;
		comp->sls_o_searches++;
	}
	/*
	 * Didn't find it -- re-use oldest cstate.  Send an
	 * uncompressed packet that tells the other side what
	 * connection number we're using for this conversation.
	 *
	 * Note that since the state list is circular, the oldest
	 * state points to the newest and we only need to set
	 * xmit_oldest to update the lru linkage.
	 */
	comp->sls_o_misses++;
	comp->xmit_oldest = lcs->cs_this;
	goto uncompressed;

found:
	/*
	 * Found it -- move to the front on the connection list.
	 */
	if(lcs == ocs) {
		/* found at most recently used */
	} else if (cs == ocs) {
		/* found at least recently used */
		comp->xmit_oldest = lcs->cs_this;
	} else {
		/* more than 2 elements */
		lcs->next = cs->next;
		cs->next = ocs->next;
		ocs->next = cs;
	}

	/*
	 * Make sure that only what we expect to change changed.
	 * Check the following:
	 * IP protocol version, header length & type of service.
	 * The "Don't fragment" bit.
	 * The time-to-live field.
	 * The TCP header length.
	 * IP options, if any.
	 * TCP options, if any.
	 * If any of these things are different between the previous &
	 * current datagram, we send the current datagram `uncompressed'.
	 */
	oth = &cs->cs_tcp;

	if(ip->version != cs->cs_ip.version || ip->ihl != cs->cs_ip.ihl
	 || ip->tos != cs->cs_ip.tos
	 || (ip->frag_off & htons(0x4000)) != (cs->cs_ip.frag_off & htons(0x4000))
	 || ip->ttl != cs->cs_ip.ttl
	 || th->doff != cs->cs_tcp.doff
	 || (ip->ihl > 5 && memcmp(ip+1,cs->cs_ipopt,((ip->ihl)-5)*4) != 0)
	 || (th->doff > 5 && memcmp(th+1,cs->cs_tcpopt,((th->doff)-5)*4) != 0)){
		goto uncompressed;
	}

	/*
	 * Figure out which of the changing fields changed.  The
	 * receiver expects changes in the order: urgent, window,
	 * ack, seq (the order minimizes the number of temporaries
	 * needed in this section of code).
	 */
	if(th->urg){
		deltaS = ntohs(th->urg_ptr);
		cp = encode(cp,deltaS);
		changes |= NEW_U;
	} else if(th->urg_ptr != oth->urg_ptr){
		/* argh! URG not set but urp changed -- a sensible
		 * implementation should never do this but RFC793
		 * doesn't prohibit the change so we have to deal
		 * with it. */
		goto uncompressed;
	}
	if((deltaS = ntohs(th->window) - ntohs(oth->window)) != 0){
		cp = encode(cp,deltaS);
		changes |= NEW_W;
	}
	if((deltaA = ntohl(th->ack_seq) - ntohl(oth->ack_seq)) != 0L){
		if(deltaA > 0x0000ffff)
			goto uncompressed;
		cp = encode(cp,deltaA);
		changes |= NEW_A;
	}
	if((deltaS = ntohl(th->seq) - ntohl(oth->seq)) != 0L){
		if(deltaS > 0x0000ffff)
			goto uncompressed;
		cp = encode(cp,deltaS);
		changes |= NEW_S;
	}

	switch(changes){
	case 0:	/* Nothing changed. If this packet contains data and the
		 * last one didn't, this is probably a data packet following
		 * an ack (normal on an interactive connection) and we send
		 * it compressed.  Otherwise it's probably a retransmit,
		 * retransmitted ack or window probe.  Send it uncompressed
		 * in case the other side missed the compressed version.
		 */
		if(ip->tot_len != cs->cs_ip.tot_len &&
		   ntohs(cs->cs_ip.tot_len) == hlen)
			break;
		goto uncompressed;
	case SPECIAL_I:
	case SPECIAL_D:
		/* actual changes match one of our special case encodings --
		 * send packet uncompressed.
		 */
		goto uncompressed;
	case NEW_S|NEW_A:
		if(deltaS == deltaA &&
		    deltaS == ntohs(cs->cs_ip.tot_len) - hlen){
			/* special case for echoed terminal traffic */
			changes = SPECIAL_I;
			cp = new_seq;
		}
		break;
	case NEW_S:
		if(deltaS == ntohs(cs->cs_ip.tot_len) - hlen){
			/* special case for data xfer */
			changes = SPECIAL_D;
			cp = new_seq;
		}
		break;
	}
	deltaS = ntohs(ip->id) - ntohs(cs->cs_ip.id);
	if(deltaS != 1){
		cp = encode(cp,deltaS);
		changes |= NEW_I;
	}
	if(th->psh)
		changes |= TCP_PUSH_BIT;
	/* Grab the cksum before we overwrite it below.  Then update our
	 * state with this packet's header.
	 */
	csum = th->check;
	memcpy(&cs->cs_ip,ip,20);
	memcpy(&cs->cs_tcp,th,20);
	/* We want to use the original packet as our compressed packet.
	 * (cp - new_seq) is the number of bytes we need for compressed
	 * sequence numbers.  In addition we need one byte for the change
	 * mask, one for the connection id and two for the tcp checksum.
	 * So, (cp - new_seq) + 4 bytes of header are needed.
	 */
	deltaS = cp - new_seq;
	if(compress_cid == 0 || comp->xmit_current != cs->cs_this){
		cp = ocp;
		*cpp = ocp;
		*cp++ = changes | NEW_C;
		*cp++ = cs->cs_this;
		comp->xmit_current = cs->cs_this;
	} else {
		cp = ocp;
		*cpp = ocp;
		*cp++ = changes;
	}
	*(__sum16 *)cp = csum;
	cp += 2;
/* deltaS is now the size of the change section of the compressed header */
	memcpy(cp,new_seq,deltaS);	/* Write list of deltas */
	memcpy(cp+deltaS,icp+hlen,isize-hlen);
	comp->sls_o_compressed++;
	ocp[0] |= SL_TYPE_COMPRESSED_TCP;
	return isize - hlen + deltaS + (cp - ocp);

	/* Update connection state cs & send uncompressed packet (i.e.,
	 * a regular ip/tcp packet but with the 'conversation id' we hope
	 * to use on future compressed packets in the protocol field).
	 */
uncompressed:
	memcpy(&cs->cs_ip,ip,20);
	memcpy(&cs->cs_tcp,th,20);
	if (ip->ihl > 5)
	  memcpy(cs->cs_ipopt, ip+1, ((ip->ihl) - 5) * 4);
	if (th->doff > 5)
	  memcpy(cs->cs_tcpopt, th+1, ((th->doff) - 5) * 4);
	comp->xmit_current = cs->cs_this;
	comp->sls_o_uncompressed++;
	memcpy(ocp, icp, isize);
	*cpp = ocp;
	ocp[9] = cs->cs_this;
	ocp[0] |= SL_TYPE_UNCOMPRESSED_TCP;
	return isize;
}


int
slhc_uncompress(struct slcompress *comp, unsigned char *icp, int isize)
{
	int changes;
	long x;
	struct tcphdr *thp;
	struct iphdr *ip;
	struct cstate *cs;
	int len, hdrlen;
	unsigned char *cp = icp;

	/* We've got a compressed packet; read the change byte */
	comp->sls_i_compressed++;
	if(isize < 3){
		comp->sls_i_error++;
		return 0;
	}
	changes = *cp++;
	if(changes & NEW_C){
		/* Make sure the state index is in range, then grab the state.
		 * If we have a good state index, clear the 'discard' flag.
		 */
		x = *cp++;	/* Read conn index */
		if(x < 0 || x > comp->rslot_limit)
			goto bad;

		/* Check if the cstate is initialized */
		if (!comp->rstate[x].initialized)
			goto bad;

		comp->flags &=~ SLF_TOSS;
		comp->recv_current = x;
	} else {
		/* this packet has an implicit state index.  If we've
		 * had a line error since the last time we got an
		 * explicit state index, we have to toss the packet. */
		if(comp->flags & SLF_TOSS){
			comp->sls_i_tossed++;
			return 0;
		}
	}
	cs = &comp->rstate[comp->recv_current];
	thp = &cs->cs_tcp;
	ip = &cs->cs_ip;

	thp->check = *(__sum16 *)cp;
	cp += 2;

	thp->psh = (changes & TCP_PUSH_BIT) ? 1 : 0;
/*
 * we can use the same number for the length of the saved header and
 * the current one, because the packet wouldn't have been sent
 * as compressed unless the options were the same as the previous one
 */

	hdrlen = ip->ihl * 4 + thp->doff * 4;

	switch(changes & SPECIALS_MASK){
	case SPECIAL_I:		/* Echoed terminal traffic */
		{
		short i;
		i = ntohs(ip->tot_len) - hdrlen;
		thp->ack_seq = htonl( ntohl(thp->ack_seq) + i);
		thp->seq = htonl( ntohl(thp->seq) + i);
		}
		break;

	case SPECIAL_D:			/* Unidirectional data */
		thp->seq = htonl( ntohl(thp->seq) +
				  ntohs(ip->tot_len) - hdrlen);
		break;

	default:
		if(changes & NEW_U){
			thp->urg = 1;
			if((x = decode(&cp)) == -1) {
				goto bad;
			}
			thp->urg_ptr = htons(x);
		} else
			thp->urg = 0;
		if(changes & NEW_W){
			if((x = decode(&cp)) == -1) {
				goto bad;
			}
			thp->window = htons( ntohs(thp->window) + x);
		}
		if(changes & NEW_A){
			if((x = decode(&cp)) == -1) {
				goto bad;
			}
			thp->ack_seq = htonl( ntohl(thp->ack_seq) + x);
		}
		if(changes & NEW_S){
			if((x = decode(&cp)) == -1) {
				goto bad;
			}
			thp->seq = htonl( ntohl(thp->seq) + x);
		}
		break;
	}
	if(changes & NEW_I){
		if((x = decode(&cp)) == -1) {
			goto bad;
		}
		ip->id = htons (ntohs (ip->id) + x);
	} else
		ip->id = htons (ntohs (ip->id) + 1);

	/*
	 * At this point, cp points to the first byte of data in the
	 * packet.  Put the reconstructed TCP and IP headers back on the
	 * packet.  Recalculate IP checksum (but not TCP checksum).
	 */

	len = isize - (cp - icp);
	if (len < 0)
		goto bad;
	len += hdrlen;
	ip->tot_len = htons(len);
	ip->check = 0;

	memmove(icp + hdrlen, cp, len - hdrlen);

	cp = icp;
	memcpy(cp, ip, 20);
	cp += 20;

	if (ip->ihl > 5) {
	  memcpy(cp, cs->cs_ipopt, (ip->ihl - 5) * 4);
	  cp += (ip->ihl - 5) * 4;
	}

	put_unaligned(ip_fast_csum(icp, ip->ihl),
		      &((struct iphdr *)icp)->check);

	memcpy(cp, thp, 20);
	cp += 20;

	if (thp->doff > 5) {
	  memcpy(cp, cs->cs_tcpopt, ((thp->doff) - 5) * 4);
	  cp += ((thp->doff) - 5) * 4;
	}

	return len;
bad:
	comp->sls_i_error++;
	return slhc_toss( comp );
}


int
slhc_remember(struct slcompress *comp, unsigned char *icp, int isize)
{
	struct cstate *cs;
	unsigned ihl;

	unsigned char index;

	if(isize < 20) {
		/* The packet is shorter than a legal IP header */
		comp->sls_i_runt++;
		return slhc_toss( comp );
	}
	/* Peek at the IP header's IHL field to find its length */
	ihl = icp[0] & 0xf;
	if(ihl < 20 / 4){
		/* The IP header length field is too small */
		comp->sls_i_runt++;
		return slhc_toss( comp );
	}
	index = icp[9];
	icp[9] = IPPROTO_TCP;

	if (ip_fast_csum(icp, ihl)) {
		/* Bad IP header checksum; discard */
		comp->sls_i_badcheck++;
		return slhc_toss( comp );
	}
	if(index > comp->rslot_limit) {
		comp->sls_i_error++;
		return slhc_toss(comp);
	}

	/* Update local state */
	cs = &comp->rstate[comp->recv_current = index];
	comp->flags &=~ SLF_TOSS;
	memcpy(&cs->cs_ip,icp,20);
	memcpy(&cs->cs_tcp,icp + ihl*4,20);
	if (ihl > 5)
	  memcpy(cs->cs_ipopt, icp + sizeof(struct iphdr), (ihl - 5) * 4);
	if (cs->cs_tcp.doff > 5)
	  memcpy(cs->cs_tcpopt, icp + ihl*4 + sizeof(struct tcphdr), (cs->cs_tcp.doff - 5) * 4);
	cs->cs_hsize = ihl*2 + cs->cs_tcp.doff*2;
	cs->initialized = true;
	/* Put headers back on packet
	 * Neither header checksum is recalculated
	 */
	comp->sls_i_uncompressed++;
	return isize;
}

int
slhc_toss(struct slcompress *comp)
{
	if ( comp == NULLSLCOMPR )
		return 0;

	comp->flags |= SLF_TOSS;
	return 0;
}

#else /* CONFIG_INET */

int
slhc_toss(struct slcompress *comp)
{
  printk(KERN_DEBUG "Called IP function on non IP-system: slhc_toss");
  return -EINVAL;
}
int
slhc_uncompress(struct slcompress *comp, unsigned char *icp, int isize)
{
  printk(KERN_DEBUG "Called IP function on non IP-system: slhc_uncompress");
  return -EINVAL;
}
int
slhc_compress(struct slcompress *comp, unsigned char *icp, int isize,
	unsigned char *ocp, unsigned char **cpp, int compress_cid)
{
  printk(KERN_DEBUG "Called IP function on non IP-system: slhc_compress");
  return -EINVAL;
}

int
slhc_remember(struct slcompress *comp, unsigned char *icp, int isize)
{
  printk(KERN_DEBUG "Called IP function on non IP-system: slhc_remember");
  return -EINVAL;
}

void
slhc_free(struct slcompress *comp)
{
  printk(KERN_DEBUG "Called IP function on non IP-system: slhc_free");
}
struct slcompress *
slhc_init(int rslots, int tslots)
{
  printk(KERN_DEBUG "Called IP function on non IP-system: slhc_init");
  return NULL;
}

#endif /* CONFIG_INET */

/* VJ header compression */
EXPORT_SYMBOL(slhc_init);
EXPORT_SYMBOL(slhc_free);
EXPORT_SYMBOL(slhc_remember);
EXPORT_SYMBOL(slhc_compress);
EXPORT_SYMBOL(slhc_uncompress);
EXPORT_SYMBOL(slhc_toss);

MODULE_DESCRIPTION("Compression helpers for SLIP (serial line)");
MODULE_LICENSE("Dual BSD/GPL");
