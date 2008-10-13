/* Linux driver for NAND Flash Translation Layer      */
/* (c) 1999 Machine Vision Holdings, Inc.             */
/* Author: David Woodhouse <dwmw2@infradead.org>      */

/*
  The contents of this file are distributed under the GNU General
  Public License version 2. The author places no additional
  restrictions of any kind on it.
 */

#define PRERELEASE

#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/errno.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/hdreg.h>

#include <linux/kmod.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nftl.h>
#include <linux/mtd/blktrans.h>

/* maximum number of loops while examining next block, to have a
   chance to detect consistency problems (they should never happen
   because of the checks done in the mounting */

#define MAX_LOOPS 10000


static void nftl_add_mtd(struct mtd_blktrans_ops *tr, struct mtd_info *mtd)
{
	struct NFTLrecord *nftl;
	unsigned long temp;

	if (mtd->type != MTD_NANDFLASH)
		return;
	/* OK, this is moderately ugly.  But probably safe.  Alternatives? */
	if (memcmp(mtd->name, "DiskOnChip", 10))
		return;

	if (!mtd->block_isbad) {
		printk(KERN_ERR
"NFTL no longer supports the old DiskOnChip drivers loaded via docprobe.\n"
"Please use the new diskonchip driver under the NAND subsystem.\n");
		return;
	}

	DEBUG(MTD_DEBUG_LEVEL1, "NFTL: add_mtd for %s\n", mtd->name);

	nftl = kzalloc(sizeof(struct NFTLrecord), GFP_KERNEL);

	if (!nftl) {
		printk(KERN_WARNING "NFTL: out of memory for data structures\n");
		return;
	}

	nftl->mbd.mtd = mtd;
	nftl->mbd.devnum = -1;

	nftl->mbd.tr = tr;

        if (NFTL_mount(nftl) < 0) {
		printk(KERN_WARNING "NFTL: could not mount device\n");
		kfree(nftl);
		return;
        }

	/* OK, it's a new one. Set up all the data structures. */

	/* Calculate geometry */
	nftl->cylinders = 1024;
	nftl->heads = 16;

	temp = nftl->cylinders * nftl->heads;
	nftl->sectors = nftl->mbd.size / temp;
	if (nftl->mbd.size % temp) {
		nftl->sectors++;
		temp = nftl->cylinders * nftl->sectors;
		nftl->heads = nftl->mbd.size / temp;

		if (nftl->mbd.size % temp) {
			nftl->heads++;
			temp = nftl->heads * nftl->sectors;
			nftl->cylinders = nftl->mbd.size / temp;
		}
	}

	if (nftl->mbd.size != nftl->heads * nftl->cylinders * nftl->sectors) {
		/*
		  Oh no we don't have
		   mbd.size == heads * cylinders * sectors
		*/
		printk(KERN_WARNING "NFTL: cannot calculate a geometry to "
		       "match size of 0x%lx.\n", nftl->mbd.size);
		printk(KERN_WARNING "NFTL: using C:%d H:%d S:%d "
			"(== 0x%lx sects)\n",
			nftl->cylinders, nftl->heads , nftl->sectors,
			(long)nftl->cylinders * (long)nftl->heads *
			(long)nftl->sectors );
	}

	if (add_mtd_blktrans_dev(&nftl->mbd)) {
		kfree(nftl->ReplUnitTable);
		kfree(nftl->EUNtable);
		kfree(nftl);
		return;
	}
#ifdef PSYCHO_DEBUG
	printk(KERN_INFO "NFTL: Found new nftl%c\n", nftl->mbd.devnum + 'a');
#endif
}

static void nftl_remove_dev(struct mtd_blktrans_dev *dev)
{
	struct NFTLrecord *nftl = (void *)dev;

	DEBUG(MTD_DEBUG_LEVEL1, "NFTL: remove_dev (i=%d)\n", dev->devnum);

	del_mtd_blktrans_dev(dev);
	kfree(nftl->ReplUnitTable);
	kfree(nftl->EUNtable);
	kfree(nftl);
}

/*
 * Read oob data from flash
 */
int nftl_read_oob(struct mtd_info *mtd, loff_t offs, size_t len,
		  size_t *retlen, uint8_t *buf)
{
	struct mtd_oob_ops ops;
	int res;

	ops.mode = MTD_OOB_PLACE;
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
int nftl_write_oob(struct mtd_info *mtd, loff_t offs, size_t len,
		   size_t *retlen, uint8_t *buf)
{
	struct mtd_oob_ops ops;
	int res;

	ops.mode = MTD_OOB_PLACE;
	ops.ooboffs = offs & (mtd->writesize - 1);
	ops.ooblen = len;
	ops.oobbuf = buf;
	ops.datbuf = NULL;

	res = mtd->write_oob(mtd, offs & ~(mtd->writesize - 1), &ops);
	*retlen = ops.oobretlen;
	return res;
}

#ifdef CONFIG_NFTL_RW

/*
 * Write data and oob to flash
 */
static int nftl_write(struct mtd_info *mtd, loff_t offs, size_t len,
		      size_t *retlen, uint8_t *buf, uint8_t *oob)
{
	struct mtd_oob_ops ops;
	int res;

	ops.mode = MTD_OOB_PLACE;
	ops.ooboffs = offs;
	ops.ooblen = mtd->oobsize;
	ops.oobbuf = oob;
	ops.datbuf = buf;
	ops.len = len;

	res = mtd->write_oob(mtd, offs & ~(mtd->writesize - 1), &ops);
	*retlen = ops.retlen;
	return res;
}

/* Actual NFTL access routines */
/* NFTL_findfreeblock: Find a free Erase Unit on the NFTL partition. This function is used
 *	when the give Virtual Unit Chain
 */
static u16 NFTL_findfreeblock(struct NFTLrecord *nftl, int desperate )
{
	/* For a given Virtual Unit Chain: find or create a free block and
	   add it to the chain */
	/* We're passed the number of the last EUN in the chain, to save us from
	   having to look it up again */
	u16 pot = nftl->LastFreeEUN;
	int silly = nftl->nb_blocks;

	/* Normally, we force a fold to happen before we run out of free blocks completely */
	if (!desperate && nftl->numfreeEUNs < 2) {
		DEBUG(MTD_DEBUG_LEVEL1, "NFTL_findfreeblock: there are too few free EUNs\n");
		return 0xffff;
	}

	/* Scan for a free block */
	do {
		if (nftl->ReplUnitTable[pot] == BLOCK_FREE) {
			nftl->LastFreeEUN = pot;
			nftl->numfreeEUNs--;
			return pot;
		}

		/* This will probably point to the MediaHdr unit itself,
		   right at the beginning of the partition. But that unit
		   (and the backup unit too) should have the UCI set
		   up so that it's not selected for overwriting */
		if (++pot > nftl->lastEUN)
			pot = le16_to_cpu(nftl->MediaHdr.FirstPhysicalEUN);

		if (!silly--) {
			printk("Argh! No free blocks found! LastFreeEUN = %d, "
			       "FirstEUN = %d\n", nftl->LastFreeEUN,
			       le16_to_cpu(nftl->MediaHdr.FirstPhysicalEUN));
			return 0xffff;
		}
	} while (pot != nftl->LastFreeEUN);

	return 0xffff;
}

static u16 NFTL_foldchain (struct NFTLrecord *nftl, unsigned thisVUC, unsigned pendingblock )
{
	struct mtd_info *mtd = nftl->mbd.mtd;
	u16 BlockMap[MAX_SECTORS_PER_UNIT];
	unsigned char BlockLastState[MAX_SECTORS_PER_UNIT];
	unsigned char BlockFreeFound[MAX_SECTORS_PER_UNIT];
	unsigned int thisEUN;
	int block;
	int silly;
	unsigned int targetEUN;
	struct nftl_oob oob;
	int inplace = 1;
	size_t retlen;

	memset(BlockMap, 0xff, sizeof(BlockMap));
	memset(BlockFreeFound, 0, sizeof(BlockFreeFound));

	thisEUN = nftl->EUNtable[thisVUC];

	if (thisEUN == BLOCK_NIL) {
		printk(KERN_WARNING "Trying to fold non-existent "
		       "Virtual Unit Chain %d!\n", thisVUC);
		return BLOCK_NIL;
	}

	/* Scan to find the Erase Unit which holds the actual data for each
	   512-byte block within the Chain.
	*/
	silly = MAX_LOOPS;
	targetEUN = BLOCK_NIL;
	while (thisEUN <= nftl->lastEUN ) {
		unsigned int status, foldmark;

		targetEUN = thisEUN;
		for (block = 0; block < nftl->EraseSize / 512; block ++) {
			nftl_read_oob(mtd, (thisEUN * nftl->EraseSize) +
				      (block * 512), 16 , &retlen,
				      (char *)&oob);
			if (block == 2) {
				foldmark = oob.u.c.FoldMark | oob.u.c.FoldMark1;
				if (foldmark == FOLD_MARK_IN_PROGRESS) {
					DEBUG(MTD_DEBUG_LEVEL1,
					      "Write Inhibited on EUN %d\n", thisEUN);
					inplace = 0;
				} else {
					/* There's no other reason not to do inplace,
					   except ones that come later. So we don't need
					   to preserve inplace */
					inplace = 1;
				}
			}
			status = oob.b.Status | oob.b.Status1;
			BlockLastState[block] = status;

			switch(status) {
			case SECTOR_FREE:
				BlockFreeFound[block] = 1;
				break;

			case SECTOR_USED:
				if (!BlockFreeFound[block])
					BlockMap[block] = thisEUN;
				else
					printk(KERN_WARNING
					       "SECTOR_USED found after SECTOR_FREE "
					       "in Virtual Unit Chain %d for block %d\n",
					       thisVUC, block);
				break;
			case SECTOR_DELETED:
				if (!BlockFreeFound[block])
					BlockMap[block] = BLOCK_NIL;
				else
					printk(KERN_WARNING
					       "SECTOR_DELETED found after SECTOR_FREE "
					       "in Virtual Unit Chain %d for block %d\n",
					       thisVUC, block);
				break;

			case SECTOR_IGNORE:
				break;
			default:
				printk("Unknown status for block %d in EUN %d: %x\n",
				       block, thisEUN, status);
			}
		}

		if (!silly--) {
			printk(KERN_WARNING "Infinite loop in Virtual Unit Chain 0x%x\n",
			       thisVUC);
			return BLOCK_NIL;
		}

		thisEUN = nftl->ReplUnitTable[thisEUN];
	}

	if (inplace) {
		/* We're being asked to be a fold-in-place. Check
		   that all blocks which actually have data associated
		   with them (i.e. BlockMap[block] != BLOCK_NIL) are
		   either already present or SECTOR_FREE in the target
		   block. If not, we're going to have to fold out-of-place
		   anyway.
		*/
		for (block = 0; block < nftl->EraseSize / 512 ; block++) {
			if (BlockLastState[block] != SECTOR_FREE &&
			    BlockMap[block] != BLOCK_NIL &&
			    BlockMap[block] != targetEUN) {
				DEBUG(MTD_DEBUG_LEVEL1, "Setting inplace to 0. VUC %d, "
				      "block %d was %x lastEUN, "
				      "and is in EUN %d (%s) %d\n",
				      thisVUC, block, BlockLastState[block],
				      BlockMap[block],
				      BlockMap[block]== targetEUN ? "==" : "!=",
				      targetEUN);
				inplace = 0;
				break;
			}
		}

		if (pendingblock >= (thisVUC * (nftl->EraseSize / 512)) &&
		    pendingblock < ((thisVUC + 1)* (nftl->EraseSize / 512)) &&
		    BlockLastState[pendingblock - (thisVUC * (nftl->EraseSize / 512))] !=
		    SECTOR_FREE) {
			DEBUG(MTD_DEBUG_LEVEL1, "Pending write not free in EUN %d. "
			      "Folding out of place.\n", targetEUN);
			inplace = 0;
		}
	}

	if (!inplace) {
		DEBUG(MTD_DEBUG_LEVEL1, "Cannot fold Virtual Unit Chain %d in place. "
		      "Trying out-of-place\n", thisVUC);
		/* We need to find a targetEUN to fold into. */
		targetEUN = NFTL_findfreeblock(nftl, 1);
		if (targetEUN == BLOCK_NIL) {
			/* Ouch. Now we're screwed. We need to do a
			   fold-in-place of another chain to make room
			   for this one. We need a better way of selecting
			   which chain to fold, because makefreeblock will
			   only ask us to fold the same one again.
			*/
			printk(KERN_WARNING
			       "NFTL_findfreeblock(desperate) returns 0xffff.\n");
			return BLOCK_NIL;
		}
	} else {
		/* We put a fold mark in the chain we are folding only if we
               fold in place to help the mount check code. If we do not fold in
               place, it is possible to find the valid chain by selecting the
               longer one */
		oob.u.c.FoldMark = oob.u.c.FoldMark1 = cpu_to_le16(FOLD_MARK_IN_PROGRESS);
		oob.u.c.unused = 0xffffffff;
		nftl_write_oob(mtd, (nftl->EraseSize * targetEUN) + 2 * 512 + 8,
			       8, &retlen, (char *)&oob.u);
	}

	/* OK. We now know the location of every block in the Virtual Unit Chain,
	   and the Erase Unit into which we are supposed to be copying.
	   Go for it.
	*/
	DEBUG(MTD_DEBUG_LEVEL1,"Folding chain %d into unit %d\n", thisVUC, targetEUN);
	for (block = 0; block < nftl->EraseSize / 512 ; block++) {
		unsigned char movebuf[512];
		int ret;

		/* If it's in the target EUN already, or if it's pending write, do nothing */
		if (BlockMap[block] == targetEUN ||
		    (pendingblock == (thisVUC * (nftl->EraseSize / 512) + block))) {
			continue;
		}

		/* copy only in non free block (free blocks can only
                   happen in case of media errors or deleted blocks) */
		if (BlockMap[block] == BLOCK_NIL)
			continue;

		ret = mtd->read(mtd, (nftl->EraseSize * BlockMap[block]) + (block * 512),
				512, &retlen, movebuf);
		if (ret < 0 && ret != -EUCLEAN) {
			ret = mtd->read(mtd, (nftl->EraseSize * BlockMap[block])
					+ (block * 512), 512, &retlen,
					movebuf);
			if (ret != -EIO)
				printk("Error went away on retry.\n");
		}
		memset(&oob, 0xff, sizeof(struct nftl_oob));
		oob.b.Status = oob.b.Status1 = SECTOR_USED;

		nftl_write(nftl->mbd.mtd, (nftl->EraseSize * targetEUN) +
			   (block * 512), 512, &retlen, movebuf, (char *)&oob);
	}

	/* add the header so that it is now a valid chain */
	oob.u.a.VirtUnitNum = oob.u.a.SpareVirtUnitNum = cpu_to_le16(thisVUC);
	oob.u.a.ReplUnitNum = oob.u.a.SpareReplUnitNum = 0xffff;

	nftl_write_oob(mtd, (nftl->EraseSize * targetEUN) + 8,
		       8, &retlen, (char *)&oob.u);

	/* OK. We've moved the whole lot into the new block. Now we have to free the original blocks. */

	/* At this point, we have two different chains for this Virtual Unit, and no way to tell
	   them apart. If we crash now, we get confused. However, both contain the same data, so we
	   shouldn't actually lose data in this case. It's just that when we load up on a medium which
	   has duplicate chains, we need to free one of the chains because it's not necessary any more.
	*/
	thisEUN = nftl->EUNtable[thisVUC];
	DEBUG(MTD_DEBUG_LEVEL1,"Want to erase\n");

	/* For each block in the old chain (except the targetEUN of course),
	   free it and make it available for future use */
	while (thisEUN <= nftl->lastEUN && thisEUN != targetEUN) {
		unsigned int EUNtmp;

		EUNtmp = nftl->ReplUnitTable[thisEUN];

		if (NFTL_formatblock(nftl, thisEUN) < 0) {
			/* could not erase : mark block as reserved
			 */
			nftl->ReplUnitTable[thisEUN] = BLOCK_RESERVED;
		} else {
			/* correctly erased : mark it as free */
			nftl->ReplUnitTable[thisEUN] = BLOCK_FREE;
			nftl->numfreeEUNs++;
		}
		thisEUN = EUNtmp;
	}

	/* Make this the new start of chain for thisVUC */
	nftl->ReplUnitTable[targetEUN] = BLOCK_NIL;
	nftl->EUNtable[thisVUC] = targetEUN;

	return targetEUN;
}

static u16 NFTL_makefreeblock( struct NFTLrecord *nftl , unsigned pendingblock)
{
	/* This is the part that needs some cleverness applied.
	   For now, I'm doing the minimum applicable to actually
	   get the thing to work.
	   Wear-levelling and other clever stuff needs to be implemented
	   and we also need to do some assessment of the results when
	   the system loses power half-way through the routine.
	*/
	u16 LongestChain = 0;
	u16 ChainLength = 0, thislen;
	u16 chain, EUN;

	for (chain = 0; chain < le32_to_cpu(nftl->MediaHdr.FormattedSize) / nftl->EraseSize; chain++) {
		EUN = nftl->EUNtable[chain];
		thislen = 0;

		while (EUN <= nftl->lastEUN) {
			thislen++;
			//printk("VUC %d reaches len %d with EUN %d\n", chain, thislen, EUN);
			EUN = nftl->ReplUnitTable[EUN] & 0x7fff;
			if (thislen > 0xff00) {
				printk("Endless loop in Virtual Chain %d: Unit %x\n",
				       chain, EUN);
			}
			if (thislen > 0xff10) {
				/* Actually, don't return failure. Just ignore this chain and
				   get on with it. */
				thislen = 0;
				break;
			}
		}

		if (thislen > ChainLength) {
			//printk("New longest chain is %d with length %d\n", chain, thislen);
			ChainLength = thislen;
			LongestChain = chain;
		}
	}

	if (ChainLength < 2) {
		printk(KERN_WARNING "No Virtual Unit Chains available for folding. "
		       "Failing request\n");
		return 0xffff;
	}

	return NFTL_foldchain (nftl, LongestChain, pendingblock);
}

/* NFTL_findwriteunit: Return the unit number into which we can write
                       for this block. Make it available if it isn't already
*/
static inline u16 NFTL_findwriteunit(struct NFTLrecord *nftl, unsigned block)
{
	u16 lastEUN;
	u16 thisVUC = block / (nftl->EraseSize / 512);
	struct mtd_info *mtd = nftl->mbd.mtd;
	unsigned int writeEUN;
	unsigned long blockofs = (block * 512) & (nftl->EraseSize -1);
	size_t retlen;
	int silly, silly2 = 3;
	struct nftl_oob oob;

	do {
		/* Scan the media to find a unit in the VUC which has
		   a free space for the block in question.
		*/

		/* This condition catches the 0x[7f]fff cases, as well as
		   being a sanity check for past-end-of-media access
		*/
		lastEUN = BLOCK_NIL;
		writeEUN = nftl->EUNtable[thisVUC];
		silly = MAX_LOOPS;
		while (writeEUN <= nftl->lastEUN) {
			struct nftl_bci bci;
			size_t retlen;
			unsigned int status;

			lastEUN = writeEUN;

			nftl_read_oob(mtd,
				      (writeEUN * nftl->EraseSize) + blockofs,
				      8, &retlen, (char *)&bci);

			DEBUG(MTD_DEBUG_LEVEL2, "Status of block %d in EUN %d is %x\n",
			      block , writeEUN, le16_to_cpu(bci.Status));

			status = bci.Status | bci.Status1;
			switch(status) {
			case SECTOR_FREE:
				return writeEUN;

			case SECTOR_DELETED:
			case SECTOR_USED:
			case SECTOR_IGNORE:
				break;
			default:
				// Invalid block. Don't use it any more. Must implement.
				break;
			}

			if (!silly--) {
				printk(KERN_WARNING
				       "Infinite loop in Virtual Unit Chain 0x%x\n",
				       thisVUC);
				return 0xffff;
			}

			/* Skip to next block in chain */
			writeEUN = nftl->ReplUnitTable[writeEUN];
		}

		/* OK. We didn't find one in the existing chain, or there
		   is no existing chain. */

		/* Try to find an already-free block */
		writeEUN = NFTL_findfreeblock(nftl, 0);

		if (writeEUN == BLOCK_NIL) {
			/* That didn't work - there were no free blocks just
			   waiting to be picked up. We're going to have to fold
			   a chain to make room.
			*/

			/* First remember the start of this chain */
			//u16 startEUN = nftl->EUNtable[thisVUC];

			//printk("Write to VirtualUnitChain %d, calling makefreeblock()\n", thisVUC);
			writeEUN = NFTL_makefreeblock(nftl, 0xffff);

			if (writeEUN == BLOCK_NIL) {
				/* OK, we accept that the above comment is
				   lying - there may have been free blocks
				   last time we called NFTL_findfreeblock(),
				   but they are reserved for when we're
				   desperate. Well, now we're desperate.
				*/
				DEBUG(MTD_DEBUG_LEVEL1, "Using desperate==1 to find free EUN to accommodate write to VUC %d\n", thisVUC);
				writeEUN = NFTL_findfreeblock(nftl, 1);
			}
			if (writeEUN == BLOCK_NIL) {
				/* Ouch. This should never happen - we should
				   always be able to make some room somehow.
				   If we get here, we've allocated more storage
				   space than actual media, or our makefreeblock
				   routine is missing something.
				*/
				printk(KERN_WARNING "Cannot make free space.\n");
				return BLOCK_NIL;
			}
			//printk("Restarting scan\n");
			lastEUN = BLOCK_NIL;
			continue;
		}

		/* We've found a free block. Insert it into the chain. */

		if (lastEUN != BLOCK_NIL) {
			thisVUC |= 0x8000; /* It's a replacement block */
		} else {
			/* The first block in a new chain */
			nftl->EUNtable[thisVUC] = writeEUN;
		}

		/* set up the actual EUN we're writing into */
		/* Both in our cache... */
		nftl->ReplUnitTable[writeEUN] = BLOCK_NIL;

		/* ... and on the flash itself */
		nftl_read_oob(mtd, writeEUN * nftl->EraseSize + 8, 8,
			      &retlen, (char *)&oob.u);

		oob.u.a.VirtUnitNum = oob.u.a.SpareVirtUnitNum = cpu_to_le16(thisVUC);

		nftl_write_oob(mtd, writeEUN * nftl->EraseSize + 8, 8,
			       &retlen, (char *)&oob.u);

		/* we link the new block to the chain only after the
                   block is ready. It avoids the case where the chain
                   could point to a free block */
		if (lastEUN != BLOCK_NIL) {
			/* Both in our cache... */
			nftl->ReplUnitTable[lastEUN] = writeEUN;
			/* ... and on the flash itself */
			nftl_read_oob(mtd, (lastEUN * nftl->EraseSize) + 8,
				      8, &retlen, (char *)&oob.u);

			oob.u.a.ReplUnitNum = oob.u.a.SpareReplUnitNum
				= cpu_to_le16(writeEUN);

			nftl_write_oob(mtd, (lastEUN * nftl->EraseSize) + 8,
				       8, &retlen, (char *)&oob.u);
		}

		return writeEUN;

	} while (silly2--);

	printk(KERN_WARNING "Error folding to make room for Virtual Unit Chain 0x%x\n",
	       thisVUC);
	return 0xffff;
}

static int nftl_writeblock(struct mtd_blktrans_dev *mbd, unsigned long block,
			   char *buffer)
{
	struct NFTLrecord *nftl = (void *)mbd;
	u16 writeEUN;
	unsigned long blockofs = (block * 512) & (nftl->EraseSize - 1);
	size_t retlen;
	struct nftl_oob oob;

	writeEUN = NFTL_findwriteunit(nftl, block);

	if (writeEUN == BLOCK_NIL) {
		printk(KERN_WARNING
		       "NFTL_writeblock(): Cannot find block to write to\n");
		/* If we _still_ haven't got a block to use, we're screwed */
		return 1;
	}

	memset(&oob, 0xff, sizeof(struct nftl_oob));
	oob.b.Status = oob.b.Status1 = SECTOR_USED;

	nftl_write(nftl->mbd.mtd, (writeEUN * nftl->EraseSize) + blockofs,
		   512, &retlen, (char *)buffer, (char *)&oob);
	return 0;
}
#endif /* CONFIG_NFTL_RW */

static int nftl_readblock(struct mtd_blktrans_dev *mbd, unsigned long block,
			  char *buffer)
{
	struct NFTLrecord *nftl = (void *)mbd;
	struct mtd_info *mtd = nftl->mbd.mtd;
	u16 lastgoodEUN;
	u16 thisEUN = nftl->EUNtable[block / (nftl->EraseSize / 512)];
	unsigned long blockofs = (block * 512) & (nftl->EraseSize - 1);
	unsigned int status;
	int silly = MAX_LOOPS;
	size_t retlen;
	struct nftl_bci bci;

	lastgoodEUN = BLOCK_NIL;

	if (thisEUN != BLOCK_NIL) {
		while (thisEUN < nftl->nb_blocks) {
			if (nftl_read_oob(mtd, (thisEUN * nftl->EraseSize) +
					  blockofs, 8, &retlen,
					  (char *)&bci) < 0)
				status = SECTOR_IGNORE;
			else
				status = bci.Status | bci.Status1;

			switch (status) {
			case SECTOR_FREE:
				/* no modification of a sector should follow a free sector */
				goto the_end;
			case SECTOR_DELETED:
				lastgoodEUN = BLOCK_NIL;
				break;
			case SECTOR_USED:
				lastgoodEUN = thisEUN;
				break;
			case SECTOR_IGNORE:
				break;
			default:
				printk("Unknown status for block %ld in EUN %d: %x\n",
				       block, thisEUN, status);
				break;
			}

			if (!silly--) {
				printk(KERN_WARNING "Infinite loop in Virtual Unit Chain 0x%lx\n",
				       block / (nftl->EraseSize / 512));
				return 1;
			}
			thisEUN = nftl->ReplUnitTable[thisEUN];
		}
	}

 the_end:
	if (lastgoodEUN == BLOCK_NIL) {
		/* the requested block is not on the media, return all 0x00 */
		memset(buffer, 0, 512);
	} else {
		loff_t ptr = (lastgoodEUN * nftl->EraseSize) + blockofs;
		size_t retlen;
		int res = mtd->read(mtd, ptr, 512, &retlen, buffer);

		if (res < 0 && res != -EUCLEAN)
			return -EIO;
	}
	return 0;
}

static int nftl_getgeo(struct mtd_blktrans_dev *dev,  struct hd_geometry *geo)
{
	struct NFTLrecord *nftl = (void *)dev;

	geo->heads = nftl->heads;
	geo->sectors = nftl->sectors;
	geo->cylinders = nftl->cylinders;

	return 0;
}

/****************************************************************************
 *
 * Module stuff
 *
 ****************************************************************************/


static struct mtd_blktrans_ops nftl_tr = {
	.name		= "nftl",
	.major		= NFTL_MAJOR,
	.part_bits	= NFTL_PARTN_BITS,
	.blksize 	= 512,
	.getgeo		= nftl_getgeo,
	.readsect	= nftl_readblock,
#ifdef CONFIG_NFTL_RW
	.writesect	= nftl_writeblock,
#endif
	.add_mtd	= nftl_add_mtd,
	.remove_dev	= nftl_remove_dev,
	.owner		= THIS_MODULE,
};

static int __init init_nftl(void)
{
	return register_mtd_blktrans(&nftl_tr);
}

static void __exit cleanup_nftl(void)
{
	deregister_mtd_blktrans(&nftl_tr);
}

module_init(init_nftl);
module_exit(cleanup_nftl);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>, Fabrice Bellard <fabrice.bellard@netgem.com> et al.");
MODULE_DESCRIPTION("Support code for NAND Flash Translation Layer, used on M-Systems DiskOnChip 2000 and Millennium");
