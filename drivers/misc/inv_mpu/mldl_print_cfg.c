/*
	$License:
	Copyright (C) 2011 InvenSense Corporation, All Rights Reserved.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
	$
 */

/**
 *  @addtogroup MLDL
 *
 *  @{
 *      @file   mldl_print_cfg.c
 *      @brief  The Motion Library Driver Layer.
 */

#include <stddef.h>
#include "mldl_cfg.h"
#include "mlsl.h"
#include "linux/mpu.h"

#include "log.h"
#undef MPL_LOG_TAG
#define MPL_LOG_TAG "mldl_print_cfg:"

#undef MPL_LOG_NDEBUG
#define MPL_LOG_NDEBUG 1

void mldl_print_cfg(struct mldl_cfg *mldl_cfg)
{
	struct mpu_gyro_cfg	*mpu_gyro_cfg	= mldl_cfg->mpu_gyro_cfg;
	struct mpu_offsets	*mpu_offsets	= mldl_cfg->mpu_offsets;
	struct mpu_chip_info	*mpu_chip_info	= mldl_cfg->mpu_chip_info;
	struct inv_mpu_cfg	*inv_mpu_cfg	= mldl_cfg->inv_mpu_cfg;
	struct inv_mpu_state	*inv_mpu_state	= mldl_cfg->inv_mpu_state;
	struct ext_slave_descr	**slave		= mldl_cfg->slave;
	struct mpu_platform_data *pdata		= mldl_cfg->pdata;
	struct ext_slave_platform_data **pdata_slave = mldl_cfg->pdata_slave;
	int ii;

	/* mpu_gyro_cfg */
	MPL_LOGV("int_config     = %02x\n", mpu_gyro_cfg->int_config);
	MPL_LOGV("ext_sync       = %02x\n", mpu_gyro_cfg->ext_sync);
	MPL_LOGV("full_scale     = %02x\n", mpu_gyro_cfg->full_scale);
	MPL_LOGV("lpf            = %02x\n", mpu_gyro_cfg->lpf);
	MPL_LOGV("clk_src        = %02x\n", mpu_gyro_cfg->clk_src);
	MPL_LOGV("divider        = %02x\n", mpu_gyro_cfg->divider);
	MPL_LOGV("dmp_enable     = %02x\n", mpu_gyro_cfg->dmp_enable);
	MPL_LOGV("fifo_enable    = %02x\n", mpu_gyro_cfg->fifo_enable);
	MPL_LOGV("dmp_cfg1       = %02x\n", mpu_gyro_cfg->dmp_cfg1);
	MPL_LOGV("dmp_cfg2       = %02x\n", mpu_gyro_cfg->dmp_cfg2);
	/* mpu_offsets */
	MPL_LOGV("tc[0]      = %02x\n", mpu_offsets->tc[0]);
	MPL_LOGV("tc[1]      = %02x\n", mpu_offsets->tc[1]);
	MPL_LOGV("tc[2]      = %02x\n", mpu_offsets->tc[2]);
	MPL_LOGV("gyro[0]    = %04x\n", mpu_offsets->gyro[0]);
	MPL_LOGV("gyro[1]    = %04x\n", mpu_offsets->gyro[1]);
	MPL_LOGV("gyro[2]    = %04x\n", mpu_offsets->gyro[2]);

	/* mpu_chip_info */
	MPL_LOGV("addr            = %02x\n", mldl_cfg->mpu_chip_info->addr);

	MPL_LOGV("silicon_revision = %02x\n", mpu_chip_info->silicon_revision);
	MPL_LOGV("product_revision = %02x\n", mpu_chip_info->product_revision);
	MPL_LOGV("product_id       = %02x\n", mpu_chip_info->product_id);
	MPL_LOGV("gyro_sens_trim   = %02x\n", mpu_chip_info->gyro_sens_trim);
	MPL_LOGV("accel_sens_trim  = %02x\n", mpu_chip_info->accel_sens_trim);

	MPL_LOGV("requested_sensors = %04x\n", inv_mpu_cfg->requested_sensors);
	MPL_LOGV("ignore_system_suspend= %04x\n",
		inv_mpu_cfg->ignore_system_suspend);
	MPL_LOGV("status = %04x\n", inv_mpu_state->status);
	MPL_LOGV("i2c_slaves_enabled= %04x\n",
		inv_mpu_state->i2c_slaves_enabled);

	for (ii = 0; ii < EXT_SLAVE_NUM_TYPES; ii++) {
		if (!slave[ii])
			continue;
		MPL_LOGV("SLAVE %d:\n", ii);
		MPL_LOGV("    suspend  = %02x\n", (int)slave[ii]->suspend);
		MPL_LOGV("    resume   = %02x\n", (int)slave[ii]->resume);
		MPL_LOGV("    read     = %02x\n", (int)slave[ii]->read);
		MPL_LOGV("    type     = %02x\n", slave[ii]->type);
		MPL_LOGV("    reg      = %02x\n", slave[ii]->read_reg);
		MPL_LOGV("    len      = %02x\n", slave[ii]->read_len);
		MPL_LOGV("    endian   = %02x\n", slave[ii]->endian);
		MPL_LOGV("    range.mantissa= %02x\n",
			slave[ii]->range.mantissa);
		MPL_LOGV("    range.fraction= %02x\n",
			slave[ii]->range.fraction);
	}

	for (ii = 0; ii < EXT_SLAVE_NUM_TYPES; ii++) {
		if (!pdata_slave[ii])
			continue;
		MPL_LOGV("PDATA_SLAVE[%d]\n", ii);
		MPL_LOGV("    irq        = %02x\n", pdata_slave[ii]->irq);
		MPL_LOGV("    adapt_num  = %02x\n", pdata_slave[ii]->adapt_num);
		MPL_LOGV("    bus        = %02x\n", pdata_slave[ii]->bus);
		MPL_LOGV("    address    = %02x\n", pdata_slave[ii]->address);
		MPL_LOGV("    orientation=\n"
			"                            %2d %2d %2d\n"
			"                            %2d %2d %2d\n"
			"                            %2d %2d %2d\n",
			pdata_slave[ii]->orientation[0],
			pdata_slave[ii]->orientation[1],
			pdata_slave[ii]->orientation[2],
			pdata_slave[ii]->orientation[3],
			pdata_slave[ii]->orientation[4],
			pdata_slave[ii]->orientation[5],
			pdata_slave[ii]->orientation[6],
			pdata_slave[ii]->orientation[7],
			pdata_slave[ii]->orientation[8]);
	}

	MPL_LOGV("pdata->int_config         = %02x\n", pdata->int_config);
	MPL_LOGV("pdata->level_shifter      = %02x\n", pdata->level_shifter);
	MPL_LOGV("pdata->orientation        =\n"
		 "                            %2d %2d %2d\n"
		 "                            %2d %2d %2d\n"
		 "                            %2d %2d %2d\n",
		 pdata->orientation[0], pdata->orientation[1],
		 pdata->orientation[2], pdata->orientation[3],
		 pdata->orientation[4], pdata->orientation[5],
		 pdata->orientation[6], pdata->orientation[7],
		 pdata->orientation[8]);
}
