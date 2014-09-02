/*
 * Copyright (C) 2012-2013 Xilinx
 *
 * based on linux/arch/arm/mach-realview/hotplug.c
 *
 * Copyright (C) 2002 ARM Ltd.
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <asm/proc-fns.h>

/*
 * platform-specific code to shutdown a CPU
 *
 * Called with IRQs disabled
 */
void zynq_platform_cpu_die(unsigned int cpu)
{
	/*
	 * there is no power-control hardware on this platform, so all
	 * we can do is put the core into WFI; this is safe as the calling
	 * code will have already disabled interrupts
	 */
	for (;;)
		cpu_do_idle();
}
