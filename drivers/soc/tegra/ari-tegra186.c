// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/arm-smccc.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/panic_notifier.h>

#define SMC_SIP_INVOKE_MCE			0xc2ffff00
#define MCE_SMC_READ_MCA			12

#define MCA_ARI_CMD_RD_SERR			1

#define MCA_ARI_RW_SUBIDX_STAT			1
#define SERR_STATUS_VAL				BIT_ULL(63)

#define MCA_ARI_RW_SUBIDX_ADDR			2
#define MCA_ARI_RW_SUBIDX_MSC1			3
#define MCA_ARI_RW_SUBIDX_MSC2			4

static const char * const bank_names[] = {
	"SYS:DPMU", "ROC:IOB", "ROC:MCB", "ROC:CCE", "ROC:CQX", "ROC:CTU",
};

static void read_uncore_mca(u8 cmd, u8 idx, u8 subidx, u8 inst, u64 *data)
{
	struct arm_smccc_res res;

	arm_smccc_smc(SMC_SIP_INVOKE_MCE | MCE_SMC_READ_MCA,
		      ((u64)inst << 24) | ((u64)idx << 16) |
			      ((u64)subidx << 8) | ((u64)cmd << 0),
		      0, 0, 0, 0, 0, 0, &res);

	*data = res.a2;
}

static int tegra186_ari_panic_handler(struct notifier_block *nb,
				      unsigned long code, void *unused)
{
	u64 status;
	int i;

	for (i = 0; i < ARRAY_SIZE(bank_names); i++) {
		read_uncore_mca(MCA_ARI_CMD_RD_SERR, i, MCA_ARI_RW_SUBIDX_STAT,
				0, &status);

		if (status & SERR_STATUS_VAL) {
			u64 addr, misc1, misc2;

			read_uncore_mca(MCA_ARI_CMD_RD_SERR, i,
					MCA_ARI_RW_SUBIDX_ADDR, 0, &addr);
			read_uncore_mca(MCA_ARI_CMD_RD_SERR, i,
					MCA_ARI_RW_SUBIDX_MSC1, 0, &misc1);
			read_uncore_mca(MCA_ARI_CMD_RD_SERR, i,
					MCA_ARI_RW_SUBIDX_MSC2, 0, &misc2);

			pr_crit("Machine Check Error in %s\n"
				"  status=0x%llx addr=0x%llx\n"
				"  msc1=0x%llx msc2=0x%llx\n",
				bank_names[i], status, addr, misc1, misc2);
		}
	}

	return NOTIFY_DONE;
}

static struct notifier_block tegra186_ari_panic_nb = {
	.notifier_call = tegra186_ari_panic_handler,
};

static int __init tegra186_ari_init(void)
{
	if (of_machine_is_compatible("nvidia,tegra186"))
		atomic_notifier_chain_register(&panic_notifier_list, &tegra186_ari_panic_nb);

	return 0;
}
early_initcall(tegra186_ari_init);
