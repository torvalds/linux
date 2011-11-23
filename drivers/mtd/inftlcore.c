/*
 * inftlcore.c -- Linux driver for Inverse Flash Translation Layer (INFTL)
 *
 * Copyright © 2002, Greg Ungerer (gerg@snapgear.com)
 *
 * Based heavily on the nftlcore.c code which is:
 * Copyright © 1999 Machine Vision Holdings, Inc.
 * Copyright © 1999 David Woodhouse <dwmw2@infradead.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/hdreg.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nftl.h>
#include <linux/mtd/inftl.h>
#include <linux/mtd/nand.h>
#include <asm/uaccess.h>
#include <asm/errno.h>
#include <asm/io.h>

/*
 * Maximum number of loops while examining next block, to have a
 * chance to detect consistency problems (they should never happen
 * because of the checks done in the mounting.
 */
#define MAX_LOOPS 10000

static void inftl_add_mtd(struct mtd_blktrans_ops *tr, struct mtd_info *mtd)
{
	struct INFTLrecord *inftl;
	unsigned long temp;

	if (mtd->type != MTD_NANDFLASH || mtd->size > UINT_MAX)
		return;
	/* OK, this is moderately ugly.  But probably safe.  Alternatives? */
	if (memcmp(mtd->name, "DiskOnChip", 10))
		return;

	if (!mtd->block_isbad) {
		printk(KERN_ERR
"INFTL no longer supports the old DiskOnChip drivers loaded via docprobe.\n"
"Please use the new diskonchip driver under the NAND subsystem.\n");
		return;
	}

	pr_debug("INFTL: add_mtd for %s\n", mtd->name);

	inftl = kzalloc(sizeof(*inftl), GFP_KERNEL);

	if (!inftl)
		return;

	inftl->mbd.mtd = mtd;
	inftl->mbd.devnum = -1;

	inftl->mbd.tr = tr;

	if (INFTL_mount(inftl) < 0) {
		printk(KERN_WARNING "INFTL: could not mount device\n");
		kfree(inftl);
		return;
	}

	/* OK, it's a new one. Set up all the data structures. */

	/* Calculate geometry */
	inftl->cylinders = 1024;
	inftl->heads = 16;

	temp = inftl->cylinders * inftl->heads;
	inftl->sectors = inftl->mbd.size / temp;
	if (inftl->mbd.size % temp) {
		inftl->sectors++;
		temp = inftl->cylinders * inftl->sectors;
		inftl->heads = inftl->mbd.size / temp;

		if (inftl->mbd.size % temp) {
			inftl->heads++;
			temp = inftl->heads * inftl->sectors;
			inftl->cylinders = inftl->mbd.size / temp;
		}
	}

	if (inftl->mbd.size != inftl->heads * inftl->cylinders * inftl->sectors) {
		/*
		  Oh no we don't have
		   mbd.size == heads * cylinders * sectors
		*/
		printk(KERN_WARNING "INFTL: cannot calculate a geometry to "
		       "match size of 0x%lx.\n", inftl->mbd.size);
		printk(KERN_WARNING "INFTL: using C:%d H:%d S:%d "
			"(== 0x%lx sects)\n",
			inftl->cylinders, inftl->heads , inftl->sectors,
			(long)inftl->cylinders * (long)inftl->heads *
			(long)inftl->sectors );
	}

	if (add_mtd_blktrans_dev(&inftl->mbd)) {
		kfree(inftl->PUtable);
		kfree(inftl->VUtable);
		kfree(inftl);
		return;
	}
#ifdef PSYCHO_DEBUG
	printk(KERN_INFO "INFTL: Found new inftl%c\n", inftl->mbd.devnum + 'a');
#endif
	return;
}

static void inftl_remove_dev(struct mtd_blktrans_dev *dev)
{
	struct INFTLrecord *inftl = (void *)dev;

	pr_debug("INFTL: remove_dev (i=%d)\n", dev->devnum);

	del_mtd_blktrans_dev(dev);

	kfree(inftl->PUtable);
	kfree(inftl->VUtable);
}

/*
 * Actual INFTL access routines.
 */

/*
 * Read oob data from flash
 */
int inftl_read_oob(struct mtd_info *mtd, loff_t offs, size_t len,
		   size_t *retlen, uint8_t *buf)
{
	struct mtd_oob_ops ops;
	int res;

	ops.mode = MTD_OPS_PLACE_OOB;
	ops.ooboffs = offs & (mtd->writesize - 1);
	ops.ooblen = len;
	ops.oobbuf = buf;
	ops.datbuf = NULL;

	res = mtd->read_oob(mtd, offs & ~(mtd->writesize - 1), &ops);
	*retlen = ops.oobretlen;
	return res;
}

/*
 * Write oob data to flash
 */
int inftl_write_oob(struct mtd_info *mtd, loff_t offs, size_t len,
		    size_t *retlen, uint8_t *buf)
{
	struct mtd_oob_ops ops;
	int res;

	ops.mode = MTD_OPS_PLACE_OOB;
	ops.ooboffs = offs & (mtd->writesize - 1);
	ops.ooblen = len;
	ops.oobbuf = buf;
	ops.datbuf = NULL;

	res = mtd->write_oob(mtd, offs & ~(mtd->writesize - 1), &ops);
	*retlen = ops.oobretlen;
	return res;
}

/*
 * Write data and oob to flash
 */
static int inftl_write(struct mtd_info *mtd, loff_t offs, size_t len,
		       size_t *retlen, uint8_t *buf, uint8_t *oob)
{
	struct mtd_oob_ops ops;
	int res;

	ops.mode = MTD_OPS_PLACE_OOB;
	ops.ooboffs = offs;
	ops.ooblen = mtd->oobsize;
	ops.oobbuf = oob;
	ops.datbuf = buf;
	ops.len = len;

	res = mtd->write_oob(mtd, offs & ~(mtd->writesize - 1), &ops);
	*retlen = ops.retlen;
	return res;
}

/*
 * INFTL_findfreeblock: Find a free Erase Unit on the INFTL partition.
 *	This function is used when the give Virtual Unit Chain.
 */
static u16 INFTL_findfreeblock(struct INFTLrecord *inftl, int desperate)
{
	u16 pot = inftl->LastFreeEUN;
	int silly = inftl->nb_blocks;

	pr_debug("INFTL: INFTL_findfreeblock(inftl=%p,desperate=%d)\n",
			inftl, desperate);

	/*
	 * Normally, we force a fold to happen before we run out of free
	 * blocks completely.
	 */
	if (!desperate && inftl->numfreeEUNs < 2) {
		pr_debug("INFTL: there are too few free EUNs (%d)\n",
				inftl->numfreeEUNs);
		return BLOCK_NIL;
	}

	/* Scan for a free block */
	do {
		if (inftl->PUtable[pot] == BLOCK_FREE) {
			inftl->LastFreeEUN = pot;
			return pot;
		}

		if (++pot > inftl->lastEUN)
			pot = 0;

		if (!silly--) {
			printk(KERN_WARNING "INFTL: no free blocks found!  "
				"EUN range = %d - %d\n", 0, inftl->LastFreeEUN);
			return BLOCK_NIL;
		}
	} while (pot != inftl->LastFreeEUN);

	return BLOCK_NIL;
}

static u16 INFTL_foldchain(struct INFTLrecord *inftl, unsigned thisVUC, unsigned pendingblock)
{
	u16 BlockMap[MAX_SECTORS_PER_UNIT];
	unsigned char BlockDeleted[MAX_SECTORS_PER_UNIT];
	unsigned int thisEUN, prevEUN, status;
	struct mtd_info *mtd = inftl->mbd.mtd;
	int block, silly;
	unsigned int targetEUN;
	struct inftl_oob oob;
	size_t retlen;

	pr_debug("INFTL: INFTL_foldchain(inftl=%p,thisVUC=%d,pending=%d)\n",
			inftl, thisVUC, pendingblock);

	memset(BlockMap, 0xff, sizeof(BlockMap));
	memset(BlockDeleted, 0, sizeof(BlockDeleted));

	thisEUN = targetEUN = inftl->VUtable[thisVUC];

	if (thisEUN == BLOCK_NIL) {
		printk(KERN_WARNING "INFTL: trying to fold non-existent "
		       "Virtual Unit Chain %d!\n", thisVUC);
		return BLOCK_NIL;
	}

	/*
	 * Scan to find the Erase Unit which holds the actual data for each
	 * 512-byte block within the Chain.
	 */
	silly = MAX_LOOPS;
	while (thisEUN < inftl->nb_blocks) {
		for (block = 0; block < inftl->EraseSize/SECTORSIZE; block ++) {
			if ((BlockMap[block] != BLOCK_NIL) ||
			    BlockDeleted[block])
				continue;

			if (inftl_read_oob(mtd, (thisEUN * inftl->EraseSize)
					   + (block * SECTORSIZE), 16, &retlen,
					   (char *)&oob) < 0)
				status = SECTOR_IGNORE;
			else
				status = oob.b.Status | oob.b.Status1;

			switch(status) {
			case SECTOR_FREE:
			case SECTOR_IGNORE:
				break;
			case SECTOR_USED:
				BlockMap[block] = thisEUN;
				continue;
			case SECTOR_DELETED:
				BlockDeleted[block] = 1;
				continue;
			default:
				printk(KERN_WARNING "INFTL: unknown status "
					"for block %d in EUN %d: %x\n",
					block, thisEUN, status);
				break;
			}
		}

		if (!silly--) {
			printk(KERN_WARNING "INFTL: infinite loop in Virtual "
				"Unit Chain 0x%x\n", thisVUC);
			return BLOCK_NIL;
		}

		thisEUN = inftl->PUtable[thisEUN];
	}

	/*
	 * OK. We now know the location of every block in the Virtual Unit
	 * Chain, and the Erase Unit into which we are supposed to be copying.
	 * Go for it.
	 */
	pr_debug("INFTL: folding chain %d into unit %d\n", thisVUC, targetEUN);

	for (block = 0; block < inftl->EraseSize/SECTORSIZE ; block++) {
		unsigned char movebuf[SECTORSIZE];
		int ret;

		/*
		 * If it's in the target EUN already, or if it's pending write,
		 * do nothing.
		 */
		if (BlockMap[block] == targetEUN || (pendingblock ==
		    (thisVUC * (inftl->EraseSize / SECTORSIZE) + block))) {
			continue;
		}

		/*
		 * Copy only in non free block (free blocks can only
                 * happen in case of media errors or deleted blocks).
		 */
		if (BlockMap[block] == BLOCK_NIL)
			continue;

		ret = mtd->read(mtd, (inftl->EraseSize * BlockMap[block]) +
				(block * SECTORSIZE), SECTORSIZE, &retlen,
				movebuf);
		if (ret < 0 && !mtd_is_bitflip(ret)) {
			ret = mtd->read(mtd,
					(inftl->EraseSize * BlockMap[block]) +
					(block * SECTORSIZE), SECTORSIZE,
					&retlen, movebuf);
			if (ret != -EIO)
				pr_debug("INFTL: error went away on retry?\n");
		}
		memset(&oob, 0xff, sizeof(struct inftl_oob));
		oob.b.Status = oob.b.Status1 = SECTOR_USED;

		inftl_write(inftl->mbd.mtd, (inftl->EraseSize * targetEUN) +
			    (block * SECTORSIZE), SECTORSIZE, &retlen,
			    movebuf, (char *)&oob);
	}

	/*
	 * Newest unit in chain now contains data from _all_ older units.
	 * So go through and erase each unit in chain, oldest first. (This
	 * is important, by doing oldest first if we crash/reboot then it
	 * it is relatively simple to clean up the mess).
	 */
	pr_debug("INFTL: want to erase virtual chain %d\n", thisVUC);

	for (;;) {
		/* Find oldest unit in chain. */
		thisEUN = inftl->VUtable[thisVUC];
		prevEUN = BLOCK_NIL;
		while (inftl->PUtable[thisEUN] != BLOCK_NIL) {
			prevEUN = thisEUN;
			thisEUN = inftl->PUtable[thisEUN];
		}

		/* Check if we are all done */
		if (thisEUN == targetEUN)
			break;

		/* Unlink the last block from the chain. */
		inftl->PUtable[prevEUN] = BLOCK_NIL;

		/* Now try to erase it. */
		if (INFTL_formatblock(inftl, thisEUN) < 0) {
			/*
			 * Could not erase : mark block as reserved.
			 */
			inftl->PUtable[thisEUN] = BLOCK_RESERVED;
		} else {
			/* Correctly erased : mark it as free */
			inftl->PUtable[thisEUN] = BLOCK_FREE;
			inftl->numfreeEUNs++;
		}
	}

	return targetEUN;
}

static u16 INFTL_makefreeblock(struct INFTLrecord *inftl, unsigned pendingblock)
{
	/*
	 * This is the part that needs some cleverness applied.
	 * For now, I'm doing the minimum applicable to actually
	 * get the thing to work.
	 * Wear-levelling and other clever stuff needs to be implemented
	 * and we also need to do some assessment of the results when
	 * the system loses power half-way through the routine.
	 */
	u16 LongestChain = 0;
	u16 ChainLength = 0, thislen;
	u16 chain, EUN;

	pr_debug("INFTL: INFTL_makefreeblock(inftl=%p,"
		"pending=%d)\n", inftl, pendingblock);

	for (chain = 0; chain < inftl->nb_blocks; chain++) {
		EUN = inftl->VUtable[chain];
		thislen = 0;

		while (EUN <= inftl->lastEUN) {
			thislen++;
			EUN = inftl->PUtable[EUN];
			if (thislen > 0xff00) {
				printk(KERN_WARNING "INFTL: endless loop in "
					"Virtual Chain %d: Unit %x\n",
					chain, EUN);
				/*
				 * Actually, don't return failure.
				 * Just ignore this chain and get on with it.
				 */
				thislen = 0;
				break;
			}
		}

		if (thislen > ChainLength) {
			ChainLength = thislen;
			LongestChain = chain;
		}
	}

	if (ChainLength < 2) {
		printk(KERN_WARNING "INFTL: no Virtual Unit Chains available "
			"for folding. Failing request\n");
		return BLOCK_NIL;
	}

	return INFTL_foldchain(inftl, LongestChain, pendingblock);
}

static int nrbits(unsigned int val, int bitcount)
{
	int i, total = 0;

	for (i = 0; (i < bitcount); i++)
		total += (((0x1 << i) & val) ? 1 : 0);
	return total;
}

/*
 * INFTL_findwriteunit: Return the unit number into which we can write
 *                      for this block. Make it available if it isn't already.
 */
static inline u16 INFTL_findwriteunit(struct INFTLrecord *inftl, unsigned block)
{
	unsigned int thisVUC = block / (inftl->EraseSize / SECTORSIZE);
	unsigned int thisEUN, writeEUN, prev_block, status;
	unsigned long blockofs = (block * SECTORSIZE) & (inftl->EraseSize -1);
	struct mtd_info *mtd = inftl->mbd.mtd;
	struct inftl_oob oob;
	struct inftl_bci bci;
	unsigned char anac, nacs, parity;
	size_t retlen;
	int silly, silly2 = 3;

	pr_debug("INFTL: INFTL_findwriteunit(inftl=%p,block=%d)\n",
			inftl, block);

	do {
		/*
		 * Scan the media to find a unit in the VUC which has
		 * a free space for the block in question.
		 */
		writeEUN = BLOCK_NIL;
		thisEUN = inftl->VUtable[thisVUC];
		silly = MAX_LOOPS;

		while (thisEUN <= inftl->lastEUN) {
			inftl_read_oob(mtd, (thisEUN * inftl->EraseSize) +
				       blockofs, 8, &retlen, (char *)&bci);

			status = bci.Status | bci.Status1;
			pr_debug("INFTL: status of block %d in EUN %d is %x\n",
					block , writeEUN, status);

			switch(status) {
			case SECTOR_FREE:
				writeEUN = thisEUN;
				break;
			case SECTOR_DELETED:
			case SECTOR_USED:
				/* Can't go any further */
				goto hitused;
			case SECTOR_IGNORE:
				break;
			default:
				/*
				 * Invalid block. Don't use it any more.
				 * Must implement.
				 */
				break;
			}

			if (!silly--) {
				printk(KERN_WARNING "INFTL: infinite loop in "
					"Virtual Unit Chain 0x%x\n", thisVUC);
				return BLOCK_NIL;
			}

			/* Skip to next block in chain */
			thisEUN = inftl->PUtable[thisEUN];
		}

hitused:
		if (writeEUN != BLOCK_NIL)
			return writeEUN;


		/*
		 * OK. We didn't find one in the existing chain, or there
		 * is no existing chain. Allocate a new one.
		 */
		writeEUN = INFTL_findfreeblock(inftl, 0);

		if (writeEUN == BLOCK_NIL) {
			/*
			 * That didn't work - there were no free blocks just
			 * waiting to be picked up. We're going to have to fold
			 * a chain to make room.
			 */
			thisEUN = INFTL_makefreeblock(inftl, block);

			/*
			 * Hopefully we free something, lets try again.
			 * This time we are desperate...
			 */
			pr_debug("INFTL: using desperate==1 to find free EUN "
					"to accommodate write to VUC %d\n",
					thisVUC);
			writeEUN = INFTL_findfreeblock(inftl, 1);
			if (writeEUN == BLOCK_NIL) {
				/*
				 * Ouch. This should never happen - we should
				 * always be able to make some room somehow.
				 * If we get here, we've allocated more storage
				 * space than actual media, or our makefreeblock
				 * routine is missing something.
				 */
				printk(KERN_WARNING "INFTL: cannot make free "
					"space.\n");
#ifdef DEBUG
				INFTL_dumptables(inftl);
				INFTL_dumpVUchains(inftl);
#endif
				return BLOCK_NIL;
			}
		}

		/*
		 * Insert new block into virtual chain. Firstly update the
		 * block headers in flash...
		 */
		anac = 0;
		nacs = 0;
		thisEUN = inftl->VUtable[thisVUC];
		if (thisEUN != BLOCK_NIL) {
			inftl_read_oob(mtd, thisEUN * inftl->EraseSize
				       + 8, 8, &retlen, (char *)&oob.u);
			anac = oob.u.a.ANAC + 1;
			nacs = oob.u.a.NACs + 1;
		}

		prev_block = inftl->VUtable[thisVUC];
		if (prev_block < inftl->nb_blocks)
			prev_block -= inftl->firstEUN;

		parity = (nrbits(thisVUC, 16) & 0x1) ? 0x1 : 0;
		parity |= (nrbits(prev_block, 16) & 0x1) ? 0x2 : 0;
		parity |= (nrbits(anac, 8) & 0x1) ? 0x4 : 0;
		parity |= (nrbits(nacs, 8) & 0x1) ? 0x8 : 0;

		oob.u.a.virtualUnitNo = cpu_to_le16(thisVUC);
		oob.u.a.prevUnitNo = cpu_to_le16(prev_block);
		oob.u.a.ANAC = anac;
		oob.u.a.NACs = nacs;
		oob.u.a.parityPerField = parity;
		oob.u.a.discarded = 0xaa;

		inftl_write_oob(mtd, writeEUN * inftl->EraseSize + 8, 8,
				&retlen, (char *)&oob.u);

		/* Also back up header... */
		oob.u.b.virtualUnitNo = cpu_to_le16(thisVUC);
		oob.u.b.prevUnitNo = cpu_to_le16(prev_block);
		oob.u.b.ANAC = anac;
		oob.u.b.NACs = nacs;
		oob.u.b.parityPerField = parity;
		oob.u.b.discarded = 0xaa;

		inftl_write_oob(mtd, writeEUN * inftl->EraseSize +
				SECTORSIZE * 4 + 8, 8, &retlen, (char *)&oob.u);

		inftl->PUtable[writeEUN] = inftl->VUtable[thisVUC];
		inftl->VUtable[thisVUC] = writeEUN;

		inftl->numfreeEUNs--;
		return writeEUN;

	} while (silly2--);

	printk(KERN_WARNING "INFTL: error folding to make room for Virtual "
		"Unit Chain 0x%x\n", thisVUC);
	return BLOCK_NIL;
}

/*
 * Given a Virtual Unit Chain, see if it can be deleted, and if so do it.
 */
static void INFTL_trydeletechain(struct INFTLrecord *inftl, unsigned thisVUC)
{
	struct mtd_info *mtd = inftl->mbd.mtd;
	unsigned char BlockUsed[MAX_SECTORS_PER_UNIT];
	unsigned char BlockDeleted[MAX_SECTORS_PER_UNIT];
	unsigned int thisEUN, status;
	int block, silly;
	struct inftl_bci bci;
	size_t retlen;

	pr_debug("INFTL: INFTL_trydeletechain(inftl=%p,"
		"thisVUC=%d)\n", inftl, thisVUC);

	memset(BlockUsed, 0, sizeof(BlockUsed));
	memset(BlockDeleted, 0, sizeof(BlockDeleted));

	thisEUN = inftl->VUtable[thisVUC];
	if (thisEUN == BLOCK_NIL) {
		printk(KERN_WARNING "INFTL: trying to delete non-existent "
		       "Virtual Unit Chain %d!\n", thisVUC);
		return;
	}

	/*
	 * Scan through the Erase Units to determine whether any data is in
	 * each of the 512-byte blocks within the Chain.
	 */
	silly = MAX_LOOPS;
	while (thisEUN < inftl->nb_blocks) {
		for (block = 0; block < inftl->EraseSize/SECTORSIZE; block++) {
			if (BlockUsed[block] || BlockDeleted[block])
				continue;

			if (inftl_read_oob(mtd, (thisEUN * inftl->EraseSize)
					   + (block * SECTORSIZE), 8 , &retlen,
					  (char *)&bci) < 0)
				status = SECTOR_IGNORE;
			else
				status = bci.Status | bci.Status1;

			switch(status) {
			case SECTOR_FREE:
			case SECTOR_IGNORE:
				break;
			case SECTOR_USED:
				BlockUsed[block] = 1;
				continue;
			case SECTOR_DELETED:
				BlockDeleted[block] = 1;
				continue;
			default:
				printk(KERN_WARNING "INFTL: unknown status "
					"for block %d in EUN %d: 0x%x\n",
					block, thisEUN, status);
			}
		}

		if (!silly--) {
			printk(KERN_WARNING "INFTL: infinite loop in Virtual "
				"Unit Chain 0x%x\n", thisVUC);
			return;
		}

		thisEUN = inftl->PUtable[thisEUN];
	}

	for (block = 0; block < inftl->EraseSize/SECTORSIZE; block++)
		if (BlockUsed[block])
			return;

	/*
	 * For each block in the chain free it and make it available
	 * for future use. Erase from the oldest unit first.
	 */
	pr_debug("INFTL: deleting empty VUC %d\n", thisVUC);

	for (;;) {
		u16 *prevEUN = &inftl->VUtable[thisVUC];
		thisEUN = *prevEUN;

		/* If the chain is all gone already, we're done */
		if (thisEUN == BLOCK_NIL) {
			pr_debug("INFTL: Empty VUC %d for deletion was already absent\n", thisEUN);
			return;
		}

		/* Find oldest unit in chain. */
		while (inftl->PUtable[thisEUN] != BLOCK_NIL) {
			BUG_ON(thisEUN >= inftl->nb_blocks);

			prevEUN = &inftl->PUtable[thisEUN];
			thisEUN = *prevEUN;
		}

		pr_debug("Deleting EUN %d from VUC %d\n",
		      thisEUN, thisVUC);

		if (INFTL_formatblock(inftl, thisEUN) < 0) {
			/*
			 * Could not erase : mark block as reserved.
			 */
			inftl->PUtable[thisEUN] = BLOCK_RESERVED;
		} else {
			/* Correctly erased : mark it as free */
			inftl->PUtable[thisEUN] = BLOCK_FREE;
			inftl->numfreeEUNs++;
		}

		/* Now sort out whatever was pointing to it... */
		*prevEUN = BLOCK_NIL;

		/* Ideally we'd actually be responsive to new
		   requests while we're doing this -- if there's
		   free space why should others be made to wait? */
		cond_resched();
	}

	inftl->VUtable[thisVUC] = BLOCK_NIL;
}

static int INFTL_deleteblock(struct INFTLrecord *inftl, unsigned block)
{
	unsigned int thisEUN = inftl->VUtable[block / (inftl->EraseSize / SECTORSIZE)];
	unsigned long blockofs = (block * SECTORSIZE) & (inftl->EraseSize - 1);
	struct mtd_info *mtd = inftl->mbd.mtd;
	unsigned int status;
	int silly = MAX_LOOPS;
	size_t retlen;
	struct inftl_bci bci;

	pr_debug("INFTL: INFTL_deleteblock(inftl=%p,"
		"block=%d)\n", inftl, block);

	while (thisEUN < inftl->nb_blocks) {
		if (inftl_read_oob(mtd, (thisEUN * inftl->EraseSize) +
				   blockofs, 8, &retlen, (char *)&bci) < 0)
			status = SECTOR_IGNORE;
		else
			status = bci.Status | bci.Status1;

		switch (status) {
		case SECTOR_FREE:
		case SECTOR_IGNORE:
			break;
		case SECTOR_DELETED:
			thisEUN = BLOCK_NIL;
			goto foundit;
		case SECTOR_USED:
			goto foundit;
		default:
			printk(KERN_WARNING "INFTL: unknown status for "
				"block %d in EUN %d: 0x%x\n",
				block, thisEUN, status);
			break;
		}

		if (!silly--) {
			printk(KERN_WARNING "INFTL: infinite loop in Virtual "
				"Unit Chain 0x%x\n",
				block / (inftl->EraseSize / SECTORSIZE));
			return 1;
		}
		thisEUN = inftl->PUtable[thisEUN];
	}

foundit:
	if (thisEUN != BLOCK_NIL) {
		loff_t ptr = (thisEUN * inftl->EraseSize) + blockofs;

		if (inftl_read_oob(mtd, ptr, 8, &retlen, (char *)&bci) < 0)
			return -EIO;
		bci.Status = bci.Status1 = SECTOR_DELETED;
		if (inftl_write_oob(mtd, ptr, 8, &retlen, (char *)&bci) < 0)
			return -EIO;
		INFTL_trydeletechain(inftl, block / (inftl->EraseSize / SECTORSIZE));
	}
	return 0;
}

static int inftl_writeblock(struct mtd_blktrans_dev *mbd, unsigned long block,
			    char *buffer)
{
	struct INFTLrecord *inftl = (void *)mbd;
	unsigned int writeEUN;
	unsigned long blockofs = (block * SECTORSIZE) & (inftl->EraseSize - 1);
	size_t retlen;
	struct inftl_oob oob;
	char *p, *pend;

	pr_debug("INFTL: inftl_writeblock(inftl=%p,block=%ld,"
		"buffer=%p)\n", inftl, block, buffer);

	/* Is block all zero? */
	pend = buffer + SECTORSIZE;
	for (p = buffer; p < pend && !*p; p++)
		;

	if (p < pend) {
		writeEUN = INFTL_findwriteunit(inftl, block);

		if (writeEUN == BLOCK_NIL) {
			printk(KERN_WARNING "inftl_writeblock(): cannot find "
				"block to write to\n");
			/*
			 * If we _still_ haven't got a block to use,
			 * we're screwed.
			 */
			return 1;
		}

		memset(&oob, 0xff, sizeof(struct inftl_oob));
		oob.b.Status = oob.b.Status1 = SECTOR_USED;

		inftl_write(inftl->mbd.mtd, (writeEUN * inftl->EraseSize) +
			    blockofs, SECTORSIZE, &retlen, (char *)buffer,
			    (char *)&oob);
		/*
		 * need to write SECTOR_USED flags since they are not written
		 * in mtd_writeecc
		 */
	} else {
		INFTL_deleteblock(inftl, block);
	}

	return 0;
}

static int inftl_readblock(struct mtd_blktrans_dev *mbd, unsigned long block,
			   char *buffer)
{
	struct INFTLrecord *inftl = (void *)mbd;
	unsigned int thisEUN = inftl->VUtable[block / (inftl->EraseSize / SECTORSIZE)];
	unsigned long blockofs = (block * SECTORSIZE) & (inftl->EraseSize - 1);
	struct mtd_info *mtd = inftl->mbd.mtd;
	unsigned int status;
	int silly = MAX_LOOPS;
	struct inftl_bci bci;
	size_t retlen;

	pr_debug("INFTL: inftl_readblock(inftl=%p,block=%ld,"
		"buffer=%p)\n", inftl, block, buffer);

	while (thisEUN < inftl->nb_blocks) {
		if (inftl_read_oob(mtd, (thisEUN * inftl->EraseSize) +
				  blockofs, 8, &retlen, (char *)&bci) < 0)
			status = SECTOR_IGNORE;
		else
			status = bci.Status | bci.Status1;

		switch (status) {
		case SECTOR_DELETED:
			thisEUN = BLOCK_NIL;
			goto foundit;
		case SECTOR_USED:
			goto foundit;
		case SECTOR_FREE:
		case SECTOR_IGNORE:
			break;
		default:
			printk(KERN_WARNING "INFTL: unknown status for "
				"block %ld in EUN %d: 0x%04x\n",
				block, thisEUN, status);
			break;
		}

		if (!silly--) {
			printk(KERN_WARNING "INFTL: infinite loop in "
				"Virtual Unit Chain 0x%lx\n",
				block / (inftl->EraseSize / SECTORSIZE));
			return 1;
		}

		thisEUN = inftl->PUtable[thisEUN];
	}

foundit:
	if (thisEUN == BLOCK_NIL) {
		/* The requested block is not on the media, return all 0x00 */
		memset(buffer, 0, SECTORSIZE);
	} else {
		size_t retlen;
		loff_t ptr = (thisEUN * inftl->EraseSize) + blockofs;
		int ret = mtd->read(mtd, ptr, SECTORSIZE, &retlen, buffer);

		/* Handle corrected bit flips gracefully */
		if (ret < 0 && !mtd_is_bitflip(ret))
			return -EIO;
	}
	return 0;
}

static int inftl_getgeo(struct mtd_blktrans_dev *dev, struct hd_geometry *geo)
{
	struct INFTLrecord *inftl = (void *)dev;

	geo->heads = inftl->heads;
	geo->sectors = inftl->sectors;
	geo->cylinders = inftl->cylinders;

	return 0;
}

static struct mtd_blktrans_ops inftl_tr = {
	.name		= "inftl",
	.major		= INFTL_MAJOR,
	.part_bits	= INFTL_PARTN_BITS,
	.blksize 	= 512,
	.getgeo		= inftl_getgeo,
	.readsect	= inftl_readblock,
	.writesect	= inftl_writeblock,
	.add_mtd	= inftl_add_mtd,
	.remove_dev	= inftl_remove_dev,
	.owner		= THIS_MODULE,
};

static int __init init_inftl(void)
{
	return register_mtd_blktrans(&inftl_tr);
}

static void __exit cleanup_inftl(void)
{
	deregister_mtd_blktrans(&inftl_tr);
}

module_init(init_inftl);
module_exit(cleanup_inftl);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Greg Ungerer <gerg@snapgear.com>, David Woodhouse <dwmw2@infradead.org>, Fabrice Bellard <fabrice.bellard@netgem.com> et al.");
MODULE_DESCRIPTION("Support code for Inverse Flash Translation Layer, used on M-Systems DiskOnChip 2000, Millennium and Millennium Plus");
