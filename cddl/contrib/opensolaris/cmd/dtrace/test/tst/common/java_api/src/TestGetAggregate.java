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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

import org.opensolaris.os.dtrace.*;
import java.util.*;

/**
 * Assert getAggregate() can explicitly specify the anonymous aggregation.
 */
public class TestGetAggregate {
    static final String programString =
	    "profile:::tick-50ms" +
	    "{" +
	    "        @ = count();" +
	    "        @a = count();" +
	    "}";

    static final String ANONYMOUS_AGGREGATION = "";
    static final int TICK = 50;
    static final int EXPECTED_TICKS = 3;
    static final int INTERVALS = 4;

    static void
    testIncluded(Consumer consumer, String ... aggregationNames)
	    throws DTraceException, InterruptedException
    {
	Aggregate aggregate;
	Set <String> included = new HashSet <String> ();
	int n = 1;

	for (String name : aggregationNames) {
	    included.add(name);
	}

	// Wait up to a full second to obtain aggregate data. Without a
	// time limit, we'll loop forever if no aggregation was
	// successfully included.
	do {
	    Thread.sleep(TICK);
	    aggregate = consumer.getAggregate(included, null);
	} while (aggregate.asMap().isEmpty() && n++ < (1000 / TICK));

	for (String name : included) {
	    if (aggregate.getAggregation(name) == null) {
		throw new IllegalStateException("@" + name +
			" was explicitly included but did not appear " +
			"in the aggregate");
	    }
	}
	for (Aggregation a : aggregate.getAggregations()) {
	    if (!included.contains(a.getName())) {
		throw new IllegalStateException("@" + a.getName() +
			" was not explicitly included but appeared " +
			"in the aggregate anyway");
	    }
	}

	if (!consumer.isRunning()) {
	    throw new IllegalStateException("consumer exited");
	}
    }

    static void
    testCleared(Consumer consumer, String ... aggregationNames)
	    throws DTraceException, InterruptedException
    {
	Aggregate aggregate;
	AggregationRecord rec;
	long value;
	Long firstValue;
	int n = 1;
	Map <String, Long> firstValues = new HashMap <String, Long> ();
	Set <String> cleared = new HashSet <String> ();

	for (String name : aggregationNames) {
	    cleared.add(name);
	}

	do {
	    Thread.sleep(TICK);
	    aggregate = consumer.getAggregate(null, cleared);
	} while (aggregate.asMap().isEmpty() && n++ < (1000 / TICK));
	n = 1;

	do {
	    Thread.sleep(TICK * EXPECTED_TICKS);
	    aggregate = consumer.getAggregate(null, cleared);

	    for (Aggregation a : aggregate.getAggregations()) {
		if (!firstValues.containsKey(a.getName())) {
		    rec = a.getRecord(Tuple.EMPTY);
		    value = rec.getValue().getValue().longValue();
		    firstValues.put(a.getName(), value);
		}
	    }
	} while (consumer.isRunning() && n++ < INTERVALS);

	for (Aggregation a : aggregate.getAggregations()) {
	    rec = a.getRecord(Tuple.EMPTY);
	    value = rec.getValue().getValue().longValue();
	    firstValue = firstValues.get(a.getName());

	    if (cleared.contains(a.getName())) {
		// last value should be about the same as first value
		if (value > (firstValue * 2)) {
		    throw new IllegalStateException(
			    "@" + a.getName() + " should have " +
			    "been cleared but instead grew from " +
			    firstValue + " to " + value);
		}
	    } else {
		// last value should be about (INTERVALS * firstValue)
		if (value < (firstValue * 2)) {
		    throw new IllegalStateException(
			    "@" + a.getName() + " should have " +
			    "accumulated a running total but " +
			    "instead went from " +
			    firstValue + " to " + value);
		}
	    }
	}

	if (!consumer.isRunning()) {
	    throw new IllegalStateException("consumer exited");
	}
    }

    static Integer includedStatus;
    static Integer clearedStatus;

    static void
    startIncludedTest()
    {
	final Consumer consumer = new LocalConsumer();
	consumer.addConsumerListener(new ConsumerAdapter() {
	    public void consumerStarted(ConsumerEvent e) {
		new Thread(new Runnable() {
		    public void run() {
			try {
			    testIncluded(consumer, ANONYMOUS_AGGREGATION);
			    includedStatus = 0;
			} catch (Exception e) {
			    includedStatus = 1;
			    e.printStackTrace();
			} finally {
			    consumer.abort();
			}
		    }
		}).start();
	    }
	});

	try {
	    consumer.open();
	    consumer.setOption(Option.aggrate, Option.millis(TICK));
	    consumer.compile(programString);
	    consumer.enable();
	    consumer.go();
	} catch (Exception e) {
	    includedStatus = 1;
	    e.printStackTrace();
	}
    }

    static void
    startClearedTest()
    {
	final Consumer consumer = new LocalConsumer();
	consumer.addConsumerListener(new ConsumerAdapter() {
	    public void consumerStarted(ConsumerEvent e) {
		new Thread(new Runnable() {
		    public void run() {
			try {
			    testCleared(consumer, ANONYMOUS_AGGREGATION);
			    clearedStatus = 0;
			} catch (Exception e) {
			    clearedStatus = 1;
			    e.printStackTrace();
			} finally {
			    consumer.abort();
			}
		    }
		}).start();
	    }
	});

	try {
	    consumer.open();
	    consumer.setOption(Option.aggrate, Option.millis(TICK));
	    consumer.compile(programString);
	    consumer.enable();
	    consumer.go();
	} catch (Exception e) {
	    clearedStatus = 1;
	    e.printStackTrace();
	}
    }

    public static void
    main(String[] args)
    {
	startIncludedTest();

	do {
	    try {
		Thread.sleep(TICK);
	    } catch (InterruptedException e) {
		e.printStackTrace();
	    }
	} while (includedStatus == null);

	startClearedTest();

	do {
	    try {
		Thread.sleep(TICK);
	    } catch (InterruptedException e) {
		e.printStackTrace();
	    }
	} while (clearedStatus == null);

	if (includedStatus != 0 || clearedStatus != 0) {
	    System.out.println("Failure");
	    System.exit(1);
	}

	System.out.println("Success");
    }
}
