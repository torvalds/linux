// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2016 MediaTek Inc.
 * Author: Yong Wu <yong.wu@mediatek.com>
 */
#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/soc/mediatek/mtk_sip_svc.h>
#include <soc/mediatek/smi.h>
#include <dt-bindings/memory/mt2701-larb-port.h>
#include <dt-bindings/memory/mtk-memory-port.h>

/* SMI COMMON */
#define SMI_L1LEN			0x100

#define SMI_L1_ARB			0x200
#define SMI_BUS_SEL			0x220
#define SMI_BUS_LARB_SHIFT(larbid)	((larbid) << 1)
/* All are MMU0 defaultly. Only specialize mmu1 here. */
#define F_MMU1_LARB(larbid)		(0x1 << SMI_BUS_LARB_SHIFT(larbid))

#define SMI_READ_FIFO_TH		0x230
#define SMI_M4U_TH			0x234
#define SMI_FIFO_TH1			0x238
#define SMI_FIFO_TH2			0x23c
#define SMI_DCM				0x300
#define SMI_DUMMY			0x444

/* SMI LARB */
#define SMI_LARB_SLP_CON                0xc
#define SLP_PROT_EN                     BIT(0)
#define SLP_PROT_RDY                    BIT(16)

#define SMI_LARB_CMD_THRT_CON		0x24
#define SMI_LARB_THRT_RD_NU_LMT_MSK	GENMASK(7, 4)
#define SMI_LARB_THRT_RD_NU_LMT		(5 << 4)

#define SMI_LARB_SW_FLAG		0x40
#define SMI_LARB_SW_FLAG_1		0x1

#define SMI_LARB_OSTDL_PORT		0x200
#define SMI_LARB_OSTDL_PORTx(id)	(SMI_LARB_OSTDL_PORT + (((id) & 0x1f) << 2))

/* Below are about mmu enable registers, they are different in SoCs */
/* gen1: mt2701 */
#define REG_SMI_SECUR_CON_BASE		0x5c0

/* every register control 8 port, register offset 0x4 */
#define REG_SMI_SECUR_CON_OFFSET(id)	(((id) >> 3) << 2)
#define REG_SMI_SECUR_CON_ADDR(id)	\
	(REG_SMI_SECUR_CON_BASE + REG_SMI_SECUR_CON_OFFSET(id))

/*
 * every port have 4 bit to control, bit[port + 3] control virtual or physical,
 * bit[port + 2 : port + 1] control the domain, bit[port] control the security
 * or non-security.
 */
#define SMI_SECUR_CON_VAL_MSK(id)	(~(0xf << (((id) & 0x7) << 2)))
#define SMI_SECUR_CON_VAL_VIRT(id)	BIT((((id) & 0x7) << 2) + 3)
/* mt2701 domain should be set to 3 */
#define SMI_SECUR_CON_VAL_DOMAIN(id)	(0x3 << ((((id) & 0x7) << 2) + 1))

/* gen2: */
/* mt8167 */
#define MT8167_SMI_LARB_MMU_EN		0xfc0

/* mt8173 */
#define MT8173_SMI_LARB_MMU_EN		0xf00

/* general */
#define SMI_LARB_NONSEC_CON(id)		(0x380 + ((id) * 4))
#define F_MMU_EN			BIT(0)
#define BANK_SEL(id)			({		\
	u32 _id = (id) & 0x3;				\
	(_id << 8 | _id << 10 | _id << 12 | _id << 14);	\
})

#define SMI_COMMON_INIT_REGS_NR		6
#define SMI_LARB_PORT_NR_MAX		32

#define MTK_SMI_FLAG_THRT_UPDATE	BIT(0)
#define MTK_SMI_FLAG_SW_FLAG		BIT(1)
#define MTK_SMI_FLAG_SLEEP_CTL		BIT(2)
#define MTK_SMI_FLAG_CFG_PORT_SEC_CTL	BIT(3)
#define MTK_SMI_CAPS(flags, _x)		(!!((flags) & (_x)))

struct mtk_smi_reg_pair {
	unsigned int		offset;
	u32			value;
};

enum mtk_smi_type {
	MTK_SMI_GEN1,
	MTK_SMI_GEN2,		/* gen2 smi common */
	MTK_SMI_GEN2_SUB_COMM,	/* gen2 smi sub common */
};

/* larbs: Require apb/smi clocks while gals is optional. */
static const char * const mtk_smi_larb_clks[] = {"apb", "smi", "gals"};
#define MTK_SMI_LARB_REQ_CLK_NR		2
#define MTK_SMI_LARB_OPT_CLK_NR		1

/*
 * common: Require these four clocks in has_gals case. Otherwise, only apb/smi are required.
 * sub common: Require apb/smi/gals0 clocks in has_gals case. Otherwise, only apb/smi are required.
 */
static const char * const mtk_smi_common_clks[] = {"apb", "smi", "gals0", "gals1"};
#define MTK_SMI_CLK_NR_MAX		ARRAY_SIZE(mtk_smi_common_clks)
#define MTK_SMI_COM_REQ_CLK_NR		2
#define MTK_SMI_COM_GALS_REQ_CLK_NR	MTK_SMI_CLK_NR_MAX
#define MTK_SMI_SUB_COM_GALS_REQ_CLK_NR 3

struct mtk_smi_common_plat {
	enum mtk_smi_type	type;
	bool			has_gals;
	u32			bus_sel; /* Balance some larbs to enter mmu0 or mmu1 */

	const struct mtk_smi_reg_pair	*init;
};

struct mtk_smi_larb_gen {
	int port_in_larb[MTK_LARB_NR_MAX + 1];
	int				(*config_port)(struct device *dev);
	unsigned int			larb_direct_to_common_mask;
	unsigned int			flags_general;
	const u8			(*ostd)[SMI_LARB_PORT_NR_MAX];
};

struct mtk_smi {
	struct device			*dev;
	unsigned int			clk_num;
	struct clk_bulk_data		clks[MTK_SMI_CLK_NR_MAX];
	struct clk			*clk_async; /*only needed by mt2701*/
	union {
		void __iomem		*smi_ao_base; /* only for gen1 */
		void __iomem		*base;	      /* only for gen2 */
	};
	struct device			*smi_common_dev; /* for sub common */
	const struct mtk_smi_common_plat *plat;
};

struct mtk_smi_larb { /* larb: local arbiter */
	struct mtk_smi			smi;
	void __iomem			*base;
	struct device			*smi_common_dev; /* common or sub-common dev */
	const struct mtk_smi_larb_gen	*larb_gen;
	int				larbid;
	u32				*mmu;
	unsigned char			*bank;
};

static int
mtk_smi_larb_bind(struct device *dev, struct device *master, void *data)
{
	struct mtk_smi_larb *larb = dev_get_drvdata(dev);
	struct mtk_smi_larb_iommu *larb_mmu = data;
	unsigned int         i;

	for (i = 0; i < MTK_LARB_NR_MAX; i++) {
		if (dev == larb_mmu[i].dev) {
			larb->larbid = i;
			larb->mmu = &larb_mmu[i].mmu;
			larb->bank = larb_mmu[i].bank;
			return 0;
		}
	}
	return -ENODEV;
}

static void
mtk_smi_larb_unbind(struct device *dev, struct device *master, void *data)
{
	/* Do nothing as the iommu is always enabled. */
}

static const struct component_ops mtk_smi_larb_component_ops = {
	.bind = mtk_smi_larb_bind,
	.unbind = mtk_smi_larb_unbind,
};

static int mtk_smi_larb_config_port_gen1(struct device *dev)
{
	struct mtk_smi_larb *larb = dev_get_drvdata(dev);
	const struct mtk_smi_larb_gen *larb_gen = larb->larb_gen;
	struct mtk_smi *common = dev_get_drvdata(larb->smi_common_dev);
	int i, m4u_port_id, larb_port_num;
	u32 sec_con_val, reg_val;

	m4u_port_id = larb_gen->port_in_larb[larb->larbid];
	larb_port_num = larb_gen->port_in_larb[larb->larbid + 1]
			- larb_gen->port_in_larb[larb->larbid];

	for (i = 0; i < larb_port_num; i++, m4u_port_id++) {
		if (*larb->mmu & BIT(i)) {
			/* bit[port + 3] controls the virtual or physical */
			sec_con_val = SMI_SECUR_CON_VAL_VIRT(m4u_port_id);
		} else {
			/* do not need to enable m4u for this port */
			continue;
		}
		reg_val = readl(common->smi_ao_base
			+ REG_SMI_SECUR_CON_ADDR(m4u_port_id));
		reg_val &= SMI_SECUR_CON_VAL_MSK(m4u_port_id);
		reg_val |= sec_con_val;
		reg_val |= SMI_SECUR_CON_VAL_DOMAIN(m4u_port_id);
		writel(reg_val,
			common->smi_ao_base
			+ REG_SMI_SECUR_CON_ADDR(m4u_port_id));
	}
	return 0;
}

static int mtk_smi_larb_config_port_mt8167(struct device *dev)
{
	struct mtk_smi_larb *larb = dev_get_drvdata(dev);

	writel(*larb->mmu, larb->base + MT8167_SMI_LARB_MMU_EN);
	return 0;
}

static int mtk_smi_larb_config_port_mt8173(struct device *dev)
{
	struct mtk_smi_larb *larb = dev_get_drvdata(dev);

	writel(*larb->mmu, larb->base + MT8173_SMI_LARB_MMU_EN);
	return 0;
}

static int mtk_smi_larb_config_port_gen2_general(struct device *dev)
{
	struct mtk_smi_larb *larb = dev_get_drvdata(dev);
	u32 reg, flags_general = larb->larb_gen->flags_general;
	const u8 *larbostd = larb->larb_gen->ostd ? larb->larb_gen->ostd[larb->larbid] : NULL;
	struct arm_smccc_res res;
	int i;

	if (BIT(larb->larbid) & larb->larb_gen->larb_direct_to_common_mask)
		return 0;

	if (MTK_SMI_CAPS(flags_general, MTK_SMI_FLAG_THRT_UPDATE)) {
		reg = readl_relaxed(larb->base + SMI_LARB_CMD_THRT_CON);
		reg &= ~SMI_LARB_THRT_RD_NU_LMT_MSK;
		reg |= SMI_LARB_THRT_RD_NU_LMT;
		writel_relaxed(reg, larb->base + SMI_LARB_CMD_THRT_CON);
	}

	if (MTK_SMI_CAPS(flags_general, MTK_SMI_FLAG_SW_FLAG))
		writel_relaxed(SMI_LARB_SW_FLAG_1, larb->base + SMI_LARB_SW_FLAG);

	for (i = 0; i < SMI_LARB_PORT_NR_MAX && larbostd && !!larbostd[i]; i++)
		writel_relaxed(larbostd[i], larb->base + SMI_LARB_OSTDL_PORTx(i));

	/*
	 * When mmu_en bits are in security world, the bank_sel still is in the
	 * LARB_NONSEC_CON below. And the mmu_en bits of LARB_NONSEC_CON have no
	 * effect in this case.
	 */
	if (MTK_SMI_CAPS(flags_general, MTK_SMI_FLAG_CFG_PORT_SEC_CTL)) {
		arm_smccc_smc(MTK_SIP_KERNEL_IOMMU_CONTROL, IOMMU_ATF_CMD_CONFIG_SMI_LARB,
			      larb->larbid, *larb->mmu, 0, 0, 0, 0, &res);
		if (res.a0 != 0) {
			dev_err(dev, "Enable iommu fail, ret %ld\n", res.a0);
			return -EINVAL;
		}
	}

	for_each_set_bit(i, (unsigned long *)larb->mmu, 32) {
		reg = readl_relaxed(larb->base + SMI_LARB_NONSEC_CON(i));
		reg |= F_MMU_EN;
		reg |= BANK_SEL(larb->bank[i]);
		writel(reg, larb->base + SMI_LARB_NONSEC_CON(i));
	}
	return 0;
}

static const u8 mtk_smi_larb_mt8188_ostd[][SMI_LARB_PORT_NR_MAX] = {
	[0] = {0x02, 0x18, 0x22, 0x22, 0x01, 0x02, 0x0a,},
	[1] = {0x12, 0x02, 0x14, 0x14, 0x01, 0x18, 0x0a,},
	[2] = {0x12, 0x12, 0x12, 0x12, 0x0a,},
	[3] = {0x12, 0x12, 0x12, 0x12, 0x28, 0x28, 0x0a,},
	[4] = {0x06, 0x01, 0x17, 0x06, 0x0a, 0x07, 0x07,},
	[5] = {0x02, 0x01, 0x04, 0x02, 0x06, 0x01, 0x06, 0x0a,},
	[6] = {0x06, 0x01, 0x06, 0x0a,},
	[7] = {0x0c, 0x0c, 0x12,},
	[8] = {0x0c, 0x01, 0x0a, 0x05, 0x02, 0x03, 0x01, 0x01, 0x14, 0x14,
	       0x0a, 0x14, 0x1e, 0x01, 0x0c, 0x0a, 0x05, 0x02, 0x02, 0x05,
	       0x03, 0x01, 0x1e, 0x01, 0x05,},
	[9] = {0x1e, 0x01, 0x0a, 0x0a, 0x01, 0x01, 0x03, 0x1e, 0x1e, 0x10,
	       0x07, 0x01, 0x0a, 0x06, 0x03, 0x03, 0x0e, 0x01, 0x04, 0x28,},
	[10] = {0x03, 0x20, 0x01, 0x20, 0x01, 0x01, 0x14, 0x0a, 0x0a, 0x0c,
		0x0a, 0x05, 0x02, 0x03, 0x02, 0x14, 0x0a, 0x0a, 0x14, 0x14,
		0x14, 0x01, 0x01, 0x14, 0x1e, 0x01, 0x05, 0x03, 0x02, 0x28,},
	[11] = {0x03, 0x20, 0x01, 0x20, 0x01, 0x01, 0x14, 0x0a, 0x0a, 0x0c,
		0x0a, 0x05, 0x02, 0x03, 0x02, 0x14, 0x0a, 0x0a, 0x14, 0x14,
		0x14, 0x01, 0x01, 0x14, 0x1e, 0x01, 0x05, 0x03, 0x02, 0x28,},
	[12] = {0x03, 0x20, 0x01, 0x20, 0x01, 0x01, 0x14, 0x0a, 0x0a, 0x0c,
		0x0a, 0x05, 0x02, 0x03, 0x02, 0x14, 0x0a, 0x0a, 0x14, 0x14,
		0x14, 0x01, 0x01, 0x14, 0x1e, 0x01, 0x05, 0x03, 0x02, 0x28,},
	[13] = {0x07, 0x02, 0x04, 0x02, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
		0x07, 0x02, 0x04, 0x02, 0x05, 0x05,},
	[14] = {0x02, 0x02, 0x0c, 0x0c, 0x0c, 0x0c, 0x01, 0x01, 0x02, 0x02,
		0x02, 0x02, 0x0c, 0x0c, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
		0x02, 0x02, 0x01, 0x01,},
	[15] = {0x0c, 0x0c, 0x02, 0x02, 0x02, 0x02, 0x01, 0x01, 0x0c, 0x0c,
		0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x01, 0x02,
		0x0c, 0x01, 0x01,},
	[16] = {0x28, 0x28, 0x03, 0x01, 0x01, 0x03, 0x14, 0x14, 0x0a, 0x0d,
		0x03, 0x05, 0x0e, 0x01, 0x01, 0x05, 0x06, 0x0d, 0x01,},
	[17] = {0x28, 0x02, 0x02, 0x12, 0x02, 0x12, 0x10, 0x02, 0x02, 0x0a,
		0x12, 0x02, 0x02, 0x0a, 0x16, 0x02, 0x04,},
	[18] = {0x28, 0x02, 0x02, 0x12, 0x02, 0x12, 0x10, 0x02, 0x02, 0x0a,
		0x12, 0x02, 0x02, 0x0a, 0x16, 0x02, 0x04,},
	[19] = {0x1a, 0x0e, 0x0a, 0x0a, 0x0c, 0x0e, 0x10,},
	[20] = {0x1a, 0x0e, 0x0a, 0x0a, 0x0c, 0x0e, 0x10,},
	[21] = {0x01, 0x04, 0x01, 0x01, 0x01, 0x01, 0x01, 0x04, 0x04, 0x01,
		0x01, 0x01, 0x04, 0x0a, 0x06, 0x01, 0x01, 0x01, 0x0a, 0x06,
		0x01, 0x01, 0x05, 0x03, 0x03, 0x04, 0x01,},
	[22] = {0x28, 0x19, 0x0c, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x04,
		0x01,},
	[23] = {0x01, 0x01, 0x04, 0x01, 0x01, 0x01, 0x18, 0x01, 0x01,},
	[24] = {0x12, 0x06, 0x12, 0x06,},
	[25] = {0x01},
};

static const u8 mtk_smi_larb_mt8195_ostd[][SMI_LARB_PORT_NR_MAX] = {
	[0] = {0x0a, 0xc, 0x22, 0x22, 0x01, 0x0a,}, /* larb0 */
	[1] = {0x0a, 0xc, 0x22, 0x22, 0x01, 0x0a,}, /* larb1 */
	[2] = {0x12, 0x12, 0x12, 0x12, 0x0a,},      /* ... */
	[3] = {0x12, 0x12, 0x12, 0x12, 0x28, 0x28, 0x0a,},
	[4] = {0x06, 0x01, 0x17, 0x06, 0x0a,},
	[5] = {0x06, 0x01, 0x17, 0x06, 0x06, 0x01, 0x06, 0x0a,},
	[6] = {0x06, 0x01, 0x06, 0x0a,},
	[7] = {0x0c, 0x0c, 0x12,},
	[8] = {0x0c, 0x0c, 0x12,},
	[9] = {0x0a, 0x08, 0x04, 0x06, 0x01, 0x01, 0x10, 0x18, 0x11, 0x0a,
		0x08, 0x04, 0x11, 0x06, 0x02, 0x06, 0x01, 0x11, 0x11, 0x06,},
	[10] = {0x18, 0x08, 0x01, 0x01, 0x20, 0x12, 0x18, 0x06, 0x05, 0x10,
		0x08, 0x08, 0x10, 0x08, 0x08, 0x18, 0x0c, 0x09, 0x0b, 0x0d,
		0x0d, 0x06, 0x10, 0x10,},
	[11] = {0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x01, 0x01, 0x01, 0x01,},
	[12] = {0x09, 0x09, 0x05, 0x05, 0x0c, 0x18, 0x02, 0x02, 0x04, 0x02,},
	[13] = {0x02, 0x02, 0x12, 0x12, 0x02, 0x02, 0x02, 0x02, 0x08, 0x01,},
	[14] = {0x12, 0x12, 0x02, 0x02, 0x02, 0x02, 0x16, 0x01, 0x16, 0x01,
		0x01, 0x02, 0x02, 0x08, 0x02,},
	[15] = {},
	[16] = {0x28, 0x02, 0x02, 0x12, 0x02, 0x12, 0x10, 0x02, 0x02, 0x0a,
		0x12, 0x02, 0x0a, 0x16, 0x02, 0x04,},
	[17] = {0x1a, 0x0e, 0x0a, 0x0a, 0x0c, 0x0e, 0x10,},
	[18] = {0x12, 0x06, 0x12, 0x06,},
	[19] = {0x01, 0x04, 0x01, 0x01, 0x01, 0x01, 0x01, 0x04, 0x04, 0x01,
		0x01, 0x01, 0x04, 0x0a, 0x06, 0x01, 0x01, 0x01, 0x0a, 0x06,
		0x01, 0x01, 0x05, 0x03, 0x03, 0x04, 0x01,},
	[20] = {0x01, 0x04, 0x01, 0x01, 0x01, 0x01, 0x01, 0x04, 0x04, 0x01,
		0x01, 0x01, 0x04, 0x0a, 0x06, 0x01, 0x01, 0x01, 0x0a, 0x06,
		0x01, 0x01, 0x05, 0x03, 0x03, 0x04, 0x01,},
	[21] = {0x28, 0x19, 0x0c, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x04,},
	[22] = {0x28, 0x19, 0x0c, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x04,},
	[23] = {0x18, 0x01,},
	[24] = {0x01, 0x01, 0x04, 0x01, 0x01, 0x01, 0x01, 0x01, 0x04, 0x01,
		0x01, 0x01,},
	[25] = {0x02, 0x02, 0x02, 0x28, 0x16, 0x02, 0x02, 0x02, 0x12, 0x16,
		0x02, 0x01,},
	[26] = {0x02, 0x02, 0x02, 0x28, 0x16, 0x02, 0x02, 0x02, 0x12, 0x16,
		0x02, 0x01,},
	[27] = {0x02, 0x02, 0x02, 0x28, 0x16, 0x02, 0x02, 0x02, 0x12, 0x16,
		0x02, 0x01,},
	[28] = {0x1a, 0x0e, 0x0a, 0x0a, 0x0c, 0x0e, 0x10,},
};

static const struct mtk_smi_larb_gen mtk_smi_larb_mt2701 = {
	.port_in_larb = {
		LARB0_PORT_OFFSET, LARB1_PORT_OFFSET,
		LARB2_PORT_OFFSET, LARB3_PORT_OFFSET
	},
	.config_port = mtk_smi_larb_config_port_gen1,
};

static const struct mtk_smi_larb_gen mtk_smi_larb_mt2712 = {
	.config_port                = mtk_smi_larb_config_port_gen2_general,
	.larb_direct_to_common_mask = BIT(8) | BIT(9),      /* bdpsys */
};

static const struct mtk_smi_larb_gen mtk_smi_larb_mt6779 = {
	.config_port  = mtk_smi_larb_config_port_gen2_general,
	.larb_direct_to_common_mask =
		BIT(4) | BIT(6) | BIT(11) | BIT(12) | BIT(13),
		/* DUMMY | IPU0 | IPU1 | CCU | MDLA */
};

static const struct mtk_smi_larb_gen mtk_smi_larb_mt8167 = {
	/* mt8167 do not need the port in larb */
	.config_port = mtk_smi_larb_config_port_mt8167,
};

static const struct mtk_smi_larb_gen mtk_smi_larb_mt8173 = {
	/* mt8173 do not need the port in larb */
	.config_port = mtk_smi_larb_config_port_mt8173,
};

static const struct mtk_smi_larb_gen mtk_smi_larb_mt8183 = {
	.config_port                = mtk_smi_larb_config_port_gen2_general,
	.larb_direct_to_common_mask = BIT(2) | BIT(3) | BIT(7),
				      /* IPU0 | IPU1 | CCU */
};

static const struct mtk_smi_larb_gen mtk_smi_larb_mt8186 = {
	.config_port                = mtk_smi_larb_config_port_gen2_general,
	.flags_general	            = MTK_SMI_FLAG_SLEEP_CTL,
};

static const struct mtk_smi_larb_gen mtk_smi_larb_mt8188 = {
	.config_port                = mtk_smi_larb_config_port_gen2_general,
	.flags_general	            = MTK_SMI_FLAG_THRT_UPDATE | MTK_SMI_FLAG_SW_FLAG |
				      MTK_SMI_FLAG_SLEEP_CTL | MTK_SMI_FLAG_CFG_PORT_SEC_CTL,
	.ostd		            = mtk_smi_larb_mt8188_ostd,
};

static const struct mtk_smi_larb_gen mtk_smi_larb_mt8192 = {
	.config_port                = mtk_smi_larb_config_port_gen2_general,
};

static const struct mtk_smi_larb_gen mtk_smi_larb_mt8195 = {
	.config_port                = mtk_smi_larb_config_port_gen2_general,
	.flags_general	            = MTK_SMI_FLAG_THRT_UPDATE | MTK_SMI_FLAG_SW_FLAG |
				      MTK_SMI_FLAG_SLEEP_CTL,
	.ostd		            = mtk_smi_larb_mt8195_ostd,
};

static const struct of_device_id mtk_smi_larb_of_ids[] = {
	{.compatible = "mediatek,mt2701-smi-larb", .data = &mtk_smi_larb_mt2701},
	{.compatible = "mediatek,mt2712-smi-larb", .data = &mtk_smi_larb_mt2712},
	{.compatible = "mediatek,mt6779-smi-larb", .data = &mtk_smi_larb_mt6779},
	{.compatible = "mediatek,mt6795-smi-larb", .data = &mtk_smi_larb_mt8173},
	{.compatible = "mediatek,mt8167-smi-larb", .data = &mtk_smi_larb_mt8167},
	{.compatible = "mediatek,mt8173-smi-larb", .data = &mtk_smi_larb_mt8173},
	{.compatible = "mediatek,mt8183-smi-larb", .data = &mtk_smi_larb_mt8183},
	{.compatible = "mediatek,mt8186-smi-larb", .data = &mtk_smi_larb_mt8186},
	{.compatible = "mediatek,mt8188-smi-larb", .data = &mtk_smi_larb_mt8188},
	{.compatible = "mediatek,mt8192-smi-larb", .data = &mtk_smi_larb_mt8192},
	{.compatible = "mediatek,mt8195-smi-larb", .data = &mtk_smi_larb_mt8195},
	{}
};
MODULE_DEVICE_TABLE(of, mtk_smi_larb_of_ids);

static int mtk_smi_larb_sleep_ctrl_enable(struct mtk_smi_larb *larb)
{
	int ret;
	u32 tmp;

	writel_relaxed(SLP_PROT_EN, larb->base + SMI_LARB_SLP_CON);
	ret = readl_poll_timeout_atomic(larb->base + SMI_LARB_SLP_CON,
					tmp, !!(tmp & SLP_PROT_RDY), 10, 1000);
	if (ret) {
		/* TODO: Reset this larb if it fails here. */
		dev_err(larb->smi.dev, "sleep ctrl is not ready(0x%x).\n", tmp);
	}
	return ret;
}

static void mtk_smi_larb_sleep_ctrl_disable(struct mtk_smi_larb *larb)
{
	writel_relaxed(0, larb->base + SMI_LARB_SLP_CON);
}

static int mtk_smi_device_link_common(struct device *dev, struct device **com_dev)
{
	struct platform_device *smi_com_pdev;
	struct device_node *smi_com_node;
	struct device *smi_com_dev;
	struct device_link *link;

	smi_com_node = of_parse_phandle(dev->of_node, "mediatek,smi", 0);
	if (!smi_com_node)
		return -EINVAL;

	smi_com_pdev = of_find_device_by_node(smi_com_node);
	of_node_put(smi_com_node);
	if (smi_com_pdev) {
		/* smi common is the supplier, Make sure it is ready before */
		if (!platform_get_drvdata(smi_com_pdev)) {
			put_device(&smi_com_pdev->dev);
			return -EPROBE_DEFER;
		}
		smi_com_dev = &smi_com_pdev->dev;
		link = device_link_add(dev, smi_com_dev,
				       DL_FLAG_PM_RUNTIME | DL_FLAG_STATELESS);
		if (!link) {
			dev_err(dev, "Unable to link smi-common dev\n");
			put_device(&smi_com_pdev->dev);
			return -ENODEV;
		}
		*com_dev = smi_com_dev;
	} else {
		dev_err(dev, "Failed to get the smi_common device\n");
		return -EINVAL;
	}
	return 0;
}

static int mtk_smi_dts_clk_init(struct device *dev, struct mtk_smi *smi,
				const char * const clks[],
				unsigned int clk_nr_required,
				unsigned int clk_nr_optional)
{
	int i, ret;

	for (i = 0; i < clk_nr_required; i++)
		smi->clks[i].id = clks[i];
	ret = devm_clk_bulk_get(dev, clk_nr_required, smi->clks);
	if (ret)
		return ret;

	for (i = clk_nr_required; i < clk_nr_required + clk_nr_optional; i++)
		smi->clks[i].id = clks[i];
	ret = devm_clk_bulk_get_optional(dev, clk_nr_optional,
					 smi->clks + clk_nr_required);
	smi->clk_num = clk_nr_required + clk_nr_optional;
	return ret;
}

static int mtk_smi_larb_probe(struct platform_device *pdev)
{
	struct mtk_smi_larb *larb;
	struct device *dev = &pdev->dev;
	int ret;

	larb = devm_kzalloc(dev, sizeof(*larb), GFP_KERNEL);
	if (!larb)
		return -ENOMEM;

	larb->larb_gen = of_device_get_match_data(dev);
	larb->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(larb->base))
		return PTR_ERR(larb->base);

	ret = mtk_smi_dts_clk_init(dev, &larb->smi, mtk_smi_larb_clks,
				   MTK_SMI_LARB_REQ_CLK_NR, MTK_SMI_LARB_OPT_CLK_NR);
	if (ret)
		return ret;

	larb->smi.dev = dev;

	ret = mtk_smi_device_link_common(dev, &larb->smi_common_dev);
	if (ret < 0)
		return ret;

	pm_runtime_enable(dev);
	platform_set_drvdata(pdev, larb);
	ret = component_add(dev, &mtk_smi_larb_component_ops);
	if (ret)
		goto err_pm_disable;
	return 0;

err_pm_disable:
	pm_runtime_disable(dev);
	device_link_remove(dev, larb->smi_common_dev);
	return ret;
}

static void mtk_smi_larb_remove(struct platform_device *pdev)
{
	struct mtk_smi_larb *larb = platform_get_drvdata(pdev);

	device_link_remove(&pdev->dev, larb->smi_common_dev);
	pm_runtime_disable(&pdev->dev);
	component_del(&pdev->dev, &mtk_smi_larb_component_ops);
}

static int __maybe_unused mtk_smi_larb_resume(struct device *dev)
{
	struct mtk_smi_larb *larb = dev_get_drvdata(dev);
	const struct mtk_smi_larb_gen *larb_gen = larb->larb_gen;
	int ret;

	ret = clk_bulk_prepare_enable(larb->smi.clk_num, larb->smi.clks);
	if (ret)
		return ret;

	if (MTK_SMI_CAPS(larb->larb_gen->flags_general, MTK_SMI_FLAG_SLEEP_CTL))
		mtk_smi_larb_sleep_ctrl_disable(larb);

	/* Configure the basic setting for this larb */
	return larb_gen->config_port(dev);
}

static int __maybe_unused mtk_smi_larb_suspend(struct device *dev)
{
	struct mtk_smi_larb *larb = dev_get_drvdata(dev);
	int ret;

	if (MTK_SMI_CAPS(larb->larb_gen->flags_general, MTK_SMI_FLAG_SLEEP_CTL)) {
		ret = mtk_smi_larb_sleep_ctrl_enable(larb);
		if (ret)
			return ret;
	}

	clk_bulk_disable_unprepare(larb->smi.clk_num, larb->smi.clks);
	return 0;
}

static const struct dev_pm_ops smi_larb_pm_ops = {
	SET_RUNTIME_PM_OPS(mtk_smi_larb_suspend, mtk_smi_larb_resume, NULL)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				     pm_runtime_force_resume)
};

static struct platform_driver mtk_smi_larb_driver = {
	.probe	= mtk_smi_larb_probe,
	.remove_new = mtk_smi_larb_remove,
	.driver	= {
		.name = "mtk-smi-larb",
		.of_match_table = mtk_smi_larb_of_ids,
		.pm             = &smi_larb_pm_ops,
	}
};

static const struct mtk_smi_reg_pair mtk_smi_common_mt6795_init[SMI_COMMON_INIT_REGS_NR] = {
	{SMI_L1_ARB, 0x1b},
	{SMI_M4U_TH, 0xce810c85},
	{SMI_FIFO_TH1, 0x43214c8},
	{SMI_READ_FIFO_TH, 0x191f},
};

static const struct mtk_smi_reg_pair mtk_smi_common_mt8195_init[SMI_COMMON_INIT_REGS_NR] = {
	{SMI_L1LEN, 0xb},
	{SMI_M4U_TH, 0xe100e10},
	{SMI_FIFO_TH1, 0x506090a},
	{SMI_FIFO_TH2, 0x506090a},
	{SMI_DCM, 0x4f1},
	{SMI_DUMMY, 0x1},
};

static const struct mtk_smi_common_plat mtk_smi_common_gen1 = {
	.type     = MTK_SMI_GEN1,
};

static const struct mtk_smi_common_plat mtk_smi_common_gen2 = {
	.type	  = MTK_SMI_GEN2,
};

static const struct mtk_smi_common_plat mtk_smi_common_mt6779 = {
	.type	  = MTK_SMI_GEN2,
	.has_gals = true,
	.bus_sel  = F_MMU1_LARB(1) | F_MMU1_LARB(2) | F_MMU1_LARB(4) |
		    F_MMU1_LARB(5) | F_MMU1_LARB(6) | F_MMU1_LARB(7),
};

static const struct mtk_smi_common_plat mtk_smi_common_mt6795 = {
	.type	  = MTK_SMI_GEN2,
	.bus_sel  = F_MMU1_LARB(0),
	.init     = mtk_smi_common_mt6795_init,
};

static const struct mtk_smi_common_plat mtk_smi_common_mt8183 = {
	.type     = MTK_SMI_GEN2,
	.has_gals = true,
	.bus_sel  = F_MMU1_LARB(1) | F_MMU1_LARB(2) | F_MMU1_LARB(5) |
		    F_MMU1_LARB(7),
};

static const struct mtk_smi_common_plat mtk_smi_common_mt8186 = {
	.type     = MTK_SMI_GEN2,
	.has_gals = true,
	.bus_sel  = F_MMU1_LARB(1) | F_MMU1_LARB(4) | F_MMU1_LARB(7),
};

static const struct mtk_smi_common_plat mtk_smi_common_mt8188_vdo = {
	.type     = MTK_SMI_GEN2,
	.bus_sel  = F_MMU1_LARB(1) | F_MMU1_LARB(5) | F_MMU1_LARB(7),
	.init     = mtk_smi_common_mt8195_init,
};

static const struct mtk_smi_common_plat mtk_smi_common_mt8188_vpp = {
	.type     = MTK_SMI_GEN2,
	.bus_sel  = F_MMU1_LARB(1) | F_MMU1_LARB(2) | F_MMU1_LARB(7),
	.init     = mtk_smi_common_mt8195_init,
};

static const struct mtk_smi_common_plat mtk_smi_common_mt8192 = {
	.type     = MTK_SMI_GEN2,
	.has_gals = true,
	.bus_sel  = F_MMU1_LARB(1) | F_MMU1_LARB(2) | F_MMU1_LARB(5) |
		    F_MMU1_LARB(6),
};

static const struct mtk_smi_common_plat mtk_smi_common_mt8195_vdo = {
	.type     = MTK_SMI_GEN2,
	.has_gals = true,
	.bus_sel  = F_MMU1_LARB(1) | F_MMU1_LARB(3) | F_MMU1_LARB(5) |
		    F_MMU1_LARB(7),
	.init     = mtk_smi_common_mt8195_init,
};

static const struct mtk_smi_common_plat mtk_smi_common_mt8195_vpp = {
	.type     = MTK_SMI_GEN2,
	.has_gals = true,
	.bus_sel  = F_MMU1_LARB(1) | F_MMU1_LARB(2) | F_MMU1_LARB(7),
	.init     = mtk_smi_common_mt8195_init,
};

static const struct mtk_smi_common_plat mtk_smi_sub_common_mt8195 = {
	.type     = MTK_SMI_GEN2_SUB_COMM,
	.has_gals = true,
};

static const struct mtk_smi_common_plat mtk_smi_common_mt8365 = {
	.type     = MTK_SMI_GEN2,
	.bus_sel  = F_MMU1_LARB(2) | F_MMU1_LARB(4),
};

static const struct of_device_id mtk_smi_common_of_ids[] = {
	{.compatible = "mediatek,mt2701-smi-common", .data = &mtk_smi_common_gen1},
	{.compatible = "mediatek,mt2712-smi-common", .data = &mtk_smi_common_gen2},
	{.compatible = "mediatek,mt6779-smi-common", .data = &mtk_smi_common_mt6779},
	{.compatible = "mediatek,mt6795-smi-common", .data = &mtk_smi_common_mt6795},
	{.compatible = "mediatek,mt8167-smi-common", .data = &mtk_smi_common_gen2},
	{.compatible = "mediatek,mt8173-smi-common", .data = &mtk_smi_common_gen2},
	{.compatible = "mediatek,mt8183-smi-common", .data = &mtk_smi_common_mt8183},
	{.compatible = "mediatek,mt8186-smi-common", .data = &mtk_smi_common_mt8186},
	{.compatible = "mediatek,mt8188-smi-common-vdo", .data = &mtk_smi_common_mt8188_vdo},
	{.compatible = "mediatek,mt8188-smi-common-vpp", .data = &mtk_smi_common_mt8188_vpp},
	{.compatible = "mediatek,mt8192-smi-common", .data = &mtk_smi_common_mt8192},
	{.compatible = "mediatek,mt8195-smi-common-vdo", .data = &mtk_smi_common_mt8195_vdo},
	{.compatible = "mediatek,mt8195-smi-common-vpp", .data = &mtk_smi_common_mt8195_vpp},
	{.compatible = "mediatek,mt8195-smi-sub-common", .data = &mtk_smi_sub_common_mt8195},
	{.compatible = "mediatek,mt8365-smi-common", .data = &mtk_smi_common_mt8365},
	{}
};
MODULE_DEVICE_TABLE(of, mtk_smi_common_of_ids);

static int mtk_smi_common_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_smi *common;
	int ret, clk_required = MTK_SMI_COM_REQ_CLK_NR;

	common = devm_kzalloc(dev, sizeof(*common), GFP_KERNEL);
	if (!common)
		return -ENOMEM;
	common->dev = dev;
	common->plat = of_device_get_match_data(dev);

	if (common->plat->has_gals) {
		if (common->plat->type == MTK_SMI_GEN2)
			clk_required = MTK_SMI_COM_GALS_REQ_CLK_NR;
		else if (common->plat->type == MTK_SMI_GEN2_SUB_COMM)
			clk_required = MTK_SMI_SUB_COM_GALS_REQ_CLK_NR;
	}
	ret = mtk_smi_dts_clk_init(dev, common, mtk_smi_common_clks, clk_required, 0);
	if (ret)
		return ret;

	/*
	 * for mtk smi gen 1, we need to get the ao(always on) base to config
	 * m4u port, and we need to enable the aync clock for transform the smi
	 * clock into emi clock domain, but for mtk smi gen2, there's no smi ao
	 * base.
	 */
	if (common->plat->type == MTK_SMI_GEN1) {
		common->smi_ao_base = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(common->smi_ao_base))
			return PTR_ERR(common->smi_ao_base);

		common->clk_async = devm_clk_get(dev, "async");
		if (IS_ERR(common->clk_async))
			return PTR_ERR(common->clk_async);

		ret = clk_prepare_enable(common->clk_async);
		if (ret)
			return ret;
	} else {
		common->base = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(common->base))
			return PTR_ERR(common->base);
	}

	/* link its smi-common if this is smi-sub-common */
	if (common->plat->type == MTK_SMI_GEN2_SUB_COMM) {
		ret = mtk_smi_device_link_common(dev, &common->smi_common_dev);
		if (ret < 0)
			return ret;
	}

	pm_runtime_enable(dev);
	platform_set_drvdata(pdev, common);
	return 0;
}

static void mtk_smi_common_remove(struct platform_device *pdev)
{
	struct mtk_smi *common = dev_get_drvdata(&pdev->dev);

	if (common->plat->type == MTK_SMI_GEN2_SUB_COMM)
		device_link_remove(&pdev->dev, common->smi_common_dev);
	pm_runtime_disable(&pdev->dev);
}

static int __maybe_unused mtk_smi_common_resume(struct device *dev)
{
	struct mtk_smi *common = dev_get_drvdata(dev);
	const struct mtk_smi_reg_pair *init = common->plat->init;
	u32 bus_sel = common->plat->bus_sel; /* default is 0 */
	int ret, i;

	ret = clk_bulk_prepare_enable(common->clk_num, common->clks);
	if (ret)
		return ret;

	if (common->plat->type != MTK_SMI_GEN2)
		return 0;

	for (i = 0; i < SMI_COMMON_INIT_REGS_NR && init && init[i].offset; i++)
		writel_relaxed(init[i].value, common->base + init[i].offset);

	writel(bus_sel, common->base + SMI_BUS_SEL);
	return 0;
}

static int __maybe_unused mtk_smi_common_suspend(struct device *dev)
{
	struct mtk_smi *common = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(common->clk_num, common->clks);
	return 0;
}

static const struct dev_pm_ops smi_common_pm_ops = {
	SET_RUNTIME_PM_OPS(mtk_smi_common_suspend, mtk_smi_common_resume, NULL)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				     pm_runtime_force_resume)
};

static struct platform_driver mtk_smi_common_driver = {
	.probe	= mtk_smi_common_probe,
	.remove_new = mtk_smi_common_remove,
	.driver	= {
		.name = "mtk-smi-common",
		.of_match_table = mtk_smi_common_of_ids,
		.pm             = &smi_common_pm_ops,
	}
};

static struct platform_driver * const smidrivers[] = {
	&mtk_smi_common_driver,
	&mtk_smi_larb_driver,
};

static int __init mtk_smi_init(void)
{
	return platform_register_drivers(smidrivers, ARRAY_SIZE(smidrivers));
}
module_init(mtk_smi_init);

static void __exit mtk_smi_exit(void)
{
	platform_unregister_drivers(smidrivers, ARRAY_SIZE(smidrivers));
}
module_exit(mtk_smi_exit);

MODULE_DESCRIPTION("MediaTek SMI driver");
MODULE_LICENSE("GPL v2");
