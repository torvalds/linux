#include "slave_log_buf.h"

spinlock_t lock;

uint32_t ring_buffer_init(struct ring_buffer* ring_buf, uint32_t size)
{
    if(ring_buf== NULL) 
        return false;

    if (!is_power_of_2(size))
    {
        printk("size must be power of 2.\n");
        return false;
    }

    memset(ring_buf, 0, sizeof(struct ring_buffer));
    spin_lock_init(&lock);
    ring_buf->buffer = kmalloc(size,GFP_KERNEL);
    ring_buf->size = size;
    ring_buf->write_point = 0;
    ring_buf->read_point = 0;
    ring_buf->f_lock = &lock;
    ring_buf->init = true;
    ring_buf->cover = false;
    ring_buf->show = false;
    return true;
}

void ring_buffer_deinit(struct ring_buffer *ring_buf)
{
    memset(ring_buf, 0, sizeof(struct ring_buffer));
    if(ring_buf->buffer != NULL)
    {
        kfree(ring_buf->buffer);
        ring_buf->buffer = NULL;
    }

}
 

uint32_t __ring_buffer_len(const struct ring_buffer *ring_buf)
{
    if(ring_buf->cover == false)
    {
        return ring_buf->write_point;
    }
    if(ring_buf->show == true)
    {
        if(ring_buf->write_point < ring_buf->read_point)
            return (ring_buf->write_point + ring_buf->size - ring_buf->read_point);
        else
            return (ring_buf->write_point - ring_buf->read_point);
    }
    
    return  ring_buf->size;
}
 

uint32_t __ring_buffer_get(struct ring_buffer *ring_buf, void * buffer, uint32_t size)
{
    if((ring_buf== NULL) || (buffer== NULL))
        return 0;

    uint32_t copy_len = 0;
    uint32_t read_len = 0;
    if(ring_buf->write_point < ring_buf->read_point)
        read_len = (ring_buf->write_point + ring_buf->size - ring_buf->read_point);
    else
        read_len = (ring_buf->write_point - ring_buf->read_point);

    size  = min(size, read_len);        
    /* first get the data from fifo->read_point until the end of the buffer */
    copy_len = min(size, ring_buf->size - ring_buf->read_point);
    memcpy(buffer, ring_buf->buffer + ring_buf->read_point, copy_len);
    /* then get the rest (if any) from the beginning of the buffer */
    if(size - copy_len > 0)
    {
        memcpy(buffer + copy_len, ring_buf->buffer, size - copy_len);
    }

    ring_buf->read_point += size;
    ring_buf->read_point = (ring_buf->read_point & (ring_buf->size - 1));

    return size;
}
//向缓冲区中存放数据
uint32_t __ring_buffer_put(struct ring_buffer *ring_buf, void *buffer, uint32_t size)
{
    if((ring_buf == NULL) || (buffer == NULL))
    {
        return 0;
    }
    uint32_t copy_len = 0;

    /* first put the data starting from fifo->write_point to buffer end */
    copy_len  = min(size, ring_buf->size - ring_buf->write_point);

    memcpy(ring_buf->buffer + ring_buf->write_point, buffer, copy_len);
    /* then put the rest (if any) at the beginning of the buffer */
    if(size - copy_len > 0)
    {
        memcpy(ring_buf->buffer, buffer + copy_len, size - copy_len);
        ring_buf->cover = true;
    }

    ring_buf->write_point += size;
    ring_buf->write_point = (ring_buf->write_point & (ring_buf->size - 1));
    return size;
}
 
uint32_t ring_buffer_len(const struct ring_buffer *ring_buf)
{
    uint32_t len = 0;
    spin_lock_irq(ring_buf->f_lock);
    len = __ring_buffer_len(ring_buf);
    spin_unlock_irq(ring_buf->f_lock);
    return len;
}
 
uint32_t ring_buffer_get(struct ring_buffer *ring_buf, void *buffer, uint32_t size)
{
    uint32_t ret;
    spin_lock_irq(ring_buf->f_lock);
    ret = __ring_buffer_get(ring_buf, buffer, size);
    spin_unlock_irq(ring_buf->f_lock);
    return ret;
}
 
uint32_t ring_buffer_put(struct ring_buffer *ring_buf, void *buffer, uint32_t size)
{
    uint32_t ret;
    spin_lock_irq(ring_buf->f_lock);
    ret = __ring_buffer_put(ring_buf, buffer, size);
    spin_unlock_irq(ring_buf->f_lock);
    return ret;
}
uint32_t ring_buffer_scrolling_display(struct ring_buffer *ring_buf, char show)
{
    uint32_t ret = true;
    spin_lock_irq(ring_buf->f_lock);
    ring_buf->show = show;
    if((ring_buf->cover == true)&&(show == true))
    {
        ring_buf->read_point = (ring_buf->write_point & (ring_buf->size - 1)) + 1;
    }
    spin_unlock_irq(ring_buf->f_lock);
    return ret;
}

