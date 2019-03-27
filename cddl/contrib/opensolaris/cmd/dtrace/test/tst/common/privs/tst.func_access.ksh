#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
# Copyright (c) 2012, Joyent, Inc. All rights reserved.
#

ppriv -s A=basic,dtrace_proc,dtrace_user $$

/usr/sbin/dtrace -q -s /dev/stdin <<"EOF"

BEGIN {
	errorcount = 0;
	expected_errorcount = 27;
}

BEGIN { trace(mutex_owned(&`pidlock)); }
BEGIN { trace(mutex_owner(&`pidlock)); }
BEGIN { trace(mutex_type_adaptive(&`pidlock)); }
BEGIN { trace(mutex_type_spin(&`pidlock)); }

BEGIN { trace(rw_read_held(&`ksyms_lock)); }
BEGIN { trace(rw_write_held(&`ksyms_lock)); }
BEGIN { trace(rw_iswriter(&`ksyms_lock)); }

BEGIN { x = alloca(10); bcopy(`initname, x, 10); trace(stringof(x)); }
/* We have no reliable way to test msgsize */

BEGIN { trace(strlen(`initname)); }
BEGIN { trace(strchr(`initname, 0x69)); }
BEGIN { trace(strrchr(`initname, 0x69)); }
BEGIN { trace(strstr("/sbin/init/foo", `initname)); }
BEGIN { trace(strstr(`initname, "in")); }
BEGIN { trace(strtok(`initname, "/")); }
BEGIN { trace(strtok(NULL, "/")); }
BEGIN { trace(strtok("foo/bar", `initname)); }
BEGIN { trace(strtok(NULL, `initname)); }
BEGIN { trace(strtoll(`initname)); }
BEGIN { trace(strtoll(`initname, 10)); }
BEGIN { trace(substr(`initname, 2, 3)); }

BEGIN { trace(ddi_pathname(`top_devinfo, 1)); }
BEGIN { trace(strjoin(`initname, "foo")); }
BEGIN { trace(strjoin("foo", `initname)); }
BEGIN { trace(dirname(`initname)); }
BEGIN { trace(cleanpath(`initname)); }

BEGIN { j = "{\"/sbin/init\":\"uh oh\"}"; trace(json(j, `initname)); }
BEGIN { trace(json(`initname, "x")); }

ERROR {
	errorcount++;
}

BEGIN /errorcount == expected_errorcount/ {
	trace("test passed");
	exit(0);
}

BEGIN /errorcount != expected_errorcount/ {
	printf("fail: expected %d.  saw %d.", expected_errorcount, errorcount);
	exit(1);
}
EOF


exit $?
