/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_OS_LINUX_INTERNAL_H
#define __UM_OS_LINUX_INTERNAL_H

#include <mm_id.h>
#include <stub-data.h>

/*
 * elf_aux.c
 */
void scan_elf_aux(char **envp);

/*
 * mem.c
 */
void check_tmpexec(void);

/*
 * skas/process.c
 */
void wait_stub_done(int pid);
void wait_stub_done_seccomp(struct mm_id *mm_idp, int running, int wait_sigsys);
#endif /* __UM_OS_LINUX_INTERNAL_H */
