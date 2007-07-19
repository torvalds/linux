/*
 * IEEE 1394 for Linux
 *
 * Copyright (C) 1999 Andreas E. Bombe
 *
 * This code is licensed under the GPL.  See the file COPYING in the root
 * directory of the kernel sources for details.
 *
 *
 * Contributions:
 *
 * Christian Toegel <christian.toegel@gmx.at>
 *        unregister address space
 *
 * Manfred Weihs <weihs@ict.tuwien.ac.at>
 *        unregister address space
 *
 */

#include <linux/slab.h>
#include <linux/list.h>
#include <linux/bitops.h>

#include "ieee1394.h"
#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "highlevel.h"
#include "nodemgr.h"


struct hl_host_info {
	struct list_head list;
	struct hpsb_host *host;
	size_t size;
	unsigned long key;
	void *data;
};


static LIST_HEAD(hl_drivers);
static DECLARE_RWSEM(hl_drivers_sem);

static LIST_HEAD(hl_irqs);
static DEFINE_RWLOCK(hl_irqs_lock);

static DEFINE_RWLOCK(addr_space_lock);

/* addr_space list will have zero and max already included as bounds */
static struct hpsb_address_ops dummy_ops = { NULL, NULL, NULL, NULL };
static struct hpsb_address_serve dummy_zero_addr, dummy_max_addr;


static struct hl_host_info *hl_get_hostinfo(struct hpsb_highlevel *hl,
					    struct hpsb_host *host)
{
	struct hl_host_info *hi = NULL;

	if (!hl || !host)
		return NULL;

	read_lock(&hl->host_info_lock);
	list_for_each_entry(hi, &hl->host_info_list, list) {
		if (hi->host == host) {
			read_unlock(&hl->host_info_lock);
			return hi;
		}
	}
	read_unlock(&hl->host_info_lock);
	return NULL;
}

/**
 * hpsb_get_hostinfo - retrieve a hostinfo pointer bound to this driver/host
 *
 * Returns a per @host and @hl driver data structure that was previously stored
 * by hpsb_create_hostinfo.
 */
void *hpsb_get_hostinfo(struct hpsb_highlevel *hl, struct hpsb_host *host)
{
	struct hl_host_info *hi = hl_get_hostinfo(hl, host);

	return hi ? hi->data : NULL;
}

/**
 * hpsb_create_hostinfo - allocate a hostinfo pointer bound to this driver/host
 *
 * Allocate a hostinfo pointer backed by memory with @data_size and bind it to
 * to this @hl driver and @host.  If @data_size is zero, then the return here is
 * only valid for error checking.
 */
void *hpsb_create_hostinfo(struct hpsb_highlevel *hl, struct hpsb_host *host,
			   size_t data_size)
{
	struct hl_host_info *hi;
	void *data;
	unsigned long flags;

	hi = hl_get_hostinfo(hl, host);
	if (hi) {
		HPSB_ERR("%s called hpsb_create_hostinfo when hostinfo already"
			 " exists", hl->name);
		return NULL;
	}

	hi = kzalloc(sizeof(*hi) + data_size, GFP_ATOMIC);
	if (!hi)
		return NULL;

	if (data_size) {
		data = hi->data = hi + 1;
		hi->size = data_size;
	} else
		data = hi;

	hi->host = host;

	write_lock_irqsave(&hl->host_info_lock, flags);
	list_add_tail(&hi->list, &hl->host_info_list);
	write_unlock_irqrestore(&hl->host_info_lock, flags);

	return data;
}

/**
 * hpsb_set_hostinfo - set the hostinfo pointer to something useful
 *
 * Usually follows a call to hpsb_create_hostinfo, where the size is 0.
 */
int hpsb_set_hostinfo(struct hpsb_highlevel *hl, struct hpsb_host *host,
		      void *data)
{
	struct hl_host_info *hi;

	hi = hl_get_hostinfo(hl, host);
	if (hi) {
		if (!hi->size && !hi->data) {
			hi->data = data;
			return 0;
		} else
			HPSB_ERR("%s called hpsb_set_hostinfo when hostinfo "
				 "already has data", hl->name);
	} else
		HPSB_ERR("%s called hpsb_set_hostinfo when no hostinfo exists",
			 hl->name);
	return -EINVAL;
}

/**
 * hpsb_destroy_hostinfo - free and remove a hostinfo pointer
 *
 * Free and remove the hostinfo pointer bound to this @hl driver and @host.
 */
void hpsb_destroy_hostinfo(struct hpsb_highlevel *hl, struct hpsb_host *host)
{
	struct hl_host_info *hi;

	hi = hl_get_hostinfo(hl, host);
	if (hi) {
		unsigned long flags;
		write_lock_irqsave(&hl->host_info_lock, flags);
		list_del(&hi->list);
		write_unlock_irqrestore(&hl->host_info_lock, flags);
		kfree(hi);
	}
	return;
}

/**
 * hpsb_set_hostinfo_key - set an alternate lookup key for an hostinfo
 *
 * Sets an alternate lookup key for the hostinfo bound to this @hl driver and
 * @host.
 */
void hpsb_set_hostinfo_key(struct hpsb_highlevel *hl, struct hpsb_host *host,
			   unsigned long key)
{
	struct hl_host_info *hi;

	hi = hl_get_hostinfo(hl, host);
	if (hi)
		hi->key = key;
	return;
}

/**
 * hpsb_get_hostinfo_bykey - retrieve a hostinfo pointer by its alternate key
 */
void *hpsb_get_hostinfo_bykey(struct hpsb_highlevel *hl, unsigned long key)
{
	struct hl_host_info *hi;
	void *data = NULL;

	if (!hl)
		return NULL;

	read_lock(&hl->host_info_lock);
	list_for_each_entry(hi, &hl->host_info_list, list) {
		if (hi->key == key) {
			data = hi->data;
			break;
		}
	}
	read_unlock(&hl->host_info_lock);
	return data;
}

static int highlevel_for_each_host_reg(struct hpsb_host *host, void *__data)
{
	struct hpsb_highlevel *hl = __data;

	hl->add_host(host);

	if (host->update_config_rom && hpsb_update_config_rom_image(host) < 0)
		HPSB_ERR("Failed to generate Configuration ROM image for host "
			 "%s-%d", hl->name, host->id);
	return 0;
}

/**
 * hpsb_register_highlevel - register highlevel driver
 *
 * The name pointer in @hl has to stay valid at all times because the string is
 * not copied.
 */
void hpsb_register_highlevel(struct hpsb_highlevel *hl)
{
	unsigned long flags;

	INIT_LIST_HEAD(&hl->addr_list);
	INIT_LIST_HEAD(&hl->host_info_list);

	rwlock_init(&hl->host_info_lock);

	down_write(&hl_drivers_sem);
	list_add_tail(&hl->hl_list, &hl_drivers);
	up_write(&hl_drivers_sem);

	write_lock_irqsave(&hl_irqs_lock, flags);
	list_add_tail(&hl->irq_list, &hl_irqs);
	write_unlock_irqrestore(&hl_irqs_lock, flags);

	if (hl->add_host)
		nodemgr_for_each_host(hl, highlevel_for_each_host_reg);
	return;
}

static void __delete_addr(struct hpsb_address_serve *as)
{
	list_del(&as->host_list);
	list_del(&as->hl_list);
	kfree(as);
}

static void __unregister_host(struct hpsb_highlevel *hl, struct hpsb_host *host,
			      int update_cr)
{
	unsigned long flags;
	struct list_head *lh, *next;
	struct hpsb_address_serve *as;

	/* First, let the highlevel driver unreg */
	if (hl->remove_host)
		hl->remove_host(host);

	/* Remove any addresses that are matched for this highlevel driver
	 * and this particular host. */
	write_lock_irqsave(&addr_space_lock, flags);
	list_for_each_safe (lh, next, &hl->addr_list) {
		as = list_entry(lh, struct hpsb_address_serve, hl_list);
		if (as->host == host)
			__delete_addr(as);
	}
	write_unlock_irqrestore(&addr_space_lock, flags);

	/* Now update the config-rom to reflect anything removed by the
	 * highlevel driver. */
	if (update_cr && host->update_config_rom &&
	    hpsb_update_config_rom_image(host) < 0)
		HPSB_ERR("Failed to generate Configuration ROM image for host "
			 "%s-%d", hl->name, host->id);

	/* Finally remove all the host info associated between these two. */
	hpsb_destroy_hostinfo(hl, host);
}

static int highlevel_for_each_host_unreg(struct hpsb_host *host, void *__data)
{
	struct hpsb_highlevel *hl = __data;

	__unregister_host(hl, host, 1);
	return 0;
}

/**
 * hpsb_unregister_highlevel - unregister highlevel driver
 */
void hpsb_unregister_highlevel(struct hpsb_highlevel *hl)
{
	unsigned long flags;

	write_lock_irqsave(&hl_irqs_lock, flags);
	list_del(&hl->irq_list);
	write_unlock_irqrestore(&hl_irqs_lock, flags);

	down_write(&hl_drivers_sem);
	list_del(&hl->hl_list);
	up_write(&hl_drivers_sem);

	nodemgr_for_each_host(hl, highlevel_for_each_host_unreg);
}

/**
 * hpsb_allocate_and_register_addrspace - alloc' and reg' a host address space
 *
 * @start and @end are 48 bit pointers and have to be quadlet aligned.
 * @end points to the first address behind the handled addresses.  This
 * function can be called multiple times for a single hpsb_highlevel @hl to
 * implement sparse register sets.  The requested region must not overlap any
 * previously allocated region, otherwise registering will fail.
 *
 * It returns true for successful allocation.  Address spaces can be
 * unregistered with hpsb_unregister_addrspace.  All remaining address spaces
 * are automatically deallocated together with the hpsb_highlevel @hl.
 */
u64 hpsb_allocate_and_register_addrspace(struct hpsb_highlevel *hl,
					 struct hpsb_host *host,
					 struct hpsb_address_ops *ops,
					 u64 size, u64 alignment,
					 u64 start, u64 end)
{
	struct hpsb_address_serve *as, *a1, *a2;
	struct list_head *entry;
	u64 retval = CSR1212_INVALID_ADDR_SPACE;
	unsigned long flags;
	u64 align_mask = ~(alignment - 1);

	if ((alignment & 3) || (alignment > 0x800000000000ULL) ||
	    (hweight64(alignment) != 1)) {
		HPSB_ERR("%s called with invalid alignment: 0x%048llx",
			 __FUNCTION__, (unsigned long long)alignment);
		return retval;
	}

	/* default range,
	 * avoids controller's posted write area (see OHCI 1.1 clause 1.5) */
	if (start == CSR1212_INVALID_ADDR_SPACE &&
	    end   == CSR1212_INVALID_ADDR_SPACE) {
		start = host->middle_addr_space;
		end   = CSR1212_ALL_SPACE_END;
	}

	if (((start|end) & ~align_mask) || (start >= end) ||
	    (end > CSR1212_ALL_SPACE_END)) {
		HPSB_ERR("%s called with invalid addresses "
			 "(start = %012Lx  end = %012Lx)", __FUNCTION__,
			 (unsigned long long)start,(unsigned long long)end);
		return retval;
	}

	as = kmalloc(sizeof(*as), GFP_KERNEL);
	if (!as)
		return retval;

	INIT_LIST_HEAD(&as->host_list);
	INIT_LIST_HEAD(&as->hl_list);
	as->op = ops;
	as->host = host;

	write_lock_irqsave(&addr_space_lock, flags);
	list_for_each(entry, &host->addr_space) {
		u64 a1sa, a1ea;
		u64 a2sa, a2ea;

		a1 = list_entry(entry, struct hpsb_address_serve, host_list);
		a2 = list_entry(entry->next, struct hpsb_address_serve,
				host_list);

		a1sa = a1->start & align_mask;
		a1ea = (a1->end + alignment -1) & align_mask;
		a2sa = a2->start & align_mask;
		a2ea = (a2->end + alignment -1) & align_mask;

		if ((a2sa - a1ea >= size) && (a2sa - start >= size) &&
		    (a2sa > start)) {
			as->start = max(start, a1ea);
			as->end = as->start + size;
			list_add(&as->host_list, entry);
			list_add_tail(&as->hl_list, &hl->addr_list);
			retval = as->start;
			break;
		}
	}
	write_unlock_irqrestore(&addr_space_lock, flags);

	if (retval == CSR1212_INVALID_ADDR_SPACE)
		kfree(as);
	return retval;
}

/**
 * hpsb_register_addrspace - register a host address space
 *
 * @start and @end are 48 bit pointers and have to be quadlet aligned.
 * @end points to the first address behind the handled addresses.  This
 * function can be called multiple times for a single hpsb_highlevel @hl to
 * implement sparse register sets.  The requested region must not overlap any
 * previously allocated region, otherwise registering will fail.
 *
 * It returns true for successful allocation.  Address spaces can be
 * unregistered with hpsb_unregister_addrspace.  All remaining address spaces
 * are automatically deallocated together with the hpsb_highlevel @hl.
 */
int hpsb_register_addrspace(struct hpsb_highlevel *hl, struct hpsb_host *host,
			    struct hpsb_address_ops *ops, u64 start, u64 end)
{
	struct hpsb_address_serve *as;
	struct list_head *lh;
	int retval = 0;
	unsigned long flags;

	if (((start|end) & 3) || (start >= end) ||
	    (end > CSR1212_ALL_SPACE_END)) {
		HPSB_ERR("%s called with invalid addresses", __FUNCTION__);
		return 0;
	}

	as = kmalloc(sizeof(*as), GFP_ATOMIC);
	if (!as)
		return 0;

	INIT_LIST_HEAD(&as->host_list);
	INIT_LIST_HEAD(&as->hl_list);
	as->op = ops;
	as->start = start;
	as->end = end;
	as->host = host;

	write_lock_irqsave(&addr_space_lock, flags);
	list_for_each(lh, &host->addr_space) {
		struct hpsb_address_serve *as_this =
			list_entry(lh, struct hpsb_address_serve, host_list);
		struct hpsb_address_serve *as_next =
			list_entry(lh->next, struct hpsb_address_serve,
				   host_list);

		if (as_this->end > as->start)
			break;

		if (as_next->start >= as->end) {
			list_add(&as->host_list, lh);
			list_add_tail(&as->hl_list, &hl->addr_list);
			retval = 1;
			break;
		}
	}
	write_unlock_irqrestore(&addr_space_lock, flags);

	if (retval == 0)
		kfree(as);
	return retval;
}

int hpsb_unregister_addrspace(struct hpsb_highlevel *hl, struct hpsb_host *host,
			      u64 start)
{
	int retval = 0;
	struct hpsb_address_serve *as;
	struct list_head *lh, *next;
	unsigned long flags;

	write_lock_irqsave(&addr_space_lock, flags);
	list_for_each_safe (lh, next, &hl->addr_list) {
		as = list_entry(lh, struct hpsb_address_serve, hl_list);
		if (as->start == start && as->host == host) {
			__delete_addr(as);
			retval = 1;
			break;
		}
	}
	write_unlock_irqrestore(&addr_space_lock, flags);
	return retval;
}

static void init_hpsb_highlevel(struct hpsb_host *host)
{
	INIT_LIST_HEAD(&dummy_zero_addr.host_list);
	INIT_LIST_HEAD(&dummy_zero_addr.hl_list);
	INIT_LIST_HEAD(&dummy_max_addr.host_list);
	INIT_LIST_HEAD(&dummy_max_addr.hl_list);

	dummy_zero_addr.op = dummy_max_addr.op = &dummy_ops;

	dummy_zero_addr.start = dummy_zero_addr.end = 0;
	dummy_max_addr.start = dummy_max_addr.end = ((u64) 1) << 48;

	list_add_tail(&dummy_zero_addr.host_list, &host->addr_space);
	list_add_tail(&dummy_max_addr.host_list, &host->addr_space);
}

void highlevel_add_host(struct hpsb_host *host)
{
	struct hpsb_highlevel *hl;

	init_hpsb_highlevel(host);

	down_read(&hl_drivers_sem);
	list_for_each_entry(hl, &hl_drivers, hl_list) {
		if (hl->add_host)
			hl->add_host(host);
	}
	up_read(&hl_drivers_sem);
	if (host->update_config_rom && hpsb_update_config_rom_image(host) < 0)
		HPSB_ERR("Failed to generate Configuration ROM image for host "
			 "%s-%d", hl->name, host->id);
}

void highlevel_remove_host(struct hpsb_host *host)
{
	struct hpsb_highlevel *hl;

	down_read(&hl_drivers_sem);
	list_for_each_entry(hl, &hl_drivers, hl_list)
		__unregister_host(hl, host, 0);
	up_read(&hl_drivers_sem);
}

void highlevel_host_reset(struct hpsb_host *host)
{
	unsigned long flags;
	struct hpsb_highlevel *hl;

	read_lock_irqsave(&hl_irqs_lock, flags);
	list_for_each_entry(hl, &hl_irqs, irq_list) {
		if (hl->host_reset)
			hl->host_reset(host);
	}
	read_unlock_irqrestore(&hl_irqs_lock, flags);
}

void highlevel_fcp_request(struct hpsb_host *host, int nodeid, int direction,
			   void *data, size_t length)
{
	unsigned long flags;
	struct hpsb_highlevel *hl;
	int cts = ((quadlet_t *)data)[0] >> 4;

	read_lock_irqsave(&hl_irqs_lock, flags);
	list_for_each_entry(hl, &hl_irqs, irq_list) {
		if (hl->fcp_request)
			hl->fcp_request(host, nodeid, direction, cts, data,
					length);
	}
	read_unlock_irqrestore(&hl_irqs_lock, flags);
}

/*
 * highlevel_read, highlevel_write, highlevel_lock, highlevel_lock64:
 *
 * These functions are called to handle transactions. They are called when a
 * packet arrives.  The flags argument contains the second word of the first
 * header quadlet of the incoming packet (containing transaction label, retry
 * code, transaction code and priority).  These functions either return a
 * response code or a negative number.  In the first case a response will be
 * generated.  In the latter case, no response will be sent and the driver which
 * handled the request will send the response itself.
 */
int highlevel_read(struct hpsb_host *host, int nodeid, void *data, u64 addr,
		   unsigned int length, u16 flags)
{
	struct hpsb_address_serve *as;
	unsigned int partlength;
	int rcode = RCODE_ADDRESS_ERROR;

	read_lock(&addr_space_lock);
	list_for_each_entry(as, &host->addr_space, host_list) {
		if (as->start > addr)
			break;

		if (as->end > addr) {
			partlength = min(as->end - addr, (u64) length);

			if (as->op->read)
				rcode = as->op->read(host, nodeid, data,
						     addr, partlength, flags);
			else
				rcode = RCODE_TYPE_ERROR;

			data += partlength;
			length -= partlength;
			addr += partlength;

			if ((rcode != RCODE_COMPLETE) || !length)
				break;
		}
	}
	read_unlock(&addr_space_lock);

	if (length && (rcode == RCODE_COMPLETE))
		rcode = RCODE_ADDRESS_ERROR;
	return rcode;
}

int highlevel_write(struct hpsb_host *host, int nodeid, int destid, void *data,
		    u64 addr, unsigned int length, u16 flags)
{
	struct hpsb_address_serve *as;
	unsigned int partlength;
	int rcode = RCODE_ADDRESS_ERROR;

	read_lock(&addr_space_lock);
	list_for_each_entry(as, &host->addr_space, host_list) {
		if (as->start > addr)
			break;

		if (as->end > addr) {
			partlength = min(as->end - addr, (u64) length);

			if (as->op->write)
				rcode = as->op->write(host, nodeid, destid,
						      data, addr, partlength,
						      flags);
			else
				rcode = RCODE_TYPE_ERROR;

			data += partlength;
			length -= partlength;
			addr += partlength;

			if ((rcode != RCODE_COMPLETE) || !length)
				break;
		}
	}
	read_unlock(&addr_space_lock);

	if (length && (rcode == RCODE_COMPLETE))
		rcode = RCODE_ADDRESS_ERROR;
	return rcode;
}

int highlevel_lock(struct hpsb_host *host, int nodeid, quadlet_t *store,
		   u64 addr, quadlet_t data, quadlet_t arg, int ext_tcode,
		   u16 flags)
{
	struct hpsb_address_serve *as;
	int rcode = RCODE_ADDRESS_ERROR;

	read_lock(&addr_space_lock);
	list_for_each_entry(as, &host->addr_space, host_list) {
		if (as->start > addr)
			break;

		if (as->end > addr) {
			if (as->op->lock)
				rcode = as->op->lock(host, nodeid, store, addr,
						     data, arg, ext_tcode,
						     flags);
			else
				rcode = RCODE_TYPE_ERROR;
			break;
		}
	}
	read_unlock(&addr_space_lock);
	return rcode;
}

int highlevel_lock64(struct hpsb_host *host, int nodeid, octlet_t *store,
		     u64 addr, octlet_t data, octlet_t arg, int ext_tcode,
		     u16 flags)
{
	struct hpsb_address_serve *as;
	int rcode = RCODE_ADDRESS_ERROR;

	read_lock(&addr_space_lock);

	list_for_each_entry(as, &host->addr_space, host_list) {
		if (as->start > addr)
			break;

		if (as->end > addr) {
			if (as->op->lock64)
				rcode = as->op->lock64(host, nodeid, store,
						       addr, data, arg,
						       ext_tcode, flags);
			else
				rcode = RCODE_TYPE_ERROR;
			break;
		}
	}
	read_unlock(&addr_space_lock);
	return rcode;
}
