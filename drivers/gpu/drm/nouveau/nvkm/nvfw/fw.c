/*
 * Copyright 2019 Red Hat Inc.
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
#include <core/subdev.h>
#include <nvfw/fw.h>

const struct nvfw_bin_hdr *
nvfw_bin_hdr(struct nvkm_subdev *subdev, const void *data)
{
	const struct nvfw_bin_hdr *hdr = data;
	nvkm_debug(subdev, "binHdr:\n");
	nvkm_debug(subdev, "\tbinMagic         : 0x%08x\n", hdr->bin_magic);
	nvkm_debug(subdev, "\tbinVer           : %d\n", hdr->bin_ver);
	nvkm_debug(subdev, "\tbinSize          : %d\n", hdr->bin_size);
	nvkm_debug(subdev, "\theaderOffset     : 0x%x\n", hdr->header_offset);
	nvkm_debug(subdev, "\tdataOffset       : 0x%x\n", hdr->data_offset);
	nvkm_debug(subdev, "\tdataSize         : 0x%x\n", hdr->data_size);
	return hdr;
}

const struct nvfw_bl_desc *
nvfw_bl_desc(struct nvkm_subdev *subdev, const void *data)
{
	const struct nvfw_bl_desc *hdr = data;
	nvkm_debug(subdev, "blDesc\n");
	nvkm_debug(subdev, "\tstartTag         : 0x%x\n", hdr->start_tag);
	nvkm_debug(subdev, "\tdmemLoadOff      : 0x%x\n", hdr->dmem_load_off);
	nvkm_debug(subdev, "\tcodeOff          : 0x%x\n", hdr->code_off);
	nvkm_debug(subdev, "\tcodeSize         : 0x%x\n", hdr->code_size);
	nvkm_debug(subdev, "\tdataOff          : 0x%x\n", hdr->data_off);
	nvkm_debug(subdev, "\tdataSize         : 0x%x\n", hdr->data_size);
	return hdr;
}
