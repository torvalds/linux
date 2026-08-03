/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FSM_H_
#define _FSM_H_

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/atomic.h>

/*
 * Define this to get debugging messages.
 */
#define FSM_DEBUG         0

/*
 * Define this to get debugging massages for
 * timer handling.
 */
#define FSM_TIMER_DEBUG   0

/*
 * Define these to record a history of
 * Events/Statechanges and print it if a
 * action_function is not found.
 */
#define FSM_DEBUG_HISTORY 0
#define FSM_HISTORY_SIZE  40

struct fsm_instance_t;

/*
 * Definition of an action function, called by a FSM
 */
typedef void (*fsm_function_t)(struct fsm_instance_t *, int, void *);

/*
 * Internal jump table for a FSM
 */
typedef struct {
	fsm_function_t *jumpmatrix;
	int nr_events;
	int nr_states;
	const char **event_names;
	const char **state_names;
} fsm;

#if FSM_DEBUG_HISTORY
/*
 * Element of State/Event history used for debugging.
 */
typedef struct {
	int state;
	int event;
} fsm_history;
#endif

/*
 * Representation of a FSM
 */
typedef struct fsm_instance_t {
	fsm *f;
	atomic_t state;
	char name[16];
	void *userdata;
	int userint;
	wait_queue_head_t wait_q;
#if FSM_DEBUG_HISTORY
	int         history_index;
	int         history_size;
	fsm_history history[FSM_HISTORY_SIZE];
#endif
} fsm_instance;

/*
 * Description of a state-event combination
 */
typedef struct {
	int cond_state;
	int cond_event;
	fsm_function_t function;
} fsm_node;

/*
 * Description of a FSM Timer.
 */
typedef struct {
	fsm_instance *fi;
	struct timer_list tl;
	int expire_event;
	void *event_arg;
} fsm_timer;

/**
 * init_fsm - Creates a finite state machine
 * @name: Name of this instance for logging purposes
 * @state_names: Array of names for all states for logging purposes
 * @event_names: Array of names for all events for logging purposes
 * @nr_states: Number of states for this instance
 * @nr_events: Number of events for this instance
 * @tmpl: Pointer to fsm_node array describing this FSM
 * @tmpl_len: Number of entries in the tmpl array
 * @order: GFP flags for memory allocation (e.g. GFP_KERNEL)
 *
 * Allocates and initializes a finite state machine instance with the
 * specified states, events, and transition table.
 *
 * Return: Pointer to initialized FSM instance, or NULL on failure
 */
fsm_instance *init_fsm(char *name, const char **state_names,
		       const char **event_names, int nr_states,
		       int nr_events, const fsm_node *tmpl,
		       int tmpl_len, gfp_t order);

/**
 * kfree_fsm - Releases a finite state machine
 * @fi: Pointer to FSM instance, previously created with init_fsm()
 *
 * Frees all memory associated with the FSM instance.
 */
void kfree_fsm(fsm_instance *fi);

#if FSM_DEBUG_HISTORY
void fsm_print_history(fsm_instance *fi);

void fsm_record_history(fsm_instance *fi, int state, int event);
#endif

/**
 * fsm_event - Emits an event to a finite state machine
 * @fi: Pointer to FSM which should receive the event
 * @event: The event to be delivered
 * @arg: Generic argument, passed to the action function
 *
 * If an action function is defined for the current state/event
 * combination, that function is called with the provided arguments.
 *
 * Return:
 * * 0 - Success, action function was called
 * * 1 - State/event out of range, or no action function defined
 */
static inline int
fsm_event(fsm_instance *fi, int event, void *arg)
{
	fsm_function_t r;
	int state = atomic_read(&fi->state);

	if ((state >= fi->f->nr_states) ||
	    (event >= fi->f->nr_events)       ) {
		printk(KERN_ERR "fsm(%s): Invalid state st(%ld/%ld) ev(%d/%ld)\n",
			fi->name, (long)state,(long)fi->f->nr_states, event,
			(long)fi->f->nr_events);
#if FSM_DEBUG_HISTORY
		fsm_print_history(fi);
#endif
		return 1;
	}
	r = fi->f->jumpmatrix[fi->f->nr_states * event + state];
	if (r) {
#if FSM_DEBUG
		printk(KERN_DEBUG "fsm(%s): state %s event %s\n",
		       fi->name, fi->f->state_names[state],
		       fi->f->event_names[event]);
#endif
#if FSM_DEBUG_HISTORY
		fsm_record_history(fi, state, event);
#endif
		r(fi, event, arg);
		return 0;
	} else {
#if FSM_DEBUG || FSM_DEBUG_HISTORY
		printk(KERN_DEBUG "fsm(%s): no function for event %s in state %s\n",
		       fi->name, fi->f->event_names[event],
		       fi->f->state_names[state]);
#endif
#if FSM_DEBUG_HISTORY
		fsm_print_history(fi);
#endif
		return !0;
	}
}

/**
 * fsm_newstate - Modifies the state of a finite state machine
 * @fi: Pointer to FSM
 * @newstate: The new state for this FSM
 *
 * This does not trigger an event or call an action function.
 * Wakes up any processes waiting on the FSM's wait queue.
 */
static inline void
fsm_newstate(fsm_instance *fi, int newstate)
{
	atomic_set(&fi->state,newstate);
#if FSM_DEBUG_HISTORY
	fsm_record_history(fi, newstate, -1);
#endif
#if FSM_DEBUG
	printk(KERN_DEBUG "fsm(%s): New state %s\n", fi->name,
		fi->f->state_names[newstate]);
#endif
	wake_up(&fi->wait_q);
}

/**
 * fsm_getstate - Retrieves the current state of a finite state machine
 * @fi: Pointer to FSM
 *
 * Return: Current state number
 */
static inline int
fsm_getstate(fsm_instance *fi)
{
	return atomic_read(&fi->state);
}

/**
 * fsm_getstate_str - Retrieves the name of the current FSM state
 * @fi: Pointer to FSM
 *
 * Return: State name string, or "Invalid" if state is out of range
 */
const char *fsm_getstate_str(fsm_instance *fi);

/**
 * fsm_settimer - Initializes a timer for a finite state machine
 * @fi: Pointer to FSM
 * @this: The timer to be initialized
 *
 * Prepares an fsm_timer for usage with fsm_addtimer().
 */
void fsm_settimer(fsm_instance *fi, fsm_timer *this);

/**
 * fsm_deltimer - Clears a pending timer of an FSM instance
 * @timer: The timer to clear
 *
 * Stops and removes the timer. Safe to call on an inactive timer.
 */
void fsm_deltimer(fsm_timer *timer);

/**
 * fsm_addtimer - Adds and starts a timer for an FSM instance
 * @timer: The timer to be added (timer->fi must point to the FSM instance)
 * @millisec: Duration in milliseconds after which the timer expires
 * @event: Event to trigger when timer expires
 * @arg: Generic argument provided to the event handler
 *
 * Starts a timer that will trigger the specified event after the given
 * duration. The timer must have been initialized with fsm_settimer().
 *
 * Return: Always returns 0
 */
int fsm_addtimer(fsm_timer *timer, int millisec, int event, void *arg);

/**
 * fsm_modtimer - Modifies a timer of a finite state machine
 * @timer: The timer to modify
 * @millisec: New duration in milliseconds after which the timer expires
 * @event: Event to trigger when timer expires
 * @arg: Generic argument provided to the event handler
 *
 * Stops the existing timer and restarts it with new parameters.
 */
void fsm_modtimer(fsm_timer *timer, int millisec, int event, void *arg);

#endif /* _FSM_H_ */
