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

#include <typedefs.h>
#include <bcmendian.h>
#include <linuxver.h>
#include <bcmdefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <linux/delay.h>
#ifdef mips
#include <asm/paccess.h>
#endif				/* mips */
#include <pcicfg.h>

#include <linux/fs.h>

#define PCI_CFG_RETRY 		10

#define OS_HANDLE_MAGIC		0x1234abcd	/* Magic # to recognise osh */
#define BCM_MEM_FILENAME_LEN 	24	/* Mem. filename length */

typedef struct bcm_mem_link {
	struct bcm_mem_link *prev;
	struct bcm_mem_link *next;
	uint size;
	int line;
	char file[BCM_MEM_FILENAME_LEN];
} bcm_mem_link_t;

struct osl_info {
	osl_pubinfo_t pub;
	uint magic;
	void *pdev;
	uint malloced;
	uint failed;
	uint bustype;
	bcm_mem_link_t *dbgmem_list;
};

/* Global ASSERT type flag */
uint32 g_assert_type;

static int16 linuxbcmerrormap[] = { 0,	/* 0 */
	-EINVAL,		/* BCME_ERROR */
	-EINVAL,		/* BCME_BADARG */
	-EINVAL,		/* BCME_BADOPTION */
	-EINVAL,		/* BCME_NOTUP */
	-EINVAL,		/* BCME_NOTDOWN */
	-EINVAL,		/* BCME_NOTAP */
	-EINVAL,		/* BCME_NOTSTA */
	-EINVAL,		/* BCME_BADKEYIDX */
	-EINVAL,		/* BCME_RADIOOFF */
	-EINVAL,		/* BCME_NOTBANDLOCKED */
	-EINVAL,		/* BCME_NOCLK */
	-EINVAL,		/* BCME_BADRATESET */
	-EINVAL,		/* BCME_BADBAND */
	-E2BIG,			/* BCME_BUFTOOSHORT */
	-E2BIG,			/* BCME_BUFTOOLONG */
	-EBUSY,			/* BCME_BUSY */
	-EINVAL,		/* BCME_NOTASSOCIATED */
	-EINVAL,		/* BCME_BADSSIDLEN */
	-EINVAL,		/* BCME_OUTOFRANGECHAN */
	-EINVAL,		/* BCME_BADCHAN */
	-EFAULT,		/* BCME_BADADDR */
	-ENOMEM,		/* BCME_NORESOURCE */
	-EOPNOTSUPP,		/* BCME_UNSUPPORTED */
	-EMSGSIZE,		/* BCME_BADLENGTH */
	-EINVAL,		/* BCME_NOTREADY */
	-EPERM,			/* BCME_NOTPERMITTED */
	-ENOMEM,		/* BCME_NOMEM */
	-EINVAL,		/* BCME_ASSOCIATED */
	-ERANGE,		/* BCME_RANGE */
	-EINVAL,		/* BCME_NOTFOUND */
	-EINVAL,		/* BCME_WME_NOT_ENABLED */
	-EINVAL,		/* BCME_TSPEC_NOTFOUND */
	-EINVAL,		/* BCME_ACM_NOTSUPPORTED */
	-EINVAL,		/* BCME_NOT_WME_ASSOCIATION */
	-EIO,			/* BCME_SDIO_ERROR */
	-ENODEV,		/* BCME_DONGLE_DOWN */
	-EINVAL,		/* BCME_VERSION */
	-EIO,			/* BCME_TXFAIL */
	-EIO,			/* BCME_RXFAIL */
	-EINVAL,		/* BCME_NODEVICE */
	-EINVAL,		/* BCME_NMODE_DISABLED */
	-ENODATA,		/* BCME_NONRESIDENT */

/* When an new error code is added to bcmutils.h, add os
 * spcecific error translation here as well
 */
/* check if BCME_LAST changed since the last time this function was updated */
#if BCME_LAST != -42
#error "You need to add a OS error translation in the linuxbcmerrormap \
	for new error code defined in bcmutils.h"
#endif
};

/* translate bcmerrors into linux errors */
int osl_error(int bcmerror)
{
	if (bcmerror > 0)
		bcmerror = 0;
	else if (bcmerror < BCME_LAST)
		bcmerror = BCME_ERROR;

	/* Array bounds covered by ASSERT in osl_attach */
	return linuxbcmerrormap[-bcmerror];
}

osl_t *osl_attach(void *pdev, uint bustype, bool pkttag)
{
	osl_t *osh;

	osh = kmalloc(sizeof(osl_t), GFP_ATOMIC);
	ASSERT(osh);

	bzero(osh, sizeof(osl_t));

	/* Check that error map has the right number of entries in it */
	ASSERT(ABS(BCME_LAST) == (ARRAYSIZE(linuxbcmerrormap) - 1));

	osh->magic = OS_HANDLE_MAGIC;
	osh->malloced = 0;
	osh->failed = 0;
	osh->dbgmem_list = NULL;
	osh->pdev = pdev;
	osh->pub.pkttag = pkttag;
	osh->bustype = bustype;

	switch (bustype) {
	case PCI_BUS:
	case SI_BUS:
		osh->pub.mmbus = TRUE;
		break;
	case JTAG_BUS:
	case SDIO_BUS:
	case USB_BUS:
	case SPI_BUS:
	case RPC_BUS:
		osh->pub.mmbus = FALSE;
		break;
	default:
		ASSERT(FALSE);
		break;
	}

#ifdef BCMDBG
	if (pkttag) {
		struct sk_buff *skb;
		ASSERT(OSL_PKTTAG_SZ <= sizeof(skb->cb));
	}
#endif
	return osh;
}

void osl_detach(osl_t *osh)
{
	if (osh == NULL)
		return;

	ASSERT(osh->magic == OS_HANDLE_MAGIC);
	kfree(osh);
}

/* Return a new packet. zero out pkttag */
void *BCMFASTPATH osl_pktget(osl_t *osh, uint len)
{
	struct sk_buff *skb;

	skb = dev_alloc_skb(len);
	if (skb) {
		skb_put(skb, len);
		skb->priority = 0;

		osh->pub.pktalloced++;
	}

	return (void *)skb;
}

/* Free the driver packet. Free the tag if present */
void BCMFASTPATH osl_pktfree(osl_t *osh, void *p, bool send)
{
	struct sk_buff *skb, *nskb;
	int nest = 0;

	skb = (struct sk_buff *)p;
	ASSERT(skb);

	if (send && osh->pub.tx_fn)
		osh->pub.tx_fn(osh->pub.tx_ctx, p, 0);

	/* perversion: we use skb->next to chain multi-skb packets */
	while (skb) {
		nskb = skb->next;
		skb->next = NULL;

		if (skb->destructor)
			/* cannot kfree_skb() on hard IRQ (net/core/skbuff.c) if
			 * destructor exists
			 */
			dev_kfree_skb_any(skb);
		else
			/* can free immediately (even in_irq()) if destructor
			 * does not exist
			 */
			dev_kfree_skb(skb);

		osh->pub.pktalloced--;
		nest++;
		skb = nskb;
	}
}

uint32 osl_pci_read_config(osl_t *osh, uint offset, uint size)
{
	uint val = 0;
	uint retry = PCI_CFG_RETRY;

	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));

	/* only 4byte access supported */
	ASSERT(size == 4);

	do {
		pci_read_config_dword(osh->pdev, offset, &val);
		if (val != 0xffffffff)
			break;
	} while (retry--);

#ifdef BCMDBG
	if (retry < PCI_CFG_RETRY)
		printk("PCI CONFIG READ access to %d required %d retries\n",
		       offset, (PCI_CFG_RETRY - retry));
#endif				/* BCMDBG */

	return val;
}

void osl_pci_write_config(osl_t *osh, uint offset, uint size, uint val)
{
	uint retry = PCI_CFG_RETRY;

	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));

	/* only 4byte access supported */
	ASSERT(size == 4);

	do {
		pci_write_config_dword(osh->pdev, offset, val);
		if (offset != PCI_BAR0_WIN)
			break;
		if (osl_pci_read_config(osh, offset, size) == val)
			break;
	} while (retry--);

#ifdef BCMDBG
	if (retry < PCI_CFG_RETRY)
		printk("PCI CONFIG WRITE access to %d required %d retries\n",
		       offset, (PCI_CFG_RETRY - retry));
#endif				/* BCMDBG */
}

/* return bus # for the pci device pointed by osh->pdev */
uint osl_pci_bus(osl_t *osh)
{
	ASSERT(osh && (osh->magic == OS_HANDLE_MAGIC) && osh->pdev);

	return ((struct pci_dev *)osh->pdev)->bus->number;
}

/* return slot # for the pci device pointed by osh->pdev */
uint osl_pci_slot(osl_t *osh)
{
	ASSERT(osh && (osh->magic == OS_HANDLE_MAGIC) && osh->pdev);

	return PCI_SLOT(((struct pci_dev *)osh->pdev)->devfn);
}

static void
osl_pcmcia_attr(osl_t *osh, uint offset, char *buf, int size, bool write)
{
}

void osl_pcmcia_read_attr(osl_t *osh, uint offset, void *buf, int size)
{
	osl_pcmcia_attr(osh, offset, (char *)buf, size, FALSE);
}

void osl_pcmcia_write_attr(osl_t *osh, uint offset, void *buf, int size)
{
	osl_pcmcia_attr(osh, offset, (char *)buf, size, TRUE);
}

void *osl_malloc(osl_t *osh, uint size)
{
	void *addr;

	/* only ASSERT if osh is defined */
	if (osh)
		ASSERT(osh->magic == OS_HANDLE_MAGIC);

	addr = kmalloc(size, GFP_ATOMIC);
	if (addr == NULL) {
		if (osh)
			osh->failed++;
		return NULL;
	}
	if (osh)
		osh->malloced += size;

	return addr;
}

void osl_mfree(osl_t *osh, void *addr, uint size)
{
	if (osh) {
		ASSERT(osh->magic == OS_HANDLE_MAGIC);
		osh->malloced -= size;
	}
	kfree(addr);
}

uint osl_malloced(osl_t *osh)
{
	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));
	return osh->malloced;
}

uint osl_malloc_failed(osl_t *osh)
{
	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));
	return osh->failed;
}

uint osl_dma_consistent_align(void)
{
	return PAGE_SIZE;
}

void *osl_dma_alloc_consistent(osl_t *osh, uint size, uint16 align_bits,
			       uint *alloced, unsigned long *pap)
{
	uint16 align = (1 << align_bits);
	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));

	if (!ISALIGNED(DMA_CONSISTENT_ALIGN, align))
		size += align;
	*alloced = size;

	return pci_alloc_consistent(osh->pdev, size, (dma_addr_t *) pap);
}

void osl_dma_free_consistent(osl_t *osh, void *va, uint size, unsigned long pa)
{
	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));

	pci_free_consistent(osh->pdev, size, va, (dma_addr_t) pa);
}

uint BCMFASTPATH osl_dma_map(osl_t *osh, void *va, uint size, int direction)
{
	int dir;

	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));
	dir = (direction == DMA_TX) ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE;
	return pci_map_single(osh->pdev, va, size, dir);
}

void BCMFASTPATH osl_dma_unmap(osl_t *osh, uint pa, uint size, int direction)
{
	int dir;

	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));
	dir = (direction == DMA_TX) ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE;
	pci_unmap_single(osh->pdev, (uint32) pa, size, dir);
}

#if defined(BCMDBG_ASSERT)
void osl_assert(char *exp, char *file, int line)
{
	char tempbuf[256];
	char *basename;

	basename = strrchr(file, '/');
	/* skip the '/' */
	if (basename)
		basename++;

	if (!basename)
		basename = file;

#ifdef BCMDBG_ASSERT
	snprintf(tempbuf, 256,
		 "assertion \"%s\" failed: file \"%s\", line %d\n", exp,
		 basename, line);

	/* Print assert message and give it time to be written to /var/log/messages */
	if (!in_interrupt()) {
		const int delay = 3;
		printk("%s", tempbuf);
		printk("panic in %d seconds\n", delay);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(delay * HZ);
	}

	switch (g_assert_type) {
	case 0:
		panic("%s", tempbuf);
		break;
	case 1:
		printk("%s", tempbuf);
		BUG();
		break;
	case 2:
		printk("%s", tempbuf);
		break;
	default:
		break;
	}
#endif				/* BCMDBG_ASSERT */

}
#endif				/* defined(BCMDBG_ASSERT) */

void osl_delay(uint usec)
{
	uint d;

	while (usec > 0) {
		d = MIN(usec, 1000);
		udelay(d);
		usec -= d;
	}
}

/* Clone a packet.
 * The pkttag contents are NOT cloned.
 */
void *osl_pktdup(osl_t *osh, void *skb)
{
	void *p;

	p = skb_clone((struct sk_buff *)skb, GFP_ATOMIC);
	if (p == NULL)
		return NULL;

	/* skb_clone copies skb->cb.. we don't want that */
	if (osh->pub.pkttag)
		bzero((void *)((struct sk_buff *)p)->cb, OSL_PKTTAG_SZ);

	/* Increment the packet counter */
	osh->pub.pktalloced++;
	return p;
}

#ifdef BCMSDIO
u8 osl_readb(osl_t *osh, volatile u8 *r)
{
	osl_rreg_fn_t rreg = ((osl_pubinfo_t *) osh)->rreg_fn;
	void *ctx = ((osl_pubinfo_t *) osh)->reg_ctx;

	return (u8) ((rreg) (ctx, (void *)r, sizeof(u8)));
}

uint16 osl_readw(osl_t *osh, volatile uint16 *r)
{
	osl_rreg_fn_t rreg = ((osl_pubinfo_t *) osh)->rreg_fn;
	void *ctx = ((osl_pubinfo_t *) osh)->reg_ctx;

	return (uint16) ((rreg) (ctx, (void *)r, sizeof(uint16)));
}

uint32 osl_readl(osl_t *osh, volatile uint32 *r)
{
	osl_rreg_fn_t rreg = ((osl_pubinfo_t *) osh)->rreg_fn;
	void *ctx = ((osl_pubinfo_t *) osh)->reg_ctx;

	return (uint32) ((rreg) (ctx, (void *)r, sizeof(uint32)));
}

void osl_writeb(osl_t *osh, volatile u8 *r, u8 v)
{
	osl_wreg_fn_t wreg = ((osl_pubinfo_t *) osh)->wreg_fn;
	void *ctx = ((osl_pubinfo_t *) osh)->reg_ctx;

	((wreg) (ctx, (void *)r, v, sizeof(u8)));
}

void osl_writew(osl_t *osh, volatile uint16 *r, uint16 v)
{
	osl_wreg_fn_t wreg = ((osl_pubinfo_t *) osh)->wreg_fn;
	void *ctx = ((osl_pubinfo_t *) osh)->reg_ctx;

	((wreg) (ctx, (void *)r, v, sizeof(uint16)));
}

void osl_writel(osl_t *osh, volatile uint32 *r, uint32 v)
{
	osl_wreg_fn_t wreg = ((osl_pubinfo_t *) osh)->wreg_fn;
	void *ctx = ((osl_pubinfo_t *) osh)->reg_ctx;

	((wreg) (ctx, (void *)r, v, sizeof(uint32)));
}
#endif				/* BCMSDIO */
/* Linux Kernel: File Operations: end */
