/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _REALTEK_MDIO_H
#define _REALTEK_MDIO_H

#if IS_ENABLED(CONFIG_NET_DSA_REALTEK_MDIO)

static inline int realtek_mdio_driver_register(struct mdio_driver *drv)
{
	return mdio_driver_register(drv);
}

static inline void realtek_mdio_driver_unregister(struct mdio_driver *drv)
{
	mdio_driver_unregister(drv);
}

int realtek_mdio_probe(struct mdio_device *mdiodev);
void realtek_mdio_remove(struct mdio_device *mdiodev);
void realtek_mdio_shutdown(struct mdio_device *mdiodev);

#else /* IS_ENABLED(CONFIG_NET_DSA_REALTEK_MDIO) */

static inline int realtek_mdio_driver_register(struct mdio_driver *drv)
{
	return 0;
}

static inline void realtek_mdio_driver_unregister(struct mdio_driver *drv)
{
}

static inline int realtek_mdio_probe(struct mdio_device *mdiodev)
{
	return -ENOENT;
}

static inline void realtek_mdio_remove(struct mdio_device *mdiodev)
{
}

static inline void realtek_mdio_shutdown(struct mdio_device *mdiodev)
{
}

#endif /* IS_ENABLED(CONFIG_NET_DSA_REALTEK_MDIO) */

#endif /* _REALTEK_MDIO_H */
