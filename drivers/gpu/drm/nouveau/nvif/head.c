/*
 * Copyright 2021 Red Hat Inc.
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
 */
#include <nvif/head.h>
#include <nvif/disp.h>
#include <nvif/printf.h>

#include <nvif/class.h>
#include <nvif/if0013.h>

int
nvif_head_vblank_event_ctor(struct nvif_head *head, const char *name, nvif_event_func func,
			    bool wait, struct nvif_event *event)
{
	int ret = nvif_event_ctor(&head->object, name ?: "nvifHeadVBlank", nvif_head_id(head),
				  func, wait, NULL, 0, event);
	NVIF_ERRON(ret, &head->object, "[NEW EVENT:VBLANK]");
	return ret;
}

void
nvif_head_dtor(struct nvif_head *head)
{
	nvif_object_dtor(&head->object);
}

int
nvif_head_ctor(struct nvif_disp *disp, const char *name, int id, struct nvif_head *head)
{
	struct nvif_head_v0 args;
	int ret;

	args.version = 0;
	args.id = id;

	ret = nvif_object_ctor(&disp->object, name ? name : "nvifHead", id, NVIF_CLASS_HEAD,
			       &args, sizeof(args), &head->object);
	NVIF_ERRON(ret, &disp->object, "[NEW head id:%d]", args.id);
	return ret;
}
