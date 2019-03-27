/*	$NetBSD: h_dm.c,v 1.2 2016/01/23 21:18:27 christos Exp $	*/

/*
 * Copyright (c) 2010 Antti Kantee.  All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/disklabel.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include <dev/dm/netbsd-dm.h>

int dm_test_targets(void);
int dm_test_versions(void);

/*
 * Test simple dm versions command on device-mapper device.
 */
int
dm_test_versions(void) {
	int fd;
	int error;
	prop_dictionary_t dict_in, dict_out;
	struct plistref prefp;
	char *xml;

	error = 0;
	
	error = rump_init();
	if (error != 0)
		err(1, "Rump init failed");
	
	fd = rump_sys_open("/dev/mapper/control", O_RDWR, 0);
	if (fd == -1)
		err(1, "Open dm device failed");

	dict_in  = prop_dictionary_internalize_from_file("dm_version_cmd.plist");
	dict_out = prop_dictionary_create();
	
	prop_dictionary_externalize_to_pref(dict_in, &prefp);
	
	error = rump_sys_ioctl(fd, NETBSD_DM_IOCTL, &prefp);
	if (error < 0)
		err(1, "Dm control ioctl failed");

	dict_out = prop_dictionary_internalize(prefp.pref_plist);
	
	xml = prop_dictionary_externalize(dict_out);
	__USE(xml);

	rump_sys_close(fd);

	return error;
}

/*
 * Test simple dm targets command on device-mapper device.
 */
int
dm_test_targets(void) {
	int fd;
	int error;
	prop_dictionary_t dict_in, dict_out;
	struct plistref prefp;
	char *xml;

	error = 0;
	
	error = rump_init();
	if (error != 0)
		err(1, "Rump init failed");
	
	fd = rump_sys_open("/dev/mapper/control", O_RDWR, 0);
	if (fd == -1)
		err(1, "Open dm device failed");

	dict_in  = prop_dictionary_internalize_from_file("dm_targets_cmd.plist");
	dict_out = prop_dictionary_create();
	
	prop_dictionary_externalize_to_pref(dict_in, &prefp);

	error = rump_sys_ioctl(fd, NETBSD_DM_IOCTL, &prefp);
	if (error < 0)
		err(1, "Dm control ioctl failed");

	dict_out = prop_dictionary_internalize(prefp.pref_plist);
	
	xml = prop_dictionary_externalize(dict_out);
	__USE(xml);

	rump_sys_close(fd);

	return error;
}

int
main(int argc, char **argv) {
	int error;

	error = 0;

	error = dm_test_versions();
	if (error != 0)
		err(1, "dm_test_versions failed");

	error = dm_test_targets();
	if (error != 0)
		err(1, "dm_test_targets failed");

	return error;
}
