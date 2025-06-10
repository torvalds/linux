// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This driver implements the WMI AB device found on TUXEDO notebooks with board
 * vendor NB04.
 *
 * Copyright (C) 2024-2025 Werner Sembach <wse@tuxedocomputers.com>
 */

#include <linux/dmi.h>
#include <linux/hid.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/wmi.h>

#include "wmi_util.h"

static const struct wmi_device_id tuxedo_nb04_wmi_ab_device_ids[] = {
	{ .guid_string = "80C9BAA6-AC48-4538-9234-9F81A55E7C85" },
	{ }
};
MODULE_DEVICE_TABLE(wmi, tuxedo_nb04_wmi_ab_device_ids);

enum {
	LAMP_ARRAY_ATTRIBUTES_REPORT_ID		= 0x01,
	LAMP_ATTRIBUTES_REQUEST_REPORT_ID	= 0x02,
	LAMP_ATTRIBUTES_RESPONSE_REPORT_ID	= 0x03,
	LAMP_MULTI_UPDATE_REPORT_ID		= 0x04,
	LAMP_RANGE_UPDATE_REPORT_ID		= 0x05,
	LAMP_ARRAY_CONTROL_REPORT_ID		= 0x06,
};

static u8 tux_report_descriptor[327] = {
	0x05, 0x59,			// Usage Page (Lighting and Illumination)
	0x09, 0x01,			// Usage (Lamp Array)
	0xa1, 0x01,			// Collection (Application)
	0x85, LAMP_ARRAY_ATTRIBUTES_REPORT_ID, //  Report ID (1)
	0x09, 0x02,			//  Usage (Lamp Array Attributes Report)
	0xa1, 0x02,			//  Collection (Logical)
	0x09, 0x03,			//   Usage (Lamp Count)
	0x15, 0x00,			//   Logical Minimum (0)
	0x27, 0xff, 0xff, 0x00, 0x00,	//   Logical Maximum (65535)
	0x75, 0x10,			//   Report Size (16)
	0x95, 0x01,			//   Report Count (1)
	0xb1, 0x03,			//   Feature (Cnst,Var,Abs)
	0x09, 0x04,			//   Usage (Bounding Box Width In Micrometers)
	0x09, 0x05,			//   Usage (Bounding Box Height In Micrometers)
	0x09, 0x06,			//   Usage (Bounding Box Depth In Micrometers)
	0x09, 0x07,			//   Usage (Lamp Array Kind)
	0x09, 0x08,			//   Usage (Min Update Interval In Microseconds)
	0x15, 0x00,			//   Logical Minimum (0)
	0x27, 0xff, 0xff, 0xff, 0x7f,	//   Logical Maximum (2147483647)
	0x75, 0x20,			//   Report Size (32)
	0x95, 0x05,			//   Report Count (5)
	0xb1, 0x03,			//   Feature (Cnst,Var,Abs)
	0xc0,				//  End Collection
	0x85, LAMP_ATTRIBUTES_REQUEST_REPORT_ID, //  Report ID (2)
	0x09, 0x20,			//  Usage (Lamp Attributes Request Report)
	0xa1, 0x02,			//  Collection (Logical)
	0x09, 0x21,			//   Usage (Lamp Id)
	0x15, 0x00,			//   Logical Minimum (0)
	0x27, 0xff, 0xff, 0x00, 0x00,	//   Logical Maximum (65535)
	0x75, 0x10,			//   Report Size (16)
	0x95, 0x01,			//   Report Count (1)
	0xb1, 0x02,			//   Feature (Data,Var,Abs)
	0xc0,				//  End Collection
	0x85, LAMP_ATTRIBUTES_RESPONSE_REPORT_ID, //  Report ID (3)
	0x09, 0x22,			//  Usage (Lamp Attributes Response Report)
	0xa1, 0x02,			//  Collection (Logical)
	0x09, 0x21,			//   Usage (Lamp Id)
	0x15, 0x00,			//   Logical Minimum (0)
	0x27, 0xff, 0xff, 0x00, 0x00,	//   Logical Maximum (65535)
	0x75, 0x10,			//   Report Size (16)
	0x95, 0x01,			//   Report Count (1)
	0xb1, 0x02,			//   Feature (Data,Var,Abs)
	0x09, 0x23,			//   Usage (Position X In Micrometers)
	0x09, 0x24,			//   Usage (Position Y In Micrometers)
	0x09, 0x25,			//   Usage (Position Z In Micrometers)
	0x09, 0x27,			//   Usage (Update Latency In Microseconds)
	0x09, 0x26,			//   Usage (Lamp Purposes)
	0x15, 0x00,			//   Logical Minimum (0)
	0x27, 0xff, 0xff, 0xff, 0x7f,	//   Logical Maximum (2147483647)
	0x75, 0x20,			//   Report Size (32)
	0x95, 0x05,			//   Report Count (5)
	0xb1, 0x02,			//   Feature (Data,Var,Abs)
	0x09, 0x28,			//   Usage (Red Level Count)
	0x09, 0x29,			//   Usage (Green Level Count)
	0x09, 0x2a,			//   Usage (Blue Level Count)
	0x09, 0x2b,			//   Usage (Intensity Level Count)
	0x09, 0x2c,			//   Usage (Is Programmable)
	0x09, 0x2d,			//   Usage (Input Binding)
	0x15, 0x00,			//   Logical Minimum (0)
	0x26, 0xff, 0x00,		//   Logical Maximum (255)
	0x75, 0x08,			//   Report Size (8)
	0x95, 0x06,			//   Report Count (6)
	0xb1, 0x02,			//   Feature (Data,Var,Abs)
	0xc0,				//  End Collection
	0x85, LAMP_MULTI_UPDATE_REPORT_ID, //  Report ID (4)
	0x09, 0x50,			//  Usage (Lamp Multi Update Report)
	0xa1, 0x02,			//  Collection (Logical)
	0x09, 0x03,			//   Usage (Lamp Count)
	0x09, 0x55,			//   Usage (Lamp Update Flags)
	0x15, 0x00,			//   Logical Minimum (0)
	0x25, 0x08,			//   Logical Maximum (8)
	0x75, 0x08,			//   Report Size (8)
	0x95, 0x02,			//   Report Count (2)
	0xb1, 0x02,			//   Feature (Data,Var,Abs)
	0x09, 0x21,			//   Usage (Lamp Id)
	0x15, 0x00,			//   Logical Minimum (0)
	0x27, 0xff, 0xff, 0x00, 0x00,	//   Logical Maximum (65535)
	0x75, 0x10,			//   Report Size (16)
	0x95, 0x08,			//   Report Count (8)
	0xb1, 0x02,			//   Feature (Data,Var,Abs)
	0x09, 0x51,			//   Usage (Red Update Channel)
	0x09, 0x52,			//   Usage (Green Update Channel)
	0x09, 0x53,			//   Usage (Blue Update Channel)
	0x09, 0x54,			//   Usage (Intensity Update Channel)
	0x09, 0x51,			//   Usage (Red Update Channel)
	0x09, 0x52,			//   Usage (Green Update Channel)
	0x09, 0x53,			//   Usage (Blue Update Channel)
	0x09, 0x54,			//   Usage (Intensity Update Channel)
	0x09, 0x51,			//   Usage (Red Update Channel)
	0x09, 0x52,			//   Usage (Green Update Channel)
	0x09, 0x53,			//   Usage (Blue Update Channel)
	0x09, 0x54,			//   Usage (Intensity Update Channel)
	0x09, 0x51,			//   Usage (Red Update Channel)
	0x09, 0x52,			//   Usage (Green Update Channel)
	0x09, 0x53,			//   Usage (Blue Update Channel)
	0x09, 0x54,			//   Usage (Intensity Update Channel)
	0x09, 0x51,			//   Usage (Red Update Channel)
	0x09, 0x52,			//   Usage (Green Update Channel)
	0x09, 0x53,			//   Usage (Blue Update Channel)
	0x09, 0x54,			//   Usage (Intensity Update Channel)
	0x09, 0x51,			//   Usage (Red Update Channel)
	0x09, 0x52,			//   Usage (Green Update Channel)
	0x09, 0x53,			//   Usage (Blue Update Channel)
	0x09, 0x54,			//   Usage (Intensity Update Channel)
	0x09, 0x51,			//   Usage (Red Update Channel)
	0x09, 0x52,			//   Usage (Green Update Channel)
	0x09, 0x53,			//   Usage (Blue Update Channel)
	0x09, 0x54,			//   Usage (Intensity Update Channel)
	0x09, 0x51,			//   Usage (Red Update Channel)
	0x09, 0x52,			//   Usage (Green Update Channel)
	0x09, 0x53,			//   Usage (Blue Update Channel)
	0x09, 0x54,			//   Usage (Intensity Update Channel)
	0x15, 0x00,			//   Logical Minimum (0)
	0x26, 0xff, 0x00,		//   Logical Maximum (255)
	0x75, 0x08,			//   Report Size (8)
	0x95, 0x20,			//   Report Count (32)
	0xb1, 0x02,			//   Feature (Data,Var,Abs)
	0xc0,				//  End Collection
	0x85, LAMP_RANGE_UPDATE_REPORT_ID, //  Report ID (5)
	0x09, 0x60,			//  Usage (Lamp Range Update Report)
	0xa1, 0x02,			//  Collection (Logical)
	0x09, 0x55,			//   Usage (Lamp Update Flags)
	0x15, 0x00,			//   Logical Minimum (0)
	0x25, 0x08,			//   Logical Maximum (8)
	0x75, 0x08,			//   Report Size (8)
	0x95, 0x01,			//   Report Count (1)
	0xb1, 0x02,			//   Feature (Data,Var,Abs)
	0x09, 0x61,			//   Usage (Lamp Id Start)
	0x09, 0x62,			//   Usage (Lamp Id End)
	0x15, 0x00,			//   Logical Minimum (0)
	0x27, 0xff, 0xff, 0x00, 0x00,	//   Logical Maximum (65535)
	0x75, 0x10,			//   Report Size (16)
	0x95, 0x02,			//   Report Count (2)
	0xb1, 0x02,			//   Feature (Data,Var,Abs)
	0x09, 0x51,			//   Usage (Red Update Channel)
	0x09, 0x52,			//   Usage (Green Update Channel)
	0x09, 0x53,			//   Usage (Blue Update Channel)
	0x09, 0x54,			//   Usage (Intensity Update Channel)
	0x15, 0x00,			//   Logical Minimum (0)
	0x26, 0xff, 0x00,		//   Logical Maximum (255)
	0x75, 0x08,			//   Report Size (8)
	0x95, 0x04,			//   Report Count (4)
	0xb1, 0x02,			//   Feature (Data,Var,Abs)
	0xc0,				//  End Collection
	0x85, LAMP_ARRAY_CONTROL_REPORT_ID, //  Report ID (6)
	0x09, 0x70,			//  Usage (Lamp Array Control Report)
	0xa1, 0x02,			//  Collection (Logical)
	0x09, 0x71,			//   Usage (Autonomous Mode)
	0x15, 0x00,			//   Logical Minimum (0)
	0x25, 0x01,			//   Logical Maximum (1)
	0x75, 0x08,			//   Report Size (8)
	0x95, 0x01,			//   Report Count (1)
	0xb1, 0x02,			//   Feature (Data,Var,Abs)
	0xc0,				//  End Collection
	0xc0				// End Collection
};

struct tux_kbl_map_entry_t {
	u8 code;
	struct {
		u32 x;
		u32 y;
		u32 z;
	} pos;
};

static const struct tux_kbl_map_entry_t sirius_16_ansii_kbl_map[] = {
	{ 0x29, {  25000,  53000, 5000 } },
	{ 0x3a, {  41700,  53000, 5000 } },
	{ 0x3b, {  58400,  53000, 5000 } },
	{ 0x3c, {  75100,  53000, 5000 } },
	{ 0x3d, {  91800,  53000, 5000 } },
	{ 0x3e, { 108500,  53000, 5000 } },
	{ 0x3f, { 125200,  53000, 5000 } },
	{ 0x40, { 141900,  53000, 5000 } },
	{ 0x41, { 158600,  53000, 5000 } },
	{ 0x42, { 175300,  53000, 5000 } },
	{ 0x43, { 192000,  53000, 5000 } },
	{ 0x44, { 208700,  53000, 5000 } },
	{ 0x45, { 225400,  53000, 5000 } },
	{ 0xf1, { 242100,  53000, 5000 } },
	{ 0x46, { 258800,  53000, 5000 } },
	{ 0x4c, { 275500,  53000, 5000 } },
	{ 0x4a, { 294500,  53000, 5000 } },
	{ 0x4d, { 311200,  53000, 5000 } },
	{ 0x4b, { 327900,  53000, 5000 } },
	{ 0x4e, { 344600,  53000, 5000 } },
	{ 0x35, {  24500,  67500, 5250 } },
	{ 0x1e, {  42500,  67500, 5250 } },
	{ 0x1f, {  61000,  67500, 5250 } },
	{ 0x20, {  79500,  67500, 5250 } },
	{ 0x21, {  98000,  67500, 5250 } },
	{ 0x22, { 116500,  67500, 5250 } },
	{ 0x23, { 135000,  67500, 5250 } },
	{ 0x24, { 153500,  67500, 5250 } },
	{ 0x25, { 172000,  67500, 5250 } },
	{ 0x26, { 190500,  67500, 5250 } },
	{ 0x27, { 209000,  67500, 5250 } },
	{ 0x2d, { 227500,  67500, 5250 } },
	{ 0x2e, { 246000,  67500, 5250 } },
	{ 0x2a, { 269500,  67500, 5250 } },
	{ 0x53, { 294500,  67500, 5250 } },
	{ 0x55, { 311200,  67500, 5250 } },
	{ 0x54, { 327900,  67500, 5250 } },
	{ 0x56, { 344600,  67500, 5250 } },
	{ 0x2b, {  31000,  85500, 5500 } },
	{ 0x14, {  51500,  85500, 5500 } },
	{ 0x1a, {  70000,  85500, 5500 } },
	{ 0x08, {  88500,  85500, 5500 } },
	{ 0x15, { 107000,  85500, 5500 } },
	{ 0x17, { 125500,  85500, 5500 } },
	{ 0x1c, { 144000,  85500, 5500 } },
	{ 0x18, { 162500,  85500, 5500 } },
	{ 0x0c, { 181000,  85500, 5500 } },
	{ 0x12, { 199500,  85500, 5500 } },
	{ 0x13, { 218000,  85500, 5500 } },
	{ 0x2f, { 236500,  85500, 5500 } },
	{ 0x30, { 255000,  85500, 5500 } },
	{ 0x31, { 273500,  85500, 5500 } },
	{ 0x5f, { 294500,  85500, 5500 } },
	{ 0x60, { 311200,  85500, 5500 } },
	{ 0x61, { 327900,  85500, 5500 } },
	{ 0x39, {  33000, 103500, 5750 } },
	{ 0x04, {  57000, 103500, 5750 } },
	{ 0x16, {  75500, 103500, 5750 } },
	{ 0x07, {  94000, 103500, 5750 } },
	{ 0x09, { 112500, 103500, 5750 } },
	{ 0x0a, { 131000, 103500, 5750 } },
	{ 0x0b, { 149500, 103500, 5750 } },
	{ 0x0d, { 168000, 103500, 5750 } },
	{ 0x0e, { 186500, 103500, 5750 } },
	{ 0x0f, { 205000, 103500, 5750 } },
	{ 0x33, { 223500, 103500, 5750 } },
	{ 0x34, { 242000, 103500, 5750 } },
	{ 0x28, { 267500, 103500, 5750 } },
	{ 0x5c, { 294500, 103500, 5750 } },
	{ 0x5d, { 311200, 103500, 5750 } },
	{ 0x5e, { 327900, 103500, 5750 } },
	{ 0x57, { 344600,  94500, 5625 } },
	{ 0xe1, {  37000, 121500, 6000 } },
	{ 0x1d, {  66000, 121500, 6000 } },
	{ 0x1b, {  84500, 121500, 6000 } },
	{ 0x06, { 103000, 121500, 6000 } },
	{ 0x19, { 121500, 121500, 6000 } },
	{ 0x05, { 140000, 121500, 6000 } },
	{ 0x11, { 158500, 121500, 6000 } },
	{ 0x10, { 177000, 121500, 6000 } },
	{ 0x36, { 195500, 121500, 6000 } },
	{ 0x37, { 214000, 121500, 6000 } },
	{ 0x38, { 232500, 121500, 6000 } },
	{ 0xe5, { 251500, 121500, 6000 } },
	{ 0x52, { 273500, 129000, 6125 } },
	{ 0x59, { 294500, 121500, 6000 } },
	{ 0x5a, { 311200, 121500, 6000 } },
	{ 0x5b, { 327900, 121500, 6000 } },
	{ 0xe0, {  28000, 139500, 6250 } },
	{ 0xfe, {  47500, 139500, 6250 } },
	{ 0xe3, {  66000, 139500, 6250 } },
	{ 0xe2, {  84500, 139500, 6250 } },
	{ 0x2c, { 140000, 139500, 6250 } },
	{ 0xe6, { 195500, 139500, 6250 } },
	{ 0x65, { 214000, 139500, 6250 } },
	{ 0xe4, { 234000, 139500, 6250 } },
	{ 0x50, { 255000, 147000, 6375 } },
	{ 0x51, { 273500, 147000, 6375 } },
	{ 0x4f, { 292000, 147000, 6375 } },
	{ 0x62, { 311200, 139500, 6250 } },
	{ 0x63, { 327900, 139500, 6250 } },
	{ 0x58, { 344600, 130500, 6125 } },
};

static const struct tux_kbl_map_entry_t sirius_16_iso_kbl_map[] = {
	{ 0x29, {  25000,  53000, 5000 } },
	{ 0x3a, {  41700,  53000, 5000 } },
	{ 0x3b, {  58400,  53000, 5000 } },
	{ 0x3c, {  75100,  53000, 5000 } },
	{ 0x3d, {  91800,  53000, 5000 } },
	{ 0x3e, { 108500,  53000, 5000 } },
	{ 0x3f, { 125200,  53000, 5000 } },
	{ 0x40, { 141900,  53000, 5000 } },
	{ 0x41, { 158600,  53000, 5000 } },
	{ 0x42, { 175300,  53000, 5000 } },
	{ 0x43, { 192000,  53000, 5000 } },
	{ 0x44, { 208700,  53000, 5000 } },
	{ 0x45, { 225400,  53000, 5000 } },
	{ 0xf1, { 242100,  53000, 5000 } },
	{ 0x46, { 258800,  53000, 5000 } },
	{ 0x4c, { 275500,  53000, 5000 } },
	{ 0x4a, { 294500,  53000, 5000 } },
	{ 0x4d, { 311200,  53000, 5000 } },
	{ 0x4b, { 327900,  53000, 5000 } },
	{ 0x4e, { 344600,  53000, 5000 } },
	{ 0x35, {  24500,  67500, 5250 } },
	{ 0x1e, {  42500,  67500, 5250 } },
	{ 0x1f, {  61000,  67500, 5250 } },
	{ 0x20, {  79500,  67500, 5250 } },
	{ 0x21, {  98000,  67500, 5250 } },
	{ 0x22, { 116500,  67500, 5250 } },
	{ 0x23, { 135000,  67500, 5250 } },
	{ 0x24, { 153500,  67500, 5250 } },
	{ 0x25, { 172000,  67500, 5250 } },
	{ 0x26, { 190500,  67500, 5250 } },
	{ 0x27, { 209000,  67500, 5250 } },
	{ 0x2d, { 227500,  67500, 5250 } },
	{ 0x2e, { 246000,  67500, 5250 } },
	{ 0x2a, { 269500,  67500, 5250 } },
	{ 0x53, { 294500,  67500, 5250 } },
	{ 0x55, { 311200,  67500, 5250 } },
	{ 0x54, { 327900,  67500, 5250 } },
	{ 0x56, { 344600,  67500, 5250 } },
	{ 0x2b, {  31000,  85500, 5500 } },
	{ 0x14, {  51500,  85500, 5500 } },
	{ 0x1a, {  70000,  85500, 5500 } },
	{ 0x08, {  88500,  85500, 5500 } },
	{ 0x15, { 107000,  85500, 5500 } },
	{ 0x17, { 125500,  85500, 5500 } },
	{ 0x1c, { 144000,  85500, 5500 } },
	{ 0x18, { 162500,  85500, 5500 } },
	{ 0x0c, { 181000,  85500, 5500 } },
	{ 0x12, { 199500,  85500, 5500 } },
	{ 0x13, { 218000,  85500, 5500 } },
	{ 0x2f, { 234500,  85500, 5500 } },
	{ 0x30, { 251000,  85500, 5500 } },
	{ 0x5f, { 294500,  85500, 5500 } },
	{ 0x60, { 311200,  85500, 5500 } },
	{ 0x61, { 327900,  85500, 5500 } },
	{ 0x39, {  33000, 103500, 5750 } },
	{ 0x04, {  57000, 103500, 5750 } },
	{ 0x16, {  75500, 103500, 5750 } },
	{ 0x07, {  94000, 103500, 5750 } },
	{ 0x09, { 112500, 103500, 5750 } },
	{ 0x0a, { 131000, 103500, 5750 } },
	{ 0x0b, { 149500, 103500, 5750 } },
	{ 0x0d, { 168000, 103500, 5750 } },
	{ 0x0e, { 186500, 103500, 5750 } },
	{ 0x0f, { 205000, 103500, 5750 } },
	{ 0x33, { 223500, 103500, 5750 } },
	{ 0x34, { 240000, 103500, 5750 } },
	{ 0x32, { 256500, 103500, 5750 } },
	{ 0x28, { 271500,  94500, 5750 } },
	{ 0x5c, { 294500, 103500, 5750 } },
	{ 0x5d, { 311200, 103500, 5750 } },
	{ 0x5e, { 327900, 103500, 5750 } },
	{ 0x57, { 344600,  94500, 5625 } },
	{ 0xe1, {  28000, 121500, 6000 } },
	{ 0x64, {  47500, 121500, 6000 } },
	{ 0x1d, {  66000, 121500, 6000 } },
	{ 0x1b, {  84500, 121500, 6000 } },
	{ 0x06, { 103000, 121500, 6000 } },
	{ 0x19, { 121500, 121500, 6000 } },
	{ 0x05, { 140000, 121500, 6000 } },
	{ 0x11, { 158500, 121500, 6000 } },
	{ 0x10, { 177000, 121500, 6000 } },
	{ 0x36, { 195500, 121500, 6000 } },
	{ 0x37, { 214000, 121500, 6000 } },
	{ 0x38, { 232500, 121500, 6000 } },
	{ 0xe5, { 251500, 121500, 6000 } },
	{ 0x52, { 273500, 129000, 6125 } },
	{ 0x59, { 294500, 121500, 6000 } },
	{ 0x5a, { 311200, 121500, 6000 } },
	{ 0x5b, { 327900, 121500, 6000 } },
	{ 0xe0, {  28000, 139500, 6250 } },
	{ 0xfe, {  47500, 139500, 6250 } },
	{ 0xe3, {  66000, 139500, 6250 } },
	{ 0xe2, {  84500, 139500, 6250 } },
	{ 0x2c, { 140000, 139500, 6250 } },
	{ 0xe6, { 195500, 139500, 6250 } },
	{ 0x65, { 214000, 139500, 6250 } },
	{ 0xe4, { 234000, 139500, 6250 } },
	{ 0x50, { 255000, 147000, 6375 } },
	{ 0x51, { 273500, 147000, 6375 } },
	{ 0x4f, { 292000, 147000, 6375 } },
	{ 0x62, { 311200, 139500, 6250 } },
	{ 0x63, { 327900, 139500, 6250 } },
	{ 0x58, { 344600, 130500, 6125 } },
};

struct tux_driver_data_t {
	struct hid_device *hdev;
};

struct tux_hdev_driver_data_t {
	u8 lamp_count;
	const struct tux_kbl_map_entry_t *kbl_map;
	u8 next_lamp_id;
	union tux_wmi_xx_496in_80out_in_t next_kbl_set_multiple_keys_in;
};

static int tux_ll_start(struct hid_device *hdev)
{
	struct wmi_device *wdev = to_wmi_device(hdev->dev.parent);
	struct tux_hdev_driver_data_t *driver_data;
	union tux_wmi_xx_8in_80out_out_t out;
	union tux_wmi_xx_8in_80out_in_t in;
	u8 keyboard_type;
	int ret;

	driver_data = devm_kzalloc(&hdev->dev, sizeof(*driver_data), GFP_KERNEL);
	if (!driver_data)
		return -ENOMEM;

	in.get_device_status_in.device_type = TUX_GET_DEVICE_STATUS_DEVICE_ID_KEYBOARD;
	ret = tux_wmi_xx_8in_80out(wdev, TUX_GET_DEVICE_STATUS, &in, &out);
	if (ret)
		return ret;

	keyboard_type = out.get_device_status_out.keyboard_physical_layout;
	if (keyboard_type == TUX_GET_DEVICE_STATUS_KEYBOARD_LAYOUT_ANSII) {
		driver_data->lamp_count = ARRAY_SIZE(sirius_16_ansii_kbl_map);
		driver_data->kbl_map = sirius_16_ansii_kbl_map;
	} else if (keyboard_type == TUX_GET_DEVICE_STATUS_KEYBOARD_LAYOUT_ISO) {
		driver_data->lamp_count = ARRAY_SIZE(sirius_16_iso_kbl_map);
		driver_data->kbl_map = sirius_16_iso_kbl_map;
	} else {
		return -EINVAL;
	}
	driver_data->next_lamp_id = 0;

	dev_set_drvdata(&hdev->dev, driver_data);

	return ret;
}

static void tux_ll_stop(struct hid_device *hdev __always_unused)
{
}

static int tux_ll_open(struct hid_device *hdev __always_unused)
{
	return 0;
}

static void tux_ll_close(struct hid_device *hdev __always_unused)
{
}

static int tux_ll_parse(struct hid_device *hdev)
{
	return hid_parse_report(hdev, tux_report_descriptor,
				sizeof(tux_report_descriptor));
}

struct __packed lamp_array_attributes_report_t {
	const u8 report_id;
	u16 lamp_count;
	u32 bounding_box_width_in_micrometers;
	u32 bounding_box_height_in_micrometers;
	u32 bounding_box_depth_in_micrometers;
	u32 lamp_array_kind;
	u32 min_update_interval_in_microseconds;
};

static int handle_lamp_array_attributes_report(struct hid_device *hdev,
					       struct lamp_array_attributes_report_t *rep)
{
	struct tux_hdev_driver_data_t *driver_data = dev_get_drvdata(&hdev->dev);

	rep->lamp_count = driver_data->lamp_count;
	rep->bounding_box_width_in_micrometers = 368000;
	rep->bounding_box_height_in_micrometers = 266000;
	rep->bounding_box_depth_in_micrometers = 30000;
	/*
	 * LampArrayKindKeyboard, see "26.2.1 LampArrayKind Values" of
	 * "HID Usage Tables v1.5"
	 */
	rep->lamp_array_kind = 1;
	// Some guessed value for interval microseconds
	rep->min_update_interval_in_microseconds = 500;

	return sizeof(*rep);
}

struct __packed lamp_attributes_request_report_t {
	const u8 report_id;
	u16 lamp_id;
};

static int handle_lamp_attributes_request_report(struct hid_device *hdev,
						 struct lamp_attributes_request_report_t *rep)
{
	struct tux_hdev_driver_data_t *driver_data = dev_get_drvdata(&hdev->dev);

	if (rep->lamp_id < driver_data->lamp_count)
		driver_data->next_lamp_id = rep->lamp_id;
	else
		driver_data->next_lamp_id = 0;

	return sizeof(*rep);
}

struct __packed lamp_attributes_response_report_t {
	const u8 report_id;
	u16 lamp_id;
	u32 position_x_in_micrometers;
	u32 position_y_in_micrometers;
	u32 position_z_in_micrometers;
	u32 update_latency_in_microseconds;
	u32 lamp_purpose;
	u8 red_level_count;
	u8 green_level_count;
	u8 blue_level_count;
	u8 intensity_level_count;
	u8 is_programmable;
	u8 input_binding;
};

static int handle_lamp_attributes_response_report(struct hid_device *hdev,
						  struct lamp_attributes_response_report_t *rep)
{
	struct tux_hdev_driver_data_t *driver_data = dev_get_drvdata(&hdev->dev);
	u16 lamp_id = driver_data->next_lamp_id;

	rep->lamp_id = lamp_id;
	// Some guessed value for latency microseconds
	rep->update_latency_in_microseconds = 100;
	/*
	 * LampPurposeControl, see "26.3.1 LampPurposes Flags" of
	 * "HID Usage Tables v1.5"
	 */
	rep->lamp_purpose = 1;
	rep->red_level_count = 0xff;
	rep->green_level_count = 0xff;
	rep->blue_level_count = 0xff;
	rep->intensity_level_count = 0xff;
	rep->is_programmable = 1;

	if (driver_data->kbl_map[lamp_id].code <= 0xe8) {
		rep->input_binding = driver_data->kbl_map[lamp_id].code;
	} else {
		/*
		 * Everything bigger is reserved/undefined, see
		 * "10 Keyboard/Keypad Page (0x07)" of "HID Usage Tables v1.5"
		 * and should return 0, see "26.8.3 Lamp Attributes" of the same
		 * document.
		 */
		rep->input_binding = 0;
	}
	rep->position_x_in_micrometers = driver_data->kbl_map[lamp_id].pos.x;
	rep->position_y_in_micrometers = driver_data->kbl_map[lamp_id].pos.y;
	rep->position_z_in_micrometers = driver_data->kbl_map[lamp_id].pos.z;

	driver_data->next_lamp_id = (driver_data->next_lamp_id + 1) % driver_data->lamp_count;

	return sizeof(*rep);
}

#define LAMP_UPDATE_FLAGS_LAMP_UPDATE_COMPLETE	BIT(0)

struct __packed lamp_rgbi_tuple_t {
	u8 red;
	u8 green;
	u8 blue;
	u8 intensity;
};

#define LAMP_MULTI_UPDATE_REPORT_LAMP_COUNT_MAX	8

struct __packed lamp_multi_update_report_t {
	const u8 report_id;
	u8 lamp_count;
	u8 lamp_update_flags;
	u16 lamp_id[LAMP_MULTI_UPDATE_REPORT_LAMP_COUNT_MAX];
	struct lamp_rgbi_tuple_t update_channels[LAMP_MULTI_UPDATE_REPORT_LAMP_COUNT_MAX];
};

static int handle_lamp_multi_update_report(struct hid_device *hdev,
					   struct lamp_multi_update_report_t *rep)
{
	struct tux_hdev_driver_data_t *driver_data = dev_get_drvdata(&hdev->dev);
	union tux_wmi_xx_496in_80out_in_t *next = &driver_data->next_kbl_set_multiple_keys_in;
	struct tux_kbl_set_multiple_keys_in_rgb_config_t *rgb_configs_j;
	struct wmi_device *wdev = to_wmi_device(hdev->dev.parent);
	union tux_wmi_xx_496in_80out_out_t out;
	u8 key_id, key_id_j, intensity_i, red_i, green_i, blue_i;
	int ret;

	/*
	 * Catching misformatted lamp_multi_update_report and fail silently
	 * according to "HID Usage Tables v1.5"
	 */
	for (unsigned int i = 0; i < rep->lamp_count; ++i) {
		if (rep->lamp_id[i] > driver_data->lamp_count) {
			hid_dbg(hdev, "Out of bounds lamp_id in lamp_multi_update_report. Skipping whole report!\n");
			return sizeof(*rep);
		}

		for (unsigned int j = i + 1; j < rep->lamp_count; ++j) {
			if (rep->lamp_id[i] == rep->lamp_id[j]) {
				hid_dbg(hdev, "Duplicate lamp_id in lamp_multi_update_report. Skipping whole report!\n");
				return sizeof(*rep);
			}
		}
	}

	for (unsigned int i = 0; i < rep->lamp_count; ++i) {
		key_id = driver_data->kbl_map[rep->lamp_id[i]].code;

		for (unsigned int j = 0;
		     j < TUX_KBL_SET_MULTIPLE_KEYS_LIGHTING_SETTINGS_COUNT_MAX;
		     ++j) {
			rgb_configs_j = &next->kbl_set_multiple_keys_in.rgb_configs[j];
			key_id_j = rgb_configs_j->key_id;
			if (key_id_j != 0x00 && key_id_j != key_id)
				continue;

			if (key_id_j == 0x00)
				next->kbl_set_multiple_keys_in.rgb_configs_cnt =
					j + 1;
			rgb_configs_j->key_id = key_id;
			/*
			 * While this driver respects update_channel.intensity
			 * according to "HID Usage Tables v1.5" also on RGB
			 * leds, the Microsoft MacroPad reference implementation
			 * (https://github.com/microsoft/RP2040MacropadHidSample
			 * 1d6c3ad) does not and ignores it. If it turns out
			 * that Windows writes intensity = 0 for RGB leds
			 * instead of intensity = 255, this driver should also
			 * ignore the update_channel.intensity.
			 */
			intensity_i = rep->update_channels[i].intensity;
			red_i = rep->update_channels[i].red;
			green_i = rep->update_channels[i].green;
			blue_i = rep->update_channels[i].blue;
			rgb_configs_j->red = red_i * intensity_i / 0xff;
			rgb_configs_j->green = green_i * intensity_i / 0xff;
			rgb_configs_j->blue = blue_i * intensity_i / 0xff;

			break;
		}
	}

	if (rep->lamp_update_flags & LAMP_UPDATE_FLAGS_LAMP_UPDATE_COMPLETE) {
		ret = tux_wmi_xx_496in_80out(wdev, TUX_KBL_SET_MULTIPLE_KEYS,
					     next, &out);
		memset(next, 0, sizeof(*next));
		if (ret)
			return ret;
	}

	return sizeof(*rep);
}

struct __packed lamp_range_update_report_t {
	const u8 report_id;
	u8 lamp_update_flags;
	u16 lamp_id_start;
	u16 lamp_id_end;
	struct lamp_rgbi_tuple_t update_channel;
};

static int handle_lamp_range_update_report(struct hid_device *hdev,
					   struct lamp_range_update_report_t *rep)
{
	struct tux_hdev_driver_data_t *driver_data = dev_get_drvdata(&hdev->dev);
	struct lamp_multi_update_report_t lamp_multi_update_report = {
		.report_id = LAMP_MULTI_UPDATE_REPORT_ID,
	};
	struct lamp_rgbi_tuple_t *update_channels_j;
	int ret;

	/*
	 * Catching misformatted lamp_range_update_report and fail silently
	 * according to "HID Usage Tables v1.5"
	 */
	if (rep->lamp_id_start > rep->lamp_id_end) {
		hid_dbg(hdev, "lamp_id_start > lamp_id_end in lamp_range_update_report. Skipping whole report!\n");
		return sizeof(*rep);
	}

	if (rep->lamp_id_end > driver_data->lamp_count - 1) {
		hid_dbg(hdev, "Out of bounds lamp_id_end in lamp_range_update_report. Skipping whole report!\n");
		return sizeof(*rep);
	}

	/*
	 * Break handle_lamp_range_update_report call down to multiple
	 * handle_lamp_multi_update_report calls to easily ensure that mixing
	 * handle_lamp_range_update_report and handle_lamp_multi_update_report
	 * does not break things.
	 */
	for (unsigned int i = rep->lamp_id_start; i < rep->lamp_id_end + 1;
	     i = i + LAMP_MULTI_UPDATE_REPORT_LAMP_COUNT_MAX) {
		lamp_multi_update_report.lamp_count =
			min(rep->lamp_id_end + 1 - i,
			    LAMP_MULTI_UPDATE_REPORT_LAMP_COUNT_MAX);
		lamp_multi_update_report.lamp_update_flags =
			i + LAMP_MULTI_UPDATE_REPORT_LAMP_COUNT_MAX >=
			rep->lamp_id_end + 1 ?
				LAMP_UPDATE_FLAGS_LAMP_UPDATE_COMPLETE : 0;

		for (unsigned int j = 0; j < lamp_multi_update_report.lamp_count; ++j) {
			lamp_multi_update_report.lamp_id[j] = i + j;
			update_channels_j =
				&lamp_multi_update_report.update_channels[j];
			update_channels_j->red = rep->update_channel.red;
			update_channels_j->green = rep->update_channel.green;
			update_channels_j->blue = rep->update_channel.blue;
			update_channels_j->intensity = rep->update_channel.intensity;
		}

		ret = handle_lamp_multi_update_report(hdev, &lamp_multi_update_report);
		if (ret < 0)
			return ret;
		if (ret != sizeof(lamp_multi_update_report))
			return -EIO;
	}

	return sizeof(*rep);
}

struct __packed lamp_array_control_report_t {
	const u8 report_id;
	u8 autonomous_mode;
};

static int handle_lamp_array_control_report(struct hid_device *hdev __always_unused,
					    struct lamp_array_control_report_t *rep)
{
	/*
	 * The keyboards firmware doesn't have any built in controls and the
	 * built in effects are not implemented so this is a NOOP.
	 * According to the HID Documentation (HID Usage Tables v1.5) this
	 * function is optional and can be removed from the HID Report
	 * Descriptor, but it should first be confirmed that userspace respects
	 * this possibility too. The Microsoft MacroPad reference implementation
	 * (https://github.com/microsoft/RP2040MacropadHidSample 1d6c3ad)
	 * already deviates from the spec at another point, see
	 * handle_lamp_*_update_report.
	 */

	return sizeof(*rep);
}

static int tux_ll_raw_request(struct hid_device *hdev, u8 reportnum, u8 *buf,
			      size_t len, unsigned char rtype, int reqtype)
{
	if (rtype != HID_FEATURE_REPORT)
		return -EINVAL;

	switch (reqtype) {
	case HID_REQ_GET_REPORT:
		switch (reportnum) {
		case LAMP_ARRAY_ATTRIBUTES_REPORT_ID:
			if (len != sizeof(struct lamp_array_attributes_report_t))
				return -EINVAL;
			return handle_lamp_array_attributes_report(hdev,
				(struct lamp_array_attributes_report_t *)buf);
		case LAMP_ATTRIBUTES_RESPONSE_REPORT_ID:
			if (len != sizeof(struct lamp_attributes_response_report_t))
				return -EINVAL;
			return handle_lamp_attributes_response_report(hdev,
				(struct lamp_attributes_response_report_t *)buf);
		}
		break;
	case HID_REQ_SET_REPORT:
		switch (reportnum) {
		case LAMP_ATTRIBUTES_REQUEST_REPORT_ID:
			if (len != sizeof(struct lamp_attributes_request_report_t))
				return -EINVAL;
			return handle_lamp_attributes_request_report(hdev,
				(struct lamp_attributes_request_report_t *)buf);
		case LAMP_MULTI_UPDATE_REPORT_ID:
			if (len != sizeof(struct lamp_multi_update_report_t))
				return -EINVAL;
			return handle_lamp_multi_update_report(hdev,
				(struct lamp_multi_update_report_t *)buf);
		case LAMP_RANGE_UPDATE_REPORT_ID:
			if (len != sizeof(struct lamp_range_update_report_t))
				return -EINVAL;
			return handle_lamp_range_update_report(hdev,
				(struct lamp_range_update_report_t *)buf);
		case LAMP_ARRAY_CONTROL_REPORT_ID:
			if (len != sizeof(struct lamp_array_control_report_t))
				return -EINVAL;
			return handle_lamp_array_control_report(hdev,
				(struct lamp_array_control_report_t *)buf);
		}
		break;
	}

	return -EINVAL;
}

static const struct hid_ll_driver tux_ll_driver = {
	.start = &tux_ll_start,
	.stop = &tux_ll_stop,
	.open = &tux_ll_open,
	.close = &tux_ll_close,
	.parse = &tux_ll_parse,
	.raw_request = &tux_ll_raw_request,
};

static int tux_virt_lamparray_add_device(struct wmi_device *wdev,
					 struct hid_device **hdev_out)
{
	struct hid_device *hdev;
	int ret;

	dev_dbg(&wdev->dev, "Adding TUXEDO NB04 Virtual LampArray device.\n");

	hdev = hid_allocate_device();
	if (IS_ERR(hdev))
		return PTR_ERR(hdev);
	*hdev_out = hdev;

	strscpy(hdev->name, "TUXEDO NB04 RGB Lighting", sizeof(hdev->name));

	hdev->ll_driver = &tux_ll_driver;
	hdev->bus = BUS_VIRTUAL;
	hdev->vendor = 0x21ba;
	hdev->product = 0x0400;
	hdev->dev.parent = &wdev->dev;

	ret = hid_add_device(hdev);
	if (ret)
		hid_destroy_device(hdev);
	return ret;
}

static int tux_probe(struct wmi_device *wdev, const void *context __always_unused)
{
	struct tux_driver_data_t *driver_data;

	driver_data = devm_kzalloc(&wdev->dev, sizeof(*driver_data), GFP_KERNEL);
	if (!driver_data)
		return -ENOMEM;

	dev_set_drvdata(&wdev->dev, driver_data);

	return tux_virt_lamparray_add_device(wdev, &driver_data->hdev);
}

static void tux_remove(struct wmi_device *wdev)
{
	struct tux_driver_data_t *driver_data = dev_get_drvdata(&wdev->dev);

	hid_destroy_device(driver_data->hdev);
}

static struct wmi_driver tuxedo_nb04_wmi_tux_driver = {
	.driver = {
		.name = "tuxedo_nb04_wmi_ab",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = tuxedo_nb04_wmi_ab_device_ids,
	.probe = tux_probe,
	.remove = tux_remove,
	.no_singleton = true,
};

/*
 * We don't know if the WMI API is stable and how unique the GUID is for this
 * ODM. To be on the safe side we therefore only run this driver on tested
 * devices defined by this list.
 */
static const struct dmi_system_id tested_devices_dmi_table[] __initconst = {
	{
		// TUXEDO Sirius 16 Gen1
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "APX958"),
		},
	},
	{
		// TUXEDO Sirius 16 Gen2
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "AHP958"),
		},
	},
	{ }
};

static int __init tuxedo_nb04_wmi_tux_init(void)
{
	if (!dmi_check_system(tested_devices_dmi_table))
		return -ENODEV;

	return wmi_driver_register(&tuxedo_nb04_wmi_tux_driver);
}
module_init(tuxedo_nb04_wmi_tux_init);

static void __exit tuxedo_nb04_wmi_tux_exit(void)
{
	return wmi_driver_unregister(&tuxedo_nb04_wmi_tux_driver);
}
module_exit(tuxedo_nb04_wmi_tux_exit);

MODULE_DESCRIPTION("Virtual HID LampArray interface for TUXEDO NB04 devices");
MODULE_AUTHOR("Werner Sembach <wse@tuxedocomputers.com>");
MODULE_LICENSE("GPL");
