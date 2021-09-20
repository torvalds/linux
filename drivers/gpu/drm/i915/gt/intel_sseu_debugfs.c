// SPDX-License-Identifier: MIT

/*
 * Copyright © 2020 Intel Corporation
 */

#include "debugfs_gt.h"
#include "intel_sseu_debugfs.h"
#include "i915_drv.h"

static void sseu_copy_subslices(const struct sseu_dev_info *sseu,
				int slice, u8 *to_mask)
{
	int offset = slice * sseu->ss_stride;

	memcpy(&to_mask[offset], &sseu->subslice_mask[offset], sseu->ss_stride);
}

static void cherryview_sseu_device_status(struct intel_gt *gt,
					  struct sseu_dev_info *sseu)
{
#define SS_MAX 2
	struct intel_uncore *uncore = gt->uncore;
	const int ss_max = SS_MAX;
	u32 sig1[SS_MAX], sig2[SS_MAX];
	int ss;

	sig1[0] = intel_uncore_read(uncore, CHV_POWER_SS0_SIG1);
	sig1[1] = intel_uncore_read(uncore, CHV_POWER_SS1_SIG1);
	sig2[0] = intel_uncore_read(uncore, CHV_POWER_SS0_SIG2);
	sig2[1] = intel_uncore_read(uncore, CHV_POWER_SS1_SIG2);

	for (ss = 0; ss < ss_max; ss++) {
		unsigned int eu_cnt;

		if (sig1[ss] & CHV_SS_PG_ENABLE)
			/* skip disabled subslice */
			continue;

		sseu->slice_mask = BIT(0);
		sseu->subslice_mask[0] |= BIT(ss);
		eu_cnt = ((sig1[ss] & CHV_EU08_PG_ENABLE) ? 0 : 2) +
			 ((sig1[ss] & CHV_EU19_PG_ENABLE) ? 0 : 2) +
			 ((sig1[ss] & CHV_EU210_PG_ENABLE) ? 0 : 2) +
			 ((sig2[ss] & CHV_EU311_PG_ENABLE) ? 0 : 2);
		sseu->eu_total += eu_cnt;
		sseu->eu_per_subslice = max_t(unsigned int,
					      sseu->eu_per_subslice, eu_cnt);
	}
#undef SS_MAX
}

static void gen11_sseu_device_status(struct intel_gt *gt,
				     struct sseu_dev_info *sseu)
{
#define SS_MAX 8
	struct intel_uncore *uncore = gt->uncore;
	const struct intel_gt_info *info = &gt->info;
	u32 s_reg[SS_MAX], eu_reg[2 * SS_MAX], eu_mask[2];
	int s, ss;

	for (s = 0; s < info->sseu.max_slices; s++) {
		/*
		 * FIXME: Valid SS Mask respects the spec and read
		 * only valid bits for those registers, excluding reserved
		 * although this seems wrong because it would leave many
		 * subslices without ACK.
		 */
		s_reg[s] = intel_uncore_read(uncore, GEN10_SLICE_PGCTL_ACK(s)) &
			GEN10_PGCTL_VALID_SS_MASK(s);
		eu_reg[2 * s] = intel_uncore_read(uncore,
						  GEN10_SS01_EU_PGCTL_ACK(s));
		eu_reg[2 * s + 1] = intel_uncore_read(uncore,
						      GEN10_SS23_EU_PGCTL_ACK(s));
	}

	eu_mask[0] = GEN9_PGCTL_SSA_EU08_ACK |
		     GEN9_PGCTL_SSA_EU19_ACK |
		     GEN9_PGCTL_SSA_EU210_ACK |
		     GEN9_PGCTL_SSA_EU311_ACK;
	eu_mask[1] = GEN9_PGCTL_SSB_EU08_ACK |
		     GEN9_PGCTL_SSB_EU19_ACK |
		     GEN9_PGCTL_SSB_EU210_ACK |
		     GEN9_PGCTL_SSB_EU311_ACK;

	for (s = 0; s < info->sseu.max_slices; s++) {
		if ((s_reg[s] & GEN9_PGCTL_SLICE_ACK) == 0)
			/* skip disabled slice */
			continue;

		sseu->slice_mask |= BIT(s);
		sseu_copy_subslices(&info->sseu, s, sseu->subslice_mask);

		for (ss = 0; ss < info->sseu.max_subslices; ss++) {
			unsigned int eu_cnt;

			if (info->sseu.has_subslice_pg &&
			    !(s_reg[s] & (GEN9_PGCTL_SS_ACK(ss))))
				/* skip disabled subslice */
				continue;

			eu_cnt = 2 * hweight32(eu_reg[2 * s + ss / 2] &
					       eu_mask[ss % 2]);
			sseu->eu_total += eu_cnt;
			sseu->eu_per_subslice = max_t(unsigned int,
						      sseu->eu_per_subslice,
						      eu_cnt);
		}
	}
#undef SS_MAX
}

static void gen9_sseu_device_status(struct intel_gt *gt,
				    struct sseu_dev_info *sseu)
{
#define SS_MAX 3
	struct intel_uncore *uncore = gt->uncore;
	const struct intel_gt_info *info = &gt->info;
	u32 s_reg[SS_MAX], eu_reg[2 * SS_MAX], eu_mask[2];
	int s, ss;

	for (s = 0; s < info->sseu.max_slices; s++) {
		s_reg[s] = intel_uncore_read(uncore, GEN9_SLICE_PGCTL_ACK(s));
		eu_reg[2 * s] =
			intel_uncore_read(uncore, GEN9_SS01_EU_PGCTL_ACK(s));
		eu_reg[2 * s + 1] =
			intel_uncore_read(uncore, GEN9_SS23_EU_PGCTL_ACK(s));
	}

	eu_mask[0] = GEN9_PGCTL_SSA_EU08_ACK |
		     GEN9_PGCTL_SSA_EU19_ACK |
		     GEN9_PGCTL_SSA_EU210_ACK |
		     GEN9_PGCTL_SSA_EU311_ACK;
	eu_mask[1] = GEN9_PGCTL_SSB_EU08_ACK |
		     GEN9_PGCTL_SSB_EU19_ACK |
		     GEN9_PGCTL_SSB_EU210_ACK |
		     GEN9_PGCTL_SSB_EU311_ACK;

	for (s = 0; s < info->sseu.max_slices; s++) {
		if ((s_reg[s] & GEN9_PGCTL_SLICE_ACK) == 0)
			/* skip disabled slice */
			continue;

		sseu->slice_mask |= BIT(s);

		if (IS_GEN9_BC(gt->i915))
			sseu_copy_subslices(&info->sseu, s,
					    sseu->subslice_mask);

		for (ss = 0; ss < info->sseu.max_subslices; ss++) {
			unsigned int eu_cnt;
			u8 ss_idx = s * info->sseu.ss_stride +
				    ss / BITS_PER_BYTE;

			if (IS_GEN9_LP(gt->i915)) {
				if (!(s_reg[s] & (GEN9_PGCTL_SS_ACK(ss))))
					/* skip disabled subslice */
					continue;

				sseu->subslice_mask[ss_idx] |=
					BIT(ss % BITS_PER_BYTE);
			}

			eu_cnt = eu_reg[2 * s + ss / 2] & eu_mask[ss % 2];
			eu_cnt = 2 * hweight32(eu_cnt);

			sseu->eu_total += eu_cnt;
			sseu->eu_per_subslice = max_t(unsigned int,
						      sseu->eu_per_subslice,
						      eu_cnt);
		}
	}
#undef SS_MAX
}

static void bdw_sseu_device_status(struct intel_gt *gt,
				   struct sseu_dev_info *sseu)
{
	const struct intel_gt_info *info = &gt->info;
	u32 slice_info = intel_uncore_read(gt->uncore, GEN8_GT_SLICE_INFO);
	int s;

	sseu->slice_mask = slice_info & GEN8_LSLICESTAT_MASK;

	if (sseu->slice_mask) {
		sseu->eu_per_subslice = info->sseu.eu_per_subslice;
		for (s = 0; s < fls(sseu->slice_mask); s++)
			sseu_copy_subslices(&info->sseu, s,
					    sseu->subslice_mask);
		sseu->eu_total = sseu->eu_per_subslice *
				 intel_sseu_subslice_total(sseu);

		/* subtract fused off EU(s) from enabled slice(s) */
		for (s = 0; s < fls(sseu->slice_mask); s++) {
			u8 subslice_7eu = info->sseu.subslice_7eu[s];

			sseu->eu_total -= hweight8(subslice_7eu);
		}
	}
}

static void i915_print_sseu_info(struct seq_file *m,
				 bool is_available_info,
				 bool has_pooled_eu,
				 const struct sseu_dev_info *sseu)
{
	const char *type = is_available_info ? "Available" : "Enabled";
	int s;

	seq_printf(m, "  %s Slice Mask: %04x\n", type,
		   sseu->slice_mask);
	seq_printf(m, "  %s Slice Total: %u\n", type,
		   hweight8(sseu->slice_mask));
	seq_printf(m, "  %s Subslice Total: %u\n", type,
		   intel_sseu_subslice_total(sseu));
	for (s = 0; s < fls(sseu->slice_mask); s++) {
		seq_printf(m, "  %s Slice%i subslices: %u\n", type,
			   s, intel_sseu_subslices_per_slice(sseu, s));
	}
	seq_printf(m, "  %s EU Total: %u\n", type,
		   sseu->eu_total);
	seq_printf(m, "  %s EU Per Subslice: %u\n", type,
		   sseu->eu_per_subslice);

	if (!is_available_info)
		return;

	seq_printf(m, "  Has Pooled EU: %s\n", yesno(has_pooled_eu));
	if (has_pooled_eu)
		seq_printf(m, "  Min EU in pool: %u\n", sseu->min_eu_in_pool);

	seq_printf(m, "  Has Slice Power Gating: %s\n",
		   yesno(sseu->has_slice_pg));
	seq_printf(m, "  Has Subslice Power Gating: %s\n",
		   yesno(sseu->has_subslice_pg));
	seq_printf(m, "  Has EU Power Gating: %s\n",
		   yesno(sseu->has_eu_pg));
}

/*
 * this is called from top-level debugfs as well, so we can't get the gt from
 * the seq_file.
 */
int intel_sseu_status(struct seq_file *m, struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	const struct intel_gt_info *info = &gt->info;
	struct sseu_dev_info sseu;
	intel_wakeref_t wakeref;

	if (GRAPHICS_VER(i915) < 8)
		return -ENODEV;

	seq_puts(m, "SSEU Device Info\n");
	i915_print_sseu_info(m, true, HAS_POOLED_EU(i915), &info->sseu);

	seq_puts(m, "SSEU Device Status\n");
	memset(&sseu, 0, sizeof(sseu));
	intel_sseu_set_info(&sseu, info->sseu.max_slices,
			    info->sseu.max_subslices,
			    info->sseu.max_eus_per_subslice);

	with_intel_runtime_pm(&i915->runtime_pm, wakeref) {
		if (IS_CHERRYVIEW(i915))
			cherryview_sseu_device_status(gt, &sseu);
		else if (IS_BROADWELL(i915))
			bdw_sseu_device_status(gt, &sseu);
		else if (GRAPHICS_VER(i915) == 9)
			gen9_sseu_device_status(gt, &sseu);
		else if (GRAPHICS_VER(i915) >= 11)
			gen11_sseu_device_status(gt, &sseu);
	}

	i915_print_sseu_info(m, false, HAS_POOLED_EU(i915), &sseu);

	return 0;
}

static int sseu_status_show(struct seq_file *m, void *unused)
{
	struct intel_gt *gt = m->private;

	return intel_sseu_status(m, gt);
}
DEFINE_GT_DEBUGFS_ATTRIBUTE(sseu_status);

static int rcs_topology_show(struct seq_file *m, void *unused)
{
	struct intel_gt *gt = m->private;
	struct drm_printer p = drm_seq_file_printer(m);

	intel_sseu_print_topology(&gt->info.sseu, &p);

	return 0;
}
DEFINE_GT_DEBUGFS_ATTRIBUTE(rcs_topology);

void intel_sseu_debugfs_register(struct intel_gt *gt, struct dentry *root)
{
	static const struct debugfs_gt_file files[] = {
		{ "sseu_status", &sseu_status_fops, NULL },
		{ "rcs_topology", &rcs_topology_fops, NULL },
	};

	intel_gt_debugfs_register_files(root, files, ARRAY_SIZE(files), gt);
}
