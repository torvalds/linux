// SPDX-License-Identifier: GPL-2.0
/*
 * channel program interfaces
 *
 * Copyright IBM Corp. 2017
 *
 * Author(s): Dong Jia Shi <bjsdjshi@linux.vnet.ibm.com>
 *            Xiao Feng Ren <renxiaof@linux.vnet.ibm.com>
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/iommu.h>
#include <linux/vfio.h>
#include <asm/idals.h>

#include "vfio_ccw_cp.h"

/*
 * Max length for ccw chain.
 * XXX: Limit to 256, need to check more?
 */
#define CCWCHAIN_LEN_MAX	256

struct pfn_array {
	/* Starting guest physical I/O address. */
	unsigned long		pa_iova;
	/* Array that stores PFNs of the pages need to pin. */
	unsigned long		*pa_iova_pfn;
	/* Array that receives PFNs of the pages pinned. */
	unsigned long		*pa_pfn;
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
	struct pfn_array	*ch_pa;
};

/*
 * pfn_array_alloc() - alloc memory for PFNs
 * @pa: pfn_array on which to perform the operation
 * @iova: target guest physical address
 * @len: number of bytes that should be pinned from @iova
 *
 * Attempt to allocate memory for PFNs.
 *
 * Usage of pfn_array:
 * We expect (pa_nr == 0) and (pa_iova_pfn == NULL), any field in
 * this structure will be filled in by this function.
 *
 * Returns:
 *         0 if PFNs are allocated
 *   -EINVAL if pa->pa_nr is not initially zero, or pa->pa_iova_pfn is not NULL
 *   -ENOMEM if alloc failed
 */
static int pfn_array_alloc(struct pfn_array *pa, u64 iova, unsigned int len)
{
	int i;

	if (pa->pa_nr || pa->pa_iova_pfn)
		return -EINVAL;

	pa->pa_iova = iova;

	pa->pa_nr = ((iova & ~PAGE_MASK) + len + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
	if (!pa->pa_nr)
		return -EINVAL;

	pa->pa_iova_pfn = kcalloc(pa->pa_nr,
				  sizeof(*pa->pa_iova_pfn) +
				  sizeof(*pa->pa_pfn),
				  GFP_KERNEL);
	if (unlikely(!pa->pa_iova_pfn))
		return -ENOMEM;
	pa->pa_pfn = pa->pa_iova_pfn + pa->pa_nr;

	pa->pa_iova_pfn[0] = pa->pa_iova >> PAGE_SHIFT;
	pa->pa_pfn[0] = -1ULL;
	for (i = 1; i < pa->pa_nr; i++) {
		pa->pa_iova_pfn[i] = pa->pa_iova_pfn[i - 1] + 1;
		pa->pa_pfn[i] = -1ULL;
	}

	return 0;
}

/*
 * pfn_array_pin() - Pin user pages in memory
 * @pa: pfn_array on which to perform the operation
 * @mdev: the mediated device to perform pin operations
 *
 * Returns number of pages pinned upon success.
 * If the pin request partially succeeds, or fails completely,
 * all pages are left unpinned and a negative error value is returned.
 */
static int pfn_array_pin(struct pfn_array *pa, struct device *mdev)
{
	int ret = 0;

	ret = vfio_pin_pages(mdev, pa->pa_iova_pfn, pa->pa_nr,
			     IOMMU_READ | IOMMU_WRITE, pa->pa_pfn);

	if (ret < 0) {
		goto err_out;
	} else if (ret > 0 && ret != pa->pa_nr) {
		vfio_unpin_pages(mdev, pa->pa_iova_pfn, ret);
		ret = -EINVAL;
		goto err_out;
	}

	return ret;

err_out:
	pa->pa_nr = 0;

	return ret;
}

/* Unpin the pages before releasing the memory. */
static void pfn_array_unpin_free(struct pfn_array *pa, struct device *mdev)
{
	/* Only unpin if any pages were pinned to begin with */
	if (pa->pa_nr)
		vfio_unpin_pages(mdev, pa->pa_iova_pfn, pa->pa_nr);
	pa->pa_nr = 0;
	kfree(pa->pa_iova_pfn);
}

static bool pfn_array_iova_pinned(struct pfn_array *pa, unsigned long iova)
{
	unsigned long iova_pfn = iova >> PAGE_SHIFT;
	int i;

	for (i = 0; i < pa->pa_nr; i++)
		if (pa->pa_iova_pfn[i] == iova_pfn)
			return true;

	return false;
}
/* Create the list of IDAL words for a pfn_array. */
static inline void pfn_array_idal_create_words(
	struct pfn_array *pa,
	unsigned long *idaws)
{
	int i;

	/*
	 * Idal words (execept the first one) rely on the memory being 4k
	 * aligned. If a user virtual address is 4K aligned, then it's
	 * corresponding kernel physical address will also be 4K aligned. Thus
	 * there will be no problem here to simply use the phys to create an
	 * idaw.
	 */

	for (i = 0; i < pa->pa_nr; i++)
		idaws[i] = pa->pa_pfn[i] << PAGE_SHIFT;

	/* Adjust the first IDAW, since it may not start on a page boundary */
	idaws[0] += pa->pa_iova & (PAGE_SIZE - 1);
}


/*
 * Within the domain (@mdev), copy @n bytes from a guest physical
 * address (@iova) to a host physical address (@to).
 */
static long copy_from_iova(struct device *mdev,
			   void *to, u64 iova,
			   unsigned long n)
{
	struct pfn_array pa = {0};
	u64 from;
	int i, ret;
	unsigned long l, m;

	ret = pfn_array_alloc(&pa, iova, n);
	if (ret < 0)
		return ret;

	ret = pfn_array_pin(&pa, mdev);
	if (ret < 0) {
		pfn_array_unpin_free(&pa, mdev);
		return ret;
	}

	l = n;
	for (i = 0; i < pa.pa_nr; i++) {
		from = pa.pa_pfn[i] << PAGE_SHIFT;
		m = PAGE_SIZE;
		if (i == 0) {
			from += iova & (PAGE_SIZE - 1);
			m -= iova & (PAGE_SIZE - 1);
		}

		m = min(l, m);
		memcpy(to + (n - l), (void *)from, m);

		l -= m;
		if (l == 0)
			break;
	}

	pfn_array_unpin_free(&pa, mdev);

	return l;
}

static long copy_ccw_from_iova(struct channel_program *cp,
			       struct ccw1 *to, u64 iova,
			       unsigned long len)
{
	struct ccw0 ccw0;
	struct ccw1 *pccw1;
	int ret;
	int i;

	ret = copy_from_iova(cp->mdev, to, iova, len * sizeof(struct ccw1));
	if (ret)
		return ret;

	if (!cp->orb.cmd.fmt) {
		pccw1 = to;
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
			pccw1->cda = ccw0.cda;
			pccw1++;
		}
	}

	return ret;
}

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
static inline int is_cpa_within_range(u32 cpa, u32 head, int len)
{
	u32 tail = head + (len - 1) * sizeof(struct ccw1);

	return (head <= cpa && cpa <= tail);
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
	void *data;
	size_t size;

	/* Make ccw address aligned to 8. */
	size = ((sizeof(*chain) + 7L) & -8L) +
		sizeof(*chain->ch_ccw) * len +
		sizeof(*chain->ch_pa) * len;
	chain = kzalloc(size, GFP_DMA | GFP_KERNEL);
	if (!chain)
		return NULL;

	data = (u8 *)chain + ((sizeof(*chain) + 7L) & -8L);
	chain->ch_ccw = (struct ccw1 *)data;

	data = (u8 *)(chain->ch_ccw) + sizeof(*chain->ch_ccw) * len;
	chain->ch_pa = (struct pfn_array *)data;

	chain->ch_len = len;

	list_add_tail(&chain->next, &cp->ccwchain_list);

	return chain;
}

static void ccwchain_free(struct ccwchain *chain)
{
	list_del(&chain->next);
	kfree(chain);
}

/* Free resource for a ccw that allocated memory for its cda. */
static void ccwchain_cda_free(struct ccwchain *chain, int idx)
{
	struct ccw1 *ccw = chain->ch_ccw + idx;

	if (ccw_is_tic(ccw))
		return;

	kfree((void *)(u64)ccw->cda);
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
	struct ccw1 *ccw, *p;
	int cnt;

	/*
	 * Copy current chain from guest to host kernel.
	 * Currently the chain length is limited to CCWCHAIN_LEN_MAX (256).
	 * So copying 2K is enough (safe).
	 */
	p = ccw = kcalloc(CCWCHAIN_LEN_MAX, sizeof(*ccw), GFP_KERNEL);
	if (!ccw)
		return -ENOMEM;

	cnt = copy_ccw_from_iova(cp, ccw, iova, CCWCHAIN_LEN_MAX);
	if (cnt) {
		kfree(ccw);
		return cnt;
	}

	cnt = 0;
	do {
		cnt++;

		/*
		 * As we don't want to fail direct addressing even if the
		 * orb specified one of the unsupported formats, we defer
		 * checking for IDAWs in unsupported formats to here.
		 */
		if ((!cp->orb.cmd.c64 || cp->orb.cmd.i2k) && ccw_is_idal(ccw)) {
			kfree(p);
			return -EOPNOTSUPP;
		}

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

	kfree(p);
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

static int ccwchain_handle_ccw(u32 cda, struct channel_program *cp)
{
	struct ccwchain *chain;
	int len, ret;

	/* Get chain length. */
	len = ccwchain_calc_length(cda, cp);
	if (len < 0)
		return len;

	/* Need alloc a new chain for this one. */
	chain = ccwchain_alloc(cp, len);
	if (!chain)
		return -ENOMEM;
	chain->ch_iova = cda;

	/* Copy the new chain from user. */
	ret = copy_ccw_from_iova(cp, chain->ch_ccw, cda, len);
	if (ret) {
		ccwchain_free(chain);
		return ret;
	}

	/* Loop for tics on this new chain. */
	return ccwchain_loop_tic(chain, cp);
}

/* Loop for TICs. */
static int ccwchain_loop_tic(struct ccwchain *chain, struct channel_program *cp)
{
	struct ccw1 *tic;
	int i, ret;

	for (i = 0; i < chain->ch_len; i++) {
		tic = chain->ch_ccw + i;

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

static int ccwchain_fetch_tic(struct ccwchain *chain,
			      int idx,
			      struct channel_program *cp)
{
	struct ccw1 *ccw = chain->ch_ccw + idx;
	struct ccwchain *iter;
	u32 ccw_head;

	list_for_each_entry(iter, &cp->ccwchain_list, next) {
		ccw_head = iter->ch_iova;
		if (is_cpa_within_range(ccw->cda, ccw_head, iter->ch_len)) {
			ccw->cda = (__u32) (addr_t) (((char *)iter->ch_ccw) +
						     (ccw->cda - ccw_head));
			return 0;
		}
	}

	return -EFAULT;
}

static int ccwchain_fetch_direct(struct ccwchain *chain,
				 int idx,
				 struct channel_program *cp)
{
	struct ccw1 *ccw;
	struct pfn_array *pa;
	unsigned long *idaws;
	int ret;
	int bytes = 1;
	int idaw_nr = 1;

	ccw = chain->ch_ccw + idx;

	if (ccw->count) {
		bytes = ccw->count;
		idaw_nr = idal_nr_words((void *)(u64)ccw->cda, ccw->count);
	}

	/*
	 * Pin data page(s) in memory.
	 * The number of pages actually is the count of the idaws which will be
	 * needed when translating a direct ccw to a idal ccw.
	 */
	pa = chain->ch_pa + idx;
	ret = pfn_array_alloc(pa, ccw->cda, bytes);
	if (ret < 0)
		goto out_unpin;

	if (ccw_does_data_transfer(ccw)) {
		ret = pfn_array_pin(pa, cp->mdev);
		if (ret < 0)
			goto out_unpin;
	} else {
		pa->pa_nr = 0;
	}

	/* Translate this direct ccw to a idal ccw. */
	idaws = kcalloc(idaw_nr, sizeof(*idaws), GFP_DMA | GFP_KERNEL);
	if (!idaws) {
		ret = -ENOMEM;
		goto out_unpin;
	}
	ccw->cda = (__u32) virt_to_phys(idaws);
	ccw->flags |= CCW_FLAG_IDA;

	pfn_array_idal_create_words(pa, idaws);

	return 0;

out_unpin:
	pfn_array_unpin_free(pa, cp->mdev);
out_init:
	ccw->cda = 0;
	return ret;
}

static int ccwchain_fetch_idal(struct ccwchain *chain,
			       int idx,
			       struct channel_program *cp)
{
	struct ccw1 *ccw;
	struct pfn_array *pa;
	unsigned long *idaws;
	u64 idaw_iova;
	unsigned int idaw_nr, idaw_len;
	int i, ret;
	int bytes = 1;

	ccw = chain->ch_ccw + idx;

	if (ccw->count)
		bytes = ccw->count;

	/* Calculate size of idaws. */
	ret = copy_from_iova(cp->mdev, &idaw_iova, ccw->cda, sizeof(idaw_iova));
	if (ret)
		return ret;
	idaw_nr = idal_nr_words((void *)(idaw_iova), bytes);
	idaw_len = idaw_nr * sizeof(*idaws);

	/* Pin data page(s) in memory. */
	pa = chain->ch_pa + idx;
	ret = pfn_array_alloc(pa, idaw_iova, bytes);
	if (ret)
		goto out_init;

	/* Translate idal ccw to use new allocated idaws. */
	idaws = kzalloc(idaw_len, GFP_DMA | GFP_KERNEL);
	if (!idaws) {
		ret = -ENOMEM;
		goto out_unpin;
	}

	ret = copy_from_iova(cp->mdev, idaws, ccw->cda, idaw_len);
	if (ret)
		goto out_free_idaws;

	ccw->cda = virt_to_phys(idaws);

	for (i = 0; i < idaw_nr; i++)
		pa->pa_iova_pfn[i] = idaws[i] >> PAGE_SHIFT;

	if (ccw_does_data_transfer(ccw)) {
		ret = pfn_array_pin(pa, cp->mdev);
		if (ret < 0)
			goto out_free_idaws;
	} else {
		pa->pa_nr = 0;
	}

	pfn_array_idal_create_words(pa, idaws);

	return 0;

out_free_idaws:
	kfree(idaws);
out_unpin:
	pfn_array_unpin_free(pa, cp->mdev);
out_init:
	ccw->cda = 0;
	return ret;
}

/*
 * Fetch one ccw.
 * To reduce memory copy, we'll pin the cda page in memory,
 * and to get rid of the cda 2G limitiaion of ccw1, we'll translate
 * direct ccws to idal ccws.
 */
static int ccwchain_fetch_one(struct ccwchain *chain,
			      int idx,
			      struct channel_program *cp)
{
	struct ccw1 *ccw = chain->ch_ccw + idx;

	if (ccw_is_tic(ccw))
		return ccwchain_fetch_tic(chain, idx, cp);

	if (ccw_is_idal(ccw))
		return ccwchain_fetch_idal(chain, idx, cp);

	return ccwchain_fetch_direct(chain, idx, cp);
}

/**
 * cp_init() - allocate ccwchains for a channel program.
 * @cp: channel_program on which to perform the operation
 * @mdev: the mediated device to perform pin/unpin operations
 * @orb: control block for the channel program from the guest
 *
 * This creates one or more ccwchain(s), and copies the raw data of
 * the target channel program from @orb->cmd.iova to the new ccwchain(s).
 *
 * Limitations:
 * 1. Supports only prefetch enabled mode.
 * 2. Supports idal(c64) ccw chaining.
 * 3. Supports 4k idaw.
 *
 * Returns:
 *   %0 on success and a negative error value on failure.
 */
int cp_init(struct channel_program *cp, struct device *mdev, union orb *orb)
{
	int ret;

	/*
	 * XXX:
	 * Only support prefetch enable mode now.
	 */
	if (!orb->cmd.pfch)
		return -EOPNOTSUPP;

	INIT_LIST_HEAD(&cp->ccwchain_list);
	memcpy(&cp->orb, orb, sizeof(*orb));
	cp->mdev = mdev;

	/* Build a ccwchain for the first CCW segment */
	ret = ccwchain_handle_ccw(orb->cmd.cpa, cp);
	if (ret)
		cp_free(cp);

	/* It is safe to force: if not set but idals used
	 * ccwchain_calc_length returns an error.
	 */
	cp->orb.cmd.c64 = 1;

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
	struct ccwchain *chain, *temp;
	int i;

	if (!cp->initialized)
		return;

	cp->initialized = false;
	list_for_each_entry_safe(chain, temp, &cp->ccwchain_list, next) {
		for (i = 0; i < chain->ch_len; i++) {
			pfn_array_unpin_free(chain->ch_pa + i, cp->mdev);
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
 * the guest phsical addresses with their corresponding host physical
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
	int len, idx, ret;

	/* this is an error in the caller */
	if (!cp->initialized)
		return -EINVAL;

	list_for_each_entry(chain, &cp->ccwchain_list, next) {
		len = chain->ch_len;
		for (idx = 0; idx < len; idx++) {
			ret = ccwchain_fetch_one(chain, idx, cp);
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
 * @intparm: new intparm for the returned orb
 * @lpm: candidate value of the logical-path mask for the returned orb
 *
 * This function returns the address of the updated orb of the channel
 * program. Channel I/O device drivers could use this orb to issue a
 * ssch.
 */
union orb *cp_get_orb(struct channel_program *cp, u32 intparm, u8 lpm)
{
	union orb *orb;
	struct ccwchain *chain;
	struct ccw1 *cpa;

	/* this is an error in the caller */
	if (!cp->initialized)
		return NULL;

	orb = &cp->orb;

	orb->cmd.intparm = intparm;
	orb->cmd.fmt = 1;
	orb->cmd.key = PAGE_DEFAULT_KEY >> 4;

	if (orb->cmd.lpm == 0)
		orb->cmd.lpm = lpm;

	chain = list_first_entry(&cp->ccwchain_list, struct ccwchain, next);
	cpa = chain->ch_ccw;
	orb->cmd.cpa = (__u32) __pa(cpa);

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
	u32 cpa = scsw->cmd.cpa;
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
		ccw_head = (u32)(u64)chain->ch_ccw;
		/*
		 * On successful execution, cpa points just beyond the end
		 * of the chain.
		 */
		if (is_cpa_within_range(cpa, ccw_head, chain->ch_len + 1)) {
			/*
			 * (cpa - ccw_head) is the offset value of the host
			 * physical ccw to its chain head.
			 * Adding this value to the guest physical ccw chain
			 * head gets us the guest cpa.
			 */
			cpa = chain->ch_iova + (cpa - ccw_head);
			break;
		}
	}

	scsw->cmd.cpa = cpa;
}

/**
 * cp_iova_pinned() - check if an iova is pinned for a ccw chain.
 * @cp: channel_program on which to perform the operation
 * @iova: the iova to check
 *
 * If the @iova is currently pinned for the ccw chain, return true;
 * else return false.
 */
bool cp_iova_pinned(struct channel_program *cp, u64 iova)
{
	struct ccwchain *chain;
	int i;

	if (!cp->initialized)
		return false;

	list_for_each_entry(chain, &cp->ccwchain_list, next) {
		for (i = 0; i < chain->ch_len; i++)
			if (pfn_array_iova_pinned(chain->ch_pa + i, iova))
				return true;
	}

	return false;
}
