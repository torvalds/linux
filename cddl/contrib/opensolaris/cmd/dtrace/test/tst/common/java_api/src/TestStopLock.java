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
 * Test for bug 6399888 stop() hangs if ConsumerListener calls
 * synchronized Consumer method
 */
public class TestStopLock {
    public static void
    main(String[] args)
    {
	final Consumer consumer = new LocalConsumer();
	consumer.addConsumerListener(new ConsumerAdapter() {
	    @Override
	    public void intervalBegan(ConsumerEvent e) {
		consumer.isRunning();
	    }
	});

	try {
	    consumer.open();
	    consumer.compile("syscall:::entry { @[execname] = count(); } " +
		    "tick-101ms { printa(@); }");
	    consumer.enable();
	    consumer.go();
	    try {
		Thread.currentThread().sleep(500);
	    } catch (InterruptedException e) {
		e.printStackTrace();
		System.exit(1);
	    }
	    consumer.stop();
	    consumer.close();
	} catch (DTraceException e) {
	    e.printStackTrace();
	    System.exit(1);
	}
	System.out.println("Successful");
    }
}
