/*
 * TX4938 internal IDE driver
 * Based on tx4939ide.c.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * (C) Copyright TOSHIBA CORPORATION 2005-2007
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <asm/ide.h>
#include <asm/txx9/tx4938.h>

static void tx4938ide_tune_ebusc(unsigned int ebus_ch,
				 unsigned int gbus_clock,
				 u8 pio)
{
	struct ide_timing *t = ide_timing_find_mode(XFER_PIO_0 + pio);
	u64 cr = __raw_readq(&tx4938_ebuscptr->cr[ebus_ch]);
	unsigned int sp = (cr >> 4) & 3;
	unsigned int clock = gbus_clock / (4 - sp);
	unsigned int cycle = 1000000000 / clock;
	unsigned int shwt;
	int wt;

	/* Minimum DIOx- active time */
	wt = DIV_ROUND_UP(t->act8b, cycle) - 2;
	/* IORDY setup time: 35ns */
	wt = max_t(int, wt, DIV_ROUND_UP(35, cycle));
	/* actual wait-cycle is max(wt & ~1, 1) */
	if (wt > 2 && (wt & 1))
		wt++;
	wt &= ~1;
	/* Address-valid to DIOR/DIOW setup */
	shwt = DIV_ROUND_UP(t->setup, cycle);

	/* -DIOx recovery time (SHWT * 4) and cycle time requirement */
	while ((shwt * 4 + wt + (wt ? 2 : 3)) * cycle < t->cycle)
		shwt++;
	if (shwt > 7) {
		pr_warning("tx4938ide: SHWT violation (%d)\n", shwt);
		shwt = 7;
	}
	pr_debug("tx4938ide: ebus %d, bus cycle %dns, WT %d, SHWT %d\n",
		 ebus_ch, cycle, wt, shwt);

	__raw_writeq((cr & ~0x3f007ull) | (wt << 12) | shwt,
		     &tx4938_ebuscptr->cr[ebus_ch]);
}

static void tx4938ide_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	ide_hwif_t *hwif = drive->hwif;
	struct tx4938ide_platform_info *pdata = hwif->dev->platform_data;
	u8 safe = pio;
	ide_drive_t *pair;

	pair = ide_get_pair_dev(drive);
	if (pair)
		safe = min(safe, ide_get_best_pio_mode(pair, 255, 5));
	tx4938ide_tune_ebusc(pdata->ebus_ch, pdata->gbus_clock, safe);
}

#ifdef __BIG_ENDIAN

/* custom iops (independent from SWAP_IO_SPACE) */
static void tx4938ide_input_data_swap(ide_drive_t *drive, struct ide_cmd *cmd,
				void *buf, unsigned int len)
{
	unsigned long port = drive->hwif->io_ports.data_addr;
	unsigned short *ptr = buf;
	unsigned int count = (len + 1) / 2;

	while (count--)
		*ptr++ = cpu_to_le16(__raw_readw((void __iomem *)port));
	__ide_flush_dcache_range((unsigned long)buf, roundup(len, 2));
}

static void tx4938ide_output_data_swap(ide_drive_t *drive, struct ide_cmd *cmd,
				void *buf, unsigned int len)
{
	unsigned long port = drive->hwif->io_ports.data_addr;
	unsigned short *ptr = buf;
	unsigned int count = (len + 1) / 2;

	while (count--) {
		__raw_writew(le16_to_cpu(*ptr), (void __iomem *)port);
		ptr++;
	}
	__ide_flush_dcache_range((unsigned long)buf, roundup(len, 2));
}

static const struct ide_tp_ops tx4938ide_tp_ops = {
	.exec_command		= ide_exec_command,
	.read_status		= ide_read_status,
	.read_altstatus		= ide_read_altstatus,
	.write_devctl		= ide_write_devctl,

	.dev_select		= ide_dev_select,
	.tf_load		= ide_tf_load,
	.tf_read		= ide_tf_read,

	.input_data		= tx4938ide_input_data_swap,
	.output_data		= tx4938ide_output_data_swap,
};

#endif	/* __BIG_ENDIAN */

static const struct ide_port_ops tx4938ide_port_ops = {
	.set_pio_mode		= tx4938ide_set_pio_mode,
};

static const struct ide_port_info tx4938ide_port_info __initdata = {
	.port_ops		= &tx4938ide_port_ops,
#ifdef __BIG_ENDIAN
	.tp_ops			= &tx4938ide_tp_ops,
#endif
	.host_flags		= IDE_HFLAG_MMIO | IDE_HFLAG_NO_DMA,
	.pio_mask		= ATA_PIO5,
	.chipset		= ide_generic,
};

static int __init tx4938ide_probe(struct platform_device *pdev)
{
	hw_regs_t hw;
	hw_regs_t *hws[] = { &hw, NULL, NULL, NULL };
	struct ide_host *host;
	struct resource *res;
	struct tx4938ide_platform_info *pdata = pdev->dev.platform_data;
	int irq, ret, i;
	unsigned long mapbase, mapctl;
	struct ide_port_info d = tx4938ide_port_info;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENODEV;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	if (!devm_request_mem_region(&pdev->dev, res->start,
				     res->end - res->start + 1, "tx4938ide"))
		return -EBUSY;
	mapbase = (unsigned long)devm_ioremap(&pdev->dev, res->start,
					      8 << pdata->ioport_shift);
	mapctl = (unsigned long)devm_ioremap(&pdev->dev,
					     res->start + 0x10000 +
					     (6 << pdata->ioport_shift),
					     1 << pdata->ioport_shift);
	if (!mapbase || !mapctl)
		return -EBUSY;

	memset(&hw, 0, sizeof(hw));
	if (pdata->ioport_shift) {
		unsigned long port = mapbase;
		unsigned long ctl = mapctl;

		hw.io_ports_array[0] = port;
#ifdef __BIG_ENDIAN
		port++;
		ctl++;
#endif
		for (i = 1; i <= 7; i++)
			hw.io_ports_array[i] =
				port + (i << pdata->ioport_shift);
		hw.io_ports.ctl_addr = ctl;
	} else
		ide_std_init_ports(&hw, mapbase, mapctl);
	hw.irq = irq;
	hw.dev = &pdev->dev;

	pr_info("TX4938 IDE interface (base %#lx, ctl %#lx, irq %d)\n",
		mapbase, mapctl, hw.irq);
	if (pdata->gbus_clock)
		tx4938ide_tune_ebusc(pdata->ebus_ch, pdata->gbus_clock, 0);
	else
		d.port_ops = NULL;
	ret = ide_host_add(&d, hws, &host);
	if (!ret)
		platform_set_drvdata(pdev, host);
	return ret;
}

static int __exit tx4938ide_remove(struct platform_device *pdev)
{
	struct ide_host *host = platform_get_drvdata(pdev);

	ide_host_remove(host);
	return 0;
}

static struct platform_driver tx4938ide_driver = {
	.driver		= {
		.name	= "tx4938ide",
		.owner	= THIS_MODULE,
	},
	.remove = __exit_p(tx4938ide_remove),
};

static int __init tx4938ide_init(void)
{
	return platform_driver_probe(&tx4938ide_driver, tx4938ide_probe);
}

static void __exit tx4938ide_exit(void)
{
	platform_driver_unregister(&tx4938ide_driver);
}

module_init(tx4938ide_init);
module_exit(tx4938ide_exit);

MODULE_DESCRIPTION("TX4938 internal IDE driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:tx4938ide");
