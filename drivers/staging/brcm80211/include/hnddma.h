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

#ifndef	_hnddma_h_
#define	_hnddma_h_

#ifndef _hnddma_pub_
#define _hnddma_pub_
typedef const struct hnddma_pub hnddma_t;
#endif				/* _hnddma_pub_ */

/* range param for dma_getnexttxp() and dma_txreclaim */
typedef enum txd_range {
	HNDDMA_RANGE_ALL = 1,
	HNDDMA_RANGE_TRANSMITTED,
	HNDDMA_RANGE_TRANSFERED
} txd_range_t;

/* dma function type */
typedef void (*di_detach_t) (hnddma_t *dmah);
typedef bool(*di_txreset_t) (hnddma_t *dmah);
typedef bool(*di_rxreset_t) (hnddma_t *dmah);
typedef bool(*di_rxidle_t) (hnddma_t *dmah);
typedef void (*di_txinit_t) (hnddma_t *dmah);
typedef bool(*di_txenabled_t) (hnddma_t *dmah);
typedef void (*di_rxinit_t) (hnddma_t *dmah);
typedef void (*di_txsuspend_t) (hnddma_t *dmah);
typedef void (*di_txresume_t) (hnddma_t *dmah);
typedef bool(*di_txsuspended_t) (hnddma_t *dmah);
typedef bool(*di_txsuspendedidle_t) (hnddma_t *dmah);
typedef int (*di_txfast_t) (hnddma_t *dmah, struct sk_buff *p, bool commit);
typedef int (*di_txunframed_t) (hnddma_t *dmah, void *p, uint len,
				bool commit);
typedef void *(*di_getpos_t) (hnddma_t *di, bool direction);
typedef void (*di_fifoloopbackenable_t) (hnddma_t *dmah);
typedef bool(*di_txstopped_t) (hnddma_t *dmah);
typedef bool(*di_rxstopped_t) (hnddma_t *dmah);
typedef bool(*di_rxenable_t) (hnddma_t *dmah);
typedef bool(*di_rxenabled_t) (hnddma_t *dmah);
typedef void *(*di_rx_t) (hnddma_t *dmah);
typedef bool(*di_rxfill_t) (hnddma_t *dmah);
typedef void (*di_txreclaim_t) (hnddma_t *dmah, txd_range_t range);
typedef void (*di_rxreclaim_t) (hnddma_t *dmah);
typedef unsigned long (*di_getvar_t) (hnddma_t *dmah, const char *name);
typedef void *(*di_getnexttxp_t) (hnddma_t *dmah, txd_range_t range);
typedef void *(*di_getnextrxp_t) (hnddma_t *dmah, bool forceall);
typedef void *(*di_peeknexttxp_t) (hnddma_t *dmah);
typedef void *(*di_peeknextrxp_t) (hnddma_t *dmah);
typedef void (*di_rxparam_get_t) (hnddma_t *dmah, u16 *rxoffset,
				  u16 *rxbufsize);
typedef void (*di_txblock_t) (hnddma_t *dmah);
typedef void (*di_txunblock_t) (hnddma_t *dmah);
typedef uint(*di_txactive_t) (hnddma_t *dmah);
typedef void (*di_txrotate_t) (hnddma_t *dmah);
typedef void (*di_counterreset_t) (hnddma_t *dmah);
typedef uint(*di_ctrlflags_t) (hnddma_t *dmah, uint mask, uint flags);
typedef char *(*di_dump_t) (hnddma_t *dmah, struct bcmstrbuf *b,
			    bool dumpring);
typedef char *(*di_dumptx_t) (hnddma_t *dmah, struct bcmstrbuf *b,
			      bool dumpring);
typedef char *(*di_dumprx_t) (hnddma_t *dmah, struct bcmstrbuf *b,
			      bool dumpring);
typedef uint(*di_rxactive_t) (hnddma_t *dmah);
typedef uint(*di_txpending_t) (hnddma_t *dmah);
typedef uint(*di_txcommitted_t) (hnddma_t *dmah);

/* dma opsvec */
typedef struct di_fcn_s {
	di_detach_t detach;
	di_txinit_t txinit;
	di_txreset_t txreset;
	di_txenabled_t txenabled;
	di_txsuspend_t txsuspend;
	di_txresume_t txresume;
	di_txsuspended_t txsuspended;
	di_txsuspendedidle_t txsuspendedidle;
	di_txfast_t txfast;
	di_txunframed_t txunframed;
	di_getpos_t getpos;
	di_txstopped_t txstopped;
	di_txreclaim_t txreclaim;
	di_getnexttxp_t getnexttxp;
	di_peeknexttxp_t peeknexttxp;
	di_txblock_t txblock;
	di_txunblock_t txunblock;
	di_txactive_t txactive;
	di_txrotate_t txrotate;

	di_rxinit_t rxinit;
	di_rxreset_t rxreset;
	di_rxidle_t rxidle;
	di_rxstopped_t rxstopped;
	di_rxenable_t rxenable;
	di_rxenabled_t rxenabled;
	di_rx_t rx;
	di_rxfill_t rxfill;
	di_rxreclaim_t rxreclaim;
	di_getnextrxp_t getnextrxp;
	di_peeknextrxp_t peeknextrxp;
	di_rxparam_get_t rxparam_get;

	di_fifoloopbackenable_t fifoloopbackenable;
	di_getvar_t d_getvar;
	di_counterreset_t counterreset;
	di_ctrlflags_t ctrlflags;
	di_dump_t dump;
	di_dumptx_t dumptx;
	di_dumprx_t dumprx;
	di_rxactive_t rxactive;
	di_txpending_t txpending;
	di_txcommitted_t txcommitted;
	uint endnum;
} di_fcn_t;

/*
 * Exported data structure (read-only)
 */
/* export structure */
struct hnddma_pub {
	const di_fcn_t *di_fn;	/* DMA function pointers */
	uint txavail;		/* # free tx descriptors */
	uint dmactrlflags;	/* dma control flags */

	/* rx error counters */
	uint rxgiants;		/* rx giant frames */
	uint rxnobuf;		/* rx out of dma descriptors */
	/* tx error counters */
	uint txnobuf;		/* tx out of dma descriptors */
};

extern hnddma_t *dma_attach(struct osl_info *osh, char *name, si_t *sih,
			    void *dmaregstx, void *dmaregsrx, uint ntxd,
			    uint nrxd, uint rxbufsize, int rxextheadroom,
			    uint nrxpost, uint rxoffset, uint *msg_level);
#ifdef BCMDMA32

#define dma_detach(di)			((di)->di_fn->detach(di))
#define dma_txreset(di)			((di)->di_fn->txreset(di))
#define dma_rxreset(di)			((di)->di_fn->rxreset(di))
#define dma_rxidle(di)			((di)->di_fn->rxidle(di))
#define dma_txinit(di)                  ((di)->di_fn->txinit(di))
#define dma_txenabled(di)               ((di)->di_fn->txenabled(di))
#define dma_rxinit(di)                  ((di)->di_fn->rxinit(di))
#define dma_txsuspend(di)               ((di)->di_fn->txsuspend(di))
#define dma_txresume(di)                ((di)->di_fn->txresume(di))
#define dma_txsuspended(di)             ((di)->di_fn->txsuspended(di))
#define dma_txsuspendedidle(di)         ((di)->di_fn->txsuspendedidle(di))
#define dma_txfast(di, p, commit)	((di)->di_fn->txfast(di, p, commit))
#define dma_fifoloopbackenable(di)      ((di)->di_fn->fifoloopbackenable(di))
#define dma_txstopped(di)               ((di)->di_fn->txstopped(di))
#define dma_rxstopped(di)               ((di)->di_fn->rxstopped(di))
#define dma_rxenable(di)                ((di)->di_fn->rxenable(di))
#define dma_rxenabled(di)               ((di)->di_fn->rxenabled(di))
#define dma_rx(di)                      ((di)->di_fn->rx(di))
#define dma_rxfill(di)                  ((di)->di_fn->rxfill(di))
#define dma_txreclaim(di, range)	((di)->di_fn->txreclaim(di, range))
#define dma_rxreclaim(di)               ((di)->di_fn->rxreclaim(di))
#define dma_getvar(di, name)		((di)->di_fn->d_getvar(di, name))
#define dma_getnexttxp(di, range)	((di)->di_fn->getnexttxp(di, range))
#define dma_getnextrxp(di, forceall)    ((di)->di_fn->getnextrxp(di, forceall))
#define dma_peeknexttxp(di)             ((di)->di_fn->peeknexttxp(di))
#define dma_peeknextrxp(di)             ((di)->di_fn->peeknextrxp(di))
#define dma_rxparam_get(di, off, bufs)	((di)->di_fn->rxparam_get(di, off, bufs))

#define dma_txblock(di)                 ((di)->di_fn->txblock(di))
#define dma_txunblock(di)               ((di)->di_fn->txunblock(di))
#define dma_txactive(di)                ((di)->di_fn->txactive(di))
#define dma_rxactive(di)                ((di)->di_fn->rxactive(di))
#define dma_txrotate(di)                ((di)->di_fn->txrotate(di))
#define dma_counterreset(di)            ((di)->di_fn->counterreset(di))
#define dma_ctrlflags(di, mask, flags)  ((di)->di_fn->ctrlflags((di), (mask), (flags)))
#define dma_txpending(di)		((di)->di_fn->txpending(di))
#define dma_txcommitted(di)		((di)->di_fn->txcommitted(di))

#else				/* BCMDMA32 */
extern const di_fcn_t dma64proc;

#define dma_detach(di)			(dma64proc.detach(di))
#define dma_txreset(di)			(dma64proc.txreset(di))
#define dma_rxreset(di)			(dma64proc.rxreset(di))
#define dma_rxidle(di)			(dma64proc.rxidle(di))
#define dma_txinit(di)                  (dma64proc.txinit(di))
#define dma_txenabled(di)               (dma64proc.txenabled(di))
#define dma_rxinit(di)                  (dma64proc.rxinit(di))
#define dma_txsuspend(di)               (dma64proc.txsuspend(di))
#define dma_txresume(di)                (dma64proc.txresume(di))
#define dma_txsuspended(di)             (dma64proc.txsuspended(di))
#define dma_txsuspendedidle(di)         (dma64proc.txsuspendedidle(di))
#define dma_txfast(di, p, commit)	(dma64proc.txfast(di, p, commit))
#define dma_txunframed(di, p, l, commit)(dma64proc.txunframed(di, p, l, commit))
#define dma_getpos(di, dir)		(dma64proc.getpos(di, dir))
#define dma_fifoloopbackenable(di)      (dma64proc.fifoloopbackenable(di))
#define dma_txstopped(di)               (dma64proc.txstopped(di))
#define dma_rxstopped(di)               (dma64proc.rxstopped(di))
#define dma_rxenable(di)                (dma64proc.rxenable(di))
#define dma_rxenabled(di)               (dma64proc.rxenabled(di))
#define dma_rx(di)                      (dma64proc.rx(di))
#define dma_rxfill(di)                  (dma64proc.rxfill(di))
#define dma_txreclaim(di, range)	(dma64proc.txreclaim(di, range))
#define dma_rxreclaim(di)               (dma64proc.rxreclaim(di))
#define dma_getvar(di, name)		(dma64proc.d_getvar(di, name))
#define dma_getnexttxp(di, range)	(dma64proc.getnexttxp(di, range))
#define dma_getnextrxp(di, forceall)    (dma64proc.getnextrxp(di, forceall))
#define dma_peeknexttxp(di)             (dma64proc.peeknexttxp(di))
#define dma_peeknextrxp(di)             (dma64proc.peeknextrxp(di))
#define dma_rxparam_get(di, off, bufs)	(dma64proc.rxparam_get(di, off, bufs))

#define dma_txblock(di)                 (dma64proc.txblock(di))
#define dma_txunblock(di)               (dma64proc.txunblock(di))
#define dma_txactive(di)                (dma64proc.txactive(di))
#define dma_rxactive(di)                (dma64proc.rxactive(di))
#define dma_txrotate(di)                (dma64proc.txrotate(di))
#define dma_counterreset(di)            (dma64proc.counterreset(di))
#define dma_ctrlflags(di, mask, flags)  (dma64proc.ctrlflags((di), (mask), (flags)))
#define dma_txpending(di)		(dma64proc.txpending(di))
#define dma_txcommitted(di)		(dma64proc.txcommitted(di))

#endif				/* BCMDMA32 */

/* return addresswidth allowed
 * This needs to be done after SB attach but before dma attach.
 * SB attach provides ability to probe backplane and dma core capabilities
 * This info is needed by DMA_ALLOC_CONSISTENT in dma attach
 */
extern uint dma_addrwidth(si_t *sih, void *dmaregs);

/* pio helpers */
extern void dma_txpioloopback(struct osl_info *osh, dma32regs_t *);

#endif				/* _hnddma_h_ */
