/*
 *  linux/drivers/s390/crypto/z90main.c
 *
 *  z90crypt 1.3.2
 *
 *  Copyright (C)  2001, 2004 IBM Corporation
 *  Author(s): Robert Burroughs (burrough@us.ibm.com)
 *             Eric Rossman (edrossma@us.ibm.com)
 *
 *  Hotplug & misc device support: Jochen Roehrig (roehrig@de.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <asm/uaccess.h>       // copy_(from|to)_user
#include <linux/compat.h>
#include <linux/compiler.h>
#include <linux/delay.h>       // mdelay
#include <linux/init.h>
#include <linux/interrupt.h>   // for tasklets
#include <linux/ioctl32.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kobject_uevent.h>
#include <linux/proc_fs.h>
#include <linux/syscalls.h>
#include <linux/version.h>
#include "z90crypt.h"
#include "z90common.h"

#define VERSION_Z90MAIN_C "$Revision: 1.62 $"

static char z90main_version[] __initdata =
	"z90main.o (" VERSION_Z90MAIN_C "/"
                      VERSION_Z90COMMON_H "/" VERSION_Z90CRYPT_H ")";

extern char z90hardware_version[];

/**
 * Defaults that may be modified.
 */

/**
 * You can specify a different minor at compile time.
 */
#ifndef Z90CRYPT_MINOR
#define Z90CRYPT_MINOR	MISC_DYNAMIC_MINOR
#endif

/**
 * You can specify a different domain at compile time or on the insmod
 * command line.
 */
#ifndef DOMAIN_INDEX
#define DOMAIN_INDEX	-1
#endif

/**
 * This is the name under which the device is registered in /proc/modules.
 */
#define REG_NAME	"z90crypt"

/**
 * Cleanup should run every CLEANUPTIME seconds and should clean up requests
 * older than CLEANUPTIME seconds in the past.
 */
#ifndef CLEANUPTIME
#define CLEANUPTIME 15
#endif

/**
 * Config should run every CONFIGTIME seconds
 */
#ifndef CONFIGTIME
#define CONFIGTIME 30
#endif

/**
 * The first execution of the config task should take place
 * immediately after initialization
 */
#ifndef INITIAL_CONFIGTIME
#define INITIAL_CONFIGTIME 1
#endif

/**
 * Reader should run every READERTIME milliseconds
 * With the 100Hz patch for s390, z90crypt can lock the system solid while
 * under heavy load. We'll try to avoid that.
 */
#ifndef READERTIME
#if HZ > 1000
#define READERTIME 2
#else
#define READERTIME 10
#endif
#endif

/**
 * turn long device array index into device pointer
 */
#define LONG2DEVPTR(ndx) (z90crypt.device_p[(ndx)])

/**
 * turn short device array index into long device array index
 */
#define SHRT2LONG(ndx) (z90crypt.overall_device_x.device_index[(ndx)])

/**
 * turn short device array index into device pointer
 */
#define SHRT2DEVPTR(ndx) LONG2DEVPTR(SHRT2LONG(ndx))

/**
 * Status for a work-element
 */
#define STAT_DEFAULT	0x00 // request has not been processed

#define STAT_ROUTED	0x80 // bit 7: requests get routed to specific device
			     //	       else, device is determined each write
#define STAT_FAILED	0x40 // bit 6: this bit is set if the request failed
			     //	       before being sent to the hardware.
#define STAT_WRITTEN	0x30 // bits 5-4: work to be done, not sent to device
//			0x20 // UNUSED state
#define STAT_READPEND	0x10 // bits 5-4: work done, we're returning data now
#define STAT_NOWORK	0x00 // bits off: no work on any queue
#define STAT_RDWRMASK	0x30 // mask for bits 5-4

/**
 * Macros to check the status RDWRMASK
 */
#define CHK_RDWRMASK(statbyte) ((statbyte) & STAT_RDWRMASK)
#define SET_RDWRMASK(statbyte, newval) \
	{(statbyte) &= ~STAT_RDWRMASK; (statbyte) |= newval;}

/**
 * Audit Trail.	 Progress of a Work element
 * audit[0]: Unless noted otherwise, these bits are all set by the process
 */
#define FP_COPYFROM	0x80 // Caller's buffer has been copied to work element
#define FP_BUFFREQ	0x40 // Low Level buffer requested
#define FP_BUFFGOT	0x20 // Low Level buffer obtained
#define FP_SENT		0x10 // Work element sent to a crypto device
			     // (may be set by process or by reader task)
#define FP_PENDING	0x08 // Work element placed on pending queue
			     // (may be set by process or by reader task)
#define FP_REQUEST	0x04 // Work element placed on request queue
#define FP_ASLEEP	0x02 // Work element about to sleep
#define FP_AWAKE	0x01 // Work element has been awakened

/**
 * audit[1]: These bits are set by the reader task and/or the cleanup task
 */
#define FP_NOTPENDING	  0x80 // Work element removed from pending queue
#define FP_AWAKENING	  0x40 // Caller about to be awakened
#define FP_TIMEDOUT	  0x20 // Caller timed out
#define FP_RESPSIZESET	  0x10 // Response size copied to work element
#define FP_RESPADDRCOPIED 0x08 // Response address copied to work element
#define FP_RESPBUFFCOPIED 0x04 // Response buffer copied to work element
#define FP_REMREQUEST	  0x02 // Work element removed from request queue
#define FP_SIGNALED	  0x01 // Work element was awakened by a signal

/**
 * audit[2]: unused
 */

/**
 * state of the file handle in private_data.status
 */
#define STAT_OPEN 0
#define STAT_CLOSED 1

/**
 * PID() expands to the process ID of the current process
 */
#define PID() (current->pid)

/**
 * Selected Constants.	The number of APs and the number of devices
 */
#ifndef Z90CRYPT_NUM_APS
#define Z90CRYPT_NUM_APS 64
#endif
#ifndef Z90CRYPT_NUM_DEVS
#define Z90CRYPT_NUM_DEVS Z90CRYPT_NUM_APS
#endif

/**
 * Buffer size for receiving responses. The maximum Response Size
 * is actually the maximum request size, since in an error condition
 * the request itself may be returned unchanged.
 */
#define MAX_RESPONSE_SIZE 0x0000077C

/**
 * A count and status-byte mask
 */
struct status {
	int	      st_count;		    // # of enabled devices
	int	      disabled_count;	    // # of disabled devices
	int	      user_disabled_count;  // # of devices disabled via proc fs
	unsigned char st_mask[Z90CRYPT_NUM_APS]; // current status mask
};

/**
 * The array of device indexes is a mechanism for fast indexing into
 * a long (and sparse) array.  For instance, if APs 3, 9 and 47 are
 * installed, z90CDeviceIndex[0] is 3, z90CDeviceIndex[1] is 9, and
 * z90CDeviceIndex[2] is 47.
 */
struct device_x {
	int device_index[Z90CRYPT_NUM_DEVS];
};

/**
 * All devices are arranged in a single array: 64 APs
 */
struct device {
	int		 dev_type;	    // PCICA, PCICC, PCIXCC_MCL2,
					    // PCIXCC_MCL3, CEX2C
	enum devstat	 dev_stat;	    // current device status
	int		 dev_self_x;	    // Index in array
	int		 disabled;	    // Set when device is in error
	int		 user_disabled;	    // Set when device is disabled by user
	int		 dev_q_depth;	    // q depth
	unsigned char *	 dev_resp_p;	    // Response buffer address
	int		 dev_resp_l;	    // Response Buffer length
	int		 dev_caller_count;  // Number of callers
	int		 dev_total_req_cnt; // # requests for device since load
	struct list_head dev_caller_list;   // List of callers
};

/**
 * There's a struct status and a struct device_x for each device type.
 */
struct hdware_block {
	struct status	hdware_mask;
	struct status	type_mask[Z90CRYPT_NUM_TYPES];
	struct device_x type_x_addr[Z90CRYPT_NUM_TYPES];
	unsigned char	device_type_array[Z90CRYPT_NUM_APS];
};

/**
 * z90crypt is the topmost data structure in the hierarchy.
 */
struct z90crypt {
	int		     max_count;		// Nr of possible crypto devices
	struct status	     mask;
	int		     q_depth_array[Z90CRYPT_NUM_DEVS];
	int		     dev_type_array[Z90CRYPT_NUM_DEVS];
	struct device_x	     overall_device_x;	// array device indexes
	struct device *	     device_p[Z90CRYPT_NUM_DEVS];
	int		     terminating;
	int		     domain_established;// TRUE:  domain has been found
	int		     cdx;		// Crypto Domain Index
	int		     len;		// Length of this data structure
	struct hdware_block *hdware_info;
};

/**
 * An array of these structures is pointed to from dev_caller
 * The length of the array depends on the device type. For APs,
 * there are 8.
 *
 * The caller buffer is allocated to the user at OPEN. At WRITE,
 * it contains the request; at READ, the response. The function
 * send_to_crypto_device converts the request to device-dependent
 * form and use the caller's OPEN-allocated buffer for the response.
 *
 * For the contents of caller_dev_dep_req and caller_dev_dep_req_p
 * because that points to it, see the discussion in z90hardware.c.
 * Search for "extended request message block".
 */
struct caller {
	int		 caller_buf_l;		 // length of original request
	unsigned char *	 caller_buf_p;		 // Original request on WRITE
	int		 caller_dev_dep_req_l;	 // len device dependent request
	unsigned char *	 caller_dev_dep_req_p;	 // Device dependent form
	unsigned char	 caller_id[8];		 // caller-supplied message id
	struct list_head caller_liste;
	unsigned char	 caller_dev_dep_req[MAX_RESPONSE_SIZE];
};

/**
 * Function prototypes from z90hardware.c
 */
enum hdstat query_online(int, int, int, int *, int *);
enum devstat reset_device(int, int, int);
enum devstat send_to_AP(int, int, int, unsigned char *);
enum devstat receive_from_AP(int, int, int, unsigned char *, unsigned char *);
int convert_request(unsigned char *, int, short, int, int, int *,
		    unsigned char *);
int convert_response(unsigned char *, unsigned char *, int *, unsigned char *);

/**
 * Low level function prototypes
 */
static int create_z90crypt(int *);
static int refresh_z90crypt(int *);
static int find_crypto_devices(struct status *);
static int create_crypto_device(int);
static int destroy_crypto_device(int);
static void destroy_z90crypt(void);
static int refresh_index_array(struct status *, struct device_x *);
static int probe_device_type(struct device *);
static int probe_PCIXCC_type(struct device *);

/**
 * proc fs definitions
 */
static struct proc_dir_entry *z90crypt_entry;

/**
 * data structures
 */

/**
 * work_element.opener points back to this structure
 */
struct priv_data {
	pid_t	opener_pid;
	unsigned char	status;		// 0: open  1: closed
};

/**
 * A work element is allocated for each request
 */
struct work_element {
	struct priv_data *priv_data;
	pid_t		  pid;
	int		  devindex;	  // index of device processing this w_e
					  // (If request did not specify device,
					  // -1 until placed onto a queue)
	int		  devtype;
	struct list_head  liste;	  // used for requestq and pendingq
	char		  buffer[128];	  // local copy of user request
	int		  buff_size;	  // size of the buffer for the request
	char		  resp_buff[RESPBUFFSIZE];
	int		  resp_buff_size;
	char __user *	  resp_addr;	  // address of response in user space
	unsigned int	  funccode;	  // function code of request
	wait_queue_head_t waitq;
	unsigned long	  requestsent;	  // time at which the request was sent
	atomic_t	  alarmrung;	  // wake-up signal
	unsigned char	  caller_id[8];	  // pid + counter, for this w_e
	unsigned char	  status[1];	  // bits to mark status of the request
	unsigned char	  audit[3];	  // record of work element's progress
	unsigned char *	  requestptr;	  // address of request buffer
	int		  retcode;	  // return code of request
};

/**
 * High level function prototypes
 */
static int z90crypt_open(struct inode *, struct file *);
static int z90crypt_release(struct inode *, struct file *);
static ssize_t z90crypt_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t z90crypt_write(struct file *, const char __user *,
							size_t, loff_t *);
static long z90crypt_unlocked_ioctl(struct file *, unsigned int, unsigned long);
static long z90crypt_compat_ioctl(struct file *, unsigned int, unsigned long);

static void z90crypt_reader_task(unsigned long);
static void z90crypt_schedule_reader_task(unsigned long);
static void z90crypt_config_task(unsigned long);
static void z90crypt_cleanup_task(unsigned long);

static int z90crypt_status(char *, char **, off_t, int, int *, void *);
static int z90crypt_status_write(struct file *, const char __user *,
				 unsigned long, void *);

/**
 * Storage allocated at initialization and used throughout the life of
 * this insmod
 */
static int domain = DOMAIN_INDEX;
static struct z90crypt z90crypt;
static int quiesce_z90crypt;
static spinlock_t queuespinlock;
static struct list_head request_list;
static int requestq_count;
static struct list_head pending_list;
static int pendingq_count;

static struct tasklet_struct reader_tasklet;
static struct timer_list reader_timer;
static struct timer_list config_timer;
static struct timer_list cleanup_timer;
static atomic_t total_open;
static atomic_t z90crypt_step;

static struct file_operations z90crypt_fops = {
	.owner		= THIS_MODULE,
	.read		= z90crypt_read,
	.write		= z90crypt_write,
	.unlocked_ioctl	= z90crypt_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= z90crypt_compat_ioctl,
#endif
	.open		= z90crypt_open,
	.release	= z90crypt_release
};

static struct miscdevice z90crypt_misc_device = {
	.minor	    = Z90CRYPT_MINOR,
	.name	    = DEV_NAME,
	.fops	    = &z90crypt_fops,
	.devfs_name = DEV_NAME
};

/**
 * Documentation values.
 */
MODULE_AUTHOR("zSeries Linux Crypto Team: Robert H. Burroughs, Eric D. Rossman"
	      "and Jochen Roehrig");
MODULE_DESCRIPTION("zSeries Linux Cryptographic Coprocessor device driver, "
		   "Copyright 2001, 2004 IBM Corporation");
MODULE_LICENSE("GPL");
module_param(domain, int, 0);
MODULE_PARM_DESC(domain, "domain index for device");

#ifdef CONFIG_COMPAT
/**
 * ioctl32 conversion routines
 */
struct ica_rsa_modexpo_32 { // For 32-bit callers
	compat_uptr_t	inputdata;
	unsigned int	inputdatalength;
	compat_uptr_t	outputdata;
	unsigned int	outputdatalength;
	compat_uptr_t	b_key;
	compat_uptr_t	n_modulus;
};

static long
trans_modexpo32(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct ica_rsa_modexpo_32 __user *mex32u = compat_ptr(arg);
	struct ica_rsa_modexpo_32  mex32k;
	struct ica_rsa_modexpo __user *mex64;
	long ret = 0;
	unsigned int i;

	if (!access_ok(VERIFY_WRITE, mex32u, sizeof(struct ica_rsa_modexpo_32)))
		return -EFAULT;
	mex64 = compat_alloc_user_space(sizeof(struct ica_rsa_modexpo));
	if (!access_ok(VERIFY_WRITE, mex64, sizeof(struct ica_rsa_modexpo)))
		return -EFAULT;
	if (copy_from_user(&mex32k, mex32u, sizeof(struct ica_rsa_modexpo_32)))
		return -EFAULT;
	if (__put_user(compat_ptr(mex32k.inputdata), &mex64->inputdata)   ||
	    __put_user(mex32k.inputdatalength, &mex64->inputdatalength)   ||
	    __put_user(compat_ptr(mex32k.outputdata), &mex64->outputdata) ||
	    __put_user(mex32k.outputdatalength, &mex64->outputdatalength) ||
	    __put_user(compat_ptr(mex32k.b_key), &mex64->b_key)           ||
	    __put_user(compat_ptr(mex32k.n_modulus), &mex64->n_modulus))
		return -EFAULT;
	ret = z90crypt_unlocked_ioctl(filp, cmd, (unsigned long)mex64);
	if (!ret)
		if (__get_user(i, &mex64->outputdatalength) ||
		    __put_user(i, &mex32u->outputdatalength))
			ret = -EFAULT;
	return ret;
}

struct ica_rsa_modexpo_crt_32 { // For 32-bit callers
	compat_uptr_t	inputdata;
	unsigned int	inputdatalength;
	compat_uptr_t	outputdata;
	unsigned int	outputdatalength;
	compat_uptr_t	bp_key;
	compat_uptr_t	bq_key;
	compat_uptr_t	np_prime;
	compat_uptr_t	nq_prime;
	compat_uptr_t	u_mult_inv;
};

static long
trans_modexpo_crt32(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct ica_rsa_modexpo_crt_32 __user *crt32u = compat_ptr(arg);
	struct ica_rsa_modexpo_crt_32  crt32k;
	struct ica_rsa_modexpo_crt __user *crt64;
	long ret = 0;
	unsigned int i;

	if (!access_ok(VERIFY_WRITE, crt32u,
		       sizeof(struct ica_rsa_modexpo_crt_32)))
		return -EFAULT;
	crt64 = compat_alloc_user_space(sizeof(struct ica_rsa_modexpo_crt));
	if (!access_ok(VERIFY_WRITE, crt64, sizeof(struct ica_rsa_modexpo_crt)))
		return -EFAULT;
	if (copy_from_user(&crt32k, crt32u,
			   sizeof(struct ica_rsa_modexpo_crt_32)))
		return -EFAULT;
	if (__put_user(compat_ptr(crt32k.inputdata), &crt64->inputdata)   ||
	    __put_user(crt32k.inputdatalength, &crt64->inputdatalength)   ||
	    __put_user(compat_ptr(crt32k.outputdata), &crt64->outputdata) ||
	    __put_user(crt32k.outputdatalength, &crt64->outputdatalength) ||
	    __put_user(compat_ptr(crt32k.bp_key), &crt64->bp_key)         ||
	    __put_user(compat_ptr(crt32k.bq_key), &crt64->bq_key)         ||
	    __put_user(compat_ptr(crt32k.np_prime), &crt64->np_prime)     ||
	    __put_user(compat_ptr(crt32k.nq_prime), &crt64->nq_prime)     ||
	    __put_user(compat_ptr(crt32k.u_mult_inv), &crt64->u_mult_inv))
		return -EFAULT;
	ret = z90crypt_unlocked_ioctl(filp, cmd, (unsigned long)crt64);
	if (!ret)
		if (__get_user(i, &crt64->outputdatalength) ||
		    __put_user(i, &crt32u->outputdatalength))
			ret = -EFAULT;
	return ret;
}

static long
z90crypt_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case ICAZ90STATUS:
	case Z90QUIESCE:
	case Z90STAT_TOTALCOUNT:
	case Z90STAT_PCICACOUNT:
	case Z90STAT_PCICCCOUNT:
	case Z90STAT_PCIXCCCOUNT:
	case Z90STAT_PCIXCCMCL2COUNT:
	case Z90STAT_PCIXCCMCL3COUNT:
	case Z90STAT_CEX2CCOUNT:
	case Z90STAT_REQUESTQ_COUNT:
	case Z90STAT_PENDINGQ_COUNT:
	case Z90STAT_TOTALOPEN_COUNT:
	case Z90STAT_DOMAIN_INDEX:
	case Z90STAT_STATUS_MASK:
	case Z90STAT_QDEPTH_MASK:
	case Z90STAT_PERDEV_REQCNT:
		return z90crypt_unlocked_ioctl(filp, cmd, arg);
	case ICARSAMODEXPO:
		return trans_modexpo32(filp, cmd, arg);
	case ICARSACRT:
		return trans_modexpo_crt32(filp, cmd, arg);
	default:
		return -ENOIOCTLCMD;
  	}
}
#endif

/**
 * The module initialization code.
 */
static int __init
z90crypt_init_module(void)
{
	int result, nresult;
	struct proc_dir_entry *entry;

	PDEBUG("PID %d\n", PID());

	if ((domain < -1) || (domain > 15)) {
		PRINTKW("Invalid param: domain = %d.  Not loading.\n", domain);
		return -EINVAL;
	}

	/* Register as misc device with given minor (or get a dynamic one). */
	result = misc_register(&z90crypt_misc_device);
	if (result < 0) {
		PRINTKW(KERN_ERR "misc_register (minor %d) failed with %d\n",
			z90crypt_misc_device.minor, result);
		return result;
	}

	PDEBUG("Registered " DEV_NAME " with result %d\n", result);

	result = create_z90crypt(&domain);
	if (result != 0) {
		PRINTKW("create_z90crypt (domain index %d) failed with %d.\n",
			domain, result);
		result = -ENOMEM;
		goto init_module_cleanup;
	}

	if (result == 0) {
		PRINTKN("Version %d.%d.%d loaded, built on %s %s\n",
			z90crypt_VERSION, z90crypt_RELEASE, z90crypt_VARIANT,
			__DATE__, __TIME__);
		PRINTKN("%s\n", z90main_version);
		PRINTKN("%s\n", z90hardware_version);
		PDEBUG("create_z90crypt (domain index %d) successful.\n",
		       domain);
	} else
		PRINTK("No devices at startup\n");

	/* Initialize globals. */
	spin_lock_init(&queuespinlock);

	INIT_LIST_HEAD(&pending_list);
	pendingq_count = 0;

	INIT_LIST_HEAD(&request_list);
	requestq_count = 0;

	quiesce_z90crypt = 0;

	atomic_set(&total_open, 0);
	atomic_set(&z90crypt_step, 0);

	/* Set up the cleanup task. */
	init_timer(&cleanup_timer);
	cleanup_timer.function = z90crypt_cleanup_task;
	cleanup_timer.data = 0;
	cleanup_timer.expires = jiffies + (CLEANUPTIME * HZ);
	add_timer(&cleanup_timer);

	/* Set up the proc file system */
	entry = create_proc_entry("driver/z90crypt", 0644, 0);
	if (entry) {
		entry->nlink = 1;
		entry->data = 0;
		entry->read_proc = z90crypt_status;
		entry->write_proc = z90crypt_status_write;
	}
	else
		PRINTK("Couldn't create z90crypt proc entry\n");
	z90crypt_entry = entry;

	/* Set up the configuration task. */
	init_timer(&config_timer);
	config_timer.function = z90crypt_config_task;
	config_timer.data = 0;
	config_timer.expires = jiffies + (INITIAL_CONFIGTIME * HZ);
	add_timer(&config_timer);

	/* Set up the reader task */
	tasklet_init(&reader_tasklet, z90crypt_reader_task, 0);
	init_timer(&reader_timer);
	reader_timer.function = z90crypt_schedule_reader_task;
	reader_timer.data = 0;
	reader_timer.expires = jiffies + (READERTIME * HZ / 1000);
	add_timer(&reader_timer);

	return 0; // success

init_module_cleanup:
	if ((nresult = misc_deregister(&z90crypt_misc_device)))
		PRINTK("misc_deregister failed with %d.\n", nresult);
	else
		PDEBUG("misc_deregister successful.\n");

	return result; // failure
}

/**
 * The module termination code
 */
static void __exit
z90crypt_cleanup_module(void)
{
	int nresult;

	PDEBUG("PID %d\n", PID());

	remove_proc_entry("driver/z90crypt", 0);

	if ((nresult = misc_deregister(&z90crypt_misc_device)))
		PRINTK("misc_deregister failed with %d.\n", nresult);
	else
		PDEBUG("misc_deregister successful.\n");

	/* Remove the tasks */
	tasklet_kill(&reader_tasklet);
	del_timer(&reader_timer);
	del_timer(&config_timer);
	del_timer(&cleanup_timer);

	destroy_z90crypt();

	PRINTKN("Unloaded.\n");
}

/**
 * Functions running under a process id
 *
 * The I/O functions:
 *     z90crypt_open
 *     z90crypt_release
 *     z90crypt_read
 *     z90crypt_write
 *     z90crypt_unlocked_ioctl
 *     z90crypt_status
 *     z90crypt_status_write
 *	 disable_card
 *	 enable_card
 *
 * Helper functions:
 *     z90crypt_rsa
 *	 z90crypt_prepare
 *	 z90crypt_send
 *	 z90crypt_process_results
 *
 */
static int
z90crypt_open(struct inode *inode, struct file *filp)
{
	struct priv_data *private_data_p;

	if (quiesce_z90crypt)
		return -EQUIESCE;

	private_data_p = kmalloc(sizeof(struct priv_data), GFP_KERNEL);
	if (!private_data_p) {
		PRINTK("Memory allocate failed\n");
		return -ENOMEM;
	}

	memset((void *)private_data_p, 0, sizeof(struct priv_data));
	private_data_p->status = STAT_OPEN;
	private_data_p->opener_pid = PID();
	filp->private_data = private_data_p;
	atomic_inc(&total_open);

	return 0;
}

static int
z90crypt_release(struct inode *inode, struct file *filp)
{
	struct priv_data *private_data_p = filp->private_data;

	PDEBUG("PID %d (filp %p)\n", PID(), filp);

	private_data_p->status = STAT_CLOSED;
	memset(private_data_p, 0, sizeof(struct priv_data));
	kfree(private_data_p);
	atomic_dec(&total_open);

	return 0;
}

/*
 * there are two read functions, of which compile options will choose one
 * without USE_GET_RANDOM_BYTES
 *   => read() always returns -EPERM;
 * otherwise
 *   => read() uses get_random_bytes() kernel function
 */
#ifndef USE_GET_RANDOM_BYTES
/**
 * z90crypt_read will not be supported beyond z90crypt 1.3.1
 */
static ssize_t
z90crypt_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	PDEBUG("filp %p (PID %d)\n", filp, PID());
	return -EPERM;
}
#else // we want to use get_random_bytes
/**
 * read() just returns a string of random bytes.  Since we have no way
 * to generate these cryptographically, we just execute get_random_bytes
 * for the length specified.
 */
#include <linux/random.h>
static ssize_t
z90crypt_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	unsigned char *temp_buff;

	PDEBUG("filp %p (PID %d)\n", filp, PID());

	if (quiesce_z90crypt)
		return -EQUIESCE;
	if (count < 0) {
		PRINTK("Requested random byte count negative: %ld\n", count);
		return -EINVAL;
	}
	if (count > RESPBUFFSIZE) {
		PDEBUG("count[%d] > RESPBUFFSIZE", count);
		return -EINVAL;
	}
	if (count == 0)
		return 0;
	temp_buff = kmalloc(RESPBUFFSIZE, GFP_KERNEL);
	if (!temp_buff) {
		PRINTK("Memory allocate failed\n");
		return -ENOMEM;
	}
	get_random_bytes(temp_buff, count);

	if (copy_to_user(buf, temp_buff, count) != 0) {
		kfree(temp_buff);
		return -EFAULT;
	}
	kfree(temp_buff);
	return count;
}
#endif

/**
 * Write is is not allowed
 */
static ssize_t
z90crypt_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	PDEBUG("filp %p (PID %d)\n", filp, PID());
	return -EPERM;
}

/**
 * New status functions
 */
static inline int
get_status_totalcount(void)
{
	return z90crypt.hdware_info->hdware_mask.st_count;
}

static inline int
get_status_PCICAcount(void)
{
	return z90crypt.hdware_info->type_mask[PCICA].st_count;
}

static inline int
get_status_PCICCcount(void)
{
	return z90crypt.hdware_info->type_mask[PCICC].st_count;
}

static inline int
get_status_PCIXCCcount(void)
{
	return z90crypt.hdware_info->type_mask[PCIXCC_MCL2].st_count +
	       z90crypt.hdware_info->type_mask[PCIXCC_MCL3].st_count;
}

static inline int
get_status_PCIXCCMCL2count(void)
{
	return z90crypt.hdware_info->type_mask[PCIXCC_MCL2].st_count;
}

static inline int
get_status_PCIXCCMCL3count(void)
{
	return z90crypt.hdware_info->type_mask[PCIXCC_MCL3].st_count;
}

static inline int
get_status_CEX2Ccount(void)
{
	return z90crypt.hdware_info->type_mask[CEX2C].st_count;
}

static inline int
get_status_requestq_count(void)
{
	return requestq_count;
}

static inline int
get_status_pendingq_count(void)
{
	return pendingq_count;
}

static inline int
get_status_totalopen_count(void)
{
	return atomic_read(&total_open);
}

static inline int
get_status_domain_index(void)
{
	return z90crypt.cdx;
}

static inline unsigned char *
get_status_status_mask(unsigned char status[Z90CRYPT_NUM_APS])
{
	int i, ix;

	memcpy(status, z90crypt.hdware_info->device_type_array,
	       Z90CRYPT_NUM_APS);

	for (i = 0; i < get_status_totalcount(); i++) {
		ix = SHRT2LONG(i);
		if (LONG2DEVPTR(ix)->user_disabled)
			status[ix] = 0x0d;
	}

	return status;
}

static inline unsigned char *
get_status_qdepth_mask(unsigned char qdepth[Z90CRYPT_NUM_APS])
{
	int i, ix;

	memset(qdepth, 0, Z90CRYPT_NUM_APS);

	for (i = 0; i < get_status_totalcount(); i++) {
		ix = SHRT2LONG(i);
		qdepth[ix] = LONG2DEVPTR(ix)->dev_caller_count;
	}

	return qdepth;
}

static inline unsigned int *
get_status_perdevice_reqcnt(unsigned int reqcnt[Z90CRYPT_NUM_APS])
{
	int i, ix;

	memset(reqcnt, 0, Z90CRYPT_NUM_APS * sizeof(int));

	for (i = 0; i < get_status_totalcount(); i++) {
		ix = SHRT2LONG(i);
		reqcnt[ix] = LONG2DEVPTR(ix)->dev_total_req_cnt;
	}

	return reqcnt;
}

static inline void
init_work_element(struct work_element *we_p,
		  struct priv_data *priv_data, pid_t pid)
{
	int step;

	we_p->requestptr = (unsigned char *)we_p + sizeof(struct work_element);
	/* Come up with a unique id for this caller. */
	step = atomic_inc_return(&z90crypt_step);
	memcpy(we_p->caller_id+0, (void *) &pid, sizeof(pid));
	memcpy(we_p->caller_id+4, (void *) &step, sizeof(step));
	we_p->pid = pid;
	we_p->priv_data = priv_data;
	we_p->status[0] = STAT_DEFAULT;
	we_p->audit[0] = 0x00;
	we_p->audit[1] = 0x00;
	we_p->audit[2] = 0x00;
	we_p->resp_buff_size = 0;
	we_p->retcode = 0;
	we_p->devindex = -1;
	we_p->devtype = -1;
	atomic_set(&we_p->alarmrung, 0);
	init_waitqueue_head(&we_p->waitq);
	INIT_LIST_HEAD(&(we_p->liste));
}

static inline int
allocate_work_element(struct work_element **we_pp,
		      struct priv_data *priv_data_p, pid_t pid)
{
	struct work_element *we_p;

	we_p = (struct work_element *) get_zeroed_page(GFP_KERNEL);
	if (!we_p)
		return -ENOMEM;
	init_work_element(we_p, priv_data_p, pid);
	*we_pp = we_p;
	return 0;
}

static inline void
remove_device(struct device *device_p)
{
	if (!device_p || (device_p->disabled != 0))
		return;
	device_p->disabled = 1;
	z90crypt.hdware_info->type_mask[device_p->dev_type].disabled_count++;
	z90crypt.hdware_info->hdware_mask.disabled_count++;
}

/**
 * Bitlength limits for each card
 *
 * There are new MCLs which allow more bitlengths. See the table for details.
 * The MCL must be applied and the newer bitlengths enabled for these to work.
 *
 * Card Type    Old limit    New limit
 * PCICA          ??-2048     same (the lower limit is less than 128 bit...)
 * PCICC         512-1024     512-2048
 * PCIXCC_MCL2   512-2048     ----- (applying any GA LIC will make an MCL3 card)
 * PCIXCC_MCL3   -----        128-2048
 * CEX2C         512-2048     128-2048
 *
 * ext_bitlens (extended bitlengths) is a global, since you should not apply an
 * MCL to just one card in a machine. We assume, at first, that all cards have
 * these capabilities.
 */
int ext_bitlens = 1; // This is global
#define PCIXCC_MIN_MOD_SIZE	 16	//  128 bits
#define OLD_PCIXCC_MIN_MOD_SIZE	 64	//  512 bits
#define PCICC_MIN_MOD_SIZE	 64	//  512 bits
#define OLD_PCICC_MAX_MOD_SIZE	128	// 1024 bits
#define MAX_MOD_SIZE		256	// 2048 bits

static inline int
select_device_type(int *dev_type_p, int bytelength)
{
	static int count = 0;
	int PCICA_avail, PCIXCC_MCL3_avail, CEX2C_avail, index_to_use;
	struct status *stat;
	if ((*dev_type_p != PCICC) && (*dev_type_p != PCICA) &&
	    (*dev_type_p != PCIXCC_MCL2) && (*dev_type_p != PCIXCC_MCL3) &&
	    (*dev_type_p != CEX2C) && (*dev_type_p != ANYDEV))
		return -1;
	if (*dev_type_p != ANYDEV) {
		stat = &z90crypt.hdware_info->type_mask[*dev_type_p];
		if (stat->st_count >
		    (stat->disabled_count + stat->user_disabled_count))
			return 0;
		return -1;
	}

	/* Assumption: PCICA, PCIXCC_MCL3, and CEX2C are all similar in speed */
	stat = &z90crypt.hdware_info->type_mask[PCICA];
	PCICA_avail = stat->st_count -
			(stat->disabled_count + stat->user_disabled_count);
	stat = &z90crypt.hdware_info->type_mask[PCIXCC_MCL3];
	PCIXCC_MCL3_avail = stat->st_count -
			(stat->disabled_count + stat->user_disabled_count);
	stat = &z90crypt.hdware_info->type_mask[CEX2C];
	CEX2C_avail = stat->st_count -
			(stat->disabled_count + stat->user_disabled_count);
	if (PCICA_avail || PCIXCC_MCL3_avail || CEX2C_avail) {
		/**
		 * bitlength is a factor, PCICA is the most capable, even with
		 * the new MCL for PCIXCC.
		 */
		if ((bytelength < PCIXCC_MIN_MOD_SIZE) ||
		    (!ext_bitlens && (bytelength < OLD_PCIXCC_MIN_MOD_SIZE))) {
			if (!PCICA_avail)
				return -1;
			else {
				*dev_type_p = PCICA;
				return 0;
			}
		}

		index_to_use = count % (PCICA_avail + PCIXCC_MCL3_avail +
					CEX2C_avail);
		if (index_to_use < PCICA_avail)
			*dev_type_p = PCICA;
		else if (index_to_use < (PCICA_avail + PCIXCC_MCL3_avail))
			*dev_type_p = PCIXCC_MCL3;
		else
			*dev_type_p = CEX2C;
		count++;
		return 0;
	}

	/* Less than OLD_PCIXCC_MIN_MOD_SIZE cannot go to a PCIXCC_MCL2 */
	if (bytelength < OLD_PCIXCC_MIN_MOD_SIZE)
		return -1;
	stat = &z90crypt.hdware_info->type_mask[PCIXCC_MCL2];
	if (stat->st_count >
	    (stat->disabled_count + stat->user_disabled_count)) {
		*dev_type_p = PCIXCC_MCL2;
		return 0;
	}

	/**
	 * Less than PCICC_MIN_MOD_SIZE or more than OLD_PCICC_MAX_MOD_SIZE
	 * (if we don't have the MCL applied and the newer bitlengths enabled)
	 * cannot go to a PCICC
	 */
	if ((bytelength < PCICC_MIN_MOD_SIZE) ||
	    (!ext_bitlens && (bytelength > OLD_PCICC_MAX_MOD_SIZE))) {
		return -1;
	}
	stat = &z90crypt.hdware_info->type_mask[PCICC];
	if (stat->st_count >
	    (stat->disabled_count + stat->user_disabled_count)) {
		*dev_type_p = PCICC;
		return 0;
	}

	return -1;
}

/**
 * Try the selected number, then the selected type (can be ANYDEV)
 */
static inline int
select_device(int *dev_type_p, int *device_nr_p, int bytelength)
{
	int i, indx, devTp, low_count, low_indx;
	struct device_x *index_p;
	struct device *dev_ptr;

	PDEBUG("device type = %d, index = %d\n", *dev_type_p, *device_nr_p);
	if ((*device_nr_p >= 0) && (*device_nr_p < Z90CRYPT_NUM_DEVS)) {
		PDEBUG("trying index = %d\n", *device_nr_p);
		dev_ptr = z90crypt.device_p[*device_nr_p];

		if (dev_ptr &&
		    (dev_ptr->dev_stat != DEV_GONE) &&
		    (dev_ptr->disabled == 0) &&
		    (dev_ptr->user_disabled == 0)) {
			PDEBUG("selected by number, index = %d\n",
			       *device_nr_p);
			*dev_type_p = dev_ptr->dev_type;
			return *device_nr_p;
		}
	}
	*device_nr_p = -1;
	PDEBUG("trying type = %d\n", *dev_type_p);
	devTp = *dev_type_p;
	if (select_device_type(&devTp, bytelength) == -1) {
		PDEBUG("failed to select by type\n");
		return -1;
	}
	PDEBUG("selected type = %d\n", devTp);
	index_p = &z90crypt.hdware_info->type_x_addr[devTp];
	low_count = 0x0000FFFF;
	low_indx = -1;
	for (i = 0; i < z90crypt.hdware_info->type_mask[devTp].st_count; i++) {
		indx = index_p->device_index[i];
		dev_ptr = z90crypt.device_p[indx];
		if (dev_ptr &&
		    (dev_ptr->dev_stat != DEV_GONE) &&
		    (dev_ptr->disabled == 0) &&
		    (dev_ptr->user_disabled == 0) &&
		    (devTp == dev_ptr->dev_type) &&
		    (low_count > dev_ptr->dev_caller_count)) {
			low_count = dev_ptr->dev_caller_count;
			low_indx = indx;
		}
	}
	*device_nr_p = low_indx;
	return low_indx;
}

static inline int
send_to_crypto_device(struct work_element *we_p)
{
	struct caller *caller_p;
	struct device *device_p;
	int dev_nr;
	int bytelen = ((struct ica_rsa_modexpo *)we_p->buffer)->inputdatalength;

	if (!we_p->requestptr)
		return SEN_FATAL_ERROR;
	caller_p = (struct caller *)we_p->requestptr;
	dev_nr = we_p->devindex;
	if (select_device(&we_p->devtype, &dev_nr, bytelen) == -1) {
		if (z90crypt.hdware_info->hdware_mask.st_count != 0)
			return SEN_RETRY;
		else
			return SEN_NOT_AVAIL;
	}
	we_p->devindex = dev_nr;
	device_p = z90crypt.device_p[dev_nr];
	if (!device_p)
		return SEN_NOT_AVAIL;
	if (device_p->dev_type != we_p->devtype)
		return SEN_RETRY;
	if (device_p->dev_caller_count >= device_p->dev_q_depth)
		return SEN_QUEUE_FULL;
	PDEBUG("device number prior to send: %d\n", dev_nr);
	switch (send_to_AP(dev_nr, z90crypt.cdx,
			   caller_p->caller_dev_dep_req_l,
			   caller_p->caller_dev_dep_req_p)) {
	case DEV_SEN_EXCEPTION:
		PRINTKC("Exception during send to device %d\n", dev_nr);
		z90crypt.terminating = 1;
		return SEN_FATAL_ERROR;
	case DEV_GONE:
		PRINTK("Device %d not available\n", dev_nr);
		remove_device(device_p);
		return SEN_NOT_AVAIL;
	case DEV_EMPTY:
		return SEN_NOT_AVAIL;
	case DEV_NO_WORK:
		return SEN_FATAL_ERROR;
	case DEV_BAD_MESSAGE:
		return SEN_USER_ERROR;
	case DEV_QUEUE_FULL:
		return SEN_QUEUE_FULL;
	default:
	case DEV_ONLINE:
		break;
	}
	list_add_tail(&(caller_p->caller_liste), &(device_p->dev_caller_list));
	device_p->dev_caller_count++;
	return 0;
}

/**
 * Send puts the user's work on one of two queues:
 *   the pending queue if the send was successful
 *   the request queue if the send failed because device full or busy
 */
static inline int
z90crypt_send(struct work_element *we_p, const char *buf)
{
	int rv;

	PDEBUG("PID %d\n", PID());

	if (CHK_RDWRMASK(we_p->status[0]) != STAT_NOWORK) {
		PDEBUG("PID %d tried to send more work but has outstanding "
		       "work.\n", PID());
		return -EWORKPEND;
	}
	we_p->devindex = -1; // Reset device number
	spin_lock_irq(&queuespinlock);
	rv = send_to_crypto_device(we_p);
	switch (rv) {
	case 0:
		we_p->requestsent = jiffies;
		we_p->audit[0] |= FP_SENT;
		list_add_tail(&we_p->liste, &pending_list);
		++pendingq_count;
		we_p->audit[0] |= FP_PENDING;
		break;
	case SEN_BUSY:
	case SEN_QUEUE_FULL:
		rv = 0;
		we_p->devindex = -1; // any device will do
		we_p->requestsent = jiffies;
		list_add_tail(&we_p->liste, &request_list);
		++requestq_count;
		we_p->audit[0] |= FP_REQUEST;
		break;
	case SEN_RETRY:
		rv = -ERESTARTSYS;
		break;
	case SEN_NOT_AVAIL:
		PRINTK("*** No devices available.\n");
		rv = we_p->retcode = -ENODEV;
		we_p->status[0] |= STAT_FAILED;
		break;
	case REC_OPERAND_INV:
	case REC_OPERAND_SIZE:
	case REC_EVEN_MOD:
	case REC_INVALID_PAD:
		rv = we_p->retcode = -EINVAL;
		we_p->status[0] |= STAT_FAILED;
		break;
	default:
		we_p->retcode = rv;
		we_p->status[0] |= STAT_FAILED;
		break;
	}
	if (rv != -ERESTARTSYS)
		SET_RDWRMASK(we_p->status[0], STAT_WRITTEN);
	spin_unlock_irq(&queuespinlock);
	if (rv == 0)
		tasklet_schedule(&reader_tasklet);
	return rv;
}

/**
 * process_results copies the user's work from kernel space.
 */
static inline int
z90crypt_process_results(struct work_element *we_p, char __user *buf)
{
	int rv;

	PDEBUG("we_p %p (PID %d)\n", we_p, PID());

	LONG2DEVPTR(we_p->devindex)->dev_total_req_cnt++;
	SET_RDWRMASK(we_p->status[0], STAT_READPEND);

	rv = 0;
	if (!we_p->buffer) {
		PRINTK("we_p %p PID %d in STAT_READPEND: buffer NULL.\n",
			we_p, PID());
		rv = -ENOBUFF;
	}

	if (!rv)
		if ((rv = copy_to_user(buf, we_p->buffer, we_p->buff_size))) {
			PDEBUG("copy_to_user failed: rv = %d\n", rv);
			rv = -EFAULT;
		}

	if (!rv)
		rv = we_p->retcode;
	if (!rv)
		if (we_p->resp_buff_size
		    &&	copy_to_user(we_p->resp_addr, we_p->resp_buff,
				     we_p->resp_buff_size))
			rv = -EFAULT;

	SET_RDWRMASK(we_p->status[0], STAT_NOWORK);
	return rv;
}

static unsigned char NULL_psmid[8] =
{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/**
 * Used in device configuration functions
 */
#define MAX_RESET 90

/**
 * This is used only for PCICC support
 */
static inline int
is_PKCS11_padded(unsigned char *buffer, int length)
{
	int i;
	if ((buffer[0] != 0x00) || (buffer[1] != 0x01))
		return 0;
	for (i = 2; i < length; i++)
		if (buffer[i] != 0xFF)
			break;
	if ((i < 10) || (i == length))
		return 0;
	if (buffer[i] != 0x00)
		return 0;
	return 1;
}

/**
 * This is used only for PCICC support
 */
static inline int
is_PKCS12_padded(unsigned char *buffer, int length)
{
	int i;
	if ((buffer[0] != 0x00) || (buffer[1] != 0x02))
		return 0;
	for (i = 2; i < length; i++)
		if (buffer[i] == 0x00)
			break;
	if ((i < 10) || (i == length))
		return 0;
	if (buffer[i] != 0x00)
		return 0;
	return 1;
}

/**
 * builds struct caller and converts message from generic format to
 * device-dependent format
 * func is ICARSAMODEXPO or ICARSACRT
 * function is PCI_FUNC_KEY_ENCRYPT or PCI_FUNC_KEY_DECRYPT
 */
static inline int
build_caller(struct work_element *we_p, short function)
{
	int rv;
	struct caller *caller_p = (struct caller *)we_p->requestptr;

	if ((we_p->devtype != PCICC) && (we_p->devtype != PCICA) &&
	    (we_p->devtype != PCIXCC_MCL2) && (we_p->devtype != PCIXCC_MCL3) &&
	    (we_p->devtype != CEX2C))
		return SEN_NOT_AVAIL;

	memcpy(caller_p->caller_id, we_p->caller_id,
	       sizeof(caller_p->caller_id));
	caller_p->caller_dev_dep_req_p = caller_p->caller_dev_dep_req;
	caller_p->caller_dev_dep_req_l = MAX_RESPONSE_SIZE;
	caller_p->caller_buf_p = we_p->buffer;
	INIT_LIST_HEAD(&(caller_p->caller_liste));

	rv = convert_request(we_p->buffer, we_p->funccode, function,
			     z90crypt.cdx, we_p->devtype,
			     &caller_p->caller_dev_dep_req_l,
			     caller_p->caller_dev_dep_req_p);
	if (rv) {
		if (rv == SEN_NOT_AVAIL)
			PDEBUG("request can't be processed on hdwr avail\n");
		else
			PRINTK("Error from convert_request: %d\n", rv);
	}
	else
		memcpy(&(caller_p->caller_dev_dep_req_p[4]), we_p->caller_id,8);
	return rv;
}

static inline void
unbuild_caller(struct device *device_p, struct caller *caller_p)
{
	if (!caller_p)
		return;
	if (caller_p->caller_liste.next && caller_p->caller_liste.prev)
		if (!list_empty(&caller_p->caller_liste)) {
			list_del_init(&caller_p->caller_liste);
			device_p->dev_caller_count--;
		}
	memset(caller_p->caller_id, 0, sizeof(caller_p->caller_id));
}

static inline int
get_crypto_request_buffer(struct work_element *we_p)
{
	struct ica_rsa_modexpo *mex_p;
	struct ica_rsa_modexpo_crt *crt_p;
	unsigned char *temp_buffer;
	short function;
	int rv;

	mex_p =	(struct ica_rsa_modexpo *) we_p->buffer;
	crt_p = (struct ica_rsa_modexpo_crt *) we_p->buffer;

	PDEBUG("device type input = %d\n", we_p->devtype);

	if (z90crypt.terminating)
		return REC_NO_RESPONSE;
	if (memcmp(we_p->caller_id, NULL_psmid, 8) == 0) {
		PRINTK("psmid zeroes\n");
		return SEN_FATAL_ERROR;
	}
	if (!we_p->buffer) {
		PRINTK("buffer pointer NULL\n");
		return SEN_USER_ERROR;
	}
	if (!we_p->requestptr) {
		PRINTK("caller pointer NULL\n");
		return SEN_USER_ERROR;
	}

	if ((we_p->devtype != PCICA) && (we_p->devtype != PCICC) &&
	    (we_p->devtype != PCIXCC_MCL2) && (we_p->devtype != PCIXCC_MCL3) &&
	    (we_p->devtype != CEX2C) && (we_p->devtype != ANYDEV)) {
		PRINTK("invalid device type\n");
		return SEN_USER_ERROR;
	}

	if ((mex_p->inputdatalength < 1) ||
	    (mex_p->inputdatalength > MAX_MOD_SIZE)) {
		PRINTK("inputdatalength[%d] is not valid\n",
		       mex_p->inputdatalength);
		return SEN_USER_ERROR;
	}

	if (mex_p->outputdatalength < mex_p->inputdatalength) {
		PRINTK("outputdatalength[%d] < inputdatalength[%d]\n",
		       mex_p->outputdatalength, mex_p->inputdatalength);
		return SEN_USER_ERROR;
	}

	if (!mex_p->inputdata || !mex_p->outputdata) {
		PRINTK("inputdata[%p] or outputdata[%p] is NULL\n",
		       mex_p->outputdata, mex_p->inputdata);
		return SEN_USER_ERROR;
	}

	/**
	 * As long as outputdatalength is big enough, we can set the
	 * outputdatalength equal to the inputdatalength, since that is the
	 * number of bytes we will copy in any case
	 */
	mex_p->outputdatalength = mex_p->inputdatalength;

	rv = 0;
	switch (we_p->funccode) {
	case ICARSAMODEXPO:
		if (!mex_p->b_key || !mex_p->n_modulus)
			rv = SEN_USER_ERROR;
		break;
	case ICARSACRT:
		if (!IS_EVEN(crt_p->inputdatalength)) {
			PRINTK("inputdatalength[%d] is odd, CRT form\n",
			       crt_p->inputdatalength);
			rv = SEN_USER_ERROR;
			break;
		}
		if (!crt_p->bp_key ||
		    !crt_p->bq_key ||
		    !crt_p->np_prime ||
		    !crt_p->nq_prime ||
		    !crt_p->u_mult_inv) {
			PRINTK("CRT form, bad data: %p/%p/%p/%p/%p\n",
			       crt_p->bp_key, crt_p->bq_key,
			       crt_p->np_prime, crt_p->nq_prime,
			       crt_p->u_mult_inv);
			rv = SEN_USER_ERROR;
		}
		break;
	default:
		PRINTK("bad func = %d\n", we_p->funccode);
		rv = SEN_USER_ERROR;
		break;
	}
	if (rv != 0)
		return rv;

	if (select_device_type(&we_p->devtype, mex_p->inputdatalength) < 0)
		return SEN_NOT_AVAIL;

	temp_buffer = (unsigned char *)we_p + sizeof(struct work_element) +
		      sizeof(struct caller);
	if (copy_from_user(temp_buffer, mex_p->inputdata,
			   mex_p->inputdatalength) != 0)
		return SEN_RELEASED;

	function = PCI_FUNC_KEY_ENCRYPT;
	switch (we_p->devtype) {
	/* PCICA does everything with a simple RSA mod-expo operation */
	case PCICA:
		function = PCI_FUNC_KEY_ENCRYPT;
		break;
	/**
	 * PCIXCC_MCL2 does all Mod-Expo form with a simple RSA mod-expo
	 * operation, and all CRT forms with a PKCS-1.2 format decrypt.
	 * PCIXCC_MCL3 and CEX2C do all Mod-Expo and CRT forms with a simple RSA
	 * mod-expo operation
	 */
	case PCIXCC_MCL2:
		if (we_p->funccode == ICARSAMODEXPO)
			function = PCI_FUNC_KEY_ENCRYPT;
		else
			function = PCI_FUNC_KEY_DECRYPT;
		break;
	case PCIXCC_MCL3:
	case CEX2C:
		if (we_p->funccode == ICARSAMODEXPO)
			function = PCI_FUNC_KEY_ENCRYPT;
		else
			function = PCI_FUNC_KEY_DECRYPT;
		break;
	/**
	 * PCICC does everything as a PKCS-1.2 format request
	 */
	case PCICC:
		/* PCICC cannot handle input that is is PKCS#1.1 padded */
		if (is_PKCS11_padded(temp_buffer, mex_p->inputdatalength)) {
			return SEN_NOT_AVAIL;
		}
		if (we_p->funccode == ICARSAMODEXPO) {
			if (is_PKCS12_padded(temp_buffer,
					     mex_p->inputdatalength))
				function = PCI_FUNC_KEY_ENCRYPT;
			else
				function = PCI_FUNC_KEY_DECRYPT;
		} else
			/* all CRT forms are decrypts */
			function = PCI_FUNC_KEY_DECRYPT;
		break;
	}
	PDEBUG("function: %04x\n", function);
	rv = build_caller(we_p, function);
	PDEBUG("rv from build_caller = %d\n", rv);
	return rv;
}

static inline int
z90crypt_prepare(struct work_element *we_p, unsigned int funccode,
		 const char __user *buffer)
{
	int rv;

	we_p->devindex = -1;
	if (funccode == ICARSAMODEXPO)
		we_p->buff_size = sizeof(struct ica_rsa_modexpo);
	else
		we_p->buff_size = sizeof(struct ica_rsa_modexpo_crt);

	if (copy_from_user(we_p->buffer, buffer, we_p->buff_size))
		return -EFAULT;

	we_p->audit[0] |= FP_COPYFROM;
	SET_RDWRMASK(we_p->status[0], STAT_WRITTEN);
	we_p->funccode = funccode;
	we_p->devtype = -1;
	we_p->audit[0] |= FP_BUFFREQ;
	rv = get_crypto_request_buffer(we_p);
	switch (rv) {
	case 0:
		we_p->audit[0] |= FP_BUFFGOT;
		break;
	case SEN_USER_ERROR:
		rv = -EINVAL;
		break;
	case SEN_QUEUE_FULL:
		rv = 0;
		break;
	case SEN_RELEASED:
		rv = -EFAULT;
		break;
	case REC_NO_RESPONSE:
		rv = -ENODEV;
		break;
	case SEN_NOT_AVAIL:
	case EGETBUFF:
		rv = -EGETBUFF;
		break;
	default:
		PRINTK("rv = %d\n", rv);
		rv = -EGETBUFF;
		break;
	}
	if (CHK_RDWRMASK(we_p->status[0]) == STAT_WRITTEN)
		SET_RDWRMASK(we_p->status[0], STAT_DEFAULT);
	return rv;
}

static inline void
purge_work_element(struct work_element *we_p)
{
	struct list_head *lptr;

	spin_lock_irq(&queuespinlock);
	list_for_each(lptr, &request_list) {
		if (lptr == &we_p->liste) {
			list_del_init(lptr);
			requestq_count--;
			break;
		}
	}
	list_for_each(lptr, &pending_list) {
		if (lptr == &we_p->liste) {
			list_del_init(lptr);
			pendingq_count--;
			break;
		}
	}
	spin_unlock_irq(&queuespinlock);
}

/**
 * Build the request and send it.
 */
static inline int
z90crypt_rsa(struct priv_data *private_data_p, pid_t pid,
	     unsigned int cmd, unsigned long arg)
{
	struct work_element *we_p;
	int rv;

	if ((rv = allocate_work_element(&we_p, private_data_p, pid))) {
		PDEBUG("PID %d: allocate_work_element returned ENOMEM\n", pid);
		return rv;
	}
	if ((rv = z90crypt_prepare(we_p, cmd, (const char __user *)arg)))
		PDEBUG("PID %d: rv = %d from z90crypt_prepare\n", pid, rv);
	if (!rv)
		if ((rv = z90crypt_send(we_p, (const char *)arg)))
			PDEBUG("PID %d: rv %d from z90crypt_send.\n", pid, rv);
	if (!rv) {
		we_p->audit[0] |= FP_ASLEEP;
		wait_event(we_p->waitq, atomic_read(&we_p->alarmrung));
		we_p->audit[0] |= FP_AWAKE;
		rv = we_p->retcode;
	}
	if (!rv)
		rv = z90crypt_process_results(we_p, (char __user *)arg);

	if ((we_p->status[0] & STAT_FAILED)) {
		switch (rv) {
		/**
		 * EINVAL *after* receive is almost always a padding error or
		 * length error issued by a coprocessor (not an accelerator).
		 * We convert this return value to -EGETBUFF which should
		 * trigger a fallback to software.
		 */
		case -EINVAL:
			if (we_p->devtype != PCICA)
				rv = -EGETBUFF;
			break;
		case -ETIMEOUT:
			if (z90crypt.mask.st_count > 0)
				rv = -ERESTARTSYS; // retry with another
			else
				rv = -ENODEV; // no cards left
		/* fall through to clean up request queue */
		case -ERESTARTSYS:
		case -ERELEASED:
			switch (CHK_RDWRMASK(we_p->status[0])) {
			case STAT_WRITTEN:
				purge_work_element(we_p);
				break;
			case STAT_READPEND:
			case STAT_NOWORK:
			default:
				break;
			}
			break;
		default:
			we_p->status[0] ^= STAT_FAILED;
			break;
		}
	}
	free_page((long)we_p);
	return rv;
}

/**
 * This function is a little long, but it's really just one large switch
 * statement.
 */
static long
z90crypt_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct priv_data *private_data_p = filp->private_data;
	unsigned char *status;
	unsigned char *qdepth;
	unsigned int *reqcnt;
	struct ica_z90_status *pstat;
	int ret, i, loopLim, tempstat;
	static int deprecated_msg_count1 = 0;
	static int deprecated_msg_count2 = 0;

	PDEBUG("filp %p (PID %d), cmd 0x%08X\n", filp, PID(), cmd);
	PDEBUG("cmd 0x%08X: dir %s, size 0x%04X, type 0x%02X, nr 0x%02X\n",
		cmd,
		!_IOC_DIR(cmd) ? "NO"
		: ((_IOC_DIR(cmd) == (_IOC_READ|_IOC_WRITE)) ? "RW"
		: ((_IOC_DIR(cmd) == _IOC_READ) ? "RD"
		: "WR")),
		_IOC_SIZE(cmd), _IOC_TYPE(cmd), _IOC_NR(cmd));

	if (_IOC_TYPE(cmd) != Z90_IOCTL_MAGIC) {
		PRINTK("cmd 0x%08X contains bad magic\n", cmd);
		return -ENOTTY;
	}

	ret = 0;
	switch (cmd) {
	case ICARSAMODEXPO:
	case ICARSACRT:
		if (quiesce_z90crypt) {
			ret = -EQUIESCE;
			break;
		}
		ret = -ENODEV; // Default if no devices
		loopLim = z90crypt.hdware_info->hdware_mask.st_count -
			(z90crypt.hdware_info->hdware_mask.disabled_count +
			 z90crypt.hdware_info->hdware_mask.user_disabled_count);
		for (i = 0; i < loopLim; i++) {
			ret = z90crypt_rsa(private_data_p, PID(), cmd, arg);
			if (ret != -ERESTARTSYS)
				break;
		}
		if (ret == -ERESTARTSYS)
			ret = -ENODEV;
		break;

	case Z90STAT_TOTALCOUNT:
		tempstat = get_status_totalcount();
		if (copy_to_user((int __user *)arg, &tempstat,sizeof(int)) != 0)
			ret = -EFAULT;
		break;

	case Z90STAT_PCICACOUNT:
		tempstat = get_status_PCICAcount();
		if (copy_to_user((int __user *)arg, &tempstat, sizeof(int)) != 0)
			ret = -EFAULT;
		break;

	case Z90STAT_PCICCCOUNT:
		tempstat = get_status_PCICCcount();
		if (copy_to_user((int __user *)arg, &tempstat, sizeof(int)) != 0)
			ret = -EFAULT;
		break;

	case Z90STAT_PCIXCCMCL2COUNT:
		tempstat = get_status_PCIXCCMCL2count();
		if (copy_to_user((int __user *)arg, &tempstat, sizeof(int)) != 0)
			ret = -EFAULT;
		break;

	case Z90STAT_PCIXCCMCL3COUNT:
		tempstat = get_status_PCIXCCMCL3count();
		if (copy_to_user((int __user *)arg, &tempstat, sizeof(int)) != 0)
			ret = -EFAULT;
		break;

	case Z90STAT_CEX2CCOUNT:
		tempstat = get_status_CEX2Ccount();
		if (copy_to_user((int __user *)arg, &tempstat, sizeof(int)) != 0)
			ret = -EFAULT;
		break;

	case Z90STAT_REQUESTQ_COUNT:
		tempstat = get_status_requestq_count();
		if (copy_to_user((int __user *)arg, &tempstat, sizeof(int)) != 0)
			ret = -EFAULT;
		break;

	case Z90STAT_PENDINGQ_COUNT:
		tempstat = get_status_pendingq_count();
		if (copy_to_user((int __user *)arg, &tempstat, sizeof(int)) != 0)
			ret = -EFAULT;
		break;

	case Z90STAT_TOTALOPEN_COUNT:
		tempstat = get_status_totalopen_count();
		if (copy_to_user((int __user *)arg, &tempstat, sizeof(int)) != 0)
			ret = -EFAULT;
		break;

	case Z90STAT_DOMAIN_INDEX:
		tempstat = get_status_domain_index();
		if (copy_to_user((int __user *)arg, &tempstat, sizeof(int)) != 0)
			ret = -EFAULT;
		break;

	case Z90STAT_STATUS_MASK:
		status = kmalloc(Z90CRYPT_NUM_APS, GFP_KERNEL);
		if (!status) {
			PRINTK("kmalloc for status failed!\n");
			ret = -ENOMEM;
			break;
		}
		get_status_status_mask(status);
		if (copy_to_user((char __user *) arg, status, Z90CRYPT_NUM_APS)
									!= 0)
			ret = -EFAULT;
		kfree(status);
		break;

	case Z90STAT_QDEPTH_MASK:
		qdepth = kmalloc(Z90CRYPT_NUM_APS, GFP_KERNEL);
		if (!qdepth) {
			PRINTK("kmalloc for qdepth failed!\n");
			ret = -ENOMEM;
			break;
		}
		get_status_qdepth_mask(qdepth);
		if (copy_to_user((char __user *) arg, qdepth, Z90CRYPT_NUM_APS) != 0)
			ret = -EFAULT;
		kfree(qdepth);
		break;

	case Z90STAT_PERDEV_REQCNT:
		reqcnt = kmalloc(sizeof(int) * Z90CRYPT_NUM_APS, GFP_KERNEL);
		if (!reqcnt) {
			PRINTK("kmalloc for reqcnt failed!\n");
			ret = -ENOMEM;
			break;
		}
		get_status_perdevice_reqcnt(reqcnt);
		if (copy_to_user((char __user *) arg, reqcnt,
				 Z90CRYPT_NUM_APS * sizeof(int)) != 0)
			ret = -EFAULT;
		kfree(reqcnt);
		break;

		/* THIS IS DEPRECATED.	USE THE NEW STATUS CALLS */
	case ICAZ90STATUS:
		if (deprecated_msg_count1 < 20) {
			PRINTK("deprecated call to ioctl (ICAZ90STATUS)!\n");
			deprecated_msg_count1++;
			if (deprecated_msg_count1 == 20)
				PRINTK("No longer issuing messages related to "
				       "deprecated call to ICAZ90STATUS.\n");
		}

		pstat = kmalloc(sizeof(struct ica_z90_status), GFP_KERNEL);
		if (!pstat) {
			PRINTK("kmalloc for pstat failed!\n");
			ret = -ENOMEM;
			break;
		}

		pstat->totalcount	 = get_status_totalcount();
		pstat->leedslitecount	 = get_status_PCICAcount();
		pstat->leeds2count	 = get_status_PCICCcount();
		pstat->requestqWaitCount = get_status_requestq_count();
		pstat->pendingqWaitCount = get_status_pendingq_count();
		pstat->totalOpenCount	 = get_status_totalopen_count();
		pstat->cryptoDomain	 = get_status_domain_index();
		get_status_status_mask(pstat->status);
		get_status_qdepth_mask(pstat->qdepth);

		if (copy_to_user((struct ica_z90_status __user *) arg, pstat,
				 sizeof(struct ica_z90_status)) != 0)
			ret = -EFAULT;
		kfree(pstat);
		break;

		/* THIS IS DEPRECATED.	USE THE NEW STATUS CALLS */
	case Z90STAT_PCIXCCCOUNT:
		if (deprecated_msg_count2 < 20) {
			PRINTK("deprecated ioctl (Z90STAT_PCIXCCCOUNT)!\n");
			deprecated_msg_count2++;
			if (deprecated_msg_count2 == 20)
				PRINTK("No longer issuing messages about depre"
				       "cated ioctl Z90STAT_PCIXCCCOUNT.\n");
		}

		tempstat = get_status_PCIXCCcount();
		if (copy_to_user((int *)arg, &tempstat, sizeof(int)) != 0)
			ret = -EFAULT;
		break;

	case Z90QUIESCE:
		if (current->euid != 0) {
			PRINTK("QUIESCE fails: euid %d\n",
			       current->euid);
			ret = -EACCES;
		} else {
			PRINTK("QUIESCE device from PID %d\n", PID());
			quiesce_z90crypt = 1;
		}
		break;

	default:
		/* user passed an invalid IOCTL number */
		PDEBUG("cmd 0x%08X contains invalid ioctl code\n", cmd);
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static inline int
sprintcl(unsigned char *outaddr, unsigned char *addr, unsigned int len)
{
	int hl, i;

	hl = 0;
	for (i = 0; i < len; i++)
		hl += sprintf(outaddr+hl, "%01x", (unsigned int) addr[i]);
	hl += sprintf(outaddr+hl, " ");

	return hl;
}

static inline int
sprintrw(unsigned char *outaddr, unsigned char *addr, unsigned int len)
{
	int hl, inl, c, cx;

	hl = sprintf(outaddr, "	   ");
	inl = 0;
	for (c = 0; c < (len / 16); c++) {
		hl += sprintcl(outaddr+hl, addr+inl, 16);
		inl += 16;
	}

	cx = len%16;
	if (cx) {
		hl += sprintcl(outaddr+hl, addr+inl, cx);
		inl += cx;
	}

	hl += sprintf(outaddr+hl, "\n");

	return hl;
}

static inline int
sprinthx(unsigned char *title, unsigned char *outaddr,
	 unsigned char *addr, unsigned int len)
{
	int hl, inl, r, rx;

	hl = sprintf(outaddr, "\n%s\n", title);
	inl = 0;
	for (r = 0; r < (len / 64); r++) {
		hl += sprintrw(outaddr+hl, addr+inl, 64);
		inl += 64;
	}
	rx = len % 64;
	if (rx) {
		hl += sprintrw(outaddr+hl, addr+inl, rx);
		inl += rx;
	}

	hl += sprintf(outaddr+hl, "\n");

	return hl;
}

static inline int
sprinthx4(unsigned char *title, unsigned char *outaddr,
	  unsigned int *array, unsigned int len)
{
	int hl, r;

	hl = sprintf(outaddr, "\n%s\n", title);

	for (r = 0; r < len; r++) {
		if ((r % 8) == 0)
			hl += sprintf(outaddr+hl, "    ");
		hl += sprintf(outaddr+hl, "%08X ", array[r]);
		if ((r % 8) == 7)
			hl += sprintf(outaddr+hl, "\n");
	}

	hl += sprintf(outaddr+hl, "\n");

	return hl;
}

static int
z90crypt_status(char *resp_buff, char **start, off_t offset,
		int count, int *eof, void *data)
{
	unsigned char *workarea;
	int len;

	/* resp_buff is a page. Use the right half for a work area */
	workarea = resp_buff+2000;
	len = 0;
	len += sprintf(resp_buff+len, "\nz90crypt version: %d.%d.%d\n",
		z90crypt_VERSION, z90crypt_RELEASE, z90crypt_VARIANT);
	len += sprintf(resp_buff+len, "Cryptographic domain: %d\n",
		get_status_domain_index());
	len += sprintf(resp_buff+len, "Total device count: %d\n",
		get_status_totalcount());
	len += sprintf(resp_buff+len, "PCICA count: %d\n",
		get_status_PCICAcount());
	len += sprintf(resp_buff+len, "PCICC count: %d\n",
		get_status_PCICCcount());
	len += sprintf(resp_buff+len, "PCIXCC MCL2 count: %d\n",
		get_status_PCIXCCMCL2count());
	len += sprintf(resp_buff+len, "PCIXCC MCL3 count: %d\n",
		get_status_PCIXCCMCL3count());
	len += sprintf(resp_buff+len, "CEX2C count: %d\n",
		get_status_CEX2Ccount());
	len += sprintf(resp_buff+len, "requestq count: %d\n",
		get_status_requestq_count());
	len += sprintf(resp_buff+len, "pendingq count: %d\n",
		get_status_pendingq_count());
	len += sprintf(resp_buff+len, "Total open handles: %d\n\n",
		get_status_totalopen_count());
	len += sprinthx(
		"Online devices: 1: PCICA, 2: PCICC, 3: PCIXCC (MCL2), "
		"4: PCIXCC (MCL3), 5: CEX2C",
		resp_buff+len,
		get_status_status_mask(workarea),
		Z90CRYPT_NUM_APS);
	len += sprinthx("Waiting work element counts",
		resp_buff+len,
		get_status_qdepth_mask(workarea),
		Z90CRYPT_NUM_APS);
	len += sprinthx4(
		"Per-device successfully completed request counts",
		resp_buff+len,
		get_status_perdevice_reqcnt((unsigned int *)workarea),
		Z90CRYPT_NUM_APS);
	*eof = 1;
	memset(workarea, 0, Z90CRYPT_NUM_APS * sizeof(unsigned int));
	return len;
}

static inline void
disable_card(int card_index)
{
	struct device *devp;

	devp = LONG2DEVPTR(card_index);
	if (!devp || devp->user_disabled)
		return;
	devp->user_disabled = 1;
	z90crypt.hdware_info->hdware_mask.user_disabled_count++;
	if (devp->dev_type == -1)
		return;
	z90crypt.hdware_info->type_mask[devp->dev_type].user_disabled_count++;
}

static inline void
enable_card(int card_index)
{
	struct device *devp;

	devp = LONG2DEVPTR(card_index);
	if (!devp || !devp->user_disabled)
		return;
	devp->user_disabled = 0;
	z90crypt.hdware_info->hdware_mask.user_disabled_count--;
	if (devp->dev_type == -1)
		return;
	z90crypt.hdware_info->type_mask[devp->dev_type].user_disabled_count--;
}

static int
z90crypt_status_write(struct file *file, const char __user *buffer,
		      unsigned long count, void *data)
{
	int j, eol;
	unsigned char *lbuf, *ptr;
	unsigned int local_count;

#define LBUFSIZE 1200
	lbuf = kmalloc(LBUFSIZE, GFP_KERNEL);
	if (!lbuf) {
		PRINTK("kmalloc failed!\n");
		return 0;
	}

	if (count <= 0)
		return 0;

	local_count = UMIN((unsigned int)count, LBUFSIZE-1);

	if (copy_from_user(lbuf, buffer, local_count) != 0) {
		kfree(lbuf);
		return -EFAULT;
	}

	lbuf[local_count] = '\0';

	ptr = strstr(lbuf, "Online devices");
	if (ptr == 0) {
		PRINTK("Unable to parse data (missing \"Online devices\")\n");
		kfree(lbuf);
		return count;
	}

	ptr = strstr(ptr, "\n");
	if (ptr == 0) {
		PRINTK("Unable to parse data (missing newline after \"Online devices\")\n");
		kfree(lbuf);
		return count;
	}
	ptr++;

	if (strstr(ptr, "Waiting work element counts") == NULL) {
		PRINTK("Unable to parse data (missing \"Waiting work element counts\")\n");
		kfree(lbuf);
		return count;
	}

	j = 0;
	eol = 0;
	while ((j < 64) && (*ptr != '\0')) {
		switch (*ptr) {
		case '\t':
		case ' ':
			break;
		case '\n':
		default:
			eol = 1;
			break;
		case '0':	// no device
		case '1':	// PCICA
		case '2':	// PCICC
		case '3':	// PCIXCC_MCL2
		case '4':	// PCIXCC_MCL3
		case '5':	// CEX2C
			j++;
			break;
		case 'd':
		case 'D':
			disable_card(j);
			j++;
			break;
		case 'e':
		case 'E':
			enable_card(j);
			j++;
			break;
		}
		if (eol)
			break;
		ptr++;
	}

	kfree(lbuf);
	return count;
}

/**
 * Functions that run under a timer, with no process id
 *
 * The task functions:
 *     z90crypt_reader_task
 *	 helper_send_work
 *	 helper_handle_work_element
 *	 helper_receive_rc
 *     z90crypt_config_task
 *     z90crypt_cleanup_task
 *
 * Helper functions:
 *     z90crypt_schedule_reader_timer
 *     z90crypt_schedule_reader_task
 *     z90crypt_schedule_config_task
 *     z90crypt_schedule_cleanup_task
 */
static inline int
receive_from_crypto_device(int index, unsigned char *psmid, int *buff_len_p,
			   unsigned char *buff, unsigned char __user **dest_p_p)
{
	int dv, rv;
	struct device *dev_ptr;
	struct caller *caller_p;
	struct ica_rsa_modexpo *icaMsg_p;
	struct list_head *ptr, *tptr;

	memcpy(psmid, NULL_psmid, sizeof(NULL_psmid));

	if (z90crypt.terminating)
		return REC_FATAL_ERROR;

	caller_p = 0;
	dev_ptr = z90crypt.device_p[index];
	rv = 0;
	do {
		if (!dev_ptr || dev_ptr->disabled) {
			rv = REC_NO_WORK; // a disabled device can't return work
			break;
		}
		if (dev_ptr->dev_self_x != index) {
			PRINTKC("Corrupt dev ptr\n");
			z90crypt.terminating = 1;
			rv = REC_FATAL_ERROR;
			break;
		}
		if (!dev_ptr->dev_resp_l || !dev_ptr->dev_resp_p) {
			dv = DEV_REC_EXCEPTION;
			PRINTK("dev_resp_l = %d, dev_resp_p = %p\n",
			       dev_ptr->dev_resp_l, dev_ptr->dev_resp_p);
		} else {
			PDEBUG("Dequeue called for device %d\n", index);
			dv = receive_from_AP(index, z90crypt.cdx,
					     dev_ptr->dev_resp_l,
					     dev_ptr->dev_resp_p, psmid);
		}
		switch (dv) {
		case DEV_REC_EXCEPTION:
			rv = REC_FATAL_ERROR;
			z90crypt.terminating = 1;
			PRINTKC("Exception in receive from device %d\n",
				index);
			break;
		case DEV_ONLINE:
			rv = 0;
			break;
		case DEV_EMPTY:
			rv = REC_EMPTY;
			break;
		case DEV_NO_WORK:
			rv = REC_NO_WORK;
			break;
		case DEV_BAD_MESSAGE:
		case DEV_GONE:
		case REC_HARDWAR_ERR:
		default:
			rv = REC_NO_RESPONSE;
			break;
		}
		if (rv)
			break;
		if (dev_ptr->dev_caller_count <= 0) {
			rv = REC_USER_GONE;
			break;
	        }

		list_for_each_safe(ptr, tptr, &dev_ptr->dev_caller_list) {
			caller_p = list_entry(ptr, struct caller, caller_liste);
			if (!memcmp(caller_p->caller_id, psmid,
				    sizeof(caller_p->caller_id))) {
				if (!list_empty(&caller_p->caller_liste)) {
					list_del_init(ptr);
					dev_ptr->dev_caller_count--;
					break;
				}
			}
			caller_p = 0;
		}
		if (!caller_p) {
			PRINTKW("Unable to locate PSMID %02X%02X%02X%02X%02X"
				"%02X%02X%02X in device list\n",
				psmid[0], psmid[1], psmid[2], psmid[3],
				psmid[4], psmid[5], psmid[6], psmid[7]);
			rv = REC_USER_GONE;
			break;
		}

		PDEBUG("caller_p after successful receive: %p\n", caller_p);
		rv = convert_response(dev_ptr->dev_resp_p,
				      caller_p->caller_buf_p, buff_len_p, buff);
		switch (rv) {
		case REC_USE_PCICA:
			break;
		case REC_OPERAND_INV:
		case REC_OPERAND_SIZE:
		case REC_EVEN_MOD:
		case REC_INVALID_PAD:
			PDEBUG("device %d: 'user error' %d\n", index, rv);
			break;
		case WRONG_DEVICE_TYPE:
		case REC_HARDWAR_ERR:
		case REC_BAD_MESSAGE:
			PRINTKW("device %d: hardware error %d\n", index, rv);
			rv = REC_NO_RESPONSE;
			break;
		default:
			PDEBUG("device %d: rv = %d\n", index, rv);
			break;
		}
	} while (0);

	switch (rv) {
	case 0:
		PDEBUG("Successful receive from device %d\n", index);
		icaMsg_p = (struct ica_rsa_modexpo *)caller_p->caller_buf_p;
		*dest_p_p = icaMsg_p->outputdata;
		if (*buff_len_p == 0)
			PRINTK("Zero *buff_len_p\n");
		break;
	case REC_NO_RESPONSE:
		PRINTKW("Removing device %d from availability\n", index);
		remove_device(dev_ptr);
		break;
	}

	if (caller_p)
		unbuild_caller(dev_ptr, caller_p);

	return rv;
}

static inline void
helper_send_work(int index)
{
	struct work_element *rq_p;
	int rv;

	if (list_empty(&request_list))
		return;
	requestq_count--;
	rq_p = list_entry(request_list.next, struct work_element, liste);
	list_del_init(&rq_p->liste);
	rq_p->audit[1] |= FP_REMREQUEST;
	if (rq_p->devtype == SHRT2DEVPTR(index)->dev_type) {
		rq_p->devindex = SHRT2LONG(index);
		rv = send_to_crypto_device(rq_p);
		if (rv == 0) {
			rq_p->requestsent = jiffies;
			rq_p->audit[0] |= FP_SENT;
			list_add_tail(&rq_p->liste, &pending_list);
			++pendingq_count;
			rq_p->audit[0] |= FP_PENDING;
		} else {
			switch (rv) {
			case REC_OPERAND_INV:
			case REC_OPERAND_SIZE:
			case REC_EVEN_MOD:
			case REC_INVALID_PAD:
				rq_p->retcode = -EINVAL;
				break;
			case SEN_NOT_AVAIL:
			case SEN_RETRY:
			case REC_NO_RESPONSE:
			default:
				if (z90crypt.mask.st_count > 1)
					rq_p->retcode =
						-ERESTARTSYS;
				else
					rq_p->retcode = -ENODEV;
				break;
			}
			rq_p->status[0] |= STAT_FAILED;
			rq_p->audit[1] |= FP_AWAKENING;
			atomic_set(&rq_p->alarmrung, 1);
			wake_up(&rq_p->waitq);
		}
	} else {
		if (z90crypt.mask.st_count > 1)
			rq_p->retcode = -ERESTARTSYS;
		else
			rq_p->retcode = -ENODEV;
		rq_p->status[0] |= STAT_FAILED;
		rq_p->audit[1] |= FP_AWAKENING;
		atomic_set(&rq_p->alarmrung, 1);
		wake_up(&rq_p->waitq);
	}
}

static inline void
helper_handle_work_element(int index, unsigned char psmid[8], int rc,
			   int buff_len, unsigned char *buff,
			   unsigned char __user *resp_addr)
{
	struct work_element *pq_p;
	struct list_head *lptr, *tptr;

	pq_p = 0;
	list_for_each_safe(lptr, tptr, &pending_list) {
		pq_p = list_entry(lptr, struct work_element, liste);
		if (!memcmp(pq_p->caller_id, psmid, sizeof(pq_p->caller_id))) {
			list_del_init(lptr);
			pendingq_count--;
			pq_p->audit[1] |= FP_NOTPENDING;
			break;
		}
		pq_p = 0;
	}

	if (!pq_p) {
		PRINTK("device %d has work but no caller exists on pending Q\n",
		       SHRT2LONG(index));
		return;
	}

	switch (rc) {
		case 0:
			pq_p->resp_buff_size = buff_len;
			pq_p->audit[1] |= FP_RESPSIZESET;
			if (buff_len) {
				pq_p->resp_addr = resp_addr;
				pq_p->audit[1] |= FP_RESPADDRCOPIED;
				memcpy(pq_p->resp_buff, buff, buff_len);
				pq_p->audit[1] |= FP_RESPBUFFCOPIED;
			}
			break;
		case REC_OPERAND_INV:
		case REC_OPERAND_SIZE:
		case REC_EVEN_MOD:
		case REC_INVALID_PAD:
			PDEBUG("-EINVAL after application error %d\n", rc);
			pq_p->retcode = -EINVAL;
			pq_p->status[0] |= STAT_FAILED;
			break;
		case REC_USE_PCICA:
			pq_p->retcode = -ERESTARTSYS;
			pq_p->status[0] |= STAT_FAILED;
			break;
		case REC_NO_RESPONSE:
		default:
			if (z90crypt.mask.st_count > 1)
				pq_p->retcode = -ERESTARTSYS;
			else
				pq_p->retcode = -ENODEV;
			pq_p->status[0] |= STAT_FAILED;
			break;
	}
	if ((pq_p->status[0] != STAT_FAILED) || (pq_p->retcode != -ERELEASED)) {
		pq_p->audit[1] |= FP_AWAKENING;
		atomic_set(&pq_p->alarmrung, 1);
		wake_up(&pq_p->waitq);
	}
}

/**
 * return TRUE if the work element should be removed from the queue
 */
static inline int
helper_receive_rc(int index, int *rc_p)
{
	switch (*rc_p) {
	case 0:
	case REC_OPERAND_INV:
	case REC_OPERAND_SIZE:
	case REC_EVEN_MOD:
	case REC_INVALID_PAD:
	case REC_USE_PCICA:
		break;

	case REC_BUSY:
	case REC_NO_WORK:
	case REC_EMPTY:
	case REC_RETRY_DEV:
	case REC_FATAL_ERROR:
		return 0;

	case REC_NO_RESPONSE:
		break;

	default:
		PRINTK("rc %d, device %d converted to REC_NO_RESPONSE\n",
		       *rc_p, SHRT2LONG(index));
		*rc_p = REC_NO_RESPONSE;
		break;
	}
	return 1;
}

static inline void
z90crypt_schedule_reader_timer(void)
{
	if (timer_pending(&reader_timer))
		return;
	if (mod_timer(&reader_timer, jiffies+(READERTIME*HZ/1000)) != 0)
		PRINTK("Timer pending while modifying reader timer\n");
}

static void
z90crypt_reader_task(unsigned long ptr)
{
	int workavail, index, rc, buff_len;
	unsigned char	psmid[8];
	unsigned char __user *resp_addr;
	static unsigned char buff[1024];

	/**
	 * we use workavail = 2 to ensure 2 passes with nothing dequeued before
	 * exiting the loop. If (pendingq_count+requestq_count) == 0 after the
	 * loop, there is no work remaining on the queues.
	 */
	resp_addr = 0;
	workavail = 2;
	buff_len = 0;
	while (workavail) {
		workavail--;
		rc = 0;
		spin_lock_irq(&queuespinlock);
		memset(buff, 0x00, sizeof(buff));

		/* Dequeue once from each device in round robin. */
		for (index = 0; index < z90crypt.mask.st_count; index++) {
			PDEBUG("About to receive.\n");
			rc = receive_from_crypto_device(SHRT2LONG(index),
							psmid,
							&buff_len,
							buff,
							&resp_addr);
			PDEBUG("Dequeued: rc = %d.\n", rc);

			if (helper_receive_rc(index, &rc)) {
				if (rc != REC_NO_RESPONSE) {
					helper_send_work(index);
					workavail = 2;
				}

				helper_handle_work_element(index, psmid, rc,
							   buff_len, buff,
							   resp_addr);
			}

			if (rc == REC_FATAL_ERROR)
				PRINTKW("REC_FATAL_ERROR from device %d!\n",
					SHRT2LONG(index));
		}
		spin_unlock_irq(&queuespinlock);
	}

	if (pendingq_count + requestq_count)
		z90crypt_schedule_reader_timer();
}

static inline void
z90crypt_schedule_config_task(unsigned int expiration)
{
	if (timer_pending(&config_timer))
		return;
	if (mod_timer(&config_timer, jiffies+(expiration*HZ)) != 0)
		PRINTK("Timer pending while modifying config timer\n");
}

static void
z90crypt_config_task(unsigned long ptr)
{
	int rc;

	PDEBUG("jiffies %ld\n", jiffies);

	if ((rc = refresh_z90crypt(&z90crypt.cdx)))
		PRINTK("Error %d detected in refresh_z90crypt.\n", rc);
	/* If return was fatal, don't bother reconfiguring */
	if ((rc != TSQ_FATAL_ERROR) && (rc != RSQ_FATAL_ERROR))
		z90crypt_schedule_config_task(CONFIGTIME);
}

static inline void
z90crypt_schedule_cleanup_task(void)
{
	if (timer_pending(&cleanup_timer))
		return;
	if (mod_timer(&cleanup_timer, jiffies+(CLEANUPTIME*HZ)) != 0)
		PRINTK("Timer pending while modifying cleanup timer\n");
}

static inline void
helper_drain_queues(void)
{
	struct work_element *pq_p;
	struct list_head *lptr, *tptr;

	list_for_each_safe(lptr, tptr, &pending_list) {
		pq_p = list_entry(lptr, struct work_element, liste);
		pq_p->retcode = -ENODEV;
		pq_p->status[0] |= STAT_FAILED;
		unbuild_caller(LONG2DEVPTR(pq_p->devindex),
			       (struct caller *)pq_p->requestptr);
		list_del_init(lptr);
		pendingq_count--;
		pq_p->audit[1] |= FP_NOTPENDING;
		pq_p->audit[1] |= FP_AWAKENING;
		atomic_set(&pq_p->alarmrung, 1);
		wake_up(&pq_p->waitq);
	}

	list_for_each_safe(lptr, tptr, &request_list) {
		pq_p = list_entry(lptr, struct work_element, liste);
		pq_p->retcode = -ENODEV;
		pq_p->status[0] |= STAT_FAILED;
		list_del_init(lptr);
		requestq_count--;
		pq_p->audit[1] |= FP_REMREQUEST;
		pq_p->audit[1] |= FP_AWAKENING;
		atomic_set(&pq_p->alarmrung, 1);
		wake_up(&pq_p->waitq);
	}
}

static inline void
helper_timeout_requests(void)
{
	struct work_element *pq_p;
	struct list_head *lptr, *tptr;
	long timelimit;

	timelimit = jiffies - (CLEANUPTIME * HZ);
	/* The list is in strict chronological order */
	list_for_each_safe(lptr, tptr, &pending_list) {
		pq_p = list_entry(lptr, struct work_element, liste);
		if (pq_p->requestsent >= timelimit)
			break;
		PRINTKW("Purging(PQ) PSMID %02X%02X%02X%02X%02X%02X%02X%02X\n",
		       ((struct caller *)pq_p->requestptr)->caller_id[0],
		       ((struct caller *)pq_p->requestptr)->caller_id[1],
		       ((struct caller *)pq_p->requestptr)->caller_id[2],
		       ((struct caller *)pq_p->requestptr)->caller_id[3],
		       ((struct caller *)pq_p->requestptr)->caller_id[4],
		       ((struct caller *)pq_p->requestptr)->caller_id[5],
		       ((struct caller *)pq_p->requestptr)->caller_id[6],
		       ((struct caller *)pq_p->requestptr)->caller_id[7]);
		pq_p->retcode = -ETIMEOUT;
		pq_p->status[0] |= STAT_FAILED;
		/* get this off any caller queue it may be on */
		unbuild_caller(LONG2DEVPTR(pq_p->devindex),
			       (struct caller *) pq_p->requestptr);
		list_del_init(lptr);
		pendingq_count--;
		pq_p->audit[1] |= FP_TIMEDOUT;
		pq_p->audit[1] |= FP_NOTPENDING;
		pq_p->audit[1] |= FP_AWAKENING;
		atomic_set(&pq_p->alarmrung, 1);
		wake_up(&pq_p->waitq);
	}

	/**
	 * If pending count is zero, items left on the request queue may
	 * never be processed.
	 */
	if (pendingq_count <= 0) {
		list_for_each_safe(lptr, tptr, &request_list) {
			pq_p = list_entry(lptr, struct work_element, liste);
			if (pq_p->requestsent >= timelimit)
				break;
		PRINTKW("Purging(RQ) PSMID %02X%02X%02X%02X%02X%02X%02X%02X\n",
		       ((struct caller *)pq_p->requestptr)->caller_id[0],
		       ((struct caller *)pq_p->requestptr)->caller_id[1],
		       ((struct caller *)pq_p->requestptr)->caller_id[2],
		       ((struct caller *)pq_p->requestptr)->caller_id[3],
		       ((struct caller *)pq_p->requestptr)->caller_id[4],
		       ((struct caller *)pq_p->requestptr)->caller_id[5],
		       ((struct caller *)pq_p->requestptr)->caller_id[6],
		       ((struct caller *)pq_p->requestptr)->caller_id[7]);
			pq_p->retcode = -ETIMEOUT;
			pq_p->status[0] |= STAT_FAILED;
			list_del_init(lptr);
			requestq_count--;
			pq_p->audit[1] |= FP_TIMEDOUT;
			pq_p->audit[1] |= FP_REMREQUEST;
			pq_p->audit[1] |= FP_AWAKENING;
			atomic_set(&pq_p->alarmrung, 1);
			wake_up(&pq_p->waitq);
		}
	}
}

static void
z90crypt_cleanup_task(unsigned long ptr)
{
	PDEBUG("jiffies %ld\n", jiffies);
	spin_lock_irq(&queuespinlock);
	if (z90crypt.mask.st_count <= 0) // no devices!
		helper_drain_queues();
	else
		helper_timeout_requests();
	spin_unlock_irq(&queuespinlock);
	z90crypt_schedule_cleanup_task();
}

static void
z90crypt_schedule_reader_task(unsigned long ptr)
{
	tasklet_schedule(&reader_tasklet);
}

/**
 * Lowlevel Functions:
 *
 *   create_z90crypt:  creates and initializes basic data structures
 *   refresh_z90crypt:	re-initializes basic data structures
 *   find_crypto_devices: returns a count and mask of hardware status
 *   create_crypto_device:  builds the descriptor for a device
 *   destroy_crypto_device:  unallocates the descriptor for a device
 *   destroy_z90crypt:	drains all work, unallocates structs
 */

/**
 * build the z90crypt root structure using the given domain index
 */
static int
create_z90crypt(int *cdx_p)
{
	struct hdware_block *hdware_blk_p;

	memset(&z90crypt, 0x00, sizeof(struct z90crypt));
	z90crypt.domain_established = 0;
	z90crypt.len = sizeof(struct z90crypt);
	z90crypt.max_count = Z90CRYPT_NUM_DEVS;
	z90crypt.cdx = *cdx_p;

	hdware_blk_p = (struct hdware_block *)
		kmalloc(sizeof(struct hdware_block), GFP_ATOMIC);
	if (!hdware_blk_p) {
		PDEBUG("kmalloc for hardware block failed\n");
		return ENOMEM;
	}
	memset(hdware_blk_p, 0x00, sizeof(struct hdware_block));
	z90crypt.hdware_info = hdware_blk_p;

	return 0;
}

static inline int
helper_scan_devices(int cdx_array[16], int *cdx_p, int *correct_cdx_found)
{
	enum hdstat hd_stat;
	int q_depth, dev_type;
	int indx, chkdom, numdomains;

	q_depth = dev_type = numdomains = 0;
	for (chkdom = 0; chkdom <= 15; cdx_array[chkdom++] = -1);
	for (indx = 0; indx < z90crypt.max_count; indx++) {
		hd_stat = HD_NOT_THERE;
		numdomains = 0;
		for (chkdom = 0; chkdom <= 15; chkdom++) {
			hd_stat = query_online(indx, chkdom, MAX_RESET,
					       &q_depth, &dev_type);
			if (hd_stat == HD_TSQ_EXCEPTION) {
				z90crypt.terminating = 1;
				PRINTKC("exception taken!\n");
				break;
			}
			if (hd_stat == HD_ONLINE) {
				cdx_array[numdomains++] = chkdom;
				if (*cdx_p == chkdom) {
					*correct_cdx_found  = 1;
					break;
				}
			}
		}
		if ((*correct_cdx_found == 1) || (numdomains != 0))
			break;
		if (z90crypt.terminating)
			break;
	}
	return numdomains;
}

static inline int
probe_crypto_domain(int *cdx_p)
{
	int cdx_array[16];
	char cdx_array_text[53], temp[5];
	int correct_cdx_found, numdomains;

	correct_cdx_found = 0;
	numdomains = helper_scan_devices(cdx_array, cdx_p, &correct_cdx_found);

	if (z90crypt.terminating)
		return TSQ_FATAL_ERROR;

	if (correct_cdx_found)
		return 0;

	if (numdomains == 0) {
		PRINTKW("Unable to find crypto domain: No devices found\n");
		return Z90C_NO_DEVICES;
	}

	if (numdomains == 1) {
		if (*cdx_p == -1) {
			*cdx_p = cdx_array[0];
			return 0;
		}
		PRINTKW("incorrect domain: specified = %d, found = %d\n",
		       *cdx_p, cdx_array[0]);
		return Z90C_INCORRECT_DOMAIN;
	}

	numdomains--;
	sprintf(cdx_array_text, "%d", cdx_array[numdomains]);
	while (numdomains) {
		numdomains--;
		sprintf(temp, ", %d", cdx_array[numdomains]);
		strcat(cdx_array_text, temp);
	}

	PRINTKW("ambiguous domain detected: specified = %d, found array = %s\n",
		*cdx_p, cdx_array_text);
	return Z90C_AMBIGUOUS_DOMAIN;
}

static int
refresh_z90crypt(int *cdx_p)
{
	int i, j, indx, rv;
	static struct status local_mask;
	struct device *devPtr;
	unsigned char oldStat, newStat;
	int return_unchanged;

	if (z90crypt.len != sizeof(z90crypt))
		return ENOTINIT;
	if (z90crypt.terminating)
		return TSQ_FATAL_ERROR;
	rv = 0;
	if (!z90crypt.hdware_info->hdware_mask.st_count &&
	    !z90crypt.domain_established) {
		rv = probe_crypto_domain(cdx_p);
		if (z90crypt.terminating)
			return TSQ_FATAL_ERROR;
		if (rv == Z90C_NO_DEVICES)
			return 0; // try later
		if (rv)
			return rv;
		z90crypt.cdx = *cdx_p;
		z90crypt.domain_established = 1;
	}
	rv = find_crypto_devices(&local_mask);
	if (rv) {
		PRINTK("find crypto devices returned %d\n", rv);
		return rv;
	}
	if (!memcmp(&local_mask, &z90crypt.hdware_info->hdware_mask,
		    sizeof(struct status))) {
		return_unchanged = 1;
		for (i = 0; i < Z90CRYPT_NUM_TYPES; i++) {
			/**
			 * Check for disabled cards.  If any device is marked
			 * disabled, destroy it.
			 */
			for (j = 0;
			     j < z90crypt.hdware_info->type_mask[i].st_count;
			     j++) {
				indx = z90crypt.hdware_info->type_x_addr[i].
								device_index[j];
				devPtr = z90crypt.device_p[indx];
				if (devPtr && devPtr->disabled) {
					local_mask.st_mask[indx] = HD_NOT_THERE;
					return_unchanged = 0;
				}
			}
		}
		if (return_unchanged == 1)
			return 0;
	}

	spin_lock_irq(&queuespinlock);
	for (i = 0; i < z90crypt.max_count; i++) {
		oldStat = z90crypt.hdware_info->hdware_mask.st_mask[i];
		newStat = local_mask.st_mask[i];
		if ((oldStat == HD_ONLINE) && (newStat != HD_ONLINE))
			destroy_crypto_device(i);
		else if ((oldStat != HD_ONLINE) && (newStat == HD_ONLINE)) {
			rv = create_crypto_device(i);
			if (rv >= REC_FATAL_ERROR)
				return rv;
			if (rv != 0) {
				local_mask.st_mask[i] = HD_NOT_THERE;
				local_mask.st_count--;
			}
		}
	}
	memcpy(z90crypt.hdware_info->hdware_mask.st_mask, local_mask.st_mask,
	       sizeof(local_mask.st_mask));
	z90crypt.hdware_info->hdware_mask.st_count = local_mask.st_count;
	z90crypt.hdware_info->hdware_mask.disabled_count =
						      local_mask.disabled_count;
	refresh_index_array(&z90crypt.mask, &z90crypt.overall_device_x);
	for (i = 0; i < Z90CRYPT_NUM_TYPES; i++)
		refresh_index_array(&(z90crypt.hdware_info->type_mask[i]),
				    &(z90crypt.hdware_info->type_x_addr[i]));
	spin_unlock_irq(&queuespinlock);

	return rv;
}

static int
find_crypto_devices(struct status *deviceMask)
{
	int i, q_depth, dev_type;
	enum hdstat hd_stat;

	deviceMask->st_count = 0;
	deviceMask->disabled_count = 0;
	deviceMask->user_disabled_count = 0;

	for (i = 0; i < z90crypt.max_count; i++) {
		hd_stat = query_online(i, z90crypt.cdx, MAX_RESET, &q_depth,
				       &dev_type);
		if (hd_stat == HD_TSQ_EXCEPTION) {
			z90crypt.terminating = 1;
			PRINTKC("Exception during probe for crypto devices\n");
			return TSQ_FATAL_ERROR;
		}
		deviceMask->st_mask[i] = hd_stat;
		if (hd_stat == HD_ONLINE) {
			PDEBUG("Got an online crypto!: %d\n", i);
			PDEBUG("Got a queue depth of %d\n", q_depth);
			PDEBUG("Got a device type of %d\n", dev_type);
			if (q_depth <= 0)
				return TSQ_FATAL_ERROR;
			deviceMask->st_count++;
			z90crypt.q_depth_array[i] = q_depth;
			z90crypt.dev_type_array[i] = dev_type;
		}
	}

	return 0;
}

static int
refresh_index_array(struct status *status_str, struct device_x *index_array)
{
	int i, count;
	enum devstat stat;

	i = -1;
	count = 0;
	do {
		stat = status_str->st_mask[++i];
		if (stat == DEV_ONLINE)
			index_array->device_index[count++] = i;
	} while ((i < Z90CRYPT_NUM_DEVS) && (count < status_str->st_count));

	return count;
}

static int
create_crypto_device(int index)
{
	int rv, devstat, total_size;
	struct device *dev_ptr;
	struct status *type_str_p;
	int deviceType;

	dev_ptr = z90crypt.device_p[index];
	if (!dev_ptr) {
		total_size = sizeof(struct device) +
			     z90crypt.q_depth_array[index] * sizeof(int);

		dev_ptr = (struct device *) kmalloc(total_size, GFP_ATOMIC);
		if (!dev_ptr) {
			PRINTK("kmalloc device %d failed\n", index);
			return ENOMEM;
		}
		memset(dev_ptr, 0, total_size);
		dev_ptr->dev_resp_p = kmalloc(MAX_RESPONSE_SIZE, GFP_ATOMIC);
		if (!dev_ptr->dev_resp_p) {
			kfree(dev_ptr);
			PRINTK("kmalloc device %d rec buffer failed\n", index);
			return ENOMEM;
		}
		dev_ptr->dev_resp_l = MAX_RESPONSE_SIZE;
		INIT_LIST_HEAD(&(dev_ptr->dev_caller_list));
	}

	devstat = reset_device(index, z90crypt.cdx, MAX_RESET);
	if (devstat == DEV_RSQ_EXCEPTION) {
		PRINTK("exception during reset device %d\n", index);
		kfree(dev_ptr->dev_resp_p);
		kfree(dev_ptr);
		return RSQ_FATAL_ERROR;
	}
	if (devstat == DEV_ONLINE) {
		dev_ptr->dev_self_x = index;
		dev_ptr->dev_type = z90crypt.dev_type_array[index];
		if (dev_ptr->dev_type == NILDEV) {
			rv = probe_device_type(dev_ptr);
			if (rv) {
				PRINTK("rv = %d from probe_device_type %d\n",
				       rv, index);
				kfree(dev_ptr->dev_resp_p);
				kfree(dev_ptr);
				return rv;
			}
		}
		if (dev_ptr->dev_type == PCIXCC_UNK) {
			rv = probe_PCIXCC_type(dev_ptr);
			if (rv) {
				PRINTK("rv = %d from probe_PCIXCC_type %d\n",
				       rv, index);
				kfree(dev_ptr->dev_resp_p);
				kfree(dev_ptr);
				return rv;
			}
		}
		deviceType = dev_ptr->dev_type;
		z90crypt.dev_type_array[index] = deviceType;
		if (deviceType == PCICA)
			z90crypt.hdware_info->device_type_array[index] = 1;
		else if (deviceType == PCICC)
			z90crypt.hdware_info->device_type_array[index] = 2;
		else if (deviceType == PCIXCC_MCL2)
			z90crypt.hdware_info->device_type_array[index] = 3;
		else if (deviceType == PCIXCC_MCL3)
			z90crypt.hdware_info->device_type_array[index] = 4;
		else if (deviceType == CEX2C)
			z90crypt.hdware_info->device_type_array[index] = 5;
		else
			z90crypt.hdware_info->device_type_array[index] = -1;
	}

	/**
	 * 'q_depth' returned by the hardware is one less than
	 * the actual depth
	 */
	dev_ptr->dev_q_depth = z90crypt.q_depth_array[index];
	dev_ptr->dev_type = z90crypt.dev_type_array[index];
	dev_ptr->dev_stat = devstat;
	dev_ptr->disabled = 0;
	z90crypt.device_p[index] = dev_ptr;

	if (devstat == DEV_ONLINE) {
		if (z90crypt.mask.st_mask[index] != DEV_ONLINE) {
			z90crypt.mask.st_mask[index] = DEV_ONLINE;
			z90crypt.mask.st_count++;
		}
		deviceType = dev_ptr->dev_type;
		type_str_p = &z90crypt.hdware_info->type_mask[deviceType];
		if (type_str_p->st_mask[index] != DEV_ONLINE) {
			type_str_p->st_mask[index] = DEV_ONLINE;
			type_str_p->st_count++;
		}
	}

	return 0;
}

static int
destroy_crypto_device(int index)
{
	struct device *dev_ptr;
	int t, disabledFlag;

	dev_ptr = z90crypt.device_p[index];

	/* remember device type; get rid of device struct */
	if (dev_ptr) {
		disabledFlag = dev_ptr->disabled;
		t = dev_ptr->dev_type;
		if (dev_ptr->dev_resp_p)
			kfree(dev_ptr->dev_resp_p);
		kfree(dev_ptr);
	} else {
		disabledFlag = 0;
		t = -1;
	}
	z90crypt.device_p[index] = 0;

	/* if the type is valid, remove the device from the type_mask */
	if ((t != -1) && z90crypt.hdware_info->type_mask[t].st_mask[index]) {
		  z90crypt.hdware_info->type_mask[t].st_mask[index] = 0x00;
		  z90crypt.hdware_info->type_mask[t].st_count--;
		  if (disabledFlag == 1)
			z90crypt.hdware_info->type_mask[t].disabled_count--;
	}
	if (z90crypt.mask.st_mask[index] != DEV_GONE) {
		z90crypt.mask.st_mask[index] = DEV_GONE;
		z90crypt.mask.st_count--;
	}
	z90crypt.hdware_info->device_type_array[index] = 0;

	return 0;
}

static void
destroy_z90crypt(void)
{
	int i;
	for (i = 0; i < z90crypt.max_count; i++)
		if (z90crypt.device_p[i])
			destroy_crypto_device(i);
	if (z90crypt.hdware_info)
		kfree((void *)z90crypt.hdware_info);
	memset((void *)&z90crypt, 0, sizeof(z90crypt));
}

static unsigned char static_testmsg[384] = {
0x00,0x00,0x00,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x00,0x06,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x58,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x43,0x43,
0x41,0x2d,0x41,0x50,0x50,0x4c,0x20,0x20,0x20,0x01,0x01,0x01,0x00,0x00,0x00,0x00,
0x50,0x4b,0x00,0x00,0x00,0x00,0x01,0x1c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x05,0xb8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x70,0x00,0x41,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x54,0x32,
0x01,0x00,0xa0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0xb8,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x0a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x49,0x43,0x53,0x46,
0x20,0x20,0x20,0x20,0x50,0x4b,0x0a,0x00,0x50,0x4b,0x43,0x53,0x2d,0x31,0x2e,0x32,
0x37,0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0x00,0x11,0x22,0x33,0x44,
0x55,0x66,0x77,0x88,0x99,0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0x00,
0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0x00,0x11,0x22,0x33,0x44,0x55,0x66,
0x77,0x88,0x99,0x00,0x11,0x22,0x33,0x5d,0x00,0x5b,0x00,0x77,0x88,0x1e,0x00,0x00,
0x57,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x4f,0x00,0x00,0x00,0x03,0x02,0x00,0x00,
0x40,0x01,0x00,0x01,0xce,0x02,0x68,0x2d,0x5f,0xa9,0xde,0x0c,0xf6,0xd2,0x7b,0x58,
0x4b,0xf9,0x28,0x68,0x3d,0xb4,0xf4,0xef,0x78,0xd5,0xbe,0x66,0x63,0x42,0xef,0xf8,
0xfd,0xa4,0xf8,0xb0,0x8e,0x29,0xc2,0xc9,0x2e,0xd8,0x45,0xb8,0x53,0x8c,0x6f,0x4e,
0x72,0x8f,0x6c,0x04,0x9c,0x88,0xfc,0x1e,0xc5,0x83,0x55,0x57,0xf7,0xdd,0xfd,0x4f,
0x11,0x36,0x95,0x5d,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

static int
probe_device_type(struct device *devPtr)
{
	int rv, dv, i, index, length;
	unsigned char psmid[8];
	static unsigned char loc_testmsg[sizeof(static_testmsg)];

	index = devPtr->dev_self_x;
	rv = 0;
	do {
		memcpy(loc_testmsg, static_testmsg, sizeof(static_testmsg));
		length = sizeof(static_testmsg) - 24;
		/* the -24 allows for the header */
		dv = send_to_AP(index, z90crypt.cdx, length, loc_testmsg);
		if (dv) {
			PDEBUG("dv returned by send during probe: %d\n", dv);
			if (dv == DEV_SEN_EXCEPTION) {
				rv = SEN_FATAL_ERROR;
				PRINTKC("exception in send to AP %d\n", index);
				break;
			}
			PDEBUG("return value from send_to_AP: %d\n", rv);
			switch (dv) {
			case DEV_GONE:
				PDEBUG("dev %d not available\n", index);
				rv = SEN_NOT_AVAIL;
				break;
			case DEV_ONLINE:
				rv = 0;
				break;
			case DEV_EMPTY:
				rv = SEN_NOT_AVAIL;
				break;
			case DEV_NO_WORK:
				rv = SEN_FATAL_ERROR;
				break;
			case DEV_BAD_MESSAGE:
				rv = SEN_USER_ERROR;
				break;
			case DEV_QUEUE_FULL:
				rv = SEN_QUEUE_FULL;
				break;
			default:
				PRINTK("unknown dv=%d for dev %d\n", dv, index);
				rv = SEN_NOT_AVAIL;
				break;
			}
		}

		if (rv)
			break;

		for (i = 0; i < 6; i++) {
			mdelay(300);
			dv = receive_from_AP(index, z90crypt.cdx,
					     devPtr->dev_resp_l,
					     devPtr->dev_resp_p, psmid);
			PDEBUG("dv returned by DQ = %d\n", dv);
			if (dv == DEV_REC_EXCEPTION) {
				rv = REC_FATAL_ERROR;
				PRINTKC("exception in dequeue %d\n",
					index);
				break;
			}
			switch (dv) {
			case DEV_ONLINE:
				rv = 0;
				break;
			case DEV_EMPTY:
				rv = REC_EMPTY;
				break;
			case DEV_NO_WORK:
				rv = REC_NO_WORK;
				break;
			case DEV_BAD_MESSAGE:
			case DEV_GONE:
			default:
				rv = REC_NO_RESPONSE;
				break;
			}
			if ((rv != 0) && (rv != REC_NO_WORK))
				break;
			if (rv == 0)
				break;
		}
		if (rv)
			break;
		rv = (devPtr->dev_resp_p[0] == 0x00) &&
		     (devPtr->dev_resp_p[1] == 0x86);
		if (rv)
			devPtr->dev_type = PCICC;
		else
			devPtr->dev_type = PCICA;
		rv = 0;
	} while (0);
	/* In a general error case, the card is not marked online */
	return rv;
}

static unsigned char MCL3_testmsg[] = {
0x00,0x00,0x00,0x00,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,
0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x58,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x43,0x41,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x50,0x4B,0x00,0x00,0x00,0x00,0x01,0xC4,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0x24,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xDC,0x02,0x00,0x00,0x00,0x54,0x32,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xE8,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0x24,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x50,0x4B,0x00,0x0A,0x4D,0x52,0x50,0x20,0x20,0x20,0x20,0x20,
0x00,0x42,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,
0x0E,0x0F,0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,
0xEE,0xFF,0xFF,0xEE,0xDD,0xCC,0xBB,0xAA,0x99,0x88,0x77,0x66,0x55,0x44,0x33,0x22,
0x11,0x00,0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,0xFE,0xDC,0xBA,0x98,0x76,0x54,
0x32,0x10,0x00,0x9A,0x00,0x98,0x00,0x00,0x1E,0x00,0x00,0x94,0x00,0x00,0x00,0x00,
0x04,0x00,0x00,0x8C,0x00,0x00,0x00,0x40,0x02,0x00,0x00,0x40,0xBA,0xE8,0x23,0x3C,
0x75,0xF3,0x91,0x61,0xD6,0x73,0x39,0xCF,0x7B,0x6D,0x8E,0x61,0x97,0x63,0x9E,0xD9,
0x60,0x55,0xD6,0xC7,0xEF,0xF8,0x1E,0x63,0x95,0x17,0xCC,0x28,0x45,0x60,0x11,0xC5,
0xC4,0x4E,0x66,0xC6,0xE6,0xC3,0xDE,0x8A,0x19,0x30,0xCF,0x0E,0xD7,0xAA,0xDB,0x01,
0xD8,0x00,0xBB,0x8F,0x39,0x9F,0x64,0x28,0xF5,0x7A,0x77,0x49,0xCC,0x6B,0xA3,0x91,
0x97,0x70,0xE7,0x60,0x1E,0x39,0xE1,0xE5,0x33,0xE1,0x15,0x63,0x69,0x08,0x80,0x4C,
0x67,0xC4,0x41,0x8F,0x48,0xDF,0x26,0x98,0xF1,0xD5,0x8D,0x88,0xD9,0x6A,0xA4,0x96,
0xC5,0x84,0xD9,0x30,0x49,0x67,0x7D,0x19,0xB1,0xB3,0x45,0x4D,0xB2,0x53,0x9A,0x47,
0x3C,0x7C,0x55,0xBF,0xCC,0x85,0x00,0x36,0xF1,0x3D,0x93,0x53
};

static int
probe_PCIXCC_type(struct device *devPtr)
{
	int rv, dv, i, index, length;
	unsigned char psmid[8];
	static unsigned char loc_testmsg[548];
	struct CPRBX *cprbx_p;

	index = devPtr->dev_self_x;
	rv = 0;
	do {
		memcpy(loc_testmsg, MCL3_testmsg, sizeof(MCL3_testmsg));
		length = sizeof(MCL3_testmsg) - 0x0C;
		dv = send_to_AP(index, z90crypt.cdx, length, loc_testmsg);
		if (dv) {
			PDEBUG("dv returned = %d\n", dv);
			if (dv == DEV_SEN_EXCEPTION) {
				rv = SEN_FATAL_ERROR;
				PRINTKC("exception in send to AP %d\n", index);
				break;
			}
			PDEBUG("return value from send_to_AP: %d\n", rv);
			switch (dv) {
			case DEV_GONE:
				PDEBUG("dev %d not available\n", index);
				rv = SEN_NOT_AVAIL;
				break;
			case DEV_ONLINE:
				rv = 0;
				break;
			case DEV_EMPTY:
				rv = SEN_NOT_AVAIL;
				break;
			case DEV_NO_WORK:
				rv = SEN_FATAL_ERROR;
				break;
			case DEV_BAD_MESSAGE:
				rv = SEN_USER_ERROR;
				break;
			case DEV_QUEUE_FULL:
				rv = SEN_QUEUE_FULL;
				break;
			default:
				PRINTK("unknown dv=%d for dev %d\n", dv, index);
				rv = SEN_NOT_AVAIL;
				break;
			}
		}

		if (rv)
			break;

		for (i = 0; i < 6; i++) {
			mdelay(300);
			dv = receive_from_AP(index, z90crypt.cdx,
					     devPtr->dev_resp_l,
					     devPtr->dev_resp_p, psmid);
			PDEBUG("dv returned by DQ = %d\n", dv);
			if (dv == DEV_REC_EXCEPTION) {
				rv = REC_FATAL_ERROR;
				PRINTKC("exception in dequeue %d\n",
					index);
				break;
			}
			switch (dv) {
			case DEV_ONLINE:
				rv = 0;
				break;
			case DEV_EMPTY:
				rv = REC_EMPTY;
				break;
			case DEV_NO_WORK:
				rv = REC_NO_WORK;
				break;
			case DEV_BAD_MESSAGE:
			case DEV_GONE:
			default:
				rv = REC_NO_RESPONSE;
				break;
			}
			if ((rv != 0) && (rv != REC_NO_WORK))
				break;
			if (rv == 0)
				break;
		}
		if (rv)
			break;
		cprbx_p = (struct CPRBX *) (devPtr->dev_resp_p + 48);
		if ((cprbx_p->ccp_rtcode == 8) && (cprbx_p->ccp_rscode == 33)) {
			devPtr->dev_type = PCIXCC_MCL2;
			PDEBUG("device %d is MCL2\n", index);
		} else {
			devPtr->dev_type = PCIXCC_MCL3;
			PDEBUG("device %d is MCL3\n", index);
		}
	} while (0);
	/* In a general error case, the card is not marked online */
	return rv;
}

module_init(z90crypt_init_module);
module_exit(z90crypt_cleanup_module);
