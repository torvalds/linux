#include <linux/module.h>
#include "mali_linux_trace.h"
#include "mali_osk.h"

/* The Linux trace point for hardware activity (idle vs running) */
void _mali_osk_profiling_add_event(u32 event_id, u32 data0)
{
    trace_mali_timeline_event(event_id);
}

/* The Linux trace point for hardware counters */
void _mali_osk_profiling_add_counter(u32 event_id, u32 data0)
{
    trace_mali_hw_counter(event_id, data0);
}

/* This table stores the event to be counted by each counter
 * 0xFFFFFFFF is a special value which means disable counter
 */
//TODO at the moment this table is indexed by the magic numbers
//listed in gator_events_mali.c. In future these numbers should
//be shared through the mali_linux_trace.h header
u32 counter_table[17] = {0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,
                         0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,
                         0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,
                         0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,
                                                          0xFFFFFFFF};

/* Called by gator.ko to populate the table above */
int _mali_osk_counter_event(u32 counter, u32 event)
{
    /* Remember what has been set, and that a change has occured
     * When a job actually starts the code will program the registers
     */
    //TODO as above these magic numbers need to be moved to a header file
    if( counter >=5 && counter < 17 ) {
        counter_table[counter] = event;

        return 1;
	} else {
		printk("mali rjc: counter out of range (%d,%d)\n", counter, event);
    }

    return 0;
}	
EXPORT_SYMBOL(_mali_osk_counter_event);

