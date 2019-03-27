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

/**
 * Regression for bug 6419880 close() hangs running consumer.
 */
public class TestClose {
    public static void
    main(String[] args)
    {
	Consumer consumer = new LocalConsumer();

	try {
	    consumer.open();
	    consumer.compile("syscall:::entry { @[execname] = count(); } " +
		    "tick-101ms { printa(@); }");
	    consumer.enable();
	    consumer.go();
	    try {
		Thread.currentThread().sleep(1000);
	    } catch (InterruptedException e) {
		e.printStackTrace();
		System.exit(1);
	    }
	    consumer.close();
	} catch (DTraceException e) {
	    e.printStackTrace();
	    System.exit(1);
	}

	consumer = new LocalConsumer();

	try {
	    consumer.open();
	    consumer.compile("syscall:::entry { @[execname] = count(); } " +
		    "tick-101ms { printa(@); }");
	    consumer.enable();
	    consumer.go();
	    try {
		Thread.currentThread().sleep(1000);
	    } catch (InterruptedException e) {
		e.printStackTrace();
		System.exit(1);
	    }
	    try {
		// Test new rule that close() is illegal while holding
		// lock on consumer.
		synchronized (consumer) {
		    consumer.close();
		}
	    } catch (IllegalThreadStateException e) {
		consumer.close();
		System.out.println("Successful");
		System.exit(0);
	    }
	} catch (DTraceException e) {
	    e.printStackTrace();
	    System.exit(1);
	}
	System.err.println("Failed");
	System.exit(1);
    }
}
