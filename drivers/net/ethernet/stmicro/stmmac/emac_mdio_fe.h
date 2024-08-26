/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved. */

#ifndef _EMAC_MDIO_FE_API_H_
#define _EMAC_MDIO_FE_API_H_

#include <linux/phy.h>

#if IS_ENABLED(CONFIG_EMAC_MDIO_FE)

int virtio_mdio_read(struct mii_bus *bus, int addr, int regnum);

int virtio_mdio_write(struct mii_bus *bus, int addr, int regnum, u16 val);

int virtio_mdio_read_c45(struct mii_bus *bus, int addr, int devnum, int regnum);

int virtio_mdio_write_c45(struct mii_bus *bus, int addr, int devnum, int regnum, u16 val);

int virtio_mdio_read_c45_indirect(struct mii_bus *bus, int addr, int regnum);

int virtio_mdio_write_c45_indirect(struct mii_bus *bus, int addr, int regnum, u16 val);

#else
static inline int virtio_mdio_read(struct mii_bus *bus, int addr, int regnum)
{
	/* Not enabled */
	return 0;
}

static inline int virtio_mdio_write(struct mii_bus *bus, int addr, int regnum, u16 val)
{
	/* Not enabled */
	return 0;
}

static inline int virtio_mdio_read_c45(struct mii_bus *bus, int addr, int devnum, int regnum)
{
	/* Not enabled */
	return 0;
}

static inline int virtio_mdio_write_c45(struct mii_bus *bus,
					int addr, int devnum,
					int regnum, u16 val)

{
	/* Not enabled */
	return 0;
}

static inline int virtio_mdio_read_c45_indirect(struct mii_bus *bus, int addr,
						int regnum)
{
	/* Not enabled */
	return 0;
}

static inline int virtio_mdio_write_c45_indirect(struct mii_bus *bus, int addr,
						 int regnum, u16 val)
{
	/* Not enabled */
	return 0;
}
#endif /* CONFIG_EMAC_MDIO_FE */
#endif /* _EMAC_MDIO_FE_API_H_ */
