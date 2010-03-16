/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2010 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>

#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-5000-hw.h"

/*
 * ucode
 */
static int iwlagn_load_section(struct iwl_priv *priv, const char *name,
				struct fw_desc *image, u32 dst_addr)
{
	dma_addr_t phy_addr = image->p_addr;
	u32 byte_cnt = image->len;
	int ret;

	priv->ucode_write_complete = 0;

	iwl_write_direct32(priv,
		FH_TCSR_CHNL_TX_CONFIG_REG(FH_SRVC_CHNL),
		FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE);

	iwl_write_direct32(priv,
		FH_SRVC_CHNL_SRAM_ADDR_REG(FH_SRVC_CHNL), dst_addr);

	iwl_write_direct32(priv,
		FH_TFDIB_CTRL0_REG(FH_SRVC_CHNL),
		phy_addr & FH_MEM_TFDIB_DRAM_ADDR_LSB_MSK);

	iwl_write_direct32(priv,
		FH_TFDIB_CTRL1_REG(FH_SRVC_CHNL),
		(iwl_get_dma_hi_addr(phy_addr)
			<< FH_MEM_TFDIB_REG1_ADDR_BITSHIFT) | byte_cnt);

	iwl_write_direct32(priv,
		FH_TCSR_CHNL_TX_BUF_STS_REG(FH_SRVC_CHNL),
		1 << FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_NUM |
		1 << FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_IDX |
		FH_TCSR_CHNL_TX_BUF_STS_REG_VAL_TFDB_VALID);

	iwl_write_direct32(priv,
		FH_TCSR_CHNL_TX_CONFIG_REG(FH_SRVC_CHNL),
		FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE	|
		FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_DISABLE	|
		FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_ENDTFD);

	IWL_DEBUG_INFO(priv, "%s uCode section being loaded...\n", name);
	ret = wait_event_interruptible_timeout(priv->wait_command_queue,
					priv->ucode_write_complete, 5 * HZ);
	if (ret == -ERESTARTSYS) {
		IWL_ERR(priv, "Could not load the %s uCode section due "
			"to interrupt\n", name);
		return ret;
	}
	if (!ret) {
		IWL_ERR(priv, "Could not load the %s uCode section\n",
			name);
		return -ETIMEDOUT;
	}

	return 0;
}

static int iwlagn_load_given_ucode(struct iwl_priv *priv,
		struct fw_desc *inst_image,
		struct fw_desc *data_image)
{
	int ret = 0;

	ret = iwlagn_load_section(priv, "INST", inst_image,
				   IWL50_RTC_INST_LOWER_BOUND);
	if (ret)
		return ret;

	return iwlagn_load_section(priv, "DATA", data_image,
				    IWL50_RTC_DATA_LOWER_BOUND);
}

int iwlagn_load_ucode(struct iwl_priv *priv)
{
	int ret = 0;

	/* check whether init ucode should be loaded, or rather runtime ucode */
	if (priv->ucode_init.len && (priv->ucode_type == UCODE_NONE)) {
		IWL_DEBUG_INFO(priv, "Init ucode found. Loading init ucode...\n");
		ret = iwlagn_load_given_ucode(priv,
			&priv->ucode_init, &priv->ucode_init_data);
		if (!ret) {
			IWL_DEBUG_INFO(priv, "Init ucode load complete.\n");
			priv->ucode_type = UCODE_INIT;
		}
	} else {
		IWL_DEBUG_INFO(priv, "Init ucode not found, or already loaded. "
			"Loading runtime ucode...\n");
		ret = iwlagn_load_given_ucode(priv,
			&priv->ucode_code, &priv->ucode_data);
		if (!ret) {
			IWL_DEBUG_INFO(priv, "Runtime ucode load complete.\n");
			priv->ucode_type = UCODE_RT;
		}
	}

	return ret;
}

#define IWL_UCODE_GET(item)						\
static u32 iwlagn_ucode_get_##item(const struct iwl_ucode_header *ucode,\
				    u32 api_ver)			\
{									\
	if (api_ver <= 2)						\
		return le32_to_cpu(ucode->u.v1.item);			\
	return le32_to_cpu(ucode->u.v2.item);				\
}

static u32 iwlagn_ucode_get_header_size(u32 api_ver)
{
	if (api_ver <= 2)
		return UCODE_HEADER_SIZE(1);
	return UCODE_HEADER_SIZE(2);
}

static u32 iwlagn_ucode_get_build(const struct iwl_ucode_header *ucode,
				   u32 api_ver)
{
	if (api_ver <= 2)
		return 0;
	return le32_to_cpu(ucode->u.v2.build);
}

static u8 *iwlagn_ucode_get_data(const struct iwl_ucode_header *ucode,
				  u32 api_ver)
{
	if (api_ver <= 2)
		return (u8 *) ucode->u.v1.data;
	return (u8 *) ucode->u.v2.data;
}

IWL_UCODE_GET(inst_size);
IWL_UCODE_GET(data_size);
IWL_UCODE_GET(init_size);
IWL_UCODE_GET(init_data_size);
IWL_UCODE_GET(boot_size);

struct iwl_ucode_ops iwlagn_ucode = {
	.get_header_size = iwlagn_ucode_get_header_size,
	.get_build = iwlagn_ucode_get_build,
	.get_inst_size = iwlagn_ucode_get_inst_size,
	.get_data_size = iwlagn_ucode_get_data_size,
	.get_init_size = iwlagn_ucode_get_init_size,
	.get_init_data_size = iwlagn_ucode_get_init_data_size,
	.get_boot_size = iwlagn_ucode_get_boot_size,
	.get_data = iwlagn_ucode_get_data,
};
