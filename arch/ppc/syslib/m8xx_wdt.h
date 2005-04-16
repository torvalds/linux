/*
 * Author: Florian Schirmer <jolt@tuxbox.org>
 *
 * 2002 (c) Florian Schirmer <jolt@tuxbox.org> This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef _PPC_SYSLIB_M8XX_WDT_H
#define _PPC_SYSLIB_M8XX_WDT_H

extern void m8xx_wdt_handler_install(bd_t * binfo);
extern int m8xx_wdt_get_timeout(void);
extern void m8xx_wdt_reset(void);

#endif				/* _PPC_SYSLIB_M8XX_WDT_H */
