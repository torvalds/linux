// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/soc/qcom/llcc-qcom.h>

/*
 * SCT(System Cache Table) entry contains of the following members:
 * usecase_id: Unique id for the client's use case
 * slice_id: llcc slice id for each client
 * max_cap: The maximum capacity of the cache slice provided in KB
 * priority: Priority of the client used to select victim line for replacement
 * fixed_size: Boolean indicating if the slice has a fixed capacity
 * bonus_ways: Bonus ways are additional ways to be used for any slice,
 *		if client ends up using more than reserved cache ways. Bonus
 *		ways are allocated only if they are not reserved for some
 *		other client.
 * res_ways: Reserved ways for the cache slice, the reserved ways cannot
 *		be used by any other client than the one its assigned to.
 * cache_mode: Each slice operates as a cache, this controls the mode of the
 *             slice: normal or TCM(Tightly Coupled Memory)
 * probe_target_ways: Determines what ways to probe for access hit. When
 *                    configured to 1 only bonus and reserved ways are probed.
 *                    When configured to 0 all ways in llcc are probed.
 * dis_cap_alloc: Disable capacity based allocation for a client
 * retain_on_pc: If this bit is set and client has maintained active vote
 *               then the ways assigned to this client are not flushed on power
 *               collapse.
 * activate_on_init: Activate the slice immediately after the SCT is programmed
 */
#define SCT_ENTRY(uid, sid, mc, p, fs, bway, rway, cmod, ptw, dca, rp, a) \
	{					\
		.usecase_id = uid,		\
		.slice_id = sid,		\
		.max_cap = mc,			\
		.priority = p,			\
		.fixed_size = fs,		\
		.bonus_ways = bway,		\
		.res_ways = rway,		\
		.cache_mode = cmod,		\
		.probe_target_ways = ptw,	\
		.dis_cap_alloc = dca,		\
		.retain_on_pc = rp,		\
		.activate_on_init = a,		\
	}

static struct llcc_slice_config sdm845_data[] =  {
	SCT_ENTRY(LLCC_CPUSS,    1,  2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 1),
	SCT_ENTRY(LLCC_VIDSC0,   2,  512,  2, 1, 0x0,   0x0f0, 0, 0, 1, 1, 0),
	SCT_ENTRY(LLCC_VIDSC1,   3,  512,  2, 1, 0x0,   0x0f0, 0, 0, 1, 1, 0),
	SCT_ENTRY(LLCC_ROTATOR,  4,  563,  2, 1, 0x0,   0x00e, 2, 0, 1, 1, 0),
	SCT_ENTRY(LLCC_VOICE,    5,  2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0),
	SCT_ENTRY(LLCC_AUDIO,    6,  2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0),
	SCT_ENTRY(LLCC_MDMHPGRW, 7,  1024, 2, 0, 0xfc,  0xf00, 0, 0, 1, 1, 0),
	SCT_ENTRY(LLCC_MDM,      8,  2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0),
	SCT_ENTRY(LLCC_CMPT,     10, 2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0),
	SCT_ENTRY(LLCC_GPUHTW,   11, 512,  1, 1, 0xc,   0x0,   0, 0, 1, 1, 0),
	SCT_ENTRY(LLCC_GPU,      12, 2304, 1, 0, 0xff0, 0x2,   0, 0, 1, 1, 0),
	SCT_ENTRY(LLCC_MMUHWT,   13, 256,  2, 0, 0x0,   0x1,   0, 0, 1, 0, 1),
	SCT_ENTRY(LLCC_CMPTDMA,  15, 2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0),
	SCT_ENTRY(LLCC_DISP,     16, 2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0),
	SCT_ENTRY(LLCC_VIDFW,    17, 2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0),
	SCT_ENTRY(LLCC_MDMHPFX,  20, 1024, 2, 1, 0x0,   0xf00, 0, 0, 1, 1, 0),
	SCT_ENTRY(LLCC_MDMPNG,   21, 1024, 0, 1, 0x1e,  0x0,   0, 0, 1, 1, 0),
	SCT_ENTRY(LLCC_AUDHW,    22, 1024, 1, 1, 0xffc, 0x2,   0, 0, 1, 1, 0),
};

static int sdm845_qcom_llcc_remove(struct platform_device *pdev)
{
	return qcom_llcc_remove(pdev);
}

static int sdm845_qcom_llcc_probe(struct platform_device *pdev)
{
	return qcom_llcc_probe(pdev, sdm845_data, ARRAY_SIZE(sdm845_data));
}

static const struct of_device_id sdm845_qcom_llcc_of_match[] = {
	{ .compatible = "qcom,sdm845-llcc", },
	{ }
};

static struct platform_driver sdm845_qcom_llcc_driver = {
	.driver = {
		.name = "sdm845-llcc",
		.of_match_table = sdm845_qcom_llcc_of_match,
	},
	.probe = sdm845_qcom_llcc_probe,
	.remove = sdm845_qcom_llcc_remove,
};
module_platform_driver(sdm845_qcom_llcc_driver);

MODULE_DESCRIPTION("QCOM sdm845 LLCC driver");
MODULE_LICENSE("GPL v2");
