/*
 * Freescale STMP37XX/STMP378X Pin multiplexing interface definitions
 *
 * Author: Vladislav Buzov <vbuzov@embeddedalley.com>
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef __ASM_PLAT_PINS_H
#define __ASM_PLAT_PINS_H

#define STMP3XXX_PINID(bank, pin)	(bank * 32 + pin)
#define STMP3XXX_PINID_TO_BANK(pinid)	(pinid / 32)
#define STMP3XXX_PINID_TO_PINNUM(pinid)	(pinid % 32)

/*
 * Special invalid pin identificator to show a pin doesn't exist
 */
#define PINID_NO_PIN	STMP3XXX_PINID(0xFF, 0xFF)

#endif /* __ASM_PLAT_PINS_H */
