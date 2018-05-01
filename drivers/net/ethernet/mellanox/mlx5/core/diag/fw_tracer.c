/*
 * Copyright (c) 2018, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "fw_tracer.h"

static int mlx5_query_mtrc_caps(struct mlx5_fw_tracer *tracer)
{
	u32 *string_db_base_address_out = tracer->str_db.base_address_out;
	u32 *string_db_size_out = tracer->str_db.size_out;
	struct mlx5_core_dev *dev = tracer->dev;
	u32 out[MLX5_ST_SZ_DW(mtrc_cap)] = {0};
	u32 in[MLX5_ST_SZ_DW(mtrc_cap)] = {0};
	void *mtrc_cap_sp;
	int err, i;

	err = mlx5_core_access_reg(dev, in, sizeof(in), out, sizeof(out),
				   MLX5_REG_MTRC_CAP, 0, 0);
	if (err) {
		mlx5_core_warn(dev, "FWTracer: Error reading tracer caps %d\n",
			       err);
		return err;
	}

	if (!MLX5_GET(mtrc_cap, out, trace_to_memory)) {
		mlx5_core_dbg(dev, "FWTracer: Device does not support logging traces to memory\n");
		return -ENOTSUPP;
	}

	tracer->trc_ver = MLX5_GET(mtrc_cap, out, trc_ver);
	tracer->str_db.first_string_trace =
			MLX5_GET(mtrc_cap, out, first_string_trace);
	tracer->str_db.num_string_trace =
			MLX5_GET(mtrc_cap, out, num_string_trace);
	tracer->str_db.num_string_db = MLX5_GET(mtrc_cap, out, num_string_db);
	tracer->owner = !!MLX5_GET(mtrc_cap, out, trace_owner);

	for (i = 0; i < tracer->str_db.num_string_db; i++) {
		mtrc_cap_sp = MLX5_ADDR_OF(mtrc_cap, out, string_db_param[i]);
		string_db_base_address_out[i] = MLX5_GET(mtrc_string_db_param,
							 mtrc_cap_sp,
							 string_db_base_address);
		string_db_size_out[i] = MLX5_GET(mtrc_string_db_param,
						 mtrc_cap_sp, string_db_size);
	}

	return err;
}

static int mlx5_set_mtrc_caps_trace_owner(struct mlx5_fw_tracer *tracer,
					  u32 *out, u32 out_size,
					  u8 trace_owner)
{
	struct mlx5_core_dev *dev = tracer->dev;
	u32 in[MLX5_ST_SZ_DW(mtrc_cap)] = {0};

	MLX5_SET(mtrc_cap, in, trace_owner, trace_owner);

	return mlx5_core_access_reg(dev, in, sizeof(in), out, out_size,
				    MLX5_REG_MTRC_CAP, 0, 1);
}

static int mlx5_fw_tracer_ownership_acquire(struct mlx5_fw_tracer *tracer)
{
	struct mlx5_core_dev *dev = tracer->dev;
	u32 out[MLX5_ST_SZ_DW(mtrc_cap)] = {0};
	int err;

	err = mlx5_set_mtrc_caps_trace_owner(tracer, out, sizeof(out),
					     MLX5_FW_TRACER_ACQUIRE_OWNERSHIP);
	if (err) {
		mlx5_core_warn(dev, "FWTracer: Acquire tracer ownership failed %d\n",
			       err);
		return err;
	}

	tracer->owner = !!MLX5_GET(mtrc_cap, out, trace_owner);

	if (!tracer->owner)
		return -EBUSY;

	return 0;
}

static void mlx5_fw_tracer_ownership_release(struct mlx5_fw_tracer *tracer)
{
	u32 out[MLX5_ST_SZ_DW(mtrc_cap)] = {0};

	mlx5_set_mtrc_caps_trace_owner(tracer, out, sizeof(out),
				       MLX5_FW_TRACER_RELEASE_OWNERSHIP);
	tracer->owner = false;
}

static int mlx5_fw_tracer_create_log_buf(struct mlx5_fw_tracer *tracer)
{
	struct mlx5_core_dev *dev = tracer->dev;
	struct device *ddev = &dev->pdev->dev;
	dma_addr_t dma;
	void *buff;
	gfp_t gfp;
	int err;

	tracer->buff.size = TRACE_BUFFER_SIZE_BYTE;

	gfp = GFP_KERNEL | __GFP_ZERO;
	buff = (void *)__get_free_pages(gfp,
					get_order(tracer->buff.size));
	if (!buff) {
		err = -ENOMEM;
		mlx5_core_warn(dev, "FWTracer: Failed to allocate pages, %d\n", err);
		return err;
	}
	tracer->buff.log_buf = buff;

	dma = dma_map_single(ddev, buff, tracer->buff.size, DMA_FROM_DEVICE);
	if (dma_mapping_error(ddev, dma)) {
		mlx5_core_warn(dev, "FWTracer: Unable to map DMA: %d\n",
			       dma_mapping_error(ddev, dma));
		err = -ENOMEM;
		goto free_pages;
	}
	tracer->buff.dma = dma;

	return 0;

free_pages:
	free_pages((unsigned long)tracer->buff.log_buf, get_order(tracer->buff.size));

	return err;
}

static void mlx5_fw_tracer_destroy_log_buf(struct mlx5_fw_tracer *tracer)
{
	struct mlx5_core_dev *dev = tracer->dev;
	struct device *ddev = &dev->pdev->dev;

	if (!tracer->buff.log_buf)
		return;

	dma_unmap_single(ddev, tracer->buff.dma, tracer->buff.size, DMA_FROM_DEVICE);
	free_pages((unsigned long)tracer->buff.log_buf, get_order(tracer->buff.size));
}

static int mlx5_fw_tracer_create_mkey(struct mlx5_fw_tracer *tracer)
{
	struct mlx5_core_dev *dev = tracer->dev;
	int err, inlen, i;
	__be64 *mtt;
	void *mkc;
	u32 *in;

	inlen = MLX5_ST_SZ_BYTES(create_mkey_in) +
			sizeof(*mtt) * round_up(TRACER_BUFFER_PAGE_NUM, 2);

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(create_mkey_in, in, translations_octword_actual_size,
		 DIV_ROUND_UP(TRACER_BUFFER_PAGE_NUM, 2));
	mtt = (u64 *)MLX5_ADDR_OF(create_mkey_in, in, klm_pas_mtt);
	for (i = 0 ; i < TRACER_BUFFER_PAGE_NUM ; i++)
		mtt[i] = cpu_to_be64(tracer->buff.dma + i * PAGE_SIZE);

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_MTT);
	MLX5_SET(mkc, mkc, lr, 1);
	MLX5_SET(mkc, mkc, lw, 1);
	MLX5_SET(mkc, mkc, pd, tracer->buff.pdn);
	MLX5_SET(mkc, mkc, bsf_octword_size, 0);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET(mkc, mkc, log_page_size, PAGE_SHIFT);
	MLX5_SET(mkc, mkc, translations_octword_size,
		 DIV_ROUND_UP(TRACER_BUFFER_PAGE_NUM, 2));
	MLX5_SET64(mkc, mkc, start_addr, tracer->buff.dma);
	MLX5_SET64(mkc, mkc, len, tracer->buff.size);
	err = mlx5_core_create_mkey(dev, &tracer->buff.mkey, in, inlen);
	if (err)
		mlx5_core_warn(dev, "FWTracer: Failed to create mkey, %d\n", err);

	kvfree(in);

	return err;
}

static void mlx5_fw_tracer_free_strings_db(struct mlx5_fw_tracer *tracer)
{
	u32 num_string_db = tracer->str_db.num_string_db;
	int i;

	for (i = 0; i < num_string_db; i++) {
		kfree(tracer->str_db.buffer[i]);
		tracer->str_db.buffer[i] = NULL;
	}
}

static int mlx5_fw_tracer_allocate_strings_db(struct mlx5_fw_tracer *tracer)
{
	u32 *string_db_size_out = tracer->str_db.size_out;
	u32 num_string_db = tracer->str_db.num_string_db;
	int i;

	for (i = 0; i < num_string_db; i++) {
		tracer->str_db.buffer[i] = kzalloc(string_db_size_out[i], GFP_KERNEL);
		if (!tracer->str_db.buffer[i])
			goto free_strings_db;
	}

	return 0;

free_strings_db:
	mlx5_fw_tracer_free_strings_db(tracer);
	return -ENOMEM;
}

static void mlx5_tracer_read_strings_db(struct work_struct *work)
{
	struct mlx5_fw_tracer *tracer = container_of(work, struct mlx5_fw_tracer,
						     read_fw_strings_work);
	u32 num_of_reads, num_string_db = tracer->str_db.num_string_db;
	struct mlx5_core_dev *dev = tracer->dev;
	u32 in[MLX5_ST_SZ_DW(mtrc_cap)] = {0};
	u32 leftovers, offset;
	int err = 0, i, j;
	u32 *out, outlen;
	void *out_value;

	outlen = MLX5_ST_SZ_BYTES(mtrc_stdb) + STRINGS_DB_READ_SIZE_BYTES;
	out = kzalloc(outlen, GFP_KERNEL);
	if (!out) {
		err = -ENOMEM;
		goto out;
	}

	for (i = 0; i < num_string_db; i++) {
		offset = 0;
		MLX5_SET(mtrc_stdb, in, string_db_index, i);
		num_of_reads = tracer->str_db.size_out[i] /
				STRINGS_DB_READ_SIZE_BYTES;
		leftovers = (tracer->str_db.size_out[i] %
				STRINGS_DB_READ_SIZE_BYTES) /
					STRINGS_DB_LEFTOVER_SIZE_BYTES;

		MLX5_SET(mtrc_stdb, in, read_size, STRINGS_DB_READ_SIZE_BYTES);
		for (j = 0; j < num_of_reads; j++) {
			MLX5_SET(mtrc_stdb, in, start_offset, offset);

			err = mlx5_core_access_reg(dev, in, sizeof(in), out,
						   outlen, MLX5_REG_MTRC_STDB,
						   0, 1);
			if (err) {
				mlx5_core_dbg(dev, "FWTracer: Failed to read strings DB %d\n",
					      err);
				goto out_free;
			}

			out_value = MLX5_ADDR_OF(mtrc_stdb, out, string_db_data);
			memcpy(tracer->str_db.buffer[i] + offset, out_value,
			       STRINGS_DB_READ_SIZE_BYTES);
			offset += STRINGS_DB_READ_SIZE_BYTES;
		}

		/* Strings database is aligned to 64, need to read leftovers*/
		MLX5_SET(mtrc_stdb, in, read_size,
			 STRINGS_DB_LEFTOVER_SIZE_BYTES);
		for (j = 0; j < leftovers; j++) {
			MLX5_SET(mtrc_stdb, in, start_offset, offset);

			err = mlx5_core_access_reg(dev, in, sizeof(in), out,
						   outlen, MLX5_REG_MTRC_STDB,
						   0, 1);
			if (err) {
				mlx5_core_dbg(dev, "FWTracer: Failed to read strings DB %d\n",
					      err);
				goto out_free;
			}

			out_value = MLX5_ADDR_OF(mtrc_stdb, out, string_db_data);
			memcpy(tracer->str_db.buffer[i] + offset, out_value,
			       STRINGS_DB_LEFTOVER_SIZE_BYTES);
			offset += STRINGS_DB_LEFTOVER_SIZE_BYTES;
		}
	}

	tracer->str_db.loaded = true;

out_free:
	kfree(out);
out:
	return;
}

static void mlx5_fw_tracer_ownership_change(struct work_struct *work)
{
	struct mlx5_fw_tracer *tracer = container_of(work, struct mlx5_fw_tracer,
						     ownership_change_work);
	struct mlx5_core_dev *dev = tracer->dev;
	int err;

	if (tracer->owner) {
		mlx5_fw_tracer_ownership_release(tracer);
		return;
	}

	err = mlx5_fw_tracer_ownership_acquire(tracer);
	if (err) {
		mlx5_core_dbg(dev, "FWTracer: Ownership was not granted %d\n", err);
		return;
	}
}

struct mlx5_fw_tracer *mlx5_fw_tracer_create(struct mlx5_core_dev *dev)
{
	struct mlx5_fw_tracer *tracer = NULL;
	int err;

	if (!MLX5_CAP_MCAM_REG(dev, tracer_registers)) {
		mlx5_core_dbg(dev, "FWTracer: Tracer capability not present\n");
		return NULL;
	}

	tracer = kzalloc(sizeof(*tracer), GFP_KERNEL);
	if (!tracer)
		return ERR_PTR(-ENOMEM);

	tracer->work_queue = create_singlethread_workqueue("mlx5_fw_tracer");
	if (!tracer->work_queue) {
		err = -ENOMEM;
		goto free_tracer;
	}

	tracer->dev = dev;

	INIT_WORK(&tracer->ownership_change_work, mlx5_fw_tracer_ownership_change);
	INIT_WORK(&tracer->read_fw_strings_work, mlx5_tracer_read_strings_db);

	err = mlx5_query_mtrc_caps(tracer);
	if (err) {
		mlx5_core_dbg(dev, "FWTracer: Failed to query capabilities %d\n", err);
		goto destroy_workqueue;
	}

	err = mlx5_fw_tracer_create_log_buf(tracer);
	if (err) {
		mlx5_core_warn(dev, "FWTracer: Create log buffer failed %d\n", err);
		goto destroy_workqueue;
	}

	err = mlx5_fw_tracer_allocate_strings_db(tracer);
	if (err) {
		mlx5_core_warn(dev, "FWTracer: Allocate strings database failed %d\n", err);
		goto free_log_buf;
	}

	return tracer;

free_log_buf:
	mlx5_fw_tracer_destroy_log_buf(tracer);
destroy_workqueue:
	tracer->dev = NULL;
	destroy_workqueue(tracer->work_queue);
free_tracer:
	kfree(tracer);
	return ERR_PTR(err);
}

int mlx5_fw_tracer_init(struct mlx5_fw_tracer *tracer)
{
	struct mlx5_core_dev *dev;
	int err;

	if (IS_ERR_OR_NULL(tracer))
		return 0;

	dev = tracer->dev;

	if (!tracer->str_db.loaded)
		queue_work(tracer->work_queue, &tracer->read_fw_strings_work);

	err = mlx5_core_alloc_pd(dev, &tracer->buff.pdn);
	if (err) {
		mlx5_core_warn(dev, "FWTracer: Failed to allocate PD %d\n", err);
		return err;
	}

	err = mlx5_fw_tracer_create_mkey(tracer);
	if (err) {
		mlx5_core_warn(dev, "FWTracer: Failed to create mkey %d\n", err);
		goto err_dealloc_pd;
	}

	err = mlx5_fw_tracer_ownership_acquire(tracer);
	if (err) /* Don't fail since ownership can be acquired on a later FW event */
		mlx5_core_dbg(dev, "FWTracer: Ownership was not granted %d\n", err);

	return 0;
err_dealloc_pd:
	mlx5_core_dealloc_pd(dev, tracer->buff.pdn);
	return err;
}

void mlx5_fw_tracer_cleanup(struct mlx5_fw_tracer *tracer)
{
	if (IS_ERR_OR_NULL(tracer))
		return;

	cancel_work_sync(&tracer->ownership_change_work);

	if (tracer->owner)
		mlx5_fw_tracer_ownership_release(tracer);

	mlx5_core_destroy_mkey(tracer->dev, &tracer->buff.mkey);
	mlx5_core_dealloc_pd(tracer->dev, tracer->buff.pdn);
}

void mlx5_fw_tracer_destroy(struct mlx5_fw_tracer *tracer)
{
	if (IS_ERR_OR_NULL(tracer))
		return;

	cancel_work_sync(&tracer->read_fw_strings_work);
	mlx5_fw_tracer_free_strings_db(tracer);
	mlx5_fw_tracer_destroy_log_buf(tracer);
	flush_workqueue(tracer->work_queue);
	destroy_workqueue(tracer->work_queue);
	kfree(tracer);
}

EXPORT_TRACEPOINT_SYMBOL(mlx5_fw);
