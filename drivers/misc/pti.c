/*
 *  pti.c - PTI driver for cJTAG data extration
 *
 *  Copyright (C) Intel 2010
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * The PTI (Parallel Trace Interface) driver directs trace data routed from
 * various parts in the system out through the Intel Penwell PTI port and
 * out of the mobile device for analysis with a debugging tool
 * (Lauterbach, Fido). This is part of a solution for the MIPI P1149.7,
 * compact JTAG, standard.
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/pci.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <linux/pti.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define DRIVERNAME		"pti"
#define PCINAME			"pciPTI"
#define TTYNAME			"ttyPTI"
#define CHARNAME		"pti"
#define PTITTY_MINOR_START	0
#define PTITTY_MINOR_NUM	2
#define MAX_APP_IDS		16   /* 128 channel ids / u8 bit size */
#define MAX_OS_IDS		16   /* 128 channel ids / u8 bit size */
#define MAX_MODEM_IDS		16   /* 128 channel ids / u8 bit size */
#define MODEM_BASE_ID		71   /* modem master ID address    */
#define CONTROL_ID		72   /* control master ID address  */
#define CONSOLE_ID		73   /* console master ID address  */
#define OS_BASE_ID		74   /* base OS master ID address  */
#define APP_BASE_ID		80   /* base App master ID address */
#define CONTROL_FRAME_LEN	32   /* PTI control frame maximum size */
#define USER_COPY_SIZE		8192 /* 8Kb buffer for user space copy */
#define APERTURE_14		0x3800000 /* offset to first OS write addr */
#define APERTURE_LEN		0x400000  /* address length */

struct pti_tty {
	struct pti_masterchannel *mc;
};

struct pti_dev {
	struct tty_port port;
	unsigned long pti_addr;
	unsigned long aperture_base;
	void __iomem *pti_ioaddr;
	u8 ia_app[MAX_APP_IDS];
	u8 ia_os[MAX_OS_IDS];
	u8 ia_modem[MAX_MODEM_IDS];
};

/*
 * This protects access to ia_app, ia_os, and ia_modem,
 * which keeps track of channels allocated in
 * an aperture write id.
 */
static DEFINE_MUTEX(alloclock);

static struct pci_device_id pci_ids[] __devinitconst = {
		{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x82B)},
		{0}
};

static struct tty_driver *pti_tty_driver;
static struct pti_dev *drv_data;

static unsigned int pti_console_channel;
static unsigned int pti_control_channel;

/**
 *  pti_write_to_aperture()- The private write function to PTI HW.
 *
 *  @mc: The 'aperture'. It's part of a write address that holds
 *       a master and channel ID.
 *  @buf: Data being written to the HW that will ultimately be seen
 *        in a debugging tool (Fido, Lauterbach).
 *  @len: Size of buffer.
 *
 *  Since each aperture is specified by a unique
 *  master/channel ID, no two processes will be writing
 *  to the same aperture at the same time so no lock is required. The
 *  PTI-Output agent will send these out in the order that they arrived, and
 *  thus, it will intermix these messages. The debug tool can then later
 *  regroup the appropriate message segments together reconstituting each
 *  message.
 */
static void pti_write_to_aperture(struct pti_masterchannel *mc,
				  u8 *buf,
				  int len)
{
	int dwordcnt;
	int final;
	int i;
	u32 ptiword;
	u32 __iomem *aperture;
	u8 *p = buf;

	/*
	 * calculate the aperture offset from the base using the master and
	 * channel id's.
	 */
	aperture = drv_data->pti_ioaddr + (mc->master << 15)
		+ (mc->channel << 8);

	dwordcnt = len >> 2;
	final = len - (dwordcnt << 2);	    /* final = trailing bytes    */
	if (final == 0 && dwordcnt != 0) {  /* always need a final dword */
		final += 4;
		dwordcnt--;
	}

	for (i = 0; i < dwordcnt; i++) {
		ptiword = be32_to_cpu(*(u32 *)p);
		p += 4;
		iowrite32(ptiword, aperture);
	}

	aperture += PTI_LASTDWORD_DTS;	/* adding DTS signals that is EOM */

	ptiword = 0;
	for (i = 0; i < final; i++)
		ptiword |= *p++ << (24-(8*i));

	iowrite32(ptiword, aperture);
	return;
}

/**
 *  pti_control_frame_built_and_sent()- control frame build and send function.
 *
 *  @mc:          The master / channel structure on which the function
 *                built a control frame.
 *  @thread_name: The thread name associated with the master / channel or
 *                'NULL' if using the 'current' global variable.
 *
 *  To be able to post process the PTI contents on host side, a control frame
 *  is added before sending any PTI content. So the host side knows on
 *  each PTI frame the name of the thread using a dedicated master / channel.
 *  The thread name is retrieved from 'current' global variable if 'thread_name'
 *  is 'NULL', else it is retrieved from 'thread_name' parameter.
 *  This function builds this frame and sends it to a master ID CONTROL_ID.
 *  The overhead is only 32 bytes since the driver only writes to HW
 *  in 32 byte chunks.
 */
static void pti_control_frame_built_and_sent(struct pti_masterchannel *mc,
					     const char *thread_name)
{
	/*
	 * Since we access the comm member in current's task_struct, we only
	 * need to be as large as what 'comm' in that structure is.
	 */
	char comm[TASK_COMM_LEN];
	struct pti_masterchannel mccontrol = {.master = CONTROL_ID,
					      .channel = 0};
	const char *thread_name_p;
	const char *control_format = "%3d %3d %s";
	u8 control_frame[CONTROL_FRAME_LEN];

	if (!thread_name) {
		if (!in_interrupt())
			get_task_comm(comm, current);
		else
			strncpy(comm, "Interrupt", TASK_COMM_LEN);

		/* Absolutely ensure our buffer is zero terminated. */
		comm[TASK_COMM_LEN-1] = 0;
		thread_name_p = comm;
	} else {
		thread_name_p = thread_name;
	}

	mccontrol.channel = pti_control_channel;
	pti_control_channel = (pti_control_channel + 1) & 0x7f;

	snprintf(control_frame, CONTROL_FRAME_LEN, control_format, mc->master,
		mc->channel, thread_name_p);
	pti_write_to_aperture(&mccontrol, control_frame, strlen(control_frame));
}

/**
 *  pti_write_full_frame_to_aperture()- high level function to
 *					write to PTI.
 *
 *  @mc:  The 'aperture'. It's part of a write address that holds
 *        a master and channel ID.
 *  @buf: Data being written to the HW that will ultimately be seen
 *        in a debugging tool (Fido, Lauterbach).
 *  @len: Size of buffer.
 *
 *  All threads sending data (either console, user space application, ...)
 *  are calling the high level function to write to PTI meaning that it is
 *  possible to add a control frame before sending the content.
 */
static void pti_write_full_frame_to_aperture(struct pti_masterchannel *mc,
						const unsigned char *buf,
						int len)
{
	pti_control_frame_built_and_sent(mc, NULL);
	pti_write_to_aperture(mc, (u8 *)buf, len);
}

/**
 * get_id()- Allocate a master and channel ID.
 *
 * @id_array:    an array of bits representing what channel
 *               id's are allocated for writing.
 * @max_ids:     The max amount of available write IDs to use.
 * @base_id:     The starting SW channel ID, based on the Intel
 *               PTI arch.
 * @thread_name: The thread name associated with the master / channel or
 *               'NULL' if using the 'current' global variable.
 *
 * Returns:
 *	pti_masterchannel struct with master, channel ID address
 *	0 for error
 *
 * Each bit in the arrays ia_app and ia_os correspond to a master and
 * channel id. The bit is one if the id is taken and 0 if free. For
 * every master there are 128 channel id's.
 */
static struct pti_masterchannel *get_id(u8 *id_array,
					int max_ids,
					int base_id,
					const char *thread_name)
{
	struct pti_masterchannel *mc;
	int i, j, mask;

	mc = kmalloc(sizeof(struct pti_masterchannel), GFP_KERNEL);
	if (mc == NULL)
		return NULL;

	/* look for a byte with a free bit */
	for (i = 0; i < max_ids; i++)
		if (id_array[i] != 0xff)
			break;
	if (i == max_ids) {
		kfree(mc);
		return NULL;
	}
	/* find the bit in the 128 possible channel opportunities */
	mask = 0x80;
	for (j = 0; j < 8; j++) {
		if ((id_array[i] & mask) == 0)
			break;
		mask >>= 1;
	}

	/* grab it */
	id_array[i] |= mask;
	mc->master  = base_id;
	mc->channel = ((i & 0xf)<<3) + j;
	/* write new master Id / channel Id allocation to channel control */
	pti_control_frame_built_and_sent(mc, thread_name);
	return mc;
}

/*
 * The following three functions:
 * pti_request_mastercahannel(), mipi_release_masterchannel()
 * and pti_writedata() are an API for other kernel drivers to
 * access PTI.
 */

/**
 * pti_request_masterchannel()- Kernel API function used to allocate
 *				a master, channel ID address
 *				to write to PTI HW.
 *
 * @type:        0- request Application  master, channel aperture ID
 *                  write address.
 *               1- request OS master, channel aperture ID write
 *                  address.
 *               2- request Modem master, channel aperture ID
 *                  write address.
 *               Other values, error.
 * @thread_name: The thread name associated with the master / channel or
 *               'NULL' if using the 'current' global variable.
 *
 * Returns:
 *	pti_masterchannel struct
 *	0 for error
 */
struct pti_masterchannel *pti_request_masterchannel(u8 type,
						    const char *thread_name)
{
	struct pti_masterchannel *mc;

	mutex_lock(&alloclock);

	switch (type) {

	case 0:
		mc = get_id(drv_data->ia_app, MAX_APP_IDS,
			    APP_BASE_ID, thread_name);
		break;

	case 1:
		mc = get_id(drv_data->ia_os, MAX_OS_IDS,
			    OS_BASE_ID, thread_name);
		break;

	case 2:
		mc = get_id(drv_data->ia_modem, MAX_MODEM_IDS,
			    MODEM_BASE_ID, thread_name);
		break;
	default:
		mc = NULL;
	}

	mutex_unlock(&alloclock);
	return mc;
}
EXPORT_SYMBOL_GPL(pti_request_masterchannel);

/**
 * pti_release_masterchannel()- Kernel API function used to release
 *				a master, channel ID address
 *				used to write to PTI HW.
 *
 * @mc: master, channel apeture ID address to be released.  This
 *      will de-allocate the structure via kfree().
 */
void pti_release_masterchannel(struct pti_masterchannel *mc)
{
	u8 master, channel, i;

	mutex_lock(&alloclock);

	if (mc) {
		master = mc->master;
		channel = mc->channel;

		if (master == APP_BASE_ID) {
			i = channel >> 3;
			drv_data->ia_app[i] &=  ~(0x80>>(channel & 0x7));
		} else if (master == OS_BASE_ID) {
			i = channel >> 3;
			drv_data->ia_os[i] &= ~(0x80>>(channel & 0x7));
		} else {
			i = channel >> 3;
			drv_data->ia_modem[i] &= ~(0x80>>(channel & 0x7));
		}

		kfree(mc);
	}

	mutex_unlock(&alloclock);
}
EXPORT_SYMBOL_GPL(pti_release_masterchannel);

/**
 * pti_writedata()- Kernel API function used to write trace
 *                  debugging data to PTI HW.
 *
 * @mc:    Master, channel aperture ID address to write to.
 *         Null value will return with no write occurring.
 * @buf:   Trace debuging data to write to the PTI HW.
 *         Null value will return with no write occurring.
 * @count: Size of buf. Value of 0 or a negative number will
 *         return with no write occuring.
 */
void pti_writedata(struct pti_masterchannel *mc, u8 *buf, int count)
{
	/*
	 * since this function is exported, this is treated like an
	 * API function, thus, all parameters should
	 * be checked for validity.
	 */
	if ((mc != NULL) && (buf != NULL) && (count > 0))
		pti_write_to_aperture(mc, buf, count);
	return;
}
EXPORT_SYMBOL_GPL(pti_writedata);

/**
 * pti_pci_remove()- Driver exit method to remove PTI from
 *		   PCI bus.
 * @pdev: variable containing pci info of PTI.
 */
static void __devexit pti_pci_remove(struct pci_dev *pdev)
{
	struct pti_dev *drv_data;

	drv_data = pci_get_drvdata(pdev);
	if (drv_data != NULL) {
		pci_iounmap(pdev, drv_data->pti_ioaddr);
		pci_set_drvdata(pdev, NULL);
		kfree(drv_data);
		pci_release_region(pdev, 1);
		pci_disable_device(pdev);
	}
}

/*
 * for the tty_driver_*() basic function descriptions, see tty_driver.h.
 * Specific header comments made for PTI-related specifics.
 */

/**
 * pti_tty_driver_open()- Open an Application master, channel aperture
 * ID to the PTI device via tty device.
 *
 * @tty: tty interface.
 * @filp: filp interface pased to tty_port_open() call.
 *
 * Returns:
 *	int, 0 for success
 *	otherwise, fail value
 *
 * The main purpose of using the tty device interface is for
 * each tty port to have a unique PTI write aperture.  In an
 * example use case, ttyPTI0 gets syslogd and an APP aperture
 * ID and ttyPTI1 is where the n_tracesink ldisc hooks to route
 * modem messages into PTI.  Modem trace data does not have to
 * go to ttyPTI1, but ttyPTI0 and ttyPTI1 do need to be distinct
 * master IDs.  These messages go through the PTI HW and out of
 * the handheld platform and to the Fido/Lauterbach device.
 */
static int pti_tty_driver_open(struct tty_struct *tty, struct file *filp)
{
	/*
	 * we actually want to allocate a new channel per open, per
	 * system arch.  HW gives more than plenty channels for a single
	 * system task to have its own channel to write trace data. This
	 * also removes a locking requirement for the actual write
	 * procedure.
	 */
	return tty_port_open(&drv_data->port, tty, filp);
}

/**
 * pti_tty_driver_close()- close tty device and release Application
 * master, channel aperture ID to the PTI device via tty device.
 *
 * @tty: tty interface.
 * @filp: filp interface pased to tty_port_close() call.
 *
 * The main purpose of using the tty device interface is to route
 * syslog daemon messages to the PTI HW and out of the handheld platform
 * and to the Fido/Lauterbach device.
 */
static void pti_tty_driver_close(struct tty_struct *tty, struct file *filp)
{
	tty_port_close(&drv_data->port, tty, filp);
}

/**
 * pti_tty_install()- Used to set up specific master-channels
 *		      to tty ports for organizational purposes when
 *		      tracing viewed from debuging tools.
 *
 * @driver: tty driver information.
 * @tty: tty struct containing pti information.
 *
 * Returns:
 *	0 for success
 *	otherwise, error
 */
static int pti_tty_install(struct tty_driver *driver, struct tty_struct *tty)
{
	int idx = tty->index;
	struct pti_tty *pti_tty_data;
	int ret = tty_init_termios(tty);

	if (ret == 0) {
		tty_driver_kref_get(driver);
		tty->count++;
		driver->ttys[idx] = tty;

		pti_tty_data = kmalloc(sizeof(struct pti_tty), GFP_KERNEL);
		if (pti_tty_data == NULL)
			return -ENOMEM;

		if (idx == PTITTY_MINOR_START)
			pti_tty_data->mc = pti_request_masterchannel(0, NULL);
		else
			pti_tty_data->mc = pti_request_masterchannel(2, NULL);

		if (pti_tty_data->mc == NULL) {
			kfree(pti_tty_data);
			return -ENXIO;
		}
		tty->driver_data = pti_tty_data;
	}

	return ret;
}

/**
 * pti_tty_cleanup()- Used to de-allocate master-channel resources
 *		      tied to tty's of this driver.
 *
 * @tty: tty struct containing pti information.
 */
static void pti_tty_cleanup(struct tty_struct *tty)
{
	struct pti_tty *pti_tty_data = tty->driver_data;
	if (pti_tty_data == NULL)
		return;
	pti_release_masterchannel(pti_tty_data->mc);
	kfree(pti_tty_data);
	tty->driver_data = NULL;
}

/**
 * pti_tty_driver_write()-  Write trace debugging data through the char
 * interface to the PTI HW.  Part of the misc device implementation.
 *
 * @filp: Contains private data which is used to obtain
 *        master, channel write ID.
 * @data: trace data to be written.
 * @len:  # of byte to write.
 *
 * Returns:
 *	int, # of bytes written
 *	otherwise, error
 */
static int pti_tty_driver_write(struct tty_struct *tty,
	const unsigned char *buf, int len)
{
	struct pti_tty *pti_tty_data = tty->driver_data;
	if ((pti_tty_data != NULL) && (pti_tty_data->mc != NULL)) {
		pti_write_to_aperture(pti_tty_data->mc, (u8 *)buf, len);
		return len;
	}
	/*
	 * we can't write to the pti hardware if the private driver_data
	 * and the mc address is not there.
	 */
	else
		return -EFAULT;
}

/**
 * pti_tty_write_room()- Always returns 2048.
 *
 * @tty: contains tty info of the pti driver.
 */
static int pti_tty_write_room(struct tty_struct *tty)
{
	return 2048;
}

/**
 * pti_char_open()- Open an Application master, channel aperture
 * ID to the PTI device. Part of the misc device implementation.
 *
 * @inode: not used.
 * @filp:  Output- will have a masterchannel struct set containing
 *                 the allocated application PTI aperture write address.
 *
 * Returns:
 *	int, 0 for success
 *	otherwise, a fail value
 */
static int pti_char_open(struct inode *inode, struct file *filp)
{
	struct pti_masterchannel *mc;

	/*
	 * We really do want to fail immediately if
	 * pti_request_masterchannel() fails,
	 * before assigning the value to filp->private_data.
	 * Slightly easier to debug if this driver needs debugging.
	 */
	mc = pti_request_masterchannel(0, NULL);
	if (mc == NULL)
		return -ENOMEM;
	filp->private_data = mc;
	return 0;
}

/**
 * pti_char_release()-  Close a char channel to the PTI device. Part
 * of the misc device implementation.
 *
 * @inode: Not used in this implementaiton.
 * @filp:  Contains private_data that contains the master, channel
 *         ID to be released by the PTI device.
 *
 * Returns:
 *	always 0
 */
static int pti_char_release(struct inode *inode, struct file *filp)
{
	pti_release_masterchannel(filp->private_data);
	filp->private_data = NULL;
	return 0;
}

/**
 * pti_char_write()-  Write trace debugging data through the char
 * interface to the PTI HW.  Part of the misc device implementation.
 *
 * @filp:  Contains private data which is used to obtain
 *         master, channel write ID.
 * @data:  trace data to be written.
 * @len:   # of byte to write.
 * @ppose: Not used in this function implementation.
 *
 * Returns:
 *	int, # of bytes written
 *	otherwise, error value
 *
 * Notes: From side discussions with Alan Cox and experimenting
 * with PTI debug HW like Nokia's Fido box and Lauterbach
 * devices, 8192 byte write buffer used by USER_COPY_SIZE was
 * deemed an appropriate size for this type of usage with
 * debugging HW.
 */
static ssize_t pti_char_write(struct file *filp, const char __user *data,
			      size_t len, loff_t *ppose)
{
	struct pti_masterchannel *mc;
	void *kbuf;
	const char __user *tmp;
	size_t size = USER_COPY_SIZE;
	size_t n = 0;

	tmp = data;
	mc = filp->private_data;

	kbuf = kmalloc(size, GFP_KERNEL);
	if (kbuf == NULL)  {
		pr_err("%s(%d): buf allocation failed\n",
			__func__, __LINE__);
		return -ENOMEM;
	}

	do {
		if (len - n > USER_COPY_SIZE)
			size = USER_COPY_SIZE;
		else
			size = len - n;

		if (copy_from_user(kbuf, tmp, size)) {
			kfree(kbuf);
			return n ? n : -EFAULT;
		}

		pti_write_to_aperture(mc, kbuf, size);
		n  += size;
		tmp += size;

	} while (len > n);

	kfree(kbuf);
	return len;
}

static const struct tty_operations pti_tty_driver_ops = {
	.open		= pti_tty_driver_open,
	.close		= pti_tty_driver_close,
	.write		= pti_tty_driver_write,
	.write_room	= pti_tty_write_room,
	.install	= pti_tty_install,
	.cleanup	= pti_tty_cleanup
};

static const struct file_operations pti_char_driver_ops = {
	.owner		= THIS_MODULE,
	.write		= pti_char_write,
	.open		= pti_char_open,
	.release	= pti_char_release,
};

static struct miscdevice pti_char_driver = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= CHARNAME,
	.fops		= &pti_char_driver_ops
};

/**
 * pti_console_write()-  Write to the console that has been acquired.
 *
 * @c:   Not used in this implementaiton.
 * @buf: Data to be written.
 * @len: Length of buf.
 */
static void pti_console_write(struct console *c, const char *buf, unsigned len)
{
	static struct pti_masterchannel mc = {.master  = CONSOLE_ID,
					      .channel = 0};

	mc.channel = pti_console_channel;
	pti_console_channel = (pti_console_channel + 1) & 0x7f;

	pti_write_full_frame_to_aperture(&mc, buf, len);
}

/**
 * pti_console_device()-  Return the driver tty structure and set the
 *			  associated index implementation.
 *
 * @c:     Console device of the driver.
 * @index: index associated with c.
 *
 * Returns:
 *	always value of pti_tty_driver structure when this function
 *	is called.
 */
static struct tty_driver *pti_console_device(struct console *c, int *index)
{
	*index = c->index;
	return pti_tty_driver;
}

/**
 * pti_console_setup()-  Initialize console variables used by the driver.
 *
 * @c:     Not used.
 * @opts:  Not used.
 *
 * Returns:
 *	always 0.
 */
static int pti_console_setup(struct console *c, char *opts)
{
	pti_console_channel = 0;
	pti_control_channel = 0;
	return 0;
}

/*
 * pti_console struct, used to capture OS printk()'s and shift
 * out to the PTI device for debugging.  This cannot be
 * enabled upon boot because of the possibility of eating
 * any serial console printk's (race condition discovered).
 * The console should be enabled upon when the tty port is
 * used for the first time.  Since the primary purpose for
 * the tty port is to hook up syslog to it, the tty port
 * will be open for a really long time.
 */
static struct console pti_console = {
	.name		= TTYNAME,
	.write		= pti_console_write,
	.device		= pti_console_device,
	.setup		= pti_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= 0,
};

/**
 * pti_port_activate()- Used to start/initialize any items upon
 * first opening of tty_port().
 *
 * @port- The tty port number of the PTI device.
 * @tty-  The tty struct associated with this device.
 *
 * Returns:
 *	always returns 0
 *
 * Notes: The primary purpose of the PTI tty port 0 is to hook
 * the syslog daemon to it; thus this port will be open for a
 * very long time.
 */
static int pti_port_activate(struct tty_port *port, struct tty_struct *tty)
{
	if (port->tty->index == PTITTY_MINOR_START)
		console_start(&pti_console);
	return 0;
}

/**
 * pti_port_shutdown()- Used to stop/shutdown any items upon the
 * last tty port close.
 *
 * @port- The tty port number of the PTI device.
 *
 * Notes: The primary purpose of the PTI tty port 0 is to hook
 * the syslog daemon to it; thus this port will be open for a
 * very long time.
 */
static void pti_port_shutdown(struct tty_port *port)
{
	if (port->tty->index == PTITTY_MINOR_START)
		console_stop(&pti_console);
}

static const struct tty_port_operations tty_port_ops = {
	.activate = pti_port_activate,
	.shutdown = pti_port_shutdown,
};

/*
 * Note the _probe() call sets everything up and ties the char and tty
 * to successfully detecting the PTI device on the pci bus.
 */

/**
 * pti_pci_probe()- Used to detect pti on the pci bus and set
 *		    things up in the driver.
 *
 * @pdev- pci_dev struct values for pti.
 * @ent-  pci_device_id struct for pti driver.
 *
 * Returns:
 *	0 for success
 *	otherwise, error
 */
static int __devinit pti_pci_probe(struct pci_dev *pdev,
		const struct pci_device_id *ent)
{
	int retval = -EINVAL;
	int pci_bar = 1;

	dev_dbg(&pdev->dev, "%s %s(%d): PTI PCI ID %04x:%04x\n", __FILE__,
			__func__, __LINE__, pdev->vendor, pdev->device);

	retval = misc_register(&pti_char_driver);
	if (retval) {
		pr_err("%s(%d): CHAR registration failed of pti driver\n",
			__func__, __LINE__);
		pr_err("%s(%d): Error value returned: %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	retval = pci_enable_device(pdev);
	if (retval != 0) {
		dev_err(&pdev->dev,
			"%s: pci_enable_device() returned error %d\n",
			__func__, retval);
		return retval;
	}

	drv_data = kzalloc(sizeof(*drv_data), GFP_KERNEL);

	if (drv_data == NULL) {
		retval = -ENOMEM;
		dev_err(&pdev->dev,
			"%s(%d): kmalloc() returned NULL memory.\n",
			__func__, __LINE__);
		return retval;
	}
	drv_data->pti_addr = pci_resource_start(pdev, pci_bar);

	retval = pci_request_region(pdev, pci_bar, dev_name(&pdev->dev));
	if (retval != 0) {
		dev_err(&pdev->dev,
			"%s(%d): pci_request_region() returned error %d\n",
			__func__, __LINE__, retval);
		kfree(drv_data);
		return retval;
	}
	drv_data->aperture_base = drv_data->pti_addr+APERTURE_14;
	drv_data->pti_ioaddr =
		ioremap_nocache((u32)drv_data->aperture_base,
		APERTURE_LEN);
	if (!drv_data->pti_ioaddr) {
		pci_release_region(pdev, pci_bar);
		retval = -ENOMEM;
		kfree(drv_data);
		return retval;
	}

	pci_set_drvdata(pdev, drv_data);

	tty_port_init(&drv_data->port);
	drv_data->port.ops = &tty_port_ops;

	tty_register_device(pti_tty_driver, 0, &pdev->dev);
	tty_register_device(pti_tty_driver, 1, &pdev->dev);

	register_console(&pti_console);

	return retval;
}

static struct pci_driver pti_pci_driver = {
	.name		= PCINAME,
	.id_table	= pci_ids,
	.probe		= pti_pci_probe,
	.remove		= pti_pci_remove,
};

/**
 *
 * pti_init()- Overall entry/init call to the pti driver.
 *             It starts the registration process with the kernel.
 *
 * Returns:
 *	int __init, 0 for success
 *	otherwise value is an error
 *
 */
static int __init pti_init(void)
{
	int retval = -EINVAL;

	/* First register module as tty device */

	pti_tty_driver = alloc_tty_driver(1);
	if (pti_tty_driver == NULL) {
		pr_err("%s(%d): Memory allocation failed for ptiTTY driver\n",
			__func__, __LINE__);
		return -ENOMEM;
	}

	pti_tty_driver->owner			= THIS_MODULE;
	pti_tty_driver->magic			= TTY_DRIVER_MAGIC;
	pti_tty_driver->driver_name		= DRIVERNAME;
	pti_tty_driver->name			= TTYNAME;
	pti_tty_driver->major			= 0;
	pti_tty_driver->minor_start		= PTITTY_MINOR_START;
	pti_tty_driver->minor_num		= PTITTY_MINOR_NUM;
	pti_tty_driver->num			= PTITTY_MINOR_NUM;
	pti_tty_driver->type			= TTY_DRIVER_TYPE_SYSTEM;
	pti_tty_driver->subtype			= SYSTEM_TYPE_SYSCONS;
	pti_tty_driver->flags			= TTY_DRIVER_REAL_RAW |
						  TTY_DRIVER_DYNAMIC_DEV;
	pti_tty_driver->init_termios		= tty_std_termios;

	tty_set_operations(pti_tty_driver, &pti_tty_driver_ops);

	retval = tty_register_driver(pti_tty_driver);
	if (retval) {
		pr_err("%s(%d): TTY registration failed of pti driver\n",
			__func__, __LINE__);
		pr_err("%s(%d): Error value returned: %d\n",
			__func__, __LINE__, retval);

		pti_tty_driver = NULL;
		return retval;
	}

	retval = pci_register_driver(&pti_pci_driver);

	if (retval) {
		pr_err("%s(%d): PCI registration failed of pti driver\n",
			__func__, __LINE__);
		pr_err("%s(%d): Error value returned: %d\n",
			__func__, __LINE__, retval);

		tty_unregister_driver(pti_tty_driver);
		pr_err("%s(%d): Unregistering TTY part of pti driver\n",
			__func__, __LINE__);
		pti_tty_driver = NULL;
		return retval;
	}

	return retval;
}

/**
 * pti_exit()- Unregisters this module as a tty and pci driver.
 */
static void __exit pti_exit(void)
{
	int retval;

	tty_unregister_device(pti_tty_driver, 0);
	tty_unregister_device(pti_tty_driver, 1);

	retval = tty_unregister_driver(pti_tty_driver);
	if (retval) {
		pr_err("%s(%d): TTY unregistration failed of pti driver\n",
			__func__, __LINE__);
		pr_err("%s(%d): Error value returned: %d\n",
			__func__, __LINE__, retval);
	}

	pci_unregister_driver(&pti_pci_driver);

	retval = misc_deregister(&pti_char_driver);
	if (retval) {
		pr_err("%s(%d): CHAR unregistration failed of pti driver\n",
			__func__, __LINE__);
		pr_err("%s(%d): Error value returned: %d\n",
			__func__, __LINE__, retval);
	}

	unregister_console(&pti_console);
	return;
}

module_init(pti_init);
module_exit(pti_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ken Mills, Jay Freyensee");
MODULE_DESCRIPTION("PTI Driver");

