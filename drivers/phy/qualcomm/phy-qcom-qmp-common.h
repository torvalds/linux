/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
 */

#ifndef QCOM_PHY_QMP_COMMON_H_
#define QCOM_PHY_QMP_COMMON_H_

struct qmp_phy_init_tbl {
	unsigned int offset;
	unsigned int val;
	char *name;
	/*
	 * mask of lanes for which this register is written
	 * for cases when second lane needs different values
	 */
	u8 lane_mask;
};

#define QMP_PHY_INIT_CFG(o, v)		\
	{				\
		.offset = o,		\
		.val = v,		\
		.name = #o,		\
		.lane_mask = 0xff,	\
	}

#define QMP_PHY_INIT_CFG_LANE(o, v, l)	\
	{				\
		.offset = o,		\
		.val = v,		\
		.name = #o,		\
		.lane_mask = l,		\
	}

static inline void qmp_configure_lane(struct device *dev, void __iomem *base,
				      const struct qmp_phy_init_tbl tbl[],
				      int num, u8 lane_mask)
{
	int i;
	const struct qmp_phy_init_tbl *t = tbl;

	if (!t)
		return;

	for (i = 0; i < num; i++, t++) {
		if (!(t->lane_mask & lane_mask))
			continue;

		dev_dbg(dev, "Writing Reg: %s Offset: 0x%04x Val: 0x%02x\n",
			t->name, t->offset, t->val);
		writel(t->val, base + t->offset);
	}
}

static inline void qmp_configure(struct device *dev, void __iomem *base,
				 const struct qmp_phy_init_tbl tbl[], int num)
{
	qmp_configure_lane(dev, base, tbl, num, 0xff);
}

#endif
