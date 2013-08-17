/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * -----------------------------------------------------------------------------
 */
#ifndef _OZUSBSVC_H
#define _OZUSBSVC_H

/*------------------------------------------------------------------------------
 * Per PD context info stored in application context area of PD.
 * This object is reference counted to ensure it doesn't disappear while
 * still in use.
 */
struct oz_usb_ctx {
	atomic_t ref_count;
	u8 tx_seq_num;
	u8 rx_seq_num;
	struct oz_pd *pd;
	void *hport;
	int stopped;
};

int oz_usb_init(void);
void oz_usb_term(void);
int oz_usb_start(struct oz_pd *pd, int resume);
void oz_usb_stop(struct oz_pd *pd, int pause);
void oz_usb_rx(struct oz_pd *pd, struct oz_elt *elt);
int oz_usb_heartbeat(struct oz_pd *pd);
void oz_usb_farewell(struct oz_pd *pd, u8 ep_num, u8 *data, u8 len);

#endif /* _OZUSBSVC_H */

