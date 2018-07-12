/*
 * Common Flash Interface support:
 *   ST Advanced Architecture Command Set (ID 0x0020)
 *
 * (C) 2000 Red Hat. GPL'd
 *
 * 10/10/2000	Nicolas Pitre <nico@fluxnic.net>
 * 	- completely revamped method functions so they are aware and
 * 	  independent of the flash geometry (buswidth, interleave, etc.)
 * 	- scalability vs code size is completely set at compile-time
 * 	  (see include/linux/mtd/cfi.h for selection)
 *	- optimized write buffer method
 * 06/21/2002	Joern Engel <joern@wh.fh-wedel.de> and others
 *	- modified Intel Command Set 0x0001 to support ST Advanced Architecture
 *	  (command set 0x0020)
 *	- added a writev function
 * 07/13/2005	Joern Engel <joern@wh.fh-wedel.de>
 * 	- Plugged memory leak in cfi_staa_writev().
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <asm/byteorder.h>

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mtd/map.h>
#include <linux/mtd/cfi.h>
#include <linux/mtd/mtd.h>


static int cfi_staa_read(struct mtd_info *, loff_t, size_t, size_t *, u_char *);
static int cfi_staa_write_buffers(struct mtd_info *, loff_t, size_t, size_t *, const u_char *);
static int cfi_staa_writev(struct mtd_info *mtd, const struct kvec *vecs,
		unsigned long count, loff_t to, size_t *retlen);
static int cfi_staa_erase_varsize(struct mtd_info *, struct erase_info *);
static void cfi_staa_sync (struct mtd_info *);
static int cfi_staa_lock(struct mtd_info *mtd, loff_t ofs, uint64_t len);
static int cfi_staa_unlock(struct mtd_info *mtd, loff_t ofs, uint64_t len);
static int cfi_staa_suspend (struct mtd_info *);
static void cfi_staa_resume (struct mtd_info *);

static void cfi_staa_destroy(struct mtd_info *);

struct mtd_info *cfi_cmdset_0020(struct map_info *, int);

static struct mtd_info *cfi_staa_setup (struct map_info *);

static struct mtd_chip_driver cfi_staa_chipdrv = {
	.probe		= NULL, /* Not usable directly */
	.destroy	= cfi_staa_destroy,
	.name		= "cfi_cmdset_0020",
	.module		= THIS_MODULE
};

/* #define DEBUG_LOCK_BITS */
//#define DEBUG_CFI_FEATURES

#ifdef DEBUG_CFI_FEATURES
static void cfi_tell_features(struct cfi_pri_intelext *extp)
{
        int i;
        printk("  Feature/Command Support: %4.4X\n", extp->FeatureSupport);
	printk("     - Chip Erase:         %s\n", extp->FeatureSupport&1?"supported":"unsupported");
	printk("     - Suspend Erase:      %s\n", extp->FeatureSupport&2?"supported":"unsupported");
	printk("     - Suspend Program:    %s\n", extp->FeatureSupport&4?"supported":"unsupported");
	printk("     - Legacy Lock/Unlock: %s\n", extp->FeatureSupport&8?"supported":"unsupported");
	printk("     - Queued Erase:       %s\n", extp->FeatureSupport&16?"supported":"unsupported");
	printk("     - Instant block lock: %s\n", extp->FeatureSupport&32?"supported":"unsupported");
	printk("     - Protection Bits:    %s\n", extp->FeatureSupport&64?"supported":"unsupported");
	printk("     - Page-mode read:     %s\n", extp->FeatureSupport&128?"supported":"unsupported");
	printk("     - Synchronous read:   %s\n", extp->FeatureSupport&256?"supported":"unsupported");
	for (i=9; i<32; i++) {
		if (extp->FeatureSupport & (1<<i))
			printk("     - Unknown Bit %X:      supported\n", i);
	}

	printk("  Supported functions after Suspend: %2.2X\n", extp->SuspendCmdSupport);
	printk("     - Program after Erase Suspend: %s\n", extp->SuspendCmdSupport&1?"supported":"unsupported");
	for (i=1; i<8; i++) {
		if (extp->SuspendCmdSupport & (1<<i))
			printk("     - Unknown Bit %X:               supported\n", i);
	}

	printk("  Block Status Register Mask: %4.4X\n", extp->BlkStatusRegMask);
	printk("     - Lock Bit Active:      %s\n", extp->BlkStatusRegMask&1?"yes":"no");
	printk("     - Valid Bit Active:     %s\n", extp->BlkStatusRegMask&2?"yes":"no");
	for (i=2; i<16; i++) {
		if (extp->BlkStatusRegMask & (1<<i))
			printk("     - Unknown Bit %X Active: yes\n",i);
	}

	printk("  Vcc Logic Supply Optimum Program/Erase Voltage: %d.%d V\n",
	       extp->VccOptimal >> 8, extp->VccOptimal & 0xf);
	if (extp->VppOptimal)
		printk("  Vpp Programming Supply Optimum Program/Erase Voltage: %d.%d V\n",
		       extp->VppOptimal >> 8, extp->VppOptimal & 0xf);
}
#endif

/* This routine is made available to other mtd code via
 * inter_module_register.  It must only be accessed through
 * inter_module_get which will bump the use count of this module.  The
 * addresses passed back in cfi are valid as long as the use count of
 * this module is non-zero, i.e. between inter_module_get and
 * inter_module_put.  Keith Owens <kaos@ocs.com.au> 29 Oct 2000.
 */
struct mtd_info *cfi_cmdset_0020(struct map_info *map, int primary)
{
	struct cfi_private *cfi = map->fldrv_priv;
	int i;

	if (cfi->cfi_mode) {
		/*
		 * It's a real CFI chip, not one for which the probe
		 * routine faked a CFI structure. So we read the feature
		 * table from it.
		 */
		__u16 adr = primary?cfi->cfiq->P_ADR:cfi->cfiq->A_ADR;
		struct cfi_pri_intelext *extp;

		extp = (struct cfi_pri_intelext*)cfi_read_pri(map, adr, sizeof(*extp), "ST Microelectronics");
		if (!extp)
			return NULL;

		if (extp->MajorVersion != '1' ||
		    (extp->MinorVersion < '0' || extp->MinorVersion > '3')) {
			printk(KERN_ERR "  Unknown ST Microelectronics"
			       " Extended Query version %c.%c.\n",
			       extp->MajorVersion, extp->MinorVersion);
			kfree(extp);
			return NULL;
		}

		/* Do some byteswapping if necessary */
		extp->FeatureSupport = cfi32_to_cpu(map, extp->FeatureSupport);
		extp->BlkStatusRegMask = cfi32_to_cpu(map,
						extp->BlkStatusRegMask);

#ifdef DEBUG_CFI_FEATURES
		/* Tell the user about it in lots of lovely detail */
		cfi_tell_features(extp);
#endif

		/* Install our own private info structure */
		cfi->cmdset_priv = extp;
	}

	for (i=0; i< cfi->numchips; i++) {
		cfi->chips[i].word_write_time = 128;
		cfi->chips[i].buffer_write_time = 128;
		cfi->chips[i].erase_time = 1024;
		cfi->chips[i].ref_point_counter = 0;
		init_waitqueue_head(&(cfi->chips[i].wq));
	}

	return cfi_staa_setup(map);
}
EXPORT_SYMBOL_GPL(cfi_cmdset_0020);

static struct mtd_info *cfi_staa_setup(struct map_info *map)
{
	struct cfi_private *cfi = map->fldrv_priv;
	struct mtd_info *mtd;
	unsigned long offset = 0;
	int i,j;
	unsigned long devsize = (1<<cfi->cfiq->DevSize) * cfi->interleave;

	mtd = kzalloc(sizeof(*mtd), GFP_KERNEL);
	//printk(KERN_DEBUG "number of CFI chips: %d\n", cfi->numchips);

	if (!mtd) {
		kfree(cfi->cmdset_priv);
		return NULL;
	}

	mtd->priv = map;
	mtd->type = MTD_NORFLASH;
	mtd->size = devsize * cfi->numchips;

	mtd->numeraseregions = cfi->cfiq->NumEraseRegions * cfi->numchips;
	mtd->eraseregions = kmalloc(sizeof(struct mtd_erase_region_info)
			* mtd->numeraseregions, GFP_KERNEL);
	if (!mtd->eraseregions) {
		kfree(cfi->cmdset_priv);
		kfree(mtd);
		return NULL;
	}

	for (i=0; i<cfi->cfiq->NumEraseRegions; i++) {
		unsigned long ernum, ersize;
		ersize = ((cfi->cfiq->EraseRegionInfo[i] >> 8) & ~0xff) * cfi->interleave;
		ernum = (cfi->cfiq->EraseRegionInfo[i] & 0xffff) + 1;

		if (mtd->erasesize < ersize) {
			mtd->erasesize = ersize;
		}
		for (j=0; j<cfi->numchips; j++) {
			mtd->eraseregions[(j*cfi->cfiq->NumEraseRegions)+i].offset = (j*devsize)+offset;
			mtd->eraseregions[(j*cfi->cfiq->NumEraseRegions)+i].erasesize = ersize;
			mtd->eraseregions[(j*cfi->cfiq->NumEraseRegions)+i].numblocks = ernum;
		}
		offset += (ersize * ernum);
	}

	if (offset != devsize) {
		/* Argh */
		printk(KERN_WARNING "Sum of regions (%lx) != total size of set of interleaved chips (%lx)\n", offset, devsize);
		kfree(mtd->eraseregions);
		kfree(cfi->cmdset_priv);
		kfree(mtd);
		return NULL;
	}

	for (i=0; i<mtd->numeraseregions;i++){
		printk(KERN_DEBUG "%d: offset=0x%llx,size=0x%x,blocks=%d\n",
		       i, (unsigned long long)mtd->eraseregions[i].offset,
		       mtd->eraseregions[i].erasesize,
		       mtd->eraseregions[i].numblocks);
	}

	/* Also select the correct geometry setup too */
	mtd->_erase = cfi_staa_erase_varsize;
	mtd->_read = cfi_staa_read;
	mtd->_write = cfi_staa_write_buffers;
	mtd->_writev = cfi_staa_writev;
	mtd->_sync = cfi_staa_sync;
	mtd->_lock = cfi_staa_lock;
	mtd->_unlock = cfi_staa_unlock;
	mtd->_suspend = cfi_staa_suspend;
	mtd->_resume = cfi_staa_resume;
	mtd->flags = MTD_CAP_NORFLASH & ~MTD_BIT_WRITEABLE;
	mtd->writesize = 8; /* FIXME: Should be 0 for STMicro flashes w/out ECC */
	mtd->writebufsize = cfi_interleave(cfi) << cfi->cfiq->MaxBufWriteSize;
	map->fldrv = &cfi_staa_chipdrv;
	__module_get(THIS_MODULE);
	mtd->name = map->name;
	return mtd;
}


static inline int do_read_onechip(struct map_info *map, struct flchip *chip, loff_t adr, size_t len, u_char *buf)
{
	map_word status, status_OK;
	unsigned long timeo;
	DECLARE_WAITQUEUE(wait, current);
	int suspended = 0;
	unsigned long cmd_addr;
	struct cfi_private *cfi = map->fldrv_priv;

	adr += chip->start;

	/* Ensure cmd read/writes are aligned. */
	cmd_addr = adr & ~(map_bankwidth(map)-1);

	/* Let's determine this according to the interleave only once */
	status_OK = CMD(0x80);

	timeo = jiffies + HZ;
 retry:
	mutex_lock(&chip->mutex);

	/* Check that the chip's ready to talk to us.
	 * If it's in FL_ERASING state, suspend it and make it talk now.
	 */
	switch (chip->state) {
	case FL_ERASING:
		if (!(((struct cfi_pri_intelext *)cfi->cmdset_priv)->FeatureSupport & 2))
			goto sleep; /* We don't support erase suspend */

		map_write (map, CMD(0xb0), cmd_addr);
		/* If the flash has finished erasing, then 'erase suspend'
		 * appears to make some (28F320) flash devices switch to
		 * 'read' mode.  Make sure that we switch to 'read status'
		 * mode so we get the right data. --rmk
		 */
		map_write(map, CMD(0x70), cmd_addr);
		chip->oldstate = FL_ERASING;
		chip->state = FL_ERASE_SUSPENDING;
		//		printk("Erase suspending at 0x%lx\n", cmd_addr);
		for (;;) {
			status = map_read(map, cmd_addr);
			if (map_word_andequal(map, status, status_OK, status_OK))
				break;

			if (time_after(jiffies, timeo)) {
				/* Urgh */
				map_write(map, CMD(0xd0), cmd_addr);
				/* make sure we're in 'read status' mode */
				map_write(map, CMD(0x70), cmd_addr);
				chip->state = FL_ERASING;
				wake_up(&chip->wq);
				mutex_unlock(&chip->mutex);
				printk(KERN_ERR "Chip not ready after erase "
				       "suspended: status = 0x%lx\n", status.x[0]);
				return -EIO;
			}

			mutex_unlock(&chip->mutex);
			cfi_udelay(1);
			mutex_lock(&chip->mutex);
		}

		suspended = 1;
		map_write(map, CMD(0xff), cmd_addr);
		chip->state = FL_READY;
		break;

#if 0
	case FL_WRITING:
		/* Not quite yet */
#endif

	case FL_READY:
		break;

	case FL_CFI_QUERY:
	case FL_JEDEC_QUERY:
		map_write(map, CMD(0x70), cmd_addr);
		chip->state = FL_STATUS;

	case FL_STATUS:
		status = map_read(map, cmd_addr);
		if (map_word_andequal(map, status, status_OK, status_OK)) {
			map_write(map, CMD(0xff), cmd_addr);
			chip->state = FL_READY;
			break;
		}

		/* Urgh. Chip not yet ready to talk to us. */
		if (time_after(jiffies, timeo)) {
			mutex_unlock(&chip->mutex);
			printk(KERN_ERR "waiting for chip to be ready timed out in read. WSM status = %lx\n", status.x[0]);
			return -EIO;
		}

		/* Latency issues. Drop the lock, wait a while and retry */
		mutex_unlock(&chip->mutex);
		cfi_udelay(1);
		goto retry;

	default:
	sleep:
		/* Stick ourselves on a wait queue to be woken when
		   someone changes the status */
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&chip->wq, &wait);
		mutex_unlock(&chip->mutex);
		schedule();
		remove_wait_queue(&chip->wq, &wait);
		timeo = jiffies + HZ;
		goto retry;
	}

	map_copy_from(map, buf, adr, len);

	if (suspended) {
		chip->state = chip->oldstate;
		/* What if one interleaved chip has finished and the
		   other hasn't? The old code would leave the finished
		   one in READY mode. That's bad, and caused -EROFS
		   errors to be returned from do_erase_oneblock because
		   that's the only bit it checked for at the time.
		   As the state machine appears to explicitly allow
		   sending the 0x70 (Read Status) command to an erasing
		   chip and expecting it to be ignored, that's what we
		   do. */
		map_write(map, CMD(0xd0), cmd_addr);
		map_write(map, CMD(0x70), cmd_addr);
	}

	wake_up(&chip->wq);
	mutex_unlock(&chip->mutex);
	return 0;
}

static int cfi_staa_read (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	unsigned long ofs;
	int chipnum;
	int ret = 0;

	/* ofs: offset within the first chip that the first read should start */
	chipnum = (from >> cfi->chipshift);
	ofs = from - (chipnum <<  cfi->chipshift);

	while (len) {
		unsigned long thislen;

		if (chipnum >= cfi->numchips)
			break;

		if ((len + ofs -1) >> cfi->chipshift)
			thislen = (1<<cfi->chipshift) - ofs;
		else
			thislen = len;

		ret = do_read_onechip(map, &cfi->chips[chipnum], ofs, thislen, buf);
		if (ret)
			break;

		*retlen += thislen;
		len -= thislen;
		buf += thislen;

		ofs = 0;
		chipnum++;
	}
	return ret;
}

static int do_write_buffer(struct map_info *map, struct flchip *chip,
				  unsigned long adr, const u_char *buf, int len)
{
	struct cfi_private *cfi = map->fldrv_priv;
	map_word status, status_OK;
	unsigned long cmd_adr, timeo;
	DECLARE_WAITQUEUE(wait, current);
	int wbufsize, z;

        /* M58LW064A requires bus alignment for buffer wriets -- saw */
        if (adr & (map_bankwidth(map)-1))
            return -EINVAL;

        wbufsize = cfi_interleave(cfi) << cfi->cfiq->MaxBufWriteSize;
        adr += chip->start;
	cmd_adr = adr & ~(wbufsize-1);

	/* Let's determine this according to the interleave only once */
        status_OK = CMD(0x80);

	timeo = jiffies + HZ;
 retry:

#ifdef DEBUG_CFI_FEATURES
       printk("%s: chip->state[%d]\n", __func__, chip->state);
#endif
	mutex_lock(&chip->mutex);

	/* Check that the chip's ready to talk to us.
	 * Later, we can actually think about interrupting it
	 * if it's in FL_ERASING state.
	 * Not just yet, though.
	 */
	switch (chip->state) {
	case FL_READY:
		break;

	case FL_CFI_QUERY:
	case FL_JEDEC_QUERY:
		map_write(map, CMD(0x70), cmd_adr);
                chip->state = FL_STATUS;
#ifdef DEBUG_CFI_FEATURES
	printk("%s: 1 status[%x]\n", __func__, map_read(map, cmd_adr));
#endif

	case FL_STATUS:
		status = map_read(map, cmd_adr);
		if (map_word_andequal(map, status, status_OK, status_OK))
			break;
		/* Urgh. Chip not yet ready to talk to us. */
		if (time_after(jiffies, timeo)) {
			mutex_unlock(&chip->mutex);
                        printk(KERN_ERR "waiting for chip to be ready timed out in buffer write Xstatus = %lx, status = %lx\n",
                               status.x[0], map_read(map, cmd_adr).x[0]);
			return -EIO;
		}

		/* Latency issues. Drop the lock, wait a while and retry */
		mutex_unlock(&chip->mutex);
		cfi_udelay(1);
		goto retry;

	default:
		/* Stick ourselves on a wait queue to be woken when
		   someone changes the status */
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&chip->wq, &wait);
		mutex_unlock(&chip->mutex);
		schedule();
		remove_wait_queue(&chip->wq, &wait);
		timeo = jiffies + HZ;
		goto retry;
	}

	ENABLE_VPP(map);
	map_write(map, CMD(0xe8), cmd_adr);
	chip->state = FL_WRITING_TO_BUFFER;

	z = 0;
	for (;;) {
		status = map_read(map, cmd_adr);
		if (map_word_andequal(map, status, status_OK, status_OK))
			break;

		mutex_unlock(&chip->mutex);
		cfi_udelay(1);
		mutex_lock(&chip->mutex);

		if (++z > 100) {
			/* Argh. Not ready for write to buffer */
			DISABLE_VPP(map);
                        map_write(map, CMD(0x70), cmd_adr);
			chip->state = FL_STATUS;
			mutex_unlock(&chip->mutex);
			printk(KERN_ERR "Chip not ready for buffer write. Xstatus = %lx\n", status.x[0]);
			return -EIO;
		}
	}

	/* Write length of data to come */
	map_write(map, CMD(len/map_bankwidth(map)-1), cmd_adr );

	/* Write data */
	for (z = 0; z < len;
	     z += map_bankwidth(map), buf += map_bankwidth(map)) {
		map_word d;
		d = map_word_load(map, buf);
		map_write(map, d, adr+z);
	}
	/* GO GO GO */
	map_write(map, CMD(0xd0), cmd_adr);
	chip->state = FL_WRITING;

	mutex_unlock(&chip->mutex);
	cfi_udelay(chip->buffer_write_time);
	mutex_lock(&chip->mutex);

	timeo = jiffies + (HZ/2);
	z = 0;
	for (;;) {
		if (chip->state != FL_WRITING) {
			/* Someone's suspended the write. Sleep */
			set_current_state(TASK_UNINTERRUPTIBLE);
			add_wait_queue(&chip->wq, &wait);
			mutex_unlock(&chip->mutex);
			schedule();
			remove_wait_queue(&chip->wq, &wait);
			timeo = jiffies + (HZ / 2); /* FIXME */
			mutex_lock(&chip->mutex);
			continue;
		}

		status = map_read(map, cmd_adr);
		if (map_word_andequal(map, status, status_OK, status_OK))
			break;

		/* OK Still waiting */
		if (time_after(jiffies, timeo)) {
                        /* clear status */
                        map_write(map, CMD(0x50), cmd_adr);
                        /* put back into read status register mode */
                        map_write(map, CMD(0x70), adr);
			chip->state = FL_STATUS;
			DISABLE_VPP(map);
			mutex_unlock(&chip->mutex);
			printk(KERN_ERR "waiting for chip to be ready timed out in bufwrite\n");
			return -EIO;
		}

		/* Latency issues. Drop the lock, wait a while and retry */
		mutex_unlock(&chip->mutex);
		cfi_udelay(1);
		z++;
		mutex_lock(&chip->mutex);
	}
	if (!z) {
		chip->buffer_write_time--;
		if (!chip->buffer_write_time)
			chip->buffer_write_time++;
	}
	if (z > 1)
		chip->buffer_write_time++;

	/* Done and happy. */
	DISABLE_VPP(map);
	chip->state = FL_STATUS;

        /* check for errors: 'lock bit', 'VPP', 'dead cell'/'unerased cell' or 'incorrect cmd' -- saw */
        if (map_word_bitsset(map, status, CMD(0x3a))) {
#ifdef DEBUG_CFI_FEATURES
		printk("%s: 2 status[%lx]\n", __func__, status.x[0]);
#endif
		/* clear status */
		map_write(map, CMD(0x50), cmd_adr);
		/* put back into read status register mode */
		map_write(map, CMD(0x70), adr);
		wake_up(&chip->wq);
		mutex_unlock(&chip->mutex);
		return map_word_bitsset(map, status, CMD(0x02)) ? -EROFS : -EIO;
	}
	wake_up(&chip->wq);
	mutex_unlock(&chip->mutex);

        return 0;
}

static int cfi_staa_write_buffers (struct mtd_info *mtd, loff_t to,
				       size_t len, size_t *retlen, const u_char *buf)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	int wbufsize = cfi_interleave(cfi) << cfi->cfiq->MaxBufWriteSize;
	int ret = 0;
	int chipnum;
	unsigned long ofs;

	chipnum = to >> cfi->chipshift;
	ofs = to  - (chipnum << cfi->chipshift);

#ifdef DEBUG_CFI_FEATURES
	printk("%s: map_bankwidth(map)[%x]\n", __func__, map_bankwidth(map));
	printk("%s: chipnum[%x] wbufsize[%x]\n", __func__, chipnum, wbufsize);
	printk("%s: ofs[%x] len[%x]\n", __func__, ofs, len);
#endif

        /* Write buffer is worth it only if more than one word to write... */
        while (len > 0) {
		/* We must not cross write block boundaries */
		int size = wbufsize - (ofs & (wbufsize-1));

                if (size > len)
                    size = len;

                ret = do_write_buffer(map, &cfi->chips[chipnum],
				      ofs, buf, size);
		if (ret)
			return ret;

		ofs += size;
		buf += size;
		(*retlen) += size;
		len -= size;

		if (ofs >> cfi->chipshift) {
			chipnum ++;
			ofs = 0;
			if (chipnum == cfi->numchips)
				return 0;
		}
	}

	return 0;
}

/*
 * Writev for ECC-Flashes is a little more complicated. We need to maintain
 * a small buffer for this.
 * XXX: If the buffer size is not a multiple of 2, this will break
 */
#define ECCBUF_SIZE (mtd->writesize)
#define ECCBUF_DIV(x) ((x) & ~(ECCBUF_SIZE - 1))
#define ECCBUF_MOD(x) ((x) &  (ECCBUF_SIZE - 1))
static int
cfi_staa_writev(struct mtd_info *mtd, const struct kvec *vecs,
		unsigned long count, loff_t to, size_t *retlen)
{
	unsigned long i;
	size_t	 totlen = 0, thislen;
	int	 ret = 0;
	size_t	 buflen = 0;
	char *buffer;

	if (!ECCBUF_SIZE) {
		/* We should fall back to a general writev implementation.
		 * Until that is written, just break.
		 */
		return -EIO;
	}
	buffer = kmalloc(ECCBUF_SIZE, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	for (i=0; i<count; i++) {
		size_t elem_len = vecs[i].iov_len;
		void *elem_base = vecs[i].iov_base;
		if (!elem_len) /* FIXME: Might be unnecessary. Check that */
			continue;
		if (buflen) { /* cut off head */
			if (buflen + elem_len < ECCBUF_SIZE) { /* just accumulate */
				memcpy(buffer+buflen, elem_base, elem_len);
				buflen += elem_len;
				continue;
			}
			memcpy(buffer+buflen, elem_base, ECCBUF_SIZE-buflen);
			ret = mtd_write(mtd, to, ECCBUF_SIZE, &thislen,
					buffer);
			totlen += thislen;
			if (ret || thislen != ECCBUF_SIZE)
				goto write_error;
			elem_len -= thislen-buflen;
			elem_base += thislen-buflen;
			to += ECCBUF_SIZE;
		}
		if (ECCBUF_DIV(elem_len)) { /* write clean aligned data */
			ret = mtd_write(mtd, to, ECCBUF_DIV(elem_len),
					&thislen, elem_base);
			totlen += thislen;
			if (ret || thislen != ECCBUF_DIV(elem_len))
				goto write_error;
			to += thislen;
		}
		buflen = ECCBUF_MOD(elem_len); /* cut off tail */
		if (buflen) {
			memset(buffer, 0xff, ECCBUF_SIZE);
			memcpy(buffer, elem_base + thislen, buflen);
		}
	}
	if (buflen) { /* flush last page, even if not full */
		/* This is sometimes intended behaviour, really */
		ret = mtd_write(mtd, to, buflen, &thislen, buffer);
		totlen += thislen;
		if (ret || thislen != ECCBUF_SIZE)
			goto write_error;
	}
write_error:
	if (retlen)
		*retlen = totlen;
	kfree(buffer);
	return ret;
}


static inline int do_erase_oneblock(struct map_info *map, struct flchip *chip, unsigned long adr)
{
	struct cfi_private *cfi = map->fldrv_priv;
	map_word status, status_OK;
	unsigned long timeo;
	int retries = 3;
	DECLARE_WAITQUEUE(wait, current);
	int ret = 0;

	adr += chip->start;

	/* Let's determine this according to the interleave only once */
	status_OK = CMD(0x80);

	timeo = jiffies + HZ;
retry:
	mutex_lock(&chip->mutex);

	/* Check that the chip's ready to talk to us. */
	switch (chip->state) {
	case FL_CFI_QUERY:
	case FL_JEDEC_QUERY:
	case FL_READY:
		map_write(map, CMD(0x70), adr);
		chip->state = FL_STATUS;

	case FL_STATUS:
		status = map_read(map, adr);
		if (map_word_andequal(map, status, status_OK, status_OK))
			break;

		/* Urgh. Chip not yet ready to talk to us. */
		if (time_after(jiffies, timeo)) {
			mutex_unlock(&chip->mutex);
			printk(KERN_ERR "waiting for chip to be ready timed out in erase\n");
			return -EIO;
		}

		/* Latency issues. Drop the lock, wait a while and retry */
		mutex_unlock(&chip->mutex);
		cfi_udelay(1);
		goto retry;

	default:
		/* Stick ourselves on a wait queue to be woken when
		   someone changes the status */
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&chip->wq, &wait);
		mutex_unlock(&chip->mutex);
		schedule();
		remove_wait_queue(&chip->wq, &wait);
		timeo = jiffies + HZ;
		goto retry;
	}

	ENABLE_VPP(map);
	/* Clear the status register first */
	map_write(map, CMD(0x50), adr);

	/* Now erase */
	map_write(map, CMD(0x20), adr);
	map_write(map, CMD(0xD0), adr);
	chip->state = FL_ERASING;

	mutex_unlock(&chip->mutex);
	msleep(1000);
	mutex_lock(&chip->mutex);

	/* FIXME. Use a timer to check this, and return immediately. */
	/* Once the state machine's known to be working I'll do that */

	timeo = jiffies + (HZ*20);
	for (;;) {
		if (chip->state != FL_ERASING) {
			/* Someone's suspended the erase. Sleep */
			set_current_state(TASK_UNINTERRUPTIBLE);
			add_wait_queue(&chip->wq, &wait);
			mutex_unlock(&chip->mutex);
			schedule();
			remove_wait_queue(&chip->wq, &wait);
			timeo = jiffies + (HZ*20); /* FIXME */
			mutex_lock(&chip->mutex);
			continue;
		}

		status = map_read(map, adr);
		if (map_word_andequal(map, status, status_OK, status_OK))
			break;

		/* OK Still waiting */
		if (time_after(jiffies, timeo)) {
			map_write(map, CMD(0x70), adr);
			chip->state = FL_STATUS;
			printk(KERN_ERR "waiting for erase to complete timed out. Xstatus = %lx, status = %lx.\n", status.x[0], map_read(map, adr).x[0]);
			DISABLE_VPP(map);
			mutex_unlock(&chip->mutex);
			return -EIO;
		}

		/* Latency issues. Drop the lock, wait a while and retry */
		mutex_unlock(&chip->mutex);
		cfi_udelay(1);
		mutex_lock(&chip->mutex);
	}

	DISABLE_VPP(map);
	ret = 0;

	/* We've broken this before. It doesn't hurt to be safe */
	map_write(map, CMD(0x70), adr);
	chip->state = FL_STATUS;
	status = map_read(map, adr);

	/* check for lock bit */
	if (map_word_bitsset(map, status, CMD(0x3a))) {
		unsigned char chipstatus = status.x[0];
		if (!map_word_equal(map, status, CMD(chipstatus))) {
			int i, w;
			for (w=0; w<map_words(map); w++) {
				for (i = 0; i<cfi_interleave(cfi); i++) {
					chipstatus |= status.x[w] >> (cfi->device_type * 8);
				}
			}
			printk(KERN_WARNING "Status is not identical for all chips: 0x%lx. Merging to give 0x%02x\n",
			       status.x[0], chipstatus);
		}
		/* Reset the error bits */
		map_write(map, CMD(0x50), adr);
		map_write(map, CMD(0x70), adr);

		if ((chipstatus & 0x30) == 0x30) {
			printk(KERN_NOTICE "Chip reports improper command sequence: status 0x%x\n", chipstatus);
			ret = -EIO;
		} else if (chipstatus & 0x02) {
			/* Protection bit set */
			ret = -EROFS;
		} else if (chipstatus & 0x8) {
			/* Voltage */
			printk(KERN_WARNING "Chip reports voltage low on erase: status 0x%x\n", chipstatus);
			ret = -EIO;
		} else if (chipstatus & 0x20) {
			if (retries--) {
				printk(KERN_DEBUG "Chip erase failed at 0x%08lx: status 0x%x. Retrying...\n", adr, chipstatus);
				timeo = jiffies + HZ;
				chip->state = FL_STATUS;
				mutex_unlock(&chip->mutex);
				goto retry;
			}
			printk(KERN_DEBUG "Chip erase failed at 0x%08lx: status 0x%x\n", adr, chipstatus);
			ret = -EIO;
		}
	}

	wake_up(&chip->wq);
	mutex_unlock(&chip->mutex);
	return ret;
}

static int cfi_staa_erase_varsize(struct mtd_info *mtd,
				  struct erase_info *instr)
{	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	unsigned long adr, len;
	int chipnum, ret = 0;
	int i, first;
	struct mtd_erase_region_info *regions = mtd->eraseregions;

	/* Check that both start and end of the requested erase are
	 * aligned with the erasesize at the appropriate addresses.
	 */

	i = 0;

	/* Skip all erase regions which are ended before the start of
	   the requested erase. Actually, to save on the calculations,
	   we skip to the first erase region which starts after the
	   start of the requested erase, and then go back one.
	*/

	while (i < mtd->numeraseregions && instr->addr >= regions[i].offset)
	       i++;
	i--;

	/* OK, now i is pointing at the erase region in which this
	   erase request starts. Check the start of the requested
	   erase range is aligned with the erase size which is in
	   effect here.
	*/

	if (instr->addr & (regions[i].erasesize-1))
		return -EINVAL;

	/* Remember the erase region we start on */
	first = i;

	/* Next, check that the end of the requested erase is aligned
	 * with the erase region at that address.
	 */

	while (i<mtd->numeraseregions && (instr->addr + instr->len) >= regions[i].offset)
		i++;

	/* As before, drop back one to point at the region in which
	   the address actually falls
	*/
	i--;

	if ((instr->addr + instr->len) & (regions[i].erasesize-1))
		return -EINVAL;

	chipnum = instr->addr >> cfi->chipshift;
	adr = instr->addr - (chipnum << cfi->chipshift);
	len = instr->len;

	i=first;

	while(len) {
		ret = do_erase_oneblock(map, &cfi->chips[chipnum], adr);

		if (ret)
			return ret;

		adr += regions[i].erasesize;
		len -= regions[i].erasesize;

		if (adr % (1<< cfi->chipshift) == (((unsigned long)regions[i].offset + (regions[i].erasesize * regions[i].numblocks)) %( 1<< cfi->chipshift)))
			i++;

		if (adr >> cfi->chipshift) {
			adr = 0;
			chipnum++;

			if (chipnum >= cfi->numchips)
				break;
		}
	}

	return 0;
}

static void cfi_staa_sync (struct mtd_info *mtd)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	int i;
	struct flchip *chip;
	int ret = 0;
	DECLARE_WAITQUEUE(wait, current);

	for (i=0; !ret && i<cfi->numchips; i++) {
		chip = &cfi->chips[i];

	retry:
		mutex_lock(&chip->mutex);

		switch(chip->state) {
		case FL_READY:
		case FL_STATUS:
		case FL_CFI_QUERY:
		case FL_JEDEC_QUERY:
			chip->oldstate = chip->state;
			chip->state = FL_SYNCING;
			/* No need to wake_up() on this state change -
			 * as the whole point is that nobody can do anything
			 * with the chip now anyway.
			 */
		case FL_SYNCING:
			mutex_unlock(&chip->mutex);
			break;

		default:
			/* Not an idle state */
			set_current_state(TASK_UNINTERRUPTIBLE);
			add_wait_queue(&chip->wq, &wait);

			mutex_unlock(&chip->mutex);
			schedule();
		        remove_wait_queue(&chip->wq, &wait);

			goto retry;
		}
	}

	/* Unlock the chips again */

	for (i--; i >=0; i--) {
		chip = &cfi->chips[i];

		mutex_lock(&chip->mutex);

		if (chip->state == FL_SYNCING) {
			chip->state = chip->oldstate;
			wake_up(&chip->wq);
		}
		mutex_unlock(&chip->mutex);
	}
}

static inline int do_lock_oneblock(struct map_info *map, struct flchip *chip, unsigned long adr)
{
	struct cfi_private *cfi = map->fldrv_priv;
	map_word status, status_OK;
	unsigned long timeo = jiffies + HZ;
	DECLARE_WAITQUEUE(wait, current);

	adr += chip->start;

	/* Let's determine this according to the interleave only once */
	status_OK = CMD(0x80);

	timeo = jiffies + HZ;
retry:
	mutex_lock(&chip->mutex);

	/* Check that the chip's ready to talk to us. */
	switch (chip->state) {
	case FL_CFI_QUERY:
	case FL_JEDEC_QUERY:
	case FL_READY:
		map_write(map, CMD(0x70), adr);
		chip->state = FL_STATUS;

	case FL_STATUS:
		status = map_read(map, adr);
		if (map_word_andequal(map, status, status_OK, status_OK))
			break;

		/* Urgh. Chip not yet ready to talk to us. */
		if (time_after(jiffies, timeo)) {
			mutex_unlock(&chip->mutex);
			printk(KERN_ERR "waiting for chip to be ready timed out in lock\n");
			return -EIO;
		}

		/* Latency issues. Drop the lock, wait a while and retry */
		mutex_unlock(&chip->mutex);
		cfi_udelay(1);
		goto retry;

	default:
		/* Stick ourselves on a wait queue to be woken when
		   someone changes the status */
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&chip->wq, &wait);
		mutex_unlock(&chip->mutex);
		schedule();
		remove_wait_queue(&chip->wq, &wait);
		timeo = jiffies + HZ;
		goto retry;
	}

	ENABLE_VPP(map);
	map_write(map, CMD(0x60), adr);
	map_write(map, CMD(0x01), adr);
	chip->state = FL_LOCKING;

	mutex_unlock(&chip->mutex);
	msleep(1000);
	mutex_lock(&chip->mutex);

	/* FIXME. Use a timer to check this, and return immediately. */
	/* Once the state machine's known to be working I'll do that */

	timeo = jiffies + (HZ*2);
	for (;;) {

		status = map_read(map, adr);
		if (map_word_andequal(map, status, status_OK, status_OK))
			break;

		/* OK Still waiting */
		if (time_after(jiffies, timeo)) {
			map_write(map, CMD(0x70), adr);
			chip->state = FL_STATUS;
			printk(KERN_ERR "waiting for lock to complete timed out. Xstatus = %lx, status = %lx.\n", status.x[0], map_read(map, adr).x[0]);
			DISABLE_VPP(map);
			mutex_unlock(&chip->mutex);
			return -EIO;
		}

		/* Latency issues. Drop the lock, wait a while and retry */
		mutex_unlock(&chip->mutex);
		cfi_udelay(1);
		mutex_lock(&chip->mutex);
	}

	/* Done and happy. */
	chip->state = FL_STATUS;
	DISABLE_VPP(map);
	wake_up(&chip->wq);
	mutex_unlock(&chip->mutex);
	return 0;
}
static int cfi_staa_lock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	unsigned long adr;
	int chipnum, ret = 0;
#ifdef DEBUG_LOCK_BITS
	int ofs_factor = cfi->interleave * cfi->device_type;
#endif

	if (ofs & (mtd->erasesize - 1))
		return -EINVAL;

	if (len & (mtd->erasesize -1))
		return -EINVAL;

	chipnum = ofs >> cfi->chipshift;
	adr = ofs - (chipnum << cfi->chipshift);

	while(len) {

#ifdef DEBUG_LOCK_BITS
		cfi_send_gen_cmd(0x90, 0x55, 0, map, cfi, cfi->device_type, NULL);
		printk("before lock: block status register is %x\n",cfi_read_query(map, adr+(2*ofs_factor)));
		cfi_send_gen_cmd(0xff, 0x55, 0, map, cfi, cfi->device_type, NULL);
#endif

		ret = do_lock_oneblock(map, &cfi->chips[chipnum], adr);

#ifdef DEBUG_LOCK_BITS
		cfi_send_gen_cmd(0x90, 0x55, 0, map, cfi, cfi->device_type, NULL);
		printk("after lock: block status register is %x\n",cfi_read_query(map, adr+(2*ofs_factor)));
		cfi_send_gen_cmd(0xff, 0x55, 0, map, cfi, cfi->device_type, NULL);
#endif

		if (ret)
			return ret;

		adr += mtd->erasesize;
		len -= mtd->erasesize;

		if (adr >> cfi->chipshift) {
			adr = 0;
			chipnum++;

			if (chipnum >= cfi->numchips)
				break;
		}
	}
	return 0;
}
static inline int do_unlock_oneblock(struct map_info *map, struct flchip *chip, unsigned long adr)
{
	struct cfi_private *cfi = map->fldrv_priv;
	map_word status, status_OK;
	unsigned long timeo = jiffies + HZ;
	DECLARE_WAITQUEUE(wait, current);

	adr += chip->start;

	/* Let's determine this according to the interleave only once */
	status_OK = CMD(0x80);

	timeo = jiffies + HZ;
retry:
	mutex_lock(&chip->mutex);

	/* Check that the chip's ready to talk to us. */
	switch (chip->state) {
	case FL_CFI_QUERY:
	case FL_JEDEC_QUERY:
	case FL_READY:
		map_write(map, CMD(0x70), adr);
		chip->state = FL_STATUS;

	case FL_STATUS:
		status = map_read(map, adr);
		if (map_word_andequal(map, status, status_OK, status_OK))
			break;

		/* Urgh. Chip not yet ready to talk to us. */
		if (time_after(jiffies, timeo)) {
			mutex_unlock(&chip->mutex);
			printk(KERN_ERR "waiting for chip to be ready timed out in unlock\n");
			return -EIO;
		}

		/* Latency issues. Drop the lock, wait a while and retry */
		mutex_unlock(&chip->mutex);
		cfi_udelay(1);
		goto retry;

	default:
		/* Stick ourselves on a wait queue to be woken when
		   someone changes the status */
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&chip->wq, &wait);
		mutex_unlock(&chip->mutex);
		schedule();
		remove_wait_queue(&chip->wq, &wait);
		timeo = jiffies + HZ;
		goto retry;
	}

	ENABLE_VPP(map);
	map_write(map, CMD(0x60), adr);
	map_write(map, CMD(0xD0), adr);
	chip->state = FL_UNLOCKING;

	mutex_unlock(&chip->mutex);
	msleep(1000);
	mutex_lock(&chip->mutex);

	/* FIXME. Use a timer to check this, and return immediately. */
	/* Once the state machine's known to be working I'll do that */

	timeo = jiffies + (HZ*2);
	for (;;) {

		status = map_read(map, adr);
		if (map_word_andequal(map, status, status_OK, status_OK))
			break;

		/* OK Still waiting */
		if (time_after(jiffies, timeo)) {
			map_write(map, CMD(0x70), adr);
			chip->state = FL_STATUS;
			printk(KERN_ERR "waiting for unlock to complete timed out. Xstatus = %lx, status = %lx.\n", status.x[0], map_read(map, adr).x[0]);
			DISABLE_VPP(map);
			mutex_unlock(&chip->mutex);
			return -EIO;
		}

		/* Latency issues. Drop the unlock, wait a while and retry */
		mutex_unlock(&chip->mutex);
		cfi_udelay(1);
		mutex_lock(&chip->mutex);
	}

	/* Done and happy. */
	chip->state = FL_STATUS;
	DISABLE_VPP(map);
	wake_up(&chip->wq);
	mutex_unlock(&chip->mutex);
	return 0;
}
static int cfi_staa_unlock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	unsigned long adr;
	int chipnum, ret = 0;
#ifdef DEBUG_LOCK_BITS
	int ofs_factor = cfi->interleave * cfi->device_type;
#endif

	chipnum = ofs >> cfi->chipshift;
	adr = ofs - (chipnum << cfi->chipshift);

#ifdef DEBUG_LOCK_BITS
	{
		unsigned long temp_adr = adr;
		unsigned long temp_len = len;

		cfi_send_gen_cmd(0x90, 0x55, 0, map, cfi, cfi->device_type, NULL);
                while (temp_len) {
			printk("before unlock %x: block status register is %x\n",temp_adr,cfi_read_query(map, temp_adr+(2*ofs_factor)));
			temp_adr += mtd->erasesize;
			temp_len -= mtd->erasesize;
		}
		cfi_send_gen_cmd(0xff, 0x55, 0, map, cfi, cfi->device_type, NULL);
	}
#endif

	ret = do_unlock_oneblock(map, &cfi->chips[chipnum], adr);

#ifdef DEBUG_LOCK_BITS
	cfi_send_gen_cmd(0x90, 0x55, 0, map, cfi, cfi->device_type, NULL);
	printk("after unlock: block status register is %x\n",cfi_read_query(map, adr+(2*ofs_factor)));
	cfi_send_gen_cmd(0xff, 0x55, 0, map, cfi, cfi->device_type, NULL);
#endif

	return ret;
}

static int cfi_staa_suspend(struct mtd_info *mtd)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	int i;
	struct flchip *chip;
	int ret = 0;

	for (i=0; !ret && i<cfi->numchips; i++) {
		chip = &cfi->chips[i];

		mutex_lock(&chip->mutex);

		switch(chip->state) {
		case FL_READY:
		case FL_STATUS:
		case FL_CFI_QUERY:
		case FL_JEDEC_QUERY:
			chip->oldstate = chip->state;
			chip->state = FL_PM_SUSPENDED;
			/* No need to wake_up() on this state change -
			 * as the whole point is that nobody can do anything
			 * with the chip now anyway.
			 */
		case FL_PM_SUSPENDED:
			break;

		default:
			ret = -EAGAIN;
			break;
		}
		mutex_unlock(&chip->mutex);
	}

	/* Unlock the chips again */

	if (ret) {
		for (i--; i >=0; i--) {
			chip = &cfi->chips[i];

			mutex_lock(&chip->mutex);

			if (chip->state == FL_PM_SUSPENDED) {
				/* No need to force it into a known state here,
				   because we're returning failure, and it didn't
				   get power cycled */
				chip->state = chip->oldstate;
				wake_up(&chip->wq);
			}
			mutex_unlock(&chip->mutex);
		}
	}

	return ret;
}

static void cfi_staa_resume(struct mtd_info *mtd)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	int i;
	struct flchip *chip;

	for (i=0; i<cfi->numchips; i++) {

		chip = &cfi->chips[i];

		mutex_lock(&chip->mutex);

		/* Go to known state. Chip may have been power cycled */
		if (chip->state == FL_PM_SUSPENDED) {
			map_write(map, CMD(0xFF), 0);
			chip->state = FL_READY;
			wake_up(&chip->wq);
		}

		mutex_unlock(&chip->mutex);
	}
}

static void cfi_staa_destroy(struct mtd_info *mtd)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	kfree(cfi->cmdset_priv);
	kfree(cfi);
}

MODULE_LICENSE("GPL");
