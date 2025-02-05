// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2024 Linaro Ltd.
 */

#include <linux/bits.h>
#include <linux/bug.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include "gsi.h"
#include "gsi_private.h"
#include "gsi_reg.h"
#include "gsi_trans.h"
#include "ipa_data.h"
#include "ipa_gsi.h"
#include "ipa_version.h"
#include "reg.h"

/**
 * DOC: The IPA Generic Software Interface
 *
 * The generic software interface (GSI) is an integral component of the IPA,
 * providing a well-defined communication layer between the AP subsystem
 * and the IPA core.  The modem uses the GSI layer as well.
 *
 *	--------	     ---------
 *	|      |	     |	     |
 *	|  AP  +<---.	.----+ Modem |
 *	|      +--. |	| .->+	     |
 *	|      |  | |	| |  |	     |
 *	--------  | |	| |  ---------
 *		  v |	v |
 *		--+-+---+-+--
 *		|    GSI    |
 *		|-----------|
 *		|	    |
 *		|    IPA    |
 *		|	    |
 *		-------------
 *
 * In the above diagram, the AP and Modem represent "execution environments"
 * (EEs), which are independent operating environments that use the IPA for
 * data transfer.
 *
 * Each EE uses a set of unidirectional GSI "channels," which allow transfer
 * of data to or from the IPA.  A channel is implemented as a ring buffer,
 * with a DRAM-resident array of "transfer elements" (TREs) available to
 * describe transfers to or from other EEs through the IPA.  A transfer
 * element can also contain an immediate command, requesting the IPA perform
 * actions other than data transfer.
 *
 * Each TRE refers to a block of data--also located in DRAM.  After writing
 * one or more TREs to a channel, the writer (either the IPA or an EE) writes
 * a doorbell register to inform the receiving side how many elements have
 * been written.
 *
 * Each channel has a GSI "event ring" associated with it.  An event ring
 * is implemented very much like a channel ring, but is always directed from
 * the IPA to an EE.  The IPA notifies an EE (such as the AP) about channel
 * events by adding an entry to the event ring associated with the channel.
 * The GSI then writes its doorbell for the event ring, causing the target
 * EE to be interrupted.  Each entry in an event ring contains a pointer
 * to the channel TRE whose completion the event represents.
 *
 * Each TRE in a channel ring has a set of flags.  One flag indicates whether
 * the completion of the transfer operation generates an entry (and possibly
 * an interrupt) in the channel's event ring.  Other flags allow transfer
 * elements to be chained together, forming a single logical transaction.
 * TRE flags are used to control whether and when interrupts are generated
 * to signal completion of channel transfers.
 *
 * Elements in channel and event rings are completed (or consumed) strictly
 * in order.  Completion of one entry implies the completion of all preceding
 * entries.  A single completion interrupt can therefore communicate the
 * completion of many transfers.
 *
 * Note that all GSI registers are little-endian, which is the assumed
 * endianness of I/O space accesses.  The accessor functions perform byte
 * swapping if needed (i.e., for a big endian CPU).
 */

/* Delay period for interrupt moderation (in 32KHz IPA internal timer ticks) */
#define GSI_EVT_RING_INT_MODT		(32 * 1) /* 1ms under 32KHz clock */

#define GSI_CMD_TIMEOUT			50	/* milliseconds */

#define GSI_CHANNEL_STOP_RETRIES	10
#define GSI_CHANNEL_MODEM_HALT_RETRIES	10
#define GSI_CHANNEL_MODEM_FLOW_RETRIES	5	/* disable flow control only */

#define GSI_MHI_EVENT_ID_START		10	/* 1st reserved event id */
#define GSI_MHI_EVENT_ID_END		16	/* Last reserved event id */

#define GSI_ISR_MAX_ITER		50	/* Detect interrupt storms */

/* An entry in an event ring */
struct gsi_event {
	__le64 xfer_ptr;
	__le16 len;
	u8 reserved1;
	u8 code;
	__le16 reserved2;
	u8 type;
	u8 chid;
};

/** gsi_channel_scratch_gpi - GPI protocol scratch register
 * @max_outstanding_tre:
 *	Defines the maximum number of TREs allowed in a single transaction
 *	on a channel (in bytes).  This determines the amount of prefetch
 *	performed by the hardware.  We configure this to equal the size of
 *	the TLV FIFO for the channel.
 * @outstanding_threshold:
 *	Defines the threshold (in bytes) determining when the sequencer
 *	should update the channel doorbell.  We configure this to equal
 *	the size of two TREs.
 */
struct gsi_channel_scratch_gpi {
	u64 reserved1;
	u16 reserved2;
	u16 max_outstanding_tre;
	u16 reserved3;
	u16 outstanding_threshold;
};

/** gsi_channel_scratch - channel scratch configuration area
 *
 * The exact interpretation of this register is protocol-specific.
 * We only use GPI channels; see struct gsi_channel_scratch_gpi, above.
 */
union gsi_channel_scratch {
	struct gsi_channel_scratch_gpi gpi;
	struct {
		u32 word1;
		u32 word2;
		u32 word3;
		u32 word4;
	} data;
};

/* Check things that can be validated at build time. */
static void gsi_validate_build(void)
{
	/* This is used as a divisor */
	BUILD_BUG_ON(!GSI_RING_ELEMENT_SIZE);

	/* Code assumes the size of channel and event ring element are
	 * the same (and fixed).  Make sure the size of an event ring
	 * element is what's expected.
	 */
	BUILD_BUG_ON(sizeof(struct gsi_event) != GSI_RING_ELEMENT_SIZE);

	/* Hardware requires a 2^n ring size.  We ensure the number of
	 * elements in an event ring is a power of 2 elsewhere; this
	 * ensure the elements themselves meet the requirement.
	 */
	BUILD_BUG_ON(!is_power_of_2(GSI_RING_ELEMENT_SIZE));
}

/* Return the channel id associated with a given channel */
static u32 gsi_channel_id(struct gsi_channel *channel)
{
	return channel - &channel->gsi->channel[0];
}

/* An initialized channel has a non-null GSI pointer */
static bool gsi_channel_initialized(struct gsi_channel *channel)
{
	return !!channel->gsi;
}

/* Encode the channel protocol for the CH_C_CNTXT_0 register */
static u32 ch_c_cntxt_0_type_encode(enum ipa_version version,
				    const struct reg *reg,
				    enum gsi_channel_type type)
{
	u32 val;

	val = reg_encode(reg, CHTYPE_PROTOCOL, type);
	if (version < IPA_VERSION_4_5 || version >= IPA_VERSION_5_0)
		return val;

	type >>= hweight32(reg_fmask(reg, CHTYPE_PROTOCOL));

	return val | reg_encode(reg, CHTYPE_PROTOCOL_MSB, type);
}

/* Update the GSI IRQ type register with the cached value */
static void gsi_irq_type_update(struct gsi *gsi, u32 val)
{
	const struct reg *reg = gsi_reg(gsi, CNTXT_TYPE_IRQ_MSK);

	gsi->type_enabled_bitmap = val;
	iowrite32(val, gsi->virt + reg_offset(reg));
}

static void gsi_irq_type_enable(struct gsi *gsi, enum gsi_irq_type_id type_id)
{
	gsi_irq_type_update(gsi, gsi->type_enabled_bitmap | type_id);
}

static void gsi_irq_type_disable(struct gsi *gsi, enum gsi_irq_type_id type_id)
{
	gsi_irq_type_update(gsi, gsi->type_enabled_bitmap & ~type_id);
}

/* Event ring commands are performed one at a time.  Their completion
 * is signaled by the event ring control GSI interrupt type, which is
 * only enabled when we issue an event ring command.  Only the event
 * ring being operated on has this interrupt enabled.
 */
static void gsi_irq_ev_ctrl_enable(struct gsi *gsi, u32 evt_ring_id)
{
	u32 val = BIT(evt_ring_id);
	const struct reg *reg;

	/* There's a small chance that a previous command completed
	 * after the interrupt was disabled, so make sure we have no
	 * pending interrupts before we enable them.
	 */
	reg = gsi_reg(gsi, CNTXT_SRC_EV_CH_IRQ_CLR);
	iowrite32(~0, gsi->virt + reg_offset(reg));

	reg = gsi_reg(gsi, CNTXT_SRC_EV_CH_IRQ_MSK);
	iowrite32(val, gsi->virt + reg_offset(reg));
	gsi_irq_type_enable(gsi, GSI_EV_CTRL);
}

/* Disable event ring control interrupts */
static void gsi_irq_ev_ctrl_disable(struct gsi *gsi)
{
	const struct reg *reg;

	gsi_irq_type_disable(gsi, GSI_EV_CTRL);

	reg = gsi_reg(gsi, CNTXT_SRC_EV_CH_IRQ_MSK);
	iowrite32(0, gsi->virt + reg_offset(reg));
}

/* Channel commands are performed one at a time.  Their completion is
 * signaled by the channel control GSI interrupt type, which is only
 * enabled when we issue a channel command.  Only the channel being
 * operated on has this interrupt enabled.
 */
static void gsi_irq_ch_ctrl_enable(struct gsi *gsi, u32 channel_id)
{
	u32 val = BIT(channel_id);
	const struct reg *reg;

	/* There's a small chance that a previous command completed
	 * after the interrupt was disabled, so make sure we have no
	 * pending interrupts before we enable them.
	 */
	reg = gsi_reg(gsi, CNTXT_SRC_CH_IRQ_CLR);
	iowrite32(~0, gsi->virt + reg_offset(reg));

	reg = gsi_reg(gsi, CNTXT_SRC_CH_IRQ_MSK);
	iowrite32(val, gsi->virt + reg_offset(reg));

	gsi_irq_type_enable(gsi, GSI_CH_CTRL);
}

/* Disable channel control interrupts */
static void gsi_irq_ch_ctrl_disable(struct gsi *gsi)
{
	const struct reg *reg;

	gsi_irq_type_disable(gsi, GSI_CH_CTRL);

	reg = gsi_reg(gsi, CNTXT_SRC_CH_IRQ_MSK);
	iowrite32(0, gsi->virt + reg_offset(reg));
}

static void gsi_irq_ieob_enable_one(struct gsi *gsi, u32 evt_ring_id)
{
	bool enable_ieob = !gsi->ieob_enabled_bitmap;
	const struct reg *reg;
	u32 val;

	gsi->ieob_enabled_bitmap |= BIT(evt_ring_id);

	reg = gsi_reg(gsi, CNTXT_SRC_IEOB_IRQ_MSK);
	val = gsi->ieob_enabled_bitmap;
	iowrite32(val, gsi->virt + reg_offset(reg));

	/* Enable the interrupt type if this is the first channel enabled */
	if (enable_ieob)
		gsi_irq_type_enable(gsi, GSI_IEOB);
}

static void gsi_irq_ieob_disable(struct gsi *gsi, u32 event_mask)
{
	const struct reg *reg;
	u32 val;

	gsi->ieob_enabled_bitmap &= ~event_mask;

	/* Disable the interrupt type if this was the last enabled channel */
	if (!gsi->ieob_enabled_bitmap)
		gsi_irq_type_disable(gsi, GSI_IEOB);

	reg = gsi_reg(gsi, CNTXT_SRC_IEOB_IRQ_MSK);
	val = gsi->ieob_enabled_bitmap;
	iowrite32(val, gsi->virt + reg_offset(reg));
}

static void gsi_irq_ieob_disable_one(struct gsi *gsi, u32 evt_ring_id)
{
	gsi_irq_ieob_disable(gsi, BIT(evt_ring_id));
}

/* Enable all GSI_interrupt types */
static void gsi_irq_enable(struct gsi *gsi)
{
	const struct reg *reg;
	u32 val;

	/* Global interrupts include hardware error reports.  Enable
	 * that so we can at least report the error should it occur.
	 */
	reg = gsi_reg(gsi, CNTXT_GLOB_IRQ_EN);
	iowrite32(ERROR_INT, gsi->virt + reg_offset(reg));

	gsi_irq_type_update(gsi, gsi->type_enabled_bitmap | GSI_GLOB_EE);

	/* General GSI interrupts are reported to all EEs; if they occur
	 * they are unrecoverable (without reset).  A breakpoint interrupt
	 * also exists, but we don't support that.  We want to be notified
	 * of errors so we can report them, even if they can't be handled.
	 */
	reg = gsi_reg(gsi, CNTXT_GSI_IRQ_EN);
	val = BUS_ERROR;
	val |= CMD_FIFO_OVRFLOW;
	val |= MCS_STACK_OVRFLOW;
	iowrite32(val, gsi->virt + reg_offset(reg));

	gsi_irq_type_update(gsi, gsi->type_enabled_bitmap | GSI_GENERAL);
}

/* Disable all GSI interrupt types */
static void gsi_irq_disable(struct gsi *gsi)
{
	const struct reg *reg;

	gsi_irq_type_update(gsi, 0);

	/* Clear the type-specific interrupt masks set by gsi_irq_enable() */
	reg = gsi_reg(gsi, CNTXT_GSI_IRQ_EN);
	iowrite32(0, gsi->virt + reg_offset(reg));

	reg = gsi_reg(gsi, CNTXT_GLOB_IRQ_EN);
	iowrite32(0, gsi->virt + reg_offset(reg));
}

/* Return the virtual address associated with a ring index */
void *gsi_ring_virt(struct gsi_ring *ring, u32 index)
{
	/* Note: index *must* be used modulo the ring count here */
	return ring->virt + (index % ring->count) * GSI_RING_ELEMENT_SIZE;
}

/* Return the 32-bit DMA address associated with a ring index */
static u32 gsi_ring_addr(struct gsi_ring *ring, u32 index)
{
	return lower_32_bits(ring->addr) + index * GSI_RING_ELEMENT_SIZE;
}

/* Return the ring index of a 32-bit ring offset */
static u32 gsi_ring_index(struct gsi_ring *ring, u32 offset)
{
	return (offset - gsi_ring_addr(ring, 0)) / GSI_RING_ELEMENT_SIZE;
}

/* Issue a GSI command by writing a value to a register, then wait for
 * completion to be signaled.  Returns true if the command completes
 * or false if it times out.
 */
static bool gsi_command(struct gsi *gsi, u32 reg, u32 val)
{
	unsigned long timeout = msecs_to_jiffies(GSI_CMD_TIMEOUT);
	struct completion *completion = &gsi->completion;

	reinit_completion(completion);

	iowrite32(val, gsi->virt + reg);

	return !!wait_for_completion_timeout(completion, timeout);
}

/* Return the hardware's notion of the current state of an event ring */
static enum gsi_evt_ring_state
gsi_evt_ring_state(struct gsi *gsi, u32 evt_ring_id)
{
	const struct reg *reg = gsi_reg(gsi, EV_CH_E_CNTXT_0);
	u32 val;

	val = ioread32(gsi->virt + reg_n_offset(reg, evt_ring_id));

	return reg_decode(reg, EV_CHSTATE, val);
}

/* Issue an event ring command and wait for it to complete */
static void gsi_evt_ring_command(struct gsi *gsi, u32 evt_ring_id,
				 enum gsi_evt_cmd_opcode opcode)
{
	struct device *dev = gsi->dev;
	const struct reg *reg;
	bool timeout;
	u32 val;

	/* Enable the completion interrupt for the command */
	gsi_irq_ev_ctrl_enable(gsi, evt_ring_id);

	reg = gsi_reg(gsi, EV_CH_CMD);
	val = reg_encode(reg, EV_CHID, evt_ring_id);
	val |= reg_encode(reg, EV_OPCODE, opcode);

	timeout = !gsi_command(gsi, reg_offset(reg), val);

	gsi_irq_ev_ctrl_disable(gsi);

	if (!timeout)
		return;

	dev_err(dev, "GSI command %u for event ring %u timed out, state %u\n",
		opcode, evt_ring_id, gsi_evt_ring_state(gsi, evt_ring_id));
}

/* Allocate an event ring in NOT_ALLOCATED state */
static int gsi_evt_ring_alloc_command(struct gsi *gsi, u32 evt_ring_id)
{
	enum gsi_evt_ring_state state;

	/* Get initial event ring state */
	state = gsi_evt_ring_state(gsi, evt_ring_id);
	if (state != GSI_EVT_RING_STATE_NOT_ALLOCATED) {
		dev_err(gsi->dev, "event ring %u bad state %u before alloc\n",
			evt_ring_id, state);
		return -EINVAL;
	}

	gsi_evt_ring_command(gsi, evt_ring_id, GSI_EVT_ALLOCATE);

	/* If successful the event ring state will have changed */
	state = gsi_evt_ring_state(gsi, evt_ring_id);
	if (state == GSI_EVT_RING_STATE_ALLOCATED)
		return 0;

	dev_err(gsi->dev, "event ring %u bad state %u after alloc\n",
		evt_ring_id, state);

	return -EIO;
}

/* Reset a GSI event ring in ALLOCATED or ERROR state. */
static void gsi_evt_ring_reset_command(struct gsi *gsi, u32 evt_ring_id)
{
	enum gsi_evt_ring_state state;

	state = gsi_evt_ring_state(gsi, evt_ring_id);
	if (state != GSI_EVT_RING_STATE_ALLOCATED &&
	    state != GSI_EVT_RING_STATE_ERROR) {
		dev_err(gsi->dev, "event ring %u bad state %u before reset\n",
			evt_ring_id, state);
		return;
	}

	gsi_evt_ring_command(gsi, evt_ring_id, GSI_EVT_RESET);

	/* If successful the event ring state will have changed */
	state = gsi_evt_ring_state(gsi, evt_ring_id);
	if (state == GSI_EVT_RING_STATE_ALLOCATED)
		return;

	dev_err(gsi->dev, "event ring %u bad state %u after reset\n",
		evt_ring_id, state);
}

/* Issue a hardware de-allocation request for an allocated event ring */
static void gsi_evt_ring_de_alloc_command(struct gsi *gsi, u32 evt_ring_id)
{
	enum gsi_evt_ring_state state;

	state = gsi_evt_ring_state(gsi, evt_ring_id);
	if (state != GSI_EVT_RING_STATE_ALLOCATED) {
		dev_err(gsi->dev, "event ring %u state %u before dealloc\n",
			evt_ring_id, state);
		return;
	}

	gsi_evt_ring_command(gsi, evt_ring_id, GSI_EVT_DE_ALLOC);

	/* If successful the event ring state will have changed */
	state = gsi_evt_ring_state(gsi, evt_ring_id);
	if (state == GSI_EVT_RING_STATE_NOT_ALLOCATED)
		return;

	dev_err(gsi->dev, "event ring %u bad state %u after dealloc\n",
		evt_ring_id, state);
}

/* Fetch the current state of a channel from hardware */
static enum gsi_channel_state gsi_channel_state(struct gsi_channel *channel)
{
	const struct reg *reg = gsi_reg(channel->gsi, CH_C_CNTXT_0);
	u32 channel_id = gsi_channel_id(channel);
	struct gsi *gsi = channel->gsi;
	void __iomem *virt = gsi->virt;
	u32 val;

	reg = gsi_reg(gsi, CH_C_CNTXT_0);
	val = ioread32(virt + reg_n_offset(reg, channel_id));

	return reg_decode(reg, CHSTATE, val);
}

/* Issue a channel command and wait for it to complete */
static void
gsi_channel_command(struct gsi_channel *channel, enum gsi_ch_cmd_opcode opcode)
{
	u32 channel_id = gsi_channel_id(channel);
	struct gsi *gsi = channel->gsi;
	struct device *dev = gsi->dev;
	const struct reg *reg;
	bool timeout;
	u32 val;

	/* Enable the completion interrupt for the command */
	gsi_irq_ch_ctrl_enable(gsi, channel_id);

	reg = gsi_reg(gsi, CH_CMD);
	val = reg_encode(reg, CH_CHID, channel_id);
	val |= reg_encode(reg, CH_OPCODE, opcode);

	timeout = !gsi_command(gsi, reg_offset(reg), val);

	gsi_irq_ch_ctrl_disable(gsi);

	if (!timeout)
		return;

	dev_err(dev, "GSI command %u for channel %u timed out, state %u\n",
		opcode, channel_id, gsi_channel_state(channel));
}

/* Allocate GSI channel in NOT_ALLOCATED state */
static int gsi_channel_alloc_command(struct gsi *gsi, u32 channel_id)
{
	struct gsi_channel *channel = &gsi->channel[channel_id];
	struct device *dev = gsi->dev;
	enum gsi_channel_state state;

	/* Get initial channel state */
	state = gsi_channel_state(channel);
	if (state != GSI_CHANNEL_STATE_NOT_ALLOCATED) {
		dev_err(dev, "channel %u bad state %u before alloc\n",
			channel_id, state);
		return -EINVAL;
	}

	gsi_channel_command(channel, GSI_CH_ALLOCATE);

	/* If successful the channel state will have changed */
	state = gsi_channel_state(channel);
	if (state == GSI_CHANNEL_STATE_ALLOCATED)
		return 0;

	dev_err(dev, "channel %u bad state %u after alloc\n",
		channel_id, state);

	return -EIO;
}

/* Start an ALLOCATED channel */
static int gsi_channel_start_command(struct gsi_channel *channel)
{
	struct device *dev = channel->gsi->dev;
	enum gsi_channel_state state;

	state = gsi_channel_state(channel);
	if (state != GSI_CHANNEL_STATE_ALLOCATED &&
	    state != GSI_CHANNEL_STATE_STOPPED) {
		dev_err(dev, "channel %u bad state %u before start\n",
			gsi_channel_id(channel), state);
		return -EINVAL;
	}

	gsi_channel_command(channel, GSI_CH_START);

	/* If successful the channel state will have changed */
	state = gsi_channel_state(channel);
	if (state == GSI_CHANNEL_STATE_STARTED)
		return 0;

	dev_err(dev, "channel %u bad state %u after start\n",
		gsi_channel_id(channel), state);

	return -EIO;
}

/* Stop a GSI channel in STARTED state */
static int gsi_channel_stop_command(struct gsi_channel *channel)
{
	struct device *dev = channel->gsi->dev;
	enum gsi_channel_state state;

	state = gsi_channel_state(channel);

	/* Channel could have entered STOPPED state since last call
	 * if it timed out.  If so, we're done.
	 */
	if (state == GSI_CHANNEL_STATE_STOPPED)
		return 0;

	if (state != GSI_CHANNEL_STATE_STARTED &&
	    state != GSI_CHANNEL_STATE_STOP_IN_PROC) {
		dev_err(dev, "channel %u bad state %u before stop\n",
			gsi_channel_id(channel), state);
		return -EINVAL;
	}

	gsi_channel_command(channel, GSI_CH_STOP);

	/* If successful the channel state will have changed */
	state = gsi_channel_state(channel);
	if (state == GSI_CHANNEL_STATE_STOPPED)
		return 0;

	/* We may have to try again if stop is in progress */
	if (state == GSI_CHANNEL_STATE_STOP_IN_PROC)
		return -EAGAIN;

	dev_err(dev, "channel %u bad state %u after stop\n",
		gsi_channel_id(channel), state);

	return -EIO;
}

/* Reset a GSI channel in ALLOCATED or ERROR state. */
static void gsi_channel_reset_command(struct gsi_channel *channel)
{
	struct device *dev = channel->gsi->dev;
	enum gsi_channel_state state;

	/* A short delay is required before a RESET command */
	usleep_range(USEC_PER_MSEC, 2 * USEC_PER_MSEC);

	state = gsi_channel_state(channel);
	if (state != GSI_CHANNEL_STATE_STOPPED &&
	    state != GSI_CHANNEL_STATE_ERROR) {
		/* No need to reset a channel already in ALLOCATED state */
		if (state != GSI_CHANNEL_STATE_ALLOCATED)
			dev_err(dev, "channel %u bad state %u before reset\n",
				gsi_channel_id(channel), state);
		return;
	}

	gsi_channel_command(channel, GSI_CH_RESET);

	/* If successful the channel state will have changed */
	state = gsi_channel_state(channel);
	if (state != GSI_CHANNEL_STATE_ALLOCATED)
		dev_err(dev, "channel %u bad state %u after reset\n",
			gsi_channel_id(channel), state);
}

/* Deallocate an ALLOCATED GSI channel */
static void gsi_channel_de_alloc_command(struct gsi *gsi, u32 channel_id)
{
	struct gsi_channel *channel = &gsi->channel[channel_id];
	struct device *dev = gsi->dev;
	enum gsi_channel_state state;

	state = gsi_channel_state(channel);
	if (state != GSI_CHANNEL_STATE_ALLOCATED) {
		dev_err(dev, "channel %u bad state %u before dealloc\n",
			channel_id, state);
		return;
	}

	gsi_channel_command(channel, GSI_CH_DE_ALLOC);

	/* If successful the channel state will have changed */
	state = gsi_channel_state(channel);

	if (state != GSI_CHANNEL_STATE_NOT_ALLOCATED)
		dev_err(dev, "channel %u bad state %u after dealloc\n",
			channel_id, state);
}

/* Ring an event ring doorbell, reporting the last entry processed by the AP.
 * The index argument (modulo the ring count) is the first unfilled entry, so
 * we supply one less than that with the doorbell.  Update the event ring
 * index field with the value provided.
 */
static void gsi_evt_ring_doorbell(struct gsi *gsi, u32 evt_ring_id, u32 index)
{
	const struct reg *reg = gsi_reg(gsi, EV_CH_E_DOORBELL_0);
	struct gsi_ring *ring = &gsi->evt_ring[evt_ring_id].ring;
	u32 val;

	ring->index = index;	/* Next unused entry */

	/* Note: index *must* be used modulo the ring count here */
	val = gsi_ring_addr(ring, (index - 1) % ring->count);
	iowrite32(val, gsi->virt + reg_n_offset(reg, evt_ring_id));
}

/* Program an event ring for use */
static void gsi_evt_ring_program(struct gsi *gsi, u32 evt_ring_id)
{
	struct gsi_evt_ring *evt_ring = &gsi->evt_ring[evt_ring_id];
	struct gsi_ring *ring = &evt_ring->ring;
	const struct reg *reg;
	u32 val;

	reg = gsi_reg(gsi, EV_CH_E_CNTXT_0);
	/* We program all event rings as GPI type/protocol */
	val = reg_encode(reg, EV_CHTYPE, GSI_CHANNEL_TYPE_GPI);
	/* EV_EE field is 0 (GSI_EE_AP) */
	val |= reg_bit(reg, EV_INTYPE);
	val |= reg_encode(reg, EV_ELEMENT_SIZE, GSI_RING_ELEMENT_SIZE);
	iowrite32(val, gsi->virt + reg_n_offset(reg, evt_ring_id));

	reg = gsi_reg(gsi, EV_CH_E_CNTXT_1);
	val = reg_encode(reg, R_LENGTH, ring->count * GSI_RING_ELEMENT_SIZE);
	iowrite32(val, gsi->virt + reg_n_offset(reg, evt_ring_id));

	/* The context 2 and 3 registers store the low-order and
	 * high-order 32 bits of the address of the event ring,
	 * respectively.
	 */
	reg = gsi_reg(gsi, EV_CH_E_CNTXT_2);
	val = lower_32_bits(ring->addr);
	iowrite32(val, gsi->virt + reg_n_offset(reg, evt_ring_id));

	reg = gsi_reg(gsi, EV_CH_E_CNTXT_3);
	val = upper_32_bits(ring->addr);
	iowrite32(val, gsi->virt + reg_n_offset(reg, evt_ring_id));

	/* Enable interrupt moderation by setting the moderation delay */
	reg = gsi_reg(gsi, EV_CH_E_CNTXT_8);
	val = reg_encode(reg, EV_MODT, GSI_EVT_RING_INT_MODT);
	val |= reg_encode(reg, EV_MODC, 1);	/* comes from channel */
	/* EV_MOD_CNT is 0 (no counter-based interrupt coalescing) */
	iowrite32(val, gsi->virt + reg_n_offset(reg, evt_ring_id));

	/* No MSI write data, and MSI high and low address is 0 */
	reg = gsi_reg(gsi, EV_CH_E_CNTXT_9);
	iowrite32(0, gsi->virt + reg_n_offset(reg, evt_ring_id));

	reg = gsi_reg(gsi, EV_CH_E_CNTXT_10);
	iowrite32(0, gsi->virt + reg_n_offset(reg, evt_ring_id));

	reg = gsi_reg(gsi, EV_CH_E_CNTXT_11);
	iowrite32(0, gsi->virt + reg_n_offset(reg, evt_ring_id));

	/* We don't need to get event read pointer updates */
	reg = gsi_reg(gsi, EV_CH_E_CNTXT_12);
	iowrite32(0, gsi->virt + reg_n_offset(reg, evt_ring_id));

	reg = gsi_reg(gsi, EV_CH_E_CNTXT_13);
	iowrite32(0, gsi->virt + reg_n_offset(reg, evt_ring_id));

	/* Finally, tell the hardware our "last processed" event (arbitrary) */
	gsi_evt_ring_doorbell(gsi, evt_ring_id, ring->index);
}

/* Find the transaction whose completion indicates a channel is quiesced */
static struct gsi_trans *gsi_channel_trans_last(struct gsi_channel *channel)
{
	struct gsi_trans_info *trans_info = &channel->trans_info;
	u32 pending_id = trans_info->pending_id;
	struct gsi_trans *trans;
	u16 trans_id;

	if (channel->toward_ipa && pending_id != trans_info->free_id) {
		/* There is a small chance a TX transaction got allocated
		 * just before we disabled transmits, so check for that.
		 * The last allocated, committed, or pending transaction
		 * precedes the first free transaction.
		 */
		trans_id = trans_info->free_id - 1;
	} else if (trans_info->polled_id != pending_id) {
		/* Otherwise (TX or RX) we want to wait for anything that
		 * has completed, or has been polled but not released yet.
		 *
		 * The last completed or polled transaction precedes the
		 * first pending transaction.
		 */
		trans_id = pending_id - 1;
	} else {
		return NULL;
	}

	/* Caller will wait for this, so take a reference */
	trans = &trans_info->trans[trans_id % channel->tre_count];
	refcount_inc(&trans->refcount);

	return trans;
}

/* Wait for transaction activity on a channel to complete */
static void gsi_channel_trans_quiesce(struct gsi_channel *channel)
{
	struct gsi_trans *trans;

	/* Get the last transaction, and wait for it to complete */
	trans = gsi_channel_trans_last(channel);
	if (trans) {
		wait_for_completion(&trans->completion);
		gsi_trans_free(trans);
	}
}

/* Program a channel for use; there is no gsi_channel_deprogram() */
static void gsi_channel_program(struct gsi_channel *channel, bool doorbell)
{
	size_t size = channel->tre_ring.count * GSI_RING_ELEMENT_SIZE;
	u32 channel_id = gsi_channel_id(channel);
	union gsi_channel_scratch scr = { };
	struct gsi_channel_scratch_gpi *gpi;
	struct gsi *gsi = channel->gsi;
	const struct reg *reg;
	u32 wrr_weight = 0;
	u32 offset;
	u32 val;

	reg = gsi_reg(gsi, CH_C_CNTXT_0);

	/* We program all channels as GPI type/protocol */
	val = ch_c_cntxt_0_type_encode(gsi->version, reg, GSI_CHANNEL_TYPE_GPI);
	if (channel->toward_ipa)
		val |= reg_bit(reg, CHTYPE_DIR);
	if (gsi->version < IPA_VERSION_5_0)
		val |= reg_encode(reg, ERINDEX, channel->evt_ring_id);
	val |= reg_encode(reg, ELEMENT_SIZE, GSI_RING_ELEMENT_SIZE);
	iowrite32(val, gsi->virt + reg_n_offset(reg, channel_id));

	reg = gsi_reg(gsi, CH_C_CNTXT_1);
	val = reg_encode(reg, CH_R_LENGTH, size);
	if (gsi->version >= IPA_VERSION_5_0)
		val |= reg_encode(reg, CH_ERINDEX, channel->evt_ring_id);
	iowrite32(val, gsi->virt + reg_n_offset(reg, channel_id));

	/* The context 2 and 3 registers store the low-order and
	 * high-order 32 bits of the address of the channel ring,
	 * respectively.
	 */
	reg = gsi_reg(gsi, CH_C_CNTXT_2);
	val = lower_32_bits(channel->tre_ring.addr);
	iowrite32(val, gsi->virt + reg_n_offset(reg, channel_id));

	reg = gsi_reg(gsi, CH_C_CNTXT_3);
	val = upper_32_bits(channel->tre_ring.addr);
	iowrite32(val, gsi->virt + reg_n_offset(reg, channel_id));

	reg = gsi_reg(gsi, CH_C_QOS);

	/* Command channel gets low weighted round-robin priority */
	if (channel->command)
		wrr_weight = reg_field_max(reg, WRR_WEIGHT);
	val = reg_encode(reg, WRR_WEIGHT, wrr_weight);

	/* Max prefetch is 1 segment (do not set MAX_PREFETCH_FMASK) */

	/* No need to use the doorbell engine starting at IPA v4.0 */
	if (gsi->version < IPA_VERSION_4_0 && doorbell)
		val |= reg_bit(reg, USE_DB_ENG);

	/* v4.0 introduces an escape buffer for prefetch.  We use it
	 * on all but the AP command channel.
	 */
	if (gsi->version >= IPA_VERSION_4_0 && !channel->command) {
		/* If not otherwise set, prefetch buffers are used */
		if (gsi->version < IPA_VERSION_4_5)
			val |= reg_bit(reg, USE_ESCAPE_BUF_ONLY);
		else
			val |= reg_encode(reg, PREFETCH_MODE, ESCAPE_BUF_ONLY);
	}
	/* All channels set DB_IN_BYTES */
	if (gsi->version >= IPA_VERSION_4_9)
		val |= reg_bit(reg, DB_IN_BYTES);

	iowrite32(val, gsi->virt + reg_n_offset(reg, channel_id));

	/* Now update the scratch registers for GPI protocol */
	gpi = &scr.gpi;
	gpi->max_outstanding_tre = channel->trans_tre_max *
					GSI_RING_ELEMENT_SIZE;
	gpi->outstanding_threshold = 2 * GSI_RING_ELEMENT_SIZE;

	reg = gsi_reg(gsi, CH_C_SCRATCH_0);
	val = scr.data.word1;
	iowrite32(val, gsi->virt + reg_n_offset(reg, channel_id));

	reg = gsi_reg(gsi, CH_C_SCRATCH_1);
	val = scr.data.word2;
	iowrite32(val, gsi->virt + reg_n_offset(reg, channel_id));

	reg = gsi_reg(gsi, CH_C_SCRATCH_2);
	val = scr.data.word3;
	iowrite32(val, gsi->virt + reg_n_offset(reg, channel_id));

	/* We must preserve the upper 16 bits of the last scratch register.
	 * The next sequence assumes those bits remain unchanged between the
	 * read and the write.
	 */
	reg = gsi_reg(gsi, CH_C_SCRATCH_3);
	offset = reg_n_offset(reg, channel_id);
	val = ioread32(gsi->virt + offset);
	val = (scr.data.word4 & GENMASK(31, 16)) | (val & GENMASK(15, 0));
	iowrite32(val, gsi->virt + offset);

	/* All done! */
}

static int __gsi_channel_start(struct gsi_channel *channel, bool resume)
{
	struct gsi *gsi = channel->gsi;
	int ret;

	/* Prior to IPA v4.0 suspend/resume is not implemented by GSI */
	if (resume && gsi->version < IPA_VERSION_4_0)
		return 0;

	mutex_lock(&gsi->mutex);

	ret = gsi_channel_start_command(channel);

	mutex_unlock(&gsi->mutex);

	return ret;
}

/* Start an allocated GSI channel */
int gsi_channel_start(struct gsi *gsi, u32 channel_id)
{
	struct gsi_channel *channel = &gsi->channel[channel_id];
	int ret;

	/* Enable NAPI and the completion interrupt */
	napi_enable(&channel->napi);
	gsi_irq_ieob_enable_one(gsi, channel->evt_ring_id);

	ret = __gsi_channel_start(channel, false);
	if (ret) {
		gsi_irq_ieob_disable_one(gsi, channel->evt_ring_id);
		napi_disable(&channel->napi);
	}

	return ret;
}

static int gsi_channel_stop_retry(struct gsi_channel *channel)
{
	u32 retries = GSI_CHANNEL_STOP_RETRIES;
	int ret;

	do {
		ret = gsi_channel_stop_command(channel);
		if (ret != -EAGAIN)
			break;
		usleep_range(3 * USEC_PER_MSEC, 5 * USEC_PER_MSEC);
	} while (retries--);

	return ret;
}

static int __gsi_channel_stop(struct gsi_channel *channel, bool suspend)
{
	struct gsi *gsi = channel->gsi;
	int ret;

	/* Wait for any underway transactions to complete before stopping. */
	gsi_channel_trans_quiesce(channel);

	/* Prior to IPA v4.0 suspend/resume is not implemented by GSI */
	if (suspend && gsi->version < IPA_VERSION_4_0)
		return 0;

	mutex_lock(&gsi->mutex);

	ret = gsi_channel_stop_retry(channel);

	mutex_unlock(&gsi->mutex);

	return ret;
}

/* Stop a started channel */
int gsi_channel_stop(struct gsi *gsi, u32 channel_id)
{
	struct gsi_channel *channel = &gsi->channel[channel_id];
	int ret;

	ret = __gsi_channel_stop(channel, false);
	if (ret)
		return ret;

	/* Disable the completion interrupt and NAPI if successful */
	gsi_irq_ieob_disable_one(gsi, channel->evt_ring_id);
	napi_disable(&channel->napi);

	return 0;
}

/* Reset and reconfigure a channel, (possibly) enabling the doorbell engine */
void gsi_channel_reset(struct gsi *gsi, u32 channel_id, bool doorbell)
{
	struct gsi_channel *channel = &gsi->channel[channel_id];

	mutex_lock(&gsi->mutex);

	gsi_channel_reset_command(channel);
	/* Due to a hardware quirk we may need to reset RX channels twice. */
	if (gsi->version < IPA_VERSION_4_0 && !channel->toward_ipa)
		gsi_channel_reset_command(channel);

	/* Hardware assumes this is 0 following reset */
	channel->tre_ring.index = 0;
	gsi_channel_program(channel, doorbell);
	gsi_channel_trans_cancel_pending(channel);

	mutex_unlock(&gsi->mutex);
}

/* Stop a started channel for suspend */
int gsi_channel_suspend(struct gsi *gsi, u32 channel_id)
{
	struct gsi_channel *channel = &gsi->channel[channel_id];
	int ret;

	ret = __gsi_channel_stop(channel, true);
	if (ret)
		return ret;

	/* Ensure NAPI polling has finished. */
	napi_synchronize(&channel->napi);

	return 0;
}

/* Resume a suspended channel (starting if stopped) */
int gsi_channel_resume(struct gsi *gsi, u32 channel_id)
{
	struct gsi_channel *channel = &gsi->channel[channel_id];

	return __gsi_channel_start(channel, true);
}

/* Prevent all GSI interrupts while suspended */
void gsi_suspend(struct gsi *gsi)
{
	disable_irq(gsi->irq);
}

/* Allow all GSI interrupts again when resuming */
void gsi_resume(struct gsi *gsi)
{
	enable_irq(gsi->irq);
}

void gsi_trans_tx_committed(struct gsi_trans *trans)
{
	struct gsi_channel *channel = &trans->gsi->channel[trans->channel_id];

	channel->trans_count++;
	channel->byte_count += trans->len;

	trans->trans_count = channel->trans_count;
	trans->byte_count = channel->byte_count;
}

void gsi_trans_tx_queued(struct gsi_trans *trans)
{
	u32 channel_id = trans->channel_id;
	struct gsi *gsi = trans->gsi;
	struct gsi_channel *channel;
	u32 trans_count;
	u32 byte_count;

	channel = &gsi->channel[channel_id];

	byte_count = channel->byte_count - channel->queued_byte_count;
	trans_count = channel->trans_count - channel->queued_trans_count;
	channel->queued_byte_count = channel->byte_count;
	channel->queued_trans_count = channel->trans_count;

	ipa_gsi_channel_tx_queued(gsi, channel_id, trans_count, byte_count);
}

/**
 * gsi_trans_tx_completed() - Report completed TX transactions
 * @trans:	TX channel transaction that has completed
 *
 * Report that a transaction on a TX channel has completed.  At the time a
 * transaction is committed, we record *in the transaction* its channel's
 * committed transaction and byte counts.  Transactions are completed in
 * order, and the difference between the channel's byte/transaction count
 * when the transaction was committed and when it completes tells us
 * exactly how much data has been transferred while the transaction was
 * pending.
 *
 * We report this information to the network stack, which uses it to manage
 * the rate at which data is sent to hardware.
 */
static void gsi_trans_tx_completed(struct gsi_trans *trans)
{
	u32 channel_id = trans->channel_id;
	struct gsi *gsi = trans->gsi;
	struct gsi_channel *channel;
	u32 trans_count;
	u32 byte_count;

	channel = &gsi->channel[channel_id];
	trans_count = trans->trans_count - channel->compl_trans_count;
	byte_count = trans->byte_count - channel->compl_byte_count;

	channel->compl_trans_count += trans_count;
	channel->compl_byte_count += byte_count;

	ipa_gsi_channel_tx_completed(gsi, channel_id, trans_count, byte_count);
}

/* Channel control interrupt handler */
static void gsi_isr_chan_ctrl(struct gsi *gsi)
{
	const struct reg *reg;
	u32 channel_mask;

	reg = gsi_reg(gsi, CNTXT_SRC_CH_IRQ);
	channel_mask = ioread32(gsi->virt + reg_offset(reg));

	reg = gsi_reg(gsi, CNTXT_SRC_CH_IRQ_CLR);
	iowrite32(channel_mask, gsi->virt + reg_offset(reg));

	while (channel_mask) {
		u32 channel_id = __ffs(channel_mask);

		channel_mask ^= BIT(channel_id);

		complete(&gsi->completion);
	}
}

/* Event ring control interrupt handler */
static void gsi_isr_evt_ctrl(struct gsi *gsi)
{
	const struct reg *reg;
	u32 event_mask;

	reg = gsi_reg(gsi, CNTXT_SRC_EV_CH_IRQ);
	event_mask = ioread32(gsi->virt + reg_offset(reg));

	reg = gsi_reg(gsi, CNTXT_SRC_EV_CH_IRQ_CLR);
	iowrite32(event_mask, gsi->virt + reg_offset(reg));

	while (event_mask) {
		u32 evt_ring_id = __ffs(event_mask);

		event_mask ^= BIT(evt_ring_id);

		complete(&gsi->completion);
	}
}

/* Global channel error interrupt handler */
static void
gsi_isr_glob_chan_err(struct gsi *gsi, u32 err_ee, u32 channel_id, u32 code)
{
	if (code == GSI_OUT_OF_RESOURCES) {
		dev_err(gsi->dev, "channel %u out of resources\n", channel_id);
		complete(&gsi->completion);
		return;
	}

	/* Report, but otherwise ignore all other error codes */
	dev_err(gsi->dev, "channel %u global error ee 0x%08x code 0x%08x\n",
		channel_id, err_ee, code);
}

/* Global event error interrupt handler */
static void
gsi_isr_glob_evt_err(struct gsi *gsi, u32 err_ee, u32 evt_ring_id, u32 code)
{
	if (code == GSI_OUT_OF_RESOURCES) {
		struct gsi_evt_ring *evt_ring = &gsi->evt_ring[evt_ring_id];
		u32 channel_id = gsi_channel_id(evt_ring->channel);

		complete(&gsi->completion);
		dev_err(gsi->dev, "evt_ring for channel %u out of resources\n",
			channel_id);
		return;
	}

	/* Report, but otherwise ignore all other error codes */
	dev_err(gsi->dev, "event ring %u global error ee %u code 0x%08x\n",
		evt_ring_id, err_ee, code);
}

/* Global error interrupt handler */
static void gsi_isr_glob_err(struct gsi *gsi)
{
	const struct reg *log_reg;
	const struct reg *clr_reg;
	enum gsi_err_type type;
	enum gsi_err_code code;
	u32 offset;
	u32 which;
	u32 val;
	u32 ee;

	/* Get the logged error, then reinitialize the log */
	log_reg = gsi_reg(gsi, ERROR_LOG);
	offset = reg_offset(log_reg);
	val = ioread32(gsi->virt + offset);
	iowrite32(0, gsi->virt + offset);

	clr_reg = gsi_reg(gsi, ERROR_LOG_CLR);
	iowrite32(~0, gsi->virt + reg_offset(clr_reg));

	/* Parse the error value */
	ee = reg_decode(log_reg, ERR_EE, val);
	type = reg_decode(log_reg, ERR_TYPE, val);
	which = reg_decode(log_reg, ERR_VIRT_IDX, val);
	code = reg_decode(log_reg, ERR_CODE, val);

	if (type == GSI_ERR_TYPE_CHAN)
		gsi_isr_glob_chan_err(gsi, ee, which, code);
	else if (type == GSI_ERR_TYPE_EVT)
		gsi_isr_glob_evt_err(gsi, ee, which, code);
	else	/* type GSI_ERR_TYPE_GLOB should be fatal */
		dev_err(gsi->dev, "unexpected global error 0x%08x\n", type);
}

/* Generic EE interrupt handler */
static void gsi_isr_gp_int1(struct gsi *gsi)
{
	const struct reg *reg;
	u32 result;
	u32 val;

	/* This interrupt is used to handle completions of GENERIC GSI
	 * commands.  We use these to allocate and halt channels on the
	 * modem's behalf due to a hardware quirk on IPA v4.2.  The modem
	 * "owns" channels even when the AP allocates them, and have no
	 * way of knowing whether a modem channel's state has been changed.
	 *
	 * We also use GENERIC commands to enable/disable channel flow
	 * control for IPA v4.2+.
	 *
	 * It is recommended that we halt the modem channels we allocated
	 * when shutting down, but it's possible the channel isn't running
	 * at the time we issue the HALT command.  We'll get an error in
	 * that case, but it's harmless (the channel is already halted).
	 * Similarly, we could get an error back when updating flow control
	 * on a channel because it's not in the proper state.
	 *
	 * In either case, we silently ignore a INCORRECT_CHANNEL_STATE
	 * error if we receive it.
	 */
	reg = gsi_reg(gsi, CNTXT_SCRATCH_0);
	val = ioread32(gsi->virt + reg_offset(reg));
	result = reg_decode(reg, GENERIC_EE_RESULT, val);

	switch (result) {
	case GENERIC_EE_SUCCESS:
	case GENERIC_EE_INCORRECT_CHANNEL_STATE:
		gsi->result = 0;
		break;

	case GENERIC_EE_RETRY:
		gsi->result = -EAGAIN;
		break;

	default:
		dev_err(gsi->dev, "global INT1 generic result %u\n", result);
		gsi->result = -EIO;
		break;
	}

	complete(&gsi->completion);
}

/* Inter-EE interrupt handler */
static void gsi_isr_glob_ee(struct gsi *gsi)
{
	const struct reg *reg;
	u32 val;

	reg = gsi_reg(gsi, CNTXT_GLOB_IRQ_STTS);
	val = ioread32(gsi->virt + reg_offset(reg));

	if (val & ERROR_INT)
		gsi_isr_glob_err(gsi);

	reg = gsi_reg(gsi, CNTXT_GLOB_IRQ_CLR);
	iowrite32(val, gsi->virt + reg_offset(reg));

	val &= ~ERROR_INT;

	if (val & GP_INT1) {
		val ^= GP_INT1;
		gsi_isr_gp_int1(gsi);
	}

	if (val)
		dev_err(gsi->dev, "unexpected global interrupt 0x%08x\n", val);
}

/* I/O completion interrupt event */
static void gsi_isr_ieob(struct gsi *gsi)
{
	const struct reg *reg;
	u32 event_mask;

	reg = gsi_reg(gsi, CNTXT_SRC_IEOB_IRQ);
	event_mask = ioread32(gsi->virt + reg_offset(reg));

	gsi_irq_ieob_disable(gsi, event_mask);

	reg = gsi_reg(gsi, CNTXT_SRC_IEOB_IRQ_CLR);
	iowrite32(event_mask, gsi->virt + reg_offset(reg));

	while (event_mask) {
		u32 evt_ring_id = __ffs(event_mask);

		event_mask ^= BIT(evt_ring_id);

		napi_schedule(&gsi->evt_ring[evt_ring_id].channel->napi);
	}
}

/* General event interrupts represent serious problems, so report them */
static void gsi_isr_general(struct gsi *gsi)
{
	struct device *dev = gsi->dev;
	const struct reg *reg;
	u32 val;

	reg = gsi_reg(gsi, CNTXT_GSI_IRQ_STTS);
	val = ioread32(gsi->virt + reg_offset(reg));

	reg = gsi_reg(gsi, CNTXT_GSI_IRQ_CLR);
	iowrite32(val, gsi->virt + reg_offset(reg));

	dev_err(dev, "unexpected general interrupt 0x%08x\n", val);
}

/**
 * gsi_isr() - Top level GSI interrupt service routine
 * @irq:	Interrupt number (ignored)
 * @dev_id:	GSI pointer supplied to request_irq()
 *
 * This is the main handler function registered for the GSI IRQ. Each type
 * of interrupt has a separate handler function that is called from here.
 */
static irqreturn_t gsi_isr(int irq, void *dev_id)
{
	struct gsi *gsi = dev_id;
	const struct reg *reg;
	u32 intr_mask;
	u32 cnt = 0;
	u32 offset;

	reg = gsi_reg(gsi, CNTXT_TYPE_IRQ);
	offset = reg_offset(reg);

	/* enum gsi_irq_type_id defines GSI interrupt types */
	while ((intr_mask = ioread32(gsi->virt + offset))) {
		/* intr_mask contains bitmask of pending GSI interrupts */
		do {
			u32 gsi_intr = BIT(__ffs(intr_mask));

			intr_mask ^= gsi_intr;

			/* Note: the IRQ condition for each type is cleared
			 * when the type-specific register is updated.
			 */
			switch (gsi_intr) {
			case GSI_CH_CTRL:
				gsi_isr_chan_ctrl(gsi);
				break;
			case GSI_EV_CTRL:
				gsi_isr_evt_ctrl(gsi);
				break;
			case GSI_GLOB_EE:
				gsi_isr_glob_ee(gsi);
				break;
			case GSI_IEOB:
				gsi_isr_ieob(gsi);
				break;
			case GSI_GENERAL:
				gsi_isr_general(gsi);
				break;
			default:
				dev_err(gsi->dev,
					"unrecognized interrupt type 0x%08x\n",
					gsi_intr);
				break;
			}
		} while (intr_mask);

		if (++cnt > GSI_ISR_MAX_ITER) {
			dev_err(gsi->dev, "interrupt flood\n");
			break;
		}
	}

	return IRQ_HANDLED;
}

/* Init function for GSI IRQ lookup; there is no gsi_irq_exit() */
static int gsi_irq_init(struct gsi *gsi, struct platform_device *pdev)
{
	int ret;

	ret = platform_get_irq_byname(pdev, "gsi");
	if (ret <= 0)
		return ret ? : -EINVAL;

	gsi->irq = ret;

	return 0;
}

/* Return the transaction associated with a transfer completion event */
static struct gsi_trans *
gsi_event_trans(struct gsi *gsi, struct gsi_event *event)
{
	u32 channel_id = event->chid;
	struct gsi_channel *channel;
	struct gsi_trans *trans;
	u32 tre_offset;
	u32 tre_index;

	channel = &gsi->channel[channel_id];
	if (WARN(!channel->gsi, "event has bad channel %u\n", channel_id))
		return NULL;

	/* Event xfer_ptr records the TRE it's associated with */
	tre_offset = lower_32_bits(le64_to_cpu(event->xfer_ptr));
	tre_index = gsi_ring_index(&channel->tre_ring, tre_offset);

	trans = gsi_channel_trans_mapped(channel, tre_index);

	if (WARN(!trans, "channel %u event with no transaction\n", channel_id))
		return NULL;

	return trans;
}

/**
 * gsi_evt_ring_update() - Update transaction state from hardware
 * @gsi:		GSI pointer
 * @evt_ring_id:	Event ring ID
 * @index:		Event index in ring reported by hardware
 *
 * Events for RX channels contain the actual number of bytes received into
 * the buffer.  Every event has a transaction associated with it, and here
 * we update transactions to record their actual received lengths.
 *
 * When an event for a TX channel arrives we use information in the
 * transaction to report the number of requests and bytes that have
 * been transferred.
 *
 * This function is called whenever we learn that the GSI hardware has filled
 * new events since the last time we checked.  The ring's index field tells
 * the first entry in need of processing.  The index provided is the
 * first *unfilled* event in the ring (following the last filled one).
 *
 * Events are sequential within the event ring, and transactions are
 * sequential within the transaction array.
 *
 * Note that @index always refers to an element *within* the event ring.
 */
static void gsi_evt_ring_update(struct gsi *gsi, u32 evt_ring_id, u32 index)
{
	struct gsi_evt_ring *evt_ring = &gsi->evt_ring[evt_ring_id];
	struct gsi_ring *ring = &evt_ring->ring;
	struct gsi_event *event_done;
	struct gsi_event *event;
	u32 event_avail;
	u32 old_index;

	/* Starting with the oldest un-processed event, determine which
	 * transaction (and which channel) is associated with the event.
	 * For RX channels, update each completed transaction with the
	 * number of bytes that were actually received.  For TX channels
	 * associated with a network device, report to the network stack
	 * the number of transfers and bytes this completion represents.
	 */
	old_index = ring->index;
	event = gsi_ring_virt(ring, old_index);

	/* Compute the number of events to process before we wrap,
	 * and determine when we'll be done processing events.
	 */
	event_avail = ring->count - old_index % ring->count;
	event_done = gsi_ring_virt(ring, index);
	do {
		struct gsi_trans *trans;

		trans = gsi_event_trans(gsi, event);
		if (!trans)
			return;

		if (trans->direction == DMA_FROM_DEVICE)
			trans->len = __le16_to_cpu(event->len);
		else
			gsi_trans_tx_completed(trans);

		gsi_trans_move_complete(trans);

		/* Move on to the next event and transaction */
		if (--event_avail)
			event++;
		else
			event = gsi_ring_virt(ring, 0);
	} while (event != event_done);

	/* Tell the hardware we've handled these events */
	gsi_evt_ring_doorbell(gsi, evt_ring_id, index);
}

/* Initialize a ring, including allocating DMA memory for its entries */
static int gsi_ring_alloc(struct gsi *gsi, struct gsi_ring *ring, u32 count)
{
	u32 size = count * GSI_RING_ELEMENT_SIZE;
	struct device *dev = gsi->dev;
	dma_addr_t addr;

	/* Hardware requires a 2^n ring size, with alignment equal to size.
	 * The DMA address returned by dma_alloc_coherent() is guaranteed to
	 * be a power-of-2 number of pages, which satisfies the requirement.
	 */
	ring->virt = dma_alloc_coherent(dev, size, &addr, GFP_KERNEL);
	if (!ring->virt)
		return -ENOMEM;

	ring->addr = addr;
	ring->count = count;
	ring->index = 0;

	return 0;
}

/* Free a previously-allocated ring */
static void gsi_ring_free(struct gsi *gsi, struct gsi_ring *ring)
{
	size_t size = ring->count * GSI_RING_ELEMENT_SIZE;

	dma_free_coherent(gsi->dev, size, ring->virt, ring->addr);
}

/* Allocate an available event ring id */
static int gsi_evt_ring_id_alloc(struct gsi *gsi)
{
	u32 evt_ring_id;

	if (gsi->event_bitmap == ~0U) {
		dev_err(gsi->dev, "event rings exhausted\n");
		return -ENOSPC;
	}

	evt_ring_id = ffz(gsi->event_bitmap);
	gsi->event_bitmap |= BIT(evt_ring_id);

	return (int)evt_ring_id;
}

/* Free a previously-allocated event ring id */
static void gsi_evt_ring_id_free(struct gsi *gsi, u32 evt_ring_id)
{
	gsi->event_bitmap &= ~BIT(evt_ring_id);
}

/* Ring a channel doorbell, reporting the first un-filled entry */
void gsi_channel_doorbell(struct gsi_channel *channel)
{
	struct gsi_ring *tre_ring = &channel->tre_ring;
	u32 channel_id = gsi_channel_id(channel);
	struct gsi *gsi = channel->gsi;
	const struct reg *reg;
	u32 val;

	reg = gsi_reg(gsi, CH_C_DOORBELL_0);
	/* Note: index *must* be used modulo the ring count here */
	val = gsi_ring_addr(tre_ring, tre_ring->index % tre_ring->count);
	iowrite32(val, gsi->virt + reg_n_offset(reg, channel_id));
}

/* Consult hardware, move newly completed transactions to completed state */
void gsi_channel_update(struct gsi_channel *channel)
{
	u32 evt_ring_id = channel->evt_ring_id;
	struct gsi *gsi = channel->gsi;
	struct gsi_evt_ring *evt_ring;
	struct gsi_trans *trans;
	struct gsi_ring *ring;
	const struct reg *reg;
	u32 offset;
	u32 index;

	evt_ring = &gsi->evt_ring[evt_ring_id];
	ring = &evt_ring->ring;

	/* See if there's anything new to process; if not, we're done.  Note
	 * that index always refers to an entry *within* the event ring.
	 */
	reg = gsi_reg(gsi, EV_CH_E_CNTXT_4);
	offset = reg_n_offset(reg, evt_ring_id);
	index = gsi_ring_index(ring, ioread32(gsi->virt + offset));
	if (index == ring->index % ring->count)
		return;

	/* Get the transaction for the latest completed event. */
	trans = gsi_event_trans(gsi, gsi_ring_virt(ring, index - 1));
	if (!trans)
		return;

	/* For RX channels, update each completed transaction with the number
	 * of bytes that were actually received.  For TX channels, report
	 * the number of transactions and bytes this completion represents
	 * up the network stack.
	 */
	gsi_evt_ring_update(gsi, evt_ring_id, index);
}

/**
 * gsi_channel_poll_one() - Return a single completed transaction on a channel
 * @channel:	Channel to be polled
 *
 * Return:	Transaction pointer, or null if none are available
 *
 * This function returns the first of a channel's completed transactions.
 * If no transactions are in completed state, the hardware is consulted to
 * determine whether any new transactions have completed.  If so, they're
 * moved to completed state and the first such transaction is returned.
 * If there are no more completed transactions, a null pointer is returned.
 */
static struct gsi_trans *gsi_channel_poll_one(struct gsi_channel *channel)
{
	struct gsi_trans *trans;

	/* Get the first completed transaction */
	trans = gsi_channel_trans_complete(channel);
	if (trans)
		gsi_trans_move_polled(trans);

	return trans;
}

/**
 * gsi_channel_poll() - NAPI poll function for a channel
 * @napi:	NAPI structure for the channel
 * @budget:	Budget supplied by NAPI core
 *
 * Return:	Number of items polled (<= budget)
 *
 * Single transactions completed by hardware are polled until either
 * the budget is exhausted, or there are no more.  Each transaction
 * polled is passed to gsi_trans_complete(), to perform remaining
 * completion processing and retire/free the transaction.
 */
static int gsi_channel_poll(struct napi_struct *napi, int budget)
{
	struct gsi_channel *channel;
	int count;

	channel = container_of(napi, struct gsi_channel, napi);
	for (count = 0; count < budget; count++) {
		struct gsi_trans *trans;

		trans = gsi_channel_poll_one(channel);
		if (!trans)
			break;
		gsi_trans_complete(trans);
	}

	if (count < budget && napi_complete(napi))
		gsi_irq_ieob_enable_one(channel->gsi, channel->evt_ring_id);

	return count;
}

/* The event bitmap represents which event ids are available for allocation.
 * Set bits are not available, clear bits can be used.  This function
 * initializes the map so all events supported by the hardware are available,
 * then precludes any reserved events from being allocated.
 */
static u32 gsi_event_bitmap_init(u32 evt_ring_max)
{
	u32 event_bitmap = GENMASK(BITS_PER_LONG - 1, evt_ring_max);

	event_bitmap |= GENMASK(GSI_MHI_EVENT_ID_END, GSI_MHI_EVENT_ID_START);

	return event_bitmap;
}

/* Setup function for a single channel */
static int gsi_channel_setup_one(struct gsi *gsi, u32 channel_id)
{
	struct gsi_channel *channel = &gsi->channel[channel_id];
	u32 evt_ring_id = channel->evt_ring_id;
	int ret;

	if (!gsi_channel_initialized(channel))
		return 0;

	ret = gsi_evt_ring_alloc_command(gsi, evt_ring_id);
	if (ret)
		return ret;

	gsi_evt_ring_program(gsi, evt_ring_id);

	ret = gsi_channel_alloc_command(gsi, channel_id);
	if (ret)
		goto err_evt_ring_de_alloc;

	gsi_channel_program(channel, true);

	if (channel->toward_ipa)
		netif_napi_add_tx(gsi->dummy_dev, &channel->napi,
				  gsi_channel_poll);
	else
		netif_napi_add(gsi->dummy_dev, &channel->napi,
			       gsi_channel_poll);

	return 0;

err_evt_ring_de_alloc:
	/* We've done nothing with the event ring yet so don't reset */
	gsi_evt_ring_de_alloc_command(gsi, evt_ring_id);

	return ret;
}

/* Inverse of gsi_channel_setup_one() */
static void gsi_channel_teardown_one(struct gsi *gsi, u32 channel_id)
{
	struct gsi_channel *channel = &gsi->channel[channel_id];
	u32 evt_ring_id = channel->evt_ring_id;

	if (!gsi_channel_initialized(channel))
		return;

	netif_napi_del(&channel->napi);

	gsi_channel_de_alloc_command(gsi, channel_id);
	gsi_evt_ring_reset_command(gsi, evt_ring_id);
	gsi_evt_ring_de_alloc_command(gsi, evt_ring_id);
}

/* We use generic commands only to operate on modem channels.  We don't have
 * the ability to determine channel state for a modem channel, so we simply
 * issue the command and wait for it to complete.
 */
static int gsi_generic_command(struct gsi *gsi, u32 channel_id,
			       enum gsi_generic_cmd_opcode opcode,
			       u8 params)
{
	const struct reg *reg;
	bool timeout;
	u32 offset;
	u32 val;

	/* The error global interrupt type is always enabled (until we tear
	 * down), so we will keep it enabled.
	 *
	 * A generic EE command completes with a GSI global interrupt of
	 * type GP_INT1.  We only perform one generic command at a time
	 * (to allocate, halt, or enable/disable flow control on a modem
	 * channel), and only from this function.  So we enable the GP_INT1
	 * IRQ type here, and disable it again after the command completes.
	 */
	reg = gsi_reg(gsi, CNTXT_GLOB_IRQ_EN);
	val = ERROR_INT | GP_INT1;
	iowrite32(val, gsi->virt + reg_offset(reg));

	/* First zero the result code field */
	reg = gsi_reg(gsi, CNTXT_SCRATCH_0);
	offset = reg_offset(reg);
	val = ioread32(gsi->virt + offset);

	val &= ~reg_fmask(reg, GENERIC_EE_RESULT);
	iowrite32(val, gsi->virt + offset);

	/* Now issue the command */
	reg = gsi_reg(gsi, GENERIC_CMD);
	val = reg_encode(reg, GENERIC_OPCODE, opcode);
	val |= reg_encode(reg, GENERIC_CHID, channel_id);
	val |= reg_encode(reg, GENERIC_EE, GSI_EE_MODEM);
	if (gsi->version >= IPA_VERSION_4_11)
		val |= reg_encode(reg, GENERIC_PARAMS, params);

	timeout = !gsi_command(gsi, reg_offset(reg), val);

	/* Disable the GP_INT1 IRQ type again */
	reg = gsi_reg(gsi, CNTXT_GLOB_IRQ_EN);
	iowrite32(ERROR_INT, gsi->virt + reg_offset(reg));

	if (!timeout)
		return gsi->result;

	dev_err(gsi->dev, "GSI generic command %u to channel %u timed out\n",
		opcode, channel_id);

	return -ETIMEDOUT;
}

static int gsi_modem_channel_alloc(struct gsi *gsi, u32 channel_id)
{
	return gsi_generic_command(gsi, channel_id,
				   GSI_GENERIC_ALLOCATE_CHANNEL, 0);
}

static void gsi_modem_channel_halt(struct gsi *gsi, u32 channel_id)
{
	u32 retries = GSI_CHANNEL_MODEM_HALT_RETRIES;
	int ret;

	do
		ret = gsi_generic_command(gsi, channel_id,
					  GSI_GENERIC_HALT_CHANNEL, 0);
	while (ret == -EAGAIN && retries--);

	if (ret)
		dev_err(gsi->dev, "error %d halting modem channel %u\n",
			ret, channel_id);
}

/* Enable or disable flow control for a modem GSI TX channel (IPA v4.2+) */
void
gsi_modem_channel_flow_control(struct gsi *gsi, u32 channel_id, bool enable)
{
	u32 retries = 0;
	u32 command;
	int ret;

	command = enable ? GSI_GENERIC_ENABLE_FLOW_CONTROL
			 : GSI_GENERIC_DISABLE_FLOW_CONTROL;
	/* Disabling flow control on IPA v4.11+ can return -EAGAIN if enable
	 * is underway.  In this case we need to retry the command.
	 */
	if (!enable && gsi->version >= IPA_VERSION_4_11)
		retries = GSI_CHANNEL_MODEM_FLOW_RETRIES;

	do
		ret = gsi_generic_command(gsi, channel_id, command, 0);
	while (ret == -EAGAIN && retries--);

	if (ret)
		dev_err(gsi->dev,
			"error %d %sabling mode channel %u flow control\n",
			ret, enable ? "en" : "dis", channel_id);
}

/* Setup function for channels */
static int gsi_channel_setup(struct gsi *gsi)
{
	u32 channel_id = 0;
	u32 mask;
	int ret;

	gsi_irq_enable(gsi);

	mutex_lock(&gsi->mutex);

	do {
		ret = gsi_channel_setup_one(gsi, channel_id);
		if (ret)
			goto err_unwind;
	} while (++channel_id < gsi->channel_count);

	/* Make sure no channels were defined that hardware does not support */
	while (channel_id < GSI_CHANNEL_COUNT_MAX) {
		struct gsi_channel *channel = &gsi->channel[channel_id++];

		if (!gsi_channel_initialized(channel))
			continue;

		ret = -EINVAL;
		dev_err(gsi->dev, "channel %u not supported by hardware\n",
			channel_id - 1);
		channel_id = gsi->channel_count;
		goto err_unwind;
	}

	/* Allocate modem channels if necessary */
	mask = gsi->modem_channel_bitmap;
	while (mask) {
		u32 modem_channel_id = __ffs(mask);

		ret = gsi_modem_channel_alloc(gsi, modem_channel_id);
		if (ret)
			goto err_unwind_modem;

		/* Clear bit from mask only after success (for unwind) */
		mask ^= BIT(modem_channel_id);
	}

	mutex_unlock(&gsi->mutex);

	return 0;

err_unwind_modem:
	/* Compute which modem channels need to be deallocated */
	mask ^= gsi->modem_channel_bitmap;
	while (mask) {
		channel_id = __fls(mask);

		mask ^= BIT(channel_id);

		gsi_modem_channel_halt(gsi, channel_id);
	}

err_unwind:
	while (channel_id--)
		gsi_channel_teardown_one(gsi, channel_id);

	mutex_unlock(&gsi->mutex);

	gsi_irq_disable(gsi);

	return ret;
}

/* Inverse of gsi_channel_setup() */
static void gsi_channel_teardown(struct gsi *gsi)
{
	u32 mask = gsi->modem_channel_bitmap;
	u32 channel_id;

	mutex_lock(&gsi->mutex);

	while (mask) {
		channel_id = __fls(mask);

		mask ^= BIT(channel_id);

		gsi_modem_channel_halt(gsi, channel_id);
	}

	channel_id = gsi->channel_count - 1;
	do
		gsi_channel_teardown_one(gsi, channel_id);
	while (channel_id--);

	mutex_unlock(&gsi->mutex);

	gsi_irq_disable(gsi);
}

/* Turn off all GSI interrupts initially */
static int gsi_irq_setup(struct gsi *gsi)
{
	const struct reg *reg;
	int ret;

	/* Writing 1 indicates IRQ interrupts; 0 would be MSI */
	reg = gsi_reg(gsi, CNTXT_INTSET);
	iowrite32(reg_bit(reg, INTYPE), gsi->virt + reg_offset(reg));

	/* Disable all interrupt types */
	gsi_irq_type_update(gsi, 0);

	/* Clear all type-specific interrupt masks */
	reg = gsi_reg(gsi, CNTXT_SRC_CH_IRQ_MSK);
	iowrite32(0, gsi->virt + reg_offset(reg));

	reg = gsi_reg(gsi, CNTXT_SRC_EV_CH_IRQ_MSK);
	iowrite32(0, gsi->virt + reg_offset(reg));

	reg = gsi_reg(gsi, CNTXT_GLOB_IRQ_EN);
	iowrite32(0, gsi->virt + reg_offset(reg));

	reg = gsi_reg(gsi, CNTXT_SRC_IEOB_IRQ_MSK);
	iowrite32(0, gsi->virt + reg_offset(reg));

	/* The inter-EE interrupts are not supported for IPA v3.0-v3.1 */
	if (gsi->version > IPA_VERSION_3_1) {
		reg = gsi_reg(gsi, INTER_EE_SRC_CH_IRQ_MSK);
		iowrite32(0, gsi->virt + reg_offset(reg));

		reg = gsi_reg(gsi, INTER_EE_SRC_EV_CH_IRQ_MSK);
		iowrite32(0, gsi->virt + reg_offset(reg));
	}

	reg = gsi_reg(gsi, CNTXT_GSI_IRQ_EN);
	iowrite32(0, gsi->virt + reg_offset(reg));

	ret = request_irq(gsi->irq, gsi_isr, 0, "gsi", gsi);
	if (ret)
		dev_err(gsi->dev, "error %d requesting \"gsi\" IRQ\n", ret);

	return ret;
}

static void gsi_irq_teardown(struct gsi *gsi)
{
	free_irq(gsi->irq, gsi);
}

/* Get # supported channel and event rings; there is no gsi_ring_teardown() */
static int gsi_ring_setup(struct gsi *gsi)
{
	struct device *dev = gsi->dev;
	const struct reg *reg;
	u32 count;
	u32 val;

	if (gsi->version < IPA_VERSION_3_5_1) {
		/* No HW_PARAM_2 register prior to IPA v3.5.1, assume the max */
		gsi->channel_count = GSI_CHANNEL_COUNT_MAX;
		gsi->evt_ring_count = GSI_EVT_RING_COUNT_MAX;

		return 0;
	}

	reg = gsi_reg(gsi, HW_PARAM_2);
	val = ioread32(gsi->virt + reg_offset(reg));

	count = reg_decode(reg, NUM_CH_PER_EE, val);
	if (!count) {
		dev_err(dev, "GSI reports zero channels supported\n");
		return -EINVAL;
	}
	if (count > GSI_CHANNEL_COUNT_MAX) {
		dev_warn(dev, "limiting to %u channels; hardware supports %u\n",
			 GSI_CHANNEL_COUNT_MAX, count);
		count = GSI_CHANNEL_COUNT_MAX;
	}
	gsi->channel_count = count;

	if (gsi->version < IPA_VERSION_5_0) {
		count = reg_decode(reg, NUM_EV_PER_EE, val);
	} else {
		reg = gsi_reg(gsi, HW_PARAM_4);
		count = reg_decode(reg, EV_PER_EE, val);
	}
	if (!count) {
		dev_err(dev, "GSI reports zero event rings supported\n");
		return -EINVAL;
	}
	if (count > GSI_EVT_RING_COUNT_MAX) {
		dev_warn(dev,
			 "limiting to %u event rings; hardware supports %u\n",
			 GSI_EVT_RING_COUNT_MAX, count);
		count = GSI_EVT_RING_COUNT_MAX;
	}
	gsi->evt_ring_count = count;

	return 0;
}

/* Setup function for GSI.  GSI firmware must be loaded and initialized */
int gsi_setup(struct gsi *gsi)
{
	const struct reg *reg;
	u32 val;
	int ret;

	/* Here is where we first touch the GSI hardware */
	reg = gsi_reg(gsi, GSI_STATUS);
	val = ioread32(gsi->virt + reg_offset(reg));
	if (!(val & reg_bit(reg, ENABLED))) {
		dev_err(gsi->dev, "GSI has not been enabled\n");
		return -EIO;
	}

	ret = gsi_irq_setup(gsi);
	if (ret)
		return ret;

	ret = gsi_ring_setup(gsi);	/* No matching teardown required */
	if (ret)
		goto err_irq_teardown;

	/* Initialize the error log */
	reg = gsi_reg(gsi, ERROR_LOG);
	iowrite32(0, gsi->virt + reg_offset(reg));

	ret = gsi_channel_setup(gsi);
	if (ret)
		goto err_irq_teardown;

	return 0;

err_irq_teardown:
	gsi_irq_teardown(gsi);

	return ret;
}

/* Inverse of gsi_setup() */
void gsi_teardown(struct gsi *gsi)
{
	gsi_channel_teardown(gsi);
	gsi_irq_teardown(gsi);
}

/* Initialize a channel's event ring */
static int gsi_channel_evt_ring_init(struct gsi_channel *channel)
{
	struct gsi *gsi = channel->gsi;
	struct gsi_evt_ring *evt_ring;
	int ret;

	ret = gsi_evt_ring_id_alloc(gsi);
	if (ret < 0)
		return ret;
	channel->evt_ring_id = ret;

	evt_ring = &gsi->evt_ring[channel->evt_ring_id];
	evt_ring->channel = channel;

	ret = gsi_ring_alloc(gsi, &evt_ring->ring, channel->event_count);
	if (!ret)
		return 0;	/* Success! */

	dev_err(gsi->dev, "error %d allocating channel %u event ring\n",
		ret, gsi_channel_id(channel));

	gsi_evt_ring_id_free(gsi, channel->evt_ring_id);

	return ret;
}

/* Inverse of gsi_channel_evt_ring_init() */
static void gsi_channel_evt_ring_exit(struct gsi_channel *channel)
{
	u32 evt_ring_id = channel->evt_ring_id;
	struct gsi *gsi = channel->gsi;
	struct gsi_evt_ring *evt_ring;

	evt_ring = &gsi->evt_ring[evt_ring_id];
	gsi_ring_free(gsi, &evt_ring->ring);
	gsi_evt_ring_id_free(gsi, evt_ring_id);
}

static bool gsi_channel_data_valid(struct gsi *gsi, bool command,
				   const struct ipa_gsi_endpoint_data *data)
{
	const struct gsi_channel_data *channel_data;
	u32 channel_id = data->channel_id;
	struct device *dev = gsi->dev;

	/* Make sure channel ids are in the range driver supports */
	if (channel_id >= GSI_CHANNEL_COUNT_MAX) {
		dev_err(dev, "bad channel id %u; must be less than %u\n",
			channel_id, GSI_CHANNEL_COUNT_MAX);
		return false;
	}

	if (data->ee_id != GSI_EE_AP && data->ee_id != GSI_EE_MODEM) {
		dev_err(dev, "bad EE id %u; not AP or modem\n", data->ee_id);
		return false;
	}

	if (command && !data->toward_ipa) {
		dev_err(dev, "command channel %u is not TX\n", channel_id);
		return false;
	}

	channel_data = &data->channel;

	if (!channel_data->tlv_count ||
	    channel_data->tlv_count > GSI_TLV_MAX) {
		dev_err(dev, "channel %u bad tlv_count %u; must be 1..%u\n",
			channel_id, channel_data->tlv_count, GSI_TLV_MAX);
		return false;
	}

	if (command && IPA_COMMAND_TRANS_TRE_MAX > channel_data->tlv_count) {
		dev_err(dev, "command TRE max too big for channel %u (%u > %u)\n",
			channel_id, IPA_COMMAND_TRANS_TRE_MAX,
			channel_data->tlv_count);
		return false;
	}

	/* We have to allow at least one maximally-sized transaction to
	 * be outstanding (which would use tlv_count TREs).  Given how
	 * gsi_channel_tre_max() is computed, tre_count has to be almost
	 * twice the TLV FIFO size to satisfy this requirement.
	 */
	if (channel_data->tre_count < 2 * channel_data->tlv_count - 1) {
		dev_err(dev, "channel %u TLV count %u exceeds TRE count %u\n",
			channel_id, channel_data->tlv_count,
			channel_data->tre_count);
		return false;
	}

	if (!is_power_of_2(channel_data->tre_count)) {
		dev_err(dev, "channel %u bad tre_count %u; not power of 2\n",
			channel_id, channel_data->tre_count);
		return false;
	}

	if (!is_power_of_2(channel_data->event_count)) {
		dev_err(dev, "channel %u bad event_count %u; not power of 2\n",
			channel_id, channel_data->event_count);
		return false;
	}

	return true;
}

/* Init function for a single channel */
static int gsi_channel_init_one(struct gsi *gsi,
				const struct ipa_gsi_endpoint_data *data,
				bool command)
{
	struct gsi_channel *channel;
	u32 tre_count;
	int ret;

	if (!gsi_channel_data_valid(gsi, command, data))
		return -EINVAL;

	/* Worst case we need an event for every outstanding TRE */
	if (data->channel.tre_count > data->channel.event_count) {
		tre_count = data->channel.event_count;
		dev_warn(gsi->dev, "channel %u limited to %u TREs\n",
			 data->channel_id, tre_count);
	} else {
		tre_count = data->channel.tre_count;
	}

	channel = &gsi->channel[data->channel_id];
	memset(channel, 0, sizeof(*channel));

	channel->gsi = gsi;
	channel->toward_ipa = data->toward_ipa;
	channel->command = command;
	channel->trans_tre_max = data->channel.tlv_count;
	channel->tre_count = tre_count;
	channel->event_count = data->channel.event_count;

	ret = gsi_channel_evt_ring_init(channel);
	if (ret)
		goto err_clear_gsi;

	ret = gsi_ring_alloc(gsi, &channel->tre_ring, data->channel.tre_count);
	if (ret) {
		dev_err(gsi->dev, "error %d allocating channel %u ring\n",
			ret, data->channel_id);
		goto err_channel_evt_ring_exit;
	}

	ret = gsi_channel_trans_init(gsi, data->channel_id);
	if (ret)
		goto err_ring_free;

	if (command) {
		u32 tre_max = gsi_channel_tre_max(gsi, data->channel_id);

		ret = ipa_cmd_pool_init(channel, tre_max);
	}
	if (!ret)
		return 0;	/* Success! */

	gsi_channel_trans_exit(channel);
err_ring_free:
	gsi_ring_free(gsi, &channel->tre_ring);
err_channel_evt_ring_exit:
	gsi_channel_evt_ring_exit(channel);
err_clear_gsi:
	channel->gsi = NULL;	/* Mark it not (fully) initialized */

	return ret;
}

/* Inverse of gsi_channel_init_one() */
static void gsi_channel_exit_one(struct gsi_channel *channel)
{
	if (!gsi_channel_initialized(channel))
		return;

	if (channel->command)
		ipa_cmd_pool_exit(channel);
	gsi_channel_trans_exit(channel);
	gsi_ring_free(channel->gsi, &channel->tre_ring);
	gsi_channel_evt_ring_exit(channel);
}

/* Init function for channels */
static int gsi_channel_init(struct gsi *gsi, u32 count,
			    const struct ipa_gsi_endpoint_data *data)
{
	bool modem_alloc;
	int ret = 0;
	u32 i;

	/* IPA v4.2 requires the AP to allocate channels for the modem */
	modem_alloc = gsi->version == IPA_VERSION_4_2;

	gsi->event_bitmap = gsi_event_bitmap_init(GSI_EVT_RING_COUNT_MAX);
	gsi->ieob_enabled_bitmap = 0;

	/* The endpoint data array is indexed by endpoint name */
	for (i = 0; i < count; i++) {
		bool command = i == IPA_ENDPOINT_AP_COMMAND_TX;

		if (ipa_gsi_endpoint_data_empty(&data[i]))
			continue;	/* Skip over empty slots */

		/* Mark modem channels to be allocated (hardware workaround) */
		if (data[i].ee_id == GSI_EE_MODEM) {
			if (modem_alloc)
				gsi->modem_channel_bitmap |=
						BIT(data[i].channel_id);
			continue;
		}

		ret = gsi_channel_init_one(gsi, &data[i], command);
		if (ret)
			goto err_unwind;
	}

	return ret;

err_unwind:
	while (i--) {
		if (ipa_gsi_endpoint_data_empty(&data[i]))
			continue;
		if (modem_alloc && data[i].ee_id == GSI_EE_MODEM) {
			gsi->modem_channel_bitmap &= ~BIT(data[i].channel_id);
			continue;
		}
		gsi_channel_exit_one(&gsi->channel[data->channel_id]);
	}

	return ret;
}

/* Inverse of gsi_channel_init() */
static void gsi_channel_exit(struct gsi *gsi)
{
	u32 channel_id = GSI_CHANNEL_COUNT_MAX - 1;

	do
		gsi_channel_exit_one(&gsi->channel[channel_id]);
	while (channel_id--);
	gsi->modem_channel_bitmap = 0;
}

/* Init function for GSI.  GSI hardware does not need to be "ready" */
int gsi_init(struct gsi *gsi, struct platform_device *pdev,
	     enum ipa_version version, u32 count,
	     const struct ipa_gsi_endpoint_data *data)
{
	int ret;

	gsi_validate_build();

	gsi->dev = &pdev->dev;
	gsi->version = version;

	/* GSI uses NAPI on all channels.  Create a dummy network device
	 * for the channel NAPI contexts to be associated with.
	 */
	gsi->dummy_dev = alloc_netdev_dummy(0);
	if (!gsi->dummy_dev)
		return -ENOMEM;
	init_completion(&gsi->completion);

	ret = gsi_reg_init(gsi, pdev);
	if (ret)
		goto err_reg_exit;

	ret = gsi_irq_init(gsi, pdev);	/* No matching exit required */
	if (ret)
		goto err_reg_exit;

	ret = gsi_channel_init(gsi, count, data);
	if (ret)
		goto err_reg_exit;

	mutex_init(&gsi->mutex);

	return 0;

err_reg_exit:
	free_netdev(gsi->dummy_dev);
	gsi_reg_exit(gsi);

	return ret;
}

/* Inverse of gsi_init() */
void gsi_exit(struct gsi *gsi)
{
	mutex_destroy(&gsi->mutex);
	gsi_channel_exit(gsi);
	free_netdev(gsi->dummy_dev);
	gsi_reg_exit(gsi);
}

/* The maximum number of outstanding TREs on a channel.  This limits
 * a channel's maximum number of transactions outstanding (worst case
 * is one TRE per transaction).
 *
 * The absolute limit is the number of TREs in the channel's TRE ring,
 * and in theory we should be able use all of them.  But in practice,
 * doing that led to the hardware reporting exhaustion of event ring
 * slots for writing completion information.  So the hardware limit
 * would be (tre_count - 1).
 *
 * We reduce it a bit further though.  Transaction resource pools are
 * sized to be a little larger than this maximum, to allow resource
 * allocations to always be contiguous.  The number of entries in a
 * TRE ring buffer is a power of 2, and the extra resources in a pool
 * tends to nearly double the memory allocated for it.  Reducing the
 * maximum number of outstanding TREs allows the number of entries in
 * a pool to avoid crossing that power-of-2 boundary, and this can
 * substantially reduce pool memory requirements.  The number we
 * reduce it by matches the number added in gsi_trans_pool_init().
 */
u32 gsi_channel_tre_max(struct gsi *gsi, u32 channel_id)
{
	struct gsi_channel *channel = &gsi->channel[channel_id];

	/* Hardware limit is channel->tre_count - 1 */
	return channel->tre_count - (channel->trans_tre_max - 1);
}
