// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics 2020
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>

/* FMC2 Controller Registers */
#define FMC2_BCR1			0x0
#define FMC2_BTR1			0x4
#define FMC2_BCR(x)			((x) * 0x8 + FMC2_BCR1)
#define FMC2_BTR(x)			((x) * 0x8 + FMC2_BTR1)
#define FMC2_PCSCNTR			0x20
#define FMC2_CFGR			0x20
#define FMC2_SR				0x84
#define FMC2_BWTR1			0x104
#define FMC2_BWTR(x)			((x) * 0x8 + FMC2_BWTR1)
#define FMC2_SECCFGR			0x300
#define FMC2_CIDCFGR0			0x30c
#define FMC2_CIDCFGR(x)			((x) * 0x8 + FMC2_CIDCFGR0)
#define FMC2_SEMCR0			0x310
#define FMC2_SEMCR(x)			((x) * 0x8 + FMC2_SEMCR0)

/* Register: FMC2_BCR1 */
#define FMC2_BCR1_CCLKEN		BIT(20)
#define FMC2_BCR1_FMC2EN		BIT(31)

/* Register: FMC2_BCRx */
#define FMC2_BCR_MBKEN			BIT(0)
#define FMC2_BCR_MUXEN			BIT(1)
#define FMC2_BCR_MTYP			GENMASK(3, 2)
#define FMC2_BCR_MWID			GENMASK(5, 4)
#define FMC2_BCR_FACCEN			BIT(6)
#define FMC2_BCR_BURSTEN		BIT(8)
#define FMC2_BCR_WAITPOL		BIT(9)
#define FMC2_BCR_WAITCFG		BIT(11)
#define FMC2_BCR_WREN			BIT(12)
#define FMC2_BCR_WAITEN			BIT(13)
#define FMC2_BCR_EXTMOD			BIT(14)
#define FMC2_BCR_ASYNCWAIT		BIT(15)
#define FMC2_BCR_CPSIZE			GENMASK(18, 16)
#define FMC2_BCR_CBURSTRW		BIT(19)
#define FMC2_BCR_CSCOUNT		GENMASK(21, 20)
#define FMC2_BCR_NBLSET			GENMASK(23, 22)

/* Register: FMC2_BTRx/FMC2_BWTRx */
#define FMC2_BXTR_ADDSET		GENMASK(3, 0)
#define FMC2_BXTR_ADDHLD		GENMASK(7, 4)
#define FMC2_BXTR_DATAST		GENMASK(15, 8)
#define FMC2_BXTR_BUSTURN		GENMASK(19, 16)
#define FMC2_BTR_CLKDIV			GENMASK(23, 20)
#define FMC2_BTR_DATLAT			GENMASK(27, 24)
#define FMC2_BXTR_ACCMOD		GENMASK(29, 28)
#define FMC2_BXTR_DATAHLD		GENMASK(31, 30)

/* Register: FMC2_PCSCNTR */
#define FMC2_PCSCNTR_CSCOUNT		GENMASK(15, 0)
#define FMC2_PCSCNTR_CNTBEN(x)		BIT((x) + 16)

/* Register: FMC2_CFGR */
#define FMC2_CFGR_CLKDIV		GENMASK(19, 16)
#define FMC2_CFGR_CCLKEN		BIT(20)
#define FMC2_CFGR_FMC2EN		BIT(31)

/* Register: FMC2_SR */
#define FMC2_SR_ISOST			GENMASK(1, 0)

/* Register: FMC2_CIDCFGR */
#define FMC2_CIDCFGR_CFEN		BIT(0)
#define FMC2_CIDCFGR_SEMEN		BIT(1)
#define FMC2_CIDCFGR_SCID		GENMASK(6, 4)
#define FMC2_CIDCFGR_SEMWLC1		BIT(17)

/* Register: FMC2_SEMCR */
#define FMC2_SEMCR_SEM_MUTEX		BIT(0)
#define FMC2_SEMCR_SEMCID		GENMASK(6, 4)

#define FMC2_MAX_EBI_CE			4
#define FMC2_MAX_BANKS			5
#define FMC2_MAX_RESOURCES		6
#define FMC2_CID1			1

#define FMC2_BCR_CPSIZE_0		0x0
#define FMC2_BCR_CPSIZE_128		0x1
#define FMC2_BCR_CPSIZE_256		0x2
#define FMC2_BCR_CPSIZE_512		0x3
#define FMC2_BCR_CPSIZE_1024		0x4

#define FMC2_BCR_MWID_8			0x0
#define FMC2_BCR_MWID_16		0x1

#define FMC2_BCR_MTYP_SRAM		0x0
#define FMC2_BCR_MTYP_PSRAM		0x1
#define FMC2_BCR_MTYP_NOR		0x2

#define FMC2_BCR_CSCOUNT_0		0x0
#define FMC2_BCR_CSCOUNT_1		0x1
#define FMC2_BCR_CSCOUNT_64		0x2
#define FMC2_BCR_CSCOUNT_256		0x3

#define FMC2_BXTR_EXTMOD_A		0x0
#define FMC2_BXTR_EXTMOD_B		0x1
#define FMC2_BXTR_EXTMOD_C		0x2
#define FMC2_BXTR_EXTMOD_D		0x3

#define FMC2_BCR_NBLSET_MAX		0x3
#define FMC2_BXTR_ADDSET_MAX		0xf
#define FMC2_BXTR_ADDHLD_MAX		0xf
#define FMC2_BXTR_DATAST_MAX		0xff
#define FMC2_BXTR_BUSTURN_MAX		0xf
#define FMC2_BXTR_DATAHLD_MAX		0x3
#define FMC2_BTR_CLKDIV_MAX		0xf
#define FMC2_BTR_DATLAT_MAX		0xf
#define FMC2_PCSCNTR_CSCOUNT_MAX	0xff
#define FMC2_CFGR_CLKDIV_MAX		0xf

enum stm32_fmc2_ebi_bank {
	FMC2_EBI1 = 0,
	FMC2_EBI2,
	FMC2_EBI3,
	FMC2_EBI4,
	FMC2_NAND
};

enum stm32_fmc2_ebi_register_type {
	FMC2_REG_BCR = 1,
	FMC2_REG_BTR,
	FMC2_REG_BWTR,
	FMC2_REG_PCSCNTR,
	FMC2_REG_CFGR
};

enum stm32_fmc2_ebi_transaction_type {
	FMC2_ASYNC_MODE_1_SRAM = 0,
	FMC2_ASYNC_MODE_1_PSRAM,
	FMC2_ASYNC_MODE_A_SRAM,
	FMC2_ASYNC_MODE_A_PSRAM,
	FMC2_ASYNC_MODE_2_NOR,
	FMC2_ASYNC_MODE_B_NOR,
	FMC2_ASYNC_MODE_C_NOR,
	FMC2_ASYNC_MODE_D_NOR,
	FMC2_SYNC_READ_SYNC_WRITE_PSRAM,
	FMC2_SYNC_READ_ASYNC_WRITE_PSRAM,
	FMC2_SYNC_READ_SYNC_WRITE_NOR,
	FMC2_SYNC_READ_ASYNC_WRITE_NOR
};

enum stm32_fmc2_ebi_buswidth {
	FMC2_BUSWIDTH_8 = 8,
	FMC2_BUSWIDTH_16 = 16
};

enum stm32_fmc2_ebi_cpsize {
	FMC2_CPSIZE_0 = 0,
	FMC2_CPSIZE_128 = 128,
	FMC2_CPSIZE_256 = 256,
	FMC2_CPSIZE_512 = 512,
	FMC2_CPSIZE_1024 = 1024
};

enum stm32_fmc2_ebi_cscount {
	FMC2_CSCOUNT_0 = 0,
	FMC2_CSCOUNT_1 = 1,
	FMC2_CSCOUNT_64 = 64,
	FMC2_CSCOUNT_256 = 256
};

struct stm32_fmc2_ebi;

struct stm32_fmc2_ebi_data {
	const struct stm32_fmc2_prop *child_props;
	unsigned int nb_child_props;
	u32 fmc2_enable_reg;
	u32 fmc2_enable_bit;
	int (*nwait_used_by_ctrls)(struct stm32_fmc2_ebi *ebi);
	void (*set_setup)(struct stm32_fmc2_ebi *ebi);
	int (*save_setup)(struct stm32_fmc2_ebi *ebi);
	int (*check_rif)(struct stm32_fmc2_ebi *ebi, u32 resource);
	void (*put_sems)(struct stm32_fmc2_ebi *ebi);
	void (*get_sems)(struct stm32_fmc2_ebi *ebi);
};

struct stm32_fmc2_ebi {
	struct device *dev;
	struct clk *clk;
	struct regmap *regmap;
	const struct stm32_fmc2_ebi_data *data;
	u8 bank_assigned;
	u8 sem_taken;
	bool access_granted;

	u32 bcr[FMC2_MAX_EBI_CE];
	u32 btr[FMC2_MAX_EBI_CE];
	u32 bwtr[FMC2_MAX_EBI_CE];
	u32 pcscntr;
	u32 cfgr;
};

/*
 * struct stm32_fmc2_prop - STM32 FMC2 EBI property
 * @name: the device tree binding name of the property
 * @bprop: indicate that it is a boolean property
 * @mprop: indicate that it is a mandatory property
 * @reg_type: the register that have to be modified
 * @reg_mask: the bit that have to be modified in the selected register
 *            in case of it is a boolean property
 * @reset_val: the default value that have to be set in case the property
 *             has not been defined in the device tree
 * @check: this callback ckecks that the property is compliant with the
 *         transaction type selected
 * @calculate: this callback is called to calculate for exemple a timing
 *             set in nanoseconds in the device tree in clock cycles or in
 *             clock period
 * @set: this callback applies the values in the registers
 */
struct stm32_fmc2_prop {
	const char *name;
	bool bprop;
	bool mprop;
	int reg_type;
	u32 reg_mask;
	u32 reset_val;
	int (*check)(struct stm32_fmc2_ebi *ebi,
		     const struct stm32_fmc2_prop *prop, int cs);
	u32 (*calculate)(struct stm32_fmc2_ebi *ebi, int cs, u32 setup);
	int (*set)(struct stm32_fmc2_ebi *ebi,
		   const struct stm32_fmc2_prop *prop,
		   int cs, u32 setup);
};

static int stm32_fmc2_ebi_check_mux(struct stm32_fmc2_ebi *ebi,
				    const struct stm32_fmc2_prop *prop,
				    int cs)
{
	u32 bcr;
	int ret;

	ret = regmap_read(ebi->regmap, FMC2_BCR(cs), &bcr);
	if (ret)
		return ret;

	if (bcr & FMC2_BCR_MTYP)
		return 0;

	return -EINVAL;
}

static int stm32_fmc2_ebi_check_waitcfg(struct stm32_fmc2_ebi *ebi,
					const struct stm32_fmc2_prop *prop,
					int cs)
{
	u32 bcr, val = FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_NOR);
	int ret;

	ret = regmap_read(ebi->regmap, FMC2_BCR(cs), &bcr);
	if (ret)
		return ret;

	if ((bcr & FMC2_BCR_MTYP) == val && bcr & FMC2_BCR_BURSTEN)
		return 0;

	return -EINVAL;
}

static int stm32_fmc2_ebi_check_sync_trans(struct stm32_fmc2_ebi *ebi,
					   const struct stm32_fmc2_prop *prop,
					   int cs)
{
	u32 bcr;
	int ret;

	ret = regmap_read(ebi->regmap, FMC2_BCR(cs), &bcr);
	if (ret)
		return ret;

	if (bcr & FMC2_BCR_BURSTEN)
		return 0;

	return -EINVAL;
}

static int stm32_fmc2_ebi_mp25_check_cclk(struct stm32_fmc2_ebi *ebi,
					  const struct stm32_fmc2_prop *prop,
					  int cs)
{
	if (!ebi->access_granted)
		return -EACCES;

	return stm32_fmc2_ebi_check_sync_trans(ebi, prop, cs);
}

static int stm32_fmc2_ebi_mp25_check_clk_period(struct stm32_fmc2_ebi *ebi,
						const struct stm32_fmc2_prop *prop,
						int cs)
{
	u32 cfgr;
	int ret;

	ret = regmap_read(ebi->regmap, FMC2_CFGR, &cfgr);
	if (ret)
		return ret;

	if (cfgr & FMC2_CFGR_CCLKEN && !ebi->access_granted)
		return -EACCES;

	return stm32_fmc2_ebi_check_sync_trans(ebi, prop, cs);
}

static int stm32_fmc2_ebi_check_async_trans(struct stm32_fmc2_ebi *ebi,
					    const struct stm32_fmc2_prop *prop,
					    int cs)
{
	u32 bcr;
	int ret;

	ret = regmap_read(ebi->regmap, FMC2_BCR(cs), &bcr);
	if (ret)
		return ret;

	if (!(bcr & FMC2_BCR_BURSTEN) || !(bcr & FMC2_BCR_CBURSTRW))
		return 0;

	return -EINVAL;
}

static int stm32_fmc2_ebi_check_cpsize(struct stm32_fmc2_ebi *ebi,
				       const struct stm32_fmc2_prop *prop,
				       int cs)
{
	u32 bcr, val = FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_PSRAM);
	int ret;

	ret = regmap_read(ebi->regmap, FMC2_BCR(cs), &bcr);
	if (ret)
		return ret;

	if ((bcr & FMC2_BCR_MTYP) == val && bcr & FMC2_BCR_BURSTEN)
		return 0;

	return -EINVAL;
}

static int stm32_fmc2_ebi_check_address_hold(struct stm32_fmc2_ebi *ebi,
					     const struct stm32_fmc2_prop *prop,
					     int cs)
{
	u32 bcr, bxtr, val = FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_D);
	int ret;

	ret = regmap_read(ebi->regmap, FMC2_BCR(cs), &bcr);
	if (ret)
		return ret;

	if (prop->reg_type == FMC2_REG_BWTR)
		ret = regmap_read(ebi->regmap, FMC2_BWTR(cs), &bxtr);
	else
		ret = regmap_read(ebi->regmap, FMC2_BTR(cs), &bxtr);
	if (ret)
		return ret;

	if ((!(bcr & FMC2_BCR_BURSTEN) || !(bcr & FMC2_BCR_CBURSTRW)) &&
	    ((bxtr & FMC2_BXTR_ACCMOD) == val || bcr & FMC2_BCR_MUXEN))
		return 0;

	return -EINVAL;
}

static int stm32_fmc2_ebi_check_clk_period(struct stm32_fmc2_ebi *ebi,
					   const struct stm32_fmc2_prop *prop,
					   int cs)
{
	u32 bcr, bcr1;
	int ret;

	ret = regmap_read(ebi->regmap, FMC2_BCR(cs), &bcr);
	if (ret)
		return ret;

	if (cs) {
		ret = regmap_read(ebi->regmap, FMC2_BCR1, &bcr1);
		if (ret)
			return ret;
	} else {
		bcr1 = bcr;
	}

	if (bcr & FMC2_BCR_BURSTEN && (!cs || !(bcr1 & FMC2_BCR1_CCLKEN)))
		return 0;

	return -EINVAL;
}

static int stm32_fmc2_ebi_check_cclk(struct stm32_fmc2_ebi *ebi,
				     const struct stm32_fmc2_prop *prop,
				     int cs)
{
	if (cs)
		return -EINVAL;

	return stm32_fmc2_ebi_check_sync_trans(ebi, prop, cs);
}

static u32 stm32_fmc2_ebi_ns_to_clock_cycles(struct stm32_fmc2_ebi *ebi,
					     int cs, u32 setup)
{
	unsigned long hclk = clk_get_rate(ebi->clk);
	unsigned long hclkp = NSEC_PER_SEC / (hclk / 1000);

	return DIV_ROUND_UP(setup * 1000, hclkp);
}

static u32 stm32_fmc2_ebi_ns_to_clk_period(struct stm32_fmc2_ebi *ebi,
					   int cs, u32 setup)
{
	u32 nb_clk_cycles = stm32_fmc2_ebi_ns_to_clock_cycles(ebi, cs, setup);
	u32 bcr, btr, clk_period;
	int ret;

	ret = regmap_read(ebi->regmap, FMC2_BCR1, &bcr);
	if (ret)
		return ret;

	if (bcr & FMC2_BCR1_CCLKEN || !cs)
		ret = regmap_read(ebi->regmap, FMC2_BTR1, &btr);
	else
		ret = regmap_read(ebi->regmap, FMC2_BTR(cs), &btr);
	if (ret)
		return ret;

	clk_period = FIELD_GET(FMC2_BTR_CLKDIV, btr) + 1;

	return DIV_ROUND_UP(nb_clk_cycles, clk_period);
}

static u32 stm32_fmc2_ebi_mp25_ns_to_clk_period(struct stm32_fmc2_ebi *ebi,
						int cs, u32 setup)
{
	u32 nb_clk_cycles = stm32_fmc2_ebi_ns_to_clock_cycles(ebi, cs, setup);
	u32 cfgr, btr, clk_period;
	int ret;

	ret = regmap_read(ebi->regmap, FMC2_CFGR, &cfgr);
	if (ret)
		return ret;

	if (cfgr & FMC2_CFGR_CCLKEN) {
		clk_period = FIELD_GET(FMC2_CFGR_CLKDIV, cfgr) + 1;
	} else {
		ret = regmap_read(ebi->regmap, FMC2_BTR(cs), &btr);
		if (ret)
			return ret;

		clk_period = FIELD_GET(FMC2_BTR_CLKDIV, btr) + 1;
	}

	return DIV_ROUND_UP(nb_clk_cycles, clk_period);
}

static int stm32_fmc2_ebi_get_reg(int reg_type, int cs, u32 *reg)
{
	switch (reg_type) {
	case FMC2_REG_BCR:
		*reg = FMC2_BCR(cs);
		break;
	case FMC2_REG_BTR:
		*reg = FMC2_BTR(cs);
		break;
	case FMC2_REG_BWTR:
		*reg = FMC2_BWTR(cs);
		break;
	case FMC2_REG_PCSCNTR:
		*reg = FMC2_PCSCNTR;
		break;
	case FMC2_REG_CFGR:
		*reg = FMC2_CFGR;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int stm32_fmc2_ebi_set_bit_field(struct stm32_fmc2_ebi *ebi,
					const struct stm32_fmc2_prop *prop,
					int cs, u32 setup)
{
	u32 reg;
	int ret;

	ret = stm32_fmc2_ebi_get_reg(prop->reg_type, cs, &reg);
	if (ret)
		return ret;

	regmap_update_bits(ebi->regmap, reg, prop->reg_mask,
			   setup ? prop->reg_mask : 0);

	return 0;
}

static int stm32_fmc2_ebi_set_trans_type(struct stm32_fmc2_ebi *ebi,
					 const struct stm32_fmc2_prop *prop,
					 int cs, u32 setup)
{
	u32 bcr_mask, bcr = FMC2_BCR_WREN;
	u32 btr_mask, btr = 0;
	u32 bwtr_mask, bwtr = 0;

	bwtr_mask = FMC2_BXTR_ACCMOD;
	btr_mask = FMC2_BXTR_ACCMOD;
	bcr_mask = FMC2_BCR_MUXEN | FMC2_BCR_MTYP | FMC2_BCR_FACCEN |
		   FMC2_BCR_WREN | FMC2_BCR_WAITEN | FMC2_BCR_BURSTEN |
		   FMC2_BCR_EXTMOD | FMC2_BCR_CBURSTRW;

	switch (setup) {
	case FMC2_ASYNC_MODE_1_SRAM:
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_SRAM);
		/*
		 * MUXEN = 0, MTYP = 0, FACCEN = 0, BURSTEN = 0, WAITEN = 0,
		 * WREN = 1, EXTMOD = 0, CBURSTRW = 0, ACCMOD = 0
		 */
		break;
	case FMC2_ASYNC_MODE_1_PSRAM:
		/*
		 * MUXEN = 0, MTYP = 1, FACCEN = 0, BURSTEN = 0, WAITEN = 0,
		 * WREN = 1, EXTMOD = 0, CBURSTRW = 0, ACCMOD = 0
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_PSRAM);
		break;
	case FMC2_ASYNC_MODE_A_SRAM:
		/*
		 * MUXEN = 0, MTYP = 0, FACCEN = 0, BURSTEN = 0, WAITEN = 0,
		 * WREN = 1, EXTMOD = 1, CBURSTRW = 0, ACCMOD = 0
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_SRAM);
		bcr |= FMC2_BCR_EXTMOD;
		btr |= FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_A);
		bwtr |= FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_A);
		break;
	case FMC2_ASYNC_MODE_A_PSRAM:
		/*
		 * MUXEN = 0, MTYP = 1, FACCEN = 0, BURSTEN = 0, WAITEN = 0,
		 * WREN = 1, EXTMOD = 1, CBURSTRW = 0, ACCMOD = 0
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_PSRAM);
		bcr |= FMC2_BCR_EXTMOD;
		btr |= FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_A);
		bwtr |= FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_A);
		break;
	case FMC2_ASYNC_MODE_2_NOR:
		/*
		 * MUXEN = 0, MTYP = 2, FACCEN = 1, BURSTEN = 0, WAITEN = 0,
		 * WREN = 1, EXTMOD = 0, CBURSTRW = 0, ACCMOD = 0
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_NOR);
		bcr |= FMC2_BCR_FACCEN;
		break;
	case FMC2_ASYNC_MODE_B_NOR:
		/*
		 * MUXEN = 0, MTYP = 2, FACCEN = 1, BURSTEN = 0, WAITEN = 0,
		 * WREN = 1, EXTMOD = 1, CBURSTRW = 0, ACCMOD = 1
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_NOR);
		bcr |= FMC2_BCR_FACCEN | FMC2_BCR_EXTMOD;
		btr |= FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_B);
		bwtr |= FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_B);
		break;
	case FMC2_ASYNC_MODE_C_NOR:
		/*
		 * MUXEN = 0, MTYP = 2, FACCEN = 1, BURSTEN = 0, WAITEN = 0,
		 * WREN = 1, EXTMOD = 1, CBURSTRW = 0, ACCMOD = 2
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_NOR);
		bcr |= FMC2_BCR_FACCEN | FMC2_BCR_EXTMOD;
		btr |= FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_C);
		bwtr |= FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_C);
		break;
	case FMC2_ASYNC_MODE_D_NOR:
		/*
		 * MUXEN = 0, MTYP = 2, FACCEN = 1, BURSTEN = 0, WAITEN = 0,
		 * WREN = 1, EXTMOD = 1, CBURSTRW = 0, ACCMOD = 3
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_NOR);
		bcr |= FMC2_BCR_FACCEN | FMC2_BCR_EXTMOD;
		btr |= FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_D);
		bwtr |= FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_D);
		break;
	case FMC2_SYNC_READ_SYNC_WRITE_PSRAM:
		/*
		 * MUXEN = 0, MTYP = 1, FACCEN = 0, BURSTEN = 1, WAITEN = 0,
		 * WREN = 1, EXTMOD = 0, CBURSTRW = 1, ACCMOD = 0
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_PSRAM);
		bcr |= FMC2_BCR_BURSTEN | FMC2_BCR_CBURSTRW;
		break;
	case FMC2_SYNC_READ_ASYNC_WRITE_PSRAM:
		/*
		 * MUXEN = 0, MTYP = 1, FACCEN = 0, BURSTEN = 1, WAITEN = 0,
		 * WREN = 1, EXTMOD = 0, CBURSTRW = 0, ACCMOD = 0
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_PSRAM);
		bcr |= FMC2_BCR_BURSTEN;
		break;
	case FMC2_SYNC_READ_SYNC_WRITE_NOR:
		/*
		 * MUXEN = 0, MTYP = 2, FACCEN = 1, BURSTEN = 1, WAITEN = 0,
		 * WREN = 1, EXTMOD = 0, CBURSTRW = 1, ACCMOD = 0
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_NOR);
		bcr |= FMC2_BCR_FACCEN | FMC2_BCR_BURSTEN | FMC2_BCR_CBURSTRW;
		break;
	case FMC2_SYNC_READ_ASYNC_WRITE_NOR:
		/*
		 * MUXEN = 0, MTYP = 2, FACCEN = 1, BURSTEN = 1, WAITEN = 0,
		 * WREN = 1, EXTMOD = 0, CBURSTRW = 0, ACCMOD = 0
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_NOR);
		bcr |= FMC2_BCR_FACCEN | FMC2_BCR_BURSTEN;
		break;
	default:
		/* Type of transaction not supported */
		return -EINVAL;
	}

	if (bcr & FMC2_BCR_EXTMOD)
		regmap_update_bits(ebi->regmap, FMC2_BWTR(cs),
				   bwtr_mask, bwtr);
	regmap_update_bits(ebi->regmap, FMC2_BTR(cs), btr_mask, btr);
	regmap_update_bits(ebi->regmap, FMC2_BCR(cs), bcr_mask, bcr);

	return 0;
}

static int stm32_fmc2_ebi_set_buswidth(struct stm32_fmc2_ebi *ebi,
				       const struct stm32_fmc2_prop *prop,
				       int cs, u32 setup)
{
	u32 val;

	switch (setup) {
	case FMC2_BUSWIDTH_8:
		val = FIELD_PREP(FMC2_BCR_MWID, FMC2_BCR_MWID_8);
		break;
	case FMC2_BUSWIDTH_16:
		val = FIELD_PREP(FMC2_BCR_MWID, FMC2_BCR_MWID_16);
		break;
	default:
		/* Buswidth not supported */
		return -EINVAL;
	}

	regmap_update_bits(ebi->regmap, FMC2_BCR(cs), FMC2_BCR_MWID, val);

	return 0;
}

static int stm32_fmc2_ebi_set_cpsize(struct stm32_fmc2_ebi *ebi,
				     const struct stm32_fmc2_prop *prop,
				     int cs, u32 setup)
{
	u32 val;

	switch (setup) {
	case FMC2_CPSIZE_0:
		val = FIELD_PREP(FMC2_BCR_CPSIZE, FMC2_BCR_CPSIZE_0);
		break;
	case FMC2_CPSIZE_128:
		val = FIELD_PREP(FMC2_BCR_CPSIZE, FMC2_BCR_CPSIZE_128);
		break;
	case FMC2_CPSIZE_256:
		val = FIELD_PREP(FMC2_BCR_CPSIZE, FMC2_BCR_CPSIZE_256);
		break;
	case FMC2_CPSIZE_512:
		val = FIELD_PREP(FMC2_BCR_CPSIZE, FMC2_BCR_CPSIZE_512);
		break;
	case FMC2_CPSIZE_1024:
		val = FIELD_PREP(FMC2_BCR_CPSIZE, FMC2_BCR_CPSIZE_1024);
		break;
	default:
		/* Cpsize not supported */
		return -EINVAL;
	}

	regmap_update_bits(ebi->regmap, FMC2_BCR(cs), FMC2_BCR_CPSIZE, val);

	return 0;
}

static int stm32_fmc2_ebi_set_bl_setup(struct stm32_fmc2_ebi *ebi,
				       const struct stm32_fmc2_prop *prop,
				       int cs, u32 setup)
{
	u32 val;

	val = min_t(u32, setup, FMC2_BCR_NBLSET_MAX);
	val = FIELD_PREP(FMC2_BCR_NBLSET, val);
	regmap_update_bits(ebi->regmap, FMC2_BCR(cs), FMC2_BCR_NBLSET, val);

	return 0;
}

static int stm32_fmc2_ebi_set_address_setup(struct stm32_fmc2_ebi *ebi,
					    const struct stm32_fmc2_prop *prop,
					    int cs, u32 setup)
{
	u32 bcr, bxtr, reg;
	u32 val = FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_D);
	int ret;

	ret = stm32_fmc2_ebi_get_reg(prop->reg_type, cs, &reg);
	if (ret)
		return ret;

	ret = regmap_read(ebi->regmap, FMC2_BCR(cs), &bcr);
	if (ret)
		return ret;

	if (prop->reg_type == FMC2_REG_BWTR)
		ret = regmap_read(ebi->regmap, FMC2_BWTR(cs), &bxtr);
	else
		ret = regmap_read(ebi->regmap, FMC2_BTR(cs), &bxtr);
	if (ret)
		return ret;

	if ((bxtr & FMC2_BXTR_ACCMOD) == val || bcr & FMC2_BCR_MUXEN)
		val = clamp_val(setup, 1, FMC2_BXTR_ADDSET_MAX);
	else
		val = min_t(u32, setup, FMC2_BXTR_ADDSET_MAX);
	val = FIELD_PREP(FMC2_BXTR_ADDSET, val);
	regmap_update_bits(ebi->regmap, reg, FMC2_BXTR_ADDSET, val);

	return 0;
}

static int stm32_fmc2_ebi_set_address_hold(struct stm32_fmc2_ebi *ebi,
					   const struct stm32_fmc2_prop *prop,
					   int cs, u32 setup)
{
	u32 val, reg;
	int ret;

	ret = stm32_fmc2_ebi_get_reg(prop->reg_type, cs, &reg);
	if (ret)
		return ret;

	val = clamp_val(setup, 1, FMC2_BXTR_ADDHLD_MAX);
	val = FIELD_PREP(FMC2_BXTR_ADDHLD, val);
	regmap_update_bits(ebi->regmap, reg, FMC2_BXTR_ADDHLD, val);

	return 0;
}

static int stm32_fmc2_ebi_set_data_setup(struct stm32_fmc2_ebi *ebi,
					 const struct stm32_fmc2_prop *prop,
					 int cs, u32 setup)
{
	u32 val, reg;
	int ret;

	ret = stm32_fmc2_ebi_get_reg(prop->reg_type, cs, &reg);
	if (ret)
		return ret;

	val = clamp_val(setup, 1, FMC2_BXTR_DATAST_MAX);
	val = FIELD_PREP(FMC2_BXTR_DATAST, val);
	regmap_update_bits(ebi->regmap, reg, FMC2_BXTR_DATAST, val);

	return 0;
}

static int stm32_fmc2_ebi_set_bus_turnaround(struct stm32_fmc2_ebi *ebi,
					     const struct stm32_fmc2_prop *prop,
					     int cs, u32 setup)
{
	u32 val, reg;
	int ret;

	ret = stm32_fmc2_ebi_get_reg(prop->reg_type, cs, &reg);
	if (ret)
		return ret;

	val = setup ? min_t(u32, setup - 1, FMC2_BXTR_BUSTURN_MAX) : 0;
	val = FIELD_PREP(FMC2_BXTR_BUSTURN, val);
	regmap_update_bits(ebi->regmap, reg, FMC2_BXTR_BUSTURN, val);

	return 0;
}

static int stm32_fmc2_ebi_set_data_hold(struct stm32_fmc2_ebi *ebi,
					const struct stm32_fmc2_prop *prop,
					int cs, u32 setup)
{
	u32 val, reg;
	int ret;

	ret = stm32_fmc2_ebi_get_reg(prop->reg_type, cs, &reg);
	if (ret)
		return ret;

	if (prop->reg_type == FMC2_REG_BWTR)
		val = setup ? min_t(u32, setup - 1, FMC2_BXTR_DATAHLD_MAX) : 0;
	else
		val = min_t(u32, setup, FMC2_BXTR_DATAHLD_MAX);
	val = FIELD_PREP(FMC2_BXTR_DATAHLD, val);
	regmap_update_bits(ebi->regmap, reg, FMC2_BXTR_DATAHLD, val);

	return 0;
}

static int stm32_fmc2_ebi_set_clk_period(struct stm32_fmc2_ebi *ebi,
					 const struct stm32_fmc2_prop *prop,
					 int cs, u32 setup)
{
	u32 val;

	val = setup ? clamp_val(setup - 1, 1, FMC2_BTR_CLKDIV_MAX) : 1;
	val = FIELD_PREP(FMC2_BTR_CLKDIV, val);
	regmap_update_bits(ebi->regmap, FMC2_BTR(cs), FMC2_BTR_CLKDIV, val);

	return 0;
}

static int stm32_fmc2_ebi_mp25_set_clk_period(struct stm32_fmc2_ebi *ebi,
					      const struct stm32_fmc2_prop *prop,
					      int cs, u32 setup)
{
	u32 val, cfgr;
	int ret;

	ret = regmap_read(ebi->regmap, FMC2_CFGR, &cfgr);
	if (ret)
		return ret;

	if (cfgr & FMC2_CFGR_CCLKEN) {
		val = setup ? clamp_val(setup - 1, 1, FMC2_CFGR_CLKDIV_MAX) : 1;
		val = FIELD_PREP(FMC2_CFGR_CLKDIV, val);
		regmap_update_bits(ebi->regmap, FMC2_CFGR, FMC2_CFGR_CLKDIV, val);
	} else {
		val = setup ? clamp_val(setup - 1, 1, FMC2_BTR_CLKDIV_MAX) : 1;
		val = FIELD_PREP(FMC2_BTR_CLKDIV, val);
		regmap_update_bits(ebi->regmap, FMC2_BTR(cs), FMC2_BTR_CLKDIV, val);
	}

	return 0;
}

static int stm32_fmc2_ebi_set_data_latency(struct stm32_fmc2_ebi *ebi,
					   const struct stm32_fmc2_prop *prop,
					   int cs, u32 setup)
{
	u32 val;

	val = setup > 1 ? min_t(u32, setup - 2, FMC2_BTR_DATLAT_MAX) : 0;
	val = FIELD_PREP(FMC2_BTR_DATLAT, val);
	regmap_update_bits(ebi->regmap, FMC2_BTR(cs), FMC2_BTR_DATLAT, val);

	return 0;
}

static int stm32_fmc2_ebi_set_max_low_pulse(struct stm32_fmc2_ebi *ebi,
					    const struct stm32_fmc2_prop *prop,
					    int cs, u32 setup)
{
	u32 old_val, new_val, pcscntr;
	int ret;

	if (setup < 1)
		return 0;

	ret = regmap_read(ebi->regmap, FMC2_PCSCNTR, &pcscntr);
	if (ret)
		return ret;

	/* Enable counter for the bank */
	regmap_update_bits(ebi->regmap, FMC2_PCSCNTR,
			   FMC2_PCSCNTR_CNTBEN(cs),
			   FMC2_PCSCNTR_CNTBEN(cs));

	new_val = min_t(u32, setup - 1, FMC2_PCSCNTR_CSCOUNT_MAX);
	old_val = FIELD_GET(FMC2_PCSCNTR_CSCOUNT, pcscntr);
	if (old_val && new_val > old_val)
		/* Keep current counter value */
		return 0;

	new_val = FIELD_PREP(FMC2_PCSCNTR_CSCOUNT, new_val);
	regmap_update_bits(ebi->regmap, FMC2_PCSCNTR,
			   FMC2_PCSCNTR_CSCOUNT, new_val);

	return 0;
}

static int stm32_fmc2_ebi_mp25_set_max_low_pulse(struct stm32_fmc2_ebi *ebi,
						 const struct stm32_fmc2_prop *prop,
						 int cs, u32 setup)
{
	u32 val;

	if (setup == FMC2_CSCOUNT_0)
		val = FIELD_PREP(FMC2_BCR_CSCOUNT, FMC2_BCR_CSCOUNT_0);
	else if (setup == FMC2_CSCOUNT_1)
		val = FIELD_PREP(FMC2_BCR_CSCOUNT, FMC2_BCR_CSCOUNT_1);
	else if (setup <= FMC2_CSCOUNT_64)
		val = FIELD_PREP(FMC2_BCR_CSCOUNT, FMC2_BCR_CSCOUNT_64);
	else
		val = FIELD_PREP(FMC2_BCR_CSCOUNT, FMC2_BCR_CSCOUNT_256);

	regmap_update_bits(ebi->regmap, FMC2_BCR(cs),
			   FMC2_BCR_CSCOUNT, val);

	return 0;
}

static const struct stm32_fmc2_prop stm32_fmc2_child_props[] = {
	/* st,fmc2-ebi-cs-trans-type must be the first property */
	{
		.name = "st,fmc2-ebi-cs-transaction-type",
		.mprop = true,
		.set = stm32_fmc2_ebi_set_trans_type,
	},
	{
		.name = "st,fmc2-ebi-cs-cclk-enable",
		.bprop = true,
		.reg_type = FMC2_REG_BCR,
		.reg_mask = FMC2_BCR1_CCLKEN,
		.check = stm32_fmc2_ebi_check_cclk,
		.set = stm32_fmc2_ebi_set_bit_field,
	},
	{
		.name = "st,fmc2-ebi-cs-mux-enable",
		.bprop = true,
		.reg_type = FMC2_REG_BCR,
		.reg_mask = FMC2_BCR_MUXEN,
		.check = stm32_fmc2_ebi_check_mux,
		.set = stm32_fmc2_ebi_set_bit_field,
	},
	{
		.name = "st,fmc2-ebi-cs-buswidth",
		.reset_val = FMC2_BUSWIDTH_16,
		.set = stm32_fmc2_ebi_set_buswidth,
	},
	{
		.name = "st,fmc2-ebi-cs-waitpol-high",
		.bprop = true,
		.reg_type = FMC2_REG_BCR,
		.reg_mask = FMC2_BCR_WAITPOL,
		.set = stm32_fmc2_ebi_set_bit_field,
	},
	{
		.name = "st,fmc2-ebi-cs-waitcfg-enable",
		.bprop = true,
		.reg_type = FMC2_REG_BCR,
		.reg_mask = FMC2_BCR_WAITCFG,
		.check = stm32_fmc2_ebi_check_waitcfg,
		.set = stm32_fmc2_ebi_set_bit_field,
	},
	{
		.name = "st,fmc2-ebi-cs-wait-enable",
		.bprop = true,
		.reg_type = FMC2_REG_BCR,
		.reg_mask = FMC2_BCR_WAITEN,
		.check = stm32_fmc2_ebi_check_sync_trans,
		.set = stm32_fmc2_ebi_set_bit_field,
	},
	{
		.name = "st,fmc2-ebi-cs-asyncwait-enable",
		.bprop = true,
		.reg_type = FMC2_REG_BCR,
		.reg_mask = FMC2_BCR_ASYNCWAIT,
		.check = stm32_fmc2_ebi_check_async_trans,
		.set = stm32_fmc2_ebi_set_bit_field,
	},
	{
		.name = "st,fmc2-ebi-cs-cpsize",
		.check = stm32_fmc2_ebi_check_cpsize,
		.set = stm32_fmc2_ebi_set_cpsize,
	},
	{
		.name = "st,fmc2-ebi-cs-byte-lane-setup-ns",
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_bl_setup,
	},
	{
		.name = "st,fmc2-ebi-cs-address-setup-ns",
		.reg_type = FMC2_REG_BTR,
		.reset_val = FMC2_BXTR_ADDSET_MAX,
		.check = stm32_fmc2_ebi_check_async_trans,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_address_setup,
	},
	{
		.name = "st,fmc2-ebi-cs-address-hold-ns",
		.reg_type = FMC2_REG_BTR,
		.reset_val = FMC2_BXTR_ADDHLD_MAX,
		.check = stm32_fmc2_ebi_check_address_hold,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_address_hold,
	},
	{
		.name = "st,fmc2-ebi-cs-data-setup-ns",
		.reg_type = FMC2_REG_BTR,
		.reset_val = FMC2_BXTR_DATAST_MAX,
		.check = stm32_fmc2_ebi_check_async_trans,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_data_setup,
	},
	{
		.name = "st,fmc2-ebi-cs-bus-turnaround-ns",
		.reg_type = FMC2_REG_BTR,
		.reset_val = FMC2_BXTR_BUSTURN_MAX + 1,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_bus_turnaround,
	},
	{
		.name = "st,fmc2-ebi-cs-data-hold-ns",
		.reg_type = FMC2_REG_BTR,
		.check = stm32_fmc2_ebi_check_async_trans,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_data_hold,
	},
	{
		.name = "st,fmc2-ebi-cs-clk-period-ns",
		.reset_val = FMC2_BTR_CLKDIV_MAX + 1,
		.check = stm32_fmc2_ebi_check_clk_period,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_clk_period,
	},
	{
		.name = "st,fmc2-ebi-cs-data-latency-ns",
		.check = stm32_fmc2_ebi_check_sync_trans,
		.calculate = stm32_fmc2_ebi_ns_to_clk_period,
		.set = stm32_fmc2_ebi_set_data_latency,
	},
	{
		.name = "st,fmc2-ebi-cs-write-address-setup-ns",
		.reg_type = FMC2_REG_BWTR,
		.reset_val = FMC2_BXTR_ADDSET_MAX,
		.check = stm32_fmc2_ebi_check_async_trans,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_address_setup,
	},
	{
		.name = "st,fmc2-ebi-cs-write-address-hold-ns",
		.reg_type = FMC2_REG_BWTR,
		.reset_val = FMC2_BXTR_ADDHLD_MAX,
		.check = stm32_fmc2_ebi_check_address_hold,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_address_hold,
	},
	{
		.name = "st,fmc2-ebi-cs-write-data-setup-ns",
		.reg_type = FMC2_REG_BWTR,
		.reset_val = FMC2_BXTR_DATAST_MAX,
		.check = stm32_fmc2_ebi_check_async_trans,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_data_setup,
	},
	{
		.name = "st,fmc2-ebi-cs-write-bus-turnaround-ns",
		.reg_type = FMC2_REG_BWTR,
		.reset_val = FMC2_BXTR_BUSTURN_MAX + 1,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_bus_turnaround,
	},
	{
		.name = "st,fmc2-ebi-cs-write-data-hold-ns",
		.reg_type = FMC2_REG_BWTR,
		.check = stm32_fmc2_ebi_check_async_trans,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_data_hold,
	},
	{
		.name = "st,fmc2-ebi-cs-max-low-pulse-ns",
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_max_low_pulse,
	},
};

static const struct stm32_fmc2_prop stm32_fmc2_mp25_child_props[] = {
	/* st,fmc2-ebi-cs-trans-type must be the first property */
	{
		.name = "st,fmc2-ebi-cs-transaction-type",
		.mprop = true,
		.set = stm32_fmc2_ebi_set_trans_type,
	},
	{
		.name = "st,fmc2-ebi-cs-cclk-enable",
		.bprop = true,
		.reg_type = FMC2_REG_CFGR,
		.reg_mask = FMC2_CFGR_CCLKEN,
		.check = stm32_fmc2_ebi_mp25_check_cclk,
		.set = stm32_fmc2_ebi_set_bit_field,
	},
	{
		.name = "st,fmc2-ebi-cs-mux-enable",
		.bprop = true,
		.reg_type = FMC2_REG_BCR,
		.reg_mask = FMC2_BCR_MUXEN,
		.check = stm32_fmc2_ebi_check_mux,
		.set = stm32_fmc2_ebi_set_bit_field,
	},
	{
		.name = "st,fmc2-ebi-cs-buswidth",
		.reset_val = FMC2_BUSWIDTH_16,
		.set = stm32_fmc2_ebi_set_buswidth,
	},
	{
		.name = "st,fmc2-ebi-cs-waitpol-high",
		.bprop = true,
		.reg_type = FMC2_REG_BCR,
		.reg_mask = FMC2_BCR_WAITPOL,
		.set = stm32_fmc2_ebi_set_bit_field,
	},
	{
		.name = "st,fmc2-ebi-cs-waitcfg-enable",
		.bprop = true,
		.reg_type = FMC2_REG_BCR,
		.reg_mask = FMC2_BCR_WAITCFG,
		.check = stm32_fmc2_ebi_check_waitcfg,
		.set = stm32_fmc2_ebi_set_bit_field,
	},
	{
		.name = "st,fmc2-ebi-cs-wait-enable",
		.bprop = true,
		.reg_type = FMC2_REG_BCR,
		.reg_mask = FMC2_BCR_WAITEN,
		.check = stm32_fmc2_ebi_check_sync_trans,
		.set = stm32_fmc2_ebi_set_bit_field,
	},
	{
		.name = "st,fmc2-ebi-cs-asyncwait-enable",
		.bprop = true,
		.reg_type = FMC2_REG_BCR,
		.reg_mask = FMC2_BCR_ASYNCWAIT,
		.check = stm32_fmc2_ebi_check_async_trans,
		.set = stm32_fmc2_ebi_set_bit_field,
	},
	{
		.name = "st,fmc2-ebi-cs-cpsize",
		.check = stm32_fmc2_ebi_check_cpsize,
		.set = stm32_fmc2_ebi_set_cpsize,
	},
	{
		.name = "st,fmc2-ebi-cs-byte-lane-setup-ns",
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_bl_setup,
	},
	{
		.name = "st,fmc2-ebi-cs-address-setup-ns",
		.reg_type = FMC2_REG_BTR,
		.reset_val = FMC2_BXTR_ADDSET_MAX,
		.check = stm32_fmc2_ebi_check_async_trans,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_address_setup,
	},
	{
		.name = "st,fmc2-ebi-cs-address-hold-ns",
		.reg_type = FMC2_REG_BTR,
		.reset_val = FMC2_BXTR_ADDHLD_MAX,
		.check = stm32_fmc2_ebi_check_address_hold,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_address_hold,
	},
	{
		.name = "st,fmc2-ebi-cs-data-setup-ns",
		.reg_type = FMC2_REG_BTR,
		.reset_val = FMC2_BXTR_DATAST_MAX,
		.check = stm32_fmc2_ebi_check_async_trans,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_data_setup,
	},
	{
		.name = "st,fmc2-ebi-cs-bus-turnaround-ns",
		.reg_type = FMC2_REG_BTR,
		.reset_val = FMC2_BXTR_BUSTURN_MAX + 1,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_bus_turnaround,
	},
	{
		.name = "st,fmc2-ebi-cs-data-hold-ns",
		.reg_type = FMC2_REG_BTR,
		.check = stm32_fmc2_ebi_check_async_trans,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_data_hold,
	},
	{
		.name = "st,fmc2-ebi-cs-clk-period-ns",
		.reset_val = FMC2_CFGR_CLKDIV_MAX + 1,
		.check = stm32_fmc2_ebi_mp25_check_clk_period,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_mp25_set_clk_period,
	},
	{
		.name = "st,fmc2-ebi-cs-data-latency-ns",
		.check = stm32_fmc2_ebi_check_sync_trans,
		.calculate = stm32_fmc2_ebi_mp25_ns_to_clk_period,
		.set = stm32_fmc2_ebi_set_data_latency,
	},
	{
		.name = "st,fmc2-ebi-cs-write-address-setup-ns",
		.reg_type = FMC2_REG_BWTR,
		.reset_val = FMC2_BXTR_ADDSET_MAX,
		.check = stm32_fmc2_ebi_check_async_trans,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_address_setup,
	},
	{
		.name = "st,fmc2-ebi-cs-write-address-hold-ns",
		.reg_type = FMC2_REG_BWTR,
		.reset_val = FMC2_BXTR_ADDHLD_MAX,
		.check = stm32_fmc2_ebi_check_address_hold,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_address_hold,
	},
	{
		.name = "st,fmc2-ebi-cs-write-data-setup-ns",
		.reg_type = FMC2_REG_BWTR,
		.reset_val = FMC2_BXTR_DATAST_MAX,
		.check = stm32_fmc2_ebi_check_async_trans,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_data_setup,
	},
	{
		.name = "st,fmc2-ebi-cs-write-bus-turnaround-ns",
		.reg_type = FMC2_REG_BWTR,
		.reset_val = FMC2_BXTR_BUSTURN_MAX + 1,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_bus_turnaround,
	},
	{
		.name = "st,fmc2-ebi-cs-write-data-hold-ns",
		.reg_type = FMC2_REG_BWTR,
		.check = stm32_fmc2_ebi_check_async_trans,
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_data_hold,
	},
	{
		.name = "st,fmc2-ebi-cs-max-low-pulse-ns",
		.calculate = stm32_fmc2_ebi_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_mp25_set_max_low_pulse,
	},
};

static int stm32_fmc2_ebi_mp25_check_rif(struct stm32_fmc2_ebi *ebi, u32 resource)
{
	u32 seccfgr, cidcfgr, semcr;
	int cid, ret;

	if (resource >= FMC2_MAX_RESOURCES)
		return -EINVAL;

	ret = regmap_read(ebi->regmap, FMC2_SECCFGR, &seccfgr);
	if (ret)
		return ret;

	if (seccfgr & BIT(resource)) {
		if (resource)
			dev_err(ebi->dev, "resource %d is configured as secure\n",
				resource);

		return -EACCES;
	}

	ret = regmap_read(ebi->regmap, FMC2_CIDCFGR(resource), &cidcfgr);
	if (ret)
		return ret;

	if (!(cidcfgr & FMC2_CIDCFGR_CFEN))
		/* CID filtering is turned off: access granted */
		return 0;

	if (!(cidcfgr & FMC2_CIDCFGR_SEMEN)) {
		/* Static CID mode */
		cid = FIELD_GET(FMC2_CIDCFGR_SCID, cidcfgr);
		if (cid != FMC2_CID1) {
			if (resource)
				dev_err(ebi->dev, "static CID%d set for resource %d\n",
					cid, resource);

			return -EACCES;
		}

		return 0;
	}

	/* Pass-list with semaphore mode */
	if (!(cidcfgr & FMC2_CIDCFGR_SEMWLC1)) {
		if (resource)
			dev_err(ebi->dev, "CID1 is block-listed for resource %d\n",
				resource);

		return -EACCES;
	}

	ret = regmap_read(ebi->regmap, FMC2_SEMCR(resource), &semcr);
	if (ret)
		return ret;

	if (!(semcr & FMC2_SEMCR_SEM_MUTEX)) {
		regmap_update_bits(ebi->regmap, FMC2_SEMCR(resource),
				   FMC2_SEMCR_SEM_MUTEX, FMC2_SEMCR_SEM_MUTEX);

		ret = regmap_read(ebi->regmap, FMC2_SEMCR(resource), &semcr);
		if (ret)
			return ret;
	}

	cid = FIELD_GET(FMC2_SEMCR_SEMCID, semcr);
	if (cid != FMC2_CID1) {
		if (resource)
			dev_err(ebi->dev, "resource %d is already used by CID%d\n",
				resource, cid);

		return -EACCES;
	}

	ebi->sem_taken |= BIT(resource);

	return 0;
}

static void stm32_fmc2_ebi_mp25_put_sems(struct stm32_fmc2_ebi *ebi)
{
	unsigned int resource;

	for (resource = 0; resource < FMC2_MAX_RESOURCES; resource++) {
		if (!(ebi->sem_taken & BIT(resource)))
			continue;

		regmap_update_bits(ebi->regmap, FMC2_SEMCR(resource),
				   FMC2_SEMCR_SEM_MUTEX, 0);
	}
}

static void stm32_fmc2_ebi_mp25_get_sems(struct stm32_fmc2_ebi *ebi)
{
	unsigned int resource;

	for (resource = 0; resource < FMC2_MAX_RESOURCES; resource++) {
		if (!(ebi->sem_taken & BIT(resource)))
			continue;

		regmap_update_bits(ebi->regmap, FMC2_SEMCR(resource),
				   FMC2_SEMCR_SEM_MUTEX, FMC2_SEMCR_SEM_MUTEX);
	}
}

static int stm32_fmc2_ebi_parse_prop(struct stm32_fmc2_ebi *ebi,
				     struct device_node *dev_node,
				     const struct stm32_fmc2_prop *prop,
				     int cs)
{
	struct device *dev = ebi->dev;
	u32 setup = 0;

	if (!prop->set) {
		dev_err(dev, "property %s is not well defined\n", prop->name);
		return -EINVAL;
	}

	if (prop->check && prop->check(ebi, prop, cs))
		/* Skeep this property */
		return 0;

	if (prop->bprop) {
		bool bprop;

		bprop = of_property_read_bool(dev_node, prop->name);
		if (prop->mprop && !bprop) {
			dev_err(dev, "mandatory property %s not defined in the device tree\n",
				prop->name);
			return -EINVAL;
		}

		if (bprop)
			setup = 1;
	} else {
		u32 val;
		int ret;

		ret = of_property_read_u32(dev_node, prop->name, &val);
		if (prop->mprop && ret) {
			dev_err(dev, "mandatory property %s not defined in the device tree\n",
				prop->name);
			return ret;
		}

		if (ret)
			setup = prop->reset_val;
		else if (prop->calculate)
			setup = prop->calculate(ebi, cs, val);
		else
			setup = val;
	}

	return prop->set(ebi, prop, cs, setup);
}

static void stm32_fmc2_ebi_enable_bank(struct stm32_fmc2_ebi *ebi, int cs)
{
	regmap_update_bits(ebi->regmap, FMC2_BCR(cs),
			   FMC2_BCR_MBKEN, FMC2_BCR_MBKEN);
}

static void stm32_fmc2_ebi_disable_bank(struct stm32_fmc2_ebi *ebi, int cs)
{
	regmap_update_bits(ebi->regmap, FMC2_BCR(cs), FMC2_BCR_MBKEN, 0);
}

static int stm32_fmc2_ebi_save_setup(struct stm32_fmc2_ebi *ebi)
{
	unsigned int cs;
	int ret;

	for (cs = 0; cs < FMC2_MAX_EBI_CE; cs++) {
		if (!(ebi->bank_assigned & BIT(cs)))
			continue;

		ret = regmap_read(ebi->regmap, FMC2_BCR(cs), &ebi->bcr[cs]);
		ret |= regmap_read(ebi->regmap, FMC2_BTR(cs), &ebi->btr[cs]);
		ret |= regmap_read(ebi->regmap, FMC2_BWTR(cs), &ebi->bwtr[cs]);
		if (ret)
			return ret;
	}

	return 0;
}

static int stm32_fmc2_ebi_mp1_save_setup(struct stm32_fmc2_ebi *ebi)
{
	int ret;

	ret = stm32_fmc2_ebi_save_setup(ebi);
	if (ret)
		return ret;

	return regmap_read(ebi->regmap, FMC2_PCSCNTR, &ebi->pcscntr);
}

static int stm32_fmc2_ebi_mp25_save_setup(struct stm32_fmc2_ebi *ebi)
{
	int ret;

	ret = stm32_fmc2_ebi_save_setup(ebi);
	if (ret)
		return ret;

	if (ebi->access_granted)
		ret = regmap_read(ebi->regmap, FMC2_CFGR, &ebi->cfgr);

	return ret;
}

static void stm32_fmc2_ebi_set_setup(struct stm32_fmc2_ebi *ebi)
{
	unsigned int cs;

	for (cs = 0; cs < FMC2_MAX_EBI_CE; cs++) {
		if (!(ebi->bank_assigned & BIT(cs)))
			continue;

		regmap_write(ebi->regmap, FMC2_BCR(cs), ebi->bcr[cs]);
		regmap_write(ebi->regmap, FMC2_BTR(cs), ebi->btr[cs]);
		regmap_write(ebi->regmap, FMC2_BWTR(cs), ebi->bwtr[cs]);
	}
}

static void stm32_fmc2_ebi_mp1_set_setup(struct stm32_fmc2_ebi *ebi)
{
	stm32_fmc2_ebi_set_setup(ebi);
	regmap_write(ebi->regmap, FMC2_PCSCNTR, ebi->pcscntr);
}

static void stm32_fmc2_ebi_mp25_set_setup(struct stm32_fmc2_ebi *ebi)
{
	stm32_fmc2_ebi_set_setup(ebi);

	if (ebi->access_granted)
		regmap_write(ebi->regmap, FMC2_CFGR, ebi->cfgr);
}

static void stm32_fmc2_ebi_disable_banks(struct stm32_fmc2_ebi *ebi)
{
	unsigned int cs;

	for (cs = 0; cs < FMC2_MAX_EBI_CE; cs++) {
		if (!(ebi->bank_assigned & BIT(cs)))
			continue;

		stm32_fmc2_ebi_disable_bank(ebi, cs);
	}
}

/* NWAIT signal can not be connected to EBI controller and NAND controller */
static int stm32_fmc2_ebi_nwait_used_by_ctrls(struct stm32_fmc2_ebi *ebi)
{
	struct device *dev = ebi->dev;
	unsigned int cs;
	u32 bcr;
	int ret;

	for (cs = 0; cs < FMC2_MAX_EBI_CE; cs++) {
		if (!(ebi->bank_assigned & BIT(cs)))
			continue;

		ret = regmap_read(ebi->regmap, FMC2_BCR(cs), &bcr);
		if (ret)
			return ret;

		if ((bcr & FMC2_BCR_WAITEN || bcr & FMC2_BCR_ASYNCWAIT) &&
		    ebi->bank_assigned & BIT(FMC2_NAND)) {
			dev_err(dev, "NWAIT signal connected to EBI and NAND controllers\n");
			return -EINVAL;
		}
	}

	return 0;
}

static void stm32_fmc2_ebi_enable(struct stm32_fmc2_ebi *ebi)
{
	if (!ebi->access_granted)
		return;

	regmap_update_bits(ebi->regmap, ebi->data->fmc2_enable_reg,
			   ebi->data->fmc2_enable_bit,
			   ebi->data->fmc2_enable_bit);
}

static void stm32_fmc2_ebi_disable(struct stm32_fmc2_ebi *ebi)
{
	if (!ebi->access_granted)
		return;

	regmap_update_bits(ebi->regmap, ebi->data->fmc2_enable_reg,
			   ebi->data->fmc2_enable_bit, 0);
}

static int stm32_fmc2_ebi_setup_cs(struct stm32_fmc2_ebi *ebi,
				   struct device_node *dev_node,
				   u32 cs)
{
	unsigned int i;
	int ret;

	stm32_fmc2_ebi_disable_bank(ebi, cs);

	for (i = 0; i < ebi->data->nb_child_props; i++) {
		const struct stm32_fmc2_prop *p = &ebi->data->child_props[i];

		ret = stm32_fmc2_ebi_parse_prop(ebi, dev_node, p, cs);
		if (ret) {
			dev_err(ebi->dev, "property %s could not be set: %d\n",
				p->name, ret);
			return ret;
		}
	}

	stm32_fmc2_ebi_enable_bank(ebi, cs);

	return 0;
}

static int stm32_fmc2_ebi_parse_dt(struct stm32_fmc2_ebi *ebi)
{
	struct device *dev = ebi->dev;
	bool child_found = false;
	u32 bank;
	int ret;

	for_each_available_child_of_node_scoped(dev->of_node, child) {
		ret = of_property_read_u32(child, "reg", &bank);
		if (ret)
			return dev_err_probe(dev, ret, "could not retrieve reg property\n");

		if (bank >= FMC2_MAX_BANKS) {
			dev_err(dev, "invalid reg value: %d\n", bank);
			return -EINVAL;
		}

		if (ebi->bank_assigned & BIT(bank)) {
			dev_err(dev, "bank already assigned: %d\n", bank);
			return -EINVAL;
		}

		if (ebi->data->check_rif) {
			ret = ebi->data->check_rif(ebi, bank + 1);
			if (ret) {
				dev_err(dev, "bank access failed: %d\n", bank);
				return ret;
			}
		}

		if (bank < FMC2_MAX_EBI_CE) {
			ret = stm32_fmc2_ebi_setup_cs(ebi, child, bank);
			if (ret)
				return dev_err_probe(dev, ret,
						     "setup chip select %d failed\n", bank);
		}

		ebi->bank_assigned |= BIT(bank);
		child_found = true;
	}

	if (!child_found) {
		dev_warn(dev, "no subnodes found, disable the driver.\n");
		return -ENODEV;
	}

	if (ebi->data->nwait_used_by_ctrls) {
		ret = ebi->data->nwait_used_by_ctrls(ebi);
		if (ret)
			return ret;
	}

	stm32_fmc2_ebi_enable(ebi);

	return of_platform_populate(dev->of_node, NULL, NULL, dev);
}

static int stm32_fmc2_ebi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct stm32_fmc2_ebi *ebi;
	struct reset_control *rstc;
	int ret;

	ebi = devm_kzalloc(&pdev->dev, sizeof(*ebi), GFP_KERNEL);
	if (!ebi)
		return -ENOMEM;

	ebi->dev = dev;
	platform_set_drvdata(pdev, ebi);

	ebi->data = of_device_get_match_data(dev);
	if (!ebi->data)
		return -EINVAL;

	ebi->regmap = device_node_to_regmap(dev->of_node);
	if (IS_ERR(ebi->regmap))
		return PTR_ERR(ebi->regmap);

	ebi->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ebi->clk))
		return PTR_ERR(ebi->clk);

	rstc = devm_reset_control_get(dev, NULL);
	if (PTR_ERR(rstc) == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	if (!IS_ERR(rstc)) {
		reset_control_assert(rstc);
		reset_control_deassert(rstc);
	}

	/* Check if CFGR register can be modified */
	ebi->access_granted = true;
	if (ebi->data->check_rif) {
		ret = ebi->data->check_rif(ebi, 0);
		if (ret) {
			u32 sr;

			ebi->access_granted = false;

			ret = regmap_read(ebi->regmap, FMC2_SR, &sr);
			if (ret)
				goto err_release;

			/* In case of CFGR is secure, just check that the FMC2 is enabled */
			if (sr & FMC2_SR_ISOST) {
				dev_err(dev, "FMC2 is not ready to be used.\n");
				ret = -EACCES;
				goto err_release;
			}
		}
	}

	ret = stm32_fmc2_ebi_parse_dt(ebi);
	if (ret)
		goto err_release;

	ret = ebi->data->save_setup(ebi);
	if (ret)
		goto err_release;

	return 0;

err_release:
	stm32_fmc2_ebi_disable_banks(ebi);
	stm32_fmc2_ebi_disable(ebi);
	if (ebi->data->put_sems)
		ebi->data->put_sems(ebi);
	pm_runtime_put_sync_suspend(dev);

	return ret;
}

static void stm32_fmc2_ebi_remove(struct platform_device *pdev)
{
	struct stm32_fmc2_ebi *ebi = platform_get_drvdata(pdev);

	of_platform_depopulate(&pdev->dev);
	stm32_fmc2_ebi_disable_banks(ebi);
	stm32_fmc2_ebi_disable(ebi);
	if (ebi->data->put_sems)
		ebi->data->put_sems(ebi);
	pm_runtime_put_sync_suspend(&pdev->dev);
}

static int __maybe_unused stm32_fmc2_ebi_runtime_suspend(struct device *dev)
{
	struct stm32_fmc2_ebi *ebi = dev_get_drvdata(dev);

	clk_disable_unprepare(ebi->clk);

	return 0;
}

static int __maybe_unused stm32_fmc2_ebi_runtime_resume(struct device *dev)
{
	struct stm32_fmc2_ebi *ebi = dev_get_drvdata(dev);

	return clk_prepare_enable(ebi->clk);
}

static int __maybe_unused stm32_fmc2_ebi_suspend(struct device *dev)
{
	struct stm32_fmc2_ebi *ebi = dev_get_drvdata(dev);

	stm32_fmc2_ebi_disable(ebi);
	if (ebi->data->put_sems)
		ebi->data->put_sems(ebi);
	pm_runtime_put_sync_suspend(dev);
	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int __maybe_unused stm32_fmc2_ebi_resume(struct device *dev)
{
	struct stm32_fmc2_ebi *ebi = dev_get_drvdata(dev);
	int ret;

	pinctrl_pm_select_default_state(dev);

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	if (ebi->data->get_sems)
		ebi->data->get_sems(ebi);
	ebi->data->set_setup(ebi);
	stm32_fmc2_ebi_enable(ebi);

	return 0;
}

static const struct dev_pm_ops stm32_fmc2_ebi_pm_ops = {
	SET_RUNTIME_PM_OPS(stm32_fmc2_ebi_runtime_suspend,
			   stm32_fmc2_ebi_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(stm32_fmc2_ebi_suspend, stm32_fmc2_ebi_resume)
};

static const struct stm32_fmc2_ebi_data stm32_fmc2_ebi_mp1_data = {
	.child_props = stm32_fmc2_child_props,
	.nb_child_props = ARRAY_SIZE(stm32_fmc2_child_props),
	.fmc2_enable_reg = FMC2_BCR1,
	.fmc2_enable_bit = FMC2_BCR1_FMC2EN,
	.nwait_used_by_ctrls = stm32_fmc2_ebi_nwait_used_by_ctrls,
	.set_setup = stm32_fmc2_ebi_mp1_set_setup,
	.save_setup = stm32_fmc2_ebi_mp1_save_setup,
};

static const struct stm32_fmc2_ebi_data stm32_fmc2_ebi_mp25_data = {
	.child_props = stm32_fmc2_mp25_child_props,
	.nb_child_props = ARRAY_SIZE(stm32_fmc2_mp25_child_props),
	.fmc2_enable_reg = FMC2_CFGR,
	.fmc2_enable_bit = FMC2_CFGR_FMC2EN,
	.set_setup = stm32_fmc2_ebi_mp25_set_setup,
	.save_setup = stm32_fmc2_ebi_mp25_save_setup,
	.check_rif = stm32_fmc2_ebi_mp25_check_rif,
	.put_sems = stm32_fmc2_ebi_mp25_put_sems,
	.get_sems = stm32_fmc2_ebi_mp25_get_sems,
};

static const struct of_device_id stm32_fmc2_ebi_match[] = {
	{
		.compatible = "st,stm32mp1-fmc2-ebi",
		.data = &stm32_fmc2_ebi_mp1_data,
	},
	{
		.compatible = "st,stm32mp25-fmc2-ebi",
		.data = &stm32_fmc2_ebi_mp25_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, stm32_fmc2_ebi_match);

static struct platform_driver stm32_fmc2_ebi_driver = {
	.probe	= stm32_fmc2_ebi_probe,
	.remove = stm32_fmc2_ebi_remove,
	.driver	= {
		.name = "stm32_fmc2_ebi",
		.of_match_table = stm32_fmc2_ebi_match,
		.pm = &stm32_fmc2_ebi_pm_ops,
	},
};
module_platform_driver(stm32_fmc2_ebi_driver);

MODULE_ALIAS("platform:stm32_fmc2_ebi");
MODULE_AUTHOR("Christophe Kerello <christophe.kerello@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 FMC2 ebi driver");
MODULE_LICENSE("GPL v2");
