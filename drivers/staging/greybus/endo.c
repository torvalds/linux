/*
 * Greybus endo code
 *
 * Copyright 2015 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"

/* Endo ID (16 bits long) Masks */
#define ENDO_ID_MASK				0xFFFF
#define ENDO_LARGE_MASK				0x1000
#define ENDO_MEDIUM_MASK			0x0400
#define ENDO_MINI_MASK				0x0100

#define ENDO_FRONT_MASK(id)			((id) >> 13)
#define ENDO_BACK_SIDE_RIBS_MASK(ribs)		((1 << (ribs)) - 1)

/*
 * endo_is_medium() should be used only if endo isn't large. And endo_is_mini()
 * should be used only if endo isn't large or medium.
 */
#define endo_is_large(id)			((id) & ENDO_LARGE_MASK)
#define endo_is_medium(id)			((id) & ENDO_MEDIUM_MASK)
#define endo_is_mini(id)			((id) & ENDO_MINI_MASK)

#define endo_back_left_ribs(id, ribs)		(((id) >> (ribs)) & ENDO_BACK_SIDE_RIBS_MASK(ribs))
#define endo_back_right_ribs(id, ribs)		((id) & ENDO_BACK_SIDE_RIBS_MASK(ribs))

/*
 * An Endo has interface block positions on the front and back.
 * Each has numeric ID, starting with 1 (interface 0 represents
 * the SVC within the Endo itself).  The maximum interface ID is the
 * also the number of non-SVC interfaces possible on the endo.
 *
 * Total number of interfaces:
 * - Front: 4
 * - Back left: max_ribs + 1
 * - Back right: max_ribs + 1
 */
#define max_endo_interface_id(endo_layout) \
		(4 + ((endo_layout)->max_ribs + 1) * 2)

/* endo sysfs attributes */
static ssize_t svc_serial_number_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct gb_endo *endo = to_gb_endo(dev);

	return sprintf(buf, "%s", &endo->svc_info.serial_number[0]);
}
static DEVICE_ATTR_RO(svc_serial_number);

static ssize_t svc_version_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct gb_endo *endo = to_gb_endo(dev);

	return sprintf(buf, "%s", &endo->svc_info.version[0]);
}
static DEVICE_ATTR_RO(svc_version);

static struct attribute *svc_attrs[] = {
	&dev_attr_svc_serial_number.attr,
	&dev_attr_svc_version.attr,
	NULL,
};

static const struct attribute_group svc_group = {
	.attrs = svc_attrs,
	.name = "SVC",
};

static ssize_t endo_id_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct gb_endo *endo = to_gb_endo(dev);

	return sprintf(buf, "0x%04x", endo->id);
}
static DEVICE_ATTR_RO(endo_id);

static struct attribute *endo_attrs[] = {
	&dev_attr_endo_id.attr,
	NULL,
};

static const struct attribute_group endo_group = {
	.attrs = endo_attrs,
	.name = "Endo",
};

static const struct attribute_group *endo_groups[] = {
	&endo_group,
	&svc_group,
	NULL,
};

static void gb_endo_release(struct device *dev)
{
	struct gb_endo *endo = to_gb_endo(dev);

	kfree(endo);
}

struct device_type greybus_endo_type = {
	.name =		"greybus_endo",
	.release =	gb_endo_release,
};


/* Validate Endo ID */

/*
 * The maximum module height is 2 units.  This means any adjacent pair of bits
 * in the left or right mask must have at least one bit set.
 */
static inline bool modules_oversized(unsigned int count, unsigned int mask)
{
	int i;

	for (i = 0; i < count - 1; i++)
		if (!(mask & (0x3 << i)))
			return true;

	return false;
}

/* Reverse a number of least significant bits in a value */
static u8 reverse_bits(unsigned int value, unsigned int bits)
{
	u8 result = 0;
	u8 result_mask = 1 << (bits - 1);
	u8 value_mask = 1;

	while (value && result_mask) {
		if (value & value_mask) {
			result |= result_mask;
			value ^= value_mask;
		}
		value_mask <<= 1;
		result_mask >>= 1;
	}

	return result;
}

/*
 * An Endo can have at most one instance of a single rib spanning its whole
 * width.  That is, the left and right bit masks representing the rib positions
 * must have at most one bit set in both masks.
 */
static bool single_cross_rib(u8 left_ribs, u8 right_ribs)
{
	u8 span_ribs = left_ribs & right_ribs;

	/* Power of 2 ? */
	if (span_ribs & (span_ribs - 1))
		return false;
	return true;
}

/*
 * Each Endo size has its own set of front module configurations.  For most, the
 * resulting rib mask is the same regardless of the Endo size.  The mini Endo
 * has a few differences though.
 *
 * Endo front has 4 interface blocks and 3 rib positions. A maximum of 2 ribs
 * are allowed to be present for any endo type.
 *
 * This routine validates front mask and sets 'front_ribs', its 3 least
 * significant bits represent front ribs mask, other are 0.  The front values
 * should be within range (1..6).
 *
 * front_ribs bitmask:
 * - Bit 0: 1st rib location from top, i.e. between interface 1 and 2.
 * - Bit 1: 2nd rib location from top, i.e. between interface 2 and 3.
 * - Bit 2: 3rd rib location from top, i.e. between interface 3 and 4.
 */
static bool validate_front_ribs(struct greybus_host_device *hd,
				struct endo_layout *layout, bool mini,
				u16 endo_id)
{
	u8 front_mask = ENDO_FRONT_MASK(endo_id);

	/* Verify front endo mask is in valid range, i.e. 1-6 */

	switch (front_mask) {
	case 1:
		layout->front_ribs = 0x0;
		break;
	case 2:
		layout->front_ribs = 0x1;
		break;
	case 3:
		layout->front_ribs = 0x4;
		break;
	case 4:
		layout->front_ribs = mini ? 0x2 : 0x3;
		break;
	case 5:
		layout->front_ribs = mini ? 0x2 : 0x6;
		break;
	case 6:
		layout->front_ribs = 0x5;
		break;
	default:
		dev_err(hd->parent,
			"%s: Invalid endo front mask 0x%02x, id 0x%04x\n",
			__func__, front_mask, endo_id);
		return false;
	}

	return true;
}

/*
 * The rear of an endo has a single vertical "spine", and the modules placed on
 * the left and right of that spine are separated by ribs.  Only one "cross"
 * (i.e. rib that spans the entire width) is allowed of the back of the endo;
 * all other ribs reach from the spine to the left or right edge.
 *
 * The width of the module positions on the left and right of the spine are
 * determined by the width of the endo (either 1 or 2 "units").  The height of
 * the modules is determined by the placement of the ribs (a module can be
 * either 1 or 2 units high).
 *
 * The lower 13 bits of the 16-bit endo id are used to encode back ribs
 * information.  The large form factor endo uses all of these bits; the medium
 * and mini form factors leave some bits unused (such bits shall be ignored, and
 * are 0 for the purposes of this endo id definition).
 *
 * Each defined bit represents a rib position on one or the other side
 * of the spine on the back of an endo.  If that bit is set (1), it
 * means a rib is present in the corresponding location; otherwise
 * there is no rib there.
 *
 * Rotating an endo 180 degrees does not produce a new rib configuration. A
 * single endo id represents a specific configuration of ribs without regard to
 * its rotational orientation.  We define one canonical id to represent a
 * particular endo configuration.
 */
static bool validate_back_ribs(struct greybus_host_device *hd,
			       struct endo_layout *layout, u16 endo_id)
{
	u8 max_ribs = layout->max_ribs;
	u8 left_ribs;
	u8 right_ribs;

	/* Extract the left and right rib masks */
	left_ribs = endo_back_left_ribs(endo_id, max_ribs);
	right_ribs = endo_back_right_ribs(endo_id, max_ribs);

	if (!single_cross_rib(left_ribs, right_ribs)) {
		dev_err(hd->parent,
			"%s: More than one spanning rib (left 0x%02x right 0x%02x), id 0x%04x\n",
			__func__, left_ribs, right_ribs, endo_id);
		return false;
	}

	if (modules_oversized(max_ribs, left_ribs)) {
			dev_err(hd->parent,
				"%s: Oversized module (left) 0x%02x, id 0x%04x\n",
				__func__, left_ribs, endo_id);
			return false;
	}

	if (modules_oversized(max_ribs, right_ribs)) {
			dev_err(hd->parent,
				"%s: Oversized module (Right) 0x%02x, id 0x%04x\n",
				__func__, right_ribs, endo_id);
			return false;
	}

	/*
	 * The Endo numbering scheme represents the left and right rib
	 * configuration in a way that's convenient for looking for multiple
	 * spanning ribs.  But it doesn't match the normal Endo interface
	 * numbering scheme (increasing counter-clockwise around the back).
	 * Reverse the right bit positions so they do match.
	 */
	right_ribs = reverse_bits(right_ribs, max_ribs);

	/*
	 * A mini or large Endo rotated 180 degrees is still the same Endo.  In
	 * most cases that allows two distinct values to represent the same
	 * Endo; we choose one of them to be the canonical one (and the other is
	 * invalid).  The canonical one is identified by higher value of left
	 * ribs mask.
	 *
	 * This doesn't apply to medium Endos, because the left and right sides
	 * are of different widths.
	 */
	if (max_ribs != ENDO_BACK_RIBS_MEDIUM && left_ribs < right_ribs) {
		dev_err(hd->parent, "%s: Non-canonical endo id 0x%04x\n", __func__,
			endo_id);
		return false;
	}

	layout->left_ribs = left_ribs;
	layout->right_ribs = right_ribs;
	return true;
}

/*
 * Validate the endo-id passed from SVC. Error out if its not a valid Endo,
 * else return structure representing ribs positions on front and back of Endo.
 */
static int gb_endo_validate_id(struct greybus_host_device *hd,
			       struct endo_layout *layout, u16 endo_id)
{
	/* Validate Endo Size */
	if (endo_is_large(endo_id)) {
		/* Large Endo type */
		layout->max_ribs = ENDO_BACK_RIBS_LARGE;
	} else if (endo_is_medium(endo_id)) {
		/* Medium Endo type */
		layout->max_ribs = ENDO_BACK_RIBS_MEDIUM;
	} else if (endo_is_mini(endo_id)) {
		/* Mini Endo type */
		layout->max_ribs = ENDO_BACK_RIBS_MINI;
	} else {
		dev_err(hd->parent, "%s: Invalid endo type, id 0x%04x\n",
			__func__, endo_id);
		return -EINVAL;
	}

	if (!validate_back_ribs(hd, layout, endo_id))
		return -EINVAL;

	if (!validate_front_ribs(hd, layout,
				 layout->max_ribs == ENDO_BACK_RIBS_MINI,
				 endo_id))
		return -EINVAL;

	return 0;
}

/*
 * Look up which module contains the given interface.
 *
 * A module's ID is the same as its lowest-numbered interface ID. So the module
 * ID for a 1x1 module is always the same as its interface ID.
 *
 * For Endo Back:
 * The module ID for an interface on a 1x2 or 2x2 module (which use two
 * interface blocks) can be either the interface ID, or one less than the
 * interface ID if there is no rib "above" the interface.
 *
 * For Endo Front:
 * There are three rib locations in front and all of them might be unused, i.e.
 * a single module is used for all 4 interfaces. We need to check all ribs in
 * that case to find module ID.
 */
u8 endo_get_module_id(struct gb_endo *endo, u8 interface_id)
{
	struct endo_layout *layout = &endo->layout;
	unsigned int height = layout->max_ribs + 1;
	unsigned int iid = interface_id - 1;
	unsigned int mask, rib_mask;

	if (!interface_id)
		return 0;

	if (iid < height) {			/* back left */
		mask = layout->left_ribs;
	} else if (iid < 2 * height) {	/* back right */
		mask = layout->right_ribs;
		iid -= height;
	} else {					/* front */
		mask = layout->front_ribs;
		iid -= 2 * height;
	}

	/*
	 * Find the next rib *above* this interface to determine the lowest
	 * interface ID in the module.
	 */
	rib_mask = 1 << iid;
	while ((rib_mask >>= 1) != 0 && !(mask & rib_mask))
		--interface_id;

	return interface_id;
}

/*
 * Creates all possible modules for the Endo.
 *
 * We try to create modules for all possible interface IDs. If a module is
 * already created, we skip creating it again with the help of prev_module_id.
 */
static int create_modules(struct gb_endo *endo)
{
	struct gb_module *module;
	int prev_module_id = 0;
	int interface_id;
	int module_id;
	int max_id;

	max_id = max_endo_interface_id(&endo->layout);

	/* Find module corresponding to each interface */
	for (interface_id = 1; interface_id <= max_id; interface_id++) {
		module_id = endo_get_module_id(endo, interface_id);

		if (WARN_ON(!module_id))
			continue;

		/* Skip already created modules */
		if (module_id == prev_module_id)
			continue;

		prev_module_id = module_id;

		/* New module, create it */
		module = gb_module_create(&endo->dev, module_id);
		if (!module)
			return -EINVAL;
	}

	return 0;
}

static int gb_endo_register(struct greybus_host_device *hd,
			    struct gb_endo *endo)
{
	int retval;

	endo->dev.parent = hd->parent;
	endo->dev.bus = &greybus_bus_type;
	endo->dev.type = &greybus_endo_type;
	endo->dev.groups = endo_groups;
	endo->dev.dma_mask = hd->parent->dma_mask;
	device_initialize(&endo->dev);

	// FIXME
	// Get the version and serial number from the SVC, right now we are
	// using "fake" numbers.
	strcpy(&endo->svc_info.serial_number[0], "042");
	strcpy(&endo->svc_info.version[0], "0.0");

	dev_set_name(&endo->dev, "endo-0x%04x", endo->id);
	retval = device_add(&endo->dev);
	if (retval) {
		dev_err(hd->parent, "failed to add endo device of id 0x%04x\n",
			endo->id);
		put_device(&endo->dev);
	}

	return retval;
}

struct gb_endo *gb_endo_create(struct greybus_host_device *hd, u16 endo_id)
{
	struct gb_endo *endo;
	int retval;

	endo = kzalloc(sizeof(*endo), GFP_KERNEL);
	if (!endo)
		return ERR_PTR(-ENOMEM);

	/* First check if the value supplied is a valid endo id */
	if (gb_endo_validate_id(hd, &endo->layout, endo_id)) {
		retval = -EINVAL;
		goto free_endo;
	}

	endo->id = endo_id;

	/* Register Endo device */
	retval = gb_endo_register(hd, endo);
	if (retval)
		goto free_endo;

	/* Create modules/interfaces */
	retval = create_modules(endo);
	if (retval) {
		gb_endo_remove(endo);
		return NULL;
	}

	return endo;

free_endo:
	kfree(endo);

	return ERR_PTR(retval);
}

void gb_endo_remove(struct gb_endo *endo)
{
	if (!endo)
		return;

	/* remove all modules for this endo */
	gb_module_remove_all(endo);

	device_unregister(&endo->dev);
}

