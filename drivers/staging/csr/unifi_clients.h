/*
 *****************************************************************************
 *
 * FILE : unifi_clients.h
 *
 * PURPOSE : Private header file for unifi clients.
 *
 *           UDI = UniFi Debug Interface
 *
 * Copyright (C) 2005-2008 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 *****************************************************************************
 */
#ifndef __LINUX_UNIFI_CLIENTS_H__
#define __LINUX_UNIFI_CLIENTS_H__ 1

#include <linux/kernel.h>

#define MAX_UDI_CLIENTS 8

/* The start of the range of process ids allocated for ul clients */
#define UDI_SENDER_ID_BASE      0xC000
#define UDI_SENDER_ID_SHIFT     8


/* Structure to hold a UDI logged signal */
typedef struct {

    /* List link structure */
    struct list_head q;

    /* The message that will be passed to the user app */
    udi_msg_t msg;

    /* Signal body and data follow */

} udi_log_t;



typedef struct ul_client ul_client_t;

typedef void (*udi_event_t)(ul_client_t *client,
                            const u8 *sigdata, int signal_len,
                            const bulk_data_param_t *bulkdata,
                            int dir);

void logging_handler(void *ospriv,
                     u8 *sigdata, CsrUint32 signal_len,
                     const bulk_data_param_t *bulkdata,
                     enum udi_log_direction direction);


/*
 * Structure describing a bulk data slot.
 * The length field is used to indicate empty/occupied state.
 */
typedef struct _bulk_data
{
    unsigned char ptr[2000];
    unsigned int length;
} bulk_data_t;


struct ul_client {
    /* Index of this client in the ul_clients array. */
    int client_id;

    /* Index of UniFi device to which this client is attached. */
    int instance;

    /* Flag to say whether this client has been enabled. */
    int udi_enabled;

    /* Value to use in signal->SenderProcessId */
    int sender_id;

    /* Configuration flags, e.g blocking, logging, etc. */
    unsigned int configuration;

    udi_event_t event_hook;

    /* A list to hold signals received from UniFi for reading by read() */
    struct list_head udi_log;

    /* Semaphore to protect the udi_log list */
    struct semaphore udi_sem;

    /*
     * Linux waitqueue to support blocking read and poll.
     * Logging clients should wait on udi_log. while
     * blocking clients should wait on wake_up_wq.
     */
    wait_queue_head_t udi_wq;
    CSR_SIGNAL* reply_signal;
    bulk_data_t* reply_bulkdata[UNIFI_MAX_DATA_REFERENCES];

    u16 signal_filter[SIG_FILTER_SIZE];


    /* ------------------------------------------------------------------- */
    /* Code below here is used by the sme_native configuration only */

    /* Flag to wake up blocking clients waiting on udi_wq. */
    int wake_up_wq_id;

    /*
     * A 0x00 - 0x0F mask to apply in signal->SenderProcessId.
     * Every time we do a blocking mlme request we increase this value.
     * The mlme_wait_for_reply() will wait for this sequence number.
     * Only the MLME blocking functions update this field.
     */
    unsigned char seq_no;

    /*
     * A 0x00 - 0x0F counter, containing the sequence number of
     * the signal that this client has last received.
     * Only the MLME blocking functions update this field.
     */
    unsigned char wake_seq_no;

    unifiio_snap_filter_t snap_filter;
}; /* struct ul_client */


#endif /* __LINUX_UNIFI_CLIENTS_H__ */
