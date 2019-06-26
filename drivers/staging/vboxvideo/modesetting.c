// SPDX-License-Identifier: MIT
/* Copyright (C) 2006-2017 Oracle Corporation */

#include <linux/vbox_err.h>
#include "vbox_drv.h"
#include "vboxvideo_guest.h"
#include "vboxvideo_vbe.h"
#include "hgsmi_channels.h"

/**
 * Set a video mode via an HGSMI request.  The views must have been
 * initialised first using @a VBoxHGSMISendViewInfo and if the mode is being
 * set on the first display then it must be set first using registers.
 * @ctx:           The context containing the heap to use.
 * @display:       The screen number.
 * @origin_x:      The horizontal displacement relative to the first scrn.
 * @origin_y:      The vertical displacement relative to the first screen.
 * @start_offset:  The offset of the visible area of the framebuffer
 *                 relative to the framebuffer start.
 * @pitch:         The offset in bytes between the starts of two adjecent
 *                 scan lines in video RAM.
 * @width:         The mode width.
 * @height:        The mode height.
 * @bpp:           The colour depth of the mode.
 * @flags:         Flags.
 */
void hgsmi_process_display_info(struct gen_pool *ctx, u32 display,
				s32 origin_x, s32 origin_y, u32 start_offset,
				u32 pitch, u32 width, u32 height,
				u16 bpp, u16 flags)
{
	struct vbva_infoscreen *p;

	p = hgsmi_buffer_alloc(ctx, sizeof(*p), HGSMI_CH_VBVA,
			       VBVA_INFO_SCREEN);
	if (!p)
		return;

	p->view_index = display;
	p->origin_x = origin_x;
	p->origin_y = origin_y;
	p->start_offset = start_offset;
	p->line_size = pitch;
	p->width = width;
	p->height = height;
	p->bits_per_pixel = bpp;
	p->flags = flags;

	hgsmi_buffer_submit(ctx, p);
	hgsmi_buffer_free(ctx, p);
}

/**
 * Report the rectangle relative to which absolute pointer events should be
 * expressed.  This information remains valid until the next VBVA resize event
 * for any screen, at which time it is reset to the bounding rectangle of all
 * virtual screens.
 * Return: 0 or negative errno value.
 * @ctx:       The context containing the heap to use.
 * @origin_x:  Upper left X co-ordinate relative to the first screen.
 * @origin_y:  Upper left Y co-ordinate relative to the first screen.
 * @width:     Rectangle width.
 * @height:    Rectangle height.
 */
int hgsmi_update_input_mapping(struct gen_pool *ctx, s32 origin_x, s32 origin_y,
			       u32 width, u32 height)
{
	struct vbva_report_input_mapping *p;

	p = hgsmi_buffer_alloc(ctx, sizeof(*p), HGSMI_CH_VBVA,
			       VBVA_REPORT_INPUT_MAPPING);
	if (!p)
		return -ENOMEM;

	p->x = origin_x;
	p->y = origin_y;
	p->cx = width;
	p->cy = height;

	hgsmi_buffer_submit(ctx, p);
	hgsmi_buffer_free(ctx, p);

	return 0;
}

/**
 * Get most recent video mode hints.
 * Return: 0 or negative errno value.
 * @ctx:      The context containing the heap to use.
 * @screens:  The number of screens to query hints for, starting at 0.
 * @hints:    Array of vbva_modehint structures for receiving the hints.
 */
int hgsmi_get_mode_hints(struct gen_pool *ctx, unsigned int screens,
			 struct vbva_modehint *hints)
{
	struct vbva_query_mode_hints *p;
	size_t size;

	if (WARN_ON(!hints))
		return -EINVAL;

	size = screens * sizeof(struct vbva_modehint);
	p = hgsmi_buffer_alloc(ctx, sizeof(*p) + size, HGSMI_CH_VBVA,
			       VBVA_QUERY_MODE_HINTS);
	if (!p)
		return -ENOMEM;

	p->hints_queried_count = screens;
	p->hint_structure_guest_size = sizeof(struct vbva_modehint);
	p->rc = VERR_NOT_SUPPORTED;

	hgsmi_buffer_submit(ctx, p);

	if (p->rc < 0) {
		hgsmi_buffer_free(ctx, p);
		return -EIO;
	}

	memcpy(hints, ((u8 *)p) + sizeof(struct vbva_query_mode_hints), size);
	hgsmi_buffer_free(ctx, p);

	return 0;
}
