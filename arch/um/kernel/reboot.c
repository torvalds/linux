/* 
 * Copyright (C) 2000, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/module.h"
#include "linux/sched.h"
#include "asm/smp.h"
#include "kern_util.h"
#include "kern.h"
#include "os.h"
#include "mode.h"

void (*pm_power_off)(void);

static void kill_off_processes(void)
{
	kill_off_processes_skas();
}

void uml_cleanup(void)
{
        kmalloc_ok = 0;
	do_uml_exitcalls();
	kill_off_processes();
}

void machine_restart(char * __unused)
{
        uml_cleanup();
	reboot_skas();
}

void machine_power_off(void)
{
        uml_cleanup();
	halt_skas();
}

void machine_halt(void)
{
	machine_power_off();
}
