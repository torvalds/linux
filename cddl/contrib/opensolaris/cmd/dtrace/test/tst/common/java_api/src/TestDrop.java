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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * ident	"%Z%%M%	%I%	%E% SMI"
 */
import java.util.*;
import java.util.concurrent.atomic.*;
import org.opensolaris.os.dtrace.*;

/**
 * Regression test for 6521523 aggregation drops can hang the Java
 * DTrace API.
 */
public class TestDrop {
    static final String PROGRAM =
	    "fbt:genunix::entry { @[execname, pid] = count(); }";

    static AtomicLong consumerThreadID = new AtomicLong();
    static AtomicLong getAggregateThreadID = new AtomicLong();
    static AtomicBoolean done = new AtomicBoolean();
    static int seconds;

    private static void
    startTimer()
    {
	if (seconds <= 0) {
	    return;
	}

	final Timer timer = new Timer();
	timer.schedule(new TimerTask() {
	    public void run() {
		done.set(true);
		timer.cancel();
	    }
	}, seconds * 1000L);
    }

    private static void
    sampleAggregate(Consumer consumer) throws DTraceException
    {
	while (consumer.isRunning() && !done.get()) {
	    try {
		Thread.currentThread().sleep(50);
	    } catch (InterruptedException e) {
	    }

	    consumer.getAggregate(Collections. <String> emptySet());
	}
    }

    private static void
    startAggregateThread(final Consumer consumer)
    {
	Runnable aggregateSampler = new Runnable() {
	    public void run() {
		Thread t = Thread.currentThread();
		getAggregateThreadID.set(t.getId());
		Throwable x = null;
		try {
		    sampleAggregate(consumer);
		} catch (Throwable e) {
		    x = e;
		}

		if (Thread.holdsLock(LocalConsumer.class)) {
		    if (x != null) {
			x.printStackTrace();
		    }
		    System.out.println("Lock held");
		    System.exit(1);
		} else {
		    System.out.println("Lock released");
		    consumer.close(); // blocks if lock held
		}
	    }
	};

	Thread t = new Thread(aggregateSampler, "Aggregate Sampler");
	t.start();
    }

    static void
    usage()
    {
	System.err.println("usage: java TestDrop [ seconds ]");
	System.exit(2);
    }

    public static void
    main(String[] args)
    {
	if (args.length == 1) {
	    try {
		seconds = Integer.parseInt(args[0]);
	    } catch (NumberFormatException e) {
		usage();
	    }
	} else if (args.length > 1) {
	    usage();
	}

	final Consumer consumer = new LocalConsumer() {
	    protected Thread createThread() {
		Runnable worker = new Runnable() {
		    public void run() {
			Thread t = Thread.currentThread();
			consumerThreadID.set(t.getId());
			work();
		    }
		};
		Thread t = new Thread(worker);
		return t;
	    }
	};

	consumer.addConsumerListener(new ConsumerAdapter() {
	    public void consumerStarted(ConsumerEvent e) {
		startAggregateThread(consumer);
		startTimer();
	    }
	    public void dataDropped(DropEvent e) throws ConsumerException {
		Thread t = Thread.currentThread();
		if (t.getId() == getAggregateThreadID.get()) {
		    Drop drop = e.getDrop();
		    throw new ConsumerException(drop.getDefaultMessage(),
			    drop);
		}
	    }
	});

	try {
	    consumer.open();
	    consumer.setOption(Option.aggsize, Option.kb(1));
	    consumer.setOption(Option.aggrate, Option.millis(101));
	    consumer.compile(PROGRAM);
	    consumer.enable();
	    consumer.go(new ExceptionHandler() {
		public void handleException(Throwable e) {
		    e.printStackTrace();
		}
	    });
	} catch (DTraceException e) {
	    e.printStackTrace();
	}
    }
}
