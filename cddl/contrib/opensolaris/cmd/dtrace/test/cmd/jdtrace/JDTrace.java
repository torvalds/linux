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
import java.io.*;
import java.util.*;
import java.util.logging.*;

/**
 * Emulates {@code dtrace(1M)} using the Java DTrace API.
 */
public class JDTrace {
    static Logger logger = Logger.getLogger(JDTrace.class.getName());

    static Consumer dtrace;

    static {
	Handler handler = new ConsoleHandler();
	handler.setLevel(Level.ALL);
	logger.addHandler(handler);
    }

    static final String CLASSNAME = "JDTrace";
    static final String OPTSTR =
	    "3:6:b:c:CD:ef:Fi:I:lL:m:n:o:p:P:qs:U:vVwx:X:Z";
    static boolean heading = false;
    static boolean quiet = false;
    static boolean flow = false;
    static int stackindent = 14;
    static int exitStatus = 0;
    static boolean started;
    static boolean stopped;
    static PrintStream out = System.out;
    static final String ATS = "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@";
    static final String SPACES = "                                        ";
    static final int QUANTIZE_ZERO_BUCKET = 63;

    enum Mode {
	EXEC,
	INFO,
	LIST,
	VERSION
    }

    enum ProgramType {
	STRING,
	FILE
    }

    static class CompileRequest {
	String s;
	ProgramType type;
	ProbeDescription.Spec probespec;
    }

    // Modify program string by expanding an incomplete probe
    // description according to the requested probespec.
    static void
    applyProbespec(CompileRequest req)
    {
	ProbeDescription.Spec spec = ((req.probespec == null)
		? ProbeDescription.Spec.NAME
		: req.probespec);

	int colons = 0;
	switch (req.probespec) {
	    case PROVIDER:
		colons = 3;
		break;
	    case MODULE:
		colons = 2;
		break;
	    case FUNCTION:
		colons = 1;
		break;
	}

	StringBuffer buf = new StringBuffer();
	if (colons > 0) {
	    char ch;
	    int len = req.s.length();

	    int i = 0;
	    // Find first whitespace character not including leading
	    // whitespace (end of first token).  Ignore whitespace
	    // inside a block if the block is concatenated with the
	    // probe description.
	    for (; (i < len) && Character.isWhitespace(req.s.charAt(i)); ++i);
	    int npos = i;
	    boolean inBlock = false;
	    for (; (npos < len) &&
		    (!Character.isWhitespace(ch = req.s.charAt(npos)) ||
		    inBlock); ++npos) {
		if (ch == '{') {
		    inBlock = true;
		} else if (ch == '}') {
		    inBlock = false;
		}
	    }

	    // libdtrace lets you concatenate multiple probe
	    // descriptions separated by code blocks in curly braces,
	    // for example genunix::'{printf("FOUND");}'::entry, as long
	    // as the concatenated probe descriptions begin with ':' and
	    // not a specific field such as 'syscall'.  So to expand the
	    // possibly multiple probe descriptions, we need to insert
	    // colons before each open curly brace, and again at the end
	    // only if there is at least one non-whitespace (probe
	    // specifying) character after the last closing curly brace.

	    int prev_i = 0;
	    while (i < npos) {
		for (; (i < npos) && (req.s.charAt(i) != '{'); ++i);
		buf.append(req.s.substring(prev_i, i));
		if ((i < npos) || ((i > 0) && (req.s.charAt(i - 1) != '}'))) {
		    for (int c = 0; c < colons; ++c) {
			buf.append(':');
		    }
		}
		if (i < npos) {
		    buf.append(req.s.charAt(i++));
		}
		prev_i = i;
	    }

	    // append remainder of program text
	    buf.append(req.s.substring(i));

	    req.s = buf.toString();
	}
    }

    static void
    printValue(Object value, int bytes, String stringFormat)
    {
	if (value instanceof Integer) {
	    if (bytes == 1) {
		out.printf(" %3d", (Integer)value);
	    } else if (bytes == 2) {
		out.printf(" %5d", (Integer)value);
	    } else {
		out.printf(" %8d", (Integer)value);
	    }
	} else if (value instanceof Long) {
	    out.printf(" %16d", (Long)value);
	} else {
	    out.printf(stringFormat, value.toString());
	}
    }

    static void
    consumeProbeData(ProbeData data)
    {
	if (logger.isLoggable(Level.FINER)) {
	    logger.finer(data.toString());
	}

	if (!heading) {
	    if (flow) {
		out.printf("%3s %-41s\n", "CPU", "FUNCTION");
	    } else {
		if (!quiet) {
		    out.printf("%3s %6s %32s\n",
			    "CPU", "ID", "FUNCTION:NAME");
		}
	    }
	    heading = true;
	}
	ProbeDescription probe = data.getEnabledProbeDescription();
	if (flow) {
	    Flow flow = data.getFlow();
	    int indent = (flow.getDepth() * 2);
	    StringBuffer buf = new StringBuffer();
	    // indent
	    buf.append(' ');
	    for (int i = 0; i < indent; ++i) {
		buf.append(' ');
	    }
	    // prefix
	    switch (flow.getKind()) {
		case ENTRY:
		    if (indent == 0) {
			buf.append("=> ");
		    } else {
			buf.append("-> ");
		    }
		    break;
		case RETURN:
		    if (indent == 0) {
			buf.append("<= ");
		    } else {
			buf.append("<- ");
		    }
		    break;
	    }

	    switch (flow.getKind()) {
		case NONE:
		    buf.append(probe.getFunction());
		    buf.append(':');
		    buf.append(probe.getName());
		    break;
		default:
		    buf.append(probe.getFunction());
	    }

	    out.printf("%3s %-41s ", data.getCPU(),
		    buf.toString());
	} else {
	    if (!quiet) {
		StringBuffer buf = new StringBuffer();
		buf.append(probe.getFunction());
		buf.append(':');
		buf.append(probe.getName());
		out.printf("%3s %6s %32s ",
			data.getCPU(), probe.getID(),
			buf.toString());
	    }
	}
	Record record = null;
	Object value;
	List <Record> records = data.getRecords();
	Iterator <Record> itr = records.iterator();
	while (itr.hasNext()) {
	    record = itr.next();

	    if (record instanceof ExitRecord) {
		exitStatus = ((ExitRecord)record).getStatus();
	    } else if (record instanceof ScalarRecord) {
		ScalarRecord scalar = (ScalarRecord)record;
		value = scalar.getValue();
		if (value instanceof byte[]) {
		    out.print(record.toString());
		} else {
		    if (quiet) {
			out.print(value);
		    } else {
			printValue(value, scalar.getNumberOfBytes(),
				"  %-33s");
		    }
		}
	    } else if (record instanceof PrintfRecord) {
		out.print(record);
	    } else if (record instanceof PrintaRecord) {
		PrintaRecord printa = (PrintaRecord)record;
		List <Tuple> tuples = printa.getTuples();
		if (tuples.isEmpty()) {
		    out.print(printa.getOutput());
		} else {
		    for (Tuple t : tuples) {
			out.print(printa.getFormattedString(t));
		    }
		}

		if (logger.isLoggable(Level.FINE)) {
		    logger.fine(printa.toString());
		}
	    } else if (record instanceof StackValueRecord) {
		printStack((StackValueRecord)record);
	    }
	}
	if (!quiet) {
	    out.println();
	}
    }

    static void
    printDistribution(Distribution d)
    {
	out.printf("\n%16s %41s %-9s\n", "value",
		"------------- Distribution -------------",
		"count");
	long v; // bucket frequency (value)
	long b; // lower bound of bucket range
	double total = 0;
	boolean positives = false;
	boolean negatives = false;

	Distribution.Bucket bucket;
	int b1 = 0; // first displayed bucket
	int b2 = d.size() - 1; // last displayed bucket
	for (; (b1 <= b2) && (d.get(b1).getFrequency() == 0); ++b1);
	// If possible, get one bucket before the first non-zero
	// bucket and one bucket after the last.
	if (b1 > b2) {
	    // There isn't any data.  This is possible if (and only if)
	    // negative increment values have been used.  In this case,
	    // print the buckets around the base.
	    if (d instanceof LinearDistribution) {
		b1 = 0;
		b2 = 2;
	    } else {
		b1 = QUANTIZE_ZERO_BUCKET - 1;
		b2 = QUANTIZE_ZERO_BUCKET + 1;
	    }
	} else {
	    if (b1 > 0) --b1;
	    for (; (b2 > 0) && (d.get(b2).getFrequency() == 0); --b2);
	    if (b2 < (d.size() - 1)) ++b2;
	}
	for (int i = b1; i <= b2; ++i) {
	    v = d.get(i).getFrequency();
	    if (v > 0) {
		positives = true;
	    }
	    if (v < 0) {
		negatives = true;
	    }
	    total += Math.abs((double)v);
	}
	for (int i = b1; i <= b2; ++i) {
	    bucket = d.get(i);
	    v = bucket.getFrequency();
	    b = bucket.getMin();

	    if (d instanceof LinearDistribution) {
		if (b == Long.MIN_VALUE) {
		    String lt = "< " + ((LinearDistribution)d).getBase();
		    out.printf("%16s ", lt);
		} else if (bucket.getMax() == Long.MAX_VALUE) {
		    String ge = ">= " + b;
		    out.printf("%16s ", ge);
		} else {
		    out.printf("%16d ", b);
		}
	    } else {
		out.printf("%16d ", b);
	    }

	    printDistributionLine(v, total, positives, negatives);
	}
    }

    static void
    printDistributionLine(long val, double total, boolean positives,
	    boolean negatives)
    {
	double f;
	int depth, len = 40;

	assert (ATS.length() == len && SPACES.length() == len);
	assert (!(total == 0 && (positives || negatives)));
	assert (!(val < 0 && !negatives));
	assert (!(val > 0 && !positives));
	assert (!(val != 0 && total == 0));

	if (!negatives) {
	    if (positives) {
		f = (Math.abs((double)val) * (double)len) / total;
		    depth = (int)(f + 0.5);
	    } else {
		depth = 0;
	    }

	    out.printf("|%s%s %-9d\n", ATS.substring(len - depth),
		    SPACES.substring(depth), val);
	    return;
	}

	if (!positives) {
	    f = (Math.abs((double)val) * (double)len) / total;
	    depth = (int)(f + 0.5);

	    out.printf("%s%s| %-9d\n", SPACES.substring(depth),
		    ATS.substring(len - depth), val);
	    return;
	}

	/*
	 * If we're here, we have both positive and negative bucket values.
	 * To express this graphically, we're going to generate both positive
	 * and negative bars separated by a centerline.  These bars are half
	 * the size of normal quantize()/lquantize() bars, so we divide the
	 * length in half before calculating the bar length.
	 */
	len /= 2;
	String ats = ATS.substring(len);
	String spaces = SPACES.substring(len);

	f = (Math.abs((double)val) * (double)len) / total;
	depth = (int)(f + 0.5);

	if (val <= 0) {
	    out.printf("%s%s|%s %-9d\n", spaces.substring(depth),
		    ats.substring(len - depth), repeat(" ", len), val);
	    return;
	} else {
	    out.printf("%20s|%s%s %-9d\n", "", ats.substring(len - depth),
		    spaces.substring(depth), val);
	}
    }

    public static String
    repeat(String s, int n)
    {
        StringBuffer buf = new StringBuffer();
        for (int i = 0; i < n; ++i) {
            buf.append(s);
        }
        return buf.toString();
    }

    static void
    printStack(StackValueRecord rec)
    {
	StackFrame[] frames = rec.getStackFrames();
	int i;
	out.println();
	String s;
	for (StackFrame f : frames) {
	    for (i = 0; i < stackindent; ++i) {
		out.print(' ');
	    }
	    s = f.getFrame();
	    if (s.indexOf('[') == 0) {
		out.print("  ");
	    }
	    out.println(s);
	}
    }

    static void
    printAggregate(Aggregate aggregate)
    {
	printAggregationRecords(aggregate.getOrderedRecords());
    }

    static void
    printAggregationRecords(List <AggregationRecord> list)
    {
	Tuple tuple;
	AggregationValue value;
	ValueRecord tupleRecord;
	int i;
	int len;
	for (AggregationRecord r : list) {
	    tuple = r.getTuple();
	    value = r.getValue();
	    len = tuple.size();
	    for (i = 0; i < len; ++i) {
		tupleRecord = tuple.get(i);
		if (tupleRecord instanceof StackValueRecord) {
		    printStack((StackValueRecord)tupleRecord);
		} else if (tupleRecord instanceof SymbolValueRecord) {
		    printValue(tupleRecord.toString(), -1, "  %-50s");
		} else {
		    printValue(tupleRecord.getValue(),
			    ((ScalarRecord)tupleRecord).getNumberOfBytes(),
			    "  %-50s");
		}
	    }
	    if (value instanceof Distribution) {
		Distribution d = (Distribution)value;
		printDistribution(d);
	    } else {
		Number v = value.getValue();
		printValue(v, -1, "  %-50s");
	    }
	    out.println();
	}
    }

    static void
    exit(int status)
    {
	out.flush();
	System.err.flush();
	if (status == 0) {
	    status = exitStatus;
	}
	System.exit(status);
    }

    static void
    usage()
    {
	String predact = "[[ predicate ] action ]";
	System.err.printf("Usage: java %s [-32|-64] [-CeFlqvVwZ] " +
	    "[-b bufsz] [-c cmd] [-D name[=def]]\n\t[-I path] [-L path] " +
	    "[-o output] [-p pid] [-s script] [-U name]\n\t" +
	    "[-x opt[=val]] [-X a|c|s|t]\n\n" +
	    "\t[-P provider %s]\n" +
	    "\t[-m [ provider: ] module %s]\n" +
	    "\t[-f [[ provider: ] module: ] func %s]\n" +
	    "\t[-n [[[ provider: ] module: ] func: ] name %s]\n" +
	    "\t[-i probe-id %s] [ args ... ]\n\n", CLASSNAME,
	    predact, predact, predact, predact, predact);
	System.err.printf("\tpredicate -> '/' D-expression '/'\n");
	System.err.printf("\t   action -> '{' D-statements '}'\n");
	System.err.printf("\n" +
	    "\t-32 generate 32-bit D programs\n" +
	    "\t-64 generate 64-bit D programs\n\n" +
	    "\t-b  set trace buffer size\n" +
	    "\t-c  run specified command and exit upon its completion\n" +
	    "\t-C  run cpp(1) preprocessor on script files\n" +
	    "\t-D  define symbol when invoking preprocessor\n" +
	    "\t-e  exit after compiling request but prior to enabling " +
		    "probes\n" +
	    "\t-f  enable or list probes matching the specified " +
		    "function name\n" +
	    "\t-F  coalesce trace output by function\n" +
	    "\t-i  enable or list probes matching the specified probe id\n" +
	    "\t-I  add include directory to preprocessor search path\n" +
	    "\t-l  list probes matching specified criteria\n" +
	    "\t-L  add library directory to library search path\n" +
	    "\t-m  enable or list probes matching the specified " +
		    "module name\n" +
	    "\t-n  enable or list probes matching the specified probe name\n" +
	    "\t-o  set output file\n" +
	    "\t-p  grab specified process-ID and cache its symbol tables\n" +
	    "\t-P  enable or list probes matching the specified " +
		    "provider name\n" +
	    "\t-q  set quiet mode (only output explicitly traced data)\n" +
	    "\t-s  enable or list probes according to the specified " +
		    "D script\n" +
	    "\t-U  undefine symbol when invoking preprocessor\n" +
	    "\t-v  set verbose mode (report stability attributes, " +
		    "arguments)\n" +
	    "\t-V  report DTrace API version\n" +
	    "\t-w  permit destructive actions\n" +
	    "\t-x  enable or modify compiler and tracing options\n" +
	    "\t-X  specify ISO C conformance settings for preprocessor\n" +
	    "\t-Z  permit probe descriptions that match zero probes\n" +
	    "\n" +
	    "\tTo log PrintaRecord, set this environment variable:\n" +
	    "\t\tJDTRACE_LOGGING_LEVEL=FINE\n" +
	    "\tTo log ProbeData, set JDTRACE_LOGGING_LEVEL=FINER\n");
	exit(2);
    }

    static void
    printProgramStability(String programType, String programDescription,
	    ProgramInfo info)
    {
	out.println();
	out.printf("Stability data for %s %s:\n\n",
		programType, programDescription);
	InterfaceAttributes a;
	out.println("\tMinimum probe description " +
		"attributes");
	a = info.getMinimumProbeAttributes();
	out.printf("\t\tIdentifier Names: %s\n",
		a.getNameStability());
	out.printf("\t\tData Semantics:   %s\n",
		a.getDataStability());
	out.printf("\t\tDependency Class: %s\n",
		a.getDependencyClass());
	out.println("\tMinimum probe statement attributes");
	a = info.getMinimumStatementAttributes();
	out.printf("\t\tIdentifier Names: %s\n",
		a.getNameStability());
	out.printf("\t\tData Semantics:   %s\n",
		a.getDataStability());
	out.printf("\t\tDependency Class: %s\n",
		a.getDependencyClass());
    }

    static void
    printProbeDescription(ProbeDescription p)
    {
	out.printf("%5d %10s %17s %33s %s\n", p.getID(),
	    p.getProvider(), p.getModule(),
	    p.getFunction(), p.getName());
    }

    static void
    printProbeInfo(ProbeInfo p)
    {
	InterfaceAttributes a;
	out.println("\n\tProbe Description Attributes");

	a = p.getProbeAttributes();
	out.printf("\t\tIdentifier Names: %s\n",
	    a.getNameStability());
	out.printf("\t\tData Semantics:   %s\n",
	    a.getDataStability());
	out.printf("\t\tDependency Class: %s\n",
	    a.getDependencyClass());

	out.println("\n\tArgument Attributes");

	a = p.getArgumentAttributes();
	out.printf("\t\tIdentifier Names: %s\n",
	    a.getNameStability());
	out.printf("\t\tData Semantics:   %s\n",
	    a.getDataStability());
	out.printf("\t\tDependency Class: %s\n",
	    a.getDependencyClass());

	// Argument types unsupported for now.

	out.println();
    }

    public static void
    main(String[] args)
    {
	String loggingLevel = System.getenv().get("JDTRACE_LOGGING_LEVEL");
	try {
	    logger.setLevel(Level.parse(loggingLevel));
	} catch (Exception e) {
	    logger.setLevel(Level.OFF);
	}

	if (args.length == 0) {
	    usage();
	}

	List <CompileRequest> compileRequests = new LinkedList
		<CompileRequest> ();
	List <Program> programList = new LinkedList <Program> ();
	boolean verbose = false;
	Mode mode = Mode.EXEC;

	final ExceptionHandler exceptionHandler = new ExceptionHandler() {
	    public void handleException(Throwable e) {
		if (e instanceof DTraceException) {
		    DTraceException de = (DTraceException)e;
		    System.err.printf("dtrace: %s\n", de.getMessage());
		} else if (e instanceof ConsumerException) {
		    ConsumerException ce = (ConsumerException)e;
		    Object msg = ce.getNotificationObject();
		    if ((msg instanceof org.opensolaris.os.dtrace.Error) ||
			(msg instanceof Drop)) {
			System.err.printf("dtrace: %s\n", ce.getMessage());
		    } else {
			ce.printStackTrace();
		    }
		} else {
		    e.printStackTrace();
		}
		exit(1);
	    }
	};

	Getopt g = new Getopt(CLASSNAME, args, OPTSTR);
	int c = 0;

	List <Consumer.OpenFlag> openFlags =
		new ArrayList <Consumer.OpenFlag> ();

	while ((c = g.getopt()) != -1) {
	    switch (c) {
		case '3': {
		    String s = g.getOptarg();
		    if (!s.equals("2")) {
			System.err.println("dtrace: illegal option -- 3" + s);
			usage();
		    }
		    openFlags.add(Consumer.OpenFlag.ILP32);
		    break;
		}
		case '6': {
		    String s = g.getOptarg();
		    if (!s.equals("4")) {
			System.err.println("dtrace: illegal option -- 6" + s);
			usage();
		    }
		    openFlags.add(Consumer.OpenFlag.LP64);
		    break;
		}
	    }
	}

	Consumer.OpenFlag[] oflags = new Consumer.OpenFlag[openFlags.size()];
	oflags = openFlags.toArray(oflags);

	dtrace = new LocalConsumer() {
	    protected Thread createThread() {
		Thread t = super.createThread();
		t.setDaemon(false);
		t.setPriority(Thread.MIN_PRIORITY);
		return t;
	    }
	};

	g = new Getopt(CLASSNAME, args, OPTSTR);
	c = 0;

	try {
	    dtrace.open(oflags);

	    // Set default options that may be overriden by options or #pragma
	    dtrace.setOption(Option.bufsize, Option.mb(4));
	    dtrace.setOption(Option.aggsize, Option.mb(4));

	    CompileRequest r;
	    while ((c = g.getopt()) != -1) {
		switch (c) {
		    case 'b':
			dtrace.setOption(Option.bufsize, g.getOptarg());
			break;
		    case 'c':
			dtrace.createProcess(g.getOptarg());
			break;
		    case 'C':
			dtrace.setOption(Option.cpp);
			break;
		    case 'D':
			dtrace.setOption(Option.define, g.getOptarg());
			break;
		    case 'e':
			mode = Mode.INFO;
			break;
		    case 'f':
			r = new CompileRequest();
			r.s = g.getOptarg();
			r.type = ProgramType.STRING;
			r.probespec = ProbeDescription.Spec.FUNCTION;
			compileRequests.add(r);
			break;
		    case 'F':
			dtrace.setOption(Option.flowindent);
			break;
		    case 'i':
			r = new CompileRequest();
			r.s = g.getOptarg();
			r.type = ProgramType.STRING;
			r.probespec = ProbeDescription.Spec.NAME;
			compileRequests.add(r);
			break;
		    case 'I':
			dtrace.setOption(Option.incdir, g.getOptarg());
			break;
		    case 'l':
			mode = Mode.LIST;
			dtrace.setOption(Option.zdefs); // -l implies -Z
			break;
		    case 'L':
			dtrace.setOption(Option.libdir, g.getOptarg());
			break;
		    case 'm':
			r = new CompileRequest();
			r.s = g.getOptarg();
			r.type = ProgramType.STRING;
			r.probespec = ProbeDescription.Spec.MODULE;
			compileRequests.add(r);
			break;
		    case 'n':
			r = new CompileRequest();
			r.s = g.getOptarg();
			r.type = ProgramType.STRING;
			r.probespec = ProbeDescription.Spec.NAME;
			compileRequests.add(r);
			break;
		    case 'o':
			String outFileName = g.getOptarg();
			File outFile = new File(outFileName);
			try {
			    FileOutputStream fos = new FileOutputStream(
				    outFile, true);
			    out = new PrintStream(fos);
			} catch (FileNotFoundException e) {
			    System.err.println("failed to open " +
				outFileName + " in write mode");
			    exit(1);
			} catch (SecurityException e) {
			    System.err.println("failed to open " +
				outFileName);
			    exit(1);
			}
			break;
		    case 'p':
			String pidstr = g.getOptarg();
			int pid = -1;
			try {
			    pid = Integer.parseInt(pidstr);
			} catch (NumberFormatException e) {
			    System.err.println("invalid pid: " + pidstr);
			    exit(1);
			}
			dtrace.grabProcess(pid);
			break;
		    case 'P':
			r = new CompileRequest();
			r.s = g.getOptarg();
			r.type = ProgramType.STRING;
			r.probespec = ProbeDescription.Spec.PROVIDER;
			compileRequests.add(r);
			break;
		    case 'q':
			dtrace.setOption(Option.quiet);
			break;
		    case 's':
			r = new CompileRequest();
			r.s = g.getOptarg();
			r.type = ProgramType.FILE;
			compileRequests.add(r);
			break;
		    case 'U':
			dtrace.setOption(Option.undef, g.getOptarg());
			break;
		    case 'v':
			verbose = true;
			break;
		    case 'V':
			mode = Mode.VERSION;
			break;
		    case 'w':
			dtrace.setOption(Option.destructive);
			break;
		    case 'x':
			String[] xarg = g.getOptarg().split("=", 2);
			if (xarg.length > 1) {
			    dtrace.setOption(xarg[0], xarg[1]);
			} else if (xarg.length == 1) {
			    dtrace.setOption(xarg[0]);
			}
			break;
		    case 'X':
			dtrace.setOption(Option.stdc, g.getOptarg());
			break;
		    case 'Z':
			dtrace.setOption(Option.zdefs);
			break;
		    case '?':
			usage(); // getopt() already printed an error
			break;
		    default:
			System.err.print("getopt() returned " + c + "\n");
			c = 0;
		 }
	    }
	    c = 0;
	    List <String> argList = new LinkedList <String> ();
	    for (int i = g.getOptind(); i < args.length; ++i) {
		argList.add(args[i]);
	    }

	    if (mode == Mode.VERSION) {
		out.printf("dtrace: %s\n", dtrace.getVersion());
		dtrace.close();
		exit(0);
	    }

	    String[] compileArgs = new String[argList.size()];
	    compileArgs = argList.toArray(compileArgs);

	    Program program;
	    for (CompileRequest req : compileRequests) {
		switch (req.type) {
		    case STRING:
			applyProbespec(req);
			program = dtrace.compile(req.s, compileArgs);
			break;
		    case FILE:
			File file = new File(req.s);
			program = dtrace.compile(file, compileArgs);
			break;
		    default:
			throw new IllegalArgumentException(
				"Unexpected program type: " + req.type);
		}

		programList.add(program);
	    }

	    // Get options set by #pragmas in compiled program
	    long optval;
	    quiet = (dtrace.getOption(Option.quiet) != Option.UNSET);
	    flow = (dtrace.getOption(Option.flowindent) != Option.UNSET);
	    optval = dtrace.getOption("stackindent");
	    if (optval != Option.UNSET) {
		stackindent = (int)optval;
	    }

	    if (mode == Mode.LIST) {
		out.printf("%5s %10s %17s %33s %s\n",
		    "ID", "PROVIDER", "MODULE", "FUNCTION", "NAME");

		if (verbose) {
		    List <List <Probe>> lists =
			    new LinkedList <List <Probe>> ();
		    for (Program p : programList) {
			lists.add(dtrace.listProgramProbeDetail(p));
		    }
		    ProbeDescription p;
		    ProbeInfo pinfo;
		    for (List <Probe> list : lists) {
			for (Probe probe : list) {
			    p = probe.getDescription();
			    pinfo = probe.getInfo();
			    printProbeDescription(p);
			    printProbeInfo(pinfo);
			}
		    }
		} else {
		    List <List <ProbeDescription>> lists =
			    new LinkedList <List <ProbeDescription>> ();
		    for (Program p : programList) {
			lists.add(dtrace.listProgramProbes(p));
		    }
		    for (List <ProbeDescription> list : lists) {
			for (ProbeDescription p : list) {
			    printProbeDescription(p);
			}
		    }
		}
		exit(0);
	    }

	    String programType;
	    String programDescription;
	    ProgramInfo info;
	    for (Program p : programList) {
		if (p instanceof Program.File) {
		    Program.File pf = (Program.File)p;
		    programType = "script";
		    programDescription = pf.getFile().getPath();
		} else {
		    programType = "description";
		    programDescription =
			p.getContents().split("[/{;]", 2)[0];
		}

		if (mode == Mode.EXEC) {
		    dtrace.enable(p);
		} else {
		    dtrace.getProgramInfo(p);
		}
		info = p.getInfo();
		if ((mode == Mode.EXEC) && !quiet) {
		    System.err.printf("dtrace: %s '%s' matched %d probe%s\n",
			    programType, programDescription,
			    info.getMatchingProbeCount(),
			    info.getMatchingProbeCount() == 1 ? "" : "s");
		}
		if (verbose) {
		    printProgramStability(programType,
			    programDescription, info);
		}
	    }
	    if (mode != Mode.EXEC) {
		exit(0);
	    }
	    dtrace.addConsumerListener(new ConsumerAdapter() {
		public void consumerStarted(ConsumerEvent e) {
		    started = true;
		}
		public void consumerStopped(ConsumerEvent e) {
		    stopped = true;
		    out.println();
		    try {
			Aggregate aggregate = dtrace.getAggregate();
			if (aggregate != null) {
			    printAggregate(aggregate);
			}
			dtrace.close();
		    } catch (Throwable x) {
			exceptionHandler.handleException(x);
		    }
		    exit(0);
		}
		public void dataDropped(DropEvent e) {
		    System.err.printf("dtrace: %s",
			    e.getDrop().getDefaultMessage());
		}
		public void errorEncountered(ErrorEvent e)
			throws ConsumerException {
		    org.opensolaris.os.dtrace.Error error = e.getError();
		    if (logger.isLoggable(Level.FINE)) {
			logger.fine(error.toString());
		    }
		    System.err.printf("dtrace: %s",
			    error.getDefaultMessage());
		}
		public void dataReceived(DataEvent e)
			throws ConsumerException {
		    consumeProbeData(e.getProbeData());
		}
		public void processStateChanged(ProcessEvent e)
			throws ConsumerException {
		    if (logger.isLoggable(Level.FINE)) {
			logger.fine(e.getProcessState().toString());
		    }
		}
	    });
	    // Print unprinted aggregations after Ctrl-C
	    Runtime.getRuntime().addShutdownHook(new Thread() {
		public void run() {
		    if (stopped || !started) {
			return;
		    }

		    try {
			Aggregate aggregate = dtrace.getAggregate();
			if (aggregate != null) {
			    out.println();
			    out.println();
			    printAggregate(aggregate);
			}
		    } catch (Throwable x) {
			exceptionHandler.handleException(x);
		    }
		}
	    });
	    dtrace.go(exceptionHandler);
	} catch (DTraceException e) {
	    if (c > 0) {
		// set option error
		if (g.getOptarg() == null) {
		    System.err.printf("dtrace: failed to set -%c: %s\n",
			c, e.getMessage());
		} else {
		    System.err.printf("dtrace: failed to set -%c %s: %s\n",
			c, g.getOptarg(), e.getMessage());
		}
	    } else {
		// any other error
		System.err.printf("dtrace: %s\n", e.getMessage());
	    }
	    exit(1);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1);
	}
    }
}
