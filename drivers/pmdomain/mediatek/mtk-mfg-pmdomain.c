// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for MediaTek MFlexGraphics Devices
 *
 * Copyright (C) 2025, Collabora Ltd.
 */

#include <linux/completion.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/container_of.h>
#include <linux/iopoll.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <linux/regulator/consumer.h>
#include <linux/units.h>

#define GPR_LP_STATE		0x0028
#define   EB_ON_SUSPEND		0x0
#define   EB_ON_RESUME		0x1
#define GPR_IPI_MAGIC		0x34

#define RPC_PWR_CON		0x0504
#define   PWR_ACK_M		GENMASK(31, 30)
#define RPC_DUMMY_REG_2		0x0658
#define RPC_GHPM_CFG0_CON	0x0800
#define   GHPM_ENABLE_M		BIT(0)
#define   GHPM_ON_SEQ_M		BIT(2)
#define RPC_GHPM_RO0_CON	0x09A4
#define   GHPM_STATE_M		GENMASK(7, 0)
#define   GHPM_PWR_STATE_M	BIT(16)

#define GF_REG_MAGIC			0x0000
#define GF_REG_GPU_OPP_IDX		0x0004
#define GF_REG_STK_OPP_IDX		0x0008
#define GF_REG_GPU_OPP_NUM		0x000c
#define GF_REG_STK_OPP_NUM		0x0010
#define GF_REG_GPU_OPP_SNUM		0x0014
#define GF_REG_STK_OPP_SNUM		0x0018
#define GF_REG_POWER_COUNT		0x001c
#define GF_REG_BUCK_COUNT		0x0020
#define GF_REG_MTCMOS_COUNT		0x0024
#define GF_REG_CG_COUNT			0x0028 /* CG = Clock Gate? */
#define GF_REG_ACTIVE_COUNT		0x002C
#define GF_REG_TEMP_RAW			0x0030
#define GF_REG_TEMP_NORM_GPU		0x0034
#define GF_REG_TEMP_HIGH_GPU		0x0038
#define GF_REG_TEMP_NORM_STK		0x003C
#define GF_REG_TEMP_HIGH_STK		0x0040
#define GF_REG_FREQ_CUR_GPU		0x0044
#define GF_REG_FREQ_CUR_STK		0x0048
#define GF_REG_FREQ_OUT_GPU		0x004C /* Guess: actual achieved freq */
#define GF_REG_FREQ_OUT_STK		0x0050 /* Guess: actual achieved freq */
#define GF_REG_FREQ_METER_GPU		0x0054 /* Seems unused, always 0 */
#define GF_REG_FREQ_METER_STK		0x0058 /* Seems unused, always 0 */
#define GF_REG_VOLT_CUR_GPU		0x005C /* in tens of microvolts */
#define GF_REG_VOLT_CUR_STK		0x0060 /* in tens of microvolts */
#define GF_REG_VOLT_CUR_GPU_SRAM	0x0064
#define GF_REG_VOLT_CUR_STK_SRAM	0x0068
#define GF_REG_VOLT_CUR_GPU_REG		0x006C /* Seems unused, always 0 */
#define GF_REG_VOLT_CUR_STK_REG		0x0070 /* Seems unused, always 0 */
#define GF_REG_VOLT_CUR_GPU_REG_SRAM	0x0074
#define GF_REG_VOLT_CUR_STK_REG_SRAM	0x0078
#define GF_REG_PWR_CUR_GPU		0x007C /* in milliwatts */
#define GF_REG_PWR_CUR_STK		0x0080 /* in milliwatts */
#define GF_REG_PWR_MAX_GPU		0x0084 /* in milliwatts */
#define GF_REG_PWR_MAX_STK		0x0088 /* in milliwatts */
#define GF_REG_PWR_MIN_GPU		0x008C /* in milliwatts */
#define GF_REG_PWR_MIN_STK		0x0090 /* in milliwatts */
#define GF_REG_LEAKAGE_RT_GPU		0x0094 /* Unknown */
#define GF_REG_LEAKAGE_RT_STK		0x0098 /* Unknown */
#define GF_REG_LEAKAGE_RT_SRAM		0x009C /* Unknown */
#define GF_REG_LEAKAGE_HT_GPU		0x00A0 /* Unknown */
#define GF_REG_LEAKAGE_HT_STK		0x00A4 /* Unknown */
#define GF_REG_LEAKAGE_HT_SRAM		0x00A8 /* Unknown */
#define GF_REG_VOLT_DAC_LOW_GPU		0x00AC /* Seems unused, always 0 */
#define GF_REG_VOLT_DAC_LOW_STK		0x00B0 /* Seems unused, always 0 */
#define GF_REG_OPP_CUR_CEIL		0x00B4
#define GF_REG_OPP_CUR_FLOOR		0x00B8
#define GF_REG_OPP_CUR_LIMITER_CEIL	0x00BC
#define GF_REG_OPP_CUR_LIMITER_FLOOR	0x00C0
#define GF_REG_OPP_PRIORITY_CEIL	0x00C4
#define GF_REG_OPP_PRIORITY_FLOOR	0x00C8
#define GF_REG_PWR_CTL			0x00CC
#define GF_REG_ACTIVE_SLEEP_CTL		0x00D0
#define GF_REG_DVFS_STATE		0x00D4
#define GF_REG_SHADER_PRESENT		0x00D8
#define GF_REG_ASENSOR_ENABLE		0x00DC
#define GF_REG_AGING_LOAD		0x00E0
#define GF_REG_AGING_MARGIN		0x00E4
#define GF_REG_AVS_ENABLE		0x00E8
#define GF_REG_AVS_MARGIN		0x00EC
#define GF_REG_CHIP_TYPE		0x00F0
#define GF_REG_SB_VERSION		0x00F4
#define GF_REG_PTP_VERSION		0x00F8
#define GF_REG_DBG_VERSION		0x00FC
#define GF_REG_KDBG_VERSION		0x0100
#define GF_REG_GPM1_MODE		0x0104
#define GF_REG_GPM3_MODE		0x0108
#define GF_REG_DFD_MODE			0x010C
#define GF_REG_DUAL_BUCK		0x0110
#define GF_REG_SEGMENT_ID		0x0114
#define GF_REG_POWER_TIME_H		0x0118
#define GF_REG_POWER_TIME_L		0x011C
#define GF_REG_PWR_STATUS		0x0120
#define GF_REG_STRESS_TEST		0x0124
#define GF_REG_TEST_MODE		0x0128
#define GF_REG_IPS_MODE			0x012C
#define GF_REG_TEMP_COMP_MODE		0x0130
#define GF_REG_HT_TEMP_COMP_MODE	0x0134
#define GF_REG_PWR_TRACKER_MODE		0x0138
#define GF_REG_OPP_TABLE_GPU		0x0314
#define GF_REG_OPP_TABLE_STK		0x09A4
#define GF_REG_OPP_TABLE_GPU_S		0x1034
#define GF_REG_OPP_TABLE_STK_S		0x16c4
#define GF_REG_LIMIT_TABLE		0x1d54
#define GF_REG_GPM3_TABLE		0x223C

#define MFG_MT8196_E2_ID		0x101
#define GPUEB_SLEEP_MAGIC		0x55667788UL
#define GPUEB_MEM_MAGIC			0xBABADADAUL

#define GPUEB_TIMEOUT_US		10000UL
#define GPUEB_POLL_US			50

#define MAX_OPP_NUM			70

#define GPUEB_MBOX_MAX_RX_SIZE		32 /* in bytes */

/*
 * This enum is part of the ABI of the GPUEB firmware. Don't change the
 * numbering, as you would wreak havoc.
 */
enum mtk_mfg_ipi_cmd {
	CMD_INIT_SHARED_MEM		= 0,
	CMD_GET_FREQ_BY_IDX		= 1,
	CMD_GET_POWER_BY_IDX		= 2,
	CMD_GET_OPPIDX_BY_FREQ		= 3,
	CMD_GET_LEAKAGE_POWER		= 4,
	CMD_SET_LIMIT			= 5,
	CMD_POWER_CONTROL		= 6,
	CMD_ACTIVE_SLEEP_CONTROL	= 7,
	CMD_COMMIT			= 8,
	CMD_DUAL_COMMIT			= 9,
	CMD_PDCA_CONFIG			= 10,
	CMD_UPDATE_DEBUG_OPP_INFO	= 11,
	CMD_SWITCH_LIMIT		= 12,
	CMD_FIX_TARGET_OPPIDX		= 13,
	CMD_FIX_DUAL_TARGET_OPPIDX	= 14,
	CMD_FIX_CUSTOM_FREQ_VOLT	= 15,
	CMD_FIX_DUAL_CUSTOM_FREQ_VOLT	= 16,
	CMD_SET_MFGSYS_CONFIG		= 17,
	CMD_MSSV_COMMIT			= 18,
	CMD_NUM				= 19,
};

/*
 * This struct is part of the ABI of the GPUEB firmware. Changing it, or
 * reordering fields in it, will break things, so don't do it. Thank you.
 */
struct __packed mtk_mfg_ipi_msg {
	__le32 magic;
	__le32 cmd;
	__le32 target;
	/*
	 * Downstream relies on the compiler to implicitly add the following
	 * padding, as it declares the struct as non-packed.
	 */
	__le32 reserved;
	union {
		s32 __bitwise oppidx;
		s32 __bitwise return_value;
		__le32 freq;
		__le32 volt;
		__le32 power;
		__le32 power_state;
		__le32 mode;
		__le32 value;
		struct {
			__le64 base;
			__le32 size;
		} shared_mem;
		struct {
			__le32 freq;
			__le32 volt;
		} custom;
		struct {
			__le32 limiter;
			s32 __bitwise ceiling_info;
			s32 __bitwise floor_info;
		} set_limit;
		struct {
			__le32 target;
			__le32 val;
		} mfg_cfg;
		struct {
			__le32 target;
			__le32 val;
		} mssv;
		struct {
			s32 __bitwise gpu_oppidx;
			s32 __bitwise stack_oppidx;
		} dual_commit;
		struct {
			__le32 fgpu;
			__le32 vgpu;
			__le32 fstack;
			__le32 vstack;
		} dual_custom;
	} u;
};

struct __packed mtk_mfg_ipi_sleep_msg {
	__le32 event;
	__le32 state;
	__le32 magic;
};

/**
 * struct mtk_mfg_opp_entry - OPP table entry from firmware
 * @freq_khz: The operating point's frequency in kilohertz
 * @voltage_core: The operating point's core voltage in tens of microvolts
 * @voltage_sram: The operating point's SRAM voltage in tens of microvolts
 * @posdiv: exponent of base 2 for PLL frequency divisor used for this OPP
 * @voltage_margin: Number of tens of microvolts the voltage can be undershot
 * @power_mw: estimate of power usage at this operating point, in milliwatts
 *
 * This struct is part of the ABI with the EB firmware. Do not change it.
 */
struct __packed mtk_mfg_opp_entry {
	__le32 freq_khz;
	__le32 voltage_core;
	__le32 voltage_sram;
	__le32 posdiv;
	__le32 voltage_margin;
	__le32 power_mw;
};

struct mtk_mfg_mbox {
	struct mbox_client cl;
	struct completion rx_done;
	struct mtk_mfg *mfg;
	struct mbox_chan *ch;
	void *rx_data;
};

struct mtk_mfg {
	struct generic_pm_domain pd;
	struct platform_device *pdev;
	struct clk *clk_eb;
	struct clk_bulk_data *gpu_clks;
	struct clk_hw clk_core_hw;
	struct clk_hw clk_stack_hw;
	struct regulator_bulk_data *gpu_regs;
	void __iomem *rpc;
	void __iomem *gpr;
	void __iomem *shared_mem;
	phys_addr_t shared_mem_phys;
	unsigned int shared_mem_size;
	u16 ghpm_en_reg;
	u32 ipi_magic;
	unsigned short num_gpu_opps;
	unsigned short num_stack_opps;
	struct dev_pm_opp_data *gpu_opps;
	struct dev_pm_opp_data *stack_opps;
	struct mtk_mfg_mbox *gf_mbox;
	struct mtk_mfg_mbox *slp_mbox;
	const struct mtk_mfg_variant *variant;
};

struct mtk_mfg_variant {
	const char *const *clk_names;
	unsigned int num_clks;
	const char *const *regulator_names;
	unsigned int num_regulators;
	/** @turbo_below: opp indices below this value are considered turbo */
	unsigned int turbo_below;
	int (*init)(struct mtk_mfg *mfg);
};

static inline struct mtk_mfg *mtk_mfg_from_genpd(struct generic_pm_domain *pd)
{
	return container_of(pd, struct mtk_mfg, pd);
}

static inline void mtk_mfg_update_reg_bits(void __iomem *addr, u32 mask, u32 val)
{
	writel((readl(addr) & ~mask) | (val & mask), addr);
}

static inline bool mtk_mfg_is_powered_on(struct mtk_mfg *mfg)
{
	return (readl(mfg->rpc + RPC_PWR_CON) & PWR_ACK_M) == PWR_ACK_M;
}

static unsigned long mtk_mfg_recalc_rate_gpu(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct mtk_mfg *mfg = container_of(hw, struct mtk_mfg, clk_core_hw);

	return readl(mfg->shared_mem + GF_REG_FREQ_OUT_GPU) * HZ_PER_KHZ;
}

static int mtk_mfg_determine_rate(struct clk_hw *hw,
				  struct clk_rate_request *req)
{
	/*
	 * The determine_rate callback needs to be implemented to avoid returning
	 * the current clock frequency, rather than something even remotely
	 * close to the frequency that was asked for.
	 *
	 * Instead of writing considerable amounts of possibly slow code just to
	 * somehow figure out which of the three PLLs to round for, or even to
	 * do a search through one of two OPP tables in order to find the closest
	 * OPP of a frequency, just return the rate as-is. This avoids devfreq
	 * "rounding" a request for the lowest frequency to the possibly very
	 * high current frequency, breaking the powersave governor in the process.
	 */

	return 0;
}

static unsigned long mtk_mfg_recalc_rate_stack(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct mtk_mfg *mfg = container_of(hw, struct mtk_mfg, clk_stack_hw);

	return readl(mfg->shared_mem + GF_REG_FREQ_OUT_STK) * HZ_PER_KHZ;
}

static const struct clk_ops mtk_mfg_clk_gpu_ops = {
	.recalc_rate = mtk_mfg_recalc_rate_gpu,
	.determine_rate = mtk_mfg_determine_rate,
};

static const struct clk_ops mtk_mfg_clk_stack_ops = {
	.recalc_rate = mtk_mfg_recalc_rate_stack,
	.determine_rate = mtk_mfg_determine_rate,
};

static const struct clk_init_data mtk_mfg_clk_gpu_init = {
	.name = "gpu-core",
	.ops = &mtk_mfg_clk_gpu_ops,
	.flags = CLK_GET_RATE_NOCACHE,
};

static const struct clk_init_data mtk_mfg_clk_stack_init = {
	.name = "gpu-stack",
	.ops = &mtk_mfg_clk_stack_ops,
	.flags = CLK_GET_RATE_NOCACHE,
};

static int mtk_mfg_eb_on(struct mtk_mfg *mfg)
{
	struct device *dev = &mfg->pdev->dev;
	u32 val;
	int ret;

	/*
	 * If MFG is already on from e.g. the bootloader, skip doing the
	 * power-on sequence, as it wouldn't work without powering it off first.
	 */
	if (mtk_mfg_is_powered_on(mfg))
		return 0;

	ret = readl_poll_timeout(mfg->rpc + RPC_GHPM_RO0_CON, val,
				 !(val & (GHPM_PWR_STATE_M | GHPM_STATE_M)),
				 GPUEB_POLL_US, GPUEB_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "timed out waiting for EB to power on\n");
		return ret;
	}

	mtk_mfg_update_reg_bits(mfg->rpc + mfg->ghpm_en_reg, GHPM_ENABLE_M,
				GHPM_ENABLE_M);

	mtk_mfg_update_reg_bits(mfg->rpc + RPC_GHPM_CFG0_CON, GHPM_ON_SEQ_M, 0);
	mtk_mfg_update_reg_bits(mfg->rpc + RPC_GHPM_CFG0_CON, GHPM_ON_SEQ_M,
				GHPM_ON_SEQ_M);

	mtk_mfg_update_reg_bits(mfg->rpc + mfg->ghpm_en_reg, GHPM_ENABLE_M, 0);


	ret = readl_poll_timeout(mfg->rpc + RPC_PWR_CON, val,
				 (val & PWR_ACK_M) == PWR_ACK_M,
				 GPUEB_POLL_US, GPUEB_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "timed out waiting for EB power ack, val = 0x%X\n",
			val);
		return ret;
	}

	ret = readl_poll_timeout(mfg->gpr + GPR_LP_STATE, val,
				 (val == EB_ON_RESUME),
				 GPUEB_POLL_US, GPUEB_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "timed out waiting for EB to resume, status = 0x%X\n", val);
		return ret;
	}

	return 0;
}

static int mtk_mfg_eb_off(struct mtk_mfg *mfg)
{
	struct device *dev = &mfg->pdev->dev;
	struct mtk_mfg_ipi_sleep_msg msg = {
		.event = 0,
		.state = 0,
		.magic = GPUEB_SLEEP_MAGIC
	};
	u32 val;
	int ret;

	ret = mbox_send_message(mfg->slp_mbox->ch, &msg);
	if (ret < 0) {
		dev_err(dev, "Cannot send sleep command: %pe\n", ERR_PTR(ret));
		return ret;
	}

	ret = readl_poll_timeout(mfg->rpc + RPC_PWR_CON, val,
				 !(val & PWR_ACK_M), GPUEB_POLL_US,
				 GPUEB_TIMEOUT_US);

	if (ret) {
		dev_err(dev, "Timed out waiting for EB to power off, val=0x%08X\n", val);
		return ret;
	}

	return 0;
}

/**
 * mtk_mfg_send_ipi - synchronously send an IPI message on the gpufreq channel
 * @mfg: pointer to this driver instance's private &struct mtk_mfg
 * @msg: pointer to a message to send; will have magic filled and response assigned
 *
 * Send an IPI message on the gpufreq channel, and wait for a response. Once a
 * response is received, assign a pointer to the response buffer (valid until
 * next response is received) to @msg.
 *
 * Returns 0 on success, negative errno on failure.
 */
static int mtk_mfg_send_ipi(struct mtk_mfg *mfg, struct mtk_mfg_ipi_msg *msg)
{
	struct device *dev = &mfg->pdev->dev;
	unsigned long wait;
	int ret;

	msg->magic = mfg->ipi_magic;

	ret = mbox_send_message(mfg->gf_mbox->ch, msg);
	if (ret < 0) {
		dev_err(dev, "Cannot send GPUFreq IPI command: %pe\n", ERR_PTR(ret));
		return ret;
	}

	wait = wait_for_completion_timeout(&mfg->gf_mbox->rx_done, msecs_to_jiffies(500));
	if (!wait)
		return -ETIMEDOUT;

	msg = mfg->gf_mbox->rx_data;

	if (msg->u.return_value < 0) {
		dev_err(dev, "IPI return: %d\n", msg->u.return_value);
		return -EPROTO;
	}

	return 0;
}

static int mtk_mfg_init_shared_mem(struct mtk_mfg *mfg)
{
	struct device *dev = &mfg->pdev->dev;
	struct mtk_mfg_ipi_msg msg = {};
	int ret;

	dev_dbg(dev, "clearing GPUEB shared memory, 0x%X bytes\n", mfg->shared_mem_size);
	memset_io(mfg->shared_mem, 0, mfg->shared_mem_size);

	msg.cmd = CMD_INIT_SHARED_MEM;
	msg.u.shared_mem.base = mfg->shared_mem_phys;
	msg.u.shared_mem.size = mfg->shared_mem_size;

	ret = mtk_mfg_send_ipi(mfg, &msg);
	if (ret)
		return ret;

	if (readl(mfg->shared_mem + GF_REG_MAGIC) != GPUEB_MEM_MAGIC) {
		dev_err(dev, "EB did not initialise shared memory correctly\n");
		return -EIO;
	}

	return 0;
}

static int mtk_mfg_power_control(struct mtk_mfg *mfg, bool enabled)
{
	struct mtk_mfg_ipi_msg msg = {};

	msg.cmd = CMD_POWER_CONTROL;
	msg.u.power_state = enabled ? 1 : 0;

	return mtk_mfg_send_ipi(mfg, &msg);
}

static int mtk_mfg_set_oppidx(struct mtk_mfg *mfg, unsigned int opp_idx)
{
	struct mtk_mfg_ipi_msg msg = {};
	int ret;

	if (opp_idx >= mfg->num_gpu_opps)
		return -EINVAL;

	msg.cmd = CMD_FIX_DUAL_TARGET_OPPIDX;
	msg.u.dual_commit.gpu_oppidx = opp_idx;
	msg.u.dual_commit.stack_oppidx = opp_idx;

	ret = mtk_mfg_send_ipi(mfg, &msg);
	if (ret) {
		dev_err(&mfg->pdev->dev, "Failed to set OPP %u: %pe\n",
			opp_idx, ERR_PTR(ret));
		return ret;
	}

	return 0;
}

static int mtk_mfg_read_opp_tables(struct mtk_mfg *mfg)
{
	struct device *dev = &mfg->pdev->dev;
	struct mtk_mfg_opp_entry e = {};
	unsigned int i;

	mfg->num_gpu_opps = readl(mfg->shared_mem + GF_REG_GPU_OPP_NUM);
	mfg->num_stack_opps = readl(mfg->shared_mem + GF_REG_STK_OPP_NUM);

	if (mfg->num_gpu_opps > MAX_OPP_NUM || mfg->num_gpu_opps == 0) {
		dev_err(dev, "GPU OPP count (%u) out of range %u >= count > 0\n",
			mfg->num_gpu_opps, MAX_OPP_NUM);
		return -EINVAL;
	}

	if (mfg->num_stack_opps && mfg->num_stack_opps > MAX_OPP_NUM) {
		dev_err(dev, "Stack OPP count (%u) out of range %u >= count >= 0\n",
			mfg->num_stack_opps, MAX_OPP_NUM);
		return -EINVAL;
	}

	mfg->gpu_opps = devm_kcalloc(dev, mfg->num_gpu_opps,
				     sizeof(struct dev_pm_opp_data), GFP_KERNEL);
	if (!mfg->gpu_opps)
		return -ENOMEM;

	if (mfg->num_stack_opps) {
		mfg->stack_opps = devm_kcalloc(dev, mfg->num_stack_opps,
					       sizeof(struct dev_pm_opp_data), GFP_KERNEL);
		if (!mfg->stack_opps)
			return -ENOMEM;
	}

	for (i = 0; i < mfg->num_gpu_opps; i++) {
		memcpy_fromio(&e, mfg->shared_mem + GF_REG_OPP_TABLE_GPU + i * sizeof(e),
			      sizeof(e));
		if (mem_is_zero(&e, sizeof(e))) {
			dev_err(dev, "ran into an empty GPU OPP at index %u\n",
				i);
			return -EINVAL;
		}
		mfg->gpu_opps[i].freq = e.freq_khz * HZ_PER_KHZ;
		mfg->gpu_opps[i].u_volt = e.voltage_core * 10;
		mfg->gpu_opps[i].level = i;
		if (i < mfg->variant->turbo_below)
			mfg->gpu_opps[i].turbo = true;
	}

	for (i = 0; i < mfg->num_stack_opps; i++) {
		memcpy_fromio(&e, mfg->shared_mem + GF_REG_OPP_TABLE_STK + i * sizeof(e),
			      sizeof(e));
		if (mem_is_zero(&e, sizeof(e))) {
			dev_err(dev, "ran into an empty Stack OPP at index %u\n",
				i);
			return -EINVAL;
		}
		mfg->stack_opps[i].freq = e.freq_khz * HZ_PER_KHZ;
		mfg->stack_opps[i].u_volt = e.voltage_core * 10;
		mfg->stack_opps[i].level = i;
		if (i < mfg->variant->turbo_below)
			mfg->stack_opps[i].turbo = true;
	}

	return 0;
}

static const char *const mtk_mfg_mt8196_clk_names[] = {
	"core",
	"stack0",
	"stack1",
};

static const char *const mtk_mfg_mt8196_regulators[] = {
	"core",
	"stack",
	"sram",
};

static int mtk_mfg_mt8196_init(struct mtk_mfg *mfg)
{
	void __iomem *e2_base;

	e2_base = devm_platform_ioremap_resource_byname(mfg->pdev, "hw-revision");
	if (IS_ERR(e2_base))
		return dev_err_probe(&mfg->pdev->dev, PTR_ERR(e2_base),
				     "Couldn't get hw-revision register\n");

	clk_prepare_enable(mfg->clk_eb);

	if (readl(e2_base) == MFG_MT8196_E2_ID)
		mfg->ghpm_en_reg = RPC_DUMMY_REG_2;
	else
		mfg->ghpm_en_reg = RPC_GHPM_CFG0_CON;

	clk_disable_unprepare(mfg->clk_eb);

	return 0;
}

static const struct mtk_mfg_variant mtk_mfg_mt8196_variant = {
	.clk_names = mtk_mfg_mt8196_clk_names,
	.num_clks = ARRAY_SIZE(mtk_mfg_mt8196_clk_names),
	.regulator_names = mtk_mfg_mt8196_regulators,
	.num_regulators = ARRAY_SIZE(mtk_mfg_mt8196_regulators),
	.turbo_below = 7,
	.init = mtk_mfg_mt8196_init,
};

static void mtk_mfg_mbox_rx_callback(struct mbox_client *cl, void *mssg)
{
	struct mtk_mfg_mbox *mb = container_of(cl, struct mtk_mfg_mbox, cl);

	if (mb->rx_data)
		mb->rx_data = memcpy(mb->rx_data, mssg, GPUEB_MBOX_MAX_RX_SIZE);
	complete(&mb->rx_done);
}

static int mtk_mfg_attach_dev(struct generic_pm_domain *pd, struct device *dev)
{
	struct mtk_mfg *mfg = mtk_mfg_from_genpd(pd);
	struct dev_pm_opp_data *so = mfg->stack_opps;
	struct dev_pm_opp_data *go = mfg->gpu_opps;
	struct dev_pm_opp_data *prev_o;
	struct dev_pm_opp_data *o;
	int i, ret;

	for (i = mfg->num_gpu_opps - 1; i >= 0; i--) {
		/*
		 * Adding the lower of the two OPPs avoids gaps of indices in
		 * situations where the GPU OPPs are duplicated a couple of
		 * times when only the Stack OPP is being lowered at that index.
		 */
		if (i >= mfg->num_stack_opps || go[i].freq < so[i].freq)
			o = &go[i];
		else
			o = &so[i];

		/*
		 * Skip indices where both GPU and Stack OPPs are equal. Nominally,
		 * OPP core shouldn't care about dupes, but not doing so will cause
		 * dev_pm_opp_find_freq_ceil_indexed to -ERANGE later down the line.
		 */
		if (prev_o && prev_o->freq == o->freq)
			continue;

		ret = dev_pm_opp_add_dynamic(dev, o);
		if (ret) {
			dev_err(dev, "Failed to add OPP level %u from PD %s: %pe\n",
				o->level, pd->name, ERR_PTR(ret));
			dev_pm_opp_remove_all_dynamic(dev);
			return ret;
		}
		prev_o = o;
	}

	return 0;
}

static void mtk_mfg_detach_dev(struct generic_pm_domain *pd, struct device *dev)
{
	dev_pm_opp_remove_all_dynamic(dev);
}

static int mtk_mfg_set_performance(struct generic_pm_domain *pd,
				   unsigned int state)
{
	struct mtk_mfg *mfg = mtk_mfg_from_genpd(pd);

	/*
	 * pmdomain core intentionally sets a performance state before turning
	 * a domain on, and after turning it off. For the GPUEB however, it's
	 * only possible to act on performance requests when the GPUEB is
	 * powered on. To do this, return cleanly without taking action, and
	 * defer setting what pmdomain core set in mtk_mfg_power_on.
	 */
	if (mfg->pd.status != GENPD_STATE_ON)
		return 0;

	return mtk_mfg_set_oppidx(mfg, state);
}

static int mtk_mfg_power_on(struct generic_pm_domain *pd)
{
	struct mtk_mfg *mfg = mtk_mfg_from_genpd(pd);
	int ret;

	ret = regulator_bulk_enable(mfg->variant->num_regulators,
				    mfg->gpu_regs);
	if (ret)
		return ret;

	ret = clk_prepare_enable(mfg->clk_eb);
	if (ret)
		goto err_disable_regulators;

	ret = clk_bulk_prepare_enable(mfg->variant->num_clks, mfg->gpu_clks);
	if (ret)
		goto err_disable_eb_clk;

	ret = mtk_mfg_eb_on(mfg);
	if (ret)
		goto err_disable_clks;

	mfg->ipi_magic = readl(mfg->gpr + GPR_IPI_MAGIC);

	ret = mtk_mfg_power_control(mfg, true);
	if (ret)
		goto err_eb_off;

	/* Don't try to set a OPP in probe before OPPs have been read from EB */
	if (mfg->gpu_opps) {
		/* The aforementioned deferred setting of pmdomain's state */
		ret = mtk_mfg_set_oppidx(mfg, pd->performance_state);
		if (ret)
			dev_warn(&mfg->pdev->dev, "Failed to set oppidx in %s\n", __func__);
	}

	return 0;

err_eb_off:
	mtk_mfg_eb_off(mfg);
err_disable_clks:
	clk_bulk_disable_unprepare(mfg->variant->num_clks, mfg->gpu_clks);
err_disable_eb_clk:
	clk_disable_unprepare(mfg->clk_eb);
err_disable_regulators:
	regulator_bulk_disable(mfg->variant->num_regulators, mfg->gpu_regs);

	return ret;
}

static int mtk_mfg_power_off(struct generic_pm_domain *pd)
{
	struct mtk_mfg *mfg = mtk_mfg_from_genpd(pd);
	struct device *dev = &mfg->pdev->dev;
	int ret;

	ret = mtk_mfg_power_control(mfg, false);
	if (ret) {
		dev_err(dev, "power_control failed: %pe\n", ERR_PTR(ret));
		return ret;
	}

	ret = mtk_mfg_eb_off(mfg);
	if (ret) {
		dev_err(dev, "eb_off failed: %pe\n", ERR_PTR(ret));
		return ret;
	}

	clk_bulk_disable_unprepare(mfg->variant->num_clks, mfg->gpu_clks);
	clk_disable_unprepare(mfg->clk_eb);
	ret = regulator_bulk_disable(mfg->variant->num_regulators, mfg->gpu_regs);
	if (ret) {
		dev_err(dev, "Disabling regulators failed: %pe\n", ERR_PTR(ret));
		return ret;
	}

	return 0;
}

static int mtk_mfg_init_mbox(struct mtk_mfg *mfg)
{
	struct device *dev = &mfg->pdev->dev;
	struct mtk_mfg_mbox *gf;
	struct mtk_mfg_mbox *slp;

	gf = devm_kzalloc(dev, sizeof(*gf), GFP_KERNEL);
	if (!gf)
		return -ENOMEM;

	gf->rx_data = devm_kzalloc(dev, GPUEB_MBOX_MAX_RX_SIZE, GFP_KERNEL);
	if (!gf->rx_data)
		return -ENOMEM;

	gf->mfg = mfg;
	init_completion(&gf->rx_done);
	gf->cl.dev = dev;
	gf->cl.rx_callback = mtk_mfg_mbox_rx_callback;
	gf->cl.tx_tout = GPUEB_TIMEOUT_US / USEC_PER_MSEC;
	gf->ch = mbox_request_channel_byname(&gf->cl, "gpufreq");
	if (IS_ERR(gf->ch))
		return PTR_ERR(gf->ch);

	mfg->gf_mbox = gf;

	slp = devm_kzalloc(dev, sizeof(*slp), GFP_KERNEL);
	if (!slp)
		return -ENOMEM;

	slp->mfg = mfg;
	init_completion(&slp->rx_done);
	slp->cl.dev = dev;
	slp->cl.tx_tout = GPUEB_TIMEOUT_US / USEC_PER_MSEC;
	slp->cl.tx_block = true;
	slp->ch = mbox_request_channel_byname(&slp->cl, "sleep");
	if (IS_ERR(slp->ch)) {
		mbox_free_channel(gf->ch);
		return PTR_ERR(slp->ch);
	}

	mfg->slp_mbox = slp;

	return 0;
}

static int mtk_mfg_init_clk_provider(struct mtk_mfg *mfg)
{
	struct device *dev = &mfg->pdev->dev;
	struct clk_hw_onecell_data *clk_data;
	int ret;

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, 2), GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = 2;

	mfg->clk_core_hw.init = &mtk_mfg_clk_gpu_init;
	mfg->clk_stack_hw.init = &mtk_mfg_clk_stack_init;

	ret = devm_clk_hw_register(dev, &mfg->clk_core_hw);
	if (ret)
		return dev_err_probe(dev, ret, "Couldn't register GPU core clock\n");

	ret = devm_clk_hw_register(dev, &mfg->clk_stack_hw);
	if (ret)
		return dev_err_probe(dev, ret, "Couldn't register GPU stack clock\n");

	clk_data->hws[0] = &mfg->clk_core_hw;
	clk_data->hws[1] = &mfg->clk_stack_hw;

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, clk_data);
	if (ret)
		return dev_err_probe(dev, ret, "Couldn't register clock provider\n");

	return 0;
}

static int mtk_mfg_probe(struct platform_device *pdev)
{
	struct mtk_mfg *mfg;
	struct device *dev = &pdev->dev;
	const struct mtk_mfg_variant *data = of_device_get_match_data(dev);
	struct resource res;
	int ret, i;

	mfg = devm_kzalloc(dev, sizeof(*mfg), GFP_KERNEL);
	if (!mfg)
		return -ENOMEM;

	mfg->pdev = pdev;
	mfg->variant = data;

	dev_set_drvdata(dev, mfg);

	mfg->gpr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mfg->gpr))
		return dev_err_probe(dev, PTR_ERR(mfg->gpr),
				     "Couldn't retrieve GPR MMIO registers\n");

	mfg->rpc = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(mfg->rpc))
		return dev_err_probe(dev, PTR_ERR(mfg->rpc),
				     "Couldn't retrieve RPC MMIO registers\n");

	mfg->clk_eb = devm_clk_get(dev, "eb");
	if (IS_ERR(mfg->clk_eb))
		return dev_err_probe(dev, PTR_ERR(mfg->clk_eb),
				     "Couldn't get 'eb' clock\n");

	mfg->gpu_clks = devm_kcalloc(dev, data->num_clks, sizeof(*mfg->gpu_clks),
				     GFP_KERNEL);
	if (!mfg->gpu_clks)
		return -ENOMEM;

	for (i = 0; i < data->num_clks; i++)
		mfg->gpu_clks[i].id = data->clk_names[i];

	ret = devm_clk_bulk_get(dev, data->num_clks, mfg->gpu_clks);
	if (ret)
		return dev_err_probe(dev, ret, "Couldn't get GPU clocks\n");

	mfg->gpu_regs = devm_kcalloc(dev, data->num_regulators,
				     sizeof(*mfg->gpu_regs), GFP_KERNEL);
	if (!mfg->gpu_regs)
		return -ENOMEM;

	for (i = 0; i < data->num_regulators; i++)
		mfg->gpu_regs[i].supply = data->regulator_names[i];

	ret = devm_regulator_bulk_get(dev, data->num_regulators, mfg->gpu_regs);
	if (ret)
		return dev_err_probe(dev, ret, "Couldn't get GPU regulators\n");

	ret = of_reserved_mem_region_to_resource(dev->of_node, 0, &res);
	if (ret)
		return dev_err_probe(dev, ret, "Couldn't get GPUEB shared memory\n");

	mfg->shared_mem = devm_ioremap(dev, res.start, resource_size(&res));
	if (!mfg->shared_mem)
		return dev_err_probe(dev, -ENOMEM, "Can't ioremap GPUEB shared memory\n");
	mfg->shared_mem_size = resource_size(&res);
	mfg->shared_mem_phys = res.start;

	if (data->init) {
		ret = data->init(mfg);
		if (ret)
			return dev_err_probe(dev, ret, "Variant init failed\n");
	}

	mfg->pd.name = dev_name(dev);
	mfg->pd.attach_dev = mtk_mfg_attach_dev;
	mfg->pd.detach_dev = mtk_mfg_detach_dev;
	mfg->pd.power_off = mtk_mfg_power_off;
	mfg->pd.power_on = mtk_mfg_power_on;
	mfg->pd.set_performance_state = mtk_mfg_set_performance;
	mfg->pd.flags = GENPD_FLAG_OPP_TABLE_FW;

	ret = pm_genpd_init(&mfg->pd, NULL, false);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to initialise power domain\n");

	ret = mtk_mfg_init_mbox(mfg);
	if (ret) {
		dev_err_probe(dev, ret, "Couldn't initialise mailbox\n");
		goto err_remove_genpd;
	}

	ret = mtk_mfg_power_on(&mfg->pd);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to power on MFG\n");
		goto err_free_mbox;
	}

	ret = mtk_mfg_init_shared_mem(mfg);
	if (ret) {
		dev_err_probe(dev, ret, "Couldn't initialize EB shared memory\n");
		goto err_power_off;
	}

	ret = mtk_mfg_read_opp_tables(mfg);
	if (ret) {
		dev_err_probe(dev, ret, "Error reading OPP tables from EB\n");
		goto err_power_off;
	}

	ret = mtk_mfg_init_clk_provider(mfg);
	if (ret)
		goto err_power_off;

	ret = of_genpd_add_provider_simple(dev->of_node, &mfg->pd);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to add pmdomain provider\n");
		goto err_power_off;
	}

	return 0;

err_power_off:
	mtk_mfg_power_off(&mfg->pd);
err_free_mbox:
	mbox_free_channel(mfg->slp_mbox->ch);
	mfg->slp_mbox->ch = NULL;
	mbox_free_channel(mfg->gf_mbox->ch);
	mfg->gf_mbox->ch = NULL;
err_remove_genpd:
	pm_genpd_remove(&mfg->pd);

	return ret;
}

static const struct of_device_id mtk_mfg_of_match[] = {
	{ .compatible = "mediatek,mt8196-gpufreq", .data = &mtk_mfg_mt8196_variant },
	{}
};
MODULE_DEVICE_TABLE(of, mtk_mfg_of_match);

static void mtk_mfg_remove(struct platform_device *pdev)
{
	struct mtk_mfg *mfg = dev_get_drvdata(&pdev->dev);

	if (mtk_mfg_is_powered_on(mfg))
		mtk_mfg_power_off(&mfg->pd);

	of_genpd_del_provider(pdev->dev.of_node);
	pm_genpd_remove(&mfg->pd);

	mbox_free_channel(mfg->gf_mbox->ch);
	mfg->gf_mbox->ch = NULL;

	mbox_free_channel(mfg->slp_mbox->ch);
	mfg->slp_mbox->ch = NULL;
}

static struct platform_driver mtk_mfg_driver = {
	.driver = {
		.name = "mtk-mfg-pmdomain",
		.of_match_table = mtk_mfg_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = mtk_mfg_probe,
	.remove = mtk_mfg_remove,
};
module_platform_driver(mtk_mfg_driver);

MODULE_AUTHOR("Nicolas Frattaroli <nicolas.frattaroli@collabora.com>");
MODULE_DESCRIPTION("MediaTek MFlexGraphics Power Domain Driver");
MODULE_LICENSE("GPL");
