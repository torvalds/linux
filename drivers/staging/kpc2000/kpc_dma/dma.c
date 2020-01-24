/* SPDX-License-Identifier: GPL-2.0+ */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/rwsem.h>
#include "kpc_dma_driver.h"

/**********  IRQ Handlers  **********/
static
irqreturn_t  ndd_irq_handler(int irq, void *dev_id)
{
	struct kpc_dma_device *ldev = (struct kpc_dma_device *)dev_id;

	if ((GetEngineControl(ldev) & ENG_CTL_IRQ_ACTIVE) || (ldev->desc_completed->MyDMAAddr != GetEngineCompletePtr(ldev)))
		schedule_work(&ldev->irq_work);

	return IRQ_HANDLED;
}

static
void  ndd_irq_worker(struct work_struct *ws)
{
	struct kpc_dma_descriptor *cur;
	struct kpc_dma_device *eng = container_of(ws, struct kpc_dma_device, irq_work);

	lock_engine(eng);

	if (GetEngineCompletePtr(eng) == 0)
		goto out;

	if (eng->desc_completed->MyDMAAddr == GetEngineCompletePtr(eng))
		goto out;

	cur = eng->desc_completed;
	do {
		cur = cur->Next;
		dev_dbg(&eng->pldev->dev, "Handling completed descriptor %p (acd = %p)\n", cur, cur->acd);
		BUG_ON(cur == eng->desc_next); // Ordering failure.

		if (cur->DescControlFlags & DMA_DESC_CTL_SOP) {
			eng->accumulated_bytes = 0;
			eng->accumulated_flags = 0;
		}

		eng->accumulated_bytes += cur->DescByteCount;
		if (cur->DescStatusFlags & DMA_DESC_STS_ERROR)
			eng->accumulated_flags |= ACD_FLAG_ENG_ACCUM_ERROR;

		if (cur->DescStatusFlags & DMA_DESC_STS_SHORT)
			eng->accumulated_flags |= ACD_FLAG_ENG_ACCUM_SHORT;

		if (cur->DescControlFlags & DMA_DESC_CTL_EOP) {
			if (cur->acd)
				transfer_complete_cb(cur->acd, eng->accumulated_bytes, eng->accumulated_flags | ACD_FLAG_DONE);
		}

		eng->desc_completed = cur;
	} while (cur->MyDMAAddr != GetEngineCompletePtr(eng));

 out:
	SetClearEngineControl(eng, ENG_CTL_IRQ_ACTIVE, 0);

	unlock_engine(eng);
}

/**********  DMA Engine Init/Teardown  **********/
void  start_dma_engine(struct kpc_dma_device *eng)
{
	eng->desc_next       = eng->desc_pool_first;
	eng->desc_completed  = eng->desc_pool_last;

	// Setup the engine pointer registers
	SetEngineNextPtr(eng, eng->desc_pool_first);
	SetEngineSWPtr(eng, eng->desc_pool_first);
	ClearEngineCompletePtr(eng);

	WriteEngineControl(eng, ENG_CTL_DMA_ENABLE | ENG_CTL_IRQ_ENABLE);
}

int  setup_dma_engine(struct kpc_dma_device *eng, u32 desc_cnt)
{
	u32 caps;
	struct kpc_dma_descriptor *cur;
	struct kpc_dma_descriptor *next;
	dma_addr_t next_handle;
	dma_addr_t head_handle;
	unsigned int i;
	int rv;

	caps = GetEngineCapabilities(eng);

	if (WARN(!(caps & ENG_CAP_PRESENT), "%s() called for DMA Engine at %p which isn't present in hardware!\n", __func__, eng))
		return -ENXIO;

	if (caps & ENG_CAP_DIRECTION) {
		eng->dir = DMA_FROM_DEVICE;
	} else {
		eng->dir = DMA_TO_DEVICE;
	}

	eng->desc_pool_cnt = desc_cnt;
	eng->desc_pool = dma_pool_create("KPC DMA Descriptors", &eng->pldev->dev, sizeof(struct kpc_dma_descriptor), DMA_DESC_ALIGNMENT, 4096);

	eng->desc_pool_first = dma_pool_alloc(eng->desc_pool, GFP_KERNEL | GFP_DMA, &head_handle);
	if (!eng->desc_pool_first) {
		dev_err(&eng->pldev->dev, "%s: couldn't allocate desc_pool_first!\n", __func__);
		dma_pool_destroy(eng->desc_pool);
		return -ENOMEM;
	}

	eng->desc_pool_first->MyDMAAddr = head_handle;
	clear_desc(eng->desc_pool_first);

	cur = eng->desc_pool_first;
	for (i = 1 ; i < eng->desc_pool_cnt ; i++) {
		next = dma_pool_alloc(eng->desc_pool, GFP_KERNEL | GFP_DMA, &next_handle);
		if (!next)
			goto done_alloc;

		clear_desc(next);
		next->MyDMAAddr = next_handle;

		cur->DescNextDescPtr = next_handle;
		cur->Next = next;
		cur = next;
	}

 done_alloc:
	// Link the last descriptor back to the first, so it's a circular linked list
	cur->Next = eng->desc_pool_first;
	cur->DescNextDescPtr = eng->desc_pool_first->MyDMAAddr;

	eng->desc_pool_last = cur;
	eng->desc_completed = eng->desc_pool_last;

	// Setup work queue
	INIT_WORK(&eng->irq_work, ndd_irq_worker);

	// Grab IRQ line
	rv = request_irq(eng->irq, ndd_irq_handler, IRQF_SHARED, KP_DRIVER_NAME_DMA_CONTROLLER, eng);
	if (rv) {
		dev_err(&eng->pldev->dev, "%s: failed to request_irq: %d\n", __func__, rv);
		return rv;
	}

	// Turn on the engine!
	start_dma_engine(eng);
	unlock_engine(eng);

	return 0;
}

void  stop_dma_engine(struct kpc_dma_device *eng)
{
	unsigned long timeout;

	// Disable the descriptor engine
	WriteEngineControl(eng, 0);

	// Wait for descriptor engine to finish current operaion
	timeout = jiffies + (HZ / 2);
	while (GetEngineControl(eng) & ENG_CTL_DMA_RUNNING) {
		if (time_after(jiffies, timeout)) {
			dev_crit(&eng->pldev->dev, "DMA_RUNNING still asserted!\n");
			break;
		}
	}

	// Request a reset
	WriteEngineControl(eng, ENG_CTL_DMA_RESET_REQUEST);

	// Wait for reset request to be processed
	timeout = jiffies + (HZ / 2);
	while (GetEngineControl(eng) & (ENG_CTL_DMA_RUNNING | ENG_CTL_DMA_RESET_REQUEST)) {
		if (time_after(jiffies, timeout)) {
			dev_crit(&eng->pldev->dev, "ENG_CTL_DMA_RESET_REQUEST still asserted!\n");
			break;
		}
	}

	// Request a reset
	WriteEngineControl(eng, ENG_CTL_DMA_RESET);

	// And wait for reset to complete
	timeout = jiffies + (HZ / 2);
	while (GetEngineControl(eng) & ENG_CTL_DMA_RESET) {
		if (time_after(jiffies, timeout)) {
			dev_crit(&eng->pldev->dev, "DMA_RESET still asserted!\n");
			break;
		}
	}

	// Clear any persistent bits just to make sure there is no residue from the reset
	SetClearEngineControl(eng, (ENG_CTL_IRQ_ACTIVE | ENG_CTL_DESC_COMPLETE | ENG_CTL_DESC_ALIGN_ERR | ENG_CTL_DESC_FETCH_ERR | ENG_CTL_SW_ABORT_ERR | ENG_CTL_DESC_CHAIN_END | ENG_CTL_DMA_WAITING_PERSIST), 0);

	// Reset performance counters

	// Completely disable the engine
	WriteEngineControl(eng, 0);
}

void  destroy_dma_engine(struct kpc_dma_device *eng)
{
	struct kpc_dma_descriptor *cur;
	dma_addr_t cur_handle;
	unsigned int i;

	stop_dma_engine(eng);

	cur = eng->desc_pool_first;
	cur_handle = eng->desc_pool_first->MyDMAAddr;

	for (i = 0 ; i < eng->desc_pool_cnt ; i++) {
		struct kpc_dma_descriptor *next = cur->Next;
		dma_addr_t next_handle = cur->DescNextDescPtr;

		dma_pool_free(eng->desc_pool, cur, cur_handle);
		cur_handle = next_handle;
		cur = next;
	}

	dma_pool_destroy(eng->desc_pool);

	free_irq(eng->irq, eng);
}

/**********  Helper Functions  **********/
int  count_descriptors_available(struct kpc_dma_device *eng)
{
	u32 count = 0;
	struct kpc_dma_descriptor *cur = eng->desc_next;

	while (cur != eng->desc_completed) {
		BUG_ON(cur == NULL);
		count++;
		cur = cur->Next;
	}
	return count;
}

void  clear_desc(struct kpc_dma_descriptor *desc)
{
	if (!desc)
		return;
	desc->DescByteCount         = 0;
	desc->DescStatusErrorFlags  = 0;
	desc->DescStatusFlags       = 0;
	desc->DescUserControlLS     = 0;
	desc->DescUserControlMS     = 0;
	desc->DescCardAddrLS        = 0;
	desc->DescBufferByteCount   = 0;
	desc->DescCardAddrMS        = 0;
	desc->DescControlFlags      = 0;
	desc->DescSystemAddrLS      = 0;
	desc->DescSystemAddrMS      = 0;
	desc->acd = NULL;
}
