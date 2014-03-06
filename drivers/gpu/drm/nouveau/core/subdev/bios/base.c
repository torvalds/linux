/*
 * Copyright 2012 Red Hat Inc.
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
 * Authors: Ben Skeggs
 */

#include <core/object.h>
#include <core/device.h>
#include <core/subdev.h>
#include <core/option.h>

#include <subdev/bios.h>
#include <subdev/bios/bmp.h>
#include <subdev/bios/bit.h>

u8
nvbios_checksum(const u8 *data, int size)
{
	u8 sum = 0;
	while (size--)
		sum += *data++;
	return sum;
}

u16
nvbios_findstr(const u8 *data, int size, const char *str, int len)
{
	int i, j;

	for (i = 0; i <= (size - len); i++) {
		for (j = 0; j < len; j++)
			if ((char)data[i + j] != str[j])
				break;
		if (j == len)
			return i;
	}

	return 0;
}

#if defined(__powerpc__)
static void
nouveau_bios_shadow_of(struct nouveau_bios *bios)
{
	struct pci_dev *pdev = nv_device(bios)->pdev;
	struct device_node *dn;
	const u32 *data;
	int size;

	dn = pci_device_to_OF_node(pdev);
	if (!dn) {
		nv_info(bios, "Unable to get the OF node\n");
		return;
	}

	data = of_get_property(dn, "NVDA,BMP", &size);
	if (data && size) {
		bios->size = size;
		bios->data = kmalloc(bios->size, GFP_KERNEL);
		if (bios->data)
			memcpy(bios->data, data, size);
	}
}
#endif

static void
nouveau_bios_shadow_pramin(struct nouveau_bios *bios)
{
	struct nouveau_device *device = nv_device(bios);
	u64 addr = 0;
	u32 bar0 = 0;
	int i;

	if (device->card_type >= NV_50) {
		if (  device->card_type < NV_C0 ||
		    !(nv_rd32(bios, 0x022500) & 0x00000001))
			addr = (u64)(nv_rd32(bios, 0x619f04) & 0xffffff00) << 8;

		if (!addr) {
			addr  = (u64)nv_rd32(bios, 0x001700) << 16;
			addr += 0xf0000;
		}

		bar0 = nv_mask(bios, 0x001700, 0xffffffff, addr >> 16);
	}

	/* bail if no rom signature */
	if (nv_rd08(bios, 0x700000) != 0x55 ||
	    nv_rd08(bios, 0x700001) != 0xaa)
		goto out;

	bios->size = nv_rd08(bios, 0x700002) * 512;
	if (!bios->size)
		goto out;

	bios->data = kmalloc(bios->size, GFP_KERNEL);
	if (bios->data) {
		for (i = 0; i < bios->size; i++)
			nv_wo08(bios, i, nv_rd08(bios, 0x700000 + i));
	}

out:
	if (device->card_type >= NV_50)
		nv_wr32(bios, 0x001700, bar0);
}

static void
nouveau_bios_shadow_prom(struct nouveau_bios *bios)
{
	struct nouveau_device *device = nv_device(bios);
	u32 pcireg, access;
	u16 pcir;
	int i;

	/* there is no prom on nv4x IGP's */
	if (device->card_type == NV_40 && device->chipset >= 0x4c)
		return;

	/* enable access to rom */
	if (device->card_type >= NV_50)
		pcireg = 0x088050;
	else
		pcireg = 0x001850;
	access = nv_mask(bios, pcireg, 0x00000001, 0x00000000);

	/* bail if no rom signature, with a workaround for a PROM reading
	 * issue on some chipsets.  the first read after a period of
	 * inactivity returns the wrong result, so retry the first header
	 * byte a few times before giving up as a workaround
	 */
	i = 16;
	do {
		if (nv_rd08(bios, 0x300000) == 0x55)
			break;
	} while (i--);

	if (!i || nv_rd08(bios, 0x300001) != 0xaa)
		goto out;

	/* additional check (see note below) - read PCI record header */
	pcir = nv_rd08(bios, 0x300018) |
	       nv_rd08(bios, 0x300019) << 8;
	if (nv_rd08(bios, 0x300000 + pcir) != 'P' ||
	    nv_rd08(bios, 0x300001 + pcir) != 'C' ||
	    nv_rd08(bios, 0x300002 + pcir) != 'I' ||
	    nv_rd08(bios, 0x300003 + pcir) != 'R')
		goto out;

	/* read entire bios image to system memory */
	bios->size = nv_rd08(bios, 0x300002) * 512;
	if (!bios->size)
		goto out;

	bios->data = kmalloc(bios->size, GFP_KERNEL);
	if (bios->data) {
		for (i = 0; i < bios->size; i++)
			nv_wo08(bios, i, nv_rd08(bios, 0x300000 + i));
	}

out:
	/* disable access to rom */
	nv_wr32(bios, pcireg, access);
}

#if defined(CONFIG_ACPI) && defined(CONFIG_X86)
int nouveau_acpi_get_bios_chunk(uint8_t *bios, int offset, int len);
bool nouveau_acpi_rom_supported(struct pci_dev *pdev);
#else
static inline bool
nouveau_acpi_rom_supported(struct pci_dev *pdev) {
	return false;
}

static inline int
nouveau_acpi_get_bios_chunk(uint8_t *bios, int offset, int len) {
	return -EINVAL;
}
#endif

static void
nouveau_bios_shadow_acpi(struct nouveau_bios *bios)
{
	struct pci_dev *pdev = nv_device(bios)->pdev;
	int ret, cnt, i;

	if (!nouveau_acpi_rom_supported(pdev)) {
		bios->data = NULL;
		return;
	}

	bios->size = 0;
	bios->data = kmalloc(4096, GFP_KERNEL);
	if (bios->data) {
		if (nouveau_acpi_get_bios_chunk(bios->data, 0, 4096) == 4096)
			bios->size = bios->data[2] * 512;
		kfree(bios->data);
	}

	if (!bios->size)
		return;

	bios->data = kmalloc(bios->size, GFP_KERNEL);
	if (bios->data) {
		/* disobey the acpi spec - much faster on at least w530 ... */
		ret = nouveau_acpi_get_bios_chunk(bios->data, 0, bios->size);
		if (ret != bios->size ||
		    nvbios_checksum(bios->data, bios->size)) {
			/* ... that didn't work, ok, i'll be good now */
			for (i = 0; i < bios->size; i += cnt) {
				cnt = min((bios->size - i), (u32)4096);
				ret = nouveau_acpi_get_bios_chunk(bios->data, i, cnt);
				if (ret != cnt)
					break;
			}
		}
	}
}

static void
nouveau_bios_shadow_pci(struct nouveau_bios *bios)
{
	struct pci_dev *pdev = nv_device(bios)->pdev;
	size_t size;

	if (!pci_enable_rom(pdev)) {
		void __iomem *rom = pci_map_rom(pdev, &size);
		if (rom && size) {
			bios->data = kmalloc(size, GFP_KERNEL);
			if (bios->data) {
				memcpy_fromio(bios->data, rom, size);
				bios->size = size;
			}
		}
		if (rom)
			pci_unmap_rom(pdev, rom);

		pci_disable_rom(pdev);
	}
}

static void
nouveau_bios_shadow_platform(struct nouveau_bios *bios)
{
	struct pci_dev *pdev = nv_device(bios)->pdev;
	size_t size;

	void __iomem *rom = pci_platform_rom(pdev, &size);
	if (rom && size) {
		bios->data = kmalloc(size, GFP_KERNEL);
		if (bios->data) {
			memcpy_fromio(bios->data, rom, size);
			bios->size = size;
		}
	}
}

static int
nouveau_bios_score(struct nouveau_bios *bios, const bool writeable)
{
	if (bios->size < 3 || !bios->data || bios->data[0] != 0x55 ||
			bios->data[1] != 0xAA) {
		nv_info(bios, "... signature not found\n");
		return 0;
	}

	if (nvbios_checksum(bios->data,
			min_t(u32, bios->data[2] * 512, bios->size))) {
		nv_info(bios, "... checksum invalid\n");
		/* if a ro image is somewhat bad, it's probably all rubbish */
		return writeable ? 2 : 1;
	}

	nv_info(bios, "... appears to be valid\n");
	return 3;
}

struct methods {
	const char desc[16];
	void (*shadow)(struct nouveau_bios *);
	const bool rw;
	int score;
	u32 size;
	u8 *data;
};

static int
nouveau_bios_shadow(struct nouveau_bios *bios)
{
	struct methods shadow_methods[] = {
#if defined(__powerpc__)
		{ "OpenFirmware", nouveau_bios_shadow_of, true, 0, 0, NULL },
#endif
		{ "PRAMIN", nouveau_bios_shadow_pramin, true, 0, 0, NULL },
		{ "PROM", nouveau_bios_shadow_prom, false, 0, 0, NULL },
		{ "ACPI", nouveau_bios_shadow_acpi, true, 0, 0, NULL },
		{ "PCIROM", nouveau_bios_shadow_pci, true, 0, 0, NULL },
		{ "PLATFORM", nouveau_bios_shadow_platform, true, 0, 0, NULL },
		{}
	};
	struct methods *mthd, *best;
	const struct firmware *fw;
	const char *optarg;
	int optlen, ret;
	char *source;

	optarg = nouveau_stropt(nv_device(bios)->cfgopt, "NvBios", &optlen);
	source = optarg ? kstrndup(optarg, optlen, GFP_KERNEL) : NULL;
	if (source) {
		/* try to match one of the built-in methods */
		mthd = shadow_methods;
		do {
			if (strcasecmp(source, mthd->desc))
				continue;
			nv_info(bios, "source: %s\n", mthd->desc);

			mthd->shadow(bios);
			mthd->score = nouveau_bios_score(bios, mthd->rw);
			if (mthd->score) {
				kfree(source);
				return 0;
			}
		} while ((++mthd)->shadow);

		/* attempt to load firmware image */
		ret = request_firmware(&fw, source, &nv_device(bios)->pdev->dev);
		if (ret == 0) {
			bios->size = fw->size;
			bios->data = kmemdup(fw->data, fw->size, GFP_KERNEL);
			release_firmware(fw);

			nv_info(bios, "image: %s\n", source);
			if (nouveau_bios_score(bios, 1)) {
				kfree(source);
				return 0;
			}

			kfree(bios->data);
			bios->data = NULL;
		}

		nv_error(bios, "source \'%s\' invalid\n", source);
		kfree(source);
	}

	mthd = shadow_methods;
	do {
		nv_info(bios, "checking %s for image...\n", mthd->desc);
		mthd->shadow(bios);
		mthd->score = nouveau_bios_score(bios, mthd->rw);
		mthd->size = bios->size;
		mthd->data = bios->data;
		bios->data = NULL;
	} while (mthd->score != 3 && (++mthd)->shadow);

	mthd = shadow_methods;
	best = mthd;
	do {
		if (mthd->score > best->score) {
			kfree(best->data);
			best = mthd;
		}
	} while ((++mthd)->shadow);

	if (best->score) {
		nv_info(bios, "using image from %s\n", best->desc);
		bios->size = best->size;
		bios->data = best->data;
		return 0;
	}

	nv_error(bios, "unable to locate usable image\n");
	return -EINVAL;
}

static u8
nouveau_bios_rd08(struct nouveau_object *object, u64 addr)
{
	struct nouveau_bios *bios = (void *)object;
	return bios->data[addr];
}

static u16
nouveau_bios_rd16(struct nouveau_object *object, u64 addr)
{
	struct nouveau_bios *bios = (void *)object;
	return get_unaligned_le16(&bios->data[addr]);
}

static u32
nouveau_bios_rd32(struct nouveau_object *object, u64 addr)
{
	struct nouveau_bios *bios = (void *)object;
	return get_unaligned_le32(&bios->data[addr]);
}

static void
nouveau_bios_wr08(struct nouveau_object *object, u64 addr, u8 data)
{
	struct nouveau_bios *bios = (void *)object;
	bios->data[addr] = data;
}

static void
nouveau_bios_wr16(struct nouveau_object *object, u64 addr, u16 data)
{
	struct nouveau_bios *bios = (void *)object;
	put_unaligned_le16(data, &bios->data[addr]);
}

static void
nouveau_bios_wr32(struct nouveau_object *object, u64 addr, u32 data)
{
	struct nouveau_bios *bios = (void *)object;
	put_unaligned_le32(data, &bios->data[addr]);
}

static int
nouveau_bios_ctor(struct nouveau_object *parent,
		  struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *data, u32 size,
		  struct nouveau_object **pobject)
{
	struct nouveau_bios *bios;
	struct bit_entry bit_i;
	int ret;

	ret = nouveau_subdev_create(parent, engine, oclass, 0,
				    "VBIOS", "bios", &bios);
	*pobject = nv_object(bios);
	if (ret)
		return ret;

	ret = nouveau_bios_shadow(bios);
	if (ret)
		return ret;

	/* detect type of vbios we're dealing with */
	bios->bmp_offset = nvbios_findstr(bios->data, bios->size,
					  "\xff\x7f""NV\0", 5);
	if (bios->bmp_offset) {
		nv_info(bios, "BMP version %x.%x\n",
			bmp_version(bios) >> 8,
			bmp_version(bios) & 0xff);
	}

	bios->bit_offset = nvbios_findstr(bios->data, bios->size,
					  "\xff\xb8""BIT", 5);
	if (bios->bit_offset)
		nv_info(bios, "BIT signature found\n");

	/* determine the vbios version number */
	if (!bit_entry(bios, 'i', &bit_i) && bit_i.length >= 4) {
		bios->version.major = nv_ro08(bios, bit_i.offset + 3);
		bios->version.chip  = nv_ro08(bios, bit_i.offset + 2);
		bios->version.minor = nv_ro08(bios, bit_i.offset + 1);
		bios->version.micro = nv_ro08(bios, bit_i.offset + 0);
		bios->version.patch = nv_ro08(bios, bit_i.offset + 4);
	} else
	if (bmp_version(bios)) {
		bios->version.major = nv_ro08(bios, bios->bmp_offset + 13);
		bios->version.chip  = nv_ro08(bios, bios->bmp_offset + 12);
		bios->version.minor = nv_ro08(bios, bios->bmp_offset + 11);
		bios->version.micro = nv_ro08(bios, bios->bmp_offset + 10);
	}

	nv_info(bios, "version %02x.%02x.%02x.%02x.%02x\n",
		bios->version.major, bios->version.chip,
		bios->version.minor, bios->version.micro, bios->version.patch);

	return 0;
}

static void
nouveau_bios_dtor(struct nouveau_object *object)
{
	struct nouveau_bios *bios = (void *)object;
	kfree(bios->data);
	nouveau_subdev_destroy(&bios->base);
}

static int
nouveau_bios_init(struct nouveau_object *object)
{
	struct nouveau_bios *bios = (void *)object;
	return nouveau_subdev_init(&bios->base);
}

static int
nouveau_bios_fini(struct nouveau_object *object, bool suspend)
{
	struct nouveau_bios *bios = (void *)object;
	return nouveau_subdev_fini(&bios->base, suspend);
}

struct nouveau_oclass
nouveau_bios_oclass = {
	.handle = NV_SUBDEV(VBIOS, 0x00),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nouveau_bios_ctor,
		.dtor = nouveau_bios_dtor,
		.init = nouveau_bios_init,
		.fini = nouveau_bios_fini,
		.rd08 = nouveau_bios_rd08,
		.rd16 = nouveau_bios_rd16,
		.rd32 = nouveau_bios_rd32,
		.wr08 = nouveau_bios_wr08,
		.wr16 = nouveau_bios_wr16,
		.wr32 = nouveau_bios_wr32,
	},
};
