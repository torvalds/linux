/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 Paul Cercueil <paul@crapouillou.net>
 */
#ifndef DRIVERS_CLK_INGENIC_PM_H
#define DRIVERS_CLK_INGENIC_PM_H

struct ingenic_cgu;

void ingenic_cgu_register_syscore_ops(struct ingenic_cgu *cgu);

#endif /* DRIVERS_CLK_INGENIC_PM_H */
