Deterministic Automata Instrumentation
======================================

The RV monitor file created by dot2k, with the name "$MODEL_NAME.c"
includes a section dedicated to instrumentation.

In the example of the wip.dot monitor created on [1], it will look like::

  /*
   * This is the instrumentation part of the monitor.
   *
   * This is the section where manual work is required. Here the kernel events
   * are translated into model's event.
   *
   */
  static void handle_preempt_disable(void *data, /* XXX: fill header */)
  {
	da_handle_event_wip(preempt_disable_wip);
  }

  static void handle_preempt_enable(void *data, /* XXX: fill header */)
  {
	da_handle_event_wip(preempt_enable_wip);
  }

  static void handle_sched_waking(void *data, /* XXX: fill header */)
  {
	da_handle_event_wip(sched_waking_wip);
  }

  static int enable_wip(void)
  {
	int retval;

	retval = da_monitor_init_wip();
	if (retval)
		return retval;

	rv_attach_trace_probe("wip", /* XXX: tracepoint */, handle_preempt_disable);
	rv_attach_trace_probe("wip", /* XXX: tracepoint */, handle_preempt_enable);
	rv_attach_trace_probe("wip", /* XXX: tracepoint */, handle_sched_waking);

	return 0;
  }

The comment at the top of the section explains the general idea: the
instrumentation section translates *kernel events* into the *model's
event*.

Tracing callback functions
--------------------------

The first three functions are the starting point of the callback *handler
functions* for each of the three events from the wip model. The developer
does not necessarily need to use them: they are just starting points.

Using the example of::

 void handle_preempt_disable(void *data, /* XXX: fill header */)
 {
        da_handle_event_wip(preempt_disable_wip);
 }

The preempt_disable event from the model connects directly to the
preemptirq:preempt_disable. The preemptirq:preempt_disable event
has the following signature, from include/trace/events/preemptirq.h::

  TP_PROTO(unsigned long ip, unsigned long parent_ip)

Hence, the handle_preempt_disable() function will look like::

  void handle_preempt_disable(void *data, unsigned long ip, unsigned long parent_ip)

In this case, the kernel event translates one to one with the automata
event, and indeed, no other change is required for this function.

The next handler function, handle_preempt_enable() has the same argument
list from the handle_preempt_disable(). The difference is that the
preempt_enable event will be used to synchronize the system to the model.

Initially, the *model* is placed in the initial state. However, the *system*
might or might not be in the initial state. The monitor cannot start
processing events until it knows that the system has reached the initial state.
Otherwise, the monitor and the system could be out-of-sync.

Looking at the automata definition, it is possible to see that the system
and the model are expected to return to the initial state after the
preempt_enable execution. Hence, it can be used to synchronize the
system and the model at the initialization of the monitoring section.

The start is informed via a special handle function, the
"da_handle_start_event_$(MONITOR_NAME)(event)", in this case::

  da_handle_start_event_wip(preempt_enable_wip);

So, the callback function will look like::

  void handle_preempt_enable(void *data, unsigned long ip, unsigned long parent_ip)
  {
        da_handle_start_event_wip(preempt_enable_wip);
  }

Finally, the "handle_sched_waking()" will look like::

  void handle_sched_waking(void *data, struct task_struct *task)
  {
        da_handle_event_wip(sched_waking_wip);
  }

And the explanation is left for the reader as an exercise.

enable and disable functions
----------------------------

dot2k automatically creates two special functions::

  enable_$(MONITOR_NAME)()
  disable_$(MONITOR_NAME)()

These functions are called when the monitor is enabled and disabled,
respectively.

They should be used to *attach* and *detach* the instrumentation to the running
system. The developer must add to the relative function all that is needed to
*attach* and *detach* its monitor to the system.

For the wip case, these functions were named::

 enable_wip()
 disable_wip()

But no change was required because: by default, these functions *attach* and
*detach* the tracepoints_to_attach, which was enough for this case.

Instrumentation helpers
-----------------------

To complete the instrumentation, the *handler functions* need to be attached to a
kernel event, at the monitoring enable phase.

The RV interface also facilitates this step. For example, the macro "rv_attach_trace_probe()"
is used to connect the wip model events to the relative kernel event. dot2k automatically
adds "rv_attach_trace_probe()" function call for each model event in the enable phase, as
a suggestion.

For example, from the wip sample model::

  static int enable_wip(void)
  {
        int retval;

        retval = da_monitor_init_wip();
        if (retval)
                return retval;

        rv_attach_trace_probe("wip", /* XXX: tracepoint */, handle_preempt_enable);
        rv_attach_trace_probe("wip", /* XXX: tracepoint */, handle_sched_waking);
        rv_attach_trace_probe("wip", /* XXX: tracepoint */, handle_preempt_disable);

        return 0;
  }

The probes then need to be detached at the disable phase.

[1] The wip model is presented in::

  Documentation/trace/rv/deterministic_automata.rst

The wip monitor is presented in::

  Documentation/trace/rv/da_monitor_synthesis.rst
