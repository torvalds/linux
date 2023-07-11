// SPDX-License-Identifier: GPL-2.0
/*
 * R9A06G032 clock driver
 *
 * Copyright (C) 2018 Renesas Electronics Europe Limited
 *
 * Michel Pollet <michel.pollet@bp.renesas.com>, <buserror@gmail.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>
#include <linux/soc/renesas/r9a06g032-sysctrl.h>
#include <linux/spinlock.h>
#include <dt-bindings/clock/r9a06g032-sysctrl.h>

#define R9A06G032_SYSCTRL_USB    0x00
#define R9A06G032_SYSCTRL_USB_H2MODE  (1<<1)
#define R9A06G032_SYSCTRL_DMAMUX 0xA0

/**
 * struct regbit - describe one bit in a register
 * @reg: offset of register relative to base address,
 *          expressed in units of 32-bit words (not bytes),
 * @bit: which bit (0 to 31) in the register
 *
 * This structure is used to compactly encode the location
 * of a single bit in a register. Five bits are needed to
 * encode the bit number. With uint16_t data type, this
 * leaves 11 bits to encode a register offset up to 2047.
 *
 * Since registers are aligned on 32-bit boundaries, the
 * offset will be specified in 32-bit words rather than bytes.
 * This allows encoding an offset up to 0x1FFC (8188) bytes.
 *
 * Helper macro RB() takes care of converting the register
 * offset from bytes to 32-bit words.
 */
struct regbit {
	u16 bit:5;
	u16 reg:11;
};

#define RB(_reg, _bit) ((struct regbit) { \
	.reg = (_reg) / 4, \
	.bit = (_bit) \
})

/**
 * struct r9a06g032_gate - clock-related control bits
 * @gate:   clock enable/disable
 * @reset:  clock module reset (active low)
 * @ready:  enables NoC forwarding of read/write requests to device,
 *          (eg. device is ready to handle read/write requests)
 * @midle:  request to idle the NoC interconnect
 *
 * Each of these fields describes a single bit in a register,
 * which controls some aspect of clock gating. The @gate field
 * is mandatory, this one enables/disables the clock. The
 * other fields are optional, with zero indicating "not used".
 *
 * In most cases there is a @reset bit which needs to be
 * de-asserted to bring the module out of reset.
 *
 * Modules may also need to signal when they are @ready to
 * handle requests (read/writes) from the NoC interconnect.
 *
 * Similarly, the @midle bit is used to idle the master.
 */
struct r9a06g032_gate {
	struct regbit gate, reset, ready, midle;
	/* Unused fields omitted to save space */
	/* struct regbit scon, mirack, mistat */;
};

enum gate_type {
	K_GATE = 0,	/* gate which enable/disable */
	K_FFC,		/* fixed factor clock */
	K_DIV,		/* divisor */
	K_BITSEL,	/* special for UARTs */
	K_DUALGATE	/* special for UARTs */
};

/**
 * struct r9a06g032_clkdesc - describe a single clock
 * @name:      string describing this clock
 * @managed:   boolean indicating if this clock should be
 *             started/stopped as part of power management
 * @type:      see enum @gate_type
 * @index:     the ID of this clock element
 * @source:    the ID+1 of the parent clock element.
 *             Root clock uses ID of ~0 (PARENT_ID);
 * @gate:      clock enable/disable
 * @div_min:   smallest permitted clock divider
 * @div_max:   largest permitted clock divider
 * @reg:       clock divider register offset, in 32-bit words
 * @div_table: optional list of fixed clock divider values;
 *             must be in ascending order, zero for unused
 * @div:       divisor for fixed-factor clock
 * @mul:       multiplier for fixed-factor clock
 * @group:     UART group, 0=UART0/1/2, 1=UART3/4/5/6/7
 * @sel:       select either g1/r1 or g2/r2 as clock source
 * @g1:        1st source gate (clock enable/disable)
 * @r1:        1st source reset (module reset)
 * @g2:        2nd source gate (clock enable/disable)
 * @r2:        2nd source reset (module reset)
 *
 * Describes a single element in the clock tree hierarchy.
 * As there are quite a large number of clock elements, this
 * structure is packed tightly to conserve space.
 */
struct r9a06g032_clkdesc {
	const char *name;
	uint32_t managed:1;
	enum gate_type type:3;
	uint32_t index:8;
	uint32_t source:8; /* source index + 1 (0 == none) */
	union {
		/* type = K_GATE */
		struct r9a06g032_gate gate;
		/* type = K_DIV  */
		struct {
			unsigned int div_min:10, div_max:10, reg:10;
			u16 div_table[4];
		};
		/* type = K_FFC */
		struct {
			u16 div, mul;
		};
		/* type = K_DUALGATE */
		struct {
			uint16_t group:1;
			struct regbit sel, g1, r1, g2, r2;
		} dual;
	};
};

/*
 * The last three arguments are not currently used,
 * but are kept in the r9a06g032_clocks table below.
 */
#define I_GATE(_clk, _rst, _rdy, _midle, _scon, _mirack, _mistat) { \
	.gate = _clk, \
	.reset = _rst, \
	.ready = _rdy, \
	.midle = _midle, \
	/* .scon = _scon, */ \
	/* .mirack = _mirack, */ \
	/* .mistat = _mistat */ \
}
#define D_GATE(_idx, _n, _src, ...) { \
	.type = K_GATE, \
	.index = R9A06G032_##_idx, \
	.source = 1 + R9A06G032_##_src, \
	.name = _n, \
	.gate = I_GATE(__VA_ARGS__) \
}
#define D_MODULE(_idx, _n, _src, ...) { \
	.type = K_GATE, \
	.index = R9A06G032_##_idx, \
	.source = 1 + R9A06G032_##_src, \
	.name = _n, \
	.managed = 1, \
	.gate = I_GATE(__VA_ARGS__) \
}
#define D_ROOT(_idx, _n, _mul, _div) { \
	.type = K_FFC, \
	.index = R9A06G032_##_idx, \
	.name = _n, \
	.div = _div, \
	.mul = _mul \
}
#define D_FFC(_idx, _n, _src, _div) { \
	.type = K_FFC, \
	.index = R9A06G032_##_idx, \
	.source = 1 + R9A06G032_##_src, \
	.name = _n, \
	.div = _div, \
	.mul = 1 \
}
#define D_DIV(_idx, _n, _src, _reg, _min, _max, ...) { \
	.type = K_DIV, \
	.index = R9A06G032_##_idx, \
	.source = 1 + R9A06G032_##_src, \
	.name = _n, \
	.reg = _reg, \
	.div_min = _min, \
	.div_max = _max, \
	.div_table = { __VA_ARGS__ } \
}
#define D_UGATE(_idx, _n, _src, _g, _g1, _r1, _g2, _r2) { \
	.type = K_DUALGATE, \
	.index = R9A06G032_##_idx, \
	.source = 1 + R9A06G032_##_src, \
	.name = _n, \
	.dual = { \
		.group = _g, \
		.g1 = _g1, \
		.r1 = _r1, \
		.g2 = _g2, \
		.r2 = _r2 \
	}, \
}

/* Internal clock IDs */
#define R9A06G032_CLKOUT		0
#define R9A06G032_CLKOUT_D10		2
#define R9A06G032_CLKOUT_D16		3
#define R9A06G032_CLKOUT_D160		4
#define R9A06G032_CLKOUT_D1OR2		5
#define R9A06G032_CLKOUT_D20		6
#define R9A06G032_CLKOUT_D40		7
#define R9A06G032_CLKOUT_D5		8
#define R9A06G032_CLKOUT_D8		9
#define R9A06G032_DIV_ADC		10
#define R9A06G032_DIV_I2C		11
#define R9A06G032_DIV_NAND		12
#define R9A06G032_DIV_P1_PG		13
#define R9A06G032_DIV_P2_PG		14
#define R9A06G032_DIV_P3_PG		15
#define R9A06G032_DIV_P4_PG		16
#define R9A06G032_DIV_P5_PG		17
#define R9A06G032_DIV_P6_PG		18
#define R9A06G032_DIV_QSPI0		19
#define R9A06G032_DIV_QSPI1		20
#define R9A06G032_DIV_REF_SYNC		21
#define R9A06G032_DIV_SDIO0		22
#define R9A06G032_DIV_SDIO1		23
#define R9A06G032_DIV_SWITCH		24
#define R9A06G032_DIV_UART		25
#define R9A06G032_DIV_MOTOR		64
#define R9A06G032_CLK_DDRPHY_PLLCLK_D4	78
#define R9A06G032_CLK_ECAT100_D4	79
#define R9A06G032_CLK_HSR100_D2		80
#define R9A06G032_CLK_REF_SYNC_D4	81
#define R9A06G032_CLK_REF_SYNC_D8	82
#define R9A06G032_CLK_SERCOS100_D2	83
#define R9A06G032_DIV_CA7		84

#define R9A06G032_UART_GROUP_012	154
#define R9A06G032_UART_GROUP_34567	155

#define R9A06G032_CLOCK_COUNT		(R9A06G032_UART_GROUP_34567 + 1)

static const struct r9a06g032_clkdesc r9a06g032_clocks[] = {
	D_ROOT(CLKOUT, "clkout", 25, 1),
	D_ROOT(CLK_PLL_USB, "clk_pll_usb", 12, 10),
	D_FFC(CLKOUT_D10, "clkout_d10", CLKOUT, 10),
	D_FFC(CLKOUT_D16, "clkout_d16", CLKOUT, 16),
	D_FFC(CLKOUT_D160, "clkout_d160", CLKOUT, 160),
	D_DIV(CLKOUT_D1OR2, "clkout_d1or2", CLKOUT, 0, 1, 2),
	D_FFC(CLKOUT_D20, "clkout_d20", CLKOUT, 20),
	D_FFC(CLKOUT_D40, "clkout_d40", CLKOUT, 40),
	D_FFC(CLKOUT_D5, "clkout_d5", CLKOUT, 5),
	D_FFC(CLKOUT_D8, "clkout_d8", CLKOUT, 8),
	D_DIV(DIV_ADC, "div_adc", CLKOUT, 77, 50, 250),
	D_DIV(DIV_I2C, "div_i2c", CLKOUT, 78, 12, 16),
	D_DIV(DIV_NAND, "div_nand", CLKOUT, 82, 12, 32),
	D_DIV(DIV_P1_PG, "div_p1_pg", CLKOUT, 68, 12, 200),
	D_DIV(DIV_P2_PG, "div_p2_pg", CLKOUT, 62, 12, 128),
	D_DIV(DIV_P3_PG, "div_p3_pg", CLKOUT, 64, 8, 128),
	D_DIV(DIV_P4_PG, "div_p4_pg", CLKOUT, 66, 8, 128),
	D_DIV(DIV_P5_PG, "div_p5_pg", CLKOUT, 71, 10, 40),
	D_DIV(DIV_P6_PG, "div_p6_pg", CLKOUT, 18, 12, 64),
	D_DIV(DIV_QSPI0, "div_qspi0", CLKOUT, 73, 3, 7),
	D_DIV(DIV_QSPI1, "div_qspi1", CLKOUT, 25, 3, 7),
	D_DIV(DIV_REF_SYNC, "div_ref_sync", CLKOUT, 56, 2, 16, 2, 4, 8, 16),
	D_DIV(DIV_SDIO0, "div_sdio0", CLKOUT, 74, 20, 128),
	D_DIV(DIV_SDIO1, "div_sdio1", CLKOUT, 75, 20, 128),
	D_DIV(DIV_SWITCH, "div_switch", CLKOUT, 37, 5, 40),
	D_DIV(DIV_UART, "div_uart", CLKOUT, 79, 12, 128),
	D_GATE(CLK_25_PG4, "clk_25_pg4", CLKOUT_D40, RB(0xe8, 9),
	       RB(0xe8, 10), RB(0xe8, 11), RB(0x00, 0),
	       RB(0x15c, 3), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_25_PG5, "clk_25_pg5", CLKOUT_D40, RB(0xe8, 12),
	       RB(0xe8, 13), RB(0xe8, 14), RB(0x00, 0),
	       RB(0x15c, 4), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_25_PG6, "clk_25_pg6", CLKOUT_D40, RB(0xe8, 15),
	       RB(0xe8, 16), RB(0xe8, 17), RB(0x00, 0),
	       RB(0x15c, 5), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_25_PG7, "clk_25_pg7", CLKOUT_D40, RB(0xe8, 18),
	       RB(0xe8, 19), RB(0xe8, 20), RB(0x00, 0),
	       RB(0x15c, 6), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_25_PG8, "clk_25_pg8", CLKOUT_D40, RB(0xe8, 21),
	       RB(0xe8, 22), RB(0xe8, 23), RB(0x00, 0),
	       RB(0x15c, 7), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_ADC, "clk_adc", DIV_ADC, RB(0x3c, 10),
	       RB(0x3c, 11), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_ECAT100, "clk_ecat100", CLKOUT_D10, RB(0x80, 5),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_HSR100, "clk_hsr100", CLKOUT_D10, RB(0x90, 3),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_I2C0, "clk_i2c0", DIV_I2C, RB(0x3c, 6),
	       RB(0x3c, 7), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_I2C1, "clk_i2c1", DIV_I2C, RB(0x3c, 8),
	       RB(0x3c, 9), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_MII_REF, "clk_mii_ref", CLKOUT_D40, RB(0x68, 2),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_NAND, "clk_nand", DIV_NAND, RB(0x50, 4),
	       RB(0x50, 5), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_NOUSBP2_PG6, "clk_nousbp2_pg6", DIV_P2_PG, RB(0xec, 20),
	       RB(0xec, 21), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_P1_PG2, "clk_p1_pg2", DIV_P1_PG, RB(0x10c, 2),
	       RB(0x10c, 3), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_P1_PG3, "clk_p1_pg3", DIV_P1_PG, RB(0x10c, 4),
	       RB(0x10c, 5), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_P1_PG4, "clk_p1_pg4", DIV_P1_PG, RB(0x10c, 6),
	       RB(0x10c, 7), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_P4_PG3, "clk_p4_pg3", DIV_P4_PG, RB(0x104, 4),
	       RB(0x104, 5), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_P4_PG4, "clk_p4_pg4", DIV_P4_PG, RB(0x104, 6),
	       RB(0x104, 7), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_P6_PG1, "clk_p6_pg1", DIV_P6_PG, RB(0x114, 0),
	       RB(0x114, 1), RB(0x114, 2), RB(0x00, 0),
	       RB(0x16c, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_P6_PG2, "clk_p6_pg2", DIV_P6_PG, RB(0x114, 3),
	       RB(0x114, 4), RB(0x114, 5), RB(0x00, 0),
	       RB(0x16c, 1), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_P6_PG3, "clk_p6_pg3", DIV_P6_PG, RB(0x114, 6),
	       RB(0x114, 7), RB(0x114, 8), RB(0x00, 0),
	       RB(0x16c, 2), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_P6_PG4, "clk_p6_pg4", DIV_P6_PG, RB(0x114, 9),
	       RB(0x114, 10), RB(0x114, 11), RB(0x00, 0),
	       RB(0x16c, 3), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(CLK_PCI_USB, "clk_pci_usb", CLKOUT_D40, RB(0x1c, 6),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_QSPI0, "clk_qspi0", DIV_QSPI0, RB(0x54, 4),
	       RB(0x54, 5), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_QSPI1, "clk_qspi1", DIV_QSPI1, RB(0x90, 4),
	       RB(0x90, 5), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_RGMII_REF, "clk_rgmii_ref", CLKOUT_D8, RB(0x68, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_RMII_REF, "clk_rmii_ref", CLKOUT_D20, RB(0x68, 1),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_SDIO0, "clk_sdio0", DIV_SDIO0, RB(0x0c, 4),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_SDIO1, "clk_sdio1", DIV_SDIO1, RB(0xc8, 4),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_SERCOS100, "clk_sercos100", CLKOUT_D10, RB(0x84, 5),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_SLCD, "clk_slcd", DIV_P1_PG, RB(0x10c, 0),
	       RB(0x10c, 1), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_SPI0, "clk_spi0", DIV_P3_PG, RB(0xfc, 0),
	       RB(0xfc, 1), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_SPI1, "clk_spi1", DIV_P3_PG, RB(0xfc, 2),
	       RB(0xfc, 3), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_SPI2, "clk_spi2", DIV_P3_PG, RB(0xfc, 4),
	       RB(0xfc, 5), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_SPI3, "clk_spi3", DIV_P3_PG, RB(0xfc, 6),
	       RB(0xfc, 7), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_SPI4, "clk_spi4", DIV_P4_PG, RB(0x104, 0),
	       RB(0x104, 1), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_SPI5, "clk_spi5", DIV_P4_PG, RB(0x104, 2),
	       RB(0x104, 3), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_SWITCH, "clk_switch", DIV_SWITCH, RB(0x130, 2),
	       RB(0x130, 3), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_DIV(DIV_MOTOR, "div_motor", CLKOUT_D5, 84, 2, 8),
	D_MODULE(HCLK_ECAT125, "hclk_ecat125", CLKOUT_D8, RB(0x80, 0),
		 RB(0x80, 1), RB(0x00, 0), RB(0x80, 2),
		 RB(0x00, 0), RB(0x88, 0), RB(0x88, 1)),
	D_MODULE(HCLK_PINCONFIG, "hclk_pinconfig", CLKOUT_D40, RB(0xe8, 0),
		 RB(0xe8, 1), RB(0xe8, 2), RB(0x00, 0),
		 RB(0x15c, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_SERCOS, "hclk_sercos", CLKOUT_D10, RB(0x84, 0),
		 RB(0x84, 2), RB(0x00, 0), RB(0x84, 1),
		 RB(0x00, 0), RB(0x8c, 0), RB(0x8c, 1)),
	D_MODULE(HCLK_SGPIO2, "hclk_sgpio2", DIV_P5_PG, RB(0x118, 3),
		 RB(0x118, 4), RB(0x118, 5), RB(0x00, 0),
		 RB(0x168, 1), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_SGPIO3, "hclk_sgpio3", DIV_P5_PG, RB(0x118, 6),
		 RB(0x118, 7), RB(0x118, 8), RB(0x00, 0),
		 RB(0x168, 2), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_SGPIO4, "hclk_sgpio4", DIV_P5_PG, RB(0x118, 9),
		 RB(0x118, 10), RB(0x118, 11), RB(0x00, 0),
		 RB(0x168, 3), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_TIMER0, "hclk_timer0", CLKOUT_D40, RB(0xe8, 3),
		 RB(0xe8, 4), RB(0xe8, 5), RB(0x00, 0),
		 RB(0x15c, 1), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_TIMER1, "hclk_timer1", CLKOUT_D40, RB(0xe8, 6),
		 RB(0xe8, 7), RB(0xe8, 8), RB(0x00, 0),
		 RB(0x15c, 2), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_USBF, "hclk_usbf", CLKOUT_D8, RB(0x1c, 3),
		 RB(0x00, 0), RB(0x00, 0), RB(0x1c, 4),
		 RB(0x00, 0), RB(0x20, 2), RB(0x20, 3)),
	D_MODULE(HCLK_USBH, "hclk_usbh", CLKOUT_D8, RB(0x1c, 0),
		 RB(0x1c, 1), RB(0x00, 0), RB(0x1c, 2),
		 RB(0x00, 0), RB(0x20, 0), RB(0x20, 1)),
	D_MODULE(HCLK_USBPM, "hclk_usbpm", CLKOUT_D8, RB(0x1c, 5),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_48_PG_F, "clk_48_pg_f", CLK_48, RB(0xf0, 12),
	       RB(0xf0, 13), RB(0x00, 0), RB(0xf0, 14),
	       RB(0x00, 0), RB(0x160, 4), RB(0x160, 5)),
	D_GATE(CLK_48_PG4, "clk_48_pg4", CLK_48, RB(0xf0, 9),
	       RB(0xf0, 10), RB(0xf0, 11), RB(0x00, 0),
	       RB(0x160, 3), RB(0x00, 0), RB(0x00, 0)),
	D_FFC(CLK_DDRPHY_PLLCLK_D4, "clk_ddrphy_pllclk_d4", CLK_DDRPHY_PLLCLK, 4),
	D_FFC(CLK_ECAT100_D4, "clk_ecat100_d4", CLK_ECAT100, 4),
	D_FFC(CLK_HSR100_D2, "clk_hsr100_d2", CLK_HSR100, 2),
	D_FFC(CLK_REF_SYNC_D4, "clk_ref_sync_d4", CLK_REF_SYNC, 4),
	D_FFC(CLK_REF_SYNC_D8, "clk_ref_sync_d8", CLK_REF_SYNC, 8),
	D_FFC(CLK_SERCOS100_D2, "clk_sercos100_d2", CLK_SERCOS100, 2),
	D_DIV(DIV_CA7, "div_ca7", CLK_REF_SYNC, 57, 1, 4, 1, 2, 4),
	D_MODULE(HCLK_CAN0, "hclk_can0", CLK_48, RB(0xf0, 3),
		 RB(0xf0, 4), RB(0xf0, 5), RB(0x00, 0),
		 RB(0x160, 1), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_CAN1, "hclk_can1", CLK_48, RB(0xf0, 6),
		 RB(0xf0, 7), RB(0xf0, 8), RB(0x00, 0),
		 RB(0x160, 2), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_DELTASIGMA, "hclk_deltasigma", DIV_MOTOR, RB(0x3c, 15),
		 RB(0x3c, 16), RB(0x3c, 17), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_PWMPTO, "hclk_pwmpto", DIV_MOTOR, RB(0x3c, 12),
		 RB(0x3c, 13), RB(0x3c, 14), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_RSV, "hclk_rsv", CLK_48, RB(0xf0, 0),
		 RB(0xf0, 1), RB(0xf0, 2), RB(0x00, 0),
		 RB(0x160, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_SGPIO0, "hclk_sgpio0", DIV_MOTOR, RB(0x3c, 0),
		 RB(0x3c, 1), RB(0x3c, 2), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_SGPIO1, "hclk_sgpio1", DIV_MOTOR, RB(0x3c, 3),
		 RB(0x3c, 4), RB(0x3c, 5), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_DIV(RTOS_MDC, "rtos_mdc", CLK_REF_SYNC, 100, 80, 640, 80, 160, 320, 640),
	D_GATE(CLK_CM3, "clk_cm3", CLK_REF_SYNC_D4, RB(0x174, 0),
	       RB(0x174, 1), RB(0x00, 0), RB(0x174, 2),
	       RB(0x00, 0), RB(0x178, 0), RB(0x178, 1)),
	D_GATE(CLK_DDRC, "clk_ddrc", CLK_DDRPHY_PLLCLK_D4, RB(0x64, 3),
	       RB(0x64, 4), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_ECAT25, "clk_ecat25", CLK_ECAT100_D4, RB(0x80, 3),
	       RB(0x80, 4), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_HSR50, "clk_hsr50", CLK_HSR100_D2, RB(0x90, 4),
	       RB(0x90, 5), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_HW_RTOS, "clk_hw_rtos", CLK_REF_SYNC_D4, RB(0x18c, 0),
	       RB(0x18c, 1), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_GATE(CLK_SERCOS50, "clk_sercos50", CLK_SERCOS100_D2, RB(0x84, 4),
	       RB(0x84, 3), RB(0x00, 0), RB(0x00, 0),
	       RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_ADC, "hclk_adc", CLK_REF_SYNC_D8, RB(0x34, 15),
		 RB(0x34, 16), RB(0x34, 17), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_CM3, "hclk_cm3", CLK_REF_SYNC_D4, RB(0x184, 0),
		 RB(0x184, 1), RB(0x184, 2), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_CRYPTO_EIP150, "hclk_crypto_eip150", CLK_REF_SYNC_D4, RB(0x24, 3),
		 RB(0x24, 4), RB(0x24, 5), RB(0x00, 0),
		 RB(0x28, 2), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_CRYPTO_EIP93, "hclk_crypto_eip93", CLK_REF_SYNC_D4, RB(0x24, 0),
		 RB(0x24, 1), RB(0x00, 0), RB(0x24, 2),
		 RB(0x00, 0), RB(0x28, 0), RB(0x28, 1)),
	D_MODULE(HCLK_DDRC, "hclk_ddrc", CLK_REF_SYNC_D4, RB(0x64, 0),
		 RB(0x64, 2), RB(0x00, 0), RB(0x64, 1),
		 RB(0x00, 0), RB(0x74, 0), RB(0x74, 1)),
	D_MODULE(HCLK_DMA0, "hclk_dma0", CLK_REF_SYNC_D4, RB(0x4c, 0),
		 RB(0x4c, 1), RB(0x4c, 2), RB(0x4c, 3),
		 RB(0x58, 0), RB(0x58, 1), RB(0x58, 2)),
	D_MODULE(HCLK_DMA1, "hclk_dma1", CLK_REF_SYNC_D4, RB(0x4c, 4),
		 RB(0x4c, 5), RB(0x4c, 6), RB(0x4c, 7),
		 RB(0x58, 3), RB(0x58, 4), RB(0x58, 5)),
	D_MODULE(HCLK_GMAC0, "hclk_gmac0", CLK_REF_SYNC_D4, RB(0x6c, 0),
		 RB(0x6c, 1), RB(0x6c, 2), RB(0x6c, 3),
		 RB(0x78, 0), RB(0x78, 1), RB(0x78, 2)),
	D_MODULE(HCLK_GMAC1, "hclk_gmac1", CLK_REF_SYNC_D4, RB(0x70, 0),
		 RB(0x70, 1), RB(0x70, 2), RB(0x70, 3),
		 RB(0x7c, 0), RB(0x7c, 1), RB(0x7c, 2)),
	D_MODULE(HCLK_GPIO0, "hclk_gpio0", CLK_REF_SYNC_D4, RB(0x40, 18),
		 RB(0x40, 19), RB(0x40, 20), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_GPIO1, "hclk_gpio1", CLK_REF_SYNC_D4, RB(0x40, 21),
		 RB(0x40, 22), RB(0x40, 23), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_GPIO2, "hclk_gpio2", CLK_REF_SYNC_D4, RB(0x44, 9),
		 RB(0x44, 10), RB(0x44, 11), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_HSR, "hclk_hsr", CLK_HSR100_D2, RB(0x90, 0),
		 RB(0x90, 2), RB(0x00, 0), RB(0x90, 1),
		 RB(0x00, 0), RB(0x98, 0), RB(0x98, 1)),
	D_MODULE(HCLK_I2C0, "hclk_i2c0", CLK_REF_SYNC_D8, RB(0x34, 9),
		 RB(0x34, 10), RB(0x34, 11), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_I2C1, "hclk_i2c1", CLK_REF_SYNC_D8, RB(0x34, 12),
		 RB(0x34, 13), RB(0x34, 14), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_LCD, "hclk_lcd", CLK_REF_SYNC_D4, RB(0xf4, 0),
		 RB(0xf4, 1), RB(0xf4, 2), RB(0x00, 0),
		 RB(0x164, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_MSEBI_M, "hclk_msebi_m", CLK_REF_SYNC_D4, RB(0x2c, 4),
		 RB(0x2c, 5), RB(0x2c, 6), RB(0x00, 0),
		 RB(0x30, 3), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_MSEBI_S, "hclk_msebi_s", CLK_REF_SYNC_D4, RB(0x2c, 0),
		 RB(0x2c, 1), RB(0x2c, 2), RB(0x2c, 3),
		 RB(0x30, 0), RB(0x30, 1), RB(0x30, 2)),
	D_MODULE(HCLK_NAND, "hclk_nand", CLK_REF_SYNC_D4, RB(0x50, 0),
		 RB(0x50, 1), RB(0x50, 2), RB(0x50, 3),
		 RB(0x5c, 0), RB(0x5c, 1), RB(0x5c, 2)),
	D_MODULE(HCLK_PG_I, "hclk_pg_i", CLK_REF_SYNC_D4, RB(0xf4, 12),
		 RB(0xf4, 13), RB(0x00, 0), RB(0xf4, 14),
		 RB(0x00, 0), RB(0x164, 4), RB(0x164, 5)),
	D_MODULE(HCLK_PG19, "hclk_pg19", CLK_REF_SYNC_D4, RB(0x44, 12),
		 RB(0x44, 13), RB(0x44, 14), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_PG20, "hclk_pg20", CLK_REF_SYNC_D4, RB(0x44, 15),
		 RB(0x44, 16), RB(0x44, 17), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_PG3, "hclk_pg3", CLK_REF_SYNC_D4, RB(0xf4, 6),
		 RB(0xf4, 7), RB(0xf4, 8), RB(0x00, 0),
		 RB(0x164, 2), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_PG4, "hclk_pg4", CLK_REF_SYNC_D4, RB(0xf4, 9),
		 RB(0xf4, 10), RB(0xf4, 11), RB(0x00, 0),
		 RB(0x164, 3), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_QSPI0, "hclk_qspi0", CLK_REF_SYNC_D4, RB(0x54, 0),
		 RB(0x54, 1), RB(0x54, 2), RB(0x54, 3),
		 RB(0x60, 0), RB(0x60, 1), RB(0x60, 2)),
	D_MODULE(HCLK_QSPI1, "hclk_qspi1", CLK_REF_SYNC_D4, RB(0x90, 0),
		 RB(0x90, 1), RB(0x90, 2), RB(0x90, 3),
		 RB(0x98, 0), RB(0x98, 1), RB(0x98, 2)),
	D_MODULE(HCLK_ROM, "hclk_rom", CLK_REF_SYNC_D4, RB(0x154, 0),
		 RB(0x154, 1), RB(0x154, 2), RB(0x00, 0),
		 RB(0x170, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_RTC, "hclk_rtc", CLK_REF_SYNC_D8, RB(0x140, 0),
		 RB(0x140, 3), RB(0x00, 0), RB(0x140, 2),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_SDIO0, "hclk_sdio0", CLK_REF_SYNC_D4, RB(0x0c, 0),
		 RB(0x0c, 1), RB(0x0c, 2), RB(0x0c, 3),
		 RB(0x10, 0), RB(0x10, 1), RB(0x10, 2)),
	D_MODULE(HCLK_SDIO1, "hclk_sdio1", CLK_REF_SYNC_D4, RB(0xc8, 0),
		 RB(0xc8, 1), RB(0xc8, 2), RB(0xc8, 3),
		 RB(0xcc, 0), RB(0xcc, 1), RB(0xcc, 2)),
	D_MODULE(HCLK_SEMAP, "hclk_semap", CLK_REF_SYNC_D4, RB(0xf4, 3),
		 RB(0xf4, 4), RB(0xf4, 5), RB(0x00, 0),
		 RB(0x164, 1), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_SPI0, "hclk_spi0", CLK_REF_SYNC_D4, RB(0x40, 0),
		 RB(0x40, 1), RB(0x40, 2), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_SPI1, "hclk_spi1", CLK_REF_SYNC_D4, RB(0x40, 3),
		 RB(0x40, 4), RB(0x40, 5), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_SPI2, "hclk_spi2", CLK_REF_SYNC_D4, RB(0x40, 6),
		 RB(0x40, 7), RB(0x40, 8), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_SPI3, "hclk_spi3", CLK_REF_SYNC_D4, RB(0x40, 9),
		 RB(0x40, 10), RB(0x40, 11), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_SPI4, "hclk_spi4", CLK_REF_SYNC_D4, RB(0x40, 12),
		 RB(0x40, 13), RB(0x40, 14), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_SPI5, "hclk_spi5", CLK_REF_SYNC_D4, RB(0x40, 15),
		 RB(0x40, 16), RB(0x40, 17), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_SWITCH, "hclk_switch", CLK_REF_SYNC_D4, RB(0x130, 0),
		 RB(0x00, 0), RB(0x130, 1), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_SWITCH_RG, "hclk_switch_rg", CLK_REF_SYNC_D4, RB(0x188, 0),
		 RB(0x188, 1), RB(0x188, 2), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_UART0, "hclk_uart0", CLK_REF_SYNC_D8, RB(0x34, 0),
		 RB(0x34, 1), RB(0x34, 2), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_UART1, "hclk_uart1", CLK_REF_SYNC_D8, RB(0x34, 3),
		 RB(0x34, 4), RB(0x34, 5), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_UART2, "hclk_uart2", CLK_REF_SYNC_D8, RB(0x34, 6),
		 RB(0x34, 7), RB(0x34, 8), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_UART3, "hclk_uart3", CLK_REF_SYNC_D4, RB(0x40, 24),
		 RB(0x40, 25), RB(0x40, 26), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_UART4, "hclk_uart4", CLK_REF_SYNC_D4, RB(0x40, 27),
		 RB(0x40, 28), RB(0x40, 29), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_UART5, "hclk_uart5", CLK_REF_SYNC_D4, RB(0x44, 0),
		 RB(0x44, 1), RB(0x44, 2), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_UART6, "hclk_uart6", CLK_REF_SYNC_D4, RB(0x44, 3),
		 RB(0x44, 4), RB(0x44, 5), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	D_MODULE(HCLK_UART7, "hclk_uart7", CLK_REF_SYNC_D4, RB(0x44, 6),
		 RB(0x44, 7), RB(0x44, 8), RB(0x00, 0),
		 RB(0x00, 0), RB(0x00, 0), RB(0x00, 0)),
	/*
	 * These are not hardware clocks, but are needed to handle the special
	 * case where we have a 'selector bit' that doesn't just change the
	 * parent for a clock, but also the gate it's supposed to use.
	 */
	{
		.index = R9A06G032_UART_GROUP_012,
		.name = "uart_group_012",
		.type = K_BITSEL,
		.source = 1 + R9A06G032_DIV_UART,
		/* R9A06G032_SYSCTRL_REG_PWRCTRL_PG0_0 */
		.dual.sel = RB(0x34, 30),
		.dual.group = 0,
	},
	{
		.index = R9A06G032_UART_GROUP_34567,
		.name = "uart_group_34567",
		.type = K_BITSEL,
		.source = 1 + R9A06G032_DIV_P2_PG,
		/* R9A06G032_SYSCTRL_REG_PWRCTRL_PG1_PR2 */
		.dual.sel = RB(0xec, 24),
		.dual.group = 1,
	},
	D_UGATE(CLK_UART0, "clk_uart0", UART_GROUP_012, 0,
		RB(0x34, 18), RB(0x34, 19), RB(0x34, 20), RB(0x34, 21)),
	D_UGATE(CLK_UART1, "clk_uart1", UART_GROUP_012, 0,
		RB(0x34, 22), RB(0x34, 23), RB(0x34, 24), RB(0x34, 25)),
	D_UGATE(CLK_UART2, "clk_uart2", UART_GROUP_012, 0,
		RB(0x34, 26), RB(0x34, 27), RB(0x34, 28), RB(0x34, 29)),
	D_UGATE(CLK_UART3, "clk_uart3", UART_GROUP_34567, 1,
		RB(0xec, 0), RB(0xec, 1), RB(0xec, 2), RB(0xec, 3)),
	D_UGATE(CLK_UART4, "clk_uart4", UART_GROUP_34567, 1,
		RB(0xec, 4), RB(0xec, 5), RB(0xec, 6), RB(0xec, 7)),
	D_UGATE(CLK_UART5, "clk_uart5", UART_GROUP_34567, 1,
		RB(0xec, 8), RB(0xec, 9), RB(0xec, 10), RB(0xec, 11)),
	D_UGATE(CLK_UART6, "clk_uart6", UART_GROUP_34567, 1,
		RB(0xec, 12), RB(0xec, 13), RB(0xec, 14), RB(0xec, 15)),
	D_UGATE(CLK_UART7, "clk_uart7", UART_GROUP_34567, 1,
		RB(0xec, 16), RB(0xec, 17), RB(0xec, 18), RB(0xec, 19)),
};

struct r9a06g032_priv {
	struct clk_onecell_data data;
	spinlock_t lock; /* protects concurrent access to gates */
	void __iomem *reg;
};

static struct r9a06g032_priv *sysctrl_priv;

/* Exported helper to access the DMAMUX register */
int r9a06g032_sysctrl_set_dmamux(u32 mask, u32 val)
{
	unsigned long flags;
	u32 dmamux;

	if (!sysctrl_priv)
		return -EPROBE_DEFER;

	spin_lock_irqsave(&sysctrl_priv->lock, flags);

	dmamux = readl(sysctrl_priv->reg + R9A06G032_SYSCTRL_DMAMUX);
	dmamux &= ~mask;
	dmamux |= val & mask;
	writel(dmamux, sysctrl_priv->reg + R9A06G032_SYSCTRL_DMAMUX);

	spin_unlock_irqrestore(&sysctrl_priv->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(r9a06g032_sysctrl_set_dmamux);

static void clk_rdesc_set(struct r9a06g032_priv *clocks,
			  struct regbit rb, unsigned int on)
{
	u32 __iomem *reg = clocks->reg + (rb.reg * 4);
	u32 val;

	if (!rb.reg && !rb.bit)
		return;

	val = readl(reg);
	val = (val & ~BIT(rb.bit)) | ((!!on) << rb.bit);
	writel(val, reg);
}

static int clk_rdesc_get(struct r9a06g032_priv *clocks, struct regbit rb)
{
	u32 __iomem *reg = clocks->reg + (rb.reg * 4);
	u32 val = readl(reg);

	return !!(val & BIT(rb.bit));
}

/*
 * This implements the R9A06G032 clock gate 'driver'. We cannot use the system's
 * clock gate framework as the gates on the R9A06G032 have a special enabling
 * sequence, therefore we use this little proxy.
 */
struct r9a06g032_clk_gate {
	struct clk_hw hw;
	struct r9a06g032_priv *clocks;
	u16 index;

	struct r9a06g032_gate gate;
};

#define to_r9a06g032_gate(_hw) container_of(_hw, struct r9a06g032_clk_gate, hw)

static int create_add_module_clock(struct of_phandle_args *clkspec,
				   struct device *dev)
{
	struct clk *clk;
	int error;

	clk = of_clk_get_from_provider(clkspec);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	error = pm_clk_create(dev);
	if (error) {
		clk_put(clk);
		return error;
	}

	error = pm_clk_add_clk(dev, clk);
	if (error) {
		pm_clk_destroy(dev);
		clk_put(clk);
	}

	return error;
}

static int r9a06g032_attach_dev(struct generic_pm_domain *pd,
				struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct of_phandle_args clkspec;
	int i = 0;
	int error;
	int index;

	while (!of_parse_phandle_with_args(np, "clocks", "#clock-cells", i++,
					   &clkspec)) {
		if (clkspec.np != pd->dev.of_node)
			continue;

		index = clkspec.args[0];
		if (index < R9A06G032_CLOCK_COUNT &&
		    r9a06g032_clocks[index].managed) {
			error = create_add_module_clock(&clkspec, dev);
			of_node_put(clkspec.np);
			if (error)
				return error;
		}
	}

	return 0;
}

static void r9a06g032_detach_dev(struct generic_pm_domain *unused, struct device *dev)
{
	if (!pm_clk_no_clocks(dev))
		pm_clk_destroy(dev);
}

static int r9a06g032_add_clk_domain(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct generic_pm_domain *pd;

	pd = devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	pd->name = np->name;
	pd->flags = GENPD_FLAG_PM_CLK | GENPD_FLAG_ALWAYS_ON |
		    GENPD_FLAG_ACTIVE_WAKEUP;
	pd->attach_dev = r9a06g032_attach_dev;
	pd->detach_dev = r9a06g032_detach_dev;
	pm_genpd_init(pd, &pm_domain_always_on_gov, false);

	of_genpd_add_provider_simple(np, pd);
	return 0;
}

static void
r9a06g032_clk_gate_set(struct r9a06g032_priv *clocks,
		       struct r9a06g032_gate *g, int on)
{
	unsigned long flags;

	WARN_ON(!g->gate.reg && !g->gate.bit);

	spin_lock_irqsave(&clocks->lock, flags);
	clk_rdesc_set(clocks, g->gate, on);
	/* De-assert reset */
	clk_rdesc_set(clocks, g->reset, 1);
	spin_unlock_irqrestore(&clocks->lock, flags);

	/* Hardware manual recommends 5us delay after enabling clock & reset */
	udelay(5);

	/* If the peripheral is memory mapped (i.e. an AXI slave), there is an
	 * associated SLVRDY bit in the System Controller that needs to be set
	 * so that the FlexWAY bus fabric passes on the read/write requests.
	 */
	spin_lock_irqsave(&clocks->lock, flags);
	clk_rdesc_set(clocks, g->ready, on);
	/* Clear 'Master Idle Request' bit */
	clk_rdesc_set(clocks, g->midle, !on);
	spin_unlock_irqrestore(&clocks->lock, flags);

	/* Note: We don't wait for FlexWAY Socket Connection signal */
}

static int r9a06g032_clk_gate_enable(struct clk_hw *hw)
{
	struct r9a06g032_clk_gate *g = to_r9a06g032_gate(hw);

	r9a06g032_clk_gate_set(g->clocks, &g->gate, 1);
	return 0;
}

static void r9a06g032_clk_gate_disable(struct clk_hw *hw)
{
	struct r9a06g032_clk_gate *g = to_r9a06g032_gate(hw);

	r9a06g032_clk_gate_set(g->clocks, &g->gate, 0);
}

static int r9a06g032_clk_gate_is_enabled(struct clk_hw *hw)
{
	struct r9a06g032_clk_gate *g = to_r9a06g032_gate(hw);

	/* if clock is in reset, the gate might be on, and still not 'be' on */
	if (g->gate.reset.reg && !clk_rdesc_get(g->clocks, g->gate.reset))
		return 0;

	return clk_rdesc_get(g->clocks, g->gate.gate);
}

static const struct clk_ops r9a06g032_clk_gate_ops = {
	.enable = r9a06g032_clk_gate_enable,
	.disable = r9a06g032_clk_gate_disable,
	.is_enabled = r9a06g032_clk_gate_is_enabled,
};

static struct clk *
r9a06g032_register_gate(struct r9a06g032_priv *clocks,
			const char *parent_name,
			const struct r9a06g032_clkdesc *desc)
{
	struct clk *clk;
	struct r9a06g032_clk_gate *g;
	struct clk_init_data init = {};

	g = kzalloc(sizeof(*g), GFP_KERNEL);
	if (!g)
		return NULL;

	init.name = desc->name;
	init.ops = &r9a06g032_clk_gate_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;

	g->clocks = clocks;
	g->index = desc->index;
	g->gate = desc->gate;
	g->hw.init = &init;

	/*
	 * important here, some clocks are already in use by the CM3, we
	 * have to assume they are not Linux's to play with and try to disable
	 * at the end of the boot!
	 */
	if (r9a06g032_clk_gate_is_enabled(&g->hw)) {
		init.flags |= CLK_IS_CRITICAL;
		pr_debug("%s was enabled, making read-only\n", desc->name);
	}

	clk = clk_register(NULL, &g->hw);
	if (IS_ERR(clk)) {
		kfree(g);
		return NULL;
	}
	return clk;
}

struct r9a06g032_clk_div {
	struct clk_hw hw;
	struct r9a06g032_priv *clocks;
	u16 index;
	u16 reg;
	u16 min, max;
	u8 table_size;
	u16 table[8];	/* we know there are no more than 8 */
};

#define to_r9a06g032_div(_hw) \
		container_of(_hw, struct r9a06g032_clk_div, hw)

static unsigned long
r9a06g032_div_recalc_rate(struct clk_hw *hw,
			  unsigned long parent_rate)
{
	struct r9a06g032_clk_div *clk = to_r9a06g032_div(hw);
	u32 __iomem *reg = clk->clocks->reg + (4 * clk->reg);
	u32 div = readl(reg);

	if (div < clk->min)
		div = clk->min;
	else if (div > clk->max)
		div = clk->max;
	return DIV_ROUND_UP(parent_rate, div);
}

/*
 * Attempts to find a value that is in range of min,max,
 * and if a table of set dividers was specified for this
 * register, try to find the fixed divider that is the closest
 * to the target frequency
 */
static long
r9a06g032_div_clamp_div(struct r9a06g032_clk_div *clk,
			unsigned long rate, unsigned long prate)
{
	/* + 1 to cope with rates that have the remainder dropped */
	u32 div = DIV_ROUND_UP(prate, rate + 1);
	int i;

	if (div <= clk->min)
		return clk->min;
	if (div >= clk->max)
		return clk->max;

	for (i = 0; clk->table_size && i < clk->table_size - 1; i++) {
		if (div >= clk->table[i] && div <= clk->table[i + 1]) {
			unsigned long m = rate -
				DIV_ROUND_UP(prate, clk->table[i]);
			unsigned long p =
				DIV_ROUND_UP(prate, clk->table[i + 1]) -
				rate;
			/*
			 * select the divider that generates
			 * the value closest to the ideal frequency
			 */
			div = p >= m ? clk->table[i] : clk->table[i + 1];
			return div;
		}
	}
	return div;
}

static int
r9a06g032_div_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	struct r9a06g032_clk_div *clk = to_r9a06g032_div(hw);
	u32 div = DIV_ROUND_UP(req->best_parent_rate, req->rate);

	pr_devel("%s %pC %ld (prate %ld) (wanted div %u)\n", __func__,
		 hw->clk, req->rate, req->best_parent_rate, div);
	pr_devel("   min %d (%ld) max %d (%ld)\n",
		 clk->min, DIV_ROUND_UP(req->best_parent_rate, clk->min),
		 clk->max, DIV_ROUND_UP(req->best_parent_rate, clk->max));

	div = r9a06g032_div_clamp_div(clk, req->rate, req->best_parent_rate);
	/*
	 * this is a hack. Currently the serial driver asks for a clock rate
	 * that is 16 times the baud rate -- and that is wildly outside the
	 * range of the UART divider, somehow there is no provision for that
	 * case of 'let the divider as is if outside range'.
	 * The serial driver *shouldn't* play with these clocks anyway, there's
	 * several uarts attached to this divider, and changing this impacts
	 * everyone.
	 */
	if (clk->index == R9A06G032_DIV_UART ||
	    clk->index == R9A06G032_DIV_P2_PG) {
		pr_devel("%s div uart hack!\n", __func__);
		req->rate = clk_get_rate(hw->clk);
		return 0;
	}
	req->rate = DIV_ROUND_UP(req->best_parent_rate, div);
	pr_devel("%s %pC %ld / %u = %ld\n", __func__, hw->clk,
		 req->best_parent_rate, div, req->rate);
	return 0;
}

static int
r9a06g032_div_set_rate(struct clk_hw *hw,
		       unsigned long rate, unsigned long parent_rate)
{
	struct r9a06g032_clk_div *clk = to_r9a06g032_div(hw);
	/* + 1 to cope with rates that have the remainder dropped */
	u32 div = DIV_ROUND_UP(parent_rate, rate + 1);
	u32 __iomem *reg = clk->clocks->reg + (4 * clk->reg);

	pr_devel("%s %pC rate %ld parent %ld div %d\n", __func__, hw->clk,
		 rate, parent_rate, div);

	/*
	 * Need to write the bit 31 with the divider value to
	 * latch it. Technically we should wait until it has been
	 * cleared too.
	 * TODO: Find whether this callback is sleepable, in case
	 * the hardware /does/ require some sort of spinloop here.
	 */
	writel(div | BIT(31), reg);

	return 0;
}

static const struct clk_ops r9a06g032_clk_div_ops = {
	.recalc_rate = r9a06g032_div_recalc_rate,
	.determine_rate = r9a06g032_div_determine_rate,
	.set_rate = r9a06g032_div_set_rate,
};

static struct clk *
r9a06g032_register_div(struct r9a06g032_priv *clocks,
		       const char *parent_name,
		       const struct r9a06g032_clkdesc *desc)
{
	struct r9a06g032_clk_div *div;
	struct clk *clk;
	struct clk_init_data init = {};
	unsigned int i;

	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div)
		return NULL;

	init.name = desc->name;
	init.ops = &r9a06g032_clk_div_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;

	div->clocks = clocks;
	div->index = desc->index;
	div->reg = desc->reg;
	div->hw.init = &init;
	div->min = desc->div_min;
	div->max = desc->div_max;
	/* populate (optional) divider table fixed values */
	for (i = 0; i < ARRAY_SIZE(div->table) &&
	     i < ARRAY_SIZE(desc->div_table) && desc->div_table[i]; i++) {
		div->table[div->table_size++] = desc->div_table[i];
	}

	clk = clk_register(NULL, &div->hw);
	if (IS_ERR(clk)) {
		kfree(div);
		return NULL;
	}
	return clk;
}

/*
 * This clock provider handles the case of the R9A06G032 where you have
 * peripherals that have two potential clock source and two gates, one for
 * each of the clock source - the used clock source (for all sub clocks)
 * is selected by a single bit.
 * That single bit affects all sub-clocks, and therefore needs to change the
 * active gate (and turn the others off) and force a recalculation of the rates.
 *
 * This implements two clock providers, one 'bitselect' that
 * handles the switch between both parents, and another 'dualgate'
 * that knows which gate to poke at, depending on the parent's bit position.
 */
struct r9a06g032_clk_bitsel {
	struct clk_hw	hw;
	struct r9a06g032_priv *clocks;
	u16 index;
	struct regbit selector;		/* selector register + bit */
};

#define to_clk_bitselect(_hw) \
		container_of(_hw, struct r9a06g032_clk_bitsel, hw)

static u8 r9a06g032_clk_mux_get_parent(struct clk_hw *hw)
{
	struct r9a06g032_clk_bitsel *set = to_clk_bitselect(hw);

	return clk_rdesc_get(set->clocks, set->selector);
}

static int r9a06g032_clk_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct r9a06g032_clk_bitsel *set = to_clk_bitselect(hw);

	/* a single bit in the register selects one of two parent clocks */
	clk_rdesc_set(set->clocks, set->selector, !!index);

	return 0;
}

static const struct clk_ops clk_bitselect_ops = {
	.determine_rate = clk_hw_determine_rate_no_reparent,
	.get_parent = r9a06g032_clk_mux_get_parent,
	.set_parent = r9a06g032_clk_mux_set_parent,
};

static struct clk *
r9a06g032_register_bitsel(struct r9a06g032_priv *clocks,
			  const char *parent_name,
			  const struct r9a06g032_clkdesc *desc)
{
	struct clk *clk;
	struct r9a06g032_clk_bitsel *g;
	struct clk_init_data init = {};
	const char *names[2];

	/* allocate the gate */
	g = kzalloc(sizeof(*g), GFP_KERNEL);
	if (!g)
		return NULL;

	names[0] = parent_name;
	names[1] = "clk_pll_usb";

	init.name = desc->name;
	init.ops = &clk_bitselect_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = names;
	init.num_parents = 2;

	g->clocks = clocks;
	g->index = desc->index;
	g->selector = desc->dual.sel;
	g->hw.init = &init;

	clk = clk_register(NULL, &g->hw);
	if (IS_ERR(clk)) {
		kfree(g);
		return NULL;
	}
	return clk;
}

struct r9a06g032_clk_dualgate {
	struct clk_hw	hw;
	struct r9a06g032_priv *clocks;
	u16 index;
	struct regbit selector;		/* selector register + bit */
	struct r9a06g032_gate gate[2];
};

#define to_clk_dualgate(_hw) \
		container_of(_hw, struct r9a06g032_clk_dualgate, hw)

static int
r9a06g032_clk_dualgate_setenable(struct r9a06g032_clk_dualgate *g, int enable)
{
	u8 sel_bit = clk_rdesc_get(g->clocks, g->selector);

	/* we always turn off the 'other' gate, regardless */
	r9a06g032_clk_gate_set(g->clocks, &g->gate[!sel_bit], 0);
	r9a06g032_clk_gate_set(g->clocks, &g->gate[sel_bit], enable);

	return 0;
}

static int r9a06g032_clk_dualgate_enable(struct clk_hw *hw)
{
	struct r9a06g032_clk_dualgate *gate = to_clk_dualgate(hw);

	r9a06g032_clk_dualgate_setenable(gate, 1);

	return 0;
}

static void r9a06g032_clk_dualgate_disable(struct clk_hw *hw)
{
	struct r9a06g032_clk_dualgate *gate = to_clk_dualgate(hw);

	r9a06g032_clk_dualgate_setenable(gate, 0);
}

static int r9a06g032_clk_dualgate_is_enabled(struct clk_hw *hw)
{
	struct r9a06g032_clk_dualgate *g = to_clk_dualgate(hw);
	u8 sel_bit = clk_rdesc_get(g->clocks, g->selector);

	return clk_rdesc_get(g->clocks, g->gate[sel_bit].gate);
}

static const struct clk_ops r9a06g032_clk_dualgate_ops = {
	.enable = r9a06g032_clk_dualgate_enable,
	.disable = r9a06g032_clk_dualgate_disable,
	.is_enabled = r9a06g032_clk_dualgate_is_enabled,
};

static struct clk *
r9a06g032_register_dualgate(struct r9a06g032_priv *clocks,
			    const char *parent_name,
			    const struct r9a06g032_clkdesc *desc,
			    struct regbit sel)
{
	struct r9a06g032_clk_dualgate *g;
	struct clk *clk;
	struct clk_init_data init = {};

	/* allocate the gate */
	g = kzalloc(sizeof(*g), GFP_KERNEL);
	if (!g)
		return NULL;
	g->clocks = clocks;
	g->index = desc->index;
	g->selector = sel;
	g->gate[0].gate = desc->dual.g1;
	g->gate[0].reset = desc->dual.r1;
	g->gate[1].gate = desc->dual.g2;
	g->gate[1].reset = desc->dual.r2;

	init.name = desc->name;
	init.ops = &r9a06g032_clk_dualgate_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	g->hw.init = &init;
	/*
	 * important here, some clocks are already in use by the CM3, we
	 * have to assume they are not Linux's to play with and try to disable
	 * at the end of the boot!
	 */
	if (r9a06g032_clk_dualgate_is_enabled(&g->hw)) {
		init.flags |= CLK_IS_CRITICAL;
		pr_debug("%s was enabled, making read-only\n", desc->name);
	}

	clk = clk_register(NULL, &g->hw);
	if (IS_ERR(clk)) {
		kfree(g);
		return NULL;
	}
	return clk;
}

static void r9a06g032_clocks_del_clk_provider(void *data)
{
	of_clk_del_provider(data);
}

static void __init r9a06g032_init_h2mode(struct r9a06g032_priv *clocks)
{
	struct device_node *usbf_np = NULL;
	u32 usb;

	while ((usbf_np = of_find_compatible_node(usbf_np, NULL,
						  "renesas,rzn1-usbf"))) {
		if (of_device_is_available(usbf_np))
			break;
	}

	usb = readl(clocks->reg + R9A06G032_SYSCTRL_USB);
	if (usbf_np) {
		/* 1 host and 1 device mode */
		usb &= ~R9A06G032_SYSCTRL_USB_H2MODE;
		of_node_put(usbf_np);
	} else {
		/* 2 hosts mode */
		usb |= R9A06G032_SYSCTRL_USB_H2MODE;
	}
	writel(usb, clocks->reg + R9A06G032_SYSCTRL_USB);
}

static int __init r9a06g032_clocks_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct r9a06g032_priv *clocks;
	struct clk **clks;
	struct clk *mclk;
	unsigned int i;
	struct regbit uart_group_sel[2];
	int error;

	clocks = devm_kzalloc(dev, sizeof(*clocks), GFP_KERNEL);
	clks = devm_kcalloc(dev, R9A06G032_CLOCK_COUNT, sizeof(struct clk *),
			    GFP_KERNEL);
	if (!clocks || !clks)
		return -ENOMEM;

	spin_lock_init(&clocks->lock);

	clocks->data.clks = clks;
	clocks->data.clk_num = R9A06G032_CLOCK_COUNT;

	mclk = devm_clk_get(dev, "mclk");
	if (IS_ERR(mclk))
		return PTR_ERR(mclk);

	clocks->reg = of_iomap(np, 0);
	if (WARN_ON(!clocks->reg))
		return -ENOMEM;

	r9a06g032_init_h2mode(clocks);

	for (i = 0; i < ARRAY_SIZE(r9a06g032_clocks); ++i) {
		const struct r9a06g032_clkdesc *d = &r9a06g032_clocks[i];
		const char *parent_name = d->source ?
			__clk_get_name(clocks->data.clks[d->source - 1]) :
			__clk_get_name(mclk);
		struct clk *clk = NULL;

		switch (d->type) {
		case K_FFC:
			clk = clk_register_fixed_factor(NULL, d->name,
							parent_name, 0,
							d->mul, d->div);
			break;
		case K_GATE:
			clk = r9a06g032_register_gate(clocks, parent_name, d);
			break;
		case K_DIV:
			clk = r9a06g032_register_div(clocks, parent_name, d);
			break;
		case K_BITSEL:
			/* keep that selector register around */
			uart_group_sel[d->dual.group] = d->dual.sel;
			clk = r9a06g032_register_bitsel(clocks, parent_name, d);
			break;
		case K_DUALGATE:
			clk = r9a06g032_register_dualgate(clocks, parent_name,
							  d,
							  uart_group_sel[d->dual.group]);
			break;
		}
		clocks->data.clks[d->index] = clk;
	}
	error = of_clk_add_provider(np, of_clk_src_onecell_get, &clocks->data);
	if (error)
		return error;

	error = devm_add_action_or_reset(dev,
					r9a06g032_clocks_del_clk_provider, np);
	if (error)
		return error;

	error = r9a06g032_add_clk_domain(dev);
	if (error)
		return error;

	sysctrl_priv = clocks;

	error = of_platform_populate(np, NULL, NULL, dev);
	if (error)
		dev_err(dev, "Failed to populate children (%d)\n", error);

	return 0;
}

static const struct of_device_id r9a06g032_match[] = {
	{ .compatible = "renesas,r9a06g032-sysctrl" },
	{ }
};

static struct platform_driver r9a06g032_clock_driver = {
	.driver		= {
		.name	= "renesas,r9a06g032-sysctrl",
		.of_match_table = r9a06g032_match,
	},
};

static int __init r9a06g032_clocks_init(void)
{
	return platform_driver_probe(&r9a06g032_clock_driver,
			r9a06g032_clocks_probe);
}

subsys_initcall(r9a06g032_clocks_init);
