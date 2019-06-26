/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef KPC_DMA_DRIVER_H
#define KPC_DMA_DRIVER_H
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/kfifo.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/miscdevice.h>
#include <linux/rwsem.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/aio.h>
#include <linux/bitops.h>
#include "../kpc.h"


struct kp2000_device;
struct kpc_dma_device {
	struct list_head            list;
	struct platform_device     *pldev;
	u32 __iomem                *eng_regs;
	struct device              *kpc_dma_dev;
	struct kobject              kobj;
	char                        name[16];
	
	int                         dir; // DMA_FROM_DEVICE || DMA_TO_DEVICE
	struct mutex                sem;
	unsigned int                irq;
	struct work_struct          irq_work;
	
	atomic_t                    open_count;
	
	size_t                      accumulated_bytes;
	u32                         accumulated_flags;
	
	// Descriptor "Pool" housekeeping
	u32                         desc_pool_cnt;
	struct dma_pool            *desc_pool;
	struct kpc_dma_descriptor  *desc_pool_first;
	struct kpc_dma_descriptor  *desc_pool_last;
	
	struct kpc_dma_descriptor  *desc_next;
	struct kpc_dma_descriptor  *desc_completed;
};

struct dev_private_data {
	struct kpc_dma_device      *ldev;
	u64                         card_addr;
	u64                         user_ctl;
	u64                         user_ctl_last;
	u64                         user_sts;
};

struct kpc_dma_device *  kpc_dma_lookup_device(int minor);

extern struct file_operations  kpc_dma_fops;

#define ENG_CAP_PRESENT                 0x00000001
#define ENG_CAP_DIRECTION               0x00000002
#define ENG_CAP_TYPE_MASK               0x000000F0
#define ENG_CAP_NUMBER_MASK             0x0000FF00
#define ENG_CAP_CARD_ADDR_SIZE_MASK     0x007F0000
#define ENG_CAP_DESC_MAX_BYTE_CNT_MASK  0x3F000000
#define ENG_CAP_PERF_SCALE_MASK         0xC0000000

#define ENG_CTL_IRQ_ENABLE              BIT(0)
#define ENG_CTL_IRQ_ACTIVE              BIT(1)
#define ENG_CTL_DESC_COMPLETE           BIT(2)
#define ENG_CTL_DESC_ALIGN_ERR          BIT(3)
#define ENG_CTL_DESC_FETCH_ERR          BIT(4)
#define ENG_CTL_SW_ABORT_ERR            BIT(5)
#define ENG_CTL_DESC_CHAIN_END          BIT(7)
#define ENG_CTL_DMA_ENABLE              BIT(8)
#define ENG_CTL_DMA_RUNNING             BIT(10)
#define ENG_CTL_DMA_WAITING             BIT(11)
#define ENG_CTL_DMA_WAITING_PERSIST     BIT(12)
#define ENG_CTL_DMA_RESET_REQUEST       BIT(14)
#define ENG_CTL_DMA_RESET               BIT(15)
#define ENG_CTL_DESC_FETCH_ERR_CLASS_MASK   0x700000

struct aio_cb_data {
	struct dev_private_data    *priv;
	struct kpc_dma_device      *ldev;
	struct completion  *cpl;
	unsigned char       flags;
	struct kiocb       *kcb;
	size_t              len;
	
	unsigned int        page_count;
	struct page       **user_pages;
	struct sg_table     sgt;
	int                 mapped_entry_count;
};

#define ACD_FLAG_DONE               0
#define ACD_FLAG_ABORT              1
#define ACD_FLAG_ENG_ACCUM_ERROR    4
#define ACD_FLAG_ENG_ACCUM_SHORT    5

struct kpc_dma_descriptor {
	struct {
		volatile u32  DescByteCount              :20;
		volatile u32  DescStatusErrorFlags       :4;
		volatile u32  DescStatusFlags            :8;
	};
		volatile u32  DescUserControlLS;
		volatile u32  DescUserControlMS;
		volatile u32  DescCardAddrLS;
	struct {
		volatile u32  DescBufferByteCount        :20;
		volatile u32  DescCardAddrMS             :4;
		volatile u32  DescControlFlags           :8;
	};
		volatile u32  DescSystemAddrLS;
		volatile u32  DescSystemAddrMS;
		volatile u32  DescNextDescPtr;
		
		dma_addr_t    MyDMAAddr;
		struct kpc_dma_descriptor   *Next;
		
		struct aio_cb_data  *acd;
} __attribute__((packed));
// DescControlFlags:
#define DMA_DESC_CTL_SOP            BIT(7)
#define DMA_DESC_CTL_EOP            BIT(6)
#define DMA_DESC_CTL_AFIFO          BIT(2)
#define DMA_DESC_CTL_IRQONERR       BIT(1)
#define DMA_DESC_CTL_IRQONDONE      BIT(0)
// DescStatusFlags:
#define DMA_DESC_STS_SOP            BIT(7)
#define DMA_DESC_STS_EOP            BIT(6)
#define DMA_DESC_STS_ERROR          BIT(4)
#define DMA_DESC_STS_USMSZ          BIT(3)
#define DMA_DESC_STS_USLSZ          BIT(2)
#define DMA_DESC_STS_SHORT          BIT(1)
#define DMA_DESC_STS_COMPLETE       BIT(0)
// DescStatusErrorFlags:
#define DMA_DESC_ESTS_ECRC          BIT(2)
#define DMA_DESC_ESTS_POISON        BIT(1)
#define DMA_DESC_ESTS_UNSUCCESSFUL  BIT(0)

#define DMA_DESC_ALIGNMENT          0x20

static inline
u32  GetEngineCapabilities(struct kpc_dma_device *eng)
{
	return readl(eng->eng_regs + 0);
}

static inline
void  WriteEngineControl(struct kpc_dma_device *eng, u32 value)
{
	writel(value, eng->eng_regs + 1);
}
static inline
u32  GetEngineControl(struct kpc_dma_device *eng)
{
	return readl(eng->eng_regs + 1);
}
static inline
void  SetClearEngineControl(struct kpc_dma_device *eng, u32 set_bits, u32 clear_bits)
{
	u32 val = GetEngineControl(eng);
	val |= set_bits;
	val &= ~clear_bits;
	WriteEngineControl(eng, val);
}

static inline
void  SetEngineNextPtr(struct kpc_dma_device *eng, struct kpc_dma_descriptor * desc)
{
	writel(desc->MyDMAAddr, eng->eng_regs + 2);
}
static inline
void  SetEngineSWPtr(struct kpc_dma_device *eng, struct kpc_dma_descriptor * desc)
{
	writel(desc->MyDMAAddr, eng->eng_regs + 3);
}
static inline
void  ClearEngineCompletePtr(struct kpc_dma_device *eng)
{
	writel(0, eng->eng_regs + 4);
}
static inline
u32  GetEngineCompletePtr(struct kpc_dma_device *eng)
{
	return readl(eng->eng_regs + 4);
}

static inline
void  lock_engine(struct kpc_dma_device *eng)
{
	BUG_ON(eng == NULL);
	mutex_lock(&eng->sem);
}

static inline
void  unlock_engine(struct kpc_dma_device *eng)
{
	BUG_ON(eng == NULL);
	mutex_unlock(&eng->sem);
}


/// Shared Functions
void  start_dma_engine(struct kpc_dma_device *eng);
int   setup_dma_engine(struct kpc_dma_device *eng, u32 desc_cnt);
void  stop_dma_engine(struct kpc_dma_device *eng);
void  destroy_dma_engine(struct kpc_dma_device *eng);
void  clear_desc(struct kpc_dma_descriptor *desc);
int   count_descriptors_available(struct kpc_dma_device *eng);
void  transfer_complete_cb(struct aio_cb_data *acd, size_t xfr_count, u32 flags);

#endif /* KPC_DMA_DRIVER_H */

