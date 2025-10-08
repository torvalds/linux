// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2017 ATMEL
 * Copyright 2017 Free Electrons
 *
 * Author: Boris Brezillon <boris.brezillon@free-electrons.com>
 *
 * Derived from the atmel_nand.c driver which contained the following
 * copyrights:
 *
 *   Copyright 2003 Rick Bronson
 *
 *   Derived from drivers/mtd/nand/autcpu12.c (removed in v3.8)
 *	Copyright 2001 Thomas Gleixner (gleixner@autronix.de)
 *
 *   Derived from drivers/mtd/spia.c (removed in v3.8)
 *	Copyright 2000 Steven J. Hill (sjhill@cotw.com)
 *
 *   Add Hardware ECC support for AT91SAM9260 / AT91SAM9263
 *	Richard Genoud (richard.genoud@gmail.com), Adeneo Copyright 2007
 *
 *   Derived from Das U-Boot source code
 *	(u-boot-1.1.5/board/atmel/at91sam9263ek/nand.c)
 *      Copyright 2006 ATMEL Rousset, Lacressonniere Nicolas
 *
 *   Add Programmable Multibit ECC support for various AT91 SoC
 *	Copyright 2012 ATMEL, Hong Xu
 *
 *   Add Nand Flash Controller support for SAMA5 SoC
 *	Copyright 2013 ATMEL, Josh Wu (josh.wu@atmel.com)
 *
 * The PMECC is an hardware assisted BCH engine, which means part of the
 * ECC algorithm is left to the software. The hardware/software repartition
 * is explained in the "PMECC Controller Functional Description" chapter in
 * Atmel datasheets, and some of the functions in this file are directly
 * implementing the algorithms described in the "Software Implementation"
 * sub-section.
 *
 * TODO: it seems that the software BCH implementation in lib/bch.c is already
 * providing some of the logic we are implementing here. It would be smart
 * to expose the needed lib/bch.c helpers/functions and re-use them here.
 */

#include <linux/genalloc.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mtd/rawnand.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "pmecc.h"

/* Galois field dimension */
#define PMECC_GF_DIMENSION_13			13
#define PMECC_GF_DIMENSION_14			14

/* Primitive Polynomial used by PMECC */
#define PMECC_GF_13_PRIMITIVE_POLY		0x201b
#define PMECC_GF_14_PRIMITIVE_POLY		0x4443

#define PMECC_LOOKUP_TABLE_SIZE_512		0x2000
#define PMECC_LOOKUP_TABLE_SIZE_1024		0x4000

/* Time out value for reading PMECC status register */
#define PMECC_MAX_TIMEOUT_MS			100

/* PMECC Register Definitions */
#define ATMEL_PMECC_CFG				0x0
#define PMECC_CFG_BCH_STRENGTH(x)		(x)
#define PMECC_CFG_BCH_STRENGTH_MASK		GENMASK(2, 0)
#define PMECC_CFG_SECTOR512			(0 << 4)
#define PMECC_CFG_SECTOR1024			(1 << 4)
#define PMECC_CFG_NSECTORS(x)			((fls(x) - 1) << 8)
#define PMECC_CFG_READ_OP			(0 << 12)
#define PMECC_CFG_WRITE_OP			(1 << 12)
#define PMECC_CFG_SPARE_ENABLE			BIT(16)
#define PMECC_CFG_AUTO_ENABLE			BIT(20)

#define ATMEL_PMECC_SAREA			0x4
#define ATMEL_PMECC_SADDR			0x8
#define ATMEL_PMECC_EADDR			0xc

#define ATMEL_PMECC_CLK				0x10
#define PMECC_CLK_133MHZ			(2 << 0)

#define ATMEL_PMECC_CTRL			0x14
#define PMECC_CTRL_RST				BIT(0)
#define PMECC_CTRL_DATA				BIT(1)
#define PMECC_CTRL_USER				BIT(2)
#define PMECC_CTRL_ENABLE			BIT(4)
#define PMECC_CTRL_DISABLE			BIT(5)

#define ATMEL_PMECC_SR				0x18
#define PMECC_SR_BUSY				BIT(0)
#define PMECC_SR_ENABLE				BIT(4)

#define ATMEL_PMECC_IER				0x1c
#define ATMEL_PMECC_IDR				0x20
#define ATMEL_PMECC_IMR				0x24
#define ATMEL_PMECC_ISR				0x28
#define PMECC_ERROR_INT				BIT(0)

#define ATMEL_PMECC_ECC(sector, n)		\
	((((sector) + 1) * 0x40) + (n))

#define ATMEL_PMECC_REM(sector, n)		\
	((((sector) + 1) * 0x40) + ((n) * 4) + 0x200)

/* PMERRLOC Register Definitions */
#define ATMEL_PMERRLOC_ELCFG			0x0
#define PMERRLOC_ELCFG_SECTOR_512		(0 << 0)
#define PMERRLOC_ELCFG_SECTOR_1024		(1 << 0)
#define PMERRLOC_ELCFG_NUM_ERRORS(n)		((n) << 16)

#define ATMEL_PMERRLOC_ELPRIM			0x4
#define ATMEL_PMERRLOC_ELEN			0x8
#define ATMEL_PMERRLOC_ELDIS			0xc
#define PMERRLOC_DISABLE			BIT(0)

#define ATMEL_PMERRLOC_ELSR			0x10
#define PMERRLOC_ELSR_BUSY			BIT(0)

#define ATMEL_PMERRLOC_ELIER			0x14
#define ATMEL_PMERRLOC_ELIDR			0x18
#define ATMEL_PMERRLOC_ELIMR			0x1c
#define ATMEL_PMERRLOC_ELISR			0x20
#define PMERRLOC_ERR_NUM_MASK			GENMASK(12, 8)
#define PMERRLOC_CALC_DONE			BIT(0)

#define ATMEL_PMERRLOC_SIGMA(x)			(((x) * 0x4) + 0x28)

#define ATMEL_PMERRLOC_EL(offs, x)		(((x) * 0x4) + (offs))

struct atmel_pmecc_gf_tables {
	u16 *alpha_to;
	u16 *index_of;
};

struct atmel_pmecc_caps {
	const int *strengths;
	int nstrengths;
	int el_offset;
	bool correct_erased_chunks;
	bool clk_ctrl;
};

struct atmel_pmecc {
	struct device *dev;
	const struct atmel_pmecc_caps *caps;

	struct {
		void __iomem *base;
		void __iomem *errloc;
	} regs;

	struct mutex lock;
};

struct atmel_pmecc_user_conf_cache {
	u32 cfg;
	u32 sarea;
	u32 saddr;
	u32 eaddr;
};

struct atmel_pmecc_user {
	struct atmel_pmecc_user_conf_cache cache;
	struct atmel_pmecc *pmecc;
	const struct atmel_pmecc_gf_tables *gf_tables;
	int eccbytes;
	s16 *partial_syn;
	s16 *si;
	s16 *lmu;
	s16 *smu;
	s32 *mu;
	s32 *dmu;
	s32 *delta;
	u32 isr;
};

static DEFINE_MUTEX(pmecc_gf_tables_lock);
static const struct atmel_pmecc_gf_tables *pmecc_gf_tables_512;
static const struct atmel_pmecc_gf_tables *pmecc_gf_tables_1024;

static inline int deg(unsigned int poly)
{
	/* polynomial degree is the most-significant bit index */
	return fls(poly) - 1;
}

static int atmel_pmecc_build_gf_tables(int mm, unsigned int poly,
				       struct atmel_pmecc_gf_tables *gf_tables)
{
	unsigned int i, x = 1;
	const unsigned int k = BIT(deg(poly));
	unsigned int nn = BIT(mm) - 1;

	/* primitive polynomial must be of degree m */
	if (k != (1u << mm))
		return -EINVAL;

	for (i = 0; i < nn; i++) {
		gf_tables->alpha_to[i] = x;
		gf_tables->index_of[x] = i;
		if (i && (x == 1))
			/* polynomial is not primitive (a^i=1 with 0<i<2^m-1) */
			return -EINVAL;
		x <<= 1;
		if (x & k)
			x ^= poly;
	}
	gf_tables->alpha_to[nn] = 1;
	gf_tables->index_of[0] = 0;

	return 0;
}

static const struct atmel_pmecc_gf_tables *
atmel_pmecc_create_gf_tables(const struct atmel_pmecc_user_req *req)
{
	struct atmel_pmecc_gf_tables *gf_tables;
	unsigned int poly, degree, table_size;
	int ret;

	if (req->ecc.sectorsize == 512) {
		degree = PMECC_GF_DIMENSION_13;
		poly = PMECC_GF_13_PRIMITIVE_POLY;
		table_size = PMECC_LOOKUP_TABLE_SIZE_512;
	} else {
		degree = PMECC_GF_DIMENSION_14;
		poly = PMECC_GF_14_PRIMITIVE_POLY;
		table_size = PMECC_LOOKUP_TABLE_SIZE_1024;
	}

	gf_tables = kzalloc(sizeof(*gf_tables) +
			    (2 * table_size * sizeof(u16)),
			    GFP_KERNEL);
	if (!gf_tables)
		return ERR_PTR(-ENOMEM);

	gf_tables->alpha_to = (void *)(gf_tables + 1);
	gf_tables->index_of = gf_tables->alpha_to + table_size;

	ret = atmel_pmecc_build_gf_tables(degree, poly, gf_tables);
	if (ret) {
		kfree(gf_tables);
		return ERR_PTR(ret);
	}

	return gf_tables;
}

static const struct atmel_pmecc_gf_tables *
atmel_pmecc_get_gf_tables(const struct atmel_pmecc_user_req *req)
{
	const struct atmel_pmecc_gf_tables **gf_tables, *ret;

	mutex_lock(&pmecc_gf_tables_lock);
	if (req->ecc.sectorsize == 512)
		gf_tables = &pmecc_gf_tables_512;
	else
		gf_tables = &pmecc_gf_tables_1024;

	ret = *gf_tables;

	if (!ret) {
		ret = atmel_pmecc_create_gf_tables(req);
		if (!IS_ERR(ret))
			*gf_tables = ret;
	}
	mutex_unlock(&pmecc_gf_tables_lock);

	return ret;
}

static int atmel_pmecc_prepare_user_req(struct atmel_pmecc *pmecc,
					struct atmel_pmecc_user_req *req)
{
	int i, max_eccbytes, eccbytes = 0, eccstrength = 0;

	if (req->pagesize <= 0 || req->oobsize <= 0 || req->ecc.bytes <= 0)
		return -EINVAL;

	if (req->ecc.ooboffset >= 0 &&
	    req->ecc.ooboffset + req->ecc.bytes > req->oobsize)
		return -EINVAL;

	if (req->ecc.sectorsize == ATMEL_PMECC_SECTOR_SIZE_AUTO) {
		if (req->ecc.strength != ATMEL_PMECC_MAXIMIZE_ECC_STRENGTH)
			return -EINVAL;

		if (req->pagesize > 512)
			req->ecc.sectorsize = 1024;
		else
			req->ecc.sectorsize = 512;
	}

	if (req->ecc.sectorsize != 512 && req->ecc.sectorsize != 1024)
		return -EINVAL;

	if (req->pagesize % req->ecc.sectorsize)
		return -EINVAL;

	req->ecc.nsectors = req->pagesize / req->ecc.sectorsize;

	max_eccbytes = req->ecc.bytes;

	for (i = 0; i < pmecc->caps->nstrengths; i++) {
		int nbytes, strength = pmecc->caps->strengths[i];

		if (req->ecc.strength != ATMEL_PMECC_MAXIMIZE_ECC_STRENGTH &&
		    strength < req->ecc.strength)
			continue;

		nbytes = DIV_ROUND_UP(strength * fls(8 * req->ecc.sectorsize),
				      8);
		nbytes *= req->ecc.nsectors;

		if (nbytes > max_eccbytes)
			break;

		eccstrength = strength;
		eccbytes = nbytes;

		if (req->ecc.strength != ATMEL_PMECC_MAXIMIZE_ECC_STRENGTH)
			break;
	}

	if (!eccstrength)
		return -EINVAL;

	req->ecc.bytes = eccbytes;
	req->ecc.strength = eccstrength;

	if (req->ecc.ooboffset < 0)
		req->ecc.ooboffset = req->oobsize - eccbytes;

	return 0;
}

struct atmel_pmecc_user *
atmel_pmecc_create_user(struct atmel_pmecc *pmecc,
			struct atmel_pmecc_user_req *req)
{
	struct atmel_pmecc_user *user;
	const struct atmel_pmecc_gf_tables *gf_tables;
	int strength, size, ret;

	ret = atmel_pmecc_prepare_user_req(pmecc, req);
	if (ret)
		return ERR_PTR(ret);

	size = sizeof(*user);
	size = ALIGN(size, sizeof(u16));
	/* Reserve space for partial_syn, si and smu */
	size += ((2 * req->ecc.strength) + 1) * sizeof(u16) *
		(2 + req->ecc.strength + 2);
	/* Reserve space for lmu. */
	size += (req->ecc.strength + 1) * sizeof(u16);
	/* Reserve space for mu, dmu and delta. */
	size = ALIGN(size, sizeof(s32));
	size += (req->ecc.strength + 1) * sizeof(s32) * 3;

	user = devm_kzalloc(pmecc->dev, size, GFP_KERNEL);
	if (!user)
		return ERR_PTR(-ENOMEM);

	user->pmecc = pmecc;

	user->partial_syn = (s16 *)PTR_ALIGN(user + 1, sizeof(u16));
	user->si = user->partial_syn + ((2 * req->ecc.strength) + 1);
	user->lmu = user->si + ((2 * req->ecc.strength) + 1);
	user->smu = user->lmu + (req->ecc.strength + 1);
	user->mu = (s32 *)PTR_ALIGN(user->smu +
				    (((2 * req->ecc.strength) + 1) *
				     (req->ecc.strength + 2)),
				    sizeof(s32));
	user->dmu = user->mu + req->ecc.strength + 1;
	user->delta = user->dmu + req->ecc.strength + 1;

	gf_tables = atmel_pmecc_get_gf_tables(req);
	if (IS_ERR(gf_tables))
		return ERR_CAST(gf_tables);

	user->gf_tables = gf_tables;

	user->eccbytes = req->ecc.bytes / req->ecc.nsectors;

	for (strength = 0; strength < pmecc->caps->nstrengths; strength++) {
		if (pmecc->caps->strengths[strength] == req->ecc.strength)
			break;
	}

	user->cache.cfg = PMECC_CFG_BCH_STRENGTH(strength) |
			  PMECC_CFG_NSECTORS(req->ecc.nsectors);

	if (req->ecc.sectorsize == 1024)
		user->cache.cfg |= PMECC_CFG_SECTOR1024;

	user->cache.sarea = req->oobsize - 1;
	user->cache.saddr = req->ecc.ooboffset;
	user->cache.eaddr = req->ecc.ooboffset + req->ecc.bytes - 1;

	return user;
}
EXPORT_SYMBOL_GPL(atmel_pmecc_create_user);

static int get_strength(struct atmel_pmecc_user *user)
{
	const int *strengths = user->pmecc->caps->strengths;

	return strengths[user->cache.cfg & PMECC_CFG_BCH_STRENGTH_MASK];
}

static int get_sectorsize(struct atmel_pmecc_user *user)
{
	return user->cache.cfg & PMECC_CFG_SECTOR1024 ? 1024 : 512;
}

static void atmel_pmecc_gen_syndrome(struct atmel_pmecc_user *user, int sector)
{
	int strength = get_strength(user);
	u32 value;
	int i;

	/* Fill odd syndromes */
	for (i = 0; i < strength; i++) {
		value = readl_relaxed(user->pmecc->regs.base +
				      ATMEL_PMECC_REM(sector, i / 2));
		if (i & 1)
			value >>= 16;

		user->partial_syn[(2 * i) + 1] = value;
	}
}

static void atmel_pmecc_substitute(struct atmel_pmecc_user *user)
{
	int degree = get_sectorsize(user) == 512 ? 13 : 14;
	int cw_len = BIT(degree) - 1;
	int strength = get_strength(user);
	s16 *alpha_to = user->gf_tables->alpha_to;
	s16 *index_of = user->gf_tables->index_of;
	s16 *partial_syn = user->partial_syn;
	s16 *si;
	int i, j;

	/*
	 * si[] is a table that holds the current syndrome value,
	 * an element of that table belongs to the field
	 */
	si = user->si;

	memset(&si[1], 0, sizeof(s16) * ((2 * strength) - 1));

	/* Computation 2t syndromes based on S(x) */
	/* Odd syndromes */
	for (i = 1; i < 2 * strength; i += 2) {
		for (j = 0; j < degree; j++) {
			if (partial_syn[i] & BIT(j))
				si[i] = alpha_to[i * j] ^ si[i];
		}
	}
	/* Even syndrome = (Odd syndrome) ** 2 */
	for (i = 2, j = 1; j <= strength; i = ++j << 1) {
		if (si[j] == 0) {
			si[i] = 0;
		} else {
			s16 tmp;

			tmp = index_of[si[j]];
			tmp = (tmp * 2) % cw_len;
			si[i] = alpha_to[tmp];
		}
	}
}

static void atmel_pmecc_get_sigma(struct atmel_pmecc_user *user)
{
	s16 *lmu = user->lmu;
	s16 *si = user->si;
	s32 *mu = user->mu;
	s32 *dmu = user->dmu;
	s32 *delta = user->delta;
	int degree = get_sectorsize(user) == 512 ? 13 : 14;
	int cw_len = BIT(degree) - 1;
	int strength = get_strength(user);
	int num = 2 * strength + 1;
	s16 *index_of = user->gf_tables->index_of;
	s16 *alpha_to = user->gf_tables->alpha_to;
	int i, j, k;
	u32 dmu_0_count, tmp;
	s16 *smu = user->smu;

	/* index of largest delta */
	int ro;
	int largest;
	int diff;

	dmu_0_count = 0;

	/* First Row */

	/* Mu */
	mu[0] = -1;

	memset(smu, 0, sizeof(s16) * num);
	smu[0] = 1;

	/* discrepancy set to 1 */
	dmu[0] = 1;
	/* polynom order set to 0 */
	lmu[0] = 0;
	delta[0] = (mu[0] * 2 - lmu[0]) >> 1;

	/* Second Row */

	/* Mu */
	mu[1] = 0;
	/* Sigma(x) set to 1 */
	memset(&smu[num], 0, sizeof(s16) * num);
	smu[num] = 1;

	/* discrepancy set to S1 */
	dmu[1] = si[1];

	/* polynom order set to 0 */
	lmu[1] = 0;

	delta[1] = (mu[1] * 2 - lmu[1]) >> 1;

	/* Init the Sigma(x) last row */
	memset(&smu[(strength + 1) * num], 0, sizeof(s16) * num);

	for (i = 1; i <= strength; i++) {
		mu[i + 1] = i << 1;
		/* Begin Computing Sigma (Mu+1) and L(mu) */
		/* check if discrepancy is set to 0 */
		if (dmu[i] == 0) {
			dmu_0_count++;

			tmp = ((strength - (lmu[i] >> 1) - 1) / 2);
			if ((strength - (lmu[i] >> 1) - 1) & 0x1)
				tmp += 2;
			else
				tmp += 1;

			if (dmu_0_count == tmp) {
				for (j = 0; j <= (lmu[i] >> 1) + 1; j++)
					smu[(strength + 1) * num + j] =
							smu[i * num + j];

				lmu[strength + 1] = lmu[i];
				return;
			}

			/* copy polynom */
			for (j = 0; j <= lmu[i] >> 1; j++)
				smu[(i + 1) * num + j] = smu[i * num + j];

			/* copy previous polynom order to the next */
			lmu[i + 1] = lmu[i];
		} else {
			ro = 0;
			largest = -1;
			/* find largest delta with dmu != 0 */
			for (j = 0; j < i; j++) {
				if ((dmu[j]) && (delta[j] > largest)) {
					largest = delta[j];
					ro = j;
				}
			}

			/* compute difference */
			diff = (mu[i] - mu[ro]);

			/* Compute degree of the new smu polynomial */
			if ((lmu[i] >> 1) > ((lmu[ro] >> 1) + diff))
				lmu[i + 1] = lmu[i];
			else
				lmu[i + 1] = ((lmu[ro] >> 1) + diff) * 2;

			/* Init smu[i+1] with 0 */
			for (k = 0; k < num; k++)
				smu[(i + 1) * num + k] = 0;

			/* Compute smu[i+1] */
			for (k = 0; k <= lmu[ro] >> 1; k++) {
				s16 a, b, c;

				if (!(smu[ro * num + k] && dmu[i]))
					continue;

				a = index_of[dmu[i]];
				b = index_of[dmu[ro]];
				c = index_of[smu[ro * num + k]];
				tmp = a + (cw_len - b) + c;
				a = alpha_to[tmp % cw_len];
				smu[(i + 1) * num + (k + diff)] = a;
			}

			for (k = 0; k <= lmu[i] >> 1; k++)
				smu[(i + 1) * num + k] ^= smu[i * num + k];
		}

		/* End Computing Sigma (Mu+1) and L(mu) */
		/* In either case compute delta */
		delta[i + 1] = (mu[i + 1] * 2 - lmu[i + 1]) >> 1;

		/* Do not compute discrepancy for the last iteration */
		if (i >= strength)
			continue;

		for (k = 0; k <= (lmu[i + 1] >> 1); k++) {
			tmp = 2 * (i - 1);
			if (k == 0) {
				dmu[i + 1] = si[tmp + 3];
			} else if (smu[(i + 1) * num + k] && si[tmp + 3 - k]) {
				s16 a, b, c;

				a = index_of[smu[(i + 1) * num + k]];
				b = si[2 * (i - 1) + 3 - k];
				c = index_of[b];
				tmp = a + c;
				tmp %= cw_len;
				dmu[i + 1] = alpha_to[tmp] ^ dmu[i + 1];
			}
		}
	}
}

static int atmel_pmecc_err_location(struct atmel_pmecc_user *user)
{
	int sector_size = get_sectorsize(user);
	int degree = sector_size == 512 ? 13 : 14;
	struct atmel_pmecc *pmecc = user->pmecc;
	int strength = get_strength(user);
	int ret, roots_nbr, i, err_nbr = 0;
	int num = (2 * strength) + 1;
	s16 *smu = user->smu;
	u32 val;

	writel(PMERRLOC_DISABLE, pmecc->regs.errloc + ATMEL_PMERRLOC_ELDIS);

	for (i = 0; i <= user->lmu[strength + 1] >> 1; i++) {
		writel_relaxed(smu[(strength + 1) * num + i],
			       pmecc->regs.errloc + ATMEL_PMERRLOC_SIGMA(i));
		err_nbr++;
	}

	val = (err_nbr - 1) << 16;
	if (sector_size == 1024)
		val |= 1;

	writel(val, pmecc->regs.errloc + ATMEL_PMERRLOC_ELCFG);
	writel((sector_size * 8) + (degree * strength),
	       pmecc->regs.errloc + ATMEL_PMERRLOC_ELEN);

	ret = readl_relaxed_poll_timeout(pmecc->regs.errloc +
					 ATMEL_PMERRLOC_ELISR,
					 val, val & PMERRLOC_CALC_DONE, 0,
					 PMECC_MAX_TIMEOUT_MS * 1000);
	if (ret) {
		dev_err(pmecc->dev,
			"PMECC: Timeout to calculate error location.\n");
		return ret;
	}

	roots_nbr = (val & PMERRLOC_ERR_NUM_MASK) >> 8;
	/* Number of roots == degree of smu hence <= cap */
	if (roots_nbr == user->lmu[strength + 1] >> 1)
		return err_nbr - 1;

	/*
	 * Number of roots does not match the degree of smu
	 * unable to correct error.
	 */
	return -EBADMSG;
}

int atmel_pmecc_correct_sector(struct atmel_pmecc_user *user, int sector,
			       void *data, void *ecc)
{
	struct atmel_pmecc *pmecc = user->pmecc;
	int sectorsize = get_sectorsize(user);
	int eccbytes = user->eccbytes;
	int i, nerrors;

	if (!(user->isr & BIT(sector)))
		return 0;

	atmel_pmecc_gen_syndrome(user, sector);
	atmel_pmecc_substitute(user);
	atmel_pmecc_get_sigma(user);

	nerrors = atmel_pmecc_err_location(user);
	if (nerrors < 0)
		return nerrors;

	for (i = 0; i < nerrors; i++) {
		const char *area;
		int byte, bit;
		u32 errpos;
		u8 *ptr;

		errpos = readl_relaxed(pmecc->regs.errloc +
				ATMEL_PMERRLOC_EL(pmecc->caps->el_offset, i));
		errpos--;

		byte = errpos / 8;
		bit = errpos % 8;

		if (byte < sectorsize) {
			ptr = data + byte;
			area = "data";
		} else if (byte < sectorsize + eccbytes) {
			ptr = ecc + byte - sectorsize;
			area = "ECC";
		} else {
			dev_dbg(pmecc->dev,
				"Invalid errpos value (%d, max is %d)\n",
				errpos, (sectorsize + eccbytes) * 8);
			return -EINVAL;
		}

		dev_dbg(pmecc->dev,
			"Bit flip in %s area, byte %d: 0x%02x -> 0x%02x\n",
			area, byte, *ptr, (unsigned int)(*ptr ^ BIT(bit)));

		*ptr ^= BIT(bit);
	}

	return nerrors;
}
EXPORT_SYMBOL_GPL(atmel_pmecc_correct_sector);

bool atmel_pmecc_correct_erased_chunks(struct atmel_pmecc_user *user)
{
	return user->pmecc->caps->correct_erased_chunks;
}
EXPORT_SYMBOL_GPL(atmel_pmecc_correct_erased_chunks);

void atmel_pmecc_get_generated_eccbytes(struct atmel_pmecc_user *user,
					int sector, void *ecc)
{
	struct atmel_pmecc *pmecc = user->pmecc;
	u8 *ptr = ecc;
	int i;

	for (i = 0; i < user->eccbytes; i++)
		ptr[i] = readb_relaxed(pmecc->regs.base +
				       ATMEL_PMECC_ECC(sector, i));
}
EXPORT_SYMBOL_GPL(atmel_pmecc_get_generated_eccbytes);

void atmel_pmecc_reset(struct atmel_pmecc *pmecc)
{
	writel(PMECC_CTRL_RST, pmecc->regs.base + ATMEL_PMECC_CTRL);
	writel(PMECC_CTRL_DISABLE, pmecc->regs.base + ATMEL_PMECC_CTRL);
}
EXPORT_SYMBOL_GPL(atmel_pmecc_reset);

int atmel_pmecc_enable(struct atmel_pmecc_user *user, int op)
{
	struct atmel_pmecc *pmecc = user->pmecc;
	u32 cfg;

	if (op != NAND_ECC_READ && op != NAND_ECC_WRITE) {
		dev_err(pmecc->dev, "Bad ECC operation!");
		return -EINVAL;
	}

	mutex_lock(&user->pmecc->lock);

	cfg = user->cache.cfg;
	if (op == NAND_ECC_WRITE)
		cfg |= PMECC_CFG_WRITE_OP;
	else
		cfg |= PMECC_CFG_AUTO_ENABLE;

	writel(cfg, pmecc->regs.base + ATMEL_PMECC_CFG);
	writel(user->cache.sarea, pmecc->regs.base + ATMEL_PMECC_SAREA);
	writel(user->cache.saddr, pmecc->regs.base + ATMEL_PMECC_SADDR);
	writel(user->cache.eaddr, pmecc->regs.base + ATMEL_PMECC_EADDR);

	writel(PMECC_CTRL_ENABLE, pmecc->regs.base + ATMEL_PMECC_CTRL);
	writel(PMECC_CTRL_DATA, pmecc->regs.base + ATMEL_PMECC_CTRL);

	return 0;
}
EXPORT_SYMBOL_GPL(atmel_pmecc_enable);

void atmel_pmecc_disable(struct atmel_pmecc_user *user)
{
	atmel_pmecc_reset(user->pmecc);
	mutex_unlock(&user->pmecc->lock);
}
EXPORT_SYMBOL_GPL(atmel_pmecc_disable);

int atmel_pmecc_wait_rdy(struct atmel_pmecc_user *user)
{
	struct atmel_pmecc *pmecc = user->pmecc;
	u32 status;
	int ret;

	ret = readl_relaxed_poll_timeout(pmecc->regs.base +
					 ATMEL_PMECC_SR,
					 status, !(status & PMECC_SR_BUSY), 0,
					 PMECC_MAX_TIMEOUT_MS * 1000);
	if (ret) {
		dev_err(pmecc->dev,
			"Timeout while waiting for PMECC ready.\n");
		return ret;
	}

	user->isr = readl_relaxed(pmecc->regs.base + ATMEL_PMECC_ISR);

	return 0;
}
EXPORT_SYMBOL_GPL(atmel_pmecc_wait_rdy);

static struct atmel_pmecc *atmel_pmecc_create(struct platform_device *pdev,
					const struct atmel_pmecc_caps *caps,
					int pmecc_res_idx, int errloc_res_idx)
{
	struct device *dev = &pdev->dev;
	struct atmel_pmecc *pmecc;

	pmecc = devm_kzalloc(dev, sizeof(*pmecc), GFP_KERNEL);
	if (!pmecc)
		return ERR_PTR(-ENOMEM);

	pmecc->caps = caps;
	pmecc->dev = dev;
	mutex_init(&pmecc->lock);

	pmecc->regs.base = devm_platform_ioremap_resource(pdev, pmecc_res_idx);
	if (IS_ERR(pmecc->regs.base))
		return ERR_CAST(pmecc->regs.base);

	pmecc->regs.errloc = devm_platform_ioremap_resource(pdev, errloc_res_idx);
	if (IS_ERR(pmecc->regs.errloc))
		return ERR_CAST(pmecc->regs.errloc);

	/* pmecc data setup time */
	if (caps->clk_ctrl)
		writel(PMECC_CLK_133MHZ, pmecc->regs.base + ATMEL_PMECC_CLK);

	/* Disable all interrupts before registering the PMECC handler. */
	writel(0xffffffff, pmecc->regs.base + ATMEL_PMECC_IDR);
	atmel_pmecc_reset(pmecc);

	return pmecc;
}

static void devm_atmel_pmecc_put(struct device *dev, void *res)
{
	struct atmel_pmecc **pmecc = res;

	put_device((*pmecc)->dev);
}

static struct atmel_pmecc *atmel_pmecc_get_by_node(struct device *userdev,
						   struct device_node *np)
{
	struct platform_device *pdev;
	struct atmel_pmecc *pmecc, **ptr;
	int ret;

	pdev = of_find_device_by_node(np);
	if (!pdev)
		return ERR_PTR(-EPROBE_DEFER);
	pmecc = platform_get_drvdata(pdev);
	if (!pmecc) {
		ret = -EPROBE_DEFER;
		goto err_put_device;
	}

	ptr = devres_alloc(devm_atmel_pmecc_put, sizeof(*ptr), GFP_KERNEL);
	if (!ptr) {
		ret = -ENOMEM;
		goto err_put_device;
	}

	*ptr = pmecc;

	devres_add(userdev, ptr);

	return pmecc;

err_put_device:
	put_device(&pdev->dev);
	return ERR_PTR(ret);
}

static const int atmel_pmecc_strengths[] = { 2, 4, 8, 12, 24, 32 };

static struct atmel_pmecc_caps at91sam9g45_caps = {
	.strengths = atmel_pmecc_strengths,
	.nstrengths = 5,
	.el_offset = 0x8c,
	.clk_ctrl = true,
};

static struct atmel_pmecc_caps sama5d4_caps = {
	.strengths = atmel_pmecc_strengths,
	.nstrengths = 5,
	.el_offset = 0x8c,
	.correct_erased_chunks = true,
};

static struct atmel_pmecc_caps sama5d2_caps = {
	.strengths = atmel_pmecc_strengths,
	.nstrengths = 6,
	.el_offset = 0xac,
	.correct_erased_chunks = true,
};

static const struct of_device_id __maybe_unused atmel_pmecc_legacy_match[] = {
	{ .compatible = "atmel,sama5d4-nand", &sama5d4_caps },
	{ .compatible = "atmel,sama5d2-nand", &sama5d2_caps },
	{ /* sentinel */ }
};

struct atmel_pmecc *devm_atmel_pmecc_get(struct device *userdev)
{
	struct atmel_pmecc *pmecc;
	struct device_node *np;

	if (!userdev)
		return ERR_PTR(-EINVAL);

	if (!userdev->of_node)
		return NULL;

	np = of_parse_phandle(userdev->of_node, "ecc-engine", 0);
	if (np) {
		pmecc = atmel_pmecc_get_by_node(userdev, np);
		of_node_put(np);
	} else {
		/*
		 * Support old DT bindings: in this case the PMECC iomem
		 * resources are directly defined in the user pdev at position
		 * 1 and 2. Extract all relevant information from there.
		 */
		struct platform_device *pdev = to_platform_device(userdev);
		const struct atmel_pmecc_caps *caps;
		const struct of_device_id *match;

		/* No PMECC engine available. */
		if (!of_property_read_bool(userdev->of_node,
					   "atmel,has-pmecc"))
			return NULL;

		caps = &at91sam9g45_caps;

		/* Find the caps associated to the NAND dev node. */
		match = of_match_node(atmel_pmecc_legacy_match,
				      userdev->of_node);
		if (match && match->data)
			caps = match->data;

		pmecc = atmel_pmecc_create(pdev, caps, 1, 2);
	}

	return pmecc;
}
EXPORT_SYMBOL(devm_atmel_pmecc_get);

static const struct of_device_id atmel_pmecc_match[] = {
	{ .compatible = "atmel,at91sam9g45-pmecc", &at91sam9g45_caps },
	{ .compatible = "atmel,sama5d4-pmecc", &sama5d4_caps },
	{ .compatible = "atmel,sama5d2-pmecc", &sama5d2_caps },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, atmel_pmecc_match);

static int atmel_pmecc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct atmel_pmecc_caps *caps;
	struct atmel_pmecc *pmecc;

	caps = of_device_get_match_data(&pdev->dev);
	if (!caps) {
		dev_err(dev, "Invalid caps\n");
		return -EINVAL;
	}

	pmecc = atmel_pmecc_create(pdev, caps, 0, 1);
	if (IS_ERR(pmecc))
		return PTR_ERR(pmecc);

	platform_set_drvdata(pdev, pmecc);

	return 0;
}

static struct platform_driver atmel_pmecc_driver = {
	.driver = {
		.name = "atmel-pmecc",
		.of_match_table = atmel_pmecc_match,
	},
	.probe = atmel_pmecc_probe,
};
module_platform_driver(atmel_pmecc_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Boris Brezillon <boris.brezillon@free-electrons.com>");
MODULE_DESCRIPTION("PMECC engine driver");
MODULE_ALIAS("platform:atmel_pmecc");
