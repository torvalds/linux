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

import java.io.File;
import java.io.IOException;
import java.util.List;
import org.opensolaris.os.dtrace.*;

/**
 * Regression for multi-aggregation printa() corner cases.
 */
public class TestMultiAggPrinta {
    static int exitStatus;

    // Gets a string representation of the given PrintaRecord minus the
    // timestamp of the aggregate snapshot, so that the output is
    // comparable across multiple test runs.
    static String
    printaRecordString(PrintaRecord rec)
    {
	StringBuffer buf = new StringBuffer();
	buf.append(PrintaRecord.class.getName());
	buf.append("[aggregations = ");
	buf.append(rec.getAggregations());
	buf.append(", formattedStrings = ");
	buf.append(rec.getFormattedStrings());
	buf.append(", tuples = ");
	buf.append(rec.getTuples());
	buf.append(", output = ");
	buf.append(rec.getOutput());
	buf.append(']');
	return buf.toString();
    }

    static String
    probeDataString(ProbeData data)
    {
	StringBuffer buf = new StringBuffer();
	buf.append(ProbeData.class.getName());
	buf.append("[epid = ");
	buf.append(data.getEnabledProbeID());
	// Do not include cpu, since it can change across test runs
	buf.append(", enabledProbeDescription = ");
	buf.append(data.getEnabledProbeDescription());
	buf.append(", flow = ");
	buf.append(data.getFlow());
	buf.append(", records = ");

	List <Record> records = data.getRecords();
	Record record;
	Object value;
	buf.append('[');
	for (int i = 0; i < records.size(); ++i) {
	    if (i > 0) {
		buf.append(", ");
	    }
	    record = records.get(i);
	    if (record instanceof ValueRecord) {
		value = ((ValueRecord)record).getValue();
		if (value instanceof String) {
		    buf.append("\"");
		    buf.append((String)value);
		    buf.append("\"");
		} else {
		    buf.append(record);
		}
	    } else if (record instanceof PrintaRecord) {
		PrintaRecord printa = (PrintaRecord)record;
		buf.append(printaRecordString(printa));
	    } else {
		buf.append(record);
	    }
	}
	buf.append(']');
	return buf.toString();
    }

    public static void
    main(String[] args)
    {
	if (args.length != 1) {
	    System.err.println("usage: java TestMultiAggPrinta <script>");
	    System.exit(2);
	}

	final Consumer consumer = new LocalConsumer();
	consumer.addConsumerListener(new ConsumerAdapter() {
	    public void dataReceived(DataEvent e) {
		ProbeData data = e.getProbeData();
		List <Record> records = data.getRecords();
		for (Record r : records) {
		    if (r instanceof ExitRecord) {
			ExitRecord exitRecord = (ExitRecord)r;
			exitStatus = exitRecord.getStatus();
		    }
		}
		System.out.println(probeDataString(e.getProbeData()));
	    }
	    public void consumerStopped(ConsumerEvent e) {
		consumer.close();
		System.exit(exitStatus);
	    }
	});

	File file = new File(args[0]);
	try {
	    consumer.open();
	    consumer.compile(file);
	    consumer.enable();
	    consumer.go();
	} catch (DTraceException e) {
	    e.printStackTrace();
	    System.exit(1);
	} catch (IOException e) {
	    e.printStackTrace();
	    System.exit(1);
	}
    }
}
