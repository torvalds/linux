/*
 * Header providing constants for i.MX28 pinctrl bindings.
 *
 * Copyright (C) 2013 Lothar Waßmann <LW@KARO-electronics.de>
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef __DT_BINDINGS_MXS_PINCTRL_H__
#define __DT_BINDINGS_MXS_PINCTRL_H__

/* fsl,drive-strength property */
#define MXS_DRIVE_4mA		0
#define MXS_DRIVE_8mA		1
#define MXS_DRIVE_12mA		2
#define MXS_DRIVE_16mA		3

/* fsl,voltage property */
#define MXS_VOLTAGE_LOW		0
#define MXS_VOLTAGE_HIGH	1

/* fsl,pull-up property */
#define MXS_PULL_DISABLE	0
#define MXS_PULL_ENABLE		1

#endif /* __DT_BINDINGS_MXS_PINCTRL_H__ */
