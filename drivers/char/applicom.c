/* Derived from Applicom driver ac.c for SCO Unix                            */
/* Ported by David Woodhouse, Axiom (Cambridge) Ltd.                         */
/* dwmw2@infradead.org 30/8/98                                               */
/* $Id: ac.c,v 1.30 2000/03/22 16:03:57 dwmw2 Exp $			     */
/* This module is for Linux 2.1 and 2.2 series kernels.                      */
/*****************************************************************************/
/* J PAGET 18/02/94 passage V2.4.2 ioctl avec code 2 reset to les interrupt  */
/* ceci pour reseter correctement apres une sortie sauvage                   */
/* J PAGET 02/05/94 passage V2.4.3 dans le traitement de d'interruption,     */
/* LoopCount n'etait pas initialise a 0.                                     */
/* F LAFORSE 04/07/95 version V2.6.0 lecture bidon apres acces a une carte   */
/*           pour liberer le bus                                             */
/* J.PAGET 19/11/95 version V2.6.1 Nombre, addresse,irq n'est plus configure */
/* et passe en argument a acinit, mais est scrute sur le bus pour s'adapter  */
/* au nombre de cartes presentes sur le bus. IOCL code 6 affichait V2.4.3    */
/* F.LAFORSE 28/11/95 creation de fichiers acXX.o avec les differentes       */
/* adresses de base des cartes, IOCTL 6 plus complet                         */
/* J.PAGET le 19/08/96 copie de la version V2.6 en V2.8.0 sans modification  */
/* de code autre que le texte V2.6.1 en V2.8.0                               */
/*****************************************************************************/


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/pci.h>
#include <linux/wait.h>
#include <linux/init.h>
#include <linux/fs.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include "applicom.h"


/* NOTE: We use for loops with {write,read}b() instead of 
   memcpy_{from,to}io throughout this driver. This is because
   the board doesn't correctly handle word accesses - only
   bytes. 
*/


#undef DEBUG

#define MAX_BOARD 8		/* maximum of pc board possible */
#define MAX_ISA_BOARD 4
#define LEN_RAM_IO 0x800
#define AC_MINOR 157

#ifndef PCI_VENDOR_ID_APPLICOM
#define PCI_VENDOR_ID_APPLICOM                0x1389
#define PCI_DEVICE_ID_APPLICOM_PCIGENERIC     0x0001
#define PCI_DEVICE_ID_APPLICOM_PCI2000IBS_CAN 0x0002
#define PCI_DEVICE_ID_APPLICOM_PCI2000PFB     0x0003
#endif

static char *applicom_pci_devnames[] = {
	"PCI board",
	"PCI2000IBS / PCI2000CAN",
	"PCI2000PFB"
};

static struct pci_device_id applicom_pci_tbl[] = {
	{ PCI_VDEVICE(APPLICOM, PCI_DEVICE_ID_APPLICOM_PCIGENERIC) },
	{ PCI_VDEVICE(APPLICOM, PCI_DEVICE_ID_APPLICOM_PCI2000IBS_CAN) },
	{ PCI_VDEVICE(APPLICOM, PCI_DEVICE_ID_APPLICOM_PCI2000PFB) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, applicom_pci_tbl);

MODULE_AUTHOR("David Woodhouse & Applicom International");
MODULE_DESCRIPTION("Driver for Applicom Profibus card");
MODULE_LICENSE("GPL");

MODULE_SUPPORTED_DEVICE("ac");


static struct applicom_board {
	unsigned long PhysIO;
	void __iomem *RamIO;
	wait_queue_head_t FlagSleepSend;
	long irq;
	spinlock_t mutex;
} apbs[MAX_BOARD];

static unsigned int irq = 0;	/* interrupt number IRQ       */
static unsigned long mem = 0;	/* physical segment of board  */

module_param(irq, uint, 0);
MODULE_PARM_DESC(irq, "IRQ of the Applicom board");
module_param(mem, ulong, 0);
MODULE_PARM_DESC(mem, "Shared Memory Address of Applicom board");

static unsigned int numboards;	/* number of installed boards */
static volatile unsigned char Dummy;
static DECLARE_WAIT_QUEUE_HEAD(FlagSleepRec);
static unsigned int WriteErrorCount;	/* number of write error      */
static unsigned int ReadErrorCount;	/* number of read error       */
static unsigned int DeviceErrorCount;	/* number of device error     */

static ssize_t ac_read (struct file *, char __user *, size_t, loff_t *);
static ssize_t ac_write (struct file *, const char __user *, size_t, loff_t *);
static int ac_ioctl(struct inode *, struct file *, unsigned int,
		    unsigned long);
static irqreturn_t ac_interrupt(int, void *);

static const struct file_operations ac_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.read = ac_read,
	.write = ac_write,
	.ioctl = ac_ioctl,
};

static struct miscdevice ac_miscdev = {
	AC_MINOR,
	"ac",
	&ac_fops
};

static int dummy;	/* dev_id for request_irq() */

static int ac_register_board(unsigned long physloc, void __iomem *loc, 
		      unsigned char boardno)
{
	volatile unsigned char byte_reset_it;

	if((readb(loc + CONF_END_TEST)     != 0x00) ||
	   (readb(loc + CONF_END_TEST + 1) != 0x55) ||
	   (readb(loc + CONF_END_TEST + 2) != 0xAA) ||
	   (readb(loc + CONF_END_TEST + 3) != 0xFF))
		return 0;

	if (!boardno)
		boardno = readb(loc + NUMCARD_OWNER_TO_PC);

	if (!boardno || boardno > MAX_BOARD) {
		printk(KERN_WARNING "Board #%d (at 0x%lx) is out of range (1 <= x <= %d).\n",
		       boardno, physloc, MAX_BOARD);
		return 0;
	}

	if (apbs[boardno - 1].RamIO) {
		printk(KERN_WARNING "Board #%d (at 0x%lx) conflicts with previous board #%d (at 0x%lx)\n", 
		       boardno, physloc, boardno, apbs[boardno-1].PhysIO);
		return 0;
	}

	boardno--;

	apbs[boardno].PhysIO = physloc;
	apbs[boardno].RamIO = loc;
	init_waitqueue_head(&apbs[boardno].FlagSleepSend);
	spin_lock_init(&apbs[boardno].mutex);
	byte_reset_it = readb(loc + RAM_IT_TO_PC);

	numboards++;
	return boardno + 1;
}

static void __exit applicom_exit(void)
{
	unsigned int i;

	misc_deregister(&ac_miscdev);

	for (i = 0; i < MAX_BOARD; i++) {

		if (!apbs[i].RamIO)
			continue;

		if (apbs[i].irq)
			free_irq(apbs[i].irq, &dummy);

		iounmap(apbs[i].RamIO);
	}
}

static int __init applicom_init(void)
{
	int i, numisa = 0;
	struct pci_dev *dev = NULL;
	void __iomem *RamIO;
	int boardno, ret;

	printk(KERN_INFO "Applicom driver: $Id: ac.c,v 1.30 2000/03/22 16:03:57 dwmw2 Exp $\n");

	/* No mem and irq given - check for a PCI card */

	while ( (dev = pci_get_class(PCI_CLASS_OTHERS << 16, dev))) {

		if (!pci_match_id(applicom_pci_tbl, dev))
			continue;
		
		if (pci_enable_device(dev))
			return -EIO;

		RamIO = ioremap_nocache(pci_resource_start(dev, 0), LEN_RAM_IO);

		if (!RamIO) {
			printk(KERN_INFO "ac.o: Failed to ioremap PCI memory "
				"space at 0x%llx\n",
				(unsigned long long)pci_resource_start(dev, 0));
			pci_disable_device(dev);
			return -EIO;
		}

		printk(KERN_INFO "Applicom %s found at mem 0x%llx, irq %d\n",
		       applicom_pci_devnames[dev->device-1],
			   (unsigned long long)pci_resource_start(dev, 0),
		       dev->irq);

		boardno = ac_register_board(pci_resource_start(dev, 0),
				RamIO, 0);
		if (!boardno) {
			printk(KERN_INFO "ac.o: PCI Applicom device doesn't have correct signature.\n");
			iounmap(RamIO);
			pci_disable_device(dev);
			continue;
		}

		if (request_irq(dev->irq, &ac_interrupt, IRQF_SHARED, "Applicom PCI", &dummy)) {
			printk(KERN_INFO "Could not allocate IRQ %d for PCI Applicom device.\n", dev->irq);
			iounmap(RamIO);
			pci_disable_device(dev);
			apbs[boardno - 1].RamIO = NULL;
			continue;
		}

		/* Enable interrupts. */

		writeb(0x40, apbs[boardno - 1].RamIO + RAM_IT_FROM_PC);

		apbs[boardno - 1].irq = dev->irq;
	}

	/* Finished with PCI cards. If none registered, 
	 * and there was no mem/irq specified, exit */

	if (!mem || !irq) {
		if (numboards)
			goto fin;
		else {
			printk(KERN_INFO "ac.o: No PCI boards found.\n");
			printk(KERN_INFO "ac.o: For an ISA board you must supply memory and irq parameters.\n");
			return -ENXIO;
		}
	}

	/* Now try the specified ISA cards */

	for (i = 0; i < MAX_ISA_BOARD; i++) {
		RamIO = ioremap_nocache(mem + (LEN_RAM_IO * i), LEN_RAM_IO);

		if (!RamIO) {
			printk(KERN_INFO "ac.o: Failed to ioremap the ISA card's memory space (slot #%d)\n", i + 1);
			continue;
		}

		if (!(boardno = ac_register_board((unsigned long)mem+ (LEN_RAM_IO*i),
						  RamIO,i+1))) {
			iounmap(RamIO);
			continue;
		}

		printk(KERN_NOTICE "Applicom ISA card found at mem 0x%lx, irq %d\n", mem + (LEN_RAM_IO*i), irq);

		if (!numisa) {
			if (request_irq(irq, &ac_interrupt, IRQF_SHARED, "Applicom ISA", &dummy)) {
				printk(KERN_WARNING "Could not allocate IRQ %d for ISA Applicom device.\n", irq);
				iounmap(RamIO);
				apbs[boardno - 1].RamIO = NULL;
			}
			else
				apbs[boardno - 1].irq = irq;
		}
		else
			apbs[boardno - 1].irq = 0;

		numisa++;
	}

	if (!numisa)
		printk(KERN_WARNING "ac.o: No valid ISA Applicom boards found "
				"at mem 0x%lx\n", mem);

 fin:
	init_waitqueue_head(&FlagSleepRec);

	WriteErrorCount = 0;
	ReadErrorCount = 0;
	DeviceErrorCount = 0;

	if (numboards) {
		ret = misc_register(&ac_miscdev);
		if (ret) {
			printk(KERN_WARNING "ac.o: Unable to register misc device\n");
			goto out;
		}
		for (i = 0; i < MAX_BOARD; i++) {
			int serial;
			char boardname[(SERIAL_NUMBER - TYPE_CARD) + 1];

			if (!apbs[i].RamIO)
				continue;

			for (serial = 0; serial < SERIAL_NUMBER - TYPE_CARD; serial++)
				boardname[serial] = readb(apbs[i].RamIO + TYPE_CARD + serial);

			boardname[serial] = 0;


			printk(KERN_INFO "Applicom board %d: %s, PROM V%d.%d",
			       i+1, boardname,
			       (int)(readb(apbs[i].RamIO + VERS) >> 4),
			       (int)(readb(apbs[i].RamIO + VERS) & 0xF));
			
			serial = (readb(apbs[i].RamIO + SERIAL_NUMBER) << 16) + 
				(readb(apbs[i].RamIO + SERIAL_NUMBER + 1) << 8) + 
				(readb(apbs[i].RamIO + SERIAL_NUMBER + 2) );

			if (serial != 0)
				printk(" S/N %d\n", serial);
			else
				printk("\n");
		}
		return 0;
	}

	else
		return -ENXIO;

out:
	for (i = 0; i < MAX_BOARD; i++) {
		if (!apbs[i].RamIO)
			continue;
		if (apbs[i].irq)
			free_irq(apbs[i].irq, &dummy);
		iounmap(apbs[i].RamIO);
	}
	pci_disable_device(dev);
	return ret;
}

module_init(applicom_init);
module_exit(applicom_exit);


static ssize_t ac_write(struct file *file, const char __user *buf, size_t count, loff_t * ppos)
{
	unsigned int NumCard;	/* Board number 1 -> 8           */
	unsigned int IndexCard;	/* Index board number 0 -> 7     */
	unsigned char TicCard;	/* Board TIC to send             */
	unsigned long flags;	/* Current priority              */
	struct st_ram_io st_loc;
	struct mailbox tmpmailbox;
#ifdef DEBUG
	int c;
#endif
	DECLARE_WAITQUEUE(wait, current);

	if (count != sizeof(struct st_ram_io) + sizeof(struct mailbox)) {
		static int warncount = 5;
		if (warncount) {
			printk(KERN_INFO "Hmmm. write() of Applicom card, length %zd != expected %zd\n",
			       count, sizeof(struct st_ram_io) + sizeof(struct mailbox));
			warncount--;
		}
		return -EINVAL;
	}

	if(copy_from_user(&st_loc, buf, sizeof(struct st_ram_io))) 
		return -EFAULT;
	
	if(copy_from_user(&tmpmailbox, &buf[sizeof(struct st_ram_io)],
			  sizeof(struct mailbox))) 
		return -EFAULT;

	NumCard = st_loc.num_card;	/* board number to send          */
	TicCard = st_loc.tic_des_from_pc;	/* tic number to send            */
	IndexCard = NumCard - 1;

	if((NumCard < 1) || (NumCard > MAX_BOARD) || !apbs[IndexCard].RamIO)
		return -EINVAL;

#ifdef DEBUG
	printk("Write to applicom card #%d. struct st_ram_io follows:",
	       IndexCard+1);

		for (c = 0; c < sizeof(struct st_ram_io);) {
		
			printk("\n%5.5X: %2.2X", c, ((unsigned char *) &st_loc)[c]);

			for (c++; c % 8 && c < sizeof(struct st_ram_io); c++) {
				printk(" %2.2X", ((unsigned char *) &st_loc)[c]);
			}
		}

		printk("\nstruct mailbox follows:");

		for (c = 0; c < sizeof(struct mailbox);) {
			printk("\n%5.5X: %2.2X", c, ((unsigned char *) &tmpmailbox)[c]);

			for (c++; c % 8 && c < sizeof(struct mailbox); c++) {
				printk(" %2.2X", ((unsigned char *) &tmpmailbox)[c]);
			}
		}

		printk("\n");
#endif

	spin_lock_irqsave(&apbs[IndexCard].mutex, flags);

	/* Test octet ready correct */
	if(readb(apbs[IndexCard].RamIO + DATA_FROM_PC_READY) > 2) { 
		Dummy = readb(apbs[IndexCard].RamIO + VERS);
		spin_unlock_irqrestore(&apbs[IndexCard].mutex, flags);
		printk(KERN_WARNING "APPLICOM driver write error board %d, DataFromPcReady = %d\n",
		       IndexCard,(int)readb(apbs[IndexCard].RamIO + DATA_FROM_PC_READY));
		DeviceErrorCount++;
		return -EIO;
	}
	
	/* Place ourselves on the wait queue */
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&apbs[IndexCard].FlagSleepSend, &wait);

	/* Check whether the card is ready for us */
	while (readb(apbs[IndexCard].RamIO + DATA_FROM_PC_READY) != 0) {
		Dummy = readb(apbs[IndexCard].RamIO + VERS);
		/* It's busy. Sleep. */

		spin_unlock_irqrestore(&apbs[IndexCard].mutex, flags);
		schedule();
		if (signal_pending(current)) {
			remove_wait_queue(&apbs[IndexCard].FlagSleepSend,
					  &wait);
			return -EINTR;
		}
		spin_lock_irqsave(&apbs[IndexCard].mutex, flags);
		set_current_state(TASK_INTERRUPTIBLE);
	}

	/* We may not have actually slept */
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&apbs[IndexCard].FlagSleepSend, &wait);

	writeb(1, apbs[IndexCard].RamIO + DATA_FROM_PC_READY);

	/* Which is best - lock down the pages with rawio and then
	   copy directly, or use bounce buffers? For now we do the latter 
	   because it works with 2.2 still */
	{
		unsigned char *from = (unsigned char *) &tmpmailbox;
		void __iomem *to = apbs[IndexCard].RamIO + RAM_FROM_PC;
		int c;

		for (c = 0; c < sizeof(struct mailbox); c++)
			writeb(*(from++), to++);
	}

	writeb(0x20, apbs[IndexCard].RamIO + TIC_OWNER_FROM_PC);
	writeb(0xff, apbs[IndexCard].RamIO + NUMCARD_OWNER_FROM_PC);
	writeb(TicCard, apbs[IndexCard].RamIO + TIC_DES_FROM_PC);
	writeb(NumCard, apbs[IndexCard].RamIO + NUMCARD_DES_FROM_PC);
	writeb(2, apbs[IndexCard].RamIO + DATA_FROM_PC_READY);
	writeb(1, apbs[IndexCard].RamIO + RAM_IT_FROM_PC);
	Dummy = readb(apbs[IndexCard].RamIO + VERS);
	spin_unlock_irqrestore(&apbs[IndexCard].mutex, flags);
	return 0;
}

static int do_ac_read(int IndexCard, char __user *buf,
		struct st_ram_io *st_loc, struct mailbox *mailbox)
{
	void __iomem *from = apbs[IndexCard].RamIO + RAM_TO_PC;
	unsigned char *to = (unsigned char *)&mailbox;
#ifdef DEBUG
	int c;
#endif

	st_loc->tic_owner_to_pc = readb(apbs[IndexCard].RamIO + TIC_OWNER_TO_PC);
	st_loc->numcard_owner_to_pc = readb(apbs[IndexCard].RamIO + NUMCARD_OWNER_TO_PC);


	{
		int c;

		for (c = 0; c < sizeof(struct mailbox); c++)
			*(to++) = readb(from++);
	}
	writeb(1, apbs[IndexCard].RamIO + ACK_FROM_PC_READY);
	writeb(1, apbs[IndexCard].RamIO + TYP_ACK_FROM_PC);
	writeb(IndexCard+1, apbs[IndexCard].RamIO + NUMCARD_ACK_FROM_PC);
	writeb(readb(apbs[IndexCard].RamIO + TIC_OWNER_TO_PC), 
	       apbs[IndexCard].RamIO + TIC_ACK_FROM_PC);
	writeb(2, apbs[IndexCard].RamIO + ACK_FROM_PC_READY);
	writeb(0, apbs[IndexCard].RamIO + DATA_TO_PC_READY);
	writeb(2, apbs[IndexCard].RamIO + RAM_IT_FROM_PC);
	Dummy = readb(apbs[IndexCard].RamIO + VERS);

#ifdef DEBUG
		printk("Read from applicom card #%d. struct st_ram_io follows:", NumCard);

		for (c = 0; c < sizeof(struct st_ram_io);) {
			printk("\n%5.5X: %2.2X", c, ((unsigned char *)st_loc)[c]);

			for (c++; c % 8 && c < sizeof(struct st_ram_io); c++) {
				printk(" %2.2X", ((unsigned char *)st_loc)[c]);
			}
		}

		printk("\nstruct mailbox follows:");

		for (c = 0; c < sizeof(struct mailbox);) {
			printk("\n%5.5X: %2.2X", c, ((unsigned char *)mailbox)[c]);

			for (c++; c % 8 && c < sizeof(struct mailbox); c++) {
				printk(" %2.2X", ((unsigned char *)mailbox)[c]);
			}
		}
		printk("\n");
#endif
	return (sizeof(struct st_ram_io) + sizeof(struct mailbox));
}

static ssize_t ac_read (struct file *filp, char __user *buf, size_t count, loff_t *ptr)
{
	unsigned long flags;
	unsigned int i;
	unsigned char tmp;
	int ret = 0;
	DECLARE_WAITQUEUE(wait, current);
#ifdef DEBUG
	int loopcount=0;
#endif
	/* No need to ratelimit this. Only root can trigger it anyway */
	if (count != sizeof(struct st_ram_io) + sizeof(struct mailbox)) {
		printk( KERN_WARNING "Hmmm. read() of Applicom card, length %zd != expected %zd\n",
			count,sizeof(struct st_ram_io) + sizeof(struct mailbox));
		return -EINVAL;
	}
	
	while(1) {
		/* Stick ourself on the wait queue */
		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&FlagSleepRec, &wait);
		
		/* Scan each board, looking for one which has a packet for us */
		for (i=0; i < MAX_BOARD; i++) {
			if (!apbs[i].RamIO)
				continue;
			spin_lock_irqsave(&apbs[i].mutex, flags);
			
			tmp = readb(apbs[i].RamIO + DATA_TO_PC_READY);
			
			if (tmp == 2) {
				struct st_ram_io st_loc;
				struct mailbox mailbox;

				/* Got a packet for us */
				ret = do_ac_read(i, buf, &st_loc, &mailbox);
				spin_unlock_irqrestore(&apbs[i].mutex, flags);
				set_current_state(TASK_RUNNING);
				remove_wait_queue(&FlagSleepRec, &wait);

				if (copy_to_user(buf, &st_loc, sizeof(st_loc)))
					return -EFAULT;
				if (copy_to_user(buf + sizeof(st_loc), &mailbox, sizeof(mailbox)))
					return -EFAULT;
				return tmp;
			}
			
			if (tmp > 2) {
				/* Got an error */
				Dummy = readb(apbs[i].RamIO + VERS);
				
				spin_unlock_irqrestore(&apbs[i].mutex, flags);
				set_current_state(TASK_RUNNING);
				remove_wait_queue(&FlagSleepRec, &wait);
				
				printk(KERN_WARNING "APPLICOM driver read error board %d, DataToPcReady = %d\n",
				       i,(int)readb(apbs[i].RamIO + DATA_TO_PC_READY));
				DeviceErrorCount++;
				return -EIO;
			}
			
			/* Nothing for us. Try the next board */
			Dummy = readb(apbs[i].RamIO + VERS);
			spin_unlock_irqrestore(&apbs[i].mutex, flags);
			
		} /* per board */

		/* OK - No boards had data for us. Sleep now */

		schedule();
		remove_wait_queue(&FlagSleepRec, &wait);

		if (signal_pending(current))
			return -EINTR;

#ifdef DEBUG
		if (loopcount++ > 2) {
			printk(KERN_DEBUG "Looping in ac_read. loopcount %d\n", loopcount);
		}
#endif
	} 
}

static irqreturn_t ac_interrupt(int vec, void *dev_instance)
{
	unsigned int i;
	unsigned int FlagInt;
	unsigned int LoopCount;
	int handled = 0;

	//    printk("Applicom interrupt on IRQ %d occurred\n", vec);

	LoopCount = 0;

	do {
		FlagInt = 0;
		for (i = 0; i < MAX_BOARD; i++) {
			
			/* Skip if this board doesn't exist */
			if (!apbs[i].RamIO)
				continue;

			spin_lock(&apbs[i].mutex);

			/* Skip if this board doesn't want attention */
			if(readb(apbs[i].RamIO + RAM_IT_TO_PC) == 0) {
				spin_unlock(&apbs[i].mutex);
				continue;
			}

			handled = 1;
			FlagInt = 1;
			writeb(0, apbs[i].RamIO + RAM_IT_TO_PC);

			if (readb(apbs[i].RamIO + DATA_TO_PC_READY) > 2) {
				printk(KERN_WARNING "APPLICOM driver interrupt err board %d, DataToPcReady = %d\n",
				       i+1,(int)readb(apbs[i].RamIO + DATA_TO_PC_READY));
				DeviceErrorCount++;
			}

			if((readb(apbs[i].RamIO + DATA_FROM_PC_READY) > 2) && 
			   (readb(apbs[i].RamIO + DATA_FROM_PC_READY) != 6)) {
				
				printk(KERN_WARNING "APPLICOM driver interrupt err board %d, DataFromPcReady = %d\n",
				       i+1,(int)readb(apbs[i].RamIO + DATA_FROM_PC_READY));
				DeviceErrorCount++;
			}

			if (readb(apbs[i].RamIO + DATA_TO_PC_READY) == 2) {	/* mailbox sent by the card ?   */
				if (waitqueue_active(&FlagSleepRec)) {
				wake_up_interruptible(&FlagSleepRec);
			}
			}

			if (readb(apbs[i].RamIO + DATA_FROM_PC_READY) == 0) {	/* ram i/o free for write by pc ? */
				if (waitqueue_active(&apbs[i].FlagSleepSend)) {	/* process sleep during read ?    */
					wake_up_interruptible(&apbs[i].FlagSleepSend);
				}
			}
			Dummy = readb(apbs[i].RamIO + VERS);

			if(readb(apbs[i].RamIO + RAM_IT_TO_PC)) {
				/* There's another int waiting on this card */
				spin_unlock(&apbs[i].mutex);
				i--;
			} else {
				spin_unlock(&apbs[i].mutex);
			}
		}
		if (FlagInt)
			LoopCount = 0;
		else
			LoopCount++;
	} while(LoopCount < 2);
	return IRQ_RETVAL(handled);
}



static int ac_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
     
{				/* @ ADG ou ATO selon le cas */
	int i;
	unsigned char IndexCard;
	void __iomem *pmem;
	int ret = 0;
	volatile unsigned char byte_reset_it;
	struct st_ram_io *adgl;
	void __user *argp = (void __user *)arg;

	/* In general, the device is only openable by root anyway, so we're not
	   particularly concerned that bogus ioctls can flood the console. */

	adgl = kmalloc(sizeof(struct st_ram_io), GFP_KERNEL);
	if (!adgl)
		return -ENOMEM;

	if (copy_from_user(adgl, argp, sizeof(struct st_ram_io))) {
		kfree(adgl);
		return -EFAULT;
	}
	
	IndexCard = adgl->num_card-1;
	 
	if(cmd != 0 && cmd != 6 &&
	   ((IndexCard >= MAX_BOARD) || !apbs[IndexCard].RamIO)) {
		static int warncount = 10;
		if (warncount) {
			printk( KERN_WARNING "APPLICOM driver IOCTL, bad board number %d\n",(int)IndexCard+1);
			warncount--;
		}
		kfree(adgl);
		return -EINVAL;
	}

	switch (cmd) {
		
	case 0:
		pmem = apbs[IndexCard].RamIO;
		for (i = 0; i < sizeof(struct st_ram_io); i++)
			((unsigned char *)adgl)[i]=readb(pmem++);
		if (copy_to_user(argp, adgl, sizeof(struct st_ram_io)))
			ret = -EFAULT;
		break;
	case 1:
		pmem = apbs[IndexCard].RamIO + CONF_END_TEST;
		for (i = 0; i < 4; i++)
			adgl->conf_end_test[i] = readb(pmem++);
		for (i = 0; i < 2; i++)
			adgl->error_code[i] = readb(pmem++);
		for (i = 0; i < 4; i++)
			adgl->parameter_error[i] = readb(pmem++);
		pmem = apbs[IndexCard].RamIO + VERS;
		adgl->vers = readb(pmem);
		pmem = apbs[IndexCard].RamIO + TYPE_CARD;
		for (i = 0; i < 20; i++)
			adgl->reserv1[i] = readb(pmem++);
		*(int *)&adgl->reserv1[20] =  
			(readb(apbs[IndexCard].RamIO + SERIAL_NUMBER) << 16) + 
			(readb(apbs[IndexCard].RamIO + SERIAL_NUMBER + 1) << 8) + 
			(readb(apbs[IndexCard].RamIO + SERIAL_NUMBER + 2) );

		if (copy_to_user(argp, adgl, sizeof(struct st_ram_io)))
			ret = -EFAULT;
		break;
	case 2:
		pmem = apbs[IndexCard].RamIO + CONF_END_TEST;
		for (i = 0; i < 10; i++)
			writeb(0xff, pmem++);
		writeb(adgl->data_from_pc_ready, 
		       apbs[IndexCard].RamIO + DATA_FROM_PC_READY);

		writeb(1, apbs[IndexCard].RamIO + RAM_IT_FROM_PC);
		
		for (i = 0; i < MAX_BOARD; i++) {
			if (apbs[i].RamIO) {
				byte_reset_it = readb(apbs[i].RamIO + RAM_IT_TO_PC);
			}
		}
		break;
	case 3:
		pmem = apbs[IndexCard].RamIO + TIC_DES_FROM_PC;
		writeb(adgl->tic_des_from_pc, pmem);
		break;
	case 4:
		pmem = apbs[IndexCard].RamIO + TIC_OWNER_TO_PC;
		adgl->tic_owner_to_pc     = readb(pmem++);
		adgl->numcard_owner_to_pc = readb(pmem);
		if (copy_to_user(argp, adgl,sizeof(struct st_ram_io)))
			ret = -EFAULT;
		break;
	case 5:
		writeb(adgl->num_card, apbs[IndexCard].RamIO + NUMCARD_OWNER_TO_PC);
		writeb(adgl->num_card, apbs[IndexCard].RamIO + NUMCARD_DES_FROM_PC);
		writeb(adgl->num_card, apbs[IndexCard].RamIO + NUMCARD_ACK_FROM_PC);
		writeb(4, apbs[IndexCard].RamIO + DATA_FROM_PC_READY);
		writeb(1, apbs[IndexCard].RamIO + RAM_IT_FROM_PC);
		break;
	case 6:
		printk(KERN_INFO "APPLICOM driver release .... V2.8.0 ($Revision: 1.30 $)\n");
		printk(KERN_INFO "Number of installed boards . %d\n", (int) numboards);
		printk(KERN_INFO "Segment of board ........... %X\n", (int) mem);
		printk(KERN_INFO "Interrupt IRQ number ....... %d\n", (int) irq);
		for (i = 0; i < MAX_BOARD; i++) {
			int serial;
			char boardname[(SERIAL_NUMBER - TYPE_CARD) + 1];

			if (!apbs[i].RamIO)
				continue;

			for (serial = 0; serial < SERIAL_NUMBER - TYPE_CARD; serial++)
				boardname[serial] = readb(apbs[i].RamIO + TYPE_CARD + serial);
			boardname[serial] = 0;

			printk(KERN_INFO "Prom version board %d ....... V%d.%d %s",
			       i+1,
			       (int)(readb(apbs[IndexCard].RamIO + VERS) >> 4),
			       (int)(readb(apbs[IndexCard].RamIO + VERS) & 0xF),
			       boardname);


			serial = (readb(apbs[i].RamIO + SERIAL_NUMBER) << 16) + 
				(readb(apbs[i].RamIO + SERIAL_NUMBER + 1) << 8) + 
				(readb(apbs[i].RamIO + SERIAL_NUMBER + 2) );

			if (serial != 0)
				printk(" S/N %d\n", serial);
			else
				printk("\n");
		}
		if (DeviceErrorCount != 0)
			printk(KERN_INFO "DeviceErrorCount ........... %d\n", DeviceErrorCount);
		if (ReadErrorCount != 0)
			printk(KERN_INFO "ReadErrorCount ............. %d\n", ReadErrorCount);
		if (WriteErrorCount != 0)
			printk(KERN_INFO "WriteErrorCount ............ %d\n", WriteErrorCount);
		if (waitqueue_active(&FlagSleepRec))
			printk(KERN_INFO "Process in read pending\n");
		for (i = 0; i < MAX_BOARD; i++) {
			if (apbs[i].RamIO && waitqueue_active(&apbs[i].FlagSleepSend))
				printk(KERN_INFO "Process in write pending board %d\n",i+1);
		}
		break;
	default:
		printk(KERN_INFO "APPLICOM driver ioctl, unknown function code %d\n",cmd) ;
		ret = -EINVAL;
		break;
	}
	Dummy = readb(apbs[IndexCard].RamIO + VERS);
	kfree(adgl);
	return 0;
}

