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

import org.opensolaris.os.dtrace.*;

/**
 * Regression for 6506495 -DJAVA_DTRACE_MAX_CONSUMERS=N for any N &lt; 8
 * is treated as if it were 8.
 */
public class TestMaxConsumers {
    static final String MAX_CONSUMERS_PROPERTY_NAME =
	    "JAVA_DTRACE_MAX_CONSUMERS";

    static Integer
    getIntegerProperty(String name)
    {
	Integer value = null;
	String property = System.getProperty(name);
	if (property != null && property.length() != 0) {
	    try {
		value = Integer.parseInt(property);
	    } catch (NumberFormatException e) {
		e.printStackTrace();
	    }
	}
	return value;
    }

    public static void
    main(String[] args)
    {
	Integer property = getIntegerProperty(MAX_CONSUMERS_PROPERTY_NAME);
	int max = (property == null ? 0 : property);
	int n = (property == null ? 11 : (max < 1 ? 1 : max));

	Consumer[] consumers = new Consumer[n];
	try {
	    for (int i = 0; i < n; ++i) {
		consumers[i] = new LocalConsumer();
		consumers[i].open();
	    }
	    for (int i = 0; i < n; ++i) {
		consumers[i].close();
	    }
	    for (int i = 0; i < n; ++i) {
		consumers[i] = new LocalConsumer();
		consumers[i].open();
	    }
	} catch (Exception e) {
	    e.printStackTrace();
	    System.exit(1);
	}

	try {
	    Consumer consumer = new LocalConsumer();
	    consumer.open();
	    if (max > 0) {
		System.out.println("Error: " + (max + 1) + " > " +
			MAX_CONSUMERS_PROPERTY_NAME);
	    } else {
		System.out.println("Success");
	    }
	    consumer.close();
	} catch (Exception e) {
	    System.out.println("Success");
	} finally {
	    for (int i = 0; i < n; ++i) {
		consumers[i].close();
	    }
	}
    }
}
