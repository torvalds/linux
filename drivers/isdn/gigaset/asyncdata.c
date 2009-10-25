/*
 * Common data handling layer for ser_gigaset and usb_gigaset
 *
 * Copyright (c) 2005 by Tilman Schmidt <tilman@imap.cc>,
 *                       Hansjoerg Lipp <hjlipp@web.de>,
 *                       Stefan Eilers.
 *
 * =====================================================================
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 * =====================================================================
 */

#include "gigaset.h"
#include <linux/crc-ccitt.h>
#include <linux/bitrev.h>

/* check if byte must be stuffed/escaped
 * I'm not sure which data should be encoded.
 * Therefore I will go the hard way and decode every value
 * less than 0x20, the flag sequence and the control escape char.
 */
static inline int muststuff(unsigned char c)
{
	if (c < PPP_TRANS) return 1;
	if (c == PPP_FLAG) return 1;
	if (c == PPP_ESCAPE) return 1;
	/* other possible candidates: */
	/* 0x91: XON with parity set */
	/* 0x93: XOFF with parity set */
	return 0;
}

/* == data input =========================================================== */

/* process a block of received bytes in command mode (modem response)
 * Return value:
 *	number of processed bytes
 */
static inline int cmd_loop(unsigned char c, unsigned char *src, int numbytes,
			   struct inbuf_t *inbuf)
{
	struct cardstate *cs = inbuf->cs;
	unsigned cbytes      = cs->cbytes;
	int inputstate = inbuf->inputstate;
	int startbytes = numbytes;

	for (;;) {
		cs->respdata[cbytes] = c;
		if (c == 10 || c == 13) {
			gig_dbg(DEBUG_TRANSCMD, "%s: End of Command (%d Bytes)",
				__func__, cbytes);
			cs->cbytes = cbytes;
			gigaset_handle_modem_response(cs); /* can change
							      cs->dle */
			cbytes = 0;

			if (cs->dle &&
			    !(inputstate & INS_DLE_command)) {
				inputstate &= ~INS_command;
				break;
			}
		} else {
			/* advance in line buffer, checking for overflow */
			if (cbytes < MAX_RESP_SIZE - 1)
				cbytes++;
			else
				dev_warn(cs->dev, "response too large\n");
		}

		if (!numbytes)
			break;
		c = *src++;
		--numbytes;
		if (c == DLE_FLAG &&
		    (cs->dle || inputstate & INS_DLE_command)) {
			inputstate |= INS_DLE_char;
			break;
		}
	}

	cs->cbytes = cbytes;
	inbuf->inputstate = inputstate;

	return startbytes - numbytes;
}

/* process a block of received bytes in lock mode (tty i/f)
 * Return value:
 *	number of processed bytes
 */
static inline int lock_loop(unsigned char *src, int numbytes,
			    struct inbuf_t *inbuf)
{
	struct cardstate *cs = inbuf->cs;

	gigaset_dbg_buffer(DEBUG_LOCKCMD, "received response",
			   numbytes, src);
	gigaset_if_receive(cs, src, numbytes);

	return numbytes;
}

/* process a block of received bytes in HDLC data mode
 * Collect HDLC frames, undoing byte stuffing and watching for DLE escapes.
 * When a frame is complete, check the FCS and pass valid frames to the LL.
 * If DLE is encountered, return immediately to let the caller handle it.
 * Return value:
 *	number of processed bytes
 *	numbytes (all bytes processed) on error --FIXME
 */
static inline int hdlc_loop(unsigned char c, unsigned char *src, int numbytes,
			    struct inbuf_t *inbuf)
{
	struct cardstate *cs = inbuf->cs;
	struct bc_state *bcs = inbuf->bcs;
	int inputstate = bcs->inputstate;
	__u16 fcs = bcs->fcs;
	struct sk_buff *skb = bcs->skb;
	int startbytes = numbytes;

	if (unlikely(inputstate & INS_byte_stuff)) {
		inputstate &= ~INS_byte_stuff;
		goto byte_stuff;
	}
	for (;;) {
		if (unlikely(c == PPP_ESCAPE)) {
			if (unlikely(!numbytes)) {
				inputstate |= INS_byte_stuff;
				break;
			}
			c = *src++;
			--numbytes;
			if (unlikely(c == DLE_FLAG &&
				     (cs->dle ||
				      inbuf->inputstate & INS_DLE_command))) {
				inbuf->inputstate |= INS_DLE_char;
				inputstate |= INS_byte_stuff;
				break;
			}
byte_stuff:
			c ^= PPP_TRANS;
			if (unlikely(!muststuff(c)))
				gig_dbg(DEBUG_HDLC, "byte stuffed: 0x%02x", c);
		} else if (unlikely(c == PPP_FLAG)) {
			if (unlikely(inputstate & INS_skip_frame)) {
#ifdef CONFIG_GIGASET_DEBUG
				if (!(inputstate & INS_have_data)) { /* 7E 7E */
					++bcs->emptycount;
				} else
					gig_dbg(DEBUG_HDLC,
					    "7e----------------------------");
#endif

				/* end of frame */
				gigaset_isdn_rcv_err(bcs);
				dev_kfree_skb_any(skb);
			} else if (!(inputstate & INS_have_data)) { /* 7E 7E */
#ifdef CONFIG_GIGASET_DEBUG
				++bcs->emptycount;
#endif
				break;
			} else {
				gig_dbg(DEBUG_HDLC,
					"7e----------------------------");

				/* end of frame */
				if (unlikely(fcs != PPP_GOODFCS)) {
					dev_err(cs->dev,
				"Checksum failed, %u bytes corrupted!\n",
						skb->len);
					gigaset_isdn_rcv_err(bcs);
					dev_kfree_skb_any(skb);
				} else if (likely(skb->len > 2)) {
					__skb_trim(skb, skb->len - 2);
					gigaset_skb_rcvd(bcs, skb);
				} else {
					if (skb->len) {
						dev_err(cs->dev,
					"invalid packet size (%d)\n", skb->len);
						gigaset_isdn_rcv_err(bcs);
					}
					dev_kfree_skb_any(skb);
				}
			}

			fcs = PPP_INITFCS;
			inputstate &= ~(INS_have_data | INS_skip_frame);
			if (unlikely(bcs->ignore)) {
				inputstate |= INS_skip_frame;
				skb = NULL;
			} else {
				skb = dev_alloc_skb(SBUFSIZE + cs->hw_hdr_len);
				if (skb != NULL) {
					skb_reserve(skb, cs->hw_hdr_len);
				} else {
					dev_warn(cs->dev,
						"could not allocate new skb\n");
					inputstate |= INS_skip_frame;
				}
			}

			break;
		} else if (unlikely(muststuff(c))) {
			/* Should not happen. Possible after ZDLE=1<CR><LF>. */
			gig_dbg(DEBUG_HDLC, "not byte stuffed: 0x%02x", c);
		}

		/* add character */

#ifdef CONFIG_GIGASET_DEBUG
		if (unlikely(!(inputstate & INS_have_data))) {
			gig_dbg(DEBUG_HDLC, "7e (%d x) ================",
				bcs->emptycount);
			bcs->emptycount = 0;
		}
#endif

		inputstate |= INS_have_data;

		if (likely(!(inputstate & INS_skip_frame))) {
			if (unlikely(skb->len == SBUFSIZE)) {
				dev_warn(cs->dev, "received packet too long\n");
				dev_kfree_skb_any(skb);
				skb = NULL;
				inputstate |= INS_skip_frame;
				break;
			}
			*__skb_put(skb, 1) = c;
			fcs = crc_ccitt_byte(fcs, c);
		}

		if (unlikely(!numbytes))
			break;
		c = *src++;
		--numbytes;
		if (unlikely(c == DLE_FLAG &&
			     (cs->dle ||
			      inbuf->inputstate & INS_DLE_command))) {
			inbuf->inputstate |= INS_DLE_char;
			break;
		}
	}
	bcs->inputstate = inputstate;
	bcs->fcs = fcs;
	bcs->skb = skb;
	return startbytes - numbytes;
}

/* process a block of received bytes in transparent data mode
 * Invert bytes, undoing byte stuffing and watching for DLE escapes.
 * If DLE is encountered, return immediately to let the caller handle it.
 * Return value:
 *	number of processed bytes
 *	numbytes (all bytes processed) on error --FIXME
 */
static inline int iraw_loop(unsigned char c, unsigned char *src, int numbytes,
			    struct inbuf_t *inbuf)
{
	struct cardstate *cs = inbuf->cs;
	struct bc_state *bcs = inbuf->bcs;
	int inputstate = bcs->inputstate;
	struct sk_buff *skb = bcs->skb;
	int startbytes = numbytes;

	for (;;) {
		/* add character */
		inputstate |= INS_have_data;

		if (likely(!(inputstate & INS_skip_frame))) {
			if (unlikely(skb->len == SBUFSIZE)) {
				//FIXME just pass skb up and allocate a new one
				dev_warn(cs->dev, "received packet too long\n");
				dev_kfree_skb_any(skb);
				skb = NULL;
				inputstate |= INS_skip_frame;
				break;
			}
			*__skb_put(skb, 1) = bitrev8(c);
		}

		if (unlikely(!numbytes))
			break;
		c = *src++;
		--numbytes;
		if (unlikely(c == DLE_FLAG &&
			     (cs->dle ||
			      inbuf->inputstate & INS_DLE_command))) {
			inbuf->inputstate |= INS_DLE_char;
			break;
		}
	}

	/* pass data up */
	if (likely(inputstate & INS_have_data)) {
		if (likely(!(inputstate & INS_skip_frame))) {
			gigaset_skb_rcvd(bcs, skb);
		}
		inputstate &= ~(INS_have_data | INS_skip_frame);
		if (unlikely(bcs->ignore)) {
			inputstate |= INS_skip_frame;
			skb = NULL;
		} else {
			skb = dev_alloc_skb(SBUFSIZE + cs->hw_hdr_len);
			if (skb != NULL) {
				skb_reserve(skb, cs->hw_hdr_len);
			} else {
				dev_warn(cs->dev,
					 "could not allocate new skb\n");
				inputstate |= INS_skip_frame;
			}
		}
	}

	bcs->inputstate = inputstate;
	bcs->skb = skb;
	return startbytes - numbytes;
}

/**
 * gigaset_m10x_input() - process a block of data received from the device
 * @inbuf:	received data and device descriptor structure.
 *
 * Called by hardware module {ser,usb}_gigaset with a block of received
 * bytes. Separates the bytes received over the serial data channel into
 * user data and command replies (locked/unlocked) according to the
 * current state of the interface.
 */
void gigaset_m10x_input(struct inbuf_t *inbuf)
{
	struct cardstate *cs;
	unsigned tail, head, numbytes;
	unsigned char *src, c;
	int procbytes;

	head = inbuf->head;
	tail = inbuf->tail;
	gig_dbg(DEBUG_INTR, "buffer state: %u -> %u", head, tail);

	if (head != tail) {
		cs = inbuf->cs;
		src = inbuf->data + head;
		numbytes = (head > tail ? RBUFSIZE : tail) - head;
		gig_dbg(DEBUG_INTR, "processing %u bytes", numbytes);

		while (numbytes) {
			if (cs->mstate == MS_LOCKED) {
				procbytes = lock_loop(src, numbytes, inbuf);
				src += procbytes;
				numbytes -= procbytes;
			} else {
				c = *src++;
				--numbytes;
				if (c == DLE_FLAG && (cs->dle ||
				    inbuf->inputstate & INS_DLE_command)) {
					if (!(inbuf->inputstate & INS_DLE_char)) {
						inbuf->inputstate |= INS_DLE_char;
						goto nextbyte;
					}
					/* <DLE> <DLE> => <DLE> in data stream */
					inbuf->inputstate &= ~INS_DLE_char;
				}

				if (!(inbuf->inputstate & INS_DLE_char)) {

					/* FIXME use function pointers?  */
					if (inbuf->inputstate & INS_command)
						procbytes = cmd_loop(c, src, numbytes, inbuf);
					else if (inbuf->bcs->proto2 == L2_HDLC)
						procbytes = hdlc_loop(c, src, numbytes, inbuf);
					else
						procbytes = iraw_loop(c, src, numbytes, inbuf);

					src += procbytes;
					numbytes -= procbytes;
				} else {  /* DLE char */
					inbuf->inputstate &= ~INS_DLE_char;
					switch (c) {
					case 'X': /*begin of command*/
						if (inbuf->inputstate & INS_command)
							dev_warn(cs->dev,
					"received <DLE> 'X' in command mode\n");
						inbuf->inputstate |=
							INS_command | INS_DLE_command;
						break;
					case '.': /*end of command*/
						if (!(inbuf->inputstate & INS_command))
							dev_warn(cs->dev,
					"received <DLE> '.' in hdlc mode\n");
						inbuf->inputstate &= cs->dle ?
							~(INS_DLE_command|INS_command)
							: ~INS_DLE_command;
						break;
					//case DLE_FLAG: /*DLE_FLAG in data stream*/ /* schon oben behandelt! */
					default:
						dev_err(cs->dev,
						      "received 0x10 0x%02x!\n",
							(int) c);
						/* FIXME: reset driver?? */
					}
				}
			}
nextbyte:
			if (!numbytes) {
				/* end of buffer, check for wrap */
				if (head > tail) {
					head = 0;
					src = inbuf->data;
					numbytes = tail;
				} else {
					head = tail;
					break;
				}
			}
		}

		gig_dbg(DEBUG_INTR, "setting head to %u", head);
		inbuf->head = head;
	}
}
EXPORT_SYMBOL_GPL(gigaset_m10x_input);


/* == data output ========================================================== */

/*
 * Encode a data packet into an octet stuffed HDLC frame with FCS,
 * opening and closing flags, preserving headroom data.
 * parameters:
 *	skb		skb containing original packet (freed upon return)
 * Return value:
 *	pointer to newly allocated skb containing the result frame
 *	and the original link layer header, NULL on error
 */
static struct sk_buff *HDLC_Encode(struct sk_buff *skb)
{
	struct sk_buff *hdlc_skb;
	__u16 fcs;
	unsigned char c;
	unsigned char *cp;
	int len;
	unsigned int stuf_cnt;

	stuf_cnt = 0;
	fcs = PPP_INITFCS;
	cp = skb->data;
	len = skb->len;
	while (len--) {
		if (muststuff(*cp))
			stuf_cnt++;
		fcs = crc_ccitt_byte(fcs, *cp++);
	}
	fcs ^= 0xffff;			/* complement */

	/* size of new buffer: original size + number of stuffing bytes
	 * + 2 bytes FCS + 2 stuffing bytes for FCS (if needed) + 2 flag bytes
	 * + room for link layer header
	 */
	hdlc_skb = dev_alloc_skb(skb->len + stuf_cnt + 6 + skb->mac_len);
	if (!hdlc_skb) {
		dev_kfree_skb_any(skb);
		return NULL;
	}

	/* Copy link layer header into new skb */
	skb_reset_mac_header(hdlc_skb);
	skb_reserve(hdlc_skb, skb->mac_len);
	memcpy(skb_mac_header(hdlc_skb), skb_mac_header(skb), skb->mac_len);
	hdlc_skb->mac_len = skb->mac_len;

	/* Add flag sequence in front of everything.. */
	*(skb_put(hdlc_skb, 1)) = PPP_FLAG;

	/* Perform byte stuffing while copying data. */
	while (skb->len--) {
		if (muststuff(*skb->data)) {
			*(skb_put(hdlc_skb, 1)) = PPP_ESCAPE;
			*(skb_put(hdlc_skb, 1)) = (*skb->data++) ^ PPP_TRANS;
		} else
			*(skb_put(hdlc_skb, 1)) = *skb->data++;
	}

	/* Finally add FCS (byte stuffed) and flag sequence */
	c = (fcs & 0x00ff);	/* least significant byte first */
	if (muststuff(c)) {
		*(skb_put(hdlc_skb, 1)) = PPP_ESCAPE;
		c ^= PPP_TRANS;
	}
	*(skb_put(hdlc_skb, 1)) = c;

	c = ((fcs >> 8) & 0x00ff);
	if (muststuff(c)) {
		*(skb_put(hdlc_skb, 1)) = PPP_ESCAPE;
		c ^= PPP_TRANS;
	}
	*(skb_put(hdlc_skb, 1)) = c;

	*(skb_put(hdlc_skb, 1)) = PPP_FLAG;

	dev_kfree_skb_any(skb);
	return hdlc_skb;
}

/*
 * Encode a data packet into an octet stuffed raw bit inverted frame,
 * preserving headroom data.
 * parameters:
 *	skb		skb containing original packet (freed upon return)
 * Return value:
 *	pointer to newly allocated skb containing the result frame
 *	and the original link layer header, NULL on error
 */
static struct sk_buff *iraw_encode(struct sk_buff *skb)
{
	struct sk_buff *iraw_skb;
	unsigned char c;
	unsigned char *cp;
	int len;

	/* size of new buffer (worst case = every byte must be stuffed):
	 * 2 * original size + room for link layer header
	 */
	iraw_skb = dev_alloc_skb(2*skb->len + skb->mac_len);
	if (!iraw_skb) {
		dev_kfree_skb_any(skb);
		return NULL;
	}

	/* copy link layer header into new skb */
	skb_reset_mac_header(iraw_skb);
	skb_reserve(iraw_skb, skb->mac_len);
	memcpy(skb_mac_header(iraw_skb), skb_mac_header(skb), skb->mac_len);
	iraw_skb->mac_len = skb->mac_len;

	/* copy and stuff data */
	cp = skb->data;
	len = skb->len;
	while (len--) {
		c = bitrev8(*cp++);
		if (c == DLE_FLAG)
			*(skb_put(iraw_skb, 1)) = c;
		*(skb_put(iraw_skb, 1)) = c;
	}
	dev_kfree_skb_any(skb);
	return iraw_skb;
}

/**
 * gigaset_m10x_send_skb() - queue an skb for sending
 * @bcs:	B channel descriptor structure.
 * @skb:	data to send.
 *
 * Called by LL to encode and queue an skb for sending, and start
 * transmission if necessary.
 * Once the payload data has been transmitted completely, gigaset_skb_sent()
 * will be called with the skb's link layer header preserved.
 *
 * Return value:
 *	number of bytes accepted for sending (skb->len) if ok,
 *	error code < 0 (eg. -ENOMEM) on error
 */
int gigaset_m10x_send_skb(struct bc_state *bcs, struct sk_buff *skb)
{
	unsigned len = skb->len;
	unsigned long flags;

	if (bcs->proto2 == L2_HDLC)
		skb = HDLC_Encode(skb);
	else
		skb = iraw_encode(skb);
	if (!skb) {
		dev_err(bcs->cs->dev,
			"unable to allocate memory for encoding!\n");
		return -ENOMEM;
	}

	skb_queue_tail(&bcs->squeue, skb);
	spin_lock_irqsave(&bcs->cs->lock, flags);
	if (bcs->cs->connected)
		tasklet_schedule(&bcs->cs->write_tasklet);
	spin_unlock_irqrestore(&bcs->cs->lock, flags);

	return len;	/* ok so far */
}
EXPORT_SYMBOL_GPL(gigaset_m10x_send_skb);
