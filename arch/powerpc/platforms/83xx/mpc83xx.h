/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MPC83XX_H__
#define __MPC83XX_H__

#include <linux/init.h>

/* System Clock Control Register */
#define MPC83XX_SCCR_OFFS          0xA08
#define MPC83XX_SCCR_USB_MASK      0x00f00000
#define MPC83XX_SCCR_USB_MPHCM_11  0x00c00000
#define MPC83XX_SCCR_USB_MPHCM_01  0x00400000
#define MPC83XX_SCCR_USB_MPHCM_10  0x00800000
#define MPC83XX_SCCR_USB_DRCM_11   0x00300000
#define MPC83XX_SCCR_USB_DRCM_01   0x00100000
#define MPC83XX_SCCR_USB_DRCM_10   0x00200000
#define MPC8315_SCCR_USB_MASK      0x00c00000
#define MPC8315_SCCR_USB_DRCM_11   0x00c00000
#define MPC8315_SCCR_USB_DRCM_01   0x00400000
#define MPC837X_SCCR_USB_DRCM_11   0x00c00000

/* system i/o configuration register low */
#define MPC83XX_SICRL_OFFS         0x114
#define MPC834X_SICRL_USB_MASK     0x60000000
#define MPC834X_SICRL_USB0         0x20000000
#define MPC834X_SICRL_USB1         0x40000000
#define MPC831X_SICRL_USB_MASK     0x00000c00
#define MPC831X_SICRL_USB_ULPI     0x00000800
#define MPC8315_SICRL_USB_MASK     0x000000fc
#define MPC8315_SICRL_USB_ULPI     0x00000054
#define MPC837X_SICRL_USB_MASK     0xf0000000
#define MPC837X_SICRL_USB_ULPI     0x50000000
#define MPC837X_SICRL_USBB_MASK    0x30000000
#define MPC837X_SICRL_SD           0x20000000

/* system i/o configuration register high */
#define MPC83XX_SICRH_OFFS         0x118
#define MPC8308_SICRH_USB_MASK     0x000c0000
#define MPC8308_SICRH_USB_ULPI     0x00040000
#define MPC834X_SICRH_USB_UTMI     0x00020000
#define MPC831X_SICRH_USB_MASK     0x000000e0
#define MPC831X_SICRH_USB_ULPI     0x000000a0
#define MPC8315_SICRH_USB_MASK     0x0000ff00
#define MPC8315_SICRH_USB_ULPI     0x00000000
#define MPC837X_SICRH_SPI_MASK     0x00000003
#define MPC837X_SICRH_SD           0x00000001

/* USB Control Register */
#define FSL_USB2_CONTROL_OFFS      0x500
#define CONTROL_UTMI_PHY_EN        0x00000200
#define CONTROL_REFSEL_24MHZ       0x00000040
#define CONTROL_REFSEL_48MHZ       0x00000080
#define CONTROL_PHY_CLK_SEL_ULPI   0x00000400
#define CONTROL_OTG_PORT           0x00000020

/* USB PORTSC Registers */
#define FSL_USB2_PORTSC1_OFFS      0x184
#define FSL_USB2_PORTSC2_OFFS      0x188
#define PORTSCX_PTW_16BIT          0x10000000
#define PORTSCX_PTS_UTMI           0x00000000
#define PORTSCX_PTS_ULPI           0x80000000

/*
 * Declaration for the various functions exported by the
 * mpc83xx_* files. Mostly for use by mpc83xx_setup
 */

extern void __noreturn mpc83xx_restart(char *cmd);
extern long mpc83xx_time_init(void);
int __init mpc837x_usb_cfg(void);
int __init mpc834x_usb_cfg(void);
int __init mpc831x_usb_cfg(void);
extern void mpc83xx_ipic_init_IRQ(void);

#ifdef CONFIG_PCI
extern void mpc83xx_setup_pci(void);
#else
#define mpc83xx_setup_pci	NULL
#endif

extern int mpc83xx_declare_of_platform_devices(void);
extern void mpc83xx_setup_arch(void);

#endif				/* __MPC83XX_H__ */
