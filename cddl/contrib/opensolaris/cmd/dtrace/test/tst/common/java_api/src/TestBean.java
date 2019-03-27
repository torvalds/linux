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
 *
 * ident	"%Z%%M%	%I%	%E% SMI"
 */

import org.opensolaris.os.dtrace.*;
import java.util.*;
import java.io.*;
import java.beans.*;
import java.lang.reflect.*;

/**
 * Regression test for serialization and XML encoding/decoding.  Tests
 * every Serializable class in the Java DTrace API by creating a dummy
 * instance, writing it to a file, then reading it back in and comparing
 * the string values of the object before and after, as well as
 * verifying object equality before and after if the class overrides the
 * equals() method.
 */
public class TestBean {
    public static final String[] TESTS = new String[] {
	"ExitRecord",
	"AggregationRecord",
	"Aggregation",
	"Tuple",
	"ScalarRecord",
	"KernelStackRecord",
	"LogDistribution",
	"LinearDistribution",
	"Option",
	"ProcessState",
	"ProbeDescription",
	"PrintaRecord",
	"PrintfRecord",
	"ProbeData",
	"Aggregate",
	"UserStackRecord",
	"AvgValue",
	"CountValue",
	"SumValue",
	"MinValue",
	"MaxValue",
	"Error",
	"Drop",
	"InterfaceAttributes",
	"ProgramInfo",
	"ProbeInfo",
	"Probe",
	"Flow",
	"KernelSymbolRecord",
	"UserSymbolRecord",
	"UserSymbolRecord$Value",
	"Program",
	"Program$File",
	"StddevValue"
    };

    static File file;

    static void
    exit(int status)
    {
	System.out.flush();
	System.err.flush();
	System.exit(status);
    }

    public static XMLEncoder
    getXMLEncoder(File file)
    {
        XMLEncoder encoder = null;
        try {
            OutputStream out = new BufferedOutputStream
                    (new FileOutputStream(file));
            encoder = new XMLEncoder(out);
        } catch (Exception e) {
	    e.printStackTrace();
	    exit(1);
        }
        return encoder;
    }

    public static XMLDecoder
    getXMLDecoder(File file)
    {
        return getXMLDecoder(file, null);
    }

    public static XMLDecoder
    getXMLDecoder(File file, ExceptionListener exceptionListener)
    {
        XMLDecoder decoder = null;
        try {
            InputStream in = new BufferedInputStream
                    (new FileInputStream(file));
            decoder = new XMLDecoder(in, null, exceptionListener);
        } catch (Exception e) {
	    e.printStackTrace();
	    exit(1);
        }
        return decoder;
    }

    public static ExitRecord
    getExitRecord()
    {
	ExitRecord r = new ExitRecord(1);
	return r;
    }

    public static AggregationRecord
    getAggregationRecord()
    {
	Tuple tuple = getTuple();
	AggregationValue value = new CountValue(7);
	AggregationRecord r = new AggregationRecord(tuple, value);
	return r;
    }

    public static Aggregation
    getAggregation()
    {
	List < AggregationRecord > list =
	    new ArrayList < AggregationRecord > ();
	AggregationRecord r;
	r = getAggregationRecord();
	list.add(r);

	ValueRecord v1 = new ScalarRecord(new byte[] {(byte)1, (byte)2,
	    (byte)3}, 3);
	ValueRecord v2 = new ScalarRecord("shebang!", 256);
	Tuple tuple = new Tuple(v1, v2);
	AggregationValue value = getLinearDistribution();
	r = new AggregationRecord(tuple, value);
	list.add(r);

	Aggregation a = new Aggregation("counts", 2, list);
	return a;
    }

    public static Tuple
    getTuple()
    {
	ValueRecord r1 = new ScalarRecord("cat", 256);
	ValueRecord r2 = new ScalarRecord(new Integer(9), 2);
	ValueRecord r3 = new KernelStackRecord(
		new StackFrame[] {
		    new StackFrame("has"),
		    new StackFrame("nine"),
		    new StackFrame("lives")},
		new byte[] { (byte)0, (byte)1, (byte)2 });
	ValueRecord r4 = new ScalarRecord(new byte[] {(byte)1, (byte)2,
	    (byte)3}, 3);

	Tuple tuple = new Tuple(r1, r2, r3, r4);
	return tuple;
    }

    public static ScalarRecord
    getScalarRecord()
    {
	Object v = new byte[] {(byte)1, (byte)2, (byte)3};
	ScalarRecord r = new ScalarRecord(v, 3);
	return r;
    }

    public static KernelStackRecord
    getKernelStackRecord()
    {
	StackFrame[] stackFrames = new StackFrame[] {
	    new StackFrame("Frame 1"),
	    new StackFrame("Frame 2"),
	    new StackFrame("Frame 3")
	};
	KernelStackRecord r = new KernelStackRecord(stackFrames,
		new byte[] { (byte)0, (byte)1, (byte)2 });
	return r;
    }

    public static LogDistribution
    getLogDistribution()
    {
	List < Distribution.Bucket > buckets =
		new ArrayList < Distribution.Bucket > ();
	Distribution.Bucket bucket;
	int n = 0;
	long base = 0;
	long i;
	long sign;
	long nextSign;
	long power;
	long nextPower;
	long lowerBound;
	long upperBound;
	for (i = -62; i <= 62; ++i) {
	    if (i == 0) {
		bucket = new Distribution.Bucket(-1, -1, n++);
		buckets.add(bucket);
		bucket = new Distribution.Bucket(0, 0, n++);
		buckets.add(bucket);
		bucket = new Distribution.Bucket(1, 1, n++);
		buckets.add(bucket);
		continue;
	    }
	    sign = ((i < 0) ? -1L : 1L);
	    power = (sign * i);
	    nextSign = (((i + 1) < 0) ? -1L : 1L);
	    nextPower = (nextSign * (i + 1));
	    lowerBound = sign * ((long) Math.pow(2L, power));
	    upperBound = (nextPower == 0 ? -2L :
		    (nextSign * ((long) Math.pow(2L, nextPower))) - 1);
	    if ((upperBound > 0) && ((upperBound * 2L) < 0)) {
		upperBound = Long.MAX_VALUE;
	    }
	    bucket = new Distribution.Bucket(lowerBound, upperBound, n++);
	    buckets.add(bucket);
	}
	LogDistribution d = new LogDistribution(buckets);
	return d;
    }

    public static LinearDistribution
    getLinearDistribution()
    {
	List < Distribution.Bucket > buckets =
		new ArrayList < Distribution.Bucket > ();
	Distribution.Bucket bucket;
	int n = 10; // number of buckets
	int base = 1;
	int step = 10;
	bucket = new Distribution.Bucket(Long.MIN_VALUE, (base - 1), 0);
	buckets.add(bucket);
	for (int i = base; i < (n * step); i += step) {
	    bucket = new Distribution.Bucket(i, (i + (step - 1)),
		    ((i - 1) / step));
	    buckets.add(bucket);
	}
	bucket = new Distribution.Bucket((n * step) + 1, Long.MAX_VALUE, 0);
	buckets.add(bucket);
	LinearDistribution d = new LinearDistribution(base, step, buckets);
	return d;
    }

    public static Option
    getOption()
    {
	Option option = new Option("aggrate", "1s");
	return option;
    }

    public static ProcessState
    getProcessState()
    {
	ProcessState p = new ProcessState(123456, "UNDEAD",
		3, "SIGSTOP",
		-2, "Process stopped on dime");
	return p;
    }

    public static ProbeDescription
    getProbeDescription()
    {
	ProbeDescription d = new ProbeDescription(256, "syscall", null,
	    "malloc", "entry");
	return d;
    }

    public static PrintaRecord
    getPrintaRecord()
    {
	List < Aggregation > aggregations = new ArrayList < Aggregation > ();
	Aggregation a = getAggregation();
	aggregations.add(a);
	aggregations.add(a);
	Map < Tuple, String > formattedOutput =
		new HashMap < Tuple, String > ();
	for (Tuple t : a.asMap().keySet()) {
	    formattedOutput.put(t, "cat");
	}
	List < Tuple > tuples = new ArrayList < Tuple > ();
	for (Tuple t : a.asMap().keySet()) {
	    tuples.add(t);
	}
	Collections.sort(tuples);
	PrintaRecord r = new PrintaRecord(1234567890L,
	    aggregations, formattedOutput, tuples,
	    "Yes, this is the formatted printa() output");
	return r;
    }

    public static PrintfRecord
    getPrintfRecord()
    {
	List < ValueRecord > list = new ArrayList < ValueRecord > ();
	ValueRecord v1 = getScalarRecord();
	ValueRecord v2 = new ScalarRecord(new Integer(7), 4);
	list.add(v1);
	list.add(v2);
	PrintfRecord r = new PrintfRecord(list,
		"long formatted string");
	return r;
    }

    public static ProbeData
    getProbeData()
    {
	List < Record > list = new ArrayList < Record > ();
	list.add(getPrintaRecord());
	list.add(getPrintfRecord());
	list.add(getScalarRecord());
	list.add(getUserSymbolRecord());
	list.add(getUserStackRecord());
	list.add(getExitRecord());
	ProbeData d = new ProbeData(7, 1, getProbeDescription(),
	    getFlow(), list);
	return d;
    }

    public static Aggregate
    getAggregate()
    {
	List < Aggregation > list = new ArrayList < Aggregation > ();
	list.add(getAggregation());

	List < AggregationRecord > reclist =
	    new ArrayList < AggregationRecord > ();
	AggregationRecord r;
	ValueRecord v1 = new ScalarRecord("cat", 256);
	ValueRecord v2 = new ScalarRecord("dog", 256);
	ValueRecord v3 = new ScalarRecord("mouse", 256);
	ValueRecord v4 = new ScalarRecord("mouse", 256);
	ValueRecord v5 = new ScalarRecord(new Byte((byte) 'C'), 1);
	ValueRecord v6 = new ScalarRecord(new Short((short) 7), 2);
	Tuple tuple = new Tuple(v1, v2, v3, v4, v5, v6);
	AggregationValue value = getCountValue();
	r = new AggregationRecord(tuple, value);
	reclist.add(r);
	list.add(new Aggregation("times", 1, reclist));

        Aggregate a = new Aggregate(1234567890L, list);
	return a;
    }

    public static UserStackRecord
    getUserStackRecord()
    {
	StackFrame[] frames = new StackFrame[] {
	    new StackFrame("User Stack Frame 1"),
	    new StackFrame("User Stack Frame 2"),
	    new StackFrame("User Stack Frame 3")
	};
	UserStackRecord r = new UserStackRecord(123456, frames,
		new byte[] { (byte)0, (byte)1, (byte)2 });
	return r;
    }

    public static AvgValue
    getAvgValue()
    {
	AvgValue v = new AvgValue(5, 20, 4);
	return v;
    }

    public static CountValue
    getCountValue()
    {
	CountValue v = new CountValue(9);
	return v;
    }

    public static MinValue
    getMinValue()
    {
	MinValue v = new MinValue(101);
	return v;
    }

    public static MaxValue
    getMaxValue()
    {
	MaxValue v = new MaxValue(101);
	return v;
    }

    public static SumValue
    getSumValue()
    {
	SumValue v = new SumValue(25);
	return v;
    }

    public static org.opensolaris.os.dtrace.Error
    getError()
    {
	ProbeDescription probe = getProbeDescription();
	org.opensolaris.os.dtrace.Error e =
	    new org.opensolaris.os.dtrace.Error(probe, 8, 3,
	    1, 20, "DTRACEFLT_BADALIGN", -1, "error on enabled probe ID 8 " +
	    "(ID " + probe.getID() + ": " + probe + "): Bad alignment " +
	    "(0x33ef) in action #1 at DIF offset 20");
	return e;
    }

    public static Drop
    getDrop()
    {
	Drop drop = new Drop(2, "SPECBUSY", 72, 1041,
	    "Guess we dropped stuff all over the place.");
	return drop;
    }

    public static InterfaceAttributes
    getInterfaceAttributes()
    {
	InterfaceAttributes a = new InterfaceAttributes(
                InterfaceAttributes.Stability.UNSTABLE,
                InterfaceAttributes.Stability.EVOLVING,
                InterfaceAttributes.DependencyClass.ISA);
	return a;
    }

    public static ProgramInfo
    getProgramInfo()
    {
	ProgramInfo info = new ProgramInfo(getInterfaceAttributes(),
		getInterfaceAttributes(), 256);
	return info;
    }

    public static ProbeInfo
    getProbeInfo()
    {
	ProbeInfo info = new ProbeInfo(getInterfaceAttributes(),
		getInterfaceAttributes());
	return info;
    }

    public static Probe
    getProbe()
    {
	Probe p = new Probe(getProbeDescription(), getProbeInfo());
	return p;
    }

    public static Flow
    getFlow()
    {
	Flow f = new Flow(Flow.Kind.RETURN.name(), 3);
	return f;
    }

    public static KernelSymbolRecord
    getKernelSymbolRecord()
    {
	KernelSymbolRecord r = new KernelSymbolRecord("mod`func+0x4", -1L);
	return r;
    }

    public static UserSymbolRecord
    getUserSymbolRecord()
    {
	UserSymbolRecord r = new UserSymbolRecord(7, "mod`func+0x4", -1L);
	return r;
    }

    public static UserSymbolRecord.Value
    getUserSymbolRecord$Value()
    {
	UserSymbolRecord.Value v = new UserSymbolRecord.Value(7, -1L);
	return v;
    }

    public static Program
    getProgram()
    {
	final String PROGRAM = "syscall:::entry { @[execname] = count(); }";
	Consumer consumer = new LocalConsumer();
	Program p;
	try {
	    consumer.open();
	    p = consumer.compile(PROGRAM);
	    consumer.close();
	} catch (DTraceException e) {
	    e.printStackTrace();
	    p = null;
	}
	return p;
    }

    public static Program.File
    getProgram$File()
    {
	final String PROGRAM = "syscall:::entry { @[execname] = count(); }";
	Consumer consumer = new LocalConsumer();
	Program p;
	try {
            OutputStream out = new FileOutputStream(file);
	    out.write(PROGRAM.getBytes(), 0, PROGRAM.length());
	    out.flush();
	    out.close();
	    consumer.open();
	    p = consumer.compile(file);
	    consumer.close();
	} catch (Exception e) {
	    e.printStackTrace();
	    p = null;
	}
	return Program.File.class.cast(p);
    }

    public static StddevValue
    getStddevValue()
    {
	StddevValue v = new StddevValue(37, 114, 5, Integer.toString(9544));
	return v;
    }

    @SuppressWarnings("unchecked")
    static String
    getString(Object o)
    {
	String s;
	if (o instanceof ScalarRecord) {
	    o = ((ScalarRecord)o).getValue();
	}

	if (o instanceof byte[]) {
	    s = Arrays.toString((byte[])o);
	} else if (o instanceof Object[]) {
	    s = Arrays.toString((Object[])o);
	} else {
	    Class c = o.getClass();
	    try {
		Method m = c.getDeclaredMethod("toLogString");
		s = (String)m.invoke(o);
	    } catch (Exception e) {
		s = o.toString();
	    }
	}
	return s;
    }

    static void
    checkEquality(Object obj, Object newobj)
    {
	// If the class overrides equals(), make sure the re-created
	// object still equals the original object
	try {
	    Method eq = obj.getClass().getDeclaredMethod("equals",
		    Object.class);
	    Boolean ret = (Boolean) eq.invoke(obj, newobj);
	    if (ret != true) {
		System.err.println("serialization failed: " +
			obj.getClass().getName());
		exit(1);
	    }
	} catch (Exception e) {
	    // Does not override equals(), although a super-class might.
	    // A better test would check for any superclass other than
	    // Object.class.
	}
    }

    static void
    performSerializationTest(File file, String classname)
            throws IOException, ClassNotFoundException
    {
	String methodName = "get" + classname;
	Object obj = null;
	Object newobj = null;
	try {
	    Method method = TestBean.class.getDeclaredMethod(methodName);
	    obj = method.invoke(null);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1);
	}

	System.out.println(classname + ":");
	String serialized = getString(obj);
	System.out.println("  serialized: " + serialized);
	FileOutputStream fos = new FileOutputStream(file);
	ObjectOutputStream out = new ObjectOutputStream(fos);
	out.writeObject(obj);
	out.close();
	FileInputStream fis = new FileInputStream(file);
	ObjectInputStream in = new ObjectInputStream(fis);
	newobj = in.readObject();
	in.close();
	String deserialized = getString(newobj);
	System.out.println("  deserialized: " + deserialized);

	if (!serialized.equals(deserialized)) {
	    System.err.println("serialization failed: " + classname);
	    exit(1);
	}
	checkEquality(obj, newobj);
    }

    static void
    performBeanTest(File file, String classname)
    {
	String methodName = "get" + classname;
	Object obj = null;
	Object newobj = null;
	try {
	    Method method = TestBean.class.getDeclaredMethod(methodName);
	    obj = method.invoke(null);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1);
	}

	Class c = obj.getClass();
	if (c.getConstructors().length == 0) {
	    return;
	}

	System.out.println(classname + ":");
	XMLEncoder encoder = getXMLEncoder(file);
	String encoded = getString(obj);
	System.out.println("  encoded: " + encoded);
	encoder.writeObject(obj);
	encoder.close();
	XMLDecoder decoder = getXMLDecoder(file);
	newobj = decoder.readObject();
	String decoded = getString(newobj);
	System.out.println("  decoded: " + decoded);
	decoder.close();

	if (!encoded.equals(decoded)) {
	    System.err.println("bean persistence failed: " + classname);
	    exit(1);
	}
	checkEquality(obj, newobj);
    }

    public static void
    main(String[] args)
    {
	if ((args.length != 1) && (args.length != 2)) {
	    System.err.println("usage: java TestBean < filename > " +
		    "[ < classname > ]");
	    exit(1);
	}

	String filename = args[0];
	String classname = null;
	if (args.length >= 2) {
	    classname = args[1];
	}

	file = new File(filename);
	try {
	    if (!file.canRead()) {
		try {
		    file.createNewFile();
		} catch (Exception e) {
		    System.err.println("failed to create " + filename);
		    exit(1);
		}
	    }
	} catch (SecurityException e) {
	    System.err.println("failed to open " + filename);
	    exit(1);
	}

	String[] tests = (classname == null ? TESTS:
		new String[] { classname });
	try {
	    for (int i = 0; i < tests.length; ++i) {
		performSerializationTest(file, tests[i]);
		performBeanTest(file, tests[i]);
	    }
	} catch (IOException e) {
	    e.printStackTrace();
	    exit(1);
	} catch (ClassNotFoundException e) {
	    e.printStackTrace();
	    exit(1);
	}
    }
}
