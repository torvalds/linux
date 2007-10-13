/*
 *  linux/arch/i386/kernel/mca.c
 *  Written by Martin Kolinek, February 1996
 *
 * Changes:
 *
 *	Chris Beauregard July 28th, 1996
 *	- Fixed up integrated SCSI detection
 *
 *	Chris Beauregard August 3rd, 1996
 *	- Made mca_info local
 *	- Made integrated registers accessible through standard function calls
 *	- Added name field
 *	- More sanity checking
 *
 *	Chris Beauregard August 9th, 1996
 *	- Rewrote /proc/mca
 *
 *	Chris Beauregard January 7th, 1997
 *	- Added basic NMI-processing
 *	- Added more information to mca_info structure
 *
 *	David Weinehall October 12th, 1998
 *	- Made a lot of cleaning up in the source
 *	- Added use of save_flags / restore_flags
 *	- Added the 'driver_loaded' flag in MCA_adapter
 *	- Added an alternative implemention of ZP Gu's mca_find_unused_adapter
 *
 *	David Weinehall March 24th, 1999
 *	- Fixed the output of 'Driver Installed' in /proc/mca/pos
 *	- Made the Integrated Video & SCSI show up even if they have id 0000
 *
 *	Alexander Viro November 9th, 1999
 *	- Switched to regular procfs methods
 *
 *	Alfred Arnold & David Weinehall August 23rd, 2000
 *	- Added support for Planar POS-registers
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mca.h>
#include <linux/kprobes.h>
#include <asm/system.h>
#include <asm/io.h>
#include <linux/proc_fs.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/ioport.h>
#include <asm/uaccess.h>
#include <linux/init.h>
#include <asm/arch_hooks.h>

static unsigned char which_scsi = 0;

int MCA_bus = 0;
EXPORT_SYMBOL(MCA_bus);

/*
 * Motherboard register spinlock. Untested on SMP at the moment, but
 * are there any MCA SMP boxes?
 *
 * Yes - Alan
 */
static DEFINE_SPINLOCK(mca_lock);

/* Build the status info for the adapter */

static void mca_configure_adapter_status(struct mca_device *mca_dev) {
	mca_dev->status = MCA_ADAPTER_NONE;

	mca_dev->pos_id = mca_dev->pos[0]
		+ (mca_dev->pos[1] << 8);

	if(!mca_dev->pos_id && mca_dev->slot < MCA_MAX_SLOT_NR) {

		/* id = 0x0000 usually indicates hardware failure,
		 * however, ZP Gu (zpg@castle.net> reports that his 9556
		 * has 0x0000 as id and everything still works. There
		 * also seem to be an adapter with id = 0x0000; the
		 * NCR Parallel Bus Memory Card. Until this is confirmed,
		 * however, this code will stay.
		 */

		mca_dev->status = MCA_ADAPTER_ERROR;

		return;
	} else if(mca_dev->pos_id != 0xffff) {

		/* 0xffff usually indicates that there's no adapter,
		 * however, some integrated adapters may have 0xffff as
		 * their id and still be valid. Examples are on-board
		 * VGA of the 55sx, the integrated SCSI of the 56 & 57,
		 * and possibly also the 95 ULTIMEDIA.
		 */

		mca_dev->status = MCA_ADAPTER_NORMAL;
	}

	if((mca_dev->pos_id == 0xffff ||
	    mca_dev->pos_id == 0x0000) && mca_dev->slot >= MCA_MAX_SLOT_NR) {
		int j;

		for(j = 2; j < 8; j++) {
			if(mca_dev->pos[j] != 0xff) {
				mca_dev->status = MCA_ADAPTER_NORMAL;
				break;
			}
		}
	}

	if(!(mca_dev->pos[2] & MCA_ENABLED)) {

		/* enabled bit is in POS 2 */

		mca_dev->status = MCA_ADAPTER_DISABLED;
	}
} /* mca_configure_adapter_status */

/*--------------------------------------------------------------------*/

static struct resource mca_standard_resources[] = {
	{ .start = 0x60, .end = 0x60, .name = "system control port B (MCA)" },
	{ .start = 0x90, .end = 0x90, .name = "arbitration (MCA)" },
	{ .start = 0x91, .end = 0x91, .name = "card Select Feedback (MCA)" },
	{ .start = 0x92, .end = 0x92, .name = "system Control port A (MCA)" },
	{ .start = 0x94, .end = 0x94, .name = "system board setup (MCA)" },
	{ .start = 0x96, .end = 0x97, .name = "POS (MCA)" },
	{ .start = 0x100, .end = 0x107, .name = "POS (MCA)" }
};

#define MCA_STANDARD_RESOURCES	ARRAY_SIZE(mca_standard_resources)

/**
 *	mca_read_and_store_pos - read the POS registers into a memory buffer
 *      @pos: a char pointer to 8 bytes, contains the POS register value on
 *            successful return
 *
 *	Returns 1 if a card actually exists (i.e. the pos isn't
 *	all 0xff) or 0 otherwise
 */
static int mca_read_and_store_pos(unsigned char *pos) {
	int j;
	int found = 0;

	for(j=0; j<8; j++) {
		if((pos[j] = inb_p(MCA_POS_REG(j))) != 0xff) {
			/* 0xff all across means no device. 0x00 means
			 * something's broken, but a device is
			 * probably there.  However, if you get 0x00
			 * from a motherboard register it won't matter
			 * what we find.  For the record, on the
			 * 57SLC, the integrated SCSI adapter has
			 * 0xffff for the adapter ID, but nonzero for
			 * other registers.  */

			found = 1;
		}
	}
	return found;
}

static unsigned char mca_pc_read_pos(struct mca_device *mca_dev, int reg)
{
	unsigned char byte;
	unsigned long flags;

	if(reg < 0 || reg >= 8)
		return 0;

	spin_lock_irqsave(&mca_lock, flags);
	if(mca_dev->pos_register) {
		/* Disable adapter setup, enable motherboard setup */

		outb_p(0, MCA_ADAPTER_SETUP_REG);
		outb_p(mca_dev->pos_register, MCA_MOTHERBOARD_SETUP_REG);

		byte = inb_p(MCA_POS_REG(reg));
		outb_p(0xff, MCA_MOTHERBOARD_SETUP_REG);
	} else {

		/* Make sure motherboard setup is off */

		outb_p(0xff, MCA_MOTHERBOARD_SETUP_REG);

		/* Read the appropriate register */

		outb_p(0x8|(mca_dev->slot & 0xf), MCA_ADAPTER_SETUP_REG);
		byte = inb_p(MCA_POS_REG(reg));
		outb_p(0, MCA_ADAPTER_SETUP_REG);
	}
	spin_unlock_irqrestore(&mca_lock, flags);

	mca_dev->pos[reg] = byte;

	return byte;
}

static void mca_pc_write_pos(struct mca_device *mca_dev, int reg,
			     unsigned char byte)
{
	unsigned long flags;

	if(reg < 0 || reg >= 8)
		return;

	spin_lock_irqsave(&mca_lock, flags);

	/* Make sure motherboard setup is off */

	outb_p(0xff, MCA_MOTHERBOARD_SETUP_REG);

	/* Read in the appropriate register */

	outb_p(0x8|(mca_dev->slot&0xf), MCA_ADAPTER_SETUP_REG);
	outb_p(byte, MCA_POS_REG(reg));
	outb_p(0, MCA_ADAPTER_SETUP_REG);

	spin_unlock_irqrestore(&mca_lock, flags);

	/* Update the global register list, while we have the byte */

	mca_dev->pos[reg] = byte;

}

/* for the primary MCA bus, we have identity transforms */
static int mca_dummy_transform_irq(struct mca_device * mca_dev, int irq)
{
	return irq;
}

static int mca_dummy_transform_ioport(struct mca_device * mca_dev, int port)
{
	return port;
}

static void *mca_dummy_transform_memory(struct mca_device * mca_dev, void *mem)
{
	return mem;
}


static int __init mca_init(void)
{
	unsigned int i, j;
	struct mca_device *mca_dev;
	unsigned char pos[8];
	short mca_builtin_scsi_ports[] = {0xf7, 0xfd, 0x00};
	struct mca_bus *bus;

	/* WARNING: Be careful when making changes here. Putting an adapter
	 * and the motherboard simultaneously into setup mode may result in
	 * damage to chips (according to The Indispensible PC Hardware Book
	 * by Hans-Peter Messmer). Also, we disable system interrupts (so
	 * that we are not disturbed in the middle of this).
	 */

	/* Make sure the MCA bus is present */

	if (mca_system_init()) {
		printk(KERN_ERR "MCA bus system initialisation failed\n");
		return -ENODEV;
	}

	if (!MCA_bus)
		return -ENODEV;

	printk(KERN_INFO "Micro Channel bus detected.\n");

	/* All MCA systems have at least a primary bus */
	bus = mca_attach_bus(MCA_PRIMARY_BUS);
	if (!bus)
		goto out_nomem;
	bus->default_dma_mask = 0xffffffffLL;
	bus->f.mca_write_pos = mca_pc_write_pos;
	bus->f.mca_read_pos = mca_pc_read_pos;
	bus->f.mca_transform_irq = mca_dummy_transform_irq;
	bus->f.mca_transform_ioport = mca_dummy_transform_ioport;
	bus->f.mca_transform_memory = mca_dummy_transform_memory;

	/* get the motherboard device */
	mca_dev = kzalloc(sizeof(struct mca_device), GFP_KERNEL);
	if(unlikely(!mca_dev))
		goto out_nomem;

	/*
	 * We do not expect many MCA interrupts during initialization,
	 * but let us be safe:
	 */
	spin_lock_irq(&mca_lock);

	/* Make sure adapter setup is off */

	outb_p(0, MCA_ADAPTER_SETUP_REG);

	/* Read motherboard POS registers */

	mca_dev->pos_register = 0x7f;
	outb_p(mca_dev->pos_register, MCA_MOTHERBOARD_SETUP_REG);
	mca_dev->name[0] = 0;
	mca_read_and_store_pos(mca_dev->pos);
	mca_configure_adapter_status(mca_dev);
	/* fake POS and slot for a motherboard */
	mca_dev->pos_id = MCA_MOTHERBOARD_POS;
	mca_dev->slot = MCA_MOTHERBOARD;
	mca_register_device(MCA_PRIMARY_BUS, mca_dev);

	mca_dev = kzalloc(sizeof(struct mca_device), GFP_ATOMIC);
	if(unlikely(!mca_dev))
		goto out_unlock_nomem;

	/* Put motherboard into video setup mode, read integrated video
	 * POS registers, and turn motherboard setup off.
	 */

	mca_dev->pos_register = 0xdf;
	outb_p(mca_dev->pos_register, MCA_MOTHERBOARD_SETUP_REG);
	mca_dev->name[0] = 0;
	mca_read_and_store_pos(mca_dev->pos);
	mca_configure_adapter_status(mca_dev);
	/* fake POS and slot for the integrated video */
	mca_dev->pos_id = MCA_INTEGVIDEO_POS;
	mca_dev->slot = MCA_INTEGVIDEO;
	mca_register_device(MCA_PRIMARY_BUS, mca_dev);

	/* Put motherboard into scsi setup mode, read integrated scsi
	 * POS registers, and turn motherboard setup off.
	 *
	 * It seems there are two possible SCSI registers. Martin says that
	 * for the 56,57, 0xf7 is the one, but fails on the 76.
	 * Alfredo (apena@vnet.ibm.com) says
	 * 0xfd works on his machine. We'll try both of them. I figure it's
	 * a good bet that only one could be valid at a time. This could
	 * screw up though if one is used for something else on the other
	 * machine.
	 */

	for(i = 0; (which_scsi = mca_builtin_scsi_ports[i]) != 0; i++) {
		outb_p(which_scsi, MCA_MOTHERBOARD_SETUP_REG);
		if(mca_read_and_store_pos(pos))
			break;
	}
	if(which_scsi) {
		/* found a scsi card */
		mca_dev = kzalloc(sizeof(struct mca_device), GFP_ATOMIC);
		if(unlikely(!mca_dev))
			goto out_unlock_nomem;

		for(j = 0; j < 8; j++)
			mca_dev->pos[j] = pos[j];

		mca_configure_adapter_status(mca_dev);
		/* fake POS and slot for integrated SCSI controller */
		mca_dev->pos_id = MCA_INTEGSCSI_POS;
		mca_dev->slot = MCA_INTEGSCSI;
		mca_dev->pos_register = which_scsi;
		mca_register_device(MCA_PRIMARY_BUS, mca_dev);
	}

	/* Turn off motherboard setup */

	outb_p(0xff, MCA_MOTHERBOARD_SETUP_REG);

	/* Now loop over MCA slots: put each adapter into setup mode, and
	 * read its POS registers. Then put adapter setup off.
	 */

	for(i=0; i<MCA_MAX_SLOT_NR; i++) {
		outb_p(0x8|(i&0xf), MCA_ADAPTER_SETUP_REG);
		if(!mca_read_and_store_pos(pos))
			continue;

		mca_dev = kzalloc(sizeof(struct mca_device), GFP_ATOMIC);
		if(unlikely(!mca_dev))
			goto out_unlock_nomem;

		for(j=0; j<8; j++)
			mca_dev->pos[j]=pos[j];

		mca_dev->driver_loaded = 0;
		mca_dev->slot = i;
		mca_dev->pos_register = 0;
		mca_configure_adapter_status(mca_dev);
		mca_register_device(MCA_PRIMARY_BUS, mca_dev);
	}
	outb_p(0, MCA_ADAPTER_SETUP_REG);

	/* Enable interrupts and return memory start */
	spin_unlock_irq(&mca_lock);

	for (i = 0; i < MCA_STANDARD_RESOURCES; i++)
		request_resource(&ioport_resource, mca_standard_resources + i);

	mca_do_proc_init();

	return 0;

 out_unlock_nomem:
	spin_unlock_irq(&mca_lock);
 out_nomem:
	printk(KERN_EMERG "Failed memory allocation in MCA setup!\n");
	return -ENOMEM;
}

subsys_initcall(mca_init);

/*--------------------------------------------------------------------*/

static __kprobes void
mca_handle_nmi_device(struct mca_device *mca_dev, int check_flag)
{
	int slot = mca_dev->slot;

	if(slot == MCA_INTEGSCSI) {
		printk(KERN_CRIT "NMI: caused by MCA integrated SCSI adapter (%s)\n",
			mca_dev->name);
	} else if(slot == MCA_INTEGVIDEO) {
		printk(KERN_CRIT "NMI: caused by MCA integrated video adapter (%s)\n",
			mca_dev->name);
	} else if(slot == MCA_MOTHERBOARD) {
		printk(KERN_CRIT "NMI: caused by motherboard (%s)\n",
			mca_dev->name);
	}

	/* More info available in POS 6 and 7? */

	if(check_flag) {
		unsigned char pos6, pos7;

		pos6 = mca_device_read_pos(mca_dev, 6);
		pos7 = mca_device_read_pos(mca_dev, 7);

		printk(KERN_CRIT "NMI: POS 6 = 0x%x, POS 7 = 0x%x\n", pos6, pos7);
	}

} /* mca_handle_nmi_slot */

/*--------------------------------------------------------------------*/

static int __kprobes mca_handle_nmi_callback(struct device *dev, void *data)
{
	struct mca_device *mca_dev = to_mca_device(dev);
	unsigned char pos5;

	pos5 = mca_device_read_pos(mca_dev, 5);

	if(!(pos5 & 0x80)) {
		/* Bit 7 of POS 5 is reset when this adapter has a hardware
		 * error. Bit 7 it reset if there's error information
		 * available in POS 6 and 7.
		 */
		mca_handle_nmi_device(mca_dev, !(pos5 & 0x40));
		return 1;
	}
	return 0;
}

void __kprobes mca_handle_nmi(void)
{
	/* First try - scan the various adapters and see if a specific
	 * adapter was responsible for the error.
	 */
	bus_for_each_dev(&mca_bus_type, NULL, NULL, mca_handle_nmi_callback);

	mca_nmi_hook();
} /* mca_handle_nmi */
