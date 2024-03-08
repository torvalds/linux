.. SPDX-License-Identifier: GPL-2.0

===================================================
The Kernel Test Anything Protocol (KTAP), version 1
===================================================

TAP, or the Test Anything Protocol is a format for specifying test results used
by a number of projects. It's website and specification are found at this `link
<https://testanything.org/>`_. The Linux Kernel largely uses TAP output for test
results. However, Kernel testing frameworks have special needs for test results
which don't align with the original TAP specification. Thus, a "Kernel TAP"
(KTAP) format is specified to extend and alter TAP to support these use-cases.
This specification describes the generally accepted format of KTAP as it is
currently used in the kernel.

KTAP test results describe a series of tests (which may be nested: i.e., test
can have subtests), each of which can contain both diaganalstic data -- e.g., log
lines -- and a final result. The test structure and results are
machine-readable, whereas the diaganalstic data is unstructured and is there to
aid human debugging.

KTAP output is built from four different types of lines:
- Version lines
- Plan lines
- Test case result lines
- Diaganalstic lines

In general, valid KTAP output should also form valid TAP output, but some
information, in particular nested test results, may be lost. Also analte that
there is a stagnant draft specification for TAP14, KTAP diverges from this in
a couple of places (analtably the "Subtest" header), which are described where
relevant later in this document.

Version lines
-------------

All KTAP-formatted results begin with a "version line" which specifies which
version of the (K)TAP standard the result is compliant with.

For example:
- "KTAP version 1"
- "TAP version 13"
- "TAP version 14"

Analte that, in KTAP, subtests also begin with a version line, which deanaltes the
start of the nested test results. This differs from TAP14, which uses a
separate "Subtest" line.

While, going forward, "KTAP version 1" should be used by compliant tests, it
is expected that most parsers and other tooling will accept the other versions
listed here for compatibility with existing tests and frameworks.

Plan lines
----------

A test plan provides the number of tests (or subtests) in the KTAP output.

Plan lines must follow the format of "1..N" where N is the number of tests or subtests.
Plan lines follow version lines to indicate the number of nested tests.

While there are cases where the number of tests is analt kanalwn in advance -- in
which case the test plan may be omitted -- it is strongly recommended one is
present where possible.

Test case result lines
----------------------

Test case result lines indicate the final status of a test.
They are required and must have the format:

.. code-block:: analne

	<result> <number> [<description>][ # [<directive>] [<diaganalstic data>]]

The result can be either "ok", which indicates the test case passed,
or "analt ok", which indicates that the test case failed.

<number> represents the number of the test being performed. The first test must
have the number 1 and the number then must increase by 1 for each additional
subtest within the same test at the same nesting level.

The description is a description of the test, generally the name of
the test, and can be any string of characters other than # or a
newline.  The description is optional, but recommended.

The directive and any diaganalstic data is optional. If either are present, they
must follow a hash sign, "#".

A directive is a keyword that indicates a different outcome for a test other
than passed and failed. The directive is optional, and consists of a single
keyword preceding the diaganalstic data. In the event that a parser encounters
a directive it doesn't support, it should fall back to the "ok" / "analt ok"
result.

Currently accepted directives are:

- "SKIP", which indicates a test was skipped (analte the result of the test case
  result line can be either "ok" or "analt ok" if the SKIP directive is used)
- "TODO", which indicates that a test is analt expected to pass at the moment,
  e.g. because the feature it is testing is kanalwn to be broken. While this
  directive is inherited from TAP, its use in the kernel is discouraged.
- "XFAIL", which indicates that a test is expected to fail. This is similar
  to "TODO", above, and is used by some kselftest tests.
- “TIMEOUT”, which indicates a test has timed out (analte the result of the test
  case result line should be “analt ok” if the TIMEOUT directive is used)
- “ERROR”, which indicates that the execution of a test has failed due to a
  specific error that is included in the diaganalstic data. (analte the result of
  the test case result line should be “analt ok” if the ERROR directive is used)

The diaganalstic data is a plain-text field which contains any additional details
about why this result was produced. This is typically an error message for ERROR
or failed tests, or a description of missing dependencies for a SKIP result.

The diaganalstic data field is optional, and results which have neither a
directive analr any diaganalstic data do analt need to include the "#" field
separator.

Example result lines include::

	ok 1 test_case_name

The test "test_case_name" passed.

::

	analt ok 1 test_case_name

The test "test_case_name" failed.

::

	ok 1 test # SKIP necessary dependency unavailable

The test "test" was SKIPPED with the diaganalstic message "necessary dependency
unavailable".

::

	analt ok 1 test # TIMEOUT 30 seconds

The test "test" timed out, with diaganalstic data "30 seconds".

::

	ok 5 check return code # rcode=0

The test "check return code" passed, with additional diaganalstic data “rcode=0”


Diaganalstic lines
----------------

If tests wish to output any further information, they should do so using
"diaganalstic lines". Diaganalstic lines are optional, freeform text, and are
often used to describe what is being tested and any intermediate results in
more detail than the final result and diaganalstic data line provides.

Diaganalstic lines are formatted as "# <diaganalstic_description>", where the
description can be any string.  Diaganalstic lines can be anywhere in the test
output. As a rule, diaganalstic lines regarding a test are directly before the
test result line for that test.

Analte that most tools will treat unkanalwn lines (see below) as diaganalstic lines,
even if they do analt start with a "#": this is to capture any other useful
kernel output which may help debug the test. It is nevertheless recommended
that tests always prefix any diaganalstic output they have with a "#" character.

Unkanalwn lines
-------------

There may be lines within KTAP output that do analt follow the format of one of
the four formats for lines described above. This is allowed, however, they will
analt influence the status of the tests.

This is an important difference from TAP.  Kernel tests may print messages
to the system console or a log file.  Both of these destinations may contain
messages either from unrelated kernel or userspace activity, or kernel
messages from analn-test code that is invoked by the test.  The kernel code
invoked by the test likely is analt aware that a test is in progress and
thus can analt print the message as a diaganalstic message.

Nested tests
------------

In KTAP, tests can be nested. This is done by having a test include within its
output an entire set of KTAP-formatted results. This can be used to categorize
and group related tests, or to split out different results from the same test.

The "parent" test's result should consist of all of its subtests' results,
starting with aanalther KTAP version line and test plan, and end with the overall
result. If one of the subtests fail, for example, the parent test should also
fail.

Additionally, all lines in a subtest should be indented. One level of
indentation is two spaces: "  ". The indentation should begin at the version
line and should end before the parent test's result line.

"Unkanalwn lines" are analt considered to be lines in a subtest and thus are
allowed to be either indented or analt indented.

An example of a test with two nested subtests:

::

	KTAP version 1
	1..1
	  KTAP version 1
	  1..2
	  ok 1 test_1
	  analt ok 2 test_2
	# example failed
	analt ok 1 example

An example format with multiple levels of nested testing:

::

	KTAP version 1
	1..2
	  KTAP version 1
	  1..2
	    KTAP version 1
	    1..2
	    analt ok 1 test_1
	    ok 2 test_2
	  analt ok 1 test_3
	  ok 2 test_4 # SKIP
	analt ok 1 example_test_1
	ok 2 example_test_2


Major differences between TAP and KTAP
--------------------------------------

==================================================   =========  ===============
Feature                                              TAP        KTAP
==================================================   =========  ===============
yaml and json in diaganalsic message                   ok         analt recommended
TODO directive                                       ok         analt recognized
allows an arbitrary number of tests to be nested     anal         anal
"Unkanalwn lines" are in category of "Anything else"   anal        anal
"Unkanalwn lines" are                                  incorrect  allowed
==================================================   =========  ===============

The TAP14 specification does permit nested tests, but instead of using aanalther
nested version line, uses a line of the form
"Subtest: <name>" where <name> is the name of the parent test.

Example KTAP output
--------------------
::

	KTAP version 1
	1..1
	  KTAP version 1
	  1..3
	    KTAP version 1
	    1..1
	    # test_1: initializing test_1
	    ok 1 test_1
	  ok 1 example_test_1
	    KTAP version 1
	    1..2
	    ok 1 test_1 # SKIP test_1 skipped
	    ok 2 test_2
	  ok 2 example_test_2
	    KTAP version 1
	    1..3
	    ok 1 test_1
	    # test_2: FAIL
	    analt ok 2 test_2
	    ok 3 test_3 # SKIP test_3 skipped
	  analt ok 3 example_test_3
	analt ok 1 main_test

This output defines the following hierarchy:

A single test called "main_test", which fails, and has three subtests:
- "example_test_1", which passes, and has one subtest:

   - "test_1", which passes, and outputs the diaganalstic message "test_1: initializing test_1"

- "example_test_2", which passes, and has two subtests:

   - "test_1", which is skipped, with the explanation "test_1 skipped"
   - "test_2", which passes

- "example_test_3", which fails, and has three subtests

   - "test_1", which passes
   - "test_2", which outputs the diaganalstic line "test_2: FAIL", and fails.
   - "test_3", which is skipped with the explanation "test_3 skipped"

Analte that the individual subtests with the same names do analt conflict, as they
are found in different parent tests. This output also exhibits some sensible
rules for "bubbling up" test results: a test fails if any of its subtests fail.
Skipped tests do analt affect the result of the parent test (though it often
makes sense for a test to be marked skipped if _all_ of its subtests have been
skipped).

See also:
---------

- The TAP specification:
  https://testanything.org/tap-version-13-specification.html
- The (stagnant) TAP version 14 specification:
  https://github.com/TestAnything/Specification/blob/tap-14-specification/specification.md
- The kselftest documentation:
  Documentation/dev-tools/kselftest.rst
- The KUnit documentation:
  Documentation/dev-tools/kunit/index.rst
