/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#ifndef _IONIC_BUS_H_
#define _IONIC_BUS_H_

int ionic_bus_get_irq(struct ionic *ionic, unsigned int num);
const char *ionic_bus_info(struct ionic *ionic);
int ionic_bus_alloc_irq_vectors(struct ionic *ionic, unsigned int nintrs);
void ionic_bus_free_irq_vectors(struct ionic *ionic);
int ionic_bus_register_driver(void);
void ionic_bus_unregister_driver(void);
void __iomem *ionic_bus_map_dbpage(struct ionic *ionic, int page_num);
void ionic_bus_unmap_dbpage(struct ionic *ionic, void __iomem *page);

#endif /* _IONIC_BUS_H_ */
