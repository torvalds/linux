/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 SiFive, Inc.
 *
 */

#ifndef _SIFIVE_PL2_H
#define _SIFIVE_PL2_H

#define SIFIVE_PL2_PMCLIENT_OFFSET	0x2800

int sifive_u74_l2_pmu_probe(struct device_node *pl2_node,
			 void __iomem *pl2_base, int cpu);
#endif /*_SIFIVE_PL2_H */
