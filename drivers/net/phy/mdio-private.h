/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __MDIO_PRIVATE_H
#define __MDIO_PRIVATE_H

/* MDIO internal helpers
 */

int mdio_device_register_reset(struct mdio_device *mdiodev);
void mdio_device_unregister_reset(struct mdio_device *mdiodev);

#endif /* __MDIO_PRIVATE_H */
