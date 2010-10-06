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

#include <linux/version.h>

#define LINUX_OSL
#include <linux/sched.h>
#include <typedefs.h>
#include <bcmendian.h>
#include <linuxver.h>
#include <bcmdefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <linux/delay.h>
#include <pcicfg.h>

#define PCI_CFG_RETRY	10

#define OS_HANDLE_MAGIC	0x1234abcd
#define BCM_MEM_FILENAME_LEN 	24

#ifdef DHD_USE_STATIC_BUF
#define MAX_STATIC_BUF_NUM 16
#define STATIC_BUF_SIZE	(PAGE_SIZE*2)
#define STATIC_BUF_TOTAL_LEN (MAX_STATIC_BUF_NUM*STATIC_BUF_SIZE)
typedef struct bcm_static_buf {
	struct semaphore static_sem;
	unsigned char *buf_ptr;
	unsigned char buf_use[MAX_STATIC_BUF_NUM];
} bcm_static_buf_t;

static bcm_static_buf_t *bcm_static_buf = 0;

#define MAX_STATIC_PKT_NUM 8
typedef struct bcm_static_pkt {
	struct sk_buff *skb_4k[MAX_STATIC_PKT_NUM];
	struct sk_buff *skb_8k[MAX_STATIC_PKT_NUM];
	struct semaphore osl_pkt_sem;
	unsigned char pkt_use[MAX_STATIC_PKT_NUM * 2];
} bcm_static_pkt_t;
static bcm_static_pkt_t *bcm_static_skb = 0;
#endif				/* DHD_USE_STATIC_BUF */

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

static int16 linuxbcmerrormap[] = { 0,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-E2BIG,
	-E2BIG,
	-EBUSY,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EFAULT,
	-ENOMEM,
	-EOPNOTSUPP,
	-EMSGSIZE,
	-EINVAL,
	-EPERM,
	-ENOMEM,
	-EINVAL,
	-ERANGE,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EIO,
	-ENODEV,
	-EINVAL,
	-EIO,
	-EIO,
	-EINVAL,
	-EINVAL,
	-ENODATA,

#if BCME_LAST != BCME_NONRESIDENT
#error "You need to add a OS error translation in the linuxbcmerrormap \
	for new error code defined in bcmutils.h"
#endif
};

/* Global ASSERT type flag */
uint32 g_assert_type = 0;

int osl_error(int bcmerror)
{
	if (bcmerror > 0)
		bcmerror = 0;
	else if (bcmerror < BCME_LAST)
		bcmerror = BCME_ERROR;

	return linuxbcmerrormap[-bcmerror];
}

void *dhd_os_prealloc(int section, unsigned long size);
osl_t *osl_attach(void *pdev, uint bustype, bool pkttag)
{
	osl_t *osh;

	osh = kmalloc(sizeof(osl_t), GFP_ATOMIC);
	ASSERT(osh);

	bzero(osh, sizeof(osl_t));

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
	case PCMCIA_BUS:
		osh->pub.mmbus = TRUE;
		break;
	case JTAG_BUS:
	case SDIO_BUS:
	case USB_BUS:
	case SPI_BUS:
		osh->pub.mmbus = FALSE;
		break;
	default:
		ASSERT(FALSE);
		break;
	}

#ifdef DHD_USE_STATIC_BUF

	if (!bcm_static_buf) {
		bcm_static_buf = (bcm_static_buf_t *) dhd_os_prealloc(3,
					STATIC_BUF_SIZE + STATIC_BUF_TOTAL_LEN);
		if (!bcm_static_buf) {
			printk(KERN_ERR "can not alloc static buf!\n");
		} else
			printk(KERN_ERR "alloc static buf at %x!\n",
			       (unsigned int)bcm_static_buf);

		init_MUTEX(&bcm_static_buf->static_sem);

		bcm_static_buf->buf_ptr =
		    (unsigned char *)bcm_static_buf + STATIC_BUF_SIZE;

	}

	if (!bcm_static_skb) {
		int i;
		void *skb_buff_ptr = 0;
		bcm_static_skb =
		    (bcm_static_pkt_t *) ((char *)bcm_static_buf + 2048);
		skb_buff_ptr = dhd_os_prealloc(4, 0);

		bcopy(skb_buff_ptr, bcm_static_skb,
		      sizeof(struct sk_buff *) * 16);
		for (i = 0; i < MAX_STATIC_PKT_NUM * 2; i++)
			bcm_static_skb->pkt_use[i] = 0;

		init_MUTEX(&bcm_static_skb->osl_pkt_sem);
	}
#endif				/* DHD_USE_STATIC_BUF */
	return osh;
}

void osl_detach(osl_t *osh)
{
	if (osh == NULL)
		return;

#ifdef DHD_USE_STATIC_BUF
	if (bcm_static_buf)
		bcm_static_buf = 0;

	if (bcm_static_skb)
		bcm_static_skb = 0;
#endif
	ASSERT(osh->magic == OS_HANDLE_MAGIC);
	kfree(osh);
}

void *osl_pktget(osl_t *osh, uint len)
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

void osl_pktfree(osl_t *osh, void *p, bool send)
{
	struct sk_buff *skb, *nskb;

	skb = (struct sk_buff *)p;

	if (send && osh->pub.tx_fn)
		osh->pub.tx_fn(osh->pub.tx_ctx, p, 0);

	while (skb) {
		nskb = skb->next;
		skb->next = NULL;

		if (skb->destructor)
			dev_kfree_skb_any(skb);
		else
			dev_kfree_skb(skb);

		osh->pub.pktalloced--;

		skb = nskb;
	}
}

#ifdef DHD_USE_STATIC_BUF
void *osl_pktget_static(osl_t *osh, uint len)
{
	int i = 0;
	struct sk_buff *skb;

	if (len > (PAGE_SIZE * 2)) {
		printk(KERN_ERR "Do we really need this big skb??\n");
		return osl_pktget(osh, len);
	}

	down(&bcm_static_skb->osl_pkt_sem);
	if (len <= PAGE_SIZE) {
		for (i = 0; i < MAX_STATIC_PKT_NUM; i++) {
			if (bcm_static_skb->pkt_use[i] == 0)
				break;
		}

		if (i != MAX_STATIC_PKT_NUM) {
			bcm_static_skb->pkt_use[i] = 1;
			up(&bcm_static_skb->osl_pkt_sem);

			skb = bcm_static_skb->skb_4k[i];
			skb->tail = skb->data + len;
			skb->len = len;

			return skb;
		}
	}

	for (i = 0; i < MAX_STATIC_PKT_NUM; i++) {
		if (bcm_static_skb->pkt_use[i + MAX_STATIC_PKT_NUM] == 0)
			break;
	}

	if (i != MAX_STATIC_PKT_NUM) {
		bcm_static_skb->pkt_use[i + MAX_STATIC_PKT_NUM] = 1;
		up(&bcm_static_skb->osl_pkt_sem);
		skb = bcm_static_skb->skb_8k[i];
		skb->tail = skb->data + len;
		skb->len = len;

		return skb;
	}

	up(&bcm_static_skb->osl_pkt_sem);
	printk(KERN_ERR "all static pkt in use!\n");
	return osl_pktget(osh, len);
}

void osl_pktfree_static(osl_t *osh, void *p, bool send)
{
	int i;

	for (i = 0; i < MAX_STATIC_PKT_NUM * 2; i++) {
		if (p == bcm_static_skb->skb_4k[i]) {
			down(&bcm_static_skb->osl_pkt_sem);
			bcm_static_skb->pkt_use[i] = 0;
			up(&bcm_static_skb->osl_pkt_sem);

			return;
		}
	}
	return osl_pktfree(osh, p, send);
}
#endif				/* DHD_USE_STATIC_BUF */

uint32 osl_pci_read_config(osl_t *osh, uint offset, uint size)
{
	uint val = 0;
	uint retry = PCI_CFG_RETRY;

	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));
	ASSERT(size == 4);

	do {
		pci_read_config_dword(osh->pdev, offset, &val);
		if (val != 0xffffffff)
			break;
	} while (retry--);

	return val;
}

void osl_pci_write_config(osl_t *osh, uint offset, uint size, uint val)
{
	uint retry = PCI_CFG_RETRY;

	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));
	ASSERT(size == 4);

	do {
		pci_write_config_dword(osh->pdev, offset, val);
		if (offset != PCI_BAR0_WIN)
			break;
		if (osl_pci_read_config(osh, offset, size) == val)
			break;
	} while (retry--);

}

uint osl_pci_bus(osl_t *osh)
{
	ASSERT(osh && (osh->magic == OS_HANDLE_MAGIC) && osh->pdev);

	return ((struct pci_dev *)osh->pdev)->bus->number;
}

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

	if (osh)
		ASSERT(osh->magic == OS_HANDLE_MAGIC);

#ifdef DHD_USE_STATIC_BUF
	if (bcm_static_buf) {
		int i = 0;
		if ((size >= PAGE_SIZE) && (size <= STATIC_BUF_SIZE)) {
			down(&bcm_static_buf->static_sem);
			for (i = 0; i < MAX_STATIC_BUF_NUM; i++) {
				if (bcm_static_buf->buf_use[i] == 0)
					break;
			}
			if (i == MAX_STATIC_BUF_NUM) {
				up(&bcm_static_buf->static_sem);
				printk(KERN_ERR "all static buff in use!\n");
				goto original;
			}
			bcm_static_buf->buf_use[i] = 1;
			up(&bcm_static_buf->static_sem);

			bzero(bcm_static_buf->buf_ptr + STATIC_BUF_SIZE * i,
			      size);
			if (osh)
				osh->malloced += size;

			return (void *)(bcm_static_buf->buf_ptr +
					 STATIC_BUF_SIZE * i);
		}
	}
original:
#endif				/* DHD_USE_STATIC_BUF */

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
#ifdef DHD_USE_STATIC_BUF
	if (bcm_static_buf) {
		if ((addr > (void *)bcm_static_buf) && ((unsigned char *)addr
				<= ((unsigned char *)
				    bcm_static_buf +
				    STATIC_BUF_TOTAL_LEN))) {
			int buf_idx = 0;
			buf_idx =
			    ((unsigned char *)addr -
			     bcm_static_buf->buf_ptr) / STATIC_BUF_SIZE;
			down(&bcm_static_buf->static_sem);
			bcm_static_buf->buf_use[buf_idx] = 0;
			up(&bcm_static_buf->static_sem);

			if (osh) {
				ASSERT(osh->magic == OS_HANDLE_MAGIC);
				osh->malloced -= size;
			}
			return;
		}
	}
#endif				/* DHD_USE_STATIC_BUF */
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

void *osl_dma_alloc_consistent(osl_t *osh, uint size, unsigned long *pap)
{
	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));

	return pci_alloc_consistent(osh->pdev, size, (dma_addr_t *) pap);
}

void osl_dma_free_consistent(osl_t *osh, void *va, uint size, unsigned long pa)
{
	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));

	pci_free_consistent(osh->pdev, size, va, (dma_addr_t) pa);
}

uint osl_dma_map(osl_t *osh, void *va, uint size, int direction)
{
	int dir;

	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));
	dir = (direction == DMA_TX) ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE;
	return pci_map_single(osh->pdev, va, size, dir);
}

void osl_dma_unmap(osl_t *osh, uint pa, uint size, int direction)
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

	/* Print assert message and give it time to be written
		 to /var/log/messages */
	if (!in_interrupt()) {
		const int delay = 3;
		printk(KERN_ERR "%s", tempbuf);
		printk(KERN_ERR "panic in %d seconds\n", delay);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(delay * HZ);
	}

	switch (g_assert_type) {
	case 0:
		panic(KERN_ERR "%s", tempbuf);
		break;
	case 1:
		printk(KERN_ERR "%s", tempbuf);
		BUG();
		break;
	case 2:
		printk(KERN_ERR "%s", tempbuf);
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

void *osl_pktdup(osl_t *osh, void *skb)
{
	void *p;

	p = skb_clone((struct sk_buff *)skb, GFP_ATOMIC);
	if (p == NULL)
		return NULL;

	if (osh->pub.pkttag)
		bzero((void *)((struct sk_buff *)p)->cb, OSL_PKTTAG_SZ);

	osh->pub.pktalloced++;
	return p;
}
