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

#ifndef _dhd_bus_h_
#define _dhd_bus_h_

/* Packet alignment for most efficient SDIO (can change based on platform) */
#ifndef BRCMF_SDALIGN
#define BRCMF_SDALIGN	32
#endif
#if !ISPOWEROF2(BRCMF_SDALIGN)
#error BRCMF_SDALIGN is not a power of 2!
#endif

/*
 * Exported from dhd bus module (dhd_usb, dhd_sdio)
 */

/* Watchdog timer interval */
extern uint brcmf_watchdog_ms;

/* Indicate (dis)interest in finding dongles. */
extern int dhd_bus_register(void);
extern void dhd_bus_unregister(void);

/* Stop bus module: clear pending frames, disable data flow */
extern void brcmf_sdbrcm_bus_stop(struct dhd_bus *bus, bool enforce_mutex);

/* Initialize bus module: prepare for communication w/dongle */
extern int brcmf_sdbrcm_bus_init(struct brcmf_pub *dhdp, bool enforce_mutex);

/* Send a data frame to the dongle.  Callee disposes of txp. */
extern int brcmf_sdbrcm_bus_txdata(struct dhd_bus *bus, struct sk_buff *txp);

/* Send/receive a control message to/from the dongle.
 * Expects caller to enforce a single outstanding transaction.
 */
extern int
brcmf_sdbrcm_bus_txctl(struct dhd_bus *bus, unsigned char *msg, uint msglen);

extern int
brcmf_sdbrcm_bus_rxctl(struct dhd_bus *bus, unsigned char *msg, uint msglen);

extern void dhd_bus_isr(bool *InterruptRecognized,
			bool *QueueMiniportHandleInterrupt, void *arg);

/* Check for and handle local prot-specific iovar commands */
extern int brcmf_sdbrcm_bus_iovar_op(struct brcmf_pub *dhdp, const char *name,
			    void *params, int plen, void *arg, int len,
			    bool set);

/* Add bus dump output to a buffer */
extern void brcmf_sdbrcm_bus_dump(struct brcmf_pub *dhdp,
				  struct brcmu_strbuf *strbuf);

/* Clear any bus counters */
extern void dhd_bus_clearcounts(struct brcmf_pub *dhdp);

/* return the dongle chipid */
extern uint dhd_bus_chip(struct dhd_bus *bus);

extern void *dhd_bus_pub(struct dhd_bus *bus);
extern void *dhd_bus_txq(struct dhd_bus *bus);
extern uint dhd_bus_hdrlen(struct dhd_bus *bus);

extern void brcmf_sdbrcm_wd_timer(struct dhd_bus *bus, uint wdtick);

#endif				/* _dhd_bus_h_ */
