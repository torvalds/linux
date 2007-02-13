#ifndef __MPC83XX_H__
#define __MPC83XX_H__

#include <linux/init.h>
#include <linux/device.h>

/* System Clock Control Register */
#define MPC83XX_SCCR_OFFS          0xA08
#define MPC83XX_SCCR_USB_MPHCM_11  0x00c00000
#define MPC83XX_SCCR_USB_MPHCM_01  0x00400000
#define MPC83XX_SCCR_USB_MPHCM_10  0x00800000
#define MPC83XX_SCCR_USB_DRCM_11   0x00300000
#define MPC83XX_SCCR_USB_DRCM_01   0x00100000
#define MPC83XX_SCCR_USB_DRCM_10   0x00200000

/* system i/o configuration register low */
#define MPC83XX_SICRL_OFFS         0x114
#define MPC83XX_SICRL_USB0         0x40000000
#define MPC83XX_SICRL_USB1         0x20000000

/* system i/o configuration register high */
#define MPC83XX_SICRH_OFFS         0x118
#define MPC83XX_SICRH_USB_UTMI     0x00020000

/*
 * Declaration for the various functions exported by the
 * mpc83xx_* files. Mostly for use by mpc83xx_setup
 */

extern int add_bridge(struct device_node *dev);
extern int mpc83xx_exclude_device(u_char bus, u_char devfn);
extern void mpc83xx_restart(char *cmd);
extern long mpc83xx_time_init(void);

#endif				/* __MPC83XX_H__ */
