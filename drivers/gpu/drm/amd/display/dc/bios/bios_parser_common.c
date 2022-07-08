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

#include "bios_parser_common.h"
#include "include/grph_object_ctrl_defs.h"

static enum object_type object_type_from_bios_object_id(uint32_t bios_object_id)
{
	uint32_t bios_object_type = (bios_object_id & OBJECT_TYPE_MASK)
				>> OBJECT_TYPE_SHIFT;
	enum object_type object_type;

	switch (bios_object_type) {
	case GRAPH_OBJECT_TYPE_GPU:
		object_type = OBJECT_TYPE_GPU;
		break;
	case GRAPH_OBJECT_TYPE_ENCODER:
		object_type = OBJECT_TYPE_ENCODER;
		break;
	case GRAPH_OBJECT_TYPE_CONNECTOR:
		object_type = OBJECT_TYPE_CONNECTOR;
		break;
	case GRAPH_OBJECT_TYPE_ROUTER:
		object_type = OBJECT_TYPE_ROUTER;
		break;
	case GRAPH_OBJECT_TYPE_GENERIC:
		object_type = OBJECT_TYPE_GENERIC;
		break;
	default:
		object_type = OBJECT_TYPE_UNKNOWN;
		break;
	}

	return object_type;
}

static enum object_enum_id enum_id_from_bios_object_id(uint32_t bios_object_id)
{
	uint32_t bios_enum_id =
			(bios_object_id & ENUM_ID_MASK) >> ENUM_ID_SHIFT;
	enum object_enum_id id;

	switch (bios_enum_id) {
	case GRAPH_OBJECT_ENUM_ID1:
		id = ENUM_ID_1;
		break;
	case GRAPH_OBJECT_ENUM_ID2:
		id = ENUM_ID_2;
		break;
	case GRAPH_OBJECT_ENUM_ID3:
		id = ENUM_ID_3;
		break;
	case GRAPH_OBJECT_ENUM_ID4:
		id = ENUM_ID_4;
		break;
	case GRAPH_OBJECT_ENUM_ID5:
		id = ENUM_ID_5;
		break;
	case GRAPH_OBJECT_ENUM_ID6:
		id = ENUM_ID_6;
		break;
	case GRAPH_OBJECT_ENUM_ID7:
		id = ENUM_ID_7;
		break;
	default:
		id = ENUM_ID_UNKNOWN;
		break;
	}

	return id;
}

static uint32_t gpu_id_from_bios_object_id(uint32_t bios_object_id)
{
	return (bios_object_id & OBJECT_ID_MASK) >> OBJECT_ID_SHIFT;
}

static enum encoder_id encoder_id_from_bios_object_id(uint32_t bios_object_id)
{
	uint32_t bios_encoder_id = gpu_id_from_bios_object_id(bios_object_id);
	enum encoder_id id;

	switch (bios_encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_LVDS:
		id = ENCODER_ID_INTERNAL_LVDS;
		break;
	case ENCODER_OBJECT_ID_INTERNAL_TMDS1:
		id = ENCODER_ID_INTERNAL_TMDS1;
		break;
	case ENCODER_OBJECT_ID_INTERNAL_TMDS2:
		id = ENCODER_ID_INTERNAL_TMDS2;
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DAC1:
		id = ENCODER_ID_INTERNAL_DAC1;
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DAC2:
		id = ENCODER_ID_INTERNAL_DAC2;
		break;
	case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
		id = ENCODER_ID_INTERNAL_LVTM1;
		break;
	case ENCODER_OBJECT_ID_HDMI_INTERNAL:
		id = ENCODER_ID_INTERNAL_HDMI;
		break;
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
		id = ENCODER_ID_INTERNAL_KLDSCP_TMDS1;
		break;
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1:
		id = ENCODER_ID_INTERNAL_KLDSCP_DAC1;
		break;
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2:
		id = ENCODER_ID_INTERNAL_KLDSCP_DAC2;
		break;
	case ENCODER_OBJECT_ID_MVPU_FPGA:
		id = ENCODER_ID_EXTERNAL_MVPU_FPGA;
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DDI:
		id = ENCODER_ID_INTERNAL_DDI;
		break;
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
		id = ENCODER_ID_INTERNAL_UNIPHY;
		break;
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
		id = ENCODER_ID_INTERNAL_KLDSCP_LVTMA;
		break;
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
		id = ENCODER_ID_INTERNAL_UNIPHY1;
		break;
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
		id = ENCODER_ID_INTERNAL_UNIPHY2;
		break;
	case ENCODER_OBJECT_ID_ALMOND: /* ENCODER_OBJECT_ID_NUTMEG */
		id = ENCODER_ID_EXTERNAL_NUTMEG;
		break;
	case ENCODER_OBJECT_ID_TRAVIS:
		id = ENCODER_ID_EXTERNAL_TRAVIS;
		break;
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY3:
		id = ENCODER_ID_INTERNAL_UNIPHY3;
		break;
	default:
		id = ENCODER_ID_UNKNOWN;
		ASSERT(0);
		break;
	}

	return id;
}

static enum connector_id connector_id_from_bios_object_id(
	uint32_t bios_object_id)
{
	uint32_t bios_connector_id = gpu_id_from_bios_object_id(bios_object_id);

	enum connector_id id;

	switch (bios_connector_id) {
	case CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_I:
		id = CONNECTOR_ID_SINGLE_LINK_DVII;
		break;
	case CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_I:
		id = CONNECTOR_ID_DUAL_LINK_DVII;
		break;
	case CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_D:
		id = CONNECTOR_ID_SINGLE_LINK_DVID;
		break;
	case CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_D:
		id = CONNECTOR_ID_DUAL_LINK_DVID;
		break;
	case CONNECTOR_OBJECT_ID_VGA:
		id = CONNECTOR_ID_VGA;
		break;
	case CONNECTOR_OBJECT_ID_HDMI_TYPE_A:
		id = CONNECTOR_ID_HDMI_TYPE_A;
		break;
	case CONNECTOR_OBJECT_ID_LVDS:
		id = CONNECTOR_ID_LVDS;
		break;
	case CONNECTOR_OBJECT_ID_PCIE_CONNECTOR:
		id = CONNECTOR_ID_PCIE;
		break;
	case CONNECTOR_OBJECT_ID_HARDCODE_DVI:
		id = CONNECTOR_ID_HARDCODE_DVI;
		break;
	case CONNECTOR_OBJECT_ID_DISPLAYPORT:
		id = CONNECTOR_ID_DISPLAY_PORT;
		break;
	case CONNECTOR_OBJECT_ID_eDP:
		id = CONNECTOR_ID_EDP;
		break;
	case CONNECTOR_OBJECT_ID_MXM:
		id = CONNECTOR_ID_MXM;
		break;
	case CONNECTOR_OBJECT_ID_USBC:
		id = CONNECTOR_ID_USBC;
		break;
	default:
		id = CONNECTOR_ID_UNKNOWN;
		break;
	}

	return id;
}

static enum generic_id generic_id_from_bios_object_id(uint32_t bios_object_id)
{
	uint32_t bios_generic_id = gpu_id_from_bios_object_id(bios_object_id);

	enum generic_id id;

	switch (bios_generic_id) {
	case GENERIC_OBJECT_ID_MXM_OPM:
		id = GENERIC_ID_MXM_OPM;
		break;
	case GENERIC_OBJECT_ID_GLSYNC:
		id = GENERIC_ID_GLSYNC;
		break;
	case GENERIC_OBJECT_ID_STEREO_PIN:
		id = GENERIC_ID_STEREO;
		break;
	default:
		id = GENERIC_ID_UNKNOWN;
		break;
	}

	return id;
}

static uint32_t id_from_bios_object_id(enum object_type type,
	uint32_t bios_object_id)
{
	switch (type) {
	case OBJECT_TYPE_GPU:
		return gpu_id_from_bios_object_id(bios_object_id);
	case OBJECT_TYPE_ENCODER:
		return (uint32_t)encoder_id_from_bios_object_id(bios_object_id);
	case OBJECT_TYPE_CONNECTOR:
		return (uint32_t)connector_id_from_bios_object_id(
				bios_object_id);
	case OBJECT_TYPE_GENERIC:
		return generic_id_from_bios_object_id(bios_object_id);
	default:
		return 0;
	}
}

struct graphics_object_id object_id_from_bios_object_id(uint32_t bios_object_id)
{
	enum object_type type;
	enum object_enum_id enum_id;
	struct graphics_object_id go_id = { 0 };

	type = object_type_from_bios_object_id(bios_object_id);

	if (OBJECT_TYPE_UNKNOWN == type)
		return go_id;

	enum_id = enum_id_from_bios_object_id(bios_object_id);

	if (ENUM_ID_UNKNOWN == enum_id)
		return go_id;

	go_id = dal_graphics_object_id_init(
			id_from_bios_object_id(type, bios_object_id), enum_id, type);

	return go_id;
}


