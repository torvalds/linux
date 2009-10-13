/*
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
#include <linux/scatterlist.h>

#include <asm/mach-au1x00/au1xxx.h>
#include <asm/mach-au1x00/au1xxx_dbdma.h>
#include <asm/mach-au1x00/au1xxx_ide.h>

#define DRV_NAME	"au1200-ide"
#define DRV_AUTHOR	"Enrico Walther <enrico.walther@amd.com> / Pete Popov <ppopov@embeddedalley.com>"

/* enable the burstmode in the dbdma */
#define IDE_AU1XXX_BURSTMODE	1

static _auide_hwif auide_hwif;

#if defined(CONFIG_BLK_DEV_IDE_AU1XXX_PIO_DBDMA)

static inline void auide_insw(unsigned long port, void *addr, u32 count)
{
	_auide_hwif *ahwif = &auide_hwif;
	chan_tab_t *ctp;
	au1x_ddma_desc_t *dp;

	if (!au1xxx_dbdma_put_dest(ahwif->rx_chan, virt_to_phys(addr),
				   count << 1, DDMA_FLAGS_NOIE)) {
		printk(KERN_ERR "%s failed %d\n", __func__, __LINE__);
		return;
	}
	ctp = *((chan_tab_t **)ahwif->rx_chan);
	dp = ctp->cur_ptr;
	while (dp->dscr_cmd0 & DSCR_CMD0_V)
		;
	ctp->cur_ptr = au1xxx_ddma_get_nextptr_virt(dp);
}

static inline void auide_outsw(unsigned long port, void *addr, u32 count)
{
	_auide_hwif *ahwif = &auide_hwif;
	chan_tab_t *ctp;
	au1x_ddma_desc_t *dp;

	if (!au1xxx_dbdma_put_source(ahwif->tx_chan, virt_to_phys(addr),
				     count << 1, DDMA_FLAGS_NOIE)) {
		printk(KERN_ERR "%s failed %d\n", __func__, __LINE__);
		return;
	}
	ctp = *((chan_tab_t **)ahwif->tx_chan);
	dp = ctp->cur_ptr;
	while (dp->dscr_cmd0 & DSCR_CMD0_V)
		;
	ctp->cur_ptr = au1xxx_ddma_get_nextptr_virt(dp);
}

static void au1xxx_input_data(ide_drive_t *drive, struct ide_cmd *cmd,
			      void *buf, unsigned int len)
{
	auide_insw(drive->hwif->io_ports.data_addr, buf, (len + 1) / 2);
}

static void au1xxx_output_data(ide_drive_t *drive, struct ide_cmd *cmd,
			       void *buf, unsigned int len)
{
	auide_outsw(drive->hwif->io_ports.data_addr, buf, (len + 1) / 2);
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
	}

	au_writel(mem_sttime,MEM_STTIME2);
	au_writel(mem_stcfg,MEM_STCFG2);
}

/*
 * Multi-Word DMA + DbDMA functions
 */

#ifdef CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA
static int auide_build_dmatable(ide_drive_t *drive, struct ide_cmd *cmd)
{
	ide_hwif_t *hwif = drive->hwif;
	_auide_hwif *ahwif = &auide_hwif;
	struct scatterlist *sg;
	int i = cmd->sg_nents, count = 0;
	int iswrite = !!(cmd->tf_flags & IDE_TFLAG_WRITE);

	/* Save for interrupt context */
	ahwif->drive = drive;

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
				return 0;
			}

			/* Lets enable intr for the last descriptor only */
			if (1==i)
				flags = DDMA_FLAGS_IE;
			else
				flags = DDMA_FLAGS_NOIE;

			if (iswrite) {
				if (!au1xxx_dbdma_put_source(ahwif->tx_chan,
					sg_phys(sg), tc, flags)) {
					printk(KERN_ERR "%s failed %d\n", 
					       __func__, __LINE__);
				}
			} else  {
				if (!au1xxx_dbdma_put_dest(ahwif->rx_chan,
					sg_phys(sg), tc, flags)) {
					printk(KERN_ERR "%s failed %d\n", 
					       __func__, __LINE__);
				}
			}

			cur_addr += tc;
			cur_len -= tc;
		}
		sg = sg_next(sg);
		i--;
	}

	if (count)
		return 1;

	return 0; /* revert to PIO for this request */
}

static int auide_dma_end(ide_drive_t *drive)
{
	return 0;
}

static void auide_dma_start(ide_drive_t *drive )
{
}


static int auide_dma_setup(ide_drive_t *drive, struct ide_cmd *cmd)
{
	if (auide_build_dmatable(drive, cmd) == 0)
		return 1;

	return 0;
}

static int auide_dma_test_irq(ide_drive_t *drive)
{
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

static void auide_dma_host_set(ide_drive_t *drive, int on)
{
}

static void auide_ddma_tx_callback(int irq, void *param)
{
}

static void auide_ddma_rx_callback(int irq, void *param)
{
}
#endif /* end CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA */

static void auide_init_dbdma_dev(dbdev_tab_t *dev, u32 dev_id, u32 tsize, u32 devwidth, u32 flags)
{
	dev->dev_id          = dev_id;
	dev->dev_physaddr    = (u32)IDE_PHYS_ADDR;
	dev->dev_intlevel    = 0;
	dev->dev_intpolarity = 0;
	dev->dev_tsize       = tsize;
	dev->dev_devwidth    = devwidth;
	dev->dev_flags       = flags;
}

#ifdef CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA
static const struct ide_dma_ops au1xxx_dma_ops = {
	.dma_host_set		= auide_dma_host_set,
	.dma_setup		= auide_dma_setup,
	.dma_start		= auide_dma_start,
	.dma_end		= auide_dma_end,
	.dma_test_irq		= auide_dma_test_irq,
	.dma_lost_irq		= ide_dma_lost_irq,
};

static int auide_ddma_init(ide_hwif_t *hwif, const struct ide_port_info *d)
{
	_auide_hwif *auide = &auide_hwif;
	dbdev_tab_t source_dev_tab, target_dev_tab;
	u32 dev_id, tsize, devwidth, flags;

	dev_id	 = IDE_DDMA_REQ;

	tsize    =  8; /*  1 */
	devwidth = 32; /* 16 */

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

	/* FIXME: check return value */
	(void)ide_allocate_dma_engine(hwif);
	
	au1xxx_dbdma_start( auide->tx_chan );
	au1xxx_dbdma_start( auide->rx_chan );
 
	return 0;
} 
#else
static int auide_ddma_init(ide_hwif_t *hwif, const struct ide_port_info *d)
{
	_auide_hwif *auide = &auide_hwif;
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

static void auide_setup_ports(struct ide_hw *hw, _auide_hwif *ahwif)
{
	int i;
	unsigned long *ata_regs = hw->io_ports_array;

	/* FIXME? */
	for (i = 0; i < 8; i++)
		*ata_regs++ = ahwif->regbase + (i << IDE_REG_SHIFT);

	/* set the Alternative Status register */
	*ata_regs = ahwif->regbase + (14 << IDE_REG_SHIFT);
}

#ifdef CONFIG_BLK_DEV_IDE_AU1XXX_PIO_DBDMA
static const struct ide_tp_ops au1xxx_tp_ops = {
	.exec_command		= ide_exec_command,
	.read_status		= ide_read_status,
	.read_altstatus		= ide_read_altstatus,
	.write_devctl		= ide_write_devctl,

	.dev_select		= ide_dev_select,
	.tf_load		= ide_tf_load,
	.tf_read		= ide_tf_read,

	.input_data		= au1xxx_input_data,
	.output_data		= au1xxx_output_data,
};
#endif

static const struct ide_port_ops au1xxx_port_ops = {
	.set_pio_mode		= au1xxx_set_pio_mode,
	.set_dma_mode		= auide_set_dma_mode,
};

static const struct ide_port_info au1xxx_port_info = {
	.init_dma		= auide_ddma_init,
#ifdef CONFIG_BLK_DEV_IDE_AU1XXX_PIO_DBDMA
	.tp_ops			= &au1xxx_tp_ops,
#endif
	.port_ops		= &au1xxx_port_ops,
#ifdef CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA
	.dma_ops		= &au1xxx_dma_ops,
#endif
	.host_flags		= IDE_HFLAG_POST_SET_MODE |
				  IDE_HFLAG_NO_IO_32BIT |
				  IDE_HFLAG_UNMASK_IRQS,
	.pio_mask		= ATA_PIO4,
#ifdef CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA
	.mwdma_mask		= ATA_MWDMA2,
#endif
	.chipset		= ide_au1xxx,
};

static int au_ide_probe(struct platform_device *dev)
{
	_auide_hwif *ahwif = &auide_hwif;
	struct resource *res;
	struct ide_host *host;
	int ret = 0;
	struct ide_hw hw, *hws[] = { &hw };

#if defined(CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA)
	char *mode = "MWDMA2";
#elif defined(CONFIG_BLK_DEV_IDE_AU1XXX_PIO_DBDMA)
	char *mode = "PIO+DDMA(offload)";
#endif

	memset(&auide_hwif, 0, sizeof(_auide_hwif));
	ahwif->irq = platform_get_irq(dev, 0);

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);

	if (res == NULL) {
		pr_debug("%s %d: no base address\n", DRV_NAME, dev->id);
		ret = -ENODEV;
		goto out;
	}
	if (ahwif->irq < 0) {
		pr_debug("%s %d: no IRQ\n", DRV_NAME, dev->id);
		ret = -ENODEV;
		goto out;
	}

	if (!request_mem_region(res->start, resource_size(res), dev->name)) {
		pr_debug("%s: request_mem_region failed\n", DRV_NAME);
		ret =  -EBUSY;
		goto out;
	}

	ahwif->regbase = (u32)ioremap(res->start, resource_size(res));
	if (ahwif->regbase == 0) {
		ret = -ENOMEM;
		goto out;
	}

	memset(&hw, 0, sizeof(hw));
	auide_setup_ports(&hw, ahwif);
	hw.irq = ahwif->irq;
	hw.dev = &dev->dev;

	ret = ide_host_add(&au1xxx_port_info, hws, 1, &host);
	if (ret)
		goto out;

	auide_hwif.hwif = host->ports[0];

	platform_set_drvdata(dev, host);

	printk(KERN_INFO "Au1xxx IDE(builtin) configured for %s\n", mode );

 out:
	return ret;
}

static int au_ide_remove(struct platform_device *dev)
{
	struct resource *res;
	struct ide_host *host = platform_get_drvdata(dev);
	_auide_hwif *ahwif = &auide_hwif;

	ide_host_remove(host);

	iounmap((void *)ahwif->regbase);

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	return 0;
}

static struct platform_driver au1200_ide_driver = {
	.driver = {
		.name		= "au1200-ide",
		.owner		= THIS_MODULE,
	},
	.probe 		= au_ide_probe,
	.remove		= au_ide_remove,
};

static int __init au_ide_init(void)
{
	return platform_driver_register(&au1200_ide_driver);
}

static void __exit au_ide_exit(void)
{
	platform_driver_unregister(&au1200_ide_driver);
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AU1200 IDE driver");

module_init(au_ide_init);
module_exit(au_ide_exit);
