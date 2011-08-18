/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
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
#include "iwl-helpers.h"
#include "iwl-4965-hw.h"
#include "iwl-4965.h"
#include "iwl-4965-calib.h"

#define IL_AC_UNSET -1

/**
 * il_verify_inst_sparse - verify runtime uCode image in card vs. host,
 *   using sample data 100 bytes apart.  If these sample points are good,
 *   it's a pretty good bet that everything between them is good, too.
 */
static int
il4965_verify_inst_sparse(struct il_priv *il, __le32 *image, u32 len)
{
	u32 val;
	int ret = 0;
	u32 errcnt = 0;
	u32 i;

	D_INFO("ucode inst image size is %u\n", len);

	for (i = 0; i < len; i += 100, image += 100/sizeof(u32)) {
		/* read data comes through single port, auto-incr addr */
		/* NOTE: Use the debugless read so we don't flood kernel log
		 * if IL_DL_IO is set */
		il_write_direct32(il, HBUS_TARG_MEM_RADDR,
			i + IWL4965_RTC_INST_LOWER_BOUND);
		val = _il_read_direct32(il, HBUS_TARG_MEM_RDAT);
		if (val != le32_to_cpu(*image)) {
			ret = -EIO;
			errcnt++;
			if (errcnt >= 3)
				break;
		}
	}

	return ret;
}

/**
 * il4965_verify_inst_full - verify runtime uCode image in card vs. host,
 *     looking at all data.
 */
static int il4965_verify_inst_full(struct il_priv *il, __le32 *image,
				 u32 len)
{
	u32 val;
	u32 save_len = len;
	int ret = 0;
	u32 errcnt;

	D_INFO("ucode inst image size is %u\n", len);

	il_write_direct32(il, HBUS_TARG_MEM_RADDR,
			   IWL4965_RTC_INST_LOWER_BOUND);

	errcnt = 0;
	for (; len > 0; len -= sizeof(u32), image++) {
		/* read data comes through single port, auto-incr addr */
		/* NOTE: Use the debugless read so we don't flood kernel log
		 * if IL_DL_IO is set */
		val = _il_read_direct32(il, HBUS_TARG_MEM_RDAT);
		if (val != le32_to_cpu(*image)) {
			IL_ERR("uCode INST section is invalid at "
				  "offset 0x%x, is 0x%x, s/b 0x%x\n",
				  save_len - len, val, le32_to_cpu(*image));
			ret = -EIO;
			errcnt++;
			if (errcnt >= 20)
				break;
		}
	}

	if (!errcnt)
		D_INFO(
		    "ucode image in INSTRUCTION memory is good\n");

	return ret;
}

/**
 * il4965_verify_ucode - determine which instruction image is in SRAM,
 *    and verify its contents
 */
int il4965_verify_ucode(struct il_priv *il)
{
	__le32 *image;
	u32 len;
	int ret;

	/* Try bootstrap */
	image = (__le32 *)il->ucode_boot.v_addr;
	len = il->ucode_boot.len;
	ret = il4965_verify_inst_sparse(il, image, len);
	if (!ret) {
		D_INFO("Bootstrap uCode is good in inst SRAM\n");
		return 0;
	}

	/* Try initialize */
	image = (__le32 *)il->ucode_init.v_addr;
	len = il->ucode_init.len;
	ret = il4965_verify_inst_sparse(il, image, len);
	if (!ret) {
		D_INFO("Initialize uCode is good in inst SRAM\n");
		return 0;
	}

	/* Try runtime/protocol */
	image = (__le32 *)il->ucode_code.v_addr;
	len = il->ucode_code.len;
	ret = il4965_verify_inst_sparse(il, image, len);
	if (!ret) {
		D_INFO("Runtime uCode is good in inst SRAM\n");
		return 0;
	}

	IL_ERR("NO VALID UCODE IMAGE IN INSTRUCTION SRAM!!\n");

	/* Since nothing seems to match, show first several data entries in
	 * instruction SRAM, so maybe visual inspection will give a clue.
	 * Selection of bootstrap image (vs. other images) is arbitrary. */
	image = (__le32 *)il->ucode_boot.v_addr;
	len = il->ucode_boot.len;
	ret = il4965_verify_inst_full(il, image, len);

	return ret;
}
