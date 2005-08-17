/*
 * IBM eServer Hypervisor Virtual Console Server Device Driver
 * Copyright (C) 2003, 2004 IBM Corp.
 *  Ryan S. Arnold (rsa@us.ibm.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Author(s) :  Ryan S. Arnold <rsa@us.ibm.com>
 *
 * This is the device driver for the IBM Hypervisor Virtual Console Server,
 * "hvcs".  The IBM hvcs provides a tty driver interface to allow Linux
 * user space applications access to the system consoles of logically
 * partitioned operating systems, e.g. Linux, running on the same partitioned
 * Power5 ppc64 system.  Physical hardware consoles per partition are not
 * practical on this hardware so system consoles are accessed by this driver
 * using inter-partition firmware interfaces to virtual terminal devices.
 *
 * A vty is known to the HMC as a "virtual serial server adapter".  It is a
 * virtual terminal device that is created by firmware upon partition creation
 * to act as a partitioned OS's console device.
 *
 * Firmware dynamically (via hotplug) exposes vty-servers to a running ppc64
 * Linux system upon their creation by the HMC or their exposure during boot.
 * The non-user interactive backend of this driver is implemented as a vio
 * device driver so that it can receive notification of vty-server lifetimes
 * after it registers with the vio bus to handle vty-server probe and remove
 * callbacks.
 *
 * Many vty-servers can be configured to connect to one vty, but a vty can
 * only be actively connected to by a single vty-server, in any manner, at one
 * time.  If the HMC is currently hosting the console for a target Linux
 * partition; attempts to open the tty device to the partition's console using
 * the hvcs on any partition will return -EBUSY with every open attempt until
 * the HMC frees the connection between its vty-server and the desired
 * partition's vty device.  Conversely, a vty-server may only be connected to
 * a single vty at one time even though it may have several configured vty
 * partner possibilities.
 *
 * Firmware does not provide notification of vty partner changes to this
 * driver.  This means that an HMC Super Admin may add or remove partner vtys
 * from a vty-server's partner list but the changes will not be signaled to
 * the vty-server.  Firmware only notifies the driver when a vty-server is
 * added or removed from the system.  To compensate for this deficiency, this
 * driver implements a sysfs update attribute which provides a method for
 * rescanning partner information upon a user's request.
 *
 * Each vty-server, prior to being exposed to this driver is reference counted
 * using the 2.6 Linux kernel kobject construct.  This kobject is also used by
 * the vio bus to provide a vio device sysfs entry that this driver attaches
 * device specific attributes to, including partner information.  The vio bus
 * framework also provides a sysfs entry for each vio driver.  The hvcs driver
 * provides driver attributes in this entry.
 *
 * For direction on installation and usage of this driver please reference
 * Documentation/powerpc/hvcs.txt.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/major.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/stat.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <asm/hvconsole.h>
#include <asm/hvcserver.h>
#include <asm/uaccess.h>
#include <asm/vio.h>

/*
 * 1.3.0 -> 1.3.1 In hvcs_open memset(..,0x00,..) instead of memset(..,0x3F,00).
 * Removed braces around single statements following conditionals.  Removed '=
 * 0' after static int declarations since these default to zero.  Removed
 * list_for_each_safe() and replaced with list_for_each_entry() in
 * hvcs_get_by_index().  The 'safe' version is un-needed now that the driver is
 * using spinlocks.  Changed spin_lock_irqsave() to spin_lock() when locking
 * hvcs_structs_lock and hvcs_pi_lock since these are not touched in an int
 * handler.  Initialized hvcs_structs_lock and hvcs_pi_lock to
 * SPIN_LOCK_UNLOCKED at declaration time rather than in hvcs_module_init().
 * Added spin_lock around list_del() in destroy_hvcs_struct() to protect the
 * list traversals from a deletion.  Removed '= NULL' from pointer declaration
 * statements since they are initialized NULL by default.  Removed wmb()
 * instances from hvcs_try_write().  They probably aren't needed with locking in
 * place.  Added check and cleanup for hvcs_pi_buff = kmalloc() in
 * hvcs_module_init().  Exposed hvcs_struct.index via a sysfs attribute so that
 * the coupling between /dev/hvcs* and a vty-server can be automatically
 * determined.  Moved kobject_put() in hvcs_open outside of the
 * spin_unlock_irqrestore().
 *
 * 1.3.1 -> 1.3.2 Changed method for determining hvcs_struct->index and had it
 * align with how the tty layer always assigns the lowest index available.  This
 * change resulted in a list of ints that denotes which indexes are available.
 * Device additions and removals use the new hvcs_get_index() and
 * hvcs_return_index() helper functions.  The list is created with
 * hvsc_alloc_index_list() and it is destroyed with hvcs_free_index_list().
 * Without these fixes hotplug vty-server adapter support goes crazy with this
 * driver if the user removes a vty-server adapter.  Moved free_irq() outside of
 * the hvcs_final_close() function in order to get it out of the spinlock.
 * Rearranged hvcs_close().  Cleaned up some printks and did some housekeeping
 * on the changelog.  Removed local CLC_LENGTH and used HVCS_CLC_LENGTH from
 * arch/ppc64/hvcserver.h.
 *
 * 1.3.2 -> 1.3.3 Replaced yield() in hvcs_close() with tty_wait_until_sent() to
 * prevent possible lockup with realtime scheduling as similarily pointed out by
 * akpm in hvc_console.  Changed resulted in the removal of hvcs_final_close()
 * to reorder cleanup operations and prevent discarding of pending data during
 * an hvcs_close().  Removed spinlock protection of hvcs_struct data members in
 * hvcs_write_room() and hvcs_chars_in_buffer() because they aren't needed.
 */

#define HVCS_DRIVER_VERSION "1.3.3"

MODULE_AUTHOR("Ryan S. Arnold <rsa@us.ibm.com>");
MODULE_DESCRIPTION("IBM hvcs (Hypervisor Virtual Console Server) Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(HVCS_DRIVER_VERSION);

/*
 * Wait this long per iteration while trying to push buffered data to the
 * hypervisor before allowing the tty to complete a close operation.
 */
#define HVCS_CLOSE_WAIT (HZ/100) /* 1/10 of a second */

/*
 * Since the Linux TTY code does not currently (2-04-2004) support dynamic
 * addition of tty derived devices and we shouldn't allocate thousands of
 * tty_device pointers when the number of vty-server & vty partner connections
 * will most often be much lower than this, we'll arbitrarily allocate
 * HVCS_DEFAULT_SERVER_ADAPTERS tty_structs and cdev's by default when we
 * register the tty_driver. This can be overridden using an insmod parameter.
 */
#define HVCS_DEFAULT_SERVER_ADAPTERS	64

/*
 * The user can't insmod with more than HVCS_MAX_SERVER_ADAPTERS hvcs device
 * nodes as a sanity check.  Theoretically there can be over 1 Billion
 * vty-server & vty partner connections.
 */
#define HVCS_MAX_SERVER_ADAPTERS	1024

/*
 * We let Linux assign us a major number and we start the minors at zero.  There
 * is no intuitive mapping between minor number and the target vty-server
 * adapter except that each new vty-server adapter is always assigned to the
 * smallest minor number available.
 */
#define HVCS_MINOR_START	0

/*
 * The hcall interface involves putting 8 chars into each of two registers.
 * We load up those 2 registers (in arch/ppc64/hvconsole.c) by casting char[16]
 * to long[2].  It would work without __ALIGNED__, but a little (tiny) bit
 * slower because an unaligned load is slower than aligned load.
 */
#define __ALIGNED__	__attribute__((__aligned__(8)))

/*
 * How much data can firmware send with each hvc_put_chars()?  Maybe this
 * should be moved into an architecture specific area.
 */
#define HVCS_BUFF_LEN	16

/*
 * This is the maximum amount of data we'll let the user send us (hvcs_write) at
 * once in a chunk as a sanity check.
 */
#define HVCS_MAX_FROM_USER	4096

/*
 * Be careful when adding flags to this line discipline.  Don't add anything
 * that will cause echoing or we'll go into recursive loop echoing chars back
 * and forth with the console drivers.
 */
static struct termios hvcs_tty_termios = {
	.c_iflag = IGNBRK | IGNPAR,
	.c_oflag = OPOST,
	.c_cflag = B38400 | CS8 | CREAD | HUPCL,
	.c_cc = INIT_C_CC
};

/*
 * This value is used to take the place of a command line parameter when the
 * module is inserted.  It starts as -1 and stays as such if the user doesn't
 * specify a module insmod parameter.  If they DO specify one then it is set to
 * the value of the integer passed in.
 */
static int hvcs_parm_num_devs = -1;
module_param(hvcs_parm_num_devs, int, 0);

char hvcs_driver_name[] = "hvcs";
char hvcs_device_node[] = "hvcs";
char hvcs_driver_string[]
	= "IBM hvcs (Hypervisor Virtual Console Server) Driver";

/* Status of partner info rescan triggered via sysfs. */
static int hvcs_rescan_status;

static struct tty_driver *hvcs_tty_driver;

/*
 * In order to be somewhat sane this driver always associates the hvcs_struct
 * index element with the numerically equal tty->index.  This means that a
 * hotplugged vty-server adapter will always map to the lowest index valued
 * device node.  If vty-servers were hotplug removed from the system and then
 * new ones added the new vty-server may have the largest slot number of all
 * the vty-server adapters in the partition but it may have the lowest dev node
 * index of all the adapters due to the hole left by the hotplug removed
 * adapter.  There are a set of functions provided to get the lowest index for
 * a new device as well as return the index to the list.  This list is allocated
 * with a number of elements equal to the number of device nodes requested when
 * the module was inserted.
 */
static int *hvcs_index_list;

/*
 * How large is the list?  This is kept for traversal since the list is
 * dynamically created.
 */
static int hvcs_index_count;

/*
 * Used by the khvcsd to pick up I/O operations when the kernel_thread is
 * already awake but potentially shifted to TASK_INTERRUPTIBLE state.
 */
static int hvcs_kicked;

/*
 * Use by the kthread construct for task operations like waking the sleeping
 * thread and stopping the kthread.
 */
static struct task_struct *hvcs_task;

/*
 * We allocate this for the use of all of the hvcs_structs when they fetch
 * partner info.
 */
static unsigned long *hvcs_pi_buff;

/* Only allow one hvcs_struct to use the hvcs_pi_buff at a time. */
static DEFINE_SPINLOCK(hvcs_pi_lock);

/* One vty-server per hvcs_struct */
struct hvcs_struct {
	spinlock_t lock;

	/*
	 * This index identifies this hvcs device as the complement to a
	 * specific tty index.
	 */
	unsigned int index;

	struct tty_struct *tty;
	unsigned int open_count;

	/*
	 * Used to tell the driver kernel_thread what operations need to take
	 * place upon this hvcs_struct instance.
	 */
	int todo_mask;

	/*
	 * This buffer is required so that when hvcs_write_room() reports that
	 * it can send HVCS_BUFF_LEN characters that it will buffer the full
	 * HVCS_BUFF_LEN characters if need be.  This is essential for opost
	 * writes since they do not do high level buffering and expect to be
	 * able to send what the driver commits to sending buffering
	 * [e.g. tab to space conversions in n_tty.c opost()].
	 */
	char buffer[HVCS_BUFF_LEN];
	int chars_in_buffer;

	/*
	 * Any variable below the kobject is valid before a tty is connected and
	 * stays valid after the tty is disconnected.  These shouldn't be
	 * whacked until the koject refcount reaches zero though some entries
	 * may be changed via sysfs initiatives.
	 */
	struct kobject kobj; /* ref count & hvcs_struct lifetime */
	int connected; /* is the vty-server currently connected to a vty? */
	uint32_t p_unit_address; /* partner unit address */
	uint32_t p_partition_ID; /* partner partition ID */
	char p_location_code[HVCS_CLC_LENGTH + 1]; /* CLC + Null Term */
	struct list_head next; /* list management */
	struct vio_dev *vdev;
};

/* Required to back map a kobject to its containing object */
#define from_kobj(kobj) container_of(kobj, struct hvcs_struct, kobj)

static struct list_head hvcs_structs = LIST_HEAD_INIT(hvcs_structs);
static DEFINE_SPINLOCK(hvcs_structs_lock);

static void hvcs_unthrottle(struct tty_struct *tty);
static void hvcs_throttle(struct tty_struct *tty);
static irqreturn_t hvcs_handle_interrupt(int irq, void *dev_instance,
		struct pt_regs *regs);

static int hvcs_write(struct tty_struct *tty,
		const unsigned char *buf, int count);
static int hvcs_write_room(struct tty_struct *tty);
static int hvcs_chars_in_buffer(struct tty_struct *tty);

static int hvcs_has_pi(struct hvcs_struct *hvcsd);
static void hvcs_set_pi(struct hvcs_partner_info *pi,
		struct hvcs_struct *hvcsd);
static int hvcs_get_pi(struct hvcs_struct *hvcsd);
static int hvcs_rescan_devices_list(void);

static int hvcs_partner_connect(struct hvcs_struct *hvcsd);
static void hvcs_partner_free(struct hvcs_struct *hvcsd);

static int hvcs_enable_device(struct hvcs_struct *hvcsd,
		uint32_t unit_address, unsigned int irq, struct vio_dev *dev);

static void destroy_hvcs_struct(struct kobject *kobj);
static int hvcs_open(struct tty_struct *tty, struct file *filp);
static void hvcs_close(struct tty_struct *tty, struct file *filp);
static void hvcs_hangup(struct tty_struct * tty);

static void hvcs_create_device_attrs(struct hvcs_struct *hvcsd);
static void hvcs_remove_device_attrs(struct vio_dev *vdev);
static void hvcs_create_driver_attrs(void);
static void hvcs_remove_driver_attrs(void);

static int __devinit hvcs_probe(struct vio_dev *dev,
		const struct vio_device_id *id);
static int __devexit hvcs_remove(struct vio_dev *dev);
static int __init hvcs_module_init(void);
static void __exit hvcs_module_exit(void);

#define HVCS_SCHED_READ	0x00000001
#define HVCS_QUICK_READ	0x00000002
#define HVCS_TRY_WRITE	0x00000004
#define HVCS_READ_MASK	(HVCS_SCHED_READ | HVCS_QUICK_READ)

static void hvcs_kick(void)
{
	hvcs_kicked = 1;
	wmb();
	wake_up_process(hvcs_task);
}

static void hvcs_unthrottle(struct tty_struct *tty)
{
	struct hvcs_struct *hvcsd = tty->driver_data;
	unsigned long flags;

	spin_lock_irqsave(&hvcsd->lock, flags);
	hvcsd->todo_mask |= HVCS_SCHED_READ;
	spin_unlock_irqrestore(&hvcsd->lock, flags);
	hvcs_kick();
}

static void hvcs_throttle(struct tty_struct *tty)
{
	struct hvcs_struct *hvcsd = tty->driver_data;
	unsigned long flags;

	spin_lock_irqsave(&hvcsd->lock, flags);
	vio_disable_interrupts(hvcsd->vdev);
	spin_unlock_irqrestore(&hvcsd->lock, flags);
}

/*
 * If the device is being removed we don't have to worry about this interrupt
 * handler taking any further interrupts because they are disabled which means
 * the hvcs_struct will always be valid in this handler.
 */
static irqreturn_t hvcs_handle_interrupt(int irq, void *dev_instance,
		struct pt_regs *regs)
{
	struct hvcs_struct *hvcsd = dev_instance;

	spin_lock(&hvcsd->lock);
	vio_disable_interrupts(hvcsd->vdev);
	hvcsd->todo_mask |= HVCS_SCHED_READ;
	spin_unlock(&hvcsd->lock);
	hvcs_kick();

	return IRQ_HANDLED;
}

/* This function must be called with the hvcsd->lock held */
static void hvcs_try_write(struct hvcs_struct *hvcsd)
{
	uint32_t unit_address = hvcsd->vdev->unit_address;
	struct tty_struct *tty = hvcsd->tty;
	int sent;

	if (hvcsd->todo_mask & HVCS_TRY_WRITE) {
		/* won't send partial writes */
		sent = hvc_put_chars(unit_address,
				&hvcsd->buffer[0],
				hvcsd->chars_in_buffer );
		if (sent > 0) {
			hvcsd->chars_in_buffer = 0;
			/* wmb(); */
			hvcsd->todo_mask &= ~(HVCS_TRY_WRITE);
			/* wmb(); */

			/*
			 * We are still obligated to deliver the data to the
			 * hypervisor even if the tty has been closed because
			 * we commited to delivering it.  But don't try to wake
			 * a non-existent tty.
			 */
			if (tty) {
				tty_wakeup(tty);
			}
		}
	}
}

static int hvcs_io(struct hvcs_struct *hvcsd)
{
	uint32_t unit_address;
	struct tty_struct *tty;
	char buf[HVCS_BUFF_LEN] __ALIGNED__;
	unsigned long flags;
	int got = 0;
	int i;

	spin_lock_irqsave(&hvcsd->lock, flags);

	unit_address = hvcsd->vdev->unit_address;
	tty = hvcsd->tty;

	hvcs_try_write(hvcsd);

	if (!tty || test_bit(TTY_THROTTLED, &tty->flags)) {
		hvcsd->todo_mask &= ~(HVCS_READ_MASK);
		goto bail;
	} else if (!(hvcsd->todo_mask & (HVCS_READ_MASK)))
		goto bail;

	/* remove the read masks */
	hvcsd->todo_mask &= ~(HVCS_READ_MASK);

	if ((tty->flip.count + HVCS_BUFF_LEN) < TTY_FLIPBUF_SIZE) {
		got = hvc_get_chars(unit_address,
				&buf[0],
				HVCS_BUFF_LEN);
		for (i=0;got && i<got;i++)
			tty_insert_flip_char(tty, buf[i], TTY_NORMAL);
	}

	/* Give the TTY time to process the data we just sent. */
	if (got)
		hvcsd->todo_mask |= HVCS_QUICK_READ;

	spin_unlock_irqrestore(&hvcsd->lock, flags);
	if (tty->flip.count) {
		/* This is synch because tty->low_latency == 1 */
		tty_flip_buffer_push(tty);
	}

	if (!got) {
		/* Do this _after_ the flip_buffer_push */
		spin_lock_irqsave(&hvcsd->lock, flags);
		vio_enable_interrupts(hvcsd->vdev);
		spin_unlock_irqrestore(&hvcsd->lock, flags);
	}

	return hvcsd->todo_mask;

 bail:
	spin_unlock_irqrestore(&hvcsd->lock, flags);
	return hvcsd->todo_mask;
}

static int khvcsd(void *unused)
{
	struct hvcs_struct *hvcsd;
	int hvcs_todo_mask;

	__set_current_state(TASK_RUNNING);

	do {
		hvcs_todo_mask = 0;
		hvcs_kicked = 0;
		wmb();

		spin_lock(&hvcs_structs_lock);
		list_for_each_entry(hvcsd, &hvcs_structs, next) {
			hvcs_todo_mask |= hvcs_io(hvcsd);
		}
		spin_unlock(&hvcs_structs_lock);

		/*
		 * If any of the hvcs adapters want to try a write or quick read
		 * don't schedule(), yield a smidgen then execute the hvcs_io
		 * thread again for those that want the write.
		 */
		 if (hvcs_todo_mask & (HVCS_TRY_WRITE | HVCS_QUICK_READ)) {
			yield();
			continue;
		}

		set_current_state(TASK_INTERRUPTIBLE);
		if (!hvcs_kicked)
			schedule();
		__set_current_state(TASK_RUNNING);
	} while (!kthread_should_stop());

	return 0;
}

static struct vio_device_id hvcs_driver_table[] __devinitdata= {
	{"serial-server", "hvterm2"},
	{ "", "" }
};
MODULE_DEVICE_TABLE(vio, hvcs_driver_table);

static void hvcs_return_index(int index)
{
	/* Paranoia check */
	if (!hvcs_index_list)
		return;
	if (index < 0 || index >= hvcs_index_count)
		return;
	if (hvcs_index_list[index] == -1)
		return;
	else
		hvcs_index_list[index] = -1;
}

/* callback when the kboject ref count reaches zero */
static void destroy_hvcs_struct(struct kobject *kobj)
{
	struct hvcs_struct *hvcsd = from_kobj(kobj);
	struct vio_dev *vdev;
	unsigned long flags;

	spin_lock(&hvcs_structs_lock);
	spin_lock_irqsave(&hvcsd->lock, flags);

	/* the list_del poisons the pointers */
	list_del(&(hvcsd->next));

	if (hvcsd->connected == 1) {
		hvcs_partner_free(hvcsd);
		printk(KERN_INFO "HVCS: Closed vty-server@%X and"
				" partner vty@%X:%d connection.\n",
				hvcsd->vdev->unit_address,
				hvcsd->p_unit_address,
				(uint32_t)hvcsd->p_partition_ID);
	}
	printk(KERN_INFO "HVCS: Destroyed hvcs_struct for vty-server@%X.\n",
			hvcsd->vdev->unit_address);

	vdev = hvcsd->vdev;
	hvcsd->vdev = NULL;

	hvcsd->p_unit_address = 0;
	hvcsd->p_partition_ID = 0;
	hvcs_return_index(hvcsd->index);
	memset(&hvcsd->p_location_code[0], 0x00, HVCS_CLC_LENGTH + 1);

	spin_unlock_irqrestore(&hvcsd->lock, flags);
	spin_unlock(&hvcs_structs_lock);

	hvcs_remove_device_attrs(vdev);

	kfree(hvcsd);
}

static struct kobj_type hvcs_kobj_type = {
	.release = destroy_hvcs_struct,
};

static int hvcs_get_index(void)
{
	int i;
	/* Paranoia check */
	if (!hvcs_index_list) {
		printk(KERN_ERR "HVCS: hvcs_index_list NOT valid!.\n");
		return -EFAULT;
	}
	/* Find the numerically lowest first free index. */
	for(i = 0; i < hvcs_index_count; i++) {
		if (hvcs_index_list[i] == -1) {
			hvcs_index_list[i] = 0;
			return i;
		}
	}
	return -1;
}

static int __devinit hvcs_probe(
	struct vio_dev *dev,
	const struct vio_device_id *id)
{
	struct hvcs_struct *hvcsd;
	int index;

	if (!dev || !id) {
		printk(KERN_ERR "HVCS: probed with invalid parameter.\n");
		return -EPERM;
	}

	/* early to avoid cleanup on failure */
	index = hvcs_get_index();
	if (index < 0) {
		return -EFAULT;
	}

	hvcsd = kmalloc(sizeof(*hvcsd), GFP_KERNEL);
	if (!hvcsd)
		return -ENODEV;

	/* hvcsd->tty is zeroed out with the memset */
	memset(hvcsd, 0x00, sizeof(*hvcsd));

	spin_lock_init(&hvcsd->lock);
	/* Automatically incs the refcount the first time */
	kobject_init(&hvcsd->kobj);
	/* Set up the callback for terminating the hvcs_struct's life */
	hvcsd->kobj.ktype = &hvcs_kobj_type;

	hvcsd->vdev = dev;
	dev->dev.driver_data = hvcsd;

	hvcsd->index = index;

	/* hvcsd->index = ++hvcs_struct_count; */
	hvcsd->chars_in_buffer = 0;
	hvcsd->todo_mask = 0;
	hvcsd->connected = 0;

	/*
	 * This will populate the hvcs_struct's partner info fields for the
	 * first time.
	 */
	if (hvcs_get_pi(hvcsd)) {
		printk(KERN_ERR "HVCS: Failed to fetch partner"
			" info for vty-server@%X on device probe.\n",
			hvcsd->vdev->unit_address);
	}

	/*
	 * If a user app opens a tty that corresponds to this vty-server before
	 * the hvcs_struct has been added to the devices list then the user app
	 * will get -ENODEV.
	 */

	spin_lock(&hvcs_structs_lock);

	list_add_tail(&(hvcsd->next), &hvcs_structs);

	spin_unlock(&hvcs_structs_lock);

	hvcs_create_device_attrs(hvcsd);

	printk(KERN_INFO "HVCS: vty-server@%X added to the vio bus.\n", dev->unit_address);

	/*
	 * DON'T enable interrupts here because there is no user to receive the
	 * data.
	 */
	return 0;
}

static int __devexit hvcs_remove(struct vio_dev *dev)
{
	struct hvcs_struct *hvcsd = dev->dev.driver_data;
	unsigned long flags;
	struct kobject *kobjp;
	struct tty_struct *tty;

	if (!hvcsd)
		return -ENODEV;

	/* By this time the vty-server won't be getting any more interrups */

	spin_lock_irqsave(&hvcsd->lock, flags);

	tty = hvcsd->tty;

	kobjp = &hvcsd->kobj;

	spin_unlock_irqrestore(&hvcsd->lock, flags);

	/*
	 * Let the last holder of this object cause it to be removed, which
	 * would probably be tty_hangup below.
	 */
	kobject_put (kobjp);

	/*
	 * The hangup is a scheduled function which will auto chain call
	 * hvcs_hangup.  The tty should always be valid at this time unless a
	 * simultaneous tty close already cleaned up the hvcs_struct.
	 */
	if (tty)
		tty_hangup(tty);

	printk(KERN_INFO "HVCS: vty-server@%X removed from the"
			" vio bus.\n", dev->unit_address);
	return 0;
};

static struct vio_driver hvcs_vio_driver = {
	.name		= hvcs_driver_name,
	.id_table	= hvcs_driver_table,
	.probe		= hvcs_probe,
	.remove		= hvcs_remove,
};

/* Only called from hvcs_get_pi please */
static void hvcs_set_pi(struct hvcs_partner_info *pi, struct hvcs_struct *hvcsd)
{
	int clclength;

	hvcsd->p_unit_address = pi->unit_address;
	hvcsd->p_partition_ID  = pi->partition_ID;
	clclength = strlen(&pi->location_code[0]);
	if (clclength > HVCS_CLC_LENGTH)
		clclength = HVCS_CLC_LENGTH;

	/* copy the null-term char too */
	strncpy(&hvcsd->p_location_code[0],
			&pi->location_code[0], clclength + 1);
}

/*
 * Traverse the list and add the partner info that is found to the hvcs_struct
 * struct entry. NOTE: At this time I know that partner info will return a
 * single entry but in the future there may be multiple partner info entries per
 * vty-server and you'll want to zero out that list and reset it.  If for some
 * reason you have an old version of this driver but there IS more than one
 * partner info then hvcsd->p_* will hold the last partner info data from the
 * firmware query.  A good way to update this code would be to replace the three
 * partner info fields in hvcs_struct with a list of hvcs_partner_info
 * instances.
 *
 * This function must be called with the hvcsd->lock held.
 */
static int hvcs_get_pi(struct hvcs_struct *hvcsd)
{
	struct hvcs_partner_info *pi;
	uint32_t unit_address = hvcsd->vdev->unit_address;
	struct list_head head;
	int retval;

	spin_lock(&hvcs_pi_lock);
	if (!hvcs_pi_buff) {
		spin_unlock(&hvcs_pi_lock);
		return -EFAULT;
	}
	retval = hvcs_get_partner_info(unit_address, &head, hvcs_pi_buff);
	spin_unlock(&hvcs_pi_lock);
	if (retval) {
		printk(KERN_ERR "HVCS: Failed to fetch partner"
			" info for vty-server@%x.\n", unit_address);
		return retval;
	}

	/* nixes the values if the partner vty went away */
	hvcsd->p_unit_address = 0;
	hvcsd->p_partition_ID = 0;

	list_for_each_entry(pi, &head, node)
		hvcs_set_pi(pi, hvcsd);

	hvcs_free_partner_info(&head);
	return 0;
}

/*
 * This function is executed by the driver "rescan" sysfs entry.  It shouldn't
 * be executed elsewhere, in order to prevent deadlock issues.
 */
static int hvcs_rescan_devices_list(void)
{
	struct hvcs_struct *hvcsd;
	unsigned long flags;

	spin_lock(&hvcs_structs_lock);

	list_for_each_entry(hvcsd, &hvcs_structs, next) {
		spin_lock_irqsave(&hvcsd->lock, flags);
		hvcs_get_pi(hvcsd);
		spin_unlock_irqrestore(&hvcsd->lock, flags);
	}

	spin_unlock(&hvcs_structs_lock);

	return 0;
}

/*
 * Farm this off into its own function because it could be more complex once
 * multiple partners support is added. This function should be called with
 * the hvcsd->lock held.
 */
static int hvcs_has_pi(struct hvcs_struct *hvcsd)
{
	if ((!hvcsd->p_unit_address) || (!hvcsd->p_partition_ID))
		return 0;
	return 1;
}

/*
 * NOTE: It is possible that the super admin removed a partner vty and then
 * added a different vty as the new partner.
 *
 * This function must be called with the hvcsd->lock held.
 */
static int hvcs_partner_connect(struct hvcs_struct *hvcsd)
{
	int retval;
	unsigned int unit_address = hvcsd->vdev->unit_address;

	/*
	 * If there wasn't any pi when the device was added it doesn't meant
	 * there isn't any now.  This driver isn't notified when a new partner
	 * vty is added to a vty-server so we discover changes on our own.
	 * Please see comments in hvcs_register_connection() for justification
	 * of this bizarre code.
	 */
	retval = hvcs_register_connection(unit_address,
			hvcsd->p_partition_ID,
			hvcsd->p_unit_address);
	if (!retval) {
		hvcsd->connected = 1;
		return 0;
	} else if (retval != -EINVAL)
		return retval;

	/*
	 * As per the spec re-get the pi and try again if -EINVAL after the
	 * first connection attempt.
	 */
	if (hvcs_get_pi(hvcsd))
		return -ENOMEM;

	if (!hvcs_has_pi(hvcsd))
		return -ENODEV;

	retval = hvcs_register_connection(unit_address,
			hvcsd->p_partition_ID,
			hvcsd->p_unit_address);
	if (retval != -EINVAL) {
		hvcsd->connected = 1;
		return retval;
	}

	/*
	 * EBUSY is the most likely scenario though the vty could have been
	 * removed or there really could be an hcall error due to the parameter
	 * data but thanks to ambiguous firmware return codes we can't really
	 * tell.
	 */
	printk(KERN_INFO "HVCS: vty-server or partner"
			" vty is busy.  Try again later.\n");
	return -EBUSY;
}

/* This function must be called with the hvcsd->lock held */
static void hvcs_partner_free(struct hvcs_struct *hvcsd)
{
	int retval;
	do {
		retval = hvcs_free_connection(hvcsd->vdev->unit_address);
	} while (retval == -EBUSY);
	hvcsd->connected = 0;
}

/* This helper function must be called WITHOUT the hvcsd->lock held */
static int hvcs_enable_device(struct hvcs_struct *hvcsd, uint32_t unit_address,
		unsigned int irq, struct vio_dev *vdev)
{
	unsigned long flags;
	int rc;

	/*
	 * It is possible that the vty-server was removed between the time that
	 * the conn was registered and now.
	 */
	if (!(rc = request_irq(irq, &hvcs_handle_interrupt,
				SA_INTERRUPT, "ibmhvcs", hvcsd))) {
		/*
		 * It is possible the vty-server was removed after the irq was
		 * requested but before we have time to enable interrupts.
		 */
		if (vio_enable_interrupts(vdev) == H_Success)
			return 0;
		else {
			printk(KERN_ERR "HVCS: int enable failed for"
					" vty-server@%X.\n", unit_address);
			free_irq(irq, hvcsd);
		}
	} else
		printk(KERN_ERR "HVCS: irq req failed for"
				" vty-server@%X.\n", unit_address);

	spin_lock_irqsave(&hvcsd->lock, flags);
	hvcs_partner_free(hvcsd);
	spin_unlock_irqrestore(&hvcsd->lock, flags);

	return rc;

}

/*
 * This always increments the kobject ref count if the call is successful.
 * Please remember to dec when you are done with the instance.
 *
 * NOTICE: Do NOT hold either the hvcs_struct.lock or hvcs_structs_lock when
 * calling this function or you will get deadlock.
 */
struct hvcs_struct *hvcs_get_by_index(int index)
{
	struct hvcs_struct *hvcsd = NULL;
	unsigned long flags;

	spin_lock(&hvcs_structs_lock);
	/* We can immediately discard OOB requests */
	if (index >= 0 && index < HVCS_MAX_SERVER_ADAPTERS) {
		list_for_each_entry(hvcsd, &hvcs_structs, next) {
			spin_lock_irqsave(&hvcsd->lock, flags);
			if (hvcsd->index == index) {
				kobject_get(&hvcsd->kobj);
				spin_unlock_irqrestore(&hvcsd->lock, flags);
				spin_unlock(&hvcs_structs_lock);
				return hvcsd;
			}
			spin_unlock_irqrestore(&hvcsd->lock, flags);
		}
		hvcsd = NULL;
	}

	spin_unlock(&hvcs_structs_lock);
	return hvcsd;
}

/*
 * This is invoked via the tty_open interface when a user app connects to the
 * /dev node.
 */
static int hvcs_open(struct tty_struct *tty, struct file *filp)
{
	struct hvcs_struct *hvcsd;
	int rc, retval = 0;
	unsigned long flags;
	unsigned int irq;
	struct vio_dev *vdev;
	unsigned long unit_address;
	struct kobject *kobjp;

	if (tty->driver_data)
		goto fast_open;

	/*
	 * Is there a vty-server that shares the same index?
	 * This function increments the kobject index.
	 */
	if (!(hvcsd = hvcs_get_by_index(tty->index))) {
		printk(KERN_WARNING "HVCS: open failed, no device associated"
				" with tty->index %d.\n", tty->index);
		return -ENODEV;
	}

	spin_lock_irqsave(&hvcsd->lock, flags);

	if (hvcsd->connected == 0)
		if ((retval = hvcs_partner_connect(hvcsd)))
			goto error_release;

	hvcsd->open_count = 1;
	hvcsd->tty = tty;
	tty->driver_data = hvcsd;

	/*
	 * Set this driver to low latency so that we actually have a chance at
	 * catching a throttled TTY after we flip_buffer_push.  Otherwise the
	 * flush_to_async may not execute until after the kernel_thread has
	 * yielded and resumed the next flip_buffer_push resulting in data
	 * loss.
	 */
	tty->low_latency = 1;

	memset(&hvcsd->buffer[0], 0x00, HVCS_BUFF_LEN);

	/*
	 * Save these in the spinlock for the enable operations that need them
	 * outside of the spinlock.
	 */
	irq = hvcsd->vdev->irq;
	vdev = hvcsd->vdev;
	unit_address = hvcsd->vdev->unit_address;

	hvcsd->todo_mask |= HVCS_SCHED_READ;
	spin_unlock_irqrestore(&hvcsd->lock, flags);

	/*
	 * This must be done outside of the spinlock because it requests irqs
	 * and will grab the spinlock and free the connection if it fails.
	 */
	if (((rc = hvcs_enable_device(hvcsd, unit_address, irq, vdev)))) {
		kobject_put(&hvcsd->kobj);
		printk(KERN_WARNING "HVCS: enable device failed.\n");
		return rc;
	}

	goto open_success;

fast_open:
	hvcsd = tty->driver_data;

	spin_lock_irqsave(&hvcsd->lock, flags);
	if (!kobject_get(&hvcsd->kobj)) {
		spin_unlock_irqrestore(&hvcsd->lock, flags);
		printk(KERN_ERR "HVCS: Kobject of open"
			" hvcs doesn't exist.\n");
		return -EFAULT; /* Is this the right return value? */
	}

	hvcsd->open_count++;

	hvcsd->todo_mask |= HVCS_SCHED_READ;
	spin_unlock_irqrestore(&hvcsd->lock, flags);
open_success:
	hvcs_kick();

	printk(KERN_INFO "HVCS: vty-server@%X connection opened.\n",
		hvcsd->vdev->unit_address );

	return 0;

error_release:
	kobjp = &hvcsd->kobj;
	spin_unlock_irqrestore(&hvcsd->lock, flags);
	kobject_put(&hvcsd->kobj);

	printk(KERN_WARNING "HVCS: partner connect failed.\n");
	return retval;
}

static void hvcs_close(struct tty_struct *tty, struct file *filp)
{
	struct hvcs_struct *hvcsd;
	unsigned long flags;
	struct kobject *kobjp;
	int irq = NO_IRQ;

	/*
	 * Is someone trying to close the file associated with this device after
	 * we have hung up?  If so tty->driver_data wouldn't be valid.
	 */
	if (tty_hung_up_p(filp))
		return;

	/*
	 * No driver_data means that this close was probably issued after a
	 * failed hvcs_open by the tty layer's release_dev() api and we can just
	 * exit cleanly.
	 */
	if (!tty->driver_data)
		return;

	hvcsd = tty->driver_data;

	spin_lock_irqsave(&hvcsd->lock, flags);
	kobjp = &hvcsd->kobj;
	if (--hvcsd->open_count == 0) {

		vio_disable_interrupts(hvcsd->vdev);

		/*
		 * NULL this early so that the kernel_thread doesn't try to
		 * execute any operations on the TTY even though it is obligated
		 * to deliver any pending I/O to the hypervisor.
		 */
		hvcsd->tty = NULL;

		irq = hvcsd->vdev->irq;
		spin_unlock_irqrestore(&hvcsd->lock, flags);

		tty_wait_until_sent(tty, HVCS_CLOSE_WAIT);

		/*
		 * This line is important because it tells hvcs_open that this
		 * device needs to be re-configured the next time hvcs_open is
		 * called.
		 */
		tty->driver_data = NULL;

		free_irq(irq, hvcsd);
		kobject_put(kobjp);
		return;
	} else if (hvcsd->open_count < 0) {
		printk(KERN_ERR "HVCS: vty-server@%X open_count: %d"
				" is missmanaged.\n",
		hvcsd->vdev->unit_address, hvcsd->open_count);
	}

	spin_unlock_irqrestore(&hvcsd->lock, flags);
	kobject_put(kobjp);
}

static void hvcs_hangup(struct tty_struct * tty)
{
	struct hvcs_struct *hvcsd = tty->driver_data;
	unsigned long flags;
	int temp_open_count;
	struct kobject *kobjp;
	int irq = NO_IRQ;

	spin_lock_irqsave(&hvcsd->lock, flags);
	/* Preserve this so that we know how many kobject refs to put */
	temp_open_count = hvcsd->open_count;

	/*
	 * Don't kobject put inside the spinlock because the destruction
	 * callback may use the spinlock and it may get called before the
	 * spinlock has been released.  Get a pointer to the kobject and
	 * kobject_put on that after releasing the spinlock.
	 */
	kobjp = &hvcsd->kobj;

	vio_disable_interrupts(hvcsd->vdev);

	hvcsd->todo_mask = 0;

	/* I don't think the tty needs the hvcs_struct pointer after a hangup */
	hvcsd->tty->driver_data = NULL;
	hvcsd->tty = NULL;

	hvcsd->open_count = 0;

	/* This will drop any buffered data on the floor which is OK in a hangup
	 * scenario. */
	memset(&hvcsd->buffer[0], 0x00, HVCS_BUFF_LEN);
	hvcsd->chars_in_buffer = 0;

	irq = hvcsd->vdev->irq;

	spin_unlock_irqrestore(&hvcsd->lock, flags);

	free_irq(irq, hvcsd);

	/*
	 * We need to kobject_put() for every open_count we have since the
	 * tty_hangup() function doesn't invoke a close per open connection on a
	 * non-console device.
	 */
	while(temp_open_count) {
		--temp_open_count;
		/*
		 * The final put will trigger destruction of the hvcs_struct.
		 * NOTE:  If this hangup was signaled from user space then the
		 * final put will never happen.
		 */
		kobject_put(kobjp);
	}
}

/*
 * NOTE: This is almost always from_user since user level apps interact with the
 * /dev nodes. I'm trusting that if hvcs_write gets called and interrupted by
 * hvcs_remove (which removes the target device and executes tty_hangup()) that
 * tty_hangup will allow hvcs_write time to complete execution before it
 * terminates our device.
 */
static int hvcs_write(struct tty_struct *tty,
		const unsigned char *buf, int count)
{
	struct hvcs_struct *hvcsd = tty->driver_data;
	unsigned int unit_address;
	const unsigned char *charbuf;
	unsigned long flags;
	int total_sent = 0;
	int tosend = 0;
	int result = 0;

	/*
	 * If they don't check the return code off of their open they may
	 * attempt this even if there is no connected device.
	 */
	if (!hvcsd)
		return -ENODEV;

	/* Reasonable size to prevent user level flooding */
	if (count > HVCS_MAX_FROM_USER) {
		printk(KERN_WARNING "HVCS write: count being truncated to"
				" HVCS_MAX_FROM_USER.\n");
		count = HVCS_MAX_FROM_USER;
	}

	charbuf = buf;

	spin_lock_irqsave(&hvcsd->lock, flags);

	/*
	 * Somehow an open succedded but the device was removed or the
	 * connection terminated between the vty-server and partner vty during
	 * the middle of a write operation?  This is a crummy place to do this
	 * but we want to keep it all in the spinlock.
	 */
	if (hvcsd->open_count <= 0) {
		spin_unlock_irqrestore(&hvcsd->lock, flags);
		return -ENODEV;
	}

	unit_address = hvcsd->vdev->unit_address;

	while (count > 0) {
		tosend = min(count, (HVCS_BUFF_LEN - hvcsd->chars_in_buffer));
		/*
		 * No more space, this probably means that the last call to
		 * hvcs_write() didn't succeed and the buffer was filled up.
		 */
		if (!tosend)
			break;

		memcpy(&hvcsd->buffer[hvcsd->chars_in_buffer],
				&charbuf[total_sent],
				tosend);

		hvcsd->chars_in_buffer += tosend;

		result = 0;

		/*
		 * If this is true then we don't want to try writing to the
		 * hypervisor because that is the kernel_threads job now.  We'll
		 * just add to the buffer.
		 */
		if (!(hvcsd->todo_mask & HVCS_TRY_WRITE))
			/* won't send partial writes */
			result = hvc_put_chars(unit_address,
					&hvcsd->buffer[0],
					hvcsd->chars_in_buffer);

		/*
		 * Since we know we have enough room in hvcsd->buffer for
		 * tosend we record that it was sent regardless of whether the
		 * hypervisor actually took it because we have it buffered.
		 */
		total_sent+=tosend;
		count-=tosend;
		if (result == 0) {
			hvcsd->todo_mask |= HVCS_TRY_WRITE;
			hvcs_kick();
			break;
		}

		hvcsd->chars_in_buffer = 0;
		/*
		 * Test after the chars_in_buffer reset otherwise this could
		 * deadlock our writes if hvc_put_chars fails.
		 */
		if (result < 0)
			break;
	}

	spin_unlock_irqrestore(&hvcsd->lock, flags);

	if (result == -1)
		return -EIO;
	else
		return total_sent;
}

/*
 * This is really asking how much can we guarentee that we can send or that we
 * absolutely WILL BUFFER if we can't send it.  This driver MUST honor the
 * return value, hence the reason for hvcs_struct buffering.
 */
static int hvcs_write_room(struct tty_struct *tty)
{
	struct hvcs_struct *hvcsd = tty->driver_data;

	if (!hvcsd || hvcsd->open_count <= 0)
		return 0;

	return HVCS_BUFF_LEN - hvcsd->chars_in_buffer;
}

static int hvcs_chars_in_buffer(struct tty_struct *tty)
{
	struct hvcs_struct *hvcsd = tty->driver_data;

	return hvcsd->chars_in_buffer;
}

static struct tty_operations hvcs_ops = {
	.open = hvcs_open,
	.close = hvcs_close,
	.hangup = hvcs_hangup,
	.write = hvcs_write,
	.write_room = hvcs_write_room,
	.chars_in_buffer = hvcs_chars_in_buffer,
	.unthrottle = hvcs_unthrottle,
	.throttle = hvcs_throttle,
};

static int hvcs_alloc_index_list(int n)
{
	int i;
	hvcs_index_list = kmalloc(n * sizeof(hvcs_index_count),GFP_KERNEL);
	if (!hvcs_index_list)
		return -ENOMEM;
	hvcs_index_count = n;
	for(i = 0; i < hvcs_index_count; i++)
		hvcs_index_list[i] = -1;
	return 0;
}

static void hvcs_free_index_list(void)
{
	/* Paranoia check to be thorough. */
	if (hvcs_index_list) {
		kfree(hvcs_index_list);
		hvcs_index_list = NULL;
		hvcs_index_count = 0;
	}
}

static int __init hvcs_module_init(void)
{
	int rc;
	int num_ttys_to_alloc;

	printk(KERN_INFO "Initializing %s\n", hvcs_driver_string);

	/* Has the user specified an overload with an insmod param? */
	if (hvcs_parm_num_devs <= 0 ||
		(hvcs_parm_num_devs > HVCS_MAX_SERVER_ADAPTERS)) {
		num_ttys_to_alloc = HVCS_DEFAULT_SERVER_ADAPTERS;
	} else
		num_ttys_to_alloc = hvcs_parm_num_devs;

	hvcs_tty_driver = alloc_tty_driver(num_ttys_to_alloc);
	if (!hvcs_tty_driver)
		return -ENOMEM;

	if (hvcs_alloc_index_list(num_ttys_to_alloc))
		return -ENOMEM;

	hvcs_tty_driver->owner = THIS_MODULE;

	hvcs_tty_driver->driver_name = hvcs_driver_name;
	hvcs_tty_driver->name = hvcs_device_node;
	hvcs_tty_driver->devfs_name = hvcs_device_node;

	/*
	 * We'll let the system assign us a major number, indicated by leaving
	 * it blank.
	 */

	hvcs_tty_driver->minor_start = HVCS_MINOR_START;
	hvcs_tty_driver->type = TTY_DRIVER_TYPE_SYSTEM;

	/*
	 * We role our own so that we DONT ECHO.  We can't echo because the
	 * device we are connecting to already echoes by default and this would
	 * throw us into a horrible recursive echo-echo-echo loop.
	 */
	hvcs_tty_driver->init_termios = hvcs_tty_termios;
	hvcs_tty_driver->flags = TTY_DRIVER_REAL_RAW;

	tty_set_operations(hvcs_tty_driver, &hvcs_ops);

	/*
	 * The following call will result in sysfs entries that denote the
	 * dynamically assigned major and minor numbers for our devices.
	 */
	if (tty_register_driver(hvcs_tty_driver)) {
		printk(KERN_ERR "HVCS: registration "
			" as a tty driver failed.\n");
		hvcs_free_index_list();
		put_tty_driver(hvcs_tty_driver);
		return -EIO;
	}

	hvcs_pi_buff = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!hvcs_pi_buff) {
		tty_unregister_driver(hvcs_tty_driver);
		hvcs_free_index_list();
		put_tty_driver(hvcs_tty_driver);
		return -ENOMEM;
	}

	hvcs_task = kthread_run(khvcsd, NULL, "khvcsd");
	if (IS_ERR(hvcs_task)) {
		printk(KERN_ERR "HVCS: khvcsd creation failed.  Driver not loaded.\n");
		kfree(hvcs_pi_buff);
		tty_unregister_driver(hvcs_tty_driver);
		hvcs_free_index_list();
		put_tty_driver(hvcs_tty_driver);
		return -EIO;
	}

	rc = vio_register_driver(&hvcs_vio_driver);

	/*
	 * This needs to be done AFTER the vio_register_driver() call or else
	 * the kobjects won't be initialized properly.
	 */
	hvcs_create_driver_attrs();

	printk(KERN_INFO "HVCS: driver module inserted.\n");

	return rc;
}

static void __exit hvcs_module_exit(void)
{
	/*
	 * This driver receives hvcs_remove callbacks for each device upon
	 * module removal.
	 */

	/*
	 * This synchronous operation  will wake the khvcsd kthread if it is
	 * asleep and will return when khvcsd has terminated.
	 */
	kthread_stop(hvcs_task);

	spin_lock(&hvcs_pi_lock);
	kfree(hvcs_pi_buff);
	hvcs_pi_buff = NULL;
	spin_unlock(&hvcs_pi_lock);

	hvcs_remove_driver_attrs();

	vio_unregister_driver(&hvcs_vio_driver);

	tty_unregister_driver(hvcs_tty_driver);

	hvcs_free_index_list();

	put_tty_driver(hvcs_tty_driver);

	printk(KERN_INFO "HVCS: driver module removed.\n");
}

module_init(hvcs_module_init);
module_exit(hvcs_module_exit);

static inline struct hvcs_struct *from_vio_dev(struct vio_dev *viod)
{
	return viod->dev.driver_data;
}
/* The sysfs interface for the driver and devices */

static ssize_t hvcs_partner_vtys_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct vio_dev *viod = to_vio_dev(dev);
	struct hvcs_struct *hvcsd = from_vio_dev(viod);
	unsigned long flags;
	int retval;

	spin_lock_irqsave(&hvcsd->lock, flags);
	retval = sprintf(buf, "%X\n", hvcsd->p_unit_address);
	spin_unlock_irqrestore(&hvcsd->lock, flags);
	return retval;
}
static DEVICE_ATTR(partner_vtys, S_IRUGO, hvcs_partner_vtys_show, NULL);

static ssize_t hvcs_partner_clcs_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct vio_dev *viod = to_vio_dev(dev);
	struct hvcs_struct *hvcsd = from_vio_dev(viod);
	unsigned long flags;
	int retval;

	spin_lock_irqsave(&hvcsd->lock, flags);
	retval = sprintf(buf, "%s\n", &hvcsd->p_location_code[0]);
	spin_unlock_irqrestore(&hvcsd->lock, flags);
	return retval;
}
static DEVICE_ATTR(partner_clcs, S_IRUGO, hvcs_partner_clcs_show, NULL);

static ssize_t hvcs_current_vty_store(struct device *dev, struct device_attribute *attr, const char * buf,
		size_t count)
{
	/*
	 * Don't need this feature at the present time because firmware doesn't
	 * yet support multiple partners.
	 */
	printk(KERN_INFO "HVCS: Denied current_vty change: -EPERM.\n");
	return -EPERM;
}

static ssize_t hvcs_current_vty_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct vio_dev *viod = to_vio_dev(dev);
	struct hvcs_struct *hvcsd = from_vio_dev(viod);
	unsigned long flags;
	int retval;

	spin_lock_irqsave(&hvcsd->lock, flags);
	retval = sprintf(buf, "%s\n", &hvcsd->p_location_code[0]);
	spin_unlock_irqrestore(&hvcsd->lock, flags);
	return retval;
}

static DEVICE_ATTR(current_vty,
	S_IRUGO | S_IWUSR, hvcs_current_vty_show, hvcs_current_vty_store);

static ssize_t hvcs_vterm_state_store(struct device *dev, struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct vio_dev *viod = to_vio_dev(dev);
	struct hvcs_struct *hvcsd = from_vio_dev(viod);
	unsigned long flags;

	/* writing a '0' to this sysfs entry will result in the disconnect. */
	if (simple_strtol(buf, NULL, 0) != 0)
		return -EINVAL;

	spin_lock_irqsave(&hvcsd->lock, flags);

	if (hvcsd->open_count > 0) {
		spin_unlock_irqrestore(&hvcsd->lock, flags);
		printk(KERN_INFO "HVCS: vterm state unchanged.  "
				"The hvcs device node is still in use.\n");
		return -EPERM;
	}

	if (hvcsd->connected == 0) {
		spin_unlock_irqrestore(&hvcsd->lock, flags);
		printk(KERN_INFO "HVCS: vterm state unchanged. The"
				" vty-server is not connected to a vty.\n");
		return -EPERM;
	}

	hvcs_partner_free(hvcsd);
	printk(KERN_INFO "HVCS: Closed vty-server@%X and"
			" partner vty@%X:%d connection.\n",
			hvcsd->vdev->unit_address,
			hvcsd->p_unit_address,
			(uint32_t)hvcsd->p_partition_ID);

	spin_unlock_irqrestore(&hvcsd->lock, flags);
	return count;
}

static ssize_t hvcs_vterm_state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct vio_dev *viod = to_vio_dev(dev);
	struct hvcs_struct *hvcsd = from_vio_dev(viod);
	unsigned long flags;
	int retval;

	spin_lock_irqsave(&hvcsd->lock, flags);
	retval = sprintf(buf, "%d\n", hvcsd->connected);
	spin_unlock_irqrestore(&hvcsd->lock, flags);
	return retval;
}
static DEVICE_ATTR(vterm_state, S_IRUGO | S_IWUSR,
		hvcs_vterm_state_show, hvcs_vterm_state_store);

static ssize_t hvcs_index_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct vio_dev *viod = to_vio_dev(dev);
	struct hvcs_struct *hvcsd = from_vio_dev(viod);
	unsigned long flags;
	int retval;

	spin_lock_irqsave(&hvcsd->lock, flags);
	retval = sprintf(buf, "%d\n", hvcsd->index);
	spin_unlock_irqrestore(&hvcsd->lock, flags);
	return retval;
}

static DEVICE_ATTR(index, S_IRUGO, hvcs_index_show, NULL);

static struct attribute *hvcs_attrs[] = {
	&dev_attr_partner_vtys.attr,
	&dev_attr_partner_clcs.attr,
	&dev_attr_current_vty.attr,
	&dev_attr_vterm_state.attr,
	&dev_attr_index.attr,
	NULL,
};

static struct attribute_group hvcs_attr_group = {
	.attrs = hvcs_attrs,
};

static void hvcs_create_device_attrs(struct hvcs_struct *hvcsd)
{
	struct vio_dev *vdev = hvcsd->vdev;
	sysfs_create_group(&vdev->dev.kobj, &hvcs_attr_group);
}

static void hvcs_remove_device_attrs(struct vio_dev *vdev)
{
	sysfs_remove_group(&vdev->dev.kobj, &hvcs_attr_group);
}

static ssize_t hvcs_rescan_show(struct device_driver *ddp, char *buf)
{
	/* A 1 means it is updating, a 0 means it is done updating */
	return snprintf(buf, PAGE_SIZE, "%d\n", hvcs_rescan_status);
}

static ssize_t hvcs_rescan_store(struct device_driver *ddp, const char * buf,
		size_t count)
{
	if ((simple_strtol(buf, NULL, 0) != 1)
		&& (hvcs_rescan_status != 0))
		return -EINVAL;

	hvcs_rescan_status = 1;
	printk(KERN_INFO "HVCS: rescanning partner info for all"
		" vty-servers.\n");
	hvcs_rescan_devices_list();
	hvcs_rescan_status = 0;
	return count;
}
static DRIVER_ATTR(rescan,
	S_IRUGO | S_IWUSR, hvcs_rescan_show, hvcs_rescan_store);

static void hvcs_create_driver_attrs(void)
{
	struct device_driver *driverfs = &(hvcs_vio_driver.driver);
	driver_create_file(driverfs, &driver_attr_rescan);
}

static void hvcs_remove_driver_attrs(void)
{
	struct device_driver *driverfs = &(hvcs_vio_driver.driver);
	driver_remove_file(driverfs, &driver_attr_rescan);
}
