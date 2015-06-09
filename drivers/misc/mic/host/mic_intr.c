/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Intel MIC Host driver.
 *
 */
#include <linux/pci.h>
#include <linux/interrupt.h>

#include "../common/mic_dev.h"
#include "mic_device.h"

static irqreturn_t mic_thread_fn(int irq, void *dev)
{
	struct mic_device *mdev = dev;
	struct mic_intr_info *intr_info = mdev->intr_info;
	struct mic_irq_info *irq_info = &mdev->irq_info;
	struct mic_intr_cb *intr_cb;
	struct pci_dev *pdev = container_of(mdev->sdev->parent,
					    struct pci_dev, dev);
	int i;

	spin_lock(&irq_info->mic_thread_lock);
	for (i = intr_info->intr_start_idx[MIC_INTR_DB];
			i < intr_info->intr_len[MIC_INTR_DB]; i++)
		if (test_and_clear_bit(i, &irq_info->mask)) {
			list_for_each_entry(intr_cb, &irq_info->cb_list[i],
					    list)
				if (intr_cb->thread_fn)
					intr_cb->thread_fn(pdev->irq,
							 intr_cb->data);
		}
	spin_unlock(&irq_info->mic_thread_lock);
	return IRQ_HANDLED;
}
/**
 * mic_interrupt - Generic interrupt handler for
 * MSI and INTx based interrupts.
 */
static irqreturn_t mic_interrupt(int irq, void *dev)
{
	struct mic_device *mdev = dev;
	struct mic_intr_info *intr_info = mdev->intr_info;
	struct mic_irq_info *irq_info = &mdev->irq_info;
	struct mic_intr_cb *intr_cb;
	struct pci_dev *pdev = container_of(mdev->sdev->parent,
					    struct pci_dev, dev);
	u32 mask;
	int i;

	mask = mdev->ops->ack_interrupt(mdev);
	if (!mask)
		return IRQ_NONE;

	spin_lock(&irq_info->mic_intr_lock);
	for (i = intr_info->intr_start_idx[MIC_INTR_DB];
			i < intr_info->intr_len[MIC_INTR_DB]; i++)
		if (mask & BIT(i)) {
			list_for_each_entry(intr_cb, &irq_info->cb_list[i],
					    list)
				if (intr_cb->handler)
					intr_cb->handler(pdev->irq,
							 intr_cb->data);
			set_bit(i, &irq_info->mask);
		}
	spin_unlock(&irq_info->mic_intr_lock);
	return IRQ_WAKE_THREAD;
}

/* Return the interrupt offset from the index. Index is 0 based. */
static u16 mic_map_src_to_offset(struct mic_device *mdev,
		int intr_src, enum mic_intr_type type)
{
	if (type >= MIC_NUM_INTR_TYPES)
		return MIC_NUM_OFFSETS;
	if (intr_src >= mdev->intr_info->intr_len[type])
		return MIC_NUM_OFFSETS;

	return mdev->intr_info->intr_start_idx[type] + intr_src;
}

/* Return next available msix_entry. */
static struct msix_entry *mic_get_available_vector(struct mic_device *mdev)
{
	int i;
	struct mic_irq_info *info = &mdev->irq_info;

	for (i = 0; i < info->num_vectors; i++)
		if (!info->mic_msi_map[i])
			return &info->msix_entries[i];
	return NULL;
}

/**
 * mic_register_intr_callback - Register a callback handler for the
 * given source id.
 *
 * @mdev: pointer to the mic_device instance
 * @idx: The source id to be registered.
 * @handler: The function to be called when the source id receives
 * the interrupt.
 * @thread_fn: thread fn. corresponding to the handler
 * @data: Private data of the requester.
 * Return the callback structure that was registered or an
 * appropriate error on failure.
 */
static struct mic_intr_cb *mic_register_intr_callback(struct mic_device *mdev,
			u8 idx, irq_handler_t handler, irq_handler_t thread_fn,
			void *data)
{
	struct mic_intr_cb *intr_cb;
	unsigned long flags;
	int rc;
	intr_cb = kmalloc(sizeof(*intr_cb), GFP_KERNEL);

	if (!intr_cb)
		return ERR_PTR(-ENOMEM);

	intr_cb->handler = handler;
	intr_cb->thread_fn = thread_fn;
	intr_cb->data = data;
	intr_cb->cb_id = ida_simple_get(&mdev->irq_info.cb_ida,
		0, 0, GFP_KERNEL);
	if (intr_cb->cb_id < 0) {
		rc = intr_cb->cb_id;
		goto ida_fail;
	}

	spin_lock(&mdev->irq_info.mic_thread_lock);
	spin_lock_irqsave(&mdev->irq_info.mic_intr_lock, flags);
	list_add_tail(&intr_cb->list, &mdev->irq_info.cb_list[idx]);
	spin_unlock_irqrestore(&mdev->irq_info.mic_intr_lock, flags);
	spin_unlock(&mdev->irq_info.mic_thread_lock);

	return intr_cb;
ida_fail:
	kfree(intr_cb);
	return ERR_PTR(rc);
}

/**
 * mic_unregister_intr_callback - Unregister the callback handler
 * identified by its callback id.
 *
 * @mdev: pointer to the mic_device instance
 * @idx: The callback structure id to be unregistered.
 * Return the source id that was unregistered or MIC_NUM_OFFSETS if no
 * such callback handler was found.
 */
static u8 mic_unregister_intr_callback(struct mic_device *mdev, u32 idx)
{
	struct list_head *pos, *tmp;
	struct mic_intr_cb *intr_cb;
	unsigned long flags;
	int i;

	spin_lock(&mdev->irq_info.mic_thread_lock);
	spin_lock_irqsave(&mdev->irq_info.mic_intr_lock, flags);
	for (i = 0;  i < MIC_NUM_OFFSETS; i++) {
		list_for_each_safe(pos, tmp, &mdev->irq_info.cb_list[i]) {
			intr_cb = list_entry(pos, struct mic_intr_cb, list);
			if (intr_cb->cb_id == idx) {
				list_del(pos);
				ida_simple_remove(&mdev->irq_info.cb_ida,
						  intr_cb->cb_id);
				kfree(intr_cb);
				spin_unlock_irqrestore(
					&mdev->irq_info.mic_intr_lock, flags);
				spin_unlock(&mdev->irq_info.mic_thread_lock);
				return i;
			}
		}
	}
	spin_unlock_irqrestore(&mdev->irq_info.mic_intr_lock, flags);
	spin_unlock(&mdev->irq_info.mic_thread_lock);
	return MIC_NUM_OFFSETS;
}

/**
 * mic_setup_msix - Initializes MSIx interrupts.
 *
 * @mdev: pointer to mic_device instance
 *
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
static int mic_setup_msix(struct mic_device *mdev, struct pci_dev *pdev)
{
	int rc, i;
	int entry_size = sizeof(*mdev->irq_info.msix_entries);

	mdev->irq_info.msix_entries = kmalloc_array(MIC_MIN_MSIX,
						    entry_size, GFP_KERNEL);
	if (!mdev->irq_info.msix_entries) {
		rc = -ENOMEM;
		goto err_nomem1;
	}

	for (i = 0; i < MIC_MIN_MSIX; i++)
		mdev->irq_info.msix_entries[i].entry = i;

	rc = pci_enable_msix_exact(pdev, mdev->irq_info.msix_entries,
		MIC_MIN_MSIX);
	if (rc) {
		dev_dbg(&pdev->dev, "Error enabling MSIx. rc = %d\n", rc);
		goto err_enable_msix;
	}

	mdev->irq_info.num_vectors = MIC_MIN_MSIX;
	mdev->irq_info.mic_msi_map = kzalloc((sizeof(u32) *
		mdev->irq_info.num_vectors), GFP_KERNEL);

	if (!mdev->irq_info.mic_msi_map) {
		rc = -ENOMEM;
		goto err_nomem2;
	}

	dev_dbg(mdev->sdev->parent,
		"%d MSIx irqs setup\n", mdev->irq_info.num_vectors);
	return 0;
err_nomem2:
	pci_disable_msix(pdev);
err_enable_msix:
	kfree(mdev->irq_info.msix_entries);
err_nomem1:
	mdev->irq_info.num_vectors = 0;
	return rc;
}

/**
 * mic_setup_callbacks - Initialize data structures needed
 * to handle callbacks.
 *
 * @mdev: pointer to mic_device instance
 */
static int mic_setup_callbacks(struct mic_device *mdev)
{
	int i;

	mdev->irq_info.cb_list = kmalloc_array(MIC_NUM_OFFSETS,
					       sizeof(*mdev->irq_info.cb_list),
					       GFP_KERNEL);
	if (!mdev->irq_info.cb_list)
		return -ENOMEM;

	for (i = 0; i < MIC_NUM_OFFSETS; i++)
		INIT_LIST_HEAD(&mdev->irq_info.cb_list[i]);
	ida_init(&mdev->irq_info.cb_ida);
	spin_lock_init(&mdev->irq_info.mic_intr_lock);
	spin_lock_init(&mdev->irq_info.mic_thread_lock);
	return 0;
}

/**
 * mic_release_callbacks - Uninitialize data structures needed
 * to handle callbacks.
 *
 * @mdev: pointer to mic_device instance
 */
static void mic_release_callbacks(struct mic_device *mdev)
{
	unsigned long flags;
	struct list_head *pos, *tmp;
	struct mic_intr_cb *intr_cb;
	int i;

	spin_lock(&mdev->irq_info.mic_thread_lock);
	spin_lock_irqsave(&mdev->irq_info.mic_intr_lock, flags);
	for (i = 0; i < MIC_NUM_OFFSETS; i++) {

		if (list_empty(&mdev->irq_info.cb_list[i]))
			break;

		list_for_each_safe(pos, tmp, &mdev->irq_info.cb_list[i]) {
			intr_cb = list_entry(pos, struct mic_intr_cb, list);
			list_del(pos);
			ida_simple_remove(&mdev->irq_info.cb_ida,
					  intr_cb->cb_id);
			kfree(intr_cb);
		}
	}
	spin_unlock_irqrestore(&mdev->irq_info.mic_intr_lock, flags);
	spin_unlock(&mdev->irq_info.mic_thread_lock);
	ida_destroy(&mdev->irq_info.cb_ida);
	kfree(mdev->irq_info.cb_list);
}

/**
 * mic_setup_msi - Initializes MSI interrupts.
 *
 * @mdev: pointer to mic_device instance
 * @pdev: PCI device structure
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
static int mic_setup_msi(struct mic_device *mdev, struct pci_dev *pdev)
{
	int rc;

	rc = pci_enable_msi(pdev);
	if (rc) {
		dev_dbg(&pdev->dev, "Error enabling MSI. rc = %d\n", rc);
		return rc;
	}

	mdev->irq_info.num_vectors = 1;
	mdev->irq_info.mic_msi_map = kzalloc((sizeof(u32) *
		mdev->irq_info.num_vectors), GFP_KERNEL);

	if (!mdev->irq_info.mic_msi_map) {
		rc = -ENOMEM;
		goto err_nomem1;
	}

	rc = mic_setup_callbacks(mdev);
	if (rc) {
		dev_err(&pdev->dev, "Error setting up callbacks\n");
		goto err_nomem2;
	}

	rc = request_threaded_irq(pdev->irq, mic_interrupt, mic_thread_fn,
				  0, "mic-msi", mdev);
	if (rc) {
		dev_err(&pdev->dev, "Error allocating MSI interrupt\n");
		goto err_irq_req_fail;
	}

	dev_dbg(&pdev->dev, "%d MSI irqs setup\n", mdev->irq_info.num_vectors);
	return 0;
err_irq_req_fail:
	mic_release_callbacks(mdev);
err_nomem2:
	kfree(mdev->irq_info.mic_msi_map);
err_nomem1:
	pci_disable_msi(pdev);
	mdev->irq_info.num_vectors = 0;
	return rc;
}

/**
 * mic_setup_intx - Initializes legacy interrupts.
 *
 * @mdev: pointer to mic_device instance
 * @pdev: PCI device structure
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
static int mic_setup_intx(struct mic_device *mdev, struct pci_dev *pdev)
{
	int rc;

	/* Enable intx */
	pci_intx(pdev, 1);
	rc = mic_setup_callbacks(mdev);
	if (rc) {
		dev_err(&pdev->dev, "Error setting up callbacks\n");
		goto err_nomem;
	}

	rc = request_threaded_irq(pdev->irq, mic_interrupt, mic_thread_fn,
				  IRQF_SHARED, "mic-intx", mdev);
	if (rc)
		goto err;

	dev_dbg(&pdev->dev, "intx irq setup\n");
	return 0;
err:
	mic_release_callbacks(mdev);
err_nomem:
	return rc;
}

/**
 * mic_next_db - Retrieve the next doorbell interrupt source id.
 * The id is picked sequentially from the available pool of
 * doorlbell ids.
 *
 * @mdev: pointer to the mic_device instance.
 *
 * Returns the next doorbell interrupt source.
 */
int mic_next_db(struct mic_device *mdev)
{
	int next_db;

	next_db = mdev->irq_info.next_avail_src %
		mdev->intr_info->intr_len[MIC_INTR_DB];
	mdev->irq_info.next_avail_src++;
	return next_db;
}

#define COOKIE_ID_SHIFT 16
#define GET_ENTRY(cookie) ((cookie) & 0xFFFF)
#define GET_OFFSET(cookie) ((cookie) >> COOKIE_ID_SHIFT)
#define MK_COOKIE(x, y) ((x) | (y) << COOKIE_ID_SHIFT)

/**
 * mic_request_threaded_irq - request an irq. mic_mutex needs
 * to be held before calling this function.
 *
 * @mdev: pointer to mic_device instance
 * @handler: The callback function that handles the interrupt.
 * The function needs to call ack_interrupts
 * (mdev->ops->ack_interrupt(mdev)) when handling the interrupts.
 * @thread_fn: thread fn required by request_threaded_irq.
 * @name: The ASCII name of the callee requesting the irq.
 * @data: private data that is returned back when calling the
 * function handler.
 * @intr_src: The source id of the requester. Its the doorbell id
 * for Doorbell interrupts and DMA channel id for DMA interrupts.
 * @type: The type of interrupt. Values defined in mic_intr_type
 *
 * returns: The cookie that is transparent to the caller. Passed
 * back when calling mic_free_irq. An appropriate error code
 * is returned on failure. Caller needs to use IS_ERR(return_val)
 * to check for failure and PTR_ERR(return_val) to obtained the
 * error code.
 *
 */
struct mic_irq *
mic_request_threaded_irq(struct mic_device *mdev,
			 irq_handler_t handler, irq_handler_t thread_fn,
			 const char *name, void *data, int intr_src,
			 enum mic_intr_type type)
{
	u16 offset;
	int rc = 0;
	struct msix_entry *msix = NULL;
	unsigned long cookie = 0;
	u16 entry;
	struct mic_intr_cb *intr_cb;
	struct pci_dev *pdev = container_of(mdev->sdev->parent,
		struct pci_dev, dev);

	offset = mic_map_src_to_offset(mdev, intr_src, type);
	if (offset >= MIC_NUM_OFFSETS) {
		dev_err(mdev->sdev->parent,
			"Error mapping index %d to a valid source id.\n",
			intr_src);
		rc = -EINVAL;
		goto err;
	}

	if (mdev->irq_info.num_vectors > 1) {
		msix = mic_get_available_vector(mdev);
		if (!msix) {
			dev_err(mdev->sdev->parent,
				"No MSIx vectors available for use.\n");
			rc = -ENOSPC;
			goto err;
		}

		rc = request_threaded_irq(msix->vector, handler, thread_fn,
					  0, name, data);
		if (rc) {
			dev_dbg(mdev->sdev->parent,
				"request irq failed rc = %d\n", rc);
			goto err;
		}
		entry = msix->entry;
		mdev->irq_info.mic_msi_map[entry] |= BIT(offset);
		mdev->intr_ops->program_msi_to_src_map(mdev,
				entry, offset, true);
		cookie = MK_COOKIE(entry, offset);
		dev_dbg(mdev->sdev->parent, "irq: %d assigned for src: %d\n",
			msix->vector, intr_src);
	} else {
		intr_cb = mic_register_intr_callback(mdev, offset, handler,
						     thread_fn, data);
		if (IS_ERR(intr_cb)) {
			dev_err(mdev->sdev->parent,
				"No available callback entries for use\n");
			rc = PTR_ERR(intr_cb);
			goto err;
		}

		entry = 0;
		if (pci_dev_msi_enabled(pdev)) {
			mdev->irq_info.mic_msi_map[entry] |= (1 << offset);
			mdev->intr_ops->program_msi_to_src_map(mdev,
				entry, offset, true);
		}
		cookie = MK_COOKIE(entry, intr_cb->cb_id);
		dev_dbg(mdev->sdev->parent, "callback %d registered for src: %d\n",
			intr_cb->cb_id, intr_src);
	}
	return (struct mic_irq *)cookie;
err:
	return ERR_PTR(rc);
}

/**
 * mic_free_irq - free irq. mic_mutex
 *  needs to be held before calling this function.
 *
 * @mdev: pointer to mic_device instance
 * @cookie: cookie obtained during a successful call to mic_request_threaded_irq
 * @data: private data specified by the calling function during the
 * mic_request_threaded_irq
 *
 * returns: none.
 */
void mic_free_irq(struct mic_device *mdev,
	struct mic_irq *cookie, void *data)
{
	u32 offset;
	u32 entry;
	u8 src_id;
	unsigned int irq;
	struct pci_dev *pdev = container_of(mdev->sdev->parent,
		struct pci_dev, dev);

	entry = GET_ENTRY((unsigned long)cookie);
	offset = GET_OFFSET((unsigned long)cookie);
	if (mdev->irq_info.num_vectors > 1) {
		if (entry >= mdev->irq_info.num_vectors) {
			dev_warn(mdev->sdev->parent,
				 "entry %d should be < num_irq %d\n",
				entry, mdev->irq_info.num_vectors);
			return;
		}
		irq = mdev->irq_info.msix_entries[entry].vector;
		free_irq(irq, data);
		mdev->irq_info.mic_msi_map[entry] &= ~(BIT(offset));
		mdev->intr_ops->program_msi_to_src_map(mdev,
			entry, offset, false);

		dev_dbg(mdev->sdev->parent, "irq: %d freed\n", irq);
	} else {
		irq = pdev->irq;
		src_id = mic_unregister_intr_callback(mdev, offset);
		if (src_id >= MIC_NUM_OFFSETS) {
			dev_warn(mdev->sdev->parent, "Error unregistering callback\n");
			return;
		}
		if (pci_dev_msi_enabled(pdev)) {
			mdev->irq_info.mic_msi_map[entry] &= ~(BIT(src_id));
			mdev->intr_ops->program_msi_to_src_map(mdev,
				entry, src_id, false);
		}
		dev_dbg(mdev->sdev->parent, "callback %d unregistered for src: %d\n",
			offset, src_id);
	}
}

/**
 * mic_setup_interrupts - Initializes interrupts.
 *
 * @mdev: pointer to mic_device instance
 * @pdev: PCI device structure
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
int mic_setup_interrupts(struct mic_device *mdev, struct pci_dev *pdev)
{
	int rc;

	rc = mic_setup_msix(mdev, pdev);
	if (!rc)
		goto done;

	rc = mic_setup_msi(mdev, pdev);
	if (!rc)
		goto done;

	rc = mic_setup_intx(mdev, pdev);
	if (rc) {
		dev_err(mdev->sdev->parent, "no usable interrupts\n");
		return rc;
	}
done:
	mdev->intr_ops->enable_interrupts(mdev);
	return 0;
}

/**
 * mic_free_interrupts - Frees interrupts setup by mic_setup_interrupts
 *
 * @mdev: pointer to mic_device instance
 * @pdev: PCI device structure
 *
 * returns none.
 */
void mic_free_interrupts(struct mic_device *mdev, struct pci_dev *pdev)
{
	int i;

	mdev->intr_ops->disable_interrupts(mdev);
	if (mdev->irq_info.num_vectors > 1) {
		for (i = 0; i < mdev->irq_info.num_vectors; i++) {
			if (mdev->irq_info.mic_msi_map[i])
				dev_warn(&pdev->dev, "irq %d may still be in use.\n",
					 mdev->irq_info.msix_entries[i].vector);
		}
		kfree(mdev->irq_info.mic_msi_map);
		kfree(mdev->irq_info.msix_entries);
		pci_disable_msix(pdev);
	} else {
		if (pci_dev_msi_enabled(pdev)) {
			free_irq(pdev->irq, mdev);
			kfree(mdev->irq_info.mic_msi_map);
			pci_disable_msi(pdev);
		} else {
			free_irq(pdev->irq, mdev);
		}
		mic_release_callbacks(mdev);
	}
}

/**
 * mic_intr_restore - Restore MIC interrupt registers.
 *
 * @mdev: pointer to mic_device instance.
 *
 * Restore the interrupt registers to values previously
 * stored in the SW data structures. mic_mutex needs to
 * be held before calling this function.
 *
 * returns None.
 */
void mic_intr_restore(struct mic_device *mdev)
{
	int entry, offset;
	struct pci_dev *pdev = container_of(mdev->sdev->parent,
		struct pci_dev, dev);

	if (!pci_dev_msi_enabled(pdev))
		return;

	for (entry = 0; entry < mdev->irq_info.num_vectors; entry++) {
		for (offset = 0; offset < MIC_NUM_OFFSETS; offset++) {
			if (mdev->irq_info.mic_msi_map[entry] & BIT(offset))
				mdev->intr_ops->program_msi_to_src_map(mdev,
					entry, offset, true);
		}
	}
}
