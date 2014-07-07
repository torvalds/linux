/***************************************************************************
 *	   Copyright (c) 2005-2009, Broadcom Corporation.
 *
 *  Name: crystalhd_misc . c
 *
 *  Description:
 *		BCM70012 Linux driver misc routines.
 *
 *  HISTORY:
 *
 **********************************************************************
 * This file is part of the crystalhd device driver.
 *
 * This driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * This driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this driver.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/

#include "crystalhd.h"

#include <linux/slab.h>

uint32_t g_linklog_level;

static inline uint32_t crystalhd_dram_rd(struct crystalhd_adp *adp,
					 uint32_t mem_off)
{
	crystalhd_reg_wr(adp, DCI_DRAM_BASE_ADDR, (mem_off >> 19));
	return bc_dec_reg_rd(adp, (0x00380000 | (mem_off & 0x0007FFFF)));
}

static inline void crystalhd_dram_wr(struct crystalhd_adp *adp,
					 uint32_t mem_off, uint32_t val)
{
	crystalhd_reg_wr(adp, DCI_DRAM_BASE_ADDR, (mem_off >> 19));
	bc_dec_reg_wr(adp, (0x00380000 | (mem_off & 0x0007FFFF)), val);
}

static inline enum BC_STATUS bc_chk_dram_range(struct crystalhd_adp *adp,
					 uint32_t start_off, uint32_t cnt)
{
	return BC_STS_SUCCESS;
}

static struct crystalhd_dio_req *crystalhd_alloc_dio(struct crystalhd_adp *adp)
{
	unsigned long flags = 0;
	struct crystalhd_dio_req *temp = NULL;

	if (!adp) {
		BCMLOG_ERR("Invalid Arg!!\n");
		return temp;
	}

	spin_lock_irqsave(&adp->lock, flags);
	temp = adp->ua_map_free_head;
	if (temp)
		adp->ua_map_free_head = adp->ua_map_free_head->next;
	spin_unlock_irqrestore(&adp->lock, flags);

	return temp;
}

static void crystalhd_free_dio(struct crystalhd_adp *adp,
					 struct crystalhd_dio_req *dio)
{
	unsigned long flags = 0;

	if (!adp || !dio)
		return;
	spin_lock_irqsave(&adp->lock, flags);
	dio->sig = crystalhd_dio_inv;
	dio->page_cnt = 0;
	dio->fb_size = 0;
	memset(&dio->uinfo, 0, sizeof(dio->uinfo));
	dio->next = adp->ua_map_free_head;
	adp->ua_map_free_head = dio;
	spin_unlock_irqrestore(&adp->lock, flags);
}

static struct crystalhd_elem *crystalhd_alloc_elem(struct crystalhd_adp *adp)
{
	unsigned long flags = 0;
	struct crystalhd_elem *temp = NULL;

	if (!adp)
		return temp;
	spin_lock_irqsave(&adp->lock, flags);
	temp = adp->elem_pool_head;
	if (temp) {
		adp->elem_pool_head = adp->elem_pool_head->flink;
		memset(temp, 0, sizeof(*temp));
	}
	spin_unlock_irqrestore(&adp->lock, flags);

	return temp;
}
static void crystalhd_free_elem(struct crystalhd_adp *adp,
					 struct crystalhd_elem *elem)
{
	unsigned long flags = 0;

	if (!adp || !elem)
		return;
	spin_lock_irqsave(&adp->lock, flags);
	elem->flink = adp->elem_pool_head;
	adp->elem_pool_head = elem;
	spin_unlock_irqrestore(&adp->lock, flags);
}

static inline void crystalhd_set_sg(struct scatterlist *sg, struct page *page,
				  unsigned int len, unsigned int offset)
{
	sg_set_page(sg, page, len, offset);
#ifdef CONFIG_X86_64
	sg->dma_length = len;
#endif
}

static inline void crystalhd_init_sg(struct scatterlist *sg,
					 unsigned int entries)
{
	/* http://lkml.org/lkml/2007/11/27/68 */
	sg_init_table(sg, entries);
}

/*========================== Extern ========================================*/
/**
 * bc_dec_reg_rd - Read 7412's device register.
 * @adp: Adapter instance
 * @reg_off: Register offset.
 *
 * Return:
 *	32bit value read
 *
 * 7412's device register read routine. This interface use
 * 7412's device access range mapped from BAR-2 (4M) of PCIe
 * configuration space.
 */
uint32_t bc_dec_reg_rd(struct crystalhd_adp *adp, uint32_t reg_off)
{
	if (!adp || (reg_off > adp->pci_mem_len)) {
		BCMLOG_ERR("dec_rd_reg_off outof range: 0x%08x\n", reg_off);
		return 0;
	}

	return readl(adp->addr + reg_off);
}

/**
 * bc_dec_reg_wr - Write 7412's device register
 * @adp: Adapter instance
 * @reg_off: Register offset.
 * @val: Dword value to be written.
 *
 * Return:
 *	none.
 *
 * 7412's device register write routine. This interface use
 * 7412's device access range mapped from BAR-2 (4M) of PCIe
 * configuration space.
 */
void bc_dec_reg_wr(struct crystalhd_adp *adp, uint32_t reg_off, uint32_t val)
{
	if (!adp || (reg_off > adp->pci_mem_len)) {
		BCMLOG_ERR("dec_wr_reg_off outof range: 0x%08x\n", reg_off);
		return;
	}
	writel(val, adp->addr + reg_off);
	udelay(8);
}

/**
 * crystalhd_reg_rd - Read Link's device register.
 * @adp: Adapter instance
 * @reg_off: Register offset.
 *
 * Return:
 *	32bit value read
 *
 * Link device register  read routine. This interface use
 * Link's device access range mapped from BAR-1 (64K) of PCIe
 * configuration space.
 *
 */
uint32_t crystalhd_reg_rd(struct crystalhd_adp *adp, uint32_t reg_off)
{
	if (!adp || (reg_off > adp->pci_i2o_len)) {
		BCMLOG_ERR("link_rd_reg_off outof range: 0x%08x\n", reg_off);
		return 0;
	}
	return readl(adp->i2o_addr + reg_off);
}

/**
 * crystalhd_reg_wr - Write Link's device register
 * @adp: Adapter instance
 * @reg_off: Register offset.
 * @val: Dword value to be written.
 *
 * Return:
 *	none.
 *
 * Link device register  write routine. This interface use
 * Link's device access range mapped from BAR-1 (64K) of PCIe
 * configuration space.
 *
 */
void crystalhd_reg_wr(struct crystalhd_adp *adp, uint32_t reg_off,
					 uint32_t val)
{
	if (!adp || (reg_off > adp->pci_i2o_len)) {
		BCMLOG_ERR("link_wr_reg_off outof range: 0x%08x\n", reg_off);
		return;
	}
	writel(val, adp->i2o_addr + reg_off);
}

/**
 * crystalhd_mem_rd - Read data from 7412's DRAM area.
 * @adp: Adapter instance
 * @start_off: Start offset.
 * @dw_cnt: Count in dwords.
 * @rd_buff: Buffer to copy the data from dram.
 *
 * Return:
 *	Status.
 *
 * 7412's Dram read routine.
 */
enum BC_STATUS crystalhd_mem_rd(struct crystalhd_adp *adp, uint32_t start_off,
			 uint32_t dw_cnt, uint32_t *rd_buff)
{
	uint32_t ix = 0;

	if (!adp || !rd_buff ||
	    (bc_chk_dram_range(adp, start_off, dw_cnt) != BC_STS_SUCCESS)) {
		BCMLOG_ERR("Invalid arg\n");
		return BC_STS_INV_ARG;
	}
	for (ix = 0; ix < dw_cnt; ix++)
		rd_buff[ix] = crystalhd_dram_rd(adp, (start_off + (ix * 4)));

	return BC_STS_SUCCESS;
}

/**
 * crystalhd_mem_wr - Write data to 7412's DRAM area.
 * @adp: Adapter instance
 * @start_off: Start offset.
 * @dw_cnt: Count in dwords.
 * @wr_buff: Data Buffer to be written.
 *
 * Return:
 *	Status.
 *
 * 7412's Dram write routine.
 */
enum BC_STATUS crystalhd_mem_wr(struct crystalhd_adp *adp, uint32_t start_off,
			 uint32_t dw_cnt, uint32_t *wr_buff)
{
	uint32_t ix = 0;

	if (!adp || !wr_buff ||
	    (bc_chk_dram_range(adp, start_off, dw_cnt) != BC_STS_SUCCESS)) {
		BCMLOG_ERR("Invalid arg\n");
		return BC_STS_INV_ARG;
	}

	for (ix = 0; ix < dw_cnt; ix++)
		crystalhd_dram_wr(adp, (start_off + (ix * 4)), wr_buff[ix]);

	return BC_STS_SUCCESS;
}
/**
 * crystalhd_pci_cfg_rd - PCIe config read
 * @adp: Adapter instance
 * @off: PCI config space offset.
 * @len: Size -- Byte, Word & dword.
 * @val: Value read
 *
 * Return:
 *	Status.
 *
 * Get value from Link's PCIe config space.
 */
enum BC_STATUS crystalhd_pci_cfg_rd(struct crystalhd_adp *adp, uint32_t off,
			     uint32_t len, uint32_t *val)
{
	enum BC_STATUS sts = BC_STS_SUCCESS;
	int rc = 0;

	if (!adp || !val) {
		BCMLOG_ERR("Invalid arg\n");
		return BC_STS_INV_ARG;
	}

	switch (len) {
	case 1:
		rc = pci_read_config_byte(adp->pdev, off, (u8 *)val);
		break;
	case 2:
		rc = pci_read_config_word(adp->pdev, off, (u16 *)val);
		break;
	case 4:
		rc = pci_read_config_dword(adp->pdev, off, (u32 *)val);
		break;
	default:
		rc = -EINVAL;
		sts = BC_STS_INV_ARG;
		BCMLOG_ERR("Invalid len:%d\n", len);
	}

	if (rc && (sts == BC_STS_SUCCESS))
		sts = BC_STS_ERROR;

	return sts;
}

/**
 * crystalhd_pci_cfg_wr - PCIe config write
 * @adp: Adapter instance
 * @off: PCI config space offset.
 * @len: Size -- Byte, Word & dword.
 * @val: Value to be written
 *
 * Return:
 *	Status.
 *
 * Set value to Link's PCIe config space.
 */
enum BC_STATUS crystalhd_pci_cfg_wr(struct crystalhd_adp *adp, uint32_t off,
			     uint32_t len, uint32_t val)
{
	enum BC_STATUS sts = BC_STS_SUCCESS;
	int rc = 0;

	if (!adp || !val) {
		BCMLOG_ERR("Invalid arg\n");
		return BC_STS_INV_ARG;
	}

	switch (len) {
	case 1:
		rc = pci_write_config_byte(adp->pdev, off, (u8)val);
		break;
	case 2:
		rc = pci_write_config_word(adp->pdev, off, (u16)val);
		break;
	case 4:
		rc = pci_write_config_dword(adp->pdev, off, val);
		break;
	default:
		rc = -EINVAL;
		sts = BC_STS_INV_ARG;
		BCMLOG_ERR("Invalid len:%d\n", len);
	}

	if (rc && (sts == BC_STS_SUCCESS))
		sts = BC_STS_ERROR;

	return sts;
}

/**
 * bc_kern_dma_alloc - Allocate memory for Dma rings
 * @adp: Adapter instance
 * @sz: Size of the memory to allocate.
 * @phy_addr: Physical address of the memory allocated.
 *	   Typedef to system's dma_addr_t (u64)
 *
 * Return:
 *  Pointer to allocated memory..
 *
 * Wrapper to Linux kernel interface.
 *
 */
void *bc_kern_dma_alloc(struct crystalhd_adp *adp, uint32_t sz,
			dma_addr_t *phy_addr)
{
	void *temp = NULL;

	if (!adp || !sz || !phy_addr) {
		BCMLOG_ERR("Invalid Arg..\n");
		return temp;
	}

	temp = pci_alloc_consistent(adp->pdev, sz, phy_addr);
	if (temp)
		memset(temp, 0, sz);

	return temp;
}

/**
 * bc_kern_dma_free - Release Dma ring memory.
 * @adp: Adapter instance
 * @sz: Size of the memory to allocate.
 * @ka: Kernel virtual address returned during _dio_alloc()
 * @phy_addr: Physical address of the memory allocated.
 *	   Typedef to system's dma_addr_t (u64)
 *
 * Return:
 *     none.
 */
void bc_kern_dma_free(struct crystalhd_adp *adp, uint32_t sz, void *ka,
		      dma_addr_t phy_addr)
{
	if (!adp || !ka || !sz || !phy_addr) {
		BCMLOG_ERR("Invalid Arg..\n");
		return;
	}

	pci_free_consistent(adp->pdev, sz, ka, phy_addr);
}

/**
 * crystalhd_create_dioq - Create Generic DIO queue
 * @adp: Adapter instance
 * @dioq_hnd: Handle to the dio queue created
 * @cb	: Optional - Call back To free the element.
 * @cbctx: Context to pass to callback.
 *
 * Return:
 *  status
 *
 * Initialize Generic DIO queue to hold any data. Callback
 * will be used to free elements while deleting the queue.
 */
enum BC_STATUS crystalhd_create_dioq(struct crystalhd_adp *adp,
			      struct crystalhd_dioq **dioq_hnd,
			      crystalhd_data_free_cb cb, void *cbctx)
{
	struct crystalhd_dioq *dioq = NULL;

	if (!adp || !dioq_hnd) {
		BCMLOG_ERR("Invalid arg!!\n");
		return BC_STS_INV_ARG;
	}

	dioq = kzalloc(sizeof(*dioq), GFP_KERNEL);
	if (!dioq)
		return BC_STS_INSUFF_RES;

	spin_lock_init(&dioq->lock);
	dioq->sig = BC_LINK_DIOQ_SIG;
	dioq->head = (struct crystalhd_elem *)&dioq->head;
	dioq->tail = (struct crystalhd_elem *)&dioq->head;
	crystalhd_create_event(&dioq->event);
	dioq->adp = adp;
	dioq->data_rel_cb = cb;
	dioq->cb_context = cbctx;
	*dioq_hnd = dioq;

	return BC_STS_SUCCESS;
}

/**
 * crystalhd_delete_dioq - Delete Generic DIO queue
 * @adp: Adapter instance
 * @dioq: DIOQ instance..
 *
 * Return:
 *  None.
 *
 * Release Generic DIO queue. This function will remove
 * all the entries from the Queue and will release data
 * by calling the call back provided during creation.
 *
 */
void crystalhd_delete_dioq(struct crystalhd_adp *adp,
			 struct crystalhd_dioq *dioq)
{
	void *temp;

	if (!dioq || (dioq->sig != BC_LINK_DIOQ_SIG))
		return;

	do {
		temp = crystalhd_dioq_fetch(dioq);
		if (temp && dioq->data_rel_cb)
			dioq->data_rel_cb(dioq->cb_context, temp);
	} while (temp);
	dioq->sig = 0;
	kfree(dioq);
}

/**
 * crystalhd_dioq_add - Add new DIO request element.
 * @ioq: DIO queue instance
 * @t: DIO request to be added.
 * @wake: True - Wake up suspended process.
 * @tag: Special tag to assign - For search and get.
 *
 * Return:
 *  Status.
 *
 * Insert new element to Q tail.
 */
enum BC_STATUS crystalhd_dioq_add(struct crystalhd_dioq *ioq, void *data,
			   bool wake, uint32_t tag)
{
	unsigned long flags = 0;
	struct crystalhd_elem *tmp;

	if (!ioq || (ioq->sig != BC_LINK_DIOQ_SIG) || !data) {
		BCMLOG_ERR("Invalid arg!!\n");
		return BC_STS_INV_ARG;
	}

	tmp = crystalhd_alloc_elem(ioq->adp);
	if (!tmp) {
		BCMLOG_ERR("No free elements.\n");
		return BC_STS_INSUFF_RES;
	}

	tmp->data = data;
	tmp->tag = tag;
	spin_lock_irqsave(&ioq->lock, flags);
	tmp->flink = (struct crystalhd_elem *)&ioq->head;
	tmp->blink = ioq->tail;
	tmp->flink->blink = tmp;
	tmp->blink->flink = tmp;
	ioq->count++;
	spin_unlock_irqrestore(&ioq->lock, flags);

	if (wake)
		crystalhd_set_event(&ioq->event);

	return BC_STS_SUCCESS;
}

/**
 * crystalhd_dioq_fetch - Fetch element from head.
 * @ioq: DIO queue instance
 *
 * Return:
 *	data element from the head..
 *
 * Remove an element from Queue.
 */
void *crystalhd_dioq_fetch(struct crystalhd_dioq *ioq)
{
	unsigned long flags = 0;
	struct crystalhd_elem *tmp;
	struct crystalhd_elem *ret = NULL;
	void *data = NULL;

	if (!ioq || (ioq->sig != BC_LINK_DIOQ_SIG)) {
		BCMLOG_ERR("Invalid arg!!\n");
		return data;
	}

	spin_lock_irqsave(&ioq->lock, flags);
	tmp = ioq->head;
	if (tmp != (struct crystalhd_elem *)&ioq->head) {
		ret = tmp;
		tmp->flink->blink = tmp->blink;
		tmp->blink->flink = tmp->flink;
		ioq->count--;
	}
	spin_unlock_irqrestore(&ioq->lock, flags);
	if (ret) {
		data = ret->data;
		crystalhd_free_elem(ioq->adp, ret);
	}

	return data;
}
/**
 * crystalhd_dioq_find_and_fetch - Search the tag and Fetch element
 * @ioq: DIO queue instance
 * @tag: Tag to search for.
 *
 * Return:
 *	element from the head..
 *
 * Search TAG and remove the element.
 */
void *crystalhd_dioq_find_and_fetch(struct crystalhd_dioq *ioq, uint32_t tag)
{
	unsigned long flags = 0;
	struct crystalhd_elem *tmp;
	struct crystalhd_elem *ret = NULL;
	void *data = NULL;

	if (!ioq || (ioq->sig != BC_LINK_DIOQ_SIG)) {
		BCMLOG_ERR("Invalid arg!!\n");
		return data;
	}

	spin_lock_irqsave(&ioq->lock, flags);
	tmp = ioq->head;
	while (tmp != (struct crystalhd_elem *)&ioq->head) {
		if (tmp->tag == tag) {
			ret = tmp;
			tmp->flink->blink = tmp->blink;
			tmp->blink->flink = tmp->flink;
			ioq->count--;
			break;
		}
		tmp = tmp->flink;
	}
	spin_unlock_irqrestore(&ioq->lock, flags);

	if (ret) {
		data = ret->data;
		crystalhd_free_elem(ioq->adp, ret);
	}

	return data;
}

/**
 * crystalhd_dioq_fetch_wait - Fetch element from Head.
 * @ioq: DIO queue instance
 * @to_secs: Wait timeout in seconds..
 *
 * Return:
 *	element from the head..
 *
 * Return element from head if Q is not empty. Wait for new element
 * if Q is empty for Timeout seconds.
 */
void *crystalhd_dioq_fetch_wait(struct crystalhd_dioq *ioq, uint32_t to_secs,
			      uint32_t *sig_pend)
{
	unsigned long flags = 0;
	int rc = 0, count;
	void *tmp = NULL;

	if (!ioq || (ioq->sig != BC_LINK_DIOQ_SIG) || !to_secs || !sig_pend) {
		BCMLOG_ERR("Invalid arg!!\n");
		return tmp;
	}

	count = to_secs;
	spin_lock_irqsave(&ioq->lock, flags);
	while ((ioq->count == 0) && count) {
		spin_unlock_irqrestore(&ioq->lock, flags);

		crystalhd_wait_on_event(&ioq->event,
				 (ioq->count > 0), 1000, rc, 0);
		if (rc == 0) {
			goto out;
		} else if (rc == -EINTR) {
			BCMLOG(BCMLOG_INFO, "Cancelling fetch wait\n");
			*sig_pend = 1;
			return tmp;
		}
		spin_lock_irqsave(&ioq->lock, flags);
		count--;
	}
	spin_unlock_irqrestore(&ioq->lock, flags);

out:
	return crystalhd_dioq_fetch(ioq);
}

/**
 * crystalhd_map_dio - Map user address for DMA
 * @adp:	Adapter instance
 * @ubuff:	User buffer to map.
 * @ubuff_sz:	User buffer size.
 * @uv_offset:	UV buffer offset.
 * @en_422mode: TRUE:422 FALSE:420 Capture mode.
 * @dir_tx:	TRUE for Tx (To device from host)
 * @dio_hnd:	Handle to mapped DIO request.
 *
 * Return:
 *	Status.
 *
 * This routine maps user address and lock pages for DMA.
 *
 */
enum BC_STATUS crystalhd_map_dio(struct crystalhd_adp *adp, void *ubuff,
			  uint32_t ubuff_sz, uint32_t uv_offset,
			  bool en_422mode, bool dir_tx,
			  struct crystalhd_dio_req **dio_hnd)
{
	struct crystalhd_dio_req	*dio;
	/* FIXME: jarod: should some of these
	 unsigned longs be uint32_t or uintptr_t? */
	unsigned long start = 0, end = 0, uaddr = 0, count = 0;
	unsigned long spsz = 0, uv_start = 0;
	int i = 0, rw = 0, res = 0, nr_pages = 0, skip_fb_sg = 0;

	if (!adp || !ubuff || !ubuff_sz || !dio_hnd) {
		BCMLOG_ERR("Invalid arg\n");
		return BC_STS_INV_ARG;
	}
	/* Compute pages */
	uaddr = (unsigned long)ubuff;
	count = (unsigned long)ubuff_sz;
	end = (uaddr + count + PAGE_SIZE - 1) >> PAGE_SHIFT;
	start = uaddr >> PAGE_SHIFT;
	nr_pages = end - start;

	if (!count || ((uaddr + count) < uaddr)) {
		BCMLOG_ERR("User addr overflow!!\n");
		return BC_STS_INV_ARG;
	}

	dio = crystalhd_alloc_dio(adp);
	if (!dio) {
		BCMLOG_ERR("dio pool empty..\n");
		return BC_STS_INSUFF_RES;
	}

	if (dir_tx) {
		rw = WRITE;
		dio->direction = DMA_TO_DEVICE;
	} else {
		rw = READ;
		dio->direction = DMA_FROM_DEVICE;
	}

	if (nr_pages > dio->max_pages) {
		BCMLOG_ERR("max_pages(%d) exceeded(%d)!!\n",
			   dio->max_pages, nr_pages);
		crystalhd_unmap_dio(adp, dio);
		return BC_STS_INSUFF_RES;
	}

	if (uv_offset) {
		uv_start = (uaddr + (unsigned long)uv_offset)  >> PAGE_SHIFT;
		dio->uinfo.uv_sg_ix = uv_start - start;
		dio->uinfo.uv_sg_off = ((uaddr + (unsigned long)uv_offset) &
					 ~PAGE_MASK);
	}

	dio->fb_size = ubuff_sz & 0x03;
	if (dio->fb_size) {
		res = copy_from_user(dio->fb_va,
				     (void __user *)(uaddr + count - dio->fb_size),
				     dio->fb_size);
		if (res) {
			BCMLOG_ERR("failed %d to copy %u fill bytes from %p\n",
				   res, dio->fb_size,
				   (void *)(uaddr + count-dio->fb_size));
			crystalhd_unmap_dio(adp, dio);
			return BC_STS_INSUFF_RES;
		}
	}

	down_read(&current->mm->mmap_sem);
	res = get_user_pages(current, current->mm, uaddr, nr_pages, rw == READ,
			     0, dio->pages, NULL);
	up_read(&current->mm->mmap_sem);

	/* Save for release..*/
	dio->sig = crystalhd_dio_locked;
	if (res < nr_pages) {
		BCMLOG_ERR("get pages failed: %d-%d\n", nr_pages, res);
		dio->page_cnt = res;
		crystalhd_unmap_dio(adp, dio);
		return BC_STS_ERROR;
	}

	dio->page_cnt = nr_pages;
	/* Get scatter/gather */
	crystalhd_init_sg(dio->sg, dio->page_cnt);
	crystalhd_set_sg(&dio->sg[0], dio->pages[0], 0, uaddr & ~PAGE_MASK);
	if (nr_pages > 1) {
		dio->sg[0].length = PAGE_SIZE - dio->sg[0].offset;

#ifdef CONFIG_X86_64
		dio->sg[0].dma_length = dio->sg[0].length;
#endif
		count -= dio->sg[0].length;
		for (i = 1; i < nr_pages; i++) {
			if (count < 4) {
				spsz = count;
				skip_fb_sg = 1;
			} else {
				spsz = (count < PAGE_SIZE) ?
					(count & ~0x03) : PAGE_SIZE;
			}
			crystalhd_set_sg(&dio->sg[i], dio->pages[i], spsz, 0);
			count -= spsz;
		}
	} else {
		if (count < 4) {
			dio->sg[0].length = count;
			skip_fb_sg = 1;
		} else {
			dio->sg[0].length = count - dio->fb_size;
		}
#ifdef CONFIG_X86_64
		dio->sg[0].dma_length = dio->sg[0].length;
#endif
	}
	dio->sg_cnt = pci_map_sg(adp->pdev, dio->sg,
				 dio->page_cnt, dio->direction);
	if (dio->sg_cnt <= 0) {
		BCMLOG_ERR("sg map %d-%d\n", dio->sg_cnt, dio->page_cnt);
		crystalhd_unmap_dio(adp, dio);
		return BC_STS_ERROR;
	}
	if (dio->sg_cnt && skip_fb_sg)
		dio->sg_cnt -= 1;
	dio->sig = crystalhd_dio_sg_mapped;
	/* Fill in User info.. */
	dio->uinfo.xfr_len   = ubuff_sz;
	dio->uinfo.xfr_buff  = ubuff;
	dio->uinfo.uv_offset = uv_offset;
	dio->uinfo.b422mode  = en_422mode;
	dio->uinfo.dir_tx    = dir_tx;

	*dio_hnd = dio;

	return BC_STS_SUCCESS;
}

/**
 * crystalhd_unmap_sgl - Release mapped resources
 * @adp: Adapter instance
 * @dio: DIO request instance
 *
 * Return:
 *	Status.
 *
 * This routine is to unmap the user buffer pages.
 */
enum BC_STATUS crystalhd_unmap_dio(struct crystalhd_adp *adp,
				 struct crystalhd_dio_req *dio)
{
	struct page *page = NULL;
	int j = 0;

	if (!adp || !dio) {
		BCMLOG_ERR("Invalid arg\n");
		return BC_STS_INV_ARG;
	}

	if ((dio->page_cnt > 0) && (dio->sig != crystalhd_dio_inv)) {
		for (j = 0; j < dio->page_cnt; j++) {
			page = dio->pages[j];
			if (page) {
				if (!PageReserved(page) &&
				    (dio->direction == DMA_FROM_DEVICE))
					SetPageDirty(page);
				page_cache_release(page);
			}
		}
	}
	if (dio->sig == crystalhd_dio_sg_mapped)
		pci_unmap_sg(adp->pdev, dio->sg, dio->page_cnt,
			 dio->direction);

	crystalhd_free_dio(adp, dio);

	return BC_STS_SUCCESS;
}

/**
 * crystalhd_create_dio_pool - Allocate mem pool for DIO management.
 * @adp: Adapter instance
 * @max_pages: Max pages for size calculation.
 *
 * Return:
 *	system error.
 *
 * This routine creates a memory pool to hold dio context for
 * for HW Direct IO operation.
 */
int crystalhd_create_dio_pool(struct crystalhd_adp *adp, uint32_t max_pages)
{
	uint32_t asz = 0, i = 0;
	uint8_t	*temp;
	struct crystalhd_dio_req *dio;

	if (!adp || !max_pages) {
		BCMLOG_ERR("Invalid Arg!!\n");
		return -EINVAL;
	}

	/* Get dma memory for fill byte handling..*/
	adp->fill_byte_pool = pci_pool_create("crystalhd_fbyte",
					      adp->pdev, 8, 8, 0);
	if (!adp->fill_byte_pool) {
		BCMLOG_ERR("failed to create fill byte pool\n");
		return -ENOMEM;
	}

	/* Get the max size from user based on 420/422 modes */
	asz =  (sizeof(*dio->pages) * max_pages) +
	       (sizeof(*dio->sg) * max_pages) + sizeof(*dio);

	BCMLOG(BCMLOG_DBG, "Initializing Dio pool %d %d %x %p\n",
	       BC_LINK_SG_POOL_SZ, max_pages, asz, adp->fill_byte_pool);

	for (i = 0; i < BC_LINK_SG_POOL_SZ; i++) {
		temp = kzalloc(asz, GFP_KERNEL);
		if ((temp) == NULL) {
			BCMLOG_ERR("Failed to alloc %d mem\n", asz);
			return -ENOMEM;
		}

		dio = (struct crystalhd_dio_req *)temp;
		temp += sizeof(*dio);
		dio->pages = (struct page **)temp;
		temp += (sizeof(*dio->pages) * max_pages);
		dio->sg = (struct scatterlist *)temp;
		dio->max_pages = max_pages;
		dio->fb_va = pci_pool_alloc(adp->fill_byte_pool, GFP_KERNEL,
					    &dio->fb_pa);
		if (!dio->fb_va) {
			BCMLOG_ERR("fill byte alloc failed.\n");
			return -ENOMEM;
		}

		crystalhd_free_dio(adp, dio);
	}

	return 0;
}

/**
 * crystalhd_destroy_dio_pool - Release DIO mem pool.
 * @adp: Adapter instance
 *
 * Return:
 *	none.
 *
 * This routine releases dio memory pool during close.
 */
void crystalhd_destroy_dio_pool(struct crystalhd_adp *adp)
{
	struct crystalhd_dio_req *dio;
	int count = 0;

	if (!adp) {
		BCMLOG_ERR("Invalid Arg!!\n");
		return;
	}

	do {
		dio = crystalhd_alloc_dio(adp);
		if (dio) {
			if (dio->fb_va)
				pci_pool_free(adp->fill_byte_pool,
					      dio->fb_va, dio->fb_pa);
			count++;
			kfree(dio);
		}
	} while (dio);

	if (adp->fill_byte_pool) {
		pci_pool_destroy(adp->fill_byte_pool);
		adp->fill_byte_pool = NULL;
	}

	BCMLOG(BCMLOG_DBG, "Released dio pool %d\n", count);
}

/**
 * crystalhd_create_elem_pool - List element pool creation.
 * @adp: Adapter instance
 * @pool_size: Number of elements in the pool.
 *
 * Return:
 *	0 - success, <0 error
 *
 * Create general purpose list element pool to hold pending,
 * and active requests.
 */
int crystalhd_create_elem_pool(struct crystalhd_adp *adp,
		uint32_t pool_size)
{
	uint32_t i;
	struct crystalhd_elem *temp;

	if (!adp || !pool_size)
		return -EINVAL;

	for (i = 0; i < pool_size; i++) {
		temp = kzalloc(sizeof(*temp), GFP_KERNEL);
		if (!temp) {
			BCMLOG_ERR("kalloc failed\n");
			return -ENOMEM;
		}
		crystalhd_free_elem(adp, temp);
	}
	BCMLOG(BCMLOG_DBG, "allocated %d elem\n", pool_size);
	return 0;
}

/**
 * crystalhd_delete_elem_pool - List element pool deletion.
 * @adp: Adapter instance
 *
 * Return:
 *	none
 *
 * Delete general purpose list element pool.
 */
void crystalhd_delete_elem_pool(struct crystalhd_adp *adp)
{
	struct crystalhd_elem *temp;
	int dbg_cnt = 0;

	if (!adp)
		return;

	do {
		temp = crystalhd_alloc_elem(adp);
		if (temp) {
			kfree(temp);
			dbg_cnt++;
		}
	} while (temp);

	BCMLOG(BCMLOG_DBG, "released %d elem\n", dbg_cnt);
}

/*================ Debug support routines.. ================================*/
void crystalhd_show_buffer(uint32_t off, uint8_t *buff, uint32_t dwcount)
{
	uint32_t i, k = 1;

	for (i = 0; i < dwcount; i++) {
		if (k == 1)
			BCMLOG(BCMLOG_DATA, "0x%08X : ", off);

		BCMLOG(BCMLOG_DATA, " 0x%08X ", *((uint32_t *)buff));

		buff += sizeof(uint32_t);
		off  += sizeof(uint32_t);
		k++;
		if ((i == dwcount - 1) || (k > 4)) {
			BCMLOG(BCMLOG_DATA, "\n");
			k = 1;
		}
	}
}
