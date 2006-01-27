#ifndef __MPC83XX_H__
#define __MPC83XX_H__

#include <linux/init.h>
#include <linux/device.h>

/*
 * Declaration for the various functions exported by the
 * mpc83xx_* files. Mostly for use by mpc83xx_setup
 */

extern int add_bridge(struct device_node *dev);

#endif /* __MPC83XX_H__ */
