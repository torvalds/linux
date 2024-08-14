// SPDX-License-Identifier: GPL-2.0
/*
 * channel program interfaces
 *
 * Copyright IBM Corp. 2017
 *
 * Author(s): Dong Jia Shi <bjsdjshi@linux.vnet.ibm.com>
 *            Xiao Feng Ren <renxiaof@linux.vnet.ibm.com>
 */

#include <linux/ratelimit.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/iommu.h>
#include <linux/vfio.h>
#include <asm/idals.h>

#include "vfio_ccw_cp.h"
#include "vfio_ccw_private.h"

struct page_array {
	/* Array that stores pages need to pin. */
	dma_addr_t		*pa_iova;
	/* Array that receives the pinned pages. */
	struct page		**pa_page;
	/* Number of pages pinned from @pa_iova. */
	int			pa_nr;
};

struct ccwchain {
	struct list_head	next;
	struct ccw1		*ch_ccw;
	/* Guest physical address of the current chain. */
	u64			ch_iova;
	/* Count of the valid ccws in chain. */
	int			ch_len;
	/* Pinned PAGEs for the original data. */
	struct page_array	*ch_pa;
};

/*
 * page_array_alloc() - alloc memory for page array
 * @pa: page_array on which to perform the operation
 * @len: number of pages that should be pinned from @iova
 *
 * Attempt to allocate memory for page array.
 *
 * Usage of page_array:
 * We expect (pa_nr == 0) and (pa_iova == NULL), any field in
 * this structure will be filled in by this function.
 *
 * Returns:
 *         0 if page array is allocated
 *   -EINVAL if pa->pa_nr is not initially zero, or pa->pa_iova is not NULL
 *   -ENOMEM if alloc failed
 */
static int page_array_alloc(struct page_array *pa, unsigned int len)
{
	if (pa->pa_nr || pa->pa_iova)
		return -EINVAL;

	if (len == 0)
		return -EINVAL;

	pa->pa_nr = len;

	pa->pa_iova = kcalloc(len, sizeof(*pa->pa_iova), GFP_KERNEL);
	if (!pa->pa_iova)
		return -ENOMEM;

	pa->pa_page = kcalloc(len, sizeof(*pa->pa_page), GFP_KERNEL);
	if (!pa->pa_page) {
		kfree(pa->pa_iova);
		return -ENOMEM;
	}

	return 0;
}

/*
 * page_array_unpin() - Unpin user pages in memory
 * @pa: page_array on which to perform the operation
 * @vdev: the vfio device to perform the operation
 * @pa_nr: number of user pages to unpin
 * @unaligned: were pages unaligned on the pin request
 *
 * Only unpin if any pages were pinned to begin with, i.e. pa_nr > 0,
 * otherwise only clear pa->pa_nr
 */
static void page_array_unpin(struct page_array *pa,
			     struct vfio_device *vdev, int pa_nr, bool unaligned)
{
	int unpinned = 0, npage = 1;

	while (unpinned < pa_nr) {
		dma_addr_t *first = &pa->pa_iova[unpinned];
		dma_addr_t *last = &first[npage];

		if (unpinned + npage < pa_nr &&
		    *first + npage * PAGE_SIZE == *last &&
		    !unaligned) {
			npage++;
			continue;
		}

		vfio_unpin_pages(vdev, *first, npage);
		unpinned += npage;
		npage = 1;
	}

	pa->pa_nr = 0;
}

/*
 * page_array_pin() - Pin user pages in memory
 * @pa: page_array on which to perform the operation
 * @vdev: the vfio device to perform pin operations
 * @unaligned: are pages aligned to 4K boundary?
 *
 * Returns number of pages pinned upon success.
 * If the pin request partially succeeds, or fails completely,
 * all pages are left unpinned and a negative error value is returned.
 *
 * Requests to pin "aligned" pages can be coalesced into a single
 * vfio_pin_pages request for the sake of efficiency, based on the
 * expectation of 4K page requests. Unaligned requests are probably
 * dealing with 2K "pages", and cannot be coalesced without
 * reworking this logic to incorporate that math.
 */
static int page_array_pin(struct page_array *pa, struct vfio_device *vdev, bool unaligned)
{
	int pinned = 0, npage = 1;
	int ret = 0;

	while (pinned < pa->pa_nr) {
		dma_addr_t *first = &pa->pa_iova[pinned];
		dma_addr_t *last = &first[npage];

		if (pinned + npage < pa->pa_nr &&
		    *first + npage * PAGE_SIZE == *last &&
		    !unaligned) {
			npage++;
			continue;
		}

		ret = vfio_pin_pages(vdev, *first, npage,
				     IOMMU_READ | IOMMU_WRITE,
				     &pa->pa_page[pinned]);
		if (ret < 0) {
			goto err_out;
		} else if (ret > 0 && ret != npage) {
			pinned += ret;
			ret = -EINVAL;
			goto err_out;
		}
		pinned += npage;
		npage = 1;
	}

	return ret;

err_out:
	page_array_unpin(pa, vdev, pinned, unaligned);
	return ret;
}

/* Unpin the pages before releasing the memory. */
static void page_array_unpin_free(struct page_array *pa, struct vfio_device *vdev, bool unaligned)
{
	page_array_unpin(pa, vdev, pa->pa_nr, unaligned);
	kfree(pa->pa_page);
	kfree(pa->pa_iova);
}

static bool page_array_iova_pinned(struct page_array *pa, u64 iova, u64 length)
{
	u64 iova_pfn_start = iova >> PAGE_SHIFT;
	u64 iova_pfn_end = (iova + length - 1) >> PAGE_SHIFT;
	u64 pfn;
	int i;

	for (i = 0; i < pa->pa_nr; i++) {
		pfn = pa->pa_iova[i] >> PAGE_SHIFT;
		if (pfn >= iova_pfn_start && pfn <= iova_pfn_end)
			return true;
	}

	return false;
}
/* Create the list of IDAL words for a page_array. */
static inline void page_array_idal_create_words(struct page_array *pa,
						dma64_t *idaws)
{
	int i;

	/*
	 * Idal words (execept the first one) rely on the memory being 4k
	 * aligned. If a user virtual address is 4K aligned, then it's
	 * corresponding kernel physical address will also be 4K aligned. Thus
	 * there will be no problem here to simply use the phys to create an
	 * idaw.
	 */

	for (i = 0; i < pa->pa_nr; i++) {
		idaws[i] = virt_to_dma64(page_to_virt(pa->pa_page[i]));

		/* Incorporate any offset from each starting address */
		idaws[i] = dma64_add(idaws[i], pa->pa_iova[i] & ~PAGE_MASK);
	}
}

static void convert_ccw0_to_ccw1(struct ccw1 *source, unsigned long len)
{
	struct ccw0 ccw0;
	struct ccw1 *pccw1 = source;
	int i;

	for (i = 0; i < len; i++) {
		ccw0 = *(struct ccw0 *)pccw1;
		if ((pccw1->cmd_code & 0x0f) == CCW_CMD_TIC) {
			pccw1->cmd_code = CCW_CMD_TIC;
			pccw1->flags = 0;
			pccw1->count = 0;
		} else {
			pccw1->cmd_code = ccw0.cmd_code;
			pccw1->flags = ccw0.flags;
			pccw1->count = ccw0.count;
		}
		pccw1->cda = u32_to_dma32(ccw0.cda);
		pccw1++;
	}
}

#define idal_is_2k(_cp) (!(_cp)->orb.cmd.c64 || (_cp)->orb.cmd.i2k)

/*
 * Helpers to operate ccwchain.
 */
#define ccw_is_read(_ccw) (((_ccw)->cmd_code & 0x03) == 0x02)
#define ccw_is_read_backward(_ccw) (((_ccw)->cmd_code & 0x0F) == 0x0C)
#define ccw_is_sense(_ccw) (((_ccw)->cmd_code & 0x0F) == CCW_CMD_BASIC_SENSE)

#define ccw_is_noop(_ccw) ((_ccw)->cmd_code == CCW_CMD_NOOP)

#define ccw_is_tic(_ccw) ((_ccw)->cmd_code == CCW_CMD_TIC)

#define ccw_is_idal(_ccw) ((_ccw)->flags & CCW_FLAG_IDA)
#define ccw_is_skip(_ccw) ((_ccw)->flags & CCW_FLAG_SKIP)

#define ccw_is_chain(_ccw) ((_ccw)->flags & (CCW_FLAG_CC | CCW_FLAG_DC))

/*
 * ccw_does_data_transfer()
 *
 * Determine whether a CCW will move any data, such that the guest pages
 * would need to be pinned before performing the I/O.
 *
 * Returns 1 if yes, 0 if no.
 */
static inline int ccw_does_data_transfer(struct ccw1 *ccw)
{
	/* If the count field is zero, then no data will be transferred */
	if (ccw->count == 0)
		return 0;

	/* If the command is a NOP, then no data will be transferred */
	if (ccw_is_noop(ccw))
		return 0;

	/* If the skip flag is off, then data will be transferred */
	if (!ccw_is_skip(ccw))
		return 1;

	/*
	 * If the skip flag is on, it is only meaningful if the command
	 * code is a read, read backward, sense, or sense ID.  In those
	 * cases, no data will be transferred.
	 */
	if (ccw_is_read(ccw) || ccw_is_read_backward(ccw))
		return 0;

	if (ccw_is_sense(ccw))
		return 0;

	/* The skip flag is on, but it is ignored for this command code. */
	return 1;
}

/*
 * is_cpa_within_range()
 *
 * @cpa: channel program address being questioned
 * @head: address of the beginning of a CCW chain
 * @len: number of CCWs within the chain
 *
 * Determine whether the address of a CCW (whether a new chain,
 * or the target of a TIC) falls within a range (including the end points).
 *
 * Returns 1 if yes, 0 if no.
 */
static inline int is_cpa_within_range(dma32_t cpa, u32 head, int len)
{
	u32 tail = head + (len - 1) * sizeof(struct ccw1);
	u32 gcpa = dma32_to_u32(cpa);

	return head <= gcpa && gcpa <= tail;
}

static inline int is_tic_within_range(struct ccw1 *ccw, u32 head, int len)
{
	if (!ccw_is_tic(ccw))
		return 0;

	return is_cpa_within_range(ccw->cda, head, len);
}

static struct ccwchain *ccwchain_alloc(struct channel_program *cp, int len)
{
	struct ccwchain *chain;

	chain = kzalloc(sizeof(*chain), GFP_KERNEL);
	if (!chain)
		return NULL;

	chain->ch_ccw = kcalloc(len, sizeof(*chain->ch_ccw), GFP_DMA | GFP_KERNEL);
	if (!chain->ch_ccw)
		goto out_err;

	chain->ch_pa = kcalloc(len, sizeof(*chain->ch_pa), GFP_KERNEL);
	if (!chain->ch_pa)
		goto out_err;

	list_add_tail(&chain->next, &cp->ccwchain_list);

	return chain;

out_err:
	kfree(chain->ch_ccw);
	kfree(chain);
	return NULL;
}

static void ccwchain_free(struct ccwchain *chain)
{
	list_del(&chain->next);
	kfree(chain->ch_pa);
	kfree(chain->ch_ccw);
	kfree(chain);
}

/* Free resource for a ccw that allocated memory for its cda. */
static void ccwchain_cda_free(struct ccwchain *chain, int idx)
{
	struct ccw1 *ccw = &chain->ch_ccw[idx];

	if (ccw_is_tic(ccw))
		return;

	kfree(dma32_to_virt(ccw->cda));
}

/**
 * ccwchain_calc_length - calculate the length of the ccw chain.
 * @iova: guest physical address of the target ccw chain
 * @cp: channel_program on which to perform the operation
 *
 * This is the chain length not considering any TICs.
 * You need to do a new round for each TIC target.
 *
 * The program is also validated for absence of not yet supported
 * indirect data addressing scenarios.
 *
 * Returns: the length of the ccw chain or -errno.
 */
static int ccwchain_calc_length(u64 iova, struct channel_program *cp)
{
	struct ccw1 *ccw = cp->guest_cp;
	int cnt = 0;

	do {
		cnt++;

		/*
		 * We want to keep counting if the current CCW has the
		 * command-chaining flag enabled, or if it is a TIC CCW
		 * that loops back into the current chain.  The latter
		 * is used for device orientation, where the CCW PRIOR to
		 * the TIC can either jump to the TIC or a CCW immediately
		 * after the TIC, depending on the results of its operation.
		 */
		if (!ccw_is_chain(ccw) && !is_tic_within_range(ccw, iova, cnt))
			break;

		ccw++;
	} while (cnt < CCWCHAIN_LEN_MAX + 1);

	if (cnt == CCWCHAIN_LEN_MAX + 1)
		cnt = -EINVAL;

	return cnt;
}

static int tic_target_chain_exists(struct ccw1 *tic, struct channel_program *cp)
{
	struct ccwchain *chain;
	u32 ccw_head;

	list_for_each_entry(chain, &cp->ccwchain_list, next) {
		ccw_head = chain->ch_iova;
		if (is_cpa_within_range(tic->cda, ccw_head, chain->ch_len))
			return 1;
	}

	return 0;
}

static int ccwchain_loop_tic(struct ccwchain *chain,
			     struct channel_program *cp);

static int ccwchain_handle_ccw(dma32_t cda, struct channel_program *cp)
{
	struct vfio_device *vdev =
		&container_of(cp, struct vfio_ccw_private, cp)->vdev;
	struct ccwchain *chain;
	int len, ret;
	u32 gcda;

	gcda = dma32_to_u32(cda);
	/* Copy 2K (the most we support today) of possible CCWs */
	ret = vfio_dma_rw(vdev, gcda, cp->guest_cp, CCWCHAIN_LEN_MAX * sizeof(struct ccw1), false);
	if (ret)
		return ret;

	/* Convert any Format-0 CCWs to Format-1 */
	if (!cp->orb.cmd.fmt)
		convert_ccw0_to_ccw1(cp->guest_cp, CCWCHAIN_LEN_MAX);

	/* Count the CCWs in the current chain */
	len = ccwchain_calc_length(gcda, cp);
	if (len < 0)
		return len;

	/* Need alloc a new chain for this one. */
	chain = ccwchain_alloc(cp, len);
	if (!chain)
		return -ENOMEM;

	chain->ch_len = len;
	chain->ch_iova = gcda;

	/* Copy the actual CCWs into the new chain */
	memcpy(chain->ch_ccw, cp->guest_cp, len * sizeof(struct ccw1));

	/* Loop for tics on this new chain. */
	ret = ccwchain_loop_tic(chain, cp);

	if (ret)
		ccwchain_free(chain);

	return ret;
}

/* Loop for TICs. */
static int ccwchain_loop_tic(struct ccwchain *chain, struct channel_program *cp)
{
	struct ccw1 *tic;
	int i, ret;

	for (i = 0; i < chain->ch_len; i++) {
		tic = &chain->ch_ccw[i];

		if (!ccw_is_tic(tic))
			continue;

		/* May transfer to an existing chain. */
		if (tic_target_chain_exists(tic, cp))
			continue;

		/* Build a ccwchain for the next segment */
		ret = ccwchain_handle_ccw(tic->cda, cp);
		if (ret)
			return ret;
	}

	return 0;
}

static int ccwchain_fetch_tic(struct ccw1 *ccw,
			      struct channel_program *cp)
{
	struct ccwchain *iter;
	u32 offset, ccw_head;

	list_for_each_entry(iter, &cp->ccwchain_list, next) {
		ccw_head = iter->ch_iova;
		if (is_cpa_within_range(ccw->cda, ccw_head, iter->ch_len)) {
			/* Calculate offset of TIC target */
			offset = dma32_to_u32(ccw->cda) - ccw_head;
			ccw->cda = virt_to_dma32((void *)iter->ch_ccw + offset);
			return 0;
		}
	}

	return -EFAULT;
}

static dma64_t *get_guest_idal(struct ccw1 *ccw, struct channel_program *cp, int idaw_nr)
{
	struct vfio_device *vdev =
		&container_of(cp, struct vfio_ccw_private, cp)->vdev;
	dma64_t *idaws;
	dma32_t *idaws_f1;
	int idal_len = idaw_nr * sizeof(*idaws);
	int idaw_size = idal_is_2k(cp) ? PAGE_SIZE / 2 : PAGE_SIZE;
	int idaw_mask = ~(idaw_size - 1);
	int i, ret;

	idaws = kcalloc(idaw_nr, sizeof(*idaws), GFP_DMA | GFP_KERNEL);
	if (!idaws)
		return ERR_PTR(-ENOMEM);

	if (ccw_is_idal(ccw)) {
		/* Copy IDAL from guest */
		ret = vfio_dma_rw(vdev, dma32_to_u32(ccw->cda), idaws, idal_len, false);
		if (ret) {
			kfree(idaws);
			return ERR_PTR(ret);
		}
	} else {
		/* Fabricate an IDAL based off CCW data address */
		if (cp->orb.cmd.c64) {
			idaws[0] = u64_to_dma64(dma32_to_u32(ccw->cda));
			for (i = 1; i < idaw_nr; i++) {
				idaws[i] = dma64_add(idaws[i - 1], idaw_size);
				idaws[i] = dma64_and(idaws[i], idaw_mask);
			}
		} else {
			idaws_f1 = (dma32_t *)idaws;
			idaws_f1[0] = ccw->cda;
			for (i = 1; i < idaw_nr; i++) {
				idaws_f1[i] = dma32_add(idaws_f1[i - 1], idaw_size);
				idaws_f1[i] = dma32_and(idaws_f1[i], idaw_mask);
			}
		}
	}

	return idaws;
}

/*
 * ccw_count_idaws() - Calculate the number of IDAWs needed to transfer
 * a specified amount of data
 *
 * @ccw: The Channel Command Word being translated
 * @cp: Channel Program being processed
 *
 * The ORB is examined, since it specifies what IDAWs could actually be
 * used by any CCW in the channel program, regardless of whether or not
 * the CCW actually does. An ORB that does not specify Format-2-IDAW
 * Control could still contain a CCW with an IDAL, which would be
 * Format-1 and thus only move 2K with each IDAW. Thus all CCWs within
 * the channel program must follow the same size requirements.
 */
static int ccw_count_idaws(struct ccw1 *ccw,
			   struct channel_program *cp)
{
	struct vfio_device *vdev =
		&container_of(cp, struct vfio_ccw_private, cp)->vdev;
	u64 iova;
	int size = cp->orb.cmd.c64 ? sizeof(u64) : sizeof(u32);
	int ret;
	int bytes = 1;

	if (ccw->count)
		bytes = ccw->count;

	if (ccw_is_idal(ccw)) {
		/* Read first IDAW to check its starting address. */
		/* All subsequent IDAWs will be 2K- or 4K-aligned. */
		ret = vfio_dma_rw(vdev, dma32_to_u32(ccw->cda), &iova, size, false);
		if (ret)
			return ret;

		/*
		 * Format-1 IDAWs only occupy the first 32 bits,
		 * and bit 0 is always off.
		 */
		if (!cp->orb.cmd.c64)
			iova = iova >> 32;
	} else {
		iova = dma32_to_u32(ccw->cda);
	}

	/* Format-1 IDAWs operate on 2K each */
	if (!cp->orb.cmd.c64)
		return idal_2k_nr_words((void *)iova, bytes);

	/* Using the 2K variant of Format-2 IDAWs? */
	if (cp->orb.cmd.i2k)
		return idal_2k_nr_words((void *)iova, bytes);

	/* The 'usual' case is 4K Format-2 IDAWs */
	return idal_nr_words((void *)iova, bytes);
}

static int ccwchain_fetch_ccw(struct ccw1 *ccw,
			      struct page_array *pa,
			      struct channel_program *cp)
{
	struct vfio_device *vdev =
		&container_of(cp, struct vfio_ccw_private, cp)->vdev;
	dma64_t *idaws;
	dma32_t *idaws_f1;
	int ret;
	int idaw_nr;
	int i;

	/* Calculate size of IDAL */
	idaw_nr = ccw_count_idaws(ccw, cp);
	if (idaw_nr < 0)
		return idaw_nr;

	/* Allocate an IDAL from host storage */
	idaws = get_guest_idal(ccw, cp, idaw_nr);
	if (IS_ERR(idaws)) {
		ret = PTR_ERR(idaws);
		goto out_init;
	}

	/*
	 * Allocate an array of pages to pin/translate.
	 * The number of pages is actually the count of the idaws
	 * required for the data transfer, since we only only support
	 * 4K IDAWs today.
	 */
	ret = page_array_alloc(pa, idaw_nr);
	if (ret < 0)
		goto out_free_idaws;

	/*
	 * Copy guest IDAWs into page_array, in case the memory they
	 * occupy is not contiguous.
	 */
	idaws_f1 = (dma32_t *)idaws;
	for (i = 0; i < idaw_nr; i++) {
		if (cp->orb.cmd.c64)
			pa->pa_iova[i] = dma64_to_u64(idaws[i]);
		else
			pa->pa_iova[i] = dma32_to_u32(idaws_f1[i]);
	}

	if (ccw_does_data_transfer(ccw)) {
		ret = page_array_pin(pa, vdev, idal_is_2k(cp));
		if (ret < 0)
			goto out_unpin;
	} else {
		pa->pa_nr = 0;
	}

	ccw->cda = virt_to_dma32(idaws);
	ccw->flags |= CCW_FLAG_IDA;

	/* Populate the IDAL with pinned/translated addresses from page */
	page_array_idal_create_words(pa, idaws);

	return 0;

out_unpin:
	page_array_unpin_free(pa, vdev, idal_is_2k(cp));
out_free_idaws:
	kfree(idaws);
out_init:
	ccw->cda = 0;
	return ret;
}

/*
 * Fetch one ccw.
 * To reduce memory copy, we'll pin the cda page in memory,
 * and to get rid of the cda 2G limitation of ccw1, we'll translate
 * direct ccws to idal ccws.
 */
static int ccwchain_fetch_one(struct ccw1 *ccw,
			      struct page_array *pa,
			      struct channel_program *cp)

{
	if (ccw_is_tic(ccw))
		return ccwchain_fetch_tic(ccw, cp);

	return ccwchain_fetch_ccw(ccw, pa, cp);
}

/**
 * cp_init() - allocate ccwchains for a channel program.
 * @cp: channel_program on which to perform the operation
 * @orb: control block for the channel program from the guest
 *
 * This creates one or more ccwchain(s), and copies the raw data of
 * the target channel program from @orb->cmd.iova to the new ccwchain(s).
 *
 * Limitations:
 * 1. Supports idal(c64) ccw chaining.
 * 2. Supports 4k idaw.
 *
 * Returns:
 *   %0 on success and a negative error value on failure.
 */
int cp_init(struct channel_program *cp, union orb *orb)
{
	struct vfio_device *vdev =
		&container_of(cp, struct vfio_ccw_private, cp)->vdev;
	/* custom ratelimit used to avoid flood during guest IPL */
	static DEFINE_RATELIMIT_STATE(ratelimit_state, 5 * HZ, 1);
	int ret;

	/* this is an error in the caller */
	if (cp->initialized)
		return -EBUSY;

	/*
	 * We only support prefetching the channel program. We assume all channel
	 * programs executed by supported guests likewise support prefetching.
	 * Executing a channel program that does not specify prefetching will
	 * typically not cause an error, but a warning is issued to help identify
	 * the problem if something does break.
	 */
	if (!orb->cmd.pfch && __ratelimit(&ratelimit_state))
		dev_warn(
			vdev->dev,
			"Prefetching channel program even though prefetch not specified in ORB");

	INIT_LIST_HEAD(&cp->ccwchain_list);
	memcpy(&cp->orb, orb, sizeof(*orb));

	/* Build a ccwchain for the first CCW segment */
	ret = ccwchain_handle_ccw(orb->cmd.cpa, cp);

	if (!ret)
		cp->initialized = true;

	return ret;
}


/**
 * cp_free() - free resources for channel program.
 * @cp: channel_program on which to perform the operation
 *
 * This unpins the memory pages and frees the memory space occupied by
 * @cp, which must have been returned by a previous call to cp_init().
 * Otherwise, undefined behavior occurs.
 */
void cp_free(struct channel_program *cp)
{
	struct vfio_device *vdev =
		&container_of(cp, struct vfio_ccw_private, cp)->vdev;
	struct ccwchain *chain, *temp;
	int i;

	if (!cp->initialized)
		return;

	cp->initialized = false;
	list_for_each_entry_safe(chain, temp, &cp->ccwchain_list, next) {
		for (i = 0; i < chain->ch_len; i++) {
			page_array_unpin_free(&chain->ch_pa[i], vdev, idal_is_2k(cp));
			ccwchain_cda_free(chain, i);
		}
		ccwchain_free(chain);
	}
}

/**
 * cp_prefetch() - translate a guest physical address channel program to
 *                 a real-device runnable channel program.
 * @cp: channel_program on which to perform the operation
 *
 * This function translates the guest-physical-address channel program
 * and stores the result to ccwchain list. @cp must have been
 * initialized by a previous call with cp_init(). Otherwise, undefined
 * behavior occurs.
 * For each chain composing the channel program:
 * - On entry ch_len holds the count of CCWs to be translated.
 * - On exit ch_len is adjusted to the count of successfully translated CCWs.
 * This allows cp_free to find in ch_len the count of CCWs to free in a chain.
 *
 * The S/390 CCW Translation APIS (prefixed by 'cp_') are introduced
 * as helpers to do ccw chain translation inside the kernel. Basically
 * they accept a channel program issued by a virtual machine, and
 * translate the channel program to a real-device runnable channel
 * program.
 *
 * These APIs will copy the ccws into kernel-space buffers, and update
 * the guest physical addresses with their corresponding host physical
 * addresses.  Then channel I/O device drivers could issue the
 * translated channel program to real devices to perform an I/O
 * operation.
 *
 * These interfaces are designed to support translation only for
 * channel programs, which are generated and formatted by a
 * guest. Thus this will make it possible for things like VFIO to
 * leverage the interfaces to passthrough a channel I/O mediated
 * device in QEMU.
 *
 * We support direct ccw chaining by translating them to idal ccws.
 *
 * Returns:
 *   %0 on success and a negative error value on failure.
 */
int cp_prefetch(struct channel_program *cp)
{
	struct ccwchain *chain;
	struct ccw1 *ccw;
	struct page_array *pa;
	int len, idx, ret;

	/* this is an error in the caller */
	if (!cp->initialized)
		return -EINVAL;

	list_for_each_entry(chain, &cp->ccwchain_list, next) {
		len = chain->ch_len;
		for (idx = 0; idx < len; idx++) {
			ccw = &chain->ch_ccw[idx];
			pa = &chain->ch_pa[idx];

			ret = ccwchain_fetch_one(ccw, pa, cp);
			if (ret)
				goto out_err;
		}
	}

	return 0;
out_err:
	/* Only cleanup the chain elements that were actually translated. */
	chain->ch_len = idx;
	list_for_each_entry_continue(chain, &cp->ccwchain_list, next) {
		chain->ch_len = 0;
	}
	return ret;
}

/**
 * cp_get_orb() - get the orb of the channel program
 * @cp: channel_program on which to perform the operation
 * @sch: subchannel the operation will be performed against
 *
 * This function returns the address of the updated orb of the channel
 * program. Channel I/O device drivers could use this orb to issue a
 * ssch.
 */
union orb *cp_get_orb(struct channel_program *cp, struct subchannel *sch)
{
	union orb *orb;
	struct ccwchain *chain;
	struct ccw1 *cpa;

	/* this is an error in the caller */
	if (!cp->initialized)
		return NULL;

	orb = &cp->orb;

	orb->cmd.intparm = (u32)virt_to_phys(sch);
	orb->cmd.fmt = 1;

	/*
	 * Everything built by vfio-ccw is a Format-2 IDAL.
	 * If the input was a Format-1 IDAL, indicate that
	 * 2K Format-2 IDAWs were created here.
	 */
	if (!orb->cmd.c64)
		orb->cmd.i2k = 1;
	orb->cmd.c64 = 1;

	if (orb->cmd.lpm == 0)
		orb->cmd.lpm = sch->lpm;

	chain = list_first_entry(&cp->ccwchain_list, struct ccwchain, next);
	cpa = chain->ch_ccw;
	orb->cmd.cpa = virt_to_dma32(cpa);

	return orb;
}

/**
 * cp_update_scsw() - update scsw for a channel program.
 * @cp: channel_program on which to perform the operation
 * @scsw: I/O results of the channel program and also the target to be
 *        updated
 *
 * @scsw contains the I/O results of the channel program that pointed
 * to by @cp. However what @scsw->cpa stores is a host physical
 * address, which is meaningless for the guest, which is waiting for
 * the I/O results.
 *
 * This function updates @scsw->cpa to its coressponding guest physical
 * address.
 */
void cp_update_scsw(struct channel_program *cp, union scsw *scsw)
{
	struct ccwchain *chain;
	dma32_t cpa = scsw->cmd.cpa;
	u32 ccw_head;

	if (!cp->initialized)
		return;

	/*
	 * LATER:
	 * For now, only update the cmd.cpa part. We may need to deal with
	 * other portions of the schib as well, even if we don't return them
	 * in the ioctl directly. Path status changes etc.
	 */
	list_for_each_entry(chain, &cp->ccwchain_list, next) {
		ccw_head = dma32_to_u32(virt_to_dma32(chain->ch_ccw));
		/*
		 * On successful execution, cpa points just beyond the end
		 * of the chain.
		 */
		if (is_cpa_within_range(cpa, ccw_head, chain->ch_len + 1)) {
			/*
			 * (cpa - ccw_head) is the offset value of the host
			 * physical ccw to its chain head.
			 * Adding this value to the guest physical ccw chain
			 * head gets us the guest cpa:
			 * cpa = chain->ch_iova + (cpa - ccw_head)
			 */
			cpa = dma32_add(cpa, chain->ch_iova - ccw_head);
			break;
		}
	}

	scsw->cmd.cpa = cpa;
}

/**
 * cp_iova_pinned() - check if an iova is pinned for a ccw chain.
 * @cp: channel_program on which to perform the operation
 * @iova: the iova to check
 * @length: the length to check from @iova
 *
 * If the @iova is currently pinned for the ccw chain, return true;
 * else return false.
 */
bool cp_iova_pinned(struct channel_program *cp, u64 iova, u64 length)
{
	struct ccwchain *chain;
	int i;

	if (!cp->initialized)
		return false;

	list_for_each_entry(chain, &cp->ccwchain_list, next) {
		for (i = 0; i < chain->ch_len; i++)
			if (page_array_iova_pinned(&chain->ch_pa[i], iova, length))
				return true;
	}

	return false;
}
