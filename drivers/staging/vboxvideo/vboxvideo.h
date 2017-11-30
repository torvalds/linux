/*
 * Copyright (C) 2006-2016 Oracle Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 */

#ifndef __VBOXVIDEO_H__
#define __VBOXVIDEO_H__

/*
 * This should be in sync with monitorCount <xsd:maxInclusive value="64"/> in
 * src/VBox/Main/xml/VirtualBox-settings-common.xsd
 */
#define VBOX_VIDEO_MAX_SCREENS 64

/*
 * The last 4096 bytes of the guest VRAM contains the generic info for all
 * DualView chunks: sizes and offsets of chunks. This is filled by miniport.
 *
 * Last 4096 bytes of each chunk contain chunk specific data: framebuffer info,
 * etc. This is used exclusively by the corresponding instance of a display
 * driver.
 *
 * The VRAM layout:
 *   Last 4096 bytes - Adapter information area.
 *   4096 bytes aligned miniport heap (value specified in the config rouded up).
 *   Slack - what left after dividing the VRAM.
 *   4096 bytes aligned framebuffers:
 *     last 4096 bytes of each framebuffer is the display information area.
 *
 * The Virtual Graphics Adapter information in the guest VRAM is stored by the
 * guest video driver using structures prepended by VBOXVIDEOINFOHDR.
 *
 * When the guest driver writes dword 0 to the VBE_DISPI_INDEX_VBOX_VIDEO
 * the host starts to process the info. The first element at the start of
 * the 4096 bytes region should be normally be a LINK that points to
 * actual information chain. That way the guest driver can have some
 * fixed layout of the information memory block and just rewrite
 * the link to point to relevant memory chain.
 *
 * The processing stops at the END element.
 *
 * The host can access the memory only when the port IO is processed.
 * All data that will be needed later must be copied from these 4096 bytes.
 * But other VRAM can be used by host until the mode is disabled.
 *
 * The guest driver writes dword 0xffffffff to the VBE_DISPI_INDEX_VBOX_VIDEO
 * to disable the mode.
 *
 * VBE_DISPI_INDEX_VBOX_VIDEO is used to read the configuration information
 * from the host and issue commands to the host.
 *
 * The guest writes the VBE_DISPI_INDEX_VBOX_VIDEO index register, the the
 * following operations with the VBE data register can be performed:
 *
 * Operation            Result
 * write 16 bit value   NOP
 * read 16 bit value    count of monitors
 * write 32 bit value   set the vbox cmd value and the cmd processed by the host
 * read 32 bit value    result of the last vbox command is returned
 */

/**
 * VBVA command header.
 *
 * @todo Where does this fit in?
 */
struct vbva_cmd_hdr {
   /** Coordinates of affected rectangle. */
	s16 x;
	s16 y;
	u16 w;
	u16 h;
} __packed;

/** @name VBVA ring defines.
 *
 * The VBVA ring buffer is suitable for transferring large (< 2GB) amount of
 * data. For example big bitmaps which do not fit to the buffer.
 *
 * Guest starts writing to the buffer by initializing a record entry in the
 * records queue. VBVA_F_RECORD_PARTIAL indicates that the record is being
 * written. As data is written to the ring buffer, the guest increases
 * free_offset.
 *
 * The host reads the records on flushes and processes all completed records.
 * When host encounters situation when only a partial record presents and
 * len_and_flags & ~VBVA_F_RECORD_PARTIAL >= VBVA_RING_BUFFER_SIZE -
 * VBVA_RING_BUFFER_THRESHOLD, the host fetched all record data and updates
 * data_offset. After that on each flush the host continues fetching the data
 * until the record is completed.
 *
 */
#define VBVA_RING_BUFFER_SIZE        (4194304 - 1024)
#define VBVA_RING_BUFFER_THRESHOLD   (4096)

#define VBVA_MAX_RECORDS (64)

#define VBVA_F_MODE_ENABLED         0x00000001u
#define VBVA_F_MODE_VRDP            0x00000002u
#define VBVA_F_MODE_VRDP_RESET      0x00000004u
#define VBVA_F_MODE_VRDP_ORDER_MASK 0x00000008u

#define VBVA_F_STATE_PROCESSING     0x00010000u

#define VBVA_F_RECORD_PARTIAL       0x80000000u

/**
 * VBVA record.
 */
struct vbva_record {
	/** The length of the record. Changed by guest. */
	u32 len_and_flags;
} __packed;

/*
 * The minimum HGSMI heap size is PAGE_SIZE (4096 bytes) and is a restriction of
 * the runtime heapsimple API. Use minimum 2 pages here, because the info area
 * also may contain other data (for example hgsmi_host_flags structure).
 */
#define VBVA_ADAPTER_INFORMATION_SIZE 65536
#define VBVA_MIN_BUFFER_SIZE          65536

/* The value for port IO to let the adapter to interpret the adapter memory. */
#define VBOX_VIDEO_DISABLE_ADAPTER_MEMORY        0xFFFFFFFF

/* The value for port IO to let the adapter to interpret the adapter memory. */
#define VBOX_VIDEO_INTERPRET_ADAPTER_MEMORY      0x00000000

/* The value for port IO to let the adapter to interpret the display memory.
 * The display number is encoded in low 16 bits.
 */
#define VBOX_VIDEO_INTERPRET_DISPLAY_MEMORY_BASE 0x00010000

struct vbva_host_flags {
	u32 host_events;
	u32 supported_orders;
} __packed;

struct vbva_buffer {
	struct vbva_host_flags host_flags;

	/* The offset where the data start in the buffer. */
	u32 data_offset;
	/* The offset where next data must be placed in the buffer. */
	u32 free_offset;

	/* The queue of record descriptions. */
	struct vbva_record records[VBVA_MAX_RECORDS];
	u32 record_first_index;
	u32 record_free_index;

	/* Space to leave free when large partial records are transferred. */
	u32 partial_write_tresh;

	u32 data_len;
	/* variable size for the rest of the vbva_buffer area in VRAM. */
	u8 data[0];
} __packed;

#define VBVA_MAX_RECORD_SIZE (128 * 1024 * 1024)

/* guest->host commands */
#define VBVA_QUERY_CONF32			 1
#define VBVA_SET_CONF32				 2
#define VBVA_INFO_VIEW				 3
#define VBVA_INFO_HEAP				 4
#define VBVA_FLUSH				 5
#define VBVA_INFO_SCREEN			 6
#define VBVA_ENABLE				 7
#define VBVA_MOUSE_POINTER_SHAPE		 8
/* informs host about HGSMI caps. see vbva_caps below */
#define VBVA_INFO_CAPS				12
/* configures scanline, see VBVASCANLINECFG below */
#define VBVA_SCANLINE_CFG			13
/* requests scanline info, see VBVASCANLINEINFO below */
#define VBVA_SCANLINE_INFO			14
/* inform host about VBVA Command submission */
#define VBVA_CMDVBVA_SUBMIT			16
/* inform host about VBVA Command submission */
#define VBVA_CMDVBVA_FLUSH			17
/* G->H DMA command */
#define VBVA_CMDVBVA_CTL			18
/* Query most recent mode hints sent */
#define VBVA_QUERY_MODE_HINTS			19
/**
 * Report the guest virtual desktop position and size for mapping host and
 * guest pointer positions.
 */
#define VBVA_REPORT_INPUT_MAPPING		20
/** Report the guest cursor position and query the host position. */
#define VBVA_CURSOR_POSITION			21

/* host->guest commands */
#define VBVAHG_EVENT				1
#define VBVAHG_DISPLAY_CUSTOM			2

/* vbva_conf32::index */
#define VBOX_VBVA_CONF32_MONITOR_COUNT		0
#define VBOX_VBVA_CONF32_HOST_HEAP_SIZE		1
/**
 * Returns VINF_SUCCESS if the host can report mode hints via VBVA.
 * Set value to VERR_NOT_SUPPORTED before calling.
 */
#define VBOX_VBVA_CONF32_MODE_HINT_REPORTING	2
/**
 * Returns VINF_SUCCESS if the host can report guest cursor enabled status via
 * VBVA.  Set value to VERR_NOT_SUPPORTED before calling.
 */
#define VBOX_VBVA_CONF32_GUEST_CURSOR_REPORTING	3
/**
 * Returns the currently available host cursor capabilities.  Available if
 * vbva_conf32::VBOX_VBVA_CONF32_GUEST_CURSOR_REPORTING returns success.
 * @see VMMDevReqMouseStatus::mouseFeatures.
 */
#define VBOX_VBVA_CONF32_CURSOR_CAPABILITIES	4
/** Returns the supported flags in vbva_infoscreen::flags. */
#define VBOX_VBVA_CONF32_SCREEN_FLAGS		5
/** Returns the max size of VBVA record. */
#define VBOX_VBVA_CONF32_MAX_RECORD_SIZE	6

struct vbva_conf32 {
	u32 index;
	u32 value;
} __packed;

/** Reserved for historical reasons. */
#define VBOX_VBVA_CURSOR_CAPABILITY_RESERVED0   BIT(0)
/**
 * Guest cursor capability: can the host show a hardware cursor at the host
 * pointer location?
 */
#define VBOX_VBVA_CURSOR_CAPABILITY_HARDWARE    BIT(1)
/** Reserved for historical reasons. */
#define VBOX_VBVA_CURSOR_CAPABILITY_RESERVED2   BIT(2)
/** Reserved for historical reasons.  Must always be unset. */
#define VBOX_VBVA_CURSOR_CAPABILITY_RESERVED3   BIT(3)
/** Reserved for historical reasons. */
#define VBOX_VBVA_CURSOR_CAPABILITY_RESERVED4   BIT(4)
/** Reserved for historical reasons. */
#define VBOX_VBVA_CURSOR_CAPABILITY_RESERVED5   BIT(5)

struct vbva_infoview {
	/* Index of the screen, assigned by the guest. */
	u32 view_index;

	/* The screen offset in VRAM, the framebuffer starts here. */
	u32 view_offset;

	/* The size of the VRAM memory that can be used for the view. */
	u32 view_size;

	/* The recommended maximum size of the VRAM memory for the screen. */
	u32 max_screen_size;
} __packed;

struct vbva_flush {
	u32 reserved;
} __packed;

/* vbva_infoscreen::flags */
#define VBVA_SCREEN_F_NONE			0x0000
#define VBVA_SCREEN_F_ACTIVE			0x0001
/**
 * The virtual monitor has been disabled by the guest and should be removed
 * by the host and ignored for purposes of pointer position calculation.
 */
#define VBVA_SCREEN_F_DISABLED			0x0002
/**
 * The virtual monitor has been blanked by the guest and should be blacked
 * out by the host using width, height, etc values from the vbva_infoscreen
 * request.
 */
#define VBVA_SCREEN_F_BLANK			0x0004
/**
 * The virtual monitor has been blanked by the guest and should be blacked
 * out by the host using the previous mode values for width. height, etc.
 */
#define VBVA_SCREEN_F_BLANK2			0x0008

struct vbva_infoscreen {
	/* Which view contains the screen. */
	u32 view_index;

	/* Physical X origin relative to the primary screen. */
	s32 origin_x;

	/* Physical Y origin relative to the primary screen. */
	s32 origin_y;

	/* Offset of visible framebuffer relative to the framebuffer start. */
	u32 start_offset;

	/* The scan line size in bytes. */
	u32 line_size;

	/* Width of the screen. */
	u32 width;

	/* Height of the screen. */
	u32 height;

	/* Color depth. */
	u16 bits_per_pixel;

	/* VBVA_SCREEN_F_* */
	u16 flags;
} __packed;

/* vbva_enable::flags */
#define VBVA_F_NONE				0x00000000
#define VBVA_F_ENABLE				0x00000001
#define VBVA_F_DISABLE				0x00000002
/* extended VBVA to be used with WDDM */
#define VBVA_F_EXTENDED				0x00000004
/* vbva offset is absolute VRAM offset */
#define VBVA_F_ABSOFFSET			0x00000008

struct vbva_enable {
	u32 flags;
	u32 offset;
	s32 result;
} __packed;

struct vbva_enable_ex {
	struct vbva_enable base;
	u32 screen_id;
} __packed;

struct vbva_mouse_pointer_shape {
	/* The host result. */
	s32 result;

	/* VBOX_MOUSE_POINTER_* bit flags. */
	u32 flags;

	/* X coordinate of the hot spot. */
	u32 hot_X;

	/* Y coordinate of the hot spot. */
	u32 hot_y;

	/* Width of the pointer in pixels. */
	u32 width;

	/* Height of the pointer in scanlines. */
	u32 height;

	/* Pointer data.
	 *
	 ****
	 * The data consists of 1 bpp AND mask followed by 32 bpp XOR (color)
	 * mask.
	 *
	 * For pointers without alpha channel the XOR mask pixels are 32 bit
	 * values: (lsb)BGR0(msb). For pointers with alpha channel the XOR mask
	 * consists of (lsb)BGRA(msb) 32 bit values.
	 *
	 * Guest driver must create the AND mask for pointers with alpha chan.,
	 * so if host does not support alpha, the pointer could be displayed as
	 * a normal color pointer. The AND mask can be constructed from alpha
	 * values. For example alpha value >= 0xf0 means bit 0 in the AND mask.
	 *
	 * The AND mask is 1 bpp bitmap with byte aligned scanlines. Size of AND
	 * mask, therefore, is and_len = (width + 7) / 8 * height. The padding
	 * bits at the end of any scanline are undefined.
	 *
	 * The XOR mask follows the AND mask on the next 4 bytes aligned offset:
	 * u8 *xor = and + (and_len + 3) & ~3
	 * Bytes in the gap between the AND and the XOR mask are undefined.
	 * XOR mask scanlines have no gap between them and size of XOR mask is:
	 * xor_len = width * 4 * height.
	 ****
	 *
	 * Preallocate 4 bytes for accessing actual data as p->data.
	 */
	u8 data[4];
} __packed;

/**
 * @name vbva_mouse_pointer_shape::flags
 * @note The VBOX_MOUSE_POINTER_* flags are used in the guest video driver,
 *       values must be <= 0x8000 and must not be changed. (try make more sense
 *       of this, please).
 * @{
 */

/** pointer is visible */
#define VBOX_MOUSE_POINTER_VISIBLE		0x0001
/** pointer has alpha channel */
#define VBOX_MOUSE_POINTER_ALPHA		0x0002
/** pointerData contains new pointer shape */
#define VBOX_MOUSE_POINTER_SHAPE		0x0004

/** @} */

/*
 * The guest driver can handle asynch guest cmd completion by reading the
 * command offset from io port.
 */
#define VBVACAPS_COMPLETEGCMD_BY_IOREAD		0x00000001
/* the guest driver can handle video adapter IRQs */
#define VBVACAPS_IRQ				0x00000002
/** The guest can read video mode hints sent via VBVA. */
#define VBVACAPS_VIDEO_MODE_HINTS		0x00000004
/** The guest can switch to a software cursor on demand. */
#define VBVACAPS_DISABLE_CURSOR_INTEGRATION	0x00000008
/** The guest does not depend on host handling the VBE registers. */
#define VBVACAPS_USE_VBVA_ONLY			0x00000010

struct vbva_caps {
	s32 rc;
	u32 caps;
} __packed;

/** Query the most recent mode hints received from the host. */
struct vbva_query_mode_hints {
	/** The maximum number of screens to return hints for. */
	u16 hints_queried_count;
	/** The size of the mode hint structures directly following this one. */
	u16 hint_structure_guest_size;
	/** Return code for the operation. Initialise to VERR_NOT_SUPPORTED. */
	s32 rc;
} __packed;

/**
 * Structure in which a mode hint is returned. The guest allocates an array
 * of these immediately after the vbva_query_mode_hints structure.
 * To accommodate future extensions, the vbva_query_mode_hints structure
 * specifies the size of the vbva_modehint structures allocated by the guest,
 * and the host only fills out structure elements which fit into that size. The
 * host should fill any unused members (e.g. dx, dy) or structure space on the
 * end with ~0. The whole structure can legally be set to ~0 to skip a screen.
 */
struct vbva_modehint {
	u32 magic;
	u32 cx;
	u32 cy;
	u32 bpp;		/* Which has never been used... */
	u32 display;
	u32 dx;			/**< X offset into the virtual frame-buffer. */
	u32 dy;			/**< Y offset into the virtual frame-buffer. */
	u32 enabled;		/* Not flags. Add new members for new flags. */
} __packed;

#define VBVAMODEHINT_MAGIC 0x0801add9u

/**
 * Report the rectangle relative to which absolute pointer events should be
 * expressed. This information remains valid until the next VBVA resize event
 * for any screen, at which time it is reset to the bounding rectangle of all
 * virtual screens and must be re-set.
 * @see VBVA_REPORT_INPUT_MAPPING.
 */
struct vbva_report_input_mapping {
	s32 x;	/**< Upper left X co-ordinate relative to the first screen. */
	s32 y;	/**< Upper left Y co-ordinate relative to the first screen. */
	u32 cx;	/**< Rectangle width. */
	u32 cy;	/**< Rectangle height. */
} __packed;

/**
 * Report the guest cursor position and query the host one. The host may wish
 * to use the guest information to re-position its own cursor (though this is
 * currently unlikely).
 * @see VBVA_CURSOR_POSITION
 */
struct vbva_cursor_position {
	u32 report_position;	/**< Are we reporting a position? */
	u32 x;			/**< Guest cursor X position */
	u32 y;			/**< Guest cursor Y position */
} __packed;

#endif
