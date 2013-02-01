/*
 *  linux/drivers/pinctrl/pinmux-xway.c
 *  based on linux/drivers/pinctrl/pinmux-pxa910.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 *
 *  Copyright (C) 2012 John Crispin <blogic@openwrt.org>
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#include "pinctrl-lantiq.h"

#include <lantiq_soc.h>

/* we have 3 1/2 banks of 16 bit each */
#define PINS			16
#define PORT3			3
#define PORT(x)			(x / PINS)
#define PORT_PIN(x)		(x % PINS)

/* we have 2 mux bits that can be set for each pin */
#define MUX_ALT0	0x1
#define MUX_ALT1	0x2

/*
 * each bank has this offset apart from the 1/2 bank that is mixed into the
 * other 3 ranges
 */
#define REG_OFF			0x30

/* these are the offsets to our registers */
#define GPIO_BASE(p)		(REG_OFF * PORT(p))
#define GPIO_OUT(p)		GPIO_BASE(p)
#define GPIO_IN(p)		(GPIO_BASE(p) + 0x04)
#define GPIO_DIR(p)		(GPIO_BASE(p) + 0x08)
#define GPIO_ALT0(p)		(GPIO_BASE(p) + 0x0C)
#define GPIO_ALT1(p)		(GPIO_BASE(p) + 0x10)
#define GPIO_OD(p)		(GPIO_BASE(p) + 0x14)
#define GPIO_PUDSEL(p)		(GPIO_BASE(p) + 0x1c)
#define GPIO_PUDEN(p)		(GPIO_BASE(p) + 0x20)

/* the 1/2 port needs special offsets for some registers */
#define GPIO3_OD		(GPIO_BASE(0) + 0x24)
#define GPIO3_PUDSEL		(GPIO_BASE(0) + 0x28)
#define GPIO3_PUDEN		(GPIO_BASE(0) + 0x2C)
#define GPIO3_ALT1		(GPIO_BASE(PINS) + 0x24)

/* macros to help us access the registers */
#define gpio_getbit(m, r, p)	(!!(ltq_r32(m + r) & BIT(p)))
#define gpio_setbit(m, r, p)	ltq_w32_mask(0, BIT(p), m + r)
#define gpio_clearbit(m, r, p)	ltq_w32_mask(BIT(p), 0, m + r)

#define MFP_XWAY(a, f0, f1, f2, f3)	\
	{				\
		.name = #a,		\
		.pin = a,		\
		.func = {		\
			XWAY_MUX_##f0,	\
			XWAY_MUX_##f1,	\
			XWAY_MUX_##f2,	\
			XWAY_MUX_##f3,	\
		},			\
	}

#define GRP_MUX(a, m, p)		\
	{ .name = a, .mux = XWAY_MUX_##m, .pins = p, .npins = ARRAY_SIZE(p), }

#define FUNC_MUX(f, m)		\
	{ .func = f, .mux = XWAY_MUX_##m, }

#define XWAY_MAX_PIN		32
#define XR9_MAX_PIN		56

enum xway_mux {
	XWAY_MUX_GPIO = 0,
	XWAY_MUX_SPI,
	XWAY_MUX_ASC,
	XWAY_MUX_PCI,
	XWAY_MUX_CGU,
	XWAY_MUX_EBU,
	XWAY_MUX_JTAG,
	XWAY_MUX_EXIN,
	XWAY_MUX_TDM,
	XWAY_MUX_STP,
	XWAY_MUX_SIN,
	XWAY_MUX_GPT,
	XWAY_MUX_NMI,
	XWAY_MUX_MDIO,
	XWAY_MUX_MII,
	XWAY_MUX_EPHY,
	XWAY_MUX_DFE,
	XWAY_MUX_SDIO,
	XWAY_MUX_NONE = 0xffff,
};

static const struct ltq_mfp_pin xway_mfp[] = {
	/*       pin    f0	f1	f2	f3   */
	MFP_XWAY(GPIO0, GPIO,	EXIN,	NONE,	TDM),
	MFP_XWAY(GPIO1, GPIO,	EXIN,	NONE,	NONE),
	MFP_XWAY(GPIO2, GPIO,	CGU,	EXIN,	NONE),
	MFP_XWAY(GPIO3, GPIO,	CGU,	NONE,	PCI),
	MFP_XWAY(GPIO4, GPIO,	STP,	NONE,	ASC),
	MFP_XWAY(GPIO5, GPIO,	STP,	NONE,	NONE),
	MFP_XWAY(GPIO6, GPIO,	STP,	GPT,	ASC),
	MFP_XWAY(GPIO7, GPIO,	CGU,	PCI,	NONE),
	MFP_XWAY(GPIO8, GPIO,	CGU,	NMI,	NONE),
	MFP_XWAY(GPIO9, GPIO,	ASC,	SPI,	EXIN),
	MFP_XWAY(GPIO10, GPIO,	ASC,	SPI,	NONE),
	MFP_XWAY(GPIO11, GPIO,	ASC,	PCI,	SPI),
	MFP_XWAY(GPIO12, GPIO,	ASC,	NONE,	NONE),
	MFP_XWAY(GPIO13, GPIO,	EBU,	SPI,	NONE),
	MFP_XWAY(GPIO14, GPIO,	CGU,	PCI,	NONE),
	MFP_XWAY(GPIO15, GPIO,	SPI,	JTAG,	NONE),
	MFP_XWAY(GPIO16, GPIO,	SPI,	NONE,	JTAG),
	MFP_XWAY(GPIO17, GPIO,	SPI,	NONE,	JTAG),
	MFP_XWAY(GPIO18, GPIO,	SPI,	NONE,	JTAG),
	MFP_XWAY(GPIO19, GPIO,	PCI,	NONE,	NONE),
	MFP_XWAY(GPIO20, GPIO,	JTAG,	NONE,	NONE),
	MFP_XWAY(GPIO21, GPIO,	PCI,	EBU,	GPT),
	MFP_XWAY(GPIO22, GPIO,	SPI,	NONE,	NONE),
	MFP_XWAY(GPIO23, GPIO,	EBU,	PCI,	STP),
	MFP_XWAY(GPIO24, GPIO,	EBU,	TDM,	PCI),
	MFP_XWAY(GPIO25, GPIO,	TDM,	NONE,	ASC),
	MFP_XWAY(GPIO26, GPIO,	EBU,	NONE,	TDM),
	MFP_XWAY(GPIO27, GPIO,	TDM,	NONE,	ASC),
	MFP_XWAY(GPIO28, GPIO,	GPT,	NONE,	NONE),
	MFP_XWAY(GPIO29, GPIO,	PCI,	NONE,	NONE),
	MFP_XWAY(GPIO30, GPIO,	PCI,	NONE,	NONE),
	MFP_XWAY(GPIO31, GPIO,	EBU,	PCI,	NONE),
	MFP_XWAY(GPIO32, GPIO,	NONE,	NONE,	EBU),
	MFP_XWAY(GPIO33, GPIO,	NONE,	NONE,	EBU),
	MFP_XWAY(GPIO34, GPIO,	NONE,	NONE,	EBU),
	MFP_XWAY(GPIO35, GPIO,	NONE,	NONE,	EBU),
	MFP_XWAY(GPIO36, GPIO,	SIN,	NONE,	EBU),
	MFP_XWAY(GPIO37, GPIO,	PCI,	NONE,	NONE),
	MFP_XWAY(GPIO38, GPIO,	PCI,	NONE,	NONE),
	MFP_XWAY(GPIO39, GPIO,	EXIN,	NONE,	NONE),
	MFP_XWAY(GPIO40, GPIO,	NONE,	NONE,	NONE),
	MFP_XWAY(GPIO41, GPIO,	NONE,	NONE,	NONE),
	MFP_XWAY(GPIO42, GPIO,	MDIO,	NONE,	NONE),
	MFP_XWAY(GPIO43, GPIO,	MDIO,	NONE,	NONE),
	MFP_XWAY(GPIO44, GPIO,	NONE,	NONE,	SIN),
	MFP_XWAY(GPIO45, GPIO,	NONE,	NONE,	SIN),
	MFP_XWAY(GPIO46, GPIO,	NONE,	NONE,	EXIN),
	MFP_XWAY(GPIO47, GPIO,	NONE,	NONE,	SIN),
	MFP_XWAY(GPIO48, GPIO,	EBU,	NONE,	NONE),
	MFP_XWAY(GPIO49, GPIO,	EBU,	NONE,	NONE),
	MFP_XWAY(GPIO50, GPIO,	NONE,	NONE,	NONE),
	MFP_XWAY(GPIO51, GPIO,	NONE,	NONE,	NONE),
	MFP_XWAY(GPIO52, GPIO,	NONE,	NONE,	NONE),
	MFP_XWAY(GPIO53, GPIO,	NONE,	NONE,	NONE),
	MFP_XWAY(GPIO54, GPIO,	NONE,	NONE,	NONE),
	MFP_XWAY(GPIO55, GPIO,	NONE,	NONE,	NONE),
};

static const struct ltq_mfp_pin ase_mfp[] = {
	/*       pin    f0	f1	f2	f3   */
	MFP_XWAY(GPIO0, GPIO,	EXIN,	MII,	TDM),
	MFP_XWAY(GPIO1, GPIO,	STP,	DFE,	EBU),
	MFP_XWAY(GPIO2, GPIO,	STP,	DFE,	EPHY),
	MFP_XWAY(GPIO3, GPIO,	STP,	EPHY,	EBU),
	MFP_XWAY(GPIO4, GPIO,	GPT,	EPHY,	MII),
	MFP_XWAY(GPIO5, GPIO,	MII,	ASC,	GPT),
	MFP_XWAY(GPIO6, GPIO,	MII,	ASC,	EXIN),
	MFP_XWAY(GPIO7, GPIO,	SPI,	MII,	JTAG),
	MFP_XWAY(GPIO8, GPIO,	SPI,	MII,	JTAG),
	MFP_XWAY(GPIO9, GPIO,	SPI,	MII,	JTAG),
	MFP_XWAY(GPIO10, GPIO,	SPI,	MII,	JTAG),
	MFP_XWAY(GPIO11, GPIO,	EBU,	CGU,	JTAG),
	MFP_XWAY(GPIO12, GPIO,	EBU,	MII,	SDIO),
	MFP_XWAY(GPIO13, GPIO,	EBU,	MII,	CGU),
	MFP_XWAY(GPIO14, GPIO,	EBU,	SPI,	CGU),
	MFP_XWAY(GPIO15, GPIO,	EBU,	SPI,	SDIO),
	MFP_XWAY(GPIO16, GPIO,	NONE,	NONE,	NONE),
	MFP_XWAY(GPIO17, GPIO,	NONE,	NONE,	NONE),
	MFP_XWAY(GPIO18, GPIO,	NONE,	NONE,	NONE),
	MFP_XWAY(GPIO19, GPIO,	EBU,	MII,	SDIO),
	MFP_XWAY(GPIO20, GPIO,	EBU,	MII,	SDIO),
	MFP_XWAY(GPIO21, GPIO,	EBU,	MII,	SDIO),
	MFP_XWAY(GPIO22, GPIO,	EBU,	MII,	CGU),
	MFP_XWAY(GPIO23, GPIO,	EBU,	MII,	CGU),
	MFP_XWAY(GPIO24, GPIO,	EBU,	NONE,	MII),
	MFP_XWAY(GPIO25, GPIO,	EBU,	MII,	GPT),
	MFP_XWAY(GPIO26, GPIO,	EBU,	MII,	SDIO),
	MFP_XWAY(GPIO27, GPIO,	EBU,	NONE,	MII),
	MFP_XWAY(GPIO28, GPIO,	MII,	EBU,	SDIO),
	MFP_XWAY(GPIO29, GPIO,	EBU,	MII,	EXIN),
	MFP_XWAY(GPIO30, GPIO,	NONE,	NONE,	NONE),
	MFP_XWAY(GPIO31, GPIO,	NONE,	NONE,	NONE),
};

static const unsigned pins_jtag[] = {GPIO15, GPIO16, GPIO17, GPIO19, GPIO35};
static const unsigned pins_asc0[] = {GPIO11, GPIO12};
static const unsigned pins_asc0_cts_rts[] = {GPIO9, GPIO10};
static const unsigned pins_stp[] = {GPIO4, GPIO5, GPIO6};
static const unsigned pins_nmi[] = {GPIO8};
static const unsigned pins_mdio[] = {GPIO42, GPIO43};

static const unsigned pins_ebu_a24[] = {GPIO13};
static const unsigned pins_ebu_clk[] = {GPIO21};
static const unsigned pins_ebu_cs1[] = {GPIO23};
static const unsigned pins_ebu_a23[] = {GPIO24};
static const unsigned pins_ebu_wait[] = {GPIO26};
static const unsigned pins_ebu_a25[] = {GPIO31};
static const unsigned pins_ebu_rdy[] = {GPIO48};
static const unsigned pins_ebu_rd[] = {GPIO49};

static const unsigned pins_nand_ale[] = {GPIO13};
static const unsigned pins_nand_cs1[] = {GPIO23};
static const unsigned pins_nand_cle[] = {GPIO24};
static const unsigned pins_nand_rdy[] = {GPIO48};
static const unsigned pins_nand_rd[] = {GPIO49};

static const unsigned pins_exin0[] = {GPIO0};
static const unsigned pins_exin1[] = {GPIO1};
static const unsigned pins_exin2[] = {GPIO2};
static const unsigned pins_exin3[] = {GPIO39};
static const unsigned pins_exin4[] = {GPIO46};
static const unsigned pins_exin5[] = {GPIO9};

static const unsigned pins_spi[] = {GPIO16, GPIO17, GPIO18};
static const unsigned pins_spi_cs1[] = {GPIO15};
static const unsigned pins_spi_cs2[] = {GPIO21};
static const unsigned pins_spi_cs3[] = {GPIO13};
static const unsigned pins_spi_cs4[] = {GPIO10};
static const unsigned pins_spi_cs5[] = {GPIO9};
static const unsigned pins_spi_cs6[] = {GPIO11};

static const unsigned pins_gpt1[] = {GPIO28};
static const unsigned pins_gpt2[] = {GPIO21};
static const unsigned pins_gpt3[] = {GPIO6};

static const unsigned pins_clkout0[] = {GPIO8};
static const unsigned pins_clkout1[] = {GPIO7};
static const unsigned pins_clkout2[] = {GPIO3};
static const unsigned pins_clkout3[] = {GPIO2};

static const unsigned pins_pci_gnt1[] = {GPIO30};
static const unsigned pins_pci_gnt2[] = {GPIO23};
static const unsigned pins_pci_gnt3[] = {GPIO19};
static const unsigned pins_pci_gnt4[] = {GPIO38};
static const unsigned pins_pci_req1[] = {GPIO29};
static const unsigned pins_pci_req2[] = {GPIO31};
static const unsigned pins_pci_req3[] = {GPIO3};
static const unsigned pins_pci_req4[] = {GPIO37};

static const unsigned ase_pins_jtag[] = {GPIO7, GPIO8, GPIO9, GPIO10, GPIO11};
static const unsigned ase_pins_asc[] = {GPIO5, GPIO6};
static const unsigned ase_pins_stp[] = {GPIO1, GPIO2, GPIO3};
static const unsigned ase_pins_ephy[] = {GPIO2, GPIO3, GPIO4};
static const unsigned ase_pins_dfe[] = {GPIO1, GPIO2};

static const unsigned ase_pins_spi[] = {GPIO8, GPIO9, GPIO10};
static const unsigned ase_pins_spi_cs1[] = {GPIO7};
static const unsigned ase_pins_spi_cs2[] = {GPIO15};
static const unsigned ase_pins_spi_cs3[] = {GPIO14};

static const unsigned ase_pins_exin0[] = {GPIO6};
static const unsigned ase_pins_exin1[] = {GPIO29};
static const unsigned ase_pins_exin2[] = {GPIO0};

static const unsigned ase_pins_gpt1[] = {GPIO5};
static const unsigned ase_pins_gpt2[] = {GPIO4};
static const unsigned ase_pins_gpt3[] = {GPIO25};

static const struct ltq_pin_group xway_grps[] = {
	GRP_MUX("exin0", EXIN, pins_exin0),
	GRP_MUX("exin1", EXIN, pins_exin1),
	GRP_MUX("exin2", EXIN, pins_exin2),
	GRP_MUX("jtag", JTAG, pins_jtag),
	GRP_MUX("ebu a23", EBU, pins_ebu_a23),
	GRP_MUX("ebu a24", EBU, pins_ebu_a24),
	GRP_MUX("ebu a25", EBU, pins_ebu_a25),
	GRP_MUX("ebu clk", EBU, pins_ebu_clk),
	GRP_MUX("ebu cs1", EBU, pins_ebu_cs1),
	GRP_MUX("ebu wait", EBU, pins_ebu_wait),
	GRP_MUX("nand ale", EBU, pins_nand_ale),
	GRP_MUX("nand cs1", EBU, pins_nand_cs1),
	GRP_MUX("nand cle", EBU, pins_nand_cle),
	GRP_MUX("spi", SPI, pins_spi),
	GRP_MUX("spi_cs1", SPI, pins_spi_cs1),
	GRP_MUX("spi_cs2", SPI, pins_spi_cs2),
	GRP_MUX("spi_cs3", SPI, pins_spi_cs3),
	GRP_MUX("spi_cs4", SPI, pins_spi_cs4),
	GRP_MUX("spi_cs5", SPI, pins_spi_cs5),
	GRP_MUX("spi_cs6", SPI, pins_spi_cs6),
	GRP_MUX("asc0", ASC, pins_asc0),
	GRP_MUX("asc0 cts rts", ASC, pins_asc0_cts_rts),
	GRP_MUX("stp", STP, pins_stp),
	GRP_MUX("nmi", NMI, pins_nmi),
	GRP_MUX("gpt1", GPT, pins_gpt1),
	GRP_MUX("gpt2", GPT, pins_gpt2),
	GRP_MUX("gpt3", GPT, pins_gpt3),
	GRP_MUX("clkout0", CGU, pins_clkout0),
	GRP_MUX("clkout1", CGU, pins_clkout1),
	GRP_MUX("clkout2", CGU, pins_clkout2),
	GRP_MUX("clkout3", CGU, pins_clkout3),
	GRP_MUX("gnt1", PCI, pins_pci_gnt1),
	GRP_MUX("gnt2", PCI, pins_pci_gnt2),
	GRP_MUX("gnt3", PCI, pins_pci_gnt3),
	GRP_MUX("req1", PCI, pins_pci_req1),
	GRP_MUX("req2", PCI, pins_pci_req2),
	GRP_MUX("req3", PCI, pins_pci_req3),
/* xrx only */
	GRP_MUX("nand rdy", EBU, pins_nand_rdy),
	GRP_MUX("nand rd", EBU, pins_nand_rd),
	GRP_MUX("exin3", EXIN, pins_exin3),
	GRP_MUX("exin4", EXIN, pins_exin4),
	GRP_MUX("exin5", EXIN, pins_exin5),
	GRP_MUX("gnt4", PCI, pins_pci_gnt4),
	GRP_MUX("req4", PCI, pins_pci_gnt4),
	GRP_MUX("mdio", MDIO, pins_mdio),
};

static const struct ltq_pin_group ase_grps[] = {
	GRP_MUX("exin0", EXIN, ase_pins_exin0),
	GRP_MUX("exin1", EXIN, ase_pins_exin1),
	GRP_MUX("exin2", EXIN, ase_pins_exin2),
	GRP_MUX("jtag", JTAG, ase_pins_jtag),
	GRP_MUX("stp", STP, ase_pins_stp),
	GRP_MUX("asc", ASC, ase_pins_asc),
	GRP_MUX("gpt1", GPT, ase_pins_gpt1),
	GRP_MUX("gpt2", GPT, ase_pins_gpt2),
	GRP_MUX("gpt3", GPT, ase_pins_gpt3),
	GRP_MUX("ephy", EPHY, ase_pins_ephy),
	GRP_MUX("dfe", DFE, ase_pins_dfe),
	GRP_MUX("spi", SPI, ase_pins_spi),
	GRP_MUX("spi_cs1", SPI, ase_pins_spi_cs1),
	GRP_MUX("spi_cs2", SPI, ase_pins_spi_cs2),
	GRP_MUX("spi_cs3", SPI, ase_pins_spi_cs3),
};

static const char * const xway_pci_grps[] = {"gnt1", "gnt2",
						"gnt3", "req1",
						"req2", "req3"};
static const char * const xway_spi_grps[] = {"spi", "spi_cs1",
						"spi_cs2", "spi_cs3",
						"spi_cs4", "spi_cs5",
						"spi_cs6"};
static const char * const xway_cgu_grps[] = {"clkout0", "clkout1",
						"clkout2", "clkout3"};
static const char * const xway_ebu_grps[] = {"ebu a23", "ebu a24",
						"ebu a25", "ebu cs1",
						"ebu wait", "ebu clk",
						"nand ale", "nand cs1",
						"nand cle"};
static const char * const xway_exin_grps[] = {"exin0", "exin1", "exin2"};
static const char * const xway_gpt_grps[] = {"gpt1", "gpt2", "gpt3"};
static const char * const xway_asc_grps[] = {"asc0", "asc0 cts rts"};
static const char * const xway_jtag_grps[] = {"jtag"};
static const char * const xway_stp_grps[] = {"stp"};
static const char * const xway_nmi_grps[] = {"nmi"};

/* ar9/vr9/gr9 */
static const char * const xrx_mdio_grps[] = {"mdio"};
static const char * const xrx_ebu_grps[] = {"ebu a23", "ebu a24",
						"ebu a25", "ebu cs1",
						"ebu wait", "ebu clk",
						"nand ale", "nand cs1",
						"nand cle", "nand rdy",
						"nand rd"};
static const char * const xrx_exin_grps[] = {"exin0", "exin1", "exin2",
						"exin3", "exin4", "exin5"};
static const char * const xrx_pci_grps[] = {"gnt1", "gnt2",
						"gnt3", "gnt4",
						"req1", "req2",
						"req3", "req4"};

/* ase */
static const char * const ase_exin_grps[] = {"exin0", "exin1", "exin2"};
static const char * const ase_gpt_grps[] = {"gpt1", "gpt2", "gpt3"};
static const char * const ase_dfe_grps[] = {"dfe"};
static const char * const ase_ephy_grps[] = {"ephy"};
static const char * const ase_asc_grps[] = {"asc"};
static const char * const ase_jtag_grps[] = {"jtag"};
static const char * const ase_stp_grps[] = {"stp"};
static const char * const ase_spi_grps[] = {"spi", "spi_cs1",
						"spi_cs2", "spi_cs3"};

static const struct ltq_pmx_func danube_funcs[] = {
	{"spi",		ARRAY_AND_SIZE(xway_spi_grps)},
	{"asc",		ARRAY_AND_SIZE(xway_asc_grps)},
	{"cgu",		ARRAY_AND_SIZE(xway_cgu_grps)},
	{"jtag",	ARRAY_AND_SIZE(xway_jtag_grps)},
	{"exin",	ARRAY_AND_SIZE(xway_exin_grps)},
	{"stp",		ARRAY_AND_SIZE(xway_stp_grps)},
	{"gpt",		ARRAY_AND_SIZE(xway_gpt_grps)},
	{"nmi",		ARRAY_AND_SIZE(xway_nmi_grps)},
	{"pci",		ARRAY_AND_SIZE(xway_pci_grps)},
	{"ebu",		ARRAY_AND_SIZE(xway_ebu_grps)},
};

static const struct ltq_pmx_func xrx_funcs[] = {
	{"spi",		ARRAY_AND_SIZE(xway_spi_grps)},
	{"asc",		ARRAY_AND_SIZE(xway_asc_grps)},
	{"cgu",		ARRAY_AND_SIZE(xway_cgu_grps)},
	{"jtag",	ARRAY_AND_SIZE(xway_jtag_grps)},
	{"exin",	ARRAY_AND_SIZE(xrx_exin_grps)},
	{"stp",		ARRAY_AND_SIZE(xway_stp_grps)},
	{"gpt",		ARRAY_AND_SIZE(xway_gpt_grps)},
	{"nmi",		ARRAY_AND_SIZE(xway_nmi_grps)},
	{"pci",		ARRAY_AND_SIZE(xrx_pci_grps)},
	{"ebu",		ARRAY_AND_SIZE(xrx_ebu_grps)},
	{"mdio",	ARRAY_AND_SIZE(xrx_mdio_grps)},
};

static const struct ltq_pmx_func ase_funcs[] = {
	{"spi",		ARRAY_AND_SIZE(ase_spi_grps)},
	{"asc",		ARRAY_AND_SIZE(ase_asc_grps)},
	{"jtag",	ARRAY_AND_SIZE(ase_jtag_grps)},
	{"exin",	ARRAY_AND_SIZE(ase_exin_grps)},
	{"stp",		ARRAY_AND_SIZE(ase_stp_grps)},
	{"gpt",		ARRAY_AND_SIZE(ase_gpt_grps)},
	{"ephy",	ARRAY_AND_SIZE(ase_ephy_grps)},
	{"dfe",		ARRAY_AND_SIZE(ase_dfe_grps)},
};

/* ---------  pinconf related code --------- */
static int xway_pinconf_get(struct pinctrl_dev *pctldev,
				unsigned pin,
				unsigned long *config)
{
	struct ltq_pinmux_info *info = pinctrl_dev_get_drvdata(pctldev);
	enum ltq_pinconf_param param = LTQ_PINCONF_UNPACK_PARAM(*config);
	int port = PORT(pin);
	u32 reg;

	switch (param) {
	case LTQ_PINCONF_PARAM_OPEN_DRAIN:
		if (port == PORT3)
			reg = GPIO3_OD;
		else
			reg = GPIO_OD(pin);
		*config = LTQ_PINCONF_PACK(param,
			!gpio_getbit(info->membase[0], reg, PORT_PIN(pin)));
		break;

	case LTQ_PINCONF_PARAM_PULL:
		if (port == PORT3)
			reg = GPIO3_PUDEN;
		else
			reg = GPIO_PUDEN(pin);
		if (!gpio_getbit(info->membase[0], reg, PORT_PIN(pin))) {
			*config = LTQ_PINCONF_PACK(param, 0);
			break;
		}

		if (port == PORT3)
			reg = GPIO3_PUDSEL;
		else
			reg = GPIO_PUDSEL(pin);
		if (!gpio_getbit(info->membase[0], reg, PORT_PIN(pin)))
			*config = LTQ_PINCONF_PACK(param, 2);
		else
			*config = LTQ_PINCONF_PACK(param, 1);
		break;

	default:
		dev_err(pctldev->dev, "Invalid config param %04x\n", param);
		return -ENOTSUPP;
	}
	return 0;
}

static int xway_pinconf_set(struct pinctrl_dev *pctldev,
				unsigned pin,
				unsigned long config)
{
	struct ltq_pinmux_info *info = pinctrl_dev_get_drvdata(pctldev);
	enum ltq_pinconf_param param = LTQ_PINCONF_UNPACK_PARAM(config);
	int arg = LTQ_PINCONF_UNPACK_ARG(config);
	int port = PORT(pin);
	u32 reg;

	switch (param) {
	case LTQ_PINCONF_PARAM_OPEN_DRAIN:
		if (port == PORT3)
			reg = GPIO3_OD;
		else
			reg = GPIO_OD(pin);
		gpio_setbit(info->membase[0], reg, PORT_PIN(pin));
		break;

	case LTQ_PINCONF_PARAM_PULL:
		if (port == PORT3)
			reg = GPIO3_PUDEN;
		else
			reg = GPIO_PUDEN(pin);
		if (arg == 0) {
			gpio_clearbit(info->membase[0], reg, PORT_PIN(pin));
			break;
		}
		gpio_setbit(info->membase[0], reg, PORT_PIN(pin));

		if (port == PORT3)
			reg = GPIO3_PUDSEL;
		else
			reg = GPIO_PUDSEL(pin);
		if (arg == 1)
			gpio_clearbit(info->membase[0], reg, PORT_PIN(pin));
		else if (arg == 2)
			gpio_setbit(info->membase[0], reg, PORT_PIN(pin));
		else
			dev_err(pctldev->dev, "Invalid pull value %d\n", arg);
		break;

	default:
		dev_err(pctldev->dev, "Invalid config param %04x\n", param);
		return -ENOTSUPP;
	}
	return 0;
}

int xway_pinconf_group_set(struct pinctrl_dev *pctldev,
			unsigned selector,
			unsigned long config)
{
	struct ltq_pinmux_info *info = pinctrl_dev_get_drvdata(pctldev);
	int i, ret = 0;

	for (i = 0; i < info->grps[selector].npins && !ret; i++)
		ret = xway_pinconf_set(pctldev,
				info->grps[selector].pins[i], config);

	return ret;
}

static struct pinconf_ops xway_pinconf_ops = {
	.pin_config_get	= xway_pinconf_get,
	.pin_config_set	= xway_pinconf_set,
	.pin_config_group_set = xway_pinconf_group_set,
};

static struct pinctrl_desc xway_pctrl_desc = {
	.owner		= THIS_MODULE,
	.confops	= &xway_pinconf_ops,
};

static inline int xway_mux_apply(struct pinctrl_dev *pctrldev,
				int pin, int mux)
{
	struct ltq_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);
	int port = PORT(pin);
	u32 alt1_reg = GPIO_ALT1(pin);

	if (port == PORT3)
		alt1_reg = GPIO3_ALT1;

	if (mux & MUX_ALT0)
		gpio_setbit(info->membase[0], GPIO_ALT0(pin), PORT_PIN(pin));
	else
		gpio_clearbit(info->membase[0], GPIO_ALT0(pin), PORT_PIN(pin));

	if (mux & MUX_ALT1)
		gpio_setbit(info->membase[0], alt1_reg, PORT_PIN(pin));
	else
		gpio_clearbit(info->membase[0], alt1_reg, PORT_PIN(pin));

	return 0;
}

static const struct ltq_cfg_param xway_cfg_params[] = {
	{"lantiq,pull",		LTQ_PINCONF_PARAM_PULL},
	{"lantiq,open-drain",	LTQ_PINCONF_PARAM_OPEN_DRAIN},
};

static struct ltq_pinmux_info xway_info = {
	.desc		= &xway_pctrl_desc,
	.apply_mux	= xway_mux_apply,
	.params		= xway_cfg_params,
	.num_params	= ARRAY_SIZE(xway_cfg_params),
};

/* ---------  gpio_chip related code --------- */
static void xway_gpio_set(struct gpio_chip *chip, unsigned int pin, int val)
{
	struct ltq_pinmux_info *info = dev_get_drvdata(chip->dev);

	if (val)
		gpio_setbit(info->membase[0], GPIO_OUT(pin), PORT_PIN(pin));
	else
		gpio_clearbit(info->membase[0], GPIO_OUT(pin), PORT_PIN(pin));
}

static int xway_gpio_get(struct gpio_chip *chip, unsigned int pin)
{
	struct ltq_pinmux_info *info = dev_get_drvdata(chip->dev);

	return gpio_getbit(info->membase[0], GPIO_IN(pin), PORT_PIN(pin));
}

static int xway_gpio_dir_in(struct gpio_chip *chip, unsigned int pin)
{
	struct ltq_pinmux_info *info = dev_get_drvdata(chip->dev);

	gpio_clearbit(info->membase[0], GPIO_DIR(pin), PORT_PIN(pin));

	return 0;
}

static int xway_gpio_dir_out(struct gpio_chip *chip, unsigned int pin, int val)
{
	struct ltq_pinmux_info *info = dev_get_drvdata(chip->dev);

	gpio_setbit(info->membase[0], GPIO_DIR(pin), PORT_PIN(pin));
	xway_gpio_set(chip, pin, val);

	return 0;
}

static int xway_gpio_req(struct gpio_chip *chip, unsigned offset)
{
	int gpio = chip->base + offset;

	return pinctrl_request_gpio(gpio);
}

static void xway_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	int gpio = chip->base + offset;

	pinctrl_free_gpio(gpio);
}

static struct gpio_chip xway_chip = {
	.label = "gpio-xway",
	.direction_input = xway_gpio_dir_in,
	.direction_output = xway_gpio_dir_out,
	.get = xway_gpio_get,
	.set = xway_gpio_set,
	.request = xway_gpio_req,
	.free = xway_gpio_free,
	.base = -1,
};


/* --------- register the pinctrl layer --------- */
static const unsigned xway_exin_pin_map[] = {GPIO0, GPIO1, GPIO2, GPIO39, GPIO46, GPIO9};
static const unsigned ase_exin_pins_map[] = {GPIO6, GPIO29, GPIO0};

static struct pinctrl_xway_soc {
	int pin_count;
	const struct ltq_mfp_pin *mfp;
	const struct ltq_pin_group *grps;
	unsigned int num_grps;
	const struct ltq_pmx_func *funcs;
	unsigned int num_funcs;
	const unsigned *exin;
	unsigned int num_exin;
} soc_cfg[] = {
	/* legacy xway */
	{XWAY_MAX_PIN, xway_mfp,
		xway_grps, ARRAY_SIZE(xway_grps),
		danube_funcs, ARRAY_SIZE(danube_funcs),
		xway_exin_pin_map, 3},
	/* xway xr9 series */
	{XR9_MAX_PIN, xway_mfp,
		xway_grps, ARRAY_SIZE(xway_grps),
		xrx_funcs, ARRAY_SIZE(xrx_funcs),
		xway_exin_pin_map, 6},
	/* xway ase series */
	{XWAY_MAX_PIN, ase_mfp,
		ase_grps, ARRAY_SIZE(ase_grps),
		ase_funcs, ARRAY_SIZE(ase_funcs),
		ase_exin_pins_map, 3},
};

static struct pinctrl_gpio_range xway_gpio_range = {
	.name	= "XWAY GPIO",
	.gc	= &xway_chip,
};

static const struct of_device_id xway_match[] = {
	{ .compatible = "lantiq,pinctrl-xway", .data = &soc_cfg[0]},
	{ .compatible = "lantiq,pinctrl-xr9", .data = &soc_cfg[1]},
	{ .compatible = "lantiq,pinctrl-ase", .data = &soc_cfg[2]},
	{},
};
MODULE_DEVICE_TABLE(of, xway_match);

static int pinmux_xway_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct pinctrl_xway_soc *xway_soc;
	struct resource *res;
	int ret, i;

	/* get and remap our register range */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get resource\n");
		return -ENOENT;
	}
	xway_info.membase[0] = devm_request_and_ioremap(&pdev->dev, res);
	if (!xway_info.membase[0]) {
		dev_err(&pdev->dev, "Failed to remap resource\n");
		return -ENOMEM;
	}

	match = of_match_device(xway_match, &pdev->dev);
	if (match)
		xway_soc = (const struct pinctrl_xway_soc *) match->data;
	else
		xway_soc = &soc_cfg[0];

	/* find out how many pads we have */
	xway_chip.ngpio = xway_soc->pin_count;

	/* load our pad descriptors */
	xway_info.pads = devm_kzalloc(&pdev->dev,
			sizeof(struct pinctrl_pin_desc) * xway_chip.ngpio,
			GFP_KERNEL);
	if (!xway_info.pads) {
		dev_err(&pdev->dev, "Failed to allocate pads\n");
		return -ENOMEM;
	}
	for (i = 0; i < xway_chip.ngpio; i++) {
		/* strlen("ioXY") + 1 = 5 */
		char *name = devm_kzalloc(&pdev->dev, 5, GFP_KERNEL);

		if (!name) {
			dev_err(&pdev->dev, "Failed to allocate pad name\n");
			return -ENOMEM;
		}
		snprintf(name, 5, "io%d", i);
		xway_info.pads[i].number = GPIO0 + i;
		xway_info.pads[i].name = name;
	}
	xway_pctrl_desc.pins = xway_info.pads;

	/* load the gpio chip */
	xway_chip.dev = &pdev->dev;
	of_gpiochip_add(&xway_chip);
	ret = gpiochip_add(&xway_chip);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register gpio chip\n");
		return ret;
	}

	/* setup the data needed by pinctrl */
	xway_pctrl_desc.name	= dev_name(&pdev->dev);
	xway_pctrl_desc.npins	= xway_chip.ngpio;

	xway_info.num_pads	= xway_chip.ngpio;
	xway_info.num_mfp	= xway_chip.ngpio;
	xway_info.mfp		= xway_soc->mfp;
	xway_info.grps		= xway_soc->grps;
	xway_info.num_grps	= xway_soc->num_grps;
	xway_info.funcs		= xway_soc->funcs;
	xway_info.num_funcs	= xway_soc->num_funcs;
	xway_info.exin		= xway_soc->exin;
	xway_info.num_exin	= xway_soc->num_exin;

	/* register with the generic lantiq layer */
	ret = ltq_pinctrl_register(pdev, &xway_info);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register pinctrl driver\n");
		return ret;
	}

	/* finish with registering the gpio range in pinctrl */
	xway_gpio_range.npins = xway_chip.ngpio;
	xway_gpio_range.base = xway_chip.base;
	pinctrl_add_gpio_range(xway_info.pctrl, &xway_gpio_range);
	dev_info(&pdev->dev, "Init done\n");
	return 0;
}

static struct platform_driver pinmux_xway_driver = {
	.probe	= pinmux_xway_probe,
	.driver = {
		.name	= "pinctrl-xway",
		.owner	= THIS_MODULE,
		.of_match_table = xway_match,
	},
};

static int __init pinmux_xway_init(void)
{
	return platform_driver_register(&pinmux_xway_driver);
}

core_initcall_sync(pinmux_xway_init);
