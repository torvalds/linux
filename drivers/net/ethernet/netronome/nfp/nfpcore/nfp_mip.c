// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2015-2017 Netronome Systems, Inc. */

/*
 * nfp_mip.c
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Espen Skoglund <espen.skoglund@netronome.com>
 */
#include <linux/kernel.h>
#include <linux/slab.h>

#include "nfp.h"
#include "nfp_cpp.h"
#include "nfp_nffw.h"

#define NFP_MIP_SIGNATURE	cpu_to_le32(0x0050494d)  /* "MIP\0" */
#define NFP_MIP_VERSION		cpu_to_le32(1)
#define NFP_MIP_MAX_OFFSET	(256 * 1024)

struct nfp_mip {
	__le32 signature;
	__le32 mip_version;
	__le32 mip_size;
	__le32 first_entry;

	__le32 version;
	__le32 buildnum;
	__le32 buildtime;
	__le32 loadtime;

	__le32 symtab_addr;
	__le32 symtab_size;
	__le32 strtab_addr;
	__le32 strtab_size;

	char name[16];
	char toolchain[32];
};

/* Read memory and check if it could be a valid MIP */
static int
nfp_mip_try_read(struct nfp_cpp *cpp, u32 cpp_id, u64 addr, struct nfp_mip *mip)
{
	int ret;

	ret = nfp_cpp_read(cpp, cpp_id, addr, mip, sizeof(*mip));
	if (ret != sizeof(*mip)) {
		nfp_err(cpp, "Failed to read MIP data (%d, %zu)\n",
			ret, sizeof(*mip));
		return -EIO;
	}
	if (mip->signature != NFP_MIP_SIGNATURE) {
		nfp_warn(cpp, "Incorrect MIP signature (0x%08x)\n",
			 le32_to_cpu(mip->signature));
		return -EINVAL;
	}
	if (mip->mip_version != NFP_MIP_VERSION) {
		nfp_warn(cpp, "Unsupported MIP version (%d)\n",
			 le32_to_cpu(mip->mip_version));
		return -EINVAL;
	}

	return 0;
}

/* Try to locate MIP using the resource table */
static int nfp_mip_read_resource(struct nfp_cpp *cpp, struct nfp_mip *mip)
{
	struct nfp_nffw_info *nffw_info;
	u32 cpp_id;
	u64 addr;
	int err;

	nffw_info = nfp_nffw_info_open(cpp);
	if (IS_ERR(nffw_info))
		return PTR_ERR(nffw_info);

	err = nfp_nffw_info_mip_first(nffw_info, &cpp_id, &addr);
	if (err)
		goto exit_close_nffw;

	err = nfp_mip_try_read(cpp, cpp_id, addr, mip);
exit_close_nffw:
	nfp_nffw_info_close(nffw_info);
	return err;
}

/**
 * nfp_mip_open() - Get device MIP structure
 * @cpp:	NFP CPP Handle
 *
 * Copy MIP structure from NFP device and return it.  The returned
 * structure is handled internally by the library and should be
 * freed by calling nfp_mip_close().
 *
 * Return: pointer to mip, NULL on failure.
 */
const struct nfp_mip *nfp_mip_open(struct nfp_cpp *cpp)
{
	struct nfp_mip *mip;
	int err;

	mip = kmalloc(sizeof(*mip), GFP_KERNEL);
	if (!mip)
		return NULL;

	err = nfp_mip_read_resource(cpp, mip);
	if (err) {
		kfree(mip);
		return NULL;
	}

	mip->name[sizeof(mip->name) - 1] = 0;

	return mip;
}

void nfp_mip_close(const struct nfp_mip *mip)
{
	kfree(mip);
}

const char *nfp_mip_name(const struct nfp_mip *mip)
{
	return mip->name;
}

/**
 * nfp_mip_symtab() - Get the address and size of the MIP symbol table
 * @mip:	MIP handle
 * @addr:	Location for NFP DDR address of MIP symbol table
 * @size:	Location for size of MIP symbol table
 */
void nfp_mip_symtab(const struct nfp_mip *mip, u32 *addr, u32 *size)
{
	*addr = le32_to_cpu(mip->symtab_addr);
	*size = le32_to_cpu(mip->symtab_size);
}

/**
 * nfp_mip_strtab() - Get the address and size of the MIP symbol name table
 * @mip:	MIP handle
 * @addr:	Location for NFP DDR address of MIP symbol name table
 * @size:	Location for size of MIP symbol name table
 */
void nfp_mip_strtab(const struct nfp_mip *mip, u32 *addr, u32 *size)
{
	*addr = le32_to_cpu(mip->strtab_addr);
	*size = le32_to_cpu(mip->strtab_size);
}
