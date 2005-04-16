#ifndef IEEE1394_RAW1394_PRIVATE_H
#define IEEE1394_RAW1394_PRIVATE_H

/* header for definitions that are private to the raw1394 driver
   and not visible to user-space */

#define RAW1394_DEVICE_MAJOR      171
#define RAW1394_DEVICE_NAME       "raw1394"

#define RAW1394_MAX_USER_CSR_DIRS	16

struct iso_block_store {
        atomic_t refcount;
        size_t data_size;
        quadlet_t data[0];
};

enum raw1394_iso_state { RAW1394_ISO_INACTIVE = 0,
			 RAW1394_ISO_RECV = 1,
			 RAW1394_ISO_XMIT = 2 };

struct file_info {
        struct list_head list;

        enum { opened, initialized, connected } state;
        unsigned int protocol_version;

        struct hpsb_host *host;

        struct list_head req_pending;
        struct list_head req_complete;
        struct semaphore complete_sem;
        spinlock_t reqlists_lock;
        wait_queue_head_t poll_wait_complete;

        struct list_head addr_list;

        u8 __user *fcp_buffer;

	/* old ISO API */
        u64 listen_channels;
        quadlet_t __user *iso_buffer;
        size_t iso_buffer_length;

        u8 notification; /* (busreset-notification) RAW1394_NOTIFY_OFF/ON */

	/* new rawiso API */
	enum raw1394_iso_state iso_state;
	struct hpsb_iso *iso_handle;

	/* User space's CSR1212 dynamic ConfigROM directories */
	struct csr1212_keyval *csr1212_dirs[RAW1394_MAX_USER_CSR_DIRS];

	/* Legacy ConfigROM update flag */
	u8 cfgrom_upd;
};

struct arm_addr {
        struct list_head addr_list; /* file_info list */
        u64    start, end;
        u64    arm_tag;
        u8     access_rights;
        u8     notification_options;
        u8     client_transactions;
        u64    recvb;
        u16    rec_length;
        u8     *addr_space_buffer; /* accessed by read/write/lock */
};

struct pending_request {
        struct list_head list;
        struct file_info *file_info;
        struct hpsb_packet *packet;
        struct iso_block_store *ibs;
        quadlet_t *data;
        int free_data;
        struct raw1394_request req;
};

struct host_info {
        struct list_head list;
        struct hpsb_host *host;
        struct list_head file_info_list;
};

#endif  /* IEEE1394_RAW1394_PRIVATE_H */
