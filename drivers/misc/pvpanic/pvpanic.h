// SPDX-License-Identifier: GPL-2.0+
/*
 *  Pvpanic Device Support
 *
 *  Copyright (C) 2021 Oracle.
 */

#ifndef PVPANIC_H_
#define PVPANIC_H_

void pvpanic_probe(void __iomem *base, unsigned int dev_cap);
void pvpanic_remove(void);
void pvpanic_set_events(unsigned int dev_events);

#endif /* PVPANIC_H_ */
