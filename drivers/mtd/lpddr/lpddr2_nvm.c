/*
 * LPDDR2-NVM MTD driver. This module provides read, write, erase, lock/unlock
 * support for LPDDR2-NVM PCM memories
 *
 * Copyright © 2012 Micron Technology, Inc.
 *
 * Vincenzo Aliberti <vincenzo.aliberti@gmail.com>
 * Domenico Manna <domenico.manna@gmail.com>
 * Many thanks to Andrea Vigilante for initial enabling
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, __func__

#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mtd/map.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/err.h>

/* Parameters */
#define ERASE_BLOCKSIZE			(0x00020000/2)	/* in Word */
#define WRITE_BUFFSIZE			(0x00000400/2)	/* in Word */
#define OW_BASE_ADDRESS			0x00000000	/* OW offset */
#define BUS_WIDTH			0x00000020	/* x32 devices */

/* PFOW symbols address offset */
#define PFOW_QUERY_STRING_P		(0x0000/2)	/* in Word */
#define PFOW_QUERY_STRING_F		(0x0002/2)	/* in Word */
#define PFOW_QUERY_STRING_O		(0x0004/2)	/* in Word */
#define PFOW_QUERY_STRING_W		(0x0006/2)	/* in Word */

/* OW registers address */
#define CMD_CODE_OFS			(0x0080/2)	/* in Word */
#define CMD_DATA_OFS			(0x0084/2)	/* in Word */
#define CMD_ADD_L_OFS			(0x0088/2)	/* in Word */
#define CMD_ADD_H_OFS			(0x008A/2)	/* in Word */
#define MPR_L_OFS			(0x0090/2)	/* in Word */
#define MPR_H_OFS			(0x0092/2)	/* in Word */
#define CMD_EXEC_OFS			(0x00C0/2)	/* in Word */
#define STATUS_REG_OFS			(0x00CC/2)	/* in Word */
#define PRG_BUFFER_OFS			(0x0010/2)	/* in Word */

/* Datamask */
#define MR_CFGMASK			0x8000
#define SR_OK_DATAMASK			0x0080

/* LPDDR2-NVM Commands */
#define LPDDR2_NVM_LOCK			0x0061
#define LPDDR2_NVM_UNLOCK		0x0062
#define LPDDR2_NVM_SW_PROGRAM		0x0041
#define LPDDR2_NVM_SW_OVERWRITE		0x0042
#define LPDDR2_NVM_BUF_PROGRAM		0x00E9
#define LPDDR2_NVM_BUF_OVERWRITE	0x00EA
#define LPDDR2_NVM_ERASE		0x0020

/* LPDDR2-NVM Registers offset */
#define LPDDR2_MODE_REG_DATA		0x0040
#define LPDDR2_MODE_REG_CFG		0x0050

/*
 * Internal Type Definitions
 * pcm_int_data contains memory controller details:
 * @reg_data : LPDDR2_MODE_REG_DATA register address after remapping
 * @reg_cfg  : LPDDR2_MODE_REG_CFG register address after remapping
 * &bus_width: memory bus-width (eg: x16 2 Bytes, x32 4 Bytes)
 */
struct pcm_int_data {
	void __iomem *ctl_regs;
	int bus_width;
};

static DEFINE_MUTEX(lpdd2_nvm_mutex);

/*
 * Build a map_word starting from an u_long
 */
static inline map_word build_map_word(u_long myword)
{
	map_word val = { {0} };
	val.x[0] = myword;
	return val;
}

/*
 * Build Mode Register Configuration DataMask based on device bus-width
 */
static inline u_int build_mr_cfgmask(u_int bus_width)
{
	u_int val = MR_CFGMASK;

	if (bus_width == 0x0004)		/* x32 device */
		val = val << 16;

	return val;
}

/*
 * Build Status Register OK DataMask based on device bus-width
 */
static inline u_int build_sr_ok_datamask(u_int bus_width)
{
	u_int val = SR_OK_DATAMASK;

	if (bus_width == 0x0004)		/* x32 device */
		val = (val << 16)+val;

	return val;
}

/*
 * Evaluates Overlay Window Control Registers address
 */
static inline u_long ow_reg_add(struct map_info *map, u_long offset)
{
	u_long val = 0;
	struct pcm_int_data *pcm_data = map->fldrv_priv;

	val = map->pfow_base + offset*pcm_data->bus_width;

	return val;
}

/*
 * Enable lpddr2-nvm Overlay Window
 * Overlay Window is a memory mapped area containing all LPDDR2-NVM registers
 * used by device commands as well as uservisible resources like Device Status
 * Register, Device ID, etc
 */
static inline void ow_enable(struct map_info *map)
{
	struct pcm_int_data *pcm_data = map->fldrv_priv;

	writel_relaxed(build_mr_cfgmask(pcm_data->bus_width) | 0x18,
		pcm_data->ctl_regs + LPDDR2_MODE_REG_CFG);
	writel_relaxed(0x01, pcm_data->ctl_regs + LPDDR2_MODE_REG_DATA);
}

/*
 * Disable lpddr2-nvm Overlay Window
 * Overlay Window is a memory mapped area containing all LPDDR2-NVM registers
 * used by device commands as well as uservisible resources like Device Status
 * Register, Device ID, etc
 */
static inline void ow_disable(struct map_info *map)
{
	struct pcm_int_data *pcm_data = map->fldrv_priv;

	writel_relaxed(build_mr_cfgmask(pcm_data->bus_width) | 0x18,
		pcm_data->ctl_regs + LPDDR2_MODE_REG_CFG);
	writel_relaxed(0x02, pcm_data->ctl_regs + LPDDR2_MODE_REG_DATA);
}

/*
 * Execute lpddr2-nvm operations
 */
static int lpddr2_nvm_do_op(struct map_info *map, u_long cmd_code,
	u_long cmd_data, u_long cmd_add, u_long cmd_mpr, u_char *buf)
{
	map_word add_l = { {0} }, add_h = { {0} }, mpr_l = { {0} },
		mpr_h = { {0} }, data_l = { {0} }, cmd = { {0} },
		exec_cmd = { {0} }, sr;
	map_word data_h = { {0} };	/* only for 2x x16 devices stacked */
	u_long i, status_reg, prg_buff_ofs;
	struct pcm_int_data *pcm_data = map->fldrv_priv;
	u_int sr_ok_datamask = build_sr_ok_datamask(pcm_data->bus_width);

	/* Builds low and high words for OW Control Registers */
	add_l.x[0]	= cmd_add & 0x0000FFFF;
	add_h.x[0]	= (cmd_add >> 16) & 0x0000FFFF;
	mpr_l.x[0]	= cmd_mpr & 0x0000FFFF;
	mpr_h.x[0]	= (cmd_mpr >> 16) & 0x0000FFFF;
	cmd.x[0]	= cmd_code & 0x0000FFFF;
	exec_cmd.x[0]	= 0x0001;
	data_l.x[0]	= cmd_data & 0x0000FFFF;
	data_h.x[0]	= (cmd_data >> 16) & 0x0000FFFF; /* only for 2x x16 */

	/* Set Overlay Window Control Registers */
	map_write(map, cmd, ow_reg_add(map, CMD_CODE_OFS));
	map_write(map, data_l, ow_reg_add(map, CMD_DATA_OFS));
	map_write(map, add_l, ow_reg_add(map, CMD_ADD_L_OFS));
	map_write(map, add_h, ow_reg_add(map, CMD_ADD_H_OFS));
	map_write(map, mpr_l, ow_reg_add(map, MPR_L_OFS));
	map_write(map, mpr_h, ow_reg_add(map, MPR_H_OFS));
	if (pcm_data->bus_width == 0x0004) {	/* 2x16 devices stacked */
		map_write(map, cmd, ow_reg_add(map, CMD_CODE_OFS) + 2);
		map_write(map, data_h, ow_reg_add(map, CMD_DATA_OFS) + 2);
		map_write(map, add_l, ow_reg_add(map, CMD_ADD_L_OFS) + 2);
		map_write(map, add_h, ow_reg_add(map, CMD_ADD_H_OFS) + 2);
		map_write(map, mpr_l, ow_reg_add(map, MPR_L_OFS) + 2);
		map_write(map, mpr_h, ow_reg_add(map, MPR_H_OFS) + 2);
	}

	/* Fill Program Buffer */
	if ((cmd_code == LPDDR2_NVM_BUF_PROGRAM) ||
		(cmd_code == LPDDR2_NVM_BUF_OVERWRITE)) {
		prg_buff_ofs = (map_read(map,
			ow_reg_add(map, PRG_BUFFER_OFS))).x[0];
		for (i = 0; i < cmd_mpr; i++) {
			map_write(map, build_map_word(buf[i]), map->pfow_base +
			prg_buff_ofs + i);
		}
	}

	/* Command Execute */
	map_write(map, exec_cmd, ow_reg_add(map, CMD_EXEC_OFS));
	if (pcm_data->bus_width == 0x0004)	/* 2x16 devices stacked */
		map_write(map, exec_cmd, ow_reg_add(map, CMD_EXEC_OFS) + 2);

	/* Status Register Check */
	do {
		sr = map_read(map, ow_reg_add(map, STATUS_REG_OFS));
		status_reg = sr.x[0];
		if (pcm_data->bus_width == 0x0004) {/* 2x16 devices stacked */
			sr = map_read(map, ow_reg_add(map,
				STATUS_REG_OFS) + 2);
			status_reg += sr.x[0] << 16;
		}
	} while ((status_reg & sr_ok_datamask) != sr_ok_datamask);

	return (((status_reg & sr_ok_datamask) == sr_ok_datamask) ? 0 : -EIO);
}

/*
 * Execute lpddr2-nvm operations @ block level
 */
static int lpddr2_nvm_do_block_op(struct mtd_info *mtd, loff_t start_add,
	uint64_t len, u_char block_op)
{
	struct map_info *map = mtd->priv;
	u_long add, end_add;
	int ret = 0;

	mutex_lock(&lpdd2_nvm_mutex);

	ow_enable(map);

	add = start_add;
	end_add = add + len;

	do {
		ret = lpddr2_nvm_do_op(map, block_op, 0x00, add, add, NULL);
		if (ret)
			goto out;
		add += mtd->erasesize;
	} while (add < end_add);

out:
	ow_disable(map);
	mutex_unlock(&lpdd2_nvm_mutex);
	return ret;
}

/*
 * verify presence of PFOW string
 */
static int lpddr2_nvm_pfow_present(struct map_info *map)
{
	map_word pfow_val[4];
	unsigned int found = 1;

	mutex_lock(&lpdd2_nvm_mutex);

	ow_enable(map);

	/* Load string from array */
	pfow_val[0] = map_read(map, ow_reg_add(map, PFOW_QUERY_STRING_P));
	pfow_val[1] = map_read(map, ow_reg_add(map, PFOW_QUERY_STRING_F));
	pfow_val[2] = map_read(map, ow_reg_add(map, PFOW_QUERY_STRING_O));
	pfow_val[3] = map_read(map, ow_reg_add(map, PFOW_QUERY_STRING_W));

	/* Verify the string loaded vs expected */
	if (!map_word_equal(map, build_map_word('P'), pfow_val[0]))
		found = 0;
	if (!map_word_equal(map, build_map_word('F'), pfow_val[1]))
		found = 0;
	if (!map_word_equal(map, build_map_word('O'), pfow_val[2]))
		found = 0;
	if (!map_word_equal(map, build_map_word('W'), pfow_val[3]))
		found = 0;

	ow_disable(map);

	mutex_unlock(&lpdd2_nvm_mutex);

	return found;
}

/*
 * lpddr2_nvm driver read method
 */
static int lpddr2_nvm_read(struct mtd_info *mtd, loff_t start_add,
				size_t len, size_t *retlen, u_char *buf)
{
	struct map_info *map = mtd->priv;

	mutex_lock(&lpdd2_nvm_mutex);

	*retlen = len;

	map_copy_from(map, buf, start_add, *retlen);

	mutex_unlock(&lpdd2_nvm_mutex);
	return 0;
}

/*
 * lpddr2_nvm driver write method
 */
static int lpddr2_nvm_write(struct mtd_info *mtd, loff_t start_add,
				size_t len, size_t *retlen, const u_char *buf)
{
	struct map_info *map = mtd->priv;
	struct pcm_int_data *pcm_data = map->fldrv_priv;
	u_long add, current_len, tot_len, target_len, my_data;
	u_char *write_buf = (u_char *)buf;
	int ret = 0;

	mutex_lock(&lpdd2_nvm_mutex);

	ow_enable(map);

	/* Set start value for the variables */
	add = start_add;
	target_len = len;
	tot_len = 0;

	while (tot_len < target_len) {
		if (!(IS_ALIGNED(add, mtd->writesize))) { /* do sw program */
			my_data = write_buf[tot_len];
			my_data += (write_buf[tot_len+1]) << 8;
			if (pcm_data->bus_width == 0x0004) {/* 2x16 devices */
				my_data += (write_buf[tot_len+2]) << 16;
				my_data += (write_buf[tot_len+3]) << 24;
			}
			ret = lpddr2_nvm_do_op(map, LPDDR2_NVM_SW_OVERWRITE,
				my_data, add, 0x00, NULL);
			if (ret)
				goto out;

			add += pcm_data->bus_width;
			tot_len += pcm_data->bus_width;
		} else {		/* do buffer program */
			current_len = min(target_len - tot_len,
				(u_long) mtd->writesize);
			ret = lpddr2_nvm_do_op(map, LPDDR2_NVM_BUF_OVERWRITE,
				0x00, add, current_len, write_buf + tot_len);
			if (ret)
				goto out;

			add += current_len;
			tot_len += current_len;
		}
	}

out:
	*retlen = tot_len;
	ow_disable(map);
	mutex_unlock(&lpdd2_nvm_mutex);
	return ret;
}

/*
 * lpddr2_nvm driver erase method
 */
static int lpddr2_nvm_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	int ret = lpddr2_nvm_do_block_op(mtd, instr->addr, instr->len,
		LPDDR2_NVM_ERASE);
	if (!ret) {
		instr->state = MTD_ERASE_DONE;
		mtd_erase_callback(instr);
	}

	return ret;
}

/*
 * lpddr2_nvm driver unlock method
 */
static int lpddr2_nvm_unlock(struct mtd_info *mtd, loff_t start_add,
	uint64_t len)
{
	return lpddr2_nvm_do_block_op(mtd, start_add, len, LPDDR2_NVM_UNLOCK);
}

/*
 * lpddr2_nvm driver lock method
 */
static int lpddr2_nvm_lock(struct mtd_info *mtd, loff_t start_add,
	uint64_t len)
{
	return lpddr2_nvm_do_block_op(mtd, start_add, len, LPDDR2_NVM_LOCK);
}

/*
 * lpddr2_nvm driver probe method
 */
static int lpddr2_nvm_probe(struct platform_device *pdev)
{
	struct map_info *map;
	struct mtd_info *mtd;
	struct resource *add_range;
	struct resource *control_regs;
	struct pcm_int_data *pcm_data;

	/* Allocate memory control_regs data structures */
	pcm_data = devm_kzalloc(&pdev->dev, sizeof(*pcm_data), GFP_KERNEL);
	if (!pcm_data)
		return -ENOMEM;

	pcm_data->bus_width = BUS_WIDTH;

	/* Allocate memory for map_info & mtd_info data structures */
	map = devm_kzalloc(&pdev->dev, sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	mtd = devm_kzalloc(&pdev->dev, sizeof(*mtd), GFP_KERNEL);
	if (!mtd)
		return -ENOMEM;

	/* lpddr2_nvm address range */
	add_range = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	/* Populate map_info data structure */
	*map = (struct map_info) {
		.virt		= devm_ioremap_resource(&pdev->dev, add_range),
		.name		= pdev->dev.init_name,
		.phys		= add_range->start,
		.size		= resource_size(add_range),
		.bankwidth	= pcm_data->bus_width / 2,
		.pfow_base	= OW_BASE_ADDRESS,
		.fldrv_priv	= pcm_data,
	};
	if (IS_ERR(map->virt))
		return PTR_ERR(map->virt);

	simple_map_init(map);	/* fill with default methods */

	control_regs = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	pcm_data->ctl_regs = devm_ioremap_resource(&pdev->dev, control_regs);
	if (IS_ERR(pcm_data->ctl_regs))
		return PTR_ERR(pcm_data->ctl_regs);

	/* Populate mtd_info data structure */
	*mtd = (struct mtd_info) {
		.name		= pdev->dev.init_name,
		.type		= MTD_RAM,
		.priv		= map,
		.size		= resource_size(add_range),
		.erasesize	= ERASE_BLOCKSIZE * pcm_data->bus_width,
		.writesize	= 1,
		.writebufsize	= WRITE_BUFFSIZE * pcm_data->bus_width,
		.flags		= (MTD_CAP_NVRAM | MTD_POWERUP_LOCK),
		._read		= lpddr2_nvm_read,
		._write		= lpddr2_nvm_write,
		._erase		= lpddr2_nvm_erase,
		._unlock	= lpddr2_nvm_unlock,
		._lock		= lpddr2_nvm_lock,
	};

	/* Verify the presence of the device looking for PFOW string */
	if (!lpddr2_nvm_pfow_present(map)) {
		pr_err("device not recognized\n");
		return -EINVAL;
	}
	/* Parse partitions and register the MTD device */
	return mtd_device_parse_register(mtd, NULL, NULL, NULL, 0);
}

/*
 * lpddr2_nvm driver remove method
 */
static int lpddr2_nvm_remove(struct platform_device *pdev)
{
	return mtd_device_unregister(dev_get_drvdata(&pdev->dev));
}

/* Initialize platform_driver data structure for lpddr2_nvm */
static struct platform_driver lpddr2_nvm_drv = {
	.driver		= {
		.name	= "lpddr2_nvm",
	},
	.probe		= lpddr2_nvm_probe,
	.remove		= lpddr2_nvm_remove,
};

module_platform_driver(lpddr2_nvm_drv);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vincenzo Aliberti <vincenzo.aliberti@gmail.com>");
MODULE_DESCRIPTION("MTD driver for LPDDR2-NVM PCM memories");
