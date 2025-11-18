// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/array_size.h>
#include <linux/build_bug.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/netlink.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "core.h"
#include "flash.h"
#include "fw.h"

#define ZL3073X_FW_ERR_PFX "FW load failed: "
#define ZL3073X_FW_ERR_MSG(_extack, _msg, ...)				\
	NL_SET_ERR_MSG_FMT_MOD((_extack), ZL3073X_FW_ERR_PFX _msg,	\
			       ## __VA_ARGS__)

enum zl3073x_flash_type {
	ZL3073X_FLASH_TYPE_NONE = 0,
	ZL3073X_FLASH_TYPE_SECTORS,
	ZL3073X_FLASH_TYPE_PAGE,
	ZL3073X_FLASH_TYPE_PAGE_AND_COPY,
};

struct zl3073x_fw_component_info {
	const char		*name;
	size_t			max_size;
	enum zl3073x_flash_type	flash_type;
	u32			load_addr;
	u32			dest_page;
	u32			copy_page;
};

static const struct zl3073x_fw_component_info component_info[] = {
	[ZL_FW_COMPONENT_UTIL] = {
		.name		= "utility",
		.max_size	= 0x4000,
		.load_addr	= 0x20000000,
		.flash_type	= ZL3073X_FLASH_TYPE_NONE,
	},
	[ZL_FW_COMPONENT_FW1] = {
		.name		= "firmware1",
		.max_size	= 0x35000,
		.load_addr	= 0x20002000,
		.flash_type	= ZL3073X_FLASH_TYPE_SECTORS,
		.dest_page	= 0x020,
	},
	[ZL_FW_COMPONENT_FW2] = {
		.name		= "firmware2",
		.max_size	= 0x0040,
		.load_addr	= 0x20000000,
		.flash_type	= ZL3073X_FLASH_TYPE_PAGE_AND_COPY,
		.dest_page	= 0x3e0,
		.copy_page	= 0x000,
	},
	[ZL_FW_COMPONENT_FW3] = {
		.name		= "firmware3",
		.max_size	= 0x0248,
		.load_addr	= 0x20000400,
		.flash_type	= ZL3073X_FLASH_TYPE_PAGE_AND_COPY,
		.dest_page	= 0x3e4,
		.copy_page	= 0x004,
	},
	[ZL_FW_COMPONENT_CFG0] = {
		.name		= "config0",
		.max_size	= 0x1000,
		.load_addr	= 0x20000000,
		.flash_type	= ZL3073X_FLASH_TYPE_PAGE,
		.dest_page	= 0x3d0,
	},
	[ZL_FW_COMPONENT_CFG1] = {
		.name		= "config1",
		.max_size	= 0x1000,
		.load_addr	= 0x20000000,
		.flash_type	= ZL3073X_FLASH_TYPE_PAGE,
		.dest_page	= 0x3c0,
	},
	[ZL_FW_COMPONENT_CFG2] = {
		.name		= "config2",
		.max_size	= 0x1000,
		.load_addr	= 0x20000000,
		.flash_type	= ZL3073X_FLASH_TYPE_PAGE,
		.dest_page	= 0x3b0,
	},
	[ZL_FW_COMPONENT_CFG3] = {
		.name		= "config3",
		.max_size	= 0x1000,
		.load_addr	= 0x20000000,
		.flash_type	= ZL3073X_FLASH_TYPE_PAGE,
		.dest_page	= 0x3a0,
	},
	[ZL_FW_COMPONENT_CFG4] = {
		.name		= "config4",
		.max_size	= 0x1000,
		.load_addr	= 0x20000000,
		.flash_type	= ZL3073X_FLASH_TYPE_PAGE,
		.dest_page	= 0x390,
	},
	[ZL_FW_COMPONENT_CFG5] = {
		.name		= "config5",
		.max_size	= 0x1000,
		.load_addr	= 0x20000000,
		.flash_type	= ZL3073X_FLASH_TYPE_PAGE,
		.dest_page	= 0x380,
	},
	[ZL_FW_COMPONENT_CFG6] = {
		.name		= "config6",
		.max_size	= 0x1000,
		.load_addr	= 0x20000000,
		.flash_type	= ZL3073X_FLASH_TYPE_PAGE,
		.dest_page	= 0x370,
	},
};

/* Sanity check */
static_assert(ARRAY_SIZE(component_info) == ZL_FW_NUM_COMPONENTS);

/**
 * zl3073x_fw_component_alloc - Alloc structure to hold firmware component
 * @size: size of buffer to store data
 *
 * Return: pointer to allocated component structure or NULL on error.
 */
static struct zl3073x_fw_component *
zl3073x_fw_component_alloc(size_t size)
{
	struct zl3073x_fw_component *comp;

	comp = kzalloc(sizeof(*comp), GFP_KERNEL);
	if (!comp)
		return NULL;

	comp->size = size;
	comp->data = kzalloc(size, GFP_KERNEL);
	if (!comp->data) {
		kfree(comp);
		return NULL;
	}

	return comp;
}

/**
 * zl3073x_fw_component_free - Free allocated component structure
 * @comp: pointer to allocated component
 */
static void
zl3073x_fw_component_free(struct zl3073x_fw_component *comp)
{
	if (comp)
		kfree(comp->data);

	kfree(comp);
}

/**
 * zl3073x_fw_component_id_get - Get ID for firmware component name
 * @name: input firmware component name
 *
 * Return:
 * - ZL3073X_FW_COMPONENT_* ID for known component name
 * - ZL3073X_FW_COMPONENT_INVALID if the given name is unknown
 */
static enum zl3073x_fw_component_id
zl3073x_fw_component_id_get(const char *name)
{
	enum zl3073x_fw_component_id id;

	for (id = 0; id < ZL_FW_NUM_COMPONENTS; id++)
		if (!strcasecmp(name, component_info[id].name))
			return id;

	return ZL_FW_COMPONENT_INVALID;
}

/**
 * zl3073x_fw_component_load - Load component from firmware source
 * @zldev: zl3073x device structure
 * @pcomp: pointer to loaded component
 * @psrc: data pointer to load component from
 * @psize: remaining bytes in buffer
 * @extack: netlink extack pointer to report errors
 *
 * The function allocates single firmware component and loads the data from
 * the buffer specified by @psrc and @psize. Pointer to allocated component
 * is stored in output @pcomp. Source data pointer @psrc and remaining bytes
 * @psize are updated accordingly.
 *
 * Return:
 * * 1 when component was allocated and loaded
 * * 0 when there is no component to load
 * * <0 on error
 */
static ssize_t
zl3073x_fw_component_load(struct zl3073x_dev *zldev,
			  struct zl3073x_fw_component **pcomp,
			  const char **psrc, size_t *psize,
			  struct netlink_ext_ack *extack)
{
	const struct zl3073x_fw_component_info *info;
	struct zl3073x_fw_component *comp = NULL;
	struct device *dev = zldev->dev;
	enum zl3073x_fw_component_id id;
	char buf[32], name[16];
	u32 count, size, *dest;
	int pos, rc;

	/* Fetch image name and size from input */
	strscpy(buf, *psrc, min(sizeof(buf), *psize));
	rc = sscanf(buf, "%15s %u %n", name, &count, &pos);
	if (!rc) {
		/* No more data */
		return 0;
	} else if (rc == 1 || count > U32_MAX / sizeof(u32)) {
		ZL3073X_FW_ERR_MSG(extack, "invalid component size");
		return -EINVAL;
	}
	*psrc += pos;
	*psize -= pos;

	dev_dbg(dev, "Firmware component '%s' found\n", name);

	id = zl3073x_fw_component_id_get(name);
	if (id == ZL_FW_COMPONENT_INVALID) {
		ZL3073X_FW_ERR_MSG(extack, "unknown component type '%s'", name);
		return -EINVAL;
	}

	info = &component_info[id];
	size = count * sizeof(u32); /* get size in bytes */

	/* Check image size validity */
	if (size > component_info[id].max_size) {
		ZL3073X_FW_ERR_MSG(extack,
				   "[%s] component is too big (%u bytes)\n",
				   info->name, size);
		return -EINVAL;
	}

	dev_dbg(dev, "Indicated component image size: %u bytes\n", size);

	/* Alloc component */
	comp = zl3073x_fw_component_alloc(size);
	if (!comp) {
		ZL3073X_FW_ERR_MSG(extack, "failed to alloc memory");
		return -ENOMEM;
	}
	comp->id = id;

	/* Load component data from firmware source */
	for (dest = comp->data; count; count--, dest++) {
		strscpy(buf, *psrc, min(sizeof(buf), *psize));
		rc = sscanf(buf, "%x %n", dest, &pos);
		if (!rc)
			goto err_data;

		*psrc += pos;
		*psize -= pos;
	}

	*pcomp = comp;

	return 1;

err_data:
	ZL3073X_FW_ERR_MSG(extack, "[%s] invalid or missing data", info->name);

	zl3073x_fw_component_free(comp);

	return -ENODATA;
}

/**
 * zl3073x_fw_free - Free allocated firmware
 * @fw: firmware pointer
 *
 * The function frees existing firmware allocated by @zl3073x_fw_load.
 */
void zl3073x_fw_free(struct zl3073x_fw *fw)
{
	size_t i;

	if (!fw)
		return;

	for (i = 0; i < ZL_FW_NUM_COMPONENTS; i++)
		zl3073x_fw_component_free(fw->component[i]);

	kfree(fw);
}

/**
 * zl3073x_fw_load - Load all components from source
 * @zldev: zl3073x device structure
 * @data: source buffer pointer
 * @size: size of source buffer
 * @extack: netlink extack pointer to report errors
 *
 * The functions allocate firmware structure and loads all components from
 * the given buffer specified by @data and @size.
 *
 * Return: pointer to firmware on success, error pointer on error
 */
struct zl3073x_fw *zl3073x_fw_load(struct zl3073x_dev *zldev, const char *data,
				   size_t size, struct netlink_ext_ack *extack)
{
	struct zl3073x_fw_component *comp;
	enum zl3073x_fw_component_id id;
	struct zl3073x_fw *fw;
	ssize_t rc;

	/* Allocate firmware structure */
	fw = kzalloc(sizeof(*fw), GFP_KERNEL);
	if (!fw)
		return ERR_PTR(-ENOMEM);

	do {
		/* Load single component */
		rc = zl3073x_fw_component_load(zldev, &comp, &data, &size,
					       extack);
		if (rc <= 0)
			/* Everything was read or error occurred */
			break;

		id = comp->id;

		/* Report error if the given component is present twice
		 * or more.
		 */
		if (fw->component[id]) {
			ZL3073X_FW_ERR_MSG(extack,
					   "duplicate component '%s' detected",
					   component_info[id].name);
			zl3073x_fw_component_free(comp);
			rc = -EINVAL;
			break;
		}

		fw->component[id] = comp;
	} while (true);

	if (rc) {
		/* Free allocated firmware in case of error */
		zl3073x_fw_free(fw);
		return ERR_PTR(rc);
	}

	return fw;
}

/**
 * zl3073x_flash_bundle_flash - Flash all components
 * @zldev: zl3073x device structure
 * @components: pointer to components array
 * @extack: netlink extack pointer to report errors
 *
 * Returns 0 in case of success or negative number otherwise.
 */
static int
zl3073x_fw_component_flash(struct zl3073x_dev *zldev,
			   struct zl3073x_fw_component *comp,
			   struct netlink_ext_ack *extack)
{
	const struct zl3073x_fw_component_info *info;
	int rc;

	info = &component_info[comp->id];

	switch (info->flash_type) {
	case ZL3073X_FLASH_TYPE_NONE:
		/* Non-flashable component - used for utility */
		return 0;
	case ZL3073X_FLASH_TYPE_SECTORS:
		rc = zl3073x_flash_sectors(zldev, info->name, info->dest_page,
					   info->load_addr, comp->data,
					   comp->size, extack);
		break;
	case ZL3073X_FLASH_TYPE_PAGE:
		rc = zl3073x_flash_page(zldev, info->name, info->dest_page,
					info->load_addr, comp->data, comp->size,
					extack);
		break;
	case ZL3073X_FLASH_TYPE_PAGE_AND_COPY:
		rc = zl3073x_flash_page(zldev, info->name, info->dest_page,
					info->load_addr, comp->data, comp->size,
					extack);
		if (!rc)
			rc = zl3073x_flash_page_copy(zldev, info->name,
						     info->dest_page,
						     info->copy_page, extack);
		break;
	}
	if (rc)
		ZL3073X_FW_ERR_MSG(extack, "Failed to flash component '%s'",
				   info->name);

	return rc;
}

int zl3073x_fw_flash(struct zl3073x_dev *zldev, struct zl3073x_fw *zlfw,
		     struct netlink_ext_ack *extack)
{
	int i, rc = 0;

	for (i = 0; i < ZL_FW_NUM_COMPONENTS; i++) {
		if (!zlfw->component[i])
			continue; /* Component is not present */

		rc = zl3073x_fw_component_flash(zldev, zlfw->component[i],
						extack);
		if (rc)
			break;
	}

	return rc;
}
