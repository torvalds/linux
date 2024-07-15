// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CXL Flash Device Driver
 *
 * Written by: Manoj N. Kumar <manoj@linux.vnet.ibm.com>, IBM Corporation
 *             Matthew R. Ochs <mrochs@linux.vnet.ibm.com>, IBM Corporation
 *
 * Copyright (C) 2015 IBM Corporation
 */

#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/syscalls.h>
#include <asm/unaligned.h>
#include <asm/bitsperlong.h>

#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <uapi/scsi/cxlflash_ioctl.h>

#include "sislite.h"
#include "common.h"
#include "vlun.h"
#include "superpipe.h"

/**
 * marshal_virt_to_resize() - translate uvirtual to resize structure
 * @virt:	Source structure from which to translate/copy.
 * @resize:	Destination structure for the translate/copy.
 */
static void marshal_virt_to_resize(struct dk_cxlflash_uvirtual *virt,
				   struct dk_cxlflash_resize *resize)
{
	resize->hdr = virt->hdr;
	resize->context_id = virt->context_id;
	resize->rsrc_handle = virt->rsrc_handle;
	resize->req_size = virt->lun_size;
	resize->last_lba = virt->last_lba;
}

/**
 * marshal_clone_to_rele() - translate clone to release structure
 * @clone:	Source structure from which to translate/copy.
 * @release:	Destination structure for the translate/copy.
 */
static void marshal_clone_to_rele(struct dk_cxlflash_clone *clone,
				  struct dk_cxlflash_release *release)
{
	release->hdr = clone->hdr;
	release->context_id = clone->context_id_dst;
}

/**
 * ba_init() - initializes a block allocator
 * @ba_lun:	Block allocator to initialize.
 *
 * Return: 0 on success, -errno on failure
 */
static int ba_init(struct ba_lun *ba_lun)
{
	struct ba_lun_info *bali = NULL;
	int lun_size_au = 0, i = 0;
	int last_word_underflow = 0;
	u64 *lam;

	pr_debug("%s: Initializing LUN: lun_id=%016llx "
		 "ba_lun->lsize=%lx ba_lun->au_size=%lX\n",
		__func__, ba_lun->lun_id, ba_lun->lsize, ba_lun->au_size);

	/* Calculate bit map size */
	lun_size_au = ba_lun->lsize / ba_lun->au_size;
	if (lun_size_au == 0) {
		pr_debug("%s: Requested LUN size of 0!\n", __func__);
		return -EINVAL;
	}

	/* Allocate lun information container */
	bali = kzalloc(sizeof(struct ba_lun_info), GFP_KERNEL);
	if (unlikely(!bali)) {
		pr_err("%s: Failed to allocate lun_info lun_id=%016llx\n",
		       __func__, ba_lun->lun_id);
		return -ENOMEM;
	}

	bali->total_aus = lun_size_au;
	bali->lun_bmap_size = lun_size_au / BITS_PER_LONG;

	if (lun_size_au % BITS_PER_LONG)
		bali->lun_bmap_size++;

	/* Allocate bitmap space */
	bali->lun_alloc_map = kzalloc((bali->lun_bmap_size * sizeof(u64)),
				      GFP_KERNEL);
	if (unlikely(!bali->lun_alloc_map)) {
		pr_err("%s: Failed to allocate lun allocation map: "
		       "lun_id=%016llx\n", __func__, ba_lun->lun_id);
		kfree(bali);
		return -ENOMEM;
	}

	/* Initialize the bit map size and set all bits to '1' */
	bali->free_aun_cnt = lun_size_au;

	for (i = 0; i < bali->lun_bmap_size; i++)
		bali->lun_alloc_map[i] = 0xFFFFFFFFFFFFFFFFULL;

	/* If the last word not fully utilized, mark extra bits as allocated */
	last_word_underflow = (bali->lun_bmap_size * BITS_PER_LONG);
	last_word_underflow -= bali->free_aun_cnt;
	if (last_word_underflow > 0) {
		lam = &bali->lun_alloc_map[bali->lun_bmap_size - 1];
		for (i = (HIBIT - last_word_underflow + 1);
		     i < BITS_PER_LONG;
		     i++)
			clear_bit(i, (ulong *)lam);
	}

	/* Initialize high elevator index, low/curr already at 0 from kzalloc */
	bali->free_high_idx = bali->lun_bmap_size;

	/* Allocate clone map */
	bali->aun_clone_map = kzalloc((bali->total_aus * sizeof(u8)),
				      GFP_KERNEL);
	if (unlikely(!bali->aun_clone_map)) {
		pr_err("%s: Failed to allocate clone map: lun_id=%016llx\n",
		       __func__, ba_lun->lun_id);
		kfree(bali->lun_alloc_map);
		kfree(bali);
		return -ENOMEM;
	}

	/* Pass the allocated LUN info as a handle to the user */
	ba_lun->ba_lun_handle = bali;

	pr_debug("%s: Successfully initialized the LUN: "
		 "lun_id=%016llx bitmap size=%x, free_aun_cnt=%llx\n",
		__func__, ba_lun->lun_id, bali->lun_bmap_size,
		bali->free_aun_cnt);
	return 0;
}

/**
 * find_free_range() - locates a free bit within the block allocator
 * @low:	First word in block allocator to start search.
 * @high:	Last word in block allocator to search.
 * @bali:	LUN information structure owning the block allocator to search.
 * @bit_word:	Passes back the word in the block allocator owning the free bit.
 *
 * Return: The bit position within the passed back word, -1 on failure
 */
static int find_free_range(u32 low,
			   u32 high,
			   struct ba_lun_info *bali, int *bit_word)
{
	int i;
	u64 bit_pos = -1;
	ulong *lam, num_bits;

	for (i = low; i < high; i++)
		if (bali->lun_alloc_map[i] != 0) {
			lam = (ulong *)&bali->lun_alloc_map[i];
			num_bits = (sizeof(*lam) * BITS_PER_BYTE);
			bit_pos = find_first_bit(lam, num_bits);

			pr_devel("%s: Found free bit %llu in LUN "
				 "map entry %016llx at bitmap index = %d\n",
				 __func__, bit_pos, bali->lun_alloc_map[i], i);

			*bit_word = i;
			bali->free_aun_cnt--;
			clear_bit(bit_pos, lam);
			break;
		}

	return bit_pos;
}

/**
 * ba_alloc() - allocates a block from the block allocator
 * @ba_lun:	Block allocator from which to allocate a block.
 *
 * Return: The allocated block, -1 on failure
 */
static u64 ba_alloc(struct ba_lun *ba_lun)
{
	u64 bit_pos = -1;
	int bit_word = 0;
	struct ba_lun_info *bali = NULL;

	bali = ba_lun->ba_lun_handle;

	pr_debug("%s: Received block allocation request: "
		 "lun_id=%016llx free_aun_cnt=%llx\n",
		 __func__, ba_lun->lun_id, bali->free_aun_cnt);

	if (bali->free_aun_cnt == 0) {
		pr_debug("%s: No space left on LUN: lun_id=%016llx\n",
			 __func__, ba_lun->lun_id);
		return -1ULL;
	}

	/* Search to find a free entry, curr->high then low->curr */
	bit_pos = find_free_range(bali->free_curr_idx,
				  bali->free_high_idx, bali, &bit_word);
	if (bit_pos == -1) {
		bit_pos = find_free_range(bali->free_low_idx,
					  bali->free_curr_idx,
					  bali, &bit_word);
		if (bit_pos == -1) {
			pr_debug("%s: Could not find an allocation unit on LUN:"
				 " lun_id=%016llx\n", __func__, ba_lun->lun_id);
			return -1ULL;
		}
	}

	/* Update the free_curr_idx */
	if (bit_pos == HIBIT)
		bali->free_curr_idx = bit_word + 1;
	else
		bali->free_curr_idx = bit_word;

	pr_debug("%s: Allocating AU number=%llx lun_id=%016llx "
		 "free_aun_cnt=%llx\n", __func__,
		 ((bit_word * BITS_PER_LONG) + bit_pos), ba_lun->lun_id,
		 bali->free_aun_cnt);

	return (u64) ((bit_word * BITS_PER_LONG) + bit_pos);
}

/**
 * validate_alloc() - validates the specified block has been allocated
 * @bali:		LUN info owning the block allocator.
 * @aun:		Block to validate.
 *
 * Return: 0 on success, -1 on failure
 */
static int validate_alloc(struct ba_lun_info *bali, u64 aun)
{
	int idx = 0, bit_pos = 0;

	idx = aun / BITS_PER_LONG;
	bit_pos = aun % BITS_PER_LONG;

	if (test_bit(bit_pos, (ulong *)&bali->lun_alloc_map[idx]))
		return -1;

	return 0;
}

/**
 * ba_free() - frees a block from the block allocator
 * @ba_lun:	Block allocator from which to allocate a block.
 * @to_free:	Block to free.
 *
 * Return: 0 on success, -1 on failure
 */
static int ba_free(struct ba_lun *ba_lun, u64 to_free)
{
	int idx = 0, bit_pos = 0;
	struct ba_lun_info *bali = NULL;

	bali = ba_lun->ba_lun_handle;

	if (validate_alloc(bali, to_free)) {
		pr_debug("%s: AUN %llx is not allocated on lun_id=%016llx\n",
			 __func__, to_free, ba_lun->lun_id);
		return -1;
	}

	pr_debug("%s: Received a request to free AU=%llx lun_id=%016llx "
		 "free_aun_cnt=%llx\n", __func__, to_free, ba_lun->lun_id,
		 bali->free_aun_cnt);

	if (bali->aun_clone_map[to_free] > 0) {
		pr_debug("%s: AUN %llx lun_id=%016llx cloned. Clone count=%x\n",
			 __func__, to_free, ba_lun->lun_id,
			 bali->aun_clone_map[to_free]);
		bali->aun_clone_map[to_free]--;
		return 0;
	}

	idx = to_free / BITS_PER_LONG;
	bit_pos = to_free % BITS_PER_LONG;

	set_bit(bit_pos, (ulong *)&bali->lun_alloc_map[idx]);
	bali->free_aun_cnt++;

	if (idx < bali->free_low_idx)
		bali->free_low_idx = idx;
	else if (idx > bali->free_high_idx)
		bali->free_high_idx = idx;

	pr_debug("%s: Successfully freed AU bit_pos=%x bit map index=%x "
		 "lun_id=%016llx free_aun_cnt=%llx\n", __func__, bit_pos, idx,
		 ba_lun->lun_id, bali->free_aun_cnt);

	return 0;
}

/**
 * ba_clone() - Clone a chunk of the block allocation table
 * @ba_lun:	Block allocator from which to allocate a block.
 * @to_clone:	Block to clone.
 *
 * Return: 0 on success, -1 on failure
 */
static int ba_clone(struct ba_lun *ba_lun, u64 to_clone)
{
	struct ba_lun_info *bali = ba_lun->ba_lun_handle;

	if (validate_alloc(bali, to_clone)) {
		pr_debug("%s: AUN=%llx not allocated on lun_id=%016llx\n",
			 __func__, to_clone, ba_lun->lun_id);
		return -1;
	}

	pr_debug("%s: Received a request to clone AUN %llx on lun_id=%016llx\n",
		 __func__, to_clone, ba_lun->lun_id);

	if (bali->aun_clone_map[to_clone] == MAX_AUN_CLONE_CNT) {
		pr_debug("%s: AUN %llx on lun_id=%016llx hit max clones already\n",
			 __func__, to_clone, ba_lun->lun_id);
		return -1;
	}

	bali->aun_clone_map[to_clone]++;

	return 0;
}

/**
 * ba_space() - returns the amount of free space left in the block allocator
 * @ba_lun:	Block allocator.
 *
 * Return: Amount of free space in block allocator
 */
static u64 ba_space(struct ba_lun *ba_lun)
{
	struct ba_lun_info *bali = ba_lun->ba_lun_handle;

	return bali->free_aun_cnt;
}

/**
 * cxlflash_ba_terminate() - frees resources associated with the block allocator
 * @ba_lun:	Block allocator.
 *
 * Safe to call in a partially allocated state.
 */
void cxlflash_ba_terminate(struct ba_lun *ba_lun)
{
	struct ba_lun_info *bali = ba_lun->ba_lun_handle;

	if (bali) {
		kfree(bali->aun_clone_map);
		kfree(bali->lun_alloc_map);
		kfree(bali);
		ba_lun->ba_lun_handle = NULL;
	}
}

/**
 * init_vlun() - initializes a LUN for virtual use
 * @lli:	LUN information structure that owns the block allocator.
 *
 * Return: 0 on success, -errno on failure
 */
static int init_vlun(struct llun_info *lli)
{
	int rc = 0;
	struct glun_info *gli = lli->parent;
	struct blka *blka = &gli->blka;

	memset(blka, 0, sizeof(*blka));
	mutex_init(&blka->mutex);

	/* LUN IDs are unique per port, save the index instead */
	blka->ba_lun.lun_id = lli->lun_index;
	blka->ba_lun.lsize = gli->max_lba + 1;
	blka->ba_lun.lba_size = gli->blk_len;

	blka->ba_lun.au_size = MC_CHUNK_SIZE;
	blka->nchunk = blka->ba_lun.lsize / MC_CHUNK_SIZE;

	rc = ba_init(&blka->ba_lun);
	if (unlikely(rc))
		pr_debug("%s: cannot init block_alloc, rc=%d\n", __func__, rc);

	pr_debug("%s: returning rc=%d lli=%p\n", __func__, rc, lli);
	return rc;
}

/**
 * write_same16() - sends a SCSI WRITE_SAME16 (0) command to specified LUN
 * @sdev:	SCSI device associated with LUN.
 * @lba:	Logical block address to start write same.
 * @nblks:	Number of logical blocks to write same.
 *
 * The SCSI WRITE_SAME16 can take quite a while to complete. Should an EEH occur
 * while in scsi_execute_cmd(), the EEH handler will attempt to recover. As
 * part of the recovery, the handler drains all currently running ioctls,
 * waiting until they have completed before proceeding with a reset. As this
 * routine is used on the ioctl path, this can create a condition where the
 * EEH handler becomes stuck, infinitely waiting for this ioctl thread. To
 * avoid this behavior, temporarily unmark this thread as an ioctl thread by
 * releasing the ioctl read semaphore. This will allow the EEH handler to
 * proceed with a recovery while this thread is still running. Once the
 * scsi_execute_cmd() returns, reacquire the ioctl read semaphore and check the
 * adapter state in case it changed while inside of scsi_execute_cmd(). The
 * state check will wait if the adapter is still being recovered or return a
 * failure if the recovery failed. In the event that the adapter reset failed,
 * simply return the failure as the ioctl would be unable to continue.
 *
 * Note that the above puts a requirement on this routine to only be called on
 * an ioctl thread.
 *
 * Return: 0 on success, -errno on failure
 */
static int write_same16(struct scsi_device *sdev,
			u64 lba,
			u32 nblks)
{
	u8 *cmd_buf = NULL;
	u8 *scsi_cmd = NULL;
	int rc = 0;
	int result = 0;
	u64 offset = lba;
	int left = nblks;
	struct cxlflash_cfg *cfg = shost_priv(sdev->host);
	struct device *dev = &cfg->dev->dev;
	const u32 s = ilog2(sdev->sector_size) - 9;
	const u32 to = sdev->request_queue->rq_timeout;
	const u32 ws_limit =
		sdev->request_queue->limits.max_write_zeroes_sectors >> s;

	cmd_buf = kzalloc(CMD_BUFSIZE, GFP_KERNEL);
	scsi_cmd = kzalloc(MAX_COMMAND_SIZE, GFP_KERNEL);
	if (unlikely(!cmd_buf || !scsi_cmd)) {
		rc = -ENOMEM;
		goto out;
	}

	while (left > 0) {

		scsi_cmd[0] = WRITE_SAME_16;
		scsi_cmd[1] = cfg->ws_unmap ? 0x8 : 0;
		put_unaligned_be64(offset, &scsi_cmd[2]);
		put_unaligned_be32(ws_limit < left ? ws_limit : left,
				   &scsi_cmd[10]);

		/* Drop the ioctl read semaphore across lengthy call */
		up_read(&cfg->ioctl_rwsem);
		result = scsi_execute_cmd(sdev, scsi_cmd, REQ_OP_DRV_OUT,
					  cmd_buf, CMD_BUFSIZE, to,
					  CMD_RETRIES, NULL);
		down_read(&cfg->ioctl_rwsem);
		rc = check_state(cfg);
		if (rc) {
			dev_err(dev, "%s: Failed state result=%08x\n",
				__func__, result);
			rc = -ENODEV;
			goto out;
		}

		if (result) {
			dev_err_ratelimited(dev, "%s: command failed for "
					    "offset=%lld result=%08x\n",
					    __func__, offset, result);
			rc = -EIO;
			goto out;
		}
		left -= ws_limit;
		offset += ws_limit;
	}

out:
	kfree(cmd_buf);
	kfree(scsi_cmd);
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
}

/**
 * grow_lxt() - expands the translation table associated with the specified RHTE
 * @afu:	AFU associated with the host.
 * @sdev:	SCSI device associated with LUN.
 * @ctxid:	Context ID of context owning the RHTE.
 * @rhndl:	Resource handle associated with the RHTE.
 * @rhte:	Resource handle entry (RHTE).
 * @new_size:	Number of translation entries associated with RHTE.
 *
 * By design, this routine employs a 'best attempt' allocation and will
 * truncate the requested size down if there is not sufficient space in
 * the block allocator to satisfy the request but there does exist some
 * amount of space. The user is made aware of this by returning the size
 * allocated.
 *
 * Return: 0 on success, -errno on failure
 */
static int grow_lxt(struct afu *afu,
		    struct scsi_device *sdev,
		    ctx_hndl_t ctxid,
		    res_hndl_t rhndl,
		    struct sisl_rht_entry *rhte,
		    u64 *new_size)
{
	struct cxlflash_cfg *cfg = shost_priv(sdev->host);
	struct device *dev = &cfg->dev->dev;
	struct sisl_lxt_entry *lxt = NULL, *lxt_old = NULL;
	struct llun_info *lli = sdev->hostdata;
	struct glun_info *gli = lli->parent;
	struct blka *blka = &gli->blka;
	u32 av_size;
	u32 ngrps, ngrps_old;
	u64 aun;		/* chunk# allocated by block allocator */
	u64 delta = *new_size - rhte->lxt_cnt;
	u64 my_new_size;
	int i, rc = 0;

	/*
	 * Check what is available in the block allocator before re-allocating
	 * LXT array. This is done up front under the mutex which must not be
	 * released until after allocation is complete.
	 */
	mutex_lock(&blka->mutex);
	av_size = ba_space(&blka->ba_lun);
	if (unlikely(av_size <= 0)) {
		dev_dbg(dev, "%s: ba_space error av_size=%d\n",
			__func__, av_size);
		mutex_unlock(&blka->mutex);
		rc = -ENOSPC;
		goto out;
	}

	if (av_size < delta)
		delta = av_size;

	lxt_old = rhte->lxt_start;
	ngrps_old = LXT_NUM_GROUPS(rhte->lxt_cnt);
	ngrps = LXT_NUM_GROUPS(rhte->lxt_cnt + delta);

	if (ngrps != ngrps_old) {
		/* reallocate to fit new size */
		lxt = kzalloc((sizeof(*lxt) * LXT_GROUP_SIZE * ngrps),
			      GFP_KERNEL);
		if (unlikely(!lxt)) {
			mutex_unlock(&blka->mutex);
			rc = -ENOMEM;
			goto out;
		}

		/* copy over all old entries */
		memcpy(lxt, lxt_old, (sizeof(*lxt) * rhte->lxt_cnt));
	} else
		lxt = lxt_old;

	/* nothing can fail from now on */
	my_new_size = rhte->lxt_cnt + delta;

	/* add new entries to the end */
	for (i = rhte->lxt_cnt; i < my_new_size; i++) {
		/*
		 * Due to the earlier check of available space, ba_alloc
		 * cannot fail here. If it did due to internal error,
		 * leave a rlba_base of -1u which will likely be a
		 * invalid LUN (too large).
		 */
		aun = ba_alloc(&blka->ba_lun);
		if ((aun == -1ULL) || (aun >= blka->nchunk))
			dev_dbg(dev, "%s: ba_alloc error allocated chunk=%llu "
				"max=%llu\n", __func__, aun, blka->nchunk - 1);

		/* select both ports, use r/w perms from RHT */
		lxt[i].rlba_base = ((aun << MC_CHUNK_SHIFT) |
				    (lli->lun_index << LXT_LUNIDX_SHIFT) |
				    (RHT_PERM_RW << LXT_PERM_SHIFT |
				     lli->port_sel));
	}

	mutex_unlock(&blka->mutex);

	/*
	 * The following sequence is prescribed in the SISlite spec
	 * for syncing up with the AFU when adding LXT entries.
	 */
	dma_wmb(); /* Make LXT updates are visible */

	rhte->lxt_start = lxt;
	dma_wmb(); /* Make RHT entry's LXT table update visible */

	rhte->lxt_cnt = my_new_size;
	dma_wmb(); /* Make RHT entry's LXT table size update visible */

	rc = cxlflash_afu_sync(afu, ctxid, rhndl, AFU_LW_SYNC);
	if (unlikely(rc))
		rc = -EAGAIN;

	/* free old lxt if reallocated */
	if (lxt != lxt_old)
		kfree(lxt_old);
	*new_size = my_new_size;
out:
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
}

/**
 * shrink_lxt() - reduces translation table associated with the specified RHTE
 * @afu:	AFU associated with the host.
 * @sdev:	SCSI device associated with LUN.
 * @rhndl:	Resource handle associated with the RHTE.
 * @rhte:	Resource handle entry (RHTE).
 * @ctxi:	Context owning resources.
 * @new_size:	Number of translation entries associated with RHTE.
 *
 * Return: 0 on success, -errno on failure
 */
static int shrink_lxt(struct afu *afu,
		      struct scsi_device *sdev,
		      res_hndl_t rhndl,
		      struct sisl_rht_entry *rhte,
		      struct ctx_info *ctxi,
		      u64 *new_size)
{
	struct cxlflash_cfg *cfg = shost_priv(sdev->host);
	struct device *dev = &cfg->dev->dev;
	struct sisl_lxt_entry *lxt, *lxt_old;
	struct llun_info *lli = sdev->hostdata;
	struct glun_info *gli = lli->parent;
	struct blka *blka = &gli->blka;
	ctx_hndl_t ctxid = DECODE_CTXID(ctxi->ctxid);
	bool needs_ws = ctxi->rht_needs_ws[rhndl];
	bool needs_sync = !ctxi->err_recovery_active;
	u32 ngrps, ngrps_old;
	u64 aun;		/* chunk# allocated by block allocator */
	u64 delta = rhte->lxt_cnt - *new_size;
	u64 my_new_size;
	int i, rc = 0;

	lxt_old = rhte->lxt_start;
	ngrps_old = LXT_NUM_GROUPS(rhte->lxt_cnt);
	ngrps = LXT_NUM_GROUPS(rhte->lxt_cnt - delta);

	if (ngrps != ngrps_old) {
		/* Reallocate to fit new size unless new size is 0 */
		if (ngrps) {
			lxt = kzalloc((sizeof(*lxt) * LXT_GROUP_SIZE * ngrps),
				      GFP_KERNEL);
			if (unlikely(!lxt)) {
				rc = -ENOMEM;
				goto out;
			}

			/* Copy over old entries that will remain */
			memcpy(lxt, lxt_old,
			       (sizeof(*lxt) * (rhte->lxt_cnt - delta)));
		} else
			lxt = NULL;
	} else
		lxt = lxt_old;

	/* Nothing can fail from now on */
	my_new_size = rhte->lxt_cnt - delta;

	/*
	 * The following sequence is prescribed in the SISlite spec
	 * for syncing up with the AFU when removing LXT entries.
	 */
	rhte->lxt_cnt = my_new_size;
	dma_wmb(); /* Make RHT entry's LXT table size update visible */

	rhte->lxt_start = lxt;
	dma_wmb(); /* Make RHT entry's LXT table update visible */

	if (needs_sync) {
		rc = cxlflash_afu_sync(afu, ctxid, rhndl, AFU_HW_SYNC);
		if (unlikely(rc))
			rc = -EAGAIN;
	}

	if (needs_ws) {
		/*
		 * Mark the context as unavailable, so that we can release
		 * the mutex safely.
		 */
		ctxi->unavail = true;
		mutex_unlock(&ctxi->mutex);
	}

	/* Free LBAs allocated to freed chunks */
	mutex_lock(&blka->mutex);
	for (i = delta - 1; i >= 0; i--) {
		aun = lxt_old[my_new_size + i].rlba_base >> MC_CHUNK_SHIFT;
		if (needs_ws)
			write_same16(sdev, aun, MC_CHUNK_SIZE);
		ba_free(&blka->ba_lun, aun);
	}
	mutex_unlock(&blka->mutex);

	if (needs_ws) {
		/* Make the context visible again */
		mutex_lock(&ctxi->mutex);
		ctxi->unavail = false;
	}

	/* Free old lxt if reallocated */
	if (lxt != lxt_old)
		kfree(lxt_old);
	*new_size = my_new_size;
out:
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
}

/**
 * _cxlflash_vlun_resize() - changes the size of a virtual LUN
 * @sdev:	SCSI device associated with LUN owning virtual LUN.
 * @ctxi:	Context owning resources.
 * @resize:	Resize ioctl data structure.
 *
 * On successful return, the user is informed of the new size (in blocks)
 * of the virtual LUN in last LBA format. When the size of the virtual
 * LUN is zero, the last LBA is reflected as -1. See comment in the
 * prologue for _cxlflash_disk_release() regarding AFU syncs and contexts
 * on the error recovery list.
 *
 * Return: 0 on success, -errno on failure
 */
int _cxlflash_vlun_resize(struct scsi_device *sdev,
			  struct ctx_info *ctxi,
			  struct dk_cxlflash_resize *resize)
{
	struct cxlflash_cfg *cfg = shost_priv(sdev->host);
	struct device *dev = &cfg->dev->dev;
	struct llun_info *lli = sdev->hostdata;
	struct glun_info *gli = lli->parent;
	struct afu *afu = cfg->afu;
	bool put_ctx = false;

	res_hndl_t rhndl = resize->rsrc_handle;
	u64 new_size;
	u64 nsectors;
	u64 ctxid = DECODE_CTXID(resize->context_id),
	    rctxid = resize->context_id;

	struct sisl_rht_entry *rhte;

	int rc = 0;

	/*
	 * The requested size (req_size) is always assumed to be in 4k blocks,
	 * so we have to convert it here from 4k to chunk size.
	 */
	nsectors = (resize->req_size * CXLFLASH_BLOCK_SIZE) / gli->blk_len;
	new_size = DIV_ROUND_UP(nsectors, MC_CHUNK_SIZE);

	dev_dbg(dev, "%s: ctxid=%llu rhndl=%llu req_size=%llu new_size=%llu\n",
		__func__, ctxid, resize->rsrc_handle, resize->req_size,
		new_size);

	if (unlikely(gli->mode != MODE_VIRTUAL)) {
		dev_dbg(dev, "%s: LUN mode does not support resize mode=%d\n",
			__func__, gli->mode);
		rc = -EINVAL;
		goto out;

	}

	if (!ctxi) {
		ctxi = get_context(cfg, rctxid, lli, CTX_CTRL_ERR_FALLBACK);
		if (unlikely(!ctxi)) {
			dev_dbg(dev, "%s: Bad context ctxid=%llu\n",
				__func__, ctxid);
			rc = -EINVAL;
			goto out;
		}

		put_ctx = true;
	}

	rhte = get_rhte(ctxi, rhndl, lli);
	if (unlikely(!rhte)) {
		dev_dbg(dev, "%s: Bad resource handle rhndl=%u\n",
			__func__, rhndl);
		rc = -EINVAL;
		goto out;
	}

	if (new_size > rhte->lxt_cnt)
		rc = grow_lxt(afu, sdev, ctxid, rhndl, rhte, &new_size);
	else if (new_size < rhte->lxt_cnt)
		rc = shrink_lxt(afu, sdev, rhndl, rhte, ctxi, &new_size);
	else {
		/*
		 * Rare case where there is already sufficient space, just
		 * need to perform a translation sync with the AFU. This
		 * scenario likely follows a previous sync failure during
		 * a resize operation. Accordingly, perform the heavyweight
		 * form of translation sync as it is unknown which type of
		 * resize failed previously.
		 */
		rc = cxlflash_afu_sync(afu, ctxid, rhndl, AFU_HW_SYNC);
		if (unlikely(rc)) {
			rc = -EAGAIN;
			goto out;
		}
	}

	resize->hdr.return_flags = 0;
	resize->last_lba = (new_size * MC_CHUNK_SIZE * gli->blk_len);
	resize->last_lba /= CXLFLASH_BLOCK_SIZE;
	resize->last_lba--;

out:
	if (put_ctx)
		put_context(ctxi);
	dev_dbg(dev, "%s: resized to %llu returning rc=%d\n",
		__func__, resize->last_lba, rc);
	return rc;
}

int cxlflash_vlun_resize(struct scsi_device *sdev, void *resize)
{
	return _cxlflash_vlun_resize(sdev, NULL, resize);
}

/**
 * cxlflash_restore_luntable() - Restore LUN table to prior state
 * @cfg:	Internal structure associated with the host.
 */
void cxlflash_restore_luntable(struct cxlflash_cfg *cfg)
{
	struct llun_info *lli, *temp;
	u32 lind;
	int k;
	struct device *dev = &cfg->dev->dev;
	__be64 __iomem *fc_port_luns;

	mutex_lock(&global.mutex);

	list_for_each_entry_safe(lli, temp, &cfg->lluns, list) {
		if (!lli->in_table)
			continue;

		lind = lli->lun_index;
		dev_dbg(dev, "%s: Virtual LUNs on slot %d:\n", __func__, lind);

		for (k = 0; k < cfg->num_fc_ports; k++)
			if (lli->port_sel & (1 << k)) {
				fc_port_luns = get_fc_port_luns(cfg, k);
				writeq_be(lli->lun_id[k], &fc_port_luns[lind]);
				dev_dbg(dev, "\t%d=%llx\n", k, lli->lun_id[k]);
			}
	}

	mutex_unlock(&global.mutex);
}

/**
 * get_num_ports() - compute number of ports from port selection mask
 * @psm:	Port selection mask.
 *
 * Return: Population count of port selection mask
 */
static inline u8 get_num_ports(u32 psm)
{
	static const u8 bits[16] = { 0, 1, 1, 2, 1, 2, 2, 3,
				     1, 2, 2, 3, 2, 3, 3, 4 };

	return bits[psm & 0xf];
}

/**
 * init_luntable() - write an entry in the LUN table
 * @cfg:	Internal structure associated with the host.
 * @lli:	Per adapter LUN information structure.
 *
 * On successful return, a LUN table entry is created:
 *	- at the top for LUNs visible on multiple ports.
 *	- at the bottom for LUNs visible only on one port.
 *
 * Return: 0 on success, -errno on failure
 */
static int init_luntable(struct cxlflash_cfg *cfg, struct llun_info *lli)
{
	u32 chan;
	u32 lind;
	u32 nports;
	int rc = 0;
	int k;
	struct device *dev = &cfg->dev->dev;
	__be64 __iomem *fc_port_luns;

	mutex_lock(&global.mutex);

	if (lli->in_table)
		goto out;

	nports = get_num_ports(lli->port_sel);
	if (nports == 0 || nports > cfg->num_fc_ports) {
		WARN(1, "Unsupported port configuration nports=%u", nports);
		rc = -EIO;
		goto out;
	}

	if (nports > 1) {
		/*
		 * When LUN is visible from multiple ports, we will put
		 * it in the top half of the LUN table.
		 */
		for (k = 0; k < cfg->num_fc_ports; k++) {
			if (!(lli->port_sel & (1 << k)))
				continue;

			if (cfg->promote_lun_index == cfg->last_lun_index[k]) {
				rc = -ENOSPC;
				goto out;
			}
		}

		lind = lli->lun_index = cfg->promote_lun_index;
		dev_dbg(dev, "%s: Virtual LUNs on slot %d:\n", __func__, lind);

		for (k = 0; k < cfg->num_fc_ports; k++) {
			if (!(lli->port_sel & (1 << k)))
				continue;

			fc_port_luns = get_fc_port_luns(cfg, k);
			writeq_be(lli->lun_id[k], &fc_port_luns[lind]);
			dev_dbg(dev, "\t%d=%llx\n", k, lli->lun_id[k]);
		}

		cfg->promote_lun_index++;
	} else {
		/*
		 * When LUN is visible only from one port, we will put
		 * it in the bottom half of the LUN table.
		 */
		chan = PORTMASK2CHAN(lli->port_sel);
		if (cfg->promote_lun_index == cfg->last_lun_index[chan]) {
			rc = -ENOSPC;
			goto out;
		}

		lind = lli->lun_index = cfg->last_lun_index[chan];
		fc_port_luns = get_fc_port_luns(cfg, chan);
		writeq_be(lli->lun_id[chan], &fc_port_luns[lind]);
		cfg->last_lun_index[chan]--;
		dev_dbg(dev, "%s: Virtual LUNs on slot %d:\n\t%d=%llx\n",
			__func__, lind, chan, lli->lun_id[chan]);
	}

	lli->in_table = true;
out:
	mutex_unlock(&global.mutex);
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
}

/**
 * cxlflash_disk_virtual_open() - open a virtual disk of specified size
 * @sdev:	SCSI device associated with LUN owning virtual LUN.
 * @arg:	UVirtual ioctl data structure.
 *
 * On successful return, the user is informed of the resource handle
 * to be used to identify the virtual LUN and the size (in blocks) of
 * the virtual LUN in last LBA format. When the size of the virtual LUN
 * is zero, the last LBA is reflected as -1.
 *
 * Return: 0 on success, -errno on failure
 */
int cxlflash_disk_virtual_open(struct scsi_device *sdev, void *arg)
{
	struct cxlflash_cfg *cfg = shost_priv(sdev->host);
	struct device *dev = &cfg->dev->dev;
	struct llun_info *lli = sdev->hostdata;
	struct glun_info *gli = lli->parent;

	struct dk_cxlflash_uvirtual *virt = (struct dk_cxlflash_uvirtual *)arg;
	struct dk_cxlflash_resize resize;

	u64 ctxid = DECODE_CTXID(virt->context_id),
	    rctxid = virt->context_id;
	u64 lun_size = virt->lun_size;
	u64 last_lba = 0;
	u64 rsrc_handle = -1;

	int rc = 0;

	struct ctx_info *ctxi = NULL;
	struct sisl_rht_entry *rhte = NULL;

	dev_dbg(dev, "%s: ctxid=%llu ls=%llu\n", __func__, ctxid, lun_size);

	/* Setup the LUNs block allocator on first call */
	mutex_lock(&gli->mutex);
	if (gli->mode == MODE_NONE) {
		rc = init_vlun(lli);
		if (rc) {
			dev_err(dev, "%s: init_vlun failed rc=%d\n",
				__func__, rc);
			rc = -ENOMEM;
			goto err0;
		}
	}

	rc = cxlflash_lun_attach(gli, MODE_VIRTUAL, true);
	if (unlikely(rc)) {
		dev_err(dev, "%s: Failed attach to LUN (VIRTUAL)\n", __func__);
		goto err0;
	}
	mutex_unlock(&gli->mutex);

	rc = init_luntable(cfg, lli);
	if (rc) {
		dev_err(dev, "%s: init_luntable failed rc=%d\n", __func__, rc);
		goto err1;
	}

	ctxi = get_context(cfg, rctxid, lli, 0);
	if (unlikely(!ctxi)) {
		dev_err(dev, "%s: Bad context ctxid=%llu\n", __func__, ctxid);
		rc = -EINVAL;
		goto err1;
	}

	rhte = rhte_checkout(ctxi, lli);
	if (unlikely(!rhte)) {
		dev_err(dev, "%s: too many opens ctxid=%llu\n",
			__func__, ctxid);
		rc = -EMFILE;	/* too many opens  */
		goto err1;
	}

	rsrc_handle = (rhte - ctxi->rht_start);

	/* Populate RHT format 0 */
	rhte->nmask = MC_RHT_NMASK;
	rhte->fp = SISL_RHT_FP(0U, ctxi->rht_perms);

	/* Resize even if requested size is 0 */
	marshal_virt_to_resize(virt, &resize);
	resize.rsrc_handle = rsrc_handle;
	rc = _cxlflash_vlun_resize(sdev, ctxi, &resize);
	if (rc) {
		dev_err(dev, "%s: resize failed rc=%d\n", __func__, rc);
		goto err2;
	}
	last_lba = resize.last_lba;

	if (virt->hdr.flags & DK_CXLFLASH_UVIRTUAL_NEED_WRITE_SAME)
		ctxi->rht_needs_ws[rsrc_handle] = true;

	virt->hdr.return_flags = 0;
	virt->last_lba = last_lba;
	virt->rsrc_handle = rsrc_handle;

	if (get_num_ports(lli->port_sel) > 1)
		virt->hdr.return_flags |= DK_CXLFLASH_ALL_PORTS_ACTIVE;
out:
	if (likely(ctxi))
		put_context(ctxi);
	dev_dbg(dev, "%s: returning handle=%llu rc=%d llba=%llu\n",
		__func__, rsrc_handle, rc, last_lba);
	return rc;

err2:
	rhte_checkin(ctxi, rhte);
err1:
	cxlflash_lun_detach(gli);
	goto out;
err0:
	/* Special common cleanup prior to successful LUN attach */
	cxlflash_ba_terminate(&gli->blka.ba_lun);
	mutex_unlock(&gli->mutex);
	goto out;
}

/**
 * clone_lxt() - copies translation tables from source to destination RHTE
 * @afu:	AFU associated with the host.
 * @blka:	Block allocator associated with LUN.
 * @ctxid:	Context ID of context owning the RHTE.
 * @rhndl:	Resource handle associated with the RHTE.
 * @rhte:	Destination resource handle entry (RHTE).
 * @rhte_src:	Source resource handle entry (RHTE).
 *
 * Return: 0 on success, -errno on failure
 */
static int clone_lxt(struct afu *afu,
		     struct blka *blka,
		     ctx_hndl_t ctxid,
		     res_hndl_t rhndl,
		     struct sisl_rht_entry *rhte,
		     struct sisl_rht_entry *rhte_src)
{
	struct cxlflash_cfg *cfg = afu->parent;
	struct device *dev = &cfg->dev->dev;
	struct sisl_lxt_entry *lxt = NULL;
	bool locked = false;
	u32 ngrps;
	u64 aun;		/* chunk# allocated by block allocator */
	int j;
	int i = 0;
	int rc = 0;

	ngrps = LXT_NUM_GROUPS(rhte_src->lxt_cnt);

	if (ngrps) {
		/* allocate new LXTs for clone */
		lxt = kzalloc((sizeof(*lxt) * LXT_GROUP_SIZE * ngrps),
				GFP_KERNEL);
		if (unlikely(!lxt)) {
			rc = -ENOMEM;
			goto out;
		}

		/* copy over */
		memcpy(lxt, rhte_src->lxt_start,
		       (sizeof(*lxt) * rhte_src->lxt_cnt));

		/* clone the LBAs in block allocator via ref_cnt, note that the
		 * block allocator mutex must be held until it is established
		 * that this routine will complete without the need for a
		 * cleanup.
		 */
		mutex_lock(&blka->mutex);
		locked = true;
		for (i = 0; i < rhte_src->lxt_cnt; i++) {
			aun = (lxt[i].rlba_base >> MC_CHUNK_SHIFT);
			if (ba_clone(&blka->ba_lun, aun) == -1ULL) {
				rc = -EIO;
				goto err;
			}
		}
	}

	/*
	 * The following sequence is prescribed in the SISlite spec
	 * for syncing up with the AFU when adding LXT entries.
	 */
	dma_wmb(); /* Make LXT updates are visible */

	rhte->lxt_start = lxt;
	dma_wmb(); /* Make RHT entry's LXT table update visible */

	rhte->lxt_cnt = rhte_src->lxt_cnt;
	dma_wmb(); /* Make RHT entry's LXT table size update visible */

	rc = cxlflash_afu_sync(afu, ctxid, rhndl, AFU_LW_SYNC);
	if (unlikely(rc)) {
		rc = -EAGAIN;
		goto err2;
	}

out:
	if (locked)
		mutex_unlock(&blka->mutex);
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
err2:
	/* Reset the RHTE */
	rhte->lxt_cnt = 0;
	dma_wmb();
	rhte->lxt_start = NULL;
	dma_wmb();
err:
	/* free the clones already made */
	for (j = 0; j < i; j++) {
		aun = (lxt[j].rlba_base >> MC_CHUNK_SHIFT);
		ba_free(&blka->ba_lun, aun);
	}
	kfree(lxt);
	goto out;
}

/**
 * cxlflash_disk_clone() - clone a context by making snapshot of another
 * @sdev:	SCSI device associated with LUN owning virtual LUN.
 * @arg:	Clone ioctl data structure.
 *
 * This routine effectively performs cxlflash_disk_open operation for each
 * in-use virtual resource in the source context. Note that the destination
 * context must be in pristine state and cannot have any resource handles
 * open at the time of the clone.
 *
 * Return: 0 on success, -errno on failure
 */
int cxlflash_disk_clone(struct scsi_device *sdev, void *arg)
{
	struct dk_cxlflash_clone *clone = arg;
	struct cxlflash_cfg *cfg = shost_priv(sdev->host);
	struct device *dev = &cfg->dev->dev;
	struct llun_info *lli = sdev->hostdata;
	struct glun_info *gli = lli->parent;
	struct blka *blka = &gli->blka;
	struct afu *afu = cfg->afu;
	struct dk_cxlflash_release release = { { 0 }, 0 };

	struct ctx_info *ctxi_src = NULL,
			*ctxi_dst = NULL;
	struct lun_access *lun_access_src, *lun_access_dst;
	u32 perms;
	u64 ctxid_src = DECODE_CTXID(clone->context_id_src),
	    ctxid_dst = DECODE_CTXID(clone->context_id_dst),
	    rctxid_src = clone->context_id_src,
	    rctxid_dst = clone->context_id_dst;
	int i, j;
	int rc = 0;
	bool found;
	LIST_HEAD(sidecar);

	dev_dbg(dev, "%s: ctxid_src=%llu ctxid_dst=%llu\n",
		__func__, ctxid_src, ctxid_dst);

	/* Do not clone yourself */
	if (unlikely(rctxid_src == rctxid_dst)) {
		rc = -EINVAL;
		goto out;
	}

	if (unlikely(gli->mode != MODE_VIRTUAL)) {
		rc = -EINVAL;
		dev_dbg(dev, "%s: Only supported on virtual LUNs mode=%u\n",
			__func__, gli->mode);
		goto out;
	}

	ctxi_src = get_context(cfg, rctxid_src, lli, CTX_CTRL_CLONE);
	ctxi_dst = get_context(cfg, rctxid_dst, lli, 0);
	if (unlikely(!ctxi_src || !ctxi_dst)) {
		dev_dbg(dev, "%s: Bad context ctxid_src=%llu ctxid_dst=%llu\n",
			__func__, ctxid_src, ctxid_dst);
		rc = -EINVAL;
		goto out;
	}

	/* Verify there is no open resource handle in the destination context */
	for (i = 0; i < MAX_RHT_PER_CONTEXT; i++)
		if (ctxi_dst->rht_start[i].nmask != 0) {
			rc = -EINVAL;
			goto out;
		}

	/* Clone LUN access list */
	list_for_each_entry(lun_access_src, &ctxi_src->luns, list) {
		found = false;
		list_for_each_entry(lun_access_dst, &ctxi_dst->luns, list)
			if (lun_access_dst->sdev == lun_access_src->sdev) {
				found = true;
				break;
			}

		if (!found) {
			lun_access_dst = kzalloc(sizeof(*lun_access_dst),
						 GFP_KERNEL);
			if (unlikely(!lun_access_dst)) {
				dev_err(dev, "%s: lun_access allocation fail\n",
					__func__);
				rc = -ENOMEM;
				goto out;
			}

			*lun_access_dst = *lun_access_src;
			list_add(&lun_access_dst->list, &sidecar);
		}
	}

	if (unlikely(!ctxi_src->rht_out)) {
		dev_dbg(dev, "%s: Nothing to clone\n", __func__);
		goto out_success;
	}

	/* User specified permission on attach */
	perms = ctxi_dst->rht_perms;

	/*
	 * Copy over checked-out RHT (and their associated LXT) entries by
	 * hand, stopping after we've copied all outstanding entries and
	 * cleaning up if the clone fails.
	 *
	 * Note: This loop is equivalent to performing cxlflash_disk_open and
	 * cxlflash_vlun_resize. As such, LUN accounting needs to be taken into
	 * account by attaching after each successful RHT entry clone. In the
	 * event that a clone failure is experienced, the LUN detach is handled
	 * via the cleanup performed by _cxlflash_disk_release.
	 */
	for (i = 0; i < MAX_RHT_PER_CONTEXT; i++) {
		if (ctxi_src->rht_out == ctxi_dst->rht_out)
			break;
		if (ctxi_src->rht_start[i].nmask == 0)
			continue;

		/* Consume a destination RHT entry */
		ctxi_dst->rht_out++;
		ctxi_dst->rht_start[i].nmask = ctxi_src->rht_start[i].nmask;
		ctxi_dst->rht_start[i].fp =
		    SISL_RHT_FP_CLONE(ctxi_src->rht_start[i].fp, perms);
		ctxi_dst->rht_lun[i] = ctxi_src->rht_lun[i];

		rc = clone_lxt(afu, blka, ctxid_dst, i,
			       &ctxi_dst->rht_start[i],
			       &ctxi_src->rht_start[i]);
		if (rc) {
			marshal_clone_to_rele(clone, &release);
			for (j = 0; j < i; j++) {
				release.rsrc_handle = j;
				_cxlflash_disk_release(sdev, ctxi_dst,
						       &release);
			}

			/* Put back the one we failed on */
			rhte_checkin(ctxi_dst, &ctxi_dst->rht_start[i]);
			goto err;
		}

		cxlflash_lun_attach(gli, gli->mode, false);
	}

out_success:
	list_splice(&sidecar, &ctxi_dst->luns);

	/* fall through */
out:
	if (ctxi_src)
		put_context(ctxi_src);
	if (ctxi_dst)
		put_context(ctxi_dst);
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;

err:
	list_for_each_entry_safe(lun_access_src, lun_access_dst, &sidecar, list)
		kfree(lun_access_src);
	goto out;
}
