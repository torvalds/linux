/*
 * Copyright (C) ST-Ericsson AB 2012
 * Author: Sjur Brændeland <sjur.brandeland@stericsson.com>
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/remoteproc.h>
#include <linux/ste_modem_shm.h>
#include "remoteproc_internal.h"

#define SPROC_FW_SIZE (50 * 4096)
#define SPROC_MAX_TOC_ENTRIES 32
#define SPROC_MAX_NOTIFY_ID 14
#define SPROC_RESOURCE_NAME "rsc-table"
#define SPROC_MODEM_NAME "ste-modem"
#define SPROC_MODEM_FIRMWARE SPROC_MODEM_NAME "-fw.bin"

#define sproc_dbg(sproc, fmt, ...) \
	dev_dbg(&sproc->mdev->pdev.dev, fmt, ##__VA_ARGS__)
#define sproc_err(sproc, fmt, ...) \
	dev_err(&sproc->mdev->pdev.dev, fmt, ##__VA_ARGS__)

/* STE-modem control structure */
struct sproc {
	struct rproc *rproc;
	struct ste_modem_device *mdev;
	int error;
	void *fw_addr;
	size_t fw_size;
	dma_addr_t fw_dma_addr;
};

/* STE-Modem firmware entry */
struct ste_toc_entry {
	__le32 start;
	__le32 size;
	__le32 flags;
	__le32 entry_point;
	__le32 load_addr;
	char name[12];
};

/*
 * The Table Of Content is located at the start of the firmware image and
 * at offset zero in the shared memory region. The resource table typically
 * contains the initial boot image (boot strap) and other information elements
 * such as remoteproc resource table. Each entry is identified by a unique
 * name.
 */
struct ste_toc {
	struct ste_toc_entry table[SPROC_MAX_TOC_ENTRIES];
};

/* Loads the firmware to shared memory. */
static int sproc_load_segments(struct rproc *rproc, const struct firmware *fw)
{
	struct sproc *sproc = rproc->priv;

	memcpy(sproc->fw_addr, fw->data, fw->size);

	return 0;
}

/* Find the entry for resource table in the Table of Content */
static const struct ste_toc_entry *sproc_find_rsc_entry(const void *data)
{
	int i;
	const struct ste_toc *toc;
	toc = data;

	/* Search the table for the resource table */
	for (i = 0; i < SPROC_MAX_TOC_ENTRIES &&
		    toc->table[i].start != 0xffffffff; i++) {
		if (!strncmp(toc->table[i].name, SPROC_RESOURCE_NAME,
			     sizeof(toc->table[i].name)))
			return &toc->table[i];
	}

	return NULL;
}

/* Find the resource table inside the remote processor's firmware. */
static struct resource_table *
sproc_find_rsc_table(struct rproc *rproc, const struct firmware *fw,
		     int *tablesz)
{
	struct sproc *sproc = rproc->priv;
	struct resource_table *table;
	const struct ste_toc_entry *entry;

	if (!fw)
		return NULL;

	entry = sproc_find_rsc_entry(fw->data);
	if (!entry) {
		sproc_err(sproc, "resource table not found in fw\n");
		return NULL;
	}

	table = (void *)(fw->data + entry->start);

	/* sanity check size and offset of resource table */
	if (entry->start > SPROC_FW_SIZE ||
	    entry->size > SPROC_FW_SIZE ||
	    fw->size > SPROC_FW_SIZE ||
	    entry->start + entry->size > fw->size ||
	    sizeof(struct resource_table) > entry->size) {
		sproc_err(sproc, "bad size of fw or resource table\n");
		return NULL;
	}

	/* we don't support any version beyond the first */
	if (table->ver != 1) {
		sproc_err(sproc, "unsupported fw ver: %d\n", table->ver);
		return NULL;
	}

	/* make sure reserved bytes are zeroes */
	if (table->reserved[0] || table->reserved[1]) {
		sproc_err(sproc, "non zero reserved bytes\n");
		return NULL;
	}

	/* make sure the offsets array isn't truncated */
	if (table->num > SPROC_MAX_TOC_ENTRIES ||
	    table->num * sizeof(table->offset[0]) +
	    sizeof(struct resource_table) > entry->size) {
		sproc_err(sproc, "resource table incomplete\n");
		return NULL;
	}

	/* If the fw size has grown, release the previous fw allocation */
	if (SPROC_FW_SIZE < fw->size) {
		sproc_err(sproc, "Insufficient space for fw (%d < %zd)\n",
			  SPROC_FW_SIZE, fw->size);
		return NULL;
	}

	sproc->fw_size = fw->size;
	*tablesz = entry->size;

	return table;
}

/* Find the resource table inside the remote processor's firmware. */
static struct resource_table *
sproc_find_loaded_rsc_table(struct rproc *rproc, const struct firmware *fw)
{
	struct sproc *sproc = rproc->priv;
	const struct ste_toc_entry *entry;

	if (!fw || !sproc->fw_addr)
		return NULL;

	entry = sproc_find_rsc_entry(sproc->fw_addr);
	if (!entry) {
		sproc_err(sproc, "resource table not found in fw\n");
		return NULL;
	}

	return sproc->fw_addr + entry->start;
}

/* STE modem firmware handler operations */
static const struct rproc_fw_ops sproc_fw_ops = {
	.load = sproc_load_segments,
	.find_rsc_table = sproc_find_rsc_table,
	.find_loaded_rsc_table = sproc_find_loaded_rsc_table,
};

/* Kick the modem with specified notification id */
static void sproc_kick(struct rproc *rproc, int vqid)
{
	struct sproc *sproc = rproc->priv;

	sproc_dbg(sproc, "kick vqid:%d\n", vqid);

	/*
	 * We need different notification IDs for RX and TX so add
	 * an offset on TX notification IDs.
	 */
	sproc->mdev->ops.kick(sproc->mdev, vqid + SPROC_MAX_NOTIFY_ID);
}

/* Received a kick from a modem, kick the virtqueue */
static void sproc_kick_callback(struct ste_modem_device *mdev, int vqid)
{
	struct sproc *sproc = mdev->drv_data;

	if (rproc_vq_interrupt(sproc->rproc, vqid) == IRQ_NONE)
		sproc_dbg(sproc, "no message was found in vqid %d\n", vqid);
}

static struct ste_modem_dev_cb sproc_dev_cb = {
	.kick = sproc_kick_callback,
};

/* Start the STE modem */
static int sproc_start(struct rproc *rproc)
{
	struct sproc *sproc = rproc->priv;
	int i, err;

	sproc_dbg(sproc, "start ste-modem\n");

	/* Sanity test the max_notifyid */
	if (rproc->max_notifyid > SPROC_MAX_NOTIFY_ID) {
		sproc_err(sproc, "Notification IDs too high:%d\n",
			  rproc->max_notifyid);
		return -EINVAL;
	}

	/* Subscribe to notifications */
	for (i = 0; i <= rproc->max_notifyid; i++) {
		err = sproc->mdev->ops.kick_subscribe(sproc->mdev, i);
		if (err) {
			sproc_err(sproc,
				  "subscription of kicks failed:%d\n", err);
			return err;
		}
	}

	/* Request modem start-up*/
	return sproc->mdev->ops.power(sproc->mdev, true);
}

/* Stop the STE modem */
static int sproc_stop(struct rproc *rproc)
{
	struct sproc *sproc = rproc->priv;
	sproc_dbg(sproc, "stop ste-modem\n");

	return sproc->mdev->ops.power(sproc->mdev, false);
}

static struct rproc_ops sproc_ops = {
	.start		= sproc_start,
	.stop		= sproc_stop,
	.kick		= sproc_kick,
};

/* STE modem device is unregistered */
static int sproc_drv_remove(struct platform_device *pdev)
{
	struct ste_modem_device *mdev =
		container_of(pdev, struct ste_modem_device, pdev);
	struct sproc *sproc = mdev->drv_data;

	sproc_dbg(sproc, "remove ste-modem\n");

	/* Reset device callback functions */
	sproc->mdev->ops.setup(sproc->mdev, NULL);

	/* Unregister as remoteproc device */
	rproc_del(sproc->rproc);
	dma_free_coherent(sproc->rproc->dev.parent, SPROC_FW_SIZE,
			  sproc->fw_addr, sproc->fw_dma_addr);
	rproc_put(sproc->rproc);

	mdev->drv_data = NULL;

	return 0;
}

/* Handle probe of a modem device */
static int sproc_probe(struct platform_device *pdev)
{
	struct ste_modem_device *mdev =
		container_of(pdev, struct ste_modem_device, pdev);
	struct sproc *sproc;
	struct rproc *rproc;
	int err;

	dev_dbg(&mdev->pdev.dev, "probe ste-modem\n");

	if (!mdev->ops.setup || !mdev->ops.kick || !mdev->ops.kick_subscribe ||
	    !mdev->ops.power) {
		dev_err(&mdev->pdev.dev, "invalid mdev ops\n");
		return -EINVAL;
	}

	rproc = rproc_alloc(&mdev->pdev.dev, mdev->pdev.name, &sproc_ops,
			    SPROC_MODEM_FIRMWARE, sizeof(*sproc));
	if (!rproc)
		return -ENOMEM;

	sproc = rproc->priv;
	sproc->mdev = mdev;
	sproc->rproc = rproc;
	mdev->drv_data = sproc;

	/* Provide callback functions to modem device */
	sproc->mdev->ops.setup(sproc->mdev, &sproc_dev_cb);

	/* Set the STE-modem specific firmware handler */
	rproc->fw_ops = &sproc_fw_ops;

	/*
	 * STE-modem requires the firmware to be located
	 * at the start of the shared memory region. So we need to
	 * reserve space for firmware at the start.
	 */
	sproc->fw_addr = dma_alloc_coherent(rproc->dev.parent, SPROC_FW_SIZE,
					    &sproc->fw_dma_addr,
					    GFP_KERNEL);
	if (!sproc->fw_addr) {
		sproc_err(sproc, "Cannot allocate memory for fw\n");
		err = -ENOMEM;
		goto free_rproc;
	}

	/* Register as a remoteproc device */
	err = rproc_add(rproc);
	if (err)
		goto free_mem;

	return 0;

free_mem:
	dma_free_coherent(rproc->dev.parent, SPROC_FW_SIZE,
			  sproc->fw_addr, sproc->fw_dma_addr);
free_rproc:
	/* Reset device data upon error */
	mdev->drv_data = NULL;
	rproc_put(rproc);
	return err;
}

static struct platform_driver sproc_driver = {
	.driver	= {
		.name	= SPROC_MODEM_NAME,
		.owner	= THIS_MODULE,
	},
	.probe	= sproc_probe,
	.remove	= sproc_drv_remove,
};

module_platform_driver(sproc_driver);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("STE Modem driver using the Remote Processor Framework");
