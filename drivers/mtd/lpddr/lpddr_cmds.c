/*
 * LPDDR flash memory device operations. This module provides read, write,
 * erase, lock/unlock support for LPDDR flash memories
 * (C) 2008 Korolev Alexey <akorolev@infradead.org>
 * (C) 2008 Vasiliy Leonenko <vasiliy.leonenko@gmail.com>
 * Many thanks to Roman Borisov for initial enabling
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 * TODO:
 * Implement VPP management
 * Implement XIP support
 * Implement OTP support
 */
#include <linux/mtd/pfow.h>
#include <linux/mtd/qinfo.h>
#include <linux/slab.h>

static int lpddr_read(struct mtd_info *mtd, loff_t adr, size_t len,
					size_t *retlen, u_char *buf);
static int lpddr_write_buffers(struct mtd_info *mtd, loff_t to,
				size_t len, size_t *retlen, const u_char *buf);
static int lpddr_writev(struct mtd_info *mtd, const struct kvec *vecs,
				unsigned long count, loff_t to, size_t *retlen);
static int lpddr_erase(struct mtd_info *mtd, struct erase_info *instr);
static int lpddr_lock(struct mtd_info *mtd, loff_t ofs, uint64_t len);
static int lpddr_unlock(struct mtd_info *mtd, loff_t ofs, uint64_t len);
static int lpddr_point(struct mtd_info *mtd, loff_t adr, size_t len,
			size_t *retlen, void **mtdbuf, resource_size_t *phys);
static void lpddr_unpoint(struct mtd_info *mtd, loff_t adr, size_t len);
static int get_chip(struct map_info *map, struct flchip *chip, int mode);
static int chip_ready(struct map_info *map, struct flchip *chip, int mode);
static void put_chip(struct map_info *map, struct flchip *chip);

struct mtd_info *lpddr_cmdset(struct map_info *map)
{
	struct lpddr_private *lpddr = map->fldrv_priv;
	struct flchip_shared *shared;
	struct flchip *chip;
	struct mtd_info *mtd;
	int numchips;
	int i, j;

	mtd = kzalloc(sizeof(*mtd), GFP_KERNEL);
	if (!mtd) {
		printk(KERN_ERR "Failed to allocate memory for MTD device\n");
		return NULL;
	}
	mtd->priv = map;
	mtd->type = MTD_NORFLASH;

	/* Fill in the default mtd operations */
	mtd->read = lpddr_read;
	mtd->type = MTD_NORFLASH;
	mtd->flags = MTD_CAP_NORFLASH;
	mtd->flags &= ~MTD_BIT_WRITEABLE;
	mtd->erase = lpddr_erase;
	mtd->write = lpddr_write_buffers;
	mtd->writev = lpddr_writev;
	mtd->read_oob = NULL;
	mtd->write_oob = NULL;
	mtd->sync = NULL;
	mtd->lock = lpddr_lock;
	mtd->unlock = lpddr_unlock;
	mtd->suspend = NULL;
	mtd->resume = NULL;
	if (map_is_linear(map)) {
		mtd->point = lpddr_point;
		mtd->unpoint = lpddr_unpoint;
	}
	mtd->block_isbad = NULL;
	mtd->block_markbad = NULL;
	mtd->size = 1 << lpddr->qinfo->DevSizeShift;
	mtd->erasesize = 1 << lpddr->qinfo->UniformBlockSizeShift;
	mtd->writesize = 1 << lpddr->qinfo->BufSizeShift;

	shared = kmalloc(sizeof(struct flchip_shared) * lpddr->numchips,
						GFP_KERNEL);
	if (!shared) {
		kfree(lpddr);
		kfree(mtd);
		return NULL;
	}

	chip = &lpddr->chips[0];
	numchips = lpddr->numchips / lpddr->qinfo->HWPartsNum;
	for (i = 0; i < numchips; i++) {
		shared[i].writing = shared[i].erasing = NULL;
		mutex_init(&shared[i].lock);
		for (j = 0; j < lpddr->qinfo->HWPartsNum; j++) {
			*chip = lpddr->chips[i];
			chip->start += j << lpddr->chipshift;
			chip->oldstate = chip->state = FL_READY;
			chip->priv = &shared[i];
			/* those should be reset too since
			   they create memory references. */
			init_waitqueue_head(&chip->wq);
			mutex_init(&chip->mutex);
			chip++;
		}
	}

	return mtd;
}
EXPORT_SYMBOL(lpddr_cmdset);

static int wait_for_ready(struct map_info *map, struct flchip *chip,
		unsigned int chip_op_time)
{
	unsigned int timeo, reset_timeo, sleep_time;
	unsigned int dsr;
	flstate_t chip_state = chip->state;
	int ret = 0;

	/* set our timeout to 8 times the expected delay */
	timeo = chip_op_time * 8;
	if (!timeo)
		timeo = 500000;
	reset_timeo = timeo;
	sleep_time = chip_op_time / 2;

	for (;;) {
		dsr = CMDVAL(map_read(map, map->pfow_base + PFOW_DSR));
		if (dsr & DSR_READY_STATUS)
			break;
		if (!timeo) {
			printk(KERN_ERR "%s: Flash timeout error state %d \n",
							map->name, chip_state);
			ret = -ETIME;
			break;
		}

		/* OK Still waiting. Drop the lock, wait a while and retry. */
		mutex_unlock(&chip->mutex);
		if (sleep_time >= 1000000/HZ) {
			/*
			 * Half of the normal delay still remaining
			 * can be performed with a sleeping delay instead
			 * of busy waiting.
			 */
			msleep(sleep_time/1000);
			timeo -= sleep_time;
			sleep_time = 1000000/HZ;
		} else {
			udelay(1);
			cond_resched();
			timeo--;
		}
		mutex_lock(&chip->mutex);

		while (chip->state != chip_state) {
			/* Someone's suspended the operation: sleep */
			DECLARE_WAITQUEUE(wait, current);
			set_current_state(TASK_UNINTERRUPTIBLE);
			add_wait_queue(&chip->wq, &wait);
			mutex_unlock(&chip->mutex);
			schedule();
			remove_wait_queue(&chip->wq, &wait);
			mutex_lock(&chip->mutex);
		}
		if (chip->erase_suspended || chip->write_suspended)  {
			/* Suspend has occurred while sleep: reset timeout */
			timeo = reset_timeo;
			chip->erase_suspended = chip->write_suspended = 0;
		}
	}
	/* check status for errors */
	if (dsr & DSR_ERR) {
		/* Clear DSR*/
		map_write(map, CMD(~(DSR_ERR)), map->pfow_base + PFOW_DSR);
		printk(KERN_WARNING"%s: Bad status on wait: 0x%x \n",
				map->name, dsr);
		print_drs_error(dsr);
		ret = -EIO;
	}
	chip->state = FL_READY;
	return ret;
}

static int get_chip(struct map_info *map, struct flchip *chip, int mode)
{
	int ret;
	DECLARE_WAITQUEUE(wait, current);

 retry:
	if (chip->priv && (mode == FL_WRITING || mode == FL_ERASING)
		&& chip->state != FL_SYNCING) {
		/*
		 * OK. We have possibility for contension on the write/erase
		 * operations which are global to the real chip and not per
		 * partition.  So let's fight it over in the partition which
		 * currently has authority on the operation.
		 *
		 * The rules are as follows:
		 *
		 * - any write operation must own shared->writing.
		 *
		 * - any erase operation must own _both_ shared->writing and
		 *   shared->erasing.
		 *
		 * - contension arbitration is handled in the owner's context.
		 *
		 * The 'shared' struct can be read and/or written only when
		 * its lock is taken.
		 */
		struct flchip_shared *shared = chip->priv;
		struct flchip *contender;
		mutex_lock(&shared->lock);
		contender = shared->writing;
		if (contender && contender != chip) {
			/*
			 * The engine to perform desired operation on this
			 * partition is already in use by someone else.
			 * Let's fight over it in the context of the chip
			 * currently using it.  If it is possible to suspend,
			 * that other partition will do just that, otherwise
			 * it'll happily send us to sleep.  In any case, when
			 * get_chip returns success we're clear to go ahead.
			 */
			ret = mutex_trylock(&contender->mutex);
			mutex_unlock(&shared->lock);
			if (!ret)
				goto retry;
			mutex_unlock(&chip->mutex);
			ret = chip_ready(map, contender, mode);
			mutex_lock(&chip->mutex);

			if (ret == -EAGAIN) {
				mutex_unlock(&contender->mutex);
				goto retry;
			}
			if (ret) {
				mutex_unlock(&contender->mutex);
				return ret;
			}
			mutex_lock(&shared->lock);

			/* We should not own chip if it is already in FL_SYNCING
			 * state. Put contender and retry. */
			if (chip->state == FL_SYNCING) {
				put_chip(map, contender);
				mutex_unlock(&contender->mutex);
				goto retry;
			}
			mutex_unlock(&contender->mutex);
		}

		/* Check if we have suspended erase on this chip.
		   Must sleep in such a case. */
		if (mode == FL_ERASING && shared->erasing
		    && shared->erasing->oldstate == FL_ERASING) {
			mutex_unlock(&shared->lock);
			set_current_state(TASK_UNINTERRUPTIBLE);
			add_wait_queue(&chip->wq, &wait);
			mutex_unlock(&chip->mutex);
			schedule();
			remove_wait_queue(&chip->wq, &wait);
			mutex_lock(&chip->mutex);
			goto retry;
		}

		/* We now own it */
		shared->writing = chip;
		if (mode == FL_ERASING)
			shared->erasing = chip;
		mutex_unlock(&shared->lock);
	}

	ret = chip_ready(map, chip, mode);
	if (ret == -EAGAIN)
		goto retry;

	return ret;
}

static int chip_ready(struct map_info *map, struct flchip *chip, int mode)
{
	struct lpddr_private *lpddr = map->fldrv_priv;
	int ret = 0;
	DECLARE_WAITQUEUE(wait, current);

	/* Prevent setting state FL_SYNCING for chip in suspended state. */
	if (FL_SYNCING == mode && FL_READY != chip->oldstate)
		goto sleep;

	switch (chip->state) {
	case FL_READY:
	case FL_JEDEC_QUERY:
		return 0;

	case FL_ERASING:
		if (!lpddr->qinfo->SuspEraseSupp ||
			!(mode == FL_READY || mode == FL_POINT))
			goto sleep;

		map_write(map, CMD(LPDDR_SUSPEND),
			map->pfow_base + PFOW_PROGRAM_ERASE_SUSPEND);
		chip->oldstate = FL_ERASING;
		chip->state = FL_ERASE_SUSPENDING;
		ret = wait_for_ready(map, chip, 0);
		if (ret) {
			/* Oops. something got wrong. */
			/* Resume and pretend we weren't here.  */
			map_write(map, CMD(LPDDR_RESUME),
				map->pfow_base + PFOW_COMMAND_CODE);
			map_write(map, CMD(LPDDR_START_EXECUTION),
				map->pfow_base + PFOW_COMMAND_EXECUTE);
			chip->state = FL_ERASING;
			chip->oldstate = FL_READY;
			printk(KERN_ERR "%s: suspend operation failed."
					"State may be wrong \n", map->name);
			return -EIO;
		}
		chip->erase_suspended = 1;
		chip->state = FL_READY;
		return 0;
		/* Erase suspend */
	case FL_POINT:
		/* Only if there's no operation suspended... */
		if (mode == FL_READY && chip->oldstate == FL_READY)
			return 0;

	default:
sleep:
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&chip->wq, &wait);
		mutex_unlock(&chip->mutex);
		schedule();
		remove_wait_queue(&chip->wq, &wait);
		mutex_lock(&chip->mutex);
		return -EAGAIN;
	}
}

static void put_chip(struct map_info *map, struct flchip *chip)
{
	if (chip->priv) {
		struct flchip_shared *shared = chip->priv;
		mutex_lock(&shared->lock);
		if (shared->writing == chip && chip->oldstate == FL_READY) {
			/* We own the ability to write, but we're done */
			shared->writing = shared->erasing;
			if (shared->writing && shared->writing != chip) {
				/* give back the ownership */
				struct flchip *loaner = shared->writing;
				mutex_lock(&loaner->mutex);
				mutex_unlock(&shared->lock);
				mutex_unlock(&chip->mutex);
				put_chip(map, loaner);
				mutex_lock(&chip->mutex);
				mutex_unlock(&loaner->mutex);
				wake_up(&chip->wq);
				return;
			}
			shared->erasing = NULL;
			shared->writing = NULL;
		} else if (shared->erasing == chip && shared->writing != chip) {
			/*
			 * We own the ability to erase without the ability
			 * to write, which means the erase was suspended
			 * and some other partition is currently writing.
			 * Don't let the switch below mess things up since
			 * we don't have ownership to resume anything.
			 */
			mutex_unlock(&shared->lock);
			wake_up(&chip->wq);
			return;
		}
		mutex_unlock(&shared->lock);
	}

	switch (chip->oldstate) {
	case FL_ERASING:
		chip->state = chip->oldstate;
		map_write(map, CMD(LPDDR_RESUME),
				map->pfow_base + PFOW_COMMAND_CODE);
		map_write(map, CMD(LPDDR_START_EXECUTION),
				map->pfow_base + PFOW_COMMAND_EXECUTE);
		chip->oldstate = FL_READY;
		chip->state = FL_ERASING;
		break;
	case FL_READY:
		break;
	default:
		printk(KERN_ERR "%s: put_chip() called with oldstate %d!\n",
				map->name, chip->oldstate);
	}
	wake_up(&chip->wq);
}

int do_write_buffer(struct map_info *map, struct flchip *chip,
			unsigned long adr, const struct kvec **pvec,
			unsigned long *pvec_seek, int len)
{
	struct lpddr_private *lpddr = map->fldrv_priv;
	map_word datum;
	int ret, wbufsize, word_gap, words;
	const struct kvec *vec;
	unsigned long vec_seek;
	unsigned long prog_buf_ofs;

	wbufsize = 1 << lpddr->qinfo->BufSizeShift;

	mutex_lock(&chip->mutex);
	ret = get_chip(map, chip, FL_WRITING);
	if (ret) {
		mutex_unlock(&chip->mutex);
		return ret;
	}
	/* Figure out the number of words to write */
	word_gap = (-adr & (map_bankwidth(map)-1));
	words = (len - word_gap + map_bankwidth(map) - 1) / map_bankwidth(map);
	if (!word_gap) {
		words--;
	} else {
		word_gap = map_bankwidth(map) - word_gap;
		adr -= word_gap;
		datum = map_word_ff(map);
	}
	/* Write data */
	/* Get the program buffer offset from PFOW register data first*/
	prog_buf_ofs = map->pfow_base + CMDVAL(map_read(map,
				map->pfow_base + PFOW_PROGRAM_BUFFER_OFFSET));
	vec = *pvec;
	vec_seek = *pvec_seek;
	do {
		int n = map_bankwidth(map) - word_gap;

		if (n > vec->iov_len - vec_seek)
			n = vec->iov_len - vec_seek;
		if (n > len)
			n = len;

		if (!word_gap && (len < map_bankwidth(map)))
			datum = map_word_ff(map);

		datum = map_word_load_partial(map, datum,
				vec->iov_base + vec_seek, word_gap, n);

		len -= n;
		word_gap += n;
		if (!len || word_gap == map_bankwidth(map)) {
			map_write(map, datum, prog_buf_ofs);
			prog_buf_ofs += map_bankwidth(map);
			word_gap = 0;
		}

		vec_seek += n;
		if (vec_seek == vec->iov_len) {
			vec++;
			vec_seek = 0;
		}
	} while (len);
	*pvec = vec;
	*pvec_seek = vec_seek;

	/* GO GO GO */
	send_pfow_command(map, LPDDR_BUFF_PROGRAM, adr, wbufsize, NULL);
	chip->state = FL_WRITING;
	ret = wait_for_ready(map, chip, (1<<lpddr->qinfo->ProgBufferTime));
	if (ret)	{
		printk(KERN_WARNING"%s Buffer program error: %d at %lx; \n",
			map->name, ret, adr);
		goto out;
	}

 out:	put_chip(map, chip);
	mutex_unlock(&chip->mutex);
	return ret;
}

int do_erase_oneblock(struct mtd_info *mtd, loff_t adr)
{
	struct map_info *map = mtd->priv;
	struct lpddr_private *lpddr = map->fldrv_priv;
	int chipnum = adr >> lpddr->chipshift;
	struct flchip *chip = &lpddr->chips[chipnum];
	int ret;

	mutex_lock(&chip->mutex);
	ret = get_chip(map, chip, FL_ERASING);
	if (ret) {
		mutex_unlock(&chip->mutex);
		return ret;
	}
	send_pfow_command(map, LPDDR_BLOCK_ERASE, adr, 0, NULL);
	chip->state = FL_ERASING;
	ret = wait_for_ready(map, chip, (1<<lpddr->qinfo->BlockEraseTime)*1000);
	if (ret) {
		printk(KERN_WARNING"%s Erase block error %d at : %llx\n",
			map->name, ret, adr);
		goto out;
	}
 out:	put_chip(map, chip);
	mutex_unlock(&chip->mutex);
	return ret;
}

static int lpddr_read(struct mtd_info *mtd, loff_t adr, size_t len,
			size_t *retlen, u_char *buf)
{
	struct map_info *map = mtd->priv;
	struct lpddr_private *lpddr = map->fldrv_priv;
	int chipnum = adr >> lpddr->chipshift;
	struct flchip *chip = &lpddr->chips[chipnum];
	int ret = 0;

	mutex_lock(&chip->mutex);
	ret = get_chip(map, chip, FL_READY);
	if (ret) {
		mutex_unlock(&chip->mutex);
		return ret;
	}

	map_copy_from(map, buf, adr, len);
	*retlen = len;

	put_chip(map, chip);
	mutex_unlock(&chip->mutex);
	return ret;
}

static int lpddr_point(struct mtd_info *mtd, loff_t adr, size_t len,
			size_t *retlen, void **mtdbuf, resource_size_t *phys)
{
	struct map_info *map = mtd->priv;
	struct lpddr_private *lpddr = map->fldrv_priv;
	int chipnum = adr >> lpddr->chipshift;
	unsigned long ofs, last_end = 0;
	struct flchip *chip = &lpddr->chips[chipnum];
	int ret = 0;

	if (!map->virt || (adr + len > mtd->size))
		return -EINVAL;

	/* ofs: offset within the first chip that the first read should start */
	ofs = adr - (chipnum << lpddr->chipshift);

	*mtdbuf = (void *)map->virt + chip->start + ofs;
	*retlen = 0;

	while (len) {
		unsigned long thislen;

		if (chipnum >= lpddr->numchips)
			break;

		/* We cannot point across chips that are virtually disjoint */
		if (!last_end)
			last_end = chip->start;
		else if (chip->start != last_end)
			break;

		if ((len + ofs - 1) >> lpddr->chipshift)
			thislen = (1<<lpddr->chipshift) - ofs;
		else
			thislen = len;
		/* get the chip */
		mutex_lock(&chip->mutex);
		ret = get_chip(map, chip, FL_POINT);
		mutex_unlock(&chip->mutex);
		if (ret)
			break;

		chip->state = FL_POINT;
		chip->ref_point_counter++;
		*retlen += thislen;
		len -= thislen;

		ofs = 0;
		last_end += 1 << lpddr->chipshift;
		chipnum++;
		chip = &lpddr->chips[chipnum];
	}
	return 0;
}

static void lpddr_unpoint (struct mtd_info *mtd, loff_t adr, size_t len)
{
	struct map_info *map = mtd->priv;
	struct lpddr_private *lpddr = map->fldrv_priv;
	int chipnum = adr >> lpddr->chipshift;
	unsigned long ofs;

	/* ofs: offset within the first chip that the first read should start */
	ofs = adr - (chipnum << lpddr->chipshift);

	while (len) {
		unsigned long thislen;
		struct flchip *chip;

		chip = &lpddr->chips[chipnum];
		if (chipnum >= lpddr->numchips)
			break;

		if ((len + ofs - 1) >> lpddr->chipshift)
			thislen = (1<<lpddr->chipshift) - ofs;
		else
			thislen = len;

		mutex_lock(&chip->mutex);
		if (chip->state == FL_POINT) {
			chip->ref_point_counter--;
			if (chip->ref_point_counter == 0)
				chip->state = FL_READY;
		} else
			printk(KERN_WARNING "%s: Warning: unpoint called on non"
					"pointed region\n", map->name);

		put_chip(map, chip);
		mutex_unlock(&chip->mutex);

		len -= thislen;
		ofs = 0;
		chipnum++;
	}
}

static int lpddr_write_buffers(struct mtd_info *mtd, loff_t to, size_t len,
				size_t *retlen, const u_char *buf)
{
	struct kvec vec;

	vec.iov_base = (void *) buf;
	vec.iov_len = len;

	return lpddr_writev(mtd, &vec, 1, to, retlen);
}


static int lpddr_writev(struct mtd_info *mtd, const struct kvec *vecs,
				unsigned long count, loff_t to, size_t *retlen)
{
	struct map_info *map = mtd->priv;
	struct lpddr_private *lpddr = map->fldrv_priv;
	int ret = 0;
	int chipnum;
	unsigned long ofs, vec_seek, i;
	int wbufsize = 1 << lpddr->qinfo->BufSizeShift;

	size_t len = 0;

	for (i = 0; i < count; i++)
		len += vecs[i].iov_len;

	*retlen = 0;
	if (!len)
		return 0;

	chipnum = to >> lpddr->chipshift;

	ofs = to;
	vec_seek = 0;

	do {
		/* We must not cross write block boundaries */
		int size = wbufsize - (ofs & (wbufsize-1));

		if (size > len)
			size = len;

		ret = do_write_buffer(map, &lpddr->chips[chipnum],
					  ofs, &vecs, &vec_seek, size);
		if (ret)
			return ret;

		ofs += size;
		(*retlen) += size;
		len -= size;

		/* Be nice and reschedule with the chip in a usable
		 * state for other processes */
		cond_resched();

	} while (len);

	return 0;
}

static int lpddr_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	unsigned long ofs, len;
	int ret;
	struct map_info *map = mtd->priv;
	struct lpddr_private *lpddr = map->fldrv_priv;
	int size = 1 << lpddr->qinfo->UniformBlockSizeShift;

	ofs = instr->addr;
	len = instr->len;

	if (ofs > mtd->size || (len + ofs) > mtd->size)
		return -EINVAL;

	while (len > 0) {
		ret = do_erase_oneblock(mtd, ofs);
		if (ret)
			return ret;
		ofs += size;
		len -= size;
	}
	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);

	return 0;
}

#define DO_XXLOCK_LOCK		1
#define DO_XXLOCK_UNLOCK	2
int do_xxlock(struct mtd_info *mtd, loff_t adr, uint32_t len, int thunk)
{
	int ret = 0;
	struct map_info *map = mtd->priv;
	struct lpddr_private *lpddr = map->fldrv_priv;
	int chipnum = adr >> lpddr->chipshift;
	struct flchip *chip = &lpddr->chips[chipnum];

	mutex_lock(&chip->mutex);
	ret = get_chip(map, chip, FL_LOCKING);
	if (ret) {
		mutex_unlock(&chip->mutex);
		return ret;
	}

	if (thunk == DO_XXLOCK_LOCK) {
		send_pfow_command(map, LPDDR_LOCK_BLOCK, adr, adr + len, NULL);
		chip->state = FL_LOCKING;
	} else if (thunk == DO_XXLOCK_UNLOCK) {
		send_pfow_command(map, LPDDR_UNLOCK_BLOCK, adr, adr + len, NULL);
		chip->state = FL_UNLOCKING;
	} else
		BUG();

	ret = wait_for_ready(map, chip, 1);
	if (ret)	{
		printk(KERN_ERR "%s: block unlock error status %d \n",
				map->name, ret);
		goto out;
	}
out:	put_chip(map, chip);
	mutex_unlock(&chip->mutex);
	return ret;
}

static int lpddr_lock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	return do_xxlock(mtd, ofs, len, DO_XXLOCK_LOCK);
}

static int lpddr_unlock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	return do_xxlock(mtd, ofs, len, DO_XXLOCK_UNLOCK);
}

int word_program(struct map_info *map, loff_t adr, uint32_t curval)
{
    int ret;
	struct lpddr_private *lpddr = map->fldrv_priv;
	int chipnum = adr >> lpddr->chipshift;
	struct flchip *chip = &lpddr->chips[chipnum];

	mutex_lock(&chip->mutex);
	ret = get_chip(map, chip, FL_WRITING);
	if (ret) {
		mutex_unlock(&chip->mutex);
		return ret;
	}

	send_pfow_command(map, LPDDR_WORD_PROGRAM, adr, 0x00, (map_word *)&curval);

	ret = wait_for_ready(map, chip, (1<<lpddr->qinfo->SingleWordProgTime));
	if (ret)	{
		printk(KERN_WARNING"%s word_program error at: %llx; val: %x\n",
			map->name, adr, curval);
		goto out;
	}

out:	put_chip(map, chip);
	mutex_unlock(&chip->mutex);
	return ret;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexey Korolev <akorolev@infradead.org>");
MODULE_DESCRIPTION("MTD driver for LPDDR flash chips");
