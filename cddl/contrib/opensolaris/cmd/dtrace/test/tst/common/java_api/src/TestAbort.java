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
import java.util.NoSuchElementException;

/**
 * Regression for 6426129 abort() after close() throws
 * NoSuchElementException.
 */
public class TestAbort {
    static boolean aborted = false;

    public static void
    main(String[] args)
    {
	Consumer consumer = new LocalConsumer();

	// Test for deadlock (bug 6419880)
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

	// Should be able to abort an unopened consumer
	try {
	    aborted = false;
	    consumer.addConsumerListener(new ConsumerAdapter() {
		public void consumerStopped(ConsumerEvent e) {
		    aborted = true;
		}
	    });
	    consumer.abort();
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
	    if (!aborted) {
		throw new IllegalStateException("consumer not aborted");
	    }
	    consumer.close();
	} catch (Exception e) {
	    e.printStackTrace();
	    System.exit(1);
	}

	consumer = new LocalConsumer();

	// Should be safe to call abort() in any state
	try {
	    consumer.abort();
	    consumer.open();
	    consumer.abort();
	    consumer.compile("syscall:::entry { @[execname] = count(); } " +
		    "tick-101ms { printa(@); }");
	    consumer.abort();
	    consumer.enable();
	    consumer.abort();
	    consumer.go();
	    consumer.abort();
	    consumer.close();
	    // Should be safe to call after close()
	    try {
		consumer.abort();
	    } catch (NoSuchElementException e) {
		e.printStackTrace();
		System.exit(1);
	    }
	} catch (Exception e) {
	    e.printStackTrace();
	    System.exit(1);
	}

	consumer = new LocalConsumer();

	// Tests that close() throws expected exception when called on
	// synchronized consumer.
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
		synchronized (consumer) {
		    consumer.close();
		}
	    } catch (IllegalThreadStateException e) {
		try {
		    consumer.close();
		    System.out.println("Successful");
		    System.exit(0);
		} catch (NoSuchElementException x) {
		    x.printStackTrace();
		    System.exit(1);
		}
	    }
	} catch (DTraceException e) {
	    e.printStackTrace();
	    System.exit(1);
	}
	System.err.println("Failed");
	System.exit(1);
    }
}
