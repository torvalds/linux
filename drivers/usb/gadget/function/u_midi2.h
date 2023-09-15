// SPDX-License-Identifier: GPL-2.0+
/*
 * Utility definitions for MIDI 2.0 function
 */

#ifndef U_MIDI2_H
#define U_MIDI2_H

#include <linux/usb/composite.h>
#include <sound/asound.h>

struct f_midi2_opts;
struct f_midi2_ep_opts;
struct f_midi2_block_opts;

/* UMP Function Block info */
struct f_midi2_block_info {
	unsigned int direction;		/* FB direction: 1-3 */
	unsigned int first_group;	/* first UMP group: 0-15 */
	unsigned int num_groups;	/* number of UMP groups: 1-16 */
	unsigned int midi1_first_group;	/* first UMP group for MIDI 1.0 */
	unsigned int midi1_num_groups;	/* number of UMP groups for MIDI 1.0 */
	unsigned int ui_hint;		/* UI-hint: 0-3 */
	unsigned int midi_ci_version;	/* MIDI-CI version: 0-255 */
	unsigned int sysex8_streams;	/* number of sysex8 streams: 0-255 */
	unsigned int is_midi1;		/* MIDI 1.0 port: 0-2 */
	bool active;			/* FB active flag: bool */
	const char *name;		/* FB name */
};

/* UMP Endpoint info */
struct f_midi2_ep_info {
	unsigned int protocol_caps;	/* protocol capabilities: 1-3 */
	unsigned int protocol;		/* default protocol: 1-2 */
	unsigned int manufacturer;	/* manufacturer id: 0-0xffffff */
	unsigned int family;		/* device family id: 0-0xffff */
	unsigned int model;		/* device model id: 0x-0xffff */
	unsigned int sw_revision;	/* software revision: 32bit */

	const char *ep_name;		/* Endpoint name */
	const char *product_id;		/* Product ID */
};

struct f_midi2_card_info {
	bool process_ump;		/* process UMP stream: bool */
	bool static_block;		/* static FBs: bool */
	unsigned int req_buf_size;	/* request buffer size */
	unsigned int num_reqs;		/* number of requests */
	const char *iface_name;		/* interface name */
};

struct f_midi2_block_opts {
	struct config_group group;
	unsigned int id;
	struct f_midi2_block_info info;
	struct f_midi2_ep_opts *ep;
};

struct f_midi2_ep_opts {
	struct config_group group;
	unsigned int index;
	struct f_midi2_ep_info info;
	struct f_midi2_block_opts *blks[SNDRV_UMP_MAX_BLOCKS];
	struct f_midi2_opts *opts;
};

#define MAX_UMP_EPS		4
#define MAX_CABLES		16

struct f_midi2_opts {
	struct usb_function_instance func_inst;
	struct mutex lock;
	int refcnt;

	struct f_midi2_card_info info;

	unsigned int num_eps;
	struct f_midi2_ep_opts *eps[MAX_UMP_EPS];
};

#endif /* U_MIDI2_H */
