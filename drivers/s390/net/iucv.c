/* 
 * IUCV network driver
 *
 * Copyright (C) 2001 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Author(s):
 *    Original source:
 *      Alan Altmark (Alan_Altmark@us.ibm.com)  Sept. 2000
 *      Xenia Tkatschow (xenia@us.ibm.com)
 *    2Gb awareness and general cleanup:
 *      Fritz Elfert (elfert@de.ibm.com, felfert@millenux.com)
 *
 * Documentation used:
 *    The original source
 *    CP Programming Service, IBM document # SC24-5760
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/* #define DEBUG */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/config.h>

#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/device.h>
#include <asm/atomic.h>
#include "iucv.h"
#include <asm/io.h>
#include <asm/s390_ext.h>
#include <asm/ebcdic.h>
#include <asm/smp.h>
#include <asm/s390_rdev.h>

/* FLAGS:
 * All flags are defined in the field IPFLAGS1 of each function
 * and can be found in CP Programming Services.
 * IPSRCCLS - Indicates you have specified a source class
 * IPFGMCL  - Indicates you have specified a target class
 * IPFGPID  - Indicates you have specified a pathid
 * IPFGMID  - Indicates you have specified a message ID
 * IPANSLST - Indicates that you are using an address list for
 *            reply data
 * IPBUFLST - Indicates that you are using an address list for
 *            message data
 */

#define IPSRCCLS 	0x01
#define IPFGMCL         0x01
#define IPFGPID         0x02
#define IPFGMID         0x04
#define IPANSLST        0x08
#define IPBUFLST        0x40

static int
iucv_bus_match (struct device *dev, struct device_driver *drv)
{
	return 0;
}

struct bus_type iucv_bus = {
	.name = "iucv",
	.match = iucv_bus_match,
};	

struct device *iucv_root;

/* General IUCV interrupt structure */
typedef struct {
	__u16 ippathid;
	__u8  res1;
	__u8  iptype;
	__u32 res2;
	__u8  ipvmid[8];
	__u8  res3[24];
} iucv_GeneralInterrupt;

static iucv_GeneralInterrupt *iucv_external_int_buffer = NULL;

/* Spin Lock declaration */

static DEFINE_SPINLOCK(iucv_lock);

static int messagesDisabled = 0;

/***************INTERRUPT HANDLING ***************/

typedef struct {
	struct list_head queue;
	iucv_GeneralInterrupt data;
} iucv_irqdata;

static struct list_head  iucv_irq_queue;
static DEFINE_SPINLOCK(iucv_irq_queue_lock);

/*
 *Internal function prototypes
 */
static void iucv_tasklet_handler(unsigned long);
static void iucv_irq_handler(struct pt_regs *, __u16);

static DECLARE_TASKLET(iucv_tasklet,iucv_tasklet_handler,0);

/************ FUNCTION ID'S ****************************/

#define ACCEPT          10
#define CONNECT         11
#define DECLARE_BUFFER  12
#define PURGE           9
#define QUERY           0
#define QUIESCE         13
#define RECEIVE         5
#define REJECT          8
#define REPLY           6
#define RESUME          14
#define RETRIEVE_BUFFER 2
#define SEND            4
#define SETMASK         16
#define SEVER           15

/**
 * Structure: handler
 * members: list - list management.
 *          structure: id
 *             userid - 8 char array of machine identification
 *             user_data - 16 char array for user identification
 *             mask - 24 char array used to compare the 2 previous
 *          interrupt_table - vector of interrupt functions.
 *          pgm_data -  ulong, application data that is passed
 *                      to the interrupt handlers
*/
typedef struct handler_t {
	struct list_head list;
	struct {
		__u8 userid[8];
		__u8 user_data[16];
		__u8 mask[24];
	}                    id;
	iucv_interrupt_ops_t *interrupt_table;
	void                 *pgm_data;
} handler;

/**
 * iucv_handler_table: List of registered handlers.
 */
static struct list_head iucv_handler_table;

/**
 * iucv_pathid_table: an array of *handler pointing into
 *                    iucv_handler_table for fast indexing by pathid;
 */
static handler **iucv_pathid_table;

static unsigned long max_connections;

/**
 * iucv_cpuid: contains the logical cpu number of the cpu which
 * has declared the iucv buffer by issuing DECLARE_BUFFER.
 * If no cpu has done the initialization iucv_cpuid contains -1.
 */
static int iucv_cpuid = -1;
/**
 * register_flag: is 0 when external interrupt has not been registered
 */
static int register_flag;

/****************FIVE 40-BYTE PARAMETER STRUCTURES******************/
/* Data struct 1: iparml_control
 * Used for iucv_accept
 *          iucv_connect
 *          iucv_quiesce
 *          iucv_resume
 *          iucv_sever
 *          iucv_retrieve_buffer
 * Data struct 2: iparml_dpl     (data in parameter list)
 * Used for iucv_send_prmmsg
 *          iucv_send2way_prmmsg
 *          iucv_send2way_prmmsg_array
 *          iucv_reply_prmmsg
 * Data struct 3: iparml_db       (data in a buffer)
 * Used for iucv_receive
 *          iucv_receive_array
 *          iucv_reject
 *          iucv_reply
 *          iucv_reply_array
 *          iucv_send
 *          iucv_send_array
 *          iucv_send2way
 *          iucv_send2way_array
 *          iucv_declare_buffer
 * Data struct 4: iparml_purge
 * Used for iucv_purge
 *          iucv_query
 * Data struct 5: iparml_set_mask
 * Used for iucv_set_mask
 */

typedef struct {
	__u16 ippathid;
	__u8  ipflags1;
	__u8  iprcode;
	__u16 ipmsglim;
	__u16 res1;
	__u8  ipvmid[8];
	__u8  ipuser[16];
	__u8  iptarget[8];
} iparml_control;

typedef struct {
	__u16 ippathid;
	__u8  ipflags1;
	__u8  iprcode;
	__u32 ipmsgid;
	__u32 iptrgcls;
	__u8  iprmmsg[8];
	__u32 ipsrccls;
	__u32 ipmsgtag;
	__u32 ipbfadr2;
	__u32 ipbfln2f;
	__u32 res;
} iparml_dpl;

typedef struct {
	__u16 ippathid;
	__u8  ipflags1;
	__u8  iprcode;
	__u32 ipmsgid;
	__u32 iptrgcls;
	__u32 ipbfadr1;
	__u32 ipbfln1f;
	__u32 ipsrccls;
	__u32 ipmsgtag;
	__u32 ipbfadr2;
	__u32 ipbfln2f;
	__u32 res;
} iparml_db;

typedef struct {
	__u16 ippathid;
	__u8  ipflags1;
	__u8  iprcode;
	__u32 ipmsgid;
	__u8  ipaudit[3];
	__u8  res1[5];
	__u32 res2;
	__u32 ipsrccls;
	__u32 ipmsgtag;
	__u32 res3[3];
} iparml_purge;

typedef struct {
	__u8  ipmask;
	__u8  res1[2];
	__u8  iprcode;
	__u32 res2[9];
} iparml_set_mask;

typedef struct {
	union {
		iparml_control  p_ctrl;
		iparml_dpl      p_dpl;
		iparml_db       p_db;
		iparml_purge    p_purge;
		iparml_set_mask p_set_mask;
	} param;
	atomic_t in_use;
	__u32    res;
}  __attribute__ ((aligned(8))) iucv_param;
#define PARAM_POOL_SIZE (PAGE_SIZE / sizeof(iucv_param))

static iucv_param * iucv_param_pool;

MODULE_AUTHOR("(C) 2001 IBM Corp. by Fritz Elfert (felfert@millenux.com)");
MODULE_DESCRIPTION("Linux for S/390 IUCV lowlevel driver");
MODULE_LICENSE("GPL");

/*
 * Debugging stuff
 *******************************************************************************/


#ifdef DEBUG
static int debuglevel = 0;

module_param(debuglevel, int, 0);
MODULE_PARM_DESC(debuglevel,
 "Specifies the debug level (0=off ... 3=all)");

static void
iucv_dumpit(char *title, void *buf, int len)
{
	int i;
	__u8 *p = (__u8 *)buf;

	if (debuglevel < 3)
		return;

	printk(KERN_DEBUG "%s\n", title);
	printk("  ");
	for (i = 0; i < len; i++) {
		if (!(i % 16) && i != 0)
			printk ("\n  ");
		else if (!(i % 4) && i != 0)
			printk(" ");
		printk("%02X", *p++);
	}
	if (len % 16)
		printk ("\n");
	return;
}
#define iucv_debug(lvl, fmt, args...) \
do { \
	if (debuglevel >= lvl) \
		printk(KERN_DEBUG "%s: " fmt "\n", __FUNCTION__ , ## args); \
} while (0)

#else

#define iucv_debug(lvl, fmt, args...)
#define iucv_dumpit(title, buf, len)

#endif

/*
 * Internal functions
 *******************************************************************************/

/**
 * print start banner
 */
static void
iucv_banner(void)
{
	printk(KERN_INFO "IUCV lowlevel driver initialized\n");
}

/**
 * iucv_init - Initialization
 *
 * Allocates and initializes various data structures.
 */
static int
iucv_init(void)
{
	int ret;

	if (iucv_external_int_buffer)
		return 0;

	if (!MACHINE_IS_VM) {
		printk(KERN_ERR "IUCV: IUCV connection needs VM as base\n");
		return -EPROTONOSUPPORT;
	}

	ret = bus_register(&iucv_bus);
	if (ret) {
		printk(KERN_ERR "IUCV: failed to register bus.\n");
		return ret;
	}

	iucv_root = s390_root_dev_register("iucv");
	if (IS_ERR(iucv_root)) {
		printk(KERN_ERR "IUCV: failed to register iucv root.\n");
		bus_unregister(&iucv_bus);
		return PTR_ERR(iucv_root);
	}

	/* Note: GFP_DMA used used to get memory below 2G */
	iucv_external_int_buffer = kmalloc(sizeof(iucv_GeneralInterrupt),
					   GFP_KERNEL|GFP_DMA);
	if (!iucv_external_int_buffer) {
		printk(KERN_WARNING
		       "%s: Could not allocate external interrupt buffer\n",
		       __FUNCTION__);
		s390_root_dev_unregister(iucv_root);
		bus_unregister(&iucv_bus);
		return -ENOMEM;
	}
	memset(iucv_external_int_buffer, 0, sizeof(iucv_GeneralInterrupt));

	/* Initialize parameter pool */
	iucv_param_pool = kmalloc(sizeof(iucv_param) * PARAM_POOL_SIZE,
				  GFP_KERNEL|GFP_DMA);
	if (!iucv_param_pool) {
		printk(KERN_WARNING "%s: Could not allocate param pool\n",
		       __FUNCTION__);
		kfree(iucv_external_int_buffer);
		iucv_external_int_buffer = NULL;
		s390_root_dev_unregister(iucv_root);
		bus_unregister(&iucv_bus);
		return -ENOMEM;
	}
	memset(iucv_param_pool, 0, sizeof(iucv_param) * PARAM_POOL_SIZE);

	/* Initialize irq queue */
	INIT_LIST_HEAD(&iucv_irq_queue);

	/* Initialize handler table */
	INIT_LIST_HEAD(&iucv_handler_table);

	iucv_banner();
	return 0;
}

/**
 * iucv_exit - De-Initialization
 *
 * Frees everything allocated from iucv_init.
 */
static int iucv_retrieve_buffer (void);

static void
iucv_exit(void)
{
	iucv_retrieve_buffer();
	kfree(iucv_external_int_buffer);
	iucv_external_int_buffer = NULL;
	kfree(iucv_param_pool);
	iucv_param_pool = NULL;
	s390_root_dev_unregister(iucv_root);
	bus_unregister(&iucv_bus);
	printk(KERN_INFO "IUCV lowlevel driver unloaded\n");
}

/**
 * grab_param: - Get a parameter buffer from the pre-allocated pool.
 *
 * This function searches for an unused element in the pre-allocated pool
 * of parameter buffers. If one is found, it marks it "in use" and returns
 * a pointer to it. The calling function is responsible for releasing it
 * when it has finished its usage.
 *
 * Returns: A pointer to iucv_param.
 */
static __inline__ iucv_param *
grab_param(void)
{
	iucv_param *ptr;
        static int hint = 0;

	ptr = iucv_param_pool + hint;
	do {
		ptr++;
		if (ptr >= iucv_param_pool + PARAM_POOL_SIZE)
			ptr = iucv_param_pool;
	} while (atomic_cmpxchg(&ptr->in_use, 0, 1) != 0);
	hint = ptr - iucv_param_pool;

	memset(&ptr->param, 0, sizeof(ptr->param));
	return ptr;
}

/**
 * release_param - Release a parameter buffer.
 * @p: A pointer to a struct iucv_param, previously obtained by calling
 *     grab_param().
 *
 * This function marks the specified parameter buffer "unused".
 */
static __inline__ void
release_param(void *p)
{
	atomic_set(&((iucv_param *)p)->in_use, 0);
}

/**
 * iucv_add_handler: - Add a new handler
 * @new_handler: handle that is being entered into chain.
 *
 * Places new handle on iucv_handler_table, if identical handler is not
 * found.
 *
 * Returns: 0 on success, !0 on failure (handler already in chain).
 */
static int
iucv_add_handler (handler *new)
{
	ulong flags;

	iucv_debug(1, "entering");
	iucv_dumpit("handler:", new, sizeof(handler));

	spin_lock_irqsave (&iucv_lock, flags);
	if (!list_empty(&iucv_handler_table)) {
		struct list_head *lh;

		/**
		 * Search list for handler with identical id. If one
		 * is found, the new handler is _not_ added.
		 */
		list_for_each(lh, &iucv_handler_table) {
			handler *h = list_entry(lh, handler, list);
			if (!memcmp(&new->id, &h->id, sizeof(h->id))) {
				iucv_debug(1, "ret 1");
				spin_unlock_irqrestore (&iucv_lock, flags);
				return 1;
			}
		}
	}
	/**
	 * If we get here, no handler was found.
	 */
	INIT_LIST_HEAD(&new->list);
	list_add(&new->list, &iucv_handler_table);
	spin_unlock_irqrestore (&iucv_lock, flags);

	iucv_debug(1, "exiting");
	return 0;
}

/**
 * b2f0:
 * @code: identifier of IUCV call to CP.
 * @parm: pointer to 40 byte iparml area passed to CP
 *
 * Calls CP to execute IUCV commands.
 *
 * Returns: return code from CP's IUCV call
 */
static __inline__ ulong
b2f0(__u32 code, void *parm)
{
	iucv_dumpit("iparml before b2f0 call:", parm, sizeof(iucv_param));

	asm volatile (
		"LRA   1,0(%1)\n\t"
		"LR    0,%0\n\t"
		".long 0xb2f01000"
		:
		: "d" (code), "a" (parm)
		: "0", "1"
		);

	iucv_dumpit("iparml after b2f0 call:", parm, sizeof(iucv_param));

	return (unsigned long)*((__u8 *)(parm + 3));
}

/*
 * Name: iucv_add_pathid
 * Purpose: Adds a path id to the system.
 * Input: pathid -  pathid that is going to be entered into system
 *        handle -  address of handler that the pathid will be associated
 *		   with.
 *        pgm_data - token passed in by application.
 * Output: 0: successful addition of pathid
 *	   - EINVAL - pathid entry is being used by another application
 *	   - ENOMEM - storage allocation for a new pathid table failed
*/
static int
__iucv_add_pathid(__u16 pathid, handler *handler)
{

	iucv_debug(1, "entering");

	iucv_debug(1, "handler is pointing to %p", handler);

	if (pathid > (max_connections - 1))
		return -EINVAL;

	if (iucv_pathid_table[pathid]) {
		iucv_debug(1, "pathid entry is %p", iucv_pathid_table[pathid]);
		printk(KERN_WARNING
		       "%s: Pathid being used, error.\n", __FUNCTION__);
		return -EINVAL;
	}
	iucv_pathid_table[pathid] = handler;

	iucv_debug(1, "exiting");
	return 0;
}				/* end of add_pathid function */

static int
iucv_add_pathid(__u16 pathid, handler *handler)
{
	ulong flags;
	int rc;

	spin_lock_irqsave (&iucv_lock, flags);
	rc = __iucv_add_pathid(pathid, handler);
	spin_unlock_irqrestore (&iucv_lock, flags);
	return rc;
}

static void
iucv_remove_pathid(__u16 pathid)
{
	ulong flags;

	if (pathid > (max_connections - 1))
		return;

	spin_lock_irqsave (&iucv_lock, flags);
	iucv_pathid_table[pathid] = NULL;
	spin_unlock_irqrestore (&iucv_lock, flags);
}

/**
 * iucv_declare_buffer_cpuid
 * Register at VM for subsequent IUCV operations. This is executed
 * on the reserved CPU iucv_cpuid. Called from iucv_declare_buffer().
 */
static void
iucv_declare_buffer_cpuid (void *result)
{
	iparml_db *parm;

	parm = (iparml_db *)grab_param();
	parm->ipbfadr1 = virt_to_phys(iucv_external_int_buffer);
	if ((*((ulong *)result) = b2f0(DECLARE_BUFFER, parm)) == 1)
		*((ulong *)result) = parm->iprcode;
	release_param(parm);
}

/**
 * iucv_retrieve_buffer_cpuid:
 * Unregister IUCV usage at VM. This is always executed on the same
 * cpu that registered the buffer to VM.
 * Called from iucv_retrieve_buffer().
 */
static void
iucv_retrieve_buffer_cpuid (void *cpu)
{
	iparml_control *parm;

	parm = (iparml_control *)grab_param();
	b2f0(RETRIEVE_BUFFER, parm);
	release_param(parm);
}

/**
 * Name: iucv_declare_buffer
 * Purpose: Specifies the guests real address of an external
 *          interrupt.
 * Input: void
 * Output: iprcode - return code from b2f0 call
 */
static int
iucv_declare_buffer (void)
{
	unsigned long flags;
	ulong b2f0_result;

	iucv_debug(1, "entering");
	b2f0_result = -ENODEV;
	spin_lock_irqsave (&iucv_lock, flags);
	if (iucv_cpuid == -1) {
		/* Reserve any cpu for use by iucv. */
		iucv_cpuid = smp_get_cpu(CPU_MASK_ALL);
		spin_unlock_irqrestore (&iucv_lock, flags);
		smp_call_function_on(iucv_declare_buffer_cpuid,
			&b2f0_result, 0, 1, iucv_cpuid);
		if (b2f0_result) {
			smp_put_cpu(iucv_cpuid);
			iucv_cpuid = -1;
		}
		iucv_debug(1, "Address of EIB = %p", iucv_external_int_buffer);
	} else {
		spin_unlock_irqrestore (&iucv_lock, flags);
		b2f0_result = 0;
	}
	iucv_debug(1, "exiting");
	return b2f0_result;
}

/**
 * iucv_retrieve_buffer:
 *
 * Terminates all use of IUCV.
 * Returns: return code from CP
 */
static int
iucv_retrieve_buffer (void)
{
	iucv_debug(1, "entering");
	if (iucv_cpuid != -1) {
		smp_call_function_on(iucv_retrieve_buffer_cpuid,
				     0, 0, 1, iucv_cpuid);
		/* Release the cpu reserved by iucv_declare_buffer. */
		smp_put_cpu(iucv_cpuid);
		iucv_cpuid = -1;
	}
	iucv_debug(1, "exiting");
	return 0;
}

/**
 * iucv_remove_handler:
 * @users_handler: handler to be removed
 *
 * Remove handler when application unregisters.
 */
static void
iucv_remove_handler(handler *handler)
{
	unsigned long flags;

	if ((!iucv_pathid_table) || (!handler))
		return;

	iucv_debug(1, "entering");

	spin_lock_irqsave (&iucv_lock, flags);
	list_del(&handler->list);
	if (list_empty(&iucv_handler_table)) {
		if (register_flag) {
			unregister_external_interrupt(0x4000, iucv_irq_handler);
			register_flag = 0;
		}
	}
	spin_unlock_irqrestore (&iucv_lock, flags);

	iucv_debug(1, "exiting");
	return;
}

/**
 * iucv_register_program:
 * @pgmname:  user identification
 * @userid:   machine identification
 * @pgmmask:  Indicates which bits in the pgmname and userid combined will be
 *            used to determine who is given control.
 * @ops:      Address of interrupt handler table.
 * @pgm_data: Application data to be passed to interrupt handlers.
 *
 * Registers an application with IUCV.
 * Returns:
 *           The address of handler, or NULL on failure.
 * NOTE on pgmmask:
 *   If pgmname, userid and pgmmask are provided, pgmmask is entered into the
 *   handler as is.
 *   If pgmmask is NULL, the internal mask is set to all 0xff's
 *   When userid is NULL, the first 8 bytes of the internal mask are forced
 *   to 0x00.
 *   If pgmmask and userid are NULL, the first 8 bytes of the internal mask
 *   are forced to 0x00 and the last 16 bytes to 0xff.
 */

iucv_handle_t
iucv_register_program (__u8 pgmname[16],
		       __u8 userid[8],
		       __u8 pgmmask[24],
		       iucv_interrupt_ops_t * ops, void *pgm_data)
{
	ulong rc = 0;		/* return code from function calls */
	handler *new_handler;

	iucv_debug(1, "entering");

	if (ops == NULL) {
		/* interrupt table is not defined */
		printk(KERN_WARNING "%s: Interrupt table is not defined, "
		       "exiting\n", __FUNCTION__);
		return NULL;
	}
	if (!pgmname) {
		printk(KERN_WARNING "%s: pgmname not provided\n", __FUNCTION__);
		return NULL;
	}

	/* Allocate handler entry */
	new_handler = (handler *)kmalloc(sizeof(handler), GFP_ATOMIC);
	if (new_handler == NULL) {
		printk(KERN_WARNING "%s: storage allocation for new handler "
		       "failed.\n", __FUNCTION__);
		return NULL;
	}

	if (!iucv_pathid_table) {
		if (iucv_init()) {
			kfree(new_handler);
			return NULL;
		}

		max_connections = iucv_query_maxconn();
		iucv_pathid_table = kmalloc(max_connections * sizeof(handler *),
				       GFP_ATOMIC);
		if (iucv_pathid_table == NULL) {
			printk(KERN_WARNING "%s: iucv_pathid_table storage "
			       "allocation failed\n", __FUNCTION__);
			kfree(new_handler);
			return NULL;
		}
		memset (iucv_pathid_table, 0, max_connections * sizeof(handler *));
	}
	memset(new_handler, 0, sizeof (handler));
	memcpy(new_handler->id.user_data, pgmname,
		sizeof (new_handler->id.user_data));
	if (userid) {
		memcpy (new_handler->id.userid, userid,
			sizeof (new_handler->id.userid));
		ASCEBC (new_handler->id.userid,
			sizeof (new_handler->id.userid));
		EBC_TOUPPER (new_handler->id.userid,
			     sizeof (new_handler->id.userid));
		
		if (pgmmask) {
			memcpy (new_handler->id.mask, pgmmask,
				sizeof (new_handler->id.mask));
		} else {
			memset (new_handler->id.mask, 0xFF,
				sizeof (new_handler->id.mask));
		}
	} else {
		if (pgmmask) {
			memcpy (new_handler->id.mask, pgmmask,
				sizeof (new_handler->id.mask));
		} else {
			memset (new_handler->id.mask, 0xFF,
				sizeof (new_handler->id.mask));
		}
		memset (new_handler->id.userid, 0x00,
			sizeof (new_handler->id.userid));
	}
	/* fill in the rest of handler */
	new_handler->pgm_data = pgm_data;
	new_handler->interrupt_table = ops;

	/*
	 * Check if someone else is registered with same pgmname, userid
	 * and mask. If someone is already registered with same pgmname,
	 * userid and mask, registration will fail and NULL will be returned
	 * to the application.
	 * If identical handler not found, then handler is added to list.
	 */
	rc = iucv_add_handler(new_handler);
	if (rc) {
		printk(KERN_WARNING "%s: Someone already registered with same "
		       "pgmname, userid, pgmmask\n", __FUNCTION__);
		kfree (new_handler);
		return NULL;
	}

	rc = iucv_declare_buffer();
	if (rc) {
		char *err = "Unknown";
		iucv_remove_handler(new_handler);
		kfree(new_handler);
		switch(rc) {
		case 0x03:
			err = "Directory error";
			break;
		case 0x0a:
			err = "Invalid length";
			break;
		case 0x13:
			err = "Buffer already exists";
			break;
		case 0x3e:
			err = "Buffer overlap";
			break;
		case 0x5c:
			err = "Paging or storage error";
			break;
		}
		printk(KERN_WARNING "%s: iucv_declare_buffer "
		       "returned error 0x%02lx (%s)\n", __FUNCTION__, rc, err);
		return NULL;
	}
	if (!register_flag) {
		/* request the 0x4000 external interrupt */
		rc = register_external_interrupt (0x4000, iucv_irq_handler);
		if (rc) {
			iucv_remove_handler(new_handler);
			kfree (new_handler);
			printk(KERN_WARNING "%s: "
			       "register_external_interrupt returned %ld\n",
			       __FUNCTION__, rc);
			return NULL;

		}
		register_flag = 1;
	}
	iucv_debug(1, "exiting");
	return new_handler;
}				/* end of register function */

/**
 * iucv_unregister_program:
 * @handle: address of handler
 *
 * Unregister application with IUCV.
 * Returns:
 *   0 on success, -EINVAL, if specified handle is invalid.
 */

int
iucv_unregister_program (iucv_handle_t handle)
{
	handler *h = NULL;
	struct list_head *lh;
	int i;
	ulong flags;

	iucv_debug(1, "entering");
	iucv_debug(1, "address of handler is %p", h);

	/* Checking if handle is valid  */
	spin_lock_irqsave (&iucv_lock, flags);
	list_for_each(lh, &iucv_handler_table) {
		if ((handler *)handle == list_entry(lh, handler, list)) {
			h = (handler *)handle;
			break;
		}
	}
	if (!h) {
		spin_unlock_irqrestore (&iucv_lock, flags);
		if (handle)
			printk(KERN_WARNING
			       "%s: Handler not found in iucv_handler_table.\n",
			       __FUNCTION__);
		else
			printk(KERN_WARNING
			       "%s: NULL handle passed by application.\n",
			       __FUNCTION__);
		return -EINVAL;
	}

	/**
	 * First, walk thru iucv_pathid_table and sever any pathid which is
	 * still pointing to the handler to be removed.
	 */
	for (i = 0; i < max_connections; i++)
		if (iucv_pathid_table[i] == h) {
			spin_unlock_irqrestore (&iucv_lock, flags);
			iucv_sever(i, h->id.user_data);
			spin_lock_irqsave(&iucv_lock, flags);
		}
	spin_unlock_irqrestore (&iucv_lock, flags);

	iucv_remove_handler(h);
	kfree(h);

	iucv_debug(1, "exiting");
	return 0;
}

/**
 * iucv_accept:
 * @pathid:             Path identification number
 * @msglim_reqstd:      The number of outstanding messages requested.
 * @user_data:          Data specified by the iucv_connect function.
 * @flags1:             Contains options for this path.
 *     - IPPRTY (0x20)   Specifies if you want to send priority message.
 *     - IPRMDATA (0x80) Specifies whether your program can handle a message
 *                       in the parameter list.
 *     - IPQUSCE (0x40)  Specifies whether you want to quiesce the path being
 *		         established.
 * @handle:             Address of handler.
 * @pgm_data:           Application data passed to interrupt handlers.
 * @flags1_out:         Pointer to an int. If not NULL, on return the options for
 *                      the path are stored at the given location:
 *     - IPPRTY (0x20)  Indicates you may send a priority message.
 * @msglim:             Pointer to an __u16. If not NULL, on return the maximum
 *                      number of outstanding messages is stored at the given
 *                      location.
 *
 * This function is issued after the user receives a Connection Pending external
 * interrupt and now wishes to complete the IUCV communication path.
 * Returns:
 *   return code from CP
 */
int
iucv_accept(__u16 pathid, __u16 msglim_reqstd,
	     __u8 user_data[16], int flags1,
	     iucv_handle_t handle, void *pgm_data,
	     int *flags1_out, __u16 * msglim)
{
	ulong b2f0_result = 0;
	ulong flags;
	struct list_head *lh;
	handler *h = NULL;
	iparml_control *parm;

	iucv_debug(1, "entering");
	iucv_debug(1, "pathid = %d", pathid);

	/* Checking if handle is valid  */
	spin_lock_irqsave (&iucv_lock, flags);
	list_for_each(lh, &iucv_handler_table) {
		if ((handler *)handle == list_entry(lh, handler, list)) {
			h = (handler *)handle;
			break;
		}
	}
	spin_unlock_irqrestore (&iucv_lock, flags);

	if (!h) {
		if (handle)
			printk(KERN_WARNING
			       "%s: Handler not found in iucv_handler_table.\n",
			       __FUNCTION__);
		else
			printk(KERN_WARNING
			       "%s: NULL handle passed by application.\n",
			       __FUNCTION__);
		return -EINVAL;
	}

	parm = (iparml_control *)grab_param();

	parm->ippathid = pathid;
	parm->ipmsglim = msglim_reqstd;
	if (user_data)
		memcpy(parm->ipuser, user_data, sizeof(parm->ipuser));

	parm->ipflags1 = (__u8)flags1;
	b2f0_result = b2f0(ACCEPT, parm);

	if (!b2f0_result) {
		if (msglim)
			*msglim = parm->ipmsglim;
		if (pgm_data)
			h->pgm_data = pgm_data;
		if (flags1_out)
			*flags1_out = (parm->ipflags1 & IPPRTY) ? IPPRTY : 0;
	}
	release_param(parm);

	iucv_debug(1, "exiting");
	return b2f0_result;
}

/**
 * iucv_connect:
 * @pathid:        Path identification number
 * @msglim_reqstd: Number of outstanding messages requested
 * @user_data:     16-byte user data
 * @userid:        8-byte of user identification
 * @system_name:   8-byte identifying the system name
 * @flags1:        Specifies options for this path:
 *     - IPPRTY (0x20)   Specifies if you want to send priority message.
 *     - IPRMDATA (0x80) Specifies whether your program can handle a message
 *                       in  the parameter list.
 *     - IPQUSCE (0x40)  Specifies whether you want to quiesce the path being
 *                       established.
 *     - IPLOCAL (0x01)  Allows an application to force the partner to be on the
 *                       local system. If local is specified then target class
 *                       cannot be specified.
 * @flags1_out:    Pointer to an int. If not NULL, on return the options for
 *                 the path are stored at the given location:
 *     - IPPRTY (0x20)   Indicates you may send a priority message.
 * @msglim:        Pointer to an __u16. If not NULL, on return the maximum
 *                 number of outstanding messages is stored at the given
 *                 location.
 * @handle:        Address of handler.
 * @pgm_data:      Application data to be passed to interrupt handlers.
 *
 * This function establishes an IUCV path. Although the connect may complete
 * successfully, you are not able to use the path until you receive an IUCV
 * Connection Complete external interrupt.
 * Returns: return code from CP, or one of the following
 *     - ENOMEM
 *     - return code from iucv_declare_buffer
 *     - EINVAL - invalid handle passed by application
 *     - EINVAL - pathid address is NULL
 *     - ENOMEM - pathid table storage allocation failed
 *     - return code from internal function add_pathid
 */
int
iucv_connect (__u16 *pathid, __u16 msglim_reqstd,
	      __u8 user_data[16], __u8 userid[8],
	      __u8 system_name[8], int flags1,
	      int *flags1_out, __u16 * msglim,
	      iucv_handle_t handle, void *pgm_data)
{
	iparml_control *parm;
	iparml_control local_parm;
	struct list_head *lh;
	ulong b2f0_result = 0;
	ulong flags;
	int add_pathid_result = 0;
	handler *h = NULL;
	__u8 no_memory[16] = "NO MEMORY";

	iucv_debug(1, "entering");

	/* Checking if handle is valid  */
	spin_lock_irqsave (&iucv_lock, flags);
	list_for_each(lh, &iucv_handler_table) {
		if ((handler *)handle == list_entry(lh, handler, list)) {
			h = (handler *)handle;
			break;
		}
	}
	spin_unlock_irqrestore (&iucv_lock, flags);

	if (!h) {
		if (handle)
			printk(KERN_WARNING
			       "%s: Handler not found in iucv_handler_table.\n",
			       __FUNCTION__);
		else
			printk(KERN_WARNING
			       "%s: NULL handle passed by application.\n",
			       __FUNCTION__);
		return -EINVAL;
	}

	if (pathid == NULL) {
		printk(KERN_WARNING "%s: NULL pathid pointer\n",
		       __FUNCTION__);
		return -EINVAL;
	}

	parm = (iparml_control *)grab_param();

	parm->ipmsglim = msglim_reqstd;

	if (user_data)
		memcpy(parm->ipuser, user_data, sizeof(parm->ipuser));

	if (userid) {
		memcpy(parm->ipvmid, userid, sizeof(parm->ipvmid));
		ASCEBC(parm->ipvmid, sizeof(parm->ipvmid));
		EBC_TOUPPER(parm->ipvmid, sizeof(parm->ipvmid));
	}

	if (system_name) {
		memcpy(parm->iptarget, system_name, sizeof(parm->iptarget));
		ASCEBC(parm->iptarget, sizeof(parm->iptarget));
		EBC_TOUPPER(parm->iptarget, sizeof(parm->iptarget));
	}

	/* In order to establish an IUCV connection, the procedure is:
         *
         * b2f0(CONNECT)
         * take the ippathid from the b2f0 call
         * register the handler to the ippathid
         *
         * Unfortunately, the ConnectionEstablished message gets sent after the
         * b2f0(CONNECT) call but before the register is handled.
         *
         * In order for this race condition to be eliminated, the IUCV Control
         * Interrupts must be disabled for the above procedure.
         *
         * David Kennedy <dkennedy@linuxcare.com>
         */

	/* Enable everything but IUCV Control messages */
	iucv_setmask(~(AllInterrupts));
	messagesDisabled = 1;

	spin_lock_irqsave (&iucv_lock, flags);
	parm->ipflags1 = (__u8)flags1;
	b2f0_result = b2f0(CONNECT, parm);
	memcpy(&local_parm, parm, sizeof(local_parm));
	release_param(parm);
	parm = &local_parm;
	if (!b2f0_result)
		add_pathid_result = __iucv_add_pathid(parm->ippathid, h);
	spin_unlock_irqrestore (&iucv_lock, flags);

	if (b2f0_result) {
		iucv_setmask(~0);
		messagesDisabled = 0;
		return b2f0_result;
	}

	*pathid = parm->ippathid;

	/* Enable everything again */
	iucv_setmask(IUCVControlInterruptsFlag);

	if (msglim)
		*msglim = parm->ipmsglim;
	if (flags1_out)
		*flags1_out = (parm->ipflags1 & IPPRTY) ? IPPRTY : 0;

	if (add_pathid_result) {
		iucv_sever(*pathid, no_memory);
		printk(KERN_WARNING "%s: add_pathid failed with rc ="
			" %d\n", __FUNCTION__, add_pathid_result);
		return(add_pathid_result);
	}

	iucv_debug(1, "exiting");
	return b2f0_result;
}

/**
 * iucv_purge:
 * @pathid: Path identification number
 * @msgid:  Message ID of message to purge.
 * @srccls: Message class of the message to purge.
 * @audit:  Pointer to an __u32. If not NULL, on return, information about
 *          asynchronous errors that may have affected the normal completion
 *          of this message ist stored at the given location.
 *
 * Cancels a message you have sent.
 * Returns: return code from CP
 */
int
iucv_purge (__u16 pathid, __u32 msgid, __u32 srccls, __u32 *audit)
{
	iparml_purge *parm;
	ulong b2f0_result = 0;

	iucv_debug(1, "entering");
	iucv_debug(1, "pathid = %d", pathid);

	parm = (iparml_purge *)grab_param();

	parm->ipmsgid = msgid;
	parm->ippathid = pathid;
	parm->ipsrccls = srccls;
	parm->ipflags1 |= (IPSRCCLS | IPFGMID | IPFGPID);
	b2f0_result = b2f0(PURGE, parm);

	if (!b2f0_result && audit) {
		memcpy(audit, parm->ipaudit, sizeof(parm->ipaudit));
		/* parm->ipaudit has only 3 bytes */
		*audit >>= 8;
	}
	
	release_param(parm);

	iucv_debug(1, "b2f0_result = %ld", b2f0_result);
	iucv_debug(1, "exiting");
	return b2f0_result;
}

/**
 * iucv_query_generic:
 * @want_maxconn: Flag, describing which value is to be returned.
 *
 * Helper function for iucv_query_maxconn() and iucv_query_bufsize().
 *
 * Returns: The buffersize, if want_maxconn is 0; the maximum number of
 *           connections, if want_maxconn is 1 or an error-code < 0 on failure.
 */
static int
iucv_query_generic(int want_maxconn)
{
	iparml_purge *parm = (iparml_purge *)grab_param();
	int bufsize, maxconn;
	int ccode;

	/**
	 * Call b2f0 and store R0 (max buffer size),
	 * R1 (max connections) and CC.
	 */
	asm volatile (
		"LRA   1,0(%4)\n\t"
		"LR    0,%3\n\t"
		".long 0xb2f01000\n\t"
		"IPM   %0\n\t"
		"SRL   %0,28\n\t"
		"ST    0,%1\n\t"
		"ST    1,%2\n\t"
		: "=d" (ccode), "=m" (bufsize), "=m" (maxconn)
		: "d" (QUERY), "a" (parm)
		: "0", "1", "cc"
		);
	release_param(parm);

	if (ccode)
		return -EPERM;
	if (want_maxconn)
		return maxconn;
	return bufsize;
}

/**
 * iucv_query_maxconn:
 *
 * Determines the maximum number of connections thay may be established.
 *
 * Returns: Maximum number of connections that can be.
 */
ulong
iucv_query_maxconn(void)
{
	return iucv_query_generic(1);
}

/**
 * iucv_query_bufsize:
 *
 * Determines the size of the external interrupt buffer.
 *
 * Returns: Size of external interrupt buffer.
 */
ulong
iucv_query_bufsize (void)
{
	return iucv_query_generic(0);
}

/**
 * iucv_quiesce:
 * @pathid:    Path identification number
 * @user_data: 16-byte user data
 *
 * Temporarily suspends incoming messages on an IUCV path.
 * You can later reactivate the path by invoking the iucv_resume function.
 * Returns: return code from CP
 */
int
iucv_quiesce (__u16 pathid, __u8 user_data[16])
{
	iparml_control *parm;
	ulong b2f0_result = 0;

	iucv_debug(1, "entering");
	iucv_debug(1, "pathid = %d", pathid);

	parm = (iparml_control *)grab_param();

	memcpy(parm->ipuser, user_data, sizeof(parm->ipuser));
	parm->ippathid = pathid;

	b2f0_result = b2f0(QUIESCE, parm);
	release_param(parm);

	iucv_debug(1, "b2f0_result = %ld", b2f0_result);
	iucv_debug(1, "exiting");

	return b2f0_result;
}

/**
 * iucv_receive:
 * @pathid: Path identification number.
 * @buffer: Address of buffer to receive. Must be below 2G.
 * @buflen: Length of buffer to receive.
 * @msgid:  Specifies the message ID.
 * @trgcls: Specifies target class.
 * @flags1_out: Receives options for path on return.
 *    - IPNORPY (0x10)  Specifies whether a reply is required
 *    - IPPRTY (0x20)   Specifies if you want to send priority message
 *    - IPRMDATA (0x80) Specifies the data is contained in the parameter list
 * @residual_buffer: Receives the address of buffer updated by the number
 *                   of bytes you have received on return.
 * @residual_length: On return, receives one of the following values:
 *    - 0                          If the receive buffer is the same length as
 *                                 the message.
 *    - Remaining bytes in buffer  If the receive buffer is longer than the
 *                                 message.
 *    - Remaining bytes in message If the receive buffer is shorter than the
 *                                 message.
 *
 * This function receives messages that are being sent to you over established
 * paths.
 * Returns: return code from CP IUCV call; If the receive buffer is shorter
 *   than the message, always 5
 *   -EINVAL - buffer address is pointing to NULL
 */
int
iucv_receive (__u16 pathid, __u32 msgid, __u32 trgcls,
	      void *buffer, ulong buflen,
	      int *flags1_out, ulong * residual_buffer, ulong * residual_length)
{
	iparml_db *parm;
	ulong b2f0_result;
	int moved = 0;	/* number of bytes moved from parmlist to buffer */

	iucv_debug(2, "entering");

	if (!buffer)
		return -EINVAL;

	parm = (iparml_db *)grab_param();

	parm->ipbfadr1 = (__u32) (addr_t) buffer;
	parm->ipbfln1f = (__u32) ((ulong) buflen);
	parm->ipmsgid = msgid;
	parm->ippathid = pathid;
	parm->iptrgcls = trgcls;
	parm->ipflags1 = (IPFGPID | IPFGMID | IPFGMCL);

	b2f0_result = b2f0(RECEIVE, parm);

	if (!b2f0_result || b2f0_result == 5) {
		if (flags1_out) {
			iucv_debug(2, "*flags1_out = %d", *flags1_out);
			*flags1_out = (parm->ipflags1 & (~0x07));
			iucv_debug(2, "*flags1_out = %d", *flags1_out);
		}

		if (!(parm->ipflags1 & IPRMDATA)) {	/*msg not in parmlist */
			if (residual_length)
				*residual_length = parm->ipbfln1f;

			if (residual_buffer)
				*residual_buffer = parm->ipbfadr1;
		} else {
			moved = min_t (unsigned long, buflen, 8);

			memcpy ((char *) buffer,
				(char *) &parm->ipbfadr1, moved);

			if (buflen < 8)
				b2f0_result = 5;

			if (residual_length)
				*residual_length = abs (buflen - 8);

			if (residual_buffer)
				*residual_buffer = (ulong) (buffer + moved);
		}
	}
	release_param(parm);

	iucv_debug(2, "exiting");
	return b2f0_result;
}

/*
 * Name: iucv_receive_array
 * Purpose: This function receives messages that are being sent to you
 *          over established paths.
 * Input: pathid - path identification number
 *        buffer - address of array of buffers
 *        buflen - total length of buffers
 *        msgid - specifies the message ID.
 *        trgcls - specifies target class
 * Output:
 *        flags1_out: Options for path.
 *          IPNORPY - 0x10 specifies whether a reply is required
 *          IPPRTY - 0x20 specifies if you want to send priority message
 *         IPRMDATA - 0x80 specifies the data is contained in the parameter list
 *       residual_buffer - address points to the current list entry IUCV
 *                         is working on.
 *       residual_length -
 *              Contains one of the following values, if the receive buffer is:
 *               The same length as the message, this field is zero.
 *               Longer than the message, this field contains the number of
 *                bytes remaining in the buffer.
 *               Shorter than the message, this field contains the residual
 *                count (that is, the number of bytes remaining in the
 *                message that does not fit into the buffer. In this case
 *		  b2f0_result = 5.
 * Return: b2f0_result - return code from CP
 *         (-EINVAL) - buffer address is NULL
 */
int
iucv_receive_array (__u16 pathid,
		    __u32 msgid, __u32 trgcls,
		    iucv_array_t * buffer, ulong buflen,
		    int *flags1_out,
		    ulong * residual_buffer, ulong * residual_length)
{
	iparml_db *parm;
	ulong b2f0_result;
	int i = 0, moved = 0, need_to_move = 8, dyn_len;

	iucv_debug(2, "entering");

	if (!buffer)
		return -EINVAL;

	parm = (iparml_db *)grab_param();

	parm->ipbfadr1 = (__u32) ((ulong) buffer);
	parm->ipbfln1f = (__u32) buflen;
	parm->ipmsgid = msgid;
	parm->ippathid = pathid;
	parm->iptrgcls = trgcls;
	parm->ipflags1 = (IPBUFLST | IPFGPID | IPFGMID | IPFGMCL);

	b2f0_result = b2f0(RECEIVE, parm);

	if (!b2f0_result || b2f0_result == 5) {

		if (flags1_out) {
			iucv_debug(2, "*flags1_out = %d", *flags1_out);
			*flags1_out = (parm->ipflags1 & (~0x07));
			iucv_debug(2, "*flags1_out = %d", *flags1_out);
		}

		if (!(parm->ipflags1 & IPRMDATA)) {	/*msg not in parmlist */

			if (residual_length)
				*residual_length = parm->ipbfln1f;

			if (residual_buffer)
				*residual_buffer = parm->ipbfadr1;

		} else {
			/* copy msg from parmlist to users array. */

			while ((moved < 8) && (moved < buflen)) {
				dyn_len =
				    min_t (unsigned int,
					 (buffer + i)->length, need_to_move);

				memcpy ((char *)((ulong)((buffer + i)->address)),
					((char *) &parm->ipbfadr1) + moved,
					dyn_len);

				moved += dyn_len;
				need_to_move -= dyn_len;

				(buffer + i)->address =
				    	(__u32)
				((ulong)(__u8 *) ((ulong)(buffer + i)->address)
						+ dyn_len);

				(buffer + i)->length -= dyn_len;
				i++;
			}

			if (need_to_move)	/* buflen < 8 bytes */
				b2f0_result = 5;

			if (residual_length)
				*residual_length = abs (buflen - 8);

			if (residual_buffer) {
				if (!moved)
					*residual_buffer = (ulong) buffer;
				else
					*residual_buffer =
					    (ulong) (buffer + (i - 1));
			}

		}
	}
	release_param(parm);

	iucv_debug(2, "exiting");
	return b2f0_result;
}

/**
 * iucv_reject:
 * @pathid: Path identification number.
 * @msgid:  Message ID of the message to reject.
 * @trgcls: Target class of the message to reject.
 * Returns: return code from CP
 *
 * Refuses a specified message. Between the time you are notified of a
 * message and the time that you complete the message, the message may
 * be rejected.
 */
int
iucv_reject (__u16 pathid, __u32 msgid, __u32 trgcls)
{
	iparml_db *parm;
	ulong b2f0_result = 0;

	iucv_debug(1, "entering");
	iucv_debug(1, "pathid = %d", pathid);

	parm = (iparml_db *)grab_param();

	parm->ippathid = pathid;
	parm->ipmsgid = msgid;
	parm->iptrgcls = trgcls;
	parm->ipflags1 = (IPFGMCL | IPFGMID | IPFGPID);

	b2f0_result = b2f0(REJECT, parm);
	release_param(parm);

	iucv_debug(1, "b2f0_result = %ld", b2f0_result);
	iucv_debug(1, "exiting");

	return b2f0_result;
}

/*
 * Name: iucv_reply
 * Purpose: This function responds to the two-way messages that you
 *          receive. You must identify completely the message to
 *          which you wish to reply. ie, pathid, msgid, and trgcls.
 * Input: pathid - path identification number
 *        msgid - specifies the message ID.
 *        trgcls - specifies target class
 *        flags1 - option for path
 *                 IPPRTY- 0x20 - specifies if you want to send priority message
 *        buffer - address of reply buffer
 *        buflen - length of reply buffer
 * Output: ipbfadr2 - Address of buffer updated by the number
 *                    of bytes you have moved.
 *         ipbfln2f - Contains one of the following values:
 *              If the answer buffer is the same length as the reply, this field
 *               contains zero.
 *              If the answer buffer is longer than the reply, this field contains
 *               the number of bytes remaining in the buffer.
 *              If the answer buffer is shorter than the reply, this field contains
 *               a residual count (that is, the number of bytes remianing in the
 *               reply that does not fit into the buffer. In this
 *                case b2f0_result = 5.
 * Return: b2f0_result - return code from CP
 *         (-EINVAL) - buffer address is NULL
 */
int
iucv_reply (__u16 pathid,
	    __u32 msgid, __u32 trgcls,
	    int flags1,
	    void *buffer, ulong buflen, ulong * ipbfadr2, ulong * ipbfln2f)
{
	iparml_db *parm;
	ulong b2f0_result;

	iucv_debug(2, "entering");

	if (!buffer)
		return -EINVAL;

	parm = (iparml_db *)grab_param();

	parm->ipbfadr2 = (__u32) ((ulong) buffer);
	parm->ipbfln2f = (__u32) buflen;	/* length of message */
	parm->ippathid = pathid;
	parm->ipmsgid = msgid;
	parm->iptrgcls = trgcls;
	parm->ipflags1 = (__u8) flags1;	/* priority message */

	b2f0_result = b2f0(REPLY, parm);

	if ((!b2f0_result) || (b2f0_result == 5)) {
		if (ipbfadr2)
			*ipbfadr2 = parm->ipbfadr2;
		if (ipbfln2f)
			*ipbfln2f = parm->ipbfln2f;
	}
	release_param(parm);

	iucv_debug(2, "exiting");

	return b2f0_result;
}

/*
 * Name: iucv_reply_array
 * Purpose: This function responds to the two-way messages that you
 *          receive. You must identify completely the message to
 *          which you wish to reply. ie, pathid, msgid, and trgcls.
 *          The array identifies a list of addresses and lengths of
 *          discontiguous buffers that contains the reply data.
 * Input: pathid - path identification number
 *        msgid - specifies the message ID.
 *        trgcls - specifies target class
 *        flags1 - option for path
 *                 IPPRTY- specifies if you want to send priority message
 *        buffer - address of array of reply buffers
 *        buflen - total length of reply buffers
 * Output: ipbfadr2 - Address of buffer which IUCV is currently working on.
 *         ipbfln2f - Contains one of the following values:
 *              If the answer buffer is the same length as the reply, this field
 *               contains zero.
 *              If the answer buffer is longer than the reply, this field contains
 *               the number of bytes remaining in the buffer.
 *              If the answer buffer is shorter than the reply, this field contains
 *               a residual count (that is, the number of bytes remianing in the
 *               reply that does not fit into the buffer. In this
 *               case b2f0_result = 5.
 * Return: b2f0_result - return code from CP
 *             (-EINVAL) - buffer address is NULL
*/
int
iucv_reply_array (__u16 pathid,
		  __u32 msgid, __u32 trgcls,
		  int flags1,
		  iucv_array_t * buffer,
		  ulong buflen, ulong * ipbfadr2, ulong * ipbfln2f)
{
	iparml_db *parm;
	ulong b2f0_result;

	iucv_debug(2, "entering");

	if (!buffer)
		return -EINVAL;

	parm = (iparml_db *)grab_param();

	parm->ipbfadr2 = (__u32) ((ulong) buffer);
	parm->ipbfln2f = buflen;	/* length of message */
	parm->ippathid = pathid;
	parm->ipmsgid = msgid;
	parm->iptrgcls = trgcls;
	parm->ipflags1 = (IPANSLST | flags1);

	b2f0_result = b2f0(REPLY, parm);

	if ((!b2f0_result) || (b2f0_result == 5)) {

		if (ipbfadr2)
			*ipbfadr2 = parm->ipbfadr2;
		if (ipbfln2f)
			*ipbfln2f = parm->ipbfln2f;
	}
	release_param(parm);

	iucv_debug(2, "exiting");

	return b2f0_result;
}

/*
 * Name: iucv_reply_prmmsg
 * Purpose: This function responds to the two-way messages that you
 *          receive. You must identify completely the message to
 *          which you wish to reply. ie, pathid, msgid, and trgcls.
 *          Prmmsg signifies the data is moved into the
 *          parameter list.
 * Input: pathid - path identification number
 *        msgid - specifies the message ID.
 *        trgcls - specifies target class
 *        flags1 - option for path
 *                 IPPRTY- specifies if you want to send priority message
 *        prmmsg - 8-bytes of data to be placed into the parameter
 *                 list.
 * Output: NA
 * Return: b2f0_result - return code from CP
*/
int
iucv_reply_prmmsg (__u16 pathid,
		   __u32 msgid, __u32 trgcls, int flags1, __u8 prmmsg[8])
{
	iparml_dpl *parm;
	ulong b2f0_result;

	iucv_debug(2, "entering");

	parm = (iparml_dpl *)grab_param();

	parm->ippathid = pathid;
	parm->ipmsgid = msgid;
	parm->iptrgcls = trgcls;
	memcpy(parm->iprmmsg, prmmsg, sizeof (parm->iprmmsg));
	parm->ipflags1 = (IPRMDATA | flags1);

	b2f0_result = b2f0(REPLY, parm);
	release_param(parm);

	iucv_debug(2, "exiting");

	return b2f0_result;
}

/**
 * iucv_resume:
 * @pathid:    Path identification number
 * @user_data: 16-byte of user data
 *
 * This function restores communication over a quiesced path.
 * Returns: return code from CP
 */
int
iucv_resume (__u16 pathid, __u8 user_data[16])
{
	iparml_control *parm;
	ulong b2f0_result = 0;

	iucv_debug(1, "entering");
	iucv_debug(1, "pathid = %d", pathid);

	parm = (iparml_control *)grab_param();

	memcpy (parm->ipuser, user_data, sizeof (*user_data));
	parm->ippathid = pathid;

	b2f0_result = b2f0(RESUME, parm);
	release_param(parm);

	iucv_debug(1, "exiting");

	return b2f0_result;
}

/*
 * Name: iucv_send
 * Purpose: sends messages
 * Input: pathid - ushort, pathid
 *        msgid  - ulong *, id of message returned to caller
 *        trgcls - ulong, target message class
 *        srccls - ulong, source message class
 *        msgtag - ulong, message tag
 *	  flags1  - Contains options for this path.
 *		IPPRTY - Ox20 - specifies if you want to send a priority message.
 *        buffer - pointer to buffer
 *        buflen - ulong, length of buffer
 * Output: b2f0_result - return code from b2f0 call
 *         msgid - returns message id
 */
int
iucv_send (__u16 pathid, __u32 * msgid,
	   __u32 trgcls, __u32 srccls,
	   __u32 msgtag, int flags1, void *buffer, ulong buflen)
{
	iparml_db *parm;
	ulong b2f0_result;

	iucv_debug(2, "entering");

	if (!buffer)
		return -EINVAL;

	parm = (iparml_db *)grab_param();

	parm->ipbfadr1 = (__u32) ((ulong) buffer);
	parm->ippathid = pathid;
	parm->iptrgcls = trgcls;
	parm->ipbfln1f = (__u32) buflen;	/* length of message */
	parm->ipsrccls = srccls;
	parm->ipmsgtag = msgtag;
	parm->ipflags1 = (IPNORPY | flags1);	/* one way priority message */

	b2f0_result = b2f0(SEND, parm);

	if ((!b2f0_result) && (msgid))
		*msgid = parm->ipmsgid;
	release_param(parm);

	iucv_debug(2, "exiting");

	return b2f0_result;
}

/*
 * Name: iucv_send_array
 * Purpose: This function transmits data to another application.
 *          The contents of buffer is the address of the array of
 *          addresses and lengths of discontiguous buffers that hold
 *          the message text. This is a one-way message and the
 *          receiver will not reply to the message.
 * Input: pathid - path identification number
 *        trgcls - specifies target class
 *        srccls - specifies the source message class
 *        msgtag - specifies a tag to be associated witht the message
 *        flags1 - option for path
 *                 IPPRTY- specifies if you want to send priority message
 *        buffer - address of array of send buffers
 *        buflen - total length of send buffers
 * Output: msgid - specifies the message ID.
 * Return: b2f0_result - return code from CP
 *         (-EINVAL) - buffer address is NULL
 */
int
iucv_send_array (__u16 pathid,
		 __u32 * msgid,
		 __u32 trgcls,
		 __u32 srccls,
		 __u32 msgtag, int flags1, iucv_array_t * buffer, ulong buflen)
{
	iparml_db *parm;
	ulong b2f0_result;

	iucv_debug(2, "entering");

	if (!buffer)
		return -EINVAL;

	parm = (iparml_db *)grab_param();

	parm->ippathid = pathid;
	parm->iptrgcls = trgcls;
	parm->ipbfadr1 = (__u32) ((ulong) buffer);
	parm->ipbfln1f = (__u32) buflen;	/* length of message */
	parm->ipsrccls = srccls;
	parm->ipmsgtag = msgtag;
	parm->ipflags1 = (IPNORPY | IPBUFLST | flags1);
	b2f0_result = b2f0(SEND, parm);

	if ((!b2f0_result) && (msgid))
		*msgid = parm->ipmsgid;
	release_param(parm);

	iucv_debug(2, "exiting");
	return b2f0_result;
}

/*
 * Name: iucv_send_prmmsg
 * Purpose: This function transmits data to another application.
 *          Prmmsg specifies that the 8-bytes of data are to be moved
 *          into the parameter list. This is a one-way message and the
 *          receiver will not reply to the message.
 * Input: pathid - path identification number
 *        trgcls - specifies target class
 *        srccls - specifies the source message class
 *        msgtag - specifies a tag to be associated with the message
 *        flags1 - option for path
 *                 IPPRTY- specifies if you want to send priority message
 *        prmmsg - 8-bytes of data to be placed into parameter list
 * Output: msgid - specifies the message ID.
 * Return: b2f0_result - return code from CP
*/
int
iucv_send_prmmsg (__u16 pathid,
		  __u32 * msgid,
		  __u32 trgcls,
		  __u32 srccls, __u32 msgtag, int flags1, __u8 prmmsg[8])
{
	iparml_dpl *parm;
	ulong b2f0_result;

	iucv_debug(2, "entering");

	parm = (iparml_dpl *)grab_param();

	parm->ippathid = pathid;
	parm->iptrgcls = trgcls;
	parm->ipsrccls = srccls;
	parm->ipmsgtag = msgtag;
	parm->ipflags1 = (IPRMDATA | IPNORPY | flags1);
	memcpy(parm->iprmmsg, prmmsg, sizeof(parm->iprmmsg));

	b2f0_result = b2f0(SEND, parm);

	if ((!b2f0_result) && (msgid))
		*msgid = parm->ipmsgid;
	release_param(parm);

	iucv_debug(2, "exiting");

	return b2f0_result;
}

/*
 * Name: iucv_send2way
 * Purpose: This function transmits data to another application.
 *          Data to be transmitted is in a buffer. The receiver
 *          of the send is expected to reply to the message and
 *          a buffer is provided into which IUCV moves the reply
 *          to this message.
 * Input: pathid - path identification number
 *        trgcls - specifies target class
 *        srccls - specifies the source message class
 *        msgtag - specifies a tag associated with the message
 *        flags1 - option for path
 *                 IPPRTY- specifies if you want to send priority message
 *        buffer - address of send buffer
 *        buflen - length of send buffer
 *        ansbuf - address of buffer to reply with
 *        anslen - length of buffer to reply with
 * Output: msgid - specifies the message ID.
 * Return: b2f0_result - return code from CP
 *         (-EINVAL) - buffer or ansbuf address is NULL
 */
int
iucv_send2way (__u16 pathid,
	       __u32 * msgid,
	       __u32 trgcls,
	       __u32 srccls,
	       __u32 msgtag,
	       int flags1,
	       void *buffer, ulong buflen, void *ansbuf, ulong anslen)
{
	iparml_db *parm;
	ulong b2f0_result;

	iucv_debug(2, "entering");

	if (!buffer || !ansbuf)
		return -EINVAL;

	parm = (iparml_db *)grab_param();

	parm->ippathid = pathid;
	parm->iptrgcls = trgcls;
	parm->ipbfadr1 = (__u32) ((ulong) buffer);
	parm->ipbfln1f = (__u32) buflen;	/* length of message */
	parm->ipbfadr2 = (__u32) ((ulong) ansbuf);
	parm->ipbfln2f = (__u32) anslen;
	parm->ipsrccls = srccls;
	parm->ipmsgtag = msgtag;
	parm->ipflags1 = flags1;	/* priority message */

	b2f0_result = b2f0(SEND, parm);

	if ((!b2f0_result) && (msgid))
		*msgid = parm->ipmsgid;
	release_param(parm);

	iucv_debug(2, "exiting");

	return b2f0_result;
}

/*
 * Name: iucv_send2way_array
 * Purpose: This function transmits data to another application.
 *          The contents of buffer is the address of the array of
 *          addresses and lengths of discontiguous buffers that hold
 *          the message text. The receiver of the send is expected to
 *          reply to the message and a buffer is provided into which
 *          IUCV moves the reply to this message.
 * Input: pathid - path identification number
 *        trgcls - specifies target class
 *        srccls - specifies the source message class
 *        msgtag - spcifies a tag to be associated with the message
 *        flags1 - option for path
 *                 IPPRTY- specifies if you want to send priority message
 *        buffer - address of array of send buffers
 *        buflen - total length of send buffers
 *        ansbuf - address of buffer to reply with
 *        anslen - length of buffer to reply with
 * Output: msgid - specifies the message ID.
 * Return: b2f0_result - return code from CP
 *         (-EINVAL) - buffer address is NULL
 */
int
iucv_send2way_array (__u16 pathid,
		     __u32 * msgid,
		     __u32 trgcls,
		     __u32 srccls,
		     __u32 msgtag,
		     int flags1,
		     iucv_array_t * buffer,
		     ulong buflen, iucv_array_t * ansbuf, ulong anslen)
{
	iparml_db *parm;
	ulong b2f0_result;

	iucv_debug(2, "entering");

	if (!buffer || !ansbuf)
		return -EINVAL;

	parm = (iparml_db *)grab_param();

	parm->ippathid = pathid;
	parm->iptrgcls = trgcls;
	parm->ipbfadr1 = (__u32) ((ulong) buffer);
	parm->ipbfln1f = (__u32) buflen;	/* length of message */
	parm->ipbfadr2 = (__u32) ((ulong) ansbuf);
	parm->ipbfln2f = (__u32) anslen;
	parm->ipsrccls = srccls;
	parm->ipmsgtag = msgtag;
	parm->ipflags1 = (IPBUFLST | IPANSLST | flags1);
	b2f0_result = b2f0(SEND, parm);
	if ((!b2f0_result) && (msgid))
		*msgid = parm->ipmsgid;
	release_param(parm);

	iucv_debug(2, "exiting");
	return b2f0_result;
}

/*
 * Name: iucv_send2way_prmmsg
 * Purpose: This function transmits data to another application.
 *          Prmmsg specifies that the 8-bytes of data are to be moved
 *          into the parameter list. This is a two-way message and the
 *          receiver of the message is expected to reply. A buffer
 *          is provided into which IUCV moves the reply to this
 *          message.
 * Input: pathid - path identification number
 *        trgcls - specifies target class
 *        srccls - specifies the source message class
 *        msgtag - specifies a tag to be associated with the message
 *        flags1 - option for path
 *                 IPPRTY- specifies if you want to send priority message
 *        prmmsg - 8-bytes of data to be placed in parameter list
 *        ansbuf - address of buffer to reply with
 *        anslen - length of buffer to reply with
 * Output: msgid - specifies the message ID.
 * Return: b2f0_result - return code from CP
 *         (-EINVAL) - buffer address is NULL
*/
int
iucv_send2way_prmmsg (__u16 pathid,
		      __u32 * msgid,
		      __u32 trgcls,
		      __u32 srccls,
		      __u32 msgtag,
		      ulong flags1, __u8 prmmsg[8], void *ansbuf, ulong anslen)
{
	iparml_dpl *parm;
	ulong b2f0_result;

	iucv_debug(2, "entering");

	if (!ansbuf)
		return -EINVAL;

	parm = (iparml_dpl *)grab_param();

	parm->ippathid = pathid;
	parm->iptrgcls = trgcls;
	parm->ipsrccls = srccls;
	parm->ipmsgtag = msgtag;
	parm->ipbfadr2 = (__u32) ((ulong) ansbuf);
	parm->ipbfln2f = (__u32) anslen;
	parm->ipflags1 = (IPRMDATA | flags1);	/* message in prmlist */
	memcpy(parm->iprmmsg, prmmsg, sizeof(parm->iprmmsg));

	b2f0_result = b2f0(SEND, parm);

	if ((!b2f0_result) && (msgid))
		*msgid = parm->ipmsgid;
	release_param(parm);

	iucv_debug(2, "exiting");

	return b2f0_result;
}

/*
 * Name: iucv_send2way_prmmsg_array
 * Purpose: This function transmits data to another application.
 *          Prmmsg specifies that the 8-bytes of data are to be moved
 *          into the parameter list. This is a two-way message and the
 *          receiver of the message is expected to reply. A buffer
 *          is provided into which IUCV moves the reply to this
 *          message. The contents of ansbuf is the address of the
 *          array of addresses and lengths of discontiguous buffers
 *          that contain the reply.
 * Input: pathid - path identification number
 *        trgcls - specifies target class
 *        srccls - specifies the source message class
 *        msgtag - specifies a tag to be associated with the message
 *        flags1 - option for path
 *                 IPPRTY- specifies if you want to send priority message
 *        prmmsg - 8-bytes of data to be placed into the parameter list
 *        ansbuf - address of buffer to reply with
 *        anslen - length of buffer to reply with
 * Output: msgid - specifies the message ID.
 * Return: b2f0_result - return code from CP
 *         (-EINVAL) - ansbuf address is NULL
 */
int
iucv_send2way_prmmsg_array (__u16 pathid,
			    __u32 * msgid,
			    __u32 trgcls,
			    __u32 srccls,
			    __u32 msgtag,
			    int flags1,
			    __u8 prmmsg[8],
			    iucv_array_t * ansbuf, ulong anslen)
{
	iparml_dpl *parm;
	ulong b2f0_result;

	iucv_debug(2, "entering");

	if (!ansbuf)
		return -EINVAL;

	parm = (iparml_dpl *)grab_param();

	parm->ippathid = pathid;
	parm->iptrgcls = trgcls;
	parm->ipsrccls = srccls;
	parm->ipmsgtag = msgtag;
	parm->ipbfadr2 = (__u32) ((ulong) ansbuf);
	parm->ipbfln2f = (__u32) anslen;
	parm->ipflags1 = (IPRMDATA | IPANSLST | flags1);
	memcpy(parm->iprmmsg, prmmsg, sizeof(parm->iprmmsg));
	b2f0_result = b2f0(SEND, parm);
	if ((!b2f0_result) && (msgid))
		*msgid = parm->ipmsgid;
	release_param(parm);

	iucv_debug(2, "exiting");
	return b2f0_result;
}

void
iucv_setmask_cpuid (void *result)
{
        iparml_set_mask *parm;

        iucv_debug(1, "entering");
        parm = (iparml_set_mask *)grab_param();
        parm->ipmask = *((__u8*)result);
        *((ulong *)result) = b2f0(SETMASK, parm);
        release_param(parm);

        iucv_debug(1, "b2f0_result = %ld", *((ulong *)result));
        iucv_debug(1, "exiting");
}

/*
 * Name: iucv_setmask
 * Purpose: This function enables or disables the following IUCV
 *          external interruptions: Nonpriority and priority message
 *          interrupts, nonpriority and priority reply interrupts.
 * Input: SetMaskFlag - options for interrupts
 *           0x80 - Nonpriority_MessagePendingInterruptsFlag
 *           0x40 - Priority_MessagePendingInterruptsFlag
 *           0x20 - Nonpriority_MessageCompletionInterruptsFlag
 *           0x10 - Priority_MessageCompletionInterruptsFlag
 *           0x08 - IUCVControlInterruptsFlag
 * Output: NA
 * Return: b2f0_result - return code from CP
*/
int
iucv_setmask (int SetMaskFlag)
{
	union {
		ulong result;
		__u8  param;
	} u;
	int cpu;

	u.param = SetMaskFlag;
	cpu = get_cpu();
	smp_call_function_on(iucv_setmask_cpuid, &u, 0, 1, iucv_cpuid);
	put_cpu();

	return u.result;
}

/**
 * iucv_sever:
 * @pathid:    Path identification number
 * @user_data: 16-byte of user data
 *
 * This function terminates an iucv path.
 * Returns: return code from CP
 */
int
iucv_sever(__u16 pathid, __u8 user_data[16])
{
	iparml_control *parm;
	ulong b2f0_result = 0;

	iucv_debug(1, "entering");
	parm = (iparml_control *)grab_param();

	memcpy(parm->ipuser, user_data, sizeof(parm->ipuser));
	parm->ippathid = pathid;

	b2f0_result = b2f0(SEVER, parm);

	if (!b2f0_result)
		iucv_remove_pathid(pathid);
	release_param(parm);

	iucv_debug(1, "exiting");
	return b2f0_result;
}

/*
 * Interrupt Handlers
 *******************************************************************************/

/**
 * iucv_irq_handler:
 * @regs: Current registers
 * @code: irq code
 *
 * Handles external interrupts coming in from CP.
 * Places the interrupt buffer on a queue and schedules iucv_tasklet_handler().
 */
static void
iucv_irq_handler(struct pt_regs *regs, __u16 code)
{
	iucv_irqdata *irqdata;

	irqdata = kmalloc(sizeof(iucv_irqdata), GFP_ATOMIC);
	if (!irqdata) {
		printk(KERN_WARNING "%s: out of memory\n", __FUNCTION__);
		return;
	}

	memcpy(&irqdata->data, iucv_external_int_buffer,
	       sizeof(iucv_GeneralInterrupt));

	spin_lock(&iucv_irq_queue_lock);
	list_add_tail(&irqdata->queue, &iucv_irq_queue);
	spin_unlock(&iucv_irq_queue_lock);

	tasklet_schedule(&iucv_tasklet);
}

/**
 * iucv_do_int:
 * @int_buf: Pointer to copy of external interrupt buffer
 *
 * The workhorse for handling interrupts queued by iucv_irq_handler().
 * This function is called from the bottom half iucv_tasklet_handler().
 */
static void
iucv_do_int(iucv_GeneralInterrupt * int_buf)
{
	handler *h = NULL;
	struct list_head *lh;
	ulong flags;
	iucv_interrupt_ops_t *interrupt = NULL;	/* interrupt addresses */
	__u8 temp_buff1[24], temp_buff2[24];	/* masked handler id. */
	int rc = 0, j = 0;
	__u8 no_listener[16] = "NO LISTENER";

	iucv_debug(2, "entering, pathid %d, type %02X",
		 int_buf->ippathid, int_buf->iptype);
	iucv_dumpit("External Interrupt Buffer:",
		    int_buf, sizeof(iucv_GeneralInterrupt));

	ASCEBC (no_listener, 16);

	if (int_buf->iptype != 01) {
		if ((int_buf->ippathid) > (max_connections - 1)) {
			printk(KERN_WARNING "%s: Got interrupt with pathid %d"
			       " > max_connections (%ld)\n", __FUNCTION__,
			       int_buf->ippathid, max_connections - 1);
		} else {
			h = iucv_pathid_table[int_buf->ippathid];
			interrupt = h->interrupt_table;
			iucv_dumpit("Handler:", h, sizeof(handler));
		}
	}

	/* end of if statement */
	switch (int_buf->iptype) {
		case 0x01:		/* connection pending */
			if (messagesDisabled) {
			    iucv_setmask(~0);
			    messagesDisabled = 0;
			}
			spin_lock_irqsave(&iucv_lock, flags);
			list_for_each(lh, &iucv_handler_table) {
				h = list_entry(lh, handler, list);
				memcpy(temp_buff1, &(int_buf->ipvmid), 24);
				memcpy(temp_buff2, &(h->id.userid), 24);
				for (j = 0; j < 24; j++) {
					temp_buff1[j] &= (h->id.mask)[j];
					temp_buff2[j] &= (h->id.mask)[j];
				}
				
				iucv_dumpit("temp_buff1:",
					    temp_buff1, sizeof(temp_buff1));
				iucv_dumpit("temp_buff2",
					    temp_buff2, sizeof(temp_buff2));
				
				if (!memcmp (temp_buff1, temp_buff2, 24)) {
					
					iucv_debug(2,
						   "found a matching handler");
					break;
				} else
					h = NULL;
			}
			spin_unlock_irqrestore (&iucv_lock, flags);
			if (h) {
				/* ADD PATH TO PATHID TABLE */
				rc = iucv_add_pathid(int_buf->ippathid, h);
				if (rc) {
					iucv_sever (int_buf->ippathid,
						    no_listener);
					iucv_debug(1,
						   "add_pathid failed, rc = %d",
						   rc);
				} else {
					interrupt = h->interrupt_table;
					if (interrupt->ConnectionPending) {
						EBCASC (int_buf->ipvmid, 8);
						interrupt->ConnectionPending(
							(iucv_ConnectionPending *)int_buf,
							h->pgm_data);
					} else
						iucv_sever(int_buf->ippathid,
							   no_listener);
				}
			} else
				iucv_sever(int_buf->ippathid, no_listener);
			break;
			
		case 0x02:		/*connection complete */
			if (messagesDisabled) {
			    iucv_setmask(~0);
			    messagesDisabled = 0;
			}
			if (h) {
				if (interrupt->ConnectionComplete)
				{
					interrupt->ConnectionComplete(
						(iucv_ConnectionComplete *)int_buf,
						h->pgm_data);
				}
				else
					iucv_debug(1,
						   "ConnectionComplete not called");
			} else
				iucv_sever(int_buf->ippathid, no_listener);
			break;
			
		case 0x03:		/* connection severed */
			if (messagesDisabled) {
			    iucv_setmask(~0);
			    messagesDisabled = 0;
			}
			if (h) {
				if (interrupt->ConnectionSevered)
					interrupt->ConnectionSevered(
						(iucv_ConnectionSevered *)int_buf,
						h->pgm_data);
				
				else
					iucv_sever (int_buf->ippathid, no_listener);
			} else
				iucv_sever(int_buf->ippathid, no_listener);
			break;
			
		case 0x04:		/* connection quiesced */
			if (messagesDisabled) {
			    iucv_setmask(~0);
			    messagesDisabled = 0;
			}
			if (h) {
				if (interrupt->ConnectionQuiesced)
					interrupt->ConnectionQuiesced(
						(iucv_ConnectionQuiesced *)int_buf,
						h->pgm_data);
				else
					iucv_debug(1,
						   "ConnectionQuiesced not called");
			}
			break;
			
		case 0x05:		/* connection resumed */
			if (messagesDisabled) {
			    iucv_setmask(~0);
			    messagesDisabled = 0;
			}
			if (h) {
				if (interrupt->ConnectionResumed)
					interrupt->ConnectionResumed(
						(iucv_ConnectionResumed *)int_buf,
						h->pgm_data);
				else
					iucv_debug(1,
						   "ConnectionResumed not called");
			}
			break;
			
		case 0x06:		/* priority message complete */
		case 0x07:		/* nonpriority message complete */
			if (h) {
				if (interrupt->MessageComplete)
					interrupt->MessageComplete(
						(iucv_MessageComplete *)int_buf,
						h->pgm_data);
				else
					iucv_debug(2,
						   "MessageComplete not called");
			}
			break;
			
		case 0x08:		/* priority message pending  */
		case 0x09:		/* nonpriority message pending  */
			if (h) {
				if (interrupt->MessagePending)
					interrupt->MessagePending(
						(iucv_MessagePending *) int_buf,
						h->pgm_data);
				else
					iucv_debug(2,
						   "MessagePending not called");
			}
			break;
		default:		/* unknown iucv type */
			printk(KERN_WARNING "%s: unknown iucv interrupt\n",
			       __FUNCTION__);
			break;
	}			/* end switch */
	
	iucv_debug(2, "exiting pathid %d, type %02X",
		 int_buf->ippathid, int_buf->iptype);

	return;
}

/**
 * iucv_tasklet_handler:
 *
 * This function loops over the queue of irq buffers and runs iucv_do_int()
 * on every queue element.
 */
static void
iucv_tasklet_handler(unsigned long ignored)
{
	struct list_head head;
	struct list_head *next;
	ulong  flags;

	spin_lock_irqsave(&iucv_irq_queue_lock, flags);
	list_add(&head, &iucv_irq_queue);
	list_del_init(&iucv_irq_queue);
	spin_unlock_irqrestore (&iucv_irq_queue_lock, flags);

	next = head.next;
	while (next != &head) {
		iucv_irqdata *p = list_entry(next, iucv_irqdata, queue);

		next = next->next;
		iucv_do_int(&p->data);
		kfree(p);
	}

	return;
}

subsys_initcall(iucv_init);
module_exit(iucv_exit);

/**
 * Export all public stuff
 */
EXPORT_SYMBOL (iucv_bus);
EXPORT_SYMBOL (iucv_root);
EXPORT_SYMBOL (iucv_accept);
EXPORT_SYMBOL (iucv_connect);
#if 0
EXPORT_SYMBOL (iucv_purge);
EXPORT_SYMBOL (iucv_query_maxconn);
EXPORT_SYMBOL (iucv_query_bufsize);
EXPORT_SYMBOL (iucv_quiesce);
#endif
EXPORT_SYMBOL (iucv_receive);
#if 0
EXPORT_SYMBOL (iucv_receive_array);
#endif
EXPORT_SYMBOL (iucv_reject);
#if 0
EXPORT_SYMBOL (iucv_reply);
EXPORT_SYMBOL (iucv_reply_array);
EXPORT_SYMBOL (iucv_resume);
#endif
EXPORT_SYMBOL (iucv_reply_prmmsg);
EXPORT_SYMBOL (iucv_send);
EXPORT_SYMBOL (iucv_send2way);
EXPORT_SYMBOL (iucv_send2way_array);
EXPORT_SYMBOL (iucv_send2way_prmmsg);
EXPORT_SYMBOL (iucv_send2way_prmmsg_array);
#if 0
EXPORT_SYMBOL (iucv_send_array);
EXPORT_SYMBOL (iucv_send_prmmsg);
EXPORT_SYMBOL (iucv_setmask);
#endif
EXPORT_SYMBOL (iucv_sever);
EXPORT_SYMBOL (iucv_register_program);
EXPORT_SYMBOL (iucv_unregister_program);
