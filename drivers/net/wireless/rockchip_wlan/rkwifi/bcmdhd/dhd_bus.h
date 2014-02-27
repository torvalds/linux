/*
 * Header file describing the internal (inter-module) DHD interfaces.
 *
 * Provides type definitions and function prototypes used to link the
 * DHD OS, bus, and protocol modules.
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: dhd_bus.h 335569 2012-05-29 12:04:43Z $
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
extern bool dhd_bus_download_firmware(struct dhd_bus *bus, osl_t *osh,
	char *fw_path, char *nv_path, char *conf_path);

/* Stop bus module: clear pending frames, disable data flow */
extern void dhd_bus_stop(struct dhd_bus *bus, bool enforce_mutex);

/* Initialize bus module: prepare for communication w/dongle */
extern int dhd_bus_init(dhd_pub_t *dhdp, bool enforce_mutex);

/* Get the Bus Idle Time */
extern void dhd_bus_getidletime(dhd_pub_t *dhdp, int *idletime);

/* Set the Bus Idle Time */
extern void dhd_bus_setidletime(dhd_pub_t *dhdp, int idle_time);

/* Send a data frame to the dongle.  Callee disposes of txp. */
extern int dhd_bus_txdata(struct dhd_bus *bus, void *txp);

/* Send/receive a control message to/from the dongle.
 * Expects caller to enforce a single outstanding transaction.
 */
extern int dhd_bus_txctl(struct dhd_bus *bus, uchar *msg, uint msglen);
extern int dhd_bus_rxctl(struct dhd_bus *bus, uchar *msg, uint msglen);

/* Watchdog timer function */
extern bool dhd_bus_watchdog(dhd_pub_t *dhd);
extern void dhd_disable_intr(dhd_pub_t *dhd);

#if defined(DHD_DEBUG)
/* Device console input function */
extern int dhd_bus_console_in(dhd_pub_t *dhd, uchar *msg, uint msglen);
#endif /* defined(DHD_DEBUG) */

/* Deferred processing for the bus, return TRUE requests reschedule */
extern bool dhd_bus_dpc(struct dhd_bus *bus);
extern void dhd_bus_isr(bool * InterruptRecognized, bool * QueueMiniportHandleInterrupt, void *arg);


/* Check for and handle local prot-specific iovar commands */
extern int dhd_bus_iovar_op(dhd_pub_t *dhdp, const char *name,
                            void *params, int plen, void *arg, int len, bool set);

/* Add bus dump output to a buffer */
extern void dhd_bus_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf);

/* Clear any bus counters */
extern void dhd_bus_clearcounts(dhd_pub_t *dhdp);

/* return the dongle chipid */
extern uint dhd_bus_chip(struct dhd_bus *bus);

/* Set user-specified nvram parameters. */
extern void dhd_bus_set_nvram_params(struct dhd_bus * bus, const char *nvram_params);

extern void *dhd_bus_pub(struct dhd_bus *bus);
extern void *dhd_bus_txq(struct dhd_bus *bus);
extern uint dhd_bus_hdrlen(struct dhd_bus *bus);


#define DHD_SET_BUS_STATE_DOWN(_bus)  do { \
	(_bus)->dhd->busstate = DHD_BUS_DOWN; \
} while (0)

/* Register a dummy SDIO client driver in order to be notified of new SDIO device */
extern int dhd_bus_reg_sdio_notify(void* semaphore);
extern void dhd_bus_unreg_sdio_notify(void);

extern void dhd_txglom_enable(dhd_pub_t *dhdp, bool enable);

#endif /* _dhd_bus_h_ */
