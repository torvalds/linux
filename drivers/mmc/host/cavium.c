/*
 * Shared part of driver for MMC/SDHC controller on Cavium OCTEON and
 * ThunderX SOCs.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012-2017 Cavium Inc.
 * Authors:
 *   David Daney <david.daney@cavium.com>
 *   Peter Swain <pswain@cavium.com>
 *   Steven J. Hill <steven.hill@cavium.com>
 *   Jan Glauber <jglauber@cavium.com>
 */
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/scatterlist.h>
#include <linux/time.h>

#include "cavium.h"

const char *cvm_mmc_irq_names[] = {
	"MMC Buffer",
	"MMC Command",
	"MMC DMA",
	"MMC Command Error",
	"MMC DMA Error",
	"MMC Switch",
	"MMC Switch Error",
	"MMC DMA int Fifo",
	"MMC DMA int",
};

/*
 * The Cavium MMC host hardware assumes that all commands have fixed
 * command and response types.  These are correct if MMC devices are
 * being used.  However, non-MMC devices like SD use command and
 * response types that are unexpected by the host hardware.
 *
 * The command and response types can be overridden by supplying an
 * XOR value that is applied to the type.  We calculate the XOR value
 * from the values in this table and the flags passed from the MMC
 * core.
 */
static struct cvm_mmc_cr_type cvm_mmc_cr_types[] = {
	{0, 0},		/* CMD0 */
	{0, 3},		/* CMD1 */
	{0, 2},		/* CMD2 */
	{0, 1},		/* CMD3 */
	{0, 0},		/* CMD4 */
	{0, 1},		/* CMD5 */
	{0, 1},		/* CMD6 */
	{0, 1},		/* CMD7 */
	{1, 1},		/* CMD8 */
	{0, 2},		/* CMD9 */
	{0, 2},		/* CMD10 */
	{1, 1},		/* CMD11 */
	{0, 1},		/* CMD12 */
	{0, 1},		/* CMD13 */
	{1, 1},		/* CMD14 */
	{0, 0},		/* CMD15 */
	{0, 1},		/* CMD16 */
	{1, 1},		/* CMD17 */
	{1, 1},		/* CMD18 */
	{3, 1},		/* CMD19 */
	{2, 1},		/* CMD20 */
	{0, 0},		/* CMD21 */
	{0, 0},		/* CMD22 */
	{0, 1},		/* CMD23 */
	{2, 1},		/* CMD24 */
	{2, 1},		/* CMD25 */
	{2, 1},		/* CMD26 */
	{2, 1},		/* CMD27 */
	{0, 1},		/* CMD28 */
	{0, 1},		/* CMD29 */
	{1, 1},		/* CMD30 */
	{1, 1},		/* CMD31 */
	{0, 0},		/* CMD32 */
	{0, 0},		/* CMD33 */
	{0, 0},		/* CMD34 */
	{0, 1},		/* CMD35 */
	{0, 1},		/* CMD36 */
	{0, 0},		/* CMD37 */
	{0, 1},		/* CMD38 */
	{0, 4},		/* CMD39 */
	{0, 5},		/* CMD40 */
	{0, 0},		/* CMD41 */
	{2, 1},		/* CMD42 */
	{0, 0},		/* CMD43 */
	{0, 0},		/* CMD44 */
	{0, 0},		/* CMD45 */
	{0, 0},		/* CMD46 */
	{0, 0},		/* CMD47 */
	{0, 0},		/* CMD48 */
	{0, 0},		/* CMD49 */
	{0, 0},		/* CMD50 */
	{0, 0},		/* CMD51 */
	{0, 0},		/* CMD52 */
	{0, 0},		/* CMD53 */
	{0, 0},		/* CMD54 */
	{0, 1},		/* CMD55 */
	{0xff, 0xff},	/* CMD56 */
	{0, 0},		/* CMD57 */
	{0, 0},		/* CMD58 */
	{0, 0},		/* CMD59 */
	{0, 0},		/* CMD60 */
	{0, 0},		/* CMD61 */
	{0, 0},		/* CMD62 */
	{0, 0}		/* CMD63 */
};

static struct cvm_mmc_cr_mods cvm_mmc_get_cr_mods(struct mmc_command *cmd)
{
	struct cvm_mmc_cr_type *cr;
	u8 hardware_ctype, hardware_rtype;
	u8 desired_ctype = 0, desired_rtype = 0;
	struct cvm_mmc_cr_mods r;

	cr = cvm_mmc_cr_types + (cmd->opcode & 0x3f);
	hardware_ctype = cr->ctype;
	hardware_rtype = cr->rtype;
	if (cmd->opcode == MMC_GEN_CMD)
		hardware_ctype = (cmd->arg & 1) ? 1 : 2;

	switch (mmc_cmd_type(cmd)) {
	case MMC_CMD_ADTC:
		desired_ctype = (cmd->data->flags & MMC_DATA_WRITE) ? 2 : 1;
		break;
	case MMC_CMD_AC:
	case MMC_CMD_BC:
	case MMC_CMD_BCR:
		desired_ctype = 0;
		break;
	}

	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_NONE:
		desired_rtype = 0;
		break;
	case MMC_RSP_R1:/* MMC_RSP_R5, MMC_RSP_R6, MMC_RSP_R7 */
	case MMC_RSP_R1B:
		desired_rtype = 1;
		break;
	case MMC_RSP_R2:
		desired_rtype = 2;
		break;
	case MMC_RSP_R3: /* MMC_RSP_R4 */
		desired_rtype = 3;
		break;
	}
	r.ctype_xor = desired_ctype ^ hardware_ctype;
	r.rtype_xor = desired_rtype ^ hardware_rtype;
	return r;
}

static void check_switch_errors(struct cvm_mmc_host *host)
{
	u64 emm_switch;

	emm_switch = readq(host->base + MIO_EMM_SWITCH(host));
	if (emm_switch & MIO_EMM_SWITCH_ERR0)
		dev_err(host->dev, "Switch power class error\n");
	if (emm_switch & MIO_EMM_SWITCH_ERR1)
		dev_err(host->dev, "Switch hs timing error\n");
	if (emm_switch & MIO_EMM_SWITCH_ERR2)
		dev_err(host->dev, "Switch bus width error\n");
}

static void clear_bus_id(u64 *reg)
{
	u64 bus_id_mask = GENMASK_ULL(61, 60);

	*reg &= ~bus_id_mask;
}

static void set_bus_id(u64 *reg, int bus_id)
{
	clear_bus_id(reg);
	*reg |= FIELD_PREP(GENMASK(61, 60), bus_id);
}

static int get_bus_id(u64 reg)
{
	return FIELD_GET(GENMASK_ULL(61, 60), reg);
}

/*
 * We never set the switch_exe bit since that would interfere
 * with the commands send by the MMC core.
 */
static void do_switch(struct cvm_mmc_host *host, u64 emm_switch)
{
	int retries = 100;
	u64 rsp_sts;
	int bus_id;

	/*
	 * Modes setting only taken from slot 0. Work around that hardware
	 * issue by first switching to slot 0.
	 */
	bus_id = get_bus_id(emm_switch);
	clear_bus_id(&emm_switch);
	writeq(emm_switch, host->base + MIO_EMM_SWITCH(host));

	set_bus_id(&emm_switch, bus_id);
	writeq(emm_switch, host->base + MIO_EMM_SWITCH(host));

	/* wait for the switch to finish */
	do {
		rsp_sts = readq(host->base + MIO_EMM_RSP_STS(host));
		if (!(rsp_sts & MIO_EMM_RSP_STS_SWITCH_VAL))
			break;
		udelay(10);
	} while (--retries);

	check_switch_errors(host);
}

static bool switch_val_changed(struct cvm_mmc_slot *slot, u64 new_val)
{
	/* Match BUS_ID, HS_TIMING, BUS_WIDTH, POWER_CLASS, CLK_HI, CLK_LO */
	u64 match = 0x3001070fffffffffull;

	return (slot->cached_switch & match) != (new_val & match);
}

static void set_wdog(struct cvm_mmc_slot *slot, unsigned int ns)
{
	u64 timeout;

	if (!slot->clock)
		return;

	if (ns)
		timeout = (slot->clock * ns) / NSEC_PER_SEC;
	else
		timeout = (slot->clock * 850ull) / 1000ull;
	writeq(timeout, slot->host->base + MIO_EMM_WDOG(slot->host));
}

static void cvm_mmc_reset_bus(struct cvm_mmc_slot *slot)
{
	struct cvm_mmc_host *host = slot->host;
	u64 emm_switch, wdog;

	emm_switch = readq(slot->host->base + MIO_EMM_SWITCH(host));
	emm_switch &= ~(MIO_EMM_SWITCH_EXE | MIO_EMM_SWITCH_ERR0 |
			MIO_EMM_SWITCH_ERR1 | MIO_EMM_SWITCH_ERR2);
	set_bus_id(&emm_switch, slot->bus_id);

	wdog = readq(slot->host->base + MIO_EMM_WDOG(host));
	do_switch(slot->host, emm_switch);

	slot->cached_switch = emm_switch;

	msleep(20);

	writeq(wdog, slot->host->base + MIO_EMM_WDOG(host));
}

/* Switch to another slot if needed */
static void cvm_mmc_switch_to(struct cvm_mmc_slot *slot)
{
	struct cvm_mmc_host *host = slot->host;
	struct cvm_mmc_slot *old_slot;
	u64 emm_sample, emm_switch;

	if (slot->bus_id == host->last_slot)
		return;

	if (host->last_slot >= 0 && host->slot[host->last_slot]) {
		old_slot = host->slot[host->last_slot];
		old_slot->cached_switch = readq(host->base + MIO_EMM_SWITCH(host));
		old_slot->cached_rca = readq(host->base + MIO_EMM_RCA(host));
	}

	writeq(slot->cached_rca, host->base + MIO_EMM_RCA(host));
	emm_switch = slot->cached_switch;
	set_bus_id(&emm_switch, slot->bus_id);
	do_switch(host, emm_switch);

	emm_sample = FIELD_PREP(MIO_EMM_SAMPLE_CMD_CNT, slot->cmd_cnt) |
		     FIELD_PREP(MIO_EMM_SAMPLE_DAT_CNT, slot->dat_cnt);
	writeq(emm_sample, host->base + MIO_EMM_SAMPLE(host));

	host->last_slot = slot->bus_id;
}

static void do_read(struct cvm_mmc_host *host, struct mmc_request *req,
		    u64 dbuf)
{
	struct sg_mapping_iter *smi = &host->smi;
	int data_len = req->data->blocks * req->data->blksz;
	int bytes_xfered, shift = -1;
	u64 dat = 0;

	/* Auto inc from offset zero */
	writeq((0x10000 | (dbuf << 6)), host->base + MIO_EMM_BUF_IDX(host));

	for (bytes_xfered = 0; bytes_xfered < data_len;) {
		if (smi->consumed >= smi->length) {
			if (!sg_miter_next(smi))
				break;
			smi->consumed = 0;
		}

		if (shift < 0) {
			dat = readq(host->base + MIO_EMM_BUF_DAT(host));
			shift = 56;
		}

		while (smi->consumed < smi->length && shift >= 0) {
			((u8 *)smi->addr)[smi->consumed] = (dat >> shift) & 0xff;
			bytes_xfered++;
			smi->consumed++;
			shift -= 8;
		}
	}

	sg_miter_stop(smi);
	req->data->bytes_xfered = bytes_xfered;
	req->data->error = 0;
}

static void do_write(struct mmc_request *req)
{
	req->data->bytes_xfered = req->data->blocks * req->data->blksz;
	req->data->error = 0;
}

static void set_cmd_response(struct cvm_mmc_host *host, struct mmc_request *req,
			     u64 rsp_sts)
{
	u64 rsp_hi, rsp_lo;

	if (!(rsp_sts & MIO_EMM_RSP_STS_RSP_VAL))
		return;

	rsp_lo = readq(host->base + MIO_EMM_RSP_LO(host));

	switch (FIELD_GET(MIO_EMM_RSP_STS_RSP_TYPE, rsp_sts)) {
	case 1:
	case 3:
		req->cmd->resp[0] = (rsp_lo >> 8) & 0xffffffff;
		req->cmd->resp[1] = 0;
		req->cmd->resp[2] = 0;
		req->cmd->resp[3] = 0;
		break;
	case 2:
		req->cmd->resp[3] = rsp_lo & 0xffffffff;
		req->cmd->resp[2] = (rsp_lo >> 32) & 0xffffffff;
		rsp_hi = readq(host->base + MIO_EMM_RSP_HI(host));
		req->cmd->resp[1] = rsp_hi & 0xffffffff;
		req->cmd->resp[0] = (rsp_hi >> 32) & 0xffffffff;
		break;
	}
}

static int get_dma_dir(struct mmc_data *data)
{
	return (data->flags & MMC_DATA_WRITE) ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
}

static int finish_dma_single(struct cvm_mmc_host *host, struct mmc_data *data)
{
	data->bytes_xfered = data->blocks * data->blksz;
	data->error = 0;
	return 1;
}

static int finish_dma_sg(struct cvm_mmc_host *host, struct mmc_data *data)
{
	u64 fifo_cfg;
	int count;

	/* Check if there are any pending requests left */
	fifo_cfg = readq(host->dma_base + MIO_EMM_DMA_FIFO_CFG(host));
	count = FIELD_GET(MIO_EMM_DMA_FIFO_CFG_COUNT, fifo_cfg);
	if (count)
		dev_err(host->dev, "%u requests still pending\n", count);

	data->bytes_xfered = data->blocks * data->blksz;
	data->error = 0;

	/* Clear and disable FIFO */
	writeq(BIT_ULL(16), host->dma_base + MIO_EMM_DMA_FIFO_CFG(host));
	dma_unmap_sg(host->dev, data->sg, data->sg_len, get_dma_dir(data));
	return 1;
}

static int finish_dma(struct cvm_mmc_host *host, struct mmc_data *data)
{
	if (host->use_sg && data->sg_len > 1)
		return finish_dma_sg(host, data);
	else
		return finish_dma_single(host, data);
}

static int check_status(u64 rsp_sts)
{
	if (rsp_sts & MIO_EMM_RSP_STS_RSP_BAD_STS ||
	    rsp_sts & MIO_EMM_RSP_STS_RSP_CRC_ERR ||
	    rsp_sts & MIO_EMM_RSP_STS_BLK_CRC_ERR)
		return -EILSEQ;
	if (rsp_sts & MIO_EMM_RSP_STS_RSP_TIMEOUT ||
	    rsp_sts & MIO_EMM_RSP_STS_BLK_TIMEOUT)
		return -ETIMEDOUT;
	if (rsp_sts & MIO_EMM_RSP_STS_DBUF_ERR)
		return -EIO;
	return 0;
}

/* Try to clean up failed DMA. */
static void cleanup_dma(struct cvm_mmc_host *host, u64 rsp_sts)
{
	u64 emm_dma;

	emm_dma = readq(host->base + MIO_EMM_DMA(host));
	emm_dma |= FIELD_PREP(MIO_EMM_DMA_VAL, 1) |
		   FIELD_PREP(MIO_EMM_DMA_DAT_NULL, 1);
	set_bus_id(&emm_dma, get_bus_id(rsp_sts));
	writeq(emm_dma, host->base + MIO_EMM_DMA(host));
}

irqreturn_t cvm_mmc_interrupt(int irq, void *dev_id)
{
	struct cvm_mmc_host *host = dev_id;
	struct mmc_request *req;
	unsigned long flags = 0;
	u64 emm_int, rsp_sts;
	bool host_done;

	if (host->need_irq_handler_lock)
		spin_lock_irqsave(&host->irq_handler_lock, flags);
	else
		__acquire(&host->irq_handler_lock);

	/* Clear interrupt bits (write 1 clears ). */
	emm_int = readq(host->base + MIO_EMM_INT(host));
	writeq(emm_int, host->base + MIO_EMM_INT(host));

	if (emm_int & MIO_EMM_INT_SWITCH_ERR)
		check_switch_errors(host);

	req = host->current_req;
	if (!req)
		goto out;

	rsp_sts = readq(host->base + MIO_EMM_RSP_STS(host));
	/*
	 * dma_val set means DMA is still in progress. Don't touch
	 * the request and wait for the interrupt indicating that
	 * the DMA is finished.
	 */
	if ((rsp_sts & MIO_EMM_RSP_STS_DMA_VAL) && host->dma_active)
		goto out;

	if (!host->dma_active && req->data &&
	    (emm_int & MIO_EMM_INT_BUF_DONE)) {
		unsigned int type = (rsp_sts >> 7) & 3;

		if (type == 1)
			do_read(host, req, rsp_sts & MIO_EMM_RSP_STS_DBUF);
		else if (type == 2)
			do_write(req);
	}

	host_done = emm_int & MIO_EMM_INT_CMD_DONE ||
		    emm_int & MIO_EMM_INT_DMA_DONE ||
		    emm_int & MIO_EMM_INT_CMD_ERR  ||
		    emm_int & MIO_EMM_INT_DMA_ERR;

	if (!(host_done && req->done))
		goto no_req_done;

	req->cmd->error = check_status(rsp_sts);

	if (host->dma_active && req->data)
		if (!finish_dma(host, req->data))
			goto no_req_done;

	set_cmd_response(host, req, rsp_sts);
	if ((emm_int & MIO_EMM_INT_DMA_ERR) &&
	    (rsp_sts & MIO_EMM_RSP_STS_DMA_PEND))
		cleanup_dma(host, rsp_sts);

	host->current_req = NULL;
	req->done(req);

no_req_done:
	if (host->dmar_fixup_done)
		host->dmar_fixup_done(host);
	if (host_done)
		host->release_bus(host);
out:
	if (host->need_irq_handler_lock)
		spin_unlock_irqrestore(&host->irq_handler_lock, flags);
	else
		__release(&host->irq_handler_lock);
	return IRQ_RETVAL(emm_int != 0);
}

/*
 * Program DMA_CFG and if needed DMA_ADR.
 * Returns 0 on error, DMA address otherwise.
 */
static u64 prepare_dma_single(struct cvm_mmc_host *host, struct mmc_data *data)
{
	u64 dma_cfg, addr;
	int count, rw;

	count = dma_map_sg(host->dev, data->sg, data->sg_len,
			   get_dma_dir(data));
	if (!count)
		return 0;

	rw = (data->flags & MMC_DATA_WRITE) ? 1 : 0;
	dma_cfg = FIELD_PREP(MIO_EMM_DMA_CFG_EN, 1) |
		  FIELD_PREP(MIO_EMM_DMA_CFG_RW, rw);
#ifdef __LITTLE_ENDIAN
	dma_cfg |= FIELD_PREP(MIO_EMM_DMA_CFG_ENDIAN, 1);
#endif
	dma_cfg |= FIELD_PREP(MIO_EMM_DMA_CFG_SIZE,
			      (sg_dma_len(&data->sg[0]) / 8) - 1);

	addr = sg_dma_address(&data->sg[0]);
	if (!host->big_dma_addr)
		dma_cfg |= FIELD_PREP(MIO_EMM_DMA_CFG_ADR, addr);
	writeq(dma_cfg, host->dma_base + MIO_EMM_DMA_CFG(host));

	pr_debug("[%s] sg_dma_len: %u  total sg_elem: %d\n",
		 (rw) ? "W" : "R", sg_dma_len(&data->sg[0]), count);

	if (host->big_dma_addr)
		writeq(addr, host->dma_base + MIO_EMM_DMA_ADR(host));
	return addr;
}

/*
 * Queue complete sg list into the FIFO.
 * Returns 0 on error, 1 otherwise.
 */
static u64 prepare_dma_sg(struct cvm_mmc_host *host, struct mmc_data *data)
{
	struct scatterlist *sg;
	u64 fifo_cmd, addr;
	int count, i, rw;

	count = dma_map_sg(host->dev, data->sg, data->sg_len,
			   get_dma_dir(data));
	if (!count)
		return 0;
	if (count > 16)
		goto error;

	/* Enable FIFO by removing CLR bit */
	writeq(0, host->dma_base + MIO_EMM_DMA_FIFO_CFG(host));

	for_each_sg(data->sg, sg, count, i) {
		/* Program DMA address */
		addr = sg_dma_address(sg);
		if (addr & 7)
			goto error;
		writeq(addr, host->dma_base + MIO_EMM_DMA_FIFO_ADR(host));

		/*
		 * If we have scatter-gather support we also have an extra
		 * register for the DMA addr, so no need to check
		 * host->big_dma_addr here.
		 */
		rw = (data->flags & MMC_DATA_WRITE) ? 1 : 0;
		fifo_cmd = FIELD_PREP(MIO_EMM_DMA_FIFO_CMD_RW, rw);

		/* enable interrupts on the last element */
		fifo_cmd |= FIELD_PREP(MIO_EMM_DMA_FIFO_CMD_INTDIS,
				       (i + 1 == count) ? 0 : 1);

#ifdef __LITTLE_ENDIAN
		fifo_cmd |= FIELD_PREP(MIO_EMM_DMA_FIFO_CMD_ENDIAN, 1);
#endif
		fifo_cmd |= FIELD_PREP(MIO_EMM_DMA_FIFO_CMD_SIZE,
				       sg_dma_len(sg) / 8 - 1);
		/*
		 * The write copies the address and the command to the FIFO
		 * and increments the FIFO's COUNT field.
		 */
		writeq(fifo_cmd, host->dma_base + MIO_EMM_DMA_FIFO_CMD(host));
		pr_debug("[%s] sg_dma_len: %u  sg_elem: %d/%d\n",
			 (rw) ? "W" : "R", sg_dma_len(sg), i, count);
	}

	/*
	 * In difference to prepare_dma_single we don't return the
	 * address here, as it would not make sense for scatter-gather.
	 * The dma fixup is only required on models that don't support
	 * scatter-gather, so that is not a problem.
	 */
	return 1;

error:
	WARN_ON_ONCE(1);
	dma_unmap_sg(host->dev, data->sg, data->sg_len, get_dma_dir(data));
	/* Disable FIFO */
	writeq(BIT_ULL(16), host->dma_base + MIO_EMM_DMA_FIFO_CFG(host));
	return 0;
}

static u64 prepare_dma(struct cvm_mmc_host *host, struct mmc_data *data)
{
	if (host->use_sg && data->sg_len > 1)
		return prepare_dma_sg(host, data);
	else
		return prepare_dma_single(host, data);
}

static u64 prepare_ext_dma(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct cvm_mmc_slot *slot = mmc_priv(mmc);
	u64 emm_dma;

	emm_dma = FIELD_PREP(MIO_EMM_DMA_VAL, 1) |
		  FIELD_PREP(MIO_EMM_DMA_SECTOR,
			     mmc_card_is_blockaddr(mmc->card) ? 1 : 0) |
		  FIELD_PREP(MIO_EMM_DMA_RW,
			     (mrq->data->flags & MMC_DATA_WRITE) ? 1 : 0) |
		  FIELD_PREP(MIO_EMM_DMA_BLOCK_CNT, mrq->data->blocks) |
		  FIELD_PREP(MIO_EMM_DMA_CARD_ADDR, mrq->cmd->arg);
	set_bus_id(&emm_dma, slot->bus_id);

	if (mmc_card_mmc(mmc->card) || (mmc_card_sd(mmc->card) &&
	    (mmc->card->scr.cmds & SD_SCR_CMD23_SUPPORT)))
		emm_dma |= FIELD_PREP(MIO_EMM_DMA_MULTI, 1);

	pr_debug("[%s] blocks: %u  multi: %d\n",
		(emm_dma & MIO_EMM_DMA_RW) ? "W" : "R",
		 mrq->data->blocks, (emm_dma & MIO_EMM_DMA_MULTI) ? 1 : 0);
	return emm_dma;
}

static void cvm_mmc_dma_request(struct mmc_host *mmc,
				struct mmc_request *mrq)
{
	struct cvm_mmc_slot *slot = mmc_priv(mmc);
	struct cvm_mmc_host *host = slot->host;
	struct mmc_data *data;
	u64 emm_dma, addr;

	if (!mrq->data || !mrq->data->sg || !mrq->data->sg_len ||
	    !mrq->stop || mrq->stop->opcode != MMC_STOP_TRANSMISSION) {
		dev_err(&mmc->card->dev,
			"Error: cmv_mmc_dma_request no data\n");
		goto error;
	}

	cvm_mmc_switch_to(slot);

	data = mrq->data;
	pr_debug("DMA request  blocks: %d  block_size: %d  total_size: %d\n",
		 data->blocks, data->blksz, data->blocks * data->blksz);
	if (data->timeout_ns)
		set_wdog(slot, data->timeout_ns);

	WARN_ON(host->current_req);
	host->current_req = mrq;

	emm_dma = prepare_ext_dma(mmc, mrq);
	addr = prepare_dma(host, data);
	if (!addr) {
		dev_err(host->dev, "prepare_dma failed\n");
		goto error;
	}

	host->dma_active = true;
	host->int_enable(host, MIO_EMM_INT_CMD_ERR | MIO_EMM_INT_DMA_DONE |
			 MIO_EMM_INT_DMA_ERR);

	if (host->dmar_fixup)
		host->dmar_fixup(host, mrq->cmd, data, addr);

	/*
	 * If we have a valid SD card in the slot, we set the response
	 * bit mask to check for CRC errors and timeouts only.
	 * Otherwise, use the default power reset value.
	 */
	if (mmc_card_sd(mmc->card))
		writeq(0x00b00000ull, host->base + MIO_EMM_STS_MASK(host));
	else
		writeq(0xe4390080ull, host->base + MIO_EMM_STS_MASK(host));
	writeq(emm_dma, host->base + MIO_EMM_DMA(host));
	return;

error:
	mrq->cmd->error = -EINVAL;
	if (mrq->done)
		mrq->done(mrq);
	host->release_bus(host);
}

static void do_read_request(struct cvm_mmc_host *host, struct mmc_request *mrq)
{
	sg_miter_start(&host->smi, mrq->data->sg, mrq->data->sg_len,
		       SG_MITER_ATOMIC | SG_MITER_TO_SG);
}

static void do_write_request(struct cvm_mmc_host *host, struct mmc_request *mrq)
{
	unsigned int data_len = mrq->data->blocks * mrq->data->blksz;
	struct sg_mapping_iter *smi = &host->smi;
	unsigned int bytes_xfered;
	int shift = 56;
	u64 dat = 0;

	/* Copy data to the xmit buffer before issuing the command. */
	sg_miter_start(smi, mrq->data->sg, mrq->data->sg_len, SG_MITER_FROM_SG);

	/* Auto inc from offset zero, dbuf zero */
	writeq(0x10000ull, host->base + MIO_EMM_BUF_IDX(host));

	for (bytes_xfered = 0; bytes_xfered < data_len;) {
		if (smi->consumed >= smi->length) {
			if (!sg_miter_next(smi))
				break;
			smi->consumed = 0;
		}

		while (smi->consumed < smi->length && shift >= 0) {
			dat |= (u64)((u8 *)smi->addr)[smi->consumed] << shift;
			bytes_xfered++;
			smi->consumed++;
			shift -= 8;
		}

		if (shift < 0) {
			writeq(dat, host->base + MIO_EMM_BUF_DAT(host));
			shift = 56;
			dat = 0;
		}
	}
	sg_miter_stop(smi);
}

static void cvm_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct cvm_mmc_slot *slot = mmc_priv(mmc);
	struct cvm_mmc_host *host = slot->host;
	struct mmc_command *cmd = mrq->cmd;
	struct cvm_mmc_cr_mods mods;
	u64 emm_cmd, rsp_sts;
	int retries = 100;

	/*
	 * Note about locking:
	 * All MMC devices share the same bus and controller. Allow only a
	 * single user of the bootbus/MMC bus at a time. The lock is acquired
	 * on all entry points from the MMC layer.
	 *
	 * For requests the lock is only released after the completion
	 * interrupt!
	 */
	host->acquire_bus(host);

	if (cmd->opcode == MMC_READ_MULTIPLE_BLOCK ||
	    cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK)
		return cvm_mmc_dma_request(mmc, mrq);

	cvm_mmc_switch_to(slot);

	mods = cvm_mmc_get_cr_mods(cmd);

	WARN_ON(host->current_req);
	host->current_req = mrq;

	if (cmd->data) {
		if (cmd->data->flags & MMC_DATA_READ)
			do_read_request(host, mrq);
		else
			do_write_request(host, mrq);

		if (cmd->data->timeout_ns)
			set_wdog(slot, cmd->data->timeout_ns);
	} else
		set_wdog(slot, 0);

	host->dma_active = false;
	host->int_enable(host, MIO_EMM_INT_CMD_DONE | MIO_EMM_INT_CMD_ERR);

	emm_cmd = FIELD_PREP(MIO_EMM_CMD_VAL, 1) |
		  FIELD_PREP(MIO_EMM_CMD_CTYPE_XOR, mods.ctype_xor) |
		  FIELD_PREP(MIO_EMM_CMD_RTYPE_XOR, mods.rtype_xor) |
		  FIELD_PREP(MIO_EMM_CMD_IDX, cmd->opcode) |
		  FIELD_PREP(MIO_EMM_CMD_ARG, cmd->arg);
	set_bus_id(&emm_cmd, slot->bus_id);
	if (cmd->data && mmc_cmd_type(cmd) == MMC_CMD_ADTC)
		emm_cmd |= FIELD_PREP(MIO_EMM_CMD_OFFSET,
				64 - ((cmd->data->blocks * cmd->data->blksz) / 8));

	writeq(0, host->base + MIO_EMM_STS_MASK(host));

retry:
	rsp_sts = readq(host->base + MIO_EMM_RSP_STS(host));
	if (rsp_sts & MIO_EMM_RSP_STS_DMA_VAL ||
	    rsp_sts & MIO_EMM_RSP_STS_CMD_VAL ||
	    rsp_sts & MIO_EMM_RSP_STS_SWITCH_VAL ||
	    rsp_sts & MIO_EMM_RSP_STS_DMA_PEND) {
		udelay(10);
		if (--retries)
			goto retry;
	}
	if (!retries)
		dev_err(host->dev, "Bad status: %llx before command write\n", rsp_sts);
	writeq(emm_cmd, host->base + MIO_EMM_CMD(host));
}

static void cvm_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct cvm_mmc_slot *slot = mmc_priv(mmc);
	struct cvm_mmc_host *host = slot->host;
	int clk_period = 0, power_class = 10, bus_width = 0;
	u64 clock, emm_switch;

	host->acquire_bus(host);
	cvm_mmc_switch_to(slot);

	/* Set the power state */
	switch (ios->power_mode) {
	case MMC_POWER_ON:
		break;

	case MMC_POWER_OFF:
		cvm_mmc_reset_bus(slot);
		if (host->global_pwr_gpiod)
			host->set_shared_power(host, 0);
		else if (!IS_ERR(mmc->supply.vmmc))
			mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, 0);
		break;

	case MMC_POWER_UP:
		if (host->global_pwr_gpiod)
			host->set_shared_power(host, 1);
		else if (!IS_ERR(mmc->supply.vmmc))
			mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, ios->vdd);
		break;
	}

	/* Convert bus width to HW definition */
	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_8:
		bus_width = 2;
		break;
	case MMC_BUS_WIDTH_4:
		bus_width = 1;
		break;
	case MMC_BUS_WIDTH_1:
		bus_width = 0;
		break;
	}

	/* DDR is available for 4/8 bit bus width */
	if (ios->bus_width && ios->timing == MMC_TIMING_MMC_DDR52)
		bus_width |= 4;

	/* Change the clock frequency. */
	clock = ios->clock;
	if (clock > 52000000)
		clock = 52000000;
	slot->clock = clock;

	if (clock)
		clk_period = (host->sys_freq + clock - 1) / (2 * clock);

	emm_switch = FIELD_PREP(MIO_EMM_SWITCH_HS_TIMING,
				(ios->timing == MMC_TIMING_MMC_HS)) |
		     FIELD_PREP(MIO_EMM_SWITCH_BUS_WIDTH, bus_width) |
		     FIELD_PREP(MIO_EMM_SWITCH_POWER_CLASS, power_class) |
		     FIELD_PREP(MIO_EMM_SWITCH_CLK_HI, clk_period) |
		     FIELD_PREP(MIO_EMM_SWITCH_CLK_LO, clk_period);
	set_bus_id(&emm_switch, slot->bus_id);

	if (!switch_val_changed(slot, emm_switch))
		goto out;

	set_wdog(slot, 0);
	do_switch(host, emm_switch);
	slot->cached_switch = emm_switch;
out:
	host->release_bus(host);
}

static const struct mmc_host_ops cvm_mmc_ops = {
	.request        = cvm_mmc_request,
	.set_ios        = cvm_mmc_set_ios,
	.get_ro		= mmc_gpio_get_ro,
	.get_cd		= mmc_gpio_get_cd,
};

static void cvm_mmc_set_clock(struct cvm_mmc_slot *slot, unsigned int clock)
{
	struct mmc_host *mmc = slot->mmc;

	clock = min(clock, mmc->f_max);
	clock = max(clock, mmc->f_min);
	slot->clock = clock;
}

static int cvm_mmc_init_lowlevel(struct cvm_mmc_slot *slot)
{
	struct cvm_mmc_host *host = slot->host;
	u64 emm_switch;

	/* Enable this bus slot. */
	host->emm_cfg |= (1ull << slot->bus_id);
	writeq(host->emm_cfg, slot->host->base + MIO_EMM_CFG(host));
	udelay(10);

	/* Program initial clock speed and power. */
	cvm_mmc_set_clock(slot, slot->mmc->f_min);
	emm_switch = FIELD_PREP(MIO_EMM_SWITCH_POWER_CLASS, 10);
	emm_switch |= FIELD_PREP(MIO_EMM_SWITCH_CLK_HI,
				 (host->sys_freq / slot->clock) / 2);
	emm_switch |= FIELD_PREP(MIO_EMM_SWITCH_CLK_LO,
				 (host->sys_freq / slot->clock) / 2);

	/* Make the changes take effect on this bus slot. */
	set_bus_id(&emm_switch, slot->bus_id);
	do_switch(host, emm_switch);

	slot->cached_switch = emm_switch;

	/*
	 * Set watchdog timeout value and default reset value
	 * for the mask register. Finally, set the CARD_RCA
	 * bit so that we can get the card address relative
	 * to the CMD register for CMD7 transactions.
	 */
	set_wdog(slot, 0);
	writeq(0xe4390080ull, host->base + MIO_EMM_STS_MASK(host));
	writeq(1, host->base + MIO_EMM_RCA(host));
	return 0;
}

static int cvm_mmc_of_parse(struct device *dev, struct cvm_mmc_slot *slot)
{
	u32 id, cmd_skew = 0, dat_skew = 0, bus_width = 0;
	struct device_node *node = dev->of_node;
	struct mmc_host *mmc = slot->mmc;
	u64 clock_period;
	int ret;

	ret = of_property_read_u32(node, "reg", &id);
	if (ret) {
		dev_err(dev, "Missing or invalid reg property on %pOF\n", node);
		return ret;
	}

	if (id >= CAVIUM_MAX_MMC || slot->host->slot[id]) {
		dev_err(dev, "Invalid reg property on %pOF\n", node);
		return -EINVAL;
	}

	ret = mmc_regulator_get_supply(mmc);
	if (ret == -EPROBE_DEFER)
		return ret;
	/*
	 * Legacy Octeon firmware has no regulator entry, fall-back to
	 * a hard-coded voltage to get a sane OCR.
	 */
	if (IS_ERR(mmc->supply.vmmc))
		mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;

	/* Common MMC bindings */
	ret = mmc_of_parse(mmc);
	if (ret)
		return ret;

	/* Set bus width */
	if (!(mmc->caps & (MMC_CAP_8_BIT_DATA | MMC_CAP_4_BIT_DATA))) {
		of_property_read_u32(node, "cavium,bus-max-width", &bus_width);
		if (bus_width == 8)
			mmc->caps |= MMC_CAP_8_BIT_DATA | MMC_CAP_4_BIT_DATA;
		else if (bus_width == 4)
			mmc->caps |= MMC_CAP_4_BIT_DATA;
	}

	/* Set maximum and minimum frequency */
	if (!mmc->f_max)
		of_property_read_u32(node, "spi-max-frequency", &mmc->f_max);
	if (!mmc->f_max || mmc->f_max > 52000000)
		mmc->f_max = 52000000;
	mmc->f_min = 400000;

	/* Sampling register settings, period in picoseconds */
	clock_period = 1000000000000ull / slot->host->sys_freq;
	of_property_read_u32(node, "cavium,cmd-clk-skew", &cmd_skew);
	of_property_read_u32(node, "cavium,dat-clk-skew", &dat_skew);
	slot->cmd_cnt = (cmd_skew + clock_period / 2) / clock_period;
	slot->dat_cnt = (dat_skew + clock_period / 2) / clock_period;

	return id;
}

int cvm_mmc_of_slot_probe(struct device *dev, struct cvm_mmc_host *host)
{
	struct cvm_mmc_slot *slot;
	struct mmc_host *mmc;
	int ret, id;

	mmc = mmc_alloc_host(sizeof(struct cvm_mmc_slot), dev);
	if (!mmc)
		return -ENOMEM;

	slot = mmc_priv(mmc);
	slot->mmc = mmc;
	slot->host = host;

	ret = cvm_mmc_of_parse(dev, slot);
	if (ret < 0)
		goto error;
	id = ret;

	/* Set up host parameters */
	mmc->ops = &cvm_mmc_ops;

	/*
	 * We only have a 3.3v supply, we cannot support any
	 * of the UHS modes. We do support the high speed DDR
	 * modes up to 52MHz.
	 *
	 * Disable bounce buffers for max_segs = 1
	 */
	mmc->caps |= MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED |
		     MMC_CAP_ERASE | MMC_CAP_CMD23 | MMC_CAP_POWER_OFF_CARD |
		     MMC_CAP_3_3V_DDR | MMC_CAP_NO_BOUNCE_BUFF;

	if (host->use_sg)
		mmc->max_segs = 16;
	else
		mmc->max_segs = 1;

	/* DMA size field can address up to 8 MB */
	mmc->max_seg_size = 8 * 1024 * 1024;
	mmc->max_req_size = mmc->max_seg_size;
	/* External DMA is in 512 byte blocks */
	mmc->max_blk_size = 512;
	/* DMA block count field is 15 bits */
	mmc->max_blk_count = 32767;

	slot->clock = mmc->f_min;
	slot->bus_id = id;
	slot->cached_rca = 1;

	host->acquire_bus(host);
	host->slot[id] = slot;
	cvm_mmc_switch_to(slot);
	cvm_mmc_init_lowlevel(slot);
	host->release_bus(host);

	ret = mmc_add_host(mmc);
	if (ret) {
		dev_err(dev, "mmc_add_host() returned %d\n", ret);
		slot->host->slot[id] = NULL;
		goto error;
	}
	return 0;

error:
	mmc_free_host(slot->mmc);
	return ret;
}

int cvm_mmc_of_slot_remove(struct cvm_mmc_slot *slot)
{
	mmc_remove_host(slot->mmc);
	slot->host->slot[slot->bus_id] = NULL;
	mmc_free_host(slot->mmc);
	return 0;
}
