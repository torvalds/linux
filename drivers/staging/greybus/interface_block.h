/*
 * Greybus Interface Block code
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#ifndef __INTERFACE_BLOCK_H
#define __INTERFACE_BLOCK_H

/* Increase these values if needed */
#define MAX_CPORTS_PER_MODULE	10
#define MAX_STRINGS_PER_MODULE	10


/* Greybus "public" definitions" */
struct gb_interface_block {
	struct device dev;

	struct list_head interfaces;
	struct list_head links;	/* greybus_host_device->modules */
	u8 module_id;		/* Physical location within the Endo */

	/* Information taken from the manifest module descriptor */
	u16 vendor;
	u16 product;
	char *vendor_string;
	char *product_string;
	u64 unique_id;

	struct greybus_host_device *hd;
};
#define to_gb_interface_block(d) container_of(d, struct gb_interface_block, dev)

static inline void
gb_interface_block_set_drvdata(struct gb_interface_block *gb_ib, void *data)
{
	dev_set_drvdata(&gb_ib->dev, data);
}

static inline void *
gb_interface_block_get_drvdata(struct gb_interface_block *gb_ib)
{
	return dev_get_drvdata(&gb_ib->dev);
}

/* Greybus "private" definitions */

const struct greybus_interface_block_id *
	gb_ib_match_id(struct gb_interface_block *gb_ib,
		       const struct greybus_interface_block_id *id);

struct gb_interface_block *gb_ib_find(struct greybus_host_device *hd,
				      u8 module_id);

#endif /* __INTERFACE_BLOCK_H */
