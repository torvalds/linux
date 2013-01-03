/*
 * Copyright (c) 2010-2012, NVIDIA Corporation. All rights reserved.
 * Copyright (c) 2011, Google, Inc.
 *
 * Author: Colin Cross <ccross@android.com>
 *         Gary King <gking@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/linkage.h>

#include <asm/assembler.h>

#include "sleep.h"
#include "flowctrl.h"

#if defined(CONFIG_HOTPLUG_CPU) || defined(CONFIG_PM_SLEEP)
/*
 * tegra20_hotplug_shutdown(void)
 *
 * puts the current cpu in reset
 * should never return
 */
ENTRY(tegra20_hotplug_shutdown)
	/* Put this CPU down */
	cpu_id	r0
	bl	tegra20_cpu_shutdown
	mov	pc, lr			@ should never get here
ENDPROC(tegra20_hotplug_shutdown)

/*
 * tegra20_cpu_shutdown(int cpu)
 *
 * r0 is cpu to reset
 *
 * puts the specified CPU in wait-for-event mode on the flow controller
 * and puts the CPU in reset
 * can be called on the current cpu or another cpu
 * if called on the current cpu, does not return
 * MUST NOT BE CALLED FOR CPU 0.
 *
 * corrupts r0-r3, r12
 */
ENTRY(tegra20_cpu_shutdown)
	cmp	r0, #0
	moveq	pc, lr			@ must not be called for CPU 0

	cpu_to_halt_reg r1, r0
	ldr	r3, =TEGRA_FLOW_CTRL_VIRT
	mov	r2, #FLOW_CTRL_WAITEVENT | FLOW_CTRL_JTAG_RESUME
	str	r2, [r3, r1]		@ put flow controller in wait event mode
	ldr	r2, [r3, r1]
	isb
	dsb
	movw	r1, 0x1011
	mov	r1, r1, lsl r0
	ldr	r3, =TEGRA_CLK_RESET_VIRT
	str	r1, [r3, #0x340]	@ put slave CPU in reset
	isb
	dsb
	cpu_id	r3
	cmp	r3, r0
	beq	.
	mov	pc, lr
ENDPROC(tegra20_cpu_shutdown)
#endif
