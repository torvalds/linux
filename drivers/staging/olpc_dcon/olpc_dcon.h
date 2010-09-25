#ifndef OLPC_DCON_H_
#define OLPC_DCON_H_

/* DCON registers */

#define DCON_REG_ID		 0
#define DCON_REG_MODE		 1

#define MODE_PASSTHRU	(1<<0)
#define MODE_SLEEP	(1<<1)
#define MODE_SLEEP_AUTO	(1<<2)
#define MODE_BL_ENABLE	(1<<3)
#define MODE_BLANK	(1<<4)
#define MODE_CSWIZZLE	(1<<5)
#define MODE_COL_AA	(1<<6)
#define MODE_MONO_LUMA	(1<<7)
#define MODE_SCAN_INT	(1<<8)
#define MODE_CLOCKDIV	(1<<9)
#define MODE_DEBUG	(1<<14)
#define MODE_SELFTEST	(1<<15)

#define DCON_REG_HRES		2
#define DCON_REG_HTOTAL		3
#define DCON_REG_HSYNC_WIDTH	4
#define DCON_REG_VRES		5
#define DCON_REG_VTOTAL		6
#define DCON_REG_VSYNC_WIDTH	7
#define DCON_REG_TIMEOUT	8
#define DCON_REG_SCAN_INT	9
#define DCON_REG_BRIGHT		10

/* GPIO registers (CS5536) */

#define MSR_LBAR_GPIO		0x5140000C

#define GPIOx_OUT_VAL     0x00
#define GPIOx_OUT_EN      0x04
#define GPIOx_IN_EN       0x20
#define GPIOx_INV_EN      0x24
#define GPIOx_IN_FLTR_EN  0x28
#define GPIOx_EVNTCNT_EN  0x2C
#define GPIOx_READ_BACK   0x30
#define GPIOx_EVNT_EN     0x38
#define GPIOx_NEGEDGE_EN  0x44
#define GPIOx_NEGEDGE_STS 0x4C
#define GPIO_FLT7_AMNT    0xD8
#define GPIO_MAP_X        0xE0
#define GPIO_MAP_Y        0xE4
#define GPIO_FE7_SEL      0xF7


/* Status values */

#define DCONSTAT_SCANINT	0
#define DCONSTAT_SCANINT_DCON	1
#define DCONSTAT_DISPLAYLOAD	2
#define DCONSTAT_MISSED		3

/* Source values */

#define DCON_SOURCE_DCON        0
#define DCON_SOURCE_CPU         1

/* Output values */
#define DCON_OUTPUT_COLOR       0
#define DCON_OUTPUT_MONO        1

/* Sleep values */
#define DCON_ACTIVE             0
#define DCON_SLEEP              1

/* Interrupt */
#define DCON_IRQ                6

#endif
