// SPDX-License-Identifier: GPL-2.0
/*
 * Counter driver for the ACCES 104-QUAD-8
 * Copyright (C) 2016 William Breathitt Gray
 *
 * This driver supports the ACCES 104-QUAD-8 and ACCES 104-QUAD-4.
 */
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/counter.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/isa.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/spinlock.h>

#define QUAD8_EXTENT 32

static unsigned int base[max_num_isa_dev(QUAD8_EXTENT)];
static unsigned int num_quad8;
module_param_hw_array(base, uint, ioport, &num_quad8, 0);
MODULE_PARM_DESC(base, "ACCES 104-QUAD-8 base addresses");

static unsigned int irq[max_num_isa_dev(QUAD8_EXTENT)];
static unsigned int num_irq;
module_param_hw_array(irq, uint, irq, &num_irq, 0);
MODULE_PARM_DESC(irq, "ACCES 104-QUAD-8 interrupt line numbers");

#define QUAD8_NUM_COUNTERS 8

/**
 * struct channel_reg - channel register structure
 * @data:	Count data
 * @control:	Channel flags and control
 */
struct channel_reg {
	u8 data;
	u8 control;
};

/**
 * struct quad8_reg - device register structure
 * @channel:		quadrature counter data and control
 * @interrupt_status:	channel interrupt status
 * @channel_oper:	enable/reset counters and interrupt functions
 * @index_interrupt:	enable channel interrupts
 * @reserved:		reserved for Factory Use
 * @index_input_levels:	index signal logical input level
 * @cable_status:	differential encoder cable status
 */
struct quad8_reg {
	struct channel_reg channel[QUAD8_NUM_COUNTERS];
	u8 interrupt_status;
	u8 channel_oper;
	u8 index_interrupt;
	u8 reserved[3];
	u8 index_input_levels;
	u8 cable_status;
};

/**
 * struct quad8 - device private data structure
 * @lock:		lock to prevent clobbering device states during R/W ops
 * @counter:		instance of the counter_device
 * @fck_prescaler:	array of filter clock prescaler configurations
 * @preset:		array of preset values
 * @count_mode:		array of count mode configurations
 * @quadrature_mode:	array of quadrature mode configurations
 * @quadrature_scale:	array of quadrature mode scale configurations
 * @ab_enable:		array of A and B inputs enable configurations
 * @preset_enable:	array of set_to_preset_on_index attribute configurations
 * @irq_trigger:	array of current IRQ trigger function configurations
 * @synchronous_mode:	array of index function synchronous mode configurations
 * @index_polarity:	array of index function polarity configurations
 * @cable_fault_enable:	differential encoder cable status enable configurations
 * @reg:		I/O address offset for the device registers
 */
struct quad8 {
	spinlock_t lock;
	unsigned int fck_prescaler[QUAD8_NUM_COUNTERS];
	unsigned int preset[QUAD8_NUM_COUNTERS];
	unsigned int count_mode[QUAD8_NUM_COUNTERS];
	unsigned int quadrature_mode[QUAD8_NUM_COUNTERS];
	unsigned int quadrature_scale[QUAD8_NUM_COUNTERS];
	unsigned int ab_enable[QUAD8_NUM_COUNTERS];
	unsigned int preset_enable[QUAD8_NUM_COUNTERS];
	unsigned int irq_trigger[QUAD8_NUM_COUNTERS];
	unsigned int synchronous_mode[QUAD8_NUM_COUNTERS];
	unsigned int index_polarity[QUAD8_NUM_COUNTERS];
	unsigned int cable_fault_enable;
	struct quad8_reg __iomem *reg;
};

/* Error flag */
#define FLAG_E BIT(4)
/* Up/Down flag */
#define FLAG_UD BIT(5)

#define REGISTER_SELECTION GENMASK(6, 5)

/* Reset and Load Signal Decoders */
#define SELECT_RLD u8_encode_bits(0x0, REGISTER_SELECTION)
/* Counter Mode Register */
#define SELECT_CMR u8_encode_bits(0x1, REGISTER_SELECTION)
/* Input / Output Control Register */
#define SELECT_IOR u8_encode_bits(0x2, REGISTER_SELECTION)
/* Index Control Register */
#define SELECT_IDR u8_encode_bits(0x3, REGISTER_SELECTION)

/*
 * Reset and Load Signal Decoders
 */
#define RESETS GENMASK(2, 1)
#define LOADS GENMASK(4, 3)
/* Reset Byte Pointer (three byte data pointer) */
#define RESET_BP BIT(0)
/* Reset Borrow Toggle, Carry toggle, Compare toggle, Sign, and Index flags */
#define RESET_BT_CT_CPT_S_IDX u8_encode_bits(0x2, RESETS)
/* Reset Error flag */
#define RESET_E u8_encode_bits(0x3, RESETS)
/* Preset Register to Counter */
#define TRANSFER_PR_TO_CNTR u8_encode_bits(0x1, LOADS)
/* Transfer Counter to Output Latch */
#define TRANSFER_CNTR_TO_OL u8_encode_bits(0x2, LOADS)
/* Transfer Preset Register LSB to FCK Prescaler */
#define TRANSFER_PR0_TO_PSC u8_encode_bits(0x3, LOADS)

/*
 * Counter Mode Registers
 */
#define COUNT_ENCODING BIT(0)
#define COUNT_MODE GENMASK(2, 1)
#define QUADRATURE_MODE GENMASK(4, 3)
/* Binary count */
#define BINARY u8_encode_bits(0x0, COUNT_ENCODING)
/* Normal count */
#define NORMAL_COUNT 0x0
/* Range Limit */
#define RANGE_LIMIT 0x1
/* Non-recycle count */
#define NON_RECYCLE_COUNT 0x2
/* Modulo-N */
#define MODULO_N 0x3
/* Non-quadrature */
#define NON_QUADRATURE 0x0
/* Quadrature X1 */
#define QUADRATURE_X1 0x1
/* Quadrature X2 */
#define QUADRATURE_X2 0x2
/* Quadrature X4 */
#define QUADRATURE_X4 0x3

/*
 * Input/Output Control Register
 */
#define AB_GATE BIT(0)
#define LOAD_PIN BIT(1)
#define FLG_PINS GENMASK(4, 3)
/* Disable inputs A and B */
#define DISABLE_AB u8_encode_bits(0x0, AB_GATE)
/* Load Counter input */
#define LOAD_CNTR 0x0
/* FLG1 = CARRY(active low); FLG2 = BORROW(active low) */
#define FLG1_CARRY_FLG2_BORROW 0x0
/* FLG1 = COMPARE(active low); FLG2 = BORROW(active low) */
#define FLG1_COMPARE_FLG2_BORROW 0x1
/* FLG1 = Carry(active low)/Borrow(active low); FLG2 = U/D(active low) flag */
#define FLG1_CARRYBORROW_FLG2_UD 0x2
/* FLG1 = INDX (low pulse at INDEX pin active level); FLG2 = E flag */
#define FLG1_INDX_FLG2_E 0x3

/*
 * INDEX CONTROL REGISTERS
 */
#define INDEX_MODE BIT(0)
#define INDEX_POLARITY BIT(1)
/* Disable Index mode */
#define DISABLE_INDEX_MODE 0x0
/* Negative Index Polarity */
#define NEGATIVE_INDEX_POLARITY 0x0

/*
 * Channel Operation Register
 */
#define COUNTERS_OPERATION BIT(0)
#define INTERRUPT_FUNCTION BIT(2)
/* Enable all Counters */
#define ENABLE_COUNTERS u8_encode_bits(0x0, COUNTERS_OPERATION)
/* Reset all Counters */
#define RESET_COUNTERS u8_encode_bits(0x1, COUNTERS_OPERATION)
/* Disable the interrupt function */
#define DISABLE_INTERRUPT_FUNCTION u8_encode_bits(0x0, INTERRUPT_FUNCTION)
/* Enable the interrupt function */
#define ENABLE_INTERRUPT_FUNCTION u8_encode_bits(0x1, INTERRUPT_FUNCTION)
/* Any write to the Channel Operation register clears any pending interrupts */
#define CLEAR_PENDING_INTERRUPTS (ENABLE_COUNTERS | ENABLE_INTERRUPT_FUNCTION)

/* Each Counter is 24 bits wide */
#define LS7267_CNTR_MAX GENMASK(23, 0)

static int quad8_signal_read(struct counter_device *counter,
			     struct counter_signal *signal,
			     enum counter_signal_level *level)
{
	const struct quad8 *const priv = counter_priv(counter);
	unsigned int state;

	/* Only Index signal levels can be read */
	if (signal->id < 16)
		return -EINVAL;

	state = ioread8(&priv->reg->index_input_levels) & BIT(signal->id - 16);

	*level = (state) ? COUNTER_SIGNAL_LEVEL_HIGH : COUNTER_SIGNAL_LEVEL_LOW;

	return 0;
}

static int quad8_count_read(struct counter_device *counter,
			    struct counter_count *count, u64 *val)
{
	struct quad8 *const priv = counter_priv(counter);
	struct channel_reg __iomem *const chan = priv->reg->channel + count->id;
	unsigned long irqflags;
	int i;

	*val = 0;

	spin_lock_irqsave(&priv->lock, irqflags);

	iowrite8(SELECT_RLD | RESET_BP | TRANSFER_CNTR_TO_OL, &chan->control);

	for (i = 0; i < 3; i++)
		*val |= (unsigned long)ioread8(&chan->data) << (8 * i);

	spin_unlock_irqrestore(&priv->lock, irqflags);

	return 0;
}

static int quad8_count_write(struct counter_device *counter,
			     struct counter_count *count, u64 val)
{
	struct quad8 *const priv = counter_priv(counter);
	struct channel_reg __iomem *const chan = priv->reg->channel + count->id;
	unsigned long irqflags;
	int i;

	if (val > LS7267_CNTR_MAX)
		return -ERANGE;

	spin_lock_irqsave(&priv->lock, irqflags);

	iowrite8(SELECT_RLD | RESET_BP, &chan->control);

	/* Counter can only be set via Preset Register */
	for (i = 0; i < 3; i++)
		iowrite8(val >> (8 * i), &chan->data);

	iowrite8(SELECT_RLD | TRANSFER_PR_TO_CNTR, &chan->control);

	iowrite8(SELECT_RLD | RESET_BP, &chan->control);

	/* Set Preset Register back to original value */
	val = priv->preset[count->id];
	for (i = 0; i < 3; i++)
		iowrite8(val >> (8 * i), &chan->data);

	iowrite8(SELECT_RLD | RESET_BT_CT_CPT_S_IDX, &chan->control);
	iowrite8(SELECT_RLD | RESET_E, &chan->control);

	spin_unlock_irqrestore(&priv->lock, irqflags);

	return 0;
}

static const enum counter_function quad8_count_functions_list[] = {
	COUNTER_FUNCTION_PULSE_DIRECTION,
	COUNTER_FUNCTION_QUADRATURE_X1_A,
	COUNTER_FUNCTION_QUADRATURE_X2_A,
	COUNTER_FUNCTION_QUADRATURE_X4,
};

static int quad8_function_get(const struct quad8 *const priv, const size_t id,
			      enum counter_function *const function)
{
	if (!priv->quadrature_mode[id]) {
		*function = COUNTER_FUNCTION_PULSE_DIRECTION;
		return 0;
	}

	switch (priv->quadrature_scale[id]) {
	case 0:
		*function = COUNTER_FUNCTION_QUADRATURE_X1_A;
		return 0;
	case 1:
		*function = COUNTER_FUNCTION_QUADRATURE_X2_A;
		return 0;
	case 2:
		*function = COUNTER_FUNCTION_QUADRATURE_X4;
		return 0;
	default:
		/* should never reach this path */
		return -EINVAL;
	}
}

static int quad8_function_read(struct counter_device *counter,
			       struct counter_count *count,
			       enum counter_function *function)
{
	struct quad8 *const priv = counter_priv(counter);
	unsigned long irqflags;
	int retval;

	spin_lock_irqsave(&priv->lock, irqflags);

	retval = quad8_function_get(priv, count->id, function);

	spin_unlock_irqrestore(&priv->lock, irqflags);

	return retval;
}

static int quad8_function_write(struct counter_device *counter,
				struct counter_count *count,
				enum counter_function function)
{
	struct quad8 *const priv = counter_priv(counter);
	const int id = count->id;
	unsigned int *const quadrature_mode = priv->quadrature_mode + id;
	unsigned int *const scale = priv->quadrature_scale + id;
	unsigned int *const synchronous_mode = priv->synchronous_mode + id;
	u8 __iomem *const control = &priv->reg->channel[id].control;
	unsigned long irqflags;
	unsigned int mode_cfg;
	unsigned int idr_cfg;

	spin_lock_irqsave(&priv->lock, irqflags);

	mode_cfg = u8_encode_bits(priv->count_mode[id], COUNT_MODE);
	idr_cfg = u8_encode_bits(priv->index_polarity[id], INDEX_POLARITY);

	if (function == COUNTER_FUNCTION_PULSE_DIRECTION) {
		*quadrature_mode = 0;

		/* Quadrature scaling only available in quadrature mode */
		*scale = 0;

		mode_cfg |= u8_encode_bits(NON_QUADRATURE, QUADRATURE_MODE);

		/* Synchronous function not supported in non-quadrature mode */
		if (*synchronous_mode) {
			*synchronous_mode = 0;
			/* Disable synchronous function mode */
			idr_cfg |= u8_encode_bits(*synchronous_mode, INDEX_MODE);
			iowrite8(SELECT_IDR | idr_cfg, control);
		}
	} else {
		*quadrature_mode = 1;

		switch (function) {
		case COUNTER_FUNCTION_QUADRATURE_X1_A:
			*scale = 0;
			mode_cfg |= u8_encode_bits(QUADRATURE_X1, QUADRATURE_MODE);
			break;
		case COUNTER_FUNCTION_QUADRATURE_X2_A:
			*scale = 1;
			mode_cfg |= u8_encode_bits(QUADRATURE_X2, QUADRATURE_MODE);
			break;
		case COUNTER_FUNCTION_QUADRATURE_X4:
			*scale = 2;
			mode_cfg |= u8_encode_bits(QUADRATURE_X4, QUADRATURE_MODE);
			break;
		default:
			/* should never reach this path */
			spin_unlock_irqrestore(&priv->lock, irqflags);
			return -EINVAL;
		}
	}

	/* Load mode configuration to Counter Mode Register */
	iowrite8(SELECT_CMR | mode_cfg, control);

	spin_unlock_irqrestore(&priv->lock, irqflags);

	return 0;
}

static int quad8_direction_read(struct counter_device *counter,
				struct counter_count *count,
				enum counter_count_direction *direction)
{
	const struct quad8 *const priv = counter_priv(counter);
	unsigned int ud_flag;
	u8 __iomem *const flag_addr = &priv->reg->channel[count->id].control;
	u8 flag;

	flag = ioread8(flag_addr);
	/* U/D flag: nonzero = up, zero = down */
	ud_flag = u8_get_bits(flag, FLAG_UD);

	*direction = (ud_flag) ? COUNTER_COUNT_DIRECTION_FORWARD :
		COUNTER_COUNT_DIRECTION_BACKWARD;

	return 0;
}

static const enum counter_synapse_action quad8_index_actions_list[] = {
	COUNTER_SYNAPSE_ACTION_NONE,
	COUNTER_SYNAPSE_ACTION_RISING_EDGE,
};

static const enum counter_synapse_action quad8_synapse_actions_list[] = {
	COUNTER_SYNAPSE_ACTION_NONE,
	COUNTER_SYNAPSE_ACTION_RISING_EDGE,
	COUNTER_SYNAPSE_ACTION_FALLING_EDGE,
	COUNTER_SYNAPSE_ACTION_BOTH_EDGES,
};

static int quad8_action_read(struct counter_device *counter,
			     struct counter_count *count,
			     struct counter_synapse *synapse,
			     enum counter_synapse_action *action)
{
	struct quad8 *const priv = counter_priv(counter);
	unsigned long irqflags;
	int err;
	enum counter_function function;
	const size_t signal_a_id = count->synapses[0].signal->id;
	enum counter_count_direction direction;

	/* Handle Index signals */
	if (synapse->signal->id >= 16) {
		if (!priv->preset_enable[count->id])
			*action = COUNTER_SYNAPSE_ACTION_RISING_EDGE;
		else
			*action = COUNTER_SYNAPSE_ACTION_NONE;

		return 0;
	}

	spin_lock_irqsave(&priv->lock, irqflags);

	/* Get Count function and direction atomically */
	err = quad8_function_get(priv, count->id, &function);
	if (err) {
		spin_unlock_irqrestore(&priv->lock, irqflags);
		return err;
	}
	err = quad8_direction_read(counter, count, &direction);
	if (err) {
		spin_unlock_irqrestore(&priv->lock, irqflags);
		return err;
	}

	spin_unlock_irqrestore(&priv->lock, irqflags);

	/* Default action mode */
	*action = COUNTER_SYNAPSE_ACTION_NONE;

	/* Determine action mode based on current count function mode */
	switch (function) {
	case COUNTER_FUNCTION_PULSE_DIRECTION:
		if (synapse->signal->id == signal_a_id)
			*action = COUNTER_SYNAPSE_ACTION_RISING_EDGE;
		return 0;
	case COUNTER_FUNCTION_QUADRATURE_X1_A:
		if (synapse->signal->id == signal_a_id) {
			if (direction == COUNTER_COUNT_DIRECTION_FORWARD)
				*action = COUNTER_SYNAPSE_ACTION_RISING_EDGE;
			else
				*action = COUNTER_SYNAPSE_ACTION_FALLING_EDGE;
		}
		return 0;
	case COUNTER_FUNCTION_QUADRATURE_X2_A:
		if (synapse->signal->id == signal_a_id)
			*action = COUNTER_SYNAPSE_ACTION_BOTH_EDGES;
		return 0;
	case COUNTER_FUNCTION_QUADRATURE_X4:
		*action = COUNTER_SYNAPSE_ACTION_BOTH_EDGES;
		return 0;
	default:
		/* should never reach this path */
		return -EINVAL;
	}
}

enum {
	QUAD8_EVENT_CARRY = FLG1_CARRY_FLG2_BORROW,
	QUAD8_EVENT_COMPARE = FLG1_COMPARE_FLG2_BORROW,
	QUAD8_EVENT_CARRY_BORROW = FLG1_CARRYBORROW_FLG2_UD,
	QUAD8_EVENT_INDEX = FLG1_INDX_FLG2_E,
};

static int quad8_events_configure(struct counter_device *counter)
{
	struct quad8 *const priv = counter_priv(counter);
	unsigned long irq_enabled = 0;
	unsigned long irqflags;
	struct counter_event_node *event_node;
	unsigned int next_irq_trigger;
	unsigned long ior_cfg;

	spin_lock_irqsave(&priv->lock, irqflags);

	list_for_each_entry(event_node, &counter->events_list, l) {
		switch (event_node->event) {
		case COUNTER_EVENT_OVERFLOW:
			next_irq_trigger = QUAD8_EVENT_CARRY;
			break;
		case COUNTER_EVENT_THRESHOLD:
			next_irq_trigger = QUAD8_EVENT_COMPARE;
			break;
		case COUNTER_EVENT_OVERFLOW_UNDERFLOW:
			next_irq_trigger = QUAD8_EVENT_CARRY_BORROW;
			break;
		case COUNTER_EVENT_INDEX:
			next_irq_trigger = QUAD8_EVENT_INDEX;
			break;
		default:
			/* should never reach this path */
			spin_unlock_irqrestore(&priv->lock, irqflags);
			return -EINVAL;
		}

		/* Enable IRQ line */
		irq_enabled |= BIT(event_node->channel);

		/* Skip configuration if it is the same as previously set */
		if (priv->irq_trigger[event_node->channel] == next_irq_trigger)
			continue;

		/* Save new IRQ function configuration */
		priv->irq_trigger[event_node->channel] = next_irq_trigger;

		/* Load configuration to I/O Control Register */
		ior_cfg = u8_encode_bits(priv->ab_enable[event_node->channel], AB_GATE) |
			  u8_encode_bits(priv->preset_enable[event_node->channel], LOAD_PIN) |
			  u8_encode_bits(priv->irq_trigger[event_node->channel], FLG_PINS);
		iowrite8(SELECT_IOR | ior_cfg,
			 &priv->reg->channel[event_node->channel].control);
	}

	iowrite8(irq_enabled, &priv->reg->index_interrupt);

	spin_unlock_irqrestore(&priv->lock, irqflags);

	return 0;
}

static int quad8_watch_validate(struct counter_device *counter,
				const struct counter_watch *watch)
{
	struct counter_event_node *event_node;

	if (watch->channel > QUAD8_NUM_COUNTERS - 1)
		return -EINVAL;

	switch (watch->event) {
	case COUNTER_EVENT_OVERFLOW:
	case COUNTER_EVENT_THRESHOLD:
	case COUNTER_EVENT_OVERFLOW_UNDERFLOW:
	case COUNTER_EVENT_INDEX:
		list_for_each_entry(event_node, &counter->next_events_list, l)
			if (watch->channel == event_node->channel &&
				watch->event != event_node->event)
				return -EINVAL;
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct counter_ops quad8_ops = {
	.signal_read = quad8_signal_read,
	.count_read = quad8_count_read,
	.count_write = quad8_count_write,
	.function_read = quad8_function_read,
	.function_write = quad8_function_write,
	.action_read = quad8_action_read,
	.events_configure = quad8_events_configure,
	.watch_validate = quad8_watch_validate,
};

static const char *const quad8_index_polarity_modes[] = {
	"negative",
	"positive"
};

static int quad8_index_polarity_get(struct counter_device *counter,
				    struct counter_signal *signal,
				    u32 *index_polarity)
{
	const struct quad8 *const priv = counter_priv(counter);
	const size_t channel_id = signal->id - 16;

	*index_polarity = priv->index_polarity[channel_id];

	return 0;
}

static int quad8_index_polarity_set(struct counter_device *counter,
				    struct counter_signal *signal,
				    u32 index_polarity)
{
	struct quad8 *const priv = counter_priv(counter);
	const size_t channel_id = signal->id - 16;
	u8 __iomem *const control = &priv->reg->channel[channel_id].control;
	unsigned long irqflags;
	unsigned int idr_cfg = u8_encode_bits(index_polarity, INDEX_POLARITY);

	spin_lock_irqsave(&priv->lock, irqflags);

	idr_cfg |= u8_encode_bits(priv->synchronous_mode[channel_id], INDEX_MODE);

	priv->index_polarity[channel_id] = index_polarity;

	/* Load Index Control configuration to Index Control Register */
	iowrite8(SELECT_IDR | idr_cfg, control);

	spin_unlock_irqrestore(&priv->lock, irqflags);

	return 0;
}

static int quad8_polarity_read(struct counter_device *counter,
			       struct counter_signal *signal,
			       enum counter_signal_polarity *polarity)
{
	int err;
	u32 index_polarity;

	err = quad8_index_polarity_get(counter, signal, &index_polarity);
	if (err)
		return err;

	*polarity = (index_polarity) ? COUNTER_SIGNAL_POLARITY_POSITIVE :
		COUNTER_SIGNAL_POLARITY_NEGATIVE;

	return 0;
}

static int quad8_polarity_write(struct counter_device *counter,
				struct counter_signal *signal,
				enum counter_signal_polarity polarity)
{
	const u32 pol = (polarity == COUNTER_SIGNAL_POLARITY_POSITIVE) ? 1 : 0;

	return quad8_index_polarity_set(counter, signal, pol);
}

static const char *const quad8_synchronous_modes[] = {
	"non-synchronous",
	"synchronous"
};

static int quad8_synchronous_mode_get(struct counter_device *counter,
				      struct counter_signal *signal,
				      u32 *synchronous_mode)
{
	const struct quad8 *const priv = counter_priv(counter);
	const size_t channel_id = signal->id - 16;

	*synchronous_mode = priv->synchronous_mode[channel_id];

	return 0;
}

static int quad8_synchronous_mode_set(struct counter_device *counter,
				      struct counter_signal *signal,
				      u32 synchronous_mode)
{
	struct quad8 *const priv = counter_priv(counter);
	const size_t channel_id = signal->id - 16;
	u8 __iomem *const control = &priv->reg->channel[channel_id].control;
	unsigned long irqflags;
	unsigned int idr_cfg = u8_encode_bits(synchronous_mode, INDEX_MODE);

	spin_lock_irqsave(&priv->lock, irqflags);

	idr_cfg |= u8_encode_bits(priv->index_polarity[channel_id], INDEX_POLARITY);

	/* Index function must be non-synchronous in non-quadrature mode */
	if (synchronous_mode && !priv->quadrature_mode[channel_id]) {
		spin_unlock_irqrestore(&priv->lock, irqflags);
		return -EINVAL;
	}

	priv->synchronous_mode[channel_id] = synchronous_mode;

	/* Load Index Control configuration to Index Control Register */
	iowrite8(SELECT_IDR | idr_cfg, control);

	spin_unlock_irqrestore(&priv->lock, irqflags);

	return 0;
}

static int quad8_count_floor_read(struct counter_device *counter,
				  struct counter_count *count, u64 *floor)
{
	/* Only a floor of 0 is supported */
	*floor = 0;

	return 0;
}

static int quad8_count_mode_read(struct counter_device *counter,
				 struct counter_count *count,
				 enum counter_count_mode *cnt_mode)
{
	const struct quad8 *const priv = counter_priv(counter);

	switch (priv->count_mode[count->id]) {
	case NORMAL_COUNT:
		*cnt_mode = COUNTER_COUNT_MODE_NORMAL;
		break;
	case RANGE_LIMIT:
		*cnt_mode = COUNTER_COUNT_MODE_RANGE_LIMIT;
		break;
	case NON_RECYCLE_COUNT:
		*cnt_mode = COUNTER_COUNT_MODE_NON_RECYCLE;
		break;
	case MODULO_N:
		*cnt_mode = COUNTER_COUNT_MODE_MODULO_N;
		break;
	}

	return 0;
}

static int quad8_count_mode_write(struct counter_device *counter,
				  struct counter_count *count,
				  enum counter_count_mode cnt_mode)
{
	struct quad8 *const priv = counter_priv(counter);
	unsigned int count_mode;
	unsigned int mode_cfg;
	u8 __iomem *const control = &priv->reg->channel[count->id].control;
	unsigned long irqflags;

	switch (cnt_mode) {
	case COUNTER_COUNT_MODE_NORMAL:
		count_mode = NORMAL_COUNT;
		break;
	case COUNTER_COUNT_MODE_RANGE_LIMIT:
		count_mode = RANGE_LIMIT;
		break;
	case COUNTER_COUNT_MODE_NON_RECYCLE:
		count_mode = NON_RECYCLE_COUNT;
		break;
	case COUNTER_COUNT_MODE_MODULO_N:
		count_mode = MODULO_N;
		break;
	default:
		/* should never reach this path */
		return -EINVAL;
	}

	spin_lock_irqsave(&priv->lock, irqflags);

	priv->count_mode[count->id] = count_mode;

	/* Set count mode configuration value */
	mode_cfg = u8_encode_bits(count_mode, COUNT_MODE);

	/* Add quadrature mode configuration */
	if (priv->quadrature_mode[count->id])
		mode_cfg |= u8_encode_bits(priv->quadrature_scale[count->id] + 1, QUADRATURE_MODE);
	else
		mode_cfg |= u8_encode_bits(NON_QUADRATURE, QUADRATURE_MODE);

	/* Load mode configuration to Counter Mode Register */
	iowrite8(SELECT_CMR | mode_cfg, control);

	spin_unlock_irqrestore(&priv->lock, irqflags);

	return 0;
}

static int quad8_count_enable_read(struct counter_device *counter,
				   struct counter_count *count, u8 *enable)
{
	const struct quad8 *const priv = counter_priv(counter);

	*enable = priv->ab_enable[count->id];

	return 0;
}

static int quad8_count_enable_write(struct counter_device *counter,
				    struct counter_count *count, u8 enable)
{
	struct quad8 *const priv = counter_priv(counter);
	u8 __iomem *const control = &priv->reg->channel[count->id].control;
	unsigned long irqflags;
	unsigned int ior_cfg;

	spin_lock_irqsave(&priv->lock, irqflags);

	priv->ab_enable[count->id] = enable;

	ior_cfg = u8_encode_bits(enable, AB_GATE) |
		  u8_encode_bits(priv->preset_enable[count->id], LOAD_PIN) |
		  u8_encode_bits(priv->irq_trigger[count->id], FLG_PINS);

	/* Load I/O control configuration */
	iowrite8(SELECT_IOR | ior_cfg, control);

	spin_unlock_irqrestore(&priv->lock, irqflags);

	return 0;
}

static const char *const quad8_noise_error_states[] = {
	"No excessive noise is present at the count inputs",
	"Excessive noise is present at the count inputs"
};

static int quad8_error_noise_get(struct counter_device *counter,
				 struct counter_count *count, u32 *noise_error)
{
	const struct quad8 *const priv = counter_priv(counter);
	u8 __iomem *const flag_addr = &priv->reg->channel[count->id].control;
	u8 flag;

	flag = ioread8(flag_addr);
	*noise_error = u8_get_bits(flag, FLAG_E);

	return 0;
}

static int quad8_count_preset_read(struct counter_device *counter,
				   struct counter_count *count, u64 *preset)
{
	const struct quad8 *const priv = counter_priv(counter);

	*preset = priv->preset[count->id];

	return 0;
}

static void quad8_preset_register_set(struct quad8 *const priv, const int id,
				      const unsigned int preset)
{
	struct channel_reg __iomem *const chan = priv->reg->channel + id;
	int i;

	priv->preset[id] = preset;

	iowrite8(SELECT_RLD | RESET_BP, &chan->control);

	/* Set Preset Register */
	for (i = 0; i < 3; i++)
		iowrite8(preset >> (8 * i), &chan->data);
}

static int quad8_count_preset_write(struct counter_device *counter,
				    struct counter_count *count, u64 preset)
{
	struct quad8 *const priv = counter_priv(counter);
	unsigned long irqflags;

	if (preset > LS7267_CNTR_MAX)
		return -ERANGE;

	spin_lock_irqsave(&priv->lock, irqflags);

	quad8_preset_register_set(priv, count->id, preset);

	spin_unlock_irqrestore(&priv->lock, irqflags);

	return 0;
}

static int quad8_count_ceiling_read(struct counter_device *counter,
				    struct counter_count *count, u64 *ceiling)
{
	struct quad8 *const priv = counter_priv(counter);
	unsigned long irqflags;

	spin_lock_irqsave(&priv->lock, irqflags);

	/* Range Limit and Modulo-N count modes use preset value as ceiling */
	switch (priv->count_mode[count->id]) {
	case RANGE_LIMIT:
	case MODULO_N:
		*ceiling = priv->preset[count->id];
		break;
	default:
		*ceiling = LS7267_CNTR_MAX;
		break;
	}

	spin_unlock_irqrestore(&priv->lock, irqflags);

	return 0;
}

static int quad8_count_ceiling_write(struct counter_device *counter,
				     struct counter_count *count, u64 ceiling)
{
	struct quad8 *const priv = counter_priv(counter);
	unsigned long irqflags;

	if (ceiling > LS7267_CNTR_MAX)
		return -ERANGE;

	spin_lock_irqsave(&priv->lock, irqflags);

	/* Range Limit and Modulo-N count modes use preset value as ceiling */
	switch (priv->count_mode[count->id]) {
	case RANGE_LIMIT:
	case MODULO_N:
		quad8_preset_register_set(priv, count->id, ceiling);
		spin_unlock_irqrestore(&priv->lock, irqflags);
		return 0;
	}

	spin_unlock_irqrestore(&priv->lock, irqflags);

	return -EINVAL;
}

static int quad8_count_preset_enable_read(struct counter_device *counter,
					  struct counter_count *count,
					  u8 *preset_enable)
{
	const struct quad8 *const priv = counter_priv(counter);

	*preset_enable = !priv->preset_enable[count->id];

	return 0;
}

static int quad8_count_preset_enable_write(struct counter_device *counter,
					   struct counter_count *count,
					   u8 preset_enable)
{
	struct quad8 *const priv = counter_priv(counter);
	u8 __iomem *const control = &priv->reg->channel[count->id].control;
	unsigned long irqflags;
	unsigned int ior_cfg;

	/* Preset enable is active low in Input/Output Control register */
	preset_enable = !preset_enable;

	spin_lock_irqsave(&priv->lock, irqflags);

	priv->preset_enable[count->id] = preset_enable;

	ior_cfg = u8_encode_bits(priv->ab_enable[count->id], AB_GATE) |
		  u8_encode_bits(preset_enable, LOAD_PIN) |
		  u8_encode_bits(priv->irq_trigger[count->id], FLG_PINS);

	/* Load I/O control configuration to Input / Output Control Register */
	iowrite8(SELECT_IOR | ior_cfg, control);

	spin_unlock_irqrestore(&priv->lock, irqflags);

	return 0;
}

static int quad8_signal_cable_fault_read(struct counter_device *counter,
					 struct counter_signal *signal,
					 u8 *cable_fault)
{
	struct quad8 *const priv = counter_priv(counter);
	const size_t channel_id = signal->id / 2;
	unsigned long irqflags;
	bool disabled;
	unsigned int status;

	spin_lock_irqsave(&priv->lock, irqflags);

	disabled = !(priv->cable_fault_enable & BIT(channel_id));

	if (disabled) {
		spin_unlock_irqrestore(&priv->lock, irqflags);
		return -EINVAL;
	}

	/* Logic 0 = cable fault */
	status = ioread8(&priv->reg->cable_status);

	spin_unlock_irqrestore(&priv->lock, irqflags);

	/* Mask respective channel and invert logic */
	*cable_fault = !(status & BIT(channel_id));

	return 0;
}

static int quad8_signal_cable_fault_enable_read(struct counter_device *counter,
						struct counter_signal *signal,
						u8 *enable)
{
	const struct quad8 *const priv = counter_priv(counter);
	const size_t channel_id = signal->id / 2;

	*enable = !!(priv->cable_fault_enable & BIT(channel_id));

	return 0;
}

static int quad8_signal_cable_fault_enable_write(struct counter_device *counter,
						 struct counter_signal *signal,
						 u8 enable)
{
	struct quad8 *const priv = counter_priv(counter);
	const size_t channel_id = signal->id / 2;
	unsigned long irqflags;
	unsigned int cable_fault_enable;

	spin_lock_irqsave(&priv->lock, irqflags);

	if (enable)
		priv->cable_fault_enable |= BIT(channel_id);
	else
		priv->cable_fault_enable &= ~BIT(channel_id);

	/* Enable is active low in Differential Encoder Cable Status register */
	cable_fault_enable = ~priv->cable_fault_enable;

	iowrite8(cable_fault_enable, &priv->reg->cable_status);

	spin_unlock_irqrestore(&priv->lock, irqflags);

	return 0;
}

static int quad8_signal_fck_prescaler_read(struct counter_device *counter,
					   struct counter_signal *signal,
					   u8 *prescaler)
{
	const struct quad8 *const priv = counter_priv(counter);

	*prescaler = priv->fck_prescaler[signal->id / 2];

	return 0;
}

static int quad8_signal_fck_prescaler_write(struct counter_device *counter,
					    struct counter_signal *signal,
					    u8 prescaler)
{
	struct quad8 *const priv = counter_priv(counter);
	const size_t channel_id = signal->id / 2;
	struct channel_reg __iomem *const chan = priv->reg->channel + channel_id;
	unsigned long irqflags;

	spin_lock_irqsave(&priv->lock, irqflags);

	priv->fck_prescaler[channel_id] = prescaler;

	iowrite8(SELECT_RLD | RESET_BP, &chan->control);

	/* Set filter clock factor */
	iowrite8(prescaler, &chan->data);
	iowrite8(SELECT_RLD | RESET_BP | TRANSFER_PR0_TO_PSC, &chan->control);

	spin_unlock_irqrestore(&priv->lock, irqflags);

	return 0;
}

static struct counter_comp quad8_signal_ext[] = {
	COUNTER_COMP_SIGNAL_BOOL("cable_fault", quad8_signal_cable_fault_read,
				 NULL),
	COUNTER_COMP_SIGNAL_BOOL("cable_fault_enable",
				 quad8_signal_cable_fault_enable_read,
				 quad8_signal_cable_fault_enable_write),
	COUNTER_COMP_SIGNAL_U8("filter_clock_prescaler",
			       quad8_signal_fck_prescaler_read,
			       quad8_signal_fck_prescaler_write)
};

static const enum counter_signal_polarity quad8_polarities[] = {
	COUNTER_SIGNAL_POLARITY_POSITIVE,
	COUNTER_SIGNAL_POLARITY_NEGATIVE,
};

static DEFINE_COUNTER_AVAILABLE(quad8_polarity_available, quad8_polarities);

static DEFINE_COUNTER_ENUM(quad8_index_pol_enum, quad8_index_polarity_modes);
static DEFINE_COUNTER_ENUM(quad8_synch_mode_enum, quad8_synchronous_modes);

static struct counter_comp quad8_index_ext[] = {
	COUNTER_COMP_SIGNAL_ENUM("index_polarity", quad8_index_polarity_get,
				 quad8_index_polarity_set,
				 quad8_index_pol_enum),
	COUNTER_COMP_POLARITY(quad8_polarity_read, quad8_polarity_write,
			      quad8_polarity_available),
	COUNTER_COMP_SIGNAL_ENUM("synchronous_mode", quad8_synchronous_mode_get,
				 quad8_synchronous_mode_set,
				 quad8_synch_mode_enum),
};

#define QUAD8_QUAD_SIGNAL(_id, _name) {		\
	.id = (_id),				\
	.name = (_name),			\
	.ext = quad8_signal_ext,		\
	.num_ext = ARRAY_SIZE(quad8_signal_ext)	\
}

#define	QUAD8_INDEX_SIGNAL(_id, _name) {	\
	.id = (_id),				\
	.name = (_name),			\
	.ext = quad8_index_ext,			\
	.num_ext = ARRAY_SIZE(quad8_index_ext)	\
}

static struct counter_signal quad8_signals[] = {
	QUAD8_QUAD_SIGNAL(0, "Channel 1 Quadrature A"),
	QUAD8_QUAD_SIGNAL(1, "Channel 1 Quadrature B"),
	QUAD8_QUAD_SIGNAL(2, "Channel 2 Quadrature A"),
	QUAD8_QUAD_SIGNAL(3, "Channel 2 Quadrature B"),
	QUAD8_QUAD_SIGNAL(4, "Channel 3 Quadrature A"),
	QUAD8_QUAD_SIGNAL(5, "Channel 3 Quadrature B"),
	QUAD8_QUAD_SIGNAL(6, "Channel 4 Quadrature A"),
	QUAD8_QUAD_SIGNAL(7, "Channel 4 Quadrature B"),
	QUAD8_QUAD_SIGNAL(8, "Channel 5 Quadrature A"),
	QUAD8_QUAD_SIGNAL(9, "Channel 5 Quadrature B"),
	QUAD8_QUAD_SIGNAL(10, "Channel 6 Quadrature A"),
	QUAD8_QUAD_SIGNAL(11, "Channel 6 Quadrature B"),
	QUAD8_QUAD_SIGNAL(12, "Channel 7 Quadrature A"),
	QUAD8_QUAD_SIGNAL(13, "Channel 7 Quadrature B"),
	QUAD8_QUAD_SIGNAL(14, "Channel 8 Quadrature A"),
	QUAD8_QUAD_SIGNAL(15, "Channel 8 Quadrature B"),
	QUAD8_INDEX_SIGNAL(16, "Channel 1 Index"),
	QUAD8_INDEX_SIGNAL(17, "Channel 2 Index"),
	QUAD8_INDEX_SIGNAL(18, "Channel 3 Index"),
	QUAD8_INDEX_SIGNAL(19, "Channel 4 Index"),
	QUAD8_INDEX_SIGNAL(20, "Channel 5 Index"),
	QUAD8_INDEX_SIGNAL(21, "Channel 6 Index"),
	QUAD8_INDEX_SIGNAL(22, "Channel 7 Index"),
	QUAD8_INDEX_SIGNAL(23, "Channel 8 Index")
};

#define QUAD8_COUNT_SYNAPSES(_id) {					\
	{								\
		.actions_list = quad8_synapse_actions_list,		\
		.num_actions = ARRAY_SIZE(quad8_synapse_actions_list),	\
		.signal = quad8_signals + 2 * (_id)			\
	},								\
	{								\
		.actions_list = quad8_synapse_actions_list,		\
		.num_actions = ARRAY_SIZE(quad8_synapse_actions_list),	\
		.signal = quad8_signals + 2 * (_id) + 1			\
	},								\
	{								\
		.actions_list = quad8_index_actions_list,		\
		.num_actions = ARRAY_SIZE(quad8_index_actions_list),	\
		.signal = quad8_signals + 2 * (_id) + 16		\
	}								\
}

static struct counter_synapse quad8_count_synapses[][3] = {
	QUAD8_COUNT_SYNAPSES(0), QUAD8_COUNT_SYNAPSES(1),
	QUAD8_COUNT_SYNAPSES(2), QUAD8_COUNT_SYNAPSES(3),
	QUAD8_COUNT_SYNAPSES(4), QUAD8_COUNT_SYNAPSES(5),
	QUAD8_COUNT_SYNAPSES(6), QUAD8_COUNT_SYNAPSES(7)
};

static const enum counter_count_mode quad8_cnt_modes[] = {
	COUNTER_COUNT_MODE_NORMAL,
	COUNTER_COUNT_MODE_RANGE_LIMIT,
	COUNTER_COUNT_MODE_NON_RECYCLE,
	COUNTER_COUNT_MODE_MODULO_N,
};

static DEFINE_COUNTER_AVAILABLE(quad8_count_mode_available, quad8_cnt_modes);

static DEFINE_COUNTER_ENUM(quad8_error_noise_enum, quad8_noise_error_states);

static struct counter_comp quad8_count_ext[] = {
	COUNTER_COMP_CEILING(quad8_count_ceiling_read,
			     quad8_count_ceiling_write),
	COUNTER_COMP_FLOOR(quad8_count_floor_read, NULL),
	COUNTER_COMP_COUNT_MODE(quad8_count_mode_read, quad8_count_mode_write,
				quad8_count_mode_available),
	COUNTER_COMP_DIRECTION(quad8_direction_read),
	COUNTER_COMP_ENABLE(quad8_count_enable_read, quad8_count_enable_write),
	COUNTER_COMP_COUNT_ENUM("error_noise", quad8_error_noise_get, NULL,
				quad8_error_noise_enum),
	COUNTER_COMP_PRESET(quad8_count_preset_read, quad8_count_preset_write),
	COUNTER_COMP_PRESET_ENABLE(quad8_count_preset_enable_read,
				   quad8_count_preset_enable_write),
};

#define QUAD8_COUNT(_id, _cntname) {					\
	.id = (_id),							\
	.name = (_cntname),						\
	.functions_list = quad8_count_functions_list,			\
	.num_functions = ARRAY_SIZE(quad8_count_functions_list),	\
	.synapses = quad8_count_synapses[(_id)],			\
	.num_synapses =	2,						\
	.ext = quad8_count_ext,						\
	.num_ext = ARRAY_SIZE(quad8_count_ext)				\
}

static struct counter_count quad8_counts[] = {
	QUAD8_COUNT(0, "Channel 1 Count"),
	QUAD8_COUNT(1, "Channel 2 Count"),
	QUAD8_COUNT(2, "Channel 3 Count"),
	QUAD8_COUNT(3, "Channel 4 Count"),
	QUAD8_COUNT(4, "Channel 5 Count"),
	QUAD8_COUNT(5, "Channel 6 Count"),
	QUAD8_COUNT(6, "Channel 7 Count"),
	QUAD8_COUNT(7, "Channel 8 Count")
};

static irqreturn_t quad8_irq_handler(int irq, void *private)
{
	struct counter_device *counter = private;
	struct quad8 *const priv = counter_priv(counter);
	unsigned long irq_status;
	unsigned long channel;
	u8 event;

	irq_status = ioread8(&priv->reg->interrupt_status);
	if (!irq_status)
		return IRQ_NONE;

	for_each_set_bit(channel, &irq_status, QUAD8_NUM_COUNTERS) {
		switch (priv->irq_trigger[channel]) {
		case QUAD8_EVENT_CARRY:
			event = COUNTER_EVENT_OVERFLOW;
				break;
		case QUAD8_EVENT_COMPARE:
			event = COUNTER_EVENT_THRESHOLD;
				break;
		case QUAD8_EVENT_CARRY_BORROW:
			event = COUNTER_EVENT_OVERFLOW_UNDERFLOW;
				break;
		case QUAD8_EVENT_INDEX:
			event = COUNTER_EVENT_INDEX;
				break;
		default:
			/* should never reach this path */
			WARN_ONCE(true, "invalid interrupt trigger function %u configured for channel %lu\n",
				  priv->irq_trigger[channel], channel);
			continue;
		}

		counter_push_event(counter, event, channel);
	}

	/* Clear pending interrupts on device */
	iowrite8(CLEAR_PENDING_INTERRUPTS, &priv->reg->channel_oper);

	return IRQ_HANDLED;
}

static void quad8_init_counter(struct channel_reg __iomem *const chan)
{
	unsigned long i;

	iowrite8(SELECT_RLD | RESET_BP, &chan->control);
	/* Reset filter clock factor */
	iowrite8(0, &chan->data);
	iowrite8(SELECT_RLD | RESET_BP | TRANSFER_PR0_TO_PSC, &chan->control);
	iowrite8(SELECT_RLD | RESET_BP, &chan->control);
	/* Reset Preset Register */
	for (i = 0; i < 3; i++)
		iowrite8(0x00, &chan->data);
	iowrite8(SELECT_RLD | RESET_BT_CT_CPT_S_IDX, &chan->control);
	iowrite8(SELECT_RLD | RESET_E, &chan->control);
	/* Binary encoding; Normal count; non-quadrature mode */
	iowrite8(SELECT_CMR | BINARY | u8_encode_bits(NORMAL_COUNT, COUNT_MODE) |
		 u8_encode_bits(NON_QUADRATURE, QUADRATURE_MODE), &chan->control);
	/* Disable A and B inputs; preset on index; FLG1 as Carry */
	iowrite8(SELECT_IOR | DISABLE_AB | u8_encode_bits(LOAD_CNTR, LOAD_PIN) |
		 u8_encode_bits(FLG1_CARRY_FLG2_BORROW, FLG_PINS), &chan->control);
	/* Disable index function; negative index polarity */
	iowrite8(SELECT_IDR | u8_encode_bits(DISABLE_INDEX_MODE, INDEX_MODE) |
		 u8_encode_bits(NEGATIVE_INDEX_POLARITY, INDEX_POLARITY), &chan->control);
}

static int quad8_probe(struct device *dev, unsigned int id)
{
	struct counter_device *counter;
	struct quad8 *priv;
	unsigned long i;
	int err;

	if (!devm_request_region(dev, base[id], QUAD8_EXTENT, dev_name(dev))) {
		dev_err(dev, "Unable to lock port addresses (0x%X-0x%X)\n",
			base[id], base[id] + QUAD8_EXTENT);
		return -EBUSY;
	}

	counter = devm_counter_alloc(dev, sizeof(*priv));
	if (!counter)
		return -ENOMEM;
	priv = counter_priv(counter);

	priv->reg = devm_ioport_map(dev, base[id], QUAD8_EXTENT);
	if (!priv->reg)
		return -ENOMEM;

	/* Initialize Counter device and driver data */
	counter->name = dev_name(dev);
	counter->parent = dev;
	counter->ops = &quad8_ops;
	counter->counts = quad8_counts;
	counter->num_counts = ARRAY_SIZE(quad8_counts);
	counter->signals = quad8_signals;
	counter->num_signals = ARRAY_SIZE(quad8_signals);

	spin_lock_init(&priv->lock);

	/* Reset Index/Interrupt Register */
	iowrite8(0x00, &priv->reg->index_interrupt);
	/* Reset all counters and disable interrupt function */
	iowrite8(RESET_COUNTERS | DISABLE_INTERRUPT_FUNCTION, &priv->reg->channel_oper);
	/* Set initial configuration for all counters */
	for (i = 0; i < QUAD8_NUM_COUNTERS; i++)
		quad8_init_counter(priv->reg->channel + i);
	/* Disable Differential Encoder Cable Status for all channels */
	iowrite8(0xFF, &priv->reg->cable_status);
	/* Enable all counters and enable interrupt function */
	iowrite8(ENABLE_COUNTERS | ENABLE_INTERRUPT_FUNCTION, &priv->reg->channel_oper);

	err = devm_request_irq(&counter->dev, irq[id], quad8_irq_handler,
			       IRQF_SHARED, counter->name, counter);
	if (err)
		return err;

	err = devm_counter_add(dev, counter);
	if (err < 0)
		return dev_err_probe(dev, err, "Failed to add counter\n");

	return 0;
}

static struct isa_driver quad8_driver = {
	.probe = quad8_probe,
	.driver = {
		.name = "104-quad-8"
	}
};

module_isa_driver_with_irq(quad8_driver, num_quad8, num_irq);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("ACCES 104-QUAD-8 driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(COUNTER);
