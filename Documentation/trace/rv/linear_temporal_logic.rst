Linear temporal logic
=====================

Introduction
------------

Runtime verification monitor is a verification technique which checks that the
kernel follows a specification. It does so by using tracepoints to monitor the
kernel's execution trace, and verifying that the execution trace sastifies the
specification.

Initially, the specification can only be written in the form of deterministic
automaton (DA).  However, while attempting to implement DA monitors for some
complex specifications, deterministic automaton is found to be inappropriate as
the specification language. The automaton is complicated, hard to understand,
and error-prone.

Thus, RV monitors based on linear temporal logic (LTL) are introduced. This type
of monitor uses LTL as specification instead of DA. For some cases, writing the
specification as LTL is more concise and intuitive.

Many materials explain LTL in details. One book is::

  Christel Baier and Joost-Pieter Katoen: Principles of Model Checking, The MIT
  Press, 2008.

Grammar
-------

Unlike some existing syntax, kernel's implementation of LTL is more verbose.
This is motivated by considering that the people who read the LTL specifications
may not be well-versed in LTL.

Grammar:
    ltl ::= opd | ( ltl ) | ltl binop ltl | unop ltl

Operands (opd):
    true, false, user-defined names consisting of upper-case characters, digits,
    and underscore.

Unary Operators (unop):
    always
    eventually
    next
    not

Binary Operators (binop):
    until
    and
    or
    imply
    equivalent

This grammar is ambiguous: operator precedence is not defined. Parentheses must
be used.

Example linear temporal logic
-----------------------------
.. code-block::

   RAIN imply (GO_OUTSIDE imply HAVE_UMBRELLA)

means: if it is raining, going outside means having an umbrella.

.. code-block::

   RAIN imply (WET until not RAIN)

means: if it is raining, it is going to be wet until the rain stops.

.. code-block::

   RAIN imply eventually not RAIN

means: if it is raining, rain will eventually stop.

The above examples are referring to the current time instance only. For kernel
verification, the `always` operator is usually desirable, to specify that
something is always true at the present and for all future. For example::

    always (RAIN imply eventually not RAIN)

means: *all* rain eventually stops.

In the above examples, `RAIN`, `GO_OUTSIDE`, `HAVE_UMBRELLA` and `WET` are the
"atomic propositions".

Monitor synthesis
-----------------

To synthesize an LTL into a kernel monitor, the `rvgen` tool can be used:
`tools/verification/rvgen`. The specification needs to be provided as a file,
and it must have a "RULE = LTL" assignment. For example::

    RULE = always (ACQUIRE imply ((not KILLED and not CRASHED) until RELEASE))

which says: if `ACQUIRE`, then `RELEASE` must happen before `KILLED` or
`CRASHED`.

The LTL can be broken down using sub-expressions. The above is equivalent to:

   .. code-block::

    RULE = always (ACQUIRE imply (ALIVE until RELEASE))
    ALIVE = not KILLED and not CRASHED

From this specification, `rvgen` generates the C implementation of a Buchi
automaton - a non-deterministic state machine which checks the satisfiability of
the LTL. See Documentation/trace/rv/monitor_synthesis.rst for details on using
`rvgen`.

References
----------

One book covering model checking and linear temporal logic is::

  Christel Baier and Joost-Pieter Katoen: Principles of Model Checking, The MIT
  Press, 2008.

For an example of using linear temporal logic in software testing, see::

  Ruijie Meng, Zhen Dong, Jialin Li, Ivan Beschastnikh, and Abhik Roychoudhury.
  2022. Linear-time temporal logic guided greybox fuzzing. In Proceedings of the
  44th International Conference on Software Engineering (ICSE '22).  Association
  for Computing Machinery, New York, NY, USA, 1343–1355.
  https://doi.org/10.1145/3510003.3510082

The kernel's LTL monitor implementation is based on::

  Gerth, R., Peled, D., Vardi, M.Y., Wolper, P. (1996). Simple On-the-fly
  Automatic Verification of Linear Temporal Logic. In: Dembiński, P., Średniawa,
  M. (eds) Protocol Specification, Testing and Verification XV. PSTV 1995. IFIP
  Advances in Information and Communication Technology. Springer, Boston, MA.
  https://doi.org/10.1007/978-0-387-34892-6_1
