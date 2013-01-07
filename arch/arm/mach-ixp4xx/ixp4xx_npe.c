/*
 * Intel IXP4xx Network Processor Engine driver for Linux
 *
 * Copyright (C) 2007 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * The code is based on publicly available information:
 * - Intel IXP4xx Developer's Manual and other e-papers
 * - Intel IXP400 Access Library Software (BSD license)
 * - previous works by Christian Hohnstaedt <chohnstaedt@innominate.com>
 *   Thanks, Christian.
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <mach/npe.h>

#define DEBUG_MSG			0
#define DEBUG_FW			0

#define NPE_COUNT			3
#define MAX_RETRIES			1000	/* microseconds */
#define NPE_42X_DATA_SIZE		0x800	/* in dwords */
#define NPE_46X_DATA_SIZE		0x1000
#define NPE_A_42X_INSTR_SIZE		0x1000
#define NPE_B_AND_C_42X_INSTR_SIZE	0x800
#define NPE_46X_INSTR_SIZE		0x1000
#define REGS_SIZE			0x1000

#define NPE_PHYS_REG			32

#define FW_MAGIC			0xFEEDF00D
#define FW_BLOCK_TYPE_INSTR		0x0
#define FW_BLOCK_TYPE_DATA		0x1
#define FW_BLOCK_TYPE_EOF		0xF

/* NPE exec status (read) and command (write) */
#define CMD_NPE_STEP			0x01
#define CMD_NPE_START			0x02
#define CMD_NPE_STOP			0x03
#define CMD_NPE_CLR_PIPE		0x04
#define CMD_CLR_PROFILE_CNT		0x0C
#define CMD_RD_INS_MEM			0x10 /* instruction memory */
#define CMD_WR_INS_MEM			0x11
#define CMD_RD_DATA_MEM			0x12 /* data memory */
#define CMD_WR_DATA_MEM			0x13
#define CMD_RD_ECS_REG			0x14 /* exec access register */
#define CMD_WR_ECS_REG			0x15

#define STAT_RUN			0x80000000
#define STAT_STOP			0x40000000
#define STAT_CLEAR			0x20000000
#define STAT_ECS_K			0x00800000 /* pipeline clean */

#define NPE_STEVT			0x1B
#define NPE_STARTPC			0x1C
#define NPE_REGMAP			0x1E
#define NPE_CINDEX			0x1F

#define INSTR_WR_REG_SHORT		0x0000C000
#define INSTR_WR_REG_BYTE		0x00004000
#define INSTR_RD_FIFO			0x0F888220
#define INSTR_RESET_MBOX		0x0FAC8210

#define ECS_BG_CTXT_REG_0		0x00 /* Background Executing Context */
#define ECS_BG_CTXT_REG_1		0x01 /*		Stack level */
#define ECS_BG_CTXT_REG_2		0x02
#define ECS_PRI_1_CTXT_REG_0		0x04 /* Priority 1 Executing Context */
#define ECS_PRI_1_CTXT_REG_1		0x05 /*		Stack level */
#define ECS_PRI_1_CTXT_REG_2		0x06
#define ECS_PRI_2_CTXT_REG_0		0x08 /* Priority 2 Executing Context */
#define ECS_PRI_2_CTXT_REG_1		0x09 /*		Stack level */
#define ECS_PRI_2_CTXT_REG_2		0x0A
#define ECS_DBG_CTXT_REG_0		0x0C /* Debug Executing Context */
#define ECS_DBG_CTXT_REG_1		0x0D /*		Stack level */
#define ECS_DBG_CTXT_REG_2		0x0E
#define ECS_INSTRUCT_REG		0x11 /* NPE Instruction Register */

#define ECS_REG_0_ACTIVE		0x80000000 /* all levels */
#define ECS_REG_0_NEXTPC_MASK		0x1FFF0000 /* BG/PRI1/PRI2 levels */
#define ECS_REG_0_LDUR_BITS		8
#define ECS_REG_0_LDUR_MASK		0x00000700 /* all levels */
#define ECS_REG_1_CCTXT_BITS		16
#define ECS_REG_1_CCTXT_MASK		0x000F0000 /* all levels */
#define ECS_REG_1_SELCTXT_BITS		0
#define ECS_REG_1_SELCTXT_MASK		0x0000000F /* all levels */
#define ECS_DBG_REG_2_IF		0x00100000 /* debug level */
#define ECS_DBG_REG_2_IE		0x00080000 /* debug level */

/* NPE watchpoint_fifo register bit */
#define WFIFO_VALID			0x80000000

/* NPE messaging_status register bit definitions */
#define MSGSTAT_OFNE	0x00010000 /* OutFifoNotEmpty */
#define MSGSTAT_IFNF	0x00020000 /* InFifoNotFull */
#define MSGSTAT_OFNF	0x00040000 /* OutFifoNotFull */
#define MSGSTAT_IFNE	0x00080000 /* InFifoNotEmpty */
#define MSGSTAT_MBINT	0x00100000 /* Mailbox interrupt */
#define MSGSTAT_IFINT	0x00200000 /* InFifo interrupt */
#define MSGSTAT_OFINT	0x00400000 /* OutFifo interrupt */
#define MSGSTAT_WFINT	0x00800000 /* WatchFifo interrupt */

/* NPE messaging_control register bit definitions */
#define MSGCTL_OUT_FIFO			0x00010000 /* enable output FIFO */
#define MSGCTL_IN_FIFO			0x00020000 /* enable input FIFO */
#define MSGCTL_OUT_FIFO_WRITE		0x01000000 /* enable FIFO + WRITE */
#define MSGCTL_IN_FIFO_WRITE		0x02000000

/* NPE mailbox_status value for reset */
#define RESET_MBOX_STAT			0x0000F0F0

#define NPE_A_FIRMWARE "NPE-A"
#define NPE_B_FIRMWARE "NPE-B"
#define NPE_C_FIRMWARE "NPE-C"

const char *npe_names[] = { NPE_A_FIRMWARE, NPE_B_FIRMWARE, NPE_C_FIRMWARE };

#define print_npe(pri, npe, fmt, ...)					\
	printk(pri "%s: " fmt, npe_name(npe), ## __VA_ARGS__)

#if DEBUG_MSG
#define debug_msg(npe, fmt, ...)					\
	print_npe(KERN_DEBUG, npe, fmt, ## __VA_ARGS__)
#else
#define debug_msg(npe, fmt, ...)
#endif

static struct {
	u32 reg, val;
} ecs_reset[] = {
	{ ECS_BG_CTXT_REG_0,	0xA0000000 },
	{ ECS_BG_CTXT_REG_1,	0x01000000 },
	{ ECS_BG_CTXT_REG_2,	0x00008000 },
	{ ECS_PRI_1_CTXT_REG_0,	0x20000080 },
	{ ECS_PRI_1_CTXT_REG_1,	0x01000000 },
	{ ECS_PRI_1_CTXT_REG_2,	0x00008000 },
	{ ECS_PRI_2_CTXT_REG_0,	0x20000080 },
	{ ECS_PRI_2_CTXT_REG_1,	0x01000000 },
	{ ECS_PRI_2_CTXT_REG_2,	0x00008000 },
	{ ECS_DBG_CTXT_REG_0,	0x20000000 },
	{ ECS_DBG_CTXT_REG_1,	0x00000000 },
	{ ECS_DBG_CTXT_REG_2,	0x001E0000 },
	{ ECS_INSTRUCT_REG,	0x1003C00F },
};

static struct npe npe_tab[NPE_COUNT] = {
	{
		.id	= 0,
		.regs	= (struct npe_regs __iomem *)IXP4XX_NPEA_BASE_VIRT,
		.regs_phys = IXP4XX_NPEA_BASE_PHYS,
	}, {
		.id	= 1,
		.regs	= (struct npe_regs __iomem *)IXP4XX_NPEB_BASE_VIRT,
		.regs_phys = IXP4XX_NPEB_BASE_PHYS,
	}, {
		.id	= 2,
		.regs	= (struct npe_regs __iomem *)IXP4XX_NPEC_BASE_VIRT,
		.regs_phys = IXP4XX_NPEC_BASE_PHYS,
	}
};

int npe_running(struct npe *npe)
{
	return (__raw_readl(&npe->regs->exec_status_cmd) & STAT_RUN) != 0;
}

static void npe_cmd_write(struct npe *npe, u32 addr, int cmd, u32 data)
{
	__raw_writel(data, &npe->regs->exec_data);
	__raw_writel(addr, &npe->regs->exec_addr);
	__raw_writel(cmd, &npe->regs->exec_status_cmd);
}

static u32 npe_cmd_read(struct npe *npe, u32 addr, int cmd)
{
	__raw_writel(addr, &npe->regs->exec_addr);
	__raw_writel(cmd, &npe->regs->exec_status_cmd);
	/* Iintroduce extra read cycles after issuing read command to NPE
	   so that we read the register after the NPE has updated it.
	   This is to overcome race condition between XScale and NPE */
	__raw_readl(&npe->regs->exec_data);
	__raw_readl(&npe->regs->exec_data);
	return __raw_readl(&npe->regs->exec_data);
}

static void npe_clear_active(struct npe *npe, u32 reg)
{
	u32 val = npe_cmd_read(npe, reg, CMD_RD_ECS_REG);
	npe_cmd_write(npe, reg, CMD_WR_ECS_REG, val & ~ECS_REG_0_ACTIVE);
}

static void npe_start(struct npe *npe)
{
	/* ensure only Background Context Stack Level is active */
	npe_clear_active(npe, ECS_PRI_1_CTXT_REG_0);
	npe_clear_active(npe, ECS_PRI_2_CTXT_REG_0);
	npe_clear_active(npe, ECS_DBG_CTXT_REG_0);

	__raw_writel(CMD_NPE_CLR_PIPE, &npe->regs->exec_status_cmd);
	__raw_writel(CMD_NPE_START, &npe->regs->exec_status_cmd);
}

static void npe_stop(struct npe *npe)
{
	__raw_writel(CMD_NPE_STOP, &npe->regs->exec_status_cmd);
	__raw_writel(CMD_NPE_CLR_PIPE, &npe->regs->exec_status_cmd); /*FIXME?*/
}

static int __must_check npe_debug_instr(struct npe *npe, u32 instr, u32 ctx,
					u32 ldur)
{
	u32 wc;
	int i;

	/* set the Active bit, and the LDUR, in the debug level */
	npe_cmd_write(npe, ECS_DBG_CTXT_REG_0, CMD_WR_ECS_REG,
		      ECS_REG_0_ACTIVE | (ldur << ECS_REG_0_LDUR_BITS));

	/* set CCTXT at ECS DEBUG L3 to specify in which context to execute
	   the instruction, and set SELCTXT at ECS DEBUG Level to specify
	   which context store to access.
	   Debug ECS Level Reg 1 has form 0x000n000n, where n = context number
	*/
	npe_cmd_write(npe, ECS_DBG_CTXT_REG_1, CMD_WR_ECS_REG,
		      (ctx << ECS_REG_1_CCTXT_BITS) |
		      (ctx << ECS_REG_1_SELCTXT_BITS));

	/* clear the pipeline */
	__raw_writel(CMD_NPE_CLR_PIPE, &npe->regs->exec_status_cmd);

	/* load NPE instruction into the instruction register */
	npe_cmd_write(npe, ECS_INSTRUCT_REG, CMD_WR_ECS_REG, instr);

	/* we need this value later to wait for completion of NPE execution
	   step */
	wc = __raw_readl(&npe->regs->watch_count);

	/* issue a Step One command via the Execution Control register */
	__raw_writel(CMD_NPE_STEP, &npe->regs->exec_status_cmd);

	/* Watch Count register increments when NPE completes an instruction */
	for (i = 0; i < MAX_RETRIES; i++) {
		if (wc != __raw_readl(&npe->regs->watch_count))
			return 0;
		udelay(1);
	}

	print_npe(KERN_ERR, npe, "reset: npe_debug_instr(): timeout\n");
	return -ETIMEDOUT;
}

static int __must_check npe_logical_reg_write8(struct npe *npe, u32 addr,
					       u8 val, u32 ctx)
{
	/* here we build the NPE assembler instruction: mov8 d0, #0 */
	u32 instr = INSTR_WR_REG_BYTE |	/* OpCode */
		addr << 9 |		/* base Operand */
		(val & 0x1F) << 4 |	/* lower 5 bits to immediate data */
		(val & ~0x1F) << (18 - 5);/* higher 3 bits to CoProc instr. */
	return npe_debug_instr(npe, instr, ctx, 1); /* execute it */
}

static int __must_check npe_logical_reg_write16(struct npe *npe, u32 addr,
						u16 val, u32 ctx)
{
	/* here we build the NPE assembler instruction: mov16 d0, #0 */
	u32 instr = INSTR_WR_REG_SHORT | /* OpCode */
		addr << 9 |		/* base Operand */
		(val & 0x1F) << 4 |	/* lower 5 bits to immediate data */
		(val & ~0x1F) << (18 - 5);/* higher 11 bits to CoProc instr. */
	return npe_debug_instr(npe, instr, ctx, 1); /* execute it */
}

static int __must_check npe_logical_reg_write32(struct npe *npe, u32 addr,
						u32 val, u32 ctx)
{
	/* write in 16 bit steps first the high and then the low value */
	if (npe_logical_reg_write16(npe, addr, val >> 16, ctx))
		return -ETIMEDOUT;
	return npe_logical_reg_write16(npe, addr + 2, val & 0xFFFF, ctx);
}

static int npe_reset(struct npe *npe)
{
	u32 val, ctl, exec_count, ctx_reg2;
	int i;

	ctl = (__raw_readl(&npe->regs->messaging_control) | 0x3F000000) &
		0x3F3FFFFF;

	/* disable parity interrupt */
	__raw_writel(ctl & 0x3F00FFFF, &npe->regs->messaging_control);

	/* pre exec - debug instruction */
	/* turn off the halt bit by clearing Execution Count register. */
	exec_count = __raw_readl(&npe->regs->exec_count);
	__raw_writel(0, &npe->regs->exec_count);
	/* ensure that IF and IE are on (temporarily), so that we don't end up
	   stepping forever */
	ctx_reg2 = npe_cmd_read(npe, ECS_DBG_CTXT_REG_2, CMD_RD_ECS_REG);
	npe_cmd_write(npe, ECS_DBG_CTXT_REG_2, CMD_WR_ECS_REG, ctx_reg2 |
		      ECS_DBG_REG_2_IF | ECS_DBG_REG_2_IE);

	/* clear the FIFOs */
	while (__raw_readl(&npe->regs->watchpoint_fifo) & WFIFO_VALID)
		;
	while (__raw_readl(&npe->regs->messaging_status) & MSGSTAT_OFNE)
		/* read from the outFIFO until empty */
		print_npe(KERN_DEBUG, npe, "npe_reset: read FIFO = 0x%X\n",
			  __raw_readl(&npe->regs->in_out_fifo));

	while (__raw_readl(&npe->regs->messaging_status) & MSGSTAT_IFNE)
		/* step execution of the NPE intruction to read inFIFO using
		   the Debug Executing Context stack */
		if (npe_debug_instr(npe, INSTR_RD_FIFO, 0, 0))
			return -ETIMEDOUT;

	/* reset the mailbox reg from the XScale side */
	__raw_writel(RESET_MBOX_STAT, &npe->regs->mailbox_status);
	/* from NPE side */
	if (npe_debug_instr(npe, INSTR_RESET_MBOX, 0, 0))
		return -ETIMEDOUT;

	/* Reset the physical registers in the NPE register file */
	for (val = 0; val < NPE_PHYS_REG; val++) {
		if (npe_logical_reg_write16(npe, NPE_REGMAP, val >> 1, 0))
			return -ETIMEDOUT;
		/* address is either 0 or 4 */
		if (npe_logical_reg_write32(npe, (val & 1) * 4, 0, 0))
			return -ETIMEDOUT;
	}

	/* Reset the context store = each context's Context Store registers */

	/* Context 0 has no STARTPC. Instead, this value is used to set NextPC
	   for Background ECS, to set where NPE starts executing code */
	val = npe_cmd_read(npe, ECS_BG_CTXT_REG_0, CMD_RD_ECS_REG);
	val &= ~ECS_REG_0_NEXTPC_MASK;
	val |= (0 /* NextPC */ << 16) & ECS_REG_0_NEXTPC_MASK;
	npe_cmd_write(npe, ECS_BG_CTXT_REG_0, CMD_WR_ECS_REG, val);

	for (i = 0; i < 16; i++) {
		if (i) {	/* Context 0 has no STEVT nor STARTPC */
			/* STEVT = off, 0x80 */
			if (npe_logical_reg_write8(npe, NPE_STEVT, 0x80, i))
				return -ETIMEDOUT;
			if (npe_logical_reg_write16(npe, NPE_STARTPC, 0, i))
				return -ETIMEDOUT;
		}
		/* REGMAP = d0->p0, d8->p2, d16->p4 */
		if (npe_logical_reg_write16(npe, NPE_REGMAP, 0x820, i))
			return -ETIMEDOUT;
		if (npe_logical_reg_write8(npe, NPE_CINDEX, 0, i))
			return -ETIMEDOUT;
	}

	/* post exec */
	/* clear active bit in debug level */
	npe_cmd_write(npe, ECS_DBG_CTXT_REG_0, CMD_WR_ECS_REG, 0);
	/* clear the pipeline */
	__raw_writel(CMD_NPE_CLR_PIPE, &npe->regs->exec_status_cmd);
	/* restore previous values */
	__raw_writel(exec_count, &npe->regs->exec_count);
	npe_cmd_write(npe, ECS_DBG_CTXT_REG_2, CMD_WR_ECS_REG, ctx_reg2);

	/* write reset values to Execution Context Stack registers */
	for (val = 0; val < ARRAY_SIZE(ecs_reset); val++)
		npe_cmd_write(npe, ecs_reset[val].reg, CMD_WR_ECS_REG,
			      ecs_reset[val].val);

	/* clear the profile counter */
	__raw_writel(CMD_CLR_PROFILE_CNT, &npe->regs->exec_status_cmd);

	__raw_writel(0, &npe->regs->exec_count);
	__raw_writel(0, &npe->regs->action_points[0]);
	__raw_writel(0, &npe->regs->action_points[1]);
	__raw_writel(0, &npe->regs->action_points[2]);
	__raw_writel(0, &npe->regs->action_points[3]);
	__raw_writel(0, &npe->regs->watch_count);

	val = ixp4xx_read_feature_bits();
	/* reset the NPE */
	ixp4xx_write_feature_bits(val &
				  ~(IXP4XX_FEATURE_RESET_NPEA << npe->id));
	/* deassert reset */
	ixp4xx_write_feature_bits(val |
				  (IXP4XX_FEATURE_RESET_NPEA << npe->id));
	for (i = 0; i < MAX_RETRIES; i++) {
		if (ixp4xx_read_feature_bits() &
		    (IXP4XX_FEATURE_RESET_NPEA << npe->id))
			break;	/* NPE is back alive */
		udelay(1);
	}
	if (i == MAX_RETRIES)
		return -ETIMEDOUT;

	npe_stop(npe);

	/* restore NPE configuration bus Control Register - parity settings */
	__raw_writel(ctl, &npe->regs->messaging_control);
	return 0;
}


int npe_send_message(struct npe *npe, const void *msg, const char *what)
{
	const u32 *send = msg;
	int cycles = 0;

	debug_msg(npe, "Trying to send message %s [%08X:%08X]\n",
		  what, send[0], send[1]);

	if (__raw_readl(&npe->regs->messaging_status) & MSGSTAT_IFNE) {
		debug_msg(npe, "NPE input FIFO not empty\n");
		return -EIO;
	}

	__raw_writel(send[0], &npe->regs->in_out_fifo);

	if (!(__raw_readl(&npe->regs->messaging_status) & MSGSTAT_IFNF)) {
		debug_msg(npe, "NPE input FIFO full\n");
		return -EIO;
	}

	__raw_writel(send[1], &npe->regs->in_out_fifo);

	while ((cycles < MAX_RETRIES) &&
	       (__raw_readl(&npe->regs->messaging_status) & MSGSTAT_IFNE)) {
		udelay(1);
		cycles++;
	}

	if (cycles == MAX_RETRIES) {
		debug_msg(npe, "Timeout sending message\n");
		return -ETIMEDOUT;
	}

#if DEBUG_MSG > 1
	debug_msg(npe, "Sending a message took %i cycles\n", cycles);
#endif
	return 0;
}

int npe_recv_message(struct npe *npe, void *msg, const char *what)
{
	u32 *recv = msg;
	int cycles = 0, cnt = 0;

	debug_msg(npe, "Trying to receive message %s\n", what);

	while (cycles < MAX_RETRIES) {
		if (__raw_readl(&npe->regs->messaging_status) & MSGSTAT_OFNE) {
			recv[cnt++] = __raw_readl(&npe->regs->in_out_fifo);
			if (cnt == 2)
				break;
		} else {
			udelay(1);
			cycles++;
		}
	}

	switch(cnt) {
	case 1:
		debug_msg(npe, "Received [%08X]\n", recv[0]);
		break;
	case 2:
		debug_msg(npe, "Received [%08X:%08X]\n", recv[0], recv[1]);
		break;
	}

	if (cycles == MAX_RETRIES) {
		debug_msg(npe, "Timeout waiting for message\n");
		return -ETIMEDOUT;
	}

#if DEBUG_MSG > 1
	debug_msg(npe, "Receiving a message took %i cycles\n", cycles);
#endif
	return 0;
}

int npe_send_recv_message(struct npe *npe, void *msg, const char *what)
{
	int result;
	u32 *send = msg, recv[2];

	if ((result = npe_send_message(npe, msg, what)) != 0)
		return result;
	if ((result = npe_recv_message(npe, recv, what)) != 0)
		return result;

	if ((recv[0] != send[0]) || (recv[1] != send[1])) {
		debug_msg(npe, "Message %s: unexpected message received\n",
			  what);
		return -EIO;
	}
	return 0;
}


int npe_load_firmware(struct npe *npe, const char *name, struct device *dev)
{
	const struct firmware *fw_entry;

	struct dl_block {
		u32 type;
		u32 offset;
	} *blk;

	struct dl_image {
		u32 magic;
		u32 id;
		u32 size;
		union {
			u32 data[0];
			struct dl_block blocks[0];
		};
	} *image;

	struct dl_codeblock {
		u32 npe_addr;
		u32 size;
		u32 data[0];
	} *cb;

	int i, j, err, data_size, instr_size, blocks, table_end;
	u32 cmd;

	if ((err = request_firmware(&fw_entry, name, dev)) != 0)
		return err;

	err = -EINVAL;
	if (fw_entry->size < sizeof(struct dl_image)) {
		print_npe(KERN_ERR, npe, "incomplete firmware file\n");
		goto err;
	}
	image = (struct dl_image*)fw_entry->data;

#if DEBUG_FW
	print_npe(KERN_DEBUG, npe, "firmware: %08X %08X %08X (0x%X bytes)\n",
		  image->magic, image->id, image->size, image->size * 4);
#endif

	if (image->magic == swab32(FW_MAGIC)) { /* swapped file */
		image->id = swab32(image->id);
		image->size = swab32(image->size);
	} else if (image->magic != FW_MAGIC) {
		print_npe(KERN_ERR, npe, "bad firmware file magic: 0x%X\n",
			  image->magic);
		goto err;
	}
	if ((image->size * 4 + sizeof(struct dl_image)) != fw_entry->size) {
		print_npe(KERN_ERR, npe,
			  "inconsistent size of firmware file\n");
		goto err;
	}
	if (((image->id >> 24) & 0xF /* NPE ID */) != npe->id) {
		print_npe(KERN_ERR, npe, "firmware file NPE ID mismatch\n");
		goto err;
	}
	if (image->magic == swab32(FW_MAGIC))
		for (i = 0; i < image->size; i++)
			image->data[i] = swab32(image->data[i]);

	if (cpu_is_ixp42x() && ((image->id >> 28) & 0xF /* device ID */)) {
		print_npe(KERN_INFO, npe, "IXP43x/IXP46x firmware ignored on "
			  "IXP42x\n");
		goto err;
	}

	if (npe_running(npe)) {
		print_npe(KERN_INFO, npe, "unable to load firmware, NPE is "
			  "already running\n");
		err = -EBUSY;
		goto err;
	}
#if 0
	npe_stop(npe);
	npe_reset(npe);
#endif

	print_npe(KERN_INFO, npe, "firmware functionality 0x%X, "
		  "revision 0x%X:%X\n", (image->id >> 16) & 0xFF,
		  (image->id >> 8) & 0xFF, image->id & 0xFF);

	if (cpu_is_ixp42x()) {
		if (!npe->id)
			instr_size = NPE_A_42X_INSTR_SIZE;
		else
			instr_size = NPE_B_AND_C_42X_INSTR_SIZE;
		data_size = NPE_42X_DATA_SIZE;
	} else {
		instr_size = NPE_46X_INSTR_SIZE;
		data_size = NPE_46X_DATA_SIZE;
	}

	for (blocks = 0; blocks * sizeof(struct dl_block) / 4 < image->size;
	     blocks++)
		if (image->blocks[blocks].type == FW_BLOCK_TYPE_EOF)
			break;
	if (blocks * sizeof(struct dl_block) / 4 >= image->size) {
		print_npe(KERN_INFO, npe, "firmware EOF block marker not "
			  "found\n");
		goto err;
	}

#if DEBUG_FW
	print_npe(KERN_DEBUG, npe, "%i firmware blocks found\n", blocks);
#endif

	table_end = blocks * sizeof(struct dl_block) / 4 + 1 /* EOF marker */;
	for (i = 0, blk = image->blocks; i < blocks; i++, blk++) {
		if (blk->offset > image->size - sizeof(struct dl_codeblock) / 4
		    || blk->offset < table_end) {
			print_npe(KERN_INFO, npe, "invalid offset 0x%X of "
				  "firmware block #%i\n", blk->offset, i);
			goto err;
		}

		cb = (struct dl_codeblock*)&image->data[blk->offset];
		if (blk->type == FW_BLOCK_TYPE_INSTR) {
			if (cb->npe_addr + cb->size > instr_size)
				goto too_big;
			cmd = CMD_WR_INS_MEM;
		} else if (blk->type == FW_BLOCK_TYPE_DATA) {
			if (cb->npe_addr + cb->size > data_size)
				goto too_big;
			cmd = CMD_WR_DATA_MEM;
		} else {
			print_npe(KERN_INFO, npe, "invalid firmware block #%i "
				  "type 0x%X\n", i, blk->type);
			goto err;
		}
		if (blk->offset + sizeof(*cb) / 4 + cb->size > image->size) {
			print_npe(KERN_INFO, npe, "firmware block #%i doesn't "
				  "fit in firmware image: type %c, start 0x%X,"
				  " length 0x%X\n", i,
				  blk->type == FW_BLOCK_TYPE_INSTR ? 'I' : 'D',
				  cb->npe_addr, cb->size);
			goto err;
		}

		for (j = 0; j < cb->size; j++)
			npe_cmd_write(npe, cb->npe_addr + j, cmd, cb->data[j]);
	}

	npe_start(npe);
	if (!npe_running(npe))
		print_npe(KERN_ERR, npe, "unable to start\n");
	release_firmware(fw_entry);
	return 0;

too_big:
	print_npe(KERN_INFO, npe, "firmware block #%i doesn't fit in NPE "
		  "memory: type %c, start 0x%X, length 0x%X\n", i,
		  blk->type == FW_BLOCK_TYPE_INSTR ? 'I' : 'D',
		  cb->npe_addr, cb->size);
err:
	release_firmware(fw_entry);
	return err;
}


struct npe *npe_request(unsigned id)
{
	if (id < NPE_COUNT)
		if (npe_tab[id].valid)
			if (try_module_get(THIS_MODULE))
				return &npe_tab[id];
	return NULL;
}

void npe_release(struct npe *npe)
{
	module_put(THIS_MODULE);
}


static int __init npe_init_module(void)
{

	int i, found = 0;

	for (i = 0; i < NPE_COUNT; i++) {
		struct npe *npe = &npe_tab[i];
		if (!(ixp4xx_read_feature_bits() &
		      (IXP4XX_FEATURE_RESET_NPEA << i)))
			continue; /* NPE already disabled or not present */
		if (!(npe->mem_res = request_mem_region(npe->regs_phys,
							REGS_SIZE,
							npe_name(npe)))) {
			print_npe(KERN_ERR, npe,
				  "failed to request memory region\n");
			continue;
		}

		if (npe_reset(npe))
			continue;
		npe->valid = 1;
		found++;
	}

	if (!found)
		return -ENODEV;
	return 0;
}

static void __exit npe_cleanup_module(void)
{
	int i;

	for (i = 0; i < NPE_COUNT; i++)
		if (npe_tab[i].mem_res) {
			npe_reset(&npe_tab[i]);
			release_resource(npe_tab[i].mem_res);
		}
}

module_init(npe_init_module);
module_exit(npe_cleanup_module);

MODULE_AUTHOR("Krzysztof Halasa");
MODULE_LICENSE("GPL v2");
MODULE_FIRMWARE(NPE_A_FIRMWARE);
MODULE_FIRMWARE(NPE_B_FIRMWARE);
MODULE_FIRMWARE(NPE_C_FIRMWARE);

EXPORT_SYMBOL(npe_names);
EXPORT_SYMBOL(npe_running);
EXPORT_SYMBOL(npe_request);
EXPORT_SYMBOL(npe_release);
EXPORT_SYMBOL(npe_load_firmware);
EXPORT_SYMBOL(npe_send_message);
EXPORT_SYMBOL(npe_recv_message);
EXPORT_SYMBOL(npe_send_recv_message);
