/*
 *  Atari Falcon IDE Driver
 *
 *     Created 12 Jul 1997 by Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/setup.h>
#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/atari_stdma.h>
#include <asm/ide.h>

#define DRV_NAME "falconide"

    /*
     *  Offsets from base address
     */

#define ATA_HD_CONTROL	0x39

    /*
     *  falconide_intr_lock is used to obtain access to the IDE interrupt,
     *  which is shared between several drivers.
     */

static int falconide_intr_lock;

static void falconide_release_lock(void)
{
	if (falconide_intr_lock == 0) {
		printk(KERN_ERR "%s: bug\n", __func__);
		return;
	}
	falconide_intr_lock = 0;
	stdma_release();
}

static void falconide_get_lock(irq_handler_t handler, void *data)
{
	if (falconide_intr_lock == 0) {
		stdma_lock(handler, data);
		falconide_intr_lock = 1;
	}
}

static void falconide_input_data(ide_drive_t *drive, struct ide_cmd *cmd,
				 void *buf, unsigned int len)
{
	unsigned long data_addr = drive->hwif->io_ports.data_addr;

	if (drive->media == ide_disk && cmd && (cmd->tf_flags & IDE_TFLAG_FS)) {
		__ide_mm_insw(data_addr, buf, (len + 1) / 2);
		return;
	}

	raw_insw_swapw((u16 *)data_addr, buf, (len + 1) / 2);
}

static void falconide_output_data(ide_drive_t *drive, struct ide_cmd *cmd,
				  void *buf, unsigned int len)
{
	unsigned long data_addr = drive->hwif->io_ports.data_addr;

	if (drive->media == ide_disk && cmd && (cmd->tf_flags & IDE_TFLAG_FS)) {
		__ide_mm_outsw(data_addr, buf, (len + 1) / 2);
		return;
	}

	raw_outsw_swapw((u16 *)data_addr, buf, (len + 1) / 2);
}

/* Atari has a byte-swapped IDE interface */
static const struct ide_tp_ops falconide_tp_ops = {
	.exec_command		= ide_exec_command,
	.read_status		= ide_read_status,
	.read_altstatus		= ide_read_altstatus,
	.write_devctl		= ide_write_devctl,

	.dev_select		= ide_dev_select,
	.tf_load		= ide_tf_load,
	.tf_read		= ide_tf_read,

	.input_data		= falconide_input_data,
	.output_data		= falconide_output_data,
};

static const struct ide_port_info falconide_port_info = {
	.get_lock		= falconide_get_lock,
	.release_lock		= falconide_release_lock,
	.tp_ops			= &falconide_tp_ops,
	.host_flags		= IDE_HFLAG_MMIO | IDE_HFLAG_SERIALIZE |
				  IDE_HFLAG_NO_DMA,
	.irq_flags		= IRQF_SHARED,
	.chipset		= ide_generic,
};

static void __init falconide_setup_ports(struct ide_hw *hw, unsigned long base)
{
	int i;

	memset(hw, 0, sizeof(*hw));

	hw->io_ports.data_addr = base;

	for (i = 1; i < 8; i++)
		hw->io_ports_array[i] = base + 1 + i * 4;

	hw->io_ports.ctl_addr = base + ATA_HD_CONTROL;

	hw->irq = IRQ_MFP_IDE;
}

    /*
     *  Probe for a Falcon IDE interface
     */

static int __init falconide_init(struct platform_device *pdev)
{
	struct resource *res;
	struct ide_host *host;
	struct ide_hw hw, *hws[] = { &hw };
	unsigned long base;
	int rc;

	dev_info(&pdev->dev, "Atari Falcon IDE controller\n");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	if (!devm_request_mem_region(&pdev->dev, res->start,
				     resource_size(res), DRV_NAME)) {
		dev_err(&pdev->dev, "resources busy\n");
		return -EBUSY;
	}

	base = (unsigned long)res->start;

	falconide_setup_ports(&hw, base);

	host = ide_host_alloc(&falconide_port_info, hws, 1);
	if (host == NULL) {
		rc = -ENOMEM;
		goto err;
	}

	falconide_get_lock(NULL, NULL);
	rc = ide_host_register(host, &falconide_port_info, hws);
	falconide_release_lock();

	if (rc)
		goto err_free;

	return 0;
err_free:
	ide_host_free(host);
err:
	release_mem_region(res->start, resource_size(res));
	return rc;
}

static int falconide_remove(struct platform_device *pdev)
{
	struct ide_host *host = dev_get_drvdata(&pdev->dev);

	ide_host_remove(host);

	return 0;
}

static struct platform_driver ide_falcon_driver = {
	.remove = falconide_remove,
	.driver   = {
		.name	= "atari-falcon-ide",
	},
};

module_platform_driver_probe(ide_falcon_driver, falconide_init);

MODULE_AUTHOR("Geert Uytterhoeven");
MODULE_DESCRIPTION("low-level driver for Atari Falcon IDE");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:atari-falcon-ide");
