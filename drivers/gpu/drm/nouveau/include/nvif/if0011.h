/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_IF0011_H__
#define __NVIF_IF0011_H__

union nvif_conn_args {
	struct nvif_conn_v0 {
		__u8 version;
		__u8 id;	/* DCB connector table index. */
		__u8 pad02[6];
#define NVIF_CONN_V0_VGA       0x00
#define NVIF_CONN_V0_TV        0x01
#define NVIF_CONN_V0_DVI_I     0x02
#define NVIF_CONN_V0_DVI_D     0x03
#define NVIF_CONN_V0_LVDS      0x04
#define NVIF_CONN_V0_LVDS_SPWG 0x05
#define NVIF_CONN_V0_HDMI      0x06
#define NVIF_CONN_V0_DP        0x07
#define NVIF_CONN_V0_EDP       0x08
		__u8 type;
	} v0;
};

union nvif_conn_event_args {
	struct nvif_conn_event_v0 {
		__u8 version;
#define NVIF_CONN_EVENT_V0_PLUG   0x01
#define NVIF_CONN_EVENT_V0_UNPLUG 0x02
#define NVIF_CONN_EVENT_V0_IRQ    0x04
		__u8 types;
		__u8 pad02[6];
	} v0;
};
#endif
