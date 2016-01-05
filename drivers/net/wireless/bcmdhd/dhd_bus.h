/*
 * Header file describing the internal (inter-module) DHD interfaces.
 *
 * Provides type definitions and function prototypes used to link the
 * DHD OS, bus, and protocol modules.
 *
 * Copyright (C) 1999-2015, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: dhd_bus.h 457888 2014-02-25 03:34:39Z $
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
extern int dhd_bus_download_firmware(struct dhd_bus *bus, osl_t *osh, char *fw_path, char *nv_path);

/* Stop bus module: clear pending frames, disable data flow */
extern void dhd_bus_stop(struct dhd_bus *bus, bool enforce_mutex);

/* Initialize bus module: prepare for communication w/dongle */
extern int dhd_bus_init(dhd_pub_t *dhdp, bool enforce_mutex);

/* Get the Bus Idle Time */
extern void dhd_bus_getidletime(dhd_pub_t *dhdp, int *idletime);

/* Set the Bus Idle Time */
extern void dhd_bus_setidletime(dhd_pub_t *dhdp, int idle_time);

/* Send a data frame to the dongle.  Callee disposes of txp. */
#ifdef BCMPCIE
extern int dhd_bus_txdata(struct dhd_bus *bus, void *txp, uint8 ifidx);
#else
extern int dhd_bus_txdata(struct dhd_bus *bus, void *txp);
#endif


/* Send/receive a control message to/from the dongle.
 * Expects caller to enforce a single outstanding transaction.
 */
extern int dhd_bus_txctl(struct dhd_bus *bus, uchar *msg, uint msglen);
extern int dhd_bus_rxctl(struct dhd_bus *bus, uchar *msg, uint msglen);

/* Watchdog timer function */
extern bool dhd_bus_watchdog(dhd_pub_t *dhd);

extern int dhd_bus_oob_intr_register(dhd_pub_t *dhdp);
extern void dhd_bus_oob_intr_unregister(dhd_pub_t *dhdp);
extern void dhd_bus_oob_intr_set(dhd_pub_t *dhdp, bool enable);
extern void dhd_bus_dev_pm_stay_awake(dhd_pub_t *dhdpub);
extern void dhd_bus_dev_pm_relax(dhd_pub_t *dhdpub);
extern bool dhd_bus_dev_pm_enabled(dhd_pub_t *dhdpub);

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

/* return the dongle chiprev */
extern uint dhd_bus_chiprev(struct dhd_bus *bus);

/* Set user-specified nvram parameters. */
extern void dhd_bus_set_nvram_params(struct dhd_bus * bus, const char *nvram_params);

extern void *dhd_bus_pub(struct dhd_bus *bus);
extern void *dhd_bus_txq(struct dhd_bus *bus);
extern void *dhd_bus_sih(struct dhd_bus *bus);
extern uint dhd_bus_hdrlen(struct dhd_bus *bus);
extern void dhd_bus_set_dotxinrx(struct dhd_bus *bus, bool val);

#define DHD_SET_BUS_STATE_DOWN(_bus)  do { \
	(_bus)->dhd->busstate = DHD_BUS_DOWN; \
} while (0)

/* Register a dummy SDIO client driver in order to be notified of new SDIO device */
extern int dhd_bus_reg_sdio_notify(void* semaphore);
extern void dhd_bus_unreg_sdio_notify(void);
extern void dhd_txglom_enable(dhd_pub_t *dhdp, bool enable);
extern int dhd_bus_get_ids(struct dhd_bus *bus, uint32 *bus_type, uint32 *bus_num,
	uint32 *slot_num);

#ifdef BCMPCIE
enum {
	DNGL_TO_HOST_BUF_IOCT,
	DNGL_TO_HOST_BUF_ADDR,
	HOST_TO_DNGL_BUF_ADDR,
	HOST_TO_DNGL_WPTR,
	HOST_TO_DNGL_RPTR,
	DNGL_TO_HOST_WPTR,
	DNGL_TO_HOST_RPTR,
	TOTAL_LFRAG_PACKET_CNT,
	HOST_TO_DNGL_CTRLBUF_ADDR,
	DNGL_TO_HOST_CTRLBUF_ADDR,
	HTOD_CTRL_RPTR,
	HTOD_CTRL_WPTR,
	DTOH_CTRL_RPTR,
	DTOH_CTRL_WPTR,
	HTOD_MB_DATA,
	DTOH_MB_DATA,
	MAX_HOST_RXBUFS
};
typedef void (*dhd_mb_ring_t) (struct dhd_bus *, uint32);
extern void dhd_bus_cmn_writeshared(struct dhd_bus *bus, void * data, uint32 len, uint8 type);
extern void dhd_bus_ringbell(struct dhd_bus *bus, uint32 value);
extern void dhd_bus_cmn_readshared(struct dhd_bus *bus, void* data, uint8 type);
extern uint32 dhd_bus_get_sharedflags(struct dhd_bus *bus);
extern void dhd_bus_rx_frame(struct dhd_bus *bus, void* pkt, int ifidx, uint pkt_count);
extern void dhd_bus_start_queue(struct dhd_bus *bus);
extern void dhd_bus_stop_queue(struct dhd_bus *bus);
extern void dhd_bus_update_retlen(struct dhd_bus *bus, uint32 retlen, uint32 cmd_id, uint32 status,
	uint32 inline_data);
extern dhd_mb_ring_t dhd_bus_get_mbintr_fn(struct dhd_bus *bus);
#endif /* BCMPCIE */
#endif /* _dhd_bus_h_ */
