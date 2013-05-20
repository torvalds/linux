#ifndef __FM_UTILS_H__
#define __FM_UTILS_H__

#include "fm_typedef.h"


/**
 * Base structure of fm object
 */
#define FM_NAME_MAX 20
struct fm_object {
    fm_s8      	name[FM_NAME_MAX+1];						// name of fm object
    fm_u8       type;									// type of fm object
    fm_u8       flag;									// flag of fm object
    fm_s32      ref;
    void        *priv;
    //struct fm_list	*list;									// list node of fm object
};


/*
 * FM FIFO 
 */
struct fm_fifo {
    struct fm_object obj;
    fm_s32 size;
    fm_s32 in;
    fm_s32 out;
    fm_s32 len;
    fm_s32 item_size;
    fm_s32 (*input)(struct fm_fifo *thiz, void *item);
    fm_s32 (*output)(struct fm_fifo *thiz, void *item);
    fm_bool (*is_full)(struct fm_fifo *thiz);
    fm_bool (*is_empty)(struct fm_fifo *thiz);
    fm_s32 (*get_total_len)(struct fm_fifo *thiz);
    fm_s32 (*get_valid_len)(struct fm_fifo *thiz);
    fm_s32 (*reset)(struct fm_fifo *thiz);
};

extern struct fm_fifo* fm_fifo_init(struct fm_fifo* fifo, void *buf, const fm_s8 *name, fm_s32 item_size, fm_s32 item_num); 

extern struct fm_fifo* fm_fifo_create(const fm_s8 *name, fm_s32 item_size, fm_s32 item_num); 

extern fm_s32 fm_fifo_release(struct fm_fifo *fifo); 

#define FM_FIFO_INPUT(fifop, item)  \
({                                    \
    fm_s32 __ret = (fm_s32)0;              \
    if(fifop && (fifop)->input){          \
        __ret = (fifop)->input(fifop, item);    \
    }                               \
    __ret;                          \
})

#define FM_FIFO_OUTPUT(fifop, item)  \
({                                    \
    fm_s32 __ret = (fm_s32)0;              \
    if(fifop && (fifop)->output){          \
        __ret = (fifop)->output(fifop, item);    \
    }                               \
    __ret;                          \
})

#define FM_FIFO_IS_FULL(fifop)  \
({                                    \
    fm_bool __ret = fm_false;              \
    if(fifop && (fifop)->is_full){          \
        __ret = (fifop)->is_full(fifop);    \
    }                               \
    __ret;                          \
})

#define FM_FIFO_IS_EMPTY(fifop)  \
({                                    \
    fm_bool __ret = fm_false;              \
    if(fifop && (fifop)->is_empty){          \
        __ret = (fifop)->is_empty(fifop);    \
    }                               \
    __ret;                          \
})

#define FM_FIFO_RESET(fifop)  \
({                                    \
    fm_s32 __ret = (fm_s32)0;              \
    if(fifop && (fifop)->reset){          \
        __ret = (fifop)->reset(fifop);    \
    }                               \
    __ret;                          \
})

#define FM_FIFO_GET_TOTAL_LEN(fifop)  \
({                                    \
    fm_s32 __ret = (fm_s32)0;              \
    if(fifop && (fifop)->get_total_len){          \
        __ret = (fifop)->get_total_len(fifop);    \
    }                               \
    __ret;                          \
})

#define FM_FIFO_GET_VALID_LEN(fifop)  \
({                                    \
    fm_s32 __ret = (fm_s32)0;              \
    if(fifop && (fifop)->get_valid_len){          \
        __ret = (fifop)->get_valid_len(fifop);    \
    }                               \
    __ret;                          \
})

       
/*
 * FM asynchronous information mechanism
 */
struct fm_flag_event {
    fm_s32 ref;
    fm_s8  name[FM_NAME_MAX+1];
    void *priv;

    volatile fm_u32 flag;

    //flag methods
    fm_u32(*send)(struct fm_flag_event* thiz, fm_u32 mask);
    fm_s32(*wait)(struct fm_flag_event* thiz, fm_u32 mask);
    long(*wait_timeout)(struct fm_flag_event* thiz, fm_u32 mask, long timeout);
    fm_u32(*clr)(struct fm_flag_event* thiz, fm_u32 mask);
    fm_u32(*get)(struct fm_flag_event* thiz);
    fm_u32(*rst)(struct fm_flag_event* thiz);
};

extern struct fm_flag_event* fm_flag_event_create(const fm_s8 *name);

extern fm_s32 fm_flag_event_get(struct fm_flag_event *thiz);

extern fm_s32 fm_flag_event_put(struct fm_flag_event *thiz);

#define FM_EVENT_SEND(eventp, mask)  \
({                                    \
    fm_u32 __ret = (fm_u32)0;              \
    if(eventp && (eventp)->send){          \
        __ret = (eventp)->send(eventp, mask);    \
    }                               \
    __ret;                          \
})

#define FM_EVENT_WAIT(eventp, mask)  \
({                                    \
    fm_s32 __ret = (fm_s32)0;              \
    if(eventp && (eventp)->wait){          \
        __ret = (eventp)->wait(eventp, mask);    \
    }                               \
    __ret;                          \
})

#define FM_EVENT_WAIT_TIMEOUT(eventp, mask, timeout)  \
({                                    \
    long __ret = (long)0;              \
    if(eventp && (eventp)->wait_timeout){          \
        __ret = (eventp)->wait_timeout(eventp, mask, timeout);    \
    }                               \
    __ret;                          \
})

#define FM_EVENT_GET(eventp)  \
({                                    \
    fm_u32 __ret = (fm_u32)0;              \
    if(eventp && (eventp)->get){          \
        __ret = (eventp)->get(eventp);    \
    }                               \
    __ret;                          \
})

#define FM_EVENT_RESET(eventp)  \
({                                    \
    fm_u32 __ret = (fm_u32)0;              \
    if(eventp && (eventp)->rst){          \
        __ret = (eventp)->rst(eventp);    \
    }                               \
    __ret;                          \
})

#define FM_EVENT_CLR(eventp, mask)  \
({                                    \
    fm_u32 __ret = (fm_u32)0;              \
    if(eventp && (eventp)->clr){          \
        __ret = (eventp)->clr(eventp, mask);    \
    }                               \
    __ret;                          \
})

/*
 * FM lock mechanism
 */
struct fm_lock {
    fm_s8   name[FM_NAME_MAX+1];
    fm_s32  ref;
    void    *priv;

    //lock methods
    fm_s32(*lock)(struct fm_lock* thiz);
	fm_s32(*trylock)(struct fm_lock *thiz,fm_s32 retryCnt);
    fm_s32(*unlock)(struct fm_lock* thiz);
};

extern struct fm_lock* fm_lock_create(const fm_s8 *name);

extern fm_s32 fm_lock_get(struct fm_lock *thiz);

extern fm_s32 fm_lock_put(struct fm_lock *thiz);

extern struct fm_lock* fm_spin_lock_create(const fm_s8 *name);

extern fm_s32 fm_spin_lock_get(struct fm_lock *thiz);

extern fm_s32 fm_spin_lock_put(struct fm_lock *thiz);

#define FM_LOCK(a)         \
({                           \
    fm_s32 __ret = (fm_s32)0; \
    if(a && (a)->lock){          \
        __ret = (a)->lock(a);    \
    }                       \
    __ret;                   \
})

#define FM_UNLOCK(a)         \
{                             \
    if((a)->unlock){          \
        (a)->unlock(a);    \
    }                       \
}


/*
 * FM timer mechanism
 */
enum fm_timer_ctrl {
    FM_TIMER_CTRL_GET_TIME = 0,
    FM_TIMER_CTRL_SET_TIME = 1,
    FM_TIMER_CTRL_MAX
};

#define FM_TIMER_FLAG_ACTIVATED (1<<0)

struct fm_timer {
    fm_s32 ref;
    fm_s8  name[FM_NAME_MAX+1];
    void *priv;                                         // platform detail impliment

    fm_s32 flag;                                        // timer active/inactive
    void (*timeout_func)(unsigned long data);		    //  timeout function
    unsigned long data;									// timeout function's parameter
    signed long timeout_ms;							    // timeout tick
    //Tx parameters
    volatile fm_u32    count;
    volatile fm_u8     tx_pwr_ctrl_en;
    volatile fm_u8     tx_rtc_ctrl_en;
    volatile fm_u8     tx_desense_en;

    //timer methods
    fm_s32(*init)(struct fm_timer *thiz, void (*timeout)(unsigned long data), unsigned long data, signed long time, fm_s32 flag);
    fm_s32(*start)(struct fm_timer *thiz);
    fm_s32(*update)(struct fm_timer *thiz);
    fm_s32(*stop)(struct fm_timer *thiz);
    fm_s32(*control)(struct fm_timer *thiz, enum fm_timer_ctrl cmd, void* arg);
};

extern struct fm_timer* fm_timer_create(const fm_s8 *name);

extern fm_s32 fm_timer_get(struct fm_timer *thiz);

extern fm_s32 fm_timer_put(struct fm_timer *thiz);

/*
 * FM work thread mechanism
 */
struct fm_work {
    fm_s32 ref;
    fm_s8  name[FM_NAME_MAX+1];
    void *priv;

    void (*work_func)(unsigned long data);
    unsigned long data;
    //work methods
    fm_s32(*init)(struct fm_work *thiz, void (*work_func)(unsigned long data), unsigned long data);
};

extern struct fm_work* fm_work_create(const fm_s8 *name);

extern fm_s32 fm_work_get(struct fm_work *thiz);

extern fm_s32 fm_work_put(struct fm_work *thiz);


struct fm_workthread {
    fm_s32 ref;
    fm_s8  name[FM_NAME_MAX+1];
    void *priv;

    //workthread methods
    fm_s32(*add_work)(struct fm_workthread *thiz, struct fm_work *work);
};

extern struct fm_workthread* fm_workthread_create(const fm_s8* name);

extern fm_s32 fm_workthread_get(struct fm_workthread *thiz);

extern fm_s32 fm_workthread_put(struct fm_workthread *thiz);

#endif //__FM_UTILS_H__

