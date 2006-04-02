#ifndef __MPC83XX_H__
#define __MPC83XX_H__

#include <linux/init.h>
#include <linux/device.h>

/*
 * Declaration for the various functions exported by the
 * mpc83xx_* files. Mostly for use by mpc83xx_setup
 */

extern int add_bridge(struct device_node *dev);
extern int mpc83xx_exclude_device(u_char bus, u_char devfn);
extern void mpc83xx_restart(char *cmd);
extern long mpc83xx_time_init(void);

#endif				/* __MPC83XX_H__ */
