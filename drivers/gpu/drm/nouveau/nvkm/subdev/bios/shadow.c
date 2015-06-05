/*
 * Copyright 2014 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
#include "priv.h"

#include <core/device.h>
#include <core/option.h>
#include <subdev/bios.h>
#include <subdev/bios/image.h>

struct shadow {
	struct nvkm_oclass base;
	u32 skip;
	const struct nvbios_source *func;
	void *data;
	u32 size;
	int score;
};

static bool
shadow_fetch(struct nvkm_bios *bios, u32 upto)
{
	struct shadow *mthd = (void *)nv_object(bios)->oclass;
	const u32 limit = (upto + 3) & ~3;
	const u32 start = bios->size;
	void *data = mthd->data;
	if (nvbios_extend(bios, limit) > 0) {
		u32 read = mthd->func->read(data, start, limit - start, bios);
		bios->size = start + read;
	}
	return bios->size >= limit;
}

static u8
shadow_rd08(struct nvkm_object *object, u64 addr)
{
	struct nvkm_bios *bios = (void *)object;
	if (shadow_fetch(bios, addr + 1))
		return bios->data[addr];
	return 0x00;
}

static u16
shadow_rd16(struct nvkm_object *object, u64 addr)
{
	struct nvkm_bios *bios = (void *)object;
	if (shadow_fetch(bios, addr + 2))
		return get_unaligned_le16(&bios->data[addr]);
	return 0x0000;
}

static u32
shadow_rd32(struct nvkm_object *object, u64 addr)
{
	struct nvkm_bios *bios = (void *)object;
	if (shadow_fetch(bios, addr + 4))
		return get_unaligned_le32(&bios->data[addr]);
	return 0x00000000;
}

static struct nvkm_oclass
shadow_class = {
	.handle = NV_SUBDEV(VBIOS, 0x00),
	.ofuncs = &(struct nvkm_ofuncs) {
		.rd08 = shadow_rd08,
		.rd16 = shadow_rd16,
		.rd32 = shadow_rd32,
	},
};

static int
shadow_image(struct nvkm_bios *bios, int idx, struct shadow *mthd)
{
	struct nvbios_image image;
	int score = 1;

	if (!nvbios_image(bios, idx, &image)) {
		nv_debug(bios, "image %d invalid\n", idx);
		return 0;
	}
	nv_debug(bios, "%08x: type %02x, %d bytes\n",
		 image.base, image.type, image.size);

	if (!shadow_fetch(bios, image.size)) {
		nv_debug(bios, "%08x: fetch failed\n", image.base);
		return 0;
	}

	switch (image.type) {
	case 0x00:
		if (nvbios_checksum(&bios->data[image.base], image.size)) {
			nv_debug(bios, "%08x: checksum failed\n", image.base);
			if (mthd->func->rw)
				score += 1;
			score += 1;
		} else {
			score += 3;
		}
		break;
	default:
		score += 3;
		break;
	}

	if (!image.last)
		score += shadow_image(bios, idx + 1, mthd);
	return score;
}

static int
shadow_score(struct nvkm_bios *bios, struct shadow *mthd)
{
	struct nvkm_oclass *oclass = nv_object(bios)->oclass;
	int score;
	nv_object(bios)->oclass = &mthd->base;
	score = shadow_image(bios, 0, mthd);
	nv_object(bios)->oclass = oclass;
	return score;

}

static int
shadow_method(struct nvkm_bios *bios, struct shadow *mthd, const char *name)
{
	const struct nvbios_source *func = mthd->func;
	if (func->name) {
		nv_debug(bios, "trying %s...\n", name ? name : func->name);
		if (func->init) {
			mthd->data = func->init(bios, name);
			if (IS_ERR(mthd->data)) {
				mthd->data = NULL;
				return 0;
			}
		}
		mthd->score = shadow_score(bios, mthd);
		if (func->fini)
			func->fini(mthd->data);
		nv_debug(bios, "scored %d\n", mthd->score);
		mthd->data = bios->data;
		mthd->size = bios->size;
		bios->data  = NULL;
		bios->size  = 0;
	}
	return mthd->score;
}

static u32
shadow_fw_read(void *data, u32 offset, u32 length, struct nvkm_bios *bios)
{
	const struct firmware *fw = data;
	if (offset + length <= fw->size) {
		memcpy(bios->data + offset, fw->data + offset, length);
		return length;
	}
	return 0;
}

static void *
shadow_fw_init(struct nvkm_bios *bios, const char *name)
{
	struct device *dev = &nv_device(bios)->pdev->dev;
	const struct firmware *fw;
	int ret = request_firmware(&fw, name, dev);
	if (ret)
		return ERR_PTR(-ENOENT);
	return (void *)fw;
}

static const struct nvbios_source
shadow_fw = {
	.name = "firmware",
	.init = shadow_fw_init,
	.fini = (void(*)(void *))release_firmware,
	.read = shadow_fw_read,
	.rw = false,
};

int
nvbios_shadow(struct nvkm_bios *bios)
{
	struct shadow mthds[] = {
		{ shadow_class, 0, &nvbios_of },
		{ shadow_class, 0, &nvbios_ramin },
		{ shadow_class, 0, &nvbios_rom },
		{ shadow_class, 0, &nvbios_acpi_fast },
		{ shadow_class, 4, &nvbios_acpi_slow },
		{ shadow_class, 1, &nvbios_pcirom },
		{ shadow_class, 1, &nvbios_platform },
		{ shadow_class }
	}, *mthd = mthds, *best = NULL;
	const char *optarg;
	char *source;
	int optlen;

	/* handle user-specified bios source */
	optarg = nvkm_stropt(nv_device(bios)->cfgopt, "NvBios", &optlen);
	source = optarg ? kstrndup(optarg, optlen, GFP_KERNEL) : NULL;
	if (source) {
		/* try to match one of the built-in methods */
		for (mthd = mthds; mthd->func; mthd++) {
			if (mthd->func->name &&
			    !strcasecmp(source, mthd->func->name)) {
				best = mthd;
				if (shadow_method(bios, mthd, NULL))
					break;
			}
		}

		/* otherwise, attempt to load as firmware */
		if (!best && (best = mthd)) {
			mthd->func = &shadow_fw;
			shadow_method(bios, mthd, source);
			mthd->func = NULL;
		}

		if (!best->score) {
			nv_error(bios, "%s invalid\n", source);
			kfree(source);
			source = NULL;
		}
	}

	/* scan all potential bios sources, looking for best image */
	if (!best || !best->score) {
		for (mthd = mthds, best = mthd; mthd->func; mthd++) {
			if (!mthd->skip || best->score < mthd->skip) {
				if (shadow_method(bios, mthd, NULL)) {
					if (mthd->score > best->score)
						best = mthd;
				}
			}
		}
	}

	/* cleanup the ones we didn't use */
	for (mthd = mthds; mthd->func; mthd++) {
		if (mthd != best)
			kfree(mthd->data);
	}

	if (!best->score) {
		nv_fatal(bios, "unable to locate usable image\n");
		return -EINVAL;
	}

	nv_info(bios, "using image from %s\n", best->func ?
		best->func->name : source);
	bios->data = best->data;
	bios->size = best->size;
	kfree(source);
	return 0;
}
