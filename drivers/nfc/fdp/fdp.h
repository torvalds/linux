/* -------------------------------------------------------------------------
 * Copyright (C) 2014-2016, Intel Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * -------------------------------------------------------------------------
 */

#ifndef __LOCAL_FDP_H_
#define __LOCAL_FDP_H_

#include <net/nfc/nci_core.h>
#include <linux/gpio/consumer.h>

struct fdp_i2c_phy {
	struct i2c_client *i2c_dev;
	struct gpio_desc *power_gpio;
	struct nci_dev *ndev;

	/* < 0 if i2c error occurred */
	int hard_fault;
	uint16_t next_read_size;
};

int fdp_nci_probe(struct fdp_i2c_phy *phy, struct nfc_phy_ops *phy_ops,
		  struct nci_dev **ndev, int tx_headroom, int tx_tailroom,
		  u8 clock_type, u32 clock_freq, u8 *fw_vsc_cfg);
void fdp_nci_remove(struct nci_dev *ndev);
int fdp_nci_recv_frame(struct nci_dev *ndev, struct sk_buff *skb);

#endif /* __LOCAL_FDP_H_ */
