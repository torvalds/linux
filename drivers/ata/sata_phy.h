/*
 * Copyright (c) 2010-2012 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com
 *
 * SATA utility framework definitions.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define IN_USE		1
#define NOT_IN_USE	0

struct sata_phy {
	int (*init) (struct sata_phy *);
	int (*shutdown) (struct sata_phy *);
	struct device *dev;
	void *priv_data;
	struct list_head head;
	unsigned char status;
};

struct sata_phy *sata_get_phy(struct device_node *);
int sata_add_phy(struct sata_phy *);
void sata_remove_phy(struct sata_phy *);
void sata_put_phy(struct sata_phy *);
int sata_init_phy(struct sata_phy *);
void sata_shutdown_phy(struct sata_phy *);

