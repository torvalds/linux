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
#include <nvif/outp.h>
#include <nvif/disp.h>
#include <nvif/printf.h>

#include <nvif/class.h>
#include <nvif/if0012.h>

int
nvif_outp_load_detect(struct nvif_outp *outp, u32 loadval)
{
	struct nvif_outp_load_detect_v0 args;
	int ret;

	args.version = 0;
	args.data = loadval;

	ret = nvif_mthd(&outp->object, NVIF_OUTP_V0_LOAD_DETECT, &args, sizeof(args));
	NVIF_ERRON(ret, &outp->object, "[LOAD_DETECT data:%08x] load:%02x", args.data, args.load);
	return ret < 0 ? ret : args.load;
}

void
nvif_outp_dtor(struct nvif_outp *outp)
{
	nvif_object_dtor(&outp->object);
}

int
nvif_outp_ctor(struct nvif_disp *disp, const char *name, int id, struct nvif_outp *outp)
{
	struct nvif_outp_v0 args;
	int ret;

	args.version = 0;
	args.id = id;

	ret = nvif_object_ctor(&disp->object, name ?: "nvifOutp", id, NVIF_CLASS_OUTP,
			       &args, sizeof(args), &outp->object);
	NVIF_ERRON(ret, &disp->object, "[NEW outp id:%d]", id);
	return ret;
}
