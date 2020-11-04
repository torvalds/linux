/*
 *  linux/drivers/mmchost/rkemmc_ops.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/mmc/core.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/slab.h>

#include <linux/scatterlist.h>
#include <linux/swap.h>		/* For nr_free_buffer_pages() */
#include <linux/list.h>

#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include "../core/block.h"
#include "../core/card.h"
#include "../core/core.h"
#include "../core/mmc_ops.h"

#define BLKSZ		512

enum emmc_area_type {
	MMC_DATA_AREA_MAIN,
	MMC_DATA_AREA_BOOT1,
	MMC_DATA_AREA_BOOT2,
	MMC_DATA_AREA_RPMB,
};

static int rk_emmc_set_areatype(enum emmc_area_type areatype)
{
	int err;
	u8 part_config;

	part_config = this_card->ext_csd.part_config;
	part_config &= ~EXT_CSD_PART_CONFIG_ACC_MASK;
	part_config |= (u8)areatype;
	err = mmc_switch(this_card, EXT_CSD_CMD_SET_NORMAL,
			 EXT_CSD_PART_CONFIG, part_config,
			 this_card->ext_csd.part_time);

	return err;
}

/*
 * Fill in the mmc_request structure given a set of transfer parameters.
 */
static void rk_emmc_prepare_mrq(struct mmc_request *mrq, struct scatterlist *sg, 
		unsigned sg_len, unsigned dev_addr, unsigned blocks, unsigned blksz, int write)
{
	BUG_ON(!mrq || !mrq->cmd || !mrq->data || !mrq->stop);

	if (blocks > 1) {
		mrq->cmd->opcode = write ?
			MMC_WRITE_MULTIPLE_BLOCK : MMC_READ_MULTIPLE_BLOCK;
	} else {
		mrq->cmd->opcode = write ?
			MMC_WRITE_BLOCK : MMC_READ_SINGLE_BLOCK;
	}

	mrq->cmd->arg = dev_addr;
	if (!mmc_card_blockaddr(this_card))
		mrq->cmd->arg <<= 9;

	mrq->cmd->flags = MMC_RSP_R1 | MMC_CMD_ADTC;

	if (blocks == 1)
		mrq->stop = NULL;
	else {
		mrq->stop->opcode = MMC_STOP_TRANSMISSION;
		mrq->stop->arg = 0;
		mrq->stop->flags = MMC_RSP_R1B | MMC_CMD_AC;
	}

	mrq->data->blksz = blksz;
	mrq->data->blocks = blocks;
	mrq->data->flags = write ? MMC_DATA_WRITE : MMC_DATA_READ;
	mrq->data->sg = sg;
	mrq->data->sg_len = sg_len;
	mmc_set_data_timeout(mrq->data, this_card);
}

static int rk_emmc_busy(struct mmc_command *cmd)
{
	return !(cmd->resp[0] & R1_READY_FOR_DATA) ||
		(R1_CURRENT_STATE(cmd->resp[0]) == 7);
}

/*
 * Wait for the card to finish the busy state
 */
static int rk_emmc_wait_busy(void)
{
	int ret, busy;
	struct mmc_command cmd = {0};

	busy = 0;
	do {
		memset(&cmd, 0, sizeof(struct mmc_command));

		cmd.opcode = MMC_SEND_STATUS;
		cmd.arg = this_card->rca << 16;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;

		ret = mmc_wait_for_cmd(this_card->host, &cmd, 0);
		if (ret)
			break;

		if (!busy && rk_emmc_busy(&cmd)) {
			busy = 1;
			if (this_card->host->caps & MMC_CAP_WAIT_WHILE_BUSY)
				pr_info("%s: Warning: Host did not "
					"wait for busy state to end.\n",
					mmc_hostname(this_card->host));
		}
	} while (rk_emmc_busy(&cmd));

	return ret;
}

/*
 * Transfer a single sector of kernel addressable data
 */
int rk_emmc_transfer(u8 *buffer, unsigned addr, unsigned blksz, int write)
{
	int ret = 0;
	enum emmc_area_type areatype;

	struct mmc_request mrq = {0};
	struct mmc_command cmd = {0};
	struct mmc_command stop = {0};
	struct mmc_data data = {0};

	struct scatterlist sg;

	if(!this_card)
		return -EIO;

	mrq.cmd = &cmd;
	mrq.data = &data;
	mrq.stop = &stop;

	sg_init_one(&sg, buffer, blksz);

	rk_emmc_prepare_mrq(&mrq, &sg, 1, addr, 1, blksz, write);

	mmc_claim_host(this_card->host);

	areatype = (enum emmc_area_type)this_card->ext_csd.part_config
		    & EXT_CSD_PART_CONFIG_ACC_MASK;
	if (areatype != MMC_DATA_AREA_MAIN) {
		ret = rk_emmc_set_areatype(MMC_DATA_AREA_MAIN);
		if (ret) {
			pr_err("rk_emmc_set_areatype error!.\n");
			goto exit;
		}
	}

	mmc_wait_for_req(this_card->host, &mrq);

	if (cmd.error){
		ret = cmd.error;
		goto exit;
	}
	if (data.error){
		ret =  data.error;
		goto exit;
	}

	ret = rk_emmc_wait_busy();

	if (areatype != MMC_DATA_AREA_MAIN) {
		ret = rk_emmc_set_areatype(areatype);
		if (ret)
			pr_err("rk_emmc_set_areatype error!.\n");
	}

exit:
	mmc_release_host(this_card->host);
	return ret;
}
EXPORT_SYMBOL(rk_emmc_transfer);

MODULE_LICENSE("GPL");
