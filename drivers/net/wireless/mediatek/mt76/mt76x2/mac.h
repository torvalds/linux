/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 */

#ifndef __MT76x2_MAC_H
#define __MT76x2_MAC_H

#include "mt76x2.h"

struct mt76x02_dev;
struct mt76x2_sta;
struct mt76x02_vif;

void mt76x2_mac_stop(struct mt76x02_dev *dev, bool force);

static inline void mt76x2_mac_resume(struct mt76x02_dev *dev)
{
	mt76_wr(dev, MT_MAC_SYS_CTRL,
		MT_MAC_SYS_CTRL_ENABLE_TX |
		MT_MAC_SYS_CTRL_ENABLE_RX);
}

#endif
