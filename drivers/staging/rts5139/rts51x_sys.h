/* Driver for Realtek USB RTS51xx card reader
 * Header file
 *
 * Copyright(c) 2009 Realtek Semiconductor Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   wwang (wei_wang@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 * Maintainer:
 *   Edwin Rong (edwin_rong@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 */

#ifndef __RTS51X_SYS_H
#define __RTS51X_SYS_H

#include "rts51x.h"
#include "rts51x_chip.h"
#include "rts51x_card.h"

#define USING_POLLING_CYCLE_DELINK

/* typedef dma_addr_t ULONG_PTR; */

void rts51x_enter_ss(struct rts51x_chip *chip);
void rts51x_exit_ss(struct rts51x_chip *chip);

#endif /* __RTS51X_SYS_H */
