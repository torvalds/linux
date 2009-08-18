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

#include <asm/setup.h>
#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/atari_stdma.h>
#include <asm/ide.h>

#define DRV_NAME "falconide"

    /*
     *  Base of the IDE interface
     */

#define ATA_HD_BASE	0xfff00000

    /*
     *  Offsets from the above base
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
		if (in_interrupt() > 0)
			panic("Falcon IDE hasn't ST-DMA lock in interrupt");
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

static void __init falconide_setup_ports(struct ide_hw *hw)
{
	int i;

	memset(hw, 0, sizeof(*hw));

	hw->io_ports.data_addr = ATA_HD_BASE;

	for (i = 1; i < 8; i++)
		hw->io_ports_array[i] = ATA_HD_BASE + 1 + i * 4;

	hw->io_ports.ctl_addr = ATA_HD_BASE + ATA_HD_CONTROL;

	hw->irq = IRQ_MFP_IDE;
}

    /*
     *  Probe for a Falcon IDE interface
     */

static int __init falconide_init(void)
{
	struct ide_host *host;
	struct ide_hw hw, *hws[] = { &hw };
	int rc;

	if (!MACH_IS_ATARI || !ATARIHW_PRESENT(IDE))
		return -ENODEV;

	printk(KERN_INFO "ide: Falcon IDE controller\n");

	if (!request_mem_region(ATA_HD_BASE, 0x40, DRV_NAME)) {
		printk(KERN_ERR "%s: resources busy\n", DRV_NAME);
		return -EBUSY;
	}

	falconide_setup_ports(&hw);

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
	release_mem_region(ATA_HD_BASE, 0x40);
	return rc;
}

module_init(falconide_init);

MODULE_LICENSE("GPL");
