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
 * Regression test for the LocalConsumer state machine.  Calls Consumer
 * methods before and after open(), compile(), enable(), go(), stop(),
 * and close() to verify that the calls succeed as expected or fail with
 * the expected Java exception.
 */
public class TestStateMachine {
    static Program program;

    static void
    exit(int status)
    {
	exit(status, null);
    }

    static void
    exit(int status, String msg)
    {
	if (msg != null) {
	    System.out.println(msg);
	}
	System.out.flush();
	System.err.flush();
	System.exit(status);
    }

    static void
    printState(Consumer consumer)
    {
	System.out.println("open: " + consumer.isOpen());
	System.out.println("enabled: " + consumer.isEnabled());
	System.out.println("closed: " + consumer.isClosed());
    }

    static void
    beforeOpen(Consumer consumer)
    {
	System.out.println("before open");
	printState(consumer);

	// compile
	try {
	    consumer.compile("syscall:::entry");
	    exit(1, "compile before open");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "compile before open");
	}

	// enable
	try {
	    consumer.enable();
	    exit(1, "enable before open");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "enable before open");
	}

	// getOption, setOption, unsetOption
	try {
	    consumer.getOption(Option.bufsize);
	    exit(1, "getOption before open");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "getOption before open");
	}
	try {
	    consumer.setOption(Option.bufsize, Option.mb(1));
	    exit(1, "setOption before open");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "setOption before open");
	}
	try {
	    consumer.unsetOption(Option.quiet);
	    exit(1, "unsetOption before open");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "unsetOption before open");
	}

	// createProcess, grabProcess
	try {
	    consumer.createProcess("date");
	    exit(1, "createProcess before open");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "createProcess before open");
	}
	try {
	    consumer.grabProcess(1);
	    exit(1, "grabProcess before open");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "grabProcess before open");
	}

	// listProbes
	try {
	    consumer.listProbes(ProbeDescription.EMPTY);
	    exit(1, "listProbes before open");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "listProbes before open");
	}

	// getAggregate
	try {
	    consumer.getAggregate();
	    exit(1, "getAggregate before open");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "getAggregate before open");
	}

	// getVersion
	try {
	    consumer.getVersion(); // allowed
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "getVersion before open");
	}
    }

    static void
    beforeCompile(Consumer consumer)
    {
	System.out.println("before compile");
	printState(consumer);

	// open
	try {
	    consumer.open();
	    exit(1, "open after open");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "open after open");
	}

	// enable
	try {
	    consumer.enable();
	    exit(1, "enable before compile");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "enable before compile");
	}
    }

    static void
    beforeEnable(Consumer consumer)
    {
	System.out.println("before enable");
	printState(consumer);

	// go
	try {
	    consumer.go();
	    exit(1, "go before enable");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "go before enable");
	}
    }

    static void
    beforeGo(Consumer consumer)
    {
	System.out.println("before go");
	printState(consumer);

	// getAggregate
	try {
	    consumer.getAggregate();
	    exit(1, "getAggregate before go");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "getAggregate before go");
	}

	// lookupKernelFunction, lookupUserFunction
	try {
	    consumer.lookupKernelFunction(1);
	    exit(1, "lookupKernelFunction before go");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "lookupKernelFunction before go");
	}
	try {
	    consumer.lookupUserFunction(1, 1);
	    exit(1, "lookupUserFunction before go");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "lookupUserFunction before go");
	}

	// stop
	try {
	    consumer.stop();
	    exit(1, "stop before go");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "stop before go");
	}
    }

    static void
    afterGo(Consumer consumer, Program program)
    {
	System.out.println("after go");
	printState(consumer);

	// go
	try {
	    consumer.go();
	    exit(1, "go after go");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "go after go");
	}

	// createProcess, grabProcess
	try {
	    consumer.createProcess("date");
	    exit(1, "createProcess after go");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "createProcess after go");
	}
	try {
	    consumer.grabProcess(1);
	    exit(1, "grabProcess after go");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "grabProcess after go");
	}

	// listProbes
	try {
	    consumer.listProbes(ProbeDescription.EMPTY);
	    exit(1, "listProbes after go");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "listProbes after go");
	}

	// compile
	try {
	    consumer.compile("syscall:::entry");
	    exit(1, "compile after go");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "compile after go");
	}

	// enable
	try {
	    consumer.enable();
	    exit(1, "enable after go");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "enable after go");
	}

	// getAggregate
	try {
	    consumer.getAggregate();
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "getAggregate after go");
	}

	// getProgramInfo
	try {
	    consumer.getProgramInfo(program);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "getProgramInfo after go");
	}

	// getOption, setOption, unsetOption
	try {
	    consumer.getOption(Option.quiet);
	    consumer.setOption(Option.quiet);
	    consumer.unsetOption(Option.quiet);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "get, set, unset option after go");
	}
    }

    static void
    afterStop(Consumer consumer, Program program)
    {
	System.out.println("after stop");
	printState(consumer);

	// stop
	try {
	    consumer.stop();
	    exit(1, "stop after stop");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "stop after stop");
	}

	// getAggregate
	try {
	    consumer.getAggregate();
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "getAggregate after stop");
	}

	// getProgramInfo
	try {
	    consumer.getProgramInfo(program);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "getProgramInfo after stop");
	}

	// getOption, setOption, unsetOption
	try {
	    consumer.getOption(Option.quiet);
	    consumer.setOption(Option.quiet);
	    consumer.unsetOption(Option.quiet);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "get, set, unset option after stop");
	}
    }

    static void
    afterClose(Consumer consumer, Program program)
    {
	System.out.println("after close");
	printState(consumer);

	// open
	try {
	    consumer.open();
	    exit(1, "open after close");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "open after close");
	}

	// compile
	try {
	    consumer.compile("syscall:::entry");
	    exit(1, "compile after close");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "compile after close");
	}

	// enable
	try {
	    consumer.enable();
	    exit(1, "enable after close");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "enable after close");
	}

	// getOption, setOption, unsetOption
	try {
	    consumer.getOption(Option.bufsize);
	    exit(1, "getOption after close");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "getOption after close");
	}
	try {
	    consumer.setOption(Option.bufsize, Option.mb(1));
	    exit(1, "setOption after close");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "setOption after close");
	}
	try {
	    consumer.unsetOption(Option.quiet);
	    exit(1, "unsetOption after close");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "unsetOption after close");
	}

	// createProcess, grabProcess
	try {
	    consumer.createProcess("date");
	    exit(1, "createProcess after close");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "createProcess after close");
	}
	try {
	    consumer.grabProcess(1);
	    exit(1, "grabProcess after close");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "grabProcess after close");
	}

	// listProbes
	try {
	    consumer.listProbes(ProbeDescription.EMPTY);
	    exit(1, "listProbes after close");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "listProbes after close");
	}

	// getAggregate
	try {
	    consumer.getAggregate();
	    exit(1, "getAggregate after close");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "getAggregate after close");
	}

	// getVersion
	try {
	    consumer.getVersion(); // allowed
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "getVersion after close");
	}

	// go
	try {
	    consumer.go();
	    exit(1, "go after close");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "go after close");
	}

	// lookupKernelFunction, lookupUserFunction
	try {
	    consumer.lookupKernelFunction(1);
	    exit(1, "lookupKernelFunction after close");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "lookupKernelFunction after close");
	}
	try {
	    consumer.lookupUserFunction(1, 1);
	    exit(1, "lookupUserFunction after close");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "lookupUserFunction after close");
	}

	// stop
	try {
	    consumer.stop();
	    exit(1, "stop after close");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "stop after close");
	}

	// getProgramInfo
	try {
	    consumer.getProgramInfo(program);
	    exit(1, "getProgramInfo after close");
	} catch (IllegalStateException e) {
	    System.out.println(e);
	} catch (Exception e) {
	    e.printStackTrace();
	    exit(1, "getProgramInfo after close");
	}
    }

    public static void
    main(String[] args)
    {
	final Consumer consumer = new LocalConsumer();
	consumer.addConsumerListener(new ConsumerAdapter() {
	    public void consumerStarted(ConsumerEvent e) {
		System.out.println("consumerStarted, running: " +
			consumer.isRunning());
		afterGo(consumer, program);
	    }
	    public void consumerStopped(ConsumerEvent e) {
		System.out.println("consumerStopped, running: " +
			consumer.isRunning());
	    }
	});

	try {
	    beforeOpen(consumer);
	    consumer.open();
	    beforeCompile(consumer);
	    program = consumer.compile(
		    "syscall:::entry { @[execname] = count(); } " +
		    "tick-101ms { printa(@); }");
	    beforeEnable(consumer);
	    consumer.enable();
	    beforeGo(consumer);
	    System.out.println("before go, running: " + consumer.isRunning());
	    consumer.go();
	    // Avoid race, call afterGo() in ConsumerListener
	    try {
		Thread.currentThread().sleep(300);
	    } catch (InterruptedException e) {
		e.printStackTrace();
		exit(1);
	    }
	    consumer.stop();
	    System.out.println("after stop, running: " + consumer.isRunning());
	    afterStop(consumer, program);
	    consumer.close();
	    afterClose(consumer, program);
	} catch (DTraceException e) {
	    e.printStackTrace();
	    exit(1);
	}
    }
}
