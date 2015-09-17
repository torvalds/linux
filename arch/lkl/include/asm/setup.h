#ifndef _ASM_LKL_SETUP_H
#define _ASM_LKL_SETUP_H

#define COMMAND_LINE_SIZE 4096

#ifndef __ASSEMBLY__
#define ARCH_RUN_INIT_PROCESS
int run_init_process(const char *init_filename);
void wakeup_cpu(void);
#endif

#endif
