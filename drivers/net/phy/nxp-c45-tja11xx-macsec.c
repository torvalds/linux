// SPDX-License-Identifier: GPL-2.0
/* NXP C45 PTP PHY driver interface
 * Copyright 2023 NXP
 * Author: Radu Pirea <radu-nicolae.pirea@oss.nxp.com>
 */

#include <linux/delay.h>
#include <linux/ethtool_netlink.h>
#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/processor.h>
#include <net/dst_metadata.h>
#include <net/macsec.h>

#include "nxp-c45-tja11xx.h"

#define MACSEC_REG_SIZE			32
#define TX_SC_MAX			4

#define TX_SC_BIT(secy_id)		BIT(MACSEC_REG_SIZE - (secy_id) - 1)

#define VEND1_MACSEC_BASE		0x9000

#define MACSEC_CFG			0x0000
#define MACSEC_CFG_BYPASS		BIT(1)
#define MACSEC_CFG_S0I			BIT(0)

#define MACSEC_TPNET			0x0044
#define PN_WRAP_THRESHOLD		0xffffffff

#define MACSEC_RXSCA			0x0080
#define MACSEC_RXSCKA			0x0084

#define MACSEC_TXSCA			0x00C0
#define MACSEC_TXSCKA			0x00C4

#define MACSEC_RXSC_SCI_1H		0x0100

#define MACSEC_RXSC_CFG			0x0128
#define MACSEC_RXSC_CFG_XPN		BIT(25)
#define MACSEC_RXSC_CFG_AES_256		BIT(24)
#define MACSEC_RXSC_CFG_SCI_EN		BIT(11)
#define MACSEC_RXSC_CFG_RP		BIT(10)
#define MACSEC_RXSC_CFG_VF_MASK		GENMASK(9, 8)
#define MACSEC_RXSC_CFG_VF_OFF		8

#define MACSEC_RPW			0x012C

#define MACSEC_RXSA_A_CS		0x0180
#define MACSEC_RXSA_A_NPN		0x0184
#define MACSEC_RXSA_A_XNPN		0x0188
#define MACSEC_RXSA_A_LNPN		0x018C
#define MACSEC_RXSA_A_LXNPN		0x0190

#define MACSEC_RXSA_B_CS		0x01C0
#define MACSEC_RXSA_B_NPN		0x01C4
#define MACSEC_RXSA_B_XNPN		0x01C8
#define MACSEC_RXSA_B_LNPN		0x01CC
#define MACSEC_RXSA_B_LXNPN		0x01D0

#define MACSEC_RXSA_CS_AN_OFF		1
#define MACSEC_RXSA_CS_EN		BIT(0)

#define MACSEC_TXSC_SCI_1H		0x0200
#define MACSEC_TXSC_CFG			0x0228
#define MACSEC_TXSC_CFG_XPN		BIT(25)
#define MACSEC_TXSC_CFG_AES_256		BIT(24)
#define MACSEC_TXSC_CFG_AN_MASK		GENMASK(19, 18)
#define MACSEC_TXSC_CFG_AN_OFF		18
#define MACSEC_TXSC_CFG_ASA		BIT(17)
#define MACSEC_TXSC_CFG_SCE		BIT(16)
#define MACSEC_TXSC_CFG_ENCRYPT		BIT(4)
#define MACSEC_TXSC_CFG_PROTECT		BIT(3)
#define MACSEC_TXSC_CFG_SEND_SCI	BIT(2)
#define MACSEC_TXSC_CFG_END_STATION	BIT(1)
#define MACSEC_TXSC_CFG_SCB		BIT(0)

#define MACSEC_TXSA_A_CS		0x0280
#define MACSEC_TXSA_A_NPN		0x0284
#define MACSEC_TXSA_A_XNPN		0x0288

#define MACSEC_TXSA_B_CS		0x02C0
#define MACSEC_TXSA_B_NPN		0x02C4
#define MACSEC_TXSA_B_XNPN		0x02C8

#define MACSEC_SA_CS_A			BIT(31)

#define MACSEC_EVR			0x0400
#define MACSEC_EVER			0x0404

#define MACSEC_RXSA_A_KA		0x0700
#define MACSEC_RXSA_A_SSCI		0x0720
#define MACSEC_RXSA_A_SALT		0x0724

#define MACSEC_RXSA_B_KA		0x0740
#define MACSEC_RXSA_B_SSCI		0x0760
#define MACSEC_RXSA_B_SALT		0x0764

#define MACSEC_TXSA_A_KA		0x0780
#define MACSEC_TXSA_A_SSCI		0x07A0
#define MACSEC_TXSA_A_SALT		0x07A4

#define MACSEC_TXSA_B_KA		0x07C0
#define MACSEC_TXSA_B_SSCI		0x07E0
#define MACSEC_TXSA_B_SALT		0x07E4

#define MACSEC_UPFR0D2			0x0A08
#define MACSEC_UPFR0M1			0x0A10
#define MACSEC_OVP			BIT(12)

#define	MACSEC_UPFR0M2			0x0A14
#define ETYPE_MASK			0xffff

#define MACSEC_UPFR0R			0x0A18
#define MACSEC_UPFR_EN			BIT(0)

#define ADPTR_CNTRL			0x0F00
#define ADPTR_CNTRL_CONFIG_EN		BIT(14)
#define ADPTR_CNTRL_ADPTR_EN		BIT(12)
#define ADPTR_TX_TAG_CNTRL		0x0F0C
#define ADPTR_TX_TAG_CNTRL_ENA		BIT(31)

#define TX_SC_FLT_BASE			0x800
#define TX_SC_FLT_SIZE			0x10
#define TX_FLT_BASE(flt_id)		(TX_SC_FLT_BASE + \
	TX_SC_FLT_SIZE * (flt_id))

#define TX_SC_FLT_OFF_MAC_DA_SA		0x04
#define TX_SC_FLT_OFF_MAC_SA		0x08
#define TX_SC_FLT_OFF_MAC_CFG		0x0C
#define TX_SC_FLT_BY_SA			BIT(14)
#define TX_SC_FLT_EN			BIT(8)

#define TX_SC_FLT_MAC_DA_SA(base)	((base) + TX_SC_FLT_OFF_MAC_DA_SA)
#define TX_SC_FLT_MAC_SA(base)		((base) + TX_SC_FLT_OFF_MAC_SA)
#define TX_SC_FLT_MAC_CFG(base)		((base) + TX_SC_FLT_OFF_MAC_CFG)

#define ADAPTER_EN	BIT(6)
#define MACSEC_EN	BIT(5)

#define MACSEC_INOV1HS			0x0140
#define MACSEC_INOV2HS			0x0144
#define MACSEC_INOD1HS			0x0148
#define MACSEC_INOD2HS			0x014C
#define MACSEC_RXSCIPUS			0x0150
#define MACSEC_RXSCIPDS			0x0154
#define MACSEC_RXSCIPLS			0x0158
#define MACSEC_RXAN0INUSS		0x0160
#define MACSEC_RXAN0IPUSS		0x0170
#define MACSEC_RXSA_A_IPOS		0x0194
#define MACSEC_RXSA_A_IPIS		0x01B0
#define MACSEC_RXSA_A_IPNVS		0x01B4
#define MACSEC_RXSA_B_IPOS		0x01D4
#define MACSEC_RXSA_B_IPIS		0x01F0
#define MACSEC_RXSA_B_IPNVS		0x01F4
#define MACSEC_OPUS			0x021C
#define MACSEC_OPTLS			0x022C
#define MACSEC_OOP1HS			0x0240
#define MACSEC_OOP2HS			0x0244
#define MACSEC_OOE1HS			0x0248
#define MACSEC_OOE2HS			0x024C
#define MACSEC_TXSA_A_OPPS		0x028C
#define MACSEC_TXSA_A_OPES		0x0290
#define MACSEC_TXSA_B_OPPS		0x02CC
#define MACSEC_TXSA_B_OPES		0x02D0
#define MACSEC_INPWTS			0x0630
#define MACSEC_INPBTS			0x0638
#define MACSEC_IPSNFS			0x063C

#define TJA11XX_TLV_TX_NEEDED_HEADROOM	(32)
#define TJA11XX_TLV_NEEDED_TAILROOM	(0)

#define ETH_P_TJA11XX_TLV		(0x4e58)

enum nxp_c45_sa_type {
	TX_SA,
	RX_SA,
};

struct nxp_c45_sa {
	void *sa;
	const struct nxp_c45_sa_regs *regs;
	enum nxp_c45_sa_type type;
	bool is_key_a;
	u8 an;
	struct list_head list;
};

struct nxp_c45_secy {
	struct macsec_secy *secy;
	struct macsec_rx_sc *rx_sc;
	struct list_head sa_list;
	int secy_id;
	bool rx_sc0_impl;
	struct list_head list;
};

struct nxp_c45_macsec {
	struct list_head secy_list;
	DECLARE_BITMAP(secy_bitmap, TX_SC_MAX);
	DECLARE_BITMAP(tx_sc_bitmap, TX_SC_MAX);
};

struct nxp_c45_sa_regs {
	u16 cs;
	u16 npn;
	u16 xnpn;
	u16 lnpn;
	u16 lxnpn;
	u16 ka;
	u16 ssci;
	u16 salt;
	u16 ipis;
	u16 ipnvs;
	u16 ipos;
	u16 opps;
	u16 opes;
};

static const struct nxp_c45_sa_regs rx_sa_a_regs = {
	.cs	= MACSEC_RXSA_A_CS,
	.npn	= MACSEC_RXSA_A_NPN,
	.xnpn	= MACSEC_RXSA_A_XNPN,
	.lnpn	= MACSEC_RXSA_A_LNPN,
	.lxnpn	= MACSEC_RXSA_A_LXNPN,
	.ka	= MACSEC_RXSA_A_KA,
	.ssci	= MACSEC_RXSA_A_SSCI,
	.salt	= MACSEC_RXSA_A_SALT,
	.ipis	= MACSEC_RXSA_A_IPIS,
	.ipnvs	= MACSEC_RXSA_A_IPNVS,
	.ipos	= MACSEC_RXSA_A_IPOS,
};

static const struct nxp_c45_sa_regs rx_sa_b_regs = {
	.cs	= MACSEC_RXSA_B_CS,
	.npn	= MACSEC_RXSA_B_NPN,
	.xnpn	= MACSEC_RXSA_B_XNPN,
	.lnpn	= MACSEC_RXSA_B_LNPN,
	.lxnpn	= MACSEC_RXSA_B_LXNPN,
	.ka	= MACSEC_RXSA_B_KA,
	.ssci	= MACSEC_RXSA_B_SSCI,
	.salt	= MACSEC_RXSA_B_SALT,
	.ipis	= MACSEC_RXSA_B_IPIS,
	.ipnvs	= MACSEC_RXSA_B_IPNVS,
	.ipos	= MACSEC_RXSA_B_IPOS,
};

static const struct nxp_c45_sa_regs tx_sa_a_regs = {
	.cs	= MACSEC_TXSA_A_CS,
	.npn	= MACSEC_TXSA_A_NPN,
	.xnpn	= MACSEC_TXSA_A_XNPN,
	.ka	= MACSEC_TXSA_A_KA,
	.ssci	= MACSEC_TXSA_A_SSCI,
	.salt	= MACSEC_TXSA_A_SALT,
	.opps	= MACSEC_TXSA_A_OPPS,
	.opes	= MACSEC_TXSA_A_OPES,
};

static const struct nxp_c45_sa_regs tx_sa_b_regs = {
	.cs	= MACSEC_TXSA_B_CS,
	.npn	= MACSEC_TXSA_B_NPN,
	.xnpn	= MACSEC_TXSA_B_XNPN,
	.ka	= MACSEC_TXSA_B_KA,
	.ssci	= MACSEC_TXSA_B_SSCI,
	.salt	= MACSEC_TXSA_B_SALT,
	.opps	= MACSEC_TXSA_B_OPPS,
	.opes	= MACSEC_TXSA_B_OPES,
};

static const
struct nxp_c45_sa_regs *nxp_c45_sa_regs_get(enum nxp_c45_sa_type sa_type,
					    bool key_a)
{
	if (sa_type == RX_SA)
		if (key_a)
			return &rx_sa_a_regs;
		else
			return &rx_sa_b_regs;
	else if (sa_type == TX_SA)
		if (key_a)
			return &tx_sa_a_regs;
		else
			return &tx_sa_b_regs;
	else
		return NULL;
}

static int nxp_c45_macsec_write(struct phy_device *phydev, u16 addr, u32 value)
{
	u32 lvalue = value;
	u16 laddr;
	int ret;

	WARN_ON_ONCE(addr % 4);

	phydev_dbg(phydev, "write addr 0x%x value 0x%x\n", addr, value);

	laddr = VEND1_MACSEC_BASE + addr / 2;
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, laddr, lvalue);
	if (ret)
		return ret;

	laddr += 1;
	lvalue >>= 16;
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, laddr, lvalue);

	return ret;
}

static int nxp_c45_macsec_read(struct phy_device *phydev, u16 addr, u32 *value)
{
	u32 lvalue;
	u16 laddr;
	int ret;

	WARN_ON_ONCE(addr % 4);

	laddr = VEND1_MACSEC_BASE + addr / 2;
	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, laddr);
	if (ret < 0)
		return ret;

	laddr += 1;
	lvalue = (u32)ret & 0xffff;
	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, laddr);
	if (ret < 0)
		return ret;

	lvalue |= (u32)ret << 16;
	*value = lvalue;

	phydev_dbg(phydev, "read addr 0x%x value 0x%x\n", addr, *value);

	return 0;
}

static void nxp_c45_macsec_read32_64(struct phy_device *phydev, u16 addr,
				     u64 *value)
{
	u32 lvalue;

	nxp_c45_macsec_read(phydev, addr, &lvalue);
	*value = lvalue;
}

static void nxp_c45_macsec_read64(struct phy_device *phydev, u16 addr,
				  u64 *value)
{
	u32 lvalue;

	nxp_c45_macsec_read(phydev, addr, &lvalue);
	*value = (u64)lvalue << 32;
	nxp_c45_macsec_read(phydev, addr + 4, &lvalue);
	*value |= lvalue;
}

static void nxp_c45_secy_irq_en(struct phy_device *phydev,
				struct nxp_c45_secy *phy_secy, bool en)
{
	u32 reg;

	nxp_c45_macsec_read(phydev, MACSEC_EVER, &reg);
	if (en)
		reg |= TX_SC_BIT(phy_secy->secy_id);
	else
		reg &= ~TX_SC_BIT(phy_secy->secy_id);
	nxp_c45_macsec_write(phydev, MACSEC_EVER, reg);
}

static struct nxp_c45_secy *nxp_c45_find_secy(struct list_head *secy_list,
					      sci_t sci)
{
	struct nxp_c45_secy *pos, *tmp;

	list_for_each_entry_safe(pos, tmp, secy_list, list)
		if (pos->secy->sci == sci)
			return pos;

	return ERR_PTR(-EINVAL);
}

static struct
nxp_c45_secy *nxp_c45_find_secy_by_id(struct list_head *secy_list,
				      int id)
{
	struct nxp_c45_secy *pos, *tmp;

	list_for_each_entry_safe(pos, tmp, secy_list, list)
		if (pos->secy_id == id)
			return pos;

	return ERR_PTR(-EINVAL);
}

static void nxp_c45_secy_free(struct nxp_c45_secy *phy_secy)
{
	list_del(&phy_secy->list);
	kfree(phy_secy);
}

static struct nxp_c45_sa *nxp_c45_find_sa(struct list_head *sa_list,
					  enum nxp_c45_sa_type sa_type, u8 an)
{
	struct nxp_c45_sa *pos, *tmp;

	list_for_each_entry_safe(pos, tmp, sa_list, list)
		if (pos->an == an && pos->type == sa_type)
			return pos;

	return ERR_PTR(-EINVAL);
}

static struct nxp_c45_sa *nxp_c45_sa_alloc(struct list_head *sa_list, void *sa,
					   enum nxp_c45_sa_type sa_type, u8 an)
{
	struct nxp_c45_sa *first = NULL, *pos, *tmp;
	int occurrences = 0;

	list_for_each_entry_safe(pos, tmp, sa_list, list) {
		if (pos->type != sa_type)
			continue;

		if (pos->an == an)
			return ERR_PTR(-EINVAL);

		first = pos;
		occurrences++;
		if (occurrences >= 2)
			return ERR_PTR(-ENOSPC);
	}

	tmp = kzalloc(sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return ERR_PTR(-ENOMEM);

	if (first)
		tmp->is_key_a = !first->is_key_a;
	else
		tmp->is_key_a = true;

	tmp->sa = sa;
	tmp->type = sa_type;
	tmp->an = an;
	tmp->regs = nxp_c45_sa_regs_get(tmp->type, tmp->is_key_a);
	list_add_tail(&tmp->list, sa_list);

	return tmp;
}

static void nxp_c45_sa_free(struct nxp_c45_sa *sa)
{
	list_del(&sa->list);
	kfree(sa);
}

static void nxp_c45_sa_list_free(struct list_head *sa_list)
{
	struct nxp_c45_sa *pos, *tmp;

	list_for_each_entry_safe(pos, tmp, sa_list, list)
		nxp_c45_sa_free(pos);
}

static void nxp_c45_sa_set_pn(struct phy_device *phydev,
			      struct nxp_c45_sa *sa, u64 pn,
			      u32 replay_window)
{
	const struct nxp_c45_sa_regs *sa_regs = sa->regs;
	pn_t npn = {.full64 = pn};
	pn_t lnpn;

	nxp_c45_macsec_write(phydev, sa_regs->npn, npn.lower);
	nxp_c45_macsec_write(phydev, sa_regs->xnpn, npn.upper);
	if (sa->type != RX_SA)
		return;

	if (pn > replay_window)
		lnpn.full64 = pn - replay_window;
	else
		lnpn.full64 = 1;

	nxp_c45_macsec_write(phydev, sa_regs->lnpn, lnpn.lower);
	nxp_c45_macsec_write(phydev, sa_regs->lxnpn, lnpn.upper);
}

static void nxp_c45_sa_set_key(struct macsec_context *ctx,
			       const struct nxp_c45_sa_regs *sa_regs,
			       u8 *salt, ssci_t ssci)
{
	struct phy_device *phydev = ctx->phydev;
	u32 key_size = ctx->secy->key_len / 4;
	u32 salt_size = MACSEC_SALT_LEN / 4;
	u32 *key_u32 = (u32 *)ctx->sa.key;
	u32 *salt_u32 = (u32 *)salt;
	u32 reg, value;
	int i;

	for (i = 0; i < key_size; i++) {
		reg = sa_regs->ka + i * 4;
		value = (__force u32)cpu_to_be32(key_u32[i]);
		nxp_c45_macsec_write(phydev, reg, value);
	}

	if (ctx->secy->xpn) {
		for (i = 0; i < salt_size; i++) {
			reg = sa_regs->salt + (2 - i) * 4;
			value = (__force u32)cpu_to_be32(salt_u32[i]);
			nxp_c45_macsec_write(phydev, reg, value);
		}

		value = (__force u32)cpu_to_be32((__force u32)ssci);
		nxp_c45_macsec_write(phydev, sa_regs->ssci, value);
	}

	nxp_c45_macsec_write(phydev, sa_regs->cs, MACSEC_SA_CS_A);
}

static void nxp_c45_rx_sa_clear_stats(struct phy_device *phydev,
				      struct nxp_c45_sa *sa)
{
	nxp_c45_macsec_write(phydev, sa->regs->ipis, 0);
	nxp_c45_macsec_write(phydev, sa->regs->ipnvs, 0);
	nxp_c45_macsec_write(phydev, sa->regs->ipos, 0);

	nxp_c45_macsec_write(phydev, MACSEC_RXAN0INUSS + sa->an * 4, 0);
	nxp_c45_macsec_write(phydev, MACSEC_RXAN0IPUSS + sa->an * 4, 0);
}

static void nxp_c45_rx_sa_read_stats(struct phy_device *phydev,
				     struct nxp_c45_sa *sa,
				     struct macsec_rx_sa_stats *stats)
{
	nxp_c45_macsec_read(phydev, sa->regs->ipis, &stats->InPktsInvalid);
	nxp_c45_macsec_read(phydev, sa->regs->ipnvs, &stats->InPktsNotValid);
	nxp_c45_macsec_read(phydev, sa->regs->ipos, &stats->InPktsOK);
}

static void nxp_c45_tx_sa_clear_stats(struct phy_device *phydev,
				      struct nxp_c45_sa *sa)
{
	nxp_c45_macsec_write(phydev, sa->regs->opps, 0);
	nxp_c45_macsec_write(phydev, sa->regs->opes, 0);
}

static void nxp_c45_tx_sa_read_stats(struct phy_device *phydev,
				     struct nxp_c45_sa *sa,
				     struct macsec_tx_sa_stats *stats)
{
	nxp_c45_macsec_read(phydev, sa->regs->opps, &stats->OutPktsProtected);
	nxp_c45_macsec_read(phydev, sa->regs->opes, &stats->OutPktsEncrypted);
}

static void nxp_c45_rx_sa_update(struct phy_device *phydev,
				 struct nxp_c45_sa *sa, bool en)
{
	const struct nxp_c45_sa_regs *sa_regs = sa->regs;
	u32 cfg;

	cfg = sa->an << MACSEC_RXSA_CS_AN_OFF;
	cfg |= en ? MACSEC_RXSA_CS_EN : 0;
	nxp_c45_macsec_write(phydev, sa_regs->cs, cfg);
}

static void nxp_c45_tx_sa_update(struct phy_device *phydev,
				 struct nxp_c45_sa *sa, bool en)
{
	u32 cfg = 0;

	nxp_c45_macsec_read(phydev, MACSEC_TXSC_CFG, &cfg);

	cfg &= ~MACSEC_TXSC_CFG_AN_MASK;
	cfg |= sa->an << MACSEC_TXSC_CFG_AN_OFF;

	if (sa->is_key_a)
		cfg &= ~MACSEC_TXSC_CFG_ASA;
	else
		cfg |= MACSEC_TXSC_CFG_ASA;

	if (en)
		cfg |= MACSEC_TXSC_CFG_SCE;
	else
		cfg &= ~MACSEC_TXSC_CFG_SCE;

	nxp_c45_macsec_write(phydev, MACSEC_TXSC_CFG, cfg);
}

static void nxp_c45_set_sci(struct phy_device *phydev, u16 sci_base_addr,
			    sci_t sci)
{
	u64 lsci = sci_to_cpu(sci);

	nxp_c45_macsec_write(phydev, sci_base_addr, lsci >> 32);
	nxp_c45_macsec_write(phydev, sci_base_addr + 4, lsci);
}

static bool nxp_c45_port_is_1(sci_t sci)
{
	u16 port = sci_to_cpu(sci);

	return port == 1;
}

static void nxp_c45_select_secy(struct phy_device *phydev, u8 id)
{
	nxp_c45_macsec_write(phydev, MACSEC_RXSCA, id);
	nxp_c45_macsec_write(phydev, MACSEC_RXSCKA, id);
	nxp_c45_macsec_write(phydev, MACSEC_TXSCA, id);
	nxp_c45_macsec_write(phydev, MACSEC_TXSCKA, id);
}

static bool nxp_c45_secy_valid(struct nxp_c45_secy *phy_secy,
			       bool can_rx_sc0_impl)
{
	bool end_station = phy_secy->secy->tx_sc.end_station;
	bool scb = phy_secy->secy->tx_sc.scb;

	phy_secy->rx_sc0_impl = false;

	if (end_station) {
		if (!nxp_c45_port_is_1(phy_secy->secy->sci))
			return false;
		if (!phy_secy->rx_sc)
			return true;
		return nxp_c45_port_is_1(phy_secy->rx_sc->sci);
	}

	if (scb)
		return false;

	if (!can_rx_sc0_impl)
		return false;

	if (phy_secy->secy_id != 0)
		return false;

	phy_secy->rx_sc0_impl = true;

	return true;
}

static bool nxp_c45_rx_sc0_impl(struct nxp_c45_secy *phy_secy)
{
	bool end_station = phy_secy->secy->tx_sc.end_station;
	bool send_sci = phy_secy->secy->tx_sc.send_sci;
	bool scb = phy_secy->secy->tx_sc.scb;

	return !end_station && !send_sci && !scb;
}

static bool nxp_c45_mac_addr_free(struct macsec_context *ctx)
{
	struct nxp_c45_phy *priv = ctx->phydev->priv;
	struct nxp_c45_secy *pos, *tmp;

	list_for_each_entry_safe(pos, tmp, &priv->macsec->secy_list, list) {
		if (pos->secy == ctx->secy)
			continue;

		if (memcmp(pos->secy->netdev->dev_addr,
			   ctx->secy->netdev->dev_addr, ETH_ALEN) == 0)
			return false;
	}

	return true;
}

static void nxp_c45_tx_sc_en_flt(struct phy_device *phydev, int secy_id,
				 bool en)
{
	u32 tx_flt_base = TX_FLT_BASE(secy_id);
	u32 reg = 0;

	nxp_c45_macsec_read(phydev, TX_SC_FLT_MAC_CFG(tx_flt_base), &reg);
	if (en)
		reg |= TX_SC_FLT_EN;
	else
		reg &= ~TX_SC_FLT_EN;
	nxp_c45_macsec_write(phydev, TX_SC_FLT_MAC_CFG(tx_flt_base), reg);
}

static void nxp_c45_tx_sc_set_flt(struct phy_device *phydev,
				  struct nxp_c45_secy *phy_secy)
{
	const u8 *dev_addr = phy_secy->secy->netdev->dev_addr;
	u32 tx_flt_base = TX_FLT_BASE(phy_secy->secy_id);
	u32 reg;

	reg = dev_addr[0] << 8 | dev_addr[1];
	nxp_c45_macsec_write(phydev, TX_SC_FLT_MAC_DA_SA(tx_flt_base), reg);
	reg = dev_addr[5] | dev_addr[4] << 8 | dev_addr[3] << 16 |
		dev_addr[2] << 24;

	nxp_c45_macsec_write(phydev, TX_SC_FLT_MAC_SA(tx_flt_base), reg);
	nxp_c45_macsec_read(phydev, TX_SC_FLT_MAC_CFG(tx_flt_base), &reg);
	reg &= TX_SC_FLT_EN;
	reg |= TX_SC_FLT_BY_SA | phy_secy->secy_id;
	nxp_c45_macsec_write(phydev, TX_SC_FLT_MAC_CFG(tx_flt_base), reg);
}

static void nxp_c45_tx_sc_update(struct phy_device *phydev,
				 struct nxp_c45_secy *phy_secy)
{
	u32 cfg = 0;

	nxp_c45_macsec_read(phydev, MACSEC_TXSC_CFG, &cfg);

	phydev_dbg(phydev, "XPN %s\n", phy_secy->secy->xpn ? "on" : "off");
	if (phy_secy->secy->xpn)
		cfg |= MACSEC_TXSC_CFG_XPN;
	else
		cfg &= ~MACSEC_TXSC_CFG_XPN;

	phydev_dbg(phydev, "key len %u\n", phy_secy->secy->key_len);
	if (phy_secy->secy->key_len == 32)
		cfg |= MACSEC_TXSC_CFG_AES_256;
	else
		cfg &= ~MACSEC_TXSC_CFG_AES_256;

	phydev_dbg(phydev, "encryption %s\n",
		   phy_secy->secy->tx_sc.encrypt ? "on" : "off");
	if (phy_secy->secy->tx_sc.encrypt)
		cfg |= MACSEC_TXSC_CFG_ENCRYPT;
	else
		cfg &= ~MACSEC_TXSC_CFG_ENCRYPT;

	phydev_dbg(phydev, "protect frames %s\n",
		   phy_secy->secy->protect_frames ? "on" : "off");
	if (phy_secy->secy->protect_frames)
		cfg |= MACSEC_TXSC_CFG_PROTECT;
	else
		cfg &= ~MACSEC_TXSC_CFG_PROTECT;

	phydev_dbg(phydev, "send sci %s\n",
		   phy_secy->secy->tx_sc.send_sci ? "on" : "off");
	if (phy_secy->secy->tx_sc.send_sci)
		cfg |= MACSEC_TXSC_CFG_SEND_SCI;
	else
		cfg &= ~MACSEC_TXSC_CFG_SEND_SCI;

	phydev_dbg(phydev, "end station %s\n",
		   phy_secy->secy->tx_sc.end_station ? "on" : "off");
	if (phy_secy->secy->tx_sc.end_station)
		cfg |= MACSEC_TXSC_CFG_END_STATION;
	else
		cfg &= ~MACSEC_TXSC_CFG_END_STATION;

	phydev_dbg(phydev, "scb %s\n",
		   phy_secy->secy->tx_sc.scb ? "on" : "off");
	if (phy_secy->secy->tx_sc.scb)
		cfg |= MACSEC_TXSC_CFG_SCB;
	else
		cfg &= ~MACSEC_TXSC_CFG_SCB;

	nxp_c45_macsec_write(phydev, MACSEC_TXSC_CFG, cfg);
}

static void nxp_c45_tx_sc_clear_stats(struct phy_device *phydev,
				      struct nxp_c45_secy *phy_secy)
{
	struct nxp_c45_sa *pos, *tmp;

	list_for_each_entry_safe(pos, tmp, &phy_secy->sa_list, list)
		if (pos->type == TX_SA)
			nxp_c45_tx_sa_clear_stats(phydev, pos);

	nxp_c45_macsec_write(phydev, MACSEC_OPUS, 0);
	nxp_c45_macsec_write(phydev, MACSEC_OPTLS, 0);
	nxp_c45_macsec_write(phydev, MACSEC_OOP1HS, 0);
	nxp_c45_macsec_write(phydev, MACSEC_OOP2HS, 0);
	nxp_c45_macsec_write(phydev, MACSEC_OOE1HS, 0);
	nxp_c45_macsec_write(phydev, MACSEC_OOE2HS, 0);
}

static void nxp_c45_set_rx_sc0_impl(struct phy_device *phydev,
				    bool enable)
{
	u32 reg = 0;

	nxp_c45_macsec_read(phydev, MACSEC_CFG, &reg);
	if (enable)
		reg |= MACSEC_CFG_S0I;
	else
		reg &= ~MACSEC_CFG_S0I;
	nxp_c45_macsec_write(phydev, MACSEC_CFG, reg);
}

static bool nxp_c45_is_rx_sc0_impl(struct list_head *secy_list)
{
	struct nxp_c45_secy *pos, *tmp;

	list_for_each_entry_safe(pos, tmp, secy_list, list)
		if (pos->rx_sc0_impl)
			return pos->rx_sc0_impl;

	return false;
}

static void nxp_c45_rx_sc_en(struct phy_device *phydev,
			     struct macsec_rx_sc *rx_sc, bool en)
{
	u32 reg = 0;

	nxp_c45_macsec_read(phydev, MACSEC_RXSC_CFG, &reg);
	if (rx_sc->active && en)
		reg |= MACSEC_RXSC_CFG_SCI_EN;
	else
		reg &= ~MACSEC_RXSC_CFG_SCI_EN;
	nxp_c45_macsec_write(phydev, MACSEC_RXSC_CFG, reg);
}

static void nxp_c45_rx_sc_update(struct phy_device *phydev,
				 struct nxp_c45_secy *phy_secy)
{
	struct macsec_rx_sc *rx_sc = phy_secy->rx_sc;
	struct nxp_c45_phy *priv = phydev->priv;
	u32 cfg = 0;

	nxp_c45_macsec_read(phydev, MACSEC_RXSC_CFG, &cfg);
	cfg &= ~MACSEC_RXSC_CFG_VF_MASK;
	cfg = phy_secy->secy->validate_frames << MACSEC_RXSC_CFG_VF_OFF;

	phydev_dbg(phydev, "validate frames %u\n",
		   phy_secy->secy->validate_frames);
	phydev_dbg(phydev, "replay_protect %s window %u\n",
		   phy_secy->secy->replay_protect ? "on" : "off",
		   phy_secy->secy->replay_window);
	if (phy_secy->secy->replay_protect) {
		cfg |= MACSEC_RXSC_CFG_RP;
		nxp_c45_macsec_write(phydev, MACSEC_RPW,
				     phy_secy->secy->replay_window);
	} else {
		cfg &= ~MACSEC_RXSC_CFG_RP;
	}

	phydev_dbg(phydev, "rx_sc->active %s\n",
		   rx_sc->active ? "on" : "off");
	if (rx_sc->active &&
	    test_bit(phy_secy->secy_id, priv->macsec->secy_bitmap))
		cfg |= MACSEC_RXSC_CFG_SCI_EN;
	else
		cfg &= ~MACSEC_RXSC_CFG_SCI_EN;

	phydev_dbg(phydev, "key len %u\n", phy_secy->secy->key_len);
	if (phy_secy->secy->key_len == 32)
		cfg |= MACSEC_RXSC_CFG_AES_256;
	else
		cfg &= ~MACSEC_RXSC_CFG_AES_256;

	phydev_dbg(phydev, "XPN %s\n", phy_secy->secy->xpn ? "on" : "off");
	if (phy_secy->secy->xpn)
		cfg |= MACSEC_RXSC_CFG_XPN;
	else
		cfg &= ~MACSEC_RXSC_CFG_XPN;

	nxp_c45_macsec_write(phydev, MACSEC_RXSC_CFG, cfg);
}

static void nxp_c45_rx_sc_clear_stats(struct phy_device *phydev,
				      struct nxp_c45_secy *phy_secy)
{
	struct nxp_c45_sa *pos, *tmp;
	int i;

	list_for_each_entry_safe(pos, tmp, &phy_secy->sa_list, list)
		if (pos->type == RX_SA)
			nxp_c45_rx_sa_clear_stats(phydev, pos);

	nxp_c45_macsec_write(phydev, MACSEC_INOD1HS, 0);
	nxp_c45_macsec_write(phydev, MACSEC_INOD2HS, 0);

	nxp_c45_macsec_write(phydev, MACSEC_INOV1HS, 0);
	nxp_c45_macsec_write(phydev, MACSEC_INOV2HS, 0);

	nxp_c45_macsec_write(phydev, MACSEC_RXSCIPDS, 0);
	nxp_c45_macsec_write(phydev, MACSEC_RXSCIPLS, 0);
	nxp_c45_macsec_write(phydev, MACSEC_RXSCIPUS, 0);

	for (i = 0; i < MACSEC_NUM_AN; i++) {
		nxp_c45_macsec_write(phydev, MACSEC_RXAN0INUSS + i * 4, 0);
		nxp_c45_macsec_write(phydev, MACSEC_RXAN0IPUSS + i * 4, 0);
	}
}

static void nxp_c45_rx_sc_del(struct phy_device *phydev,
			      struct nxp_c45_secy *phy_secy)
{
	struct nxp_c45_sa *pos, *tmp;

	nxp_c45_macsec_write(phydev, MACSEC_RXSC_CFG, 0);
	nxp_c45_macsec_write(phydev, MACSEC_RPW, 0);
	nxp_c45_set_sci(phydev, MACSEC_RXSC_SCI_1H, 0);

	nxp_c45_rx_sc_clear_stats(phydev, phy_secy);

	list_for_each_entry_safe(pos, tmp, &phy_secy->sa_list, list) {
		if (pos->type == RX_SA) {
			nxp_c45_rx_sa_update(phydev, pos, false);
			nxp_c45_sa_free(pos);
		}
	}
}

static void nxp_c45_clear_global_stats(struct phy_device *phydev)
{
	nxp_c45_macsec_write(phydev, MACSEC_INPBTS, 0);
	nxp_c45_macsec_write(phydev, MACSEC_INPWTS, 0);
	nxp_c45_macsec_write(phydev, MACSEC_IPSNFS, 0);
}

static void nxp_c45_macsec_en(struct phy_device *phydev, bool en)
{
	u32 reg;

	nxp_c45_macsec_read(phydev, MACSEC_CFG, &reg);
	if (en)
		reg |= MACSEC_CFG_BYPASS;
	else
		reg &= ~MACSEC_CFG_BYPASS;
	nxp_c45_macsec_write(phydev, MACSEC_CFG, reg);
}

static int nxp_c45_mdo_dev_open(struct macsec_context *ctx)
{
	struct phy_device *phydev = ctx->phydev;
	struct nxp_c45_phy *priv = phydev->priv;
	struct nxp_c45_secy *phy_secy;
	int any_bit_set;

	phy_secy = nxp_c45_find_secy(&priv->macsec->secy_list, ctx->secy->sci);
	if (IS_ERR(phy_secy))
		return PTR_ERR(phy_secy);

	nxp_c45_select_secy(phydev, phy_secy->secy_id);

	nxp_c45_tx_sc_en_flt(phydev, phy_secy->secy_id, true);
	nxp_c45_set_rx_sc0_impl(phydev, phy_secy->rx_sc0_impl);
	if (phy_secy->rx_sc)
		nxp_c45_rx_sc_en(phydev, phy_secy->rx_sc, true);

	any_bit_set = find_first_bit(priv->macsec->secy_bitmap, TX_SC_MAX);
	if (any_bit_set == TX_SC_MAX)
		nxp_c45_macsec_en(phydev, true);

	set_bit(phy_secy->secy_id, priv->macsec->secy_bitmap);

	return 0;
}

static int nxp_c45_mdo_dev_stop(struct macsec_context *ctx)
{
	struct phy_device *phydev = ctx->phydev;
	struct nxp_c45_phy *priv = phydev->priv;
	struct nxp_c45_secy *phy_secy;
	int any_bit_set;

	phy_secy = nxp_c45_find_secy(&priv->macsec->secy_list, ctx->secy->sci);
	if (IS_ERR(phy_secy))
		return PTR_ERR(phy_secy);

	nxp_c45_select_secy(phydev, phy_secy->secy_id);

	nxp_c45_tx_sc_en_flt(phydev, phy_secy->secy_id, false);
	if (phy_secy->rx_sc)
		nxp_c45_rx_sc_en(phydev, phy_secy->rx_sc, false);
	nxp_c45_set_rx_sc0_impl(phydev, false);

	clear_bit(phy_secy->secy_id, priv->macsec->secy_bitmap);
	any_bit_set = find_first_bit(priv->macsec->secy_bitmap, TX_SC_MAX);
	if (any_bit_set == TX_SC_MAX)
		nxp_c45_macsec_en(phydev, false);

	return 0;
}

static int nxp_c45_mdo_add_secy(struct macsec_context *ctx)
{
	struct phy_device *phydev = ctx->phydev;
	struct nxp_c45_phy *priv = phydev->priv;
	struct nxp_c45_secy *phy_secy;
	bool can_rx_sc0_impl;
	int idx;

	phydev_dbg(phydev, "add SecY SCI %016llx\n",
		   sci_to_cpu(ctx->secy->sci));

	if (!nxp_c45_mac_addr_free(ctx))
		return -EBUSY;

	if (nxp_c45_is_rx_sc0_impl(&priv->macsec->secy_list))
		return -EBUSY;

	idx = find_first_zero_bit(priv->macsec->tx_sc_bitmap, TX_SC_MAX);
	if (idx == TX_SC_MAX)
		return -ENOSPC;

	phy_secy = kzalloc(sizeof(*phy_secy), GFP_KERNEL);
	if (!phy_secy)
		return -ENOMEM;

	INIT_LIST_HEAD(&phy_secy->sa_list);
	phy_secy->secy = ctx->secy;
	phy_secy->secy_id = idx;

	/* If the point to point mode should be enabled, we should have no
	 * SecY added yet.
	 */
	can_rx_sc0_impl = list_count_nodes(&priv->macsec->secy_list) == 0;
	if (!nxp_c45_secy_valid(phy_secy, can_rx_sc0_impl)) {
		kfree(phy_secy);
		return -EINVAL;
	}

	phy_secy->rx_sc0_impl = nxp_c45_rx_sc0_impl(phy_secy);

	nxp_c45_select_secy(phydev, phy_secy->secy_id);
	nxp_c45_set_sci(phydev, MACSEC_TXSC_SCI_1H, ctx->secy->sci);
	nxp_c45_tx_sc_set_flt(phydev, phy_secy);
	nxp_c45_tx_sc_update(phydev, phy_secy);
	if (phy_interrupt_is_valid(phydev))
		nxp_c45_secy_irq_en(phydev, phy_secy, true);

	set_bit(idx, priv->macsec->tx_sc_bitmap);
	list_add_tail(&phy_secy->list, &priv->macsec->secy_list);

	return 0;
}

static void nxp_c45_tx_sa_next(struct nxp_c45_secy *phy_secy,
			       struct nxp_c45_sa *next_sa, u8 encoding_sa)
{
	struct nxp_c45_sa *sa;

	sa = nxp_c45_find_sa(&phy_secy->sa_list, TX_SA, encoding_sa);
	if (!IS_ERR(sa)) {
		memcpy(next_sa, sa, sizeof(*sa));
	} else {
		next_sa->is_key_a = true;
		next_sa->an = encoding_sa;
	}
}

static int nxp_c45_mdo_upd_secy(struct macsec_context *ctx)
{
	u8 encoding_sa = ctx->secy->tx_sc.encoding_sa;
	struct phy_device *phydev = ctx->phydev;
	struct nxp_c45_phy *priv = phydev->priv;
	struct nxp_c45_secy *phy_secy;
	struct nxp_c45_sa next_sa;
	bool can_rx_sc0_impl;

	phydev_dbg(phydev, "update SecY SCI %016llx\n",
		   sci_to_cpu(ctx->secy->sci));

	phy_secy = nxp_c45_find_secy(&priv->macsec->secy_list, ctx->secy->sci);
	if (IS_ERR(phy_secy))
		return PTR_ERR(phy_secy);

	if (!nxp_c45_mac_addr_free(ctx))
		return -EBUSY;

	/* If the point to point mode should be enabled, we should have only
	 * one SecY added, respectively the updated one.
	 */
	can_rx_sc0_impl = list_count_nodes(&priv->macsec->secy_list) == 1;
	if (!nxp_c45_secy_valid(phy_secy, can_rx_sc0_impl))
		return -EINVAL;
	phy_secy->rx_sc0_impl = nxp_c45_rx_sc0_impl(phy_secy);

	nxp_c45_select_secy(phydev, phy_secy->secy_id);
	nxp_c45_tx_sc_set_flt(phydev, phy_secy);
	nxp_c45_tx_sc_update(phydev, phy_secy);
	nxp_c45_tx_sa_next(phy_secy, &next_sa, encoding_sa);
	nxp_c45_tx_sa_update(phydev, &next_sa, ctx->secy->operational);

	nxp_c45_set_rx_sc0_impl(phydev, phy_secy->rx_sc0_impl);
	if (phy_secy->rx_sc)
		nxp_c45_rx_sc_update(phydev, phy_secy);

	return 0;
}

static int nxp_c45_mdo_del_secy(struct macsec_context *ctx)
{
	u8 encoding_sa = ctx->secy->tx_sc.encoding_sa;
	struct phy_device *phydev = ctx->phydev;
	struct nxp_c45_phy *priv = phydev->priv;
	struct nxp_c45_secy *phy_secy;
	struct nxp_c45_sa next_sa;

	phydev_dbg(phydev, "delete SecY SCI %016llx\n",
		   sci_to_cpu(ctx->secy->sci));

	phy_secy = nxp_c45_find_secy(&priv->macsec->secy_list, ctx->secy->sci);
	if (IS_ERR(phy_secy))
		return PTR_ERR(phy_secy);
	nxp_c45_select_secy(phydev, phy_secy->secy_id);

	nxp_c45_mdo_dev_stop(ctx);
	nxp_c45_tx_sa_next(phy_secy, &next_sa, encoding_sa);
	nxp_c45_tx_sa_update(phydev, &next_sa, false);
	nxp_c45_tx_sc_clear_stats(phydev, phy_secy);
	if (phy_secy->rx_sc)
		nxp_c45_rx_sc_del(phydev, phy_secy);

	nxp_c45_sa_list_free(&phy_secy->sa_list);
	if (phy_interrupt_is_valid(phydev))
		nxp_c45_secy_irq_en(phydev, phy_secy, false);

	clear_bit(phy_secy->secy_id, priv->macsec->tx_sc_bitmap);
	nxp_c45_secy_free(phy_secy);

	if (list_empty(&priv->macsec->secy_list))
		nxp_c45_clear_global_stats(phydev);

	return 0;
}

static int nxp_c45_mdo_add_rxsc(struct macsec_context *ctx)
{
	struct phy_device *phydev = ctx->phydev;
	struct nxp_c45_phy *priv = phydev->priv;
	struct nxp_c45_secy *phy_secy;

	phydev_dbg(phydev, "add RX SC SCI %016llx %s\n",
		   sci_to_cpu(ctx->rx_sc->sci),
		   ctx->rx_sc->active ? "enabled" : "disabled");

	phy_secy = nxp_c45_find_secy(&priv->macsec->secy_list, ctx->secy->sci);
	if (IS_ERR(phy_secy))
		return PTR_ERR(phy_secy);

	if (phy_secy->rx_sc)
		return -ENOSPC;

	if (phy_secy->secy->tx_sc.end_station &&
	    !nxp_c45_port_is_1(ctx->rx_sc->sci))
		return -EINVAL;

	phy_secy->rx_sc = ctx->rx_sc;

	nxp_c45_select_secy(phydev, phy_secy->secy_id);
	nxp_c45_set_sci(phydev, MACSEC_RXSC_SCI_1H, ctx->rx_sc->sci);
	nxp_c45_rx_sc_update(phydev, phy_secy);

	return 0;
}

static int nxp_c45_mdo_upd_rxsc(struct macsec_context *ctx)
{
	struct phy_device *phydev = ctx->phydev;
	struct nxp_c45_phy *priv = phydev->priv;
	struct nxp_c45_secy *phy_secy;

	phydev_dbg(phydev, "update RX SC SCI %016llx %s\n",
		   sci_to_cpu(ctx->rx_sc->sci),
		   ctx->rx_sc->active ? "enabled" : "disabled");

	phy_secy = nxp_c45_find_secy(&priv->macsec->secy_list, ctx->secy->sci);
	if (IS_ERR(phy_secy))
		return PTR_ERR(phy_secy);

	nxp_c45_select_secy(phydev, phy_secy->secy_id);
	nxp_c45_rx_sc_update(phydev, phy_secy);

	return 0;
}

static int nxp_c45_mdo_del_rxsc(struct macsec_context *ctx)
{
	struct phy_device *phydev = ctx->phydev;
	struct nxp_c45_phy *priv = phydev->priv;
	struct nxp_c45_secy *phy_secy;

	phydev_dbg(phydev, "delete RX SC SCI %016llx %s\n",
		   sci_to_cpu(ctx->rx_sc->sci),
		   ctx->rx_sc->active ? "enabled" : "disabled");

	phy_secy = nxp_c45_find_secy(&priv->macsec->secy_list, ctx->secy->sci);
	if (IS_ERR(phy_secy))
		return PTR_ERR(phy_secy);

	nxp_c45_select_secy(phydev, phy_secy->secy_id);
	nxp_c45_rx_sc_del(phydev, phy_secy);
	phy_secy->rx_sc = NULL;

	return 0;
}

static int nxp_c45_mdo_add_rxsa(struct macsec_context *ctx)
{
	struct macsec_rx_sa *rx_sa = ctx->sa.rx_sa;
	struct phy_device *phydev = ctx->phydev;
	struct nxp_c45_phy *priv = phydev->priv;
	struct nxp_c45_secy *phy_secy;
	u8 an = ctx->sa.assoc_num;
	struct nxp_c45_sa *sa;

	phydev_dbg(phydev, "add RX SA %u %s to RX SC SCI %016llx\n",
		   an, rx_sa->active ? "enabled" : "disabled",
		   sci_to_cpu(rx_sa->sc->sci));

	phy_secy = nxp_c45_find_secy(&priv->macsec->secy_list, ctx->secy->sci);
	if (IS_ERR(phy_secy))
		return PTR_ERR(phy_secy);

	sa = nxp_c45_sa_alloc(&phy_secy->sa_list, rx_sa, RX_SA, an);
	if (IS_ERR(sa))
		return PTR_ERR(sa);

	nxp_c45_select_secy(phydev, phy_secy->secy_id);
	nxp_c45_sa_set_pn(phydev, sa, rx_sa->next_pn,
			  ctx->secy->replay_window);
	nxp_c45_sa_set_key(ctx, sa->regs, rx_sa->key.salt.bytes, rx_sa->ssci);
	nxp_c45_rx_sa_update(phydev, sa, rx_sa->active);

	return 0;
}

static int nxp_c45_mdo_upd_rxsa(struct macsec_context *ctx)
{
	struct macsec_rx_sa *rx_sa = ctx->sa.rx_sa;
	struct phy_device *phydev = ctx->phydev;
	struct nxp_c45_phy *priv = phydev->priv;
	struct nxp_c45_secy *phy_secy;
	u8 an = ctx->sa.assoc_num;
	struct nxp_c45_sa *sa;

	phydev_dbg(phydev, "update RX SA %u %s to RX SC SCI %016llx\n",
		   an, rx_sa->active ? "enabled" : "disabled",
		   sci_to_cpu(rx_sa->sc->sci));

	phy_secy = nxp_c45_find_secy(&priv->macsec->secy_list, ctx->secy->sci);
	if (IS_ERR(phy_secy))
		return PTR_ERR(phy_secy);

	sa = nxp_c45_find_sa(&phy_secy->sa_list, RX_SA, an);
	if (IS_ERR(sa))
		return PTR_ERR(sa);

	nxp_c45_select_secy(phydev, phy_secy->secy_id);
	if (ctx->sa.update_pn)
		nxp_c45_sa_set_pn(phydev, sa, rx_sa->next_pn,
				  ctx->secy->replay_window);
	nxp_c45_rx_sa_update(phydev, sa, rx_sa->active);

	return 0;
}

static int nxp_c45_mdo_del_rxsa(struct macsec_context *ctx)
{
	struct macsec_rx_sa *rx_sa = ctx->sa.rx_sa;
	struct phy_device *phydev = ctx->phydev;
	struct nxp_c45_phy *priv = phydev->priv;
	struct nxp_c45_secy *phy_secy;
	u8 an = ctx->sa.assoc_num;
	struct nxp_c45_sa *sa;

	phydev_dbg(phydev, "delete RX SA %u %s to RX SC SCI %016llx\n",
		   an, rx_sa->active ? "enabled" : "disabled",
		   sci_to_cpu(rx_sa->sc->sci));

	phy_secy = nxp_c45_find_secy(&priv->macsec->secy_list, ctx->secy->sci);
	if (IS_ERR(phy_secy))
		return PTR_ERR(phy_secy);

	sa = nxp_c45_find_sa(&phy_secy->sa_list, RX_SA, an);
	if (IS_ERR(sa))
		return PTR_ERR(sa);

	nxp_c45_select_secy(phydev, phy_secy->secy_id);
	nxp_c45_rx_sa_update(phydev, sa, false);
	nxp_c45_rx_sa_clear_stats(phydev, sa);

	nxp_c45_sa_free(sa);

	return 0;
}

static int nxp_c45_mdo_add_txsa(struct macsec_context *ctx)
{
	struct macsec_tx_sa *tx_sa = ctx->sa.tx_sa;
	struct phy_device *phydev = ctx->phydev;
	struct nxp_c45_phy *priv = phydev->priv;
	struct nxp_c45_secy *phy_secy;
	u8 an = ctx->sa.assoc_num;
	struct nxp_c45_sa *sa;

	phydev_dbg(phydev, "add TX SA %u %s to TX SC %016llx\n",
		   an, ctx->sa.tx_sa->active ? "enabled" : "disabled",
		   sci_to_cpu(ctx->secy->sci));

	phy_secy = nxp_c45_find_secy(&priv->macsec->secy_list, ctx->secy->sci);
	if (IS_ERR(phy_secy))
		return PTR_ERR(phy_secy);

	sa = nxp_c45_sa_alloc(&phy_secy->sa_list, tx_sa, TX_SA, an);
	if (IS_ERR(sa))
		return PTR_ERR(sa);

	nxp_c45_select_secy(phydev, phy_secy->secy_id);
	nxp_c45_sa_set_pn(phydev, sa, tx_sa->next_pn, 0);
	nxp_c45_sa_set_key(ctx, sa->regs, tx_sa->key.salt.bytes, tx_sa->ssci);
	if (ctx->secy->tx_sc.encoding_sa == sa->an)
		nxp_c45_tx_sa_update(phydev, sa, tx_sa->active);

	return 0;
}

static int nxp_c45_mdo_upd_txsa(struct macsec_context *ctx)
{
	struct macsec_tx_sa *tx_sa = ctx->sa.tx_sa;
	struct phy_device *phydev = ctx->phydev;
	struct nxp_c45_phy *priv = phydev->priv;
	struct nxp_c45_secy *phy_secy;
	u8 an = ctx->sa.assoc_num;
	struct nxp_c45_sa *sa;

	phydev_dbg(phydev, "update TX SA %u %s to TX SC %016llx\n",
		   an, ctx->sa.tx_sa->active ? "enabled" : "disabled",
		   sci_to_cpu(ctx->secy->sci));

	phy_secy = nxp_c45_find_secy(&priv->macsec->secy_list, ctx->secy->sci);
	if (IS_ERR(phy_secy))
		return PTR_ERR(phy_secy);

	sa = nxp_c45_find_sa(&phy_secy->sa_list, TX_SA, an);
	if (IS_ERR(sa))
		return PTR_ERR(sa);

	nxp_c45_select_secy(phydev, phy_secy->secy_id);
	if (ctx->sa.update_pn)
		nxp_c45_sa_set_pn(phydev, sa, tx_sa->next_pn, 0);
	if (ctx->secy->tx_sc.encoding_sa == sa->an)
		nxp_c45_tx_sa_update(phydev, sa, tx_sa->active);

	return 0;
}

static int nxp_c45_mdo_del_txsa(struct macsec_context *ctx)
{
	struct phy_device *phydev = ctx->phydev;
	struct nxp_c45_phy *priv = phydev->priv;
	struct nxp_c45_secy *phy_secy;
	u8 an = ctx->sa.assoc_num;
	struct nxp_c45_sa *sa;

	phydev_dbg(phydev, "delete TX SA %u %s to TX SC %016llx\n",
		   an, ctx->sa.tx_sa->active ? "enabled" : "disabled",
		   sci_to_cpu(ctx->secy->sci));

	phy_secy = nxp_c45_find_secy(&priv->macsec->secy_list, ctx->secy->sci);
	if (IS_ERR(phy_secy))
		return PTR_ERR(phy_secy);

	sa = nxp_c45_find_sa(&phy_secy->sa_list, TX_SA, an);
	if (IS_ERR(sa))
		return PTR_ERR(sa);

	nxp_c45_select_secy(phydev, phy_secy->secy_id);
	if (ctx->secy->tx_sc.encoding_sa == sa->an)
		nxp_c45_tx_sa_update(phydev, sa, false);
	nxp_c45_tx_sa_clear_stats(phydev, sa);

	nxp_c45_sa_free(sa);

	return 0;
}

static int nxp_c45_mdo_get_dev_stats(struct macsec_context *ctx)
{
	struct phy_device *phydev = ctx->phydev;
	struct nxp_c45_phy *priv = phydev->priv;
	struct macsec_dev_stats  *dev_stats;
	struct nxp_c45_secy *phy_secy;

	phy_secy = nxp_c45_find_secy(&priv->macsec->secy_list, ctx->secy->sci);
	if (IS_ERR(phy_secy))
		return PTR_ERR(phy_secy);

	dev_stats = ctx->stats.dev_stats;
	nxp_c45_select_secy(phydev, phy_secy->secy_id);

	nxp_c45_macsec_read32_64(phydev, MACSEC_OPUS,
				 &dev_stats->OutPktsUntagged);
	nxp_c45_macsec_read32_64(phydev, MACSEC_OPTLS,
				 &dev_stats->OutPktsTooLong);
	nxp_c45_macsec_read32_64(phydev, MACSEC_INPBTS,
				 &dev_stats->InPktsBadTag);

	if (phy_secy->secy->validate_frames == MACSEC_VALIDATE_STRICT)
		nxp_c45_macsec_read32_64(phydev, MACSEC_INPWTS,
					 &dev_stats->InPktsNoTag);
	else
		nxp_c45_macsec_read32_64(phydev, MACSEC_INPWTS,
					 &dev_stats->InPktsUntagged);

	if (phy_secy->secy->validate_frames == MACSEC_VALIDATE_STRICT)
		nxp_c45_macsec_read32_64(phydev, MACSEC_IPSNFS,
					 &dev_stats->InPktsNoSCI);
	else
		nxp_c45_macsec_read32_64(phydev, MACSEC_IPSNFS,
					 &dev_stats->InPktsUnknownSCI);

	/* Always 0. */
	dev_stats->InPktsOverrun = 0;

	return 0;
}

static int nxp_c45_mdo_get_tx_sc_stats(struct macsec_context *ctx)
{
	struct phy_device *phydev = ctx->phydev;
	struct nxp_c45_phy *priv = phydev->priv;
	struct macsec_tx_sa_stats tx_sa_stats;
	struct macsec_tx_sc_stats *stats;
	struct nxp_c45_secy *phy_secy;
	struct nxp_c45_sa *pos, *tmp;

	phy_secy = nxp_c45_find_secy(&priv->macsec->secy_list, ctx->secy->sci);
	if (IS_ERR(phy_secy))
		return PTR_ERR(phy_secy);

	stats = ctx->stats.tx_sc_stats;
	nxp_c45_select_secy(phydev, phy_secy->secy_id);

	nxp_c45_macsec_read64(phydev, MACSEC_OOE1HS,
			      &stats->OutOctetsEncrypted);
	nxp_c45_macsec_read64(phydev, MACSEC_OOP1HS,
			      &stats->OutOctetsProtected);
	list_for_each_entry_safe(pos, tmp, &phy_secy->sa_list, list) {
		if (pos->type != TX_SA)
			continue;

		memset(&tx_sa_stats, 0, sizeof(tx_sa_stats));
		nxp_c45_tx_sa_read_stats(phydev, pos, &tx_sa_stats);

		stats->OutPktsEncrypted += tx_sa_stats.OutPktsEncrypted;
		stats->OutPktsProtected += tx_sa_stats.OutPktsProtected;
	}

	return 0;
}

static int nxp_c45_mdo_get_tx_sa_stats(struct macsec_context *ctx)
{
	struct phy_device *phydev = ctx->phydev;
	struct nxp_c45_phy *priv = phydev->priv;
	struct macsec_tx_sa_stats *stats;
	struct nxp_c45_secy *phy_secy;
	u8 an = ctx->sa.assoc_num;
	struct nxp_c45_sa *sa;

	phy_secy = nxp_c45_find_secy(&priv->macsec->secy_list, ctx->secy->sci);
	if (IS_ERR(phy_secy))
		return PTR_ERR(phy_secy);

	sa = nxp_c45_find_sa(&phy_secy->sa_list, TX_SA, an);
	if (IS_ERR(sa))
		return PTR_ERR(sa);

	stats = ctx->stats.tx_sa_stats;
	nxp_c45_select_secy(phydev, phy_secy->secy_id);
	nxp_c45_tx_sa_read_stats(phydev, sa, stats);

	return 0;
}

static int nxp_c45_mdo_get_rx_sc_stats(struct macsec_context *ctx)
{
	struct phy_device *phydev = ctx->phydev;
	struct nxp_c45_phy *priv = phydev->priv;
	struct macsec_rx_sa_stats rx_sa_stats;
	struct macsec_rx_sc_stats *stats;
	struct nxp_c45_secy *phy_secy;
	struct nxp_c45_sa *pos, *tmp;
	u32 reg = 0;
	int i;

	phy_secy = nxp_c45_find_secy(&priv->macsec->secy_list, ctx->secy->sci);
	if (IS_ERR(phy_secy))
		return PTR_ERR(phy_secy);

	if (phy_secy->rx_sc != ctx->rx_sc)
		return -EINVAL;

	stats = ctx->stats.rx_sc_stats;
	nxp_c45_select_secy(phydev, phy_secy->secy_id);

	list_for_each_entry_safe(pos, tmp, &phy_secy->sa_list, list) {
		if (pos->type != RX_SA)
			continue;

		memset(&rx_sa_stats, 0, sizeof(rx_sa_stats));
		nxp_c45_rx_sa_read_stats(phydev, pos, &rx_sa_stats);

		stats->InPktsInvalid += rx_sa_stats.InPktsInvalid;
		stats->InPktsNotValid += rx_sa_stats.InPktsNotValid;
		stats->InPktsOK += rx_sa_stats.InPktsOK;
	}

	for (i = 0; i < MACSEC_NUM_AN; i++) {
		nxp_c45_macsec_read(phydev, MACSEC_RXAN0INUSS + i * 4, &reg);
		stats->InPktsNotUsingSA += reg;
		nxp_c45_macsec_read(phydev, MACSEC_RXAN0IPUSS + i * 4, &reg);
		stats->InPktsUnusedSA += reg;
	}

	nxp_c45_macsec_read64(phydev, MACSEC_INOD1HS,
			      &stats->InOctetsDecrypted);
	nxp_c45_macsec_read64(phydev, MACSEC_INOV1HS,
			      &stats->InOctetsValidated);

	nxp_c45_macsec_read32_64(phydev, MACSEC_RXSCIPDS,
				 &stats->InPktsDelayed);
	nxp_c45_macsec_read32_64(phydev, MACSEC_RXSCIPLS,
				 &stats->InPktsLate);
	nxp_c45_macsec_read32_64(phydev, MACSEC_RXSCIPUS,
				 &stats->InPktsUnchecked);

	return 0;
}

static int nxp_c45_mdo_get_rx_sa_stats(struct macsec_context *ctx)
{
	struct phy_device *phydev = ctx->phydev;
	struct nxp_c45_phy *priv = phydev->priv;
	struct macsec_rx_sa_stats *stats;
	struct nxp_c45_secy *phy_secy;
	u8 an = ctx->sa.assoc_num;
	struct nxp_c45_sa *sa;

	phy_secy = nxp_c45_find_secy(&priv->macsec->secy_list, ctx->secy->sci);
	if (IS_ERR(phy_secy))
		return PTR_ERR(phy_secy);

	sa = nxp_c45_find_sa(&phy_secy->sa_list, RX_SA, an);
	if (IS_ERR(sa))
		return PTR_ERR(sa);

	stats = ctx->stats.rx_sa_stats;
	nxp_c45_select_secy(phydev, phy_secy->secy_id);

	nxp_c45_rx_sa_read_stats(phydev, sa, stats);
	nxp_c45_macsec_read(phydev, MACSEC_RXAN0INUSS + an * 4,
			    &stats->InPktsNotUsingSA);
	nxp_c45_macsec_read(phydev, MACSEC_RXAN0IPUSS + an * 4,
			    &stats->InPktsUnusedSA);

	return 0;
}

struct tja11xx_tlv_header {
	struct ethhdr eth;
	u8 subtype;
	u8 len;
	u8 payload[28];
};

static int nxp_c45_mdo_insert_tx_tag(struct phy_device *phydev,
				     struct sk_buff *skb)
{
	struct tja11xx_tlv_header *tlv;
	struct ethhdr *eth;

	eth = eth_hdr(skb);
	tlv = skb_push(skb, TJA11XX_TLV_TX_NEEDED_HEADROOM);
	memmove(tlv, eth, sizeof(*eth));
	skb_reset_mac_header(skb);
	tlv->eth.h_proto = htons(ETH_P_TJA11XX_TLV);
	tlv->subtype = 1;
	tlv->len = sizeof(tlv->payload);
	memset(tlv->payload, 0, sizeof(tlv->payload));

	return 0;
}

static const struct macsec_ops nxp_c45_macsec_ops = {
	.mdo_dev_open = nxp_c45_mdo_dev_open,
	.mdo_dev_stop = nxp_c45_mdo_dev_stop,
	.mdo_add_secy = nxp_c45_mdo_add_secy,
	.mdo_upd_secy = nxp_c45_mdo_upd_secy,
	.mdo_del_secy = nxp_c45_mdo_del_secy,
	.mdo_add_rxsc = nxp_c45_mdo_add_rxsc,
	.mdo_upd_rxsc = nxp_c45_mdo_upd_rxsc,
	.mdo_del_rxsc = nxp_c45_mdo_del_rxsc,
	.mdo_add_rxsa = nxp_c45_mdo_add_rxsa,
	.mdo_upd_rxsa = nxp_c45_mdo_upd_rxsa,
	.mdo_del_rxsa = nxp_c45_mdo_del_rxsa,
	.mdo_add_txsa = nxp_c45_mdo_add_txsa,
	.mdo_upd_txsa = nxp_c45_mdo_upd_txsa,
	.mdo_del_txsa = nxp_c45_mdo_del_txsa,
	.mdo_get_dev_stats = nxp_c45_mdo_get_dev_stats,
	.mdo_get_tx_sc_stats = nxp_c45_mdo_get_tx_sc_stats,
	.mdo_get_tx_sa_stats = nxp_c45_mdo_get_tx_sa_stats,
	.mdo_get_rx_sc_stats = nxp_c45_mdo_get_rx_sc_stats,
	.mdo_get_rx_sa_stats = nxp_c45_mdo_get_rx_sa_stats,
	.mdo_insert_tx_tag = nxp_c45_mdo_insert_tx_tag,
	.needed_headroom = TJA11XX_TLV_TX_NEEDED_HEADROOM,
	.needed_tailroom = TJA11XX_TLV_NEEDED_TAILROOM,
};

int nxp_c45_macsec_config_init(struct phy_device *phydev)
{
	struct nxp_c45_phy *priv = phydev->priv;
	int ret;

	if (!priv->macsec)
		return 0;

	ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_PORT_FUNC_ENABLES,
			       MACSEC_EN | ADAPTER_EN);
	if (ret)
		return ret;

	ret = nxp_c45_macsec_write(phydev, ADPTR_CNTRL, ADPTR_CNTRL_CONFIG_EN |
				   ADPTR_CNTRL_ADPTR_EN);
	if (ret)
		return ret;

	ret = nxp_c45_macsec_write(phydev, ADPTR_TX_TAG_CNTRL,
				   ADPTR_TX_TAG_CNTRL_ENA);
	if (ret)
		return ret;

	ret = nxp_c45_macsec_write(phydev, ADPTR_CNTRL, ADPTR_CNTRL_ADPTR_EN);
	if (ret)
		return ret;

	ret = nxp_c45_macsec_write(phydev, MACSEC_TPNET, PN_WRAP_THRESHOLD);
	if (ret)
		return ret;

	/* Set MKA filter. */
	ret = nxp_c45_macsec_write(phydev, MACSEC_UPFR0D2, ETH_P_PAE);
	if (ret)
		return ret;

	ret = nxp_c45_macsec_write(phydev, MACSEC_UPFR0M1, MACSEC_OVP);
	if (ret)
		return ret;

	ret = nxp_c45_macsec_write(phydev, MACSEC_UPFR0M2, ETYPE_MASK);
	if (ret)
		return ret;

	ret = nxp_c45_macsec_write(phydev, MACSEC_UPFR0R, MACSEC_UPFR_EN);

	return ret;
}

int nxp_c45_macsec_probe(struct phy_device *phydev)
{
	struct nxp_c45_phy *priv = phydev->priv;
	struct device *dev = &phydev->mdio.dev;

	priv->macsec = devm_kzalloc(dev, sizeof(*priv->macsec), GFP_KERNEL);
	if (!priv->macsec)
		return -ENOMEM;

	INIT_LIST_HEAD(&priv->macsec->secy_list);
	phydev->macsec_ops = &nxp_c45_macsec_ops;

	return 0;
}

void nxp_c45_macsec_remove(struct phy_device *phydev)
{
	struct nxp_c45_phy *priv = phydev->priv;
	struct nxp_c45_secy *secy_p, *secy_t;
	struct nxp_c45_sa *sa_p, *sa_t;
	struct list_head *secy_list;

	if (!priv->macsec)
		return;

	secy_list = &priv->macsec->secy_list;
	nxp_c45_macsec_en(phydev, false);

	list_for_each_entry_safe(secy_p, secy_t, secy_list, list) {
		list_for_each_entry_safe(sa_p, sa_t, &secy_p->sa_list, list)
			nxp_c45_sa_free(sa_p);
		nxp_c45_secy_free(secy_p);
	}
}

void nxp_c45_handle_macsec_interrupt(struct phy_device *phydev,
				     irqreturn_t *ret)
{
	struct nxp_c45_phy *priv = phydev->priv;
	struct nxp_c45_secy *secy;
	struct nxp_c45_sa *sa;
	u8 encoding_sa;
	int secy_id;
	u32 reg = 0;

	if (!priv->macsec)
		return;

	do {
		nxp_c45_macsec_read(phydev, MACSEC_EVR, &reg);
		if (!reg)
			return;

		secy_id = MACSEC_REG_SIZE - ffs(reg);
		secy = nxp_c45_find_secy_by_id(&priv->macsec->secy_list,
					       secy_id);
		if (IS_ERR(secy)) {
			WARN_ON(1);
			goto macsec_ack_irq;
		}

		encoding_sa = secy->secy->tx_sc.encoding_sa;
		phydev_dbg(phydev, "pn_wrapped: TX SC %d, encoding_sa %u\n",
			   secy->secy_id, encoding_sa);

		sa = nxp_c45_find_sa(&secy->sa_list, TX_SA, encoding_sa);
		if (!IS_ERR(sa))
			macsec_pn_wrapped(secy->secy, sa->sa);
		else
			WARN_ON(1);

macsec_ack_irq:
		nxp_c45_macsec_write(phydev, MACSEC_EVR,
				     TX_SC_BIT(secy_id));
		*ret = IRQ_HANDLED;
	} while (reg);
}
