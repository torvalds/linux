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

/*
 * Exported from dhd bus module (dhd_usb, dhd_sdio)
 */

/* Indicate (dis)interest in finding dongles. */
extern int dhd_bus_register(void);
extern void dhd_bus_unregister(void);

/* Download firmware image and nvram image */
extern bool dhd_bus_download_firmware(struct dhd_bus *bus, osl_t * osh,
				      char *fw_path, char *nv_path);

/* Stop bus module: clear pending frames, disable data flow */
extern void dhd_bus_stop(struct dhd_bus *bus, bool enforce_mutex);

/* Initialize bus module: prepare for communication w/dongle */
extern int dhd_bus_init(dhd_pub_t *dhdp, bool enforce_mutex);

/* Send a data frame to the dongle.  Callee disposes of txp. */
extern int dhd_bus_txdata(struct dhd_bus *bus, void *txp);

/* Send/receive a control message to/from the dongle.
 * Expects caller to enforce a single outstanding transaction.
 */
extern int dhd_bus_txctl(struct dhd_bus *bus, unsigned char *msg, uint msglen);
extern int dhd_bus_rxctl(struct dhd_bus *bus, unsigned char *msg, uint msglen);

/* Watchdog timer function */
extern bool dhd_bus_watchdog(dhd_pub_t *dhd);

#ifdef DHD_DEBUG
/* Device console input function */
extern int dhd_bus_console_in(dhd_pub_t *dhd, unsigned char *msg, uint msglen);
#endif				/* DHD_DEBUG */

/* Deferred processing for the bus, return TRUE requests reschedule */
extern bool dhd_bus_dpc(struct dhd_bus *bus);
extern void dhd_bus_isr(bool *InterruptRecognized,
			bool *QueueMiniportHandleInterrupt, void *arg);

/* Check for and handle local prot-specific iovar commands */
extern int dhd_bus_iovar_op(dhd_pub_t *dhdp, const char *name,
			    void *params, int plen, void *arg, int len,
			    bool set);

/* Add bus dump output to a buffer */
extern void dhd_bus_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf);

/* Clear any bus counters */
extern void dhd_bus_clearcounts(dhd_pub_t *dhdp);

/* return the dongle chipid */
extern uint dhd_bus_chip(struct dhd_bus *bus);

/* Set user-specified nvram parameters. */
extern void dhd_bus_set_nvram_params(struct dhd_bus *bus,
				     const char *nvram_params);

extern void *dhd_bus_pub(struct dhd_bus *bus);
extern void *dhd_bus_txq(struct dhd_bus *bus);
extern uint dhd_bus_hdrlen(struct dhd_bus *bus);

#endif				/* _dhd_bus_h_ */
