/* linux/arch/arm/mach-msm/dma.c
 *
 * Copyright (C) 2007 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <asm/io.h>
#include <linux/interrupt.h>
#include <mach/dma.h>

#define MSM_DMOV_CHANNEL_COUNT 16

enum {
	MSM_DMOV_PRINT_ERRORS = 1,
	MSM_DMOV_PRINT_IO = 2,
	MSM_DMOV_PRINT_FLOW = 4
};

static DEFINE_SPINLOCK(msm_dmov_lock);
static struct msm_dmov_cmd active_command;
static struct list_head ready_commands[MSM_DMOV_CHANNEL_COUNT];
static struct list_head active_commands[MSM_DMOV_CHANNEL_COUNT];
unsigned int msm_dmov_print_mask = MSM_DMOV_PRINT_ERRORS;

#define MSM_DMOV_DPRINTF(mask, format, args...) \
	do { \
		if ((mask) & msm_dmov_print_mask) \
			printk(KERN_ERR format, args); \
	} while (0)
#define PRINT_ERROR(format, args...) \
	MSM_DMOV_DPRINTF(MSM_DMOV_PRINT_ERRORS, format, args);
#define PRINT_IO(format, args...) \
	MSM_DMOV_DPRINTF(MSM_DMOV_PRINT_IO, format, args);
#define PRINT_FLOW(format, args...) \
	MSM_DMOV_DPRINTF(MSM_DMOV_PRINT_FLOW, format, args);

void msm_dmov_enqueue_cmd(unsigned id, struct msm_dmov_cmd *cmd)
{
	unsigned long irq_flags;
	unsigned int status;

	spin_lock_irqsave(&msm_dmov_lock, irq_flags);
	status = readl(DMOV_STATUS(id));
	if (list_empty(&ready_commands[id]) &&
		(status & DMOV_STATUS_CMD_PTR_RDY)) {
#if 0
		if (list_empty(&active_commands[id])) {
			PRINT_FLOW("msm_dmov_enqueue_cmd(%d), enable interrupt\n", id);
			writel(DMOV_CONFIG_IRQ_EN, DMOV_CONFIG(id));
		}
#endif
		PRINT_IO("msm_dmov_enqueue_cmd(%d), start command, status %x\n", id, status);
		list_add_tail(&cmd->list, &active_commands[id]);
		writel(cmd->cmdptr, DMOV_CMD_PTR(id));
	} else {
		if (list_empty(&active_commands[id]))
			PRINT_ERROR("msm_dmov_enqueue_cmd(%d), error datamover stalled, status %x\n", id, status);

		PRINT_IO("msm_dmov_enqueue_cmd(%d), enqueue command, status %x\n", id, status);
		list_add_tail(&cmd->list, &ready_commands[id]);
	}
	spin_unlock_irqrestore(&msm_dmov_lock, irq_flags);
}

struct msm_dmov_exec_cmdptr_cmd {
	struct msm_dmov_cmd dmov_cmd;
	struct completion complete;
	unsigned id;
	unsigned int result;
	unsigned int flush[6];
};

static void dmov_exec_cmdptr_complete_func(struct msm_dmov_cmd *_cmd, unsigned int result)
{
	struct msm_dmov_exec_cmdptr_cmd *cmd = container_of(_cmd, struct msm_dmov_exec_cmdptr_cmd, dmov_cmd);
	cmd->result = result;
	if (result != 0x80000002) {
		cmd->flush[0] = readl(DMOV_FLUSH0(cmd->id));
		cmd->flush[1] = readl(DMOV_FLUSH1(cmd->id));
		cmd->flush[2] = readl(DMOV_FLUSH2(cmd->id));
		cmd->flush[3] = readl(DMOV_FLUSH3(cmd->id));
		cmd->flush[4] = readl(DMOV_FLUSH4(cmd->id));
		cmd->flush[5] = readl(DMOV_FLUSH5(cmd->id));
	}
	complete(&cmd->complete);
}

int msm_dmov_exec_cmd(unsigned id, unsigned int cmdptr)
{
	struct msm_dmov_exec_cmdptr_cmd cmd;

	PRINT_FLOW("dmov_exec_cmdptr(%d, %x)\n", id, cmdptr);

	cmd.dmov_cmd.cmdptr = cmdptr;
	cmd.dmov_cmd.complete_func = dmov_exec_cmdptr_complete_func;
	cmd.id = id;
	init_completion(&cmd.complete);

	msm_dmov_enqueue_cmd(id, &cmd.dmov_cmd);
	wait_for_completion(&cmd.complete);

	if (cmd.result != 0x80000002) {
		PRINT_ERROR("dmov_exec_cmdptr(%d): ERROR, result: %x\n", id, cmd.result);
		PRINT_ERROR("dmov_exec_cmdptr(%d):  flush: %x %x %x %x\n",
			id, cmd.flush[0], cmd.flush[1], cmd.flush[2], cmd.flush[3]);
		return -EIO;
	}
	PRINT_FLOW("dmov_exec_cmdptr(%d, %x) done\n", id, cmdptr);
	return 0;
}


static irqreturn_t msm_datamover_irq_handler(int irq, void *dev_id)
{
	unsigned int int_status, mask, id;
	unsigned long irq_flags;
	unsigned int ch_status;
	unsigned int ch_result;
	struct msm_dmov_cmd *cmd;

	spin_lock_irqsave(&msm_dmov_lock, irq_flags);

	int_status = readl(DMOV_ISR); /* read and clear interrupt */
	PRINT_FLOW("msm_datamover_irq_handler: DMOV_ISR %x\n", int_status);

	while (int_status) {
		mask = int_status & -int_status;
		id = fls(mask) - 1;
		PRINT_FLOW("msm_datamover_irq_handler %08x %08x id %d\n", int_status, mask, id);
		int_status &= ~mask;
		ch_status = readl(DMOV_STATUS(id));
		if (!(ch_status & DMOV_STATUS_RSLT_VALID)) {
			PRINT_FLOW("msm_datamover_irq_handler id %d, result not valid %x\n", id, ch_status);
			continue;
		}
		do {
			ch_result = readl(DMOV_RSLT(id));
			if (list_empty(&active_commands[id])) {
				PRINT_ERROR("msm_datamover_irq_handler id %d, got result "
					"with no active command, status %x, result %x\n",
					id, ch_status, ch_result);
				cmd = NULL;
			} else
				cmd = list_entry(active_commands[id].next, typeof(*cmd), list);
			PRINT_FLOW("msm_datamover_irq_handler id %d, status %x, result %x\n", id, ch_status, ch_result);
			if (ch_result & DMOV_RSLT_DONE) {
				PRINT_FLOW("msm_datamover_irq_handler id %d, status %x\n",
					id, ch_status);
				PRINT_IO("msm_datamover_irq_handler id %d, got result "
					"for %p, result %x\n", id, cmd, ch_result);
				if (cmd) {
					list_del(&cmd->list);
					cmd->complete_func(cmd, ch_result);
				}
			}
			if (ch_result & DMOV_RSLT_FLUSH) {
				unsigned int flush0 = readl(DMOV_FLUSH0(id));
				PRINT_FLOW("msm_datamover_irq_handler id %d, status %x\n", id, ch_status);
				PRINT_FLOW("msm_datamover_irq_handler id %d, flush, result %x, flush0 %x\n", id, ch_result, flush0);
				if (cmd) {
					list_del(&cmd->list);
					cmd->complete_func(cmd, ch_result);
				}
			}
			if (ch_result & DMOV_RSLT_ERROR) {
				unsigned int flush0 = readl(DMOV_FLUSH0(id));
				PRINT_ERROR("msm_datamover_irq_handler id %d, status %x\n", id, ch_status);
				PRINT_ERROR("msm_datamover_irq_handler id %d, error, result %x, flush0 %x\n", id, ch_result, flush0);
				if (cmd) {
					list_del(&cmd->list);
					cmd->complete_func(cmd, ch_result);
				}
				/* this does not seem to work, once we get an error */
				/* the datamover will no longer accept commands */
				writel(0, DMOV_FLUSH0(id));
			}
			ch_status = readl(DMOV_STATUS(id));
			PRINT_FLOW("msm_datamover_irq_handler id %d, status %x\n", id, ch_status);
			if ((ch_status & DMOV_STATUS_CMD_PTR_RDY) && !list_empty(&ready_commands[id])) {
				cmd = list_entry(ready_commands[id].next, typeof(*cmd), list);
				list_del(&cmd->list);
				list_add_tail(&cmd->list, &active_commands[id]);
				PRINT_FLOW("msm_datamover_irq_handler id %d, start command\n", id);
				writel(cmd->cmdptr, DMOV_CMD_PTR(id));
			}
		} while (ch_status & DMOV_STATUS_RSLT_VALID);
		PRINT_FLOW("msm_datamover_irq_handler id %d, status %x\n", id, ch_status);
	}
	spin_unlock_irqrestore(&msm_dmov_lock, irq_flags);
	return IRQ_HANDLED;
}

static int __init msm_init_datamover(void)
{
	int i;
	for (i = 0; i < MSM_DMOV_CHANNEL_COUNT; i++) {
		INIT_LIST_HEAD(&ready_commands[i]);
		INIT_LIST_HEAD(&active_commands[i]);
		writel(DMOV_CONFIG_IRQ_EN | DMOV_CONFIG_FORCE_TOP_PTR_RSLT | DMOV_CONFIG_FORCE_FLUSH_RSLT, DMOV_CONFIG(i));
	}
	return request_irq(INT_ADM_AARM, msm_datamover_irq_handler, 0, "msmdatamover", NULL);
}

arch_initcall(msm_init_datamover);

