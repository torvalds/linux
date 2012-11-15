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

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>

#include <brcmu_utils.h>
#include <aiutils.h>
#include "types.h"
#include "main.h"
#include "dma.h"
#include "soc.h"
#include "scb.h"
#include "ampdu.h"
#include "debug.h"

/*
 * dma register field offset calculation
 */
#define DMA64REGOFFS(field)		offsetof(struct dma64regs, field)
#define DMA64TXREGOFFS(di, field)	(di->d64txregbase + DMA64REGOFFS(field))
#define DMA64RXREGOFFS(di, field)	(di->d64rxregbase + DMA64REGOFFS(field))

/*
 * DMA hardware requires each descriptor ring to be 8kB aligned, and fit within
 * a contiguous 8kB physical address.
 */
#define D64RINGALIGN_BITS	13
#define	D64MAXRINGSZ		(1 << D64RINGALIGN_BITS)
#define	D64RINGALIGN		(1 << D64RINGALIGN_BITS)

#define	D64MAXDD	(D64MAXRINGSZ / sizeof(struct dma64desc))

/* transmit channel control */
#define	D64_XC_XE		0x00000001	/* transmit enable */
#define	D64_XC_SE		0x00000002	/* transmit suspend request */
#define	D64_XC_LE		0x00000004	/* loopback enable */
#define	D64_XC_FL		0x00000010	/* flush request */
#define	D64_XC_PD		0x00000800	/* parity check disable */
#define	D64_XC_AE		0x00030000	/* address extension bits */
#define	D64_XC_AE_SHIFT		16

/* transmit descriptor table pointer */
#define	D64_XP_LD_MASK		0x00000fff	/* last valid descriptor */

/* transmit channel status */
#define	D64_XS0_CD_MASK		0x00001fff	/* current descriptor pointer */
#define	D64_XS0_XS_MASK		0xf0000000	/* transmit state */
#define	D64_XS0_XS_SHIFT		28
#define	D64_XS0_XS_DISABLED	0x00000000	/* disabled */
#define	D64_XS0_XS_ACTIVE	0x10000000	/* active */
#define	D64_XS0_XS_IDLE		0x20000000	/* idle wait */
#define	D64_XS0_XS_STOPPED	0x30000000	/* stopped */
#define	D64_XS0_XS_SUSP		0x40000000	/* suspend pending */

#define	D64_XS1_AD_MASK		0x00001fff	/* active descriptor */
#define	D64_XS1_XE_MASK		0xf0000000	/* transmit errors */
#define	D64_XS1_XE_SHIFT		28
#define	D64_XS1_XE_NOERR	0x00000000	/* no error */
#define	D64_XS1_XE_DPE		0x10000000	/* descriptor protocol error */
#define	D64_XS1_XE_DFU		0x20000000	/* data fifo underrun */
#define	D64_XS1_XE_DTE		0x30000000	/* data transfer error */
#define	D64_XS1_XE_DESRE	0x40000000	/* descriptor read error */
#define	D64_XS1_XE_COREE	0x50000000	/* core error */

/* receive channel control */
/* receive enable */
#define	D64_RC_RE		0x00000001
/* receive frame offset */
#define	D64_RC_RO_MASK		0x000000fe
#define	D64_RC_RO_SHIFT		1
/* direct fifo receive (pio) mode */
#define	D64_RC_FM		0x00000100
/* separate rx header descriptor enable */
#define	D64_RC_SH		0x00000200
/* overflow continue */
#define	D64_RC_OC		0x00000400
/* parity check disable */
#define	D64_RC_PD		0x00000800
/* address extension bits */
#define	D64_RC_AE		0x00030000
#define	D64_RC_AE_SHIFT		16

/* flags for dma controller */
/* partity enable */
#define DMA_CTRL_PEN		(1 << 0)
/* rx overflow continue */
#define DMA_CTRL_ROC		(1 << 1)
/* allow rx scatter to multiple descriptors */
#define DMA_CTRL_RXMULTI	(1 << 2)
/* Unframed Rx/Tx data */
#define DMA_CTRL_UNFRAMED	(1 << 3)

/* receive descriptor table pointer */
#define	D64_RP_LD_MASK		0x00000fff	/* last valid descriptor */

/* receive channel status */
#define	D64_RS0_CD_MASK		0x00001fff	/* current descriptor pointer */
#define	D64_RS0_RS_MASK		0xf0000000	/* receive state */
#define	D64_RS0_RS_SHIFT		28
#define	D64_RS0_RS_DISABLED	0x00000000	/* disabled */
#define	D64_RS0_RS_ACTIVE	0x10000000	/* active */
#define	D64_RS0_RS_IDLE		0x20000000	/* idle wait */
#define	D64_RS0_RS_STOPPED	0x30000000	/* stopped */
#define	D64_RS0_RS_SUSP		0x40000000	/* suspend pending */

#define	D64_RS1_AD_MASK		0x0001ffff	/* active descriptor */
#define	D64_RS1_RE_MASK		0xf0000000	/* receive errors */
#define	D64_RS1_RE_SHIFT		28
#define	D64_RS1_RE_NOERR	0x00000000	/* no error */
#define	D64_RS1_RE_DPO		0x10000000	/* descriptor protocol error */
#define	D64_RS1_RE_DFU		0x20000000	/* data fifo overflow */
#define	D64_RS1_RE_DTE		0x30000000	/* data transfer error */
#define	D64_RS1_RE_DESRE	0x40000000	/* descriptor read error */
#define	D64_RS1_RE_COREE	0x50000000	/* core error */

/* fifoaddr */
#define	D64_FA_OFF_MASK		0xffff	/* offset */
#define	D64_FA_SEL_MASK		0xf0000	/* select */
#define	D64_FA_SEL_SHIFT	16
#define	D64_FA_SEL_XDD		0x00000	/* transmit dma data */
#define	D64_FA_SEL_XDP		0x10000	/* transmit dma pointers */
#define	D64_FA_SEL_RDD		0x40000	/* receive dma data */
#define	D64_FA_SEL_RDP		0x50000	/* receive dma pointers */
#define	D64_FA_SEL_XFD		0x80000	/* transmit fifo data */
#define	D64_FA_SEL_XFP		0x90000	/* transmit fifo pointers */
#define	D64_FA_SEL_RFD		0xc0000	/* receive fifo data */
#define	D64_FA_SEL_RFP		0xd0000	/* receive fifo pointers */
#define	D64_FA_SEL_RSD		0xe0000	/* receive frame status data */
#define	D64_FA_SEL_RSP		0xf0000	/* receive frame status pointers */

/* descriptor control flags 1 */
#define D64_CTRL_COREFLAGS	0x0ff00000	/* core specific flags */
#define	D64_CTRL1_EOT		((u32)1 << 28)	/* end of descriptor table */
#define	D64_CTRL1_IOC		((u32)1 << 29)	/* interrupt on completion */
#define	D64_CTRL1_EOF		((u32)1 << 30)	/* end of frame */
#define	D64_CTRL1_SOF		((u32)1 << 31)	/* start of frame */

/* descriptor control flags 2 */
/* buffer byte count. real data len must <= 16KB */
#define	D64_CTRL2_BC_MASK	0x00007fff
/* address extension bits */
#define	D64_CTRL2_AE		0x00030000
#define	D64_CTRL2_AE_SHIFT	16
/* parity bit */
#define D64_CTRL2_PARITY	0x00040000

/* control flags in the range [27:20] are core-specific and not defined here */
#define	D64_CTRL_CORE_MASK	0x0ff00000

#define D64_RX_FRM_STS_LEN	0x0000ffff	/* frame length mask */
#define D64_RX_FRM_STS_OVFL	0x00800000	/* RxOverFlow */
#define D64_RX_FRM_STS_DSCRCNT	0x0f000000  /* no. of descriptors used - 1 */
#define D64_RX_FRM_STS_DATATYPE	0xf0000000	/* core-dependent data type */

/*
 * packet headroom necessary to accommodate the largest header
 * in the system, (i.e TXOFF). By doing, we avoid the need to
 * allocate an extra buffer for the header when bridging to WL.
 * There is a compile time check in wlc.c which ensure that this
 * value is at least as big as TXOFF. This value is used in
 * dma_rxfill().
 */

#define BCMEXTRAHDROOM 172

#define	MAXNAMEL	8	/* 8 char names */

/* macros to convert between byte offsets and indexes */
#define	B2I(bytes, type)	((bytes) / sizeof(type))
#define	I2B(index, type)	((index) * sizeof(type))

#define	PCI32ADDR_HIGH		0xc0000000	/* address[31:30] */
#define	PCI32ADDR_HIGH_SHIFT	30	/* address[31:30] */

#define	PCI64ADDR_HIGH		0x80000000	/* address[63] */
#define	PCI64ADDR_HIGH_SHIFT	31	/* address[63] */

/*
 * DMA Descriptor
 * Descriptors are only read by the hardware, never written back.
 */
struct dma64desc {
	__le32 ctrl1;	/* misc control bits & bufcount */
	__le32 ctrl2;	/* buffer count and address extension */
	__le32 addrlow;	/* memory address of the date buffer, bits 31:0 */
	__le32 addrhigh; /* memory address of the date buffer, bits 63:32 */
};

/* dma engine software state */
struct dma_info {
	struct dma_pub dma; /* exported structure */
	char name[MAXNAMEL];	/* callers name for diag msgs */

	struct bcma_device *core;
	struct device *dmadev;

	/* session information for AMPDU */
	struct brcms_ampdu_session ampdu_session;

	bool dma64;	/* this dma engine is operating in 64-bit mode */
	bool addrext;	/* this dma engine supports DmaExtendedAddrChanges */

	/* 64-bit dma tx engine registers */
	uint d64txregbase;
	/* 64-bit dma rx engine registers */
	uint d64rxregbase;
	/* pointer to dma64 tx descriptor ring */
	struct dma64desc *txd64;
	/* pointer to dma64 rx descriptor ring */
	struct dma64desc *rxd64;

	u16 dmadesc_align;	/* alignment requirement for dma descriptors */

	u16 ntxd;		/* # tx descriptors tunable */
	u16 txin;		/* index of next descriptor to reclaim */
	u16 txout;		/* index of next descriptor to post */
	/* pointer to parallel array of pointers to packets */
	struct sk_buff **txp;
	/* Aligned physical address of descriptor ring */
	dma_addr_t txdpa;
	/* Original physical address of descriptor ring */
	dma_addr_t txdpaorig;
	u16 txdalign;	/* #bytes added to alloc'd mem to align txd */
	u32 txdalloc;	/* #bytes allocated for the ring */
	u32 xmtptrbase;	/* When using unaligned descriptors, the ptr register
			 * is not just an index, it needs all 13 bits to be
			 * an offset from the addr register.
			 */

	u16 nrxd;	/* # rx descriptors tunable */
	u16 rxin;	/* index of next descriptor to reclaim */
	u16 rxout;	/* index of next descriptor to post */
	/* pointer to parallel array of pointers to packets */
	struct sk_buff **rxp;
	/* Aligned physical address of descriptor ring */
	dma_addr_t rxdpa;
	/* Original physical address of descriptor ring */
	dma_addr_t rxdpaorig;
	u16 rxdalign;	/* #bytes added to alloc'd mem to align rxd */
	u32 rxdalloc;	/* #bytes allocated for the ring */
	u32 rcvptrbase;	/* Base for ptr reg when using unaligned descriptors */

	/* tunables */
	unsigned int rxbufsize;	/* rx buffer size in bytes, not including
				 * the extra headroom
				 */
	uint rxextrahdrroom;	/* extra rx headroom, reverseved to assist upper
				 * stack, e.g. some rx pkt buffers will be
				 * bridged to tx side without byte copying.
				 * The extra headroom needs to be large enough
				 * to fit txheader needs. Some dongle driver may
				 * not need it.
				 */
	uint nrxpost;		/* # rx buffers to keep posted */
	unsigned int rxoffset;	/* rxcontrol offset */
	/* add to get dma address of descriptor ring, low 32 bits */
	uint ddoffsetlow;
	/*   high 32 bits */
	uint ddoffsethigh;
	/* add to get dma address of data buffer, low 32 bits */
	uint dataoffsetlow;
	/*   high 32 bits */
	uint dataoffsethigh;
	/* descriptor base need to be aligned or not */
	bool aligndesc_4k;
};

/* Check for odd number of 1's */
static u32 parity32(__le32 data)
{
	/* no swap needed for counting 1's */
	u32 par_data = *(u32 *)&data;

	par_data ^= par_data >> 16;
	par_data ^= par_data >> 8;
	par_data ^= par_data >> 4;
	par_data ^= par_data >> 2;
	par_data ^= par_data >> 1;

	return par_data & 1;
}

static bool dma64_dd_parity(struct dma64desc *dd)
{
	return parity32(dd->addrlow ^ dd->addrhigh ^ dd->ctrl1 ^ dd->ctrl2);
}

/* descriptor bumping functions */

static uint xxd(uint x, uint n)
{
	return x & (n - 1); /* faster than %, but n must be power of 2 */
}

static uint txd(struct dma_info *di, uint x)
{
	return xxd(x, di->ntxd);
}

static uint rxd(struct dma_info *di, uint x)
{
	return xxd(x, di->nrxd);
}

static uint nexttxd(struct dma_info *di, uint i)
{
	return txd(di, i + 1);
}

static uint prevtxd(struct dma_info *di, uint i)
{
	return txd(di, i - 1);
}

static uint nextrxd(struct dma_info *di, uint i)
{
	return rxd(di, i + 1);
}

static uint ntxdactive(struct dma_info *di, uint h, uint t)
{
	return txd(di, t-h);
}

static uint nrxdactive(struct dma_info *di, uint h, uint t)
{
	return rxd(di, t-h);
}

static uint _dma_ctrlflags(struct dma_info *di, uint mask, uint flags)
{
	uint dmactrlflags;

	if (di == NULL) {
		brcms_dbg_dma(di->core, "NULL dma handle\n");
		return 0;
	}

	dmactrlflags = di->dma.dmactrlflags;
	dmactrlflags &= ~mask;
	dmactrlflags |= flags;

	/* If trying to enable parity, check if parity is actually supported */
	if (dmactrlflags & DMA_CTRL_PEN) {
		u32 control;

		control = bcma_read32(di->core, DMA64TXREGOFFS(di, control));
		bcma_write32(di->core, DMA64TXREGOFFS(di, control),
		      control | D64_XC_PD);
		if (bcma_read32(di->core, DMA64TXREGOFFS(di, control)) &
		    D64_XC_PD)
			/* We *can* disable it so it is supported,
			 * restore control register
			 */
			bcma_write32(di->core, DMA64TXREGOFFS(di, control),
				     control);
		else
			/* Not supported, don't allow it to be enabled */
			dmactrlflags &= ~DMA_CTRL_PEN;
	}

	di->dma.dmactrlflags = dmactrlflags;

	return dmactrlflags;
}

static bool _dma64_addrext(struct dma_info *di, uint ctrl_offset)
{
	u32 w;
	bcma_set32(di->core, ctrl_offset, D64_XC_AE);
	w = bcma_read32(di->core, ctrl_offset);
	bcma_mask32(di->core, ctrl_offset, ~D64_XC_AE);
	return (w & D64_XC_AE) == D64_XC_AE;
}

/*
 * return true if this dma engine supports DmaExtendedAddrChanges,
 * otherwise false
 */
static bool _dma_isaddrext(struct dma_info *di)
{
	/* DMA64 supports full 32- or 64-bit operation. AE is always valid */

	/* not all tx or rx channel are available */
	if (di->d64txregbase != 0) {
		if (!_dma64_addrext(di, DMA64TXREGOFFS(di, control)))
			brcms_dbg_dma(di->core,
				      "%s: DMA64 tx doesn't have AE set\n",
				      di->name);
		return true;
	} else if (di->d64rxregbase != 0) {
		if (!_dma64_addrext(di, DMA64RXREGOFFS(di, control)))
			brcms_dbg_dma(di->core,
				      "%s: DMA64 rx doesn't have AE set\n",
				      di->name);
		return true;
	}

	return false;
}

static bool _dma_descriptor_align(struct dma_info *di)
{
	u32 addrl;

	/* Check to see if the descriptors need to be aligned on 4K/8K or not */
	if (di->d64txregbase != 0) {
		bcma_write32(di->core, DMA64TXREGOFFS(di, addrlow), 0xff0);
		addrl = bcma_read32(di->core, DMA64TXREGOFFS(di, addrlow));
		if (addrl != 0)
			return false;
	} else if (di->d64rxregbase != 0) {
		bcma_write32(di->core, DMA64RXREGOFFS(di, addrlow), 0xff0);
		addrl = bcma_read32(di->core, DMA64RXREGOFFS(di, addrlow));
		if (addrl != 0)
			return false;
	}
	return true;
}

/*
 * Descriptor table must start at the DMA hardware dictated alignment, so
 * allocated memory must be large enough to support this requirement.
 */
static void *dma_alloc_consistent(struct dma_info *di, uint size,
				  u16 align_bits, uint *alloced,
				  dma_addr_t *pap)
{
	if (align_bits) {
		u16 align = (1 << align_bits);
		if (!IS_ALIGNED(PAGE_SIZE, align))
			size += align;
		*alloced = size;
	}
	return dma_alloc_coherent(di->dmadev, size, pap, GFP_ATOMIC);
}

static
u8 dma_align_sizetobits(uint size)
{
	u8 bitpos = 0;
	while (size >>= 1)
		bitpos++;
	return bitpos;
}

/* This function ensures that the DMA descriptor ring will not get allocated
 * across Page boundary. If the allocation is done across the page boundary
 * at the first time, then it is freed and the allocation is done at
 * descriptor ring size aligned location. This will ensure that the ring will
 * not cross page boundary
 */
static void *dma_ringalloc(struct dma_info *di, u32 boundary, uint size,
			   u16 *alignbits, uint *alloced,
			   dma_addr_t *descpa)
{
	void *va;
	u32 desc_strtaddr;
	u32 alignbytes = 1 << *alignbits;

	va = dma_alloc_consistent(di, size, *alignbits, alloced, descpa);

	if (NULL == va)
		return NULL;

	desc_strtaddr = (u32) roundup((unsigned long)va, alignbytes);
	if (((desc_strtaddr + size - 1) & boundary) != (desc_strtaddr
							& boundary)) {
		*alignbits = dma_align_sizetobits(size);
		dma_free_coherent(di->dmadev, size, va, *descpa);
		va = dma_alloc_consistent(di, size, *alignbits,
			alloced, descpa);
	}
	return va;
}

static bool dma64_alloc(struct dma_info *di, uint direction)
{
	u16 size;
	uint ddlen;
	void *va;
	uint alloced = 0;
	u16 align;
	u16 align_bits;

	ddlen = sizeof(struct dma64desc);

	size = (direction == DMA_TX) ? (di->ntxd * ddlen) : (di->nrxd * ddlen);
	align_bits = di->dmadesc_align;
	align = (1 << align_bits);

	if (direction == DMA_TX) {
		va = dma_ringalloc(di, D64RINGALIGN, size, &align_bits,
			&alloced, &di->txdpaorig);
		if (va == NULL) {
			brcms_dbg_dma(di->core,
				      "%s: DMA_ALLOC_CONSISTENT(ntxd) failed\n",
				      di->name);
			return false;
		}
		align = (1 << align_bits);
		di->txd64 = (struct dma64desc *)
					roundup((unsigned long)va, align);
		di->txdalign = (uint) ((s8 *)di->txd64 - (s8 *) va);
		di->txdpa = di->txdpaorig + di->txdalign;
		di->txdalloc = alloced;
	} else {
		va = dma_ringalloc(di, D64RINGALIGN, size, &align_bits,
			&alloced, &di->rxdpaorig);
		if (va == NULL) {
			brcms_dbg_dma(di->core,
				      "%s: DMA_ALLOC_CONSISTENT(nrxd) failed\n",
				      di->name);
			return false;
		}
		align = (1 << align_bits);
		di->rxd64 = (struct dma64desc *)
					roundup((unsigned long)va, align);
		di->rxdalign = (uint) ((s8 *)di->rxd64 - (s8 *) va);
		di->rxdpa = di->rxdpaorig + di->rxdalign;
		di->rxdalloc = alloced;
	}

	return true;
}

static bool _dma_alloc(struct dma_info *di, uint direction)
{
	return dma64_alloc(di, direction);
}

struct dma_pub *dma_attach(char *name, struct brcms_c_info *wlc,
			   uint txregbase, uint rxregbase, uint ntxd, uint nrxd,
			   uint rxbufsize, int rxextheadroom,
			   uint nrxpost, uint rxoffset)
{
	struct si_pub *sih = wlc->hw->sih;
	struct bcma_device *core = wlc->hw->d11core;
	struct dma_info *di;
	u8 rev = core->id.rev;
	uint size;
	struct si_info *sii = container_of(sih, struct si_info, pub);

	/* allocate private info structure */
	di = kzalloc(sizeof(struct dma_info), GFP_ATOMIC);
	if (di == NULL)
		return NULL;

	di->dma64 =
		((bcma_aread32(core, BCMA_IOST) & SISF_DMA64) == SISF_DMA64);

	/* init dma reg info */
	di->core = core;
	di->d64txregbase = txregbase;
	di->d64rxregbase = rxregbase;

	/*
	 * Default flags (which can be changed by the driver calling
	 * dma_ctrlflags before enable): For backwards compatibility
	 * both Rx Overflow Continue and Parity are DISABLED.
	 */
	_dma_ctrlflags(di, DMA_CTRL_ROC | DMA_CTRL_PEN, 0);

	brcms_dbg_dma(di->core, "%s: %s flags 0x%x ntxd %d nrxd %d "
		      "rxbufsize %d rxextheadroom %d nrxpost %d rxoffset %d "
		      "txregbase %u rxregbase %u\n", name, "DMA64",
		      di->dma.dmactrlflags, ntxd, nrxd, rxbufsize,
		      rxextheadroom, nrxpost, rxoffset, txregbase, rxregbase);

	/* make a private copy of our callers name */
	strncpy(di->name, name, MAXNAMEL);
	di->name[MAXNAMEL - 1] = '\0';

	di->dmadev = core->dma_dev;

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
	 *     PCI/PCIE: they map silicon backplace address to zero
	 *     based memory, need offset
	 *     Other bus: use zero SI_BUS BIGENDIAN kludge: use sdram
	 *     swapped region for data buffer, not descriptor
	 */
	di->ddoffsetlow = 0;
	di->dataoffsetlow = 0;
	/* for pci bus, add offset */
	if (sii->icbus->hosttype == BCMA_HOSTTYPE_PCI) {
		/* add offset for pcie with DMA64 bus */
		di->ddoffsetlow = 0;
		di->ddoffsethigh = SI_PCIE_DMA_H32;
	}
	di->dataoffsetlow = di->ddoffsetlow;
	di->dataoffsethigh = di->ddoffsethigh;

	/* WAR64450 : DMACtl.Addr ext fields are not supported in SDIOD core. */
	if ((core->id.id == BCMA_CORE_SDIO_DEV)
	    && ((rev > 0) && (rev <= 2)))
		di->addrext = false;
	else if ((core->id.id == BCMA_CORE_I2S) &&
		 ((rev == 0) || (rev == 1)))
		di->addrext = false;
	else
		di->addrext = _dma_isaddrext(di);

	/* does the descriptor need to be aligned and if yes, on 4K/8K or not */
	di->aligndesc_4k = _dma_descriptor_align(di);
	if (di->aligndesc_4k) {
		di->dmadesc_align = D64RINGALIGN_BITS;
		if ((ntxd < D64MAXDD / 2) && (nrxd < D64MAXDD / 2))
			/* for smaller dd table, HW relax alignment reqmnt */
			di->dmadesc_align = D64RINGALIGN_BITS - 1;
	} else {
		di->dmadesc_align = 4;	/* 16 byte alignment */
	}

	brcms_dbg_dma(di->core, "DMA descriptor align_needed %d, align %d\n",
		      di->aligndesc_4k, di->dmadesc_align);

	/* allocate tx packet pointer vector */
	if (ntxd) {
		size = ntxd * sizeof(void *);
		di->txp = kzalloc(size, GFP_ATOMIC);
		if (di->txp == NULL)
			goto fail;
	}

	/* allocate rx packet pointer vector */
	if (nrxd) {
		size = nrxd * sizeof(void *);
		di->rxp = kzalloc(size, GFP_ATOMIC);
		if (di->rxp == NULL)
			goto fail;
	}

	/*
	 * allocate transmit descriptor ring, only need ntxd descriptors
	 * but it must be aligned
	 */
	if (ntxd) {
		if (!_dma_alloc(di, DMA_TX))
			goto fail;
	}

	/*
	 * allocate receive descriptor ring, only need nrxd descriptors
	 * but it must be aligned
	 */
	if (nrxd) {
		if (!_dma_alloc(di, DMA_RX))
			goto fail;
	}

	if ((di->ddoffsetlow != 0) && !di->addrext) {
		if (di->txdpa > SI_PCI_DMA_SZ) {
			brcms_dbg_dma(di->core,
				      "%s: txdpa 0x%x: addrext not supported\n",
				      di->name, (u32)di->txdpa);
			goto fail;
		}
		if (di->rxdpa > SI_PCI_DMA_SZ) {
			brcms_dbg_dma(di->core,
				      "%s: rxdpa 0x%x: addrext not supported\n",
				      di->name, (u32)di->rxdpa);
			goto fail;
		}
	}

	/* Initialize AMPDU session */
	brcms_c_ampdu_reset_session(&di->ampdu_session, wlc);

	brcms_dbg_dma(di->core,
		      "ddoffsetlow 0x%x ddoffsethigh 0x%x dataoffsetlow 0x%x dataoffsethigh 0x%x addrext %d\n",
		      di->ddoffsetlow, di->ddoffsethigh,
		      di->dataoffsetlow, di->dataoffsethigh,
		      di->addrext);

	return (struct dma_pub *) di;

 fail:
	dma_detach((struct dma_pub *)di);
	return NULL;
}

static inline void
dma64_dd_upd(struct dma_info *di, struct dma64desc *ddring,
	     dma_addr_t pa, uint outidx, u32 *flags, u32 bufcount)
{
	u32 ctrl2 = bufcount & D64_CTRL2_BC_MASK;

	/* PCI bus with big(>1G) physical address, use address extension */
	if ((di->dataoffsetlow == 0) || !(pa & PCI32ADDR_HIGH)) {
		ddring[outidx].addrlow = cpu_to_le32(pa + di->dataoffsetlow);
		ddring[outidx].addrhigh = cpu_to_le32(di->dataoffsethigh);
		ddring[outidx].ctrl1 = cpu_to_le32(*flags);
		ddring[outidx].ctrl2 = cpu_to_le32(ctrl2);
	} else {
		/* address extension for 32-bit PCI */
		u32 ae;

		ae = (pa & PCI32ADDR_HIGH) >> PCI32ADDR_HIGH_SHIFT;
		pa &= ~PCI32ADDR_HIGH;

		ctrl2 |= (ae << D64_CTRL2_AE_SHIFT) & D64_CTRL2_AE;
		ddring[outidx].addrlow = cpu_to_le32(pa + di->dataoffsetlow);
		ddring[outidx].addrhigh = cpu_to_le32(di->dataoffsethigh);
		ddring[outidx].ctrl1 = cpu_to_le32(*flags);
		ddring[outidx].ctrl2 = cpu_to_le32(ctrl2);
	}
	if (di->dma.dmactrlflags & DMA_CTRL_PEN) {
		if (dma64_dd_parity(&ddring[outidx]))
			ddring[outidx].ctrl2 =
			     cpu_to_le32(ctrl2 | D64_CTRL2_PARITY);
	}
}

/* !! may be called with core in reset */
void dma_detach(struct dma_pub *pub)
{
	struct dma_info *di = (struct dma_info *)pub;

	brcms_dbg_dma(di->core, "%s:\n", di->name);

	/* free dma descriptor rings */
	if (di->txd64)
		dma_free_coherent(di->dmadev, di->txdalloc,
				  ((s8 *)di->txd64 - di->txdalign),
				  (di->txdpaorig));
	if (di->rxd64)
		dma_free_coherent(di->dmadev, di->rxdalloc,
				  ((s8 *)di->rxd64 - di->rxdalign),
				  (di->rxdpaorig));

	/* free packet pointer vectors */
	kfree(di->txp);
	kfree(di->rxp);

	/* free our private info structure */
	kfree(di);

}

/* initialize descriptor table base address */
static void
_dma_ddtable_init(struct dma_info *di, uint direction, dma_addr_t pa)
{
	if (!di->aligndesc_4k) {
		if (direction == DMA_TX)
			di->xmtptrbase = pa;
		else
			di->rcvptrbase = pa;
	}

	if ((di->ddoffsetlow == 0)
	    || !(pa & PCI32ADDR_HIGH)) {
		if (direction == DMA_TX) {
			bcma_write32(di->core, DMA64TXREGOFFS(di, addrlow),
				     pa + di->ddoffsetlow);
			bcma_write32(di->core, DMA64TXREGOFFS(di, addrhigh),
				     di->ddoffsethigh);
		} else {
			bcma_write32(di->core, DMA64RXREGOFFS(di, addrlow),
				     pa + di->ddoffsetlow);
			bcma_write32(di->core, DMA64RXREGOFFS(di, addrhigh),
				     di->ddoffsethigh);
		}
	} else {
		/* DMA64 32bits address extension */
		u32 ae;

		/* shift the high bit(s) from pa to ae */
		ae = (pa & PCI32ADDR_HIGH) >> PCI32ADDR_HIGH_SHIFT;
		pa &= ~PCI32ADDR_HIGH;

		if (direction == DMA_TX) {
			bcma_write32(di->core, DMA64TXREGOFFS(di, addrlow),
				     pa + di->ddoffsetlow);
			bcma_write32(di->core, DMA64TXREGOFFS(di, addrhigh),
				     di->ddoffsethigh);
			bcma_maskset32(di->core, DMA64TXREGOFFS(di, control),
				       D64_XC_AE, (ae << D64_XC_AE_SHIFT));
		} else {
			bcma_write32(di->core, DMA64RXREGOFFS(di, addrlow),
				     pa + di->ddoffsetlow);
			bcma_write32(di->core, DMA64RXREGOFFS(di, addrhigh),
				     di->ddoffsethigh);
			bcma_maskset32(di->core, DMA64RXREGOFFS(di, control),
				       D64_RC_AE, (ae << D64_RC_AE_SHIFT));
		}
	}
}

static void _dma_rxenable(struct dma_info *di)
{
	uint dmactrlflags = di->dma.dmactrlflags;
	u32 control;

	brcms_dbg_dma(di->core, "%s:\n", di->name);

	control = D64_RC_RE | (bcma_read32(di->core,
					   DMA64RXREGOFFS(di, control)) &
			       D64_RC_AE);

	if ((dmactrlflags & DMA_CTRL_PEN) == 0)
		control |= D64_RC_PD;

	if (dmactrlflags & DMA_CTRL_ROC)
		control |= D64_RC_OC;

	bcma_write32(di->core, DMA64RXREGOFFS(di, control),
		((di->rxoffset << D64_RC_RO_SHIFT) | control));
}

void dma_rxinit(struct dma_pub *pub)
{
	struct dma_info *di = (struct dma_info *)pub;

	brcms_dbg_dma(di->core, "%s:\n", di->name);

	if (di->nrxd == 0)
		return;

	di->rxin = di->rxout = 0;

	/* clear rx descriptor ring */
	memset(di->rxd64, '\0', di->nrxd * sizeof(struct dma64desc));

	/* DMA engine with out alignment requirement requires table to be inited
	 * before enabling the engine
	 */
	if (!di->aligndesc_4k)
		_dma_ddtable_init(di, DMA_RX, di->rxdpa);

	_dma_rxenable(di);

	if (di->aligndesc_4k)
		_dma_ddtable_init(di, DMA_RX, di->rxdpa);
}

static struct sk_buff *dma64_getnextrxp(struct dma_info *di, bool forceall)
{
	uint i, curr;
	struct sk_buff *rxp;
	dma_addr_t pa;

	i = di->rxin;

	/* return if no packets posted */
	if (i == di->rxout)
		return NULL;

	curr =
	    B2I(((bcma_read32(di->core,
			      DMA64RXREGOFFS(di, status0)) & D64_RS0_CD_MASK) -
		 di->rcvptrbase) & D64_RS0_CD_MASK, struct dma64desc);

	/* ignore curr if forceall */
	if (!forceall && (i == curr))
		return NULL;

	/* get the packet pointer that corresponds to the rx descriptor */
	rxp = di->rxp[i];
	di->rxp[i] = NULL;

	pa = le32_to_cpu(di->rxd64[i].addrlow) - di->dataoffsetlow;

	/* clear this packet from the descriptor ring */
	dma_unmap_single(di->dmadev, pa, di->rxbufsize, DMA_FROM_DEVICE);

	di->rxd64[i].addrlow = cpu_to_le32(0xdeadbeef);
	di->rxd64[i].addrhigh = cpu_to_le32(0xdeadbeef);

	di->rxin = nextrxd(di, i);

	return rxp;
}

static struct sk_buff *_dma_getnextrxp(struct dma_info *di, bool forceall)
{
	if (di->nrxd == 0)
		return NULL;

	return dma64_getnextrxp(di, forceall);
}

/*
 * !! rx entry routine
 * returns the number packages in the next frame, or 0 if there are no more
 *   if DMA_CTRL_RXMULTI is defined, DMA scattering(multiple buffers) is
 *   supported with pkts chain
 *   otherwise, it's treated as giant pkt and will be tossed.
 *   The DMA scattering starts with normal DMA header, followed by first
 *   buffer data. After it reaches the max size of buffer, the data continues
 *   in next DMA descriptor buffer WITHOUT DMA header
 */
int dma_rx(struct dma_pub *pub, struct sk_buff_head *skb_list)
{
	struct dma_info *di = (struct dma_info *)pub;
	struct sk_buff_head dma_frames;
	struct sk_buff *p, *next;
	uint len;
	uint pkt_len;
	int resid = 0;
	int pktcnt = 1;

	skb_queue_head_init(&dma_frames);
 next_frame:
	p = _dma_getnextrxp(di, false);
	if (p == NULL)
		return 0;

	len = le16_to_cpu(*(__le16 *) (p->data));
	brcms_dbg_dma(di->core, "%s: dma_rx len %d\n", di->name, len);
	dma_spin_for_len(len, p);

	/* set actual length */
	pkt_len = min((di->rxoffset + len), di->rxbufsize);
	__skb_trim(p, pkt_len);
	skb_queue_tail(&dma_frames, p);
	resid = len - (di->rxbufsize - di->rxoffset);

	/* check for single or multi-buffer rx */
	if (resid > 0) {
		while ((resid > 0) && (p = _dma_getnextrxp(di, false))) {
			pkt_len = min_t(uint, resid, di->rxbufsize);
			__skb_trim(p, pkt_len);
			skb_queue_tail(&dma_frames, p);
			resid -= di->rxbufsize;
			pktcnt++;
		}

#ifdef DEBUG
		if (resid > 0) {
			uint cur;
			cur =
			    B2I(((bcma_read32(di->core,
					      DMA64RXREGOFFS(di, status0)) &
				  D64_RS0_CD_MASK) - di->rcvptrbase) &
				D64_RS0_CD_MASK, struct dma64desc);
			brcms_dbg_dma(di->core,
				      "rxin %d rxout %d, hw_curr %d\n",
				      di->rxin, di->rxout, cur);
		}
#endif				/* DEBUG */

		if ((di->dma.dmactrlflags & DMA_CTRL_RXMULTI) == 0) {
			brcms_dbg_dma(di->core, "%s: bad frame length (%d)\n",
				      di->name, len);
			skb_queue_walk_safe(&dma_frames, p, next) {
				skb_unlink(p, &dma_frames);
				brcmu_pkt_buf_free_skb(p);
			}
			di->dma.rxgiants++;
			pktcnt = 1;
			goto next_frame;
		}
	}

	skb_queue_splice_tail(&dma_frames, skb_list);
	return pktcnt;
}

static bool dma64_rxidle(struct dma_info *di)
{
	brcms_dbg_dma(di->core, "%s:\n", di->name);

	if (di->nrxd == 0)
		return true;

	return ((bcma_read32(di->core,
			     DMA64RXREGOFFS(di, status0)) & D64_RS0_CD_MASK) ==
		(bcma_read32(di->core, DMA64RXREGOFFS(di, ptr)) &
		 D64_RS0_CD_MASK));
}

static bool dma64_txidle(struct dma_info *di)
{
	if (di->ntxd == 0)
		return true;

	return ((bcma_read32(di->core,
			     DMA64TXREGOFFS(di, status0)) & D64_XS0_CD_MASK) ==
		(bcma_read32(di->core, DMA64TXREGOFFS(di, ptr)) &
		 D64_XS0_CD_MASK));
}

/*
 * post receive buffers
 *  return false is refill failed completely and ring is empty this will stall
 *  the rx dma and user might want to call rxfill again asap. This unlikely
 *  happens on memory-rich NIC, but often on memory-constrained dongle
 */
bool dma_rxfill(struct dma_pub *pub)
{
	struct dma_info *di = (struct dma_info *)pub;
	struct sk_buff *p;
	u16 rxin, rxout;
	u32 flags = 0;
	uint n;
	uint i;
	dma_addr_t pa;
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

	n = di->nrxpost - nrxdactive(di, rxin, rxout);

	brcms_dbg_dma(di->core, "%s: post %d\n", di->name, n);

	if (di->rxbufsize > BCMEXTRAHDROOM)
		extra_offset = di->rxextrahdrroom;

	for (i = 0; i < n; i++) {
		/*
		 * the di->rxbufsize doesn't include the extra headroom,
		 * we need to add it to the size to be allocated
		 */
		p = brcmu_pkt_buf_get_skb(di->rxbufsize + extra_offset);

		if (p == NULL) {
			brcms_dbg_dma(di->core, "%s: out of rxbufs\n",
				      di->name);
			if (i == 0 && dma64_rxidle(di)) {
				brcms_dbg_dma(di->core, "%s: ring is empty !\n",
					      di->name);
				ring_empty = true;
			}
			di->dma.rxnobuf++;
			break;
		}
		/* reserve an extra headroom, if applicable */
		if (extra_offset)
			skb_pull(p, extra_offset);

		/* Do a cached write instead of uncached write since DMA_MAP
		 * will flush the cache.
		 */
		*(u32 *) (p->data) = 0;

		pa = dma_map_single(di->dmadev, p->data, di->rxbufsize,
				    DMA_FROM_DEVICE);

		/* save the free packet pointer */
		di->rxp[rxout] = p;

		/* reset flags for each descriptor */
		flags = 0;
		if (rxout == (di->nrxd - 1))
			flags = D64_CTRL1_EOT;

		dma64_dd_upd(di, di->rxd64, pa, rxout, &flags,
			     di->rxbufsize);
		rxout = nextrxd(di, rxout);
	}

	di->rxout = rxout;

	/* update the chip lastdscr pointer */
	bcma_write32(di->core, DMA64RXREGOFFS(di, ptr),
	      di->rcvptrbase + I2B(rxout, struct dma64desc));

	return ring_empty;
}

void dma_rxreclaim(struct dma_pub *pub)
{
	struct dma_info *di = (struct dma_info *)pub;
	struct sk_buff *p;

	brcms_dbg_dma(di->core, "%s:\n", di->name);

	while ((p = _dma_getnextrxp(di, true)))
		brcmu_pkt_buf_free_skb(p);
}

void dma_counterreset(struct dma_pub *pub)
{
	/* reset all software counters */
	pub->rxgiants = 0;
	pub->rxnobuf = 0;
	pub->txnobuf = 0;
}

/* get the address of the var in order to change later */
unsigned long dma_getvar(struct dma_pub *pub, const char *name)
{
	struct dma_info *di = (struct dma_info *)pub;

	if (!strcmp(name, "&txavail"))
		return (unsigned long)&(di->dma.txavail);
	return 0;
}

/* 64-bit DMA functions */

void dma_txinit(struct dma_pub *pub)
{
	struct dma_info *di = (struct dma_info *)pub;
	u32 control = D64_XC_XE;

	brcms_dbg_dma(di->core, "%s:\n", di->name);

	if (di->ntxd == 0)
		return;

	di->txin = di->txout = 0;
	di->dma.txavail = di->ntxd - 1;

	/* clear tx descriptor ring */
	memset(di->txd64, '\0', (di->ntxd * sizeof(struct dma64desc)));

	/* DMA engine with out alignment requirement requires table to be inited
	 * before enabling the engine
	 */
	if (!di->aligndesc_4k)
		_dma_ddtable_init(di, DMA_TX, di->txdpa);

	if ((di->dma.dmactrlflags & DMA_CTRL_PEN) == 0)
		control |= D64_XC_PD;
	bcma_set32(di->core, DMA64TXREGOFFS(di, control), control);

	/* DMA engine with alignment requirement requires table to be inited
	 * before enabling the engine
	 */
	if (di->aligndesc_4k)
		_dma_ddtable_init(di, DMA_TX, di->txdpa);
}

void dma_txsuspend(struct dma_pub *pub)
{
	struct dma_info *di = (struct dma_info *)pub;

	brcms_dbg_dma(di->core, "%s:\n", di->name);

	if (di->ntxd == 0)
		return;

	bcma_set32(di->core, DMA64TXREGOFFS(di, control), D64_XC_SE);
}

void dma_txresume(struct dma_pub *pub)
{
	struct dma_info *di = (struct dma_info *)pub;

	brcms_dbg_dma(di->core, "%s:\n", di->name);

	if (di->ntxd == 0)
		return;

	bcma_mask32(di->core, DMA64TXREGOFFS(di, control), ~D64_XC_SE);
}

bool dma_txsuspended(struct dma_pub *pub)
{
	struct dma_info *di = (struct dma_info *)pub;

	return (di->ntxd == 0) ||
	       ((bcma_read32(di->core,
			     DMA64TXREGOFFS(di, control)) & D64_XC_SE) ==
		D64_XC_SE);
}

void dma_txreclaim(struct dma_pub *pub, enum txd_range range)
{
	struct dma_info *di = (struct dma_info *)pub;
	struct sk_buff *p;

	brcms_dbg_dma(di->core, "%s: %s\n",
		      di->name,
		      range == DMA_RANGE_ALL ? "all" :
		      range == DMA_RANGE_TRANSMITTED ? "transmitted" :
		      "transferred");

	if (di->txin == di->txout)
		return;

	while ((p = dma_getnexttxp(pub, range))) {
		/* For unframed data, we don't have any packets to free */
		if (!(di->dma.dmactrlflags & DMA_CTRL_UNFRAMED))
			brcmu_pkt_buf_free_skb(p);
	}
}

bool dma_txreset(struct dma_pub *pub)
{
	struct dma_info *di = (struct dma_info *)pub;
	u32 status;

	if (di->ntxd == 0)
		return true;

	/* suspend tx DMA first */
	bcma_write32(di->core, DMA64TXREGOFFS(di, control), D64_XC_SE);
	SPINWAIT(((status =
		   (bcma_read32(di->core, DMA64TXREGOFFS(di, status0)) &
		    D64_XS0_XS_MASK)) != D64_XS0_XS_DISABLED) &&
		  (status != D64_XS0_XS_IDLE) && (status != D64_XS0_XS_STOPPED),
		 10000);

	bcma_write32(di->core, DMA64TXREGOFFS(di, control), 0);
	SPINWAIT(((status =
		   (bcma_read32(di->core, DMA64TXREGOFFS(di, status0)) &
		    D64_XS0_XS_MASK)) != D64_XS0_XS_DISABLED), 10000);

	/* wait for the last transaction to complete */
	udelay(300);

	return status == D64_XS0_XS_DISABLED;
}

bool dma_rxreset(struct dma_pub *pub)
{
	struct dma_info *di = (struct dma_info *)pub;
	u32 status;

	if (di->nrxd == 0)
		return true;

	bcma_write32(di->core, DMA64RXREGOFFS(di, control), 0);
	SPINWAIT(((status =
		   (bcma_read32(di->core, DMA64RXREGOFFS(di, status0)) &
		    D64_RS0_RS_MASK)) != D64_RS0_RS_DISABLED), 10000);

	return status == D64_RS0_RS_DISABLED;
}

static void dma_txenq(struct dma_info *di, struct sk_buff *p)
{
	unsigned char *data;
	uint len;
	u16 txout;
	u32 flags = 0;
	dma_addr_t pa;

	txout = di->txout;

	if (WARN_ON(nexttxd(di, txout) == di->txin))
		return;

	/*
	 * obtain and initialize transmit descriptor entry.
	 */
	data = p->data;
	len = p->len;

	/* get physical address of buffer start */
	pa = dma_map_single(di->dmadev, data, len, DMA_TO_DEVICE);

	/* With a DMA segment list, Descriptor table is filled
	 * using the segment list instead of looping over
	 * buffers in multi-chain DMA. Therefore, EOF for SGLIST
	 * is when end of segment list is reached.
	 */
	flags = D64_CTRL1_SOF | D64_CTRL1_IOC | D64_CTRL1_EOF;
	if (txout == (di->ntxd - 1))
		flags |= D64_CTRL1_EOT;

	dma64_dd_upd(di, di->txd64, pa, txout, &flags, len);

	txout = nexttxd(di, txout);

	/* save the packet */
	di->txp[prevtxd(di, txout)] = p;

	/* bump the tx descriptor index */
	di->txout = txout;
}

static void ampdu_finalize(struct dma_info *di)
{
	struct brcms_ampdu_session *session = &di->ampdu_session;
	struct sk_buff *p;

	if (WARN_ON(skb_queue_empty(&session->skb_list)))
		return;

	brcms_c_ampdu_finalize(session);

	while (!skb_queue_empty(&session->skb_list)) {
		p = skb_dequeue(&session->skb_list);
		dma_txenq(di, p);
	}

	bcma_write32(di->core, DMA64TXREGOFFS(di, ptr),
		     di->xmtptrbase + I2B(di->txout, struct dma64desc));
	brcms_c_ampdu_reset_session(session, session->wlc);
}

static void prep_ampdu_frame(struct dma_info *di, struct sk_buff *p)
{
	struct brcms_ampdu_session *session = &di->ampdu_session;
	int ret;

	ret = brcms_c_ampdu_add_frame(session, p);
	if (ret == -ENOSPC) {
		/*
		 * AMPDU cannot accomodate this frame. Close out the in-
		 * progress AMPDU session and start a new one.
		 */
		ampdu_finalize(di);
		ret = brcms_c_ampdu_add_frame(session, p);
	}

	WARN_ON(ret);
}

/* Update count of available tx descriptors based on current DMA state */
static void dma_update_txavail(struct dma_info *di)
{
	/*
	 * Available space is number of descriptors less the number of
	 * active descriptors and the number of queued AMPDU frames.
	 */
	di->dma.txavail = di->ntxd - ntxdactive(di, di->txin, di->txout) -
			  skb_queue_len(&di->ampdu_session.skb_list) - 1;
}

/*
 * !! tx entry routine
 * WARNING: call must check the return value for error.
 *   the error(toss frames) could be fatal and cause many subsequent hard
 *   to debug problems
 */
int dma_txfast(struct brcms_c_info *wlc, struct dma_pub *pub,
	       struct sk_buff *p)
{
	struct dma_info *di = (struct dma_info *)pub;
	struct brcms_ampdu_session *session = &di->ampdu_session;
	struct ieee80211_tx_info *tx_info;
	bool is_ampdu;

	brcms_dbg_dma(di->core, "%s:\n", di->name);

	/* no use to transmit a zero length packet */
	if (p->len == 0)
		return 0;

	/* return nonzero if out of tx descriptors */
	if (di->dma.txavail == 0 || nexttxd(di, di->txout) == di->txin)
		goto outoftxd;

	tx_info = IEEE80211_SKB_CB(p);
	is_ampdu = tx_info->flags & IEEE80211_TX_CTL_AMPDU;
	if (is_ampdu)
		prep_ampdu_frame(di, p);
	else
		dma_txenq(di, p);

	/* tx flow control */
	dma_update_txavail(di);

	/* kick the chip */
	if (is_ampdu) {
		/*
		 * Start sending data if we've got a full AMPDU, there's
		 * no more space in the DMA ring, or the ring isn't
		 * currently transmitting.
		 */
		if (skb_queue_len(&session->skb_list) == session->max_ampdu_frames ||
		    di->dma.txavail == 0 || dma64_txidle(di))
			ampdu_finalize(di);
	} else {
		bcma_write32(di->core, DMA64TXREGOFFS(di, ptr),
			     di->xmtptrbase + I2B(di->txout, struct dma64desc));
	}

	return 0;

 outoftxd:
	brcms_dbg_dma(di->core, "%s: out of txds !!!\n", di->name);
	brcmu_pkt_buf_free_skb(p);
	di->dma.txavail = 0;
	di->dma.txnobuf++;
	return -ENOSPC;
}

void dma_txflush(struct dma_pub *pub)
{
	struct dma_info *di = (struct dma_info *)pub;
	struct brcms_ampdu_session *session = &di->ampdu_session;

	if (!skb_queue_empty(&session->skb_list))
		ampdu_finalize(di);
}

int dma_txpending(struct dma_pub *pub)
{
	struct dma_info *di = (struct dma_info *)pub;
	return ntxdactive(di, di->txin, di->txout);
}

/*
 * If we have an active AMPDU session and are not transmitting,
 * this function will force tx to start.
 */
void dma_kick_tx(struct dma_pub *pub)
{
	struct dma_info *di = (struct dma_info *)pub;
	struct brcms_ampdu_session *session = &di->ampdu_session;

	if (!skb_queue_empty(&session->skb_list) && dma64_txidle(di))
		ampdu_finalize(di);
}

/*
 * Reclaim next completed txd (txds if using chained buffers) in the range
 * specified and return associated packet.
 * If range is DMA_RANGE_TRANSMITTED, reclaim descriptors that have be
 * transmitted as noted by the hardware "CurrDescr" pointer.
 * If range is DMA_RANGE_TRANSFERED, reclaim descriptors that have be
 * transferred by the DMA as noted by the hardware "ActiveDescr" pointer.
 * If range is DMA_RANGE_ALL, reclaim all txd(s) posted to the ring and
 * return associated packet regardless of the value of hardware pointers.
 */
struct sk_buff *dma_getnexttxp(struct dma_pub *pub, enum txd_range range)
{
	struct dma_info *di = (struct dma_info *)pub;
	u16 start, end, i;
	u16 active_desc;
	struct sk_buff *txp;

	brcms_dbg_dma(di->core, "%s: %s\n",
		      di->name,
		      range == DMA_RANGE_ALL ? "all" :
		      range == DMA_RANGE_TRANSMITTED ? "transmitted" :
		      "transferred");

	if (di->ntxd == 0)
		return NULL;

	txp = NULL;

	start = di->txin;
	if (range == DMA_RANGE_ALL)
		end = di->txout;
	else {
		end = (u16) (B2I(((bcma_read32(di->core,
					       DMA64TXREGOFFS(di, status0)) &
				   D64_XS0_CD_MASK) - di->xmtptrbase) &
				 D64_XS0_CD_MASK, struct dma64desc));

		if (range == DMA_RANGE_TRANSFERED) {
			active_desc =
				(u16)(bcma_read32(di->core,
						  DMA64TXREGOFFS(di, status1)) &
				      D64_XS1_AD_MASK);
			active_desc =
			    (active_desc - di->xmtptrbase) & D64_XS0_CD_MASK;
			active_desc = B2I(active_desc, struct dma64desc);
			if (end != active_desc)
				end = prevtxd(di, active_desc);
		}
	}

	if ((start == 0) && (end > di->txout))
		goto bogus;

	for (i = start; i != end && !txp; i = nexttxd(di, i)) {
		dma_addr_t pa;
		uint size;

		pa = le32_to_cpu(di->txd64[i].addrlow) - di->dataoffsetlow;

		size =
		    (le32_to_cpu(di->txd64[i].ctrl2) &
		     D64_CTRL2_BC_MASK);

		di->txd64[i].addrlow = cpu_to_le32(0xdeadbeef);
		di->txd64[i].addrhigh = cpu_to_le32(0xdeadbeef);

		txp = di->txp[i];
		di->txp[i] = NULL;

		dma_unmap_single(di->dmadev, pa, size, DMA_TO_DEVICE);
	}

	di->txin = i;

	/* tx flow control */
	dma_update_txavail(di);

	return txp;

 bogus:
	brcms_dbg_dma(di->core, "bogus curr: start %d end %d txout %d\n",
		      start, end, di->txout);
	return NULL;
}

/*
 * Mac80211 initiated actions sometimes require packets in the DMA queue to be
 * modified. The modified portion of the packet is not under control of the DMA
 * engine. This function calls a caller-supplied function for each packet in
 * the caller specified dma chain.
 */
void dma_walk_packets(struct dma_pub *dmah, void (*callback_fnc)
		      (void *pkt, void *arg_a), void *arg_a)
{
	struct dma_info *di = (struct dma_info *) dmah;
	uint i =   di->txin;
	uint end = di->txout;
	struct sk_buff *skb;
	struct ieee80211_tx_info *tx_info;

	while (i != end) {
		skb = di->txp[i];
		if (skb != NULL) {
			tx_info = (struct ieee80211_tx_info *)skb->cb;
			(callback_fnc)(tx_info, arg_a);
		}
		i = nexttxd(di, i);
	}
}
