/* SPDX-License-Identifier: GPL-2.0 */
/** @file dbus.c
 *
 * Hides details of USB / SDIO / SPI interfaces and OS details. It is intended to shield details and
 * provide the caller with one common bus interface for all dongle devices. In practice, it is only
 * used for USB interfaces. DBUS is not a protocol, but an abstraction layer.
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
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
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dbus.c 553311 2015-04-29 10:23:08Z $
 */


#include "osl.h"
#include "dbus.h"
#include <bcmutils.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_proto.h>
#ifdef PROP_TXSTATUS /* a form of flow control between host and dongle */
#include <dhd_wlfc.h>
#endif
#include <dhd_config.h>

#if defined(BCM_REQUEST_FW)
#include <bcmsrom_fmt.h>
#include <trxhdr.h>
#include <usbrdl.h>
#include <bcmendian.h>
#include <sbpcmcia.h>
#include <bcmnvram.h>
#include <bcmdevs.h>
#endif 



#if defined(BCM_REQUEST_FW)
#ifndef VARS_MAX
#define VARS_MAX            8192
#endif
#endif 

#ifdef DBUS_USB_LOOPBACK
extern bool is_loopback_pkt(void *buf);
extern int matches_loopback_pkt(void *buf);
#endif

/** General info for all BUS types */
typedef struct dbus_irbq {
	dbus_irb_t *head;
	dbus_irb_t *tail;
	int cnt;
} dbus_irbq_t;

/**
 * This private structure dhd_bus_t is also declared in dbus_usb_linux.c.
 * All the fields must be consistent in both declarations.
 */
typedef struct dhd_bus {
	dbus_pub_t   pub; /* MUST BE FIRST */
	dhd_pub_t *dhd;

	void        *cbarg;
	dbus_callbacks_t *cbs; /* callbacks to higher level, e.g. dhd_linux.c */
	void        *bus_info;
	dbus_intf_t *drvintf;  /* callbacks to lower level, e.g. dbus_usb.c or dbus_usb_linux.c */
	uint8       *fw;
	int         fwlen;
	uint32      errmask;
	int         rx_low_watermark;  /* avoid rx overflow by filling rx with free IRBs */
	int         tx_low_watermark;
	bool        txoff;
	bool        txoverride;   /* flow control related */
	bool        rxoff;
	bool        tx_timer_ticking;


	dbus_irbq_t *rx_q;
	dbus_irbq_t *tx_q;

	uint8        *nvram;
	int          nvram_len;
	uint8        *image;  /* buffer for combine fw and nvram */
	int          image_len;
	uint8        *orig_fw;
	int          origfw_len;
	int          decomp_memsize;
	dbus_extdl_t extdl;
	int          nvram_nontxt;
#if defined(BCM_REQUEST_FW)
	void         *firmware;
	void         *nvfile;
#endif
	char		*fw_path;		/* module_param: path to firmware image */
	char		*nv_path;		/* module_param: path to nvram vars file */
} dhd_bus_t;

struct exec_parms {
	union {
		/* Can consolidate same params, if need be, but this shows
		 * group of parameters per function
		 */
		struct {
			dbus_irbq_t  *q;
			dbus_irb_t   *b;
		} qenq;

		struct {
			dbus_irbq_t  *q;
		} qdeq;
	};
};

#define EXEC_RXLOCK(info, fn, a) \
	info->drvintf->exec_rxlock(dhd_bus->bus_info, ((exec_cb_t)fn), ((struct exec_parms *) a))

#define EXEC_TXLOCK(info, fn, a) \
	info->drvintf->exec_txlock(dhd_bus->bus_info, ((exec_cb_t)fn), ((struct exec_parms *) a))

/*
 * Callbacks common for all BUS
 */
static void dbus_if_send_irb_timeout(void *handle, dbus_irb_tx_t *txirb);
static void dbus_if_send_irb_complete(void *handle, dbus_irb_tx_t *txirb, int status);
static void dbus_if_recv_irb_complete(void *handle, dbus_irb_rx_t *rxirb, int status);
static void dbus_if_errhandler(void *handle, int err);
static void dbus_if_ctl_complete(void *handle, int type, int status);
static void dbus_if_state_change(void *handle, int state);
static void *dbus_if_pktget(void *handle, uint len, bool send);
static void dbus_if_pktfree(void *handle, void *p, bool send);
static struct dbus_irb *dbus_if_getirb(void *cbarg, bool send);
static void dbus_if_rxerr_indicate(void *handle, bool on);

void * dhd_dbus_probe_cb(void *arg, const char *desc, uint32 bustype,
	uint16 bus_no, uint16 slot, uint32 hdrlen);
void dhd_dbus_disconnect_cb(void *arg);
void dbus_detach(dhd_bus_t *pub);

/** functions in this file that are called by lower DBUS levels, e.g. dbus_usb.c */
static dbus_intf_callbacks_t dbus_intf_cbs = {
	dbus_if_send_irb_timeout,
	dbus_if_send_irb_complete,
	dbus_if_recv_irb_complete,
	dbus_if_errhandler,
	dbus_if_ctl_complete,
	dbus_if_state_change,
	NULL,			/* isr */
	NULL,			/* dpc */
	NULL,			/* watchdog */
	dbus_if_pktget,
	dbus_if_pktfree,
	dbus_if_getirb,
	dbus_if_rxerr_indicate
};

/*
 * Need global for probe() and disconnect() since
 * attach() is not called at probe and detach()
 * can be called inside disconnect()
 */
static dbus_intf_t     *g_busintf = NULL;
static probe_cb_t      probe_cb = NULL;
static disconnect_cb_t disconnect_cb = NULL;
static void            *probe_arg = NULL;
static void            *disc_arg = NULL;

#if defined(BCM_REQUEST_FW)
int8 *nonfwnvram = NULL; /* stand-alone multi-nvram given with driver load */
int nonfwnvramlen = 0;
#endif /* #if defined(BCM_REQUEST_FW) */

static void* q_enq(dbus_irbq_t *q, dbus_irb_t *b);
static void* q_enq_exec(struct exec_parms *args);
static dbus_irb_t*q_deq(dbus_irbq_t *q);
static void* q_deq_exec(struct exec_parms *args);
static int   dbus_tx_timer_init(dhd_bus_t *dhd_bus);
static int   dbus_tx_timer_start(dhd_bus_t *dhd_bus, uint timeout);
static int   dbus_tx_timer_stop(dhd_bus_t *dhd_bus);
static int   dbus_irbq_init(dhd_bus_t *dhd_bus, dbus_irbq_t *q, int nq, int size_irb);
static int   dbus_irbq_deinit(dhd_bus_t *dhd_bus, dbus_irbq_t *q, int size_irb);
static int   dbus_rxirbs_fill(dhd_bus_t *dhd_bus);
static int   dbus_send_irb(dbus_pub_t *pub, uint8 *buf, int len, void *pkt, void *info);
static void  dbus_disconnect(void *handle);
static void *dbus_probe(void *arg, const char *desc, uint32 bustype,
	uint16 bus_no, uint16 slot, uint32 hdrlen);

#if defined(BCM_REQUEST_FW)
extern char * dngl_firmware;
extern unsigned int dngl_fwlen;
#ifndef EXTERNAL_FW_PATH
static int dbus_get_nvram(dhd_bus_t *dhd_bus);
static int dbus_jumbo_nvram(dhd_bus_t *dhd_bus);
static int dbus_otp(dhd_bus_t *dhd_bus, uint16 *boardtype, uint16 *boardrev);
static int dbus_select_nvram(dhd_bus_t *dhd_bus, int8 *jumbonvram, int jumbolen,
uint16 boardtype, uint16 boardrev, int8 **nvram, int *nvram_len);
#endif /* !EXTERNAL_FW_PATH */
extern int dbus_zlib_decomp(dhd_bus_t *dhd_bus);
extern void *dbus_zlib_calloc(int num, int size);
extern void dbus_zlib_free(void *ptr);
#endif

/* function */
void
dbus_flowctrl_tx(void *dbi, bool on)
{
	dhd_bus_t *dhd_bus = dbi;

	if (dhd_bus == NULL)
		return;

	DBUSTRACE(("%s on %d\n", __FUNCTION__, on));

	if (dhd_bus->txoff == on)
		return;

	dhd_bus->txoff = on;

	if (dhd_bus->cbs && dhd_bus->cbs->txflowcontrol)
		dhd_bus->cbs->txflowcontrol(dhd_bus->cbarg, on);
}

/**
 * if lower level DBUS signaled a rx error, more free rx IRBs should be allocated or flow control
 * should kick in to make more free rx IRBs available.
 */
static void
dbus_if_rxerr_indicate(void *handle, bool on)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) handle;

	DBUSTRACE(("%s, on %d\n", __FUNCTION__, on));

	if (dhd_bus == NULL)
		return;

	if (dhd_bus->txoverride == on)
		return;

	dhd_bus->txoverride = on;	/* flow control */

	if (!on)
		dbus_rxirbs_fill(dhd_bus);

}

/** q_enq()/q_deq() are executed with protection via exec_rxlock()/exec_txlock() */
static void*
q_enq(dbus_irbq_t *q, dbus_irb_t *b)
{
	ASSERT(q->tail != b);
	ASSERT(b->next == NULL);
	b->next = NULL;
	if (q->tail) {
		q->tail->next = b;
		q->tail = b;
	} else
		q->head = q->tail = b;

	q->cnt++;

	return b;
}

static void*
q_enq_exec(struct exec_parms *args)
{
	return q_enq(args->qenq.q, args->qenq.b);
}

static dbus_irb_t*
q_deq(dbus_irbq_t *q)
{
	dbus_irb_t *b;

	b = q->head;
	if (b) {
		q->head = q->head->next;
		b->next = NULL;

		if (q->head == NULL)
			q->tail = q->head;

		q->cnt--;
	}
	return b;
}

static void*
q_deq_exec(struct exec_parms *args)
{
	return q_deq(args->qdeq.q);
}

/**
 * called during attach phase. Status @ Dec 2012: this function does nothing since for all of the
 * lower DBUS levels dhd_bus->drvintf->tx_timer_init is NULL.
 */
static int
dbus_tx_timer_init(dhd_bus_t *dhd_bus)
{
	if (dhd_bus && dhd_bus->drvintf && dhd_bus->drvintf->tx_timer_init)
		return dhd_bus->drvintf->tx_timer_init(dhd_bus->bus_info);
	else
		return DBUS_ERR;
}

static int
dbus_tx_timer_start(dhd_bus_t *dhd_bus, uint timeout)
{
	if (dhd_bus == NULL)
		return DBUS_ERR;

	if (dhd_bus->tx_timer_ticking)
		return DBUS_OK;

	if (dhd_bus->drvintf && dhd_bus->drvintf->tx_timer_start) {
		if (dhd_bus->drvintf->tx_timer_start(dhd_bus->bus_info, timeout) == DBUS_OK) {
			dhd_bus->tx_timer_ticking = TRUE;
			return DBUS_OK;
		}
	}

	return DBUS_ERR;
}

static int
dbus_tx_timer_stop(dhd_bus_t *dhd_bus)
{
	if (dhd_bus == NULL)
		return DBUS_ERR;

	if (!dhd_bus->tx_timer_ticking)
		return DBUS_OK;

	if (dhd_bus->drvintf && dhd_bus->drvintf->tx_timer_stop) {
		if (dhd_bus->drvintf->tx_timer_stop(dhd_bus->bus_info) == DBUS_OK) {
			dhd_bus->tx_timer_ticking = FALSE;
			return DBUS_OK;
		}
	}

	return DBUS_ERR;
}

/** called during attach phase. */
static int
dbus_irbq_init(dhd_bus_t *dhd_bus, dbus_irbq_t *q, int nq, int size_irb)
{
	int i;
	dbus_irb_t *irb;

	ASSERT(q);
	ASSERT(dhd_bus);

	for (i = 0; i < nq; i++) {
		/* MALLOC dbus_irb_tx or dbus_irb_rx, but cast to simple dbus_irb_t linkedlist */
		irb = (dbus_irb_t *) MALLOC(dhd_bus->pub.osh, size_irb);
		if (irb == NULL) {
			ASSERT(irb);
			return DBUS_ERR;
		}
		bzero(irb, size_irb);

		/* q_enq() does not need to go through EXEC_xxLOCK() during init() */
		q_enq(q, irb);
	}

	return DBUS_OK;
}

/** called during detach phase or when attach failed */
static int
dbus_irbq_deinit(dhd_bus_t *dhd_bus, dbus_irbq_t *q, int size_irb)
{
	dbus_irb_t *irb;

	ASSERT(q);
	ASSERT(dhd_bus);

	/* q_deq() does not need to go through EXEC_xxLOCK()
	 * during deinit(); all callbacks are stopped by this time
	 */
	while ((irb = q_deq(q)) != NULL) {
		MFREE(dhd_bus->pub.osh, irb, size_irb);
	}

	if (q->cnt)
		DBUSERR(("deinit: q->cnt=%d > 0\n", q->cnt));
	return DBUS_OK;
}

/** multiple code paths require the rx queue to be filled with more free IRBs */
static int
dbus_rxirbs_fill(dhd_bus_t *dhd_bus)
{
	int err = DBUS_OK;


	dbus_irb_rx_t *rxirb;
	struct exec_parms args;

	ASSERT(dhd_bus);
	if (dhd_bus->pub.busstate != DBUS_STATE_UP) {
		DBUSERR(("dbus_rxirbs_fill: DBUS not up \n"));
		return DBUS_ERR;
	} else if (!dhd_bus->drvintf || (dhd_bus->drvintf->recv_irb == NULL)) {
		/* Lower edge bus interface does not support recv_irb().
		 * No need to pre-submit IRBs in this case.
		 */
		return DBUS_ERR;
	}

	/* The dongle recv callback is freerunning without lock. So multiple callbacks(and this
	 *  refill) can run in parallel. While the rxoff condition is triggered outside,
	 *  below while loop has to check and abort posting more to avoid RPC rxq overflow.
	 */
	args.qdeq.q = dhd_bus->rx_q;
	while ((!dhd_bus->rxoff) &&
	       (rxirb = (EXEC_RXLOCK(dhd_bus, q_deq_exec, &args))) != NULL) {
		err = dhd_bus->drvintf->recv_irb(dhd_bus->bus_info, rxirb);
		if (err == DBUS_ERR_RXDROP || err == DBUS_ERR_RXFAIL) {
			/* Add the the free rxirb back to the queue
			 * and wait till later
			 */
			bzero(rxirb, sizeof(dbus_irb_rx_t));
			args.qenq.q = dhd_bus->rx_q;
			args.qenq.b = (dbus_irb_t *) rxirb;
			EXEC_RXLOCK(dhd_bus, q_enq_exec, &args);
			break;
		} else if (err != DBUS_OK) {
			int i = 0;
			while (i++ < 100) {
				DBUSERR(("%s :: memory leak for rxirb note?\n", __FUNCTION__));
			}
		}
	}
	return err;
} /* dbus_rxirbs_fill */

/** called when the DBUS interface state changed. */
void
dbus_flowctrl_rx(dbus_pub_t *pub, bool on)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;

	if (dhd_bus == NULL)
		return;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dhd_bus->rxoff == on)
		return;

	dhd_bus->rxoff = on;

	if (dhd_bus->pub.busstate == DBUS_STATE_UP) {
		if (!on) {
			/* post more irbs, resume rx if necessary */
			dbus_rxirbs_fill(dhd_bus);
			if (dhd_bus && dhd_bus->drvintf->recv_resume) {
				dhd_bus->drvintf->recv_resume(dhd_bus->bus_info);
			}
		} else {
			/* ??? cancell posted irbs first */

			if (dhd_bus && dhd_bus->drvintf->recv_stop) {
				dhd_bus->drvintf->recv_stop(dhd_bus->bus_info);
			}
		}
	}
}

/**
 * Several code paths in this file want to send a buffer to the dongle. This function handles both
 * sending of a buffer or a pkt.
 */
static int
dbus_send_irb(dbus_pub_t *pub, uint8 *buf, int len, void *pkt, void *info)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;
	int err = DBUS_OK;
	dbus_irb_tx_t *txirb = NULL;
	int txirb_pending;
	struct exec_parms args;

	if (dhd_bus == NULL)
		return DBUS_ERR;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dhd_bus->pub.busstate == DBUS_STATE_UP ||
		dhd_bus->pub.busstate == DBUS_STATE_SLEEP) {
		args.qdeq.q = dhd_bus->tx_q;
		if (dhd_bus->drvintf)
			txirb = EXEC_TXLOCK(dhd_bus, q_deq_exec, &args);

		if (txirb == NULL) {
			DBUSERR(("Out of tx dbus_bufs\n"));
			return DBUS_ERR;
		}

		if (pkt != NULL) {
			txirb->pkt = pkt;
			txirb->buf = NULL;
			txirb->len = 0;
		} else if (buf != NULL) {
			txirb->pkt = NULL;
			txirb->buf = buf;
			txirb->len = len;
		} else {
			ASSERT(0); /* Should not happen */
		}
		txirb->info = info;
		txirb->arg = NULL;
		txirb->retry_count = 0;

		if (dhd_bus->drvintf && dhd_bus->drvintf->send_irb) {
			/* call lower DBUS level send_irb function */
			err = dhd_bus->drvintf->send_irb(dhd_bus->bus_info, txirb);
			if (err == DBUS_ERR_TXDROP) {
				/* tx fail and no completion routine to clean up, reclaim irb NOW */
				DBUSERR(("%s: send_irb failed, status = %d\n", __FUNCTION__, err));
				bzero(txirb, sizeof(dbus_irb_tx_t));
				args.qenq.q = dhd_bus->tx_q;
				args.qenq.b = (dbus_irb_t *) txirb;
				EXEC_TXLOCK(dhd_bus, q_enq_exec, &args);
			} else {
				dbus_tx_timer_start(dhd_bus, DBUS_TX_TIMEOUT_INTERVAL);
				txirb_pending = dhd_bus->pub.ntxq - dhd_bus->tx_q->cnt;
				if (txirb_pending > (dhd_bus->tx_low_watermark * 3)) {
					dbus_flowctrl_tx(dhd_bus, TRUE);
				}
			}
		}
	} else {
		err = DBUS_ERR_TXFAIL;
		DBUSTRACE(("%s: bus down, send_irb failed\n", __FUNCTION__));
	}

	return err;
} /* dbus_send_irb */

#if defined(BCM_REQUEST_FW)

/**
 * Before downloading a firmware image into the dongle, the validity of the image must be checked.
 */
static int
check_file(osl_t *osh, unsigned char *headers)
{
	struct trx_header *trx;
	int actual_len = -1;

	/* Extract trx header */
	trx = (struct trx_header *)headers;
	if (ltoh32(trx->magic) != TRX_MAGIC) {
		printf("Error: trx bad hdr %x\n", ltoh32(trx->magic));
		return -1;
	}

	headers += SIZEOF_TRX(trx);

	/* TRX V1: get firmware len */
	/* TRX V2: get firmware len and DSG/CFG lengths */
	if (ltoh32(trx->flag_version) & TRX_UNCOMP_IMAGE) {
		actual_len = ltoh32(trx->offsets[TRX_OFFSETS_DLFWLEN_IDX]) +
		                     SIZEOF_TRX(trx);
#ifdef BCMTRXV2
		if (ISTRX_V2(trx)) {
			actual_len += ltoh32(trx->offsets[TRX_OFFSETS_DSG_LEN_IDX]) +
				ltoh32(trx->offsets[TRX_OFFSETS_CFG_LEN_IDX]);
		}
#endif
		return actual_len;
	}  else {
		printf("compressed image\n");
	}

	return -1;
}

#ifdef EXTERNAL_FW_PATH
static int
dbus_get_fw_nvram(dhd_bus_t *dhd_bus, char *pfw_path, char *pnv_path)
{
	int bcmerror = -1, i;
	uint len, total_len;
	void *nv_image = NULL, *fw_image = NULL;
	char *nv_memblock = NULL, *fw_memblock = NULL;
	char *bufp;
	bool file_exists;
	uint8 nvram_words_pad = 0;
	uint memblock_size = 2048;
	uint8 *memptr;
	int	actual_fwlen;
	struct trx_header *hdr;
	uint32 img_offset = 0;
	int offset = 0;

	/* For Get nvram */
	file_exists = ((pnv_path != NULL) && (pnv_path[0] != '\0'));
	if (file_exists) {
		nv_image = dhd_os_open_image1(dhd_bus->dhd, pnv_path);
		if (nv_image == NULL) {
			printf("%s: Open nvram file failed %s\n", __FUNCTION__, pnv_path);
			goto err;
		}
	}
	nv_memblock = MALLOC(dhd_bus->pub.osh, MAX_NVRAMBUF_SIZE);
	if (nv_memblock == NULL) {
		DBUSERR(("%s: Failed to allocate memory %d bytes\n",
		           __FUNCTION__, MAX_NVRAMBUF_SIZE));
		goto err;
	}
	len = dhd_os_get_image_block(nv_memblock, MAX_NVRAMBUF_SIZE, nv_image);
	if (len > 0 && len < MAX_NVRAMBUF_SIZE) {
		bufp = (char *)nv_memblock;
		bufp[len] = 0;
		dhd_bus->nvram_len = process_nvram_vars(bufp, len);
		if (dhd_bus->nvram_len % 4)
			nvram_words_pad = 4 - dhd_bus->nvram_len % 4;
	} else {
		DBUSERR(("%s: error reading nvram file: %d\n", __FUNCTION__, len));
		bcmerror = DBUS_ERR_NVRAM;
		goto err;
	}
	if (nv_image) {
		dhd_os_close_image1(dhd_bus->dhd, nv_image);
		nv_image = NULL;
	}

	/* For Get first block of fw to calculate total_len */
	file_exists = ((pfw_path != NULL) && (pfw_path[0] != '\0'));
	if (file_exists) {
		fw_image = dhd_os_open_image1(dhd_bus->dhd, pfw_path);
		if (fw_image == NULL) {
			printf("%s: Open fw file failed %s\n", __FUNCTION__, pfw_path);
			goto err;
		}
	}
	memptr = fw_memblock = MALLOC(dhd_bus->pub.osh, memblock_size);
	if (fw_memblock == NULL) {
		DBUSERR(("%s: Failed to allocate memory %d bytes\n", __FUNCTION__,
			memblock_size));
		goto err;
	}
	len = dhd_os_get_image_block((char*)memptr, memblock_size, fw_image);
	if ((actual_fwlen = check_file(dhd_bus->pub.osh, memptr)) <= 0) {
		DBUSERR(("%s: bad firmware format!\n", __FUNCTION__));
		goto err;
	}

	total_len = actual_fwlen + dhd_bus->nvram_len + nvram_words_pad;
	dhd_bus->image = MALLOC(dhd_bus->pub.osh, total_len);
	dhd_bus->image_len = total_len;
	if (dhd_bus->image == NULL) {
		DBUSERR(("%s: malloc failed!\n", __FUNCTION__));
		goto err;
	}

	/* Step1: Copy trx header + firmwre */
	memptr = fw_memblock;
	do {
		if (len < 0) {
			DBUSERR(("%s: dhd_os_get_image_block failed (%d)\n", __FUNCTION__, len));
			bcmerror = BCME_ERROR;
			goto err;
		}
		bcopy(memptr, dhd_bus->image+offset, len);
		offset += len;
	} while ((len = dhd_os_get_image_block((char*)memptr, memblock_size, fw_image)));
	/* Step2: Copy NVRAM + pad */
	hdr = (struct trx_header *)dhd_bus->image;
	img_offset = SIZEOF_TRX(hdr) + hdr->offsets[TRX_OFFSETS_DLFWLEN_IDX];
	bcopy(nv_memblock, (uint8 *)(dhd_bus->image + img_offset),
		dhd_bus->nvram_len);
	img_offset += dhd_bus->nvram_len;
	if (nvram_words_pad) {
		bzero(&dhd_bus->image[img_offset], nvram_words_pad);
		img_offset += nvram_words_pad;
	}
#ifdef BCMTRXV2
	/* Step3: Copy DSG/CFG for V2 */
	if (ISTRX_V2(hdr) &&
		(hdr->offsets[TRX_OFFSETS_DSG_LEN_IDX] ||
		hdr->offsets[TRX_OFFSETS_CFG_LEN_IDX])) {
		DBUSERR(("%s: fix me\n", __FUNCTION__));
	}
#endif /* BCMTRXV2 */
	/* Step4: update TRX header for nvram size */
	hdr = (struct trx_header *)dhd_bus->image;
	hdr->len = htol32(total_len);
	/* Pass the actual fw len */
	hdr->offsets[TRX_OFFSETS_NVM_LEN_IDX] =
		htol32(dhd_bus->nvram_len + nvram_words_pad);
	/* Calculate CRC over header */
	hdr->crc32 = hndcrc32((uint8 *)&hdr->flag_version,
		SIZEOF_TRX(hdr) - OFFSETOF(struct trx_header, flag_version),
		CRC32_INIT_VALUE);

	/* Calculate CRC over data */
	for (i = SIZEOF_TRX(hdr); i < total_len; ++i)
			hdr->crc32 = hndcrc32((uint8 *)&dhd_bus->image[i], 1, hdr->crc32);
	hdr->crc32 = htol32(hdr->crc32);

	bcmerror = DBUS_OK;

err:
	if (fw_memblock)
		MFREE(dhd_bus->pub.osh, fw_memblock, MAX_NVRAMBUF_SIZE);
	if (fw_image)
		dhd_os_close_image1(dhd_bus->dhd, fw_image);
	if (nv_memblock)
		MFREE(dhd_bus->pub.osh, nv_memblock, MAX_NVRAMBUF_SIZE);
	if (nv_image)
		dhd_os_close_image1(dhd_bus->dhd, nv_image);

	return bcmerror;
}

/**
 * during driver initialization ('attach') or after PnP 'resume', firmware needs to be loaded into
 * the dongle
 */
static int
dbus_do_download(dhd_bus_t *dhd_bus, char *pfw_path, char *pnv_path)
{
	int err = DBUS_OK;

	err = dbus_get_fw_nvram(dhd_bus, pfw_path, pnv_path);
	if (err) {
		DBUSERR(("dbus_do_download: fail to get nvram %d\n", err));
		return err;
	}

	if (dhd_bus->drvintf->dlstart && dhd_bus->drvintf->dlrun) {
		err = dhd_bus->drvintf->dlstart(dhd_bus->bus_info,
			dhd_bus->image, dhd_bus->image_len);
		if (err == DBUS_OK) {
			err = dhd_bus->drvintf->dlrun(dhd_bus->bus_info);
		}
	} else
		err = DBUS_ERR;

	if (dhd_bus->image) {
		MFREE(dhd_bus->pub.osh, dhd_bus->image, dhd_bus->image_len);
		dhd_bus->image = NULL;
		dhd_bus->image_len = 0;
	}

	return err;
} /* dbus_do_download */
#else

/**
 * It is easy for the user to pass one jumbo nvram file to the driver than a set of smaller files.
 * The 'jumbo nvram' file format is essentially a set of nvram files. Before commencing firmware
 * download, the dongle needs to be probed so that the correct nvram contents within the jumbo nvram
 * file is selected.
 */
static int
dbus_jumbo_nvram(dhd_bus_t *dhd_bus)
{
	int8 *nvram = NULL;
	int nvram_len = 0;
	int ret = DBUS_OK;
	uint16 boardrev = 0xFFFF;
	uint16 boardtype = 0xFFFF;

	/* read the otp for boardrev & boardtype
	* if boardtype/rev are present in otp
	* select nvram data for that boardtype/rev
	*/
	dbus_otp(dhd_bus, &boardtype, &boardrev);

	ret = dbus_select_nvram(dhd_bus, dhd_bus->extdl.vars, dhd_bus->extdl.varslen,
		boardtype, boardrev, &nvram, &nvram_len);

	if (ret == DBUS_JUMBO_BAD_FORMAT)
			return DBUS_ERR_NVRAM;
	else if (ret == DBUS_JUMBO_NOMATCH &&
		(boardtype != 0xFFFF || boardrev  != 0xFFFF)) {
			DBUSERR(("No matching NVRAM for boardtype 0x%02x boardrev 0x%02x\n",
				boardtype, boardrev));
			return DBUS_ERR_NVRAM;
	}
	dhd_bus->nvram = nvram;
	dhd_bus->nvram_len =  nvram_len;

	return DBUS_OK;
}

/** before commencing fw download, the correct NVRAM image to download has to be picked */
static int
dbus_get_nvram(dhd_bus_t *dhd_bus)
{
	int len, i;
	struct trx_header *hdr;
	int	actual_fwlen;
	uint32 img_offset = 0;

	dhd_bus->nvram_len = 0;
	if (dhd_bus->extdl.varslen) {
		if (DBUS_OK != dbus_jumbo_nvram(dhd_bus))
			return DBUS_ERR_NVRAM;
		DBUSERR(("NVRAM %d bytes downloaded\n", dhd_bus->nvram_len));
	}
#if defined(BCM_REQUEST_FW)
	else if (nonfwnvram) {
		dhd_bus->nvram = nonfwnvram;
		dhd_bus->nvram_len = nonfwnvramlen;
		DBUSERR(("NVRAM %d bytes downloaded\n", dhd_bus->nvram_len));
	}
#endif
	if (dhd_bus->nvram) {
		uint8 nvram_words_pad = 0;
		/* Validate the format/length etc of the file */
		if ((actual_fwlen = check_file(dhd_bus->pub.osh, dhd_bus->fw)) <= 0) {
			DBUSERR(("%s: bad firmware format!\n", __FUNCTION__));
			return DBUS_ERR_NVRAM;
		}

		if (!dhd_bus->nvram_nontxt) {
			/* host supplied nvram could be in .txt format
			* with all the comments etc...
			*/
			dhd_bus->nvram_len = process_nvram_vars(dhd_bus->nvram,
				dhd_bus->nvram_len);
		}
		if (dhd_bus->nvram_len % 4)
			nvram_words_pad = 4 - dhd_bus->nvram_len % 4;

		len = actual_fwlen + dhd_bus->nvram_len + nvram_words_pad;
		dhd_bus->image = MALLOC(dhd_bus->pub.osh, len);
		dhd_bus->image_len = len;
		if (dhd_bus->image == NULL) {
			DBUSERR(("%s: malloc failed!\n", __FUNCTION__));
			return DBUS_ERR_NVRAM;
		}
		hdr = (struct trx_header *)dhd_bus->fw;
		/* Step1: Copy trx header + firmwre */
		img_offset = SIZEOF_TRX(hdr) + hdr->offsets[TRX_OFFSETS_DLFWLEN_IDX];
		bcopy(dhd_bus->fw, dhd_bus->image, img_offset);
		/* Step2: Copy NVRAM + pad */
		bcopy(dhd_bus->nvram, (uint8 *)(dhd_bus->image + img_offset),
			dhd_bus->nvram_len);
		img_offset += dhd_bus->nvram_len;
		if (nvram_words_pad) {
			bzero(&dhd_bus->image[img_offset],
				nvram_words_pad);
			img_offset += nvram_words_pad;
		}
#ifdef BCMTRXV2
		/* Step3: Copy DSG/CFG for V2 */
		if (ISTRX_V2(hdr) &&
			(hdr->offsets[TRX_OFFSETS_DSG_LEN_IDX] ||
			hdr->offsets[TRX_OFFSETS_CFG_LEN_IDX])) {

			bcopy(dhd_bus->fw + SIZEOF_TRX(hdr) +
				hdr->offsets[TRX_OFFSETS_DLFWLEN_IDX] +
				hdr->offsets[TRX_OFFSETS_NVM_LEN_IDX],
				dhd_bus->image + img_offset,
				hdr->offsets[TRX_OFFSETS_DSG_LEN_IDX] +
				hdr->offsets[TRX_OFFSETS_CFG_LEN_IDX]);

			img_offset += hdr->offsets[TRX_OFFSETS_DSG_LEN_IDX] +
				hdr->offsets[TRX_OFFSETS_CFG_LEN_IDX];
		}
#endif /* BCMTRXV2 */
		/* Step4: update TRX header for nvram size */
		hdr = (struct trx_header *)dhd_bus->image;
		hdr->len = htol32(len);
		/* Pass the actual fw len */
		hdr->offsets[TRX_OFFSETS_NVM_LEN_IDX] =
			htol32(dhd_bus->nvram_len + nvram_words_pad);
		/* Calculate CRC over header */
		hdr->crc32 = hndcrc32((uint8 *)&hdr->flag_version,
			SIZEOF_TRX(hdr) - OFFSETOF(struct trx_header, flag_version),
			CRC32_INIT_VALUE);

		/* Calculate CRC over data */
		for (i = SIZEOF_TRX(hdr); i < len; ++i)
				hdr->crc32 = hndcrc32((uint8 *)&dhd_bus->image[i], 1, hdr->crc32);
		hdr->crc32 = htol32(hdr->crc32);
	} else {
		dhd_bus->image = dhd_bus->fw;
		dhd_bus->image_len = (uint32)dhd_bus->fwlen;
	}

	return DBUS_OK;
} /* dbus_get_nvram */

/**
 * during driver initialization ('attach') or after PnP 'resume', firmware needs to be loaded into
 * the dongle
 */
static int
dbus_do_download(dhd_bus_t *dhd_bus)
{
	int err = DBUS_OK;
#ifndef BCM_REQUEST_FW
	int decomp_override = 0;
#endif
#ifdef BCM_REQUEST_FW
	uint16 boardrev = 0xFFFF, boardtype = 0xFFFF;
	int8 *temp_nvram;
	int temp_len;
#endif

#if defined(BCM_REQUEST_FW)
	dhd_bus->firmware = dbus_get_fw_nvfile(dhd_bus->pub.attrib.devid,
		dhd_bus->pub.attrib.chiprev, &dhd_bus->fw, &dhd_bus->fwlen,
		DBUS_FIRMWARE, 0, 0);
	if (!dhd_bus->firmware)
		return DBUS_ERR;
#endif 

	dhd_bus->image = dhd_bus->fw;
	dhd_bus->image_len = (uint32)dhd_bus->fwlen;

#ifndef BCM_REQUEST_FW
	if (UNZIP_ENAB(dhd_bus) && !decomp_override) {
		err = dbus_zlib_decomp(dhd_bus);
		if (err) {
			DBUSERR(("dbus_attach: fw decompress fail %d\n", err));
			return err;
		}
	}
#endif

#if defined(BCM_REQUEST_FW)
	/* check if firmware is appended with nvram file */
	err = dbus_otp(dhd_bus, &boardtype, &boardrev);
	/* check if nvram is provided as separte file */
	nonfwnvram = NULL;
	nonfwnvramlen = 0;
	dhd_bus->nvfile = dbus_get_fw_nvfile(dhd_bus->pub.attrib.devid,
		dhd_bus->pub.attrib.chiprev, (void *)&temp_nvram, &temp_len,
		DBUS_NVFILE, boardtype, boardrev);
	if (dhd_bus->nvfile) {
		int8 *tmp = MALLOC(dhd_bus->pub.osh, temp_len);
		if (tmp) {
			bcopy(temp_nvram, tmp, temp_len);
			nonfwnvram = tmp;
			nonfwnvramlen = temp_len;
		} else {
			err = DBUS_ERR;
			goto fail;
		}
	}
#endif /* defined(BCM_REQUEST_FW) */

	err = dbus_get_nvram(dhd_bus);
	if (err) {
		DBUSERR(("dbus_do_download: fail to get nvram %d\n", err));
		return err;
	}


	if (dhd_bus->drvintf->dlstart && dhd_bus->drvintf->dlrun) {
		err = dhd_bus->drvintf->dlstart(dhd_bus->bus_info,
			dhd_bus->image, dhd_bus->image_len);

		if (err == DBUS_OK)
			err = dhd_bus->drvintf->dlrun(dhd_bus->bus_info);
	} else
		err = DBUS_ERR;

	if (dhd_bus->nvram) {
		MFREE(dhd_bus->pub.osh, dhd_bus->image, dhd_bus->image_len);
		dhd_bus->image = dhd_bus->fw;
		dhd_bus->image_len = (uint32)dhd_bus->fwlen;
	}

#ifndef BCM_REQUEST_FW
	if (UNZIP_ENAB(dhd_bus) && (!decomp_override) && dhd_bus->orig_fw) {
		MFREE(dhd_bus->pub.osh, dhd_bus->fw, dhd_bus->decomp_memsize);
		dhd_bus->image = dhd_bus->fw = dhd_bus->orig_fw;
		dhd_bus->image_len = dhd_bus->fwlen = dhd_bus->origfw_len;
	}
#endif

#if defined(BCM_REQUEST_FW)
fail:
	if (dhd_bus->firmware) {
		dbus_release_fw_nvfile(dhd_bus->firmware);
		dhd_bus->firmware = NULL;
	}
	if (dhd_bus->nvfile) {
		dbus_release_fw_nvfile(dhd_bus->nvfile);
		dhd_bus->nvfile = NULL;
	}
	if (nonfwnvram) {
		MFREE(dhd_bus->pub.osh, nonfwnvram, nonfwnvramlen);
		nonfwnvram = NULL;
		nonfwnvramlen = 0;
	}
#endif
	return err;
} /* dbus_do_download */
#endif /* EXTERNAL_FW_PATH */
#endif

/** required for DBUS deregistration */
static void
dbus_disconnect(void *handle)
{
	DBUSTRACE(("%s\n", __FUNCTION__));

	if (disconnect_cb)
		disconnect_cb(disc_arg);
}

/**
 * This function is called when the sent irb times out without a tx response status.
 * DBUS adds reliability by resending timed out IRBs DBUS_TX_RETRY_LIMIT times.
 */
static void
dbus_if_send_irb_timeout(void *handle, dbus_irb_tx_t *txirb)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) handle;

	if ((dhd_bus == NULL) || (dhd_bus->drvintf == NULL) || (txirb == NULL)) {
		return;
	}

	DBUSTRACE(("%s\n", __FUNCTION__));

	return;

} /* dbus_if_send_irb_timeout */

/**
 * When lower DBUS level signals that a send IRB completed, either successful or not, the higher
 * level (e.g. dhd_linux.c) has to be notified, and transmit flow control has to be evaluated.
 */
static void BCMFASTPATH
dbus_if_send_irb_complete(void *handle, dbus_irb_tx_t *txirb, int status)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) handle;
	int txirb_pending;
	struct exec_parms args;
	void *pktinfo;

	if ((dhd_bus == NULL) || (txirb == NULL)) {
		return;
	}

	DBUSTRACE(("%s: status = %d\n", __FUNCTION__, status));

	dbus_tx_timer_stop(dhd_bus);

	/* re-queue BEFORE calling send_complete which will assume that this irb
	   is now available.
	 */
	pktinfo = txirb->info;
	bzero(txirb, sizeof(dbus_irb_tx_t));
	args.qenq.q = dhd_bus->tx_q;
	args.qenq.b = (dbus_irb_t *) txirb;
	EXEC_TXLOCK(dhd_bus, q_enq_exec, &args);

	if (dhd_bus->pub.busstate != DBUS_STATE_DOWN) {
		if ((status == DBUS_OK) || (status == DBUS_ERR_NODEVICE)) {
			if (dhd_bus->cbs && dhd_bus->cbs->send_complete)
				dhd_bus->cbs->send_complete(dhd_bus->cbarg, pktinfo,
					status);

			if (status == DBUS_OK) {
				txirb_pending = dhd_bus->pub.ntxq - dhd_bus->tx_q->cnt;
				if (txirb_pending)
					dbus_tx_timer_start(dhd_bus, DBUS_TX_TIMEOUT_INTERVAL);
				if ((txirb_pending < dhd_bus->tx_low_watermark) &&
					dhd_bus->txoff && !dhd_bus->txoverride) {
					dbus_flowctrl_tx(dhd_bus, OFF);
				}
			}
		} else {
			DBUSERR(("%s: %d WARNING freeing orphan pkt %p\n", __FUNCTION__, __LINE__,
				pktinfo));
#if defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_TXNOCOPY) || defined(BCM_RPC_TOC)
			if (pktinfo)
				if (dhd_bus->cbs && dhd_bus->cbs->send_complete)
					dhd_bus->cbs->send_complete(dhd_bus->cbarg, pktinfo,
						status);
#else
			dbus_if_pktfree(dhd_bus, (void*)pktinfo, TRUE);
#endif /* defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_TXNOCOPY) || defined(BCM_RPC_TOC) */
		}
	} else {
		DBUSERR(("%s: %d WARNING freeing orphan pkt %p\n", __FUNCTION__, __LINE__,
			pktinfo));
#if defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_TXNOCOPY) || defined(BCM_RPC_TOC)
		if (pktinfo)
			if (dhd_bus->cbs && dhd_bus->cbs->send_complete)
				dhd_bus->cbs->send_complete(dhd_bus->cbarg, pktinfo,
					status);
#else
		dbus_if_pktfree(dhd_bus, (void*)pktinfo, TRUE);
#endif /* defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_TXNOCOPY) defined(BCM_RPC_TOC) */
	}
} /* dbus_if_send_irb_complete */

/**
 * When lower DBUS level signals that a receive IRB completed, either successful or not, the higher
 * level (e.g. dhd_linux.c) has to be notified, and fresh free receive IRBs may have to be given
 * to lower levels.
 */
static void BCMFASTPATH
dbus_if_recv_irb_complete(void *handle, dbus_irb_rx_t *rxirb, int status)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) handle;
	int rxirb_pending;
	struct exec_parms args;

	if ((dhd_bus == NULL) || (rxirb == NULL)) {
		return;
	}
	DBUSTRACE(("%s\n", __FUNCTION__));
	if (dhd_bus->pub.busstate != DBUS_STATE_DOWN &&
		dhd_bus->pub.busstate != DBUS_STATE_SLEEP) {
		if (status == DBUS_OK) {
			if ((rxirb->buf != NULL) && (rxirb->actual_len > 0)) {
#ifdef DBUS_USB_LOOPBACK
				if (is_loopback_pkt(rxirb->buf)) {
					matches_loopback_pkt(rxirb->buf);
				} else
#endif
				if (dhd_bus->cbs && dhd_bus->cbs->recv_buf) {
					dhd_bus->cbs->recv_buf(dhd_bus->cbarg, rxirb->buf,
					rxirb->actual_len);
				}
			} else if (rxirb->pkt != NULL) {
				if (dhd_bus->cbs && dhd_bus->cbs->recv_pkt)
					dhd_bus->cbs->recv_pkt(dhd_bus->cbarg, rxirb->pkt);
			} else {
				ASSERT(0); /* Should not happen */
			}

			rxirb_pending = dhd_bus->pub.nrxq - dhd_bus->rx_q->cnt - 1;
			if ((rxirb_pending <= dhd_bus->rx_low_watermark) &&
				!dhd_bus->rxoff) {
				DBUSTRACE(("Low watermark so submit more %d <= %d \n",
					dhd_bus->rx_low_watermark, rxirb_pending));
				dbus_rxirbs_fill(dhd_bus);
			} else if (dhd_bus->rxoff)
				DBUSTRACE(("rx flow controlled. not filling more. cut_rxq=%d\n",
					dhd_bus->rx_q->cnt));
		} else if (status == DBUS_ERR_NODEVICE) {
			DBUSERR(("%s: %d status = %d, buf %p\n", __FUNCTION__, __LINE__, status,
				rxirb->buf));
#if defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_RXNOCOPY)
			if (rxirb->buf) {
				PKTFRMNATIVE(dhd_bus->pub.osh, rxirb->buf);
				PKTFREE(dhd_bus->pub.osh, rxirb->buf, FALSE);
			}
#endif /* BCM_RPC_NOCOPY || BCM_RPC_TXNOCOPY || BCM_RPC_TOC */
		} else {
			if (status != DBUS_ERR_RXZLP)
				DBUSERR(("%s: %d status = %d, buf %p\n", __FUNCTION__, __LINE__,
					status, rxirb->buf));
#if defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_RXNOCOPY)
			if (rxirb->buf) {
				PKTFRMNATIVE(dhd_bus->pub.osh, rxirb->buf);
				PKTFREE(dhd_bus->pub.osh, rxirb->buf, FALSE);
			}
#endif /* BCM_RPC_NOCOPY || BCM_RPC_TXNOCOPY || BCM_RPC_TOC */
		}
	} else {
		DBUSTRACE(("%s: DBUS down, ignoring recv callback. buf %p\n", __FUNCTION__,
			rxirb->buf));
#if defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_RXNOCOPY)
		if (rxirb->buf) {
			PKTFRMNATIVE(dhd_bus->pub.osh, rxirb->buf);
			PKTFREE(dhd_bus->pub.osh, rxirb->buf, FALSE);
		}
#endif /* BCM_RPC_NOCOPY || BCM_RPC_TXNOCOPY || BCM_RPC_TOC */
	}
	if (dhd_bus->rx_q != NULL) {
		bzero(rxirb, sizeof(dbus_irb_rx_t));
		args.qenq.q = dhd_bus->rx_q;
		args.qenq.b = (dbus_irb_t *) rxirb;
		EXEC_RXLOCK(dhd_bus, q_enq_exec, &args);
	} else
		MFREE(dhd_bus->pub.osh, rxirb, sizeof(dbus_irb_tx_t));
} /* dbus_if_recv_irb_complete */

/**
 *  Accumulate errors signaled by lower DBUS levels and signal them to higher (e.g. dhd_linux.c)
 *  level.
 */
static void
dbus_if_errhandler(void *handle, int err)
{
	dhd_bus_t *dhd_bus = handle;
	uint32 mask = 0;

	if (dhd_bus == NULL)
		return;

	switch (err) {
		case DBUS_ERR_TXFAIL:
			dhd_bus->pub.stats.tx_errors++;
			mask |= ERR_CBMASK_TXFAIL;
			break;
		case DBUS_ERR_TXDROP:
			dhd_bus->pub.stats.tx_dropped++;
			mask |= ERR_CBMASK_TXFAIL;
			break;
		case DBUS_ERR_RXFAIL:
			dhd_bus->pub.stats.rx_errors++;
			mask |= ERR_CBMASK_RXFAIL;
			break;
		case DBUS_ERR_RXDROP:
			dhd_bus->pub.stats.rx_dropped++;
			mask |= ERR_CBMASK_RXFAIL;
			break;
		default:
			break;
	}

	if (dhd_bus->cbs && dhd_bus->cbs->errhandler && (dhd_bus->errmask & mask))
		dhd_bus->cbs->errhandler(dhd_bus->cbarg, err);
}

/**
 * When lower DBUS level signals control IRB completed, higher level (e.g. dhd_linux.c) has to be
 * notified.
 */
static void
dbus_if_ctl_complete(void *handle, int type, int status)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) handle;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dhd_bus == NULL) {
		DBUSERR(("%s: dhd_bus is NULL\n", __FUNCTION__));
		return;
	}

	if (dhd_bus->pub.busstate != DBUS_STATE_DOWN) {
		if (dhd_bus->cbs && dhd_bus->cbs->ctl_complete)
			dhd_bus->cbs->ctl_complete(dhd_bus->cbarg, type, status);
	}
}

/**
 * Rx related functionality (flow control, posting of free IRBs to rx queue) is dependent upon the
 * bus state. When lower DBUS level signals a change in the interface state, take appropriate action
 * and forward the signaling to the higher (e.g. dhd_linux.c) level.
 */
static void
dbus_if_state_change(void *handle, int state)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) handle;
	int old_state;

	if (dhd_bus == NULL)
		return;

	if (dhd_bus->pub.busstate == state)
		return;
	old_state = dhd_bus->pub.busstate;
	if (state == DBUS_STATE_DISCONNECT) {
		DBUSERR(("DBUS disconnected\n"));
	}

	/* Ignore USB SUSPEND while not up yet */
	if (state == DBUS_STATE_SLEEP && old_state != DBUS_STATE_UP)
		return;

	DBUSTRACE(("dbus state change from %d to to %d\n", old_state, state));

	/* Don't update state if it's PnP firmware re-download */
	if (state != DBUS_STATE_PNP_FWDL)
		dhd_bus->pub.busstate = state;
	else
		dbus_flowctrl_rx(handle, FALSE);
	if (state == DBUS_STATE_SLEEP)
		dbus_flowctrl_rx(handle, TRUE);
	if (state == DBUS_STATE_UP) {
		dbus_rxirbs_fill(dhd_bus);
		dbus_flowctrl_rx(handle, FALSE);
	}

	if (dhd_bus->cbs && dhd_bus->cbs->state_change)
		dhd_bus->cbs->state_change(dhd_bus->cbarg, state);
}

/** Forward request for packet from lower DBUS layer to higher layer (e.g. dhd_linux.c) */
static void *
dbus_if_pktget(void *handle, uint len, bool send)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) handle;
	void *p = NULL;

	if (dhd_bus == NULL)
		return NULL;

	if (dhd_bus->cbs && dhd_bus->cbs->pktget)
		p = dhd_bus->cbs->pktget(dhd_bus->cbarg, len, send);
	else
		ASSERT(0);

	return p;
}

/** Forward request to free packet from lower DBUS layer to higher layer (e.g. dhd_linux.c) */
static void
dbus_if_pktfree(void *handle, void *p, bool send)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) handle;

	if (dhd_bus == NULL)
		return;

	if (dhd_bus->cbs && dhd_bus->cbs->pktfree)
		dhd_bus->cbs->pktfree(dhd_bus->cbarg, p, send);
	else
		ASSERT(0);
}

/** Lower DBUS level requests either a send or receive IRB */
static struct dbus_irb*
dbus_if_getirb(void *cbarg, bool send)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) cbarg;
	struct exec_parms args;
	struct dbus_irb *irb;

	if ((dhd_bus == NULL) || (dhd_bus->pub.busstate != DBUS_STATE_UP))
		return NULL;

	if (send == TRUE) {
		args.qdeq.q = dhd_bus->tx_q;
		irb = EXEC_TXLOCK(dhd_bus, q_deq_exec, &args);
	} else {
		args.qdeq.q = dhd_bus->rx_q;
		irb = EXEC_RXLOCK(dhd_bus, q_deq_exec, &args);
	}

	return irb;
}

/**
 * Called as part of DBUS bus registration. Calls back into higher level (e.g. dhd_linux.c) probe
 * function.
 */
static void *
dbus_probe(void *arg, const char *desc, uint32 bustype, uint16 bus_no,
	uint16 slot, uint32 hdrlen)
{
	DBUSTRACE(("%s\n", __FUNCTION__));
	if (probe_cb) {
		disc_arg = probe_cb(probe_arg, desc, bustype, bus_no, slot, hdrlen);
		return disc_arg;
	}

	return (void *)DBUS_ERR;
}

/**
 * As part of initialization, higher level (e.g. dhd_linux.c) requests DBUS to prepare for
 * action.
 */
int
dhd_bus_register(void)
{
	int err;

	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	probe_cb = dhd_dbus_probe_cb;
	disconnect_cb = dhd_dbus_disconnect_cb;
	probe_arg = NULL;

	err = dbus_bus_register(0xa5c, 0x48f, dbus_probe, /* call lower DBUS level register function */
		dbus_disconnect, NULL, &g_busintf, NULL, NULL);

	/* Device not detected */
	if (err == DBUS_ERR_NODEVICE)
		err = DBUS_OK;

	return err;
}

dhd_pub_t *g_pub = NULL;
void
dhd_bus_unregister(void)
{
	int ret;

	DBUSTRACE(("%s\n", __FUNCTION__));

	DHD_MUTEX_LOCK();
	if (g_pub) {
		g_pub->dhd_remove = TRUE;
		if (!g_pub->bus) {
			dhd_dbus_disconnect_cb(g_pub->bus);
		}
	}
	probe_cb = NULL;
	DHD_MUTEX_UNLOCK();
	ret = dbus_bus_deregister();
	disconnect_cb = NULL;
	probe_arg = NULL;
}

/** As part of initialization, data structures have to be allocated and initialized */
dhd_bus_t *
dbus_attach(osl_t *osh, int rxsize, int nrxq, int ntxq, dhd_pub_t *pub,
	dbus_callbacks_t *cbs, dbus_extdl_t *extdl, struct shared_info *sh)
{
	dhd_bus_t *dhd_bus;
	int err;

	if ((g_busintf == NULL) || (g_busintf->attach == NULL) || (cbs == NULL))
		return NULL;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if ((nrxq <= 0) || (ntxq <= 0))
		return NULL;

	dhd_bus = MALLOC(osh, sizeof(dhd_bus_t));
	if (dhd_bus == NULL) {
		DBUSERR(("%s: malloc failed %zu\n", __FUNCTION__, sizeof(dhd_bus_t)));
		return NULL;
	}

	bzero(dhd_bus, sizeof(dhd_bus_t));

	/* BUS-specific driver interface (at a lower DBUS level) */
	dhd_bus->drvintf = g_busintf;
	dhd_bus->cbarg = pub;
	dhd_bus->cbs = cbs;

	dhd_bus->pub.sh = sh;
	dhd_bus->pub.osh = osh;
	dhd_bus->pub.rxsize = rxsize;

	dhd_bus->pub.nrxq = nrxq;
	dhd_bus->rx_low_watermark = nrxq / 2;	/* keep enough posted rx urbs */
	dhd_bus->pub.ntxq = ntxq;
	dhd_bus->tx_low_watermark = ntxq / 4;	/* flow control when too many tx urbs posted */

	dhd_bus->tx_q = MALLOC(osh, sizeof(dbus_irbq_t));
	if (dhd_bus->tx_q == NULL)
		goto error;
	else {
		bzero(dhd_bus->tx_q, sizeof(dbus_irbq_t));
		err = dbus_irbq_init(dhd_bus, dhd_bus->tx_q, ntxq, sizeof(dbus_irb_tx_t));
		if (err != DBUS_OK)
			goto error;
	}

	dhd_bus->rx_q = MALLOC(osh, sizeof(dbus_irbq_t));
	if (dhd_bus->rx_q == NULL)
		goto error;
	else {
		bzero(dhd_bus->rx_q, sizeof(dbus_irbq_t));
		err = dbus_irbq_init(dhd_bus, dhd_bus->rx_q, nrxq, sizeof(dbus_irb_rx_t));
		if (err != DBUS_OK)
			goto error;
	}


	dhd_bus->bus_info = (void *)g_busintf->attach(&dhd_bus->pub,
		dhd_bus, &dbus_intf_cbs);
	if (dhd_bus->bus_info == NULL)
		goto error;

	dbus_tx_timer_init(dhd_bus);

#if defined(BCM_REQUEST_FW)
	/* Need to copy external image for re-download */
	if (extdl && extdl->fw && (extdl->fwlen > 0)) {
		dhd_bus->extdl.fw = MALLOC(osh, extdl->fwlen);
		if (dhd_bus->extdl.fw) {
			bcopy(extdl->fw, dhd_bus->extdl.fw, extdl->fwlen);
			dhd_bus->extdl.fwlen = extdl->fwlen;
		}
	}

	if (extdl && extdl->vars && (extdl->varslen > 0)) {
		dhd_bus->extdl.vars = MALLOC(osh, extdl->varslen);
		if (dhd_bus->extdl.vars) {
			bcopy(extdl->vars, dhd_bus->extdl.vars, extdl->varslen);
			dhd_bus->extdl.varslen = extdl->varslen;
		}
	}
#endif 

	return (dhd_bus_t *)dhd_bus;

error:
	DBUSERR(("%s: Failed\n", __FUNCTION__));
	dbus_detach(dhd_bus);
	return NULL;
} /* dbus_attach */

void
dbus_detach(dhd_bus_t *pub)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;
	osl_t *osh;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dhd_bus == NULL)
		return;

	dbus_tx_timer_stop(dhd_bus);

	osh = pub->pub.osh;

	if (dhd_bus->drvintf && dhd_bus->drvintf->detach)
		 dhd_bus->drvintf->detach((dbus_pub_t *)dhd_bus, dhd_bus->bus_info);

	if (dhd_bus->tx_q) {
		dbus_irbq_deinit(dhd_bus, dhd_bus->tx_q, sizeof(dbus_irb_tx_t));
		MFREE(osh, dhd_bus->tx_q, sizeof(dbus_irbq_t));
		dhd_bus->tx_q = NULL;
	}

	if (dhd_bus->rx_q) {
		dbus_irbq_deinit(dhd_bus, dhd_bus->rx_q, sizeof(dbus_irb_rx_t));
		MFREE(osh, dhd_bus->rx_q, sizeof(dbus_irbq_t));
		dhd_bus->rx_q = NULL;
	}


	if (dhd_bus->extdl.fw && (dhd_bus->extdl.fwlen > 0)) {
		MFREE(osh, dhd_bus->extdl.fw, dhd_bus->extdl.fwlen);
		dhd_bus->extdl.fw = NULL;
		dhd_bus->extdl.fwlen = 0;
	}

	if (dhd_bus->extdl.vars && (dhd_bus->extdl.varslen > 0)) {
		MFREE(osh, dhd_bus->extdl.vars, dhd_bus->extdl.varslen);
		dhd_bus->extdl.vars = NULL;
		dhd_bus->extdl.varslen = 0;
	}

	MFREE(osh, dhd_bus, sizeof(dhd_bus_t));
} /* dbus_detach */

int dbus_dlneeded(dhd_bus_t *pub)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;
	int dlneeded = DBUS_ERR;

	if (!dhd_bus) {
		DBUSERR(("%s: dhd_bus is NULL\n", __FUNCTION__));
		return DBUS_ERR;
	}

	DBUSTRACE(("%s: state %d\n", __FUNCTION__, dhd_bus->pub.busstate));

	if (dhd_bus->drvintf->dlneeded) {
		dlneeded = dhd_bus->drvintf->dlneeded(dhd_bus->bus_info);
	}
	printf("%s: dlneeded=%d\n", __FUNCTION__, dlneeded);

	/* dlneeded > 0: need to download
	  * dlneeded = 0: downloaded
	  * dlneeded < 0: bus error*/
	return dlneeded;
}

#if defined(BCM_REQUEST_FW)
int dbus_download_firmware(dhd_bus_t *pub, char *pfw_path, char *pnv_path)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;
	int err = DBUS_OK;

	if (!dhd_bus) {
		DBUSERR(("%s: dhd_bus is NULL\n", __FUNCTION__));
		return DBUS_ERR;
	}

	DBUSTRACE(("%s: state %d\n", __FUNCTION__, dhd_bus->pub.busstate));

	dhd_bus->pub.busstate = DBUS_STATE_DL_PENDING;
#ifdef EXTERNAL_FW_PATH
	err = dbus_do_download(dhd_bus, pfw_path, pnv_path);
#else
	err = dbus_do_download(dhd_bus);
#endif /* EXTERNAL_FW_PATH */
	if (err == DBUS_OK) {
		dhd_bus->pub.busstate = DBUS_STATE_DL_DONE;
	} else {
		DBUSERR(("%s: download failed (%d)\n", __FUNCTION__, err));
	}

	return err;
}
#endif 

/**
 * higher layer requests us to 'up' the interface to the dongle. Prerequisite is that firmware (not
 * bootloader) must be active in the dongle.
 */
int
dbus_up(struct dhd_bus *pub)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;
	int err = DBUS_OK;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dhd_bus == NULL) {
		DBUSERR(("%s: dhd_bus is NULL\n", __FUNCTION__));
		return DBUS_ERR;
	}

	if ((dhd_bus->pub.busstate == DBUS_STATE_DL_DONE) ||
		(dhd_bus->pub.busstate == DBUS_STATE_DOWN) ||
		(dhd_bus->pub.busstate == DBUS_STATE_SLEEP)) {
		if (dhd_bus->drvintf && dhd_bus->drvintf->up) {
			err = dhd_bus->drvintf->up(dhd_bus->bus_info);

			if (err == DBUS_OK) {
				dbus_rxirbs_fill(dhd_bus);
			}
		}
	} else
		err = DBUS_ERR;

	return err;
}

/** higher layer requests us to 'down' the interface to the dongle. */
int
dbus_down(dbus_pub_t *pub)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dhd_bus == NULL)
		return DBUS_ERR;

	dbus_tx_timer_stop(dhd_bus);

	if (dhd_bus->pub.busstate == DBUS_STATE_UP ||
		dhd_bus->pub.busstate == DBUS_STATE_SLEEP) {
		if (dhd_bus->drvintf && dhd_bus->drvintf->down)
			return dhd_bus->drvintf->down(dhd_bus->bus_info);
	}

	return DBUS_ERR;
}

int
dbus_shutdown(dbus_pub_t *pub)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dhd_bus == NULL)
		return DBUS_ERR;

	if (dhd_bus->drvintf && dhd_bus->drvintf->shutdown)
		return dhd_bus->drvintf->shutdown(dhd_bus->bus_info);

	return DBUS_OK;
}

int
dbus_stop(struct dhd_bus *pub)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dhd_bus == NULL)
		return DBUS_ERR;

	if (dhd_bus->pub.busstate == DBUS_STATE_UP ||
		dhd_bus->pub.busstate == DBUS_STATE_SLEEP) {
		if (dhd_bus->drvintf && dhd_bus->drvintf->stop)
			return dhd_bus->drvintf->stop(dhd_bus->bus_info);
	}

	return DBUS_ERR;
}

int dbus_send_txdata(dbus_pub_t *dbus, void *pktbuf)
{
	return dbus_send_pkt(dbus, pktbuf, pktbuf /* pktinfo */);
}

int
dbus_send_buf(dbus_pub_t *pub, uint8 *buf, int len, void *info)
{
	return dbus_send_irb(pub, buf, len, NULL, info);
}

int
dbus_send_pkt(dbus_pub_t *pub, void *pkt, void *info)
{
	return dbus_send_irb(pub, NULL, 0, pkt, info);
}

int
dbus_send_ctl(struct dhd_bus *pub, uint8 *buf, int len)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;

	if (dhd_bus == NULL) {
		DBUSERR(("%s: dhd_bus is NULL\n", __FUNCTION__));
		return DBUS_ERR;
	}

	if (dhd_bus->pub.busstate == DBUS_STATE_UP ||
		dhd_bus->pub.busstate == DBUS_STATE_SLEEP) {
		if (dhd_bus->drvintf && dhd_bus->drvintf->send_ctl)
			return dhd_bus->drvintf->send_ctl(dhd_bus->bus_info, buf, len);
	} else {
		DBUSERR(("%s: bustate=%d\n", __FUNCTION__, dhd_bus->pub.busstate));
	}

	return DBUS_ERR;
}

int
dbus_recv_ctl(struct dhd_bus *pub, uint8 *buf, int len)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;

	if ((dhd_bus == NULL) || (buf == NULL))
		return DBUS_ERR;

	if (dhd_bus->pub.busstate == DBUS_STATE_UP ||
		dhd_bus->pub.busstate == DBUS_STATE_SLEEP) {
		if (dhd_bus->drvintf && dhd_bus->drvintf->recv_ctl)
			return dhd_bus->drvintf->recv_ctl(dhd_bus->bus_info, buf, len);
	}

	return DBUS_ERR;
}

/** Only called via RPC (Dec 2012) */
int
dbus_recv_bulk(dbus_pub_t *pub, uint32 ep_idx)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;

	dbus_irb_rx_t *rxirb;
	struct exec_parms args;
	int status;


	if (dhd_bus == NULL)
		return DBUS_ERR;

	args.qdeq.q = dhd_bus->rx_q;
	if (dhd_bus->pub.busstate == DBUS_STATE_UP) {
		if (dhd_bus->drvintf && dhd_bus->drvintf->recv_irb_from_ep) {
			if ((rxirb = (EXEC_RXLOCK(dhd_bus, q_deq_exec, &args))) != NULL) {
				status = dhd_bus->drvintf->recv_irb_from_ep(dhd_bus->bus_info,
					rxirb, ep_idx);
				if (status == DBUS_ERR_RXDROP) {
					bzero(rxirb, sizeof(dbus_irb_rx_t));
					args.qenq.q = dhd_bus->rx_q;
					args.qenq.b = (dbus_irb_t *) rxirb;
					EXEC_RXLOCK(dhd_bus, q_enq_exec, &args);
				}
			}
		}
	}

	return DBUS_ERR;
}

/** only called by dhd_cdc.c (Dec 2012) */
int
dbus_poll_intr(dbus_pub_t *pub)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;

	int status = DBUS_ERR;

	if (dhd_bus == NULL)
		return DBUS_ERR;

	if (dhd_bus->pub.busstate == DBUS_STATE_UP) {
		if (dhd_bus->drvintf && dhd_bus->drvintf->recv_irb_from_ep) {
			status = dhd_bus->drvintf->recv_irb_from_ep(dhd_bus->bus_info,
				NULL, 0xff);
		}
	}
	return status;
}

/** called by nobody (Dec 2012) */
void *
dbus_pktget(dbus_pub_t *pub, int len)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;

	if ((dhd_bus == NULL) || (len < 0))
		return NULL;

	return PKTGET(dhd_bus->pub.osh, len, TRUE);
}

/** called by nobody (Dec 2012) */
void
dbus_pktfree(dbus_pub_t *pub, void* pkt)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;

	if ((dhd_bus == NULL) || (pkt == NULL))
		return;

	PKTFREE(dhd_bus->pub.osh, pkt, TRUE);
}

/** called by nobody (Dec 2012) */
int
dbus_get_stats(dbus_pub_t *pub, dbus_stats_t *stats)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;

	if ((dhd_bus == NULL) || (stats == NULL))
		return DBUS_ERR;

	bcopy(&dhd_bus->pub.stats, stats, sizeof(dbus_stats_t));

	return DBUS_OK;
}

int
dbus_get_attrib(dhd_bus_t *pub, dbus_attrib_t *attrib)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;
	int err = DBUS_ERR;

	if ((dhd_bus == NULL) || (attrib == NULL))
		return DBUS_ERR;

	if (dhd_bus->drvintf && dhd_bus->drvintf->get_attrib) {
		err = dhd_bus->drvintf->get_attrib(dhd_bus->bus_info,
		&dhd_bus->pub.attrib);
	}

	bcopy(&dhd_bus->pub.attrib, attrib, sizeof(dbus_attrib_t));
	return err;
}

int
dbus_get_device_speed(dbus_pub_t *pub)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;

	if (dhd_bus == NULL)
		return INVALID_SPEED;

	return (dhd_bus->pub.device_speed);
}

int
dbus_set_config(dbus_pub_t *pub, dbus_config_t *config)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;
	int err = DBUS_ERR;

	if ((dhd_bus == NULL) || (config == NULL))
		return DBUS_ERR;

	if (dhd_bus->drvintf && dhd_bus->drvintf->set_config) {
		err = dhd_bus->drvintf->set_config(dhd_bus->bus_info,
			config);

		if ((config->config_id == DBUS_CONFIG_ID_AGGR_LIMIT) &&
			(!err) &&
			(dhd_bus->pub.busstate == DBUS_STATE_UP)) {
			dbus_rxirbs_fill(dhd_bus);
		}
	}

	return err;
}

int
dbus_get_config(dbus_pub_t *pub, dbus_config_t *config)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;
	int err = DBUS_ERR;

	if ((dhd_bus == NULL) || (config == NULL))
		return DBUS_ERR;

	if (dhd_bus->drvintf && dhd_bus->drvintf->get_config) {
		err = dhd_bus->drvintf->get_config(dhd_bus->bus_info,
		config);
	}

	return err;
}

int
dbus_set_errmask(dbus_pub_t *pub, uint32 mask)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;
	int err = DBUS_OK;

	if (dhd_bus == NULL)
		return DBUS_ERR;

	dhd_bus->errmask = mask;
	return err;
}

int
dbus_pnp_resume(dbus_pub_t *pub, int *fw_reload)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;
	int err = DBUS_ERR;
	bool fwdl = FALSE;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dhd_bus == NULL)
		return DBUS_ERR;

	if (dhd_bus->pub.busstate == DBUS_STATE_UP) {
		return DBUS_OK;
	}



	if (dhd_bus->drvintf->pnp) {
		err = dhd_bus->drvintf->pnp(dhd_bus->bus_info,
			DBUS_PNP_RESUME);
	}

	if (dhd_bus->drvintf->recv_needed) {
		if (dhd_bus->drvintf->recv_needed(dhd_bus->bus_info)) {
			/* Refill after sleep/hibernate */
			dbus_rxirbs_fill(dhd_bus);
		}
	}


	if (fw_reload)
		*fw_reload = fwdl;

	return err;
} /* dbus_pnp_resume */

int
dbus_pnp_sleep(dbus_pub_t *pub)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;
	int err = DBUS_ERR;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dhd_bus == NULL)
		return DBUS_ERR;

	dbus_tx_timer_stop(dhd_bus);

	if (dhd_bus->drvintf && dhd_bus->drvintf->pnp) {
		err = dhd_bus->drvintf->pnp(dhd_bus->bus_info,
			DBUS_PNP_SLEEP);
	}

	return err;
}

int
dbus_pnp_disconnect(dbus_pub_t *pub)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) pub;
	int err = DBUS_ERR;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dhd_bus == NULL)
		return DBUS_ERR;

	dbus_tx_timer_stop(dhd_bus);

	if (dhd_bus->drvintf && dhd_bus->drvintf->pnp) {
		err = dhd_bus->drvintf->pnp(dhd_bus->bus_info,
			DBUS_PNP_DISCONNECT);
	}

	return err;
}

int
dhd_bus_iovar_op(dhd_pub_t *dhdp, const char *name,
	void *params, int plen, void *arg, int len, bool set)
{
	dhd_bus_t *dhd_bus = (dhd_bus_t *) dhdp->bus;
	int err = DBUS_ERR;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dhd_bus == NULL)
		return DBUS_ERR;

	if (dhd_bus->drvintf && dhd_bus->drvintf->iovar_op) {
		err = dhd_bus->drvintf->iovar_op(dhd_bus->bus_info,
			name, params, plen, arg, len, set);
	}

	return err;
}


void *
dhd_dbus_txq(const dbus_pub_t *pub)
{
	return NULL;
}

uint
dhd_dbus_hdrlen(const dbus_pub_t *pub)
{
	return 0;
}

void *
dbus_get_devinfo(dbus_pub_t *pub)
{
	return pub->dev_info;
}

#if defined(BCM_REQUEST_FW) && !defined(EXTERNAL_FW_PATH)
static int
dbus_otp(dhd_bus_t *dhd_bus, uint16 *boardtype, uint16 *boardrev)
{
	uint32 value = 0;
	uint8 *cis;
	uint16 *otpinfo;
	uint32 i;
	bool standard_cis = TRUE;
	uint8 tup, tlen;
	bool btype_present = FALSE;
	bool brev_present = FALSE;
	int ret;
	int devid;
	uint16 btype = 0;
	uint16 brev = 0;
	uint32 otp_size = 0, otp_addr = 0, otp_sw_rgn = 0;

	if (dhd_bus == NULL || dhd_bus->drvintf == NULL ||
		dhd_bus->drvintf->readreg == NULL)
		return DBUS_ERR;

	devid = dhd_bus->pub.attrib.devid;

	if ((devid == BCM43234_CHIP_ID) || (devid == BCM43235_CHIP_ID) ||
		(devid == BCM43236_CHIP_ID)) {

		otp_size = BCM_OTP_SIZE_43236;
		otp_sw_rgn = BCM_OTP_SW_RGN_43236;
		otp_addr = BCM_OTP_ADDR_43236;

	} else {
		return DBUS_ERR_NVRAM;
	}

	cis = MALLOC(dhd_bus->pub.osh, otp_size * 2);
	if (cis == NULL)
		return DBUS_ERR;

	otpinfo = (uint16 *) cis;

	for (i = 0; i < otp_size; i++) {

		ret = dhd_bus->drvintf->readreg(dhd_bus->bus_info,
			otp_addr + ((otp_sw_rgn + i) << 1), 2, &value);

		if (ret != DBUS_OK) {
			MFREE(dhd_bus->pub.osh, cis, otp_size * 2);
			return ret;
		}
		otpinfo[i] = (uint16) value;
	}

	for (i = 0; i < (otp_size << 1); ) {

		if (standard_cis) {
			tup = cis[i++];
			if (tup == CISTPL_NULL || tup == CISTPL_END)
				tlen = 0;
			else
				tlen = cis[i++];
		} else {
			if (cis[i] == CISTPL_NULL || cis[i] == CISTPL_END) {
				tlen = 0;
				tup = cis[i];
			} else {
				tlen = cis[i];
				tup = CISTPL_BRCM_HNBU;
			}
			++i;
		}

		if (tup == CISTPL_END || (i + tlen) >= (otp_size << 1)) {
			break;
		}

		switch (tup) {

		case CISTPL_BRCM_HNBU:

			switch (cis[i]) {

			case HNBU_BOARDTYPE:

				btype = (uint16) ((cis[i + 2] << 8) + cis[i + 1]);
				btype_present = TRUE;
				DBUSTRACE(("%s: HNBU_BOARDTYPE = 0x%2x\n", __FUNCTION__,
					(uint32)btype));
				break;

			case HNBU_BOARDREV:

				if (tlen == 2)
					brev = (uint16) cis[i + 1];
				else
					brev = (uint16) ((cis[i + 2] << 8) + cis[i + 1]);
				brev_present = TRUE;
				DBUSTRACE(("%s: HNBU_BOARDREV =  0x%2x\n", __FUNCTION__,
					(uint32)*boardrev));
				break;

			case HNBU_HNBUCIS:
				DBUSTRACE(("%s: HNBU_HNBUCIS\n", __FUNCTION__));
				tlen++;
				standard_cis = FALSE;
				break;
			}
			break;
		}

		i += tlen;
	}

	MFREE(dhd_bus->pub.osh, cis, otp_size * 2);

	if (btype_present == TRUE && brev_present == TRUE) {
		*boardtype = btype;
		*boardrev = brev;
		DBUSERR(("otp boardtype = 0x%2x boardrev = 0x%2x\n",
			*boardtype, *boardrev));

		return DBUS_OK;
	}
	else
		return DBUS_ERR;
} /* dbus_otp */

static int
dbus_select_nvram(dhd_bus_t *dhd_bus, int8 *jumbonvram, int jumbolen,
uint16 boardtype, uint16 boardrev, int8 **nvram, int *nvram_len)
{
	/* Multi board nvram file format is contenation of nvram info with \r
	*  The file format for two contatenated set is
	*  \nBroadcom Jumbo Nvram file\nfirst_set\nsecond_set\nthird_set\n
	*/
	uint8 *nvram_start = NULL, *nvram_end = NULL;
	uint8 *nvram_start_prev = NULL, *nvram_end_prev = NULL;
	uint16 btype = 0, brev = 0;
	int len  = 0;
	char *field;

	*nvram = NULL;
	*nvram_len = 0;

	if (strncmp(BCM_JUMBO_START, jumbonvram, strlen(BCM_JUMBO_START))) {
		/* single nvram file in the native format */
		DBUSTRACE(("%s: Non-Jumbo NVRAM File \n", __FUNCTION__));
		*nvram = jumbonvram;
		*nvram_len = jumbolen;
		return DBUS_OK;
	} else {
		DBUSTRACE(("%s: Jumbo NVRAM File \n", __FUNCTION__));
	}

	/* sanity test the end of the config sets for proper ending */
	if (jumbonvram[jumbolen - 1] != BCM_JUMBO_NVRAM_DELIMIT ||
		jumbonvram[jumbolen - 2] != '\0') {
		DBUSERR(("%s: Bad Jumbo NVRAM file format\n", __FUNCTION__));
		return DBUS_JUMBO_BAD_FORMAT;
	}

	dhd_bus->nvram_nontxt = DBUS_NVRAM_NONTXT;

	nvram_start = jumbonvram;

	while (*nvram_start != BCM_JUMBO_NVRAM_DELIMIT && len < jumbolen) {

		/* consume the  first file info line
		* \nBroadcom Jumbo Nvram file\nfile1\n ...
		*/
		len ++;
		nvram_start ++;
	}

	nvram_end = nvram_start;

	/* search for "boardrev=0xabcd" and "boardtype=0x1234" information in
	* the concatenated nvram config files /sets
	*/

	while (len < jumbolen) {

		if (*nvram_end == '\0') {
			/* end of a config set is marked by multiple null characters */
			len ++;
			nvram_end ++;
			DBUSTRACE(("%s: NULL chr len = %d char = 0x%x\n", __FUNCTION__,
				len, *nvram_end));
			continue;

		} else if (*nvram_end == BCM_JUMBO_NVRAM_DELIMIT) {

			/* config set delimiter is reached */
			/* check if next config set is present or not
			*  return  if next config is not present
			*/

			/* start search the next config set */
			nvram_start_prev = nvram_start;
			nvram_end_prev = nvram_end;

			nvram_end ++;
			nvram_start = nvram_end;
			btype = brev = 0;
			DBUSTRACE(("%s: going to next record len = %d "
					"char = 0x%x \n", __FUNCTION__, len, *nvram_end));
			len ++;
			if (len >= jumbolen) {

				*nvram = nvram_start_prev;
				*nvram_len = (int)(nvram_end_prev - nvram_start_prev);

				DBUSTRACE(("%s: no more len = %d nvram_end = 0x%p",
					__FUNCTION__, len, nvram_end));

				return DBUS_JUMBO_NOMATCH;

			} else {
				continue;
			}

		} else {

			DBUSTRACE(("%s: config str = %s\n", __FUNCTION__, nvram_end));

			if (bcmp(nvram_end, "boardtype", strlen("boardtype")) == 0) {

				field = strchr(nvram_end, '=');
				field++;
				btype = (uint16)bcm_strtoul(field, NULL, 0);

				DBUSTRACE(("%s: btype = 0x%x boardtype = 0x%x \n", __FUNCTION__,
					btype, boardtype));
			}

			if (bcmp(nvram_end, "boardrev", strlen("boardrev")) == 0) {

				field = strchr(nvram_end, '=');
				field++;
				brev = (uint16)bcm_strtoul(field, NULL, 0);

				DBUSTRACE(("%s: brev = 0x%x boardrev = 0x%x \n", __FUNCTION__,
					brev, boardrev));
			}
			if (btype == boardtype && brev == boardrev) {
				/* locate nvram config set end - ie.find '\r' char */
				while (*nvram_end != BCM_JUMBO_NVRAM_DELIMIT)
					nvram_end ++;
				*nvram = nvram_start;
				*nvram_len = (int) (nvram_end - nvram_start);
				DBUSTRACE(("found len = %d nvram_start = 0x%p "
					"nvram_end = 0x%p\n", *nvram_len, nvram_start, nvram_end));
				return DBUS_OK;
			}

			len += (strlen(nvram_end) + 1);
			nvram_end += (strlen(nvram_end) + 1);
		}
	}
	return DBUS_JUMBO_NOMATCH;
} /* dbus_select_nvram */

#endif 

#define DBUS_NRXQ	50
#define DBUS_NTXQ	100

static void
dhd_dbus_send_complete(void *handle, void *info, int status)
{
	dhd_pub_t *dhd = (dhd_pub_t *)handle;
	void *pkt = info;

	if ((dhd == NULL) || (pkt == NULL)) {
		DBUSERR(("dhd or pkt is NULL\n"));
		return;
	}

	if (status == DBUS_OK) {
		dhd->dstats.tx_packets++;
	} else {
		DBUSERR(("TX error=%d\n", status));
		dhd->dstats.tx_errors++;
	}
#ifdef PROP_TXSTATUS
	if (DHD_PKTTAG_WLFCPKT(PKTTAG(pkt)) &&
		(dhd_wlfc_txcomplete(dhd, pkt, status == 0) != WLFC_UNSUPPORTED)) {
		return;
	}
#endif /* PROP_TXSTATUS */
	PKTFREE(dhd->osh, pkt, TRUE);
}

static void
dhd_dbus_recv_pkt(void *handle, void *pkt)
{
	uchar reorder_info_buf[WLHOST_REORDERDATA_TOTLEN];
	uint reorder_info_len;
	uint pkt_count;
	dhd_pub_t *dhd = (dhd_pub_t *)handle;
	int ifidx = 0;

	if (dhd == NULL) {
		DBUSERR(("%s: dhd is NULL\n", __FUNCTION__));
		return;
	}

	/* If the protocol uses a data header, check and remove it */
	if (dhd_prot_hdrpull(dhd, &ifidx, pkt, reorder_info_buf,
		&reorder_info_len) != 0) {
		DBUSERR(("rx protocol error\n"));
		PKTFREE(dhd->osh, pkt, FALSE);
		dhd->rx_errors++;
		return;
	}

	if (reorder_info_len) {
		/* Reordering info from the firmware */
		dhd_process_pkt_reorder_info(dhd, reorder_info_buf, reorder_info_len,
			&pkt, &pkt_count);
		if (pkt_count == 0)
			return;
	}
	else {
		pkt_count = 1;
	}
	dhd_rx_frame(dhd, ifidx, pkt, pkt_count, 0);
}

static void
dhd_dbus_recv_buf(void *handle, uint8 *buf, int len)
{
	dhd_pub_t *dhd = (dhd_pub_t *)handle;
	void *pkt;

	if (dhd == NULL) {
		DBUSERR(("%s: dhd is NULL\n", __FUNCTION__));
		return;
	}

	if ((pkt = PKTGET(dhd->osh, len, FALSE)) == NULL) {
		DBUSERR(("PKTGET (rx) failed=%d\n", len));
		return;
	}

	bcopy(buf, PKTDATA(dhd->osh, pkt), len);
	dhd_dbus_recv_pkt(dhd, pkt);
}

static void
dhd_dbus_txflowcontrol(void *handle, bool onoff)
{
	dhd_pub_t *dhd = (dhd_pub_t *)handle;
	bool wlfc_enabled = FALSE;

	if (dhd == NULL) {
		DBUSERR(("%s: dhd is NULL\n", __FUNCTION__));
		return;
	}

#ifdef PROP_TXSTATUS
	wlfc_enabled = (dhd_wlfc_flowcontrol(dhd, onoff, !onoff) != WLFC_UNSUPPORTED);
#endif

	if (!wlfc_enabled) {
		dhd_txflowcontrol(dhd, ALL_INTERFACES, onoff);
	}
}

static void
dhd_dbus_errhandler(void *handle, int err)
{
}

static void
dhd_dbus_ctl_complete(void *handle, int type, int status)
{
	dhd_pub_t *dhd = (dhd_pub_t *)handle;

	if (dhd == NULL) {
		DBUSERR(("%s: dhd is NULL\n", __FUNCTION__));
		return;
	}

	if (type == DBUS_CBCTL_READ) {
		if (status == DBUS_OK)
			dhd->rx_ctlpkts++;
		else
			dhd->rx_ctlerrs++;
	} else if (type == DBUS_CBCTL_WRITE) {
		if (status == DBUS_OK)
			dhd->tx_ctlpkts++;
		else
			dhd->tx_ctlerrs++;
	}

	dhd_prot_ctl_complete(dhd);
}

static void
dhd_dbus_state_change(void *handle, int state)
{
	dhd_pub_t *dhd = (dhd_pub_t *)handle;

	if (dhd == NULL) {
		DBUSERR(("%s: dhd is NULL\n", __FUNCTION__));
		return;
	}

	switch (state) {

		case DBUS_STATE_DL_NEEDED:
			DBUSERR(("%s: firmware request cannot be handled\n", __FUNCTION__));
			break;
		case DBUS_STATE_DOWN:
			DBUSTRACE(("%s: DBUS is down\n", __FUNCTION__));
			dhd->busstate = DHD_BUS_DOWN;
			break;
		case DBUS_STATE_UP:
			DBUSTRACE(("%s: DBUS is up\n", __FUNCTION__));
			dhd->busstate = DHD_BUS_DATA;
			break;
		default:
			break;
	}

	DBUSERR(("%s: DBUS current state=%d\n", __FUNCTION__, state));
}

static void *
dhd_dbus_pktget(void *handle, uint len, bool send)
{
	dhd_pub_t *dhd = (dhd_pub_t *)handle;
	void *p = NULL;

	if (dhd == NULL) {
		DBUSERR(("%s: dhd is NULL\n", __FUNCTION__));
		return NULL;
	}

	if (send == TRUE) {
		dhd_os_sdlock_txq(dhd);
		p = PKTGET(dhd->osh, len, TRUE);
		dhd_os_sdunlock_txq(dhd);
	} else {
		dhd_os_sdlock_rxq(dhd);
		p = PKTGET(dhd->osh, len, FALSE);
		dhd_os_sdunlock_rxq(dhd);
	}

	return p;
}

static void
dhd_dbus_pktfree(void *handle, void *p, bool send)
{
	dhd_pub_t *dhd = (dhd_pub_t *)handle;

	if (dhd == NULL) {
		DBUSERR(("%s: dhd is NULL\n", __FUNCTION__));
		return;
	}

	if (send == TRUE) {
#ifdef PROP_TXSTATUS
		if (DHD_PKTTAG_WLFCPKT(PKTTAG(p)) &&
			(dhd_wlfc_txcomplete(dhd, p, FALSE) != WLFC_UNSUPPORTED)) {
			return;
		}
#endif /* PROP_TXSTATUS */

		dhd_os_sdlock_txq(dhd);
		PKTFREE(dhd->osh, p, TRUE);
		dhd_os_sdunlock_txq(dhd);
	} else {
		dhd_os_sdlock_rxq(dhd);
		PKTFREE(dhd->osh, p, FALSE);
		dhd_os_sdunlock_rxq(dhd);
	}
}


static dbus_callbacks_t dhd_dbus_cbs = {
	dhd_dbus_send_complete,
	dhd_dbus_recv_buf,
	dhd_dbus_recv_pkt,
	dhd_dbus_txflowcontrol,
	dhd_dbus_errhandler,
	dhd_dbus_ctl_complete,
	dhd_dbus_state_change,
	dhd_dbus_pktget,
	dhd_dbus_pktfree
};

uint
dhd_bus_chip(struct dhd_bus *bus)
{
	ASSERT(bus != NULL);
	return bus->pub.attrib.devid;
}

uint
dhd_bus_chiprev(struct dhd_bus *bus)
{
	ASSERT(bus);
	ASSERT(bus != NULL);
	return bus->pub.attrib.chiprev;
}

void
dhd_bus_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf)
{
	bcm_bprintf(strbuf, "Bus USB\n");
}

void
dhd_bus_clearcounts(dhd_pub_t *dhdp)
{
}

int
dhd_bus_txdata(struct dhd_bus *bus, void *pktbuf)
{
	DBUSTRACE(("%s\n", __FUNCTION__));
	if (bus->txoff) {
		DBUSTRACE(("txoff\n"));
		return BCME_EPERM;
	}
	return dbus_send_txdata(&bus->pub, pktbuf);
}

static void
dhd_dbus_advertise_bus_cleanup(dhd_pub_t *dhdp)
{
	unsigned long flags;
	int timeleft;

	DHD_LINUX_GENERAL_LOCK(dhdp, flags);
	dhdp->busstate = DHD_BUS_DOWN_IN_PROGRESS;
	DHD_LINUX_GENERAL_UNLOCK(dhdp, flags);

	timeleft = dhd_os_busbusy_wait_negation(dhdp, &dhdp->dhd_bus_busy_state);
	if ((timeleft == 0) || (timeleft == 1)) {
		DBUSERR(("%s : Timeout due to dhd_bus_busy_state=0x%x\n",
				__FUNCTION__, dhdp->dhd_bus_busy_state));
		ASSERT(0);
	}

	return;
}

static void
dhd_dbus_advertise_bus_remove(dhd_pub_t *dhdp)
{
	unsigned long flags;
	int timeleft;

	DHD_LINUX_GENERAL_LOCK(dhdp, flags);
	dhdp->busstate = DHD_BUS_REMOVE;
	DHD_LINUX_GENERAL_UNLOCK(dhdp, flags);

	timeleft = dhd_os_busbusy_wait_negation(dhdp, &dhdp->dhd_bus_busy_state);
	if ((timeleft == 0) || (timeleft == 1)) {
		DBUSERR(("%s : Timeout due to dhd_bus_busy_state=0x%x\n",
				__FUNCTION__, dhdp->dhd_bus_busy_state));
		ASSERT(0);
	}

	return;
}

int
dhd_bus_devreset(dhd_pub_t *dhdp, uint8 flag)
{
	int bcmerror = 0;
	unsigned long flags;
	wifi_adapter_info_t *adapter = (wifi_adapter_info_t *)dhdp->adapter;

	if (flag == TRUE) {
		if (!dhdp->dongle_reset) {
			DBUSERR(("%s: == Power OFF ==\n", __FUNCTION__));
			dhd_dbus_advertise_bus_cleanup(dhdp);
			dhd_os_wd_timer(dhdp, 0);
#if !defined(IGNORE_ETH0_DOWN)
			/* Force flow control as protection when stop come before ifconfig_down */
			dhd_txflowcontrol(dhdp, ALL_INTERFACES, ON);
#endif /* !defined(IGNORE_ETH0_DOWN) */
			dbus_stop(dhdp->bus);

			dhdp->dongle_reset = TRUE;
			dhdp->up = FALSE;

			DHD_LINUX_GENERAL_LOCK(dhdp, flags);
			dhdp->busstate = DHD_BUS_DOWN;
			DHD_LINUX_GENERAL_UNLOCK(dhdp, flags);
			wifi_clr_adapter_status(adapter, WIFI_STATUS_FW_READY);

			printf("%s:  WLAN OFF DONE\n", __FUNCTION__);
			/* App can now remove power from device */
		} else
			bcmerror = BCME_ERROR;
	} else {
		/* App must have restored power to device before calling */
		printf("\n\n%s: == WLAN ON ==\n", __FUNCTION__);
		if (dhdp->dongle_reset) {
			/* Turn on WLAN */
			DHD_MUTEX_UNLOCK();
			wait_event_interruptible_timeout(adapter->status_event,
				wifi_get_adapter_status(adapter, WIFI_STATUS_FW_READY),
				msecs_to_jiffies(DHD_FW_READY_TIMEOUT));
			DHD_MUTEX_LOCK();
			bcmerror = dbus_up(dhdp->bus);
			if (bcmerror == BCME_OK) {
				dhdp->dongle_reset = FALSE;
				dhdp->up = TRUE;
#if !defined(IGNORE_ETH0_DOWN)
				/* Restore flow control  */
				dhd_txflowcontrol(dhdp, ALL_INTERFACES, OFF);
#endif 
				dhd_os_wd_timer(dhdp, dhd_watchdog_ms);

				DBUSTRACE(("%s: WLAN ON DONE\n", __FUNCTION__));
			} else {
				DBUSERR(("%s: failed to dbus_up with code %d\n", __FUNCTION__, bcmerror));
			}
		}
	}

#ifdef PKT_STATICS
	memset((uint8*) &tx_statics, 0, sizeof(pkt_statics_t));
#endif
	return bcmerror;
}

void
dhd_bus_update_fw_nv_path(struct dhd_bus *bus, char *pfw_path,
	char *pnv_path, char *pclm_path, char *pconf_path)
{
	DBUSTRACE(("%s\n", __FUNCTION__));

	if (bus == NULL) {
		DBUSERR(("%s: bus is NULL\n", __FUNCTION__));
		return;
	}

	bus->fw_path = pfw_path;
	bus->nv_path = pnv_path;
	bus->dhd->clm_path = pclm_path;
	bus->dhd->conf_path = pconf_path;

	dhd_conf_set_path_params(bus->dhd, bus->fw_path, bus->nv_path);

}

/*
 * hdrlen is space to reserve in pkt headroom for DBUS
 */
void *
dhd_dbus_probe_cb(void *arg, const char *desc, uint32 bustype,
	uint16 bus_no, uint16 slot, uint32 hdrlen)
{
	osl_t *osh = NULL;
	dhd_bus_t *bus = NULL;
	dhd_pub_t *pub = NULL;
	uint rxsz;
	int dlneeded = 0;
	wifi_adapter_info_t *adapter = NULL;

	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	adapter = dhd_wifi_platform_get_adapter(bustype, bus_no, slot);

	if (!g_pub) {
		/* Ask the OS interface part for an OSL handle */
		if (!(osh = osl_attach(NULL, bustype, TRUE))) {
			DBUSERR(("%s: OSL attach failed\n", __FUNCTION__));
			goto fail;
		}

		/* Attach to the dhd/OS interface */
		if (!(pub = dhd_attach(osh, bus, hdrlen, adapter))) {
			DBUSERR(("%s: dhd_attach failed\n", __FUNCTION__));
			goto fail;
		}
	} else {
		pub = g_pub;
		osh = pub->osh;
	}

	if (pub->bus) {
		DBUSERR(("%s: wrong probe\n", __FUNCTION__));
		goto fail;
	}

	rxsz = dhd_get_rxsz(pub);
	bus = dbus_attach(osh, rxsz, DBUS_NRXQ, DBUS_NTXQ, pub, &dhd_dbus_cbs, NULL, NULL);
	if (bus) {
		pub->bus = bus;
		bus->dhd = pub;

		dlneeded = dbus_dlneeded(bus);
		if (dlneeded >= 0) {
			if (!g_pub) {
				dhd_conf_reset(pub);
				dhd_conf_set_chiprev(pub, bus->pub.attrib.devid, bus->pub.attrib.chiprev);
				dhd_conf_preinit(pub);
			}
		}

		if (g_pub || dhd_download_fw_on_driverload) {
			if (dlneeded == 0) {
				wifi_set_adapter_status(adapter, WIFI_STATUS_FW_READY);
#ifdef BCM_REQUEST_FW
			} else if (dlneeded > 0) {
				dhd_set_path(bus->dhd);
				if (dbus_download_firmware(bus, bus->fw_path, bus->nv_path) != DBUS_OK)
					goto fail;
#endif
			} else {
				goto fail;
			}
		}
	} else {
		DBUSERR(("%s: dbus_attach failed\n", __FUNCTION__));
	}

	if (!g_pub) {
		/* Ok, finish the attach to the OS network interface */
		if (dhd_register_if(pub, 0, TRUE) != 0) {
			DBUSERR(("%s: dhd_register_if failed\n", __FUNCTION__));
			goto fail;
		}
		pub->hang_report  = TRUE;
#if defined(MULTIPLE_SUPPLICANT)
		wl_android_post_init(); // terence 20120530: fix critical section in dhd_open and dhdsdio_probe
#endif
		g_pub = pub;
	}

	DBUSTRACE(("%s: Exit\n", __FUNCTION__));
	wifi_clr_adapter_status(adapter, WIFI_STATUS_DETTACH);
	wifi_set_adapter_status(adapter, WIFI_STATUS_ATTACH);
	wake_up_interruptible(&adapter->status_event);
	/* This is passed to dhd_dbus_disconnect_cb */
	return bus;

fail:
	if (pub && pub->bus) {
		dbus_detach(pub->bus);
		pub->bus = NULL;
	}
	/* Release resources in reverse order */
	if (!g_pub) {
		if (pub) {
			dhd_detach(pub);
			dhd_free(pub);
		}
		if (osh) {
			osl_detach(osh);
		}
	}

	printf("%s: Failed\n", __FUNCTION__);
	return NULL;
}

void
dhd_dbus_disconnect_cb(void *arg)
{
	dhd_bus_t *bus = (dhd_bus_t *)arg;
	dhd_pub_t *pub = g_pub;
	osl_t *osh;
	wifi_adapter_info_t *adapter = NULL;

	adapter = (wifi_adapter_info_t *)pub->adapter;

	if (pub && !pub->dhd_remove && bus == NULL) {
		DBUSERR(("%s: bus is NULL\n", __FUNCTION__));
		return;
	}
	if (!adapter) {
		DBUSERR(("%s: adapter is NULL\n", __FUNCTION__));
		return;
	}

	printf("%s: Enter dhd_remove=%d on %s\n", __FUNCTION__,
		pub->dhd_remove, adapter->name);
	if (!pub->dhd_remove) {
		/* Advertise bus remove during rmmod */
		dhd_dbus_advertise_bus_remove(bus->dhd);
		dbus_detach(pub->bus);
		pub->bus = NULL;
		wifi_clr_adapter_status(adapter, WIFI_STATUS_ATTACH);
		wifi_set_adapter_status(adapter, WIFI_STATUS_DETTACH);
		wake_up_interruptible(&adapter->status_event);
	} else {
		osh = pub->osh;
		dhd_detach(pub);
		if (pub->bus) {
			dbus_detach(pub->bus);
			pub->bus = NULL;
		}
		dhd_free(pub);
		g_pub = NULL;
		if (MALLOCED(osh)) {
			DBUSERR(("%s: MEMORY LEAK %d bytes\n", __FUNCTION__, MALLOCED(osh)));
		}
		osl_detach(osh);
	}

	DBUSTRACE(("%s: Exit\n", __FUNCTION__));
}

#ifdef LINUX_EXTERNAL_MODULE_DBUS

static int __init
bcm_dbus_module_init(void)
{
	printf("Inserting bcm_dbus module \n");
	return 0;
}

static void __exit
bcm_dbus_module_exit(void)
{
	printf("Removing bcm_dbus module \n");
	return;
}

EXPORT_SYMBOL(dbus_pnp_sleep);
EXPORT_SYMBOL(dbus_get_devinfo);
EXPORT_SYMBOL(dbus_detach);
EXPORT_SYMBOL(dbus_get_attrib);
EXPORT_SYMBOL(dbus_down);
EXPORT_SYMBOL(dbus_pnp_resume);
EXPORT_SYMBOL(dbus_set_config);
EXPORT_SYMBOL(dbus_flowctrl_rx);
EXPORT_SYMBOL(dbus_up);
EXPORT_SYMBOL(dbus_get_device_speed);
EXPORT_SYMBOL(dbus_send_pkt);
EXPORT_SYMBOL(dbus_recv_ctl);
EXPORT_SYMBOL(dbus_attach);

MODULE_LICENSE("GPL");

module_init(bcm_dbus_module_init);
module_exit(bcm_dbus_module_exit);

#endif  /* #ifdef LINUX_EXTERNAL_MODULE_DBUS */
