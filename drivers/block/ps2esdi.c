/* ps2esdi driver based on assembler code by Arindam Banerji,
   written by Peter De Schrijver */
/* Reassuring note to IBM : This driver was NOT developed by vice-versa
   engineering the PS/2's BIOS */
/* Dedicated to Wannes, Tofke, Ykke, Godot, Killroy and all those 
   other lovely fish out there... */
/* This code was written during the long and boring WINA 
   elections 1994 */
/* Thanks to Arindam Banerij for giving me the source of his driver */
/* This code may be freely distributed and modified in any way, 
   as long as these notes remain intact */

/*  Revised: 05/07/94 by Arindam Banerji (axb@cse.nd.edu) */
/*  Revised: 09/08/94 by Peter De Schrijver (stud11@cc4.kuleuven.ac.be)
   Thanks to Arindam Banerij for sending me the docs of the adapter */

/* BA Modified for ThinkPad 720 by Boris Ashkinazi */
/*                    (bash@vnet.ibm.com) 08/08/95 */

/* Modified further for ThinkPad-720C by Uri Blumenthal */
/*                    (uri@watson.ibm.com) Sep 11, 1995 */

/* TODO : 
   + Timeouts
   + Get disk parameters
   + DMA above 16MB
   + reset after read/write error
 */

#define DEVICE_NAME "PS/2 ESDI"

#include <linux/config.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/genhd.h>
#include <linux/ps2esdi.h>
#include <linux/blkdev.h>
#include <linux/mca-legacy.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/module.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/mca_dma.h>
#include <asm/uaccess.h>

#define PS2ESDI_IRQ 14
#define MAX_HD 2
#define MAX_RETRIES 5
#define MAX_16BIT 65536
#define ESDI_TIMEOUT   0xf000
#define ESDI_STAT_TIMEOUT 4

#define TYPE_0_CMD_BLK_LENGTH 2
#define TYPE_1_CMD_BLK_LENGTH 4

static void reset_ctrl(void);

static int ps2esdi_geninit(void);

static void do_ps2esdi_request(request_queue_t * q);

static void ps2esdi_readwrite(int cmd, struct request *req);

static void ps2esdi_fill_cmd_block(u_short * cmd_blk, u_short cmd,
u_short cyl, u_short head, u_short sector, u_short length, u_char drive);

static int ps2esdi_out_cmd_blk(u_short * cmd_blk);

static void ps2esdi_prep_dma(char *buffer, u_short length, u_char dma_xmode);

static irqreturn_t ps2esdi_interrupt_handler(int irq, void *dev_id,
				      struct pt_regs *regs);
static void (*current_int_handler) (u_int) = NULL;
static void ps2esdi_normal_interrupt_handler(u_int);
static void ps2esdi_initial_reset_int_handler(u_int);
static void ps2esdi_geometry_int_handler(u_int);
static int ps2esdi_ioctl(struct inode *inode, struct file *file,
			 u_int cmd, u_long arg);

static int ps2esdi_read_status_words(int num_words, int max_words, u_short * buffer);

static void dump_cmd_complete_status(u_int int_ret_code);

static void ps2esdi_get_device_cfg(void);

static void ps2esdi_reset_timer(unsigned long unused);

static u_int dma_arb_level;		/* DMA arbitration level */

static DECLARE_WAIT_QUEUE_HEAD(ps2esdi_int);

static int no_int_yet;
static int ps2esdi_drives;
static u_short io_base;
static DEFINE_TIMER(esdi_timer, ps2esdi_reset_timer, 0, 0);
static int reset_status;
static int ps2esdi_slot = -1;
static int tp720esdi = 0;	/* Is it Integrated ESDI of ThinkPad-720? */
static int intg_esdi = 0;       /* If integrated adapter */
struct ps2esdi_i_struct {
	unsigned int head, sect, cyl, wpcom, lzone, ctl;
};
static DEFINE_SPINLOCK(ps2esdi_lock);
static struct request_queue *ps2esdi_queue;
static struct request *current_req;

#if 0
#if 0				/* try both - I don't know which one is better... UB */
static struct ps2esdi_i_struct ps2esdi_info[MAX_HD] =
{
	{4, 48, 1553, 0, 0, 0},
	{0, 0, 0, 0, 0, 0}};
#else
static struct ps2esdi_i_struct ps2esdi_info[MAX_HD] =
{
	{64, 32, 161, 0, 0, 0},
	{0, 0, 0, 0, 0, 0}};
#endif
#endif
static struct ps2esdi_i_struct ps2esdi_info[MAX_HD] =
{
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0}};

static struct block_device_operations ps2esdi_fops =
{
	.owner		= THIS_MODULE,
	.ioctl		= ps2esdi_ioctl,
};

static struct gendisk *ps2esdi_gendisk[2];

/* initialization routine called by ll_rw_blk.c   */
static int __init ps2esdi_init(void)
{

	int error = 0;

	/* register the device - pass the name and major number */
	if (register_blkdev(PS2ESDI_MAJOR, "ed"))
		return -EBUSY;

	/* set up some global information - indicating device specific info */
	ps2esdi_queue = blk_init_queue(do_ps2esdi_request, &ps2esdi_lock);
	if (!ps2esdi_queue) {
		unregister_blkdev(PS2ESDI_MAJOR, "ed");
		return -ENOMEM;
	}

	/* some minor housekeeping - setup the global gendisk structure */
	error = ps2esdi_geninit();
	if (error) {
		printk(KERN_WARNING "PS2ESDI: error initialising"
			" device, releasing resources\n");
		unregister_blkdev(PS2ESDI_MAJOR, "ed");
		blk_cleanup_queue(ps2esdi_queue);
		return error;
	}
	return 0;
}				/* ps2esdi_init */

#ifndef MODULE

module_init(ps2esdi_init);

#else

static int cyl[MAX_HD] = {-1,-1};
static int head[MAX_HD] = {-1, -1};
static int sect[MAX_HD] = {-1, -1};

module_param(tp720esdi, bool, 0);
module_param_array(cyl, int, NULL, 0);
module_param_array(head, int, NULL, 0);
module_param_array(sect, int, NULL, 0);
MODULE_LICENSE("GPL");

int init_module(void) {
	int drive;

	for(drive = 0; drive < MAX_HD; drive++) {
	        struct ps2esdi_i_struct *info = &ps2esdi_info[drive];

        	if (cyl[drive] != -1) {
		  	info->cyl = info->lzone = cyl[drive];
			info->wpcom = 0;
		}
        	if (head[drive] != -1) {
			info->head = head[drive];
			info->ctl = (head[drive] > 8 ? 8 : 0);
		}
        	if (sect[drive] != -1) info->sect = sect[drive];
	}
	return ps2esdi_init();
}

void
cleanup_module(void) {
	int i;
	if(ps2esdi_slot) {
		mca_mark_as_unused(ps2esdi_slot);
		mca_set_adapter_procfn(ps2esdi_slot, NULL, NULL);
	}
	release_region(io_base, 4);
	free_dma(dma_arb_level);
	free_irq(PS2ESDI_IRQ, &ps2esdi_gendisk);
	unregister_blkdev(PS2ESDI_MAJOR, "ed");
	blk_cleanup_queue(ps2esdi_queue);
	for (i = 0; i < ps2esdi_drives; i++) {
		del_gendisk(ps2esdi_gendisk[i]);
		put_disk(ps2esdi_gendisk[i]);
	}
}
#endif /* MODULE */

/* handles boot time command line parameters */
void __init tp720_setup(char *str, int *ints)
{
	/* no params, just sets the tp720esdi flag if it exists */

	printk("%s: TP 720 ESDI flag set\n", DEVICE_NAME);
	tp720esdi = 1;
}

void __init ed_setup(char *str, int *ints)
{
	int hdind = 0;

	/* handles 3 parameters only - corresponding to
	   1. Number of cylinders
	   2. Number of heads
	   3. Sectors/track
	 */

	if (ints[0] != 3)
		return;

	/* print out the information - seen at boot time */
	printk("%s: ints[0]=%d ints[1]=%d ints[2]=%d ints[3]=%d\n",
	       DEVICE_NAME, ints[0], ints[1], ints[2], ints[3]);

	/* set the index into device specific information table */
	if (ps2esdi_info[0].head != 0)
		hdind = 1;

	/* set up all the device information */
	ps2esdi_info[hdind].head = ints[2];
	ps2esdi_info[hdind].sect = ints[3];
	ps2esdi_info[hdind].cyl = ints[1];
	ps2esdi_info[hdind].wpcom = 0;
	ps2esdi_info[hdind].lzone = ints[1];
	ps2esdi_info[hdind].ctl = (ints[2] > 8 ? 8 : 0);
#if 0				/* this may be needed for PS2/Mod.80, but it hurts ThinkPad! */
	ps2esdi_drives = hdind + 1;	/* increment index for the next time */
#endif
}				/* ed_setup */

static int ps2esdi_getinfo(char *buf, int slot, void *d)
{
	int len = 0;

	len += sprintf(buf + len, "DMA Arbitration Level: %d\n",
		       dma_arb_level);
	len += sprintf(buf + len, "IO Port: %x\n", io_base);
	len += sprintf(buf + len, "IRQ: 14\n");
	len += sprintf(buf + len, "Drives: %d\n", ps2esdi_drives);

	return len;
}

/* ps2 esdi specific initialization - called thru the gendisk chain */
static int __init ps2esdi_geninit(void)
{
	/*
	   The first part contains the initialization code
	   for the ESDI disk subsystem.  All we really do
	   is search for the POS registers of the controller
	   to do some simple setup operations.  First, we
	   must ensure that the controller is installed,
	   enabled, and configured as PRIMARY.  Then we must
	   determine the DMA arbitration level being used by
	   the controller so we can handle data transfer
	   operations properly.  If all of this works, then
	   we will set the INIT_FLAG to a non-zero value.
	 */

	int slot = 0, i, reset_start, reset_end;
	u_char status;
	unsigned short adapterID;
	int error = 0;

	if ((slot = mca_find_adapter(INTG_ESDI_ID, 0)) != MCA_NOTFOUND) {
		adapterID = INTG_ESDI_ID;
		printk("%s: integrated ESDI adapter found in slot %d\n",
		       DEVICE_NAME, slot+1);
#ifndef MODULE
		mca_set_adapter_name(slot, "PS/2 Integrated ESDI");
#endif
	} else if ((slot = mca_find_adapter(NRML_ESDI_ID, 0)) != -1) {
		adapterID = NRML_ESDI_ID;
		printk("%s: normal ESDI adapter found in slot %d\n",
		       DEVICE_NAME, slot+1);
		mca_set_adapter_name(slot, "PS/2 ESDI");
	} else {
		return -ENODEV;
	}

	ps2esdi_slot = slot;
	mca_mark_as_used(slot);
	mca_set_adapter_procfn(slot, (MCA_ProcFn) ps2esdi_getinfo, NULL);

	/* Found the slot - read the POS register 2 to get the necessary
	   configuration and status information.  POS register 2 has the
	   following information :
	   Bit           Function
	   7             reserved = 0
	   6             arbitration method
	   0 - fairness enabled
	   1 - fairness disabled, linear priority assignment
	   5-2           arbitration level
	   1             alternate address
	   1              alternate address
	   0 - use addresses 0x3510 - 0x3517
	   0             adapter enable
	 */

	status = mca_read_stored_pos(slot, 2);
	/* is it enabled ? */
	if (!(status & STATUS_ENABLED)) {
		printk("%s: ESDI adapter disabled\n", DEVICE_NAME);
		error = -ENODEV;
		goto err_out1;
	}
	/* try to grab IRQ, and try to grab a slow IRQ if it fails, so we can
	   share with the SCSI driver */
	if (request_irq(PS2ESDI_IRQ, ps2esdi_interrupt_handler,
		  SA_INTERRUPT | SA_SHIRQ, "PS/2 ESDI", &ps2esdi_gendisk)
	    && request_irq(PS2ESDI_IRQ, ps2esdi_interrupt_handler,
			   SA_SHIRQ, "PS/2 ESDI", &ps2esdi_gendisk)
	    ) {
		printk("%s: Unable to get IRQ %d\n", DEVICE_NAME, PS2ESDI_IRQ);
		error = -EBUSY;
		goto err_out1;
	}
	if (status & STATUS_ALTERNATE)
		io_base = ALT_IO_BASE;
	else
		io_base = PRIMARY_IO_BASE;

	if (!request_region(io_base, 4, "ed")) {
		printk(KERN_WARNING"Unable to request region 0x%x\n", io_base);
		error = -EBUSY;
		goto err_out2;
	}
	/* get the dma arbitration level */
	dma_arb_level = (status >> 2) & 0xf;

	/* BA */
	printk("%s: DMA arbitration level : %d\n",
	       DEVICE_NAME, dma_arb_level);

	LITE_ON;
	current_int_handler = ps2esdi_initial_reset_int_handler;
	reset_ctrl();
	reset_status = 0;
	reset_start = jiffies;
	while (!reset_status) {
		init_timer(&esdi_timer);
		esdi_timer.expires = jiffies + HZ;
		esdi_timer.data = 0;
		add_timer(&esdi_timer);
		sleep_on(&ps2esdi_int);
	}
	reset_end = jiffies;
	LITE_OFF;
	printk("%s: reset interrupt after %d jiffies,  %u.%02u secs\n",
	       DEVICE_NAME, reset_end - reset_start, (reset_end - reset_start) / HZ,
	       (reset_end - reset_start) % HZ);


	/* Integrated ESDI Disk and Controller has only one drive! */
	if (adapterID == INTG_ESDI_ID) {/* if not "normal" PS2 ESDI adapter */
		ps2esdi_drives = 1;	/* then we have only one physical disk! */		intg_esdi = 1;
	}



	/* finally this part sets up some global data structures etc. */

	ps2esdi_get_device_cfg();

	/* some annoyance in the above routine returns TWO drives?
	 Is something else happining in the background?
	 Regaurdless we fix the # of drives again. AJK */
	/* Integrated ESDI Disk and Controller has only one drive! */
	if (adapterID == INTG_ESDI_ID)	/* if not "normal" PS2 ESDI adapter */
		ps2esdi_drives = 1;	/* Not three or two, ONE DAMNIT! */

	current_int_handler = ps2esdi_normal_interrupt_handler;

	if (request_dma(dma_arb_level, "ed") !=0) {
		printk(KERN_WARNING "PS2ESDI: Can't request dma-channel %d\n"
			,(int) dma_arb_level);
		error = -EBUSY;
		goto err_out3;
	}
	blk_queue_max_sectors(ps2esdi_queue, 128);

	error = -ENOMEM;
	for (i = 0; i < ps2esdi_drives; i++) {
		struct gendisk *disk = alloc_disk(64);
		if (!disk)
			goto err_out4;
		disk->major = PS2ESDI_MAJOR;
		disk->first_minor = i<<6;
		sprintf(disk->disk_name, "ed%c", 'a'+i);
		sprintf(disk->devfs_name, "ed/target%d", i);
		disk->fops = &ps2esdi_fops;
		ps2esdi_gendisk[i] = disk;
	}

	for (i = 0; i < ps2esdi_drives; i++) {
		struct gendisk *disk = ps2esdi_gendisk[i];
		set_capacity(disk, ps2esdi_info[i].head * ps2esdi_info[i].sect *
				ps2esdi_info[i].cyl);
		disk->queue = ps2esdi_queue;
		disk->private_data = &ps2esdi_info[i];
		add_disk(disk);
	}
	return 0;
err_out4:
	while (i--)
		put_disk(ps2esdi_gendisk[i]);
err_out3:
	release_region(io_base, 4);
err_out2:
	free_irq(PS2ESDI_IRQ, &ps2esdi_gendisk);
err_out1:
	if(ps2esdi_slot) {
		mca_mark_as_unused(ps2esdi_slot);
		mca_set_adapter_procfn(ps2esdi_slot, NULL, NULL);
	}
	return error;
}

static void __init ps2esdi_get_device_cfg(void)
{
	u_short cmd_blk[TYPE_0_CMD_BLK_LENGTH];

	/*BA */ printk("%s: Drive 0\n", DEVICE_NAME);
	current_int_handler = ps2esdi_geometry_int_handler;
	cmd_blk[0] = CMD_GET_DEV_CONFIG | 0x600;
	cmd_blk[1] = 0;
	no_int_yet = TRUE;
	ps2esdi_out_cmd_blk(cmd_blk);
	if (no_int_yet)
		sleep_on(&ps2esdi_int);

	if (ps2esdi_drives > 1) {
		printk("%s: Drive 1\n", DEVICE_NAME);	/*BA */
		cmd_blk[0] = CMD_GET_DEV_CONFIG | (1 << 5) | 0x600;
		cmd_blk[1] = 0;
		no_int_yet = TRUE;
		ps2esdi_out_cmd_blk(cmd_blk);
		if (no_int_yet)
			sleep_on(&ps2esdi_int);
	}			/* if second physical drive is present */
	return;
}

/* strategy routine that handles most of the IO requests */
static void do_ps2esdi_request(request_queue_t * q)
{
	struct request *req;
	/* since, this routine is called with interrupts cleared - they 
	   must be before it finishes  */

	req = elv_next_request(q);
	if (!req)
		return;

#if 0
	printk("%s:got request. device : %s command : %d  sector : %ld count : %ld, buffer: %p\n",
	       DEVICE_NAME,
	       req->rq_disk->disk_name,
	       req->cmd, req->sector,
	       req->current_nr_sectors, req->buffer);
#endif

	/* check for above 16Mb dmas */
	if (isa_virt_to_bus(req->buffer + req->current_nr_sectors * 512) > 16 * MB) {
		printk("%s: DMA above 16MB not supported\n", DEVICE_NAME);
		end_request(req, FAIL);
		return;
	}

	if (req->sector+req->current_nr_sectors > get_capacity(req->rq_disk)) {
		printk("Grrr. error. ps2esdi_drives: %d, %llu %llu\n",
		    ps2esdi_drives, req->sector,
		    (unsigned long long)get_capacity(req->rq_disk));
		end_request(req, FAIL);
		return;
	}

	switch (rq_data_dir(req)) {
	case READ:
		ps2esdi_readwrite(READ, req);
		break;
	case WRITE:
		ps2esdi_readwrite(WRITE, req);
		break;
	default:
		printk("%s: Unknown command\n", req->rq_disk->disk_name);
		end_request(req, FAIL);
		break;
	}		/* handle different commands */
}				/* main strategy routine */

/* resets the ESDI adapter */
static void reset_ctrl(void)
{

	u_long expire;
	u_short status;

	/* enable interrupts on the controller */
	status = inb(ESDI_INTRPT);
	outb((status & 0xe0) | ATT_EOI, ESDI_ATTN);	/* to be sure we don't have
							   any interrupt pending... */
	outb_p(CTRL_ENABLE_INTR, ESDI_CONTROL);

	/* read the ESDI status port - if the controller is not busy,
	   simply do a soft reset (fast) - otherwise we'll have to do a
	   hard (slow) reset.  */
	if (!(inb_p(ESDI_STATUS) & STATUS_BUSY)) {
		/*BA */ printk("%s: soft reset...\n", DEVICE_NAME);
		outb_p(CTRL_SOFT_RESET, ESDI_ATTN);
	}
	/* soft reset */ 
	else {
		/*BA */
		printk("%s: hard reset...\n", DEVICE_NAME);
		outb_p(CTRL_HARD_RESET, ESDI_CONTROL);
		expire = jiffies + 2*HZ;
		while (time_before(jiffies, expire));
		outb_p(1, ESDI_CONTROL);
	}			/* hard reset */


}				/* reset the controller */

/* called by the strategy routine to handle read and write requests */
static void ps2esdi_readwrite(int cmd, struct request *req)
{
	struct ps2esdi_i_struct *p = req->rq_disk->private_data;
	unsigned block = req->sector;
	unsigned count = req->current_nr_sectors;
	int drive = p - ps2esdi_info;
	u_short track, head, cylinder, sector;
	u_short cmd_blk[TYPE_1_CMD_BLK_LENGTH];

	/* do some relevant arithmatic */
	track = block / p->sect;
	head = track % p->head;
	cylinder = track / p->head;
	sector = block % p->sect;

#if 0
	printk("%s: cyl=%d head=%d sect=%d\n", DEVICE_NAME, cylinder, head, sector);
#endif
	/* call the routine that actually fills out a command block */
	ps2esdi_fill_cmd_block
	    (cmd_blk,
	     (cmd == READ) ? CMD_READ : CMD_WRITE,
	     cylinder, head, sector, count, drive);

	/* send the command block to the controller */
	current_req = req;
	spin_unlock_irq(&ps2esdi_lock);
	if (ps2esdi_out_cmd_blk(cmd_blk)) {
		spin_lock_irq(&ps2esdi_lock);
		printk("%s: Controller failed\n", DEVICE_NAME);
		if ((++req->errors) >= MAX_RETRIES)
			end_request(req, FAIL);
	}
	/* check for failure to put out the command block */ 
	else {
		spin_lock_irq(&ps2esdi_lock);
#if 0
		printk("%s: waiting for xfer\n", DEVICE_NAME);
#endif
		/* turn disk lights on */
		LITE_ON;
	}

}				/* ps2esdi_readwrite */

/* fill out the command block */
static void ps2esdi_fill_cmd_block(u_short * cmd_blk, u_short cmd,
 u_short cyl, u_short head, u_short sector, u_short length, u_char drive)
{

	cmd_blk[0] = (drive << 5) | cmd;
	cmd_blk[1] = length;
	cmd_blk[2] = ((cyl & 0x1f) << 11) | (head << 5) | sector;
	cmd_blk[3] = (cyl & 0x3E0) >> 5;

}				/* fill out the command block */

/* write a command block to the controller */
static int ps2esdi_out_cmd_blk(u_short * cmd_blk)
{

	int i;
	unsigned long jif;
	u_char status;

	/* enable interrupts */
	outb(CTRL_ENABLE_INTR, ESDI_CONTROL);

	/* do not write to the controller, if it is busy */
	for (jif = jiffies + ESDI_STAT_TIMEOUT;
		time_after(jif, jiffies) &&
			(inb(ESDI_STATUS) & STATUS_BUSY); )
		;

#if 0
	printk("%s: i(1)=%ld\n", DEVICE_NAME, jif);
#endif

	/* if device is still busy - then just time out */
	if (inb(ESDI_STATUS) & STATUS_BUSY) {
		printk("%s: ps2esdi_out_cmd timed out (1)\n", DEVICE_NAME);
		return ERROR;
	}			/* timeout ??? */
	/* Set up the attention register in the controller */
	outb(((*cmd_blk) & 0xE0) | 1, ESDI_ATTN);

#if 0
	printk("%s: sending %d words to controller\n", DEVICE_NAME, (((*cmd_blk) >> 14) + 1) << 1);
#endif

	/* one by one send each word out */
	for (i = (((*cmd_blk) >> 14) + 1) << 1; i; i--) {
		status = inb(ESDI_STATUS);
		for (jif = jiffies + ESDI_STAT_TIMEOUT;
		     time_after(jif, jiffies) && (status & STATUS_BUSY) &&
		   (status & STATUS_CMD_INF); status = inb(ESDI_STATUS));
		if ((status & (STATUS_BUSY | STATUS_CMD_INF)) == STATUS_BUSY) {
#if 0
			printk("%s: sending %04X\n", DEVICE_NAME, *cmd_blk);
#endif
			outw(*cmd_blk++, ESDI_CMD_INT);
		} else {
			printk("%s: ps2esdi_out_cmd timed out while sending command (status=%02X)\n",
			       DEVICE_NAME, status);
			return ERROR;
		}
	}			/* send all words out */
	return OK;
}				/* send out the commands */


/* prepare for dma - do all the necessary setup */
static void ps2esdi_prep_dma(char *buffer, u_short length, u_char dma_xmode)
{
	unsigned long flags = claim_dma_lock();

	mca_disable_dma(dma_arb_level);

	mca_set_dma_addr(dma_arb_level, isa_virt_to_bus(buffer));

	mca_set_dma_count(dma_arb_level, length * 512 / 2);

	mca_set_dma_mode(dma_arb_level, dma_xmode);

	mca_enable_dma(dma_arb_level);

	release_dma_lock(flags);

}				/* prepare for dma */



static irqreturn_t ps2esdi_interrupt_handler(int irq, void *dev_id,
				      struct pt_regs *regs)
{
	u_int int_ret_code;

	if (inb(ESDI_STATUS) & STATUS_INTR) {
		int_ret_code = inb(ESDI_INTRPT);
		if (current_int_handler) {
			/* Disable adapter interrupts till processing is finished */
			outb(CTRL_DISABLE_INTR, ESDI_CONTROL);
			current_int_handler(int_ret_code);
		} else
			printk("%s: help ! No interrupt handler.\n", DEVICE_NAME);
	} else {
		return IRQ_NONE;
	}
	return IRQ_HANDLED;
}

static void ps2esdi_initial_reset_int_handler(u_int int_ret_code)
{

	switch (int_ret_code & 0xf) {
	case INT_RESET:
		/*BA */
		printk("%s: initial reset completed.\n", DEVICE_NAME);
		outb((int_ret_code & 0xe0) | ATT_EOI, ESDI_ATTN);
		wake_up(&ps2esdi_int);
		break;
	case INT_ATTN_ERROR:
		printk("%s: Attention error. interrupt status : %02X\n", DEVICE_NAME,
		       int_ret_code);
		printk("%s: status: %02x\n", DEVICE_NAME, inb(ESDI_STATUS));
		break;
	default:
		printk("%s: initial reset handler received interrupt: %02X\n",
		       DEVICE_NAME, int_ret_code);
		outb((int_ret_code & 0xe0) | ATT_EOI, ESDI_ATTN);
		break;
	}
	outb(CTRL_ENABLE_INTR, ESDI_CONTROL);
}


static void ps2esdi_geometry_int_handler(u_int int_ret_code)
{
	u_int status, drive_num;
	unsigned long rba;
	int i;

	drive_num = int_ret_code >> 5;
	switch (int_ret_code & 0xf) {
	case INT_CMD_COMPLETE:
		for (i = ESDI_TIMEOUT; i && !(inb(ESDI_STATUS) & STATUS_STAT_AVAIL); i--);
		if (!(inb(ESDI_STATUS) & STATUS_STAT_AVAIL)) {
			printk("%s: timeout reading status word\n", DEVICE_NAME);
			outb((int_ret_code & 0xe0) | ATT_EOI, ESDI_ATTN);
			break;
		}
		status = inw(ESDI_STT_INT);
		if ((status & 0x1F) == CMD_GET_DEV_CONFIG) {
#define REPLY_WORDS 5		/* we already read word 0 */
			u_short reply[REPLY_WORDS];

			if (ps2esdi_read_status_words((status >> 8) - 1, REPLY_WORDS, reply)) {
				/*BA */
				printk("%s: Device Configuration Status for drive %u\n",
				       DEVICE_NAME, drive_num);

				printk("%s: Spares/cyls: %u", DEVICE_NAME, reply[0] >> 8);

				printk
				    ("Config bits: %s%s%s%s%s\n",
				     (reply[0] & CONFIG_IS) ? "Invalid Secondary, " : "",
				     ((reply[0] & CONFIG_ZD) && !(reply[0] & CONFIG_IS))
				 ? "Zero Defect, " : "Defects Present, ",
				     (reply[0] & CONFIG_SF) ? "Skewed Format, " : "",
				     (reply[0] & CONFIG_FR) ? "Removable, " : "Non-Removable, ",
				     (reply[0] & CONFIG_RT) ? "No Retries" : "Retries");

				rba = reply[1] | ((unsigned long) reply[2] << 16);
				printk("%s: Number of RBA's: %lu\n", DEVICE_NAME, rba);

				printk("%s: Physical number of cylinders: %u, Sectors/Track: %u, Heads: %u\n",
				       DEVICE_NAME, reply[3], reply[4] >> 8, reply[4] & 0xff);

				if (!ps2esdi_info[drive_num].head) {
					ps2esdi_info[drive_num].head = 64;
					ps2esdi_info[drive_num].sect = 32;
					ps2esdi_info[drive_num].cyl = rba / (64 * 32);
					ps2esdi_info[drive_num].wpcom = 0;
					ps2esdi_info[drive_num].lzone = ps2esdi_info[drive_num].cyl;
					ps2esdi_info[drive_num].ctl = 8;
					if (tp720esdi) {	/* store the retrieved parameters */
						ps2esdi_info[0].head = reply[4] & 0Xff;
						ps2esdi_info[0].sect = reply[4] >> 8;
						ps2esdi_info[0].cyl = reply[3];
						ps2esdi_info[0].wpcom = 0;
						ps2esdi_info[0].lzone = reply[3];
					} else {
						if (!intg_esdi)
							ps2esdi_drives++;
					}
				}
#ifdef OBSOLETE
				if (!ps2esdi_info[drive_num].head) {
					ps2esdi_info[drive_num].head = reply[4] & 0Xff;
					ps2esdi_info[drive_num].sect = reply[4] >> 8;
					ps2esdi_info[drive_num].cyl = reply[3];
					ps2esdi_info[drive_num].wpcom = 0;
					ps2esdi_info[drive_num].lzone = reply[3];
					if (tp720esdi) {	/* store the retrieved parameters */
						ps2esdi_info[0].head = reply[4] & 0Xff;
						ps2esdi_info[0].sect = reply[4] >> 8;
						ps2esdi_info[0].cyl = reply[3];
						ps2esdi_info[0].wpcom = 0;
						ps2esdi_info[0].lzone = reply[3];
					} else {
						ps2esdi_drives++;
					}
				}
#endif

			} else
				printk("%s: failed while getting device config\n", DEVICE_NAME);
#undef REPLY_WORDS
		} else
			printk("%s: command %02X unknown by geometry handler\n",
			       DEVICE_NAME, status & 0x1f);

		outb((int_ret_code & 0xe0) | ATT_EOI, ESDI_ATTN);
		break;

	case INT_ATTN_ERROR:
		printk("%s: Attention error. interrupt status : %02X\n", DEVICE_NAME,
		       int_ret_code);
		printk("%s: Device not available\n", DEVICE_NAME);
		break;
	case INT_CMD_ECC:
	case INT_CMD_RETRY:
	case INT_CMD_ECC_RETRY:
	case INT_CMD_WARNING:
	case INT_CMD_ABORT:
	case INT_CMD_FAILED:
	case INT_DMA_ERR:
	case INT_CMD_BLK_ERR:
		/*BA */ printk("%s: Whaa. Error occurred...\n", DEVICE_NAME);
		dump_cmd_complete_status(int_ret_code);
		outb((int_ret_code & 0xe0) | ATT_EOI, ESDI_ATTN);
		break;
	default:
		printk("%s: Unknown interrupt reason: %02X\n",
		       DEVICE_NAME, int_ret_code & 0xf);
		outb((int_ret_code & 0xe0) | ATT_EOI, ESDI_ATTN);
		break;
	}

	wake_up(&ps2esdi_int);
	no_int_yet = FALSE;
	outb(CTRL_ENABLE_INTR, ESDI_CONTROL);

}

static void ps2esdi_normal_interrupt_handler(u_int int_ret_code)
{
	unsigned long flags;
	u_int status;
	u_int ending;
	int i;

	switch (int_ret_code & 0x0f) {
	case INT_TRANSFER_REQ:
		ps2esdi_prep_dma(current_req->buffer,
				 current_req->current_nr_sectors,
		    (rq_data_dir(current_req) == READ)
		    ? MCA_DMA_MODE_16 | MCA_DMA_MODE_WRITE | MCA_DMA_MODE_XFER
		    : MCA_DMA_MODE_16 | MCA_DMA_MODE_READ);
		outb(CTRL_ENABLE_DMA | CTRL_ENABLE_INTR, ESDI_CONTROL);
		ending = -1;
		break;

	case INT_ATTN_ERROR:
		printk("%s: Attention error. interrupt status : %02X\n", DEVICE_NAME,
		       int_ret_code);
		outb(CTRL_ENABLE_INTR, ESDI_CONTROL);
		ending = FAIL;
		break;

	case INT_CMD_COMPLETE:
		for (i = ESDI_TIMEOUT; i && !(inb(ESDI_STATUS) & STATUS_STAT_AVAIL); i--);
		if (!(inb(ESDI_STATUS) & STATUS_STAT_AVAIL)) {
			printk("%s: timeout reading status word\n", DEVICE_NAME);
			outb((int_ret_code & 0xe0) | ATT_EOI, ESDI_ATTN);
			outb(CTRL_ENABLE_INTR, ESDI_CONTROL);
			if ((++current_req->errors) >= MAX_RETRIES)
				ending = FAIL;
			else
				ending = -1;
			break;
		}
		status = inw(ESDI_STT_INT);
		switch (status & 0x1F) {
		case (CMD_READ & 0xff):
		case (CMD_WRITE & 0xff):
			LITE_OFF;
			outb((int_ret_code & 0xe0) | ATT_EOI, ESDI_ATTN);
			outb(CTRL_ENABLE_INTR, ESDI_CONTROL);
			ending = SUCCES;
			break;
		default:
			printk("%s: interrupt for unknown command %02X\n",
			       DEVICE_NAME, status & 0x1f);
			outb((int_ret_code & 0xe0) | ATT_EOI, ESDI_ATTN);
			outb(CTRL_ENABLE_INTR, ESDI_CONTROL);
			ending = -1;
			break;
		}
		break;
	case INT_CMD_ECC:
	case INT_CMD_RETRY:
	case INT_CMD_ECC_RETRY:
		LITE_OFF;
		dump_cmd_complete_status(int_ret_code);
		outb((int_ret_code & 0xe0) | ATT_EOI, ESDI_ATTN);
		outb(CTRL_ENABLE_INTR, ESDI_CONTROL);
		ending = SUCCES;
		break;
	case INT_CMD_WARNING:
	case INT_CMD_ABORT:
	case INT_CMD_FAILED:
	case INT_DMA_ERR:
		LITE_OFF;
		dump_cmd_complete_status(int_ret_code);
		outb((int_ret_code & 0xe0) | ATT_EOI, ESDI_ATTN);
		outb(CTRL_ENABLE_INTR, ESDI_CONTROL);
		if ((++current_req->errors) >= MAX_RETRIES)
			ending = FAIL;
		else
			ending = -1;
		break;

	case INT_CMD_BLK_ERR:
		dump_cmd_complete_status(int_ret_code);
		outb((int_ret_code & 0xe0) | ATT_EOI, ESDI_ATTN);
		outb(CTRL_ENABLE_INTR, ESDI_CONTROL);
		ending = FAIL;
		break;

	case INT_CMD_FORMAT:
		printk("%s: huh ? Who issued this format command ?\n"
		       ,DEVICE_NAME);
		outb((int_ret_code & 0xe0) | ATT_EOI, ESDI_ATTN);
		outb(CTRL_ENABLE_INTR, ESDI_CONTROL);
		ending = -1;
		break;

	case INT_RESET:
		/* BA printk("%s: reset completed.\n", DEVICE_NAME) */ ;
		outb((int_ret_code & 0xe0) | ATT_EOI, ESDI_ATTN);
		outb(CTRL_ENABLE_INTR, ESDI_CONTROL);
		ending = -1;
		break;

	default:
		printk("%s: Unknown interrupt reason: %02X\n",
		       DEVICE_NAME, int_ret_code & 0xf);
		outb((int_ret_code & 0xe0) | ATT_EOI, ESDI_ATTN);
		outb(CTRL_ENABLE_INTR, ESDI_CONTROL);
		ending = -1;
		break;
	}
	if(ending != -1) {
		spin_lock_irqsave(&ps2esdi_lock, flags);
		end_request(current_req, ending);
		current_req = NULL;
		do_ps2esdi_request(ps2esdi_queue);
		spin_unlock_irqrestore(&ps2esdi_lock, flags);
	}
}				/* handle interrupts */



static int ps2esdi_read_status_words(int num_words,
				     int max_words,
				     u_short * buffer)
{
	int i;

	for (; max_words && num_words; max_words--, num_words--, buffer++) {
		for (i = ESDI_TIMEOUT; i && !(inb(ESDI_STATUS) & STATUS_STAT_AVAIL); i--);
		if (!(inb(ESDI_STATUS) & STATUS_STAT_AVAIL)) {
			printk("%s: timeout reading status word\n", DEVICE_NAME);
			return FAIL;
		}
		*buffer = inw(ESDI_STT_INT);
	}
	return SUCCES;
}




static void dump_cmd_complete_status(u_int int_ret_code)
{
#define WAIT_FOR_STATUS \
  for(i=ESDI_TIMEOUT;i && !(inb(ESDI_STATUS) & STATUS_STAT_AVAIL);i--); \
    if(!(inb(ESDI_STATUS) & STATUS_STAT_AVAIL)) { \
    printk("%s: timeout reading status word\n",DEVICE_NAME); \
    return; \
    }

	int i, word_count;
	u_short stat_word;
	u_long rba;

	printk("%s: Device: %u, interrupt ID: %02X\n",
	       DEVICE_NAME, int_ret_code >> 5,
	       int_ret_code & 0xf);

	WAIT_FOR_STATUS;
	stat_word = inw(ESDI_STT_INT);
	word_count = (stat_word >> 8) - 1;
	printk("%s: %u status words, command: %02X\n", DEVICE_NAME, word_count,
	       stat_word & 0xff);

	if (word_count--) {
		WAIT_FOR_STATUS;
		stat_word = inw(ESDI_STT_INT);
		printk("%s: command status code: %02X, command error code: %02X\n",
		       DEVICE_NAME, stat_word >> 8, stat_word & 0xff);
	}
	if (word_count--) {
		WAIT_FOR_STATUS;
		stat_word = inw(ESDI_STT_INT);
		printk("%s: device error code: %s%s%s%s%s,%02X\n", DEVICE_NAME,
		       (stat_word & 0x1000) ? "Ready, " : "Not Ready, ",
		  (stat_word & 0x0800) ? "Selected, " : "Not Selected, ",
		       (stat_word & 0x0400) ? "Write Fault, " : "",
		       (stat_word & 0x0200) ? "Track 0, " : "",
		(stat_word & 0x0100) ? "Seek or command complete, " : "",
		       stat_word >> 8);
	}
	if (word_count--) {
		WAIT_FOR_STATUS;
		stat_word = inw(ESDI_STT_INT);
		printk("%s: Blocks to do: %u", DEVICE_NAME, stat_word);
	}
	if (word_count -= 2) {
		WAIT_FOR_STATUS;
		rba = inw(ESDI_STT_INT);
		WAIT_FOR_STATUS;
		rba |= inw(ESDI_STT_INT) << 16;
		printk(", Last Cyl: %u Head: %u Sector: %u\n",
		       (u_short) ((rba & 0x1ff80000) >> 11),
		 (u_short) ((rba & 0x7E0) >> 5), (u_short) (rba & 0x1f));
	} else
		printk("\n");

	if (word_count--) {
		WAIT_FOR_STATUS;
		stat_word = inw(ESDI_STT_INT);
		printk("%s: Blocks required ECC: %u", DEVICE_NAME, stat_word);
	}
	printk("\n");

#undef WAIT_FOR_STATUS

}

static int ps2esdi_ioctl(struct inode *inode,
			 struct file *file, u_int cmd, u_long arg)
{
	struct ps2esdi_i_struct *p = inode->i_bdev->bd_disk->private_data;
	struct ps2esdi_geometry geom;

	if (cmd != HDIO_GETGEO)
		return -EINVAL;
	memset(&geom, 0, sizeof(geom));
	geom.heads = p->head;
	geom.sectors = p->sect;
	geom.cylinders = p->cyl;
	geom.start = get_start_sect(inode->i_bdev);
	if (copy_to_user((void __user *)arg, &geom, sizeof(geom)))
		return -EFAULT;
	return 0;
}

static void ps2esdi_reset_timer(unsigned long unused)
{

	int status;

	status = inb(ESDI_INTRPT);
	if ((status & 0xf) == INT_RESET) {
		outb((status & 0xe0) | ATT_EOI, ESDI_ATTN);
		outb(CTRL_ENABLE_INTR, ESDI_CONTROL);
		reset_status = 1;
	}
	wake_up(&ps2esdi_int);
}
