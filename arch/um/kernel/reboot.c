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
#include "choose-mode.h"

void (*pm_power_off)(void);

static void kill_off_processes(void)
{
	CHOOSE_MODE(kill_off_processes_tt(), kill_off_processes_skas());
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
	CHOOSE_MODE(reboot_tt(), reboot_skas());
}

void machine_power_off(void)
{
        uml_cleanup();
	CHOOSE_MODE(halt_tt(), halt_skas());
}

void machine_halt(void)
{
	machine_power_off();
}
