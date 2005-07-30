/* 
 * Copyright (C) 2000, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/module.h"
#include "linux/sched.h"
#include "user_util.h"
#include "kern_util.h"
#include "kern.h"
#include "os.h"
#include "mode.h"
#include "choose-mode.h"

#ifdef CONFIG_SMP
static void kill_idlers(int me)
{
#ifdef CONFIG_MODE_TT
	struct task_struct *p;
	int i;

	for(i = 0; i < sizeof(idle_threads)/sizeof(idle_threads[0]); i++){
		p = idle_threads[i];
		if((p != NULL) && (p->thread.mode.tt.extern_pid != me))
			os_kill_process(p->thread.mode.tt.extern_pid, 0);
	}
#endif
}
#endif

static void kill_off_processes(void)
{
	CHOOSE_MODE(kill_off_processes_tt(), kill_off_processes_skas());
#ifdef CONFIG_SMP
	kill_idlers(os_getpid());
#endif
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

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
