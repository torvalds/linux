/*
 * Platform information definitions.
 *
 * 2006 (c) MontaVista Software, Inc.
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef FS_PD_H
#define FS_PD_H
#include <sysdev/fsl_soc.h>
#include <asm/time.h>

#ifdef CONFIG_CPM2
#include <asm/cpm2.h>

#define cpm2_map(member) (&cpm2_immr->member)
#define cpm2_map_size(member, size) (&cpm2_immr->member)
#define cpm2_unmap(addr) do {} while(0)
#endif

static inline int uart_baudrate(void)
{
        return get_baudrate();
}

static inline int uart_clock(void)
{
        return ppc_proc_freq;
}

#endif
