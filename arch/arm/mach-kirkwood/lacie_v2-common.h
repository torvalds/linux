/*
 * arch/arm/mach-kirkwood/lacie_v2-common.h
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ARCH_KIRKWOOD_LACIE_V2_COMMON_H
#define __ARCH_KIRKWOOD_LACIE_V2_COMMON_H

void lacie_v2_register_flash(void);
void lacie_v2_register_i2c_devices(void);
void lacie_v2_hdd_power_init(int hdd_num);

extern struct sys_timer lacie_v2_timer;

#endif
