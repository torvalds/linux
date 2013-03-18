/*
 * ---------------------------------------------------------------------------
 *  FILE:     os.c
 *
 *  PURPOSE:
 *      Routines to fulfil the OS-abstraction for the HIP lib.
 *      It is part of the porting exercise.
 *
 * Copyright (C) 2005-2009 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ---------------------------------------------------------------------------
 */

/**
 * The HIP lib OS abstraction consists of the implementation
 * of the functions in this file. It is part of the porting exercise.
 */

#include "unifi_priv.h"


/*
 * ---------------------------------------------------------------------------
 *  unifi_net_data_malloc
 *
 *      Allocate an OS specific net data buffer of "size" bytes.
 *      The bulk_data_slot.os_data_ptr must be initialised to point
 *      to the buffer allocated. The bulk_data_slot.length must be
 *      initialised to the requested size, zero otherwise.
 *      The bulk_data_slot.os_net_buf_ptr can be initialised to
 *      an OS specific pointer to be used in the unifi_net_data_free().
 *
 *
 *  Arguments:
 *      ospriv              Pointer to device private context struct.
 *      bulk_data_slot      Pointer to the bulk data structure to initialise.
 *      size                Size of the buffer to be allocated.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, CSR_RESULT_FAILURE otherwise.
 * ---------------------------------------------------------------------------
 */
CsrResult
unifi_net_data_malloc(void *ospriv, bulk_data_desc_t *bulk_data_slot, unsigned int size)
{
    struct sk_buff *skb;
    unifi_priv_t *priv = (unifi_priv_t*)ospriv;
    int rounded_length;

    if (priv->card_info.sdio_block_size == 0) {
        unifi_error(priv, "unifi_net_data_malloc: Invalid SDIO block size\n");
        return CSR_RESULT_FAILURE;
    }

    rounded_length = (size + priv->card_info.sdio_block_size - 1) & ~(priv->card_info.sdio_block_size - 1);

    /*
     * (ETH_HLEN + 2) bytes tailroom for header manipulation
     * CSR_WIFI_ALIGN_BYTES bytes headroom for alignment manipulation
     */
    skb = dev_alloc_skb(rounded_length + 2 + ETH_HLEN + CSR_WIFI_ALIGN_BYTES);
    if (! skb) {
        unifi_error(ospriv, "alloc_skb failed.\n");
        bulk_data_slot->os_net_buf_ptr = NULL;
        bulk_data_slot->net_buf_length = 0;
        bulk_data_slot->os_data_ptr = NULL;
        bulk_data_slot->data_length = 0;
        return CSR_RESULT_FAILURE;
    }

    bulk_data_slot->os_net_buf_ptr = (const unsigned char*)skb;
    bulk_data_slot->net_buf_length = rounded_length + 2 + ETH_HLEN + CSR_WIFI_ALIGN_BYTES;
    bulk_data_slot->os_data_ptr = (const void*)skb->data;
    bulk_data_slot->data_length = size;

    return CSR_RESULT_SUCCESS;
} /* unifi_net_data_malloc() */

/*
 * ---------------------------------------------------------------------------
 *  unifi_net_data_free
 *
 *      Free an OS specific net data buffer.
 *      The bulk_data_slot.length must be initialised to 0.
 *
 *
 *  Arguments:
 *      ospriv              Pointer to device private context struct.
 *      bulk_data_slot      Pointer to the bulk data structure that
 *                          holds the data to be freed.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
void
unifi_net_data_free(void *ospriv, bulk_data_desc_t *bulk_data_slot)
{
    struct sk_buff *skb;
    CSR_UNUSED(ospriv);

    skb = (struct sk_buff *)bulk_data_slot->os_net_buf_ptr;
    dev_kfree_skb(skb);

    bulk_data_slot->net_buf_length = 0;
    bulk_data_slot->data_length = 0;
    bulk_data_slot->os_data_ptr = bulk_data_slot->os_net_buf_ptr = NULL;

} /* unifi_net_data_free() */


/*
* ---------------------------------------------------------------------------
*  unifi_net_dma_align
*
*      DMA align an OS specific net data buffer.
*      The buffer must be empty.
*
*
*  Arguments:
*      ospriv              Pointer to device private context struct.
*      bulk_data_slot      Pointer to the bulk data structure that
*                          holds the data to be aligned.
*
*  Returns:
*      None.
* ---------------------------------------------------------------------------
*/
CsrResult
unifi_net_dma_align(void *ospriv, bulk_data_desc_t *bulk_data_slot)
{
    struct sk_buff *skb;
    unsigned long buf_address;
    int offset;
    unifi_priv_t *priv = (unifi_priv_t*)ospriv;

    if ((bulk_data_slot == NULL) || (CSR_WIFI_ALIGN_BYTES == 0)) {
        return CSR_RESULT_SUCCESS;
    }

    if ((bulk_data_slot->os_data_ptr == NULL) || (bulk_data_slot->data_length == 0)) {
        return CSR_RESULT_SUCCESS;
    }

    buf_address = (unsigned long)(bulk_data_slot->os_data_ptr) & (CSR_WIFI_ALIGN_BYTES - 1);

    unifi_trace(priv, UDBG5,
                "unifi_net_dma_align: Allign buffer (0x%p) by %d bytes\n",
                bulk_data_slot->os_data_ptr, buf_address);

    offset = CSR_WIFI_ALIGN_BYTES - buf_address;
    if (offset < 0) {
        unifi_error(priv, "unifi_net_dma_align: Failed (offset=%d)\n", offset);
        return CSR_RESULT_FAILURE;
    }

    skb = (struct sk_buff*)(bulk_data_slot->os_net_buf_ptr);
    skb_reserve(skb, offset);
    bulk_data_slot->os_net_buf_ptr = (const unsigned char*)skb;
    bulk_data_slot->os_data_ptr = (const void*)(skb->data);

    return CSR_RESULT_SUCCESS;

} /* unifi_net_dma_align() */

#ifdef ANDROID_TIMESTAMP
static volatile unsigned int printk_cpu = UINT_MAX;
char tbuf[30];

char* print_time(void )
{
    unsigned long long t;
    unsigned long nanosec_rem;

    t = cpu_clock(printk_cpu);
    nanosec_rem = do_div(t, 1000000000);
    sprintf(tbuf, "[%5lu.%06lu] ",
                    (unsigned long) t,
                    nanosec_rem / 1000);

    return tbuf;
}
#endif


/* Module parameters */
extern int unifi_debug;

#ifdef UNIFI_DEBUG
#define DEBUG_BUFFER_SIZE       120

#define FORMAT_TRACE(_s, _len, _args, _fmt)             \
    do {                                                \
        va_start(_args, _fmt);                          \
        _len += vsnprintf(&(_s)[_len],                  \
                         (DEBUG_BUFFER_SIZE - _len),    \
                         _fmt, _args);                  \
        va_end(_args);                                  \
        if (_len >= DEBUG_BUFFER_SIZE) {                \
            (_s)[DEBUG_BUFFER_SIZE - 2] = '\n';         \
            (_s)[DEBUG_BUFFER_SIZE - 1] = 0;            \
        }                                               \
    } while (0)

void
unifi_error(void* ospriv, const char *fmt, ...)
{
    unifi_priv_t *priv = (unifi_priv_t*) ospriv;
    char s[DEBUG_BUFFER_SIZE];
    va_list args;
    unsigned int len;
#ifdef ANDROID_TIMESTAMP
    if (priv != NULL) {
        len = snprintf(s, DEBUG_BUFFER_SIZE, KERN_ERR "%s unifi%d: ", print_time(), priv->instance);
    } else {
        len = snprintf(s, DEBUG_BUFFER_SIZE, KERN_ERR "%s unifi: ", print_time());
    }
#else
    if (priv != NULL) {
        len = snprintf(s, DEBUG_BUFFER_SIZE, KERN_ERR "unifi%d: ", priv->instance);
    } else {
        len = snprintf(s, DEBUG_BUFFER_SIZE, KERN_ERR "unifi: ");
    }
#endif /* ANDROID_TIMESTAMP */
    FORMAT_TRACE(s, len, args, fmt);

    printk("%s", s);
}

void
unifi_warning(void* ospriv, const char *fmt, ...)
{
    unifi_priv_t *priv = (unifi_priv_t*) ospriv;
    char s[DEBUG_BUFFER_SIZE];
    va_list args;
    unsigned int len;

#ifdef ANDROID_TIMESTAMP
    if (priv != NULL) {
        len = snprintf(s, DEBUG_BUFFER_SIZE, KERN_WARNING "%s unifi%d: ", print_time(), priv->instance);
    } else {
        len = snprintf(s, DEBUG_BUFFER_SIZE, KERN_WARNING "%s unifi: ", print_time());
    }
#else
    if (priv != NULL) {
        len = snprintf(s, DEBUG_BUFFER_SIZE, KERN_WARNING "unifi%d: ", priv->instance);
    } else {
        len = snprintf(s, DEBUG_BUFFER_SIZE, KERN_WARNING "unifi: ");
    }
#endif /* ANDROID_TIMESTAMP */

    FORMAT_TRACE(s, len, args, fmt);

    printk("%s", s);
}


void
unifi_notice(void* ospriv, const char *fmt, ...)
{
    unifi_priv_t *priv = (unifi_priv_t*) ospriv;
    char s[DEBUG_BUFFER_SIZE];
    va_list args;
    unsigned int len;

#ifdef ANDROID_TIMESTAMP
    if (priv != NULL) {
        len = snprintf(s, DEBUG_BUFFER_SIZE, KERN_NOTICE "%s unifi%d: ", print_time(), priv->instance);
    } else {
        len = snprintf(s, DEBUG_BUFFER_SIZE, KERN_NOTICE "%s unifi: ", print_time());
    }
#else
    if (priv != NULL) {
        len = snprintf(s, DEBUG_BUFFER_SIZE, KERN_NOTICE "unifi%d: ", priv->instance);
    } else {
        len = snprintf(s, DEBUG_BUFFER_SIZE, KERN_NOTICE "unifi: ");
    }
#endif /* ANDROID_TIMESTAMP */

    FORMAT_TRACE(s, len, args, fmt);

    printk("%s", s);
}


void
unifi_info(void* ospriv, const char *fmt, ...)
{
    unifi_priv_t *priv = (unifi_priv_t*) ospriv;
    char s[DEBUG_BUFFER_SIZE];
    va_list args;
    unsigned int len;

#ifdef ANDROID_TIMESTAMP
    if (priv != NULL) {
        len = snprintf(s, DEBUG_BUFFER_SIZE, KERN_INFO "%s unifi%d: ", print_time(), priv->instance);
    } else {
        len = snprintf(s, DEBUG_BUFFER_SIZE, KERN_INFO "%s unifi: ", print_time());
    }
#else
    if (priv != NULL) {
        len = snprintf(s, DEBUG_BUFFER_SIZE, KERN_INFO "unifi%d: ", priv->instance);
    } else {
        len = snprintf(s, DEBUG_BUFFER_SIZE, KERN_INFO "unifi: ");
    }
#endif /* ANDROID_TIMESTAMP */

    FORMAT_TRACE(s, len, args, fmt);

    printk("%s", s);
}

/* debugging */
void
unifi_trace(void* ospriv, int level, const char *fmt, ...)
{
    unifi_priv_t *priv = (unifi_priv_t*) ospriv;
    char s[DEBUG_BUFFER_SIZE];
    va_list args;
    unsigned int len;

    if (unifi_debug >= level) {
#ifdef ANDROID_TIMESTAMP
        if (priv != NULL) {
            len = snprintf(s, DEBUG_BUFFER_SIZE, KERN_ERR "%s unifi%d: ", print_time(), priv->instance);
        } else {
            len = snprintf(s, DEBUG_BUFFER_SIZE, KERN_ERR "%s unifi: ", print_time());
        }
#else
        if (priv != NULL) {
            len = snprintf(s, DEBUG_BUFFER_SIZE, KERN_ERR "unifi%d: ", priv->instance);
        } else {
            len = snprintf(s, DEBUG_BUFFER_SIZE, KERN_ERR "unifi: ");
        }
#endif /* ANDROID_TIMESTAMP */

        FORMAT_TRACE(s, len, args, fmt);

        printk("%s", s);
    }
}

#else

void
unifi_error_nop(void* ospriv, const char *fmt, ...)
{
}

void
unifi_trace_nop(void* ospriv, int level, const char *fmt, ...)
{
}

#endif /* UNIFI_DEBUG */


/*
 * ---------------------------------------------------------------------------
 *
 *      Debugging support.
 *
 * ---------------------------------------------------------------------------
 */

#ifdef UNIFI_DEBUG

/* Memory dump with level filter controlled by unifi_debug */
void
unifi_dump(void *ospriv, int level, const char *msg, void *mem, u16 len)
{
    unifi_priv_t *priv = (unifi_priv_t*) ospriv;

    if (unifi_debug >= level) {
#ifdef ANDROID_TIMESTAMP
        if (priv != NULL) {
            printk(KERN_ERR "%s unifi%d: --- dump: %s ---\n", print_time(), priv->instance, msg ? msg : "");
        } else {
            printk(KERN_ERR "%s unifi: --- dump: %s ---\n", print_time(), msg ? msg : "");
        }
#else
        if (priv != NULL) {
            printk(KERN_ERR "unifi%d: --- dump: %s ---\n", priv->instance, msg ? msg : "");
        } else {
            printk(KERN_ERR "unifi: --- dump: %s ---\n", msg ? msg : "");
        }
#endif /* ANDROID_TIMESTAMP */
        dump(mem, len);

        if (priv != NULL) {
            printk(KERN_ERR "unifi%d: --- end of dump ---\n", priv->instance);
        } else {
            printk(KERN_ERR "unifi: --- end of dump ---\n");
        }
    }
}

/* Memory dump that appears all the time, use sparingly */
void
dump(void *mem, u16 len)
{
    int i, col = 0;
    unsigned char *pdata = (unsigned char *)mem;
#ifdef ANDROID_TIMESTAMP
    printk("timestamp %s \n", print_time());
#endif /* ANDROID_TIMESTAMP */
    if (mem == NULL) {
        printk("(null dump)\n");
        return;
    }
    for (i = 0; i < len; i++) {
        if (col == 0)
            printk("0x%02X: ", i);

        printk(" %02X", pdata[i]);

        if (++col == 16) {
            printk("\n");
            col = 0;
        }
    }
    if (col)
        printk("\n");
} /* dump() */


void
dump16(void *mem, u16 len)
{
    int i, col=0;
    unsigned short *p = (unsigned short *)mem;
#ifdef ANDROID_TIMESTAMP
    printk("timestamp %s \n", print_time());
#endif /* ANDROID_TIMESTAMP */
    for (i = 0; i < len; i+=2) {
        if (col == 0)
            printk("0x%02X: ", i);

        printk(" %04X", *p++);

        if (++col == 8) {
            printk("\n");
            col = 0;
        }
    }
    if (col)
        printk("\n");
}


#ifdef CSR_WIFI_HIP_DEBUG_OFFLINE
void
dump_str(void *mem, u16 len)
{
    int i;
    unsigned char *pdata = (unsigned char *)mem;
#ifdef ANDROID_TIMESTAMP
    printk("timestamp %s \n", print_time());
#endif /* ANDROID_TIMESTAMP */
    for (i = 0; i < len; i++) {
        printk("%c", pdata[i]);
    }
	printk("\n");

} /* dump_str() */
#endif /* CSR_ONLY_NOTES */


#endif /* UNIFI_DEBUG */


/* ---------------------------------------------------------------------------
 *                              - End -
 * ------------------------------------------------------------------------- */
