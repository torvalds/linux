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

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <bcmdefs.h>
#include <bcmdevs.h>
#include <hndsoc.h>
#include <bcmutils.h>
#include <aiutils.h>

#include <sbhnddma.h>
#include <hnddma.h>

#if defined(__mips__)
#include <asm/addrspace.h>
#endif

#ifdef BRCM_FULLMAC
#error "hnddma.c shouldn't be needed for FULLMAC"
#endif

/* debug/trace */
#ifdef BCMDBG
#define	DMA_ERROR(args) \
	do { \
		if (!(*di->msg_level & 1)) \
			; \
		else \
			printk args; \
	} while (0)
#define	DMA_TRACE(args) \
	do { \
		if (!(*di->msg_level & 2)) \
			; \
		else \
			printk args; \
	} while (0)
#else
#define	DMA_ERROR(args)
#define	DMA_TRACE(args)
#endif				/* BCMDBG */

#define	DMA_NONE(args)

#define d64txregs	dregs.d64_u.txregs_64
#define d64rxregs	dregs.d64_u.rxregs_64
#define txd64		dregs.d64_u.txd_64
#define rxd64		dregs.d64_u.rxd_64

/* default dma message level (if input msg_level pointer is null in dma_attach()) */
static uint dma_msg_level;

#define	MAXNAMEL	8	/* 8 char names */

#define	DI_INFO(dmah)	((dma_info_t *)dmah)

#define R_SM(r)		(*(r))
#define W_SM(r, v)	(*(r) = (v))

/* dma engine software state */
typedef struct dma_info {
	struct hnddma_pub hnddma; /* exported structure */
	uint *msg_level;	/* message level pointer */
	char name[MAXNAMEL];	/* callers name for diag msgs */

	void *pbus;		/* bus handle */

	bool dma64;		/* this dma engine is operating in 64-bit mode */
	bool addrext;		/* this dma engine supports DmaExtendedAddrChanges */

	union {
		struct {
			dma64regs_t *txregs_64;	/* 64-bit dma tx engine registers */
			dma64regs_t *rxregs_64;	/* 64-bit dma rx engine registers */
			dma64dd_t *txd_64;	/* pointer to dma64 tx descriptor ring */
			dma64dd_t *rxd_64;	/* pointer to dma64 rx descriptor ring */
		} d64_u;
	} dregs;

	u16 dmadesc_align;	/* alignment requirement for dma descriptors */

	u16 ntxd;		/* # tx descriptors tunable */
	u16 txin;		/* index of next descriptor to reclaim */
	u16 txout;		/* index of next descriptor to post */
	void **txp;		/* pointer to parallel array of pointers to packets */
	hnddma_seg_map_t *txp_dmah;	/* DMA MAP meta-data handle */
	dmaaddr_t txdpa;	/* Aligned physical address of descriptor ring */
	dmaaddr_t txdpaorig;	/* Original physical address of descriptor ring */
	u16 txdalign;	/* #bytes added to alloc'd mem to align txd */
	u32 txdalloc;	/* #bytes allocated for the ring */
	u32 xmtptrbase;	/* When using unaligned descriptors, the ptr register
				 * is not just an index, it needs all 13 bits to be
				 * an offset from the addr register.
				 */

	u16 nrxd;		/* # rx descriptors tunable */
	u16 rxin;		/* index of next descriptor to reclaim */
	u16 rxout;		/* index of next descriptor to post */
	void **rxp;		/* pointer to parallel array of pointers to packets */
	hnddma_seg_map_t *rxp_dmah;	/* DMA MAP meta-data handle */
	dmaaddr_t rxdpa;	/* Aligned physical address of descriptor ring */
	dmaaddr_t rxdpaorig;	/* Original physical address of descriptor ring */
	u16 rxdalign;	/* #bytes added to alloc'd mem to align rxd */
	u32 rxdalloc;	/* #bytes allocated for the ring */
	u32 rcvptrbase;	/* Base for ptr reg when using unaligned descriptors */

	/* tunables */
	unsigned int rxbufsize;	/* rx buffer size in bytes,
				 * not including the extra headroom
				 */
	uint rxextrahdrroom;	/* extra rx headroom, reverseved to assist upper stack
				 *  e.g. some rx pkt buffers will be bridged to tx side
				 *  without byte copying. The extra headroom needs to be
				 *  large enough to fit txheader needs.
				 *  Some dongle driver may not need it.
				 */
	uint nrxpost;		/* # rx buffers to keep posted */
	unsigned int rxoffset;	/* rxcontrol offset */
	uint ddoffsetlow;	/* add to get dma address of descriptor ring, low 32 bits */
	uint ddoffsethigh;	/*   high 32 bits */
	uint dataoffsetlow;	/* add to get dma address of data buffer, low 32 bits */
	uint dataoffsethigh;	/*   high 32 bits */
	bool aligndesc_4k;	/* descriptor base need to be aligned or not */
} dma_info_t;

/* DMA Scatter-gather list is supported. Note this is limited to TX direction only */
#ifdef BCMDMASGLISTOSL
#define DMASGLIST_ENAB true
#else
#define DMASGLIST_ENAB false
#endif				/* BCMDMASGLISTOSL */

/* descriptor bumping macros */
#define	XXD(x, n)	((x) & ((n) - 1))	/* faster than %, but n must be power of 2 */
#define	TXD(x)		XXD((x), di->ntxd)
#define	RXD(x)		XXD((x), di->nrxd)
#define	NEXTTXD(i)	TXD((i) + 1)
#define	PREVTXD(i)	TXD((i) - 1)
#define	NEXTRXD(i)	RXD((i) + 1)
#define	PREVRXD(i)	RXD((i) - 1)

#define	NTXDACTIVE(h, t)	TXD((t) - (h))
#define	NRXDACTIVE(h, t)	RXD((t) - (h))

/* macros to convert between byte offsets and indexes */
#define	B2I(bytes, type)	((bytes) / sizeof(type))
#define	I2B(index, type)	((index) * sizeof(type))

#define	PCI32ADDR_HIGH		0xc0000000	/* address[31:30] */
#define	PCI32ADDR_HIGH_SHIFT	30	/* address[31:30] */

#define	PCI64ADDR_HIGH		0x80000000	/* address[63] */
#define	PCI64ADDR_HIGH_SHIFT	31	/* address[63] */

/* Common prototypes */
static bool _dma_isaddrext(dma_info_t *di);
static bool _dma_descriptor_align(dma_info_t *di);
static bool _dma_alloc(dma_info_t *di, uint direction);
static void _dma_detach(dma_info_t *di);
static void _dma_ddtable_init(dma_info_t *di, uint direction, dmaaddr_t pa);
static void _dma_rxinit(dma_info_t *di);
static void *_dma_rx(dma_info_t *di);
static bool _dma_rxfill(dma_info_t *di);
static void _dma_rxreclaim(dma_info_t *di);
static void _dma_rxenable(dma_info_t *di);
static void *_dma_getnextrxp(dma_info_t *di, bool forceall);
static void _dma_rx_param_get(dma_info_t *di, u16 *rxoffset,
			      u16 *rxbufsize);

static void _dma_txblock(dma_info_t *di);
static void _dma_txunblock(dma_info_t *di);
static uint _dma_txactive(dma_info_t *di);
static uint _dma_rxactive(dma_info_t *di);
static uint _dma_txpending(dma_info_t *di);
static uint _dma_txcommitted(dma_info_t *di);

static void *_dma_peeknexttxp(dma_info_t *di);
static void *_dma_peeknextrxp(dma_info_t *di);
static unsigned long _dma_getvar(dma_info_t *di, const char *name);
static void _dma_counterreset(dma_info_t *di);
static void _dma_fifoloopbackenable(dma_info_t *di);
static uint _dma_ctrlflags(dma_info_t *di, uint mask, uint flags);
static u8 dma_align_sizetobits(uint size);
static void *dma_ringalloc(dma_info_t *di, u32 boundary, uint size,
			   u16 *alignbits, uint *alloced,
			   dmaaddr_t *descpa);

/* Prototypes for 64-bit routines */
static bool dma64_alloc(dma_info_t *di, uint direction);
static bool dma64_txreset(dma_info_t *di);
static bool dma64_rxreset(dma_info_t *di);
static bool dma64_txsuspendedidle(dma_info_t *di);
static int dma64_txfast(dma_info_t *di, struct sk_buff *p0, bool commit);
static int dma64_txunframed(dma_info_t *di, void *p0, uint len, bool commit);
static void *dma64_getpos(dma_info_t *di, bool direction);
static void *dma64_getnexttxp(dma_info_t *di, txd_range_t range);
static void *dma64_getnextrxp(dma_info_t *di, bool forceall);
static void dma64_txrotate(dma_info_t *di);

static bool dma64_rxidle(dma_info_t *di);
static void dma64_txinit(dma_info_t *di);
static bool dma64_txenabled(dma_info_t *di);
static void dma64_txsuspend(dma_info_t *di);
static void dma64_txresume(dma_info_t *di);
static bool dma64_txsuspended(dma_info_t *di);
static void dma64_txreclaim(dma_info_t *di, txd_range_t range);
static bool dma64_txstopped(dma_info_t *di);
static bool dma64_rxstopped(dma_info_t *di);
static bool dma64_rxenabled(dma_info_t *di);
static bool _dma64_addrext(dma64regs_t *dma64regs);

static inline u32 parity32(u32 data);

const di_fcn_t dma64proc = {
	(di_detach_t) _dma_detach,
	(di_txinit_t) dma64_txinit,
	(di_txreset_t) dma64_txreset,
	(di_txenabled_t) dma64_txenabled,
	(di_txsuspend_t) dma64_txsuspend,
	(di_txresume_t) dma64_txresume,
	(di_txsuspended_t) dma64_txsuspended,
	(di_txsuspendedidle_t) dma64_txsuspendedidle,
	(di_txfast_t) dma64_txfast,
	(di_txunframed_t) dma64_txunframed,
	(di_getpos_t) dma64_getpos,
	(di_txstopped_t) dma64_txstopped,
	(di_txreclaim_t) dma64_txreclaim,
	(di_getnexttxp_t) dma64_getnexttxp,
	(di_peeknexttxp_t) _dma_peeknexttxp,
	(di_txblock_t) _dma_txblock,
	(di_txunblock_t) _dma_txunblock,
	(di_txactive_t) _dma_txactive,
	(di_txrotate_t) dma64_txrotate,

	(di_rxinit_t) _dma_rxinit,
	(di_rxreset_t) dma64_rxreset,
	(di_rxidle_t) dma64_rxidle,
	(di_rxstopped_t) dma64_rxstopped,
	(di_rxenable_t) _dma_rxenable,
	(di_rxenabled_t) dma64_rxenabled,
	(di_rx_t) _dma_rx,
	(di_rxfill_t) _dma_rxfill,
	(di_rxreclaim_t) _dma_rxreclaim,
	(di_getnextrxp_t) _dma_getnextrxp,
	(di_peeknextrxp_t) _dma_peeknextrxp,
	(di_rxparam_get_t) _dma_rx_param_get,

	(di_fifoloopbackenable_t) _dma_fifoloopbackenable,
	(di_getvar_t) _dma_getvar,
	(di_counterreset_t) _dma_counterreset,
	(di_ctrlflags_t) _dma_ctrlflags,
	NULL,
	NULL,
	NULL,
	(di_rxactive_t) _dma_rxactive,
	(di_txpending_t) _dma_txpending,
	(di_txcommitted_t) _dma_txcommitted,
	39
};

struct hnddma_pub *dma_attach(char *name, si_t *sih,
		     void *dmaregstx, void *dmaregsrx, uint ntxd,
		     uint nrxd, uint rxbufsize, int rxextheadroom,
		     uint nrxpost, uint rxoffset, uint *msg_level)
{
	dma_info_t *di;
	uint size;

	/* allocate private info structure */
	di = kzalloc(sizeof(dma_info_t), GFP_ATOMIC);
	if (di == NULL) {
#ifdef BCMDBG
		printk(KERN_ERR "dma_attach: out of memory\n");
#endif
		return NULL;
	}

	di->msg_level = msg_level ? msg_level : &dma_msg_level;


	di->dma64 = ((ai_core_sflags(sih, 0, 0) & SISF_DMA64) == SISF_DMA64);

	/* init dma reg pointer */
	di->d64txregs = (dma64regs_t *) dmaregstx;
	di->d64rxregs = (dma64regs_t *) dmaregsrx;
	di->hnddma.di_fn = (const di_fcn_t *)&dma64proc;

	/* Default flags (which can be changed by the driver calling dma_ctrlflags
	 * before enable): For backwards compatibility both Rx Overflow Continue
	 * and Parity are DISABLED.
	 * supports it.
	 */
	di->hnddma.di_fn->ctrlflags(&di->hnddma, DMA_CTRL_ROC | DMA_CTRL_PEN,
				    0);

	DMA_TRACE(("%s: dma_attach: %s flags 0x%x ntxd %d nrxd %d "
		   "rxbufsize %d rxextheadroom %d nrxpost %d rxoffset %d "
		   "dmaregstx %p dmaregsrx %p\n", name, "DMA64",
		   di->hnddma.dmactrlflags, ntxd, nrxd, rxbufsize,
		   rxextheadroom, nrxpost, rxoffset, dmaregstx, dmaregsrx));

	/* make a private copy of our callers name */
	strncpy(di->name, name, MAXNAMEL);
	di->name[MAXNAMEL - 1] = '\0';

	di->pbus = ((struct si_info *)sih)->pbus;

	/* save tunables */
	di->ntxd = (u16) ntxd;
	di->nrxd = (u16) nrxd;

	/* the actual dma size doesn't include the extra headroom */
	di->rxextrahdrroom =
	    (rxextheadroom == -1) ? BCMEXTRAHDROOM : rxextheadroom;
	if (rxbufsize > BCMEXTRAHDROOM)
		di->rxbufsize = (u16) (rxbufsize - di->rxextrahdrroom);
	else
		di->rxbufsize = (u16) rxbufsize;

	di->nrxpost = (u16) nrxpost;
	di->rxoffset = (u8) rxoffset;

	/*
	 * figure out the DMA physical address offset for dd and data
	 *     PCI/PCIE: they map silicon backplace address to zero based memory, need offset
	 *     Other bus: use zero
	 *     SI_BUS BIGENDIAN kludge: use sdram swapped region for data buffer, not descriptor
	 */
	di->ddoffsetlow = 0;
	di->dataoffsetlow = 0;
	/* for pci bus, add offset */
	if (sih->bustype == PCI_BUS) {
		/* pcie with DMA64 */
		di->ddoffsetlow = 0;
		di->ddoffsethigh = SI_PCIE_DMA_H32;
		di->dataoffsetlow = di->ddoffsetlow;
		di->dataoffsethigh = di->ddoffsethigh;
	}
#if defined(__mips__) && defined(IL_BIGENDIAN)
	di->dataoffsetlow = di->dataoffsetlow + SI_SDRAM_SWAPPED;
#endif				/* defined(__mips__) && defined(IL_BIGENDIAN) */
	/* WAR64450 : DMACtl.Addr ext fields are not supported in SDIOD core. */
	if ((ai_coreid(sih) == SDIOD_CORE_ID)
	    && ((ai_corerev(sih) > 0) && (ai_corerev(sih) <= 2)))
		di->addrext = 0;
	else if ((ai_coreid(sih) == I2S_CORE_ID) &&
		 ((ai_corerev(sih) == 0) || (ai_corerev(sih) == 1)))
		di->addrext = 0;
	else
		di->addrext = _dma_isaddrext(di);

	/* does the descriptors need to be aligned and if yes, on 4K/8K or not */
	di->aligndesc_4k = _dma_descriptor_align(di);
	if (di->aligndesc_4k) {
		di->dmadesc_align = D64RINGALIGN_BITS;
		if ((ntxd < D64MAXDD / 2) && (nrxd < D64MAXDD / 2)) {
			/* for smaller dd table, HW relax alignment reqmnt */
			di->dmadesc_align = D64RINGALIGN_BITS - 1;
		}
	} else
		di->dmadesc_align = 4;	/* 16 byte alignment */

	DMA_NONE(("DMA descriptor align_needed %d, align %d\n",
		  di->aligndesc_4k, di->dmadesc_align));

	/* allocate tx packet pointer vector */
	if (ntxd) {
		size = ntxd * sizeof(void *);
		di->txp = kzalloc(size, GFP_ATOMIC);
		if (di->txp == NULL) {
			DMA_ERROR(("%s: dma_attach: out of tx memory\n", di->name));
			goto fail;
		}
	}

	/* allocate rx packet pointer vector */
	if (nrxd) {
		size = nrxd * sizeof(void *);
		di->rxp = kzalloc(size, GFP_ATOMIC);
		if (di->rxp == NULL) {
			DMA_ERROR(("%s: dma_attach: out of rx memory\n", di->name));
			goto fail;
		}
	}

	/* allocate transmit descriptor ring, only need ntxd descriptors but it must be aligned */
	if (ntxd) {
		if (!_dma_alloc(di, DMA_TX))
			goto fail;
	}

	/* allocate receive descriptor ring, only need nrxd descriptors but it must be aligned */
	if (nrxd) {
		if (!_dma_alloc(di, DMA_RX))
			goto fail;
	}

	if ((di->ddoffsetlow != 0) && !di->addrext) {
		if (PHYSADDRLO(di->txdpa) > SI_PCI_DMA_SZ) {
			DMA_ERROR(("%s: dma_attach: txdpa 0x%x: addrext not supported\n", di->name, (u32) PHYSADDRLO(di->txdpa)));
			goto fail;
		}
		if (PHYSADDRLO(di->rxdpa) > SI_PCI_DMA_SZ) {
			DMA_ERROR(("%s: dma_attach: rxdpa 0x%x: addrext not supported\n", di->name, (u32) PHYSADDRLO(di->rxdpa)));
			goto fail;
		}
	}

	DMA_TRACE(("ddoffsetlow 0x%x ddoffsethigh 0x%x dataoffsetlow 0x%x dataoffsethigh " "0x%x addrext %d\n", di->ddoffsetlow, di->ddoffsethigh, di->dataoffsetlow, di->dataoffsethigh, di->addrext));

	/* allocate DMA mapping vectors */
	if (DMASGLIST_ENAB) {
		if (ntxd) {
			size = ntxd * sizeof(hnddma_seg_map_t);
			di->txp_dmah = kzalloc(size, GFP_ATOMIC);
			if (di->txp_dmah == NULL)
				goto fail;
		}

		if (nrxd) {
			size = nrxd * sizeof(hnddma_seg_map_t);
			di->rxp_dmah = kzalloc(size, GFP_ATOMIC);
			if (di->rxp_dmah == NULL)
				goto fail;
		}
	}

	return (struct hnddma_pub *) di;

 fail:
	_dma_detach(di);
	return NULL;
}

/* Check for odd number of 1's */
static inline u32 parity32(u32 data)
{
	data ^= data >> 16;
	data ^= data >> 8;
	data ^= data >> 4;
	data ^= data >> 2;
	data ^= data >> 1;

	return data & 1;
}

#define DMA64_DD_PARITY(dd)  parity32((dd)->addrlow ^ (dd)->addrhigh ^ (dd)->ctrl1 ^ (dd)->ctrl2)

static inline void
dma64_dd_upd(dma_info_t *di, dma64dd_t *ddring, dmaaddr_t pa, uint outidx,
	     u32 *flags, u32 bufcount)
{
	u32 ctrl2 = bufcount & D64_CTRL2_BC_MASK;

	/* PCI bus with big(>1G) physical address, use address extension */
#if defined(__mips__) && defined(IL_BIGENDIAN)
	if ((di->dataoffsetlow == SI_SDRAM_SWAPPED)
	    || !(PHYSADDRLO(pa) & PCI32ADDR_HIGH)) {
#else
	if ((di->dataoffsetlow == 0) || !(PHYSADDRLO(pa) & PCI32ADDR_HIGH)) {
#endif				/* defined(__mips__) && defined(IL_BIGENDIAN) */

		W_SM(&ddring[outidx].addrlow,
		     BUS_SWAP32(PHYSADDRLO(pa) + di->dataoffsetlow));
		W_SM(&ddring[outidx].addrhigh,
		     BUS_SWAP32(PHYSADDRHI(pa) + di->dataoffsethigh));
		W_SM(&ddring[outidx].ctrl1, BUS_SWAP32(*flags));
		W_SM(&ddring[outidx].ctrl2, BUS_SWAP32(ctrl2));
	} else {
		/* address extension for 32-bit PCI */
		u32 ae;

		ae = (PHYSADDRLO(pa) & PCI32ADDR_HIGH) >> PCI32ADDR_HIGH_SHIFT;
		PHYSADDRLO(pa) &= ~PCI32ADDR_HIGH;

		ctrl2 |= (ae << D64_CTRL2_AE_SHIFT) & D64_CTRL2_AE;
		W_SM(&ddring[outidx].addrlow,
		     BUS_SWAP32(PHYSADDRLO(pa) + di->dataoffsetlow));
		W_SM(&ddring[outidx].addrhigh,
		     BUS_SWAP32(0 + di->dataoffsethigh));
		W_SM(&ddring[outidx].ctrl1, BUS_SWAP32(*flags));
		W_SM(&ddring[outidx].ctrl2, BUS_SWAP32(ctrl2));
	}
	if (di->hnddma.dmactrlflags & DMA_CTRL_PEN) {
		if (DMA64_DD_PARITY(&ddring[outidx])) {
			W_SM(&ddring[outidx].ctrl2,
			     BUS_SWAP32(ctrl2 | D64_CTRL2_PARITY));
		}
	}
}

static bool _dma_alloc(dma_info_t *di, uint direction)
{
	return dma64_alloc(di, direction);
}

void *dma_alloc_consistent(struct pci_dev *pdev, uint size, u16 align_bits,
			       uint *alloced, unsigned long *pap)
{
	if (align_bits) {
		u16 align = (1 << align_bits);
		if (!IS_ALIGNED(PAGE_SIZE, align))
			size += align;
		*alloced = size;
	}
	return pci_alloc_consistent(pdev, size, (dma_addr_t *) pap);
}

/* !! may be called with core in reset */
static void _dma_detach(dma_info_t *di)
{

	DMA_TRACE(("%s: dma_detach\n", di->name));

	/* free dma descriptor rings */
	if (di->txd64)
		pci_free_consistent(di->pbus, di->txdalloc,
				    ((s8 *)di->txd64 - di->txdalign),
				    (di->txdpaorig));
	if (di->rxd64)
		pci_free_consistent(di->pbus, di->rxdalloc,
				    ((s8 *)di->rxd64 - di->rxdalign),
				    (di->rxdpaorig));

	/* free packet pointer vectors */
	kfree(di->txp);
	kfree(di->rxp);

	/* free tx packet DMA handles */
	kfree(di->txp_dmah);

	/* free rx packet DMA handles */
	kfree(di->rxp_dmah);

	/* free our private info structure */
	kfree(di);

}

static bool _dma_descriptor_align(dma_info_t *di)
{
	u32 addrl;

	/* Check to see if the descriptors need to be aligned on 4K/8K or not */
	if (di->d64txregs != NULL) {
		W_REG(&di->d64txregs->addrlow, 0xff0);
		addrl = R_REG(&di->d64txregs->addrlow);
		if (addrl != 0)
			return false;
	} else if (di->d64rxregs != NULL) {
		W_REG(&di->d64rxregs->addrlow, 0xff0);
		addrl = R_REG(&di->d64rxregs->addrlow);
		if (addrl != 0)
			return false;
	}
	return true;
}

/* return true if this dma engine supports DmaExtendedAddrChanges, otherwise false */
static bool _dma_isaddrext(dma_info_t *di)
{
	/* DMA64 supports full 32- or 64-bit operation. AE is always valid */

	/* not all tx or rx channel are available */
	if (di->d64txregs != NULL) {
		if (!_dma64_addrext(di->d64txregs)) {
			DMA_ERROR(("%s: _dma_isaddrext: DMA64 tx doesn't have "
				   "AE set\n", di->name));
		}
		return true;
	} else if (di->d64rxregs != NULL) {
		if (!_dma64_addrext(di->d64rxregs)) {
			DMA_ERROR(("%s: _dma_isaddrext: DMA64 rx doesn't have "
				   "AE set\n", di->name));
		}
		return true;
	}
	return false;
}

/* initialize descriptor table base address */
static void _dma_ddtable_init(dma_info_t *di, uint direction, dmaaddr_t pa)
{
	if (!di->aligndesc_4k) {
		if (direction == DMA_TX)
			di->xmtptrbase = PHYSADDRLO(pa);
		else
			di->rcvptrbase = PHYSADDRLO(pa);
	}

	if ((di->ddoffsetlow == 0)
	    || !(PHYSADDRLO(pa) & PCI32ADDR_HIGH)) {
		if (direction == DMA_TX) {
			W_REG(&di->d64txregs->addrlow,
			      (PHYSADDRLO(pa) + di->ddoffsetlow));
			W_REG(&di->d64txregs->addrhigh,
			      (PHYSADDRHI(pa) + di->ddoffsethigh));
		} else {
			W_REG(&di->d64rxregs->addrlow,
			      (PHYSADDRLO(pa) + di->ddoffsetlow));
			W_REG(&di->d64rxregs->addrhigh,
				(PHYSADDRHI(pa) + di->ddoffsethigh));
		}
	} else {
		/* DMA64 32bits address extension */
		u32 ae;

		/* shift the high bit(s) from pa to ae */
		ae = (PHYSADDRLO(pa) & PCI32ADDR_HIGH) >>
		    PCI32ADDR_HIGH_SHIFT;
		PHYSADDRLO(pa) &= ~PCI32ADDR_HIGH;

		if (direction == DMA_TX) {
			W_REG(&di->d64txregs->addrlow,
			      (PHYSADDRLO(pa) + di->ddoffsetlow));
			W_REG(&di->d64txregs->addrhigh,
			      di->ddoffsethigh);
			SET_REG(&di->d64txregs->control,
				D64_XC_AE, (ae << D64_XC_AE_SHIFT));
		} else {
			W_REG(&di->d64rxregs->addrlow,
			      (PHYSADDRLO(pa) + di->ddoffsetlow));
			W_REG(&di->d64rxregs->addrhigh,
			      di->ddoffsethigh);
			SET_REG(&di->d64rxregs->control,
				D64_RC_AE, (ae << D64_RC_AE_SHIFT));
		}
	}
}

static void _dma_fifoloopbackenable(dma_info_t *di)
{
	DMA_TRACE(("%s: dma_fifoloopbackenable\n", di->name));

	OR_REG(&di->d64txregs->control, D64_XC_LE);
}

static void _dma_rxinit(dma_info_t *di)
{
	DMA_TRACE(("%s: dma_rxinit\n", di->name));

	if (di->nrxd == 0)
		return;

	di->rxin = di->rxout = 0;

	/* clear rx descriptor ring */
	memset((void *)di->rxd64, '\0',
		(di->nrxd * sizeof(dma64dd_t)));

	/* DMA engine with out alignment requirement requires table to be inited
	 * before enabling the engine
	 */
	if (!di->aligndesc_4k)
		_dma_ddtable_init(di, DMA_RX, di->rxdpa);

	_dma_rxenable(di);

	if (di->aligndesc_4k)
		_dma_ddtable_init(di, DMA_RX, di->rxdpa);
}

static void _dma_rxenable(dma_info_t *di)
{
	uint dmactrlflags = di->hnddma.dmactrlflags;
	u32 control;

	DMA_TRACE(("%s: dma_rxenable\n", di->name));

	control =
	    (R_REG(&di->d64rxregs->control) & D64_RC_AE) |
	    D64_RC_RE;

	if ((dmactrlflags & DMA_CTRL_PEN) == 0)
		control |= D64_RC_PD;

	if (dmactrlflags & DMA_CTRL_ROC)
		control |= D64_RC_OC;

	W_REG(&di->d64rxregs->control,
		((di->rxoffset << D64_RC_RO_SHIFT) | control));
}

static void
_dma_rx_param_get(dma_info_t *di, u16 *rxoffset, u16 *rxbufsize)
{
	/* the normal values fit into 16 bits */
	*rxoffset = (u16) di->rxoffset;
	*rxbufsize = (u16) di->rxbufsize;
}

/* !! rx entry routine
 * returns a pointer to the next frame received, or NULL if there are no more
 *   if DMA_CTRL_RXMULTI is defined, DMA scattering(multiple buffers) is supported
 *      with pkts chain
 *   otherwise, it's treated as giant pkt and will be tossed.
 *   The DMA scattering starts with normal DMA header, followed by first buffer data.
 *   After it reaches the max size of buffer, the data continues in next DMA descriptor
 *   buffer WITHOUT DMA header
 */
static void *_dma_rx(dma_info_t *di)
{
	struct sk_buff *p, *head, *tail;
	uint len;
	uint pkt_len;
	int resid = 0;

 next_frame:
	head = _dma_getnextrxp(di, false);
	if (head == NULL)
		return NULL;

	len = le16_to_cpu(*(u16 *) (head->data));
	DMA_TRACE(("%s: dma_rx len %d\n", di->name, len));
	dma_spin_for_len(len, head);

	/* set actual length */
	pkt_len = min((di->rxoffset + len), di->rxbufsize);
	__skb_trim(head, pkt_len);
	resid = len - (di->rxbufsize - di->rxoffset);

	/* check for single or multi-buffer rx */
	if (resid > 0) {
		tail = head;
		while ((resid > 0) && (p = _dma_getnextrxp(di, false))) {
			tail->next = p;
			pkt_len = min(resid, (int)di->rxbufsize);
			__skb_trim(p, pkt_len);

			tail = p;
			resid -= di->rxbufsize;
		}

#ifdef BCMDBG
		if (resid > 0) {
			uint cur;
			cur =
			    B2I(((R_REG(&di->d64rxregs->status0) &
				  D64_RS0_CD_MASK) -
				 di->rcvptrbase) & D64_RS0_CD_MASK,
				dma64dd_t);
			DMA_ERROR(("_dma_rx, rxin %d rxout %d, hw_curr %d\n",
				   di->rxin, di->rxout, cur));
		}
#endif				/* BCMDBG */

		if ((di->hnddma.dmactrlflags & DMA_CTRL_RXMULTI) == 0) {
			DMA_ERROR(("%s: dma_rx: bad frame length (%d)\n",
				   di->name, len));
			bcm_pkt_buf_free_skb(head);
			di->hnddma.rxgiants++;
			goto next_frame;
		}
	}

	return head;
}

/* post receive buffers
 *  return false is refill failed completely and ring is empty
 *  this will stall the rx dma and user might want to call rxfill again asap
 *  This unlikely happens on memory-rich NIC, but often on memory-constrained dongle
 */
static bool _dma_rxfill(dma_info_t *di)
{
	struct sk_buff *p;
	u16 rxin, rxout;
	u32 flags = 0;
	uint n;
	uint i;
	dmaaddr_t pa;
	uint extra_offset = 0;
	bool ring_empty;

	ring_empty = false;

	/*
	 * Determine how many receive buffers we're lacking
	 * from the full complement, allocate, initialize,
	 * and post them, then update the chip rx lastdscr.
	 */

	rxin = di->rxin;
	rxout = di->rxout;

	n = di->nrxpost - NRXDACTIVE(rxin, rxout);

	DMA_TRACE(("%s: dma_rxfill: post %d\n", di->name, n));

	if (di->rxbufsize > BCMEXTRAHDROOM)
		extra_offset = di->rxextrahdrroom;

	for (i = 0; i < n; i++) {
		/* the di->rxbufsize doesn't include the extra headroom, we need to add it to the
		   size to be allocated
		 */

		p = bcm_pkt_buf_get_skb(di->rxbufsize + extra_offset);

		if (p == NULL) {
			DMA_ERROR(("%s: dma_rxfill: out of rxbufs\n",
				   di->name));
			if (i == 0 && dma64_rxidle(di)) {
				DMA_ERROR(("%s: rxfill64: ring is empty !\n",
					   di->name));
				ring_empty = true;
			}
			di->hnddma.rxnobuf++;
			break;
		}
		/* reserve an extra headroom, if applicable */
		if (extra_offset)
			skb_pull(p, extra_offset);

		/* Do a cached write instead of uncached write since DMA_MAP
		 * will flush the cache.
		 */
		*(u32 *) (p->data) = 0;

		if (DMASGLIST_ENAB)
			memset(&di->rxp_dmah[rxout], 0,
				sizeof(hnddma_seg_map_t));

		pa = pci_map_single(di->pbus, p->data,
			di->rxbufsize, PCI_DMA_FROMDEVICE);

		/* save the free packet pointer */
		di->rxp[rxout] = p;

		/* reset flags for each descriptor */
		flags = 0;
		if (rxout == (di->nrxd - 1))
			flags = D64_CTRL1_EOT;

		dma64_dd_upd(di, di->rxd64, pa, rxout, &flags,
			     di->rxbufsize);
		rxout = NEXTRXD(rxout);
	}

	di->rxout = rxout;

	/* update the chip lastdscr pointer */
	W_REG(&di->d64rxregs->ptr,
	      di->rcvptrbase + I2B(rxout, dma64dd_t));

	return ring_empty;
}

/* like getnexttxp but no reclaim */
static void *_dma_peeknexttxp(dma_info_t *di)
{
	uint end, i;

	if (di->ntxd == 0)
		return NULL;

	end =
	    B2I(((R_REG(&di->d64txregs->status0) &
		  D64_XS0_CD_MASK) - di->xmtptrbase) & D64_XS0_CD_MASK,
		  dma64dd_t);

	for (i = di->txin; i != end; i = NEXTTXD(i))
		if (di->txp[i])
			return di->txp[i];

	return NULL;
}

/* like getnextrxp but not take off the ring */
static void *_dma_peeknextrxp(dma_info_t *di)
{
	uint end, i;

	if (di->nrxd == 0)
		return NULL;

	end =
	    B2I(((R_REG(&di->d64rxregs->status0) &
		  D64_RS0_CD_MASK) - di->rcvptrbase) & D64_RS0_CD_MASK,
		  dma64dd_t);

	for (i = di->rxin; i != end; i = NEXTRXD(i))
		if (di->rxp[i])
			return di->rxp[i];

	return NULL;
}

static void _dma_rxreclaim(dma_info_t *di)
{
	void *p;

	DMA_TRACE(("%s: dma_rxreclaim\n", di->name));

	while ((p = _dma_getnextrxp(di, true)))
		bcm_pkt_buf_free_skb(p);
}

static void *_dma_getnextrxp(dma_info_t *di, bool forceall)
{
	if (di->nrxd == 0)
		return NULL;

	return dma64_getnextrxp(di, forceall);
}

static void _dma_txblock(dma_info_t *di)
{
	di->hnddma.txavail = 0;
}

static void _dma_txunblock(dma_info_t *di)
{
	di->hnddma.txavail = di->ntxd - NTXDACTIVE(di->txin, di->txout) - 1;
}

static uint _dma_txactive(dma_info_t *di)
{
	return NTXDACTIVE(di->txin, di->txout);
}

static uint _dma_txpending(dma_info_t *di)
{
	uint curr;

	curr =
	    B2I(((R_REG(&di->d64txregs->status0) &
		  D64_XS0_CD_MASK) - di->xmtptrbase) & D64_XS0_CD_MASK,
		  dma64dd_t);

	return NTXDACTIVE(curr, di->txout);
}

static uint _dma_txcommitted(dma_info_t *di)
{
	uint ptr;
	uint txin = di->txin;

	if (txin == di->txout)
		return 0;

	ptr = B2I(R_REG(&di->d64txregs->ptr), dma64dd_t);

	return NTXDACTIVE(di->txin, ptr);
}

static uint _dma_rxactive(dma_info_t *di)
{
	return NRXDACTIVE(di->rxin, di->rxout);
}

static void _dma_counterreset(dma_info_t *di)
{
	/* reset all software counter */
	di->hnddma.rxgiants = 0;
	di->hnddma.rxnobuf = 0;
	di->hnddma.txnobuf = 0;
}

static uint _dma_ctrlflags(dma_info_t *di, uint mask, uint flags)
{
	uint dmactrlflags = di->hnddma.dmactrlflags;

	if (di == NULL) {
		DMA_ERROR(("%s: _dma_ctrlflags: NULL dma handle\n", di->name));
		return 0;
	}

	dmactrlflags &= ~mask;
	dmactrlflags |= flags;

	/* If trying to enable parity, check if parity is actually supported */
	if (dmactrlflags & DMA_CTRL_PEN) {
		u32 control;

		control = R_REG(&di->d64txregs->control);
		W_REG(&di->d64txregs->control,
		      control | D64_XC_PD);
		if (R_REG(&di->d64txregs->control) & D64_XC_PD) {
			/* We *can* disable it so it is supported,
			 * restore control register
			 */
			W_REG(&di->d64txregs->control,
			control);
		} else {
			/* Not supported, don't allow it to be enabled */
			dmactrlflags &= ~DMA_CTRL_PEN;
		}
	}

	di->hnddma.dmactrlflags = dmactrlflags;

	return dmactrlflags;
}

/* get the address of the var in order to change later */
static unsigned long _dma_getvar(dma_info_t *di, const char *name)
{
	if (!strcmp(name, "&txavail"))
		return (unsigned long)&(di->hnddma.txavail);
	return 0;
}

static
u8 dma_align_sizetobits(uint size)
{
	u8 bitpos = 0;
	while (size >>= 1) {
		bitpos++;
	}
	return bitpos;
}

/* This function ensures that the DMA descriptor ring will not get allocated
 * across Page boundary. If the allocation is done across the page boundary
 * at the first time, then it is freed and the allocation is done at
 * descriptor ring size aligned location. This will ensure that the ring will
 * not cross page boundary
 */
static void *dma_ringalloc(dma_info_t *di, u32 boundary, uint size,
			   u16 *alignbits, uint *alloced,
			   dmaaddr_t *descpa)
{
	void *va;
	u32 desc_strtaddr;
	u32 alignbytes = 1 << *alignbits;

	va = dma_alloc_consistent(di->pbus, size, *alignbits, alloced, descpa);

	if (NULL == va)
		return NULL;

	desc_strtaddr = (u32) roundup((unsigned long)va, alignbytes);
	if (((desc_strtaddr + size - 1) & boundary) != (desc_strtaddr
							& boundary)) {
		*alignbits = dma_align_sizetobits(size);
		pci_free_consistent(di->pbus, size, va, *descpa);
		va = dma_alloc_consistent(di->pbus, size, *alignbits,
			alloced, descpa);
	}
	return va;
}

/* 64-bit DMA functions */

static void dma64_txinit(dma_info_t *di)
{
	u32 control = D64_XC_XE;

	DMA_TRACE(("%s: dma_txinit\n", di->name));

	if (di->ntxd == 0)
		return;

	di->txin = di->txout = 0;
	di->hnddma.txavail = di->ntxd - 1;

	/* clear tx descriptor ring */
	memset((void *)di->txd64, '\0', (di->ntxd * sizeof(dma64dd_t)));

	/* DMA engine with out alignment requirement requires table to be inited
	 * before enabling the engine
	 */
	if (!di->aligndesc_4k)
		_dma_ddtable_init(di, DMA_TX, di->txdpa);

	if ((di->hnddma.dmactrlflags & DMA_CTRL_PEN) == 0)
		control |= D64_XC_PD;
	OR_REG(&di->d64txregs->control, control);

	/* DMA engine with alignment requirement requires table to be inited
	 * before enabling the engine
	 */
	if (di->aligndesc_4k)
		_dma_ddtable_init(di, DMA_TX, di->txdpa);
}

static bool dma64_txenabled(dma_info_t *di)
{
	u32 xc;

	/* If the chip is dead, it is not enabled :-) */
	xc = R_REG(&di->d64txregs->control);
	return (xc != 0xffffffff) && (xc & D64_XC_XE);
}

static void dma64_txsuspend(dma_info_t *di)
{
	DMA_TRACE(("%s: dma_txsuspend\n", di->name));

	if (di->ntxd == 0)
		return;

	OR_REG(&di->d64txregs->control, D64_XC_SE);
}

static void dma64_txresume(dma_info_t *di)
{
	DMA_TRACE(("%s: dma_txresume\n", di->name));

	if (di->ntxd == 0)
		return;

	AND_REG(&di->d64txregs->control, ~D64_XC_SE);
}

static bool dma64_txsuspended(dma_info_t *di)
{
	return (di->ntxd == 0) ||
	    ((R_REG(&di->d64txregs->control) & D64_XC_SE) ==
	     D64_XC_SE);
}

static void dma64_txreclaim(dma_info_t *di, txd_range_t range)
{
	void *p;

	DMA_TRACE(("%s: dma_txreclaim %s\n", di->name,
		   (range == HNDDMA_RANGE_ALL) ? "all" :
		   ((range ==
		     HNDDMA_RANGE_TRANSMITTED) ? "transmitted" :
		    "transferred")));

	if (di->txin == di->txout)
		return;

	while ((p = dma64_getnexttxp(di, range))) {
		/* For unframed data, we don't have any packets to free */
		if (!(di->hnddma.dmactrlflags & DMA_CTRL_UNFRAMED))
			bcm_pkt_buf_free_skb(p);
	}
}

static bool dma64_txstopped(dma_info_t *di)
{
	return ((R_REG(&di->d64txregs->status0) & D64_XS0_XS_MASK) ==
		D64_XS0_XS_STOPPED);
}

static bool dma64_rxstopped(dma_info_t *di)
{
	return ((R_REG(&di->d64rxregs->status0) & D64_RS0_RS_MASK) ==
		D64_RS0_RS_STOPPED);
}

static bool dma64_alloc(dma_info_t *di, uint direction)
{
	u16 size;
	uint ddlen;
	void *va;
	uint alloced = 0;
	u16 align;
	u16 align_bits;

	ddlen = sizeof(dma64dd_t);

	size = (direction == DMA_TX) ? (di->ntxd * ddlen) : (di->nrxd * ddlen);
	align_bits = di->dmadesc_align;
	align = (1 << align_bits);

	if (direction == DMA_TX) {
		va = dma_ringalloc(di, D64RINGALIGN, size, &align_bits,
			&alloced, &di->txdpaorig);
		if (va == NULL) {
			DMA_ERROR(("%s: dma64_alloc: DMA_ALLOC_CONSISTENT(ntxd) failed\n", di->name));
			return false;
		}
		align = (1 << align_bits);
		di->txd64 = (dma64dd_t *) roundup((unsigned long)va, align);
		di->txdalign = (uint) ((s8 *)di->txd64 - (s8 *) va);
		PHYSADDRLOSET(di->txdpa,
			      PHYSADDRLO(di->txdpaorig) + di->txdalign);
		PHYSADDRHISET(di->txdpa, PHYSADDRHI(di->txdpaorig));
		di->txdalloc = alloced;
	} else {
		va = dma_ringalloc(di, D64RINGALIGN, size, &align_bits,
			&alloced, &di->rxdpaorig);
		if (va == NULL) {
			DMA_ERROR(("%s: dma64_alloc: DMA_ALLOC_CONSISTENT(nrxd) failed\n", di->name));
			return false;
		}
		align = (1 << align_bits);
		di->rxd64 = (dma64dd_t *) roundup((unsigned long)va, align);
		di->rxdalign = (uint) ((s8 *)di->rxd64 - (s8 *) va);
		PHYSADDRLOSET(di->rxdpa,
			      PHYSADDRLO(di->rxdpaorig) + di->rxdalign);
		PHYSADDRHISET(di->rxdpa, PHYSADDRHI(di->rxdpaorig));
		di->rxdalloc = alloced;
	}

	return true;
}

static bool dma64_txreset(dma_info_t *di)
{
	u32 status;

	if (di->ntxd == 0)
		return true;

	/* suspend tx DMA first */
	W_REG(&di->d64txregs->control, D64_XC_SE);
	SPINWAIT(((status =
		   (R_REG(&di->d64txregs->status0) & D64_XS0_XS_MASK))
		  != D64_XS0_XS_DISABLED) && (status != D64_XS0_XS_IDLE)
		 && (status != D64_XS0_XS_STOPPED), 10000);

	W_REG(&di->d64txregs->control, 0);
	SPINWAIT(((status =
		   (R_REG(&di->d64txregs->status0) & D64_XS0_XS_MASK))
		  != D64_XS0_XS_DISABLED), 10000);

	/* wait for the last transaction to complete */
	udelay(300);

	return status == D64_XS0_XS_DISABLED;
}

static bool dma64_rxidle(dma_info_t *di)
{
	DMA_TRACE(("%s: dma_rxidle\n", di->name));

	if (di->nrxd == 0)
		return true;

	return ((R_REG(&di->d64rxregs->status0) & D64_RS0_CD_MASK) ==
		(R_REG(&di->d64rxregs->ptr) & D64_RS0_CD_MASK));
}

static bool dma64_rxreset(dma_info_t *di)
{
	u32 status;

	if (di->nrxd == 0)
		return true;

	W_REG(&di->d64rxregs->control, 0);
	SPINWAIT(((status =
		   (R_REG(&di->d64rxregs->status0) & D64_RS0_RS_MASK))
		  != D64_RS0_RS_DISABLED), 10000);

	return status == D64_RS0_RS_DISABLED;
}

static bool dma64_rxenabled(dma_info_t *di)
{
	u32 rc;

	rc = R_REG(&di->d64rxregs->control);
	return (rc != 0xffffffff) && (rc & D64_RC_RE);
}

static bool dma64_txsuspendedidle(dma_info_t *di)
{

	if (di->ntxd == 0)
		return true;

	if (!(R_REG(&di->d64txregs->control) & D64_XC_SE))
		return 0;

	if ((R_REG(&di->d64txregs->status0) & D64_XS0_XS_MASK) ==
	    D64_XS0_XS_IDLE)
		return 1;

	return 0;
}

/* Useful when sending unframed data.  This allows us to get a progress report from the DMA.
 * We return a pointer to the beginning of the DATA buffer of the current descriptor.
 * If DMA is idle, we return NULL.
 */
static void *dma64_getpos(dma_info_t *di, bool direction)
{
	void *va;
	bool idle;
	u32 cd_offset;

	if (direction == DMA_TX) {
		cd_offset =
		    R_REG(&di->d64txregs->status0) & D64_XS0_CD_MASK;
		idle = !NTXDACTIVE(di->txin, di->txout);
		va = di->txp[B2I(cd_offset, dma64dd_t)];
	} else {
		cd_offset =
		    R_REG(&di->d64rxregs->status0) & D64_XS0_CD_MASK;
		idle = !NRXDACTIVE(di->rxin, di->rxout);
		va = di->rxp[B2I(cd_offset, dma64dd_t)];
	}

	/* If DMA is IDLE, return NULL */
	if (idle) {
		DMA_TRACE(("%s: DMA idle, return NULL\n", __func__));
		va = NULL;
	}

	return va;
}

/* TX of unframed data
 *
 * Adds a DMA ring descriptor for the data pointed to by "buf".
 * This is for DMA of a buffer of data and is unlike other hnddma TX functions
 * that take a pointer to a "packet"
 * Each call to this is results in a single descriptor being added for "len" bytes of
 * data starting at "buf", it doesn't handle chained buffers.
 */
static int dma64_txunframed(dma_info_t *di, void *buf, uint len, bool commit)
{
	u16 txout;
	u32 flags = 0;
	dmaaddr_t pa;		/* phys addr */

	txout = di->txout;

	/* return nonzero if out of tx descriptors */
	if (NEXTTXD(txout) == di->txin)
		goto outoftxd;

	if (len == 0)
		return 0;

	pa = pci_map_single(di->pbus, buf, len, PCI_DMA_TODEVICE);

	flags = (D64_CTRL1_SOF | D64_CTRL1_IOC | D64_CTRL1_EOF);

	if (txout == (di->ntxd - 1))
		flags |= D64_CTRL1_EOT;

	dma64_dd_upd(di, di->txd64, pa, txout, &flags, len);

	/* save the buffer pointer - used by dma_getpos */
	di->txp[txout] = buf;

	txout = NEXTTXD(txout);
	/* bump the tx descriptor index */
	di->txout = txout;

	/* kick the chip */
	if (commit) {
		W_REG(&di->d64txregs->ptr,
		      di->xmtptrbase + I2B(txout, dma64dd_t));
	}

	/* tx flow control */
	di->hnddma.txavail = di->ntxd - NTXDACTIVE(di->txin, di->txout) - 1;

	return 0;

 outoftxd:
	DMA_ERROR(("%s: %s: out of txds !!!\n", di->name, __func__));
	di->hnddma.txavail = 0;
	di->hnddma.txnobuf++;
	return -1;
}

/* !! tx entry routine
 * WARNING: call must check the return value for error.
 *   the error(toss frames) could be fatal and cause many subsequent hard to debug problems
 */
static int dma64_txfast(dma_info_t *di, struct sk_buff *p0,
				    bool commit)
{
	struct sk_buff *p, *next;
	unsigned char *data;
	uint len;
	u16 txout;
	u32 flags = 0;
	dmaaddr_t pa;

	DMA_TRACE(("%s: dma_txfast\n", di->name));

	txout = di->txout;

	/*
	 * Walk the chain of packet buffers
	 * allocating and initializing transmit descriptor entries.
	 */
	for (p = p0; p; p = next) {
		uint nsegs, j;
		hnddma_seg_map_t *map;

		data = p->data;
		len = p->len;
		next = p->next;

		/* return nonzero if out of tx descriptors */
		if (NEXTTXD(txout) == di->txin)
			goto outoftxd;

		if (len == 0)
			continue;

		/* get physical address of buffer start */
		if (DMASGLIST_ENAB)
			memset(&di->txp_dmah[txout], 0,
				sizeof(hnddma_seg_map_t));

		pa = pci_map_single(di->pbus, data, len, PCI_DMA_TODEVICE);

		if (DMASGLIST_ENAB) {
			map = &di->txp_dmah[txout];

			/* See if all the segments can be accounted for */
			if (map->nsegs >
			    (uint) (di->ntxd - NTXDACTIVE(di->txin, di->txout) -
				    1))
				goto outoftxd;

			nsegs = map->nsegs;
		} else
			nsegs = 1;

		for (j = 1; j <= nsegs; j++) {
			flags = 0;
			if (p == p0 && j == 1)
				flags |= D64_CTRL1_SOF;

			/* With a DMA segment list, Descriptor table is filled
			 * using the segment list instead of looping over
			 * buffers in multi-chain DMA. Therefore, EOF for SGLIST is when
			 * end of segment list is reached.
			 */
			if ((!DMASGLIST_ENAB && next == NULL) ||
			    (DMASGLIST_ENAB && j == nsegs))
				flags |= (D64_CTRL1_IOC | D64_CTRL1_EOF);
			if (txout == (di->ntxd - 1))
				flags |= D64_CTRL1_EOT;

			if (DMASGLIST_ENAB) {
				len = map->segs[j - 1].length;
				pa = map->segs[j - 1].addr;
			}
			dma64_dd_upd(di, di->txd64, pa, txout, &flags, len);

			txout = NEXTTXD(txout);
		}

		/* See above. No need to loop over individual buffers */
		if (DMASGLIST_ENAB)
			break;
	}

	/* if last txd eof not set, fix it */
	if (!(flags & D64_CTRL1_EOF))
		W_SM(&di->txd64[PREVTXD(txout)].ctrl1,
		     BUS_SWAP32(flags | D64_CTRL1_IOC | D64_CTRL1_EOF));

	/* save the packet */
	di->txp[PREVTXD(txout)] = p0;

	/* bump the tx descriptor index */
	di->txout = txout;

	/* kick the chip */
	if (commit)
		W_REG(&di->d64txregs->ptr,
		      di->xmtptrbase + I2B(txout, dma64dd_t));

	/* tx flow control */
	di->hnddma.txavail = di->ntxd - NTXDACTIVE(di->txin, di->txout) - 1;

	return 0;

 outoftxd:
	DMA_ERROR(("%s: dma_txfast: out of txds !!!\n", di->name));
	bcm_pkt_buf_free_skb(p0);
	di->hnddma.txavail = 0;
	di->hnddma.txnobuf++;
	return -1;
}

/*
 * Reclaim next completed txd (txds if using chained buffers) in the range
 * specified and return associated packet.
 * If range is HNDDMA_RANGE_TRANSMITTED, reclaim descriptors that have be
 * transmitted as noted by the hardware "CurrDescr" pointer.
 * If range is HNDDMA_RANGE_TRANSFERED, reclaim descriptors that have be
 * transferred by the DMA as noted by the hardware "ActiveDescr" pointer.
 * If range is HNDDMA_RANGE_ALL, reclaim all txd(s) posted to the ring and
 * return associated packet regardless of the value of hardware pointers.
 */
static void *dma64_getnexttxp(dma_info_t *di, txd_range_t range)
{
	u16 start, end, i;
	u16 active_desc;
	void *txp;

	DMA_TRACE(("%s: dma_getnexttxp %s\n", di->name,
		   (range == HNDDMA_RANGE_ALL) ? "all" :
		   ((range ==
		     HNDDMA_RANGE_TRANSMITTED) ? "transmitted" :
		    "transferred")));

	if (di->ntxd == 0)
		return NULL;

	txp = NULL;

	start = di->txin;
	if (range == HNDDMA_RANGE_ALL)
		end = di->txout;
	else {
		dma64regs_t *dregs = di->d64txregs;

		end =
		    (u16) (B2I
			      (((R_REG(&dregs->status0) &
				 D64_XS0_CD_MASK) -
				di->xmtptrbase) & D64_XS0_CD_MASK, dma64dd_t));

		if (range == HNDDMA_RANGE_TRANSFERED) {
			active_desc =
			    (u16) (R_REG(&dregs->status1) &
				      D64_XS1_AD_MASK);
			active_desc =
			    (active_desc - di->xmtptrbase) & D64_XS0_CD_MASK;
			active_desc = B2I(active_desc, dma64dd_t);
			if (end != active_desc)
				end = PREVTXD(active_desc);
		}
	}

	if ((start == 0) && (end > di->txout))
		goto bogus;

	for (i = start; i != end && !txp; i = NEXTTXD(i)) {
		dmaaddr_t pa;
		hnddma_seg_map_t *map = NULL;
		uint size, j, nsegs;

		PHYSADDRLOSET(pa,
			      (BUS_SWAP32(R_SM(&di->txd64[i].addrlow)) -
			       di->dataoffsetlow));
		PHYSADDRHISET(pa,
			      (BUS_SWAP32(R_SM(&di->txd64[i].addrhigh)) -
			       di->dataoffsethigh));

		if (DMASGLIST_ENAB) {
			map = &di->txp_dmah[i];
			size = map->origsize;
			nsegs = map->nsegs;
		} else {
			size =
			    (BUS_SWAP32(R_SM(&di->txd64[i].ctrl2)) &
			     D64_CTRL2_BC_MASK);
			nsegs = 1;
		}

		for (j = nsegs; j > 0; j--) {
			W_SM(&di->txd64[i].addrlow, 0xdeadbeef);
			W_SM(&di->txd64[i].addrhigh, 0xdeadbeef);

			txp = di->txp[i];
			di->txp[i] = NULL;
			if (j > 1)
				i = NEXTTXD(i);
		}

		pci_unmap_single(di->pbus, pa, size, PCI_DMA_TODEVICE);
	}

	di->txin = i;

	/* tx flow control */
	di->hnddma.txavail = di->ntxd - NTXDACTIVE(di->txin, di->txout) - 1;

	return txp;

 bogus:
	DMA_NONE(("dma_getnexttxp: bogus curr: start %d end %d txout %d force %d\n", start, end, di->txout, forceall));
	return NULL;
}

static void *dma64_getnextrxp(dma_info_t *di, bool forceall)
{
	uint i, curr;
	void *rxp;
	dmaaddr_t pa;

	i = di->rxin;

	/* return if no packets posted */
	if (i == di->rxout)
		return NULL;

	curr =
	    B2I(((R_REG(&di->d64rxregs->status0) & D64_RS0_CD_MASK) -
		 di->rcvptrbase) & D64_RS0_CD_MASK, dma64dd_t);

	/* ignore curr if forceall */
	if (!forceall && (i == curr))
		return NULL;

	/* get the packet pointer that corresponds to the rx descriptor */
	rxp = di->rxp[i];
	di->rxp[i] = NULL;

	PHYSADDRLOSET(pa,
		      (BUS_SWAP32(R_SM(&di->rxd64[i].addrlow)) -
		       di->dataoffsetlow));
	PHYSADDRHISET(pa,
		      (BUS_SWAP32(R_SM(&di->rxd64[i].addrhigh)) -
		       di->dataoffsethigh));

	/* clear this packet from the descriptor ring */
	pci_unmap_single(di->pbus, pa, di->rxbufsize, PCI_DMA_FROMDEVICE);

	W_SM(&di->rxd64[i].addrlow, 0xdeadbeef);
	W_SM(&di->rxd64[i].addrhigh, 0xdeadbeef);

	di->rxin = NEXTRXD(i);

	return rxp;
}

static bool _dma64_addrext(dma64regs_t *dma64regs)
{
	u32 w;
	OR_REG(&dma64regs->control, D64_XC_AE);
	w = R_REG(&dma64regs->control);
	AND_REG(&dma64regs->control, ~D64_XC_AE);
	return (w & D64_XC_AE) == D64_XC_AE;
}

/*
 * Rotate all active tx dma ring entries "forward" by (ActiveDescriptor - txin).
 */
static void dma64_txrotate(dma_info_t *di)
{
	u16 ad;
	uint nactive;
	uint rot;
	u16 old, new;
	u32 w;
	u16 first, last;

	nactive = _dma_txactive(di);
	ad = (u16) (B2I
		       ((((R_REG(&di->d64txregs->status1) &
			   D64_XS1_AD_MASK)
			  - di->xmtptrbase) & D64_XS1_AD_MASK), dma64dd_t));
	rot = TXD(ad - di->txin);

	/* full-ring case is a lot harder - don't worry about this */
	if (rot >= (di->ntxd - nactive)) {
		DMA_ERROR(("%s: dma_txrotate: ring full - punt\n", di->name));
		return;
	}

	first = di->txin;
	last = PREVTXD(di->txout);

	/* move entries starting at last and moving backwards to first */
	for (old = last; old != PREVTXD(first); old = PREVTXD(old)) {
		new = TXD(old + rot);

		/*
		 * Move the tx dma descriptor.
		 * EOT is set only in the last entry in the ring.
		 */
		w = BUS_SWAP32(R_SM(&di->txd64[old].ctrl1)) & ~D64_CTRL1_EOT;
		if (new == (di->ntxd - 1))
			w |= D64_CTRL1_EOT;
		W_SM(&di->txd64[new].ctrl1, BUS_SWAP32(w));

		w = BUS_SWAP32(R_SM(&di->txd64[old].ctrl2));
		W_SM(&di->txd64[new].ctrl2, BUS_SWAP32(w));

		W_SM(&di->txd64[new].addrlow, R_SM(&di->txd64[old].addrlow));
		W_SM(&di->txd64[new].addrhigh, R_SM(&di->txd64[old].addrhigh));

		/* zap the old tx dma descriptor address field */
		W_SM(&di->txd64[old].addrlow, BUS_SWAP32(0xdeadbeef));
		W_SM(&di->txd64[old].addrhigh, BUS_SWAP32(0xdeadbeef));

		/* move the corresponding txp[] entry */
		di->txp[new] = di->txp[old];

		/* Move the map */
		if (DMASGLIST_ENAB) {
			memcpy(&di->txp_dmah[new], &di->txp_dmah[old],
			       sizeof(hnddma_seg_map_t));
			memset(&di->txp_dmah[old], 0, sizeof(hnddma_seg_map_t));
		}

		di->txp[old] = NULL;
	}

	/* update txin and txout */
	di->txin = ad;
	di->txout = TXD(di->txout + rot);
	di->hnddma.txavail = di->ntxd - NTXDACTIVE(di->txin, di->txout) - 1;

	/* kick the chip */
	W_REG(&di->d64txregs->ptr,
	      di->xmtptrbase + I2B(di->txout, dma64dd_t));
}

uint dma_addrwidth(si_t *sih, void *dmaregs)
{
	/* Perform 64-bit checks only if we want to advertise 64-bit (> 32bit) capability) */
	/* DMA engine is 64-bit capable */
	if ((ai_core_sflags(sih, 0, 0) & SISF_DMA64) == SISF_DMA64) {
		/* backplane are 64-bit capable */
		if (ai_backplane64(sih))
			/* If bus is System Backplane or PCIE then we can access 64-bits */
			if ((sih->bustype == SI_BUS) ||
			    ((sih->bustype == PCI_BUS) &&
			     (sih->buscoretype == PCIE_CORE_ID)))
				return DMADDRWIDTH_64;
	}
	/* DMA hardware not supported by this driver*/
	return DMADDRWIDTH_64;
}

/*
 * Mac80211 initiated actions sometimes require packets in the DMA queue to be
 * modified. The modified portion of the packet is not under control of the DMA
 * engine. This function calls a caller-supplied function for each packet in
 * the caller specified dma chain.
 */
void dma_walk_packets(struct hnddma_pub *dmah, void (*callback_fnc)
		      (void *pkt, void *arg_a), void *arg_a)
{
	dma_info_t *di = (dma_info_t *) dmah;
	uint i =   di->txin;
	uint end = di->txout;
	struct sk_buff *skb;
	struct ieee80211_tx_info *tx_info;

	while (i != end) {
		skb = (struct sk_buff *)di->txp[i];
		if (skb != NULL) {
			tx_info = (struct ieee80211_tx_info *)skb->cb;
			(callback_fnc)(tx_info, arg_a);
		}
		i = NEXTTXD(i);
	}
}
