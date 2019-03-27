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

/* Copyright (c) 1988 AT&T */
/* All Rights Reserved */

import java.io.StringWriter;
import java.io.PrintWriter;

/**
 * A Java port of Solaris {@code lib/libc/port/gen/getopt.c}, which is a
 * port of System V UNIX getopt.  See <b>getopt(3C)</b> and SUS/XPG
 * getopt() for function definition and requirements. Unlike that
 * definition, this implementation moves non-options to the end of the
 * argv array rather than quitting at the first non-option.
 */
public class Getopt {
    static final int EOF = -1;

    private String progname;
    private String[] args;
    private int argc;
    private String optstring;
    private int optind = 0; // args index
    private int optopt = 0;
    private String optarg = null;
    private boolean opterr = true;

    /*
     * _sp is required to keep state between successive calls to
     * getopt() while extracting aggregated short-options (ie: -abcd).
     */
    private int _sp = 1;

    /**
     * Creates a {Code Getopt} instance to parse the given command-line
     * arguments. Modifies the given args array by swapping the
     * positions of non-options and options so that non-options appear
     * at the end of the array.
     */
    public Getopt(String programName, String[] args,
	    String optionString)
    {
	progname = programName;
	// No defensive copy; Getopt is expected to modify the given
	// args array
	this.args = args;
	argc = this.args.length;
	optstring = optionString;
	validate();
    }

    private void
    validate()
    {
	if (progname == null) {
	    throw new NullPointerException("program name is null");
	}
	int i = 0;
	for (String s : args) {
	    if (s == null) {
		throw new NullPointerException("null arg at index " + i);
	    }
	    ++i;
	}
	if (optstring == null) {
	    throw new NullPointerException("option string is null");
	}
    }

    private static class StringRef {
	private String s;

	public String
	get()
	{
	    return s;
	}

	public StringRef
	set(String value)
	{
	    s = value;
	    return this;
	}
    }

    /*
     * Generalized error processing method. If the optstr parameter is
     * null, the character c is converted to a string and displayed
     * instead.
     */
    void
    err(String format, char c, String optstr)
    {
	if (opterr && optstring.charAt(0) != ':') {
	    StringWriter w = new StringWriter();
	    PrintWriter p = new PrintWriter(w);
	    p.printf(format, progname, (optstr == null ?
		    Character.toString(c) : optstr.substring(2)));
	    System.err.println(w.toString());
	}
    }

    /*
     * Determine if the specified character (c) is present in the string
     * (optstring) as a regular, single character option. If the option
     * is found, return an index into optstring where the short-option
     * character is found, otherwise return -1. The characters ':' and
     * '(' are not allowed.
     */
    static int
    parseshort(String optstring, char c)
    {
	if (c == ':' || c == '(') {
	    return -1;
	}

	int ch;
	int len = optstring.length();
	for (int i = 0; i < len; ++i) {
	    ch = optstring.charAt(i);
	    if (ch == c) {
		return i;
	    }

	    while (i < len && ch == '(') {
		for (++i; i < len && (ch = optstring.charAt(i)) != ')'; ++i);
	    }
	}

	return -1;
    }

    /**
     * Determine if the specified string (opt) is present in the string
     * (optstring) as a long-option contained within parenthesis. If the
     * long-option specifies option-argument, return a reference to it
     * in longoptarg.  Otherwise set the longoptarg reference to null.
     * If the option is found, return an index into optstring at the
     * position of the short-option character associated with the
     * long-option; otherwise return -1.
     *
     * @param optstring	the entire optstring passed to the {@code
     * Getopt} constructor
     * @param opt the long option read from the command line
     * @param longoptarg the value of the option is returned in this
     * parameter, if an option exists. Possible return values in
     * longoptarg are:
     * <ul>
     * <li><b>NULL:</b> No argument was found</li>
     * <li><b>empty string (""):</b> Argument was explicitly left empty
     * by the user (e.g., --option= )</li>
     * <li><b>valid string:</b> Argument found on the command line</li>
     * </ul>
     * @return index to equivalent short-option in optstring, or -1 if
     * option not found in optstring.
     */
    static int
    parselong(String optstring, String opt, StringRef longoptarg)
    {
	int cp; // index into optstring, beginning of one option spec
	int ip; // index into optstring, traverses every char
	char ic; // optstring char
	int il; // optstring length
	int op;	// index into opt
	char oc; // opt char
	int ol; // opt length
	boolean	match; // true if opt is matching part of optstring

	longoptarg.set(null);
	cp = ip = 0;
	il = optstring.length();
	ol = opt.length();
	do {
	    ic = optstring.charAt(ip);
	    if (ic != '(' && ++ip == il)
		break;
	    ic = optstring.charAt(ip);
	    if (ic == ':' && ++ip == il)
		break;
	    ic = optstring.charAt(ip);
	    while (ic == '(') {
		if (++ip == il)
		    break;
		op = 0;
		match = true;
		while (ip < il && (ic = optstring.charAt(ip)) != ')' &&
			op < ol) {
		    oc = opt.charAt(op++);
		    match = (ic == oc && match);
		    ++ip;
		}

		if (match && ip < il && ic == ')' && (op >= ol ||
			opt.charAt(op) == '=')) {
		    if (op < ol && opt.charAt(op) == '=') {
			/* may be an empty string - OK */
			longoptarg.set(opt.substring(op + 1));
		    } else {
			longoptarg.set(null);
		    }
		    return cp;
		}
		if (ip < il && ic == ')' && ++ip == il)
		    break;
		ic = optstring.charAt(ip);
	    }
	    cp = ip;
	    /*
	     * Handle double-colon in optstring ("a::(longa)") The old
	     * getopt() accepts it and treats it as a required argument.
	     */
	    while ((cp > 0) && (cp < il) && (optstring.charAt(cp) == ':')) {
		--cp;
	    }
	} while (cp < il);
	return -1;
    }

    /**
     * Get the current option value.
     */
    public String
    getOptarg()
    {
	return optarg;
    }

    /**
     * Get the index of the next option to be parsed.
     */
    public int
    getOptind()
    {
	return optind;
    }

    /**
     * Gets the command-line arguments.
     */
    public String[]
    getArgv()
    {
	// No defensive copy: Getopt is expected to modify the given
	// args array.
	return args;
    }

    /**
     * Gets the aggregated short option that just failed. Since long
     * options can't be aggregated, a failed long option can be obtained
     * by {@code getArgv()[getOptind() - 1]}.
     */
    public int
    getOptopt()
    {
	return optopt;
    }

    /**
     * Set to {@code false} to suppress diagnostic messages to stderr.
     */
    public void
    setOpterr(boolean err)
    {
	opterr = err;
    }

    /**
     * Gets the next option character, or -1 if there are no more
     * options. If getopt() encounters a short-option character or a
     * long-option string not described in the {@code optionString}
     * argument to the constructor, it returns the question-mark (?)
     * character. If it detects a missing option-argument, it also
     * returns the question-mark (?) character, unless the first
     * character of the {@code optionString} argument was a colon (:),
     * in which case getopt() returns the colon (:) character.
     * <p>
     * This implementation swaps the positions of options and
     * non-options in the given argv array.
     */
    public int
    getopt()
    {
	char c;
	int cp;
	boolean longopt;
	StringRef longoptarg = new StringRef();

	/*
	 * Has the end of the options been encountered?  The following
	 * implements the SUS requirements:
	 *
	 * If, when getopt() is called:
	 *	- the first character of argv[optind] is not '-'
	 *	- argv[optind] is the string "-"
	 * getopt() returns -1 without changing optind if
	 *	- argv[optind] is the string "--"
	 * getopt() returns -1 after incrementing optind
	 */
	if (_sp == 1) {
	    boolean nonOption;
	    do {
		nonOption = false;
		if (optind >= argc || args[optind].equals("-")) {
		    return EOF;
		} else if (args[optind].equals("--")) {
		    ++optind;
		    return EOF;
		} else if (args[optind].charAt(0) != '-') {
		    // non-option: here we deviate from the SUS requirements
		    // by not quitting, and instead move non-options to the
		    // end of the args array
		    nonOption = true;
		    String tmp = args[optind];
		    if (optind + 1 < args.length) {
			System.arraycopy(args, optind + 1, args, optind,
				args.length - (optind + 1));
			args[args.length - 1] = tmp;
		    }
		    --argc;
		}
	    } while (nonOption);
	}

	/*
	 * Getting this far indicates that an option has been encountered.
	 * Note that the syntax of optstring applies special meanings to
	 * the characters ':' and '(', so they are not permissible as
	 * option letters. A special meaning is also applied to the ')'
	 * character, but its meaning can be determined from context.
	 * Note that the specification only requires that the alnum
	 * characters be accepted.
	 *
	 * If the second character of the argument is a '-' this must be
	 * a long-option, otherwise it must be a short option.  Scan for
	 * the option in optstring by the appropriate algorithm. Either
	 * scan will return an index to the short-option character in
	 * optstring if the option is found and -1 otherwise.
	 *
	 * For an unrecognized long-option, optopt will equal 0, but
	 * since long-options can't aggregate the failing option can be
	 * identified by argv[optind-1].
	 */
	optopt = c = args[optind].charAt(_sp);
	optarg = null;
	longopt = (_sp == 1 && c == '-');
	if (!(longopt
		? ((cp = parselong(optstring, args[optind].substring(2),
		longoptarg)) != -1)
		: ((cp = parseshort(optstring, c)) != -1))) {
	    err("%s: illegal option -- %s", c,
		    (longopt ? args[optind] : null));
	    /*
	     * Note: When the long option is unrecognized, optopt will
	     * be '-' here, which matches the specification.
	     */
	    if (args[optind].length() == ++_sp || longopt) {
		++optind;
		_sp = 1;
	    }
	    return '?';
	}
	optopt = c = optstring.charAt(cp);

	/*
	 * A valid option has been identified.  If it should have an
	 * option-argument, process that now.  SUS defines the setting
	 * of optarg as follows:
	 *
	 *   1.	If the option was the last character in an element of
	 *   argv, then optarg contains the next element of argv, and
	 *   optind is incremented by 2. If the resulting value of
	 *   optind is not less than argc, this indicates a missing
	 *   option-argument, and getopt() returns an error indication.
	 *
	 *   2.	Otherwise, optarg points to the string following the
	 *   option character in that element of argv, and optind is
	 *   incremented by 1.
	 *
	 * The second clause allows -abcd (where b requires an
	 * option-argument) to be interpreted as "-a -b cd".
	 *
	 * Note that the option-argument can legally be an empty string,
	 * such as:
	 * 	command --option= operand
	 * which explicitly sets the value of --option to nil
	 */
	if (cp + 1 < optstring.length() && optstring.charAt(cp + 1) == ':') {
	    // The option takes an argument
	    if (!longopt && ((_sp + 1) < args[optind].length())) {
		optarg = args[optind++].substring(_sp + 1);
	    } else if (longopt && (longoptarg.get() != null)) {
		/*
		 * The option argument was explicitly set to the empty
		 * string on the command line (--option=)
		 */
		optind++;
		optarg = longoptarg.get();
	    } else if (++optind >= argc) {
		err("%s: option requires an argument -- %s", c,
			(longopt ? args[optind - 1] : null));
		_sp = 1;
		optarg = null;
		return (optstring.charAt(0) == ':' ? ':' : '?');
	    } else
		optarg = args[optind++];
		_sp = 1;
	    } else {
		// The option does NOT take an argument
		if (longopt && (longoptarg.get() != null)) {
		// User supplied an arg to an option that takes none
		err("%s: option doesn't take an argument -- %s", (char)0,
			(longopt ? args[optind] : null));
		optarg = longoptarg.set(null).get();
		c = '?';
	    }

	    if (longopt || args[optind].length() == ++_sp) {
		_sp = 1;
		++optind;
	    }
	    optarg = null;
	}
	return (c);
    }
}
