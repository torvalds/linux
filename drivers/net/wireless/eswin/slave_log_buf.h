#ifndef _SLAVE_LOG_BUF_H_
#define _SLAVE_LOG_BUF_H_
#include <linux/firmware.h>
#include "core.h"
#include "ecrnx_debug.h"


#ifndef uint32_t
#define uint32_t unsigned int
#endif


#define is_power_of_2(x) ((x) != 0 && (((x) & ((x) - 1)) == 0))

//#define min(a, b) (((a) < (b)) ? (a) : (b))
 
struct ring_buffer
{
    void            *buffer;
    uint32_t        size;
    uint32_t        write_point;
    uint32_t        read_point;
    char            cover; 
    char            show;
    char            init; 
    spinlock_t     *f_lock;
};

uint32_t ring_buffer_get(struct ring_buffer *ring_buf, void *buffer, uint32_t size);
uint32_t ring_buffer_put(struct ring_buffer *ring_buf, void *buffer, uint32_t size);
uint32_t ring_buffer_init(struct ring_buffer* ring_buf, uint32_t size);
void ring_buffer_deinit(struct ring_buffer *ring_buf);
uint32_t ring_buffer_scrolling_display(struct ring_buffer *ring_buf, char show);
uint32_t ring_buffer_len(const struct ring_buffer *ring_buf);


#endif
