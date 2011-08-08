/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _BRCMF_BUS_H_
#define _BRCMF_BUS_H_

/* Packet alignment for most efficient SDIO (can change based on platform) */
#ifndef BRCMF_SDALIGN
#define BRCMF_SDALIGN	32
#endif
#if !ISPOWEROF2(BRCMF_SDALIGN)
#error BRCMF_SDALIGN is not a power of 2!
#endif

/*
 * Exported from brcmf bus module (brcmf_usb, brcmf_sdio)
 */

/* dongle ram module parameter */
extern int brcmf_dongle_memsize;

/* Tx/Rx bounds module parameters */
extern uint brcmf_txbound;
extern uint brcmf_rxbound;

/* Watchdog timer interval */
extern uint brcmf_watchdog_ms;

/* Indicate (dis)interest in finding dongles. */
extern int brcmf_bus_register(void);
extern void brcmf_bus_unregister(void);

/* Stop bus module: clear pending frames, disable data flow */
extern void brcmf_sdbrcm_bus_stop(struct brcmf_bus *bus, bool enforce_mutex);

/* Initialize bus module: prepare for communication w/dongle */
extern int brcmf_sdbrcm_bus_init(struct brcmf_pub *drvr, bool enforce_mutex);

/* Send a data frame to the dongle.  Callee disposes of txp. */
extern int brcmf_sdbrcm_bus_txdata(struct brcmf_bus *bus, struct sk_buff *txp);

/* Send/receive a control message to/from the dongle.
 * Expects caller to enforce a single outstanding transaction.
 */
extern int
brcmf_sdbrcm_bus_txctl(struct brcmf_bus *bus, unsigned char *msg, uint msglen);

extern int
brcmf_sdbrcm_bus_rxctl(struct brcmf_bus *bus, unsigned char *msg, uint msglen);

/* Check for and handle local prot-specific iovar commands */
extern int brcmf_sdbrcm_bus_iovar_op(struct brcmf_pub *drvr, const char *name,
			    void *params, int plen, void *arg, int len,
			    bool set);

/* Add bus dump output to a buffer */
extern void brcmf_sdbrcm_bus_dump(struct brcmf_pub *drvr,
				  struct brcmu_strbuf *strbuf);

/* Clear any bus counters */
extern void brcmf_bus_clearcounts(struct brcmf_pub *drvr);

extern void brcmf_sdbrcm_wd_timer(struct brcmf_bus *bus, uint wdtick);

#endif				/* _BRCMF_BUS_H_ */
