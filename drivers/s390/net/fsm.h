/* $Id: fsm.h,v 1.1.1.1 2002/03/13 19:33:09 mschwide Exp $
 */
#ifndef _FSM_H_
#define _FSM_H_

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <asm/atomic.h>

/**
 * Define this to get debugging messages.
 */
#define FSM_DEBUG         0

/**
 * Define this to get debugging massages for
 * timer handling.
 */
#define FSM_TIMER_DEBUG   0

/**
 * Define these to record a history of
 * Events/Statechanges and print it if a
 * action_function is not found.
 */
#define FSM_DEBUG_HISTORY 0
#define FSM_HISTORY_SIZE  40

struct fsm_instance_t;

/**
 * Definition of an action function, called by a FSM
 */
typedef void (*fsm_function_t)(struct fsm_instance_t *, int, void *);

/**
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
/**
 * Element of State/Event history used for debugging.
 */
typedef struct {
	int state;
	int event;
} fsm_history;
#endif

/**
 * Representation of a FSM
 */
typedef struct fsm_instance_t {
	fsm *f;
	atomic_t state;
	char name[16];
	void *userdata;
	int userint;
#if FSM_DEBUG_HISTORY
	int         history_index;
	int         history_size;
	fsm_history history[FSM_HISTORY_SIZE];
#endif
} fsm_instance;

/**
 * Description of a state-event combination
 */
typedef struct {
	int cond_state;
	int cond_event;
	fsm_function_t function;
} fsm_node;

/**
 * Description of a FSM Timer.
 */
typedef struct {
	fsm_instance *fi;
	struct timer_list tl;
	int expire_event;
	void *event_arg;
} fsm_timer;

/**
 * Creates an FSM
 *
 * @param name        Name of this instance for logging purposes.
 * @param state_names An array of names for all states for logging purposes.
 * @param event_names An array of names for all events for logging purposes.
 * @param nr_states   Number of states for this instance.
 * @param nr_events   Number of events for this instance.
 * @param tmpl        An array of fsm_nodes, describing this FSM.
 * @param tmpl_len    Length of the describing array.
 * @param order       Parameter for allocation of the FSM data structs.
 */
extern fsm_instance *
init_fsm(char *name, const char **state_names,
	 const char **event_names,
	 int nr_states, int nr_events, const fsm_node *tmpl,
	 int tmpl_len, gfp_t order);

/**
 * Releases an FSM
 *
 * @param fi Pointer to an FSM, previously created with init_fsm.
 */
extern void kfree_fsm(fsm_instance *fi);

#if FSM_DEBUG_HISTORY
extern void
fsm_print_history(fsm_instance *fi);

extern void
fsm_record_history(fsm_instance *fi, int state, int event);
#endif

/**
 * Emits an event to a FSM.
 * If an action function is defined for the current state/event combination,
 * this function is called.
 *
 * @param fi    Pointer to FSM which should receive the event.
 * @param event The event do be delivered.
 * @param arg   A generic argument, handed to the action function.
 *
 * @return      0  on success,
 *              1  if current state or event is out of range
 *              !0 if state and event in range, but no action defined.
 */
extern __inline__ int
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
 * Modifies the state of an FSM.
 * This does <em>not</em> trigger an event or calls an action function.
 *
 * @param fi    Pointer to FSM
 * @param state The new state for this FSM.
 */
extern __inline__ void
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
}

/**
 * Retrieves the state of an FSM
 *
 * @param fi Pointer to FSM
 *
 * @return The current state of the FSM.
 */
extern __inline__ int
fsm_getstate(fsm_instance *fi)
{
	return atomic_read(&fi->state);
}

/**
 * Retrieves the name of the state of an FSM
 *
 * @param fi Pointer to FSM
 *
 * @return The current state of the FSM in a human readable form.
 */
extern const char *fsm_getstate_str(fsm_instance *fi);

/**
 * Initializes a timer for an FSM.
 * This prepares an fsm_timer for usage with fsm_addtimer.
 *
 * @param fi    Pointer to FSM
 * @param timer The timer to be initialized.
 */
extern void fsm_settimer(fsm_instance *fi, fsm_timer *);

/**
 * Clears a pending timer of an FSM instance.
 *
 * @param timer The timer to clear.
 */
extern void fsm_deltimer(fsm_timer *timer);

/**
 * Adds and starts a timer to an FSM instance.
 *
 * @param timer    The timer to be added. The field fi of that timer
 *                 must have been set to point to the instance.
 * @param millisec Duration, after which the timer should expire.
 * @param event    Event, to trigger if timer expires.
 * @param arg      Generic argument, provided to expiry function.
 *
 * @return         0 on success, -1 if timer is already active.
 */
extern int fsm_addtimer(fsm_timer *timer, int millisec, int event, void *arg);

/**
 * Modifies a timer of an FSM.
 *
 * @param timer    The timer to modify.
 * @param millisec Duration, after which the timer should expire.
 * @param event    Event, to trigger if timer expires.
 * @param arg      Generic argument, provided to expiry function.
 */
extern void fsm_modtimer(fsm_timer *timer, int millisec, int event, void *arg);

#endif /* _FSM_H_ */
