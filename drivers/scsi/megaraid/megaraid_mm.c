// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *			Linux MegaRAID device driver
 *
 * Copyright (c) 2003-2004  LSI Logic Corporation.
 *
 * FILE		: megaraid_mm.c
 * Version	: v2.20.2.7 (Jul 16 2006)
 *
 * Common management module
 */
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include "megaraid_mm.h"


// Entry points for char node driver
static DEFINE_MUTEX(mraid_mm_mutex);
static int mraid_mm_open(struct inode *, struct file *);
static long mraid_mm_unlocked_ioctl(struct file *, uint, unsigned long);


// routines to convert to and from the old the format
static int mimd_to_kioc(mimd_t __user *, mraid_mmadp_t *, uioc_t *);
static int kioc_to_mimd(uioc_t *, mimd_t __user *);


// Helper functions
static int handle_drvrcmd(void __user *, uint8_t, int *);
static int lld_ioctl(mraid_mmadp_t *, uioc_t *);
static void ioctl_done(uioc_t *);
static void lld_timedout(struct timer_list *);
static void hinfo_to_cinfo(mraid_hba_info_t *, mcontroller_t *);
static mraid_mmadp_t *mraid_mm_get_adapter(mimd_t __user *, int *);
static uioc_t *mraid_mm_alloc_kioc(mraid_mmadp_t *);
static void mraid_mm_dealloc_kioc(mraid_mmadp_t *, uioc_t *);
static int mraid_mm_attach_buf(mraid_mmadp_t *, uioc_t *, int);
static int mraid_mm_setup_dma_pools(mraid_mmadp_t *);
static void mraid_mm_free_adp_resources(mraid_mmadp_t *);
static void mraid_mm_teardown_dma_pools(mraid_mmadp_t *);

MODULE_AUTHOR("LSI Logic Corporation");
MODULE_DESCRIPTION("LSI Logic Management Module");
MODULE_LICENSE("GPL");
MODULE_VERSION(LSI_COMMON_MOD_VERSION);

static int dbglevel = CL_ANN;
module_param_named(dlevel, dbglevel, int, 0);
MODULE_PARM_DESC(dlevel, "Debug level (default=0)");

EXPORT_SYMBOL(mraid_mm_register_adp);
EXPORT_SYMBOL(mraid_mm_unregister_adp);
EXPORT_SYMBOL(mraid_mm_adapter_app_handle);

static uint32_t drvr_ver	= 0x02200207;

static int adapters_count_g;
static struct list_head adapters_list_g;

static wait_queue_head_t wait_q;

static const struct file_operations lsi_fops = {
	.open	= mraid_mm_open,
	.unlocked_ioctl = mraid_mm_unlocked_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.owner	= THIS_MODULE,
	.llseek = noop_llseek,
};

static struct miscdevice megaraid_mm_dev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name   = "megadev0",
	.fops   = &lsi_fops,
};

/**
 * mraid_mm_open - open routine for char node interface
 * @inode	: unused
 * @filep	: unused
 *
 * Allow ioctl operations by apps only if they have superuser privilege.
 */
static int
mraid_mm_open(struct inode *inode, struct file *filep)
{
	/*
	 * Only allow superuser to access private ioctl interface
	 */
	if (!capable(CAP_SYS_ADMIN)) return (-EACCES);

	return 0;
}

/**
 * mraid_mm_ioctl - module entry-point for ioctls
 * @filep	: file operations pointer (ignored)
 * @cmd		: ioctl command
 * @arg		: user ioctl packet
 */
static int
mraid_mm_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	uioc_t		*kioc;
	char		signature[EXT_IOCTL_SIGN_SZ]	= {0};
	int		rval;
	mraid_mmadp_t	*adp;
	uint8_t		old_ioctl;
	int		drvrcmd_rval;
	void __user *argp = (void __user *)arg;

	/*
	 * Make sure only USCSICMD are issued through this interface.
	 * MIMD application would still fire different command.
	 */

	if ((_IOC_TYPE(cmd) != MEGAIOC_MAGIC) && (cmd != USCSICMD)) {
		return (-EINVAL);
	}

	/*
	 * Look for signature to see if this is the new or old ioctl format.
	 */
	if (copy_from_user(signature, argp, EXT_IOCTL_SIGN_SZ)) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid cmm: copy from usr addr failed\n"));
		return (-EFAULT);
	}

	if (memcmp(signature, EXT_IOCTL_SIGN, EXT_IOCTL_SIGN_SZ) == 0)
		old_ioctl = 0;
	else
		old_ioctl = 1;

	/*
	 * At present, we don't support the new ioctl packet
	 */
	if (!old_ioctl )
		return (-EINVAL);

	/*
	 * If it is a driver ioctl (as opposed to fw ioctls), then we can
	 * handle the command locally. rval > 0 means it is not a drvr cmd
	 */
	rval = handle_drvrcmd(argp, old_ioctl, &drvrcmd_rval);

	if (rval < 0)
		return rval;
	else if (rval == 0)
		return drvrcmd_rval;

	rval = 0;
	if ((adp = mraid_mm_get_adapter(argp, &rval)) == NULL) {
		return rval;
	}

	/*
	 * Check if adapter can accept ioctl. We may have marked it offline
	 * if any previous kioc had timedout on this controller.
	 */
	if (!adp->quiescent) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid cmm: controller cannot accept cmds due to "
			"earlier errors\n" ));
		return -EFAULT;
	}

	/*
	 * The following call will block till a kioc is available
	 * or return NULL if the list head is empty for the pointer
	 * of type mraid_mmapt passed to mraid_mm_alloc_kioc
	 */
	kioc = mraid_mm_alloc_kioc(adp);
	if (!kioc)
		return -ENXIO;

	/*
	 * User sent the old mimd_t ioctl packet. Convert it to uioc_t.
	 */
	if ((rval = mimd_to_kioc(argp, adp, kioc))) {
		mraid_mm_dealloc_kioc(adp, kioc);
		return rval;
	}

	kioc->done = ioctl_done;

	/*
	 * Issue the IOCTL to the low level driver. After the IOCTL completes
	 * release the kioc if and only if it was _not_ timedout. If it was
	 * timedout, that means that resources are still with low level driver.
	 */
	if ((rval = lld_ioctl(adp, kioc))) {

		if (!kioc->timedout)
			mraid_mm_dealloc_kioc(adp, kioc);

		return rval;
	}

	/*
	 * Convert the kioc back to user space
	 */
	rval = kioc_to_mimd(kioc, argp);

	/*
	 * Return the kioc to free pool
	 */
	mraid_mm_dealloc_kioc(adp, kioc);

	return rval;
}

static long
mraid_mm_unlocked_ioctl(struct file *filep, unsigned int cmd,
		        unsigned long arg)
{
	int err;

	mutex_lock(&mraid_mm_mutex);
	err = mraid_mm_ioctl(filep, cmd, arg);
	mutex_unlock(&mraid_mm_mutex);

	return err;
}

/**
 * mraid_mm_get_adapter - Returns corresponding adapters for the mimd packet
 * @umimd	: User space mimd_t ioctl packet
 * @rval	: returned success/error status
 *
 * The function return value is a pointer to the located @adapter.
 */
static mraid_mmadp_t *
mraid_mm_get_adapter(mimd_t __user *umimd, int *rval)
{
	mraid_mmadp_t	*adapter;
	mimd_t		mimd;
	uint32_t	adapno;
	int		iterator;
	bool		is_found;

	if (copy_from_user(&mimd, umimd, sizeof(mimd_t))) {
		*rval = -EFAULT;
		return NULL;
	}

	adapno = GETADAP(mimd.ui.fcs.adapno);

	if (adapno >= adapters_count_g) {
		*rval = -ENODEV;
		return NULL;
	}

	adapter = NULL;
	iterator = 0;
	is_found = false;

	list_for_each_entry(adapter, &adapters_list_g, list) {
		if (iterator++ == adapno) {
			is_found = true;
			break;
		}
	}

	if (!is_found) {
		*rval = -ENODEV;
		return NULL;
	}

	return adapter;
}

/**
 * handle_drvrcmd - Checks if the opcode is a driver cmd and if it is, handles it.
 * @arg		: packet sent by the user app
 * @old_ioctl	: mimd if 1; uioc otherwise
 * @rval	: pointer for command's returned value (not function status)
 */
static int
handle_drvrcmd(void __user *arg, uint8_t old_ioctl, int *rval)
{
	mimd_t		__user *umimd;
	mimd_t		kmimd;
	uint8_t		opcode;
	uint8_t		subopcode;

	if (old_ioctl)
		goto old_packet;
	else
		goto new_packet;

new_packet:
	return (-ENOTSUPP);

old_packet:
	*rval = 0;
	umimd = arg;

	if (copy_from_user(&kmimd, umimd, sizeof(mimd_t)))
		return (-EFAULT);

	opcode		= kmimd.ui.fcs.opcode;
	subopcode	= kmimd.ui.fcs.subopcode;

	/*
	 * If the opcode is 0x82 and the subopcode is either GET_DRVRVER or
	 * GET_NUMADP, then we can handle. Otherwise we should return 1 to
	 * indicate that we cannot handle this.
	 */
	if (opcode != 0x82)
		return 1;

	switch (subopcode) {

	case MEGAIOC_QDRVRVER:

		if (copy_to_user(kmimd.data, &drvr_ver, sizeof(uint32_t)))
			return (-EFAULT);

		return 0;

	case MEGAIOC_QNADAP:

		*rval = adapters_count_g;

		if (copy_to_user(kmimd.data, &adapters_count_g,
				sizeof(uint32_t)))
			return (-EFAULT);

		return 0;

	default:
		/* cannot handle */
		return 1;
	}

	return 0;
}


/**
 * mimd_to_kioc	- Converter from old to new ioctl format
 * @umimd	: user space old MIMD IOCTL
 * @adp		: adapter softstate
 * @kioc	: kernel space new format IOCTL
 *
 * Routine to convert MIMD interface IOCTL to new interface IOCTL packet. The
 * new packet is in kernel space so that driver can perform operations on it
 * freely.
 */

static int
mimd_to_kioc(mimd_t __user *umimd, mraid_mmadp_t *adp, uioc_t *kioc)
{
	mbox64_t		*mbox64;
	mbox_t			*mbox;
	mraid_passthru_t	*pthru32;
	uint32_t		adapno;
	uint8_t			opcode;
	uint8_t			subopcode;
	mimd_t			mimd;

	if (copy_from_user(&mimd, umimd, sizeof(mimd_t)))
		return (-EFAULT);

	/*
	 * Applications are not allowed to send extd pthru
	 */
	if ((mimd.mbox[0] == MBOXCMD_PASSTHRU64) ||
			(mimd.mbox[0] == MBOXCMD_EXTPTHRU))
		return (-EINVAL);

	opcode		= mimd.ui.fcs.opcode;
	subopcode	= mimd.ui.fcs.subopcode;
	adapno		= GETADAP(mimd.ui.fcs.adapno);

	if (adapno >= adapters_count_g)
		return (-ENODEV);

	kioc->adapno	= adapno;
	kioc->mb_type	= MBOX_LEGACY;
	kioc->app_type	= APPTYPE_MIMD;

	switch (opcode) {

	case 0x82:

		if (subopcode == MEGAIOC_QADAPINFO) {

			kioc->opcode	= GET_ADAP_INFO;
			kioc->data_dir	= UIOC_RD;
			kioc->xferlen	= sizeof(mraid_hba_info_t);

			if (mraid_mm_attach_buf(adp, kioc, kioc->xferlen))
				return (-ENOMEM);
		}
		else {
			con_log(CL_ANN, (KERN_WARNING
					"megaraid cmm: Invalid subop\n"));
			return (-EINVAL);
		}

		break;

	case 0x81:

		kioc->opcode		= MBOX_CMD;
		kioc->xferlen		= mimd.ui.fcs.length;
		kioc->user_data_len	= kioc->xferlen;
		kioc->user_data		= mimd.ui.fcs.buffer;

		if (mraid_mm_attach_buf(adp, kioc, kioc->xferlen))
			return (-ENOMEM);

		if (mimd.outlen) kioc->data_dir  = UIOC_RD;
		if (mimd.inlen) kioc->data_dir |= UIOC_WR;

		break;

	case 0x80:

		kioc->opcode		= MBOX_CMD;
		kioc->xferlen		= (mimd.outlen > mimd.inlen) ?
						mimd.outlen : mimd.inlen;
		kioc->user_data_len	= kioc->xferlen;
		kioc->user_data		= mimd.data;

		if (mraid_mm_attach_buf(adp, kioc, kioc->xferlen))
			return (-ENOMEM);

		if (mimd.outlen) kioc->data_dir  = UIOC_RD;
		if (mimd.inlen) kioc->data_dir |= UIOC_WR;

		break;

	default:
		return (-EINVAL);
	}

	/*
	 * If driver command, nothing else to do
	 */
	if (opcode == 0x82)
		return 0;

	/*
	 * This is a mailbox cmd; copy the mailbox from mimd
	 */
	mbox64	= (mbox64_t *)((unsigned long)kioc->cmdbuf);
	mbox	= &mbox64->mbox32;
	memcpy(mbox, mimd.mbox, 14);

	if (mbox->cmd != MBOXCMD_PASSTHRU) {	// regular DCMD

		mbox->xferaddr	= (uint32_t)kioc->buf_paddr;

		if (kioc->data_dir & UIOC_WR) {
			if (copy_from_user(kioc->buf_vaddr, kioc->user_data,
							kioc->xferlen)) {
				return (-EFAULT);
			}
		}

		return 0;
	}

	/*
	 * This is a regular 32-bit pthru cmd; mbox points to pthru struct.
	 * Just like in above case, the beginning for memblk is treated as
	 * a mailbox. The passthru will begin at next 1K boundary. And the
	 * data will start 1K after that.
	 */
	pthru32			= kioc->pthru32;
	kioc->user_pthru	= &umimd->pthru;
	mbox->xferaddr		= (uint32_t)kioc->pthru32_h;

	if (copy_from_user(pthru32, kioc->user_pthru,
			sizeof(mraid_passthru_t))) {
		return (-EFAULT);
	}

	pthru32->dataxferaddr	= kioc->buf_paddr;
	if (kioc->data_dir & UIOC_WR) {
		if (pthru32->dataxferlen > kioc->xferlen)
			return -EINVAL;
		if (copy_from_user(kioc->buf_vaddr, kioc->user_data,
						pthru32->dataxferlen)) {
			return (-EFAULT);
		}
	}

	return 0;
}

/**
 * mraid_mm_attach_buf - Attach a free dma buffer for required size
 * @adp		: Adapter softstate
 * @kioc	: kioc that the buffer needs to be attached to
 * @xferlen	: required length for buffer
 *
 * First we search for a pool with smallest buffer that is >= @xferlen. If
 * that pool has no free buffer, we will try for the next bigger size. If none
 * is available, we will try to allocate the smallest buffer that is >=
 * @xferlen and attach it the pool.
 */
static int
mraid_mm_attach_buf(mraid_mmadp_t *adp, uioc_t *kioc, int xferlen)
{
	mm_dmapool_t	*pool;
	int		right_pool = -1;
	unsigned long	flags;
	int		i;

	kioc->pool_index	= -1;
	kioc->buf_vaddr		= NULL;
	kioc->buf_paddr		= 0;
	kioc->free_buf		= 0;

	/*
	 * We need xferlen amount of memory. See if we can get it from our
	 * dma pools. If we don't get exact size, we will try bigger buffer
	 */

	for (i = 0; i < MAX_DMA_POOLS; i++) {

		pool = &adp->dma_pool_list[i];

		if (xferlen > pool->buf_size)
			continue;

		if (right_pool == -1)
			right_pool = i;

		spin_lock_irqsave(&pool->lock, flags);

		if (!pool->in_use) {

			pool->in_use		= 1;
			kioc->pool_index	= i;
			kioc->buf_vaddr		= pool->vaddr;
			kioc->buf_paddr		= pool->paddr;

			spin_unlock_irqrestore(&pool->lock, flags);
			return 0;
		}
		else {
			spin_unlock_irqrestore(&pool->lock, flags);
			continue;
		}
	}

	/*
	 * If xferlen doesn't match any of our pools, return error
	 */
	if (right_pool == -1)
		return -EINVAL;

	/*
	 * We did not get any buffer from the preallocated pool. Let us try
	 * to allocate one new buffer. NOTE: This is a blocking call.
	 */
	pool = &adp->dma_pool_list[right_pool];

	spin_lock_irqsave(&pool->lock, flags);

	kioc->pool_index	= right_pool;
	kioc->free_buf		= 1;
	kioc->buf_vaddr		= dma_pool_alloc(pool->handle, GFP_ATOMIC,
							&kioc->buf_paddr);
	spin_unlock_irqrestore(&pool->lock, flags);

	if (!kioc->buf_vaddr)
		return -ENOMEM;

	return 0;
}

/**
 * mraid_mm_alloc_kioc - Returns a uioc_t from free list
 * @adp	: Adapter softstate for this module
 *
 * The kioc_semaphore is initialized with number of kioc nodes in the
 * free kioc pool. If the kioc pool is empty, this function blocks till
 * a kioc becomes free.
 */
static uioc_t *
mraid_mm_alloc_kioc(mraid_mmadp_t *adp)
{
	uioc_t			*kioc;
	struct list_head*	head;
	unsigned long		flags;

	down(&adp->kioc_semaphore);

	spin_lock_irqsave(&adp->kioc_pool_lock, flags);

	head = &adp->kioc_pool;

	if (list_empty(head)) {
		up(&adp->kioc_semaphore);
		spin_unlock_irqrestore(&adp->kioc_pool_lock, flags);

		con_log(CL_ANN, ("megaraid cmm: kioc list empty!\n"));
		return NULL;
	}

	kioc = list_entry(head->next, uioc_t, list);
	list_del_init(&kioc->list);

	spin_unlock_irqrestore(&adp->kioc_pool_lock, flags);

	memset((caddr_t)(unsigned long)kioc->cmdbuf, 0, sizeof(mbox64_t));
	memset((caddr_t) kioc->pthru32, 0, sizeof(mraid_passthru_t));

	kioc->buf_vaddr		= NULL;
	kioc->buf_paddr		= 0;
	kioc->pool_index	=-1;
	kioc->free_buf		= 0;
	kioc->user_data		= NULL;
	kioc->user_data_len	= 0;
	kioc->user_pthru	= NULL;
	kioc->timedout		= 0;

	return kioc;
}

/**
 * mraid_mm_dealloc_kioc - Return kioc to free pool
 * @adp		: Adapter softstate
 * @kioc	: uioc_t node to be returned to free pool
 */
static void
mraid_mm_dealloc_kioc(mraid_mmadp_t *adp, uioc_t *kioc)
{
	mm_dmapool_t	*pool;
	unsigned long	flags;

	if (kioc->pool_index != -1) {
		pool = &adp->dma_pool_list[kioc->pool_index];

		/* This routine may be called in non-isr context also */
		spin_lock_irqsave(&pool->lock, flags);

		/*
		 * While attaching the dma buffer, if we didn't get the 
		 * required buffer from the pool, we would have allocated 
		 * it at the run time and set the free_buf flag. We must 
		 * free that buffer. Otherwise, just mark that the buffer is 
		 * not in use
		 */
		if (kioc->free_buf == 1)
			dma_pool_free(pool->handle, kioc->buf_vaddr, 
							kioc->buf_paddr);
		else
			pool->in_use = 0;

		spin_unlock_irqrestore(&pool->lock, flags);
	}

	/* Return the kioc to the free pool */
	spin_lock_irqsave(&adp->kioc_pool_lock, flags);
	list_add(&kioc->list, &adp->kioc_pool);
	spin_unlock_irqrestore(&adp->kioc_pool_lock, flags);

	/* increment the free kioc count */
	up(&adp->kioc_semaphore);

	return;
}

/**
 * lld_ioctl - Routine to issue ioctl to low level drvr
 * @adp		: The adapter handle
 * @kioc	: The ioctl packet with kernel addresses
 */
static int
lld_ioctl(mraid_mmadp_t *adp, uioc_t *kioc)
{
	int			rval;
	struct uioc_timeout	timeout = { };

	kioc->status	= -ENODATA;
	rval		= adp->issue_uioc(adp->drvr_data, kioc, IOCTL_ISSUE);

	if (rval) return rval;

	/*
	 * Start the timer
	 */
	if (adp->timeout > 0) {
		timeout.uioc = kioc;
		timer_setup_on_stack(&timeout.timer, lld_timedout, 0);

		timeout.timer.expires	= jiffies + adp->timeout * HZ;

		add_timer(&timeout.timer);
	}

	/*
	 * Wait till the low level driver completes the ioctl. After this
	 * call, the ioctl either completed successfully or timedout.
	 */
	wait_event(wait_q, (kioc->status != -ENODATA));
	if (timeout.timer.function) {
		del_timer_sync(&timeout.timer);
		destroy_timer_on_stack(&timeout.timer);
	}

	/*
	 * If the command had timedout, we mark the controller offline
	 * before returning
	 */
	if (kioc->timedout) {
		adp->quiescent = 0;
	}

	return kioc->status;
}


/**
 * ioctl_done - callback from the low level driver
 * @kioc	: completed ioctl packet
 */
static void
ioctl_done(uioc_t *kioc)
{
	uint32_t	adapno;
	int		iterator;
	mraid_mmadp_t*	adapter;
	bool		is_found;

	/*
	 * When the kioc returns from driver, make sure it still doesn't
	 * have ENODATA in status. Otherwise, driver will hang on wait_event
	 * forever
	 */
	if (kioc->status == -ENODATA) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid cmm: lld didn't change status!\n"));

		kioc->status = -EINVAL;
	}

	/*
	 * Check if this kioc was timedout before. If so, nobody is waiting
	 * on this kioc. We don't have to wake up anybody. Instead, we just
	 * have to free the kioc
	 */
	if (kioc->timedout) {
		iterator	= 0;
		adapter		= NULL;
		adapno		= kioc->adapno;
		is_found	= false;

		con_log(CL_ANN, ( KERN_WARNING "megaraid cmm: completed "
					"ioctl that was timedout before\n"));

		list_for_each_entry(adapter, &adapters_list_g, list) {
			if (iterator++ == adapno) {
				is_found = true;
				break;
			}
		}

		kioc->timedout = 0;

		if (is_found)
			mraid_mm_dealloc_kioc( adapter, kioc );

	}
	else {
		wake_up(&wait_q);
	}
}


/**
 * lld_timedout	- callback from the expired timer
 * @t		: timer that timed out
 */
static void
lld_timedout(struct timer_list *t)
{
	struct uioc_timeout *timeout = from_timer(timeout, t, timer);
	uioc_t *kioc	= timeout->uioc;

	kioc->status 	= -ETIME;
	kioc->timedout	= 1;

	con_log(CL_ANN, (KERN_WARNING "megaraid cmm: ioctl timed out\n"));

	wake_up(&wait_q);
}


/**
 * kioc_to_mimd	- Converter from new back to old format
 * @kioc	: Kernel space IOCTL packet (successfully issued)
 * @mimd	: User space MIMD packet
 */
static int
kioc_to_mimd(uioc_t *kioc, mimd_t __user *mimd)
{
	mimd_t			kmimd;
	uint8_t			opcode;
	uint8_t			subopcode;

	mbox64_t		*mbox64;
	mraid_passthru_t	__user *upthru32;
	mraid_passthru_t	*kpthru32;
	mcontroller_t		cinfo;
	mraid_hba_info_t	*hinfo;


	if (copy_from_user(&kmimd, mimd, sizeof(mimd_t)))
		return (-EFAULT);

	opcode		= kmimd.ui.fcs.opcode;
	subopcode	= kmimd.ui.fcs.subopcode;

	if (opcode == 0x82) {
		switch (subopcode) {

		case MEGAIOC_QADAPINFO:

			hinfo = (mraid_hba_info_t *)(unsigned long)
					kioc->buf_vaddr;

			hinfo_to_cinfo(hinfo, &cinfo);

			if (copy_to_user(kmimd.data, &cinfo, sizeof(cinfo)))
				return (-EFAULT);

			return 0;

		default:
			return (-EINVAL);
		}

		return 0;
	}

	mbox64 = (mbox64_t *)(unsigned long)kioc->cmdbuf;

	if (kioc->user_pthru) {

		upthru32 = kioc->user_pthru;
		kpthru32 = kioc->pthru32;

		if (copy_to_user(&upthru32->scsistatus,
					&kpthru32->scsistatus,
					sizeof(uint8_t))) {
			return (-EFAULT);
		}
	}

	if (kioc->user_data) {
		if (copy_to_user(kioc->user_data, kioc->buf_vaddr,
					kioc->user_data_len)) {
			return (-EFAULT);
		}
	}

	if (copy_to_user(&mimd->mbox[17],
			&mbox64->mbox32.status, sizeof(uint8_t))) {
		return (-EFAULT);
	}

	return 0;
}


/**
 * hinfo_to_cinfo - Convert new format hba info into old format
 * @hinfo	: New format, more comprehensive adapter info
 * @cinfo	: Old format adapter info to support mimd_t apps
 */
static void
hinfo_to_cinfo(mraid_hba_info_t *hinfo, mcontroller_t *cinfo)
{
	if (!hinfo || !cinfo)
		return;

	cinfo->base		= hinfo->baseport;
	cinfo->irq		= hinfo->irq;
	cinfo->numldrv		= hinfo->num_ldrv;
	cinfo->pcibus		= hinfo->pci_bus;
	cinfo->pcidev		= hinfo->pci_slot;
	cinfo->pcifun		= PCI_FUNC(hinfo->pci_dev_fn);
	cinfo->pciid		= hinfo->pci_device_id;
	cinfo->pcivendor	= hinfo->pci_vendor_id;
	cinfo->pcislot		= hinfo->pci_slot;
	cinfo->uid		= hinfo->unique_id;
}


/**
 * mraid_mm_register_adp - Registration routine for low level drivers
 * @lld_adp	: Adapter object
 */
int
mraid_mm_register_adp(mraid_mmadp_t *lld_adp)
{
	mraid_mmadp_t	*adapter;
	mbox64_t	*mbox_list;
	uioc_t		*kioc;
	uint32_t	rval;
	int		i;


	if (lld_adp->drvr_type != DRVRTYPE_MBOX)
		return (-EINVAL);

	adapter = kzalloc(sizeof(mraid_mmadp_t), GFP_KERNEL);

	if (!adapter)
		return -ENOMEM;


	adapter->unique_id	= lld_adp->unique_id;
	adapter->drvr_type	= lld_adp->drvr_type;
	adapter->drvr_data	= lld_adp->drvr_data;
	adapter->pdev		= lld_adp->pdev;
	adapter->issue_uioc	= lld_adp->issue_uioc;
	adapter->timeout	= lld_adp->timeout;
	adapter->max_kioc	= lld_adp->max_kioc;
	adapter->quiescent	= 1;

	/*
	 * Allocate single blocks of memory for all required kiocs,
	 * mailboxes and passthru structures.
	 */
	adapter->kioc_list	= kmalloc_array(lld_adp->max_kioc,
						  sizeof(uioc_t),
						  GFP_KERNEL);
	adapter->mbox_list	= kmalloc_array(lld_adp->max_kioc,
						  sizeof(mbox64_t),
						  GFP_KERNEL);
	adapter->pthru_dma_pool = dma_pool_create("megaraid mm pthru pool",
						&adapter->pdev->dev,
						sizeof(mraid_passthru_t),
						16, 0);

	if (!adapter->kioc_list || !adapter->mbox_list ||
			!adapter->pthru_dma_pool) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid cmm: out of memory, %s %d\n", __func__,
			__LINE__));

		rval = (-ENOMEM);

		goto memalloc_error;
	}

	/*
	 * Slice kioc_list and make a kioc_pool with the individiual kiocs
	 */
	INIT_LIST_HEAD(&adapter->kioc_pool);
	spin_lock_init(&adapter->kioc_pool_lock);
	sema_init(&adapter->kioc_semaphore, lld_adp->max_kioc);

	mbox_list	= (mbox64_t *)adapter->mbox_list;

	for (i = 0; i < lld_adp->max_kioc; i++) {

		kioc		= adapter->kioc_list + i;
		kioc->cmdbuf	= (uint64_t)(unsigned long)(mbox_list + i);
		kioc->pthru32	= dma_pool_alloc(adapter->pthru_dma_pool,
						GFP_KERNEL, &kioc->pthru32_h);

		if (!kioc->pthru32) {

			con_log(CL_ANN, (KERN_WARNING
				"megaraid cmm: out of memory, %s %d\n",
					__func__, __LINE__));

			rval = (-ENOMEM);

			goto pthru_dma_pool_error;
		}

		list_add_tail(&kioc->list, &adapter->kioc_pool);
	}

	// Setup the dma pools for data buffers
	if ((rval = mraid_mm_setup_dma_pools(adapter)) != 0) {
		goto dma_pool_error;
	}

	list_add_tail(&adapter->list, &adapters_list_g);

	adapters_count_g++;

	return 0;

dma_pool_error:
	/* Do nothing */

pthru_dma_pool_error:

	for (i = 0; i < lld_adp->max_kioc; i++) {
		kioc = adapter->kioc_list + i;
		if (kioc->pthru32) {
			dma_pool_free(adapter->pthru_dma_pool, kioc->pthru32,
				kioc->pthru32_h);
		}
	}

memalloc_error:

	kfree(adapter->kioc_list);
	kfree(adapter->mbox_list);

	dma_pool_destroy(adapter->pthru_dma_pool);

	kfree(adapter);

	return rval;
}


/**
 * mraid_mm_adapter_app_handle - return the application handle for this adapter
 * @unique_id	: adapter unique identifier
 *
 * For the given driver data, locate the adapter in our global list and
 * return the corresponding handle, which is also used by applications to
 * uniquely identify an adapter.
 *
 * Return adapter handle if found in the list.
 * Return 0 if adapter could not be located, should never happen though.
 */
uint32_t
mraid_mm_adapter_app_handle(uint32_t unique_id)
{
	mraid_mmadp_t	*adapter;
	mraid_mmadp_t	*tmp;
	int		index = 0;

	list_for_each_entry_safe(adapter, tmp, &adapters_list_g, list) {

		if (adapter->unique_id == unique_id) {

			return MKADAP(index);
		}

		index++;
	}

	return 0;
}


/**
 * mraid_mm_setup_dma_pools - Set up dma buffer pools per adapter
 * @adp	: Adapter softstate
 *
 * We maintain a pool of dma buffers per each adapter. Each pool has one
 * buffer. E.g, we may have 5 dma pools - one each for 4k, 8k ... 64k buffers.
 * We have just one 4k buffer in 4k pool, one 8k buffer in 8k pool etc. We
 * dont' want to waste too much memory by allocating more buffers per each
 * pool.
 */
static int
mraid_mm_setup_dma_pools(mraid_mmadp_t *adp)
{
	mm_dmapool_t	*pool;
	int		bufsize;
	int		i;

	/*
	 * Create MAX_DMA_POOLS number of pools
	 */
	bufsize = MRAID_MM_INIT_BUFF_SIZE;

	for (i = 0; i < MAX_DMA_POOLS; i++){

		pool = &adp->dma_pool_list[i];

		pool->buf_size = bufsize;
		spin_lock_init(&pool->lock);

		pool->handle = dma_pool_create("megaraid mm data buffer",
						&adp->pdev->dev, bufsize,
						16, 0);

		if (!pool->handle) {
			goto dma_pool_setup_error;
		}

		pool->vaddr = dma_pool_alloc(pool->handle, GFP_KERNEL,
							&pool->paddr);

		if (!pool->vaddr)
			goto dma_pool_setup_error;

		bufsize = bufsize * 2;
	}

	return 0;

dma_pool_setup_error:

	mraid_mm_teardown_dma_pools(adp);
	return (-ENOMEM);
}


/**
 * mraid_mm_unregister_adp - Unregister routine for low level drivers
 * @unique_id	: UID of the adpater
 *
 * Assumes no outstanding ioctls to llds.
 */
int
mraid_mm_unregister_adp(uint32_t unique_id)
{
	mraid_mmadp_t	*adapter;
	mraid_mmadp_t	*tmp;

	list_for_each_entry_safe(adapter, tmp, &adapters_list_g, list) {


		if (adapter->unique_id == unique_id) {

			adapters_count_g--;

			list_del_init(&adapter->list);

			mraid_mm_free_adp_resources(adapter);

			kfree(adapter);

			con_log(CL_ANN, (
				"megaraid cmm: Unregistered one adapter:%#x\n",
				unique_id));

			return 0;
		}
	}

	return (-ENODEV);
}

/**
 * mraid_mm_free_adp_resources - Free adapter softstate
 * @adp	: Adapter softstate
 */
static void
mraid_mm_free_adp_resources(mraid_mmadp_t *adp)
{
	uioc_t	*kioc;
	int	i;

	mraid_mm_teardown_dma_pools(adp);

	for (i = 0; i < adp->max_kioc; i++) {

		kioc = adp->kioc_list + i;

		dma_pool_free(adp->pthru_dma_pool, kioc->pthru32,
				kioc->pthru32_h);
	}

	kfree(adp->kioc_list);
	kfree(adp->mbox_list);

	dma_pool_destroy(adp->pthru_dma_pool);


	return;
}


/**
 * mraid_mm_teardown_dma_pools - Free all per adapter dma buffers
 * @adp	: Adapter softstate
 */
static void
mraid_mm_teardown_dma_pools(mraid_mmadp_t *adp)
{
	int		i;
	mm_dmapool_t	*pool;

	for (i = 0; i < MAX_DMA_POOLS; i++) {

		pool = &adp->dma_pool_list[i];

		if (pool->handle) {

			if (pool->vaddr)
				dma_pool_free(pool->handle, pool->vaddr,
							pool->paddr);

			dma_pool_destroy(pool->handle);
			pool->handle = NULL;
		}
	}

	return;
}

/**
 * mraid_mm_init	- Module entry point
 */
static int __init
mraid_mm_init(void)
{
	int err;

	// Announce the driver version
	con_log(CL_ANN, (KERN_INFO "megaraid cmm: %s %s\n",
		LSI_COMMON_MOD_VERSION, LSI_COMMON_MOD_EXT_VERSION));

	err = misc_register(&megaraid_mm_dev);
	if (err < 0) {
		con_log(CL_ANN, ("megaraid cmm: cannot register misc device\n"));
		return err;
	}

	init_waitqueue_head(&wait_q);

	INIT_LIST_HEAD(&adapters_list_g);

	return 0;
}


/**
 * mraid_mm_exit	- Module exit point
 */
static void __exit
mraid_mm_exit(void)
{
	con_log(CL_DLEVEL1 , ("exiting common mod\n"));

	misc_deregister(&megaraid_mm_dev);
}

module_init(mraid_mm_init);
module_exit(mraid_mm_exit);

/* vi: set ts=8 sw=8 tw=78: */
