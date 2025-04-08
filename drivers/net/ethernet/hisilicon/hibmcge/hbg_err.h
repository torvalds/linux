/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2024 Hisilicon Limited. */

#ifndef __HBG_ERR_H
#define __HBG_ERR_H

#include <linux/pci.h>

void hbg_set_pci_err_handler(struct pci_driver *pdrv);
int hbg_reset(struct hbg_priv *priv);
int hbg_rebuild(struct hbg_priv *priv);
void hbg_err_reset(struct hbg_priv *priv);

#endif
