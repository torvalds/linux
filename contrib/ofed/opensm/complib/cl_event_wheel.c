/*
 * Copyright (c) 2004-2007 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <math.h>
#include <stdlib.h>
#include <complib/cl_event_wheel.h>
#include <complib/cl_debug.h>

#define CL_DBG(fmt, ...)

static cl_status_t __event_will_age_before(IN const cl_list_item_t *
					   const p_list_item, IN void *context)
{
	uint64_t aging_time = *((uint64_t *) context);
	cl_event_wheel_reg_info_t *p_event;

	p_event =
	    PARENT_STRUCT(p_list_item, cl_event_wheel_reg_info_t, list_item);

	if (p_event->aging_time < aging_time)
		return CL_SUCCESS;
	else
		return CL_NOT_FOUND;
}

static void __cl_event_wheel_callback(IN void *context)
{
	cl_event_wheel_t *p_event_wheel = (cl_event_wheel_t *) context;
	cl_list_item_t *p_list_item, *p_prev_event_list_item;
	cl_list_item_t *p_list_next_item;
	cl_event_wheel_reg_info_t *p_event;
	uint64_t current_time;
	uint64_t next_aging_time;
	uint32_t new_timeout;
	cl_status_t cl_status;

	/* might be during closing ...  */
	if (p_event_wheel->closing)
		return;

	current_time = cl_get_time_stamp();

	if (NULL != p_event_wheel->p_external_lock)

		/* Take care of the order of acquiring locks to avoid the deadlock!
		 * The external lock goes first.
		 */
		cl_spinlock_acquire(p_event_wheel->p_external_lock);

	cl_spinlock_acquire(&p_event_wheel->lock);

	p_list_item = cl_qlist_head(&p_event_wheel->events_wheel);
	if (p_list_item == cl_qlist_end(&p_event_wheel->events_wheel))
		/* the list is empty - nothing to do */
		goto Exit;

	/* we found such an item.  get the p_event */
	p_event =
	    PARENT_STRUCT(p_list_item, cl_event_wheel_reg_info_t, list_item);

	while (p_event->aging_time <= current_time) {
		/* this object has aged - invoke it's callback */
		if (p_event->pfn_aged_callback)
			next_aging_time =
			    p_event->pfn_aged_callback(p_event->key,
						       p_event->num_regs,
						       p_event->context);
		else
			next_aging_time = 0;

		/* point to the next object in the wheel */
		p_list_next_item = cl_qlist_next(p_list_item);

		/* We need to retire the event if the next aging time passed */
		if (next_aging_time < current_time) {
			/* remove it from the map */
			cl_qmap_remove_item(&p_event_wheel->events_map,
					    &(p_event->map_item));

			/* pop p_event from the wheel */
			cl_qlist_remove_head(&p_event_wheel->events_wheel);

			/* delete the event info object - allocated by cl_event_wheel_reg */
			free(p_event);
		} else {
			/* update the required aging time */
			p_event->aging_time = next_aging_time;
			p_event->num_regs++;

			/* do not remove from the map  - but remove from the list head and
			   place in the correct position */

			/* pop p_event from the wheel */
			cl_qlist_remove_head(&p_event_wheel->events_wheel);

			/* find the event that ages just before */
			p_prev_event_list_item =
			    cl_qlist_find_from_tail(&p_event_wheel->
						    events_wheel,
						    __event_will_age_before,
						    &p_event->aging_time);

			/* insert just after */
			cl_qlist_insert_next(&p_event_wheel->events_wheel,
					     p_prev_event_list_item,
					     &p_event->list_item);

			/* as we have modified the list - restart from first item: */
			p_list_next_item =
			    cl_qlist_head(&p_event_wheel->events_wheel);
		}

		/* advance to next event */
		p_list_item = p_list_next_item;
		if (p_list_item == cl_qlist_end(&p_event_wheel->events_wheel))
			/* the list is empty - nothing to do */
			break;

		/* get the p_event */
		p_event =
		    PARENT_STRUCT(p_list_item, cl_event_wheel_reg_info_t,
				  list_item);
	}

	/* We need to restart the timer only if the list is not empty now */
	if (p_list_item != cl_qlist_end(&p_event_wheel->events_wheel)) {
		/* get the p_event */
		p_event =
		    PARENT_STRUCT(p_list_item, cl_event_wheel_reg_info_t,
				  list_item);

		/* start the timer to the timeout [msec] */
		new_timeout =
		    (uint32_t) ((p_event->aging_time - current_time + 500) / 1000);
		CL_DBG("__cl_event_wheel_callback: Restart timer in: "
		       "%u [msec]\n", new_timeout);
		cl_status = cl_timer_start(&p_event_wheel->timer, new_timeout);
		if (cl_status != CL_SUCCESS) {
			CL_DBG("__cl_event_wheel_callback : ERR 6200: "
			       "Failed to start timer\n");
		}
	}

	/* release the lock */
Exit:
	cl_spinlock_release(&p_event_wheel->lock);
	if (NULL != p_event_wheel->p_external_lock)
		cl_spinlock_release(p_event_wheel->p_external_lock);
}

/*
 * Construct and Initialize
 */
void cl_event_wheel_construct(IN cl_event_wheel_t * const p_event_wheel)
{
	cl_spinlock_construct(&(p_event_wheel->lock));
	cl_timer_construct(&(p_event_wheel->timer));
}

cl_status_t cl_event_wheel_init(IN cl_event_wheel_t * const p_event_wheel)
{
	cl_status_t cl_status = CL_SUCCESS;

	/* initialize */
	p_event_wheel->p_external_lock = NULL;
	p_event_wheel->closing = FALSE;
	cl_status = cl_spinlock_init(&(p_event_wheel->lock));
	if (cl_status != CL_SUCCESS)
		return cl_status;
	cl_qlist_init(&p_event_wheel->events_wheel);
	cl_qmap_init(&p_event_wheel->events_map);

	/* init the timer with timeout */
	cl_status = cl_timer_init(&p_event_wheel->timer, __cl_event_wheel_callback, p_event_wheel);	/* cb context */

	return cl_status;
}

cl_status_t cl_event_wheel_init_ex(IN cl_event_wheel_t * const p_event_wheel,
				   IN cl_spinlock_t * p_external_lock)
{
	cl_status_t cl_status;

	cl_status = cl_event_wheel_init(p_event_wheel);
	if (CL_SUCCESS != cl_status)
		return cl_status;

	p_event_wheel->p_external_lock = p_external_lock;
	return cl_status;
}

void cl_event_wheel_dump(IN cl_event_wheel_t * const p_event_wheel)
{
	cl_list_item_t *p_list_item;
	cl_event_wheel_reg_info_t __attribute__((__unused__)) *p_event;

	p_list_item = cl_qlist_head(&p_event_wheel->events_wheel);

	while (p_list_item != cl_qlist_end(&p_event_wheel->events_wheel)) {
		p_event =
		    PARENT_STRUCT(p_list_item, cl_event_wheel_reg_info_t,
				  list_item);
		CL_DBG("cl_event_wheel_dump: Found event key:<0x%"
		       PRIx64 ">, num_regs:%d, aging time:%" PRIu64 "\n",
		       p_event->key, p_event->num_regs, p_event->aging_time);
		p_list_item = cl_qlist_next(p_list_item);
	}
}

void cl_event_wheel_destroy(IN cl_event_wheel_t * const p_event_wheel)
{
	cl_list_item_t *p_list_item;
	cl_map_item_t *p_map_item;
	cl_event_wheel_reg_info_t *p_event;

	/* we need to get a lock */
	cl_spinlock_acquire(&p_event_wheel->lock);

	cl_event_wheel_dump(p_event_wheel);

	/* go over all the items in the list and remove them */
	p_list_item = cl_qlist_remove_head(&p_event_wheel->events_wheel);
	while (p_list_item != cl_qlist_end(&p_event_wheel->events_wheel)) {
		p_event =
		    PARENT_STRUCT(p_list_item, cl_event_wheel_reg_info_t,
				  list_item);

		CL_DBG("cl_event_wheel_destroy: Found outstanding event"
		       " key:<0x%" PRIx64 ">\n", p_event->key);

		/* remove it from the map */
		p_map_item = &(p_event->map_item);
		cl_qmap_remove_item(&p_event_wheel->events_map, p_map_item);
		free(p_event);	/* allocated by cl_event_wheel_reg */
		p_list_item =
		    cl_qlist_remove_head(&p_event_wheel->events_wheel);
	}

	/* destroy the timer */
	cl_timer_destroy(&p_event_wheel->timer);

	/* destroy the lock (this should be done without releasing - we don't want
	   any other run to grab the lock at this point. */
	cl_spinlock_release(&p_event_wheel->lock);
	cl_spinlock_destroy(&(p_event_wheel->lock));
}

cl_status_t cl_event_wheel_reg(IN cl_event_wheel_t * const p_event_wheel,
			       IN const uint64_t key,
			       IN const uint64_t aging_time_usec,
			       IN cl_pfn_event_aged_cb_t pfn_callback,
			       IN void *const context)
{
	cl_event_wheel_reg_info_t *p_event;
	uint64_t timeout;
	uint32_t to;
	cl_status_t cl_status = CL_SUCCESS;
	cl_list_item_t *prev_event_list_item;
	cl_map_item_t *p_map_item;

	/* Get the lock on the manager */
	cl_spinlock_acquire(&(p_event_wheel->lock));

	cl_event_wheel_dump(p_event_wheel);

	/* Make sure such a key does not exists */
	p_map_item = cl_qmap_get(&p_event_wheel->events_map, key);
	if (p_map_item != cl_qmap_end(&p_event_wheel->events_map)) {
		CL_DBG("cl_event_wheel_reg: Already exists key:0x%"
		       PRIx64 "\n", key);

		/* already there - remove it from the list as it is getting a new time */
		p_event =
		    PARENT_STRUCT(p_map_item, cl_event_wheel_reg_info_t,
				  map_item);

		/* remove the item from the qlist */
		cl_qlist_remove_item(&p_event_wheel->events_wheel,
				     &p_event->list_item);
		/* and the qmap */
		cl_qmap_remove_item(&p_event_wheel->events_map,
				    &p_event->map_item);
	} else {
		/* make a new one */
		p_event = (cl_event_wheel_reg_info_t *)
		    malloc(sizeof(cl_event_wheel_reg_info_t));
		p_event->num_regs = 0;
	}

	p_event->key = key;
	p_event->aging_time = aging_time_usec;
	p_event->pfn_aged_callback = pfn_callback;
	p_event->context = context;
	p_event->num_regs++;

	CL_DBG("cl_event_wheel_reg: Registering event key:0x%" PRIx64
	       " aging in %u [msec]\n", p_event->key,
	       (uint32_t) ((p_event->aging_time - cl_get_time_stamp()) / 1000));

	/* If the list is empty - need to start the timer */
	if (cl_is_qlist_empty(&p_event_wheel->events_wheel)) {
		/* Edward Bortnikov 03/29/2003
		 * ++TBD Consider moving the timer manipulation behind the list manipulation.
		 */

		/* calculate the new timeout */
		timeout =
		    (p_event->aging_time - cl_get_time_stamp() + 500) / 1000;

		/* stop the timer if it is running */

		/* Edward Bortnikov 03/29/2003
		 * Don't call cl_timer_stop() because it spins forever.
		 * cl_timer_start() will invoke cl_timer_stop() by itself.
		 *
		 * The problematic scenario is when __cl_event_wheel_callback()
		 * is in race condition with this code. It sets timer.in_timer_cb
		 * to TRUE and then blocks on p_event_wheel->lock. Following this,
		 * the call to cl_timer_stop() hangs. Following this, the whole system
		 * enters into a deadlock.
		 *
		 * cl_timer_stop(&p_event_wheel->timer);
		 */

		/* The timeout for the cl_timer_start should be given as uint32_t.
		   if there is an overflow - warn about it. */
		to = (uint32_t) timeout;
		if (timeout > (uint32_t) timeout) {
			to = 0xffffffff;	/* max 32 bit timer */
			CL_DBG("cl_event_wheel_reg: timeout requested is "
			       "too large. Using timeout: %u\n", to);
		}

		/* start the timer to the timeout [msec] */
		cl_status = cl_timer_start(&p_event_wheel->timer, to);
		if (cl_status != CL_SUCCESS) {
			CL_DBG("cl_event_wheel_reg : ERR 6203: "
			       "Failed to start timer\n");
			goto Exit;
		}
	}

	/* insert the object to the qlist and the qmap */

	/* BUT WE MUST INSERT IT IN A SORTED MANNER */
	prev_event_list_item =
	    cl_qlist_find_from_tail(&p_event_wheel->events_wheel,
				    __event_will_age_before,
				    &p_event->aging_time);

	cl_qlist_insert_next(&p_event_wheel->events_wheel,
			     prev_event_list_item, &p_event->list_item);

	cl_qmap_insert(&p_event_wheel->events_map, key, &(p_event->map_item));

Exit:
	cl_spinlock_release(&p_event_wheel->lock);

	return cl_status;
}

void cl_event_wheel_unreg(IN cl_event_wheel_t * const p_event_wheel,
			  IN uint64_t key)
{
	cl_event_wheel_reg_info_t *p_event;
	cl_map_item_t *p_map_item;

	CL_DBG("cl_event_wheel_unreg: " "Removing key:0x%" PRIx64 "\n", key);

	cl_spinlock_acquire(&p_event_wheel->lock);
	p_map_item = cl_qmap_get(&p_event_wheel->events_map, key);
	if (p_map_item != cl_qmap_end(&p_event_wheel->events_map)) {
		/* we found such an item. */
		p_event =
		    PARENT_STRUCT(p_map_item, cl_event_wheel_reg_info_t,
				  map_item);

		/* remove the item from the qlist */
		cl_qlist_remove_item(&p_event_wheel->events_wheel,
				     &(p_event->list_item));
		/* remove the item from the qmap */
		cl_qmap_remove_item(&p_event_wheel->events_map,
				    &(p_event->map_item));

		CL_DBG("cl_event_wheel_unreg: Removed key:0x%" PRIx64 "\n",
		       key);

		/* free the item */
		free(p_event);
	} else {
		CL_DBG("cl_event_wheel_unreg: did not find key:0x%" PRIx64
		       "\n", key);
	}

	cl_spinlock_release(&p_event_wheel->lock);
}

uint32_t cl_event_wheel_num_regs(IN cl_event_wheel_t * const p_event_wheel,
				 IN uint64_t key)
{

	cl_event_wheel_reg_info_t *p_event;
	cl_map_item_t *p_map_item;
	uint32_t num_regs = 0;

	/* try to find the key in the map */
	CL_DBG("cl_event_wheel_num_regs: Looking for key:0x%" PRIx64 "\n", key);

	cl_spinlock_acquire(&p_event_wheel->lock);
	p_map_item = cl_qmap_get(&p_event_wheel->events_map, key);
	if (p_map_item != cl_qmap_end(&p_event_wheel->events_map)) {
		/* ok so we can simply return it's num_regs */
		p_event =
		    PARENT_STRUCT(p_map_item, cl_event_wheel_reg_info_t,
				  map_item);
		num_regs = p_event->num_regs;
	}

	cl_spinlock_release(&p_event_wheel->lock);
	return (num_regs);
}

#ifdef __CL_EVENT_WHEEL_TEST__

/* Dump out the complete state of the event wheel */
void __cl_event_wheel_dump(IN cl_event_wheel_t * const p_event_wheel)
{
	cl_list_item_t *p_list_item;
	cl_map_item_t *p_map_item;
	cl_event_wheel_reg_info_t *p_event;

	printf("************** Event Wheel Dump ***********************\n");
	printf("Event Wheel List has %u items:\n",
	       cl_qlist_count(&p_event_wheel->events_wheel));

	p_list_item = cl_qlist_head(&p_event_wheel->events_wheel);
	while (p_list_item != cl_qlist_end(&p_event_wheel->events_wheel)) {
		p_event =
		    PARENT_STRUCT(p_list_item, cl_event_wheel_reg_info_t,
				  list_item);
		printf("Event key:0x%" PRIx64 " Context:%s NumRegs:%u\n",
		       p_event->key, (char *)p_event->context,
		       p_event->num_regs);

		/* next */
		p_list_item = cl_qlist_next(p_list_item);
	}

	printf("Event Map has %u items:\n",
	       cl_qmap_count(&p_event_wheel->events_map));

	p_map_item = cl_qmap_head(&p_event_wheel->events_map);
	while (p_map_item != cl_qmap_end(&p_event_wheel->events_map)) {
		p_event =
		    PARENT_STRUCT(p_map_item, cl_event_wheel_reg_info_t,
				  map_item);
		printf("Event key:0x%" PRIx64 " Context:%s NumRegs:%u\n",
		       p_event->key, (char *)p_event->context,
		       p_event->num_regs);

		/* next */
		p_map_item = cl_qmap_next(p_map_item);
	}

}

/* The callback for aging event */
/* We assume we pass a text context */
static uint64_t __test_event_aging(uint64_t key, uint32_t num_regs, void *context)
{
	printf("*****************************************************\n");
	printf("Aged key: 0x%" PRIx64 " Context:%s\n", key, (char *)context);
}

int main()
{
	cl_event_wheel_t event_wheel;
	/*  uint64_t key; */

	/* init complib */
	complib_init();

	/* construct */
	cl_event_wheel_construct(&event_wheel);

	/* init */
	cl_event_wheel_init(&event_wheel);

	/* Start Playing */
	cl_event_wheel_reg(&event_wheel, 1,	/*  key */
			   cl_get_time_stamp() + 3000000,	/*  3 sec lifetime */
			   __test_event_aging,	/*  cb */
			   "The first Aging Event");

	cl_event_wheel_reg(&event_wheel, 2,	/*  key */
			   cl_get_time_stamp() + 3000000,	/*  3 sec lifetime */
			   __test_event_aging,	/*  cb */
			   "The Second Aging Event");

	cl_event_wheel_reg(&event_wheel, 3,	/*  key */
			   cl_get_time_stamp() + 3500000,	/*  3 sec lifetime */
			   __test_event_aging,	/*  cb */
			   "The Third Aging Event");

	__cl_event_wheel_dump(&event_wheel);

	sleep(2);
	cl_event_wheel_reg(&event_wheel, 2,	/*  key */
			   cl_get_time_stamp() + 8000000,	/*  3 sec lifetime */
			   __test_event_aging,	/*  cb */
			   "The Second Aging Event Moved");

	__cl_event_wheel_dump(&event_wheel);

	sleep(1);
	/* remove the third event */
	cl_event_wheel_unreg(&event_wheel, 3);	/*  key */

	/* get the number of registrations for the keys */
	printf("Event 1 Registered: %u\n",
	       cl_event_wheel_num_regs(&event_wheel, 1));
	printf("Event 2 Registered: %u\n",
	       cl_event_wheel_num_regs(&event_wheel, 2));

	sleep(5);
	/* destroy */
	cl_event_wheel_destroy(&event_wheel);

	complib_exit();

	return (0);
}

#endif				/* __CL_EVENT_WHEEL_TEST__ */
