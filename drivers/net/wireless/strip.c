/*
 * Copyright 1996 The Board of Trustees of The Leland Stanford
 * Junior University. All Rights Reserved.
 *
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  Stanford University
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 * strip.c	This module implements Starmode Radio IP (STRIP)
 *		for kernel-based devices like TTY.  It interfaces between a
 *		raw TTY, and the kernel's INET protocol layers (via DDI).
 *
 * Version:	@(#)strip.c	1.3	July 1997
 *
 * Author:	Stuart Cheshire <cheshire@cs.stanford.edu>
 *
 * Fixes:	v0.9 12th Feb 1996 (SC)
 *		New byte stuffing (2+6 run-length encoding)
 *		New watchdog timer task
 *		New Protocol key (SIP0)
 *		
 *		v0.9.1 3rd March 1996 (SC)
 *		Changed to dynamic device allocation -- no more compile
 *		time (or boot time) limit on the number of STRIP devices.
 *		
 *		v0.9.2 13th March 1996 (SC)
 *		Uses arp cache lookups (but doesn't send arp packets yet)
 *		
 *		v0.9.3 17th April 1996 (SC)
 *		Fixed bug where STR_ERROR flag was getting set unneccessarily
 *		(causing otherwise good packets to be unneccessarily dropped)
 *		
 *		v0.9.4 27th April 1996 (SC)
 *		First attempt at using "&COMMAND" Starmode AT commands
 *		
 *		v0.9.5 29th May 1996 (SC)
 *		First attempt at sending (unicast) ARP packets
 *		
 *		v0.9.6 5th June 1996 (Elliot)
 *		Put "message level" tags in every "printk" statement
 *		
 *		v0.9.7 13th June 1996 (laik)
 *		Added support for the /proc fs
 *
 *              v0.9.8 July 1996 (Mema)
 *              Added packet logging
 *
 *              v1.0 November 1996 (SC)
 *              Fixed (severe) memory leaks in the /proc fs code
 *              Fixed race conditions in the logging code
 *
 *              v1.1 January 1997 (SC)
 *              Deleted packet logging (use tcpdump instead)
 *              Added support for Metricom Firmware v204 features
 *              (like message checksums)
 *
 *              v1.2 January 1997 (SC)
 *              Put portables list back in
 *
 *              v1.3 July 1997 (SC)
 *              Made STRIP driver set the radio's baud rate automatically.
 *              It is no longer necessarily to manually set the radio's
 *              rate permanently to 115200 -- the driver handles setting
 *              the rate automatically.
 */

#ifdef MODULE
static const char StripVersion[] = "1.3A-STUART.CHESHIRE-MODULAR";
#else
static const char StripVersion[] = "1.3A-STUART.CHESHIRE";
#endif

#define TICKLE_TIMERS 0
#define EXT_COUNTERS 1


/************************************************************************/
/* Header files								*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <asm/system.h>
#include <asm/uaccess.h>

# include <linux/ctype.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/if_strip.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/serial.h>
#include <linux/serialP.h>
#include <linux/rcupdate.h>
#include <net/arp.h>
#include <net/net_namespace.h>

#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/time.h>
#include <linux/jiffies.h>

/************************************************************************/
/* Useful structures and definitions					*/

/*
 * A MetricomKey identifies the protocol being carried inside a Metricom
 * Starmode packet.
 */

typedef union {
	__u8 c[4];
	__u32 l;
} MetricomKey;

/*
 * An IP address can be viewed as four bytes in memory (which is what it is) or as
 * a single 32-bit long (which is convenient for assignment, equality testing etc.)
 */

typedef union {
	__u8 b[4];
	__u32 l;
} IPaddr;

/*
 * A MetricomAddressString is used to hold a printable representation of
 * a Metricom address.
 */

typedef struct {
	__u8 c[24];
} MetricomAddressString;

/* Encapsulation can expand packet of size x to 65/64x + 1
 * Sent packet looks like "<CR>*<address>*<key><encaps payload><CR>"
 *                           1 1   1-18  1  4         ?         1
 * eg.                     <CR>*0000-1234*SIP0<encaps payload><CR>
 * We allow 31 bytes for the stars, the key, the address and the <CR>s
 */
#define STRIP_ENCAP_SIZE(X) (32 + (X)*65L/64L)

/*
 * A STRIP_Header is never really sent over the radio, but making a dummy
 * header for internal use within the kernel that looks like an Ethernet
 * header makes certain other software happier. For example, tcpdump
 * already understands Ethernet headers.
 */

typedef struct {
	MetricomAddress dst_addr;	/* Destination address, e.g. "0000-1234"   */
	MetricomAddress src_addr;	/* Source address, e.g. "0000-5678"        */
	unsigned short protocol;	/* The protocol type, using Ethernet codes */
} STRIP_Header;

typedef struct {
	char c[60];
} MetricomNode;

#define NODE_TABLE_SIZE 32
typedef struct {
	struct timeval timestamp;
	int num_nodes;
	MetricomNode node[NODE_TABLE_SIZE];
} MetricomNodeTable;

enum { FALSE = 0, TRUE = 1 };

/*
 * Holds the radio's firmware version.
 */
typedef struct {
	char c[50];
} FirmwareVersion;

/*
 * Holds the radio's serial number.
 */
typedef struct {
	char c[18];
} SerialNumber;

/*
 * Holds the radio's battery voltage.
 */
typedef struct {
	char c[11];
} BatteryVoltage;

typedef struct {
	char c[8];
} char8;

enum {
	NoStructure = 0,	/* Really old firmware */
	StructuredMessages = 1,	/* Parsable AT response msgs */
	ChecksummedMessages = 2	/* Parsable AT response msgs with checksums */
};

struct strip {
	int magic;
	/*
	 * These are pointers to the malloc()ed frame buffers.
	 */

	unsigned char *rx_buff;	/* buffer for received IP packet */
	unsigned char *sx_buff;	/* buffer for received serial data */
	int sx_count;		/* received serial data counter */
	int sx_size;		/* Serial buffer size           */
	unsigned char *tx_buff;	/* transmitter buffer           */
	unsigned char *tx_head;	/* pointer to next byte to XMIT */
	int tx_left;		/* bytes left in XMIT queue     */
	int tx_size;		/* Serial buffer size           */

	/*
	 * STRIP interface statistics.
	 */

	unsigned long rx_packets;	/* inbound frames counter       */
	unsigned long tx_packets;	/* outbound frames counter      */
	unsigned long rx_errors;	/* Parity, etc. errors          */
	unsigned long tx_errors;	/* Planned stuff                */
	unsigned long rx_dropped;	/* No memory for skb            */
	unsigned long tx_dropped;	/* When MTU change              */
	unsigned long rx_over_errors;	/* Frame bigger then STRIP buf. */

	unsigned long pps_timer;	/* Timer to determine pps       */
	unsigned long rx_pps_count;	/* Counter to determine pps     */
	unsigned long tx_pps_count;	/* Counter to determine pps     */
	unsigned long sx_pps_count;	/* Counter to determine pps     */
	unsigned long rx_average_pps;	/* rx packets per second * 8    */
	unsigned long tx_average_pps;	/* tx packets per second * 8    */
	unsigned long sx_average_pps;	/* sent packets per second * 8  */

#ifdef EXT_COUNTERS
	unsigned long rx_bytes;		/* total received bytes */
	unsigned long tx_bytes;		/* total received bytes */
	unsigned long rx_rbytes;	/* bytes thru radio i/f */
	unsigned long tx_rbytes;	/* bytes thru radio i/f */
	unsigned long rx_sbytes;	/* tot bytes thru serial i/f */
	unsigned long tx_sbytes;	/* tot bytes thru serial i/f */
	unsigned long rx_ebytes;	/* tot stat/err bytes */
	unsigned long tx_ebytes;	/* tot stat/err bytes */
#endif

	/*
	 * Internal variables.
	 */

	struct list_head  list;		/* Linked list of devices */

	int discard;			/* Set if serial error          */
	int working;			/* Is radio working correctly?  */
	int firmware_level;		/* Message structuring level    */
	int next_command;		/* Next periodic command        */
	unsigned int user_baud;		/* The user-selected baud rate  */
	int mtu;			/* Our mtu (to spot changes!)   */
	long watchdog_doprobe;		/* Next time to test the radio  */
	long watchdog_doreset;		/* Time to do next reset        */
	long gratuitous_arp;		/* Time to send next ARP refresh */
	long arp_interval;		/* Next ARP interval            */
	struct timer_list idle_timer;	/* For periodic wakeup calls    */
	MetricomAddress true_dev_addr;	/* True address of radio        */
	int manual_dev_addr;		/* Hack: See note below         */

	FirmwareVersion firmware_version;	/* The radio's firmware version */
	SerialNumber serial_number;	/* The radio's serial number    */
	BatteryVoltage battery_voltage;	/* The radio's battery voltage  */

	/*
	 * Other useful structures.
	 */

	struct tty_struct *tty;		/* ptr to TTY structure         */
	struct net_device *dev;		/* Our device structure         */

	/*
	 * Neighbour radio records
	 */

	MetricomNodeTable portables;
	MetricomNodeTable poletops;
};

/*
 * Note: manual_dev_addr hack
 * 
 * It is not possible to change the hardware address of a Metricom radio,
 * or to send packets with a user-specified hardware source address, thus
 * trying to manually set a hardware source address is a questionable
 * thing to do.  However, if the user *does* manually set the hardware
 * source address of a STRIP interface, then the kernel will believe it,
 * and use it in certain places. For example, the hardware address listed
 * by ifconfig will be the manual address, not the true one.
 * (Both addresses are listed in /proc/net/strip.)
 * Also, ARP packets will be sent out giving the user-specified address as
 * the source address, not the real address. This is dangerous, because
 * it means you won't receive any replies -- the ARP replies will go to
 * the specified address, which will be some other radio. The case where
 * this is useful is when that other radio is also connected to the same
 * machine. This allows you to connect a pair of radios to one machine,
 * and to use one exclusively for inbound traffic, and the other
 * exclusively for outbound traffic. Pretty neat, huh?
 * 
 * Here's the full procedure to set this up:
 * 
 * 1. "slattach" two interfaces, e.g. st0 for outgoing packets,
 *    and st1 for incoming packets
 * 
 * 2. "ifconfig" st0 (outbound radio) to have the hardware address
 *    which is the real hardware address of st1 (inbound radio).
 *    Now when it sends out packets, it will masquerade as st1, and
 *    replies will be sent to that radio, which is exactly what we want.
 * 
 * 3. Set the route table entry ("route add default ..." or
 *    "route add -net ...", as appropriate) to send packets via the st0
 *    interface (outbound radio). Do not add any route which sends packets
 *    out via the st1 interface -- that radio is for inbound traffic only.
 * 
 * 4. "ifconfig" st1 (inbound radio) to have hardware address zero.
 *    This tells the STRIP driver to "shut down" that interface and not
 *    send any packets through it. In particular, it stops sending the
 *    periodic gratuitous ARP packets that a STRIP interface normally sends.
 *    Also, when packets arrive on that interface, it will search the
 *    interface list to see if there is another interface who's manual
 *    hardware address matches its own real address (i.e. st0 in this
 *    example) and if so it will transfer ownership of the skbuff to
 *    that interface, so that it looks to the kernel as if the packet
 *    arrived on that interface. This is necessary because when the
 *    kernel sends an ARP packet on st0, it expects to get a reply on
 *    st0, and if it sees the reply come from st1 then it will ignore
 *    it (to be accurate, it puts the entry in the ARP table, but
 *    labelled in such a way that st0 can't use it).
 * 
 * Thanks to Petros Maniatis for coming up with the idea of splitting
 * inbound and outbound traffic between two interfaces, which turned
 * out to be really easy to implement, even if it is a bit of a hack.
 * 
 * Having set a manual address on an interface, you can restore it
 * to automatic operation (where the address is automatically kept
 * consistent with the real address of the radio) by setting a manual
 * address of all ones, e.g. "ifconfig st0 hw strip FFFFFFFFFFFF"
 * This 'turns off' manual override mode for the device address.
 * 
 * Note: The IEEE 802 headers reported in tcpdump will show the *real*
 * radio addresses the packets were sent and received from, so that you
 * can see what is really going on with packets, and which interfaces
 * they are really going through.
 */


/************************************************************************/
/* Constants								*/

/*
 * CommandString1 works on all radios
 * Other CommandStrings are only used with firmware that provides structured responses.
 * 
 * ats319=1 Enables Info message for node additions and deletions
 * ats319=2 Enables Info message for a new best node
 * ats319=4 Enables checksums
 * ats319=8 Enables ACK messages
 */

static const int MaxCommandStringLength = 32;
static const int CompatibilityCommand = 1;

static const char CommandString0[] = "*&COMMAND*ATS319=7";	/* Turn on checksums & info messages */
static const char CommandString1[] = "*&COMMAND*ATS305?";	/* Query radio name */
static const char CommandString2[] = "*&COMMAND*ATS325?";	/* Query battery voltage */
static const char CommandString3[] = "*&COMMAND*ATS300?";	/* Query version information */
static const char CommandString4[] = "*&COMMAND*ATS311?";	/* Query poletop list */
static const char CommandString5[] = "*&COMMAND*AT~LA";		/* Query portables list */
typedef struct {
	const char *string;
	long length;
} StringDescriptor;

static const StringDescriptor CommandString[] = {
	{CommandString0, sizeof(CommandString0) - 1},
	{CommandString1, sizeof(CommandString1) - 1},
	{CommandString2, sizeof(CommandString2) - 1},
	{CommandString3, sizeof(CommandString3) - 1},
	{CommandString4, sizeof(CommandString4) - 1},
	{CommandString5, sizeof(CommandString5) - 1}
};

#define GOT_ALL_RADIO_INFO(S)      \
    ((S)->firmware_version.c[0] && \
     (S)->battery_voltage.c[0]  && \
     memcmp(&(S)->true_dev_addr, zero_address.c, sizeof(zero_address)))

static const char hextable[16] = "0123456789ABCDEF";

static const MetricomAddress zero_address;
static const MetricomAddress broadcast_address =
    { {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF} };

static const MetricomKey SIP0Key = { "SIP0" };
static const MetricomKey ARP0Key = { "ARP0" };
static const MetricomKey ATR_Key = { "ATR " };
static const MetricomKey ACK_Key = { "ACK_" };
static const MetricomKey INF_Key = { "INF_" };
static const MetricomKey ERR_Key = { "ERR_" };

static const long MaxARPInterval = 60 * HZ;	/* One minute */

/*
 * Maximum Starmode packet length is 1183 bytes. Allowing 4 bytes for
 * protocol key, 4 bytes for checksum, one byte for CR, and 65/64 expansion
 * for STRIP encoding, that translates to a maximum payload MTU of 1155.
 * Note: A standard NFS 1K data packet is a total of 0x480 (1152) bytes
 * long, including IP header, UDP header, and NFS header. Setting the STRIP
 * MTU to 1152 allows us to send default sized NFS packets without fragmentation.
 */
static const unsigned short MAX_SEND_MTU = 1152;
static const unsigned short MAX_RECV_MTU = 1500;	/* Hoping for Ethernet sized packets in the future! */
static const unsigned short DEFAULT_STRIP_MTU = 1152;
static const int STRIP_MAGIC = 0x5303;
static const long LongTime = 0x7FFFFFFF;

/************************************************************************/
/* Global variables							*/

static LIST_HEAD(strip_list);
static DEFINE_SPINLOCK(strip_lock);

/************************************************************************/
/* Macros								*/

/* Returns TRUE if text T begins with prefix P */
#define has_prefix(T,L,P) (((L) >= sizeof(P)-1) && !strncmp((T), (P), sizeof(P)-1))

/* Returns TRUE if text T of length L is equal to string S */
#define text_equal(T,L,S) (((L) == sizeof(S)-1) && !strncmp((T), (S), sizeof(S)-1))

#define READHEX(X) ((X)>='0' && (X)<='9' ? (X)-'0' :      \
                    (X)>='a' && (X)<='f' ? (X)-'a'+10 :   \
                    (X)>='A' && (X)<='F' ? (X)-'A'+10 : 0 )

#define READHEX16(X) ((__u16)(READHEX(X)))

#define READDEC(X) ((X)>='0' && (X)<='9' ? (X)-'0' : 0)

#define ARRAY_END(X) (&((X)[ARRAY_SIZE(X)]))

#define JIFFIE_TO_SEC(X) ((X) / HZ)


/************************************************************************/
/* Utility routines							*/

static int arp_query(unsigned char *haddr, u32 paddr,
		     struct net_device *dev)
{
	struct neighbour *neighbor_entry;
	int ret = 0;

	neighbor_entry = neigh_lookup(&arp_tbl, &paddr, dev);

	if (neighbor_entry != NULL) {
		neighbor_entry->used = jiffies;
		if (neighbor_entry->nud_state & NUD_VALID) {
			memcpy(haddr, neighbor_entry->ha, dev->addr_len);
			ret = 1;
		}
		neigh_release(neighbor_entry);
	}
	return ret;
}

static void DumpData(char *msg, struct strip *strip_info, __u8 * ptr,
		     __u8 * end)
{
	static const int MAX_DumpData = 80;
	__u8 pkt_text[MAX_DumpData], *p = pkt_text;

	*p++ = '\"';

	while (ptr < end && p < &pkt_text[MAX_DumpData - 4]) {
		if (*ptr == '\\') {
			*p++ = '\\';
			*p++ = '\\';
		} else {
			if (*ptr >= 32 && *ptr <= 126) {
				*p++ = *ptr;
			} else {
				sprintf(p, "\\%02X", *ptr);
				p += 3;
			}
		}
		ptr++;
	}

	if (ptr == end)
		*p++ = '\"';
	*p++ = 0;

	printk(KERN_INFO "%s: %-13s%s\n", strip_info->dev->name, msg, pkt_text);
}


/************************************************************************/
/* Byte stuffing/unstuffing routines					*/

/* Stuffing scheme:
 * 00    Unused (reserved character)
 * 01-3F Run of 2-64 different characters
 * 40-7F Run of 1-64 different characters plus a single zero at the end
 * 80-BF Run of 1-64 of the same character
 * C0-FF Run of 1-64 zeroes (ASCII 0)
 */

typedef enum {
	Stuff_Diff = 0x00,
	Stuff_DiffZero = 0x40,
	Stuff_Same = 0x80,
	Stuff_Zero = 0xC0,
	Stuff_NoCode = 0xFF,	/* Special code, meaning no code selected */

	Stuff_CodeMask = 0xC0,
	Stuff_CountMask = 0x3F,
	Stuff_MaxCount = 0x3F,
	Stuff_Magic = 0x0D	/* The value we are eliminating */
} StuffingCode;

/* StuffData encodes the data starting at "src" for "length" bytes.
 * It writes it to the buffer pointed to by "dst" (which must be at least
 * as long as 1 + 65/64 of the input length). The output may be up to 1.6%
 * larger than the input for pathological input, but will usually be smaller.
 * StuffData returns the new value of the dst pointer as its result.
 * "code_ptr_ptr" points to a "__u8 *" which is used to hold encoding state
 * between calls, allowing an encoded packet to be incrementally built up
 * from small parts. On the first call, the "__u8 *" pointed to should be
 * initialized to NULL; between subsequent calls the calling routine should
 * leave the value alone and simply pass it back unchanged so that the
 * encoder can recover its current state.
 */

#define StuffData_FinishBlock(X) \
(*code_ptr = (X) ^ Stuff_Magic, code = Stuff_NoCode)

static __u8 *StuffData(__u8 * src, __u32 length, __u8 * dst,
		       __u8 ** code_ptr_ptr)
{
	__u8 *end = src + length;
	__u8 *code_ptr = *code_ptr_ptr;
	__u8 code = Stuff_NoCode, count = 0;

	if (!length)
		return (dst);

	if (code_ptr) {
		/*
		 * Recover state from last call, if applicable
		 */
		code = (*code_ptr ^ Stuff_Magic) & Stuff_CodeMask;
		count = (*code_ptr ^ Stuff_Magic) & Stuff_CountMask;
	}

	while (src < end) {
		switch (code) {
			/* Stuff_NoCode: If no current code, select one */
		case Stuff_NoCode:
			/* Record where we're going to put this code */
			code_ptr = dst++;
			count = 0;	/* Reset the count (zero means one instance) */
			/* Tentatively start a new block */
			if (*src == 0) {
				code = Stuff_Zero;
				src++;
			} else {
				code = Stuff_Same;
				*dst++ = *src++ ^ Stuff_Magic;
			}
			/* Note: We optimistically assume run of same -- */
			/* which will be fixed later in Stuff_Same */
			/* if it turns out not to be true. */
			break;

			/* Stuff_Zero: We already have at least one zero encoded */
		case Stuff_Zero:
			/* If another zero, count it, else finish this code block */
			if (*src == 0) {
				count++;
				src++;
			} else {
				StuffData_FinishBlock(Stuff_Zero + count);
			}
			break;

			/* Stuff_Same: We already have at least one byte encoded */
		case Stuff_Same:
			/* If another one the same, count it */
			if ((*src ^ Stuff_Magic) == code_ptr[1]) {
				count++;
				src++;
				break;
			}
			/* else, this byte does not match this block. */
			/* If we already have two or more bytes encoded, finish this code block */
			if (count) {
				StuffData_FinishBlock(Stuff_Same + count);
				break;
			}
			/* else, we only have one so far, so switch to Stuff_Diff code */
			code = Stuff_Diff;
			/* and fall through to Stuff_Diff case below
			 * Note cunning cleverness here: case Stuff_Diff compares 
			 * the current character with the previous two to see if it
			 * has a run of three the same. Won't this be an error if
			 * there aren't two previous characters stored to compare with?
			 * No. Because we know the current character is *not* the same
			 * as the previous one, the first test below will necessarily
			 * fail and the send half of the "if" won't be executed.
			 */

			/* Stuff_Diff: We have at least two *different* bytes encoded */
		case Stuff_Diff:
			/* If this is a zero, must encode a Stuff_DiffZero, and begin a new block */
			if (*src == 0) {
				StuffData_FinishBlock(Stuff_DiffZero +
						      count);
			}
			/* else, if we have three in a row, it is worth starting a Stuff_Same block */
			else if ((*src ^ Stuff_Magic) == dst[-1]
				 && dst[-1] == dst[-2]) {
				/* Back off the last two characters we encoded */
				code += count - 2;
				/* Note: "Stuff_Diff + 0" is an illegal code */
				if (code == Stuff_Diff + 0) {
					code = Stuff_Same + 0;
				}
				StuffData_FinishBlock(code);
				code_ptr = dst - 2;
				/* dst[-1] already holds the correct value */
				count = 2;	/* 2 means three bytes encoded */
				code = Stuff_Same;
			}
			/* else, another different byte, so add it to the block */
			else {
				*dst++ = *src ^ Stuff_Magic;
				count++;
			}
			src++;	/* Consume the byte */
			break;
		}
		if (count == Stuff_MaxCount) {
			StuffData_FinishBlock(code + count);
		}
	}
	if (code == Stuff_NoCode) {
		*code_ptr_ptr = NULL;
	} else {
		*code_ptr_ptr = code_ptr;
		StuffData_FinishBlock(code + count);
	}
	return (dst);
}

/*
 * UnStuffData decodes the data at "src", up to (but not including) "end".
 * It writes the decoded data into the buffer pointed to by "dst", up to a
 * maximum of "dst_length", and returns the new value of "src" so that a
 * follow-on call can read more data, continuing from where the first left off.
 * 
 * There are three types of results:
 * 1. The source data runs out before extracting "dst_length" bytes:
 *    UnStuffData returns NULL to indicate failure.
 * 2. The source data produces exactly "dst_length" bytes:
 *    UnStuffData returns new_src = end to indicate that all bytes were consumed.
 * 3. "dst_length" bytes are extracted, with more remaining.
 *    UnStuffData returns new_src < end to indicate that there are more bytes
 *    to be read.
 * 
 * Note: The decoding may be destructive, in that it may alter the source
 * data in the process of decoding it (this is necessary to allow a follow-on
 * call to resume correctly).
 */

static __u8 *UnStuffData(__u8 * src, __u8 * end, __u8 * dst,
			 __u32 dst_length)
{
	__u8 *dst_end = dst + dst_length;
	/* Sanity check */
	if (!src || !end || !dst || !dst_length)
		return (NULL);
	while (src < end && dst < dst_end) {
		int count = (*src ^ Stuff_Magic) & Stuff_CountMask;
		switch ((*src ^ Stuff_Magic) & Stuff_CodeMask) {
		case Stuff_Diff:
			if (src + 1 + count >= end)
				return (NULL);
			do {
				*dst++ = *++src ^ Stuff_Magic;
			}
			while (--count >= 0 && dst < dst_end);
			if (count < 0)
				src += 1;
			else {
				if (count == 0)
					*src = Stuff_Same ^ Stuff_Magic;
				else
					*src =
					    (Stuff_Diff +
					     count) ^ Stuff_Magic;
			}
			break;
		case Stuff_DiffZero:
			if (src + 1 + count >= end)
				return (NULL);
			do {
				*dst++ = *++src ^ Stuff_Magic;
			}
			while (--count >= 0 && dst < dst_end);
			if (count < 0)
				*src = Stuff_Zero ^ Stuff_Magic;
			else
				*src =
				    (Stuff_DiffZero + count) ^ Stuff_Magic;
			break;
		case Stuff_Same:
			if (src + 1 >= end)
				return (NULL);
			do {
				*dst++ = src[1] ^ Stuff_Magic;
			}
			while (--count >= 0 && dst < dst_end);
			if (count < 0)
				src += 2;
			else
				*src = (Stuff_Same + count) ^ Stuff_Magic;
			break;
		case Stuff_Zero:
			do {
				*dst++ = 0;
			}
			while (--count >= 0 && dst < dst_end);
			if (count < 0)
				src += 1;
			else
				*src = (Stuff_Zero + count) ^ Stuff_Magic;
			break;
		}
	}
	if (dst < dst_end)
		return (NULL);
	else
		return (src);
}


/************************************************************************/
/* General routines for STRIP						*/

/*
 * set_baud sets the baud rate to the rate defined by baudcode
 */
static void set_baud(struct tty_struct *tty, speed_t baudrate)
{
	struct ktermios old_termios;

	mutex_lock(&tty->termios_mutex);
	old_termios =*(tty->termios);
	tty_encode_baud_rate(tty, baudrate, baudrate);
	tty->ops->set_termios(tty, &old_termios);
	mutex_unlock(&tty->termios_mutex);
}

/*
 * Convert a string to a Metricom Address.
 */

#define IS_RADIO_ADDRESS(p) (                                                 \
  isdigit((p)[0]) && isdigit((p)[1]) && isdigit((p)[2]) && isdigit((p)[3]) && \
  (p)[4] == '-' &&                                                            \
  isdigit((p)[5]) && isdigit((p)[6]) && isdigit((p)[7]) && isdigit((p)[8])    )

static int string_to_radio_address(MetricomAddress * addr, __u8 * p)
{
	if (!IS_RADIO_ADDRESS(p))
		return (1);
	addr->c[0] = 0;
	addr->c[1] = 0;
	addr->c[2] = READHEX(p[0]) << 4 | READHEX(p[1]);
	addr->c[3] = READHEX(p[2]) << 4 | READHEX(p[3]);
	addr->c[4] = READHEX(p[5]) << 4 | READHEX(p[6]);
	addr->c[5] = READHEX(p[7]) << 4 | READHEX(p[8]);
	return (0);
}

/*
 * Convert a Metricom Address to a string.
 */

static __u8 *radio_address_to_string(const MetricomAddress * addr,
				     MetricomAddressString * p)
{
	sprintf(p->c, "%02X%02X-%02X%02X", addr->c[2], addr->c[3],
		addr->c[4], addr->c[5]);
	return (p->c);
}

/*
 * Note: Must make sure sx_size is big enough to receive a stuffed
 * MAX_RECV_MTU packet. Additionally, we also want to ensure that it's
 * big enough to receive a large radio neighbour list (currently 4K).
 */

static int allocate_buffers(struct strip *strip_info, int mtu)
{
	struct net_device *dev = strip_info->dev;
	int sx_size = max_t(int, STRIP_ENCAP_SIZE(MAX_RECV_MTU), 4096);
	int tx_size = STRIP_ENCAP_SIZE(mtu) + MaxCommandStringLength;
	__u8 *r = kmalloc(MAX_RECV_MTU, GFP_ATOMIC);
	__u8 *s = kmalloc(sx_size, GFP_ATOMIC);
	__u8 *t = kmalloc(tx_size, GFP_ATOMIC);
	if (r && s && t) {
		strip_info->rx_buff = r;
		strip_info->sx_buff = s;
		strip_info->tx_buff = t;
		strip_info->sx_size = sx_size;
		strip_info->tx_size = tx_size;
		strip_info->mtu = dev->mtu = mtu;
		return (1);
	}
	kfree(r);
	kfree(s);
	kfree(t);
	return (0);
}

/*
 * MTU has been changed by the IP layer. 
 * We could be in
 * an upcall from the tty driver, or in an ip packet queue.
 */
static int strip_change_mtu(struct net_device *dev, int new_mtu)
{
	struct strip *strip_info = netdev_priv(dev);
	int old_mtu = strip_info->mtu;
	unsigned char *orbuff = strip_info->rx_buff;
	unsigned char *osbuff = strip_info->sx_buff;
	unsigned char *otbuff = strip_info->tx_buff;

	if (new_mtu > MAX_SEND_MTU) {
		printk(KERN_ERR
		       "%s: MTU exceeds maximum allowable (%d), MTU change cancelled.\n",
		       strip_info->dev->name, MAX_SEND_MTU);
		return -EINVAL;
	}

	spin_lock_bh(&strip_lock);
	if (!allocate_buffers(strip_info, new_mtu)) {
		printk(KERN_ERR "%s: unable to grow strip buffers, MTU change cancelled.\n",
		       strip_info->dev->name);
		spin_unlock_bh(&strip_lock);
		return -ENOMEM;
	}

	if (strip_info->sx_count) {
		if (strip_info->sx_count <= strip_info->sx_size)
			memcpy(strip_info->sx_buff, osbuff,
			       strip_info->sx_count);
		else {
			strip_info->discard = strip_info->sx_count;
			strip_info->rx_over_errors++;
		}
	}

	if (strip_info->tx_left) {
		if (strip_info->tx_left <= strip_info->tx_size)
			memcpy(strip_info->tx_buff, strip_info->tx_head,
			       strip_info->tx_left);
		else {
			strip_info->tx_left = 0;
			strip_info->tx_dropped++;
		}
	}
	strip_info->tx_head = strip_info->tx_buff;
	spin_unlock_bh(&strip_lock);

	printk(KERN_NOTICE "%s: strip MTU changed fom %d to %d.\n",
	       strip_info->dev->name, old_mtu, strip_info->mtu);

	kfree(orbuff);
	kfree(osbuff);
	kfree(otbuff);
	return 0;
}

static void strip_unlock(struct strip *strip_info)
{
	/*
	 * Set the timer to go off in one second.
	 */
	strip_info->idle_timer.expires = jiffies + 1 * HZ;
	add_timer(&strip_info->idle_timer);
	netif_wake_queue(strip_info->dev);
}



/*
 * If the time is in the near future, time_delta prints the number of
 * seconds to go into the buffer and returns the address of the buffer.
 * If the time is not in the near future, it returns the address of the
 * string "Not scheduled" The buffer must be long enough to contain the
 * ascii representation of the number plus 9 charactes for the " seconds"
 * and the null character.
 */
#ifdef CONFIG_PROC_FS
static char *time_delta(char buffer[], long time)
{
	time -= jiffies;
	if (time > LongTime / 2)
		return ("Not scheduled");
	if (time < 0)
		time = 0;	/* Don't print negative times */
	sprintf(buffer, "%ld seconds", time / HZ);
	return (buffer);
}

/* get Nth element of the linked list */
static struct strip *strip_get_idx(loff_t pos) 
{
	struct strip *str;
	int i = 0;

	list_for_each_entry_rcu(str, &strip_list, list) {
		if (pos == i)
			return str;
		++i;
	}
	return NULL;
}

static void *strip_seq_start(struct seq_file *seq, loff_t *pos)
{
	rcu_read_lock();
	return *pos ? strip_get_idx(*pos - 1) : SEQ_START_TOKEN;
}

static void *strip_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct list_head *l;
	struct strip *s;

	++*pos;
	if (v == SEQ_START_TOKEN)
		return strip_get_idx(1);

	s = v;
	l = &s->list;
	list_for_each_continue_rcu(l, &strip_list) {
		return list_entry(l, struct strip, list);
	}
	return NULL;
}

static void strip_seq_stop(struct seq_file *seq, void *v)
{
	rcu_read_unlock();
}

static void strip_seq_neighbours(struct seq_file *seq,
			   const MetricomNodeTable * table,
			   const char *title)
{
	/* We wrap this in a do/while loop, so if the table changes */
	/* while we're reading it, we just go around and try again. */
	struct timeval t;

	do {
		int i;
		t = table->timestamp;
		if (table->num_nodes)
			seq_printf(seq, "\n %s\n", title);
		for (i = 0; i < table->num_nodes; i++) {
			MetricomNode node;

			spin_lock_bh(&strip_lock);
			node = table->node[i];
			spin_unlock_bh(&strip_lock);
			seq_printf(seq, "  %s\n", node.c);
		}
	} while (table->timestamp.tv_sec != t.tv_sec
		 || table->timestamp.tv_usec != t.tv_usec);
}

/*
 * This function prints radio status information via the seq_file
 * interface.  The interface takes care of buffer size and over
 * run issues. 
 *
 * The buffer in seq_file is PAGESIZE (4K) 
 * so this routine should never print more or it will get truncated.
 * With the maximum of 32 portables and 32 poletops
 * reported, the routine outputs 3107 bytes into the buffer.
 */
static void strip_seq_status_info(struct seq_file *seq, 
				  const struct strip *strip_info)
{
	char temp[32];
	MetricomAddressString addr_string;

	/* First, we must copy all of our data to a safe place, */
	/* in case a serial interrupt comes in and changes it.  */
	int tx_left = strip_info->tx_left;
	unsigned long rx_average_pps = strip_info->rx_average_pps;
	unsigned long tx_average_pps = strip_info->tx_average_pps;
	unsigned long sx_average_pps = strip_info->sx_average_pps;
	int working = strip_info->working;
	int firmware_level = strip_info->firmware_level;
	long watchdog_doprobe = strip_info->watchdog_doprobe;
	long watchdog_doreset = strip_info->watchdog_doreset;
	long gratuitous_arp = strip_info->gratuitous_arp;
	long arp_interval = strip_info->arp_interval;
	FirmwareVersion firmware_version = strip_info->firmware_version;
	SerialNumber serial_number = strip_info->serial_number;
	BatteryVoltage battery_voltage = strip_info->battery_voltage;
	char *if_name = strip_info->dev->name;
	MetricomAddress true_dev_addr = strip_info->true_dev_addr;
	MetricomAddress dev_dev_addr =
	    *(MetricomAddress *) strip_info->dev->dev_addr;
	int manual_dev_addr = strip_info->manual_dev_addr;
#ifdef EXT_COUNTERS
	unsigned long rx_bytes = strip_info->rx_bytes;
	unsigned long tx_bytes = strip_info->tx_bytes;
	unsigned long rx_rbytes = strip_info->rx_rbytes;
	unsigned long tx_rbytes = strip_info->tx_rbytes;
	unsigned long rx_sbytes = strip_info->rx_sbytes;
	unsigned long tx_sbytes = strip_info->tx_sbytes;
	unsigned long rx_ebytes = strip_info->rx_ebytes;
	unsigned long tx_ebytes = strip_info->tx_ebytes;
#endif

	seq_printf(seq, "\nInterface name\t\t%s\n", if_name);
	seq_printf(seq, " Radio working:\t\t%s\n", working ? "Yes" : "No");
	radio_address_to_string(&true_dev_addr, &addr_string);
	seq_printf(seq, " Radio address:\t\t%s\n", addr_string.c);
	if (manual_dev_addr) {
		radio_address_to_string(&dev_dev_addr, &addr_string);
		seq_printf(seq, " Device address:\t%s\n", addr_string.c);
	}
	seq_printf(seq, " Firmware version:\t%s", !working ? "Unknown" :
		     !firmware_level ? "Should be upgraded" :
		     firmware_version.c);
	if (firmware_level >= ChecksummedMessages)
		seq_printf(seq, " (Checksums Enabled)");
	seq_printf(seq, "\n");
	seq_printf(seq, " Serial number:\t\t%s\n", serial_number.c);
	seq_printf(seq, " Battery voltage:\t%s\n", battery_voltage.c);
	seq_printf(seq, " Transmit queue (bytes):%d\n", tx_left);
	seq_printf(seq, " Receive packet rate:   %ld packets per second\n",
		     rx_average_pps / 8);
	seq_printf(seq, " Transmit packet rate:  %ld packets per second\n",
		     tx_average_pps / 8);
	seq_printf(seq, " Sent packet rate:      %ld packets per second\n",
		     sx_average_pps / 8);
	seq_printf(seq, " Next watchdog probe:\t%s\n",
		     time_delta(temp, watchdog_doprobe));
	seq_printf(seq, " Next watchdog reset:\t%s\n",
		     time_delta(temp, watchdog_doreset));
	seq_printf(seq, " Next gratuitous ARP:\t");

	if (!memcmp
	    (strip_info->dev->dev_addr, zero_address.c,
	     sizeof(zero_address)))
		seq_printf(seq, "Disabled\n");
	else {
		seq_printf(seq, "%s\n", time_delta(temp, gratuitous_arp));
		seq_printf(seq, " Next ARP interval:\t%ld seconds\n",
			     JIFFIE_TO_SEC(arp_interval));
	}

	if (working) {
#ifdef EXT_COUNTERS
		seq_printf(seq, "\n");
		seq_printf(seq,
			     " Total bytes:         \trx:\t%lu\ttx:\t%lu\n",
			     rx_bytes, tx_bytes);
		seq_printf(seq,
			     "  thru radio:         \trx:\t%lu\ttx:\t%lu\n",
			     rx_rbytes, tx_rbytes);
		seq_printf(seq,
			     "  thru serial port:   \trx:\t%lu\ttx:\t%lu\n",
			     rx_sbytes, tx_sbytes);
		seq_printf(seq,
			     " Total stat/err bytes:\trx:\t%lu\ttx:\t%lu\n",
			     rx_ebytes, tx_ebytes);
#endif
		strip_seq_neighbours(seq, &strip_info->poletops,
					"Poletops:");
		strip_seq_neighbours(seq, &strip_info->portables,
					"Portables:");
	}
}

/*
 * This function is exports status information from the STRIP driver through
 * the /proc file system.
 */
static int strip_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_printf(seq, "strip_version: %s\n", StripVersion);
	else
		strip_seq_status_info(seq, (const struct strip *)v);
	return 0;
}


static struct seq_operations strip_seq_ops = {
	.start = strip_seq_start,
	.next  = strip_seq_next,
	.stop  = strip_seq_stop,
	.show  = strip_seq_show,
};

static int strip_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &strip_seq_ops);
}

static const struct file_operations strip_seq_fops = {
	.owner	 = THIS_MODULE,
	.open    = strip_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};
#endif



/************************************************************************/
/* Sending routines							*/

static void ResetRadio(struct strip *strip_info)
{
	struct tty_struct *tty = strip_info->tty;
	static const char init[] = "ate0q1dt**starmode\r**";
	StringDescriptor s = { init, sizeof(init) - 1 };

	/* 
	 * If the radio isn't working anymore,
	 * we should clear the old status information.
	 */
	if (strip_info->working) {
		printk(KERN_INFO "%s: No response: Resetting radio.\n",
		       strip_info->dev->name);
		strip_info->firmware_version.c[0] = '\0';
		strip_info->serial_number.c[0] = '\0';
		strip_info->battery_voltage.c[0] = '\0';
		strip_info->portables.num_nodes = 0;
		do_gettimeofday(&strip_info->portables.timestamp);
		strip_info->poletops.num_nodes = 0;
		do_gettimeofday(&strip_info->poletops.timestamp);
	}

	strip_info->pps_timer = jiffies;
	strip_info->rx_pps_count = 0;
	strip_info->tx_pps_count = 0;
	strip_info->sx_pps_count = 0;
	strip_info->rx_average_pps = 0;
	strip_info->tx_average_pps = 0;
	strip_info->sx_average_pps = 0;

	/* Mark radio address as unknown */
	*(MetricomAddress *) & strip_info->true_dev_addr = zero_address;
	if (!strip_info->manual_dev_addr)
		*(MetricomAddress *) strip_info->dev->dev_addr =
		    zero_address;
	strip_info->working = FALSE;
	strip_info->firmware_level = NoStructure;
	strip_info->next_command = CompatibilityCommand;
	strip_info->watchdog_doprobe = jiffies + 10 * HZ;
	strip_info->watchdog_doreset = jiffies + 1 * HZ;

	/* If the user has selected a baud rate above 38.4 see what magic we have to do */
	if (strip_info->user_baud > 38400) {
		/*
		 * Subtle stuff: Pay attention :-)
		 * If the serial port is currently at the user's selected (>38.4) rate,
		 * then we temporarily switch to 19.2 and issue the ATS304 command
		 * to tell the radio to switch to the user's selected rate.
		 * If the serial port is not currently at that rate, that means we just
		 * issued the ATS304 command last time through, so this time we restore
		 * the user's selected rate and issue the normal starmode reset string.
		 */
		if (strip_info->user_baud == tty_get_baud_rate(tty)) {
			static const char b0[] = "ate0q1s304=57600\r";
			static const char b1[] = "ate0q1s304=115200\r";
			static const StringDescriptor baudstring[2] =
			    { {b0, sizeof(b0) - 1}
			, {b1, sizeof(b1) - 1}
			};
			set_baud(tty, 19200);
			if (strip_info->user_baud == 57600)
				s = baudstring[0];
			else if (strip_info->user_baud == 115200)
				s = baudstring[1];
			else
				s = baudstring[1];	/* For now */
		} else
			set_baud(tty, strip_info->user_baud);
	}

	tty->ops->write(tty, s.string, s.length);
#ifdef EXT_COUNTERS
	strip_info->tx_ebytes += s.length;
#endif
}

/*
 * Called by the driver when there's room for more data.  If we have
 * more packets to send, we send them here.
 */

static void strip_write_some_more(struct tty_struct *tty)
{
	struct strip *strip_info = (struct strip *) tty->disc_data;

	/* First make sure we're connected. */
	if (!strip_info || strip_info->magic != STRIP_MAGIC ||
	    !netif_running(strip_info->dev))
		return;

	if (strip_info->tx_left > 0) {
		int num_written =
		    tty->ops->write(tty, strip_info->tx_head,
				      strip_info->tx_left);
		strip_info->tx_left -= num_written;
		strip_info->tx_head += num_written;
#ifdef EXT_COUNTERS
		strip_info->tx_sbytes += num_written;
#endif
	} else {		/* Else start transmission of another packet */

		tty->flags &= ~(1 << TTY_DO_WRITE_WAKEUP);
		strip_unlock(strip_info);
	}
}

static __u8 *add_checksum(__u8 * buffer, __u8 * end)
{
	__u16 sum = 0;
	__u8 *p = buffer;
	while (p < end)
		sum += *p++;
	end[3] = hextable[sum & 0xF];
	sum >>= 4;
	end[2] = hextable[sum & 0xF];
	sum >>= 4;
	end[1] = hextable[sum & 0xF];
	sum >>= 4;
	end[0] = hextable[sum & 0xF];
	return (end + 4);
}

static unsigned char *strip_make_packet(unsigned char *buffer,
					struct strip *strip_info,
					struct sk_buff *skb)
{
	__u8 *ptr = buffer;
	__u8 *stuffstate = NULL;
	STRIP_Header *header = (STRIP_Header *) skb->data;
	MetricomAddress haddr = header->dst_addr;
	int len = skb->len - sizeof(STRIP_Header);
	MetricomKey key;

	/*HexDump("strip_make_packet", strip_info, skb->data, skb->data + skb->len); */

	if (header->protocol == htons(ETH_P_IP))
		key = SIP0Key;
	else if (header->protocol == htons(ETH_P_ARP))
		key = ARP0Key;
	else {
		printk(KERN_ERR
		       "%s: strip_make_packet: Unknown packet type 0x%04X\n",
		       strip_info->dev->name, ntohs(header->protocol));
		return (NULL);
	}

	if (len > strip_info->mtu) {
		printk(KERN_ERR
		       "%s: Dropping oversized transmit packet: %d bytes\n",
		       strip_info->dev->name, len);
		return (NULL);
	}

	/*
	 * If we're sending to ourselves, discard the packet.
	 * (Metricom radios choke if they try to send a packet to their own address.)
	 */
	if (!memcmp(haddr.c, strip_info->true_dev_addr.c, sizeof(haddr))) {
		printk(KERN_ERR "%s: Dropping packet addressed to self\n",
		       strip_info->dev->name);
		return (NULL);
	}

	/*
	 * If this is a broadcast packet, send it to our designated Metricom
	 * 'broadcast hub' radio (First byte of address being 0xFF means broadcast)
	 */
	if (haddr.c[0] == 0xFF) {
		__be32 brd = 0;
		struct in_device *in_dev;

		rcu_read_lock();
		in_dev = __in_dev_get_rcu(strip_info->dev);
		if (in_dev == NULL) {
			rcu_read_unlock();
			return NULL;
		}
		if (in_dev->ifa_list)
			brd = in_dev->ifa_list->ifa_broadcast;
		rcu_read_unlock();

		/* arp_query returns 1 if it succeeds in looking up the address, 0 if it fails */
		if (!arp_query(haddr.c, brd, strip_info->dev)) {
			printk(KERN_ERR
			       "%s: Unable to send packet (no broadcast hub configured)\n",
			       strip_info->dev->name);
			return (NULL);
		}
		/*
		 * If we are the broadcast hub, don't bother sending to ourselves.
		 * (Metricom radios choke if they try to send a packet to their own address.)
		 */
		if (!memcmp
		    (haddr.c, strip_info->true_dev_addr.c, sizeof(haddr)))
			return (NULL);
	}

	*ptr++ = 0x0D;
	*ptr++ = '*';
	*ptr++ = hextable[haddr.c[2] >> 4];
	*ptr++ = hextable[haddr.c[2] & 0xF];
	*ptr++ = hextable[haddr.c[3] >> 4];
	*ptr++ = hextable[haddr.c[3] & 0xF];
	*ptr++ = '-';
	*ptr++ = hextable[haddr.c[4] >> 4];
	*ptr++ = hextable[haddr.c[4] & 0xF];
	*ptr++ = hextable[haddr.c[5] >> 4];
	*ptr++ = hextable[haddr.c[5] & 0xF];
	*ptr++ = '*';
	*ptr++ = key.c[0];
	*ptr++ = key.c[1];
	*ptr++ = key.c[2];
	*ptr++ = key.c[3];

	ptr =
	    StuffData(skb->data + sizeof(STRIP_Header), len, ptr,
		      &stuffstate);

	if (strip_info->firmware_level >= ChecksummedMessages)
		ptr = add_checksum(buffer + 1, ptr);

	*ptr++ = 0x0D;
	return (ptr);
}

static void strip_send(struct strip *strip_info, struct sk_buff *skb)
{
	MetricomAddress haddr;
	unsigned char *ptr = strip_info->tx_buff;
	int doreset = (long) jiffies - strip_info->watchdog_doreset >= 0;
	int doprobe = (long) jiffies - strip_info->watchdog_doprobe >= 0
	    && !doreset;
	__be32 addr, brd;

	/*
	 * 1. If we have a packet, encapsulate it and put it in the buffer
	 */
	if (skb) {
		char *newptr = strip_make_packet(ptr, strip_info, skb);
		strip_info->tx_pps_count++;
		if (!newptr)
			strip_info->tx_dropped++;
		else {
			ptr = newptr;
			strip_info->sx_pps_count++;
			strip_info->tx_packets++;	/* Count another successful packet */
#ifdef EXT_COUNTERS
			strip_info->tx_bytes += skb->len;
			strip_info->tx_rbytes += ptr - strip_info->tx_buff;
#endif
			/*DumpData("Sending:", strip_info, strip_info->tx_buff, ptr); */
			/*HexDump("Sending", strip_info, strip_info->tx_buff, ptr); */
		}
	}

	/*
	 * 2. If it is time for another tickle, tack it on, after the packet
	 */
	if (doprobe) {
		StringDescriptor ts = CommandString[strip_info->next_command];
#if TICKLE_TIMERS
		{
			struct timeval tv;
			do_gettimeofday(&tv);
			printk(KERN_INFO "**** Sending tickle string %d      at %02d.%06d\n",
			       strip_info->next_command, tv.tv_sec % 100,
			       tv.tv_usec);
		}
#endif
		if (ptr == strip_info->tx_buff)
			*ptr++ = 0x0D;

		*ptr++ = '*';	/* First send "**" to provoke an error message */
		*ptr++ = '*';

		/* Then add the command */
		memcpy(ptr, ts.string, ts.length);

		/* Add a checksum ? */
		if (strip_info->firmware_level < ChecksummedMessages)
			ptr += ts.length;
		else
			ptr = add_checksum(ptr, ptr + ts.length);

		*ptr++ = 0x0D;	/* Terminate the command with a <CR> */

		/* Cycle to next periodic command? */
		if (strip_info->firmware_level >= StructuredMessages)
			if (++strip_info->next_command >=
			    ARRAY_SIZE(CommandString))
				strip_info->next_command = 0;
#ifdef EXT_COUNTERS
		strip_info->tx_ebytes += ts.length;
#endif
		strip_info->watchdog_doprobe = jiffies + 10 * HZ;
		strip_info->watchdog_doreset = jiffies + 1 * HZ;
		/*printk(KERN_INFO "%s: Routine radio test.\n", strip_info->dev->name); */
	}

	/*
	 * 3. Set up the strip_info ready to send the data (if any).
	 */
	strip_info->tx_head = strip_info->tx_buff;
	strip_info->tx_left = ptr - strip_info->tx_buff;
	strip_info->tty->flags |= (1 << TTY_DO_WRITE_WAKEUP);

	/*
	 * 4. Debugging check to make sure we're not overflowing the buffer.
	 */
	if (strip_info->tx_size - strip_info->tx_left < 20)
		printk(KERN_ERR "%s: Sending%5d bytes;%5d bytes free.\n",
		       strip_info->dev->name, strip_info->tx_left,
		       strip_info->tx_size - strip_info->tx_left);

	/*
	 * 5. If watchdog has expired, reset the radio. Note: if there's data waiting in
	 * the buffer, strip_write_some_more will send it after the reset has finished
	 */
	if (doreset) {
		ResetRadio(strip_info);
		return;
	}

	if (1) {
		struct in_device *in_dev;

		brd = addr = 0;
		rcu_read_lock();
		in_dev = __in_dev_get_rcu(strip_info->dev);
		if (in_dev) {
			if (in_dev->ifa_list) {
				brd = in_dev->ifa_list->ifa_broadcast;
				addr = in_dev->ifa_list->ifa_local;
			}
		}
		rcu_read_unlock();
	}


	/*
	 * 6. If it is time for a periodic ARP, queue one up to be sent.
	 * We only do this if:
	 *  1. The radio is working
	 *  2. It's time to send another periodic ARP
	 *  3. We really know what our address is (and it is not manually set to zero)
	 *  4. We have a designated broadcast address configured
	 * If we queue up an ARP packet when we don't have a designated broadcast
	 * address configured, then the packet will just have to be discarded in
	 * strip_make_packet. This is not fatal, but it causes misleading information
	 * to be displayed in tcpdump. tcpdump will report that periodic APRs are
	 * being sent, when in fact they are not, because they are all being dropped
	 * in the strip_make_packet routine.
	 */
	if (strip_info->working
	    && (long) jiffies - strip_info->gratuitous_arp >= 0
	    && memcmp(strip_info->dev->dev_addr, zero_address.c,
		      sizeof(zero_address))
	    && arp_query(haddr.c, brd, strip_info->dev)) {
		/*printk(KERN_INFO "%s: Sending gratuitous ARP with interval %ld\n",
		   strip_info->dev->name, strip_info->arp_interval / HZ); */
		strip_info->gratuitous_arp =
		    jiffies + strip_info->arp_interval;
		strip_info->arp_interval *= 2;
		if (strip_info->arp_interval > MaxARPInterval)
			strip_info->arp_interval = MaxARPInterval;
		if (addr)
			arp_send(ARPOP_REPLY, ETH_P_ARP, addr,	/* Target address of ARP packet is our address */
				 strip_info->dev,	/* Device to send packet on */
				 addr,	/* Source IP address this ARP packet comes from */
				 NULL,	/* Destination HW address is NULL (broadcast it) */
				 strip_info->dev->dev_addr,	/* Source HW address is our HW address */
				 strip_info->dev->dev_addr);	/* Target HW address is our HW address (redundant) */
	}

	/*
	 * 7. All ready. Start the transmission
	 */
	strip_write_some_more(strip_info->tty);
}

/* Encapsulate a datagram and kick it into a TTY queue. */
static int strip_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct strip *strip_info = netdev_priv(dev);

	if (!netif_running(dev)) {
		printk(KERN_ERR "%s: xmit call when iface is down\n",
		       dev->name);
		return (1);
	}

	netif_stop_queue(dev);

	del_timer(&strip_info->idle_timer);


	if (time_after(jiffies, strip_info->pps_timer + HZ)) {
		unsigned long t = jiffies - strip_info->pps_timer;
		unsigned long rx_pps_count = (strip_info->rx_pps_count * HZ * 8 + t / 2) / t;
		unsigned long tx_pps_count = (strip_info->tx_pps_count * HZ * 8 + t / 2) / t;
		unsigned long sx_pps_count = (strip_info->sx_pps_count * HZ * 8 + t / 2) / t;

		strip_info->pps_timer = jiffies;
		strip_info->rx_pps_count = 0;
		strip_info->tx_pps_count = 0;
		strip_info->sx_pps_count = 0;

		strip_info->rx_average_pps = (strip_info->rx_average_pps + rx_pps_count + 1) / 2;
		strip_info->tx_average_pps = (strip_info->tx_average_pps + tx_pps_count + 1) / 2;
		strip_info->sx_average_pps = (strip_info->sx_average_pps + sx_pps_count + 1) / 2;

		if (rx_pps_count / 8 >= 10)
			printk(KERN_INFO "%s: WARNING: Receiving %ld packets per second.\n",
			       strip_info->dev->name, rx_pps_count / 8);
		if (tx_pps_count / 8 >= 10)
			printk(KERN_INFO "%s: WARNING: Tx        %ld packets per second.\n",
			       strip_info->dev->name, tx_pps_count / 8);
		if (sx_pps_count / 8 >= 10)
			printk(KERN_INFO "%s: WARNING: Sending   %ld packets per second.\n",
			       strip_info->dev->name, sx_pps_count / 8);
	}

	spin_lock_bh(&strip_lock);

	strip_send(strip_info, skb);

	spin_unlock_bh(&strip_lock);

	if (skb)
		dev_kfree_skb(skb);
	return 0;
}

/*
 * IdleTask periodically calls strip_xmit, so even when we have no IP packets
 * to send for an extended period of time, the watchdog processing still gets
 * done to ensure that the radio stays in Starmode
 */

static void strip_IdleTask(unsigned long parameter)
{
	strip_xmit(NULL, (struct net_device *) parameter);
}

/*
 * Create the MAC header for an arbitrary protocol layer
 *
 * saddr!=NULL        means use this specific address (n/a for Metricom)
 * saddr==NULL        means use default device source address
 * daddr!=NULL        means use this destination address
 * daddr==NULL        means leave destination address alone
 *                 (e.g. unresolved arp -- kernel will call
 *                 rebuild_header later to fill in the address)
 */

static int strip_header(struct sk_buff *skb, struct net_device *dev,
			unsigned short type, const void *daddr,
			const void *saddr, unsigned len)
{
	struct strip *strip_info = netdev_priv(dev);
	STRIP_Header *header = (STRIP_Header *) skb_push(skb, sizeof(STRIP_Header));

	/*printk(KERN_INFO "%s: strip_header 0x%04X %s\n", dev->name, type,
	   type == ETH_P_IP ? "IP" : type == ETH_P_ARP ? "ARP" : ""); */

	header->src_addr = strip_info->true_dev_addr;
	header->protocol = htons(type);

	/*HexDump("strip_header", netdev_priv(dev), skb->data, skb->data + skb->len); */

	if (!daddr)
		return (-dev->hard_header_len);

	header->dst_addr = *(MetricomAddress *) daddr;
	return (dev->hard_header_len);
}

/*
 * Rebuild the MAC header. This is called after an ARP
 * (or in future other address resolution) has completed on this
 * sk_buff. We now let ARP fill in the other fields.
 * I think this should return zero if packet is ready to send,
 * or non-zero if it needs more time to do an address lookup
 */

static int strip_rebuild_header(struct sk_buff *skb)
{
#ifdef CONFIG_INET
	STRIP_Header *header = (STRIP_Header *) skb->data;

	/* Arp find returns zero if if knows the address, */
	/* or if it doesn't know the address it sends an ARP packet and returns non-zero */
	return arp_find(header->dst_addr.c, skb) ? 1 : 0;
#else
	return 0;
#endif
}


/************************************************************************/
/* Receiving routines							*/

/*
 * This function parses the response to the ATS300? command,
 * extracting the radio version and serial number.
 */
static void get_radio_version(struct strip *strip_info, __u8 * ptr, __u8 * end)
{
	__u8 *p, *value_begin, *value_end;
	int len;

	/* Determine the beginning of the second line of the payload */
	p = ptr;
	while (p < end && *p != 10)
		p++;
	if (p >= end)
		return;
	p++;
	value_begin = p;

	/* Determine the end of line */
	while (p < end && *p != 10)
		p++;
	if (p >= end)
		return;
	value_end = p;
	p++;

	len = value_end - value_begin;
	len = min_t(int, len, sizeof(FirmwareVersion) - 1);
	if (strip_info->firmware_version.c[0] == 0)
		printk(KERN_INFO "%s: Radio Firmware: %.*s\n",
		       strip_info->dev->name, len, value_begin);
	sprintf(strip_info->firmware_version.c, "%.*s", len, value_begin);

	/* Look for the first colon */
	while (p < end && *p != ':')
		p++;
	if (p >= end)
		return;
	/* Skip over the space */
	p += 2;
	len = sizeof(SerialNumber) - 1;
	if (p + len <= end) {
		sprintf(strip_info->serial_number.c, "%.*s", len, p);
	} else {
		printk(KERN_DEBUG
		       "STRIP: radio serial number shorter (%zd) than expected (%d)\n",
		       end - p, len);
	}
}

/*
 * This function parses the response to the ATS325? command,
 * extracting the radio battery voltage.
 */
static void get_radio_voltage(struct strip *strip_info, __u8 * ptr, __u8 * end)
{
	int len;

	len = sizeof(BatteryVoltage) - 1;
	if (ptr + len <= end) {
		sprintf(strip_info->battery_voltage.c, "%.*s", len, ptr);
	} else {
		printk(KERN_DEBUG
		       "STRIP: radio voltage string shorter (%zd) than expected (%d)\n",
		       end - ptr, len);
	}
}

/*
 * This function parses the responses to the AT~LA and ATS311 commands,
 * which list the radio's neighbours.
 */
static void get_radio_neighbours(MetricomNodeTable * table, __u8 * ptr, __u8 * end)
{
	table->num_nodes = 0;
	while (ptr < end && table->num_nodes < NODE_TABLE_SIZE) {
		MetricomNode *node = &table->node[table->num_nodes++];
		char *dst = node->c, *limit = dst + sizeof(*node) - 1;
		while (ptr < end && *ptr <= 32)
			ptr++;
		while (ptr < end && dst < limit && *ptr != 10)
			*dst++ = *ptr++;
		*dst++ = 0;
		while (ptr < end && ptr[-1] != 10)
			ptr++;
	}
	do_gettimeofday(&table->timestamp);
}

static int get_radio_address(struct strip *strip_info, __u8 * p)
{
	MetricomAddress addr;

	if (string_to_radio_address(&addr, p))
		return (1);

	/* See if our radio address has changed */
	if (memcmp(strip_info->true_dev_addr.c, addr.c, sizeof(addr))) {
		MetricomAddressString addr_string;
		radio_address_to_string(&addr, &addr_string);
		printk(KERN_INFO "%s: Radio address = %s\n",
		       strip_info->dev->name, addr_string.c);
		strip_info->true_dev_addr = addr;
		if (!strip_info->manual_dev_addr)
			*(MetricomAddress *) strip_info->dev->dev_addr =
			    addr;
		/* Give the radio a few seconds to get its head straight, then send an arp */
		strip_info->gratuitous_arp = jiffies + 15 * HZ;
		strip_info->arp_interval = 1 * HZ;
	}
	return (0);
}

static int verify_checksum(struct strip *strip_info)
{
	__u8 *p = strip_info->sx_buff;
	__u8 *end = strip_info->sx_buff + strip_info->sx_count - 4;
	u_short sum =
	    (READHEX16(end[0]) << 12) | (READHEX16(end[1]) << 8) |
	    (READHEX16(end[2]) << 4) | (READHEX16(end[3]));
	while (p < end)
		sum -= *p++;
	if (sum == 0 && strip_info->firmware_level == StructuredMessages) {
		strip_info->firmware_level = ChecksummedMessages;
		printk(KERN_INFO "%s: Radio provides message checksums\n",
		       strip_info->dev->name);
	}
	return (sum == 0);
}

static void RecvErr(char *msg, struct strip *strip_info)
{
	__u8 *ptr = strip_info->sx_buff;
	__u8 *end = strip_info->sx_buff + strip_info->sx_count;
	DumpData(msg, strip_info, ptr, end);
	strip_info->rx_errors++;
}

static void RecvErr_Message(struct strip *strip_info, __u8 * sendername,
			    const __u8 * msg, u_long len)
{
	if (has_prefix(msg, len, "001")) {	/* Not in StarMode! */
		RecvErr("Error Msg:", strip_info);
		printk(KERN_INFO "%s: Radio %s is not in StarMode\n",
		       strip_info->dev->name, sendername);
	}

	else if (has_prefix(msg, len, "002")) {	/* Remap handle */
		/* We ignore "Remap handle" messages for now */
	}

	else if (has_prefix(msg, len, "003")) {	/* Can't resolve name */
		RecvErr("Error Msg:", strip_info);
		printk(KERN_INFO "%s: Destination radio name is unknown\n",
		       strip_info->dev->name);
	}

	else if (has_prefix(msg, len, "004")) {	/* Name too small or missing */
		strip_info->watchdog_doreset = jiffies + LongTime;
#if TICKLE_TIMERS
		{
			struct timeval tv;
			do_gettimeofday(&tv);
			printk(KERN_INFO
			       "**** Got ERR_004 response         at %02d.%06d\n",
			       tv.tv_sec % 100, tv.tv_usec);
		}
#endif
		if (!strip_info->working) {
			strip_info->working = TRUE;
			printk(KERN_INFO "%s: Radio now in starmode\n",
			       strip_info->dev->name);
			/*
			 * If the radio has just entered a working state, we should do our first
			 * probe ASAP, so that we find out our radio address etc. without delay.
			 */
			strip_info->watchdog_doprobe = jiffies;
		}
		if (strip_info->firmware_level == NoStructure && sendername) {
			strip_info->firmware_level = StructuredMessages;
			strip_info->next_command = 0;	/* Try to enable checksums ASAP */
			printk(KERN_INFO
			       "%s: Radio provides structured messages\n",
			       strip_info->dev->name);
		}
		if (strip_info->firmware_level >= StructuredMessages) {
			/*
			 * If this message has a valid checksum on the end, then the call to verify_checksum
			 * will elevate the firmware_level to ChecksummedMessages for us. (The actual return
			 * code from verify_checksum is ignored here.)
			 */
			verify_checksum(strip_info);
			/*
			 * If the radio has structured messages but we don't yet have all our information about it,
			 * we should do probes without delay, until we have gathered all the information
			 */
			if (!GOT_ALL_RADIO_INFO(strip_info))
				strip_info->watchdog_doprobe = jiffies;
		}
	}

	else if (has_prefix(msg, len, "005"))	/* Bad count specification */
		RecvErr("Error Msg:", strip_info);

	else if (has_prefix(msg, len, "006"))	/* Header too big */
		RecvErr("Error Msg:", strip_info);

	else if (has_prefix(msg, len, "007")) {	/* Body too big */
		RecvErr("Error Msg:", strip_info);
		printk(KERN_ERR
		       "%s: Error! Packet size too big for radio.\n",
		       strip_info->dev->name);
	}

	else if (has_prefix(msg, len, "008")) {	/* Bad character in name */
		RecvErr("Error Msg:", strip_info);
		printk(KERN_ERR
		       "%s: Radio name contains illegal character\n",
		       strip_info->dev->name);
	}

	else if (has_prefix(msg, len, "009"))	/* No count or line terminator */
		RecvErr("Error Msg:", strip_info);

	else if (has_prefix(msg, len, "010"))	/* Invalid checksum */
		RecvErr("Error Msg:", strip_info);

	else if (has_prefix(msg, len, "011"))	/* Checksum didn't match */
		RecvErr("Error Msg:", strip_info);

	else if (has_prefix(msg, len, "012"))	/* Failed to transmit packet */
		RecvErr("Error Msg:", strip_info);

	else
		RecvErr("Error Msg:", strip_info);
}

static void process_AT_response(struct strip *strip_info, __u8 * ptr,
				__u8 * end)
{
	u_long len;
	__u8 *p = ptr;
	while (p < end && p[-1] != 10)
		p++;		/* Skip past first newline character */
	/* Now ptr points to the AT command, and p points to the text of the response. */
	len = p - ptr;

#if TICKLE_TIMERS
	{
		struct timeval tv;
		do_gettimeofday(&tv);
		printk(KERN_INFO "**** Got AT response %.7s      at %02d.%06d\n",
		       ptr, tv.tv_sec % 100, tv.tv_usec);
	}
#endif

	if (has_prefix(ptr, len, "ATS300?"))
		get_radio_version(strip_info, p, end);
	else if (has_prefix(ptr, len, "ATS305?"))
		get_radio_address(strip_info, p);
	else if (has_prefix(ptr, len, "ATS311?"))
		get_radio_neighbours(&strip_info->poletops, p, end);
	else if (has_prefix(ptr, len, "ATS319=7"))
		verify_checksum(strip_info);
	else if (has_prefix(ptr, len, "ATS325?"))
		get_radio_voltage(strip_info, p, end);
	else if (has_prefix(ptr, len, "AT~LA"))
		get_radio_neighbours(&strip_info->portables, p, end);
	else
		RecvErr("Unknown AT Response:", strip_info);
}

static void process_ACK(struct strip *strip_info, __u8 * ptr, __u8 * end)
{
	/* Currently we don't do anything with ACKs from the radio */
}

static void process_Info(struct strip *strip_info, __u8 * ptr, __u8 * end)
{
	if (ptr + 16 > end)
		RecvErr("Bad Info Msg:", strip_info);
}

static struct net_device *get_strip_dev(struct strip *strip_info)
{
	/* If our hardware address is *manually set* to zero, and we know our */
	/* real radio hardware address, try to find another strip device that has been */
	/* manually set to that address that we can 'transfer ownership' of this packet to  */
	if (strip_info->manual_dev_addr &&
	    !memcmp(strip_info->dev->dev_addr, zero_address.c,
		    sizeof(zero_address))
	    && memcmp(&strip_info->true_dev_addr, zero_address.c,
		      sizeof(zero_address))) {
		struct net_device *dev;
		read_lock_bh(&dev_base_lock);
		for_each_netdev(&init_net, dev) {
			if (dev->type == strip_info->dev->type &&
			    !memcmp(dev->dev_addr,
				    &strip_info->true_dev_addr,
				    sizeof(MetricomAddress))) {
				printk(KERN_INFO
				       "%s: Transferred packet ownership to %s.\n",
				       strip_info->dev->name, dev->name);
				read_unlock_bh(&dev_base_lock);
				return (dev);
			}
		}
		read_unlock_bh(&dev_base_lock);
	}
	return (strip_info->dev);
}

/*
 * Send one completely decapsulated datagram to the next layer.
 */

static void deliver_packet(struct strip *strip_info, STRIP_Header * header,
			   __u16 packetlen)
{
	struct sk_buff *skb = dev_alloc_skb(sizeof(STRIP_Header) + packetlen);
	if (!skb) {
		printk(KERN_ERR "%s: memory squeeze, dropping packet.\n",
		       strip_info->dev->name);
		strip_info->rx_dropped++;
	} else {
		memcpy(skb_put(skb, sizeof(STRIP_Header)), header,
		       sizeof(STRIP_Header));
		memcpy(skb_put(skb, packetlen), strip_info->rx_buff,
		       packetlen);
		skb->dev = get_strip_dev(strip_info);
		skb->protocol = header->protocol;
		skb_reset_mac_header(skb);

		/* Having put a fake header on the front of the sk_buff for the */
		/* benefit of tools like tcpdump, skb_pull now 'consumes' that  */
		/* fake header before we hand the packet up to the next layer.  */
		skb_pull(skb, sizeof(STRIP_Header));

		/* Finally, hand the packet up to the next layer (e.g. IP or ARP, etc.) */
		strip_info->rx_packets++;
		strip_info->rx_pps_count++;
#ifdef EXT_COUNTERS
		strip_info->rx_bytes += packetlen;
#endif
		skb->dev->last_rx = jiffies;
		netif_rx(skb);
	}
}

static void process_IP_packet(struct strip *strip_info,
			      STRIP_Header * header, __u8 * ptr,
			      __u8 * end)
{
	__u16 packetlen;

	/* Decode start of the IP packet header */
	ptr = UnStuffData(ptr, end, strip_info->rx_buff, 4);
	if (!ptr) {
		RecvErr("IP Packet too short", strip_info);
		return;
	}

	packetlen = ((__u16) strip_info->rx_buff[2] << 8) | strip_info->rx_buff[3];

	if (packetlen > MAX_RECV_MTU) {
		printk(KERN_INFO "%s: Dropping oversized received IP packet: %d bytes\n",
		       strip_info->dev->name, packetlen);
		strip_info->rx_dropped++;
		return;
	}

	/*printk(KERN_INFO "%s: Got %d byte IP packet\n", strip_info->dev->name, packetlen); */

	/* Decode remainder of the IP packet */
	ptr =
	    UnStuffData(ptr, end, strip_info->rx_buff + 4, packetlen - 4);
	if (!ptr) {
		RecvErr("IP Packet too short", strip_info);
		return;
	}

	if (ptr < end) {
		RecvErr("IP Packet too long", strip_info);
		return;
	}

	header->protocol = htons(ETH_P_IP);

	deliver_packet(strip_info, header, packetlen);
}

static void process_ARP_packet(struct strip *strip_info,
			       STRIP_Header * header, __u8 * ptr,
			       __u8 * end)
{
	__u16 packetlen;
	struct arphdr *arphdr = (struct arphdr *) strip_info->rx_buff;

	/* Decode start of the ARP packet */
	ptr = UnStuffData(ptr, end, strip_info->rx_buff, 8);
	if (!ptr) {
		RecvErr("ARP Packet too short", strip_info);
		return;
	}

	packetlen = 8 + (arphdr->ar_hln + arphdr->ar_pln) * 2;

	if (packetlen > MAX_RECV_MTU) {
		printk(KERN_INFO
		       "%s: Dropping oversized received ARP packet: %d bytes\n",
		       strip_info->dev->name, packetlen);
		strip_info->rx_dropped++;
		return;
	}

	/*printk(KERN_INFO "%s: Got %d byte ARP %s\n",
	   strip_info->dev->name, packetlen,
	   ntohs(arphdr->ar_op) == ARPOP_REQUEST ? "request" : "reply"); */

	/* Decode remainder of the ARP packet */
	ptr =
	    UnStuffData(ptr, end, strip_info->rx_buff + 8, packetlen - 8);
	if (!ptr) {
		RecvErr("ARP Packet too short", strip_info);
		return;
	}

	if (ptr < end) {
		RecvErr("ARP Packet too long", strip_info);
		return;
	}

	header->protocol = htons(ETH_P_ARP);

	deliver_packet(strip_info, header, packetlen);
}

/*
 * process_text_message processes a <CR>-terminated block of data received
 * from the radio that doesn't begin with a '*' character. All normal
 * Starmode communication messages with the radio begin with a '*',
 * so any text that does not indicates a serial port error, a radio that
 * is in Hayes command mode instead of Starmode, or a radio with really
 * old firmware that doesn't frame its Starmode responses properly.
 */
static void process_text_message(struct strip *strip_info)
{
	__u8 *msg = strip_info->sx_buff;
	int len = strip_info->sx_count;

	/* Check for anything that looks like it might be our radio name */
	/* (This is here for backwards compatibility with old firmware)  */
	if (len == 9 && get_radio_address(strip_info, msg) == 0)
		return;

	if (text_equal(msg, len, "OK"))
		return;		/* Ignore 'OK' responses from prior commands */
	if (text_equal(msg, len, "ERROR"))
		return;		/* Ignore 'ERROR' messages */
	if (has_prefix(msg, len, "ate0q1"))
		return;		/* Ignore character echo back from the radio */

	/* Catch other error messages */
	/* (This is here for backwards compatibility with old firmware) */
	if (has_prefix(msg, len, "ERR_")) {
		RecvErr_Message(strip_info, NULL, &msg[4], len - 4);
		return;
	}

	RecvErr("No initial *", strip_info);
}

/*
 * process_message processes a <CR>-terminated block of data received
 * from the radio. If the radio is not in Starmode or has old firmware,
 * it may be a line of text in response to an AT command. Ideally, with
 * a current radio that's properly in Starmode, all data received should
 * be properly framed and checksummed radio message blocks, containing
 * either a starmode packet, or a other communication from the radio
 * firmware, like "INF_" Info messages and &COMMAND responses.
 */
static void process_message(struct strip *strip_info)
{
	STRIP_Header header = { zero_address, zero_address, 0 };
	__u8 *ptr = strip_info->sx_buff;
	__u8 *end = strip_info->sx_buff + strip_info->sx_count;
	__u8 sendername[32], *sptr = sendername;
	MetricomKey key;

	/*HexDump("Receiving", strip_info, ptr, end); */

	/* Check for start of address marker, and then skip over it */
	if (*ptr == '*')
		ptr++;
	else {
		process_text_message(strip_info);
		return;
	}

	/* Copy out the return address */
	while (ptr < end && *ptr != '*'
	       && sptr < ARRAY_END(sendername) - 1)
		*sptr++ = *ptr++;
	*sptr = 0;		/* Null terminate the sender name */

	/* Check for end of address marker, and skip over it */
	if (ptr >= end || *ptr != '*') {
		RecvErr("No second *", strip_info);
		return;
	}
	ptr++;			/* Skip the second '*' */

	/* If the sender name is "&COMMAND", ignore this 'packet'       */
	/* (This is here for backwards compatibility with old firmware) */
	if (!strcmp(sendername, "&COMMAND")) {
		strip_info->firmware_level = NoStructure;
		strip_info->next_command = CompatibilityCommand;
		return;
	}

	if (ptr + 4 > end) {
		RecvErr("No proto key", strip_info);
		return;
	}

	/* Get the protocol key out of the buffer */
	key.c[0] = *ptr++;
	key.c[1] = *ptr++;
	key.c[2] = *ptr++;
	key.c[3] = *ptr++;

	/* If we're using checksums, verify the checksum at the end of the packet */
	if (strip_info->firmware_level >= ChecksummedMessages) {
		end -= 4;	/* Chop the last four bytes off the packet (they're the checksum) */
		if (ptr > end) {
			RecvErr("Missing Checksum", strip_info);
			return;
		}
		if (!verify_checksum(strip_info)) {
			RecvErr("Bad Checksum", strip_info);
			return;
		}
	}

	/*printk(KERN_INFO "%s: Got packet from \"%s\".\n", strip_info->dev->name, sendername); */

	/*
	 * Fill in (pseudo) source and destination addresses in the packet.
	 * We assume that the destination address was our address (the radio does not
	 * tell us this). If the radio supplies a source address, then we use it.
	 */
	header.dst_addr = strip_info->true_dev_addr;
	string_to_radio_address(&header.src_addr, sendername);

#ifdef EXT_COUNTERS
	if (key.l == SIP0Key.l) {
		strip_info->rx_rbytes += (end - ptr);
		process_IP_packet(strip_info, &header, ptr, end);
	} else if (key.l == ARP0Key.l) {
		strip_info->rx_rbytes += (end - ptr);
		process_ARP_packet(strip_info, &header, ptr, end);
	} else if (key.l == ATR_Key.l) {
		strip_info->rx_ebytes += (end - ptr);
		process_AT_response(strip_info, ptr, end);
	} else if (key.l == ACK_Key.l) {
		strip_info->rx_ebytes += (end - ptr);
		process_ACK(strip_info, ptr, end);
	} else if (key.l == INF_Key.l) {
		strip_info->rx_ebytes += (end - ptr);
		process_Info(strip_info, ptr, end);
	} else if (key.l == ERR_Key.l) {
		strip_info->rx_ebytes += (end - ptr);
		RecvErr_Message(strip_info, sendername, ptr, end - ptr);
	} else
		RecvErr("Unrecognized protocol key", strip_info);
#else
	if (key.l == SIP0Key.l)
		process_IP_packet(strip_info, &header, ptr, end);
	else if (key.l == ARP0Key.l)
		process_ARP_packet(strip_info, &header, ptr, end);
	else if (key.l == ATR_Key.l)
		process_AT_response(strip_info, ptr, end);
	else if (key.l == ACK_Key.l)
		process_ACK(strip_info, ptr, end);
	else if (key.l == INF_Key.l)
		process_Info(strip_info, ptr, end);
	else if (key.l == ERR_Key.l)
		RecvErr_Message(strip_info, sendername, ptr, end - ptr);
	else
		RecvErr("Unrecognized protocol key", strip_info);
#endif
}

#define TTYERROR(X) ((X) == TTY_BREAK   ? "Break"            : \
                     (X) == TTY_FRAME   ? "Framing Error"    : \
                     (X) == TTY_PARITY  ? "Parity Error"     : \
                     (X) == TTY_OVERRUN ? "Hardware Overrun" : "Unknown Error")

/*
 * Handle the 'receiver data ready' interrupt.
 * This function is called by the 'tty_io' module in the kernel when
 * a block of STRIP data has been received, which can now be decapsulated
 * and sent on to some IP layer for further processing.
 */

static void strip_receive_buf(struct tty_struct *tty, const unsigned char *cp,
		  char *fp, int count)
{
	struct strip *strip_info = (struct strip *) tty->disc_data;
	const unsigned char *end = cp + count;

	if (!strip_info || strip_info->magic != STRIP_MAGIC
	    || !netif_running(strip_info->dev))
		return;

	spin_lock_bh(&strip_lock);
#if 0
	{
		struct timeval tv;
		do_gettimeofday(&tv);
		printk(KERN_INFO
		       "**** strip_receive_buf: %3d bytes at %02d.%06d\n",
		       count, tv.tv_sec % 100, tv.tv_usec);
	}
#endif

#ifdef EXT_COUNTERS
	strip_info->rx_sbytes += count;
#endif

	/* Read the characters out of the buffer */
	while (cp < end) {
		if (fp && *fp)
			printk(KERN_INFO "%s: %s on serial port\n",
			       strip_info->dev->name, TTYERROR(*fp));
		if (fp && *fp++ && !strip_info->discard) {	/* If there's a serial error, record it */
			/* If we have some characters in the buffer, discard them */
			strip_info->discard = strip_info->sx_count;
			strip_info->rx_errors++;
		}

		/* Leading control characters (CR, NL, Tab, etc.) are ignored */
		if (strip_info->sx_count > 0 || *cp >= ' ') {
			if (*cp == 0x0D) {	/* If end of packet, decide what to do with it */
				if (strip_info->sx_count > 3000)
					printk(KERN_INFO
					       "%s: Cut a %d byte packet (%zd bytes remaining)%s\n",
					       strip_info->dev->name,
					       strip_info->sx_count,
					       end - cp - 1,
					       strip_info->
					       discard ? " (discarded)" :
					       "");
				if (strip_info->sx_count >
				    strip_info->sx_size) {
					strip_info->rx_over_errors++;
					printk(KERN_INFO
					       "%s: sx_buff overflow (%d bytes total)\n",
					       strip_info->dev->name,
					       strip_info->sx_count);
				} else if (strip_info->discard)
					printk(KERN_INFO
					       "%s: Discarding bad packet (%d/%d)\n",
					       strip_info->dev->name,
					       strip_info->discard,
					       strip_info->sx_count);
				else
					process_message(strip_info);
				strip_info->discard = 0;
				strip_info->sx_count = 0;
			} else {
				/* Make sure we have space in the buffer */
				if (strip_info->sx_count <
				    strip_info->sx_size)
					strip_info->sx_buff[strip_info->
							    sx_count] =
					    *cp;
				strip_info->sx_count++;
			}
		}
		cp++;
	}
	spin_unlock_bh(&strip_lock);
}


/************************************************************************/
/* General control routines						*/

static int set_mac_address(struct strip *strip_info,
			   MetricomAddress * addr)
{
	/*
	 * We're using a manually specified address if the address is set
	 * to anything other than all ones. Setting the address to all ones
	 * disables manual mode and goes back to automatic address determination
	 * (tracking the true address that the radio has).
	 */
	strip_info->manual_dev_addr =
	    memcmp(addr->c, broadcast_address.c,
		   sizeof(broadcast_address));
	if (strip_info->manual_dev_addr)
		*(MetricomAddress *) strip_info->dev->dev_addr = *addr;
	else
		*(MetricomAddress *) strip_info->dev->dev_addr =
		    strip_info->true_dev_addr;
	return 0;
}

static int strip_set_mac_address(struct net_device *dev, void *addr)
{
	struct strip *strip_info = netdev_priv(dev);
	struct sockaddr *sa = addr;
	printk(KERN_INFO "%s: strip_set_dev_mac_address called\n", dev->name);
	set_mac_address(strip_info, (MetricomAddress *) sa->sa_data);
	return 0;
}

static struct net_device_stats *strip_get_stats(struct net_device *dev)
{
	struct strip *strip_info = netdev_priv(dev);
	static struct net_device_stats stats;

	memset(&stats, 0, sizeof(struct net_device_stats));

	stats.rx_packets = strip_info->rx_packets;
	stats.tx_packets = strip_info->tx_packets;
	stats.rx_dropped = strip_info->rx_dropped;
	stats.tx_dropped = strip_info->tx_dropped;
	stats.tx_errors = strip_info->tx_errors;
	stats.rx_errors = strip_info->rx_errors;
	stats.rx_over_errors = strip_info->rx_over_errors;
	return (&stats);
}


/************************************************************************/
/* Opening and closing							*/

/*
 * Here's the order things happen:
 * When the user runs "slattach -p strip ..."
 *  1. The TTY module calls strip_open;;
 *  2. strip_open calls strip_alloc
 *  3.                  strip_alloc calls register_netdev
 *  4.                  register_netdev calls strip_dev_init
 *  5. then strip_open finishes setting up the strip_info
 *
 * When the user runs "ifconfig st<x> up address netmask ..."
 *  6. strip_open_low gets called
 *
 * When the user runs "ifconfig st<x> down"
 *  7. strip_close_low gets called
 *
 * When the user kills the slattach process
 *  8. strip_close gets called
 *  9. strip_close calls dev_close
 * 10. if the device is still up, then dev_close calls strip_close_low
 * 11. strip_close calls strip_free
 */

/* Open the low-level part of the STRIP channel. Easy! */

static int strip_open_low(struct net_device *dev)
{
	struct strip *strip_info = netdev_priv(dev);

	if (strip_info->tty == NULL)
		return (-ENODEV);

	if (!allocate_buffers(strip_info, dev->mtu))
		return (-ENOMEM);

	strip_info->sx_count = 0;
	strip_info->tx_left = 0;

	strip_info->discard = 0;
	strip_info->working = FALSE;
	strip_info->firmware_level = NoStructure;
	strip_info->next_command = CompatibilityCommand;
	strip_info->user_baud = tty_get_baud_rate(strip_info->tty);

	printk(KERN_INFO "%s: Initializing Radio.\n",
	       strip_info->dev->name);
	ResetRadio(strip_info);
	strip_info->idle_timer.expires = jiffies + 1 * HZ;
	add_timer(&strip_info->idle_timer);
	netif_wake_queue(dev);
	return (0);
}


/*
 * Close the low-level part of the STRIP channel. Easy!
 */

static int strip_close_low(struct net_device *dev)
{
	struct strip *strip_info = netdev_priv(dev);

	if (strip_info->tty == NULL)
		return -EBUSY;
	strip_info->tty->flags &= ~(1 << TTY_DO_WRITE_WAKEUP);

	netif_stop_queue(dev);

	/*
	 * Free all STRIP frame buffers.
	 */
	kfree(strip_info->rx_buff);
	strip_info->rx_buff = NULL;
	kfree(strip_info->sx_buff);
	strip_info->sx_buff = NULL;
	kfree(strip_info->tx_buff);
	strip_info->tx_buff = NULL;

	del_timer(&strip_info->idle_timer);
	return 0;
}

static const struct header_ops strip_header_ops = {
	.create = strip_header,
	.rebuild = strip_rebuild_header,
};

/*
 * This routine is called by DDI when the
 * (dynamically assigned) device is registered
 */

static void strip_dev_setup(struct net_device *dev)
{
	/*
	 * Finish setting up the DEVICE info.
	 */

	dev->trans_start = 0;
	dev->last_rx = 0;
	dev->tx_queue_len = 30;	/* Drop after 30 frames queued */

	dev->flags = 0;
	dev->mtu = DEFAULT_STRIP_MTU;
	dev->type = ARPHRD_METRICOM;	/* dtang */
	dev->hard_header_len = sizeof(STRIP_Header);
	/*
	 *  dev->priv             Already holds a pointer to our struct strip
	 */

	*(MetricomAddress *) & dev->broadcast = broadcast_address;
	dev->dev_addr[0] = 0;
	dev->addr_len = sizeof(MetricomAddress);

	/*
	 * Pointers to interface service routines.
	 */

	dev->open = strip_open_low;
	dev->stop = strip_close_low;
	dev->hard_start_xmit = strip_xmit;
	dev->header_ops = &strip_header_ops;

	dev->set_mac_address = strip_set_mac_address;
	dev->get_stats = strip_get_stats;
	dev->change_mtu = strip_change_mtu;
}

/*
 * Free a STRIP channel.
 */

static void strip_free(struct strip *strip_info)
{
	spin_lock_bh(&strip_lock);
	list_del_rcu(&strip_info->list);
	spin_unlock_bh(&strip_lock);

	strip_info->magic = 0;

	free_netdev(strip_info->dev);
}


/*
 * Allocate a new free STRIP channel
 */
static struct strip *strip_alloc(void)
{
	struct list_head *n;
	struct net_device *dev;
	struct strip *strip_info;

	dev = alloc_netdev(sizeof(struct strip), "st%d",
			   strip_dev_setup);

	if (!dev)
		return NULL;	/* If no more memory, return */


	strip_info = netdev_priv(dev);
	strip_info->dev = dev;

	strip_info->magic = STRIP_MAGIC;
	strip_info->tty = NULL;

	strip_info->gratuitous_arp = jiffies + LongTime;
	strip_info->arp_interval = 0;
	init_timer(&strip_info->idle_timer);
	strip_info->idle_timer.data = (long) dev;
	strip_info->idle_timer.function = strip_IdleTask;


	spin_lock_bh(&strip_lock);
 rescan:
	/*
	 * Search the list to find where to put our new entry
	 * (and in the process decide what channel number it is
	 * going to be)
	 */
	list_for_each(n, &strip_list) {
		struct strip *s = hlist_entry(n, struct strip, list);

		if (s->dev->base_addr == dev->base_addr) {
			++dev->base_addr;
			goto rescan;
		}
	}

	sprintf(dev->name, "st%ld", dev->base_addr);

	list_add_tail_rcu(&strip_info->list, &strip_list);
	spin_unlock_bh(&strip_lock);

	return strip_info;
}

/*
 * Open the high-level part of the STRIP channel.
 * This function is called by the TTY module when the
 * STRIP line discipline is called for.  Because we are
 * sure the tty line exists, we only have to link it to
 * a free STRIP channel...
 */

static int strip_open(struct tty_struct *tty)
{
	struct strip *strip_info = (struct strip *) tty->disc_data;

	/*
	 * First make sure we're not already connected.
	 */

	if (strip_info && strip_info->magic == STRIP_MAGIC)
		return -EEXIST;

	/*
	 * We need a write method.
	 */

	if (tty->ops->write == NULL)
		return -EOPNOTSUPP;

	/*
	 * OK.  Find a free STRIP channel to use.
	 */
	if ((strip_info = strip_alloc()) == NULL)
		return -ENFILE;

	/*
	 * Register our newly created device so it can be ifconfig'd
	 * strip_dev_init() will be called as a side-effect
	 */

	if (register_netdev(strip_info->dev) != 0) {
		printk(KERN_ERR "strip: register_netdev() failed.\n");
		strip_free(strip_info);
		return -ENFILE;
	}

	strip_info->tty = tty;
	tty->disc_data = strip_info;
	tty->receive_room = 65536;

	tty_driver_flush_buffer(tty);

	/*
	 * Restore default settings
	 */

	strip_info->dev->type = ARPHRD_METRICOM;	/* dtang */

	/*
	 * Set tty options
	 */

	tty->termios->c_iflag |= IGNBRK | IGNPAR;	/* Ignore breaks and parity errors. */
	tty->termios->c_cflag |= CLOCAL;	/* Ignore modem control signals. */
	tty->termios->c_cflag &= ~HUPCL;	/* Don't close on hup */

	printk(KERN_INFO "STRIP: device \"%s\" activated\n",
	       strip_info->dev->name);

	/*
	 * Done.  We have linked the TTY line to a channel.
	 */
	return (strip_info->dev->base_addr);
}

/*
 * Close down a STRIP channel.
 * This means flushing out any pending queues, and then restoring the
 * TTY line discipline to what it was before it got hooked to STRIP
 * (which usually is TTY again).
 */

static void strip_close(struct tty_struct *tty)
{
	struct strip *strip_info = (struct strip *) tty->disc_data;

	/*
	 * First make sure we're connected.
	 */

	if (!strip_info || strip_info->magic != STRIP_MAGIC)
		return;

	unregister_netdev(strip_info->dev);

	tty->disc_data = NULL;
	strip_info->tty = NULL;
	printk(KERN_INFO "STRIP: device \"%s\" closed down\n",
	       strip_info->dev->name);
	strip_free(strip_info);
	tty->disc_data = NULL;
}


/************************************************************************/
/* Perform I/O control calls on an active STRIP channel.		*/

static int strip_ioctl(struct tty_struct *tty, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	struct strip *strip_info = (struct strip *) tty->disc_data;

	/*
	 * First make sure we're connected.
	 */

	if (!strip_info || strip_info->magic != STRIP_MAGIC)
		return -EINVAL;

	switch (cmd) {
	case SIOCGIFNAME:
		if(copy_to_user((void __user *) arg, strip_info->dev->name, strlen(strip_info->dev->name) + 1))
			return -EFAULT;
		break;
	case SIOCSIFHWADDR:
	{
		MetricomAddress addr;
		//printk(KERN_INFO "%s: SIOCSIFHWADDR\n", strip_info->dev->name);
		if(copy_from_user(&addr, (void __user *) arg, sizeof(MetricomAddress)))
			return -EFAULT;
		return set_mac_address(strip_info, &addr);
	}
	default:
		return tty_mode_ioctl(tty, file, cmd, arg);
		break;
	}
	return 0;
}


/************************************************************************/
/* Initialization							*/

static struct tty_ldisc strip_ldisc = {
	.magic = TTY_LDISC_MAGIC,
	.name = "strip",
	.owner = THIS_MODULE,
	.open = strip_open,
	.close = strip_close,
	.ioctl = strip_ioctl,
	.receive_buf = strip_receive_buf,
	.write_wakeup = strip_write_some_more,
};

/*
 * Initialize the STRIP driver.
 * This routine is called at boot time, to bootstrap the multi-channel
 * STRIP driver
 */

static char signon[] __initdata =
    KERN_INFO "STRIP: Version %s (unlimited channels)\n";

static int __init strip_init_driver(void)
{
	int status;

	printk(signon, StripVersion);

	
	/*
	 * Fill in our line protocol discipline, and register it
	 */
	if ((status = tty_register_ldisc(N_STRIP, &strip_ldisc)))
		printk(KERN_ERR "STRIP: can't register line discipline (err = %d)\n",
		       status);

	/*
	 * Register the status file with /proc
	 */
	proc_net_fops_create(&init_net, "strip", S_IFREG | S_IRUGO, &strip_seq_fops);

	return status;
}

module_init(strip_init_driver);

static const char signoff[] __exitdata =
    KERN_INFO "STRIP: Module Unloaded\n";

static void __exit strip_exit_driver(void)
{
	int i;
	struct list_head *p,*n;

	/* module ref count rules assure that all entries are unregistered */
	list_for_each_safe(p, n, &strip_list) {
		struct strip *s = list_entry(p, struct strip, list);
		strip_free(s);
	}

	/* Unregister with the /proc/net file here. */
	proc_net_remove(&init_net, "strip");

	if ((i = tty_unregister_ldisc(N_STRIP)))
		printk(KERN_ERR "STRIP: can't unregister line discipline (err = %d)\n", i);

	printk(signoff);
}

module_exit(strip_exit_driver);

MODULE_AUTHOR("Stuart Cheshire <cheshire@cs.stanford.edu>");
MODULE_DESCRIPTION("Starmode Radio IP (STRIP) Device Driver");
MODULE_LICENSE("Dual BSD/GPL");

MODULE_SUPPORTED_DEVICE("Starmode Radio IP (STRIP) modem");
