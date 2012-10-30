/*
 * Copyright (c) 2010-2012 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com
 *
 * EXYNOS - SATA utility framework definitions.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

enum sata_phy_type {
	SATA_PHY_GENERATION1,
	SATA_PHY_GENERATION2,
	SATA_PHY_GENERATION3,
};

struct sata_phy {
	int (*init) (struct sata_phy *);
	int (*shutdown) (struct sata_phy *);
	struct device *dev;
	void *priv_data;
	enum sata_phy_type type;
	struct list_head head;
};

static inline int sata_init_phy(struct sata_phy *x)
{
	if (x && x->init)
		return x->init(x);

	return -EINVAL;
}

static inline void sata_shutdown_phy(struct sata_phy *x)
{
	if (x && x->shutdown)
		x->shutdown(x);
}

struct sata_phy *sata_get_phy(enum sata_phy_type);
int sata_add_phy(struct sata_phy *, enum sata_phy_type);
void sata_remove_phy(struct sata_phy *);
void sata_put_phy(struct sata_phy *);
