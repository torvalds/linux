/*
 * Copyright (C) 2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_ARC_SERIAL_H
#define _ASM_ARC_SERIAL_H

/*
 * early-8250 requires BASE_BAUD to be defined and includes this header.
 * We put in a typical value:
 * 	(core clk / 16) - i.e. UART samples 16 times per sec.
 * Athough in multi-platform-image this might not work, specially if the
 * clk driving the UART is different.
 * We can't use DeviceTree as this is typically for early serial.
 */

#include <asm/clk.h>

#define BASE_BAUD	(arc_get_core_freq() / 16)

#endif /* _ASM_ARC_SERIAL_H */
