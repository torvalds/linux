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
#include <linux/export.h>

/* check if byte must be stuffed/escaped
 * I'm not sure which data should be encoded.
 * Therefore I will go the hard way and encode every value
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

/* process a block of received bytes in command mode
 * (mstate != MS_LOCKED && (inputstate & INS_command))
 * Append received bytes to the command response buffer and forward them
 * line by line to the response handler. Exit whenever a mode/state change
 * might have occurred.
 * Note: Received lines may be terminated by CR, LF, or CR LF, which will be
 * removed before passing the line to the response handler.
 * Return value:
 *	number of processed bytes
 */
static unsigned cmd_loop(unsigned numbytes, struct inbuf_t *inbuf)
{
	unsigned char *src = inbuf->data + inbuf->head;
	struct cardstate *cs = inbuf->cs;
	unsigned cbytes = cs->cbytes;
	unsigned procbytes = 0;
	unsigned char c;

	while (procbytes < numbytes) {
		c = *src++;
		procbytes++;

		switch (c) {
		case '\n':
			if (cbytes == 0 && cs->respdata[0] == '\r') {
				/* collapse LF with preceding CR */
				cs->respdata[0] = 0;
				break;
			}
			/* --v-- fall through --v-- */
		case '\r':
			/* end of message line, pass to response handler */
			if (cbytes >= MAX_RESP_SIZE) {
				dev_warn(cs->dev, "response too large (%d)\n",
					 cbytes);
				cbytes = MAX_RESP_SIZE;
			}
			cs->cbytes = cbytes;
			gigaset_dbg_buffer(DEBUG_TRANSCMD, "received response",
					   cbytes, cs->respdata);
			gigaset_handle_modem_response(cs);
			cbytes = 0;

			/* store EOL byte for CRLF collapsing */
			cs->respdata[0] = c;

			/* cs->dle may have changed */
			if (cs->dle && !(inbuf->inputstate & INS_DLE_command))
				inbuf->inputstate &= ~INS_command;

			/* return for reevaluating state */
			goto exit;

		case DLE_FLAG:
			if (inbuf->inputstate & INS_DLE_char) {
				/* quoted DLE: clear quote flag */
				inbuf->inputstate &= ~INS_DLE_char;
			} else if (cs->dle ||
				   (inbuf->inputstate & INS_DLE_command)) {
				/* DLE escape, pass up for handling */
				inbuf->inputstate |= INS_DLE_char;
				goto exit;
			}
			/* quoted or not in DLE mode: treat as regular data */
			/* --v-- fall through --v-- */
		default:
			/* append to line buffer if possible */
			if (cbytes < MAX_RESP_SIZE)
				cs->respdata[cbytes] = c;
			cbytes++;
		}
	}
exit:
	cs->cbytes = cbytes;
	return procbytes;
}

/* process a block of received bytes in lock mode
 * All received bytes are passed unmodified to the tty i/f.
 * Return value:
 *	number of processed bytes
 */
static unsigned lock_loop(unsigned numbytes, struct inbuf_t *inbuf)
{
	unsigned char *src = inbuf->data + inbuf->head;

	gigaset_dbg_buffer(DEBUG_LOCKCMD, "received response", numbytes, src);
	gigaset_if_receive(inbuf->cs, src, numbytes);
	return numbytes;
}

/* process a block of received bytes in HDLC data mode
 * (mstate != MS_LOCKED && !(inputstate & INS_command) && proto2 == L2_HDLC)
 * Collect HDLC frames, undoing byte stuffing and watching for DLE escapes.
 * When a frame is complete, check the FCS and pass valid frames to the LL.
 * If DLE is encountered, return immediately to let the caller handle it.
 * Return value:
 *	number of processed bytes
 */
static unsigned hdlc_loop(unsigned numbytes, struct inbuf_t *inbuf)
{
	struct cardstate *cs = inbuf->cs;
	struct bc_state *bcs = cs->bcs;
	int inputstate = bcs->inputstate;
	__u16 fcs = bcs->rx_fcs;
	struct sk_buff *skb = bcs->rx_skb;
	unsigned char *src = inbuf->data + inbuf->head;
	unsigned procbytes = 0;
	unsigned char c;

	if (inputstate & INS_byte_stuff) {
		if (!numbytes)
			return 0;
		inputstate &= ~INS_byte_stuff;
		goto byte_stuff;
	}

	while (procbytes < numbytes) {
		c = *src++;
		procbytes++;
		if (c == DLE_FLAG) {
			if (inputstate & INS_DLE_char) {
				/* quoted DLE: clear quote flag */
				inputstate &= ~INS_DLE_char;
			} else if (cs->dle || (inputstate & INS_DLE_command)) {
				/* DLE escape, pass up for handling */
				inputstate |= INS_DLE_char;
				break;
			}
		}

		if (c == PPP_ESCAPE) {
			/* byte stuffing indicator: pull in next byte */
			if (procbytes >= numbytes) {
				/* end of buffer, save for later processing */
				inputstate |= INS_byte_stuff;
				break;
			}
byte_stuff:
			c = *src++;
			procbytes++;
			if (c == DLE_FLAG) {
				if (inputstate & INS_DLE_char) {
					/* quoted DLE: clear quote flag */
					inputstate &= ~INS_DLE_char;
				} else if (cs->dle ||
					   (inputstate & INS_DLE_command)) {
					/* DLE escape, pass up for handling */
					inputstate |=
						INS_DLE_char | INS_byte_stuff;
					break;
				}
			}
			c ^= PPP_TRANS;
#ifdef CONFIG_GIGASET_DEBUG
			if (!muststuff(c))
				gig_dbg(DEBUG_HDLC, "byte stuffed: 0x%02x", c);
#endif
		} else if (c == PPP_FLAG) {
			/* end of frame: process content if any */
			if (inputstate & INS_have_data) {
				gig_dbg(DEBUG_HDLC,
					"7e----------------------------");

				/* check and pass received frame */
				if (!skb) {
					/* skipped frame */
					gigaset_isdn_rcv_err(bcs);
				} else if (skb->len < 2) {
					/* frame too short for FCS */
					dev_warn(cs->dev,
						 "short frame (%d)\n",
						 skb->len);
					gigaset_isdn_rcv_err(bcs);
					dev_kfree_skb_any(skb);
				} else if (fcs != PPP_GOODFCS) {
					/* frame check error */
					dev_err(cs->dev,
						"Checksum failed, %u bytes corrupted!\n",
						skb->len);
					gigaset_isdn_rcv_err(bcs);
					dev_kfree_skb_any(skb);
				} else {
					/* good frame */
					__skb_trim(skb, skb->len - 2);
					gigaset_skb_rcvd(bcs, skb);
				}

				/* prepare reception of next frame */
				inputstate &= ~INS_have_data;
				skb = gigaset_new_rx_skb(bcs);
			} else {
				/* empty frame (7E 7E) */
#ifdef CONFIG_GIGASET_DEBUG
				++bcs->emptycount;
#endif
				if (!skb) {
					/* skipped (?) */
					gigaset_isdn_rcv_err(bcs);
					skb = gigaset_new_rx_skb(bcs);
				}
			}

			fcs = PPP_INITFCS;
			continue;
#ifdef CONFIG_GIGASET_DEBUG
		} else if (muststuff(c)) {
			/* Should not happen. Possible after ZDLE=1<CR><LF>. */
			gig_dbg(DEBUG_HDLC, "not byte stuffed: 0x%02x", c);
#endif
		}

		/* regular data byte, append to skb */
#ifdef CONFIG_GIGASET_DEBUG
		if (!(inputstate & INS_have_data)) {
			gig_dbg(DEBUG_HDLC, "7e (%d x) ================",
				bcs->emptycount);
			bcs->emptycount = 0;
		}
#endif
		inputstate |= INS_have_data;
		if (skb) {
			if (skb->len >= bcs->rx_bufsize) {
				dev_warn(cs->dev, "received packet too long\n");
				dev_kfree_skb_any(skb);
				/* skip remainder of packet */
				bcs->rx_skb = skb = NULL;
			} else {
				*(u8 *)__skb_put(skb, 1) = c;
				fcs = crc_ccitt_byte(fcs, c);
			}
		}
	}

	bcs->inputstate = inputstate;
	bcs->rx_fcs = fcs;
	return procbytes;
}

/* process a block of received bytes in transparent data mode
 * (mstate != MS_LOCKED && !(inputstate & INS_command) && proto2 != L2_HDLC)
 * Invert bytes, undoing byte stuffing and watching for DLE escapes.
 * If DLE is encountered, return immediately to let the caller handle it.
 * Return value:
 *	number of processed bytes
 */
static unsigned iraw_loop(unsigned numbytes, struct inbuf_t *inbuf)
{
	struct cardstate *cs = inbuf->cs;
	struct bc_state *bcs = cs->bcs;
	int inputstate = bcs->inputstate;
	struct sk_buff *skb = bcs->rx_skb;
	unsigned char *src = inbuf->data + inbuf->head;
	unsigned procbytes = 0;
	unsigned char c;

	if (!skb) {
		/* skip this block */
		gigaset_new_rx_skb(bcs);
		return numbytes;
	}

	while (procbytes < numbytes && skb->len < bcs->rx_bufsize) {
		c = *src++;
		procbytes++;

		if (c == DLE_FLAG) {
			if (inputstate & INS_DLE_char) {
				/* quoted DLE: clear quote flag */
				inputstate &= ~INS_DLE_char;
			} else if (cs->dle || (inputstate & INS_DLE_command)) {
				/* DLE escape, pass up for handling */
				inputstate |= INS_DLE_char;
				break;
			}
		}

		/* regular data byte: append to current skb */
		inputstate |= INS_have_data;
		*(u8 *)__skb_put(skb, 1) = bitrev8(c);
	}

	/* pass data up */
	if (inputstate & INS_have_data) {
		gigaset_skb_rcvd(bcs, skb);
		inputstate &= ~INS_have_data;
		gigaset_new_rx_skb(bcs);
	}

	bcs->inputstate = inputstate;
	return procbytes;
}

/* process DLE escapes
 * Called whenever a DLE sequence might be encountered in the input stream.
 * Either processes the entire DLE sequence or, if that isn't possible,
 * notes the fact that an initial DLE has been received in the INS_DLE_char
 * inputstate flag and resumes processing of the sequence on the next call.
 */
static void handle_dle(struct inbuf_t *inbuf)
{
	struct cardstate *cs = inbuf->cs;

	if (cs->mstate == MS_LOCKED)
		return;		/* no DLE processing in lock mode */

	if (!(inbuf->inputstate & INS_DLE_char)) {
		/* no DLE pending */
		if (inbuf->data[inbuf->head] == DLE_FLAG &&
		    (cs->dle || inbuf->inputstate & INS_DLE_command)) {
			/* start of DLE sequence */
			inbuf->head++;
			if (inbuf->head == inbuf->tail ||
			    inbuf->head == RBUFSIZE) {
				/* end of buffer, save for later processing */
				inbuf->inputstate |= INS_DLE_char;
				return;
			}
		} else {
			/* regular data byte */
			return;
		}
	}

	/* consume pending DLE */
	inbuf->inputstate &= ~INS_DLE_char;

	switch (inbuf->data[inbuf->head]) {
	case 'X':	/* begin of event message */
		if (inbuf->inputstate & INS_command)
			dev_notice(cs->dev,
				   "received <DLE>X in command mode\n");
		inbuf->inputstate |= INS_command | INS_DLE_command;
		inbuf->head++;	/* byte consumed */
		break;
	case '.':	/* end of event message */
		if (!(inbuf->inputstate & INS_DLE_command))
			dev_notice(cs->dev,
				   "received <DLE>. without <DLE>X\n");
		inbuf->inputstate &= ~INS_DLE_command;
		/* return to data mode if in DLE mode */
		if (cs->dle)
			inbuf->inputstate &= ~INS_command;
		inbuf->head++;	/* byte consumed */
		break;
	case DLE_FLAG:	/* DLE in data stream */
		/* mark as quoted */
		inbuf->inputstate |= INS_DLE_char;
		if (!(cs->dle || inbuf->inputstate & INS_DLE_command))
			dev_notice(cs->dev,
				   "received <DLE><DLE> not in DLE mode\n");
		break;	/* quoted byte left in buffer */
	default:
		dev_notice(cs->dev, "received <DLE><%02x>\n",
			   inbuf->data[inbuf->head]);
		/* quoted byte left in buffer */
	}
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
	struct cardstate *cs = inbuf->cs;
	unsigned numbytes, procbytes;

	gig_dbg(DEBUG_INTR, "buffer state: %u -> %u", inbuf->head, inbuf->tail);

	while (inbuf->head != inbuf->tail) {
		/* check for DLE escape */
		handle_dle(inbuf);

		/* process a contiguous block of bytes */
		numbytes = (inbuf->head > inbuf->tail ?
			    RBUFSIZE : inbuf->tail) - inbuf->head;
		gig_dbg(DEBUG_INTR, "processing %u bytes", numbytes);
		/*
		 * numbytes may be 0 if handle_dle() ate the last byte.
		 * This does no harm, *_loop() will just return 0 immediately.
		 */

		if (cs->mstate == MS_LOCKED)
			procbytes = lock_loop(numbytes, inbuf);
		else if (inbuf->inputstate & INS_command)
			procbytes = cmd_loop(numbytes, inbuf);
		else if (cs->bcs->proto2 == L2_HDLC)
			procbytes = hdlc_loop(numbytes, inbuf);
		else
			procbytes = iraw_loop(numbytes, inbuf);
		inbuf->head += procbytes;

		/* check for buffer wraparound */
		if (inbuf->head >= RBUFSIZE)
			inbuf->head = 0;

		gig_dbg(DEBUG_INTR, "head set to %u", inbuf->head);
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
	*(u8 *)skb_put(hdlc_skb, 1) = PPP_FLAG;

	/* Perform byte stuffing while copying data. */
	while (skb->len--) {
		if (muststuff(*skb->data)) {
			*(u8 *)skb_put(hdlc_skb, 1) = PPP_ESCAPE;
			*(u8 *)skb_put(hdlc_skb, 1) = (*skb->data++) ^ PPP_TRANS;
		} else
			*(u8 *)skb_put(hdlc_skb, 1) = *skb->data++;
	}

	/* Finally add FCS (byte stuffed) and flag sequence */
	c = (fcs & 0x00ff);	/* least significant byte first */
	if (muststuff(c)) {
		*(u8 *)skb_put(hdlc_skb, 1) = PPP_ESCAPE;
		c ^= PPP_TRANS;
	}
	*(u8 *)skb_put(hdlc_skb, 1) = c;

	c = ((fcs >> 8) & 0x00ff);
	if (muststuff(c)) {
		*(u8 *)skb_put(hdlc_skb, 1) = PPP_ESCAPE;
		c ^= PPP_TRANS;
	}
	*(u8 *)skb_put(hdlc_skb, 1) = c;

	*(u8 *)skb_put(hdlc_skb, 1) = PPP_FLAG;

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
	iraw_skb = dev_alloc_skb(2 * skb->len + skb->mac_len);
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
			*(u8 *)skb_put(iraw_skb, 1) = c;
		*(u8 *)skb_put(iraw_skb, 1) = c;
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
	struct cardstate *cs = bcs->cs;
	unsigned len = skb->len;
	unsigned long flags;

	if (bcs->proto2 == L2_HDLC)
		skb = HDLC_Encode(skb);
	else
		skb = iraw_encode(skb);
	if (!skb) {
		dev_err(cs->dev,
			"unable to allocate memory for encoding!\n");
		return -ENOMEM;
	}

	skb_queue_tail(&bcs->squeue, skb);
	spin_lock_irqsave(&cs->lock, flags);
	if (cs->connected)
		tasklet_schedule(&cs->write_tasklet);
	spin_unlock_irqrestore(&cs->lock, flags);

	return len;	/* ok so far */
}
EXPORT_SYMBOL_GPL(gigaset_m10x_send_skb);
