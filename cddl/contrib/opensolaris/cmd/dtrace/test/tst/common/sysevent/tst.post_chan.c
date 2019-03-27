/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <libsysevent.h>
#include <stdio.h>

int
main(int argc, char **argv)
{
	evchan_t *ch;

	if (sysevent_evc_bind("channel_dtest", &ch,
	    EVCH_CREAT | EVCH_HOLD_PEND) != 0) {
		(void) fprintf(stderr, "failed to bind to sysevent channel\n");
		return (1);
	}

	for (;;) {
		if (sysevent_evc_publish(ch, "class_dtest", "subclass_dtest",
		    "vendor_dtest", "publisher_dtest", NULL, EVCH_SLEEP) != 0) {
			(void) sysevent_evc_unbind(ch);
			(void) fprintf(stderr, "failed to publisth sysevent\n");
			return (1);
		}
		sleep(1);
	}
}
