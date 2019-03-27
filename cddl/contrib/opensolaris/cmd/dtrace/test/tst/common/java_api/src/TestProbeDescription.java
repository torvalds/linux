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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * ident	"%Z%%M%	%I%	%E% SMI"
 */

import org.opensolaris.os.dtrace.*;
import java.util.logging.*;

/**
 * Regression for 6399915 ProbeDescription single arg constructor should
 * parse probe descriptions.
 */
public class TestProbeDescription {
    public static void
    main(String[] args)
    {
	ProbeDescription p = null;
	int len = args.length;
	if (len == 0) {
	    p = new ProbeDescription("syscall:::entry");
	} else if (len == 1) {
	    p = new ProbeDescription(args[0]);
	} else if (len == 2) {
	    p = new ProbeDescription(args[0], args[1]);
	} else if (len == 3) {
	    p = new ProbeDescription(args[0], args[1], args[2]);
	} else if (len == 4) {
	    p = new ProbeDescription(args[0], args[1], args[2], args[3]);
	}
	System.out.println(p);
    }
}
