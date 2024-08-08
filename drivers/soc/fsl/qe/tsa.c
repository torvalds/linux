// SPDX-License-Identifier: GPL-2.0
/*
 * TSA driver
 *
 * Copyright 2022 CS GROUP France
 *
 * Author: Herve Codina <herve.codina@bootlin.com>
 */

#include "tsa.h"
#include <dt-bindings/soc/cpm1-fsl,tsa.h>
#include <dt-bindings/soc/qe-fsl,tsa.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <soc/fsl/qe/ucc.h>

/* TSA SI RAM routing tables entry (CPM1) */
#define TSA_CPM1_SIRAM_ENTRY_LAST	BIT(16)
#define TSA_CPM1_SIRAM_ENTRY_BYTE	BIT(17)
#define TSA_CPM1_SIRAM_ENTRY_CNT_MASK	GENMASK(21, 18)
#define TSA_CPM1_SIRAM_ENTRY_CNT(x)	FIELD_PREP(TSA_CPM1_SIRAM_ENTRY_CNT_MASK, x)
#define TSA_CPM1_SIRAM_ENTRY_CSEL_MASK	GENMASK(24, 22)
#define TSA_CPM1_SIRAM_ENTRY_CSEL_NU	FIELD_PREP_CONST(TSA_CPM1_SIRAM_ENTRY_CSEL_MASK, 0x0)
#define TSA_CPM1_SIRAM_ENTRY_CSEL_SCC2	FIELD_PREP_CONST(TSA_CPM1_SIRAM_ENTRY_CSEL_MASK, 0x2)
#define TSA_CPM1_SIRAM_ENTRY_CSEL_SCC3	FIELD_PREP_CONST(TSA_CPM1_SIRAM_ENTRY_CSEL_MASK, 0x3)
#define TSA_CPM1_SIRAM_ENTRY_CSEL_SCC4	FIELD_PREP_CONST(TSA_CPM1_SIRAM_ENTRY_CSEL_MASK, 0x4)
#define TSA_CPM1_SIRAM_ENTRY_CSEL_SMC1	FIELD_PREP_CONST(TSA_CPM1_SIRAM_ENTRY_CSEL_MASK, 0x5)
#define TSA_CPM1_SIRAM_ENTRY_CSEL_SMC2	FIELD_PREP_CONST(TSA_CPM1_SIRAM_ENTRY_CSEL_MASK, 0x6)

/* TSA SI RAM routing tables entry (QE) */
#define TSA_QE_SIRAM_ENTRY_LAST		BIT(0)
#define TSA_QE_SIRAM_ENTRY_BYTE		BIT(1)
#define TSA_QE_SIRAM_ENTRY_CNT_MASK	GENMASK(4, 2)
#define TSA_QE_SIRAM_ENTRY_CNT(x)	FIELD_PREP(TSA_QE_SIRAM_ENTRY_CNT_MASK, x)
#define TSA_QE_SIRAM_ENTRY_CSEL_MASK	GENMASK(8, 5)
#define TSA_QE_SIRAM_ENTRY_CSEL_NU	FIELD_PREP_CONST(TSA_QE_SIRAM_ENTRY_CSEL_MASK, 0x0)
#define TSA_QE_SIRAM_ENTRY_CSEL_UCC5	FIELD_PREP_CONST(TSA_QE_SIRAM_ENTRY_CSEL_MASK, 0x1)
#define TSA_QE_SIRAM_ENTRY_CSEL_UCC1	FIELD_PREP_CONST(TSA_QE_SIRAM_ENTRY_CSEL_MASK, 0x9)
#define TSA_QE_SIRAM_ENTRY_CSEL_UCC2	FIELD_PREP_CONST(TSA_QE_SIRAM_ENTRY_CSEL_MASK, 0xa)
#define TSA_QE_SIRAM_ENTRY_CSEL_UCC3	FIELD_PREP_CONST(TSA_QE_SIRAM_ENTRY_CSEL_MASK, 0xb)
#define TSA_QE_SIRAM_ENTRY_CSEL_UCC4	FIELD_PREP_CONST(TSA_QE_SIRAM_ENTRY_CSEL_MASK, 0xc)

/*
 * SI mode register :
 * - CPM1: 32bit register split in 2*16bit (16bit TDM)
 * - QE: 4x16bit registers, one per TDM
 */
#define TSA_CPM1_SIMODE		0x00
#define TSA_QE_SIAMR		0x00
#define TSA_QE_SIBMR		0x02
#define TSA_QE_SICMR		0x04
#define TSA_QE_SIDMR		0x06
#define   TSA_CPM1_SIMODE_SMC2			BIT(31)
#define   TSA_CPM1_SIMODE_SMC1			BIT(15)
#define   TSA_CPM1_SIMODE_TDMA_MASK		GENMASK(11, 0)
#define   TSA_CPM1_SIMODE_TDMA(x)		FIELD_PREP(TSA_CPM1_SIMODE_TDMA_MASK, x)
#define   TSA_CPM1_SIMODE_TDMB_MASK		GENMASK(27, 16)
#define   TSA_CPM1_SIMODE_TDMB(x)		FIELD_PREP(TSA_CPM1_SIMODE_TDMB_MASK, x)
#define     TSA_QE_SIMODE_TDM_SAD_MASK		GENMASK(15, 12)
#define     TSA_QE_SIMODE_TDM_SAD(x)		FIELD_PREP(TSA_QE_SIMODE_TDM_SAD_MASK, x)
#define     TSA_CPM1_SIMODE_TDM_MASK		GENMASK(11, 0)
#define     TSA_SIMODE_TDM_SDM_MASK		GENMASK(11, 10)
#define       TSA_SIMODE_TDM_SDM_NORM		FIELD_PREP_CONST(TSA_SIMODE_TDM_SDM_MASK, 0x0)
#define       TSA_SIMODE_TDM_SDM_ECHO		FIELD_PREP_CONST(TSA_SIMODE_TDM_SDM_MASK, 0x1)
#define       TSA_SIMODE_TDM_SDM_INTL_LOOP	FIELD_PREP_CONST(TSA_SIMODE_TDM_SDM_MASK, 0x2)
#define       TSA_SIMODE_TDM_SDM_LOOP_CTRL	FIELD_PREP_CONST(TSA_SIMODE_TDM_SDM_MASK, 0x3)
#define     TSA_SIMODE_TDM_RFSD_MASK		GENMASK(9, 8)
#define     TSA_SIMODE_TDM_RFSD(x)		FIELD_PREP(TSA_SIMODE_TDM_RFSD_MASK, x)
#define     TSA_SIMODE_TDM_DSC			BIT(7)
#define     TSA_SIMODE_TDM_CRT			BIT(6)
#define     TSA_CPM1_SIMODE_TDM_STZ		BIT(5) /* bit 5: STZ in CPM1 */
#define     TSA_QE_SIMODE_TDM_SL		BIT(5) /* bit 5: SL in QE */
#define     TSA_SIMODE_TDM_CE			BIT(4)
#define     TSA_SIMODE_TDM_FE			BIT(3)
#define     TSA_SIMODE_TDM_GM			BIT(2)
#define     TSA_SIMODE_TDM_TFSD_MASK		GENMASK(1, 0)
#define     TSA_SIMODE_TDM_TFSD(x)		FIELD_PREP(TSA_SIMODE_TDM_TFSD_MASK, x)

/* CPM SI global mode register (8 bits) */
#define TSA_CPM1_SIGMR	0x04
#define TSA_CPM1_SIGMR_ENB			BIT(3)
#define TSA_CPM1_SIGMR_ENA			BIT(2)
#define TSA_CPM1_SIGMR_RDM_MASK			GENMASK(1, 0)
#define   TSA_CPM1_SIGMR_RDM_STATIC_TDMA	FIELD_PREP_CONST(TSA_CPM1_SIGMR_RDM_MASK, 0x0)
#define   TSA_CPM1_SIGMR_RDM_DYN_TDMA		FIELD_PREP_CONST(TSA_CPM1_SIGMR_RDM_MASK, 0x1)
#define   TSA_CPM1_SIGMR_RDM_STATIC_TDMAB	FIELD_PREP_CONST(TSA_CPM1_SIGMR_RDM_MASK, 0x2)
#define   TSA_CPM1_SIGMR_RDM_DYN_TDMAB		FIELD_PREP_CONST(TSA_CPM1_SIGMR_RDM_MASK, 0x3)

/* QE SI global mode register high (8 bits) */
#define TSA_QE_SIGLMRH	0x08
#define TSA_QE_SIGLMRH_END	BIT(3)
#define TSA_QE_SIGLMRH_ENC	BIT(2)
#define TSA_QE_SIGLMRH_ENB	BIT(1)
#define TSA_QE_SIGLMRH_ENA	BIT(0)

/* SI clock route register (32 bits) */
#define TSA_CPM1_SICR	0x0C
#define   TSA_CPM1_SICR_SCC2_MASK		GENMASK(15, 8)
#define   TSA_CPM1_SICR_SCC2(x)			FIELD_PREP(TSA_CPM1_SICR_SCC2_MASK, x)
#define   TSA_CPM1_SICR_SCC3_MASK		GENMASK(23, 16)
#define   TSA_CPM1_SICR_SCC3(x)			FIELD_PREP(TSA_CPM1_SICR_SCC3_MASK, x)
#define   TSA_CPM1_SICR_SCC4_MASK		GENMASK(31, 24)
#define   TSA_CPM1_SICR_SCC4(x)			FIELD_PREP(TSA_CPM1_SICR_SCC4_MASK, x)
#define     TSA_CPM1_SICR_SCC_MASK		GENMASK(7, 0)
#define     TSA_CPM1_SICR_SCC_GRX		BIT(7)
#define     TSA_CPM1_SICR_SCC_SCX_TSA		BIT(6)
#define     TSA_CPM1_SICR_SCC_RXCS_MASK		GENMASK(5, 3)
#define       TSA_CPM1_SICR_SCC_RXCS_BRG1	FIELD_PREP_CONST(TSA_CPM1_SICR_SCC_RXCS_MASK, 0x0)
#define       TSA_CPM1_SICR_SCC_RXCS_BRG2	FIELD_PREP_CONST(TSA_CPM1_SICR_SCC_RXCS_MASK, 0x1)
#define       TSA_CPM1_SICR_SCC_RXCS_BRG3	FIELD_PREP_CONST(TSA_CPM1_SICR_SCC_RXCS_MASK, 0x2)
#define       TSA_CPM1_SICR_SCC_RXCS_BRG4	FIELD_PREP_CONST(TSA_CPM1_SICR_SCC_RXCS_MASK, 0x3)
#define       TSA_CPM1_SICR_SCC_RXCS_CLK15	FIELD_PREP_CONST(TSA_CPM1_SICR_SCC_RXCS_MASK, 0x4)
#define       TSA_CPM1_SICR_SCC_RXCS_CLK26	FIELD_PREP_CONST(TSA_CPM1_SICR_SCC_RXCS_MASK, 0x5)
#define       TSA_CPM1_SICR_SCC_RXCS_CLK37	FIELD_PREP_CONST(TSA_CPM1_SICR_SCC_RXCS_MASK, 0x6)
#define       TSA_CPM1_SICR_SCC_RXCS_CLK48	FIELD_PREP_CONST(TSA_CPM1_SICR_SCC_RXCS_MASK, 0x7)
#define     TSA_CPM1_SICR_SCC_TXCS_MASK		GENMASK(2, 0)
#define       TSA_CPM1_SICR_SCC_TXCS_BRG1	FIELD_PREP_CONST(TSA_CPM1_SICR_SCC_TXCS_MASK, 0x0)
#define       TSA_CPM1_SICR_SCC_TXCS_BRG2	FIELD_PREP_CONST(TSA_CPM1_SICR_SCC_TXCS_MASK, 0x1)
#define       TSA_CPM1_SICR_SCC_TXCS_BRG3	FIELD_PREP_CONST(TSA_CPM1_SICR_SCC_TXCS_MASK, 0x2)
#define       TSA_CPM1_SICR_SCC_TXCS_BRG4	FIELD_PREP_CONST(TSA_CPM1_SICR_SCC_TXCS_MASK, 0x3)
#define       TSA_CPM1_SICR_SCC_TXCS_CLK15	FIELD_PREP_CONST(TSA_CPM1_SICR_SCC_TXCS_MASK, 0x4)
#define       TSA_CPM1_SICR_SCC_TXCS_CLK26	FIELD_PREP_CONST(TSA_CPM1_SICR_SCC_TXCS_MASK, 0x5)
#define       TSA_CPM1_SICR_SCC_TXCS_CLK37	FIELD_PREP_CONST(TSA_CPM1_SICR_SCC_TXCS_MASK, 0x6)
#define       TSA_CPM1_SICR_SCC_TXCS_CLK48	FIELD_PREP_CONST(TSA_CPM1_SICR_SCC_TXCS_MASK, 0x7)

struct tsa_entries_area {
	void __iomem *entries_start;
	void __iomem *entries_next;
	void __iomem *last_entry;
};

struct tsa_tdm {
	bool is_enable;
	struct clk *l1rclk_clk;
	struct clk *l1rsync_clk;
	struct clk *l1tclk_clk;
	struct clk *l1tsync_clk;
	u32 simode_tdm;
};

#define TSA_TDMA	0
#define TSA_TDMB	1
#define TSA_TDMC	2 /* QE implementation only */
#define TSA_TDMD	3 /* QE implementation only */

enum tsa_version {
	TSA_CPM1 = 1, /* Avoid 0 value */
	TSA_QE,
};

struct tsa {
	struct device *dev;
	void __iomem *si_regs;
	void __iomem *si_ram;
	resource_size_t si_ram_sz;
	spinlock_t	lock; /* Lock for read/modify/write sequence */
	enum tsa_version version;
	int tdms; /* TSA_TDMx ORed */
#if IS_ENABLED(CONFIG_QUICC_ENGINE)
	struct tsa_tdm tdm[4]; /* TDMa, TDMb, TDMc and TDMd */
#else
	struct tsa_tdm tdm[2]; /* TDMa and TDMb */
#endif
	/* Same number of serials for CPM1 and QE:
	 * CPM1: NU, 3 SCCs and 2 SMCs
	 * QE: NU and 5 UCCs
	 */
	struct tsa_serial {
		unsigned int id;
		struct tsa_serial_info info;
	} serials[6];
};

static inline struct tsa *tsa_serial_get_tsa(struct tsa_serial *tsa_serial)
{
	/* The serials table is indexed by the serial id */
	return container_of(tsa_serial, struct tsa, serials[tsa_serial->id]);
}

static inline void tsa_write32(void __iomem *addr, u32 val)
{
	iowrite32be(val, addr);
}

static inline void tsa_write16(void __iomem *addr, u16 val)
{
	iowrite16be(val, addr);
}

static inline void tsa_write8(void __iomem *addr, u8 val)
{
	iowrite8(val, addr);
}

static inline u32 tsa_read32(void __iomem *addr)
{
	return ioread32be(addr);
}

static inline u16 tsa_read16(void __iomem *addr)
{
	return ioread16be(addr);
}

static inline void tsa_clrbits32(void __iomem *addr, u32 clr)
{
	tsa_write32(addr, tsa_read32(addr) & ~clr);
}

static inline void tsa_clrbits16(void __iomem *addr, u16 clr)
{
	tsa_write16(addr, tsa_read16(addr) & ~clr);
}

static inline void tsa_clrsetbits32(void __iomem *addr, u32 clr, u32 set)
{
	tsa_write32(addr, (tsa_read32(addr) & ~clr) | set);
}

static bool tsa_is_qe(const struct tsa *tsa)
{
	if (IS_ENABLED(CONFIG_QUICC_ENGINE) && IS_ENABLED(CONFIG_CPM))
		return tsa->version == TSA_QE;

	return IS_ENABLED(CONFIG_QUICC_ENGINE);
}

static int tsa_qe_serial_get_num(struct tsa_serial *tsa_serial)
{
	struct tsa *tsa = tsa_serial_get_tsa(tsa_serial);

	switch (tsa_serial->id) {
	case FSL_QE_TSA_UCC1: return 0;
	case FSL_QE_TSA_UCC2: return 1;
	case FSL_QE_TSA_UCC3: return 2;
	case FSL_QE_TSA_UCC4: return 3;
	case FSL_QE_TSA_UCC5: return 4;
	default:
		break;
	}

	dev_err(tsa->dev, "Unsupported serial id %u\n", tsa_serial->id);
	return -EINVAL;
}

int tsa_serial_get_num(struct tsa_serial *tsa_serial)
{
	struct tsa *tsa = tsa_serial_get_tsa(tsa_serial);

	/*
	 * There is no need to get the serial num out of the TSA driver in the
	 * CPM case.
	 * Further more, in CPM, we can have 2 types of serial SCCs and FCCs.
	 * What kind of numbering to use that can be global to both SCCs and
	 * FCCs ?
	 */
	return tsa_is_qe(tsa) ? tsa_qe_serial_get_num(tsa_serial) : -EOPNOTSUPP;
}
EXPORT_SYMBOL(tsa_serial_get_num);

static int tsa_cpm1_serial_connect(struct tsa_serial *tsa_serial, bool connect)
{
	struct tsa *tsa = tsa_serial_get_tsa(tsa_serial);
	unsigned long flags;
	u32 clear;
	u32 set;

	switch (tsa_serial->id) {
	case FSL_CPM_TSA_SCC2:
		clear = TSA_CPM1_SICR_SCC2(TSA_CPM1_SICR_SCC_MASK);
		set = TSA_CPM1_SICR_SCC2(TSA_CPM1_SICR_SCC_SCX_TSA);
		break;
	case FSL_CPM_TSA_SCC3:
		clear = TSA_CPM1_SICR_SCC3(TSA_CPM1_SICR_SCC_MASK);
		set = TSA_CPM1_SICR_SCC3(TSA_CPM1_SICR_SCC_SCX_TSA);
		break;
	case FSL_CPM_TSA_SCC4:
		clear = TSA_CPM1_SICR_SCC4(TSA_CPM1_SICR_SCC_MASK);
		set = TSA_CPM1_SICR_SCC4(TSA_CPM1_SICR_SCC_SCX_TSA);
		break;
	default:
		dev_err(tsa->dev, "Unsupported serial id %u\n", tsa_serial->id);
		return -EINVAL;
	}

	spin_lock_irqsave(&tsa->lock, flags);
	tsa_clrsetbits32(tsa->si_regs + TSA_CPM1_SICR, clear,
			 connect ? set : 0);
	spin_unlock_irqrestore(&tsa->lock, flags);

	return 0;
}

static int tsa_qe_serial_connect(struct tsa_serial *tsa_serial, bool connect)
{
	struct tsa *tsa = tsa_serial_get_tsa(tsa_serial);
	unsigned long flags;
	int ucc_num;
	int ret;

	ucc_num = tsa_qe_serial_get_num(tsa_serial);
	if (ucc_num < 0)
		return ucc_num;

	spin_lock_irqsave(&tsa->lock, flags);
	ret = ucc_set_qe_mux_tsa(ucc_num, connect);
	spin_unlock_irqrestore(&tsa->lock, flags);
	if (ret) {
		dev_err(tsa->dev, "Connect serial id %u to TSA failed (%d)\n",
			tsa_serial->id, ret);
		return ret;
	}
	return 0;
}

int tsa_serial_connect(struct tsa_serial *tsa_serial)
{
	struct tsa *tsa = tsa_serial_get_tsa(tsa_serial);

	return tsa_is_qe(tsa) ?
		tsa_qe_serial_connect(tsa_serial, true) :
		tsa_cpm1_serial_connect(tsa_serial, true);
}
EXPORT_SYMBOL(tsa_serial_connect);

int tsa_serial_disconnect(struct tsa_serial *tsa_serial)
{
	struct tsa *tsa = tsa_serial_get_tsa(tsa_serial);

	return tsa_is_qe(tsa) ?
		tsa_qe_serial_connect(tsa_serial, false) :
		tsa_cpm1_serial_connect(tsa_serial, false);
}
EXPORT_SYMBOL(tsa_serial_disconnect);

int tsa_serial_get_info(struct tsa_serial *tsa_serial, struct tsa_serial_info *info)
{
	memcpy(info, &tsa_serial->info, sizeof(*info));
	return 0;
}
EXPORT_SYMBOL(tsa_serial_get_info);

static void tsa_cpm1_init_entries_area(struct tsa *tsa, struct tsa_entries_area *area,
				       u32 tdms, u32 tdm_id, bool is_rx)
{
	resource_size_t quarter;
	resource_size_t half;

	quarter = tsa->si_ram_sz / 4;
	half = tsa->si_ram_sz / 2;

	if (tdms == BIT(TSA_TDMA)) {
		/* Only TDMA */
		if (is_rx) {
			/* First half of si_ram */
			area->entries_start = tsa->si_ram;
			area->entries_next = area->entries_start + half;
			area->last_entry = NULL;
		} else {
			/* Second half of si_ram */
			area->entries_start = tsa->si_ram + half;
			area->entries_next = area->entries_start + half;
			area->last_entry = NULL;
		}
	} else {
		/* Only TDMB or both TDMs */
		if (tdm_id == TSA_TDMA) {
			if (is_rx) {
				/* First half of first half of si_ram */
				area->entries_start = tsa->si_ram;
				area->entries_next = area->entries_start + quarter;
				area->last_entry = NULL;
			} else {
				/* First half of second half of si_ram */
				area->entries_start = tsa->si_ram + (2 * quarter);
				area->entries_next = area->entries_start + quarter;
				area->last_entry = NULL;
			}
		} else {
			if (is_rx) {
				/* Second half of first half of si_ram */
				area->entries_start = tsa->si_ram + quarter;
				area->entries_next = area->entries_start + quarter;
				area->last_entry = NULL;
			} else {
				/* Second half of second half of si_ram */
				area->entries_start = tsa->si_ram + (3 * quarter);
				area->entries_next = area->entries_start + quarter;
				area->last_entry = NULL;
			}
		}
	}
}

static void tsa_qe_init_entries_area(struct tsa *tsa, struct tsa_entries_area *area,
				     u32 tdms, u32 tdm_id, bool is_rx)
{
	resource_size_t eighth;
	resource_size_t half;

	eighth = tsa->si_ram_sz / 8;
	half = tsa->si_ram_sz / 2;

	/*
	 * One half of the SI RAM used for Tx, the other one for Rx.
	 * In each half, 1/4 of the area is assigned to each TDM.
	 */
	if (is_rx) {
		/* Rx: Second half of si_ram */
		area->entries_start = tsa->si_ram + half + (eighth * tdm_id);
		area->entries_next = area->entries_start + eighth;
		area->last_entry = NULL;
	} else {
		/* Tx: First half of si_ram */
		area->entries_start = tsa->si_ram + (eighth * tdm_id);
		area->entries_next = area->entries_start + eighth;
		area->last_entry = NULL;
	}
}

static void tsa_init_entries_area(struct tsa *tsa, struct tsa_entries_area *area,
				  u32 tdms, u32 tdm_id, bool is_rx)
{
	if (tsa_is_qe(tsa))
		tsa_qe_init_entries_area(tsa, area, tdms, tdm_id, is_rx);
	else
		tsa_cpm1_init_entries_area(tsa, area, tdms, tdm_id, is_rx);
}

static const char *tsa_cpm1_serial_id2name(struct tsa *tsa, u32 serial_id)
{
	switch (serial_id) {
	case FSL_CPM_TSA_NU:	return "Not used";
	case FSL_CPM_TSA_SCC2:	return "SCC2";
	case FSL_CPM_TSA_SCC3:	return "SCC3";
	case FSL_CPM_TSA_SCC4:	return "SCC4";
	case FSL_CPM_TSA_SMC1:	return "SMC1";
	case FSL_CPM_TSA_SMC2:	return "SMC2";
	default:
		break;
	}
	return NULL;
}

static const char *tsa_qe_serial_id2name(struct tsa *tsa, u32 serial_id)
{
	switch (serial_id) {
	case FSL_QE_TSA_NU:	return "Not used";
	case FSL_QE_TSA_UCC1:	return "UCC1";
	case FSL_QE_TSA_UCC2:	return "UCC2";
	case FSL_QE_TSA_UCC3:	return "UCC3";
	case FSL_QE_TSA_UCC4:	return "UCC4";
	case FSL_QE_TSA_UCC5:	return "UCC5";
	default:
		break;
	}
	return NULL;
}

static const char *tsa_serial_id2name(struct tsa *tsa, u32 serial_id)
{
	return tsa_is_qe(tsa) ?
		tsa_qe_serial_id2name(tsa, serial_id) :
		tsa_cpm1_serial_id2name(tsa, serial_id);
}

static u32 tsa_cpm1_serial_id2csel(struct tsa *tsa, u32 serial_id)
{
	switch (serial_id) {
	case FSL_CPM_TSA_SCC2:	return TSA_CPM1_SIRAM_ENTRY_CSEL_SCC2;
	case FSL_CPM_TSA_SCC3:	return TSA_CPM1_SIRAM_ENTRY_CSEL_SCC3;
	case FSL_CPM_TSA_SCC4:	return TSA_CPM1_SIRAM_ENTRY_CSEL_SCC4;
	case FSL_CPM_TSA_SMC1:	return TSA_CPM1_SIRAM_ENTRY_CSEL_SMC1;
	case FSL_CPM_TSA_SMC2:	return TSA_CPM1_SIRAM_ENTRY_CSEL_SMC2;
	default:
		break;
	}
	return TSA_CPM1_SIRAM_ENTRY_CSEL_NU;
}

static int tsa_cpm1_add_entry(struct tsa *tsa, struct tsa_entries_area *area,
			      u32 count, u32 serial_id)
{
	void __iomem *addr;
	u32 left;
	u32 val;
	u32 cnt;
	u32 nb;

	addr = area->last_entry ? area->last_entry + 4 : area->entries_start;

	nb = DIV_ROUND_UP(count, 8);
	if ((addr + (nb * 4)) > area->entries_next) {
		dev_err(tsa->dev, "si ram area full\n");
		return -ENOSPC;
	}

	if (area->last_entry) {
		/* Clear last flag */
		tsa_clrbits32(area->last_entry, TSA_CPM1_SIRAM_ENTRY_LAST);
	}

	left = count;
	while (left) {
		val = TSA_CPM1_SIRAM_ENTRY_BYTE | tsa_cpm1_serial_id2csel(tsa, serial_id);

		if (left > 16) {
			cnt = 16;
		} else {
			cnt = left;
			val |= TSA_CPM1_SIRAM_ENTRY_LAST;
			area->last_entry = addr;
		}
		val |= TSA_CPM1_SIRAM_ENTRY_CNT(cnt - 1);

		tsa_write32(addr, val);
		addr += 4;
		left -= cnt;
	}

	return 0;
}

static u32 tsa_qe_serial_id2csel(struct tsa *tsa, u32 serial_id)
{
	switch (serial_id) {
	case FSL_QE_TSA_UCC1:	return TSA_QE_SIRAM_ENTRY_CSEL_UCC1;
	case FSL_QE_TSA_UCC2:	return TSA_QE_SIRAM_ENTRY_CSEL_UCC2;
	case FSL_QE_TSA_UCC3:	return TSA_QE_SIRAM_ENTRY_CSEL_UCC3;
	case FSL_QE_TSA_UCC4:	return TSA_QE_SIRAM_ENTRY_CSEL_UCC4;
	case FSL_QE_TSA_UCC5:	return TSA_QE_SIRAM_ENTRY_CSEL_UCC5;
	default:
		break;
	}
	return TSA_QE_SIRAM_ENTRY_CSEL_NU;
}

static int tsa_qe_add_entry(struct tsa *tsa, struct tsa_entries_area *area,
			    u32 count, u32 serial_id)
{
	void __iomem *addr;
	u32 left;
	u32 val;
	u32 cnt;
	u32 nb;

	addr = area->last_entry ? area->last_entry + 2 : area->entries_start;

	nb = DIV_ROUND_UP(count, 8);
	if ((addr + (nb * 2)) > area->entries_next) {
		dev_err(tsa->dev, "si ram area full\n");
		return -ENOSPC;
	}

	if (area->last_entry) {
		/* Clear last flag */
		tsa_clrbits16(area->last_entry, TSA_QE_SIRAM_ENTRY_LAST);
	}

	left = count;
	while (left) {
		val = TSA_QE_SIRAM_ENTRY_BYTE | tsa_qe_serial_id2csel(tsa, serial_id);

		if (left > 8) {
			cnt = 8;
		} else {
			cnt = left;
			val |= TSA_QE_SIRAM_ENTRY_LAST;
			area->last_entry = addr;
		}
		val |= TSA_QE_SIRAM_ENTRY_CNT(cnt - 1);

		tsa_write16(addr, val);
		addr += 2;
		left -= cnt;
	}

	return 0;
}

static int tsa_add_entry(struct tsa *tsa, struct tsa_entries_area *area,
			 u32 count, u32 serial_id)
{
	return tsa_is_qe(tsa) ?
		tsa_qe_add_entry(tsa, area, count, serial_id) :
		tsa_cpm1_add_entry(tsa, area, count, serial_id);
}

static int tsa_of_parse_tdm_route(struct tsa *tsa, struct device_node *tdm_np,
				  u32 tdms, u32 tdm_id, bool is_rx)
{
	struct tsa_entries_area area;
	const char *route_name;
	u32 serial_id;
	int len, i;
	u32 count;
	const char *serial_name;
	struct tsa_serial_info *serial_info;
	struct tsa_tdm *tdm;
	int ret;
	u32 ts;

	route_name = is_rx ? "fsl,rx-ts-routes" : "fsl,tx-ts-routes";

	len = of_property_count_u32_elems(tdm_np,  route_name);
	if (len < 0) {
		dev_err(tsa->dev, "%pOF: failed to read %s\n", tdm_np, route_name);
		return len;
	}
	if (len % 2 != 0) {
		dev_err(tsa->dev, "%pOF: wrong %s format\n", tdm_np, route_name);
		return -EINVAL;
	}

	tsa_init_entries_area(tsa, &area, tdms, tdm_id, is_rx);
	ts = 0;
	for (i = 0; i < len; i += 2) {
		of_property_read_u32_index(tdm_np, route_name, i, &count);
		of_property_read_u32_index(tdm_np, route_name, i + 1, &serial_id);

		if (serial_id >= ARRAY_SIZE(tsa->serials)) {
			dev_err(tsa->dev, "%pOF: invalid serial id (%u)\n",
				tdm_np, serial_id);
			return -EINVAL;
		}

		serial_name = tsa_serial_id2name(tsa, serial_id);
		if (!serial_name) {
			dev_err(tsa->dev, "%pOF: unsupported serial id (%u)\n",
				tdm_np, serial_id);
			return -EINVAL;
		}

		dev_dbg(tsa->dev, "tdm_id=%u, %s ts %u..%u -> %s\n",
			tdm_id, route_name, ts, ts + count - 1, serial_name);
		ts += count;

		ret = tsa_add_entry(tsa, &area, count, serial_id);
		if (ret)
			return ret;

		serial_info = &tsa->serials[serial_id].info;
		tdm = &tsa->tdm[tdm_id];
		if (is_rx) {
			serial_info->rx_fs_rate = clk_get_rate(tdm->l1rsync_clk);
			serial_info->rx_bit_rate = clk_get_rate(tdm->l1rclk_clk);
			serial_info->nb_rx_ts += count;
		} else {
			serial_info->tx_fs_rate = tdm->l1tsync_clk ?
				clk_get_rate(tdm->l1tsync_clk) :
				clk_get_rate(tdm->l1rsync_clk);
			serial_info->tx_bit_rate = tdm->l1tclk_clk ?
				clk_get_rate(tdm->l1tclk_clk) :
				clk_get_rate(tdm->l1rclk_clk);
			serial_info->nb_tx_ts += count;
		}
	}
	return 0;
}

static inline int tsa_of_parse_tdm_rx_route(struct tsa *tsa,
					    struct device_node *tdm_np,
					    u32 tdms, u32 tdm_id)
{
	return tsa_of_parse_tdm_route(tsa, tdm_np, tdms, tdm_id, true);
}

static inline int tsa_of_parse_tdm_tx_route(struct tsa *tsa,
					    struct device_node *tdm_np,
					    u32 tdms, u32 tdm_id)
{
	return tsa_of_parse_tdm_route(tsa, tdm_np, tdms, tdm_id, false);
}

static int tsa_of_parse_tdms(struct tsa *tsa, struct device_node *np)
{
	struct device_node *tdm_np;
	struct tsa_tdm *tdm;
	struct clk *clk;
	u32 tdm_id, val;
	int ret;
	int i;

	tsa->tdms = 0;
	for (i = 0; i < ARRAY_SIZE(tsa->tdm); i++)
		tsa->tdm[i].is_enable = false;

	for_each_available_child_of_node(np, tdm_np) {
		ret = of_property_read_u32(tdm_np, "reg", &tdm_id);
		if (ret) {
			dev_err(tsa->dev, "%pOF: failed to read reg\n", tdm_np);
			of_node_put(tdm_np);
			return ret;
		}
		switch (tdm_id) {
		case 0:
			tsa->tdms |= BIT(TSA_TDMA);
			break;
		case 1:
			tsa->tdms |= BIT(TSA_TDMB);
			break;
		case 2:
			if (!tsa_is_qe(tsa))
				goto invalid_tdm; /* Not available on CPM1 */
			tsa->tdms |= BIT(TSA_TDMC);
			break;
		case 3:
			if (!tsa_is_qe(tsa))
				goto invalid_tdm;  /* Not available on CPM1 */
			tsa->tdms |= BIT(TSA_TDMD);
			break;
		default:
invalid_tdm:
			dev_err(tsa->dev, "%pOF: Invalid tdm_id (%u)\n", tdm_np,
				tdm_id);
			of_node_put(tdm_np);
			return -EINVAL;
		}
	}

	for_each_available_child_of_node(np, tdm_np) {
		ret = of_property_read_u32(tdm_np, "reg", &tdm_id);
		if (ret) {
			dev_err(tsa->dev, "%pOF: failed to read reg\n", tdm_np);
			of_node_put(tdm_np);
			return ret;
		}

		tdm = &tsa->tdm[tdm_id];
		tdm->simode_tdm = TSA_SIMODE_TDM_SDM_NORM;

		val = 0;
		ret = of_property_read_u32(tdm_np, "fsl,rx-frame-sync-delay-bits",
					   &val);
		if (ret && ret != -EINVAL) {
			dev_err(tsa->dev,
				"%pOF: failed to read fsl,rx-frame-sync-delay-bits\n",
				tdm_np);
			of_node_put(tdm_np);
			return ret;
		}
		if (val > 3) {
			dev_err(tsa->dev,
				"%pOF: Invalid fsl,rx-frame-sync-delay-bits (%u)\n",
				tdm_np, val);
			of_node_put(tdm_np);
			return -EINVAL;
		}
		tdm->simode_tdm |= TSA_SIMODE_TDM_RFSD(val);

		val = 0;
		ret = of_property_read_u32(tdm_np, "fsl,tx-frame-sync-delay-bits",
					   &val);
		if (ret && ret != -EINVAL) {
			dev_err(tsa->dev,
				"%pOF: failed to read fsl,tx-frame-sync-delay-bits\n",
				tdm_np);
			of_node_put(tdm_np);
			return ret;
		}
		if (val > 3) {
			dev_err(tsa->dev,
				"%pOF: Invalid fsl,tx-frame-sync-delay-bits (%u)\n",
				tdm_np, val);
			of_node_put(tdm_np);
			return -EINVAL;
		}
		tdm->simode_tdm |= TSA_SIMODE_TDM_TFSD(val);

		if (of_property_read_bool(tdm_np, "fsl,common-rxtx-pins"))
			tdm->simode_tdm |= TSA_SIMODE_TDM_CRT;

		if (of_property_read_bool(tdm_np, "fsl,clock-falling-edge"))
			tdm->simode_tdm |= TSA_SIMODE_TDM_CE;

		if (of_property_read_bool(tdm_np, "fsl,fsync-rising-edge"))
			tdm->simode_tdm |= TSA_SIMODE_TDM_FE;

		if (tsa_is_qe(tsa) &&
		    of_property_read_bool(tdm_np, "fsl,fsync-active-low"))
			tdm->simode_tdm |= TSA_QE_SIMODE_TDM_SL;

		if (of_property_read_bool(tdm_np, "fsl,double-speed-clock"))
			tdm->simode_tdm |= TSA_SIMODE_TDM_DSC;

		clk = of_clk_get_by_name(tdm_np, tsa_is_qe(tsa) ? "rsync" : "l1rsync");
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			of_node_put(tdm_np);
			goto err;
		}
		ret = clk_prepare_enable(clk);
		if (ret) {
			clk_put(clk);
			of_node_put(tdm_np);
			goto err;
		}
		tdm->l1rsync_clk = clk;

		clk = of_clk_get_by_name(tdm_np, tsa_is_qe(tsa) ? "rclk" : "l1rclk");
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			of_node_put(tdm_np);
			goto err;
		}
		ret = clk_prepare_enable(clk);
		if (ret) {
			clk_put(clk);
			of_node_put(tdm_np);
			goto err;
		}
		tdm->l1rclk_clk = clk;

		if (!(tdm->simode_tdm & TSA_SIMODE_TDM_CRT)) {
			clk = of_clk_get_by_name(tdm_np, tsa_is_qe(tsa) ? "tsync" : "l1tsync");
			if (IS_ERR(clk)) {
				ret = PTR_ERR(clk);
				of_node_put(tdm_np);
				goto err;
			}
			ret = clk_prepare_enable(clk);
			if (ret) {
				clk_put(clk);
				of_node_put(tdm_np);
				goto err;
			}
			tdm->l1tsync_clk = clk;

			clk = of_clk_get_by_name(tdm_np, tsa_is_qe(tsa) ? "tclk" : "l1tclk");
			if (IS_ERR(clk)) {
				ret = PTR_ERR(clk);
				of_node_put(tdm_np);
				goto err;
			}
			ret = clk_prepare_enable(clk);
			if (ret) {
				clk_put(clk);
				of_node_put(tdm_np);
				goto err;
			}
			tdm->l1tclk_clk = clk;
		}

		if (tsa_is_qe(tsa)) {
			/*
			 * The starting address for TSA table must be set.
			 * 512 entries for Tx and 512 entries for Rx are
			 * available for 4 TDMs.
			 * We assign entries equally -> 128 Rx/Tx entries per
			 * TDM. In other words, 4 blocks of 32 entries per TDM.
			 */
			tdm->simode_tdm |= TSA_QE_SIMODE_TDM_SAD(4 * tdm_id);
		}

		ret = tsa_of_parse_tdm_rx_route(tsa, tdm_np, tsa->tdms, tdm_id);
		if (ret) {
			of_node_put(tdm_np);
			goto err;
		}

		ret = tsa_of_parse_tdm_tx_route(tsa, tdm_np, tsa->tdms, tdm_id);
		if (ret) {
			of_node_put(tdm_np);
			goto err;
		}

		tdm->is_enable = true;
	}
	return 0;

err:
	for (i = 0; i < ARRAY_SIZE(tsa->tdm); i++) {
		if (tsa->tdm[i].l1rsync_clk) {
			clk_disable_unprepare(tsa->tdm[i].l1rsync_clk);
			clk_put(tsa->tdm[i].l1rsync_clk);
		}
		if (tsa->tdm[i].l1rclk_clk) {
			clk_disable_unprepare(tsa->tdm[i].l1rclk_clk);
			clk_put(tsa->tdm[i].l1rclk_clk);
		}
		if (tsa->tdm[i].l1tsync_clk) {
			clk_disable_unprepare(tsa->tdm[i].l1rsync_clk);
			clk_put(tsa->tdm[i].l1rsync_clk);
		}
		if (tsa->tdm[i].l1tclk_clk) {
			clk_disable_unprepare(tsa->tdm[i].l1rclk_clk);
			clk_put(tsa->tdm[i].l1rclk_clk);
		}
	}
	return ret;
}

static void tsa_init_si_ram(struct tsa *tsa)
{
	resource_size_t i;

	/* Fill all entries as the last one */
	if (tsa_is_qe(tsa)) {
		for (i = 0; i < tsa->si_ram_sz; i += 2)
			tsa_write16(tsa->si_ram + i, TSA_QE_SIRAM_ENTRY_LAST);
	} else {
		for (i = 0; i < tsa->si_ram_sz; i += 4)
			tsa_write32(tsa->si_ram + i, TSA_CPM1_SIRAM_ENTRY_LAST);
	}
}

static int tsa_cpm1_setup(struct tsa *tsa)
{
	u32 val;

	/* Set SIMODE */
	val = 0;
	if (tsa->tdm[0].is_enable)
		val |= TSA_CPM1_SIMODE_TDMA(tsa->tdm[0].simode_tdm);
	if (tsa->tdm[1].is_enable)
		val |= TSA_CPM1_SIMODE_TDMB(tsa->tdm[1].simode_tdm);

	tsa_clrsetbits32(tsa->si_regs + TSA_CPM1_SIMODE,
			 TSA_CPM1_SIMODE_TDMA(TSA_CPM1_SIMODE_TDM_MASK) |
			 TSA_CPM1_SIMODE_TDMB(TSA_CPM1_SIMODE_TDM_MASK),
			 val);

	/* Set SIGMR */
	val = (tsa->tdms == BIT(TSA_TDMA)) ?
		TSA_CPM1_SIGMR_RDM_STATIC_TDMA : TSA_CPM1_SIGMR_RDM_STATIC_TDMAB;
	if (tsa->tdms & BIT(TSA_TDMA))
		val |= TSA_CPM1_SIGMR_ENA;
	if (tsa->tdms & BIT(TSA_TDMB))
		val |= TSA_CPM1_SIGMR_ENB;
	tsa_write8(tsa->si_regs + TSA_CPM1_SIGMR, val);

	return 0;
}

static int tsa_qe_setup(struct tsa *tsa)
{
	unsigned int sixmr;
	u8 siglmrh = 0;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(tsa->tdm); i++) {
		if (!tsa->tdm[i].is_enable)
			continue;

		switch (i) {
		case 0:
			sixmr = TSA_QE_SIAMR;
			siglmrh |= TSA_QE_SIGLMRH_ENA;
			break;
		case 1:
			sixmr = TSA_QE_SIBMR;
			siglmrh |= TSA_QE_SIGLMRH_ENB;
			break;
		case 2:
			sixmr = TSA_QE_SICMR;
			siglmrh |= TSA_QE_SIGLMRH_ENC;
			break;
		case 3:
			sixmr = TSA_QE_SIDMR;
			siglmrh |= TSA_QE_SIGLMRH_END;
			break;
		default:
			return -EINVAL;
		}

		/* Set SI mode register */
		tsa_write16(tsa->si_regs + sixmr, tsa->tdm[i].simode_tdm);
	}

	/* Enable TDMs */
	tsa_write8(tsa->si_regs + TSA_QE_SIGLMRH, siglmrh);

	return 0;
}

static int tsa_setup(struct tsa *tsa)
{
	return tsa_is_qe(tsa) ? tsa_qe_setup(tsa) : tsa_cpm1_setup(tsa);
}

static int tsa_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	struct tsa *tsa;
	unsigned int i;
	int ret;

	tsa = devm_kzalloc(&pdev->dev, sizeof(*tsa), GFP_KERNEL);
	if (!tsa)
		return -ENOMEM;

	tsa->dev = &pdev->dev;
	tsa->version = (enum tsa_version)(uintptr_t)of_device_get_match_data(&pdev->dev);
	switch (tsa->version) {
	case TSA_CPM1:
		dev_info(tsa->dev, "CPM1 version\n");
		break;
	case TSA_QE:
		dev_info(tsa->dev, "QE version\n");
		break;
	default:
		dev_err(tsa->dev, "Unknown version (%d)\n", tsa->version);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(tsa->serials); i++)
		tsa->serials[i].id = i;

	spin_lock_init(&tsa->lock);

	tsa->si_regs = devm_platform_ioremap_resource_byname(pdev, "si_regs");
	if (IS_ERR(tsa->si_regs))
		return PTR_ERR(tsa->si_regs);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "si_ram");
	if (!res) {
		dev_err(tsa->dev, "si_ram resource missing\n");
		return -EINVAL;
	}
	tsa->si_ram_sz = resource_size(res);
	tsa->si_ram = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(tsa->si_ram))
		return PTR_ERR(tsa->si_ram);

	tsa_init_si_ram(tsa);

	ret = tsa_of_parse_tdms(tsa, np);
	if (ret)
		return ret;

	ret = tsa_setup(tsa);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, tsa);

	return 0;
}

static void tsa_remove(struct platform_device *pdev)
{
	struct tsa *tsa = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < ARRAY_SIZE(tsa->tdm); i++) {
		if (tsa->tdm[i].l1rsync_clk) {
			clk_disable_unprepare(tsa->tdm[i].l1rsync_clk);
			clk_put(tsa->tdm[i].l1rsync_clk);
		}
		if (tsa->tdm[i].l1rclk_clk) {
			clk_disable_unprepare(tsa->tdm[i].l1rclk_clk);
			clk_put(tsa->tdm[i].l1rclk_clk);
		}
		if (tsa->tdm[i].l1tsync_clk) {
			clk_disable_unprepare(tsa->tdm[i].l1rsync_clk);
			clk_put(tsa->tdm[i].l1rsync_clk);
		}
		if (tsa->tdm[i].l1tclk_clk) {
			clk_disable_unprepare(tsa->tdm[i].l1rclk_clk);
			clk_put(tsa->tdm[i].l1rclk_clk);
		}
	}
}

static const struct of_device_id tsa_id_table[] = {
#if IS_ENABLED(CONFIG_CPM1)
	{ .compatible = "fsl,cpm1-tsa", .data = (void *)TSA_CPM1 },
#endif
#if IS_ENABLED(CONFIG_QUICC_ENGINE)
	{ .compatible = "fsl,qe-tsa", .data = (void *)TSA_QE },
#endif
	{} /* sentinel */
};
MODULE_DEVICE_TABLE(of, tsa_id_table);

static struct platform_driver tsa_driver = {
	.driver = {
		.name = "fsl-tsa",
		.of_match_table = of_match_ptr(tsa_id_table),
	},
	.probe = tsa_probe,
	.remove_new = tsa_remove,
};
module_platform_driver(tsa_driver);

struct tsa_serial *tsa_serial_get_byphandle(struct device_node *np,
					    const char *phandle_name)
{
	struct of_phandle_args out_args;
	struct platform_device *pdev;
	struct tsa_serial *tsa_serial;
	struct tsa *tsa;
	int ret;

	ret = of_parse_phandle_with_fixed_args(np, phandle_name, 1, 0, &out_args);
	if (ret < 0)
		return ERR_PTR(ret);

	if (!of_match_node(tsa_driver.driver.of_match_table, out_args.np)) {
		of_node_put(out_args.np);
		return ERR_PTR(-EINVAL);
	}

	pdev = of_find_device_by_node(out_args.np);
	of_node_put(out_args.np);
	if (!pdev)
		return ERR_PTR(-ENODEV);

	tsa = platform_get_drvdata(pdev);
	if (!tsa) {
		platform_device_put(pdev);
		return ERR_PTR(-EPROBE_DEFER);
	}

	if (out_args.args_count != 1) {
		platform_device_put(pdev);
		return ERR_PTR(-EINVAL);
	}

	if (out_args.args[0] >= ARRAY_SIZE(tsa->serials)) {
		platform_device_put(pdev);
		return ERR_PTR(-EINVAL);
	}

	tsa_serial = &tsa->serials[out_args.args[0]];

	/*
	 * Be sure that the serial id matches the phandle arg.
	 * The tsa_serials table is indexed by serial ids. The serial id is set
	 * during the probe() call and needs to be coherent.
	 */
	if (WARN_ON(tsa_serial->id != out_args.args[0])) {
		platform_device_put(pdev);
		return ERR_PTR(-EINVAL);
	}

	return tsa_serial;
}
EXPORT_SYMBOL(tsa_serial_get_byphandle);

void tsa_serial_put(struct tsa_serial *tsa_serial)
{
	struct tsa *tsa = tsa_serial_get_tsa(tsa_serial);

	put_device(tsa->dev);
}
EXPORT_SYMBOL(tsa_serial_put);

static void devm_tsa_serial_release(struct device *dev, void *res)
{
	struct tsa_serial **tsa_serial = res;

	tsa_serial_put(*tsa_serial);
}

struct tsa_serial *devm_tsa_serial_get_byphandle(struct device *dev,
						 struct device_node *np,
						 const char *phandle_name)
{
	struct tsa_serial *tsa_serial;
	struct tsa_serial **dr;

	dr = devres_alloc(devm_tsa_serial_release, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return ERR_PTR(-ENOMEM);

	tsa_serial = tsa_serial_get_byphandle(np, phandle_name);
	if (!IS_ERR(tsa_serial)) {
		*dr = tsa_serial;
		devres_add(dev, dr);
	} else {
		devres_free(dr);
	}

	return tsa_serial;
}
EXPORT_SYMBOL(devm_tsa_serial_get_byphandle);

MODULE_AUTHOR("Herve Codina <herve.codina@bootlin.com>");
MODULE_DESCRIPTION("CPM/QE TSA driver");
MODULE_LICENSE("GPL");
