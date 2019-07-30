// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2019 Mellanox Technologies. All rights reserved */

#define pr_fmt(fmt) "mlxfw_mfa2: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/xz.h>
#include "mlxfw_mfa2.h"
#include "mlxfw_mfa2_file.h"
#include "mlxfw_mfa2_tlv.h"
#include "mlxfw_mfa2_format.h"
#include "mlxfw_mfa2_tlv_multi.h"

/*               MFA2 FILE
 *  +----------------------------------+
 *  |        MFA2 finger print         |
 *  +----------------------------------+
 *  |   package descriptor multi_tlv   |
 *  | +------------------------------+ |     +-----------------+
 *  | |    package descriptor tlv    +-----> |num_devices=n    |
 *  | +------------------------------+ |     |num_components=m |
 *  +----------------------------------+     |CB offset        |
 *  |    device descriptor multi_tlv   |     |...              |
 *  | +------------------------------+ |     |                 |
 *  | |           PSID tlv           | |     +-----------------+
 *  | +------------------------------+ |
 *  | |     component index tlv      | |
 *  | +------------------------------+ |
 *  +----------------------------------+
 *  |  component descriptor multi_tlv  |
 *  | +------------------------------+ |     +-----------------+
 *  | |  component descriptor tlv    +-----> |Among others:    |
 *  | +------------------------------+ |     |CB offset=o      |
 *  +----------------------------------+     |comp index=i     |
 *  |                                  |     |...              |
 *  |                                  |     |                 |
 *  |                                  |     +-----------------+
 *  |        COMPONENT BLOCK (CB)      |
 *  |                                  |
 *  |                                  |
 *  |                                  |
 *  +----------------------------------+
 *
 * On the top level, an MFA2 file contains:
 *  - Fingerprint
 *  - Several multi_tlvs (TLVs of type MLXFW_MFA2_TLV_MULTI, as defined in
 *    mlxfw_mfa2_format.h)
 *  - Compresses content block
 *
 * The first multi_tlv
 * -------------------
 * The first multi TLV is treated as package descriptor, and expected to have a
 * first TLV child of type MLXFW_MFA2_TLV_PACKAGE_DESCRIPTOR which contains all
 * the global information needed to parse the file. Among others, it contains
 * the number of device descriptors and component descriptor following this
 * multi TLV.
 *
 * The device descriptor multi_tlv
 * -------------------------------
 * The multi TLVs following the package descriptor are treated as device
 * descriptor, and are expected to have the following children:
 *  - PSID TLV child of type MLXFW_MFA2_TLV_PSID containing that device PSID.
 *  - Component index of type MLXFW_MFA2_TLV_COMPONENT_PTR that contains that
 *    device component index.
 *
 * The component descriptor multi_tlv
 * ----------------------------------
 * The multi TLVs following the device descriptor multi TLVs are treated as
 * component descriptor, and are expected to have a first child of type
 * MLXFW_MFA2_TLV_COMPONENT_DESCRIPTOR that contains mostly the component index,
 * needed for the flash process and the offset to the binary within the
 * component block.
 */

static const u8 mlxfw_mfa2_fingerprint[] = "MLNX.MFA2.XZ.00!";
static const int mlxfw_mfa2_fingerprint_len =
			sizeof(mlxfw_mfa2_fingerprint) - 1;

static const u8 mlxfw_mfa2_comp_magic[] = "#BIN.COMPONENT!#";
static const int mlxfw_mfa2_comp_magic_len = sizeof(mlxfw_mfa2_comp_magic) - 1;

bool mlxfw_mfa2_check(const struct firmware *fw)
{
	if (fw->size < sizeof(mlxfw_mfa2_fingerprint))
		return false;

	return memcmp(fw->data, mlxfw_mfa2_fingerprint,
		      mlxfw_mfa2_fingerprint_len) == 0;
}

static bool
mlxfw_mfa2_tlv_multi_validate(const struct mlxfw_mfa2_file *mfa2_file,
			      const struct mlxfw_mfa2_tlv_multi *multi)
{
	const struct mlxfw_mfa2_tlv *tlv;
	u16 idx;

	/* Check that all children are valid */
	mlxfw_mfa2_tlv_multi_foreach(mfa2_file, tlv, idx, multi) {
		if (!tlv) {
			pr_err("Multi has invalid child");
			return false;
		}
	}
	return true;
}

static bool
mlxfw_mfa2_file_dev_validate(const struct mlxfw_mfa2_file *mfa2_file,
			     const struct mlxfw_mfa2_tlv *dev_tlv,
			     u16 dev_idx)
{
	const struct mlxfw_mfa2_tlv_component_ptr *cptr;
	const struct mlxfw_mfa2_tlv_multi *multi;
	const struct mlxfw_mfa2_tlv_psid *psid;
	const struct mlxfw_mfa2_tlv *tlv;
	u16 cptr_count;
	u16 cptr_idx;
	int err;

	pr_debug("Device %d\n", dev_idx);

	multi = mlxfw_mfa2_tlv_multi_get(mfa2_file, dev_tlv);
	if (!multi) {
		pr_err("Device %d is not a valid TLV error\n", dev_idx);
		return false;
	}

	if (!mlxfw_mfa2_tlv_multi_validate(mfa2_file, multi))
		return false;

	/* Validate the device has PSID tlv */
	tlv = mlxfw_mfa2_tlv_multi_child_find(mfa2_file, multi,
					      MLXFW_MFA2_TLV_PSID, 0);
	if (!tlv) {
		pr_err("Device %d does not have PSID\n", dev_idx);
		return false;
	}

	psid = mlxfw_mfa2_tlv_psid_get(mfa2_file, tlv);
	if (!psid) {
		pr_err("Device %d PSID TLV is not valid\n", dev_idx);
		return false;
	}

	print_hex_dump_debug("  -- Device PSID ", DUMP_PREFIX_NONE, 16, 16,
			     psid->psid, be16_to_cpu(tlv->len), true);

	/* Validate the device has COMPONENT_PTR */
	err = mlxfw_mfa2_tlv_multi_child_count(mfa2_file, multi,
					       MLXFW_MFA2_TLV_COMPONENT_PTR,
					       &cptr_count);
	if (err)
		return false;

	if (cptr_count == 0) {
		pr_err("Device %d has no components\n", dev_idx);
		return false;
	}

	for (cptr_idx = 0; cptr_idx < cptr_count; cptr_idx++) {
		tlv = mlxfw_mfa2_tlv_multi_child_find(mfa2_file, multi,
						      MLXFW_MFA2_TLV_COMPONENT_PTR,
						      cptr_idx);
		if (!tlv)
			return false;

		cptr = mlxfw_mfa2_tlv_component_ptr_get(mfa2_file, tlv);
		if (!cptr) {
			pr_err("Device %d COMPONENT_PTR TLV is not valid\n",
			       dev_idx);
			return false;
		}

		pr_debug("  -- Component index %d\n",
			 be16_to_cpu(cptr->component_index));
	}
	return true;
}

static bool
mlxfw_mfa2_file_comp_validate(const struct mlxfw_mfa2_file *mfa2_file,
			      const struct mlxfw_mfa2_tlv *comp_tlv,
			      u16 comp_idx)
{
	const struct mlxfw_mfa2_tlv_component_descriptor *cdesc;
	const struct mlxfw_mfa2_tlv_multi *multi;
	const struct mlxfw_mfa2_tlv *tlv;

	pr_debug("Component %d\n", comp_idx);

	multi = mlxfw_mfa2_tlv_multi_get(mfa2_file, comp_tlv);
	if (!multi) {
		pr_err("Component %d is not a valid TLV error\n", comp_idx);
		return false;
	}

	if (!mlxfw_mfa2_tlv_multi_validate(mfa2_file, multi))
		return false;

	/* Check that component have COMPONENT_DESCRIPTOR as first child */
	tlv = mlxfw_mfa2_tlv_multi_child(mfa2_file, multi);
	if (!tlv) {
		pr_err("Component descriptor %d multi TLV error\n", comp_idx);
		return false;
	}

	cdesc = mlxfw_mfa2_tlv_component_descriptor_get(mfa2_file, tlv);
	if (!cdesc) {
		pr_err("Component %d does not have a valid descriptor\n",
		       comp_idx);
		return false;
	}
	pr_debug("  -- Component type %d\n", be16_to_cpu(cdesc->identifier));
	pr_debug("  -- Offset 0x%llx and size %d\n",
		 ((u64) be32_to_cpu(cdesc->cb_offset_h) << 32)
		 | be32_to_cpu(cdesc->cb_offset_l), be32_to_cpu(cdesc->size));

	return true;
}

static bool mlxfw_mfa2_file_validate(const struct mlxfw_mfa2_file *mfa2_file)
{
	const struct mlxfw_mfa2_tlv *tlv;
	u16 idx;

	pr_debug("Validating file\n");

	/* check that all the devices exist */
	mlxfw_mfa2_tlv_foreach(mfa2_file, tlv, idx, mfa2_file->first_dev,
			       mfa2_file->dev_count) {
		if (!tlv) {
			pr_err("Device TLV error\n");
			return false;
		}

		/* Check each device */
		if (!mlxfw_mfa2_file_dev_validate(mfa2_file, tlv, idx))
			return false;
	}

	/* check that all the components exist */
	mlxfw_mfa2_tlv_foreach(mfa2_file, tlv, idx, mfa2_file->first_component,
			       mfa2_file->component_count) {
		if (!tlv) {
			pr_err("Device TLV error\n");
			return false;
		}

		/* Check each component */
		if (!mlxfw_mfa2_file_comp_validate(mfa2_file, tlv, idx))
			return false;
	}
	return true;
}

struct mlxfw_mfa2_file *mlxfw_mfa2_file_init(const struct firmware *fw)
{
	const struct mlxfw_mfa2_tlv_package_descriptor *pd;
	const struct mlxfw_mfa2_tlv_multi *multi;
	const struct mlxfw_mfa2_tlv *multi_child;
	const struct mlxfw_mfa2_tlv *first_tlv;
	struct mlxfw_mfa2_file *mfa2_file;
	const void *first_tlv_ptr;
	const void *cb_top_ptr;

	mfa2_file = kcalloc(1, sizeof(*mfa2_file), GFP_KERNEL);
	if (!mfa2_file)
		return ERR_PTR(-ENOMEM);

	mfa2_file->fw = fw;
	first_tlv_ptr = fw->data + NLA_ALIGN(mlxfw_mfa2_fingerprint_len);
	first_tlv = mlxfw_mfa2_tlv_get(mfa2_file, first_tlv_ptr);
	if (!first_tlv) {
		pr_err("Could not parse package descriptor TLV\n");
		goto err_out;
	}

	multi = mlxfw_mfa2_tlv_multi_get(mfa2_file, first_tlv);
	if (!multi) {
		pr_err("First TLV is not of valid multi type\n");
		goto err_out;
	}

	multi_child = mlxfw_mfa2_tlv_multi_child(mfa2_file, multi);
	if (!multi_child)
		goto err_out;

	pd = mlxfw_mfa2_tlv_package_descriptor_get(mfa2_file, multi_child);
	if (!pd) {
		pr_err("Could not parse package descriptor TLV\n");
		goto err_out;
	}

	mfa2_file->first_dev = mlxfw_mfa2_tlv_next(mfa2_file, first_tlv);
	if (!mfa2_file->first_dev) {
		pr_err("First device TLV is not valid\n");
		goto err_out;
	}

	mfa2_file->dev_count = be16_to_cpu(pd->num_devices);
	mfa2_file->first_component = mlxfw_mfa2_tlv_advance(mfa2_file,
							    mfa2_file->first_dev,
							    mfa2_file->dev_count);
	mfa2_file->component_count = be16_to_cpu(pd->num_components);
	mfa2_file->cb = fw->data + NLA_ALIGN(be32_to_cpu(pd->cb_offset));
	if (!mlxfw_mfa2_valid_ptr(mfa2_file, mfa2_file->cb)) {
		pr_err("Component block is out side the file\n");
		goto err_out;
	}
	mfa2_file->cb_archive_size = be32_to_cpu(pd->cb_archive_size);
	cb_top_ptr = mfa2_file->cb + mfa2_file->cb_archive_size - 1;
	if (!mlxfw_mfa2_valid_ptr(mfa2_file, cb_top_ptr)) {
		pr_err("Component block size is too big\n");
		goto err_out;
	}

	if (!mlxfw_mfa2_file_validate(mfa2_file))
		goto err_out;
	return mfa2_file;
err_out:
	kfree(mfa2_file);
	return ERR_PTR(-EINVAL);
}

static const struct mlxfw_mfa2_tlv_multi *
mlxfw_mfa2_tlv_dev_get(const struct mlxfw_mfa2_file *mfa2_file,
		       const char *psid, u16 psid_size)
{
	const struct mlxfw_mfa2_tlv_psid *tlv_psid;
	const struct mlxfw_mfa2_tlv_multi *dev_multi;
	const struct mlxfw_mfa2_tlv *dev_tlv;
	const struct mlxfw_mfa2_tlv *tlv;
	u32 idx;

	/* for each device tlv */
	mlxfw_mfa2_tlv_foreach(mfa2_file, dev_tlv, idx, mfa2_file->first_dev,
			       mfa2_file->dev_count) {
		if (!dev_tlv)
			return NULL;

		dev_multi = mlxfw_mfa2_tlv_multi_get(mfa2_file, dev_tlv);
		if (!dev_multi)
			return NULL;

		/* find psid child and compare */
		tlv = mlxfw_mfa2_tlv_multi_child_find(mfa2_file, dev_multi,
						      MLXFW_MFA2_TLV_PSID, 0);
		if (!tlv)
			return NULL;
		if (be16_to_cpu(tlv->len) != psid_size)
			continue;

		tlv_psid = mlxfw_mfa2_tlv_psid_get(mfa2_file, tlv);
		if (!tlv_psid)
			return NULL;

		if (memcmp(psid, tlv_psid->psid, psid_size) == 0)
			return dev_multi;
	}

	return NULL;
}

int mlxfw_mfa2_file_component_count(const struct mlxfw_mfa2_file *mfa2_file,
				    const char *psid, u32 psid_size,
				    u32 *p_count)
{
	const struct mlxfw_mfa2_tlv_multi *dev_multi;
	u16 count;
	int err;

	dev_multi = mlxfw_mfa2_tlv_dev_get(mfa2_file, psid, psid_size);
	if (!dev_multi)
		return -EINVAL;

	err = mlxfw_mfa2_tlv_multi_child_count(mfa2_file, dev_multi,
					       MLXFW_MFA2_TLV_COMPONENT_PTR,
					       &count);
	if (err)
		return err;

	*p_count = count;
	return 0;
}

static int mlxfw_mfa2_xz_dec_run(struct xz_dec *xz_dec, struct xz_buf *xz_buf,
				 bool *finished)
{
	enum xz_ret xz_ret;

	xz_ret = xz_dec_run(xz_dec, xz_buf);

	switch (xz_ret) {
	case XZ_STREAM_END:
		*finished = true;
		return 0;
	case XZ_OK:
		*finished = false;
		return 0;
	case XZ_MEM_ERROR:
		pr_err("xz no memory\n");
		return -ENOMEM;
	case XZ_DATA_ERROR:
		pr_err("xz file corrupted\n");
		return -EINVAL;
	case XZ_FORMAT_ERROR:
		pr_err("xz format not found\n");
		return -EINVAL;
	case XZ_OPTIONS_ERROR:
		pr_err("unsupported xz option\n");
		return -EINVAL;
	case XZ_MEMLIMIT_ERROR:
		pr_err("xz dictionary too small\n");
		return -EINVAL;
	default:
		pr_err("xz error %d\n", xz_ret);
		return -EINVAL;
	}
}

static int mlxfw_mfa2_file_cb_offset_xz(const struct mlxfw_mfa2_file *mfa2_file,
					off_t off, size_t size, u8 *buf)
{
	struct xz_dec *xz_dec;
	struct xz_buf dec_buf;
	off_t curr_off = 0;
	bool finished;
	int err;

	xz_dec = xz_dec_init(XZ_DYNALLOC, (u32) -1);
	if (!xz_dec)
		return -EINVAL;

	dec_buf.in_size = mfa2_file->cb_archive_size;
	dec_buf.in = mfa2_file->cb;
	dec_buf.in_pos = 0;
	dec_buf.out = buf;

	/* decode up to the offset */
	do {
		dec_buf.out_pos = 0;
		dec_buf.out_size = min_t(size_t, size, off - curr_off);
		if (dec_buf.out_size == 0)
			break;

		err = mlxfw_mfa2_xz_dec_run(xz_dec, &dec_buf, &finished);
		if (err)
			goto out;
		if (finished) {
			pr_err("xz section too short\n");
			err = -EINVAL;
			goto out;
		}
		curr_off += dec_buf.out_pos;
	} while (curr_off != off);

	/* decode the needed section */
	dec_buf.out_pos = 0;
	dec_buf.out_size = size;
	err = mlxfw_mfa2_xz_dec_run(xz_dec, &dec_buf, &finished);
out:
	xz_dec_end(xz_dec);
	return err;
}

static const struct mlxfw_mfa2_tlv_component_descriptor *
mlxfw_mfa2_file_component_tlv_get(const struct mlxfw_mfa2_file *mfa2_file,
				  u16 comp_index)
{
	const struct mlxfw_mfa2_tlv_multi *multi;
	const struct mlxfw_mfa2_tlv *multi_child;
	const struct mlxfw_mfa2_tlv *comp_tlv;

	if (comp_index > mfa2_file->component_count)
		return NULL;

	comp_tlv = mlxfw_mfa2_tlv_advance(mfa2_file, mfa2_file->first_component,
					  comp_index);
	if (!comp_tlv)
		return NULL;

	multi = mlxfw_mfa2_tlv_multi_get(mfa2_file, comp_tlv);
	if (!multi)
		return NULL;

	multi_child = mlxfw_mfa2_tlv_multi_child(mfa2_file, multi);
	if (!multi_child)
		return NULL;

	return mlxfw_mfa2_tlv_component_descriptor_get(mfa2_file, multi_child);
}

struct mlxfw_mfa2_comp_data {
	struct mlxfw_mfa2_component comp;
	u8 buff[0];
};

static const struct mlxfw_mfa2_tlv_component_descriptor *
mlxfw_mfa2_file_component_find(const struct mlxfw_mfa2_file *mfa2_file,
			       const char *psid, int psid_size,
			       int component_index)
{
	const struct mlxfw_mfa2_tlv_component_ptr *cptr;
	const struct mlxfw_mfa2_tlv_multi *dev_multi;
	const struct mlxfw_mfa2_tlv *cptr_tlv;
	u16 comp_idx;

	dev_multi = mlxfw_mfa2_tlv_dev_get(mfa2_file, psid, psid_size);
	if (!dev_multi)
		return NULL;

	cptr_tlv = mlxfw_mfa2_tlv_multi_child_find(mfa2_file, dev_multi,
						   MLXFW_MFA2_TLV_COMPONENT_PTR,
						   component_index);
	if (!cptr_tlv)
		return NULL;

	cptr = mlxfw_mfa2_tlv_component_ptr_get(mfa2_file, cptr_tlv);
	if (!cptr)
		return NULL;

	comp_idx = be16_to_cpu(cptr->component_index);
	return mlxfw_mfa2_file_component_tlv_get(mfa2_file, comp_idx);
}

struct mlxfw_mfa2_component *
mlxfw_mfa2_file_component_get(const struct mlxfw_mfa2_file *mfa2_file,
			      const char *psid, int psid_size,
			      int component_index)
{
	const struct mlxfw_mfa2_tlv_component_descriptor *comp;
	struct mlxfw_mfa2_comp_data *comp_data;
	u32 comp_buf_size;
	off_t cb_offset;
	u32 comp_size;
	int err;

	comp = mlxfw_mfa2_file_component_find(mfa2_file, psid, psid_size,
					      component_index);
	if (!comp)
		return ERR_PTR(-EINVAL);

	cb_offset = (u64) be32_to_cpu(comp->cb_offset_h) << 32 |
		    be32_to_cpu(comp->cb_offset_l);
	comp_size = be32_to_cpu(comp->size);
	comp_buf_size = comp_size + mlxfw_mfa2_comp_magic_len;

	comp_data = kmalloc(sizeof(*comp_data) + comp_buf_size, GFP_KERNEL);
	if (!comp_data)
		return ERR_PTR(-ENOMEM);
	comp_data->comp.data_size = comp_size;
	comp_data->comp.index = be16_to_cpu(comp->identifier);
	err = mlxfw_mfa2_file_cb_offset_xz(mfa2_file, cb_offset, comp_buf_size,
					   comp_data->buff);
	if (err) {
		pr_err("Component could not be reached in CB\n");
		goto err_out;
	}

	if (memcmp(comp_data->buff, mlxfw_mfa2_comp_magic,
		   mlxfw_mfa2_comp_magic_len) != 0) {
		pr_err("Component has wrong magic\n");
		err = -EINVAL;
		goto err_out;
	}

	comp_data->comp.data = comp_data->buff + mlxfw_mfa2_comp_magic_len;
	return &comp_data->comp;
err_out:
	kfree(comp_data);
	return ERR_PTR(err);
}

void mlxfw_mfa2_file_component_put(struct mlxfw_mfa2_component *comp)
{
	const struct mlxfw_mfa2_comp_data *comp_data;

	comp_data = container_of(comp, struct mlxfw_mfa2_comp_data, comp);
	kfree(comp_data);
}

void mlxfw_mfa2_file_fini(struct mlxfw_mfa2_file *mfa2_file)
{
	kfree(mfa2_file);
}
