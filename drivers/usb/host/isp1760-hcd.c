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
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <asm/unaligned.h>

#include "../core/hcd.h"
#include "isp1760-hcd.h"

static struct kmem_cache *qtd_cachep;
static struct kmem_cache *qh_cachep;

struct isp1760_hcd {
	u32 hcs_params;
	spinlock_t		lock;
	struct inter_packet_info atl_ints[32];
	struct inter_packet_info int_ints[32];
	struct memory_chunk memory_pool[BLOCKS];

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
static inline struct usb_hcd *priv_to_hcd(struct isp1760_hcd *priv)
{
	return container_of((void *) priv, struct usb_hcd, hcd_priv);
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
	struct isp1760_qtd *hw_next;
	u8 packet_type;
	u8 toggle;

	void *data_buffer;
	/* the rest is HCD-private */
	struct list_head qtd_list;
	struct urb *urb;
	size_t length;

	/* isp special*/
	u32 status;
#define URB_COMPLETE_NOTIFY	(1 << 0)
#define URB_ENQUEUED		(1 << 1)
#define URB_TYPE_ATL		(1 << 2)
#define URB_TYPE_INT		(1 << 3)
};

struct isp1760_qh {
	/* first part defined by EHCI spec */
	struct list_head qtd_list;
	struct isp1760_hcd *priv;

	/* periodic schedule info */
	unsigned short period;		/* polling interval */
	struct usb_device *dev;

	u32 toggle;
	u32 ping;
};

#define ehci_port_speed(priv, portsc) (1 << USB_PORT_FEAT_HIGHSPEED)

static unsigned int isp1760_readl(__u32 __iomem *regs)
{
	return readl(regs);
}

static void isp1760_writel(const unsigned int val, __u32 __iomem *regs)
{
	writel(val, regs);
}

/*
 * The next two copy via MMIO data to/from the device. memcpy_{to|from}io()
 * doesn't quite work because some people have to enforce 32-bit access
 */
static void priv_read_copy(struct isp1760_hcd *priv, u32 *src,
		__u32 __iomem *dst, u32 len)
{
	u32 val;
	u8 *buff8;

	if (!src) {
		printk(KERN_ERR "ERROR: buffer: %p len: %d\n", src, len);
		return;
	}

	while (len >= 4) {
		*src = __raw_readl(dst);
		len -= 4;
		src++;
		dst++;
	}

	if (!len)
		return;

	/* in case we have 3, 2 or 1 by left. The dst buffer may not be fully
	 * allocated.
	 */
	val = isp1760_readl(dst);

	buff8 = (u8 *)src;
	while (len) {

		*buff8 = val;
		val >>= 8;
		len--;
		buff8++;
	}
}

static void priv_write_copy(const struct isp1760_hcd *priv, const u32 *src,
		__u32 __iomem *dst, u32 len)
{
	while (len >= 4) {
		__raw_writel(*src, dst);
		len -= 4;
		src++;
		dst++;
	}

	if (!len)
		return;
	/* in case we have 3, 2 or 1 by left. The buffer is allocated and the
	 * extra bytes should not be read by the HW
	 */

	__raw_writel(*src, dst);
}

/* memory management of the 60kb on the chip from 0x1000 to 0xffff */
static void init_memory(struct isp1760_hcd *priv)
{
	int i;
	u32 payload;

	payload = 0x1000;
	for (i = 0; i < BLOCK_1_NUM; i++) {
		priv->memory_pool[i].start = payload;
		priv->memory_pool[i].size = BLOCK_1_SIZE;
		priv->memory_pool[i].free = 1;
		payload += priv->memory_pool[i].size;
	}


	for (i = BLOCK_1_NUM; i < BLOCK_1_NUM + BLOCK_2_NUM; i++) {
		priv->memory_pool[i].start = payload;
		priv->memory_pool[i].size = BLOCK_2_SIZE;
		priv->memory_pool[i].free = 1;
		payload += priv->memory_pool[i].size;
	}


	for (i = BLOCK_1_NUM + BLOCK_2_NUM; i < BLOCKS; i++) {
		priv->memory_pool[i].start = payload;
		priv->memory_pool[i].size = BLOCK_3_SIZE;
		priv->memory_pool[i].free = 1;
		payload += priv->memory_pool[i].size;
	}

	BUG_ON(payload - priv->memory_pool[i - 1].size > PAYLOAD_SIZE);
}

static u32 alloc_mem(struct isp1760_hcd *priv, u32 size)
{
	int i;

	if (!size)
		return ISP1760_NULL_POINTER;

	for (i = 0; i < BLOCKS; i++) {
		if (priv->memory_pool[i].size >= size &&
				priv->memory_pool[i].free) {

			priv->memory_pool[i].free = 0;
			return priv->memory_pool[i].start;
		}
	}

	printk(KERN_ERR "ISP1760 MEM: can not allocate %d bytes of memory\n",
			size);
	printk(KERN_ERR "Current memory map:\n");
	for (i = 0; i < BLOCKS; i++) {
		printk(KERN_ERR "Pool %2d size %4d status: %d\n",
				i, priv->memory_pool[i].size,
				priv->memory_pool[i].free);
	}
	/* XXX maybe -ENOMEM could be possible */
	BUG();
	return 0;
}

static void free_mem(struct isp1760_hcd *priv, u32 mem)
{
	int i;

	if (mem == ISP1760_NULL_POINTER)
		return;

	for (i = 0; i < BLOCKS; i++) {
		if (priv->memory_pool[i].start == mem) {

			BUG_ON(priv->memory_pool[i].free);

			priv->memory_pool[i].free = 1;
			return ;
		}
	}

	printk(KERN_ERR "Trying to free not-here-allocated memory :%08x\n",
			mem);
	BUG();
}

static void isp1760_init_regs(struct usb_hcd *hcd)
{
	isp1760_writel(0, hcd->regs + HC_BUFFER_STATUS_REG);
	isp1760_writel(NO_TRANSFER_ACTIVE, hcd->regs +
			HC_ATL_PTD_SKIPMAP_REG);
	isp1760_writel(NO_TRANSFER_ACTIVE, hcd->regs +
			HC_INT_PTD_SKIPMAP_REG);
	isp1760_writel(NO_TRANSFER_ACTIVE, hcd->regs +
			HC_ISO_PTD_SKIPMAP_REG);

	isp1760_writel(~NO_TRANSFER_ACTIVE, hcd->regs +
			HC_ATL_PTD_DONEMAP_REG);
	isp1760_writel(~NO_TRANSFER_ACTIVE, hcd->regs +
			HC_INT_PTD_DONEMAP_REG);
	isp1760_writel(~NO_TRANSFER_ACTIVE, hcd->regs +
			HC_ISO_PTD_DONEMAP_REG);
}

static int handshake(struct isp1760_hcd *priv, void __iomem *ptr,
		      u32 mask, u32 done, int usec)
{
	u32 result;

	do {
		result = isp1760_readl(ptr);
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
static int ehci_reset(struct isp1760_hcd *priv)
{
	int retval;
	struct usb_hcd *hcd = priv_to_hcd(priv);
	u32 command = isp1760_readl(hcd->regs + HC_USBCMD);

	command |= CMD_RESET;
	isp1760_writel(command, hcd->regs + HC_USBCMD);
	hcd->state = HC_STATE_HALT;
	priv->next_statechange = jiffies;
	retval = handshake(priv, hcd->regs + HC_USBCMD,
			    CMD_RESET, 0, 250 * 1000);
	return retval;
}

static void qh_destroy(struct isp1760_qh *qh)
{
	BUG_ON(!list_empty(&qh->qtd_list));
	kmem_cache_free(qh_cachep, qh);
}

static struct isp1760_qh *isp1760_qh_alloc(struct isp1760_hcd *priv,
		gfp_t flags)
{
	struct isp1760_qh *qh;

	qh = kmem_cache_zalloc(qh_cachep, flags);
	if (!qh)
		return qh;

	INIT_LIST_HEAD(&qh->qtd_list);
	qh->priv = priv;
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
	hcc_params = isp1760_readl(hcd->regs + HC_HCCPARAMS);
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

	/*
	 * We have to set this first in case we're in 16-bit mode.
	 * Write it twice to ensure correct upper bits if switching
	 * to 16-bit mode.
	 */
	isp1760_writel(hwmode, hcd->regs + HC_HW_MODE_CTRL);
	isp1760_writel(hwmode, hcd->regs + HC_HW_MODE_CTRL);

	isp1760_writel(0xdeadbabe, hcd->regs + HC_SCRATCH_REG);
	/* Change bus pattern */
	scratch = isp1760_readl(hcd->regs + HC_CHIP_ID_REG);
	scratch = isp1760_readl(hcd->regs + HC_SCRATCH_REG);
	if (scratch != 0xdeadbabe) {
		printk(KERN_ERR "ISP1760: Scratch test failed.\n");
		return -ENODEV;
	}

	/* pre reset */
	isp1760_init_regs(hcd);

	/* reset */
	isp1760_writel(SW_RESET_RESET_ALL, hcd->regs + HC_RESET_REG);
	mdelay(100);

	isp1760_writel(SW_RESET_RESET_HC, hcd->regs + HC_RESET_REG);
	mdelay(100);

	result = ehci_reset(priv);
	if (result)
		return result;

	/* Step 11 passed */

	isp1760_info(priv, "bus width: %d, oc: %s\n",
			   (priv->devflags & ISP1760_FLAG_BUS_WIDTH_16) ?
			   16 : 32, (priv->devflags & ISP1760_FLAG_ANALOG_OC) ?
			   "analog" : "digital");

	/* ATL reset */
	isp1760_writel(hwmode | ALL_ATX_RESET, hcd->regs + HC_HW_MODE_CTRL);
	mdelay(10);
	isp1760_writel(hwmode, hcd->regs + HC_HW_MODE_CTRL);

	isp1760_writel(INTERRUPT_ENABLE_MASK, hcd->regs + HC_INTERRUPT_REG);
	isp1760_writel(INTERRUPT_ENABLE_MASK, hcd->regs + HC_INTERRUPT_ENABLE);

	/*
	 * PORT 1 Control register of the ISP1760 is the OTG control
	 * register on ISP1761.
	 */
	if (!(priv->devflags & ISP1760_FLAG_ISP1761) &&
	    !(priv->devflags & ISP1760_FLAG_PORT1_DIS)) {
		isp1760_writel(PORT1_POWER | PORT1_INIT2,
			       hcd->regs + HC_PORT1_CTRL);
		mdelay(10);
	}

	priv->hcs_params = isp1760_readl(hcd->regs + HC_HCSPARAMS);

	return priv_init(hcd);
}

static void isp1760_init_maps(struct usb_hcd *hcd)
{
	/*set last maps, for iso its only 1, else 32 tds bitmap*/
	isp1760_writel(0x80000000, hcd->regs + HC_ATL_PTD_LASTPTD_REG);
	isp1760_writel(0x80000000, hcd->regs + HC_INT_PTD_LASTPTD_REG);
	isp1760_writel(0x00000001, hcd->regs + HC_ISO_PTD_LASTPTD_REG);
}

static void isp1760_enable_interrupts(struct usb_hcd *hcd)
{
	isp1760_writel(0, hcd->regs + HC_ATL_IRQ_MASK_AND_REG);
	isp1760_writel(0, hcd->regs + HC_ATL_IRQ_MASK_OR_REG);
	isp1760_writel(0, hcd->regs + HC_INT_IRQ_MASK_AND_REG);
	isp1760_writel(0, hcd->regs + HC_INT_IRQ_MASK_OR_REG);
	isp1760_writel(0, hcd->regs + HC_ISO_IRQ_MASK_AND_REG);
	isp1760_writel(0xffffffff, hcd->regs + HC_ISO_IRQ_MASK_OR_REG);
	/* step 23 passed */
}

static int isp1760_run(struct usb_hcd *hcd)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	int retval;
	u32 temp;
	u32 command;
	u32 chipid;

	hcd->uses_new_polling = 1;
	hcd->poll_rh = 0;

	hcd->state = HC_STATE_RUNNING;
	isp1760_enable_interrupts(hcd);
	temp = isp1760_readl(hcd->regs + HC_HW_MODE_CTRL);
	isp1760_writel(temp | HW_GLOBAL_INTR_EN, hcd->regs + HC_HW_MODE_CTRL);

	command = isp1760_readl(hcd->regs + HC_USBCMD);
	command &= ~(CMD_LRESET|CMD_RESET);
	command |= CMD_RUN;
	isp1760_writel(command, hcd->regs + HC_USBCMD);

	retval = handshake(priv, hcd->regs + HC_USBCMD,	CMD_RUN, CMD_RUN,
			250 * 1000);
	if (retval)
		return retval;

	/*
	 * XXX
	 * Spec says to write FLAG_CF as last config action, priv code grabs
	 * the semaphore while doing so.
	 */
	down_write(&ehci_cf_port_reset_rwsem);
	isp1760_writel(FLAG_CF, hcd->regs + HC_CONFIGFLAG);

	retval = handshake(priv, hcd->regs + HC_CONFIGFLAG, FLAG_CF, FLAG_CF,
			250 * 1000);
	up_write(&ehci_cf_port_reset_rwsem);
	if (retval)
		return retval;

	chipid = isp1760_readl(hcd->regs + HC_CHIP_ID_REG);
	isp1760_info(priv, "USB ISP %04x HW rev. %d started\n",	chipid & 0xffff,
			chipid >> 16);

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

static void transform_into_atl(struct isp1760_hcd *priv, struct isp1760_qh *qh,
			struct isp1760_qtd *qtd, struct urb *urb,
			u32 payload, struct ptd *ptd)
{
	u32 dw0;
	u32 dw1;
	u32 dw2;
	u32 dw3;
	u32 maxpacket;
	u32 multi;
	u32 pid_code;
	u32 rl = RL_COUNTER;
	u32 nak = NAK_COUNTER;

	/* according to 3.6.2, max packet len can not be > 0x400 */
	maxpacket = usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe));
	multi =  1 + ((maxpacket >> 11) & 0x3);
	maxpacket &= 0x7ff;

	/* DW0 */
	dw0 = PTD_VALID;
	dw0 |= PTD_LENGTH(qtd->length);
	dw0 |= PTD_MAXPACKET(maxpacket);
	dw0 |= PTD_ENDPOINT(usb_pipeendpoint(urb->pipe));
	dw1 = usb_pipeendpoint(urb->pipe) >> 1;

	/* DW1 */
	dw1 |= PTD_DEVICE_ADDR(usb_pipedevice(urb->pipe));

	pid_code = qtd->packet_type;
	dw1 |= PTD_PID_TOKEN(pid_code);

	if (usb_pipebulk(urb->pipe))
		dw1 |= PTD_TRANS_BULK;
	else if  (usb_pipeint(urb->pipe))
		dw1 |= PTD_TRANS_INT;

	if (urb->dev->speed != USB_SPEED_HIGH) {
		/* split transaction */

		dw1 |= PTD_TRANS_SPLIT;
		if (urb->dev->speed == USB_SPEED_LOW)
			dw1 |= PTD_SE_USB_LOSPEED;

		dw1 |= PTD_PORT_NUM(urb->dev->ttport);
		dw1 |= PTD_HUB_NUM(urb->dev->tt->hub->devnum);

		/* SE bit for Split INT transfers */
		if (usb_pipeint(urb->pipe) &&
				(urb->dev->speed == USB_SPEED_LOW))
			dw1 |= 2 << 16;

		dw3 = 0;
		rl = 0;
		nak = 0;
	} else {
		dw0 |= PTD_MULTI(multi);
		if (usb_pipecontrol(urb->pipe) || usb_pipebulk(urb->pipe))
			dw3 = qh->ping;
		else
			dw3 = 0;
	}
	/* DW2 */
	dw2 = 0;
	dw2 |= PTD_DATA_START_ADDR(base_to_chip(payload));
	dw2 |= PTD_RL_CNT(rl);
	dw3 |= PTD_NAC_CNT(nak);

	/* DW3 */
	if (usb_pipecontrol(urb->pipe))
		dw3 |= PTD_DATA_TOGGLE(qtd->toggle);
	else
		dw3 |= qh->toggle;


	dw3 |= PTD_ACTIVE;
	/* Cerr */
	dw3 |= PTD_CERR(ERR_COUNTER);

	memset(ptd, 0, sizeof(*ptd));

	ptd->dw0 = cpu_to_le32(dw0);
	ptd->dw1 = cpu_to_le32(dw1);
	ptd->dw2 = cpu_to_le32(dw2);
	ptd->dw3 = cpu_to_le32(dw3);
}

static void transform_add_int(struct isp1760_hcd *priv, struct isp1760_qh *qh,
			struct isp1760_qtd *qtd, struct urb *urb,
			u32 payload, struct ptd *ptd)
{
	u32 maxpacket;
	u32 multi;
	u32 numberofusofs;
	u32 i;
	u32 usofmask, usof;
	u32 period;

	maxpacket = usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe));
	multi =  1 + ((maxpacket >> 11) & 0x3);
	maxpacket &= 0x7ff;
	/* length of the data per uframe */
	maxpacket = multi * maxpacket;

	numberofusofs = urb->transfer_buffer_length / maxpacket;
	if (urb->transfer_buffer_length % maxpacket)
		numberofusofs += 1;

	usofmask = 1;
	usof = 0;
	for (i = 0; i < numberofusofs; i++) {
		usof |= usofmask;
		usofmask <<= 1;
	}

	if (urb->dev->speed != USB_SPEED_HIGH) {
		/* split */
		ptd->dw5 = __constant_cpu_to_le32(0x1c);

		if (qh->period >= 32)
			period = qh->period / 2;
		else
			period = qh->period;

	} else {

		if (qh->period >= 8)
			period = qh->period/8;
		else
			period = qh->period;

		if (period >= 32)
			period  = 16;

		if (qh->period >= 8) {
			/* millisecond period */
			period = (period << 3);
		} else {
			/* usof based tranmsfers */
			/* minimum 4 usofs */
			usof = 0x11;
		}
	}

	ptd->dw2 |= cpu_to_le32(period);
	ptd->dw4 = cpu_to_le32(usof);
}

static void transform_into_int(struct isp1760_hcd *priv, struct isp1760_qh *qh,
			struct isp1760_qtd *qtd, struct urb *urb,
			u32 payload, struct ptd *ptd)
{
	transform_into_atl(priv, qh, qtd, urb, payload, ptd);
	transform_add_int(priv, qh, qtd, urb,  payload, ptd);
}

static int qtd_fill(struct isp1760_qtd *qtd, void *databuffer, size_t len,
		u32 token)
{
	int count;

	qtd->data_buffer = databuffer;
	qtd->packet_type = GET_QTD_TOKEN_TYPE(token);
	qtd->toggle = GET_DATA_TOGGLE(token);

	if (len > HC_ATL_PL_SIZE)
		count = HC_ATL_PL_SIZE;
	else
		count = len;

	qtd->length = count;
	return count;
}

static int check_error(struct ptd *ptd)
{
	int error = 0;
	u32 dw3;

	dw3 = le32_to_cpu(ptd->dw3);
	if (dw3 & DW3_HALT_BIT)
		error = -EPIPE;

	if (dw3 & DW3_ERROR_BIT) {
		printk(KERN_ERR "error bit is set in DW3\n");
		error = -EPIPE;
	}

	if (dw3 & DW3_QTD_ACTIVE) {
		printk(KERN_ERR "transfer active bit is set DW3\n");
		printk(KERN_ERR "nak counter: %d, rl: %d\n", (dw3 >> 19) & 0xf,
				(le32_to_cpu(ptd->dw2) >> 25) & 0xf);
	}

	return error;
}

static void check_int_err_status(u32 dw4)
{
	u32 i;

	dw4 >>= 8;

	for (i = 0; i < 8; i++) {
		switch (dw4 & 0x7) {
		case INT_UNDERRUN:
			printk(KERN_ERR "ERROR: under run , %d\n", i);
			break;

		case INT_EXACT:
			printk(KERN_ERR "ERROR: transaction error, %d\n", i);
			break;

		case INT_BABBLE:
			printk(KERN_ERR "ERROR: babble error, %d\n", i);
			break;
		}
		dw4 >>= 3;
	}
}

static void enqueue_one_qtd(struct isp1760_qtd *qtd, struct isp1760_hcd *priv,
		u32 payload)
{
	u32 token;
	struct usb_hcd *hcd = priv_to_hcd(priv);

	token = qtd->packet_type;

	if (qtd->length && (qtd->length <= HC_ATL_PL_SIZE)) {
		switch (token) {
		case IN_PID:
			break;
		case OUT_PID:
		case SETUP_PID:
			priv_write_copy(priv, qtd->data_buffer,
					hcd->regs + payload,
					qtd->length);
		}
	}
}

static void enqueue_one_atl_qtd(u32 atl_regs, u32 payload,
		struct isp1760_hcd *priv, struct isp1760_qh *qh,
		struct urb *urb, u32 slot, struct isp1760_qtd *qtd)
{
	struct ptd ptd;
	struct usb_hcd *hcd = priv_to_hcd(priv);

	transform_into_atl(priv, qh, qtd, urb, payload, &ptd);
	priv_write_copy(priv, (u32 *)&ptd, hcd->regs + atl_regs, sizeof(ptd));
	enqueue_one_qtd(qtd, priv, payload);

	priv->atl_ints[slot].urb = urb;
	priv->atl_ints[slot].qh = qh;
	priv->atl_ints[slot].qtd = qtd;
	priv->atl_ints[slot].data_buffer = qtd->data_buffer;
	priv->atl_ints[slot].payload = payload;
	qtd->status |= URB_ENQUEUED | URB_TYPE_ATL;
	qtd->status |= slot << 16;
}

static void enqueue_one_int_qtd(u32 int_regs, u32 payload,
		struct isp1760_hcd *priv, struct isp1760_qh *qh,
		struct urb *urb, u32 slot,  struct isp1760_qtd *qtd)
{
	struct ptd ptd;
	struct usb_hcd *hcd = priv_to_hcd(priv);

	transform_into_int(priv, qh, qtd, urb, payload, &ptd);
	priv_write_copy(priv, (u32 *)&ptd, hcd->regs + int_regs, sizeof(ptd));
	enqueue_one_qtd(qtd, priv, payload);

	priv->int_ints[slot].urb = urb;
	priv->int_ints[slot].qh = qh;
	priv->int_ints[slot].qtd = qtd;
	priv->int_ints[slot].data_buffer = qtd->data_buffer;
	priv->int_ints[slot].payload = payload;
	qtd->status |= URB_ENQUEUED | URB_TYPE_INT;
	qtd->status |= slot << 16;
}

static void enqueue_an_ATL_packet(struct usb_hcd *hcd, struct isp1760_qh *qh,
				  struct isp1760_qtd *qtd)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	u32 skip_map, or_map;
	u32 queue_entry;
	u32 slot;
	u32 atl_regs, payload;
	u32 buffstatus;

	skip_map = isp1760_readl(hcd->regs + HC_ATL_PTD_SKIPMAP_REG);

	BUG_ON(!skip_map);
	slot = __ffs(skip_map);
	queue_entry = 1 << slot;

	atl_regs = ATL_REGS_OFFSET + slot * sizeof(struct ptd);

	payload = alloc_mem(priv, qtd->length);

	enqueue_one_atl_qtd(atl_regs, payload, priv, qh, qtd->urb, slot, qtd);

	or_map = isp1760_readl(hcd->regs + HC_ATL_IRQ_MASK_OR_REG);
	or_map |= queue_entry;
	isp1760_writel(or_map, hcd->regs + HC_ATL_IRQ_MASK_OR_REG);

	skip_map &= ~queue_entry;
	isp1760_writel(skip_map, hcd->regs + HC_ATL_PTD_SKIPMAP_REG);

	buffstatus = isp1760_readl(hcd->regs + HC_BUFFER_STATUS_REG);
	buffstatus |= ATL_BUFFER;
	isp1760_writel(buffstatus, hcd->regs + HC_BUFFER_STATUS_REG);
}

static void enqueue_an_INT_packet(struct usb_hcd *hcd, struct isp1760_qh *qh,
				  struct isp1760_qtd *qtd)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	u32 skip_map, or_map;
	u32 queue_entry;
	u32 slot;
	u32 int_regs, payload;
	u32 buffstatus;

	skip_map = isp1760_readl(hcd->regs + HC_INT_PTD_SKIPMAP_REG);

	BUG_ON(!skip_map);
	slot = __ffs(skip_map);
	queue_entry = 1 << slot;

	int_regs = INT_REGS_OFFSET + slot * sizeof(struct ptd);

	payload = alloc_mem(priv, qtd->length);

	enqueue_one_int_qtd(int_regs, payload, priv, qh, qtd->urb, slot, qtd);

	or_map = isp1760_readl(hcd->regs + HC_INT_IRQ_MASK_OR_REG);
	or_map |= queue_entry;
	isp1760_writel(or_map, hcd->regs + HC_INT_IRQ_MASK_OR_REG);

	skip_map &= ~queue_entry;
	isp1760_writel(skip_map, hcd->regs + HC_INT_PTD_SKIPMAP_REG);

	buffstatus = isp1760_readl(hcd->regs + HC_BUFFER_STATUS_REG);
	buffstatus |= INT_BUFFER;
	isp1760_writel(buffstatus, hcd->regs + HC_BUFFER_STATUS_REG);
}

static void isp1760_urb_done(struct isp1760_hcd *priv, struct urb *urb, int status)
__releases(priv->lock)
__acquires(priv->lock)
{
	if (!urb->unlinked) {
		if (status == -EINPROGRESS)
			status = 0;
	}

	/* complete() can reenter this HCD */
	usb_hcd_unlink_urb_from_ep(priv_to_hcd(priv), urb);
	spin_unlock(&priv->lock);
	usb_hcd_giveback_urb(priv_to_hcd(priv), urb, status);
	spin_lock(&priv->lock);
}

static void isp1760_qtd_free(struct isp1760_qtd *qtd)
{
	kmem_cache_free(qtd_cachep, qtd);
}

static struct isp1760_qtd *clean_this_qtd(struct isp1760_qtd *qtd)
{
	struct isp1760_qtd *tmp_qtd;

	tmp_qtd = qtd->hw_next;
	list_del(&qtd->qtd_list);
	isp1760_qtd_free(qtd);
	return tmp_qtd;
}

/*
 * Remove this QTD from the QH list and free its memory. If this QTD
 * isn't the last one than remove also his successor(s).
 * Returns the QTD which is part of an new URB and should be enqueued.
 */
static struct isp1760_qtd *clean_up_qtdlist(struct isp1760_qtd *qtd)
{
	struct isp1760_qtd *tmp_qtd;
	int last_one;

	do {
		tmp_qtd = qtd->hw_next;
		last_one = qtd->status & URB_COMPLETE_NOTIFY;
		list_del(&qtd->qtd_list);
		isp1760_qtd_free(qtd);
		qtd = tmp_qtd;
	} while (!last_one && qtd);

	return qtd;
}

static void do_atl_int(struct usb_hcd *usb_hcd)
{
	struct isp1760_hcd *priv = hcd_to_priv(usb_hcd);
	u32 done_map, skip_map;
	struct ptd ptd;
	struct urb *urb = NULL;
	u32 atl_regs_base;
	u32 atl_regs;
	u32 queue_entry;
	u32 payload;
	u32 length;
	u32 or_map;
	u32 status = -EINVAL;
	int error;
	struct isp1760_qtd *qtd;
	struct isp1760_qh *qh;
	u32 rl;
	u32 nakcount;

	done_map = isp1760_readl(usb_hcd->regs +
			HC_ATL_PTD_DONEMAP_REG);
	skip_map = isp1760_readl(usb_hcd->regs +
			HC_ATL_PTD_SKIPMAP_REG);

	or_map = isp1760_readl(usb_hcd->regs + HC_ATL_IRQ_MASK_OR_REG);
	or_map &= ~done_map;
	isp1760_writel(or_map, usb_hcd->regs + HC_ATL_IRQ_MASK_OR_REG);

	atl_regs_base = ATL_REGS_OFFSET;
	while (done_map) {
		u32 dw1;
		u32 dw2;
		u32 dw3;

		status = 0;

		queue_entry = __ffs(done_map);
		done_map &= ~(1 << queue_entry);
		skip_map |= 1 << queue_entry;

		atl_regs = atl_regs_base + queue_entry * sizeof(struct ptd);

		urb = priv->atl_ints[queue_entry].urb;
		qtd = priv->atl_ints[queue_entry].qtd;
		qh = priv->atl_ints[queue_entry].qh;
		payload = priv->atl_ints[queue_entry].payload;

		if (!qh) {
			printk(KERN_ERR "qh is 0\n");
			continue;
		}
		isp1760_writel(atl_regs + ISP_BANK(0), usb_hcd->regs +
				HC_MEMORY_REG);
		isp1760_writel(payload  + ISP_BANK(1), usb_hcd->regs +
				HC_MEMORY_REG);
		/*
		 * write bank1 address twice to ensure the 90ns delay (time
		 * between BANK0 write and the priv_read_copy() call is at
		 * least 3*t_WHWL + 2*t_w11 = 3*25ns + 2*17ns = 109ns)
		 */
		isp1760_writel(payload  + ISP_BANK(1), usb_hcd->regs +
				HC_MEMORY_REG);

		priv_read_copy(priv, (u32 *)&ptd, usb_hcd->regs + atl_regs +
				ISP_BANK(0), sizeof(ptd));

		dw1 = le32_to_cpu(ptd.dw1);
		dw2 = le32_to_cpu(ptd.dw2);
		dw3 = le32_to_cpu(ptd.dw3);
		rl = (dw2 >> 25) & 0x0f;
		nakcount = (dw3 >> 19) & 0xf;

		/* Transfer Error, *but* active and no HALT -> reload */
		if ((dw3 & DW3_ERROR_BIT) && (dw3 & DW3_QTD_ACTIVE) &&
				!(dw3 & DW3_HALT_BIT)) {

			/* according to ppriv code, we have to
			 * reload this one if trasfered bytes != requested bytes
			 * else act like everything went smooth..
			 * XXX This just doesn't feel right and hasn't
			 * triggered so far.
			 */

			length = PTD_XFERRED_LENGTH(dw3);
			printk(KERN_ERR "Should reload now.... transfered %d "
					"of %zu\n", length, qtd->length);
			BUG();
		}

		if (!nakcount && (dw3 & DW3_QTD_ACTIVE)) {
			u32 buffstatus;

			/* XXX
			 * NAKs are handled in HW by the chip. Usually if the
			 * device is not able to send data fast enough.
			 * This did not trigger for a long time now.
			 */
			printk(KERN_ERR "Reloading ptd %p/%p... qh %p readed: "
					"%d of %zu done: %08x cur: %08x\n", qtd,
					urb, qh, PTD_XFERRED_LENGTH(dw3),
					qtd->length, done_map,
					(1 << queue_entry));

			/* RL counter = ERR counter */
			dw3 &= ~(0xf << 19);
			dw3 |= rl << 19;
			dw3 &= ~(3 << (55 - 32));
			dw3 |= ERR_COUNTER << (55 - 32);

			/*
			 * It is not needed to write skip map back because it
			 * is unchanged. Just make sure that this entry is
			 * unskipped once it gets written to the HW.
			 */
			skip_map &= ~(1 << queue_entry);
			or_map = isp1760_readl(usb_hcd->regs +
					HC_ATL_IRQ_MASK_OR_REG);
			or_map |= 1 << queue_entry;
			isp1760_writel(or_map, usb_hcd->regs +
					HC_ATL_IRQ_MASK_OR_REG);

			ptd.dw3 = cpu_to_le32(dw3);
			priv_write_copy(priv, (u32 *)&ptd, usb_hcd->regs +
					atl_regs, sizeof(ptd));

			ptd.dw0 |= __constant_cpu_to_le32(PTD_VALID);
			priv_write_copy(priv, (u32 *)&ptd, usb_hcd->regs +
					atl_regs, sizeof(ptd));

			buffstatus = isp1760_readl(usb_hcd->regs +
					HC_BUFFER_STATUS_REG);
			buffstatus |= ATL_BUFFER;
			isp1760_writel(buffstatus, usb_hcd->regs +
					HC_BUFFER_STATUS_REG);
			continue;
		}

		error = check_error(&ptd);
		if (error) {
			status = error;
			priv->atl_ints[queue_entry].qh->toggle = 0;
			priv->atl_ints[queue_entry].qh->ping = 0;
			urb->status = -EPIPE;

#if 0
			printk(KERN_ERR "Error in %s().\n", __func__);
			printk(KERN_ERR "IN dw0: %08x dw1: %08x dw2: %08x "
					"dw3: %08x dw4: %08x dw5: %08x dw6: "
					"%08x dw7: %08x\n",
					ptd.dw0, ptd.dw1, ptd.dw2, ptd.dw3,
					ptd.dw4, ptd.dw5, ptd.dw6, ptd.dw7);
#endif
		} else {
			if (usb_pipetype(urb->pipe) == PIPE_BULK) {
				priv->atl_ints[queue_entry].qh->toggle = dw3 &
					(1 << 25);
				priv->atl_ints[queue_entry].qh->ping = dw3 &
					(1 << 26);
			}
		}

		length = PTD_XFERRED_LENGTH(dw3);
		if (length) {
			switch (DW1_GET_PID(dw1)) {
			case IN_PID:
				priv_read_copy(priv,
					priv->atl_ints[queue_entry].data_buffer,
					usb_hcd->regs + payload + ISP_BANK(1),
					length);

			case OUT_PID:

				urb->actual_length += length;

			case SETUP_PID:
				break;
			}
		}

		priv->atl_ints[queue_entry].data_buffer = NULL;
		priv->atl_ints[queue_entry].urb = NULL;
		priv->atl_ints[queue_entry].qtd = NULL;
		priv->atl_ints[queue_entry].qh = NULL;

		free_mem(priv, payload);

		isp1760_writel(skip_map, usb_hcd->regs +
				HC_ATL_PTD_SKIPMAP_REG);

		if (urb->status == -EPIPE) {
			/* HALT was received */

			qtd = clean_up_qtdlist(qtd);
			isp1760_urb_done(priv, urb, urb->status);

		} else if (usb_pipebulk(urb->pipe) && (length < qtd->length)) {
			/* short BULK received */

			if (urb->transfer_flags & URB_SHORT_NOT_OK) {
				urb->status = -EREMOTEIO;
				isp1760_dbg(priv, "short bulk, %d instead %zu "
					"with URB_SHORT_NOT_OK flag.\n",
					length, qtd->length);
			}

			if (urb->status == -EINPROGRESS)
				urb->status = 0;

			qtd = clean_up_qtdlist(qtd);

			isp1760_urb_done(priv, urb, urb->status);

		} else if (qtd->status & URB_COMPLETE_NOTIFY) {
			/* that was the last qtd of that URB */

			if (urb->status == -EINPROGRESS)
				urb->status = 0;

			qtd = clean_this_qtd(qtd);
			isp1760_urb_done(priv, urb, urb->status);

		} else {
			/* next QTD of this URB */

			qtd = clean_this_qtd(qtd);
			BUG_ON(!qtd);
		}

		if (qtd)
			enqueue_an_ATL_packet(usb_hcd, qh, qtd);

		skip_map = isp1760_readl(usb_hcd->regs +
				HC_ATL_PTD_SKIPMAP_REG);
	}
}

static void do_intl_int(struct usb_hcd *usb_hcd)
{
	struct isp1760_hcd *priv = hcd_to_priv(usb_hcd);
	u32 done_map, skip_map;
	struct ptd ptd;
	struct urb *urb = NULL;
	u32 int_regs;
	u32 int_regs_base;
	u32 payload;
	u32 length;
	u32 or_map;
	int error;
	u32 queue_entry;
	struct isp1760_qtd *qtd;
	struct isp1760_qh *qh;

	done_map = isp1760_readl(usb_hcd->regs +
			HC_INT_PTD_DONEMAP_REG);
	skip_map = isp1760_readl(usb_hcd->regs +
			HC_INT_PTD_SKIPMAP_REG);

	or_map = isp1760_readl(usb_hcd->regs + HC_INT_IRQ_MASK_OR_REG);
	or_map &= ~done_map;
	isp1760_writel(or_map, usb_hcd->regs + HC_INT_IRQ_MASK_OR_REG);

	int_regs_base = INT_REGS_OFFSET;

	while (done_map) {
		u32 dw1;
		u32 dw3;

		queue_entry = __ffs(done_map);
		done_map &= ~(1 << queue_entry);
		skip_map |= 1 << queue_entry;

		int_regs = int_regs_base + queue_entry * sizeof(struct ptd);
		urb = priv->int_ints[queue_entry].urb;
		qtd = priv->int_ints[queue_entry].qtd;
		qh = priv->int_ints[queue_entry].qh;
		payload = priv->int_ints[queue_entry].payload;

		if (!qh) {
			printk(KERN_ERR "(INT) qh is 0\n");
			continue;
		}

		isp1760_writel(int_regs + ISP_BANK(0), usb_hcd->regs +
				HC_MEMORY_REG);
		isp1760_writel(payload  + ISP_BANK(1), usb_hcd->regs +
				HC_MEMORY_REG);
		/*
		 * write bank1 address twice to ensure the 90ns delay (time
		 * between BANK0 write and the priv_read_copy() call is at
		 * least 3*t_WHWL + 2*t_w11 = 3*25ns + 2*17ns = 92ns)
		 */
		isp1760_writel(payload  + ISP_BANK(1), usb_hcd->regs +
				HC_MEMORY_REG);

		priv_read_copy(priv, (u32 *)&ptd, usb_hcd->regs + int_regs +
				ISP_BANK(0), sizeof(ptd));
		dw1 = le32_to_cpu(ptd.dw1);
		dw3 = le32_to_cpu(ptd.dw3);
		check_int_err_status(le32_to_cpu(ptd.dw4));

		error = check_error(&ptd);
		if (error) {
#if 0
			printk(KERN_ERR "Error in %s().\n", __func__);
			printk(KERN_ERR "IN dw0: %08x dw1: %08x dw2: %08x "
					"dw3: %08x dw4: %08x dw5: %08x dw6: "
					"%08x dw7: %08x\n",
					ptd.dw0, ptd.dw1, ptd.dw2, ptd.dw3,
					ptd.dw4, ptd.dw5, ptd.dw6, ptd.dw7);
#endif
			urb->status = -EPIPE;
			priv->int_ints[queue_entry].qh->toggle = 0;
			priv->int_ints[queue_entry].qh->ping = 0;

		} else {
			priv->int_ints[queue_entry].qh->toggle =
				dw3 & (1 << 25);
			priv->int_ints[queue_entry].qh->ping = dw3 & (1 << 26);
		}

		if (urb->dev->speed != USB_SPEED_HIGH)
			length = PTD_XFERRED_LENGTH_LO(dw3);
		else
			length = PTD_XFERRED_LENGTH(dw3);

		if (length) {
			switch (DW1_GET_PID(dw1)) {
			case IN_PID:
				priv_read_copy(priv,
					priv->int_ints[queue_entry].data_buffer,
					usb_hcd->regs + payload + ISP_BANK(1),
					length);
			case OUT_PID:

				urb->actual_length += length;

			case SETUP_PID:
				break;
			}
		}

		priv->int_ints[queue_entry].data_buffer = NULL;
		priv->int_ints[queue_entry].urb = NULL;
		priv->int_ints[queue_entry].qtd = NULL;
		priv->int_ints[queue_entry].qh = NULL;

		isp1760_writel(skip_map, usb_hcd->regs +
				HC_INT_PTD_SKIPMAP_REG);
		free_mem(priv, payload);

		if (urb->status == -EPIPE) {
			/* HALT received */

			 qtd = clean_up_qtdlist(qtd);
			 isp1760_urb_done(priv, urb, urb->status);

		} else if (qtd->status & URB_COMPLETE_NOTIFY) {

			if (urb->status == -EINPROGRESS)
				urb->status = 0;

			qtd = clean_this_qtd(qtd);
			isp1760_urb_done(priv, urb, urb->status);

		} else {
			/* next QTD of this URB */

			qtd = clean_this_qtd(qtd);
			BUG_ON(!qtd);
		}

		if (qtd)
			enqueue_an_INT_packet(usb_hcd, qh, qtd);

		skip_map = isp1760_readl(usb_hcd->regs +
				HC_INT_PTD_SKIPMAP_REG);
	}
}

#define max_packet(wMaxPacketSize) ((wMaxPacketSize) & 0x07ff)
static struct isp1760_qh *qh_make(struct isp1760_hcd *priv, struct urb *urb,
		gfp_t flags)
{
	struct isp1760_qh *qh;
	int is_input, type;

	qh = isp1760_qh_alloc(priv, flags);
	if (!qh)
		return qh;

	/*
	 * init endpoint/device data for this QH
	 */
	is_input = usb_pipein(urb->pipe);
	type = usb_pipetype(urb->pipe);

	if (type == PIPE_INTERRUPT) {

		if (urb->dev->speed == USB_SPEED_HIGH) {

			qh->period = urb->interval >> 3;
			if (qh->period == 0 && urb->interval != 1) {
				/* NOTE interval 2 or 4 uframes could work.
				 * But interval 1 scheduling is simpler, and
				 * includes high bandwidth.
				 */
				printk(KERN_ERR "intr period %d uframes, NYET!",
						urb->interval);
				qh_destroy(qh);
				return NULL;
			}
		} else {
			qh->period = urb->interval;
		}
	}

	/* support for tt scheduling, and access to toggles */
	qh->dev = urb->dev;

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
static struct isp1760_qh *qh_append_tds(struct isp1760_hcd *priv,
		struct urb *urb, struct list_head *qtd_list, int epnum,
		void **ptr)
{
	struct isp1760_qh *qh;
	struct isp1760_qtd *qtd;
	struct isp1760_qtd *prev_qtd;

	qh = (struct isp1760_qh *)*ptr;
	if (!qh) {
		/* can't sleep here, we have priv->lock... */
		qh = qh_make(priv, urb, GFP_ATOMIC);
		if (!qh)
			return qh;
		*ptr = qh;
	}

	qtd = list_entry(qtd_list->next, struct isp1760_qtd,
			qtd_list);
	if (!list_empty(&qh->qtd_list))
		prev_qtd = list_entry(qh->qtd_list.prev,
				struct isp1760_qtd, qtd_list);
	else
		prev_qtd = NULL;

	list_splice(qtd_list, qh->qtd_list.prev);
	if (prev_qtd) {
		BUG_ON(prev_qtd->hw_next);
		prev_qtd->hw_next = qtd;
	}

	urb->hcpriv = qh;
	return qh;
}

static void qtd_list_free(struct isp1760_hcd *priv, struct urb *urb,
		struct list_head *qtd_list)
{
	struct list_head *entry, *temp;

	list_for_each_safe(entry, temp, qtd_list) {
		struct isp1760_qtd	*qtd;

		qtd = list_entry(entry, struct isp1760_qtd, qtd_list);
		list_del(&qtd->qtd_list);
		isp1760_qtd_free(qtd);
	}
}

static int isp1760_prepare_enqueue(struct isp1760_hcd *priv, struct urb *urb,
		struct list_head *qtd_list, gfp_t mem_flags, packet_enqueue *p)
{
	struct isp1760_qtd         *qtd;
	int                     epnum;
	unsigned long           flags;
	struct isp1760_qh          *qh = NULL;
	int                     rc;
	int qh_busy;

	qtd = list_entry(qtd_list->next, struct isp1760_qtd, qtd_list);
	epnum = urb->ep->desc.bEndpointAddress;

	spin_lock_irqsave(&priv->lock, flags);
	if (!test_bit(HCD_FLAG_HW_ACCESSIBLE, &priv_to_hcd(priv)->flags)) {
		rc = -ESHUTDOWN;
		goto done;
	}
	rc = usb_hcd_link_urb_to_ep(priv_to_hcd(priv), urb);
	if (rc)
		goto done;

	qh = urb->ep->hcpriv;
	if (qh)
		qh_busy = !list_empty(&qh->qtd_list);
	else
		qh_busy = 0;

	qh = qh_append_tds(priv, urb, qtd_list, epnum, &urb->ep->hcpriv);
	if (!qh) {
		usb_hcd_unlink_urb_from_ep(priv_to_hcd(priv), urb);
		rc = -ENOMEM;
		goto done;
	}

	if (!qh_busy)
		p(priv_to_hcd(priv), qh, qtd);

done:
	spin_unlock_irqrestore(&priv->lock, flags);
	if (!qh)
		qtd_list_free(priv, urb, qtd_list);
	return rc;
}

static struct isp1760_qtd *isp1760_qtd_alloc(struct isp1760_hcd *priv,
		gfp_t flags)
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
static struct list_head *qh_urb_transaction(struct isp1760_hcd *priv,
		struct urb *urb, struct list_head *head, gfp_t flags)
{
	struct isp1760_qtd *qtd, *qtd_prev;
	void *buf;
	int len, maxpacket;
	int is_input;
	u32 token;

	/*
	 * URBs map to sequences of QTDs:  one logical transaction
	 */
	qtd = isp1760_qtd_alloc(priv, flags);
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
		token ^= DATA_TOGGLE;
		qtd_prev = qtd;
		qtd = isp1760_qtd_alloc(priv, flags);
		if (!qtd)
			goto cleanup;
		qtd->urb = urb;
		qtd_prev->hw_next = qtd;
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
			printk(KERN_ERR "buf is null, dma is %08lx len is %d\n",
					(long unsigned)urb->transfer_dma, len);
			WARN_ON(1);
		}

		this_qtd_len = qtd_fill(qtd, buf, len, token);
		len -= this_qtd_len;
		buf += this_qtd_len;

		/* qh makes control packets use qtd toggle; maybe switch it */
		if ((maxpacket & (this_qtd_len + (maxpacket - 1))) == 0)
			token ^= DATA_TOGGLE;

		if (len <= 0)
			break;

		qtd_prev = qtd;
		qtd = isp1760_qtd_alloc(priv, flags);
		if (!qtd)
			goto cleanup;
		qtd->urb = urb;
		qtd_prev->hw_next = qtd;
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
			/* force DATA1 */
			token |= DATA_TOGGLE;
		} else if (usb_pipebulk(urb->pipe)
				&& (urb->transfer_flags & URB_ZERO_PACKET)
				&& !(urb->transfer_buffer_length % maxpacket)) {
			one_more = 1;
		}
		if (one_more) {
			qtd_prev = qtd;
			qtd = isp1760_qtd_alloc(priv, flags);
			if (!qtd)
				goto cleanup;
			qtd->urb = urb;
			qtd_prev->hw_next = qtd;
			list_add_tail(&qtd->qtd_list, head);

			/* never any data in such packets */
			qtd_fill(qtd, NULL, 0, token);
		}
	}

	qtd->status = URB_COMPLETE_NOTIFY;
	return head;

cleanup:
	qtd_list_free(priv, urb, head);
	return NULL;
}

static int isp1760_urb_enqueue(struct usb_hcd *hcd, struct urb *urb,
		gfp_t mem_flags)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	struct list_head qtd_list;
	packet_enqueue *pe;

	INIT_LIST_HEAD(&qtd_list);

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
	case PIPE_BULK:

		if (!qh_urb_transaction(priv, urb, &qtd_list, mem_flags))
			return -ENOMEM;
		pe =  enqueue_an_ATL_packet;
		break;

	case PIPE_INTERRUPT:
		if (!qh_urb_transaction(priv, urb, &qtd_list, mem_flags))
			return -ENOMEM;
		pe = enqueue_an_INT_packet;
		break;

	case PIPE_ISOCHRONOUS:
		printk(KERN_ERR "PIPE_ISOCHRONOUS ain't supported\n");
	default:
		return -EPIPE;
	}

	return isp1760_prepare_enqueue(priv, urb, &qtd_list, mem_flags, pe);
}

static int isp1760_urb_dequeue(struct usb_hcd *hcd, struct urb *urb,
		int status)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	struct inter_packet_info *ints;
	u32 i;
	u32 reg_base, or_reg, skip_reg;
	unsigned long flags;
	struct ptd ptd;

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_ISOCHRONOUS:
		return -EPIPE;
		break;

	case PIPE_INTERRUPT:
		ints = priv->int_ints;
		reg_base = INT_REGS_OFFSET;
		or_reg = HC_INT_IRQ_MASK_OR_REG;
		skip_reg = HC_INT_PTD_SKIPMAP_REG;
		break;

	default:
		ints = priv->atl_ints;
		reg_base = ATL_REGS_OFFSET;
		or_reg = HC_ATL_IRQ_MASK_OR_REG;
		skip_reg = HC_ATL_PTD_SKIPMAP_REG;
		break;
	}

	memset(&ptd, 0, sizeof(ptd));
	spin_lock_irqsave(&priv->lock, flags);

	for (i = 0; i < 32; i++) {
		if (ints->urb == urb) {
			u32 skip_map;
			u32 or_map;
			struct isp1760_qtd *qtd;

			skip_map = isp1760_readl(hcd->regs + skip_reg);
			skip_map |= 1 << i;
			isp1760_writel(skip_map, hcd->regs + skip_reg);

			or_map = isp1760_readl(hcd->regs + or_reg);
			or_map &= ~(1 << i);
			isp1760_writel(or_map, hcd->regs + or_reg);

			priv_write_copy(priv, (u32 *)&ptd, hcd->regs + reg_base
					+ i * sizeof(ptd), sizeof(ptd));
			qtd = ints->qtd;

			clean_up_qtdlist(qtd);

			free_mem(priv, ints->payload);

			ints->urb = NULL;
			ints->qh = NULL;
			ints->qtd = NULL;
			ints->data_buffer = NULL;
			ints->payload = 0;

			isp1760_urb_done(priv, urb, status);
			break;
		}
		ints++;
	}

	spin_unlock_irqrestore(&priv->lock, flags);
	return 0;
}

static irqreturn_t isp1760_irq(struct usb_hcd *usb_hcd)
{
	struct isp1760_hcd *priv = hcd_to_priv(usb_hcd);
	u32 imask;
	irqreturn_t irqret = IRQ_NONE;

	spin_lock(&priv->lock);

	if (!(usb_hcd->state & HC_STATE_RUNNING))
		goto leave;

	imask = isp1760_readl(usb_hcd->regs + HC_INTERRUPT_REG);
	if (unlikely(!imask))
		goto leave;

	isp1760_writel(imask, usb_hcd->regs + HC_INTERRUPT_REG);
	if (imask & HC_ATL_INT)
		do_atl_int(usb_hcd);

	if (imask & HC_INTL_INT)
		do_intl_int(usb_hcd);

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
	temp = isp1760_readl(hcd->regs + HC_PORTSC1);

	if (temp & PORT_OWNER) {
		if (temp & PORT_CSC) {
			temp &= ~PORT_CSC;
			isp1760_writel(temp, hcd->regs + HC_PORTSC1);
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

	/* two bitmaps:  ports removable, and usb 1.0 legacy PortPwrCtrlMask */
	memset(&desc->bitmap[0], 0, temp);
	memset(&desc->bitmap[temp], 0xff, temp);

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

static int check_reset_complete(struct isp1760_hcd *priv, int index,
		u32 __iomem *status_reg, int port_status)
{
	if (!(port_status & PORT_CONNECT))
		return port_status;

	/* if reset finished and it's still not enabled -- handoff */
	if (!(port_status & PORT_PE)) {

		printk(KERN_ERR "port %d full speed --> companion\n",
			index + 1);

		port_status |= PORT_OWNER;
		port_status &= ~PORT_RWC_BITS;
		isp1760_writel(port_status, status_reg);

	} else
		printk(KERN_ERR "port %d high speed\n", index + 1);

	return port_status;
}

static int isp1760_hub_control(struct usb_hcd *hcd, u16 typeReq,
		u16 wValue, u16 wIndex, char *buf, u16 wLength)
{
	struct isp1760_hcd *priv = hcd_to_priv(hcd);
	int ports = HCS_N_PORTS(priv->hcs_params);
	u32 __iomem *status_reg = hcd->regs + HC_PORTSC1;
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
		temp = isp1760_readl(status_reg);

		/*
		 * Even if OWNER is set, so the port is owned by the
		 * companion controller, khubd needs to be able to clear
		 * the port-change status bits (especially
		 * USB_PORT_FEAT_C_CONNECTION).
		 */

		switch (wValue) {
		case USB_PORT_FEAT_ENABLE:
			isp1760_writel(temp & ~PORT_PE, status_reg);
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
				isp1760_writel(temp | PORT_RESUME,
						status_reg);
				priv->reset_done = jiffies +
					msecs_to_jiffies(20);
			}
			break;
		case USB_PORT_FEAT_C_SUSPEND:
			/* we auto-clear this feature */
			break;
		case USB_PORT_FEAT_POWER:
			if (HCS_PPC(priv->hcs_params))
				isp1760_writel(temp & ~PORT_POWER, status_reg);
			break;
		case USB_PORT_FEAT_C_CONNECTION:
			isp1760_writel(temp | PORT_CSC,
					status_reg);
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
		isp1760_readl(hcd->regs + HC_USBCMD);
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
		temp = isp1760_readl(status_reg);

		/* wPortChange bits */
		if (temp & PORT_CSC)
			status |= 1 << USB_PORT_FEAT_C_CONNECTION;


		/* whoever resumes must GetPortStatus to complete it!! */
		if (temp & PORT_RESUME) {
			printk(KERN_ERR "Port resume should be skipped.\n");

			/* Remote Wakeup received? */
			if (!priv->reset_done) {
				/* resume signaling for 20 msec */
				priv->reset_done = jiffies
						+ msecs_to_jiffies(20);
				/* check the port again */
				mod_timer(&priv_to_hcd(priv)->rh_timer,
						priv->reset_done);
			}

			/* resume completed? */
			else if (time_after_eq(jiffies,
					priv->reset_done)) {
				status |= 1 << USB_PORT_FEAT_C_SUSPEND;
				priv->reset_done = 0;

				/* stop resume signaling */
				temp = isp1760_readl(status_reg);
				isp1760_writel(
					temp & ~(PORT_RWC_BITS | PORT_RESUME),
					status_reg);
				retval = handshake(priv, status_reg,
					   PORT_RESUME, 0, 2000 /* 2msec */);
				if (retval != 0) {
					isp1760_err(priv,
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
			status |= 1 << USB_PORT_FEAT_C_RESET;
			priv->reset_done = 0;

			/* force reset to complete */
			isp1760_writel(temp & ~PORT_RESET,
					status_reg);
			/* REVISIT:  some hardware needs 550+ usec to clear
			 * this bit; seems too long to spin routinely...
			 */
			retval = handshake(priv, status_reg,
					PORT_RESET, 0, 750);
			if (retval != 0) {
				isp1760_err(priv, "port %d reset error %d\n",
						wIndex + 1, retval);
				goto error;
			}

			/* see what we found out */
			temp = check_reset_complete(priv, wIndex, status_reg,
					isp1760_readl(status_reg));
		}
		/*
		 * Even if OWNER is set, there's no harm letting khubd
		 * see the wPortStatus values (they should all be 0 except
		 * for PORT_POWER anyway).
		 */

		if (temp & PORT_OWNER)
			printk(KERN_ERR "Warning: PORT_OWNER is set\n");

		if (temp & PORT_CONNECT) {
			status |= 1 << USB_PORT_FEAT_CONNECTION;
			/* status may be from integrated TT */
			status |= ehci_port_speed(priv, temp);
		}
		if (temp & PORT_PE)
			status |= 1 << USB_PORT_FEAT_ENABLE;
		if (temp & (PORT_SUSPEND|PORT_RESUME))
			status |= 1 << USB_PORT_FEAT_SUSPEND;
		if (temp & PORT_RESET)
			status |= 1 << USB_PORT_FEAT_RESET;
		if (temp & PORT_POWER)
			status |= 1 << USB_PORT_FEAT_POWER;

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
		temp = isp1760_readl(status_reg);
		if (temp & PORT_OWNER)
			break;

/*		temp &= ~PORT_RWC_BITS; */
		switch (wValue) {
		case USB_PORT_FEAT_ENABLE:
			isp1760_writel(temp | PORT_PE, status_reg);
			break;

		case USB_PORT_FEAT_SUSPEND:
			if ((temp & PORT_PE) == 0
					|| (temp & PORT_RESET) != 0)
				goto error;

			isp1760_writel(temp | PORT_SUSPEND, status_reg);
			break;
		case USB_PORT_FEAT_POWER:
			if (HCS_PPC(priv->hcs_params))
				isp1760_writel(temp | PORT_POWER,
						status_reg);
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
			isp1760_writel(temp, status_reg);
			break;
		default:
			goto error;
		}
		isp1760_readl(hcd->regs + HC_USBCMD);
		break;

	default:
error:
		/* "stall" on error */
		retval = -EPIPE;
	}
	spin_unlock_irqrestore(&priv->lock, flags);
	return retval;
}

static void isp1760_endpoint_disable(struct usb_hcd *usb_hcd,
		struct usb_host_endpoint *ep)
{
	struct isp1760_hcd *priv = hcd_to_priv(usb_hcd);
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
			isp1760_urb_dequeue(usb_hcd, qtd->urb, -ECONNRESET);
			spin_lock_irqsave(&priv->lock, flags);
		} else {
			struct urb *urb;

			urb = qtd->urb;
			clean_up_qtdlist(qtd);
			isp1760_urb_done(priv, urb, -ECONNRESET);
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

	fr = isp1760_readl(hcd->regs + HC_FRINDEX);
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
	ehci_reset(priv);
	/* Disable IRQ */
	temp = isp1760_readl(hcd->regs + HC_HW_MODE_CTRL);
	isp1760_writel(temp &= ~HW_GLOBAL_INTR_EN, hcd->regs + HC_HW_MODE_CTRL);
	spin_unlock_irq(&priv->lock);

	isp1760_writel(0, hcd->regs + HC_CONFIGFLAG);
}

static void isp1760_shutdown(struct usb_hcd *hcd)
{
	u32 command, temp;

	isp1760_stop(hcd);
	temp = isp1760_readl(hcd->regs + HC_HW_MODE_CTRL);
	isp1760_writel(temp &= ~HW_GLOBAL_INTR_EN, hcd->regs + HC_HW_MODE_CTRL);

	command = isp1760_readl(hcd->regs + HC_USBCMD);
	command &= ~CMD_RUN;
	isp1760_writel(command, hcd->regs + HC_USBCMD);
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

struct usb_hcd *isp1760_register(u64 res_start, u64 res_len, int irq,
		u64 irqflags, struct device *dev, const char *busname,
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
