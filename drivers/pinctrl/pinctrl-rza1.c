// SPDX-License-Identifier: GPL-2.0
/*
 * Combined GPIO and pin controller support for Renesas RZ/A1 (r7s72100) SoC
 *
 * Copyright (C) 2017 Jacopo Mondi
 */

/*
 * This pin controller/gpio combined driver supports Renesas devices of RZ/A1
 * family.
 * This includes SoCs which are sub- or super- sets of this particular line,
 * as RZ/A1H (r7s721000), RZ/A1M (r7s721010) and RZ/A1L (r7s721020).
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/slab.h>

#include "core.h"
#include "devicetree.h"
#include "pinconf.h"
#include "pinmux.h"

#define DRIVER_NAME			"pinctrl-rza1"

#define RZA1_P_REG			0x0000
#define RZA1_PPR_REG			0x0200
#define RZA1_PM_REG			0x0300
#define RZA1_PMC_REG			0x0400
#define RZA1_PFC_REG			0x0500
#define RZA1_PFCE_REG			0x0600
#define RZA1_PFCEA_REG			0x0a00
#define RZA1_PIBC_REG			0x4000
#define RZA1_PBDC_REG			0x4100
#define RZA1_PIPC_REG			0x4200

#define RZA1_ADDR(mem, reg, port)	((mem) + (reg) + ((port) * 4))

#define RZA1_NPORTS			12
#define RZA1_PINS_PER_PORT		16
#define RZA1_NPINS			(RZA1_PINS_PER_PORT * RZA1_NPORTS)
#define RZA1_PIN_ID_TO_PORT(id)		((id) / RZA1_PINS_PER_PORT)
#define RZA1_PIN_ID_TO_PIN(id)		((id) % RZA1_PINS_PER_PORT)

/*
 * Use 16 lower bits [15:0] for pin identifier
 * Use 16 higher bits [31:16] for pin mux function
 */
#define MUX_PIN_ID_MASK			GENMASK(15, 0)
#define MUX_FUNC_MASK			GENMASK(31, 16)

#define MUX_FUNC_OFFS			16
#define MUX_FUNC(pinconf)		\
	((pinconf & MUX_FUNC_MASK) >> MUX_FUNC_OFFS)
#define MUX_FUNC_PFC_MASK		BIT(0)
#define MUX_FUNC_PFCE_MASK		BIT(1)
#define MUX_FUNC_PFCEA_MASK		BIT(2)

/* Pin mux flags */
#define MUX_FLAGS_BIDIR			BIT(0)
#define MUX_FLAGS_SWIO_INPUT		BIT(1)
#define MUX_FLAGS_SWIO_OUTPUT		BIT(2)

/* ----------------------------------------------------------------------------
 * RZ/A1 pinmux flags
 */

/**
 * rza1_bidir_pin - describe a single pin that needs bidir flag applied.
 */
struct rza1_bidir_pin {
	u8 pin: 4;
	u8 func: 4;
};

/**
 * rza1_bidir_entry - describe a list of pins that needs bidir flag applied.
 *		      Each struct rza1_bidir_entry describes a port.
 */
struct rza1_bidir_entry {
	const unsigned int npins;
	const struct rza1_bidir_pin *pins;
};

/**
 * rza1_swio_pin - describe a single pin that needs bidir flag applied.
 */
struct rza1_swio_pin {
	u16 pin: 4;
	u16 port: 4;
	u16 func: 4;
	u16 input: 1;
};

/**
 * rza1_swio_entry - describe a list of pins that needs swio flag applied
 */
struct rza1_swio_entry {
	const unsigned int npins;
	const struct rza1_swio_pin *pins;
};

/**
 * rza1_pinmux_conf - group together bidir and swio pinmux flag tables
 */
struct rza1_pinmux_conf {
	const struct rza1_bidir_entry *bidir_entries;
	const struct rza1_swio_entry *swio_entries;
};

/* ----------------------------------------------------------------------------
 * RZ/A1H (r7s72100) pinmux flags
 */

static const struct rza1_bidir_pin rza1h_bidir_pins_p1[] = {
	{ .pin = 0, .func = 1 },
	{ .pin = 1, .func = 1 },
	{ .pin = 2, .func = 1 },
	{ .pin = 3, .func = 1 },
	{ .pin = 4, .func = 1 },
	{ .pin = 5, .func = 1 },
	{ .pin = 6, .func = 1 },
	{ .pin = 7, .func = 1 },
};

static const struct rza1_bidir_pin rza1h_bidir_pins_p2[] = {
	{ .pin = 0, .func = 1 },
	{ .pin = 1, .func = 1 },
	{ .pin = 2, .func = 1 },
	{ .pin = 3, .func = 1 },
	{ .pin = 4, .func = 1 },
	{ .pin = 0, .func = 4 },
	{ .pin = 1, .func = 4 },
	{ .pin = 2, .func = 4 },
	{ .pin = 3, .func = 4 },
	{ .pin = 5, .func = 1 },
	{ .pin = 6, .func = 1 },
	{ .pin = 7, .func = 1 },
	{ .pin = 8, .func = 1 },
	{ .pin = 9, .func = 1 },
	{ .pin = 10, .func = 1 },
	{ .pin = 11, .func = 1 },
	{ .pin = 12, .func = 1 },
	{ .pin = 13, .func = 1 },
	{ .pin = 14, .func = 1 },
	{ .pin = 15, .func = 1 },
	{ .pin = 12, .func = 4 },
	{ .pin = 13, .func = 4 },
	{ .pin = 14, .func = 4 },
	{ .pin = 15, .func = 4 },
};

static const struct rza1_bidir_pin rza1h_bidir_pins_p3[] = {
	{ .pin = 3, .func = 2 },
	{ .pin = 10, .func = 7 },
	{ .pin = 11, .func = 7 },
	{ .pin = 13, .func = 7 },
	{ .pin = 14, .func = 7 },
	{ .pin = 15, .func = 7 },
	{ .pin = 10, .func = 8 },
	{ .pin = 11, .func = 8 },
	{ .pin = 13, .func = 8 },
	{ .pin = 14, .func = 8 },
	{ .pin = 15, .func = 8 },
};

static const struct rza1_bidir_pin rza1h_bidir_pins_p4[] = {
	{ .pin = 0, .func = 8 },
	{ .pin = 1, .func = 8 },
	{ .pin = 2, .func = 8 },
	{ .pin = 3, .func = 8 },
	{ .pin = 10, .func = 3 },
	{ .pin = 11, .func = 3 },
	{ .pin = 13, .func = 3 },
	{ .pin = 14, .func = 3 },
	{ .pin = 15, .func = 3 },
	{ .pin = 10, .func = 4 },
	{ .pin = 11, .func = 4 },
	{ .pin = 13, .func = 4 },
	{ .pin = 14, .func = 4 },
	{ .pin = 15, .func = 4 },
	{ .pin = 12, .func = 5 },
	{ .pin = 13, .func = 5 },
	{ .pin = 14, .func = 5 },
	{ .pin = 15, .func = 5 },
};

static const struct rza1_bidir_pin rza1h_bidir_pins_p6[] = {
	{ .pin = 0, .func = 1 },
	{ .pin = 1, .func = 1 },
	{ .pin = 2, .func = 1 },
	{ .pin = 3, .func = 1 },
	{ .pin = 4, .func = 1 },
	{ .pin = 5, .func = 1 },
	{ .pin = 6, .func = 1 },
	{ .pin = 7, .func = 1 },
	{ .pin = 8, .func = 1 },
	{ .pin = 9, .func = 1 },
	{ .pin = 10, .func = 1 },
	{ .pin = 11, .func = 1 },
	{ .pin = 12, .func = 1 },
	{ .pin = 13, .func = 1 },
	{ .pin = 14, .func = 1 },
	{ .pin = 15, .func = 1 },
};

static const struct rza1_bidir_pin rza1h_bidir_pins_p7[] = {
	{ .pin = 13, .func = 3 },
};

static const struct rza1_bidir_pin rza1h_bidir_pins_p8[] = {
	{ .pin = 8, .func = 3 },
	{ .pin = 9, .func = 3 },
	{ .pin = 10, .func = 3 },
	{ .pin = 11, .func = 3 },
	{ .pin = 14, .func = 2 },
	{ .pin = 15, .func = 2 },
	{ .pin = 14, .func = 3 },
	{ .pin = 15, .func = 3 },
};

static const struct rza1_bidir_pin rza1h_bidir_pins_p9[] = {
	{ .pin = 0, .func = 2 },
	{ .pin = 1, .func = 2 },
	{ .pin = 4, .func = 2 },
	{ .pin = 5, .func = 2 },
	{ .pin = 6, .func = 2 },
	{ .pin = 7, .func = 2 },
};

static const struct rza1_bidir_pin rza1h_bidir_pins_p11[] = {
	{ .pin = 6, .func = 2 },
	{ .pin = 7, .func = 2 },
	{ .pin = 9, .func = 2 },
	{ .pin = 6, .func = 4 },
	{ .pin = 7, .func = 4 },
	{ .pin = 9, .func = 4 },
	{ .pin = 10, .func = 2 },
	{ .pin = 11, .func = 2 },
	{ .pin = 10, .func = 4 },
	{ .pin = 11, .func = 4 },
	{ .pin = 12, .func = 4 },
	{ .pin = 13, .func = 4 },
	{ .pin = 14, .func = 4 },
	{ .pin = 15, .func = 4 },
};

static const struct rza1_swio_pin rza1h_swio_pins[] = {
	{ .port = 2, .pin = 7, .func = 4, .input = 0 },
	{ .port = 2, .pin = 11, .func = 4, .input = 0 },
	{ .port = 3, .pin = 7, .func = 3, .input = 0 },
	{ .port = 3, .pin = 7, .func = 8, .input = 0 },
	{ .port = 4, .pin = 7, .func = 5, .input = 0 },
	{ .port = 4, .pin = 7, .func = 11, .input = 0 },
	{ .port = 4, .pin = 15, .func = 6, .input = 0 },
	{ .port = 5, .pin = 0, .func = 1, .input = 1 },
	{ .port = 5, .pin = 1, .func = 1, .input = 1 },
	{ .port = 5, .pin = 2, .func = 1, .input = 1 },
	{ .port = 5, .pin = 3, .func = 1, .input = 1 },
	{ .port = 5, .pin = 4, .func = 1, .input = 1 },
	{ .port = 5, .pin = 5, .func = 1, .input = 1 },
	{ .port = 5, .pin = 6, .func = 1, .input = 1 },
	{ .port = 5, .pin = 7, .func = 1, .input = 1 },
	{ .port = 7, .pin = 4, .func = 6, .input = 0 },
	{ .port = 7, .pin = 11, .func = 2, .input = 0 },
	{ .port = 8, .pin = 10, .func = 8, .input = 0 },
	{ .port = 10, .pin = 15, .func = 2, .input = 0 },
};

static const struct rza1_bidir_entry rza1h_bidir_entries[RZA1_NPORTS] = {
	[1] = { ARRAY_SIZE(rza1h_bidir_pins_p1), rza1h_bidir_pins_p1 },
	[2] = { ARRAY_SIZE(rza1h_bidir_pins_p2), rza1h_bidir_pins_p2 },
	[3] = { ARRAY_SIZE(rza1h_bidir_pins_p3), rza1h_bidir_pins_p3 },
	[4] = { ARRAY_SIZE(rza1h_bidir_pins_p4), rza1h_bidir_pins_p4 },
	[6] = { ARRAY_SIZE(rza1h_bidir_pins_p6), rza1h_bidir_pins_p6 },
	[7] = { ARRAY_SIZE(rza1h_bidir_pins_p7), rza1h_bidir_pins_p7 },
	[8] = { ARRAY_SIZE(rza1h_bidir_pins_p8), rza1h_bidir_pins_p8 },
	[9] = { ARRAY_SIZE(rza1h_bidir_pins_p9), rza1h_bidir_pins_p9 },
	[11] = { ARRAY_SIZE(rza1h_bidir_pins_p11), rza1h_bidir_pins_p11 },
};

static const struct rza1_swio_entry rza1h_swio_entries[] = {
	[0] = { ARRAY_SIZE(rza1h_swio_pins), rza1h_swio_pins },
};

/* RZ/A1H (r7s72100x) pinmux flags table */
static const struct rza1_pinmux_conf rza1h_pmx_conf = {
	.bidir_entries	= rza1h_bidir_entries,
	.swio_entries	= rza1h_swio_entries,
};

/* ----------------------------------------------------------------------------
 * RZ/A1L (r7s72102) pinmux flags
 */

static const struct rza1_bidir_pin rza1l_bidir_pins_p1[] = {
	{ .pin = 0, .func = 1 },
	{ .pin = 1, .func = 1 },
	{ .pin = 2, .func = 1 },
	{ .pin = 3, .func = 1 },
	{ .pin = 4, .func = 1 },
	{ .pin = 5, .func = 1 },
	{ .pin = 6, .func = 1 },
	{ .pin = 7, .func = 1 },
};

static const struct rza1_bidir_pin rza1l_bidir_pins_p3[] = {
	{ .pin = 0, .func = 2 },
	{ .pin = 1, .func = 2 },
	{ .pin = 2, .func = 2 },
	{ .pin = 4, .func = 2 },
	{ .pin = 5, .func = 2 },
	{ .pin = 10, .func = 2 },
	{ .pin = 11, .func = 2 },
	{ .pin = 12, .func = 2 },
	{ .pin = 13, .func = 2 },
};

static const struct rza1_bidir_pin rza1l_bidir_pins_p4[] = {
	{ .pin = 1, .func = 4 },
	{ .pin = 2, .func = 2 },
	{ .pin = 3, .func = 2 },
	{ .pin = 6, .func = 2 },
	{ .pin = 7, .func = 2 },
};

static const struct rza1_bidir_pin rza1l_bidir_pins_p5[] = {
	{ .pin = 0, .func = 1 },
	{ .pin = 1, .func = 1 },
	{ .pin = 2, .func = 1 },
	{ .pin = 3, .func = 1 },
	{ .pin = 4, .func = 1 },
	{ .pin = 5, .func = 1 },
	{ .pin = 6, .func = 1 },
	{ .pin = 7, .func = 1 },
	{ .pin = 8, .func = 1 },
	{ .pin = 9, .func = 1 },
	{ .pin = 10, .func = 1 },
	{ .pin = 11, .func = 1 },
	{ .pin = 12, .func = 1 },
	{ .pin = 13, .func = 1 },
	{ .pin = 14, .func = 1 },
	{ .pin = 15, .func = 1 },
	{ .pin = 0, .func = 2 },
	{ .pin = 1, .func = 2 },
	{ .pin = 2, .func = 2 },
	{ .pin = 3, .func = 2 },
};

static const struct rza1_bidir_pin rza1l_bidir_pins_p6[] = {
	{ .pin = 0, .func = 1 },
	{ .pin = 1, .func = 1 },
	{ .pin = 2, .func = 1 },
	{ .pin = 3, .func = 1 },
	{ .pin = 4, .func = 1 },
	{ .pin = 5, .func = 1 },
	{ .pin = 6, .func = 1 },
	{ .pin = 7, .func = 1 },
	{ .pin = 8, .func = 1 },
	{ .pin = 9, .func = 1 },
	{ .pin = 10, .func = 1 },
	{ .pin = 11, .func = 1 },
	{ .pin = 12, .func = 1 },
	{ .pin = 13, .func = 1 },
	{ .pin = 14, .func = 1 },
	{ .pin = 15, .func = 1 },
};

static const struct rza1_bidir_pin rza1l_bidir_pins_p7[] = {
	{ .pin = 2, .func = 2 },
	{ .pin = 3, .func = 2 },
	{ .pin = 5, .func = 2 },
	{ .pin = 6, .func = 2 },
	{ .pin = 7, .func = 2 },
	{ .pin = 2, .func = 3 },
	{ .pin = 3, .func = 3 },
	{ .pin = 5, .func = 3 },
	{ .pin = 6, .func = 3 },
	{ .pin = 7, .func = 3 },
};

static const struct rza1_bidir_pin rza1l_bidir_pins_p9[] = {
	{ .pin = 1, .func = 2 },
	{ .pin = 0, .func = 3 },
	{ .pin = 1, .func = 3 },
	{ .pin = 3, .func = 3 },
	{ .pin = 4, .func = 3 },
	{ .pin = 5, .func = 3 },
};

static const struct rza1_swio_pin rza1l_swio_pins[] = {
	{ .port = 2, .pin = 8, .func = 2, .input = 0 },
	{ .port = 5, .pin = 6, .func = 3, .input = 0 },
	{ .port = 6, .pin = 6, .func = 3, .input = 0 },
	{ .port = 6, .pin = 10, .func = 3, .input = 0 },
	{ .port = 7, .pin = 10, .func = 2, .input = 0 },
	{ .port = 8, .pin = 2, .func = 3, .input = 0 },
};

static const struct rza1_bidir_entry rza1l_bidir_entries[RZA1_NPORTS] = {
	[1] = { ARRAY_SIZE(rza1l_bidir_pins_p1), rza1l_bidir_pins_p1 },
	[3] = { ARRAY_SIZE(rza1l_bidir_pins_p3), rza1l_bidir_pins_p3 },
	[4] = { ARRAY_SIZE(rza1l_bidir_pins_p4), rza1l_bidir_pins_p4 },
	[5] = { ARRAY_SIZE(rza1l_bidir_pins_p4), rza1l_bidir_pins_p5 },
	[6] = { ARRAY_SIZE(rza1l_bidir_pins_p6), rza1l_bidir_pins_p6 },
	[7] = { ARRAY_SIZE(rza1l_bidir_pins_p7), rza1l_bidir_pins_p7 },
	[9] = { ARRAY_SIZE(rza1l_bidir_pins_p9), rza1l_bidir_pins_p9 },
};

static const struct rza1_swio_entry rza1l_swio_entries[] = {
	[0] = { ARRAY_SIZE(rza1h_swio_pins), rza1h_swio_pins },
};

/* RZ/A1L (r7s72102x) pinmux flags table */
static const struct rza1_pinmux_conf rza1l_pmx_conf = {
	.bidir_entries	= rza1l_bidir_entries,
	.swio_entries	= rza1l_swio_entries,
};

/* ----------------------------------------------------------------------------
 * RZ/A1 types
 */
/**
 * rza1_mux_conf - describes a pin multiplexing operation
 *
 * @id: the pin identifier from 0 to RZA1_NPINS
 * @port: the port where pin sits on
 * @pin: pin id
 * @mux_func: alternate function id number
 * @mux_flags: alternate function flags
 * @value: output value to set the pin to
 */
struct rza1_mux_conf {
	u16 id;
	u8 port;
	u8 pin;
	u8 mux_func;
	u8 mux_flags;
	u8 value;
};

/**
 * rza1_port - describes a pin port
 *
 * This is mostly useful to lock register writes per-bank and not globally.
 *
 * @lock: protect access to HW registers
 * @id: port number
 * @base: logical address base
 * @pins: pins sitting on this port
 */
struct rza1_port {
	spinlock_t lock;
	unsigned int id;
	void __iomem *base;
	struct pinctrl_pin_desc *pins;
};

/**
 * rza1_pinctrl - RZ pincontroller device
 *
 * @dev: parent device structure
 * @mutex: protect [pinctrl|pinmux]_generic functions
 * @base: logical address base
 * @nports: number of pin controller ports
 * @ports: pin controller banks
 * @pins: pin array for pinctrl core
 * @desc: pincontroller desc for pinctrl core
 * @pctl: pinctrl device
 * @data: device specific data
 */
struct rza1_pinctrl {
	struct device *dev;

	struct mutex mutex;

	void __iomem *base;

	unsigned int nport;
	struct rza1_port *ports;

	struct pinctrl_pin_desc *pins;
	struct pinctrl_desc desc;
	struct pinctrl_dev *pctl;

	const void *data;
};

/* ----------------------------------------------------------------------------
 * RZ/A1 pinmux flags
 */
static inline bool rza1_pinmux_get_bidir(unsigned int port,
					 unsigned int pin,
					 unsigned int func,
					 const struct rza1_bidir_entry *table)
{
	const struct rza1_bidir_entry *entry = &table[port];
	const struct rza1_bidir_pin *bidir_pin;
	unsigned int i;

	for (i = 0; i < entry->npins; ++i) {
		bidir_pin = &entry->pins[i];
		if (bidir_pin->pin == pin && bidir_pin->func == func)
			return true;
	}

	return false;
}

static inline int rza1_pinmux_get_swio(unsigned int port,
				       unsigned int pin,
				       unsigned int func,
				       const struct rza1_swio_entry *table)
{
	const struct rza1_swio_pin *swio_pin;
	unsigned int i;


	for (i = 0; i < table->npins; ++i) {
		swio_pin = &table->pins[i];
		if (swio_pin->port == port && swio_pin->pin == pin &&
		    swio_pin->func == func)
			return swio_pin->input;
	}

	return -ENOENT;
}

/**
 * rza1_pinmux_get_flags() - return pinmux flags associated to a pin
 */
static unsigned int rza1_pinmux_get_flags(unsigned int port, unsigned int pin,
					  unsigned int func,
					  struct rza1_pinctrl *rza1_pctl)

{
	const struct rza1_pinmux_conf *pmx_conf = rza1_pctl->data;
	const struct rza1_bidir_entry *bidir_entries = pmx_conf->bidir_entries;
	const struct rza1_swio_entry *swio_entries = pmx_conf->swio_entries;
	unsigned int pmx_flags = 0;
	int ret;

	if (rza1_pinmux_get_bidir(port, pin, func, bidir_entries))
		pmx_flags |= MUX_FLAGS_BIDIR;

	ret = rza1_pinmux_get_swio(port, pin, func, swio_entries);
	if (ret == 0)
		pmx_flags |= MUX_FLAGS_SWIO_OUTPUT;
	else if (ret > 0)
		pmx_flags |= MUX_FLAGS_SWIO_INPUT;

	return pmx_flags;
}

/* ----------------------------------------------------------------------------
 * RZ/A1 SoC operations
 */

/**
 * rza1_set_bit() - un-locked set/clear a single bit in pin configuration
 *		    registers
 */
static inline void rza1_set_bit(struct rza1_port *port, unsigned int reg,
				unsigned int bit, bool set)
{
	void __iomem *mem = RZA1_ADDR(port->base, reg, port->id);
	u16 val = ioread16(mem);

	if (set)
		val |= BIT(bit);
	else
		val &= ~BIT(bit);

	iowrite16(val, mem);
}

static inline unsigned int rza1_get_bit(struct rza1_port *port,
					unsigned int reg, unsigned int bit)
{
	void __iomem *mem = RZA1_ADDR(port->base, reg, port->id);

	return ioread16(mem) & BIT(bit);
}

/**
 * rza1_pin_reset() - reset a pin to default initial state
 *
 * Reset pin state disabling input buffer and bi-directional control,
 * and configure it as input port.
 * Note that pin is now configured with direction as input but with input
 * buffer disabled. This implies the pin value cannot be read in this state.
 *
 * @port: port where pin sits on
 * @pin: pin offset
 */
static void rza1_pin_reset(struct rza1_port *port, unsigned int pin)
{
	unsigned long irqflags;

	spin_lock_irqsave(&port->lock, irqflags);
	rza1_set_bit(port, RZA1_PIBC_REG, pin, 0);
	rza1_set_bit(port, RZA1_PBDC_REG, pin, 0);

	rza1_set_bit(port, RZA1_PM_REG, pin, 1);
	rza1_set_bit(port, RZA1_PMC_REG, pin, 0);
	rza1_set_bit(port, RZA1_PIPC_REG, pin, 0);
	spin_unlock_irqrestore(&port->lock, irqflags);
}

/**
 * rza1_pin_set_direction() - set I/O direction on a pin in port mode
 *
 * When running in output port mode keep PBDC enabled to allow reading the
 * pin value from PPR.
 *
 * @port: port where pin sits on
 * @pin: pin offset
 * @input: input enable/disable flag
 */
static inline void rza1_pin_set_direction(struct rza1_port *port,
					  unsigned int pin, bool input)
{
	unsigned long irqflags;

	spin_lock_irqsave(&port->lock, irqflags);

	rza1_set_bit(port, RZA1_PIBC_REG, pin, 1);
	if (input) {
		rza1_set_bit(port, RZA1_PM_REG, pin, 1);
		rza1_set_bit(port, RZA1_PBDC_REG, pin, 0);
	} else {
		rza1_set_bit(port, RZA1_PM_REG, pin, 0);
		rza1_set_bit(port, RZA1_PBDC_REG, pin, 1);
	}

	spin_unlock_irqrestore(&port->lock, irqflags);
}

static inline void rza1_pin_set(struct rza1_port *port, unsigned int pin,
				unsigned int value)
{
	unsigned long irqflags;

	spin_lock_irqsave(&port->lock, irqflags);
	rza1_set_bit(port, RZA1_P_REG, pin, !!value);
	spin_unlock_irqrestore(&port->lock, irqflags);
}

static inline int rza1_pin_get(struct rza1_port *port, unsigned int pin)
{
	return rza1_get_bit(port, RZA1_PPR_REG, pin);
}

/**
 * rza1_pin_mux_single() - configure pin multiplexing on a single pin
 *
 * @pinctrl: RZ/A1 pin controller device
 * @mux_conf: pin multiplexing descriptor
 */
static int rza1_pin_mux_single(struct rza1_pinctrl *rza1_pctl,
			       struct rza1_mux_conf *mux_conf)
{
	struct rza1_port *port = &rza1_pctl->ports[mux_conf->port];
	unsigned int pin = mux_conf->pin;
	u8 mux_func = mux_conf->mux_func;
	u8 mux_flags = mux_conf->mux_flags;
	u8 mux_flags_from_table;

	rza1_pin_reset(port, pin);

	/* SWIO pinmux flags coming from DT are high precedence */
	mux_flags_from_table = rza1_pinmux_get_flags(port->id, pin, mux_func,
						     rza1_pctl);
	if (mux_flags)
		mux_flags |= (mux_flags_from_table & MUX_FLAGS_BIDIR);
	else
		mux_flags = mux_flags_from_table;

	if (mux_flags & MUX_FLAGS_BIDIR)
		rza1_set_bit(port, RZA1_PBDC_REG, pin, 1);

	/*
	 * Enable alternate function mode and select it.
	 *
	 * Be careful here: the pin mux sub-nodes in device tree
	 * enumerate alternate functions from 1 to 8;
	 * subtract 1 before using macros to match registers configuration
	 * which expects numbers from 0 to 7 instead.
	 *
	 * ----------------------------------------------------
	 * Alternate mode selection table:
	 *
	 * PMC	PFC	PFCE	PFCAE	(mux_func - 1)
	 * 1	0	0	0	0
	 * 1	1	0	0	1
	 * 1	0	1	0	2
	 * 1	1	1	0	3
	 * 1	0	0	1	4
	 * 1	1	0	1	5
	 * 1	0	1	1	6
	 * 1	1	1	1	7
	 * ----------------------------------------------------
	 */
	mux_func -= 1;
	rza1_set_bit(port, RZA1_PFC_REG, pin, mux_func & MUX_FUNC_PFC_MASK);
	rza1_set_bit(port, RZA1_PFCE_REG, pin, mux_func & MUX_FUNC_PFCE_MASK);
	rza1_set_bit(port, RZA1_PFCEA_REG, pin, mux_func & MUX_FUNC_PFCEA_MASK);

	/*
	 * All alternate functions except a few need PIPCn = 1.
	 * If PIPCn has to stay disabled (SW IO mode), configure PMn according
	 * to I/O direction specified by pin configuration -after- PMC has been
	 * set to one.
	 */
	if (mux_flags & (MUX_FLAGS_SWIO_INPUT | MUX_FLAGS_SWIO_OUTPUT))
		rza1_set_bit(port, RZA1_PM_REG, pin,
			     mux_flags & MUX_FLAGS_SWIO_INPUT);
	else
		rza1_set_bit(port, RZA1_PIPC_REG, pin, 1);

	rza1_set_bit(port, RZA1_PMC_REG, pin, 1);

	return 0;
}

/* ----------------------------------------------------------------------------
 * gpio operations
 */

/**
 * rza1_gpio_request() - configure pin in port mode
 *
 * Configure a pin as gpio (port mode).
 * After reset, the pin is in input mode with input buffer disabled.
 * To use the pin as input or output, set_direction shall be called first
 *
 * @chip: gpio chip where the gpio sits on
 * @gpio: gpio offset
 */
static int rza1_gpio_request(struct gpio_chip *chip, unsigned int gpio)
{
	struct rza1_port *port = gpiochip_get_data(chip);

	rza1_pin_reset(port, gpio);

	return 0;
}

/**
 * rza1_gpio_disable_free() - reset a pin
 *
 * Surprisingly, disable_free a gpio, is equivalent to request it.
 * Reset pin to port mode, with input buffer disabled. This overwrites all
 * port direction settings applied with set_direction
 *
 * @chip: gpio chip where the gpio sits on
 * @gpio: gpio offset
 */
static void rza1_gpio_free(struct gpio_chip *chip, unsigned int gpio)
{
	struct rza1_port *port = gpiochip_get_data(chip);

	rza1_pin_reset(port, gpio);
}

static int rza1_gpio_get_direction(struct gpio_chip *chip, unsigned int gpio)
{
	struct rza1_port *port = gpiochip_get_data(chip);

	return !!rza1_get_bit(port, RZA1_PM_REG, gpio);
}

static int rza1_gpio_direction_input(struct gpio_chip *chip,
				     unsigned int gpio)
{
	struct rza1_port *port = gpiochip_get_data(chip);

	rza1_pin_set_direction(port, gpio, true);

	return 0;
}

static int rza1_gpio_direction_output(struct gpio_chip *chip,
				      unsigned int gpio,
				      int value)
{
	struct rza1_port *port = gpiochip_get_data(chip);

	/* Set value before driving pin direction */
	rza1_pin_set(port, gpio, value);
	rza1_pin_set_direction(port, gpio, false);

	return 0;
}

/**
 * rza1_gpio_get() - read a gpio pin value
 *
 * Read gpio pin value through PPR register.
 * Requires bi-directional mode to work when reading the value of a pin
 * in output mode
 *
 * @chip: gpio chip where the gpio sits on
 * @gpio: gpio offset
 */
static int rza1_gpio_get(struct gpio_chip *chip, unsigned int gpio)
{
	struct rza1_port *port = gpiochip_get_data(chip);

	return rza1_pin_get(port, gpio);
}

static void rza1_gpio_set(struct gpio_chip *chip, unsigned int gpio,
			  int value)
{
	struct rza1_port *port = gpiochip_get_data(chip);

	rza1_pin_set(port, gpio, value);
}

static const struct gpio_chip rza1_gpiochip_template = {
	.request		= rza1_gpio_request,
	.free			= rza1_gpio_free,
	.get_direction		= rza1_gpio_get_direction,
	.direction_input	= rza1_gpio_direction_input,
	.direction_output	= rza1_gpio_direction_output,
	.get			= rza1_gpio_get,
	.set			= rza1_gpio_set,
};
/* ----------------------------------------------------------------------------
 * pinctrl operations
 */

/**
 * rza1_dt_node_pin_count() - Count number of pins in a dt node or in all its
 *			      children sub-nodes
 *
 * @np: device tree node to parse
 */
static int rza1_dt_node_pin_count(struct device_node *np)
{
	struct device_node *child;
	struct property *of_pins;
	unsigned int npins;

	of_pins = of_find_property(np, "pinmux", NULL);
	if (of_pins)
		return of_pins->length / sizeof(u32);

	npins = 0;
	for_each_child_of_node(np, child) {
		of_pins = of_find_property(child, "pinmux", NULL);
		if (!of_pins) {
			of_node_put(child);
			return -EINVAL;
		}

		npins += of_pins->length / sizeof(u32);
	}

	return npins;
}

/**
 * rza1_parse_pmx_function() - parse a pin mux sub-node
 *
 * @rza1_pctl: RZ/A1 pin controller device
 * @np: of pmx sub-node
 * @mux_confs: array of pin mux configurations to fill with parsed info
 * @grpins: array of pin ids to mux
 */
static int rza1_parse_pinmux_node(struct rza1_pinctrl *rza1_pctl,
				  struct device_node *np,
				  struct rza1_mux_conf *mux_confs,
				  unsigned int *grpins)
{
	struct pinctrl_dev *pctldev = rza1_pctl->pctl;
	char const *prop_name = "pinmux";
	unsigned long *pin_configs;
	unsigned int npin_configs;
	struct property *of_pins;
	unsigned int npins;
	u8 pinmux_flags;
	unsigned int i;
	int ret;

	of_pins = of_find_property(np, prop_name, NULL);
	if (!of_pins) {
		dev_dbg(rza1_pctl->dev, "Missing %s property\n", prop_name);
		return -ENOENT;
	}
	npins = of_pins->length / sizeof(u32);

	/*
	 * Collect pin configuration properties: they apply to all pins in
	 * this sub-node
	 */
	ret = pinconf_generic_parse_dt_config(np, pctldev, &pin_configs,
					      &npin_configs);
	if (ret) {
		dev_err(rza1_pctl->dev,
			"Unable to parse pin configuration options for %pOFn\n",
			np);
		return ret;
	}

	/*
	 * Create a mask with pinmux flags from pin configuration;
	 * very few pins (TIOC[0-4][A|B|C|D] require SWIO direction
	 * specified in device tree.
	 */
	pinmux_flags = 0;
	for (i = 0; i < npin_configs && pinmux_flags == 0; i++)
		switch (pinconf_to_config_param(pin_configs[i])) {
		case PIN_CONFIG_INPUT_ENABLE:
			pinmux_flags |= MUX_FLAGS_SWIO_INPUT;
			break;
		case PIN_CONFIG_OUTPUT:
			pinmux_flags |= MUX_FLAGS_SWIO_OUTPUT;
		default:
			break;

		}

	kfree(pin_configs);

	/* Collect pin positions and their mux settings. */
	for (i = 0; i < npins; ++i) {
		u32 of_pinconf;
		struct rza1_mux_conf *mux_conf = &mux_confs[i];

		ret = of_property_read_u32_index(np, prop_name, i, &of_pinconf);
		if (ret)
			return ret;

		mux_conf->id		= of_pinconf & MUX_PIN_ID_MASK;
		mux_conf->port		= RZA1_PIN_ID_TO_PORT(mux_conf->id);
		mux_conf->pin		= RZA1_PIN_ID_TO_PIN(mux_conf->id);
		mux_conf->mux_func	= MUX_FUNC(of_pinconf);
		mux_conf->mux_flags	= pinmux_flags;

		if (mux_conf->port >= RZA1_NPORTS ||
		    mux_conf->pin >= RZA1_PINS_PER_PORT) {
			dev_err(rza1_pctl->dev,
				"Wrong port %u pin %u for %s property\n",
				mux_conf->port, mux_conf->pin, prop_name);
			return -EINVAL;
		}

		grpins[i] = mux_conf->id;
	}

	return npins;
}

/**
 * rza1_dt_node_to_map() - map a pin mux node to a function/group
 *
 * Parse and register a pin mux function.
 *
 * @pctldev: pin controller device
 * @np: device tree node to parse
 * @map: pointer to pin map (output)
 * @num_maps: number of collected maps (output)
 */
static int rza1_dt_node_to_map(struct pinctrl_dev *pctldev,
			       struct device_node *np,
			       struct pinctrl_map **map,
			       unsigned int *num_maps)
{
	struct rza1_pinctrl *rza1_pctl = pinctrl_dev_get_drvdata(pctldev);
	struct rza1_mux_conf *mux_confs, *mux_conf;
	unsigned int *grpins, *grpin;
	struct device_node *child;
	const char *grpname;
	const char **fngrps;
	int ret, npins;
	int gsel, fsel;

	npins = rza1_dt_node_pin_count(np);
	if (npins < 0) {
		dev_err(rza1_pctl->dev, "invalid pinmux node structure\n");
		return -EINVAL;
	}

	/*
	 * Functions are made of 1 group only;
	 * in fact, functions and groups are identical for this pin controller
	 * except that functions carry an array of per-pin mux configuration
	 * settings.
	 */
	mux_confs = devm_kcalloc(rza1_pctl->dev, npins, sizeof(*mux_confs),
				 GFP_KERNEL);
	grpins = devm_kcalloc(rza1_pctl->dev, npins, sizeof(*grpins),
			      GFP_KERNEL);
	fngrps = devm_kzalloc(rza1_pctl->dev, sizeof(*fngrps), GFP_KERNEL);

	if (!mux_confs || !grpins || !fngrps)
		return -ENOMEM;

	/*
	 * Parse the pinmux node.
	 * If the node does not contain "pinmux" property (-ENOENT)
	 * that property shall be specified in all its children sub-nodes.
	 */
	mux_conf = &mux_confs[0];
	grpin = &grpins[0];

	ret = rza1_parse_pinmux_node(rza1_pctl, np, mux_conf, grpin);
	if (ret == -ENOENT)
		for_each_child_of_node(np, child) {
			ret = rza1_parse_pinmux_node(rza1_pctl, child, mux_conf,
						     grpin);
			if (ret < 0) {
				of_node_put(child);
				return ret;
			}

			grpin += ret;
			mux_conf += ret;
		}
	else if (ret < 0)
		return ret;

	/* Register pin group and function name to pinctrl_generic */
	grpname	= np->name;
	fngrps[0] = grpname;

	mutex_lock(&rza1_pctl->mutex);
	gsel = pinctrl_generic_add_group(pctldev, grpname, grpins, npins,
					 NULL);
	if (gsel < 0) {
		mutex_unlock(&rza1_pctl->mutex);
		return gsel;
	}

	fsel = pinmux_generic_add_function(pctldev, grpname, fngrps, 1,
					   mux_confs);
	if (fsel < 0) {
		ret = fsel;
		goto remove_group;
	}

	dev_info(rza1_pctl->dev, "Parsed function and group %s with %d pins\n",
				 grpname, npins);

	/* Create map where to retrieve function and mux settings from */
	*num_maps = 0;
	*map = kzalloc(sizeof(**map), GFP_KERNEL);
	if (!*map) {
		ret = -ENOMEM;
		goto remove_function;
	}

	(*map)->type = PIN_MAP_TYPE_MUX_GROUP;
	(*map)->data.mux.group = np->name;
	(*map)->data.mux.function = np->name;
	*num_maps = 1;
	mutex_unlock(&rza1_pctl->mutex);

	return 0;

remove_function:
	pinmux_generic_remove_function(pctldev, fsel);

remove_group:
	pinctrl_generic_remove_group(pctldev, gsel);
	mutex_unlock(&rza1_pctl->mutex);

	dev_info(rza1_pctl->dev, "Unable to parse function and group %s\n",
				 grpname);

	return ret;
}

static void rza1_dt_free_map(struct pinctrl_dev *pctldev,
			     struct pinctrl_map *map, unsigned int num_maps)
{
	kfree(map);
}

static const struct pinctrl_ops rza1_pinctrl_ops = {
	.get_groups_count	= pinctrl_generic_get_group_count,
	.get_group_name		= pinctrl_generic_get_group_name,
	.get_group_pins		= pinctrl_generic_get_group_pins,
	.dt_node_to_map		= rza1_dt_node_to_map,
	.dt_free_map		= rza1_dt_free_map,
};

/* ----------------------------------------------------------------------------
 * pinmux operations
 */

/**
 * rza1_set_mux() - retrieve pins from a group and apply their mux settings
 *
 * @pctldev: pin controller device
 * @selector: function selector
 * @group: group selector
 */
static int rza1_set_mux(struct pinctrl_dev *pctldev, unsigned int selector,
			   unsigned int group)
{
	struct rza1_pinctrl *rza1_pctl = pinctrl_dev_get_drvdata(pctldev);
	struct rza1_mux_conf *mux_confs;
	struct function_desc *func;
	struct group_desc *grp;
	int i;

	grp = pinctrl_generic_get_group(pctldev, group);
	if (!grp)
		return -EINVAL;

	func = pinmux_generic_get_function(pctldev, selector);
	if (!func)
		return -EINVAL;

	mux_confs = (struct rza1_mux_conf *)func->data;
	for (i = 0; i < grp->num_pins; ++i) {
		int ret;

		ret = rza1_pin_mux_single(rza1_pctl, &mux_confs[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pinmux_ops rza1_pinmux_ops = {
	.get_functions_count	= pinmux_generic_get_function_count,
	.get_function_name	= pinmux_generic_get_function_name,
	.get_function_groups	= pinmux_generic_get_function_groups,
	.set_mux		= rza1_set_mux,
	.strict			= true,
};

/* ----------------------------------------------------------------------------
 * RZ/A1 pin controller driver operations
 */

static unsigned int rza1_count_gpio_chips(struct device_node *np)
{
	struct device_node *child;
	unsigned int count = 0;

	for_each_child_of_node(np, child) {
		if (!of_property_read_bool(child, "gpio-controller"))
			continue;

		count++;
	}

	return count;
}

/**
 * rza1_parse_gpiochip() - parse and register a gpio chip and pin range
 *
 * The gpio controller subnode shall provide a "gpio-ranges" list property as
 * defined by gpio device tree binding documentation.
 *
 * @rza1_pctl: RZ/A1 pin controller device
 * @np: of gpio-controller node
 * @chip: gpio chip to register to gpiolib
 * @range: pin range to register to pinctrl core
 */
static int rza1_parse_gpiochip(struct rza1_pinctrl *rza1_pctl,
			       struct device_node *np,
			       struct gpio_chip *chip,
			       struct pinctrl_gpio_range *range)
{
	const char *list_name = "gpio-ranges";
	struct of_phandle_args of_args;
	unsigned int gpioport;
	u32 pinctrl_base;
	int ret;

	ret = of_parse_phandle_with_fixed_args(np, list_name, 3, 0, &of_args);
	if (ret) {
		dev_err(rza1_pctl->dev, "Unable to parse %s list property\n",
			list_name);
		return ret;
	}

	/*
	 * Find out on which port this gpio-chip maps to by inspecting the
	 * second argument of the "gpio-ranges" property.
	 */
	pinctrl_base = of_args.args[1];
	gpioport = RZA1_PIN_ID_TO_PORT(pinctrl_base);
	if (gpioport >= RZA1_NPORTS) {
		dev_err(rza1_pctl->dev,
			"Invalid values in property %s\n", list_name);
		return -EINVAL;
	}

	*chip		= rza1_gpiochip_template;
	chip->base	= -1;
	chip->label	= devm_kasprintf(rza1_pctl->dev, GFP_KERNEL, "%pOFn",
					 np);
	if (!chip->label)
		return -ENOMEM;

	chip->ngpio	= of_args.args[2];
	chip->of_node	= np;
	chip->parent	= rza1_pctl->dev;

	range->id	= gpioport;
	range->name	= chip->label;
	range->pin_base	= range->base = pinctrl_base;
	range->npins	= of_args.args[2];
	range->gc	= chip;

	ret = devm_gpiochip_add_data(rza1_pctl->dev, chip,
				     &rza1_pctl->ports[gpioport]);
	if (ret)
		return ret;

	pinctrl_add_gpio_range(rza1_pctl->pctl, range);

	dev_dbg(rza1_pctl->dev, "Parsed gpiochip %s with %d pins\n",
		chip->label, chip->ngpio);

	return 0;
}

/**
 * rza1_gpio_register() - parse DT to collect gpio-chips and gpio-ranges
 *
 * @rza1_pctl: RZ/A1 pin controller device
 */
static int rza1_gpio_register(struct rza1_pinctrl *rza1_pctl)
{
	struct device_node *np = rza1_pctl->dev->of_node;
	struct pinctrl_gpio_range *gpio_ranges;
	struct gpio_chip *gpio_chips;
	struct device_node *child;
	unsigned int ngpiochips;
	unsigned int i;
	int ret;

	ngpiochips = rza1_count_gpio_chips(np);
	if (ngpiochips == 0) {
		dev_dbg(rza1_pctl->dev, "No gpiochip registered\n");
		return 0;
	}

	gpio_chips = devm_kcalloc(rza1_pctl->dev, ngpiochips,
				  sizeof(*gpio_chips), GFP_KERNEL);
	gpio_ranges = devm_kcalloc(rza1_pctl->dev, ngpiochips,
				   sizeof(*gpio_ranges), GFP_KERNEL);
	if (!gpio_chips || !gpio_ranges)
		return -ENOMEM;

	i = 0;
	for_each_child_of_node(np, child) {
		if (!of_property_read_bool(child, "gpio-controller"))
			continue;

		ret = rza1_parse_gpiochip(rza1_pctl, child, &gpio_chips[i],
					  &gpio_ranges[i]);
		if (ret) {
			of_node_put(child);
			return ret;
		}

		++i;
	}

	dev_info(rza1_pctl->dev, "Registered %u gpio controllers\n", i);

	return 0;
}

/**
 * rza1_pinctrl_register() - Enumerate pins, ports and gpiochips; register
 *			     them to pinctrl and gpio cores.
 *
 * @rza1_pctl: RZ/A1 pin controller device
 */
static int rza1_pinctrl_register(struct rza1_pinctrl *rza1_pctl)
{
	struct pinctrl_pin_desc *pins;
	struct rza1_port *ports;
	unsigned int i;
	int ret;

	pins = devm_kcalloc(rza1_pctl->dev, RZA1_NPINS, sizeof(*pins),
			    GFP_KERNEL);
	ports = devm_kcalloc(rza1_pctl->dev, RZA1_NPORTS, sizeof(*ports),
			     GFP_KERNEL);
	if (!pins || !ports)
		return -ENOMEM;

	rza1_pctl->pins		= pins;
	rza1_pctl->desc.pins	= pins;
	rza1_pctl->desc.npins	= RZA1_NPINS;
	rza1_pctl->ports	= ports;

	for (i = 0; i < RZA1_NPINS; ++i) {
		unsigned int pin = RZA1_PIN_ID_TO_PIN(i);
		unsigned int port = RZA1_PIN_ID_TO_PORT(i);

		pins[i].number = i;
		pins[i].name = devm_kasprintf(rza1_pctl->dev, GFP_KERNEL,
					      "P%u-%u", port, pin);
		if (!pins[i].name)
			return -ENOMEM;

		if (i % RZA1_PINS_PER_PORT == 0) {
			/*
			 * Setup ports;
			 * they provide per-port lock and logical base address.
			 */
			unsigned int port_id = RZA1_PIN_ID_TO_PORT(i);

			ports[port_id].id	= port_id;
			ports[port_id].base	= rza1_pctl->base;
			ports[port_id].pins	= &pins[i];
			spin_lock_init(&ports[port_id].lock);
		}
	}

	ret = devm_pinctrl_register_and_init(rza1_pctl->dev, &rza1_pctl->desc,
					     rza1_pctl, &rza1_pctl->pctl);
	if (ret) {
		dev_err(rza1_pctl->dev,
			"RZ/A1 pin controller registration failed\n");
		return ret;
	}

	ret = pinctrl_enable(rza1_pctl->pctl);
	if (ret) {
		dev_err(rza1_pctl->dev,
			"RZ/A1 pin controller failed to start\n");
		return ret;
	}

	ret = rza1_gpio_register(rza1_pctl);
	if (ret) {
		dev_err(rza1_pctl->dev, "RZ/A1 GPIO registration failed\n");
		return ret;
	}

	return 0;
}

static int rza1_pinctrl_probe(struct platform_device *pdev)
{
	struct rza1_pinctrl *rza1_pctl;
	int ret;

	rza1_pctl = devm_kzalloc(&pdev->dev, sizeof(*rza1_pctl), GFP_KERNEL);
	if (!rza1_pctl)
		return -ENOMEM;

	rza1_pctl->dev = &pdev->dev;

	rza1_pctl->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rza1_pctl->base))
		return PTR_ERR(rza1_pctl->base);

	mutex_init(&rza1_pctl->mutex);

	platform_set_drvdata(pdev, rza1_pctl);

	rza1_pctl->desc.name	= DRIVER_NAME;
	rza1_pctl->desc.pctlops	= &rza1_pinctrl_ops;
	rza1_pctl->desc.pmxops	= &rza1_pinmux_ops;
	rza1_pctl->desc.owner	= THIS_MODULE;
	rza1_pctl->data		= of_device_get_match_data(&pdev->dev);

	ret = rza1_pinctrl_register(rza1_pctl);
	if (ret)
		return ret;

	dev_info(&pdev->dev,
		 "RZ/A1 pin controller and gpio successfully registered\n");

	return 0;
}

static const struct of_device_id rza1_pinctrl_of_match[] = {
	{
		/* RZ/A1H, RZ/A1M */
		.compatible	= "renesas,r7s72100-ports",
		.data		= &rza1h_pmx_conf,
	},
	{
		/* RZ/A1L */
		.compatible	= "renesas,r7s72102-ports",
		.data		= &rza1l_pmx_conf,
	},
	{ }
};

static struct platform_driver rza1_pinctrl_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = rza1_pinctrl_of_match,
	},
	.probe = rza1_pinctrl_probe,
};

static int __init rza1_pinctrl_init(void)
{
	return platform_driver_register(&rza1_pinctrl_driver);
}
core_initcall(rza1_pinctrl_init);

MODULE_AUTHOR("Jacopo Mondi <jacopo+renesas@jmondi.org");
MODULE_DESCRIPTION("Pin and gpio controller driver for Reneas RZ/A1 SoC");
MODULE_LICENSE("GPL v2");
