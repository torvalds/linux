/*
   Copyright (C) 2009 Red Hat, Inc.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
	 notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above copyright
	 notice, this list of conditions and the following disclaimer in
	 the documentation and/or other materials provided with the
	 distribution.
       * Neither the name of the copyright holder nor the names of its
	 contributors may be used to endorse or promote products derived
	 from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS "AS
   IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
   TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
   PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#ifndef H_QXL_DEV
#define H_QXL_DEV

#include <linux/types.h>

/*
 * from spice-protocol
 * Release 0.10.0
 */

/* enums.h */

enum SpiceImageType {
	SPICE_IMAGE_TYPE_BITMAP,
	SPICE_IMAGE_TYPE_QUIC,
	SPICE_IMAGE_TYPE_RESERVED,
	SPICE_IMAGE_TYPE_LZ_PLT = 100,
	SPICE_IMAGE_TYPE_LZ_RGB,
	SPICE_IMAGE_TYPE_GLZ_RGB,
	SPICE_IMAGE_TYPE_FROM_CACHE,
	SPICE_IMAGE_TYPE_SURFACE,
	SPICE_IMAGE_TYPE_JPEG,
	SPICE_IMAGE_TYPE_FROM_CACHE_LOSSLESS,
	SPICE_IMAGE_TYPE_ZLIB_GLZ_RGB,
	SPICE_IMAGE_TYPE_JPEG_ALPHA,

	SPICE_IMAGE_TYPE_ENUM_END
};

enum SpiceBitmapFmt {
	SPICE_BITMAP_FMT_INVALID,
	SPICE_BITMAP_FMT_1BIT_LE,
	SPICE_BITMAP_FMT_1BIT_BE,
	SPICE_BITMAP_FMT_4BIT_LE,
	SPICE_BITMAP_FMT_4BIT_BE,
	SPICE_BITMAP_FMT_8BIT,
	SPICE_BITMAP_FMT_16BIT,
	SPICE_BITMAP_FMT_24BIT,
	SPICE_BITMAP_FMT_32BIT,
	SPICE_BITMAP_FMT_RGBA,

	SPICE_BITMAP_FMT_ENUM_END
};

enum SpiceSurfaceFmt {
	SPICE_SURFACE_FMT_INVALID,
	SPICE_SURFACE_FMT_1_A,
	SPICE_SURFACE_FMT_8_A = 8,
	SPICE_SURFACE_FMT_16_555 = 16,
	SPICE_SURFACE_FMT_32_xRGB = 32,
	SPICE_SURFACE_FMT_16_565 = 80,
	SPICE_SURFACE_FMT_32_ARGB = 96,

	SPICE_SURFACE_FMT_ENUM_END
};

enum SpiceClipType {
	SPICE_CLIP_TYPE_NONE,
	SPICE_CLIP_TYPE_RECTS,

	SPICE_CLIP_TYPE_ENUM_END
};

enum SpiceRopd {
	SPICE_ROPD_INVERS_SRC = (1 << 0),
	SPICE_ROPD_INVERS_BRUSH = (1 << 1),
	SPICE_ROPD_INVERS_DEST = (1 << 2),
	SPICE_ROPD_OP_PUT = (1 << 3),
	SPICE_ROPD_OP_OR = (1 << 4),
	SPICE_ROPD_OP_AND = (1 << 5),
	SPICE_ROPD_OP_XOR = (1 << 6),
	SPICE_ROPD_OP_BLACKNESS = (1 << 7),
	SPICE_ROPD_OP_WHITENESS = (1 << 8),
	SPICE_ROPD_OP_INVERS = (1 << 9),
	SPICE_ROPD_INVERS_RES = (1 << 10),

	SPICE_ROPD_MASK = 0x7ff
};

enum SpiceBrushType {
	SPICE_BRUSH_TYPE_NONE,
	SPICE_BRUSH_TYPE_SOLID,
	SPICE_BRUSH_TYPE_PATTERN,

	SPICE_BRUSH_TYPE_ENUM_END
};

enum SpiceCursorType {
	SPICE_CURSOR_TYPE_ALPHA,
	SPICE_CURSOR_TYPE_MONO,
	SPICE_CURSOR_TYPE_COLOR4,
	SPICE_CURSOR_TYPE_COLOR8,
	SPICE_CURSOR_TYPE_COLOR16,
	SPICE_CURSOR_TYPE_COLOR24,
	SPICE_CURSOR_TYPE_COLOR32,

	SPICE_CURSOR_TYPE_ENUM_END
};

/* qxl_dev.h */

#pragma pack(push, 1)

#define REDHAT_PCI_VENDOR_ID 0x1b36

/* 0x100-0x11f reserved for spice, 0x1ff used for unstable work */
#define QXL_DEVICE_ID_STABLE 0x0100

enum {
	QXL_REVISION_STABLE_V04 = 0x01,
	QXL_REVISION_STABLE_V06 = 0x02,
	QXL_REVISION_STABLE_V10 = 0x03,
	QXL_REVISION_STABLE_V12 = 0x04,
};

#define QXL_DEVICE_ID_DEVEL 0x01ff
#define QXL_REVISION_DEVEL 0x01

#define QXL_ROM_MAGIC (*(uint32_t *)"QXRO")
#define QXL_RAM_MAGIC (*(uint32_t *)"QXRA")

enum {
	QXL_RAM_RANGE_INDEX,
	QXL_VRAM_RANGE_INDEX,
	QXL_ROM_RANGE_INDEX,
	QXL_IO_RANGE_INDEX,

	QXL_PCI_RANGES
};

/* qxl-1 compat: append only */
enum {
	QXL_IO_NOTIFY_CMD,
	QXL_IO_NOTIFY_CURSOR,
	QXL_IO_UPDATE_AREA,
	QXL_IO_UPDATE_IRQ,
	QXL_IO_NOTIFY_OOM,
	QXL_IO_RESET,
	QXL_IO_SET_MODE,                  /* qxl-1 */
	QXL_IO_LOG,
	/* appended for qxl-2 */
	QXL_IO_MEMSLOT_ADD,
	QXL_IO_MEMSLOT_DEL,
	QXL_IO_DETACH_PRIMARY,
	QXL_IO_ATTACH_PRIMARY,
	QXL_IO_CREATE_PRIMARY,
	QXL_IO_DESTROY_PRIMARY,
	QXL_IO_DESTROY_SURFACE_WAIT,
	QXL_IO_DESTROY_ALL_SURFACES,
	/* appended for qxl-3 */
	QXL_IO_UPDATE_AREA_ASYNC,
	QXL_IO_MEMSLOT_ADD_ASYNC,
	QXL_IO_CREATE_PRIMARY_ASYNC,
	QXL_IO_DESTROY_PRIMARY_ASYNC,
	QXL_IO_DESTROY_SURFACE_ASYNC,
	QXL_IO_DESTROY_ALL_SURFACES_ASYNC,
	QXL_IO_FLUSH_SURFACES_ASYNC,
	QXL_IO_FLUSH_RELEASE,
	/* appended for qxl-4 */
	QXL_IO_MONITORS_CONFIG_ASYNC,

	QXL_IO_RANGE_SIZE
};

typedef uint64_t QXLPHYSICAL;
typedef int32_t QXLFIXED; /* fixed 28.4 */

struct qxl_point_fix {
	QXLFIXED x;
	QXLFIXED y;
};

struct qxl_point {
	int32_t x;
	int32_t y;
};

struct qxl_point_1_6 {
	int16_t x;
	int16_t y;
};

struct qxl_rect {
	int32_t top;
	int32_t left;
	int32_t bottom;
	int32_t right;
};

struct qxl_urect {
	uint32_t top;
	uint32_t left;
	uint32_t bottom;
	uint32_t right;
};

/* qxl-1 compat: append only */
struct qxl_rom {
	uint32_t magic;
	uint32_t id;
	uint32_t update_id;
	uint32_t compression_level;
	uint32_t log_level;
	uint32_t mode;			  /* qxl-1 */
	uint32_t modes_offset;
	uint32_t num_io_pages;
	uint32_t pages_offset;		  /* qxl-1 */
	uint32_t draw_area_offset;	  /* qxl-1 */
	uint32_t surface0_area_size;	  /* qxl-1 name: draw_area_size */
	uint32_t ram_header_offset;
	uint32_t mm_clock;
	/* appended for qxl-2 */
	uint32_t n_surfaces;
	uint64_t flags;
	uint8_t slots_start;
	uint8_t slots_end;
	uint8_t slot_gen_bits;
	uint8_t slot_id_bits;
	uint8_t slot_generation;
	/* appended for qxl-4 */
	uint8_t client_present;
	uint8_t client_capabilities[58];
	uint32_t client_monitors_config_crc;
	struct {
		uint16_t count;
	uint16_t padding;
		struct qxl_urect heads[64];
	} client_monitors_config;
};

/* qxl-1 compat: fixed */
struct qxl_mode {
	uint32_t id;
	uint32_t x_res;
	uint32_t y_res;
	uint32_t bits;
	uint32_t stride;
	uint32_t x_mili;
	uint32_t y_mili;
	uint32_t orientation;
};

/* qxl-1 compat: fixed */
struct qxl_modes {
	uint32_t n_modes;
	struct qxl_mode modes[0];
};

/* qxl-1 compat: append only */
enum qxl_cmd_type {
	QXL_CMD_NOP,
	QXL_CMD_DRAW,
	QXL_CMD_UPDATE,
	QXL_CMD_CURSOR,
	QXL_CMD_MESSAGE,
	QXL_CMD_SURFACE,
};

/* qxl-1 compat: fixed */
struct qxl_command {
	QXLPHYSICAL data;
	uint32_t type;
	uint32_t padding;
};

#define QXL_COMMAND_FLAG_COMPAT		(1<<0)
#define QXL_COMMAND_FLAG_COMPAT_16BPP	(2<<0)

struct qxl_command_ext {
	struct qxl_command cmd;
	uint32_t group_id;
	uint32_t flags;
};

struct qxl_mem_slot {
	uint64_t mem_start;
	uint64_t mem_end;
};

#define QXL_SURF_TYPE_PRIMARY	   0

#define QXL_SURF_FLAG_KEEP_DATA	   (1 << 0)

struct qxl_surface_create {
	uint32_t width;
	uint32_t height;
	int32_t stride;
	uint32_t format;
	uint32_t position;
	uint32_t mouse_mode;
	uint32_t flags;
	uint32_t type;
	QXLPHYSICAL mem;
};

#define QXL_COMMAND_RING_SIZE 32
#define QXL_CURSOR_RING_SIZE 32
#define QXL_RELEASE_RING_SIZE 8

#define QXL_LOG_BUF_SIZE 4096

#define QXL_INTERRUPT_DISPLAY (1 << 0)
#define QXL_INTERRUPT_CURSOR (1 << 1)
#define QXL_INTERRUPT_IO_CMD (1 << 2)
#define QXL_INTERRUPT_ERROR  (1 << 3)
#define QXL_INTERRUPT_CLIENT (1 << 4)
#define QXL_INTERRUPT_CLIENT_MONITORS_CONFIG  (1 << 5)

struct qxl_ring_header {
	uint32_t num_items;
	uint32_t prod;
	uint32_t notify_on_prod;
	uint32_t cons;
	uint32_t notify_on_cons;
};

/* qxl-1 compat: append only */
struct qxl_ram_header {
	uint32_t magic;
	uint32_t int_pending;
	uint32_t int_mask;
	uint8_t log_buf[QXL_LOG_BUF_SIZE];
	struct qxl_ring_header  cmd_ring_hdr;
	struct qxl_command	cmd_ring[QXL_COMMAND_RING_SIZE];
	struct qxl_ring_header  cursor_ring_hdr;
	struct qxl_command	cursor_ring[QXL_CURSOR_RING_SIZE];
	struct qxl_ring_header  release_ring_hdr;
	uint64_t		release_ring[QXL_RELEASE_RING_SIZE];
	struct qxl_rect update_area;
	/* appended for qxl-2 */
	uint32_t update_surface;
	struct qxl_mem_slot mem_slot;
	struct qxl_surface_create create_surface;
	uint64_t flags;

	/* appended for qxl-4 */

	/* used by QXL_IO_MONITORS_CONFIG_ASYNC */
	QXLPHYSICAL monitors_config;
	uint8_t guest_capabilities[64];
};

union qxl_release_info {
	uint64_t id;	  /* in  */
	uint64_t next;	  /* out */
};

struct qxl_release_info_ext {
	union qxl_release_info *info;
	uint32_t group_id;
};

struct qxl_data_chunk {
	uint32_t data_size;
	QXLPHYSICAL prev_chunk;
	QXLPHYSICAL next_chunk;
	uint8_t data[0];
};

struct qxl_message {
	union qxl_release_info release_info;
	uint8_t data[0];
};

struct qxl_compat_update_cmd {
	union qxl_release_info release_info;
	struct qxl_rect area;
	uint32_t update_id;
};

struct qxl_update_cmd {
	union qxl_release_info release_info;
	struct qxl_rect area;
	uint32_t update_id;
	uint32_t surface_id;
};

struct qxl_cursor_header {
	uint64_t unique;
	uint16_t type;
	uint16_t width;
	uint16_t height;
	uint16_t hot_spot_x;
	uint16_t hot_spot_y;
};

struct qxl_cursor {
	struct qxl_cursor_header header;
	uint32_t data_size;
	struct qxl_data_chunk chunk;
};

enum {
	QXL_CURSOR_SET,
	QXL_CURSOR_MOVE,
	QXL_CURSOR_HIDE,
	QXL_CURSOR_TRAIL,
};

#define QXL_CURSOR_DEVICE_DATA_SIZE 128

struct qxl_cursor_cmd {
	union qxl_release_info release_info;
	uint8_t type;
	union {
		struct {
			struct qxl_point_1_6 position;
			uint8_t visible;
			QXLPHYSICAL shape;
		} set;
		struct {
			uint16_t length;
			uint16_t frequency;
		} trail;
		struct qxl_point_1_6 position;
	} u;
	/* todo: dynamic size from rom */
	uint8_t device_data[QXL_CURSOR_DEVICE_DATA_SIZE];
};

enum {
	QXL_DRAW_NOP,
	QXL_DRAW_FILL,
	QXL_DRAW_OPAQUE,
	QXL_DRAW_COPY,
	QXL_COPY_BITS,
	QXL_DRAW_BLEND,
	QXL_DRAW_BLACKNESS,
	QXL_DRAW_WHITENESS,
	QXL_DRAW_INVERS,
	QXL_DRAW_ROP3,
	QXL_DRAW_STROKE,
	QXL_DRAW_TEXT,
	QXL_DRAW_TRANSPARENT,
	QXL_DRAW_ALPHA_BLEND,
	QXL_DRAW_COMPOSITE
};

struct qxl_raster_glyph {
	struct qxl_point render_pos;
	struct qxl_point glyph_origin;
	uint16_t width;
	uint16_t height;
	uint8_t data[0];
};

struct qxl_string {
	uint32_t data_size;
	uint16_t length;
	uint16_t flags;
	struct qxl_data_chunk chunk;
};

struct qxl_copy_bits {
	struct qxl_point src_pos;
};

enum qxl_effect_type {
	QXL_EFFECT_BLEND = 0,
	QXL_EFFECT_OPAQUE = 1,
	QXL_EFFECT_REVERT_ON_DUP = 2,
	QXL_EFFECT_BLACKNESS_ON_DUP = 3,
	QXL_EFFECT_WHITENESS_ON_DUP = 4,
	QXL_EFFECT_NOP_ON_DUP = 5,
	QXL_EFFECT_NOP = 6,
	QXL_EFFECT_OPAQUE_BRUSH = 7
};

struct qxl_pattern {
	QXLPHYSICAL pat;
	struct qxl_point pos;
};

struct qxl_brush {
	uint32_t type;
	union {
		uint32_t color;
		struct qxl_pattern pattern;
	} u;
};

struct qxl_q_mask {
	uint8_t flags;
	struct qxl_point pos;
	QXLPHYSICAL bitmap;
};

struct qxl_fill {
	struct qxl_brush brush;
	uint16_t rop_descriptor;
	struct qxl_q_mask mask;
};

struct qxl_opaque {
	QXLPHYSICAL src_bitmap;
	struct qxl_rect src_area;
	struct qxl_brush brush;
	uint16_t rop_descriptor;
	uint8_t scale_mode;
	struct qxl_q_mask mask;
};

struct qxl_copy {
	QXLPHYSICAL src_bitmap;
	struct qxl_rect src_area;
	uint16_t rop_descriptor;
	uint8_t scale_mode;
	struct qxl_q_mask mask;
};

struct qxl_transparent {
	QXLPHYSICAL src_bitmap;
	struct qxl_rect src_area;
	uint32_t src_color;
	uint32_t true_color;
};

struct qxl_alpha_blend {
	uint16_t alpha_flags;
	uint8_t alpha;
	QXLPHYSICAL src_bitmap;
	struct qxl_rect src_area;
};

struct qxl_compat_alpha_blend {
	uint8_t alpha;
	QXLPHYSICAL src_bitmap;
	struct qxl_rect src_area;
};

struct qxl_rop_3 {
	QXLPHYSICAL src_bitmap;
	struct qxl_rect src_area;
	struct qxl_brush brush;
	uint8_t rop3;
	uint8_t scale_mode;
	struct qxl_q_mask mask;
};

struct qxl_line_attr {
	uint8_t flags;
	uint8_t join_style;
	uint8_t end_style;
	uint8_t style_nseg;
	QXLFIXED width;
	QXLFIXED miter_limit;
	QXLPHYSICAL style;
};

struct qxl_stroke {
	QXLPHYSICAL path;
	struct qxl_line_attr attr;
	struct qxl_brush brush;
	uint16_t fore_mode;
	uint16_t back_mode;
};

struct qxl_text {
	QXLPHYSICAL str;
	struct qxl_rect back_area;
	struct qxl_brush fore_brush;
	struct qxl_brush back_brush;
	uint16_t fore_mode;
	uint16_t back_mode;
};

struct qxl_mask {
	struct qxl_q_mask mask;
};

struct qxl_clip {
	uint32_t type;
	QXLPHYSICAL data;
};

enum qxl_operator {
	QXL_OP_CLEAR			 = 0x00,
	QXL_OP_SOURCE			 = 0x01,
	QXL_OP_DST			 = 0x02,
	QXL_OP_OVER			 = 0x03,
	QXL_OP_OVER_REVERSE		 = 0x04,
	QXL_OP_IN			 = 0x05,
	QXL_OP_IN_REVERSE		 = 0x06,
	QXL_OP_OUT			 = 0x07,
	QXL_OP_OUT_REVERSE		 = 0x08,
	QXL_OP_ATOP			 = 0x09,
	QXL_OP_ATOP_REVERSE		 = 0x0a,
	QXL_OP_XOR			 = 0x0b,
	QXL_OP_ADD			 = 0x0c,
	QXL_OP_SATURATE			 = 0x0d,
	/* Note the jump here from 0x0d to 0x30 */
	QXL_OP_MULTIPLY			 = 0x30,
	QXL_OP_SCREEN			 = 0x31,
	QXL_OP_OVERLAY			 = 0x32,
	QXL_OP_DARKEN			 = 0x33,
	QXL_OP_LIGHTEN			 = 0x34,
	QXL_OP_COLOR_DODGE		 = 0x35,
	QXL_OP_COLOR_BURN		 = 0x36,
	QXL_OP_HARD_LIGHT		 = 0x37,
	QXL_OP_SOFT_LIGHT		 = 0x38,
	QXL_OP_DIFFERENCE		 = 0x39,
	QXL_OP_EXCLUSION		 = 0x3a,
	QXL_OP_HSL_HUE			 = 0x3b,
	QXL_OP_HSL_SATURATION		 = 0x3c,
	QXL_OP_HSL_COLOR		 = 0x3d,
	QXL_OP_HSL_LUMINOSITY		 = 0x3e
};

struct qxl_transform {
	uint32_t	t00;
	uint32_t	t01;
	uint32_t	t02;
	uint32_t	t10;
	uint32_t	t11;
	uint32_t	t12;
};

/* The flags field has the following bit fields:
 *
 *     operator:		[  0 -  7 ]
 *     src_filter:		[  8 - 10 ]
 *     mask_filter:		[ 11 - 13 ]
 *     src_repeat:		[ 14 - 15 ]
 *     mask_repeat:		[ 16 - 17 ]
 *     component_alpha:		[ 18 - 18 ]
 *     reserved:		[ 19 - 31 ]
 *
 * The repeat and filter values are those of pixman:
 *		REPEAT_NONE =		0
 *              REPEAT_NORMAL =		1
 *		REPEAT_PAD =		2
 *		REPEAT_REFLECT =	3
 *
 * The filter values are:
 *		FILTER_NEAREST =	0
 *		FILTER_BILINEAR	=	1
 */
struct qxl_composite {
	uint32_t		flags;

	QXLPHYSICAL			src;
	QXLPHYSICAL			src_transform;	/* May be NULL */
	QXLPHYSICAL			mask;		/* May be NULL */
	QXLPHYSICAL			mask_transform;	/* May be NULL */
	struct qxl_point_1_6	src_origin;
	struct qxl_point_1_6	mask_origin;
};

struct qxl_compat_drawable {
	union qxl_release_info release_info;
	uint8_t effect;
	uint8_t type;
	uint16_t bitmap_offset;
	struct qxl_rect bitmap_area;
	struct qxl_rect bbox;
	struct qxl_clip clip;
	uint32_t mm_time;
	union {
		struct qxl_fill fill;
		struct qxl_opaque opaque;
		struct qxl_copy copy;
		struct qxl_transparent transparent;
		struct qxl_compat_alpha_blend alpha_blend;
		struct qxl_copy_bits copy_bits;
		struct qxl_copy blend;
		struct qxl_rop_3 rop3;
		struct qxl_stroke stroke;
		struct qxl_text text;
		struct qxl_mask blackness;
		struct qxl_mask invers;
		struct qxl_mask whiteness;
	} u;
};

struct qxl_drawable {
	union qxl_release_info release_info;
	uint32_t surface_id;
	uint8_t effect;
	uint8_t type;
	uint8_t self_bitmap;
	struct qxl_rect self_bitmap_area;
	struct qxl_rect bbox;
	struct qxl_clip clip;
	uint32_t mm_time;
	int32_t surfaces_dest[3];
	struct qxl_rect surfaces_rects[3];
	union {
		struct qxl_fill fill;
		struct qxl_opaque opaque;
		struct qxl_copy copy;
		struct qxl_transparent transparent;
		struct qxl_alpha_blend alpha_blend;
		struct qxl_copy_bits copy_bits;
		struct qxl_copy blend;
		struct qxl_rop_3 rop3;
		struct qxl_stroke stroke;
		struct qxl_text text;
		struct qxl_mask blackness;
		struct qxl_mask invers;
		struct qxl_mask whiteness;
		struct qxl_composite composite;
	} u;
};

enum qxl_surface_cmd_type {
	QXL_SURFACE_CMD_CREATE,
	QXL_SURFACE_CMD_DESTROY,
};

struct qxl_surface {
	uint32_t format;
	uint32_t width;
	uint32_t height;
	int32_t stride;
	QXLPHYSICAL data;
};

struct qxl_surface_cmd {
	union qxl_release_info release_info;
	uint32_t surface_id;
	uint8_t type;
	uint32_t flags;
	union {
		struct qxl_surface surface_create;
	} u;
};

struct qxl_clip_rects {
	uint32_t num_rects;
	struct qxl_data_chunk chunk;
};

enum {
	QXL_PATH_BEGIN = (1 << 0),
	QXL_PATH_END = (1 << 1),
	QXL_PATH_CLOSE = (1 << 3),
	QXL_PATH_BEZIER = (1 << 4),
};

struct qxl_path_seg {
	uint32_t flags;
	uint32_t count;
	struct qxl_point_fix points[0];
};

struct qxl_path {
	uint32_t data_size;
	struct qxl_data_chunk chunk;
};

enum {
	QXL_IMAGE_GROUP_DRIVER,
	QXL_IMAGE_GROUP_DEVICE,
	QXL_IMAGE_GROUP_RED,
	QXL_IMAGE_GROUP_DRIVER_DONT_CACHE,
};

struct qxl_image_id {
	uint32_t group;
	uint32_t unique;
};

union qxl_image_id_union {
	struct qxl_image_id id;
	uint64_t value;
};

enum qxl_image_flags {
	QXL_IMAGE_CACHE = (1 << 0),
	QXL_IMAGE_HIGH_BITS_SET = (1 << 1),
};

enum qxl_bitmap_flags {
	QXL_BITMAP_DIRECT = (1 << 0),
	QXL_BITMAP_UNSTABLE = (1 << 1),
	QXL_BITMAP_TOP_DOWN = (1 << 2), /* == SPICE_BITMAP_FLAGS_TOP_DOWN */
};

#define QXL_SET_IMAGE_ID(image, _group, _unique) {              \
	(image)->descriptor.id = (((uint64_t)_unique) << 32) | _group;	\
}

struct qxl_image_descriptor {
	uint64_t id;
	uint8_t type;
	uint8_t flags;
	uint32_t width;
	uint32_t height;
};

struct qxl_palette {
	uint64_t unique;
	uint16_t num_ents;
	uint32_t ents[0];
};

struct qxl_bitmap {
	uint8_t format;
	uint8_t flags;
	uint32_t x;
	uint32_t y;
	uint32_t stride;
	QXLPHYSICAL palette;
	QXLPHYSICAL data; /* data[0] ? */
};

struct qxl_surface_id {
	uint32_t surface_id;
};

struct qxl_encoder_data {
	uint32_t data_size;
	uint8_t data[0];
};

struct qxl_image {
	struct qxl_image_descriptor descriptor;
	union { /* variable length */
		struct qxl_bitmap bitmap;
		struct qxl_encoder_data quic;
		struct qxl_surface_id surface_image;
	} u;
};

/* A QXLHead is a single monitor output backed by a QXLSurface.
 * x and y offsets are unsigned since they are used in relation to
 * the given surface, not the same as the x, y coordinates in the guest
 * screen reference frame. */
struct qxl_head {
	uint32_t id;
	uint32_t surface_id;
	uint32_t width;
	uint32_t height;
	uint32_t x;
	uint32_t y;
	uint32_t flags;
};

struct qxl_monitors_config {
	uint16_t count;
	uint16_t max_allowed; /* If it is 0 no fixed limit is given by the
				 driver */
	struct qxl_head heads[0];
};

#pragma pack(pop)

#endif /* _H_QXL_DEV */
