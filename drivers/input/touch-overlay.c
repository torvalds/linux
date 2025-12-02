// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Helper functions for overlay objects on touchscreens
 *
 *  Copyright (c) 2023 Javier Carrasco <javier.carrasco@wolfvision.net>
 */

#include <linux/export.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touch-overlay.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/property.h>

struct touch_overlay_segment {
	struct list_head list;
	u32 x_origin;
	u32 y_origin;
	u32 x_size;
	u32 y_size;
	u32 key;
	bool pressed;
	int slot;
};

static int touch_overlay_get_segment(struct fwnode_handle *segment_node,
				     struct touch_overlay_segment *segment,
				     struct input_dev *input)
{
	int error;

	error = fwnode_property_read_u32(segment_node, "x-origin",
					 &segment->x_origin);
	if (error)
		return error;

	error = fwnode_property_read_u32(segment_node, "y-origin",
					 &segment->y_origin);
	if (error)
		return error;

	error = fwnode_property_read_u32(segment_node, "x-size",
					 &segment->x_size);
	if (error)
		return error;

	error = fwnode_property_read_u32(segment_node, "y-size",
					 &segment->y_size);
	if (error)
		return error;

	error = fwnode_property_read_u32(segment_node, "linux,code",
					 &segment->key);
	if (!error)
		input_set_capability(input, EV_KEY, segment->key);
	else if (error != -EINVAL)
		return error;

	return 0;
}

/**
 * touch_overlay_map - map overlay objects from the device tree and set
 * key capabilities if buttons are defined.
 * @list: pointer to the list that will hold the segments
 * @input: pointer to the already allocated input_dev
 *
 * Returns 0 on success and error number otherwise.
 *
 * If buttons are defined, key capabilities are set accordingly.
 */
int touch_overlay_map(struct list_head *list, struct input_dev *input)
{
	struct fwnode_handle *fw_segment;
	struct device *dev = input->dev.parent;
	struct touch_overlay_segment *segment;
	int error;

	struct fwnode_handle *overlay __free(fwnode_handle) =
		device_get_named_child_node(dev, "touch-overlay");
	if (!overlay)
		return 0;

	fwnode_for_each_available_child_node(overlay, fw_segment) {
		segment = devm_kzalloc(dev, sizeof(*segment), GFP_KERNEL);
		if (!segment) {
			fwnode_handle_put(fw_segment);
			return -ENOMEM;
		}
		error = touch_overlay_get_segment(fw_segment, segment, input);
		if (error) {
			fwnode_handle_put(fw_segment);
			return error;
		}
		list_add_tail(&segment->list, list);
	}

	return 0;
}
EXPORT_SYMBOL(touch_overlay_map);

/**
 * touch_overlay_get_touchscreen_abs - get abs size from the touchscreen area.
 * @list: pointer to the list that holds the segments
 * @x: horizontal abs
 * @y: vertical abs
 */
void touch_overlay_get_touchscreen_abs(struct list_head *list, u16 *x, u16 *y)
{
	struct touch_overlay_segment *segment;
	struct list_head *ptr;

	list_for_each(ptr, list) {
		segment = list_entry(ptr, struct touch_overlay_segment, list);
		if (!segment->key) {
			*x = segment->x_size - 1;
			*y = segment->y_size - 1;
			break;
		}
	}
}
EXPORT_SYMBOL(touch_overlay_get_touchscreen_abs);

static bool touch_overlay_segment_event(struct touch_overlay_segment *seg,
					struct input_mt_pos *pos)
{
	if (pos->x >= seg->x_origin && pos->x < (seg->x_origin + seg->x_size) &&
	    pos->y >= seg->y_origin && pos->y < (seg->y_origin + seg->y_size))
		return true;

	return false;
}

/**
 * touch_overlay_mapped_touchscreen - check if a touchscreen area is mapped
 * @list: pointer to the list that holds the segments
 *
 * Returns true if a touchscreen area is mapped or false otherwise.
 */
bool touch_overlay_mapped_touchscreen(struct list_head *list)
{
	struct touch_overlay_segment *segment;
	struct list_head *ptr;

	list_for_each(ptr, list) {
		segment = list_entry(ptr, struct touch_overlay_segment, list);
		if (!segment->key)
			return true;
	}

	return false;
}
EXPORT_SYMBOL(touch_overlay_mapped_touchscreen);

static bool touch_overlay_event_on_ts(struct list_head *list,
				      struct input_mt_pos *pos)
{
	struct touch_overlay_segment *segment;
	struct list_head *ptr;

	list_for_each(ptr, list) {
		segment = list_entry(ptr, struct touch_overlay_segment, list);
		if (segment->key)
			continue;

		if (touch_overlay_segment_event(segment, pos)) {
			pos->x -= segment->x_origin;
			pos->y -= segment->y_origin;
			return true;
		}
		/* ignore touch events outside the defined area */
		return false;
	}

	return true;
}

static bool touch_overlay_button_event(struct input_dev *input,
				       struct touch_overlay_segment *segment,
				       struct input_mt_pos *pos, int slot)
{
	struct input_mt *mt = input->mt;
	struct input_mt_slot *s = &mt->slots[slot];
	bool button_contact = touch_overlay_segment_event(segment, pos);

	if (segment->slot == slot && segment->pressed) {
		/* sliding out of the button releases it */
		if (!button_contact) {
			input_report_key(input, segment->key, false);
			segment->pressed = false;
			/* keep available for a possible touch event */
			return false;
		}
		/* ignore sliding on the button while pressed */
		s->frame = mt->frame;
		return true;
	} else if (button_contact) {
		input_report_key(input, segment->key, true);
		s->frame = mt->frame;
		segment->slot = slot;
		segment->pressed = true;
		return true;
	}

	return false;
}

/**
 * touch_overlay_sync_frame - update the status of the segments and report
 * buttons whose tracked slot is unused.
 * @list: pointer to the list that holds the segments
 * @input: pointer to the input device associated to the contact
 */
void touch_overlay_sync_frame(struct list_head *list, struct input_dev *input)
{
	struct touch_overlay_segment *segment;
	struct input_mt *mt = input->mt;
	struct input_mt_slot *s;
	struct list_head *ptr;

	list_for_each(ptr, list) {
		segment = list_entry(ptr, struct touch_overlay_segment, list);
		if (!segment->key)
			continue;

		s = &mt->slots[segment->slot];
		if (!input_mt_is_used(mt, s) && segment->pressed) {
			input_report_key(input, segment->key, false);
			segment->pressed = false;
		}
	}
}
EXPORT_SYMBOL(touch_overlay_sync_frame);

/**
 * touch_overlay_process_contact - process contacts according to the overlay
 * mapping. This function acts as a filter to release the calling driver
 * from the contacts that are either related to overlay buttons or out of the
 * overlay touchscreen area, if defined.
 * @list: pointer to the list that holds the segments
 * @input: pointer to the input device associated to the contact
 * @pos: pointer to the contact position
 * @slot: slot associated to the contact (0 if multitouch is not supported)
 *
 * Returns true if the contact was processed (reported for valid key events
 * and dropped for contacts outside the overlay touchscreen area) or false
 * if the contact must be processed by the caller. In that case this function
 * shifts the (x,y) coordinates to the overlay touchscreen axis if required.
 */
bool touch_overlay_process_contact(struct list_head *list,
				   struct input_dev *input,
				   struct input_mt_pos *pos, int slot)
{
	struct touch_overlay_segment *segment;
	struct list_head *ptr;

	/*
	 * buttons must be prioritized over overlay touchscreens to account for
	 * overlappings e.g. a button inside the touchscreen area.
	 */
	list_for_each(ptr, list) {
		segment = list_entry(ptr, struct touch_overlay_segment, list);
		if (segment->key &&
		    touch_overlay_button_event(input, segment, pos, slot))
			return true;
	}

	/*
	 * valid contacts on the overlay touchscreen are left for the client
	 * to be processed/reported according to its (possibly) unique features.
	 */
	return !touch_overlay_event_on_ts(list, pos);
}
EXPORT_SYMBOL(touch_overlay_process_contact);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Helper functions for overlay objects on touch devices");
