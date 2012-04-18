/*
 *  sx8.c: Driver for Promise SATA SX8 looks-like-I2O hardware
 *
 *  Copyright 2004-2005 Red Hat, Inc.
 *
 *  Author/maintainer:  Jeff Garzik <jgarzik@pobox.com>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file "COPYING" in the main directory of this archive
 *  for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/compiler.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/hdreg.h>
#include <linux/dma-mapping.h>
#include <linux/completion.h>
#include <linux/scatterlist.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#if 0
#define CARM_DEBUG
#define CARM_VERBOSE_DEBUG
#else
#undef CARM_DEBUG
#undef CARM_VERBOSE_DEBUG
#endif
#undef CARM_NDEBUG

#define DRV_NAME "sx8"
#define DRV_VERSION "1.0"
#define PFX DRV_NAME ": "

MODULE_AUTHOR("Jeff Garzik");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Promise SATA SX8 block driver");
MODULE_VERSION(DRV_VERSION);

/*
 * SX8 hardware has a single message queue for all ATA ports.
 * When this driver was written, the hardware (firmware?) would
 * corrupt data eventually, if more than one request was outstanding.
 * As one can imagine, having 8 ports bottlenecking on a single
 * command hurts performance.
 *
 * Based on user reports, later versions of the hardware (firmware?)
 * seem to be able to survive with more than one command queued.
 *
 * Therefore, we default to the safe option -- 1 command -- but
 * allow the user to increase this.
 *
 * SX8 should be able to support up to ~60 queued commands (CARM_MAX_REQ),
 * but problems seem to occur when you exceed ~30, even on newer hardware.
 */
static int max_queue = 1;
module_param(max_queue, int, 0444);
MODULE_PARM_DESC(max_queue, "Maximum number of queued commands. (min==1, max==30, safe==1)");


#define NEXT_RESP(idx)	((idx + 1) % RMSG_Q_LEN)

/* 0xf is just arbitrary, non-zero noise; this is sorta like poisoning */
#define TAG_ENCODE(tag)	(((tag) << 16) | 0xf)
#define TAG_DECODE(tag)	(((tag) >> 16) & 0x1f)
#define TAG_VALID(tag)	((((tag) & 0xf) == 0xf) && (TAG_DECODE(tag) < 32))

/* note: prints function name for you */
#ifdef CARM_DEBUG
#define DPRINTK(fmt, args...) printk(KERN_ERR "%s: " fmt, __func__, ## args)
#ifdef CARM_VERBOSE_DEBUG
#define VPRINTK(fmt, args...) printk(KERN_ERR "%s: " fmt, __func__, ## args)
#else
#define VPRINTK(fmt, args...)
#endif	/* CARM_VERBOSE_DEBUG */
#else
#define DPRINTK(fmt, args...)
#define VPRINTK(fmt, args...)
#endif	/* CARM_DEBUG */

#ifdef CARM_NDEBUG
#define assert(expr)
#else
#define assert(expr) \
        if(unlikely(!(expr))) {                                   \
        printk(KERN_ERR "Assertion failed! %s,%s,%s,line=%d\n", \
	#expr, __FILE__, __func__, __LINE__);          \
        }
#endif

/* defines only for the constants which don't work well as enums */
struct carm_host;

enum {
	/* adapter-wide limits */
	CARM_MAX_PORTS		= 8,
	CARM_SHM_SIZE		= (4096 << 7),
	CARM_MINORS_PER_MAJOR	= 256 / CARM_MAX_PORTS,
	CARM_MAX_WAIT_Q		= CARM_MAX_PORTS + 1,

	/* command message queue limits */
	CARM_MAX_REQ		= 64,	       /* max command msgs per host */
	CARM_MSG_LOW_WATER	= (CARM_MAX_REQ / 4),	     /* refill mark */

	/* S/G limits, host-wide and per-request */
	CARM_MAX_REQ_SG		= 32,	     /* max s/g entries per request */
	CARM_MAX_HOST_SG	= 600,		/* max s/g entries per host */
	CARM_SG_LOW_WATER	= (CARM_MAX_HOST_SG / 4),   /* re-fill mark */

	/* hardware registers */
	CARM_IHQP		= 0x1c,
	CARM_INT_STAT		= 0x10, /* interrupt status */
	CARM_INT_MASK		= 0x14, /* interrupt mask */
	CARM_HMUC		= 0x18, /* host message unit control */
	RBUF_ADDR_LO		= 0x20, /* response msg DMA buf low 32 bits */
	RBUF_ADDR_HI		= 0x24, /* response msg DMA buf high 32 bits */
	RBUF_BYTE_SZ		= 0x28,
	CARM_RESP_IDX		= 0x2c,
	CARM_CMS0		= 0x30, /* command message size reg 0 */
	CARM_LMUC		= 0x48,
	CARM_HMPHA		= 0x6c,
	CARM_INITC		= 0xb5,

	/* bits in CARM_INT_{STAT,MASK} */
	INT_RESERVED		= 0xfffffff0,
	INT_WATCHDOG		= (1 << 3),	/* watchdog timer */
	INT_Q_OVERFLOW		= (1 << 2),	/* cmd msg q overflow */
	INT_Q_AVAILABLE		= (1 << 1),	/* cmd msg q has free space */
	INT_RESPONSE		= (1 << 0),	/* response msg available */
	INT_ACK_MASK		= INT_WATCHDOG | INT_Q_OVERFLOW,
	INT_DEF_MASK		= INT_RESERVED | INT_Q_OVERFLOW |
				  INT_RESPONSE,

	/* command messages, and related register bits */
	CARM_HAVE_RESP		= 0x01,
	CARM_MSG_READ		= 1,
	CARM_MSG_WRITE		= 2,
	CARM_MSG_VERIFY		= 3,
	CARM_MSG_GET_CAPACITY	= 4,
	CARM_MSG_FLUSH		= 5,
	CARM_MSG_IOCTL		= 6,
	CARM_MSG_ARRAY		= 8,
	CARM_MSG_MISC		= 9,
	CARM_CME		= (1 << 2),
	CARM_RME		= (1 << 1),
	CARM_WZBC		= (1 << 0),
	CARM_RMI		= (1 << 0),
	CARM_Q_FULL		= (1 << 3),
	CARM_MSG_SIZE		= 288,
	CARM_Q_LEN		= 48,

	/* CARM_MSG_IOCTL messages */
	CARM_IOC_SCAN_CHAN	= 5,	/* scan channels for devices */
	CARM_IOC_GET_TCQ	= 13,	/* get tcq/ncq depth */
	CARM_IOC_SET_TCQ	= 14,	/* set tcq/ncq depth */

	IOC_SCAN_CHAN_NODEV	= 0x1f,
	IOC_SCAN_CHAN_OFFSET	= 0x40,

	/* CARM_MSG_ARRAY messages */
	CARM_ARRAY_INFO		= 0,

	ARRAY_NO_EXIST		= (1 << 31),

	/* response messages */
	RMSG_SZ			= 8,	/* sizeof(struct carm_response) */
	RMSG_Q_LEN		= 48,	/* resp. msg list length */
	RMSG_OK			= 1,	/* bit indicating msg was successful */
					/* length of entire resp. msg buffer */
	RBUF_LEN		= RMSG_SZ * RMSG_Q_LEN,

	PDC_SHM_SIZE		= (4096 << 7), /* length of entire h/w buffer */

	/* CARM_MSG_MISC messages */
	MISC_GET_FW_VER		= 2,
	MISC_ALLOC_MEM		= 3,
	MISC_SET_TIME		= 5,

	/* MISC_GET_FW_VER feature bits */
	FW_VER_4PORT		= (1 << 2), /* 1=4 ports, 0=8 ports */
	FW_VER_NON_RAID		= (1 << 1), /* 1=non-RAID firmware, 0=RAID */
	FW_VER_ZCR		= (1 << 0), /* zero channel RAID (whatever that is) */

	/* carm_host flags */
	FL_NON_RAID		= FW_VER_NON_RAID,
	FL_4PORT		= FW_VER_4PORT,
	FL_FW_VER_MASK		= (FW_VER_NON_RAID | FW_VER_4PORT),
	FL_DAC			= (1 << 16),
	FL_DYN_MAJOR		= (1 << 17),
};

enum {
	CARM_SG_BOUNDARY	= 0xffffUL,	    /* s/g segment boundary */
};

enum scatter_gather_types {
	SGT_32BIT		= 0,
	SGT_64BIT		= 1,
};

enum host_states {
	HST_INVALID,		/* invalid state; never used */
	HST_ALLOC_BUF,		/* setting up master SHM area */
	HST_ERROR,		/* we never leave here */
	HST_PORT_SCAN,		/* start dev scan */
	HST_DEV_SCAN_START,	/* start per-device probe */
	HST_DEV_SCAN,		/* continue per-device probe */
	HST_DEV_ACTIVATE,	/* activate devices we found */
	HST_PROBE_FINISHED,	/* probe is complete */
	HST_PROBE_START,	/* initiate probe */
	HST_SYNC_TIME,		/* tell firmware what time it is */
	HST_GET_FW_VER,		/* get firmware version, adapter port cnt */
};

#ifdef CARM_DEBUG
static const char *state_name[] = {
	"HST_INVALID",
	"HST_ALLOC_BUF",
	"HST_ERROR",
	"HST_PORT_SCAN",
	"HST_DEV_SCAN_START",
	"HST_DEV_SCAN",
	"HST_DEV_ACTIVATE",
	"HST_PROBE_FINISHED",
	"HST_PROBE_START",
	"HST_SYNC_TIME",
	"HST_GET_FW_VER",
};
#endif

struct carm_port {
	unsigned int			port_no;
	struct gendisk			*disk;
	struct carm_host		*host;

	/* attached device characteristics */
	u64				capacity;
	char				name[41];
	u16				dev_geom_head;
	u16				dev_geom_sect;
	u16				dev_geom_cyl;
};

struct carm_request {
	unsigned int			tag;
	int				n_elem;
	unsigned int			msg_type;
	unsigned int			msg_subtype;
	unsigned int			msg_bucket;
	struct request			*rq;
	struct carm_port		*port;
	struct scatterlist		sg[CARM_MAX_REQ_SG];
};

struct carm_host {
	unsigned long			flags;
	void				__iomem *mmio;
	void				*shm;
	dma_addr_t			shm_dma;

	int				major;
	int				id;
	char				name[32];

	spinlock_t			lock;
	struct pci_dev			*pdev;
	unsigned int			state;
	u32				fw_ver;

	struct request_queue		*oob_q;
	unsigned int			n_oob;

	unsigned int			hw_sg_used;

	unsigned int			resp_idx;

	unsigned int			wait_q_prod;
	unsigned int			wait_q_cons;
	struct request_queue		*wait_q[CARM_MAX_WAIT_Q];

	unsigned int			n_msgs;
	u64				msg_alloc;
	struct carm_request		req[CARM_MAX_REQ];
	void				*msg_base;
	dma_addr_t			msg_dma;

	int				cur_scan_dev;
	unsigned long			dev_active;
	unsigned long			dev_present;
	struct carm_port		port[CARM_MAX_PORTS];

	struct work_struct		fsm_task;

	struct completion		probe_comp;
};

struct carm_response {
	__le32 ret_handle;
	__le32 status;
}  __attribute__((packed));

struct carm_msg_sg {
	__le32 start;
	__le32 len;
}  __attribute__((packed));

struct carm_msg_rw {
	u8 type;
	u8 id;
	u8 sg_count;
	u8 sg_type;
	__le32 handle;
	__le32 lba;
	__le16 lba_count;
	__le16 lba_high;
	struct carm_msg_sg sg[32];
}  __attribute__((packed));

struct carm_msg_allocbuf {
	u8 type;
	u8 subtype;
	u8 n_sg;
	u8 sg_type;
	__le32 handle;
	__le32 addr;
	__le32 len;
	__le32 evt_pool;
	__le32 n_evt;
	__le32 rbuf_pool;
	__le32 n_rbuf;
	__le32 msg_pool;
	__le32 n_msg;
	struct carm_msg_sg sg[8];
}  __attribute__((packed));

struct carm_msg_ioctl {
	u8 type;
	u8 subtype;
	u8 array_id;
	u8 reserved1;
	__le32 handle;
	__le32 data_addr;
	u32 reserved2;
}  __attribute__((packed));

struct carm_msg_sync_time {
	u8 type;
	u8 subtype;
	u16 reserved1;
	__le32 handle;
	u32 reserved2;
	__le32 timestamp;
}  __attribute__((packed));

struct carm_msg_get_fw_ver {
	u8 type;
	u8 subtype;
	u16 reserved1;
	__le32 handle;
	__le32 data_addr;
	u32 reserved2;
}  __attribute__((packed));

struct carm_fw_ver {
	__le32 version;
	u8 features;
	u8 reserved1;
	u16 reserved2;
}  __attribute__((packed));

struct carm_array_info {
	__le32 size;

	__le16 size_hi;
	__le16 stripe_size;

	__le32 mode;

	__le16 stripe_blk_sz;
	__le16 reserved1;

	__le16 cyl;
	__le16 head;

	__le16 sect;
	u8 array_id;
	u8 reserved2;

	char name[40];

	__le32 array_status;

	/* device list continues beyond this point? */
}  __attribute__((packed));

static int carm_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static void carm_remove_one (struct pci_dev *pdev);
static int carm_bdev_getgeo(struct block_device *bdev, struct hd_geometry *geo);

static const struct pci_device_id carm_pci_tbl[] = {
	{ PCI_VENDOR_ID_PROMISE, 0x8000, PCI_ANY_ID, PCI_ANY_ID, 0, 0, },
	{ PCI_VENDOR_ID_PROMISE, 0x8002, PCI_ANY_ID, PCI_ANY_ID, 0, 0, },
	{ }	/* terminate list */
};
MODULE_DEVICE_TABLE(pci, carm_pci_tbl);

static struct pci_driver carm_driver = {
	.name		= DRV_NAME,
	.id_table	= carm_pci_tbl,
	.probe		= carm_init_one,
	.remove		= carm_remove_one,
};

static const struct block_device_operations carm_bd_ops = {
	.owner		= THIS_MODULE,
	.getgeo		= carm_bdev_getgeo,
};

static unsigned int carm_host_id;
static unsigned long carm_major_alloc;



static int carm_bdev_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct carm_port *port = bdev->bd_disk->private_data;

	geo->heads = (u8) port->dev_geom_head;
	geo->sectors = (u8) port->dev_geom_sect;
	geo->cylinders = port->dev_geom_cyl;
	return 0;
}

static const u32 msg_sizes[] = { 32, 64, 128, CARM_MSG_SIZE };

static inline int carm_lookup_bucket(u32 msg_size)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(msg_sizes); i++)
		if (msg_size <= msg_sizes[i])
			return i;

	return -ENOENT;
}

static void carm_init_buckets(void __iomem *mmio)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(msg_sizes); i++)
		writel(msg_sizes[i], mmio + CARM_CMS0 + (4 * i));
}

static inline void *carm_ref_msg(struct carm_host *host,
				 unsigned int msg_idx)
{
	return host->msg_base + (msg_idx * CARM_MSG_SIZE);
}

static inline dma_addr_t carm_ref_msg_dma(struct carm_host *host,
					  unsigned int msg_idx)
{
	return host->msg_dma + (msg_idx * CARM_MSG_SIZE);
}

static int carm_send_msg(struct carm_host *host,
			 struct carm_request *crq)
{
	void __iomem *mmio = host->mmio;
	u32 msg = (u32) carm_ref_msg_dma(host, crq->tag);
	u32 cm_bucket = crq->msg_bucket;
	u32 tmp;
	int rc = 0;

	VPRINTK("ENTER\n");

	tmp = readl(mmio + CARM_HMUC);
	if (tmp & CARM_Q_FULL) {
#if 0
		tmp = readl(mmio + CARM_INT_MASK);
		tmp |= INT_Q_AVAILABLE;
		writel(tmp, mmio + CARM_INT_MASK);
		readl(mmio + CARM_INT_MASK);	/* flush */
#endif
		DPRINTK("host msg queue full\n");
		rc = -EBUSY;
	} else {
		writel(msg | (cm_bucket << 1), mmio + CARM_IHQP);
		readl(mmio + CARM_IHQP);	/* flush */
	}

	return rc;
}

static struct carm_request *carm_get_request(struct carm_host *host)
{
	unsigned int i;

	/* obey global hardware limit on S/G entries */
	if (host->hw_sg_used >= (CARM_MAX_HOST_SG - CARM_MAX_REQ_SG))
		return NULL;

	for (i = 0; i < max_queue; i++)
		if ((host->msg_alloc & (1ULL << i)) == 0) {
			struct carm_request *crq = &host->req[i];
			crq->port = NULL;
			crq->n_elem = 0;

			host->msg_alloc |= (1ULL << i);
			host->n_msgs++;

			assert(host->n_msgs <= CARM_MAX_REQ);
			sg_init_table(crq->sg, CARM_MAX_REQ_SG);
			return crq;
		}

	DPRINTK("no request available, returning NULL\n");
	return NULL;
}

static int carm_put_request(struct carm_host *host, struct carm_request *crq)
{
	assert(crq->tag < max_queue);

	if (unlikely((host->msg_alloc & (1ULL << crq->tag)) == 0))
		return -EINVAL; /* tried to clear a tag that was not active */

	assert(host->hw_sg_used >= crq->n_elem);

	host->msg_alloc &= ~(1ULL << crq->tag);
	host->hw_sg_used -= crq->n_elem;
	host->n_msgs--;

	return 0;
}

static struct carm_request *carm_get_special(struct carm_host *host)
{
	unsigned long flags;
	struct carm_request *crq = NULL;
	struct request *rq;
	int tries = 5000;

	while (tries-- > 0) {
		spin_lock_irqsave(&host->lock, flags);
		crq = carm_get_request(host);
		spin_unlock_irqrestore(&host->lock, flags);

		if (crq)
			break;
		msleep(10);
	}

	if (!crq)
		return NULL;

	rq = blk_get_request(host->oob_q, WRITE /* bogus */, GFP_KERNEL);
	if (!rq) {
		spin_lock_irqsave(&host->lock, flags);
		carm_put_request(host, crq);
		spin_unlock_irqrestore(&host->lock, flags);
		return NULL;
	}

	crq->rq = rq;
	return crq;
}

static int carm_array_info (struct carm_host *host, unsigned int array_idx)
{
	struct carm_msg_ioctl *ioc;
	unsigned int idx;
	u32 msg_data;
	dma_addr_t msg_dma;
	struct carm_request *crq;
	int rc;

	crq = carm_get_special(host);
	if (!crq) {
		rc = -ENOMEM;
		goto err_out;
	}

	idx = crq->tag;

	ioc = carm_ref_msg(host, idx);
	msg_dma = carm_ref_msg_dma(host, idx);
	msg_data = (u32) (msg_dma + sizeof(struct carm_array_info));

	crq->msg_type = CARM_MSG_ARRAY;
	crq->msg_subtype = CARM_ARRAY_INFO;
	rc = carm_lookup_bucket(sizeof(struct carm_msg_ioctl) +
				sizeof(struct carm_array_info));
	BUG_ON(rc < 0);
	crq->msg_bucket = (u32) rc;

	memset(ioc, 0, sizeof(*ioc));
	ioc->type	= CARM_MSG_ARRAY;
	ioc->subtype	= CARM_ARRAY_INFO;
	ioc->array_id	= (u8) array_idx;
	ioc->handle	= cpu_to_le32(TAG_ENCODE(idx));
	ioc->data_addr	= cpu_to_le32(msg_data);

	spin_lock_irq(&host->lock);
	assert(host->state == HST_DEV_SCAN_START ||
	       host->state == HST_DEV_SCAN);
	spin_unlock_irq(&host->lock);

	DPRINTK("blk_execute_rq_nowait, tag == %u\n", idx);
	crq->rq->cmd_type = REQ_TYPE_SPECIAL;
	crq->rq->special = crq;
	blk_execute_rq_nowait(host->oob_q, NULL, crq->rq, true, NULL);

	return 0;

err_out:
	spin_lock_irq(&host->lock);
	host->state = HST_ERROR;
	spin_unlock_irq(&host->lock);
	return rc;
}

typedef unsigned int (*carm_sspc_t)(struct carm_host *, unsigned int, void *);

static int carm_send_special (struct carm_host *host, carm_sspc_t func)
{
	struct carm_request *crq;
	struct carm_msg_ioctl *ioc;
	void *mem;
	unsigned int idx, msg_size;
	int rc;

	crq = carm_get_special(host);
	if (!crq)
		return -ENOMEM;

	idx = crq->tag;

	mem = carm_ref_msg(host, idx);

	msg_size = func(host, idx, mem);

	ioc = mem;
	crq->msg_type = ioc->type;
	crq->msg_subtype = ioc->subtype;
	rc = carm_lookup_bucket(msg_size);
	BUG_ON(rc < 0);
	crq->msg_bucket = (u32) rc;

	DPRINTK("blk_execute_rq_nowait, tag == %u\n", idx);
	crq->rq->cmd_type = REQ_TYPE_SPECIAL;
	crq->rq->special = crq;
	blk_execute_rq_nowait(host->oob_q, NULL, crq->rq, true, NULL);

	return 0;
}

static unsigned int carm_fill_sync_time(struct carm_host *host,
					unsigned int idx, void *mem)
{
	struct timeval tv;
	struct carm_msg_sync_time *st = mem;

	do_gettimeofday(&tv);

	memset(st, 0, sizeof(*st));
	st->type	= CARM_MSG_MISC;
	st->subtype	= MISC_SET_TIME;
	st->handle	= cpu_to_le32(TAG_ENCODE(idx));
	st->timestamp	= cpu_to_le32(tv.tv_sec);

	return sizeof(struct carm_msg_sync_time);
}

static unsigned int carm_fill_alloc_buf(struct carm_host *host,
					unsigned int idx, void *mem)
{
	struct carm_msg_allocbuf *ab = mem;

	memset(ab, 0, sizeof(*ab));
	ab->type	= CARM_MSG_MISC;
	ab->subtype	= MISC_ALLOC_MEM;
	ab->handle	= cpu_to_le32(TAG_ENCODE(idx));
	ab->n_sg	= 1;
	ab->sg_type	= SGT_32BIT;
	ab->addr	= cpu_to_le32(host->shm_dma + (PDC_SHM_SIZE >> 1));
	ab->len		= cpu_to_le32(PDC_SHM_SIZE >> 1);
	ab->evt_pool	= cpu_to_le32(host->shm_dma + (16 * 1024));
	ab->n_evt	= cpu_to_le32(1024);
	ab->rbuf_pool	= cpu_to_le32(host->shm_dma);
	ab->n_rbuf	= cpu_to_le32(RMSG_Q_LEN);
	ab->msg_pool	= cpu_to_le32(host->shm_dma + RBUF_LEN);
	ab->n_msg	= cpu_to_le32(CARM_Q_LEN);
	ab->sg[0].start	= cpu_to_le32(host->shm_dma + (PDC_SHM_SIZE >> 1));
	ab->sg[0].len	= cpu_to_le32(65536);

	return sizeof(struct carm_msg_allocbuf);
}

static unsigned int carm_fill_scan_channels(struct carm_host *host,
					    unsigned int idx, void *mem)
{
	struct carm_msg_ioctl *ioc = mem;
	u32 msg_data = (u32) (carm_ref_msg_dma(host, idx) +
			      IOC_SCAN_CHAN_OFFSET);

	memset(ioc, 0, sizeof(*ioc));
	ioc->type	= CARM_MSG_IOCTL;
	ioc->subtype	= CARM_IOC_SCAN_CHAN;
	ioc->handle	= cpu_to_le32(TAG_ENCODE(idx));
	ioc->data_addr	= cpu_to_le32(msg_data);

	/* fill output data area with "no device" default values */
	mem += IOC_SCAN_CHAN_OFFSET;
	memset(mem, IOC_SCAN_CHAN_NODEV, CARM_MAX_PORTS);

	return IOC_SCAN_CHAN_OFFSET + CARM_MAX_PORTS;
}

static unsigned int carm_fill_get_fw_ver(struct carm_host *host,
					 unsigned int idx, void *mem)
{
	struct carm_msg_get_fw_ver *ioc = mem;
	u32 msg_data = (u32) (carm_ref_msg_dma(host, idx) + sizeof(*ioc));

	memset(ioc, 0, sizeof(*ioc));
	ioc->type	= CARM_MSG_MISC;
	ioc->subtype	= MISC_GET_FW_VER;
	ioc->handle	= cpu_to_le32(TAG_ENCODE(idx));
	ioc->data_addr	= cpu_to_le32(msg_data);

	return sizeof(struct carm_msg_get_fw_ver) +
	       sizeof(struct carm_fw_ver);
}

static inline void carm_end_request_queued(struct carm_host *host,
					   struct carm_request *crq,
					   int error)
{
	struct request *req = crq->rq;
	int rc;

	__blk_end_request_all(req, error);

	rc = carm_put_request(host, crq);
	assert(rc == 0);
}

static inline void carm_push_q (struct carm_host *host, struct request_queue *q)
{
	unsigned int idx = host->wait_q_prod % CARM_MAX_WAIT_Q;

	blk_stop_queue(q);
	VPRINTK("STOPPED QUEUE %p\n", q);

	host->wait_q[idx] = q;
	host->wait_q_prod++;
	BUG_ON(host->wait_q_prod == host->wait_q_cons); /* overrun */
}

static inline struct request_queue *carm_pop_q(struct carm_host *host)
{
	unsigned int idx;

	if (host->wait_q_prod == host->wait_q_cons)
		return NULL;

	idx = host->wait_q_cons % CARM_MAX_WAIT_Q;
	host->wait_q_cons++;

	return host->wait_q[idx];
}

static inline void carm_round_robin(struct carm_host *host)
{
	struct request_queue *q = carm_pop_q(host);
	if (q) {
		blk_start_queue(q);
		VPRINTK("STARTED QUEUE %p\n", q);
	}
}

static inline void carm_end_rq(struct carm_host *host, struct carm_request *crq,
			       int error)
{
	carm_end_request_queued(host, crq, error);
	if (max_queue == 1)
		carm_round_robin(host);
	else if ((host->n_msgs <= CARM_MSG_LOW_WATER) &&
		 (host->hw_sg_used <= CARM_SG_LOW_WATER)) {
		carm_round_robin(host);
	}
}

static void carm_oob_rq_fn(struct request_queue *q)
{
	struct carm_host *host = q->queuedata;
	struct carm_request *crq;
	struct request *rq;
	int rc;

	while (1) {
		DPRINTK("get req\n");
		rq = blk_fetch_request(q);
		if (!rq)
			break;

		crq = rq->special;
		assert(crq != NULL);
		assert(crq->rq == rq);

		crq->n_elem = 0;

		DPRINTK("send req\n");
		rc = carm_send_msg(host, crq);
		if (rc) {
			blk_requeue_request(q, rq);
			carm_push_q(host, q);
			return;		/* call us again later, eventually */
		}
	}
}

static void carm_rq_fn(struct request_queue *q)
{
	struct carm_port *port = q->queuedata;
	struct carm_host *host = port->host;
	struct carm_msg_rw *msg;
	struct carm_request *crq;
	struct request *rq;
	struct scatterlist *sg;
	int writing = 0, pci_dir, i, n_elem, rc;
	u32 tmp;
	unsigned int msg_size;

queue_one_request:
	VPRINTK("get req\n");
	rq = blk_peek_request(q);
	if (!rq)
		return;

	crq = carm_get_request(host);
	if (!crq) {
		carm_push_q(host, q);
		return;		/* call us again later, eventually */
	}
	crq->rq = rq;

	blk_start_request(rq);

	if (rq_data_dir(rq) == WRITE) {
		writing = 1;
		pci_dir = PCI_DMA_TODEVICE;
	} else {
		pci_dir = PCI_DMA_FROMDEVICE;
	}

	/* get scatterlist from block layer */
	sg = &crq->sg[0];
	n_elem = blk_rq_map_sg(q, rq, sg);
	if (n_elem <= 0) {
		carm_end_rq(host, crq, -EIO);
		return;		/* request with no s/g entries? */
	}

	/* map scatterlist to PCI bus addresses */
	n_elem = pci_map_sg(host->pdev, sg, n_elem, pci_dir);
	if (n_elem <= 0) {
		carm_end_rq(host, crq, -EIO);
		return;		/* request with no s/g entries? */
	}
	crq->n_elem = n_elem;
	crq->port = port;
	host->hw_sg_used += n_elem;

	/*
	 * build read/write message
	 */

	VPRINTK("build msg\n");
	msg = (struct carm_msg_rw *) carm_ref_msg(host, crq->tag);

	if (writing) {
		msg->type = CARM_MSG_WRITE;
		crq->msg_type = CARM_MSG_WRITE;
	} else {
		msg->type = CARM_MSG_READ;
		crq->msg_type = CARM_MSG_READ;
	}

	msg->id		= port->port_no;
	msg->sg_count	= n_elem;
	msg->sg_type	= SGT_32BIT;
	msg->handle	= cpu_to_le32(TAG_ENCODE(crq->tag));
	msg->lba	= cpu_to_le32(blk_rq_pos(rq) & 0xffffffff);
	tmp		= (blk_rq_pos(rq) >> 16) >> 16;
	msg->lba_high	= cpu_to_le16( (u16) tmp );
	msg->lba_count	= cpu_to_le16(blk_rq_sectors(rq));

	msg_size = sizeof(struct carm_msg_rw) - sizeof(msg->sg);
	for (i = 0; i < n_elem; i++) {
		struct carm_msg_sg *carm_sg = &msg->sg[i];
		carm_sg->start = cpu_to_le32(sg_dma_address(&crq->sg[i]));
		carm_sg->len = cpu_to_le32(sg_dma_len(&crq->sg[i]));
		msg_size += sizeof(struct carm_msg_sg);
	}

	rc = carm_lookup_bucket(msg_size);
	BUG_ON(rc < 0);
	crq->msg_bucket = (u32) rc;

	/*
	 * queue read/write message to hardware
	 */

	VPRINTK("send msg, tag == %u\n", crq->tag);
	rc = carm_send_msg(host, crq);
	if (rc) {
		carm_put_request(host, crq);
		blk_requeue_request(q, rq);
		carm_push_q(host, q);
		return;		/* call us again later, eventually */
	}

	goto queue_one_request;
}

static void carm_handle_array_info(struct carm_host *host,
				   struct carm_request *crq, u8 *mem,
				   int error)
{
	struct carm_port *port;
	u8 *msg_data = mem + sizeof(struct carm_array_info);
	struct carm_array_info *desc = (struct carm_array_info *) msg_data;
	u64 lo, hi;
	int cur_port;
	size_t slen;

	DPRINTK("ENTER\n");

	carm_end_rq(host, crq, error);

	if (error)
		goto out;
	if (le32_to_cpu(desc->array_status) & ARRAY_NO_EXIST)
		goto out;

	cur_port = host->cur_scan_dev;

	/* should never occur */
	if ((cur_port < 0) || (cur_port >= CARM_MAX_PORTS)) {
		printk(KERN_ERR PFX "BUG: cur_scan_dev==%d, array_id==%d\n",
		       cur_port, (int) desc->array_id);
		goto out;
	}

	port = &host->port[cur_port];

	lo = (u64) le32_to_cpu(desc->size);
	hi = (u64) le16_to_cpu(desc->size_hi);

	port->capacity = lo | (hi << 32);
	port->dev_geom_head = le16_to_cpu(desc->head);
	port->dev_geom_sect = le16_to_cpu(desc->sect);
	port->dev_geom_cyl = le16_to_cpu(desc->cyl);

	host->dev_active |= (1 << cur_port);

	strncpy(port->name, desc->name, sizeof(port->name));
	port->name[sizeof(port->name) - 1] = 0;
	slen = strlen(port->name);
	while (slen && (port->name[slen - 1] == ' ')) {
		port->name[slen - 1] = 0;
		slen--;
	}

	printk(KERN_INFO DRV_NAME "(%s): port %u device %Lu sectors\n",
	       pci_name(host->pdev), port->port_no,
	       (unsigned long long) port->capacity);
	printk(KERN_INFO DRV_NAME "(%s): port %u device \"%s\"\n",
	       pci_name(host->pdev), port->port_no, port->name);

out:
	assert(host->state == HST_DEV_SCAN);
	schedule_work(&host->fsm_task);
}

static void carm_handle_scan_chan(struct carm_host *host,
				  struct carm_request *crq, u8 *mem,
				  int error)
{
	u8 *msg_data = mem + IOC_SCAN_CHAN_OFFSET;
	unsigned int i, dev_count = 0;
	int new_state = HST_DEV_SCAN_START;

	DPRINTK("ENTER\n");

	carm_end_rq(host, crq, error);

	if (error) {
		new_state = HST_ERROR;
		goto out;
	}

	/* TODO: scan and support non-disk devices */
	for (i = 0; i < 8; i++)
		if (msg_data[i] == 0) { /* direct-access device (disk) */
			host->dev_present |= (1 << i);
			dev_count++;
		}

	printk(KERN_INFO DRV_NAME "(%s): found %u interesting devices\n",
	       pci_name(host->pdev), dev_count);

out:
	assert(host->state == HST_PORT_SCAN);
	host->state = new_state;
	schedule_work(&host->fsm_task);
}

static void carm_handle_generic(struct carm_host *host,
				struct carm_request *crq, int error,
				int cur_state, int next_state)
{
	DPRINTK("ENTER\n");

	carm_end_rq(host, crq, error);

	assert(host->state == cur_state);
	if (error)
		host->state = HST_ERROR;
	else
		host->state = next_state;
	schedule_work(&host->fsm_task);
}

static inline void carm_handle_rw(struct carm_host *host,
				  struct carm_request *crq, int error)
{
	int pci_dir;

	VPRINTK("ENTER\n");

	if (rq_data_dir(crq->rq) == WRITE)
		pci_dir = PCI_DMA_TODEVICE;
	else
		pci_dir = PCI_DMA_FROMDEVICE;

	pci_unmap_sg(host->pdev, &crq->sg[0], crq->n_elem, pci_dir);

	carm_end_rq(host, crq, error);
}

static inline void carm_handle_resp(struct carm_host *host,
				    __le32 ret_handle_le, u32 status)
{
	u32 handle = le32_to_cpu(ret_handle_le);
	unsigned int msg_idx;
	struct carm_request *crq;
	int error = (status == RMSG_OK) ? 0 : -EIO;
	u8 *mem;

	VPRINTK("ENTER, handle == 0x%x\n", handle);

	if (unlikely(!TAG_VALID(handle))) {
		printk(KERN_ERR DRV_NAME "(%s): BUG: invalid tag 0x%x\n",
		       pci_name(host->pdev), handle);
		return;
	}

	msg_idx = TAG_DECODE(handle);
	VPRINTK("tag == %u\n", msg_idx);

	crq = &host->req[msg_idx];

	/* fast path */
	if (likely(crq->msg_type == CARM_MSG_READ ||
		   crq->msg_type == CARM_MSG_WRITE)) {
		carm_handle_rw(host, crq, error);
		return;
	}

	mem = carm_ref_msg(host, msg_idx);

	switch (crq->msg_type) {
	case CARM_MSG_IOCTL: {
		switch (crq->msg_subtype) {
		case CARM_IOC_SCAN_CHAN:
			carm_handle_scan_chan(host, crq, mem, error);
			break;
		default:
			/* unknown / invalid response */
			goto err_out;
		}
		break;
	}

	case CARM_MSG_MISC: {
		switch (crq->msg_subtype) {
		case MISC_ALLOC_MEM:
			carm_handle_generic(host, crq, error,
					    HST_ALLOC_BUF, HST_SYNC_TIME);
			break;
		case MISC_SET_TIME:
			carm_handle_generic(host, crq, error,
					    HST_SYNC_TIME, HST_GET_FW_VER);
			break;
		case MISC_GET_FW_VER: {
			struct carm_fw_ver *ver = (struct carm_fw_ver *)
				(mem + sizeof(struct carm_msg_get_fw_ver));
			if (!error) {
				host->fw_ver = le32_to_cpu(ver->version);
				host->flags |= (ver->features & FL_FW_VER_MASK);
			}
			carm_handle_generic(host, crq, error,
					    HST_GET_FW_VER, HST_PORT_SCAN);
			break;
		}
		default:
			/* unknown / invalid response */
			goto err_out;
		}
		break;
	}

	case CARM_MSG_ARRAY: {
		switch (crq->msg_subtype) {
		case CARM_ARRAY_INFO:
			carm_handle_array_info(host, crq, mem, error);
			break;
		default:
			/* unknown / invalid response */
			goto err_out;
		}
		break;
	}

	default:
		/* unknown / invalid response */
		goto err_out;
	}

	return;

err_out:
	printk(KERN_WARNING DRV_NAME "(%s): BUG: unhandled message type %d/%d\n",
	       pci_name(host->pdev), crq->msg_type, crq->msg_subtype);
	carm_end_rq(host, crq, -EIO);
}

static inline void carm_handle_responses(struct carm_host *host)
{
	void __iomem *mmio = host->mmio;
	struct carm_response *resp = (struct carm_response *) host->shm;
	unsigned int work = 0;
	unsigned int idx = host->resp_idx % RMSG_Q_LEN;

	while (1) {
		u32 status = le32_to_cpu(resp[idx].status);

		if (status == 0xffffffff) {
			VPRINTK("ending response on index %u\n", idx);
			writel(idx << 3, mmio + CARM_RESP_IDX);
			break;
		}

		/* response to a message we sent */
		else if ((status & (1 << 31)) == 0) {
			VPRINTK("handling msg response on index %u\n", idx);
			carm_handle_resp(host, resp[idx].ret_handle, status);
			resp[idx].status = cpu_to_le32(0xffffffff);
		}

		/* asynchronous events the hardware throws our way */
		else if ((status & 0xff000000) == (1 << 31)) {
			u8 *evt_type_ptr = (u8 *) &resp[idx];
			u8 evt_type = *evt_type_ptr;
			printk(KERN_WARNING DRV_NAME "(%s): unhandled event type %d\n",
			       pci_name(host->pdev), (int) evt_type);
			resp[idx].status = cpu_to_le32(0xffffffff);
		}

		idx = NEXT_RESP(idx);
		work++;
	}

	VPRINTK("EXIT, work==%u\n", work);
	host->resp_idx += work;
}

static irqreturn_t carm_interrupt(int irq, void *__host)
{
	struct carm_host *host = __host;
	void __iomem *mmio;
	u32 mask;
	int handled = 0;
	unsigned long flags;

	if (!host) {
		VPRINTK("no host\n");
		return IRQ_NONE;
	}

	spin_lock_irqsave(&host->lock, flags);

	mmio = host->mmio;

	/* reading should also clear interrupts */
	mask = readl(mmio + CARM_INT_STAT);

	if (mask == 0 || mask == 0xffffffff) {
		VPRINTK("no work, mask == 0x%x\n", mask);
		goto out;
	}

	if (mask & INT_ACK_MASK)
		writel(mask, mmio + CARM_INT_STAT);

	if (unlikely(host->state == HST_INVALID)) {
		VPRINTK("not initialized yet, mask = 0x%x\n", mask);
		goto out;
	}

	if (mask & CARM_HAVE_RESP) {
		handled = 1;
		carm_handle_responses(host);
	}

out:
	spin_unlock_irqrestore(&host->lock, flags);
	VPRINTK("EXIT\n");
	return IRQ_RETVAL(handled);
}

static void carm_fsm_task (struct work_struct *work)
{
	struct carm_host *host =
		container_of(work, struct carm_host, fsm_task);
	unsigned long flags;
	unsigned int state;
	int rc, i, next_dev;
	int reschedule = 0;
	int new_state = HST_INVALID;

	spin_lock_irqsave(&host->lock, flags);
	state = host->state;
	spin_unlock_irqrestore(&host->lock, flags);

	DPRINTK("ENTER, state == %s\n", state_name[state]);

	switch (state) {
	case HST_PROBE_START:
		new_state = HST_ALLOC_BUF;
		reschedule = 1;
		break;

	case HST_ALLOC_BUF:
		rc = carm_send_special(host, carm_fill_alloc_buf);
		if (rc) {
			new_state = HST_ERROR;
			reschedule = 1;
		}
		break;

	case HST_SYNC_TIME:
		rc = carm_send_special(host, carm_fill_sync_time);
		if (rc) {
			new_state = HST_ERROR;
			reschedule = 1;
		}
		break;

	case HST_GET_FW_VER:
		rc = carm_send_special(host, carm_fill_get_fw_ver);
		if (rc) {
			new_state = HST_ERROR;
			reschedule = 1;
		}
		break;

	case HST_PORT_SCAN:
		rc = carm_send_special(host, carm_fill_scan_channels);
		if (rc) {
			new_state = HST_ERROR;
			reschedule = 1;
		}
		break;

	case HST_DEV_SCAN_START:
		host->cur_scan_dev = -1;
		new_state = HST_DEV_SCAN;
		reschedule = 1;
		break;

	case HST_DEV_SCAN:
		next_dev = -1;
		for (i = host->cur_scan_dev + 1; i < CARM_MAX_PORTS; i++)
			if (host->dev_present & (1 << i)) {
				next_dev = i;
				break;
			}

		if (next_dev >= 0) {
			host->cur_scan_dev = next_dev;
			rc = carm_array_info(host, next_dev);
			if (rc) {
				new_state = HST_ERROR;
				reschedule = 1;
			}
		} else {
			new_state = HST_DEV_ACTIVATE;
			reschedule = 1;
		}
		break;

	case HST_DEV_ACTIVATE: {
		int activated = 0;
		for (i = 0; i < CARM_MAX_PORTS; i++)
			if (host->dev_active & (1 << i)) {
				struct carm_port *port = &host->port[i];
				struct gendisk *disk = port->disk;

				set_capacity(disk, port->capacity);
				add_disk(disk);
				activated++;
			}

		printk(KERN_INFO DRV_NAME "(%s): %d ports activated\n",
		       pci_name(host->pdev), activated);

		new_state = HST_PROBE_FINISHED;
		reschedule = 1;
		break;
	}

	case HST_PROBE_FINISHED:
		complete(&host->probe_comp);
		break;

	case HST_ERROR:
		/* FIXME: TODO */
		break;

	default:
		/* should never occur */
		printk(KERN_ERR PFX "BUG: unknown state %d\n", state);
		assert(0);
		break;
	}

	if (new_state != HST_INVALID) {
		spin_lock_irqsave(&host->lock, flags);
		host->state = new_state;
		spin_unlock_irqrestore(&host->lock, flags);
	}
	if (reschedule)
		schedule_work(&host->fsm_task);
}

static int carm_init_wait(void __iomem *mmio, u32 bits, unsigned int test_bit)
{
	unsigned int i;

	for (i = 0; i < 50000; i++) {
		u32 tmp = readl(mmio + CARM_LMUC);
		udelay(100);

		if (test_bit) {
			if ((tmp & bits) == bits)
				return 0;
		} else {
			if ((tmp & bits) == 0)
				return 0;
		}

		cond_resched();
	}

	printk(KERN_ERR PFX "carm_init_wait timeout, bits == 0x%x, test_bit == %s\n",
	       bits, test_bit ? "yes" : "no");
	return -EBUSY;
}

static void carm_init_responses(struct carm_host *host)
{
	void __iomem *mmio = host->mmio;
	unsigned int i;
	struct carm_response *resp = (struct carm_response *) host->shm;

	for (i = 0; i < RMSG_Q_LEN; i++)
		resp[i].status = cpu_to_le32(0xffffffff);

	writel(0, mmio + CARM_RESP_IDX);
}

static int carm_init_host(struct carm_host *host)
{
	void __iomem *mmio = host->mmio;
	u32 tmp;
	u8 tmp8;
	int rc;

	DPRINTK("ENTER\n");

	writel(0, mmio + CARM_INT_MASK);

	tmp8 = readb(mmio + CARM_INITC);
	if (tmp8 & 0x01) {
		tmp8 &= ~0x01;
		writeb(tmp8, mmio + CARM_INITC);
		readb(mmio + CARM_INITC);	/* flush */

		DPRINTK("snooze...\n");
		msleep(5000);
	}

	tmp = readl(mmio + CARM_HMUC);
	if (tmp & CARM_CME) {
		DPRINTK("CME bit present, waiting\n");
		rc = carm_init_wait(mmio, CARM_CME, 1);
		if (rc) {
			DPRINTK("EXIT, carm_init_wait 1 failed\n");
			return rc;
		}
	}
	if (tmp & CARM_RME) {
		DPRINTK("RME bit present, waiting\n");
		rc = carm_init_wait(mmio, CARM_RME, 1);
		if (rc) {
			DPRINTK("EXIT, carm_init_wait 2 failed\n");
			return rc;
		}
	}

	tmp &= ~(CARM_RME | CARM_CME);
	writel(tmp, mmio + CARM_HMUC);
	readl(mmio + CARM_HMUC);	/* flush */

	rc = carm_init_wait(mmio, CARM_RME | CARM_CME, 0);
	if (rc) {
		DPRINTK("EXIT, carm_init_wait 3 failed\n");
		return rc;
	}

	carm_init_buckets(mmio);

	writel(host->shm_dma & 0xffffffff, mmio + RBUF_ADDR_LO);
	writel((host->shm_dma >> 16) >> 16, mmio + RBUF_ADDR_HI);
	writel(RBUF_LEN, mmio + RBUF_BYTE_SZ);

	tmp = readl(mmio + CARM_HMUC);
	tmp |= (CARM_RME | CARM_CME | CARM_WZBC);
	writel(tmp, mmio + CARM_HMUC);
	readl(mmio + CARM_HMUC);	/* flush */

	rc = carm_init_wait(mmio, CARM_RME | CARM_CME, 1);
	if (rc) {
		DPRINTK("EXIT, carm_init_wait 4 failed\n");
		return rc;
	}

	writel(0, mmio + CARM_HMPHA);
	writel(INT_DEF_MASK, mmio + CARM_INT_MASK);

	carm_init_responses(host);

	/* start initialization, probing state machine */
	spin_lock_irq(&host->lock);
	assert(host->state == HST_INVALID);
	host->state = HST_PROBE_START;
	spin_unlock_irq(&host->lock);
	schedule_work(&host->fsm_task);

	DPRINTK("EXIT\n");
	return 0;
}

static int carm_init_disks(struct carm_host *host)
{
	unsigned int i;
	int rc = 0;

	for (i = 0; i < CARM_MAX_PORTS; i++) {
		struct gendisk *disk;
		struct request_queue *q;
		struct carm_port *port;

		port = &host->port[i];
		port->host = host;
		port->port_no = i;

		disk = alloc_disk(CARM_MINORS_PER_MAJOR);
		if (!disk) {
			rc = -ENOMEM;
			break;
		}

		port->disk = disk;
		sprintf(disk->disk_name, DRV_NAME "/%u",
			(unsigned int) (host->id * CARM_MAX_PORTS) + i);
		disk->major = host->major;
		disk->first_minor = i * CARM_MINORS_PER_MAJOR;
		disk->fops = &carm_bd_ops;
		disk->private_data = port;

		q = blk_init_queue(carm_rq_fn, &host->lock);
		if (!q) {
			rc = -ENOMEM;
			break;
		}
		disk->queue = q;
		blk_queue_max_segments(q, CARM_MAX_REQ_SG);
		blk_queue_segment_boundary(q, CARM_SG_BOUNDARY);

		q->queuedata = port;
	}

	return rc;
}

static void carm_free_disks(struct carm_host *host)
{
	unsigned int i;

	for (i = 0; i < CARM_MAX_PORTS; i++) {
		struct gendisk *disk = host->port[i].disk;
		if (disk) {
			struct request_queue *q = disk->queue;

			if (disk->flags & GENHD_FL_UP)
				del_gendisk(disk);
			if (q)
				blk_cleanup_queue(q);
			put_disk(disk);
		}
	}
}

static int carm_init_shm(struct carm_host *host)
{
	host->shm = pci_alloc_consistent(host->pdev, CARM_SHM_SIZE,
					 &host->shm_dma);
	if (!host->shm)
		return -ENOMEM;

	host->msg_base = host->shm + RBUF_LEN;
	host->msg_dma = host->shm_dma + RBUF_LEN;

	memset(host->shm, 0xff, RBUF_LEN);
	memset(host->msg_base, 0, PDC_SHM_SIZE - RBUF_LEN);

	return 0;
}

static int carm_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct carm_host *host;
	unsigned int pci_dac;
	int rc;
	struct request_queue *q;
	unsigned int i;

	printk_once(KERN_DEBUG DRV_NAME " version " DRV_VERSION "\n");

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out;

#ifdef IF_64BIT_DMA_IS_POSSIBLE /* grrrr... */
	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (!rc) {
		rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
		if (rc) {
			printk(KERN_ERR DRV_NAME "(%s): consistent DMA mask failure\n",
				pci_name(pdev));
			goto err_out_regions;
		}
		pci_dac = 1;
	} else {
#endif
		rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (rc) {
			printk(KERN_ERR DRV_NAME "(%s): DMA mask failure\n",
				pci_name(pdev));
			goto err_out_regions;
		}
		pci_dac = 0;
#ifdef IF_64BIT_DMA_IS_POSSIBLE /* grrrr... */
	}
#endif

	host = kzalloc(sizeof(*host), GFP_KERNEL);
	if (!host) {
		printk(KERN_ERR DRV_NAME "(%s): memory alloc failure\n",
		       pci_name(pdev));
		rc = -ENOMEM;
		goto err_out_regions;
	}

	host->pdev = pdev;
	host->flags = pci_dac ? FL_DAC : 0;
	spin_lock_init(&host->lock);
	INIT_WORK(&host->fsm_task, carm_fsm_task);
	init_completion(&host->probe_comp);

	for (i = 0; i < ARRAY_SIZE(host->req); i++)
		host->req[i].tag = i;

	host->mmio = ioremap(pci_resource_start(pdev, 0),
			     pci_resource_len(pdev, 0));
	if (!host->mmio) {
		printk(KERN_ERR DRV_NAME "(%s): MMIO alloc failure\n",
		       pci_name(pdev));
		rc = -ENOMEM;
		goto err_out_kfree;
	}

	rc = carm_init_shm(host);
	if (rc) {
		printk(KERN_ERR DRV_NAME "(%s): DMA SHM alloc failure\n",
		       pci_name(pdev));
		goto err_out_iounmap;
	}

	q = blk_init_queue(carm_oob_rq_fn, &host->lock);
	if (!q) {
		printk(KERN_ERR DRV_NAME "(%s): OOB queue alloc failure\n",
		       pci_name(pdev));
		rc = -ENOMEM;
		goto err_out_pci_free;
	}
	host->oob_q = q;
	q->queuedata = host;

	/*
	 * Figure out which major to use: 160, 161, or dynamic
	 */
	if (!test_and_set_bit(0, &carm_major_alloc))
		host->major = 160;
	else if (!test_and_set_bit(1, &carm_major_alloc))
		host->major = 161;
	else
		host->flags |= FL_DYN_MAJOR;

	host->id = carm_host_id;
	sprintf(host->name, DRV_NAME "%d", carm_host_id);

	rc = register_blkdev(host->major, host->name);
	if (rc < 0)
		goto err_out_free_majors;
	if (host->flags & FL_DYN_MAJOR)
		host->major = rc;

	rc = carm_init_disks(host);
	if (rc)
		goto err_out_blkdev_disks;

	pci_set_master(pdev);

	rc = request_irq(pdev->irq, carm_interrupt, IRQF_SHARED, DRV_NAME, host);
	if (rc) {
		printk(KERN_ERR DRV_NAME "(%s): irq alloc failure\n",
		       pci_name(pdev));
		goto err_out_blkdev_disks;
	}

	rc = carm_init_host(host);
	if (rc)
		goto err_out_free_irq;

	DPRINTK("waiting for probe_comp\n");
	wait_for_completion(&host->probe_comp);

	printk(KERN_INFO "%s: pci %s, ports %d, io %llx, irq %u, major %d\n",
	       host->name, pci_name(pdev), (int) CARM_MAX_PORTS,
	       (unsigned long long)pci_resource_start(pdev, 0),
		   pdev->irq, host->major);

	carm_host_id++;
	pci_set_drvdata(pdev, host);
	return 0;

err_out_free_irq:
	free_irq(pdev->irq, host);
err_out_blkdev_disks:
	carm_free_disks(host);
	unregister_blkdev(host->major, host->name);
err_out_free_majors:
	if (host->major == 160)
		clear_bit(0, &carm_major_alloc);
	else if (host->major == 161)
		clear_bit(1, &carm_major_alloc);
	blk_cleanup_queue(host->oob_q);
err_out_pci_free:
	pci_free_consistent(pdev, CARM_SHM_SIZE, host->shm, host->shm_dma);
err_out_iounmap:
	iounmap(host->mmio);
err_out_kfree:
	kfree(host);
err_out_regions:
	pci_release_regions(pdev);
err_out:
	pci_disable_device(pdev);
	return rc;
}

static void carm_remove_one (struct pci_dev *pdev)
{
	struct carm_host *host = pci_get_drvdata(pdev);

	if (!host) {
		printk(KERN_ERR PFX "BUG: no host data for PCI(%s)\n",
		       pci_name(pdev));
		return;
	}

	free_irq(pdev->irq, host);
	carm_free_disks(host);
	unregister_blkdev(host->major, host->name);
	if (host->major == 160)
		clear_bit(0, &carm_major_alloc);
	else if (host->major == 161)
		clear_bit(1, &carm_major_alloc);
	blk_cleanup_queue(host->oob_q);
	pci_free_consistent(pdev, CARM_SHM_SIZE, host->shm, host->shm_dma);
	iounmap(host->mmio);
	kfree(host);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static int __init carm_init(void)
{
	return pci_register_driver(&carm_driver);
}

static void __exit carm_exit(void)
{
	pci_unregister_driver(&carm_driver);
}

module_init(carm_init);
module_exit(carm_exit);


