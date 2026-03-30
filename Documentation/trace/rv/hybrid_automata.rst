Hybrid Automata
===============

Hybrid automata are an extension of deterministic automata, there are several
definitions of hybrid automata in the literature. The adaptation implemented
here is formally denoted by G and defined as a 7-tuple:

        *G* = { *X*, *E*, *V*, *f*, x\ :subscript:`0`, X\ :subscript:`m`, *i* }

- *X* is the set of states;
- *E* is the finite set of events;
- *V* is the finite set of environment variables;
- x\ :subscript:`0` is the initial state;
- X\ :subscript:`m` (subset of *X*) is the set of marked (or final) states.
- *f* : *X* x *E* x *C(V)* -> *X* is the transition function.
  It defines the state transition in the occurrence of an event from *E* in the
  state *X*. Unlike deterministic automata, the transition function also
  includes guards from the set of all possible constraints (defined as *C(V)*).
  Guards can be true or false with the valuation of *V* when the event occurs,
  and the transition is possible only when constraints are true. Similarly to
  deterministic automata, the occurrence of the event in *E* in a state in *X*
  has a deterministic next state from *X*, if the guard is true.
- *i* : *X* -> *C'(V)* is the invariant assignment function, this is a
  constraint assigned to each state in *X*, every state in *X* must be left
  before the invariant turns to false. We can omit the representation of
  invariants whose value is true regardless of the valuation of *V*.

The set of all possible constraints *C(V)* is defined according to the
following grammar:

        g = v < c | v > c | v <= c | v >= c | v == c | v != c | g && g | true

With v a variable in *V* and c a numerical value.

We define the special case of hybrid automata whose variables grow with uniform
rates as timed automata. In this case, the variables are called clocks.
As the name implies, timed automata can be used to describe real time.
Additionally, clocks support another type of guard which always evaluates to true:

        reset(v)

The reset constraint is used to set the value of a clock to 0.

The set of invariant constraints *C'(V)* is a subset of *C(V)* including only
constraint of the form:

        g = v < c | true

This simplifies the implementation as a clock expiration is a necessary and
sufficient condition for the violation of invariants while still allowing more
complex constraints to be specified as guards.

It is important to note that any hybrid automaton is a valid deterministic
automaton with additional guards and invariants. Those can only further
constrain what transitions are valid but it is not possible to define
transition functions starting from the same state in *X* and the same event in
*E* but ending up in different states in *X* based on the valuation of *V*.

Examples
--------

Wip as hybrid automaton
~~~~~~~~~~~~~~~~~~~~~~~

The 'wip' (wakeup in preemptive) example introduced as a deterministic automaton
can also be described as:

- *X* = { ``any_thread_running`` }
- *E* = { ``sched_waking`` }
- *V* = { ``preemptive`` }
- x\ :subscript:`0` = ``any_thread_running``
- X\ :subscript:`m` = {``any_thread_running``}
- *f* =
   - *f*\ (``any_thread_running``, ``sched_waking``, ``preemptive==0``) = ``any_thread_running``
- *i* =
   - *i*\ (``any_thread_running``) = ``true``

Which can be represented graphically as::

     |
     |
     v
   #====================#   sched_waking;preemptive==0
   H                    H ------------------------------+
   H any_thread_running H                               |
   H                    H <-----------------------------+
   #====================#

In this example, by using the preemptive state of the system as an environment
variable, we can assert this constraint on ``sched_waking`` without requiring
preemption events (as we would in a deterministic automaton), which can be
useful in case those events are not available or not reliable on the system.

Since all the invariants in *i* are true, we can omit them from the representation.

Stall model with guards (iteration 1)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

As a sample timed automaton we can define 'stall' as:

- *X* = { ``dequeued``, ``enqueued``, ``running``}
- *E* = { ``enqueue``, ``dequeue``, ``switch_in``}
- *V* = { ``clk`` }
- x\ :subscript:`0` = ``dequeue``
- X\ :subscript:`m` = {``dequeue``}
- *f* =
   - *f*\ (``enqueued``, ``switch_in``, ``clk < threshold``) = ``running``
   - *f*\ (``running``, ``dequeue``) = ``dequeued``
   - *f*\ (``dequeued``, ``enqueue``, ``reset(clk)``) = ``enqueued``
- *i* = *omitted as all true*

Graphically represented as::

       |
       |
       v
     #============================#
     H          dequeued          H <+
     #============================#  |
       |                             |
       | enqueue; reset(clk)         |
       v                             |
     +----------------------------+  |
     |          enqueued          |  | dequeue
     +----------------------------+  |
       |                             |
       | switch_in; clk < threshold  |
       v                             |
     +----------------------------+  |
     |          running           | -+
     +----------------------------+

This model imposes that the time between when a task is enqueued (it becomes
runnable) and when the task gets to run must be lower than a certain threshold.
A failure in this model means that the task is starving.
One problem in using guards on the edges in this case is that the model will
not report a failure until the ``switch_in`` event occurs. This means that,
according to the model, it is valid for the task never to run.

Stall model with invariants (iteration 2)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The first iteration isn't exactly what was intended, we can change the model as:

- *X* = { ``dequeued``, ``enqueued``, ``running``}
- *E* = { ``enqueue``, ``dequeue``, ``switch_in``}
- *V* = { ``clk`` }
- x\ :subscript:`0` = ``dequeue``
- X\ :subscript:`m` = {``dequeue``}
- *f* =
   - *f*\ (``enqueued``, ``switch_in``) = ``running``
   - *f*\ (``running``, ``dequeue``) = ``dequeued``
   - *f*\ (``dequeued``, ``enqueue``, ``reset(clk)``) = ``enqueued``
- *i* =
   - *i*\ (``enqueued``) = ``clk < threshold``

Graphically::

    |
    |
    v
  #=========================#
  H        dequeued         H <+
  #=========================#  |
    |                          |
    | enqueue; reset(clk)      |
    v                          |
  +-------------------------+  |
  |        enqueued         |  |
  |    clk < threshold      |  | dequeue
  +-------------------------+  |
    |                          |
    | switch_in                |
    v                          |
  +-------------------------+  |
  |         running         | -+
  +-------------------------+

In this case, we moved the guard as an invariant to the ``enqueued`` state,
this means we not only forbid the occurrence of ``switch_in`` when ``clk`` is
past the threshold but also mark as invalid in case we are *still* in
``enqueued`` after the threshold. This model is effectively in an invalid state
as soon as a task is starving, rather than when the starving task finally runs.

Hybrid Automaton in C
---------------------

The definition of hybrid automata in C is heavily based on the deterministic
automata one. Specifically, we add the set of environment variables and the
constraints (both guards on transitions and invariants on states) as follows.
This is a combination of both iterations of the stall example::

  /* enum representation of X (set of states) to be used as index */
  enum states {
	dequeued,
	enqueued,
	running,
	state_max,
  };

  #define INVALID_STATE state_max

  /* enum representation of E (set of events) to be used as index */
  enum events {
	dequeue,
	enqueue,
	switch_in,
	event_max,
  };

  /* enum representation of V (set of environment variables) to be used as index */
  enum envs {
	clk,
	env_max,
	env_max_stored = env_max,
  };

  struct automaton {
	char *state_names[state_max];                  // X: the set of states
	char *event_names[event_max];                  // E: the finite set of events
	char *env_names[env_max];                      // V: the finite set of env vars
	unsigned char function[state_max][event_max];  // f: transition function
	unsigned char initial_state;                   // x_0: the initial state
	bool final_states[state_max];                  // X_m: the set of marked states
  };

  struct automaton aut = {
	.state_names = {
		"dequeued",
		"enqueued",
		"running",
	},
	.event_names = {
		"dequeue",
		"enqueue",
		"switch_in",
	},
	.env_names = {
		"clk",
	},
	.function = {
		{ INVALID_STATE,      enqueued, INVALID_STATE },
		{ INVALID_STATE, INVALID_STATE,       running },
		{      dequeued, INVALID_STATE, INVALID_STATE },
	},
	.initial_state = dequeued,
	.final_states = { 1, 0, 0 },
  };

  static bool verify_constraint(enum states curr_state, enum events event,
                                enum states next_state)
  {
	bool res = true;

	/* Validate guards as part of f */
	if (curr_state == enqueued && event == switch_in)
		res = get_env(clk) < threshold;
	else if (curr_state == dequeued && event == enqueue)
		reset_env(clk);

	/* Validate invariants in i */
	if (next_state == curr_state || !res)
		return res;
	if (next_state == enqueued)
		ha_start_timer_jiffy(ha_mon, clk, threshold_jiffies);
	else if (curr_state == enqueued)
		res = !ha_cancel_timer(ha_mon);
	return res;
  }

The function ``verify_constraint``, here reported as simplified, checks guards,
performs resets and starts timers to validate invariants according to
specification, those cannot easily be represented in the automaton struct.
Due to the complex nature of environment variables, the user needs to provide
functions to get and reset environment variables that are not common clocks
(e.g. clocks with ns or jiffy granularity).
Since invariants are only defined as clock expirations (e.g. *clk <
threshold*), reaching the expiration of a timer armed when entering the state
is in fact a failure in the model and triggers a reaction. Leaving the state
stops the timer.

It is important to note that timers implemented with hrtimers introduce
overhead, if the monitor has several instances (e.g. all tasks) this can become
an issue. The impact can be decreased using the timer wheel (``HA_TIMER_TYPE``
set to ``HA_TIMER_WHEEL``), this lowers the responsiveness of the timer without
damaging the accuracy of the model, since the invariant condition is checked
before disabling the timer in case the callback is late.
Alternatively, if the monitor is guaranteed to *eventually* leave the state and
the incurred delay to wait for the next event is acceptable, guards can be used
in place of invariants, as seen in the stall example.

Graphviz .dot format
--------------------

Also the Graphviz representation of hybrid automata is an extension of the
deterministic automata one. Specifically, guards can be provided in the event
name separated by ``;``::

    "state_start" -> "state_dest" [ label = "sched_waking;preemptible==0;reset(clk)" ];

Invariant can be specified in the state label (not the node name!) separated by ``\n``::

    "enqueued" [label = "enqueued\nclk < threshold_jiffies"];

Constraints can be specified as valid C comparisons and allow spaces, the first
element of the comparison must be the clock while the second is a numerical or
parametrised value. Guards allow comparisons to be combined with boolean
operations (``&&`` and ``||``), resets must be separated from other constraints.

This is the full example of the last version of the 'stall' model in DOT::

  digraph state_automaton {
      {node [shape = circle] "enqueued"};
      {node [shape = plaintext, style=invis, label=""] "__init_dequeued"};
      {node [shape = doublecircle] "dequeued"};
      {node [shape = circle] "running"};
      "__init_dequeued" -> "dequeued";
      "enqueued" [label = "enqueued\nclk < threshold_jiffies"];
      "running" [label = "running"];
      "dequeued" [label = "dequeued"];
      "enqueued" -> "running" [ label = "switch_in" ];
      "running" -> "dequeued" [ label = "dequeue" ];
      "dequeued" -> "enqueued" [ label = "enqueue;reset(clk)" ];
      { rank = min ;
          "__init_dequeued";
          "dequeued";
      }
  }

References
----------

One book covering model checking and timed automata is::

  Christel Baier and Joost-Pieter Katoen: Principles of Model Checking,
  The MIT Press, 2008.

Hybrid automata are described in detail in::

  Thomas Henzinger: The theory of hybrid automata,
  Proceedings 11th Annual IEEE Symposium on Logic in Computer Science, 1996.
