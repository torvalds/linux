/*
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 *
 * This program is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, or 
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _INIT_H_
#define _INIT_H_ 
int ssv6xxx_init_mac(struct ssv_hw *sh);
int ssv6xxx_do_iq_calib(struct ssv_hw *sh, struct ssv6xxx_iqk_cfg *p_cfg);
void ssv6xxx_deinit_mac(struct ssv_softc *sc);
void ssv6xxx_restart_hw(struct ssv_softc *sc);
#endif
