/*****************************************************************************
* Copyright 2004 - 2009 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

/* ---- Include Files ---------------------------------------------------- */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <asm/mach-types.h>
#include <asm/system.h>

#include <mach/reg_nand.h>
#include <mach/reg_umi.h>

#include "nand_bcm_umi.h"

#include <mach/memory_settings.h>

#define USE_DMA 1
#include <mach/dma.h>
#include <linux/dma-mapping.h>
#include <linux/completion.h>

/* ---- External Variable Declarations ----------------------------------- */
/* ---- External Function Prototypes ------------------------------------- */
/* ---- Public Variables ------------------------------------------------- */
/* ---- Private Constants and Types -------------------------------------- */
static const __devinitconst char gBanner[] = KERN_INFO \
	"BCM UMI MTD NAND Driver: 1.00\n";

#ifdef CONFIG_MTD_PARTITIONS
const char *part_probes[] = { "cmdlinepart", NULL };
#endif

#if NAND_ECC_BCH
static uint8_t scan_ff_pattern[] = { 0xff };

static struct nand_bbt_descr largepage_bbt = {
	.options = 0,
	.offs = 0,
	.len = 1,
	.pattern = scan_ff_pattern
};
#endif

/*
** Preallocate a buffer to avoid having to do this every dma operation.
** This is the size of the preallocated coherent DMA buffer.
*/
#if USE_DMA
#define DMA_MIN_BUFLEN	512
#define DMA_MAX_BUFLEN	PAGE_SIZE
#define USE_DIRECT_IO(len)	(((len) < DMA_MIN_BUFLEN) || \
	((len) > DMA_MAX_BUFLEN))

/*
 * The current NAND data space goes from 0x80001900 to 0x80001FFF,
 * which is only 0x700 = 1792 bytes long. This is too small for 2K, 4K page
 * size NAND flash. Need to break the DMA down to multiple 1Ks.
 *
 * Need to make sure REG_NAND_DATA_PADDR + DMA_MAX_LEN < 0x80002000
 */
#define DMA_MAX_LEN             1024

#else /* !USE_DMA */
#define DMA_MIN_BUFLEN          0
#define DMA_MAX_BUFLEN          0
#define USE_DIRECT_IO(len)      1
#endif
/* ---- Private Function Prototypes -------------------------------------- */
static void bcm_umi_nand_read_buf(struct mtd_info *mtd, u_char * buf, int len);
static void bcm_umi_nand_write_buf(struct mtd_info *mtd, const u_char * buf,
				   int len);

/* ---- Private Variables ------------------------------------------------ */
static struct mtd_info *board_mtd;
static void __iomem *bcm_umi_io_base;
static void *virtPtr;
static dma_addr_t physPtr;
static struct completion nand_comp;

/* ---- Private Functions ------------------------------------------------ */
#if NAND_ECC_BCH
#include "bcm_umi_bch.c"
#else
#include "bcm_umi_hamming.c"
#endif

#if USE_DMA

/* Handler called when the DMA finishes. */
static void nand_dma_handler(DMA_Device_t dev, int reason, void *userData)
{
	complete(&nand_comp);
}

static int nand_dma_init(void)
{
	int rc;

	rc = dma_set_device_handler(DMA_DEVICE_NAND_MEM_TO_MEM,
		nand_dma_handler, NULL);
	if (rc != 0) {
		printk(KERN_ERR "dma_set_device_handler failed: %d\n", rc);
		return rc;
	}

	virtPtr =
	    dma_alloc_coherent(NULL, DMA_MAX_BUFLEN, &physPtr, GFP_KERNEL);
	if (virtPtr == NULL) {
		printk(KERN_ERR "NAND - Failed to allocate memory for DMA buffer\n");
		return -ENOMEM;
	}

	return 0;
}

static void nand_dma_term(void)
{
	if (virtPtr != NULL)
		dma_free_coherent(NULL, DMA_MAX_BUFLEN, virtPtr, physPtr);
}

static void nand_dma_read(void *buf, int len)
{
	int offset = 0;
	int tmp_len = 0;
	int len_left = len;
	DMA_Handle_t hndl;

	if (virtPtr == NULL)
		panic("nand_dma_read: virtPtr == NULL\n");

	if ((void *)physPtr == NULL)
		panic("nand_dma_read: physPtr == NULL\n");

	hndl = dma_request_channel(DMA_DEVICE_NAND_MEM_TO_MEM);
	if (hndl < 0) {
		printk(KERN_ERR
		       "nand_dma_read: unable to allocate dma channel: %d\n",
		       (int)hndl);
		panic("\n");
	}

	while (len_left > 0) {
		if (len_left > DMA_MAX_LEN) {
			tmp_len = DMA_MAX_LEN;
			len_left -= DMA_MAX_LEN;
		} else {
			tmp_len = len_left;
			len_left = 0;
		}

		init_completion(&nand_comp);
		dma_transfer_mem_to_mem(hndl, REG_NAND_DATA_PADDR,
					physPtr + offset, tmp_len);
		wait_for_completion(&nand_comp);

		offset += tmp_len;
	}

	dma_free_channel(hndl);

	if (buf != NULL)
		memcpy(buf, virtPtr, len);
}

static void nand_dma_write(const void *buf, int len)
{
	int offset = 0;
	int tmp_len = 0;
	int len_left = len;
	DMA_Handle_t hndl;

	if (buf == NULL)
		panic("nand_dma_write: buf == NULL\n");

	if (virtPtr == NULL)
		panic("nand_dma_write: virtPtr == NULL\n");

	if ((void *)physPtr == NULL)
		panic("nand_dma_write: physPtr == NULL\n");

	memcpy(virtPtr, buf, len);


	hndl = dma_request_channel(DMA_DEVICE_NAND_MEM_TO_MEM);
	if (hndl < 0) {
		printk(KERN_ERR
		       "nand_dma_write: unable to allocate dma channel: %d\n",
		       (int)hndl);
		panic("\n");
	}

	while (len_left > 0) {
		if (len_left > DMA_MAX_LEN) {
			tmp_len = DMA_MAX_LEN;
			len_left -= DMA_MAX_LEN;
		} else {
			tmp_len = len_left;
			len_left = 0;
		}

		init_completion(&nand_comp);
		dma_transfer_mem_to_mem(hndl, physPtr + offset,
					REG_NAND_DATA_PADDR, tmp_len);
		wait_for_completion(&nand_comp);

		offset += tmp_len;
	}

	dma_free_channel(hndl);
}

#endif

static int nand_dev_ready(struct mtd_info *mtd)
{
	return nand_bcm_umi_dev_ready();
}

/****************************************************************************
*
*  bcm_umi_nand_inithw
*
*   This routine does the necessary hardware (board-specific)
*   initializations.  This includes setting up the timings, etc.
*
***************************************************************************/
int bcm_umi_nand_inithw(void)
{
	/* Configure nand timing parameters */
	REG_UMI_NAND_TCR &= ~0x7ffff;
	REG_UMI_NAND_TCR |= HW_CFG_NAND_TCR;

#if !defined(CONFIG_MTD_NAND_BCM_UMI_HWCS)
	/* enable software control of CS */
	REG_UMI_NAND_TCR |= REG_UMI_NAND_TCR_CS_SWCTRL;
#endif

	/* keep NAND chip select asserted */
	REG_UMI_NAND_RCSR |= REG_UMI_NAND_RCSR_CS_ASSERTED;

	REG_UMI_NAND_TCR &= ~REG_UMI_NAND_TCR_WORD16;
	/* enable writes to flash */
	REG_UMI_MMD_ICR |= REG_UMI_MMD_ICR_FLASH_WP;

	writel(NAND_CMD_RESET, bcm_umi_io_base + REG_NAND_CMD_OFFSET);
	nand_bcm_umi_wait_till_ready();

#if NAND_ECC_BCH
	nand_bcm_umi_bch_config_ecc(NAND_ECC_NUM_BYTES);
#endif

	return 0;
}

/* Used to turn latch the proper register for access. */
static void bcm_umi_nand_hwcontrol(struct mtd_info *mtd, int cmd,
				   unsigned int ctrl)
{
	/* send command to hardware */
	struct nand_chip *chip = mtd->priv;
	if (ctrl & NAND_CTRL_CHANGE) {
		if (ctrl & NAND_CLE) {
			chip->IO_ADDR_W = bcm_umi_io_base + REG_NAND_CMD_OFFSET;
			goto CMD;
		}
		if (ctrl & NAND_ALE) {
			chip->IO_ADDR_W =
			    bcm_umi_io_base + REG_NAND_ADDR_OFFSET;
			goto CMD;
		}
		chip->IO_ADDR_W = bcm_umi_io_base + REG_NAND_DATA8_OFFSET;
	}

CMD:
	/* Send command to chip directly */
	if (cmd != NAND_CMD_NONE)
		writeb(cmd, chip->IO_ADDR_W);
}

static void bcm_umi_nand_write_buf(struct mtd_info *mtd, const u_char * buf,
				   int len)
{
	if (USE_DIRECT_IO(len)) {
		/* Do it the old way if the buffer is small or too large.
		 * Probably quicker than starting and checking dma. */
		int i;
		struct nand_chip *this = mtd->priv;

		for (i = 0; i < len; i++)
			writeb(buf[i], this->IO_ADDR_W);
	}
#if USE_DMA
	else
		nand_dma_write(buf, len);
#endif
}

static void bcm_umi_nand_read_buf(struct mtd_info *mtd, u_char * buf, int len)
{
	if (USE_DIRECT_IO(len)) {
		int i;
		struct nand_chip *this = mtd->priv;

		for (i = 0; i < len; i++)
			buf[i] = readb(this->IO_ADDR_R);
	}
#if USE_DMA
	else
		nand_dma_read(buf, len);
#endif
}

static uint8_t readbackbuf[NAND_MAX_PAGESIZE];
static int bcm_umi_nand_verify_buf(struct mtd_info *mtd, const u_char * buf,
				   int len)
{
	/*
	 * Try to readback page with ECC correction. This is necessary
	 * for MLC parts which may have permanently stuck bits.
	 */
	struct nand_chip *chip = mtd->priv;
	int ret = chip->ecc.read_page(mtd, chip, readbackbuf, 0);
	if (ret < 0)
		return -EFAULT;
	else {
		if (memcmp(readbackbuf, buf, len) == 0)
			return 0;

		return -EFAULT;
	}
	return 0;
}

static int __devinit bcm_umi_nand_probe(struct platform_device *pdev)
{
	struct nand_chip *this;
	struct resource *r;
	int err = 0;

	printk(gBanner);

	/* Allocate memory for MTD device structure and private data */
	board_mtd =
	    kmalloc(sizeof(struct mtd_info) + sizeof(struct nand_chip),
		    GFP_KERNEL);
	if (!board_mtd) {
		printk(KERN_WARNING
		       "Unable to allocate NAND MTD device structure.\n");
		return -ENOMEM;
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!r)
		return -ENXIO;

	/* map physical address */
	bcm_umi_io_base = ioremap(r->start, r->end - r->start + 1);

	if (!bcm_umi_io_base) {
		printk(KERN_ERR "ioremap to access BCM UMI NAND chip failed\n");
		kfree(board_mtd);
		return -EIO;
	}

	/* Get pointer to private data */
	this = (struct nand_chip *)(&board_mtd[1]);

	/* Initialize structures */
	memset((char *)board_mtd, 0, sizeof(struct mtd_info));
	memset((char *)this, 0, sizeof(struct nand_chip));

	/* Link the private data with the MTD structure */
	board_mtd->priv = this;

	/* Initialize the NAND hardware.  */
	if (bcm_umi_nand_inithw() < 0) {
		printk(KERN_ERR "BCM UMI NAND chip could not be initialized\n");
		iounmap(bcm_umi_io_base);
		kfree(board_mtd);
		return -EIO;
	}

	/* Set address of NAND IO lines */
	this->IO_ADDR_W = bcm_umi_io_base + REG_NAND_DATA8_OFFSET;
	this->IO_ADDR_R = bcm_umi_io_base + REG_NAND_DATA8_OFFSET;

	/* Set command delay time, see datasheet for correct value */
	this->chip_delay = 0;
	/* Assign the device ready function, if available */
	this->dev_ready = nand_dev_ready;
	this->options = 0;

	this->write_buf = bcm_umi_nand_write_buf;
	this->read_buf = bcm_umi_nand_read_buf;
	this->verify_buf = bcm_umi_nand_verify_buf;

	this->cmd_ctrl = bcm_umi_nand_hwcontrol;
	this->ecc.mode = NAND_ECC_HW;
	this->ecc.size = 512;
	this->ecc.bytes = NAND_ECC_NUM_BYTES;
#if NAND_ECC_BCH
	this->ecc.read_page = bcm_umi_bch_read_page_hwecc;
	this->ecc.write_page = bcm_umi_bch_write_page_hwecc;
#else
	this->ecc.correct = nand_correct_data512;
	this->ecc.calculate = bcm_umi_hamming_get_hw_ecc;
	this->ecc.hwctl = bcm_umi_hamming_enable_hwecc;
#endif

#if USE_DMA
	err = nand_dma_init();
	if (err != 0)
		return err;
#endif

	/* Figure out the size of the device that we have.
	 * We need to do this to figure out which ECC
	 * layout we'll be using.
	 */

	err = nand_scan_ident(board_mtd, 1);
	if (err) {
		printk(KERN_ERR "nand_scan failed: %d\n", err);
		iounmap(bcm_umi_io_base);
		kfree(board_mtd);
		return err;
	}

	/* Now that we know the nand size, we can setup the ECC layout */

	switch (board_mtd->writesize) {	/* writesize is the pagesize */
	case 4096:
		this->ecc.layout = &nand_hw_eccoob_4096;
		break;
	case 2048:
		this->ecc.layout = &nand_hw_eccoob_2048;
		break;
	case 512:
		this->ecc.layout = &nand_hw_eccoob_512;
		break;
	default:
		{
			printk(KERN_ERR "NAND - Unrecognized pagesize: %d\n",
					 board_mtd->writesize);
			return -EINVAL;
		}
	}

#if NAND_ECC_BCH
	if (board_mtd->writesize > 512) {
		if (this->options & NAND_USE_FLASH_BBT)
			largepage_bbt.options = NAND_BBT_SCAN2NDPAGE;
		this->badblock_pattern = &largepage_bbt;
	}
#endif

	/* Now finish off the scan, now that ecc.layout has been initialized. */

	err = nand_scan_tail(board_mtd);
	if (err) {
		printk(KERN_ERR "nand_scan failed: %d\n", err);
		iounmap(bcm_umi_io_base);
		kfree(board_mtd);
		return err;
	}

	/* Register the partitions */
	{
		int nr_partitions;
		struct mtd_partition *partition_info;

		board_mtd->name = "bcm_umi-nand";
		nr_partitions =
		    parse_mtd_partitions(board_mtd, part_probes,
					 &partition_info, 0);

		if (nr_partitions <= 0) {
			printk(KERN_ERR "BCM UMI NAND: Too few partitions - %d\n",
			       nr_partitions);
			iounmap(bcm_umi_io_base);
			kfree(board_mtd);
			return -EIO;
		}
		add_mtd_partitions(board_mtd, partition_info, nr_partitions);
	}

	/* Return happy */
	return 0;
}

static int bcm_umi_nand_remove(struct platform_device *pdev)
{
#if USE_DMA
	nand_dma_term();
#endif

	/* Release resources, unregister device */
	nand_release(board_mtd);

	/* unmap physical address */
	iounmap(bcm_umi_io_base);

	/* Free the MTD device structure */
	kfree(board_mtd);

	return 0;
}

#ifdef CONFIG_PM
static int bcm_umi_nand_suspend(struct platform_device *pdev,
				pm_message_t state)
{
	printk(KERN_ERR "MTD NAND suspend is being called\n");
	return 0;
}

static int bcm_umi_nand_resume(struct platform_device *pdev)
{
	printk(KERN_ERR "MTD NAND resume is being called\n");
	return 0;
}
#else
#define bcm_umi_nand_suspend   NULL
#define bcm_umi_nand_resume    NULL
#endif

static struct platform_driver nand_driver = {
	.driver = {
		   .name = "bcm-nand",
		   .owner = THIS_MODULE,
		   },
	.probe = bcm_umi_nand_probe,
	.remove = bcm_umi_nand_remove,
	.suspend = bcm_umi_nand_suspend,
	.resume = bcm_umi_nand_resume,
};

static int __init nand_init(void)
{
	return platform_driver_register(&nand_driver);
}

static void __exit nand_exit(void)
{
	platform_driver_unregister(&nand_driver);
}

module_init(nand_init);
module_exit(nand_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("BCM UMI MTD NAND driver");
