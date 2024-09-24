/*
 *
 * (C) COPYRIGHT 2021-2023 Arm Limited.
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
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#include "ethosn_smc.h"

#include <linux/bug.h>

/* Compatible SiP service version */
#define ETHOSN_SIP_MAJOR_VERSION        4
#define ETHOSN_SIP_MINOR_VERSION        0

/* SMC functions */
#define ETHOSN_SMC_VERSION              0xc2000050
#define ETHOSN_SMC_IS_SECURE            0xc2000051
#define ETHOSN_SMC_CORE_HARD_RESET      0xc2000052
#define ETHOSN_SMC_CORE_SOFT_RESET      0xc2000053
#define ETHOSN_SMC_CORE_IS_SLEEPING     0xc2000054
#define ETHOSN_SMC_GET_FW_PROP          0xc2000055
#define ETHOSN_SMC_CORE_BOOT_FW         0xc2000056

/* Properties for ETHOSN_SMC_GET_FW_PROP */
#define ETHOSN_FW_PROP_VERSION          0xF00
#define ETHOSN_FW_PROP_MEM_INFO         0xF01
#define ETHOSN_FW_PROP_OFFSETS          0xF02
#define ETHOSN_FW_PROP_VA_MAP           0xF03

static inline int __must_check ethosn_smc_core_call(u32 cmd,
						    phys_addr_t core_addr,
						    struct arm_smccc_res *res)
{
	arm_smccc_smc(cmd, core_addr, 0, 0, 0, 0, 0, 0, res);

	/*
	 * Only use the first 32-bits of the response to handle an error from a
	 * 32-bit TF-A correctly.
	 */
	return ((int)(res->a0 & 0xFFFFFFFF));
}

static inline int __must_check ethosn_smc_core_reset_call(u32 cmd,
							  phys_addr_t core_addr,
							  uint32_t asset_alloc_idx,
							  bool halt,
							  bool is_protected,
							  u32 aux_config,
							  struct arm_smccc_res *res)
{
	arm_smccc_smc(cmd, core_addr, asset_alloc_idx, halt, is_protected,
		      aux_config, 0, 0, res);

	/*
	 * Only use the first 32-bits of the response to handle an error from a
	 * 32-bit TF-A correctly.
	 */
	return ((int)(res->a0 & 0xFFFFFFFF));
}

static inline int __must_check ethosn_smc_call(u32 cmd,
					       struct arm_smccc_res *res)
{
	return ethosn_smc_core_call(cmd, 0U, res);
}

int ethosn_smc_version_check(const struct device *dev)
{
	struct arm_smccc_res res = { 0 };
	int ret = ethosn_smc_call(ETHOSN_SMC_VERSION, &res);

	if (ret < 0) {
		dev_warn(dev, "Failed to get SiP service version: %d\n", ret);

		return -ENXIO;
	}

	if (res.a0 != ETHOSN_SIP_MAJOR_VERSION ||
	    res.a1 < ETHOSN_SIP_MINOR_VERSION) {
		dev_warn(dev, "Incompatible SiP service version: %lu.%lu\n",
			 res.a0, res.a1);

		return -EPROTO;
	}

	return 0;
}

int ethosn_smc_is_secure(const struct device *dev,
			 phys_addr_t core_addr)
{
	struct arm_smccc_res res = { 0 };
	int ret = ethosn_smc_core_call(ETHOSN_SMC_IS_SECURE, core_addr,
				       &res);

	if (ret < 0) {
		dev_err(dev, "Failed to get secure status: %d\n", ret);

		return -ENXIO;
	}

	if (res.a0 > 1U) {
		dev_err(dev, "Invalid secure status: %lu\n", res.a0);

		return -EPROTO;
	}

	return res.a0;
}

/* Exported for use by test module */
EXPORT_SYMBOL(ethosn_smc_is_secure);

int ethosn_smc_core_reset(const struct device *dev,
			  phys_addr_t core_addr,
			  uint32_t asset_alloc_idx,
			  bool halt,
			  bool hard_reset,
			  bool is_protected,
			  const struct ethosn_smc_aux_config *aux_config)
{
	struct arm_smccc_res res = { 0 };
	const u32 smc_reset_call = hard_reset ? ETHOSN_SMC_CORE_HARD_RESET :
				   ETHOSN_SMC_CORE_SOFT_RESET;
	int ret = ethosn_smc_core_reset_call(smc_reset_call, core_addr,
					     asset_alloc_idx, halt,
					     is_protected, aux_config->word,
					     &res);

	if (ret) {
		dev_warn(dev,
			 "Failed to %s%s reset the hardware in %scontext: %d\n",
			 hard_reset ? "hard" : "soft", halt ? " halt" : "",
			 is_protected ? "" : "non-",
			 ret);

		return -EFAULT;
	}

	return 0;
}

int ethosn_smc_core_is_sleeping(const struct device *dev,
				phys_addr_t core_addr)
{
	struct arm_smccc_res res = { 0 };
	int ret = ethosn_smc_core_call(ETHOSN_SMC_CORE_IS_SLEEPING, core_addr,
				       &res);

	if (WARN_ONCE(ret < 0, "Failed to get core sleep state: %d\n", ret))
		return -ENXIO;

	if (WARN_ONCE(res.a0 > 1, "Invalid core sleeping state: %lu\n", res.a0))
		return -EPROTO;

	return ret;
}

/* Exported for use by test module */
EXPORT_SYMBOL(ethosn_smc_core_is_sleeping);

#ifdef ETHOSN_TZMP1

int ethosn_smc_get_firmware_version(const struct device *dev,
				    uint32_t *out_major,
				    uint32_t *out_minor,
				    uint32_t *out_patch)
{
	struct arm_smccc_res res = { 0 };
	int ret = ethosn_smc_core_call(ETHOSN_SMC_GET_FW_PROP,
				       ETHOSN_FW_PROP_VERSION,
				       &res);

	if (ret < 0) {
		dev_err(dev,
			"Failed to get firmware version from SiP service: %d\n",
			ret);

		return -ENXIO;
	}

	*out_major = res.a1;
	*out_minor = res.a2;
	*out_patch = res.a3;

	return ret;
}

int ethosn_smc_get_firmware_mem_info(const struct device *dev,
				     phys_addr_t *out_addr,
				     size_t *out_size)
{
	struct arm_smccc_res res = { 0 };
	int ret = ethosn_smc_core_call(ETHOSN_SMC_GET_FW_PROP,
				       ETHOSN_FW_PROP_MEM_INFO,
				       &res);

	if (ret < 0) {
		dev_err(dev,
			"Failed to get firmware memory info from SiP service: %d\n",
			ret);

		return -ENXIO;
	}

	*out_addr = res.a1;
	*out_size = res.a2;

	return ret;
}

int ethosn_smc_get_firmware_offsets(const struct device *dev,
				    uint32_t *out_ple_offset,
				    uint32_t *out_stack_offset)
{
	struct arm_smccc_res res = { 0 };
	int ret = ethosn_smc_core_call(ETHOSN_SMC_GET_FW_PROP,
				       ETHOSN_FW_PROP_OFFSETS,
				       &res);

	if (ret < 0) {
		dev_err(dev,
			"Failed to get firmware offsets from SiP service: %d\n",
			ret);

		return -ENXIO;
	}

	*out_ple_offset = res.a1;
	*out_stack_offset = res.a2;

	return ret;
}

int ethosn_smc_get_firmware_va_map(const struct device *dev,
				   dma_addr_t *out_firmware_va,
				   dma_addr_t *out_working_data_va,
				   dma_addr_t *out_command_stream_va)
{
	struct arm_smccc_res res = { 0 };
	int ret = ethosn_smc_core_call(ETHOSN_SMC_GET_FW_PROP,
				       ETHOSN_FW_PROP_VA_MAP,
				       &res);

	if (ret < 0) {
		dev_err(dev,
			"Failed to get firmware virtual address map from SiP service: %d\n",
			ret);

		return -ENXIO;
	}

	*out_firmware_va = res.a1;
	*out_working_data_va = res.a2;
	*out_command_stream_va = res.a3;

	return ret;
}

int ethosn_smc_core_boot_firmware(const struct device *dev,
				  phys_addr_t core_addr)
{
	struct arm_smccc_res res = { 0 };
	int ret = ethosn_smc_core_call(ETHOSN_SMC_CORE_BOOT_FW,
				       core_addr,
				       &res);

	if (ret < 0) {
		dev_err(dev, "Failed to boot firmware on core: %d\n", ret);

		return -EFAULT;
	}

	return ret;
}

#endif
