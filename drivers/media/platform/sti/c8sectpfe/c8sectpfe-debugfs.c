// SPDX-License-Identifier: GPL-2.0
/*
 * c8sectpfe-debugfs.c - C8SECTPFE STi DVB driver
 *
 * Copyright (c) STMicroelectronics 2015
 *
 * Author: Peter Griffin <peter.griffin@linaro.org>
 *
 */
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "c8sectpfe-debugfs.h"

#define dump_register(nm ...)			\
{						\
	.name	= #nm,				\
	.offset	= nm,				\
}

static const struct debugfs_reg32 fei_sys_regs[] = {
	dump_register(SYS_INPUT_ERR_STATUS),
	dump_register(SYS_OTHER_ERR_STATUS),
	dump_register(SYS_INPUT_ERR_MASK),
	dump_register(SYS_DMA_ROUTE),
	dump_register(SYS_INPUT_CLKEN),
	dump_register(IBENABLE_MASK),
	dump_register(SYS_OTHER_CLKEN),
	dump_register(SYS_CFG_NUM_IB),
	dump_register(SYS_CFG_NUM_MIB),
	dump_register(SYS_CFG_NUM_SWTS),
	dump_register(SYS_CFG_NUM_TSOUT),
	dump_register(SYS_CFG_NUM_CCSC),
	dump_register(SYS_CFG_NUM_RAM),
	dump_register(SYS_CFG_NUM_TP),

	dump_register(C8SECTPFE_IB_IP_FMT_CFG(0)),
	dump_register(C8SECTPFE_IB_TAGBYTES_CFG(0)),
	dump_register(C8SECTPFE_IB_PID_SET(0)),
	dump_register(C8SECTPFE_IB_PKT_LEN(0)),
	dump_register(C8SECTPFE_IB_BUFF_STRT(0)),
	dump_register(C8SECTPFE_IB_BUFF_END(0)),
	dump_register(C8SECTPFE_IB_READ_PNT(0)),
	dump_register(C8SECTPFE_IB_WRT_PNT(0)),
	dump_register(C8SECTPFE_IB_PRI_THRLD(0)),
	dump_register(C8SECTPFE_IB_STAT(0)),
	dump_register(C8SECTPFE_IB_MASK(0)),
	dump_register(C8SECTPFE_IB_SYS(0)),

	dump_register(C8SECTPFE_IB_IP_FMT_CFG(1)),
	dump_register(C8SECTPFE_IB_TAGBYTES_CFG(1)),
	dump_register(C8SECTPFE_IB_PID_SET(1)),
	dump_register(C8SECTPFE_IB_PKT_LEN(1)),
	dump_register(C8SECTPFE_IB_BUFF_STRT(1)),
	dump_register(C8SECTPFE_IB_BUFF_END(1)),
	dump_register(C8SECTPFE_IB_READ_PNT(1)),
	dump_register(C8SECTPFE_IB_WRT_PNT(1)),
	dump_register(C8SECTPFE_IB_PRI_THRLD(1)),
	dump_register(C8SECTPFE_IB_STAT(1)),
	dump_register(C8SECTPFE_IB_MASK(1)),
	dump_register(C8SECTPFE_IB_SYS(1)),

	dump_register(C8SECTPFE_IB_IP_FMT_CFG(2)),
	dump_register(C8SECTPFE_IB_TAGBYTES_CFG(2)),
	dump_register(C8SECTPFE_IB_PID_SET(2)),
	dump_register(C8SECTPFE_IB_PKT_LEN(2)),
	dump_register(C8SECTPFE_IB_BUFF_STRT(2)),
	dump_register(C8SECTPFE_IB_BUFF_END(2)),
	dump_register(C8SECTPFE_IB_READ_PNT(2)),
	dump_register(C8SECTPFE_IB_WRT_PNT(2)),
	dump_register(C8SECTPFE_IB_PRI_THRLD(2)),
	dump_register(C8SECTPFE_IB_STAT(2)),
	dump_register(C8SECTPFE_IB_MASK(2)),
	dump_register(C8SECTPFE_IB_SYS(2)),

	dump_register(C8SECTPFE_IB_IP_FMT_CFG(3)),
	dump_register(C8SECTPFE_IB_TAGBYTES_CFG(3)),
	dump_register(C8SECTPFE_IB_PID_SET(3)),
	dump_register(C8SECTPFE_IB_PKT_LEN(3)),
	dump_register(C8SECTPFE_IB_BUFF_STRT(3)),
	dump_register(C8SECTPFE_IB_BUFF_END(3)),
	dump_register(C8SECTPFE_IB_READ_PNT(3)),
	dump_register(C8SECTPFE_IB_WRT_PNT(3)),
	dump_register(C8SECTPFE_IB_PRI_THRLD(3)),
	dump_register(C8SECTPFE_IB_STAT(3)),
	dump_register(C8SECTPFE_IB_MASK(3)),
	dump_register(C8SECTPFE_IB_SYS(3)),

	dump_register(C8SECTPFE_IB_IP_FMT_CFG(4)),
	dump_register(C8SECTPFE_IB_TAGBYTES_CFG(4)),
	dump_register(C8SECTPFE_IB_PID_SET(4)),
	dump_register(C8SECTPFE_IB_PKT_LEN(4)),
	dump_register(C8SECTPFE_IB_BUFF_STRT(4)),
	dump_register(C8SECTPFE_IB_BUFF_END(4)),
	dump_register(C8SECTPFE_IB_READ_PNT(4)),
	dump_register(C8SECTPFE_IB_WRT_PNT(4)),
	dump_register(C8SECTPFE_IB_PRI_THRLD(4)),
	dump_register(C8SECTPFE_IB_STAT(4)),
	dump_register(C8SECTPFE_IB_MASK(4)),
	dump_register(C8SECTPFE_IB_SYS(4)),

	dump_register(C8SECTPFE_IB_IP_FMT_CFG(5)),
	dump_register(C8SECTPFE_IB_TAGBYTES_CFG(5)),
	dump_register(C8SECTPFE_IB_PID_SET(5)),
	dump_register(C8SECTPFE_IB_PKT_LEN(5)),
	dump_register(C8SECTPFE_IB_BUFF_STRT(5)),
	dump_register(C8SECTPFE_IB_BUFF_END(5)),
	dump_register(C8SECTPFE_IB_READ_PNT(5)),
	dump_register(C8SECTPFE_IB_WRT_PNT(5)),
	dump_register(C8SECTPFE_IB_PRI_THRLD(5)),
	dump_register(C8SECTPFE_IB_STAT(5)),
	dump_register(C8SECTPFE_IB_MASK(5)),
	dump_register(C8SECTPFE_IB_SYS(5)),

	dump_register(C8SECTPFE_IB_IP_FMT_CFG(6)),
	dump_register(C8SECTPFE_IB_TAGBYTES_CFG(6)),
	dump_register(C8SECTPFE_IB_PID_SET(6)),
	dump_register(C8SECTPFE_IB_PKT_LEN(6)),
	dump_register(C8SECTPFE_IB_BUFF_STRT(6)),
	dump_register(C8SECTPFE_IB_BUFF_END(6)),
	dump_register(C8SECTPFE_IB_READ_PNT(6)),
	dump_register(C8SECTPFE_IB_WRT_PNT(6)),
	dump_register(C8SECTPFE_IB_PRI_THRLD(6)),
	dump_register(C8SECTPFE_IB_STAT(6)),
	dump_register(C8SECTPFE_IB_MASK(6)),
	dump_register(C8SECTPFE_IB_SYS(6)),

	dump_register(DMA_CPU_ID),
	dump_register(DMA_CPU_VCR),
	dump_register(DMA_CPU_RUN),
	dump_register(DMA_CPU_PC),

	dump_register(DMA_PER_TPn_DREQ_MASK),
	dump_register(DMA_PER_TPn_DACK_SET),
	dump_register(DMA_PER_TPn_DREQ),
	dump_register(DMA_PER_TPn_DACK),
	dump_register(DMA_PER_DREQ_MODE),
	dump_register(DMA_PER_STBUS_SYNC),
	dump_register(DMA_PER_STBUS_ACCESS),
	dump_register(DMA_PER_STBUS_ADDRESS),
	dump_register(DMA_PER_IDLE_INT),
	dump_register(DMA_PER_PRIORITY),
	dump_register(DMA_PER_MAX_OPCODE),
	dump_register(DMA_PER_MAX_CHUNK),
	dump_register(DMA_PER_PAGE_SIZE),
	dump_register(DMA_PER_MBOX_STATUS),
	dump_register(DMA_PER_MBOX_SET),
	dump_register(DMA_PER_MBOX_CLEAR),
	dump_register(DMA_PER_MBOX_MASK),
	dump_register(DMA_PER_INJECT_PKT_SRC),
	dump_register(DMA_PER_INJECT_PKT_DEST),
	dump_register(DMA_PER_INJECT_PKT_ADDR),
	dump_register(DMA_PER_INJECT_PKT),
	dump_register(DMA_PER_PAT_PTR_INIT),
	dump_register(DMA_PER_PAT_PTR),
	dump_register(DMA_PER_SLEEP_MASK),
	dump_register(DMA_PER_SLEEP_COUNTER),

	dump_register(DMA_FIRMWARE_VERSION),
	dump_register(DMA_PTRREC_BASE),
	dump_register(DMA_PTRREC_INPUT_OFFSET),
	dump_register(DMA_ERRREC_BASE),

	dump_register(DMA_ERROR_RECORD(0)),
	dump_register(DMA_ERROR_RECORD(1)),
	dump_register(DMA_ERROR_RECORD(2)),
	dump_register(DMA_ERROR_RECORD(3)),
	dump_register(DMA_ERROR_RECORD(4)),
	dump_register(DMA_ERROR_RECORD(5)),
	dump_register(DMA_ERROR_RECORD(6)),
	dump_register(DMA_ERROR_RECORD(7)),
	dump_register(DMA_ERROR_RECORD(8)),
	dump_register(DMA_ERROR_RECORD(9)),
	dump_register(DMA_ERROR_RECORD(10)),
	dump_register(DMA_ERROR_RECORD(11)),
	dump_register(DMA_ERROR_RECORD(12)),
	dump_register(DMA_ERROR_RECORD(13)),
	dump_register(DMA_ERROR_RECORD(14)),
	dump_register(DMA_ERROR_RECORD(15)),
	dump_register(DMA_ERROR_RECORD(16)),
	dump_register(DMA_ERROR_RECORD(17)),
	dump_register(DMA_ERROR_RECORD(18)),
	dump_register(DMA_ERROR_RECORD(19)),
	dump_register(DMA_ERROR_RECORD(20)),
	dump_register(DMA_ERROR_RECORD(21)),
	dump_register(DMA_ERROR_RECORD(22)),

	dump_register(DMA_IDLE_REQ),
	dump_register(DMA_FIRMWARE_CONFIG),

	dump_register(PIDF_BASE(0)),
	dump_register(PIDF_BASE(1)),
	dump_register(PIDF_BASE(2)),
	dump_register(PIDF_BASE(3)),
	dump_register(PIDF_BASE(4)),
	dump_register(PIDF_BASE(5)),
	dump_register(PIDF_BASE(6)),
	dump_register(PIDF_BASE(7)),
	dump_register(PIDF_BASE(8)),
	dump_register(PIDF_BASE(9)),
	dump_register(PIDF_BASE(10)),
	dump_register(PIDF_BASE(11)),
	dump_register(PIDF_BASE(12)),
	dump_register(PIDF_BASE(13)),
	dump_register(PIDF_BASE(14)),
	dump_register(PIDF_BASE(15)),
	dump_register(PIDF_BASE(16)),
	dump_register(PIDF_BASE(17)),
	dump_register(PIDF_BASE(18)),
	dump_register(PIDF_BASE(19)),
	dump_register(PIDF_BASE(20)),
	dump_register(PIDF_BASE(21)),
	dump_register(PIDF_BASE(22)),
	dump_register(PIDF_LEAK_ENABLE),
	dump_register(PIDF_LEAK_STATUS),
	dump_register(PIDF_LEAK_COUNT_RESET),
	dump_register(PIDF_LEAK_COUNTER),
};

void c8sectpfe_debugfs_init(struct c8sectpfei *fei)
{
	struct dentry		*root;
	struct dentry		*file;

	root = debugfs_create_dir("c8sectpfe", NULL);
	if (!root)
		goto err;

	fei->root = root;

	fei->regset =  devm_kzalloc(fei->dev, sizeof(*fei->regset), GFP_KERNEL);
	if (!fei->regset)
		goto err;

	fei->regset->regs = fei_sys_regs;
	fei->regset->nregs = ARRAY_SIZE(fei_sys_regs);
	fei->regset->base = fei->io;

	file = debugfs_create_regset32("registers", S_IRUGO, root,
				fei->regset);
	if (!file) {
		dev_err(fei->dev,
			"%s not able to create 'registers' debugfs\n"
			, __func__);
		goto err;
	}

	return;

err:
	debugfs_remove_recursive(root);
}

void c8sectpfe_debugfs_exit(struct c8sectpfei *fei)
{
	debugfs_remove_recursive(fei->root);
	fei->root = NULL;
}
