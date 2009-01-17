/**
 * @file me8100_di_reg.h
 *
 * @brief ME-8100 digital input subdevice register definitions.
 * @note Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 * @author Guenter Gebhardt
 */

/*
 * Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _ME8100_DI_REG_H_
#define _ME8100_DI_REG_H_

#ifdef __KERNEL__

#define ME8100_RES_INT_REG_A		0x02	//(r, )
#define ME8100_DI_REG_A			0x04	//(r, )
#define ME8100_PATTERN_REG_A		0x08	//( ,w)
#define ME8100_MASK_REG_A		0x0A	//( ,w)
#define ME8100_INT_DI_REG_A		0x0A	//(r, )

#define ME8100_RES_INT_REG_B		0x0E	//(r, )
#define ME8100_DI_REG_B			0x10	//(r, )
#define ME8100_PATTERN_REG_B		0x14	//( ,w)
#define ME8100_MASK_REG_B		0x16	//( ,w)
#define ME8100_INT_DI_REG_B		0x16	//(r, )

#define ME8100_REG_OFFSET		0x0C

#endif
#endif
