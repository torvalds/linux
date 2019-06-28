/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2019 IBM Corp.  */

#ifndef ASPEED_PINMUX_H
#define ASPEED_PINMUX_H

#include <linux/regmap.h>
#include <stdbool.h>

/*
 * The ASPEED SoCs provide typically more than 200 pins for GPIO and other
 * functions. The SoC function enabled on a pin is determined on a priority
 * basis where a given pin can provide a number of different signal types.
 *
 * The signal active on a pin is described by both a priority level and
 * compound logical expressions involving multiple operators, registers and
 * bits. Some difficulty arises as the pin's function bit masks for each
 * priority level are frequently not the same (i.e. cannot just flip a bit to
 * change from a high to low priority signal), or even in the same register.
 * Further, not all signals can be unmuxed, as some expressions depend on
 * values in the hardware strapping register (which is treated as read-only).
 *
 * SoC Multi-function Pin Expression Examples
 * ------------------------------------------
 *
 * Here are some sample mux configurations from the AST2400 and AST2500
 * datasheets to illustrate the corner cases, roughly in order of least to most
 * corner. The signal priorities are in decending order from P0 (highest).
 *
 * D6 is a pin with a single function (beside GPIO); a high priority signal
 * that participates in one function:
 *
 * Ball | Default | P0 Signal | P0 Expression               | P1 Signal | P1 Expression | Other
 * -----+---------+-----------+-----------------------------+-----------+---------------+----------
 *  D6    GPIOA0    MAC1LINK    SCU80[0]=1                                                GPIOA0
 * -----+---------+-----------+-----------------------------+-----------+---------------+----------
 *
 * C5 is a multi-signal pin (high and low priority signals). Here we touch
 * different registers for the different functions that enable each signal:
 *
 * -----+---------+-----------+-----------------------------+-----------+---------------+----------
 *  C5    GPIOA4    SCL9        SCU90[22]=1                   TIMER5      SCU80[4]=1      GPIOA4
 * -----+---------+-----------+-----------------------------+-----------+---------------+----------
 *
 * E19 is a single-signal pin with two functions that influence the active
 * signal. In this case both bits have the same meaning - enable a dedicated
 * LPC reset pin. However it's not always the case that the bits in the
 * OR-relationship have the same meaning.
 *
 * -----+---------+-----------+-----------------------------+-----------+---------------+----------
 *  E19   GPIOB4    LPCRST#     SCU80[12]=1 | Strap[14]=1                                 GPIOB4
 * -----+---------+-----------+-----------------------------+-----------+---------------+----------
 *
 * For example, pin B19 has a low-priority signal that's enabled by two
 * distinct SoC functions: A specific SIOPBI bit in register SCUA4, and an ACPI
 * bit in the STRAP register. The ACPI bit configures signals on pins in
 * addition to B19. Both of the low priority functions as well as the high
 * priority function must be disabled for GPIOF1 to be used.
 *
 * Ball | Default | P0 Signal | P0 Expression                           | P1 Signal | P1 Expression                          | Other
 * -----+---------+-----------+-----------------------------------------+-----------+----------------------------------------+----------
 *  B19   GPIOF1    NDCD4       SCU80[25]=1                               SIOPBI#     SCUA4[12]=1 | Strap[19]=0                GPIOF1
 * -----+---------+-----------+-----------------------------------------+-----------+----------------------------------------+----------
 *
 * For pin E18, the SoC ANDs the expected state of three bits to determine the
 * pin's active signal:
 *
 * * SCU3C[3]: Enable external SOC reset function
 * * SCU80[15]: Enable SPICS1# or EXTRST# function pin
 * * SCU90[31]: Select SPI interface CS# output
 *
 * -----+---------+-----------+-----------------------------------------+-----------+----------------------------------------+----------
 *  E18   GPIOB7    EXTRST#     SCU3C[3]=1 & SCU80[15]=1 & SCU90[31]=0    SPICS1#     SCU3C[3]=1 & SCU80[15]=1 & SCU90[31]=1   GPIOB7
 * -----+---------+-----------+-----------------------------------------+-----------+----------------------------------------+----------
 *
 * (Bits SCU3C[3] and SCU80[15] appear to only be used in the expressions for
 * selecting the signals on pin E18)
 *
 * Pin T5 is a multi-signal pin with a more complex configuration:
 *
 * Ball | Default | P0 Signal | P0 Expression                | P1 Signal | P1 Expression | Other
 * -----+---------+-----------+------------------------------+-----------+---------------+----------
 *  T5    GPIOL1    VPIDE       SCU90[5:4]!=0 & SCU84[17]=1    NDCD1       SCU84[17]=1     GPIOL1
 * -----+---------+-----------+------------------------------+-----------+---------------+----------
 *
 * The high priority signal configuration is best thought of in terms of its
 * exploded form, with reference to the SCU90[5:4] bits:
 *
 * * SCU90[5:4]=00: disable
 * * SCU90[5:4]=01: 18 bits (R6/G6/B6) video mode.
 * * SCU90[5:4]=10: 24 bits (R8/G8/B8) video mode.
 * * SCU90[5:4]=11: 30 bits (R10/G10/B10) video mode.
 *
 * Re-writing:
 *
 * -----+---------+-----------+------------------------------+-----------+---------------+----------
 *  T5    GPIOL1    VPIDE      (SCU90[5:4]=1 & SCU84[17]=1)    NDCD1       SCU84[17]=1     GPIOL1
 *                             | (SCU90[5:4]=2 & SCU84[17]=1)
 *                             | (SCU90[5:4]=3 & SCU84[17]=1)
 * -----+---------+-----------+------------------------------+-----------+---------------+----------
 *
 * For reference the SCU84[17] bit configure the "UART1 NDCD1 or Video VPIDE
 * function pin", where the signal itself is determined by whether SCU94[5:4]
 * is disabled or in one of the 18, 24 or 30bit video modes.
 *
 * Other video-input-related pins require an explicit state in SCU90[5:4], e.g.
 * W1 and U5:
 *
 * -----+---------+-----------+------------------------------+-----------+---------------+----------
 *  W1    GPIOL6    VPIB0       SCU90[5:4]=3 & SCU84[22]=1     TXD1        SCU84[22]=1     GPIOL6
 *  U5    GPIOL7    VPIB1       SCU90[5:4]=3 & SCU84[23]=1     RXD1        SCU84[23]=1     GPIOL7
 * -----+---------+-----------+------------------------------+-----------+---------------+----------
 *
 * The examples of T5 and W1 are particularly fertile, as they also demonstrate
 * that despite operating as part of the video input bus each signal needs to
 * be enabled individually via it's own SCU84 (in the cases of T5 and W1)
 * register bit. This is a little crazy if the bus doesn't have optional
 * signals, but is used to decent effect with some of the UARTs where not all
 * signals are required. However, this isn't done consistently - UART1 is
 * enabled on a per-pin basis, and by contrast, all signals for UART6 are
 * enabled by a single bit.
 *
 * Further, the high and low priority signals listed in the table above share
 * a configuration bit. The VPI signals should operate in concert in a single
 * function, but the UART signals should retain the ability to be configured
 * independently. This pushes the implementation down the path of tagging a
 * signal's expressions with the function they participate in, rather than
 * defining masks affecting multiple signals per function. The latter approach
 * fails in this instance where applying the configuration for the UART pin of
 * interest will stomp on the state of other UART signals when disabling the
 * VPI functions on the current pin.
 *
 * Ball |  Default   | P0 Signal | P0 Expression             | P1 Signal | P1 Expression | Other
 * -----+------------+-----------+---------------------------+-----------+---------------+------------
 *  A12   RGMII1TXCK   GPIOT0      SCUA0[0]=1                  RMII1TXEN   Strap[6]=0      RGMII1TXCK
 *  B12   RGMII1TXCTL  GPIOT1      SCUA0[1]=1                  –           Strap[6]=0      RGMII1TXCTL
 * -----+------------+-----------+---------------------------+-----------+---------------+------------
 *
 * A12 demonstrates that the "Other" signal isn't always GPIO - in this case
 * GPIOT0 is a high-priority signal and RGMII1TXCK is Other. Thus, GPIO
 * should be treated like any other signal type with full function expression
 * requirements, and not assumed to be the default case. Separately, GPIOT0 and
 * GPIOT1's signal descriptor bits are distinct, therefore we must iterate all
 * pins in the function's group to disable the higher-priority signals such
 * that the signal for the function of interest is correctly enabled.
 *
 * Finally, three priority levels aren't always enough; the AST2500 brings with
 * it 18 pins of five priority levels, however the 18 pins only use three of
 * the five priority levels.
 *
 * Ultimately the requirement to control pins in the examples above drive the
 * design:
 *
 * * Pins provide signals according to functions activated in the mux
 *   configuration
 *
 * * Pins provide up to five signal types in a priority order
 *
 * * For priorities levels defined on a pin, each priority provides one signal
 *
 * * Enabling lower priority signals requires higher priority signals be
 *   disabled
 *
 * * A function represents a set of signals; functions are distinct if their
 *   sets of signals are not equal
 *
 * * Signals participate in one or more functions
 *
 * * A function is described by an expression of one or more signal
 *   descriptors, which compare bit values in a register
 *
 * * A signal expression is the smallest set of signal descriptors whose
 *   comparisons must evaluate 'true' for a signal to be enabled on a pin.
 *
 * * A function's signal is active on a pin if evaluating all signal
 *   descriptors in the pin's signal expression for the function yields a 'true'
 *   result
 *
 * * A signal at a given priority on a given pin is active if any of the
 *   functions in which the signal participates are active, and no higher
 *   priority signal on the pin is active
 *
 * * GPIO is configured per-pin
 *
 * And so:
 *
 * * To disable a signal, any function(s) activating the signal must be
 *   disabled
 *
 * * Each pin must know the signal expressions of functions in which it
 *   participates, for the purpose of enabling the Other function. This is done
 *   by deactivating all functions that activate higher priority signals on the
 *   pin.
 *
 * As a concrete example:
 *
 * * T5 provides three signals types: VPIDE, NDCD1 and GPIO
 *
 * * The VPIDE signal participates in 3 functions: VPI18, VPI24 and VPI30
 *
 * * The NDCD1 signal participates in just its own NDCD1 function
 *
 * * VPIDE is high priority, NDCD1 is low priority, and GPIOL1 is the least
 *   prioritised
 *
 * * The prerequisit for activating the NDCD1 signal is that the VPI18, VPI24
 *   and VPI30 functions all be disabled
 *
 * * Similarly, all of VPI18, VPI24, VPI30 and NDCD1 functions must be disabled
 *   to provide GPIOL6
 *
 * Considerations
 * --------------
 *
 * If pinctrl allows us to allocate a pin we can configure a function without
 * concern for the function of already allocated pins, if pin groups are
 * created with respect to the SoC functions in which they participate. This is
 * intuitive, but it did not feel obvious from the bit/pin relationships.
 *
 * Conversely, failing to allocate all pins in a group indicates some bits (as
 * well as pins) required for the group's configuration will already be in use,
 * likely in a way that's inconsistent with the requirements of the failed
 * group.
 */

#define ASPEED_IP_SCU		0
#define ASPEED_IP_GFX		1
#define ASPEED_IP_LPC		2
#define ASPEED_NR_PINMUX_IPS	3

 /**
  * A signal descriptor, which describes the register, bits and the
  * enable/disable values that should be compared or written.
  *
  * @ip: The IP block identifier, used as an index into the regmap array in
  *      struct aspeed_pinctrl_data
  * @reg: The register offset with respect to the base address of the IP block
  * @mask: The mask to apply to the register. The lowest set bit of the mask is
  *        used to derive the shift value.
  * @enable: The value that enables the function. Value should be in the LSBs,
  *          not at the position of the mask.
  * @disable: The value that disables the function. Value should be in the
  *           LSBs, not at the position of the mask.
  */
struct aspeed_sig_desc {
	unsigned int ip;
	unsigned int reg;
	u32 mask;
	u32 enable;
	u32 disable;
};

/**
 * Describes a signal expression. The expression is evaluated by ANDing the
 * evaluation of the descriptors.
 *
 * @signal: The signal name for the priority level on the pin. If the signal
 *          type is GPIO, then the signal name must begin with the string
 *          "GPIO", e.g. GPIOA0, GPIOT4 etc.
 * @function: The name of the function the signal participates in for the
 *            associated expression
 * @ndescs: The number of signal descriptors in the expression
 * @descs: Pointer to an array of signal descriptors that comprise the
 *         function expression
 */
struct aspeed_sig_expr {
	const char *signal;
	const char *function;
	int ndescs;
	const struct aspeed_sig_desc *descs;
};

/**
 * A struct capturing the list of expressions enabling signals at each priority
 * for a given pin. The signal configuration for a priority level is evaluated
 * by ORing the evaluation of the signal expressions in the respective
 * priority's list.
 *
 * @name: A name for the pin
 * @prios: A pointer to an array of expression list pointers
 *
 */
struct aspeed_pin_desc {
	const char *name;
	const struct aspeed_sig_expr ***prios;
};

/* Macro hell */

#define SIG_DESC_IP_BIT(ip, reg, idx, val) \
	{ ip, reg, BIT_MASK(idx), val, (((val) + 1) & 1) }

/**
 * Short-hand macro for describing an SCU descriptor enabled by the state of
 * one bit. The disable value is derived.
 *
 * @reg: The signal's associated register, offset from base
 * @idx: The signal's bit index in the register
 * @val: The value (0 or 1) that enables the function
 */
#define SIG_DESC_BIT(reg, idx, val) \
	SIG_DESC_IP_BIT(ASPEED_IP_SCU, reg, idx, val)

#define SIG_DESC_IP_SET(ip, reg, idx) SIG_DESC_IP_BIT(ip, reg, idx, 1)

/**
 * A further short-hand macro expanding to an SCU descriptor enabled by a set
 * bit.
 *
 * @reg: The register, offset from base
 * @idx: The bit index in the register
 */
#define SIG_DESC_SET(reg, idx) SIG_DESC_IP_BIT(ASPEED_IP_SCU, reg, idx, 1)

#define SIG_DESC_LIST_SYM(sig, func) sig_descs_ ## sig ## _ ## func
#define SIG_DESC_LIST_DECL(sig, func, ...) \
	static const struct aspeed_sig_desc SIG_DESC_LIST_SYM(sig, func)[] = \
		{ __VA_ARGS__ }

#define SIG_EXPR_SYM(sig, func) sig_expr_ ## sig ## _ ## func
#define SIG_EXPR_DECL_(sig, func) \
	static const struct aspeed_sig_expr SIG_EXPR_SYM(sig, func) = \
	{ \
		.signal = #sig, \
		.function = #func, \
		.ndescs = ARRAY_SIZE(SIG_DESC_LIST_SYM(sig, func)), \
		.descs = &(SIG_DESC_LIST_SYM(sig, func))[0], \
	}

/**
 * Declare a signal expression.
 *
 * @sig: A macro symbol name for the signal (is subjected to stringification
 *        and token pasting)
 * @func: The function in which the signal is participating
 * @...: Signal descriptors that define the signal expression
 *
 * For example, the following declares the ROMD8 signal for the ROM16 function:
 *
 *     SIG_EXPR_DECL(ROMD8, ROM16, SIG_DESC_SET(SCU90, 6));
 *
 * And with multiple signal descriptors:
 *
 *     SIG_EXPR_DECL(ROMD8, ROM16S, SIG_DESC_SET(HW_STRAP1, 4),
 *              { HW_STRAP1, GENMASK(1, 0), 0, 0 });
 */
#define SIG_EXPR_DECL(sig, func, ...) \
	SIG_DESC_LIST_DECL(sig, func, __VA_ARGS__); \
	SIG_EXPR_DECL_(sig, func)

/**
 * Declare a pointer to a signal expression
 *
 * @sig: The macro symbol name for the signal (subjected to token pasting)
 * @func: The macro symbol name for the function (subjected to token pasting)
 */
#define SIG_EXPR_PTR(sig, func) (&SIG_EXPR_SYM(sig, func))

#define SIG_EXPR_LIST_SYM(sig) sig_exprs_ ## sig

/**
 * Declare a signal expression list for reference in a struct aspeed_pin_prio.
 *
 * @sig: A macro symbol name for the signal (is subjected to token pasting)
 * @...: Signal expression structure pointers (use SIG_EXPR_PTR())
 *
 * For example, the 16-bit ROM bus can be enabled by one of two possible signal
 * expressions:
 *
 *     SIG_EXPR_DECL(ROMD8, ROM16, SIG_DESC_SET(SCU90, 6));
 *     SIG_EXPR_DECL(ROMD8, ROM16S, SIG_DESC_SET(HW_STRAP1, 4),
 *              { HW_STRAP1, GENMASK(1, 0), 0, 0 });
 *     SIG_EXPR_LIST_DECL(ROMD8, SIG_EXPR_PTR(ROMD8, ROM16),
 *              SIG_EXPR_PTR(ROMD8, ROM16S));
 */
#define SIG_EXPR_LIST_DECL(sig, ...) \
	static const struct aspeed_sig_expr *SIG_EXPR_LIST_SYM(sig)[] = \
		{ __VA_ARGS__, NULL }

/**
 * A short-hand macro for declaring a function expression and an expression
 * list with a single function.
 *
 * @func: A macro symbol name for the function (is subjected to token pasting)
 * @...: Function descriptors that define the function expression
 *
 * For example, signal NCTS6 participates in its own function with one group:
 *
 *     SIG_EXPR_LIST_DECL_SINGLE(NCTS6, NCTS6, SIG_DESC_SET(SCU90, 7));
 */
#define SIG_EXPR_LIST_DECL_SINGLE(sig, func, ...) \
	SIG_DESC_LIST_DECL(sig, func, __VA_ARGS__); \
	SIG_EXPR_DECL_(sig, func); \
	SIG_EXPR_LIST_DECL(sig, SIG_EXPR_PTR(sig, func))

#define SIG_EXPR_LIST_DECL_DUAL(sig, f0, f1) \
	SIG_EXPR_LIST_DECL(sig, SIG_EXPR_PTR(sig, f0), SIG_EXPR_PTR(sig, f1))

#define SIG_EXPR_LIST_PTR(sig) (&SIG_EXPR_LIST_SYM(sig)[0])

#define PIN_EXPRS_SYM(pin) pin_exprs_ ## pin
#define PIN_EXPRS_PTR(pin) (&PIN_EXPRS_SYM(pin)[0])
#define PIN_SYM(pin) pin_ ## pin

#define MS_PIN_DECL_(pin, ...) \
	static const struct aspeed_sig_expr **PIN_EXPRS_SYM(pin)[] = \
		{ __VA_ARGS__, NULL }; \
	static const struct aspeed_pin_desc PIN_SYM(pin) = \
		{ #pin, PIN_EXPRS_PTR(pin) }

/**
 * Declare a multi-signal pin
 *
 * @pin: The pin number
 * @other: Macro name for "other" functionality (subjected to stringification)
 * @high: Macro name for the highest priority signal functions
 * @low: Macro name for the low signal functions
 *
 * For example:
 *
 *     #define A8 56
 *     SIG_EXPR_DECL(ROMD8, ROM16, SIG_DESC_SET(SCU90, 6));
 *     SIG_EXPR_DECL(ROMD8, ROM16S, SIG_DESC_SET(HW_STRAP1, 4),
 *              { HW_STRAP1, GENMASK(1, 0), 0, 0 });
 *     SIG_EXPR_LIST_DECL(ROMD8, SIG_EXPR_PTR(ROMD8, ROM16),
 *              SIG_EXPR_PTR(ROMD8, ROM16S));
 *     SIG_EXPR_LIST_DECL_SINGLE(NCTS6, NCTS6, SIG_DESC_SET(SCU90, 7));
 *     MS_PIN_DECL(A8, GPIOH0, ROMD8, NCTS6);
 */
#define MS_PIN_DECL(pin, other, high, low) \
	SIG_EXPR_LIST_DECL_SINGLE(other, other); \
	MS_PIN_DECL_(pin, \
			SIG_EXPR_LIST_PTR(high), \
			SIG_EXPR_LIST_PTR(low), \
			SIG_EXPR_LIST_PTR(other))

#define PIN_GROUP_SYM(func) pins_ ## func
#define FUNC_GROUP_SYM(func) groups_ ## func
#define FUNC_GROUP_DECL(func, ...) \
	static const int PIN_GROUP_SYM(func)[] = { __VA_ARGS__ }; \
	static const char *FUNC_GROUP_SYM(func)[] = { #func }

/**
 * Declare a single signal pin
 *
 * @pin: The pin number
 * @other: Macro name for "other" functionality (subjected to stringification)
 * @sig: Macro name for the signal (subjected to stringification)
 *
 * For example:
 *
 *     #define E3 80
 *     SIG_EXPR_LIST_DECL_SINGLE(SCL5, I2C5, I2C5_DESC);
 *     SS_PIN_DECL(E3, GPIOK0, SCL5);
 */
#define SS_PIN_DECL(pin, other, sig) \
	SIG_EXPR_LIST_DECL_SINGLE(other, other); \
	MS_PIN_DECL_(pin, SIG_EXPR_LIST_PTR(sig), SIG_EXPR_LIST_PTR(other))

/**
 * Single signal, single function pin declaration
 *
 * @pin: The pin number
 * @other: Macro name for "other" functionality (subjected to stringification)
 * @sig: Macro name for the signal (subjected to stringification)
 * @...: Signal descriptors that define the function expression
 *
 * For example:
 *
 *    SSSF_PIN_DECL(A4, GPIOA2, TIMER3, SIG_DESC_SET(SCU80, 2));
 */
#define SSSF_PIN_DECL(pin, other, sig, ...) \
	SIG_EXPR_LIST_DECL_SINGLE(sig, sig, __VA_ARGS__); \
	SIG_EXPR_LIST_DECL_SINGLE(other, other); \
	MS_PIN_DECL_(pin, SIG_EXPR_LIST_PTR(sig), SIG_EXPR_LIST_PTR(other)); \
	FUNC_GROUP_DECL(sig, pin)

#define GPIO_PIN_DECL(pin, gpio) \
	SIG_EXPR_LIST_DECL_SINGLE(gpio, gpio); \
	MS_PIN_DECL_(pin, SIG_EXPR_LIST_PTR(gpio))

struct aspeed_pin_group {
	const char *name;
	const unsigned int *pins;
	const unsigned int npins;
};

#define ASPEED_PINCTRL_GROUP(name_) { \
	.name = #name_, \
	.pins = &(PIN_GROUP_SYM(name_))[0], \
	.npins = ARRAY_SIZE(PIN_GROUP_SYM(name_)), \
}

struct aspeed_pin_function {
	const char *name;
	const char *const *groups;
	unsigned int ngroups;
};

#define ASPEED_PINCTRL_FUNC(name_, ...) { \
	.name = #name_, \
	.groups = &FUNC_GROUP_SYM(name_)[0], \
	.ngroups = ARRAY_SIZE(FUNC_GROUP_SYM(name_)), \
}

struct aspeed_pinmux_data;

struct aspeed_pinmux_ops {
	int (*set)(const struct aspeed_pinmux_data *ctx,
		   const struct aspeed_sig_expr *expr, bool enabled);
};

struct aspeed_pinmux_data {
	struct regmap *maps[ASPEED_NR_PINMUX_IPS];

	const struct aspeed_pinmux_ops *ops;

	const struct aspeed_pin_group *groups;
	const unsigned int ngroups;

	const struct aspeed_pin_function *functions;
	const unsigned int nfunctions;
};

int aspeed_sig_desc_eval(const struct aspeed_sig_desc *desc, bool enabled,
			 struct regmap *map);

int aspeed_sig_expr_eval(const struct aspeed_pinmux_data *ctx,
			 const struct aspeed_sig_expr *expr,
			 bool enabled);

static inline int aspeed_sig_expr_set(const struct aspeed_pinmux_data *ctx,
				      const struct aspeed_sig_expr *expr,
				      bool enabled)
{
	return ctx->ops->set(ctx, expr, enabled);
}

#endif /* ASPEED_PINMUX_H */
