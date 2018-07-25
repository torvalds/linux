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

#ifndef _SSV_PM_H_
#define _SSV_PM_H_ 
#include <linux/version.h>
#ifdef CONFIG_SSV_SUPPORT_ANDROID
#ifdef CONFIG_HAS_EARLYSUSPEND
void ssv6xxx_early_suspend(struct early_suspend *h);
void ssv6xxx_late_resume(struct early_suspend *h);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
void ssv6xxx_early_suspend(void);
void ssv6xxx_late_resume(void);
#endif
#ifdef CONFIG_HAS_WAKELOCK
void ssv_wakelock_init(struct ssv_softc *sc);
void ssv_wakelock_destroy(struct ssv_softc *sc);
void ssv_wake_lock(struct ssv_softc *sc);
void ssv_wake_timeout(struct ssv_softc *sc, int secs);
void ssv_wake_unlock(struct ssv_softc *sc);
#endif
#endif
#endif
