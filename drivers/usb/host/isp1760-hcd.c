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
 */
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
#include <asm/unaligned.h>
#include <asm/cacheflush.h>

#include "isp1760-hcd.h"

static struct kmem_cache *qtd_cachep;
static struct kmem_cache *qh_cachep;

struct isp1760_hcd {
	u32 hcs_params;
	spinlock_t		lock;
	struct inter_packet_info atl_ints[32];
	struct inter_packet_info int_ints[32];
	struct memory_chunk memory_pool[BLOCKS];
	u32 atl_queued;

	/* periodic schedule support */
#define	DEFAULT_I_TDPS		1024
	unsigned		periodic_size;
	unsigned		i_thresh;
	unsigned long		reset_done;
	unsigned long		next_statechange;
	unsigned int		devflags;
};

static inline struct isp1760_hcd *hcd_to_priv(struct usb_hcd *hcd)
{
	return (struct isp1760_hcd *) (hcd->hcd_priv);
}

/* Section 2.2 Host Controller Capability Registers */
#define HC_LENGTH(p)		(((p)>>00)&0x00ff)	/* bits 7:0 */
#define HC_VERSION(p)		(((p)>>16)&0xffff)	/* bits 31:16 */
#define HCS_INDICATOR(p)	((p)&(1 << 16))	/* true: has port indicators */
#define HCS_PPC(p)		((p)&(1 << 4))	/* true: port power control */
#define HCS_N_PORTS(p)		(((p)>>0)&0xf)	/* bits 3:0, ports on HC */
#define HCC_ISOC_CACHE(p)       ((p)&(1 << 7))  /* true: can cache isoc frame */
#define HCC_ISOC_THRES(p)       (((p)>>4)&0x7)  /* bits 6:4, uframes cached */

/* Section 2.3 Host Controller Operational Registers */
#define CMD_LRESET	(1<<7)		/* partial reset (no ports, etc) */
#define CMD_RESET	(1<<1)		/* reset HC not bus */
#define CMD_RUN		(1<<0)		/* start/stop HC */
#define STS_PCD		(1<<2)		/* port change detect */
#define FLAG_CF		(1<<0)		/* true: we'll support "high speed" */

#define PORT_OWNER	(1<<13)		/* true: companion hc owns this port */
#define PORT_POWER	(1<<12)		/* true: has power (see PPC) */
#define PORT_USB11(x) (((x) & (3 << 10)) == (1 << 10))	/* USB 1.1 device */
#define PORT_RESET	(1<<8)		/* reset port */
#define PORT_SUSPEND	(1<<7)		/* suspend port */
#define PORT_RESUME	(1<<6)		/* resume it */
#define PORT_PE		(1<<2)		/* port enable */
#define PORT_CSC	(1<<1)		/* connect status change */
#define PORT_CONNECT	(1<<0)		/* device connected */
#define PORT_RWC_BITS   (PORT_CSC)

struct isp1760_qtd {
	u8 packet_type;
	void *data_buffer;
	u32 payload_addr;

	/* the rest is HCD-private */
	struct list_head qtd_list;
	struct urb *urb;
	size_t length;

	/* isp special*/
	u32 status;
#define URB_ENQUEUED		(1 << 1)
};

struct isp1760_qh {
	/* first part defined by EHCI spec */
	struct list_head qtd_list;

	u32 toggle;
	u32 ping;
};

/*
 * Access functions for isp176x registers (addresses 0..0x03FF).
 */
static u32 reg_read32(void __iomem *base, u32 reg)
{
	return readl(base + reg);
}

static void reg_write32(void __iomem *base, u32 reg, u32 val)
{
	writel(val, base + reg);
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

	BUG_ON(payload_addr - priv->memory_pool[0].start > PAYLOAD_AREA_SIZE);
}

static void alloc_mem(struct usb_hcd *hcd, struct isp1760_qtd *qtd)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	int i;

	BUG_ON(qtd->payload_addr);

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

	dev_err(hcd->self.controller,
				"%s: Cannot allocate %zu bytes of memory\n"
				"Current memory map:\n",
				__func__, qtd->length);
	for (i = 0; i < BLOCKS; i++) {
		dev_err(hcd->self.controller, "Pool %2d size %4d status: %d\n",
				i, priv->memory_pool[i].size,
				priv->memory_pool[i].free);
	}
	/* XXX maybe -ENOMEM could be possible */
	BUG();
	return;
}

static void free_mem(struct usb_hcd *hcd, struct isp1760_qtd *qtd)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	int i;

	if (!qtd->payload_addr)
		return;

	for (i = 0; i < BLOCKS; i++) {
		if (priv->memory_pool[i].start == qtd->payload_addr) {
			BUG_ON(priv->memory_pool[i].free);
			priv->memory_pool[i].free = 1;
			qtd->payload_addr = 0;
			return;
		}
	}

	dev_err(hcd->self.controller, "%s: Invalid pointer: %08x\n",
						__func__, qtd->payload_addr);
	BUG();
}

static void isp1760_init_regs(struct usb_hcd *hcd)
{
	reg_write32(hcd->regs, HC_BUFFER_STATUS_REG, 0);
	reg_write32(hcd->regs, HC_ATL_PTD_SKIPMAP_REG, NO_TRANSFER_ACTIVE);
	reg_write32(hcd->regs, HC_INT_PTD_SKIPMAP_REG, NO_TRANSFER_ACTIVE);
	reg_write32(hcd->regs, HC_ISO_PTD_SKIPMAP_REG, NO_TRANSFER_ACTIVE);

	reg_write32(hcd->regs, HC_ATL_PTD_DONEMAP_REG, ~NO_TRANSFER_ACTIVE);
	reg_write32(hcd->regs, HC_INT_PTD_DONEMAP_REG, ~NO_TRANSFER_ACTIVE);
	reg_write32(hcd->regs, HC_ISO_PTD_DONEMAP_REG, ~NO_TRANSFER_ACTIVE);
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

static void qh_destroy(struct isp1760_qh *qh)
{
	BUG_ON(!list_empty(&qh->qtd_list));
	kmem_cache_free(qh_cachep, qh);
}

static struct isp1760_qh *isp1760_qh_alloc(gfp_t flags)
{
	struct isp1760_qh *qh;

	qh = kmem_cache_zalloc(qh_cachep, flags);
	if (!qh)
		return qh;

	INIT_LIST_HEAD(&qh->qtd_list);
	return qh;
}

/* magic numbers that can affect system performance */
#define	EHCI_TUNE_CERR		3	/* 0-3 qtd retries; 0 == don't stop */
#define	EHCI_TUNE_RL_HS		4	/* nak throttle; see 4.9 */
#define	EHCI_TUNE_RL_TT		0
#define	EHCI_TUNE_MULT_HS	1	/* 1-3 transactions/uframe; 4.10.3 */
#define	EHCI_TUNE_MULT_TT	1
#define	EHCI_TUNE_FLS		2	/* (small) 256 frame schedule */

/* one-time init, only for memory state */
static int priv_init(struct usb_hcd *hcd)
{
	struct isp1760_hcd		*priv = hcd_to_priv(hcd);
	u32			hcc_params;

	spin_lock_init(&priv->lock);

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

	/* Setup HW Mode Control: This assumes a level active-low interrupt */
	hwmode = HW_DATA_BUS_32BIT;

	if (priv->devflags & ISP1760_FLAG_BUS_WIDTH_16)
		hwmode &= ~HW_DATA_BUS_32BIT;
	if (priv->devflags & ISP1760_FLAG_ANALOG_OC)
		hwmode |= HW_ANA_DIGI_OC;
	if (priv->devflags & ISP1760_FLAG_DACK_POL_HIGH)
		hwmode |= HW_DACK_POL_HIGH;
	if (priv->devflags & ISP1760_FLAG_DREQ_POL_HIGH)
		hwmode |= HW_DREQ_POL_HIGH;
	if (priv->devflags & ISP1760_FLAG_INTR_POL_HIGH)
		hwmode |= HW_INTR_HIGH_ACT;
	if (priv->devflags & ISP1760_FLAG_INTR_EDGE_TRIG)
		hwmode |= HW_INTR_EDGE_TRIG;

	/*
	 * We have to set this first in case we're in 16-bit mode.
	 * Write it twice to ensure correct upper bits if switching
	 * to 16-bit mode.
	 */
	reg_write32(hcd->regs, HC_HW_MODE_CTRL, hwmode);
	reg_write32(hcd->regs, HC_HW_MODE_CTRL, hwmode);

	reg_write32(hcd->regs, HC_SCRATCH_REG, 0xdeadbabe);
	/* Change bus pattern */
	scratch = reg_read32(hcd->regs, HC_CHIP_ID_REG);
	scratch = reg_read32(hcd->regs, HC_SCRATCH_REG);
	if (scratch != 0xdeadbabe) {
		dev_err(hcd->self.controller, "Scratch test failed.\n");
		return -ENODEV;
	}

	/* pre reset */
	isp1760_init_regs(hcd);

	/* reset */
	reg_write32(hcd->regs, HC_RESET_REG, SW_RESET_RESET_ALL);
	mdelay(100);

	reg_write32(hcd->regs, HC_RESET_REG, SW_RESET_RESET_HC);
	mdelay(100);

	result = ehci_reset(hcd);
	if (result)
		return result;

	/* Step 11 passed */

	dev_info(hcd->self.controller, "bus width: %d, oc: %s\n",
			   (priv->devflags & ISP1760_FLAG_BUS_WIDTH_16) ?
			   16 : 32, (priv->devflags & ISP1760_FLAG_ANALOG_OC) ?
			   "analog" : "digital");

	/* ATL reset */
	reg_write32(hcd->regs, HC_HW_MODE_CTRL, hwmode | ALL_ATX_RESET);
	mdelay(10);
	reg_write32(hcd->regs, HC_HW_MODE_CTRL, hwmode);

	reg_write32(hcd->regs, HC_INTERRUPT_REG, INTERRUPT_ENABLE_MASK);
	reg_write32(hcd->regs, HC_INTERRUPT_ENABLE, INTERRUPT_ENABLE_MASK);

	/*
	 * PORT 1 Control register of the ISP1760 is the OTG control
	 * register on ISP1761. Since there is no OTG or device controller
	 * support in this driver, we use port 1 as a "normal" USB host port on
	 * both chips.
	 */
	reg_write32(hcd->regs, HC_PORT1_CTRL, PORT1_POWER | PORT1_INIT2);
	mdelay(10);

	priv->hcs_params = reg_read32(hcd->regs, HC_HCSPARAMS);

	return priv_init(hcd);
}

static void isp1760_init_maps(struct usb_hcd *hcd)
{
	/*set last maps, for iso its only 1, else 32 tds bitmap*/
	reg_write32(hcd->regs, HC_ATL_PTD_LASTPTD_REG, 0x80000000);
	reg_write32(hcd->regs, HC_INT_PTD_LASTPTD_REG, 0x80000000);
	reg_write32(hcd->regs, HC_ISO_PTD_LASTPTD_REG, 0x00000001);
}

static void isp1760_enable_interrupts(struct usb_hcd *hcd)
{
	reg_write32(hcd->regs, HC_ATL_IRQ_MASK_AND_REG, 0);
	reg_write32(hcd->regs, HC_ATL_IRQ_MASK_OR_REG, 0);
	reg_write32(hcd->regs, HC_INT_IRQ_MASK_AND_REG, 0);
	reg_write32(hcd->regs, HC_INT_IRQ_MASK_OR_REG, 0);
	reg_write32(hcd->regs, HC_ISO_IRQ_MASK_AND_REG, 0);
	reg_write32(hcd->regs, HC_ISO_IRQ_MASK_OR_REG, 0xffffffff);
	/* step 23 passed */
}

static int isp1760_run(struct usb_hcd *hcd)
{
	int retval;
	u32 temp;
	u32 command;
	u32 chipid;

	hcd->uses_new_polling = 1;

	hcd->state = HC_STATE_RUNNING;
	isp1760_enable_interrupts(hcd);
	temp = reg_read32(hcd->regs, HC_HW_MODE_CTRL);
	reg_write32(hcd->regs, HC_HW_MODE_CTRL, temp | HW_GLOBAL_INTR_EN);

	command = reg_read32(hcd->regs, HC_USBCMD);
	command &= ~(CMD_LRESET|CMD_RESET);
	command |= CMD_RUN;
	reg_write32(hcd->regs, HC_USBCMD, command);

	retval = handshake(hcd, HC_USBCMD, CMD_RUN, CMD_RUN,
			250 * 1000);
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

	chipid = reg_read32(hcd->regs, HC_CHIP_ID_REG);
	dev_info(hcd->self.controller, "USB ISP %04x HW rev. %d started\n",
					chipid & 0xffff, chipid >> 16);

	/* PTD Register Init Part 2, Step 28 */
	/* enable INTs */
	isp1760_init_maps(hcd);

	/* GRR this is run-once init(), being done every time the HC starts.
	 * So long as they're part of class devices, we can't do it init()
	 * since the class device isn't created that early.
	 */
	return 0;
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

static void transform_into_atl(struct isp1760_qh *qh,
			struct isp1760_qtd *qtd, struct ptd *ptd)
{
	u32 maxpacket;
	u32 multi;
	u32 pid_code;
	u32 rl = RL_COUNTER;
	u32 nak = NAK_COUNTER;

	memset(ptd, 0, sizeof(*ptd));

	/* according to 3.6.2, max packet len can not be > 0x400 */
	maxpacket = usb_maxpacket(qtd->urb->dev, qtd->urb->pipe,
						usb_pipeout(qtd->urb->pipe));
	multi =  1 + ((maxpacket >> 11) & 0x3);
	maxpacket &= 0x7ff;

	/* DW0 */
	ptd->dw0 = PTD_VALID;
	ptd->dw0 |= PTD_LENGTH(qtd->length);
	ptd->dw0 |= PTD_MAXPACKET(maxpacket);
	ptd->dw0 |= PTD_ENDPOINT(usb_pipeendpoint(qtd->urb->pipe));

	/* DW1 */
	ptd->dw1 = usb_pipeendpoint(qtd->urb->pipe) >> 1;
	ptd->dw1 |= PTD_DEVICE_ADDR(usb_pipedevice(qtd->urb->pipe));

	pid_code = qtd->packet_type;
	ptd->dw1 |= PTD_PID_TOKEN(pid_code);

	if (usb_pipebulk(qtd->urb->pipe))
		ptd->dw1 |= PTD_TRANS_BULK;
	else if  (usb_pipeint(qtd->urb->pipe))
		ptd->dw1 |= PTD_TRANS_INT;

	if (qtd->urb->dev->speed != USB_SPEED_HIGH) {
		/* split transaction */

		ptd->dw1 |= PTD_TRANS_SPLIT;
		if (qtd->urb->dev->speed == USB_SPEED_LOW)
			ptd->dw1 |= PTD_SE_USB_LOSPEED;

		ptd->dw1 |= PTD_PORT_NUM(qtd->urb->dev->ttport);
		ptd->dw1 |= PTD_HUB_NUM(qtd->urb->dev->tt->hub->devnum);

		/* SE bit for Split INT transfers */
		if (usb_pipeint(qtd->urb->pipe) &&
				(qtd->urb->dev->speed == USB_SPEED_LOW))
			ptd->dw1 |= 2 << 16;

		ptd->dw3 = 0;
		rl = 0;
		nak = 0;
	} else {
		ptd->dw0 |= PTD_MULTI(multi);
		if (usb_pipecontrol(qtd->urb->pipe) ||
						usb_pipebulk(qtd->urb->pipe))
			ptd->dw3 = qh->ping;
		else
			ptd->dw3 = 0;
	}
	/* DW2 */
	ptd->dw2 = 0;
	ptd->dw2 |= PTD_DATA_START_ADDR(base_to_chip(qtd->payload_addr));
	ptd->dw2 |= PTD_RL_CNT(rl);
	ptd->dw3 |= PTD_NAC_CNT(nak);

	/* DW3 */
	ptd->dw3 |= qh->toggle;
	if (usb_pipecontrol(qtd->urb->pipe)) {
		if (qtd->data_buffer == qtd->urb->setup_packet)
			ptd->dw3 &= ~PTD_DATA_TOGGLE(1);
		else if (last_qtd_of_urb(qtd, qh))
			ptd->dw3 |= PTD_DATA_TOGGLE(1);
	}

	ptd->dw3 |= PTD_ACTIVE;
	/* Cerr */
	ptd->dw3 |= PTD_CERR(ERR_COUNTER);
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

static void transform_into_int(struct isp1760_qh *qh,
			struct isp1760_qtd *qtd, struct ptd *ptd)
{
	transform_into_atl(qh, qtd, ptd);
	transform_add_int(qh, qtd, ptd);
}

static int qtd_fill(struct isp1760_qtd *qtd, void *databuffer, size_t len,
		u32 token)
{
	int count;

	qtd->data_buffer = databuffer;
	qtd->packet_type = GET_QTD_TOKEN_TYPE(token);

	if (len > MAX_PAYLOAD_SIZE)
		count = MAX_PAYLOAD_SIZE;
	else
		count = len;

	qtd->length = count;
	return count;
}

static int check_error(struct usb_hcd *hcd, struct ptd *ptd)
{
	int error = 0;

	if (ptd->dw3 & DW3_HALT_BIT) {
		error = -EPIPE;

		if (ptd->dw3 & DW3_ERROR_BIT)
			pr_err("error bit is set in DW3\n");
	}

	if (ptd->dw3 & DW3_QTD_ACTIVE) {
		dev_err(hcd->self.controller, "Transfer active bit is set DW3\n"
			"nak counter: %d, rl: %d\n",
			(ptd->dw3 >> 19) & 0xf, (ptd->dw2 >> 25) & 0xf);
	}

	return error;
}

static void check_int_err_status(struct usb_hcd *hcd, u32 dw4)
{
	u32 i;

	dw4 >>= 8;

	for (i = 0; i < 8; i++) {
		switch (dw4 & 0x7) {
		case INT_UNDERRUN:
			dev_err(hcd->self.controller, "Underrun (%d)\n", i);
			break;

		case INT_EXACT:
			dev_err(hcd->self.controller,
						"Transaction error (%d)\n", i);
			break;

		case INT_BABBLE:
			dev_err(hcd->self.controller, "Babble error (%d)\n", i);
			break;
		}
		dw4 >>= 3;
	}
}

static void enqueue_one_qtd(struct usb_hcd *hcd, struct isp1760_qtd *qtd)
{
	if (qtd->length && (qtd->length <= MAX_PAYLOAD_SIZE)) {
		switch (qtd->packet_type) {
		case IN_PID:
			break;
		case OUT_PID:
		case SETUP_PID:
			mem_writes8(hcd->regs, qtd->payload_addr,
						qtd->data_buffer, qtd->length);
		}
	}
}

static void enqueue_one_atl_qtd(struct usb_hcd *hcd, struct isp1760_qh *qh,
					u32 slot, struct isp1760_qtd *qtd)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	struct ptd ptd;

	alloc_mem(hcd, qtd);
	transform_into_atl(qh, qtd, &ptd);
	ptd_write(hcd->regs, ATL_PTD_OFFSET, slot, &ptd);
	enqueue_one_qtd(hcd, qtd);

	priv->atl_ints[slot].qh = qh;
	priv->atl_ints[slot].qtd = qtd;
	qtd->status |= URB_ENQUEUED;
	qtd->status |= slot << 16;
}

static void enqueue_one_int_qtd(struct usb_hcd *hcd, struct isp1760_qh *qh,
					u32 slot, struct isp1760_qtd *qtd)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	struct ptd ptd;

	alloc_mem(hcd, qtd);
	transform_into_int(qh, qtd, &ptd);
	ptd_write(hcd->regs, INT_PTD_OFFSET, slot, &ptd);
	enqueue_one_qtd(hcd, qtd);

	priv->int_ints[slot].qh = qh;
	priv->int_ints[slot].qtd = qtd;
	qtd->status |= URB_ENQUEUED;
	qtd->status |= slot << 16;
}

static void enqueue_an_ATL_packet(struct usb_hcd *hcd, struct isp1760_qh *qh,
				  struct isp1760_qtd *qtd)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	u32 skip_map, or_map;
	u32 slot;
	u32 buffstatus;

	/*
	 * When this function is called from the interrupt handler to enqueue
	 * a follow-up packet, the SKIP register gets written and read back
	 * almost immediately. With ISP1761, this register requires a delay of
	 * 195ns between a write and subsequent read (see section 15.1.1.3).
	 */
	mmiowb();
	ndelay(195);
	skip_map = reg_read32(hcd->regs, HC_ATL_PTD_SKIPMAP_REG);

	BUG_ON(!skip_map);
	slot = __ffs(skip_map);

	enqueue_one_atl_qtd(hcd, qh, slot, qtd);

	or_map = reg_read32(hcd->regs, HC_ATL_IRQ_MASK_OR_REG);
	or_map |= (1 << slot);
	reg_write32(hcd->regs, HC_ATL_IRQ_MASK_OR_REG, or_map);

	skip_map &= ~(1 << slot);
	reg_write32(hcd->regs, HC_ATL_PTD_SKIPMAP_REG, skip_map);

	priv->atl_queued++;
	if (priv->atl_queued == 2)
		reg_write32(hcd->regs, HC_INTERRUPT_ENABLE,
				INTERRUPT_ENABLE_SOT_MASK);

	buffstatus = reg_read32(hcd->regs, HC_BUFFER_STATUS_REG);
	buffstatus |= ATL_BUFFER;
	reg_write32(hcd->regs, HC_BUFFER_STATUS_REG, buffstatus);
}

static void enqueue_an_INT_packet(struct usb_hcd *hcd, struct isp1760_qh *qh,
				  struct isp1760_qtd *qtd)
{
	u32 skip_map, or_map;
	u32 slot;
	u32 buffstatus;

	/*
	 * When this function is called from the interrupt handler to enqueue
	 * a follow-up packet, the SKIP register gets written and read back
	 * almost immediately. With ISP1761, this register requires a delay of
	 * 195ns between a write and subsequent read (see section 15.1.1.3).
	 */
	mmiowb();
	ndelay(195);
	skip_map = reg_read32(hcd->regs, HC_INT_PTD_SKIPMAP_REG);

	BUG_ON(!skip_map);
	slot = __ffs(skip_map);

	enqueue_one_int_qtd(hcd, qh, slot, qtd);

	or_map = reg_read32(hcd->regs, HC_INT_IRQ_MASK_OR_REG);
	or_map |= (1 << slot);
	reg_write32(hcd->regs, HC_INT_IRQ_MASK_OR_REG, or_map);

	skip_map &= ~(1 << slot);
	reg_write32(hcd->regs, HC_INT_PTD_SKIPMAP_REG, skip_map);

	buffstatus = reg_read32(hcd->regs, HC_BUFFER_STATUS_REG);
	buffstatus |= INT_BUFFER;
	reg_write32(hcd->regs, HC_BUFFER_STATUS_REG, buffstatus);
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

static void isp1760_qtd_free(struct isp1760_qtd *qtd)
{
	BUG_ON(qtd->payload_addr);
	kmem_cache_free(qtd_cachep, qtd);
}

static struct isp1760_qtd *clean_this_qtd(struct isp1760_qtd *qtd,
							struct isp1760_qh *qh)
{
	struct isp1760_qtd *tmp_qtd;

	if (list_is_last(&qtd->qtd_list, &qh->qtd_list))
		tmp_qtd = NULL;
	else
		tmp_qtd = list_entry(qtd->qtd_list.next, struct isp1760_qtd,
								qtd_list);
	list_del(&qtd->qtd_list);
	isp1760_qtd_free(qtd);
	return tmp_qtd;
}

/*
 * Remove this QTD from the QH list and free its memory. If this QTD
 * isn't the last one than remove also his successor(s).
 * Returns the QTD which is part of an new URB and should be enqueued.
 */
static struct isp1760_qtd *clean_up_qtdlist(struct isp1760_qtd *qtd,
							struct isp1760_qh *qh)
{
	struct urb *urb;

	urb = qtd->urb;
	do {
		qtd = clean_this_qtd(qtd, qh);
	} while (qtd && (qtd->urb == urb));

	return qtd;
}

static void do_atl_int(struct usb_hcd *hcd)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	u32 done_map, skip_map;
	struct ptd ptd;
	struct urb *urb;
	u32 slot;
	u32 length;
	u32 or_map;
	u32 status = -EINVAL;
	int error;
	struct isp1760_qtd *qtd;
	struct isp1760_qh *qh;
	u32 rl;
	u32 nakcount;

	done_map = reg_read32(hcd->regs, HC_ATL_PTD_DONEMAP_REG);
	skip_map = reg_read32(hcd->regs, HC_ATL_PTD_SKIPMAP_REG);

	or_map = reg_read32(hcd->regs, HC_ATL_IRQ_MASK_OR_REG);
	or_map &= ~done_map;
	reg_write32(hcd->regs, HC_ATL_IRQ_MASK_OR_REG, or_map);

	while (done_map) {
		status = 0;
		priv->atl_queued--;

		slot = __ffs(done_map);
		done_map &= ~(1 << slot);
		skip_map |= (1 << slot);

		qtd = priv->atl_ints[slot].qtd;
		qh = priv->atl_ints[slot].qh;

		if (!qh) {
			dev_err(hcd->self.controller, "qh is 0\n");
			continue;
		}
		ptd_read(hcd->regs, ATL_PTD_OFFSET, slot, &ptd);

		rl = (ptd.dw2 >> 25) & 0x0f;
		nakcount = (ptd.dw3 >> 19) & 0xf;

		/* Transfer Error, *but* active and no HALT -> reload */
		if ((ptd.dw3 & DW3_ERROR_BIT) && (ptd.dw3 & DW3_QTD_ACTIVE) &&
				!(ptd.dw3 & DW3_HALT_BIT)) {

			/* according to ppriv code, we have to
			 * reload this one if trasfered bytes != requested bytes
			 * else act like everything went smooth..
			 * XXX This just doesn't feel right and hasn't
			 * triggered so far.
			 */

			length = PTD_XFERRED_LENGTH(ptd.dw3);
			dev_err(hcd->self.controller,
					"Should reload now... transferred %d "
					"of %zu\n", length, qtd->length);
			BUG();
		}

		if (!nakcount && (ptd.dw3 & DW3_QTD_ACTIVE)) {
			u32 buffstatus;

			/*
			 * NAKs are handled in HW by the chip. Usually if the
			 * device is not able to send data fast enough.
			 * This happens mostly on slower hardware.
			 */

			/* RL counter = ERR counter */
			ptd.dw3 &= ~(0xf << 19);
			ptd.dw3 |= rl << 19;
			ptd.dw3 &= ~(3 << (55 - 32));
			ptd.dw3 |= ERR_COUNTER << (55 - 32);

			/*
			 * It is not needed to write skip map back because it
			 * is unchanged. Just make sure that this entry is
			 * unskipped once it gets written to the HW.
			 */
			skip_map &= ~(1 << slot);
			or_map = reg_read32(hcd->regs, HC_ATL_IRQ_MASK_OR_REG);
			or_map |= 1 << slot;
			reg_write32(hcd->regs, HC_ATL_IRQ_MASK_OR_REG, or_map);

			ptd.dw0 |= PTD_VALID;
			ptd_write(hcd->regs, ATL_PTD_OFFSET, slot, &ptd);

			priv->atl_queued++;
			if (priv->atl_queued == 2)
				reg_write32(hcd->regs, HC_INTERRUPT_ENABLE,
						INTERRUPT_ENABLE_SOT_MASK);

			buffstatus = reg_read32(hcd->regs,
							HC_BUFFER_STATUS_REG);
			buffstatus |= ATL_BUFFER;
			reg_write32(hcd->regs, HC_BUFFER_STATUS_REG,
								buffstatus);
			continue;
		}

		error = check_error(hcd, &ptd);
		if (error) {
			status = error;
			priv->atl_ints[slot].qh->toggle = 0;
			priv->atl_ints[slot].qh->ping = 0;
			qtd->urb->status = -EPIPE;

#if 0
			printk(KERN_ERR "Error in %s().\n", __func__);
			printk(KERN_ERR "IN dw0: %08x dw1: %08x dw2: %08x "
					"dw3: %08x dw4: %08x dw5: %08x dw6: "
					"%08x dw7: %08x\n",
					ptd.dw0, ptd.dw1, ptd.dw2, ptd.dw3,
					ptd.dw4, ptd.dw5, ptd.dw6, ptd.dw7);
#endif
		} else {
			priv->atl_ints[slot].qh->toggle = ptd.dw3 & (1 << 25);
			priv->atl_ints[slot].qh->ping = ptd.dw3 & (1 << 26);
		}

		length = PTD_XFERRED_LENGTH(ptd.dw3);
		if (length) {
			switch (DW1_GET_PID(ptd.dw1)) {
			case IN_PID:
				mem_reads8(hcd->regs, qtd->payload_addr,
						qtd->data_buffer, length);

			case OUT_PID:

				qtd->urb->actual_length += length;

			case SETUP_PID:
				break;
			}
		}

		priv->atl_ints[slot].qtd = NULL;
		priv->atl_ints[slot].qh = NULL;

		free_mem(hcd, qtd);

		reg_write32(hcd->regs, HC_ATL_PTD_SKIPMAP_REG, skip_map);

		if (qtd->urb->status == -EPIPE) {
			/* HALT was received */

			urb = qtd->urb;
			qtd = clean_up_qtdlist(qtd, qh);
			isp1760_urb_done(hcd, urb);

		} else if (usb_pipebulk(qtd->urb->pipe) &&
						(length < qtd->length)) {
			/* short BULK received */

			if (qtd->urb->transfer_flags & URB_SHORT_NOT_OK) {
				qtd->urb->status = -EREMOTEIO;
				dev_dbg(hcd->self.controller,
						"short bulk, %d instead %zu "
						"with URB_SHORT_NOT_OK flag.\n",
						length, qtd->length);
			}

			if (qtd->urb->status == -EINPROGRESS)
				qtd->urb->status = 0;

			urb = qtd->urb;
			qtd = clean_up_qtdlist(qtd, qh);
			isp1760_urb_done(hcd, urb);

		} else if (last_qtd_of_urb(qtd, qh)) {
			/* that was the last qtd of that URB */

			if (qtd->urb->status == -EINPROGRESS)
				qtd->urb->status = 0;

			urb = qtd->urb;
			qtd = clean_up_qtdlist(qtd, qh);
			isp1760_urb_done(hcd, urb);

		} else {
			/* next QTD of this URB */

			qtd = clean_this_qtd(qtd, qh);
			BUG_ON(!qtd);
		}

		if (qtd)
			enqueue_an_ATL_packet(hcd, qh, qtd);

		skip_map = reg_read32(hcd->regs, HC_ATL_PTD_SKIPMAP_REG);
	}
	if (priv->atl_queued <= 1)
		reg_write32(hcd->regs, HC_INTERRUPT_ENABLE,
							INTERRUPT_ENABLE_MASK);
}

static void do_intl_int(struct usb_hcd *hcd)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	u32 done_map, skip_map;
	struct ptd ptd;
	struct urb *urb;
	u32 length;
	u32 or_map;
	int error;
	u32 slot;
	struct isp1760_qtd *qtd;
	struct isp1760_qh *qh;

	done_map = reg_read32(hcd->regs, HC_INT_PTD_DONEMAP_REG);
	skip_map = reg_read32(hcd->regs, HC_INT_PTD_SKIPMAP_REG);

	or_map = reg_read32(hcd->regs, HC_INT_IRQ_MASK_OR_REG);
	or_map &= ~done_map;
	reg_write32(hcd->regs, HC_INT_IRQ_MASK_OR_REG, or_map);

	while (done_map) {
		slot = __ffs(done_map);
		done_map &= ~(1 << slot);
		skip_map |= (1 << slot);

		qtd = priv->int_ints[slot].qtd;
		qh = priv->int_ints[slot].qh;

		if (!qh) {
			dev_err(hcd->self.controller, "(INT) qh is 0\n");
			continue;
		}

		ptd_read(hcd->regs, INT_PTD_OFFSET, slot, &ptd);
		check_int_err_status(hcd, ptd.dw4);

		error = check_error(hcd, &ptd);
		if (error) {
#if 0
			printk(KERN_ERR "Error in %s().\n", __func__);
			printk(KERN_ERR "IN dw0: %08x dw1: %08x dw2: %08x "
					"dw3: %08x dw4: %08x dw5: %08x dw6: "
					"%08x dw7: %08x\n",
					ptd.dw0, ptd.dw1, ptd.dw2, ptd.dw3,
					ptd.dw4, ptd.dw5, ptd.dw6, ptd.dw7);
#endif
			qtd->urb->status = -EPIPE;
			priv->int_ints[slot].qh->toggle = 0;
			priv->int_ints[slot].qh->ping = 0;

		} else {
			priv->int_ints[slot].qh->toggle = ptd.dw3 & (1 << 25);
			priv->int_ints[slot].qh->ping = ptd.dw3 & (1 << 26);
		}

		if (qtd->urb->dev->speed != USB_SPEED_HIGH)
			length = PTD_XFERRED_LENGTH_LO(ptd.dw3);
		else
			length = PTD_XFERRED_LENGTH(ptd.dw3);

		if (length) {
			switch (DW1_GET_PID(ptd.dw1)) {
			case IN_PID:
				mem_reads8(hcd->regs, qtd->payload_addr,
						qtd->data_buffer, length);
			case OUT_PID:

				qtd->urb->actual_length += length;

			case SETUP_PID:
				break;
			}
		}

		priv->int_ints[slot].qtd = NULL;
		priv->int_ints[slot].qh = NULL;

		reg_write32(hcd->regs, HC_INT_PTD_SKIPMAP_REG, skip_map);
		free_mem(hcd, qtd);

		if (qtd->urb->status == -EPIPE) {
			/* HALT received */

			urb = qtd->urb;
			qtd = clean_up_qtdlist(qtd, qh);
			isp1760_urb_done(hcd, urb);

		} else if (last_qtd_of_urb(qtd, qh)) {

			if (qtd->urb->status == -EINPROGRESS)
				qtd->urb->status = 0;

			urb = qtd->urb;
			qtd = clean_up_qtdlist(qtd, qh);
			isp1760_urb_done(hcd, urb);

		} else {
			/* next QTD of this URB */

			qtd = clean_this_qtd(qtd, qh);
			BUG_ON(!qtd);
		}

		if (qtd)
			enqueue_an_INT_packet(hcd, qh, qtd);

		skip_map = reg_read32(hcd->regs, HC_INT_PTD_SKIPMAP_REG);
	}
}

static struct isp1760_qh *qh_make(struct usb_hcd *hcd, struct urb *urb,
		gfp_t flags)
{
	struct isp1760_qh *qh;
	int is_input, type;

	qh = isp1760_qh_alloc(flags);
	if (!qh)
		return qh;

	/*
	 * init endpoint/device data for this QH
	 */
	is_input = usb_pipein(urb->pipe);
	type = usb_pipetype(urb->pipe);

	if (!usb_pipecontrol(urb->pipe))
		usb_settoggle(urb->dev, usb_pipeendpoint(urb->pipe), !is_input,
				1);
	return qh;
}

/*
 * For control/bulk/interrupt, return QH with these TDs appended.
 * Allocates and initializes the QH if necessary.
 * Returns null if it can't allocate a QH it needs to.
 * If the QH has TDs (urbs) already, that's great.
 */
static struct isp1760_qh *qh_append_tds(struct usb_hcd *hcd,
		struct urb *urb, struct list_head *qtd_list, int epnum,
		void **ptr)
{
	struct isp1760_qh *qh;

	qh = (struct isp1760_qh *)*ptr;
	if (!qh) {
		/* can't sleep here, we have priv->lock... */
		qh = qh_make(hcd, urb, GFP_ATOMIC);
		if (!qh)
			return qh;
		*ptr = qh;
	}

	list_splice(qtd_list, qh->qtd_list.prev);

	return qh;
}

static void qtd_list_free(struct urb *urb, struct list_head *qtd_list)
{
	struct list_head *entry, *temp;

	list_for_each_safe(entry, temp, qtd_list) {
		struct isp1760_qtd	*qtd;

		qtd = list_entry(entry, struct isp1760_qtd, qtd_list);
		list_del(&qtd->qtd_list);
		isp1760_qtd_free(qtd);
	}
}

static int isp1760_prepare_enqueue(struct usb_hcd *hcd, struct urb *urb,
		struct list_head *qtd_list, gfp_t mem_flags, packet_enqueue *p)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	struct isp1760_qtd         *qtd;
	int                     epnum;
	unsigned long           flags;
	struct isp1760_qh          *qh = NULL;
	int                     rc;
	int qh_busy;

	qtd = list_entry(qtd_list->next, struct isp1760_qtd, qtd_list);
	epnum = urb->ep->desc.bEndpointAddress;

	spin_lock_irqsave(&priv->lock, flags);
	if (!HCD_HW_ACCESSIBLE(hcd)) {
		rc = -ESHUTDOWN;
		goto done;
	}
	rc = usb_hcd_link_urb_to_ep(hcd, urb);
	if (rc)
		goto done;

	qh = urb->ep->hcpriv;
	if (qh)
		qh_busy = !list_empty(&qh->qtd_list);
	else
		qh_busy = 0;

	qh = qh_append_tds(hcd, urb, qtd_list, epnum, &urb->ep->hcpriv);
	if (!qh) {
		usb_hcd_unlink_urb_from_ep(hcd, urb);
		rc = -ENOMEM;
		goto done;
	}

	if (!qh_busy)
		p(hcd, qh, qtd);

done:
	spin_unlock_irqrestore(&priv->lock, flags);
	if (!qh)
		qtd_list_free(urb, qtd_list);
	return rc;
}

static struct isp1760_qtd *isp1760_qtd_alloc(gfp_t flags)
{
	struct isp1760_qtd *qtd;

	qtd = kmem_cache_zalloc(qtd_cachep, flags);
	if (qtd)
		INIT_LIST_HEAD(&qtd->qtd_list);

	return qtd;
}

/*
 * create a list of filled qtds for this URB; won't link into qh.
 */
#define max_packet(wMaxPacketSize) ((wMaxPacketSize) & 0x07ff)
static struct list_head *qh_urb_transaction(struct usb_hcd *hcd,
		struct urb *urb, struct list_head *head, gfp_t flags)
{
	struct isp1760_qtd *qtd;
	void *buf;
	int len, maxpacket;
	int is_input;
	u32 token;

	/*
	 * URBs map to sequences of QTDs:  one logical transaction
	 */
	qtd = isp1760_qtd_alloc(flags);
	if (!qtd)
		return NULL;

	list_add_tail(&qtd->qtd_list, head);
	qtd->urb = urb;
	urb->status = -EINPROGRESS;

	token = 0;
	/* for split transactions, SplitXState initialized to zero */

	len = urb->transfer_buffer_length;
	is_input = usb_pipein(urb->pipe);
	if (usb_pipecontrol(urb->pipe)) {
		/* SETUP pid */
		qtd_fill(qtd, urb->setup_packet,
				sizeof(struct usb_ctrlrequest),
				token | SETUP_PID);

		/* ... and always at least one more pid */
		qtd = isp1760_qtd_alloc(flags);
		if (!qtd)
			goto cleanup;
		qtd->urb = urb;
		list_add_tail(&qtd->qtd_list, head);

		/* for zero length DATA stages, STATUS is always IN */
		if (len == 0)
			token |= IN_PID;
	}

	/*
	 * data transfer stage:  buffer setup
	 */
	buf = urb->transfer_buffer;

	if (is_input)
		token |= IN_PID;
	else
		token |= OUT_PID;

	maxpacket = max_packet(usb_maxpacket(urb->dev, urb->pipe, !is_input));

	/*
	 * buffer gets wrapped in one or more qtds;
	 * last one may be "short" (including zero len)
	 * and may serve as a control status ack
	 */
	for (;;) {
		int this_qtd_len;

		if (!buf && len) {
			/* XXX This looks like usb storage / SCSI bug */
			dev_err(hcd->self.controller, "buf is null, dma is %08lx len is %d\n",
					(long unsigned)urb->transfer_dma, len);
			WARN_ON(1);
		}

		this_qtd_len = qtd_fill(qtd, buf, len, token);
		len -= this_qtd_len;
		buf += this_qtd_len;

		if (len <= 0)
			break;

		qtd = isp1760_qtd_alloc(flags);
		if (!qtd)
			goto cleanup;
		qtd->urb = urb;
		list_add_tail(&qtd->qtd_list, head);
	}

	/*
	 * control requests may need a terminating data "status" ack;
	 * bulk ones may need a terminating short packet (zero length).
	 */
	if (urb->transfer_buffer_length != 0) {
		int one_more = 0;

		if (usb_pipecontrol(urb->pipe)) {
			one_more = 1;
			/* "in" <--> "out"  */
			token ^= IN_PID;
		} else if (usb_pipebulk(urb->pipe)
				&& (urb->transfer_flags & URB_ZERO_PACKET)
				&& !(urb->transfer_buffer_length % maxpacket)) {
			one_more = 1;
		}
		if (one_more) {
			qtd = isp1760_qtd_alloc(flags);
			if (!qtd)
				goto cleanup;
			qtd->urb = urb;
			list_add_tail(&qtd->qtd_list, head);

			/* never any data in such packets */
			qtd_fill(qtd, NULL, 0, token);
		}
	}

	qtd->status = 0;
	return head;

cleanup:
	qtd_list_free(urb, head);
	return NULL;
}

static int isp1760_urb_enqueue(struct usb_hcd *hcd, struct urb *urb,
		gfp_t mem_flags)
{
	struct list_head qtd_list;
	packet_enqueue *pe;

	INIT_LIST_HEAD(&qtd_list);

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
	case PIPE_BULK:
		if (!qh_urb_transaction(hcd, urb, &qtd_list, mem_flags))
			return -ENOMEM;
		pe =  enqueue_an_ATL_packet;
		break;

	case PIPE_INTERRUPT:
		if (!qh_urb_transaction(hcd, urb, &qtd_list, mem_flags))
			return -ENOMEM;
		pe = enqueue_an_INT_packet;
		break;

	case PIPE_ISOCHRONOUS:
		dev_err(hcd->self.controller, "PIPE_ISOCHRONOUS ain't supported\n");
	default:
		return -EPIPE;
	}

	return isp1760_prepare_enqueue(hcd, urb, &qtd_list, mem_flags, pe);
}

static int isp1760_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	struct inter_packet_info *ints;
	u32 i;
	u32 reg_base, or_reg, skip_reg;
	unsigned long flags;
	struct ptd ptd;
	packet_enqueue *pe;

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_ISOCHRONOUS:
		return -EPIPE;
		break;

	case PIPE_INTERRUPT:
		ints = priv->int_ints;
		reg_base = INT_PTD_OFFSET;
		or_reg = HC_INT_IRQ_MASK_OR_REG;
		skip_reg = HC_INT_PTD_SKIPMAP_REG;
		pe = enqueue_an_INT_packet;
		break;

	default:
		ints = priv->atl_ints;
		reg_base = ATL_PTD_OFFSET;
		or_reg = HC_ATL_IRQ_MASK_OR_REG;
		skip_reg = HC_ATL_PTD_SKIPMAP_REG;
		pe =  enqueue_an_ATL_packet;
		break;
	}

	memset(&ptd, 0, sizeof(ptd));
	spin_lock_irqsave(&priv->lock, flags);

	for (i = 0; i < 32; i++) {
		if (!ints[i].qh)
			continue;
		BUG_ON(!ints[i].qtd);

		if (ints[i].qtd->urb == urb) {
			u32 skip_map;
			u32 or_map;
			struct isp1760_qtd *qtd;
			struct isp1760_qh *qh;

			skip_map = reg_read32(hcd->regs, skip_reg);
			skip_map |= 1 << i;
			reg_write32(hcd->regs, skip_reg, skip_map);

			or_map = reg_read32(hcd->regs, or_reg);
			or_map &= ~(1 << i);
			reg_write32(hcd->regs, or_reg, or_map);

			ptd_write(hcd->regs, reg_base, i, &ptd);

			qtd = ints[i].qtd;
			qh = ints[i].qh;

			free_mem(hcd, qtd);
			qtd = clean_up_qtdlist(qtd, qh);

			ints[i].qh = NULL;
			ints[i].qtd = NULL;

			urb->status = status;
			isp1760_urb_done(hcd, urb);
			if (qtd)
				pe(hcd, qh, qtd);
			break;

		} else {
			struct isp1760_qtd *qtd;

			list_for_each_entry(qtd, &ints[i].qtd->qtd_list,
								qtd_list) {
				if (qtd->urb == urb) {
					clean_up_qtdlist(qtd, ints[i].qh);
					isp1760_urb_done(hcd, urb);
					qtd = NULL;
					break;
				}
			}

			/* We found the urb before the last slot */
			if (!qtd)
				break;
		}
	}

	spin_unlock_irqrestore(&priv->lock, flags);
	return 0;
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

	reg_write32(hcd->regs, HC_INTERRUPT_REG, imask);
	if (imask & (HC_ATL_INT | HC_SOT_INT))
		do_atl_int(hcd);

	if (imask & HC_INTL_INT)
		do_intl_int(hcd);

	irqret = IRQ_HANDLED;
leave:
	spin_unlock(&priv->lock);
	return irqret;
}

static int isp1760_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	u32 temp, status = 0;
	u32 mask;
	int retval = 1;
	unsigned long flags;

	/* if !USB_SUSPEND, root hub timers won't get shut down ... */
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
	 * Otherwise khubd wouldn't see the disconnect event when a
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
	temp = 0x0008;
	if (HCS_PPC(priv->hcs_params))
		/* per-port power control */
		temp |= 0x0001;
	else
		/* no power switching */
		temp |= 0x0002;
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

		dev_err(hcd->self.controller,
					"port %d full speed --> companion\n",
					index + 1);

		port_status |= PORT_OWNER;
		port_status &= ~PORT_RWC_BITS;
		reg_write32(hcd->regs, HC_PORTSC1, port_status);

	} else
		dev_err(hcd->self.controller, "port %d high speed\n",
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
		 * companion controller, khubd needs to be able to clear
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
		 * Even if OWNER is set, there's no harm letting khubd
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

static void isp1760_endpoint_disable(struct usb_hcd *hcd,
		struct usb_host_endpoint *ep)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	struct isp1760_qh *qh;
	struct isp1760_qtd *qtd;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	qh = ep->hcpriv;
	if (!qh)
		goto out;

	ep->hcpriv = NULL;
	do {
		/* more than entry might get removed */
		if (list_empty(&qh->qtd_list))
			break;

		qtd = list_first_entry(&qh->qtd_list, struct isp1760_qtd,
				qtd_list);

		if (qtd->status & URB_ENQUEUED) {
			spin_unlock_irqrestore(&priv->lock, flags);
			isp1760_urb_dequeue(hcd, qtd->urb, -ECONNRESET);
			spin_lock_irqsave(&priv->lock, flags);
		} else {
			struct urb *urb;

			urb = qtd->urb;
			clean_up_qtdlist(qtd, qh);
			urb->status = -ECONNRESET;
			isp1760_urb_done(hcd, urb);
		}
	} while (1);

	qh_destroy(qh);
	/* remove requests and leak them.
	 * ATL are pretty fast done, INT could take a while...
	 * The latter shoule be removed
	 */
out:
	spin_unlock_irqrestore(&priv->lock, flags);
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

static const struct hc_driver isp1760_hc_driver = {
	.description		= "isp1760-hcd",
	.product_desc		= "NXP ISP1760 USB Host Controller",
	.hcd_priv_size		= sizeof(struct isp1760_hcd),
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
};

int __init init_kmem_once(void)
{
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

void deinit_kmem_cache(void)
{
	kmem_cache_destroy(qtd_cachep);
	kmem_cache_destroy(qh_cachep);
}

struct usb_hcd *isp1760_register(phys_addr_t res_start, resource_size_t res_len,
				 int irq, unsigned long irqflags,
				 struct device *dev, const char *busname,
				 unsigned int devflags)
{
	struct usb_hcd *hcd;
	struct isp1760_hcd *priv;
	int ret;

	if (usb_disabled())
		return ERR_PTR(-ENODEV);

	/* prevent usb-core allocating DMA pages */
	dev->dma_mask = NULL;

	hcd = usb_create_hcd(&isp1760_hc_driver, dev, dev_name(dev));
	if (!hcd)
		return ERR_PTR(-ENOMEM);

	priv = hcd_to_priv(hcd);
	priv->devflags = devflags;
	init_memory(priv);
	hcd->regs = ioremap(res_start, res_len);
	if (!hcd->regs) {
		ret = -EIO;
		goto err_put;
	}

	hcd->irq = irq;
	hcd->rsrc_start = res_start;
	hcd->rsrc_len = res_len;

	ret = usb_add_hcd(hcd, irq, irqflags);
	if (ret)
		goto err_unmap;

	return hcd;

err_unmap:
	 iounmap(hcd->regs);

err_put:
	 usb_put_hcd(hcd);

	 return ERR_PTR(ret);
}

MODULE_DESCRIPTION("Driver for the ISP1760 USB-controller from NXP");
MODULE_AUTHOR("Sebastian Siewior <bigeasy@linuxtronix.de>");
MODULE_LICENSE("GPL v2");
