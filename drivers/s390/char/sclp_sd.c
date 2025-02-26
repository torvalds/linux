// SPDX-License-Identifier: GPL-2.0
/*
 * SCLP Store Data support and sysfs interface
 *
 * Copyright IBM Corp. 2017
 */

#define KMSG_COMPONENT "sclp_sd"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/completion.h>
#include <linux/jiffies.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/async.h>
#include <linux/export.h>
#include <linux/mutex.h>

#include <asm/pgalloc.h>

#include "sclp.h"

#define SD_EQ_STORE_DATA	0
#define SD_EQ_HALT		1
#define SD_EQ_SIZE		2

#define SD_DI_CONFIG		3

#define SD_TIMEOUT		msecs_to_jiffies(30000)

struct sclp_sd_evbuf {
	struct evbuf_header hdr;
	u8 eq;
	u8 di;
	u8 rflags;
	u64 :56;
	u32 id;
	u16 :16;
	u8 fmt;
	u8 status;
	u64 sat;
	u64 sa;
	u32 esize;
	u32 dsize;
} __packed;

struct sclp_sd_sccb {
	struct sccb_header hdr;
	struct sclp_sd_evbuf evbuf;
} __packed __aligned(PAGE_SIZE);

/**
 * struct sclp_sd_data - Result of a Store Data request
 * @esize_bytes: Resulting esize in bytes
 * @dsize_bytes: Resulting dsize in bytes
 * @data: Pointer to data - must be released using vfree()
 */
struct sclp_sd_data {
	size_t esize_bytes;
	size_t dsize_bytes;
	void *data;
};

/**
 * struct sclp_sd_listener - Listener for asynchronous Store Data response
 * @list: For enqueueing this struct
 * @id: Event ID of response to listen for
 * @completion: Can be used to wait for response
 * @evbuf: Contains the resulting Store Data response after completion
 */
struct sclp_sd_listener {
	struct list_head list;
	u32 id;
	struct completion completion;
	struct sclp_sd_evbuf evbuf;
};

/**
 * struct sclp_sd_file - Sysfs representation of a Store Data entity
 * @kobj: Kobject
 * @data_attr: Attribute for accessing data contents
 * @data_mutex: Mutex to serialize access and updates to @data
 * @data: Data associated with this entity
 * @di: DI value associated with this entity
 */
struct sclp_sd_file {
	struct kobject kobj;
	struct bin_attribute data_attr;
	struct mutex data_mutex;
	struct sclp_sd_data data;
	u8 di;
};
#define to_sd_file(x) container_of(x, struct sclp_sd_file, kobj)

static struct kset *sclp_sd_kset;
static struct sclp_sd_file *config_file;

static LIST_HEAD(sclp_sd_queue);
static DEFINE_SPINLOCK(sclp_sd_queue_lock);

/**
 * sclp_sd_listener_add() - Add listener for Store Data responses
 * @listener: Listener to add
 */
static void sclp_sd_listener_add(struct sclp_sd_listener *listener)
{
	spin_lock_irq(&sclp_sd_queue_lock);
	list_add_tail(&listener->list, &sclp_sd_queue);
	spin_unlock_irq(&sclp_sd_queue_lock);
}

/**
 * sclp_sd_listener_remove() - Remove listener for Store Data responses
 * @listener: Listener to remove
 */
static void sclp_sd_listener_remove(struct sclp_sd_listener *listener)
{
	spin_lock_irq(&sclp_sd_queue_lock);
	list_del(&listener->list);
	spin_unlock_irq(&sclp_sd_queue_lock);
}

/**
 * sclp_sd_listener_init() - Initialize a Store Data response listener
 * @listener: Response listener to initialize
 * @id: Event ID to listen for
 *
 * Initialize a listener for asynchronous Store Data responses. This listener
 * can afterwards be used to wait for a specific response and to retrieve
 * the associated response data.
 */
static void sclp_sd_listener_init(struct sclp_sd_listener *listener, u32 id)
{
	memset(listener, 0, sizeof(*listener));
	listener->id = id;
	init_completion(&listener->completion);
}

/**
 * sclp_sd_receiver() - Receiver for Store Data events
 * @evbuf_hdr: Header of received events
 *
 * Process Store Data events and complete listeners with matching event IDs.
 */
static void sclp_sd_receiver(struct evbuf_header *evbuf_hdr)
{
	struct sclp_sd_evbuf *evbuf = (struct sclp_sd_evbuf *) evbuf_hdr;
	struct sclp_sd_listener *listener;
	int found = 0;

	pr_debug("received event (id=0x%08x)\n", evbuf->id);
	spin_lock(&sclp_sd_queue_lock);
	list_for_each_entry(listener, &sclp_sd_queue, list) {
		if (listener->id != evbuf->id)
			continue;

		listener->evbuf = *evbuf;
		complete(&listener->completion);
		found = 1;
		break;
	}
	spin_unlock(&sclp_sd_queue_lock);

	if (!found)
		pr_debug("unsolicited event (id=0x%08x)\n", evbuf->id);
}

static struct sclp_register sclp_sd_register = {
	.send_mask = EVTYP_STORE_DATA_MASK,
	.receive_mask = EVTYP_STORE_DATA_MASK,
	.receiver_fn = sclp_sd_receiver,
};

/**
 * sclp_sd_sync() - Perform Store Data request synchronously
 * @page: Address of work page - must be below 2GB
 * @eq: Input EQ value
 * @di: Input DI value
 * @sat: Input SAT value
 * @sa: Input SA value used to specify the address of the target buffer
 * @dsize_ptr: Optional pointer to input and output DSIZE value
 * @esize_ptr: Optional pointer to output ESIZE value
 *
 * Perform Store Data request with specified parameters and wait for completion.
 *
 * Return %0 on success and store resulting DSIZE and ESIZE values in
 * @dsize_ptr and @esize_ptr (if provided). Return non-zero on error.
 */
static int sclp_sd_sync(unsigned long page, u8 eq, u8 di, u64 sat, u64 sa,
			u32 *dsize_ptr, u32 *esize_ptr)
{
	struct sclp_sd_sccb *sccb = (void *) page;
	struct sclp_sd_listener listener;
	struct sclp_sd_evbuf *evbuf;
	int rc;

	if (!sclp_sd_register.sclp_send_mask ||
	    !sclp_sd_register.sclp_receive_mask)
		return -EIO;

	sclp_sd_listener_init(&listener, __pa(sccb));
	sclp_sd_listener_add(&listener);

	/* Prepare SCCB */
	memset(sccb, 0, PAGE_SIZE);
	sccb->hdr.length = sizeof(sccb->hdr) + sizeof(sccb->evbuf);
	evbuf = &sccb->evbuf;
	evbuf->hdr.length = sizeof(*evbuf);
	evbuf->hdr.type = EVTYP_STORE_DATA;
	evbuf->eq = eq;
	evbuf->di = di;
	evbuf->id = listener.id;
	evbuf->fmt = 1;
	evbuf->sat = sat;
	evbuf->sa = sa;
	if (dsize_ptr)
		evbuf->dsize = *dsize_ptr;

	/* Perform command */
	pr_debug("request (eq=%d, di=%d, id=0x%08x)\n", eq, di, listener.id);
	rc = sclp_sync_request(SCLP_CMDW_WRITE_EVENT_DATA, sccb);
	pr_debug("request done (rc=%d)\n", rc);
	if (rc)
		goto out;

	/* Evaluate response */
	if (sccb->hdr.response_code == 0x73f0) {
		pr_debug("event not supported\n");
		rc = -EIO;
		goto out_remove;
	}
	if (sccb->hdr.response_code != 0x0020 || !(evbuf->hdr.flags & 0x80)) {
		rc = -EIO;
		goto out;
	}
	if (!(evbuf->rflags & 0x80)) {
		rc = wait_for_completion_interruptible_timeout(&listener.completion, SD_TIMEOUT);
		if (rc == 0)
			rc = -ETIME;
		if (rc < 0)
			goto out;
		rc = 0;
		evbuf = &listener.evbuf;
	}
	switch (evbuf->status) {
	case 0:
		if (dsize_ptr)
			*dsize_ptr = evbuf->dsize;
		if (esize_ptr)
			*esize_ptr = evbuf->esize;
		pr_debug("success (dsize=%u, esize=%u)\n", evbuf->dsize,
			 evbuf->esize);
		break;
	case 3:
		rc = -ENOENT;
		break;
	default:
		rc = -EIO;
		break;

	}

out:
	if (rc && rc != -ENOENT) {
		/* Provide some information about what went wrong */
		pr_warn("Store Data request failed (eq=%d, di=%d, "
			"response=0x%04x, flags=0x%02x, status=%d, rc=%d)\n",
			eq, di, sccb->hdr.response_code, evbuf->hdr.flags,
			evbuf->status, rc);
	}

out_remove:
	sclp_sd_listener_remove(&listener);

	return rc;
}

/**
 * sclp_sd_store_data() - Obtain data for specified Store Data entity
 * @result: Resulting data
 * @di: DI value associated with this entity
 *
 * Perform a series of Store Data requests to obtain the size and contents of
 * the specified Store Data entity.
 *
 * Return:
 *   %0:       Success - result is stored in @result. @result->data must be
 *	       released using vfree() after use.
 *   %-ENOENT: No data available for this entity
 *   %<0:      Other error
 */
static int sclp_sd_store_data(struct sclp_sd_data *result, u8 di)
{
	u32 dsize = 0, esize = 0;
	unsigned long page, asce = 0;
	void *data = NULL;
	int rc;

	page = __get_free_page(GFP_KERNEL | GFP_DMA);
	if (!page)
		return -ENOMEM;

	/* Get size */
	rc = sclp_sd_sync(page, SD_EQ_SIZE, di, 0, 0, &dsize, &esize);
	if (rc)
		goto out;
	if (dsize == 0)
		goto out_result;

	/* Allocate memory */
	data = vzalloc(array_size((size_t)dsize, PAGE_SIZE));
	if (!data) {
		rc = -ENOMEM;
		goto out;
	}

	/* Get translation table for buffer */
	asce = base_asce_alloc((unsigned long) data, dsize);
	if (!asce) {
		vfree(data);
		rc = -ENOMEM;
		goto out;
	}

	/* Get data */
	rc = sclp_sd_sync(page, SD_EQ_STORE_DATA, di, asce, (u64) data, &dsize,
			  &esize);
	if (rc) {
		/* Cancel running request if interrupted or timed out */
		if (rc == -ERESTARTSYS || rc == -ETIME) {
			if (sclp_sd_sync(page, SD_EQ_HALT, di, 0, 0, NULL, NULL)) {
				pr_warn("Could not stop Store Data request - leaking at least %zu bytes\n",
					(size_t)dsize * PAGE_SIZE);
				data = NULL;
				asce = 0;
			}
		}
		vfree(data);
		goto out;
	}

out_result:
	result->esize_bytes = (size_t) esize * PAGE_SIZE;
	result->dsize_bytes = (size_t) dsize * PAGE_SIZE;
	result->data = data;

out:
	base_asce_free(asce);
	free_page(page);

	return rc;
}

/**
 * sclp_sd_data_reset() - Reset Store Data result buffer
 * @data: Data buffer to reset
 *
 * Reset @data to initial state and release associated memory.
 */
static void sclp_sd_data_reset(struct sclp_sd_data *data)
{
	vfree(data->data);
	data->data = NULL;
	data->dsize_bytes = 0;
	data->esize_bytes = 0;
}

/**
 * sclp_sd_file_release() - Release function for sclp_sd_file object
 * @kobj: Kobject embedded in sclp_sd_file object
 */
static void sclp_sd_file_release(struct kobject *kobj)
{
	struct sclp_sd_file *sd_file = to_sd_file(kobj);

	sclp_sd_data_reset(&sd_file->data);
	kfree(sd_file);
}

/**
 * sclp_sd_file_update() - Update contents of sclp_sd_file object
 * @sd_file: Object to update
 *
 * Obtain the current version of data associated with the Store Data entity
 * @sd_file.
 *
 * On success, return %0 and generate a KOBJ_CHANGE event to indicate that the
 * data may have changed. Return non-zero otherwise.
 */
static int sclp_sd_file_update(struct sclp_sd_file *sd_file)
{
	const char *name = kobject_name(&sd_file->kobj);
	struct sclp_sd_data data;
	int rc;

	rc = sclp_sd_store_data(&data, sd_file->di);
	if (rc) {
		if (rc == -ENOENT) {
			pr_info("No data is available for the %s data entity\n",
				 name);
		}
		return rc;
	}

	mutex_lock(&sd_file->data_mutex);
	sclp_sd_data_reset(&sd_file->data);
	sd_file->data = data;
	mutex_unlock(&sd_file->data_mutex);

	pr_info("A %zu-byte %s data entity was retrieved\n", data.dsize_bytes,
		name);
	kobject_uevent(&sd_file->kobj, KOBJ_CHANGE);

	return 0;
}

/**
 * sclp_sd_file_update_async() - Wrapper for asynchronous update call
 * @data: Object to update
 * @cookie: Unused
 */
static void sclp_sd_file_update_async(void *data, async_cookie_t cookie)
{
	struct sclp_sd_file *sd_file = data;

	sclp_sd_file_update(sd_file);
}

/**
 * reload_store() - Store function for "reload" sysfs attribute
 * @kobj: Kobject of sclp_sd_file object
 * @attr: Reload attribute
 * @buf: Data written to sysfs attribute
 * @count: Count of bytes written
 *
 * Initiate a reload of the data associated with an sclp_sd_file object.
 */
static ssize_t reload_store(struct kobject *kobj, struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	struct sclp_sd_file *sd_file = to_sd_file(kobj);

	sclp_sd_file_update(sd_file);

	return count;
}

static struct kobj_attribute reload_attr = __ATTR_WO(reload);

static struct attribute *sclp_sd_file_default_attrs[] = {
	&reload_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(sclp_sd_file_default);

static struct kobj_type sclp_sd_file_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.release = sclp_sd_file_release,
	.default_groups = sclp_sd_file_default_groups,
};

/**
 * data_read() - Read function for "data" sysfs attribute
 * @file: Open file pointer
 * @kobj: Kobject of sclp_sd_file object
 * @attr: Data attribute
 * @buffer: Target buffer
 * @off: Requested file offset
 * @size: Requested number of bytes
 *
 * Store the requested portion of the Store Data entity contents into the
 * specified buffer. Return the number of bytes stored on success, or %0
 * on EOF.
 */
static ssize_t data_read(struct file *file, struct kobject *kobj,
			 const struct bin_attribute *attr, char *buffer,
			 loff_t off, size_t size)
{
	struct sclp_sd_file *sd_file = to_sd_file(kobj);
	size_t data_size;
	char *data;

	mutex_lock(&sd_file->data_mutex);

	data = sd_file->data.data;
	data_size = sd_file->data.dsize_bytes;
	if (!data || off >= data_size) {
		size = 0;
	} else {
		if (off + size > data_size)
			size = data_size - off;
		memcpy(buffer, data + off, size);
	}

	mutex_unlock(&sd_file->data_mutex);

	return size;
}

/**
 * sclp_sd_file_create() - Add a sysfs file representing a Store Data entity
 * @name: Name of file
 * @di: DI value associated with this entity
 *
 * Create a sysfs directory with the given @name located under
 *
 *   /sys/firmware/sclp_sd/
 *
 * The files in this directory can be used to access the contents of the Store
 * Data entity associated with @DI.
 *
 * Return pointer to resulting sclp_sd_file object on success, %NULL otherwise.
 * The object must be freed by calling kobject_put() on the embedded kobject
 * pointer after use.
 */
static __init struct sclp_sd_file *sclp_sd_file_create(const char *name, u8 di)
{
	struct sclp_sd_file *sd_file;
	int rc;

	sd_file = kzalloc(sizeof(*sd_file), GFP_KERNEL);
	if (!sd_file)
		return NULL;
	sd_file->di = di;
	mutex_init(&sd_file->data_mutex);

	/* Create kobject located under /sys/firmware/sclp_sd/ */
	sd_file->kobj.kset = sclp_sd_kset;
	rc = kobject_init_and_add(&sd_file->kobj, &sclp_sd_file_ktype, NULL,
				  "%s", name);
	if (rc) {
		kobject_put(&sd_file->kobj);
		return NULL;
	}

	sysfs_bin_attr_init(&sd_file->data_attr);
	sd_file->data_attr.attr.name = "data";
	sd_file->data_attr.attr.mode = 0444;
	sd_file->data_attr.read_new = data_read;

	rc = sysfs_create_bin_file(&sd_file->kobj, &sd_file->data_attr);
	if (rc) {
		kobject_put(&sd_file->kobj);
		return NULL;
	}

	/*
	 * For completeness only - users interested in entity data should listen
	 * for KOBJ_CHANGE instead.
	 */
	kobject_uevent(&sd_file->kobj, KOBJ_ADD);

	/* Don't let a slow Store Data request delay further initialization */
	async_schedule(sclp_sd_file_update_async, sd_file);

	return sd_file;
}

/**
 * sclp_sd_init() - Initialize sclp_sd support and register sysfs files
 */
static __init int sclp_sd_init(void)
{
	int rc;

	rc = sclp_register(&sclp_sd_register);
	if (rc)
		return rc;

	/* Create kset named "sclp_sd" located under /sys/firmware/ */
	rc = -ENOMEM;
	sclp_sd_kset = kset_create_and_add("sclp_sd", NULL, firmware_kobj);
	if (!sclp_sd_kset)
		goto err_kset;

	rc = -EINVAL;
	config_file = sclp_sd_file_create("config", SD_DI_CONFIG);
	if (!config_file)
		goto err_config;

	return 0;

err_config:
	kset_unregister(sclp_sd_kset);
err_kset:
	sclp_unregister(&sclp_sd_register);

	return rc;
}
device_initcall(sclp_sd_init);
