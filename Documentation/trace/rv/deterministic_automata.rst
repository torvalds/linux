Deterministic Automata
======================

Formally, a deterministic automaton, denoted by G, is defined as a quintuple:

        *G* = { *X*, *E*, *f*, x\ :subscript:`0`, X\ :subscript:`m` }

where:

- *X* is the set of states;
- *E* is the finite set of events;
- x\ :subscript:`0` is the initial state;
- X\ :subscript:`m` (subset of *X*) is the set of marked (or final) states.
- *f* : *X* x *E* -> *X* $ is the transition function. It defines the state
  transition in the occurrence of an event from *E* in the state *X*. In the
  special case of deterministic automata, the occurrence of the event in *E*
  in a state in *X* has a deterministic next state from *X*.

For example, a given automaton named 'wip' (wakeup in preemptive) can
be defined as:

- *X* = { ``preemptive``, ``non_preemptive``}
- *E* = { ``preempt_enable``, ``preempt_disable``, ``sched_waking``}
- x\ :subscript:`0` = ``preemptive``
- X\ :subscript:`m` = {``preemptive``}
- *f* =
   - *f*\ (``preemptive``, ``preempt_disable``) = ``non_preemptive``
   - *f*\ (``non_preemptive``, ``sched_waking``) = ``non_preemptive``
   - *f*\ (``non_preemptive``, ``preempt_enable``) = ``preemptive``

One of the benefits of this formal definition is that it can be presented
in multiple formats. For example, using a *graphical representation*, using
vertices (nodes) and edges, which is very intuitive for *operating system*
practitioners, without any loss.

The previous 'wip' automaton can also be represented as::

                       preempt_enable
          +---------------------------------+
          v                                 |
        #============#  preempt_disable   +------------------+
    --> H preemptive H -----------------> |  non_preemptive  |
        #============#                    +------------------+
                                            ^              |
                                            | sched_waking |
                                            +--------------+

Deterministic Automaton in C
----------------------------

In the paper "Efficient formal verification for the Linux kernel",
the authors present a simple way to represent an automaton in C that can
be used as regular code in the Linux kernel.

For example, the 'wip' automata can be presented as (augmented with comments)::

  /* enum representation of X (set of states) to be used as index */
  enum states {
	preemptive = 0,
	non_preemptive,
	state_max
  };

  #define INVALID_STATE state_max

  /* enum representation of E (set of events) to be used as index */
  enum events {
	preempt_disable = 0,
	preempt_enable,
	sched_waking,
	event_max
  };

  struct automaton {
	char *state_names[state_max];                   // X: the set of states
	char *event_names[event_max];                   // E: the finite set of events
	unsigned char function[state_max][event_max];   // f: transition function
	unsigned char initial_state;                    // x_0: the initial state
	bool final_states[state_max];                   // X_m: the set of marked states
  };

  struct automaton aut = {
	.state_names = {
		"preemptive",
		"non_preemptive"
	},
	.event_names = {
		"preempt_disable",
		"preempt_enable",
		"sched_waking"
	},
	.function = {
		{ non_preemptive,  INVALID_STATE,  INVALID_STATE },
		{  INVALID_STATE,     preemptive, non_preemptive },
	},
	.initial_state = preemptive,
	.final_states = { 1, 0 },
  };

The *transition function* is represented as a matrix of states (lines) and
events (columns), and so the function *f* : *X* x *E* -> *X* can be solved
in O(1). For example::

  next_state = automaton_wip.function[curr_state][event];

Graphviz .dot format
--------------------

The Graphviz open-source tool can produce the graphical representation
of an automaton using the (textual) DOT language as the source code.
The DOT format is widely used and can be converted to many other formats.

For example, this is the 'wip' model in DOT::

  digraph state_automaton {
        {node [shape = circle] "non_preemptive"};
        {node [shape = plaintext, style=invis, label=""] "__init_preemptive"};
        {node [shape = doublecircle] "preemptive"};
        {node [shape = circle] "preemptive"};
        "__init_preemptive" -> "preemptive";
        "non_preemptive" [label = "non_preemptive"];
        "non_preemptive" -> "non_preemptive" [ label = "sched_waking" ];
        "non_preemptive" -> "preemptive" [ label = "preempt_enable" ];
        "preemptive" [label = "preemptive"];
        "preemptive" -> "non_preemptive" [ label = "preempt_disable" ];
        { rank = min ;
                "__init_preemptive";
                "preemptive";
        }
  }

This DOT format can be transformed into a bitmap or vectorial image
using the dot utility, or into an ASCII art using graph-easy. For
instance::

  $ dot -Tsvg -o wip.svg wip.dot
  $ graph-easy wip.dot > wip.txt

dot2c
-----

dot2c is a utility that can parse a .dot file containing an automaton as
in the example above and automatically convert it to the C representation
presented in [3].

For example, having the previous 'wip' model into a file named 'wip.dot',
the following command will transform the .dot file into the C
representation (previously shown) in the 'wip.h' file::

  $ dot2c wip.dot > wip.h

The 'wip.h' content is the code sample in section 'Deterministic Automaton
in C'.

Remarks
-------

The automata formalism allows modeling discrete event systems (DES) in
multiple formats, suitable for different applications/users.

For example, the formal description using set theory is better suitable
for automata operations, while the graphical format for human interpretation;
and computer languages for machine execution.

References
----------

Many textbooks cover automata formalism. For a brief introduction see::

  O'Regan, Gerard. Concise guide to software engineering. Springer,
  Cham, 2017.

For a detailed description, including operations, and application on Discrete
Event Systems (DES), see::

  Cassandras, Christos G., and Stephane Lafortune, eds. Introduction to discrete
  event systems. Boston, MA: Springer US, 2008.

For the C representation in kernel, see::

  De Oliveira, Daniel Bristot; Cucinotta, Tommaso; De Oliveira, Romulo
  Silva. Efficient formal verification for the Linux kernel. In:
  International Conference on Software Engineering and Formal Methods.
  Springer, Cham, 2019. p. 315-332.
