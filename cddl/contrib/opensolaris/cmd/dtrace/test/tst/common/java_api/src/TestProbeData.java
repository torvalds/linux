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

import java.util.*;
import org.opensolaris.os.dtrace.*;

/**
 * Regression test verifies that ProbeData instances sort as expected
 * with multiple enabled probe IDs and multiple records including byte
 * array (sorted as unsigned bytes), stand-alone ufunc() action, and
 * signed integer.
 */
public class TestProbeData {
    public static final String PROGRAM =
	    "pid$target::fN:entry\n" +
	    "{\n" +
	    "        tracemem(copyin(arg1, 6), 6);\n" +
	    "        ufunc(arg0);\n" +
	    "        trace((int)arg2);\n" +
	    "}" +
	    "" +
	    "pid$target::fN2:entry\n" +
	    "{\n" +
	    "        tracemem(copyin(arg1, 6), 6);\n" +
	    "        ufunc(arg0);\n" +
	    "        trace((int)arg2);\n" +
	    "}";

    static String
    getString(ProbeData p)
    {
	StringBuilder buf = new StringBuilder();
	buf.append("[probe ");
	buf.append(p.getEnabledProbeID());
	buf.append(' ');
	ProbeDescription d = p.getEnabledProbeDescription();
	buf.append("pid$target");
	buf.append(':');
	buf.append(d.getModule());
	buf.append(':');
	buf.append(d.getFunction());
	buf.append(':');
	buf.append(d.getName());
	buf.append(' ');
	buf.append(p.getRecords());
	buf.append("]");
	return buf.toString();
    }

    public static void
    main(String[] args)
    {
	if (args.length != 1) {
	    System.err.println("usage: java TestProbedata <command>");
	    System.exit(2);
	}

	String command = args[0];
	final Consumer consumer = new LocalConsumer();
	final List <ProbeData> list = new ArrayList <ProbeData> ();
	consumer.addConsumerListener(new ConsumerAdapter() {
	    public void dataReceived(DataEvent e) {
		list.add(e.getProbeData());
	    }
	    public void consumerStopped(ConsumerEvent e) {
		Collections.sort(list);
		for (ProbeData p : list) {
		    System.out.println(getString(p));
		    System.out.println();
		}
		consumer.close();
	    }
	});

	try {
	    consumer.open();
	    consumer.createProcess(command);
	    consumer.compile(PROGRAM);
	    consumer.enable();
	    consumer.go();
	} catch (DTraceException e) {
	    e.printStackTrace();
	}
    }
}
