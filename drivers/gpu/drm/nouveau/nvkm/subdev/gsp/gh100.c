/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "priv.h"

#include <linux/elf.h>
#include <linux/crc32.h>

#include <subdev/fb.h>
#include <subdev/fsp.h>

#include <rm/r570/nvrm/gsp.h>

#include <nvhw/drf.h>
#include <nvhw/ref/gh100/dev_falcon_v4.h>
#include <nvhw/ref/gh100/dev_riscv_pri.h>

int
gh100_gsp_fini(struct nvkm_gsp *gsp, bool suspend)
{
	struct nvkm_falcon *falcon = &gsp->falcon;
	int ret, time = 4000;

	/* Shutdown RM. */
	ret = r535_gsp_fini(gsp, suspend);
	if (ret && suspend)
		return ret;

	/* Wait for RISC-V to halt. */
	do {
		u32 data = nvkm_falcon_rd32(falcon, falcon->addr2 + NV_PRISCV_RISCV_CPUCTL);

		if (NVVAL_GET(data, NV_PRISCV, RISCV_CPUCTL, HALTED))
			return 0;

		usleep_range(1000, 2000);
	} while(time--);

	return -ETIMEDOUT;
}

static bool
gh100_gsp_lockdown_released(struct nvkm_gsp *gsp, u32 *mbox0)
{
	u32 data;

	/* Wait for GSP access via BAR0 to be allowed. */
	*mbox0 = nvkm_falcon_rd32(&gsp->falcon, NV_PFALCON_FALCON_MAILBOX0);

	if (*mbox0 && (*mbox0 & 0xffffff00) == 0xbadf4100)
		return false;

	/* Check if an error code has been reported. */
	if (*mbox0) {
		u32 mbox1 = nvkm_falcon_rd32(&gsp->falcon, NV_PFALCON_FALCON_MAILBOX1);

		/* Any value that's not GSP_FMC_BOOT_PARAMS addr is an error. */
		if ((((u64)mbox1 << 32) | *mbox0) != gsp->fmc.args.addr)
			return true;
	}

	/* Check if lockdown has been released. */
	data = nvkm_falcon_rd32(&gsp->falcon, NV_PFALCON_FALCON_HWCFG2);
	return !NVVAL_GET(data, NV_PFALCON, FALCON_HWCFG2, RISCV_BR_PRIV_LOCKDOWN);
}

int
gh100_gsp_init(struct nvkm_gsp *gsp)
{
	struct nvkm_subdev *subdev = &gsp->subdev;
	struct nvkm_device *device = subdev->device;
	const bool resume = gsp->sr.meta.data != NULL;
	struct nvkm_gsp_mem *meta;
	GSP_FMC_BOOT_PARAMS *args;
	int ret, time = 4000;
	u32 rsvd_size;
	u32 mbox0;

	if (!resume) {
		ret = nvkm_gsp_mem_ctor(gsp, sizeof(*args), &gsp->fmc.args);
		if (ret)
			return ret;

		meta = &gsp->wpr_meta;
	} else {
		gsp->rm->api->gsp->set_rmargs(gsp, true);
		meta = &gsp->sr.meta;
	}

	args = gsp->fmc.args.data;

	args->bootGspRmParams.gspRmDescOffset = meta->addr;
	args->bootGspRmParams.gspRmDescSize = meta->size;
	args->bootGspRmParams.target = GSP_DMA_TARGET_COHERENT_SYSTEM;
	args->bootGspRmParams.bIsGspRmBoot = 1;

	args->gspRmParams.target = GSP_DMA_TARGET_NONCOHERENT_SYSTEM;
	args->gspRmParams.bootArgsOffset = gsp->libos.addr;

	rsvd_size = gsp->fb.heap.size;
	if (gsp->rm->wpr->rsvd_size_pmu)
		rsvd_size = ALIGN(rsvd_size + gsp->rm->wpr->rsvd_size_pmu, 0x200000);

	ret = nvkm_fsp_boot_gsp_fmc(device->fsp, gsp->fmc.args.addr, rsvd_size, resume,
				    gsp->fmc.fw.addr, gsp->fmc.hash, gsp->fmc.pkey, gsp->fmc.sig);
	if (ret)
		return ret;

	do {
		if (gh100_gsp_lockdown_released(gsp, &mbox0))
			break;

		usleep_range(1000, 2000);
	} while(time--);

	if (time < 0) {
		nvkm_error(subdev, "GSP-FMC boot timed out\n");
		return -ETIMEDOUT;
	}

	if (mbox0) {
		nvkm_error(subdev, "GSP-FMC boot failed (mbox: 0x%08x)\n", mbox0);
		return -EIO;
	}

	return r535_gsp_init(gsp);
}

static int
gh100_gsp_wpr_meta_init(struct nvkm_gsp *gsp)
{
	GspFwWprMeta *meta;
	int ret;

	ret = nvkm_gsp_mem_ctor(gsp, sizeof(*meta), &gsp->wpr_meta);
	if (ret)
		return ret;

	gsp->fb.size = nvkm_fb_vidmem_size(gsp->subdev.device);
	gsp->fb.bios.vga_workspace.size = 128 * 1024;
	gsp->fb.heap.size = gsp->rm->wpr->heap_size_non_wpr;

	meta = gsp->wpr_meta.data;

	meta->magic = GSP_FW_WPR_META_MAGIC;
	meta->revision = GSP_FW_WPR_META_REVISION;

	meta->sizeOfRadix3Elf = gsp->fw.len;
	meta->sysmemAddrOfRadix3Elf = gsp->radix3.lvl0.addr;

	meta->sizeOfBootloader = gsp->boot.fw.size;
	meta->sysmemAddrOfBootloader = gsp->boot.fw.addr;
	meta->bootloaderCodeOffset = gsp->boot.code_offset;
	meta->bootloaderDataOffset = gsp->boot.data_offset;
	meta->bootloaderManifestOffset = gsp->boot.manifest_offset;

	meta->sysmemAddrOfSignature = gsp->sig.addr;
	meta->sizeOfSignature = gsp->sig.size;

	meta->nonWprHeapSize = gsp->fb.heap.size;
	meta->gspFwHeapSize = tu102_gsp_wpr_heap_size(gsp);
	meta->frtsSize = 0x100000;
	meta->vgaWorkspaceSize = gsp->fb.bios.vga_workspace.size;
	meta->pmuReservedSize = gsp->rm->wpr->rsvd_size_pmu;
	return 0;
}

/* The sh_flags value for the binary blobs in the ELF image */
#define FMC_SHF_FLAGS (SHF_MASKPROC | SHF_MASKOS | SHF_OS_NONCONFORMING | SHF_ALLOC)

#define ELF_HDR_SIZE ((u8)sizeof(struct elf32_hdr))
#define ELF_SHDR_SIZE ((u8)sizeof(struct elf32_shdr))

/* The FMC ELF header must be exactly this */
static const u8 elf_header[] = {
	0x7f, 'E', 'L', 'F', 1, 1, 1, 0,
	0, 0, 0, 0, 0, 0, 0, 0,

	0, 0, 0, 0, 1, 0, 0, 0, /* e_type, e_machine, e_version */
	0, 0, 0, 0, 0, 0, 0, 0, /* e_entry, e_phoff */

	ELF_HDR_SIZE, 0, 0, 0, 0, 0, 0, 0, /* e_shoff, e_flags */
	ELF_HDR_SIZE, 0, 0, 0, /* e_ehsize, e_phentsize */
	0, 0, ELF_SHDR_SIZE, 0, /* e_phnum, e_shentsize */

	6, 0, 1, 0, /* e_shnum, e_shstrndx */
};

/**
 * elf_validate_sections - validate each section in the FMC ELF image
 * @elf: ELF image
 * @length: size of the entire ELF image
 */
static bool
elf_validate_sections(const void *elf, size_t length)
{
	const struct elf32_hdr *ehdr = elf;
	const struct elf32_shdr *shdr = elf + ehdr->e_shoff;

	/* The offset of the first section */
	Elf32_Off section_begin = ehdr->e_shoff + ehdr->e_shnum * ehdr->e_shentsize;

	if (section_begin > length)
		return false;

	/* The first section header is the null section, so skip it */
	for (unsigned int i = 1; i < ehdr->e_shnum; i++) {
		if (i == ehdr->e_shstrndx) {
			if (shdr[i].sh_type != SHT_STRTAB)
				return false;
			if (shdr[i].sh_flags != SHF_STRINGS)
				return false;
		} else {
			if (shdr[i].sh_type != SHT_PROGBITS)
				return false;
			if (shdr[i].sh_flags != FMC_SHF_FLAGS)
				return false;
		}

		/* Ensure that each section is inside the image */
		if (shdr[i].sh_offset < section_begin ||
		    (u64)shdr[i].sh_offset + shdr[i].sh_size > length)
			return false;

		/* Non-zero sh_info is a CRC */
		if (shdr[i].sh_info) {
			/* The kernel's CRC32 needs a pre- and post-xor to match standard CRCs */
			u32 crc32 = crc32_le(~0, elf + shdr[i].sh_offset, shdr[i].sh_size) ^ ~0;

			if (shdr[i].sh_info != crc32)
				return false;
		}
	}

	return true;
}

/**
 * elf_section - return a pointer to the data for a given section
 * @elf: ELF image
 * @name: section name to search for
 * @len: pointer to returned length of found section
 */
static const void *
elf_section(const void *elf, const char *name, unsigned int *len)
{
	const struct elf32_hdr *ehdr = elf;
	const struct elf32_shdr *shdr = elf + ehdr->e_shoff;
	const char *names = elf + shdr[ehdr->e_shstrndx].sh_offset;

	for (unsigned int i = 1; i < ehdr->e_shnum; i++) {
		if (!strcmp(&names[shdr[i].sh_name], name)) {
			*len = shdr[i].sh_size;
			return elf + shdr[i].sh_offset;
		}
	}

	return NULL;
}

int
gh100_gsp_oneinit(struct nvkm_gsp *gsp)
{
	struct nvkm_subdev *subdev = &gsp->subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_fsp *fsp = device->fsp;
	const void *fw = gsp->fws.fmc->data;
	const void *hash, *sig, *pkey, *img;
	unsigned int img_len = 0, hash_len = 0, pkey_len = 0, sig_len = 0;
	int ret;

	if (gsp->fws.fmc->size < ELF_HDR_SIZE ||
	    memcmp(fw, elf_header, sizeof(elf_header)) ||
	    !elf_validate_sections(fw, gsp->fws.fmc->size)) {
		nvkm_error(subdev, "fmc firmware image is invalid\n");
		return -ENODATA;
	}

	hash = elf_section(fw, "hash", &hash_len);
	sig = elf_section(fw, "signature", &sig_len);
	pkey = elf_section(fw, "publickey", &pkey_len);
	img = elf_section(fw, "image", &img_len);

	if (!hash || !sig || !pkey || !img) {
		nvkm_error(subdev, "fmc firmware image is invalid\n");
		return -ENODATA;
	}

	if (!nvkm_fsp_verify_gsp_fmc(fsp, hash_len, pkey_len, sig_len))
		return -EINVAL;

	/* Load GSP-FMC FW into memory. */
	ret = nvkm_gsp_mem_ctor(gsp, img_len, &gsp->fmc.fw);
	if (ret)
		return ret;

	memcpy(gsp->fmc.fw.data, img, img_len);

	gsp->fmc.hash = kmemdup(hash, hash_len, GFP_KERNEL);
	gsp->fmc.pkey = kmemdup(pkey, pkey_len, GFP_KERNEL);
	gsp->fmc.sig = kmemdup(sig, sig_len, GFP_KERNEL);
	if (!gsp->fmc.hash || !gsp->fmc.pkey || !gsp->fmc.sig)
		return -ENOMEM;

	ret = r535_gsp_oneinit(gsp);
	if (ret)
		return ret;

	return gh100_gsp_wpr_meta_init(gsp);
}

static const struct nvkm_gsp_func
gh100_gsp = {
	.flcn = &ga102_gsp_flcn,

	.sig_section = ".fwsignature_gh100",

	.dtor = r535_gsp_dtor,
	.oneinit = gh100_gsp_oneinit,
	.init = gh100_gsp_init,
	.fini = gh100_gsp_fini,

	.rm.gpu = &gh100_gpu,
};

int
gh100_gsp_load(struct nvkm_gsp *gsp, int ver, const struct nvkm_gsp_fwif *fwif)
{
	int ret;

	ret = tu102_gsp_load_rm(gsp, fwif);
	if (ret)
		goto done;

	ret = nvkm_gsp_load_fw(gsp, "fmc", fwif->ver, &gsp->fws.fmc);

done:
	if (ret)
		nvkm_gsp_dtor_fws(gsp);

	return ret;
}

static struct nvkm_gsp_fwif
gh100_gsps[] = {
	{ 0, gh100_gsp_load, &gh100_gsp, &r570_rm_gh100, "570.144" },
	{}
};

int
gh100_gsp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_gsp **pgsp)
{
	return nvkm_gsp_new_(gh100_gsps, device, type, inst, pgsp);
}

NVKM_GSP_FIRMWARE_FMC(gh100, 570.144);
