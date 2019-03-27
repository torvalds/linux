/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*****************************************************************************
 *                                                                           *
 * These structures should be considered PRIVATE to the record layer. No     *
 * non-record layer code should be using these structures in any way.        *
 *                                                                           *
 *****************************************************************************/

typedef struct ssl3_buffer_st {
    /* at least SSL3_RT_MAX_PACKET_SIZE bytes, see ssl3_setup_buffers() */
    unsigned char *buf;
    /* default buffer size (or 0 if no default set) */
    size_t default_len;
    /* buffer size */
    size_t len;
    /* where to 'copy from' */
    size_t offset;
    /* how many bytes left */
    size_t left;
} SSL3_BUFFER;

#define SEQ_NUM_SIZE                            8

typedef struct ssl3_record_st {
    /* Record layer version */
    /* r */
    int rec_version;
    /* type of record */
    /* r */
    int type;
    /* How many bytes available */
    /* rw */
    size_t length;
    /*
     * How many bytes were available before padding was removed? This is used
     * to implement the MAC check in constant time for CBC records.
     */
    /* rw */
    size_t orig_len;
    /* read/write offset into 'buf' */
    /* r */
    size_t off;
    /* pointer to the record data */
    /* rw */
    unsigned char *data;
    /* where the decode bytes are */
    /* rw */
    unsigned char *input;
    /* only used with decompression - malloc()ed */
    /* r */
    unsigned char *comp;
    /* Whether the data from this record has already been read or not */
    /* r */
    unsigned int read;
    /* epoch number, needed by DTLS1 */
    /* r */
    unsigned long epoch;
    /* sequence number, needed by DTLS1 */
    /* r */
    unsigned char seq_num[SEQ_NUM_SIZE];
} SSL3_RECORD;

typedef struct dtls1_bitmap_st {
    /* Track 32 packets on 32-bit systems and 64 - on 64-bit systems */
    unsigned long map;
    /* Max record number seen so far, 64-bit value in big-endian encoding */
    unsigned char max_seq_num[SEQ_NUM_SIZE];
} DTLS1_BITMAP;

typedef struct record_pqueue_st {
    unsigned short epoch;
    struct pqueue_st *q;
} record_pqueue;

typedef struct dtls1_record_data_st {
    unsigned char *packet;
    size_t packet_length;
    SSL3_BUFFER rbuf;
    SSL3_RECORD rrec;
#ifndef OPENSSL_NO_SCTP
    struct bio_dgram_sctp_rcvinfo recordinfo;
#endif
} DTLS1_RECORD_DATA;

typedef struct dtls_record_layer_st {
    /*
     * The current data and handshake epoch.  This is initially
     * undefined, and starts at zero once the initial handshake is
     * completed
     */
    unsigned short r_epoch;
    unsigned short w_epoch;
    /* records being received in the current epoch */
    DTLS1_BITMAP bitmap;
    /* renegotiation starts a new set of sequence numbers */
    DTLS1_BITMAP next_bitmap;
    /* Received handshake records (processed and unprocessed) */
    record_pqueue unprocessed_rcds;
    record_pqueue processed_rcds;
    /*
     * Buffered application records. Only for records between CCS and
     * Finished to prevent either protocol violation or unnecessary message
     * loss.
     */
    record_pqueue buffered_app_data;
    /* save last and current sequence numbers for retransmissions */
    unsigned char last_write_sequence[8];
    unsigned char curr_write_sequence[8];
} DTLS_RECORD_LAYER;

/*****************************************************************************
 *                                                                           *
 * This structure should be considered "opaque" to anything outside of the   *
 * record layer. No non-record layer code should be accessing the members of *
 * this structure.                                                           *
 *                                                                           *
 *****************************************************************************/

typedef struct record_layer_st {
    /* The parent SSL structure */
    SSL *s;
    /*
     * Read as many input bytes as possible (for
     * non-blocking reads)
     */
    int read_ahead;
    /* where we are when reading */
    int rstate;
    /* How many pipelines can be used to read data */
    size_t numrpipes;
    /* How many pipelines can be used to write data */
    size_t numwpipes;
    /* read IO goes into here */
    SSL3_BUFFER rbuf;
    /* write IO goes into here */
    SSL3_BUFFER wbuf[SSL_MAX_PIPELINES];
    /* each decoded record goes in here */
    SSL3_RECORD rrec[SSL_MAX_PIPELINES];
    /* used internally to point at a raw packet */
    unsigned char *packet;
    size_t packet_length;
    /* number of bytes sent so far */
    size_t wnum;
    unsigned char handshake_fragment[4];
    size_t handshake_fragment_len;
    /* The number of consecutive empty records we have received */
    size_t empty_record_count;
    /* partial write - check the numbers match */
    /* number bytes written */
    size_t wpend_tot;
    int wpend_type;
    /* number of bytes submitted */
    size_t wpend_ret;
    const unsigned char *wpend_buf;
    unsigned char read_sequence[SEQ_NUM_SIZE];
    unsigned char write_sequence[SEQ_NUM_SIZE];
    /* Set to true if this is the first record in a connection */
    unsigned int is_first_record;
    /* Count of the number of consecutive warning alerts received */
    unsigned int alert_count;
    DTLS_RECORD_LAYER *d;
} RECORD_LAYER;

/*****************************************************************************
 *                                                                           *
 * The following macros/functions represent the libssl internal API to the   *
 * record layer. Any libssl code may call these functions/macros             *
 *                                                                           *
 *****************************************************************************/

#define MIN_SSL2_RECORD_LEN     9

#define RECORD_LAYER_set_read_ahead(rl, ra)     ((rl)->read_ahead = (ra))
#define RECORD_LAYER_get_read_ahead(rl)         ((rl)->read_ahead)
#define RECORD_LAYER_get_packet(rl)             ((rl)->packet)
#define RECORD_LAYER_get_packet_length(rl)      ((rl)->packet_length)
#define RECORD_LAYER_add_packet_length(rl, inc) ((rl)->packet_length += (inc))
#define DTLS_RECORD_LAYER_get_w_epoch(rl)       ((rl)->d->w_epoch)
#define DTLS_RECORD_LAYER_get_processed_rcds(rl) \
                                                ((rl)->d->processed_rcds)
#define DTLS_RECORD_LAYER_get_unprocessed_rcds(rl) \
                                                ((rl)->d->unprocessed_rcds)
#define RECORD_LAYER_get_rbuf(rl)               (&(rl)->rbuf)
#define RECORD_LAYER_get_wbuf(rl)               ((rl)->wbuf)

void RECORD_LAYER_init(RECORD_LAYER *rl, SSL *s);
void RECORD_LAYER_clear(RECORD_LAYER *rl);
void RECORD_LAYER_release(RECORD_LAYER *rl);
int RECORD_LAYER_read_pending(const RECORD_LAYER *rl);
int RECORD_LAYER_processed_read_pending(const RECORD_LAYER *rl);
int RECORD_LAYER_write_pending(const RECORD_LAYER *rl);
void RECORD_LAYER_reset_read_sequence(RECORD_LAYER *rl);
void RECORD_LAYER_reset_write_sequence(RECORD_LAYER *rl);
int RECORD_LAYER_is_sslv2_record(RECORD_LAYER *rl);
size_t RECORD_LAYER_get_rrec_length(RECORD_LAYER *rl);
__owur size_t ssl3_pending(const SSL *s);
__owur int ssl3_write_bytes(SSL *s, int type, const void *buf, size_t len,
                            size_t *written);
int do_ssl3_write(SSL *s, int type, const unsigned char *buf,
                  size_t *pipelens, size_t numpipes,
                  int create_empty_fragment, size_t *written);
__owur int ssl3_read_bytes(SSL *s, int type, int *recvd_type,
                           unsigned char *buf, size_t len, int peek,
                           size_t *readbytes);
__owur int ssl3_setup_buffers(SSL *s);
__owur int ssl3_enc(SSL *s, SSL3_RECORD *inrecs, size_t n_recs, int send);
__owur int n_ssl3_mac(SSL *ssl, SSL3_RECORD *rec, unsigned char *md, int send);
__owur int ssl3_write_pending(SSL *s, int type, const unsigned char *buf, size_t len,
                              size_t *written);
__owur int tls1_enc(SSL *s, SSL3_RECORD *recs, size_t n_recs, int send);
__owur int tls1_mac(SSL *ssl, SSL3_RECORD *rec, unsigned char *md, int send);
__owur int tls13_enc(SSL *s, SSL3_RECORD *recs, size_t n_recs, int send);
int DTLS_RECORD_LAYER_new(RECORD_LAYER *rl);
void DTLS_RECORD_LAYER_free(RECORD_LAYER *rl);
void DTLS_RECORD_LAYER_clear(RECORD_LAYER *rl);
void DTLS_RECORD_LAYER_set_saved_w_epoch(RECORD_LAYER *rl, unsigned short e);
void DTLS_RECORD_LAYER_clear(RECORD_LAYER *rl);
void DTLS_RECORD_LAYER_set_write_sequence(RECORD_LAYER *rl, unsigned char *seq);
__owur int dtls1_read_bytes(SSL *s, int type, int *recvd_type,
                            unsigned char *buf, size_t len, int peek,
                            size_t *readbytes);
__owur int dtls1_write_bytes(SSL *s, int type, const void *buf, size_t len,
                             size_t *written);
int do_dtls1_write(SSL *s, int type, const unsigned char *buf,
                   size_t len, int create_empty_fragment, size_t *written);
void dtls1_reset_seq_numbers(SSL *s, int rw);
int dtls_buffer_listen_record(SSL *s, size_t len, unsigned char *seq,
                              size_t off);
