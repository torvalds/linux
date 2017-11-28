/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef _LINUX_IP_H
#define _LINUX_IP_H

/* SOL_IP socket options */

#define IPTOS_TOS_MASK		0x1E
#define IPTOS_TOS(tos)		((tos)&IPTOS_TOS_MASK)
#define	IPTOS_LOWDELAY		0x10
#define	IPTOS_THROUGHPUT	0x08
#define	IPTOS_RELIABILITY	0x04
#define	IPTOS_MINCOST		0x02

#define IPTOS_PREC_MASK		0xE0
#define IPTOS_PREC(tos)		((tos)&IPTOS_PREC_MASK)
#define IPTOS_PREC_NETCONTROL           0xe0
#define IPTOS_PREC_INTERNETCONTROL      0xc0
#define IPTOS_PREC_CRITIC_ECP           0xa0
#define IPTOS_PREC_FLASHOVERRIDE        0x80
#define IPTOS_PREC_FLASH                0x60
#define IPTOS_PREC_IMMEDIATE            0x40
#define IPTOS_PREC_PRIORITY             0x20
#define IPTOS_PREC_ROUTINE              0x00


/* IP options */
#define IPOPT_COPY		0x80
#define IPOPT_CLASS_MASK	0x60
#define IPOPT_NUMBER_MASK	0x1f

#define	IPOPT_COPIED(o)		((o)&IPOPT_COPY)
#define	IPOPT_CLASS(o)		((o)&IPOPT_CLASS_MASK)
#define	IPOPT_NUMBER(o)		((o)&IPOPT_NUMBER_MASK)

#define	IPOPT_CONTROL		0x00
#define	IPOPT_RESERVED1		0x20
#define	IPOPT_MEASUREMENT	0x40
#define	IPOPT_RESERVED2		0x60

#define IPOPT_END	(0 | IPOPT_CONTROL)
#define IPOPT_NOOP	(1 | IPOPT_CONTROL)
#define IPOPT_SEC	(2 | IPOPT_CONTROL | IPOPT_COPY)
#define IPOPT_LSRR	(3 | IPOPT_CONTROL | IPOPT_COPY)
#define IPOPT_TIMESTAMP	(4 | IPOPT_MEASUREMENT)
#define IPOPT_RR	(7 | IPOPT_CONTROL)
#define IPOPT_SID	(8 | IPOPT_CONTROL | IPOPT_COPY)
#define IPOPT_SSRR	(9 | IPOPT_CONTROL | IPOPT_COPY)
#define IPOPT_RA	(20 | IPOPT_CONTROL | IPOPT_COPY)

#define IPVERSION	4
#define MAXTTL		255
#define IPDEFTTL	64

/* struct timestamp, struct route and MAX_ROUTES are removed.

   REASONS: it is clear that nobody used them because:
   - MAX_ROUTES value was wrong.
   - "struct route" was wrong.
   - "struct timestamp" had fatally misaligned bitfields and was completely unusable.
 */

#define IPOPT_OPTVAL 0
#define IPOPT_OLEN   1
#define IPOPT_OFFSET 2
#define IPOPT_MINOFF 4
#define MAX_IPOPTLEN 40
#define IPOPT_NOP IPOPT_NOOP
#define IPOPT_EOL IPOPT_END
#define IPOPT_TS  IPOPT_TIMESTAMP

#define	IPOPT_TS_TSONLY		0		/* timestamps only */
#define	IPOPT_TS_TSANDADDR	1		/* timestamps and addresses */
#define	IPOPT_TS_PRESPEC	3		/* specified modules only */

#ifdef PLATFORM_LINUX

struct ip_options {
	__u32		faddr;				/* Saved first hop address */
	unsigned char	optlen;
	unsigned char srr;
	unsigned char rr;
	unsigned char ts;
	unsigned char is_setbyuser:1,			/* Set by setsockopt?			*/
		 is_data:1,			/* Options in __data, rather than skb	*/
		 is_strictroute:1,		/* Strict source route			*/
		 srr_is_hit:1,			/* Packet destination addr was our one	*/
		 is_changed:1,			/* IP checksum more not valid		*/
		 rr_needaddr:1,			/* Need to record addr of outgoing dev	*/
		 ts_needtime:1,			/* Need to record timestamp		*/
		 ts_needaddr:1;			/* Need to record addr of outgoing dev */
	unsigned char router_alert;
	unsigned char __pad1;
	unsigned char __pad2;
	unsigned char __data[0];
};

#define optlength(opt) (sizeof(struct ip_options) + opt->optlen)
#endif

struct iphdr {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8	ihl:4,
		version:4;
#elif defined (__BIG_ENDIAN_BITFIELD)
	__u8	version:4,
		ihl:4;
#else
#error	"Please fix <asm/byteorder.h>"
#endif
	__u8	tos;
	__u16	tot_len;
	__u16	id;
	__u16	frag_off;
	__u8	ttl;
	__u8	protocol;
	__u16	check;
	__u32	saddr;
	__u32	daddr;
	/*The options start here. */
};

#endif	/* _LINUX_IP_H */
