/*
 *	Functions to handle I2O controllers and I2O message handling
 *
 *	Copyright (C) 1999-2002	Red Hat Software
 *
 *	Written by Alan Cox, Building Number Three Ltd
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation; either version 2 of the License, or (at your
 *	option) any later version.
 *
 *	A lot of the I2O message side code from this is taken from the
 *	Red Creek RCPCI45 adapter driver by Red Creek Communications
 *
 *	Fixes/additions:
 *		Philipp Rumpf
 *		Juha Sievänen <Juha.Sievanen@cs.Helsinki.FI>
 *		Auvo Häkkinen <Auvo.Hakkinen@cs.Helsinki.FI>
 *		Deepak Saxena <deepak@plexity.net>
 *		Boji T Kannanthanam <boji.t.kannanthanam@intel.com>
 *		Alan Cox <alan@redhat.com>:
 *			Ported to Linux 2.5.
 *		Markus Lidel <Markus.Lidel@shadowconnect.com>:
 *			Minor fixes for 2.6.
 */

#include <linux/module.h>
#include <linux/i2o.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include "core.h"

#define OSM_NAME	"i2o"
#define OSM_VERSION	"1.325"
#define OSM_DESCRIPTION	"I2O subsystem"

/* global I2O controller list */
LIST_HEAD(i2o_controllers);

/*
 * global I2O System Table. Contains information about all the IOPs in the
 * system. Used to inform IOPs about each others existence.
 */
static struct i2o_dma i2o_systab;

static int i2o_hrt_get(struct i2o_controller *c);

/**
 *	i2o_msg_get_wait - obtain an I2O message from the IOP
 *	@c: I2O controller
 *	@msg: pointer to a I2O message pointer
 *	@wait: how long to wait until timeout
 *
 *	This function waits up to wait seconds for a message slot to be
 *	available.
 *
 *	On a success the message is returned and the pointer to the message is
 *	set in msg. The returned message is the physical page frame offset
 *	address from the read port (see the i2o spec). If no message is
 *	available returns I2O_QUEUE_EMPTY and msg is leaved untouched.
 */
struct i2o_message *i2o_msg_get_wait(struct i2o_controller *c, int wait)
{
	unsigned long timeout = jiffies + wait * HZ;
	struct i2o_message *msg;

	while (IS_ERR(msg = i2o_msg_get(c))) {
		if (time_after(jiffies, timeout)) {
			osm_debug("%s: Timeout waiting for message frame.\n",
				  c->name);
			return ERR_PTR(-ETIMEDOUT);
		}
		schedule_timeout_uninterruptible(1);
	}

	return msg;
};

#if BITS_PER_LONG == 64
/**
 *      i2o_cntxt_list_add - Append a pointer to context list and return a id
 *	@c: controller to which the context list belong
 *	@ptr: pointer to add to the context list
 *
 *	Because the context field in I2O is only 32-bit large, on 64-bit the
 *	pointer is to large to fit in the context field. The i2o_cntxt_list
 *	functions therefore map pointers to context fields.
 *
 *	Returns context id > 0 on success or 0 on failure.
 */
u32 i2o_cntxt_list_add(struct i2o_controller * c, void *ptr)
{
	struct i2o_context_list_element *entry;
	unsigned long flags;

	if (!ptr)
		osm_err("%s: couldn't add NULL pointer to context list!\n",
			c->name);

	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry) {
		osm_err("%s: Could not allocate memory for context list element"
			"\n", c->name);
		return 0;
	}

	entry->ptr = ptr;
	entry->timestamp = jiffies;
	INIT_LIST_HEAD(&entry->list);

	spin_lock_irqsave(&c->context_list_lock, flags);

	if (unlikely(atomic_inc_and_test(&c->context_list_counter)))
		atomic_inc(&c->context_list_counter);

	entry->context = atomic_read(&c->context_list_counter);

	list_add(&entry->list, &c->context_list);

	spin_unlock_irqrestore(&c->context_list_lock, flags);

	osm_debug("%s: Add context to list %p -> %d\n", c->name, ptr, context);

	return entry->context;
};

/**
 *      i2o_cntxt_list_remove - Remove a pointer from the context list
 *	@c: controller to which the context list belong
 *	@ptr: pointer which should be removed from the context list
 *
 *	Removes a previously added pointer from the context list and returns
 *	the matching context id.
 *
 *	Returns context id on succes or 0 on failure.
 */
u32 i2o_cntxt_list_remove(struct i2o_controller * c, void *ptr)
{
	struct i2o_context_list_element *entry;
	u32 context = 0;
	unsigned long flags;

	spin_lock_irqsave(&c->context_list_lock, flags);
	list_for_each_entry(entry, &c->context_list, list)
	    if (entry->ptr == ptr) {
		list_del(&entry->list);
		context = entry->context;
		kfree(entry);
		break;
	}
	spin_unlock_irqrestore(&c->context_list_lock, flags);

	if (!context)
		osm_warn("%s: Could not remove nonexistent ptr %p\n", c->name,
			 ptr);

	osm_debug("%s: remove ptr from context list %d -> %p\n", c->name,
		  context, ptr);

	return context;
};

/**
 *      i2o_cntxt_list_get - Get a pointer from the context list and remove it
 *	@c: controller to which the context list belong
 *	@context: context id to which the pointer belong
 *
 *	Returns pointer to the matching context id on success or NULL on
 *	failure.
 */
void *i2o_cntxt_list_get(struct i2o_controller *c, u32 context)
{
	struct i2o_context_list_element *entry;
	unsigned long flags;
	void *ptr = NULL;

	spin_lock_irqsave(&c->context_list_lock, flags);
	list_for_each_entry(entry, &c->context_list, list)
	    if (entry->context == context) {
		list_del(&entry->list);
		ptr = entry->ptr;
		kfree(entry);
		break;
	}
	spin_unlock_irqrestore(&c->context_list_lock, flags);

	if (!ptr)
		osm_warn("%s: context id %d not found\n", c->name, context);

	osm_debug("%s: get ptr from context list %d -> %p\n", c->name, context,
		  ptr);

	return ptr;
};

/**
 *      i2o_cntxt_list_get_ptr - Get a context id from the context list
 *	@c: controller to which the context list belong
 *	@ptr: pointer to which the context id should be fetched
 *
 *	Returns context id which matches to the pointer on succes or 0 on
 *	failure.
 */
u32 i2o_cntxt_list_get_ptr(struct i2o_controller * c, void *ptr)
{
	struct i2o_context_list_element *entry;
	u32 context = 0;
	unsigned long flags;

	spin_lock_irqsave(&c->context_list_lock, flags);
	list_for_each_entry(entry, &c->context_list, list)
	    if (entry->ptr == ptr) {
		context = entry->context;
		break;
	}
	spin_unlock_irqrestore(&c->context_list_lock, flags);

	if (!context)
		osm_warn("%s: Could not find nonexistent ptr %p\n", c->name,
			 ptr);

	osm_debug("%s: get context id from context list %p -> %d\n", c->name,
		  ptr, context);

	return context;
};
#endif

/**
 *	i2o_iop_find - Find an I2O controller by id
 *	@unit: unit number of the I2O controller to search for
 *
 *	Lookup the I2O controller on the controller list.
 *
 *	Returns pointer to the I2O controller on success or NULL if not found.
 */
struct i2o_controller *i2o_find_iop(int unit)
{
	struct i2o_controller *c;

	list_for_each_entry(c, &i2o_controllers, list) {
		if (c->unit == unit)
			return c;
	}

	return NULL;
};

/**
 *	i2o_iop_find_device - Find a I2O device on an I2O controller
 *	@c: I2O controller where the I2O device hangs on
 *	@tid: TID of the I2O device to search for
 *
 *	Searches the devices of the I2O controller for a device with TID tid and
 *	returns it.
 *
 *	Returns a pointer to the I2O device if found, otherwise NULL.
 */
struct i2o_device *i2o_iop_find_device(struct i2o_controller *c, u16 tid)
{
	struct i2o_device *dev;

	list_for_each_entry(dev, &c->devices, list)
	    if (dev->lct_data.tid == tid)
		return dev;

	return NULL;
};

/**
 *	i2o_quiesce_controller - quiesce controller
 *	@c: controller
 *
 *	Quiesce an IOP. Causes IOP to make external operation quiescent
 *	(i2o 'READY' state). Internal operation of the IOP continues normally.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_iop_quiesce(struct i2o_controller *c)
{
	struct i2o_message *msg;
	i2o_status_block *sb = c->status_block.virt;
	int rc;

	i2o_status_get(c);

	/* SysQuiesce discarded if IOP not in READY or OPERATIONAL state */
	if ((sb->iop_state != ADAPTER_STATE_READY) &&
	    (sb->iop_state != ADAPTER_STATE_OPERATIONAL))
		return 0;

	msg = i2o_msg_get_wait(c, I2O_TIMEOUT_MESSAGE_GET);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	msg->u.head[0] = cpu_to_le32(FOUR_WORD_MSG_SIZE | SGL_OFFSET_0);
	msg->u.head[1] =
	    cpu_to_le32(I2O_CMD_SYS_QUIESCE << 24 | HOST_TID << 12 |
			ADAPTER_TID);

	/* Long timeout needed for quiesce if lots of devices */
	if ((rc = i2o_msg_post_wait(c, msg, 240)))
		osm_info("%s: Unable to quiesce (status=%#x).\n", c->name, -rc);
	else
		osm_debug("%s: Quiesced.\n", c->name);

	i2o_status_get(c);	// Entered READY state

	return rc;
};

/**
 *	i2o_iop_enable - move controller from ready to OPERATIONAL
 *	@c: I2O controller
 *
 *	Enable IOP. This allows the IOP to resume external operations and
 *	reverses the effect of a quiesce. Returns zero or an error code if
 *	an error occurs.
 */
static int i2o_iop_enable(struct i2o_controller *c)
{
	struct i2o_message *msg;
	i2o_status_block *sb = c->status_block.virt;
	int rc;

	i2o_status_get(c);

	/* Enable only allowed on READY state */
	if (sb->iop_state != ADAPTER_STATE_READY)
		return -EINVAL;

	msg = i2o_msg_get_wait(c, I2O_TIMEOUT_MESSAGE_GET);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	msg->u.head[0] = cpu_to_le32(FOUR_WORD_MSG_SIZE | SGL_OFFSET_0);
	msg->u.head[1] =
	    cpu_to_le32(I2O_CMD_SYS_ENABLE << 24 | HOST_TID << 12 |
			ADAPTER_TID);

	/* How long of a timeout do we need? */
	if ((rc = i2o_msg_post_wait(c, msg, 240)))
		osm_err("%s: Could not enable (status=%#x).\n", c->name, -rc);
	else
		osm_debug("%s: Enabled.\n", c->name);

	i2o_status_get(c);	// entered OPERATIONAL state

	return rc;
};

/**
 *	i2o_iop_quiesce_all - Quiesce all I2O controllers on the system
 *
 *	Quiesce all I2O controllers which are connected to the system.
 */
static inline void i2o_iop_quiesce_all(void)
{
	struct i2o_controller *c, *tmp;

	list_for_each_entry_safe(c, tmp, &i2o_controllers, list) {
		if (!c->no_quiesce)
			i2o_iop_quiesce(c);
	}
};

/**
 *	i2o_iop_enable_all - Enables all controllers on the system
 *
 *	Enables all I2O controllers which are connected to the system.
 */
static inline void i2o_iop_enable_all(void)
{
	struct i2o_controller *c, *tmp;

	list_for_each_entry_safe(c, tmp, &i2o_controllers, list)
	    i2o_iop_enable(c);
};

/**
 *	i2o_clear_controller - Bring I2O controller into HOLD state
 *	@c: controller
 *
 *	Clear an IOP to HOLD state, ie. terminate external operations, clear all
 *	input queues and prepare for a system restart. IOP's internal operation
 *	continues normally and the outbound queue is alive. The IOP is not
 *	expected to rebuild its LCT.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_iop_clear(struct i2o_controller *c)
{
	struct i2o_message *msg;
	int rc;

	msg = i2o_msg_get_wait(c, I2O_TIMEOUT_MESSAGE_GET);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	/* Quiesce all IOPs first */
	i2o_iop_quiesce_all();

	msg->u.head[0] = cpu_to_le32(FOUR_WORD_MSG_SIZE | SGL_OFFSET_0);
	msg->u.head[1] =
	    cpu_to_le32(I2O_CMD_ADAPTER_CLEAR << 24 | HOST_TID << 12 |
			ADAPTER_TID);

	if ((rc = i2o_msg_post_wait(c, msg, 30)))
		osm_info("%s: Unable to clear (status=%#x).\n", c->name, -rc);
	else
		osm_debug("%s: Cleared.\n", c->name);

	/* Enable all IOPs */
	i2o_iop_enable_all();

	return rc;
}

/**
 *	i2o_iop_init_outbound_queue - setup the outbound message queue
 *	@c: I2O controller
 *
 *	Clear and (re)initialize IOP's outbound queue and post the message
 *	frames to the IOP.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_iop_init_outbound_queue(struct i2o_controller *c)
{
	u32 m;
	volatile u8 *status = c->status.virt;
	struct i2o_message *msg;
	ulong timeout;
	int i;

	osm_debug("%s: Initializing Outbound Queue...\n", c->name);

	memset(c->status.virt, 0, 4);

	msg = i2o_msg_get_wait(c, I2O_TIMEOUT_MESSAGE_GET);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	msg->u.head[0] = cpu_to_le32(EIGHT_WORD_MSG_SIZE | SGL_OFFSET_6);
	msg->u.head[1] =
	    cpu_to_le32(I2O_CMD_OUTBOUND_INIT << 24 | HOST_TID << 12 |
			ADAPTER_TID);
	msg->u.s.icntxt = cpu_to_le32(i2o_exec_driver.context);
	msg->u.s.tcntxt = cpu_to_le32(0x00000000);
	msg->body[0] = cpu_to_le32(PAGE_SIZE);
	/* Outbound msg frame size in words and Initcode */
	msg->body[1] = cpu_to_le32(I2O_OUTBOUND_MSG_FRAME_SIZE << 16 | 0x80);
	msg->body[2] = cpu_to_le32(0xd0000004);
	msg->body[3] = cpu_to_le32(i2o_dma_low(c->status.phys));
	msg->body[4] = cpu_to_le32(i2o_dma_high(c->status.phys));

	i2o_msg_post(c, msg);

	timeout = jiffies + I2O_TIMEOUT_INIT_OUTBOUND_QUEUE * HZ;
	while (*status <= I2O_CMD_IN_PROGRESS) {
		if (time_after(jiffies, timeout)) {
			osm_warn("%s: Timeout Initializing\n", c->name);
			return -ETIMEDOUT;
		}
		schedule_timeout_uninterruptible(1);
	}

	m = c->out_queue.phys;

	/* Post frames */
	for (i = 0; i < I2O_MAX_OUTBOUND_MSG_FRAMES; i++) {
		i2o_flush_reply(c, m);
		udelay(1);	/* Promise */
		m += I2O_OUTBOUND_MSG_FRAME_SIZE * sizeof(u32);
	}

	return 0;
}

/**
 *	i2o_iop_reset - reset an I2O controller
 *	@c: controller to reset
 *
 *	Reset the IOP into INIT state and wait until IOP gets into RESET state.
 *	Terminate all external operations, clear IOP's inbound and outbound
 *	queues, terminate all DDMs, and reload the IOP's operating environment
 *	and all local DDMs. The IOP rebuilds its LCT.
 */
static int i2o_iop_reset(struct i2o_controller *c)
{
	volatile u8 *status = c->status.virt;
	struct i2o_message *msg;
	unsigned long timeout;
	i2o_status_block *sb = c->status_block.virt;
	int rc = 0;

	osm_debug("%s: Resetting controller\n", c->name);

	msg = i2o_msg_get_wait(c, I2O_TIMEOUT_MESSAGE_GET);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	memset(c->status_block.virt, 0, 8);

	/* Quiesce all IOPs first */
	i2o_iop_quiesce_all();

	msg->u.head[0] = cpu_to_le32(EIGHT_WORD_MSG_SIZE | SGL_OFFSET_0);
	msg->u.head[1] =
	    cpu_to_le32(I2O_CMD_ADAPTER_RESET << 24 | HOST_TID << 12 |
			ADAPTER_TID);
	msg->u.s.icntxt = cpu_to_le32(i2o_exec_driver.context);
	msg->u.s.tcntxt = cpu_to_le32(0x00000000);
	msg->body[0] = cpu_to_le32(0x00000000);
	msg->body[1] = cpu_to_le32(0x00000000);
	msg->body[2] = cpu_to_le32(i2o_dma_low(c->status.phys));
	msg->body[3] = cpu_to_le32(i2o_dma_high(c->status.phys));

	i2o_msg_post(c, msg);

	/* Wait for a reply */
	timeout = jiffies + I2O_TIMEOUT_RESET * HZ;
	while (!*status) {
		if (time_after(jiffies, timeout))
			break;

		schedule_timeout_uninterruptible(1);
	}

	switch (*status) {
	case I2O_CMD_REJECTED:
		osm_warn("%s: IOP reset rejected\n", c->name);
		rc = -EPERM;
		break;

	case I2O_CMD_IN_PROGRESS:
		/*
		 * Once the reset is sent, the IOP goes into the INIT state
		 * which is indeterminate. We need to wait until the IOP has
		 * rebooted before we can let the system talk to it. We read
		 * the inbound Free_List until a message is available. If we
		 * can't read one in the given ammount of time, we assume the
		 * IOP could not reboot properly.
		 */
		osm_debug("%s: Reset in progress, waiting for reboot...\n",
			  c->name);

		while (IS_ERR(msg = i2o_msg_get_wait(c, I2O_TIMEOUT_RESET))) {
			if (time_after(jiffies, timeout)) {
				osm_err("%s: IOP reset timeout.\n", c->name);
				rc = PTR_ERR(msg);
				goto exit;
			}
			schedule_timeout_uninterruptible(1);
		}
		i2o_msg_nop(c, msg);

		/* from here all quiesce commands are safe */
		c->no_quiesce = 0;

		/* verify if controller is in state RESET */
		i2o_status_get(c);

		if (!c->promise && (sb->iop_state != ADAPTER_STATE_RESET))
			osm_warn("%s: reset completed, but adapter not in RESET"
				 " state.\n", c->name);
		else
			osm_debug("%s: reset completed.\n", c->name);

		break;

	default:
		osm_err("%s: IOP reset timeout.\n", c->name);
		rc = -ETIMEDOUT;
		break;
	}

      exit:
	/* Enable all IOPs */
	i2o_iop_enable_all();

	return rc;
};

/**
 *	i2o_iop_activate - Bring controller up to HOLD
 *	@c: controller
 *
 *	This function brings an I2O controller into HOLD state. The adapter
 *	is reset if necessary and then the queues and resource table are read.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_iop_activate(struct i2o_controller *c)
{
	i2o_status_block *sb = c->status_block.virt;
	int rc;
	int state;

	/* In INIT state, Wait Inbound Q to initialize (in i2o_status_get) */
	/* In READY state, Get status */

	rc = i2o_status_get(c);
	if (rc) {
		osm_info("%s: Unable to obtain status, attempting a reset.\n",
			 c->name);
		rc = i2o_iop_reset(c);
		if (rc)
			return rc;
	}

	if (sb->i2o_version > I2OVER15) {
		osm_err("%s: Not running version 1.5 of the I2O Specification."
			"\n", c->name);
		return -ENODEV;
	}

	switch (sb->iop_state) {
	case ADAPTER_STATE_FAULTED:
		osm_err("%s: hardware fault\n", c->name);
		return -EFAULT;

	case ADAPTER_STATE_READY:
	case ADAPTER_STATE_OPERATIONAL:
	case ADAPTER_STATE_HOLD:
	case ADAPTER_STATE_FAILED:
		osm_debug("%s: already running, trying to reset...\n", c->name);
		rc = i2o_iop_reset(c);
		if (rc)
			return rc;
	}

	/* preserve state */
	state = sb->iop_state;

	rc = i2o_iop_init_outbound_queue(c);
	if (rc)
		return rc;

	/* if adapter was not in RESET state clear now */
	if (state != ADAPTER_STATE_RESET)
		i2o_iop_clear(c);

	i2o_status_get(c);

	if (sb->iop_state != ADAPTER_STATE_HOLD) {
		osm_err("%s: failed to bring IOP into HOLD state\n", c->name);
		return -EIO;
	}

	return i2o_hrt_get(c);
};

/**
 *	i2o_iop_systab_set - Set the I2O System Table of the specified IOP
 *	@c: I2O controller to which the system table should be send
 *
 *	Before the systab could be set i2o_systab_build() must be called.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_iop_systab_set(struct i2o_controller *c)
{
	struct i2o_message *msg;
	i2o_status_block *sb = c->status_block.virt;
	struct device *dev = &c->pdev->dev;
	struct resource *root;
	int rc;

	if (sb->current_mem_size < sb->desired_mem_size) {
		struct resource *res = &c->mem_resource;
		res->name = c->pdev->bus->name;
		res->flags = IORESOURCE_MEM;
		res->start = 0;
		res->end = 0;
		osm_info("%s: requires private memory resources.\n", c->name);
		root = pci_find_parent_resource(c->pdev, res);
		if (root == NULL)
			osm_warn("%s: Can't find parent resource!\n", c->name);
		if (root && allocate_resource(root, res, sb->desired_mem_size, sb->desired_mem_size, sb->desired_mem_size, 1 << 20,	/* Unspecified, so use 1Mb and play safe */
					      NULL, NULL) >= 0) {
			c->mem_alloc = 1;
			sb->current_mem_size = 1 + res->end - res->start;
			sb->current_mem_base = res->start;
			osm_info("%s: allocated %llu bytes of PCI memory at "
				"0x%016llX.\n", c->name,
				(unsigned long long)(1 + res->end - res->start),
				(unsigned long long)res->start);
		}
	}

	if (sb->current_io_size < sb->desired_io_size) {
		struct resource *res = &c->io_resource;
		res->name = c->pdev->bus->name;
		res->flags = IORESOURCE_IO;
		res->start = 0;
		res->end = 0;
		osm_info("%s: requires private memory resources.\n", c->name);
		root = pci_find_parent_resource(c->pdev, res);
		if (root == NULL)
			osm_warn("%s: Can't find parent resource!\n", c->name);
		if (root && allocate_resource(root, res, sb->desired_io_size, sb->desired_io_size, sb->desired_io_size, 1 << 20,	/* Unspecified, so use 1Mb and play safe */
					      NULL, NULL) >= 0) {
			c->io_alloc = 1;
			sb->current_io_size = 1 + res->end - res->start;
			sb->current_mem_base = res->start;
			osm_info("%s: allocated %llu bytes of PCI I/O at "
				"0x%016llX.\n", c->name,
				(unsigned long long)(1 + res->end - res->start),
				(unsigned long long)res->start);
		}
	}

	msg = i2o_msg_get_wait(c, I2O_TIMEOUT_MESSAGE_GET);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	i2o_systab.phys = dma_map_single(dev, i2o_systab.virt, i2o_systab.len,
					 PCI_DMA_TODEVICE);
	if (!i2o_systab.phys) {
		i2o_msg_nop(c, msg);
		return -ENOMEM;
	}

	msg->u.head[0] = cpu_to_le32(I2O_MESSAGE_SIZE(12) | SGL_OFFSET_6);
	msg->u.head[1] =
	    cpu_to_le32(I2O_CMD_SYS_TAB_SET << 24 | HOST_TID << 12 |
			ADAPTER_TID);

	/*
	 * Provide three SGL-elements:
	 * System table (SysTab), Private memory space declaration and
	 * Private i/o space declaration
	 */

	msg->body[0] = cpu_to_le32(c->unit + 2);
	msg->body[1] = cpu_to_le32(0x00000000);
	msg->body[2] = cpu_to_le32(0x54000000 | i2o_systab.len);
	msg->body[3] = cpu_to_le32(i2o_systab.phys);
	msg->body[4] = cpu_to_le32(0x54000000 | sb->current_mem_size);
	msg->body[5] = cpu_to_le32(sb->current_mem_base);
	msg->body[6] = cpu_to_le32(0xd4000000 | sb->current_io_size);
	msg->body[6] = cpu_to_le32(sb->current_io_base);

	rc = i2o_msg_post_wait(c, msg, 120);

	dma_unmap_single(dev, i2o_systab.phys, i2o_systab.len,
			 PCI_DMA_TODEVICE);

	if (rc < 0)
		osm_err("%s: Unable to set SysTab (status=%#x).\n", c->name,
			-rc);
	else
		osm_debug("%s: SysTab set.\n", c->name);

	return rc;
}

/**
 *	i2o_iop_online - Bring a controller online into OPERATIONAL state.
 *	@c: I2O controller
 *
 *	Send the system table and enable the I2O controller.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_iop_online(struct i2o_controller *c)
{
	int rc;

	rc = i2o_iop_systab_set(c);
	if (rc)
		return rc;

	/* In READY state */
	osm_debug("%s: Attempting to enable...\n", c->name);
	rc = i2o_iop_enable(c);
	if (rc)
		return rc;

	return 0;
};

/**
 *	i2o_iop_remove - Remove the I2O controller from the I2O core
 *	@c: I2O controller
 *
 *	Remove the I2O controller from the I2O core. If devices are attached to
 *	the controller remove these also and finally reset the controller.
 */
void i2o_iop_remove(struct i2o_controller *c)
{
	struct i2o_device *dev, *tmp;

	osm_debug("%s: deleting controller\n", c->name);

	i2o_driver_notify_controller_remove_all(c);

	list_del(&c->list);

	list_for_each_entry_safe(dev, tmp, &c->devices, list)
	    i2o_device_remove(dev);

	device_del(&c->device);

	/* Ask the IOP to switch to RESET state */
	i2o_iop_reset(c);
}

/**
 *	i2o_systab_build - Build system table
 *
 *	The system table contains information about all the IOPs in the system
 *	(duh) and is used by the Executives on the IOPs to establish peer2peer
 *	connections. We're not supporting peer2peer at the moment, but this
 *	will be needed down the road for things like lan2lan forwarding.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_systab_build(void)
{
	struct i2o_controller *c, *tmp;
	int num_controllers = 0;
	u32 change_ind = 0;
	int count = 0;
	struct i2o_sys_tbl *systab = i2o_systab.virt;

	list_for_each_entry_safe(c, tmp, &i2o_controllers, list)
	    num_controllers++;

	if (systab) {
		change_ind = systab->change_ind;
		kfree(i2o_systab.virt);
	}

	/* Header + IOPs */
	i2o_systab.len = sizeof(struct i2o_sys_tbl) + num_controllers *
	    sizeof(struct i2o_sys_tbl_entry);

	systab = i2o_systab.virt = kzalloc(i2o_systab.len, GFP_KERNEL);
	if (!systab) {
		osm_err("unable to allocate memory for System Table\n");
		return -ENOMEM;
	}

	systab->version = I2OVERSION;
	systab->change_ind = change_ind + 1;

	list_for_each_entry_safe(c, tmp, &i2o_controllers, list) {
		i2o_status_block *sb;

		if (count >= num_controllers) {
			osm_err("controller added while building system table"
				"\n");
			break;
		}

		sb = c->status_block.virt;

		/*
		 * Get updated IOP state so we have the latest information
		 *
		 * We should delete the controller at this point if it
		 * doesn't respond since if it's not on the system table
		 * it is techninically not part of the I2O subsystem...
		 */
		if (unlikely(i2o_status_get(c))) {
			osm_err("%s: Deleting b/c could not get status while "
				"attempting to build system table\n", c->name);
			i2o_iop_remove(c);
			continue;	// try the next one
		}

		systab->iops[count].org_id = sb->org_id;
		systab->iops[count].iop_id = c->unit + 2;
		systab->iops[count].seg_num = 0;
		systab->iops[count].i2o_version = sb->i2o_version;
		systab->iops[count].iop_state = sb->iop_state;
		systab->iops[count].msg_type = sb->msg_type;
		systab->iops[count].frame_size = sb->inbound_frame_size;
		systab->iops[count].last_changed = change_ind;
		systab->iops[count].iop_capabilities = sb->iop_capabilities;
		systab->iops[count].inbound_low =
		    i2o_dma_low(c->base.phys + I2O_IN_PORT);
		systab->iops[count].inbound_high =
		    i2o_dma_high(c->base.phys + I2O_IN_PORT);

		count++;
	}

	systab->num_entries = count;

	return 0;
};

/**
 *	i2o_parse_hrt - Parse the hardware resource table.
 *	@c: I2O controller
 *
 *	We don't do anything with it except dumping it (in debug mode).
 *
 *	Returns 0.
 */
static int i2o_parse_hrt(struct i2o_controller *c)
{
	i2o_dump_hrt(c);
	return 0;
};

/**
 *	i2o_status_get - Get the status block from the I2O controller
 *	@c: I2O controller
 *
 *	Issue a status query on the controller. This updates the attached
 *	status block. The status block could then be accessed through
 *	c->status_block.
 *
 *	Returns 0 on success or negative error code on failure.
 */
int i2o_status_get(struct i2o_controller *c)
{
	struct i2o_message *msg;
	volatile u8 *status_block;
	unsigned long timeout;

	status_block = (u8 *) c->status_block.virt;
	memset(c->status_block.virt, 0, sizeof(i2o_status_block));

	msg = i2o_msg_get_wait(c, I2O_TIMEOUT_MESSAGE_GET);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	msg->u.head[0] = cpu_to_le32(NINE_WORD_MSG_SIZE | SGL_OFFSET_0);
	msg->u.head[1] =
	    cpu_to_le32(I2O_CMD_STATUS_GET << 24 | HOST_TID << 12 |
			ADAPTER_TID);
	msg->u.s.icntxt = cpu_to_le32(i2o_exec_driver.context);
	msg->u.s.tcntxt = cpu_to_le32(0x00000000);
	msg->body[0] = cpu_to_le32(0x00000000);
	msg->body[1] = cpu_to_le32(0x00000000);
	msg->body[2] = cpu_to_le32(i2o_dma_low(c->status_block.phys));
	msg->body[3] = cpu_to_le32(i2o_dma_high(c->status_block.phys));
	msg->body[4] = cpu_to_le32(sizeof(i2o_status_block));	/* always 88 bytes */

	i2o_msg_post(c, msg);

	/* Wait for a reply */
	timeout = jiffies + I2O_TIMEOUT_STATUS_GET * HZ;
	while (status_block[87] != 0xFF) {
		if (time_after(jiffies, timeout)) {
			osm_err("%s: Get status timeout.\n", c->name);
			return -ETIMEDOUT;
		}

		schedule_timeout_uninterruptible(1);
	}

#ifdef DEBUG
	i2o_debug_state(c);
#endif

	return 0;
}

/*
 *	i2o_hrt_get - Get the Hardware Resource Table from the I2O controller
 *	@c: I2O controller from which the HRT should be fetched
 *
 *	The HRT contains information about possible hidden devices but is
 *	mostly useless to us.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_hrt_get(struct i2o_controller *c)
{
	int rc;
	int i;
	i2o_hrt *hrt = c->hrt.virt;
	u32 size = sizeof(i2o_hrt);
	struct device *dev = &c->pdev->dev;

	for (i = 0; i < I2O_HRT_GET_TRIES; i++) {
		struct i2o_message *msg;

		msg = i2o_msg_get_wait(c, I2O_TIMEOUT_MESSAGE_GET);
		if (IS_ERR(msg))
			return PTR_ERR(msg);

		msg->u.head[0] = cpu_to_le32(SIX_WORD_MSG_SIZE | SGL_OFFSET_4);
		msg->u.head[1] =
		    cpu_to_le32(I2O_CMD_HRT_GET << 24 | HOST_TID << 12 |
				ADAPTER_TID);
		msg->body[0] = cpu_to_le32(0xd0000000 | c->hrt.len);
		msg->body[1] = cpu_to_le32(c->hrt.phys);

		rc = i2o_msg_post_wait_mem(c, msg, 20, &c->hrt);

		if (rc < 0) {
			osm_err("%s: Unable to get HRT (status=%#x)\n", c->name,
				-rc);
			return rc;
		}

		size = hrt->num_entries * hrt->entry_len << 2;
		if (size > c->hrt.len) {
			if (i2o_dma_realloc(dev, &c->hrt, size, GFP_KERNEL))
				return -ENOMEM;
			else
				hrt = c->hrt.virt;
		} else
			return i2o_parse_hrt(c);
	}

	osm_err("%s: Unable to get HRT after %d tries, giving up\n", c->name,
		I2O_HRT_GET_TRIES);

	return -EBUSY;
}

/**
 *	i2o_iop_release - release the memory for a I2O controller
 *	@dev: I2O controller which should be released
 *
 *	Release the allocated memory. This function is called if refcount of
 *	device reaches 0 automatically.
 */
static void i2o_iop_release(struct device *dev)
{
	struct i2o_controller *c = to_i2o_controller(dev);

	i2o_iop_free(c);
};

/**
 *	i2o_iop_alloc - Allocate and initialize a i2o_controller struct
 *
 *	Allocate the necessary memory for a i2o_controller struct and
 *	initialize the lists and message mempool.
 *
 *	Returns a pointer to the I2O controller or a negative error code on
 *	failure.
 */
struct i2o_controller *i2o_iop_alloc(void)
{
	static int unit = 0;	/* 0 and 1 are NULL IOP and Local Host */
	struct i2o_controller *c;
	char poolname[32];

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c) {
		osm_err("i2o: Insufficient memory to allocate a I2O controller."
			"\n");
		return ERR_PTR(-ENOMEM);
	}

	c->unit = unit++;
	sprintf(c->name, "iop%d", c->unit);

	snprintf(poolname, sizeof(poolname), "i2o_%s_msg_inpool", c->name);
	if (i2o_pool_alloc
	    (&c->in_msg, poolname, I2O_INBOUND_MSG_FRAME_SIZE * 4 + sizeof(u32),
	     I2O_MSG_INPOOL_MIN)) {
		kfree(c);
		return ERR_PTR(-ENOMEM);
	};

	INIT_LIST_HEAD(&c->devices);
	spin_lock_init(&c->lock);
	mutex_init(&c->lct_lock);

	device_initialize(&c->device);

	c->device.release = &i2o_iop_release;

	snprintf(c->device.bus_id, BUS_ID_SIZE, "iop%d", c->unit);

#if BITS_PER_LONG == 64
	spin_lock_init(&c->context_list_lock);
	atomic_set(&c->context_list_counter, 0);
	INIT_LIST_HEAD(&c->context_list);
#endif

	return c;
};

/**
 *	i2o_iop_add - Initialize the I2O controller and add him to the I2O core
 *	@c: controller
 *
 *	Initialize the I2O controller and if no error occurs add him to the I2O
 *	core.
 *
 *	Returns 0 on success or negative error code on failure.
 */
int i2o_iop_add(struct i2o_controller *c)
{
	int rc;

	if ((rc = device_add(&c->device))) {
		osm_err("%s: could not add controller\n", c->name);
		goto iop_reset;
	}

	osm_info("%s: Activating I2O controller...\n", c->name);
	osm_info("%s: This may take a few minutes if there are many devices\n",
		 c->name);

	if ((rc = i2o_iop_activate(c))) {
		osm_err("%s: could not activate controller\n", c->name);
		goto device_del;
	}

	osm_debug("%s: building sys table...\n", c->name);

	if ((rc = i2o_systab_build()))
		goto device_del;

	osm_debug("%s: online controller...\n", c->name);

	if ((rc = i2o_iop_online(c)))
		goto device_del;

	osm_debug("%s: getting LCT...\n", c->name);

	if ((rc = i2o_exec_lct_get(c)))
		goto device_del;

	list_add(&c->list, &i2o_controllers);

	i2o_driver_notify_controller_add_all(c);

	osm_info("%s: Controller added\n", c->name);

	return 0;

      device_del:
	device_del(&c->device);

      iop_reset:
	i2o_iop_reset(c);

	return rc;
};

/**
 *	i2o_event_register - Turn on/off event notification for a I2O device
 *	@dev: I2O device which should receive the event registration request
 *	@drv: driver which want to get notified
 *	@tcntxt: transaction context to use with this notifier
 *	@evt_mask: mask of events
 *
 *	Create and posts an event registration message to the task. No reply
 *	is waited for, or expected. If you do not want further notifications,
 *	call the i2o_event_register again with a evt_mask of 0.
 *
 *	Returns 0 on success or negative error code on failure.
 */
int i2o_event_register(struct i2o_device *dev, struct i2o_driver *drv,
		       int tcntxt, u32 evt_mask)
{
	struct i2o_controller *c = dev->iop;
	struct i2o_message *msg;

	msg = i2o_msg_get_wait(c, I2O_TIMEOUT_MESSAGE_GET);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	msg->u.head[0] = cpu_to_le32(FIVE_WORD_MSG_SIZE | SGL_OFFSET_0);
	msg->u.head[1] =
	    cpu_to_le32(I2O_CMD_UTIL_EVT_REGISTER << 24 | HOST_TID << 12 | dev->
			lct_data.tid);
	msg->u.s.icntxt = cpu_to_le32(drv->context);
	msg->u.s.tcntxt = cpu_to_le32(tcntxt);
	msg->body[0] = cpu_to_le32(evt_mask);

	i2o_msg_post(c, msg);

	return 0;
};

/**
 *	i2o_iop_init - I2O main initialization function
 *
 *	Initialize the I2O drivers (OSM) functions, register the Executive OSM,
 *	initialize the I2O PCI part and finally initialize I2O device stuff.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int __init i2o_iop_init(void)
{
	int rc = 0;

	printk(KERN_INFO OSM_DESCRIPTION " v" OSM_VERSION "\n");

	if ((rc = i2o_driver_init()))
		goto exit;

	if ((rc = i2o_exec_init()))
		goto driver_exit;

	if ((rc = i2o_pci_init()))
		goto exec_exit;

	return 0;

      exec_exit:
	i2o_exec_exit();

      driver_exit:
	i2o_driver_exit();

      exit:
	return rc;
}

/**
 *	i2o_iop_exit - I2O main exit function
 *
 *	Removes I2O controllers from PCI subsystem and shut down OSMs.
 */
static void __exit i2o_iop_exit(void)
{
	i2o_pci_exit();
	i2o_exec_exit();
	i2o_driver_exit();
};

module_init(i2o_iop_init);
module_exit(i2o_iop_exit);

MODULE_AUTHOR("Red Hat Software");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(OSM_DESCRIPTION);
MODULE_VERSION(OSM_VERSION);

#if BITS_PER_LONG == 64
EXPORT_SYMBOL(i2o_cntxt_list_add);
EXPORT_SYMBOL(i2o_cntxt_list_get);
EXPORT_SYMBOL(i2o_cntxt_list_remove);
EXPORT_SYMBOL(i2o_cntxt_list_get_ptr);
#endif
EXPORT_SYMBOL(i2o_msg_get_wait);
EXPORT_SYMBOL(i2o_find_iop);
EXPORT_SYMBOL(i2o_iop_find_device);
EXPORT_SYMBOL(i2o_event_register);
EXPORT_SYMBOL(i2o_status_get);
EXPORT_SYMBOL(i2o_controllers);
