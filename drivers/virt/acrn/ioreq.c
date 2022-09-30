// SPDX-License-Identifier: GPL-2.0
/*
 * ACRN_HSM: Handle I/O requests
 *
 * Copyright (C) 2020 Intel Corporation. All rights reserved.
 *
 * Authors:
 *	Jason Chen CJ <jason.cj.chen@intel.com>
 *	Fengwei Yin <fengwei.yin@intel.com>
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <asm/acrn.h>

#include "acrn_drv.h"

static void ioreq_pause(void);
static void ioreq_resume(void);

static void ioreq_dispatcher(struct work_struct *work);
static struct workqueue_struct *ioreq_wq;
static DECLARE_WORK(ioreq_work, ioreq_dispatcher);

static inline bool has_pending_request(struct acrn_ioreq_client *client)
{
	return !bitmap_empty(client->ioreqs_map, ACRN_IO_REQUEST_MAX);
}

static inline bool is_destroying(struct acrn_ioreq_client *client)
{
	return test_bit(ACRN_IOREQ_CLIENT_DESTROYING, &client->flags);
}

static int ioreq_complete_request(struct acrn_vm *vm, u16 vcpu,
				  struct acrn_io_request *acrn_req)
{
	bool polling_mode;
	int ret = 0;

	polling_mode = acrn_req->completion_polling;
	/* Add barrier() to make sure the writes are done before completion */
	smp_store_release(&acrn_req->processed, ACRN_IOREQ_STATE_COMPLETE);

	/*
	 * To fulfill the requirement of real-time in several industry
	 * scenarios, like automotive, ACRN can run under the partition mode,
	 * in which User VMs and Service VM are bound to dedicated CPU cores.
	 * Polling mode of handling the I/O request is introduced to achieve a
	 * faster I/O request handling. In polling mode, the hypervisor polls
	 * I/O request's completion. Once an I/O request is marked as
	 * ACRN_IOREQ_STATE_COMPLETE, hypervisor resumes from the polling point
	 * to continue the I/O request flow. Thus, the completion notification
	 * from HSM of I/O request is not needed.  Please note,
	 * completion_polling needs to be read before the I/O request being
	 * marked as ACRN_IOREQ_STATE_COMPLETE to avoid racing with the
	 * hypervisor.
	 */
	if (!polling_mode) {
		ret = hcall_notify_req_finish(vm->vmid, vcpu);
		if (ret < 0)
			dev_err(acrn_dev.this_device,
				"Notify I/O request finished failed!\n");
	}

	return ret;
}

static int acrn_ioreq_complete_request(struct acrn_ioreq_client *client,
				       u16 vcpu,
				       struct acrn_io_request *acrn_req)
{
	int ret;

	if (vcpu >= client->vm->vcpu_num)
		return -EINVAL;

	clear_bit(vcpu, client->ioreqs_map);
	if (!acrn_req) {
		acrn_req = (struct acrn_io_request *)client->vm->ioreq_buf;
		acrn_req += vcpu;
	}

	ret = ioreq_complete_request(client->vm, vcpu, acrn_req);

	return ret;
}

int acrn_ioreq_request_default_complete(struct acrn_vm *vm, u16 vcpu)
{
	int ret = 0;

	spin_lock_bh(&vm->ioreq_clients_lock);
	if (vm->default_client)
		ret = acrn_ioreq_complete_request(vm->default_client,
						  vcpu, NULL);
	spin_unlock_bh(&vm->ioreq_clients_lock);

	return ret;
}

/**
 * acrn_ioreq_range_add() - Add an iorange monitored by an ioreq client
 * @client:	The ioreq client
 * @type:	Type (ACRN_IOREQ_TYPE_MMIO or ACRN_IOREQ_TYPE_PORTIO)
 * @start:	Start address of iorange
 * @end:	End address of iorange
 *
 * Return: 0 on success, <0 on error
 */
int acrn_ioreq_range_add(struct acrn_ioreq_client *client,
			 u32 type, u64 start, u64 end)
{
	struct acrn_ioreq_range *range;

	if (end < start) {
		dev_err(acrn_dev.this_device,
			"Invalid IO range [0x%llx,0x%llx]\n", start, end);
		return -EINVAL;
	}

	range = kzalloc(sizeof(*range), GFP_KERNEL);
	if (!range)
		return -ENOMEM;

	range->type = type;
	range->start = start;
	range->end = end;

	write_lock_bh(&client->range_lock);
	list_add(&range->list, &client->range_list);
	write_unlock_bh(&client->range_lock);

	return 0;
}

/**
 * acrn_ioreq_range_del() - Del an iorange monitored by an ioreq client
 * @client:	The ioreq client
 * @type:	Type (ACRN_IOREQ_TYPE_MMIO or ACRN_IOREQ_TYPE_PORTIO)
 * @start:	Start address of iorange
 * @end:	End address of iorange
 */
void acrn_ioreq_range_del(struct acrn_ioreq_client *client,
			  u32 type, u64 start, u64 end)
{
	struct acrn_ioreq_range *range;

	write_lock_bh(&client->range_lock);
	list_for_each_entry(range, &client->range_list, list) {
		if (type == range->type &&
		    start == range->start &&
		    end == range->end) {
			list_del(&range->list);
			kfree(range);
			break;
		}
	}
	write_unlock_bh(&client->range_lock);
}

/*
 * ioreq_task() is the execution entity of handler thread of an I/O client.
 * The handler callback of the I/O client is called within the handler thread.
 */
static int ioreq_task(void *data)
{
	struct acrn_ioreq_client *client = data;
	struct acrn_io_request *req;
	unsigned long *ioreqs_map;
	int vcpu, ret;

	/*
	 * Lockless access to ioreqs_map is safe, because
	 * 1) set_bit() and clear_bit() are atomic operations.
	 * 2) I/O requests arrives serialized. The access flow of ioreqs_map is:
	 *	set_bit() - in ioreq_work handler
	 *	Handler callback handles corresponding I/O request
	 *	clear_bit() - in handler thread (include ACRN userspace)
	 *	Mark corresponding I/O request completed
	 *	Loop again if a new I/O request occurs
	 */
	ioreqs_map = client->ioreqs_map;
	while (!kthread_should_stop()) {
		acrn_ioreq_client_wait(client);
		while (has_pending_request(client)) {
			vcpu = find_first_bit(ioreqs_map, client->vm->vcpu_num);
			req = client->vm->ioreq_buf->req_slot + vcpu;
			ret = client->handler(client, req);
			if (ret < 0) {
				dev_err(acrn_dev.this_device,
					"IO handle failure: %d\n", ret);
				break;
			}
			acrn_ioreq_complete_request(client, vcpu, req);
		}
	}

	return 0;
}

/*
 * For the non-default I/O clients, give them chance to complete the current
 * I/O requests if there are any. For the default I/O client, it is safe to
 * clear all pending I/O requests because the clearing request is from ACRN
 * userspace.
 */
void acrn_ioreq_request_clear(struct acrn_vm *vm)
{
	struct acrn_ioreq_client *client;
	bool has_pending = false;
	unsigned long vcpu;
	int retry = 10;

	/*
	 * IO requests of this VM will be completed directly in
	 * acrn_ioreq_dispatch if ACRN_VM_FLAG_CLEARING_IOREQ flag is set.
	 */
	set_bit(ACRN_VM_FLAG_CLEARING_IOREQ, &vm->flags);

	/*
	 * acrn_ioreq_request_clear is only called in VM reset case. Simply
	 * wait 100ms in total for the IO requests' completion.
	 */
	do {
		spin_lock_bh(&vm->ioreq_clients_lock);
		list_for_each_entry(client, &vm->ioreq_clients, list) {
			has_pending = has_pending_request(client);
			if (has_pending)
				break;
		}
		spin_unlock_bh(&vm->ioreq_clients_lock);

		if (has_pending)
			schedule_timeout_interruptible(HZ / 100);
	} while (has_pending && --retry > 0);
	if (retry == 0)
		dev_warn(acrn_dev.this_device,
			 "%s cannot flush pending request!\n", client->name);

	/* Clear all ioreqs belonging to the default client */
	spin_lock_bh(&vm->ioreq_clients_lock);
	client = vm->default_client;
	if (client) {
		for_each_set_bit(vcpu, client->ioreqs_map, ACRN_IO_REQUEST_MAX)
			acrn_ioreq_complete_request(client, vcpu, NULL);
	}
	spin_unlock_bh(&vm->ioreq_clients_lock);

	/* Clear ACRN_VM_FLAG_CLEARING_IOREQ flag after the clearing */
	clear_bit(ACRN_VM_FLAG_CLEARING_IOREQ, &vm->flags);
}

int acrn_ioreq_client_wait(struct acrn_ioreq_client *client)
{
	if (client->is_default) {
		/*
		 * In the default client, a user space thread waits on the
		 * waitqueue. The is_destroying() check is used to notify user
		 * space the client is going to be destroyed.
		 */
		wait_event_interruptible(client->wq,
					 has_pending_request(client) ||
					 is_destroying(client));
		if (is_destroying(client))
			return -ENODEV;
	} else {
		wait_event_interruptible(client->wq,
					 has_pending_request(client) ||
					 kthread_should_stop());
	}

	return 0;
}

static bool is_cfg_addr(struct acrn_io_request *req)
{
	return ((req->type == ACRN_IOREQ_TYPE_PORTIO) &&
		(req->reqs.pio_request.address == 0xcf8));
}

static bool is_cfg_data(struct acrn_io_request *req)
{
	return ((req->type == ACRN_IOREQ_TYPE_PORTIO) &&
		((req->reqs.pio_request.address >= 0xcfc) &&
		 (req->reqs.pio_request.address < (0xcfc + 4))));
}

/* The low 8-bit of supported pci_reg addr.*/
#define PCI_LOWREG_MASK  0xFC
/* The high 4-bit of supported pci_reg addr */
#define PCI_HIGHREG_MASK 0xF00
/* Max number of supported functions */
#define PCI_FUNCMAX	7
/* Max number of supported slots */
#define PCI_SLOTMAX	31
/* Max number of supported buses */
#define PCI_BUSMAX	255
#define CONF1_ENABLE	0x80000000UL
/*
 * A PCI configuration space access via PIO 0xCF8 and 0xCFC normally has two
 * following steps:
 *   1) writes address into 0xCF8 port
 *   2) accesses data in/from 0xCFC
 * This function combines such paired PCI configuration space I/O requests into
 * one ACRN_IOREQ_TYPE_PCICFG type I/O request and continues the processing.
 */
static bool handle_cf8cfc(struct acrn_vm *vm,
			  struct acrn_io_request *req, u16 vcpu)
{
	int offset, pci_cfg_addr, pci_reg;
	bool is_handled = false;

	if (is_cfg_addr(req)) {
		WARN_ON(req->reqs.pio_request.size != 4);
		if (req->reqs.pio_request.direction == ACRN_IOREQ_DIR_WRITE)
			vm->pci_conf_addr = req->reqs.pio_request.value;
		else
			req->reqs.pio_request.value = vm->pci_conf_addr;
		is_handled = true;
	} else if (is_cfg_data(req)) {
		if (!(vm->pci_conf_addr & CONF1_ENABLE)) {
			if (req->reqs.pio_request.direction ==
					ACRN_IOREQ_DIR_READ)
				req->reqs.pio_request.value = 0xffffffff;
			is_handled = true;
		} else {
			offset = req->reqs.pio_request.address - 0xcfc;

			req->type = ACRN_IOREQ_TYPE_PCICFG;
			pci_cfg_addr = vm->pci_conf_addr;
			req->reqs.pci_request.bus =
					(pci_cfg_addr >> 16) & PCI_BUSMAX;
			req->reqs.pci_request.dev =
					(pci_cfg_addr >> 11) & PCI_SLOTMAX;
			req->reqs.pci_request.func =
					(pci_cfg_addr >> 8) & PCI_FUNCMAX;
			pci_reg = (pci_cfg_addr & PCI_LOWREG_MASK) +
				   ((pci_cfg_addr >> 16) & PCI_HIGHREG_MASK);
			req->reqs.pci_request.reg = pci_reg + offset;
		}
	}

	if (is_handled)
		ioreq_complete_request(vm, vcpu, req);

	return is_handled;
}

static bool in_range(struct acrn_ioreq_range *range,
		     struct acrn_io_request *req)
{
	bool ret = false;

	if (range->type == req->type) {
		switch (req->type) {
		case ACRN_IOREQ_TYPE_MMIO:
			if (req->reqs.mmio_request.address >= range->start &&
			    (req->reqs.mmio_request.address +
			     req->reqs.mmio_request.size - 1) <= range->end)
				ret = true;
			break;
		case ACRN_IOREQ_TYPE_PORTIO:
			if (req->reqs.pio_request.address >= range->start &&
			    (req->reqs.pio_request.address +
			     req->reqs.pio_request.size - 1) <= range->end)
				ret = true;
			break;
		default:
			break;
		}
	}

	return ret;
}

static struct acrn_ioreq_client *find_ioreq_client(struct acrn_vm *vm,
						   struct acrn_io_request *req)
{
	struct acrn_ioreq_client *client, *found = NULL;
	struct acrn_ioreq_range *range;

	lockdep_assert_held(&vm->ioreq_clients_lock);

	list_for_each_entry(client, &vm->ioreq_clients, list) {
		read_lock_bh(&client->range_lock);
		list_for_each_entry(range, &client->range_list, list) {
			if (in_range(range, req)) {
				found = client;
				break;
			}
		}
		read_unlock_bh(&client->range_lock);
		if (found)
			break;
	}
	return found ? found : vm->default_client;
}

/**
 * acrn_ioreq_client_create() - Create an ioreq client
 * @vm:		The VM that this client belongs to
 * @handler:	The ioreq_handler of ioreq client acrn_hsm will create a kernel
 *		thread and call the handler to handle I/O requests.
 * @priv:	Private data for the handler
 * @is_default:	If it is the default client
 * @name:	The name of ioreq client
 *
 * Return: acrn_ioreq_client pointer on success, NULL on error
 */
struct acrn_ioreq_client *acrn_ioreq_client_create(struct acrn_vm *vm,
						   ioreq_handler_t handler,
						   void *priv, bool is_default,
						   const char *name)
{
	struct acrn_ioreq_client *client;

	if (!handler && !is_default) {
		dev_dbg(acrn_dev.this_device,
			"Cannot create non-default client w/o handler!\n");
		return NULL;
	}
	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return NULL;

	client->handler = handler;
	client->vm = vm;
	client->priv = priv;
	client->is_default = is_default;
	if (name)
		strncpy(client->name, name, sizeof(client->name) - 1);
	rwlock_init(&client->range_lock);
	INIT_LIST_HEAD(&client->range_list);
	init_waitqueue_head(&client->wq);

	if (client->handler) {
		client->thread = kthread_run(ioreq_task, client, "VM%u-%s",
					     client->vm->vmid, client->name);
		if (IS_ERR(client->thread)) {
			kfree(client);
			return NULL;
		}
	}

	spin_lock_bh(&vm->ioreq_clients_lock);
	if (is_default)
		vm->default_client = client;
	else
		list_add(&client->list, &vm->ioreq_clients);
	spin_unlock_bh(&vm->ioreq_clients_lock);

	dev_dbg(acrn_dev.this_device, "Created ioreq client %s.\n", name);
	return client;
}

/**
 * acrn_ioreq_client_destroy() - Destroy an ioreq client
 * @client:	The ioreq client
 */
void acrn_ioreq_client_destroy(struct acrn_ioreq_client *client)
{
	struct acrn_ioreq_range *range, *next;
	struct acrn_vm *vm = client->vm;

	dev_dbg(acrn_dev.this_device,
		"Destroy ioreq client %s.\n", client->name);
	ioreq_pause();
	set_bit(ACRN_IOREQ_CLIENT_DESTROYING, &client->flags);
	if (client->is_default)
		wake_up_interruptible(&client->wq);
	else
		kthread_stop(client->thread);

	spin_lock_bh(&vm->ioreq_clients_lock);
	if (client->is_default)
		vm->default_client = NULL;
	else
		list_del(&client->list);
	spin_unlock_bh(&vm->ioreq_clients_lock);

	write_lock_bh(&client->range_lock);
	list_for_each_entry_safe(range, next, &client->range_list, list) {
		list_del(&range->list);
		kfree(range);
	}
	write_unlock_bh(&client->range_lock);
	kfree(client);

	ioreq_resume();
}

static int acrn_ioreq_dispatch(struct acrn_vm *vm)
{
	struct acrn_ioreq_client *client;
	struct acrn_io_request *req;
	int i;

	for (i = 0; i < vm->vcpu_num; i++) {
		req = vm->ioreq_buf->req_slot + i;

		/* barrier the read of processed of acrn_io_request */
		if (smp_load_acquire(&req->processed) ==
				     ACRN_IOREQ_STATE_PENDING) {
			/* Complete the IO request directly in clearing stage */
			if (test_bit(ACRN_VM_FLAG_CLEARING_IOREQ, &vm->flags)) {
				ioreq_complete_request(vm, i, req);
				continue;
			}
			if (handle_cf8cfc(vm, req, i))
				continue;

			spin_lock_bh(&vm->ioreq_clients_lock);
			client = find_ioreq_client(vm, req);
			if (!client) {
				dev_err(acrn_dev.this_device,
					"Failed to find ioreq client!\n");
				spin_unlock_bh(&vm->ioreq_clients_lock);
				return -EINVAL;
			}
			if (!client->is_default)
				req->kernel_handled = 1;
			else
				req->kernel_handled = 0;
			/*
			 * Add barrier() to make sure the writes are done
			 * before setting ACRN_IOREQ_STATE_PROCESSING
			 */
			smp_store_release(&req->processed,
					  ACRN_IOREQ_STATE_PROCESSING);
			set_bit(i, client->ioreqs_map);
			wake_up_interruptible(&client->wq);
			spin_unlock_bh(&vm->ioreq_clients_lock);
		}
	}

	return 0;
}

static void ioreq_dispatcher(struct work_struct *work)
{
	struct acrn_vm *vm;

	read_lock(&acrn_vm_list_lock);
	list_for_each_entry(vm, &acrn_vm_list, list) {
		if (!vm->ioreq_buf)
			break;
		acrn_ioreq_dispatch(vm);
	}
	read_unlock(&acrn_vm_list_lock);
}

static void ioreq_intr_handler(void)
{
	queue_work(ioreq_wq, &ioreq_work);
}

static void ioreq_pause(void)
{
	/* Flush and unarm the handler to ensure no I/O requests pending */
	acrn_remove_intr_handler();
	drain_workqueue(ioreq_wq);
}

static void ioreq_resume(void)
{
	/* Schedule after enabling in case other clients miss interrupt */
	acrn_setup_intr_handler(ioreq_intr_handler);
	queue_work(ioreq_wq, &ioreq_work);
}

int acrn_ioreq_intr_setup(void)
{
	acrn_setup_intr_handler(ioreq_intr_handler);
	ioreq_wq = alloc_workqueue("ioreq_wq",
				   WQ_HIGHPRI | WQ_MEM_RECLAIM | WQ_UNBOUND, 1);
	if (!ioreq_wq) {
		dev_err(acrn_dev.this_device, "Failed to alloc workqueue!\n");
		acrn_remove_intr_handler();
		return -ENOMEM;
	}
	return 0;
}

void acrn_ioreq_intr_remove(void)
{
	if (ioreq_wq)
		destroy_workqueue(ioreq_wq);
	acrn_remove_intr_handler();
}

int acrn_ioreq_init(struct acrn_vm *vm, u64 buf_vma)
{
	struct acrn_ioreq_buffer *set_buffer;
	struct page *page;
	int ret;

	if (vm->ioreq_buf)
		return -EEXIST;

	set_buffer = kzalloc(sizeof(*set_buffer), GFP_KERNEL);
	if (!set_buffer)
		return -ENOMEM;

	ret = pin_user_pages_fast(buf_vma, 1,
				  FOLL_WRITE | FOLL_LONGTERM, &page);
	if (unlikely(ret != 1) || !page) {
		dev_err(acrn_dev.this_device, "Failed to pin ioreq page!\n");
		ret = -EFAULT;
		goto free_buf;
	}

	vm->ioreq_buf = page_address(page);
	vm->ioreq_page = page;
	set_buffer->ioreq_buf = page_to_phys(page);
	ret = hcall_set_ioreq_buffer(vm->vmid, virt_to_phys(set_buffer));
	if (ret < 0) {
		dev_err(acrn_dev.this_device, "Failed to init ioreq buffer!\n");
		unpin_user_page(page);
		vm->ioreq_buf = NULL;
		goto free_buf;
	}

	dev_dbg(acrn_dev.this_device,
		"Init ioreq buffer %pK!\n", vm->ioreq_buf);
	ret = 0;
free_buf:
	kfree(set_buffer);
	return ret;
}

void acrn_ioreq_deinit(struct acrn_vm *vm)
{
	struct acrn_ioreq_client *client, *next;

	dev_dbg(acrn_dev.this_device,
		"Deinit ioreq buffer %pK!\n", vm->ioreq_buf);
	/* Destroy all clients belonging to this VM */
	list_for_each_entry_safe(client, next, &vm->ioreq_clients, list)
		acrn_ioreq_client_destroy(client);
	if (vm->default_client)
		acrn_ioreq_client_destroy(vm->default_client);

	if (vm->ioreq_buf && vm->ioreq_page) {
		unpin_user_page(vm->ioreq_page);
		vm->ioreq_buf = NULL;
	}
}
