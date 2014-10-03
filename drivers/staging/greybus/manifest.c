/*
 * Greybus module manifest parsing
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/err.h>

#include "greybus.h"

/*
 * We scan the manifest once to identify where all the descriptors
 * are.  The result is a list of these manifest_desc structures.  We
 * then pick through them for what we're looking for (starting with
 * the module descriptor).  As each is processed we remove it from
 * the list.  When we're done the list should (probably) be empty.
 */
struct manifest_desc {
	struct list_head		links;

	size_t				size;
	void				*data;
	enum greybus_descriptor_type	type;
};

static LIST_HEAD(manifest_descs);

static void release_manifest_descriptor(struct manifest_desc *descriptor)
{
	list_del(&descriptor->links);
	kfree(descriptor);
}

static void release_manifest_descriptors(void)
{
	struct manifest_desc *descriptor;
	struct manifest_desc *next;

	list_for_each_entry_safe(descriptor, next, &manifest_descs, links)
		release_manifest_descriptor(descriptor);
}

/*
 * Validate the given descriptor.  Its reported size must fit within
 * the number of bytes reamining, and it must have a recognized
 * type.  Check that the reported size is at least as big as what
 * we expect to see.  (It could be bigger, perhaps for a new version
 * of the format.)
 *
 * Returns the number of bytes consumed by the descriptor, or a
 * negative errno.
 */
static int identify_descriptor(struct greybus_descriptor *desc, size_t size)
{
	struct greybus_descriptor_header *desc_header = &desc->header;
	struct manifest_desc *descriptor;
	int desc_size;
	size_t expected_size;

	if (size < sizeof(*desc_header)) {
		pr_err("manifest too small\n");
		return -EINVAL;		/* Must at least have header */
	}

	desc_size = (int)le16_to_cpu(desc_header->size);
	if ((size_t)desc_size > size) {
		pr_err("descriptor too big\n");
		return -EINVAL;
	}

	switch (desc_header->type) {
	case GREYBUS_TYPE_MODULE:
		if (desc_size < sizeof(struct greybus_descriptor_module)) {
			pr_err("module descriptor too small (%u)\n",
				desc_size);
			return -EINVAL;
		}
		break;
	case GREYBUS_TYPE_STRING:
		expected_size = sizeof(struct greybus_descriptor_header);
		expected_size += sizeof(struct greybus_descriptor_string);
		expected_size += (size_t)desc->string.length;
		if (desc_size < expected_size) {
			pr_err("string descriptor too small (%u)\n",
				desc_size);
			return -EINVAL;
		}
		break;
	case GREYBUS_TYPE_INTERFACE:
		break;
	case GREYBUS_TYPE_CPORT:
		if (desc_size < sizeof(struct greybus_descriptor_cport)) {
			pr_err("cport descriptor too small (%u)\n",
				desc_size);
			return -EINVAL;
		}
		break;
	case GREYBUS_TYPE_CLASS:
		pr_warn("class descriptor found (ignoring)\n");
		break;
	case GREYBUS_TYPE_INVALID:
	default:
		pr_err("invalid descriptor type (%hhu)\n", desc_header->type);
		return -EINVAL;
	}

	descriptor = kzalloc(sizeof(*descriptor), GFP_KERNEL);
	if (!descriptor)
		return -ENOMEM;

	descriptor->size = desc_size;
	descriptor->data = desc;
	descriptor->type = desc_header->type;
	list_add_tail(&descriptor->links, &manifest_descs);

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
static char *gb_string_get(u8 string_id)
{
	struct greybus_descriptor_string *desc_string;
	struct manifest_desc *descriptor;
	bool found = false;
	char *string;

	/* A zero string id means no string (but no error) */
	if (!string_id)
		return NULL;

	list_for_each_entry(descriptor, &manifest_descs, links) {
		struct greybus_descriptor *desc;

		if (descriptor->type != GREYBUS_TYPE_STRING)
			continue;

		desc = descriptor->data;
		desc_string = &desc->string;
		if (desc_string->id == string_id) {
			found = true;
			break;
		}
	}
	if (!found)
		return ERR_PTR(-ENOENT);

	/* Allocate an extra byte so we can guarantee it's NUL-terminated */
	string = kmemdup(&desc_string->string, (size_t)desc_string->length + 1,
				GFP_KERNEL);
	if (!string)
		return ERR_PTR(-ENOMEM);
	string[desc_string->length] = '\0';

	/* Ok we've used this string, so we're done with it */
	release_manifest_descriptor(descriptor);

	return string;
}

/*
 * Find cport descriptors in the manifest and set up data structures
 * for the functions that use them.  Returns the number of interfaces
 * set up for the given module, or 0 if there is an error.
 */
static u32 gb_manifest_parse_cports(struct gb_interface *interface)
{
	u32 count = 0;

	while (true) {
		struct manifest_desc *descriptor;
		struct greybus_descriptor_cport *desc_cport;
		enum greybus_protocol protocol;
		u16 cport_id;
		bool found;

		/* Find a cport descriptor */
		found = false;
		list_for_each_entry(descriptor, &manifest_descs, links) {
			if (descriptor->type == GREYBUS_TYPE_CPORT) {
				desc_cport = descriptor->data;
				if (desc_cport->interface == interface->id) {
					found = true;
					break;
				}
			}
		}
		if (!found)
			break;

		/* Found one.  Set up its function structure */
		protocol = (enum greybus_protocol)desc_cport->protocol;
		cport_id = le16_to_cpu(desc_cport->id);
		if (!gb_connection_create(interface, cport_id, protocol))
			return 0;	/* Error */

		count++;
		/* Release the cport descriptor */
		release_manifest_descriptor(descriptor);
	}

	return count;
}

/*
 * Find interface descriptors in the manifest and set up their data
 * structures.  Returns the number of interfaces set up for the
 * given module.
 */
static u32 gb_manifest_parse_interfaces(struct gb_module *gmod)
{
	u32 count = 0;

	while (true) {
		struct manifest_desc *descriptor;
		struct greybus_descriptor_interface *desc_interface;
		struct gb_interface *interface;
		bool found = false;

		/* Find an interface descriptor */
		list_for_each_entry(descriptor, &manifest_descs, links) {
			if (descriptor->type == GREYBUS_TYPE_INTERFACE) {
				found = true;
				break;
			}
		}
		if (!found)
			break;

		/* Found one.  Set up its interface structure*/
		desc_interface = descriptor->data;
		interface = gb_interface_create(gmod, desc_interface->id);
		if (!interface)
			return 0;	/* Error */

		/* Now go set up this interface's functions and cports */
		if (!gb_manifest_parse_cports(interface))
			return 0;	/* Error parsing cports */

		count++;

		/* Done with this interface descriptor */
		release_manifest_descriptor(descriptor);
	}

	return count;
}

static bool gb_manifest_parse_module(struct gb_module *gmod,
					struct manifest_desc *module_desc)
{
	struct greybus_descriptor *desc = module_desc->data;
	struct greybus_descriptor_module *desc_module = &desc->module;

	/* Handle the strings first--they can fail */
	gmod->vendor_string = gb_string_get(desc_module->vendor_stringid);
	if (IS_ERR(gmod->vendor_string))
		return false;

	gmod->product_string = gb_string_get(desc_module->product_stringid);
	if (IS_ERR(gmod->product_string)) {
		goto out_err;
	}

	gmod->vendor = le16_to_cpu(desc_module->vendor);
	gmod->product = le16_to_cpu(desc_module->product);
	gmod->version = le16_to_cpu(desc_module->version);
	gmod->unique_id = le64_to_cpu(desc_module->unique_id);

	/* Release the module descriptor, now that we're done with it */
	release_manifest_descriptor(module_desc);

	/* A module must have at least one interface descriptor */
	if (!gb_manifest_parse_interfaces(gmod)) {
		pr_err("manifest interface descriptors not valid\n");
		goto out_err;
	}

	return true;
out_err:
	kfree(gmod->product_string);
	gmod->product_string = NULL;
	kfree(gmod->vendor_string);
	gmod->vendor_string = NULL;

	return false;
}

/*
 * Parse a buffer containing a module manifest.
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
 * Next we scan the descriptors, looking for a module descriptor;
 * there must be exactly one of those.  When found, we record the
 * information it contains, and then remove that descriptor (and any
 * string descriptors it refers to) from further consideration.
 *
 * After that we look for the module's interfaces--there must be at
 * least one of those.
 *
 * Returns true if parsing was successful, false otherwise.
 */
bool gb_manifest_parse(struct gb_module *gmod, void *data, size_t size)
{
	struct greybus_manifest *manifest;
	struct greybus_manifest_header *header;
	struct greybus_descriptor *desc;
	struct manifest_desc *descriptor;
	struct manifest_desc *module_desc = false;
	u16 manifest_size;
	u32 found = 0;
	bool result = false;

	/* we have to have at _least_ the manifest header */
	if (size <= sizeof(manifest->header)) {
		pr_err("short manifest (%zu)\n", size);
		return false;
	}

	/* Make sure the size is right */
	manifest = data;
	header = &manifest->header;
	manifest_size = le16_to_cpu(header->size);
	if (manifest_size != size) {
		pr_err("manifest size mismatch %zu != %hu\n",
			size, manifest_size);
		return false;
	}

	/* Validate major/minor number */
	if (header->version_major > GREYBUS_VERSION_MAJOR) {
		pr_err("manifest version too new (%hhu.%hhu > %hhu.%hhu)\n",
			header->version_major, header->version_minor,
			GREYBUS_VERSION_MAJOR, GREYBUS_VERSION_MINOR);
		return false;
	}

	/* OK, find all the descriptors */
	desc = (struct greybus_descriptor *)(header + 1);
	size -= sizeof(*header);
	while (size) {
		int desc_size;

		desc_size = identify_descriptor(desc, size);
		if (desc_size <= 0) {
			if (!desc_size)
				pr_err("zero-sized manifest descriptor\n");
			goto out;
		}
		desc = (struct greybus_descriptor *)((char *)desc + desc_size);
		size -= desc_size;
	}

	/* There must be a single module descriptor */
	list_for_each_entry(descriptor, &manifest_descs, links) {
		if (descriptor->type == GREYBUS_TYPE_MODULE)
			if (!found++)
				module_desc = descriptor;
	}
	if (found != 1) {
		pr_err("manifest must have 1 module descriptor (%u found)\n",
			found);
		goto out;
	}

	/* Parse the module manifest, starting with the module descriptor */
	result = gb_manifest_parse_module(gmod, module_desc);

	/*
	 * We really should have no remaining descriptors, but we
	 * don't know what newer format manifests might leave.
	 */
	if (!list_empty(&manifest_descs))
		pr_info("excess descriptors in module manifest\n");
out:
	release_manifest_descriptors();

	return false;
}
