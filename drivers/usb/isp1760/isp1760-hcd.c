/*
 * Driver for the NXP ISP1760 chip
 *
 * However, the code might contain some bugs. What doesn't work for sure is:
 * - ISO
 * - OTG
 e The interrupt line is configured as active low, level.
 *
 * (c) 2007 Sebastian Siewior <bigeasy@linutronix.de>
 *
 * (c) 2011 Arvid Brodin <arvid.brodin@enea.com>
 *
 */
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <asm/unaligned.h>
#include <asm/cacheflush.h>

#include "isp1760-core.h"
#include "isp1760-hcd.h"
#include "isp1760-regs.h"

static struct kmem_cache *qtd_cachep;
static struct kmem_cache *qh_cachep;
static struct kmem_cache *urb_listitem_cachep;

typedef void (packet_enqueue)(struct usb_hcd *hcd, struct isp1760_qh *qh,
		struct isp1760_qtd *qtd);

static inline struct isp1760_hcd *hcd_to_priv(struct usb_hcd *hcd)
{
	return *(struct isp1760_hcd **)hcd->hcd_priv;
}

/* urb state*/
#define DELETE_URB		(0x0008)
#define NO_TRANSFER_ACTIVE	(0xffffffff)

/* Philips Proprietary Transfer Descriptor (PTD) */
typedef __u32 __bitwise __dw;
struct ptd {
	__dw dw0;
	__dw dw1;
	__dw dw2;
	__dw dw3;
	__dw dw4;
	__dw dw5;
	__dw dw6;
	__dw dw7;
};
#define PTD_OFFSET		0x0400
#define ISO_PTD_OFFSET		0x0400
#define INT_PTD_OFFSET		0x0800
#define ATL_PTD_OFFSET		0x0c00
#define PAYLOAD_OFFSET		0x1000


/* ATL */
/* DW0 */
#define DW0_VALID_BIT			1
#define FROM_DW0_VALID(x)		((x) & 0x01)
#define TO_DW0_LENGTH(x)		(((u32) x) << 3)
#define TO_DW0_MAXPACKET(x)		(((u32) x) << 18)
#define TO_DW0_MULTI(x)			(((u32) x) << 29)
#define TO_DW0_ENDPOINT(x)		(((u32)	x) << 31)
/* DW1 */
#define TO_DW1_DEVICE_ADDR(x)		(((u32) x) << 3)
#define TO_DW1_PID_TOKEN(x)		(((u32) x) << 10)
#define DW1_TRANS_BULK			((u32) 2 << 12)
#define DW1_TRANS_INT			((u32) 3 << 12)
#define DW1_TRANS_SPLIT			((u32) 1 << 14)
#define DW1_SE_USB_LOSPEED		((u32) 2 << 16)
#define TO_DW1_PORT_NUM(x)		(((u32) x) << 18)
#define TO_DW1_HUB_NUM(x)		(((u32) x) << 25)
/* DW2 */
#define TO_DW2_DATA_START_ADDR(x)	(((u32) x) << 8)
#define TO_DW2_RL(x)			((x) << 25)
#define FROM_DW2_RL(x)			(((x) >> 25) & 0xf)
/* DW3 */
#define FROM_DW3_NRBYTESTRANSFERRED(x)		((x) & 0x7fff)
#define FROM_DW3_SCS_NRBYTESTRANSFERRED(x)	((x) & 0x07ff)
#define TO_DW3_NAKCOUNT(x)		((x) << 19)
#define FROM_DW3_NAKCOUNT(x)		(((x) >> 19) & 0xf)
#define TO_DW3_CERR(x)			((x) << 23)
#define FROM_DW3_CERR(x)		(((x) >> 23) & 0x3)
#define TO_DW3_DATA_TOGGLE(x)		((x) << 25)
#define FROM_DW3_DATA_TOGGLE(x)		(((x) >> 25) & 0x1)
#define TO_DW3_PING(x)			((x) << 26)
#define FROM_DW3_PING(x)		(((x) >> 26) & 0x1)
#define DW3_ERROR_BIT			(1 << 28)
#define DW3_BABBLE_BIT			(1 << 29)
#define DW3_HALT_BIT			(1 << 30)
#define DW3_ACTIVE_BIT			(1 << 31)
#define FROM_DW3_ACTIVE(x)		(((x) >> 31) & 0x01)

#define INT_UNDERRUN			(1 << 2)
#define INT_BABBLE			(1 << 1)
#define INT_EXACT			(1 << 0)

#define SETUP_PID	(2)
#define IN_PID		(1)
#define OUT_PID		(0)

/* Errata 1 */
#define RL_COUNTER	(0)
#define NAK_COUNTER	(0)
#define ERR_COUNTER	(2)

struct isp1760_qtd {
	u8 packet_type;
	void *data_buffer;
	u32 payload_addr;

	/* the rest is HCD-private */
	struct list_head qtd_list;
	struct urb *urb;
	size_t length;
	size_t actual_length;

	/* QTD_ENQUEUED:	waiting for transfer (inactive) */
	/* QTD_PAYLOAD_ALLOC:	chip mem has been allocated for payload */
	/* QTD_XFER_STARTED:	valid ptd has been written to isp176x - only
				interrupt handler may touch this qtd! */
	/* QTD_XFER_COMPLETE:	payload has been transferred successfully */
	/* QTD_RETIRE:		transfer error/abort qtd */
#define QTD_ENQUEUED		0
#define QTD_PAYLOAD_ALLOC	1
#define QTD_XFER_STARTED	2
#define QTD_XFER_COMPLETE	3
#define QTD_RETIRE		4
	u32 status;
};

/* Queue head, one for each active endpoint */
struct isp1760_qh {
	struct list_head qh_list;
	struct list_head qtd_list;
	u32 toggle;
	u32 ping;
	int slot;
	int tt_buffer_dirty;	/* See USB2.0 spec section 11.17.5 */
};

struct urb_listitem {
	struct list_head urb_list;
	struct urb *urb;
};

/*
 * Access functions for isp176x registers (addresses 0..0x03FF).
 */
static u32 reg_read32(void __iomem *base, u32 reg)
{
	return isp1760_read32(base, reg);
}

static void reg_write32(void __iomem *base, u32 reg, u32 val)
{
	isp1760_write32(base, reg, val);
}

/*
 * Access functions for isp176x memory (offset >= 0x0400).
 *
 * bank_reads8() reads memory locations prefetched by an earlier write to
 * HC_MEMORY_REG (see isp176x datasheet). Unless you want to do fancy multi-
 * bank optimizations, you should use the more generic mem_reads8() below.
 *
 * For access to ptd memory, use the specialized ptd_read() and ptd_write()
 * below.
 *
 * These functions copy via MMIO data to/from the device. memcpy_{to|from}io()
 * doesn't quite work because some people have to enforce 32-bit access
 */
static void bank_reads8(void __iomem *src_base, u32 src_offset, u32 bank_addr,
							__u32 *dst, u32 bytes)
{
	__u32 __iomem *src;
	u32 val;
	__u8 *src_byteptr;
	__u8 *dst_byteptr;

	src = src_base + (bank_addr | src_offset);

	if (src_offset < PAYLOAD_OFFSET) {
		while (bytes >= 4) {
			*dst = le32_to_cpu(__raw_readl(src));
			bytes -= 4;
			src++;
			dst++;
		}
	} else {
		while (bytes >= 4) {
			*dst = __raw_readl(src);
			bytes -= 4;
			src++;
			dst++;
		}
	}

	if (!bytes)
		return;

	/* in case we have 3, 2 or 1 by left. The dst buffer may not be fully
	 * allocated.
	 */
	if (src_offset < PAYLOAD_OFFSET)
		val = le32_to_cpu(__raw_readl(src));
	else
		val = __raw_readl(src);

	dst_byteptr = (void *) dst;
	src_byteptr = (void *) &val;
	while (bytes > 0) {
		*dst_byteptr = *src_byteptr;
		dst_byteptr++;
		src_byteptr++;
		bytes--;
	}
}

static void mem_reads8(void __iomem *src_base, u32 src_offset, void *dst,
								u32 bytes)
{
	reg_write32(src_base, HC_MEMORY_REG, src_offset + ISP_BANK(0));
	ndelay(90);
	bank_reads8(src_base, src_offset, ISP_BANK(0), dst, bytes);
}

static void mem_writes8(void __iomem *dst_base, u32 dst_offset,
						__u32 const *src, u32 bytes)
{
	__u32 __iomem *dst;

	dst = dst_base + dst_offset;

	if (dst_offset < PAYLOAD_OFFSET) {
		while (bytes >= 4) {
			__raw_writel(cpu_to_le32(*src), dst);
			bytes -= 4;
			src++;
			dst++;
		}
	} else {
		while (bytes >= 4) {
			__raw_writel(*src, dst);
			bytes -= 4;
			src++;
			dst++;
		}
	}

	if (!bytes)
		return;
	/* in case we have 3, 2 or 1 bytes left. The buffer is allocated and the
	 * extra bytes should not be read by the HW.
	 */

	if (dst_offset < PAYLOAD_OFFSET)
		__raw_writel(cpu_to_le32(*src), dst);
	else
		__raw_writel(*src, dst);
}

/*
 * Read and write ptds. 'ptd_offset' should be one of ISO_PTD_OFFSET,
 * INT_PTD_OFFSET, and ATL_PTD_OFFSET. 'slot' should be less than 32.
 */
static void ptd_read(void __iomem *base, u32 ptd_offset, u32 slot,
								struct ptd *ptd)
{
	reg_write32(base, HC_MEMORY_REG,
				ISP_BANK(0) + ptd_offset + slot*sizeof(*ptd));
	ndelay(90);
	bank_reads8(base, ptd_offset + slot*sizeof(*ptd), ISP_BANK(0),
						(void *) ptd, sizeof(*ptd));
}

static void ptd_write(void __iomem *base, u32 ptd_offset, u32 slot,
								struct ptd *ptd)
{
	mem_writes8(base, ptd_offset + slot*sizeof(*ptd) + sizeof(ptd->dw0),
						&ptd->dw1, 7*sizeof(ptd->dw1));
	/* Make sure dw0 gets written last (after other dw's and after payload)
	   since it contains the enable bit */
	wmb();
	mem_writes8(base, ptd_offset + slot*sizeof(*ptd), &ptd->dw0,
							sizeof(ptd->dw0));
}


/* memory management of the 60kb on the chip from 0x1000 to 0xffff */
static void init_memory(struct isp1760_hcd *priv)
{
	int i, curr;
	u32 payload_addr;

	payload_addr = PAYLOAD_OFFSET;
	for (i = 0; i < BLOCK_1_NUM; i++) {
		priv->memory_pool[i].start = payload_addr;
		priv->memory_pool[i].size = BLOCK_1_SIZE;
		priv->memory_pool[i].free = 1;
		payload_addr += priv->memory_pool[i].size;
	}

	curr = i;
	for (i = 0; i < BLOCK_2_NUM; i++) {
		priv->memory_pool[curr + i].start = payload_addr;
		priv->memory_pool[curr + i].size = BLOCK_2_SIZE;
		priv->memory_pool[curr + i].free = 1;
		payload_addr += priv->memory_pool[curr + i].size;
	}

	curr = i;
	for (i = 0; i < BLOCK_3_NUM; i++) {
		priv->memory_pool[curr + i].start = payload_addr;
		priv->memory_pool[curr + i].size = BLOCK_3_SIZE;
		priv->memory_pool[curr + i].free = 1;
		payload_addr += priv->memory_pool[curr + i].size;
	}

	WARN_ON(payload_addr - priv->memory_pool[0].start > PAYLOAD_AREA_SIZE);
}

static void alloc_mem(struct usb_hcd *hcd, struct isp1760_qtd *qtd)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	int i;

	WARN_ON(qtd->payload_addr);

	if (!qtd->length)
		return;

	for (i = 0; i < BLOCKS; i++) {
		if (priv->memory_pool[i].size >= qtd->length &&
				priv->memory_pool[i].free) {
			priv->memory_pool[i].free = 0;
			qtd->payload_addr = priv->memory_pool[i].start;
			return;
		}
	}
}

static void free_mem(struct usb_hcd *hcd, struct isp1760_qtd *qtd)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	int i;

	if (!qtd->payload_addr)
		return;

	for (i = 0; i < BLOCKS; i++) {
		if (priv->memory_pool[i].start == qtd->payload_addr) {
			WARN_ON(priv->memory_pool[i].free);
			priv->memory_pool[i].free = 1;
			qtd->payload_addr = 0;
			return;
		}
	}

	dev_err(hcd->self.controller, "%s: Invalid pointer: %08x\n",
						__func__, qtd->payload_addr);
	WARN_ON(1);
	qtd->payload_addr = 0;
}

static int handshake(struct usb_hcd *hcd, u32 reg,
		      u32 mask, u32 done, int usec)
{
	u32 result;

	do {
		result = reg_read32(hcd->regs, reg);
		if (result == ~0)
			return -ENODEV;
		result &= mask;
		if (result == done)
			return 0;
		udelay(1);
		usec--;
	} while (usec > 0);
	return -ETIMEDOUT;
}

/* reset a non-running (STS_HALT == 1) controller */
static int ehci_reset(struct usb_hcd *hcd)
{
	int retval;
	struct isp1760_hcd *priv = hcd_to_priv(hcd);

	u32 command = reg_read32(hcd->regs, HC_USBCMD);

	command |= CMD_RESET;
	reg_write32(hcd->regs, HC_USBCMD, command);
	hcd->state = HC_STATE_HALT;
	priv->next_statechange = jiffies;
	retval = handshake(hcd, HC_USBCMD,
			    CMD_RESET, 0, 250 * 1000);
	return retval;
}

static struct isp1760_qh *qh_alloc(gfp_t flags)
{
	struct isp1760_qh *qh;

	qh = kmem_cache_zalloc(qh_cachep, flags);
	if (!qh)
		return NULL;

	INIT_LIST_HEAD(&qh->qh_list);
	INIT_LIST_HEAD(&qh->qtd_list);
	qh->slot = -1;

	return qh;
}

static void qh_free(struct isp1760_qh *qh)
{
	WARN_ON(!list_empty(&qh->qtd_list));
	WARN_ON(qh->slot > -1);
	kmem_cache_free(qh_cachep, qh);
}

/* one-time init, only for memory state */
static int priv_init(struct usb_hcd *hcd)
{
	struct isp1760_hcd		*priv = hcd_to_priv(hcd);
	u32			hcc_params;
	int i;

	spin_lock_init(&priv->lock);

	for (i = 0; i < QH_END; i++)
		INIT_LIST_HEAD(&priv->qh_list[i]);

	/*
	 * hw default: 1K periodic list heads, one per frame.
	 * periodic_size can shrink by USBCMD update if hcc_params allows.
	 */
	priv->periodic_size = DEFAULT_I_TDPS;

	/* controllers may cache some of the periodic schedule ... */
	hcc_params = reg_read32(hcd->regs, HC_HCCPARAMS);
	/* full frame cache */
	if (HCC_ISOC_CACHE(hcc_params))
		priv->i_thresh = 8;
	else /* N microframes cached */
		priv->i_thresh = 2 + HCC_ISOC_THRES(hcc_params);

	return 0;
}

static int isp1760_hc_setup(struct usb_hcd *hcd)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	int result;
	u32 scratch, hwmode;

	reg_write32(hcd->regs, HC_SCRATCH_REG, 0xdeadbabe);
	/* Change bus pattern */
	scratch = reg_read32(hcd->regs, HC_CHIP_ID_REG);
	scratch = reg_read32(hcd->regs, HC_SCRATCH_REG);
	if (scratch != 0xdeadbabe) {
		dev_err(hcd->self.controller, "Scratch test failed.\n");
		return -ENODEV;
	}

	/*
	 * The RESET_HC bit in the SW_RESET register is supposed to reset the
	 * host controller without touching the CPU interface registers, but at
	 * least on the ISP1761 it seems to behave as the RESET_ALL bit and
	 * reset the whole device. We thus can't use it here, so let's reset
	 * the host controller through the EHCI USB Command register. The device
	 * has been reset in core code anyway, so this shouldn't matter.
	 */
	reg_write32(hcd->regs, HC_BUFFER_STATUS_REG, 0);
	reg_write32(hcd->regs, HC_ATL_PTD_SKIPMAP_REG, NO_TRANSFER_ACTIVE);
	reg_write32(hcd->regs, HC_INT_PTD_SKIPMAP_REG, NO_TRANSFER_ACTIVE);
	reg_write32(hcd->regs, HC_ISO_PTD_SKIPMAP_REG, NO_TRANSFER_ACTIVE);

	result = ehci_reset(hcd);
	if (result)
		return result;

	/* Step 11 passed */

	/* ATL reset */
	hwmode = reg_read32(hcd->regs, HC_HW_MODE_CTRL) & ~ALL_ATX_RESET;
	reg_write32(hcd->regs, HC_HW_MODE_CTRL, hwmode | ALL_ATX_RESET);
	mdelay(10);
	reg_write32(hcd->regs, HC_HW_MODE_CTRL, hwmode);

	reg_write32(hcd->regs, HC_INTERRUPT_ENABLE, INTERRUPT_ENABLE_MASK);

	priv->hcs_params = reg_read32(hcd->regs, HC_HCSPARAMS);

	return priv_init(hcd);
}

static u32 base_to_chip(u32 base)
{
	return ((base - 0x400) >> 3);
}

static int last_qtd_of_urb(struct isp1760_qtd *qtd, struct isp1760_qh *qh)
{
	struct urb *urb;

	if (list_is_last(&qtd->qtd_list, &qh->qtd_list))
		return 1;

	urb = qtd->urb;
	qtd = list_entry(qtd->qtd_list.next, typeof(*qtd), qtd_list);
	return (qtd->urb != urb);
}

/* magic numbers that can affect system performance */
#define	EHCI_TUNE_CERR		3	/* 0-3 qtd retries; 0 == don't stop */
#define	EHCI_TUNE_RL_HS		4	/* nak throttle; see 4.9 */
#define	EHCI_TUNE_RL_TT		0
#define	EHCI_TUNE_MULT_HS	1	/* 1-3 transactions/uframe; 4.10.3 */
#define	EHCI_TUNE_MULT_TT	1
#define	EHCI_TUNE_FLS		2	/* (small) 256 frame schedule */

static void create_ptd_atl(struct isp1760_qh *qh,
			struct isp1760_qtd *qtd, struct ptd *ptd)
{
	u32 maxpacket;
	u32 multi;
	u32 rl = RL_COUNTER;
	u32 nak = NAK_COUNTER;

	memset(ptd, 0, sizeof(*ptd));

	/* according to 3.6.2, max packet len can not be > 0x400 */
	maxpacket = usb_maxpacket(qtd->urb->dev, qtd->urb->pipe,
						usb_pipeout(qtd->urb->pipe));
	multi =  1 + ((maxpacket >> 11) & 0x3);
	maxpacket &= 0x7ff;

	/* DW0 */
	ptd->dw0 = DW0_VALID_BIT;
	ptd->dw0 |= TO_DW0_LENGTH(qtd->length);
	ptd->dw0 |= TO_DW0_MAXPACKET(maxpacket);
	ptd->dw0 |= TO_DW0_ENDPOINT(usb_pipeendpoint(qtd->urb->pipe));

	/* DW1 */
	ptd->dw1 = usb_pipeendpoint(qtd->urb->pipe) >> 1;
	ptd->dw1 |= TO_DW1_DEVICE_ADDR(usb_pipedevice(qtd->urb->pipe));
	ptd->dw1 |= TO_DW1_PID_TOKEN(qtd->packet_type);

	if (usb_pipebulk(qtd->urb->pipe))
		ptd->dw1 |= DW1_TRANS_BULK;
	else if  (usb_pipeint(qtd->urb->pipe))
		ptd->dw1 |= DW1_TRANS_INT;

	if (qtd->urb->dev->speed != USB_SPEED_HIGH) {
		/* split transaction */

		ptd->dw1 |= DW1_TRANS_SPLIT;
		if (qtd->urb->dev->speed == USB_SPEED_LOW)
			ptd->dw1 |= DW1_SE_USB_LOSPEED;

		ptd->dw1 |= TO_DW1_PORT_NUM(qtd->urb->dev->ttport);
		ptd->dw1 |= TO_DW1_HUB_NUM(qtd->urb->dev->tt->hub->devnum);

		/* SE bit for Split INT transfers */
		if (usb_pipeint(qtd->urb->pipe) &&
				(qtd->urb->dev->speed == USB_SPEED_LOW))
			ptd->dw1 |= 2 << 16;

		rl = 0;
		nak = 0;
	} else {
		ptd->dw0 |= TO_DW0_MULTI(multi);
		if (usb_pipecontrol(qtd->urb->pipe) ||
						usb_pipebulk(qtd->urb->pipe))
			ptd->dw3 |= TO_DW3_PING(qh->ping);
	}
	/* DW2 */
	ptd->dw2 = 0;
	ptd->dw2 |= TO_DW2_DATA_START_ADDR(base_to_chip(qtd->payload_addr));
	ptd->dw2 |= TO_DW2_RL(rl);

	/* DW3 */
	ptd->dw3 |= TO_DW3_NAKCOUNT(nak);
	ptd->dw3 |= TO_DW3_DATA_TOGGLE(qh->toggle);
	if (usb_pipecontrol(qtd->urb->pipe)) {
		if (qtd->data_buffer == qtd->urb->setup_packet)
			ptd->dw3 &= ~TO_DW3_DATA_TOGGLE(1);
		else if (last_qtd_of_urb(qtd, qh))
			ptd->dw3 |= TO_DW3_DATA_TOGGLE(1);
	}

	ptd->dw3 |= DW3_ACTIVE_BIT;
	/* Cerr */
	ptd->dw3 |= TO_DW3_CERR(ERR_COUNTER);
}

static void transform_add_int(struct isp1760_qh *qh,
			struct isp1760_qtd *qtd, struct ptd *ptd)
{
	u32 usof;
	u32 period;

	/*
	 * Most of this is guessing. ISP1761 datasheet is quite unclear, and
	 * the algorithm from the original Philips driver code, which was
	 * pretty much used in this driver before as well, is quite horrendous
	 * and, i believe, incorrect. The code below follows the datasheet and
	 * USB2.0 spec as far as I can tell, and plug/unplug seems to be much
	 * more reliable this way (fingers crossed...).
	 */

	if (qtd->urb->dev->speed == USB_SPEED_HIGH) {
		/* urb->interval is in units of microframes (1/8 ms) */
		period = qtd->urb->interval >> 3;

		if (qtd->urb->interval > 4)
			usof = 0x01; /* One bit set =>
						interval 1 ms * uFrame-match */
		else if (qtd->urb->interval > 2)
			usof = 0x22; /* Two bits set => interval 1/2 ms */
		else if (qtd->urb->interval > 1)
			usof = 0x55; /* Four bits set => interval 1/4 ms */
		else
			usof = 0xff; /* All bits set => interval 1/8 ms */
	} else {
		/* urb->interval is in units of frames (1 ms) */
		period = qtd->urb->interval;
		usof = 0x0f;		/* Execute Start Split on any of the
					   four first uFrames */

		/*
		 * First 8 bits in dw5 is uSCS and "specifies which uSOF the
		 * complete split needs to be sent. Valid only for IN." Also,
		 * "All bits can be set to one for every transfer." (p 82,
		 * ISP1761 data sheet.) 0x1c is from Philips driver. Where did
		 * that number come from? 0xff seems to work fine...
		 */
		/* ptd->dw5 = 0x1c; */
		ptd->dw5 = 0xff; /* Execute Complete Split on any uFrame */
	}

	period = period >> 1;/* Ensure equal or shorter period than requested */
	period &= 0xf8; /* Mask off too large values and lowest unused 3 bits */

	ptd->dw2 |= period;
	ptd->dw4 = usof;
}

static void create_ptd_int(struct isp1760_qh *qh,
			struct isp1760_qtd *qtd, struct ptd *ptd)
{
	create_ptd_atl(qh, qtd, ptd);
	transform_add_int(qh, qtd, ptd);
}

static void isp1760_urb_done(struct usb_hcd *hcd, struct urb *urb)
__releases(priv->lock)
__acquires(priv->lock)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);

	if (!urb->unlinked) {
		if (urb->status == -EINPROGRESS)
			urb->status = 0;
	}

	if (usb_pipein(urb->pipe) && usb_pipetype(urb->pipe) != PIPE_CONTROL) {
		void *ptr;
		for (ptr = urb->transfer_buffer;
		     ptr < urb->transfer_buffer + urb->transfer_buffer_length;
		     ptr += PAGE_SIZE)
			flush_dcache_page(virt_to_page(ptr));
	}

	/* complete() can reenter this HCD */
	usb_hcd_unlink_urb_from_ep(hcd, urb);
	spin_unlock(&priv->lock);
	usb_hcd_giveback_urb(hcd, urb, urb->status);
	spin_lock(&priv->lock);
}

static struct isp1760_qtd *qtd_alloc(gfp_t flags, struct urb *urb,
								u8 packet_type)
{
	struct isp1760_qtd *qtd;

	qtd = kmem_cache_zalloc(qtd_cachep, flags);
	if (!qtd)
		return NULL;

	INIT_LIST_HEAD(&qtd->qtd_list);
	qtd->urb = urb;
	qtd->packet_type = packet_type;
	qtd->status = QTD_ENQUEUED;
	qtd->actual_length = 0;

	return qtd;
}

static void qtd_free(struct isp1760_qtd *qtd)
{
	WARN_ON(qtd->payload_addr);
	kmem_cache_free(qtd_cachep, qtd);
}

static void start_bus_transfer(struct usb_hcd *hcd, u32 ptd_offset, int slot,
				struct isp1760_slotinfo *slots,
				struct isp1760_qtd *qtd, struct isp1760_qh *qh,
				struct ptd *ptd)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	int skip_map;

	WARN_ON((slot < 0) || (slot > 31));
	WARN_ON(qtd->length && !qtd->payload_addr);
	WARN_ON(slots[slot].qtd);
	WARN_ON(slots[slot].qh);
	WARN_ON(qtd->status != QTD_PAYLOAD_ALLOC);

	/* Make sure done map has not triggered from some unlinked transfer */
	if (ptd_offset == ATL_PTD_OFFSET) {
		priv->atl_done_map |= reg_read32(hcd->regs,
						HC_ATL_PTD_DONEMAP_REG);
		priv->atl_done_map &= ~(1 << slot);
	} else {
		priv->int_done_map |= reg_read32(hcd->regs,
						HC_INT_PTD_DONEMAP_REG);
		priv->int_done_map &= ~(1 << slot);
	}

	qh->slot = slot;
	qtd->status = QTD_XFER_STARTED;
	slots[slot].timestamp = jiffies;
	slots[slot].qtd = qtd;
	slots[slot].qh = qh;
	ptd_write(hcd->regs, ptd_offset, slot, ptd);

	if (ptd_offset == ATL_PTD_OFFSET) {
		skip_map = reg_read32(hcd->regs, HC_ATL_PTD_SKIPMAP_REG);
		skip_map &= ~(1 << qh->slot);
		reg_write32(hcd->regs, HC_ATL_PTD_SKIPMAP_REG, skip_map);
	} else {
		skip_map = reg_read32(hcd->regs, HC_INT_PTD_SKIPMAP_REG);
		skip_map &= ~(1 << qh->slot);
		reg_write32(hcd->regs, HC_INT_PTD_SKIPMAP_REG, skip_map);
	}
}

static int is_short_bulk(struct isp1760_qtd *qtd)
{
	return (usb_pipebulk(qtd->urb->pipe) &&
					(qtd->actual_length < qtd->length));
}

static void collect_qtds(struct usb_hcd *hcd, struct isp1760_qh *qh,
						struct list_head *urb_list)
{
	int last_qtd;
	struct isp1760_qtd *qtd, *qtd_next;
	struct urb_listitem *urb_listitem;

	list_for_each_entry_safe(qtd, qtd_next, &qh->qtd_list, qtd_list) {
		if (qtd->status < QTD_XFER_COMPLETE)
			break;

		last_qtd = last_qtd_of_urb(qtd, qh);

		if ((!last_qtd) && (qtd->status == QTD_RETIRE))
			qtd_next->status = QTD_RETIRE;

		if (qtd->status == QTD_XFER_COMPLETE) {
			if (qtd->actual_length) {
				switch (qtd->packet_type) {
				case IN_PID:
					mem_reads8(hcd->regs, qtd->payload_addr,
							qtd->data_buffer,
							qtd->actual_length);
					/* Fall through (?) */
				case OUT_PID:
					qtd->urb->actual_length +=
							qtd->actual_length;
					/* Fall through ... */
				case SETUP_PID:
					break;
				}
			}

			if (is_short_bulk(qtd)) {
				if (qtd->urb->transfer_flags & URB_SHORT_NOT_OK)
					qtd->urb->status = -EREMOTEIO;
				if (!last_qtd)
					qtd_next->status = QTD_RETIRE;
			}
		}

		if (qtd->payload_addr)
			free_mem(hcd, qtd);

		if (last_qtd) {
			if ((qtd->status == QTD_RETIRE) &&
					(qtd->urb->status == -EINPROGRESS))
				qtd->urb->status = -EPIPE;
			/* Defer calling of urb_done() since it releases lock */
			urb_listitem = kmem_cache_zalloc(urb_listitem_cachep,
								GFP_ATOMIC);
			if (unlikely(!urb_listitem))
				break; /* Try again on next call */
			urb_listitem->urb = qtd->urb;
			list_add_tail(&urb_listitem->urb_list, urb_list);
		}

		list_del(&qtd->qtd_list);
		qtd_free(qtd);
	}
}

#define ENQUEUE_DEPTH	2
static void enqueue_qtds(struct usb_hcd *hcd, struct isp1760_qh *qh)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	int ptd_offset;
	struct isp1760_slotinfo *slots;
	int curr_slot, free_slot;
	int n;
	struct ptd ptd;
	struct isp1760_qtd *qtd;

	if (unlikely(list_empty(&qh->qtd_list))) {
		WARN_ON(1);
		return;
	}

	/* Make sure this endpoint's TT buffer is clean before queueing ptds */
	if (qh->tt_buffer_dirty)
		return;

	if (usb_pipeint(list_entry(qh->qtd_list.next, struct isp1760_qtd,
							qtd_list)->urb->pipe)) {
		ptd_offset = INT_PTD_OFFSET;
		slots = priv->int_slots;
	} else {
		ptd_offset = ATL_PTD_OFFSET;
		slots = priv->atl_slots;
	}

	free_slot = -1;
	for (curr_slot = 0; curr_slot < 32; curr_slot++) {
		if ((free_slot == -1) && (slots[curr_slot].qtd == NULL))
			free_slot = curr_slot;
		if (slots[curr_slot].qh == qh)
			break;
	}

	n = 0;
	list_for_each_entry(qtd, &qh->qtd_list, qtd_list) {
		if (qtd->status == QTD_ENQUEUED) {
			WARN_ON(qtd->payload_addr);
			alloc_mem(hcd, qtd);
			if ((qtd->length) && (!qtd->payload_addr))
				break;

			if ((qtd->length) &&
			    ((qtd->packet_type == SETUP_PID) ||
			     (qtd->packet_type == OUT_PID))) {
				mem_writes8(hcd->regs, qtd->payload_addr,
						qtd->data_buffer, qtd->length);
			}

			qtd->status = QTD_PAYLOAD_ALLOC;
		}

		if (qtd->status == QTD_PAYLOAD_ALLOC) {
/*
			if ((curr_slot > 31) && (free_slot == -1))
				dev_dbg(hcd->self.controller, "%s: No slot "
					"available for transfer\n", __func__);
*/
			/* Start xfer for this endpoint if not already done */
			if ((curr_slot > 31) && (free_slot > -1)) {
				if (usb_pipeint(qtd->urb->pipe))
					create_ptd_int(qh, qtd, &ptd);
				else
					create_ptd_atl(qh, qtd, &ptd);

				start_bus_transfer(hcd, ptd_offset, free_slot,
							slots, qtd, qh, &ptd);
				curr_slot = free_slot;
			}

			n++;
			if (n >= ENQUEUE_DEPTH)
				break;
		}
	}
}

static void schedule_ptds(struct usb_hcd *hcd)
{
	struct isp1760_hcd *priv;
	struct isp1760_qh *qh, *qh_next;
	struct list_head *ep_queue;
	LIST_HEAD(urb_list);
	struct urb_listitem *urb_listitem, *urb_listitem_next;
	int i;

	if (!hcd) {
		WARN_ON(1);
		return;
	}

	priv = hcd_to_priv(hcd);

	/*
	 * check finished/retired xfers, transfer payloads, call urb_done()
	 */
	for (i = 0; i < QH_END; i++) {
		ep_queue = &priv->qh_list[i];
		list_for_each_entry_safe(qh, qh_next, ep_queue, qh_list) {
			collect_qtds(hcd, qh, &urb_list);
			if (list_empty(&qh->qtd_list))
				list_del(&qh->qh_list);
		}
	}

	list_for_each_entry_safe(urb_listitem, urb_listitem_next, &urb_list,
								urb_list) {
		isp1760_urb_done(hcd, urb_listitem->urb);
		kmem_cache_free(urb_listitem_cachep, urb_listitem);
	}

	/*
	 * Schedule packets for transfer.
	 *
	 * According to USB2.0 specification:
	 *
	 * 1st prio: interrupt xfers, up to 80 % of bandwidth
	 * 2nd prio: control xfers
	 * 3rd prio: bulk xfers
	 *
	 * ... but let's use a simpler scheme here (mostly because ISP1761 doc
	 * is very unclear on how to prioritize traffic):
	 *
	 * 1) Enqueue any queued control transfers, as long as payload chip mem
	 *    and PTD ATL slots are available.
	 * 2) Enqueue any queued INT transfers, as long as payload chip mem
	 *    and PTD INT slots are available.
	 * 3) Enqueue any queued bulk transfers, as long as payload chip mem
	 *    and PTD ATL slots are available.
	 *
	 * Use double buffering (ENQUEUE_DEPTH==2) as a compromise between
	 * conservation of chip mem and performance.
	 *
	 * I'm sure this scheme could be improved upon!
	 */
	for (i = 0; i < QH_END; i++) {
		ep_queue = &priv->qh_list[i];
		list_for_each_entry_safe(qh, qh_next, ep_queue, qh_list)
			enqueue_qtds(hcd, qh);
	}
}

#define PTD_STATE_QTD_DONE	1
#define PTD_STATE_QTD_RELOAD	2
#define PTD_STATE_URB_RETIRE	3

static int check_int_transfer(struct usb_hcd *hcd, struct ptd *ptd,
								struct urb *urb)
{
	__dw dw4;
	int i;

	dw4 = ptd->dw4;
	dw4 >>= 8;

	/* FIXME: ISP1761 datasheet does not say what to do with these. Do we
	   need to handle these errors? Is it done in hardware? */

	if (ptd->dw3 & DW3_HALT_BIT) {

		urb->status = -EPROTO; /* Default unknown error */

		for (i = 0; i < 8; i++) {
			switch (dw4 & 0x7) {
			case INT_UNDERRUN:
				dev_dbg(hcd->self.controller, "%s: underrun "
						"during uFrame %d\n",
						__func__, i);
				urb->status = -ECOMM; /* Could not write data */
				break;
			case INT_EXACT:
				dev_dbg(hcd->self.controller, "%s: transaction "
						"error during uFrame %d\n",
						__func__, i);
				urb->status = -EPROTO; /* timeout, bad CRC, PID
							  error etc. */
				break;
			case INT_BABBLE:
				dev_dbg(hcd->self.controller, "%s: babble "
						"error during uFrame %d\n",
						__func__, i);
				urb->status = -EOVERFLOW;
				break;
			}
			dw4 >>= 3;
		}

		return PTD_STATE_URB_RETIRE;
	}

	return PTD_STATE_QTD_DONE;
}

static int check_atl_transfer(struct usb_hcd *hcd, struct ptd *ptd,
								struct urb *urb)
{
	WARN_ON(!ptd);
	if (ptd->dw3 & DW3_HALT_BIT) {
		if (ptd->dw3 & DW3_BABBLE_BIT)
			urb->status = -EOVERFLOW;
		else if (FROM_DW3_CERR(ptd->dw3))
			urb->status = -EPIPE;  /* Stall */
		else if (ptd->dw3 & DW3_ERROR_BIT)
			urb->status = -EPROTO; /* XactErr */
		else
			urb->status = -EPROTO; /* Unknown */
/*
		dev_dbg(hcd->self.controller, "%s: ptd error:\n"
			"        dw0: %08x dw1: %08x dw2: %08x dw3: %08x\n"
			"        dw4: %08x dw5: %08x dw6: %08x dw7: %08x\n",
			__func__,
			ptd->dw0, ptd->dw1, ptd->dw2, ptd->dw3,
			ptd->dw4, ptd->dw5, ptd->dw6, ptd->dw7);
*/
		return PTD_STATE_URB_RETIRE;
	}

	if ((ptd->dw3 & DW3_ERROR_BIT) && (ptd->dw3 & DW3_ACTIVE_BIT)) {
		/* Transfer Error, *but* active and no HALT -> reload */
		dev_dbg(hcd->self.controller, "PID error; reloading ptd\n");
		return PTD_STATE_QTD_RELOAD;
	}

	if (!FROM_DW3_NAKCOUNT(ptd->dw3) && (ptd->dw3 & DW3_ACTIVE_BIT)) {
		/*
		 * NAKs are handled in HW by the chip. Usually if the
		 * device is not able to send data fast enough.
		 * This happens mostly on slower hardware.
		 */
		return PTD_STATE_QTD_RELOAD;
	}

	return PTD_STATE_QTD_DONE;
}

static void handle_done_ptds(struct usb_hcd *hcd)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	struct ptd ptd;
	struct isp1760_qh *qh;
	int slot;
	int state;
	struct isp1760_slotinfo *slots;
	u32 ptd_offset;
	struct isp1760_qtd *qtd;
	int modified;
	int skip_map;

	skip_map = reg_read32(hcd->regs, HC_INT_PTD_SKIPMAP_REG);
	priv->int_done_map &= ~skip_map;
	skip_map = reg_read32(hcd->regs, HC_ATL_PTD_SKIPMAP_REG);
	priv->atl_done_map &= ~skip_map;

	modified = priv->int_done_map || priv->atl_done_map;

	while (priv->int_done_map || priv->atl_done_map) {
		if (priv->int_done_map) {
			/* INT ptd */
			slot = __ffs(priv->int_done_map);
			priv->int_done_map &= ~(1 << slot);
			slots = priv->int_slots;
			/* This should not trigger, and could be removed if
			   noone have any problems with it triggering: */
			if (!slots[slot].qh) {
				WARN_ON(1);
				continue;
			}
			ptd_offset = INT_PTD_OFFSET;
			ptd_read(hcd->regs, INT_PTD_OFFSET, slot, &ptd);
			state = check_int_transfer(hcd, &ptd,
							slots[slot].qtd->urb);
		} else {
			/* ATL ptd */
			slot = __ffs(priv->atl_done_map);
			priv->atl_done_map &= ~(1 << slot);
			slots = priv->atl_slots;
			/* This should not trigger, and could be removed if
			   noone have any problems with it triggering: */
			if (!slots[slot].qh) {
				WARN_ON(1);
				continue;
			}
			ptd_offset = ATL_PTD_OFFSET;
			ptd_read(hcd->regs, ATL_PTD_OFFSET, slot, &ptd);
			state = check_atl_transfer(hcd, &ptd,
							slots[slot].qtd->urb);
		}

		qtd = slots[slot].qtd;
		slots[slot].qtd = NULL;
		qh = slots[slot].qh;
		slots[slot].qh = NULL;
		qh->slot = -1;

		WARN_ON(qtd->status != QTD_XFER_STARTED);

		switch (state) {
		case PTD_STATE_QTD_DONE:
			if ((usb_pipeint(qtd->urb->pipe)) &&
				       (qtd->urb->dev->speed != USB_SPEED_HIGH))
				qtd->actual_length =
				       FROM_DW3_SCS_NRBYTESTRANSFERRED(ptd.dw3);
			else
				qtd->actual_length =
					FROM_DW3_NRBYTESTRANSFERRED(ptd.dw3);

			qtd->status = QTD_XFER_COMPLETE;
			if (list_is_last(&qtd->qtd_list, &qh->qtd_list) ||
							is_short_bulk(qtd))
				qtd = NULL;
			else
				qtd = list_entry(qtd->qtd_list.next,
							typeof(*qtd), qtd_list);

			qh->toggle = FROM_DW3_DATA_TOGGLE(ptd.dw3);
			qh->ping = FROM_DW3_PING(ptd.dw3);
			break;

		case PTD_STATE_QTD_RELOAD: /* QTD_RETRY, for atls only */
			qtd->status = QTD_PAYLOAD_ALLOC;
			ptd.dw0 |= DW0_VALID_BIT;
			/* RL counter = ERR counter */
			ptd.dw3 &= ~TO_DW3_NAKCOUNT(0xf);
			ptd.dw3 |= TO_DW3_NAKCOUNT(FROM_DW2_RL(ptd.dw2));
			ptd.dw3 &= ~TO_DW3_CERR(3);
			ptd.dw3 |= TO_DW3_CERR(ERR_COUNTER);
			qh->toggle = FROM_DW3_DATA_TOGGLE(ptd.dw3);
			qh->ping = FROM_DW3_PING(ptd.dw3);
			break;

		case PTD_STATE_URB_RETIRE:
			qtd->status = QTD_RETIRE;
			if ((qtd->urb->dev->speed != USB_SPEED_HIGH) &&
					(qtd->urb->status != -EPIPE) &&
					(qtd->urb->status != -EREMOTEIO)) {
				qh->tt_buffer_dirty = 1;
				if (usb_hub_clear_tt_buffer(qtd->urb))
					/* Clear failed; let's hope things work
					   anyway */
					qh->tt_buffer_dirty = 0;
			}
			qtd = NULL;
			qh->toggle = 0;
			qh->ping = 0;
			break;

		default:
			WARN_ON(1);
			continue;
		}

		if (qtd && (qtd->status == QTD_PAYLOAD_ALLOC)) {
			if (slots == priv->int_slots) {
				if (state == PTD_STATE_QTD_RELOAD)
					dev_err(hcd->self.controller,
						"%s: PTD_STATE_QTD_RELOAD on "
						"interrupt packet\n", __func__);
				if (state != PTD_STATE_QTD_RELOAD)
					create_ptd_int(qh, qtd, &ptd);
			} else {
				if (state != PTD_STATE_QTD_RELOAD)
					create_ptd_atl(qh, qtd, &ptd);
			}

			start_bus_transfer(hcd, ptd_offset, slot, slots, qtd,
				qh, &ptd);
		}
	}

	if (modified)
		schedule_ptds(hcd);
}

static irqreturn_t isp1760_irq(struct usb_hcd *hcd)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	u32 imask;
	irqreturn_t irqret = IRQ_NONE;

	spin_lock(&priv->lock);

	if (!(hcd->state & HC_STATE_RUNNING))
		goto leave;

	imask = reg_read32(hcd->regs, HC_INTERRUPT_REG);
	if (unlikely(!imask))
		goto leave;
	reg_write32(hcd->regs, HC_INTERRUPT_REG, imask); /* Clear */

	priv->int_done_map |= reg_read32(hcd->regs, HC_INT_PTD_DONEMAP_REG);
	priv->atl_done_map |= reg_read32(hcd->regs, HC_ATL_PTD_DONEMAP_REG);

	handle_done_ptds(hcd);

	irqret = IRQ_HANDLED;
leave:
	spin_unlock(&priv->lock);

	return irqret;
}

/*
 * Workaround for problem described in chip errata 2:
 *
 * Sometimes interrupts are not generated when ATL (not INT?) completion occurs.
 * One solution suggested in the errata is to use SOF interrupts _instead_of_
 * ATL done interrupts (the "instead of" might be important since it seems
 * enabling ATL interrupts also causes the chip to sometimes - rarely - "forget"
 * to set the PTD's done bit in addition to not generating an interrupt!).
 *
 * So if we use SOF + ATL interrupts, we sometimes get stale PTDs since their
 * done bit is not being set. This is bad - it blocks the endpoint until reboot.
 *
 * If we use SOF interrupts only, we get latency between ptd completion and the
 * actual handling. This is very noticeable in testusb runs which takes several
 * minutes longer without ATL interrupts.
 *
 * A better solution is to run the code below every SLOT_CHECK_PERIOD ms. If it
 * finds active ATL slots which are older than SLOT_TIMEOUT ms, it checks the
 * slot's ACTIVE and VALID bits. If these are not set, the ptd is considered
 * completed and its done map bit is set.
 *
 * The values of SLOT_TIMEOUT and SLOT_CHECK_PERIOD have been arbitrarily chosen
 * not to cause too much lag when this HW bug occurs, while still hopefully
 * ensuring that the check does not falsely trigger.
 */
#define SLOT_TIMEOUT 300
#define SLOT_CHECK_PERIOD 200
static struct timer_list errata2_timer;

static void errata2_function(unsigned long data)
{
	struct usb_hcd *hcd = (struct usb_hcd *) data;
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	int slot;
	struct ptd ptd;
	unsigned long spinflags;

	spin_lock_irqsave(&priv->lock, spinflags);

	for (slot = 0; slot < 32; slot++)
		if (priv->atl_slots[slot].qh && time_after(jiffies,
					priv->atl_slots[slot].timestamp +
					SLOT_TIMEOUT * HZ / 1000)) {
			ptd_read(hcd->regs, ATL_PTD_OFFSET, slot, &ptd);
			if (!FROM_DW0_VALID(ptd.dw0) &&
					!FROM_DW3_ACTIVE(ptd.dw3))
				priv->atl_done_map |= 1 << slot;
		}

	if (priv->atl_done_map)
		handle_done_ptds(hcd);

	spin_unlock_irqrestore(&priv->lock, spinflags);

	errata2_timer.expires = jiffies + SLOT_CHECK_PERIOD * HZ / 1000;
	add_timer(&errata2_timer);
}

static int isp1760_run(struct usb_hcd *hcd)
{
	int retval;
	u32 temp;
	u32 command;
	u32 chipid;

	hcd->uses_new_polling = 1;

	hcd->state = HC_STATE_RUNNING;

	/* Set PTD interrupt AND & OR maps */
	reg_write32(hcd->regs, HC_ATL_IRQ_MASK_AND_REG, 0);
	reg_write32(hcd->regs, HC_ATL_IRQ_MASK_OR_REG, 0xffffffff);
	reg_write32(hcd->regs, HC_INT_IRQ_MASK_AND_REG, 0);
	reg_write32(hcd->regs, HC_INT_IRQ_MASK_OR_REG, 0xffffffff);
	reg_write32(hcd->regs, HC_ISO_IRQ_MASK_AND_REG, 0);
	reg_write32(hcd->regs, HC_ISO_IRQ_MASK_OR_REG, 0xffffffff);
	/* step 23 passed */

	temp = reg_read32(hcd->regs, HC_HW_MODE_CTRL);
	reg_write32(hcd->regs, HC_HW_MODE_CTRL, temp | HW_GLOBAL_INTR_EN);

	command = reg_read32(hcd->regs, HC_USBCMD);
	command &= ~(CMD_LRESET|CMD_RESET);
	command |= CMD_RUN;
	reg_write32(hcd->regs, HC_USBCMD, command);

	retval = handshake(hcd, HC_USBCMD, CMD_RUN, CMD_RUN, 250 * 1000);
	if (retval)
		return retval;

	/*
	 * XXX
	 * Spec says to write FLAG_CF as last config action, priv code grabs
	 * the semaphore while doing so.
	 */
	down_write(&ehci_cf_port_reset_rwsem);
	reg_write32(hcd->regs, HC_CONFIGFLAG, FLAG_CF);

	retval = handshake(hcd, HC_CONFIGFLAG, FLAG_CF, FLAG_CF, 250 * 1000);
	up_write(&ehci_cf_port_reset_rwsem);
	if (retval)
		return retval;

	setup_timer(&errata2_timer, errata2_function, (unsigned long)hcd);
	errata2_timer.expires = jiffies + SLOT_CHECK_PERIOD * HZ / 1000;
	add_timer(&errata2_timer);

	chipid = reg_read32(hcd->regs, HC_CHIP_ID_REG);
	dev_info(hcd->self.controller, "USB ISP %04x HW rev. %d started\n",
					chipid & 0xffff, chipid >> 16);

	/* PTD Register Init Part 2, Step 28 */

	/* Setup registers controlling PTD checking */
	reg_write32(hcd->regs, HC_ATL_PTD_LASTPTD_REG, 0x80000000);
	reg_write32(hcd->regs, HC_INT_PTD_LASTPTD_REG, 0x80000000);
	reg_write32(hcd->regs, HC_ISO_PTD_LASTPTD_REG, 0x00000001);
	reg_write32(hcd->regs, HC_ATL_PTD_SKIPMAP_REG, 0xffffffff);
	reg_write32(hcd->regs, HC_INT_PTD_SKIPMAP_REG, 0xffffffff);
	reg_write32(hcd->regs, HC_ISO_PTD_SKIPMAP_REG, 0xffffffff);
	reg_write32(hcd->regs, HC_BUFFER_STATUS_REG,
						ATL_BUF_FILL | INT_BUF_FILL);

	/* GRR this is run-once init(), being done every time the HC starts.
	 * So long as they're part of class devices, we can't do it init()
	 * since the class device isn't created that early.
	 */
	return 0;
}

static int qtd_fill(struct isp1760_qtd *qtd, void *databuffer, size_t len)
{
	qtd->data_buffer = databuffer;

	if (len > MAX_PAYLOAD_SIZE)
		len = MAX_PAYLOAD_SIZE;
	qtd->length = len;

	return qtd->length;
}

static void qtd_list_free(struct list_head *qtd_list)
{
	struct isp1760_qtd *qtd, *qtd_next;

	list_for_each_entry_safe(qtd, qtd_next, qtd_list, qtd_list) {
		list_del(&qtd->qtd_list);
		qtd_free(qtd);
	}
}

/*
 * Packetize urb->transfer_buffer into list of packets of size wMaxPacketSize.
 * Also calculate the PID type (SETUP/IN/OUT) for each packet.
 */
#define max_packet(wMaxPacketSize) ((wMaxPacketSize) & 0x07ff)
static void packetize_urb(struct usb_hcd *hcd,
		struct urb *urb, struct list_head *head, gfp_t flags)
{
	struct isp1760_qtd *qtd;
	void *buf;
	int len, maxpacketsize;
	u8 packet_type;

	/*
	 * URBs map to sequences of QTDs:  one logical transaction
	 */

	if (!urb->transfer_buffer && urb->transfer_buffer_length) {
		/* XXX This looks like usb storage / SCSI bug */
		dev_err(hcd->self.controller,
				"buf is null, dma is %08lx len is %d\n",
				(long unsigned)urb->transfer_dma,
				urb->transfer_buffer_length);
		WARN_ON(1);
	}

	if (usb_pipein(urb->pipe))
		packet_type = IN_PID;
	else
		packet_type = OUT_PID;

	if (usb_pipecontrol(urb->pipe)) {
		qtd = qtd_alloc(flags, urb, SETUP_PID);
		if (!qtd)
			goto cleanup;
		qtd_fill(qtd, urb->setup_packet, sizeof(struct usb_ctrlrequest));
		list_add_tail(&qtd->qtd_list, head);

		/* for zero length DATA stages, STATUS is always IN */
		if (urb->transfer_buffer_length == 0)
			packet_type = IN_PID;
	}

	maxpacketsize = max_packet(usb_maxpacket(urb->dev, urb->pipe,
						usb_pipeout(urb->pipe)));

	/*
	 * buffer gets wrapped in one or more qtds;
	 * last one may be "short" (including zero len)
	 * and may serve as a control status ack
	 */
	buf = urb->transfer_buffer;
	len = urb->transfer_buffer_length;

	for (;;) {
		int this_qtd_len;

		qtd = qtd_alloc(flags, urb, packet_type);
		if (!qtd)
			goto cleanup;
		this_qtd_len = qtd_fill(qtd, buf, len);
		list_add_tail(&qtd->qtd_list, head);

		len -= this_qtd_len;
		buf += this_qtd_len;

		if (len <= 0)
			break;
	}

	/*
	 * control requests may need a terminating data "status" ack;
	 * bulk ones may need a terminating short packet (zero length).
	 */
	if (urb->transfer_buffer_length != 0) {
		int one_more = 0;

		if (usb_pipecontrol(urb->pipe)) {
			one_more = 1;
			if (packet_type == IN_PID)
				packet_type = OUT_PID;
			else
				packet_type = IN_PID;
		} else if (usb_pipebulk(urb->pipe)
				&& (urb->transfer_flags & URB_ZERO_PACKET)
				&& !(urb->transfer_buffer_length %
							maxpacketsize)) {
			one_more = 1;
		}
		if (one_more) {
			qtd = qtd_alloc(flags, urb, packet_type);
			if (!qtd)
				goto cleanup;

			/* never any data in such packets */
			qtd_fill(qtd, NULL, 0);
			list_add_tail(&qtd->qtd_list, head);
		}
	}

	return;

cleanup:
	qtd_list_free(head);
}

static int isp1760_urb_enqueue(struct usb_hcd *hcd, struct urb *urb,
		gfp_t mem_flags)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	struct list_head *ep_queue;
	struct isp1760_qh *qh, *qhit;
	unsigned long spinflags;
	LIST_HEAD(new_qtds);
	int retval;
	int qh_in_queue;

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		ep_queue = &priv->qh_list[QH_CONTROL];
		break;
	case PIPE_BULK:
		ep_queue = &priv->qh_list[QH_BULK];
		break;
	case PIPE_INTERRUPT:
		if (urb->interval < 0)
			return -EINVAL;
		/* FIXME: Check bandwidth  */
		ep_queue = &priv->qh_list[QH_INTERRUPT];
		break;
	case PIPE_ISOCHRONOUS:
		dev_err(hcd->self.controller, "%s: isochronous USB packets "
							"not yet supported\n",
							__func__);
		return -EPIPE;
	default:
		dev_err(hcd->self.controller, "%s: unknown pipe type\n",
							__func__);
		return -EPIPE;
	}

	if (usb_pipein(urb->pipe))
		urb->actual_length = 0;

	packetize_urb(hcd, urb, &new_qtds, mem_flags);
	if (list_empty(&new_qtds))
		return -ENOMEM;

	retval = 0;
	spin_lock_irqsave(&priv->lock, spinflags);

	if (!test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags)) {
		retval = -ESHUTDOWN;
		qtd_list_free(&new_qtds);
		goto out;
	}
	retval = usb_hcd_link_urb_to_ep(hcd, urb);
	if (retval) {
		qtd_list_free(&new_qtds);
		goto out;
	}

	qh = urb->ep->hcpriv;
	if (qh) {
		qh_in_queue = 0;
		list_for_each_entry(qhit, ep_queue, qh_list) {
			if (qhit == qh) {
				qh_in_queue = 1;
				break;
			}
		}
		if (!qh_in_queue)
			list_add_tail(&qh->qh_list, ep_queue);
	} else {
		qh = qh_alloc(GFP_ATOMIC);
		if (!qh) {
			retval = -ENOMEM;
			usb_hcd_unlink_urb_from_ep(hcd, urb);
			qtd_list_free(&new_qtds);
			goto out;
		}
		list_add_tail(&qh->qh_list, ep_queue);
		urb->ep->hcpriv = qh;
	}

	list_splice_tail(&new_qtds, &qh->qtd_list);
	schedule_ptds(hcd);

out:
	spin_unlock_irqrestore(&priv->lock, spinflags);
	return retval;
}

static void kill_transfer(struct usb_hcd *hcd, struct urb *urb,
		struct isp1760_qh *qh)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	int skip_map;

	WARN_ON(qh->slot == -1);

	/* We need to forcefully reclaim the slot since some transfers never
	   return, e.g. interrupt transfers and NAKed bulk transfers. */
	if (usb_pipecontrol(urb->pipe) || usb_pipebulk(urb->pipe)) {
		skip_map = reg_read32(hcd->regs, HC_ATL_PTD_SKIPMAP_REG);
		skip_map |= (1 << qh->slot);
		reg_write32(hcd->regs, HC_ATL_PTD_SKIPMAP_REG, skip_map);
		priv->atl_slots[qh->slot].qh = NULL;
		priv->atl_slots[qh->slot].qtd = NULL;
	} else {
		skip_map = reg_read32(hcd->regs, HC_INT_PTD_SKIPMAP_REG);
		skip_map |= (1 << qh->slot);
		reg_write32(hcd->regs, HC_INT_PTD_SKIPMAP_REG, skip_map);
		priv->int_slots[qh->slot].qh = NULL;
		priv->int_slots[qh->slot].qtd = NULL;
	}

	qh->slot = -1;
}

/*
 * Retire the qtds beginning at 'qtd' and belonging all to the same urb, killing
 * any active transfer belonging to the urb in the process.
 */
static void dequeue_urb_from_qtd(struct usb_hcd *hcd, struct isp1760_qh *qh,
						struct isp1760_qtd *qtd)
{
	struct urb *urb;
	int urb_was_running;

	urb = qtd->urb;
	urb_was_running = 0;
	list_for_each_entry_from(qtd, &qh->qtd_list, qtd_list) {
		if (qtd->urb != urb)
			break;

		if (qtd->status >= QTD_XFER_STARTED)
			urb_was_running = 1;
		if (last_qtd_of_urb(qtd, qh) &&
					(qtd->status >= QTD_XFER_COMPLETE))
			urb_was_running = 0;

		if (qtd->status == QTD_XFER_STARTED)
			kill_transfer(hcd, urb, qh);
		qtd->status = QTD_RETIRE;
	}

	if ((urb->dev->speed != USB_SPEED_HIGH) && urb_was_running) {
		qh->tt_buffer_dirty = 1;
		if (usb_hub_clear_tt_buffer(urb))
			/* Clear failed; let's hope things work anyway */
			qh->tt_buffer_dirty = 0;
	}
}

static int isp1760_urb_dequeue(struct usb_hcd *hcd, struct urb *urb,
		int status)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	unsigned long spinflags;
	struct isp1760_qh *qh;
	struct isp1760_qtd *qtd;
	int retval = 0;

	spin_lock_irqsave(&priv->lock, spinflags);
	retval = usb_hcd_check_unlink_urb(hcd, urb, status);
	if (retval)
		goto out;

	qh = urb->ep->hcpriv;
	if (!qh) {
		retval = -EINVAL;
		goto out;
	}

	list_for_each_entry(qtd, &qh->qtd_list, qtd_list)
		if (qtd->urb == urb) {
			dequeue_urb_from_qtd(hcd, qh, qtd);
			list_move(&qtd->qtd_list, &qh->qtd_list);
			break;
		}

	urb->status = status;
	schedule_ptds(hcd);

out:
	spin_unlock_irqrestore(&priv->lock, spinflags);
	return retval;
}

static void isp1760_endpoint_disable(struct usb_hcd *hcd,
		struct usb_host_endpoint *ep)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	unsigned long spinflags;
	struct isp1760_qh *qh, *qh_iter;
	int i;

	spin_lock_irqsave(&priv->lock, spinflags);

	qh = ep->hcpriv;
	if (!qh)
		goto out;

	WARN_ON(!list_empty(&qh->qtd_list));

	for (i = 0; i < QH_END; i++)
		list_for_each_entry(qh_iter, &priv->qh_list[i], qh_list)
			if (qh_iter == qh) {
				list_del(&qh_iter->qh_list);
				i = QH_END;
				break;
			}
	qh_free(qh);
	ep->hcpriv = NULL;

	schedule_ptds(hcd);

out:
	spin_unlock_irqrestore(&priv->lock, spinflags);
}

static int isp1760_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	u32 temp, status = 0;
	u32 mask;
	int retval = 1;
	unsigned long flags;

	/* if !PM, root hub timers won't get shut down ... */
	if (!HC_IS_RUNNING(hcd->state))
		return 0;

	/* init status to no-changes */
	buf[0] = 0;
	mask = PORT_CSC;

	spin_lock_irqsave(&priv->lock, flags);
	temp = reg_read32(hcd->regs, HC_PORTSC1);

	if (temp & PORT_OWNER) {
		if (temp & PORT_CSC) {
			temp &= ~PORT_CSC;
			reg_write32(hcd->regs, HC_PORTSC1, temp);
			goto done;
		}
	}

	/*
	 * Return status information even for ports with OWNER set.
	 * Otherwise hub_wq wouldn't see the disconnect event when a
	 * high-speed device is switched over to the companion
	 * controller by the user.
	 */

	if ((temp & mask) != 0
			|| ((temp & PORT_RESUME) != 0
				&& time_after_eq(jiffies,
					priv->reset_done))) {
		buf [0] |= 1 << (0 + 1);
		status = STS_PCD;
	}
	/* FIXME autosuspend idle root hubs */
done:
	spin_unlock_irqrestore(&priv->lock, flags);
	return status ? retval : 0;
}

static void isp1760_hub_descriptor(struct isp1760_hcd *priv,
		struct usb_hub_descriptor *desc)
{
	int ports = HCS_N_PORTS(priv->hcs_params);
	u16 temp;

	desc->bDescriptorType = 0x29;
	/* priv 1.0, 2.3.9 says 20ms max */
	desc->bPwrOn2PwrGood = 10;
	desc->bHubContrCurrent = 0;

	desc->bNbrPorts = ports;
	temp = 1 + (ports / 8);
	desc->bDescLength = 7 + 2 * temp;

	/* ports removable, and usb 1.0 legacy PortPwrCtrlMask */
	memset(&desc->u.hs.DeviceRemovable[0], 0, temp);
	memset(&desc->u.hs.DeviceRemovable[temp], 0xff, temp);

	/* per-port overcurrent reporting */
	temp = HUB_CHAR_INDV_PORT_OCPM;
	if (HCS_PPC(priv->hcs_params))
		/* per-port power control */
		temp |= HUB_CHAR_INDV_PORT_LPSM;
	else
		/* no power switching */
		temp |= HUB_CHAR_NO_LPSM;
	desc->wHubCharacteristics = cpu_to_le16(temp);
}

#define	PORT_WAKE_BITS	(PORT_WKOC_E|PORT_WKDISC_E|PORT_WKCONN_E)

static int check_reset_complete(struct usb_hcd *hcd, int index,
		int port_status)
{
	if (!(port_status & PORT_CONNECT))
		return port_status;

	/* if reset finished and it's still not enabled -- handoff */
	if (!(port_status & PORT_PE)) {

		dev_info(hcd->self.controller,
					"port %d full speed --> companion\n",
					index + 1);

		port_status |= PORT_OWNER;
		port_status &= ~PORT_RWC_BITS;
		reg_write32(hcd->regs, HC_PORTSC1, port_status);

	} else
		dev_info(hcd->self.controller, "port %d high speed\n",
								index + 1);

	return port_status;
}

static int isp1760_hub_control(struct usb_hcd *hcd, u16 typeReq,
		u16 wValue, u16 wIndex, char *buf, u16 wLength)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	int ports = HCS_N_PORTS(priv->hcs_params);
	u32 temp, status;
	unsigned long flags;
	int retval = 0;
	unsigned selector;

	/*
	 * FIXME:  support SetPortFeatures USB_PORT_FEAT_INDICATOR.
	 * HCS_INDICATOR may say we can change LEDs to off/amber/green.
	 * (track current state ourselves) ... blink for diagnostics,
	 * power, "this is the one", etc.  EHCI spec supports this.
	 */

	spin_lock_irqsave(&priv->lock, flags);
	switch (typeReq) {
	case ClearHubFeature:
		switch (wValue) {
		case C_HUB_LOCAL_POWER:
		case C_HUB_OVER_CURRENT:
			/* no hub-wide feature/status flags */
			break;
		default:
			goto error;
		}
		break;
	case ClearPortFeature:
		if (!wIndex || wIndex > ports)
			goto error;
		wIndex--;
		temp = reg_read32(hcd->regs, HC_PORTSC1);

		/*
		 * Even if OWNER is set, so the port is owned by the
		 * companion controller, hub_wq needs to be able to clear
		 * the port-change status bits (especially
		 * USB_PORT_STAT_C_CONNECTION).
		 */

		switch (wValue) {
		case USB_PORT_FEAT_ENABLE:
			reg_write32(hcd->regs, HC_PORTSC1, temp & ~PORT_PE);
			break;
		case USB_PORT_FEAT_C_ENABLE:
			/* XXX error? */
			break;
		case USB_PORT_FEAT_SUSPEND:
			if (temp & PORT_RESET)
				goto error;

			if (temp & PORT_SUSPEND) {
				if ((temp & PORT_PE) == 0)
					goto error;
				/* resume signaling for 20 msec */
				temp &= ~(PORT_RWC_BITS);
				reg_write32(hcd->regs, HC_PORTSC1,
							temp | PORT_RESUME);
				priv->reset_done = jiffies +
					msecs_to_jiffies(20);
			}
			break;
		case USB_PORT_FEAT_C_SUSPEND:
			/* we auto-clear this feature */
			break;
		case USB_PORT_FEAT_POWER:
			if (HCS_PPC(priv->hcs_params))
				reg_write32(hcd->regs, HC_PORTSC1,
							temp & ~PORT_POWER);
			break;
		case USB_PORT_FEAT_C_CONNECTION:
			reg_write32(hcd->regs, HC_PORTSC1, temp | PORT_CSC);
			break;
		case USB_PORT_FEAT_C_OVER_CURRENT:
			/* XXX error ?*/
			break;
		case USB_PORT_FEAT_C_RESET:
			/* GetPortStatus clears reset */
			break;
		default:
			goto error;
		}
		reg_read32(hcd->regs, HC_USBCMD);
		break;
	case GetHubDescriptor:
		isp1760_hub_descriptor(priv, (struct usb_hub_descriptor *)
			buf);
		break;
	case GetHubStatus:
		/* no hub-wide feature/status flags */
		memset(buf, 0, 4);
		break;
	case GetPortStatus:
		if (!wIndex || wIndex > ports)
			goto error;
		wIndex--;
		status = 0;
		temp = reg_read32(hcd->regs, HC_PORTSC1);

		/* wPortChange bits */
		if (temp & PORT_CSC)
			status |= USB_PORT_STAT_C_CONNECTION << 16;


		/* whoever resumes must GetPortStatus to complete it!! */
		if (temp & PORT_RESUME) {
			dev_err(hcd->self.controller, "Port resume should be skipped.\n");

			/* Remote Wakeup received? */
			if (!priv->reset_done) {
				/* resume signaling for 20 msec */
				priv->reset_done = jiffies
						+ msecs_to_jiffies(20);
				/* check the port again */
				mod_timer(&hcd->rh_timer, priv->reset_done);
			}

			/* resume completed? */
			else if (time_after_eq(jiffies,
					priv->reset_done)) {
				status |= USB_PORT_STAT_C_SUSPEND << 16;
				priv->reset_done = 0;

				/* stop resume signaling */
				temp = reg_read32(hcd->regs, HC_PORTSC1);
				reg_write32(hcd->regs, HC_PORTSC1,
					temp & ~(PORT_RWC_BITS | PORT_RESUME));
				retval = handshake(hcd, HC_PORTSC1,
					   PORT_RESUME, 0, 2000 /* 2msec */);
				if (retval != 0) {
					dev_err(hcd->self.controller,
						"port %d resume error %d\n",
						wIndex + 1, retval);
					goto error;
				}
				temp &= ~(PORT_SUSPEND|PORT_RESUME|(3<<10));
			}
		}

		/* whoever resets must GetPortStatus to complete it!! */
		if ((temp & PORT_RESET)
				&& time_after_eq(jiffies,
					priv->reset_done)) {
			status |= USB_PORT_STAT_C_RESET << 16;
			priv->reset_done = 0;

			/* force reset to complete */
			reg_write32(hcd->regs, HC_PORTSC1, temp & ~PORT_RESET);
			/* REVISIT:  some hardware needs 550+ usec to clear
			 * this bit; seems too long to spin routinely...
			 */
			retval = handshake(hcd, HC_PORTSC1,
					PORT_RESET, 0, 750);
			if (retval != 0) {
				dev_err(hcd->self.controller, "port %d reset error %d\n",
						wIndex + 1, retval);
				goto error;
			}

			/* see what we found out */
			temp = check_reset_complete(hcd, wIndex,
					reg_read32(hcd->regs, HC_PORTSC1));
		}
		/*
		 * Even if OWNER is set, there's no harm letting hub_wq
		 * see the wPortStatus values (they should all be 0 except
		 * for PORT_POWER anyway).
		 */

		if (temp & PORT_OWNER)
			dev_err(hcd->self.controller, "PORT_OWNER is set\n");

		if (temp & PORT_CONNECT) {
			status |= USB_PORT_STAT_CONNECTION;
			/* status may be from integrated TT */
			status |= USB_PORT_STAT_HIGH_SPEED;
		}
		if (temp & PORT_PE)
			status |= USB_PORT_STAT_ENABLE;
		if (temp & (PORT_SUSPEND|PORT_RESUME))
			status |= USB_PORT_STAT_SUSPEND;
		if (temp & PORT_RESET)
			status |= USB_PORT_STAT_RESET;
		if (temp & PORT_POWER)
			status |= USB_PORT_STAT_POWER;

		put_unaligned(cpu_to_le32(status), (__le32 *) buf);
		break;
	case SetHubFeature:
		switch (wValue) {
		case C_HUB_LOCAL_POWER:
		case C_HUB_OVER_CURRENT:
			/* no hub-wide feature/status flags */
			break;
		default:
			goto error;
		}
		break;
	case SetPortFeature:
		selector = wIndex >> 8;
		wIndex &= 0xff;
		if (!wIndex || wIndex > ports)
			goto error;
		wIndex--;
		temp = reg_read32(hcd->regs, HC_PORTSC1);
		if (temp & PORT_OWNER)
			break;

/*		temp &= ~PORT_RWC_BITS; */
		switch (wValue) {
		case USB_PORT_FEAT_ENABLE:
			reg_write32(hcd->regs, HC_PORTSC1, temp | PORT_PE);
			break;

		case USB_PORT_FEAT_SUSPEND:
			if ((temp & PORT_PE) == 0
					|| (temp & PORT_RESET) != 0)
				goto error;

			reg_write32(hcd->regs, HC_PORTSC1, temp | PORT_SUSPEND);
			break;
		case USB_PORT_FEAT_POWER:
			if (HCS_PPC(priv->hcs_params))
				reg_write32(hcd->regs, HC_PORTSC1,
							temp | PORT_POWER);
			break;
		case USB_PORT_FEAT_RESET:
			if (temp & PORT_RESUME)
				goto error;
			/* line status bits may report this as low speed,
			 * which can be fine if this root hub has a
			 * transaction translator built in.
			 */
			if ((temp & (PORT_PE|PORT_CONNECT)) == PORT_CONNECT
					&& PORT_USB11(temp)) {
				temp |= PORT_OWNER;
			} else {
				temp |= PORT_RESET;
				temp &= ~PORT_PE;

				/*
				 * caller must wait, then call GetPortStatus
				 * usb 2.0 spec says 50 ms resets on root
				 */
				priv->reset_done = jiffies +
					msecs_to_jiffies(50);
			}
			reg_write32(hcd->regs, HC_PORTSC1, temp);
			break;
		default:
			goto error;
		}
		reg_read32(hcd->regs, HC_USBCMD);
		break;

	default:
error:
		/* "stall" on error */
		retval = -EPIPE;
	}
	spin_unlock_irqrestore(&priv->lock, flags);
	return retval;
}

static int isp1760_get_frame(struct usb_hcd *hcd)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	u32 fr;

	fr = reg_read32(hcd->regs, HC_FRINDEX);
	return (fr >> 3) % priv->periodic_size;
}

static void isp1760_stop(struct usb_hcd *hcd)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	u32 temp;

	del_timer(&errata2_timer);

	isp1760_hub_control(hcd, ClearPortFeature, USB_PORT_FEAT_POWER,	1,
			NULL, 0);
	mdelay(20);

	spin_lock_irq(&priv->lock);
	ehci_reset(hcd);
	/* Disable IRQ */
	temp = reg_read32(hcd->regs, HC_HW_MODE_CTRL);
	reg_write32(hcd->regs, HC_HW_MODE_CTRL, temp &= ~HW_GLOBAL_INTR_EN);
	spin_unlock_irq(&priv->lock);

	reg_write32(hcd->regs, HC_CONFIGFLAG, 0);
}

static void isp1760_shutdown(struct usb_hcd *hcd)
{
	u32 command, temp;

	isp1760_stop(hcd);
	temp = reg_read32(hcd->regs, HC_HW_MODE_CTRL);
	reg_write32(hcd->regs, HC_HW_MODE_CTRL, temp &= ~HW_GLOBAL_INTR_EN);

	command = reg_read32(hcd->regs, HC_USBCMD);
	command &= ~CMD_RUN;
	reg_write32(hcd->regs, HC_USBCMD, command);
}

static void isp1760_clear_tt_buffer_complete(struct usb_hcd *hcd,
						struct usb_host_endpoint *ep)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	struct isp1760_qh *qh = ep->hcpriv;
	unsigned long spinflags;

	if (!qh)
		return;

	spin_lock_irqsave(&priv->lock, spinflags);
	qh->tt_buffer_dirty = 0;
	schedule_ptds(hcd);
	spin_unlock_irqrestore(&priv->lock, spinflags);
}


static const struct hc_driver isp1760_hc_driver = {
	.description		= "isp1760-hcd",
	.product_desc		= "NXP ISP1760 USB Host Controller",
	.hcd_priv_size		= sizeof(struct isp1760_hcd *),
	.irq			= isp1760_irq,
	.flags			= HCD_MEMORY | HCD_USB2,
	.reset			= isp1760_hc_setup,
	.start			= isp1760_run,
	.stop			= isp1760_stop,
	.shutdown		= isp1760_shutdown,
	.urb_enqueue		= isp1760_urb_enqueue,
	.urb_dequeue		= isp1760_urb_dequeue,
	.endpoint_disable	= isp1760_endpoint_disable,
	.get_frame_number	= isp1760_get_frame,
	.hub_status_data	= isp1760_hub_status_data,
	.hub_control		= isp1760_hub_control,
	.clear_tt_buffer_complete	= isp1760_clear_tt_buffer_complete,
};

int __init isp1760_init_kmem_once(void)
{
	urb_listitem_cachep = kmem_cache_create("isp1760_urb_listitem",
			sizeof(struct urb_listitem), 0, SLAB_TEMPORARY |
			SLAB_MEM_SPREAD, NULL);

	if (!urb_listitem_cachep)
		return -ENOMEM;

	qtd_cachep = kmem_cache_create("isp1760_qtd",
			sizeof(struct isp1760_qtd), 0, SLAB_TEMPORARY |
			SLAB_MEM_SPREAD, NULL);

	if (!qtd_cachep)
		return -ENOMEM;

	qh_cachep = kmem_cache_create("isp1760_qh", sizeof(struct isp1760_qh),
			0, SLAB_TEMPORARY | SLAB_MEM_SPREAD, NULL);

	if (!qh_cachep) {
		kmem_cache_destroy(qtd_cachep);
		return -ENOMEM;
	}

	return 0;
}

void isp1760_deinit_kmem_cache(void)
{
	kmem_cache_destroy(qtd_cachep);
	kmem_cache_destroy(qh_cachep);
	kmem_cache_destroy(urb_listitem_cachep);
}

int isp1760_hcd_register(struct isp1760_hcd *priv, void __iomem *regs,
			 struct resource *mem, int irq, unsigned long irqflags,
			 struct device *dev)
{
	struct usb_hcd *hcd;
	int ret;

	hcd = usb_create_hcd(&isp1760_hc_driver, dev, dev_name(dev));
	if (!hcd)
		return -ENOMEM;

	*(struct isp1760_hcd **)hcd->hcd_priv = priv;

	priv->hcd = hcd;

	init_memory(priv);

	hcd->irq = irq;
	hcd->regs = regs;
	hcd->rsrc_start = mem->start;
	hcd->rsrc_len = resource_size(mem);

	/* This driver doesn't support wakeup requests */
	hcd->cant_recv_wakeups = 1;

	ret = usb_add_hcd(hcd, irq, irqflags);
	if (ret)
		goto error;

	device_wakeup_enable(hcd->self.controller);

	return 0;

error:
	usb_put_hcd(hcd);
	return ret;
}

void isp1760_hcd_unregister(struct isp1760_hcd *priv)
{
	if (!priv->hcd)
		return;

	usb_remove_hcd(priv->hcd);
	usb_put_hcd(priv->hcd);
}
