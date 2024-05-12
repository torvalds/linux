/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include <asm/addrspace.h>
#include <asm/fw/fw.h>

int fw_argc;
int *_fw_argv;
int *_fw_envp;

#ifndef CONFIG_HAVE_PLAT_FW_INIT_CMDLINE
void __init fw_init_cmdline(void)
{
	int i;

	/* Validate command line parameters. */
	if ((fw_arg0 >= CKSEG0) || (fw_arg1 < CKSEG0)) {
		fw_argc = 0;
		_fw_argv = NULL;
	} else {
		fw_argc = (fw_arg0 & 0x0000ffff);
		_fw_argv = (int *)fw_arg1;
	}

	/* Validate environment pointer. */
	if (fw_arg2 < CKSEG0)
		_fw_envp = NULL;
	else
		_fw_envp = (int *)fw_arg2;

	for (i = 1; i < fw_argc; i++) {
		strlcat(arcs_cmdline, fw_argv(i), COMMAND_LINE_SIZE);
		if (i < (fw_argc - 1))
			strlcat(arcs_cmdline, " ", COMMAND_LINE_SIZE);
	}
}
#endif

char * __init fw_getcmdline(void)
{
	return &(arcs_cmdline[0]);
}

char *fw_getenv(char *envname)
{
	char *result = NULL;

	if (_fw_envp != NULL && fw_envp(0) != NULL) {
		/*
		 * Return a pointer to the given environment variable.
		 * YAMON uses "name", "value" pairs, while U-Boot uses
		 * "name=value".
		 */
		int i, yamon, index = 0;

		yamon = (strchr(fw_envp(index), '=') == NULL);
		i = strlen(envname);

		while (fw_envp(index)) {
			if (strncmp(envname, fw_envp(index), i) == 0) {
				if (yamon) {
					result = fw_envp(index + 1);
					break;
				} else if (fw_envp(index)[i] == '=') {
					result = fw_envp(index) + i + 1;
					break;
				}
			}

			/* Increment array index. */
			if (yamon)
				index += 2;
			else
				index += 1;
		}
	}

	return result;
}

unsigned long fw_getenvl(char *envname)
{
	unsigned long envl = 0UL;
	char *str;
	int tmp;

	str = fw_getenv(envname);
	if (str) {
		tmp = kstrtoul(str, 0, &envl);
		if (tmp)
			envl = 0;
	}

	return envl;
}
