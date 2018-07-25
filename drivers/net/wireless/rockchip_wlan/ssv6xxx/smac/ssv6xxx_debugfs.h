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

#ifndef __SSV6XXX_DBGFS_H__
#define __SSV6XXX_DBGFS_H__ 
int ssv6xxx_init_debugfs (struct ssv_softc *sc, const char *name);
void ssv6xxx_deinit_debugfs (struct ssv_softc *sc);
int ssv6xxx_debugfs_remove_interface(struct ssv_softc *sc, struct ieee80211_vif *vif);
int ssv6xxx_debugfs_add_interface(struct ssv_softc *sc, struct ieee80211_vif *vif);
int ssv6xxx_debugfs_remove_sta(struct ssv_softc *sc, struct ssv_sta_info *sta);
int ssv6xxx_debugfs_add_sta(struct ssv_softc *sc, struct ssv_sta_info *sta);
#endif
