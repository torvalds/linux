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
 * Prove that enable() handles multiple programs, recognizing programs
 * that are already enabled and programs that were compiled by another
 * consumer.
 */
public class TestEnable {
    static void
    exit(int status)
    {
	System.out.flush();
	System.err.flush();
	System.exit(status);
    }

    public static void
    main(String[] args)
    {
	Consumer consumer = new LocalConsumer();

	try {
	    consumer.open();
	    Program p0 = consumer.compile("dtrace:::BEGIN");
	    Program p1 = consumer.compile("syscall:::entry");
	    Program p2 = consumer.compile("dtrace:::END");
	    consumer.enable(p0);
	    consumer.enable(p1);
	    try {
		consumer.go();
		System.err.println("go() illegal, not all programs " +
			"enabled (p0, p1)");
		exit(1);
	    } catch (IllegalStateException e) {
		System.out.println(e);
	    } catch (Exception e) {
		e.printStackTrace();
		exit(1);
	    }
	    try {
		consumer.enable();
		System.err.println("enable() illegal, some programs " +
			"already enabled (p0, p1)");
		exit(1);
	    } catch (IllegalStateException e) {
		System.out.println(e);
	    } catch (Exception e) {
		e.printStackTrace();
		exit(1);
	    }
	    try {
		consumer.enable(p0);
		System.err.println("cannot enable a program that " +
			"has already been enabled (p0)");
		exit(1);
	    } catch (IllegalStateException e) {
		System.out.println(e);
	    } catch (Exception e) {
		e.printStackTrace();
		exit(1);
	    }
	    consumer.enable(p2);
	    Program p3 = consumer.compile("syscall:::return");
	    try {
		consumer.go();
		System.err.println("go() illegal, not all programs " +
			"enabled (p0, p1, p2)");
		exit(1);
	    } catch (IllegalStateException e) {
		System.out.println(e);
	    } catch (Exception e) {
		e.printStackTrace();
		exit(1);
	    }
	    try {
		consumer.enable();
		System.err.println("enable() illegal, some programs " +
			"already enabled (p0, p1, p2)");
		exit(1);
	    } catch (IllegalStateException e) {
		System.out.println(e);
	    } catch (Exception e) {
		e.printStackTrace();
		exit(1);
	    }
	    // Try to fool the consumer with a program compiled by
	    // another consumer
	    Consumer consumer2 = new LocalConsumer();
	    consumer2.open();
	    Program p3x = consumer2.compile("syscall:::return");
	    try {
		consumer.enable(p3x);
		System.err.println("cannot enable program compiled " +
			"by another consumer");
		exit(1);
	    } catch (IllegalArgumentException e) {
		System.out.println(e);
	    } catch (Exception e) {
		e.printStackTrace();
		exit(1);
	    } finally {
		consumer2.close();
	    }
	    consumer.enable(p3);
	    consumer.go();
	    consumer.close();

	    // Enable all compiled programs at once
	    consumer = new LocalConsumer();
	    consumer.open();
	    consumer.compile("dtrace:::BEGIN");
	    consumer.compile("syscall:::entry");
	    consumer.compile("dtrace:::END");
	    consumer.enable();
	    consumer.go();
	    consumer.close();
	    exit(0);
	} catch (DTraceException e) {
	    e.printStackTrace();
	    exit(1);
	}
    }
}
