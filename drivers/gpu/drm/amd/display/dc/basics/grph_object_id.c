/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dm_services.h"
#include "include/grph_object_id.h"

bool dal_graphics_object_id_is_valid(struct graphics_object_id id)
{
	bool rc = true;

	switch (id.type) {
	case OBJECT_TYPE_UNKNOWN:
		rc = false;
		break;
	case OBJECT_TYPE_GPU:
	case OBJECT_TYPE_ENGINE:
		/* do NOT check for id.id == 0 */
		if (id.enum_id == ENUM_ID_UNKNOWN)
			rc = false;
		break;
	default:
		if (id.id == 0 || id.enum_id == ENUM_ID_UNKNOWN)
			rc = false;
		break;
	}

	return rc;
}

bool dal_graphics_object_id_is_equal(
	struct graphics_object_id id1,
	struct graphics_object_id id2)
{
	if (false == dal_graphics_object_id_is_valid(id1)) {
		dm_output_to_console(
		"%s: Warning: comparing invalid object 'id1'!\n", __func__);
		return false;
	}

	if (false == dal_graphics_object_id_is_valid(id2)) {
		dm_output_to_console(
		"%s: Warning: comparing invalid object 'id2'!\n", __func__);
		return false;
	}

	if (id1.id == id2.id && id1.enum_id == id2.enum_id
		&& id1.type == id2.type)
		return true;

	return false;
}

/* Based on internal data members memory layout */
uint32_t dal_graphics_object_id_to_uint(struct graphics_object_id id)
{
	uint32_t object_id = 0;

	object_id = id.id + (id.enum_id << 0x8) + (id.type << 0xc);
	return object_id;
}

/*
 * ******* get specific ID - internal safe cast into specific type *******
 */

enum controller_id dal_graphics_object_id_get_controller_id(
	struct graphics_object_id id)
{
	if (id.type == OBJECT_TYPE_CONTROLLER)
		return id.id;
	return CONTROLLER_ID_UNDEFINED;
}

enum clock_source_id dal_graphics_object_id_get_clock_source_id(
	struct graphics_object_id id)
{
	if (id.type == OBJECT_TYPE_CLOCK_SOURCE)
		return id.id;
	return CLOCK_SOURCE_ID_UNDEFINED;
}

enum encoder_id dal_graphics_object_id_get_encoder_id(
	struct graphics_object_id id)
{
	if (id.type == OBJECT_TYPE_ENCODER)
		return id.id;
	return ENCODER_ID_UNKNOWN;
}

enum connector_id dal_graphics_object_id_get_connector_id(
	struct graphics_object_id id)
{
	if (id.type == OBJECT_TYPE_CONNECTOR)
		return id.id;
	return CONNECTOR_ID_UNKNOWN;
}

enum audio_id dal_graphics_object_id_get_audio_id(struct graphics_object_id id)
{
	if (id.type == OBJECT_TYPE_AUDIO)
		return id.id;
	return AUDIO_ID_UNKNOWN;
}

enum engine_id dal_graphics_object_id_get_engine_id(
	struct graphics_object_id id)
{
	if (id.type == OBJECT_TYPE_ENGINE)
		return id.id;
	return ENGINE_ID_UNKNOWN;
}

