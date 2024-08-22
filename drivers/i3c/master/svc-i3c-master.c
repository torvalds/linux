// SPDX-License-Identifier: GPL-2.0
/*
 * Silvaco dual-role I3C master driver
 *
 * Copyright (C) 2020 Silvaco
 * Author: Miquel RAYNAL <miquel.raynal@bootlin.com>
 * Based on a work from: Conor Culhane <conor.culhane@silvaco.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/i3c/master.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

/* Master Mode Registers */
#define SVC_I3C_MCONFIG      0x000
#define   SVC_I3C_MCONFIG_MASTER_EN BIT(0)
#define   SVC_I3C_MCONFIG_DISTO(x) FIELD_PREP(BIT(3), (x))
#define   SVC_I3C_MCONFIG_HKEEP(x) FIELD_PREP(GENMASK(5, 4), (x))
#define   SVC_I3C_MCONFIG_ODSTOP(x) FIELD_PREP(BIT(6), (x))
#define   SVC_I3C_MCONFIG_PPBAUD(x) FIELD_PREP(GENMASK(11, 8), (x))
#define   SVC_I3C_MCONFIG_PPLOW(x) FIELD_PREP(GENMASK(15, 12), (x))
#define   SVC_I3C_MCONFIG_ODBAUD(x) FIELD_PREP(GENMASK(23, 16), (x))
#define   SVC_I3C_MCONFIG_ODHPP(x) FIELD_PREP(BIT(24), (x))
#define   SVC_I3C_MCONFIG_SKEW(x) FIELD_PREP(GENMASK(27, 25), (x))
#define   SVC_I3C_MCONFIG_I2CBAUD(x) FIELD_PREP(GENMASK(31, 28), (x))

#define SVC_I3C_MCTRL        0x084
#define   SVC_I3C_MCTRL_REQUEST_MASK GENMASK(2, 0)
#define   SVC_I3C_MCTRL_REQUEST_NONE 0
#define   SVC_I3C_MCTRL_REQUEST_START_ADDR 1
#define   SVC_I3C_MCTRL_REQUEST_STOP 2
#define   SVC_I3C_MCTRL_REQUEST_IBI_ACKNACK 3
#define   SVC_I3C_MCTRL_REQUEST_PROC_DAA 4
#define   SVC_I3C_MCTRL_REQUEST_AUTO_IBI 7
#define   SVC_I3C_MCTRL_TYPE_I3C 0
#define   SVC_I3C_MCTRL_TYPE_I2C BIT(4)
#define   SVC_I3C_MCTRL_IBIRESP_AUTO 0
#define   SVC_I3C_MCTRL_IBIRESP_ACK_WITHOUT_BYTE 0
#define   SVC_I3C_MCTRL_IBIRESP_ACK_WITH_BYTE BIT(7)
#define   SVC_I3C_MCTRL_IBIRESP_NACK BIT(6)
#define   SVC_I3C_MCTRL_IBIRESP_MANUAL GENMASK(7, 6)
#define   SVC_I3C_MCTRL_DIR(x) FIELD_PREP(BIT(8), (x))
#define   SVC_I3C_MCTRL_DIR_WRITE 0
#define   SVC_I3C_MCTRL_DIR_READ 1
#define   SVC_I3C_MCTRL_ADDR(x) FIELD_PREP(GENMASK(15, 9), (x))
#define   SVC_I3C_MCTRL_RDTERM(x) FIELD_PREP(GENMASK(23, 16), (x))

#define SVC_I3C_MSTATUS      0x088
#define   SVC_I3C_MSTATUS_STATE(x) FIELD_GET(GENMASK(2, 0), (x))
#define   SVC_I3C_MSTATUS_STATE_DAA(x) (SVC_I3C_MSTATUS_STATE(x) == 5)
#define   SVC_I3C_MSTATUS_STATE_IDLE(x) (SVC_I3C_MSTATUS_STATE(x) == 0)
#define   SVC_I3C_MSTATUS_BETWEEN(x) FIELD_GET(BIT(4), (x))
#define   SVC_I3C_MSTATUS_NACKED(x) FIELD_GET(BIT(5), (x))
#define   SVC_I3C_MSTATUS_IBITYPE(x) FIELD_GET(GENMASK(7, 6), (x))
#define   SVC_I3C_MSTATUS_IBITYPE_IBI 1
#define   SVC_I3C_MSTATUS_IBITYPE_MASTER_REQUEST 2
#define   SVC_I3C_MSTATUS_IBITYPE_HOT_JOIN 3
#define   SVC_I3C_MINT_SLVSTART BIT(8)
#define   SVC_I3C_MINT_MCTRLDONE BIT(9)
#define   SVC_I3C_MINT_COMPLETE BIT(10)
#define   SVC_I3C_MINT_RXPEND BIT(11)
#define   SVC_I3C_MINT_TXNOTFULL BIT(12)
#define   SVC_I3C_MINT_IBIWON BIT(13)
#define   SVC_I3C_MINT_ERRWARN BIT(15)
#define   SVC_I3C_MSTATUS_SLVSTART(x) FIELD_GET(SVC_I3C_MINT_SLVSTART, (x))
#define   SVC_I3C_MSTATUS_MCTRLDONE(x) FIELD_GET(SVC_I3C_MINT_MCTRLDONE, (x))
#define   SVC_I3C_MSTATUS_COMPLETE(x) FIELD_GET(SVC_I3C_MINT_COMPLETE, (x))
#define   SVC_I3C_MSTATUS_RXPEND(x) FIELD_GET(SVC_I3C_MINT_RXPEND, (x))
#define   SVC_I3C_MSTATUS_TXNOTFULL(x) FIELD_GET(SVC_I3C_MINT_TXNOTFULL, (x))
#define   SVC_I3C_MSTATUS_IBIWON(x) FIELD_GET(SVC_I3C_MINT_IBIWON, (x))
#define   SVC_I3C_MSTATUS_ERRWARN(x) FIELD_GET(SVC_I3C_MINT_ERRWARN, (x))
#define   SVC_I3C_MSTATUS_IBIADDR(x) FIELD_GET(GENMASK(30, 24), (x))

#define SVC_I3C_IBIRULES     0x08C
#define   SVC_I3C_IBIRULES_ADDR(slot, addr) FIELD_PREP(GENMASK(29, 0), \
						       ((addr) & 0x3F) << ((slot) * 6))
#define   SVC_I3C_IBIRULES_ADDRS 5
#define   SVC_I3C_IBIRULES_MSB0 BIT(30)
#define   SVC_I3C_IBIRULES_NOBYTE BIT(31)
#define   SVC_I3C_IBIRULES_MANDBYTE 0
#define SVC_I3C_MINTSET      0x090
#define SVC_I3C_MINTCLR      0x094
#define SVC_I3C_MINTMASKED   0x098
#define SVC_I3C_MERRWARN     0x09C
#define   SVC_I3C_MERRWARN_NACK BIT(2)
#define   SVC_I3C_MERRWARN_TIMEOUT BIT(20)
#define SVC_I3C_MDMACTRL     0x0A0
#define SVC_I3C_MDATACTRL    0x0AC
#define   SVC_I3C_MDATACTRL_FLUSHTB BIT(0)
#define   SVC_I3C_MDATACTRL_FLUSHRB BIT(1)
#define   SVC_I3C_MDATACTRL_UNLOCK_TRIG BIT(3)
#define   SVC_I3C_MDATACTRL_TXTRIG_FIFO_NOT_FULL GENMASK(5, 4)
#define   SVC_I3C_MDATACTRL_RXTRIG_FIFO_NOT_EMPTY 0
#define   SVC_I3C_MDATACTRL_RXCOUNT(x) FIELD_GET(GENMASK(28, 24), (x))
#define   SVC_I3C_MDATACTRL_TXFULL BIT(30)
#define   SVC_I3C_MDATACTRL_RXEMPTY BIT(31)

#define SVC_I3C_MWDATAB      0x0B0
#define   SVC_I3C_MWDATAB_END BIT(8)

#define SVC_I3C_MWDATABE     0x0B4
#define SVC_I3C_MWDATAH      0x0B8
#define SVC_I3C_MWDATAHE     0x0BC
#define SVC_I3C_MRDATAB      0x0C0
#define SVC_I3C_MRDATAH      0x0C8
#define SVC_I3C_MWMSG_SDR    0x0D0
#define SVC_I3C_MRMSG_SDR    0x0D4
#define SVC_I3C_MWMSG_DDR    0x0D8
#define SVC_I3C_MRMSG_DDR    0x0DC

#define SVC_I3C_MDYNADDR     0x0E4
#define   SVC_MDYNADDR_VALID BIT(0)
#define   SVC_MDYNADDR_ADDR(x) FIELD_PREP(GENMASK(7, 1), (x))

#define SVC_I3C_MAX_DEVS 32
#define SVC_I3C_PM_TIMEOUT_MS 1000

/* This parameter depends on the implementation and may be tuned */
#define SVC_I3C_FIFO_SIZE 16

#define SVC_I3C_EVENT_IBI	BIT(0)
#define SVC_I3C_EVENT_HOTJOIN	BIT(1)

struct svc_i3c_cmd {
	u8 addr;
	bool rnw;
	u8 *in;
	const void *out;
	unsigned int len;
	unsigned int actual_len;
	struct i3c_priv_xfer *xfer;
	bool continued;
};

struct svc_i3c_xfer {
	struct list_head node;
	struct completion comp;
	int ret;
	unsigned int type;
	unsigned int ncmds;
	struct svc_i3c_cmd cmds[] __counted_by(ncmds);
};

struct svc_i3c_regs_save {
	u32 mconfig;
	u32 mdynaddr;
};

/**
 * struct svc_i3c_master - Silvaco I3C Master structure
 * @base: I3C master controller
 * @dev: Corresponding device
 * @regs: Memory mapping
 * @saved_regs: Volatile values for PM operations
 * @free_slots: Bit array of available slots
 * @addrs: Array containing the dynamic addresses of each attached device
 * @descs: Array of descriptors, one per attached device
 * @hj_work: Hot-join work
 * @ibi_work: IBI work
 * @irq: Main interrupt
 * @pclk: System clock
 * @fclk: Fast clock (bus)
 * @sclk: Slow clock (other events)
 * @xferqueue: Transfer queue structure
 * @xferqueue.list: List member
 * @xferqueue.cur: Current ongoing transfer
 * @xferqueue.lock: Queue lock
 * @ibi: IBI structure
 * @ibi.num_slots: Number of slots available in @ibi.slots
 * @ibi.slots: Available IBI slots
 * @ibi.tbq_slot: To be queued IBI slot
 * @ibi.lock: IBI lock
 * @lock: Transfer lock, protect between IBI work thread and callbacks from master
 * @enabled_events: Bit masks for enable events (IBI, HotJoin).
 */
struct svc_i3c_master {
	struct i3c_master_controller base;
	struct device *dev;
	void __iomem *regs;
	struct svc_i3c_regs_save saved_regs;
	u32 free_slots;
	u8 addrs[SVC_I3C_MAX_DEVS];
	struct i3c_dev_desc *descs[SVC_I3C_MAX_DEVS];
	struct work_struct hj_work;
	struct work_struct ibi_work;
	int irq;
	struct clk *pclk;
	struct clk *fclk;
	struct clk *sclk;
	struct {
		struct list_head list;
		struct svc_i3c_xfer *cur;
		/* Prevent races between transfers */
		spinlock_t lock;
	} xferqueue;
	struct {
		unsigned int num_slots;
		struct i3c_dev_desc **slots;
		struct i3c_ibi_slot *tbq_slot;
		/* Prevent races within IBI handlers */
		spinlock_t lock;
	} ibi;
	struct mutex lock;
	int enabled_events;
};

/**
 * struct svc_i3c_i2c_dev_data - Device specific data
 * @index: Index in the master tables corresponding to this device
 * @ibi: IBI slot index in the master structure
 * @ibi_pool: IBI pool associated to this device
 */
struct svc_i3c_i2c_dev_data {
	u8 index;
	int ibi;
	struct i3c_generic_ibi_pool *ibi_pool;
};

static inline bool is_events_enabled(struct svc_i3c_master *master, u32 mask)
{
	return !!(master->enabled_events & mask);
}

static bool svc_i3c_master_error(struct svc_i3c_master *master)
{
	u32 mstatus, merrwarn;

	mstatus = readl(master->regs + SVC_I3C_MSTATUS);
	if (SVC_I3C_MSTATUS_ERRWARN(mstatus)) {
		merrwarn = readl(master->regs + SVC_I3C_MERRWARN);
		writel(merrwarn, master->regs + SVC_I3C_MERRWARN);

		/* Ignore timeout error */
		if (merrwarn & SVC_I3C_MERRWARN_TIMEOUT) {
			dev_dbg(master->dev, "Warning condition: MSTATUS 0x%08x, MERRWARN 0x%08x\n",
				mstatus, merrwarn);
			return false;
		}

		dev_err(master->dev,
			"Error condition: MSTATUS 0x%08x, MERRWARN 0x%08x\n",
			mstatus, merrwarn);

		return true;
	}

	return false;
}

static void svc_i3c_master_enable_interrupts(struct svc_i3c_master *master, u32 mask)
{
	writel(mask, master->regs + SVC_I3C_MINTSET);
}

static void svc_i3c_master_disable_interrupts(struct svc_i3c_master *master)
{
	u32 mask = readl(master->regs + SVC_I3C_MINTSET);

	writel(mask, master->regs + SVC_I3C_MINTCLR);
}

static void svc_i3c_master_clear_merrwarn(struct svc_i3c_master *master)
{
	/* Clear pending warnings */
	writel(readl(master->regs + SVC_I3C_MERRWARN),
	       master->regs + SVC_I3C_MERRWARN);
}

static void svc_i3c_master_flush_fifo(struct svc_i3c_master *master)
{
	/* Flush FIFOs */
	writel(SVC_I3C_MDATACTRL_FLUSHTB | SVC_I3C_MDATACTRL_FLUSHRB,
	       master->regs + SVC_I3C_MDATACTRL);
}

static void svc_i3c_master_reset_fifo_trigger(struct svc_i3c_master *master)
{
	u32 reg;

	/* Set RX and TX tigger levels, flush FIFOs */
	reg = SVC_I3C_MDATACTRL_FLUSHTB |
	      SVC_I3C_MDATACTRL_FLUSHRB |
	      SVC_I3C_MDATACTRL_UNLOCK_TRIG |
	      SVC_I3C_MDATACTRL_TXTRIG_FIFO_NOT_FULL |
	      SVC_I3C_MDATACTRL_RXTRIG_FIFO_NOT_EMPTY;
	writel(reg, master->regs + SVC_I3C_MDATACTRL);
}

static void svc_i3c_master_reset(struct svc_i3c_master *master)
{
	svc_i3c_master_clear_merrwarn(master);
	svc_i3c_master_reset_fifo_trigger(master);
	svc_i3c_master_disable_interrupts(master);
}

static inline struct svc_i3c_master *
to_svc_i3c_master(struct i3c_master_controller *master)
{
	return container_of(master, struct svc_i3c_master, base);
}

static void svc_i3c_master_hj_work(struct work_struct *work)
{
	struct svc_i3c_master *master;

	master = container_of(work, struct svc_i3c_master, hj_work);
	i3c_master_do_daa(&master->base);
}

static struct i3c_dev_desc *
svc_i3c_master_dev_from_addr(struct svc_i3c_master *master,
			     unsigned int ibiaddr)
{
	int i;

	for (i = 0; i < SVC_I3C_MAX_DEVS; i++)
		if (master->addrs[i] == ibiaddr)
			break;

	if (i == SVC_I3C_MAX_DEVS)
		return NULL;

	return master->descs[i];
}

static void svc_i3c_master_emit_stop(struct svc_i3c_master *master)
{
	writel(SVC_I3C_MCTRL_REQUEST_STOP, master->regs + SVC_I3C_MCTRL);

	/*
	 * This delay is necessary after the emission of a stop, otherwise eg.
	 * repeating IBIs do not get detected. There is a note in the manual
	 * about it, stating that the stop condition might not be settled
	 * correctly if a start condition follows too rapidly.
	 */
	udelay(1);
}

static int svc_i3c_master_handle_ibi(struct svc_i3c_master *master,
				     struct i3c_dev_desc *dev)
{
	struct svc_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_ibi_slot *slot;
	unsigned int count;
	u32 mdatactrl;
	int ret, val;
	u8 *buf;

	slot = i3c_generic_ibi_get_free_slot(data->ibi_pool);
	if (!slot)
		return -ENOSPC;

	slot->len = 0;
	buf = slot->data;

	ret = readl_relaxed_poll_timeout(master->regs + SVC_I3C_MSTATUS, val,
						SVC_I3C_MSTATUS_COMPLETE(val), 0, 1000);
	if (ret) {
		dev_err(master->dev, "Timeout when polling for COMPLETE\n");
		return ret;
	}

	while (SVC_I3C_MSTATUS_RXPEND(readl(master->regs + SVC_I3C_MSTATUS))  &&
	       slot->len < SVC_I3C_FIFO_SIZE) {
		mdatactrl = readl(master->regs + SVC_I3C_MDATACTRL);
		count = SVC_I3C_MDATACTRL_RXCOUNT(mdatactrl);
		readsl(master->regs + SVC_I3C_MRDATAB, buf, count);
		slot->len += count;
		buf += count;
	}

	master->ibi.tbq_slot = slot;

	return 0;
}

static void svc_i3c_master_ack_ibi(struct svc_i3c_master *master,
				   bool mandatory_byte)
{
	unsigned int ibi_ack_nack;

	ibi_ack_nack = SVC_I3C_MCTRL_REQUEST_IBI_ACKNACK;
	if (mandatory_byte)
		ibi_ack_nack |= SVC_I3C_MCTRL_IBIRESP_ACK_WITH_BYTE;
	else
		ibi_ack_nack |= SVC_I3C_MCTRL_IBIRESP_ACK_WITHOUT_BYTE;

	writel(ibi_ack_nack, master->regs + SVC_I3C_MCTRL);
}

static void svc_i3c_master_nack_ibi(struct svc_i3c_master *master)
{
	writel(SVC_I3C_MCTRL_REQUEST_IBI_ACKNACK |
	       SVC_I3C_MCTRL_IBIRESP_NACK,
	       master->regs + SVC_I3C_MCTRL);
}

static void svc_i3c_master_ibi_work(struct work_struct *work)
{
	struct svc_i3c_master *master = container_of(work, struct svc_i3c_master, ibi_work);
	struct svc_i3c_i2c_dev_data *data;
	unsigned int ibitype, ibiaddr;
	struct i3c_dev_desc *dev;
	u32 status, val;
	int ret;

	mutex_lock(&master->lock);
	/*
	 * IBIWON may be set before SVC_I3C_MCTRL_REQUEST_AUTO_IBI, causing
	 * readl_relaxed_poll_timeout() to return immediately. Consequently,
	 * ibitype will be 0 since it was last updated only after the 8th SCL
	 * cycle, leading to missed client IBI handlers.
	 *
	 * A typical scenario is when IBIWON occurs and bus arbitration is lost
	 * at svc_i3c_master_priv_xfers().
	 *
	 * Clear SVC_I3C_MINT_IBIWON before sending SVC_I3C_MCTRL_REQUEST_AUTO_IBI.
	 */
	writel(SVC_I3C_MINT_IBIWON, master->regs + SVC_I3C_MSTATUS);

	/* Acknowledge the incoming interrupt with the AUTOIBI mechanism */
	writel(SVC_I3C_MCTRL_REQUEST_AUTO_IBI |
	       SVC_I3C_MCTRL_IBIRESP_AUTO,
	       master->regs + SVC_I3C_MCTRL);

	/* Wait for IBIWON, should take approximately 100us */
	ret = readl_relaxed_poll_timeout(master->regs + SVC_I3C_MSTATUS, val,
					 SVC_I3C_MSTATUS_IBIWON(val), 0, 1000);
	if (ret) {
		dev_err(master->dev, "Timeout when polling for IBIWON\n");
		svc_i3c_master_emit_stop(master);
		goto reenable_ibis;
	}

	status = readl(master->regs + SVC_I3C_MSTATUS);
	ibitype = SVC_I3C_MSTATUS_IBITYPE(status);
	ibiaddr = SVC_I3C_MSTATUS_IBIADDR(status);

	/* Handle the critical responses to IBI's */
	switch (ibitype) {
	case SVC_I3C_MSTATUS_IBITYPE_IBI:
		dev = svc_i3c_master_dev_from_addr(master, ibiaddr);
		if (!dev || !is_events_enabled(master, SVC_I3C_EVENT_IBI))
			svc_i3c_master_nack_ibi(master);
		else
			svc_i3c_master_handle_ibi(master, dev);
		break;
	case SVC_I3C_MSTATUS_IBITYPE_HOT_JOIN:
		if (is_events_enabled(master, SVC_I3C_EVENT_HOTJOIN))
			svc_i3c_master_ack_ibi(master, false);
		else
			svc_i3c_master_nack_ibi(master);
		break;
	case SVC_I3C_MSTATUS_IBITYPE_MASTER_REQUEST:
		svc_i3c_master_nack_ibi(master);
		break;
	default:
		break;
	}

	/*
	 * If an error happened, we probably got interrupted and the exchange
	 * timedout. In this case we just drop everything, emit a stop and wait
	 * for the slave to interrupt again.
	 */
	if (svc_i3c_master_error(master)) {
		if (master->ibi.tbq_slot) {
			data = i3c_dev_get_master_data(dev);
			i3c_generic_ibi_recycle_slot(data->ibi_pool,
						     master->ibi.tbq_slot);
			master->ibi.tbq_slot = NULL;
		}

		svc_i3c_master_emit_stop(master);

		goto reenable_ibis;
	}

	/* Handle the non critical tasks */
	switch (ibitype) {
	case SVC_I3C_MSTATUS_IBITYPE_IBI:
		if (dev) {
			i3c_master_queue_ibi(dev, master->ibi.tbq_slot);
			master->ibi.tbq_slot = NULL;
		}
		svc_i3c_master_emit_stop(master);
		break;
	case SVC_I3C_MSTATUS_IBITYPE_HOT_JOIN:
		svc_i3c_master_emit_stop(master);
		if (is_events_enabled(master, SVC_I3C_EVENT_HOTJOIN))
			queue_work(master->base.wq, &master->hj_work);
		break;
	case SVC_I3C_MSTATUS_IBITYPE_MASTER_REQUEST:
	default:
		break;
	}

reenable_ibis:
	svc_i3c_master_enable_interrupts(master, SVC_I3C_MINT_SLVSTART);
	mutex_unlock(&master->lock);
}

static irqreturn_t svc_i3c_master_irq_handler(int irq, void *dev_id)
{
	struct svc_i3c_master *master = (struct svc_i3c_master *)dev_id;
	u32 active = readl(master->regs + SVC_I3C_MSTATUS);

	if (!SVC_I3C_MSTATUS_SLVSTART(active))
		return IRQ_NONE;

	/* Clear the interrupt status */
	writel(SVC_I3C_MINT_SLVSTART, master->regs + SVC_I3C_MSTATUS);

	svc_i3c_master_disable_interrupts(master);

	/* Handle the interrupt in a non atomic context */
	queue_work(master->base.wq, &master->ibi_work);

	return IRQ_HANDLED;
}

static int svc_i3c_master_bus_init(struct i3c_master_controller *m)
{
	struct svc_i3c_master *master = to_svc_i3c_master(m);
	struct i3c_bus *bus = i3c_master_get_bus(m);
	struct i3c_device_info info = {};
	unsigned long fclk_rate, fclk_period_ns;
	unsigned int high_period_ns, od_low_period_ns;
	u32 ppbaud, pplow, odhpp, odbaud, odstop, i2cbaud, reg;
	int ret;

	ret = pm_runtime_resume_and_get(master->dev);
	if (ret < 0) {
		dev_err(master->dev,
			"<%s> cannot resume i3c bus master, err: %d\n",
			__func__, ret);
		return ret;
	}

	/* Timings derivation */
	fclk_rate = clk_get_rate(master->fclk);
	if (!fclk_rate) {
		ret = -EINVAL;
		goto rpm_out;
	}

	fclk_period_ns = DIV_ROUND_UP(1000000000, fclk_rate);

	/*
	 * Using I3C Push-Pull mode, target is 12.5MHz/80ns period.
	 * Simplest configuration is using a 50% duty-cycle of 40ns.
	 */
	ppbaud = DIV_ROUND_UP(40, fclk_period_ns) - 1;
	pplow = 0;

	/*
	 * Using I3C Open-Drain mode, target is 4.17MHz/240ns with a
	 * duty-cycle tuned so that high levels are filetered out by
	 * the 50ns filter (target being 40ns).
	 */
	odhpp = 1;
	high_period_ns = (ppbaud + 1) * fclk_period_ns;
	odbaud = DIV_ROUND_UP(240 - high_period_ns, high_period_ns) - 1;
	od_low_period_ns = (odbaud + 1) * high_period_ns;

	switch (bus->mode) {
	case I3C_BUS_MODE_PURE:
		i2cbaud = 0;
		odstop = 0;
		break;
	case I3C_BUS_MODE_MIXED_FAST:
	case I3C_BUS_MODE_MIXED_LIMITED:
		/*
		 * Using I2C Fm+ mode, target is 1MHz/1000ns, the difference
		 * between the high and low period does not really matter.
		 */
		i2cbaud = DIV_ROUND_UP(1000, od_low_period_ns) - 2;
		odstop = 1;
		break;
	case I3C_BUS_MODE_MIXED_SLOW:
		/*
		 * Using I2C Fm mode, target is 0.4MHz/2500ns, with the same
		 * constraints as the FM+ mode.
		 */
		i2cbaud = DIV_ROUND_UP(2500, od_low_period_ns) - 2;
		odstop = 1;
		break;
	default:
		goto rpm_out;
	}

	reg = SVC_I3C_MCONFIG_MASTER_EN |
	      SVC_I3C_MCONFIG_DISTO(0) |
	      SVC_I3C_MCONFIG_HKEEP(0) |
	      SVC_I3C_MCONFIG_ODSTOP(odstop) |
	      SVC_I3C_MCONFIG_PPBAUD(ppbaud) |
	      SVC_I3C_MCONFIG_PPLOW(pplow) |
	      SVC_I3C_MCONFIG_ODBAUD(odbaud) |
	      SVC_I3C_MCONFIG_ODHPP(odhpp) |
	      SVC_I3C_MCONFIG_SKEW(0) |
	      SVC_I3C_MCONFIG_I2CBAUD(i2cbaud);
	writel(reg, master->regs + SVC_I3C_MCONFIG);

	/* Master core's registration */
	ret = i3c_master_get_free_addr(m, 0);
	if (ret < 0)
		goto rpm_out;

	info.dyn_addr = ret;

	writel(SVC_MDYNADDR_VALID | SVC_MDYNADDR_ADDR(info.dyn_addr),
	       master->regs + SVC_I3C_MDYNADDR);

	ret = i3c_master_set_info(&master->base, &info);
	if (ret)
		goto rpm_out;

rpm_out:
	pm_runtime_mark_last_busy(master->dev);
	pm_runtime_put_autosuspend(master->dev);

	return ret;
}

static void svc_i3c_master_bus_cleanup(struct i3c_master_controller *m)
{
	struct svc_i3c_master *master = to_svc_i3c_master(m);
	int ret;

	ret = pm_runtime_resume_and_get(master->dev);
	if (ret < 0) {
		dev_err(master->dev, "<%s> Cannot get runtime PM.\n", __func__);
		return;
	}

	svc_i3c_master_disable_interrupts(master);

	/* Disable master */
	writel(0, master->regs + SVC_I3C_MCONFIG);

	pm_runtime_mark_last_busy(master->dev);
	pm_runtime_put_autosuspend(master->dev);
}

static int svc_i3c_master_reserve_slot(struct svc_i3c_master *master)
{
	unsigned int slot;

	if (!(master->free_slots & GENMASK(SVC_I3C_MAX_DEVS - 1, 0)))
		return -ENOSPC;

	slot = ffs(master->free_slots) - 1;

	master->free_slots &= ~BIT(slot);

	return slot;
}

static void svc_i3c_master_release_slot(struct svc_i3c_master *master,
					unsigned int slot)
{
	master->free_slots |= BIT(slot);
}

static int svc_i3c_master_attach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct svc_i3c_master *master = to_svc_i3c_master(m);
	struct svc_i3c_i2c_dev_data *data;
	int slot;

	slot = svc_i3c_master_reserve_slot(master);
	if (slot < 0)
		return slot;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		svc_i3c_master_release_slot(master, slot);
		return -ENOMEM;
	}

	data->ibi = -1;
	data->index = slot;
	master->addrs[slot] = dev->info.dyn_addr ? dev->info.dyn_addr :
						   dev->info.static_addr;
	master->descs[slot] = dev;

	i3c_dev_set_master_data(dev, data);

	return 0;
}

static int svc_i3c_master_reattach_i3c_dev(struct i3c_dev_desc *dev,
					   u8 old_dyn_addr)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct svc_i3c_master *master = to_svc_i3c_master(m);
	struct svc_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);

	master->addrs[data->index] = dev->info.dyn_addr ? dev->info.dyn_addr :
							  dev->info.static_addr;

	return 0;
}

static void svc_i3c_master_detach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct svc_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct svc_i3c_master *master = to_svc_i3c_master(m);

	master->addrs[data->index] = 0;
	svc_i3c_master_release_slot(master, data->index);

	kfree(data);
}

static int svc_i3c_master_attach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct svc_i3c_master *master = to_svc_i3c_master(m);
	struct svc_i3c_i2c_dev_data *data;
	int slot;

	slot = svc_i3c_master_reserve_slot(master);
	if (slot < 0)
		return slot;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		svc_i3c_master_release_slot(master, slot);
		return -ENOMEM;
	}

	data->index = slot;
	master->addrs[slot] = dev->addr;

	i2c_dev_set_master_data(dev, data);

	return 0;
}

static void svc_i3c_master_detach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct svc_i3c_i2c_dev_data *data = i2c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct svc_i3c_master *master = to_svc_i3c_master(m);

	svc_i3c_master_release_slot(master, data->index);

	kfree(data);
}

static int svc_i3c_master_readb(struct svc_i3c_master *master, u8 *dst,
				unsigned int len)
{
	int ret, i;
	u32 reg;

	for (i = 0; i < len; i++) {
		ret = readl_poll_timeout_atomic(master->regs + SVC_I3C_MSTATUS,
						reg,
						SVC_I3C_MSTATUS_RXPEND(reg),
						0, 1000);
		if (ret)
			return ret;

		dst[i] = readl(master->regs + SVC_I3C_MRDATAB);
	}

	return 0;
}

static int svc_i3c_master_do_daa_locked(struct svc_i3c_master *master,
					u8 *addrs, unsigned int *count)
{
	u64 prov_id[SVC_I3C_MAX_DEVS] = {}, nacking_prov_id = 0;
	unsigned int dev_nb = 0, last_addr = 0;
	u32 reg;
	int ret, i;

	while (true) {
		/* SVC_I3C_MCTRL_REQUEST_PROC_DAA have two mode, ENTER DAA or PROCESS DAA.
		 *
		 * ENTER DAA:
		 *   1 will issue START, 7E, ENTDAA, and then emits 7E/R to process first target.
		 *   2 Stops just before the new Dynamic Address (DA) is to be emitted.
		 *
		 * PROCESS DAA:
		 *   1 The DA is written using MWDATAB or ADDR bits 6:0.
		 *   2 ProcessDAA is requested again to write the new address, and then starts the
		 *     next (START, 7E, ENTDAA)  unless marked to STOP; an MSTATUS indicating NACK
		 *     means DA was not accepted (e.g. parity error). If PROCESSDAA is NACKed on the
		 *     7E/R, which means no more Slaves need a DA, then a COMPLETE will be signaled
		 *     (along with DONE), and a STOP issued automatically.
		 */
		writel(SVC_I3C_MCTRL_REQUEST_PROC_DAA |
		       SVC_I3C_MCTRL_TYPE_I3C |
		       SVC_I3C_MCTRL_IBIRESP_NACK |
		       SVC_I3C_MCTRL_DIR(SVC_I3C_MCTRL_DIR_WRITE),
		       master->regs + SVC_I3C_MCTRL);

		/*
		 * Either one slave will send its ID, or the assignment process
		 * is done.
		 */
		ret = readl_poll_timeout_atomic(master->regs + SVC_I3C_MSTATUS,
						reg,
						SVC_I3C_MSTATUS_RXPEND(reg) |
						SVC_I3C_MSTATUS_MCTRLDONE(reg),
						1, 1000);
		if (ret)
			break;

		if (SVC_I3C_MSTATUS_RXPEND(reg)) {
			u8 data[6];

			/*
			 * We only care about the 48-bit provisioned ID yet to
			 * be sure a device does not nack an address twice.
			 * Otherwise, we would just need to flush the RX FIFO.
			 */
			ret = svc_i3c_master_readb(master, data, 6);
			if (ret)
				break;

			for (i = 0; i < 6; i++)
				prov_id[dev_nb] |= (u64)(data[i]) << (8 * (5 - i));

			/* We do not care about the BCR and DCR yet */
			ret = svc_i3c_master_readb(master, data, 2);
			if (ret)
				break;
		} else if (SVC_I3C_MSTATUS_MCTRLDONE(reg)) {
			if (SVC_I3C_MSTATUS_STATE_IDLE(reg) &&
			    SVC_I3C_MSTATUS_COMPLETE(reg)) {
				/*
				 * All devices received and acked they dynamic
				 * address, this is the natural end of the DAA
				 * procedure.
				 *
				 * Hardware will auto emit STOP at this case.
				 */
				*count = dev_nb;
				return 0;

			} else if (SVC_I3C_MSTATUS_NACKED(reg)) {
				/* No I3C devices attached */
				if (dev_nb == 0) {
					/*
					 * Hardware can't treat first NACK for ENTAA as normal
					 * COMPLETE. So need manual emit STOP.
					 */
					ret = 0;
					*count = 0;
					break;
				}

				/*
				 * A slave device nacked the address, this is
				 * allowed only once, DAA will be stopped and
				 * then resumed. The same device is supposed to
				 * answer again immediately and shall ack the
				 * address this time.
				 */
				if (prov_id[dev_nb] == nacking_prov_id) {
					ret = -EIO;
					break;
				}

				dev_nb--;
				nacking_prov_id = prov_id[dev_nb];
				svc_i3c_master_emit_stop(master);

				continue;
			} else {
				break;
			}
		}

		/* Wait for the slave to be ready to receive its address */
		ret = readl_poll_timeout_atomic(master->regs + SVC_I3C_MSTATUS,
						reg,
						SVC_I3C_MSTATUS_MCTRLDONE(reg) &&
						SVC_I3C_MSTATUS_STATE_DAA(reg) &&
						SVC_I3C_MSTATUS_BETWEEN(reg),
						0, 1000);
		if (ret)
			break;

		/* Give the slave device a suitable dynamic address */
		ret = i3c_master_get_free_addr(&master->base, last_addr + 1);
		if (ret < 0)
			break;

		addrs[dev_nb] = ret;
		dev_dbg(master->dev, "DAA: device %d assigned to 0x%02x\n",
			dev_nb, addrs[dev_nb]);

		writel(addrs[dev_nb], master->regs + SVC_I3C_MWDATAB);
		last_addr = addrs[dev_nb++];
	}

	/* Need manual issue STOP except for Complete condition */
	svc_i3c_master_emit_stop(master);
	return ret;
}

static int svc_i3c_update_ibirules(struct svc_i3c_master *master)
{
	struct i3c_dev_desc *dev;
	u32 reg_mbyte = 0, reg_nobyte = SVC_I3C_IBIRULES_NOBYTE;
	unsigned int mbyte_addr_ok = 0, mbyte_addr_ko = 0, nobyte_addr_ok = 0,
		nobyte_addr_ko = 0;
	bool list_mbyte = false, list_nobyte = false;

	/* Create the IBIRULES register for both cases */
	i3c_bus_for_each_i3cdev(&master->base.bus, dev) {
		if (I3C_BCR_DEVICE_ROLE(dev->info.bcr) == I3C_BCR_I3C_MASTER)
			continue;

		if (dev->info.bcr & I3C_BCR_IBI_PAYLOAD) {
			reg_mbyte |= SVC_I3C_IBIRULES_ADDR(mbyte_addr_ok,
							   dev->info.dyn_addr);

			/* IBI rules cannot be applied to devices with MSb=1 */
			if (dev->info.dyn_addr & BIT(7))
				mbyte_addr_ko++;
			else
				mbyte_addr_ok++;
		} else {
			reg_nobyte |= SVC_I3C_IBIRULES_ADDR(nobyte_addr_ok,
							    dev->info.dyn_addr);

			/* IBI rules cannot be applied to devices with MSb=1 */
			if (dev->info.dyn_addr & BIT(7))
				nobyte_addr_ko++;
			else
				nobyte_addr_ok++;
		}
	}

	/* Device list cannot be handled by hardware */
	if (!mbyte_addr_ko && mbyte_addr_ok <= SVC_I3C_IBIRULES_ADDRS)
		list_mbyte = true;

	if (!nobyte_addr_ko && nobyte_addr_ok <= SVC_I3C_IBIRULES_ADDRS)
		list_nobyte = true;

	/* No list can be properly handled, return an error */
	if (!list_mbyte && !list_nobyte)
		return -ERANGE;

	/* Pick the first list that can be handled by hardware, randomly */
	if (list_mbyte)
		writel(reg_mbyte, master->regs + SVC_I3C_IBIRULES);
	else
		writel(reg_nobyte, master->regs + SVC_I3C_IBIRULES);

	return 0;
}

static int svc_i3c_master_do_daa(struct i3c_master_controller *m)
{
	struct svc_i3c_master *master = to_svc_i3c_master(m);
	u8 addrs[SVC_I3C_MAX_DEVS];
	unsigned long flags;
	unsigned int dev_nb;
	int ret, i;

	ret = pm_runtime_resume_and_get(master->dev);
	if (ret < 0) {
		dev_err(master->dev, "<%s> Cannot get runtime PM.\n", __func__);
		return ret;
	}

	spin_lock_irqsave(&master->xferqueue.lock, flags);
	ret = svc_i3c_master_do_daa_locked(master, addrs, &dev_nb);
	spin_unlock_irqrestore(&master->xferqueue.lock, flags);

	svc_i3c_master_clear_merrwarn(master);
	if (ret)
		goto rpm_out;

	/* Register all devices who participated to the core */
	for (i = 0; i < dev_nb; i++) {
		ret = i3c_master_add_i3c_dev_locked(m, addrs[i]);
		if (ret)
			goto rpm_out;
	}

	/* Configure IBI auto-rules */
	ret = svc_i3c_update_ibirules(master);
	if (ret)
		dev_err(master->dev, "Cannot handle such a list of devices");

rpm_out:
	pm_runtime_mark_last_busy(master->dev);
	pm_runtime_put_autosuspend(master->dev);

	return ret;
}

static int svc_i3c_master_read(struct svc_i3c_master *master,
			       u8 *in, unsigned int len)
{
	int offset = 0, i;
	u32 mdctrl, mstatus;
	bool completed = false;
	unsigned int count;
	unsigned long start = jiffies;

	while (!completed) {
		mstatus = readl(master->regs + SVC_I3C_MSTATUS);
		if (SVC_I3C_MSTATUS_COMPLETE(mstatus) != 0)
			completed = true;

		if (time_after(jiffies, start + msecs_to_jiffies(1000))) {
			dev_dbg(master->dev, "I3C read timeout\n");
			return -ETIMEDOUT;
		}

		mdctrl = readl(master->regs + SVC_I3C_MDATACTRL);
		count = SVC_I3C_MDATACTRL_RXCOUNT(mdctrl);
		if (offset + count > len) {
			dev_err(master->dev, "I3C receive length too long!\n");
			return -EINVAL;
		}
		for (i = 0; i < count; i++)
			in[offset + i] = readl(master->regs + SVC_I3C_MRDATAB);

		offset += count;
	}

	return offset;
}

static int svc_i3c_master_write(struct svc_i3c_master *master,
				const u8 *out, unsigned int len)
{
	int offset = 0, ret;
	u32 mdctrl;

	while (offset < len) {
		ret = readl_poll_timeout(master->regs + SVC_I3C_MDATACTRL,
					 mdctrl,
					 !(mdctrl & SVC_I3C_MDATACTRL_TXFULL),
					 0, 1000);
		if (ret)
			return ret;

		/*
		 * The last byte to be sent over the bus must either have the
		 * "end" bit set or be written in MWDATABE.
		 */
		if (likely(offset < (len - 1)))
			writel(out[offset++], master->regs + SVC_I3C_MWDATAB);
		else
			writel(out[offset++], master->regs + SVC_I3C_MWDATABE);
	}

	return 0;
}

static int svc_i3c_master_xfer(struct svc_i3c_master *master,
			       bool rnw, unsigned int xfer_type, u8 addr,
			       u8 *in, const u8 *out, unsigned int xfer_len,
			       unsigned int *actual_len, bool continued)
{
	int retry = 2;
	u32 reg;
	int ret;

	/* clean SVC_I3C_MINT_IBIWON w1c bits */
	writel(SVC_I3C_MINT_IBIWON, master->regs + SVC_I3C_MSTATUS);


	while (retry--) {
		writel(SVC_I3C_MCTRL_REQUEST_START_ADDR |
		       xfer_type |
		       SVC_I3C_MCTRL_IBIRESP_NACK |
		       SVC_I3C_MCTRL_DIR(rnw) |
		       SVC_I3C_MCTRL_ADDR(addr) |
		       SVC_I3C_MCTRL_RDTERM(*actual_len),
		       master->regs + SVC_I3C_MCTRL);

		ret = readl_poll_timeout(master->regs + SVC_I3C_MSTATUS, reg,
				 SVC_I3C_MSTATUS_MCTRLDONE(reg), 0, 1000);
		if (ret)
			goto emit_stop;

		if (readl(master->regs + SVC_I3C_MERRWARN) & SVC_I3C_MERRWARN_NACK) {
			/*
			 * According to I3C Spec 1.1.1, 11-Jun-2021, section: 5.1.2.2.3.
			 * If the Controller chooses to start an I3C Message with an I3C Dynamic
			 * Address, then special provisions shall be made because that same I3C
			 * Target may be initiating an IBI or a Controller Role Request. So, one of
			 * three things may happen: (skip 1, 2)
			 *
			 * 3. The Addresses match and the RnW bits also match, and so neither
			 * Controller nor Target will ACK since both are expecting the other side to
			 * provide ACK. As a result, each side might think it had "won" arbitration,
			 * but neither side would continue, as each would subsequently see that the
			 * other did not provide ACK.
			 * ...
			 * For either value of RnW: Due to the NACK, the Controller shall defer the
			 * Private Write or Private Read, and should typically transmit the Target
			 * Address again after a Repeated START (i.e., the next one or any one prior
			 * to a STOP in the Frame). Since the Address Header following a Repeated
			 * START is not arbitrated, the Controller will always win (see Section
			 * 5.1.2.2.4).
			 */
			if (retry && addr != 0x7e) {
				writel(SVC_I3C_MERRWARN_NACK, master->regs + SVC_I3C_MERRWARN);
			} else {
				ret = -ENXIO;
				*actual_len = 0;
				goto emit_stop;
			}
		} else {
			break;
		}
	}

	/*
	 * According to I3C spec ver 1.1.1, 5.1.2.2.3 Consequence of Controller Starting a Frame
	 * with I3C Target Address.
	 *
	 * The I3C Controller normally should start a Frame, the Address may be arbitrated, and so
	 * the Controller shall monitor to see whether an In-Band Interrupt request, a Controller
	 * Role Request (i.e., Secondary Controller requests to become the Active Controller), or
	 * a Hot-Join Request has been made.
	 *
	 * If missed IBIWON check, the wrong data will be return. When IBIWON happen, return failure
	 * and yield the above events handler.
	 */
	if (SVC_I3C_MSTATUS_IBIWON(reg)) {
		ret = -EAGAIN;
		*actual_len = 0;
		goto emit_stop;
	}

	if (rnw)
		ret = svc_i3c_master_read(master, in, xfer_len);
	else
		ret = svc_i3c_master_write(master, out, xfer_len);
	if (ret < 0)
		goto emit_stop;

	if (rnw)
		*actual_len = ret;

	ret = readl_poll_timeout(master->regs + SVC_I3C_MSTATUS, reg,
				 SVC_I3C_MSTATUS_COMPLETE(reg), 0, 1000);
	if (ret)
		goto emit_stop;

	writel(SVC_I3C_MINT_COMPLETE, master->regs + SVC_I3C_MSTATUS);

	if (!continued) {
		svc_i3c_master_emit_stop(master);

		/* Wait idle if stop is sent. */
		readl_poll_timeout(master->regs + SVC_I3C_MSTATUS, reg,
				   SVC_I3C_MSTATUS_STATE_IDLE(reg), 0, 1000);
	}

	return 0;

emit_stop:
	svc_i3c_master_emit_stop(master);
	svc_i3c_master_clear_merrwarn(master);

	return ret;
}

static struct svc_i3c_xfer *
svc_i3c_master_alloc_xfer(struct svc_i3c_master *master, unsigned int ncmds)
{
	struct svc_i3c_xfer *xfer;

	xfer = kzalloc(struct_size(xfer, cmds, ncmds), GFP_KERNEL);
	if (!xfer)
		return NULL;

	INIT_LIST_HEAD(&xfer->node);
	xfer->ncmds = ncmds;
	xfer->ret = -ETIMEDOUT;

	return xfer;
}

static void svc_i3c_master_free_xfer(struct svc_i3c_xfer *xfer)
{
	kfree(xfer);
}

static void svc_i3c_master_dequeue_xfer_locked(struct svc_i3c_master *master,
					       struct svc_i3c_xfer *xfer)
{
	if (master->xferqueue.cur == xfer)
		master->xferqueue.cur = NULL;
	else
		list_del_init(&xfer->node);
}

static void svc_i3c_master_dequeue_xfer(struct svc_i3c_master *master,
					struct svc_i3c_xfer *xfer)
{
	unsigned long flags;

	spin_lock_irqsave(&master->xferqueue.lock, flags);
	svc_i3c_master_dequeue_xfer_locked(master, xfer);
	spin_unlock_irqrestore(&master->xferqueue.lock, flags);
}

static void svc_i3c_master_start_xfer_locked(struct svc_i3c_master *master)
{
	struct svc_i3c_xfer *xfer = master->xferqueue.cur;
	int ret, i;

	if (!xfer)
		return;

	svc_i3c_master_clear_merrwarn(master);
	svc_i3c_master_flush_fifo(master);

	for (i = 0; i < xfer->ncmds; i++) {
		struct svc_i3c_cmd *cmd = &xfer->cmds[i];

		ret = svc_i3c_master_xfer(master, cmd->rnw, xfer->type,
					  cmd->addr, cmd->in, cmd->out,
					  cmd->len, &cmd->actual_len,
					  cmd->continued);
		/* cmd->xfer is NULL if I2C or CCC transfer */
		if (cmd->xfer)
			cmd->xfer->actual_len = cmd->actual_len;

		if (ret)
			break;
	}

	xfer->ret = ret;
	complete(&xfer->comp);

	if (ret < 0)
		svc_i3c_master_dequeue_xfer_locked(master, xfer);

	xfer = list_first_entry_or_null(&master->xferqueue.list,
					struct svc_i3c_xfer,
					node);
	if (xfer)
		list_del_init(&xfer->node);

	master->xferqueue.cur = xfer;
	svc_i3c_master_start_xfer_locked(master);
}

static void svc_i3c_master_enqueue_xfer(struct svc_i3c_master *master,
					struct svc_i3c_xfer *xfer)
{
	unsigned long flags;
	int ret;

	ret = pm_runtime_resume_and_get(master->dev);
	if (ret < 0) {
		dev_err(master->dev, "<%s> Cannot get runtime PM.\n", __func__);
		return;
	}

	init_completion(&xfer->comp);
	spin_lock_irqsave(&master->xferqueue.lock, flags);
	if (master->xferqueue.cur) {
		list_add_tail(&xfer->node, &master->xferqueue.list);
	} else {
		master->xferqueue.cur = xfer;
		svc_i3c_master_start_xfer_locked(master);
	}
	spin_unlock_irqrestore(&master->xferqueue.lock, flags);

	pm_runtime_mark_last_busy(master->dev);
	pm_runtime_put_autosuspend(master->dev);
}

static bool
svc_i3c_master_supports_ccc_cmd(struct i3c_master_controller *master,
				const struct i3c_ccc_cmd *cmd)
{
	/* No software support for CCC commands targeting more than one slave */
	return (cmd->ndests == 1);
}

static int svc_i3c_master_send_bdcast_ccc_cmd(struct svc_i3c_master *master,
					      struct i3c_ccc_cmd *ccc)
{
	unsigned int xfer_len = ccc->dests[0].payload.len + 1;
	struct svc_i3c_xfer *xfer;
	struct svc_i3c_cmd *cmd;
	u8 *buf;
	int ret;

	xfer = svc_i3c_master_alloc_xfer(master, 1);
	if (!xfer)
		return -ENOMEM;

	buf = kmalloc(xfer_len, GFP_KERNEL);
	if (!buf) {
		svc_i3c_master_free_xfer(xfer);
		return -ENOMEM;
	}

	buf[0] = ccc->id;
	memcpy(&buf[1], ccc->dests[0].payload.data, ccc->dests[0].payload.len);

	xfer->type = SVC_I3C_MCTRL_TYPE_I3C;

	cmd = &xfer->cmds[0];
	cmd->addr = ccc->dests[0].addr;
	cmd->rnw = ccc->rnw;
	cmd->in = NULL;
	cmd->out = buf;
	cmd->len = xfer_len;
	cmd->actual_len = 0;
	cmd->continued = false;

	mutex_lock(&master->lock);
	svc_i3c_master_enqueue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, msecs_to_jiffies(1000)))
		svc_i3c_master_dequeue_xfer(master, xfer);
	mutex_unlock(&master->lock);

	ret = xfer->ret;
	kfree(buf);
	svc_i3c_master_free_xfer(xfer);

	return ret;
}

static int svc_i3c_master_send_direct_ccc_cmd(struct svc_i3c_master *master,
					      struct i3c_ccc_cmd *ccc)
{
	unsigned int xfer_len = ccc->dests[0].payload.len;
	unsigned int actual_len = ccc->rnw ? xfer_len : 0;
	struct svc_i3c_xfer *xfer;
	struct svc_i3c_cmd *cmd;
	int ret;

	xfer = svc_i3c_master_alloc_xfer(master, 2);
	if (!xfer)
		return -ENOMEM;

	xfer->type = SVC_I3C_MCTRL_TYPE_I3C;

	/* Broadcasted message */
	cmd = &xfer->cmds[0];
	cmd->addr = I3C_BROADCAST_ADDR;
	cmd->rnw = 0;
	cmd->in = NULL;
	cmd->out = &ccc->id;
	cmd->len = 1;
	cmd->actual_len = 0;
	cmd->continued = true;

	/* Directed message */
	cmd = &xfer->cmds[1];
	cmd->addr = ccc->dests[0].addr;
	cmd->rnw = ccc->rnw;
	cmd->in = ccc->rnw ? ccc->dests[0].payload.data : NULL;
	cmd->out = ccc->rnw ? NULL : ccc->dests[0].payload.data;
	cmd->len = xfer_len;
	cmd->actual_len = actual_len;
	cmd->continued = false;

	mutex_lock(&master->lock);
	svc_i3c_master_enqueue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, msecs_to_jiffies(1000)))
		svc_i3c_master_dequeue_xfer(master, xfer);
	mutex_unlock(&master->lock);

	if (cmd->actual_len != xfer_len)
		ccc->dests[0].payload.len = cmd->actual_len;

	ret = xfer->ret;
	svc_i3c_master_free_xfer(xfer);

	return ret;
}

static int svc_i3c_master_send_ccc_cmd(struct i3c_master_controller *m,
				       struct i3c_ccc_cmd *cmd)
{
	struct svc_i3c_master *master = to_svc_i3c_master(m);
	bool broadcast = cmd->id < 0x80;
	int ret;

	if (broadcast)
		ret = svc_i3c_master_send_bdcast_ccc_cmd(master, cmd);
	else
		ret = svc_i3c_master_send_direct_ccc_cmd(master, cmd);

	if (ret)
		cmd->err = I3C_ERROR_M2;

	return ret;
}

static int svc_i3c_master_priv_xfers(struct i3c_dev_desc *dev,
				     struct i3c_priv_xfer *xfers,
				     int nxfers)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct svc_i3c_master *master = to_svc_i3c_master(m);
	struct svc_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct svc_i3c_xfer *xfer;
	int ret, i;

	xfer = svc_i3c_master_alloc_xfer(master, nxfers);
	if (!xfer)
		return -ENOMEM;

	xfer->type = SVC_I3C_MCTRL_TYPE_I3C;

	for (i = 0; i < nxfers; i++) {
		struct svc_i3c_cmd *cmd = &xfer->cmds[i];

		cmd->xfer = &xfers[i];
		cmd->addr = master->addrs[data->index];
		cmd->rnw = xfers[i].rnw;
		cmd->in = xfers[i].rnw ? xfers[i].data.in : NULL;
		cmd->out = xfers[i].rnw ? NULL : xfers[i].data.out;
		cmd->len = xfers[i].len;
		cmd->actual_len = xfers[i].rnw ? xfers[i].len : 0;
		cmd->continued = (i + 1) < nxfers;
	}

	mutex_lock(&master->lock);
	svc_i3c_master_enqueue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, msecs_to_jiffies(1000)))
		svc_i3c_master_dequeue_xfer(master, xfer);
	mutex_unlock(&master->lock);

	ret = xfer->ret;
	svc_i3c_master_free_xfer(xfer);

	return ret;
}

static int svc_i3c_master_i2c_xfers(struct i2c_dev_desc *dev,
				    const struct i2c_msg *xfers,
				    int nxfers)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct svc_i3c_master *master = to_svc_i3c_master(m);
	struct svc_i3c_i2c_dev_data *data = i2c_dev_get_master_data(dev);
	struct svc_i3c_xfer *xfer;
	int ret, i;

	xfer = svc_i3c_master_alloc_xfer(master, nxfers);
	if (!xfer)
		return -ENOMEM;

	xfer->type = SVC_I3C_MCTRL_TYPE_I2C;

	for (i = 0; i < nxfers; i++) {
		struct svc_i3c_cmd *cmd = &xfer->cmds[i];

		cmd->addr = master->addrs[data->index];
		cmd->rnw = xfers[i].flags & I2C_M_RD;
		cmd->in = cmd->rnw ? xfers[i].buf : NULL;
		cmd->out = cmd->rnw ? NULL : xfers[i].buf;
		cmd->len = xfers[i].len;
		cmd->actual_len = cmd->rnw ? xfers[i].len : 0;
		cmd->continued = (i + 1 < nxfers);
	}

	mutex_lock(&master->lock);
	svc_i3c_master_enqueue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, msecs_to_jiffies(1000)))
		svc_i3c_master_dequeue_xfer(master, xfer);
	mutex_unlock(&master->lock);

	ret = xfer->ret;
	svc_i3c_master_free_xfer(xfer);

	return ret;
}

static int svc_i3c_master_request_ibi(struct i3c_dev_desc *dev,
				      const struct i3c_ibi_setup *req)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct svc_i3c_master *master = to_svc_i3c_master(m);
	struct svc_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	unsigned long flags;
	unsigned int i;

	if (dev->ibi->max_payload_len > SVC_I3C_FIFO_SIZE) {
		dev_err(master->dev, "IBI max payload %d should be < %d\n",
			dev->ibi->max_payload_len, SVC_I3C_FIFO_SIZE);
		return -ERANGE;
	}

	data->ibi_pool = i3c_generic_ibi_alloc_pool(dev, req);
	if (IS_ERR(data->ibi_pool))
		return PTR_ERR(data->ibi_pool);

	spin_lock_irqsave(&master->ibi.lock, flags);
	for (i = 0; i < master->ibi.num_slots; i++) {
		if (!master->ibi.slots[i]) {
			data->ibi = i;
			master->ibi.slots[i] = dev;
			break;
		}
	}
	spin_unlock_irqrestore(&master->ibi.lock, flags);

	if (i < master->ibi.num_slots)
		return 0;

	i3c_generic_ibi_free_pool(data->ibi_pool);
	data->ibi_pool = NULL;

	return -ENOSPC;
}

static void svc_i3c_master_free_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct svc_i3c_master *master = to_svc_i3c_master(m);
	struct svc_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	unsigned long flags;

	spin_lock_irqsave(&master->ibi.lock, flags);
	master->ibi.slots[data->ibi] = NULL;
	data->ibi = -1;
	spin_unlock_irqrestore(&master->ibi.lock, flags);

	i3c_generic_ibi_free_pool(data->ibi_pool);
}

static int svc_i3c_master_enable_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct svc_i3c_master *master = to_svc_i3c_master(m);
	int ret;

	ret = pm_runtime_resume_and_get(master->dev);
	if (ret < 0) {
		dev_err(master->dev, "<%s> Cannot get runtime PM.\n", __func__);
		return ret;
	}

	master->enabled_events |= SVC_I3C_EVENT_IBI;
	svc_i3c_master_enable_interrupts(master, SVC_I3C_MINT_SLVSTART);

	return i3c_master_enec_locked(m, dev->info.dyn_addr, I3C_CCC_EVENT_SIR);
}

static int svc_i3c_master_disable_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct svc_i3c_master *master = to_svc_i3c_master(m);
	int ret;

	master->enabled_events &= ~SVC_I3C_EVENT_IBI;
	if (!master->enabled_events)
		svc_i3c_master_disable_interrupts(master);

	ret = i3c_master_disec_locked(m, dev->info.dyn_addr, I3C_CCC_EVENT_SIR);

	pm_runtime_mark_last_busy(master->dev);
	pm_runtime_put_autosuspend(master->dev);

	return ret;
}

static int svc_i3c_master_enable_hotjoin(struct i3c_master_controller *m)
{
	struct svc_i3c_master *master = to_svc_i3c_master(m);
	int ret;

	ret = pm_runtime_resume_and_get(master->dev);
	if (ret < 0) {
		dev_err(master->dev, "<%s> Cannot get runtime PM.\n", __func__);
		return ret;
	}

	master->enabled_events |= SVC_I3C_EVENT_HOTJOIN;

	svc_i3c_master_enable_interrupts(master, SVC_I3C_MINT_SLVSTART);

	return 0;
}

static int svc_i3c_master_disable_hotjoin(struct i3c_master_controller *m)
{
	struct svc_i3c_master *master = to_svc_i3c_master(m);

	master->enabled_events &= ~SVC_I3C_EVENT_HOTJOIN;

	if (!master->enabled_events)
		svc_i3c_master_disable_interrupts(master);

	pm_runtime_mark_last_busy(master->dev);
	pm_runtime_put_autosuspend(master->dev);

	return 0;
}

static void svc_i3c_master_recycle_ibi_slot(struct i3c_dev_desc *dev,
					    struct i3c_ibi_slot *slot)
{
	struct svc_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);

	i3c_generic_ibi_recycle_slot(data->ibi_pool, slot);
}

static const struct i3c_master_controller_ops svc_i3c_master_ops = {
	.bus_init = svc_i3c_master_bus_init,
	.bus_cleanup = svc_i3c_master_bus_cleanup,
	.attach_i3c_dev = svc_i3c_master_attach_i3c_dev,
	.detach_i3c_dev = svc_i3c_master_detach_i3c_dev,
	.reattach_i3c_dev = svc_i3c_master_reattach_i3c_dev,
	.attach_i2c_dev = svc_i3c_master_attach_i2c_dev,
	.detach_i2c_dev = svc_i3c_master_detach_i2c_dev,
	.do_daa = svc_i3c_master_do_daa,
	.supports_ccc_cmd = svc_i3c_master_supports_ccc_cmd,
	.send_ccc_cmd = svc_i3c_master_send_ccc_cmd,
	.priv_xfers = svc_i3c_master_priv_xfers,
	.i2c_xfers = svc_i3c_master_i2c_xfers,
	.request_ibi = svc_i3c_master_request_ibi,
	.free_ibi = svc_i3c_master_free_ibi,
	.recycle_ibi_slot = svc_i3c_master_recycle_ibi_slot,
	.enable_ibi = svc_i3c_master_enable_ibi,
	.disable_ibi = svc_i3c_master_disable_ibi,
	.enable_hotjoin = svc_i3c_master_enable_hotjoin,
	.disable_hotjoin = svc_i3c_master_disable_hotjoin,
};

static int svc_i3c_master_prepare_clks(struct svc_i3c_master *master)
{
	int ret = 0;

	ret = clk_prepare_enable(master->pclk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(master->fclk);
	if (ret) {
		clk_disable_unprepare(master->pclk);
		return ret;
	}

	ret = clk_prepare_enable(master->sclk);
	if (ret) {
		clk_disable_unprepare(master->pclk);
		clk_disable_unprepare(master->fclk);
		return ret;
	}

	return 0;
}

static void svc_i3c_master_unprepare_clks(struct svc_i3c_master *master)
{
	clk_disable_unprepare(master->pclk);
	clk_disable_unprepare(master->fclk);
	clk_disable_unprepare(master->sclk);
}

static int svc_i3c_master_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct svc_i3c_master *master;
	int ret;

	master = devm_kzalloc(dev, sizeof(*master), GFP_KERNEL);
	if (!master)
		return -ENOMEM;

	master->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(master->regs))
		return PTR_ERR(master->regs);

	master->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(master->pclk))
		return PTR_ERR(master->pclk);

	master->fclk = devm_clk_get(dev, "fast_clk");
	if (IS_ERR(master->fclk))
		return PTR_ERR(master->fclk);

	master->sclk = devm_clk_get(dev, "slow_clk");
	if (IS_ERR(master->sclk))
		return PTR_ERR(master->sclk);

	master->irq = platform_get_irq(pdev, 0);
	if (master->irq < 0)
		return master->irq;

	master->dev = dev;

	ret = svc_i3c_master_prepare_clks(master);
	if (ret)
		return ret;

	INIT_WORK(&master->hj_work, svc_i3c_master_hj_work);
	INIT_WORK(&master->ibi_work, svc_i3c_master_ibi_work);
	mutex_init(&master->lock);

	ret = devm_request_irq(dev, master->irq, svc_i3c_master_irq_handler,
			       IRQF_NO_SUSPEND, "svc-i3c-irq", master);
	if (ret)
		goto err_disable_clks;

	master->free_slots = GENMASK(SVC_I3C_MAX_DEVS - 1, 0);

	spin_lock_init(&master->xferqueue.lock);
	INIT_LIST_HEAD(&master->xferqueue.list);

	spin_lock_init(&master->ibi.lock);
	master->ibi.num_slots = SVC_I3C_MAX_DEVS;
	master->ibi.slots = devm_kcalloc(&pdev->dev, master->ibi.num_slots,
					 sizeof(*master->ibi.slots),
					 GFP_KERNEL);
	if (!master->ibi.slots) {
		ret = -ENOMEM;
		goto err_disable_clks;
	}

	platform_set_drvdata(pdev, master);

	pm_runtime_set_autosuspend_delay(&pdev->dev, SVC_I3C_PM_TIMEOUT_MS);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	svc_i3c_master_reset(master);

	/* Register the master */
	ret = i3c_master_register(&master->base, &pdev->dev,
				  &svc_i3c_master_ops, false);
	if (ret)
		goto rpm_disable;

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	return 0;

rpm_disable:
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

err_disable_clks:
	svc_i3c_master_unprepare_clks(master);

	return ret;
}

static void svc_i3c_master_remove(struct platform_device *pdev)
{
	struct svc_i3c_master *master = platform_get_drvdata(pdev);

	i3c_master_unregister(&master->base);

	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
}

static void svc_i3c_save_regs(struct svc_i3c_master *master)
{
	master->saved_regs.mconfig = readl(master->regs + SVC_I3C_MCONFIG);
	master->saved_regs.mdynaddr = readl(master->regs + SVC_I3C_MDYNADDR);
}

static void svc_i3c_restore_regs(struct svc_i3c_master *master)
{
	if (readl(master->regs + SVC_I3C_MDYNADDR) !=
	    master->saved_regs.mdynaddr) {
		writel(master->saved_regs.mconfig,
		       master->regs + SVC_I3C_MCONFIG);
		writel(master->saved_regs.mdynaddr,
		       master->regs + SVC_I3C_MDYNADDR);
	}
}

static int __maybe_unused svc_i3c_runtime_suspend(struct device *dev)
{
	struct svc_i3c_master *master = dev_get_drvdata(dev);

	svc_i3c_save_regs(master);
	svc_i3c_master_unprepare_clks(master);
	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int __maybe_unused svc_i3c_runtime_resume(struct device *dev)
{
	struct svc_i3c_master *master = dev_get_drvdata(dev);

	pinctrl_pm_select_default_state(dev);
	svc_i3c_master_prepare_clks(master);

	svc_i3c_restore_regs(master);

	return 0;
}

static const struct dev_pm_ops svc_i3c_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				      pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(svc_i3c_runtime_suspend,
			   svc_i3c_runtime_resume, NULL)
};

static const struct of_device_id svc_i3c_master_of_match_tbl[] = {
	{ .compatible = "silvaco,i3c-master-v1"},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, svc_i3c_master_of_match_tbl);

static struct platform_driver svc_i3c_master = {
	.probe = svc_i3c_master_probe,
	.remove_new = svc_i3c_master_remove,
	.driver = {
		.name = "silvaco-i3c-master",
		.of_match_table = svc_i3c_master_of_match_tbl,
		.pm = &svc_i3c_pm_ops,
	},
};
module_platform_driver(svc_i3c_master);

MODULE_AUTHOR("Conor Culhane <conor.culhane@silvaco.com>");
MODULE_AUTHOR("Miquel Raynal <miquel.raynal@bootlin.com>");
MODULE_DESCRIPTION("Silvaco dual-role I3C master driver");
MODULE_LICENSE("GPL v2");
