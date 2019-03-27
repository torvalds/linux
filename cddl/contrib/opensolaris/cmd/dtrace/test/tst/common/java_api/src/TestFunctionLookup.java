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

import org.opensolaris.os.dtrace.*;

/**
 * Regression for bug 6413280 lookupKernelFunction() and
 * lookupUserFunction() truncate last character.
 */
public class TestFunctionLookup {
    static final String kernelLookupProgram = "sdt:::callout-start { " +
           "@[((callout_t *)arg0)->c_func] = count(); }";
    static final String userLookupProgram = "pid$target::f2:entry { " +
           "@[arg0] = count(); }";

    public static void
    main(String[] args)
    {
	if (args.length != 1) {
	    System.err.println("usage: java TestFunctionLookup <command>");
	    System.exit(1);
	}
	String cmd = args[0];

	Consumer consumer = new LocalConsumer();
	try {
	    consumer.open();
	    consumer.compile(kernelLookupProgram);
	    consumer.enable();
	    consumer.go();
	    Aggregate a;
	    Number address;
	    String f;
	    boolean done = false;
	    for (int i = 0; (i < 20) && !done; ++i) {
		Thread.currentThread().sleep(100);
		a = consumer.getAggregate();
		for (Aggregation agg : a.getAggregations()) {
		    for (Tuple tuple : agg.asMap().keySet()) {
			address = (Number)tuple.get(0).getValue();
			if (address instanceof Integer) {
			    int addr = (Integer)address;
			    f = consumer.lookupKernelFunction(addr);
			} else {
			    long addr = (Long)address;
			    f = consumer.lookupKernelFunction(addr);
			}
			if (f.equals("genunix`cv_wakeup")) {
			    System.out.println(f);
			    done = true;
			}
		    }
		}
	    }
	    consumer.close();
	} catch (Exception e) {
	    e.printStackTrace();
	    System.exit(1);
	}

	consumer = new LocalConsumer();
	try {
	    consumer.open();
	    int pid = consumer.createProcess(cmd);
	    consumer.compile(userLookupProgram);
	    consumer.enable();
	    consumer.go();
	    Thread.currentThread().sleep(500);
	    Aggregate a = consumer.getAggregate();
	    Number address;
	    String f;
	    for (Aggregation agg : a.getAggregations()) {
		for (Tuple tuple : agg.asMap().keySet()) {
		    address = (Number)tuple.get(0).getValue();
		    if (address instanceof Integer) {
			int addr = (Integer)address;
			f = consumer.lookupUserFunction(pid, addr);
		    } else {
			long addr = (Long)address;
			f = consumer.lookupUserFunction(pid, addr);
		    }
		    System.out.println(f);
		}
	    }
	    consumer.close();
	} catch (Exception e) {
	    e.printStackTrace();
	    System.exit(1);
	}
    }
}
