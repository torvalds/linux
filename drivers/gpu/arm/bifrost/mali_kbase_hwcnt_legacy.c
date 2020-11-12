/*
 *
 * (C) COPYRIGHT 2018, 2020 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include "mali_kbase_hwcnt_legacy.h"
#include "mali_kbase_hwcnt_virtualizer.h"
#include "mali_kbase_hwcnt_types.h"
#include "mali_kbase_hwcnt_gpu.h"
#include "mali_kbase_ioctl.h"

#include <linux/slab.h>
#include <linux/uaccess.h>

/**
 * struct kbase_hwcnt_legacy_client - Legacy hardware counter client.
 * @user_dump_buf: Pointer to a non-NULL user buffer, where dumps are returned.
 * @enable_map:    Counter enable map.
 * @dump_buf:      Dump buffer used to manipulate dumps before copied to user.
 * @hvcli:         Hardware counter virtualizer client.
 */
struct kbase_hwcnt_legacy_client {
	void __user *user_dump_buf;
	struct kbase_hwcnt_enable_map enable_map;
	struct kbase_hwcnt_dump_buffer dump_buf;
	struct kbase_hwcnt_virtualizer_client *hvcli;
};

int kbase_hwcnt_legacy_client_create(
	struct kbase_hwcnt_virtualizer *hvirt,
	struct kbase_ioctl_hwcnt_enable *enable,
	struct kbase_hwcnt_legacy_client **out_hlcli)
{
	int errcode;
	struct kbase_hwcnt_legacy_client *hlcli;
	const struct kbase_hwcnt_metadata *metadata;
	struct kbase_hwcnt_physical_enable_map phys_em;

	if (!hvirt || !enable || !enable->dump_buffer || !out_hlcli)
		return -EINVAL;

	metadata = kbase_hwcnt_virtualizer_metadata(hvirt);

	hlcli = kzalloc(sizeof(*hlcli), GFP_KERNEL);
	if (!hlcli)
		return -ENOMEM;

	hlcli->user_dump_buf = (void __user *)(uintptr_t)enable->dump_buffer;

	errcode = kbase_hwcnt_enable_map_alloc(metadata, &hlcli->enable_map);
	if (errcode)
		goto error;

	/* Translate from the ioctl enable map to the internal one */
	phys_em.fe_bm = enable->fe_bm;
	phys_em.shader_bm = enable->shader_bm;
	phys_em.tiler_bm = enable->tiler_bm;
	phys_em.mmu_l2_bm = enable->mmu_l2_bm;
	kbase_hwcnt_gpu_enable_map_from_physical(&hlcli->enable_map, &phys_em);

	errcode = kbase_hwcnt_dump_buffer_alloc(metadata, &hlcli->dump_buf);
	if (errcode)
		goto error;

	errcode = kbase_hwcnt_virtualizer_client_create(
		hvirt, &hlcli->enable_map, &hlcli->hvcli);
	if (errcode)
		goto error;

	*out_hlcli = hlcli;
	return 0;

error:
	kbase_hwcnt_legacy_client_destroy(hlcli);
	return errcode;
}

void kbase_hwcnt_legacy_client_destroy(struct kbase_hwcnt_legacy_client *hlcli)
{
	if (!hlcli)
		return;

	kbase_hwcnt_virtualizer_client_destroy(hlcli->hvcli);
	kbase_hwcnt_dump_buffer_free(&hlcli->dump_buf);
	kbase_hwcnt_enable_map_free(&hlcli->enable_map);
	kfree(hlcli);
}

int kbase_hwcnt_legacy_client_dump(struct kbase_hwcnt_legacy_client *hlcli)
{
	int errcode;
	u64 ts_start_ns;
	u64 ts_end_ns;

	if (!hlcli)
		return -EINVAL;

	/* Dump into the kernel buffer */
	errcode = kbase_hwcnt_virtualizer_client_dump(hlcli->hvcli,
		&ts_start_ns, &ts_end_ns, &hlcli->dump_buf);
	if (errcode)
		return errcode;

	/* Patch the dump buf headers, to hide the counters that other hwcnt
	 * clients are using.
	 */
	kbase_hwcnt_gpu_patch_dump_headers(
		&hlcli->dump_buf, &hlcli->enable_map);

	/* Zero all non-enabled counters (current values are undefined) */
	kbase_hwcnt_dump_buffer_zero_non_enabled(
		&hlcli->dump_buf, &hlcli->enable_map);

	/* Copy into the user's buffer */
	errcode = copy_to_user(hlcli->user_dump_buf, hlcli->dump_buf.dump_buf,
		hlcli->dump_buf.metadata->dump_buf_bytes);
	/* Non-zero errcode implies user buf was invalid or too small */
	if (errcode)
		return -EFAULT;

	return 0;
}

int kbase_hwcnt_legacy_client_clear(struct kbase_hwcnt_legacy_client *hlcli)
{
	u64 ts_start_ns;
	u64 ts_end_ns;

	if (!hlcli)
		return -EINVAL;

	/* Dump with a NULL buffer to clear this client's counters */
	return kbase_hwcnt_virtualizer_client_dump(hlcli->hvcli,
		&ts_start_ns, &ts_end_ns, NULL);
}
