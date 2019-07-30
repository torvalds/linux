/* $Id: isdn_v110.c,v 1.1.2.2 2004/01/12 22:37:19 keil Exp $
 *
 * Linux ISDN subsystem, V.110 related functions (linklevel).
 *
 * Copyright by Thomas Pfeiffer (pfeiffer@pds.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/delay.h>

#include <linux/isdn.h>
#include "isdn_v110.h"

#undef ISDN_V110_DEBUG

char *isdn_v110_revision = "$Revision: 1.1.2.2 $";

#define V110_38400 255
#define V110_19200  15
#define V110_9600    3

/*
 * The following data are precoded matrices, online and offline matrix
 * for 9600, 19200 und 38400, respectively
 */
static unsigned char V110_OnMatrix_9600[] =
{0xfc, 0xfc, 0xfc, 0xfc, 0xff, 0xff, 0xff, 0xfd, 0xff, 0xff,
 0xff, 0xfd, 0xff, 0xff, 0xff, 0xfd, 0xff, 0xff, 0xff, 0xfd,
 0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfd, 0xff, 0xff,
 0xff, 0xfd, 0xff, 0xff, 0xff, 0xfd, 0xff, 0xff, 0xff, 0xfd};

static unsigned char V110_OffMatrix_9600[] =
{0xfc, 0xfc, 0xfc, 0xfc, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
 0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static unsigned char V110_OnMatrix_19200[] =
{0xf0, 0xf0, 0xff, 0xf7, 0xff, 0xf7, 0xff, 0xf7, 0xff, 0xf7,
 0xfd, 0xff, 0xff, 0xf7, 0xff, 0xf7, 0xff, 0xf7, 0xff, 0xf7};

static unsigned char V110_OffMatrix_19200[] =
{0xf0, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
 0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static unsigned char V110_OnMatrix_38400[] =
{0x00, 0x7f, 0x7f, 0x7f, 0x7f, 0xfd, 0x7f, 0x7f, 0x7f, 0x7f};

static unsigned char V110_OffMatrix_38400[] =
{0x00, 0xff, 0xff, 0xff, 0xff, 0xfd, 0xff, 0xff, 0xff, 0xff};

/*
 * FlipBits reorders sequences of keylen bits in one byte.
 * E.g. source order 7654321 will be converted to 45670123 when keylen = 4,
 * and to 67452301 when keylen = 2. This is necessary because ordering on
 * the isdn line is the other way.
 */
static inline unsigned char
FlipBits(unsigned char c, int keylen)
{
	unsigned char b = c;
	unsigned char bit = 128;
	int i;
	int j;
	int hunks = (8 / keylen);

	c = 0;
	for (i = 0; i < hunks; i++) {
		for (j = 0; j < keylen; j++) {
			if (b & (bit >> j))
				c |= bit >> (keylen - j - 1);
		}
		bit >>= keylen;
	}
	return c;
}


/* isdn_v110_open allocates and initializes private V.110 data
 * structures and returns a pointer to these.
 */
static isdn_v110_stream *
isdn_v110_open(unsigned char key, int hdrlen, int maxsize)
{
	int i;
	isdn_v110_stream *v;

	if ((v = kzalloc(sizeof(isdn_v110_stream), GFP_ATOMIC)) == NULL)
		return NULL;
	v->key = key;
	v->nbits = 0;
	for (i = 0; key & (1 << i); i++)
		v->nbits++;

	v->nbytes = 8 / v->nbits;
	v->decodelen = 0;

	switch (key) {
	case V110_38400:
		v->OnlineFrame = V110_OnMatrix_38400;
		v->OfflineFrame = V110_OffMatrix_38400;
		break;
	case V110_19200:
		v->OnlineFrame = V110_OnMatrix_19200;
		v->OfflineFrame = V110_OffMatrix_19200;
		break;
	default:
		v->OnlineFrame = V110_OnMatrix_9600;
		v->OfflineFrame = V110_OffMatrix_9600;
		break;
	}
	v->framelen = v->nbytes * 10;
	v->SyncInit = 5;
	v->introducer = 0;
	v->dbit = 1;
	v->b = 0;
	v->skbres = hdrlen;
	v->maxsize = maxsize - hdrlen;
	if ((v->encodebuf = kmalloc(maxsize, GFP_ATOMIC)) == NULL) {
		kfree(v);
		return NULL;
	}
	return v;
}

/* isdn_v110_close frees private V.110 data structures */
void
isdn_v110_close(isdn_v110_stream *v)
{
	if (v == NULL)
		return;
#ifdef ISDN_V110_DEBUG
	printk(KERN_DEBUG "v110 close\n");
#endif
	kfree(v->encodebuf);
	kfree(v);
}


/*
 * ValidHeaderBytes return the number of valid bytes in v->decodebuf
 */
static int
ValidHeaderBytes(isdn_v110_stream *v)
{
	int i;
	for (i = 0; (i < v->decodelen) && (i < v->nbytes); i++)
		if ((v->decodebuf[i] & v->key) != 0)
			break;
	return i;
}

/*
 * SyncHeader moves the decodebuf ptr to the next valid header
 */
static void
SyncHeader(isdn_v110_stream *v)
{
	unsigned char *rbuf = v->decodebuf;
	int len = v->decodelen;

	if (len == 0)
		return;
	for (rbuf++, len--; len > 0; len--, rbuf++)	/* such den SyncHeader in buf ! */
		if ((*rbuf & v->key) == 0)	/* erstes byte gefunden ?       */
			break;  /* jupp!                        */
	if (len)
		memcpy(v->decodebuf, rbuf, len);

	v->decodelen = len;
#ifdef ISDN_V110_DEBUG
	printk(KERN_DEBUG "isdn_v110: Header resync\n");
#endif
}

/* DecodeMatrix takes n (n>=1) matrices (v110 frames, 10 bytes) where
   len is the number of matrix-lines. len must be a multiple of 10, i.e.
   only complete matices must be given.
   From these, netto data is extracted and returned in buf. The return-value
   is the bytecount of the decoded data.
*/
static int
DecodeMatrix(isdn_v110_stream *v, unsigned char *m, int len, unsigned char *buf)
{
	int line = 0;
	int buflen = 0;
	int mbit = 64;
	int introducer = v->introducer;
	int dbit = v->dbit;
	unsigned char b = v->b;

	while (line < len) {    /* Are we done with all lines of the matrix? */
		if ((line % 10) == 0) {	/* the 0. line of the matrix is always 0 ! */
			if (m[line] != 0x00) {	/* not 0 ? -> error! */
#ifdef ISDN_V110_DEBUG
				printk(KERN_DEBUG "isdn_v110: DecodeMatrix, V110 Bad Header\n");
				/* returning now is not the right thing, though :-( */
#endif
			}
			line++; /* next line of matrix */
			continue;
		} else if ((line % 10) == 5) {	/* in line 5 there's only e-bits ! */
			if ((m[line] & 0x70) != 0x30) {	/* 011 has to be at the beginning! */
#ifdef ISDN_V110_DEBUG
				printk(KERN_DEBUG "isdn_v110: DecodeMatrix, V110 Bad 5th line\n");
				/* returning now is not the right thing, though :-( */
#endif
			}
			line++; /* next line */
			continue;
		} else if (!introducer) {	/* every byte starts with 10 (stopbit, startbit) */
			introducer = (m[line] & mbit) ? 0 : 1;	/* current bit of the matrix */
		next_byte:
			if (mbit > 2) {	/* was it the last bit in this line ? */
				mbit >>= 1;	/* no -> take next */
				continue;
			}       /* otherwise start with leftmost bit in the next line */
			mbit = 64;
			line++;
			continue;
		} else {        /* otherwise we need to set a data bit */
			if (m[line] & mbit)	/* was that bit set in the matrix ? */
				b |= dbit;	/* yes -> set it in the data byte */
			else
				b &= dbit - 1;	/* no -> clear it in the data byte */
			if (dbit < 128)	/* is that data byte done ? */
				dbit <<= 1;	/* no, got the next bit */
			else {  /* data byte is done */
				buf[buflen++] = b;	/* copy byte into the output buffer */
				introducer = b = 0;	/* init of the intro sequence and of the data byte */
				dbit = 1;	/* next we look for the 0th bit */
			}
			goto next_byte;	/* look for next bit in the matrix */
		}
	}
	v->introducer = introducer;
	v->dbit = dbit;
	v->b = b;
	return buflen;          /* return number of bytes in the output buffer */
}

/*
 * DecodeStream receives V.110 coded data from the input stream. It recovers the
 * original frames.
 * The input stream doesn't need to be framed
 */
struct sk_buff *
isdn_v110_decode(isdn_v110_stream *v, struct sk_buff *skb)
{
	int i;
	int j;
	int len;
	unsigned char *v110_buf;
	unsigned char *rbuf;

	if (!skb) {
		printk(KERN_WARNING "isdn_v110_decode called with NULL skb!\n");
		return NULL;
	}
	rbuf = skb->data;
	len = skb->len;
	if (v == NULL) {
		/* invalid handle, no chance to proceed */
		printk(KERN_WARNING "isdn_v110_decode called with NULL stream!\n");
		dev_kfree_skb(skb);
		return NULL;
	}
	if (v->decodelen == 0)  /* cache empty?               */
		for (; len > 0; len--, rbuf++)	/* scan for SyncHeader in buf */
			if ((*rbuf & v->key) == 0)
				break;	/* found first byte           */
	if (len == 0) {
		dev_kfree_skb(skb);
		return NULL;
	}
	/* copy new data to decode-buffer */
	memcpy(&(v->decodebuf[v->decodelen]), rbuf, len);
	v->decodelen += len;
ReSync:
	if (v->decodelen < v->nbytes) {	/* got a new header ? */
		dev_kfree_skb(skb);
		return NULL;    /* no, try later      */
	}
	if (ValidHeaderBytes(v) != v->nbytes) {	/* is that a valid header? */
		SyncHeader(v);  /* no -> look for header */
		goto ReSync;
	}
	len = (v->decodelen - (v->decodelen % (10 * v->nbytes))) / v->nbytes;
	if ((v110_buf = kmalloc(len, GFP_ATOMIC)) == NULL) {
		printk(KERN_WARNING "isdn_v110_decode: Couldn't allocate v110_buf\n");
		dev_kfree_skb(skb);
		return NULL;
	}
	for (i = 0; i < len; i++) {
		v110_buf[i] = 0;
		for (j = 0; j < v->nbytes; j++)
			v110_buf[i] |= (v->decodebuf[(i * v->nbytes) + j] & v->key) << (8 - ((j + 1) * v->nbits));
		v110_buf[i] = FlipBits(v110_buf[i], v->nbits);
	}
	v->decodelen = (v->decodelen % (10 * v->nbytes));
	memcpy(v->decodebuf, &(v->decodebuf[len * v->nbytes]), v->decodelen);

	skb_trim(skb, DecodeMatrix(v, v110_buf, len, skb->data));
	kfree(v110_buf);
	if (skb->len)
		return skb;
	else {
		kfree_skb(skb);
		return NULL;
	}
}

/* EncodeMatrix takes input data in buf, len is the bytecount.
   Data is encoded into v110 frames in m. Return value is the number of
   matrix-lines generated.
*/
static int
EncodeMatrix(unsigned char *buf, int len, unsigned char *m, int mlen)
{
	int line = 0;
	int i = 0;
	int mbit = 128;
	int dbit = 1;
	int introducer = 3;
	int ibit[] = {0, 1, 1};

	while ((i < len) && (line < mlen)) {	/* while we still have input data */
		switch (line % 10) {	/* in which line of the matrix are we? */
		case 0:
			m[line++] = 0x00;	/* line 0 is always 0 */
			mbit = 128;	/* go on with the 7th bit */
			break;
		case 5:
			m[line++] = 0xbf;	/* line 5 is always 10111111 */
			mbit = 128;	/* go on with the 7th bit */
			break;
		}
		if (line >= mlen) {
			printk(KERN_WARNING "isdn_v110 (EncodeMatrix): buffer full!\n");
			return line;
		}
	next_bit:
		switch (mbit) { /* leftmost or rightmost bit ? */
		case 1:
			line++;	/* rightmost -> go to next line */
			if (line >= mlen) {
				printk(KERN_WARNING "isdn_v110 (EncodeMatrix): buffer full!\n");
				return line;
			}
			/* fall through */
		case 128:
			m[line] = 128;	/* leftmost -> set byte to 1000000 */
			mbit = 64;	/* current bit in the matrix line */
			continue;
		}
		if (introducer) {	/* set 110 sequence ? */
			introducer--;	/* set on digit less */
			m[line] |= ibit[introducer] ? mbit : 0;	/* set corresponding bit */
			mbit >>= 1;	/* bit of matrix line  >> 1 */
			goto next_bit;	/* and go on there */
		}               /* else push data bits into the matrix! */
		m[line] |= (buf[i] & dbit) ? mbit : 0;	/* set data bit in matrix */
		if (dbit == 128) {	/* was it the last one? */
			dbit = 1;	/* then go on with first bit of  */
			i++;            /* next byte in input buffer */
			if (i < len)	/* input buffer done ? */
				introducer = 3;	/* no, write introducer 110 */
			else {  /* input buffer done ! */
				m[line] |= (mbit - 1) & 0xfe;	/* set remaining bits in line to 1 */
				break;
			}
		} else          /* not the last data bit */
			dbit <<= 1;	/* then go to next data bit */
		mbit >>= 1;     /* go to next bit of matrix */
		goto next_bit;

	}
	/* if necessary, generate remaining lines of the matrix... */
	if ((line) && ((line + 10) < mlen))
		switch (++line % 10) {
		case 1:
			m[line++] = 0xfe;
			/* fall through */
		case 2:
			m[line++] = 0xfe;
			/* fall through */
		case 3:
			m[line++] = 0xfe;
			/* fall through */
		case 4:
			m[line++] = 0xfe;
			/* fall through */
		case 5:
			m[line++] = 0xbf;
			/* fall through */
		case 6:
			m[line++] = 0xfe;
			/* fall through */
		case 7:
			m[line++] = 0xfe;
			/* fall through */
		case 8:
			m[line++] = 0xfe;
			/* fall through */
		case 9:
			m[line++] = 0xfe;
		}
	return line;            /* that's how many lines we have */
}

/*
 * Build a sync frame.
 */
static struct sk_buff *
isdn_v110_sync(isdn_v110_stream *v)
{
	struct sk_buff *skb;

	if (v == NULL) {
		/* invalid handle, no chance to proceed */
		printk(KERN_WARNING "isdn_v110_sync called with NULL stream!\n");
		return NULL;
	}
	if ((skb = dev_alloc_skb(v->framelen + v->skbres))) {
		skb_reserve(skb, v->skbres);
		skb_put_data(skb, v->OfflineFrame, v->framelen);
	}
	return skb;
}

/*
 * Build an idle frame.
 */
static struct sk_buff *
isdn_v110_idle(isdn_v110_stream *v)
{
	struct sk_buff *skb;

	if (v == NULL) {
		/* invalid handle, no chance to proceed */
		printk(KERN_WARNING "isdn_v110_sync called with NULL stream!\n");
		return NULL;
	}
	if ((skb = dev_alloc_skb(v->framelen + v->skbres))) {
		skb_reserve(skb, v->skbres);
		skb_put_data(skb, v->OnlineFrame, v->framelen);
	}
	return skb;
}

struct sk_buff *
isdn_v110_encode(isdn_v110_stream *v, struct sk_buff *skb)
{
	int i;
	int j;
	int rlen;
	int mlen;
	int olen;
	int size;
	int sval1;
	int sval2;
	int nframes;
	unsigned char *v110buf;
	unsigned char *rbuf;
	struct sk_buff *nskb;

	if (v == NULL) {
		/* invalid handle, no chance to proceed */
		printk(KERN_WARNING "isdn_v110_encode called with NULL stream!\n");
		return NULL;
	}
	if (!skb) {
		/* invalid skb, no chance to proceed */
		printk(KERN_WARNING "isdn_v110_encode called with NULL skb!\n");
		return NULL;
	}
	rlen = skb->len;
	nframes = (rlen + 3) / 4;
	v110buf = v->encodebuf;
	if ((nframes * 40) > v->maxsize) {
		size = v->maxsize;
		rlen = v->maxsize / 40;
	} else
		size = nframes * 40;
	if (!(nskb = dev_alloc_skb(size + v->skbres + sizeof(int)))) {
		printk(KERN_WARNING "isdn_v110_encode: Couldn't alloc skb\n");
		return NULL;
	}
	skb_reserve(nskb, v->skbres + sizeof(int));
	if (skb->len == 0) {
		skb_put_data(nskb, v->OnlineFrame, v->framelen);
		*((int *)skb_push(nskb, sizeof(int))) = 0;
		return nskb;
	}
	mlen = EncodeMatrix(skb->data, rlen, v110buf, size);
	/* now distribute 2 or 4 bits each to the output stream! */
	rbuf = skb_put(nskb, size);
	olen = 0;
	sval1 = 8 - v->nbits;
	sval2 = v->key << sval1;
	for (i = 0; i < mlen; i++) {
		v110buf[i] = FlipBits(v110buf[i], v->nbits);
		for (j = 0; j < v->nbytes; j++) {
			if (size--)
				*rbuf++ = ~v->key | (((v110buf[i] << (j * v->nbits)) & sval2) >> sval1);
			else {
				printk(KERN_WARNING "isdn_v110_encode: buffers full!\n");
				goto buffer_full;
			}
			olen++;
		}
	}
buffer_full:
	skb_trim(nskb, olen);
	*((int *)skb_push(nskb, sizeof(int))) = rlen;
	return nskb;
}

int
isdn_v110_stat_callback(int idx, isdn_ctrl *c)
{
	isdn_v110_stream *v = NULL;
	int i;
	int ret = 0;

	if (idx < 0)
		return 0;
	switch (c->command) {
	case ISDN_STAT_BSENT:
		/* Keep the send-queue of the driver filled
		 * with frames:
		 * If number of outstanding frames < 3,
		 * send down an Idle-Frame (or an Sync-Frame, if
		 * v->SyncInit != 0).
		 */
		if (!(v = dev->v110[idx]))
			return 0;
		atomic_inc(&dev->v110use[idx]);
		for (i = 0; i * v->framelen < c->parm.length; i++) {
			if (v->skbidle > 0) {
				v->skbidle--;
				ret = 1;
			} else {
				if (v->skbuser > 0)
					v->skbuser--;
				ret = 0;
			}
		}
		for (i = v->skbuser + v->skbidle; i < 2; i++) {
			struct sk_buff *skb;
			if (v->SyncInit > 0)
				skb = isdn_v110_sync(v);
			else
				skb = isdn_v110_idle(v);
			if (skb) {
				if (dev->drv[c->driver]->interface->writebuf_skb(c->driver, c->arg, 1, skb) <= 0) {
					dev_kfree_skb(skb);
					break;
				} else {
					if (v->SyncInit)
						v->SyncInit--;
					v->skbidle++;
				}
			} else
				break;
		}
		atomic_dec(&dev->v110use[idx]);
		return ret;
	case ISDN_STAT_DHUP:
	case ISDN_STAT_BHUP:
		while (1) {
			atomic_inc(&dev->v110use[idx]);
			if (atomic_dec_and_test(&dev->v110use[idx])) {
				isdn_v110_close(dev->v110[idx]);
				dev->v110[idx] = NULL;
				break;
			}
			mdelay(1);
		}
		break;
	case ISDN_STAT_BCONN:
		if (dev->v110emu[idx] && (dev->v110[idx] == NULL)) {
			int hdrlen = dev->drv[c->driver]->interface->hl_hdrlen;
			int maxsize = dev->drv[c->driver]->interface->maxbufsize;
			atomic_inc(&dev->v110use[idx]);
			switch (dev->v110emu[idx]) {
			case ISDN_PROTO_L2_V11096:
				dev->v110[idx] = isdn_v110_open(V110_9600, hdrlen, maxsize);
				break;
			case ISDN_PROTO_L2_V11019:
				dev->v110[idx] = isdn_v110_open(V110_19200, hdrlen, maxsize);
				break;
			case ISDN_PROTO_L2_V11038:
				dev->v110[idx] = isdn_v110_open(V110_38400, hdrlen, maxsize);
				break;
			default:;
			}
			if ((v = dev->v110[idx])) {
				while (v->SyncInit) {
					struct sk_buff *skb = isdn_v110_sync(v);
					if (dev->drv[c->driver]->interface->writebuf_skb(c->driver, c->arg, 1, skb) <= 0) {
						dev_kfree_skb(skb);
						/* Unable to send, try later */
						break;
					}
					v->SyncInit--;
					v->skbidle++;
				}
			} else
				printk(KERN_WARNING "isdn_v110: Couldn't open stream for chan %d\n", idx);
			atomic_dec(&dev->v110use[idx]);
		}
		break;
	default:
		return 0;
	}
	return 0;
}
