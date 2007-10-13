/*
 * linux/drivers/ide/mips/au1xxx-ide.c  version 01.30.00        Aug. 02 2005
 *
 * BRIEF MODULE DESCRIPTION
 * AMD Alchemy Au1xxx IDE interface routines over the Static Bus
 *
 * Copyright (c) 2003-2005 AMD, Personal Connectivity Solutions
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Note: for more information, please refer "AMD Alchemy Au1200/Au1550 IDE
 *       Interface and Linux Device Driver" Application Note.
 */
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>

#include <linux/init.h>
#include <linux/ide.h>
#include <linux/sysdev.h>

#include <linux/dma-mapping.h>

#include "ide-timing.h"

#include <asm/io.h>
#include <asm/mach-au1x00/au1xxx.h>
#include <asm/mach-au1x00/au1xxx_dbdma.h>

#include <asm/mach-au1x00/au1xxx_ide.h>

#define DRV_NAME	"au1200-ide"
#define DRV_VERSION	"1.0"
#define DRV_AUTHOR	"Enrico Walther <enrico.walther@amd.com> / Pete Popov <ppopov@embeddedalley.com>"

/* enable the burstmode in the dbdma */
#define IDE_AU1XXX_BURSTMODE	1

static _auide_hwif auide_hwif;
static int dbdma_init_done;

#if defined(CONFIG_BLK_DEV_IDE_AU1XXX_PIO_DBDMA)

void auide_insw(unsigned long port, void *addr, u32 count)
{
	_auide_hwif *ahwif = &auide_hwif;
	chan_tab_t *ctp;
	au1x_ddma_desc_t *dp;

	if(!put_dest_flags(ahwif->rx_chan, (void*)addr, count << 1, 
			   DDMA_FLAGS_NOIE)) {
		printk(KERN_ERR "%s failed %d\n", __FUNCTION__, __LINE__);
		return;
	}
	ctp = *((chan_tab_t **)ahwif->rx_chan);
	dp = ctp->cur_ptr;
	while (dp->dscr_cmd0 & DSCR_CMD0_V)
		;
	ctp->cur_ptr = au1xxx_ddma_get_nextptr_virt(dp);
}

void auide_outsw(unsigned long port, void *addr, u32 count)
{
	_auide_hwif *ahwif = &auide_hwif;
	chan_tab_t *ctp;
	au1x_ddma_desc_t *dp;

	if(!put_source_flags(ahwif->tx_chan, (void*)addr,
			     count << 1, DDMA_FLAGS_NOIE)) {
		printk(KERN_ERR "%s failed %d\n", __FUNCTION__, __LINE__);
		return;
	}
	ctp = *((chan_tab_t **)ahwif->tx_chan);
	dp = ctp->cur_ptr;
	while (dp->dscr_cmd0 & DSCR_CMD0_V)
		;
	ctp->cur_ptr = au1xxx_ddma_get_nextptr_virt(dp);
}

#endif

static void au1xxx_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	int mem_sttime = 0, mem_stcfg = au_readl(MEM_STCFG2);

	/* set pio mode! */
	switch(pio) {
	case 0:
		mem_sttime = SBC_IDE_TIMING(PIO0);

		/* set configuration for RCS2# */
		mem_stcfg |= TS_MASK;
		mem_stcfg &= ~TCSOE_MASK;
		mem_stcfg &= ~TOECS_MASK;
		mem_stcfg |= SBC_IDE_PIO0_TCSOE | SBC_IDE_PIO0_TOECS;
		break;

	case 1:
		mem_sttime = SBC_IDE_TIMING(PIO1);

		/* set configuration for RCS2# */
		mem_stcfg |= TS_MASK;
		mem_stcfg &= ~TCSOE_MASK;
		mem_stcfg &= ~TOECS_MASK;
		mem_stcfg |= SBC_IDE_PIO1_TCSOE | SBC_IDE_PIO1_TOECS;
		break;

	case 2:
		mem_sttime = SBC_IDE_TIMING(PIO2);

		/* set configuration for RCS2# */
		mem_stcfg &= ~TS_MASK;
		mem_stcfg &= ~TCSOE_MASK;
		mem_stcfg &= ~TOECS_MASK;
		mem_stcfg |= SBC_IDE_PIO2_TCSOE | SBC_IDE_PIO2_TOECS;
		break;

	case 3:
		mem_sttime = SBC_IDE_TIMING(PIO3);

		/* set configuration for RCS2# */
		mem_stcfg &= ~TS_MASK;
		mem_stcfg &= ~TCSOE_MASK;
		mem_stcfg &= ~TOECS_MASK;
		mem_stcfg |= SBC_IDE_PIO3_TCSOE | SBC_IDE_PIO3_TOECS;

		break;

	case 4:
		mem_sttime = SBC_IDE_TIMING(PIO4);

		/* set configuration for RCS2# */
		mem_stcfg &= ~TS_MASK;
		mem_stcfg &= ~TCSOE_MASK;
		mem_stcfg &= ~TOECS_MASK;
		mem_stcfg |= SBC_IDE_PIO4_TCSOE | SBC_IDE_PIO4_TOECS;
		break;
	}

	au_writel(mem_sttime,MEM_STTIME2);
	au_writel(mem_stcfg,MEM_STCFG2);
}

static void auide_set_dma_mode(ide_drive_t *drive, const u8 speed)
{
	int mem_sttime = 0, mem_stcfg = au_readl(MEM_STCFG2);

	switch(speed) {
#ifdef CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA
	case XFER_MW_DMA_2:
		mem_sttime = SBC_IDE_TIMING(MDMA2);

		/* set configuration for RCS2# */
		mem_stcfg &= ~TS_MASK;
		mem_stcfg &= ~TCSOE_MASK;
		mem_stcfg &= ~TOECS_MASK;
		mem_stcfg |= SBC_IDE_MDMA2_TCSOE | SBC_IDE_MDMA2_TOECS;

		break;
	case XFER_MW_DMA_1:
		mem_sttime = SBC_IDE_TIMING(MDMA1);

		/* set configuration for RCS2# */
		mem_stcfg &= ~TS_MASK;
		mem_stcfg &= ~TCSOE_MASK;
		mem_stcfg &= ~TOECS_MASK;
		mem_stcfg |= SBC_IDE_MDMA1_TCSOE | SBC_IDE_MDMA1_TOECS;

		break;
	case XFER_MW_DMA_0:
		mem_sttime = SBC_IDE_TIMING(MDMA0);

		/* set configuration for RCS2# */
		mem_stcfg |= TS_MASK;
		mem_stcfg &= ~TCSOE_MASK;
		mem_stcfg &= ~TOECS_MASK;
		mem_stcfg |= SBC_IDE_MDMA0_TCSOE | SBC_IDE_MDMA0_TOECS;

		break;
#endif
	default:
		return;
	}

	au_writel(mem_sttime,MEM_STTIME2);
	au_writel(mem_stcfg,MEM_STCFG2);
}

/*
 * Multi-Word DMA + DbDMA functions
 */

#ifdef CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA

static int auide_build_sglist(ide_drive_t *drive,  struct request *rq)
{
	ide_hwif_t *hwif = drive->hwif;
	_auide_hwif *ahwif = (_auide_hwif*)hwif->hwif_data;
	struct scatterlist *sg = hwif->sg_table;

	ide_map_sg(drive, rq);

	if (rq_data_dir(rq) == READ)
		hwif->sg_dma_direction = DMA_FROM_DEVICE;
	else
		hwif->sg_dma_direction = DMA_TO_DEVICE;

	return dma_map_sg(ahwif->dev, sg, hwif->sg_nents,
			  hwif->sg_dma_direction);
}

static int auide_build_dmatable(ide_drive_t *drive)
{
	int i, iswrite, count = 0;
	ide_hwif_t *hwif = HWIF(drive);

	struct request *rq = HWGROUP(drive)->rq;

	_auide_hwif *ahwif = (_auide_hwif*)hwif->hwif_data;
	struct scatterlist *sg;

	iswrite = (rq_data_dir(rq) == WRITE);
	/* Save for interrupt context */
	ahwif->drive = drive;

	/* Build sglist */
	hwif->sg_nents = i = auide_build_sglist(drive, rq);

	if (!i)
		return 0;

	/* fill the descriptors */
	sg = hwif->sg_table;
	while (i && sg_dma_len(sg)) {
		u32 cur_addr;
		u32 cur_len;

		cur_addr = sg_dma_address(sg);
		cur_len = sg_dma_len(sg);

		while (cur_len) {
			u32 flags = DDMA_FLAGS_NOIE;
			unsigned int tc = (cur_len < 0xfe00)? cur_len: 0xfe00;

			if (++count >= PRD_ENTRIES) {
				printk(KERN_WARNING "%s: DMA table too small\n",
				       drive->name);
				goto use_pio_instead;
			}

			/* Lets enable intr for the last descriptor only */
			if (1==i)
				flags = DDMA_FLAGS_IE;
			else
				flags = DDMA_FLAGS_NOIE;

			if (iswrite) {
				if(!put_source_flags(ahwif->tx_chan, 
						     (void*)(page_address(sg->page) 
							     + sg->offset), 
						     tc, flags)) { 
					printk(KERN_ERR "%s failed %d\n", 
					       __FUNCTION__, __LINE__);
				}
			} else 
			{
				if(!put_dest_flags(ahwif->rx_chan, 
						   (void*)(page_address(sg->page) 
							   + sg->offset), 
						   tc, flags)) { 
					printk(KERN_ERR "%s failed %d\n", 
					       __FUNCTION__, __LINE__);
				}
			}

			cur_addr += tc;
			cur_len -= tc;
		}
		sg++;
		i--;
	}

	if (count)
		return 1;

 use_pio_instead:
	dma_unmap_sg(ahwif->dev,
		     hwif->sg_table,
		     hwif->sg_nents,
		     hwif->sg_dma_direction);

	return 0; /* revert to PIO for this request */
}

static int auide_dma_end(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	_auide_hwif *ahwif = (_auide_hwif*)hwif->hwif_data;

	if (hwif->sg_nents) {
		dma_unmap_sg(ahwif->dev, hwif->sg_table, hwif->sg_nents,
			     hwif->sg_dma_direction);
		hwif->sg_nents = 0;
	}

	return 0;
}

static void auide_dma_start(ide_drive_t *drive )
{
}


static void auide_dma_exec_cmd(ide_drive_t *drive, u8 command)
{
	/* issue cmd to drive */
	ide_execute_command(drive, command, &ide_dma_intr,
			    (2*WAIT_CMD), NULL);
}

static int auide_dma_setup(ide_drive_t *drive)
{       	
	struct request *rq = HWGROUP(drive)->rq;

	if (!auide_build_dmatable(drive)) {
		ide_map_sg(drive, rq);
		return 1;
	}

	drive->waiting_for_dma = 1;
	return 0;
}

static int auide_dma_check(ide_drive_t *drive)
{
	u8 speed = ide_max_dma_mode(drive);

	if( dbdma_init_done == 0 ){
		auide_hwif.white_list = ide_in_drive_list(drive->id,
							  dma_white_list);
		auide_hwif.black_list = ide_in_drive_list(drive->id,
							  dma_black_list);
		auide_hwif.drive = drive;
		auide_ddma_init(&auide_hwif);
		dbdma_init_done = 1;
	}

	/* Is the drive in our DMA black list? */

	if ( auide_hwif.black_list ) {
		drive->using_dma = 0;

		/* Borrowed the warning message from ide-dma.c */

		printk(KERN_WARNING "%s: Disabling DMA for %s (blacklisted)\n",
		       drive->name, drive->id->model);	       
	}
	else
		drive->using_dma = 1;

	if (drive->autodma && (speed & XFER_MODE) != XFER_PIO)
		return 0;

	return -1;
}

static int auide_dma_test_irq(ide_drive_t *drive)
{	
	if (drive->waiting_for_dma == 0)
		printk(KERN_WARNING "%s: ide_dma_test_irq \
                                     called while not waiting\n", drive->name);

	/* If dbdma didn't execute the STOP command yet, the
	 * active bit is still set
	 */
	drive->waiting_for_dma++;
	if (drive->waiting_for_dma >= DMA_WAIT_TIMEOUT) {
		printk(KERN_WARNING "%s: timeout waiting for ddma to \
                                     complete\n", drive->name);
		return 1;
	}
	udelay(10);
	return 0;
}

static void auide_dma_host_on(ide_drive_t *drive)
{
}

static int auide_dma_on(ide_drive_t *drive)
{
	drive->using_dma = 1;

	return 0;
}

static void auide_dma_host_off(ide_drive_t *drive)
{
}

static void auide_dma_off_quietly(ide_drive_t *drive)
{
	drive->using_dma = 0;
}

static void auide_dma_lost_irq(ide_drive_t *drive)
{
	printk(KERN_ERR "%s: IRQ lost\n", drive->name);
}

static void auide_ddma_tx_callback(int irq, void *param)
{
	_auide_hwif *ahwif = (_auide_hwif*)param;
	ahwif->drive->waiting_for_dma = 0;
}

static void auide_ddma_rx_callback(int irq, void *param)
{
	_auide_hwif *ahwif = (_auide_hwif*)param;
	ahwif->drive->waiting_for_dma = 0;
}

#endif /* end CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA */

static void auide_init_dbdma_dev(dbdev_tab_t *dev, u32 dev_id, u32 tsize, u32 devwidth, u32 flags)
{
	dev->dev_id          = dev_id;
	dev->dev_physaddr    = (u32)AU1XXX_ATA_PHYS_ADDR;
	dev->dev_intlevel    = 0;
	dev->dev_intpolarity = 0;
	dev->dev_tsize       = tsize;
	dev->dev_devwidth    = devwidth;
	dev->dev_flags       = flags;
}
  
#if defined(CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA)

static void auide_dma_timeout(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);

	printk(KERN_ERR "%s: DMA timeout occurred: ", drive->name);

	if (hwif->ide_dma_test_irq(drive))
		return;

	hwif->ide_dma_end(drive);
}
					

static int auide_ddma_init(_auide_hwif *auide) {
	
	dbdev_tab_t source_dev_tab, target_dev_tab;
	u32 dev_id, tsize, devwidth, flags;
	ide_hwif_t *hwif = auide->hwif;

	dev_id   = AU1XXX_ATA_DDMA_REQ;

	if (auide->white_list || auide->black_list) {
		tsize    = 8;
		devwidth = 32;
	}
	else { 
		tsize    = 1;
		devwidth = 16;
		
		printk(KERN_ERR "au1xxx-ide: %s is not on ide driver whitelist.\n",auide_hwif.drive->id->model);
		printk(KERN_ERR "            please read 'Documentation/mips/AU1xxx_IDE.README'");
	}

#ifdef IDE_AU1XXX_BURSTMODE 
	flags = DEV_FLAGS_SYNC | DEV_FLAGS_BURSTABLE;
#else
	flags = DEV_FLAGS_SYNC;
#endif

	/* setup dev_tab for tx channel */
	auide_init_dbdma_dev( &source_dev_tab,
			      dev_id,
			      tsize, devwidth, DEV_FLAGS_OUT | flags);
 	auide->tx_dev_id = au1xxx_ddma_add_device( &source_dev_tab );

	auide_init_dbdma_dev( &source_dev_tab,
			      dev_id,
			      tsize, devwidth, DEV_FLAGS_IN | flags);
 	auide->rx_dev_id = au1xxx_ddma_add_device( &source_dev_tab );
	
	/* We also need to add a target device for the DMA */
	auide_init_dbdma_dev( &target_dev_tab,
			      (u32)DSCR_CMD0_ALWAYS,
			      tsize, devwidth, DEV_FLAGS_ANYUSE);
	auide->target_dev_id = au1xxx_ddma_add_device(&target_dev_tab);	
 
	/* Get a channel for TX */
	auide->tx_chan = au1xxx_dbdma_chan_alloc(auide->target_dev_id,
						 auide->tx_dev_id,
						 auide_ddma_tx_callback,
						 (void*)auide);
 
	/* Get a channel for RX */
	auide->rx_chan = au1xxx_dbdma_chan_alloc(auide->rx_dev_id,
						 auide->target_dev_id,
						 auide_ddma_rx_callback,
						 (void*)auide);

	auide->tx_desc_head = (void*)au1xxx_dbdma_ring_alloc(auide->tx_chan,
							     NUM_DESCRIPTORS);
	auide->rx_desc_head = (void*)au1xxx_dbdma_ring_alloc(auide->rx_chan,
							     NUM_DESCRIPTORS);
 
	hwif->dmatable_cpu = dma_alloc_coherent(auide->dev,
						PRD_ENTRIES * PRD_BYTES,        /* 1 Page */
						&hwif->dmatable_dma, GFP_KERNEL);
	
	au1xxx_dbdma_start( auide->tx_chan );
	au1xxx_dbdma_start( auide->rx_chan );
 
	return 0;
} 
#else
 
static int auide_ddma_init( _auide_hwif *auide )
{
	dbdev_tab_t source_dev_tab;
	int flags;

#ifdef IDE_AU1XXX_BURSTMODE 
	flags = DEV_FLAGS_SYNC | DEV_FLAGS_BURSTABLE;
#else
	flags = DEV_FLAGS_SYNC;
#endif

	/* setup dev_tab for tx channel */
	auide_init_dbdma_dev( &source_dev_tab,
			      (u32)DSCR_CMD0_ALWAYS,
			      8, 32, DEV_FLAGS_OUT | flags);
 	auide->tx_dev_id = au1xxx_ddma_add_device( &source_dev_tab );

	auide_init_dbdma_dev( &source_dev_tab,
			      (u32)DSCR_CMD0_ALWAYS,
			      8, 32, DEV_FLAGS_IN | flags);
 	auide->rx_dev_id = au1xxx_ddma_add_device( &source_dev_tab );
	
	/* Get a channel for TX */
	auide->tx_chan = au1xxx_dbdma_chan_alloc(DSCR_CMD0_ALWAYS,
						 auide->tx_dev_id,
						 NULL,
						 (void*)auide);
 
	/* Get a channel for RX */
	auide->rx_chan = au1xxx_dbdma_chan_alloc(auide->rx_dev_id,
						 DSCR_CMD0_ALWAYS,
						 NULL,
						 (void*)auide);
 
	auide->tx_desc_head = (void*)au1xxx_dbdma_ring_alloc(auide->tx_chan,
							     NUM_DESCRIPTORS);
	auide->rx_desc_head = (void*)au1xxx_dbdma_ring_alloc(auide->rx_chan,
							     NUM_DESCRIPTORS);
 
	au1xxx_dbdma_start( auide->tx_chan );
	au1xxx_dbdma_start( auide->rx_chan );
 	
	return 0;
}
#endif

static void auide_setup_ports(hw_regs_t *hw, _auide_hwif *ahwif)
{
	int i;
	unsigned long *ata_regs = hw->io_ports;

	/* FIXME? */
	for (i = 0; i < IDE_CONTROL_OFFSET; i++) {
		*ata_regs++ = ahwif->regbase + (i << AU1XXX_ATA_REG_OFFSET);
	}

	/* set the Alternative Status register */
	*ata_regs = ahwif->regbase + (14 << AU1XXX_ATA_REG_OFFSET);
}

static int au_ide_probe(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	_auide_hwif *ahwif = &auide_hwif;
	ide_hwif_t *hwif;
	struct resource *res;
	hw_regs_t *hw;
	int ret = 0;

#if defined(CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA)
	char *mode = "MWDMA2";
#elif defined(CONFIG_BLK_DEV_IDE_AU1XXX_PIO_DBDMA)
	char *mode = "PIO+DDMA(offload)";
#endif

	memset(&auide_hwif, 0, sizeof(_auide_hwif));
	auide_hwif.dev                  = 0;

	ahwif->dev = dev;
	ahwif->irq = platform_get_irq(pdev, 0);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (res == NULL) {
		pr_debug("%s %d: no base address\n", DRV_NAME, pdev->id);
		ret = -ENODEV;
		goto out;
	}
	if (ahwif->irq < 0) {
		pr_debug("%s %d: no IRQ\n", DRV_NAME, pdev->id);
		ret = -ENODEV;
		goto out;
	}

	if (!request_mem_region (res->start, res->end-res->start, pdev->name)) {
		pr_debug("%s: request_mem_region failed\n", DRV_NAME);
		ret =  -EBUSY;
		goto out;
	}

	ahwif->regbase = (u32)ioremap(res->start, res->end-res->start);
	if (ahwif->regbase == 0) {
		ret = -ENOMEM;
		goto out;
	}

	/* FIXME:  This might possibly break PCMCIA IDE devices */

	hwif                            = &ide_hwifs[pdev->id];
	hw 				= &hwif->hw;
	hwif->irq = hw->irq             = ahwif->irq;
	hwif->chipset                   = ide_au1xxx;

	auide_setup_ports(hw, ahwif);
	memcpy(hwif->io_ports, hw->io_ports, sizeof(hwif->io_ports));

	hwif->ultra_mask                = 0x0;  /* Disable Ultra DMA */
#ifdef CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA
	hwif->mwdma_mask                = 0x07; /* Multimode-2 DMA  */
	hwif->swdma_mask                = 0x00;
#else
	hwif->mwdma_mask                = 0x0;
	hwif->swdma_mask                = 0x0;
#endif

	hwif->pio_mask = ATA_PIO4;
	hwif->host_flags = IDE_HFLAG_POST_SET_MODE;

	hwif->noprobe = 0;
	hwif->drives[0].unmask          = 1;
	hwif->drives[1].unmask          = 1;

	/* hold should be on in all cases */
	hwif->hold                      = 1;

	hwif->mmio  = 1;

	/* If the user has selected DDMA assisted copies,
	   then set up a few local I/O function entry points 
	*/

#ifdef CONFIG_BLK_DEV_IDE_AU1XXX_PIO_DBDMA	
	hwif->INSW                      = auide_insw;
	hwif->OUTSW                     = auide_outsw;
#endif

	hwif->set_pio_mode		= &au1xxx_set_pio_mode;
	hwif->set_dma_mode		= &auide_set_dma_mode;

#ifdef CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA
	hwif->dma_off_quietly		= &auide_dma_off_quietly;
	hwif->dma_timeout		= &auide_dma_timeout;

	hwif->ide_dma_check             = &auide_dma_check;
	hwif->dma_exec_cmd              = &auide_dma_exec_cmd;
	hwif->dma_start                 = &auide_dma_start;
	hwif->ide_dma_end               = &auide_dma_end;
	hwif->dma_setup                 = &auide_dma_setup;
	hwif->ide_dma_test_irq          = &auide_dma_test_irq;
	hwif->dma_host_off		= &auide_dma_host_off;
	hwif->dma_host_on		= &auide_dma_host_on;
	hwif->dma_lost_irq		= &auide_dma_lost_irq;
	hwif->ide_dma_on                = &auide_dma_on;

	hwif->autodma                   = 1;
	hwif->drives[0].autodma         = hwif->autodma;
	hwif->drives[1].autodma         = hwif->autodma;
	hwif->atapi_dma                 = 1;

#else /* !CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA */
	hwif->autodma                   = 0;
	hwif->channel                   = 0;
	hwif->hold                      = 1;
	hwif->select_data               = 0;    /* no chipset-specific code */
	hwif->config_data               = 0;    /* no chipset-specific code */

	hwif->drives[0].autodma         = 0;
	hwif->drives[0].autotune        = 1;    /* 1=autotune, 2=noautotune, 0=default */
#endif
	hwif->drives[0].no_io_32bit     = 1;   

	auide_hwif.hwif                 = hwif;
	hwif->hwif_data                 = &auide_hwif;

#ifdef CONFIG_BLK_DEV_IDE_AU1XXX_PIO_DBDMA           
	auide_ddma_init(&auide_hwif);
	dbdma_init_done = 1;
#endif

	probe_hwif_init(hwif);

	ide_proc_register_port(hwif);

	dev_set_drvdata(dev, hwif);

	printk(KERN_INFO "Au1xxx IDE(builtin) configured for %s\n", mode );

 out:
	return ret;
}

static int au_ide_remove(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *res;
	ide_hwif_t *hwif = dev_get_drvdata(dev);
	_auide_hwif *ahwif = &auide_hwif;

	ide_unregister(hwif - ide_hwifs);

	iounmap((void *)ahwif->regbase);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, res->end - res->start);

	return 0;
}

static struct device_driver au1200_ide_driver = {
	.name		= "au1200-ide",
	.bus		= &platform_bus_type,
	.probe 		= au_ide_probe,
	.remove		= au_ide_remove,
};

static int __init au_ide_init(void)
{
	return driver_register(&au1200_ide_driver);
}

static void __exit au_ide_exit(void)
{
	driver_unregister(&au1200_ide_driver);
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AU1200 IDE driver");

module_init(au_ide_init);
module_exit(au_ide_exit);
