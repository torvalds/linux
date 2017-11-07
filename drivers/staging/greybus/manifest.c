// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus manifest parsing
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"

static const char *get_descriptor_type_string(u8 type)
{
	switch (type) {
	case GREYBUS_TYPE_INVALID:
		return "invalid";
	case GREYBUS_TYPE_STRING:
		return "string";
	case GREYBUS_TYPE_INTERFACE:
		return "interface";
	case GREYBUS_TYPE_CPORT:
		return "cport";
	case GREYBUS_TYPE_BUNDLE:
		return "bundle";
	default:
		WARN_ON(1);
		return "unknown";
	}
}

/*
 * We scan the manifest once to identify where all the descriptors
 * are.  The result is a list of these manifest_desc structures.  We
 * then pick through them for what we're looking for (starting with
 * the interface descriptor).  As each is processed we remove it from
 * the list.  When we're done the list should (probably) be empty.
 */
struct manifest_desc {
	struct list_head		links;

	size_t				size;
	void				*data;
	enum greybus_descriptor_type	type;
};

static void release_manifest_descriptor(struct manifest_desc *descriptor)
{
	list_del(&descriptor->links);
	kfree(descriptor);
}

static void release_manifest_descriptors(struct gb_interface *intf)
{
	struct manifest_desc *descriptor;
	struct manifest_desc *next;

	list_for_each_entry_safe(descriptor, next, &intf->manifest_descs, links)
		release_manifest_descriptor(descriptor);
}

static void release_cport_descriptors(struct list_head *head, u8 bundle_id)
{
	struct manifest_desc *desc, *tmp;
	struct greybus_descriptor_cport *desc_cport;

	list_for_each_entry_safe(desc, tmp, head, links) {
		desc_cport = desc->data;

		if (desc->type != GREYBUS_TYPE_CPORT)
			continue;

		if (desc_cport->bundle == bundle_id)
			release_manifest_descriptor(desc);
	}
}

static struct manifest_desc *get_next_bundle_desc(struct gb_interface *intf)
{
	struct manifest_desc *descriptor;
	struct manifest_desc *next;

	list_for_each_entry_safe(descriptor, next, &intf->manifest_descs, links)
		if (descriptor->type == GREYBUS_TYPE_BUNDLE)
			return descriptor;

	return NULL;
}

/*
 * Validate the given descriptor.  Its reported size must fit within
 * the number of bytes remaining, and it must have a recognized
 * type.  Check that the reported size is at least as big as what
 * we expect to see.  (It could be bigger, perhaps for a new version
 * of the format.)
 *
 * Returns the (non-zero) number of bytes consumed by the descriptor,
 * or a negative errno.
 */
static int identify_descriptor(struct gb_interface *intf,
			       struct greybus_descriptor *desc, size_t size)
{
	struct greybus_descriptor_header *desc_header = &desc->header;
	struct manifest_desc *descriptor;
	size_t desc_size;
	size_t expected_size;

	if (size < sizeof(*desc_header)) {
		dev_err(&intf->dev, "manifest too small (%zu < %zu)\n",
				size, sizeof(*desc_header));
		return -EINVAL;		/* Must at least have header */
	}

	desc_size = le16_to_cpu(desc_header->size);
	if (desc_size > size) {
		dev_err(&intf->dev, "descriptor too big (%zu > %zu)\n",
				desc_size, size);
		return -EINVAL;
	}

	/* Descriptor needs to at least have a header */
	expected_size = sizeof(*desc_header);

	switch (desc_header->type) {
	case GREYBUS_TYPE_STRING:
		expected_size += sizeof(struct greybus_descriptor_string);
		expected_size += desc->string.length;

		/* String descriptors are padded to 4 byte boundaries */
		expected_size = ALIGN(expected_size, 4);
		break;
	case GREYBUS_TYPE_INTERFACE:
		expected_size += sizeof(struct greybus_descriptor_interface);
		break;
	case GREYBUS_TYPE_BUNDLE:
		expected_size += sizeof(struct greybus_descriptor_bundle);
		break;
	case GREYBUS_TYPE_CPORT:
		expected_size += sizeof(struct greybus_descriptor_cport);
		break;
	case GREYBUS_TYPE_INVALID:
	default:
		dev_err(&intf->dev, "invalid descriptor type (%u)\n",
				desc_header->type);
		return -EINVAL;
	}

	if (desc_size < expected_size) {
		dev_err(&intf->dev, "%s descriptor too small (%zu < %zu)\n",
				get_descriptor_type_string(desc_header->type),
				desc_size, expected_size);
		return -EINVAL;
	}

	/* Descriptor bigger than what we expect */
	if (desc_size > expected_size) {
		dev_warn(&intf->dev, "%s descriptor size mismatch (want %zu got %zu)\n",
				get_descriptor_type_string(desc_header->type),
				expected_size, desc_size);
	}

	descriptor = kzalloc(sizeof(*descriptor), GFP_KERNEL);
	if (!descriptor)
		return -ENOMEM;

	descriptor->size = desc_size;
	descriptor->data = (char *)desc + sizeof(*desc_header);
	descriptor->type = desc_header->type;
	list_add_tail(&descriptor->links, &intf->manifest_descs);

	/* desc_size is positive and is known to fit in a signed int */

	return desc_size;
}

/*
 * Find the string descriptor having the given id, validate it, and
 * allocate a duplicate copy of it.  The duplicate has an extra byte
 * which guarantees the returned string is NUL-terminated.
 *
 * String index 0 is valid (it represents "no string"), and for
 * that a null pointer is returned.
 *
 * Otherwise returns a pointer to a newly-allocated copy of the
 * descriptor string, or an error-coded pointer on failure.
 */
static char *gb_string_get(struct gb_interface *intf, u8 string_id)
{
	struct greybus_descriptor_string *desc_string;
	struct manifest_desc *descriptor;
	bool found = false;
	char *string;

	/* A zero string id means no string (but no error) */
	if (!string_id)
		return NULL;

	list_for_each_entry(descriptor, &intf->manifest_descs, links) {
		if (descriptor->type != GREYBUS_TYPE_STRING)
			continue;

		desc_string = descriptor->data;
		if (desc_string->id == string_id) {
			found = true;
			break;
		}
	}
	if (!found)
		return ERR_PTR(-ENOENT);

	/* Allocate an extra byte so we can guarantee it's NUL-terminated */
	string = kmemdup(&desc_string->string, desc_string->length + 1,
				GFP_KERNEL);
	if (!string)
		return ERR_PTR(-ENOMEM);
	string[desc_string->length] = '\0';

	/* Ok we've used this string, so we're done with it */
	release_manifest_descriptor(descriptor);

	return string;
}

/*
 * Find cport descriptors in the manifest associated with the given
 * bundle, and set up data structures for the functions that use
 * them.  Returns the number of cports set up for the bundle, or 0
 * if there is an error.
 */
static u32 gb_manifest_parse_cports(struct gb_bundle *bundle)
{
	struct gb_interface *intf = bundle->intf;
	struct greybus_descriptor_cport *desc_cport;
	struct manifest_desc *desc, *next, *tmp;
	LIST_HEAD(list);
	u8 bundle_id = bundle->id;
	u16 cport_id;
	u32 count = 0;
	int i;

	/* Set up all cport descriptors associated with this bundle */
	list_for_each_entry_safe(desc, next, &intf->manifest_descs, links) {
		if (desc->type != GREYBUS_TYPE_CPORT)
			continue;

		desc_cport = desc->data;
		if (desc_cport->bundle != bundle_id)
			continue;

		cport_id = le16_to_cpu(desc_cport->id);
		if (cport_id > CPORT_ID_MAX)
			goto exit;

		/* Nothing else should have its cport_id as control cport id */
		if (cport_id == GB_CONTROL_CPORT_ID) {
			dev_err(&bundle->dev, "invalid cport id found (%02u)\n",
				cport_id);
			goto exit;
		}

		/*
		 * Found one, move it to our temporary list after checking for
		 * duplicates.
		 */
		list_for_each_entry(tmp, &list, links) {
			desc_cport = tmp->data;
			if (cport_id == le16_to_cpu(desc_cport->id)) {
				dev_err(&bundle->dev,
						"duplicate CPort %u found\n",
						cport_id);
				goto exit;
			}
		}
		list_move_tail(&desc->links, &list);
		count++;
	}

	if (!count)
		return 0;

	bundle->cport_desc = kcalloc(count, sizeof(*bundle->cport_desc),
					GFP_KERNEL);
	if (!bundle->cport_desc)
		goto exit;

	bundle->num_cports = count;

	i = 0;
	list_for_each_entry_safe(desc, next, &list, links) {
		desc_cport = desc->data;
		memcpy(&bundle->cport_desc[i++], desc_cport,
				sizeof(*desc_cport));

		/* Release the cport descriptor */
		release_manifest_descriptor(desc);
	}

	return count;
exit:
	release_cport_descriptors(&list, bundle_id);
	/*
	 * Free all cports for this bundle to avoid 'excess descriptors'
	 * warnings.
	 */
	release_cport_descriptors(&intf->manifest_descs, bundle_id);

	return 0;	/* Error; count should also be 0 */
}

/*
 * Find bundle descriptors in the manifest and set up their data
 * structures.  Returns the number of bundles set up for the
 * given interface.
 */
static u32 gb_manifest_parse_bundles(struct gb_interface *intf)
{
	struct manifest_desc *desc;
	struct gb_bundle *bundle;
	struct gb_bundle *bundle_next;
	u32 count = 0;
	u8 bundle_id;
	u8 class;

	while ((desc = get_next_bundle_desc(intf))) {
		struct greybus_descriptor_bundle *desc_bundle;

		/* Found one.  Set up its bundle structure*/
		desc_bundle = desc->data;
		bundle_id = desc_bundle->id;
		class = desc_bundle->class;

		/* Done with this bundle descriptor */
		release_manifest_descriptor(desc);

		/* Ignore any legacy control bundles */
		if (bundle_id == GB_CONTROL_BUNDLE_ID) {
			dev_dbg(&intf->dev, "%s - ignoring control bundle\n",
					__func__);
			release_cport_descriptors(&intf->manifest_descs,
								bundle_id);
			continue;
		}

		/* Nothing else should have its class set to control class */
		if (class == GREYBUS_CLASS_CONTROL) {
			dev_err(&intf->dev,
				"bundle %u cannot use control class\n",
				bundle_id);
			goto cleanup;
		}

		bundle = gb_bundle_create(intf, bundle_id, class);
		if (!bundle)
			goto cleanup;

		/*
		 * Now go set up this bundle's functions and cports.
		 *
		 * A 'bundle' represents a device in greybus. It may require
		 * multiple cports for its functioning. If we fail to setup any
		 * cport of a bundle, we better reject the complete bundle as
		 * the device may not be able to function properly then.
		 *
		 * But, failing to setup a cport of bundle X doesn't mean that
		 * the device corresponding to bundle Y will not work properly.
		 * Bundles should be treated as separate independent devices.
		 *
		 * While parsing manifest for an interface, treat bundles as
		 * separate entities and don't reject entire interface and its
		 * bundles on failing to initialize a cport. But make sure the
		 * bundle which needs the cport, gets destroyed properly.
		 */
		if (!gb_manifest_parse_cports(bundle)) {
			gb_bundle_destroy(bundle);
			continue;
		}

		count++;
	}

	return count;
cleanup:
	/* An error occurred; undo any changes we've made */
	list_for_each_entry_safe(bundle, bundle_next, &intf->bundles, links) {
		gb_bundle_destroy(bundle);
		count--;
	}
	return 0;	/* Error; count should also be 0 */
}

static bool gb_manifest_parse_interface(struct gb_interface *intf,
					struct manifest_desc *interface_desc)
{
	struct greybus_descriptor_interface *desc_intf = interface_desc->data;
	struct gb_control *control = intf->control;
	char *str;

	/* Handle the strings first--they can fail */
	str = gb_string_get(intf, desc_intf->vendor_stringid);
	if (IS_ERR(str))
		return false;
	control->vendor_string = str;

	str = gb_string_get(intf, desc_intf->product_stringid);
	if (IS_ERR(str))
		goto out_free_vendor_string;
	control->product_string = str;

	/* Assign feature flags communicated via manifest */
	intf->features = desc_intf->features;

	/* Release the interface descriptor, now that we're done with it */
	release_manifest_descriptor(interface_desc);

	/* An interface must have at least one bundle descriptor */
	if (!gb_manifest_parse_bundles(intf)) {
		dev_err(&intf->dev, "manifest bundle descriptors not valid\n");
		goto out_err;
	}

	return true;
out_err:
	kfree(control->product_string);
	control->product_string = NULL;
out_free_vendor_string:
	kfree(control->vendor_string);
	control->vendor_string = NULL;

	return false;
}

/*
 * Parse a buffer containing an interface manifest.
 *
 * If we find anything wrong with the content/format of the buffer
 * we reject it.
 *
 * The first requirement is that the manifest's version is
 * one we can parse.
 *
 * We make an initial pass through the buffer and identify all of
 * the descriptors it contains, keeping track for each its type
 * and the location size of its data in the buffer.
 *
 * Next we scan the descriptors, looking for an interface descriptor;
 * there must be exactly one of those.  When found, we record the
 * information it contains, and then remove that descriptor (and any
 * string descriptors it refers to) from further consideration.
 *
 * After that we look for the interface's bundles--there must be at
 * least one of those.
 *
 * Returns true if parsing was successful, false otherwise.
 */
bool gb_manifest_parse(struct gb_interface *intf, void *data, size_t size)
{
	struct greybus_manifest *manifest;
	struct greybus_manifest_header *header;
	struct greybus_descriptor *desc;
	struct manifest_desc *descriptor;
	struct manifest_desc *interface_desc = NULL;
	u16 manifest_size;
	u32 found = 0;
	bool result;

	/* Manifest descriptor list should be empty here */
	if (WARN_ON(!list_empty(&intf->manifest_descs)))
		return false;

	/* we have to have at _least_ the manifest header */
	if (size < sizeof(*header)) {
		dev_err(&intf->dev, "short manifest (%zu < %zu)\n",
				size, sizeof(*header));
		return false;
	}

	/* Make sure the size is right */
	manifest = data;
	header = &manifest->header;
	manifest_size = le16_to_cpu(header->size);
	if (manifest_size != size) {
		dev_err(&intf->dev, "manifest size mismatch (%zu != %u)\n",
				size, manifest_size);
		return false;
	}

	/* Validate major/minor number */
	if (header->version_major > GREYBUS_VERSION_MAJOR) {
		dev_err(&intf->dev, "manifest version too new (%u.%u > %u.%u)\n",
				header->version_major, header->version_minor,
				GREYBUS_VERSION_MAJOR, GREYBUS_VERSION_MINOR);
		return false;
	}

	/* OK, find all the descriptors */
	desc = manifest->descriptors;
	size -= sizeof(*header);
	while (size) {
		int desc_size;

		desc_size = identify_descriptor(intf, desc, size);
		if (desc_size < 0) {
			result = false;
			goto out;
		}
		desc = (struct greybus_descriptor *)((char *)desc + desc_size);
		size -= desc_size;
	}

	/* There must be a single interface descriptor */
	list_for_each_entry(descriptor, &intf->manifest_descs, links) {
		if (descriptor->type == GREYBUS_TYPE_INTERFACE)
			if (!found++)
				interface_desc = descriptor;
	}
	if (found != 1) {
		dev_err(&intf->dev, "manifest must have 1 interface descriptor (%u found)\n",
				found);
		result = false;
		goto out;
	}

	/* Parse the manifest, starting with the interface descriptor */
	result = gb_manifest_parse_interface(intf, interface_desc);

	/*
	 * We really should have no remaining descriptors, but we
	 * don't know what newer format manifests might leave.
	 */
	if (result && !list_empty(&intf->manifest_descs))
		dev_info(&intf->dev, "excess descriptors in interface manifest\n");
out:
	release_manifest_descriptors(intf);

	return result;
}
